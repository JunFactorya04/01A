/**
 * @file hardware_config.cpp
 * @brief Common hardware configuration implementation
 * @date 2026-07-02
 */

#include "hardware_config.h"

// ============ GLOBAL HARDWARE INSTANCES ============
HardwareSerial TFSerial(1);

// ============ GLOBAL SPEAKER CONTROL ============
bool g_speakerEnabled = true;

// ============ GLOBAL TRIGGER LOCK ============
bool g_triggerLock = false;

bool acquireTriggerLock() {
    if (g_triggerLock) return false;
    g_triggerLock = true;
    return true;
}

void releaseTriggerLock() {
    g_triggerLock = false;
}
