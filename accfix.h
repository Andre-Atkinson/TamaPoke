// The hand-made nudges for accessories (#116): the poses where accFindHead()'s rule
// (accessory.h) doesn't find the head by itself, one row per species and pose, checked by eye
// on the contact sheets of tools/acc_sheet.sh (every frame of all 151, standing, walking, sad
// and eating). A pose with no row uses the rule as it is.
//
// A row's pose is a PMD_* action (sdmon.h; sdmon.cpp checks the numbers): 0 standing (hops
// too: they are the standing frames bounced), 1 walking (WALKL; WALKR, its mirror image,
// uses the same row with dx turned round), 4 eating, 5 sad (Hurt: a sad pet stays in it),
// 7 POSE, 9 NOD, 10 BREATH, 11 SIT (the gestures). Sleep never shows one (put away).
//  - skip: rows of the frame's canvas above this one are not looked at (a tail, an ear or a
//    crest taller than the head);
//  - pct: the head's row needs this per cent of the frame's widest (0: ACC_HEAD_PCT, half): less
//    for a head narrower than half its body (Lapras, Doduo's two heads), more for a tail's
//    ring over half its width (Poliwag);
//  - band, mid: only the columns within `band` of the canvas's middle moved by `mid` are looked
//    at (band 0: all): a front-facing bird's head between its wings (mid 0), a head at the front
//    of a long back seen side-on (walking left: mid < 0; walking right turns it round);
//  - dx, dy: then move it, in sprite pixels (dx > 0 to the right of the screen when walking
//    left; dy > 0 down);
//  - hide: no accessory in this pose at all.
// Pure data. test/test_accessory.cpp checks every row (a species, a pose that shows one, no
// pose twice, a skip inside the canvas, small nudges) and that walking right finds the mirror
// image of walking left's head.
#pragma once
#include <stdint.h>
#include "accessory.h"  // AccHead, accFindHead

struct AccFix {
  uint8_t dex, act, skip, pct, band;
  int8_t mid, dx, dy;
  bool hide;
};

#define ACCF_STAND 0
#define ACCF_WALK 1  // WALKL; WALKR (2) mirrors it
#define ACCF_EAT 4
#define ACCF_SAD 5
#define ACCF_POSE 7
#define ACCF_NOD 9
#define ACCF_BREATH 10
#define ACCF_SIT 11

inline const AccFix ACC_FIXES[] = {
  // dex, pose, skip, pct, band, mid, dx, dy, hide
  { 60, ACCF_STAND, 0, 60, 0, 0, 0, 0, false },   // Poliwag: its tail's ring is over half its width
  { 84, ACCF_STAND, 0, 30, 0, 0, 0, 0, false },   // Doduo: one of its two heads, not their neck
  { 85, ACCF_STAND, 0, 30, 0, 0, 0, 0, false },   // Dodrio: one of its three heads
  { 131, ACCF_STAND, 0, 40, 0, 0, 0, 0, false },  // Lapras: its head, not its shell
  { 139, ACCF_STAND, 0, 30, 0, 0, 0, 0, false },  // Omastar: the shell's top, not its face
  { 144, ACCF_STAND, 0, 0, 8, 0, 0, 0, false },  // Articuno: its head between the wings
  { 146, ACCF_STAND, 0, 0, 8, 0, 0, 0, false },  // Moltres: its head between the wings
  { 145, ACCF_STAND, 0, 0, 8, 0, 0, 0, false },  // Zapdos: its head among the spikes
  { 37, ACCF_WALK, 0, 0, 5, -5, 0, 0, false },   // Vulpix: its head, in front of its tails
  { 38, ACCF_WALK, 0, 0, 4, -7, 0, 0, false },   // Ninetales: its head, in front of its tails
  { 51, ACCF_WALK, 0, 0, 0, 0, 0, 0, true },    // Dugtrio: only the earth it digs through shows
  { 53, ACCF_WALK, 0, 0, 5, -11, 0, 0, false },  // Persian: its head, not its back
  { 58, ACCF_WALK, 0, 0, 7, -9, 0, 0, false },   // Growlithe: its head, not its back
  { 77, ACCF_WALK, 0, 0, 5, -8, 0, 0, false },   // Ponyta: its head's mane, not its back
  { 79, ACCF_WALK, 0, 0, 5, -7, 0, 0, false },   // Slowpoke: its head, not its tail
  { 86, ACCF_WALK, 0, 0, 6, -6, 0, 0, false },   // Seel: its head, not its back
  { 87, ACCF_WALK, 0, 0, 5, -9, 0, 0, false },   // Dewgong: its head, not its back
  { 111, ACCF_WALK, 0, 0, 5, -11, 0, 0, false }, // Rhyhorn: its head, not its back
  { 128, ACCF_WALK, 0, 0, 4, -8, 0, 0, false },  // Tauros: its head, not its mane
  { 130, ACCF_WALK, 0, 0, 8, -22, 0, 0, false }, // Gyarados: its head, not its coils
  { 142, ACCF_WALK, 0, 0, 5, -12, 0, 0, false }, // Aerodactyl: its head, not its wing
  { 144, ACCF_WALK, 0, 0, 5, -12, 0, 0, false }, // Articuno: its head, not its wing
  { 145, ACCF_WALK, 0, 0, 5, -6, 0, 0, false },  // Zapdos: its head, not its spikes
  { 146, ACCF_WALK, 0, 0, 5, -20, 0, 0, false }, // Moltres: its head, not its wing
  { 22, ACCF_WALK, 0, 0, 5, -9, 0, 0, false }, // Fearow: its head, not its raised wing
  { 83, ACCF_SAD, 11, 0, 0, 0, 0, 0, false },   // Farfetch'd: not the leek it drops
  { 87, ACCF_SAD, 0, 0, 5, -4, 0, 0, false },   // Dewgong: its head, not its raised flipper
  { 104, ACCF_SAD, 12, 0, 0, 0, 0, 0, false },  // Cubone: not the bone it drops
  { 139, ACCF_SAD, 0, 30, 0, 0, 0, 0, false },  // Omastar: the shell's top, not its face
  { 144, ACCF_SAD, 0, 0, 5, 1, 0, 0, false },   // Articuno: its head between the wings
  { 146, ACCF_SAD, 0, 0, 5, 0, 0, 0, false },   // Moltres: its head between the wings
  { 150, ACCF_SAD, 0, 30, 0, 0, 0, 0, false },  // Mewtwo: its head, not its shoulders
  { 151, ACCF_EAT, 8, 0, 0, 0, 0, 0, false },   // Mew: not its tail's tip over its head
};
inline constexpr unsigned ACC_FIX_COUNT = sizeof(ACC_FIXES) / sizeof(ACC_FIXES[0]);

// The row for species `dex` in action `act`, or nullptr. WALKR (2) gets WALKL's row: the
// caller turns its dx round (accFixFor's `mirrored`).
inline const AccFix *accFixFor(int dex, uint8_t act, bool &mirrored) {
  mirrored = act == 2;
  uint8_t key = mirrored ? (uint8_t)ACCF_WALK : act;
  for (unsigned i = 0; i < ACC_FIX_COUNT; i++)
    if (ACC_FIXES[i].dex == dex && dex > 0 && ACC_FIXES[i].act == key) return &ACC_FIXES[i];
  return nullptr;
}

// Where the head is on one frame `fr` (w x h, action `act`) of species `dex`: accFindHead()
// with the species' row for that pose (skip kept inside the canvas, pct 0 meaning the rule's
// own, the band's middle turned round walking right). The one place that does it, for
// PmdMon::findHeads (sdmon.cpp) and the host test.
inline AccHead accHeadFor(const uint8_t *fr, uint8_t w, uint8_t h, int dex, uint8_t act) {
  bool mirrored;
  const AccFix *fix = accFixFor(dex, act, mirrored);
  uint8_t skip = fix && fix->skip < h ? fix->skip : 0;
  uint8_t pct = fix && fix->pct ? fix->pct : ACC_HEAD_PCT, band = fix ? fix->band : 0;
  int mid = fix ? (mirrored ? -fix->mid : fix->mid) : 0;
  return accFindHead(fr, w, h, skip, pct, band, mid);
}
