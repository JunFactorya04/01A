/**
 * @file factory_test_auto_shoot.cpp
 * @brief Auto Shoot mode integration into FactoryTest
 * @date 2026-07-01
 */

#include "factory_test.h"
#include "../auto_shoot/auto_shoot.h"
#include "../auto_shoot/auto_shoot_ui.cpp"

extern FactoryTest* _ft;

// ============ AUTO SHOOT TEST ============
void FactoryTest::_auto_shoot_test() {
    
    // Initialize Auto Shoot
    autoShoot.init();
    initAutoShootUI();
    
    // Enter loop
    while (1) {
        _auto_shoot_loop();
        
        // Check for power button (exit)
        if (_check_next(false)) {
            autoShoot.stop();
            break;
        }
        
        delay(10);
    }
}

// ============ AUTO SHOOT LOOP ============
void FactoryTest::_auto_shoot_loop() {
    
    // 1. Update Auto Shoot logic
    autoShoot.update();
    
    // 2. Handle input
    handleAutoShootInput();
    
    // 3. Render UI
    renderAutoShootUI();
}

// ============ INPUT HANDLING ============
void FactoryTest::handleAutoShootInput() {
    
    // Read encoder
    int currentEncPos = _enc.getCount();
    int encDelta = currentEncPos - _auto_shoot_enc_last_pos;
    _auto_shoot_enc_last_pos = currentEncPos;
    
    if (encDelta != 0) {
        autoShoot.handleEncoderRotate(encDelta);
    }
    
    // Read button (GPIO 42)
    bool btnPressed = (_btn_pwr.read() == 0);  // Active LOW
    
    // Button press detection
    if (btnPressed && !_auto_shoot_btn_pressed) {
        // Button just pressed
        _auto_shoot_btn_pressed = true;
        _auto_shoot_long_press_timer = millis();
        _tone(1000, 50);  // Short beep
    }
    else if (!btnPressed && _auto_shoot_btn_pressed) {
        // Button just released
        _auto_shoot_btn_pressed = false;
        unsigned long pressDuration = millis() - _auto_shoot_long_press_timer;
        
        if (pressDuration < 500) {
            // Short press
            handleAutoShootButtonShortPress();
        } else {
            // Long press
            handleAutoShootButtonLongPress();
        }
    }
    else if (btnPressed && _auto_shoot_btn_pressed) {
        // Button held
        unsigned long pressDuration = millis() - _auto_shoot_long_press_timer;
        
        if (pressDuration > 1000) {
            // Very long press (1s) - exit
            _auto_shoot_btn_pressed = false;
            _tone(2000, 100);
            autoShoot.stop();
        }
    }
}

void FactoryTest::handleAutoShootButtonShortPress() {
    
    // In SELECTING mode: enter EDITING
    if (autoShoot.editMode.state == EditMode::SELECTING) {
        autoShoot.handleButtonPress();
        _tone(800, 100);
    }
    // In EDITING mode: save and exit
    else if (autoShoot.editMode.state == EditMode::EDITING) {
        autoShoot.handleButtonPress();
        _tone(1200, 100);
    }
}

void FactoryTest::handleAutoShootButtonLongPress() {
    
    // Exit to menu
    autoShoot.handleButtonLongPress();
    _tone(1500, 150);
}

// ============ START/STOP HANDLERS ============
void FactoryTest::_auto_shoot_start_triggered() {
    autoShoot.start();
    _tone(800, 200);
}

void FactoryTest::_auto_shoot_stop_triggered() {
    autoShoot.stop();
    _tone(2000, 200);
}
