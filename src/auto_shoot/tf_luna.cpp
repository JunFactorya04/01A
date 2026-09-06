/**
 * @file tf_luna.cpp
 * @brief TF-Luna LiDAR I2C sensor API implementation
 * @date 2026-07-07
 */

#include "tf_luna.h"
#include "../common/hardware_config.h"
#include <Wire.h>

// Global instance
TFLuna tfLuna;

// ============ I2C BUS RECOVERY ============
// If TF-Luna was mid-transmission when the ESP32 last reset/lost power, it
// can be left holding SDA low waiting for clock pulses that will never
// come from a freshly-booted master expecting a clean, idle bus. NO I2C
// transaction (read OR write) can succeed while this holds -- this is why
// self-healing via begin()+softResetSensor() alone could never actually
// recover a truly stuck bus (softResetSensor() is itself an I2C write, so
// it fails identically). Whether this happens on a given boot depends on
// the exact timing of the previous power-down relative to the sensor's
// I2C cycle, which is why the symptom was inconsistent run to run.
//
// Standard, well-documented fix (see e.g. ESPHome's own I2C bus recovery,
// and the NXP I2C-bus specification's bus-clear procedure): before ever
// handing the pins to the Wire peripheral, manually toggle SCL as a GPIO
// up to ~20 times to walk a stuck slave through finishing its pending
// bit(s), then issue a manual STOP condition. Runs in well under a
// millisecond when the bus is already idle (the check exits immediately);
// only takes longer in the rare case recovery is actually needed. If SCL
// itself were ever found stuck (not just SDA), no software fix is
// possible -- that's a hardware/firmware fault on the other side needing
// an actual power cycle; this routine does not attempt to handle that
// case specially, it just falls through to a normal (still-failing)
// Wire1.begin() same as before this existed.
static void recoverStuckI2CBus() {
    pinMode(TFLUNA_SCL_PIN, INPUT_PULLUP);
    pinMode(TFLUNA_SDA_PIN, INPUT_PULLUP);
    delayMicroseconds(10);   // let the pull-ups settle before sampling

    if (digitalRead(TFLUNA_SDA_PIN) == HIGH) {
        return;   // bus already idle -- nothing to recover
    }

    // SDA held low: clock SCL manually (drive low / release to the pull-up
    // high, open-drain style) so a stuck slave gets a chance to finish
    // clocking out its pending bit and release SDA.
    pinMode(TFLUNA_SCL_PIN, OUTPUT);
    for (int i = 0; i < 20 && digitalRead(TFLUNA_SDA_PIN) == LOW; i++) {
        digitalWrite(TFLUNA_SCL_PIN, LOW);
        delayMicroseconds(5);
        digitalWrite(TFLUNA_SCL_PIN, HIGH);
        delayMicroseconds(5);
    }

    // Manual STOP condition (SDA transitions LOW -> HIGH while SCL is
    // HIGH) so the bus is left in a clean, idle state for Wire1.begin().
    pinMode(TFLUNA_SDA_PIN, OUTPUT);
    digitalWrite(TFLUNA_SDA_PIN, LOW);
    delayMicroseconds(5);
    digitalWrite(TFLUNA_SCL_PIN, HIGH);
    delayMicroseconds(5);
    digitalWrite(TFLUNA_SDA_PIN, HIGH);
    delayMicroseconds(5);

    // Release both pins back to plain input; Wire1.begin() reconfigures
    // them for I2C right after this returns.
    pinMode(TFLUNA_SDA_PIN, INPUT);
    pinMode(TFLUNA_SCL_PIN, INPUT);
}

// ============ INITIALIZATION ============
void TFLuna::begin() {
    // Release the driver on re-entry, then plain re-init.
    // This is exactly the config that tested best in the field.
    if (_started) {
        Wire1.end();
        delay(2);
    }

    // Clear a possibly stuck bus BEFORE the Wire peripheral takes the pins
    // -- see recoverStuckI2CBus() above for why this matters.
    recoverStuckI2CBus();

    Wire1.begin(TFLUNA_SDA_PIN, TFLUNA_SCL_PIN);
    Wire1.setTimeOut(20);   // bound any blocking I2C op to 20ms

    _started        = true;
    _distance_m     = 0.0f;
    _strength       = 0;
    _valid          = false;
    _failCount      = 0;
    _lastUpdateTime = millis();

    // Bounded, paced wait for the sensor to actually be ready to answer.
    // TF-Luna needs real time after power-up to settle (laser + internal
    // MCU boot); querying it before that is indistinguishable from a real
    // failure and was landing as the "sometimes falls back to 100Hz"
    // symptom below, since configureFrameRate()'s write+readback would
    // just fail on a sensor that hadn't woken up yet. Paced to the
    // sensor's own ~10ms native frame rate (NOT a tight busy-loop -- that
    // pattern, from the old warmUp(), was a real cause of instability
    // before). Hard-capped at 10 attempts (~100ms typical, worst case
    // under ~300ms with I2C timeouts) so this can never hang begin()
    // itself -- if no good read comes back in time, update()'s existing
    // pacing/backoff/self-heal simply takes over from a cold start,
    // exactly as it always has.
    for (int i = 0; i < 10; i++) {
        uint16_t d, s;
        if (readRegisters(d, s)) break;
        delay(10);
    }

    // Raise the poll rate above the sensor's 100Hz default. Read-verified;
    // falls back to the known-good 10ms/100Hz pacing if anything about the
    // write/readback doesn't check out, so a flaky first attempt can't
    // leave the driver polling faster than the sensor actually supports.
    // Much more likely to succeed now that the wait above has given the
    // sensor a real chance to be answering before this runs.
    configureFrameRate();
}

// ============ RAW I2C READ ============
// Reads 6 registers starting at DIST_LO:
//   dist (cm, little-endian), flux, temp — temp is read but unused.
bool TFLuna::readRegisters(uint16_t &dist_cm, uint16_t &strength) {
    Wire1.beginTransmission(TFLUNA_I2C_ADDR);
    Wire1.write((uint8_t)TFL_REG_DIST_LO);
    if (Wire1.endTransmission() != 0) {
        return false;                       // no ACK from sensor
    }

    uint8_t buf[6];
    Wire1.requestFrom((int)TFLUNA_I2C_ADDR, 6);
    int i = 0;
    while (Wire1.available() && i < 6) {
        buf[i++] = Wire1.read();
    }
    if (i != 6) return false;               // short read

    dist_cm  = buf[0] | (buf[1] << 8);      // distance (cm)
    strength = buf[2] | (buf[3] << 8);      // signal amplitude (flux)
    return true;
}

// ============ FRAME RATE CONFIG ============
// Registers 0x26 (FPS_LO) / 0x27 (FPS_HI): plain 16-bit little-endian Hz
// value (NOT a divisor formula), confirmed against Benewake's own I2C
// register map. Distinct from the 0x25 enable/disable register that has
// caused multi-second stalls in the past — writing FPS takes effect
// immediately, no soft-reset (0x21) required. Deliberately NOT saved to
// flash (no write to 0x20 Save_Settings): we don't need it to survive a
// power-cycle since begin() re-applies it every time anyway, and skipping
// flash writes avoids adding any new failure mode on top of an already
// working sensor.
static bool writeFrameRateReg(uint16_t hz) {
    Wire1.beginTransmission(TFLUNA_I2C_ADDR);
    Wire1.write((uint8_t)TFL_REG_FPS_LO);
    Wire1.write((uint8_t)(hz & 0xFF));
    Wire1.write((uint8_t)((hz >> 8) & 0xFF));
    return Wire1.endTransmission() == 0;
}

static bool readFrameRateReg(uint16_t &hzOut) {
    Wire1.beginTransmission(TFLUNA_I2C_ADDR);
    Wire1.write((uint8_t)TFL_REG_FPS_LO);
    if (Wire1.endTransmission() != 0) return false;

    Wire1.requestFrom((int)TFLUNA_I2C_ADDR, 2);
    if (Wire1.available() < 2) return false;
    uint8_t lo = Wire1.read();
    uint8_t hi = Wire1.read();
    hzOut = lo | (hi << 8);
    return true;
}

void TFLuna::configureFrameRate() {
    writeFrameRateReg(TFLUNA_TARGET_FPS_HZ);

    uint16_t confirmed = 0;
    if (readFrameRateReg(confirmed) && confirmed >= 10 && confirmed <= 250) {
        _confirmedFpsHz  = confirmed;
        _samplePeriodMs  = (uint8_t)(1000 / confirmed);
        if (_samplePeriodMs < 1) _samplePeriodMs = 1;
    } else {
        // Write or read-back didn't check out — stay on the known-good
        // 100Hz-safe pacing rather than trust an unconfirmed value.
        _confirmedFpsHz = 0;
        _samplePeriodMs = 10;
    }
}

// ============ SLEEP / WAKE (power saving) ============
// NOTE: kept in the API but NOT called on mode entry/exit. Field testing
// showed TF-Luna register writes (0x25) can stall the sensor for many
// seconds — the stable config never writes registers during normal use.
static bool writeEnableReg(uint8_t value) {
    Wire1.beginTransmission(TFLUNA_I2C_ADDR);
    Wire1.write((uint8_t)TFL_REG_ENABLE);
    Wire1.write(value);
    return Wire1.endTransmission() == 0;
}

void TFLuna::sleep() {
    if (!_started) return;
    writeEnableReg(0);      // stop measuring -> laser off, low power
    _valid = false;
}

void TFLuna::wake() {
    if (!_started) return;
    writeEnableReg(1);      // resume measuring
    _failCount = 0;
    _valid = false;         // next update() refreshes real data
}

// ============ BOOT WARM-UP ============
// Run once at power-on: init the bus early and exercise the sensor so its
// laser/I2C engine is fully settled BEFORE the user enters Auto Shoot.
// Exits early after a burst of good reads; hard-bounded by maxMs.
void TFLuna::warmUp(unsigned long maxMs) {
    begin();

    unsigned long start = millis();
    uint8_t goodReads = 0;

    while ((millis() - start) < maxMs && goodReads < 10) {
        uint16_t d, s;
        if (readRegisters(d, s)) goodReads++;
        delay(10);   // sensor frame pace (100Hz)
    }
}

// ============ SENSOR SOFT RESET ============
// Commands the TF-Luna itself to reboot (register 0x21 = 0x02).
// Used only when the sensor's I2C engine is hung and stops answering.
void TFLuna::softResetSensor() {
    Wire1.beginTransmission(TFLUNA_I2C_ADDR);
    Wire1.write((uint8_t)TFL_REG_SOFT_RESET);
    Wire1.write((uint8_t)0x02);
    Wire1.endTransmission();
    // Sensor reboots in ~500ms; reads fail meanwhile and the normal
    // 50ms backoff absorbs that without blocking the UI.
}

// ============ MAIN UPDATE ============
bool TFLuna::update() {
    // Never touch the bus before begin(): polling an un-initialized bus
    // racks up fails and triggers self-healing register writes (0x21)
    // that stall the sensor for seconds.
    if (!_started) return false;

    unsigned long now = millis();

    // Pace polling to the sensor's actual frame rate (_samplePeriodMs is set
    // in configureFrameRate() from the confirmed FPS, 10ms/100Hz if that
    // wasn't confirmed). Hammering the I2C slave faster than it updates its
    // registers is a known cause of TF-Luna bus lock-ups under sustained load.
    if (_failCount == 0 && (now - _lastAttemptTime) < _samplePeriodMs) {
        return _valid;
    }

    // Backoff while failing: retry every 50ms so blocked I2C ops
    // (up to 2x20ms each) can't stall the UI/encoder.
    if (_failCount > 0 && (now - _lastAttemptTime) < 50) {
        return false;
    }
    _lastAttemptTime = now;

    uint16_t dist_cm = 0;
    uint16_t strength = 0;

    if (!readRegisters(dist_cm, strength)) {
        _valid = false;                     // I2C failed this cycle

        // Self-healing for a truly hung sensor ("overload"): restart the
        // I2C driver AND command the TF-Luna to reboot itself (reg 0x21).
        // At most once per 3s; sensor takes ~500ms to come back.
        // NOTE: begin() re-applies our target frame rate BEFORE this reboot
        // command is sent, but the reboot itself resets the sensor's own
        // registers to power-on defaults (100Hz) since we never save FPS to
        // flash. So after a real self-heal event the sensor quietly settles
        // back to 100Hz until the mode is re-entered (next begin() call)
        // re-configures it — harmless (we'd just poll a bit faster than the
        // sensor produces new data), not worth complicating this already
        // fragile recovery path to chase.
        if (++_failCount >= TFLUNA_MAX_FAILS &&
            (millis() - _lastRecoverTime) > 3000) {
            _lastRecoverTime = millis();
            begin();
            softResetSensor();
        }
        return false;
    }

    _failCount = 0;

    // Convert cm -> meters, clamp to sensor max range.
    // 0 = no return (no object). No aggressive strength filtering.
    float d = dist_cm / 100.0f;
    if (d > TFLUNA_MAX_DISTANCE_M) d = TFLUNA_MAX_DISTANCE_M;

    _distance_m     = d;
    _strength       = strength;
    _valid          = true;
    _lastUpdateTime = millis();
    return true;
}
