/**
 * @file factory_test_display.cpp
 * @brief DISPLAY mode integration into FactoryTest (replaces old BRIGHTNESS menu entry)
 * @date 2026-07-23
 */

#include "factory_test.h"
#include "../display_mode/display_mode.h"
#include "../display_mode/display_mode_ui.h"

// ============ DISPLAY MODE ENTRY ============
void FactoryTest::_display_mode_test() {
    displayMode.loadConfig();
    displayMode.editMode.state = DisplayEditMode::SELECTING;
    displayMode.editMode.selectedIndex = 0;

    initDisplayUI();
    _display_enc_last_pos = _enc.getCount();
    _reset_mode_input_state();

    while (1) {
        _display_mode_loop();
        if (_mode_exit_requested) break;
        delay(10);
    }

    // Auto-save on exit (user rule: "thoát ra thì tự lưu")
    displayMode.saveConfig();
    displayMode.applyBrightness();
    displayMode.applyTheme();
    displayMode.applyRotation();
}

// ============ LOOP ============
void FactoryTest::_display_mode_loop() {
    handleDisplayInput();
    renderDisplayUI();
}

// ============ INPUT ============
void FactoryTest::handleDisplayInput() {
    int delta = _read_encoder_delta(_display_enc_last_pos);
    if (delta != 0) displayMode.handleEncoderRotate(delta);

    ButtonEvent ev = _read_mode_button_event();
    if (ev == ButtonEvent::ShortPress) {
        displayMode.handleButtonPress();
        _tone(800, 80);
    } else if (ev == ButtonEvent::LongPress) {
        _mode_exit_requested = true;   // save happens in _display_mode_test()
        _tone(1500, 200);
    }
}

// ============ OLD API WRAPPER ============
// Keep the legacy entry point compiling; route it to the new mode.
// (Old implementation still exists in ft_disp_test.cpp but is no longer used.)
