# Deploying everything to a Kindle

## The entry point
```bash
./deploy.sh                 # asks for the IP, checks it, builds + pushes everything
./deploy.sh 192.168.0.63
./deploy.sh 192.168.0.63 --no-build --no-koreader
```
`deploy.sh` is a thin front door that validates the address and then `exec`s
`install-all-on-kindle.sh <ip> "$@"`. **`install-all-on-kindle.sh` is the script
that does the work** — edit it, not the wrapper, when changing what gets shipped.

What it deploys: `menu chess tetris sudoku menu calib brogue-kapp` binaries,
`/mnt/us/<game>-run.sh` for 10 names, `menu-boot.sh/.conf`, the fbdaemon set
(`restore-kindle.sh kfb-watchdog.sh start-kfb.sh kfb`), `chess.conf`,
`chess-ca.pem`, xcom word clips, `/etc/upstart/menu.conf` +
`/etc/upstart/kfbterm.conf`, then starts the session. KOReader unless
`--no-koreader`.

## Exit codes (deliberate)
- `0` — everything pushed **and** the session is on the panel
- `1` — nothing sent: bad address / missing key / build failure
- `2` — everything installed but the panel did not start; log is printed

## The constraint that shapes the script: wifi naps
[verified 2026-11-05 .63] These Kindles drop wifi for 10–30 s at a time. A naive
`cat file | ssh 'cat > ...'` dies mid-push and leaves **half the binaries new and
half old** (this happened: menu/mines/sudoku landed, runners did not). So:
- `ssh_try` retries every ssh call (1,2,4…20 s, ten tries).
- `push_file` re-sends the whole stream and verifies `wc -c` against the source
  before renaming `$dst.new` → `$dst`. A half-written file never survives.
- `ssh_once` exists for **polling** only — where "no" is a real answer.
- The xcom sound tar retries by clearing `/mnt/us/xcom-sound` first.
- The rootfs remount wraps `mntroot rw` with `trap "mntroot ro" EXIT`, so a
  dropped connection cannot leave the rootfs writable.

## Success check is last-app, NOT the menu
There is no launcher screen. `menu-boot.sh` opens `/mnt/us/last-app` directly
(chess the first time) and the APPS tab inside the game is the menu. `menu-boot.sh`
has **no `menu)` case** — an older version of install-all waited for a `menu`
pid and therefore always "failed" even on a perfect deploy. The current check
reads `last-app` and waits for that binary's pid; `last-app=koreader` exits 0
straight away.

## Verify by hand
```bash
ssh -i ~/.ssh/kindle_touch_ed25519 -o BatchMode=yes <ip> \
  'cat /mnt/us/last-id; ls -l /mnt/us/*-run.sh'   # 10 runners expected
```
Kindle busybox has no `ls --time-style`, no `grep -E`, no `pgrep`. Use `pidof`
and plain `grep`.
