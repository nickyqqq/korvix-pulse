#pragma once
// Included by ClockApp.h after the touch handlers. All routes run on loop().
#include "ClockWebPage.h"

// Default root-certificate bundle that ships inside the ESP32 core (used to
// verify GIPHY's HTTPS certificate when the clock downloads a GIF itself).
extern const uint8_t clock_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t clock_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

static String jsonString(const String &value) {
  String out = "\"";
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t c = value[i];
    if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
    else if (c < 32) { char escaped[7]; snprintf(escaped, sizeof(escaped), "\\u%04x", c); out += escaped; }
    else out += char(c);
  }
  return out + '"';
}

// Rejects requests whose Host header is not this clock (DNS-rebinding guard).
static bool localHostRequest() {
  String host = clockServer.hostHeader();
  host.toLowerCase();
  if (host.endsWith(":80")) host.remove(host.length()-3);
  return (WiFi.status() == WL_CONNECTED && host == WiFi.localIP().toString()) ||
         (apActive && host == "192.168.4.1");
}

static void webReply(int code, const String &message) {
  clockServer.sendHeader("Cache-Control", "no-store");
  clockServer.send(code, "application/json", "{\"message\":" + jsonString(message) + "}");
}

static String suppliedToken() {
  return clockServer.hasHeader("X-Clock-Token") ? clockServer.header("X-Clock-Token") : clockServer.arg("token");
}

// The token is only readable by pages served from this clock, so other web
// sites open on the phone cannot send commands to it.
static bool authorizedWebRequest() {
  if (!localHostRequest() || suppliedToken() != webToken) {
    webReply(403, "Please scan the code on the clock again.");
    return false;
  }
  return true;
}

static bool validGiphyId(const String &id) {
  if (id.length() < 5 || id.length() > 40) return false;
  for (size_t i = 0; i < id.length(); ++i) if (!isalnum(static_cast<unsigned char>(id[i]))) return false;
  return true;
}

// Finds the GIF id in a giphy.com/gifs/name-ID or media.giphy.com/media/ID/... address.
static String giphyIdFromUrl(String url) {
  const int query = url.indexOf('?');
  if (query >= 0) url.remove(query);
  int at = url.indexOf("/media/");
  if (at >= 0) {
    String rest = url.substring(at + 7);
    if (rest.startsWith("v1.")) rest = rest.substring(rest.indexOf('/') + 1);
    const int slash = rest.indexOf('/');
    return slash >= 0 ? rest.substring(0, slash) : rest;
  }
  for (const char *kind : {"/gifs/", "/stickers/", "/clips/", "/embed/"}) {
    at = url.indexOf(kind);
    if (at < 0) continue;
    String rest = url.substring(at + strlen(kind));
    const int slash = rest.indexOf('/');
    if (slash >= 0) rest.remove(slash);
    const int dash = rest.lastIndexOf('-');
    return dash >= 0 ? rest.substring(dash + 1) : rest;
  }
  return "";
}

static void secureClient(NetworkClientSecure &client) {
  client.setCACertBundle(clock_crt_bundle_start, clock_crt_bundle_end - clock_crt_bundle_start);
  client.setTimeout(12);
}

// Resolves a gph.is short link to a GIF id (reads only the redirect target).
static String resolveShortLink(const String &code) {
  NetworkClientSecure client; secureClient(client);
  HTTPClient http;
  const char *headers[] = {"Location"};
  if (!http.begin(client, "https://gph.is/" + code)) return "";
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.setTimeout(12000);
  http.collectHeaders(headers, 1);
  const int code_ = http.GET();
  String location = (code_ >= 300 && code_ < 400) ? http.header("Location") : "";
  http.end();
  return giphyIdFromUrl(location);
}

// Backup path: the clock downloads the GIF from GIPHY and passes it to the phone.
// HTTPS needs ~45 KB of RAM; lend it the smooth-drawing buffer if memory is short.
struct LendFrameMemory {
  bool lent = false;
  LendFrameMemory() { if (smallFrame && ESP.getFreeHeap() < 70000) { releaseSmallFrame(); lent = true; } }
  ~LendFrameMemory() { if (lent) allocateSmallFrame(); }
};

static void proxyGiphy() {
  if (!authorizedWebRequest()) return;
  LendFrameMemory lend;
  if (WiFi.status() != WL_CONNECTED) { webReply(503, "The clock is not on the internet yet."); return; }
  String id = clockServer.arg("id");
  const String shortCode = clockServer.arg("short");
  if (shortCode.length()) {
    bool ok = shortCode.length() >= 3 && shortCode.length() <= 40;
    for (size_t i = 0; ok && i < shortCode.length(); ++i)
      ok = isalnum(static_cast<unsigned char>(shortCode[i])) || shortCode[i] == '/';
    if (!ok) { webReply(400, "That doesn't look like a GIPHY link."); return; }
    id = resolveShortLink(shortCode);
  }
  const String rendition = clockServer.arg("r") == "giphy" ? "giphy" : "200";
  if (!validGiphyId(id)) { webReply(404, "Couldn't find that GIF. Check the link."); return; }
  NetworkClientSecure client; secureClient(client);
  HTTPClient http;
  if (!http.begin(client, "https://media.giphy.com/media/" + id + "/" + rendition + ".gif")) {
    webReply(502, "Couldn't reach GIPHY."); return;
  }
  http.setTimeout(12000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  const int status = http.GET();
  const int size = http.getSize();
  static constexpr int MAX_PROXY_BYTES = 8 * 1024 * 1024;
  if (status != HTTP_CODE_OK || size > MAX_PROXY_BYTES) {
    http.end();
    webReply(status == HTTP_CODE_OK ? 413 : 502, status == HTTP_CODE_OK ? "That GIF is too big." : "Couldn't get that GIF from GIPHY.");
    return;
  }
  clockServer.sendHeader("Cache-Control", "no-store");
  clockServer.setContentLength(size > 0 ? size : CONTENT_LENGTH_UNKNOWN);
  clockServer.send(200, "image/gif", "");
  NetworkClient *stream = http.getStreamPtr();
  static uint8_t buffer[1436];
  int remaining = size, total = 0;
  uint32_t lastData = millis();
  while (http.connected() && (size <= 0 || remaining > 0) && total < MAX_PROXY_BYTES) {
    const size_t available = stream->available();
    if (!available) {
      if (millis() - lastData > 12000) break;
      delay(2); continue;
    }
    const int count = stream->readBytes(buffer, min(available, sizeof(buffer)));
    if (count <= 0) break;
    clockServer.sendContent(reinterpret_cast<const char *>(buffer), count);
    if (size > 0) remaining -= count;
    total += count; lastData = millis();
  }
  if (size <= 0) clockServer.sendContent("");
  http.end();
}

static void beginClockWeb() {
  if (webReady) return;
  char token[33];
  snprintf(token, sizeof(token), "%08lx%08lx%08lx%08lx", (unsigned long)esp_random(),
    (unsigned long)esp_random(), (unsigned long)esp_random(), (unsigned long)esp_random());
  webToken = token;
  const char *headers[] = {"X-Clock-Token"};
  clockServer.collectHeaders(headers, 1);
  clockServer.on("/", HTTP_GET, []() {
    if (!localHostRequest()) { clockServer.sendHeader("Location", "http://192.168.4.1/"); clockServer.send(302); return; }
    clockServer.sendHeader("Cache-Control", "no-store");
    clockServer.sendHeader("X-Content-Type-Options", "nosniff");
    clockServer.sendHeader("X-Frame-Options", "DENY");
    clockServer.send_P(200, "text/html; charset=utf-8", CLOCK_WEB_PAGE);
  });
  clockServer.on("/api/status", HTTP_GET, []() {
    if (!localHostRequest()) { webReply(403, "Please scan the code on the clock again."); return; }
    const char *job = wifiJob == WifiJob::Connecting || connectPending ? "connecting" :
                      wifiJob == WifiJob::Ok ? "ok" : wifiJob == WifiJob::Fail ? "fail" : "idle";
    String body = "{\"connected\":" + String(WiFi.status()==WL_CONNECTED ? "true" : "false");
    body += ",\"ssid\":" + jsonString(WiFi.status()==WL_CONNECTED ? WiFi.SSID() : String(""));
    body += ",\"ip\":" + jsonString(WiFi.localIP().toString());
    body += ",\"zone\":" + jsonString(zoneIndex >= 0 ? TIME_ZONES[zoneIndex].name : "");
    body += ",\"hasCustom\":" + String(customAnimation.exists() ? "true" : "false");
    body += ",\"customEnabled\":" + String(customEnabled ? "true" : "false");
    body += ",\"ap\":" + String(apActive ? "true" : "false");
    body += ",\"wifi\":\"" + String(job) + "\"";
    body += ",\"maxBytes\":" + String(CustomAnimation::MAX_FILE_BYTES);
    body += ",\"maxFrames\":" + String(CustomAnimation::MAX_FRAMES);
    body += ",\"maxFrame\":" + String(CustomAnimation::MAX_JPEG_BYTES);
    body += ",\"drawMs\":" + String(static_cast<int>((gifBenchMs ? gifBenchMs : gifDrawMs) + 0.5f));
    body += ",\"activeGif\":" + String(customAnimation.activeId());
    body += ",\"maxFps\":" + String(CustomAnimation::MAX_FPS);
    if (BATTERY_INDICATOR && batteryPercent >= 0)
      body += ",\"battery\":" + String(batteryPercent) + ",\"charging\":" + String(batteryCharging ? "true" : "false") +
              (BATTERY_SOURCE == 0 ? ",\"volts\":" + String(batteryVolts, 2) : String(""));
    body += ",\"frameW\":" + String(CustomAnimation::SMALL_WIDTH) + ",\"frameH\":" + String(CustomAnimation::SMALL_HEIGHT);
    body += ",\"token\":" + jsonString(webToken) + ",\"version\":\"3.0\"}";
    clockServer.sendHeader("Cache-Control", "no-store");
    clockServer.send(200, "application/json", body);
  });
  clockServer.on("/api/networks", HTTP_GET, []() {
    if (!localHostRequest()) { webReply(403, "Please scan the code on the clock again."); return; }
    int count = WiFi.scanComplete();
    const bool rescan = clockServer.hasArg("rescan") && count != WIFI_SCAN_RUNNING;
    if (rescan || count == WIFI_SCAN_FAILED || (count >= 0 && millis()-lastScanAt > 30000)) {
      WiFi.scanDelete(); WiFi.scanNetworks(true); lastScanAt = millis(); count = WIFI_SCAN_RUNNING;
    }
    String body = "{\"scanning\":" + String(count == WIFI_SCAN_RUNNING ? "true" : "false") + ",\"networks\":[";
    for (int i=0; i<count && i<30; ++i) {
      if (i) body += ',';
      body += "{\"ssid\":" + jsonString(WiFi.SSID(i)) + ",\"rssi\":" + String(WiFi.RSSI(i));
      body += ",\"open\":" + String(WiFi.encryptionType(i)==WIFI_AUTH_OPEN ? "true" : "false") + "}";
    }
    clockServer.sendHeader("Cache-Control", "no-store");
    clockServer.send(200, "application/json", body+"]}");
  });
  clockServer.on("/api/zones", HTTP_GET, []() {
    if (!localHostRequest()) { webReply(403, "Please scan the code on the clock again."); return; }
    clockServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
    clockServer.send(200, "application/json", "");
    clockServer.sendContent("[");
    String chunk;
    for (size_t i=0; i<TIME_ZONE_COUNT; ++i) {
      if (i) chunk += ',';
      chunk += jsonString(TIME_ZONES[i].name);
      if (chunk.length()>1500) { clockServer.sendContent(chunk); chunk = ""; }
    }
    clockServer.sendContent(chunk+"]"); clockServer.sendContent("");
  });
  clockServer.on("/api/wifi", HTTP_POST, []() {
    if (!authorizedWebRequest()) return;
    if (!apActive) { webReply(409, "Open Wi-Fi setup on the clock first."); return; }
    String ssid=clockServer.arg("ssid"), password=clockServer.arg("password");
    if (!ssid.length() || ssid.length()>32 || password.length()>63 || (password.length() && password.length()<8)) {
      webReply(400, "Check the Wi-Fi name and password (8 to 63 letters)."); return;
    }
    pendingSsid=ssid; pendingPassword=password; connectAt=millis()+400; connectPending=true;
    wifiJob = WifiJob::Idle;
    if (page == Page::WiFiSetup) drawPage();
    webReply(200, "Connecting. Keep this page open for a moment.");
  });
  // Battery level calibration (resistor version): "full" right after a full charge, or a multimeter reading.
  clockServer.on("/api/battery", HTTP_POST, []() {
    if (!authorizedWebRequest()) return;
    if (!BATTERY_INDICATOR || BATTERY_SOURCE != 0 || batteryVolts <= 0) { webReply(400, "No battery reading to adjust."); return; }
    if (clockServer.hasArg("full") && !batteryCharging) { webReply(400, "Plug in the charger and wait until it shows full, then try again."); return; }
    const float actual = clockServer.hasArg("full") ? 4.18f : clockServer.arg("volts").toFloat();
    if (!calibrateBattery(actual))
      webReply(400, clockServer.hasArg("full") ? "That's too far from a full battery. Charge until the charger says full, then try again."
                                               : "Type the battery voltage from your multimeter, e.g. 3.85.");
    else webReply(200, "Done. Battery now reads " + String(batteryPercent) + "%.");
  });
  clockServer.on("/api/time", HTTP_POST, []() {
    if (!authorizedWebRequest()) return;
    const String zone=clockServer.arg("zone"), epoch=clockServer.arg("epoch");
    int index=-1;
    for (size_t i=0; i<TIME_ZONE_COUNT; ++i) if (zone == TIME_ZONES[i].name) { index=i; break; }
    char *end=nullptr;
    const int64_t utc=strtoll(epoch.c_str(), &end, 10);
    if (index<0 || epoch.length()<10 || !end || *end || utc<1577836800LL || utc>=TIME_ZONE_VALID_UNTIL) {
      webReply(400, "Choose your time zone and check your phone's date and time."); return;
    }
    if (!prefsReady || !preferences.putString("zone", zone)) { webReply(500, "Could not save. Please try again."); return; }
    // Internet time (NTP) wins once online; the phone's clock is only a quick start.
    if (WiFi.status() != WL_CONNECTED) {
      timeval tv = {static_cast<time_t>(utc), 0};
      settimeofday(&tv, nullptr);
    }
    zoneIndex=index; overlayDirty=true;
    if (requireZone && (page == Page::Regions || page == Page::Zones || page == Page::ConfirmZone)) {
      requireZone = false; goToClock();
    } else if (page == Page::ManualTime || page == Page::RepeatedTime) setPage(Page::Clock);
    else if (page == Page::Settings) drawPage();
    requireZone = false;
    webReply(200, "Time zone saved.");
  });
  clockServer.on("/api/background", HTTP_POST, []() {
    if (!authorizedWebRequest()) return;
    const bool enabled=clockServer.arg("enabled")=="1";
    if (enabled && !customAnimation.exists()) { webReply(400, "Send a GIF first."); return; }
    setCustomEnabled(enabled);
    if (page == Page::Settings) drawPage();
    webReply(200, enabled ? "Your GIF is on." : "Day, sunset and night scenes are on.");
  });
  // Quotes: read the list, save an edited list (one per line; empty = built-in list), show one now.
  clockServer.on("/api/quotes", HTTP_GET, []() {
    if (!authorizedWebRequest()) return;
    String body; body.reserve(8192);
    body = "{\"custom\":" + String(quotesAreCustom() ? "true" : "false") + ",\"max\":" + String(MAX_QUOTES) +
           ",\"maxLen\":" + String(MAX_QUOTE_LEN) + ",\"quotes\":[";
    for (int i = 0, n = quoteCount(); i < n; ++i) { if (i) body += ','; body += jsonString(quoteAt(i)); }
    body += "]}";
    clockServer.sendHeader("Cache-Control", "no-store");
    clockServer.send(200, "application/json", body);
  });
  clockServer.on("/api/quotes", HTTP_POST, []() {
    if (!authorizedWebRequest()) return;
    String error;
    const bool reset = clockServer.arg("reset") == "1";
    if (!saveQuotes(reset ? "" : clockServer.arg("text").c_str(), error)) { webReply(500, error); return; }
    quoteOffset = 0; overlayDirty = true;
    if (page == Page::Settings) drawPage();
    webReply(200, reset ? "Back to the original quotes." : "Saved. " + String(quoteCount()) + " quotes on your clock.");
  });
  clockServer.on("/api/quote/show", HTTP_POST, []() {
    if (!authorizedWebRequest()) return;
    const int index = clockServer.arg("i").toInt();
    if (index < 0 || index >= quoteCount()) { webReply(400, "That quote doesn't exist."); return; }
    showQuoteNow(index);
    webReply(200, "On your clock now.");
  });
  clockServer.on("/api/giphy", HTTP_GET, proxyGiphy);
  clockServer.on("/api/animation", HTTP_POST, []() {
    if (!authorizedWebRequest()) { customAnimation.abortUpload(); uploadInProgress=false; return; }
    if (uploadSucceeded) {
      selectGif(customAnimation.activeId());
      if (page == Page::Settings) drawPage();
      const int removed = customAnimation.evictedOnLastUpload();
      webReply(200, removed == 0 ? String("Done! Look at your clock.") :
               "Done! Look at your clock. (Storage was full, so the oldest " +
               (removed == 1 ? String("GIF was") : String(removed) + " GIFs were") + " removed.)");
    } else webReply(400, uploadMessage.length() ? uploadMessage : "Nothing was saved. Please try again.");
    uploadInProgress=false; uploadSucceeded=false;
  }, []() {
    HTTPUpload &upload=clockServer.upload();
    if (upload.status == UPLOAD_FILE_START) {
      uploadSucceeded=false; uploadMessage="";
      // Upload token must be a header, never a query string or multipart field.
      if (!localHostRequest() || clockServer.header("X-Clock-Token") != webToken) {
        uploadMessage="Please scan the code on the clock again."; return;
      }
      if (uploadInProgress) { customAnimation.abortUpload(); uploadInProgress=false; uploadMessage="Send one GIF at a time."; return; }
      logMemory("GIF upload starting");
      uploadInProgress=customAnimation.beginUpload(strtoul(clockServer.arg("size").c_str(), nullptr, 10));
      currentScene=-1; overlayDirty=true;
      if (!uploadInProgress) uploadMessage=customAnimation.lastError();
    } else if (upload.status == UPLOAD_FILE_WRITE && uploadInProgress) {
      if (!customAnimation.writeUpload(upload.buf, upload.currentSize)) {
        uploadMessage=customAnimation.lastError(); customAnimation.abortUpload(); uploadInProgress=false;
      }
    } else if (upload.status == UPLOAD_FILE_END && uploadInProgress) {
      uploadSucceeded=customAnimation.finishUpload();
      if (!uploadSucceeded) uploadMessage=customAnimation.lastError();
      uploadInProgress=false;
      allocateSmallFrame();   // take the smooth-drawing buffer back if it was lent (no-op otherwise)
      logMemory(uploadSucceeded ? "GIF saved" : "GIF not saved");
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
      customAnimation.abortUpload(); uploadInProgress=false; uploadSucceeded=false;
      uploadMessage="The upload stopped. Please try again.";
      allocateSmallFrame();
    }
  });
  clockServer.on("/api/gifs", HTTP_GET, []() {
    if (!localHostRequest()) { webReply(403, "Please scan the code on the clock again."); return; }
    static CustomAnimation::GifInfo items[CustomAnimation::MAX_GIFS];
    const int count = customAnimation.list(items, CustomAnimation::MAX_GIFS);
    String body = "{\"active\":" + String(customAnimation.activeId()) +
                  ",\"playing\":" + String(customEnabled && customAnimation.exists() ? "true" : "false") +
                  ",\"free\":" + String(customAnimation.freeBytes()) +
                  ",\"total\":" + String(customAnimation.totalBytes()) + ",\"items\":[";
    for (int i = count - 1; i >= 0; --i) {  // newest first
      if (i != count - 1) body += ',';
      body += "{\"id\":" + String(items[i].id) + ",\"bytes\":" + String(items[i].bytes) +
              ",\"frames\":" + String(items[i].frames) + ",\"fps\":" + String(items[i].fps) + "}";
    }
    clockServer.sendHeader("Cache-Control", "no-store");
    clockServer.send(200, "application/json", body + "]}");
  });
  clockServer.on("/api/gif/thumb", HTTP_GET, []() {
    if (!localHostRequest()) { clockServer.send(403); return; }
    uint8_t *jpeg = nullptr; size_t length = 0;
    if (uploadInProgress || !customAnimation.readThumbnail(strtoul(clockServer.arg("id").c_str(), nullptr, 10), jpeg, length)) {
      clockServer.send(404); return;
    }
    clockServer.sendHeader("Cache-Control", "max-age=86400");  // a saved GIF never changes
    clockServer.setContentLength(length);
    clockServer.send(200, "image/jpeg", "");
    clockServer.sendContent(reinterpret_cast<const char *>(jpeg), length);
  });
  clockServer.on("/api/gif/select", HTTP_POST, []() {
    if (!authorizedWebRequest()) return;
    if (!selectGif(strtoul(clockServer.arg("id").c_str(), nullptr, 10))) { webReply(404, customAnimation.lastError()); return; }
    if (page == Page::Settings) drawPage();
    webReply(200, "Playing it now.");
  });
  clockServer.on("/api/gif/delete", HTTP_POST, []() {
    if (!authorizedWebRequest()) return;
    const uint32_t id = strtoul(clockServer.arg("id").c_str(), nullptr, 10);
    const bool wasActive = id == customAnimation.activeId();
    if (!customAnimation.removeGif(id)) { webReply(500, customAnimation.lastError()); return; }
    if (wasActive) {  // play the newest remaining GIF, or the day/night scenes
      static CustomAnimation::GifInfo items[CustomAnimation::MAX_GIFS];
      const int count = customAnimation.list(items, CustomAnimation::MAX_GIFS);
      if (!count || !selectGif(items[count - 1].id)) setCustomEnabled(false);
    }
    if (page == Page::Settings) drawPage();
    webReply(200, "Deleted.");
  });
  clockServer.onNotFound([]() {
    // Phones check a web address after joining Wi-Fi; sending them here opens the setup page.
    if (apActive && clockServer.method()==HTTP_GET) {
      clockServer.sendHeader("Location", "http://192.168.4.1/"); clockServer.send(302, "text/plain", "Open clock setup");
    } else webReply(404, "Page not found.");
  });
  clockServer.begin(); webReady=true;
}
