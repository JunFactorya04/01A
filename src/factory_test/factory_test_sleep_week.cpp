/**
 * @file factory_test_sleep_week.cpp
 * @brief Sleep + Weekly Scheduler integration into FactoryTest
 * @date 2026-07-03
 */

#include "factory_test.h"
#include "../sleep_week/sleep_week_scheduler.h"
#include "../sleep_week/sleep_week_ui.h"

extern FactoryTest* _ft;

// ============ SLEEP WEEK TEST ============
void FactoryTest::_sleep_week_test() {
    
    // Initialize Sleep Week Scheduler
    sleepWeekScheduler.init();
    initSleepWeekUI();
    _sleep_week_enc_last_pos = _enc.getCount();
    _reset_mode_input_state();
    
    // Enter loop
    while (1) {
        _sleep_week_loop();
        
        if (_mode_exit_requested) {
            sleepWeekScheduler.enableScheduler(false);
            break;
        }
        
        delay(10);
    }
}

// ============ SLEEP WEEK LOOP ============
void FactoryTest::_sleep_week_loop() {
    
    // 1. Update Sleep Week Scheduler logic
    sleepWeekScheduler.update();
    
    // 2. Handle input
    handleSleepWeekInput();
    
    // 3. Render UI
    renderSleepWeekUI();
}

// ============ INPUT HANDLING ============
void FactoryTest::handleSleepWeekInput() {
    int encDelta = _read_encoder_delta(_sleep_week_enc_last_pos);
    if (encDelta != 0) {
        sleepWeekScheduler.handleEncoderRotate(encDelta);
    }

    ButtonEvent event = _read_mode_button_event();
    if (event == ButtonEvent::ShortPress) {
        handleSleepWeekButtonShortPress();
    } else if (event == ButtonEvent::LongPress) {
        handleSleepWeekButtonLongPress();
    }
}

void FactoryTest::handleSleepWeekButtonShortPress() {
    
    // Toggle selected day or confirm
    sleepWeekScheduler.handleButtonPress();
    _tone(800, 100);
}

void FactoryTest::handleSleepWeekButtonLongPress() {
    sleepWeekScheduler.handleButtonLongPress();
    _mode_exit_requested = true;
    _tone(1500, 150);
}
