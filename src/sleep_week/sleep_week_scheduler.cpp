/**
 * @file sleep_week_scheduler.cpp
 * @brief GEOPIX Sleep + Weekly Scheduler implementation
 * @date 2026-07-03
 */

#include "sleep_week_scheduler.h"
#include "../auto_shoot/auto_shoot.h"
#include "../timelapse/timelapse.h"
#include <Preferences.h>
#include <time.h>

static bool getLocalTimeSafe(struct tm& out)
{
    time_t now = time(nullptr);
    if (now <= 0)
    {
        return false;
    }

    struct tm* local = localtime(&now);
    if (local == nullptr)
    {
        return false;
    }

    out = *local;
    return true;
}

// Global instance
SleepWeekScheduler sleepWeekScheduler;

// Static callback for RTC alarm
void (*SleepWeekScheduler::s_alarmCb)(uint8_t, uint8_t) = nullptr;
void (*SleepWeekScheduler::s_sleepCb)() = nullptr;

// ============ CONSTRUCTOR ============
SleepWeekScheduler::SleepWeekScheduler() {
    // Default config set in header
}

// ============ INITIALIZATION ============
void SleepWeekScheduler::init() {
    // Load saved configuration
    loadConfig();
    
    // Validate config
    validateConfig();
    
    // Initialize state
    state.isSleeping = false;
    state.isScheduled = config.schedulerEnabled;
    state.currentDay = getCurrentDayOfWeek();
    state.lastCheck = millis();
    state.lastSleepMinuteKey = -1;
    state.lastWakeMinuteKey = -1;
}

// ============ CONFIG MANAGEMENT ============
void SleepWeekScheduler::loadConfig() {
    Preferences prefs;
    prefs.begin("sleepWeek");
    
    config.schedulerEnabled = prefs.getBool("schedulerEnabled", false);
    
    config.weeklyEnable.monday = prefs.getBool("monday", false);
    config.weeklyEnable.tuesday = prefs.getBool("tuesday", false);
    config.weeklyEnable.wednesday = prefs.getBool("wednesday", false);
    config.weeklyEnable.thursday = prefs.getBool("thursday", false);
    config.weeklyEnable.friday = prefs.getBool("friday", false);
    config.weeklyEnable.saturday = prefs.getBool("saturday", false);
    config.weeklyEnable.sunday = prefs.getBool("sunday", false);
    
    config.timeConfig.sleepHour = prefs.getUInt("sleepHour", 0);
    config.timeConfig.sleepMinute = prefs.getUInt("sleepMinute", 0);
    config.timeConfig.wakeHour = prefs.getUInt("wakeHour", 8);
    config.timeConfig.wakeMinute = prefs.getUInt("wakeMinute", 0);
    
    config.mode = (SchedulerMode)prefs.getUInt("mode", MODE_OFF);
    
    prefs.end();
}

void SleepWeekScheduler::saveConfig() {
    Preferences prefs;
    prefs.begin("sleepWeek");
    
    prefs.putBool("schedulerEnabled", config.schedulerEnabled);
    
    prefs.putBool("monday", config.weeklyEnable.monday);
    prefs.putBool("tuesday", config.weeklyEnable.tuesday);
    prefs.putBool("wednesday", config.weeklyEnable.wednesday);
    prefs.putBool("thursday", config.weeklyEnable.thursday);
    prefs.putBool("friday", config.weeklyEnable.friday);
    prefs.putBool("saturday", config.weeklyEnable.saturday);
    prefs.putBool("sunday", config.weeklyEnable.sunday);
    
    prefs.putUInt("sleepHour", config.timeConfig.sleepHour);
    prefs.putUInt("sleepMinute", config.timeConfig.sleepMinute);
    prefs.putUInt("wakeHour", config.timeConfig.wakeHour);
    prefs.putUInt("wakeMinute", config.timeConfig.wakeMinute);
    
    prefs.putUInt("mode", (uint8_t)config.mode);
    
    prefs.end();
}

void SleepWeekScheduler::validateConfig() {
    // Validate time
    if (config.timeConfig.sleepHour > 23) config.timeConfig.sleepHour = 0;
    if (config.timeConfig.sleepMinute > 59) config.timeConfig.sleepMinute = 0;
    if (config.timeConfig.wakeHour > 23) config.timeConfig.wakeHour = 8;
    if (config.timeConfig.wakeMinute > 59) config.timeConfig.wakeMinute = 0;
    
    // Validate mode
    if (config.mode > MODE_OFF) config.mode = MODE_OFF;
}

// ============ MAIN UPDATE ============
void SleepWeekScheduler::update() {
    // Check every second so minute transitions are not missed.
    unsigned long now = millis();
    if (now - state.lastCheck < 1000) return;
    state.lastCheck = now;
    
    // Update current day
    state.currentDay = getCurrentDayOfWeek();
    
    // Check if scheduler is enabled
    if (!config.schedulerEnabled) {
        state.isScheduled = false;
        return;
    }
    
    state.isScheduled = true;
    
    // Check if should sleep
    if (shouldSleepNow() && !state.isSleeping) {
        enterSleepMode();
    }
    
    // Check if should wake
    if (shouldWakeNow() && state.isSleeping) {
        wakeUp();
    }
}

// ============ CONTROL ============
void SleepWeekScheduler::toggleDay(uint8_t day) {
    switch (day) {
        case 0: config.weeklyEnable.sunday = !config.weeklyEnable.sunday; break;
        case 1: config.weeklyEnable.monday = !config.weeklyEnable.monday; break;
        case 2: config.weeklyEnable.tuesday = !config.weeklyEnable.tuesday; break;
        case 3: config.weeklyEnable.wednesday = !config.weeklyEnable.wednesday; break;
        case 4: config.weeklyEnable.thursday = !config.weeklyEnable.thursday; break;
        case 5: config.weeklyEnable.friday = !config.weeklyEnable.friday; break;
        case 6: config.weeklyEnable.saturday = !config.weeklyEnable.saturday; break;
    }
    saveConfig();
}

void SleepWeekScheduler::setSleepTime(uint8_t hour, uint8_t minute) {
    config.timeConfig.sleepHour = hour;
    config.timeConfig.sleepMinute = minute;
    saveConfig();
}

void SleepWeekScheduler::setWakeTime(uint8_t hour, uint8_t minute) {
    config.timeConfig.wakeHour = hour;
    config.timeConfig.wakeMinute = minute;
    saveConfig();
}

void SleepWeekScheduler::setMode(SchedulerMode mode) {
    config.mode = mode;
    saveConfig();
}

void SleepWeekScheduler::enableScheduler(bool enable) {
    config.schedulerEnabled = enable;
    state.isScheduled = enable;
    if (!enable)
    {
        state.isSleeping = false;
    }
    saveConfig();
}

// ============ CHECK FUNCTIONS ============
bool SleepWeekScheduler::shouldSleepNow() {
    // Check if current day is enabled
    bool dayEnabled = false;
    switch (state.currentDay) {
        case 0: dayEnabled = config.weeklyEnable.sunday; break;
        case 1: dayEnabled = config.weeklyEnable.monday; break;
        case 2: dayEnabled = config.weeklyEnable.tuesday; break;
        case 3: dayEnabled = config.weeklyEnable.wednesday; break;
        case 4: dayEnabled = config.weeklyEnable.thursday; break;
        case 5: dayEnabled = config.weeklyEnable.friday; break;
        case 6: dayEnabled = config.weeklyEnable.saturday; break;
    }
    
    if (!dayEnabled) return false;
    
    struct tm local_time;
    if (!getLocalTimeSafe(local_time)) {
        return false;
    }

    if (local_time.tm_hour != config.timeConfig.sleepHour ||
        local_time.tm_min != config.timeConfig.sleepMinute) {
        return false;
    }

    int32_t minute_key = (local_time.tm_yday * 1440) + (local_time.tm_hour * 60) + local_time.tm_min;
    if (state.lastSleepMinuteKey == minute_key) {
        return false;
    }

    state.lastSleepMinuteKey = minute_key;
    return true;
}

bool SleepWeekScheduler::shouldWakeNow() {
    struct tm local_time;
    if (!getLocalTimeSafe(local_time)) {
        return false;
    }

    if (local_time.tm_hour != config.timeConfig.wakeHour ||
        local_time.tm_min != config.timeConfig.wakeMinute) {
        return false;
    }

    int32_t minute_key = (local_time.tm_yday * 1440) + (local_time.tm_hour * 60) + local_time.tm_min;
    if (state.lastWakeMinuteKey == minute_key) {
        return false;
    }

    state.lastWakeMinuteKey = minute_key;
    return true;
}

void SleepWeekScheduler::enterSleepMode() {
    state.isSleeping = true;
    
    // Set RTC alarm for wake time (hardware wake source)
    setRTCAlarm(config.timeConfig.wakeHour, config.timeConfig.wakeMinute);
    
    // Stop active capture modes while scheduler is sleeping.
    autoShoot.stop();
    timelapse.stop();

    // Actually power off the device (real power sleep). RTC alarm will boot it
    // back at wake time. This does not return.
    if (s_sleepCb) {
        s_sleepCb();
    }
}

void SleepWeekScheduler::wakeUp() {
    state.isSleeping = false;
    
    // Start selected mode — must init before start to ensure GPIO/sensor ready
    switch (config.mode) {
        case MODE_AUTO_SHOOT:
            autoShoot.init();
            autoShoot.start();
            timelapse.stop();
            break;
        case MODE_TIMELAPSE:
            timelapse.init();
            timelapse.start();
            autoShoot.stop();
            break;
        case MODE_OFF:
            autoShoot.stop();
            timelapse.stop();
            break;
    }
}

// ============ UI INTERACTION ============
void SleepWeekScheduler::handleEncoderRotate(int delta) {
    if (editMode.state == SleepWeekEditMode::SELECTING_DAY) {
        // Navigate all controls: 0-6=days, 7=sleep time, 8=wake time, 9=mode, 10=scheduler on/off
        int newIndex = editMode.selectedIndex + (delta > 0 ? 1 : -1);
        if (newIndex >= 0 && newIndex <= 10) {
            editMode.selectedIndex = newIndex;
        }
    } else if (editMode.state == SleepWeekEditMode::SELECTING_TIME) {
        // Adjust time — selectedIndex 7=sleep, 8=wake
        bool isSleep = (editMode.selectedIndex == 7);
        uint8_t& refHour   = isSleep ? config.timeConfig.sleepHour   : config.timeConfig.wakeHour;
        uint8_t& refMinute = isSleep ? config.timeConfig.sleepMinute : config.timeConfig.wakeMinute;

        if (editMode.timeField == 0) {
            int newVal = (int)refHour + (delta > 0 ? 1 : -1);
            if (newVal > 23) newVal = 0;
            if (newVal < 0) newVal = 23;
            refHour = (uint8_t)newVal;
        } else {
            int newVal = (int)refMinute + (delta > 0 ? 1 : -1);
            if (newVal > 59) newVal = 0;
            if (newVal < 0) newVal = 59;
            refMinute = (uint8_t)newVal;
        }
        saveConfig();
    } else if (editMode.state == SleepWeekEditMode::SELECTING_MODE) {
        // Navigate between modes
        int newMode = config.mode + (delta > 0 ? 1 : -1);
        if (newMode > MODE_OFF) newMode = MODE_AUTO_SHOOT;
        if (newMode < MODE_AUTO_SHOOT) newMode = MODE_OFF;
        config.mode = (SchedulerMode)newMode;
        saveConfig();
    }
}

void SleepWeekScheduler::handleButtonPress() {
    if (editMode.state == SleepWeekEditMode::SELECTING_DAY) {
        if (editMode.selectedIndex <= 6) {
            // Toggle selected day
            toggleDay(editMode.selectedIndex);
        } else if (editMode.selectedIndex == 7 || editMode.selectedIndex == 8) {
            // Enter time editing for sleep (7) or wake (8)
            editMode.state = SleepWeekEditMode::SELECTING_TIME;
            editMode.timeField = 0;  // Start with hour
        } else if (editMode.selectedIndex == 9) {
            // Cycle through modes
            int newMode = (int)config.mode + 1;
            if (newMode > MODE_OFF) newMode = MODE_AUTO_SHOOT;
            config.mode = (SchedulerMode)newMode;
            saveConfig();
        } else if (editMode.selectedIndex == 10) {
            // Toggle scheduler on/off
            enableScheduler(!config.schedulerEnabled);
        }
    } else if (editMode.state == SleepWeekEditMode::SELECTING_TIME) {
        // Press once: switch hour→minute. Press again: exit back to navigation
        if (editMode.timeField == 0) {
            editMode.timeField = 1;
        } else {
            editMode.state = SleepWeekEditMode::SELECTING_DAY;
        }
    } else if (editMode.state == SleepWeekEditMode::SELECTING_MODE) {
        // Confirm mode, return to navigation
        enableScheduler(true);
        editMode.state = SleepWeekEditMode::SELECTING_DAY;
    }
}

void SleepWeekScheduler::handleButtonLongPress() {
    // Exit only — keep scheduler settings (high-privilege persistence).
    // Do NOT disable: the scheduler must keep running after leaving the mode.
    saveConfig();
    editMode.state = SleepWeekEditMode::IDLE;
    editMode.selectedIndex = 0;
}

// Returns true if within 1..5s of an upcoming SLEEP (enabled day) or WAKE event.
bool SleepWeekScheduler::getCountdown(int& secsLeft, const char*& label) {
    if (!config.schedulerEnabled) return false;

    struct tm t;
    if (!getLocalTimeSafe(t)) return false;

    int nowSec = t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;

    // SLEEP event — only if today is enabled
    bool dayEnabled = false;
    switch (t.tm_wday) {
        case 0: dayEnabled = config.weeklyEnable.sunday; break;
        case 1: dayEnabled = config.weeklyEnable.monday; break;
        case 2: dayEnabled = config.weeklyEnable.tuesday; break;
        case 3: dayEnabled = config.weeklyEnable.wednesday; break;
        case 4: dayEnabled = config.weeklyEnable.thursday; break;
        case 5: dayEnabled = config.weeklyEnable.friday; break;
        case 6: dayEnabled = config.weeklyEnable.saturday; break;
    }
    if (dayEnabled) {
        int tgt = config.timeConfig.sleepHour * 3600 + config.timeConfig.sleepMinute * 60;
        int diff = tgt - nowSec;
        if (diff >= 1 && diff <= 5) { secsLeft = diff; label = "SLEEP"; return true; }
    }

    // WAKE event
    {
        int tgt = config.timeConfig.wakeHour * 3600 + config.timeConfig.wakeMinute * 60;
        int diff = tgt - nowSec;
        if (diff >= 1 && diff <= 5) { secsLeft = diff; label = "WAKE"; return true; }
    }

    return false;
}

// True if scheduler enabled and now is within today's awake window [wake, sleep).
bool SleepWeekScheduler::shouldBeAwakeNow() {
    if (!config.schedulerEnabled) return false;

    struct tm t;
    if (!getLocalTimeSafe(t)) return false;

    // Today must be an enabled day
    bool dayEnabled = false;
    switch (t.tm_wday) {
        case 0: dayEnabled = config.weeklyEnable.sunday; break;
        case 1: dayEnabled = config.weeklyEnable.monday; break;
        case 2: dayEnabled = config.weeklyEnable.tuesday; break;
        case 3: dayEnabled = config.weeklyEnable.wednesday; break;
        case 4: dayEnabled = config.weeklyEnable.thursday; break;
        case 5: dayEnabled = config.weeklyEnable.friday; break;
        case 6: dayEnabled = config.weeklyEnable.saturday; break;
    }
    if (!dayEnabled) return false;

    int now   = t.tm_hour * 60 + t.tm_min;
    int wake  = config.timeConfig.wakeHour  * 60 + config.timeConfig.wakeMinute;
    int sleep = config.timeConfig.sleepHour * 60 + config.timeConfig.sleepMinute;

    if (wake == sleep) return false;
    if (wake < sleep)  return (now >= wake && now < sleep);      // same-day window
    return (now >= wake || now < sleep);                          // spans midnight
}

// ============ GETTERS ============
const char* SleepWeekScheduler::getSelectedItemName() {
    switch (editMode.selectedIndex) {
        case 0: return "Sunday";
        case 1: return "Monday";
        case 2: return "Tuesday";
        case 3: return "Wednesday";
        case 4: return "Thursday";
        case 5: return "Friday";
        case 6: return "Saturday";
        default: return "Unknown";
    }
}

bool SleepWeekScheduler::getDayEnabled(uint8_t day) const {
    switch (day) {
        case 0: return config.weeklyEnable.sunday;
        case 1: return config.weeklyEnable.monday;
        case 2: return config.weeklyEnable.tuesday;
        case 3: return config.weeklyEnable.wednesday;
        case 4: return config.weeklyEnable.thursday;
        case 5: return config.weeklyEnable.friday;
        case 6: return config.weeklyEnable.saturday;
        default: return false;
    }
}

const char* SleepWeekScheduler::getModeString() const {
    switch (config.mode) {
        case MODE_AUTO_SHOOT: return "Auto Shoot";
        case MODE_TIMELAPSE: return "Timelapse";
        case MODE_OFF: return "Off";
        default: return "Unknown";
    }
}

bool SleepWeekScheduler::isSchedulerEnabled() const {
    return config.schedulerEnabled;
}

bool SleepWeekScheduler::isSleeping() const {
    return state.isSleeping;
}

// ============ HELPER FUNCTIONS ============
uint8_t SleepWeekScheduler::getCurrentDayOfWeek() {
    struct tm local_time;
    if (!getLocalTimeSafe(local_time)) {
        return 0;
    }

    return (uint8_t)local_time.tm_wday;
}

void SleepWeekScheduler::setRTCAlarm(uint8_t hour, uint8_t minute) {
    if (s_alarmCb) {
        s_alarmCb(hour, minute);
    }
}
