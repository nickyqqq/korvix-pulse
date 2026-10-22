#pragma once

#include <Arduino.h>

// XPT2046 on the ESP32-2432S028R. Software SPI leaves both hardware SPI
// controllers available for the display and microSD card.
// Protocol references (original implementation below):
// https://files.waveshare.com/wiki/common/XPT2046_Datasheet.pdf
//   Control byte / 24-clock timing / PENIRQ sections, pages 21-25.
// https://github.com/PaulStoffregen/XPT2046_Touchscreen/blob/master/XPT2046_Touchscreen.cpp
//   Confirms mode 0, channel commands, and 12-bit result shifted right by 3.
class BitBangTouch {
 public:
  void begin() {
    // GPIO36 and GPIO39 are input-only and have no internal pull-up.
    // The touch controller supplies PENIRQ's pull-up on this board.
    pinMode(kCs, OUTPUT);
    digitalWrite(kCs, HIGH);
    pinMode(kClock, OUTPUT);
    digitalWrite(kClock, LOW);
    pinMode(kMosi, OUTPUT);
    digitalWrite(kMosi, LOW);
    pinMode(kMiso, INPUT);
    pinMode(kIrq, INPUT);

    // Explicitly restore pen detection, including after an ESP32-only reset.
    digitalWrite(kCs, LOW);
    delayMicroseconds(1);
    finishRead();
    initialized_ = true;
  }

  // Returns true only for a stable press; x/y are unchanged on false.
  // Values are uncalibrated 0..4095 ADC coordinates. The caller owns all
  // display rotation, axis swapping, inversion, and calibration persistence.
  // No waiting for a press/release, interrupts, or hardware SPI reconfiguration.
  bool readRaw(uint16_t &x, uint16_t &y) {
    if (!initialized_ || digitalRead(kIrq) != LOW) return false;

    digitalWrite(kCs, LOW);
    delayMicroseconds(1);

    // PD1:PD0=01 keeps the ADC/panel drivers on while collecting samples.
    // Discard the first conversion after each channel change for settling.
    (void)convert(kZ1);
    const uint16_t z1 = convert(kZ1);
    (void)convert(kZ2);
    const uint16_t z2 = convert(kZ2);
    const uint16_t pressure = z1 + 4095U - z2;
    if (z1 == 0 || z2 == 4095 || pressure < kMinPressure) {
      finishRead();
      return false;
    }

    uint16_t xs[5];
    uint16_t ys[5];
    (void)convert(kX);
    for (uint8_t i = 0; i < 5; ++i) xs[i] = convert(kX);
    (void)convert(kY);
    for (uint8_t i = 0; i < 5; ++i) ys[i] = convert(kY);
    finishRead();

    // PENIRQ is valid again only after a completed power-down conversion.
    // This also rejects a finger that lifted during the sample sequence.
    if (digitalRead(kIrq) != LOW) return false;
    uint16_t averagedX, averagedY;
    if (!stableMean(xs, averagedX) || !stableMean(ys, averagedY)) return false;
    x = averagedX;
    y = averagedY;
    return true;
  }

 private:
  static constexpr uint8_t kClock = 25;
  static constexpr uint8_t kMosi = 32;
  static constexpr uint8_t kMiso = 39;
  static constexpr uint8_t kCs = 33;
  static constexpr uint8_t kIrq = 36;

  // These raw axis names follow the XPT2046_Touchscreen library; neither
  // denotes a landscape screen axis until the caller applies calibration.
  static constexpr uint8_t kX = 0x91;
  static constexpr uint8_t kY = 0xD1;
  static constexpr uint8_t kZ1 = 0xB1;
  static constexpr uint8_t kZ2 = 0xC1;
  static constexpr uint8_t kPowerDown = 0x90;
  static constexpr uint16_t kMinPressure = 200;
  static constexpr uint16_t kMaxMiddleSpread = 180;
  bool initialized_ = false;

  uint8_t transferByte(uint8_t value) {
    uint8_t received = 0;
    // Mode 0: clock idle low, send MSB first, sample on the rising edge.
    // One microsecond on each half-cycle stays below the 2 MHz clock limit.
    for (uint8_t mask = 0x80; mask != 0; mask >>= 1) {
      digitalWrite(kMosi, (value & mask) ? HIGH : LOW);
      delayMicroseconds(1);
      digitalWrite(kClock, HIGH);
      received = static_cast<uint8_t>((received << 1) |
                                     (digitalRead(kMiso) == HIGH ? 1 : 0));
      delayMicroseconds(1);
      digitalWrite(kClock, LOW);
    }
    return received;
  }

  uint16_t convert(uint8_t command) {
    // Each conversion uses exactly 8 command clocks + 16 receive clocks.
    // Receive data has one leading busy/null bit and three trailing bits.
    (void)transferByte(command);
    const uint16_t high = transferByte(0);
    const uint16_t low = transferByte(0);
    return (((high << 8) | low) >> 3) & 0x0FFFU;
  }

  void finishRead() {
    // PD1:PD0=00 restores PENIRQ. Clock the entire conversion before CS high.
    (void)convert(kPowerDown);
    digitalWrite(kCs, HIGH);
    digitalWrite(kMosi, LOW);
    delayMicroseconds(100);  // Allow the panel and PENIRQ pull-up to settle.
  }

  static bool stableMean(uint16_t *values, uint16_t &result) {
    // Sort just five readings, reject outliers, and average the middle three.
    for (uint8_t i = 1; i < 5; ++i) {
      const uint16_t value = values[i];
      uint8_t j = i;
      while (j > 0 && values[j - 1] > value) {
        values[j] = values[j - 1];
        --j;
      }
      values[j] = value;
    }
    if (values[3] - values[1] > kMaxMiddleSpread) return false;
    if (values[2] == 0 || values[2] == 4095) return false;
    result = (values[1] + values[2] + values[3] + 1U) / 3U;
    return true;
  }
};
