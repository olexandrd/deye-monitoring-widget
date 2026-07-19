#pragma once

#include <Arduino.h>

#include "metrics.h"

enum class AppState {
    Boot,
    WiFiConnecting,
    WiFiError,
    BleSetup,
    DeyePolling,
    Ok,
    Stale,
    DeyeError
};

struct SupplyBatteryStatus {
    bool valid;
    float voltage;
    uint8_t chargePercent;
};

void displayBegin();
void displaySetBrightnessPercent(uint8_t percent);
void displaySleep();
void displayWake();
void displayStatus(
    AppState state,
    const InverterMetrics& metrics,
    const char* detail,
    const SupplyBatteryStatus& supplyBattery
);
