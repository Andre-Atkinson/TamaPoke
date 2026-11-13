# Design: random minigames for Play

Status: **approved.** On 2026-11-13 the owner took all six recommendations
(see "Decisions" at the end): keepy-uppy, Berry Catch, Peekaboo and Berry Song
in a shuffle bag; per-game records on the Medals page; the "Hold: exit" chip;
25–40 s games with a gentle ramp; the reflex/calm mix as proposed. Bubble Pop
is not in this release.

## Goal

Play starts one of several short touch games at random instead of always
keepy-uppy. Every game pays the same kind of reward, so it doesn't matter for
the pet's care which one comes up. Care feature only: no battles, no network.

## What exists today (checked in the code)

- **Keepy-uppy** (`TamaPoke.ino`): `startGame()`, `respawnBall()`, `gameTap()`,
  `stepGame()`, `renderGame()`. Three misses end it; a 4 s result screen
  follows. Physics runs in real time scaled to an 85 ms step.
- **Reward:** `Pet::playResult(score)` (`pet.cpp`): JOY +5 + 2/point (full at
  15 points), SPEED training `trSpe += score/5`, ENE and FOOD drop with floors,
  weight burns `2 × score`, a heart at 5+, bond +2, counts as the day's care,
  saves. It also keeps the record `gameHi` (NVS key `ghi`).
- **Power:** while `gameOpen`, frames come every 85 ms at 240 MHz and the
  screen never turns itself off (`power.h`). Keepy-uppy always ends by itself
  because the ball keeps falling.
- **Input:** a game sees a tap only when the finger lifts (`onTap`, < 40 px of
  movement, < 1.5 s). Swipes are ignored while a game is open.
- **Assets:** the pet's PMD actions (`IDLE, WALKL/R, EAT, HURT, POSE, HOP, NOD,
  SIT`...), the berry and candy icons (`SPR_ICON_FOOD`, `_BERRY_B`, `_BERRY_G`,
  `_CANDY`), the Pokéball icon, the bath bubbles, the habitat background
  (`drawGameScene()`), and 10 square-wave sound effects (`audio.cpp`).

### Two problems found while reading (fixed as part of this work)

1. **Tapping a high ball quits keepy-uppy.** `gameTap()` treats any tap with
   `y < 72` as "leave without reward", but the ball bounces off the round wall
   up to `y ≈ 28`. Aiming at a high ball ends the game with nothing.
2. **A 3 s still finger during a game can open "Release?".** The long-press
   check in `handleTouch()` skips the gallery, card, keyboard and clock but
   not `gameOpen`. If it fires in the pet zone, the release dialog appears
   when the game ends, and a tap on YES releases the pet. Unlikely in
   keepy-uppy, but likely in Berry Catch, where she'd hold a finger still.
   Games will get their own input path (below), which closes this.

## The games

Each game is 25–40 s for a typical player, starts easy and speeds up gently.
The numbers are starting values to tune in the simulator and on the board.

| Game | Feel | Ends | Raw score |
|---|---|---|---|
| Keepy-uppy | reflex | 3 misses (unchanged) | taps on the ball |
| Berry Catch | in between | 30 s | berries caught |
| Peekaboo | reflex | 20 pop-ups | pop-ups tapped |
| Berry Song | calm, memory | 3 wrong answers, or a 12-note song | longest song repeated |
| Bubble Pop *(optional)* | calm-ish | 30 s | bubbles popped |

### Keepy-uppy (existing)

Physics and constants unchanged. Changes: the new exit (below) replaces the
header tap, a hit counts when the finger touches (not when it lifts), and the
end goes through the shared reward path. The logic moves into a header behind
the same interface as the others, with a test that pins today's physics so the
move can't change the feel.

### Berry Catch

```
        [ Hold: exit ]
     ●●●●●●●●○○○○○○○        time bar
          12
      🍓        🫐           berries fall
            🟤               rotten berry (avoid)
               🍓
    ~~~~~~~~~~~~~~~~~~~~
          (pet) ← slide →    pet walks along the ground
```

- She slides a finger anywhere in the lower half; the pet walks to the
  finger's x (smoothed, `WALKL/WALKR`, `IDLE` when still).
- Red, blue and green berries fall from the top (x 100–366, inside the circle).
  A catch is a berry crossing the pet's mouth line within ±55 px of the pet:
  +1, a short `EAT` frame, `SFX_EAT`.
- **Rotten berries** (a brown recolour of the berry icon, one new 16×16 map):
  −2 (never below 0), `HURT` frame, `SFX_DENY`. None in the first 5 s.
- **Favourite berry** counts 2 once she has discovered it (`berryKnown`), with
  a heart. The game never reveals the favourite.
- Ramp over the 30 s: a berry every 1100 → 650 ms, falling 150 → 260 px/s,
  rotten share 10 % → 30 %.

### Peekaboo

```
        [ Hold: exit ]
             7
       (bush)    (bush)
                              5 bushes, ~110 px wide
    (bush)   (bush)   (bush)
```

- Five bushes (two above, three below, all inside the circle). The pet pops up
  from one (`POSE`/`HOP`), stays a moment and ducks back.
- Tap the pet or its bush while it's up: +1, `NOD`, `SFX_PLAY`. A second tap on
  the same pop-up doesn't count again. A tap on an empty bush only rustles it
  (no penalty).
- 20 pop-ups per game, never the same bush twice in a row. Time up: 1500 →
  750 ms; gap between pop-ups 500 → 300 ms (about 30 s in all).
- Hit area: the bush plus the pet's height above it, at least 100 px wide.

### Berry Song

```
        [ Hold: exit ]
         Your turn!
            (pet)               the pet "sings" each note (NOD / POSE)
   (red)                (candy)
         (blue)  (green)        4 pads, 92 px, along the lower arc
```

- Four pads: red, blue and green berry and candy, each with its own tone
  (C, E, G, high C: four new short sound effects).
- The pet plays a song by lighting pads (with its tone); then "Your turn!" and
  she repeats it. Each round adds one note to the same song. It starts at 2
  notes.
- A wrong pad: `SFX_DENY`, the pet tilts its head, the same song plays again.
  Three wrong answers end the game. Twelve notes repeated ends it with a
  fanfare.
- No timer while it's her turn. **The pads light up, so it works with the
  sound muted** (BOOT button), and the colours differ in brightness too.
- Playback 500 ms a note, faster to 350 ms by the 8th note. Score = longest
  song repeated. A typical run (5–7 notes) takes about 30–40 s.

### Bubble Pop (optional fifth)

Bath-themed, reusing the bath bubble drawing: the pet sits in foam at the
bottom; bubbles rise and wobble; tap to pop (+1), a rainbow bubble now and then
(+3). 30 s, bubbles every 700 → 400 ms, rising 60 → 120 px/s. It overlaps
Peekaboo (tap a target), which is why it's optional.

## Shared parts

### Picking a game

Recommended: a **shuffle bag**. Play deals the games in a random order, each
once, then shuffles again; the first game of a new round is never the last one
played. With four games she sees all four in any four plays. Pure random with
"never twice in a row" also works but can bounce between two games for a
while. The bag lives in RAM (a reboot starts a fresh shuffle; nothing to save).

### Title card

About 1.2 s before the game: the game's icon, its name (size 3) and a short
hint (size 2), over the game's own background. Touches are ignored then, except
the exit.

### Leaving a game

Recommended: the same **"Hold: exit"** chip in every game, at the top centre
(160 × 40 px, y 20–60, inside the circle), with the same look, string
(`S_HOLD_EXIT`) and 1 s hold as the strength bag's exit on the 1.75C. A hold
must start on the chip; sliding onto it doesn't count; the touch that exits is
swallowed. Leaving pays nothing, as today. Same on both boards. Scores and
lives move down ~40 px to make room.

### Game input

While a game is open, `handleTouch()` gives the game the raw finger (down,
position, up) and skips the gesture code: no swipes, no long-press "Release?"
(fixes problem 2). Taps act on touch-down, which feels quicker. Berry Catch
reads the finger's position while it's down.

### Time and pausing

Each game runs on its own clock: the sum of frame steps, each capped at 250 ms.
If the screen goes off (PWR, pocket lock), the game pauses instead of its
timer running out unseen, as keepy-uppy does today. Bedtime still abandons a
game left open in the dark (`bedApply`).

### Power

Nothing new runs outside a game: no timers, no wakeups, the picker is RAM
only. Inside a game it's the same as today (85 ms frames, 240 MHz). Because a
game keeps the screen on, **every game ends after 15 s with no touch** and pays
for what was done (Berry Song needs this; the others end sooner anyway). One
NVS write per finished game, as today.

### Reward

One path for all games: `Pet::playGame(game, raw)`.

1. Update that game's record with the raw score.
2. Convert to a normalised score: `N = raw × 15 / par`, capped at 30. Par is a
   "good game": keepy-uppy 15 (so N = raw, as today), Berry Catch 20,
   Peekaboo 14, Berry Song 6, Bubble Pop 20.
3. Pay `playResult(N)` exactly as today.

At par the pet gets full JOY (+35), +3 SPEED and a heart. The cap at 30 only
changes keepy-uppy games above 30 points (SPEED gain stops at +6, weight burn
at 60); its record still shows the raw score.

**All games train SPEED.** Battles are out of scope, so the trained stats only
show on the card; Play = SPEED and the bag = STRENGTH stays easy to remember.

### Records and save data (NVS `tamapoke`)

New keys only. `ghi` keeps its meaning (keepy-uppy's best).

| Key | Type | Meaning | Missing / out of range |
|---|---|---|---|
| `ghi` | UShort | keepy-uppy best (existing) | now also clamped: above 255 (scores are 8-bit) reads as 0 |
| `mgc` | UShort | Berry Catch best | 0; above 200 reads as 0 |
| `mgp` | UShort | Peekaboo best | 0; above 20 reads as 0 |
| `mgs` | UShort | Berry Song best | 0; above 12 reads as 0 |
| `mgb` | UShort | Bubble Pop best (if built) | 0; above 200 reads as 0 |

No clash with existing keys (`gspe` is a gene) or the egg-choice draft's keys
(`echo`, `ecpk`, `ec0`–`ec2`).

Where the records show (question 3): recommended, a row of four game icons
with each best under the medal grid on the card's Medals page (y ≈ 325–365, fits
the circle; icons, so no new strings). The result screen shows that game's
record, as today.

### Strings (all 8 languages)

New, English drafts: game names *Keepy-uppy, Berry Catch, Peekaboo, Berry
Song* (*Bubble Pop*), hints *Tap the ball!, Slide to catch!, Tap when I pop
up!, Watch, then repeat!* (*Pop the bubbles!*), and *Listen... / Your turn!*
for Berry Song. Reused: `S_SCORE_FMT`, `S_NEW_RECORD`, `S_RECORD_FMT`,
`S_GREAT_JOY`/`S_PLUS_JOY`, `S_HOLD_EXIT`. Japanese in kana only; Korean only
in syllables that `unifont_t_korean2` has; the existing glyph test in
`test/test_i18n.cpp` extends to the new strings. Titles fit 400 px at size 3,
hints at size 2.

### Pet sprite

Each game uses `pmd.has(act)` and falls back to `IDLE`; with no PMD sprite
loaded it draws the old `mon` sprite or the Pokéball icon, as keepy-uppy does.

## Code layout

- `minigames.h` (pure): game ids, the shuffle bag (with an injected RNG), par
  and normalising, record limits, the hold-to-exit chip, the game clock and
  the 15 s idle end.
- `game_catch.h`, `game_peek.h`, `game_song.h` (and `game_keepy.h`; `game_bubble.h`
  if chosen), all pure: state, `reset(seed)`, `step(dt)`, `touch(...)`,
  `done()`, `score()`. The renderer only reads their state. No `millis()`,
  `random()` or drawing inside, so the host tests are deterministic.
- `TamaPoke.ino`: the renderers next to `renderGame()`, the input hook in
  `handleTouch()`, the title card and result screen, the Medals-page row.
- `pet.cpp/.h`: `playGame()`, the new keys in `save()`/`load()` with range
  checks.
- `audio.cpp/.h`: four note sounds for Berry Song.
- `species.h`: rotten-berry, bush and note icons (16 × 16).

## Tests (host; each shown failing without its code)

- **Picker:** every game once per round, never twice in a row across rounds
  (10 000 seeded draws), works with 4 or 5 games.
- **Reward:** `N` for each game at 0, par and the cap; keepy-uppy gives today's
  reward up to 30; `playGame` updates only that game's record and `ghi` keeps
  keepy-uppy's raw score; quitting pays nothing.
- **Save:** the new keys survive save/load; missing keys load as 0; values
  above each limit load as 0.
- **Exit chip:** a 1 s hold that starts on it exits; sliding onto it, releasing
  early or starting outside doesn't.
- **Clock and idle:** a long gap advances a game by at most 250 ms; 15 s
  without a touch ends any game.
- **Berry Catch:** spawn x stays inside the safe range; the ramp at 0, 15 and
  30 s; catch edges at ±55 px; rotten floors at 0; favourite counts 2 only when
  known; ends at 30 s.
- **Peekaboo:** no bush twice in a row; a hit inside the window scores once;
  after it, a miss; a wrong bush costs nothing; ends after 20.
- **Berry Song:** the song grows by one and keeps its start; pads during
  playback are ignored; a wrong pad costs a try and replays; 3 wrong ends it;
  the score is the longest song; ends at 12.
- **Keepy-uppy:** a pinned run of today's physics gives the same numbers.
- **Strings:** all 8 languages, kana/hangul only, buffer sizes
  (`TestBuffersDeTexto` covers any new `%u`).

Plus simulator screenshots of each game (title, mid-game, result) by day and
night and in Japanese and Korean, once the simulator exists.

## Plan and sequencing

1. After the owner's answers: the pure headers, `playGame`, the save keys,
   strings and host tests. This doesn't need the simulator, so it can start
   right away.
2. The screens, once the desktop simulator has merged, so every screen is
   checked in screenshots before the board.
3. One PR on the fork, the gate, two review rounds, merge on the owner's OK.
   If the diff gets too big to review well, split it: the shared parts with
   keepy-uppy first, then the new games.
4. A board check only if the owner asks: app-only update with
   `tools/update_175c.sh` (backs up the pet first).

**Overlap with the egg-choice work:** both add strings to `i18n.h/.cpp`, keys
to `pet.cpp` and touch `TamaPoke.ino` (theirs the egg screen and Care page,
ours the game and Medals page). Different keys and regions; whichever PR lands
second rebases.

## Decisions (owner, 2026-11-13: all six recommendations)

1. **Which games in the first release?** Recommended: keepy-uppy, Berry Catch,
   Peekaboo, Berry Song. Bubble Pop later if she likes the rotation.
2. **How random?** Recommended: shuffle bag (each game once per round, never
   twice in a row). Alternatives: pure random without repeats; or lean on the
   pet's mood (tired → calm game).
3. **Per-game records, and where?** Recommended: yes, as a row of icons with
   bests on the Medals page. Alternatives: a new "Games" card page; only on the
   result screen.
4. **Quitting?** Recommended: "Hold: exit" chip at the top of every game, like
   the bag. Alternatives: a tap ✕ (quicker, easier to hit by mistake); keep
   today's header tap (fixed so a high ball doesn't quit).
5. **Length and difficulty?** Recommended: 25–40 s, start easy, gentle ramp,
   numbers above. Alternatives: shorter (~20 s); gentler throughout.
6. **Reflex vs calm?** Recommended: the mix above (two reflex, one in between,
   one calm). Alternatives: more calm (add Bubble Pop now, or calm games only
   at night).
