/*
  Functions used by the program.
*/
#include "functions.h"

void write_measurement(const DeviceState& device_state) {
  Measurement measurement = {
      device_state.elapsed_time,
      device_state.basket_temperature_buffer[device_state.latest_buffer_index],
      device_state.group_temperature_buffer[device_state.latest_buffer_index], 
      device_state.machine_state
  };
  Serial.write((byte *) &measurement, sizeof(measurement));
}

float read_target_temperature() {
  // We want to round to the nearest multiple of 0.5, so instead we map to
  // double the (integer) range and divide by 2.
  int double_target_temperature = map(
      analogRead(TARGET_TEMPERATURE_PIN), 0, 1023, 180, 200
  );
  return double_target_temperature / 2.0;
}

float read_voltage(const Adafruit_ADS1115& ads1115, uint8_t channel) {
  return 0.0001875 * ads1115.readADC_SingleEnded(channel);
}

float read_temperature(const Adafruit_ADS1115& ads1115, uint8_t channel,
                       float known_resistance, float sh_a, float sh_b,
                       float sh_c) {
  // Infer the resistance from the voltage divider circuit.
  float reference_voltage = read_voltage(ads1115, REFERENCE_VOLTAGE_CHANNEL);
  float voltage = read_voltage(ads1115, channel);
  float resistance = known_resistance / ((reference_voltage / voltage) - 1.0);

  // We use the Steinhart-Hart model to infer the temperature (in degrees
  // Kelvin) from the thermistor's resistance.
  float inverse_temperature_kelvin = (
    sh_a + sh_b * log(resistance) + sh_c * pow(log(resistance), 3)
  );

  return 1.0 / inverse_temperature_kelvin - 273.15;
}

float read_basket_temperature(const Adafruit_ADS1115& ads1115) {
  return read_temperature(
      ads1115, BASKET_VOLTAGE_CHANNEL, BASKET_KNOWN_RESISTANCE,
      BASKET_SH_A, BASKET_SH_B, BASKET_SH_C
  );
}

float read_group_temperature(const Adafruit_ADS1115& ads1115) {
  return read_temperature(
      ads1115, GROUP_VOLTAGE_CHANNEL, GROUP_KNOWN_RESISTANCE,
      GROUP_SH_A, GROUP_SH_B, GROUP_SH_C
  );
}

void update_temperatures(const Adafruit_ADS1115& ads1115,
                         DeviceState& device_state) {
  // Update temperature buffers.
  device_state.latest_buffer_index =
      (device_state.latest_buffer_index + 1) % BUFFER_SIZE;
  device_state.basket_temperature_buffer[device_state.latest_buffer_index] =
      read_basket_temperature(ads1115);
  device_state.group_temperature_buffer[device_state.latest_buffer_index] =
      read_group_temperature(ads1115);

  // Compute temperature averages. It would be more efficient to remove the
  // the temperature overwritten in the buffer from the average and add the new
  // temperature to the average, but since thermistors can be disconnected from
  // the device the running average can be contaminated by NaNs. We use the
  // inefficient but safe approach instead.
  device_state.current_basket_temperature = 0.0;
  device_state.current_group_temperature = 0.0;
  for (int i = 0; i < BUFFER_SIZE; ++i) {
    device_state.current_basket_temperature +=
        device_state.basket_temperature_buffer[i];
    device_state.current_group_temperature +=
        device_state.group_temperature_buffer[i];
  }
  device_state.current_basket_temperature /= BUFFER_SIZE;
  device_state.current_group_temperature /= BUFFER_SIZE;
}

void refresh_display(const U8G2_SSD1306_128X64_NONAME_1_HW_I2C& u8g2,
                     const DeviceState& device_state) {
  // We use dtostrf for string formatting using variables of type float. See
  // https://arduino.stackexchange.com/questions/53712/creating-formatted-string-including-floats-in-arduino-compatible-c.
  // The character buffer size for the float to string conversion is 7 (WXYZ.D,
  // plus the null termination character); we don't expect basket or group
  // temperatures to be below -999.9C or above 9999.9C. We use a second
  // character buffer to include the unit of measurement.
  char number_buffer[7];
  char buffer[14];

  // If the target group temperature changed recently, display it instead of the
  // group temperature.
  bool display_target =
      millis() <= device_state.last_target_change + TARGET_DISPLAY_TIME;

  u8g2.firstPage();
  do {
    float time = min(3600.0, device_state.elapsed_time);

    u8g2.setFont(u8g2_font_helvR10_tr);
    u8g2.setFontMode(0);
    u8g2.setDrawColor(1);

    // Draw header.
    u8g2.drawStr(0, 11, display_target ? "Target" : "Group");
    u8g2.drawStr(128 - u8g2.getStrWidth("Basket") - 1, 11, "Basket");
    u8g2.drawLine(0, 13, 127, 13);

    // Display temperatures.
    float temperature = display_target ? device_state.target_group_temperature :
                                         device_state.current_group_temperature;
    dtostrf(temperature, 5, 1, number_buffer);
    snprintf(buffer, sizeof(buffer), "%sC",
             isnan(temperature) ? "---" : number_buffer);
    u8g2.drawStr(0, 30, buffer);

    dtostrf(device_state.current_basket_temperature, 5, 1, number_buffer);
    snprintf(buffer, sizeof(buffer), "%sC",
             isnan(device_state.current_basket_temperature) ? "---" : number_buffer);
    u8g2.drawStr(128 - u8g2.getStrWidth(buffer) - 1, 30, buffer);

    // Display time.
    u8g2.drawBox(0, 40, 128, 24);
    dtostrf(device_state.elapsed_time, 5, 1, number_buffer);
    snprintf(buffer, sizeof(buffer), "%02d:%02d.%1d",
             int(time) / 60, int(time) % 60, int(time * 10) % 10);

    u8g2.setFont(u8g2_font_helvR18_tn);
    u8g2.setFontMode(1);
    u8g2.setDrawColor(2);

    u8g2.drawStr((128 - u8g2.getStrWidth(buffer)) / 2, 61, buffer);
  } while ( u8g2.nextPage() );
}
