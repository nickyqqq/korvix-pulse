# Battery version (IP5306)

The same Magic Clock firmware, plus a **battery level in the bottom-right corner** of the
Day & Night, My GIFs and Quote screens: `87%` with a battery icon filled to the level.
At 15 % or less the level blinks. See `previews/battery-indicator.png`.

The only difference from the normal version: `BATTERY_INDICATOR = true` in `DigitalClock/Config.h`.
Upload exactly like the normal version (ESP32 Dev Module, Huge APP partition).

## Power wiring

    Battery (+) ──> IP5306 B+        Battery (-) ──> IP5306 B-
    IP5306 5V OUT (+) ──> board 5V   (P1 power connector, or a short USB cable)
    IP5306 GND ──────────> board GND

Charge the battery through the IP5306 module's own USB input (not the board's USB port).

## Reading the level: pick one

### A. Any IP5306 module (default, `BATTERY_SOURCE = 0`)

Most IP5306 boards have no data connection, so the clock measures the battery voltage
through two resistors into GPIO 35 (the **P3** connector: GND, IO35, IO22, IO21):

    IP5306 B+ (battery +) ──[100 kΩ]──┬── GPIO 35  (P3 "IO35")
                                      └──[100 kΩ]── GND

- Any two equal resistors from 1 kΩ to 220 kΩ work (`BATTERY_DIVIDER = 2.0`). It draws about 20 µA.
- Never connect the battery straight to GPIO 35 without the resistors (4.2 V would damage the pin).
- The percentage follows a normal Li-ion curve (4.18 V = 100 %, 3.79 V = 50 %, 3.30 V = 0 %).
- **Charging:** when you plug in the charger, a lightning bolt appears next to the number within a
  few seconds and the battery icon fills up again and again. The clock spots it because the battery
  voltage jumps up when charging starts and drops when it stops, so no extra wire is needed.
  (Switched on while already plugged in? It notices within about 5 minutes.)
- Want an exact charging signal instead? Wire the charger's **USB 5 V** through two equal resistors
  (10 kΩ each) into GPIO 27: `5 V ──[10k]──┬── IO27 (CN1)` and `└──[10k]── GND`,
  then set `CHARGE_DETECT_PIN = 27` in `Config.h`.
- Calibration: the Serial Monitor (115200) prints `Battery: 3.92 V = 70%` every minute.
  If your multimeter says e.g. 4.04 V when the clock says 3.92 V, set
  `BATTERY_CALIBRATION = 1.03` (4.04 / 3.92).

### B. IP5306 "I2C" chip (`BATTERY_SOURCE = 1`)

Some modules use the IP5306 with an I2C connection (often sold as "IP5306_I2C"; M5Stack uses it).
Wire SDA → **IO27** and SCL → **IO22** (the **CN1** connector: GND, IO22, IO27, 3.3V) and set
`BATTERY_SOURCE = 1`. The chip reports the level in steps of 25 % (0/25/50/75/100) and whether
it's charging; a small lightning bolt appears next to the number while charging.

## Battery wire on GPIO 27? Move it

GPIO 27 (on the CN1 connector) can't measure the battery: it belongs to the ESP32's second
voltage reader, which stops working whenever Wi-Fi is on, so the clock always reads nothing there.
Option B uses GPIO 27 only as a data line for the special IP5306 "I2C" chip, never for the battery wire.
Also, never connect the battery (up to 4.2 V) straight to any pin: the ESP32's pins take at most 3.3 V.
Use the two resistors and GPIO 35 as shown in option A.

## Restarts when you unplug the charger? Add a capacitor

When the charger is unplugged, the IP5306 switches its 5 V output from the charger to the
battery, and the output dips for a split second. That can restart the clock.

1. Fix it in hardware: the ESP32's power-dip protection ("brownout" reset) stays on, because
   running through a real power dip can corrupt saved settings.
2. Solder an electrolytic capacitor of **470 to 1000 uF, 10 V or more** across the
   IP5306's **5 V output** (OUT+ to OUT-), or across the 5 V and GND wires where they reach the clock.
   The capacitor has a stripe on one side: that leg is minus and goes to **GND**. Wrong way round
   can make it pop.
3. Still restarts? Open the Serial Monitor (115200): the first line says why the clock started.
   "restarted by a power dip" means the dip is still too long: use a bigger capacitor (1000-2200 uF).
   For products, fit the capacitor in every unit.

## If nothing shows

The indicator stays hidden while there is no valid reading (nothing connected, or a reading
outside 2.8–4.6 V). Check the Serial Monitor line `Battery: ...` and the wiring above.
