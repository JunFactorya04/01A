#include "flash_trigger.h"
#include "../common/hardware_config.h"
#include "../trigger_mode/trigger_mode.h"

GpioFlashTrigger flashTrigger;

// Pulse width. Matches the camera trigger pulse in Timelapse/AutoShoot -- a
// flash sync input latches on the contact closing, so the pulse only has to be
// long enough to be seen, not held.
static const int FLASH_PULSE_MS = 6;

bool GpioFlashTrigger::fire() {
    // Same global mutex every other GPIO-firing path in this codebase takes.
    // On a FLASH box nothing else should be contending for it, but taking it
    // keeps the invariant intact rather than relying on that.
    if (!acquireTriggerLock()) return false;

    bool fireG2 = triggerMode.config.triggerEnabled;
    bool fireG1 = triggerMode.config.remoteEnabled;
    // A FLASH node with neither output enabled would silently do nothing, so
    // fall back to G2 the same way the camera paths do.
    if (!fireG2 && !fireG1) fireG2 = true;

    if (fireG2) digitalWrite(TRIGGER_G2_PIN, HIGH);
    if (fireG1) digitalWrite(TRIGGER_G1_PIN, HIGH);
    delay(FLASH_PULSE_MS);
    if (fireG2) digitalWrite(TRIGGER_G2_PIN, LOW);
    if (fireG1) digitalWrite(TRIGGER_G1_PIN, LOW);

    releaseTriggerLock();
    return true;
}
