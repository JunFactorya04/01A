/**
 * @file factory_test_auto_shoot.cpp
 * @brief Auto Shoot mode integration into FactoryTest
 * @date 2026-07-01
 */

#include "factory_test.h"
#include "../auto_shoot/auto_shoot.h"
#include "../auto_shoot/auto_shoot_ui.h"
#include "../auto_shoot/tf_luna.h"

extern FactoryTest* _ft;

// ============ AUTO SHOOT TEST ============
void FactoryTest::_auto_shoot_test() {
    
    // Initialize Auto Shoot
    autoShoot.init();
    initAutoShootUI();
    _auto_shoot_enc_last_pos = _enc.getCount();
    _reset_mode_input_state();
    
    // Enter loop
    while (1) {
        _auto_shoot_loop();
        
        if (_mode_exit_requested) {
            _scheduler_autostart_pending = false;   // cancel pending auto-START
            autoShoot.stop();
            break;
        }
        
        delay(10);
    }
}

// ============ AUTO SHOOT LOOP ============
void FactoryTest::_auto_shoot_loop() {
    
    // 0. Scheduler auto-START (armed only by a scheduled WEEK wake):
    //    shows a countdown popup, then behaves exactly like pressing START
    if (_scheduler_autostart_pending && millis() >= _scheduler_autostart_at) {
        _scheduler_autostart_pending = false;
        autoShoot.start();
        _tone(800, 200);
    }

    // 1. Update Auto Shoot logic
    autoShoot.update();
    
    // 2. Handle input
    handleAutoShootInput();
    
    // 3. Render UI
    renderAutoShootUI();

    // 4. Countdown popup on top (no-op unless autostart is pending)
    _scheduler_autostart_popup();
}

// ============ INPUT HANDLING ============
void FactoryTest::handleAutoShootInput() {
    int encDelta = _read_encoder_delta(_auto_shoot_enc_last_pos);
    if (encDelta != 0) {
        autoShoot.handleEncoderRotate(encDelta);
    }

    ButtonEvent event = _read_mode_button_event();
    if (event == ButtonEvent::ShortPress) {
        handleAutoShootButtonShortPress();
    } else if (event == ButtonEvent::LongPress) {
        handleAutoShootButtonLongPress();
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
    autoShoot.handleButtonLongPress();
    _mode_exit_requested = true;
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
