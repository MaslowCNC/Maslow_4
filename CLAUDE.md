# Maslow 4 firmware + web UI

Firmware for Maslow 4 / 4.1 belt-driven CNC routers. It is a fork of FluidNC (ESP32-S3) with a Maslow
layer on top, plus the ESP3D-WEBUI browser UI that the machine serves itself.

## Layout

- `firmware/` is the PlatformIO project (env `wifi_s3` is the one that ships).
  - `FluidNC/src/Maslow/`: the Maslow code. `Maslow.cpp` (update loop, safety checks, telemetry),
    `Calibration.cpp` (state machine + anchor solver), `MotorUnit`/`DCMotor`/`AS5600` (arm hardware).
  - `FluidNC/src/Kinematics/MaslowKinematics.cpp`: belt-length math, anchor geometry config.
  - `FluidNC/src/ProcessSettings.cpp`: where `$` commands are registered (Maslow ones near the end).
  - `FluidNC/data/`: the default filesystem (`maslow.yaml`, `index.html.gz`).
  - `FluidNC/test/`: googletest unit tests (`native` env).
- `ESP3D-WEBUI/www/js/` holds the web UI (plain JS, built with gulp). Maslow logic is in `maslow.js`, `tablet.js`, `debugging.js`.
- `docs/` holds the GitHub Pages site, including `calibration-simulation/` (the JS copy of the calibration math).
- `scripts/` holds `flash-maslow.sh` (build + flash), `maslow_serial.py` (talk to a USB-connected machine),
  `report_monitor.mjs` (measure status-report size and drops over WiFi), and release tooling.
- `CAD/`, `assets/`: hardware files and docs images.

## Build and test

PlatformIO may live in a venv: use `pio` if it's on PATH, else `~/.platformio-venv/bin/pio`.

```bash
scripts/flash-maslow.sh build            # firmware only
scripts/flash-maslow.sh build --fs       # + web UI and filesystem image
# firmware unit tests (pio test -e native) currently do not build on Linux: missing Queue.h in X86TestSupport
cd ESP3D-WEBUI && npm ci && npm run build:en   # web UI -> dist/index.html.gz
cd ESP3D-WEBUI && bun test               # web UI tests (www/js/tests, needs bun)
cd ESP3D-WEBUI && npm run dev            # UI at http://localhost:8484, proxied to maslow.local; rebuilds on refresh
```

- Build through `scripts/flash-maslow.sh`, or with `PLATFORMIO_DATA_DIR=firmware/.pio/fsdata`. Changing
  that variable between builds makes PlatformIO wipe `.pio/build`.
- `platform-espressif32` is pinned on purpose in `platformio.ini` (newer GCC breaks the build). Don't unpin casually.
- The committed `firmware/FluidNC/data/index.html.gz` can be older than the UI source. Releases rebuild it.

## Flashing hardware

Flashing is done only through the `/flash-maslow` skill, and only when the user runs it. Never flash,
erase, or upload a filesystem as a side effect of other work. Likewise, don't send motion or
calibration commands (`$ALL`, `$EXT`, `$CAL`, `$TKSLK`, `$TLI`/`$TLO`/…, jogs, G-code) to a
connected machine unless the user asked for that specific action. Read-only queries (`$SS`, `$I`,
`$MINFO`, `$GSTATE`, `$CD`) are fine.

## Firmware ↔ UI contract

The architecture rules (UI actions only through `machine.js`, state from the status report,
reuse existing commands, no Pause, every connection equal) are in `.github/copilot-instructions.md`, under "Maslow
architecture rules". They apply to every agent. Follow them.

- The UI talks to the firmware with `$` commands over WebSocket `/ws`, one at a time through
  `wscommands.js`. Only `[ESP…]` commands and file transfers use HTTP.
- The firmware answers with GRBL status reports (about every 50 ms), `MINFO: {json}`, and plain
  log lines. New state goes in a status report field, not `MINFO` or a log line
  (`docs/protocol.md` lists the fields). Report size matters: a PR that adds data to the regular
  report needs review from tristan-huber or BarbourSmith on GitHub before merge, and its
  description should @-mention @tristan-huber.
- **The UI recognises many log lines by their exact text** (see `maslowInfoMsgHandling` in `maslow.js`
  and the parsing in `debugging.js`). Before changing the text of any `log_info`/`log_warn`/`log_error`,
  grep `ESP3D-WEBUI/www/js` for it and update both sides in the same change.
- Calibration state numbers (`MaslowEnums.h`, 0–10) are mirrored in the UI, which range-checks them.
  Adding a state means updating the UI too.

## Style

- C++: `.clang-format` at the repo root is authoritative (see `firmware/CodingStyle.md`). Use `#pragma once`.
  Format only the lines you touch, so diffs against upstream FluidNC stay small.
- Maslow source files carry a GPLv3 header with a MakerMade exception. Copy it into new Maslow files.
- JS: match the surrounding file (tabs in `maslow.js`, ES6 `const`/arrow functions).

## Git and CI

- PRs target `Maslow-Main`. `origin` is MaslowCNC/Maslow_4. **`upstream` is bdring/FluidNC: never push to it.**
- CI doesn't build PRs automatically. A PR comment containing `/build` or `please build` triggers
  `compile-on-review.yml`. `release.yml` builds releases and mirrors assets into `web-assets/`.
- Bots assign Copilot to issues and relay review comments to it, so expect automated PRs.
- The USB install scripts in the release zips are Maslow's own, in `scripts/release/install/`.
  `firmware/install_scripts/` is upstream FluidNC's (wrong chip or flash offsets for this board):
  leave it as upstream has it and don't use it.

## Releases

1. Tag the commit `vX.Y.Z-exp.1` (annotated: `git tag -a`) and push the tag. `release.yml` builds
   it. If it needs fixes, tag the fix commit `-exp.2`, `-exp.3`, and so on. Keep the dot: tags
   follow semver exactly, and semver only compares the number as a number when it is its own
   dot-separated part (`exp.10` > `exp.2`, but `exp10` < `exp2`).
2. Share it in the
   [Interstitial Firmware Releases](https://forums.maslowcnc.com/t/interstitial-firmware-releases/20215)
   forum thread for testing.
3. Once it's judged stable, add a second annotated tag `vX.Y.Z` to the same commit as the last
   `-exp.N` tag and push it. Don't move or delete tags, and don't make a no-op commit: the stable
   release is the exact code that was tested. `release.yml` rebuilds it, so the firmware reports
   `vX.Y.Z`. `git describe` picks the newest annotated tag when two share a commit.
- The web UI's update check (`checkupdatesdlg.js`, Preferences > Update stream) picks a release by
  GitHub's pre-release flag: Release takes the newest full release, Experimental the newest
  pre-release, Nightly the release tagged `nightly`. `release.yml` sets the flag from the tag: any
  `-` suffix makes a pre-release (never "Latest"), a bare `vX.Y.Z` the latest full release. Whether
  the picked release is an update is decided by strict semver
  (`v1.28.0-exp.1` < `-exp.2` < `-exp.10` < `v1.28.0`).
