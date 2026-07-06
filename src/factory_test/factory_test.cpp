/**
 * @file factory_test.cpp
 * @author Forairaaaaa
 * @brief
 * @version 0.1
 * @date 2023-06-06
 *
 * @copyright Copyright (c) 2023
 *
 */
#include "factory_test.h"
#include "../setting/setting.h"
#include "../sleep_week/sleep_week_scheduler.h"
#include <sys/time.h>

void FactoryTest::init()
{
    _power_on();

    _rtc_init();

    _key_init();

    _disp_init();

    // Encoder init
    _enc.attachHalfQuad(40, 41);

    _enc.setCount(0);

    // Load persisted settings (speaker on/off, etc.)
    setting.loadConfig();
    g_speakerEnabled = setting.config.speakerEnabled;

    // Sync ESP32 system time from BM8563 RTC so the clock, SLEEP&WEEK scheduler
    // and countdown popup work globally from boot.
    {
        I2C_BM8563_TimeTypeDef rt;
        I2C_BM8563_DateTypeDef rd;
        _rtc.getTime(&rt);
        _rtc.getDate(&rd);

        struct tm ti = {};
        ti.tm_year  = rd.year - 1900;
        ti.tm_mon   = rd.month - 1;
        ti.tm_mday  = rd.date;
        ti.tm_wday  = rd.weekDay;
        ti.tm_hour  = rt.hours;
        ti.tm_min   = rt.minutes;
        ti.tm_sec   = rt.seconds;
        ti.tm_isdst = -1;

        time_t t = mktime(&ti);
        if (t > 0) {
            struct timeval tv = { t, 0 };
            settimeofday(&tv, nullptr);
        }
    }

    // Load scheduler config so it runs globally (high-privilege persistence)
    sleepWeekScheduler.loadConfig();

    // Register RTC alarm + power-sleep callbacks (needed for global operation)
    _scheduler_register_callbacks();

    // if (_check_test_mode())
    // {
    //     start_factory_test();
    // }
}

void FactoryTest::start_factory_test()
{
    _wifi_start_test_task();
    _disp_test();
    _rtc_test();
    _encoder_test_new();
    _io_test();
    _wifi_test();
    _ble_test();
    _rtc_wakeup_test();
}
