/**
 * @file calibration_mode.h
 * @brief TF-Luna Calibration Mode - Distance & Strength calibration
 * @date 2026-07-01
 */

#pragma once
#include <Arduino.h>

// ============ CALIBRATION STATE ============
struct CalibrationState {
    enum CalibMode {
        IDLE = 0,
        MEASURING_DISTANCE = 1,
        MEASURING_STRENGTH = 2,
        RESULTS = 3
    } mode = IDLE;
    
    float measured_distance = 0.0f;
    uint16_t measured_strength = 0;
    uint32_t sample_count = 0;
    
    // Statistics
    float distance_min = 999.0f;
    float distance_max = 0.0f;
    float distance_avg = 0.0f;
    uint16_t strength_min = 65535;
    uint16_t strength_max = 0;
    uint16_t strength_avg = 0;
    
    // Sampling control
    unsigned long start_time = 0;
    unsigned long last_sample_time = 0;
    bool sampling_active = false;
};

// ============ CALIBRATION CLASS ============
class CalibrationMode {
public:
    CalibrationState state;
    
    CalibrationMode();
    ~CalibrationMode() = default;
    
    // ===== Mode Control =====
    void startDistanceCalibration();
    void startStrengthCalibration();
    void stopCalibration();
    void resetCalibration();
    
    // ===== Sampling =====
    void updateSamples();
    void addSample(float distance, uint16_t strength);
    void calculateStatistics();
    
    // ===== Results =====
    const char* getResultsString();
    void printResults();
    
private:
    float accumulator_distance = 0.0f;
    uint32_t accumulator_strength = 0;
};

extern CalibrationMode calibrationMode;
