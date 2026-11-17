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
  CHECK_EQ((int)S_SAY_HUNGRY + BUB_MILK, (int)S_SAY_MILK);  // #13
  CHECK_EQ(std::string(BUBBLE_KEYS[BUB_MILK]), std::string("milk"));
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

// the nature lines' total in a count by line (#86)
static int natureCount(const int n[BUB_LINES]) {
  int t = 0;
  for (int l = BUB_NAT_FIRST; l < BUB_GREET_FIRST; l++) t += n[l];
  return t;
}

TEST(Bubble, the_mix_is_30_time_20_nature_30_friendly_20_love_and_60_friendly_without_a_time_line) {
  int n[BUB_LINES] = {};
  for (uint32_t r = 0; r < 30000; r++) n[bubblePick(calm(true, 7, true), r)]++;
  CHECK_EQ(n[BUB_MORNING], 9000);
  CHECK_EQ(natureCount(n), 6000);  // #86
  CHECK_EQ(n[BUB_HI] + n[BUB_HAPPY] + n[BUB_PAT], 9000);
  CHECK_EQ(n[BUB_LOVE_NAME], 6000);
  CHECK_EQ(n[BUB_LOVE], 0);
  for (int l = BUB_HI; l <= BUB_PAT; l++) CHECK_RANGE(n[l], 2400, 3600);  // each friendly line comes up
  int m[BUB_LINES] = {};
  for (uint32_t r = 0; r < 30000; r++) m[bubblePick(calm(true, 13, false), r)]++;
  CHECK_EQ(m[BUB_HI] + m[BUB_HAPPY] + m[BUB_PAT], 18000);
  CHECK_EQ(natureCount(m), 6000);
  CHECK_EQ(m[BUB_LOVE], 6000);
  CHECK_EQ(m[BUB_LOVE_NAME], 0);  // no name: the line without one
}

TEST(Bubble, a_line_other_than_a_need_is_never_said_twice_in_a_row) {
  for (int last = 0; last < BUB_LINES; last++)
    for (uint8_t h : {7, 13, 22})
      for (bool milk : {false, true})  // the warm-milk hint (#13) due or not
        for (uint8_t nat = 0; nat < NAT_COUNT; nat++)  // every nature (#86)...
          for (bool hatch : {false, true})               // ...and its line after a hatch due or not
            for (uint32_t r = 0; r < 2000; r += 3) {
              BubbleIn in = calm(true, h, true, (uint8_t)last);
              in.milkHint = milk;
              in.nature = nat;
              in.hatchLine = hatch;
              uint8_t l = bubblePick(in, r);
              CHECK_MSG(l != last, "line " + std::string(BUBBLE_KEYS[last]) + " twice, r=" + std::to_string(r));
              CHECK(l >= BUB_MORNING && l < BUB_LINES);
              if (bubbleIsNatureLine(l))
                CHECK_MSG(l >= bubbleNatureLine(nat, 0) && l < bubbleNatureLine(nat, 0) + BUB_NAT_LINES,
                          std::string(BUBBLE_KEYS[l]) + " from " + NATURE_KEYS[nat]);
            }
}

// ---------------------------------------------------------------- the warm-milk hint (#13)
// MilkHint::due() decides whether the bubble that comes due is the hint; bubblePick() puts it
// after a low need.
static uint32_t at(uint32_t day, uint8_t h, uint8_t m = 0) { return day * 86400u + h * 3600u + m * 60u; }

TEST(Bubble, the_milk_hint_comes_after_a_low_need_and_before_the_other_lines) {
  for (uint32_t r = 0; r < 1000; r += 7) {
    BubbleIn in = calm(true, 20, true);
    in.milkHint = true;
    CHECK_EQ(bubblePick(in, r), (uint8_t)BUB_MILK);
    in.needs = attnLevels(80, 80, 20, 80);  // tired: the need's own line
    CHECK_EQ(bubblePick(in, r), (uint8_t)BUB_TIRED);
    in.needs = attnLevels(80, 80, 80, 80);
    in.greet = BUB_CHRISTMAS;  // a special day's greeting that is due comes first (#26)
    CHECK_EQ(bubblePick(in, r), (uint8_t)BUB_CHRISTMAS);
    in.greet = BUB_NONE;
    in.milkHint = false;
    CHECK(bubblePick(in, r) != BUB_MILK);  // never by chance
  }
}

TEST(Bubble, the_milk_hint_only_in_the_evening_with_a_known_clock_and_not_in_quiet_hours) {
  auto yes = [] { return (uint32_t)0; };  // the 1-in-MILK_HINT_ODDS roll comes up
  for (uint8_t h = 0; h < 24; h++) {
    MilkHint m;
    bool quiet = attnQuietAt(0, at(100, h, 30), false);  // bedtime off: 22:00-07:00
    CHECK_EQ(m.due(at(100, h, 30), quiet, yes), h >= MILK_HINT_FROM && h < 22);
  }
  // the bedtime window, when one is set, is when quiet hours start: 21:00 with preset 1
  MilkHint m;
  CHECK(m.due(at(100, 20, 59), attnQuietAt(1, at(100, 20, 59), false), yes));
  MilkHint q;
  CHECK(!q.due(at(100, 21, 0), attnQuietAt(1, at(100, 21, 0), false), yes));
  MilkHint late;
  CHECK(late.due(at(100, 22, 30), attnQuietAt(3, at(100, 22, 30), false), yes));  // preset 3: 23:00
  // no clock, or one that is stale (the caller passes 0 then)
  MilkHint none;
  CHECK(!none.due(0, false, yes));
}

TEST(Bubble, the_milk_hint_at_most_twice_an_evening_45_min_apart) {
  auto yes = [] { return (uint32_t)0; };
  MilkHint m;
  CHECK(m.due(at(100, 19, 0), false, yes));
  m.said(at(100, 19, 0));
  CHECK(!m.due(at(100, 19, 44), false, yes));
  CHECK(m.due(at(100, 19, 45), false, yes));
  m.said(at(100, 19, 45));
  CHECK(!m.due(at(100, 21, 0), false, yes));   // twice already
  CHECK(!m.due(at(100, 23, 59), false, yes));
  CHECK(m.due(at(101, 19, 0), false, yes));     // the next evening
}

TEST(Bubble, the_milk_hint_comes_now_and_then_and_rolls_only_in_the_evening) {
  struct {
    uint32_t v = 0;
    int calls = 0;
    uint32_t operator()() { calls++; return v; }
  } rnd;
  MilkHint m;
  CHECK(!m.due(at(100, 13, 0), false, [&] { return rnd(); }));
  CHECK(!m.due(at(100, 23, 0), true, [&] { return rnd(); }));  // quiet hours
  CHECK(!m.due(0, false, [&] { return rnd(); }));
  CHECK_MSG(rnd.calls == 0, "outside the evening the random sequence must not move");
  int hits = 0;
  for (uint32_t v = 0; v < 300; v++) {
    rnd.v = v;
    MilkHint e;
    hits += e.due(at(100, 20, 0), false, [&] { return rnd(); });
  }
  CHECK_EQ(hits, 300 / MILK_HINT_ODDS);
  CHECK_EQ(rnd.calls, 300);
}

TEST(Bubble, no_more_milk_hints_once_it_had_its_milk_that_evening) {
  auto yes = [] { return (uint32_t)0; };
  MilkHint m;
  m.milkGiven(at(100, 15, 0));                 // milk before a nap: the evening still hints
  CHECK(m.due(at(100, 19, 0), false, yes));
  m.milkGiven(at(100, 19, 10));
  CHECK(!m.due(at(100, 20, 30), false, yes));
  CHECK(m.due(at(101, 19, 30), false, yes));   // the next evening
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
  HatchLine hl;  // no hatch: never due
  const uint8_t greet = GREET_CHRISTMAS;
  auto choose = [&](uint32_t clock, bool quiet) { return bubbleChoose(in, 7, g, hl, clock, quiet, greet); };
  CHECK_EQ(choose(atDate(2026, 12, 25, 10, 0), false), (uint8_t)BUB_CHRISTMAS);
  CHECK(choose(atDate(2026, 12, 25, 10, 5), false) != BUB_CHRISTMAS);   // 5 min later: an ordinary line
  CHECK_EQ(choose(atDate(2026, 12, 25, 10, 40), false), (uint8_t)BUB_CHRISTMAS);
  CHECK_EQ(choose(atDate(2026, 12, 25, 21, 0), false), (uint8_t)BUB_CHRISTMAS);
  CHECK(choose(atDate(2026, 12, 25, 21, 40), false) != BUB_CHRISTMAS);  // three said today
  CHECK_EQ(g.count(), 3);
  // quiet hours: never, and nothing counted
  HolidayGreeter q;
  for (int i = 0; i < 6; i++)
    CHECK(bubbleChoose(in, 7, q, hl, atDate(2026, 12, 25, 10, 0) + i * 3600, true, greet) != BUB_CHRISTMAS);
  CHECK_EQ(q.count(), 0);
  // no trusted clock (0): never, and a day with no greeting says none
  HolidayGreeter z;
  CHECK(bubbleChoose(in, 7, z, hl, 0, false, greet) != BUB_CHRISTMAS);
  CHECK(bubbleChoose(in, 7, z, hl, atDate(2026, 12, 25, 10, 0), false, GREET_NONE) != BUB_CHRISTMAS);
  CHECK_EQ(z.count(), 0);
  // the day's greeting is the one said (the birthday's, not Christmas)
  CHECK_EQ(bubbleChoose(in, 7, z, hl, atDate(2026, 12, 25, 10, 0), false, GREET_BIRTHDAY), (uint8_t)BUB_BIRTHDAY);
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

// ---------------------------------------------------------------- the natures (#86)
// For comparing the stat card's name (capitals) with the line: ASCII, the CP437 capitals the
// font has (\200 C-cedilla, \220 E-acute) and the Latin-1 ones of ptglyphs.h.
static std::string folded(const char *s) {
  std::string o;
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    unsigned char c = *p;
    if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + 32);
    else if (c == 0x80) c = 0x87;
    else if (c == 0x90) c = 0x82;
    else if (c >= 0xC0 && c <= 0xDE && c != 0xD7) c = (unsigned char)(c + 32);
    o += (char)c;
  }
  return o;
}

TEST(Bubble, the_nature_lines_follow_the_strings_and_the_first_names_the_nature) {
  CHECK_EQ((int)S_SAY_HUNGRY + bubbleNatureLine(NAT_EASYGOING, 0), (int)S_SAY_EASYGOING_1);
  CHECK_EQ((int)S_SAY_HUNGRY + bubbleNatureLine(NAT_GREEDY, 1), (int)S_SAY_GREEDY_2);
  CHECK_EQ((int)S_SAY_HUNGRY + bubbleNatureLine(NAT_PLAYFUL, 2), (int)S_SAY_PLAYFUL_3);
  CHECK_EQ((int)S_SAY_HUNGRY + bubbleNatureLine(NAT_TIDY, 0), (int)S_SAY_TIDY_1);
  CHECK_EQ((int)S_SAY_HUNGRY + bubbleNatureLine(NAT_LAZY, 2), (int)S_SAY_LAZY_3);
  CHECK_EQ((int)S_SAY_LAZY_3 + 1, (int)S_SAY_CHRISTMAS_FMT);
  CHECK_EQ((int)bubbleNatureLine(NAT_LAZY, 2) + 1, (int)BUB_GREET_FIRST);
  CHECK(!bubbleIsNatureLine(BUB_LOVE));
  CHECK(bubbleIsNatureLine(BUB_NAT_FIRST));
  CHECK(bubbleIsNatureLine(bubbleNatureLine(NAT_LAZY, 2)));
  CHECK(!bubbleIsNatureLine(BUB_CHRISTMAS));
  // a nature out of range says easygoing's (Pet::load() reads it as easygoing)
  CHECK_EQ(bubbleNatureLine(NAT_COUNT, 1), bubbleNatureLine(NAT_EASYGOING, 1));
  CHECK_EQ(bubbleNatureLine(200, 0), (uint8_t)BUB_NAT_FIRST);
  CHECK_EQ(bubbleNatureLine(NAT_TIDY, 4), bubbleNatureLine(NAT_TIDY, 1));  // i taken modulo BUB_NAT_LINES
  for (uint8_t nat = 0; nat < NAT_COUNT; nat++)
    for (uint8_t i = 0; i < BUB_NAT_LINES; i++) {
      uint8_t l = bubbleNatureLine(nat, i);
      CHECK_EQ(std::string(BUBBLE_KEYS[l]), std::string(NATURE_KEYS[nat]) + "-" + std::to_string(i + 1));
      CHECK(!bubbleTakesName(l, true));  // no name in them
    }
  // the first line says the nature's name as the stat card's profile page does, in each language
  for (int lang = 0; lang < LANG_COUNT; lang++) {
    gLang = (Lang)lang;
    for (uint8_t nat = 0; nat < NAT_COUNT; nat++) {
      std::string line = folded(sayText(bubbleNatureLine(nat, 0)));
      CHECK_MSG(line.find(folded(natureName(nat))) != std::string::npos,
                line + " does not name " + natureName(nat) + " in " + LANG_ID[lang]);
    }
  }
  gLang = LANG_EN;
  CHECK_STREQ(sayText(bubbleNatureLine(NAT_PLAYFUL, 0)), "I'm playful!");
  CHECK_STREQ(sayText(bubbleNatureLine(NAT_LAZY, 2)), "Nice and slow...");
  gLang = LANG_PT;
  CHECK_STREQ(sayText(bubbleNatureLine(NAT_PLAYFUL, 0)), "Sou brincalh\343o!");
  CHECK_STREQ(sayText(bubbleNatureLine(NAT_LAZY, 0)), "Sou pregui\207oso!");
  gLang = LANG_DEFAULT;
}

TEST(Bubble, a_nature_says_only_its_own_lines_one_time_in_five) {
  for (int nat = 0; nat <= NAT_COUNT + 1; nat++) {  // the last two out of range: easygoing's
    uint8_t own = nat < NAT_COUNT ? (uint8_t)nat : (uint8_t)NAT_EASYGOING;
    for (uint8_t h : {7, 13, 22}) {  // a morning line, none, a night line
      int n[BUB_LINES] = {};
      for (uint32_t r = 0; r < 30000; r++) {
        BubbleIn in = calm(true, h, true);
        in.nature = (uint8_t)(nat == NAT_COUNT + 1 ? 200 : nat);
        n[bubblePick(in, r)]++;
      }
      CHECK_EQ(natureCount(n), 6000);
      for (uint8_t other = 0; other < NAT_COUNT; other++)
        for (uint8_t i = 0; i < BUB_NAT_LINES; i++) {
          int c = n[bubbleNatureLine(other, i)];
          if (other == own) CHECK_RANGE(c, 1600, 2400);  // each of its three comes up
          else CHECK_MSG(c == 0, std::string(BUBBLE_KEYS[bubbleNatureLine(other, i)]) + " from " + NATURE_KEYS[own]);
        }
      CHECK_EQ(n[BUB_LOVE_NAME], 6000);  // the player's name keeps its share
      // right after one of its lines: never that one again, another of its own instead
      // (all three: the last one wraps round to the first)
      for (uint8_t again = 0; again < BUB_NAT_LINES; again++) {
        int k[BUB_LINES] = {};
        for (uint32_t r = 0; r < 30000; r++) {
          BubbleIn in = calm(true, h, true, bubbleNatureLine(own, again));
          in.nature = (uint8_t)(nat == NAT_COUNT + 1 ? 200 : nat);
          k[bubblePick(in, r)]++;
        }
        CHECK_EQ(k[bubbleNatureLine(own, again)], 0);
        CHECK_EQ(natureCount(k), 6000);
        int ownLines = 0;
        for (uint8_t i = 0; i < BUB_NAT_LINES; i++) ownLines += k[bubbleNatureLine(own, i)];
        CHECK_EQ(ownLines, 6000);  // every nature line said is the pet's own
      }
    }
  }
}

TEST(Bubble, after_a_hatch_the_line_that_names_the_nature_comes_first_after_a_greeting_or_a_need) {
  for (uint8_t nat = 0; nat < NAT_COUNT; nat++) {
    uint8_t naming = bubbleNatureLine(nat, 0);
    for (uint32_t r = 0; r < 1000; r += 7) {
      BubbleIn in = calm(true, 20, true);
      in.nature = nat;
      in.hatchLine = true;
      in.milkHint = true;
      CHECK_EQ(bubblePick(in, r), naming);  // before the warm-milk hint and every ordinary line
      in.needs = attnLevels(80, 80, 20, 80);  // tired: the need's own line first
      CHECK_EQ(bubblePick(in, r), (uint8_t)BUB_TIRED);
      in.needs = attnLevels(80, 80, 80, 80);
      in.greet = BUB_CHRISTMAS;  // a special day's greeting that is due comes first
      CHECK_EQ(bubblePick(in, r), (uint8_t)BUB_CHRISTMAS);
      in.greet = BUB_NONE;
      in.last = naming;  // just said by chance: not twice in a row, it waits
      CHECK(bubblePick(in, r) != naming);
      CHECK_EQ(bubblePick(in, r), (uint8_t)BUB_MILK);
    }
    // not due: only now and then, by chance
    int said = 0;
    for (uint32_t r = 0; r < 30000; r++) {
      BubbleIn in = calm(true, 13, true);
      in.nature = nat;
      said += bubblePick(in, r) == naming;
    }
    CHECK_RANGE(said, 1600, 2400);
  }
}

TEST(Bubble, the_hatch_line_is_due_from_a_hatch_seen_until_its_line_is_said) {
  HatchLine h;
  for (int i = 0; i < 5; i++) CHECK(!h.watch(false));  // a pet from the start: nothing due
  CHECK(!h.due());
  CHECK(!h.watch(true));  // an egg
  CHECK(!h.due());
  CHECK(!h.watch(true));
  CHECK(h.watch(false));  // it hatched: true on that pass only
  CHECK(h.due());
  CHECK(!h.watch(false));
  CHECK(h.due());  // still due on later passes
  h.said(BUB_HI, NAT_TIDY);  // another line: still due
  h.said(bubbleNatureLine(NAT_PLAYFUL, 0), NAT_TIDY);  // another nature's: still due
  h.said(bubbleNatureLine(NAT_TIDY, 1), NAT_TIDY);    // its nature's other line: still due
  CHECK(h.due());
  h.said(bubbleNatureLine(NAT_TIDY, 0), NAT_TIDY);  // said: done
  CHECK(!h.due());
  CHECK(!h.watch(false));
  CHECK(!h.due());  // and it does not come back
  // a pet that leaves before saying it: the egg ends it, the next hatch makes it due again
  CHECK(!h.watch(true));
  CHECK(h.watch(false));
  CHECK(h.due());
  CHECK(!h.watch(true));
  CHECK(!h.due());
  CHECK(h.watch(false));
  CHECK(h.due());
  // everyPass(), what bubbleLoop() calls: the hatch pass forgets the previous pet's last line,
  // so a new tidy pet after a tidy one that last said "I'm tidy!" still says it first
  HatchLine p;
  const uint8_t tidy = bubbleNatureLine(NAT_TIDY, 0);
  uint8_t last = tidy;
  p.everyPass(false, last);  // no egg seen: kept
  CHECK_EQ(last, tidy);
  p.everyPass(true, last);  // an egg: kept (nothing shows over an egg)
  CHECK_EQ(last, tidy);
  p.everyPass(false, last);  // it hatched: forgotten
  CHECK_EQ(last, (uint8_t)BUB_NONE);
  CHECK(p.due());
  BubbleIn in = calm(true, 13, true, last);
  in.nature = NAT_TIDY;
  HolidayGreeter g;
  CHECK_EQ(bubbleChoose(in, 7, g, p, 0, false, GREET_NONE), tidy);
  last = BUB_HI;
  p.everyPass(false, last);  // later passes keep what was said
  CHECK_EQ(last, (uint8_t)BUB_HI);
}

// bubbleChoose() with a HatchLine is what bubbleLoop() runs (bubble.cpp is not compiled by the
// host tests): `last` is fed back as bubbleLoop()'s bubbleLine, BUB_NONE for a new pet.
TEST(Bubble, after_a_hatch_the_naming_line_comes_once_first_and_the_normal_mix_goes_on) {
  for (uint8_t nat = 0; nat < NAT_COUNT; nat++) {
    uint8_t naming = bubbleNatureLine(nat, 0);
    HatchLine h;
    HolidayGreeter g;
    h.watch(true);
    CHECK(h.watch(false));
    uint8_t last = BUB_NONE;
    // friendly picks only (r % 100 in 60..79): the naming line can only come from the hatch
    std::vector<uint8_t> said;
    for (uint32_t i = 0; i < 12; i++) {
      BubbleIn in = calm(true, 13, true, last);
      in.nature = nat;
      last = bubbleChoose(in, i * 100 + 60, g, h, 0, false, GREET_NONE);
      said.push_back(last);
    }
    CHECK_EQ(said[0], naming);  // first
    for (size_t i = 1; i < said.size(); i++) CHECK_MSG(said[i] != naming, "naming line again at bubble " + std::to_string(i));
    CHECK(!h.due());
    // and the mix after it is the normal one: its line by chance, 1 time in 15, not every other bubble
    int count = 0;
    for (uint32_t r = 0; r < 3000; r++) {
      BubbleIn in = calm(true, 13, true, last);
      in.nature = nat;
      last = bubbleChoose(in, r, g, h, 0, false, GREET_NONE);
      count += last == naming;
    }
    CHECK_RANGE(count, 100, 320);

    // a low need on the first bubble: it goes first, again while it lasts, and the naming line
    // is still due; it is the first bubble that is free of the need
    HatchLine d;
    d.watch(true);
    d.watch(false);
    last = BUB_NONE;
    for (int i = 0; i < 3; i++) {
      BubbleIn in = calm(true, 13, true, last);
      in.needs = attnLevels(5, 80, 80, 80);
      in.nature = nat;
      last = bubbleChoose(in, 60, g, d, 0, false, GREET_NONE);
      CHECK_EQ(last, (uint8_t)BUB_HUNGRY);
      CHECK(d.due());
    }
    BubbleIn fed = calm(true, 13, true, last);
    fed.nature = nat;
    CHECK_EQ(bubbleChoose(fed, 60, g, d, 0, false, GREET_NONE), naming);
    CHECK(!d.due());

    // an egg before it is said cancels it: the next pet says its own after its hatch
    HatchLine e;
    e.watch(true);
    e.watch(false);
    e.watch(true);  // the pet left
    BubbleIn egg = calm(true, 13, true, BUB_NONE);
    egg.nature = nat;
    CHECK(bubbleChoose(egg, 60, g, e, 0, false, GREET_NONE) != naming);
    CHECK(e.watch(false));
    CHECK_EQ(bubbleChoose(egg, 60, g, e, 0, false, GREET_NONE), naming);
  }
  // a greeting that is due goes first and leaves the hatch line due
  HatchLine h;
  HolidayGreeter g;
  h.watch(true);
  h.watch(false);
  BubbleIn in = calm(true, 10, true, BUB_NONE);
  CHECK_EQ(bubbleChoose(in, 60, g, h, atDate(2026, 12, 25, 10, 0), false, GREET_CHRISTMAS), (uint8_t)BUB_CHRISTMAS);
  CHECK(h.due());
}
