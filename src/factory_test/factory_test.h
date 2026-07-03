/**
 * @file factory_test.h
 * @author Forairaaaaa
 * @brief
 * @version 0.1
 * @date 2023-06-06
 *
 * @copyright Copyright (c) 2023
 *
 */
#pragma once
#include <Arduino.h>
#include <Button.h>
#include <I2C_BM8563.h>
#include <LovyanGFX.h>
#include <ESP32Encoder.h>

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_wifi.h>

#define FW_VERISON "v0.1"
#define BUZZ_PIN 3
#define POWER_HOLD_PIN 46

struct WiFiList_t
{
    String ssid = "";
    int8_t rssi = 0;
};

class FactoryTest
{
public:
    bool _is_test_mode;

    /* System */
    inline void _stuck_forever()
    {
        while (1)
        {
            delay(100);
        }
    }
    void _power_on();
    void _power_off();
    void _get_test_mode();
    bool _check_test_mode();

    /* Display */
    LGFX_Device* _disp;
    LGFX_Sprite* _canvas;
    inline void _canvas_update() { _canvas->pushSprite(0, 0); }
    void _disp_init();
    void _disp_test();
    void _disp_set_brightness();

    /* Button */
    Button _btn_pwr = Button(42, 20);
    void _key_init();
    void _key_test();
    void _check_reboot();
    bool _check_next(bool checkPowerOff = true);
    void _wait_next();

    /* Encoder */
    // RotaryEncoder _enc = RotaryEncoder(40, 41, RotaryEncoder::LatchMode::TWO03);
    ESP32Encoder _enc;
    int _enc_pos;
    bool _check_encoder(bool playBuzz = true);
    void _encoder_test();
    void _encoder_test_new();
    void _encoder_test_user();

    /* Buzzer */
    inline void _tone(unsigned int frequency, unsigned long duration = 0UL) { tone(BUZZ_PIN, frequency, duration); }
    inline void _noTone() { noTone(BUZZ_PIN); }

    /* RTC */
    I2C_BM8563 _rtc;
    void _rtc_init();
    void _rtc_test();
    void _rtc_wakeup_test();
    void _rtc_wakeup_test_user();

    /* Wifi */
    WiFiList_t _wifi_list[13];
    bool _wifi_tested;
    void _wifi_start_test_task();
    void _wifi_test();

    /* BLE */
    bool _is_ble_inited;
    void _ble_test();

    /* IO */
    void _io_test();
    void _io_test_user();

    /* Auto Shoot */
    void _auto_shoot_test();
    void _auto_shoot_loop();
    void handleAutoShootInput();
    void handleAutoShootButtonShortPress();
    void handleAutoShootButtonLongPress();
    void _auto_shoot_start_triggered();
    void _auto_shoot_stop_triggered();
    int _auto_shoot_enc_last_pos = 0;
    unsigned long _auto_shoot_long_press_timer = 0;
    bool _auto_shoot_btn_pressed = false;

    /* Timelapse */
    void _timelapse_test();
    void _timelapse_loop();
    void handleTimelapseInput();
    void handleTimelapseButtonShortPress();
    void handleTimelapseButtonLongPress();
    void _timelapse_start_triggered();
    void _timelapse_stop_triggered();
    int _timelapse_enc_last_pos = 0;
    unsigned long _timelapse_long_press_timer = 0;
    bool _timelapse_btn_pressed = false;

    /* Trigger Mode */
    void _trigger_mode_test();
    void _trigger_mode_loop();
    void handleTriggerModeInput();
    void handleTriggerModeButtonShortPress();
    void handleTriggerModeButtonLongPress();
    int _trigger_mode_enc_last_pos = 0;
    unsigned long _trigger_mode_long_press_timer = 0;
    bool _trigger_mode_btn_pressed = false;

    /* Sleep Week Scheduler */
    void _sleep_week_test();
    void _sleep_week_loop();
    void handleSleepWeekInput();
    void handleSleepWeekButtonShortPress();
    void handleSleepWeekButtonLongPress();
    int _sleep_week_enc_last_pos = 0;
    unsigned long _sleep_week_long_press_timer = 0;
    bool _sleep_week_btn_pressed = false;

    /* Arkanoid */
    void _arkanoid_start();
    void _arkanoid_setup();
    void _arkanoid_loop();
    void _InitGame(void);        // Initialize game
    void _UpdateGame(void);      // Update game (one frame)
    void _DrawGame(void);        // Draw game (one frame)
    void _UnloadGame(void);      // Unload game
    void _UpdateDrawFrame(void); // Update and Draw (one frame)

public:
    FactoryTest()
        : _is_test_mode(false), _disp(nullptr), _canvas(nullptr), _enc_pos(0), _wifi_tested(false), _is_ble_inited(false)
    {
    }
    ~FactoryTest() = default;

    void init();
    void start_factory_test();
};
