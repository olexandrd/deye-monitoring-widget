#include "runtime_config.h"

#include <Preferences.h>
#include <stddef.h>
#include <string.h>

#include "config.h"

namespace {
constexpr const char* NvsNamespace = "deye-mon";
constexpr const char* KeyWiFiSsid = "ssid";
constexpr const char* KeyWiFiPassword = "pass";
constexpr const char* KeyDeyeHost = "deye_host";
constexpr const char* KeyDeyeSerial = "deye_ser";
constexpr const char* KeyConfigRecord = "config_v1";
constexpr uint32_t ConfigRecordMagic = 0x44455945UL;
constexpr uint16_t ConfigRecordVersion = 1;

struct __attribute__((packed)) StoredRuntimeConfig {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    char wifiSsid[RuntimeConfigWiFiSsidLen];
    char wifiPassword[RuntimeConfigWiFiPasswordLen];
    char deyeHost[RuntimeConfigDeyeHostLen];
    uint32_t deyeSerial;
    uint32_t checksum;
};

void copyString(char* out, size_t outLen, const char* value) {
    if (out == nullptr || outLen == 0) {
        return;
    }

    if (value == nullptr) {
        value = "";
    }

    strlcpy(out, value, outLen);
}

void loadStringIfStored(Preferences& preferences, const char* key, char* out, size_t outLen) {
    if (!preferences.isKey(key)) {
        return;
    }

    String value = preferences.getString(key, "");
    copyString(out, outLen, value.c_str());
}

uint32_t configRecordChecksum(const StoredRuntimeConfig& record) {
    constexpr uint32_t FnvOffsetBasis = 2166136261UL;
    constexpr uint32_t FnvPrime = 16777619UL;
    const auto* bytes = reinterpret_cast<const uint8_t*>(&record);
    uint32_t checksum = FnvOffsetBasis;
    for (size_t i = 0; i < offsetof(StoredRuntimeConfig, checksum); ++i) {
        checksum ^= bytes[i];
        checksum *= FnvPrime;
    }
    return checksum;
}

bool hasTerminator(const char* value, size_t len) {
    return memchr(value, '\0', len) != nullptr;
}

bool configRecordIsValid(const StoredRuntimeConfig& record) {
    return record.magic == ConfigRecordMagic &&
        record.version == ConfigRecordVersion &&
        record.size == sizeof(record) &&
        hasTerminator(record.wifiSsid, sizeof(record.wifiSsid)) &&
        hasTerminator(record.wifiPassword, sizeof(record.wifiPassword)) &&
        hasTerminator(record.deyeHost, sizeof(record.deyeHost)) &&
        record.checksum == configRecordChecksum(record);
}

bool loadConfigRecord(Preferences& preferences, RuntimeConfig& config) {
    if (preferences.getBytesLength(KeyConfigRecord) != sizeof(StoredRuntimeConfig)) {
        return false;
    }

    StoredRuntimeConfig record = {};
    if (preferences.getBytes(KeyConfigRecord, &record, sizeof(record)) != sizeof(record) ||
        !configRecordIsValid(record)) {
        Serial.println("Runtime config: stored record invalid, trying legacy keys");
        return false;
    }

    copyString(config.wifiSsid, sizeof(config.wifiSsid), record.wifiSsid);
    copyString(config.wifiPassword, sizeof(config.wifiPassword), record.wifiPassword);
    copyString(config.deyeHost, sizeof(config.deyeHost), record.deyeHost);
    config.deyeSerial = record.deyeSerial;
    return true;
}

StoredRuntimeConfig makeConfigRecord(const RuntimeConfig& config) {
    StoredRuntimeConfig record = {};
    record.magic = ConfigRecordMagic;
    record.version = ConfigRecordVersion;
    record.size = sizeof(record);
    copyString(record.wifiSsid, sizeof(record.wifiSsid), config.wifiSsid);
    copyString(record.wifiPassword, sizeof(record.wifiPassword), config.wifiPassword);
    copyString(record.deyeHost, sizeof(record.deyeHost), config.deyeHost);
    record.deyeSerial = config.deyeSerial;
    record.checksum = configRecordChecksum(record);
    return record;
}
}

void runtimeConfigApplyDefaults(RuntimeConfig& config) {
    copyString(config.wifiSsid, sizeof(config.wifiSsid), WIFI_SSID);
    copyString(config.wifiPassword, sizeof(config.wifiPassword), WIFI_PASSWORD);
    copyString(config.deyeHost, sizeof(config.deyeHost), DEYE_LOGGER_IP);
    config.deyeSerial = DEYE_LOGGER_SERIAL;
}

void runtimeConfigLoad(RuntimeConfig& config) {
    runtimeConfigApplyDefaults(config);

    Preferences preferences;
    if (!preferences.begin(NvsNamespace, true)) {
        Serial.println("Runtime config: NVS open failed, using defaults");
        return;
    }

    if (!loadConfigRecord(preferences, config)) {
        loadStringIfStored(preferences, KeyWiFiSsid, config.wifiSsid, sizeof(config.wifiSsid));
        loadStringIfStored(preferences, KeyWiFiPassword, config.wifiPassword, sizeof(config.wifiPassword));
        loadStringIfStored(preferences, KeyDeyeHost, config.deyeHost, sizeof(config.deyeHost));
        if (preferences.isKey(KeyDeyeSerial)) {
            config.deyeSerial = preferences.getULong(KeyDeyeSerial, config.deyeSerial);
        }
    }
    preferences.end();
}

bool runtimeConfigSave(const RuntimeConfig& config) {
    Preferences preferences;
    if (!preferences.begin(NvsNamespace, false)) {
        Serial.println("Runtime config: NVS write open failed");
        return false;
    }

    const StoredRuntimeConfig record = makeConfigRecord(config);
    const bool ok = preferences.putBytes(KeyConfigRecord, &record, sizeof(record)) == sizeof(record);
    preferences.end();

    Serial.printf("Runtime config: save %s\n", ok ? "ok" : "failed");
    return ok;
}

void runtimeConfigPrint(const RuntimeConfig& config) {
    Serial.printf("WiFi SSID: %s\n", config.wifiSsid);
    Serial.printf("Deye logger: %s:%u\n", config.deyeHost, DEYE_LOGGER_PORT);
    Serial.printf("Deye logger serial: %lu\n", static_cast<unsigned long>(config.deyeSerial));
}

bool runtimeConfigCopyString(char* out, size_t outLen, const String& value) {
    if (out == nullptr || outLen == 0 || value.length() >= outLen) {
        return false;
    }

    copyString(out, outLen, value.c_str());
    return true;
}
