/**
 * @file factory_test_multi_box.cpp
 * @brief MULTI BOX mode — placeholder, awaiting design instructions.
 *
 * This launcher slot used to be the standalone DISPLAY mode. Its settings
 * (brightness / power save / theme / rotation) have moved into SETTING
 * mode's "Display" sub-screen (see setting.cpp / setting_ui.cpp), which
 * delegates to the untouched DisplayMode class in display_mode.h/.cpp —
 * that class and its DisplayPowerSave engine are unaffected by this move
 * and still apply at boot / run at the main menu exactly as before.
 *
 * For now this mode is an empty shell: it just shows a placeholder screen
 * and exits on any button press, so the launcher menu keeps working while
 * this mode's real design is decided.
 */

#include "factory_test.h"
#include "../common/ui_theme.h"

extern FactoryTest* _ft;

// ============ PLACEHOLDER RENDER ============
static void renderMultiBoxPlaceholder() {
    if (!_ft || !_ft->_canvas) return;

    LGFX_Sprite* c = _ft->_canvas;
    c->fillScreen(UI_BG);

    c->setFont(&fonts::efontCN_16);
    c->setTextDatum(top_center);
    c->setTextColor(UI_AL);
    c->drawString("MULTI BOX", 120, 2);

    c->setTextDatum(top_left);
    c->setTextColor(UI_FG);
    c->drawString("<", 5, 2);

    c->setFont(&fonts::efontCN_12);
    c->setTextDatum(middle_center);
    c->setTextColor(UI_BORDER);
    c->drawString("Coming soon", 120, 67);

    c->setTextDatum(top_left);
    _ft->_canvas_update();
}

// ============ MODE ENTRY (placeholder) ============
void FactoryTest::_multi_box_test() {
    _multi_box_enc_last_pos = _enc.getCount();
    _reset_mode_input_state();

    while (1) {
        _multi_box_loop();
        if (_mode_exit_requested) break;
        delay(10);
    }
}

void FactoryTest::_multi_box_loop() {
    // Power save dim/screen-off after inactivity runs here too now.
    if (!_display_power_save_tick()) {
        handleMultiBoxInput();
    }
    renderMultiBoxPlaceholder();
}

void FactoryTest::handleMultiBoxInput() {
    // Drain encoder movement so it doesn't spill into whatever mode is
    // opened next. No settings exist yet -- any press just exits.
    _read_encoder_delta(_multi_box_enc_last_pos);

    ButtonEvent ev = _read_mode_button_event();
    if (ev == ButtonEvent::ShortPress || ev == ButtonEvent::LongPress) {
        _mode_exit_requested = true;
        _tone(1500, 150);
    }
}
