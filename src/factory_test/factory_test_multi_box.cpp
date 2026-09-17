/**
 * @file factory_test_multi_box.cpp
 * @brief MULTI BOX mode integration into FactoryTest
 *
 * Wireless multi-node (native ESP-NOW) coordination: START DETECT / FLASH
 * DETECT / MAIN roles cooperating on a synchronized bulb-exposure shot.
 * See src/multi_box/ for the actual logic — this file is just the
 * mode-entry-point + input glue every mode in this codebase has
 * (factory_test_<mode>.cpp), matching e.g. factory_test_trigger_mode.cpp.
 */

#include "factory_test.h"
#include "../multi_box/multi_box.h"
#include "../multi_box/multi_box_controller.h"
#include "../multi_box/multi_box_ui.h"
#include "../display_mode/display_mode.h"   // DisplayPowerSave::keepAwake()

extern FactoryTest* _ft;

// ============ MODE ENTRY ============
void FactoryTest::_multi_box_test() {
    multiBox.init();
    initMultiBoxUI();
    _multi_box_enc_last_pos = _enc.getCount();
    _reset_mode_input_state();

    while (1) {
        _multi_box_loop();
        if (_mode_exit_requested) break;
        delay(10);
    }
}

// ============ MODE LOOP ============
void FactoryTest::_multi_box_loop() {
    // Sensor polling / ESP-NOW / bulb timing keep running every cycle
    // regardless of input/display-power-save state.
    multiBox.update();

    // Never dim/screen-off mid-exposure — a person needs to see the status.
    // Mirrors how _display_power_save_tick() itself already special-cases
    // the scheduler countdown popup (force-awake, skip its own tick/wake).
    if (multiBox.state.session == MultiBoxState::EXPOSING) {
        DisplayPowerSave::keepAwake();
        handleMultiBoxInput();
    // Low-battery guard -- warns, and on a flat pack asks this mode to exit
    // so its own clean-exit path runs before power is cut.
    _battery_guard_tick();

    } else if (!_display_power_save_tick()) {
        handleMultiBoxInput();
    }

    renderMultiBoxUI();
}

// ============ INPUT HANDLING ============
void FactoryTest::handleMultiBoxInput() {
    int encDelta = _read_encoder_delta(_multi_box_enc_last_pos);
    if (encDelta != 0) {
        multiBox.handleEncoderRotate(encDelta);
    }

    ButtonEvent event = _read_mode_button_event();
    if (event == ButtonEvent::ShortPress) {
        handleMultiBoxButtonShortPress();
    } else if (event == ButtonEvent::LongPress) {
        handleMultiBoxButtonLongPress();
    }
}

void FactoryTest::handleMultiBoxButtonShortPress() {
    if (multiBox.editMode.state == MultiBoxEditMode::CONFIRM_EXIT) {
        if (multiBox.editMode.confirmChoice == 1) {
            // OK: safe-disarm (force-close any open bulb, tell every paired
            // node, drop ESP-NOW) before actually leaving the mode.
            multiBoxController.requestExit();
            _mode_exit_requested = true;
            _tone(1500, 150);
        } else {
            multiBox.editMode.state = MultiBoxEditMode::SELECTING;
            _tone(1000, 100);
        }
        return;
    }

    multiBox.handleButtonPress();
    _tone(800, 100);
}

void FactoryTest::handleMultiBoxButtonLongPress() {
    if (multiBox.editMode.state == MultiBoxEditMode::CONFIRM_EXIT) {
        // Hold = cancel, same "long press = back" convention as every other
        // mode's sub-screen (e.g. Trigger Mode's Bluetooth screen).
        multiBox.editMode.state = MultiBoxEditMode::SELECTING;
        _tone(1000, 100);
        return;
    }

    bool wasOnMainTop = (multiBox.editMode.screen == MultiBoxEditMode::MAIN &&
                         multiBox.editMode.state == MultiBoxEditMode::SELECTING);
    if (wasOnMainTop) {
        // Never sleep/exit silently mid-shoot — always confirm first, and
        // exiting itself DISARMs rather than just abandoning the session.
        multiBox.editMode.state = MultiBoxEditMode::CONFIRM_EXIT;
        multiBox.editMode.confirmChoice = 0;   // default to CANCEL (safe)
        _tone(1200, 100);
        return;
    }

    multiBox.handleButtonLongPress();
    _tone(1000, 100);
}
