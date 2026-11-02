# Index

Read this before exploring. Then open at most three area files whose Summary or Keywords match the task. Do not read the rest of `.agent-kb`.

| Area | File | Summary | Keywords |
| --- | --- | --- | --- |
| xcom | areas/xcom.md | Turn-based fight in C for the Kindle Touch 600x800 (shared e-ink engine, true 16-level grey — never dither), now a **four-mission campaign** (M1..M4 boards, `--mission`, `MISSIONS[]`/`ALIENS[][]`, terrain `b`/`M`). | xcom, kindle, eink, sprites, touch, xcom-host, run-on-kindle, campaign, mission, advance_level, M1, M4, bush, mountains, greyscale, grayscale, grey, dither, 1-bit, ek_grey, option B, shared engine, submodule, constituent, ask first, force monochrome |
| chess | areas/chess.md | You are White on a Kindle Touch 600x800; DeepSeek plays Black over its HTTP API and taunts you. Rules, notation and drawing verified by --selftest and --fake-ai. | chess, deepseek, perft, api, curl, chess-host, run-chess-on-kindle |
| tetris | areas/tetris.md | 10x20 block-stacking on the 600x800 e-ink screen with a landing ghost and a one-ply auto-player. Rules and drawing verified by --selftest and --auto dumps plus check_tetris. | tetris, blocks, ghost, auto-player, check-tetris, tetris-host, run-tetris-on-kindle |
| sudoku | areas/sudoku.md | 9x9 Sudoku on the 600x800 e-ink screen: generated puzzles with unique solutions, EASY/MED/HARD, hints, tap-only UI. Verified by --selftest and check_pgm picture dumps. | sudoku, puzzles, generator, unique-solution, hint, check-pgm, sudoku-host, run-sudoku-on-kindle |
| rockman | areas/rockman.md | Playable 1984 cave digger in src/rockman.c. Rules taken from the asm, checked by make rockman-test. No sound, not yet on a device. | rockman, boulder-dash, vic20, rockman-host, rockman-test, run-rockman-on-kindle |
| boot-splash | areas/boot-splash.md | The frame before the boot PNG is a 2-bit logo gzipped inside both kernel images; later screens and the sleep slideshow are PNGs on the read-only root. | splash, kernel, uImage, screensaver, blanket, mntroot |
| menu | areas/menu.md | The Kindle boots into a seven-button launcher (XCOM/CHESS/TETRIS/SUDOKU/ROCKMAN/WIFI/KINDLE). ROCKMAN is exit 5, WIFI is 6, KINDLE is 0. | menu, launcher, menu-boot, menu.conf, wifi-only, rockman |
