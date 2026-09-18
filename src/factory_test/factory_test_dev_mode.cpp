/**
 * @file factory_test_dev_mode.cpp
 * @brief FOR DEVELOP mode integration into FactoryTest
 *
 * Bench harness for MULTI BOX on a single box — see src/dev_mode/ for what it
 * actually does. This file is the usual mode-entry + input glue every mode in
 * this codebase has.
 */

#include "factory_test.h"
#include "../dev_mode/dev_mode.h"
#include "../dev_mode/dev_mode_ui.h"
#include "../multi_box/multi_box.h"
#include "../display_mode/display_mode.h"

extern FactoryTest* _ft;

void FactoryTest::_dev_mode_test() {
    devMode.init();
    initDevModeUI();
    _dev_mode_enc_last_pos = _enc.getCount();
    _reset_mode_input_state();

    while (1) {
        _dev_mode_loop();
        if (_mode_exit_requested) {
            devMode.teardown();
            break;
        }
        delay(10);
    }
}

void FactoryTest::_dev_mode_loop() {
    devMode.update();

    _battery_guard_tick();

    // Never dim mid-exposure — same rule MULTI BOX follows, for the same
    // reason: someone is watching the status while a shutter is open.
    if (multiBox.state.session == MultiBoxState::EXPOSING) {
        DisplayPowerSave::keepAwake();
        handleDevModeInput();
    } else if (!_display_power_save_tick()) {
        handleDevModeInput();
    }

    renderDevModeUI();
}

void FactoryTest::handleDevModeInput() {
    int encDelta = _read_encoder_delta(_dev_mode_enc_last_pos);
    if (encDelta != 0) devMode.handleEncoderRotate(encDelta);

    ButtonEvent event = _read_mode_button_event();
    if (event == ButtonEvent::ShortPress) {
        devMode.handleButtonPress();
        _tone(800, 100);
    } else if (event == ButtonEvent::LongPress) {
        // On the MAIN screen with nothing to back out of, a hold leaves the
        // mode. teardown() closes any open bulb and restores the real role.
        bool onMainTop = (!devMode.inAdvance() &&
                          devMode.editMode.state == DevModeEditMode::SELECTING);
        if (onMainTop) {
            _mode_exit_requested = true;
            _tone(1500, 150);
            return;
        }
        devMode.handleButtonLongPress();
        _tone(1000, 100);
    }
}
