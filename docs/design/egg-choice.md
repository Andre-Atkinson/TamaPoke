# Design: choose your egg

Status: **approved by the owner on 2026-11-13**, with these decisions:

1. Each egg reveals its **type colour and rarity** (no NEW badge, no silhouette).
2. **Same odds as today**: one egg is today's roll, the other two are Common.
3. A **Care-page button, off by default** ("Eggs: Surprise / Choose").
4. **Tap to look, tap again to pick**; no re-roll, no backing out.

Sections below keep the options that were offered; ★ marks the recommendation,
and the decision above is what was built.

## What it is

Today, every pet after the first one hatches from one random egg made in
`Pet::newEgg()` when a pet's story ends (farewell, release or runaway). With
this feature, the player gets **three eggs side by side** and picks one. The first
game keeps the Bulbasaur / Charmander / Squirtle starter screen.

The point is a small, happy choice ("I'd like a water one this time"), not a
way around the luck: which Pokémon hatches stays a surprise.

## How it works today (verified in the code)

- `newEgg()` sets `eggTarget = pickEggSpecies()`, sets `starterPick` only when
  nothing is registered yet, and rolls `eggShiny` once (1/24 after a farewell,
  else 1/48, minus `careBonus()`, never better than 1/8).
- `pickEggSpecies()` rolls a tier: runaway gives Common; otherwise Rare is
  27% (45% after a farewell) + `careBonus()`, Legendary 3% (10% after a
  farewell) + `careBonus()/3`, and only with 25 or more registered. Inside the
  tier it prefers species whose evolution line is still incomplete.
- Egg-tier species: 47 Common (12 type colours), 27 Rare (13 colours),
  5 Legendary.
- The starter screen blocks ticks, offline progression, swipes, bedtime and
  the gallery while the player chooses (`starterPick`, saved as NVS `stpk`).
- **Existing bug:** the egg screen draws "Rare egg!" / "Legendary egg!?" at
  y=316 and then paints the bottom panel over it (`TamaPoke.ino` ~1804-1813,
  since upstream a53c386), so the label never shows. This design fixes it.

## Proposed design (recommendations marked ★)

### 1. What each egg reveals

| Option | Shows | Surprise kept |
|---|---|---|
| ★ Type colour + rarity | Spots on the shell in the type's colour (green = grass, red = fire, ...); shell cream / silver / gold for Common / Rare / Legendary | Species |
| Rarity only | Shell colour only | Species and type |
| Type + rarity + "NEW" | As ★, plus a NEW badge if that species isn't in the Pokédex yet | Species |
| Silhouette | The Pokémon's outline | Almost none |

The three eggs are always three different species, with three different type
colours when the tier allows (it always does for Common and Rare).

### 2. How the three eggs are rolled

| Option | Odds of a Rare-or-better / Legendary on offer |
|---|---|
| ★ Same odds as today: egg 1 is today's roll (blessing, curse, care bonus all as now); eggs 2 and 3 are Common, of other types | Unchanged: 30% / 3% after a release, 55% / 10% after a farewell |
| Three full rolls with today's odds | 66% / 9% after a release, 91% / 27% after a farewell, 98% / 36% with maximum care bonus |

Either way: a runaway makes all three Common (the curse stays); the bias
towards incomplete evolution lines stays; shininess is rolled once, hidden,
with today's odds, and goes with whichever egg is picked.

### 3. On or off

★ A third button on the Care page (with relaxed pace and bedtime): **Eggs:
Choose / Surprise**. On by default, so the next ending after the update
already offers the choice. Alternatives: off by default (**chosen**), or always
on with no button. The Care page only opens while there is a pet, so the setting cannot
change in the middle of a choice.

The three buttons shrink from 96 to 72 px high (y = 92, 172, 252) so the page
dots at y=374 stay clear; the round panel is 424 px wide at y=324 (the
lowest button's bottom edge), the button is 320.

### 4. Choosing

★ Tap an egg to look (it lifts over a glow in its type colour, and its rarity
and "Tap again to choose" appear under the row; a still screen, no animation,
so it costs no extra redraws); tap the same egg again to pick it. The player must
pick one: no re-roll and no backing out, and the choice waits for as long as
needed. Alternatives: one tap picks (like the starter screen), or tap-tap
plus a "Surprise me" button that picks one of the three at random.

A tap that only wakes a dark screen is already swallowed (`swallowGesture`),
so waking the watch never picks an egg.

### 5. Edge cases (no decision needed unless you disagree)

- **Time is paused while choosing**, exactly like the starter screen: no
  ticks, no offline progression on power-up, so the egg never hatches itself
  with a species nobody picked.
- **Power-off mid-choice:** the three candidates are saved, and the same three
  are back after a reboot.
- **Bedtime, sleep, touch lock:** an egg can't sleep, so bedtime does nothing;
  the screen dims, turns off and touch-locks as usual and comes back to the
  same choice. Swipes (gallery, pet card, clock) are blocked like on the
  starter screen.
- **First game, or after WIPE:** the starter screen, never the egg choice.
- **Serial:** `HATCH` does nothing during the choice (there is no egg
  yet); `SPEC` ends the choice; `EGGS` stays read-only. No new commands;
  `CARE?` (read-only) also prints `egg=` and `picking=`.
- **The egg screen after picking** shows the egg as it looked in the choice
  (same shell and spots). With *Surprise* the egg screen looks as before.

## Save data (NVS namespace `tamapoke`)

New keys only; no existing key changes meaning.

| Key | Type | Meaning | Missing / invalid |
|---|---|---|---|
| `eggch` | UChar | 0 = Surprise, 1 = Choose | Missing or any other value: 0 (Surprise) |
| `ecpk` | Bool | A choice is on screen | Missing: false (old saves behave exactly as today) |
| `ec0`, `ec1`, `ec2` | Short | Candidate Pokédex numbers | If any is outside 1..151, not an egg species, or a duplicate: re-roll the other two around the saved roll (`eggTarget`), so a damaged candidate save keeps a Rare or Legendary |

While a choice is open, `eggTarget` keeps the normal roll (one of the three),
so anything that reads the egg (`eggRarity()`, `eggAccent()`) stays valid.
Picking sets `eggTarget`, clears `ecpk` and saves once. If `stpk` and `ecpk`
are both set (corrupt save), the starter screen wins and the egg choice is
cleared; `ecpk` on a pet that has already hatched is cleared too. `load()`
range-checks all of this, as the hard rules require.

## Screen (466 × 466, round)

```
            Choose an egg!                 title, size 2-3, y ≈ 68
     .-.          .-.          .-.
    ( o )        ( o )        ( o )        three eggs, scale 4 (68 × 88 px)
     '-'          '-'          '-'         centres x = 93 / 233 / 373, y ≈ 214
                 ~~~~~                     the selected egg lifts over a glow
             Rare egg!                     rarity of the selected egg, y ≈ 300
         Tap again to choose               hint, y ≈ 336
```

Day or night background as on the egg screen. Rarity is shown by the shell
colour as well as the text, so it reads at a glance in any language.

## Strings (all 8 languages)

New: `S_CHOOSE_EGG` (title), `S_EGG_TAP_AGAIN` (hint), `S_EGG_COMMON` (rarity
of a Common egg), `S_EGGS` (Care button label), `S_EGG_CHOOSE` /
`S_EGG_SURPRISE` (its values). "Rare egg!" and "Legendary egg!?" already exist.
Japanese uses kana only and Korean only syllables in `unifont_t_korean2`;
the Care-page glyph test in `test/test_i18n.cpp` is extended to the new
strings, and Care values must fit the 300 px button at size 4.

## Tests (host, each shown failing without its code)

- The roll: three distinct valid egg species; distinct type colours; runaway
  gives three Common; with ★ odds, egg 1 matches today's tier odds over many
  seeded rolls and eggs 2-3 are Common.
- The flow: a farewell or release with Choose on opens a choice; with
  Surprise it gives one egg as today; the first game shows the starter screen;
  ticks and offline time don't age or hatch the egg while choosing; picking
  sets the egg and ends the choice; `eggTap` does nothing while choosing.
- Persistence: save/load keeps the three candidates and the pending flag;
  missing keys behave as today; out-of-range, non-egg and duplicate
  candidates are re-rolled; an out-of-range `eggch` falls back to Surprise;
  `stpk` + `ecpk` together resolve to the starter screen.

## Out of scope

Choosing the exact species, re-rolls, anything multiplayer or networked.
