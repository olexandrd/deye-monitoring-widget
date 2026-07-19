# ESP32-C3 Deye Local Monitor

Embedded MVP for an ESP32-C3 SuperMini USB-C with a 128x64 I2C OLED. It connects directly to a Deye/Solarman logger on the local Wi-Fi network and polls TCP port `8899` using a minimal Solarman V5 frame carrying a Modbus RTU read request.

No MQTT, Home Assistant, cloud service, RS485, CAN, SD card, RTC, or external gateway is used.

## Hardware

- ESP32-C3 SuperMini USB-C
- OLED 128x64 I2C display
- SSD1306 controller, usually address `0x3C`
- MH-CD42 Li-ion power bank / charger module
- 1-cell Li-ion/LiPo battery
- Wake button
- Voltage divider resistors: 680k and 470k

The `hardware/case/` directory contains a ready-to-print STL enclosure for the
ESP32-C3 SuperMini + OLED stack. The older 3D-model generation scripts are no
longer part of the repository; use the checked-in STL directly.

## Wiring

### OLED

ESP32-C3 SuperMini -> OLED SSD1306/SH1106 I2C

```text
3V3   -> VCC
GND   -> GND
GPIO3 -> SCL/SCK
GPIO4 -> SDA
```

Some OLED modules can use 5V VCC, but check your module before wiring. If your OLED does not work, scan the I2C bus and verify address `0x3C` or `0x3D`.

Many 1.3" OLED modules use SH1106 even when sold as SSD1306-compatible. Set `OLED_USE_SH1106` in `src/config.h` to switch between SSD1306 and SH1106. If the image is only clipped a few pixels on the left, keep SSD1306 and increase `OLED_UI_X_OFFSET`.

### ESP Supply Battery Measurement

Default ADC pin: `GPIO0`.

```text
Battery +  -> 680k -> GPIO0/ADC
GPIO0/ADC  -> 470k -> GND
Battery -  -> GND
```

This divider scales a 4.2V Li-ion battery to about 1.72V at the ADC pin:

```text
Vadc = Vbat * 470k / (680k + 470k)
```

Optional but recommended: add a 100nF capacitor from `GPIO0/ADC` to `GND` close to the ESP32-C3. The divider impedance is high, so the capacitor helps stabilize ADC readings. Never connect the battery positive directly to an ESP32 GPIO/ADC pin.

### Wake Options

Default mode relies on the MH-CD42 onboard or remote button. When MH-CD42 turns its 5V output off, the ESP32-C3 is unpowered; pressing the MH-CD42 button restores 5V and the ESP32-C3 cold-boots.

No ESP GPIO wake button is required in this mode:

```cpp
#define WAKE_BUTTON_ENABLED 0
```

Optional ESP deep-sleep wake button, only useful when MH-CD42 keeps 5V output enabled while the ESP32-C3 is sleeping:

```text
GPIO1 -> button -> GND
```

Enable it with:

```cpp
#define WAKE_BUTTON_ENABLED 1
#define WAKE_BUTTON_PIN 1
```

The firmware enables the internal pull-up. For more reliable deep-sleep wake, especially with long wires, add an external 100k pull-up from `GPIO1` to `3V3`. Do not pull the wake pin up to 5V.

On ESP32-C3, deep-sleep GPIO wake is available on `GPIO0..GPIO5`. Avoid `GPIO2`, `GPIO8`, and `GPIO9` for the wake button because they are boot strapping pins and can break boot/upload when held at the wrong level during reset.

### MH-CD42 KEY Keep-Alive

Some MH-CD42 boards turn off their 5V output when the load is too small. The module documentation describes `KEY` as a low-level trigger: briefly pulling `KEY` to `GND` simulates pressing the module button. The ESP32-C3 sends a short low pulse every 15 seconds so MH-CD42 keeps working while the firmware is awake.

Default: enabled on `GPIO5`.

```cpp
#define MH_CD42_KEEPALIVE_ENABLED 1
#define MH_CD42_KEEPALIVE_PIN 5
```

Timing:

```cpp
#define MH_CD42_KEEPALIVE_INTERVAL_MS 15000UL
#define MH_CD42_KEEPALIVE_PULSE_MS 250UL
```

Direct wiring is OK if the released `KEY` pin is measured at 3.3V or lower. If it is higher, use a small N-MOSFET or NPN transistor instead.

Direct wiring:

```text
ESP GPIO5 -> MH-CD42 KEY
ESP GND   -> MH-CD42 GND
```

Optional transistor wiring:

```text
ESP GPIO5 -> gate/base driver -> transistor
MH-CD42 KEY -> transistor drain/collector
MH-CD42 GND -> transistor source/emitter -> ESP GND
```

The GPIO is configured as open-drain: it only pulls `KEY` low for the pulse, then releases it. Do not drive the MH-CD42 `KEY` pin with 5V into an ESP GPIO.

Do not enable the ESP wake button and MH-CD42 keep-alive on the same GPIO at the same time.

### Power Chain

```text
Li-ion battery + -> MH-CD42 B+
Li-ion battery - -> MH-CD42 B-

MH-CD42 OUT+/5V -> ESP32-C3 5V/VBUS
MH-CD42 OUT-/GND -> ESP32-C3 GND

ESP32-C3 3V3 -> OLED VCC
ESP32-C3 GND -> OLED GND
ESP32-C3 GND -> battery divider GND
ESP32-C3 GND -> optional ESP wake button GND
ESP32-C3 GND -> optional MH-CD42 KEY keep-alive transistor GND
```

Keep all grounds common: MH-CD42 output ground, ESP32-C3 ground, OLED ground, resistor divider ground, and button ground.

## Configuration

Copy the example config and edit it:

```sh
cp src/config.example.h src/config.h
```

Set:

- `WIFI_SSID`
- `WIFI_PASSWORD`
- `DEYE_LOGGER_IP`
- `DEYE_LOGGER_SERIAL`
- `BLE_SETUP_ENABLED`, `BLE_SETUP_DEVICE_NAME`, and `BLE_SETUP_HOLD_MS` if using BLE setup mode
- `OLED_SDA_PIN` and `OLED_SCL_PIN` if your ESP32-C3 SuperMini revision uses different pins
- `WAKE_BUTTON_ENABLED` and `WAKE_BUTTON_PIN` only if using a separate ESP GPIO wake button instead of the MH-CD42 button
- `MH_CD42_KEEPALIVE_ENABLED` and `MH_CD42_KEEPALIVE_PIN` for the periodic MH-CD42 `KEY` pulse
- `SUPPLY_BATTERY_ADC_PIN` if `GPIO0` is not available on your board
- `SUPPLY_BATTERY_EMPTY_MV` and `SUPPLY_BATTERY_FULL_MV` to calibrate the displayed power battery percentage

`src/config.h` is ignored by git so Wi-Fi credentials are not committed.
Setting `BLE_SETUP_ENABLED` to `0` also excludes the BLE implementation and its
framework library from the linked firmware.

On boot, the firmware first loads runtime settings from ESP32 NVS. If a value
has never been saved through BLE, the matching `src/config.h` value is used as
the default.

## BLE Setup Mode

BLE setup mode is entered from a long press on the ESP GPIO wake button, so it
requires `WAKE_BUTTON_ENABLED` to be set to `1` and a button wired to
`WAKE_BUTTON_PIN`. Hold that button for 5 seconds while the firmware is awake to
start BLE setup mode. The device advertises for 3 minutes using a Nordic
UART-compatible service:

```text
Service: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
RX/write: 6E400002-B5A3-F393-E0A9-E50E24DCCA9E
TX/notify: 6E400003-B5A3-F393-E0A9-E50E24DCCA9E
```

Use a generic BLE client such as nRF Connect or LightBlue, connect to
`BLE_SETUP_DEVICE_NAME`, subscribe to TX notifications, then write `start` to
RX as UTF-8 text. Until `start` is received, the firmware periodically sends
`WRITE start` notifications so generic clients can display the ready state.
The setup flow is text-based:

```text
SETUP v3
SSID?
PASS?
DEYE HOST?
DEYE SERIAL?
SAVE yes/no?
```

The firmware also sends `empty keeps old` at the start of the flow and a short
`CUR ...` line before prompts where a current value is useful. Send an empty
text value at any prompt to keep the current value. Single-write clients remain
compatible. Longer answers may be split across consecutive writes; append a
newline to the final fragment to complete the value immediately. Values such as
`start`, `?`, and `skip` are treated literally after the setup flow has started.
After `yes`, the firmware tests the Wi-Fi connection before saving. If Wi-Fi
connects within
`BLE_WIFI_TEST_TIMEOUT_MS`, the full configuration is atomically written to NVS
and reused after power loss. If the test or save fails, the old active
configuration stays in NVS.

## Build and Flash

Install PlatformIO, then run:

```sh
pio run
pio run --target upload
pio device monitor
```

Serial monitor baud rate is `115200`.

BLE support makes the firmware too large for the default partition layout on a
4MB ESP32-C3 board. `platformio.ini` uses `min_spiffs.csv`, which provides two
larger application slots and keeps OTA partitions available.

## License

This project is licensed under the Creative Commons
Attribution-NonCommercial 4.0 International Public License
(`CC-BY-NC-4.0`). Non-commercial use, sharing, and adaptation are allowed with
attribution. Commercial use requires separate written permission.

This non-commercial license is applied intentionally to the firmware,
documentation, and the case STL, even though Creative Commons recommends
software-specific licenses for software-only projects.

## Third-Party Notice

This is an unofficial project and is not affiliated with or endorsed by Deye,
Solarman, PlatformIO, Espressif, Arduino, or U8g2.

The repository license applies only to the original project files in this
repository. Third-party dependencies are distributed under their own licenses.
No vendor firmware, proprietary documentation, credentials, or cloud service
materials are included.

## Fake Data Mode

For early display and wiring tests, set:

```cpp
#define USE_FAKE_DATA 1
```

The ESP32 still connects to Wi-Fi, but it does not contact the Deye logger. It generates changing SOC, battery, load, PV, and grid values and refreshes them using the `POLL_INTERVAL_MS` scheduler.

## Deye Direct Mode

For direct logger polling, set:

```cpp
#define USE_FAKE_DATA 0
```

The firmware connects to:

```text
DEYE_LOGGER_IP:8899
```

It sends a Solarman V5 local TCP frame containing a Modbus RTU read-holding-registers request. Polling is scheduled with `millis()` and defaults to once every 60 seconds.

The register map is isolated in `src/deye_client.h` under `namespace DeyeRegisters`. Register addresses and scaling may need adjustment for your exact inverter/logger firmware.
Daily generation is read from register `108` and displayed as daily energy (`Wh`/`kWh`).

## OLED UI

The main screen shows:

```text
SOC 65%   53.3V
BAT -120W 4.8 kWh
LOAD 430W PWR 86%
PV 820W
GRID 0W
OK 23s ago
```

States include boot, Wi-Fi connecting, Wi-Fi error, BLE setup, Deye polling, OK, stale, and Deye error. On failures, the display keeps the last valid metrics when available.

The screen starts at 50% OLED contrast, dims to 20% after 3 minutes, and enters ESP32 deep sleep after 10 minutes. In the default MH-CD42 mode, press the MH-CD42 onboard or remote button to restore 5V power and boot the ESP32-C3 again. If `WAKE_BUTTON_ENABLED` is set to `1`, pressing the ESP GPIO wake button while the device is awake restores the 50% display level and restarts the 3/10 minute timers.

## Serial Debug

USB Serial logs:

- boot info
- Wi-Fi status
- ESP32 IP
- Deye logger IP and port
- poll start
- poll success/failure
- raw response length
- parsed metrics
- error codes

The Wi-Fi password is never printed.

## Known Limitations

- The Solarman V5 implementation is intentionally minimal and built for local TCP polling.
- Deye register addresses are model-dependent and marked for verification.
- Only a single holding-register range is read in the MVP.
- No NTP is used, so the display shows data age instead of wall-clock time.
- Supply battery percentage uses a configurable linear Li-ion voltage mapping, not a fuel-gauge IC.

## TODO

- Verify register addresses and scaling for the target Deye inverter model.
- Add an optional I2C scanner helper.
- Add a button to switch pages.
- Add NTP time.
- Add a web status page.
- Add OTA firmware update.
- Add W25Q64 binary ring buffer.
- Add CSV export over HTTP.
- Add multiple OLED pages.
- Add GC9A01 TFT version.
