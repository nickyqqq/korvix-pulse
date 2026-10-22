# Third-party code

These parts keep their own licences (not CC BY-NC):

| Part | Files | Licence |
| --- | --- | --- |
| QRCode by Richard Moore, based on Project Nayuki | `DigitalClock/qrcode.c`, `qrcode.h` | MIT, see `DigitalClock/QRCode-LICENSE.txt` |
| TJpgDec by ChaN (via Bodmer's TJpg_Decoder) | `DigitalClock/clk_tjpgd.c`, `clk_tjpgd.h`, `clk_tjpgdcnf.h` | ChaN's free licence (notice in the file header) |
| Time zone rules | `DigitalClock/TimeZones.h` (generated) | From the IANA tz database (public domain); see `tools/timezone_catalog_provenance.json` |

Libraries installed separately (not included here): Arduino core for ESP32 (LGPL-2.1), ESP-IDF (Apache-2.0),
Adafruit GFX and Adafruit ST7735/ST7789 (BSD), TJpg_Decoder by Bodmer (FreeBSD).
