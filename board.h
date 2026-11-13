#pragma once

// Target board for this firmware.
//
//   0 = Waveshare ESP32-S3-Touch-AMOLED-1.75  (original TamaPoke board, microSD) - default
//   1 = Waveshare ESP32-S3-Touch-AMOLED-1.75C (aluminium case, no microSD, 32 MB flash)
//       Experimental community support. Build with -DBOARD_AMOLED_175C=1 and the
//       partition table in boards/175c/partitions.csv (tools/build_175c.sh does both).
//
// The default build (CI, web installer, Arduino IDE without extra flags) is the
// original 1.75 and is unchanged by the 1.75C code, which is all behind this flag.
#ifndef BOARD_AMOLED_175C
#define BOARD_AMOLED_175C 0
#endif

// Battery charge current (AXP2101 reg 0x62). The code is the XPowersLib enum value
// xpowers_axp2101_chg_curr_t: 300 mA = 0x09 (the chip's power-on default), 400 mA = 0x0A.
//   1.75C: 300 mA = 0.6C for its 500 mAh cell, with no battery temperature sensor (NTC)
//          to back off a hot charge, so the factory default rate is used.
//   1.75:  400 mA, the original setting for its 1100 mAh pack.
// Including this header needs XPowersLib's enum in scope (rtcbat.cpp).
#if BOARD_AMOLED_175C
#define CHARGE_CURRENT_CODE XPOWERS_AXP2101_CHG_CUR_300MA
#define CHARGE_CURRENT_MA 300
#else
#define CHARGE_CURRENT_CODE XPOWERS_AXP2101_CHG_CUR_400MA
#define CHARGE_CURRENT_MA 400
#endif
