// The care strips (#13): the small menus the Food, Play and moon buttons open over the need
// bars. Pure geometry and hit tests (no drawing, no millis() reads), tested on the PC
// (test/test_touchgesture.cpp); the sketch keeps which strip is open and until when
// (menuKind, menuUntil), and scene.cpp draws it (drawCareMenu).
//
//  - Food: the three berries and the candy, as it always was.
//  - Play: the Poké Ball (the next minigame) and the plush toy.
//  - Moon, only while the pet is awake: lights out, and warm milk, then lights out. Asleep,
//    the moon wakes the pet at once, with no strip.
//
// Every strip works the same way: it closes by itself after MENU_MS untouched, a tap on one
// of its items chooses it and closes it, and a tap anywhere else only closes it. While one
// is open it is a dialog to the rest of the game (careBusy() in TamaPoke.ino): bedtime and
// the attention calls wait for it, the pet gestures are off, the long press doesn't open
// the release dialog and the vertical swipes don't open the card or the clock.
#pragma once
#include <stdint.h>

enum : uint8_t { MENU_FOOD = 0, MENU_PLAY, MENU_MOON, MENU_KINDS };
// each strip's items, left to right (the food strip's berry items are feedBerry()'s colours)
enum : uint8_t { FOOD_RED = 0, FOOD_BLUE, FOOD_GREEN, FOOD_CANDY, FOOD_ITEMS };
enum : uint8_t { PLAY_BALL = 0, PLAY_PLUSH, PLAY_ITEMS };
enum : uint8_t { MOON_LIGHTS = 0, MOON_MILK, MOON_ITEMS };

#define MENU_MS 6000UL  // a strip closes by itself after this long
#define MENU_Y 288      // top edge; the strips cover the need bars, as the food strip always did
#define MENU_H 64
#define MENU_ITEM_W 66  // each item's slot; its 16x16 icon at x3 sits MENU_ICON_IN in from the slot's edges
#define MENU_ICON_IN 9
#define MENU_ICON_Y (MENU_Y + 8)
#define MENU_RADIUS 14
// The Food button's centre x (buttons[] in TamaPoke.ino): the hungry pet's spot sits under it
// (IDLE_SEEK_X, idleplan.h; idlelife.cpp checks it does).
#define CARE_FOOD_BTN_X 140  // test/test_garden.cpp (the_pot_is_clear_of_...) copies it: update it too

inline uint8_t menuItems(uint8_t kind) {
  if (kind == MENU_FOOD) return FOOD_ITEMS;
  if (kind == MENU_PLAY) return PLAY_ITEMS;
  if (kind == MENU_MOON) return MOON_ITEMS;
  return 0;
}
inline int menuW(uint8_t kind) { return menuItems(kind) * MENU_ITEM_W; }

// The strip's left edge. The food strip stays where it always was, centred on the screen (x
// 101): centred over its button (CARE_FOOD_BTN_X) it would run off the round panel. The
// two-item strips are centred over the button that opens them (`buttonCx`), so each one shows
// which button it belongs to.
inline int menuLeft(uint8_t kind, int buttonCx) {
  return kind == MENU_FOOD ? 233 - menuW(kind) / 2 : buttonCx - menuW(kind) / 2;
}

// The item under (x, y) on an open strip whose left edge is `left` (menuLeft): its index, or
// -1 off the strip (that tap only closes it). The right and bottom edges are outside.
inline int menuItemAt(uint8_t kind, int left, int x, int y) {
  int n = menuItems(kind);
  if (!n || y < MENU_Y || y >= MENU_Y + MENU_H || x < left || x >= left + n * MENU_ITEM_W) return -1;
  return (x - left) / MENU_ITEM_W;
}
