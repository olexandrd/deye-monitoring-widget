#pragma once

#include <Arduino.h>

constexpr size_t RuntimeConfigWiFiSsidLen = 33;
constexpr size_t RuntimeConfigWiFiPasswordLen = 65;
constexpr size_t RuntimeConfigDeyeHostLen = 64;

struct RuntimeConfig {
    char wifiSsid[RuntimeConfigWiFiSsidLen];
    char wifiPassword[RuntimeConfigWiFiPasswordLen];
    char deyeHost[RuntimeConfigDeyeHostLen];
    uint32_t deyeSerial;
};

void runtimeConfigLoad(RuntimeConfig& config);
bool runtimeConfigSave(const RuntimeConfig& config);
void runtimeConfigApplyDefaults(RuntimeConfig& config);
void runtimeConfigPrint(const RuntimeConfig& config);
bool runtimeConfigCopyString(char* out, size_t outLen, const String& value);
