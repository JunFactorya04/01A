/**
 * @file fuji_ble.h
 * @brief Fujifilm BLE camera remote driver (Basic/unsecured pairing)
 * @date 2026-09-06
 */

#pragma once
#include "../../camera_driver.h"

class FujiBLE : public CameraDriver {
public:
    const char* name() const override { return "FUJI"; }

    bool pair(unsigned int scanSeconds) override;
    bool connect() override;
    bool isConnected() override;
    void disconnect() override;

    bool trigger() override;
    bool focus() override;
    bool shutterPress() override;
    bool shutterRelease() override;

    bool hasPairedCamera() override;
    String pairedAddress() override;
    void forgetCamera() override;

private:
    bool connectTo(const String& addr, bool doHandshake);
    bool ensureConnected();
};
