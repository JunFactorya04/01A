/**
 * @file timelapse.cpp
 * @brief GEOPIX Timelapse System implementation
 * @date 2026-07-02
 */

#include "timelapse.h"
#include "../trigger_mode/trigger_mode.h"
#include <Preferences.h>

// Video playback frame rate presets shown/selected on the MAIN screen.
static const int VIDEO_FPS_PRESETS[3] = {24, 25, 30};

// Global instance
Timelapse timelapse;

// ============ CONSTRUCTOR ============
Timelapse::Timelapse() {
    // Default config set in header
}

// ============ INITIALIZATION ============
void Timelapse::init() {
    // Initialize G2 output pin (Port B Yellow, GPIO 2) for camera trigger
    pinMode(TRIGGER_G2_PIN, OUTPUT);
    digitalWrite(TRIGGER_G2_PIN, LOW);

    // Also init triggerMode so both G1/G2 GPIO are configured and config loaded
    triggerMode.init();

    // Load saved configuration
    loadConfig();

    // Validate config
    validateConfig();

    // Initialize state — always start idle (never auto-run on entering mode)
    config.enable      = false;
    state.isRunning    = false;
    state.isPaused     = false;
    state.isExposing   = false;
    state.lastShotTime = millis();
    state.shotCount    = 0;
}

// ============ CONFIG MANAGEMENT ============
void Timelapse::loadConfig() {
    Preferences prefs;
    prefs.begin("timelapse");

    config.intervalMs      = prefs.getInt("interval", 5000);
    config.totalShots      = prefs.getInt("totalShots", 0);
    config.enable          = prefs.getBool("enable", false);
    config.bulbEnabled     = prefs.getBool("bulbEn", false);
    config.bulbExposureSec = prefs.getInt("bulbSec", 15);
    config.bulbSettleSec   = prefs.getInt("bulbSettle", 0);
    config.videoFpsIndex   = (uint8_t)prefs.getInt("vidFps", 2);

    prefs.end();

    validateConfig();
}

void Timelapse::saveConfig() {
    Preferences prefs;
    prefs.begin("timelapse");

    prefs.putInt("interval", config.intervalMs);
    prefs.putInt("totalShots", config.totalShots);
    prefs.putBool("enable", config.enable);
    prefs.putBool("bulbEn", config.bulbEnabled);
    prefs.putInt("bulbSec", config.bulbExposureSec);
    prefs.putInt("bulbSettle", config.bulbSettleSec);
    prefs.putInt("vidFps", config.videoFpsIndex);

    prefs.end();
}

void Timelapse::validateConfig() {
    // Clamp Bulb Exposure
    if (config.bulbExposureSec < 1) config.bulbExposureSec = 1;
    if (config.bulbExposureSec > 900) config.bulbExposureSec = 900;

    // Clamp Settle Delay
    if (config.bulbSettleSec < 0) config.bulbSettleSec = 0;
    if (config.bulbSettleSec > 120) config.bulbSettleSec = 120;

    // Clamp Interval (100ms floor, 1 hour ceiling). When Bulb is on, Interval
    // is the REST period AFTER each exposure completes (see
    // startBulbExposure()/endBulbExposure()) -- exposure and rest are
    // sequential, not overlapping, so Interval never needs to be raised to
    // match the exposure length here. (An earlier version of this raised
    // Interval's floor to the exposure length under a "time between shot
    // STARTS" model -- that left zero real rest time whenever a user's
    // Interval landed at exactly the exposure length, which is exactly the
    // case the floor itself encouraged. Real cameras need actual time to
    // write/process a long exposure before the next one starts.)
    if (config.intervalMs < 100) config.intervalMs = 100;
    if (config.intervalMs > 3600000) config.intervalMs = 3600000;

    // Clamp Total Shots (0 to 10000)
    if (config.totalShots < 0) config.totalShots = 0;
    if (config.totalShots > 10000) config.totalShots = 10000;

    // Clamp Video FPS index
    if (config.videoFpsIndex > 2) config.videoFpsIndex = 2;
}

// ============ MAIN UPDATE ============
void Timelapse::update() {
    if (!state.isRunning) return;

    // Mid-bulb-exposure: only check for completion. Nothing else about the
    // sequence (new-shot decision, shot count) advances until the hold
    // ends -- this is what makes it non-blocking instead of a delay().
    if (state.isExposing) {
        unsigned long heldMs = millis() - state.exposureStartTime;
        if (heldMs >= (unsigned long)config.bulbExposureSec * 1000UL) {
            endBulbExposure();
        }
        return;
    }

    unsigned long now = millis();

    // Rest time before the next shot: Interval, plus Settle Delay when
    // Bulb is on (extra margin for a BLE link to reconnect/settle -- see
    // TimelapseConfig::bulbSettleSec).
    unsigned long waitMs = (unsigned long)config.intervalMs;
    if (config.bulbEnabled) waitMs += (unsigned long)config.bulbSettleSec * 1000UL;

    // Interval check
    if (now - state.lastShotTime >= waitMs) {

        // Sequence complete?
        if (config.totalShots != 0 && state.shotCount >= config.totalShots) {
            state.isRunning = false;
            state.isPaused  = false;   // finished (DONE) — keep shotCount for display
            config.enable   = false;
            return;
        }

        if (config.bulbEnabled) {
            startBulbExposure();   // non-blocking hold; finishes in a later update()
        } else {
            // Trigger camera with lock (unchanged quick-pulse path)
            triggerCamera();
            state.lastShotTime = now;
            state.shotCount++;
        }
    }
}

// ============ CAMERA TRIGGER (non-bulb quick pulse) ============
void Timelapse::triggerCamera() {
    // Acquire global trigger lock to prevent conflict with Auto Shoot
    if (!acquireTriggerLock()) return;

    // Fire G2 (Trigger) and/or G1 (Remote) based on TriggerMode config
    // If NO channel enabled (incl. Bluetooth), default to G2 (main always works)
    bool fireG2 = triggerMode.config.triggerEnabled;
    bool fireG1 = triggerMode.config.remoteEnabled;
    if (!fireG2 && !fireG1 && !triggerMode.config.bluetoothEnabled) fireG2 = true;

    if (fireG2) digitalWrite(TRIGGER_G2_PIN, HIGH);
    if (fireG1) digitalWrite(TRIGGER_G1_PIN, HIGH);
    if (triggerMode.config.beepEnabled && g_speakerEnabled) tone(BUZZ_PIN, 2500, 30);   // shot feedback
    delay(6);
    if (fireG2) digitalWrite(TRIGGER_G2_PIN, LOW);
    if (fireG1) digitalWrite(TRIGGER_G1_PIN, LOW);

    releaseTriggerLock();

    // 3rd channel: BLE camera remote — same command, after GPIO pulse
    triggerMode.fireBluetoothIfEnabled();
}

// ============ BULB EXPOSURE (non-blocking long hold) ============
// Starts a HELD trigger pulse for config.bulbExposureSec instead of the
// usual ~6ms tap. The camera must already be set to BULB mode by the
// photographer; holding G1/G2 closed (or the BLE shutter-press, see below)
// is what keeps its shutter open.
//
// BLE fires a real press-and-hold here too (RemoteManager::pressShutter(),
// released in endBulbExposure()/forceReleaseBulbIfExposing()) -- it used to
// be skipped entirely during bulb because CameraDriver only exposed a
// one-shot trigger(), which would have landed as an uncontrolled second
// shot on top of the held exposure. Now that shutterPress()/shutterRelease()
// exist as real hold primitives (mirroring each driver's own
// press-then-delay-then-release trigger() sequence, just without the fixed
// delay), BLE can hold exactly like G1/G2 does. This is UNVERIFIED against
// real camera hardware for a multi-second hold -- confirmed only by static
// protocol review (Sony/Canon/Nikon/Fuji's existing trigger() commands
// decompose cleanly into press/release pairs), not a live bulb exposure.
void Timelapse::startBulbExposure() {
    if (!acquireTriggerLock()) return;   // retry next tick; nothing advances meanwhile

    bool fireG2 = triggerMode.config.triggerEnabled;
    bool fireG1 = triggerMode.config.remoteEnabled;
    bool bleEnabled = triggerMode.config.bluetoothEnabled;
    if (!fireG2 && !fireG1 && !bleEnabled) fireG2 = true;

    bool blePressOk = triggerMode.pressBluetoothShutterIfEnabled();

    // If BLE is the only configured channel and its press failed (a BLE
    // reconnect attempt failing is real and more likely the shorter the
    // Interval is -- less time for the link to settle after the previous
    // shot's release), don't commit to a "phantom" exposure the camera
    // never actually started: shotCount would still advance and the
    // sequence would look normal, but no photo was taken. Bail out and
    // retry on the next tick instead -- same idiom as the
    // acquireTriggerLock() check above. G1/G2 physical writes can't fail
    // this way, so this only applies when BLE is the sole channel.
    if (!fireG2 && !fireG1 && bleEnabled && !blePressOk) {
        releaseTriggerLock();
        return;
    }

    state.bulbFiredG2 = fireG2;
    state.bulbFiredG1 = fireG1;

    if (fireG2) digitalWrite(TRIGGER_G2_PIN, HIGH);
    if (fireG1) digitalWrite(TRIGGER_G1_PIN, HIGH);
    if (triggerMode.config.beepEnabled && g_speakerEnabled) tone(BUZZ_PIN, 2500, 60);   // "exposure started" cue

    state.isExposing        = true;
    state.exposureStartTime = millis();
    // NOTE: lastShotTime is deliberately NOT set here. It's set in
    // endBulbExposure() instead, once the exposure actually finishes, so
    // Interval measures genuine REST time after the shot completes —
    // giving the camera real time to write/process a long exposure before
    // the next one starts — rather than time from this shot's START (which
    // left zero rest whenever Interval landed at exactly the exposure
    // length).

    // Trigger lock stays HELD for the whole exposure — released in
    // endBulbExposure(). Much longer than the ~6ms non-bulb hold, but
    // correct: this firmware only ever runs one mode at a time, so nothing
    // else can contend for it during a real exposure.
}

void Timelapse::endBulbExposure() {
    if (state.bulbFiredG2) digitalWrite(TRIGGER_G2_PIN, LOW);
    if (state.bulbFiredG1) digitalWrite(TRIGGER_G1_PIN, LOW);
    triggerMode.releaseBluetoothShutterIfEnabled();
    if (triggerMode.config.beepEnabled && g_speakerEnabled) tone(BUZZ_PIN, 1500, 60);   // "exposure done" cue

    releaseTriggerLock();

    state.isExposing = false;
    state.shotCount++;

    // Rest period (Interval) starts counting NOW, from shot completion —
    // see the note in startBulbExposure().
    state.lastShotTime = millis();
}

// Safety net: force-releases a mid-exposure hold so the camera's shutter
// is never left open indefinitely just because the user pressed
// STOP/PAUSE (or exited the mode) while a bulb shot was in progress.
void Timelapse::forceReleaseBulbIfExposing() {
    if (!state.isExposing) return;

    if (state.bulbFiredG2) digitalWrite(TRIGGER_G2_PIN, LOW);
    if (state.bulbFiredG1) digitalWrite(TRIGGER_G1_PIN, LOW);
    triggerMode.releaseBluetoothShutterIfEnabled();   // never leave the BLE shutter held either
    releaseTriggerLock();
    state.isExposing = false;
    // Deliberately NOT counted as a completed shot (no shotCount++) — it
    // was cut short, not a real capture.
}

// ============ VIDEO CALCULATOR HELPERS ============
// Real wall-clock time one shot cycle takes: the configured Interval, plus
// the Bulb exposure length when Bulb is on (exposure and rest are
// sequential -- see startBulbExposure()/endBulbExposure()). Non-bulb shots
// are a ~6ms pulse, negligible next to Interval, so they're not added here.
long Timelapse::perShotMs() const {
    long ms = config.intervalMs;
    if (config.bulbEnabled) ms += (long)config.bulbExposureSec * 1000L + (long)config.bulbSettleSec * 1000L;
    return ms;
}

long Timelapse::currentDurationSecOrBootstrap() const {
    long d = getEstimatedDurationSec();
    if (d < 0) {
        // totalShots is 0 (infinite) -- bootstrap a concrete number so the
        // calculator has something finite to start editing from.
        int shots = config.totalShots > 0 ? config.totalShots : 1;
        d = perShotMs() * shots / 1000;
    }
    if (d < 1) d = 1;
    return d;
}

float Timelapse::currentVideoLengthSecOrBootstrap() const {
    float v = getVideoLengthSec();
    if (v < 0.0f) v = 0.0f;
    return v;
}

// intervalMs = durationSec * 1000 / shots, minus the Bulb exposure length
// (if Bulb is on) since that time is spent exposing, not resting -- solves
// for the REST time needed so `shots` full cycles (expose + rest) fit
// `durationSec`. Computed in 64-bit and clamped BEFORE narrowing to int.
// durationSec can reach ~36,000,000 (10000 shots * 3600s interval) -- *1000
// is ~36 billion, which overflows a 32-bit long (ESP32's `long` is 32-bit)
// well before validateConfig() ever gets a chance to clamp it. If the
// requested duration is too tight to fit the exposures at all, this floors
// at 100ms rather than going negative -- the real duration will then end up
// longer than what was asked for, which is honest, not silently wrong.
int Timelapse::solveIntervalMs(long durationSec, int shots) const {
    if (shots < 1) shots = 1;
    long long perShotMsNeeded = ((long long)durationSec * 1000LL) / (long long)shots;
    long long bulbMs = config.bulbEnabled
                           ? (long long)(config.bulbExposureSec + config.bulbSettleSec) * 1000LL
                           : 0LL;
    long long ms = perShotMsNeeded - bulbMs;
    if (ms < 100) ms = 100;
    if (ms > 3600000LL) ms = 3600000LL;
    return (int)ms;
}

// ============ UI INTERACTION ============
void Timelapse::handleEncoderRotate(int delta) {
    if (editMode.state == TimelapseEditMode::SELECTING) {
        if (editMode.screen == TimelapseEditMode::ADVANCE) {
            // ADVANCE: 0=Interval 1=Total Shots 2=Bulb Mode 3=Exposure 4=Settle Delay
            int newIndex = editMode.advanceIndex + (delta > 0 ? 1 : -1);
            if (newIndex >= 0 && newIndex <= 4) editMode.advanceIndex = newIndex;
            return;
        }
        // MAIN: 0=Shoot Duration 1=Video Length 2=Video FPS 3=Advance 4=Control
        int newIndex = editMode.selectedIndex + (delta > 0 ? 1 : -1);
        if (newIndex >= 0 && newIndex <= 4) editMode.selectedIndex = newIndex;
    }
    else if (editMode.state == TimelapseEditMode::EDITING) {
        int rawDelta = delta;   // keep the real magnitude for speed-sensitive fields
        if (delta > 0) delta = 1;
        else if (delta < 0) delta = -1;

        if (editMode.screen == TimelapseEditMode::ADVANCE) {
            // Direct entry -- identical to the original Interval/Total
            // Shots editing logic, plus the new Bulb Exposure field.
            switch (editMode.advanceIndex) {
                case 0: {  // Interval -- speed-sensitive: a fast spin moves
                           // further, capped so a glitch/overshoot can't
                           // jump too far. Interval spans 100ms-1hr and a
                           // flat +-1 step (100ms/click) made large values
                           // impractically slow to dial in. Timelapse's
                           // Interval is a pure software timer with no
                           // sensor/hardware timing coupling, so unlike
                           // Auto Shoot's range fields this carries none of
                           // that stability risk.
                    int steps = rawDelta;
                    if (steps > 30) steps = 30;
                    if (steps < -30) steps = -30;
                    config.intervalMs += (steps * 100);
                    break;
                }
                case 1:  // Total Shots
                    config.totalShots += delta;
                    break;
                case 3:  // Bulb Exposure (seconds)
                    config.bulbExposureSec += delta;
                    break;
                case 4:  // Settle Delay (seconds)
                    config.bulbSettleSec += delta;
                    break;
                // case 2 (Bulb Mode) is an instant toggle on press, not
                // encoder-adjustable -- see handleButtonPress().
            }
            validateConfig();
            return;
        }

        // MAIN screen video calculator. Rule: whichever of these three you
        // are turning right now is the one field held fixed at its CURRENT
        // value; the other two are recomputed from it. This keeps the
        // relationship well-defined no matter which field you approach it
        // from (see the design discussion this was built from).
        switch (editMode.selectedIndex) {
            case 0: {   // Shoot Duration -- keep Total Shots fixed, solve Interval
                int shots = config.totalShots > 0 ? config.totalShots : 1;
                long durationSec = currentDurationSecOrBootstrap();
                durationSec += (long)delta * 60;   // 1 minute per click
                if (durationSec < 1) durationSec = 1;

                config.totalShots = shots;
                config.intervalMs = solveIntervalMs(durationSec, shots);
                break;
            }
            case 1: {   // Video Length -- keep Shoot Duration fixed, solve
                        // Total Shots then Interval. EXCEPT when starting
                        // from Total Shots == 0 (infinite): there is no
                        // real Duration to hold fixed yet, so bootstrapping
                        // one from a degenerate "1 shot" guess and then
                        // solving Interval for the (much larger) new shot
                        // count produced absurdly short intervals (e.g. a
                        // 5s default Interval treated as "duration for 1
                        // shot", stretched to fit 30 shots -> ~166ms
                        // Interval). Keep the existing Interval instead and
                        // just extend the shot count; Duration then follows
                        // naturally (Interval x shots).
                bool bootstrapping = (config.totalShots == 0);
                long durationSec = currentDurationSecOrBootstrap();
                int fps = getVideoFps();

                float videoLenSec = currentVideoLengthSecOrBootstrap();
                videoLenSec += (float)delta;   // 1 second per click
                if (videoLenSec < 1.0f) videoLenSec = 1.0f;

                int newTotalShots = (int)(videoLenSec * fps + 0.5f);
                if (newTotalShots < 1) newTotalShots = 1;
                if (newTotalShots > 10000) newTotalShots = 10000;

                config.totalShots = newTotalShots;
                if (!bootstrapping) {
                    config.intervalMs = solveIntervalMs(durationSec, newTotalShots);
                }
                break;
            }
            case 2: {   // Video FPS -- keep Video Length fixed, solve
                        // Total Shots then Interval (same infinite-start
                        // bootstrap guard as Video Length above)
                bool bootstrapping = (config.totalShots == 0);
                long durationSec = currentDurationSecOrBootstrap();
                float videoLenSec = currentVideoLengthSecOrBootstrap();

                int newIdx = (int)config.videoFpsIndex + (delta > 0 ? 1 : -1);
                if (newIdx < 0) newIdx = 2;
                if (newIdx > 2) newIdx = 0;
                config.videoFpsIndex = (uint8_t)newIdx;

                int newFps = VIDEO_FPS_PRESETS[config.videoFpsIndex];
                int newTotalShots = (int)(videoLenSec * newFps + 0.5f);
                if (newTotalShots < 1) newTotalShots = 1;
                if (newTotalShots > 10000) newTotalShots = 10000;

                config.totalShots = newTotalShots;
                if (!bootstrapping) {
                    config.intervalMs = solveIntervalMs(durationSec, newTotalShots);
                }
                break;
            }
        }

        validateConfig();
    }
}

void Timelapse::handleButtonPress() {
    if (editMode.state == TimelapseEditMode::SELECTING) {
        if (editMode.screen == TimelapseEditMode::ADVANCE) {
            switch (editMode.advanceIndex) {
                case 0:   // Interval
                case 1:   // Total Shots
                case 3:   // Bulb Exposure
                case 4:   // Settle Delay
                    editMode.state = TimelapseEditMode::EDITING;
                    editMode.enterTime = millis();
                    break;
                case 2:   // Bulb Mode -- instant toggle
                    config.bulbEnabled = !config.bulbEnabled;
                    validateConfig();
                    saveConfig();
                    break;
            }
            return;
        }

        // MAIN screen
        if (editMode.selectedIndex == 3) {
            // Open Advance submenu
            editMode.screen = TimelapseEditMode::ADVANCE;
            editMode.advanceIndex = 0;
        } else if (editMode.selectedIndex == 4) {
            // START / PAUSE / RESUME toggle
            toggleRunPause();
        } else {
            // 0=Shoot Duration 1=Video Length 2=Video FPS -> enter EDITING
            editMode.state = TimelapseEditMode::EDITING;
            editMode.enterTime = millis();
        }
    }
    else if (editMode.state == TimelapseEditMode::EDITING) {
        // Save and exit edit mode (Timelapse's own convention: save
        // immediately per field, unlike some other modes that defer to
        // mode-exit)
        saveConfig();
        editMode.state = TimelapseEditMode::SELECTING;
    }
}

void Timelapse::handleButtonLongPress() {
    if (editMode.screen == TimelapseEditMode::ADVANCE) {
        // Back to MAIN screen only, same convention as Auto Shoot's
        // Advance and Trigger Mode's Bluetooth sub-screen.
        editMode.screen = TimelapseEditMode::MAIN;
        editMode.selectedIndex = 3;   // back on "Advance" row
        return;
    }

    // MAIN screen: long press = stop and exit back to menu
    stop();
    saveConfig();
    editMode.state = TimelapseEditMode::IDLE;
    editMode.selectedIndex = 0;
}

// ============ CONTROL ============
void Timelapse::start() {
    config.enable      = true;
    state.isRunning    = true;
    state.isPaused     = false;
    state.isExposing   = false;
    state.lastShotTime = millis();
    state.shotCount    = 0;
}

void Timelapse::stop() {
    forceReleaseBulbIfExposing();   // never leave the shutter open on stop
    config.enable   = false;
    state.isRunning = false;
    state.isPaused  = false;
}

void Timelapse::pause() {
    forceReleaseBulbIfExposing();   // never leave the shutter open on pause
    // Pause but preserve progress (shotCount, so shooting can resume)
    config.enable   = false;
    state.isRunning = false;
    state.isPaused  = true;
}

void Timelapse::resume() {
    config.enable      = true;
    state.isRunning    = true;
    state.isPaused     = false;
    state.lastShotTime = millis();   // wait a full interval before next shot
}

void Timelapse::toggleRunPause() {
    if (state.isRunning) {
        pause();                     // running -> paused
    } else if (state.isPaused) {
        resume();                    // paused  -> running
    } else {
        start();                     // idle/done -> fresh start
    }
}

// ============ GETTERS ============
const char* Timelapse::getSelectedItemName() {
    if (editMode.screen == TimelapseEditMode::ADVANCE) {
        switch (editMode.advanceIndex) {
            case 0: return "Interval";
            case 1: return "Total Shots";
            case 2: return "Bulb Mode";
            case 3: return "Exposure";
            case 4: return "Settle Delay";
            default: return "Unknown";
        }
    }
    switch (editMode.selectedIndex) {
        case 0: return "Shoot Duration";
        case 1: return "Video Length";
        case 2: return "Video FPS";
        case 3: return "Advance";
        default: return "Unknown";
    }
}

int Timelapse::getSelectedValue() {
    if (editMode.screen == TimelapseEditMode::ADVANCE) {
        switch (editMode.advanceIndex) {
            case 0: return config.intervalMs;
            case 1: return config.totalShots;
            case 3: return config.bulbExposureSec;
            case 4: return config.bulbSettleSec;
            default: return 0;
        }
    }
    switch (editMode.selectedIndex) {
        case 0: return (int)currentDurationSecOrBootstrap();
        case 1: return (int)currentVideoLengthSecOrBootstrap();
        case 2: return getVideoFps();
        default: return 0;
    }
}

const char* Timelapse::getStatusString() {
    if (state.isExposing) return "BULB";
    // When Bulb is on, the gap between exposures is a rest period, not a
    // normal shooting cycle -- labeling it "RUNNING" (the accurate label
    // for the real non-bulb case) reads as if plain quick-pulse shooting
    // and Bulb were both active/alternating, when only Bulb ever fires.
    if (state.isRunning)  return config.bulbEnabled ? "REST" : "RUNNING";
    if (state.isPaused)   return "PAUSED";
    if (config.totalShots != 0 && state.shotCount >= config.totalShots && state.shotCount > 0)
        return "DONE";
    return "IDLE";
}

const char* Timelapse::getControlLabel() const {
    if (state.isRunning) return "PAUSE";
    if (state.isPaused)  return "RESUME";
    return "START";
}

// Total time to finish the whole sequence (seconds). -1 = infinite (totalShots=0).
long Timelapse::getEstimatedDurationSec() const {
    if (config.totalShots <= 0) return -1;
    // total = shots * (interval + bulb exposure, if any) -- see perShotMs()
    long sec = (long)((long long)config.totalShots * perShotMs() / 1000);
    return sec;
}

int Timelapse::getShotCount() const {
    return state.shotCount;
}

int Timelapse::getRemainingShots() const {
    if (config.totalShots == 0) return -1;  // Infinite
    return config.totalShots - state.shotCount;
}

unsigned long Timelapse::getTimeUntilNextShot() const {
    if (!config.enable || state.isExposing) return 0;
    unsigned long waitMs = (unsigned long)config.intervalMs;
    if (config.bulbEnabled) waitMs += (unsigned long)config.bulbSettleSec * 1000UL;
    unsigned long elapsed = millis() - state.lastShotTime;
    if (elapsed >= waitMs) return 0;
    return waitMs - elapsed;
}

unsigned long Timelapse::getBulbTimeRemaining() const {
    if (!state.isExposing) return 0;
    unsigned long elapsed = millis() - state.exposureStartTime;
    unsigned long totalMs = (unsigned long)config.bulbExposureSec * 1000UL;
    if (elapsed >= totalMs) return 0;
    return totalMs - elapsed;
}

float Timelapse::getVideoLengthSec() const {
    int fps = getVideoFps();
    if (config.totalShots <= 0 || fps <= 0) return -1.0f;
    return (float)config.totalShots / (float)fps;
}

int Timelapse::getVideoFps() const {
    uint8_t i = config.videoFpsIndex;
    if (i > 2) i = 2;
    return VIDEO_FPS_PRESETS[i];
}

bool Timelapse::isRunning() const {
    return state.isRunning;
}

bool Timelapse::isPaused() const {
    return state.isPaused;
}

bool Timelapse::isEnabled() const {
    return config.enable;
}
