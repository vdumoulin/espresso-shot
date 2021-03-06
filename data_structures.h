/*
  Data structures used by the program.
*/
#ifndef ESPRESSO_SHOT_DATA_STRUCTURES_H_
#define ESPRESSO_SHOT_DATA_STRUCTURES_H_

#include "constants.h"

// Machine state.
enum MachineState {START, RUNNING, STOP, STOPPED};

// Device state.
struct DeviceState {
  // Espresso machine state.
  MachineState machine_state;

  // We use circular buffers to compute basket and group temperature averages
  // over a certain time horizon determined by SENSOR_FREQUENCY and BUFFER_SIZE.
  int latest_buffer_index;
  float basket_temperature_buffer[BUFFER_SIZE];
  float group_temperature_buffer[BUFFER_SIZE];

  // Temperature buffer averages.
  float current_basket_temperature;
  float current_group_temperature;

  // Selected target group temperature.
  float target_group_temperature;

  // The start time is used with millis() to determine the elapsed time. When
  // the machine is not running we display the previous shot time that was
  // recorded into elapsed_time.
  unsigned long start_time;
  float elapsed_time;

  // Historical device state.
  unsigned long last_temperature_measurement;
  unsigned long last_display_refresh;
  unsigned long last_target_change;
};

// Struct used to send measurements over the serial port.
struct Measurement {
  float elapsed_time;
  float basket_temperature;
  float group_temperature;
  unsigned char state;
};

#endif  // ESPRESSO_SHOT_DATA_STRUCTURES_H_
