/**
 * @file canon_ble.h
 * @brief Canon BLE camera remote driver (BR-E1 emulation)
 * @date 2026-07-23
 */

#pragma once
#include "../../camera_driver.h"

class CanonBLE : public CameraDriver {
public:
    const char* name() const override { return "CANON"; }

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
    bool connectTo(const String& addr, bool doHandshake);
    bool ensureConnected();
};
