# Area: menu launcher

The Kindle boots into a five-button launcher (`src/menu.c`) instead of the
Amazon UI. Same e-ink engine as the games, same takeover rules.

## How it works

- `src/menu.c` — buttons: XCOM / CHESS / TETRIS / WIFI / KINDLE. On tap it
  draws a "starting..." card, releases the panel + touch grab (`ek_close()`),
  and `_exit(rc)` with `1=XCOM 2=CHESS 3=TETRIS 4=WIFI`. KINDLE/signal → 0
  (restore the UI). Host build reads `tap X Y` from stdin and `--dump` writes
  a PGM, so `make menu-test` walks every button on the PC.
- `tools/menu-boot.sh` — the always-on session, run by the upstart job. Stops
  the screen mirror (kfbterm) so the touch grab wins, waits ~20 s for wi-fi,
  runs `/mnt/us/menu`, then launches the chosen game via its runner; loops back
  to the menu when the game exits. KINDLE → `restore-kindle.sh` and exit.
- `tools/menu-boot.conf` — installed as `/etc/upstart/menu.conf`. `start on
  started lab126_gui`, `stop on runlevel [06]` (one line only, so stopping
  `x`/`lab126_gui` mid-session never kills the menu job). Replaces the old
  `chess.conf` job.
- `install-menu-on-kindle.sh` — builds menu/chess/tetris/xcom + grabtest,
  pushes binaries, runners, wifi script, chess.conf, installs
  `/etc/upstart/menu.conf` (removing `chess.conf`), and starts the session
  immediately via `setsid sh menu-boot.sh &`.

## Key gotchas

- The menu must NOT hold the touch grab when a game starts: the game's own
  `ek_open(TAKEOVER)` would fail with EBUSY. So the menu exits before the game
  runs; the grab dies with the process.
- `kfbterm` owns `/dev/input/event3` (EVIOCGRAB) on `started lab126_gui`;
  menu-boot.sh stops it first, and re-checks with `grabtest`.
- WIFI button reuses chess's proven wi-fi screens: `chess --wifi-only`
  (added for the menu; same screens, no board, hands the panel back).
- `ek_close()` → `release_ui()` → `initctl start framework`, so after WIFI or
  KINDLE the reader UI is usable again. The boot job survives the re-emitted
  `started lab126_gui` because the job is already running.

## Status

Not yet installed on a real Kindle (installed + reboot-tested on next session).
Host tests pass: `make menu-test` walks all five buttons and the dump is sane.
