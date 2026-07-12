#include "solarman_v5.h"

#include "modbus_crc.h"

namespace {
constexpr uint8_t StartByte = 0xA5;
constexpr uint8_t EndByte = 0x15;
constexpr uint16_t RequestControlCode = 0x4510;
constexpr uint16_t ResponseControlCode = 0x1510;
constexpr uint8_t FrameType = 0x02;
constexpr uint16_t SensorType = 0x0000;
constexpr size_t HeaderLen = 11;
constexpr size_t RequestPayloadOverhead = 15;
constexpr size_t ResponsePayloadOverhead = 14;

void putLe16(uint8_t* out, uint16_t value) {
    out[0] = value & 0xFF;
    out[1] = (value >> 8) & 0xFF;
}

void putLe32(uint8_t* out, uint32_t value) {
    out[0] = value & 0xFF;
    out[1] = (value >> 8) & 0xFF;
    out[2] = (value >> 16) & 0xFF;
    out[3] = (value >> 24) & 0xFF;
}

uint8_t frameChecksum(const uint8_t* data, size_t len) {
    uint8_t checksum = 0;
    for (size_t i = 1; i < len; ++i) {
        checksum += data[i];
    }
    return checksum;
}
}

SolarmanV5::SolarmanV5(uint32_t loggerSerial)
    : _loggerSerial(loggerSerial), _sequence(1) {
}

size_t SolarmanV5::buildReadHoldingRegistersFrame(
    uint8_t slaveId,
    uint16_t startRegister,
    uint16_t registerCount,
    uint8_t* out,
    size_t outMax
) {
    constexpr size_t modbusLen = 8;
    const uint16_t payloadLen = RequestPayloadOverhead + modbusLen;
    const size_t frameLen = HeaderLen + payloadLen + 2;

    if (out == nullptr || outMax < frameLen) {
        return 0;
    }

    uint8_t modbus[modbusLen] = {
        slaveId,
        0x03,
        static_cast<uint8_t>((startRegister >> 8) & 0xFF),
        static_cast<uint8_t>(startRegister & 0xFF),
        static_cast<uint8_t>((registerCount >> 8) & 0xFF),
        static_cast<uint8_t>(registerCount & 0xFF),
        0,
        0
    };
    const uint16_t crc = modbusCrc16(modbus, modbusLen - 2);
    modbus[6] = crc & 0xFF;
    modbus[7] = (crc >> 8) & 0xFF;

    size_t pos = 0;
    out[pos++] = StartByte;
    putLe16(&out[pos], payloadLen);
    pos += 2;
    putLe16(&out[pos], RequestControlCode);
    pos += 2;
    putLe16(&out[pos], _sequence++);
    pos += 2;
    putLe32(&out[pos], _loggerSerial);
    pos += 4;

    out[pos++] = FrameType;
    putLe16(&out[pos], SensorType);
    pos += 2;
    for (uint8_t i = 0; i < 12; ++i) {
        out[pos++] = 0;
    }

    for (size_t i = 0; i < modbusLen; ++i) {
        out[pos++] = modbus[i];
    }

    out[pos] = frameChecksum(out, pos);
    pos++;
    out[pos++] = EndByte;

    return pos;
}

bool SolarmanV5::parseResponse(
    const uint8_t* data,
    size_t len,
    uint8_t* modbusOut,
    size_t* modbusOutLen,
    size_t modbusOutMax
) {
    if (modbusOutLen != nullptr) {
        *modbusOutLen = 0;
    }
    if (data == nullptr || modbusOut == nullptr || modbusOutLen == nullptr) {
        return false;
    }
    if (len < HeaderLen + ResponsePayloadOverhead + 5 || data[0] != StartByte) {
        return false;
    }

    const uint16_t payloadLen = data[1] | (static_cast<uint16_t>(data[2]) << 8);
    const size_t frameLen = HeaderLen + payloadLen + 2;
    if (frameLen > len) {
        return false;
    }
    if (data[frameLen - 1] != EndByte) {
        return false;
    }

    const uint16_t controlCode = data[3] | (static_cast<uint16_t>(data[4]) << 8);
    if (controlCode != ResponseControlCode) {
        return false;
    }

    const size_t payloadStart = HeaderLen;
    const size_t payloadEnd = payloadStart + payloadLen;
    if (payloadEnd < payloadStart + ResponsePayloadOverhead) {
        return false;
    }

    const uint8_t expectedChecksum = frameChecksum(data, payloadEnd);
    if (data[payloadEnd] != expectedChecksum) {
        return false;
    }

    const size_t modbusStart = payloadStart + ResponsePayloadOverhead;
    if (payloadEnd <= modbusStart) {
        return false;
    }

    const size_t rawModbusLen = payloadEnd - modbusStart;
    if (rawModbusLen > modbusOutMax || rawModbusLen < 5) {
        return false;
    }

    for (size_t i = 0; i < rawModbusLen; ++i) {
        modbusOut[i] = data[modbusStart + i];
    }
    *modbusOutLen = rawModbusLen;

    const uint16_t receivedCrc = modbusOut[rawModbusLen - 2] |
        (static_cast<uint16_t>(modbusOut[rawModbusLen - 1]) << 8);
    const uint16_t calculatedCrc = modbusCrc16(modbusOut, rawModbusLen - 2);
    if (receivedCrc == calculatedCrc) {
        return true;
    }

    if (rawModbusLen >= 7 && modbusOut[rawModbusLen - 2] == 0 && modbusOut[rawModbusLen - 1] == 0) {
        const size_t correctedLen = rawModbusLen - 2;
        const uint16_t correctedReceivedCrc = modbusOut[correctedLen - 2] |
            (static_cast<uint16_t>(modbusOut[correctedLen - 1]) << 8);
        const uint16_t correctedCalculatedCrc = modbusCrc16(modbusOut, correctedLen - 2);
        if (correctedReceivedCrc == correctedCalculatedCrc) {
            *modbusOutLen = correctedLen;
            return true;
        }
    }

    return false;
}
