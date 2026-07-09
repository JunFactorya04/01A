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

    // USB/external power fallback: latch release doesn't cut power, so deep
    // sleep. Wake sources:
    //  - timer at the scheduled WAKE time (auto power-on for WEEK)
    //  - encoder button (G42, active-low) for manual wake
    // On battery the RTC alarm hardware-boots the board as before.
    delay(500);

    time_t nowT = time(nullptr);
    struct tm* lt = (nowT > 0) ? localtime(&nowT) : nullptr;
    if (lt) {
        int nowS  = lt->tm_hour * 3600 + lt->tm_min * 60 + lt->tm_sec;
        int wakeS = sleepWeekScheduler.config.timeConfig.wakeHour * 3600 +
                    sleepWeekScheduler.config.timeConfig.wakeMinute * 60;
        int diff = wakeS - nowS;
        if (diff <= 0) diff += 86400;    // wake time is tomorrow
        esp_sleep_enable_timer_wakeup((uint64_t)diff * 1000000ULL);
    }

    esp_sleep_enable_ext0_wakeup((gpio_num_t)POWER_BUTTON_PIN, 0);
    esp_deep_sleep_start();
}

// ============ REGISTER SCHEDULER CALLBACKS (called at boot) ============
void FactoryTest::_scheduler_register_callbacks() {
    SleepWeekScheduler::setAlarmCallback(_rtcAlarmSetter);
    SleepWeekScheduler::setSleepCallback(_schedulerPowerSleep);
}

// ============ BOOT AUTO-RESUME ============
// Only after a SCHEDULED wake (RTC alarm / deep-sleep timer): enter the
// configured capture mode, wait 10s (gear-up grace), then START it.
// A manual button power-on always goes to the main menu instead.
void FactoryTest::_scheduler_boot_resume() {
    if (_manual_power_on) return;                       // user booted -> main menu
    if (!sleepWeekScheduler.shouldBeAwakeNow()) return;

    SchedulerMode m = sleepWeekScheduler.getMode();
    if (m != MODE_AUTO_SHOOT && m != MODE_TIMELAPSE) return;

    // Arm delayed auto-START (5s), consumed inside the mode loop.
    // The mode loop shows a countdown popup until it fires.
    _scheduler_autostart_pending = true;
    _scheduler_autostart_at = millis() + 5000;

    switch (m) {
        case MODE_AUTO_SHOOT: _auto_shoot_test(); break;
        case MODE_TIMELAPSE:  _timelapse_test();  break;
        default: break;
    }
}

// ============ AUTOSTART COUNTDOWN POPUP ============
// Drawn OVER the mode UI (Auto Shoot / Timelapse) while a scheduled-wake
// auto-START is pending. Only ever visible after a scheduled wake — the
// pending flag is armed exclusively by _scheduler_boot_resume().
void FactoryTest::_scheduler_autostart_popup() {
    if (!_scheduler_autostart_pending || !_canvas) return;

    long remainMs = (long)(_scheduler_autostart_at - millis());
    if (remainMs < 0) remainMs = 0;
    int secs = (int)((remainMs + 999) / 1000);   // ceil -> 5,4,3,2,1

    // Centered popup box
    const int w = 150, h = 44;
    const int x = (240 - w) / 2, y = (135 - h) / 2;
    _canvas->fillRoundRect(x, y, w, h, 6, 0x0000);
    _canvas->drawRoundRect(x, y, w, h, 6, 0xFFE0);   // yellow border

    _canvas->setFont(&fonts::efontCN_12);
    _canvas->setTextDatum(top_center);
    _canvas->setTextColor(0xFFFF);
    _canvas->drawString("AUTO START", 120, y + 8);

    char buf[16];
    snprintf(buf, sizeof(buf), "in %ds", secs);
    _canvas->setTextColor(0xFFE0);
    _canvas->drawString(buf, 120, y + 24);
    _canvas->setTextDatum(top_left);

    _canvas_update();
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

    // 2. Run active capture modes ONLY when actually started by wakeUp().
    //    BUGFIX: autoShoot.update() has NO isRunning guard (it always polls
    //    TF-Luna for the realtime display). Calling it here every loop made
    //    the driver poll an un-initialized bus, hit 25 fails, and fire
    //    softResetSensor() (reg 0x21 write) every 3s — register writes stall
    //    the TF-Luna for many seconds, wrecking Auto Shoot afterwards.
    if (autoShoot.state.isRunning) autoShoot.update();
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
