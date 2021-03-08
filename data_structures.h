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

  // We use circular buffers to compute basket and group resistance averages
  // over a certain time horizon determined by SENSOR_FREQUENCY and BUFFER_SIZE.
  // We work with resistances instead of temperatures because converting from
  // resistance to temperature is straightforward and being able to send
  // resistance information over serial is useful for thermistor calibration.
  int latest_buffer_index;
  float basket_resistance_buffer[BUFFER_SIZE];
  float group_resistance_buffer[BUFFER_SIZE];

  // Resistance buffer averages' corresponding temperatures.
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
  unsigned long last_resistance_measurement;
  unsigned long last_display_refresh;
  unsigned long last_target_change;
};

// Struct used to send measurements over the serial port.
struct Measurement {
  float elapsed_time;
  float basket_resistance;
  float group_resistance;
  float basket_temperature;
  float group_temperature;
  // The type int is 2 bytes long for ATmega based boards
  // (https://www.arduino.cc/reference/en/language/variables/data-types/int/),
  // in contrast with the usual 4 bytes, but the type long is 4 bytes long
  // (https://www.arduino.cc/reference/en/language/variables/data-types/long/),
  // so we represent the machine state as a long that can be decoded by Python's
  // struct library as an int.
  long state;
};

#endif  // ESPRESSO_SHOT_DATA_STRUCTURES_H_
