# Area: menu launcher

The Kindle boots into an eight-button launcher (`src/menu.c`) instead of the
Amazon UI. Same e-ink engine as the games, same takeover rules.

## How it works

- `src/menu.c` — buttons: XCOM / CHESS / TETRIS / SUDOKU / ROCKMAN / KOREADER /
  WIFI / KINDLE. Row height 72, six game rows at y 170..594, then WIFI 600,
  KINDLE 660. On tap it draws a "starting..." card, releases the panel + touch
  grab (`ek_close()`), and `_exit(rc)` with `1=XCOM 2=CHESS 3=TETRIS 4=SUDOKU
  5=ROCKMAN 6=KOREADER 7=WIFI`. KINDLE/signal → 0 (restore the UI). Host build reads
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
- `run-menu-on-kindle.sh` — the iteration path: pushes fresh `menu` +
  `menu-boot.sh` + `grabtest` (write-then-`mv`: a running menu-boot.sh keeps its
  old inode, and copying over a running binary is "Text file busy"), stops the
  session job if any, TERMs leftover `menu`/`menu-boot.sh`, starts the session
  with `setsid`, confirms `pidof menu` within 20 s (and fails loudly on the
  `.menu-stop` guard exits instead of the old false "game exited" success), then
  follows `menu-boot.log`. Never touches the boot job → after a reboot a device
  without menu.conf shows the stock reader again.
- `run-koreader-on-kindle.sh` — standalone KOReader: pushes the runner as
  `koreader-run.sh`, runs `GAME=koreader` (new mode in `tools/kindle-run.sh`:
  runs `/mnt/us/koreader/koreader.sh` WITHOUT `--framework_stop`, same as the
  menu session; the leftover-kill list now covers `luajit koreader koreader.sh`)
  and follows the log until KOReader exits. Does not replace the install step.
- `install-koreader-on-kindle.sh` — pushes the staged KOReader zip
  (`jailbreak-kindle/kindle-tools/koreader-kindle-v2026.07.1.zip`, md5
  `af87904b4b47007fecfe907bbd15fe05`) and unzips at `/mnt/us` (the zip's top
  level is `koreader/` + `extensions/koreader/`, so unzip at the userstore root
  IS the install). KOReader shows in KUAL *and* as the menu's KOREADER button.

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
- KOREADER (rc 6) in menu-boot.sh runs `/mnt/us/koreader/koreader.sh` **without**
  `--framework_stop` — the framework is already stopped by the menu session, and
  koreader.sh's `STOP_FRAMEWORK=no` path just works over a dead UI. When KOReader
  exits, the panel stays dead until menu-boot re-takes it (next loop), which is
  fine.
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
  consumes it and the log shows which game launched. The Kindle has no
  `base64`/`scp`/`od` — pull `/dev/fb0` with
  `hexdump -v -e '1/1 "%02x\n"'` and reconstruct on the PC (600x800 8bpp,
  stride 608); the device also drops off wifi when the framework is down
  (ssh timeouts), and comes back by itself.
- `ek_close()` → `release_ui()` → `initctl start framework`, so after WIFI or
  KINDLE the reader UI is usable again. The boot job survives the re-emitted
  `started lab126_gui` because the job is already running.

## Status

Installed and running: 192.168.0.66 boots into the launcher (the `menu` job is
`start/running`, framework stopped, `pidof menu` alive), the standalone Rockman
runner stops and restarts that session correctly, and KOReader is installed
with the KOREADER button live — panel verified 2026-11-02: hexdump of `/dev/fb0`
shows all eight bands (y 170/242/314/386/458/530 games + 600 wifi + 660 kindle,
ROW_H 72). Host tests pass: `make menu-test` walks all eight buttons and the
dump is sane.

## Device state 2026-11-29 (the file-browser crash hunt)

- User reported: file browser (KOReader) crashes, restarting the Kindle. Reboot
  cause seen for 07:39:47 was `reset=user_reboot`.
- `.63` unreachable this whole session from EVERY vantage: WSL ssh timeout x15,
  Windows ping/22/9000/9200 dead, ARP entry `e0:cb:1d:5f:02:cd` stale and never
  refreshes, `.66` can't ping it (100% loss, empty ARP table), peer-net
  `10.30.0.205` dead, new `.69` is not a Kindle.
- `.66` reachable the whole time (busybox 2013 box, no `iw`/`head -c`, no
  sshd on it at all — we only ever SSH *into* .66, it can't SSH out; its
  `arp -n` shows nothing = thinks LAN is empty).
- kfb tunnel is DOWN: no cloudflared process on the Windows side, `kfb.aijly.com`
  returns 502. So no out-of-band screen/daemon path while diagnosing.
- Takeaway: when the framework is down (menu-game/KOReader session), the
  Kindle's wlan can go silent and *not* rejoin the AP until reboot. If `.63`
  looks dead, first suspect a wedged wlan0, not the app: check router
  DHCP table / WPS the unit back, or power-cycle it.

## Crash hunt findings (same day, later session)

- **The `.63`/`.66` pair got shuffled between sessions: the "crash unit" was at
  `.63` earlier and is now at `.66`.** The KINDLE button hands the panel back to
  the reader UI, so the device that held the crashed KOReader session is just
  the one that answers now. Diagnose by *content* (crash.log, menu-boot.log),
  not by IP.
- **The file-browser crash is a native hard kill with no traceback.** KOReader's
  `/mnt/us/koreader/crash.log` (v2026.07.1, `reader.lua`) ends abruptly at
  07:34:37 mid-startup after "Restoring user input handling" — no Lua error, no
  clean exit, no `[ko-input]` shutdown lines. dmesg shows the reboot cause
  `reset=user_reboot` (Amazon marks every soft reboot that way; it does not say
  who asked). No core: `ulimit -c` hard limit is 0 (cannot raise), and
  `core_pattern` `/mnt/us/menu-core.%p` yields nothing. Conclusion: KOReader
  died, then something rebooted — likely the device watchdog or a powerd/
  lab126 path.
- **The evidence was being destroyed by the launcher itself.** `menu-boot.sh`
  does `: > "$LOG"` (truncate) at boot, so the crashed session's last lines +
  the heartbeat vanished with the reboot. Fixed in `tools/menu-boot.sh`:
  rotate to `menu-boot.log.prev` / `menu-heartbeat.prev` before truncating, and
  write `[[koreader.sh started/exited rc=N at ...]]` markers into the
  append-only `/mnt/us/koreader-launch.log`. Deployed; takes effect next boot.
  Next crash leaves the smoking-gun lines + exact exit rc.
- **Deploy gotcha (fresh): the Kindle has no `base64` either** — the b64-pipe
  trick used for other devices silently no-ops and the `mv` then overwrites the
  launcher with the 22-byte "base64: not found" error. Restore went through as
  a raw stdin pipe (`cat file | ssh root@ip 'cat > /mnt/us/menu-boot.sh.new &&
  chmod 755 ... && mv ...'`) — keep `wc -c` + md5 + `sh -n` checks on the
  device side; the device md5 now matches the repo (b53529b...).
