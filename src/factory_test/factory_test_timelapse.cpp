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
    _timelapse_enc_last_pos = _enc.getCount();
    _reset_mode_input_state();
    
    // Enter loop
    while (1) {
        _timelapse_loop();
        
        if (_mode_exit_requested) {
            _scheduler_autostart_pending = false;   // cancel pending auto-START
            timelapse.stop();
            break;
        }
        
        delay(10);
    }
}

// ============ TIMELAPSE LOOP ============
void FactoryTest::_timelapse_loop() {
    
    // 0. Scheduler auto-START (armed only by a scheduled WEEK wake):
    //    shows a countdown popup, then behaves exactly like pressing START
    if (_scheduler_autostart_pending && millis() >= _scheduler_autostart_at) {
        _scheduler_autostart_pending = false;
        timelapse.start();
        _tone(800, 200);
    }

    // 1. Update Timelapse logic
    timelapse.update();
    
    // 2. Handle input
    handleTimelapseInput();
    
    // 3. Render UI
    renderTimelapseUI();

    // 4. Countdown popup on top (no-op unless autostart is pending)
    _scheduler_autostart_popup();
}

// ============ INPUT HANDLING ============
void FactoryTest::handleTimelapseInput() {
    int encDelta = _read_encoder_delta(_timelapse_enc_last_pos);
    if (encDelta != 0) {
        timelapse.handleEncoderRotate(encDelta);
    }

    ButtonEvent event = _read_mode_button_event();
    if (event == ButtonEvent::ShortPress) {
        handleTimelapseButtonShortPress();
    } else if (event == ButtonEvent::LongPress) {
        handleTimelapseButtonLongPress();
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
    timelapse.handleButtonLongPress();
    _mode_exit_requested = true;
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
