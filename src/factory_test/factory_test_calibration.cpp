/**
 * @file factory_test_calibration.cpp
 * @brief Calibration Mode integration into FactoryTest
 * @date 2026-07-01
 */

#include "factory_test.h"
#include "../calibration/calibration_mode.h"
#include "../calibration/calibration_ui.cpp"

extern FactoryTest* _ft;

// ============ CALIBRATION TEST ============
void FactoryTest::_calibration_test() {
    
    // Show calibration menu
    while (1) {
        renderCalibrationMenuUI();
        
        if (_check_encoder(false)) {
            break;  // Menu selected
        }
        
        if (_check_next(false)) {
            return;  // Exit
        }
        
        delay(50);
    }
    
    // Execute selected calibration
    int selection = _enc_pos % 2;
    
    if (selection == 0) {
        _calibration_distance_mode();
    } else {
        _calibration_strength_mode();
    }
}

// ============ DISTANCE CALIBRATION ============
void FactoryTest::_calibration_distance_mode() {
    
    calibrationMode.startDistanceCalibration();
    
    while (1) {
        // Update sensor
        calibrationMode.updateSamples();
        
        // Update display reading
        calibrationMode.state.measured_distance = getTfLunaDistanceRaw();
        
        // Render
        renderCalibrationUI();
        
        // Check for stop
        if (_check_next(false)) {
            calibrationMode.stopCalibration();
            break;
        }
        
        delay(20);
    }
    
    // Show results
    showCalibrationResults();
}

// ============ STRENGTH CALIBRATION ============
void FactoryTest::_calibration_strength_mode() {
    
    calibrationMode.startStrengthCalibration();
    
    while (1) {
        // Update sensor
        calibrationMode.updateSamples();
        
        // Update display reading
        calibrationMode.state.measured_strength = getTfLunaStrength();
        
        // Render
        renderCalibrationUI();
        
        // Check for stop
        if (_check_next(false)) {
            calibrationMode.stopCalibration();
            break;
        }
        
        delay(20);
    }
    
    // Show results
    showCalibrationResults();
}

// ============ RESULTS ============
void FactoryTest::showCalibrationResults() {
    
    while (1) {
        renderCalibrationUI();
        
        if (_check_next(false)) {
            break;
        }
        
        delay(100);
    }
    
    // Print to serial for logging
    calibrationMode.printResults();
}

// ============ MENU UI ============
void FactoryTest::renderCalibrationMenuUI() {
    _canvas->fillScreen(0x0000);
    
    _canvas->setFont(&fonts::efontCN_24);
    _canvas->setTextDatum(top_center);
    _canvas->setTextColor(0x07E0);
    _canvas->drawString("CALIBRATION", 120, 10);
    
    _canvas->setFont(&fonts::efontCN_16);
    _canvas->setTextDatum(top_center);
    
    // Option 0: Distance
    if (_enc_pos % 2 == 0) {
        _canvas->fillRoundRect(20, 50, 200, 30, 5, 0x07E0);
        _canvas->setTextColor(0x0000);
    } else {
        _canvas->drawRoundRect(20, 50, 200, 30, 5, 0x4208);
        _canvas->setTextColor(0xFFFF);
    }
    _canvas->drawString("Distance Calibration", 120, 60);
    
    // Option 1: Strength
    if (_enc_pos % 2 == 1) {
        _canvas->fillRoundRect(20, 90, 200, 30, 5, 0x07E0);
        _canvas->setTextColor(0x0000);
    } else {
        _canvas->drawRoundRect(20, 90, 200, 30, 5, 0x4208);
        _canvas->setTextColor(0xFFFF);
    }
    _canvas->drawString("Strength Calibration", 120, 100);
    
    _canvas_update();
}
