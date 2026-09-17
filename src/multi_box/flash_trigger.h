/**
 * @file flash_trigger.h
 * @brief Abstraction for a FLASH-role node's physical illumination output
 * @date 2026-09-06
 *
 * MultiBoxController/MultiBox only ever call fire()/isReady() on this
 * interface -- never anything GPIO- or RF-specific -- so a different output
 * (e.g. a dedicated RF flash transmitter on its own pin) can be dropped in
 * later without touching the Multi Box state machine.
 */

#pragma once
#include <Arduino.h>

class FlashTrigger {
public:
    virtual ~FlashTrigger() = default;
    virtual bool isReady() const = 0;
    virtual bool fire() = 0;   // one-shot; returns true if the command was issued
};

// Fires the flash the same way a camera shot is fired: a brief pulse on the
// trigger outputs, honouring TriggerMode's per-channel enables. A studio/
// speedlight sync input is the same kind of dry contact a camera's remote
// port is, so this needs no separate hardware path -- on a FLASH-role box
// those outputs are wired to the flash instead of to a camera.
class GpioFlashTrigger : public FlashTrigger {
public:
    bool isReady() const override { return true; }
    bool fire() override;
};

extern GpioFlashTrigger flashTrigger;
