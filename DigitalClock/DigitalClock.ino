// Open in Arduino IDE. Board: ESP32 Dev Module, Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS).
// Customers set it up from their phone: scan the QR code on the clock screen.
#include "ClockApp.h"

// Extra room for HTTPS (the clock can download GIPHY GIFs itself as a backup).
SET_LOOP_TASK_STACK_SIZE(12 * 1024);

void setup() { clockSetup(); }
void loop() { clockLoop(); }
