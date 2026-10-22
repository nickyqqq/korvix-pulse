#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <TJpg_Decoder.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <Wire.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <esp_system.h>
#include "Config.h"
#include "TimeZones.h"
#include "ManualTime.h"
#include "EmbeddedAnimations.h"
#include "BitBangTouch.h"
#include "CustomAnimation.h"
#include "qrcode.h"
#include "GifBenchmark.h"
#include "BootLogo.h"
#if CLOCK_USE_ST7789
#include <Adafruit_ST7789.h>
#else
#include <Adafruit_ILI9341.h>
#endif

static SPIClass displaySPI(HSPI);
#if CLOCK_USE_ST7789
static Adafruit_ST7789 screen(&displaySPI, 15, 2, -1);
#else
static Adafruit_ILI9341 screen(&displaySPI, 2, 15, -1);
#endif
static BitBangTouch touch;
static Preferences preferences;
static WebServer clockServer(80);
static DNSServer clockDns;
static CustomAnimation customAnimation;
static constexpr int WIDTH = 320, HEIGHT = 240, STRIP_ROWS = 16;
static constexpr int OVERLAY_Y = 128, OVERLAY_HEIGHT = 112;
static constexpr int CUSTOM_SCENE = 3;
static uint16_t pixels[WIDTH * STRIP_ROWS];
static GFXcanvas1 clockMask(WIDTH, HEIGHT);   // full screen: the Quotes face uses all of it
static int overlayTop = OVERLAY_Y;             // first screen line the clock text/darkening touches
static const uint16_t BG = 0x0863, PANEL = 0x1906, ACCENT = 0x4E9F, GOOD = 0x2230;
static const uint16_t WHITE = 0xFFFF, MUTED = 0xB5D6, WARN = 0xFD20;

enum class Page { Clock, Settings, Regions, Zones, ConfirmZone, ConfirmReset, Calibration,
                  ManualTime, RepeatedTime, WiFiSetup, Phone, LockIn, Calendar };
enum class WifiJob : uint8_t { Idle, Connecting, Ok, Fail };
static Page page = Page::Clock;
static int zoneIndex = -1, candidateZone = -1, selectedRegion = 0, zonePage = 0;
static bool requireZone = false, prefsReady = false, overlayDirty = true;
static bool showingStorageError = false;
static bool wifiConfigured = false, wasConnected = false;
static bool pendingWiFiSetup = false;
static char setupSsid[32] = {};
static char setupPassword[16] = {};
static uint32_t lastWifiRetry = 0;
static time_t lastMaskTime = -1;
static int lastMaskScene = -1, lastMaskFace = -1;
static bool lastMaskConnected = false;
static ManualDateTime enteredTime{};
static int selectedTimeField = 0;
static Page manualReturn = Page::Clock;
static const char *manualMessage = "Tap a field, then use - and +.";
static time_t earlierTime = 0, laterTime = 0;

// Phone setup / web state
// Clock faces side by side: Day/Night | GIF | Quotes.
enum : uint8_t { FACE_SCENES = 0, FACE_GIF = 1, FACE_QUOTE = 2, FACE_COUNT = 3 };
static uint8_t clockFace = FACE_SCENES;
static int quoteOffset = 0;  // swipe up/down on the Quotes face shows the next / previous quote
static bool customEnabled = false, playingCustom = false, uploadInProgress = false, uploadSucceeded = false;
static bool apActive = false, webReady = false, connectPending = false, legacySavedWiFi = false;
static bool phoneJustConnected = false;
static bool phoneForQuotes = false;   // the phone page was opened from the Quotes tile
static int quoteCount();
static constexpr int MAX_QUOTES = 200, MAX_QUOTE_LEN = 90;
static const char *const QUOTES_PATH = "/littlefs/quotes.txt";  // your own quote list
static WifiJob wifiJob = WifiJob::Idle;
static String webToken, pendingSsid, pendingPassword, uploadMessage, savedSsid, savedPassword;
static uint32_t connectAt = 0, connectStartedAt = 0, apStartedAt = 0, apCloseAt = 0, lastScanAt = 0;
static uint32_t phonePageAt = 0;
// Battery state (battery build): read by serviceBattery(), shown on the clock and the phone page.
static int batteryPercent = -1;        // -1 = no battery reading (not wired / not the battery build)
static bool batteryCharging = false;   // plugged in (charging or full)
static float batteryVolts = 0;
static uint32_t batteryReadAt = 0, batteryLoggedAt = 0;
static constexpr uint32_t SETUP_HOTSPOT_MS = 15UL * 60UL * 1000UL;
static void beginClockWeb();
static bool selectGif(uint32_t id);

struct Calibration {
  uint32_t magic;
  uint32_t rotation;
  float a, b, c, d, e, f; // Screen x=a*rawX+b*rawY+c; y=d*rawX+e*rawY+f.
};
static Calibration calibration{};
static float calibrationRawX[3], calibrationRawY[3];
static constexpr int calibrationX[3] = {30, 290, 160};
static constexpr int calibrationY[3] = {30, 30, 210};
static int calibrationStep = 0;
static Page calibrationReturn = Page::Clock;
static bool touchHeld = false;
static uint16_t lastRawX = 0, lastRawY = 0, startRawX = 0, startRawY = 0;
// Swipes: decided when the finger lifts, handled from loop() (never in the middle of drawing).
enum Swipe : int8_t { SWIPE_NONE = 0, SWIPE_LEFT, SWIPE_RIGHT, SWIPE_UP, SWIPE_DOWN };
static volatile int8_t pendingSwipe = SWIPE_NONE;
static const char *clockHint = nullptr;   // short message in the clock's title line
static uint32_t clockHintUntil = 0;
static uint32_t touchSeenAt = 0, lastTouchPoll = 0, touchStartedAt = 0;
static int touchSamples = 0;
static bool waitForRelease = true;
static uint32_t releaseStartedAt = 0;

static const AnimationClip *movie = nullptr;
static int currentScene = -1;
static uint32_t movieFrames = 0, lastFrame = UINT32_MAX;
static uint16_t movieFps = 10;
static uint64_t movieStart = 0, fallbackAt = 0;
static const char *mediaStatus = "Loading video";
static const char *const sceneNames[] = {"DAY", "SUNSET", "NIGHT", ""};

// Smooth GIF drawing: each 240x180 GIF frame is decoded completely into RAM,
// then scaled up to 320x240 and sent to the screen in one quick top-to-bottom
// pass. Without this, the screen shows half-old/half-new 16x16 tiles while a
// frame decodes, which looks like blinking. (A full 320x240 buffer, 150 KB,
// does not fit next to Wi-Fi on this board; 240x180 is 84 KB.)
static constexpr int SMALL_W = CustomAnimation::SMALL_WIDTH, SMALL_H = CustomAnimation::SMALL_HEIGHT;
static uint16_t *smallFrame = nullptr;
static bool decodingToSmallFrame = false;
static uint16_t scaleX[WIDTH];  // source column (low 14 bits) + blend weight 0..3 (top 2 bits)
static void allocateSmallFrame() {
  if (smallFrame) return;
  smallFrame = static_cast<uint16_t *>(heap_caps_malloc(SMALL_W * SMALL_H * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  Serial.printf("Smooth GIF drawing: %s (free memory %u bytes)\n", smallFrame ? "on" : "OFF, not enough memory",
                static_cast<unsigned>(ESP.getFreeHeap()));
}
// What each screen line currently shows (a checksum), so only changed lines are sent.
static uint32_t lineHash[HEIGHT];
static bool lineHashValid = false;
static int32_t smallFrameIndex = -1;   // GIF frame currently decoded in smallFrame
static bool customBackdropDrawn = false;
static float gifDrawMs = 0;            // average ms to show one GIF frame (measured while playing)
static float gifBenchMs = 0;           // worst case (whole picture changes), measured at start-up
static void forgetScreenContents() { lineHashValid = false; customBackdropDrawn = false; }
static void releaseSmallFrame() { free(smallFrame); smallFrame = nullptr; smallFrameIndex = -1; }


static uint64_t uptimeMs() { return static_cast<uint64_t>(esp_timer_get_time()) / 1000; }

static void centered(Adafruit_GFX &g, const char *text, int baseline,
                     const GFXfont *font, uint16_t color, int size = 1) {
  g.setFont(font);
  g.setTextSize(size);
  g.setTextColor(color);
  g.setTextWrap(false);
  int16_t x, y;
  uint16_t w, h;
  g.getTextBounds(text, 0, baseline, &x, &y, &w, &h);
  g.setCursor((WIDTH - static_cast<int>(w)) / 2 - x, baseline);
  g.print(text);
}

static void label(const char *text, int x, int y, uint16_t color = WHITE, int size = 1) {
  screen.setFont(nullptr);
  screen.setTextWrap(false);
  screen.setTextSize(size);
  screen.setTextColor(color);
  screen.setCursor(x, y);
  screen.print(text);
}

static void button(int x, int y, int w, int h, const char *text, uint16_t color = PANEL) {
  screen.fillRoundRect(x, y, w, h, 5, color);
  const int tw = strlen(text) * 6;
  label(text, x + (w - tw) / 2, y + (h - 8) / 2);
}

static bool inside(int x, int y, int left, int top, int w, int h) {
  return x >= left && x < left + w && y >= top && y < top + h;
}

static bool localClock(time_t utc, tm &local) {
  return zoneIndex >= 0 && timeZoneLocalTime(static_cast<size_t>(zoneIndex), utc, &local);
}

static int sceneFor(const tm &local) {
  const int minute = local.tm_hour * 60 + local.tm_min;
  if (minute >= DAY_START && minute < SUNSET_START) return 0;
  if (minute >= SUNSET_START && minute < NIGHT_START) return 1;
  return 2;
}

static int regionCount() {
  int count = 0;
  for (size_t i = 0; i < TIME_ZONE_COUNT; ++i)
    if (TIME_ZONES[i].region == selectedRegion) ++count;
  return count;
}

static int zoneAtRow(int row) {
  int ordinal = zonePage * 5 + row;
  for (size_t i = 0; i < TIME_ZONE_COUNT; ++i) {
    if (TIME_ZONES[i].region == selectedRegion && ordinal-- == 0) return i;
  }
  return -1;
}

static void drawPage();
static void resetManualForm() {
  selectedTimeField = 0;
  manualMessage = "Tap a field, then use - and +.";
  if (zoneIndex >= 0 && manualUtcToLocal(zoneIndex, time(nullptr), enteredTime)) return;
  // Only seed the editor with the build date. Never treat it as current time.
  char month[4] = {};
  int day = 1, year = 2026;
  sscanf(__DATE__, "%3s %d %d", month, &day, &year);
  const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
  const char *position = strstr(months, month);
  enteredTime = {constrain(year, 2020, 2100), position ? int(position-months)/3+1 : 1,
                 day, 12, 0, 0};
}

static void setPage(Page next) {
  if (next == Page::ManualTime && page != Page::RepeatedTime) resetManualForm();
  page = next;
  overlayDirty = true;
  lastFrame = UINT32_MAX;
  forgetScreenContents();
  fallbackAt = 0;
  if (page != Page::Clock) drawPage();
}

// ---------------------------------------------------------------------------
// Wi-Fi credentials: kept in this sketch's own settings, never in source code.
// Boards set up by the earlier WiFiManager build keep working ("legacy").
// ---------------------------------------------------------------------------
static bool savedWiFiInDriver() {
  wifi_config_t config{};
  return esp_wifi_get_config(WIFI_IF_STA, &config) == ESP_OK && config.sta.ssid[0];
}
static bool haveSavedWiFi() { return savedSsid.length() > 0 || legacySavedWiFi; }
static void beginSavedWiFi() {
  if (savedSsid.length()) WiFi.begin(savedSsid.c_str(), savedPassword.length() ? savedPassword.c_str() : nullptr);
  else if (legacySavedWiFi) WiFi.begin();
}

static void logMemory(const char *when) {
  Serial.printf("%s: free memory %u bytes, largest block %u bytes\n", when, static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
}
static void stopSetupHotspot() {
  if (!apActive) return;
  clockDns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apActive = false;
  apCloseAt = 0;
  WiFi.setAutoReconnect(true);
  if (WiFi.status() != WL_CONNECTED && haveSavedWiFi()) beginSavedWiFi();
  allocateSmallFrame();  // back for smooth GIFs (if it no longer fits, GIFs play smaller until the next restart)
  logMemory("Setup hotspot closed");
}

// QR code drawn in black on a white square of `box` pixels (includes the quiet zone).
static void drawQr(const char *value, int x, int y, int box, uint8_t ecc = ECC_LOW) {
  // Byte-mode capacity for versions 1..6. The library does not check capacity itself.
  static const uint8_t capacity[2][6] = {{17, 32, 53, 78, 106, 134}, {14, 26, 42, 62, 84, 106}};
  const size_t length = strlen(value);
  uint8_t version = 0;
  for (uint8_t v = 1; v <= 6; ++v) if (length <= capacity[ecc ? 1 : 0][v-1]) { version = v; break; }
  static uint8_t modules[211]; // qrcode_getBufferSize(6)
  QRCode qr;
  if (!version || qrcode_initText(&qr, modules, version, ecc ? ECC_MEDIUM : ECC_LOW, value) != 0) {
    label("QR unavailable", x + 8, y + box / 2); return;
  }
  const int scale = box / (qr.size + 8);
  const int side = scale * (qr.size + 8);
  x += (box - side) / 2; y += (box - side) / 2;
  screen.fillRect(x, y, side, side, WHITE);
  for (uint8_t row = 0; row < qr.size; ++row)
    for (uint8_t col = 0; col < qr.size; ++col)
      if (qrcode_getModule(&qr, col, row))
        screen.fillRect(x + (col + 4) * scale, y + (row + 4) * scale, scale, scale, 0);
}

static void startWiFiSetup() {
  pendingWiFiSetup = false;
  releaseSmallFrame();   // the setup hotspot + web page need the memory; GIFs aren't shown during setup
  logMemory("Wi-Fi setup");
  wifiConfigured = true;
  if (prefsReady) preferences.putBool("offline", false);
  wifiJob = WifiJob::Idle; connectPending = false; apCloseAt = 0;
  WiFi.mode(WIFI_AP_STA);
  // Stop background reconnect attempts so the network list scan works.
  WiFi.setAutoReconnect(false);
  if (WiFi.status() != WL_CONNECTED) WiFi.disconnect(false, false);
  snprintf(setupSsid, sizeof(setupSsid), "Clock-Setup-%06lX",
           static_cast<unsigned long>((ESP.getEfuseMac() >> 24) & 0xFFFFFF));
  // A fresh hotspot password each time; the QR code carries it, so nobody types it.
  static const char alphabet[] = "abcdefghjkmnpqrstuvwxyz23456789";
  for (int i = 0; i < 10; ++i) setupPassword[i] = alphabet[esp_random() % (sizeof(alphabet) - 1)];
  setupPassword[10] = 0;
  if (apActive) { clockDns.stop(); WiFi.softAPdisconnect(false); }
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  apActive = WiFi.softAP(setupSsid, setupPassword, 1, false, 4);
  if (apActive) clockDns.start(53, "*", IPAddress(192,168,4,1));
  apStartedAt = millis();
  beginClockWeb();
  WiFi.scanDelete(); WiFi.scanNetworks(true); lastScanAt = millis();
  setPage(Page::WiFiSetup);
}

static void goToClock() {
  if (pendingWiFiSetup) { startWiFiSetup(); return; }
  tm local{};
  if (!wifiConfigured && !localClock(time(nullptr), local)) {
    manualReturn = Page::Clock;
    setPage(Page::ManualTime);
  } else setPage(Page::Clock);
}

static void openPhonePage(bool justConnected, bool forQuotes = false) {
  phoneJustConnected = justConnected;
  phoneForQuotes = forQuotes && !justConnected;
  phonePageAt = millis();
  setPage(Page::Phone);
}

static int *timeField(int index) {
  switch (index) {
    case 0: return &enteredTime.year;
    case 1: return &enteredTime.month;
    case 2: return &enteredTime.day;
    case 3: return &enteredTime.hour;
    case 4: return &enteredTime.minute;
    default: return &enteredTime.second;
  }
}

static void adjustTimeField(int delta) {
  int *field = timeField(selectedTimeField);
  const int minimum[] = {2020, 1, 1, 0, 0, 0};
  const int maximum[] = {2100, 12, daysInMonth(enteredTime.year, enteredTime.month), 23, 59, 59};
  *field += delta;
  if (*field < minimum[selectedTimeField]) *field = maximum[selectedTimeField];
  if (*field > maximum[selectedTimeField]) *field = minimum[selectedTimeField];
  enteredTime.day = min(enteredTime.day, daysInMonth(enteredTime.year, enteredTime.month));
  manualMessage = "Tap a field, then use - and +.";
  drawPage();
}

static void applyManualTime(time_t utc) {
  const timeval value = {utc, 0};
  if (settimeofday(&value, nullptr) != 0) {
    manualMessage = "Could not set time. Please try again.";
    page = Page::ManualTime; drawPage(); return;
  }
  Serial.println("Clock set from touchscreen local date and time.");
  setPage(Page::Clock);
}

static void drawCalibration() {
  screen.fillScreen(BG);
  if (calibrationStep < 3) {
    centered(screen, "TOUCH CALIBRATION", 90, nullptr, WHITE, 2);
    char message[40];
    snprintf(message, sizeof(message), "Tap the cross: %d of 3", calibrationStep + 1);
    centered(screen, message, 118, nullptr, MUTED);
    centered(screen, "Tap, then lift your finger.", 138, nullptr, MUTED);
    int x = calibrationX[calibrationStep], y = calibrationY[calibrationStep];
    screen.drawCircle(x, y, 12, ACCENT);
    screen.drawFastHLine(x - 17, y, 35, WHITE);
    screen.drawFastVLine(x, y - 17, 35, WHITE);
  } else {
    centered(screen, "Tap the center to confirm", 55, nullptr, WHITE, 2);
    screen.drawCircle(160, 120, 15, ACCENT);
    screen.drawFastHLine(140, 120, 41, WHITE);
    screen.drawFastVLine(160, 100, 41, WHITE);
    centered(screen, "Wrong position? Calibration restarts.", 192, nullptr, MUTED);
  }
}

static void drawWiFiSetup() {
  if (!apActive) {
    centered(screen, "WI-FI SETUP", 10, nullptr, WHITE, 2);
    centered(screen, "Setup timed out.", 70, nullptr, MUTED);
    button(70, 100, 180, 40, "Start again", GOOD);
  } else {
    centered(screen, "SCAN WITH YOUR PHONE", 8, nullptr, WHITE, 2);
    // The QR code opens the step-by-step guide on the Korvix store, with this clock's hotspot
    // name and password filled in (the phone isn't on the clock's Wi-Fi yet, so the guide is online).
    char guideQr[134];
    snprintf(guideQr, sizeof(guideQr), "%s?id=%s&pw=%s", SETUP_GUIDE_URL, setupSsid + 12, setupPassword);
    drawQr(guideQr, 6, 28, 170);
    const int x = 184;
    if (wifiJob == WifiJob::Connecting || connectPending) {
      label("Connecting to", x, 60, WHITE);
      label("your Wi-Fi...", x, 74, WHITE);
      label("Please wait.", x, 98, MUTED);
    } else if (wifiJob == WifiJob::Fail) {
      label("That didn't work.", x, 40, WARN);
      label("Check the password", x, 56, WHITE);
      label("on your phone and", x, 70, WHITE);
      label("try again.", x, 84, WHITE);
    } else {
      label("1 Open the camera", x, 36, WHITE);
      label("2 Point it here", x, 54, WHITE);
      label("3 Open the link", x, 72, WHITE);
      label("4 Follow the steps", x, 90, WHITE);
    }
    label("Setup page:", x, 122, MUTED);
    label("192.168.4.1", x, 136, ACCENT);
    label("Wi-Fi name:", x, 158, MUTED);
    label(setupSsid, x, 170, MUTED);
    label("Password:", x, 184, MUTED);
    label(setupPassword, x + 60, 184, MUTED);
  }
  button(8, 204, 148, 32, "Use without Wi-Fi");
  button(164, 204, 148, 32, "Later");
}

static void drawPhonePage() {
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (!connected) {
    centered(screen, phoneForQuotes ? "EDIT QUOTES" : "ADD A GIF", 10, nullptr, WHITE, 2);
    centered(screen, "First connect the clock", 70, nullptr, MUTED);
    centered(screen, "to your Wi-Fi.", 86, nullptr, MUTED);
    button(70, 110, 180, 40, "Wi-Fi setup", GOOD);
    button(8, 204, 304, 32, "Back to clock");
    return;
  }
  centered(screen, phoneJustConnected ? "CONNECTED!" : phoneForQuotes ? "EDIT QUOTES" : "ADD A GIF", 8, nullptr,
           phoneJustConnected ? 0x87F0 : WHITE, 2);
  char url[48];
  snprintf(url, sizeof(url), "http://%s/%s", WiFi.localIP().toString().c_str(), phoneForQuotes ? "#quotes" : "");
  drawQr(url, 6, 28, 170, ECC_MEDIUM);
  const int x = 184;
  if (phoneJustConnected) {
    label("Time is set.", x, 36, WHITE);
    label("Want your own GIF", x, 60, WHITE);
    label("on the clock?", x, 74, WHITE);
    label("Scan this code", x, 92, ACCENT);
    label("with your phone.", x, 106, ACCENT);
  } else if (phoneForQuotes) {
    label("1 Scan this code", x, 36, WHITE);
    label("  with your phone", x, 50, WHITE);
    label("2 Change, add or", x, 70, WHITE);
    label("  delete quotes", x, 84, WHITE);
    label("3 Saved right away", x, 104, WHITE);
  } else {
    label("1 Scan this code", x, 36, WHITE);
    label("  with your phone", x, 50, WHITE);
    label("2 Paste a GIPHY", x, 70, WHITE);
    label("  link", x, 84, WHITE);
    label("3 Tap Show", x, 104, WHITE);
  }
  label("Phone must be on", x, 140, MUTED);
  label("the same Wi-Fi.", x, 152, MUTED);
  char shortUrl[24]; snprintf(shortUrl, sizeof(shortUrl), "%s", WiFi.localIP().toString().c_str());
  label(shortUrl, x, 176, MUTED);
  button(8, 204, 304, 32, phoneJustConnected ? "Done" : phoneForQuotes ? "Back to settings" : "Back to clock");
}

// ---------------------------------------------------------------------------
// Modern UI kit for Settings and Lock In: same FreeSans family as the clock
// face, rounded cards and pill buttons on the dark background.
// ---------------------------------------------------------------------------
static const uint16_t CARD = 0x18E5, CARD_TEXT_DIM = 0x8C71, PRIMARY = 0x3B7F, DANGER = 0xE9A7, SUCCESS = 0x2E8B, AMBER = 0xFD20;
static int textWidth(const char *text, const GFXfont *font) {
  int16_t x, y; uint16_t w, h;
  screen.setFont(font); screen.setTextSize(1);
  screen.getTextBounds(text, 0, 100, &x, &y, &w, &h);
  return w;
}
// Fades: everything drawn through these helpers is blended towards the background by uiFade (255 = normal).
static uint8_t uiFade = 255;
static uint16_t blend565(uint16_t a, uint16_t b, int t256) {  // a -> b
  const int r = (a >> 11) + ((((b >> 11) - (a >> 11)) * t256) >> 8);
  const int g = ((a >> 5) & 63) + (((((b >> 5) & 63) - ((a >> 5) & 63)) * t256) >> 8);
  const int bl = (a & 31) + ((((b & 31) - (a & 31)) * t256) >> 8);
  return uint16_t((r << 11) | (g << 5) | bl);
}
static uint16_t fc(uint16_t c) { return uiFade == 255 ? c : blend565(BG, c, uiFade); }
static void fontText(const char *text, int x, int baseline, const GFXfont *font, uint16_t color) {
  screen.setFont(font); screen.setTextSize(1); screen.setTextColor(fc(color)); screen.setTextWrap(false);
  screen.setCursor(x, baseline); screen.print(text);
}
static void fontCenter(const char *text, int cx, int baseline, const GFXfont *font, uint16_t color) {
  int16_t x, y; uint16_t w, h;
  screen.setFont(font); screen.setTextSize(1);
  screen.getTextBounds(text, 0, baseline, &x, &y, &w, &h);
  fontText(text, cx - int(w) / 2 - x, baseline, font, color);
}
static void chevronRight(int x, int cy, uint16_t color) {  // a small ">" made of two thick strokes
  color = fc(color);
  for (int t = 0; t < 2; ++t) { screen.drawLine(x + t, cy - 5, x + 5 + t, cy, color); screen.drawLine(x + 5 + t, cy, x + t, cy + 5, color); }
}
static void pill(int x, int y, int w, int h, const char *text, uint16_t fill, uint16_t color) {
  screen.fillRoundRect(x, y, w, h, h / 2, fc(fill));
  fontCenter(text, x + w / 2, y + h / 2 + 5, &FreeSansBold9pt7b, color);
}
static void clockChip() {  // top-right "Clock >" back button used on side pages
  screen.fillRoundRect(232, 8, 80, 30, 15, fc(CARD));
  fontText("Clock", 243, 28, &FreeSansBold9pt7b, WHITE);
  chevronRight(299, 23, WHITE);
}
static void pageTitle(const char *title) { fontText(title, 16, 31, &FreeSansBold12pt7b, WHITE); }
// Settings tile: title + small status line, rounded card.
static void settingsTile(int x, int y, const char *title, const char *status, uint16_t titleColor, uint16_t statusColor, int w = 146) {
  screen.fillRoundRect(x, y, w, 40, 10, CARD);
  fontText(title, x + 12, y + 17, &FreeSansBold9pt7b, titleColor);
  fontText(status, x + 12, y + 34, &FreeSans9pt7b, statusColor);
}

// ---------------------------------------------------------------------------
// Lock In: pick how many hours (left side -, right side +), press START, and a
// timer counts up from 0:00:00 inside a ring that fills towards the target.
// The session survives restarts (start time is saved once the clock knows the
// real time) and keeps running while you look at the clock face.
// ---------------------------------------------------------------------------
enum class LockState : uint8_t { Setup, Running, Done };
static LockState lockState = LockState::Setup;
static int lockHours = 1;
static bool lockActive = false;
static time_t lockStartEpoch = 0;      // real start time (0 = not known yet)
static uint64_t lockStartUptime = 0;   // used until the real time is known
static uint32_t lockTargetSecs = 0, lockShownSecs = UINT32_MAX, lockStopArmedAt = 0, lockLedUntil = 0;
static float lockDrawnDeg = 0;
static GFXcanvas1 lockText(150, 36);   // time text drawn off-screen, then copied: no flicker
static constexpr int RING_X = 160, RING_Y = 128, RING_R = 94, RING_W = 10;
static const uint16_t RING_TRACK = 0x18C4, DONE_GREEN = 0x2E8B;
static int lockEndsShownMinute = -1;

static bool timeKnown() { return time(nullptr) > 1600000000; }
static uint32_t lockElapsed() {
  if (!lockActive) return 0;
  if (lockStartEpoch && timeKnown()) { const time_t now = time(nullptr); return now > lockStartEpoch ? now - lockStartEpoch : 0; }
  return (uptimeMs() - lockStartUptime) / 1000;
}
static void formatDuration(uint32_t secs, char *out, size_t size) {
  snprintf(out, size, "%lu:%02lu:%02lu", (unsigned long)(secs / 3600), (unsigned long)(secs / 60 % 60), (unsigned long)(secs % 60));
}
static void saveLockSession() {
  if (!prefsReady) return;
  if (lockActive && lockStartEpoch) { preferences.putUInt("lockStart", lockStartEpoch); preferences.putUInt("lockSecs", lockTargetSecs); }
  else { preferences.remove("lockStart"); preferences.remove("lockSecs"); }
}
// Ring colour moves from blue at the start to green at the target.
static uint16_t ringColour(float fraction) {
  const int r = 7 + (5 - 7) * fraction, g = 30 + (63 - 30) * fraction, b = 31 + (6 - 31) * fraction;
  return (uint16_t(r) << 11) | (uint16_t(g) << 5) | uint16_t(b);
}
// Thick arc between two angles (degrees, 0 = top, clockwise), drawn as small quads.
static void drawRingArc(float fromDeg, float toDeg, bool gradient, uint16_t colour) {
  for (float a = fromDeg; a < toDeg; a += 2.0f) {
    const float b = min(a + 2.4f, toDeg);  // slight overlap: no gaps between pieces
    const float ra = (a - 90) * DEG_TO_RAD, rb = (b - 90) * DEG_TO_RAD;
    const int ox0 = lroundf(RING_X + cosf(ra) * RING_R), oy0 = lroundf(RING_Y + sinf(ra) * RING_R);
    const int ox1 = lroundf(RING_X + cosf(rb) * RING_R), oy1 = lroundf(RING_Y + sinf(rb) * RING_R);
    const int ix0 = lroundf(RING_X + cosf(ra) * (RING_R - RING_W)), iy0 = lroundf(RING_Y + sinf(ra) * (RING_R - RING_W));
    const int ix1 = lroundf(RING_X + cosf(rb) * (RING_R - RING_W)), iy1 = lroundf(RING_Y + sinf(rb) * (RING_R - RING_W));
    const uint16_t c = gradient ? ringColour(a / 360.0f) : colour;
    screen.fillTriangle(ox0, oy0, ox1, oy1, ix0, iy0, c);
    screen.fillTriangle(ix0, iy0, ox1, oy1, ix1, iy1, c);
  }
}
// Big centred text inside the ring (canvas -> screen in one go).
static void drawRingText(const char *text, const GFXfont *font, int top, uint16_t color = WHITE) {
  lockText.fillScreen(0);
  lockText.setFont(font); lockText.setTextSize(1); lockText.setTextColor(1); lockText.setTextWrap(false);
  int16_t x, y; uint16_t w, h;
  lockText.getTextBounds(text, 0, 28, &x, &y, &w, &h);
  lockText.setCursor((lockText.width() - int(w)) / 2 - x, 28);
  lockText.print(text);
  screen.drawBitmap(RING_X - lockText.width() / 2, top, lockText.getBuffer(), lockText.width(), lockText.height(), fc(color), BG);
}
static void endsAtText(uint32_t secsFromNow, char *out, size_t size) {
  tm local{};
  if (localClock(time(nullptr) + secsFromNow, local)) strftime(out, size, "Ends at %H:%M", &local);
  else out[0] = 0;
}

// ---------------------------------------------------------------------------
// Focus calendar: like GitHub's contribution graph. Every Lock In adds its time
// to the day(s) it ran on; the longer you focused that day, the greener it is.
// Kept for the last 140 days in the settings flash (survives power cuts).
// ---------------------------------------------------------------------------
static constexpr int CAL_DAYS = 140, CAL_WEEKS = 19, CAL_CELL = 11, CAL_PITCH = 14, CAL_X = 36, CAL_Y = 62;
static const uint16_t CAL_LEVEL[5] = {CARD, 0x0A25, 0x0366, 0x2528, 0x3E8A};  // GitHub's greens
struct FocusLog { int32_t lastDay; uint32_t secs[CAL_DAYS]; };
static FocusLog focusLog{};
struct FocusLive { int32_t day; uint32_t secs; };
static FocusLive focusLive[4];
static int focusLiveCount = 0;
static int calShownMinute = -1;

static int32_t daysFromCivil(int y, int m, int d) {  // days since 1970-01-01
  y -= m <= 2;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const int32_t yoe = y - era * 400;
  const int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}
static int monthOfDay(int32_t z) {  // 1..12
  z += 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const int32_t doe = z - era * 146097;
  const int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int32_t mp = (5 * doy + 2) / 153;
  return mp < 10 ? mp + 3 : mp - 9;
}
static bool localDay(time_t utc, int32_t &day, int &secOfDay, int &wday) {
  tm local{};
  if (!localClock(utc, local)) return false;
  day = daysFromCivil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
  secOfDay = local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
  wday = local.tm_wday;
  return true;
}
// Calls add(day, seconds) for each local day the interval touches.
template <typename F> static void splitByDay(time_t start, time_t end, F add) {
  while (start < end) {
    int32_t day; int sec, wday;
    if (!localDay(start, day, sec, wday)) return;
    const time_t chunk = min<time_t>(end - start, max(1, 86400 - sec));
    add(day, uint32_t(chunk));
    start += chunk;
  }
}
static void addFocus(int32_t day, uint32_t secs) {
  if (focusLog.lastDay < day) {
    const int32_t shift = day - focusLog.lastDay;
    if (shift >= CAL_DAYS) memset(focusLog.secs, 0, sizeof(focusLog.secs));
    else {
      memmove(focusLog.secs, focusLog.secs + shift, (CAL_DAYS - shift) * sizeof(uint32_t));
      memset(focusLog.secs + CAL_DAYS - shift, 0, shift * sizeof(uint32_t));
    }
    focusLog.lastDay = day;
  }
  const int32_t back = focusLog.lastDay - day;
  if (back < CAL_DAYS) {
    uint32_t &slot = focusLog.secs[CAL_DAYS - 1 - back];
    slot = min<uint32_t>(86400, slot + secs);
  }
}
// A session ended (finished or reset): add the time it ran to the calendar.
static void creditLockIn(time_t end) {
  if (!lockStartEpoch || !timeKnown()) return;
  end = min<time_t>(end, lockStartEpoch + lockTargetSecs);
  if (end <= lockStartEpoch) return;
  splitByDay(lockStartEpoch, end, addFocus);
  if (prefsReady) preferences.putBytes("focus", &focusLog, sizeof(focusLog));
}
// The session running right now counts too (not saved until it ends).
static void refreshFocusLive() {
  focusLiveCount = 0;
  if (!lockActive || !lockStartEpoch || !timeKnown()) return;
  splitByDay(lockStartEpoch, min<time_t>(time(nullptr), lockStartEpoch + lockTargetSecs), [](int32_t d, uint32_t s) {
    if (focusLiveCount < 4) focusLive[focusLiveCount++] = {d, s};
  });
}
static int32_t calToday = 0;  // set when the calendar is drawn
// Sample history for advertising videos: gets greener week by week, ends in a streak.
static uint32_t demoFocus(int32_t day) {
  int32_t today; int sec, wday;   // today's date (not the calendar's: the Lock In card uses this too)
  if (!localDay(time(nullptr), today, sec, wday)) return 0;
  const int32_t back = today - day;
  if (back < 0 || back >= CAL_WEEKS * 7) return 0;
  if (back == 0) return 2 * 3600 + 15 * 60;
  uint32_t h = uint32_t(day) * 2654435761u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
  const float progress = 1.0f - back / float(CAL_WEEKS * 7);   // 0 = oldest, 1 = today
  const float roll = (h % 1000) / 1000.0f, size = ((h >> 10) % 1000) / 1000.0f;
  if (back > 16 && roll > 0.30f + 0.62f * progress) return 0;  // rest days, rarer lately
  const float hours = 0.4f + (1.2f + 4.2f * progress) * size;
  return uint32_t(hours * 3600) / 300 * 300;
}
static uint32_t focusOn(int32_t day) {
  const int32_t back = focusLog.lastDay - day;
  uint32_t secs = back >= 0 && back < CAL_DAYS ? focusLog.secs[CAL_DAYS - 1 - back] : 0;
  if (DEMO_FOCUS_HISTORY) secs += demoFocus(day);
  for (int i = 0; i < focusLiveCount; ++i) if (focusLive[i].day == day) secs += focusLive[i].secs;
  return min<uint32_t>(secs, 86400);
}
static int streakEndingAt(int32_t today) {  // days in a row with focus (today doesn't break it until it's over)
  int streak = 0;
  for (int32_t d = focusOn(today) ? today : today - 1; d > today - CAL_DAYS && focusOn(d); --d) ++streak;
  return streak;
}
static int focusLevel(uint32_t secs) {
  return secs == 0 ? 0 : secs < 3600 ? 1 : secs < 7200 ? 2 : secs < 14400 ? 3 : 4;  // 0 | <1h | 1-2h | 2-4h | 4h+
}
static void focusText(uint32_t secs, char *out, size_t size) {
  const unsigned long m = secs / 60;
  if (m < 60) snprintf(out, size, "%lum", m);
  else if (m % 60) snprintf(out, size, "%luh %lum", m / 60, m % 60);
  else snprintf(out, size, "%luh", m / 60);
}
static void chevronLeft(int x, int cy, uint16_t color) {
  color = fc(color);
  for (int t = 0; t < 2; ++t) { screen.drawLine(x + 5 + t, cy - 5, x + t, cy, color); screen.drawLine(x + t, cy, x + 5 + t, cy + 5, color); }
}
// Bottom-left "< calendar" button on the Lock In page.
static void calendarChip() {
  screen.fillRoundRect(12, 201, 52, 30, 15, fc(CARD));
  chevronLeft(21, 216, WHITE);
  static const uint8_t levels[6] = {2, 4, 3, 1, 4, 2};
  for (int i = 0; i < 6; ++i) screen.fillRect(33 + (i % 3) * 7, 209 + (i / 3) * 7, 5, 5, fc(CAL_LEVEL[levels[i]]));
}
static void lockInChip() {  // top-right "Lock In >" on the calendar page
  const int w = textWidth("Lock In", &FreeSansBold9pt7b) + 39, x = 312 - w;
  screen.fillRoundRect(x, 8, w, 30, 15, CARD);
  fontText("Lock In", x + 11, 28, &FreeSansBold9pt7b, WHITE);
  chevronRight(x + w - 13, 23, WHITE);
}
static void calStatCard(int x, const char *title, const char *value) {
  screen.fillRoundRect(x, 182, 96, 50, 10, CARD);
  fontText(title, x + 9, 201, &FreeSans9pt7b, CARD_TEXT_DIM);
  const GFXfont *font = textWidth(value, &FreeSansBold12pt7b) <= 80 ? &FreeSansBold12pt7b : &FreeSansBold9pt7b;
  fontText(value, x + 9, 225, font, WHITE);
}
static void drawCalCell(int col, int row, uint32_t secs, bool today) {
  const int x = CAL_X + col * CAL_PITCH, y = CAL_Y + row * CAL_PITCH;
  screen.fillRect(x - 1, y - 1, CAL_CELL + 2, CAL_CELL + 2, BG);
  screen.fillRoundRect(x, y, CAL_CELL, CAL_CELL, 2, CAL_LEVEL[focusLevel(secs)]);
  if (today) screen.drawRoundRect(x - 1, y - 1, CAL_CELL + 2, CAL_CELL + 2, 3, WHITE);
}
// Streak fire, animated: drawn into a small off-screen canvas each frame and copied
// in one go (no flicker). None at 0 days; bigger, hotter and livelier as the streak grows.
static constexpr int FLAME_X = 279, FLAME_Y = 183, FLAME_W = 28, FLAME_H = 24;  // inside the Streak card
static GFXcanvas16 flameCanvas(FLAME_W, FLAME_H);
static int calStreak = 0;
static uint32_t flameFrameAt = 0, flameFrame = 0;
// One flame layer: round bottom + pointed tip (lean > 0 tips it right).
static void flameShape(Adafruit_GFX &g, int cx, int baseY, int h, uint16_t color, int lean = 0) {
  const int r = max(2, h * 3 / 10), tipX = cx + r / 3 + lean;
  g.fillCircle(cx, baseY - r, r, color);
  g.fillTriangle(cx - r, baseY - r, cx + r, baseY - r, tipX, baseY - h, color);
  g.fillTriangle(cx - r, baseY - r, tipX, baseY - h, cx - r / 2 + lean / 2, baseY - h * 2 / 3, color);
}
static void drawStreakFlame(int streak, uint32_t frame, int atX = FLAME_X, int atY = FLAME_Y) {
  if (streak <= 0) return;
  static const uint16_t RED = 0xF820, ORANGE = 0xFC60, YELLOW = 0xFEA0, CORE = 0xFFB6;
  GFXcanvas16 &g = flameCanvas;
  g.fillScreen(CARD);
  const float t = frame * 0.42f;
  // flicker: each layer breathes at its own speed; the tip sways left and right
  auto wob = [&](float speed, float phase, float amount) { return 1.0f + amount * sinf(t * speed + phase) + amount * 0.5f * sinf(t * speed * 2.3f + phase * 1.7f); };
  const int lean = lroundf(1.6f * sinf(t * 0.9f) + 0.8f * sinf(t * 2.1f));
  const int cx = 14, baseY = FLAME_H - 1;
  auto layer = [&](int h, uint16_t c, float speed, float phase, int dx = 0, int extraLean = 0) {
    flameShape(g, cx + dx, baseY, max(3, int(lroundf(h * wob(speed, phase, 0.08f)))), c, lean + extraLean);
  };
  if (streak < 3) {                 // a small, nervous flicker
    layer(10, ORANGE, 1.9f, 0.0f); layer(5, YELLOW, 2.4f, 1.0f);
  } else if (streak < 7) {          // steady flame
    layer(14, ORANGE, 1.5f, 0.0f); layer(8, YELLOW, 2.0f, 1.3f);
  } else if (streak < 14) {         // strong fire
    layer(18, RED, 1.2f, 0.0f); layer(13, ORANGE, 1.7f, 0.9f); layer(7, YELLOW, 2.2f, 2.1f);
  } else {                          // blazing: dancing side tongues, white-hot core, sparks
    layer(11, RED, 1.6f, 2.0f, -6, -3); layer(11, RED, 1.4f, 4.1f, 6, 3);
    layer(21, RED, 1.1f, 0.0f); layer(16, ORANGE, 1.5f, 1.2f); layer(10, YELLOW, 2.0f, 2.4f);
    layer(streak >= 30 ? 6 : 4, CORE, 2.6f, 0.5f);
    for (int k = 0; k < 2; ++k) {   // sparks drifting up
      const uint32_t life = (frame + k * 9) % 18;
      const int sx = cx + ((frame + k * 9) / 18 * 7 + k * 5) % 13 - 6 + int(life) / 6;
      const int sy = baseY - 12 - int(life);
      if (sy >= 0) g.drawPixel(sx, sy, life < 9 ? YELLOW : ORANGE);
    }
  }
  screen.drawRGBBitmap(atX, atY, g.getBuffer(), FLAME_W, FLAME_H);
}
static void serviceStreakFlame() {
  if (page != Page::Calendar || calStreak <= 0 || millis() - flameFrameAt < 66) return;  // ~15 frames/s
  flameFrameAt = millis();
  drawStreakFlame(calStreak, ++flameFrame);
}
// full = whole page; otherwise just today's square and the numbers (live update).
static void drawCalendar(bool full) {
  int32_t today; int sec, wday;
  if (full) { pageTitle("Focus"); lockInChip(); }
  if (!timeKnown() || !localDay(time(nullptr), today, sec, wday)) {
    calShownMinute = -1; calStreak = 0;
    if (full) {
      fontCenter("Your focus calendar starts", 160, 110, &FreeSans9pt7b, MUTED);
      fontCenter("once the clock knows the time.", 160, 132, &FreeSans9pt7b, MUTED);
    }
    return;
  }
  calShownMinute = sec / 60;
  calToday = today;
  refreshFocusLive();
  const int32_t firstDay = today - wday - (CAL_WEEKS - 1) * 7;  // Sunday of the first column
  if (full) {
    screen.setFont(nullptr); screen.setTextSize(1); screen.setTextColor(CARD_TEXT_DIM);
    static const char *const ROW_NAMES[7] = {"", "Mon", "", "Wed", "", "Fri", ""};
    for (int r = 0; r < 7; ++r) if (ROW_NAMES[r][0]) { screen.setCursor(10, CAL_Y + r * CAL_PITCH + 2); screen.print(ROW_NAMES[r]); }
    static const char *const MONTHS[12] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    int lastMonth = monthOfDay(firstDay), lastLabelCol = -9;
    for (int c = 0; c < CAL_WEEKS; ++c) {
      const int month = monthOfDay(firstDay + c * 7 + 6);  // month the week ends in
      if ((c == 0 || month != lastMonth) && c - lastLabelCol >= 3 && c <= CAL_WEEKS - 2) {
        screen.setCursor(CAL_X + c * CAL_PITCH, CAL_Y - 12); screen.print(MONTHS[month - 1]); lastLabelCol = c;
      }
      lastMonth = month;
    }
    for (int c = 0; c < CAL_WEEKS; ++c)
      for (int r = 0; r < 7; ++r) {
        const int32_t day = firstDay + c * 7 + r;
        if (day <= today) drawCalCell(c, r, focusOn(day), day == today);
      }
    // legend: Less [] [] [] [] [] More
    screen.setCursor(176, 168); screen.print("Less");
    for (int i = 0; i < 5; ++i) screen.fillRoundRect(204 + i * 14, 166, CAL_CELL, CAL_CELL, 2, CAL_LEVEL[i]);
    screen.setCursor(276, 168); screen.print("More");
  } else {
    drawCalCell(CAL_WEEKS - 1, wday, focusOn(today), true);
  }
  uint32_t total = 0, week = 0;
  for (int32_t d = firstDay; d <= today; ++d) { const uint32_t s = focusOn(d); total += s; if (d > today - wday - 1) week += s; }
  const int streak = streakEndingAt(today);
  char text[24];
  screen.fillRect(10, 164, 160, 14, BG);
  screen.setFont(nullptr); screen.setTextColor(CARD_TEXT_DIM); screen.setCursor(12, 168);
  focusText(total, text, sizeof(text)); screen.print(text); screen.print(" in 19 weeks");
  focusText(focusOn(today), text, sizeof(text)); calStatCard(12, "Today", text);
  focusText(week, text, sizeof(text)); calStatCard(112, "This week", text);
  snprintf(text, sizeof(text), "%d", streak); calStatCard(212, "Streak", text);  // big number, small "days"
  fontText(streak == 1 ? "day" : "days", screen.getCursorX() + 4, 225, &FreeSans9pt7b, MUTED);
  calStreak = streak;
  drawStreakFlame(streak, flameFrame);
}

static void drawLockSetupHours() {
  screen.fillRect(96, 84, 128, 106, BG);
  char text[8]; snprintf(text, sizeof(text), "%d", lockHours);
  fontCenter(text, RING_X, 142, &FreeSansBold24pt7b, WHITE);
  fontCenter(lockHours == 1 ? "hour" : "hours", RING_X, 166, &FreeSans9pt7b, MUTED);
  char ends[24]; endsAtText(lockHours * 3600UL, ends, sizeof(ends));
  if (ends[0]) fontCenter(ends, RING_X, 186, &FreeSans9pt7b, CARD_TEXT_DIM);
}
static void drawLockStopButton() {
  const bool armed = lockStopArmedAt && millis() - lockStopArmedAt < 3000;
  screen.fillRect(232, 198, 84, 38, BG);
  pill(236, 202, 76, 30, armed ? "Sure?" : "Reset", armed ? DANGER : CARD, WHITE);
}
// Round end on the ring at an angle (degrees from the top).
static void ringCap(float deg, uint16_t color) {
  const float r = (deg - 90) * DEG_TO_RAD, mid = RING_R - RING_W / 2.0f;
  screen.fillCircle(lroundf(RING_X + cosf(r) * mid), lroundf(RING_Y + sinf(r) * mid), RING_W / 2, color);
}
static void drawLockEnds(uint32_t secsLeft) {
  char ends[24]; endsAtText(secsLeft, ends, sizeof(ends));
  screen.fillRect(RING_X - 62, RING_Y + 26, 124, 18, BG);
  if (ends[0]) fontCenter(ends, RING_X, RING_Y + 40, &FreeSans9pt7b, CARD_TEXT_DIM);
}
// ---------------------------------------------------------------------------
// Lock In animations. Start: a padlock snaps shut, the ring sweeps round, the
// timer fades in. Finish: green shockwaves, a checkmark draws itself, then a
// "+5h today" card slides up with this week's squares and the streak fire.
// ---------------------------------------------------------------------------
static float easeOutF(float t) { t = constrain(t, 0.0f, 1.0f); return 1 - (1 - t) * (1 - t) * (1 - t); }
static float easeInF(float t) { t = constrain(t, 0.0f, 1.0f); return t * t; }
static float smoothF(float t) { t = constrain(t, 0.0f, 1.0f); return t * t * (3 - 2 * t); }
static float backOutF(float t, float k = 1.7f) { t = constrain(t, 0.0f, 1.0f) - 1; return t * t * ((k + 1) * t + k) + 1; }
static const uint16_t CHECK_GREEN = 0x472F, EMPTY_DAY = 0x2946;
static constexpr int DONE_CARD_Y = 72, DONE_CARD_H = 150;
static bool lockCelebrate = false;     // play the finish animation on the next draw of the page
static bool doneCardReady = false;     // the streak card has numbers to show
static int doneStreak = 0, doneStreakBefore = 0, doneTodayLevelBefore = 0, doneWday = 0;
static int32_t doneToday = 0;
static uint32_t doneCardFlameAt = 0, doneCardFrame = 0;
static unsigned long doneHours() { return lockTargetSecs / 3600; }
// Works out the card's numbers when a session finishes (it is already in the calendar).
static void prepareDoneCard() {
  doneCardReady = false;
  int sec;
  if (!timeKnown() || !localDay(time(nullptr), doneToday, sec, doneWday)) return;
  refreshFocusLive();
  uint32_t session = 0;
  if (lockStartEpoch) splitByDay(lockStartEpoch, lockStartEpoch + lockTargetSecs, [&](int32_t d, uint32_t secs) { if (d == doneToday) session += secs; });
  const uint32_t today = focusOn(doneToday);
  const bool hadFocusToday = today > session;
  doneTodayLevelBefore = focusLevel(hadFocusToday ? today - session : 0);
  doneStreak = streakEndingAt(doneToday);
  doneStreakBefore = hadFocusToday ? doneStreak : max(0, doneStreak - 1);
  doneCardReady = true;
}
// Checkmark from `from` to `to` (0..1 of the stroke), drawn as a round thick pen.
static void drawCheck(float from, float to, uint16_t color) {
  const float x0 = 122, y0 = 120, x1 = 150, y1 = 148, x2 = 202, y2 = 92;
  const float l1 = hypotf(x1 - x0, y1 - y0), l2 = hypotf(x2 - x1, y2 - y1), total = l1 + l2;
  for (float d = from * total; d <= to * total; d += 1.5f) {
    float x, y;
    if (d <= l1) { x = x0 + (x1 - x0) * d / l1; y = y0 + (y1 - y0) * d / l1; }
    else { x = x1 + (x2 - x1) * (d - l1) / l2; y = y1 + (y2 - y1) * (d - l1) / l2; }
    screen.fillCircle(lroundf(x), lroundf(y), 6, color);
  }
}
static void doneHoursText(char *out, size_t size) {
  snprintf(out, size, "%lu %s locked in", doneHours(), doneHours() == 1 ? "hour" : "hours");
}
static void drawDoneHeader() {
  clockChip();
  screen.fillCircle(21, 25, 4, fc(SUCCESS));
  fontText("Complete", 32, 31, &FreeSansBold9pt7b, WHITE);
}
// The finished ring with its checkmark (uiFade dims it behind the card).
static void drawDoneBase() {
  drawDoneHeader();
  drawRingArc(0, 360, false, fc(DONE_GREEN));
  drawCheck(0, 1, fc(CHECK_GREEN));
  char text[32]; doneHoursText(text, sizeof(text));
  fontCenter(text, RING_X, RING_Y + 56, &FreeSans9pt7b, MUTED);
}
static uint16_t dayColour(int level) { return level ? CAL_LEVEL[level] : EMPTY_DAY; }
static void drawDoneTodaySquare(int y0, int mix256) {
  const int x = 32 + doneWday * 22, y = y0 + 70;
  const uint16_t now = dayColour(focusLevel(focusOn(doneToday)));
  screen.fillRoundRect(x, y, 16, 16, 3, blend565(dayColour(doneTodayLevelBefore), now, mix256));
  screen.drawRoundRect(x - 2, y - 2, 20, 20, 4, WHITE);
}
static void drawDoneStreak(int y0, int streak) {
  screen.fillRect(92, y0 + 108, 150, 36, CARD);
  char text[12]; snprintf(text, sizeof(text), "%d", streak);
  fontText(text, 96, y0 + 138, &FreeSansBold18pt7b, WHITE);
  fontText(streak == 1 ? "day" : "days", screen.getCursorX() + 6, y0 + 138, &FreeSans9pt7b, MUTED);
}
// "+5h today" card: this week's squares (today outlined) and the streak with its fire.
static void drawStreakCard(int y0, int todayMix256, int streakShown) {
  screen.fillRoundRect(16, y0, 288, DONE_CARD_H, 16, CARD);
  char text[24]; snprintf(text, sizeof(text), "+%luh today", doneHours());
  fontText(text, 32, y0 + 36, &FreeSansBold18pt7b, CHECK_GREEN);
  fontText("Added to your focus calendar", 32, y0 + 58, &FreeSans9pt7b, CARD_TEXT_DIM);
  screen.setFont(nullptr); screen.setTextSize(1); screen.setTextColor(CARD_TEXT_DIM);
  for (int i = 0; i < 7; ++i) {
    const int x = 32 + i * 22, y = y0 + 70; const int32_t day = doneToday - doneWday + i;
    if (day > doneToday) screen.drawRoundRect(x, y, 16, 16, 3, EMPTY_DAY);
    else if (day < doneToday) screen.fillRoundRect(x, y, 16, 16, 3, dayColour(focusLevel(focusOn(day))));
    const char letter[2] = {"SMTWTFS"[i], 0};
    screen.setCursor(x + 5, y0 + 92); screen.print(letter);
  }
  drawDoneTodaySquare(y0, todayMix256);
  fontText("Streak", 32, y0 + 136, &FreeSans9pt7b, CARD_TEXT_DIM);
  drawDoneStreak(y0, streakShown);
  drawStreakFlame(streakShown, doneCardFrame, 254, y0 + 114);
}
static void drawDoneHint() {
  centered(screen, "Tap anywhere to close", 228, nullptr, CARD_TEXT_DIM);
}
static void playLockDoneAnimation();
// Running page without the ring: header, "of N hours", Reset (also used for the fade-in).
static void drawLockRunningText() {
  screen.fillCircle(21, 25, 4, fc(PRIMARY));
  fontText("Locked in", 32, 31, &FreeSansBold9pt7b, WHITE);
  char text[24]; snprintf(text, sizeof(text), "of %lu %s", (unsigned long)(lockTargetSecs / 3600), lockTargetSecs == 3600 ? "hour" : "hours");
  fontCenter(text, RING_X, RING_Y + 18, &FreeSans9pt7b, MUTED);
  drawLockStopButton();
}
static void drawLockPage() {
  if (lockState == LockState::Done) {
    if (lockCelebrate) { lockCelebrate = false; playLockDoneAnimation(); return; }
    if (doneCardReady) {  // dimmed ring behind the streak card
      uiFade = 80; drawDoneBase(); uiFade = 255;
      drawStreakCard(DONE_CARD_Y, 256, doneStreak); drawDoneHint();
    } else { drawDoneBase(); pill(236, 202, 76, 30, "OK", PRIMARY, WHITE); }
    return;
  }
  clockChip();
  calendarChip();
  if (lockState == LockState::Setup) {
    pageTitle("Lock In");
    fontCenter("How long will you focus?", RING_X, 66, &FreeSans9pt7b, MUTED);
    screen.fillCircle(56, 128, 30, CARD); screen.fillRoundRect(44, 126, 25, 5, 2, WHITE);  // minus
    screen.fillCircle(264, 128, 30, CARD); screen.fillRoundRect(252, 126, 25, 5, 2, WHITE);
    screen.fillRoundRect(262, 116, 5, 25, 2, WHITE);  // plus
    drawLockSetupHours();
    pill(80, 198, 160, 36, "Start", PRIMARY, WHITE);
    return;
  }
  drawRingArc(0, 360, false, RING_TRACK);
  const uint32_t elapsed = lockElapsed();
  lockDrawnDeg = min(360.0f, 360.0f * elapsed / lockTargetSecs);
  drawRingArc(0, lockDrawnDeg, true, 0);
  ringCap(0, ringColour(0));
  ringCap(lockDrawnDeg, ringColour(lockDrawnDeg / 360.0f));
  drawLockRunningText();
  lockShownSecs = UINT32_MAX;  // forces the time text
  lockEndsShownMinute = -1;
}


// Start: padlock pops in, the shackle snaps shut with a flash, it shrinks
// away while the ring sweeps round, then the page fades in.
// A canvas that draws into memory we lend it (the GIF frame buffer), so no new memory is needed.
struct BorrowedCanvas16 : public GFXcanvas16 {
  BorrowedCanvas16(uint16_t w, uint16_t h, uint16_t *memory) : GFXcanvas16(w, h, false) { buffer = memory; }
};
// Padlock in a box of `box` pixels whose top-left is (ox, oy) on `g`.
static void drawPadlock(Adafruit_GFX &g, int ox, int oy, int box, float scale, float up, float glow, float fade) {
  g.fillRect(ox, oy, box, box, BG);
  if (fade <= 0.01f) return;
  const int f = lroundf(255 * fade);
  const uint16_t body = blend565(BG, blend565(PRIMARY, 0x9E1F, lroundf(256 * glow)), f);
  const uint16_t shackle = blend565(BG, blend565(MUTED, WHITE, lroundf(256 * glow)), f);
  const float cx = ox + box / 2.0f, cy = oy + box / 2.0f;
  const float bw = 58 * scale, bh = 46 * scale, top = cy - bh / 2 + 8 * scale;
  const float sr = 19 * scale, sw = 9 * scale, acy = top - up - 14 * scale;
  g.fillCircle(lroundf(cx), lroundf(acy), lroundf(sr), shackle);                // shackle: top half of a thick ring
  g.fillCircle(lroundf(cx), lroundf(acy), lroundf(sr - sw), BG);
  g.fillRect(lroundf(cx - sr - 1), lroundf(acy), lroundf(2 * sr + 3), lroundf(sr + 2), BG);
  g.fillRect(lroundf(cx + sr - sw), lroundf(acy), lroundf(sw) + 1, lroundf(top + 3 - acy), shackle);   // right leg (hinge)
  const float leftEnd = up >= 1 ? acy + 6 * scale : top + 3;                    // left leg: out of the lock when open
  g.fillRect(lroundf(cx - sr), lroundf(acy), lroundf(sw) + 1, max(1L, lroundf(leftEnd - acy)), shackle);
  g.fillRoundRect(lroundf(cx - bw / 2), lroundf(top), lroundf(bw), lroundf(bh), lroundf(10 * scale), body);
  const float kh = top + bh * 0.38f;                                            // keyhole
  g.fillCircle(lroundf(cx), lroundf(kh), max(1L, lroundf(3.5f * scale)), BG);
  g.fillRect(lroundf(cx - 2 * scale), lroundf(kh), max(1L, lroundf(4 * scale)), lroundf(bh * 0.28f), BG);
}
static void ringHead(float deg, uint16_t color, int r) {
  const float a = (deg - 90) * DEG_TO_RAD, mid = RING_R - RING_W / 2.0f;
  screen.fillCircle(lroundf(RING_X + cosf(a) * mid), lroundf(RING_Y + sinf(a) * mid), r, color);
}
static void playLockStartAnimation() {
  screen.fillRect(80, 198, 160, 36, BG);                       // Start pressed in
  pill(86, 201, 148, 30, "Start", blend565(PRIMARY, BG, 90), WHITE);
  delay(110);
  screen.fillScreen(BG);
  static constexpr int LOCK_BOX = 116;                         // fits inside the ring
  // Drawn off-screen (no flicker): borrow the GIF frame buffer, or get new memory;
  // if neither is possible it is drawn straight onto the screen.
  uint16_t *memory = smallFrame; bool ownMemory = false;
  if (!memory) { memory = static_cast<uint16_t *>(malloc(LOCK_BOX * LOCK_BOX * 2)); ownMemory = memory != nullptr; }
  if (smallFrame) smallFrameIndex = -1;                        // the GIF frame there is overwritten
  BorrowedCanvas16 *lock = memory ? new BorrowedCanvas16(LOCK_BOX, LOCK_BOX, memory) : nullptr;
  Serial.printf("Lock In start animation: padlock drawn %s\n", !lock ? "straight to the screen" : ownMemory ? "in new memory" : "in the GIF buffer");
  const int lx = RING_X - LOCK_BOX / 2, ly = RING_Y - LOCK_BOX / 2;
  int flashR = 0; float sweep = 0; bool lockCleared = false;
  const uint32_t start = millis();
  for (;;) {
    const float t = (millis() - start) / 1000.0f;
    // the "click" flash ring: erase last frame's
    if (flashR) for (int w = 0; w < 3; ++w) screen.drawCircle(RING_X, RING_Y, flashR + w, BG);
    if (flashR && t >= 1.0f && sweep > 0) drawRingArc(0, sweep, false, RING_TRACK);   // repair where it crossed the ring
    flashR = 0;
    if (!lockCleared) {
      const float pop = backOutF(t / 0.35f), drop = easeInF((t - 0.45f) / 0.15f);
      const float bounce = t > 0.6f ? 1 + 0.08f * sinf(min((t - 0.6f) / 0.25f, 1.0f) * PI) : 1;
      const float glow = t > 0.6f ? max(0.0f, 1 - (t - 0.6f) / 0.3f) : 0;
      const float fade = 1 - smoothF((t - 0.95f) / 0.3f);
      float scale = (0.35f + 0.65f * pop) * bounce;
      if (t > 0.95f) scale *= 1 - 0.4f * smoothF((t - 0.95f) / 0.3f);
      if (lock) {
        drawPadlock(*lock, 0, 0, LOCK_BOX, scale, 16 * (1 - drop), glow, fade);
        screen.drawRGBBitmap(lx, ly, lock->getBuffer(), LOCK_BOX, LOCK_BOX);
      } else drawPadlock(screen, lx, ly, LOCK_BOX, scale, 16 * (1 - drop), glow, fade);
      lockCleared = fade <= 0.01f;
    }
    if (t >= 0.6f && t < 1.0f) {
      const float k = (t - 0.6f) / 0.4f;
      flashR = 40 + lroundf(70 * easeOutF(k));
      const uint16_t c = blend565(BG, 0xAE5F, lroundf(255 * (1 - k)));
      for (int w = 0; w < 3; ++w) screen.drawCircle(RING_X, RING_Y, flashR + w, c);
    }
    if (t >= 0.95f) {                                           // the ring sweeps round with a glowing head
      const float deg = 360 * easeOutF((t - 0.95f) / 0.55f);
      if (sweep > 0) ringHead(sweep, BG, 6);
      drawRingArc(max(0.0f, sweep - 14), deg, false, RING_TRACK);
      if (deg < 359.5f) ringHead(deg, 0x8D5F, 5);
      sweep = deg;
    }
    if (t >= 1.5f) break;
    delay(16);
  }
  delete lock;
  if (ownMemory) free(memory);
  ringHead(sweep, BG, 6);
  drawRingArc(0, 360, false, RING_TRACK);
  // The session starts now, when the timer appears (not when Start was tapped),
  // so it counts 0:00:00, 0:00:01, ... without skipping the animation's seconds.
  lockStartUptime = uptimeMs();
  if (lockStartEpoch) lockStartEpoch = time(nullptr);
  saveLockSession();
  char zero[16]; formatDuration(0, zero, sizeof(zero));
  for (int k = 1; k <= 4; ++k) {                               // text and buttons fade in (the ring is already there)
    uiFade = min(255, k * 64);
    clockChip(); calendarChip();
    drawLockRunningText();
    drawRingText(zero, &FreeSansBold18pt7b, RING_Y - 40);
    drawLockEnds(lockTargetSecs);
    delay(20);
  }
  uiFade = 255;
  ringCap(0, ringColour(0));
  lockDrawnDeg = 0; lockShownSecs = 0; lockEndsShownMinute = lockTargetSecs / 60;
}

// Finish: ring flash, three green shockwaves, the checkmark draws itself, then
// the "+5h today" card slides up, today's square fills in and the streak ticks up.
static void playLockDoneAnimation() {
  for (int k = 0; k <= 4; ++k) { drawRingArc(0, 360, false, blend565(0xD7FA, DONE_GREEN, k * 64)); delay(20); }
  int lastR[3] = {0, 0, 0}, lastW[3] = {0, 0, 0};
  uint32_t start = millis();
  for (;;) {
    const float t = (millis() - start) / 1000.0f;
    for (int i = 0; i < 3; ++i) for (int w = 0; w < lastW[i]; ++w) screen.drawCircle(RING_X, RING_Y, lastR[i] + w, BG);
    if (t > 1.25f) break;
    for (int i = 0; i < 3; ++i) {
      const float k = (t - i * 0.25f) / 0.7f; lastW[i] = 0;
      if (k <= 0 || k >= 1) continue;
      lastR[i] = RING_R + 3 + lroundf(124 * easeOutF(k)); lastW[i] = max(1L, lroundf(4 - 3 * k));
      const uint16_t c = blend565(BG, DONE_GREEN, lroundf(230 * (1 - k)));
      for (int w = 0; w < lastW[i]; ++w) screen.drawCircle(RING_X, RING_Y, lastR[i] + w, c);
    }
    delay(16);
  }
  float drawn = 0; start = millis();
  while (drawn < 1) {                                          // the checkmark draws itself
    const float k = easeOutF((millis() - start) / 450.0f);
    drawCheck(drawn, k, CHECK_GREEN); drawn = k;
    delay(16);
  }
  char text[32]; doneHoursText(text, sizeof(text));
  for (int k = 1; k <= 5; ++k) {
    uiFade = k * 51; drawDoneHeader(); fontCenter(text, RING_X, RING_Y + 56, &FreeSans9pt7b, MUTED); delay(40);
  }
  uiFade = 255;
  if (!doneCardReady) { pill(236, 202, 76, 30, "OK", PRIMARY, WHITE); return; }
  delay(700);
  for (const uint8_t f : {190, 130, 80}) { uiFade = f; drawDoneBase(); delay(20); }   // dim behind the card
  uiFade = 255;
  int bottom = HEIGHT;
  for (int step = 1; step <= 12; ++step) {                     // card slides up
    const int y = HEIGHT - lroundf((HEIGHT - DONE_CARD_Y) * easeOutF(step / 12.0f));
    drawStreakCard(y, 0, doneStreakBefore);
    if (y + DONE_CARD_H < bottom) { screen.fillRect(16, y + DONE_CARD_H, 288, bottom - (y + DONE_CARD_H), BG); }
    bottom = min(bottom, y + DONE_CARD_H);
    delay(12);
  }
  drawDoneHint();
  delay(250);
  for (int k = 1; k <= 8; ++k) { drawDoneTodaySquare(DONE_CARD_Y, k * 32); delay(40); }   // today fills in
  const int fx = 254, fy = DONE_CARD_Y + 114;
  if (doneStreak != doneStreakBefore) {                        // streak ticks up, sparkles round the fire
    delay(200);
    drawDoneStreak(DONE_CARD_Y, doneStreak);
    int sx[10], sy[10]; bool shown = false;
    start = millis();
    for (;;) {
      const float k = (millis() - start) / 650.0f;
      if (shown) for (int i = 0; i < 10; ++i) screen.fillCircle(sx[i], sy[i], 2, CARD);
      if (k >= 1) break;
      drawStreakFlame(doneStreak, ++doneCardFrame, fx, fy);
      const uint16_t c = blend565(CARD, 0xFEEF, lroundf(255 * (1 - k)));
      for (int i = 0; i < 10; ++i) {
        const float a = i * 0.63f + 0.3f, r = 16 + 30 * easeOutF(k);
        sx[i] = lroundf(fx + 14 + cosf(a) * r); sy[i] = lroundf(fy + 14 + sinf(a) * r * 0.75f);
        sx[i] = constrain(sx[i], 22, 298); sy[i] = constrain(sy[i], DONE_CARD_Y + 4, DONE_CARD_Y + DONE_CARD_H - 4);
        screen.fillCircle(sx[i], sy[i], 2, c);
      }
      shown = true;
      delay(33);
    }
    drawStreakFlame(doneStreak, ++doneCardFrame, fx, fy);
  }
}

static void startLockIn() {
  lockTargetSecs = lockHours * 3600UL;
  lockStartUptime = uptimeMs();
  lockStartEpoch = timeKnown() ? time(nullptr) : 0;
  lockActive = true; lockState = LockState::Running; lockStopArmedAt = 0;
  if (prefsReady) preferences.putUChar("lockH", lockHours);
  saveLockSession();
  if (page == Page::LockIn) playLockStartAnimation();  // ends on the finished page
  else drawPage();
}
static void stopLockIn() {
  creditLockIn(time(nullptr));
  lockActive = false; lockState = LockState::Setup; lockStopArmedAt = 0;
  saveLockSession();
  drawPage();
}

// Runs every loop: keeps time, fills the ring, finishes the session, blinks the LED.
static void serviceLockIn() {
  if (lockActive) {
    if (!lockStartEpoch && timeKnown()) {  // the real time arrived: anchor the session to it
      lockStartEpoch = time(nullptr) - (uptimeMs() - lockStartUptime) / 1000;
      saveLockSession();
    }
    const uint32_t elapsed = lockElapsed();
    if (elapsed >= lockTargetSecs) {
      creditLockIn(lockStartEpoch + lockTargetSecs);
      lockActive = false; lockState = LockState::Done;
      prepareDoneCard();
      saveLockSession();
      lockLedUntil = millis() + 6000;
      if (page == Page::Clock || page == Page::LockIn || page == Page::Calendar) { lockCelebrate = true; setPage(Page::LockIn); }
    } else if (page == Page::LockIn && lockState == LockState::Running) {
      const float deg = 360.0f * elapsed / lockTargetSecs;
      if (deg - lockDrawnDeg >= 0.5f) {
        drawRingArc(lockDrawnDeg, deg, true, 0);
        ringCap(deg, ringColour(deg / 360.0f));  // rounded leading end
        lockDrawnDeg = deg;
      }
      if (elapsed != lockShownSecs) {
        lockShownSecs = elapsed;
        char text[16]; formatDuration(elapsed, text, sizeof(text));
        drawRingText(text, &FreeSansBold18pt7b, RING_Y - 40);
        const int minute = (lockTargetSecs - elapsed) / 60;
        if (minute != lockEndsShownMinute) { lockEndsShownMinute = minute; drawLockEnds(lockTargetSecs - elapsed); }
      }
      if (lockStopArmedAt && millis() - lockStopArmedAt >= 3000) { lockStopArmedAt = 0; drawLockStopButton(); }
    }
  }
  if (page == Page::LockIn && lockState == LockState::Done && doneCardReady && doneStreak > 0 && millis() - doneCardFlameAt >= 66) {
    doneCardFlameAt = millis(); drawStreakFlame(doneStreak, ++doneCardFrame, 254, DONE_CARD_Y + 114);   // the card's fire keeps burning
  }
  if (page == Page::Calendar && timeKnown()) {  // today's square and numbers follow a running session / midnight
    int32_t day; int sec, wday;
    if (localDay(time(nullptr), day, sec, wday) && sec / 60 != calShownMinute) {
      if (sec < 60 || calShownMinute < 0) drawPage(); else if (lockActive) drawCalendar(false); else calShownMinute = sec / 60;
    }
  }
  // Celebration: the board's green LED (active low) blinks for a few seconds.
  if (lockLedUntil) {
    const bool on = static_cast<int32_t>(lockLedUntil - millis()) > 0;
    digitalWrite(16, on && (millis() / 250) % 2 ? LOW : HIGH);
    if (!on) lockLedUntil = 0;
  }
}

static void drawPage() {
  if (page == Page::Calibration) { drawCalibration(); return; }
  screen.fillScreen(BG);
  if (page == Page::Settings) {
    pageTitle("Settings");
    clockChip();
    // Tiles, 2 x 4
    const bool online = WiFi.status() == WL_CONNECTED;
    const char *zone = zoneIndex >= 0 ? TIME_ZONES[zoneIndex].name : "Choose";
    if (strrchr(zone, '/')) zone = strrchr(zone, '/') + 1;
    char zoneText[32]; snprintf(zoneText, sizeof(zoneText), "%s", zone);
    for (char *c = zoneText; *c; ++c) if (*c == '_') *c = ' ';
    while (strlen(zoneText) > 3 && textWidth(zoneText, &FreeSans9pt7b) > 124) { const size_t n = strlen(zoneText); zoneText[n - 3] = '.'; zoneText[n - 2] = '.'; zoneText[n - 1] = 0; }
    settingsTile(12, 46, "Time zone", zoneText, WHITE, CARD_TEXT_DIM);
    settingsTile(162, 46, "Wi-Fi", !wifiConfigured ? "Off" : online ? "Connected" : "Not connected", WHITE,
                 !wifiConfigured ? CARD_TEXT_DIM : online ? SUCCESS : AMBER);
    settingsTile(12, 92, "Add a GIF", "From phone", WHITE, CARD_TEXT_DIM);
    char quotesText[24]; snprintf(quotesText, sizeof(quotesText), "%d quotes", quoteCount());
    settingsTile(162, 92, "Quotes", quotesText, WHITE, CARD_TEXT_DIM);
    settingsTile(12, 138, "Date & time", wifiConfigured && online ? "Automatic" : "Set by hand", WHITE, CARD_TEXT_DIM);
    settingsTile(162, 138, "Touch", "Calibrate", WHITE, CARD_TEXT_DIM);
    settingsTile(12, 184, "Reset", "Erase everything", DANGER, CARD_TEXT_DIM, 296);  // full width, apart from the rest
  } else if (page == Page::Regions) {
    centered(screen, "CHOOSE TIME ZONE", 10, nullptr, WHITE, 2);
    label("Select a region, then your city / zone", 16, 34, MUTED);
    for (size_t i = 0; i < TIME_ZONE_REGION_COUNT; ++i) {
      button(5 + (i % 3) * 105, 53 + (i / 3) * 35, 100, 30, TIME_ZONE_REGIONS[i]);
    }
    if (!requireZone) button(12, 201, 296, 30, "Back to settings");
    else centered(screen, "Your selection is saved on this board.", 213, nullptr, MUTED);
  } else if (page == Page::Zones) {
    button(6, 5, 48, 30, "Back");
    label(TIME_ZONE_REGIONS[selectedRegion], 68, 14, WHITE, 2);
    for (int row = 0; row < 5; ++row) {
      int index = zoneAtRow(row);
      if (index < 0) break;
      const char *name = TIME_ZONES[index].name;
      const char *slash = strchr(name, '/');
      if (slash) name = slash + 1;
      button(8, 43 + row * 31, 304, 28, "", index == zoneIndex ? 0x2230 : PANEL);
      label(name, 17, 53 + row * 31);
    }
    const int pages = (regionCount() + 4) / 5;
    button(8, 205, 86, 30, "Previous");
    button(226, 205, 86, 30, "Next");
    char message[24];
    snprintf(message, sizeof(message), "%d / %d", zonePage + 1, pages);
    centered(screen, message, 216, nullptr, MUTED);
  } else if (page == Page::ConfirmZone) {
    centered(screen, "USE THIS TIME ZONE?", 22, nullptr, WHITE, 2);
    centered(screen, TIME_ZONES[candidateZone].name, 80, nullptr, ACCENT);
    centered(screen, "Includes local daylight-saving rules", 111, nullptr, MUTED);
    centered(screen, "You can change this in Settings.", 135, nullptr, MUTED);
    button(12, 182, 140, 42, "Back");
    button(168, 182, 140, 42, "Use time zone", 0x2230);
  } else if (page == Page::ConfirmReset) {
    centered(screen, "FACTORY RESET?", 30, nullptr, WHITE, 2);
    centered(screen, "Clear the time zone, Wi-Fi,", 89, nullptr, MUTED);
    centered(screen, "your GIF and touch calibration?", 109, nullptr, MUTED);
    centered(screen, "You can set everything up again.", 140, nullptr, MUTED);
    button(12, 184, 140, 40, "Cancel");
    button(168, 184, 140, 40, "Reset", 0xA985);
  } else if (page == Page::WiFiSetup) {
    drawWiFiSetup();
  } else if (page == Page::Phone) {
    drawPhonePage();
  } else if (page == Page::Calendar) {
    drawCalendar(true);
  } else if (page == Page::LockIn) {
    drawLockPage();
  } else if (page == Page::ManualTime) {
    centered(screen, "SET LOCAL DATE / TIME", 8, nullptr, WHITE, 2);
    centered(screen, zoneIndex >= 0 ? TIME_ZONES[zoneIndex].name : "Select a time zone first",
             32, nullptr, MUTED);
    const char *names[] = {"Year", "Month", "Day", "Hour (24h)", "Minute", "Second"};
    for (int i = 0; i < 6; ++i) {
      int x = 8 + (i%3)*105, y = 62 + (i/3)*60;
      label(names[i], x+5, y-12, MUTED);
      button(x, y, 94, 34, "", i == selectedTimeField ? 0x2230 : PANEL);
      if (i == selectedTimeField) screen.drawRoundRect(x, y, 94, 34, 5, ACCENT);
      char value[8];
      snprintf(value, sizeof(value), i == 0 ? "%04d" : "%02d", *timeField(i));
      label(value, x+(94-strlen(value)*12)/2, y+9, WHITE, 2);
    }
    centered(screen, manualMessage, 167, nullptr, MUTED);
    button(8, 197, 48, 34, "-");
    button(64, 197, 48, 34, "+");
    button(124, 197, 84, 34, "Cancel");
    button(220, 197, 92, 34, "Save time", 0x2230);
  } else if (page == Page::RepeatedTime) {
    centered(screen, "TIME OCCURS TWICE", 24, nullptr, WHITE, 2);
    centered(screen, "A daylight-saving change repeats", 75, nullptr, MUTED);
    centered(screen, "this time. Which occurrence is it?", 94, nullptr, MUTED);
    char value[36];
    snprintf(value, sizeof(value), "%04d-%02d-%02d %02d:%02d:%02d", enteredTime.year,
             enteredTime.month, enteredTime.day, enteredTime.hour, enteredTime.minute, enteredTime.second);
    centered(screen, value, 120, nullptr, ACCENT);
    button(12, 150, 144, 38, "First occurrence");
    button(164, 150, 144, 38, "Second occurrence");
    button(12, 199, 296, 31, "Back");
  }
}

static void beginCalibration(Page returnPage) {
  calibrationStep = 0;
  calibrationReturn = returnPage;
  setPage(Page::Calibration);
}

static bool solveCalibration() {
  const float x0 = calibrationRawX[0], x1 = calibrationRawX[1], x2 = calibrationRawX[2];
  const float y0 = calibrationRawY[0], y1 = calibrationRawY[1], y2 = calibrationRawY[2];
  const float det = x0 * (y1-y2) + x1 * (y2-y0) + x2 * (y0-y1);
  if (fabsf(det) < 100000.0f) return false;
  auto solve = [&](const int *target, float &a, float &b, float &c) {
    a = (target[0]*(y1-y2) + target[1]*(y2-y0) + target[2]*(y0-y1)) / det;
    b = (target[0]*(x2-x1) + target[1]*(x0-x2) + target[2]*(x1-x0)) / det;
    c = (target[0]*(x1*y2-x2*y1) + target[1]*(x2*y0-x0*y2) + target[2]*(x0*y1-x1*y0)) / det;
  };
  solve(calibrationX, calibration.a, calibration.b, calibration.c);
  solve(calibrationY, calibration.d, calibration.e, calibration.f);
  calibration.magic = 0x43414C31;
  calibration.rotation = DISPLAY_ORIENTATION_KEY;
  return true;
}

static void storageError() {
  showingStorageError = true;
  Serial.println("Could not save preferences; selection is not persistent.");
  screen.fillScreen(BG);
  centered(screen, "Could not save settings", 94, nullptr, WHITE, 2);
  centered(screen, "Tap to return and try again.", 130, nullptr, MUTED);
}

static void setClockFace(uint8_t face) {
  clockFace = face % FACE_COUNT;
  customEnabled = clockFace == FACE_GIF;
  if (prefsReady) { preferences.putUChar("face", clockFace); preferences.putBool("custom", customEnabled); }
  currentScene = -1; overlayDirty = true;
}
// GIF on = the GIF face; GIF off = back to the Day/Night face.
static void setCustomEnabled(bool enabled) {
  if (enabled && customAnimation.exists()) setClockFace(FACE_GIF);
  else if (clockFace == FACE_GIF) setClockFace(FACE_SCENES);
}

static void useWithoutWiFi() {
  stopSetupHotspot();
  if (prefsReady) preferences.putBool("offline", true);
  wifiConfigured = false; pendingWiFiSetup = false; wasConnected = false;
  wifiJob = WifiJob::Idle; connectPending = false;
  WiFi.disconnect(true, false); WiFi.mode(WIFI_OFF);
  if (zoneIndex < 0) { requireZone = true; setPage(Page::Regions); }
  else goToClock();
}

static void onTap(uint16_t rawX, uint16_t rawY) {
  if (showingStorageError) {
    showingStorageError = false;
    if (!prefsReady) prefsReady = preferences.begin("dayclock", false);
    drawPage();
    return;
  }
  if (page == Page::Calibration) {
    if (calibrationStep < 3) {
      calibrationRawX[calibrationStep] = rawX;
      calibrationRawY[calibrationStep] = rawY;
      if (++calibrationStep == 3 && !solveCalibration()) calibrationStep = 0;
      drawCalibration();
    } else {
      float x = calibration.a * rawX + calibration.b * rawY + calibration.c;
      float y = calibration.d * rawX + calibration.e * rawY + calibration.f;
      if (fabsf(x - 160) > 22 || fabsf(y - 120) > 22) {
        calibrationStep = 0;
        drawCalibration();
      } else if (prefsReady && preferences.putBytes("touch", &calibration, sizeof(calibration)) == sizeof(calibration)) {
        if (calibrationReturn == Page::Clock) goToClock();
        else setPage(calibrationReturn);
      } else storageError();
    }
    return;
  }
  int x = constrain(lroundf(calibration.a*rawX + calibration.b*rawY + calibration.c), 0L, 319L);
  int y = constrain(lroundf(calibration.d*rawX + calibration.e*rawY + calibration.f), 0L, 239L);
  switch (page) {
    case Page::Clock:  // screens sit side by side: Calendar | Lock In | Day/Night | GIF | Quotes | Settings
      if (clockFace == FACE_QUOTE && x >= 90 && x < 230) { pendingSwipe = SWIPE_UP; break; }  // middle: next quote
      pendingSwipe = x < WIDTH / 2 ? SWIPE_RIGHT : SWIPE_LEFT;  // handled (with the slide) from loop()
      break;
    case Page::LockIn:
      if (inside(x,y,226,0,94,44)) { pendingSwipe = SWIPE_LEFT; break; }  // "Clock >"
      if (lockState != LockState::Done && inside(x,y,0,192,72,48)) { pendingSwipe = SWIPE_RIGHT; break; }  // "< calendar"
      if (lockState == LockState::Setup) {
        if (inside(x,y,74,192,172,48)) startLockIn();
        else if (y > 46 && y < 192 && x < 130) { if (lockHours > 1) { --lockHours; drawLockSetupHours(); } }
        else if (y > 46 && y < 192 && x > 190) { if (lockHours < 24) { ++lockHours; drawLockSetupHours(); } }
      } else if (lockState == LockState::Running) {
        if (inside(x,y,226,192,94,48)) {
          if (lockStopArmedAt && millis() - lockStopArmedAt < 3000) stopLockIn();
          else { lockStopArmedAt = millis(); drawLockStopButton(); }
        }
      } else { lockState = LockState::Setup; doneCardReady = false; drawPage(); }  // finished: tap anywhere to close
      break;
    case Page::Calendar:
      if (inside(x,y,196,0,124,44)) pendingSwipe = SWIPE_LEFT;  // "Lock In >"
      break;
    case Page::Settings:
      if (inside(x,y,226,0,94,44)) pendingSwipe = SWIPE_RIGHT;  // "Clock >" (slides back)
      else if (inside(x,y,12,46,146,44)) { requireZone = false; setPage(Page::Regions); }
      else if (inside(x,y,162,46,146,44)) startWiFiSetup();
      else if (inside(x,y,12,92,146,44)) openPhonePage(false);
      else if (inside(x,y,162,92,146,44)) openPhonePage(false, true);
      else if (inside(x,y,12,138,146,44)) { manualReturn = Page::Settings; setPage(Page::ManualTime); }
      else if (inside(x,y,162,138,146,44)) beginCalibration(Page::Settings);
      else if (inside(x,y,12,184,296,52)) setPage(Page::ConfirmReset);
      break;
    case Page::Regions:
      for (size_t i=0; i<TIME_ZONE_REGION_COUNT; ++i) {
        if (inside(x,y,5+(i%3)*105,53+(i/3)*35,100,30)) {
          selectedRegion = i; zonePage = 0; setPage(Page::Zones); return;
        }
      }
      if (!requireZone && inside(x,y,12,201,296,30)) setPage(Page::Settings);
      break;
    case Page::Zones:
      if (inside(x,y,6,5,48,30)) setPage(Page::Regions);
      else if (inside(x,y,8,205,86,30)) {
        if (zonePage > 0) --zonePage;
        drawPage();
      } else if (inside(x,y,226,205,86,30)) {
        if ((zonePage+1)*5 < regionCount()) ++zonePage;
        drawPage();
      } else {
        for (int row=0; row<5; ++row) {
          if (inside(x,y,8,43+row*31,304,28)) {
            candidateZone = zoneAtRow(row);
            if (candidateZone >= 0) setPage(Page::ConfirmZone);
            break;
          }
        }
      }
      break;
    case Page::ConfirmZone:
      if (inside(x,y,12,182,140,42)) setPage(Page::Zones);
      else if (inside(x,y,168,182,140,42)) {
        if (prefsReady && preferences.putString("zone", TIME_ZONES[candidateZone].name) > 0) {
          zoneIndex = candidateZone; requireZone = false; goToClock();
        } else storageError();
      }
      break;
    case Page::ConfirmReset:
      if (inside(x,y,12,184,140,40)) setPage(Page::Settings);
      else if (inside(x,y,168,184,140,40)) {
        if (prefsReady && preferences.clear()) {
          customAnimation.removeAll();
          remove(QUOTES_PATH);
          WiFi.mode(WIFI_STA); esp_wifi_restore(); // also clears networks saved by older builds
          delay(100); ESP.restart();
        }
        else storageError();
      }
      break;
    case Page::WiFiSetup:
      if (inside(x,y,8,204,148,32)) useWithoutWiFi();
      else if (inside(x,y,164,204,148,32)) {
        stopSetupHotspot();
        setPage(Page::Clock);
      } else if (!apActive && inside(x,y,70,100,180,40)) startWiFiSetup();
      break;
    case Page::Phone:
      if (inside(x,y,8,204,304,32)) {
        if (zoneIndex < 0) { requireZone = true; setPage(Page::Regions); } // phone didn't give one
        else if (phoneForQuotes) setPage(Page::Settings);
        else setPage(Page::Clock);
      } else if (WiFi.status() != WL_CONNECTED && inside(x,y,70,110,180,40)) startWiFiSetup();
      break;
    case Page::ManualTime:
      for (int i=0; i<6; ++i) {
        if (inside(x,y,8+(i%3)*105,62+(i/3)*60,94,34)) {
          selectedTimeField = i; drawPage(); return;
        }
      }
      if (inside(x,y,8,197,48,34)) adjustTimeField(-1);
      else if (inside(x,y,64,197,48,34)) adjustTimeField(1);
      else if (inside(x,y,124,197,84,34)) setPage(manualReturn);
      else if (inside(x,y,220,197,92,34)) {
        LocalTimeResult result = manualLocalToUtc(zoneIndex, enteredTime, earlierTime, &laterTime);
        if (result == LocalTimeResult::Valid) applyManualTime(earlierTime);
        else if (result == LocalTimeResult::Ambiguous) setPage(Page::RepeatedTime);
        else {
          manualMessage = result == LocalTimeResult::Nonexistent ?
            "DST skips this time. Choose another." : "Invalid date or outside supported range.";
          drawPage();
        }
      }
      break;
    case Page::RepeatedTime:
      if (inside(x,y,12,150,144,38)) applyManualTime(earlierTime);
      else if (inside(x,y,164,150,144,38)) applyManualTime(laterTime);
      else if (inside(x,y,12,199,296,31)) setPage(Page::ManualTime);
      break;
    case Page::Calibration: break;
  }
}

static void serviceTouch() {
  const uint32_t now = millis();
  if (now - lastTouchPoll < 18) return;
  lastTouchPoll = now;
  // A contact used to hold through boot must be released before any UI tap.
  if (waitForRelease) {
    if (digitalRead(36) == LOW) releaseStartedAt = 0;
    else if (!releaseStartedAt) releaseStartedAt = now;
    else if (now - releaseStartedAt >= 100) waitForRelease = false;
    return;
  }
  uint16_t x, y;
  if (touch.readRaw(x, y)) {
    if (!touchHeld) { touchHeld = true; touchStartedAt = now; touchSamples = 0; }
    touchSeenAt = now;
    lastRawX = x; lastRawY = y;
    if (++touchSamples == 2) { startRawX = x; startRawY = y; }  // 2nd sample: past the contact bounce
  } else if (touchHeld && now - touchSeenAt > 90) {
    touchHeld = false;
    if (touchSamples < 2 || touchSeenAt - touchStartedAt < 18) return;
    if (page != Page::Calibration && !showingStorageError) {
      // Finger movement in screen pixels, through the touch calibration.
      const float dx = calibration.a * (int(lastRawX) - startRawX) + calibration.b * (int(lastRawY) - startRawY);
      const float dy = calibration.d * (int(lastRawX) - startRawX) + calibration.e * (int(lastRawY) - startRawY);
      const bool quick = touchSeenAt - touchStartedAt < 1200;
      if (quick && fabsf(dx) >= 60 && fabsf(dx) > 1.8f * fabsf(dy)) { pendingSwipe = dx < 0 ? SWIPE_LEFT : SWIPE_RIGHT; return; }
      if (quick && fabsf(dy) >= 50 && fabsf(dy) > 1.8f * fabsf(dx)) { pendingSwipe = dy < 0 ? SWIPE_UP : SWIPE_DOWN; return; }
      if (fabsf(dx) > 30 || fabsf(dy) > 30) return;  // a drag that isn't a swipe: ignore, never a stray tap
    }
    onTap(lastRawX, lastRawY);
  }
}

// Quote list (defined further down), used by the phone page.
static const char *quoteAt(int i);
static bool quotesAreCustom();
static bool saveQuotes(const char *text, String &error);
static void showQuoteNow(int index);
static bool calibrateBattery(float actual);   // battery section, further down
#include "ClockWebServer.h"

static void serviceWiFi() {
  // A network chosen on the phone. Started from loop(), after the web reply was sent.
  if (connectPending && static_cast<int32_t>(millis() - connectAt) >= 0) {
    connectPending = false;
    wifiJob = WifiJob::Connecting;
    connectStartedAt = millis();
    WiFi.scanDelete();
    WiFi.disconnect(false, false);
    delay(50);
    WiFi.begin(pendingSsid.c_str(), pendingPassword.length() ? pendingPassword.c_str() : nullptr);
    if (page == Page::WiFiSetup) drawPage();
  }
  if (wifiJob == WifiJob::Connecting) {
    const wl_status_t status = WiFi.status();
    const uint32_t elapsed = millis() - connectStartedAt;
    if (status == WL_CONNECTED) {
      wifiJob = WifiJob::Ok;
      savedSsid = pendingSsid; savedPassword = pendingPassword; legacySavedWiFi = false;
      if (prefsReady) { preferences.putString("ssid", savedSsid); preferences.putString("pass", savedPassword); }
      pendingPassword = "";
      WiFi.setAutoReconnect(true);
      apCloseAt = millis() + 15000; // Let the phone page show "Done" before the hotspot closes.
      Serial.printf("Joined Wi-Fi \"%s\"\n", savedSsid.c_str());
      if (page == Page::WiFiSetup || page == Page::Clock || page == Page::Settings) openPhonePage(true);
    } else if (elapsed > 25000 || ((status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) && elapsed > 6000)) {
      wifiJob = WifiJob::Fail;
      pendingPassword = "";
      WiFi.disconnect(false, false);
      Serial.println("Joining Wi-Fi failed; setup hotspot stays open.");
      if (page == Page::WiFiSetup) drawPage();
    }
  }
  if (apActive && apCloseAt && static_cast<int32_t>(millis() - apCloseAt) >= 0) {
    stopSetupHotspot();
    if (page == Page::Phone) drawPage();
  }
  if (apActive && !apCloseAt && wifiJob != WifiJob::Connecting && !connectPending &&
      millis() - apStartedAt > SETUP_HOTSPOT_MS && WiFi.softAPgetStationNum() == 0) {
    stopSetupHotspot();
    if (page == Page::WiFiSetup) drawPage();
  }
  if (page == Page::Phone && phoneJustConnected && millis() - phonePageAt > 180000) setPage(Page::Clock);
  if (!wifiConfigured) return;
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !wasConnected) {
    // Keep the radio on steadily: power-save switches it ~10x a second, which can
    // make the backlight dip on this board.
    WiFi.setSleep(false);
    // NTP sets UTC; the IANA table converts it to local time with exact rules.
    configTime(0, 0, "pool.ntp.org", "time.google.com", "time.nist.gov");
    Serial.printf("Wi-Fi connected (%s); requesting UTC from NTP.\n", WiFi.localIP().toString().c_str());
  }
  if (connected != wasConnected) {
    wasConnected = connected; overlayDirty = true;
    if (page == Page::Settings || page == Page::Phone) drawPage();
  }
  if (!apActive && !connected && haveSavedWiFi() && millis() - lastWifiRetry >= 30000) {
    lastWifiRetry = millis();
    if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
    beginSavedWiFi();
  }
}

static void serviceWeb() {
  if (!webReady || WiFi.getMode() == WIFI_OFF) return;
  if (apActive) clockDns.processNextRequest();
  if (apActive || WiFi.status() == WL_CONNECTED) clockServer.handleClient();
}

static bool openMovie(int scene) {
  movie = &CLOCK_ANIMATIONS[scene];
  uint16_t width = 0, height = 0;
  if (!movie->frames || !movie->frameCount || !movie->fps || movie->fps > 30 ||
      TJpgDec.getJpgSize(&width, &height, movie->frames[0].data, movie->frames[0].size) != JDR_OK ||
      width != WIDTH || height != HEIGHT) {
    movie = nullptr; mediaStatus = "Invalid embedded animation"; return false;
  }
  movieFrames = movie->frameCount; movieFps = movie->fps;
  movieStart = uptimeMs(); lastFrame = UINT32_MAX;
  mediaStatus = "";
  Serial.printf("Playing embedded %s: %lu frames at %u fps target\n", sceneNames[scene],
                static_cast<unsigned long>(movieFrames), movieFps);
  return true;
}

// ---------------------------------------------------------------------------
// Clock faces: Day/Night | GIF | Quotes, side by side (tap left / right).
// ---------------------------------------------------------------------------
static const char *const QUOTES[] = {
  "Scared money ain't make money", "No risk, no Ferrari", "Risk is always better than regret", "You vs You", "Time is ticking",
  "Stay hungry, stay humble", "Discipline beats motivation", "Dream big, work bigger", "Small steps every day",
  "Done is better than perfect", "Make it happen", "Prove them wrong", "Hustle in silence",
  "Let your results talk", "Grind now, shine later", "Winners never quit", "Comfort is the enemy",
  "Be obsessed or be average", "Work hard, stay humble", "Results, not excuses", "One hour at a time",
  "Stay locked in", "Earn it every day", "Do it for future you", "No days off",
  "Outwork everyone", "Consistency is key", "Less talk, more action", "Your only limit is you",
  "Keep going", "Start now, not later", "Pressure makes diamonds", "Make today count",
  "Rise and grind", "Built, not born", "Chase goals, not people", "Silence the noise",
  "Be the hardest worker in the room", "Stop wishing, start doing", "Progress over perfection", "Bet on yourself",
  "Level up", "Stay patient, stay focused", "Hard work pays off", "Mindset is everything",
  "Win the morning, win the day", "Fall seven times, stand up eight", "Every second counts", "Get 1% better every day",
  "Eyes on the prize", "Trust the process", "Make your future self proud", "The best view comes after the hardest climb",
  "Don't stop until you're proud", "Wake up. Work. Repeat.", "Your time is now", "Be stronger than your excuses",
  "Focus. Execute. Repeat.", "Doubt kills more dreams than failure ever will", "Stay down until you come up", "Nobody is coming to save you",
  "Work while they sleep", "Good times don't last forever, but neither do bad times", "Losing is a lesson, quitting is a choice", "Small wins add up", "Be so good they can't ignore you",
};
static constexpr int BUILTIN_QUOTE_COUNT = sizeof(QUOTES) / sizeof(QUOTES[0]);
// Your own list (edited on the phone page) lives in the clock's storage; without it the built-in list is used.
static char *customQuoteText = nullptr;          // the file, one quote per line (lines end in '\0')
static const char *customQuotes[MAX_QUOTES];
static int customQuoteCount = 0;
static int quoteCount() { return customQuoteCount ? customQuoteCount : BUILTIN_QUOTE_COUNT; }
static const char *quoteAt(int i) { return customQuoteCount ? customQuotes[i] : QUOTES[i]; }
// Cleans one line: ASCII the fonts can draw, no surrounding quote marks, single spaces, max length.
static void cleanQuote(const char *in, char *out, size_t size) {
  size_t n = 0; bool space = false;
  for (const char *p = in; *p && n + 1 < size; ++p) {
    const unsigned char c = *p;
    if (c == ' ' || c == '\t') { space = n > 0; continue; }
    if (c < 32 || c > 126) continue;
    if (space && n + 2 < size) out[n++] = ' ';
    space = false; out[n++] = c;
  }
  out[n] = 0;
  while (n && (out[n - 1] == '"' || out[n - 1] == ' ')) out[--n] = 0;  // the clock adds its own marks
  size_t start = 0; while (out[start] == '"' || out[start] == ' ') ++start;
  if (start) memmove(out, out + start, n - start + 1);
}
static bool quotesAreCustom() { return customQuoteCount > 0; }
static void freeCustomQuotes() { free(customQuoteText); customQuoteText = nullptr; customQuoteCount = 0; }
// Parses newline-separated text into the custom list (takes ownership of `text`).
static void useCustomQuoteText(char *text) {
  freeCustomQuotes();
  customQuoteText = text;
  for (char *line = text; line && *line && customQuoteCount < MAX_QUOTES;) {
    char *end = strchr(line, '\n');
    if (end) *end = 0;
    if (*line) customQuotes[customQuoteCount++] = line;
    line = end ? end + 1 : nullptr;
  }
  if (!customQuoteCount) freeCustomQuotes();
}
static void loadQuotes() {
  FILE *f = fopen(QUOTES_PATH, "rb");
  if (!f) return;
  fseek(f, 0, SEEK_END); const long size = ftell(f); fseek(f, 0, SEEK_SET);
  if (size > 0 && size < MAX_QUOTES * (MAX_QUOTE_LEN + 2)) {
    char *text = static_cast<char *>(malloc(size + 1));
    if (text) { text[fread(text, 1, size, f)] = 0; useCustomQuoteText(text); }
  }
  fclose(f);
  Serial.printf("Quotes: %d (%s)\n", quoteCount(), customQuoteCount ? "your own list" : "built-in");
}
// Saves a new list (newline separated). Empty text = back to the built-in quotes.
static bool saveQuotes(const char *text, String &error) {
  const size_t limit = MAX_QUOTES * (MAX_QUOTE_LEN + 2);
  char *clean = static_cast<char *>(malloc(limit + 1));
  if (!clean) { error = "Not enough memory."; return false; }
  size_t used = 0; int count = 0;
  for (const char *line = text; *line && count < MAX_QUOTES;) {
    const char *end = strchr(line, '\n'); const size_t len = end ? size_t(end - line) : strlen(line);
    char raw[200]; snprintf(raw, sizeof(raw), "%.*s", int(min(len, sizeof(raw) - 1)), line);
    char q[MAX_QUOTE_LEN + 1]; cleanQuote(raw, q, sizeof(q));
    if (q[0]) { const size_t n = strlen(q); memcpy(clean + used, q, n); used += n; clean[used++] = '\n'; ++count; }
    line = end ? end + 1 : line + len;
  }
  clean[used] = 0;
  if (!count) { free(clean); remove(QUOTES_PATH); freeCustomQuotes(); return true; }
  FILE *f = fopen(QUOTES_PATH, "wb");
  const bool ok = f && fwrite(clean, 1, used, f) == used;
  if (f) fclose(f);
  if (!ok) { free(clean); remove(QUOTES_PATH); error = "Could not save the quotes (storage full?)."; return false; }
  useCustomQuoteText(clean);
  return true;
}
static long quoteBaseFor(const tm &local) {  // mixes the order so neighbours in the list don't follow each other
  const int n = quoteCount();
  int step = 37; while (step > 1) { int a = step, b = n; while (b) { const int t = a % b; a = b; b = t; } if (a == 1) break; --step; }
  const long hourNumber = long(daysFromCivil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday)) * 24 + local.tm_hour;
  return (hourNumber % n) * step % n;
}
static int quoteIndexFor(time_t utc, const tm &local) {
  (void)utc;
  const int n = quoteCount();
  return int(((quoteBaseFor(local) + quoteOffset) % n + n) % n);  // a different one every hour
}
// Puts quote `index` on the clock now (the phone's "Show" button); the hourly change carries on from it.
static void showQuoteNow(int index) {
  tm local{};
  if (localClock(time(nullptr), local)) quoteOffset = index - int(quoteBaseFor(local));
  if (clockFace != FACE_QUOTE) setClockFace(FACE_QUOTE);
  overlayDirty = true;
  if (page == Page::Phone || page == Page::Settings) setPage(Page::Clock);
}
// Splits `text` into lines no wider than maxW; returns the line count (-1 if a word alone is too wide).
static int wrapLines(Adafruit_GFX &g, const char *text, int maxW, char lines[][100], int maxCount) {
  int count = 0; char current[100] = "";
  for (const char *p = text; *p;) {
    while (*p == ' ') ++p;
    const char *end = p; while (*end && *end != ' ') ++end;
    if (end == p) break;
    char word[96]; snprintf(word, sizeof(word), "%.*s", int(end - p), p); p = end;
    char test[100]; snprintf(test, sizeof(test), current[0] ? "%s %s" : "%s%s", current, word);
    int16_t x, y; uint16_t w, h; g.getTextBounds(test, 0, 100, &x, &y, &w, &h);
    if (w <= maxW) { strcpy(current, test); continue; }
    if (!current[0]) return -1;
    if (count >= maxCount - 1) return maxCount + 1;
    strcpy(lines[count++], current);
    snprintf(current, sizeof(current), "%s", word);
    g.getTextBounds(current, 0, 100, &x, &y, &w, &h);
    if (w > maxW) return -1;
  }
  if (current[0]) strcpy(lines[count++], current);
  return count;
}
// Draws `text` big and centred around `centerY`, wrapped into evenly long lines.
static void drawWrapped(Adafruit_GFX &g, const char *text, int centerY, int maxW) {
  const GFXfont *fonts[3] = {&FreeSansBold18pt7b, &FreeSansBold12pt7b, &FreeSans9pt7b};
  const int spacing[3] = {34, 26, 20}, maxLines[3] = {3, 4, 6};
  static char lines[6][100], best[6][100];
  for (int f = 0; f < 3; ++f) {
    g.setFont(fonts[f]); g.setTextSize(1); g.setTextWrap(false);
    const int count = wrapLines(g, text, maxW, best, 6);
    if (count < 1 || count > maxLines[f]) continue;
    // balance: narrow the box while the line count stays the same (no lonely last word)
    for (int w = maxW - 6; w > 60; w -= 6) {
      if (wrapLines(g, text, w, lines, 6) != count) break;
      memcpy(best, lines, sizeof(lines));
    }
    const int top = centerY - (count - 1) * spacing[f] / 2 + (f == 0 ? 12 : f == 1 ? 9 : 6);  // baseline of the first line
    for (int i = 0; i < count; ++i) centered(g, best[i], top + i * spacing[f], fonts[f], 1);
    return;
  }
  centered(g, text, centerY, &FreeSans9pt7b, 1);  // last resort (never needed for the built-in list)
}

// Quote text has its own layer so it can fade: out (old quote), then in (new quote).
static constexpr int QUOTE_LAYER_Y = 20, QUOTE_LAYER_H = 150;      // screen lines 20..169
static constexpr uint32_t QUOTE_FADE_OUT_MS = 300, QUOTE_FADE_IN_MS = 450;
static GFXcanvas1 *quoteLayer = nullptr;   // made the first time the Quote screen is shown (memory is tight during Wi-Fi setup)
static char quoteLayerText[100] = "", quoteWanted[100] = "";
static enum : uint8_t { QUOTE_STILL, QUOTE_OUT, QUOTE_IN } quoteFade = QUOTE_STILL;
static uint32_t quoteFadeAt = 0;
static int quoteAlpha = 256;   // 0 = invisible, 256 = full white
static bool quoteFading() { return quoteFade != QUOTE_STILL; }
// Picks the quote to show and moves the fade along; called while building the clock text.
static void updateQuoteLayer(const char *wanted) {
  snprintf(quoteWanted, sizeof(quoteWanted), "%s", wanted);
  if (!quoteLayer) {
    quoteLayer = new GFXcanvas1(WIDTH, QUOTE_LAYER_H);
    if (!quoteLayer->getBuffer()) { delete quoteLayer; quoteLayer = nullptr; return; }
  }
  const uint32_t now = millis();
  if (quoteFade == QUOTE_STILL && strcmp(quoteWanted, quoteLayerText) != 0) {
    quoteFade = quoteLayerText[0] ? QUOTE_OUT : QUOTE_IN; quoteFadeAt = now;
    if (quoteFade == QUOTE_IN) { strcpy(quoteLayerText, quoteWanted); quoteLayer->fillScreen(0); drawWrapped(*quoteLayer, quoteLayerText, 92 - QUOTE_LAYER_Y, 300); }
  }
  auto ease = [](float t) { t = constrain(t, 0.0f, 1.0f); return t * t * (3 - 2 * t); };
  if (quoteFade == QUOTE_OUT) {
    const float t = float(now - quoteFadeAt) / QUOTE_FADE_OUT_MS;
    quoteAlpha = lroundf(256 * (1 - ease(t)));
    if (t >= 1) {  // old one gone: draw the new one, invisible, then fade it in
      strcpy(quoteLayerText, quoteWanted);
      quoteLayer->fillScreen(0); drawWrapped(*quoteLayer, quoteLayerText, 92 - QUOTE_LAYER_Y, 300);
      quoteFade = QUOTE_IN; quoteFadeAt = now; quoteAlpha = 0;
    }
  } else if (quoteFade == QUOTE_IN) {
    const float t = float(now - quoteFadeAt) / QUOTE_FADE_IN_MS;
    quoteAlpha = lroundf(256 * ease(t));
    if (t >= 1) { quoteFade = QUOTE_STILL; quoteAlpha = 256; }
  }
}

// ---------------------------------------------------------------------------
// Battery (battery build only): level from an IP5306 power module, drawn in the
// bottom-right corner of the clock screens. Read every 10 s, smoothed.
// ---------------------------------------------------------------------------
static int percentFromVolts(float v) {  // Li-ion while powering the clock (~200 mA): reads a bit lower than at rest
  static const float V[] = {3.30f, 3.42f, 3.55f, 3.61f, 3.65f, 3.69f, 3.73f, 3.79f, 3.86f, 3.94f, 4.04f, 4.12f};
  static const int P[] =   {0,     5,     10,    20,    30,    40,    50,    60,    70,    80,    90,    100};
  if (v <= V[0]) return 0;
  for (int i = 1; i < 12; ++i)
    if (v < V[i]) return P[i - 1] + int((P[i] - P[i - 1]) * (v - V[i - 1]) / (V[i] - V[i - 1]) + 0.5f);
  return 100;
}
static bool ip5306Read(uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(0x75); Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(0x75, 1) != 1) return false;
  value = Wire.read(); return true;
}
// Charging without a charger signal (resistor build, CHARGE_DETECT_PIN = -1): plugging in the
// charger lifts the battery voltage by roughly 0.1-0.3 V within seconds, unplugging drops it again,
// and while charging it never falls. Readings every 2 s, compared over the last ~12 s and ~5 min.
static constexpr float CHARGE_STEP_V = 0.06f;
static float chargeOffset = 0;         // how much charging lifts the voltage (taken off for the %)
static float batteryUserCal = 1.0f;    // set from the phone page ("battery is full" or a multimeter reading), saved
static float batteryRawVolts() { return batteryVolts > 0 ? batteryVolts / batteryUserCal : 0; }
// Calibrate so the current reading becomes `actual` volts. Returns false if that's not believable.
static bool calibrateBattery(float actual) {
  const float raw = batteryRawVolts();
  if (raw <= 0 || actual < 3.0f || actual > 4.3f) return false;
  const float cal = actual / raw;
  if (cal < 0.90f || cal > 1.10f) return false;   // the ESP32's reading is never off by more than this
  batteryVolts = actual; batteryUserCal = cal;
  // Own storage area: a factory reset (Settings > Reset) keeps the battery calibration.
  Preferences cal_; if (cal_.begin("battery", false)) { cal_.putFloat("cal", cal); cal_.end(); }
  batteryPercent = percentFromVolts(batteryCharging ? actual - chargeOffset * fminf(1.0f, fmaxf(0.0f, (4.20f - actual) / 0.15f)) : actual);
  overlayDirty = true;
  Serial.printf("Battery calibrated: x%.3f, now %.2f V = %d%%\n", cal, actual, batteryPercent);
  return true;
}
static void serviceBattery() {
  if (!BATTERY_INDICATOR) return;
  const uint32_t period = BATTERY_SOURCE == 1 ? 5000 : 2000;
  if (batteryReadAt && millis() - batteryReadAt < period) return;
  const bool first = !batteryReadAt;
  batteryReadAt = millis() | 1;
  int percent = -1; bool charging = batteryCharging;
  if (BATTERY_SOURCE == 1) {             // IP5306 with I2C: 4 steps (25 % each) + charging flag
    static bool wireReady = false;
    if (!wireReady) { Wire.begin(27, 22, 100000); wireReady = true; }
    uint8_t level, status;
    charging = false;
    if (ip5306Read(0x78, level)) {
      switch (level & 0xF0) { case 0x00: percent = 100; break; case 0x80: percent = 75; break;
                              case 0xC0: percent = 50; break; case 0xE0: percent = 25; break; default: percent = 0; }
      if (ip5306Read(0x70, status)) charging = status & 0x08;
    }
  } else {                               // voltage divider into an ADC pin
    if (first) {
      analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);
      if (CHARGE_DETECT_PIN >= 0) pinMode(CHARGE_DETECT_PIN, INPUT_PULLDOWN);
    }
    uint32_t mv = 0;
    for (int i = 0; i < 32; ++i) mv += analogReadMilliVolts(BATTERY_ADC_PIN);
    const float v = mv / 32000.0f * BATTERY_DIVIDER * BATTERY_CALIBRATION * batteryUserCal;
    static float recent[6];              // last 12 s, every 2 s
    static float minutes[6];             // last 5 min, every minute
    static int recentN = 0, minutesN = 0;
    static uint32_t minuteAt = 0;
    if (v > 2.8f && v < 4.6f) {          // outside this: nothing connected
      if (CHARGE_DETECT_PIN >= 0) {
        charging = digitalRead(CHARGE_DETECT_PIN) == HIGH;
        if (charging && !batteryCharging) chargeOffset = 0.10f;
      } else if (recentN) {
        float lo = recent[0], hi = recent[0];
        for (int i = 1; i < recentN; ++i) { lo = fminf(lo, recent[i]); hi = fmaxf(hi, recent[i]); }
        bool jumped = false;
        if (!charging && v - lo >= CHARGE_STEP_V) {          // plugged in
          charging = true; jumped = true;
          chargeOffset = fminf(0.25f, fmaxf(0.05f, v - lo));
        } else if (charging && hi - v >= CHARGE_STEP_V) {    // unplugged
          charging = false; jumped = true;
        }
        if (jumped) { recentN = 0; minutesN = 0; batteryVolts = 0; }   // start the averages again
        // slower signs: rising every minute for 5 minutes = charging (e.g. switched on while plugged in);
        // falling for 5 minutes = not charging (e.g. unplugged when already full)
        if (!jumped && minutesN == 6) {
          // trend over the last 5 minutes (least squares, volts per minute): ignores the small wobble
          float sum = 0, sumXY = 0;
          for (int i = 0; i < 6; ++i) { sum += minutes[i]; sumXY += (i - 2.5f) * minutes[i]; }
          const float slope = sumXY / 17.5f;
          if (!charging && slope >= 0.005f && minutes[5] - minutes[0] >= 0.025f) { charging = true; chargeOffset = 0.10f; }
          else if (charging && slope <= -0.003f && minutes[5] < 4.10f) charging = false;  // (not when full: it settles a little)
        }
      }
      if (recentN == 6) { memmove(recent, recent + 1, 5 * sizeof(float)); recentN = 5; }
      recent[recentN++] = v;
      if (!minuteAt || millis() - minuteAt >= 60000) {
        minuteAt = millis() | 1;
        if (minutesN == 6) { memmove(minutes, minutes + 1, 5 * sizeof(float)); minutesN = 5; }
        minutes[minutesN++] = batteryVolts > 0 ? batteryVolts : v;
      }
      batteryVolts = batteryVolts > 0 ? batteryVolts * 0.85f + v * 0.15f : v;
      // charging lifts the voltage: take that off so the % doesn't jump up when plugged in
      // (the lift shrinks to nothing as the battery gets full at 4.2 V, so it still reaches 100 %)
      const float lift = chargeOffset * fminf(1.0f, fmaxf(0.0f, (4.20f - batteryVolts) / 0.15f));
      percent = percentFromVolts(charging ? batteryVolts - lift : batteryVolts);
      if (charging && batteryPercent >= 0 && percent < batteryPercent && percent > batteryPercent - 5) percent = batteryPercent;  // ignore small dips while charging
    } else { batteryVolts = 0; charging = false; recentN = minutesN = 0; }
  }
  // small changes are ignored so the number doesn't flicker between two values
  if (percent != batteryPercent && (percent < 0 || batteryPercent < 0 || abs(percent - batteryPercent) >= 2 ||
                                    percent == 0 || percent == 100 || charging != batteryCharging)) {
    batteryPercent = percent; overlayDirty = true;
  }
  if (charging != batteryCharging) {
    batteryCharging = charging; overlayDirty = true;
    Serial.println(charging ? "Battery: charger plugged in" : "Battery: charger unplugged");
  }
  if (millis() - batteryLoggedAt > 60000 || first) {
    batteryLoggedAt = millis();
    if (batteryPercent < 0) Serial.println("Battery: no reading (check the wiring / BATTERY_SOURCE in Config.h)");
    else if (BATTERY_SOURCE == 1) Serial.printf("Battery: %d%%%s\n", batteryPercent, batteryCharging ? " (charging)" : "");
    else Serial.printf("Battery: %.2f V = %d%%%s\n", batteryVolts, batteryPercent, batteryCharging ? " (charging)" : "");
  }
}
// Bottom-right corner: "87%" and a battery icon filled to the level (white, on the clock's text layer).
static void drawBatteryOnMask(GFXcanvas1 &g, time_t utc) {
  if (!BATTERY_INDICATOR) return;
  const int x = 287, y = 226;            // icon body 22 x 11, tip on the right
  g.drawRoundRect(x, y, 22, 11, 2, 1); g.drawRoundRect(x + 1, y + 1, 20, 9, 2, 1);
  g.fillRect(x + 22, y + 3, 3, 5, 1);
  const bool low = batteryPercent >= 0 && batteryPercent <= 15 && !batteryCharging;
  int fill = batteryPercent > 0 ? (16 * batteryPercent + 50) / 100 : 0;
  if (batteryCharging && fill < 16) fill += (16 - fill) * int(utc % 4) / 3;   // charging: the level fills up, again and again
  if (!(low && (utc & 1))) {             // low: the level blinks
    if (fill > 0) g.fillRect(x + 3, y + 3, fill, 5, 1);
  }
  char text[8];
  if (batteryPercent < 0) strcpy(text, "--");   // no reading: check the sense wire (see BATTERY.md)
  else snprintf(text, sizeof(text), "%d%%", batteryPercent);
  g.setFont(nullptr); g.setTextSize(1); g.setTextColor(1); g.setTextWrap(false);
  int tx = x - 4 - int(strlen(text)) * 6;
  g.setCursor(tx, y + 2); g.print(text);
  if (batteryCharging) {                 // small lightning bolt before the number
    const int bx = tx - 8, by = y;
    g.fillTriangle(bx + 4, by, bx, by + 6, bx + 3, by + 6, 1);
    g.fillTriangle(bx + 3, by + 5, bx + 6, by + 5, bx + 2, by + 11, 1);
  }
}

static void buildClockMask(time_t utc, bool valid, const tm &local, int scene) {
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (!overlayDirty && utc == lastMaskTime && scene == lastMaskScene && connected == lastMaskConnected && clockFace == lastMaskFace) {
    if (clockFace != FACE_QUOTE || !quoteFading()) return;
  }
  lastMaskTime = utc; lastMaskScene = scene; lastMaskConnected = connected; lastMaskFace = clockFace;
  clockMask.fillScreen(0);
  char title[48], clockText[16], dateText[48];
  if (valid) {
    const bool offline = wifiConfigured && !connected;
    const char *name = sceneNames[scene];
    snprintf(title, sizeof(title), "%s%s%s", name, offline && name[0] ? "  /  " : "", offline ? "OFFLINE" : "");
    strftime(clockText, sizeof(clockText), "%H:%M:%S", &local);
    strftime(dateText, sizeof(dateText), "%a, %d %b %Y", &local);
  } else {
    snprintf(title, sizeof(title), "%s", connected ? "SYNCING TIME" : "SET THE CLOCK");
    strcpy(clockText, "--:--:--");
    snprintf(dateText, sizeof(dateText), "Swipe left: Settings > Wi-Fi setup");
    if (utc >= TIME_ZONE_VALID_UNTIL) strcpy(dateText, "Update the time-zone database");
  }
  if (clockHint && static_cast<int32_t>(clockHintUntil - millis()) > 0) snprintf(title, sizeof(title), "%s", clockHint);
  else {
    clockHint = nullptr;
    if (lockActive) {  // a running Lock In shows on the clock face too
      char elapsed[16]; formatDuration(lockElapsed(), elapsed, sizeof(elapsed));
      snprintf(title, sizeof(title), "LOCKED IN  %s / %luH", elapsed, (unsigned long)(lockTargetSecs / 3600));
    }
  }
  if (clockFace == FACE_GIF && !customAnimation.exists() && !clockHint && !lockActive) snprintf(title, sizeof(title), "NO GIFS YET - SETTINGS > ADD A GIF");
  if (clockFace == FACE_QUOTE) {  // big quote in the middle, the time underneath
    overlayTop = 0;
    centered(clockMask, title, 8, nullptr, 1);
    {  // "Quote." with quotation marks and a full stop
      const int count = quoteCount();
      const char *q = quoteAt(valid ? quoteIndexFor(utc, local) : ((quoteOffset % count) + count) % count);  // taps work even before the time is set
      const size_t n = strlen(q);
      const bool ends = n && strchr(".!?", q[n - 1]);
      char shown[100]; snprintf(shown, sizeof(shown), "\"%s%s\"", q, ends ? "" : ".");
      updateQuoteLayer(shown);  // drawn from its own layer, with the fade
    }
    if (valid) strftime(clockText, sizeof(clockText), "%H:%M", &local);
    centered(clockMask, clockText, 204, &FreeSansBold24pt7b, 1);
    drawBatteryOnMask(clockMask, utc);
    if (valid) centered(clockMask, dateText, 228, &FreeSans9pt7b, 1);
    else centered(clockMask, dateText, 222, nullptr, 1);
  } else {
    overlayTop = OVERLAY_Y;
    centered(clockMask, title, OVERLAY_Y + 17, nullptr, 1);
    centered(clockMask, clockText, OVERLAY_Y + 70, &FreeSansBold24pt7b, 1);
    drawBatteryOnMask(clockMask, utc);
    if (valid) centered(clockMask, dateText, OVERLAY_Y + 94, &FreeSans9pt7b, 1);
    else centered(clockMask, dateText, OVERLAY_Y + 88, nullptr, 1);
    if (!movie && scene != CUSTOM_SCENE) centered(clockMask, mediaStatus, OVERLAY_Y + 102, nullptr, 1);
  }
  overlayDirty = true; // Keep this set until a frame containing the new mask is drawn.
}

static void compositeBlock(int left, int top, int width, int rows, uint16_t *data) {
  const uint8_t *mask = clockMask.getBuffer();
  for (int row = 0; row < rows; ++row) {
    const int y = top + row;
    if (y < overlayTop) continue;
    const int maskY = y;
    const int fromTop = y - overlayTop;
    const int keep = overlayTop == 0 ? 104 : fromTop < 16 ? 256 - fromTop * 10 : 96;  // Quotes face: whole sky dimmed
    const uint8_t *quoteRow = clockFace == FACE_QUOTE && quoteLayer && quoteAlpha > 0 && y >= QUOTE_LAYER_Y && y < QUOTE_LAYER_Y + QUOTE_LAYER_H
                              ? quoteLayer->getBuffer() + (y - QUOTE_LAYER_Y) * 40 : nullptr;
    for (int col = 0; col < width; ++col) {
      const int x = left + col;
      uint16_t &color = data[row*width+col];
      if (mask[maskY*40 + x/8] & (0x80 >> (x&7))) color = WHITE;
      else {
        uint16_t r = ((color >> 11) * keep) >> 8;
        uint16_t g = (((color >> 5) & 63) * keep) >> 8;
        uint16_t b = ((color & 31) * keep) >> 8;
        if (quoteRow && (quoteRow[x/8] & (0x80 >> (x&7)))) {  // quote text, blended by the fade
          r += ((31 - r) * quoteAlpha) >> 8; g += ((63 - g) * quoteAlpha) >> 8; b += ((31 - b) * quoteAlpha) >> 8;
        }
        color = (r << 11) | (g << 5) | b;
      }
    }
  }
}

static void pushStrip(int top, int rows) {
  screen.startWrite();
  screen.setAddrWindow(0, top, WIDTH, rows);
  // Native uint16_t RGB565 on ESP32: the display library swaps bytes on SPI.
  screen.writePixels(pixels, WIDTH * rows, true, false);
  screen.endWrite();
}

static bool renderMovie(uint32_t frame) {
  if (!movie || frame >= movie->frameCount) return false;
  forgetScreenContents();
  const AnimationFrame &image = movie->frames[frame];
  const JRESULT result = TJpgDec.drawJpg(0, 0, image.data, image.size);
  return result == JDR_OK || (result == JDR_INTR && (page != Page::Clock || pendingSwipe));
}

static bool jpegBlock(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t *data) {
  if (decodingToSmallFrame) {
    if (page != Page::Clock || x < 0 || y < 0 || x+width > SMALL_W || y+height > SMALL_H) return false;
    for (int row = 0; row < height; ++row)
      memcpy(smallFrame + (y+row)*SMALL_W + x, data + row*width, width * 2);
    serviceTouch(); // Nothing is being sent to the screen here, so a tap can redraw safely.
    return page == Page::Clock;
  }
  if (page != Page::Clock || x < 0 || y < 0 || x+width > WIDTH || y+height > HEIGHT) return false;
  compositeBlock(x, y, width, height, data);
  screen.startWrite();
  screen.setAddrWindow(x, y, width, height);
  screen.writePixels(data, width*height, true, false);
  screen.endWrite();
  serviceTouch(); // UI drawing only runs after ending the display transaction.
  yield();
  return page == Page::Clock && !pendingSwipe;
}

// RGB565 "spread" form: green moved to the high half with gaps between the
// colour fields, so one 32-bit multiply blends red, green and blue at once.
static inline uint32_t spread565(uint16_t c) { return (c | (uint32_t(c) << 16)) & 0x07E0F81Fu; }

// Scales the finished 240x180 frame by 4/3 (smooth, separable blend), adds the
// clock text and sends only the changed lines, in one screen transaction.
static uint32_t scaledRows[2][WIDTH];   // horizontally scaled source rows (x4)
static int scaledRowIndex[2] = {-1, -1};
static void scaleRow(int sy, uint32_t *dst) {
  const uint16_t *src = smallFrame + sy * SMALL_W;
  for (int x = 0; x < WIDTH; ++x) {
    const int sx = scaleX[x] & 0x3FFF, fx = scaleX[x] >> 14, sx1 = min(sx + 1, SMALL_W - 1);
    dst[x] = spread565(src[sx]) * (4 - fx) + spread565(src[sx1]) * fx;  // x4, fits in the gaps
  }
}
static const uint32_t *scaledRow(int sy) {
  for (int i = 0; i < 2; ++i) if (scaledRowIndex[i] == sy) return scaledRows[i];
  const int slot = (scaledRowIndex[0] < scaledRowIndex[1]) ? 0 : 1;  // replace the older row
  scaleRow(sy, scaledRows[slot]); scaledRowIndex[slot] = sy;
  return scaledRows[slot];
}
static void prepareScaling() {
  if (!scaleX[WIDTH-1]) {
    for (int x = 0; x < WIDTH; ++x) {
      const int q = x * 3 + 1;  // centre-aligned 3/4 step, in quarter pixels
      scaleX[x] = min(q >> 2, SMALL_W - 1) | ((q & 3) << 14);
    }
  }
  scaledRowIndex[0] = scaledRowIndex[1] = -1;
}
// One finished 320-pixel screen line (scaled, with the clock text); returns its checksum.
static uint32_t buildScreenLine(int y, uint16_t *line) {
  const int q = y * 3 + 1, sy = min(q >> 2, SMALL_H - 1), fy = q & 3;
  const uint32_t *r0 = scaledRow(sy);
  const uint32_t *r1 = scaledRow(min(sy + 1, SMALL_H - 1));
  const uint32_t w0 = 4 - fy, w1 = fy;
  for (int x = 0; x < WIDTH; ++x) {
    const uint32_t v = ((r0[x] * w0 + r1[x] * w1) >> 4) & 0x07E0F81Fu;  // x16 -> x1
    line[x] = uint16_t(v | (v >> 16));
  }
  if (y >= overlayTop) compositeBlock(0, y, WIDTH, 1, line);
  uint32_t hash = 2166136261u;
  for (int x = 0; x < WIDTH; x += 2) hash = (hash ^ (line[x] | (uint32_t(line[x+1]) << 16))) * 16777619u;
  return hash;
}
static void pushSmallFrame() {
  prepareScaling();
  uint16_t *line = pixels;  // reuse the strip buffer (at least one 320-pixel line)
  bool windowOpen = false;  // the display's write window continues from the last sent line
  screen.startWrite();
  for (int y = 0; y < HEIGHT; ++y) {
    const uint32_t hash = buildScreenLine(y, line);
    if (lineHashValid && hash == lineHash[y]) { windowOpen = false; continue; }
    lineHash[y] = hash;
    if (!windowOpen) { screen.setAddrWindow(0, y, WIDTH, HEIGHT - y); windowOpen = true; }
    screen.writePixels(line, WIDTH, true, false);
  }
  screen.endWrite();
  lineHashValid = true;
}

// Average time to show one GIF frame; printed to Serial and sent to the phone,
// which picks the GIF frame rate the clock can really keep up with.
static uint32_t gifTimingPrintedAt = 0;
static bool smallFrameBlock() { serviceTouch(); return page == Page::Clock && !pendingSwipe; }

// Start-up speed test: decode + scale a detailed test picture (not shown), then
// add the time the screen link needs for a full picture. The phone uses this to
// play each GIF at the highest frame rate this clock can keep up with.
static void runGifBenchmark() {
  if (!smallFrame) return;
  const uint32_t started = micros();  // flash constants are directly readable on the ESP32
  const int ok = CustomAnimation::decodeJpeg(GIF_BENCHMARK_JPEG, sizeof(GIF_BENCHMARK_JPEG), smallFrame, SMALL_W, SMALL_H, nullptr);
  prepareScaling();
  for (int y = 0; y < HEIGHT; ++y) buildScreenLine(y, pixels);
  const float computeMs = (micros() - started) / 1000.0f;
  const float screenMs = WIDTH * HEIGHT * 16.0f * 1000.0f / TFT_SPI_HZ * 1.08f;  // + command overhead
  const float readMs = 4.0f;  // reading a frame from storage
  smallFrameIndex = -1;
  if (ok != 1) { Serial.println("GIF speed test failed"); return; }
  gifBenchMs = computeMs + screenMs + readMs;
  Serial.printf("GIF speed test: %.0f ms per frame -> up to %.0f frames/s (decode+scale %.0f ms, screen %.0f ms)\n",
                gifBenchMs, 1000.0f / gifBenchMs, computeMs, screenMs);
}

// Plays GIF `id` from the library (and remembers it).
static bool pendingReveal = false;  // play the portal transition when this GIF first appears
static bool selectGif(uint32_t id) {
  if (!customAnimation.select(id)) return false;
  if (prefsReady) preferences.putUInt("gifId", id);
  setCustomEnabled(true);
  pendingReveal = true;
  return true;
}

// "Portal" transition for a newly chosen GIF: its first frame opens from the
// centre of the screen in a growing circle with a glowing edge (~0.8 s). Each
// step only sends the newly uncovered pixels, so it stays smooth.
static void revealSmallFrame() {
  prepareScaling();
  static int16_t shownFrom[HEIGHT], shownTo[HEIGHT];  // part of each line already showing the GIF
  for (int y = 0; y < HEIGHT; ++y) shownFrom[y] = shownTo[y] = WIDTH / 2;
  uint16_t *line = pixels;
  constexpr int STEPS = 24, STEP_MS = 32, EDGE_WHITE = 2, EDGE_GLOW = 5;
  constexpr float CX = WIDTH / 2.0f, CY = HEIGHT / 2.0f, MAX_R = 205.0f;
  auto chord = [&](float radius, float dy, int &from, int &to) {
    from = to = WIDTH / 2;
    if (fabsf(dy) >= radius) return;
    const float half = sqrtf(radius * radius - dy * dy);
    from = max(0, int(floorf(CX - half))); to = min(WIDTH, int(ceilf(CX + half)));
  };
  const uint32_t started = millis();
  for (int step = 1; step <= STEPS; ++step) {
    const float p = float(step) / STEPS, eased = 1 - (1 - p) * (1 - p) * (1 - p);  // fast, then gentle
    const float r = MAX_R * eased;
    screen.startWrite();
    for (int y = 0; y < HEIGHT; ++y) {
      const float dy = y + 0.5f - CY;
      int a, b, wa, wb, ga, gb;
      chord(r, dy, a, b);
      chord(r + EDGE_WHITE, dy, wa, wb);
      chord(r + EDGE_WHITE + EDGE_GLOW, dy, ga, gb);
      if (a < b && (a < shownFrom[y] || b > shownTo[y])) {
        buildScreenLine(y, line);
        if (shownFrom[y] >= shownTo[y]) {
          screen.setAddrWindow(a, y, b - a, 1); screen.writePixels(line + a, b - a, true, false);
        } else {
          if (a < shownFrom[y]) { screen.setAddrWindow(a, y, shownFrom[y] - a, 1); screen.writePixels(line + a, shownFrom[y] - a, true, false); }
          if (b > shownTo[y]) { screen.setAddrWindow(shownTo[y], y, b - shownTo[y], 1); screen.writePixels(line + shownTo[y], b - shownTo[y], true, false); }
        }
        shownFrom[y] = a; shownTo[y] = b;
      }
      // Glowing edge just outside the opening: a white rim, then the accent colour.
      if (a < b) {
        if (wa < a) screen.writeFillRect(wa, y, a - wa, 1, WHITE);
        if (wb > b) screen.writeFillRect(b, y, wb - b, 1, WHITE);
      } else if (wa < wb) screen.writeFillRect(wa, y, wb - wa, 1, WHITE);
      const int ia = wa < wb ? wa : WIDTH / 2, ib = wa < wb ? wb : WIDTH / 2;
      if (ga < ia) screen.writeFillRect(ga, y, ia - ga, 1, 0x5DDF);
      if (gb > ib) screen.writeFillRect(ib, y, gb - ib, 1, 0x5DDF);
    }
    screen.endWrite();
    serviceTouch();
    if (page != Page::Clock) return;
    while (millis() - started < uint32_t(step * STEP_MS)) delay(1);
  }
  lineHashValid = false;
  pushSmallFrame();  // exact final picture
}

static void renderFallback(int scene);
// Returns false only for a real failure (not when a tap interrupted drawing).
static bool renderCustomFrame(uint32_t frame) {
  const bool small = customAnimation.width() == SMALL_W;
  if (small && smallFrame) {
    const bool newFrame = smallFrameIndex != static_cast<int32_t>(frame);
    const uint32_t started = micros();
    if (newFrame) {  // the time changing alone needs no new decode
      smallFrameIndex = -1;
      const bool ok = customAnimation.decodeFrame(frame, smallFrame, smallFrameBlock);
      if (page != Page::Clock || pendingSwipe) return true;  // stopped early on purpose
      if (!ok) return false;
      smallFrameIndex = frame;
    }
    const bool reveal = pendingReveal && newFrame;
    if (reveal) { pendingReveal = false; revealSmallFrame(); }
    else pushSmallFrame();
    if (newFrame && !reveal) {  // the transition is not a normal frame; keep it out of the timing
      const float ms = (micros() - started) / 1000.0f;
      gifDrawMs = gifDrawMs ? gifDrawMs * 0.9f + ms * 0.1f : ms;
      if (millis() - gifTimingPrintedAt > 10000) {
        gifTimingPrintedAt = millis();
        Serial.printf("GIF: %.0f ms per frame (up to %.0f frames/s), GIF made for %u frames/s\n",
                      gifDrawMs, 1000.0f / gifDrawMs, customAnimation.fps());
      }
    }
    return true;
  }
  lineHashValid = false;  // drawn directly below, not through the line cache
  if (small) {  // No memory for smooth drawing: show it centred, unscaled.
    // Paint the black border once, never every second (that showed as a flash).
    if (!customBackdropDrawn) { renderFallback(CUSTOM_SCENE); customBackdropDrawn = page == Page::Clock; }
    if (page != Page::Clock) return true;
    return customAnimation.renderFrame(frame, (WIDTH - SMALL_W) / 2, (HEIGHT - SMALL_H) / 2) || page != Page::Clock;
  }
  return customAnimation.renderFrame(frame) || page != Page::Clock;  // 320x240 file from the first release
}

static void renderFallback(int scene) {
  lineHashValid = false;
  const uint16_t colors[] = {0x4C1C, 0xBACA, 0x10A8, 0x0000};
  for (int top=0; top<HEIGHT; top+=STRIP_ROWS) {
    for (size_t i=0; i<WIDTH*STRIP_ROWS; ++i) pixels[i] = colors[scene];
    compositeBlock(0, top, WIDTH, STRIP_ROWS, pixels);
    pushStrip(top, STRIP_ROWS);
    serviceTouch();
    if (page != Page::Clock) return;
    yield();
  }
}

static void updateClock() {
  const time_t utc = time(nullptr);
  tm local{};
  const bool valid = localClock(utc, local);
  playingCustom = clockFace == FACE_GIF && customAnimation.exists() && !uploadInProgress;
  const int scene = playingCustom ? CUSTOM_SCENE :
                    DEMO_BACKGROUNDS ? (uptimeMs()/10000)%3 : valid ? sceneFor(local) : 0;
  if (scene != currentScene) {
    currentScene = scene; overlayDirty = true; forgetScreenContents(); smallFrameIndex = -1;
    if (playingCustom) { movie = nullptr; movieStart = uptimeMs(); lastFrame = UINT32_MAX; }
    else openMovie(scene);
  }
  static uint32_t fadeFrameAt = 0;
  if (clockFace == FACE_QUOTE && quoteFading() && millis() - fadeFrameAt >= 40) { fadeFrameAt = millis(); overlayDirty = true; }
  buildClockMask(utc, valid, local, scene);
  if (playingCustom) {
    const uint32_t frames = customAnimation.frameCount();
    const uint32_t frame = ((uptimeMs()-movieStart)*customAnimation.fps()/1000) % frames;
    if (frame != lastFrame || overlayDirty) {
      if (!renderCustomFrame(frame)) {
        Serial.printf("Custom animation stopped: %s\n", customAnimation.lastError());
        clockFace = FACE_SCENES; customEnabled = false; currentScene = -1; overlayDirty = true;
        return;
      }
      if (page == Page::Clock) { lastFrame = frame; overlayDirty = false; }
    }
  } else if (movie) {
    const uint32_t frame = ((uptimeMs()-movieStart)*movieFps/1000) % movieFrames;
    if (frame != lastFrame || overlayDirty) {
      if (!renderMovie(frame)) {
        movie = nullptr;
        mediaStatus = "Animation decode failed";
        overlayDirty = true;
        return;
      }
      if (page == Page::Clock) { lastFrame = frame; overlayDirty = false; }
    }
  } else if (overlayDirty || uptimeMs() >= fallbackAt) {
    renderFallback(scene);
    if (page == Page::Clock) overlayDirty = false;
    fallbackAt = uptimeMs()+1000;
  }
}

// Start-up screen: the Korvix logo (16 grey levels) on black, faded in with the backlight.
static void drawBootLogo() {
  screen.fillScreen(0x0000);
  const int x0 = (WIDTH - BOOT_LOGO_W) / 2, y0 = (HEIGHT - BOOT_LOGO_H) / 2 - 8;
  uint16_t *line = pixels;
  screen.startWrite();
  screen.setAddrWindow(x0, y0, BOOT_LOGO_W, BOOT_LOGO_H);
  for (int y = 0; y < BOOT_LOGO_H; ++y) {
    const uint8_t *row = BOOT_LOGO + y * BOOT_LOGO_STRIDE;
    for (int x = 0; x < BOOT_LOGO_W; ++x) {
      const uint8_t level = x & 1 ? row[x >> 1] & 0x0F : row[x >> 1] >> 4;  // 0..15
      const uint16_t r = level * 31 / 15, g = level * 63 / 15;
      line[x] = (r << 11) | (g << 5) | r;
    }
    screen.writePixels(line, BOOT_LOGO_W, true, false);
  }
  screen.endWrite();
  // Small and dim, so it doesn't compete with the logo.
  centered(screen, "Booting...", 222, nullptr, 0x8410);
}
static void fadeInBacklight() {
  for (int level = 0; level <= 255; level += 5) { analogWrite(21, level * level / 255); delay(10); }  // eased
  analogWrite(21, 255);
}

static void clockSetup() {
  Serial.begin(115200);
  {
    const esp_reset_reason_t why = esp_reset_reason();
    Serial.printf("\nClock starting (%s)\n", why == ESP_RST_BROWNOUT ? "restarted by a power dip: add the capacitor, see BATTERY.md" :
                  why == ESP_RST_POWERON ? "power on" : why == ESP_RST_SW ? "software restart" :
                  why == ESP_RST_PANIC ? "after a crash" : "reset");
  }
  pinMode(21, OUTPUT); digitalWrite(21, LOW);
  for (int pin : {4,16,17,33,5}) { pinMode(pin, OUTPUT); digitalWrite(pin,HIGH); }
  displaySPI.begin(14, 12, 13, 15);
#if CLOCK_USE_ST7789
  screen.init(240, 320, SPI_MODE3);
  screen.setSPISpeed(TFT_SPI_HZ);
#else
  screen.begin(TFT_SPI_HZ);
#endif
  screen.setRotation(DISPLAY_ROTATION);
  // Apply the panel's RGB/BGR order after rotation, which rewrites MADCTL.
#if CLOCK_USE_ST7789
  uint8_t madctl = (DISPLAY_ROTATION == 1 ? 0xA0 : 0x60) | (DISPLAY_BGR ? 0x08 : 0);
#else
  uint8_t madctl = (DISPLAY_ROTATION == 1 ? 0x20 : 0xE0) | (DISPLAY_BGR ? 0x08 : 0);
#endif
  // MV swaps the display axes in landscape: MY controls screen-horizontal
  // direction, and MX controls screen-vertical direction. Keep RGB/BGR intact.
  if (DISPLAY_MIRROR_X) madctl ^= 0x80;
  if (DISPLAY_MIRROR_Y) madctl ^= 0x40;
  screen.sendCommand(0x36, &madctl, 1);
  screen.invertDisplay(DISPLAY_INVERT);
  drawBootLogo();      // drawn while the backlight is still off...
  fadeInBacklight();   // ...then faded in
  if (!clockMask.getBuffer()) {
    label("Not enough RAM for clock overlay", 10, 110);
    while (true) delay(1000);
  }
  clockHint = "<  TAP LEFT OR RIGHT FOR MORE  >"; clockHintUntil = 20000;  // first 20 s after start
  allocateSmallFrame(); // Before Wi-Fi starts, while one large free block is available.
  if (!customAnimation.reserveMemory()) Serial.println("GIF memory: could not reserve it at start-up");
  logMemory("GIF memory reserved");
  // Saving or checking a GIF needs a little memory; while that happens nothing is drawn from
  // the smooth-drawing buffer, so lend it (it comes back after the upload, see ClockWebServer.h).
  customAnimation.lowMemory = []() {
    // never while a screen is using it (GIF playing on the clock, Lock In drawing its padlock)
    if (!smallFrame || page == Page::Clock || page == Page::LockIn) return;
    releaseSmallFrame();
    logMemory("Memory short: lent the smooth-GIF buffer");
  };
  runGifBenchmark();
  touch.begin();
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(false);
  TJpgDec.setCallback(jpegBlock);
  prefsReady = preferences.begin("dayclock", false);
  if (BATTERY_INDICATOR) { Preferences cal_; if (cal_.begin("battery", true)) { const float c = cal_.getFloat("cal", 1.0f); cal_.end(); if (c >= 0.90f && c <= 1.10f) batteryUserCal = c; } }
  if (prefsReady) {
    String saved = preferences.getString("zone", "");
    for (size_t i=0; i<TIME_ZONE_COUNT; ++i) {
      if (saved == TIME_ZONES[i].name) { zoneIndex = i; break; }
    }
    if (preferences.getBytesLength("touch") == sizeof(calibration))
      preferences.getBytes("touch", &calibration, sizeof(calibration));
    savedSsid = preferences.getString("ssid", "");
    savedPassword = preferences.getString("pass", "");
    customEnabled = preferences.getBool("custom", false);
    clockFace = preferences.getUChar("face", customEnabled ? FACE_GIF : FACE_SCENES) % FACE_COUNT;
    lockHours = constrain(int(preferences.getUChar("lockH", 1)), 1, 24);
    if (preferences.getBytesLength("focus") == sizeof(focusLog)) preferences.getBytes("focus", &focusLog, sizeof(focusLog));
    const uint32_t lockSecs = preferences.getUInt("lockSecs", 0), lockStart = preferences.getUInt("lockStart", 0);
    if (lockSecs && lockStart) {  // a session that was running when the power went off
      lockActive = true; lockState = LockState::Running; lockTargetSecs = lockSecs;
      lockStartEpoch = lockStart; lockStartUptime = uptimeMs();
    }
  }
  if (!customAnimation.begin(prefsReady ? preferences.getUInt("gifId", 0) : 0))
    Serial.printf("GIF storage: %s\n", customAnimation.lastError());
  customEnabled = clockFace == FACE_GIF;  // the GIF face stays even with no GIF yet (it says how to add one)
  loadQuotes();  // your own quote list, if you made one on the phone
  int heldSamples = 0;
  const uint32_t start = millis();
  while (millis()-start < 1300) {
    uint16_t x,y;
    if (touch.readRaw(x,y)) ++heldSamples;
    delay(20);
  }
  wifiConfigured = ENABLE_WIFI_TIME_SYNC && !(prefsReady && preferences.getBool("offline", false));
  WiFi.persistent(false); // Credentials live in this sketch's own settings, not the Wi-Fi driver.
  if (wifiConfigured) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    legacySavedWiFi = !savedSsid.length() && savedWiFiInDriver();
    pendingWiFiSetup = !haveSavedWiFi();
    if (!pendingWiFiSetup) beginSavedWiFi();
    beginClockWeb();
  } else WiFi.mode(WIFI_OFF);
  // New boards ask for the time zone on the phone during Wi-Fi setup instead.
  requireZone = ASK_TIMEZONE_EVERY_BOOT || heldSamples >= 12 || (zoneIndex < 0 && !pendingWiFiSetup);
  const bool calibrated = calibration.magic == 0x43414C31 &&
    calibration.rotation == DISPLAY_ORIENTATION_KEY && isfinite(calibration.a) &&
    isfinite(calibration.b) && isfinite(calibration.c) && isfinite(calibration.d) &&
    isfinite(calibration.e) && isfinite(calibration.f);
  const Page destination = requireZone ? Page::Regions : Page::Clock;
  if (!calibrated) beginCalibration(destination);
  else if (destination == Page::Clock) goToClock();
  else setPage(destination);
}

// Quick "slide" between pages: a solid background-coloured panel sweeps across
// the screen in the direction you moved (fast, then easing out), then the new
// page is drawn. No edge lines: each step only paints the newly covered strip.
static void slideCurtain(bool towardsLeft) {
  constexpr int STEPS = 14;
  int covered = 0;
  for (int i = 1; i <= STEPS; ++i) {
    const float t = float(i) / STEPS, eased = 1 - (1 - t) * (1 - t);  // ease-out
    const int target = i == STEPS ? WIDTH : int(WIDTH * eased);
    if (target > covered) {
      const int x = towardsLeft ? WIDTH - target : covered;
      screen.fillRect(x, 0, target - covered, HEIGHT, BG);
      covered = target;
    }
    delay(6);
  }
}

// Up/down swipes: the GIF face steps through saved GIFs, the Quotes face through quotes.
static void cycleBackground(int direction) {
  static char hint[40];
  if (clockFace == FACE_QUOTE) {
    quoteOffset += direction; overlayDirty = true;
    return;
  }
  if (clockFace != FACE_GIF) return;
  static CustomAnimation::GifInfo items[CustomAnimation::MAX_GIFS];
  const int count = customAnimation.list(items, CustomAnimation::MAX_GIFS);
  if (count < 2) { clockHint = count ? "ONLY ONE GIF - ADD MORE IN SETTINGS" : "NO GIFS YET - SETTINGS > ADD A GIF"; clockHintUntil = millis() + 3000; overlayDirty = true; return; }
  int position = 0;  // 0..count-1, newest first
  for (int i = 0; i < count; ++i) if (items[i].id == customAnimation.activeId()) position = count - 1 - i;
  position = (position + direction + count) % count;
  selectGif(items[count - 1 - position].id);
  snprintf(hint, sizeof(hint), "GIF %d OF %d", position + 1, count);
  clockHint = hint; clockHintUntil = millis() + 2500; overlayDirty = true;
}
// Switch to another clock face (after the slide) and name it briefly.
static void showFace(int face) {
  static const char *const NAMES[FACE_COUNT] = {"DAY & NIGHT", "MY GIFS", "TAP THE QUOTE FOR A NEW ONE"};
  setClockFace(face);
  clockHint = NAMES[clockFace]; clockHintUntil = millis() + 2500;
  setPage(Page::Clock);
}

static void handleSwipe() {
  const int8_t swipe = pendingSwipe;
  if (!swipe) return;
  pendingSwipe = SWIPE_NONE;
  switch (page) {
    case Page::Clock:  // faces: Day/Night | GIF | Quotes, with Lock In on the left and Settings on the right
      if (swipe == SWIPE_LEFT) {
        slideCurtain(true);
        if (clockFace + 1 < FACE_COUNT) { showFace(clockFace + 1); } else setPage(Page::Settings);
      } else if (swipe == SWIPE_RIGHT) {
        slideCurtain(false);
        if (clockFace > 0) { showFace(clockFace - 1); } else setPage(Page::LockIn);
      }
      else if (swipe == SWIPE_UP) cycleBackground(+1);
      else if (swipe == SWIPE_DOWN) cycleBackground(-1);
      break;
    case Page::Settings:
    case Page::Phone:
      if (swipe == SWIPE_RIGHT) { slideCurtain(false); setPage(Page::Clock); }
      break;
    case Page::Regions:
      if (swipe == SWIPE_RIGHT && !requireZone) { slideCurtain(false); setPage(Page::Settings); }
      break;
    case Page::Zones:  // swipe through the list like pages of a book
      if (swipe == SWIPE_LEFT && (zonePage+1)*5 < regionCount()) { ++zonePage; slideCurtain(true); drawPage(); }
      else if (swipe == SWIPE_RIGHT) {
        slideCurtain(false);
        if (zonePage > 0) { --zonePage; drawPage(); } else setPage(Page::Regions);
      }
      break;
    case Page::LockIn:
      if (swipe == SWIPE_LEFT) { slideCurtain(true); setPage(Page::Clock); }
      else if (swipe == SWIPE_RIGHT) { slideCurtain(false); setPage(Page::Calendar); }
      break;
    case Page::Calendar:
      if (swipe == SWIPE_LEFT) { slideCurtain(true); setPage(Page::LockIn); }
      break;
    default: break;  // setup, time entry and confirmations keep their buttons
  }
}

static void clockLoop() {
  serviceWiFi();
  serviceWeb();
  serviceTouch();
  handleSwipe();
  serviceLockIn();
  serviceStreakFlame();
  serviceBattery();
  if (page == Page::Clock) updateClock();
  delay(1);
}
