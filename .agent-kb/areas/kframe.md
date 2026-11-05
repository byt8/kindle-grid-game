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
- `src/kframe.h` — hamburger, volume, wi-fi, apps, quit. Row geometry for APPS.
- `src/kframe.c` — shell behaviour. Chess and tetris call it. App catalog matches `tools/menu-boot.sh`.
- `src/chess.c` — `open_frame()`, items NEW, RETRY, CLOCK, TAKEN, `app_id` chess.
- `src/tetris.c` — `open_frame()`, items NEW, PAUSE, SPEED, `app_id` tetris. Play buttons stay. QUIT is the shell's.
- `src/kshell.h` / `.c` — `kshell_options()` / `kshell_option_at()`: the shared one-per-row chooser (sudoku LEVEL, tetris SPEED).
- `tools/menu-boot.sh` — boot opens `/mnt/us/last-app`. `switch-app` changes it. `.menu-stop` leaves the reader up.

## Facts
- [verified 2026-11-06 src/kframe.h] Hamburger is a **square**: `(548, 2)` size `48×48`, bottom edge y=50. A tap at `(572, 26)` opens the shell. The three bars are centred from `h` in `paint_tab()`, so the height can change without touching them. Clears chess's board (top 64), tetris's NEXT box (top 76) and sudoku's chips (top 58).
- [verified 2026-11-06 src/chess.c] The header rows (model, status, move list) go through `head_text()`, which truncates at `KFRAME_TAB_X - 4`. The clock is right-aligned to `KFRAME_TAB_X - 8`, not the panel edge. Without this the 48px square covers them.
- [verified 2026-11-05 src/kshell.h] Tab minimum width is 60. At 72, chess's four left tabs plus VOLUME WIFI APPS QUIT overlap. Centres with these labels: NEW (36, 24), RETRY (100, 24), CLOCK (164, 24), TAKEN (228, 24), VOLUME (368, 24), WIFI (436, 24), APPS (500, 24), QUIT (564, 24).
- [verified 2026-11-05 src/kframe.h] APPS row 1 (chess↔tetris, xcom is row 0) is centred at (300, 212). The running app is hidden. KINDLE writes `.menu-stop` and does not change last-app.
- [verified 2026-11-05 src/kframe.c] Volume is `amixer sget/sset Speaker`. If amixer fails, the page shows the error and `+`/`-` do not invent a level. This PC prints `amixer: not found`.
- [verified 2026-11-06 src/tetris.c] Tetris's left tabs are NEW, PAUSE, SPEED at centres (36,24), (100,24), (164,24). Each has its own `tap_*`, so a tap must be on the tab AND then on the action button (`KFRAME_ACT_*`, centre (300,236)). Log lines: `app item 1` NEW, `2` PAUSE, `3` SPEED.
- [verified 2026-11-06 src/kshell.h] `kshell_options(y0, labels, n, chosen)` draws one button per row from `KSHELL_OPT_X`/`_W`/`_H`, stepping `KSHELL_OPT_STEP` 84 from `y0`; `kshell_option_at()` returns the index or -1. Row i centre = `y0 + i*84 + 36`. Sudoku LEVEL is y0 140 (rows 176/260/344); tetris SPEED is y0 140 (rows 176/260/344/428).
- [verified 2026-11-06 src/tetris.c] `paused` stops the drop clock (`kframe_covering() || paused` pushes `drop_at` out) and draws a PAUSED card. A tap on the board also un-pauses. Without this, a pause would dump every held drop on resume.
- [verified 2026-11-05 src/kframe.c] Wi-fi shells out to `/mnt/us/chess-wifi.sh`. Host override is `-DKFRAME_WIFI_SCRIPT`. The WIFI tab is stepped by `kframe_tap`. `kframe_wifi_setup()` is a separate blocking loop (boot, `--wifi`). Do not nest that loop inside the tab.
- [verified 2026-11-05 src/kframe.h] JOIN / NEXT is `KFRAME_WIFI_GO_*`. Centre is (438, 228). It sits under the text field. The keyboard well starts at y=468.
- [verified 2026-11-06 src/sudoku.c] Sudoku calls `kframe` too: `open_frame()`, items **NEW / ERASE / HINT / LEVEL** (each with its own `tap_*`), `app_id` sudoku, `--menu-shot`. Its own NEW / ERASE / HINT / QUIT row and the digit row stay on the board; digits are not a menu tab. Left tab centres: NEW (36,24), ERASE (100,24), HINT (164,24), LEVEL (228,24).
- [verified 2026-11-05 src/rockman.c] Rockman calls it too: NEW and SPEED, `app_id` rockman. SPEED centres (100, 24); VIC row centre (300, 344). It stays `APP_VISIT`. The hamburger covers the brick at the top-right of the cave; the lives digit (row 1, col 20) is just under it. XCOM, pvz, brogue, calib and KOReader still do not.
- [verified 2026-11-05 tools/menu-boot.sh] Boot does not draw `src/menu.c`. It launches `last-app`. A missing file starts chess and says so. A name that is not in the catalog, or a binary that is not installed, goes to the reader and does not open a substitute game. A refused switch keeps the saved app.
- [ran 2026-11-05 make tetris-test && make chess-test && make wifi-test] All passed. Switch chess→tetris writes `/tmp/switch-app`. Switch chess→xcom also writes `/tmp/return-app` (`chess`). KINDLE writes `/tmp/.menu-stop` and does not write switch-app.

## Built
- Top-right hamburger opens the tab shell. Game tabs on the left, VOLUME / WIFI / APPS / QUIT on the right. QUIT's second tap writes `.menu-stop`. APPS writes `switch-app` (and `return-app` when the target has no shell). Entry: `kframe_init` from chess and tetris `open_frame`. Host stamps go to `/tmp`, device stamps to `/mnt/us`.
- Pictures without a device: `./chess-host --no-takeover --no-wifi --menu-shot FILE.pgm` and `--wifi-shot FILE list|name|pass|sym|fail`, then `python3 tools/pgm2png.py`. Session PNGs are committed in `docs/kframe/` (moved here from the `chess` workspace on 2026-11-05).

## Decisions
- Tetris got the shell in the same change as the APPS tab. The other games did not: leaving one returns to `return-app` instead of growing a shell inside each of them.
- One keyboard in `kshell`. Rejected: the old always-on symbol rows and a sticky `SHIFT ON` label. **123** swaps one punctuation page. SHIFT is one capital, then small.
- QUIT is two taps on the same tab. Leaving the tab disarms it.
- CLOCK stays open after CHANGE. NEW and RETRY close. TAKEN is pieces missing from the start set. A promotion looks like a missing pawn; the screen says so.

## Dead ends
- `--menu-shot` always opens on NEW, so two menu shots look the same after a saved clock. The clock check dumps the CLOCK tab (`tap 194 24`) and compares that picture.
- A tab tap only selects the tab; it does not log an `app item` line. Grepping for one after `tap <tab>` finds nothing and looks like a dead button. Tap the action button too, or check the picture.
- `dd if=/dev/fb0 bs=304` (4bpp) gives a mostly-blank picture: the Kindle exposes one byte per pixel, so the stride is 608. Use `bs=608 count=800` and `tools/fb_raw_to_png.py RAW OUT.png 608 600 800`.
- `grabtest` fails with `EVIOCGRAB refused: Resource busy` while a game holds the input device. Read `/dev/fb0` instead.
- A stale ARM binary looks exactly like a broken feature. The device ran a tetris built before the square-button change and its 26px tab missed taps. Check `md5sum` on both sides before debugging the source.

## Do not redo
- Tab centres above are for these labels at `KSHELL_TAB_MIN_W` 60. Recompute from `kshell.h` if a label width changes. Do not hard-code a second set. APPS row 1 stays (300, 212) only while xcom is the first catalog entry and the hidden app is chess or tetris.

## Open
- Volume has not been tried where `amixer` and the `Speaker` control exist.
- Tetris PAUSE holds only the drop clock. There is no resume countdown, and the panel waveform is not changed while paused.
