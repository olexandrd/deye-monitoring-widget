#pragma once

#include <stddef.h>
#include <stdint.h>

class SolarmanV5 {
public:
    explicit SolarmanV5(uint32_t loggerSerial);

    size_t buildReadHoldingRegistersFrame(
        uint8_t slaveId,
        uint16_t startRegister,
        uint16_t registerCount,
        uint8_t* out,
        size_t outMax
    );

    bool parseResponse(
        const uint8_t* data,
        size_t len,
        uint8_t* modbusOut,
        size_t* modbusOutLen,
        size_t modbusOutMax
    );

private:
    uint32_t _loggerSerial;
    uint16_t _sequence;
};
