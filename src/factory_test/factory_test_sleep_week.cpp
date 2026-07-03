/**
 * @file factory_test_sleep_week.cpp
 * @brief Sleep + Weekly Scheduler integration into FactoryTest
 * @date 2026-07-03
 */

#include "factory_test.h"
#include "../sleep_week/sleep_week_scheduler.h"
#include "../sleep_week/sleep_week_ui.h"
#include <sys/time.h>

extern FactoryTest* _ft;

// ============ RTC ALARM CALLBACK ============
// Called by SleepWeekScheduler when it needs to set a hardware wake alarm
static void _rtcAlarmSetter(uint8_t hour, uint8_t minute) {
    if (!_ft) return;
    I2C_BM8563_TimeTypeDef t;
    t.hours   = (int8_t)hour;
    t.minutes = (int8_t)minute;
    t.seconds = 0;
    _ft->_rtc.SetAlarmIRQ(t);
}

// ============ SLEEP WEEK TEST ============
void FactoryTest::_sleep_week_test() {

    // ── Sync ESP32 system time from BM8563 RTC ──────────────────────────
    // getLocalTimeSafe() uses time(nullptr) which needs system time set.
    // Without this, shouldSleepNow/shouldWakeNow always return false.
    {
        I2C_BM8563_TimeTypeDef rtcTime;
        I2C_BM8563_DateTypeDef rtcDate;
        _rtc.getTime(&rtcTime);
        _rtc.getDate(&rtcDate);

        struct tm ti = {};
        ti.tm_year  = rtcDate.year - 1900;   // BM8563 returns full year (e.g. 2026)
        ti.tm_mon   = rtcDate.month - 1;      // struct tm: 0-11
        ti.tm_mday  = rtcDate.date;
        ti.tm_wday  = rtcDate.weekDay;        // 0=Sun
        ti.tm_hour  = rtcTime.hours;
        ti.tm_min   = rtcTime.minutes;
        ti.tm_sec   = rtcTime.seconds;
        ti.tm_isdst = -1;

        time_t t = mktime(&ti);
        if (t > 0) {
            struct timeval tv = { t, 0 };
            settimeofday(&tv, nullptr);
        }
    }

    // Register RTC alarm callback (used by setRTCAlarm when entering sleep)
    SleepWeekScheduler::setAlarmCallback(_rtcAlarmSetter);

    // Initialize Sleep Week Scheduler
    sleepWeekScheduler.init();
    initSleepWeekUI();
    _sleep_week_enc_last_pos = _enc.getCount();
    _reset_mode_input_state();
    
    // Enter loop
    while (1) {
        _sleep_week_loop();
        
        if (_mode_exit_requested) {
            sleepWeekScheduler.enableScheduler(false);
            break;
        }
        
        delay(10);
    }
}

// ============ SLEEP WEEK LOOP ============
void FactoryTest::_sleep_week_loop() {
    
    // 1. Update Sleep Week Scheduler logic
    sleepWeekScheduler.update();
    
    // 2. Handle input
    handleSleepWeekInput();
    
    // 3. Render UI
    renderSleepWeekUI();
}

// ============ INPUT HANDLING ============
void FactoryTest::handleSleepWeekInput() {
    int encDelta = _read_encoder_delta(_sleep_week_enc_last_pos);
    if (encDelta != 0) {
        sleepWeekScheduler.handleEncoderRotate(encDelta);
    }

    ButtonEvent event = _read_mode_button_event();
    if (event == ButtonEvent::ShortPress) {
        handleSleepWeekButtonShortPress();
    } else if (event == ButtonEvent::LongPress) {
        handleSleepWeekButtonLongPress();
    }
}

void FactoryTest::handleSleepWeekButtonShortPress() {
    
    // Toggle selected day or confirm
    sleepWeekScheduler.handleButtonPress();
    _tone(800, 100);
}

void FactoryTest::handleSleepWeekButtonLongPress() {
    sleepWeekScheduler.handleButtonLongPress();
    _mode_exit_requested = true;
    _tone(1500, 150);
}
