#pragma once

#include <Arduino.h>

#include "metrics.h"
#include "solarman_v5.h"

namespace DeyeRegisters {
    // TODO: Verify these addresses against the exact inverter/logger model.
    // Keep all Deye register-map edits here.
    constexpr uint16_t START = 108;
    constexpr uint16_t COUNT = 84;

    constexpr uint16_t DAILY_GENERATION = 108;
    constexpr uint16_t BATTERY_SOC = 184;
    constexpr uint16_t BATTERY_VOLTAGE = 183;
    constexpr uint16_t BATTERY_CURRENT = 191;
    constexpr uint16_t BATTERY_POWER = 190;
    constexpr uint16_t LOAD_POWER = 178;
    constexpr uint16_t GRID_POWER = 169;
    constexpr uint16_t PV1_POWER = 186;
    constexpr uint16_t PV2_POWER = 187;
}

class DeyeClient {
public:
    DeyeClient(
        const char* host,
        uint16_t port,
        uint32_t loggerSerial,
        uint8_t slaveId
    );

    bool poll(InverterMetrics& metrics);
    const char* lastError() const;

private:
    bool applyRegisters(const uint8_t* modbus, size_t len, InverterMetrics& metrics);
    bool readU16(uint16_t reg, uint16_t& value) const;
    bool readS16(uint16_t reg, int16_t& value) const;
    void setError(const char* error);

    const char* _host;
    uint16_t _port;
    uint8_t _slaveId;
    SolarmanV5 _solarman;
    const char* _lastError;
    uint16_t _rawRegisters[DeyeRegisters::COUNT];
};
