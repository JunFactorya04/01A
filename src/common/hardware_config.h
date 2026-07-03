/**
 * @file hardware_config.h
 * @brief Common hardware configuration and global variables
 * @date 2026-07-02
 */

#pragma once
#include <HardwareSerial.h>
#include <Arduino.h>

// ============ GPIO PIN DEFINITIONS ============
#define CAMERA_TRIGGER_PIN 2
#define BUZZ_PIN 3
#define TRIGGER_G1_PIN 4
#define TRIGGER_G2_PIN 5
#define POWER_HOLD_PIN 46
#define ENCODER_PIN_A 40
#define ENCODER_PIN_B 41
#define POWER_BUTTON_PIN 42
#define BATTERY_ADC_PIN 10
#define TFLUNA_RX_PIN 13

// ============ GLOBAL HARDWARE INSTANCES ============
extern HardwareSerial TFSerial;

// ============ GLOBAL TRIGGER LOCK ============
// Prevent double trigger conflict between Auto Shoot and Timelapse
extern bool g_triggerLock;

bool acquireTriggerLock();
void releaseTriggerLock();
