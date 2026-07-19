#include <Arduino.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

#include "ble_setup.h"
#include "config.h"
#include "deye_client.h"
#include "display.h"
#include "metrics.h"
#include "runtime_config.h"

#ifndef DISPLAY_BRIGHTNESS_HIGH_PERCENT
#define DISPLAY_BRIGHTNESS_HIGH_PERCENT 50
#endif
#ifndef DISPLAY_BRIGHTNESS_DIM_PERCENT
#define DISPLAY_BRIGHTNESS_DIM_PERCENT 20
#endif
#ifndef DISPLAY_DIM_AFTER_MS
#define DISPLAY_DIM_AFTER_MS 180000UL
#endif
#ifndef DISPLAY_SLEEP_AFTER_MS
#define DISPLAY_SLEEP_AFTER_MS 600000UL
#endif

#ifndef WAKE_BUTTON_ENABLED
#define WAKE_BUTTON_ENABLED 0
#endif
#ifndef WAKE_BUTTON_PIN
#define WAKE_BUTTON_PIN 1
#endif
#ifndef WAKE_BUTTON_ACTIVE_LOW
#define WAKE_BUTTON_ACTIVE_LOW 1
#endif
#ifndef WAKE_BUTTON_DEBOUNCE_MS
#define WAKE_BUTTON_DEBOUNCE_MS 50
#endif
#ifndef BLE_SETUP_ENABLED
#define BLE_SETUP_ENABLED 1
#endif
#ifndef BLE_SETUP_HOLD_MS
#define BLE_SETUP_HOLD_MS 5000UL
#endif
#ifndef BLE_WIFI_TEST_TIMEOUT_MS
#define BLE_WIFI_TEST_TIMEOUT_MS 20000UL
#endif

#ifndef MH_CD42_KEEPALIVE_ENABLED
#define MH_CD42_KEEPALIVE_ENABLED 1
#endif
#ifndef MH_CD42_KEEPALIVE_PIN
#define MH_CD42_KEEPALIVE_PIN 5
#endif
#ifndef MH_CD42_KEEPALIVE_INTERVAL_MS
#define MH_CD42_KEEPALIVE_INTERVAL_MS 15000UL
#endif
#ifndef MH_CD42_KEEPALIVE_PULSE_MS
#define MH_CD42_KEEPALIVE_PULSE_MS 250UL
#endif

#ifndef SUPPLY_BATTERY_ADC_ENABLED
#define SUPPLY_BATTERY_ADC_ENABLED 1
#endif
#ifndef SUPPLY_BATTERY_ADC_PIN
#define SUPPLY_BATTERY_ADC_PIN 0
#endif
#ifndef SUPPLY_BATTERY_R_TOP_OHMS
#define SUPPLY_BATTERY_R_TOP_OHMS 680000.0f
#endif
#ifndef SUPPLY_BATTERY_R_BOTTOM_OHMS
#define SUPPLY_BATTERY_R_BOTTOM_OHMS 470000.0f
#endif
#ifndef SUPPLY_BATTERY_EMPTY_MV
#define SUPPLY_BATTERY_EMPTY_MV 3200.0f
#endif
#ifndef SUPPLY_BATTERY_FULL_MV
#define SUPPLY_BATTERY_FULL_MV 4200.0f
#endif
#ifndef SUPPLY_BATTERY_SAMPLE_COUNT
#define SUPPLY_BATTERY_SAMPLE_COUNT 16
#endif
#ifndef SUPPLY_BATTERY_READ_INTERVAL_MS
#define SUPPLY_BATTERY_READ_INTERVAL_MS 30000UL
#endif

namespace {
constexpr uint32_t WiFiRetryMinMs = 5000;
constexpr uint32_t WiFiRetryMaxMs = 30000;

InverterMetrics metrics;
RuntimeConfig runtimeConfig;
RuntimeConfig pendingBleConfig;
DeyeClient deyeClient(
    DEYE_LOGGER_IP,
    DEYE_LOGGER_PORT,
    DEYE_LOGGER_SERIAL,
    DEYE_MODBUS_SLAVE_ID
);
BleSetupMode bleSetup;

SupplyBatteryStatus supplyBattery = {false, NAN, 0};
AppState appState = AppState::Boot;
uint32_t nextPollMs = 0;
uint32_t nextWiFiRetryMs = 0;
uint32_t wifiRetryDelayMs = WiFiRetryMinMs;
bool wasWifiConnected = false;
uint32_t lastUserActivityMs = 0;
uint32_t nextSupplyBatteryReadMs = 0;
bool displayDimmed = false;
bool wakeButtonStablePressed = false;
bool wakeButtonLastRawPressed = false;
uint32_t wakeButtonRawChangedMs = 0;
uint32_t wakeButtonPressedStartedMs = 0;
bool wakeButtonLongPressHandled = false;
uint32_t nextMhCd42KeepAliveMs = 0;
uint32_t mhCd42KeepAlivePulseStartedMs = 0;
bool mhCd42KeepAlivePulseActive = false;
bool wifiConfigTestActive = false;
uint32_t wifiConfigTestDeadlineMs = 0;

void showState(AppState state, const char* detail = nullptr) {
    appState = state;
    displayStatus(appState, metrics, detail, supplyBattery);
}

void applyDeyeRuntimeConfig() {
    deyeClient.configure(
        runtimeConfig.deyeHost,
        DEYE_LOGGER_PORT,
        runtimeConfig.deyeSerial,
        DEYE_MODBUS_SLAVE_ID
    );
}

float clampFloat(float value, float minValue, float maxValue) {
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

bool wakeButtonPressed() {
#if WAKE_BUTTON_ENABLED
    const int level = digitalRead(WAKE_BUTTON_PIN);
#if WAKE_BUTTON_ACTIVE_LOW
    return level == LOW;
#else
    return level == HIGH;
#endif
#else
    return false;
#endif
}

void setupWakeButton() {
#if WAKE_BUTTON_ENABLED
#if WAKE_BUTTON_ACTIVE_LOW
    pinMode(WAKE_BUTTON_PIN, INPUT_PULLUP);
#else
    pinMode(WAKE_BUTTON_PIN, INPUT_PULLDOWN);
#endif
    wakeButtonStablePressed = wakeButtonPressed();
    wakeButtonLastRawPressed = wakeButtonStablePressed;
#endif
}

void setupSupplyBatteryMonitor() {
#if SUPPLY_BATTERY_ADC_ENABLED
    analogReadResolution(12);
    analogSetPinAttenuation(SUPPLY_BATTERY_ADC_PIN, ADC_11db);
#endif
}

void updateSupplyBattery(uint32_t nowMs, bool force = false) {
#if SUPPLY_BATTERY_ADC_ENABLED
    if (!force && nowMs < nextSupplyBatteryReadMs) {
        return;
    }

    uint16_t sampleCount = SUPPLY_BATTERY_SAMPLE_COUNT;
    if (sampleCount == 0) {
        sampleCount = 1;
    }

    uint32_t adcMilliVoltsTotal = 0;
    for (uint16_t i = 0; i < sampleCount; ++i) {
        adcMilliVoltsTotal += analogReadMilliVolts(SUPPLY_BATTERY_ADC_PIN);
        delay(2);
    }

    const float adcMilliVolts = static_cast<float>(adcMilliVoltsTotal) / sampleCount;
    const float dividerRatio =
        (SUPPLY_BATTERY_R_TOP_OHMS + SUPPLY_BATTERY_R_BOTTOM_OHMS) / SUPPLY_BATTERY_R_BOTTOM_OHMS;
    const float batteryMilliVolts = adcMilliVolts * dividerRatio;
    const float rawPercent =
        ((batteryMilliVolts - SUPPLY_BATTERY_EMPTY_MV) /
         (SUPPLY_BATTERY_FULL_MV - SUPPLY_BATTERY_EMPTY_MV)) *
        100.0f;

    supplyBattery.valid = true;
    supplyBattery.voltage = batteryMilliVolts / 1000.0f;
    supplyBattery.chargePercent = static_cast<uint8_t>(clampFloat(rawPercent, 0.0f, 100.0f) + 0.5f);
    nextSupplyBatteryReadMs = nowMs + SUPPLY_BATTERY_READ_INTERVAL_MS;

    Serial.printf(
        "Supply battery: ADC=%.0fmV BAT=%.2fV CHG=%u%%\n",
        adcMilliVolts,
        supplyBattery.voltage,
        supplyBattery.chargePercent
    );
#else
    supplyBattery.valid = false;
    supplyBattery.voltage = NAN;
    supplyBattery.chargePercent = 0;
#endif
}

void markUserActivity(uint32_t nowMs) {
    lastUserActivityMs = nowMs;
    nextPollMs = nowMs;
    displayWake();
    displaySetBrightnessPercent(DISPLAY_BRIGHTNESS_HIGH_PERCENT);
    displayStatus(appState, metrics, nullptr, supplyBattery);

    if (displayDimmed) {
        displayDimmed = false;
        Serial.println("Display brightness restored by button");
    } else {
        Serial.println("User activity timer reset by button");
    }
    Serial.println("Deye poll requested by wake button");
}

void startBleSetup(uint32_t nowMs) {
#if BLE_SETUP_ENABLED
    markUserActivity(nowMs);
    showState(AppState::BleSetup, "setup");
    bleSetup.begin(runtimeConfig, nowMs);
#else
    (void)nowMs;
    Serial.println("BLE setup disabled");
#endif
}

void handleWakeButton(uint32_t nowMs) {
#if WAKE_BUTTON_ENABLED
    const bool rawPressed = wakeButtonPressed();
    if (rawPressed != wakeButtonLastRawPressed) {
        wakeButtonLastRawPressed = rawPressed;
        wakeButtonRawChangedMs = nowMs;
    }

    if (nowMs - wakeButtonRawChangedMs < WAKE_BUTTON_DEBOUNCE_MS) {
        return;
    }

    if (wakeButtonStablePressed != rawPressed) {
        wakeButtonStablePressed = rawPressed;
        if (wakeButtonStablePressed) {
            Serial.println("Wake button pressed");
            wakeButtonPressedStartedMs = nowMs;
            wakeButtonLongPressHandled = false;
            markUserActivity(nowMs);
        } else {
            wakeButtonPressedStartedMs = 0;
            wakeButtonLongPressHandled = false;
        }
    }

#if BLE_SETUP_ENABLED
    if (wakeButtonStablePressed && !wakeButtonLongPressHandled &&
        nowMs - wakeButtonPressedStartedMs >= BLE_SETUP_HOLD_MS) {
        wakeButtonLongPressHandled = true;
        Serial.println("Wake button long press: starting BLE setup");
        startBleSetup(nowMs);
    }
#endif
#endif
}

void configureWakeButtonForDeepSleep() {
#if WAKE_BUTTON_ENABLED
    const gpio_num_t pin = static_cast<gpio_num_t>(WAKE_BUTTON_PIN);
#if WAKE_BUTTON_ACTIVE_LOW
    gpio_pullup_en(pin);
    gpio_pulldown_dis(pin);
    esp_deep_sleep_enable_gpio_wakeup(1ULL << WAKE_BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
#else
    gpio_pulldown_en(pin);
    gpio_pullup_dis(pin);
    esp_deep_sleep_enable_gpio_wakeup(1ULL << WAKE_BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_HIGH);
#endif
#else
    Serial.println("ESP GPIO wake disabled; wake by reset, power cycle, or MH-CD42 button");
#endif
}

void releaseMhCd42KeepAlivePin() {
#if MH_CD42_KEEPALIVE_ENABLED
    gpio_set_level(static_cast<gpio_num_t>(MH_CD42_KEEPALIVE_PIN), 1);
    mhCd42KeepAlivePulseActive = false;
#endif
}

void setupMhCd42KeepAlive(uint32_t nowMs) {
#if MH_CD42_KEEPALIVE_ENABLED
    const gpio_num_t pin = static_cast<gpio_num_t>(MH_CD42_KEEPALIVE_PIN);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(pin, 1);
    nextMhCd42KeepAliveMs = nowMs + MH_CD42_KEEPALIVE_INTERVAL_MS;
#endif
}

void handleMhCd42KeepAlive(uint32_t nowMs) {
#if MH_CD42_KEEPALIVE_ENABLED
    const gpio_num_t pin = static_cast<gpio_num_t>(MH_CD42_KEEPALIVE_PIN);

    if (mhCd42KeepAlivePulseActive) {
        if (nowMs - mhCd42KeepAlivePulseStartedMs >= MH_CD42_KEEPALIVE_PULSE_MS) {
            gpio_set_level(pin, 1);
            mhCd42KeepAlivePulseActive = false;
            nextMhCd42KeepAliveMs = nowMs + MH_CD42_KEEPALIVE_INTERVAL_MS;
            Serial.println("MH-CD42 KEY released");
        }
        return;
    }

    if (nowMs >= nextMhCd42KeepAliveMs) {
        gpio_set_level(pin, 0);
        mhCd42KeepAlivePulseStartedMs = nowMs;
        mhCd42KeepAlivePulseActive = true;
        Serial.println("MH-CD42 KEY keep-alive pulse");
    }
#endif
}

void enterDeepSleep(uint32_t nowMs) {
    Serial.printf("Entering deep sleep after %lu ms of activity\n", static_cast<unsigned long>(nowMs - lastUserActivityMs));
    showState(AppState::Stale, "sleep");
    delay(250);

    releaseMhCd42KeepAlivePin();
    bleSetup.stop();
    displaySleep();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    configureWakeButtonForDeepSleep();
    Serial.flush();
    esp_deep_sleep_start();
}

void handleDisplayPower(uint32_t nowMs) {
    const uint32_t activeMs = nowMs - lastUserActivityMs;

    if (activeMs >= DISPLAY_SLEEP_AFTER_MS) {
        enterDeepSleep(nowMs);
        return;
    }

    if (!displayDimmed && activeMs >= DISPLAY_DIM_AFTER_MS) {
        displaySetBrightnessPercent(DISPLAY_BRIGHTNESS_DIM_PERCENT);
        displayDimmed = true;
        Serial.printf("Display brightness dimmed to %u%%\n", DISPLAY_BRIGHTNESS_DIM_PERCENT);
    }
}

void startWiFiConnect(uint32_t nowMs, const RuntimeConfig& config, const char* detail = "connect") {
    Serial.printf("WiFi connecting to SSID: %s\n", config.wifiSsid);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false);
    WiFi.begin(config.wifiSsid, config.wifiPassword);
    nextWiFiRetryMs = nowMs + wifiRetryDelayMs;
    showState(AppState::WiFiConnecting, detail);
}

void startWiFiConnect(uint32_t nowMs) {
    startWiFiConnect(nowMs, runtimeConfig);
}

void startBleWiFiConfigTest(uint32_t nowMs, const RuntimeConfig& config) {
    pendingBleConfig = config;
    wifiConfigTestActive = true;
    wifiConfigTestDeadlineMs = nowMs + BLE_WIFI_TEST_TIMEOUT_MS;
    wasWifiConnected = false;

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false);
    delay(100);
    WiFi.begin(pendingBleConfig.wifiSsid, pendingBleConfig.wifiPassword);

    showState(AppState::WiFiConnecting, "test");
    bleSetup.notifyWifiTestStarted();
    Serial.printf("BLE setup: testing WiFi SSID %s\n", pendingBleConfig.wifiSsid);
}

bool handleBleWiFiConfigTest(uint32_t nowMs) {
    if (!wifiConfigTestActive) {
        return false;
    }

    const wl_status_t status = WiFi.status();
    if (status == WL_CONNECTED) {
        const String ip = WiFi.localIP().toString();
        if (runtimeConfigSave(pendingBleConfig)) {
            runtimeConfig = pendingBleConfig;
            applyDeyeRuntimeConfig();
            wifiConfigTestActive = false;
            wasWifiConnected = true;
            wifiRetryDelayMs = WiFiRetryMinMs;
            nextPollMs = nowMs;
            showState(AppState::Ok, ip.c_str());
            bleSetup.notifyWifiTestResult(true, ip.c_str());
            Serial.print("BLE setup: WiFi test OK, ESP32 IP: ");
            Serial.println(ip);
        } else {
            wifiConfigTestActive = false;
            showState(AppState::WiFiError, "NVS");
            bleSetup.notifyWifiTestResult(false, "NVS save failed");
            startWiFiConnect(nowMs);
        }
        return true;
    }

    if (status == WL_CONNECT_FAILED || nowMs >= wifiConfigTestDeadlineMs) {
        wifiConfigTestActive = false;
        wasWifiConnected = false;
        showState(AppState::WiFiError, "test");
        bleSetup.notifyWifiTestResult(false, "cannot connect");
        Serial.println("BLE setup: WiFi test failed; reverting active config");
        startWiFiConnect(nowMs);
        return true;
    }

    showState(AppState::WiFiConnecting, "test");
    return true;
}

void handleWiFi(uint32_t nowMs) {
    const wl_status_t status = WiFi.status();
    const bool connected = status == WL_CONNECTED;

    if (connected) {
        if (!wasWifiConnected) {
            wasWifiConnected = true;
            wifiRetryDelayMs = WiFiRetryMinMs;
            Serial.print("WiFi connected, ESP32 IP: ");
            Serial.println(WiFi.localIP());
            showState(metrics.valid ? AppState::Stale : AppState::Ok, WiFi.localIP().toString().c_str());
            nextPollMs = nowMs;
        }
        return;
    }

    if (wasWifiConnected) {
        wasWifiConnected = false;
        Serial.println("WiFi disconnected");
        showState(AppState::WiFiError, "lost");
        nextWiFiRetryMs = nowMs + wifiRetryDelayMs;
    }

    if (nowMs >= nextWiFiRetryMs) {
        Serial.println("WiFi retry");
        WiFi.disconnect(false);
        WiFi.begin(runtimeConfig.wifiSsid, runtimeConfig.wifiPassword);
        showState(AppState::WiFiConnecting, "retry");
        nextWiFiRetryMs = nowMs + wifiRetryDelayMs;
        wifiRetryDelayMs = min(wifiRetryDelayMs * 2, WiFiRetryMaxMs);
    }
}

void pollDeye(uint32_t nowMs) {
    if (nowMs < nextPollMs) {
        return;
    }

    nextPollMs = nowMs + POLL_INTERVAL_MS;

#if USE_FAKE_DATA
    Serial.println("Fake data poll start");
    updateFakeMetrics(metrics, nowMs);
    Serial.printf(
        "Fake metrics: SOC=%.0f BAT_V=%.1f BAT_W=%.0f DAY=%.1fkWh LOAD=%.0f GRID=%.0f PV=%.0f\n",
        metrics.batterySoc,
        metrics.batteryVoltage,
        metrics.batteryPower,
        metrics.dailyGenerationKwh,
        metrics.loadPower,
        metrics.gridPower,
        metrics.pvTotalPower
    );
    showState(WiFi.status() == WL_CONNECTED ? AppState::Ok : AppState::WiFiError, "fake");
#else
    if (WiFi.status() != WL_CONNECTED) {
        showState(AppState::WiFiError, "retry");
        return;
    }

    showState(AppState::DeyePolling);
    if (deyeClient.poll(metrics)) {
        Serial.println("Deye poll success");
        showState(AppState::Ok);
    } else {
        metrics.errorCode = -1;
        Serial.printf("Deye poll failed: %s\n", deyeClient.lastError());
        showState(metrics.valid ? AppState::Stale : AppState::DeyeError, deyeClient.lastError());
    }
#endif
}
}

void setup() {
    Serial.begin(115200);
    delay(100);

    WiFi.persistent(false);
    runtimeConfigLoad(runtimeConfig);
    applyDeyeRuntimeConfig();
    setupWakeButton();
    setupSupplyBatteryMonitor();
    resetMetrics(metrics);
    displayBegin();
    displaySetBrightnessPercent(DISPLAY_BRIGHTNESS_HIGH_PERCENT);
    lastUserActivityMs = millis();
    wakeButtonRawChangedMs = lastUserActivityMs;
    setupMhCd42KeepAlive(lastUserActivityMs);
    updateSupplyBattery(lastUserActivityMs, true);
    showState(AppState::Boot);

    Serial.println();
    Serial.println("ESP32-C3 Deye Local Monitor boot");
    Serial.printf("Wakeup cause: %d\n", static_cast<int>(esp_sleep_get_wakeup_cause()));
    runtimeConfigPrint(runtimeConfig);
    Serial.printf("OLED SDA=%d SCL=%d address=0x%02X\n", OLED_SDA_PIN, OLED_SCL_PIN, OLED_I2C_ADDRESS);
    Serial.printf("Display brightness: %u%% -> %u%% after %lu ms, sleep after %lu ms\n",
        DISPLAY_BRIGHTNESS_HIGH_PERCENT,
        DISPLAY_BRIGHTNESS_DIM_PERCENT,
        static_cast<unsigned long>(DISPLAY_DIM_AFTER_MS),
        static_cast<unsigned long>(DISPLAY_SLEEP_AFTER_MS)
    );
#if WAKE_BUTTON_ENABLED
    Serial.printf("Wake button GPIO=%d active=%s\n", WAKE_BUTTON_PIN, WAKE_BUTTON_ACTIVE_LOW ? "LOW" : "HIGH");
    Serial.printf("BLE setup long press: %s, hold %lu ms\n",
        BLE_SETUP_ENABLED ? "enabled" : "disabled",
        static_cast<unsigned long>(BLE_SETUP_HOLD_MS)
    );
#else
    Serial.println("Wake button GPIO disabled; relying on reset/power-cycle/MH-CD42 button");
#endif
#if MH_CD42_KEEPALIVE_ENABLED
    Serial.printf(
        "MH-CD42 keep-alive GPIO=%d pulse=%lu ms interval=%lu ms\n",
        MH_CD42_KEEPALIVE_PIN,
        static_cast<unsigned long>(MH_CD42_KEEPALIVE_PULSE_MS),
        static_cast<unsigned long>(MH_CD42_KEEPALIVE_INTERVAL_MS)
    );
#else
    Serial.println("MH-CD42 keep-alive disabled");
#endif
#if SUPPLY_BATTERY_ADC_ENABLED
    Serial.printf(
        "Supply battery ADC GPIO=%d divider %.0f/%.0f ohm\n",
        SUPPLY_BATTERY_ADC_PIN,
        SUPPLY_BATTERY_R_TOP_OHMS,
        SUPPLY_BATTERY_R_BOTTOM_OHMS
    );
#endif
    Serial.printf("Poll interval: %lu ms\n", static_cast<unsigned long>(POLL_INTERVAL_MS));
    Serial.printf("Fake data mode: %s\n", USE_FAKE_DATA ? "on" : "off");

    startWiFiConnect(millis());
}

void loop() {
    const uint32_t nowMs = millis();
    handleWakeButton(nowMs);
    handleMhCd42KeepAlive(nowMs);
    bleSetup.handle(nowMs);
    updateSupplyBattery(nowMs);
    handleDisplayPower(nowMs);

    RuntimeConfig requestedConfig;
    if (bleSetup.takeSaveRequest(requestedConfig)) {
        startBleWiFiConfigTest(nowMs, requestedConfig);
    }

    if (handleBleWiFiConfigTest(nowMs)) {
        yield();
        return;
    }

    handleWiFi(nowMs);

    if (bleSetup.active()) {
        yield();
        return;
    }

#if USE_FAKE_DATA
    pollDeye(nowMs);
#else
    if (WiFi.status() == WL_CONNECTED) {
        pollDeye(nowMs);
    }
#endif

    static uint32_t nextDisplayRefreshMs = 0;
    if (nowMs >= nextDisplayRefreshMs) {
        nextDisplayRefreshMs = nowMs + 1000;
        if (appState == AppState::Ok && metrics.valid && nowMs - metrics.timestampMs > POLL_INTERVAL_MS + 5000UL) {
            showState(AppState::Stale);
        } else {
            displayStatus(appState, metrics, nullptr, supplyBattery);
        }
    }

    yield();
}
