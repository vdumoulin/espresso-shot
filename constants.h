/*
  Contains all constants used by the program.
*/
#ifndef ESPRESSO_SHOT_CONSTANTS_H_
#define ESPRESSO_SHOT_CONSTANTS_H_

// Default period for device tasks, such as updating the timer or controlling
// the fan.
#define DEFAULT_TASK_PERIOD 10

// Number of times per second that we read temperatures.
#define SENSING_FREQUENCY 100

// Temperatures are averaged over a certain time horizon to reduce noise. By
// default we set it to the sensing frequency so that we get an average over the
// previous second.
#define BUFFER_SIZE SENSING_FREQUENCY
constexpr unsigned short SENSING_PERIOD = 1000.0 / SENSING_FREQUENCY;

// We use the Steinhart-Hart model to characterize the relationship between
// thermistor resistance and temperature. The coefficients A, B, and C are
// calculated empirically on three temperature-resistance pairs using an online
// calculator (e.g. https://www.thinksrs.com/downloads/programs/therm%20calc/
// ntccalibrator/ntccalculator.html).
#define BASKET_SH_A 0.7729151421e-3
#define BASKET_SH_B 2.052737727e-4
#define BASKET_SH_C 1.427250141e-7

#define GROUP_SH_A 0.7729151421e-3
#define GROUP_SH_B 2.052737727e-4
#define GROUP_SH_C 1.427250141e-7

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
#define FAN_PIN 12
#define TARGET_TEMPERATURE_PIN A0
#define TARGET_TEMPERATURE_MIN 88.0
#define TARGET_TEMPERATURE_MAX 98.0

// Number of times per second that we refresh the display.
#define DISPLAY_FREQUENCY 4
constexpr unsigned short DISPLAY_PERIOD = 1000.0 / DISPLAY_FREQUENCY;

// Number of milliseconds to display the target temperature for when it changes.
#define TARGET_DISPLAY_TIME 1000

// Size of the character buffer used to receive the string representation of
// floating point numbers. The size required to represent a temperature is 8
// (VWXY.ZC plus the null termination character), since we don't expect basket
// or group temperatures to be below -999.9C or above 9999.9C. The size required
// to represent elapsed time is also 8 (AB:CD.E plus the null termination
// character).
#define FORMAT_BUFFER_SIZE 8

#endif  // ESPRESSO_SHOT_CONSTANTS_H_
