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
    _trigger_mode_enc_last_pos = _enc.getCount();
    _reset_mode_input_state();
    
    // Enter loop
    while (1) {
        _trigger_mode_loop();
        
        if (_mode_exit_requested) {
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
    int encDelta = _read_encoder_delta(_trigger_mode_enc_last_pos);
    if (encDelta != 0) {
        triggerMode.handleEncoderRotate(encDelta);
    }

    ButtonEvent event = _read_mode_button_event();
    if (event == ButtonEvent::ShortPress) {
        handleTriggerModeButtonShortPress();
    } else if (event == ButtonEvent::LongPress) {
        handleTriggerModeButtonLongPress();
    }
}

void FactoryTest::handleTriggerModeButtonShortPress() {
    
    // Check if in TEST button area (simplified: if selected index is 2)
    // For now, just toggle selected output
    triggerMode.handleButtonPress();
    
    // Test trigger if at least one output is enabled
    if (triggerMode.getTriggerEnabled() || triggerMode.getRemoteEnabled()) {
        triggerMode.testTrigger();
    }
    
    _tone(800, 100);
}

void FactoryTest::handleTriggerModeButtonLongPress() {
    triggerMode.handleButtonLongPress();
    _mode_exit_requested = true;
    _tone(1500, 150);
}
