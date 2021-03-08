"""Script to calibrate the basket and group thermistors.

The script listens to the serial port for measurements and records resistances
along with user-specified temperatures for three separate temperatures, then
computes the group and (optionally) basket Steinhart-Hart model coefficients.

Example usage (group-only calibration):

    $ python calibrate.py --fqbn <FQBN> -p <UPLOAD PORT> --group_only
"""
import argparse
import collections
import threading

import numpy as np
import serial

import utils


def read_resistances(serial_port, basket_resistances, group_resistances):
  """Daemon function which continually reads resistances from the serial port.

  Resistances are added to the `basket_resistance` and `group_resistance`
  circular buffers as they are read.

  Args:
    serial_port: serial.Serial, serial port to read resistances from.
    basket_resistances: collections.deque, basket resistance circular buffer.
    group_resistances: collections.deque, group resistance circular buffer.
  """
  while True:
    measurement = utils.read_measurement(serial_port)
    basket_resistance, group_resistance = measurement[1:3]
    basket_resistances.append(basket_resistance)
    group_resistances.append(group_resistance)


def compute_coefficients(temperature_resistance_pairs):
  """Computes Steinhart-Hart model coefficients.

  Equations taken from
  https://www.dataloggerinc.com/wp-content/uploads/2016/10/self-calibrate-thermistors.pdf.

  Args:
    temperature_resistance_pairs: sequence of three (temperature, resistance)
      tuples.

  Returns:
    tuple (sh_a, sh_b, shc_c) of Steinhart-Hart coefficients.
  """
  l_1, l_2, l_3 = [np.log(p[1]) for p in temperature_resistance_pairs]
  y_1, y_2, y_3 = [1.0 / (p[0] + 273.15) for p in temperature_resistance_pairs]

  gamma_2 = (y_2 - y_1) / (l_2 - l_1)
  gamma_3 = (y_3 - y_1) / (l_3 - l_1)

  sh_c = ((gamma_3 - gamma_2) / (l_3 - l_2)) / (l_1 + l_2 + l_3)
  sh_b = gamma_2 - sh_c * (l_1 ** 2 + l_1 * l_2 + l_2 ** 2)
  sh_a = y_1 - (sh_b + l_1 ** 2 * sh_c) * l_1

  return (sh_a, sh_b, sh_c)


def calibrate(port, group_only):
  """Performs thermistor calibration.

  Prompts the user for three separate temperature readings from a reference
  thermometer, and infers the basket and thermistor's Steinhart-Hart model
  coefficients using the thermistors' corresponding resistances at those
  temperatures.

  Args:
    port: str, upload port.
    group_only: bool, only calibrate the group thermistor if True.
  """
  serial_port = serial.Serial(port=port, baudrate=9600)

  # We average resistances over the previous 50 measurements (i.e. half second).
  basket_resistances = collections.deque(maxlen=50)
  group_resistances = collections.deque(maxlen=50)

  # Start a daemon thread to record basket and group resistances in the
  # background.
  thread = threading.Thread(
      target=read_resistances,
      args=(serial_port, basket_resistances, group_resistances))
  thread.daemon = True
  thread.start()

  # Acquire three separate temperature-resistance pairs.
  temperature_resistance_pairs = []
  while len(temperature_resistance_pairs) < 3:
    temperature_string = input(
        'Temperature {}: '.format(len(temperature_resistance_pairs) + 1))
    try:
      temperature = float(temperature_string)
    except ValueError:
      print('Please enter a floating point value.')
      continue
    temperature_resistance_pairs.append((
        temperature, (np.mean(basket_resistances), np.mean(group_resistances))
    ))
  
  # Compute and print Steinhart-Hart model coefficients. If `group_only`, we
  # do so only for the group thermistor.
  thermistors = [(1, 'Group')] if group_only else [(0, 'Basket'), (1, 'Group')]
  for i, thermistor_name in thermistors:
    sh_a, sh_b, sh_c = compute_coefficients(
        [(t, r[i]) for t, r in temperature_resistance_pairs])
    print('{} coefficients:\n  A = {}\n  B = {}\n  C = {}'.format(
        thermistor_name, sh_a, sh_b, sh_c
    ))


if __name__ == '__main__':
  parser = argparse.ArgumentParser(
      description='Calibrate basket and group thermistors.')
  parser.add_argument(
      '--fqbn', type=str, default='arduino:mbed:nano33ble',
      help='Fully Qualified Board Name.')
  parser.add_argument(
      '-p', dest='port', type=str, default=None,
      help='Upload port, e.g.: COM10 or /dev/ttyACM0')
  parser.add_argument(
      '--recompile', action='store_true',
      help='Recompile the program and upload it to the Arduino device.')
  parser.add_argument(
      '--group_only', action='store_true',
      help='Only calibrate the group thermistor.')
  args = parser.parse_args()

  fqbn = args.fqbn
  recompile = args.recompile
  group_only = args.group_only
  port = utils.find_port_if_not_specified(fqbn, args.port)

  if recompile:
    utils.compile_and_upload(fqbn=fqbn, port=port)
    # Give the Arduino device some time to become operational.
    time.sleep(2.0)

  calibrate(port, group_only)
