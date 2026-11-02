# brogue

Status: **backend builds and previews on the host; device binary cross-compiles.**
`make brogue-host` + `make brogue-test` green (13 preview frames, layout
checks pass). `make brogue-kindle` produces a static ARM binary
(`brogue-kindle-app`, 1.4 MB). Grey layer wired: the device front-end owns the
panel (`ek_open(EK_TAKEOVER)`, `ek_grey_enable()`, `atexit(ek_close)`) and
paints the game grid as 6x12 grey glyphs from the shared cell grid (full
0..100 RGB triples, Rec.601 luma → ink 0..15, dark colour = solid ink).
Polarity = brightness, confirmed by probe: lit cells dark glyphs, unlit stays
paper. Kindles were offline — not yet pushed; run
`./run-brogue-on-kindle.sh 192.168.0.63` and take a panel shot to verify the
grey ramp on glass. Touch mapping (two-tap travel from the plan) still to do.

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
- On-device run + layout verification (panel grey ramp vs preview ramp).