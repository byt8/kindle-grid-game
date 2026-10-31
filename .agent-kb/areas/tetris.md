# tetris

Status: **working on the device** — builds, self-test clean, three screen dumps
verified by the picture checker, and it holds the screen on a real Kindle
(192.168.0.66, pushed via `run-tetris-on-kindle.sh`).

## What it is

`src/tetris.c` — one file, a 10x20 block-stacking game on the 600x800 e-ink
touchscreen. Same shape as `xcom.c`/`chess.c`: single-file game + shared e-ink
engine (`EK_HOST` for the PC build), tap buttons (LEFT/TURN/RIGHT/DOWN/DROP/QUIT),
QUIT+SURE to give the screen back. A falling piece shows a **ghost** (thin frame)
where it will land. Also has a one-ply auto-player for `--auto N`.

## Commands that matter

```
make tetris-host           # PC build, -Werror clean
./tetris-host --selftest --no-takeover --seed 1     # the rules check themselves
./tetris-host --auto 24 --no-takeover --seed 1 --dump /tmp/tetris-play.pgm
make tetris-test           # self-test + 3 dumps + check_pgm + check_tetris
make tetris-kindle         # static ARM musl, docker kfb-build
./run-tetris-on-kindle.sh [IP] [--args]   # push + take screen + run + restore
```

`make tetris-test` needs `tetris-host` built first (it calls `./tetris-host`).

## Facts learned the hard way

- **The device build flashed and closed instantly ("it starts then closes").**
  The real-Kindle loop is `while (running && !stop_req)`, but `static int
  running;` was declared with no initializer and only ever set to `0` (QUIT), so
  the loop was skipped entirely: the game took over the screen, `ek_close()` ran
  and the launcher restored the reader — all within ~15s. Fixed with `running =
  1;` in `reset_game()`. **`make tetris-test` cannot catch this**: the host build
  compiles the `#ifdef EK_HOST` pipe/interactive path, never the `#else` device
  event loop, so a device-loop bug is invisible to every host check. Verify any
  change to the `#else` loop on the real Kindle.

- **The auto-player silently played nothing.** `bot_move()` used `best` (the
  placement *score*) as its "found a placement" sentinel: `best = -1`, then
  `if (best < 0) return;`. But the score is `full*300 - holes*120 - bumps*6 -
  (y*2)` and on an empty board the `-(y*2)` floor-height penalty makes every
  legitimate placement **negative** (e.g. -48). So the bot bailed on every move,
  the well stayed empty, and `make tetris-test` failed with "playing added no
  blocks" while `check_pgm` still said the pictures differ (the HUD/next box
  changed). Fixed with a separate `found` flag; never use a possibly-negative
  value as a "was anything found" sentinel.

- **A tap's button flash was invisible in one frame.** A tap is a press + a lift
  that can both land in a single `ek_poll` drain; drawing the invert on press and
  un-invert on lift cancelled out before `present()`. The flash is now timed
  (`btn_flash[i] = ek_ms() + 140`, `expire_buttons()` in `present()`), so the
  button lights up for a moment whether or not a separate lift ever arrives.

- **`N_ACT` was used before it was declared.** `draw_chrome()` loops `i < N_ACT`
  but the enum sat lower in the file. C wants the enum above first use — move the
  button constants (enum, `BTN[]`, `BTN_LABEL[]`) above the drawing code.

- **`tools/check_tetris.py` well-frame coordinates.** `draw_well_frame` paints a
  black ring from `(WELL_X-4, WELL_Y-4)` to `(WELL_X+COLS*CELL+4, ...)`, then a
  paper hole 2px in. So the black rails are at `WELL_X-4` / `WELL_X+COLS*CELL+2`
  across and `WELL_Y-4` / `WELL_Y+ROWS*CELL+2` down — **not** at the well's own
  edge (`WELL_X+COLS*CELL`), which is paper. Sampling at the edge makes a correct
  frame look un-inked.

- **Self-test assertions can be wrong, not the code.** Two early failures were
  bad expectations: a cleared row's own contents are gone (check the row above
  *fell* into it, not that the cleared row "stays put"), and the I turns to rot 2
  from rot 1 (check flatness `SH[0][cur_rot].h == 1`, not `cur_rot == 0`).

## Layout (keep `check_tetris.py` in step with `src/tetris.c`)

`COLS 10, ROWS 20, CELL 30`; `WELL_X 152, WELL_Y 44`; buttons `BTN_W 186` at
`BTN_Y1 656` / `BTN_Y2 726`. Host stdin is `tap X Y` lines; `--dump FILE` writes
the 600x800 PGM. `tools/pgm2png.py` turns a dump into a viewable PNG.
