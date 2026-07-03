/**
 * @file factory_test_input.cpp
 * @brief Shared encoder and power-button input helpers for feature modes
 */

#include "factory_test.h"
#include "../common/hardware_config.h"

int FactoryTest::_read_encoder_delta(int& last_pos, bool playBuzz)
{
    int current_pos = _enc.getCount();
    int delta = current_pos - last_pos;

    if (delta == 0)
    {
        return 0;
    }

    if (playBuzz)
    {
        _noTone();
        _tone((delta > 0) ? 3000 : 3500, 20);
    }

    last_pos = current_pos;
    return delta;
}

FactoryTest::ButtonEvent FactoryTest::_read_mode_button_event(unsigned long shortPressMs, unsigned long longPressMs)
{
    // Use digitalRead() directly for reliable detection
    // without depending on Button library's internal debounce cache.
    bool button_now = (digitalRead(POWER_BUTTON_PIN) == LOW);

    if (button_now && !_mode_btn_pressed)
    {
        _mode_btn_pressed = true;
        _mode_btn_press_start = millis();
        _tone(1000, 40);
        return ButtonEvent::None;
    }

    if (!button_now && _mode_btn_pressed)
    {
        _mode_btn_pressed = false;

        unsigned long press_duration = millis() - _mode_btn_press_start;
        if (press_duration >= longPressMs)
        {
            return ButtonEvent::LongPress;
        }

        return ButtonEvent::ShortPress;
    }

    return ButtonEvent::None;
}

void FactoryTest::_reset_mode_input_state()
{
    // Wait for any lingering button press (e.g. from launcher click) to
    // be physically released before accepting new input.  Cap at 300 ms
    // so we never block indefinitely.
    unsigned long deadline = millis() + 300;
    while (digitalRead(POWER_BUTTON_PIN) == LOW && millis() < deadline)
    {
        delay(5);
    }
    delay(20);   // extra settle time

    _mode_btn_pressed = false;
    _mode_btn_press_start = 0;
    _mode_exit_requested = false;
}
