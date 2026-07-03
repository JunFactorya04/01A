/**
 * @file factory_test_trigger_mode.cpp
 * @brief Trigger Mode integration into FactoryTest
 * @date 2026-07-03
 */

#include "factory_test.h"
#include "../trigger_mode/trigger_mode.h"
#include "../trigger_mode/trigger_mode_ui.h"

extern FactoryTest* _ft;

// ============ TRIGGER MODE TEST ============
void FactoryTest::_trigger_mode_test() {
    
    // Initialize Trigger Mode
    triggerMode.init();
    initTriggerModeUI();
    
    // Enter loop
    while (1) {
        _trigger_mode_loop();
        
        // Check for power button (exit)
        if (_check_next(false)) {
            triggerMode.disableAll();
            break;
        }
        
        delay(10);
    }
}

// ============ TRIGGER MODE LOOP ============
void FactoryTest::_trigger_mode_loop() {
    
    // 1. Update Trigger Mode logic
    triggerMode.update();
    
    // 2. Handle input
    handleTriggerModeInput();
    
    // 3. Render UI
    renderTriggerModeUI();
}

// ============ INPUT HANDLING ============
void FactoryTest::handleTriggerModeInput() {
    
    // Read encoder
    int currentEncPos = _enc.getCount();
    int encDelta = currentEncPos - _trigger_mode_enc_last_pos;
    _trigger_mode_enc_last_pos = currentEncPos;
    
    if (encDelta != 0) {
        triggerMode.handleEncoderRotate(encDelta);
    }
    
    // Read button (GPIO 42)
    bool btnPressed = (_btn_pwr.read() == 0);  // Active LOW
    
    // Button press detection
    if (btnPressed && !_trigger_mode_btn_pressed) {
        // Button just pressed
        _trigger_mode_btn_pressed = true;
        _trigger_mode_long_press_timer = millis();
        _tone(1000, 50);  // Short beep
    }
    else if (!btnPressed && _trigger_mode_btn_pressed) {
        // Button just released
        _trigger_mode_btn_pressed = false;
        unsigned long pressDuration = millis() - _trigger_mode_long_press_timer;
        
        if (pressDuration < 500) {
            // Short press
            handleTriggerModeButtonShortPress();
        } else {
            // Long press
            handleTriggerModeButtonLongPress();
        }
    }
    else if (btnPressed && _trigger_mode_btn_pressed) {
        // Button held
        unsigned long pressDuration = millis() - _trigger_mode_long_press_timer;
        
        if (pressDuration > 1000) {
            // Very long press (1s) - exit
            _trigger_mode_btn_pressed = false;
            _tone(2000, 100);
            triggerMode.disableAll();
        }
    }
}

void FactoryTest::handleTriggerModeButtonShortPress() {
    
    // Check if in TEST button area (simplified: if selected index is 2)
    // For now, just toggle selected output
    triggerMode.handleButtonPress();
    
    // If both outputs are enabled, also trigger test
    if (triggerMode.getTriggerEnabled() && triggerMode.getWifiBtEnabled()) {
        triggerMode.testTrigger();
    }
    
    _tone(800, 100);
}

void FactoryTest::handleTriggerModeButtonLongPress() {
    
    // Disable all and exit
    triggerMode.handleButtonLongPress();
    _tone(1500, 150);
}
