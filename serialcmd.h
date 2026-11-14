// The USB serial console (#41: moved out of TamaPoke.ino): SD provisioning
// (sdSerialCommand in sdmon.cpp) and the debug commands listed in CLAUDE.md.
// loop() calls it; it reads at most one line per call.
#pragma once

void handleSerial();
