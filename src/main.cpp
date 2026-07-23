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
    ft.init();

    // GEOPIX boot logo: logo fade in -> text fade in -> hold -> fade out,
    // then fall straight through to the main UI (no key wait).
    bootLogoPlay(ft._canvas);

    view_create(&ft);

    // If we booted from an RTC scheduled wake inside the awake window,
    // auto-enter the configured capture mode (AUTO SHOOT / TIMELAPSE).
    ft._scheduler_boot_resume();
}

void loop() { view_update(); }
