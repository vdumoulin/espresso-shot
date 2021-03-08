/*
  Functions used by the program.
*/
#include "functions.h"

void write_measurement(const DeviceState& state) {
  int index = state.latest_buffer_index;
  float basket_resistance = state.basket_resistance_buffer[index];
  float group_resistance = state.group_resistance_buffer[index];
  Measurement measurement = {
      state.elapsed_time,
      basket_resistance,
      group_resistance,
      basket_resistance_to_temperature(basket_resistance),
      group_resistance_to_temperature(group_resistance),
      long(state.machine_state)
  };
  Serial.write((byte *) &measurement, sizeof(measurement));
}

float read_target_temperature() {
  // We want to round to the nearest multiple of 0.5, so instead we map to
  // double the (integer) range and divide by 2.
  int double_target_temperature = map(
      analogRead(TARGET_TEMPERATURE_PIN), 0, 1023, 180, 200
  );
  return float(double_target_temperature) / 2.0;
}

float read_voltage(Adafruit_ADS1115& ads1115, uint8_t channel) {
  return 0.0001875 * ads1115.readADC_SingleEnded(channel);
}

float read_resistance(Adafruit_ADS1115& ads1115, uint8_t channel,
                      float known_resistance) {
  // Infer the resistance from the voltage divider circuit.
  float reference_voltage = read_voltage(ads1115, REFERENCE_VOLTAGE_CHANNEL);
  float voltage = read_voltage(ads1115, channel);
  return known_resistance / ((reference_voltage / voltage) - 1.0);
}

float read_basket_resistance(Adafruit_ADS1115& ads1115) {
  return read_resistance(ads1115, BASKET_VOLTAGE_CHANNEL,
                         BASKET_KNOWN_RESISTANCE);
}

float read_group_resistance(Adafruit_ADS1115& ads1115) {
  return read_resistance(ads1115, GROUP_VOLTAGE_CHANNEL,
                         GROUP_KNOWN_RESISTANCE);
}

float resistance_to_temperature(float resistance, float sh_a, float sh_b,
                                float sh_c) {
  // We use the Steinhart-Hart model to infer the temperature (in degrees
  // Kelvin) from the thermistor's resistance.
  float inverse_temperature_kelvin = (
    sh_a + sh_b * log(resistance) + sh_c * pow(log(resistance), 3)
  );

  return 1.0 / inverse_temperature_kelvin - 273.15;
}

float basket_resistance_to_temperature(float resistance) {
  return resistance_to_temperature(resistance, BASKET_SH_A, BASKET_SH_B,
                                   BASKET_SH_C);
}

float group_resistance_to_temperature(float resistance) {
  return resistance_to_temperature(resistance, GROUP_SH_A, GROUP_SH_B,
                                   GROUP_SH_C);
}

void format_temperature(char (&buffer)[FORMAT_BUFFER_SIZE], float temperature) {
  // Cast temperature to int to get rid of all decimal places.
  int integer = temperature;
  // Casting ten times the temperature to int gets rid of all but one decimal
  // place, and use the remainder of dividing the result by 10 keeps only the
  // first decimal place.
  int decimal = int(abs(temperature) * 10) % 10;
  snprintf(buffer, sizeof(buffer), "%3d.%1dC", integer, decimal);
}

void format_elapsed_time(char (&buffer)[FORMAT_BUFFER_SIZE],
                         float elapsed_time) {
  // We only display up to an hour of elapsed time, which is more than enough
  // for an espresso shot. This guarantees a fixed-width representation.
  elapsed_time = min(elapsed_time, 3599.0);
  int minutes = int(elapsed_time) / 60;
  int seconds = int(elapsed_time) % 60;
  // Casting ten times the elapsed time to int gets rid of all but one decimal
  // place, and use the remainder of dividing the result by 10 keeps only the
  // first decimal place.
  int decimal = int(elapsed_time * 10) % 10;
  snprintf(buffer, sizeof(buffer), "%02d:%02d.%1d", minutes, seconds, decimal);
}

void update_resistances(Adafruit_ADS1115& ads1115,
                        DeviceState& state) {
  // Update resistance buffers.
  int index = (state.latest_buffer_index + 1) % BUFFER_SIZE;
  state.basket_resistance_buffer[index] = read_basket_resistance(ads1115);
  state.group_resistance_buffer[index] = read_group_resistance(ads1115);
  state.latest_buffer_index = index;

  // Compute resistance averages and their corresponding temperatures. It would
  // be more efficient to remove the the resistance overwritten in the buffer
  // from the average and add the new resistance to the average, but since
  // thermistors can be disconnected from the device the running average can be
  // contaminated by NaNs. We use the inefficient but safe approach instead.
  float basket_resistance_sum = 0.0;
  float group_resistance_sum = 0.0;
  for (int i = 0; i < BUFFER_SIZE; ++i) {
    basket_resistance_sum += state.basket_resistance_buffer[i];
    group_resistance_sum += state.group_resistance_buffer[i];
  }

  state.current_basket_temperature = basket_resistance_to_temperature(
      basket_resistance_sum / BUFFER_SIZE);
  state.current_group_temperature = group_resistance_to_temperature(
      group_resistance_sum / BUFFER_SIZE);
}

void refresh_display(U8G2_SSD1306_128X64_NONAME_1_HW_I2C& u8g2,
                     const DeviceState& state) {
  char buffer[FORMAT_BUFFER_SIZE];

  // If the target group temperature changed recently, display it instead of the
  // group temperature.
  bool display_target = millis() <= state.last_target_change +
                                    TARGET_DISPLAY_TIME;

  u8g2.firstPage();
  do {
    float elapsed_time = min(3600.0, state.elapsed_time);

    u8g2.setFont(u8g2_font_helvR10_tr);
    u8g2.setFontMode(0);
    u8g2.setDrawColor(1);

    // Draw header.
    u8g2.drawStr(0, 11, display_target ? "Target" : "Group");
    u8g2.drawStr(128 - u8g2.getStrWidth("Basket") - 1, 11, "Basket");
    u8g2.drawLine(0, 13, 127, 13);

    // Display temperatures.
    format_temperature(buffer,
                       display_target ? state.target_group_temperature :
                                        state.current_group_temperature);
    u8g2.drawStr(u8g2.getStrWidth("Basket") - u8g2.getStrWidth(buffer), 30,
                 buffer);

    format_temperature(buffer, state.current_basket_temperature);
    u8g2.drawStr(128 - u8g2.getStrWidth(buffer) - 1, 30, buffer);

    // Display time.
    u8g2.drawBox(0, 40, 128, 24);
    format_elapsed_time(buffer, elapsed_time);

    u8g2.setFont(u8g2_font_helvR18_tn);
    u8g2.setFontMode(1);
    u8g2.setDrawColor(2);

    u8g2.drawStr(24, 61, buffer);
  } while ( u8g2.nextPage() );
}
