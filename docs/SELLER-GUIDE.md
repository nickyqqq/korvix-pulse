# Seller guide: building and shipping a Pulse

For the battery version (battery level in the corner), set `BATTERY_INDICATOR = true` in `Config.h`; wiring in [BATTERY.md](BATTERY.md).

For the ESP32-2432S028 ("Cheap Yellow Display") with Micro-USB + USB-C, its built-in 320 x 240 screen and resistive touchscreen. The default screen driver is ST7789.

What's new in this version:

- **Setup by QR code.** The clock shows a QR code. The customer scans it with the phone camera, taps **Join**, and a setup page opens by itself. They tap their Wi-Fi, type the password, tap **Connect**. That's all. No app, no typing a hotspot password, no "192.168.4.1" unless the pop-up doesn't appear.
- **Time zone from the phone.** The setup page sends the phone's time zone and time to the clock, so new customers never see the time-zone picker.
- **Your own GIF.** Tap the clock → **Add a GIF** → scan the code → paste a GIPHY link → **Preview**. Drag to move it, pinch or use the slider to zoom (**Fill screen** / **Whole GIF** buttons reset it), then **Send to my clock**. The preview shows the time on top exactly where the clock draws it. A GIF or photo from the phone works too. Switch between the GIF and the day/sunset/night scenes on the phone page or in Settings.

## Upload (for you, the seller)

1. Open `DigitalClock/DigitalClock.ino` in Arduino IDE. Keep all its files together, including the large `EmbeddedAnimations.cpp`.
2. Boards Manager: **esp32 by Espressif Systems**, version **3.3.11**.
3. Select **ESP32 Dev Module**, **Flash Size: 4MB**, **CPU: 240MHz**, **PSRAM: Disabled**, **Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS)**. The GIF is stored in that 1MB "SPIFFS" area (formatted automatically on first start; no filesystem upload needed).
4. Libraries: **Adafruit GFX Library**, **Adafruit ST7735 and ST7789 Library** (with Adafruit BusIO), **TJpg_Decoder by Bodmer**. **WiFiManager is no longer used** and can stay installed or be removed.
5. Select the board's port and upload (115200 upload speed if faster speeds fail).
6. First start: tap the three calibration crosses and the centre cross. Then the **Scan with your phone** screen appears.

**Before shipping a clock:** calibrate it, then tap **Later** on the Wi-Fi screen and unplug. The customer's first start goes straight to the QR screen (calibration is remembered). If you set it up on your own Wi-Fi to test, use **Settings → Factory reset** before shipping. That clears Wi-Fi, time zone, GIF and calibration, so calibrate once more afterwards.

## What the customer sees

| Step | Clock screen | Phone |
| --- | --- | --- |
| 1 | **SCAN WITH YOUR PHONE** + QR code | Camera → opens the setup guide on korvixtech.store (with the clock's Wi-Fi name and password filled in) |
| 2 | (unchanged) | Phone Settings → Wi-Fi → join **Clock-Setup-…** with that password, then tap **Open clock setup** on the guide: tap your home Wi-Fi, type password, **Connect** |
| 3 | **Connecting to your Wi-Fi...** | "Connecting… about 20 seconds" |
| 4 | **CONNECTED!** + a second QR code for adding a GIF | "Done! Your clock is online" |
| 5 | Clock face (after **Done**, or by itself after 3 minutes) | Scan the second code any time to add a GIF |

Screens are shown in `images/`.

If the password was wrong, the clock says **That didn't work** and the phone asks to try again. The setup hotspot closes after 15 minutes without a phone, or 15 seconds after success. **Settings → Wi-Fi setup** opens it again (e.g. new router).

**Use without Wi-Fi** turns the radio off and asks for the time zone and the time on the touchscreen, as before. Without Wi-Fi the time must be set again after power loss (no battery clock on this board).

The setup hotspot `Clock-Setup-XXXXXX` uses a new random password every time; it is inside the QR code and also printed small on the screen for phones that can't scan Wi-Fi codes. The home Wi-Fi password is stored on the clock only (in its settings flash), never in the source code, and never printed to Serial.

## GIFs from GIPHY

Accepted links: `giphy.com/gifs/...`, `giphy.com/stickers/...`, `media.giphy.com/media/...`, `i.giphy.com/...`, `giphy.com/embed/...`, and `gph.is/...` short links. Text around the link (e.g. a pasted share message) is fine.

How it works: the phone does the heavy lifting. The page (served by the clock) downloads the GIF straight from GIPHY; if the phone's browser can't, the clock downloads it over HTTPS (certificate-checked) and hands it to the phone. The phone decodes the GIF, crops it to the screen's shape, samples it at 10 frames per second and converts it to 240 x 180 JPEG frames, shrinking quality until it fits. Then it uploads them to the clock.

Smooth playback: the clock decodes each frame completely into an 84 KB RAM buffer, scales it smoothly up to 320 x 240 and sends the whole picture to the screen in one pass (only the lines that changed; the per-second time update only rewrites the digits), so you never see half-drawn frames. Wi-Fi power saving is switched off while connected, because its on/off pulses can make the backlight dip on this board. A full-size buffer (150 KB) doesn't fit next to Wi-Fi on this board. Serial Monitor prints `Smooth GIF drawing: on` at start-up; if it ever says `OFF`, the GIF is shown smaller in the centre instead.

- Up to **10 seconds** (up to 150 frames) and **720 KB**. GIFs play at their own speed, 10–20 frames per second, limited to what the clock measured it can draw (Serial Monitor shows `GIF: NN ms per frame`). GIFs sent with the previous version should be sent again. Longer GIFs play their first part; the phone says so.
- The time is drawn on top, with the same darkened lower half as the built-in scenes.
- Transparent GIFs (stickers) get a black background.
- **Transition:** a newly sent (or picked) GIF opens on the clock like a portal from the centre of the screen; the phone shows the preview flying off with an "On your clock!" tick.
- **My GIFs:** every GIF you send stays on the clock (up to 32, up to 512 KB each). Tap a thumbnail on the phone page to play it again, or × to delete it. When storage is full, the oldest GIFs are removed automatically to make room.
- **Frame rate:** at start-up the clock tests how fast it can draw a GIF frame (Serial: `GIF speed test: ...`). Each GIF then plays at its own speed, up to that maximum (at most 30 frames/s). If a GIF is too big, the frame rate is lowered first, then the quality, then the length.
- The phone must be on the **same Wi-Fi** as the clock.

## Moving between screens

The screens sit side by side:

**Focus calendar | Lock In | Day & Night | My GIFs | Quote of the hour | Settings**

- On any clock face, **tap the left half** to go one screen left, **the right half** to go one screen right (or swipe).
- The three clock faces all show the time:
  - **Day & Night**: the animated sky follows the time of day (day, sunset, night).
  - **My GIFs**: your own GIFs. **Swipe up / down** for the next / previous saved GIF. With none saved yet it says how to add one.
  - **Quote of the hour**: a big motivational quote in the middle of the dimmed sky, the time underneath. A new quote every hour (66 built in, e.g. "Scared money ain't make money", "No risk, no Ferrari", "You vs You", "Time is ticking"). **Tap the middle of the screen** (or swipe up / down) for another one now; tapping near the left or right edge still moves between screens.
- The clock remembers which face you were on, also after a power cut.
- On Lock In, the **< ▦** button (bottom left) or a swipe right opens the Focus calendar; **Lock In >** goes back.
- **Clock >** on Lock In / Settings goes back to the clock face.

**Edit the quotes:** Settings → **Quotes** shows a QR code. Scan it and the phone page opens at the **Quotes** list: change a quote by typing in it, add new ones at the top (they show on the clock straight away), delete with ×, show any one now with ▶. Everything is saved on the clock (up to 200 quotes, 90 characters each; the clock adds the quotation marks and full stop). **Restore the original quotes** brings back the built-in list. Factory reset also restores it. The built-in list is in `ClockApp.h` (search for `QUOTES[]`).

## Lock In

Tap **−** (left side) or **+** (right side) to pick 1–24 hours, then **Start**. The timer counts up from 0:00:00 in the clock's number font, and a slim blue-to-green ring fills until you reach your target; "Ends at HH:MM" is shown underneath. **Reset** turns red ("Sure?") and needs a second tap within 3 seconds. When you reach the target the ring turns green, it says **Done!**, and the LED on the back blinks. A running Lock In also shows at the top of the clock face, and continues after a power cut.

**Animations:** pressing **Start** snaps a padlock shut with a flash, the ring sweeps round and the page fades in. Reaching the goal sends three green shockwaves out of the ring, draws a big checkmark, then slides up a **+5h today** card: this week's squares (today fills in), your streak ticking up with sparkles round its fire. Tap anywhere to close it. Previews: `previews/lock-in-start.gif`, `previews/lock-in-done.gif` (recorded from the real drawing code).

## Focus calendar

Like GitHub's contribution graph: one square per day for the last 19 weeks (columns are weeks, Sunday on top, today outlined in white). Every Lock In adds the time it ran to its day, and the square gets greener the longer you focused:

| Square | Focused that day |
| --- | --- |
| grey | nothing |
| darkest green | under 1 hour |
| dark green | 1–2 hours |
| green | 2–4 hours |
| bright green | 4 hours or more |

A finished Lock In counts in full; a Lock In you **Reset** early still counts the time you did. A session that runs past midnight is split between the two days. The running session shows live (today's square and the numbers update every minute). Underneath: total for the 19 weeks, **Today**, **This week** (since Sunday) and **Streak** (days in a row with any focus; today doesn't break it until it's over). The history (140 days) is kept in the clock's settings flash and survives power cuts; **Factory reset** clears it. It needs the clock to know the date, so sessions done before the time was ever set are not counted.

## Settings (the screen right of Quote of the hour)

**Time zone** · **Wi-Fi** (Connected / Not connected) · **Add a GIF** (shows the QR code) · **Quotes** (QR code to edit them on the phone) · **Date & time** · **Touch** (calibrate) · and a full-width **Reset** (factory reset) at the bottom. Turning GIFs on or off is done by moving to the **My GIFs** screen or away from it. **Clock >** at the top goes back.

## Time

Online, the clock gets UTC from internet time servers and converts it with the built-in time-zone table (598 IANA zones, rules 2020–2100, daylight saving included). The phone's time is used as a quick start during setup.

To pick a time zone by hand, hold the screen while the clock starts, or use Settings → Time zone, or the phone page (**Time zone and other settings**). `ASK_TIMEZONE_EVERY_BOOT = true` in `Config.h` asks on every start.

## Background schedule and footage

| Local time | Background |
| --- | --- |
| 06:00–16:59 | Day |
| 17:00–18:59 | Sunset |
| 19:00–05:59 | Night |

Change `DAY_START`, `SUNSET_START` and `NIGHT_START` in `Config.h`. `DEMO_BACKGROUNDS = true` cycles the scenes every ten seconds. The three clips are embedded in the firmware (151 JPEG frames, 320 x 240, 10 FPS target). `tools/embed_footage.py` regenerates them from the original MP4s.

## Editing the phone page

The page is `web/page.html` (plain HTML/JS, no internet libraries, so it also works inside the phone's Wi-Fi pop-up). After editing it, regenerate the firmware copy and re-upload:

```
python tools/make_web_page.py
```

## Troubleshooting

- **Sketch too big:** select **Huge APP (3MB No OTA/1MB SPIFFS)**.
- **Phone shows no pop-up after Join:** open `192.168.4.1` in the browser while still on the `Clock-Setup` Wi-Fi. On Android, if it says "no internet", choose to stay connected.
- **Customer's Wi-Fi isn't listed:** the clock needs 2.4 GHz Wi-Fi. Most routers broadcast both; "My Wi-Fi isn't listed" lets them type the name.
- **GIF page won't open:** phone and clock must be on the same Wi-Fi (not mobile data, not a guest network that isolates devices).
- **Blank screen:** for an ILI9341 board install **Adafruit ILI9341**, set `CLOCK_USE_ST7789` to `0`.
- **Negative or swapped colours:** adjust `DISPLAY_INVERT` / `DISPLAY_BGR`.
- **Upside down:** `DISPLAY_ROTATION` 1 ↔ 3 (touch calibration repeats).
- **Mirrored text:** `DISPLAY_MIRROR_X` / `DISPLAY_MIRROR_Y`.
- **Inaccurate touch:** Settings → Calibrate touch.
- **Display glitches:** lower `TFT_SPI_HZ` to `20000000`.
- **Diagnostics:** Serial Monitor at 115200 baud prints the clock's address once it joins Wi-Fi.

Pins: display HSPI SCK14/MOSI13/MISO12/CS15/DC2, backlight 21. Touch software SPI SCK25/MOSI32/MISO39/CS33/IRQ36. No extra wiring.

See `VALIDATION.md` for what was tested.
