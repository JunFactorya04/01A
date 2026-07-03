/**
 * @file factory_test_timelapse.cpp
 * @brief Timelapse mode integration into FactoryTest
 * @date 2026-07-02
 */

#include "factory_test.h"
#include "../timelapse/timelapse.h"
#include "../timelapse/timelapse_ui.h"

extern FactoryTest* _ft;

// ============ TIMELAPSE TEST ============
void FactoryTest::_timelapse_test() {
    
    // Initialize Timelapse
    timelapse.init();
    initTimelapseUI();
    
    // Enter loop
    while (1) {
        _timelapse_loop();
        
        // Check for power button (exit)
        if (_check_next(false)) {
            timelapse.stop();
            break;
        }
        
        delay(10);
    }
}

// ============ TIMELAPSE LOOP ============
void FactoryTest::_timelapse_loop() {
    
    // 1. Update Timelapse logic
    timelapse.update();
    
    // 2. Handle input
    handleTimelapseInput();
    
    // 3. Render UI
    renderTimelapseUI();
}

// ============ INPUT HANDLING ============
void FactoryTest::handleTimelapseInput() {
    
    // Read encoder
    int currentEncPos = _enc.getCount();
    int encDelta = currentEncPos - _timelapse_enc_last_pos;
    _timelapse_enc_last_pos = currentEncPos;
    
    if (encDelta != 0) {
        timelapse.handleEncoderRotate(encDelta);
    }
    
    // Read button (GPIO 42)
    bool btnPressed = (_btn_pwr.read() == 0);  // Active LOW
    
    // Button press detection
    if (btnPressed && !_timelapse_btn_pressed) {
        // Button just pressed
        _timelapse_btn_pressed = true;
        _timelapse_long_press_timer = millis();
        _tone(1000, 50);  // Short beep
    }
    else if (!btnPressed && _timelapse_btn_pressed) {
        // Button just released
        _timelapse_btn_pressed = false;
        unsigned long pressDuration = millis() - _timelapse_long_press_timer;
        
        if (pressDuration < 500) {
            // Short press
            handleTimelapseButtonShortPress();
        } else {
            // Long press
            handleTimelapseButtonLongPress();
        }
    }
    else if (btnPressed && _timelapse_btn_pressed) {
        // Button held
        unsigned long pressDuration = millis() - _timelapse_long_press_timer;
        
        if (pressDuration > 1000) {
            // Very long press (1s) - exit
            _timelapse_btn_pressed = false;
            _tone(2000, 100);
            timelapse.stop();
        }
    }
}

void FactoryTest::handleTimelapseButtonShortPress() {
    
    // In SELECTING mode: enter EDITING or toggle
    if (timelapse.editMode.state == TimelapseEditMode::SELECTING) {
        timelapse.handleButtonPress();
        _tone(800, 100);
    }
    // In EDITING mode: save and exit
    else if (timelapse.editMode.state == TimelapseEditMode::EDITING) {
        timelapse.handleButtonPress();
        _tone(1200, 100);
    }
}

void FactoryTest::handleTimelapseButtonLongPress() {
    
    // Exit to menu
    timelapse.handleButtonLongPress();
    _tone(1500, 150);
}

// ============ START/STOP HANDLERS ============
void FactoryTest::_timelapse_start_triggered() {
    timelapse.start();
    _tone(800, 200);
}

void FactoryTest::_timelapse_stop_triggered() {
    timelapse.stop();
    _tone(2000, 200);
}
