#pragma once

#define WIFI_SSID "your-wifi"
#define WIFI_PASSWORD "your-password"

#define DEYE_LOGGER_IP "192.168.1.2"
#define DEYE_LOGGER_PORT 8899

// Deye / Solarman logger serial.
// User must fill this from logger sticker or previous discovery.
#define DEYE_LOGGER_SERIAL 1234567890UL

// Usually 1, but keep configurable.
#define DEYE_MODBUS_SLAVE_ID 1

#define OLED_SDA_PIN 4
#define OLED_SCL_PIN 3
#define OLED_I2C_ADDRESS 0x3C
// 0 = SSD1306, 1 = SH1106.
#define OLED_USE_SH1106 0
// Increase if the visible image is clipped on the left edge.
#define OLED_UI_X_OFFSET 3

#define POLL_INTERVAL_MS 60000

// Display power policy.
#define DISPLAY_BRIGHTNESS_HIGH_PERCENT 50
#define DISPLAY_BRIGHTNESS_DIM_PERCENT 20
#define DISPLAY_DIM_AFTER_MS 180000UL
#define DISPLAY_SLEEP_AFTER_MS 600000UL

// ESP GPIO wake button is optional. Leave disabled when relying on the MH-CD42
// onboard/remote button to restore 5V power and cold-boot the ESP.
#define WAKE_BUTTON_ENABLED 1
// Optional ESP wake wiring: GPIO -> button -> GND, internal pull-up enabled.
// ESP32-C3 deep-sleep wake supports GPIO0..GPIO5; avoid strapping pins GPIO2/GPIO8/GPIO9.
#define WAKE_BUTTON_PIN 1
#define WAKE_BUTTON_ACTIVE_LOW 1
#define WAKE_BUTTON_DEBOUNCE_MS 50

// BLE setup mode. Hold the wake button for 5 seconds while the ESP is awake to
// expose a Nordic UART-compatible BLE service for 3 minutes.
#define BLE_SETUP_ENABLED 1
#define BLE_SETUP_DEVICE_NAME "Deye Monitor"
#define BLE_SETUP_HOLD_MS 5000UL
#define BLE_SETUP_WINDOW_MS 180000UL
#define BLE_WIFI_TEST_TIMEOUT_MS 20000UL

// MH-CD42 KEY keep-alive. The ESP periodically pulls the MH-CD42 KEY input low
// so the module keeps its 5V output enabled with a small load.
// Direct GPIO wiring is OK only if KEY is measured <= 3.3V when released.
#define MH_CD42_KEEPALIVE_ENABLED 1
#define MH_CD42_KEEPALIVE_PIN 5
#define MH_CD42_KEEPALIVE_INTERVAL_MS 15000UL
#define MH_CD42_KEEPALIVE_PULSE_MS 250UL

// ESP supply battery monitor.
// Default wiring: battery + -> 680k -> ADC pin -> 470k -> GND.
#define SUPPLY_BATTERY_ADC_ENABLED 1
#define SUPPLY_BATTERY_ADC_PIN 0
#define SUPPLY_BATTERY_R_TOP_OHMS 680000.0f
#define SUPPLY_BATTERY_R_BOTTOM_OHMS 470000.0f
#define SUPPLY_BATTERY_EMPTY_MV 3200.0f
#define SUPPLY_BATTERY_FULL_MV 4200.0f
#define SUPPLY_BATTERY_SAMPLE_COUNT 16
#define SUPPLY_BATTERY_READ_INTERVAL_MS 30000UL

// For early hardware tests without Deye logger.
#define USE_FAKE_DATA 0
