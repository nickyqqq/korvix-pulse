#pragma once
#include <stdint.h>

// Wi-Fi is configured from a phone: scan the QR code on the clock screen.
// No home-network name/password is compiled into this sketch.
// The touchscreen's "Use offline" choice is remembered between boots.
static constexpr bool ENABLE_WIFI_TIME_SYNC = true;

// Dual USB ESP32-2432S028: ST7789. Set to 0 for an ILI9341 revision.
#ifndef CLOCK_USE_ST7789
#define CLOCK_USE_ST7789 1
#endif
static constexpr uint8_t DISPLAY_ROTATION = 1; // 1 or 3: landscape
// Panel scan-direction correction. Enabled for this board's mirrored ST7789 view.
static constexpr bool DISPLAY_MIRROR_X = (CLOCK_USE_ST7789 != 0);
static constexpr bool DISPLAY_MIRROR_Y = false;
// Changing orientation automatically requests fresh touch calibration.
static constexpr uint32_t DISPLAY_ORIENTATION_KEY = DISPLAY_ROTATION |
    (DISPLAY_MIRROR_X ? 0x100u : 0u) | (DISPLAY_MIRROR_Y ? 0x200u : 0u) |
    (CLOCK_USE_ST7789 ? 0x10000u : 0u);
static constexpr bool DISPLAY_INVERT = false;
static constexpr bool DISPLAY_BGR = true;
static constexpr uint32_t TFT_SPI_HZ = 40000000;
// Animations are included in firmware; select the Huge APP (3MB No OTA)
// partition scheme in Arduino IDE. No microSD/filesystem upload is needed.

// Local wall-clock schedule, in minutes after midnight. Fixed times, not
// astronomical sunset; they automatically follow the selected time zone.
static constexpr int DAY_START = 6 * 60;
static constexpr int SUNSET_START = 17 * 60;
static constexpr int NIGHT_START = 19 * 60;
static_assert(0 <= DAY_START && DAY_START < SUNSET_START &&
              SUNSET_START < NIGHT_START && NIGHT_START < 1440,
              "Use increasing day/sunset/night times within one day.");
static_assert(DISPLAY_ROTATION == 1 || DISPLAY_ROTATION == 3,
              "Videos require landscape rotation 1 or 3.");

// Temporary visual check: cycle day, sunset, night every 10 seconds.
static constexpr bool DEMO_BACKGROUNDS = false;
// Advertising build: fill the Focus calendar with sample progress (shown only, never saved).
static constexpr bool DEMO_FOCUS_HISTORY = false;

// Battery version: true shows the battery level in the bottom-right corner (wiring: docs/BATTERY.md).
static constexpr bool BATTERY_INDICATOR = false;
// How the level is read:
//   0 = battery voltage through two 100k resistors into GPIO 35 (works with any IP5306 module)
//       Battery + -> 100k -> IO35 (P3 connector) -> 100k -> GND. NOT GPIO 27: with Wi-Fi on,
//       the ESP32 can't measure voltage on GPIO 27, and 4.2 V straight into a pin can damage it.
//   1 = IP5306 "I2C" version only (a data chip, not the battery wire): SDA -> GPIO 27, SCL -> GPIO 22
static constexpr int BATTERY_SOURCE = 0;
static constexpr int BATTERY_ADC_PIN = 35;
static_assert(BATTERY_ADC_PIN >= 32 && BATTERY_ADC_PIN <= 39,
              "BATTERY_ADC_PIN must be GPIO 32-39 (ADC1). Other pins can't be read while Wi-Fi is on.");
static constexpr float BATTERY_DIVIDER = 2.0f;      // (R1 + R2) / R2 of the resistor divider
static constexpr float BATTERY_CALIBRATION = 1.0f;  // e.g. 1.03 if it reads 3 % low against a multimeter
// Charging bolt (resistor version). -1 = no extra wire: the clock spots the charger from the battery
// voltage (it jumps up when plugged in), a few seconds after plugging in or out.
// For an exact signal, wire the charger's USB 5 V through two equal resistors into GPIO 27
// (5 V -> 10k -> IO27 on CN1 -> 10k -> GND) and set this to 27.
static constexpr int CHARGE_DETECT_PIN = -1;
static_assert(CHARGE_DETECT_PIN < 0 || BATTERY_SOURCE == 0, "CHARGE_DETECT_PIN is for the resistor version (BATTERY_SOURCE 0).");

// EN/RESET and power-on are indistinguishable on the classic ESP32.
// true asks EVERY boot (including power-on); false remembers the choice.
// With either setting, hold the screen through startup to reopen setup.
static constexpr bool ASK_TIMEZONE_EVERY_BOOT = false;

// Scanning the Wi-Fi setup QR code opens this page (a page on your Shopify store, handle
// "clock-setup"); the clock adds its hotspot name and password to the link so the page shows them.
static constexpr const char *SETUP_GUIDE_URL = "https://korvixtech.store/pages/clock-setup";
