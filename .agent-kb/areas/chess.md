# chess

Status: **working end to end** — the Kindle itself calls
`https://api.deepseek.com/v1/chat/completions` (model `deepseek-flash`) and the
moves come back. `chess.conf` holds the real key and is gitignored.

## What it is

`src/chess.c` — one file, you are White on a 600x800 touchscreen, DeepSeek is
Black and writes one line of trash talk under the board. Same shape as
`src/xcom.c`: single-file game + shared e-ink engine (`EK_HOST` for the PC
build), tap-to-pick/tap-to-move, QUIT+SURE to give the screen back.

## Commands that matter

```
make chess-host            # PC build, -Werror clean
./chess-host --selftest --no-takeover      # rules + perft, must print "selftest clean"
./chess-host --fake-ai --auto 8 --no-takeover --dump /tmp/c.pgm
./chess-host --apicheck --conf chess.conf  # plays e2e4, then asks the API once
make chess-kindle          # static ARM musl, docker kfb-build, 83 KB
./run-chess-on-kindle.sh [IP] [--args]     # push + take screen + run + restore
```

## Facts learned the hard way

- **Setup bugs look like engine bugs.** Perft depth 1 returning 34 instead of 20
  was the initial position written with the pawn ranks *and* the back ranks
  reversed. Kiwipete perft passed the whole time — always also run perft from
  the real opening position.
- **`san_of` must not append the destination for a castle.** It produced
  `O-Og1`; the `--selftest` "e1g1 is O-O" check caught it.
- **A reasoning model can answer with empty `content`.** deepseek-flash emits
  `reasoning_content` first; with a long prompt it can spend the budget and
  return `content: ""`. There is one automatic retry (`g_transient`) and the raw
  body is printed on failure (`reveal_reply`).
- **`--dump` has to run before the batch early-return** on the device, otherwise
  `--fake-ai --dump` produces no file. The `#else` branch now falls through to
  the dump instead of returning.
- **Device config path is `/mnt/us/chess.conf`**, not `/mnt/us/chess/...` — the
  binary itself is `/mnt/us/chess`.
- **`tools/kindle-run.sh` is game-generic**: `GAME=chess sh /mnt/us/chess-run.sh`
  runs `/mnt/us/chess` and skips the xcom sound setup. `run-chess-on-kindle.sh`
  pushes chess, grabtest, the wrapper and `chess.conf`.
- **White pieces on white squares are nearly invisible** with a 1-pixel rim: the
  rim is now two rings (12 offsets), so a white pawn on a light square still
  reads (ink per square went 385 -> ~500).
- **The Kindle must call DeepSeek directly.** `http://127.0.0.1:4020/...` only
  exists on this PC, so a conf pointing at it fails from the device. Direct is
  `https://api.deepseek.com/v1/chat/completions`, model `deepseek-flash`
  (V4.1-Flash; `deepseek-v4-flash` is a retired alias).
- **TLS works on the device with a pushed CA bundle.** The Kindle's own store
  predates DeepSeek's certificate. `run-chess-on-kindle.sh` pushes this PC's
  `/etc/ssl/certs/ca-certificates.crt` to **`/mnt/us/chess-ca.pem`** and the
  binary uses `--cacert` — verified HTTP 200 and a real reply on the Kindle.
  The old default was `/mnt/us/chess/ca.pem`, i.e. a path *inside* the binary
  file (`/mnt/us/chess` is the executable), so it could never have existed.
- **Pushing over a running binary fails** with `Text file busy`. Write
  `/mnt/us/chess.new`, `chmod +x`, then `mv` — the script does this now.
- **`scp` does not work** (no sftp-server) — push with `cat file | ssh ... 'cat > /path'`.
- **No PIL, no ImageMagick on this PC**: use `tools/pgm2png.py in.pgm out.png`
  to turn a `--dump` into something viewable.
- **`--auto N --dump FILE --no-takeover` is the cheap device test**: it plays N
  half moves against the real API, writes the board and leaves the Kindle UI
  alone. `--apicheck` prints one raw reply and the parsed move and taunt.

## Screen layout

600x800: top bar 0-64, board 28,64 with 68 px squares, comment lines from 624,
QUIT/button row at 744. Shapes are drawn in a 0..100 box and scaled; squares are
dithered, not grey.

## Wi-fi setup screen (for a gifted unit)

The stock Wi-Fi dialog is unreachable once the game owns the panel, so the game
draws its own: `wifi_setup()` in `src/chess.c` (list of networks → drawn
keyboard for the name if needed → drawn keyboard for the password → failure
screen with TRY AGAIN / OTHER NETWORKS). Joining is shelled out to
`/mnt/us/chess-wifi.sh` (`tools/chess-wifi.sh`), which uses `wpa_cli` on
`wlan0` and remembers the result in `/mnt/us/wifi.conf`.

- **busybox awk does not expand `\t` in `-F'\t'`.** `scan_results | awk -F'\t'`
  silently yields empty fields, so the network list came back blank while the
  raw output was fine. Fix: `T=$(printf '\t')` then `-F"$T"`. This one cost a
  whole debugging round.
- **`wpa_state=COMPLETED` alone is not proof of a join.** For a second after
  `select_network` the supplicant still reports the network it is leaving, so a
  join to a network that does not exist "succeeded" instantly. The wait loop now
  requires `wpa_ssid` to equal the requested ssid too.
- **`wpa_passphrase` refuses keys under 8 characters** with `Passphrase must be
  8..63 characters`, which is useless on the panel. The script checks the length
  first and says "the password must be at least 8 characters".
- **`wpa_cli get_network N psk` returns the plaintext key**, not a hash, so
  `wifi.conf` is written from `wpa_passphrase SSID PSK` instead (which exists at
  `/usr/bin/wpa_passphrase`). File is mode 600.
- **Host-build taps drive the whole flow**: `EK_HOST` reads `tap X Y` lines on
  stdin, and `wait_tap()` now does too, so `printf 'tap ...' | ./chess-host
  --wifi --dump f.pgm` walks every screen without a device. Keyboard geometry:
  rows are `1234567890` (y150), `qwertyuiop` (210), `asdfghjkl` (270),
  `zxcvbnm` (330); column x = 22 + i*56, key height 54, so a tap at
  (22+i*56+10, row_y+10) always lands on a key. BACK/NEXT at y 454, list buttons
  at y 660.
- **`--wifi-shot FILE [list|name|pass|fail]`** draws one wi-fi screen and exits,
  which is how the layout gets checked without touching the device.
- **`kill -USR1 <pid>`** writes `/tmp/chess-shot.pgm`. The only way to see the
  screen of a game running on a Kindle you are not holding (SSH drops when the
  Kindle sleeps, so a boot test cannot be watched live).
- **The Kindle sleeps and drops off the network.** After a few idle minutes SSH
  stops working (ping: Destination Host Unreachable) and nothing remote can wake
  it — someone has to touch the screen. Budget for it when scripting a long run.

## Boot straight into the game

`tools/chess-boot.conf` is pushed to `/mnt/us/chess-boot.conf` and copied to
`/etc/upstart/chess.conf` on the device (`mntroot rw` first, `mntroot ro` after).
`start on started lab126_gui`, waits up to 180 s for wi-fi, retries
`chess-wifi.sh auto` every 30 s, then `GAME=chess exec sh /mnt/us/chess-run.sh`.
The existing runner restores the reader on exit, so GIVE UP on the board is the
way out and a crash still leaves a usable Kindle. Remove the file to undo.

**Verified on a real reboot** (2026-10-31): `/mnt/us/chess-boot.log` reads
`wifi up after 10s … starting the game`, `pidof chess` returns a pid, `kfbterm`
and `framework` are `stop/waiting`, and `kill -USR1` gives a live board.

- **Upstart runs `script` blocks with `sh -e`, and that killed the job.** A bare
  `[ cond ] && sh chess-wifi.sh auto` in the loop returned 1 when there was no
  remembered network, and `-e` ended the whole job: the log stopped after one
  line and no game started, with nothing else to see. Fixed with `set +e` at the
  top of the script *and* an `if` around the retry. Any call that is allowed to
  fail needs one of those.
- **Never block the game on wi-fi.** Killing `framework` before wifid finishes
  associating leaves the unit with no link and no shell (had to be recovered by
  hand on the panel). Hence: wait, then launch anyway. No link is not a dead end
  — the game's own wi-fi screen calls `chess-wifi.sh`, which brings the link up
  with `wpa_cli` + `udhcpc` and needs no framework.
- **The Kindle clock is 2012 until wi-fi sets it**, so the boot log also writes
  `$(cat /proc/uptime)`; `date` alone cannot tell you how far into boot anything
  happened.
- **`/mnt/us/wifi.conf` can disappear** (and did here), so `auto` legitimately
  answers `error: no remembered network` while wifid has already reconnected on
  the Kindle's own saved profile. `status` is the source of truth, not the conf.
- **The Kindle is on both wlan0 and ppp0 (3G).** wlan0 holds the default route;
  ppp0 only carries host routes to Amazon. `route -n` shows which is which.

## The boot picture

**The file that is actually shown is `/opt/amazon/low_level_screens/600x800/splash.png`**
(rootfs: needs `mntroot rw` / `cp` / `sync` / `mntroot ro`). The copy in
`/var/local/blanket/low_level_screens/images/` is *not* what paints at boot —
replacing it changed nothing on the glass, which is how this was found.
`tools/make_splash.py` builds the image; the stock original is backed up to
`/mnt/us/splash-amazon-orig.png`.

- **The format is not negotiable**: 600x800, PNG, bit depth 8, colour type **0**
  (greyscale), no alpha. The stock file is exactly that; anything else (RGBA,
  palette, another size) is drawn as nothing.
- **No PIL, no ImageMagick, no ffmpeg on this PC.** `tools/make_splash.py` does
  the PNG decode (all five filter types), alpha-over-white, box-downscale and
  encode itself with `zlib`/`struct`; `--preview` prints it as ASCII. Same
  reason `tools/pgm2png.py` exists.
- A landscape logo can only get so big on a portrait panel: auto-crop to the ink,
  then fit inside `600 - 2*margin` gives 552x368 for the aijly logo.

## The progress bar over the game (solved)

`blanket` (`lxinit blanket -t "splash screensaver langpicker"`, started by the
`x` job) paints BOTH the start-up picture and the boot progress bar directly on
`/dev/fb0`. Stopping `framework` does not stop it, so its splash surface got
repainted over the game and the bar froze half-drawn — the "black blob".

- **Do not `killall blanket`.** blanket is not its own upstart job; it is a
  child of `lxinit` under the `x` job. Killing it makes `x` stop with
  `RESULT!=ok`, and `/etc/upstart/x_monitor.conf` restarts the whole GUI —
  up to `RESTART_LIMIT=5` in one boot, and then `REBOOT_LIMIT=1` fires a
  "repair" shutdown. It also comes back painting, so the bar returns anyway.
- **The fix: the game stops the `x` job**, `system("/sbin/initctl stop x")` in
  `src/chess.c` right after `ek_open` (only when it took the panel over). A
  deliberate stop is `RESULT=ok`, so `x_monitor` leaves it down, and
  `restore-kindle.sh` starts x again on exit because `x_ready` is false.
  Verified twice: after `kill <chess>` the device came back with
  `x start/running`, `framework start/running`, cvm and blanket up.
- Then `ek_clear(EK_PAPER); ek_force_full(); ek_flush();` — one full-panel
  GC16 white pass, so the splash/progress-bar pixels (which the game's partial
  updates would never touch) are wiped before the board is drawn.
- `tools/chess-boot.conf` deliberately does nothing about the splash: killing
  it there triggers the x_monitor reboot path described above.
- **Reading `/dev/fb0` does not tell you what is on the glass** — the engine
  pushes its own canvas through mxcfb ioctls and never writes fb0, so fb0 still
  holds whatever the framework last drew. Don't diagnose from it; use
  `kill -USR1 <pid of chess>` for the game's own canvas and your eyes for the
  panel.

## The power button and sleep (working, confirmed on the panel 2026-10-31)

**The key is not an input device.** All of this was measured with the game
running, not inferred:

- `tools/keyprobe.c` (Makefile `keyprobe`, deployed as `/mnt/us/keyprobe`)
  watched event0..event4 through ~90 s of presses: **zero** key events on every
  node, `event2` (`whitney-button`) included. event2 only ever carries the touch
  codes (325 BTN_TOOL_FINGER, 330 BTN_TOUCH) that zforce also sends.
- `/proc/whitney_button` never changed in 120 s of 10 Hz polling. It is the
  screen-saver *lock* flag and reads "unlocked" through every press. (Old trap:
  `"whitney button is unlocked"` contains `"locked"`.)
- What a press does produce: one `KERNEL: I pmic: PWRON1 event:` kernel line and
  **one bump of the `PMIC_IRQ` counter in `/proc/interrupts`** (152 log lines vs
  153 interrupts, then every press moved it by 1-3).

So the game counts `PMIC_IRQ`. `pmic_irqs()` parses `/proc/interrupts` (sums the
per-cpu columns, -1 when the line is missing); `pwr_poll()` compares it with
`pwr_seen` and turns an increase into `sleep_now()`. `--selftest` checks the
parse against synthetic lines, because getting it wrong costs a build and a trip
to the device.

- Stock behaviour is the proof the key works at all: with the reader UI up and
  `powerd` running, a press shows the screensaver and stands the device by. That
  is also why `powerd` must stay stopped while the game owns the panel - it
  answers a press by bringing framework+x+blanket+pillowd back over the board.
- `sleep_now()`: `pwr_settle(200)`, paint ASLEEP (`ek_force_full`), write `mem`
  to `/sys/power/state` (the write returns on wake), log `asleep for N s` from
  /proc/uptime, `pwr_settle(800)` so the waking press is not counted again,
  `ek_force_full` + `present()`, then rejoin wi-fi with `chess-wifi.sh auto`
  when `wifi_status()` says it is gone.
- A press during the model's wait cancels the question: `run_curl()` waits in
  `select(2)` with a 200 ms timeout, takes one `pwr_base` before the loop, and
  kills curl when the counter moves.
- A write failure to `/sys/power/state` shows on the status line and stderr and
  is never swallowed.

**Devices:** two Kindles answer on the LAN - `.66` (serial B010260121322E2D, the
Touch the game is built for) and `.63`. `jailbreak-kindle/.kfb-secrets` sets
`KINDLE_IP=192.168.0.63`, so always pass the address:
`./run-chess-on-kindle.sh 192.168.0.66`. The workspace service `chess-on-kindle`
does exactly that.

### Probes on the device

- `grep PMIC_IRQ /proc/interrupts` - the press counter, one bump per press.
- `/mnt/us/keyprobe [seconds] [dev...]` - held key codes and EVIOCGKEY bits per
  input device.
- `tools/pw-watch.sh` - uptime / whitney_button / powerd state /
  `/sys/power/state` once a second to `/tmp/pw.log`.

### Trap: a stopped service can leave the game holding the panel

Stopping the workspace service kills the ssh side; the game can survive, with
`x`/`pillow`/`framework` all `stop/waiting` and `/mnt/us/grabtest` reporting
`EVIOCGRAB refused: Resource busy`. Recovery, in this order:

    kill $(pidof chess)                     # SIGTERM exits it
    sh /mnt/us/fbdaemon/restore-kindle.sh   # UI, x, pillow, framework, powerd
    /sbin/initctl stop kfbterm              # the mirror grabs touch
    /mnt/us/grabtest                        # must say "grab OK"

## The screen mirror grabs the touch device

`kfbterm` (the kfb screen mirror) starts on `started lab126_gui` and EVIOCGRABs
`/dev/input/event3`. While it is up the stock Kindle UI **draws but ignores
every tap** - it looks like a bricked device. It also makes the game's grab
fail, so `kindle-run.sh` stops it at launch. A reboot does not help: kfbterm
respawns with the UI and grabs touch again.

## Next

- The promotion picker exists (`draw_promo`/`do_promo`, taps at 1311) but has no
  automated test; the tap test in `make chess-test` only covers select+move.
- Confirm on the panel that the new boot picture is what appears (it cannot be
  captured remotely).
