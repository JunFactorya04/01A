#include "flash_trigger.h"

StubFlashTrigger flashTrigger;

bool StubFlashTrigger::fire() {
    Serial.println("[FlashTrigger] fire() -- no RF/GPIO hardware wired yet (stub)");
    return true;
}
