# brogue

Status: **runs on the device, draws the main menu on the glass, and animates it.** Start it with `./tools/brogue-diag.sh [--debug] <ip>`; the log is in `diag/<stamp>/app.log` and a picture of the canvas in `diag/<stamp>/shot.png`. Whether a *tap* advances the game is still unproven (no tap-injection tool exists yet) — the pause-interrupt path is written and logs `pause interrupted`, but nobody has tapped it through. Do not SIGTERM `menu` to get the panel (see areas/menu.md). Host preview, the ARM binary and the symbol build all build.

## 2026-11-03 night — why "Brogue doesn't work": three real faults, all fixed

Found with `make brogue-debug` (unstripped ARM, `-g`) + a SIGSEGV/SIGBUS/SIGILL/SIGFPE
handler in `kfb-device.c` that prints the fault address, then `addr2line`. The
device has **no usable `dmesg` and no core files**, so this is the only way to
name a crash on it.

1. **Stack corruption → segfault at startup** (`kfb_gameLoop`, fault addr `0x6c`).
   `diag_probe_touch()` read `EVIOCGABS` into a 5-int struct; the kernel writes
   its full `struct input_absinfo` (6 ints, `value` first). The 4-byte overflow
   clobbered a callee-saved register, and the crash surfaced several call levels
   later at `atexit()` — so the reported line was nowhere near the bug. Fix: use
   all six fields. **Any ioctl into a local struct needs the struct's real size.**
2. **Host pause function registered on the device.** `kfbConsole.pauseForMilliseconds`
   pointed at the `nanosleep` stub in `kfb-platform.c` even for the ARM build, so
   `kfb_device_pauseForMilliseconds` was never called on the glass. Now `#ifdef
   EK_HOST` picks the stub and the device picks the real one.
3. **Animations drew nothing.** Brogue's menu flame loops `pauseBrogue(50)` and
   redraws, and the frame push lives in `nextKeyOrMouseEvent` — which that loop
   never reaches. The canvas stayed blank while the game animated into it. The
   device pause now flushes at most every 250 ms (an e-ink update cannot keep up
   with 50 ms) and polls touch during the delay, buffering one `rogueEvent` for
   the next input call and returning 1 when `PauseBehavior.interuptForMouseMove`
   asks to be interrupted. Without that, the flame loop could never be left.

Verified: both binaries paint the menu (`dark px` 18k of 120k sampled, and two
grabs 3 s apart differ by ~9.5k px, so it really animates).
`make brogue-test` still green (13 frames).

Runner caveat: `kindle-run.sh` refuses to start when the launcher `menu` session
is up ("the launcher will not exit"). If `menu` is alive, clear it first:
`: > /mnt/us/.menu-stop; : > /mnt/us/.menu-handoff; /sbin/initctl stop menu`
then `kill -9` any leftover `menu`.
`make brogue-host` + `make brogue-test` green (13 preview frames, layout
checks pass). `make brogue-kindle` produces a static ARM binary
(`brogue-kindle-app`, 1.4 MB). Grey layer wired: the device front-end owns the
panel (`ek_open(EK_TAKEOVER)`, `ek_grey_enable()`, `atexit(ek_close)`) and
paints the game grid as 6x12 grey glyphs from the shared cell grid (full
0..100 RGB triples, Rec.601 luma → ink 0..15, dark colour = solid ink).
Polarity = brightness, confirmed by probe: lit cells dark glyphs, unlit stays
paper. Touch mapping (two-tap travel from the plan) still to do.

## 2026-11-02 .63 incident — first on-device run parked the fridge Kindle

`./run-brogue-on-kindle.sh 192.168.0.63` pushed the game, took the screen
(`pillow stop`, `framework stop`, `eink: UI taken over`), then lost contact —
the monitor loop printed `cannot reach` forever. .63 was left running
`brogue-kindle-app` with the framework down and Wi-Fi dead, so the device
vanished from the LAN entirely (no ARP, no tcp/22 — it times out rather than
refuses).

- **Consequence: the USB drive does not mount while the device is stuck in
  the takeover.** Windows enumerates `Kindle Internal Storage`
  (serial `B011140822260CM1` = .63) but the partition reports **No Media**
  — the userstore only appears when the framework USB stack runs. Retrying
  drive-mode on Windows, disconnecting/re-enabling the PnP device, and
  `Set-Disk -IsOffline $false` do not help; the screen must be handed back
  first (tap QUIT then SURE), or the device rebooted to the library.
- **Also parked**: an unmanaged watcher task `./run-brogue-on-kindle.sh
  192.168.0.63` that loops `cannot reach` forever — kill it before working
  on .63 again.
- Recovery path documented in README "When it goes wrong": restore UI by
  hand if reachable (`sh /mnt/us/fbdaemon/restore-kindle.sh`), else quit the
  game from the panel, else reboot, then reinstall SSH via USB with
  `push-provision.ps1` (provision.sh + sshserv.conf boot job) since dropbear
  is not persistent on this device.

## What it is

BrogueCE (`brogue/` = the Brogue Community Edition source tree) on the Kindle
Touch 600x800 e-ink panel, through a **swappable console backend**
(`struct brogueConsole`): the game core plots a 100x34 grid (`COLS x ROWS`,
6x12 px glyph cells = 600x408 px) and our `kfb-platform.c` renders it into an
ink buffer, 1:1 with what the panel shows.

- Game window = bottom 408 px (34 rows * 12); permanent control bar = top
  392 px (4 rows x 3 buttons, 200 px wide x 70 px tall each; `kfb-bar.h`).
- `kfb-platform.c` (backend, both builds), `kfb-device.c` (device front-end,
  owns panel + touch, EL-includes `eink.h` — never built on PC),
  `kfb-bar.h` (button layout shared by both).
- PC build (`EK_HOST`): drives the run from `$KFB_SCRIPT` (one byte per
  keystroke) and dumps 600x800 PGM frames + a manifest into `$KFB_PREVIEW`.
- Glyph→ASCII mapping is local (`kfb_glyph_to_ascii`, classic Brogue legend:
  `#` wall, `.` floor, `@` player) with `glyphToUnicode()` as a fallback.

## Commands that matter

```
make brogue-host         # PC build (host C compiler, -DEK_HOST)
make brogue-test         # scripted run -> 13 frames in /tmp/kfb-frames + check
make brogue-kindle       # static ARM via docker kfb-build -> ./brogue-kindle-app
make brogue-debug        # same, with symbols -> ./brogue-kindle-app-debug
./tools/brogue-diag.sh [--debug] [--grab|--status] [IP]   # push, run, read the log, grab the panel
./run-brogue-on-kindle.sh [IP]   # push + take screen + run + restore
```

`brogue-test` uses `-` before the `timeout` line on purpose: the game loops
forever once `$KFB_SCRIPT` is spent, so the 20 s timeout is expected
(then `tools/check_brogue_preview.py` validates frames).

## Build facts (learned the hard way)

- **The Makefile's original `_brogue_src` list was missing three files**: the
  linker failed with `undefined reference to initializeGameVariantRapidBrogue`
  and `glyphToUnicode`. Added `src/variants/Globals{.,Rapid,}Brogue.c`,
  `src/platform/kfb-device.c` and `src/platform/platformdependent.c`.
  Upstream's own Makefile builds `$(wildcard src/brogue/*.c)
  $(wildcard src/variants/*.c) $(addprefix src/platform/,main.c
  platformdependent.c null-platform.c)` — that is the authoritative source
  list to stay in step with.
- **Upstream BrogueCE does not compile under `-Wall -Wextra -Werror`** (that
  is this repo's house flag for first-party game code). Its own Makefile uses
  only `-Wno-parentheses -Wno-unused-result -Wno-format
  -Wno-format-security -Wno-format-overflow`. The brogue-kindel rules now
  pass exactly those.
- **The device binary cannot be named `brogue`**: `brogue/brogue` is the
  upstream launcher script, so `-o ../brogue` from `cd brogue` hit "Is a
  directory". The static ARM output is `brogue-kindle-app` and the deploy
  script (`run-brogue-on-kindle.sh`) is wired accordingly.
- **`KFB_BAR_H` was defined as both its include guard and its value** — the
  guard macro is now `KFB_BAR_INCLUDED`.
- `tools/check_brogue_preview.py` counts "non-paper" pixels in the bar rows;
  a healthy frame has ~168k (the 1 px light-ink frames around the 12 buttons),
  above the 140k threshold.
- BrogueCE needs no `.dat` file: all game data compiles into the binary, and
  `DATADIR` is only for tiles/scores.

## Data / layout

`COLS 100, ROWS 34` (31 map rows + 3 message lines). Map+sidebar = `DCOLS 79 x
DROWS 29`. `DATADIR` = `brogue/compiled` on host (currently absent -> only
text mode, which is what we want); `/mnt/us/brogue` on the device.

## Still to do

- Touch mapping: two-tap travel (first tap places the cursor, second tap
  travels), taps -> `rogueEvent` on real buttons.
- On-device layout check (panel grey ramp vs the preview ramp). The binary has run on .63; taps are still unwired, so the glass cannot quit the game.