# Changelog

All notable changes to the firmware. Versions match the number shown at the
bottom of the clock/settings screen (swipe down) and the `TamaPoke fw v<version>`
line printed over USB serial at boot.

On the 1.75C, `tools/update_175c.sh` updates the firmware and keeps your Pokémon
(it writes only the app). The [web installer](https://socquique.github.io/TamaPoke/web/)
is upstream's, for the original 1.75, and installs upstream's firmware, not this
fork's. `web/` here still holds upstream's 1.19 build and is not hosted.

## [Unreleased]

### Added

- **Walks** (#117), on the 1.75C. Its motion sensor now counts steps by itself (the QMI8658's
  step engine, at 62.5 Hz: under 1% of the battery, and the datasheet's own step profile
  counted 102 on a 100-step walk), instead of being powered down. Steps taken off the charger
  and outside bedtime count, and the Fitness page shows **Steps today** and the **walk
  treats** banked: one treat for every 250 steps, at most 40 earned a day, 40 in the bank, kept overnight. Off the charger,
  when a need falls under 40 the creature spends a treat to bring it up to 80 (food,
  hygiene, energy, joy, one a minute, awake only; a bath treat clears the poops too), so a board
  in a bag on a desk still gets help for a while: about 5 to 6 minutes a treat, so a 3,000-step
  day (12 treats) holds the needs up for about an hour and a quarter, and a full bank of 40
  for about three and a half hours. Every 1,000 steps adds a stamina training point (at most 5 a day), the **walking
  ribbon** comes at 5,000 steps in a day, and after a good walk the creature says "That was a
  lovely walk!" (the ribbon's chime stays silent in quiet hours, as for a medal that time earned;
  a stamina point earned while there is only an egg waits for the hatchling). Steps taken on
  the charger or at bedtime are not counted. The serial `STEPS?`
  shows it. The original 1.75 has no step counter and is unchanged. The simulator has a
  `steps N` step and a `board 175c|175|both` step (`sim/scripts/walks.txt`).

- **Accessories earned by care** (#116). Each creature earns its own things to wear on its
  head from its medals: a **Bow** (its favourite berry found), **Flowers** (a 7-day streak), a
  **Bobble hat** (full bond), a **Cap** (level 10), a **Top hat** (level 25), a **Crown**
  (level 50) and a **Laurel** (any training ribbon, or IN SHAPE). Each one earned gets a
  banner of its own straight after its medal's ("NEW: CAP!"), and the first one goes on by
  itself. The stat card has a sixth page, the **Wardrobe** (swipe up, then left to the end):
  tap one to wear it, tap *None* to take it off, and tap a grey one to see what earns it; a
  red dot marks one you haven't seen yet. On a special day the creature wears that day's hat
  by itself (a Santa hat all December, a witch hat on 31 Oct, bunny ears on Easter Sunday, a
  heart clip on 14 Feb, a party hat on 1 Jan and on your birthday), over whatever it wears;
  picking something on the Wardrobe page that day wins until midnight. It takes its
  accessory off to sleep and to doze, and wears nothing as an egg, while evolving (the new
  form comes back wearing it), in minigames and exercises, and at a farewell, run-away or
  release. A new creature hatches with nothing earned and wearing nothing, unless your 7-day
  streak is running: then it earns the Flowers as it hatches, and puts them on. An old save loads
  wearing nothing, with what its creature's medals already earned waiting on the page.

- **A berry garden** (#115). A flower pot stands at the right-hand end of the ground,
  wherever the creature travels, and stays through an ending. Tap the empty pot to plant a
  berry (red, blue or green, a surprise), tap the plant to water it, and the rain waters it
  too. It grows only while watered: ripe after 40 watered hours, about three days of one
  watering a day. Dry a day its leaves turn yellow; dry two days a growing plant wilts (a ripe
  one never does, and the creature is never affected). Tap the ripe plant for a berry, three
  a plant: the berry hops to the creature, which eats it as a Food-button berry with +10
  food, +10 joy, a heart and +1 bond on top. The creature says when its plant is thirsty or
  its berries are ripe. Only a set clock moves the garden on: a power cut or setting the
  clock never grows, dries or wilts it.

### Changed

- The Christmas tree, the Halloween pumpkins and two of the Easter eggs moved left, out of
  the berry garden's corner, and so did two spots of spring flowers and autumn leaves.

### Fixed

- **Touch works when the creature calls for help.** When a need ran low (too many poops, say)
  and the creature called, the screen lit up for 15 s with the "!" bubble, but on the 1.75C
  every tap was ignored until PWR or BOOT, because the automatic touch lock stayed on. The
  lock now lets go while a call lights the screen, and the first tap does what it taps (a bath, food,
  a cuddle), so one tap answers the call. While the run-away button shows, the first tap on
  any call's lit screen only wakes it, also on a dimmed screen a call brightens (that first
  tap used to act there), so a tap meant for the pet can't end it. Left untouched, the
  screen goes dark again and the touch lock comes back after its usual delay. A screen turned
  off with PWR, or the pocket lock, still stays dark and locked.

## [1.20] - 2026-11-17

### Added

- **Sickness and medicine** (#15), on the stat card's Care page (*Sickness*, off by
  default). Left with its hygiene bar in the red for 6 hours (counted only while it is awake,
  outside quiet hours and with the clock set, on either pace) the creature falls sick: it
  looks poorly, says so, calls once, and can't evolve until it is better. Being sick costs: a
  slip-up when it falls sick and one every hour it stays sick, and its happiness falls a
  little faster. The green *Give medicine* button cures it at once; kept clean, it also gets
  better by itself after 12 hours. Left sick for a day it runs away by itself the next time
  you look (a warning call comes at 12 hours). It never dies. Off, nothing changes; an old
  save loads with it off.

- **Natures show in play.** A newly hatched creature's first speech bubble names its nature
  ("I'm playful!", the same word as on the stat card), and now and then (about one bubble in
  five) it says a line of its nature: "Berries are the best!", "Catch me if you can!",
  "So comfy here...". None of them asks for anything; only a low need's bubble does. Left
  alone it acts its nature a little too: a playful one plays ball and hops more, a lazy one
  stands and rests more, a greedy one heads for the food button a little sooner, and a tidy
  one strolls clear of the poops. Looks only: no new drain or reward; a playful one's extra
  ball games can reach the usual +6 JOY an hour a little sooner.

- **Battery diary.** To measure how long a charge really lasts, the board keeps a diary of
  its battery: a row every hour, and one at start-up, when USB is plugged in or pulled out,
  and when a flat battery turns it off, with the time, the battery level and voltage, whether
  it was charging, and how many minutes the screen was lit and the board asleep in between.
  The last four days or so are kept, even through a flat battery. Read it from a computer
  with `python3 tools/board.py batlog` (a CSV file); `BATLOG CLEAR` starts a new one. Nothing
  changes on screen, and the creature's save is not touched.

- **A plush toy and warm milk.** The Play button now opens a small strip, like the food
  menu: the Poké Ball for a minigame, as before, or the plush toy. With the plush the
  creature hops about for a moment: a little happiness (+10, two plays an hour) with no game
  and no cost. Awake, the moon button opens a strip too: lights out, as before, or warm
  milk, then lights out. The milk cheers it a little and adds a little weight (less than
  candy), and until it next wakes its sleep brings energy back a little faster. Now and then
  in the evening, before quiet hours, it asks for warm milk in a speech bubble. Asleep, the
  moon button still wakes it at once. A long press on a strip's item (or the food menu's)
  no longer opens the release dialog.

- **Standby clock.** Plugged in and left alone, the screen shows a dim clock with the
  date and a picture of the weather now on black, and the battery charging, instead of
  going dark: when the screen would turn off, or at 90 s with screen-off set to *Never*.
  A dark screen lights up with it by itself when the creature wakes on the charger in the
  morning or when you plug in a board whose screen timed out; only PWR keeps it dark. At
  night its picture shows a moon. It steps a few pixels each minute so the screen doesn't
  burn in. A touch or BOOT brings the creature back, PWR turns the screen off, and an
  attention call still shows the creature. Whatever was open is put away when it comes up
  (as an attention call does), so a touch finds the creature and bedtime isn't held up; a
  half-typed name or a time not yet confirmed is dropped. Nothing changes on battery,
  while the creature sleeps, or before the clock is set after a power cut. Idea from
  Mu-Muu/TamaPoke, re-implemented offline.

- **Seasons.** Spring flowers and falling blossom, summer butterflies by day and
  fireflies at night, autumn's russet ground and falling leaves, and winter's snow cover
  and snowcaps, at the meadow, the forest and the mountain. The seasons also shift the
  weather: sunnier summers with more storms, misty autumns, showery springs, and snow at
  the meadow and in the forest only in winter. Fixed to the southern hemisphere (summer
  December to February). No season before the clock is set after a power cut. Serial
  `SCENE` takes a season too. Idea from Mu-Muu/TamaPoke, re-implemented offline.

- **Travelling and made-up weather.** The creature no longer spends its whole life
  in one scene. Each day from 06:00 it is at home (its type's place) about 4 days in
  10, and at the meadow, beach, forest, volcano, mountain or snowfield the rest of the
  time. The scene only changes while nobody is looking, so you find it somewhere new
  rather than seeing it jump. Over each place there is weather: clear, cloudy, rain,
  a storm with a little lightning, snow or fog, with odds that suit the place (never
  snow at the volcano). A spell lasts from 3 to 15 hours and a change fades in over 20
  minutes. It is only scenery and never touches the needs. Clouds hide the stars, and
  on a covered night the creature doesn't ask you to look at them. Falling rain and
  snow are drawn only at the full frame rate, never while dimmed, while it sleeps
  untouched or with the screen off. Before the clock is set after a power cut: fair
  weather at home. Worked out on the board from the clock alone: no network, and
  nothing new is saved. Serial `SCENE` forces a place and weather for screenshots, and
  `SCENE?` shows them. Idea from Mu-Muu/TamaPoke (real weather over Wi-Fi),
  re-implemented offline.

- **A life of its own.** Left alone on a lit screen, the creature keeps itself
  busy: every few seconds it stands, looks around, strolls, rests, dozes, hops,
  chases and kicks a ball, strikes a pose (where its species has one) or, when
  hungry, walks to the food button and wishes for food. Tired, happy, chubby or up
  at night, it leans toward the fitting activities. A small thought bubble over
  its head shows a heart, a note, a question mark, dots, a sweat drop, z's or the
  food icon. A doze is a short daytime nap that any touch ends at once, with the
  day scene and every button as they are. A finished doze gives +3 ENE and a
  finished ball game +3 JOY, at most +6 of each an hour; nothing lowers a stat and
  it is not your care (no streak, bond or medal). Any touch, PWR or BOOT interrupts
  it (another screen takes a touch to open); a call only comes once a need is under
  25, when the creature is sad and shows that instead. Once the screen dims the
  creature strolls, gestures or stands as before, with no bubbles, ball or perks. Nothing new is
  saved. Idea from Mu-Muu/TamaPoke, re-implemented offline.

- **Speech bubbles.** Now and then the creature says something in a bubble above
  it for about 4.5 s: first what it needs ("I'm hungry!", "I need a bath!", "I'm
  tired...", "I'm bored...", the same need and threshold as the "!" bubble), or else
  "Good morning!" or "Look at the stars!" when the clock is set, a friendly line, or
  "I love you, <name>!". One is due 1–2 minutes after the last and waits for the
  creature's screen to be awake and free, then shows 3 s later. Never while the
  screen is dimmed or off, asleep, on an egg, in an ending, a dialog, a minigame or
  another screen, or while an attention call lights the screen; it never wakes or
  lights the screen. All 8 languages. The name is the player's, set from a computer
  with the new serial command `OWNER <name>` (`OWNER` alone clears it, `OWNER?` shows
  it), up to 12 characters with at least one letter: plain letters, digits, spaces
  and punctuation, plus the accented letters the font has (`OWNER ?` is refused
  and shows the stored name, like `OWNER?`). It is
  a new save key (`owner`), checked on load; nothing else in the save changes. Idea
  from Mu-Muu/TamaPoke, re-implemented offline.

- **Special days.** On a holiday the scene is decorated and the creature greets the
  player by name in a speech bubble ("Merry Christmas, <name>!", or just "Merry
  Christmas!" with no name): a Christmas tree with lights all December, with a little
  snow even in the southern summer, and the greeting on 25 December; soft fireworks on
  New Year's Day; floating hearts on Valentine's Day; eggs in the grass on Easter Sunday,
  worked out for each year; a friendly pumpkin on Halloween, with a purple sky, a
  full moon and a few bats on the evening of 31 October; and balloons on the player's
  birthday, beside that day's own decorations. The greeting takes the place of the next
  speech bubble (even a need's), at most 3 a day and at least 30 minutes apart, never in
  quiet hours; the birthday's wins over a holiday's. The day follows the clock from
  midnight to midnight; nothing shows while the clock is not set after a power-off, or
  during an ending, and care and rewards are unchanged. EN and PT. The birthday is set
  from a computer with the new serial command `BDAY <month> <day>` (`BDAY 0 0` clears
  it, `BDAY?` shows it and today's day); 29 February shows on 28 February in other
  years. It is a new save key (`bday`), checked on load; nothing else in the save
  changes. Idea from Mu-Muu/TamaPoke, re-implemented offline.

- **Touch gestures on the creature.** Besides a tap (a pat, at once as before):
  a double tap makes it hop; stroking back and forth over it pats it about once
  a second while you go on; six quick taps in a row tickle it into giggles, and
  from the twelfth quick tap on it gives a gentle huff instead (it turns away, no
  pat, no stat change); resting a finger on it for a quarter of a second, then
  sliding sideways, makes it walk after the finger. Each pat a gesture gives
  counts like a tap (+5 JOY, +1 bond) under the same daily bond limit as the rest
  of the care, so gestures never earn more than tapping. They work only on the main screen with
  the creature awake; taps, the release long-press, the touch locks and the
  strength-training taps behave as before, and so do swipes that move at once.
  One change: a sideways slide that starts with a short pause on the creature
  makes it walk after the finger instead of opening the Pokédex (swipe without
  pausing, or start off the creature). Nothing new is saved. Idea from
  Mu-Muu/TamaPoke, re-implemented offline.

- **The clock asks for the time after a power-off.** With no time at boot (the
  1.75C after a flat battery or a 4 s PWR hold; the original 1.75 when its RTC
  lost backup power) the clock restarts from the last saved time, so it is wrong.
  The clock screen now opens by itself once, after the starter or egg choice, a
  farewell, an evolution or an open dialog, titled "WHAT TIME?" with that old
  time in red. OK sets it (the time shown counts, changed or not); swiping up
  skips it and leaves a small clock badge in the sky, left of the pet, on the pet
  and egg screens; a tap on it opens the clock screen again. Setting the time any
  way (OK, serial `TIME`, `tools/update_175c.sh`) removes both. A crash or
  watchdog reboot keeps the clock marked as not set (in RTC memory, no new save
  key), so bedtime stays paused and the badge stays until the time is set.

- **Attention calls.** When food, joy, energy or hygiene first drops below 25
  (the red bar), the creature calls once: a new soft two-note chirp (if sound is
  on) and a "!" speech bubble beside it with the icon of the button that helps.
  The same need calls again only once it is back to 40 or more. The bubble stays
  while the need is low, silently. A call lights a screen that turned off by
  itself, or brightens a dimmed one, for 15 s, then the normal dimming and
  screen-off timers apply again; it does not count as a touch. On a screen that
  turned off by itself touch stays locked if the auto touch lock is on (PWR or
  BOOT turns the screen on for real; with the touch lock off, or not engaged
  yet, a touch does). A card, gallery, clock or keyboard left open goes back to
  the creature on a timed-out screen, and on a lit screen untouched for 5
  minutes (screen-off set to never); a screen turned off with PWR stays dark
  (chirp only); the pocket lock gets nothing. Quiet hours (the bedtime
  window, or 22:00-07:00 with bedtime off, and while the clock is unset after a
  power-off) are silent and dark. No calls while it sleeps, as an egg, while
  choosing a starter or an egg, or during a farewell or a run-away; a call waits for a minigame, dialog or other screen to close. No
  new NVS keys.

- **Play starts a random minigame.** Four short touch games now take turns:
  *Keepy-uppy* (the Poke Ball game), *Berry Catch* (slide to walk the pet under
  falling berries; rotten ones cost points, its favourite berry counts double
  once discovered), *Peekaboo* (tap the pet when it pops out of one of five
  bushes) and *Berry Song* (repeat a growing tune on four berry pads; no timer,
  and the pads light up so it works muted). Play deals them like a shuffled deck:
  each game once before any repeats, never the same one twice in a row. Each
  starts with a ~1 s title card and lasts about 25-40 s.
- Every game pays the same reward for a good game (full JOY, +3 SPEED, a heart),
  scaled from its own score; all of them train SPEED. Each keeps its own best
  score, shown on the result screen and in a new row under the medals on the
  card's Medals page. Keepy-uppy keeps its record (`ghi`); the others use new
  keys (`mgc`, `mgp`, `mgs`), and all four are range-checked on load.
- A game ends by itself after 15 s without a touch (paying for what was done),
  so one left open can't keep the screen on. It pauses while the screen is off.

- **Relaxed pace.** A new *Care* page on the stat card (swipe up, then swipe
  across to the fifth page) has a button that switches between Normal and
  Relaxed. Relaxed makes the needs (food, energy, hygiene, joy, poops) drain at
  half speed while the pet is awake, so it needs feeding about hourly instead of
  every half hour. Leveling, evolution, slip-up detection, the runaway
  countdown and sleep are unchanged, and the catch-up after a power-off is the
  same. Off by default, saved under a new key in the pet's NVS namespace.
- **Bedtime schedule.** The same page has a second button: Off, 21:00-07:00,
  22:00-07:00 or 23:00-08:00. The pet goes to sleep when the window starts and
  wakes when it ends. Only those two moments act, so waking it in the night
  sticks until the next bedtime. It waits for a minigame, dialog, bath or
  evolution to finish; a minigame left open with the screen off is abandoned without a
  prize at bedtime (an exercise never is: turning the screen off ends it, keeping what
  was earned). It follows the clock you set; off by default.
- **Choose your egg.** A third button on the Care page, *Eggs*: Surprise (the
  default, one random egg as before) or Choose. With Choose, every ending after
  the first game shows three eggs instead of one. The shell shows the rarity
  (cream Common, silver Rare, gold Legendary) and the spots the type colour; the
  species stays a surprise. Tap an egg to look at it, tap it again to pick it.
  One of the three is the usual roll (same odds, farewell blessing, runaway
  curse and shiny chance); the other two are Common eggs of other types, and no
  two of the three have spots of colours that look alike (ground and rock, poison
  and ghost, electric and ground, normal and rock, #34). Time is paused until you
  pick, and the choice survives a power-off. Saved under new keys in the pet's
  NVS namespace (`eggch`, `ecpk`, `ec0`-`ec2`).
- **Self-care while charging** (#24, always on). Plugged into USB and untouched
  for 10 minutes, the pet looks after itself up to half: once a minute the first
  need under 40 (food, then hygiene, which also clears the poops, then energy,
  then joy) goes back to 50. It isn't the player's care (no streak or bond, no
  medal of its own) and is silent. A need it lifts that minute is not a slip-up,
  so a pet left on the charger can still earn IN SHAPE and evolve on time. Off
  while asleep, as an egg, during an ending and in the
  powered-off catch-up, and off when the AXP2101 doesn't answer at boot (the
  firmware then reports USB as present). No new NVS key. Each top-up logs a
  `self-care:` line on the serial port. The "ready to evolve" chime now sounds
  at most once per level for each form, since the needs keep coming back over 40.
- Simulator: `pmu none` before boot simulates a power chip that doesn't answer;
  `sim/scripts/self-care.txt` and `self-care-no-pmu.txt` show self-care.
- **Natures** (#25). Every creature now hatches with one of five natures, shown on the
  stat card (swipe up) under its berry and age. *Easygoing* has no quirks. *Greedy* gets
  hungry a little sooner, berries fill it more and it trains strength a little faster.
  *Playful* gets bored a little sooner, play and the Play games cheer it more and it
  trains speed a little faster. *Tidy* needs a bath a little sooner, a bath also cheers
  it up and it trains stamina a little faster. *Lazy* tires a little sooner and sleep
  restores it faster. The quirk is small: one extra point off that need every four
  minutes awake (every eight at the relaxed pace), nothing extra asleep, and a long
  power-off still stops every need at the same floor of 15. The training bonus is +25%,
  rounded up so a small gain still counts. A creature from before the update is
  Easygoing. `STATS` prints `nat=<name>`. Idea from Mu-Muu/TamaPoke, re-implemented
  offline.
- **`CARE?` serial command** prints `pace=<n> bed=<n> inbed=<0|1> stale=<0|1>
  egg=<0|1> picking=<0|1>` (`stale=1`: bedtime paused until the clock is set;
  `egg=1`: Eggs set to Choose; `picking=1`: the three-egg choice is on screen).
- Strings for the Care page and the egg choice in all eight languages.
- Simulator: `sim/scripts/every-screen.txt` reaches what `screens.txt` does not
  (evolution, the dialogs, every minigame, the biomes, the endings, BOOT and PWR),
  so two builds can be compared screen by screen in every language.

- **Fitness page and training ribbons** (#76). The stat card's "Battle" page is now
  **Fitness**: three bars for the training you gave the creature, 0 to 100, as
  **Strength**, **Stamina** and **Speed**, each with a line saying what trains it (the
  punch, steady care and the hold, the Play games), and the weight under them. Reaching **30**
  strength, **10** stamina or **30** speed earns that stat's **ribbon**; its goal sits
  on the bar as a rosette that turns green once earned, and the Medals page shows the
  three ribbons in a row under the medals (now 11 in all). A creature already past a
  goal earns its ribbon a minute after the update. The stamina ribbon, earned by the
  minute, keeps quiet in quiet hours like the other medals time earns; one earned by
  exercise sounds. Each Play minigame's result now shows the speed it trained
  (**SPEED +3**). Training also shows in the creature's own life, as looks only: a
  strong one kicks its ball further, a fast one strolls and chases the ball up to 30 %
  faster, and one with stamina keeps hopping a little longer. Serial `STATS` still
  prints the full ATK/DEF/SPD. The simulator's `train STR STA SPD` step sets the
  training for screenshots (`sim/scripts/fitness.txt`).
- Simulator: `expect TEXT`, `expect-not TEXT` and `expect-once TEXT` steps check the serial
  output (#99): some line, no line, or exactly one line printed since the previous check
  contains TEXT (`*` stands for any run of characters). A failed check makes the run exit
  1, and `tools/gate.sh` runs every script with a check on both boards.
  `game-screen-off.txt` and `exercise-screen-off.txt` now check what was only read in
  `serial.log` before: PWR, the pocket lock and a flat battery each pay a minigame or an
  exercise once, with its own line, and nothing pays it again later.

- **LiPo charge profile on the AXP2101** (both boards). Charging is enabled at
  4.20 V. Constant current is **300 mA on the 1.75C** (the AXP2101 power-on
  default, 0.6C for its 500 mAh cell; there is no battery temperature sensor,
  so the factory rate is used) and 400 mA on the original 1.75 (1100 mAh pack; owner's choice, upstream left the chip default),
  `CHARGE_CURRENT_*` in `board.h`. Termination is the chip default, **125 mA** (reg `0x63` =
  `0x15`): 25 mA never crossed the taper on this board, so the charger stayed
  in CV at 4.20 V and `isCharging()` stayed true. USB input limit is **900 mA**
  (reg `0x16` = 2). With a 500 mA input limit the charge current plus the system
  load can reach the limit, which puts the chip in input-current regulation, and
  the datasheet disables termination for as long as that lasts; 900 mA avoids it. Precharge stays at
  the 125 mA reset value. The TS pin is taken out of battery-temperature mode
  (the 1.75C has no NTC) and the button-cell charger is forced off. Rails are
  not retargeted. The `HEALTH` line adds charger state, die temp (`22+(7274-raw)/20`, raw
  included; the datasheet only specifies 0.1 mV/LSB), which rails are on, and
  CPU MHz when built with `-DTAMAPOKE_CHG_DEBUG=1`, which also prints every 60 s
  plus FreeRTOS run-time stats. Release builds (the default, 0) print the plain
  5-minute line.

- **Battery as a number.** The clock screen cycles the header between the
  existing icon and a percent (`87%`, with a `+` while charging). The choice
  is saved. The default is still the icon.

- **Automatic touch lock.** After the screen has been off for 10 s (the
  default on the 1.75C; off on the original 1.75), 0 s, 30 s or 60 s, touches no longer wake it. A short PWR press
  wakes the screen and clears the lock. The clock pill cycles those delays
  and off. The CST9217 is put to sleep only once the lock engages, and a
  wake re-inits it. While the lock is on the touch interrupt is detached and
  light sleep no longer polls or arms it as a wake source (a sleeping CST9217
  leaves TP_INT open drain, which reads low). The triple-press pocket lock is
  unchanged.

- **Screen-off timeout.** The clock screen cycles 60 s (the default on the
  1.75C; the original 1.75 defaults to never), 30 s, 15 s and never. The panel dims at half that time and turns off at the end,
  including while the clock, keyboard or gallery is open. USB does not disable
  it; USB only skips light sleep. Never keeps the existing 90 s dim and 5 min
  near-off. A minigame is left running.

- **CPU frequency scaling.** 240 MHz while the pet is in use, 160 MHz once the
  panel has dimmed or the pet is asleep, 80 MHz with the screen off or at the
  5 min near-off step. With the default 60 s timeout that is 160 MHz from 30 s
  (the dim) and 80 MHz from 60 s (screen off), on battery. A minigame stays at 240 MHz. USB stays at 240 MHz while
  the screen is on, and drops to 80 MHz with the screen off, same as on battery.
  Light sleep stays off while USB is connected, so the serial port stays up.
  With the screen off the loop waits 100 ms instead of 20 ms.
  Not changed while a sound is playing or queued, as a precaution: the I2S
  source on the S3 is PLL_F160M by default, not the CPU clock.

- **Codec and I2S stay on; only the amplifier is gated.** An earlier
  version also powered the ES8311 down (REG0D) and stopped I2S while muted or
  idle. On the real 1.75C that left the board silent after a BOOT mute then
  unmute, and no ES8311 driver source was available to get the wake sequence
  right, so that gating was removed: the PA pin still drops while the pet
  sleeps, the screen is dimmed or off, or sound is muted, as before.
  `audioBusy()` now also covers a sound that is playing or queued.

- **Waveshare ESP32-S3-Touch-AMOLED-1.75C** (experimental community support):
  build with `tools/build_175c.sh` (`-DBOARD_AMOLED_175C=1` plus
  `boards/175c/partitions.csv`). Sprites come from a LittleFS partition in flash
  instead of the microSD slot it doesn't have, and time comes from a software
  clock (no PCF85063). 1.75C only: triple-press PWR locks the screen and touch
  for your pocket (hold 4 s still powers off), BOOT toggles sound, and strength
  training counts taps anywhere on the screen with a 1-second "Hold to exit"
  button. The default build is still the original 1.75 and is unchanged. See
  `PORTING.md`.

- **Volume levels** (#21). The sound button on the clock/settings screen now
  shows a speaker with three bars and cycles **high** (as before), **medium**,
  **low** and **off**. The level is saved, so it survives a restart or update.

### Changed

- **A landed punch is a real strike** (#90). In the rhythm punch, each punch that lands
  now plays the creature's own attack, from the moment it strikes, while it leans into the
  bag: Charmeleon swings its tail at it, Pikachu and Snorlax crouch into the blow. It plays
  for a moment (at most 0.7 s) and the creature is standing still again well before the bag
  lights up for the next punch, so it never gets in the way of the timing. Every species
  does it at its usual size, standing on the ground (drawn the plain way, most would have
  shrunk and leapt into the air). A tap that doesn't land still shows nothing and costs
  nothing. Looks only: the punches, the points and the save are unchanged.

- **One copy of the scene's shared numbers** (#66). The night hours (20:00 to 6:00), the
  sky's dawn and dusk hours and colours, the pet's walk range and the screen's centre
  column, the hop's shape, the Food button's x and the stamina blue were each written out
  in several files; each now has one name (`skylayout.h`, the new `playarea.h`,
  `caremenu.h`, `cardlayout.h`), and the build checks that the hungry pet's spot sits under
  the Food button. Nothing changes on the screen: the firmware draws every simulator
  screenshot the same, byte for byte, on both boards. (The simulator's speech-bubble script
  now shows its "Look at the stars!" shots under a clear night, the only sky the creature
  says it under.)

- **The unused `Pet::play()` is gone** (#87). The Play button runs the minigames
  (`playGame`), and only the host tests still called `play()`; they now test a game's
  reward instead. The two warnings Linux g++ gave on the host tests are fixed too (#98),
  and CI now fails on any new one. One of them was also a firmware warning, so each
  firmware build has 17 instead of 18. Nothing changes on the screen or in the save.

- **Calmer exercises** (#78). The stat card's button is now *Train*, and it offers
  two exercises instead of tapping the bag as fast as possible:
  - **Punch** (strength): the bag swings in to the creature and back, 12 times. Tap as it
    swings in, when it lights up, to land a punch; a tap at any other time does nothing
    and costs nothing.
  - **Hold** (stamina): keep a finger on the screen and the creature does reps, one every
    1.5 s, up to 12. Lifting the finger only pauses it; 15 s with no finger ends it.

  Each punch or rep is worth 1.5 points, so a full session still gives +18 at most (+23
  for a greedy creature's strength or a tidy one's stamina, #25), and
  either one tires and feeds like the bag did. Leaving early (*Hold to exit* on the
  1.75C, a tap at the very top on the 1.75) keeps what was earned; a session with
  nothing done costs nothing. The punch keeps its own record (most punches in a session);
  the old bag's tap record is no longer shown but stays in the save. Stamina still grows
  by itself with steady care, as before.

- **Stamina grows with steady care you can actually give** (#76). It took 12 hours
  in a row, awake, with every need at 40 or more, counted in memory only, so a
  restart or one hungry minute started it again and no creature got there. Now
  every awake minute with every need at 40 or more counts towards the next point,
  one point per 30 such minutes; a minute under 40 just doesn't count, and the
  count is saved, so a restart keeps it. Minutes self-care keeps the creature going
  on the charger don't count, nor does sleep. Energy is one of the needs and only
  sleep restores it, so putting a tired creature to bed for a while is part of the
  steady care: looked after hourly with such naps, the stamina ribbon comes in about
  a day and a half.

- **Two languages: English and European Portuguese** (#61). Spanish, French,
  German, Italian, Japanese and Korean are gone. **A board set to one of them
  switches to English on its first update**; the saved choice is kept as it is
  (an older firmware would still read it), and one tap on the language pill picks
  English or Portuguese again. English and Portuguese choices carry over as they
  are. The Portuguese is rewritten as European Portuguese with *tu* throughout
  ("A evoluir!", "Toca no ovo...", "A tua vez!", "Adoro-te!"), and now has every
  letter it needs: ã, õ and the accented capitals the font lacked (Á À Â Ã Ê Í Ó Ô
  Õ Ú) are 12 new glyphs, so "NÃO", "QUE HORAS SÃO?" and "EVOLUÇÃO" read right.
  The player's name (`OWNER`) may use those letters too ("João"). The language
  pill shows `EN | PT` with the active one filled. On the 1.75C the BOOT and lock
  toasts ("Sound on", "Muted", "Locked", "Unlocked") follow the language. Pokémon
  names stay in English, as in the Portuguese games. The Japanese and Korean fonts
  and the other languages' text leave the image (about 250 KB), and the U8g2
  library is no longer needed. The unused short medal labels went too (#59).

- **`TamaPoke.ino` is split into modules** (#41), moving code without changing
  it: the serial console is `serialcmd.cpp`, the text helpers `uitext.cpp`, the
  main screen's scene and pet drawing `scene.cpp`, the minigame and
  training-sack screens `gameui.cpp`, and the other screens (starter and egg
  choice, the pet card, clock and settings, the keyboard, the gallery)
  `screens.cpp`, with `app.h` declaring what the sketch shares with them.
  `TamaPoke.ino` keeps setup, the loop, the hardware, power, the care glue, touch
  and the choice of screen: 1,766 lines instead of 4,399. The tables in `dex.h`
  and `species.h` are `inline` instead of `static`, so each is stored once
  however many files use it: the firmware is about 2.6 KB smaller. Nothing
  changes on the screen or on the serial port.

- **The unused `drawMedalBadge` is gone** from `screens.cpp`. It drew the pet
  card's medal badges before the card was split into pages; since then the
  medals page has drawn its own and nothing called it. Nothing changes on the
  screen.

- **Clear power pills on the settings screen** (#11). The touch lock, screen-off
  and battery pills showed raw codes (`L10`, `60s`, `--`, `icon`, `%`). Each now
  shows an icon and a number like the sound and brightness pills: a padlock and
  the seconds after the screen goes off (`0s` locks as it goes off; an open
  padlock and "off" never locks), a screen and the seconds without a touch
  ("off" = never), and the battery as the header shows it (its icon or `87%`,
  with the live level). "off" is the only word, in every language. The pills are
  centred on the screen, 36 px tall instead of 26 (as tall as OK) and well inside
  the round edge, and a tap reaches exactly the pill that is drawn. The sound,
  brightness and language row moved up 14 px to make room. In Japanese and Korean
  the time and "OK" were placed for the Latin font; they are now centred on the
  text's measured width, which brings the time from about 44 px left of centre to
  within about 7 px.

### Fixed

- **A banner is no longer hidden under a button over the creature** (#15). A medal's or a
  streak's banner used to be drawn under the evolve, farewell or run-away button, which hid
  its text. Those buttons now wait while a banner shows, and a tap there is a pat.

- **A long press on a button over the creature no longer asks to release it** (#15). Three
  seconds on the evolve, farewell, run-away or medicine button offered "Release?"; the long
  press now has to start on the creature itself.

- **`tools/send_sd.py` sends only the files you name** (#110). For example,
  `python3 tools/send_sd.py ps151.bin` adds one species' shiny sprite to the 1.75C.
  Before sending, it checks that the files fit in the board's free space (`SDINFO`; a file
  it replaces counts as freed) and sends nothing if they don't. `--check` gives that answer
  without sending anything. It prints one line per file, stops at the first that fails with
  the command to send it again, and exits non-zero. It no longer needs pyserial: it uses
  `tools/board.py`'s console. A bare run used to send all 303 files (41.7 MB), which can't
  fit on the 1.75C. It now shows how to use it instead. It refuses a file of a size the
  board's loader would not accept, and Ctrl-C part-way through a file waits for the board to
  give up on it and prints the same command to send it again.

- **Slip-ups stop at 255** (#15). The count of slip-ups is one byte, and its 256th went back
  to 0, as if the creature had never been neglected. It now stays at 255.

- **An ending closes everything open over the creature first** (#33). A farewell, a
  run-away or a release now closes whatever is open before it starts: a minigame or an
  exercise ends and the creature keeps what it earned (as when the screen goes dark), a bath
  finishes with the creature clean, and the gallery, the rename keyboard, the clock and
  settings screen, the stat card and any menu strip or dialog close, so the ending always
  shows on screen and nothing is left open over the next egg. A bath under way used to clean
  the next creature instead, and a minigame left open lost its score. Normal play rarely
  met this (the ending buttons only show on the main screen); the serial `BYE` and `RUN` did,
  and `GAL` no longer opens the gallery over an ending.

- **The serial clock commands take only a real time** (#65). `TIME <epoch>` and
  `RTCSET <epoch>` now refuse anything but the digits of a time from 2025 to 2099, with
  `ERR clock not set: ...`: an epoch with a digit too many was taken as 2038-01-19, a minus
  sign could set a time in 2050, anything after the digits was silently ignored, and every
  real time from 2038 on was set as 2038-01-19. `tools/board.py clock` always sent a
  correct time and still does.

- **Portuguese accented capitals at full height** (#85): the accented capitals, and the
  font's own É, now stand as tall as the other capitals, so FORMA FÍSICA, NÍVEL and
  FÔLEGO no longer read as lowercase.

- **Every new medal gets its banner, and a streak's shows its whole time** (#82, #83). When
  several medals came at once (an older creature can reach level 10, IN SHAPE and a training
  ribbon in the same minute), the banner named only one of them; and a streak milestone that
  came with a medal (the 7-day streak always brings one) showed for only half a second. Now
  they take turns: the streak first, then each medal, each for its whole time. Their sound
  is as before (one chime as they are earned, silent in quiet hours for the ones time earns),
  banners that come under a minigame's or an exercise's result still wait for it to close,
  and a farewell, a run-away or a release clears any still waiting.

- **At night the stars keep out of the minigames' words** (#84). On a minigame's or an
  exercise's result and on a game's title card, the stars sat among the lines and could
  speckle the numbers; during play they touched the record line and the time bar. Now the
  result and the title card show the night sky without stars, and during play the stars stay
  below the top lines.

- **Nothing comes back after weeks switched on.** After about 25 days without a restart, a
  medal's or a streak's banner from weeks before could show again, and so could an
  evolution, a meal, warm milk or a plush play, which then held back bedtime and the
  attention calls. Each now ends for good.

- **Turning the screen off ends a minigame** (#94), as it already ends an exercise. PWR
  during a Play minigame (or the 1.75C's pocket lock) left it open in the dark with the CPU
  at full speed, and bedtime then took it away with no prize; if the battery ran out
  mid-game the score was lost. Now the game ends at once and the creature keeps what its
  score so far earned, as when the game ends by itself, but with no result screen and no
  sound; a game nobody has touched yet just closes, for nothing and at no cost. The board
  goes back to its usual low-power screen-off, and the screen comes back on to the
  creature. A medal or ribbon the game earned chimes as you press PWR and shows its
  banner when the screen is back on.

- **Turning the screen off ends an exercise** (#88). PWR during the punch or the hold
  (or the 1.75C's pocket lock) left it open in the dark, keeping the CPU at full speed
  until the screen came back on or bedtime ended it with no prize. Until the touch lock
  engaged (10 s after the screen goes off by default on the 1.75C, never on the 1.75) a
  touch in the dark could land a punch unseen, and the 1.75C kept polling its training
  touch capture. Now the session ends at once and the creature keeps what it did, as when
  you leave early, with no result screen (and it keeps it too if the battery runs out
  mid-exercise); the board goes back to its usual low-power screen-off, and the screen
  comes back on to the creature. A ribbon a session ended by PWR earned chimes as you
  press it and shows its banner when the screen is back on. A tap whose finger lifts
  after PWR has turned the screen off now does nothing.

- **Train says so while the creature sleeps** (#89). Tapping *Train* (or an exercise)
  on the stat card while it slept closed the card and did nothing. Now the card stays
  open on its Fitness page and a note says "Shh, it's sleeping" for 2 s. The note is the
  1.75C's toast, which the original 1.75 now has too.

- **"Hold to exit" is readable at night on the 1.75C's training screen** (#78). Its
  label and outline were drawn in the night ink, light on the light button, as no
  minigame's is.

- **A medal or streak banner earned during a minigame or an exercise shows after it**
  (#69). When a game or an exercise was the day's first care, the medal or streak
  milestone it completed showed its banner only under the result screen, so you
  heard it but never saw which. The banner now waits for the result to close, then
  shows its full time on the creature's screen. A medal the exercise completes (IN
  SHAPE, a ribbon) now comes as the result closes, instead of up to a minute later.
- A save with training points, weight or medal bits out of range (a damaged or
  foreign save) loads with them back in range, as the other values already did.

- **Speech bubbles and the "!" bubble sit by the creature's head** (#73). They were
  pinned high up under the status line, far above a small creature. Now their tails
  point just over its head, and they rise and fall with its hops as the little
  thought bubbles do. With the evolve, farewell or run-away button over the creature
  they stay above the button, and they are not drawn over the poops or while it huffs.

- **The sun and the moon no longer sit on the name** (#70). They moved right of the sky,
  below the name and the status line, and the stars moved out from behind the status
  line too.

- **The creature's own sounds keep out of quiet hours** (#44). The "ready to
  evolve" chime, the "about to run away" warning, the hourly level-up and the
  sound of a medal that time earned (level 10, 25 or 50, IN SHAPE) could play at
  night, most of all with bedtime off, where the level-up sounded every hour.
  They now follow the attention calls' quiet hours: the bedtime window, or
  22:00-07:00 with bedtime off, and while the clock is unset after a power-off.
  One that comes due then is skipped for good, not played in the morning; the
  buttons, the level and the medal banner show as usual. Sounds the player
  starts, and medals the player's care earns (the favourite berry, the streak,
  the bond, a final form), play as before, and so does an IN SHAPE the player
  earns by playing or training (exercise burns weight too; the medal comes with
  the next minute, as before, after the game's result screen). Only a medal of
  time alone, noticed by the minute tick with no game or training just before,
  is silent. The serial log has one line for each
  one skipped (`alert evolve: silent (quiet)`, likewise `run-away`, `level N` and
  `medal`) and for each evolve or run-away alert that sounds. No new NVS keys.

- **The run-away button goes as soon as the pet is no longer neglected.** It
  stayed for up to a minute after a care, or after a night's sleep had brought the
  energy back, so the "about to run away" warning could sound at the morning wake
  and the button could still be pressed. It now needs all four needs still at 0.

- **A time the clock does not take no longer reaches the pet's save.** Serial
  `TIME` saved the time it was sent as the pet's last-seen time even when the
  clock ignored it (a stray `TIME 5`, a mistyped epoch it could not keep), so
  the next boot could count the gap to the real time as up to two weeks
  offline. Now the save and
  the bedtime clock follow only a time the clock took; otherwise `TIME` answers
  `ERR clock not set: ...` and changes nothing. The clock screen's OK works the
  same way. Both boards now refuse a time outside 2025 to 2099 (a mistyped
  `TIME -5` became 2106 and the 1.75C clock then wrapped to a tiny time; on the
  original 1.75 the PCF85063 kept a two-digit year, so 1970 read back as 2070),
  and a saved time past 2099 no longer seeds the clock after a power cut. A pet
  already saved with a time outside that range gets no offline time from it at
  its next boot.
  `tools/update_175c.sh` now says why the clock was not set instead of "did not
  answer".

- **Leaving a minigame** is now a "Hold: exit" button at the top of every game,
  held for 1 s like the strength bag's on the 1.75C. Before, any tap near the
  top quit keepy-uppy without a reward, including a tap aimed at a high ball.
- A finger held still for 3 s during a minigame could open the "Release?"
  dialog behind the game. Games now get the touch directly, with no long press
  or swipes.
- **"Rare egg!" / "Legendary egg!?" now shows on the egg screen.** It was drawn
  and then painted over by the bottom panel in the same frame, so it never
  appeared.
- **The stat card closes when an ending starts.** A farewell or runaway started
  from the serial console with the card open left it over the ceremony and the
  new egg (on the first page it read past the Pokédex table).

- **Auto touch lock no longer defeats light sleep.** A sleeping CST9217 leaves
  TP_INT open drain, which reads low, so the idle loop saw a touch on every
  pass and never slept. The pin is now ignored (not polled, not a wake source,
  ISR detached) while either lock is on. The 1.75C PWR press is read from the
  PKEY status register directly, so a failed I2C read is no longer a press.
  If the touch chip does not answer when it is brought back up, TP_INT is
  pulled up (so a floating pin cannot read as a touch), stays ignored, and the
  bring-up is retried every 2 s while the screen is on, up to 5 times (each
  failed attempt blocks about 0.5 s), then again after the next touch unlock.

- **Triple-press PWR with the screen off or pocket-locked.** While a press
  sequence is pending the idle loop pauses 10 ms instead of 100 ms and does
  not light-sleep, so the key is polled at the normal 40 ms rate and slower
  triple presses are no longer merged into one latched edge.

- **1.75C PWR button.** Key2 is only on AXP2101 PWRON. GPIO3 (net SYS_OUT) does
  not follow it, so the triple-press detector never saw the key. A short press
  is now the PKEY short-press IRQ, polled and cleared over I2C. One short press
  toggles the screen; three within 350 ms still lock it. Holding PWR for 4 s
  is still the hardware power-off.

- **QMI8658 powers down.** The 1.75C IMU sits on VCC3V3 and this firmware does
  not read it. If the chip answers 0x05, CTRL7 is cleared (sensors off) and
  CTRL1 is written 0x03 to stop its high-speed oscillator, which is where the
  saving is. SensorLib's `powerDown()` sets CTRL1 bit 1 and earlier code here
  set bit 0; no datasheet was available, so both are set. Not measured.

- **AXP2101 DCDC5 off (1.75C only).** On the 1.75C schematic that converter is unconnected
  (no inductor, output capacitor not fitted). DCDC1 stays on: it is VCC3V3
  for the ESP32 and the panel.

- **CO5300 actually sleeps when the screen is off.** Writing brightness 0
  (reg `0x51`) leaves the panel scanning, so it stays lit. Screen-off and the
  1.75C lock blank now send `0x51=0`, display off `0x28`, wait 120 ms, and
  sleep-in `0x10`. Wake is sleep-out `0x11`, wait 120 ms, display on `0x29`,
  then brightness. DCDC1 is not switched off.

## [1.19] - 2026-11-11

### Added

- **Brightness setting** (#34). The clock/settings screen has a new button in
  the middle of the sound/language row: a sun and three bars. Tapping it cycles
  high (as before), medium (60 %) and low (30 %). The level scales every
  brightness the pet uses (awake, asleep, dimmed after a while untouched,
  low-battery caps) and is saved, so it survives a restart or update.

## [1.18] - 2026-10-27

Three contributions from **ViezeVingertjes** (PRs #25, #26 and #27), merged together.

### Added

- **Battery percentage from a LiPo discharge curve** (`batcurve.h`). On battery
  the figure comes from a typical single-cell curve over a smoothed voltage, and
  only goes down while discharging, so the charge-inflated voltage right after
  unplugging does not raise it. On USB or while charging the PMU's own gauge is
  still used.
- **Safe power-off.** When the smoothed voltage stays under 3.30 V for 30 s (and
  not in the first 30 s after boot) the pet is saved and the PMU powers off. The
  PMU is also set to cut at 3.1 V as a backstop. Assumes a 4.2 V LiPo/Li-ion
  cell, like the one the board is designed for.
- **Power policy** (`power.h`): the frame rate follows activity (10 fps in use,
  down to 1 fps when the screen is almost off, 2 fps while the pet sleeps
  untouched), static screens (keyboard, clock, card pages 2-4, starter choice)
  are painted once, the speaker amplifier is off while the pet sleeps or the
  screen is dimmed, and under 20 % battery the brightness is capped and frames
  are slower. About 68 % fewer frames reach the panel over 10 idle minutes.
- **Light sleep** with the screen off or almost off, on battery only. A touch
  wakes it. Never with USB connected, so the serial port for the SD card stays
  available.

### Changed

- Sprites are drawn as runs of equal pixels instead of one rectangle per pixel
  (same output, far fewer draw calls), and `loop()` yields 1 ms on passes that
  do not draw.
- The pet walks by real time, so a lower frame rate does not slow it down.
- Battery is read over I2C every 10 s instead of 2 s when the screen is off.

## [1.17] - 2026-09-18

### Added

- **Korean, the eighth language.** Full UI (85 strings), the three medal name
  lengths and the 151 gen-1 species names, on the same `STRINGS[LANG][StrId]`
  structure as every other language. The text measurement centralised in 1.15
  carries almost all of it; the four byte-based spots a UTF-8 row turned up are
  listed under Fixed.
- `u8g2_font_unifont_t_korean2` for Hangul, selected per language in
  `applyLangFont()` rather than through a single `CJK_FONT`. The Korean added
  uses 296 distinct syllables, all of them in KS X 1001, the 2350-syllable set
  that matches the size of `korean2`; `korean1` holds 478. Coverage was checked
  as membership in KS X 1001 rather than by walking the font table, and the
  ASCII in the strings is assumed present. Not yet checked on the board:
  whether `CJK_SIZE_DIV` suits `korean2` as it does the Japanese subset.

### Fixed

- **The release dialog could cut a Korean name mid-character.**
  `renderRelease()` built into `char q[28]`. `"%s 놓아줄까요?"` is 17 bytes
  before the name, so names of four syllables or more (34 of the 151) did not
  fit, and `snprintf` truncated inside a 3-byte sequence, leaving a tail the
  font cannot decode. Now 48; the longest case needs 33 bytes including the
  terminator.
- **Three places sized and positioned text with `strlen()`**, which counts
  bytes: the stat-card header, the species name under a nickname, and the
  gallery detail header. On a UTF-8 row the centring offset and the auto-shrink
  threshold worked off a byte count rather than a width. All three now go
  through `textW()` / `centerX()`. With the classic font `textW()` returns
  `strlen()*6*size`, the same expression as before, so the six Latin languages
  land on the same coordinate at the same size, checked at every length from 1
  to 25.
- **Three Japanese texts were cut short**, all already in 1.15 and 1.16.
  Their buffers were sized when every string was one byte per character, and
  `snprintf` truncates without a trace. The minigame record read `きろく 2`
  with a best of 219, since `char rec[12]` left room for one digit. The streak
  milestone banner never fit at all: `"%u にちれんぞく！"` passes 20 bytes with a
  single digit, and the cut landed mid-character. The streak line on the stat
  card lost digits from 100 days on. The release dialog fixed above was the
  fourth.
- **Every formatted text is now checked against its buffer, in all eight
  languages.** A new test in `test/test_tools.py` finds each
  `snprintf(buf, sizeof(buf), T(...))` in the sketch, reads the size of `buf`,
  and computes the worst case per language from the argument types (5 digits
  for `%u`/`%d`, 10 for `%lu`, and a declared maximum for each `%s`). It is what
  caught the three above, and it would have caught all four. To keep it passing
  by type rather than by typical value, four more buffers get headroom they had
  not yet needed: the medal count, the next-level line, the evolution countdown
  and the profile line.
- **`tools/test_i18n_formats.py` reads only the first `len(LANGS)` language
  blocks**, and `LANGS` listed six, so later rows were skipped without a
  warning. It now lists all eight, and the docstring notes that it needs to
  stay complete.
- **`test/test_i18n.cpp`'s `LANG_NAME[LANG_COUNT]` had six entries**, fewer
  than `LANG_COUNT`, so every entry past the sixth was null. Four of the tests
  build a label from `LANG_NAME[lang]` on every iteration, not only when a
  check fails, so each run passed a null pointer to `%s`. Now lists all eight.

## [1.16] - 2026-09-09

### Fixed

- **Japanese was silently dropping every `！` and `？`.** 21 exclamation marks
  and 4 question marks — a quarter of the Japanese strings, including the mood
  line on the main screen (`ごきげん！` rendered as `ごきげん`). The cause is that
  `u8g2_font_unifont_t_japanese1` carries kana and kanji but no CJK punctuation
  at all — no `！？。、「」` — and `Arduino_GFX` draws nothing for a glyph it can't
  find, without even advancing the cursor, so the loss is invisible in the
  source and on screen. Switched to `japanese3`, the only U8g2 Japanese font
  that has them. Same unifont family: the metrics header is byte-identical and
  all 226 codepoints the firmware uses have byte-identical bitmaps, so nothing
  moves. Costs 102 KB of flash (705 KB → 810 KB, 22% → 25% of the app
  partition); RAM is unchanged.
- **Nidoran♀ and Nidoran♂ both showed as ニドラン in Japanese**, leaving #29
  and #32 indistinguishable in the Pokédex. No U8g2 unifont subset carries
  ♀ (U+2640) or ♂ (U+2642) — not `japanese*`, `korean*` or `chinese*` — and
  `Arduino_GFX` draws nothing for a glyph it can't find, without even
  advancing the cursor. `gen_names.py` already substituted F and M on the
  Latin path; it now does the same on the UTF-8 one, so the names read
  ニドランF and ニドランM. Convention chosen by @usakomint, who preferred it
  over ニドラン(女)/(男) as closer to a species name and shorter on a small
  round screen.

Both confirmed on hardware by @usakomint, who also — without knowing it —
supplied the evidence for the first one: the missing `！` was visible in a
photo he posted of the v1.15 main screen.

## [1.15] - 2026-09-08

### Added

- **Japanese.** UI, medals and all 151 species names in katakana (フシギダネ,
  ピカチュウ). Selectable from the settings screen like any other language.

### Changed

- Text measurement and cursor positioning are now centralized (`textW()`,
  `centerX()`, `setCur()`, `setSize()`, `printT()`), replacing ~180 sites that
  assumed one byte per character and a fixed 6 px width. Latin languages render
  identically — the equivalence is arithmetic, not approximate. This is what
  makes CJK possible without a per-language branch at every draw site, and
  Chinese and Korean now only need their strings.
- Bar positions on the stat card and the care row are derived from the width of
  the widest translated label instead of fixed coordinates, so they stay aligned
  in any language. With Latin labels the numbers come out identical to before.
- New dependency: **U8g2**, for its CJK font data only.

Japanese font handling, readability and layout were all tuned against real
hardware by @usakomint, who found and diagnosed every issue in the process.

## [1.11] - 2026-09-07

### Fixed

- **The whole loop froze for exactly 1 second when touching the screen.**
  `touch.getPoint()` hung on a sleeping CST9217 for the ESP32 I2C driver's
  default 1000 ms timeout, which `Wire.setTimeOut(50)` doesn't bound because
  SensorLib doesn't route through it. Most visible in the ball minigame, where
  taps come fast and two or three stalls ran together — reported as "freezes for
  2–3 seconds on ball tap". An address-only Wire transaction before the read
  fixes it. Measured on hardware over equivalent play (~470 touch reads): 5
  stalls in 60 s before, 0 after. Reported by @gingsiro.

## [1.10] - 2026-09-07

### Fixed

- TPK2 sprites with zero-length frame durations spun `pmdFrameAt()` forever,
  hanging the render loop. These files arrive over USB, so they aren't trusted
  input: durations are clamped at load time and the loop is bounded regardless.
- `thumbs.bin` was mapped into PSRAM without bounding its size or checking that
  the offsets it contains point inside what was actually read. A truncated file
  (plausible — the transfer takes minutes) could read past the allocation.
- The serial `PUT` handler ignored `f.write()`'s return value, so a full SD card
  still reported `DONE` while leaving a truncated file behind. It also took the
  destination path straight from the serial line, so a crafted `PUT` could write
  anywhere on the card; writes are now confined to `/mons/`.
- Touches just outside the Pokédex grid and the on-screen keyboard were treated
  as cell 0. The guard divided first and then checked for a negative index, but
  integer division truncates toward zero, so it never saw one. On a round screen
  that strip is exactly where a finger lands near the edge.

All four from @ajtudela.

## [1.9] - 2026-09-07

### Fixed

- All ~13 game timers compared `millis()` against an absolute deadline, which
  gives the wrong answer once `millis()` wraps at ~49.7 days of uptime: a timer
  could stay active forever, or a ceremony cut off instantly.
- The sprite transfer restarted from scratch on every attempt. It now records
  each confirmed file, so a reload after a dropped connection doesn't redo the
  full ~40 MB / ~10 min.
- Minigame physics advanced per frame rather than per unit of time, so the ball
  fell measurably slower with a large sprite on screen (Charizard) than a small
  one (Diglett) — high scores weren't comparable between species.

### Security

- `esp-web-tools` was loaded from a floating `@10` range with no integrity
  check. Pinned to 10.4.0 with a verified sha384 SRI hash.

All from @ajtudela.

## [1.8] - 2026-09-07

### Fixed

- **The level counter wrapped to 0 after ~10.6 days of play.** `level()`
  returned `uint8_t` and, at 60 minutes per level, the level is the hour count —
  so it rolled over at 256. Reachable in ordinary play, since "stay together"
  postpones the farewell indefinitely. Now `uint16_t`, capped at 999.
- Two related truncations found while fixing it: `calcStat()` took the level as
  `uint8_t`, so stats would have overflowed anyway; and `canEvolveNow()` compared
  against `(uint8_t)(evolveLevel + careMistakes)`, which wrapped with many care
  mistakes and let evolution happen early. The stat card already computed that
  threshold as `int`, so the screen and the actual check could disagree.

Diagnosed by @ajtudela.

## [1.7] - 2026-09-07

### Fixed

- **The UI had no accents, and the assumption behind that was wrong.** The code
  stated in two places that the font had none. In fact `glcdfont.h` is a full
  256-glyph CP437 table and `write()` doesn't filter bytes ≥ 0x80. Spanish now
  has its tildes and ñ, German its umlauts, French its accents. Characters are
  written as single bytes in octal, never UTF-8, because the UI centres with
  `strlen(t) * 6`. Only strings whose length was unchanged were touched.
  Uppercase accented vowels other than É aren't in CP437, so `EVOLUCION` and
  `PROGRES` stay unaccented, as do Portuguese ã/õ.
- German grammar, from the same contributor: "Wähle deinen Starter" (accusative),
  "Leb wohl" as two words, "Entwicklung" rather than "Entwickelt", and more.

Discovered by @danielberndt.

## [1.6] - 2026-09-07

### Fixed

- **Short sound effects were inaudible.** The amplifier was switched on 8 ms
  before each effect, far less than the NS4150B needs to settle, so a 35 ms tap
  finished before the speaker woke up. The boot jingle survived because it's
  ~440 ms — which is why sound seemed to work at startup and nowhere else. The
  amplifier now stays powered while sound is on and the pet is awake, matching
  Waveshare's reference. Notes and amplitude are unchanged: this doesn't alter
  the sound design, it just makes it audible.

Diagnosed by @djyf1.

## [1.5] - 2026-08-19

### Added

- Localized Pokémon names in French and German (Bulbizarre, Bisasam…). Only
  those two differ in gen 1; Spanish, Italian and Portuguese officially use the
  English names. Names come from PokéAPI via `tools/gen_names.py`.

## [1.4] - 2026-08-07

### Fixed

- **Bond was mathematically stuck.** It could gain at most 12 points a day, but
  a single care mistake cost 3 and could repeat every 30 minutes — up to 144 a
  day. With poops draining hygiene, mistakes chained and bond never recovered.
  Two players independently reported it pinned at exactly 10, which is the
  first-day ceiling of 13 minus one mistake. Daily cap 8 → 20, penalty 3 → 1,
  cooldown 30 → 60 min.

## [1.3] - 2026-08-07

### Fixed

- **The egg hatched by itself during starter selection.** Game time kept running
  while the starter screen was open, so taking more than 3 minutes to choose
  meant the egg hatched into the species already rolled — no egg animation, and
  the player's choice ignored. Three community reports matched (Pikachu twice,
  Eevee once). Time now freezes until a starter is chosen.

## [1.2] - 2026-06

### Added

- Firmware version shown at the bottom of the clock/settings screen.
