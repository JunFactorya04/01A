/**
 * @file factory_test_sleep_week.cpp
 * @brief Sleep + Weekly Scheduler integration into FactoryTest
 * @date 2026-07-03
 */

#include "factory_test.h"
#include "../sleep_week/sleep_week_scheduler.h"
#include "../sleep_week/sleep_week_ui.h"
#include "../auto_shoot/auto_shoot.h"
#include "../timelapse/timelapse.h"
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

// ============ POWER-SLEEP CALLBACK ============
// Called by SleepWeekScheduler::enterSleepMode() to actually power off.
// The RTC wake alarm has already been set; it will boot the device at wake time.
static void _schedulerPowerSleep() {
    if (!_ft) return;
    if (_ft->_canvas) {
        _ft->_canvas->fillScreen(0x0000);
        _ft->_canvas_update();
    }
    delay(200);
    digitalWrite(POWER_HOLD_PIN, LOW);   // release soft-power latch -> power off
    while (1) { delay(1000); }           // halt (USB-powered fallback)
}

// ============ REGISTER SCHEDULER CALLBACKS (called at boot) ============
void FactoryTest::_scheduler_register_callbacks() {
    SleepWeekScheduler::setAlarmCallback(_rtcAlarmSetter);
    SleepWeekScheduler::setSleepCallback(_schedulerPowerSleep);
}

// ============ BOOT AUTO-RESUME ============
// If the scheduler is enabled and we are inside today's awake window,
// automatically enter the configured capture mode after an RTC wake.
void FactoryTest::_scheduler_boot_resume() {
    if (!sleepWeekScheduler.shouldBeAwakeNow()) return;
    switch (sleepWeekScheduler.getMode()) {
        case MODE_AUTO_SHOOT: _auto_shoot_test(); break;
        case MODE_TIMELAPSE:  _timelapse_test();  break;
        default: break;
    }
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

    // Register RTC alarm + power-sleep callbacks
    _scheduler_register_callbacks();

    // Initialize Sleep Week Scheduler
    sleepWeekScheduler.init();
    initSleepWeekUI();
    _sleep_week_enc_last_pos = _enc.getCount();
    _reset_mode_input_state();
    
    // Enter loop
    while (1) {
        _sleep_week_loop();
        
        if (_mode_exit_requested) {
            // Persist settings; keep scheduler enabled so it runs from main / after sleep
            sleepWeekScheduler.saveConfig();
            autoShoot.stop();   // stop any capture mode started by wakeUp()
            timelapse.stop();
            break;
        }
        
        delay(10);
    }
}

// ============ SLEEP WEEK LOOP ============
void FactoryTest::_sleep_week_loop() {
    
    // 1. Update Sleep Week Scheduler logic
    sleepWeekScheduler.update();

    // 2. Run active capture modes (started by wakeUp(), safe to call always —
    //    both guards against isRunning/enable internally)
    autoShoot.update();
    timelapse.update();
    
    // 3. Handle input
    handleSleepWeekInput();
    
    // 4. Render UI
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
