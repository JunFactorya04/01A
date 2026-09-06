/**
 * @file multi_box_controller.h
 * @brief MULTI BOX ESP-NOW transport + shooting-session state machine
 * @date 2026-09-06
 *
 * Owns esp_now_init()/callbacks and the CENTER-side coordinator logic
 * (sessionId ownership, bulb open/close timing) plus the non-CENTER
 * node-side reaction to CENTER's broadcasts. The ESP-NOW receive callback
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
    void update();    // drain event queue, advance CENTER bulb timing

    // Node-side actions (START/FLASH), called from MultiBox's sensor logic.
    void sendStartOrEnd(bool isEnd);
    void sendFlashFire();

    // Shared actions.
    void broadcastDisarm();
    void requestExit();   // safe-disarm sequence for the UI's exit-confirm "OK"

private:
    void handleIncoming(const MBPacket& pkt, const uint8_t mac[6], unsigned long receivedAt);
    void centerOnStart(const MBPacket& pkt, const uint8_t mac[6]);
    void centerOnEndDetect(const MBPacket& pkt, const uint8_t mac[6]);
    void centerCloseBulbIfDue();
    void openBulb();
    void closeBulb();
    void forceCloseBulbIfExposing();
    void nodeOnCenterBroadcast(const MBPacket& pkt, const uint8_t mac[6]);

    void sendTo(MBCommand cmd, const uint8_t mac[6], uint16_t sessionId);
    void broadcastTo(MBCommand cmd, uint16_t sessionId);
    uint16_t nextSequence();
    uint16_t nextSessionId();
};

extern MultiBoxController multiBoxController;
