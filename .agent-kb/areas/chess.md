# chess

Status: **working end to end** — the Kindle itself calls
`https://api.deepseek.com/v1/chat/completions` (model `deepseek-flash`) and the
moves come back. `chess.conf` holds the real key and is gitignored.

## What it is

`src/chess.c` — you are White on a 600x800 touchscreen, DeepSeek is
Black and writes one line of trash talk under the board. Tap-to-pick/tap-to-move.
Quit, volume, wi-fi and the power-key sleep are the shared shell in
`src/kframe.c` (see `areas/kframe.md`). The hamburger is the top-right corner.
NEW, RETRY and TIMER are chess's own rows under the shared ones.

## Baked piece sprites

The pieces are **not** drawn from code shape primitives any more: `chess-art/*.png`
(RGBA source images, one per piece) are baked by `tools/bake_chess_sprites.py`
into `src/chess_pieces.h` — a 52x52 tile per piece, 4 bits/pixel (0 transparent,
1 paper, 2..15 ink, value minus 1 rescaled to the engine's 0..15 grey level),
plus the `ch_at(id,x,y)` accessor. `src/chess.c`
draws each piece one grey pixel at a time via `ch_at` + `ek_grey_rect`, and
`main()` calls `ek_grey_enable()` (same pattern as xcom/pvz), so captured/promotion
pieces are just the same tile scaled down at draw time.

- The baker is **alpha-masked, not luminance-thresholded** (the pvz baker would
  fail on white pieces, whose only ink is dark shading): the silhouette comes
  from the PNG alpha channel, then the shading is the pixels' luminance
  normalised per piece so white and black get the same contrast. `ALPHA_CUT=100`.
- The bake uses the **full grey range the engine's layer takes**: 14 ink levels
  (TONE_MAX 15 — a 4-bit nibble cannot hold 16), rescaled to 0..15 at draw time
  in `draw_piece`. Older bakes crushed art to 8 levels.
- The consumer draws inline via `ch_at()`; the older separate
  `chess_blit_piece()` .c output was removed as dead (nothing compiled it).
- `bake_chess_sprites.py [--preview out.png]` also writes a preview sheet of every
  piece on a light and a dark square at panel grey/scale. Rerun the baker to
  regenerate the header if the art changes; chess.c and the Makefile stay put.

### THE `ek_mono` grey killer (found on device)

After the sprites landed, the device showed **flat black/white** pieces while the
host looked perfect. Cause: a `ek_mono(1)` call left over in `main()` from the
older 1-bit draw code. The engine (`eink.c:952`) sets
`EPDC_FLAG_FORCE_MONOCHROME` on every update whenever `force_mono` is set, and
the e-ink controller then collapses the grey layer's 16 levels to one bit.
The **host simulator does not model FORCE_MONOCHROME**, so host runs cannot
catch it — the failure only shows on the actual panel. Fix was deleting the
call; xcom/pvz never call `ek_mono`. Live check: `kill -USR1 <chess pid>` dumps
`/tmp/chess-shot.pgm`; a healthy grey draw has 15 distinct values
(0,34,51,...238,255), the broken one is just 0/255.

The TIMER row in the shared menu cycles `TIMER_CHOICES[] = {0, 60, 180, 300,
600, 900, 1800}` seconds per side (chess.com's set: off, 1, 3, 5, 10, 15,
30 min); the choice is persisted as `clock = N` in
`chess.conf` (survives a reboot), and `timer_label_set()` renders
`TIMER: off / 1 min / 3 min / 5 min / 10 min / 15 min / 30 min`. `clock_show()` draws
`W m:ss  B m:ss` top right of the board; `clock_poke()` ticks only the side to
move while no menu is open (and not while the LLM owes a reply — the model's
wait is not burned against you), `clock_restart()` hands the clock over after a
move, and a zeroed clock ends the game (over 6/7: "ran out of time"). The
prompt sent to DeepSeek includes the remaining time for both sides so the model
plays faster when you are short. Tapping TIMER mid-game resets both clocks and
carries on from the current position. `make chess-test` opens the menu, taps
TIMER, and greps `kframe: app item 3`; the clock=60 menu shot must differ from
the plain one. On host the status line "clock set …" is not echoed (`--no-wifi`
has no API), so the test keys on the item dispatch, not the message.

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
- **`tools/kindle-run.sh` is game-generic and owns the whole handoff**:
  `GAME=chess sh /mnt/us/chess-run.sh` runs `/mnt/us/chess`; it detects leftover
  games, stops the screen mirror, stops powerd, proves event3 is free, runs the
  game (with `--detach` re-homing itself under setsid so the ssh link dying
  changes nothing), then `restore()` brings powerd/framework back. Every game —
  and the boot menu via `menu-boot.sh` — goes through this one script, so a
  handoff fix lands everywhere at once. `run-chess-on-kindle.sh` pushes chess,
  grabtest, the wrapper and `chess.conf`.
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

600x800: top bar 0-64 with the hamburger at (548, 2), board 28,64 with 68 px
squares, comment lines from 616. No bottom button row. Shapes are drawn in a
0..100 box and scaled; squares are dithered, not grey.

## Wi-fi setup screen (for a gifted unit)

The stock Wi-Fi dialog is unreachable once the game owns the panel, so the game
draws its own: `kframe_wifi_setup()` in `src/kframe.c` (list of networks → drawn
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

- **A quit relaunches the game.** The job is `start on started lab126_gui`, and
  giving the screen back starts `framework`/`lab126_gui` again -- which starts
  the job again. So tap QUIT then SURE and the Kindle flashes its splash and
  comes back to a fresh board: not a crash, by design on a gift unit. A game
  that ends with `chess-run: game exited rc=0` and no message of its own is a
  quit, not a fault; look for the tap, not for a bug.

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
- **The fix that stuck: never touch the `x` job.** Chess used to
  `system("/sbin/initctl stop x")` right after `ek_open`; that stopped blanket
  painting over the board, but on exit `restore-kindle.sh` restarted X and
  blanket repainted the boot splash + progress bar over the returning menu —
  the "rebooting" flash after QUIT. Removed 2026-11-01: XCOM proved X can stay
  up while a game holds the panel (engine `EK_TAKEOVER` stops framework+pillow
  only), and with X never stopping, the restore finds X up and never flashes.
  `src/chess.c` now has no initctl calls at all; this is exactly XCOM's path.
  The one forced full-panel white flush after takeover stays.
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
- What a press does produce: one `KERNEL: I pmic: PWRON1 event:` kernel line.
- Two counters that look right and are not:
  - `PMIC_IRQ` in `/proc/interrupts` counts **every** power-chip interrupt -
    charger, RTC, USB wake, the wake from suspend itself. Measured 2026-10-31:
    94 bumps for one press, and the old build logged
    `power button pressed (180 -> 181)` with nobody touching the device, so the
    game slept whenever it felt like it. **Do not use it.**
  - A plain *count* of `PWRON1` lines in the kernel log. The ring buffer is only
    16 KB and this device fills it with USB chatter, so a press whose line
    arrives as an older one scrolls out leaves the count unchanged - and a
    saturated buffer then hides every press. Measured: the buffer held exactly 5
    `PWRON1` lines while its whole 16 KB turned over in minutes.

How the code finds a press now (`kframe_pwr_presses()`, `kframe_pwr_log_grew()` in `src/kframe.c`):
`klogctl(SIZE_BUFFER)` then `klogctl(READ_ALL)` 150 ms apart into two buffers;
`pwr_log_grew()` finds where the older copy's last 128 bytes now sit in the
newer one, which is exactly how many bytes were written in between, and only
those bytes are searched for `PWRON1`. Scrolling out can neither invent a press
nor hide one. `--selftest` covers both, with synthetic buffers that shift by a
known amount.

**`/dev/kmsg` does not exist on this kernel** (2.6.31, no devkmsg driver): plain
read, `SEEK_END`, `SEEK_DATA` and a 128-byte buffer all fail `EINVAL`, proven by
`tools/kmsgprobe.c`. `klogctl` is the only way in. `klogctl(2)` (the old
`/proc/kmsg` read) is **not** used: it advances the shared `log_start`, so the
game would eat the device's `dmesg` and destroy the evidence of its own waking.

- Stock behaviour is the proof the key works at all: with the reader UI up and
  `powerd` running, a press shows the screensaver and stands the device by. That
  is also why `powerd` must stay stopped while the game owns the panel - it
  answers a press by bringing framework+x+blanket+pillowd back over the board.
- **powerd is now stopped by the shared runner, not in-game.** Chess used to
  `initctl stop powerd` inside `main`; that moved to `tools/kindle-run.sh` step 3
  (one place, covers xcom/tetris/chess/menu identically) and `restore()` starts
  it again on exit. Chess's own power-key sleep still works — it reads the
  kernel log, not powerd.
- `sleep_now()` in `src/kframe.c`: `pwr_drain(200)`, paint ASLEEP (`ek_force_full`), write `mem`
  to `/sys/power/state` (the write returns on wake), log `asleep for N s` from
  /proc/uptime, `pwr_drain(800)` so the waking press is not counted again,
  `ek_force_full` + `present()`, then rejoin wi-fi with `chess-wifi.sh auto`
  when `wifi_status()` says it is gone.
- A press during the model's wait cancels the question: `run_curl()` waits in
  `select(2)` with a 200 ms timeout and kills curl when `kframe_pwr_presses()` reports
  anything at all.
- A write failure to `/sys/power/state` shows on the status line and stderr and
  is never swallowed.

**Sandbox caveat:** from inside this workspace shell (WSL `172.22.x.x` NAT) the
Kindle LAN `192.168.0.x` is unreachable — pings to `.66`/`.63` time out, so a
`run-chess-on-kindle.sh` handoff has to be started by the user from their own
network. Code/binary work, host renders and `make chess-test` all verify here.

**Devices:** two Kindles answer on the LAN - `.66` (serial B010260121322E2D, the
Touch the game is built for) and `.63`. `jailbreak-kindle/.kfb-secrets` sets
`KINDLE_IP=192.168.0.63`, so always pass the address:
`./run-chess-on-kindle.sh 192.168.0.66`. The workspace service `chess-on-kindle`
does exactly that.

### Probes on the device

- `grep PMIC_IRQ /proc/interrupts` - **not** a press counter (see above). It is
  only useful as a sanity number.
- `dmesg | grep -c PWRON1` - how many press records the 16 KB log still holds;
  a new press makes the last one appear at the tail.
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

### Trap: the game used to be a child of the ssh session (fixed 2026-10-31)

The 2026-10-31 freeze was the other half of this: launched with `ssh -tt`, the
game sat in the pty's foreground, so when the Kindle slept and the link dropped,
sshd hung the session up and the game died of **SIGHUP with no log line and no
dmesg entry** — leaving its last board painted, `chess` gone, `powerd` running
and `x`/`framework`/`cvm` all down. "Says it's my turn but I cannot move" is
that state, not a hung game. `tools/kindle-run.sh --detach` now re-runs itself
under `setsid` (no controlling terminal, output to `/mnt/us/chess-run.log`) and
`run-chess-on-kindle.sh` only polls the log, so the PC going away changes
nothing. Diagnose with `pidof chess` — a running game holding event3 is a hang,
an absent one is this.

### One game per boot

The chess job is `start on started lab126_gui`, and restoring the reader fires
that event, so every restore used to relaunch the game and hijack the screen
(a recovery attempt started a fresh board instead of returning the reader).
`chess-run.sh` now drops `/tmp/chess.started` (ram, cleared at reboot) and the
job exits early if it exists. Consequence: after QUIT the game does not
reappear until a reboot or a hand launch.

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
