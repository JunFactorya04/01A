#include "dev_mode.h"
#include "../multi_box/multi_box_controller.h"
#include "../trigger_mode/trigger_mode.h"
#include "../multi_box/node_manager.h"
#include <Preferences.h>

#define NVS_NS       "devMode"
#define KEY_INTERVAL "devInt"
#define KEY_SINGLE   "devSingle"

DevMode devMode;

// ============ CONFIG ============
void DevMode::loadConfig() {
    Preferences p;
    p.begin(NVS_NS);
    config.intervalMs = p.getUInt(KEY_INTERVAL, 5000);
    config.singleShot = p.getBool(KEY_SINGLE, false);
    p.end();
    validateConfig();
}

void DevMode::saveConfig() {
    validateConfig();
    Preferences p;
    p.begin(NVS_NS);
    p.putUInt(KEY_INTERVAL, config.intervalMs);
    p.putBool(KEY_SINGLE, config.singleShot);
    p.end();

    // The bulb limits and threshold edited here are MultiBox's own fields, so
    // they have to be persisted through MultiBox -- otherwise tuning on the
    // bench would be lost the moment the real mode reloaded its config.
    // saveParams(), not saveConfig(): the latter would also write the borrowed
    // MAIN role into NVS.
    multiBox.saveParams();
}

void DevMode::validateConfig() {
    if (config.intervalMs < 500) config.intervalMs = 500;
    if (config.intervalMs > 600000) config.intervalMs = 600000;
}

// ============ LIFECYCLE ============
void DevMode::init() {
    loadConfig();

    // ORDER MATTERS. multiBox.init() calls loadConfig(), which reloads
    // config.role from NodeManager -- so setting the role BEFORE init() had it
    // silently overwritten straight back to whatever the box really is. With
    // the role back at NONE, ensureSensorStarted() bailed out (no TF-Luna),
    // session stayed IDLE (simulateStart() only acts on READY, so START did
    // nothing) and timeUntilNextCycle() returned 0 (no countdown). One
    // ordering mistake, three symptoms that looked unrelated.
    multiBox.init();   // sensor + ESP-NOW, exactly as the real mode does

    // Now take the MAIN role. This is what makes the harness worth anything:
    // MultiBox::update() then runs MAIN's real sensor logic, and
    // MultiBoxController runs the real bulb open/close and timing.
    //
    // The override MUST reach NodeManager too: MultiBoxController::update()
    // gates the whole bulb-closing path on nodeManager.selfRole(), not on
    // multiBox.config.role. Setting only the latter (an earlier attempt at
    // keeping this RAM-only) meant centerCloseBulbIfDue() never ran -- the
    // shutter stayed open past maxBulbSec, the session never left EXPOSING,
    // and so the interval never counted again.
    //
    // setSelfRole() is still RAM-only by itself -- only saveAll() persists --
    // and teardown() puts the real role back and rewrites NVS, so a bench
    // session cannot leave the box reconfigured.
    _savedRole = multiBox.config.role;
    _roleSaved = true;
    multiBox.config.role = MBRole::MAIN;
    nodeManager.setSelfRole(MBRole::MAIN);

    multiBox.ensureSensorStarted();   // init() skipped it while the role was still NONE
    multiBox.state.session = MultiBoxState::READY;   // armed, waiting for the first cycle

    state.running = false;
    state.cycles = 0;
    state.lastCycleMs = millis();

    editMode = DevModeEditMode();
}

void DevMode::teardown() {
    stop();

    // Close anything still open and drop the radio, the same way leaving
    // MULTI BOX does -- this mode really is MULTI BOX underneath.
    multiBoxController.requestExit();

    saveConfig();

    if (_roleSaved) {
        // Put the real role back in both places, and rewrite NVS so that even
        // if something persisted the borrowed one mid-session (peer bookkeeping
        // calls saveAll() on its own), the box ends up as it started.
        multiBox.config.role = _savedRole;
        nodeManager.setSelfRole(_savedRole);
        nodeManager.saveAll();
        _roleSaved = false;
    }
}

void DevMode::start() {
    state.running = true;
    state.cycles = 0;
    state.cycleInFlight = false;
    // Wait a FULL interval before the first shot. Firing immediately was
    // convenient on a desk but wrong in use: pressing START and having the
    // shutter open on the spot leaves no time to put the box down and walk
    // out of frame.
    state.lastCycleMs = millis();
}

void DevMode::stop() {
    state.running = false;
    state.cycleInFlight = false;
    multiBoxController.forceCloseBulbIfExposing();
}

unsigned long DevMode::timeUntilNextCycle() const {
    if (!state.running) return 0;
    if (state.cycleInFlight) return 0;                              // a cycle is running
    if (multiBox.state.session != MultiBoxState::READY) return 0;   // busy, not waiting
    unsigned long elapsed = millis() - state.lastCycleMs;
    if (elapsed >= config.intervalMs) return 0;
    return config.intervalMs - elapsed;
}

// ============ UPDATE ============
void DevMode::update() {
    // All the real work: sensor polling, MAIN's finish-line detection, bulb
    // timing, rearm. Untouched, so whatever it does here it will do in the
    // field with two boxes.
    multiBox.update();

    if (!state.running) return;

    // Cycle just finished (shutter closed, rest window done). THIS is when the
    // interval starts counting -- not when the cycle was fired.
    if (state.cycleInFlight && multiBox.state.session == MultiBoxState::READY) {
        state.cycleInFlight = false;
        state.lastCycleMs = millis();
        if (config.singleShot) {
            state.running = false;
        }
        return;
    }

    // The one substitution: an interval instead of a START packet.
    // simulateStart() itself only acts when the session is READY, so an
    // exposure still in progress holds it off naturally too.
    if (!state.cycleInFlight &&
        millis() - state.lastCycleMs >= config.intervalMs &&
        multiBox.state.session == MultiBoxState::READY) {
        state.cycleInFlight = true;
        state.cycles++;
        multiBoxController.simulateStart();
    }
}

// ============ INPUT ============
void DevMode::handleEncoderRotate(int delta) {
    if (delta == 0) return;
    int dir = (delta > 0) ? 1 : -1;

    // MAIN: move between the two buttons.
    if (editMode.screen == DevModeEditMode::MAIN) {
        if (editMode.state == DevModeEditMode::SELECTING) {
            int v = (int)editMode.mainIndex + dir;
            if (v < 0) v = 1;
            if (v > 1) v = 0;
            editMode.mainIndex = (uint8_t)v;
        }
        return;
    }

    // Row navigation on whichever settings page is open.
    if (editMode.state == DevModeEditMode::SELECTING) {
        uint8_t* idx; int count;
        if (editMode.screen == DevModeEditMode::ADVANCE) { idx = &editMode.advIndex;    count = DEV_ADV_ROW_COUNT; }
        else if (editMode.screen == DevModeEditMode::SENSOR) { idx = &editMode.sensorIndex; count = DEV_SENSOR_ROW_COUNT; }
        else { idx = &editMode.trigIndex; count = DEV_TRIG_ROW_COUNT; }

        int v = (int)(*idx) + dir;
        if (v < 0) v = count - 1;
        if (v >= count) v = 0;
        *idx = (uint8_t)v;
        return;
    }

    // EDITING: step the selected value.
    if (editMode.screen == DevModeEditMode::ADVANCE) {
        switch (advRow(editMode.advIndex)) {
            case DevRow::SHOOT_MODE:
                config.singleShot = !config.singleShot;
                break;
            case DevRow::INTERVAL: {
                uint32_t step = (config.intervalMs >= 10000) ? 1000 : 500;
                long v = (long)config.intervalMs + (long)dir * step;
                if (v < 500) v = 500;
                if (v > 600000) v = 600000;
                config.intervalMs = (uint32_t)v;
                break;
            }
            case DevRow::MIN_BULB: {
                int v = (int)multiBox.config.minBulbSec + dir;
                if (v < 1) v = 1;
                if (v > 900) v = 900;
                multiBox.config.minBulbSec = (uint16_t)v;
                if (multiBox.config.maxBulbSec <= multiBox.config.minBulbSec)
                    multiBox.config.maxBulbSec = multiBox.config.minBulbSec + 1;
                break;
            }
            case DevRow::MAX_BULB: {
                int v = (int)multiBox.config.maxBulbSec + dir;
                if (v <= (int)multiBox.config.minBulbSec) v = multiBox.config.minBulbSec + 1;
                if (v > 900) v = 900;
                multiBox.config.maxBulbSec = (uint16_t)v;
                break;
            }
            case DevRow::REST: {
                int step = (multiBox.config.rearmMs >= 2000) ? 500 : 100;
                int v = (int)multiBox.config.rearmMs + dir * step;
                if (v < 0) v = 0;
                if (v > 60000) v = 60000;
                multiBox.config.rearmMs = (uint16_t)v;
                break;
            }
            case DevRow::END_DELAY: {
                int v = (int)multiBox.config.endDelayMs + dir * 100;
                if (v < 0) v = 0;
                if (v > 10000) v = 10000;
                multiBox.config.endDelayMs = (uint16_t)v;
                break;
            }
            default: break;
        }
        return;
    }

    if (editMode.screen == DevModeEditMode::SENSOR) {
        switch (sensorRow(editMode.sensorIndex)) {
            case DevSensorRow::DETECT: {
                int v = (int)multiBox.config.detectThresholdCm + dir * 5;
                if (v < 5) v = 5;
                if (v > 500) v = 500;
                multiBox.config.detectThresholdCm = (uint16_t)v;
                break;
            }
            case DevSensorRow::RANGE_ON:
                multiBox.config.rangeFilterEnabled = !multiBox.config.rangeFilterEnabled;
                break;
            case DevSensorRow::RANGE_MIN: {
                int v = (int)multiBox.config.rangeMinCm + dir * 10;
                if (v < 0) v = 0;
                // Clamp against the far edge rather than swapping them, the
                // same rule Auto Shoot's range editing follows.
                if (v > (int)multiBox.config.rangeMaxCm - 10) v = multiBox.config.rangeMaxCm - 10;
                multiBox.config.rangeMinCm = (uint16_t)v;
                break;
            }
            case DevSensorRow::RANGE_MAX: {
                int v = (int)multiBox.config.rangeMaxCm + dir * 10;
                if (v < (int)multiBox.config.rangeMinCm + 10) v = multiBox.config.rangeMinCm + 10;
                if (v > 3000) v = 3000;
                multiBox.config.rangeMaxCm = (uint16_t)v;
                break;
            }
            default: break;
        }
        return;
    }

    // TRIGOUT
    switch (trigRow(editMode.trigIndex)) {
        case DevTrigRow::CH_TRIGGER:
            triggerMode.config.triggerEnabled = !triggerMode.config.triggerEnabled;
            triggerMode.saveConfig();
            break;
        case DevTrigRow::CH_REMOTE:
            triggerMode.config.remoteEnabled = !triggerMode.config.remoteEnabled;
            triggerMode.saveConfig();
            break;
        case DevTrigRow::CH_BLE:
            triggerMode.config.bluetoothEnabled = !triggerMode.config.bluetoothEnabled;
            triggerMode.saveConfig();
            break;
        default: break;
    }
}

void DevMode::handleButtonPress() {
    if (editMode.screen == DevModeEditMode::MAIN) {
        if (editMode.mainIndex == 0) {
            if (state.running) stop(); else start();
        } else {
            editMode.screen = DevModeEditMode::ADVANCE;
            editMode.advIndex = 0;
            editMode.state = DevModeEditMode::SELECTING;
        }
        return;
    }

    if (editMode.state == DevModeEditMode::EDITING) {
        editMode.state = DevModeEditMode::SELECTING;
        saveConfig();
        return;
    }

    if (editMode.screen == DevModeEditMode::ADVANCE) {
        switch (advRow(editMode.advIndex)) {
            case DevRow::BACK:
                editMode.screen = DevModeEditMode::MAIN;
                editMode.mainIndex = 0;
                saveConfig();
                return;
            case DevRow::SENSOR_PAGE:
                editMode.screen = DevModeEditMode::SENSOR;
                editMode.sensorIndex = 0;
                return;
            case DevRow::TRIGOUT_PAGE:
                editMode.screen = DevModeEditMode::TRIGOUT;
                editMode.trigIndex = 0;
                return;
            case DevRow::SHOOT_MODE:
                // A two-state choice: flip it on press instead of making the
                // user enter an edit state to turn a switch.
                config.singleShot = !config.singleShot;
                saveConfig();
                return;
            default:
                editMode.state = DevModeEditMode::EDITING;
                return;
        }
    }

    if (editMode.screen == DevModeEditMode::SENSOR) {
        DevSensorRow r = sensorRow(editMode.sensorIndex);
        if (r == DevSensorRow::BACK) {
            editMode.screen = DevModeEditMode::ADVANCE;
            saveConfig();
            return;
        }
        if (r == DevSensorRow::RANGE_ON) {
            multiBox.config.rangeFilterEnabled = !multiBox.config.rangeFilterEnabled;
            saveConfig();
            return;
        }
        editMode.state = DevModeEditMode::EDITING;
        return;
    }

    // TRIGOUT — every row is a toggle except BACK.
    if (trigRow(editMode.trigIndex) == DevTrigRow::BACK) {
        editMode.screen = DevModeEditMode::ADVANCE;
        saveConfig();
        return;
    }
    handleEncoderRotate(1);
}

void DevMode::handleButtonLongPress() {
    if (editMode.state == DevModeEditMode::EDITING) {
        editMode.state = DevModeEditMode::SELECTING;
        saveConfig();
        return;
    }
    switch (editMode.screen) {
        case DevModeEditMode::SENSOR:
        case DevModeEditMode::TRIGOUT:
            editMode.screen = DevModeEditMode::ADVANCE;
            saveConfig();
            return;
        case DevModeEditMode::ADVANCE:
            editMode.screen = DevModeEditMode::MAIN;
            editMode.mainIndex = 0;
            saveConfig();
            return;
        default:
            break;   // MAIN: the caller turns this into a mode exit
    }
}
