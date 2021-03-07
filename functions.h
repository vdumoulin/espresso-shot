/*
  Functions used by the program.
*/
#ifndef ESPRESSO_SHOT_FUNCTIONS_H_
#define ESPRESSO_SHOT_FUNCTIONS_H_

#include <Arduino.h>
#include <Adafruit_ADS1015.h>
#include <U8g2lib.h>

#include "constants.h"
#include "data_structures.h"

// Writes a measurement to the serial port.
void write_measurement(const DeviceState& state);

// Reads the target group temperature set by the user on the potentiometer.
// Target temperatures are rounded to the nearest multiple of 0.5.
float read_target_temperature();

// Reads and returns the voltage at the specified ADC channel.
float read_voltage(Adafruit_ADS1115& ads1115, uint8_t channel);

// Reads and returns a thermistor's resistance at the specified ADC channel
// given the specified known resistance.
float read_resistance(Adafruit_ADS1115& ads1115, uint8_t channel,
                      float known_resistance);

// Reads the basket resistance from its corresponding thermistor. Wraps
// read_resistance for convenience.
float read_basket_resistance(Adafruit_ADS1115& ads1115);

// Reads the group resistance from its corresponding thermistor. Wraps
// read_resistance for convenience.
float read_group_resistance(Adafruit_ADS1115& ads1115);

// Converts a thermistor resistance to a temperature given its Steinhart-Hard
// model coefficients.
float resistance_to_temperature(float resistance, float sh_a, float sh_b,
                                float sh_c);

// Converts the basket thermistor's resistance to a temperature. Wraps
// resistance_to_temperature for convenience.
float basket_resistance_to_temperature(float resistance);

// Converts the group thermistor's resistance to a temperature. Wraps
// resistance_to_temperature for convenience.
float group_resistance_to_temperature(float resistance);

// Writes the string representation of a temperature to a character buffer using
// the VWXY.ZC format.
void format_temperature(char (&buffer)[FORMAT_BUFFER_SIZE], float temperature);

// Writes the string representation of elapsed time to a character buffer using
// the AB:CD.E format.
void format_elapsed_time(char (&buffer)[FORMAT_BUFFER_SIZE],
                         float elapsed_time);

// Updates the basket and group resistance buffers and recomputes the average
// basket and group resistances.
void update_resistances(Adafruit_ADS1115& ads1115, DeviceState& state);

// Refreshes the OLED screen using current basket / group resistances and
// elapsed time.
void refresh_display(U8G2_SSD1306_128X64_NONAME_1_HW_I2C& u8g2,
                     const DeviceState& state);

#endif  // ESPRESSO_SHOT_FUNCTIONS_H_
