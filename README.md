# TamaPoke

[![Flash in browser](https://img.shields.io/badge/flash-in%20browser-FF6B00?logo=googlechrome&logoColor=white)](https://socquique.github.io/TamaPoke/web/)
[![MakerWorld](https://img.shields.io/badge/MakerWorld-3D%20case-00AE42?logo=bambulab&logoColor=white)](https://makerworld.com/es/models/2937822-tamapoke-a-pokemon-pokeball-tamagotchi)
![Board](https://img.shields.io/badge/board-ESP32--S3%20round%20AMOLED-E7352C?logo=espressif&logoColor=white)
![Firmware](https://img.shields.io/badge/firmware-v1.17-8A2BE2)
[![CI](https://github.com/Andre-Atkinson/TamaPoke/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/Andre-Atkinson/TamaPoke/actions/workflows/ci.yml)
![Code](https://img.shields.io/badge/code-MIT-blue)
![Languages](https://img.shields.io/badge/languages-EN%20%7C%20PT-FFCB05)
[![Stars](https://img.shields.io/github/stars/socquique/TamaPoke?style=flat&logo=github&color=yellow)](https://github.com/socquique/TamaPoke/stargazers)

A gen-1-Pokémon-inspired tamagotchi for the
**Waveshare ESP32-S3-Touch-AMOLED-1.75** (round 466×466 AMOLED, CO5300 driver
over QSPI, CST9217 touch over I2C). Raise any of the 151, evolve it, train it
and complete them all (shinies included).

> **Personal, non-commercial fan project.** Code is MIT; the sprites are from
> PMD SpriteCollab (CC BY-NC, Pokémon © Nintendo/Game Freak), and the 3D case is
> CC BY-NC-SA. See **[License](#license)** and **Credits**.

🔴 **3D-printed Pokéball case + print profiles → [on MakerWorld](https://makerworld.com/es/models/2937822-tamapoke-a-pokemon-pokeball-tamagotchi)** · flash it in your browser → **[web installer](https://socquique.github.io/TamaPoke/web/)**

## Status

Running on hardware. Implemented: the 151 + shinies animated from microSD, full
life cycle (egg by rarity → evolution → farewell/release/runaway, each gated
behind a decision dialog), bred-Pokédex with gallery, fitness training (strength, stamina,
speed) with ribbons, retention hooks (streak / bond / medals / name), biome + real-time
backgrounds, four minigames, two calm exercises, animated bath, RTC with offline
progression, battery (AXP2101) and PWR button, anti-burn-in dimming,
**sound (ES8311)**, **English and European Portuguese UI (English default)**, **starter choice on
first run**, and a one-click **web installer**.

Pending: wild encounters / battle (designed, not implemented), 3D case, soak
test. See **Roadmap**.

## Game manual (the actual numbers)

A quick reference to how the game really works (values straight from the code).

### Time & leveling
- **1 real minute = 1 in-game minute.** Your Pokémon gains **+1 level every hour**
  of real time. Leveling is purely time-based — caring well doesn't speed it up,
  but neglect *delays evolution*.
- It keeps **aging while powered off** (the RTC runs), catching up to **2 weeks** max.
- **Relaxed pace** (stat card, *Care* page; off by default): while awake, the needs
  below drain at **half speed**, so feeding is needed about hourly instead of every
  30 minutes. Only the draining slows down. Leveling (+1 per hour), evolution
  timing, slip-up detection, the runaway countdown, weight burn and the sleeping
  rules are unchanged, so neglect still counts, it just takes twice as long to
  get there. Catch-up while powered off is the same in both paces.

### The four stats (0–100)
Needs: **FOOD**, **JOY**, **ENE** (energy), **HYG** (hygiene). Start 80 / 80 / 80 / 100.
While **awake**, per minute:

| Stat | Drain/min | Notes |
|---|---|---|
| FOOD | −2 | |
| ENE | −1 | −1 extra if overweight (weight > 50 → sluggish) |
| HYG | −1 | **−4 more per poop** on screen (max 3 poops) |
| JOY | −1 | **−2 extra** if FOOD < 30, **−2 extra** if HYG < 30 |

- ~**15 %/min** chance to poop (only if FOOD > 40). Poops tank hygiene fast.
- **Care slip-up** = letting any stat hit **≤ 10** (60-min cooldown so it counts once).
  Each slip-up **delays evolution by 1 level** and cools the bond.

### Actions
- 🍎 **Berry** (3 flavors): +25 FOOD. Each species has a **hidden favorite flavor**
  → +35 FOOD, +10 JOY, ♥, bond, and it gets revealed.
- 🍬 **Candy:** +10 FOOD, +12 JOY, but **+12 weight** (fattening).
- ⚽ **Play / minigames:** +JOY, −ENE; a random one of four short games (keepy-uppy, Berry Catch, Peekaboo, Berry Song). Each trains **SPEED** and burns weight, and keeps its own best score.
  Turning the screen off (PWR, or the 1.75C's pocket lock) ends the game at once and the
  creature keeps what its score so far earned, with no result screen; a game you haven't
  touched yet just closes, for nothing and at no cost. The screen comes back on to the creature.
- 🧸 **Plush toy** (the Play button's strip): it hops about with its plush, +10 JOY and a heart
  (+15 for a playful creature), no minigame, so no SPEED and no cost. Two plays an hour give joy;
  after that (or with JOY already full) it still plays, for no joy, so the games stay the main fun.
- 🥛 **Warm milk** (the moon button's strip): it drinks, +5 JOY and a heart, **+4 weight**
  (less than candy), then lights out. Until it next wakes, its sleep brings energy back
  **+2/min** faster (8 a minute, 10 for a lazy one). Now and then in the evening, before
  quiet hours, it asks for it ("Sleepy... warm milk?"), twice an evening at most and not
  once it has had its milk.
- 🥊 **Training** (the *Train* button on the stat card): two calm exercises, each up to
  12 punches or reps worth 1.5 points (+18 a session at most; its nature can add a quarter),
  and each tires it like a game:
  - **Punch** trains **STRENGTH**: tap as the swinging bag reaches the creature (it lights
    up), and the creature strikes it with its own attack. Off-beat taps do nothing and cost
    nothing.
  - **Hold** trains **STAMINA**: keep a finger on the screen while it does reps; lifting
    only pauses it.
  Leaving early keeps what was earned, and so does turning the screen off (PWR, or the
  1.75C's pocket lock): the session ends at once and the screen comes back on to the
  creature. While it sleeps, *Train* just says so and the card stays open.
- 🫧 **Bath:** clears poops, HYG → 100.
- 👆 **Pet it:** tap it for +5 JOY + bond. It also knows a few touches, and each
  one counts as a single pat (the daily bond limit is shared with everything else):
  - **double tap:** it hops;
  - **stroke** back and forth over it: it enjoys it (one pat a second or so while
    you keep stroking);
  - **tickle** (six quick taps in a row): it giggles; keep going and it turns away
    with a little huff, which costs nothing but gives no pat;
  - **hold** your finger on it for a moment, then **slide** sideways: it walks
    after your finger.

  Gestures work on the main screen with the creature awake, not on an egg or while
  a menu or dialog is open. A swipe, a tap or the long-press (release) starting on
  the creature works as before, with one catch: a sideways slide that starts with a
  short pause (a quarter of a second) on the creature makes it walk after your
  finger instead of opening the Pokédex. To open the Pokédex, swipe without
  pausing, or start the swipe off the creature.
- 🌙 **Sleep** (lights out on the moon button's strip): rest — ENE **+6/min**, needs drain ~**4× slower** with floors
  (FOOD 30 / JOY 35 / HYG 45). No poops, no slip-ups, can't run away while asleep.
- 🛏️ **Bedtime** (stat card, *Care* page; off by default): pick 21:00–07:00,
  22:00–07:00 or 23:00–08:00 and the creature falls asleep by itself when the
  window starts (silently) and wakes when it ends (one soft chime if sound is on).
  Only those two moments act, so if you wake it at night it stays awake until the
  next bedtime, and if you put it to bed early it still wakes in the morning.
  It waits until a minigame, dialog, bath or evolution is finished (a minigame or an
  exercise is never left open with the screen off: turning the screen off ends it,
  keeping what was earned). Eggs, farewell
  scenes, the starter pick and the egg choice are skipped. It follows the clock you set (swipe
  down): after a full power-off the 1.75C clock can be wrong until you set it, so
  after a power-off bedtime stays paused until the clock is set (clock screen, which
  opens by itself to ask, or `board.py clock`); it then acts as if the device had just started, so at 22:30 the
  creature goes to sleep and at 14:00 nothing happens.
  Changing the setting never puts the pet to sleep or wakes it by itself.
- 🔕 **Quiet hours for its own sounds:** the sounds the creature makes by itself, with
  nobody touching it, keep out of quiet hours: the "ready to evolve" chime, the "about
  to run away" warning, the hourly level-up and the medal that time earns (level 10,
  25 or 50, **IN SHAPE**, and the stamina ribbon, which steady care earns by the
  minute). Quiet hours are the attention calls' (the bedtime window,
  or 22:00–07:00 if bedtime is off, and any time the clock hasn't been set since a
  power-off). A sound that comes due then is skipped, not saved for the morning; the
  evolve or run-away button, the new level and the medal banner show as usual. Sounds
  you start (a tap, a minigame, training, care that earns a medal, an **IN SHAPE** or a
  ribbon you earn by exercise) play as before, and asleep it makes no sound at all.
- 🔌 **Self-care while charging** (always on): plugged into USB and untouched for
  **10 minutes**, the pet looks after itself a little. Once a minute, the first
  need under **40**, in the order FOOD, HYG (which also clears the poops), ENE,
  JOY, goes back up to **50**. It only goes up to half, so your own care still
  matters, and it isn't your care: it gives no streak or bond, awards no medal
  itself, and makes no sound or animation. Any touch starts the 10 minutes again.
  It's off while the pet is asleep, as an egg and during an ending, never applies
  to the time it was powered off, and stays off if the power chip doesn't answer
  (the firmware then can't tell USB from battery). A need it lifts in the same
  minute doesn't count as a slip-up, and the runaway countdown can't build. With
  no slip-ups, a pet left on the charger can still earn the **IN SHAPE** medal at
  level 5 (no slip-ups, weight 0), and slip-ups don't delay its evolution. The "ready to evolve"
  chime sounds at most once per level for each form, because the needs keep coming back over 40.
- 💬 **Speech bubbles:** now and then the creature says something in a bubble above
  it, for about 4.5 s. A need comes first (only a special day's greeting, below, comes
  before it), the same one (below 25) as the "!" bubble and the status line: "I'm
  hungry!", "I need a bath!", "I'm tired...", "I'm bored...". In the evening, now and then,
  "Sleepy... warm milk?" (see warm milk above). Otherwise
  "Good morning!" (6:00–9:59) or "Look at the stars!" (20:00–5:59) when the clock is set, a
  line of its nature (one bubble in five, see Nature below), a friendly line ("Hi there!",
  "I'm happy!", "Pat me!") or "I love you, <name>!". The name
  is the player's, set once from a computer with the serial command `OWNER <name>`
  (`python3 tools/board.py cmd OWNER Name`; `OWNER` alone clears it, `OWNER?` shows
  it); with no name it says "I love you!". Names can use plain letters, digits,
  spaces and punctuation, up to 12 characters with at least one letter, plus the
  accented letters the font has
  (é è ê ë á à â ä å í ì î ï ó ò ô ö ú ù û ü ñ Ñ ç Ç ÿ Ä Å É Ö Ü, and the Portuguese
  ã õ Á À Â Ã Ê Í Ó Ô Õ Ú, so "João" works). A bubble is due 1–2 minutes after the last one and
  waits for the creature's screen to be awake and free, then shows 3 s later, so waking
  the screen after a while gets a line within seconds. It never shows while the screen is
  dimmed or off, while the creature sleeps, on an egg, during an ending, a dialog, a
  minigame or another screen, while it eats, bathes or shows a heart or a banner, or while
  an attention call lights the screen; it takes the "!" bubble's place while it shows. It
  never wakes or lights the screen, and the bubble is not a touch target: taps work as before. Idea
  from Mu-Muu/TamaPoke.
- 🎄 **Special days:** the scene is decorated and the creature greets the player by name in a
  speech bubble ("Merry Christmas, <name>!"; just "Merry Christmas!" with no name set).
  Christmas puts up a tree with lights all December, with a little snow even in the southern
  summer, and the greeting on 25 Dec; New Year (1 Jan) has soft fireworks; Valentine's Day
  (14 Feb) floating hearts; Easter Sunday (worked out each year) eggs in the grass;
  Halloween (31 Oct) a friendly pumpkin, and on the evening a purple sky, a full moon and a few
  bats; the player's birthday balloons, beside that day's own decorations. The day runs
  from midnight to midnight on the clock. The greeting takes the place of the next speech
  bubble, so it comes a few seconds after the screen wakes: at most 3 a day, at least 30
  minutes apart, never in quiet hours (bedtime, or 22:00–07:00 with bedtime off). The
  birthday's greeting wins over a holiday's. Nothing shows while the clock is not set
  (after a power-off) or during an ending, and nothing changes in care or rewards. The
  birthday is set once from a computer with the serial command `BDAY <month> <day>`
  (`python3 tools/board.py cmd BDAY 3 14`; `BDAY 0 0` clears it, `BDAY?` shows it); a
  29 February birthday shows on 28 February in other years. Idea from Mu-Muu/TamaPoke.
- ❗ **Attention calls:** when FOOD, JOY, ENE or HYG first drops **below 25** (where
  its bar turns red), the creature calls once: one soft chirp (if sound is on) and a
  "!" speech bubble beside it with the icon of the button that helps (berry, ball,
  moon, bubbles). The same need calls again only after getting back to **40** or
  more, so a quick cuddle can't make it call twice. The bubble stays, silently,
  while a need is under 25 (the one the status line names), and hides while it sleeps.
  - A call is **not a touch**: it lights or brightens the screen for **15 s**, then the
    screen goes back to what its real idle time says (dim, or off), and self-care while
    charging, which waits for 10 untouched minutes, keeps counting from your last real
    touch. Once self-care is running it keeps the needs at 35 or more, so a pet left on
    the charger doesn't call.
  - A screen that turned off by itself lights for 15 s. Touch stays locked if the auto
    touch lock is on; PWR or BOOT turns the screen on for real, and with the touch lock
    off (or not engaged yet) a touch does, and that first touch is swallowed. A stat
    card, gallery, clock or keyboard left open goes back to the creature first. A
    dimmed screen brightens.
  - A lit screen that has sat untouched for 5 minutes (screen-off set to never, the
    original 1.75 default) counts as left behind, like a timed-out one: a call closes a
    gallery, card, clock or keyboard left open, shows the creature, brightens and chirps.
    A screen that is on and not yet that idle waits instead, until you are back on the
    creature screen.
  - A screen you turned off with PWR stays dark (the chirp still plays), and the
    triple-press pocket lock gets nothing.
  - **Quiet hours** (the bedtime window, or 22:00–07:00 if bedtime is off, and any
    time the clock hasn't been set since a power-off): no chirp and no light.
  - No calls while it sleeps, as an egg, while choosing a starter or an egg, or during
    a farewell or a run-away.
    A call waits while a minigame, an exercise, a dialog, the card, gallery,
    clock or keyboard is open on a lit screen, and comes once you are back.
- 🐾 **A life of its own:** left alone on a lit screen, the creature keeps itself busy.
  Every few seconds it picks something to do: stand, look around, stroll, rest a
  while, doze, hop, chase and kick a ball about, strike a pose (the few species that
  have one), or, when FOOD is under 40 (50 for a greedy one), walk over to the food
  button and wish for food. Its nature tips the odds a little (see Nature below). How it
  feels sets the odds: tired (ENE under 50) it dozes and rests more,
  happy (JOY over 70) it hops and plays more, chubby (weight over 60) it rests more,
  and at night (20:00–06:00) while it's still up it dozes more (an unset clock
  counts as daytime). A small thought bubble
  over its head shows a ♥, a ♪, a ?, a "…", a sweat drop, z's or the food icon.
  - A **doze** is a short daytime nap (8–15 s): the sleep pose, but the day scene,
    the buttons and the status line stay as they are, and any touch wakes it at once.
    The light button still means bed.
  - A finished doze gives **+3 ENE** and a finished ball game **+3 JOY**, at most
    **+6 of each an hour**, so they never replace your care. Nothing ever lowers a
    stat, and it isn't your care: no streak, bond or medal. The hourly limit starts
    over if the board restarts.
  - Any touch, PWR or BOOT stops what it was doing (opening another screen takes a
    touch too): it looks up for a moment, then carries on. A call only comes once a
    need is under 25, and the creature is sad then and shows that instead.
  - Only on the main screen with the creature awake and happy and nothing over it.
    Once the screen dims it just strolls, gestures or stands, as before, with no
    bubbles, ball or perks; with the screen off nothing runs at all.

### Eggs & who you get (spawn odds)
- **First ever pet:** you pick a starter — **Bulbasaur / Charmander / Squirtle**.
- Hatch the egg: tap it **3×** (or wait — it hatches on its own).
- Every later egg rolls a **rarity tier** (over the ~79 base forms that come from eggs):

| Tier | Base chance | After a proper goodbye | # species |
|---|---|---|---|
| ✨ Legendary | ~3 %\* | ~10 % | 5 |
| 🔵 Rare | ~27 % | ~45 % | 27 |
| ⚪ Common | the rest | the rest | 47 |

  \* Legendaries only start appearing once you've **registered ≥ 25** Pokémon.
- A daily **streak** and high **bond** push rare/legendary odds higher.
- A clean **goodbye blesses** the next egg; a **run-away curses** it (forces Common).
- Within a tier it favors species whose **evolution line you haven't finished** (so
  all 151 are completable).
- **Shiny:** base **1 / 48** (→ **1 / 24** right after a goodbye), improved by
  streak/bond down to a best of **1 / 8**. Tracked separately in the dex.
- Every hatch rolls unique **genes** (90–110 % per stat) — no two are identical.

### Evolution
- Triggers when **level ≥ its evolution level** (16 for most base forms; ~30 for
  stone-style, ~40 for trade-style) **and every stat ≥ 40** at that moment.
- **Never automatic** — a button appears and **you tap to witness it** (with a
  flicker between the old and new form). Each **slip-up delays it by 1 level**.
- You can **decline** ("keep form"); it re-offers at the next level.
- *Eevee* branches toward whichever evolution you're still missing.

### The three endings (you choose & witness each — none auto-fire)
- 💛 **Farewell** — when it's a **final form** that has lived **3 days**. A button
  appears; triggering it **blesses your next egg**. You can **postpone** ("stay
  together", re-offered in a day). The good ending.
- 💔 **Run-away** — if you let **all four stats sit at 0 for a full hour**. A single
  act of care cancels it. It **curses the next egg** (forces Common). The sad ending.
- 👋 **Release** — long-press the creature to let it go on your terms (neutral).

After any ending, a **new egg** appears.

- 🥚 **Choose your egg** (stat card, *Care* page → *Eggs*: *Surprise* by default,
  or *Choose*). With *Choose*, every ending after the first game shows **three
  eggs** instead of one. Each shell shows the egg's rarity (cream Common, silver
  Rare, gold Legendary) and its spots the Pokémon's **type colour**; the species
  stays a surprise. Tap an egg to look at it, tap it again to pick it. One of the
  three is the normal roll above (same odds, same blessing/curse, same shiny
  chance, which goes with whichever egg you pick); the other two are Common eggs
  of other types, and no two of the three have spots of colours that look alike
  (ground and rock, say). Time is paused until you pick (as on the starter
  screen), and the choice survives a power-off. The first game still picks a
  starter.

### Nature
Each Pokémon hatches with one of five natures, shown on the stat card (swipe up,
first page, under the berry and age):

| Nature | Drains a little faster | Its favourite care does more | Trains faster |
|---|---|---|---|
| Easygoing | — | — | — |
| Greedy | FOOD | berries **+5 FOOD** | STRENGTH |
| Playful | JOY | play and the minigames **+5 JOY** | SPEED |
| Tidy | HYG | a bath also gives **+10 JOY** | STAMINA (steady care: a point every 24 good minutes instead of 30) |
| Lazy | ENE | asleep, ENE **+8/min** instead of +6 | — |

- The faster drain is **one extra point every 4 minutes awake** (every 8 at the
  relaxed pace), none while asleep. Powered off it stops at the same floor of 15, so
  a long power-off ends where it would for any other nature.
- The training bonus is **+25%, rounded up**, so even a 1-point gain gets its extra
  point (bag: 18 → 23; a minigame's 1 → 2).
- A Pokémon from before natures existed is Easygoing.
- **It shows in play.** Right after it hatches, its first speech bubble names its nature
  ("I'm playful!", "Sou brincalhão!", the same word as the card). After that, about one
  bubble in five is a line of its nature, and none of them asks for anything: only a low
  need's bubble does. (If the egg hatched while the board was off, it skips that first one,
  and the line still comes up now and then.)

  | Nature | It says | Left alone on the main screen |
  |---|---|---|
  | Easygoing | "I'm easygoing!", "Life is good!", "No rush, no worries!" | — |
  | Greedy | "I'm a bit greedy!", "Berries are the best!", "Dreaming of berries..." | goes to the food button from FOOD under 50 (others: 40) |
  | Playful | "I'm playful!", "Catch me if you can!", "I love games!" | plays ball and hops more |
  | Tidy | "I'm tidy!", "Neat and clean!", "A place for everything!" | with poops on the ground, strolls on the right (from x 260), clear of them, and looks about more |
  | Lazy | "I'm a little lazy!", "So comfy here...", "Nice and slow..." | stands and rests more, walks and hops less |

  Looks only: these lines and habits change no drain, perk or hourly limit. A playful one's
  extra ball games can only bring the usual +3 JOY a little more often, never past +6 an hour.
  Once the screen dims every nature just strolls, gestures or stands, as before.

### Bonds, streaks, medals, Pokédex
- **Streak** (player-wide, survives across pets): first care each real day; milestones
  at **3 / 7 / 30 / 100** days; skipping a day breaks it.
- **Bond** (per pet, resets on hatch): grows with affection (**cap +8/day**), cools on
  neglect. Both streak & bond improve egg/shiny odds.
- **8 medals** (Lv10/25/50, favorite berry found, 7-day streak, max bond, final form,
  "fit" = weight 0 & no slip-ups) and **3 training ribbons** (strength 30, stamina 10,
  speed 30), per-pet + a global counter.
- **Pokédex:** raising a species registers it; **151 + shinies** to complete.

### Fitness
Training points 0–100 for **strength** (the rhythm punch), **stamina** (steady care:
every need at 40+, and the stamina hold) and **speed** (the Play minigames), shown on the
card's Fitness page with a ribbon at each goal. They show in the pet's idle life (looks
only). Its nature trains one of them a little faster (see **Nature**).

## Hardware

- Board: [ESP32-S3-Touch-AMOLED-1.75](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75)
  — get the **Standard** (no case) or **-G** (GPS, also fits) version; **not the "-B"**
  (ships with a protective case that won't fit). The separate "1.75**C**" is a different board.
  - **Where to buy**: [Waveshare store](https://www.waveshare.com/esp32-s3-touch-amoled-1.75.htm) (often on backorder), or the **-G** on Amazon — TamaPoke leaves the GPS unused, the board is otherwise identical: 🇺🇸 [.com](https://www.amazon.com/dp/B0F7XWWMJW?tag=capsuleradar-20) · 🇪🇸 [.es](https://www.amazon.es/dp/B0F7XWWMJW?tag=capsuleradar-21) · 🇩🇪 [.de](https://www.amazon.de/dp/B0F7XWWMJW?tag=capsulerada03-21) · 🇬🇧 [.co.uk](https://www.amazon.co.uk/dp/B0F7XWWMJW?tag=capsulerada0d-21) · 🇮🇹 [.it](https://www.amazon.it/dp/B0F7XWWMJW?tag=capsulerada08-21) · 🇫🇷 [.fr](https://www.amazon.fr/dp/B0F7XWWMJW?tag=capsulerada0e-21) · 🇯🇵 [.co.jp](https://www.amazon.co.jp/dp/B0F7XWWMJW?tag=capsuleradar-22) · 🇨🇦 [.ca](https://www.amazon.ca/dp/B0F7XWWMJW?tag=capsulerada09-20). Optional battery — 1100 mAh protected LiPo, MX1.25 plug: 🇪🇸 [.es](https://www.amazon.es/dp/B0F1FGZQS5?tag=capsuleradar-21) · 🇺🇸 [.com](https://www.amazon.com/dp/B0F1FGZQS5?tag=capsuleradar-20) · 🇮🇹 [.it](https://www.amazon.it/dp/B0F1FGZQS5?tag=capsulerada08-21) · 🇫🇷 [.fr](https://www.amazon.fr/dp/B0F1FGZQS5?tag=capsulerada0e-21) (DE/UK: that exact pack is not listed — search for a *protected* 3.7 V LiPo, ~1100 mAh, size **102540**, 1.25 mm micro-JST plug) — check plug polarity against the board first. <sub>Amazon links are affiliate links; as an Amazon Associate I earn from qualifying purchases.</sub>
  - **MicroSD card** (holds the sprite set — any small, class-10 card works): 🇪🇸 [.es](https://www.amazon.es/s?k=microsd+32gb&tag=capsuleradar-21) · 🇺🇸 [.com](https://www.amazon.com/s?k=microsd+32gb&tag=capsuleradar-20) · 🇩🇪 [.de](https://www.amazon.de/s?k=microsd+32gb&tag=capsulerada03-21) · 🇬🇧 [.co.uk](https://www.amazon.co.uk/s?k=microsd+32gb&tag=capsulerada0d-21) · 🇮🇹 [.it](https://www.amazon.it/s?k=microsd+32gb&tag=capsulerada08-21) · 🇫🇷 [.fr](https://www.amazon.fr/s?k=microsd+32gb&tag=capsulerada0e-21) · 🇯🇵 [.co.jp](https://www.amazon.co.jp/s?k=microsd+32gb&tag=capsuleradar-22) · 🇨🇦 [.ca](https://www.amazon.ca/s?k=microsd+32gb&tag=capsulerada09-20)
- Round 466×466 AMOLED, **CO5300** driver (QSPI, 80 MHz)
- Capacitive touch **CST9217** (I2C, address 0x5A)
- **AXP2101** (power management + battery + PWR button), **PCF85063** (RTC),
  microSD slot, **ES8311** audio codec (→ amplifier → external speaker on the
  MX1.25 connector)
- Pins taken from the [official Waveshare repo](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75) (see `pin_config.h`)

## Libraries (Arduino IDE / arduino-cli)

| Library | Author | Use |
|---|---|---|
| GFX Library for Arduino (`Arduino_GFX`) | moononournation | CO5300 over QSPI + framebuffer in PSRAM |
| SensorLib | Lewis He | CST9217 touch + PCF85063 RTC |
| XPowersLib | Lewis He | AXP2101 PMU (battery, brightness, PWR button) |
| ESP_I2S (bundled in the ESP32 core) | Espressif | I2S to the ES8311 codec |

## IDE setup / build

- Board: **ESP32S3 Dev Module** · Flash **16MB** · PSRAM **OPI PSRAM**
  (required: the 466×466×16-bit framebuffer ≈ 434 KB lives in PSRAM) ·
  Partition Scheme with FAT (e.g. `16M Flash (3MB APP/9MB FATFS)`) ·
  USB CDC On Boot **Enabled**

```bash
FQBN="esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB"
arduino-cli compile --fqbn "$FQBN" .
arduino-cli upload -p /dev/cu.usbmodemXXXX --fqbn "$FQBN" .
```

### Alternative board: Waveshare ESP32-S3-Touch-AMOLED-1.75C (experimental)

The firmware also targets the **ESP32-S3-Touch-AMOLED-1.75C** (same 466×466
CO5300 panel and CST9217 touch, aluminium case, 32 MB flash, **no microSD** and
no PCF85063 RTC). Select it with a build flag and its own partition table;
the script does both:

```bash
tools/build_175c.sh --out ./out          # -DBOARD_AMOLED_175C=1 + boards/175c/partitions.csv
tools/build_spritefs.sh ./out/storage.littlefs.bin
tools/flash_175c.sh --port /dev/ttyACM0 --out ./out
```

Sprites live in a LittleFS partition in flash (151 normal forms; shinies fall
back to the normal sprite unless you add theirs, one species at a time, with
`tools/send_sd.py`), the clock is a software clock set from the clock
screen, and the 1.75C adds a triple-PWR pocket screen lock, BOOT-button mute
and more reliable strength-training taps. The default build is still the
original 1.75. Details, pins and flashing in [`PORTING.md`](PORTING.md).

### Easiest install: the web installer

`web/index.html` flashes the firmware (ESP Web Tools) and pushes the sprites to
the SD over Web Serial, no Arduino needed. Serve it over HTTPS or `localhost`
(secure context) and open it in **Chrome/Edge**. See [`web/README.md`](web/README.md).

### Generate and load the sprites yourself

All sprites come from **[PMD SpriteCollab](https://github.com/PMDCollab/SpriteCollab)**
(CC BY-NC). You can regenerate the whole set and load it onto your board with the
pipeline below — the firmware accepts files over USB (PUT protocol with per-block
ACK), so you don't have to remove the card (it formats the SD to FAT if needed).

```bash
python3 tools/pack_pmd.py       # fetch + pack PMD sprites: the 151 + shiny -> tools/sdcard/mons/p[s]NNN.bin
python3 tools/make_thumbs.py    # Pokédex thumbnails (from the PMD sprites) -> thumbs.bin
python3 tools/send_sd.py tools/sdcard/mons/*.bin   # original 1.75: send them all to the SD over USB
```

`send_sd.py` sends only the files you name, after checking that they fit. The
1.75C gets its normal sprites from its sprite image instead
(`tools/build_spritefs.sh`, above), and its shinies one species at a time (below).

To make the **one-click web-installer bundle** instead of sending over USB:

```bash
python3 tools/pack_bundle.py    # bundle tools/sdcard/mons/* into web/sprites.pak
```

Then load it from the web installer's **"Load sprites"** button (or `send_sd.py`
above). `pack_pmd.py` also takes individual dex numbers, e.g. `pack_pmd.py 7 25`.
(~40 MB total, all PMD. Versioned under `tools/sdcard/`.)

### Add a shiny sprite for one species (1.75C)

The 1.75C's sprite storage holds the 151 normal sprites and `thumbs.bin`, with
about 8,700 KB to spare. That is room for roughly 60 of the 151 shiny sprites:
each one is 53 to 484 KB, about 134 KB on average. A shiny creature whose shiny
file isn't on the board shows its normal sprite. To add one, plug the board in
over USB, make sure no other program has its port open, and run:

```bash
python3 tools/send_sd.py --check ps151.bin   # read-only: would Mew's shiny fit?
python3 tools/send_sd.py ps151.bin           # send it
python3 tools/send_sd.py --ls                # read-only: what the board holds, and the room left
```

- `psNNN.bin` is the shiny sprite of dex number NNN (`ps025.bin` is Pikachu's), and a plain
  name is taken from `tools/sdcard/mons/`. Name several files to send several. A bare run
  sends nothing and shows how to use it.
- Before sending anything, the script checks that every file fits in the free space
  (`SDINFO`). A file the board already has is replaced, so its space counts as free. If they
  don't all fit, nothing is sent.
- It prints one line per file, stops at the first one that fails, and exits non-zero.
- The board shows a new sprite as soon as it arrives. The screen pauses while a file goes
  over (the board's main loop is busy receiving it).
- **If a send fails part-way** (or you press Ctrl-C), the board may hold only part of that
  file, or none of it, because `PUT` deletes the old copy first, and it can't use a partial
  file. The creature keeps the sprite the board already has loaded until the board loads it
  again (after a restart, when the creature changes, or once another file arrives whole).
  Then a shiny form shows a "?" and "No sprites" instead of its sprite (the nine species of
  the three starter lines show a small built-in sprite), or, if the board refused the send
  and no file is left, the normal sprite. The script waits for the board to give up on the file, then prints the command
  that sends it again.
- It refuses a file the board would not load: one that doesn't start with the right four
  bytes for its name, or is too small or too big (`psNNN.bin` and `pNNN.bin` 7 bytes to
  3 MiB, `thumbs.bin` 10 bytes to 2 MiB).
- There is no command that deletes a file from the board. A shiny you sent stays until the
  sprite image is flashed again (`tools/build_spritefs.sh`, then
  `tools/flash_175c.sh --fs-only --out tools/build`, which flashes the image just built),
  which puts back the 151 normal sprites and `thumbs.bin`, and nothing else.

## How to play

On first run you **choose a starter** (Bulbasaur / Charmander / Squirtle). After
that you start with an **egg**. Tap it 3 times or wait and it hatches. From then
on, care for your companion:

**Four stats** that decay: **FOOD**, **JOY**, **ENE** (energy), **HYG** (hygiene).
If one bottoms out it counts as a *slip-up*.

**Buttons (bottom arc, icons):**
- 🍎 **Feed** → food menu: 3 berries (each species has a hidden favourite that
  gives a bonus) and a candy (+happiness but it fattens; weight makes it sluggish).
- ⚽ **Play** → a strip with the **Poké Ball** (a random minigame, trains SPEED; hold the
  button at the top for 1 s to leave) and the **plush toy** (a little happiness, no game).
- 🌙 **Light** → awake: a strip with **lights out** and **warm milk, then lights out**
  (sleep recovers energy and dims the screen; while asleep, needs decay much slower).
  Asleep: wakes it at once.
- 🫧 **Bath** → a foam scene that cleans up the poops.

The food menu and the two strips close when you choose, when you tap anywhere else, or by
themselves after 6 s.

**Touch gestures:**
- Tap the creature = pet it (+happiness, bond).
- Horizontal swipe = open the **Pokédex / gallery**.
- Vertical swipe up = open the **stat card** (5 pages: Profile / Fitness / Medals /
  Progress / Care; swipe between them; tap the name on Profile to rename; on Fitness the
  "Train" button offers the punch and the hold; on Care two big buttons cycle the pace
  Normal ↔ Relaxed and the bedtime Off → 21–07 → 22–07 → 23–08).
- Swipe down = **set the clock** and the **settings**. The first row is sound, brightness and
  language. Under it, each pill shows an icon and a number: the touch lock (a padlock and the
  seconds after the screen goes off, `0s` = as it goes off; an open padlock and "off" = never),
  the screen-off time (a screen and the seconds without a touch, or "off" = never) and the
  battery in the header (its icon or `87%`, with the live level). After a full
  power-off the clock screen opens by itself once to ask for the time; skip it (swipe up)
  and a small clock badge in the sky opens it again until the time is set.
- Long press (3 s) on the creature = **release** dialog.

**Physical PWR button:** short = screen on/off · long (4 s) = full power-off
(the RTC stays alive, so time passes even while it's off).

## Decisions: you choose, and you watch

The three life-cycle endings and evolution **don't happen on their own** — when
the conditions are met a button appears and you tap it (so you're present to
witness it), each opening a two-option dialog:

- **Evolution** (red button): *Evolve* (epic animation: halo, rays, sparkles and
  a **flicker between the old and new form**) or *Keep form* (re-offered next level).
- **Farewell** (gold button, final form + 3 days): *Say goodbye* (warm farewell,
  rising hearts → new egg) or *Stay together* (keep your companion; re-offered in
  a day). Tension: a maxed-out friend vs. completing the Pokédex.
- **Runaway** (dark button, total neglect for 1 h): a somber "feels abandoned"
  ending in the rain — caring for the creature cancels it.

## Sprites: PMD SpriteCollab everywhere

- **PMD SpriteCollab** (everything — main screen, stat card, minigame **and the
  Pokédex grid + detail view**): behaviour sprites — `tools/pack_pmd.py` packs
  actions (Idle, Walk L/R, Sleep, Eat, Hurt, Attack, Pose, Nod, DeepBreath) into
  the multi-action **TPK2** format (`/mons/pNNN.bin`). The engine in `scene.cpp`
  makes the creature wander, gesture, doze, hop, chase a ball, curl up to sleep, chew
  and wince (idle activities: `idleplan.h`, `idlelife.cpp`). Anchored by
  the feet (lowest content row), not the canvas. The Pokédex thumbnails
  (`thumbs.bin`, TPTH) are derived from these by `tools/make_thumbs.py`.
- **In-house workshop** (`tools/sprites.py`): 9 primitive-drawn sprites as a
  no-SD fallback + the UI icons. Generates `species.h`. Preview in
  `tools/sheet.png`, emit with `python3 tools/sprites.py emit`.

`sdmon.h/.cpp` loads the PMD sprites into PSRAM (`PmdMon` for TPK2) plus the
thumbnails (`SdThumbs`). `SdMon` (TPK1) remains as a dormant legacy fallback only.

## Pokédex and species data

`tools/dex_data.py` is the **single source**: name, slug, type (accent colour +
background biome), evolution line with gen-1 levels, rarities and starters.
`tools/dex_stats.py` has the real base stats (from PokéAPI). Both UI languages use
the English species names, as the Portuguese games do. `gen_dex.py` emits `dex.h`
(the `DEX_TBL[152]` table, the `dexName()` accessor, and the egg-spot look-alike
table with `eggSpotsLookAlike()`, #34). The pet's identity is its
Pokédex number (persisted in NVS).

- **Evolution** gen-1 style (levels 16/36/…; stones ≈30, trade ≈40; Eevee
  branches to whichever evolution you're missing). Each slip-up delays it 1
  level; it won't evolve with any stat < 40 or while asleep.

## Fitness and training

Each creature trains three stats, from 0 to 100 points each, which start again
with every new pet:
- **Strength** ← the rhythm punch: 1.5 points a punch, at most 18 a session.
- **Stamina** ← steady care: a point for every 30 awake minutes with every need
  at 40 or more. The minutes add up (a minute under 40 just doesn't count), survive
  a restart, and don't count while the pet sleeps or self-care keeps it on the
  charger. Energy is one of those needs, and awake it only goes down (only sleep
  brings it back), so a pet that is put to bed for a while when it's tired builds
  stamina far faster: looked after hourly with such naps, the stamina ribbon comes
  in about a day and a half; never napped, it takes a week or more.
  The stamina hold adds a faster way: 1.5 points a rep, at most 18 a session.
- **Speed** ← the Play minigames: a fifth of the game's score (3 for a game at par).

The stat card's **Fitness** page shows the three bars, what trains each, a ribbon
at each goal, and the weight (which goes up with candy and burns off with exercise).
Each Play minigame's result shows the speed it trained (**SPEED +3**), as the
punch shows its strength and the hold its stamina. Reaching **30** strength, **10** stamina or **30** speed earns
that stat's **ribbon** (Medals page). Training also shows in the creature's own
life, as looks only: a strong pet kicks its ball further, a fast one strolls and
chases the ball up to 30 % faster, and one with stamina hops a little longer when
it's happy. Nothing else changes: no need, perk or care balance depends on it.

The serial `STATS` line still prints ATK/DEF/SPD, the gen-1 base × **genes**
(90–110 %, rolled at hatch) + level + training.

## Retention: streak, bond, medals, name

- **Streak** (the player's, persists across creatures): the first care of each
  real day advances the streak; 3/7/30/100 milestones are celebrated; skipping a
  day breaks it. Flame badge on the main screen.
- **Bond** (the creature's): rises slowly with care and petting, drops with slip-ups.
- **Medals** for the individual (level, berry, streak, bond, final form, fit, and
  the strength, stamina and speed ribbons) + a global counter. Medals page of the
  stat card. A medal or streak banner earned while a minigame or an exercise is on screen
  waits for it to close.
- **Name**: touch keyboard; the nickname rules the header and the card.

High streak and bond **improve the egg roll** (rarity and shiny): caring well
always pays off.

## Life cycle, eggs by rarity, languages

The life cycle lasts **3 days** of play. Three endings (all leave a new egg):
**farewell** (final form + 3 days), **release** (long press), **runaway** (all 4
bars at zero for 1 h). Each bred species is recorded in the **bred Pokédex**
(normal and shiny separately).

The egg rolls rarity over the ~79 base forms (47 common / 27 rare / 5 legendary),
**biased towards the lines you're missing** (all 151 are completable), blessed by
a farewell and punished by a runaway. Legendaries only with 25+ registered.
**Shiny** 1/48 (better with streak/bond/farewell).

**Languages:** the UI is in English (default) and European Portuguese, one tap
apart on the settings screen (swipe down): the language pill shows `EN | PT` with
the active one filled. The Portuguese uses *tu* throughout and has every letter
it needs: the classic 5x7 font (CP437) lacks ã, õ and most accented capitals, so
`ptglyphs.h` adds those 12 (ã õ Á À Â Ã Ê Í Ó Ô Õ Ú) as extra glyphs, one byte
each, drawn by `printT()`. Its accented capitals stand taller than the lowercase
letters, with the accent squeezed in above them, so upper-case words such as
FORMA FÍSICA stay upper-case; it redraws the font's own É to match (#85).
Pokémon names stay in English, as in the Portuguese games. Until #61 the firmware
also shipped Spanish, French, German, Italian, Japanese and Korean; a board set to
one of those shows English after the update (the stored choice is kept, see
`i18n.h`).

## Backgrounds: places, weather + real time

The idle screen paints the sky from the **RTC's real time** (dawn / day / dusk /
night with moon and stars). Sleeping forces night. Everything below is worked out on
the board from the clock alone: no network, and nothing new is saved (`weather.h`, #60).

- **Travelling.** Each creature has a home place, its **type's biome** (meadow,
  beach, forest, volcano, mountain, snow). Each day from 06:00 it is at home about
  4 days in 10, and in one of the other five places the rest of the time. The scene only
  changes while nobody is looking (the screen off, another screen open, or the
  nearly-off dim), so the creature is never seen jumping from one place to another. An
  egg stays in the meadow (a new egg after a farewell appears there at once), and the
  minigames' backgrounds follow the place.
- **Made-up weather.** Clear, cloudy, rain, storm (with a little lightning), snow and
  fog, with odds that suit each place: sun and showers at the beach, rain and mist in the
  forest, snow and fog in the snowfield and on the mountain, never snow at the volcano.
  The weather comes in 3-hour blocks that often carry on, so a spell lasts from 3 to 15
  hours, and a change fades in over 20 minutes. It is only scenery: it never touches the
  needs. Clouds hide the stars (rain, storms, snow and fog hide the moon too), and on a
  covered night the creature doesn't ask you to look at the stars.
- **Seasons** (fixed to the southern hemisphere: summer December to February, winter June
  to August). They shift the weather's odds (sunnier summers with more storms, misty
  autumns, showery springs; snow at the meadow and in the forest only in winter, never at
  the beach or the volcano), and the meadow, the forest and the mountain show them: spring
  flowers and falling blossom, summer butterflies by day and fireflies at night, autumn's
  russet ground and falling leaves, winter's snow cover and snowcaps. The beach, the
  volcano and the snowfield look the same all year.
- **The sky's layout.** The sun and the moon sit right of the sky, below the name and the
  status line, so neither text ever runs over them; a name too wide for the round screen
  at the large size (a shiny nickname at level 100) is drawn a size smaller.
- **Power.** Falling rain, snow, lightning, blossom, leaves, butterflies and fireflies are drawn only at the full frame rate.
  While the screen is dimmed, while the creature sleeps untouched or on a low battery,
  the sky, clouds and fog stay but nothing falls, and nothing is drawn with the screen off.
- **No clock yet** (after a power cut, until the time is set): fair weather at home, no season.

## Standby clock (plugged in)

Plugged in and left alone, the screen shows a dim clock instead of going dark: the time,
the date and a picture of the weather now on black, with the battery charging under them
(`standby.h`, #60). It comes when the screen would turn off (the screen-off setting), or
at 90 s when that is *Never*, and steps a few pixels each minute so the AMOLED doesn't
burn in. A screen that had gone dark lights up with the clock by itself as soon as it may
show: when the creature wakes on the charger in the morning, or when a board whose screen
timed out on battery is plugged in. Only a screen turned off with PWR (or pocket-locked)
stays dark. At night the picture shows a moon where the sun would be. Any touch (the first
only wakes) or BOOT brings the creature back, PWR turns the screen fully off, and an
attention call shows the creature for its window, then the clock again. The auto touch lock
waits for a real screen-off, so it never engages while the clock shows. Whatever was open is
put away when the clock comes up, as when an attention call lights a timed-out screen, so a
touch finds the creature and bedtime isn't held up: settings (each choice is already saved),
the card, the gallery, and the keyboard and the clock screen, which drop a half-typed name or
a time not yet confirmed. Self-care while charging carries on underneath. Nothing changes on
battery, while the creature sleeps (the screen goes dark as before), or before the clock is
set after a power cut.

## Layout

- `TamaPoke.ino` — init, game loop, hardware bring-up, buttons and power, the care glue (bedtime, attention calls, clock prompt), touch and gestures, and `render()`, which picks the screen to draw
- `app.h` — what `TamaPoke.ino` shares with the modules split out of it (#41)
- `serialcmd.h` / `serialcmd.cpp` — the USB serial console (SD provisioning and debug commands)
- `uitext.h` / `uitext.cpp` — text size, cursor and `printT()`, which draws the 12 extra Portuguese letters and the redrawn É
- `ptglyphs.h` — those 12 letters (ã õ and the accented capitals CP437 lacks) and the redrawn É as 5x7 art, packed at compile time (#61, #85)
- `scene.h` / `scene.cpp` — the main screen: sky and biome, the pet's PMD animation and walk, bath, poops, header, bars, buttons, the endings and their buttons
- `weather.h` — the place of each day, its made-up weather, the date and the season, from the clock (#60); `scene.cpp` draws them
- `skylayout.h` — where the header, the sun, the moon and the stars sit on the round screen (#70)
- `standby.h` — when the standby clock replaces screen-off on USB, and its burn-in steps (#60); `scene.cpp` draws it
- `bubble.h` / `bubble.cpp` — the speech bubbles: which line, when, and where it goes on the round screen (#23); the warm-milk hint (#13)
- `caremenu.h` — the care strips the Food, Play and moon buttons open: where they sit and which item a tap lands on (#13)
- `owner.h` / `owner.cpp` — the player's name (`OWNER <name>`): which characters the font can draw, and its NVS key
- `holiday.h` / `holiday.cpp` — the special days (#26): Easter and the other dates, the player's birthday (`BDAY <month> <day>`) and its NVS key, and how often the greeting comes; `scene.cpp` draws the decorations
- `gameui.h` / `gameui.cpp` — the minigames' screens and the calm exercises' (the rhythm punch and the stamina hold, #78; their rules are in `minigames.h` and `exercise.h`)
- `punchlunge.h` — the rhythm punch's lunge (#90): which ATTACK frames a landed punch plays, standing each on its own feet at IDLE's size, and the room they must fit
- `screens.h` / `screens.cpp` — the other screens: starter and egg choice, the pet card's pages, clock and settings, the nickname keyboard, the Pokédex gallery
- `pet.h` / `pet.cpp` — pet state and logic (stats, evolution, life cycle, streak/bond/medals, NVS)
- `sdmon.h` / `sdmon.cpp` — TPK1 (animated) and TPK2 (PMD) sprites + thumbnails, and file reception over USB (PUT/LS)
- `rtcbat.h` / `rtcbat.cpp` — PCF85063 RTC + AXP2101 PMU (battery, brightness, PWR button)
- `audio.h` / `audio.cpp` — ES8311 + I2S + Game-Boy-style tone synth (non-blocking task)
- `i18n.h` / `i18n.cpp` — the English and European Portuguese string tables, and the `lang` key
- `dex.h` — GENERATED (`gen_dex.py`): the 151 table and the egg-spot look-alike pairs
- `species.h` — GENERATED (`sprites.py`): fallback sprites, UI icons, colours
- `pin_config.h` — the board's official pins
- `tools/` — pipeline: `dex_data.py` (data), `dex_stats.py`, `gen_dex.py`,
  `sprites.py` (workshop), `pack_pmd.py` / `make_thumbs.py`
  (packers), `pack_bundle.py` (web bundle), `send_sd.py` (sends the sprite files you name over USB), `touch_log.py`
- `tools/sdcard/mons/` — the generated .bin files (animated, shiny, PMD, thumbnails)
- `web/` — the browser installer (ESP Web Tools + Web Serial sprite loader)

## Battery

The percentage on screen comes from a typical single-cell LiPo discharge curve (`batcurve.h`) over a smoothed
voltage, and only goes down while discharging; on USB or while charging the PMU's own gauge is used. When the
smoothed voltage stays under 3.30 V for 30 s the pet is saved and the PMU powers off (not in the first 30 s
after boot); the PMU itself is set to cut at 3.1 V as a backstop.

What the firmware does to save power (the policy is in `power.h`, covered by `test/test_power.cpp`):

- **Frame rate follows activity.** 10 fps while you interact; 6.7 fps after 1 min without touching;
  4 fps when the screen dims (90 s); 1 fps when it is almost off (5 min); 2 fps while the pet sleeps and
  nobody touches it. The ball minigame keeps its 85 ms step. Over 10 idle minutes this sends about 68 % fewer
  frames to the panel (each one is a full redraw plus a 434 KB QSPI transfer).
- **Static screens are not redrawn.** Keyboard, clock, card pages 2-4 and the starter choice are painted once
  and again only when something on them changes.
- **Light sleep when nobody is looking.** With the screen off, or almost off (5 min), and on battery, the CPU
  sleeps until the next frame or 200 ms; a touch wakes it at once. With USB connected it never sleeps (the
  serial port for the SD card stays available), and USB is checked right before each sleep.
- **Speaker amplifier only when it can be used.** It is off while the pet sleeps and while the screen is dimmed
  or off; a sound requested meanwhile (e.g. "ready to evolve") switches it on for 4 s, and sounds wait 50 ms
  after a power-up so the start is not cut.
- **1.75C only, on by default:** the screen turns off after 60 s without touching (dimming at 30 s), and the
  touch controller is put to sleep and its interrupt ignored 10 s after that, so the idle board can light-sleep;
  a short PWR press wakes it. The CPU runs at 240 MHz in use, 160 MHz once dimmed or asleep and 80 MHz with the
  screen off (a minigame stays at 240). The original 1.75 keeps its previous behaviour by default (screen-off and touch lock set to
  never) but can pick either in the settings, and CPU scaling applies to both boards.
- **Attention calls** light a screen that turned off by itself, or brighten a dimmed one, for 15 s (not in quiet
  hours), and only when a need first drops below 25, so a few times a day. They are noticed by the loop that
  already runs with the screen off (no extra wakeups). During those 15 s the screen draws at 10 fps with no
  light sleep, then the normal dimming and screen-off timers apply again; a call does not count as a touch.
- **Low battery (< 20 %, not charging):** brightness capped (100, then 60 under 10 %) and frames twice as slow.
- **Battery reads over I2C** every 10 s instead of 2 s when the screen is off or almost off.
- The pet walks by real time, so lower frame rates do not slow it down.

**Battery diary (#14).** To measure how long a charge really lasts, the firmware keeps a diary
(`batdiary.h`): a row every hour, and one at boot, when USB is plugged in or pulled out, and at
the low-battery power-off, with the time, the battery level and voltage, the charge state, and
how many minutes the screen was lit and the CPU in light sleep since the previous row. The
newest 96 rows (about four days) are kept in flash in their own NVS namespace, written in the
same quiet moments as the pet's autosave (the screen dimmed or off, the pocket lock blank, or the
pet asleep), so they survive a flat battery. `python3 tools/board.py batlog`
prints them as CSV; `PORTING.md` (*Measuring battery life*) has the steps of a battery test.

## Serial console (115200, debug)

`STATS` (full state) · `SPEC <dex>` (change species) · `LVL <n>` · `HATCH` ·
`SHINY` · `NICK <x>` · `BYE` / `RUN` (farewell / runaway; whatever is open over the
creature closes first, as for every ending: a minigame or an exercise is paid for what was
done) · `ABANDON` (force the
runaway-ready state) · `WIPE` (factory reset → new game) · `BEEP` (audio test) ·
`REG` (Pokédex) · `EGGS` (simulate 20 eggs) · `GAL` (gallery; not during an ending
(farewell, run-away or release): `ERR gallery not opened: ...`) · `CAREDAY` ·
`TIME <epoch>` (sets the clock and the pet's saved time; `<epoch>` is seconds since 1970 in
digits only, from 2025 to 2099, and anything else, or a time the clock does not take, gets
`ERR clock not set: ...` and changes nothing) / `RTCSET <epoch>` (the clock only, the same
check) · `HEALTH` (uptime + heap for the soak test) ·
`SAVE` (write pending pet changes now) · `OWNER <name>` (the player's name in the
speech bubbles; `OWNER` alone clears it) · `OWNER?` (read-only: `owner=<name>`) ·
`BDAY <month> <day>` (the player's birthday for the special days; `BDAY 0 0` clears it) ·
`BDAY?` (read-only: `bday=<month> <day>|none today=<none december christmas newyear valentine
easter halloween> birthday=<0|1>`, today's day on the clock; `none` on an ordinary day, while the clock is not set and during a farewell or run-away) · `CARE?` (read-only: `pace=<0|1> bed=<0-3>
inbed=<0|1> stale=<0|1> egg=<0|1> picking=<0|1>`, stale = bedtime paused until the clock
is set, egg = *Eggs* set to *Choose*, picking = the three-egg choice is on screen) ·
`SCENE <place|auto> [weather|auto] [season|auto]` (forces the place, `meadow beach forest
volcano mountain snow`, the weather, `clear cloudy rain storm snow fog`, and the season,
`spring summer autumn winter none`, in RAM only: `SCENE auto` or a restart gives them back
to the clock) · `SCENE?` (read-only: `place today home wx prev mix season clock particles standby`,
`*` after a forced value) · `BATLOG?` (read-only: the battery diary, a `BATLOG rows= cap=
unsaved= period_min=` line, a CSV header and the rows; `tools/board.py batlog` turns it into
a CSV file) · `BATLOG CLEAR` (starts a new battery diary; the pet is not touched) ·
`SDINFO` / `LS` / `PUT` (the sprite files: free space, list, send one; `tools/send_sd.py`
sends sprite files with them). `tools/board.py`
sends these from a script (clock sync, status); see `PORTING.md`.

To test fast: lower `PET_TICK_MS`, `MINUTES_PER_LEVEL` and `FAREWELL_AGE_MIN` in `pet.h`.

## Simulator

`tools/sim.sh` runs the real firmware (`TamaPoke.ino` and its modules) on the
Mac, against a simulated board, and saves PNG screenshots of the panel. Use it to see UI and care changes
without flashing. It needs nothing beyond what the firmware build already uses:
a C++ compiler, Python 3, and the GFX Library for Arduino library that
arduino-cli installed.

```bash
tools/sim.sh --lang pt --script care-page.txt --out shots/
tools/sim_shots.sh        # every main screen in both languages -> shots/<lang>/
```

`sim/scripts/every-screen.txt` reaches what `screens.txt` does not (evolution,
the dialogs, every minigame, the biomes, the endings, the BOOT and PWR presses).
`weather.txt` shows every kind of weather by day, at dusk and at night, `seasons.txt` each
season and the longest titles under the sun and the moon, and
`travel.txt` checks that the place changes only while nobody is looking (read its
`SCENE?` lines in `serial.log`), and `standby.txt` the standby clock plugged in, over a
gallery, with PWR and the pocket lock, on battery (no petals when dimmed), and with settings
left open across a bedtime, an attention call over it, the morning and a plug-in lighting a
dark screen, and PWR keeping it dark (its `standby clock` lines and `SCENE?`'s `standby=`),
and `standby-place.txt` its weather picture across the 06:00 change of place and its moon at
night. `holidays.txt` shows every special day by day and by night with its greeting (read the
`say` and `bday=` lines in `serial.log`), the birthday, and the cases with no decorations or
no greeting (no clock, quiet hours, a farewell).
Run with the other scripts in `sim/scripts/`, it lets two builds be compared
screen by screen.

A script is one step per line (the full list is in `sim/script.h`):
`tap X Y`, `hold X Y 3s`, `swipe left|right|up|down`, `press pwr 3`,
`press boot`, `wait 2h`, `serial SPEC 25`, `battery 15`, `usb on`,
`idle doze` (the creature's next idle activity, simulator only; see
`sim/scripts/idle-life.txt`), `say hungry` (a speech bubble now, simulator only; `say christmas`
and the other special days' greetings too),
`train 30 10 30` (the creature's strength, stamina and speed training, simulator only; see
`sim/scripts/fitness.txt`), `game catch` (the next Play deals this minigame, `keepy`,
`catch`, `peek` or `song`, simulator only; see `sim/scripts/game-screen-off.txt`), and `shot NAME`.
`expect Play: peek score=* (screen off)` checks the serial output: some line printed
since the previous check contains the text, where `*` stands for any run of characters;
`expect-not` checks that none does, and `expect-once` that exactly one does. A failed
check prints what it looked at and the run exits 1 at its end; `tools/gate.sh` runs every
script in `sim/scripts/` that has a check, on both boards (see `game-screen-off.txt` and
`exercise-screen-off.txt`).
`lang`, `clock 2026-11-13 22:30` and `pmu none` (the power chip
doesn't answer) set the language, the time and the PMU before boot, and only
there (`clock none`: no time, as after a power-off; see `sim/scripts/clock-prompt.txt`);
`battery`, `usb` and `charging` placed before the first step that
runs the firmware (`tap`, `wait`, `shot`...) are also in place at boot. Each run starts a new game, with an empty NVS. Screenshots are
466×466 with the round panel masked. The firmware's serial output goes to
`serial.log` next to them. `--board 175` simulates the original 1.75 instead of
the 1.75C. `--script` takes a path, or the name of a script in `sim/scripts/`
with or without `.txt` (`care-page`). `shots/` is git-ignored. The last line of
the run counts the sound effects that would have played and the light sleeps.

### Interactive window

`tools/sim.sh --window` opens the same simulated board as a window on the Mac:
the mouse is the finger, the keyboard is the buttons, and the terminal is the serial
console. It is opt-in: it needs SDL2, which comes from Homebrew (`brew install
sdl2`), and **the owner is asked before it is installed**. Without SDL2 the scripted
simulator, the tests and the gate work as before, and `--window` says what is missing.

```bash
tools/sim.sh --window                    # the 1.75C, clock = this Mac's local time
tools/sim.sh --window --board 175 --lang pt --clock "2026-11-13 22:30"
```

| Input | What it does |
|---|---|
| left mouse button | the finger: press, drag, release. A press that starts outside the round glass is ignored (unless `--square`); a drag that leaves it stays on the panel edge |
| `P` | short PWR press (on the 1.75C, three within 350 ms lock the pocket, as on the board) |
| `B` | BOOT, held while the key is held (a long press works). 1.75C only: the original 1.75 build does not read it |
| `S` | save a PNG of the panel to `--out` (default `shots/`) as `window-NNN.png`; the path is printed |
| `Space` | pause / resume (touch and `P`/`B` are ignored while paused) |
| `[` `]` | slower / faster: 1×, 10×, 60×, 600×, 3600× real time. While a finger or `B` is held, and for 1 s of real time after the last touch, `P` or `B`, the window runs at 1× (the title says `600x (1x while touching)`), so taps, swipes and long presses last as long as they do on the board |
| `U` / `C` | USB in / out; charging on / off |
| `-` `=` | battery -10 % / +10 % |
| `H` | print this key list; `Esc`, `Q` or closing the window quits |

Type a line in the terminal and press Enter to send it to the firmware's serial console
(`HATCH`, `SPEC 25`, `STATS`...); its output appears in the terminal, or in
`--serial-log FILE`. Lines over 256 characters are dropped. The window title shows the
board, the simulated date and time, the speed, the battery, USB and charging, and
whether it is paused; if the Mac cannot keep up with a fast speed, it shows the speed
actually reached. A dimmed panel is drawn darker (the firmware's dim levels), and a
screen that is off or asleep is drawn black. `--square` drops the round glass,
`--quit-after 2m` closes the window after that much real time, and `--help` lists the
rest. It runs the firmware in real time, so after a light sleep a line typed in the
terminal can wait up to 200 ms of simulated time at 1×; a touch, `P` or `B` is handled at
once. A click is held
for at least 80 ms of simulated time, so a quick tap or trackpad click is not lost. The
terminal takes ordinary commands only: `PUT`'s raw payload cannot go through it (the
line splitter drops NUL bytes and lines over 256 characters, and splits at CR and LF).

`tools/gate.sh` builds the window and runs `--selftest` for both boards when SDL2 is
there (SDL's dummy video driver, no display needed): it pushes synthetic mouse and key
events through the window's own event handler and checks that the fake touch, the
buttons, the battery controls and the screenshot saw them, and that the picture drawn
has the right colours, bezel and dimming. That is the only automatic test of the
window. The window has been tried by hand only when a PR says so.

**What it models:** the real firmware code: game logic, every screen, the
Arduino_GFX drawing code and font (the extra Portuguese letters included), the frame pacing, the
firmware's dim and screen-off decisions, the touch lock, the PWR lock, the BOOT button, and the
strength-training touch reports. It also models the sprites and thumbnails from
`tools/sdcard/` (the 1.75C without the shiny sprites, as on the board), the NVS
save in memory, and a clock you can fast-forward (`wait 2h` takes well under a
second). The firmware is compiled with `char` unsigned, as on the ESP32-S3.

**What it doesn't model:** battery life, charging and the battery curve (the
percentage is whatever the script sets), touch feel and the CST9217's own
timing, audio (sounds are counted, not played), real timing (drawing and QSPI
take no time, and light sleep is a jump of the clock), the touch INT line
(it is edge-only: the ISR fires on press, move and release, but the pin always
reads high, so level checks of the pin and the `touch.sleep()` hazard of holding
it low cannot be reproduced), and AMOLED brightness (screenshots are full colour
unless the panel is asleep, off or at brightness 0, which are saved black; the
interactive window does darken a dimmed panel, roughly). It
doesn't model PSRAM or heap limits, or NVS flash wear. The PMU, RTC, ES8311 and
QMI8658 are stand-ins, not drivers. The host is 64-bit, so code that depends on
the size of `long` or of a pointer can behave differently. A screen that looks
right in the simulator still needs a check on the board.

## Roadmap

- **Wild encounters / battle** — designed (see project memory): resolution by
  ATK/DEF/SPD with PMD Attack/Hurt animations, trainer rank as endgame. Style
  still to pick (auto / timing / turn-based).
- **Soak test** 24–48 h (instrumentation ready: `HEALTH` command/heartbeat).

*(Done: 3D-printed case [published on MakerWorld](https://makerworld.com/es/models/2937822-tamapoke-a-pokemon-pokeball-tamagotchi); repo public with the browser installer + one-click sprite bundle.)*

## Community forks

- **[TamaPoke — Expanded](https://github.com/ShadowEnemyx/TamaPoke/tree/tamapoke-expanded-update)** by **ShadowEnemy** — a substantial community fork (different author/branch): a full **type-matchup battle system**, all **151 + shinies** with a **Pokédex / collection box** and daily goals, **6 UI languages**, **ES8311 sound**, starter choice and a one-click web installer. Worth a look. 🎮
- **[TamaPoke](https://github.com/DylanPDao/TamaPoke)** by **DylanPDao** — another substantial fork: **gym battles** and **LAN battles** between two devices, **movesets**, a **party + box** system, an **EV/IV** stat system, and coverage extended **up to Gen 3 (386)**. Keeps the PMD sprite pipeline. 🏆
- **[TamaPoke — Life sim](https://github.com/Mu-Muu/TamaPoke)** by **Mu-Muu** — a gentle, battle-free fork: **all 1025 species** with regions that unlock in order, evolution by age, natures and sickness, real **weather** and a **standby clock**, a farm and album, **English + 简体中文**, points from **chess.com** games, optional **Stack-chan** voice control, and its own web installer. 🌱

## Credits

All sprites: [PMD SpriteCollab](https://github.com/PMDCollab/SpriteCollab)
(community, CC BY-NC). Base stats: [PokéAPI](https://pokeapi.co). Pokémon is a ™ of
Nintendo / Game Freak / The Pokémon Company. Non-commercial, personal-use project.
Full list in [`CREDITS.md`](CREDITS.md). Version history in
[`CHANGELOG.md`](CHANGELOG.md) — most of the fixes there came from people
who built one and reported what they found.

## License

- **Source code** (firmware + tooling): **[MIT](LICENSE)**.
- **Sprites & names**: © Nintendo / Game Freak / The Pokémon Company; pixel art
  from [PMD SpriteCollab](https://github.com/PMDCollab/SpriteCollab) (CC BY-NC 4.0).
  **Non-commercial use only.**
- **3D-printed case**: remix of *"Pokeball"* by **yoyothechicken**
  ([MakerWorld #839922](https://makerworld.com/es/models/839922-pokeball)),
  licensed **CC BY-NC-SA**, and shared here under the same terms.

This is an unofficial fan project, not affiliated with or endorsed by Nintendo.
