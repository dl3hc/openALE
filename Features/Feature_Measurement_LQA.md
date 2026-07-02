# Feature: Measurement LQA (MLQA) – Propagation Analysis and Channel Measurement Framework

## Summary

The existing ALE Link Quality Analysis (LQA) mechanism is designed exclusively to support automatic channel selection during Automatic Link Establishment. This feature extends the existing implementation with a separate **Measurement LQA (MLQA)** subsystem that continuously records, analyzes, and visualizes received channel quality measurements without altering the behavior of the standardized ALE protocol.

The goal is to transform the accumulated LQA information into a long-term propagation measurement database that allows operators to study HF propagation, station reachability, and channel behavior over time.

Operational ALE continues to use the standard LQA database. The Measurement LQA subsystem consumes the same receive events but stores its own historical measurement records for analysis purposes.

---

# User Story

**As an HF radio operator,**

I want every valid ALE reception to be stored as a historical measurement,

so that I can analyze long-term propagation behavior, compare channel performance, identify the best operating windows, and better understand the influence of ionospheric conditions on communication reliability.

---

# Motivation

The current ALE implementation only stores the latest LQA values required for automatic channel selection.

While sufficient for automatic link establishment, this discards valuable information that could reveal long-term propagation trends.

HF propagation is strongly influenced by factors including:

* time of day
* sunrise and sunset (greyline)
* season
* solar activity
* geomagnetic disturbances
* frequency
* geographic path
* channel occupancy

Persisting historical measurements enables the software to evolve from a pure ALE controller into an HF propagation analysis tool while remaining fully compliant with MIL-STD-188-141.

---

# Design Goals

The Measurement LQA subsystem shall:

* operate independently of the operational ALE LQA database
* never modify ALE channel selection
* record every valid received measurement
* retain historical measurements
* provide statistical analysis
* provide graphical visualization
* allow future correlation with external space-weather data

---

# Measurement Sources

Measurements shall only be generated from **received** ALE frames.

Valid sources include:

* received Soundings
* received Calls
* received Responses
* received Acknowledgements
* received AMD messages
* received ALE traffic between third-party stations

The following events shall never create Measurement LQA entries:

* transmitted Calls
* transmitted Soundings
* transmitted Responses
* locally generated LQA values
* unanswered Calls

This guarantees that every stored record represents an actual RF observation.

---

# Measurement Record

Each received frame creates one immutable measurement entry.

Example:

```text
Timestamp

Remote Station

Frequency

Channel

Frame Type

SINAD

BER

RSSI

SNR

Decode Confidence

Local Receiver

Scan Group

LQA Score

Measurement Source
```

Future versions may additionally store:

* GPS position
* antenna configuration
* radio model
* transmit power (if reported)
* multipath estimation
* Doppler spread
* operator notes

---

# Historical Database

Unlike operational LQA, measurements are never overwritten.

Instead they form a chronological history.

Example:

```text
DL1ABC

14.346 MHz

2026-04-11 08:12

SINAD 21

BER 1

----------------------

2026-04-11 08:18

SINAD 23

BER 0

----------------------

2026-04-11 09:01

SINAD 16

BER 5

...
```

This enables trend analysis over arbitrary time periods.

---

# Visualization

A new **Propagation Analysis** section shall be added to the GUI.

Suggested pages:

## Station History

Displays all measurements for one station.

Visualizations:

* SINAD over time
* BER over time
* RSSI over time
* successful receptions
* sounding history

---

## Channel History

Displays historical performance of one frequency.

Examples:

* average SINAD
* reception count
* average BER
* best operating periods
* activity density

---

## Heat Maps

Examples:

Frequency vs Time

```text
          00 03 06 09 12 15 18 21

3 MHz

██████████

5 MHz

██████

7 MHz

████████

10 MHz

███

14 MHz

██████████

21 MHz

██
```

or

Station vs Frequency

```text
            3   5   7  10  14  21 MHz

DL1ABC

██████

K4XYZ

██████████

JA1AAA

██

VK3XXX

████
```

These views immediately reveal propagation patterns.

---

## Time-of-Day Analysis

Aggregate measurements by hour.

Questions answered:

* When is Station X usually reachable?
* Which frequencies work best in the morning?
* When does propagation collapse?

---

## Seasonal Analysis

Measurements grouped by:

* month
* season
* year

This allows operators to observe recurring seasonal propagation characteristics.

---

## Long-Term Solar Cycle Analysis

Because measurements are retained indefinitely, they can be compared over years.

Future versions may correlate measurements with:

* Sunspot Number (SSN)
* Solar Flux Index (SFI)
* K-index
* A-index
* X-ray flux
* geomagnetic storms

This enables investigation of how the approximately 11-year solar cycle influences station reachability and channel quality.

---

## Greyline Analysis

Measurements around sunrise and sunset can be highlighted automatically.

Operators can visualize:

* improved DX opportunities
* greyline enhancement
* transition periods

---

## Station Reachability Profile

Each station automatically develops its own propagation profile.

Example:

```text
Station

DL1ABC

Best Band:

40 m

Best Time:

18:00–23:00 UTC

Most Reliable Season:

Winter

Average SINAD:

24

Measurements:

1,846
```

---

## Propagation Explorer

Interactive filters:

* station
* frequency
* band
* scan group
* date
* UTC time
* month
* year
* frame type
* measurement source

All graphs update dynamically.

---

# Data Export

Operators should be able to export measurements as:

* CSV
* JSON
* SQLite

This enables external analysis using Python, R, MATLAB or scientific visualization software.

---

# Expected Benefits

The Measurement LQA subsystem extends ALE beyond automatic link establishment into a comprehensive HF propagation observatory.

It enables operators to:

* understand long-term propagation behavior
* identify optimal operating windows
* compare channel performance statistically
* investigate seasonal propagation effects
* study greyline enhancements
* evaluate the influence of space weather
* observe changes throughout the solar cycle
* optimize frequency planning based on measured evidence rather than intuition

Most importantly, this functionality is implemented entirely alongside the standardized ALE LQA mechanism, preserving full protocol compatibility while unlocking significant analytical capabilities.
