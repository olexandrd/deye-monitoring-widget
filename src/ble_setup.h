#pragma once

#include <Arduino.h>

#include "runtime_config.h"

class BleSetupMode {
public:
    void begin(const RuntimeConfig& currentConfig, uint32_t nowMs);
    void handle(uint32_t nowMs);
    void stop();
    bool active() const;
    bool takeSaveRequest(RuntimeConfig& outConfig);
    void notifyWifiTestStarted();
    void notifyWifiTestResult(bool ok, const char* detail);

    void handleClientConnected();
    void handleClientDisconnected();
    void handleRxWrite(const uint8_t* data, size_t len);

private:
    enum class Stage {
        Idle,
        Ready,
        WiFiSsid,
        WiFiPassword,
        DeyeHost,
        DeyeSerial,
        Save,
        WaitingForWiFiTest,
        Done
    };

    void startBle();
    void sendText(const char* text);
    void sendText(const String& text);
    void setTxValue(const char* text);
    void sendPrompt();
    void sendCurrentValue();
    void processRxInput(uint32_t nowMs);
    void resetRxInput();
    void advanceWithValue(const String& input);
    bool inputIsStart(const String& input) const;
    bool inputIsYes(const String& input) const;
    bool inputIsNo(const String& input) const;
    bool parseSerial(const String& input, uint32_t& serial) const;
    void scheduleStop(uint32_t delayMs);

    RuntimeConfig _sessionConfig = {};
    RuntimeConfig _saveConfig = {};
    Stage _stage = Stage::Idle;
    bool _active = false;
    bool _connected = false;
    bool _savePending = false;
    uint32_t _deadlineMs = 0;
    uint32_t _stopAtMs = 0;
    uint32_t _nextPromptReminderMs = 0;
    uint8_t _promptReminderCount = 0;
    char _rxBuffer[RuntimeConfigWiFiPasswordLen] = {};
    size_t _rxLength = 0;
    bool _rxLineReady = false;
    bool _rxOverflow = false;
    uint32_t _rxLastWriteMs = 0;
    portMUX_TYPE _rxMux = portMUX_INITIALIZER_UNLOCKED;
};
