#include "deye_client.h"

#include <WiFiClient.h>

namespace {
constexpr uint32_t ConnectTimeoutMs = 5000;
constexpr uint32_t ReadTimeoutMs = 5000;
constexpr size_t TxBufferSize = 96;
constexpr size_t RxBufferSize = 256;
constexpr size_t ModbusBufferSize = 192;

float signedPower(int16_t value) {
    return static_cast<float>(value);
}

void printHex(const char* label, const uint8_t* data, size_t len) {
    Serial.printf("%s (%u):", label, static_cast<unsigned>(len));
    for (size_t i = 0; i < len; ++i) {
        Serial.printf(" %02X", data[i]);
    }
    Serial.println();
}

size_t expectedSolarmanFrameLen(const uint8_t* data, size_t len) {
    if (len < 3 || data[0] != 0xA5) {
        return 0;
    }
    const uint16_t payloadLen = data[1] | (static_cast<uint16_t>(data[2]) << 8);
    return 11 + payloadLen + 2;
}
}

DeyeClient::DeyeClient(
    const char* host,
    uint16_t port,
    uint32_t loggerSerial,
    uint8_t slaveId
) : _host(host),
    _port(port),
    _slaveId(slaveId),
    _solarman(loggerSerial),
    _lastError("not polled") {
    memset(_rawRegisters, 0, sizeof(_rawRegisters));
}

void DeyeClient::configure(
    const char* host,
    uint16_t port,
    uint32_t loggerSerial,
    uint8_t slaveId
) {
    _host = host;
    _port = port;
    _slaveId = slaveId;
    _solarman.setLoggerSerial(loggerSerial);
    setError("not polled");
}

bool DeyeClient::poll(InverterMetrics& metrics) {
    uint8_t tx[TxBufferSize];
    uint8_t rx[RxBufferSize];
    uint8_t modbus[ModbusBufferSize];

    const size_t txLen = _solarman.buildReadHoldingRegistersFrame(
        _slaveId,
        DeyeRegisters::START,
        DeyeRegisters::COUNT,
        tx,
        sizeof(tx)
    );
    if (txLen == 0) {
        setError("frame build failed");
        return false;
    }

    Serial.printf("Deye poll start: %s:%u\n", _host, _port);
    printHex("Deye TX", tx, txLen);

    WiFiClient client;
    client.setTimeout(ReadTimeoutMs / 1000);
    if (!client.connect(_host, _port, ConnectTimeoutMs)) {
        setError("connect failed");
        Serial.println("Deye poll failed: connect failed");
        return false;
    }

    const size_t written = client.write(tx, txLen);
    client.flush();
    if (written != txLen) {
        client.stop();
        setError("write failed");
        Serial.println("Deye poll failed: write failed");
        return false;
    }

    size_t rxLen = 0;
    const uint32_t deadline = millis() + ReadTimeoutMs;
    while (millis() < deadline && rxLen < sizeof(rx)) {
        while (client.available() > 0 && rxLen < sizeof(rx)) {
            rx[rxLen++] = static_cast<uint8_t>(client.read());
        }
        const size_t expectedLen = expectedSolarmanFrameLen(rx, rxLen);
        if (expectedLen > 0 && rxLen >= expectedLen && rx[expectedLen - 1] == 0x15) {
            break;
        }
        if (rxLen > 0 && !client.connected() && client.available() == 0) {
            break;
        }
        yield();
    }
    client.stop();

    Serial.printf("Deye raw response length: %u\n", static_cast<unsigned>(rxLen));
    if (rxLen > 0) {
        printHex("Deye RX", rx, rxLen);
    }
    if (rxLen == 0) {
        setError("read timeout");
        Serial.println("Deye poll failed: read timeout");
        return false;
    }
    if (rxLen >= sizeof(rx)) {
        setError("response too large");
        Serial.println("Deye poll failed: response too large");
        return false;
    }

    size_t modbusLen = 0;
    if (!_solarman.parseResponse(rx, rxLen, modbus, &modbusLen, sizeof(modbus))) {
        setError("parse failed");
        Serial.println("Deye poll failed: parse failed");
        return false;
    }

    Serial.printf("Deye Modbus response length: %u\n", static_cast<unsigned>(modbusLen));
    if (!applyRegisters(modbus, modbusLen, metrics)) {
        setError("register map failed");
        Serial.println("Deye poll failed: register map failed");
        return false;
    }

    metrics.valid = true;
    metrics.timestampMs = millis();
    metrics.errorCode = 0;
    setError("ok");
    Serial.printf(
        "Metrics: SOC=%.0f BAT_V=%.1f BAT_W=%.0f DAY=%.1fkWh LOAD=%.0f GRID=%.0f PV=%.0f\n",
        metrics.batterySoc,
        metrics.batteryVoltage,
        metrics.batteryPower,
        metrics.dailyGenerationKwh,
        metrics.loadPower,
        metrics.gridPower,
        metrics.pvTotalPower
    );
    return true;
}

const char* DeyeClient::lastError() const {
    return _lastError;
}

bool DeyeClient::applyRegisters(const uint8_t* modbus, size_t len, InverterMetrics& metrics) {
    if (modbus == nullptr || len < 5 || modbus[0] != _slaveId || modbus[1] != 0x03) {
        return false;
    }

    const uint8_t byteCount = modbus[2];
    if (byteCount + 5 > len || (byteCount % 2) != 0) {
        return false;
    }

    const size_t registerCount = byteCount / 2;
    if (registerCount > DeyeRegisters::COUNT) {
        return false;
    }

    for (size_t i = 0; i < registerCount; ++i) {
        _rawRegisters[i] = (static_cast<uint16_t>(modbus[3 + i * 2]) << 8) |
            modbus[4 + i * 2];
    }

    uint16_t u16 = 0;
    int16_t s16 = 0;

    metrics.batterySoc = readU16(DeyeRegisters::BATTERY_SOC, u16) ? static_cast<float>(u16) : MetricValue::Unknown;
    metrics.batteryVoltage = readU16(DeyeRegisters::BATTERY_VOLTAGE, u16) ? u16 * 0.01f : MetricValue::Unknown;
    metrics.batteryCurrent = readS16(DeyeRegisters::BATTERY_CURRENT, s16) ? s16 * 0.01f : MetricValue::Unknown;
    metrics.batteryPower = readS16(DeyeRegisters::BATTERY_POWER, s16) ? signedPower(s16) : MetricValue::Unknown;
    metrics.dailyGenerationKwh = readU16(DeyeRegisters::DAILY_GENERATION, u16) ? u16 * 0.1f : MetricValue::Unknown;
    metrics.loadPower = readU16(DeyeRegisters::LOAD_POWER, u16) ? static_cast<float>(u16) : MetricValue::Unknown;
    metrics.gridPower = readS16(DeyeRegisters::GRID_POWER, s16) ? signedPower(s16) : MetricValue::Unknown;
    metrics.pv1Power = readU16(DeyeRegisters::PV1_POWER, u16) ? static_cast<float>(u16) : MetricValue::Unknown;
    metrics.pv2Power = readU16(DeyeRegisters::PV2_POWER, u16) ? static_cast<float>(u16) : MetricValue::Unknown;

    if (isMetricKnown(metrics.pv1Power) && isMetricKnown(metrics.pv2Power)) {
        metrics.pvTotalPower = metrics.pv1Power + metrics.pv2Power;
    } else {
        metrics.pvTotalPower = MetricValue::Unknown;
    }

    return true;
}

bool DeyeClient::readU16(uint16_t reg, uint16_t& value) const {
    if (reg < DeyeRegisters::START) {
        return false;
    }
    const uint16_t offset = reg - DeyeRegisters::START;
    if (offset >= DeyeRegisters::COUNT) {
        return false;
    }
    value = _rawRegisters[offset];
    return true;
}

bool DeyeClient::readS16(uint16_t reg, int16_t& value) const {
    uint16_t raw = 0;
    if (!readU16(reg, raw)) {
        return false;
    }
    value = static_cast<int16_t>(raw);
    return true;
}

void DeyeClient::setError(const char* error) {
    _lastError = error;
}
