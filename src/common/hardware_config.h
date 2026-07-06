/**
 * @file hardware_config.h
 * @brief Common hardware configuration and global variables
 * @date 2026-07-02
 */

#pragma once
#include <HardwareSerial.h>
#include <Arduino.h>

// ============ GPIO PIN DEFINITIONS ============
// Port B connector (HY2.0-4P): Yellow=G2=GPIO2, White=G1=GPIO1
#define TRIGGER_G1_PIN 1    // Port B White
#define TRIGGER_G2_PIN 2    // Port B Yellow  (camera/DIN trigger output)
#define BUZZ_PIN 3
// GPIO 4 = ST7789V2 RS (display) — DO NOT USE for trigger
// GPIO 5 = ST7789V2 MOSI (display) — DO NOT USE for trigger
#define POWER_HOLD_PIN 46
#define ENCODER_PIN_A 40
#define ENCODER_PIN_B 41
#define POWER_BUTTON_PIN 42
#define BATTERY_ADC_PIN 10
#define TFLUNA_RX_PIN 13

// ============ TF-LUNA I2C (Port A: SDA=GPIO13, SCL=GPIO15) ============
#define TFLUNA_I2C_ADDR 0x10
#define TFLUNA_SDA_PIN  13
#define TFLUNA_SCL_PIN  15

// ============ GLOBAL HARDWARE INSTANCES ============
extern HardwareSerial TFSerial;

// ============ GLOBAL SPEAKER CONTROL ============
// Checked by FactoryTest::_tone() — set false to mute all buzzer feedback
extern bool g_speakerEnabled;

// ============ GLOBAL TRIGGER LOCK ============
// Prevent double trigger conflict between Auto Shoot and Timelapse
extern bool g_triggerLock;

bool acquireTriggerLock();
void releaseTriggerLock();
