#include "display.h"

#include <U8g2lib.h>
#include <Wire.h>

#include "config.h"

namespace {
#if OLED_USE_SH1106
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(
    U8G2_R0,
    U8X8_PIN_NONE
);
#else
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(
    U8G2_R0,
    U8X8_PIN_NONE
);
#endif

const char* stateLabel(AppState state) {
    switch (state) {
        case AppState::Boot:
            return "BOOT";
        case AppState::WiFiConnecting:
            return "WiFi";
        case AppState::WiFiError:
            return "WiFi ERR";
        case AppState::DeyePolling:
            return "POLL";
        case AppState::Ok:
            return "OK";
        case AppState::Stale:
            return "STALE";
        case AppState::DeyeError:
            return "Deye ERR";
    }
    return "ERR";
}

void metricText(char* out, size_t outLen, float value, uint8_t decimals, const char* suffix) {
    if (!isMetricKnown(value)) {
        snprintf(out, outLen, "--%s", suffix);
        return;
    }

    char number[12];
    dtostrf(value, 0, decimals, number);
    snprintf(out, outLen, "%s%s", number, suffix);
}

void ageText(char* out, size_t outLen, const InverterMetrics& metrics, uint32_t nowMs) {
    if (!metrics.valid || metrics.timestampMs == 0) {
        snprintf(out, outLen, "no data");
        return;
    }

    const uint32_t ageSec = (nowMs - metrics.timestampMs) / 1000UL;
    if (ageSec < 60) {
        snprintf(out, outLen, "%lus ago", static_cast<unsigned long>(ageSec));
    } else {
        snprintf(out, outLen, "%lum", static_cast<unsigned long>(ageSec / 60));
    }
}

void supplyText(char* out, size_t outLen, const SupplyBatteryStatus& supplyBattery) {
    if (!supplyBattery.valid) {
        snprintf(out, outLen, "PWR --");
        return;
    }

    snprintf(out, outLen, "PWR %u%%", supplyBattery.chargePercent);
}

void dailyGenerationText(char* out, size_t outLen, float dailyGenerationKwh) {
    if (!isMetricKnown(dailyGenerationKwh)) {
        snprintf(out, outLen, "-- kWh");
        return;
    }

    if (dailyGenerationKwh < 1.0f) {
        snprintf(out, outLen, "%.0f Wh", dailyGenerationKwh * 1000.0f);
    } else if (dailyGenerationKwh < 100.0f) {
        char number[8];
        dtostrf(dailyGenerationKwh, 0, 1, number);
        snprintf(out, outLen, "%s kWh", number);
    } else {
        snprintf(out, outLen, "%.0f kWh", dailyGenerationKwh);
    }
}
}

void displayBegin() {
    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
    Serial.printf("I2C scan on SDA=%d SCL=%d\n", OLED_SDA_PIN, OLED_SCL_PIN);
    for (uint8_t address = 1; address < 127; ++address) {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() == 0) {
            Serial.printf("I2C device found at 0x%02X\n", address);
        }
    }
    u8g2.setI2CAddress(OLED_I2C_ADDRESS << 1);
    u8g2.begin();
    u8g2.setFont(u8g2_font_6x10_tf);
}

void displaySetBrightnessPercent(uint8_t percent) {
    if (percent > 100) {
        percent = 100;
    }

    u8g2.setContrast(static_cast<uint8_t>((255UL * percent) / 100UL));
}

void displaySleep() {
    u8g2.clearBuffer();
    u8g2.sendBuffer();
    u8g2.setPowerSave(1);
}

void displayWake() {
    u8g2.setPowerSave(0);
}

void displayStatus(
    AppState state,
    const InverterMetrics& metrics,
    const char* detail,
    const SupplyBatteryStatus& supplyBattery
) {
    char soc[16];
    char volts[16];
    char bat[16];
    char load[16];
    char pv[16];
    char grid[16];
    char age[16];
    char supply[16];
    char daily[16];

    metricText(soc, sizeof(soc), metrics.batterySoc, 0, "%");
    metricText(volts, sizeof(volts), metrics.batteryVoltage, 1, "V");
    metricText(bat, sizeof(bat), metrics.batteryPower, 0, "W");
    metricText(load, sizeof(load), metrics.loadPower, 0, "W");
    metricText(pv, sizeof(pv), metrics.pvTotalPower, 0, "W");
    metricText(grid, sizeof(grid), metrics.gridPower, 0, "W");
    ageText(age, sizeof(age), metrics, millis());
    supplyText(supply, sizeof(supply), supplyBattery);
    dailyGenerationText(daily, sizeof(daily), metrics.dailyGenerationKwh);

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);

    const uint8_t x = OLED_UI_X_OFFSET;
    const uint8_t rightX = 76 + OLED_UI_X_OFFSET;

    u8g2.setCursor(x, 9);
    u8g2.print("SOC ");
    u8g2.print(soc);
    u8g2.setCursor(rightX, 9);
    u8g2.print(volts);

    u8g2.setCursor(x, 21);
    u8g2.print("BAT ");
    u8g2.print(bat);
    u8g2.setCursor(rightX, 21);
    u8g2.print(daily);

    u8g2.setCursor(x, 33);
    u8g2.print("LOAD ");
    u8g2.print(load);
    u8g2.setCursor(rightX, 33);
    u8g2.print(supply);

    u8g2.setCursor(x, 45);
    u8g2.print("PV ");
    u8g2.print(pv);

    u8g2.setCursor(x, 57);
    u8g2.print("GRID ");
    u8g2.print(grid);

    u8g2.setCursor(rightX, 57);
    u8g2.print(stateLabel(state));

    if (detail != nullptr) {
        u8g2.setCursor(rightX, 45);
        u8g2.print(detail);
    } else if (state == AppState::Ok || state == AppState::Stale || state == AppState::DeyeError) {
        u8g2.setCursor(rightX, 45);
        u8g2.print(age);
    }

    u8g2.sendBuffer();
}
