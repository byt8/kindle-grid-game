---
area: kframe
title: Shared Kindle shell
aliases: [hamburger, appmenu, volume]
updated: 2026-11-03
---

# Shared Kindle shell

## Map
- `src/kframe.h` — the API. Chess is the only caller.
- `src/kframe.c` — hamburger, menu, volume, wi-fi screens, power-key sleep.
- `src/chess.c` — `open_frame()`, `on_chess_item()` (NEW, RETRY).

## Facts
- [verified 2026-11-03 src/kframe.h] Hamburger is `(548, 2)` size `48×26`. A tap at `(560, 10)` hits it. QUIT is menu row 0; a tap at `(300, 110)` hits it. VOLUME is row 1, y centre 174.
- [verified 2026-11-03 src/kframe.c] Volume is `amixer sget/sset Speaker`. If amixer fails, the page shows the error and `+`/`-` do not invent a level.
- [verified 2026-11-03 src/kframe.c] Wi-fi still shells out to `/mnt/us/chess-wifi.sh`. The filename is historical. The screens moved here from `chess.c`.
- [verified 2026-11-03 src/chess.c] Other games are not wired. Their own QUIT buttons are unchanged.
- [ran 2026-11-03 make chess-test] Host selftest, menu picture, volume page, and two-tap quit all passed. Volume on this PC prints `amixer: not found`.
- [ran 2026-11-03 ssh .63] The file `./run-chess-on-kindle.sh` pushes is `./chess`, and it does not rebuild unless asked. On 2026-11-03 that file was still the 1 Nov ARM binary (111900 bytes, no `THIS GAME` string) while `chess-host` was new, so a deploy looked identical. New binary is 116004 bytes. After the push, `pidof chess` was 5925 and the log said `grab OK` then `board loop`. The glass was not seen from here.

## Built
- Chess: top-right hamburger. Menu order is QUIT, VOLUME, WIFI, then NEW and RETRY. QUIT needs a second tap (`SURE?`). Entry: `kframe_init` from `open_frame`.
- Pictures without a device: `./chess-host --no-takeover --no-wifi --menu-shot /tmp/chess-menu.pgm` then `python3 tools/pgm2png.py`.

## Decisions
- Chess first, because it already owned wi-fi and sleep. Rejected: wiring every game in the same change.
- App rows sit under the shared rows. Rejected: leaving NEW/RETRY as a second button bar, which would make two places to quit once QUIT moved.

## Open
- Tetris, sudoku, xcom, rockman, pvz, calib, brogue do not call `kframe` yet.
- Volume has not been tried on a Kindle, where `amixer` and the `Speaker` control exist.
- The glass after the .63 restart was not looked at from this PC. The running binary is the new one (`THIS GAME` is in `/mnt/us/chess`).
