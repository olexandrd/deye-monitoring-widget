#include "ble_setup.h"

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

#include "config.h"

#ifndef BLE_SETUP_DEVICE_NAME
#define BLE_SETUP_DEVICE_NAME "Deye Monitor"
#endif
#ifndef BLE_SETUP_WINDOW_MS
#define BLE_SETUP_WINDOW_MS 180000UL
#endif

namespace {
constexpr const char* ServiceUuid = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr const char* RxUuid = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr const char* TxUuid = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr size_t NotifyChunkLen = 18;
constexpr const char* ReadyText = "WRITE start\r\n";
constexpr uint32_t ReadyReminderMs = 2000UL;

BleSetupMode* activeSetup = nullptr;
BLEServer* bleServer = nullptr;
BLECharacteristic* txCharacteristic = nullptr;

class SetupServerCallbacks : public BLEServerCallbacks {
public:
    void onConnect(BLEServer*) override {
        if (activeSetup != nullptr) {
            activeSetup->handleClientConnected();
        }
    }

    void onDisconnect(BLEServer*) override {
        if (activeSetup != nullptr) {
            activeSetup->handleClientDisconnected();
        }
    }
};

class SetupRxCallbacks : public BLECharacteristicCallbacks {
public:
    void onWrite(BLECharacteristic* characteristic) override {
        if (activeSetup == nullptr || characteristic == nullptr) {
            return;
        }

        activeSetup->handleRxWrite(String(characteristic->getValue().c_str()));
    }
};

String cleanedInput(const String& value) {
    String input = value;
    input.trim();
    return input;
}
}

void BleSetupMode::begin(const RuntimeConfig& currentConfig, uint32_t nowMs) {
    if (_active) {
        _deadlineMs = nowMs + BLE_SETUP_WINDOW_MS;
        sendText("\r\nEXTENDED\r\n");
        sendPrompt();
        return;
    }

    _sessionConfig = currentConfig;
    _saveConfig = {};
    _stage = Stage::Ready;
    _active = true;
    _connected = false;
    _savePending = false;
    _deadlineMs = nowMs + BLE_SETUP_WINDOW_MS;
    _stopAtMs = 0;
    _nextPromptReminderMs = 0;
    _promptReminderCount = 0;

    startBle();
    Serial.println("BLE setup: advertising started for 3 minutes");
}

void BleSetupMode::handle(uint32_t nowMs) {
    if (!_active) {
        return;
    }

    if (_stopAtMs != 0 && nowMs >= _stopAtMs) {
        stop();
        return;
    }

    if (nowMs >= _deadlineMs) {
        sendText("\r\nBLE setup timeout.\r\n");
        stop();
        return;
    }

    if (_connected && _stage == Stage::Ready && nowMs >= _nextPromptReminderMs) {
        sendText(ReadyText);
        _nextPromptReminderMs = nowMs + ReadyReminderMs;
        return;
    }

    if (_connected && _nextPromptReminderMs != 0 && nowMs >= _nextPromptReminderMs) {
        sendText("\r\n");
        sendPrompt();
        ++_promptReminderCount;
        _nextPromptReminderMs = nowMs + 3000UL;
    }
}

void BleSetupMode::stop() {
    if (!_active && bleServer == nullptr) {
        return;
    }

    Serial.println("BLE setup: stopped");
    BLEDevice::stopAdvertising();
    if (bleServer != nullptr && bleServer->getConnectedCount() > 0) {
        bleServer->disconnect(bleServer->getConnId());
    }
    setTxValue(ReadyText);
    activeSetup = nullptr;
    _active = false;
    _connected = false;
    _stage = Stage::Idle;
    _stopAtMs = 0;
    _nextPromptReminderMs = 0;
    _promptReminderCount = 0;
}

bool BleSetupMode::active() const {
    return _active;
}

bool BleSetupMode::takeSaveRequest(RuntimeConfig& outConfig) {
    if (!_savePending) {
        return false;
    }

    outConfig = _saveConfig;
    _savePending = false;
    return true;
}

void BleSetupMode::notifyWifiTestStarted() {
    _stage = Stage::WaitingForWiFiTest;
    sendText("\r\nWIFI TEST\r\n");
}

void BleSetupMode::notifyWifiTestResult(bool ok, const char* detail) {
    if (ok) {
        _stage = Stage::Done;
        sendText("\r\nWIFI OK\r\n");
        if (detail != nullptr && detail[0] != '\0') {
            sendText(String("IP ") + detail + "\r\n");
        }
        sendText("SAVED\r\n");
        scheduleStop(1200);
        return;
    }

    _stage = Stage::WiFiSsid;
    sendText("\r\nWIFI FAIL\r\n");
    if (detail != nullptr && detail[0] != '\0') {
        sendText(String("ERR ") + detail + "\r\n");
    }
    sendText("NOT SAVED\r\n");
    if (!_connected) {
        scheduleStop(500);
        return;
    }
    sendPrompt();
}

void BleSetupMode::handleClientConnected() {
    _connected = true;
    Serial.println("BLE setup: client connected");
    setTxValue(ReadyText);
    _stage = Stage::Ready;
    _nextPromptReminderMs = millis() + ReadyReminderMs;
    _promptReminderCount = 0;
}

void BleSetupMode::handleClientDisconnected() {
    _connected = false;
    Serial.println("BLE setup: client disconnected");
    _nextPromptReminderMs = 0;
    if (_active && _stage != Stage::WaitingForWiFiTest && _stage != Stage::Done) {
        BLEDevice::startAdvertising();
    }
}

void BleSetupMode::handleRxWrite(const String& value) {
    if (!_active) {
        return;
    }

    const String input = cleanedInput(value);
    Serial.printf("BLE setup RX: %u bytes\n", static_cast<unsigned>(input.length()));
    _nextPromptReminderMs = 0;
    _promptReminderCount = 0;

    if (inputIsStart(input)) {
        _stage = Stage::WiFiSsid;
        sendText("\r\nSETUP v3\r\n");
        sendText("empty keeps old\r\n");
        sendCurrentValue();
        sendPrompt();
        return;
    }

    if (_stage == Stage::Ready) {
        sendText(ReadyText);
        return;
    }

    advanceWithValue(input);
}

void BleSetupMode::startBle() {
    activeSetup = this;

    if (bleServer != nullptr) {
        setTxValue(ReadyText);
        BLEDevice::startAdvertising();
        return;
    }

    BLEDevice::init(BLE_SETUP_DEVICE_NAME);
    bleServer = BLEDevice::createServer();
    bleServer->setCallbacks(new SetupServerCallbacks());

    BLEService* service = bleServer->createService(ServiceUuid);
    txCharacteristic = service->createCharacteristic(
        TxUuid,
        BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
    );
    txCharacteristic->addDescriptor(new BLE2902());
    txCharacteristic->setValue(ReadyText);

    BLECharacteristic* rxCharacteristic = service->createCharacteristic(
        RxUuid,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
    );
    rxCharacteristic->setCallbacks(new SetupRxCallbacks());

    service->start();

    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(ServiceUuid);
    advertising->setScanResponse(true);
    BLEDevice::startAdvertising();
}

void BleSetupMode::sendText(const char* text) {
    if (text == nullptr || txCharacteristic == nullptr) {
        return;
    }

    Serial.print(text);
    const size_t len = strlen(text);
    txCharacteristic->setValue(text);

    if (!_connected) {
        return;
    }

    for (size_t offset = 0; offset < len; offset += NotifyChunkLen) {
        const size_t chunkLen = min(NotifyChunkLen, len - offset);
        txCharacteristic->setValue(reinterpret_cast<uint8_t*>(const_cast<char*>(text + offset)), chunkLen);
        txCharacteristic->notify();
        delay(8);
    }
}

void BleSetupMode::sendText(const String& text) {
    sendText(text.c_str());
}

void BleSetupMode::setTxValue(const char* text) {
    if (text == nullptr || txCharacteristic == nullptr) {
        return;
    }

    txCharacteristic->setValue(text);
}

void BleSetupMode::sendCurrentValue() {
    switch (_stage) {
        case Stage::WiFiSsid:
            sendText(String("CUR ") + _sessionConfig.wifiSsid + "\r\n");
            break;
        case Stage::WiFiPassword:
            sendText("CUR unchanged\r\n");
            break;
        case Stage::DeyeHost:
            sendText(String("CUR ") + _sessionConfig.deyeHost + "\r\n");
            break;
        case Stage::DeyeSerial:
            sendText(String("CUR ") + String(_sessionConfig.deyeSerial) + "\r\n");
            break;
        default:
            break;
    }
}

void BleSetupMode::sendPrompt() {
    if (!_active) {
        return;
    }

    String prompt;
    switch (_stage) {
        case Stage::WiFiSsid:
            prompt = "SSID?";
            break;
        case Stage::WiFiPassword:
            prompt = "PASS?";
            break;
        case Stage::DeyeHost:
            prompt = "DEYE HOST?";
            break;
        case Stage::DeyeSerial:
            prompt = "DEYE SERIAL?";
            break;
        case Stage::Save:
            prompt = "SAVE yes/no?";
            break;
        case Stage::WaitingForWiFiTest:
            prompt = "WIFI TEST...";
            break;
        default:
            return;
    }
    prompt += "\r\n";
    sendText(prompt);
}

void BleSetupMode::advanceWithValue(const String& input) {
    switch (_stage) {
        case Stage::WiFiSsid:
            if (!inputIsSkip(input) && !runtimeConfigCopyString(
                    _sessionConfig.wifiSsid,
                    sizeof(_sessionConfig.wifiSsid),
                    input
                )) {
                sendText("\r\nSSID TOO LONG\r\n");
                sendPrompt();
                return;
            }
            _stage = Stage::WiFiPassword;
            sendText("\r\n");
            sendCurrentValue();
            sendPrompt();
            return;

        case Stage::WiFiPassword:
            if (!inputIsSkip(input) && !runtimeConfigCopyString(
                    _sessionConfig.wifiPassword,
                    sizeof(_sessionConfig.wifiPassword),
                    input
                )) {
                sendText("\r\nPASS TOO LONG\r\n");
                sendPrompt();
                return;
            }
            _stage = Stage::DeyeHost;
            sendText("\r\n");
            sendCurrentValue();
            sendPrompt();
            return;

        case Stage::DeyeHost:
            if (!inputIsSkip(input)) {
                if (input.length() == 0 || input.indexOf(' ') >= 0 ||
                    !runtimeConfigCopyString(
                        _sessionConfig.deyeHost,
                        sizeof(_sessionConfig.deyeHost),
                        input
                    )) {
                    sendText("\r\nHOST INVALID\r\n");
                    sendPrompt();
                    return;
                }
            }
            _stage = Stage::DeyeSerial;
            sendText("\r\n");
            sendCurrentValue();
            sendPrompt();
            return;

        case Stage::DeyeSerial:
            if (!inputIsSkip(input)) {
                uint32_t serial = 0;
                if (!parseSerial(input, serial)) {
                    sendText("\r\nSERIAL INVALID\r\n");
                    sendPrompt();
                    return;
                }
                _sessionConfig.deyeSerial = serial;
            }
            _stage = Stage::Save;
            sendText("\r\n");
            sendPrompt();
            return;

        case Stage::Save:
            if (inputIsYes(input)) {
                _saveConfig = _sessionConfig;
                _savePending = true;
                _stage = Stage::WaitingForWiFiTest;
                sendText("\r\nSAVE REQ\r\n");
                return;
            }
            if (inputIsNo(input)) {
                sendText("\r\nNOT SAVED\r\n");
                _stage = Stage::Done;
                scheduleStop(1200);
                return;
            }
            sendText("\r\nYES OR NO\r\n");
            sendPrompt();
            return;

        case Stage::WaitingForWiFiTest:
            sendText("\r\nBUSY\r\n");
            return;

        default:
            return;
    }
}

bool BleSetupMode::inputIsStart(const String& input) const {
    return input.equalsIgnoreCase("start") || input == "?";
}

bool BleSetupMode::inputIsSkip(const String& input) const {
    return input.length() == 0 || input.equalsIgnoreCase("skip");
}

bool BleSetupMode::inputIsYes(const String& input) const {
    return input.equalsIgnoreCase("yes") || input.equalsIgnoreCase("y");
}

bool BleSetupMode::inputIsNo(const String& input) const {
    return input.equalsIgnoreCase("no") || input.equalsIgnoreCase("n");
}

bool BleSetupMode::parseSerial(const String& input, uint32_t& serial) const {
    if (input.length() == 0) {
        return false;
    }

    uint64_t value = 0;
    for (size_t i = 0; i < input.length(); ++i) {
        const char c = input[i];
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + static_cast<uint8_t>(c - '0');
        if (value > UINT32_MAX) {
            return false;
        }
    }

    serial = static_cast<uint32_t>(value);
    return serial > 0;
}

void BleSetupMode::scheduleStop(uint32_t delayMs) {
    _stopAtMs = millis() + delayMs;
}
