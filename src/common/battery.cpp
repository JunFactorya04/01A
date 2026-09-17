#include "battery.h"
#include "hardware_config.h"

// Working range. Taken from the reference M5Launcher implementation for the
// identical GPIO10 + x2-divider arrangement. 4150 rather than a cell's true
// 4200mV full charge, so the gauge reaches 100% slightly early instead of
// sitting stuck just below it.
static const uint32_t BATTERY_EMPTY_MV = 3300;
static const uint32_t BATTERY_FULL_MV  = 4150;

// ---- filtering ----
// Measured on real hardware: raw readings on this board swing about +/-200mV
// on a battery that is not actually changing (4016..4228 observed within
// seconds, both on USB and on an 18650). On the scale above that is ~23
// percentage points of pure jitter, so filtering is not a nicety here.
//
// Two stages, because the noise has two characters:
//   MEDIAN of a short burst   -> rejects individual spikes, e.g. the sag from
//                                firing a trigger pulse or the BLE radio
//                                transmitting.
//   EMA across sampling ticks -> the burst alone is useless against the slow
//                                wander actually seen above: all samples in a
//                                2ms window share the same error. Averaging
//                                across seconds is what removes it.
// Battery voltage genuinely changes over minutes, so heavy smoothing costs
// nothing real.
static const uint8_t  SAMPLE_COUNT       = 9;    // odd, so the median is a real sample
static const uint16_t SAMPLE_SPACING_US  = 1000; // spread the burst over ~9ms
static const uint32_t SAMPLE_INTERVAL_MS = 500;  // how often the burst re-runs
static const float    EMA_ALPHA          = 0.10f; // ~5s settling at the above rate

// Raw burst, median-filtered. Blocking for ~9ms, which is why the caller-facing
// function below rate-limits how often this runs.
static uint32_t sampleBurstMilliVolts() {
    uint32_t s[SAMPLE_COUNT];
    for (uint8_t i = 0; i < SAMPLE_COUNT; i++) {
        s[i] = (uint32_t)analogReadMilliVolts(BATTERY_ADC_PIN) * 2;
        delayMicroseconds(SAMPLE_SPACING_US);
    }
    for (uint8_t i = 1; i < SAMPLE_COUNT; i++) {          // insertion sort
        uint32_t v = s[i];
        int8_t j = i - 1;
        while (j >= 0 && s[j] > v) { s[j + 1] = s[j]; j--; }
        s[j + 1] = v;
    }
    return s[SAMPLE_COUNT / 2];
}

// ---- sustained-condition tracking ----
// A single low reading must NEVER trigger anything. This board's load is
// spiky and heavy by design -- the BLE radio transmitting, TF-Luna polling at
// 200Hz, and the trigger GPIOs firing all pull the rail down briefly, and a
// pack with plenty of charge left can dip under a threshold for a moment.
// Acting on that would shut the device down mid-shoot on a half-full battery,
// which is worse than the problem being solved.
//
// So both states require the condition to HOLD CONTINUOUSLY: any reading back
// above the threshold resets the timer from scratch. A transient sag lasts
// milliseconds; genuine depletion does not recover. The critical hold is long
// (a full minute) precisely because its consequence is a shutdown -- an
// 18650 under this device's draw sags on the order of tens of millivolts, not
// the ~500mV it would take to sit at the critical threshold for a minute
// while actually holding charge.
static const uint32_t LOW_HOLD_MS      = 10000;   // 10s  -> warn
static const uint32_t CRITICAL_HOLD_MS = 60000;   // 60s  -> shut down

// Recovery needs this much headroom above the threshold before the warning
// clears, so a pack hovering right at the line does not flicker the badge.
static const uint32_t HYSTERESIS_MV = 100;

static bool     s_lowActive  = false, s_critActive = false;
static uint32_t s_lowSince   = 0,     s_critSince  = 0;

static void updateSustainedState(uint32_t mv, uint32_t now) {
    if (mv <= BATTERY_LOW_MV) {
        if (!s_lowActive) { s_lowActive = true; s_lowSince = now; }
    } else if (mv > BATTERY_LOW_MV + HYSTERESIS_MV) {
        s_lowActive = false;
    }

    if (mv <= BATTERY_CRITICAL_MV) {
        if (!s_critActive) { s_critActive = true; s_critSince = now; }
    } else if (mv > BATTERY_CRITICAL_MV + HYSTERESIS_MV) {
        s_critActive = false;
    }
}

uint32_t batteryMilliVolts() {
    static bool  adcReady = false;
    static bool  seeded   = false;
    static float ema      = 0.0f;
    static uint32_t lastSample = 0;

    if (!adcReady) {
        pinMode(BATTERY_ADC_PIN, INPUT);
        adcReady = true;
    }

    // Cheap to call every render frame: the actual (blocking) burst only runs
    // on its own schedule, everything else returns the smoothed value.
    uint32_t now = millis();
    if (seeded && (now - lastSample) < SAMPLE_INTERVAL_MS) {
        return (uint32_t)(ema + 0.5f);
    }
    lastSample = now;

    uint32_t raw = sampleBurstMilliVolts();
    if (!seeded) {
        ema = (float)raw;   // seed directly, so the first reading is not a slow climb from 0
        seeded = true;
    } else {
        ema += ((float)raw - ema) * EMA_ALPHA;
    }

    uint32_t mv = (uint32_t)(ema + 0.5f);
    updateSustainedState(mv, now);
    return mv;
}

uint8_t batteryPercent() {
    uint32_t mv = batteryMilliVolts();

    // Linear over the working range. This is an approximation: a Li-ion
    // discharge curve is genuinely flat through its middle, so the reported
    // percentage moves too fast near both ends and too slowly in between. It
    // is kept linear deliberately -- a piecewise curve would need real
    // discharge measurements on this exact pack to be any more honest than
    // this is, and inventing one from a datasheet would just look precise
    // without being accurate.
    //
    // (The reference implementation this came from divides by
    // FULL - (EMPTY + 50), which makes a full pack compute to 106% before
    // being clamped -- so a genuinely full battery and one 50mV down both
    // read 100%. Corrected here.)
    if (mv <= BATTERY_EMPTY_MV) return 0;
    if (mv >= BATTERY_FULL_MV) return 100;
    return (uint8_t)(((mv - BATTERY_EMPTY_MV) * 100) / (BATTERY_FULL_MV - BATTERY_EMPTY_MV));
}

bool batteryLow() {
    batteryMilliVolts();   // keeps the sampler and the hold timers advancing
    return s_lowActive && (millis() - s_lowSince >= LOW_HOLD_MS);
}

bool batteryCritical() {
    batteryMilliVolts();
    return s_critActive && (millis() - s_critSince >= CRITICAL_HOLD_MS);
}

bool batteryCharging() {
#ifdef BATTERY_CHARGE_STAT_PIN
    static bool statReady = false;
    if (!statReady) {
        // The STAT output on these charger ICs is usually open-drain, so it
        // needs a pull-up to read as "not charging" when released.
        pinMode(BATTERY_CHARGE_STAT_PIN, INPUT_PULLUP);
        statReady = true;
    }
    int level = digitalRead(BATTERY_CHARGE_STAT_PIN);
#ifdef BATTERY_CHARGE_STAT_ACTIVE_LOW
    return level == LOW;
#else
    return level == HIGH;
#endif
#else
    // No status line wired -- see battery.h for why this is not inferred from
    // voltage instead.
    return false;
#endif
}
