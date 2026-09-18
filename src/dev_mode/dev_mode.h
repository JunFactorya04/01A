/**
 * @file dev_mode.h
 * @brief FOR DEVELOP — single-box bench harness for the MULTI BOX cycle
 *
 * MULTI BOX needs at least two boxes to exercise, which leaves its most
 * failure-prone half untestable: MAIN opening the bulb, MAIN's own TF-Luna
 * catching the finish line, minBulbSec/maxBulbSec, the rest window. This mode
 * replaces only the ESP-NOW START packet with an interval timer -- everything
 * downstream is the REAL MultiBox/MultiBoxController code, not a copy.
 * A harness that reimplemented the logic would prove nothing about it.
 *
 * Consequently it OWNS almost no state: the bulb limits, rest window and
 * detection threshold it shows and edits are `multiBox.config`'s own fields,
 * and the output channels are TriggerMode's. Only the interval is new.
 */

#pragma once
#include <Arduino.h>
#include "../multi_box/multi_box.h"

struct DevModeConfig {
    // Stands in for "a START node saw someone". Everything else about a cycle
    // is MultiBox's own configuration.
    // REST BETWEEN CYCLES, measured from the end of one cycle to the start of
    // the next -- the same rule Timelapse uses for its own Interval. Measuring
    // start-to-start instead meant a long exposure had already consumed the
    // whole interval by the time the shutter closed, so every cycle after the
    // first fired instantly with no wait at all.
    uint32_t intervalMs = 5000;   // 500..600000

    // SINGLE fires exactly one complete cycle per START press and then stops
    // itself; AUTO keeps cycling on the interval. SINGLE is what you want when
    // checking one frame properly -- framing, exposure, whether the shutter
    // actually closed -- without shots piling up behind you.
    bool singleShot = false;
};

struct DevModeState {
    bool          running = false;
    unsigned long lastCycleMs = 0;   // when the last simulated START fired
    uint32_t      cycles = 0;

    // A cycle has been fired and has not finished yet. Two jobs: SINGLE mode
    // stops when it clears, and the Interval only starts counting from that
    // moment -- see DevMode::update().
    bool cycleInFlight = false;
};

struct DevModeEditMode {
    enum EditState { SELECTING = 0, EDITING } state = SELECTING;
    // ADVANCE splits into two sub-pages rather than one long list: sensor
    // tuning and output channels are separate jobs, done at different times.
    enum Screen { MAIN = 0, ADVANCE = 1, SENSOR = 2, TRIGOUT = 3 } screen = MAIN;

    uint8_t mainIndex = 0;      // 0 = START/STOP, 1 = Advance
    uint8_t advIndex = 0;       // see DevRow
    uint8_t sensorIndex = 0;    // see DevSensorRow
    uint8_t trigIndex = 0;      // see DevTrigRow
};

// Every settings screen carries a trailing BACK row (see CLAUDE.md's UI
// standard) -- long-press works too, but it is not discoverable.
enum class DevRow : uint8_t {
    SHOOT_MODE, INTERVAL, MIN_BULB, MAX_BULB, REST, END_DELAY,
    SENSOR_PAGE, TRIGOUT_PAGE,
    BACK,
};
#define DEV_ADV_ROW_COUNT 9

enum class DevSensorRow : uint8_t {
    DETECT, RANGE_ON, RANGE_MIN, RANGE_MAX, BACK,
};
#define DEV_SENSOR_ROW_COUNT 5

enum class DevTrigRow : uint8_t {
    CH_TRIGGER, CH_REMOTE, CH_BLE, BACK,
};
#define DEV_TRIG_ROW_COUNT 4

class DevMode {
public:
    DevModeConfig   config;
    DevModeState    state;
    DevModeEditMode editMode;

    void init();       // entering: force MAIN role, bring up sensor + ESP-NOW
    void teardown();   // leaving: stop cleanly and restore the real role
    void loadConfig();
    void saveConfig();

    void update();

    void handleEncoderRotate(int delta);
    void handleButtonPress();
    void handleButtonLongPress();

    void start();
    void stop();

    bool onMainScreen() const { return editMode.screen == DevModeEditMode::MAIN; }
    bool inAdvance() const { return editMode.screen != DevModeEditMode::MAIN; }
    DevRow       advRow(uint8_t idx) const { return (DevRow)idx; }
    DevSensorRow sensorRow(uint8_t idx) const { return (DevSensorRow)idx; }
    DevTrigRow   trigRow(uint8_t idx) const { return (DevTrigRow)idx; }

    // ms until the next simulated START, 0 when not waiting for one
    unsigned long timeUntilNextCycle() const;

private:
    // The role this box really had, restored on exit so the bench harness
    // never silently reconfigures a box that was set up for a real shoot.
    MBRole _savedRole = MBRole::NONE;
    bool   _roleSaved = false;

    void validateConfig();
};

extern DevMode devMode;
