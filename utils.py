"""Utility functions."""
import enum
import json
import struct
import subprocess
import time

import numpy as np

# Measurements contain 5 floats (elapsed_time, basket_resistance,
# group_resistance, basket_temperature, and group_temperature) and an int
# (state, for which 0, 1, 2, and 3 map to START, RUNNING, STOP, and STOPPED,
# respectively).
FORMAT_STRING = 'fffffi'


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


def read_measurement(serial_port):
  """Reads a measurement from the serial port.

  Args:
    serial_port: Serial, serial port to read from.

  Returns:
    tuple of (float, float, float, float, float, int) of form (elapsed_time,
    basket_resistance, group_resistance, basket_temperature, group_temperature,
    state).
  """
  return struct.unpack(
      FORMAT_STRING, serial_port.read(struct.calcsize(FORMAT_STRING)))


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

  def read(self, size=1):
    # One simulated second lasts half a real-time second.
    time.sleep(0.5)

    # Sample random basket and group readings.
    basket_resistance = np.random.normal(loc=10000.0, scale=100.0)
    group_resistance = np.random.normal(loc=10000.0, scale=100.0)
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

    return struct.pack(
        'fffffi',
        elapsed_time,
        basket_resistance,
        group_resistance,
        basket_temperature,
        group_temperature,
        int(state))
