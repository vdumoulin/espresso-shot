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
DeviceState device_state;

void setup() {
  // Initialize devices and pins.
  Serial.begin(9600);
  ads1115.begin();
  u8g2.begin();
  pinMode(FAN_PIN, OUTPUT);

  // Initialize running state.
  device_state.machine_state = STOPPED;

  // Initialize temperatures.
  device_state.target_group_temperature = read_target_temperature();
  device_state.current_basket_temperature = read_basket_temperature(ads1115);
  device_state.current_group_temperature = read_group_temperature(ads1115);
  
  device_state.latest_buffer_index = 0;
  
  for (int i = 0; i < BUFFER_SIZE; ++i) {
    device_state.basket_temperature_buffer[i] =
        device_state.current_basket_temperature;
    device_state.group_temperature_buffer[i] =
        device_state.current_group_temperature;
  }
  
  // Initialize time.
  device_state.start_time = millis();
  device_state.last_display_refresh = device_state.start_time;
  device_state.last_target_change = device_state.start_time;
  device_state.elapsed_time = 0.0;
}

void loop() {
  // Determine the state of the machine, as determined by the tilt switch and
  // its previous state.
  bool lever_up = read_voltage(ads1115, TILT_CHANNEL) > 3.0;
  switch (device_state.machine_state) {
    case START:
      device_state.machine_state = lever_up ? RUNNING : STOP;
      break;
    case RUNNING:
      device_state.machine_state = lever_up ? RUNNING : STOP;
      break;
    case STOP:
      device_state.machine_state = lever_up ? START : STOPPED;
      break;
    case STOPPED:
      device_state.machine_state = lever_up ? START : STOPPED;
      break;
  }

  unsigned long current_time = millis();

  // When a state transition from "stopped" to "running" occurs, reset the
  // elapsed time and start the timer.
  if (device_state.machine_state == START) {
    device_state.start_time = current_time;
    device_state.elapsed_time = 0.0;
  }

  // When the machine is running or has just stopped, update the timer.
  if (device_state.machine_state != STOPPED) {
    device_state.elapsed_time = (
        (current_time - device_state.start_time) / 1000.0
    );
  }

  // Measure temperatures when the sensing period has passed.
  if (current_time > device_state.last_temperature_measurement + SENSING_PERIOD) {
    update_temperatures(ads1115, device_state);
    device_state.last_temperature_measurement = current_time;
    write_measurement(device_state);
  }

  // Read the target group temperature set by the user. If it has changed,
  // update the target group temperature and reset the timer for displaying
  // that information instead of the group temperature.
  float new_target_temperature = read_target_temperature();
  // Target temperatures are rounded to the nearest multiple of 0.5, so to be
  // extra careful about determining equality on floats we double the numbers
  // and cast them as int before doing the comparison.
  if ((int) 2 * new_target_temperature != (int) 2 * device_state.target_group_temperature) {
    device_state.target_group_temperature = new_target_temperature;
    device_state.last_target_change = current_time;
  }

  // We cool the grouphead until it reaches the target temperature. We could
  // eventually dampen the temperature swings by implementing PID control, but
  // for now this is good enough.
  digitalWrite(
      FAN_PIN,
      device_state.current_group_temperature > device_state.target_group_temperature ?  HIGH : LOW
  );

  // Refresh the display when the refresh period has passed.
  if (current_time > device_state.last_display_refresh + DISPLAY_PERIOD) {
    refresh_display(u8g2, device_state);
    device_state.last_display_refresh = current_time;
  }
}
