/**
 * @file nikon_ble.h
 * @brief Nikon BLE camera remote driver (ML-L7 "Remote" protocol, client role)
 * @date 2026-09-06
 *
 * Full rewrite of the previous EXPERIMENTAL server-role driver — see
 * nikon_protocol.h for why (wrong BLE role, wrong service UUID, no real
 * pairing handshake). ESP32 is now the BLE client, matching Sony/Canon/Fuji.
 */

#pragma once
#include "../../camera_driver.h"

class NikonBLE : public CameraDriver {
public:
    const char* name() const override { return "NIKON"; }

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
    bool connectTo(const String& addr);
    bool ensureConnected();
    bool runHandshake();
};
