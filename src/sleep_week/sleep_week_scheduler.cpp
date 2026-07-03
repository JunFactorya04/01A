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
    
    // Set RTC alarm for wake time
    setRTCAlarm(config.timeConfig.wakeHour, config.timeConfig.wakeMinute);
    
    // Stop active capture modes while scheduler is sleeping.
    autoShoot.stop();
    timelapse.stop();
}

void SleepWeekScheduler::wakeUp() {
    state.isSleeping = false;
    
    // Start selected mode
    switch (config.mode) {
        case MODE_AUTO_SHOOT:
            autoShoot.start();
            timelapse.stop();
            break;
        case MODE_TIMELAPSE:
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
        // Navigate between days
        int newIndex = editMode.selectedIndex + (delta > 0 ? 1 : -1);
        if (newIndex >= 0 && newIndex <= 6) {
            editMode.selectedIndex = newIndex;
        }
    } else if (editMode.state == SleepWeekEditMode::SELECTING_TIME) {
        // Adjust time
        if (editMode.timeField == 0) {
            // Hour
            int newHour = config.timeConfig.sleepHour + (delta > 0 ? 1 : -1);
            if (newHour > 23) newHour = 0;
            if (newHour < 0) newHour = 23;
            config.timeConfig.sleepHour = newHour;
        } else {
            // Minute
            int newMinute = config.timeConfig.sleepMinute + (delta > 0 ? 1 : -1);
            if (newMinute > 59) newMinute = 0;
            if (newMinute < 0) newMinute = 59;
            config.timeConfig.sleepMinute = newMinute;
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
        // Toggle selected day
        toggleDay(editMode.selectedIndex);
    } else if (editMode.state == SleepWeekEditMode::SELECTING_TIME) {
        // Switch between hour/minute
        editMode.timeField = (editMode.timeField == 0) ? 1 : 0;
    } else if (editMode.state == SleepWeekEditMode::SELECTING_MODE) {
        // Confirm mode selection
        enableScheduler(true);
    }
}

void SleepWeekScheduler::handleButtonLongPress() {
    // Disable scheduler
    enableScheduler(false);
    editMode.state = SleepWeekEditMode::IDLE;
    editMode.selectedIndex = 0;
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
    // Set RTC alarm for wake time
    // In real implementation, use RTC library
    // For now, placeholder
}
