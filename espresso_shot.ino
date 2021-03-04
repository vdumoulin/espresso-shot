/*
  Code for an Arduino-based espresso shot management device for E61 grouphead
  machines. Highly inspired by Howard Smith's setup (https://
  www.home-barista.com/tips/guide-to-managing-hx-brew-temperatures-t64840.html).

  The device performs the following:

  - Monitors basket and group temperatures using thermistors. The group
    thermistor is inserted in an adapter screwed into the M6 closure plug (e.g.
    https://www.home-barista.com/espresso-machines/monitoring-brew-temperature-
    e61-groups-t1352.html). The basket thermistor is wrapped in foil tape and
    sandwiched between the portafilter basket and the grouphead.
  - Times the shot using a tilt switch taped to the brew lever.
  - Controls a DC fan which cools the grouphead to a target temperature. The
    target temperature is user-selectable with a potentiometer.
  - Displays basket temperature, group temperature, and shot time on an OLED
    screen.
  - Sends time and temperature logging information over serial. A companion
    Python script listens to the serial channel and converts the information
    into JSON files that a companion Jupyter Notebook can then read and display.
*/

#include <Adafruit_ADS1015.h>
#include <U8g2lib.h>
#include <Wire.h>

// Number of times per second that we read temperatures.
#define SENSING_FREQUENCY 100

// Temperatures are averaged over a certain time horizon to reduce noise. By
// default we set it to the sensing frequency so that we get an average over the
// previous second.
#define BUFFER_SIZE SENSING_FREQUENCY

// We use the Steinhart-Hart model to characterize the relationship between
// thermistor resistance and temperature. The coefficients A, B, and C are
// calculated empirically on three temperature-resistance pairs using an online
// calculator (e.g. https://www.thinksrs.com/downloads/programs/therm%20calc/
// ntccalibrator/ntccalculator.html).
#define BASKET_SH_A 0.1184570776e-3
#define BASKET_SH_B 2.786552243e-4
#define BASKET_SH_C 0.1957826559e-7
#define GROUP_SH_A 0.7871221906e-3
#define GROUP_SH_B 2.039769726e-4
#define GROUP_SH_C 1.469069094e-7

// The basket and group thermistors and the shot timer switch are connected to
// an ADS1115's channels 1, 2, and 3 (respectively) using a pull-up resistor
// configuration. The known resistance values for the basket and group voltage
// divider circuits are measured empirically. The reference voltage is measured
// on channel 0.
#define REFERENCE_VOLTAGE_CHANNEL 0
#define BASKET_VOLTAGE_CHANNEL 1
#define BASKET_KNOWN_RESISTANCE 9940.0
#define GROUP_VOLTAGE_CHANNEL 2
#define GROUP_KNOWN_RESISTANCE 9940.0
#define TILT_CHANNEL 3

// The cooling fan attempts to keep the grouphead at the target temperature.
// A potentiometer allows to adjust that target temperature.
#define FAN_PIN 13
#define TARGET_TEMPERATURE_PIN A0
#define TARGET_TEMPERATURE_MIN 88.0
#define TARGET_TEMPERATURE_MAX 98.0

// Number of times per second that we refresh the display.
#define DISPLAY_FREQUENCY 4

// Number of milliseconds to display the target temperature for when it changes.
#define TARGET_DISPLAY_TIME 1000

// Compute sensing and display periods to simplify implementation.
constexpr unsigned short SENSING_PERIOD = 1000.0 / SENSING_FREQUENCY;
constexpr unsigned short DISPLAY_PERIOD = 1000.0 / DISPLAY_FREQUENCY;

// We use an ADS1115 for data acquisition and an OLED screen to display
// information.
Adafruit_ADS1115 ads1115;
U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0);

// We use circular buffers to compute basket and group temperature averages over
// a certain time horizon determined by SENSOR_FREQUENCY and BUFFER_SIZE.
int latest_buffer_index;
float basket_temperature_buffer[BUFFER_SIZE];
float group_temperature_buffer[BUFFER_SIZE];

// Temperature buffer averages.
float current_basket_temperature;
float current_group_temperature;

// Selected target group temperature.
float target_group_temperature;

// The start time is used with millis() to determine the elapsed time. When the
// machine is not running we display the previous shot time that was recorded
// into elapsed_time.
unsigned long start_time;
float elapsed_time;

// Whether the machine is currently running.
bool running;

// Historical device state.
bool previously_running;
unsigned long last_temperature_measurement;
unsigned long last_display_refresh;
unsigned long last_target_change;

// Utilities for sending measurements over the serial port.
enum MachineState { START, RUNNING, STOP, STOPPED };

struct Measurement {
  float elapsed_time;
  float basket_temperature;
  float group_temperature;
  unsigned int state;
} measurement;

void setup() {
  // Initialize devices and pins.
  Serial.begin(9600);
  ads1115.begin();
  u8g2.begin();
  pinMode(FAN_PIN, OUTPUT);

  // Initialize running state.
  running = false;
  previously_running = false;

  // Initialize temperatures.
  target_group_temperature = read_target_temperature();
  current_basket_temperature = read_basket_temperature();
  current_group_temperature = read_group_temperature();
  
  latest_buffer_index = 0;
  
  for (int i = 0; i < BUFFER_SIZE; ++i) {
    basket_temperature_buffer[i] = current_basket_temperature;
    group_temperature_buffer[i] = current_group_temperature;
  }
  
  // Initialize time.
  start_time = millis();
  last_display_refresh = start_time;
  last_target_change = start_time;
  elapsed_time = 0.0;
}

void loop() {
  // Determine the running state of the machine, as determined by the tilt
  // switch.
  running = read_voltage(TILT_CHANNEL) > 3.0;

  unsigned long current_time = millis();

  // When a state transition from "stopped" to "running" occurs, reset the
  // elapsed time and start the timer.
  if (running && !previously_running) {
    start_time = current_time;
    elapsed_time = 0.0;
  }

  // When the machine is running or has just stopped, update the timer.
  if (running || previously_running) {
    elapsed_time = (current_time - start_time) / 1000.0;
  }

  // Measure temperatures when the sensing period has passed.
  if (current_time > last_temperature_measurement + SENSING_PERIOD) {
    update_temperatures();
    last_temperature_measurement = current_time;
    write_measurement();
  }

  // Read the target group temperature set by the user. If it has changed,
  // update the target group temperature and reset the timer for displaying
  // that information instead of the group temperature.
  float new_target_temperature = read_target_temperature();
  // Target temperatures are rounded to the nearest multiple of 0.5, so to be
  // extra careful about determining equality on floats we double the numbers
  // and cast them as int before doing the comparison.
  if ((int) 2 * new_target_temperature != (int) 2 * target_group_temperature) {
    target_group_temperature = new_target_temperature;
    last_target_change = current_time;
  }

  // We cool the grouphead until it reaches the target temperature. We could
  // eventually dampen the temperature swings by implementing PID control, but
  // for now this is good enough.
  digitalWrite(
      FAN_PIN,
      current_group_temperature > target_group_temperature ?  HIGH : LOW
  );

  // Refresh the display when the refresh period has passed.
  if (current_time > last_display_refresh + DISPLAY_PERIOD) {
    refresh_display();
    last_display_refresh = current_time;
  }

  previously_running = running;
}

// Writes a measurement to the serial port.
void write_measurement() {
  measurement.elapsed_time = elapsed_time;
  measurement.basket_temperature = basket_temperature_buffer[latest_buffer_index];
  measurement.group_temperature = group_temperature_buffer[latest_buffer_index];
  if (running) {
    measurement.state = previously_running ? RUNNING : START;
  } else {
    measurement.state = previously_running ? STOP : STOPPED;
  }
  Serial.write((byte *) &measurement, sizeof(measurement));
}

// Reads the target group temperature set by the user on the potentiometer.
// Target temperatures are rounded to the nearest multiple of 0.5.
float read_target_temperature() {
  // We want to round to the nearest multiple of 0.5, so instead we map to
  // double the (integer) range and divide by 2.
  int double_target_temperature = map(
      analogRead(TARGET_TEMPERATURE_PIN), 0, 1023, 180, 200
  );
  return double_target_temperature / 2.0;
}

// Reads and returns the voltage at the specified ADC channel.
float read_voltage(uint8_t channel) {
  return 0.0001875 * ads1115.readADC_SingleEnded(channel);
}

// Reads and returns a thermistor's temperature at the specified ADC channel
// given the specified known resistance and Steinhart-Hard coefficients.
float read_temperature(uint8_t channel, float known_resistance,
                       float sh_a, float sh_b, float sh_c) {
  // Infer the resistance from the voltage divider circuit.
  float reference_voltage = read_voltage(REFERENCE_VOLTAGE_CHANNEL);
  float voltage = read_voltage(channel);
  float resistance = known_resistance / ((reference_voltage / voltage) - 1.0);

  // We use the Steinhart-Hart model to infer the temperature (in degrees
  // Kelvin) from the thermistor's resistance.
  float inverse_temperature_kelvin = (
    sh_a + sh_b * log(resistance) + sh_c * pow(log(resistance), 3)
  );

  return 1.0 / inverse_temperature_kelvin - 273.15;
}

// Reads the basket temperature from its corresponding thermistor. Wraps
// read_temperature for convenience.
float read_basket_temperature() {
  return read_temperature(BASKET_VOLTAGE_CHANNEL, BASKET_KNOWN_RESISTANCE,
                          BASKET_SH_A, BASKET_SH_B, BASKET_SH_C);
}

// Reads the group temperature from its corresponding thermistor. Wraps
// read_temperature for convenience.
float read_group_temperature() {
  return read_temperature(GROUP_VOLTAGE_CHANNEL, GROUP_KNOWN_RESISTANCE,
                          GROUP_SH_A, GROUP_SH_B, GROUP_SH_C);
}

// Updates the basket and group temperature buffers and recomputes the average
// basket and group temperatures.
void update_temperatures() {
  // Update temperature buffers.
  latest_buffer_index = (latest_buffer_index + 1) % BUFFER_SIZE;
  basket_temperature_buffer[latest_buffer_index] = read_basket_temperature();
  group_temperature_buffer[latest_buffer_index] = read_group_temperature();

  // Compute temperature averages. It would be more efficient to remove the
  // the temperature overwritten in the buffer from the average and add the new
  // temperature to the average, but since thermistors can be disconnected from
  // the system the running average can be contaminated by NaNs. We use the
  // inefficient but safe approach instead.
  current_basket_temperature = 0.0;
  current_group_temperature = 0.0;
  for (int i = 0; i < BUFFER_SIZE; ++i) {
    current_basket_temperature += basket_temperature_buffer[i];
    current_group_temperature += group_temperature_buffer[i];
  }
  current_basket_temperature /= BUFFER_SIZE;
  current_group_temperature /= BUFFER_SIZE;
}

// Refreshes the OLED screen using current basket / group temperature and
// elapsed time.
void refresh_display() {
  // We use dtostrf for string formatting using variables of type float. See
  // https://arduino.stackexchange.com/questions/53712/creating-formatted-
  // string-including-floats-in-arduino-compatible-c. The character buffer size
  // for the float to string conversion is 7 (WXYZ.D, plus the null termination
  // character); we don't expect basket or group temperatures to be below
  // -999.9C or above 9999.9C. We use a second character buffer to include the
  // unit of measurement.
  char number_buffer[7];
  char buffer[14];

  // If the target group temperature changed recently, display it instead of the
  // group temperature.
  bool display_target = millis() <= last_target_change + TARGET_DISPLAY_TIME;

  u8g2.firstPage();
  do {
    float time = min(3600.0, elapsed_time);

    u8g2.setFont(u8g2_font_helvR10_tr);
    u8g2.setFontMode(0);
    u8g2.setDrawColor(1);

    // Draw header.
    u8g2.drawStr(0, 11, display_target ? "Target" : "Group");
    u8g2.drawStr(128 - u8g2.getStrWidth("Basket") - 1, 11, "Basket");
    u8g2.drawLine(0, 13, 127, 13);

    // Display temperatures.
    float temperature = display_target ? target_group_temperature :
                                         current_group_temperature;
    dtostrf(temperature, 5, 1, number_buffer);
    snprintf(buffer, sizeof(buffer), "%sC",
             isnan(temperature) ? "---" : number_buffer);
    u8g2.drawStr(0, 30, buffer);

    dtostrf(current_basket_temperature, 5, 1, number_buffer);
    snprintf(buffer, sizeof(buffer), "%sC",
             isnan(current_basket_temperature) ? "---" : number_buffer);
    u8g2.drawStr(128 - u8g2.getStrWidth(buffer) - 1, 30, buffer);

    // Display time.
    u8g2.drawBox(0, 40, 128, 24);
    dtostrf(elapsed_time, 5, 1, number_buffer);
    snprintf(buffer, sizeof(buffer), "%02d:%02d.%1d",
             int(time) / 60, int(time) % 60, int(time * 10) % 10);

    u8g2.setFont(u8g2_font_helvR18_tn);
    u8g2.setFontMode(1);
    u8g2.setDrawColor(2);

    u8g2.drawStr((128 - u8g2.getStrWidth(buffer)) / 2, 61, buffer);
  } while ( u8g2.nextPage() );
}
