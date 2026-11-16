// Speech bubbles (bubble.h, #23): which line, when it shows, how it wraps and where it
// goes on the round screen, and that every line fits in every language. Pure logic.
#include "framework.h"
#include "../bubble.h"
#include "../holiday.h"
#include "../i18n.h"
#include "../owner.h"
#include <math.h>
#include <string.h>
#include <string>
#include <vector>

static BubbleIn calm(bool clockKnown = false, uint8_t hour = 13, bool named = false, uint8_t last = BUB_NONE) {
  BubbleIn in;
  in.needs = attnLevels(80, 80, 80, 80);
  in.clockKnown = clockKnown;
  in.hour = hour;
  in.named = named;
  in.last = last;
  return in;
}

// ---------------------------------------------------------------- the line
TEST(Bubble, the_lines_follow_the_needs_and_the_strings) {
  CHECK_EQ((int)BUB_HUNGRY + ATTN_FOOD, (int)BUB_HUNGRY);
  CHECK_EQ((int)BUB_HUNGRY + ATTN_HYGIENE, (int)BUB_BATH);
  CHECK_EQ((int)BUB_HUNGRY + ATTN_ENERGY, (int)BUB_TIRED);
  CHECK_EQ((int)BUB_HUNGRY + ATTN_JOY, (int)BUB_BORED);
  CHECK_EQ((int)S_SAY_HUNGRY + BUB_LOVE, (int)S_SAY_LOVE);
  CHECK_EQ((int)S_SAY_HUNGRY + BUB_LOVE_NAME, (int)S_SAY_LOVE_FMT);
  CHECK_EQ((int)S_SAY_HUNGRY + BUB_STARS, (int)S_SAY_STARS);
  CHECK_EQ((int)S_SAY_HUNGRY + BUB_PAT, (int)S_SAY_PAT);
  for (int i = 0; i < BUB_LINES; i++) CHECK(BUBBLE_KEYS[i] && BUBBLE_KEYS[i][0]);
}

TEST(Bubble, a_low_need_always_wins_with_the_attention_threshold) {
  for (uint32_t r = 0; r < 1000; r += 7) {
    BubbleIn in = calm(true, 7, true);
    in.needs = attnLevels(24, 80, 80, 80);
    CHECK_EQ(bubblePick(in, r), (uint8_t)BUB_HUNGRY);
    in.needs = attnLevels(80, 80, 80, 24);
    CHECK_EQ(bubblePick(in, r), (uint8_t)BUB_BATH);
    in.needs = attnLevels(80, 80, 24, 80);
    CHECK_EQ(bubblePick(in, r), (uint8_t)BUB_TIRED);
    in.needs = attnLevels(80, 24, 80, 80);
    CHECK_EQ(bubblePick(in, r), (uint8_t)BUB_BORED);
    // several low: the one the "!" bubble and the status line name (attnShown)
    in.needs = attnLevels(80, 0, 10, 20);
    CHECK_EQ(bubblePick(in, r), (uint8_t)(BUB_HUNGRY + attnShown(in.needs)));
    CHECK_EQ(bubblePick(in, r), (uint8_t)BUB_BATH);
    // 25 is not low (ATTN_LOW): no need line
    in.needs = attnLevels(25, 25, 25, 25);
    CHECK(bubblePick(in, r) >= BUB_MORNING);
  }
}

TEST(Bubble, a_need_repeats_while_it_lasts) {
  BubbleIn in = calm(false, 13, false, BUB_HUNGRY);
  in.needs = attnLevels(10, 80, 80, 80);
  for (uint32_t r = 0; r < 300; r++) CHECK_EQ(bubblePick(in, r), (uint8_t)BUB_HUNGRY);
}

TEST(Bubble, no_time_line_without_a_known_clock) {
  for (uint32_t r = 0; r < 3000; r++) {
    for (uint8_t h : {0, 3, 6, 8, 21, 23}) {
      uint8_t l = bubblePick(calm(false, h), r);
      CHECK_MSG(l != BUB_MORNING && l != BUB_STARS, "a time line with no clock, r=" + std::to_string(r));
    }
  }
}

TEST(Bubble, morning_and_night_lines_follow_the_hour) {
  for (int h = 0; h < 24; h++) {
    bool morning = h >= 6 && h < 10, night = h >= 20 || h < 6;  // the night scene's hours
    bool sawMorning = false, sawStars = false;
    for (uint32_t r = 0; r < 1000; r++) {
      uint8_t l = bubblePick(calm(true, (uint8_t)h), r);
      sawMorning |= l == BUB_MORNING;
      sawStars |= l == BUB_STARS;
    }
    CHECK_MSG(sawMorning == morning, "morning line at " + std::to_string(h) + ":00");
    CHECK_MSG(sawStars == night, "stars line at " + std::to_string(h) + ":00");
  }
}

// "Look at the stars" only when the stars are drawn: not under cloud, rain, snow or fog
// (#60, sceneStarsOut()). The morning line doesn't depend on the weather.
TEST(Bubble, no_stars_line_when_the_weather_hides_the_stars) {
  for (uint8_t h : { 20, 23, 0, 5 }) {
    bool sawStars = false, sawFriendly = false;
    for (uint32_t r = 0; r < 3000; r++) {
      BubbleIn in = calm(true, h);
      in.starsOut = false;
      uint8_t l = bubblePick(in, r);
      CHECK_MSG(l != BUB_STARS, "stars line under a covered sky at " + std::to_string(h) + ":00");
      sawFriendly |= l >= BUB_HI && l <= BUB_PAT;
      in.starsOut = true;
      sawStars |= bubblePick(in, r) == BUB_STARS;
    }
    CHECK(sawStars);
    CHECK(sawFriendly);
  }
  bool sawMorning = false;
  for (uint32_t r = 0; r < 1000; r++) {
    BubbleIn in = calm(true, 7);
    in.starsOut = false;
    sawMorning |= bubblePick(in, r) == BUB_MORNING;
  }
  CHECK(sawMorning);
}

TEST(Bubble, the_mix_is_about_30_50_20_and_80_20_without_a_time_line) {
  int n[BUB_LINES] = {};
  for (uint32_t r = 0; r < 30000; r++) n[bubblePick(calm(true, 7, true), r)]++;
  CHECK_EQ(n[BUB_MORNING], 9000);
  CHECK_EQ(n[BUB_HI] + n[BUB_HAPPY] + n[BUB_PAT], 15000);
  CHECK_EQ(n[BUB_LOVE_NAME], 6000);
  CHECK_EQ(n[BUB_LOVE], 0);
  for (int l = BUB_HI; l <= BUB_PAT; l++) CHECK_RANGE(n[l], 4000, 6000);  // each friendly line comes up
  int m[BUB_LINES] = {};
  for (uint32_t r = 0; r < 30000; r++) m[bubblePick(calm(true, 13, false), r)]++;
  CHECK_EQ(m[BUB_HI] + m[BUB_HAPPY] + m[BUB_PAT], 24000);
  CHECK_EQ(m[BUB_LOVE], 6000);
  CHECK_EQ(m[BUB_LOVE_NAME], 0);  // no name: the line without one
}

TEST(Bubble, a_line_other_than_a_need_is_never_said_twice_in_a_row) {
  for (int last = 0; last < BUB_LINES; last++)
    for (uint8_t h : {7, 13, 22})
      for (uint32_t r = 0; r < 2000; r++) {
        uint8_t l = bubblePick(calm(true, h, true, (uint8_t)last), r);
        CHECK_MSG(l != last, "line " + std::string(BUBBLE_KEYS[last]) + " twice, r=" + std::to_string(r));
        CHECK(l >= BUB_MORNING && l < BUB_LINES);
      }
}

// ---------------------------------------------------------------- when it shows
struct FakeRnd {
  uint32_t v = 0;
  int calls = 0;
  uint32_t operator()() { calls++; return v; }
};

// Runs the timer from `from` to `to` (exclusive) in steps of `step` ms; returns the starts.
static int runTimer(BubbleTimer &t, uint32_t from, uint32_t to, bool free, FakeRnd &rnd, uint32_t step = 100,
                    uint32_t *firstStart = nullptr) {
  int starts = 0;
  for (uint32_t now = from; (int32_t)(to - now) > 0; now += step) {
    if (t.update(now, free, [&] { return rnd(); })) {
      if (!starts && firstStart) *firstStart = now;
      starts++;
    }
  }
  return starts;
}

TEST(Bubble, the_first_one_waits_3_s_of_free_awake_screen) {
  BubbleTimer t;
  FakeRnd rnd;
  uint32_t at = 0;
  CHECK_EQ(runTimer(t, 1000, 60000, false, rnd), 0);  // never while not free
  CHECK(!t.showing());
  CHECK_EQ(runTimer(t, 60000, 63000, true, rnd), 0);  // free for less than 3 s
  CHECK_EQ(runTimer(t, 63000, 63100, true, rnd, 100, &at), 1);
  CHECK_EQ(at, 63000u);
  CHECK(t.showing());
}

TEST(Bubble, it_shows_for_4_5_s_and_the_next_comes_1_to_2_min_after) {
  BubbleTimer t;
  FakeRnd rnd;
  rnd.v = 30000;  // the gap: 60 s + 30 s
  uint32_t at = 0;
  CHECK_EQ(runTimer(t, 0, 3100, true, rnd, 100, &at), 1);
  CHECK_EQ(at, 3000u);
  CHECK_EQ(rnd.calls, 1);  // random only when one starts
  t.update(3000 + BUBBLE_SHOW_MS - 1, true, [&] { return rnd(); });
  CHECK(t.showing());
  t.update(3000 + BUBBLE_SHOW_MS, true, [&] { return rnd(); });
  CHECK(!t.showing());
  CHECK_EQ(runTimer(t, 3000 + BUBBLE_SHOW_MS, 93000, true, rnd, 100), 0);  // 90 s from its start
  rnd.v = 0xFFFFFFFFu;  // the gap after the next one: 60 s + 47 295 ms, still under 2 min
  CHECK_EQ(runTimer(t, 93000, 93100, true, rnd, 100, &at), 1);
  CHECK_EQ(at, 93000u);
  CHECK_EQ(rnd.calls, 2);
  uint32_t gap = BUBBLE_GAP_MS + 0xFFFFFFFFu % BUBBLE_GAP_SPAN_MS;
  CHECK(gap < 120000);
  CHECK_EQ(runTimer(t, 93100, 93000 + gap - 100, true, rnd), 0);
  CHECK_EQ(runTimer(t, 93000 + gap - 100, 93000 + 120000, true, rnd, 100, &at), 1);
  CHECK(at >= 93000 + gap);
}

TEST(Bubble, a_due_bubble_waits_for_the_screen_then_shows_3_s_after) {
  // the screen dims or goes off well before the next one is due; she wakes it 10 min later
  BubbleTimer t;
  FakeRnd rnd;
  CHECK_EQ(runTimer(t, 0, 3100, true, rnd), 1);
  CHECK_EQ(runTimer(t, 3100, 50000, true, rnd), 0);
  CHECK_EQ(runTimer(t, 50000, 690000, false, rnd), 0);  // dimmed, then off; due since 63 s
  uint32_t at = 0;
  CHECK_EQ(runTimer(t, 690000, 700000, true, rnd, 100, &at), 1);
  CHECK_EQ(at, 693000u);
}

TEST(Bubble, losing_the_screen_ends_it_and_the_settle_starts_over) {
  BubbleTimer t;
  FakeRnd rnd;
  CHECK_EQ(runTimer(t, 0, 3100, true, rnd), 1);
  CHECK(t.showing());
  t.update(4000, false, [&] { return rnd(); });  // a dialog opens, or the pet falls asleep
  CHECK(!t.showing());
  t.update(4100, true, [&] { return rnd(); });
  CHECK(!t.showing());  // it does not come back
  // not due again before 60 s from its start, however long the screen is free
  CHECK_EQ(runTimer(t, 4100, 63000, true, rnd), 0);
  // a screen that was busy until 70 s: due, but only 3 s after it is free again
  CHECK_EQ(runTimer(t, 63000, 70000, false, rnd), 0);
  uint32_t at = 0;
  CHECK_EQ(runTimer(t, 70000, 74000, true, rnd, 100, &at), 1);
  CHECK_EQ(at, 73000u);
}

TEST(Bubble, the_timer_survives_millis_wrapping) {
  BubbleTimer t;
  FakeRnd rnd;
  uint32_t t0 = 0xFFFFFFFFu - 2000;  // 2 s before the wrap
  uint32_t at = 0;
  CHECK_EQ(runTimer(t, t0, t0 + 4000, true, rnd, 100, &at), 1);
  CHECK_EQ(at, t0 + 3000);
  t.update(t0 + 3000 + BUBBLE_SHOW_MS - 100, true, [&] { return rnd(); });
  CHECK(t.showing());
  t.update(t0 + 3000 + BUBBLE_SHOW_MS, true, [&] { return rnd(); });
  CHECK(!t.showing());
}

TEST(Bubble, the_simulator_step_starts_one_that_still_needs_a_free_screen) {
  BubbleTimer t;
  FakeRnd rnd;
  t.start(5000);
  CHECK(t.showing());
  t.update(5100, true, [&] { return rnd(); });
  CHECK(t.showing());
  t.update(5200, false, [&] { return rnd(); });
  CHECK(!t.showing());
}

// ---------------------------------------------------------------- wrapping
static int fixed12(const char *, int len) { return len * 12; }

static int split(const char *s, int maxW) { return bubbleSplit(s, (int)strlen(s), maxW, fixed12); }

TEST(Bubble, short_text_stays_on_one_line) {
  CHECK_EQ(split("Hi there!", 108), -1);
  CHECK_EQ(split("Hi there!", 300), -1);
}

TEST(Bubble, long_text_splits_at_the_space_that_balances_the_lines) {
  // "Ich hab dich lieb, Abcdefghijkl!" is 32 characters: 384 px
  const char *s = "Ich hab dich lieb, Abcdefghijkl!";
  int br = split(s, 368);
  CHECK_EQ(br, 18);  // "Ich hab dich lieb," / "Abcdefghijkl!"
  CHECK_EQ(split("aaaa bbbb cccc dddd", 120), 9);  // 9 + 9 over 4 + 14
  // every space fits here: the middle one, not the first or the last
  CHECK_EQ(split("aa bb cc dd ee ff", 180), 8);
  CHECK_EQ(split("aaaa bbbb cccc dddd", 96), -2);  // no space gives two lines of 8
}

TEST(Bubble, text_that_two_lines_cannot_hold_is_refused) {
  CHECK_EQ(split("Abcdefghijklmnopqrstuvwxyz", 120), -2);  // no space at all
  CHECK_EQ(split(" Abcdefghijklmnopqrstuvwxyz ", 120), -2);  // only at the ends
}

// ---------------------------------------------------------------- the box on the round screen
static bool insidePanel(double x, double y, double margin) {
  double dx = x - 233, dy = y - 233;
  return sqrt(dx * dx + dy * dy) <= 233 - margin;
}

// A rounded box is inside the circle when each corner's arc is: its centre plus the radius.
static bool boxInside(const BubbleBox &b, double margin) {
  double r = BUBBLE_RADIUS;
  double cx[2] = { b.x + r, b.x + b.w - r }, cy[2] = { b.y + r, b.y + b.h - r };
  for (double x : cx)
    for (double y : cy) {
      double dx = x - 233, dy = y - 233;
      if (sqrt(dx * dx + dy * dy) + r > 233 - margin) return false;
    }
  return insidePanel(b.tipX, b.tipY, margin);
}

// Heads to try (#73): over the header (a hop of the tallest), the tallest (169), Charmander
// (215), the lowest awake (Fearow IDLE, 262), a lying Snorlax (266), and nothing found (the
// ground line, 304).
static const int HEADS[] = { 60, 120, 169, 215, 254, 262, 266, 304, 330 };

TEST(Bubble, the_box_stays_inside_the_round_panel_clear_of_header_and_buttons) {
  for (int button = 0; button < 2; button++)
    for (int headTop : HEADS)
      for (int badge = 0; badge < 2; badge++)
        for (int lines = 1; lines <= 2; lines++)
          for (int textW = 4; textW <= bubbleTextMax(badge); textW += 4)
            for (int petX = 150; petX <= 325; petX += 5) {
              BubbleBox b = bubbleBoxAt(petX, headTop, textW, lines, badge, button);
              bool ok = boxInside(b, 6) && b.y >= 108 && (!button || b.tipY < 167) && b.tipY <= 250 && b.y + b.h < 244 &&
                        b.w == (textW + 2 * BUBBLE_PAD_X > BUBBLE_W_MIN ? textW + 2 * BUBBLE_PAD_X : BUBBLE_W_MIN) &&
                        (!badge || b.x >= 76 + 24 + 6) && b.tailX0 >= b.x + BUBBLE_RADIUS &&
                        b.tailX1 <= b.x + b.w - BUBBLE_RADIUS && b.tailX1 - b.tailX0 == 14 && b.tipX >= b.x &&
                        b.tipX <= b.x + b.w && b.tipY == b.y + b.h + BUBBLE_TAIL;
              if (ok) continue;  // the messages are built only for a failure: this loop is long
              std::string at = "pet " + std::to_string(petX) + " head " + std::to_string(headTop) + " text " +
                               std::to_string(textW) + " lines " + std::to_string(lines) + (badge ? " badge" : "") +
                               (button ? " button" : "");
              CHECK_MSG(boxInside(b, 6), "outside the panel: " + at);
              CHECK_MSG(b.y >= 108, "over the status line: " + at);
              CHECK_MSG(!button || b.tipY < 167, "into the evolve button: " + at);  // EVO_BTN_Y 172 - 5 at its biggest pulse
              CHECK_MSG(b.tipY <= 250, "tail below BUBBLE_TIP_LOW: " + at);
              CHECK_MSG(b.y + b.h < 244, "over the poops: " + at);  // drawPoops: from y 244, up to x 192
              CHECK_MSG(b.w == (textW + 2 * BUBBLE_PAD_X > BUBBLE_W_MIN ? textW + 2 * BUBBLE_PAD_X : BUBBLE_W_MIN),
                        "text clipped: " + at);
              CHECK_MSG(!badge || b.x >= 76 + 24 + 6, "over the clock badge: " + at);
              // the tail leaves the straight part of the bottom edge and its tip is under the box
              CHECK_MSG(b.tailX0 >= b.x + BUBBLE_RADIUS && b.tailX1 <= b.x + b.w - BUBBLE_RADIUS,
                        "tail on a corner: " + at);
              CHECK_MSG(b.tailX1 - b.tailX0 == 14, "tail width: " + at);
              CHECK_MSG(b.tipX >= b.x && b.tipX <= b.x + b.w, "tail tip beside the box: " + at);
              CHECK_MSG(b.tipY == b.y + b.h + BUBBLE_TAIL, "tail length: " + at);
            }
}

// #73: the bubble used to sit at y 110 whatever the pet, its tail ending some 70 px over
// Charmander's head. Now the tip is 4 px over the head, as the emotes are by it.
TEST(Bubble, the_tail_points_down_at_the_head_and_follows_it) {
  BubbleBox b = bubbleBoxAt(200, 215, 84, 1, false, false);  // Charmander, "Pat me!"
  CHECK_EQ(b.tipY, 211);
  CHECK_EQ(b.y, 211 - BUBBLE_TAIL - 28);
  CHECK_EQ(bubbleBoxAt(200, 209, 84, 1, false, false).tipY, 205);  // a 6 px hop lifts it too
  CHECK_EQ(bubbleBoxAt(200, 215, 160, 2, false, false).tipY, 211);  // two lines: the box grows upward
  CHECK_EQ(bubbleBoxAt(200, 169, 84, 1, false, false).tipY, 165);  // the tallest pet
  CHECK_EQ(bubbleBoxAt(200, 254, 84, 1, false, false).tipY, 250);  // the lowest that still gets the 4 px gap
  CHECK_EQ(bubbleBoxAt(200, 262, 84, 1, false, false).tipY, 250);  // the lowest awake head (Fearow IDLE): stops over the poops
  CHECK_EQ(bubbleBoxAt(200, 266, 84, 1, false, false).tipY, 250);  // a lying Snorlax
  CHECK_EQ(bubbleBoxAt(200, 304, 84, 1, false, false).tipY, 250);  // no head found: not down at the bars
  // a head up in the header: the box stops under the status line, its tail over the head
  b = bubbleBoxAt(200, 120, 84, 1, false, false);
  CHECK_EQ(b.y, BUBBLE_Y_MIN);
  CHECK_EQ(b.tipY, BUBBLE_Y_MIN + 28 + BUBBLE_TAIL);
}

// The poops are drawn from y 244, from x 36 to 192 at most: a box beside a low head, which
// starts at x 186, must end above them whatever the head (every height, as it can be any).
TEST(Bubble, the_box_ends_above_the_poops_for_every_head_height) {
  for (int headTop = 0; headTop <= 340; headTop++)
    for (int button = 0; button < 2; button++)
      for (int lines = 1; lines <= 2; lines++)
        for (int petX = 150; petX <= 325; petX += 5) {
          BubbleBox b = bubbleBoxAt(petX, headTop, 84, lines, false, button);
          CHECK_MSG(b.y + b.h < 244, "box bottom " + std::to_string(b.y + b.h) + " at head " +
                                         std::to_string(headTop) + " pet " + std::to_string(petX));
        }
}

TEST(Bubble, with_a_button_over_the_pet_it_stays_above_the_button) {
  CHECK_EQ(bubbleBoxAt(200, 215, 84, 1, false, true).tipY, 166);  // not under the button
  CHECK_EQ(bubbleBoxAt(200, 215, 160, 2, false, true).tipY, 166);  // two lines too (the box top at y 111)
  CHECK_EQ(bubbleBoxAt(200, 150, 84, 1, false, true).tipY, 146);  // a head already above it: as without
}

TEST(Bubble, a_short_line_sits_beside_the_head_on_the_side_with_more_room) {
  BubbleBox b = bubbleBoxAt(200, 215, 84, 1, false, false);  // "Pat me!"
  CHECK_EQ(b.x, 200 + BUBBLE_GAP);
  CHECK(b.tipX < b.x + 20);  // the tail points back toward the pet
  b = bubbleBoxAt(300, 215, 84, 1, false, false);
  CHECK_EQ(b.x + b.w, 300 - BUBBLE_GAP);
  CHECK(b.tipX > b.x + b.w - 20);
  // the walk range limits where it goes, as for the "!" bubble
  CHECK_EQ(bubbleBoxAt(0, 215, 84, 1, false, false).x, bubbleBoxAt(150, 215, 84, 1, false, false).x);
  CHECK_EQ(bubbleBoxAt(466, 215, 84, 1, false, false).x, bubbleBoxAt(325, 215, 84, 1, false, false).x);
}

TEST(Bubble, a_wide_line_slides_over_the_pet_instead_of_off_the_screen) {
  BubbleBox b = bubbleBoxAt(300, 215, bubbleTextMax(false), 1, false, false);
  CHECK_EQ(b.x, BUBBLE_X_MIN);
  CHECK_EQ(b.x + b.w, BUBBLE_X_MAX);
  b = bubbleBoxAt(150, 215, 300, 1, false, false);
  CHECK_EQ(b.x + b.w, BUBBLE_X_MAX);
  CHECK_RANGE(b.tipX, 150, 170);
  b = bubbleBoxAt(300, 215, 200, 2, true, false);
  CHECK_EQ(b.x, BUBBLE_X_MIN_BADGE);
}

// ---------------------------------------------------------------- every line in every language
static const char *LANG_ID[LANG_COUNT] = { "EN", "PT" };

// Width at size 2: the classic font, 12 px per byte (textW); the Portuguese letters of
// ptglyphs.h have the same 6 px cell.
static int widthIn(int len) { return len * 12; }

// The string at S_SAY_HUNGRY + offset, as bubble.cpp's compose() passes it to bubbleFormat().
static const char *sayText(int offset) { return T((StrId)(S_SAY_HUNGRY + offset)); }

TEST(Bubble, every_line_fits_two_lines_in_every_language_with_the_longest_name) {
  // the longest name in the font's encoding: one byte per character, no space to wrap at;
  // and no name (the greetings then say only the day)
  char name[OWNER_NAME_MAX + 1];
  memset(name, 'W', OWNER_NAME_MAX);
  name[OWNER_NAME_MAX] = 0;
  for (int lang = 0; lang < LANG_COUNT; lang++) {
    gLang = (Lang)lang;
    for (int line = 0; line < BUB_LINES; line++) {
      for (const char *nm : { (const char *)name, "" }) {
        if (line == BUB_LOVE_NAME && !nm[0]) continue;  // only picked with a name
        char say[48];
        bubbleFormat(say, sizeof say, (uint8_t)line, nm, sayText);
        CHECK_MSG(say[0], std::string("empty: ") + BUBBLE_KEYS[line] + " in " + LANG_ID[lang]);
        CHECK_MSG(!strchr(say, '%'), std::string("unformatted: ") + say);
        for (int badge = 0; badge < 2; badge++) {
          int br = bubbleSplit(say, (int)strlen(say), bubbleTextMax(badge),
                               [](const char *, int len) { return widthIn(len); });
          CHECK_MSG(br != -2, std::string("does not fit: ") + say + " in " + LANG_ID[lang] + (badge ? " (badge)" : ""));
        }
      }
    }
  }
  gLang = LANG_DEFAULT;
}

TEST(Bubble, the_need_lines_and_most_others_fit_on_one_line) {
  // two lines are kept for the lines that carry a long name: everything else is one line,
  // even beside the clock badge (a greeting without a name included)
  for (int lang = 0; lang < LANG_COUNT; lang++) {
    gLang = (Lang)lang;
    for (int line = 0; line < BUB_LINES; line++) {
      if (line == BUB_LOVE_NAME) continue;
      char s[48];
      bubbleFormat(s, sizeof s, (uint8_t)line, "", sayText);
      CHECK_MSG(widthIn((int)strlen(s)) <= bubbleTextMax(true), std::string("two lines: ") + s);
    }
  }
  gLang = LANG_DEFAULT;
}

// ---------------------------------------------------------------- the special days (#26)
TEST(Bubble, a_greeting_says_the_day_with_the_name_or_without) {
  gLang = LANG_EN;
  char say[48];
  bubbleFormat(say, sizeof say, (uint8_t)BUB_CHRISTMAS, "Sam", sayText);
  CHECK_STREQ(say, "Merry Christmas, Sam!");
  bubbleFormat(say, sizeof say, (uint8_t)BUB_CHRISTMAS, "", sayText);
  CHECK_STREQ(say, "Merry Christmas!");
  bubbleFormat(say, sizeof say, (uint8_t)BUB_BIRTHDAY, "Sam", sayText);
  CHECK_STREQ(say, "Happy birthday, Sam!");
  bubbleFormat(say, sizeof say, (uint8_t)BUB_BIRTHDAY, "", sayText);
  CHECK_STREQ(say, "Happy birthday!");
  bubbleFormat(say, sizeof say, (uint8_t)BUB_NEW_YEAR, "Sam", sayText);
  CHECK_STREQ(say, "Happy New Year, Sam!");
  bubbleFormat(say, sizeof say, (uint8_t)BUB_HALLOWEEN, "", sayText);
  CHECK_STREQ(say, "Happy Halloween!");
  bubbleFormat(say, sizeof say, (uint8_t)BUB_LOVE, "Sam", sayText);  // the plain love line never takes the name
  CHECK_STREQ(say, "I love you!");
  gLang = LANG_PT;
  bubbleFormat(say, sizeof say, (uint8_t)BUB_EASTER, "Sam", sayText);
  CHECK_STREQ(say, "Feliz P\240scoa, Sam!");
  bubbleFormat(say, sizeof say, (uint8_t)BUB_VALENTINE, "", sayText);
  CHECK_STREQ(say, "Feliz Dia dos Namorados!");
  gLang = LANG_DEFAULT;
  // only a string with the name is used as a format (a plain one is copied as it is)
  CHECK(bubbleTakesName(BUB_CHRISTMAS, true));
  CHECK(!bubbleTakesName(BUB_CHRISTMAS, false));
  CHECK(!bubbleTakesName(BUB_BIRTHDAY, false));
  CHECK(bubbleTakesName(BUB_LOVE_NAME, false));
  CHECK(!bubbleTakesName(BUB_LOVE, true));
  CHECK(!bubbleTakesName(BUB_HI, true));
  CHECK_EQ((int)BUB_GREET_FIRST + GREET_BIRTHDAY - GREET_CHRISTMAS, (int)BUB_BIRTHDAY);
  CHECK_EQ((int)BUB_BIRTHDAY + 1, (int)BUB_LINES);
}

// bubbleChoose() is what bubbleLoop() calls: the pick, the HolidayGreeter's rules and its
// count (bubble.cpp itself is not compiled by the host tests). A local epoch for a date,
// worked out as test_holiday.cpp's at().
static uint32_t atDate(int y, int m, int d, int hh, int mm) {
  y -= m <= 2;
  int era = y / 400, yoe = y - era * 400;
  int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (uint32_t)(era * 146097 + doe - 719468) * 86400u + (uint32_t)(hh * 3600 + mm * 60);
}

TEST(Bubble, the_greeting_comes_3_times_a_day_30_min_apart_and_never_in_quiet_hours) {
  BubbleIn in = calm(true, 10, false, BUB_HI);
  HolidayGreeter g;
  const uint8_t greet = GREET_CHRISTMAS;
  auto choose = [&](uint32_t clock, bool quiet) { return bubbleChoose(in, 7, g, clock, quiet, greet); };
  CHECK_EQ(choose(atDate(2026, 12, 25, 10, 0), false), (uint8_t)BUB_CHRISTMAS);
  CHECK(choose(atDate(2026, 12, 25, 10, 5), false) != BUB_CHRISTMAS);   // 5 min later: an ordinary line
  CHECK_EQ(choose(atDate(2026, 12, 25, 10, 40), false), (uint8_t)BUB_CHRISTMAS);
  CHECK_EQ(choose(atDate(2026, 12, 25, 21, 0), false), (uint8_t)BUB_CHRISTMAS);
  CHECK(choose(atDate(2026, 12, 25, 21, 40), false) != BUB_CHRISTMAS);  // three said today
  CHECK_EQ(g.count(), 3);
  // quiet hours: never, and nothing counted
  HolidayGreeter q;
  for (int i = 0; i < 6; i++)
    CHECK(bubbleChoose(in, 7, q, atDate(2026, 12, 25, 10, 0) + i * 3600, true, greet) != BUB_CHRISTMAS);
  CHECK_EQ(q.count(), 0);
  // no trusted clock (0): never, and a day with no greeting says none
  HolidayGreeter z;
  CHECK(bubbleChoose(in, 7, z, 0, false, greet) != BUB_CHRISTMAS);
  CHECK(bubbleChoose(in, 7, z, atDate(2026, 12, 25, 10, 0), false, GREET_NONE) != BUB_CHRISTMAS);
  CHECK_EQ(z.count(), 0);
  // the day's greeting is the one said (the birthday's, not Christmas)
  CHECK_EQ(bubbleChoose(in, 7, z, atDate(2026, 12, 25, 10, 0), false, GREET_BIRTHDAY), (uint8_t)BUB_BIRTHDAY);
}

TEST(Bubble, a_due_greeting_comes_first_even_before_a_need) {
  BubbleIn in = calm(true, 9, true, BUB_HI);
  in.needs = attnLevels(5, 80, 80, 80);  // hungry
  in.greet = BUB_CHRISTMAS;
  for (uint32_t r = 0; r < 300; r++) CHECK_EQ(bubblePick(in, r), (uint8_t)BUB_CHRISTMAS);
  in.last = BUB_CHRISTMAS;  // even right after the same greeting (HolidayGreeter spaces them)
  CHECK_EQ(bubblePick(in, 7), (uint8_t)BUB_CHRISTMAS);
  in.greet = BUB_NONE;  // none due: the need, as before
  CHECK_EQ(bubblePick(in, 7), (uint8_t)BUB_HUNGRY);
  in.greet = BUB_LOVE;  // not a greeting line: ignored
  CHECK_EQ(bubblePick(in, 7), (uint8_t)BUB_HUNGRY);
}

