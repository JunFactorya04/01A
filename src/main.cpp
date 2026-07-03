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

static FactoryTest ft;

void view_create(FactoryTest* ft);
void view_update();

void setup()
{
    ft.init();
    view_create(&ft);
}

void loop() { view_update(); }
