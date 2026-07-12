#pragma once

#include <Arduino.h>

struct InverterMetrics {
    bool valid;
    uint32_t timestampMs;

    float batterySoc;
    float batteryVoltage;
    float batteryCurrent;
    float batteryPower;
    float dailyGenerationKwh;

    float loadPower;
    float gridPower;
    float pv1Power;
    float pv2Power;
    float pvTotalPower;

    int errorCode;
};

namespace MetricValue {
    const float Unknown = NAN;
}

void resetMetrics(InverterMetrics& metrics);
void updateFakeMetrics(InverterMetrics& metrics, uint32_t nowMs);
bool isMetricKnown(float value);
