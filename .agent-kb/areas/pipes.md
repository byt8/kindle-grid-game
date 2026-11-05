# Pipemania: a rotation puzzle on the Kindle

One file `src/pipemania.c` (~900 lines) on the shared `kframe`/`kshell` shell and
the fbdaemon engine. 600x800, 1-bit. Grid `GRID_X 30 / GRID_Y 92`, 10 cols x 7
rows of `CELL 54`, `GRID_W 540 / GRID_H 378`. HUD lines at `STAT_Y1/2`, big GO
button at `GO_X 24, GO_Y 640, GO_W 552, GO_H 76`.

## How it plays

Not a dispenser remake. The **whole pipeline already exists** from the IN spout
to the OUT drain, and most of it starts at a wrong angle.

- Tap a pipe: `mask[r][c] = rot90(mask[r][c])`, `turns++`, `drop_water()`. One
  small rect repaint per tap, which is why every move is cheap on e-ink.
- **RUN THE WATER** (`run_water`) floods from the spout. A win needs the water to
  reach the drain with `leaks == 0`. A dry tap on a non-pipe cell logs
  `pipemania: R,C is not a pipe` rather than doing nothing silently.
- Turning any pipe after a run throws that run away (`drop_water`): the water in
  the pipes is no longer where the pipes are.
- Menu (top-right hamburger, `kframe`): **NEW**, **LEVEL** (6 starts), **HELP**,
  plus VOLUME / WIFI / APPS / QUIT on the right. Card screen after a level clears.

## Board generation

`path_dfs()` walks a randomised DFS from spout to drain; pieces are laid along
that path into `sol_r/sol_c/sol_m`, then the board is scrambled. Safety net: if
the scrambled board already runs (a turned pipe pointing into the drain), rotate
the first path pipe until it does not, max 4 tries, then `exit(1)`. A level that
cannot be broken is a bug, not something to ship.

## Verified by

`make pipes-test`:

- `--selftest`: four turns put a piece back; a two-arm pipe changes on one turn;
  8 seeds x 6 levels all generate scrambled, the solution floods with no leak,
  the water wets exactly `sol_n` pipes (+1 for the spout cell), and turning the
  first or last path pipe stops the water. Prints
  `pipemania: 48 levels generated and solved`.
- `--dump` pgms: `/tmp/pipes-open.pgm`, `-turn.pgm`, `-wet.pgm`, `-menu.pgm`.
- `tools/check_pipes.py open.pgm wet.pgm`: grid ink must grow by >10% (wet pipes
  draw 18 wide vs 10, and the drain fills).
- Tap test asserts via `grep -q 'turns 2'`; the tap report is `#ifdef EK_HOST`
  only, so the glass is not spammed.

## Host test flags

`--seed N`, `--level N` (1..99), `--solve` (put every pipe back as the walk laid
it), `--turn N` (turn the first N pipes row by row), `--go` (run the water before
the first picture), `--dump FILE`, `--menu-shot FILE`, `--stats`, `--selftest`,
`--no-takeover`.

## Known traps

- Grid cells at `GRID_X + c*54 + 27, GRID_Y + r*54 + 27`. A tap outside a pipe
  silently does nothing but log; use the probe trick below to pick cells.
- On seed 7, row 0 col 0 and rows 0-2 col 0 are walls/empty — early taps there
  did nothing. Pipes confirmed at (3,3), (5,3): screen (219,281), (219,389).
- Probe cells without a debug flag:
  `for r/c: tap X Y` piped into `./pipes-host --no-takeover --seed 7 2>&1 >/dev/null | grep 'is not a pipe'`.
- `exit(1)` in the generator is deliberate: a broken level must be loud.

## Wiring a new game in (this is the checklist)

1. `src/pipemania.c` + Makefile targets `pipes`, `pipes-host`, `pipes-test`,
   `pipes-kindle`, and the `.PHONY` list.
2. `src/kframe.c` `APPS[]`: `{ "pipes", "PIPES", APP_VISIT }`.
3. `tools/menu-boot.sh`: `known_app` id, `runner_for`, `bin_for`, `launch_app`,
   the leftover-kill `for g in ...` loop, and the `.menu-handoff` `pidof` guard.
4. `tools/kindle-run.sh`: leftover-kill `for g in ...`.
5. `install-all-on-kindle.sh`: build list, `need pipes`, binary push loop, runner
   push loop.
6. `run-pipes-on-kindle.sh` (copy of the pvz runner with `GAME=pipes`).
7. `tools/check_pipes.py` + README summary/Quick Start/play section/`make -kindle`
   line.

## Runner

`run-pipes-on-kindle.sh [ip]` pushes `./pipes`, `grabtest`, `tools/kindle-run.sh`
as `/mnt/us/pipes-run.sh`, then `GAME=pipes sh /mnt/us/pipes-run.sh --detach`.
Build the binary first: `make pipes-kindle` (docker `kfb-build`, ARM static).
