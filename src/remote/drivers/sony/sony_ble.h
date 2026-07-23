/**
 * @file sony_ble.h
 * @brief Sony BLE camera remote driver (freemote protocol)
 * @date 2026-07-23
 */

#pragma once
#include "../../camera_driver.h"

class SonyBLE : public CameraDriver {
public:
    const char* name() const override { return "SONY"; }

    bool pair(unsigned int scanSeconds) override;
    bool connect() override;
    bool isConnected() override;
    void disconnect() override;

    bool trigger() override;
    bool focus() override;

    bool hasPairedCamera() override;
    String pairedAddress() override;
    void forgetCamera() override;

private:
    bool connectTo(const String& addr, uint8_t addrType);
    bool ensureConnected();
};
