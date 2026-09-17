/**
 * @file battery.h
 * @brief Battery voltage reading and state-of-charge estimation
 *
 * Replaces the bare `analogReadMilliVolts(10) * 2` that used to be copy-pasted
 * at four call sites (view.cpp twice, ft_io_test.cpp twice), none of which
 * filtered the reading or turned it into anything a user could act on.
 *
 * Divider and pin match the reference M5Launcher board that uses this same
 * GPIO10 + x2 arrangement, so its validated 3300-4150mV working range carries
 * over directly.
 *
 * NO CHARGE / FULL DETECTION. Only the ADC pin is wired -- there is no PMIC
 * and no charger-IC STAT line on any GPIO -- so the only thing available is
 * the voltage, and it cannot distinguish "charging" from "charged" at all:
 * both sit at the charger's float voltage. Real charge reporting needs the
 * charger's STAT pin wired to a spare GPIO, i.e. a hardware change.
 */
#pragma once
#include <Arduino.h>

// Warn the user, but never interrupt a shot in progress.
static const uint32_t BATTERY_LOW_MV = 3500;

// Stop cleanly while there is still enough power to do it properly (close an
// open bulb exposure, persist config) instead of browning out mid-frame.
static const uint32_t BATTERY_CRITICAL_MV = 3300;

// Smoothed battery voltage in millivolts. Cheap to call every render frame --
// the blocking ADC burst is internally rate-limited, see battery.cpp.
uint32_t batteryMilliVolts();

// State of charge, 0-100. Linear over 3300..4150mV -- an approximation, not a
// real Li-ion discharge curve; see battery.cpp.
uint8_t batteryPercent();

// Running low: warn, keep working.
bool batteryLow();

// Out of usable charge: shut down gracefully.
bool batteryCritical();

// ---- charging ----
// Charging is NOT detectable from voltage on this board, and this was measured
// rather than assumed: with the filtering in battery.cpp settled, USB read
// 3810mV and the 18650 alone read 3838mV -- 28mV apart, i.e. indistinguishable.
// (An earlier unfiltered comparison appeared to show a 200mV gap; that turned
// out to be the +/-200mV ADC noise the filter now removes, not a real signal.)
//
// The honest fix is one wire: most charger ICs expose a STAT pin that pulls
// low while charging and releases when done. Route it to any free GPIO
// (6/7/8/9/11/12/14/16/17/18/21 are all unused on this board), then define
// BATTERY_CHARGE_STAT_PIN below and the charging bolt in the header lights up
// for real. Until then batteryCharging() is hard false rather than a guess
// that would be wrong as often as right.
//
// #define BATTERY_CHARGE_STAT_PIN 12
// #define BATTERY_CHARGE_STAT_ACTIVE_LOW 1

bool batteryCharging();
