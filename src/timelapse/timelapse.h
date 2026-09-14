/**
 * @file timelapse.h
 * @brief GEOPIX Timelapse System - Time-based camera trigger
 * @date 2026-07-02
 */

#pragma once
#include "../common/hardware_config.h"

// ============ CONFIG STRUCTURE ============
struct TimelapseConfig {
    int intervalMs = 5000;      // time between shots (milliseconds) -- single source of truth
    int totalShots = 0;         // 0 = infinite, >0 = max shots -- single source of truth
    bool enable = false;        // master switch

    // Bulb / long-exposure mode (milky way / astro). While enabled, each
    // shot HOLDS the trigger HIGH for bulbExposureSec instead of firing a
    // brief pulse -- the camera itself must be set to BULB mode by the
    // photographer first; this device has no way to change that camera
    // setting remotely, it only controls how long the shutter release
    // stays closed.
    bool bulbEnabled = false;
    int bulbExposureSec = 15;   // 1-900s

    // Extra rest added on top of Interval, only while Bulb is on. Exists
    // specifically to give a BLE camera link real time to reconnect/settle
    // between shots (see RemoteManager::pressShutter() -- a write right
    // after a fresh reconnect can silently fail) without having to inflate
    // the user's actual desired Interval to get that margin. 0 = no change
    // from before this existed.
    int bulbSettleSec = 0;      // 0-120s

    // Video calculator (MAIN screen). intervalMs/totalShots above remain
    // the only real, persisted shooting parameters -- Shoot Duration and
    // Video Length shown/edited on the MAIN screen are DERIVED from them
    // (durationSec = intervalMs * totalShots / 1000; videoLengthSec =
    // totalShots / fps) and written back by solving for intervalMs or
    // totalShots. See timelapse.cpp's handleEncoderRotate() for the exact
    // "what stays fixed when you edit X" rules. videoFpsIndex is the only
    // extra field actually needed/persisted for this.
    uint8_t videoFpsIndex = 2;  // 0=24fps 1=25fps 2=30fps
};

// ============ STATE STRUCTURE ============
struct TimelapseState {
    unsigned long lastShotTime = 0;
    int shotCount = 0;
    bool isRunning = false;
    bool isPaused = false;   // paused mid-sequence (progress preserved)

    // Bulb exposure currently in progress. Implemented as a non-blocking
    // hold (checked every loop tick in update()), NOT a blocking delay --
    // a 10-30s+ blocking delay would freeze input handling and rendering
    // for the whole exposure, which is unacceptable for something this
    // long (unlike the existing ~6ms non-bulb pulse).
    bool isExposing = false;
    unsigned long exposureStartTime = 0;
    bool bulbFiredG2 = false;   // which pins were actually driven HIGH at
    bool bulbFiredG1 = false;   // exposure start, so the release matches

    // Settle Delay in progress: a distinct pause phase entered right after
    // a bulb exposure ends (before the normal Interval rest even starts
    // counting), not just extra time folded into the same countdown as
    // Interval. Only entered when bulbSettleSec > 0.
    bool isSettling = false;
    unsigned long settleStartTime = 0;
};

// ============ EDIT MODE ============
struct TimelapseEditMode {
    enum EditState {
        IDLE = 0,
        SELECTING = 1,
        EDITING = 2
    } state = IDLE;

    // MAIN = video calculator (default, matches the dominant "shooting for
    // a video" use case). ADVANCE = direct Interval/Total Shots entry plus
    // Bulb mode -- same MAIN/sub-screen split already used by Auto Shoot's
    // Advance screen and Trigger Mode's Bluetooth screen.
    enum Screen { MAIN = 0, ADVANCE = 1 } screen = MAIN;

    uint8_t selectedIndex = 0;  // MAIN: 0=Shoot Duration 1=Video Length 2=Video FPS 3=Advance 4=Control
    uint8_t advanceIndex = 0;   // ADVANCE: 0=Interval 1=Total Shots 2=Bulb Mode 3=Exposure
                                // 4=Settle Delay (5 rows total, windowed to 4 visible --
                                // see timelapse_ui.cpp's renderTimelapseAdvanceScreen())
    unsigned long enterTime = 0;
};

// ============ TIMELAPSE CLASS ============
class Timelapse {
public:
    TimelapseConfig config;
    TimelapseState state;
    TimelapseEditMode editMode;

    Timelapse();
    ~Timelapse() = default;

    // ===== Initialization =====
    void init();
    void loadConfig();
    void saveConfig();

    // ===== Main Loop =====
    void update();

    // ===== Control =====
    void start();          // fresh start (resets shot count)
    void stop();           // stop + reset run state (force-releases a mid-bulb exposure)
    void pause();          // pause, keep progress (force-releases a mid-bulb exposure)
    void resume();         // resume from pause
    void toggleRunPause(); // START -> PAUSE -> RESUME toggle

    // ===== Camera Trigger (non-bulb path) =====
    void triggerCamera();

    // ===== UI Interaction =====
    void handleEncoderRotate(int delta);
    void handleButtonPress();
    void handleButtonLongPress();

    // ===== Advance submenu =====
    bool inAdvanceScreen() const { return editMode.screen == TimelapseEditMode::ADVANCE; }

    // ===== Getters =====
    const char* getSelectedItemName();
    int getSelectedValue();
    const char* getStatusString();
    const char* getControlLabel() const;   // START / PAUSE / RESUME
    long getEstimatedDurationSec() const;  // total time to finish; -1 = infinite
    int getShotCount() const;
    int getRemainingShots() const;
    unsigned long getTimeUntilNextShot() const;   // ms until the next shot starts (0 if exposing/idle)
    unsigned long getBulbTimeRemaining() const;   // ms left in the current exposure, 0 if not exposing
    unsigned long getSettleTimeRemaining() const; // ms left in the Settle Delay pause, 0 if not settling
    float getVideoLengthSec() const;              // totalShots / fps, -1 = infinite (totalShots == 0)
    int getVideoFps() const;                      // resolved from videoFpsIndex (24/25/30)
    bool isRunning() const;
    bool isPaused() const;
    bool isEnabled() const;

private:
    void validateConfig();
    void startBulbExposure();
    void endBulbExposure();
    void forceReleaseBulbIfExposing();   // safety net for stop()/pause()
    long currentDurationSecOrBootstrap() const;   // getEstimatedDurationSec(), but never -1
    float currentVideoLengthSecOrBootstrap() const;
    int solveIntervalMs(long durationSec, int shots) const;   // overflow-safe rest-time solver, clamped
    long perShotMs() const;   // Interval + Bulb exposure (if enabled) -- real time per shot cycle
};

// Global instance
extern Timelapse timelapse;
