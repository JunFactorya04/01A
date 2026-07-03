/**
 * @file factory_test_sleep_week.cpp
 * @brief Sleep + Weekly Scheduler integration into FactoryTest
 * @date 2026-07-03
 */

#include "factory_test.h"
#include "../sleep_week/sleep_week_scheduler.h"
#include "../sleep_week/sleep_week_ui.h"

extern FactoryTest* _ft;

// ============ SLEEP WEEK TEST ============
void FactoryTest::_sleep_week_test() {
    
    // Initialize Sleep Week Scheduler
    sleepWeekScheduler.init();
    initSleepWeekUI();
    
    // Enter loop
    while (1) {
        _sleep_week_loop();
        
        // Check for power button (exit)
        if (_check_next(false)) {
            sleepWeekScheduler.enableScheduler(false);
            break;
        }
        
        delay(10);
    }
}

// ============ SLEEP WEEK LOOP ============
void FactoryTest::_sleep_week_loop() {
    
    // 1. Update Sleep Week Scheduler logic
    sleepWeekScheduler.update();
    
    // 2. Handle input
    handleSleepWeekInput();
    
    // 3. Render UI
    renderSleepWeekUI();
}

// ============ INPUT HANDLING ============
void FactoryTest::handleSleepWeekInput() {
    
    // Read encoder
    int currentEncPos = _enc.getCount();
    int encDelta = currentEncPos - _sleep_week_enc_last_pos;
    _sleep_week_enc_last_pos = currentEncPos;
    
    if (encDelta != 0) {
        sleepWeekScheduler.handleEncoderRotate(encDelta);
    }
    
    // Read button (GPIO 42)
    bool btnPressed = (_btn_pwr.read() == 0);  // Active LOW
    
    // Button press detection
    if (btnPressed && !_sleep_week_btn_pressed) {
        // Button just pressed
        _sleep_week_btn_pressed = true;
        _sleep_week_long_press_timer = millis();
        _tone(1000, 50);  // Short beep
    }
    else if (!btnPressed && _sleep_week_btn_pressed) {
        // Button just released
        _sleep_week_btn_pressed = false;
        unsigned long pressDuration = millis() - _sleep_week_long_press_timer;
        
        if (pressDuration < 500) {
            // Short press
            handleSleepWeekButtonShortPress();
        } else {
            // Long press
            handleSleepWeekButtonLongPress();
        }
    }
    else if (btnPressed && _sleep_week_btn_pressed) {
        // Button held
        unsigned long pressDuration = millis() - _sleep_week_long_press_timer;
        
        if (pressDuration > 1000) {
            // Very long press (1s) - exit
            _sleep_week_btn_pressed = false;
            _tone(2000, 100);
            sleepWeekScheduler.enableScheduler(false);
        }
    }
}

void FactoryTest::handleSleepWeekButtonShortPress() {
    
    // Toggle selected day or confirm
    sleepWeekScheduler.handleButtonPress();
    _tone(800, 100);
}

void FactoryTest::handleSleepWeekButtonLongPress() {
    
    // Disable scheduler
    sleepWeekScheduler.handleButtonLongPress();
    _tone(1500, 150);
}
