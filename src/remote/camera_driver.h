/**
 * @file camera_driver.h
 * @brief Common API for BLE camera remote drivers (Sony / Canon / Nikon)
 * @date 2026-07-23
 *
 * ESP32 acts as BLE client (remote), camera is BLE server.
 * Each brand implements this interface; RemoteManager routes to the
 * active driver. Paired camera MAC persists in NVS per driver.
 */

#pragma once
#include <Arduino.h>

enum class CameraBrand : uint8_t {
    None  = 0,   // plain GPIO pulse on G1 (original behavior)
    Sony  = 1,
    Canon = 2,
    Nikon = 3,
};

class CameraDriver {
public:
    virtual ~CameraDriver() = default;

    virtual const char* name() const = 0;

    // Blocking scan + pair (camera must be in its BLE pairing screen).
    // On success stores camera identity in NVS. scanSeconds ~10.
    virtual bool pair(unsigned int scanSeconds) = 0;

    virtual bool connect() = 0;        // reconnect using saved identity
    virtual bool isConnected() = 0;
    virtual void disconnect() = 0;

    virtual bool trigger() = 0;        // take one photo (auto-reconnect)
    virtual bool focus() = 0;          // half-press focus

    virtual bool hasPairedCamera() = 0;
    virtual String pairedAddress() = 0;
    virtual void forgetCamera() = 0;   // clear saved identity
};
