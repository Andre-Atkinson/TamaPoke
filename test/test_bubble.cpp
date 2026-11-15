// Speech bubbles (bubble.h, #23): which line, when it shows, how it wraps and where it
// goes on the round screen, and that every line fits in every language. Pure logic.
#include "framework.h"
#include "../bubble.h"
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
  CHECK_EQ((int)S_SAY_HUNGRY + BUB_LINES - 1, (int)S_SAY_LOVE);
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

TEST(Bubble, the_box_stays_inside_the_round_panel_clear_of_header_and_buttons) {
  for (int badge = 0; badge < 2; badge++)
    for (int lines = 1; lines <= 2; lines++)
      for (int textW = 4; textW <= bubbleTextMax(badge); textW += 4)
        for (int petX = 150; petX <= 325; petX += 5) {
          BubbleBox b = bubbleBoxAt(petX, textW, lines, badge);
          std::string at = "pet " + std::to_string(petX) + " text " + std::to_string(textW) + " lines " +
                           std::to_string(lines) + (badge ? " badge" : "");
          CHECK_MSG(boxInside(b, 6), "outside the panel: " + at);
          CHECK_MSG(b.y >= 108, "over the status line: " + at);
          CHECK_MSG(b.tipY < 167, "into the evolve button: " + at);  // EVO_BTN_Y 172 - 5 at its biggest pulse
          CHECK_MSG(b.w == (textW + 2 * BUBBLE_PAD_X > BUBBLE_W_MIN ? textW + 2 * BUBBLE_PAD_X : BUBBLE_W_MIN),
                    "text clipped: " + at);
          CHECK_MSG(!badge || b.x >= 76 + 24 + 6, "over the clock badge: " + at);
          // the tail leaves the straight part of the bottom edge and its tip is under the box
          CHECK_MSG(b.tailX0 >= b.x + BUBBLE_RADIUS && b.tailX1 <= b.x + b.w - BUBBLE_RADIUS, "tail on a corner: " + at);
          CHECK_MSG(b.tailX1 - b.tailX0 == 14, "tail width: " + at);
          CHECK_MSG(b.tipX >= b.x && b.tipX <= b.x + b.w, "tail tip beside the box: " + at);
          CHECK_MSG(b.tipY == b.y + b.h + BUBBLE_TAIL, "tail length: " + at);
        }
}

TEST(Bubble, a_short_line_sits_beside_the_head_on_the_side_with_more_room) {
  BubbleBox b = bubbleBoxAt(200, 84, 1, false);  // "Pat me!"
  CHECK_EQ(b.x, 200 + BUBBLE_GAP);
  CHECK(b.tipX < b.x + 20);  // the tail points back toward the pet
  b = bubbleBoxAt(300, 84, 1, false);
  CHECK_EQ(b.x + b.w, 300 - BUBBLE_GAP);
  CHECK(b.tipX > b.x + b.w - 20);
  // the walk range limits where it goes, as for the "!" bubble
  CHECK_EQ(bubbleBoxAt(0, 84, 1, false).x, bubbleBoxAt(150, 84, 1, false).x);
  CHECK_EQ(bubbleBoxAt(466, 84, 1, false).x, bubbleBoxAt(325, 84, 1, false).x);
}

TEST(Bubble, a_wide_line_slides_over_the_pet_instead_of_off_the_screen) {
  BubbleBox b = bubbleBoxAt(300, bubbleTextMax(false), 1, false);
  CHECK_EQ(b.x, BUBBLE_X_MIN);
  CHECK_EQ(b.x + b.w, BUBBLE_X_MAX);
  b = bubbleBoxAt(150, 300, 1, false);
  CHECK_EQ(b.x + b.w, BUBBLE_X_MAX);
  CHECK_RANGE(b.tipX, 150, 170);
  b = bubbleBoxAt(300, 200, 2, true);
  CHECK_EQ(b.x, BUBBLE_X_MIN_BADGE);
}

// ---------------------------------------------------------------- every line in every language
static const char *LANG_ID[LANG_COUNT] = { "EN", "PT" };

// Width at size 2: the classic font, 12 px per byte (textW); the Portuguese letters of
// ptglyphs.h have the same 6 px cell.
static int widthIn(int len) { return len * 12; }

TEST(Bubble, every_line_fits_two_lines_in_every_language_with_the_longest_name) {
  // the longest name in the font's encoding: one byte per character, no space to wrap at
  char name[OWNER_NAME_MAX + 1];
  memset(name, 'W', OWNER_NAME_MAX);
  name[OWNER_NAME_MAX] = 0;
  for (int lang = 0; lang < LANG_COUNT; lang++) {
    gLang = (Lang)lang;
    for (int line = 0; line < BUB_LINES; line++) {
      char say[48];
      if (line == BUB_LOVE_NAME) snprintf(say, sizeof say, T(S_SAY_LOVE_FMT), name);
      else snprintf(say, sizeof say, "%s", T((StrId)(S_SAY_HUNGRY + line)));
      CHECK_MSG(say[0], std::string("empty: ") + BUBBLE_KEYS[line] + " in " + LANG_ID[lang]);
      for (int badge = 0; badge < 2; badge++) {
        int br = bubbleSplit(say, (int)strlen(say), bubbleTextMax(badge),
                             [](const char *, int len) { return widthIn(len); });
        CHECK_MSG(br != -2, std::string("does not fit: ") + say + " in " + LANG_ID[lang] + (badge ? " (badge)" : ""));
      }
    }
  }
  gLang = LANG_DEFAULT;
}

TEST(Bubble, the_need_lines_and_most_others_fit_on_one_line) {
  // two lines are kept for the love line with a long name: everything else is one line,
  // even beside the clock badge
  for (int lang = 0; lang < LANG_COUNT; lang++) {
    gLang = (Lang)lang;
    for (int line = 0; line < BUB_LINES; line++) {
      if (line == BUB_LOVE_NAME) continue;
      const char *s = T((StrId)(S_SAY_HUNGRY + line));
      CHECK_MSG(widthIn((int)strlen(s)) <= bubbleTextMax(true), std::string("two lines: ") + s);
    }
  }
  gLang = LANG_DEFAULT;
}

