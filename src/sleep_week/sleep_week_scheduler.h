/**
 * @file sleep_week_scheduler.h
 * @brief GEOPIX Sleep + Weekly Scheduler
 * @date 2026-07-03
 */

#pragma once
#include "../common/hardware_config.h"

// ============ WEEKLY ENABLE STRUCTURE ============
struct WeeklyEnable {
    bool monday = false;
    bool tuesday = false;
    bool wednesday = false;
    bool thursday = false;
    bool friday = false;
    bool saturday = false;
    bool sunday = false;
};

// ============ TIME STRUCTURE ============
struct TimeConfig {
    uint8_t sleepHour = 0;      // 0-23
    uint8_t sleepMinute = 0;    // 0-59
    uint8_t wakeHour = 8;       // 0-23
    uint8_t wakeMinute = 0;      // 0-59
};

// ============ MODE STRUCTURE ============
enum SchedulerMode {
    MODE_AUTO_SHOOT = 0,
    MODE_TIMELAPSE = 1,
    MODE_OFF = 2
};

// ============ CONFIG STRUCTURE ============
struct SleepWeekConfig {
    bool schedulerEnabled = false;
    WeeklyEnable weeklyEnable;
    TimeConfig timeConfig;
    SchedulerMode mode = MODE_OFF;
};

// ============ STATE STRUCTURE ============
struct SleepWeekState {
    bool isSleeping = false;
    bool isScheduled = false;
    uint8_t currentDay = 0;  // 0-6 (Sun-Sat)
    unsigned long lastCheck = 0;
};

// ============ EDIT MODE ============
struct SleepWeekEditMode {
    enum EditState {
        IDLE = 0,
        SELECTING_DAY = 1,
        SELECTING_TIME = 2,
        SELECTING_MODE = 3
    } state = IDLE;
    
    uint8_t selectedIndex = 0;  // 0-6: days, 7: sleep time, 8: wake time, 9: mode
    uint8_t timeField = 0;      // 0: hour, 1: minute
};

// ============ SCHEDULER CLASS ============
class SleepWeekScheduler {
public:
    SleepWeekConfig config;
    SleepWeekState state;
    SleepWeekEditMode editMode;
    
    SleepWeekScheduler();
    ~SleepWeekScheduler() = default;
    
    // ===== Initialization =====
    void init();
    void loadConfig();
    void saveConfig();
    
    // ===== Main Loop =====
    void update();
    
    // ===== Control =====
    void toggleDay(uint8_t day);
    void setSleepTime(uint8_t hour, uint8_t minute);
    void setWakeTime(uint8_t hour, uint8_t minute);
    void setMode(SchedulerMode mode);
    void enableScheduler(bool enable);
    
    // ===== Check Functions =====
    bool shouldSleepNow();
    bool shouldWakeNow();
    void enterSleepMode();
    void wakeUp();
    
    // ===== UI Interaction =====
    void handleEncoderRotate(int delta);
    void handleButtonPress();
    void handleButtonLongPress();
    
    // ===== Getters =====
    const char* getSelectedItemName();
    bool getDayEnabled(uint8_t day) const;
    const char* getModeString() const;
    bool isSchedulerEnabled() const;
    bool isSleeping() const;
    
private:
    void validateConfig();
    uint8_t getCurrentDayOfWeek();
    void setRTCAlarm(uint8_t hour, uint8_t minute);
};

// Global instance
extern SleepWeekScheduler sleepWeekScheduler;
