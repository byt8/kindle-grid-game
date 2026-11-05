---
area: kframe
title: Shared Kindle shell
aliases: [hamburger, appmenu, volume, kshell, tabs]
updated: 2026-11-05
---

# Shared Kindle shell

## Map
- `src/kshell.h` — tab and keyboard geometry. Tests read these defines.
- `src/kshell.c` — paints the tabs and the keyboard. `kshell_init` fails loud if tabs overlap or do not fit.
- `src/kframe.h` — hamburger, volume, wi-fi, and the chess shell API.
- `src/kframe.c` — shell behaviour. Chess is the only caller.
- `src/chess.c` — `open_frame()`, items NEW, RETRY, CLOCK, TAKEN.

## Facts
- [verified 2026-11-05 src/kframe.h] Hamburger is `(548, 2)` size `48×26`. A tap at `(560, 10)` opens the shell.
- [verified 2026-11-05 src/kshell.c] App tabs pack from the left at x=6, each at least 72px. General tabs pack from the right. With labels NEW, RETRY, CLOCK, TAKEN, VOLUME, WIFI, QUIT (all 72px) the centres are (42, 24), (118, 24), (194, 24), (270, 24), (406, 24), (482, 24), (558, 24). A fifth 72px app tab fails init.
- [verified 2026-11-05 src/kframe.c] Volume is `amixer sget/sset Speaker`. If amixer fails, the page shows the error and `+`/`-` do not invent a level. This PC prints `amixer: not found`.
- [verified 2026-11-05 src/kframe.c] Wi-fi shells out to `/mnt/us/chess-wifi.sh`. Host override is `-DKFRAME_WIFI_SCRIPT`. The WIFI tab is stepped by `kframe_tap`. `kframe_wifi_setup()` is a separate blocking loop (boot, `--wifi`). Do not nest that loop inside the tab.
- [verified 2026-11-05 src/kframe.h] JOIN / NEXT is `KFRAME_WIFI_GO_*`. Centre is (438, 228). It sits under the text field. The keyboard well starts at y=468.
- [verified 2026-11-05 src/chess.c] Other games do not call `kframe`. Their own QUIT buttons are unchanged. The launcher in `src/menu.c` is a different screen.
- [ran 2026-11-05 make chess-test && make wifi-test] Both passed. Volume and the wi-fi list on this PC show the real missing-tool errors.

## Built
- Top-right hamburger opens the tab shell. Game tabs on the left, VOLUME / WIFI / QUIT on the right. QUIT needs a second tap (`SURE?`). Keyboard (`kshell`) only while a text field is focused. Entry: `kframe_init` from chess `open_frame`.
- Pictures without a device: `./chess-host --no-takeover --no-wifi --menu-shot FILE.pgm` and `--wifi-shot FILE list|name|pass|sym|fail`, then `python3 tools/pgm2png.py`. Session PNGs are committed in `docs/kframe/` (moved here from the `chess` workspace on 2026-11-05).

## Decisions
- Chess first. Rejected: wiring tetris, sudoku, xcom, rockman, pvz, calib, brogue in the same change.
- One keyboard in `kshell`. Rejected: the old always-on symbol rows and a sticky `SHIFT ON` label. **123** swaps one punctuation page. SHIFT is one capital, then small.
- QUIT is two taps on the same tab. Leaving the tab disarms it.
- CLOCK stays open after CHANGE. NEW and RETRY close. TAKEN is pieces missing from the start set. A promotion looks like a missing pawn; the screen says so.

## Dead ends
- `--menu-shot` always opens on NEW, so two menu shots look the same after a saved clock. The clock check dumps the CLOCK tab (`tap 194 24`) and compares that picture.

## Do not redo
- Tab centres above are for these seven 72px labels. Recompute from `kshell.h` if a label width changes. Do not hard-code a second set.

## Open
- The tab shell has not been pushed to a Kindle. Host pictures only.
- Volume has not been tried where `amixer` and the `Speaker` control exist.
