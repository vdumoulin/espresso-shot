/*
  Code for an Arduino-based espresso shot management device for E61 grouphead
  machines. Highly inspired by Howard Smith's setup
  (https://www.home-barista.com/tips/guide-to-managing-hx-brew-temperatures-t64840.html).

  The device performs the following:

  - Monitors basket and group temperatures using thermistors. The group
    thermistor is inserted in an adapter screwed into the M6 closure plug (e.g.
    https://www.home-barista.com/espresso-machines/monitoring-brew-temperature-e61-groups-t1352.html).
    The basket thermistor is wrapped in foil tape and sandwiched between the
    portafilter basket and the grouphead.
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

#include "constants.h"
#include "data_structures.h"
#include "functions.h"

// We use an ADS1115 for data acquisition and an OLED screen to display
// information.
Adafruit_ADS1115 ads1115;
U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0);

// Device state.
DeviceState state;

void setup() {
  // Initialize devices and pins.
  Serial.begin(9600);
  ads1115.begin();
  u8g2.begin();
  pinMode(FAN_PIN, OUTPUT);

  // Initialize running state.
  state.machine_state = STOPPED;

  // Initialize resistances and temperatures.
  float basket_resistance = read_basket_resistance(ads1115);
  float group_resistance = read_group_resistance(ads1115);
  
  for (int i = 0; i < BUFFER_SIZE; ++i) {
    state.basket_resistance_buffer[i] = basket_resistance;
    state.group_resistance_buffer[i] = group_resistance;
  }
  state.latest_buffer_index = BUFFER_SIZE - 1;

  state.target_group_temperature = read_target_temperature();
  state.current_basket_temperature = basket_resistance_to_temperature(
      basket_resistance);
  state.current_group_temperature = group_resistance_to_temperature(
      group_resistance);
  
  // Initialize time.
  state.start_time = millis();
  state.last_display_refresh = state.start_time;
  state.last_target_change = state.start_time;
  state.elapsed_time = 0.0;
}

void loop() {
  // Determine the state of the machine, as determined by the tilt switch and
  // its previous state.
  bool lever_up = read_voltage(ads1115, TILT_CHANNEL) > 3.0;
  switch (state.machine_state) {
    case START:
      state.machine_state = lever_up ? RUNNING : STOP;
      break;
    case RUNNING:
      state.machine_state = lever_up ? RUNNING : STOP;
      break;
    case STOP:
      state.machine_state = lever_up ? START : STOPPED;
      break;
    case STOPPED:
      state.machine_state = lever_up ? START : STOPPED;
      break;
  }

  unsigned long current_time = millis();

  // When a state transition from "stopped" to "running" occurs, reset the
  // elapsed time and start the timer.
  if (state.machine_state == START) {
    state.start_time = current_time;
    state.elapsed_time = 0.0;
  }

  // When the machine is running or has just stopped, update the timer.
  if (state.machine_state != STOPPED)
    state.elapsed_time = (current_time - state.start_time) / 1000.0;

  // Measure temperatures when the sensing period has passed.
  if (current_time > state.last_resistance_measurement + SENSING_PERIOD) {
    update_resistances(ads1115, state);
    state.last_resistance_measurement = current_time;
    write_measurement(state);
  }

  // Read the target group temperature set by the user. If it has changed,
  // update the target group temperature and reset the timer for displaying
  // that information instead of the group temperature.
  float new_target_temperature = read_target_temperature();
  // Target temperatures are rounded to the nearest multiple of 0.5, so to be
  // extra careful about determining equality on floats we double the numbers
  // and cast them as int before doing the comparison.
  if (int(2 * new_target_temperature) !=
      int(2 * state.target_group_temperature)) {
    state.target_group_temperature = new_target_temperature;
    state.last_target_change = current_time;
  }

  // We cool the grouphead until it reaches the target temperature. We could
  // eventually dampen the temperature swings by implementing PID control, but
  // for now this is good enough.
  bool over_target_temperature = state.current_group_temperature >
                                 state.target_group_temperature;
  // Since we are using a BJT to set the voltage at the MOSFET gate, the logic
  // is inverted and we need to output HIGH to stop the fan.
  digitalWrite(FAN_PIN, over_target_temperature ? LOW : HIGH);

  // Refresh the display when the refresh period has passed.
  if (current_time > state.last_display_refresh + DISPLAY_PERIOD) {
    refresh_display(u8g2, state);
    state.last_display_refresh = current_time;
  }
}
