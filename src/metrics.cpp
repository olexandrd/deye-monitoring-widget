#include "metrics.h"

#include <math.h>

void resetMetrics(InverterMetrics& metrics) {
    metrics.valid = false;
    metrics.timestampMs = 0;
    metrics.batterySoc = MetricValue::Unknown;
    metrics.batteryVoltage = MetricValue::Unknown;
    metrics.batteryCurrent = MetricValue::Unknown;
    metrics.batteryPower = MetricValue::Unknown;
    metrics.dailyGenerationKwh = MetricValue::Unknown;
    metrics.loadPower = MetricValue::Unknown;
    metrics.gridPower = MetricValue::Unknown;
    metrics.pv1Power = MetricValue::Unknown;
    metrics.pv2Power = MetricValue::Unknown;
    metrics.pvTotalPower = MetricValue::Unknown;
    metrics.errorCode = 0;
}

void updateFakeMetrics(InverterMetrics& metrics, uint32_t nowMs) {
    const float phase = (nowMs % 600000UL) / 600000.0f;
    const float wave = sinf(phase * 2.0f * PI);
    const float wave2 = sinf((phase * 2.0f * PI) + 1.7f);

    metrics.valid = true;
    metrics.timestampMs = nowMs;
    metrics.batterySoc = 65.0f + wave * 4.0f;
    metrics.batteryVoltage = 53.2f + wave2 * 0.5f;
    metrics.batteryCurrent = -2.4f + wave * 1.2f;
    metrics.batteryPower = metrics.batteryVoltage * metrics.batteryCurrent;
    metrics.dailyGenerationKwh = 4.8f + phase * 5.4f;
    metrics.loadPower = 420.0f + wave2 * 90.0f;
    metrics.pv1Power = 410.0f + wave * 160.0f;
    metrics.pv2Power = 360.0f + wave2 * 120.0f;
    metrics.pvTotalPower = metrics.pv1Power + metrics.pv2Power;
    metrics.gridPower = metrics.loadPower + metrics.batteryPower - metrics.pvTotalPower;
    metrics.errorCode = 0;
}

bool isMetricKnown(float value) {
    return !isnan(value);
}
