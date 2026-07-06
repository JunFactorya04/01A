/**
 * @file sleep_week_ui.h
 * @brief Sleep + Weekly Scheduler UI declarations
 * @date 2026-07-03
 */

#pragma once
#include <Arduino.h>

// ============ UI RENDER FUNCTIONS ============
void renderSleepWeekUI();
void initSleepWeekUI();

// ============ GLOBAL SCHEDULER COUNTDOWN OVERLAY ============
// Call these from ANY mode's render (before canvas push) and from main menu
// to show a 5s countdown popup before a scheduled SLEEP/WAKE event.
bool schedulerPopupActive();
void schedulerPopupDraw();
