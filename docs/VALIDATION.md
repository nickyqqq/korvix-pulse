# Validation — 8 October 2026, round 4 (slide fix)

- Fixed the page slide: each step drew a 1-px accent line at its leading edge that the next step never covered, leaving ~10 coloured (purple-looking) lines on screen until the new page finished drawing. The slide is now a solid background-coloured sweep with an ease-out (14 steps, ~85 ms) and no lines. Compiled cleanly: flash 3,063,275 bytes (97%).

# Validation — 8 October 2026, round 4 (slide fix, Reset)

- Fixed: the page slide left ~10 coloured vertical lines behind (each step drew its edge line inside the strip it had just filled, so the next strip never covered it). The edge is now a soft 3-px white glow drawn just ahead of the curtain and painted over by the next step; 20-px steps. Simulated with the real graphics library: 0 leftover pixels in both directions.
- Lock In: the Stop button is now called **Reset** (still asks "Sure?" first).
- Compiled cleanly: flash **3,063,275 bytes (97%)**.

# Validation — 8 October 2026, round 3 (Korvix boot logo)

- Start-up shows the Korvix logo (from the supplied brand image; cropped, 250 x 117 px, 16 grey levels so curves stay smooth, `BootLogo.h`, 14.6 KB) centred on black. It is drawn while the backlight is off, then the backlight fades in over ~0.5 s. A small grey "Booting..." line sits underneath (holding the screen during start-up still opens the time-zone picker; it just isn't announced). Rendered on a PC with the real graphics library (`previews/boot-screen.png`).
- Compiled cleanly: flash **3,063,259 bytes (97%, about 80 KB left)**, static RAM **90,116 bytes**.
- Not tested on hardware: the backlight fade uses `analogWrite` (PWM) on the backlight pin 21.

# Validation — 8 October 2026, round 2 (tap navigation + Lock In)

- **Tap to move between screens:** screens sit side by side, Lock In | Clock | Settings. On the clock, tap the left half = Lock In, right half = Settings, with a sliding curtain in that direction. Swipes still work (right = Lock In, left = Settings; up/down = backgrounds). Lock In: "Clock >" or swipe left goes back. Fixed: the Settings "Back to clock" button did nothing in the previous build (its tap was handled by a catch-all branch removed with the swipe change).
- **Lock In page:** tap the left side for -1 hour, the right side for +1 (1–24 h), START. Counts up from 0:00:00 inside a ring that fills clockwise from blue to green, with "of N hours" and the finish time. Stop needs two taps within 3 s ("Sure?"). At the target: green ring, "DONE!", and the board's green LED blinks for 6 s. A running session shows on the clock face ("LOCKED IN 1:23:45 / 5H"), and survives power loss once the clock knows the real time.
- The four Lock In states were drawn with the actual Adafruit GFX library compiled on a PC from the same drawing code (`previews/lock-in-screens.png`).
- Compiled cleanly: flash **3,039,480 bytes (96%, about 106 KB left)**, static RAM **89,904 bytes**.
- Not tested on hardware: tap zones, the LED (assumed green on GPIO16, active low, as on standard CYD boards).

# Validation — 8 October 2026 (swipe navigation)

- Clock: swipe left = Settings; swipe up/down = next/previous background (day & night scenes, then each saved GIF; a new GIF opens with the portal transition); tap = "SWIPE < FOR SETTINGS" hint (also shown for 20 s after start). Settings / Add-a-GIF page: swipe right = back to clock. Time-zone list: swipe left/right = next/previous page. Setup, time entry and confirmation pages keep buttons only. Buttons still work by tapping.
- Swipe = at least 60 px sideways (50 px up/down), mostly straight, under 1.2 s, measured in screen pixels through the touch calibration (so mirrored/rotated panels work). Diagonal or slow drags are ignored, never treated as taps. Gestures are handled from loop(), and drawing stops early when one is waiting, so pages change promptly.
- Page change shows a quick sliding curtain with an accent-coloured leading edge.
- Gesture classification checked with a simulated mirrored/rotated, noisy panel: left/right/up/down/tap correct; diagonal and slow drags ignored.
- Compiled cleanly: flash **3,025,680 bytes (96%)**, static RAM **89,824 bytes**. Not tested on the real touchscreen: thresholds may need tuning.

# Validation — 5 October 2026 (send transition)

- **Clock:** when a GIF is sent or picked in My GIFs, its first frame opens from the centre in a growing circle with a white + blue glowing rim (24 steps, ~0.8 s, ease-out), then plays normally. Each step sends only newly uncovered pixels; a full exact redraw follows. Simulated on a PC with the same geometry (`previews/clock-reveal.gif`). Excluded from frame timing.
- **Phone:** after a successful send the preview flies up and fades, an "On your clock!" tick pops, then the editor closes and the page scrolls to My GIFs. Respects the phone's reduce-motion setting.
- Compiled cleanly: flash **3,024,388 bytes (96%)**, static RAM **89,384 bytes**. All phone flows re-tested, no JS errors.

# Validation — 4 October 2026, round 4 (GIF library + automatic max frame rate)

- **GIF library:** every GIF is kept on the clock (`/gifs/gNNNNNNNN.clkj`, higher number = newer, up to 32 GIFs, up to 512 KB each in the ~880 KB storage). The playing one is remembered across restarts. Before saving a new GIF, the clock deletes the oldest GIFs until the new one fits. A GIF saved by the previous version is moved into the library automatically. Phone page: "My GIFs" grid (first-frame thumbnails served by the clock), tap to play, × to delete, storage meter.
- **Automatic max frame rate:** at start-up the clock decodes and scales a detailed 240x180 test picture (not shown) and adds the screen-link time for a full picture; result printed as `GIF speed test: NN ms per frame -> up to NN frames/s` and sent to the phone. The phone uses the GIF's own speed capped at that measured maximum (up to 30 frames/s). If a GIF doesn't fit, the phone lowers the frame rate (not below 10) before lowering quality, and only shortens it as a last resort.
- Phone tests (mock clock with the same eviction rules): 9 uploads; a 30 frames/s GIF was sent at 22 frames/s (mock speed test 45 ms); the second large GIF evicted the 4 oldest; thumbnails load; select and delete work; no JS errors. All uploads passed the firmware's checks with the real decoder. Speed-test picture decodes identically with both decoders.
- Compiled cleanly: flash **3,021,212 bytes (96%; about 120 KB left)**, static RAM **88,424 bytes**.
- Not tested on hardware: the library file operations on LittleFS (directory listing, rename, delete), and the speed-test accuracy.

# Validation — 4 October 2026, round 3 (faster GIF playback)

- **Faster decoding:** GIF frames use a private, renamed copy of TJpgDec R0.03 (`clk_tjpgd.*`) set to its fastest mode (table-driven Huffman decoding, table clipping, no scaling code), decoding straight into the frame buffer. Output checked **pixel-identical** to the library decoder on every frame of 4 test GIFs (141 frames); 12–28% faster on a PC. Found and fixed on the way: this decoder keeps a byte-swap flag from uninitialised memory, which could have shown GIFs in wrong colours; the decoder struct is now zeroed.
- **Faster 4:3 scaling:** separable blend with packed RGB565 arithmetic (one multiply handles R, G and B); each source row is scaled once instead of per screen line. Output **identical** to the previous version on a test frame, with about a quarter of the arithmetic.
- **Higher frame rate:** GIFs now play at their own speed, from 10 up to 20 frames/s. The clock measures its real drawing time per frame (Serial: `GIF: NN ms per frame`) and reports it to the phone, which never picks a rate faster than that. Up to 150 frames / 10 s. Time-only updates reuse the decoded frame.
- Frame read buffer reduced to 16 KB to offset the decoder's 11 KB workspace.
- Compiled cleanly: flash **2,999,440 bytes (95%)**; static RAM **83,984 bytes** (the frame buffer itself is allocated at start-up).
- Phone tests: all flows pass; a 70 ms/frame GIF was sent at 14 frames/s, 100 ms GIFs at 10.
- Not measured: actual frames per second on the board (Serial prints it).

# Validation — 4 October 2026, round 2 (flash fix + preview/reframe)

- Flash fixes: (1) bug: without enough memory, the GIF's black border was repainted over the whole screen every second; now painted once. (2) Only changed screen lines are sent (per-line checksum), and a time-only update reuses the decoded frame and rewrites just the digit rows. (3) `WiFi.setSleep(false)` once connected (steady radio current; power-save pulses can dim the backlight).
- Phone: **Preview** opens an animated preview with the clock's time drawn on top; drag/pinch/slider/mouse-wheel framing, **Fill screen** and **Whole GIF**. The chosen framing is applied to every frame sent.
- Tested in headless Chromium: GIPHY link → preview → drag + zoom 2x → send; **Whole GIF** → send (uploaded frame has black side bars, verified); long GIF; phone photo; all uploads passed the firmware's checks with the real decoder. Two-finger pinch was not tested (no touchscreen in the test browser); drag and slider were.
- Compiled cleanly: flash **2,991,896 bytes (95%)**, static RAM **69,760 bytes**.

# Validation — 4 October 2026 (smooth GIF playback)

Fix for GIFs looking "blinky": the clock used to paint each GIF frame on screen in 16x16 tiles while decoding (~80 ms of every 100 ms showed a half-old, half-new picture). The phone now sends 240 x 180 frames; the clock decodes each one fully into an 84 KB RAM buffer (allocated at start-up, before Wi-Fi), then scales it 4:3 with bilinear blending and writes all 320 x 240 pixels in one screen transaction. Frame cap lowered to 24 KB (frames are now ~2–10 KB). HTTPS backup downloads temporarily borrow that buffer if memory is short.

- Compiled cleanly: flash **2,986,096 bytes (94%)**, static RAM **68,792 bytes**.
- All test uploads re-validated with the firmware's own rules and the real TJpg_Decoder, including the partial bottom block row (180 is not a multiple of 16): 21, 12, 80, 8 and 1 frames. The noisy stress GIF now fits 80 frames instead of 40.
- A Python port of the scaling code, run on a real uploaded frame, matched Pillow's bilinear resize (mean difference 3.8/255, from 16-bit colour rounding).
- Not measured: free memory on the real board (Serial prints it) and the actual frame rate.

# Validation — 3 October 2026 (QR setup + GIPHY GIFs)

This build replaces the WiFiManager portal with the clock's own setup page, adds QR codes on the clock screen, and adds custom GIFs. It continues the work Codex started (custom-animation storage, web server skeleton, QR library); Codex stopped before the phone page and the wiring were written, so that folder did not compile at that point.

## ESP32 build

Compiled cleanly (`--clean`, no warnings from project code) with Arduino CLI 1.3.1, ESP32 core 3.3.11, board options `esp32:esp32:esp32:PartitionScheme=huge_app,PSRAM=disabled,FlashSize=4M,CPUFreq=240`, and Adafruit GFX, Adafruit ST7735/ST7789, Adafruit BusIO, TJpg_Decoder (latest from GitHub).

- Flash: **2,984,504 / 3,145,728 bytes (94%)**. About 160 KB is left for future changes.
- Static RAM: **68,128 / 327,680 bytes (20%)**.
- Loop task stack raised to 16 KB for the HTTPS backup download.
- The ILI9341 display path was not recompiled in this round; that code is unchanged.

## Clock screens and QR codes

The Wi-Fi setup, connecting, failed, connected, add-a-GIF and settings screens were rendered pixel-for-pixel with the display's built-in font (`previews/screen-*.png`) to check that text fits and nothing overlaps.

The QR library is compiled from the project's own `qrcode.c`. That library does not check whether text fits a QR size, so the firmware now picks the size itself from the capacity table. The rendered Wi-Fi code (`WIFI:T:WPA;S:Clock-Setup-…;P:…;;`) and address code (`http://192.168.100.123/`) were both decoded successfully by OpenCV's QR reader.

## Phone page

Tested in headless Chromium at phone size (390 x 844, touch) against a mock of the clock's API:

- **GIF decoder** compared with Chrome's built-in decoder (WebCodecs ImageDecoder) on an animated interlaced GIF, a transparent "sticker" GIF with background disposal, and a GIF using "restore previous" disposal with zero delays: **identical pixels on every frame**. A single-frame GIF and a 22 MB / 140-frame noise GIF (12-bit LZW codes, clear codes) also decoded.
- **Uploads:** every file the page produced was checked by a host build of the firmware's own rules: container header, frame count, size limits, baseline-JPEG header parser copied from `CustomAnimation.h`, and a full decode with the real TJpg_Decoder C code. All passed (21, 12, 40, 8 and 1 frames; largest 608 KB; largest frame 16.8 KB).
- **Links:** 9 GIPHY link styles parsed to the right id (including share text around the link, `v1.` media URLs, `.webp`, embed, stickers, `gph.is`). Non-GIPHY text gives a plain-language error.
- **Flows:** GIPHY link → clock, phone photo → clock, background switch, bad link, Wi-Fi pick → wrong password → retry → success. No JavaScript errors. Network names containing HTML are shown as plain text.

## Not tested here (needs the real board and phones)

The sandbox has no ESP32 and cannot reach GIPHY, so these need a check after upload:

1. Scanning the Wi-Fi QR on an **iPhone** and an **Android** phone, the setup pop-up opening, and joining a real 2.4 GHz router (the phone may drop off the hotspot for a moment while the clock changes channel; the page handles this).
2. A real GIPHY link end-to-end. The phone downloads from `media.giphy.com` directly; if GIPHY blocks that, the clock downloads it over HTTPS instead (also untested on hardware).
3. **Safari's JPEG output** (only Chrome's was tested). The clock rejects anything it can't play with a clear message, so the risk is an error message, not a crash.
4. GIF playback speed on the board (frames are read from flash storage; the built-in scenes play from program memory).
5. LittleFS formatting on first start of a board that previously ran other firmware.


## Lock In and Settings redesign

- Only `ClockApp.h` changed. Compiles: 3,069,259 bytes (97 %), 90,116 bytes RAM.
- Every screen state (Lock In setup, running, Reset armed, done; Settings online/offline, long time-zone name) was drawn with the real Adafruit GFX library on the PC and checked: nothing overlaps or runs past its card (`previews/ui-redesign.png`).
- Tap areas match the drawn buttons and tiles.
- Not yet checked on a real board: how the colours look on the ST7789 panel and how easy the tiles are to hit with the resistive touch.


## Focus calendar

- Compiles: 3,072,727 bytes (97 %), 90,716 bytes RAM (+600 bytes for the 140-day history).
- The calendar page and the new Lock In button were drawn with the real Adafruit GFX library on the PC, with made-up history and with an empty history plus a running session that crosses midnight (split correctly into two days): `previews/focus-calendar.png`.
- Not yet checked on a real board: saving the history to flash and reading it back after a restart.


## Clock faces and quotes

- Compiles: 3,078,367 bytes (97 %), 90,796 bytes RAM (+ a 9.6 KB full-screen text mask).
- All 64 quotes were drawn with the real Adafruit GFX fonts on the PC over the day, sunset and night footage: each fits in at most 3 big lines (or 4 smaller), with even line lengths (`previews/quotes.png`).
- Not yet checked on a real board: the frame rate of the dimmed full-screen sky on the Quotes face.


## Editable quotes

- Firmware compiles (98 % of the app space).
- Phone page tested in Chromium against a stand-in clock: the Quotes list opens from the QR link, adding (with curly quotes/apostrophes from a phone keyboard, cleaned to plain text), editing, deleting, show-now and restoring the original list all work, with no script errors (`previews/phone-quotes.png`).
- New Settings layout and quotes up to 90 characters drawn with the real fonts (`previews/settings-quotes.png`).
- Not yet checked on a real board: saving the list to the clock's storage and reading it back after a restart.
