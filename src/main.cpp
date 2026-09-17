/**
 * @file main.cpp
 * @author Forairaaaaa
 * @brief
 * @version 0.1
 * @date 2023-06-06
 *
 * @copyright Copyright (c) 2023
 *
 */
#include "factory_test/factory_test.h"
#include "common/hardware_config.h"
#include "auto_shoot/tf_luna.h"
#include "boot/boot_logo.h"

static FactoryTest ft;

void view_create(FactoryTest* ft);
void view_update();

void setup()
{
    // Nothing in this project ever called Serial.begin(), so every
    // Serial.print/printf scattered through the drivers and modes silently
    // produced nothing -- diagnosing anything by serial log was impossible.
    // Paired with -DARDUINO_USB_CDC_ON_BOOT=1 in platformio.ini, this routes
    // that output to the built-in USB-Serial-JTAG port (the flashing socket).
    Serial.begin(115200);

    ft.init();

    // GEOPIX boot logo: logo fade in -> text fade in -> hold -> fade out,
    // then fall straight through to the main UI (no key wait).
    //
    // Extra hold on a "cold" boot (VIN2 direct power, or an RTC scheduled
    // wake) that skipped the manual power button's ~2s hold: TF-Luna is on
    // the same power rail and needs real wall-clock time since power was
    // applied to finish its own physical power-on settling, independent of
    // any firmware call. The button-hold path already gets that time for
    // free; this closes the gap for the paths that don't, so the sensor
    // isn't queried (in AutoShoot::init(), on mode entry) before it's
    // actually ready — instead of a bare delay() that would just look like
    // a stall, it's spent extending the logo's hold, which is already
    // static and expected to sit there a moment.
    bootLogoPlay(ft._canvas, ft._manual_power_on ? 0 : 2000);

    view_create(&ft);

    // If we booted from an RTC scheduled wake inside the awake window,
    // auto-enter the configured capture mode (AUTO SHOOT / TIMELAPSE).
    ft._scheduler_boot_resume();
}

void loop() { view_update(); }
