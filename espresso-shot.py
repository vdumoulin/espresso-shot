"""Script to run the espresso shot management device.

The script builds and uploads the Arduino sketch to the device, then starts
listening to serial communication on the upload port and records measurement
series to JSON files.

Example usage:

    $ python espresso-shot.py --fqbn <FQBN> -p <UPLOAD PORT>

The space key toggles between saving measurement series to disk and simply
displaying data on screen.
"""
import argparse
import collections
import curses
import datetime
import functools
import json
import struct
import time

import numpy as np
import serial

import utils


def main_loop(stdscr, port, simulate):
  """Runs the main loop.

  Args:
    stdscr: curses window object.
    port: str, upload port.
    simulate: bool, whether to simulate a connected device.
  """
  serial_class = utils.MockSerial if simulate else serial.Serial
  serial_port = serial_class(port=port, baudrate=9600)

  # We average temperatures over the previous 100 measurements.
  basket_temperatures = collections.deque(maxlen=100)
  group_temperatures = collections.deque(maxlen=100)

  curses.curs_set(0)
  stdscr.nodelay(True)
  stdscr.clear()

  record_mode = False

  while True:
    # Read serial one measurement at a time.
    measurement = utils.read_measurement(serial_port)
    elapsed_time = measurement[0]
    basket_temperature, group_temperature, state = measurement[-3:]

    basket_temperatures.append(basket_temperature)
    group_temperatures.append(group_temperature)

    # The space key toggles the recording mode.
    try:
      key = stdscr.getkey()
    except:
      key = None
    if key == ' ':
      record_mode = not record_mode

    # Update the terminal display
    stdscr.clear()
    num_rows, num_cols = stdscr.getmaxyx()
    section_width = num_cols // 4

    stdscr.addstr(0, 0, 'Elapsed time', curses.A_BOLD)
    stdscr.addstr(1, 0, '{:.2f}'.format(elapsed_time))

    stdscr.addstr(0, section_width, 'Group temperature', curses.A_BOLD)
    mean = np.mean(group_temperatures)
    stdscr.addstr(1, section_width,
                  ('{:.3f}C'.format(mean) if mean > -273.0 else '---C'))

    stdscr.addstr(0, 2 * section_width, 'Basket temperature', curses.A_BOLD)
    basket_temperatures_mean = np.mean(basket_temperatures)
    stdscr.addstr(1, 2 * section_width,
                  ('{:.3f}C'.format(mean) if mean > -273.0 else '---C'))

    stdscr.addstr(0, 3 * section_width, 'State', curses.A_BOLD)
    stdscr.addstr(1, 3 * section_width, str(utils.State(state)))

    record_mode_string = 'Recording' if record_mode else 'Not recording'
    stdscr.addstr(num_rows - 1, num_cols - len(record_mode_string) - 1,
                  record_mode_string)
    stdscr.refresh()

    # A new measurement series begins with the state "START".
    if state == utils.State.START:
      # We will write the measurement series to a JSON file with the current
      # date and time as its name.
      file_path = 'data/{}.json'.format(''.join(
          datetime.datetime.now().isoformat('-', timespec='seconds').split(':')
      ))
      # The shot data to be serialized to JSON.
      shot_data = {
        'posix time': time.time(),
        'description': "",
        'time': [],
        'basket_temperature': [],
        'group_temperature': [],
      }
    # A measurement series ends with the state "STOP".
    elif state == utils.State.STOP:
      # When the measurement series ends, we serialize it to a JSON file.
      if record_mode and not simulate:
        with open(file_path, 'w') as f:
          json.dump(shot_data, f)
    # When running, we record shot data.
    elif state == utils.State.RUNNING:
      shot_data['time'].append(elapsed_time)
      shot_data['basket_temperature'].append(basket_temperature)
      shot_data['group_temperature'].append(group_temperature)


if __name__ == '__main__':
  parser = argparse.ArgumentParser(
      description='Run the espresso shot management device.')
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
      '--simulate', action='store_true',
      help='Simulate a connected Arduino device.')
  args = parser.parse_args()

  fqbn = args.fqbn
  recompile = args.recompile
  simulate = args.simulate
  port = None if simulate else utils.find_port_if_not_specified(fqbn, args.port)

  if recompile and not simulate:
    utils.compile_and_upload(fqbn=fqbn, port=port)
    # Give the Arduino device some time to become operational.
    time.sleep(2.0)

  curses.wrapper(functools.partial(
      main_loop,
      port=port,
      simulate=simulate,
  ))
