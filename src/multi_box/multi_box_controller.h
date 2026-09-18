/**
 * @file multi_box_controller.h
 * @brief MULTI BOX ESP-NOW transport + shooting-session state machine
 * @date 2026-09-06
 *
 * Owns esp_now_init()/callbacks and the MAIN-side coordinator logic
 * (sessionId ownership, bulb open/close timing) plus the non-MAIN
 * node-side reaction to MAIN's broadcasts. The ESP-NOW receive callback
 * (which runs off the WiFi driver's own task) only ever validates size and
 * pushes to a queue — everything else (camera GPIO, UI, NVS) happens in
 * update(), called from the main loop.
 */

#pragma once
#include <Arduino.h>
#include "multi_box_protocol.h"

class MultiBoxController {
public:
    bool begin();     // WiFi STA + esp_now_init + callbacks; false on failure
    void end();       // esp_now_deinit + WiFi off
    void update();    // drain event queue, advance MAIN bulb timing

    // Node-side actions (START/FLASH), called from MultiBox's sensor logic.
    void sendStartOrEnd(bool isEnd);
    void sendFlashFire();

    // Shared actions.
    void broadcastDisarm();
    // MAIN's own TF-Luna saw the finish line crossed. Does NOT close the
    // shutter directly -- minBulbSec is still enforced by update(), so a
    // runner crossing early (or sensor noise just after the shutter opened)
    // cannot cut the frame short.
    void requestEnd();

    // Open a session as if a START node had asked for one. Exists for the
    // single-box development mode: without a second box there is no way to
    // exercise MAIN's half of the cycle -- bulb open, MAIN's own TF-Luna
    // closing it, minBulbSec/maxBulbSec, rearm -- which is precisely the half
    // most likely to be wrong. Deliberately calls the SAME code the packet
    // path does rather than reimplementing it; a simulator that duplicated the
    // logic would prove nothing about the real one.
    void simulateStart();

    void requestExit();   // safe-disarm sequence for the UI's exit-confirm "OK"

    // public: a role change mid-exposure has to be able to close the shutter
    void forceCloseBulbIfExposing();

private:
    void handleIncoming(const MBPacket& pkt, const uint8_t mac[6], unsigned long receivedAt);
    void centerOnStart(const MBPacket& pkt, const uint8_t mac[6]);
    void centerOnEndDetect(const MBPacket& pkt, const uint8_t mac[6]);
    void centerCloseBulbIfDue();
    void openBulb();
    void closeBulb();
    void nodeOnCenterBroadcast(const MBPacket& pkt, const uint8_t mac[6]);

    void sendTo(MBCommand cmd, const uint8_t mac[6], uint16_t sessionId);
    void broadcastTo(MBCommand cmd, uint16_t sessionId);
    uint16_t nextSequence();
    uint16_t nextSessionId();
};

extern MultiBoxController multiBoxController;
