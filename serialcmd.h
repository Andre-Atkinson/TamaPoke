// The USB serial console (#41: moved out of TamaPoke.ino): SD provisioning
// (sdSerialCommand in sdmon.cpp) and the debug commands listed in CLAUDE.md.
// loop() calls it; it reads at most one line per call.
#pragma once

// The console's transmit timeout, set in setup(): 0, so a board with no monitor never blocks
// on Serial.print (what does not fit the USB transmit buffer is dropped). A long reply that
// needs every line (BATLOG?, batdiary.cpp, its DONE included) raises it to a few ms while it
// prints, then sets this back; a stalled write call retries 20 times (HWCDC::write()), so it holds the loop up to
// 100 ms (BATLOG_STALL_MAX_MS, batdiary.h).
#define SERIAL_TX_TIMEOUT_MS 0

void handleSerial();
