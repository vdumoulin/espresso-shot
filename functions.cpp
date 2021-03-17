/*
  Functions used by the program.
*/
#include "functions.h"

void initialize_state(Adafruit_ADS1115& ads1115, DeviceState& state) {
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

  state.target_group_temperature = TARGET_TEMPERATURE_DEFAULT;
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

void update_machine_state(Button& temperature_increase_button,
                          Button& temperature_decrease_button,
                          Button& tilt_switch,
                          DeviceState& state) {
  if (temperature_increase_button.pressed()) {
    state.target_group_temperature = min(
        state.target_group_temperature + TARGET_TEMPERATURE_INCREMENT,
        TARGET_TEMPERATURE_MAX);
    state.last_target_change = millis();
  } else if (temperature_decrease_button.pressed()) {
    state.target_group_temperature = max(
        state.target_group_temperature - TARGET_TEMPERATURE_INCREMENT,
        TARGET_TEMPERATURE_MIN);
    state.last_target_change = millis();
  }

  bool lever_up = tilt_switch.read() == Button::RELEASED;
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
}

void update_timer(DeviceState& state) {
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

void control_fan(DeviceState& state) {
  // We cool the grouphead until it reaches the target temperature. We could
  // eventually dampen the temperature swings by implementing PID control, but
  // for now this is good enough.
  bool over_target_temperature = state.current_group_temperature >
                                 state.target_group_temperature;
  // Since we are using a BJT to set the voltage at the MOSFET gate, the logic
  // is inverted and we need to output HIGH to stop the fan.
  digitalWrite(FAN_PIN, over_target_temperature ? LOW : HIGH);
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
    u8g2.drawStr(0, 30, buffer);

    format_temperature(buffer, state.current_basket_temperature);
    u8g2.drawStr(128 - u8g2.getStrWidth(buffer) - 1, 30, buffer);

    // Display time.
    u8g2.drawBox(0, 40, 128, 24);
    format_elapsed_time(buffer, state.elapsed_time);

    u8g2.setFont(u8g2_font_helvR18_tn);
    u8g2.setFontMode(1);
    u8g2.setDrawColor(2);

    u8g2.drawStr(24, 61, buffer);
  } while ( u8g2.nextPage() );
}

float basket_resistance_to_temperature(float resistance) {
  return resistance_to_temperature(resistance, BASKET_SH_A, BASKET_SH_B,
                                   BASKET_SH_C);
}

float group_resistance_to_temperature(float resistance) {
  return resistance_to_temperature(resistance, GROUP_SH_A, GROUP_SH_B,
                                   GROUP_SH_C);
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

void format_temperature(char (&buffer)[FORMAT_BUFFER_SIZE], float temperature) {
  // If the resistance is so high that the temperature is close to absolute
  // zero, that probably means that the wire is disconnected.
  if (temperature > -273.0) {
    // Cast temperature to int to get rid of all decimal places.
    int integer = temperature;
    // Casting ten times the temperature to int gets rid of all but one decimal
    // place, and use the remainder of dividing the result by 10 keeps only the
    // first decimal place.
    int decimal = int(abs(temperature) * 10) % 10;
    snprintf(buffer, sizeof(buffer), "%3d.%1dC", integer, decimal);
  } else {
    snprintf(buffer, sizeof(buffer), "--- C");
  }
}

float read_basket_resistance(Adafruit_ADS1115& ads1115) {
  return read_resistance(ads1115, BASKET_VOLTAGE_CHANNEL,
                         BASKET_KNOWN_RESISTANCE);
}

float read_group_resistance(Adafruit_ADS1115& ads1115) {
  return read_resistance(ads1115, GROUP_VOLTAGE_CHANNEL,
                         GROUP_KNOWN_RESISTANCE);
}

float read_resistance(Adafruit_ADS1115& ads1115, uint8_t channel,
                      float known_resistance) {
  // Infer the resistance from the voltage divider circuit.
  float reference_voltage = read_voltage(ads1115, REFERENCE_VOLTAGE_CHANNEL);
  float voltage = read_voltage(ads1115, channel);
  float voltage_ratio = reference_voltage / voltage;
  // In theory the voltage should never be greater than the reference voltage,
  // but in practice noise in the circuit could make that happen. If the
  // voltage is close enough to the reference voltage, we just return an
  // infinite resistance value.
  return abs(voltage_ratio) < 1.01 ?
      INFINITY : known_resistance / (voltage_ratio - 1.0);
}

float read_voltage(Adafruit_ADS1115& ads1115, uint8_t channel) {
  return 0.0001875 * ads1115.readADC_SingleEnded(channel);
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
