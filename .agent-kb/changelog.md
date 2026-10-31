# Changelog

Newest first. One line per task. Current truth is in `areas/`, not here.

- 2026-10-31 — chess — Boot progress bar fixed: the splash process `blanket` (a child of the `x` job) repaints its splash *and* the boot bar over whoever holds the panel, and it survives a stopped framework, so a half-drawn bar stuck on top of the board. `killall blanket` is the wrong cure (lxinit dies abnormally → `x_monitor` restarts the GUI 5× then reboots into repair), so `src/chess.c` now `initctl stop x` after takeover — a clean stop leaves `x_monitor` quiet and `restore-kindle.sh` brings X back on exit — plus one forced full-panel white flush before the first paint. Also: the real boot splash is `/opt/amazon/low_level_screens/600x800/splash.png`, not the `/var/local` copy; the aijly logo is installed there (stock at `/mnt/us/splash-amazon-orig.png`). Verified on two reboots; restore verified twice.
- 2026-10-31 — chess — Gift-unit boot now verified on a real reboot: `tools/chess-boot.conf` waits up to 180 s for wi-fi then launches anyway, and the job no longer dies on the first failing `chess-wifi.sh` call (upstart runs script blocks with `sh -e`; `set +e` + guarded retry). Boot log grew uptime. Replaced the Kindle's start-up picture with the aijly logo via new `tools/make_splash.py` (600x800 greyscale PNG, written over the `/var/local` symlink, no root-fs write). README gained the splash swap and the 3-minute wait.
- 2026-10-31 — chess — Gift-unit standalone: in-game wi-fi setup (scan, drawn keyboard, remembered network in `/mnt/us/wifi.conf` via `tools/chess-wifi.sh`), fixed the busybox `awk -F'\t'` scan bug and a join that "succeeded" without switching networks, `--wifi-shot` per-screen renders, `kill -USR1` screenshot of a running game, and `tools/chess-boot.conf` to boot straight into the game; README rewritten around Quick Start. Boot job still to be installed and reboot-tested.

- 2026-10-31 — chess — Chess against DeepSeek on the Kindle: full rule set with perft/kiwipete self-tests, tap-to-move board, one API question per move with a retry and a visible failure, `make chess-kindle` and `run-chess-on-kindle.sh`; `tools/kindle-run.sh` became game-generic. Waiting on a real API key.

2026-10-31 — xcom — Added MANUAL.md and make_manual_audio.py for TTS listening

- 2026-10-31 — xcom — The Kindle now talks: log lines are spoken word by word from 91 baked clips with shot/hit/miss/down/turn/win/lose effects; taps use the fast waveform instead of a full GC16 clean; `--auto` quits on the device; `make sound` bakes clips and `make test` fails if a word has none.
- 2026-10-31 — xcom — Made it actually run on the Kindle: on-device launcher frees the touch device, restores the UI, takes any Kindle IP; README rebuilt around Quick Start.
- 2026-10-30 — xcom — Added the one-mission Kindle fight and the sprite cutter.
