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
import enum
import functools
import json
import struct
import subprocess
import time

import numpy as np
import pytz
import serial


class State(enum.IntEnum):
  START = 0
  RUNNING = 1
  STOP = 2
  STOPPED = 3


def compile_and_upload(fqbn, port):
  """Compiles the Arduino sketch and uploads it to the device.

  Args:
    fbqn: str, fully qualified board name.
    port: str, upload port.
  """
  subprocess.run(['arduino-cli', 'compile', '--fqbn', fqbn, 'espresso-shot'])
  subprocess.run(['arduino-cli', 'upload', '-p', port, '--fqbn', fqbn,
                  'espresso-shot'])


def find_port_if_not_specified(fqbn, port):
  """Finds an upload port if it's left unspecified.

  If `port` is None, then uses `arduino-cli board list` to find all boards
  connected to the computer with the specified fully qualified board name
  and sets `port` to that of the first board found.

  Args:
    fbqn: str, fully qualified board name.
    port: str or None, upload port.

  Returns:
    port: str, the upload port.
  
  Raises:
    RuntimeError, if `port` is None and no board with the specified fully
    qualified board name is connected to the computer.
  """
  process = subprocess.Popen(
      ['arduino-cli', 'board', 'list', '--format', 'json'],
      stdout=subprocess.PIPE)

  devices = json.loads(process.communicate()[0].decode('utf-8'))
  for device in devices:
    if 'boards' in device and any(board['FQBN'] == fqbn
                                  for board in device['boards']):
      port = port or device['address']
      break

  if port is None:
    raise RuntimeError('no port specified and no board with the specified '
                       'FQBN was found.')

  return port


class MockSerial:
  """Mock serial port used to test the interface when no device is available.

  We simulate alternating between pulling a shot for 30 seconds and letting the
  machine idle for 30 seconds, but we have time run twice as fast for
  convenience.
  """

  def __init__(self, **kwargs):
    self._time = 0
    self._period = 30
    self._running = True

  def readline(self):
    # One simulated second lasts half a real-time second.
    time.sleep(0.5)

    # Sample random basket and group temperatures.
    basket_temperature = np.random.normal(loc=92.0, scale=0.5)
    group_temperature = np.random.normal(loc=92.0, scale=0.5)

    # The device displays the previous shot's time when the machine is idle.
    elapsed_time = self._time if self._running else self._period

    # Determine the device's state.
    if self._running:
      # The state is 'START' at the first measurement of a shot, and 'RUNNING'
      # afterwards.
      state = State.START if self._time == 0 else State.RUNNING
    else:
      # The first measurement at the end of the shot has state 'STOP', and
      # subsequent measurements have state 'STOPPED'.
      state = (
          State.STOP if (self._time == self._period and self._running)
          else State.STOPPED)

    # Advance simulated time by one second, and reset to zero after 30 seconds
    # has passed.
    self._time = (self._time + 1) % (self._period + 1)

    # Switch between "pulling a shot" and "idle" at every cycle.
    if self._time == 0:
      self._running = not self._running

    return  '{},{},{},{}'.format(
        elapsed_time, basket_temperature, group_temperature, int(state)
    ).encode('utf-8')


def main_loop(stdscr, port, simulate):
  """Runs the main loop.

  Args:
    stdscr: curses window object.
    port: str, upload port.
    simulate: bool, whether to simulate a connected device.
  """
  serial_class = MockSerial if simulate else serial.Serial
  serial_port = serial_class(port=port, baudrate=9600)

  # We average temperatures over the previous 100 measurements.
  basket_temperatures = collections.deque(maxlen=100)
  group_temperatures = collections.deque(maxlen=100)

  curses.curs_set(0)
  stdscr.nodelay(True)
  stdscr.clear()

  record_mode = False

  while True:
    # Read serial one measurement at a time. Measurements are 14-bytes
    # sequences: 4 bytes each for the elapsed_time, group_temperature, and
    # basket_temperature floats, and 2 bytes for the machine state unsigned int
    # (0, 1, 2, and 3 map to START, RUNNING, STOP, and STOPPED, respectively).
    elapsed_time, basket_temperature, group_temperature, state = struct.unpack(
        'fffH', serial_port.read(14))

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
    stdscr.addstr(1, section_width, '{:.3f}'.format(group_temperature))

    stdscr.addstr(0, 2 * section_width, 'Basket temperature', curses.A_BOLD)
    stdscr.addstr(1, 2 * section_width, '{:.3f}'.format(basket_temperature))

    stdscr.addstr(0, 3 * section_width, 'State', curses.A_BOLD)
    stdscr.addstr(1, 3 * section_width, str(State(state)))

    record_mode_string = 'Recording' if record_mode else 'Not recording'
    stdscr.addstr(num_rows - 1, num_cols - len(record_mode_string) - 1,
                  record_mode_string)
    stdscr.refresh()

    # A new measurement series begins with the state "START".
    if state == State.START:
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
    elif state == State.STOP:
      # When the measurement series ends, we serialize it to a JSON file.
      if not simulate:
        with open(file_path, 'w') as f:
          json.dump(shot_data, f)
    # When running, we record shot data.
    elif state == State.RUNNING:
      shot_data['time'].append(elapsed_time)
      shot_data['basket_temperature'].append(basket_temperature)
      shot_data['group_temperature'].append(group_temperature)


if __name__ == '__main__':
  parser = argparse.ArgumentParser(
      description='Run the espresso shot management device.')
  parser.add_argument(
      '--fqbn', type=str, default='arduino:avr:uno',
      help='Fully Qualified Board Name.')
  parser.add_argument(
      '-p', dest='port', type=str, default=None,
      help='Upload port, e.g.: COM10 or /dev/ttyACM0')
  parser.add_argument(
      '--simulate', action='store_true',
      help='Simulate a connected Arduino device.')
  args = parser.parse_args()

  fqbn = args.fqbn
  simulate = args.simulate
  port = None if simulate else find_port_if_not_specified(fqbn, args.port)

  if not simulate:
    compile_and_upload(fqbn=fqbn, port=port)
  
  curses.wrapper(functools.partial(
      main_loop,
      port=port,
      simulate=simulate,
  ))
