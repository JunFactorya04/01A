/**
 * @file factory_test_setting.cpp
 * @brief Setting Mode integration into FactoryTest
 * @date 2026-07-04
 */

#include "factory_test.h"
#include "../setting/setting.h"
#include "../setting/setting_ui.h"

extern FactoryTest* _ft;

// ============ RTC READ CALLBACK ============
static void _settingRTCRead(SettingDateTime& dt) {
    if (!_ft) return;
    I2C_BM8563_TimeTypeDef t;
    I2C_BM8563_DateTypeDef d;
    _ft->_rtc.getTime(&t);
    _ft->_rtc.getDate(&d);
    dt.second  = (uint8_t)t.seconds;
    dt.minute  = (uint8_t)t.minutes;
    dt.hour    = (uint8_t)t.hours;
    dt.day     = (uint8_t)d.date;
    dt.weekDay = (uint8_t)d.weekDay;
    dt.month   = (uint8_t)d.month;
    dt.year    = (uint16_t)d.year;
}

// ============ RTC WRITE CALLBACK ============
static void _settingRTCWrite(const SettingDateTime& dt) {
    if (!_ft) return;

    I2C_BM8563_TimeTypeDef t;
    t.seconds = 0;
    t.minutes = (int8_t)dt.minute;
    t.hours   = (int8_t)dt.hour;
    _ft->_rtc.setTime(&t);

    I2C_BM8563_DateTypeDef d;
    d.date    = (int8_t)dt.day;
    d.weekDay = (int8_t)dt.weekDay;
    d.month   = (int8_t)dt.month;
    d.year    = (int16_t)dt.year;
    _ft->_rtc.setDate(&d);
}

// ============ SETTING TEST ============
void FactoryTest::_setting_test() {
    Setting::setRTCCallbacks(_settingRTCRead, _settingRTCWrite);

    setting.init();          // load speaker config
    setting.readFromRTC();   // populate dateTime from BM8563

    initSettingUI();
    _setting_enc_last_pos = _enc.getCount();
    _reset_mode_input_state();

    while (1) {
        _setting_loop();
        if (_mode_exit_requested) break;
        delay(10);
    }
}

// ============ SETTING LOOP ============
void FactoryTest::_setting_loop() {
    handleSettingInput();
    renderSettingUI();
}

// ============ INPUT HANDLING ============
void FactoryTest::handleSettingInput() {
    int delta = _read_encoder_delta(_setting_enc_last_pos);
    if (delta != 0) setting.handleEncoderRotate(delta);

    ButtonEvent ev = _read_mode_button_event();
    if (ev == ButtonEvent::ShortPress)   handleSettingButtonShortPress();
    else if (ev == ButtonEvent::LongPress) handleSettingButtonLongPress();
}

void FactoryTest::handleSettingButtonShortPress() {
    setting.handleButtonPress();
    _tone(800, 80);
}

void FactoryTest::handleSettingButtonLongPress() {
    // Long press: save to RTC + system time + exit
    setting.handleButtonLongPress();  // applyToRTC() inside
    _mode_exit_requested = true;
    _tone(1500, 200);
}
