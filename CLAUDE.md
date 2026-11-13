# TamaPoke: working notes for Claude

This is a fork of [socquique/TamaPoke](https://github.com/socquique/TamaPoke)
for one device: a **Waveshare ESP32-S3-Touch-AMOLED-1.75C** (aluminium case,
32 MB flash, no microSD, no RTC chip, 500 mAh 3.7 V LiPo). It is a gift: the
owner's wife looks after the pet. See `PORTING.md` for the 1.75C details.

**In scope:** tamagotchi-style care features, app and power/hardware
optimizations, tooling for this device. **Out of scope:** battles, gyms,
trading, anything multiplayer.

## Hard rules

- **No external comms, ever.** Do not add Wi-Fi, Bluetooth/BLE, ESP-NOW,
  HTTP(S), NTP, MQTT, network OTA, or any cloud/API call to the firmware, and
  do not pull them in from a fork. USB serial from the owner's own computer is
  the only channel. A new `WiFi.h`, `esp_wifi*`, `BLE*`/`NimBLE*`, `esp_now*`,
  `HTTPClient`, `WiFiClient*`, `AsyncTCP` or similar is a blocking review
  finding.
- **The pet's save is sacred.** NVS namespace `tamapoke` holds it. New keys
  are fine; changing what an existing key means needs a migration and a test.
  Values loaded from NVS must be range-checked before use (`Pet::load()`).
- **Both boards must build.** The default build is the original 1.75; ours is
  the 1.75C (`-DBOARD_AMOLED_175C=1`). Keep board-specific code behind
  `#if BOARD_AMOLED_175C`, and compile both before a PR.
- **App-only updates keep the pet.** Write only `app0` (`0x10000`). Never
  write `nvs` (`0x9000`) or `storage` (`0x310000`, the sprites) unless the
  task is explicitly about them.
- **The board is the owner's.** Flash it, set its clock, or send `WIPE`/`BYE`/
  `RUN`/`SPEC`/`LVL` over serial only when asked to in this session.
- **Python:** pip installs go in `.venv/` only, never system-wide.

## Gate (run before every PR; keep output short)

Print summaries plus every error and warning; never drop a warning or a
skipped-test count, and read any failure in full.

```bash
./test/run_tests.sh --asan 2>&1 | tail -6          # host logic + ASan/UBSan + tools tests
python3 tools/test_i18n_formats.py | tail -1        # string tables, all 8 languages
tools/build_175c.sh --out ./out 2>&1 | grep -E 'error|warning: .*TamaPoke/|Sketch uses|build OK'
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB" . 2>&1 | grep -E 'error|Sketch uses'
```

`ruff check tools/` runs in CI (install it in `.venv/` to run it locally).

If a change affects device behaviour, flash it (app-only) and check it over
USB serial (`HEALTH`, `STATS`, the 60 s `HEALTH` heartbeat) — or say plainly
in the PR that it was not exercised on hardware. Host tests and CI do not
prove anything about the panel, touch, PMU or battery.

A new regression test must be shown to **fail without the fix**: say in the PR
what you reverted and what the test reported. One mutation per behaviour, and
it must target the line the test names.

## Pull requests: two independent reviews

Every PR, with no size threshold, gets **exactly two** independent review
rounds before it is merged. Both are mandatory, and two is also the cap.

1. **Round 1.** Dispatch a fresh `pr-reviewer` agent (`.claude/agents/`) on
   the PR. It works read-only from the diff and the repo, reports back to the
   main session, and posts nothing to GitHub.
2. **Round-1 fixes** go to a fresh `issue-fixer` agent (never the agent that
   wrote the code), briefed with the PR, each finding's file and line, and the
   reviewer's evidence. It pushes to the same branch.
3. **Round 2.** Dispatch another fresh `pr-reviewer` on the fixed state, even
   if round 1 found nothing.
4. **After round 2** no more review agents. The main session fixes any
   HIGH/MEDIUM findings itself (a large set can go to one fresh
   `issue-fixer`, whose diff the main session then reviews), re-runs the gate,
   and fixes or explicitly dispositions LOW ones. Nothing gets silently dropped.

Your own verification supplements the reviews; it never replaces them. A
reader with no memory of writing the code catches the claims a change makes
about itself (a comment asserting a guard that does not hold, a test that
passes either way); the author does not.

Every agent brief, including review briefs, says: keep command output short,
and batch mutation checks in one script that applies each mutation, runs only
its test, records the result and restores the file.

**Merging** needs all three: the work is verified (gate, plus hardware when
relevant), both review rounds are done with every finding fixed or
dispositioned, and the owner has approved *that* PR in chat. Approval is
per-PR and never standing. Agents never merge their own PRs.

PR descriptions say what changed, why, how it was tested (including what was
not tested on hardware), and list anything fixed beyond the brief under
**Also fixed**.
