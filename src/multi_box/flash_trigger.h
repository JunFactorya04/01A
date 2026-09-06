/**
 * @file flash_trigger.h
 * @brief Abstraction for a FLASH-role node's physical illumination output
 * @date 2026-09-06
 *
 * MultiBoxController/MultiBox only ever call fire()/isReady() on this
 * interface — never anything RF- or GPIO-specific — so a real Yongnuo
 * 2.4GHz RF flash trigger implementation can be dropped in later without
 * touching the Multi Box state machine. StubFlashTrigger is the only
 * implementation for now: it logs, no hardware is wired.
 */

#pragma once
#include <Arduino.h>

class FlashTrigger {
public:
    virtual ~FlashTrigger() = default;
    virtual bool isReady() const = 0;
    virtual bool fire() = 0;   // one-shot; returns true if the command was issued
};

class StubFlashTrigger : public FlashTrigger {
public:
    bool isReady() const override { return true; }
    bool fire() override;
};

extern StubFlashTrigger flashTrigger;
