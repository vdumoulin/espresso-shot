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
    target temperature is user-selectable with two push buttons.
  - Displays basket temperature, group temperature, and shot time on an OLED
    screen.
  - Sends time and temperature logging information over serial. A companion
    Python script listens to the serial channel and converts the information
    into JSON files that a companion Jupyter Notebook can then read and display.
*/

#include <Adafruit_ADS1015.h>
#include <Button.h>
#include <TaskScheduler.h>
#include <U8g2lib.h>
#include <Wire.h>

#include "constants.h"
#include "data_structures.h"
#include "functions.h"

// We use an ADS1115 for data acquisition and an OLED screen to display
// information. We also monitor two button switches (target group temperature
// increase / decrease) and one tilt switch (brew lever up / down).
Adafruit_ADS1115 ads1115;
U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0);
Button temperature_increase_button(TARGET_TEMPERATURE_INCREASE_PIN);
Button temperature_decrease_button(TARGET_TEMPERATURE_DECREASE_PIN);
Button tilt_switch(TILT_PIN);

// Device state.
DeviceState state;

// Task scheduler.
void update_machine_state_callback() {
  update_machine_state(temperature_increase_button, temperature_decrease_button,
                       tilt_switch, state);
}
void update_timer_callback() { update_timer(state); }
void sense_callback() {
  update_resistances(ads1115, state);
  write_measurement(state);
}
void control_fan_callback() { control_fan(state); }
void refresh_display_callback() { refresh_display(u8g2, state); }

Task tasks[] = {
    {DEFAULT_TASK_PERIOD, TASK_FOREVER, &update_machine_state_callback},
    {DEFAULT_TASK_PERIOD, TASK_FOREVER, &update_timer_callback},
    {SENSING_PERIOD, TASK_FOREVER, &sense_callback},
    {DEFAULT_TASK_PERIOD, TASK_FOREVER, &control_fan_callback},
    {DISPLAY_PERIOD, TASK_FOREVER, &refresh_display_callback}
};

Scheduler runner;

void setup() {
  Serial.begin(9600);
  ads1115.begin();
  u8g2.begin();
  temperature_increase_button.begin();
  temperature_decrease_button.begin();
  tilt_switch.begin();
  pinMode(FAN_PIN, OUTPUT);
  initialize_state(ads1115, state);

  runner.init();
  for (Task& task : tasks) {
      runner.addTask(task);
      task.enable();
  }
}

void loop() {
  runner.execute();
}
