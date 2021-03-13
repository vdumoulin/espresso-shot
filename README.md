# E61 Temperature Monitoring and Control System

**Disclaimer: modify your espresso machine at your own risk. My electronics
knowledge is virtually nonexistent, and my design should definitely be
approached with a healthy dose of skepticism.**

This repository contains the source code for an Arduino-powered temperature
monitoring and control system for espresso machines with an E61 grouphead. The
project is heavily inspired by [Howard Smith's setup](https://www.home-barista.com/tips/guide-to-managing-hx-brew-temperatures-t64840.html), which uses a small DC
fan to cool the grouphead instead of the usual cooling flush. This approach has
the advantage of not wasting water, which is good both environmentally and
financially.

I started this project to address an overheating issue on my Sanremo Treviso
espresso machine and figured others might also benefit from it, which is why I'm
open-sourcing the code and circuit design. This is not meant to be an easy and
straightforward build guide, but rather a (hopefully) useful starting point for
people interested in building something similar.

![Circuit diagram](espresso-shot.png?raw=true)

## Features

- Monitors basket and group temperatures using thermistors. The group
  thermistor is inserted in an adapter screwed into the M6 closure plug (such as
  [Eric's grouphead thermometer](https://www.home-barista.com/espresso-machines/monitoring-brew-temperature-e61-groups-t1352.html)).
  The basket thermistor is wrapped in foil tape and sandwiched between the
  portafilter basket and the grouphead.
- Controls a DC fan which cools the grouphead to a target temperature. The
  target temperature is user-selectable with two push buttons.
- Times the shot using a tilt switch taped to the brew lever.
- Displays basket temperature, group temperature, and shot time on an OLED
  screen.
- Sends time and temperature logging information over serial. A companion
  Python script listens to the serial channel and converts the information
  into JSON files that a companion Jupyter Notebook can then read and display.

## Requirements

- `python3`
  - `pytz`
  - `pyserial`
  - `seaborn`
- `arduino-cli`
  - `Adafruit_ADS1X15`
  - `Button`
  - `TaskScheduler`
  - `U8g2`

## Calibration

The group and basket thermistors' Steinhart-Hart model coefficients
(`{BASKET,GROUP}_SH_{A,B,C}`) and the voltage divider circuits' known resistance
(`{BASKET,GROUP}_KNOWN_RESISTANCE`) are hard-coded for my own setup and need to
be calibrated to the specific thermistors and resistances used in a new build:

- The known resistances can be measured empirically using a multimeter or set
  to the resistances' nominal values.
- The Steinhart-Hart coefficients can be estimated using an
  [online calculator](https://www.thinksrs.com/downloads/programs/therm%20calc/ntccalibrator/ntccalculator.html). The thermistors' resistances need to be measured
  for three known temperatures and input into the calculator. I find that one
  measurement is enough when using the following temperature-resistance pairs:
  - The thermistor's nominal resistance at 25°C.
  - The resistance at 75°C as predicted by the beta parameter model (using the
    thermistor's nominal beta).
  - An empirical resistance measurement at around 95°C.

## Usage

The basket thermistor is meant to be used to characterize the espresso machine's
thermal properties. The grouphead thermometer measures temperature a few
centimeters above the espresso puck, which means that the temperature it reads
is typically slightly higher than water temperature at the puck (assuming that
the grouphead acts as a heat sink and the shot has been running for some time).

By pulling throwaway shots using stale coffee beans &mdash; I personally would
not want to drink a shot that has been in contact with the basket thermistor
&mdash; we can monitor the temperature at both points and establish a
relationship between the temperature read by the grouphead thermistor and the
temperature at the espresso puck. I do not have experience with enough HX
espresso machines to draw a definitive conclusion, but I suspect that the
relationship is machine-dependent.

Once we know the relationship, we can target brew temperatures by setting the
corresponding grouphead temperature. I find that placing the DC fan a few
centimeters above the E61 grouphead _mushroom_ and having it point downwards
works well.

### Recording measurements

1. Connect the Arduino device to the computer with a USB cable.
2. Run `python3 espresso-shot.py --fqbn <YOUR_ARDUINO_FQBN>`.
3. Press the spacebar to turn the Python script's recording mode on.
4. Pull a shot.
5. Press the spacebar again to turn recording mode off. This allows to perform
   a cleaning flush without polluting the `data/` directory with spurious
   measurements.

### Displaying measurements

1. Open `Data Analysis.ipynb` by running `jupyter notebook` and opening the
   notebook with the web interface.
2. Run the _Imports_ and _Data loading_ cells.
3. Select the runs to display from the list that appears below the _Data
   loading_ cell.
4. Run the _Data plotting_ cell.

### Cooling the grouphead to a target temperature

1. Position the DC fan.
2. Power on the Arduino device.
3. Adjust the target temperature with the push buttons. The OLED screen will
   switch from showing the grouphead temperature to showing the target
   temperature for one second when the buttons are pressed.
4. Wait for the DC fan to cool the grouphead down to the target temperature. The
   fan turns on when the grouphead is above target temperature and turns off
   otherwise.
