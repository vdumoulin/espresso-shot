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
void write_measurement(const DeviceState& device_state);

// Reads the target group temperature set by the user on the potentiometer.
// Target temperatures are rounded to the nearest multiple of 0.5.
float read_target_temperature();

// Reads and returns the voltage at the specified ADC channel.
float read_voltage(const Adafruit_ADS1115& ads1115, uint8_t channel);

// Reads and returns a thermistor's temperature at the specified ADC channel
// given the specified known resistance and Steinhart-Hard coefficients.
float read_temperature(const Adafruit_ADS1115& ads1115, uint8_t channel,
                       float known_resistance, float sh_a, float sh_b,
                       float sh_c);

// Reads the basket temperature from its corresponding thermistor. Wraps
// read_temperature for convenience.
float read_basket_temperature(const Adafruit_ADS1115& ads1115);

// Reads the group temperature from its corresponding thermistor. Wraps
// read_temperature for convenience.
float read_group_temperature(const Adafruit_ADS1115& ads1115);

// Updates the basket and group temperature buffers and recomputes the average
// basket and group temperatures.
void update_temperatures(const Adafruit_ADS1115& ads1115,
                         DeviceState& device_state);

// Refreshes the OLED screen using current basket / group temperature and
// elapsed time.
void refresh_display(const U8G2_SSD1306_128X64_NONAME_1_HW_I2C& u8g2,
                     const DeviceState& device_state);

#endif  // ESPRESSO_SHOT_FUNCTIONS_H_
