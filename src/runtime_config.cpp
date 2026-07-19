#include "runtime_config.h"

#include <Preferences.h>

#include "config.h"

namespace {
constexpr const char* NvsNamespace = "deye-mon";
constexpr const char* KeyWiFiSsid = "ssid";
constexpr const char* KeyWiFiPassword = "pass";
constexpr const char* KeyDeyeHost = "deye_host";
constexpr const char* KeyDeyeSerial = "deye_ser";

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

    loadStringIfStored(preferences, KeyWiFiSsid, config.wifiSsid, sizeof(config.wifiSsid));
    loadStringIfStored(preferences, KeyWiFiPassword, config.wifiPassword, sizeof(config.wifiPassword));
    loadStringIfStored(preferences, KeyDeyeHost, config.deyeHost, sizeof(config.deyeHost));
    if (preferences.isKey(KeyDeyeSerial)) {
        config.deyeSerial = preferences.getULong(KeyDeyeSerial, config.deyeSerial);
    }
    preferences.end();
}

bool runtimeConfigSave(const RuntimeConfig& config) {
    Preferences preferences;
    if (!preferences.begin(NvsNamespace, false)) {
        Serial.println("Runtime config: NVS write open failed");
        return false;
    }

    const size_t ssidSaved = preferences.putString(KeyWiFiSsid, config.wifiSsid);
    const size_t passwordSaved = preferences.putString(KeyWiFiPassword, config.wifiPassword);
    const size_t hostSaved = preferences.putString(KeyDeyeHost, config.deyeHost);
    const size_t serialSaved = preferences.putULong(KeyDeyeSerial, config.deyeSerial);
    const bool ok =
        ssidSaved > 0 &&
        (passwordSaved > 0 || config.wifiPassword[0] == '\0') &&
        hostSaved > 0 &&
        serialSaved > 0;
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
