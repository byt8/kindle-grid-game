# Area: sudoku

9x9 Sudoku on the Kindle Touch 600x800 e-ink panel: the menu's fourth game
(rc 4 from `src/menu.c`), single-file C on the shared e-ink engine, same
conventions as tetris/chess (host build reads `tap X Y` from stdin and
`--dump` writes a PGM; ARM build is static musl via docker `kfb-build`).

## How it works

- `src/sudoku.c` — one file, ~820 lines. Board top at y~110, 60px cells with
  thick 3x3 box lines and thin cell lines, three chips (EASY/MED/HARD), two
  action rows (ERASE/HINT/NEW + QUIT/SURE prompt), digit row 1-9 at the
  bottom. Coordinates (`X0/Y0/CELL/BOX_*`) are mirrored in
  `tools/check_pgm.py`.
- Generator: seedable xorshift rng; fills a random completed grid via
  backtracking with a shuffled digit order, then carves out a symmetric
  (180°) pattern until `81 - clues` removed, re-checking unique solution at
  every removal by solving with a two-solution cap. Clue targets:
  EASY 33, MED 29, HARD 25. Seed via `--seed N`; default seeds from the clock.
- State: `given[r][c]` (clues), `cell[r][c]` (typed), `err[r][c]`, `n_filled`,
  `sel_r/sel_c`, `win`, `quit_armed`. `drawn_style[][]` diffing drives
  incremental repaint; only the tapped button inverts (a 140 ms flash).
  No EK_UP handling — taps are single EK_DOWN events, matching host stdin.
- HINT writes the answer into the selection and boxes it so it reads as
  filled-by-hint; ERASE only clears non-given cells. WIN fires when all 81
  are set with no conflicts: "PUZZLE DONE" card.
- Difficulty change or NEW re-runs the generator from the same seed state
  (NEW keeps the same puzzle; changing difficulty makes a new one).
- CLI: `--selftest` (rule/board/verify checks, ~30 asserts), `--auto N`
  (plays N moves), `--dump FILE`, `--seed N`, `--stats`, `--clean`/`--no-wait`.

## Interaction with the other games

- Menu rc 4: `tools/menu-boot.sh` launches `GAME=sudoku sh /mnt/us/sudoku-run.sh`.
- Runner: `run-sudoku-on-kindle.sh` — the standard handoff (push binary +
  `tools/kindle-run.sh` as `/mnt/us/sudoku-run.sh`, stop mirror, run, restore
  Kindle UI on exit). Same as run-chess/tetris.
- Installer: `make menu chess tetris sudoku grabtest` then `make kindle`, and
  the ARM binary is committed (like chess/tetris) so installs need no
  cross-compiler.

## Key gotchas

- The unique-solution check (count solves up to 2) runs at every carve, so
  generation is fast enough even at HARD (25 clues).
- HINT is implemented as an overwrite of `cell[]` + a box drawn in `err[]`
  style — the digit row stays enabled; the box makes the hint visually
  distinct from a typed entry.
- Host stdin taps are parsed as `tap X Y`; the host build must draw the same
  geometry as the device or the checker-script taps miss.

## Status

Verified: `make sudoku-test` (selftest + check_pgm picture walk) and menu
walk (`make menu-test`) pass; ARM build clean. Not yet played on a real
Kindle (devices were offline; `run-sudoku-on-kindle.sh 192.168.0.66` next
session).