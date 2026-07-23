/**
 * @file nikon_ble.h
 * @brief Nikon BLE camera remote driver (ML-L7 emulation) — EXPERIMENTAL
 * @date 2026-07-23
 */

#pragma once
#include "../../camera_driver.h"

class NikonBLE : public CameraDriver {
public:
    const char* name() const override { return "NIKON"; }

    // Advertise as ML-L7 and wait scanSeconds for camera to connect
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
    bool startServer();
    bool waitForCamera(unsigned int seconds);
};
