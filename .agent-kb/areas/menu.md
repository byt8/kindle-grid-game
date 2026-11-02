# Area: menu launcher

The Kindle boots into a seven-button launcher (`src/menu.c`) instead of the
Amazon UI. Same e-ink engine as the games, same takeover rules.

## How it works

- `src/menu.c` — buttons: XCOM / CHESS / TETRIS / SUDOKU / ROCKMAN / WIFI / KINDLE.
  On tap it draws a "starting..." card, releases the panel + touch grab
  (`ek_close()`), and `_exit(rc)` with `1=XCOM 2=CHESS 3=TETRIS 4=SUDOKU
  5=ROCKMAN 6=WIFI`. KINDLE/signal → 0 (restore the UI). Host build reads
  `tap X Y` from stdin and `--dump` writes a PGM, so `make menu-test` walks
  every button on the PC.
- `tools/menu-boot.sh` — the always-on session, run by the upstart job. Stops
  the screen mirror (kfbterm) so the touch grab wins, waits ~20 s for wi-fi,
  runs `/mnt/us/menu`, then launches the chosen game via its runner; loops back
  to the menu when the game exits. KINDLE → `restore-kindle.sh` and exit.
- `tools/menu-boot.conf` — installed as `/etc/upstart/menu.conf`. `start on
  started lab126_gui`, `stop on runlevel [06]` (one line only, so stopping
  `x`/`lab126_gui` mid-session never kills the menu job). Replaces the old
  `chess.conf` job.
- `install-menu-on-kindle.sh` — builds menu/chess/tetris/sudoku/rockman/xcom + grabtest,
  pushes binaries, runners, wifi script, chess.conf, installs
  `/etc/upstart/menu.conf` (removing `chess.conf`), and starts the session
  immediately via `setsid sh menu-boot.sh &`.

## Key gotchas

- The menu must NOT hold the touch grab when a game starts: the game's own
  `ek_open(TAKEOVER)` would fail with EBUSY. So the menu exits before the game
  runs; the grab dies with the process.
- `kfbterm` owns `/dev/input/event3` (EVIOCGRAB) on `started lab126_gui`;
  menu-boot.sh stops it first, and re-checks with `grabtest`.
- **The standalone runners used to collide with this session.** `./run-*-on-kindle.sh`
  from a PC while the launcher was showing its menu got `grabtest: EVIOCGRAB
  refused: Resource busy` (the `menu` process holds the grab), died before
  drawing, and the host watcher still printed "the game has exited; the Kindle
  reader is back" — a false success, and the "needs a reboot" line from the
  restore was noise because the reader is *meant* to be down here. Fixed in
  `tools/kindle-run.sh` (used by all five games): before the grab check, a live
  `pidof menu` means someone else's session holds the panel → `initctl stop menu`
  (plus a TERM to the menu process if the job's stop leaves it), and `restore()`
  then does `initctl start menu` instead of the stock-reader restore. A live
  `menu` process can only be the menu phase: when the session launches a game
  itself the menu has already exited, so the stop never kills our own parent.
  `run-rockman-on-kindle.sh` now prints the runner's log and exits 1 when the
  game never started.
- WIFI button reuses chess's proven wi-fi screens: `chess --wifi-only`
  (added for the menu; same screens, no board, hands the panel back).
- **`menu-boot.sh` is deployed, not run from the repo — it goes stale.** The
  `case $rc` dispatch lives in the *deployed* copy (`/mnt/us/menu-boot.sh`),
  so a repo edit does nothing until re-pushed (and `install-menu-on-kindle.sh`
  pushes it only when re-run). Symptom of staleness: a new button's exit code
  lands on an old arm — ROCKMAN (rc 5) launched WIFI. Check with `md5sum`
  local vs `/mnt/us/menu-boot.sh`. Push with write-then-`mv` (a running shell
  keeps the old inode, so the rename avoids corrupting it), then restart the
  job: `initctl stop menu`, TERM the leftover `menu`/`menu-boot.sh`, `initctl
  start menu`. Verify remotely by piping a raw `input_event` blob (ARM32
  `<iihhi>`: X `type3 code0x35`, Y `code0x36`, `BTN_TOUCH 0x14a`, SYN) into
  `/dev/input/event3` — raw = screen × 4095 / (W-1); the menu's own grab
  consumes it and the log shows which game launched.
- `ek_close()` → `release_ui()` → `initctl start framework`, so after WIFI or
  KINDLE the reader UI is usable again. The boot job survives the re-emitted
  `started lab126_gui` because the job is already running.

## Status

Installed and running: 192.168.0.66 boots into the launcher (the `menu` job is
`start/running`, framework stopped, `pidof menu` alive), and the standalone
Rockman runner now stops and restarts that session correctly — verified on the
panel 2026-11-02 (grabtest free, game reached its title screen). Host tests
pass: `make menu-test` walks all seven buttons and the dump is sane.
