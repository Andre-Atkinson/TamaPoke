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
  the only channel. `tools/gate.sh` enforces this over the firmware sources
  (`*.ino`, `*.cpp`, `*.h` at the repo root): it fails on includes of
  `WiFi*`, `HTTPClient`, `HTTPUpdate`, `WebServer`, `ESPAsyncWebServer`,
  `AsyncTCP`, `ESPmDNS`, `ArduinoOTA`, `PubSubClient`, `BluetoothSerial`,
  `BLE*`, `NimBLE*`, `esp_now`, `esp_gap_ble`, `esp_gatt`, `esp_http_client`,
  `esp_https_ota`, `esp_sntp`, `esp_netif`, `esp_eth`, `lwip/`, `sntp`, and on
  calls such as `configTime(`, `sntp_*`, `esp_now_*`,
  `esp_wifi_(init|start|connect)`, `esp_bt_controller_(init|enable)`,
  `WiFi.begin`, `WiFi.softAP`. Any hit is a blocking review finding.
  *Carve-out:* calls that only stop or release a radio that is never started
  (`esp_wifi_stop`/`esp_wifi_deinit`, `WiFi.mode(WIFI_OFF)`, `btStop()`,
  `esp_bt_controller_mem_release`) are allowed when the PR justifies them (for
  example power saving); the check does not flag them.
- **The pet's save is sacred.** NVS namespace `tamapoke` holds it. New keys
  are fine; changing what an existing key means needs a migration and a test.
  Values loaded from NVS must be range-checked before use, in `Pet::load()`
  and every other reader of the namespace.
- **Both boards must build.** The default build is the original 1.75; ours is
  the 1.75C (`-DBOARD_AMOLED_175C=1`). Keep board-specific code behind
  `#if BOARD_AMOLED_175C`, and compile both before a PR.
- **App-only updates keep the pet.** Write only `app0` (`0x10000`). Never
  write `nvs` (`0x9000`) or `storage` (`0x310000`, the sprites) unless the
  task is explicitly about them. `tools/build_175c.sh` caps the image at the
  `app0` size (3,145,728 bytes), so an oversize build fails instead of
  spilling into the sprite partition.
- **The board is the owner's.** Unless the owner asks in this session, don't
  flash it (`esptool`, `tools/flash_175c.sh`, `arduino-cli upload`), don't
  open its serial port, and don't send it anything. Each state-changing
  command needs its own request: `WIPE`, `BYE`, `RUN`, `HATCH`, `SPEC`,
  `LVL`, `NICK`, `SHINY`, `CAREDAY`, `ABANDON`, `TIME <epoch>`, `RTCSET`,
  `GAL`, `BEEP`, `TRAINTRACE ON|OFF`, `PUT` (`tools/send_sd.py`). Read-only:
  `HEALTH`, `STATS`, `REG`, `EGGS`, `TIME`, `LS`, `SDINFO`, `LOCK?`,
  `TRAINTRACE?`. (From `handleSerial` in `TamaPoke.ino` and `sdSerialCommand`
  in `sdmon.cpp`; re-check when those change.)
- **Python:** pip installs go in `.venv/` only, never system-wide.
- **Processes and history:** stop only processes and worktrees you started,
  never another session's. Never rewrite pushed history. Always pass
  `--repo Andre-Atkinson/TamaPoke` to `gh` (never the upstream).

## Gate (run before every PR; keep output short)

```bash
tools/gate.sh              # everything; real exit codes, one summary line per step
tools/gate.sh --no-build   # quick runs: skips the two firmware builds
```

It runs: `test/run_tests.sh --asan` (host logic + ASan/UBSan + tools tests),
`tools/test_i18n_formats.py`, the no-network check, and both firmware builds
(1.75C via `tools/build_175c.sh`, original 1.75 via `arduino-cli compile
--warnings=all`) with `--warnings=all`. The builds run from a staged copy of
the working tree in a folder named `TamaPoke`, because arduino-cli rejects a
sketch whose folder name differs from the `.ino` (every agent worktree). Never
run the builds by hand through a `grep` pipe: it hides failures and the exit
code becomes grep's.

Never drop a warning or a skipped-test count, and read any failure in full.
`main` currently has about 20 known sketch warnings per build (the gate prints
the count); only new ones are findings. `ruff check tools/` and a
generated-headers check (needs Pillow) run in CI but not in the gate: ask the
owner before installing ruff (even into `.venv/`).

If a change affects device behaviour, say plainly in the PR whether it was
exercised on hardware. Only flash it (app-only) and check it over USB serial
(`HEALTH`, `STATS`, the periodic `HEALTH` heartbeat) when the owner has asked
for that in this session. Host tests and CI do not prove anything about the
panel, touch, PMU or battery. For hardware claims (registers, pins, timings,
charger limits) the library sources the firmware builds against are in
`~/Library/Arduino15/packages/esp32/` (macOS) or `~/.arduino15` (Linux), and
`~/Documents/Arduino/libraries/`.

A new or changed regression test must be shown to **fail without the fix**:
say in the PR what you reverted and what the test reported. One mutation per
behaviour, and it must target the line the test names. Mutation rules:

- Commit before running a mutation script, so `git status` proves the restore.
- A mutation that fails to compile proves nothing: record it separately. A
  build failure makes the runner print `HAY FALLOS`, which looks like a caught
  mutation.
- Record each failing test's name and its first failing assertion.
- A test you change needs the same evidence as one you add.
- Header-only code (`power.h`, `batcurve.h`, `blit.h`) is covered too: the test
  Makefile depends on every repo-root header.

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
per-PR and never standing. Once all three hold, the main session may merge.
Subagents never merge, and nobody submits an approving review.

Mutation evidence for round-1 fixes goes in the PR body (`gh pr edit`), not
only in a comment. If two review rounds feels wrong for a change (too few or
too many), say so and ask the owner rather than exceeding the cap.

PR descriptions say what changed, why, how it was tested (including what was
not tested on hardware), and list anything fixed beyond the brief under
**Also fixed**. A fixer may fix something beyond the brief only when it is in
the same area as the PR, needs no owner decision, adds no new contract or
migration, touches no file another open PR owns, and comes with its own
regression test and mutation evidence. Anything bigger is reported to the main
session, which records it; findings are never silently dropped.
