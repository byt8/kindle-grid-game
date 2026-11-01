# Index

Read this before exploring. Then open at most three area files whose Summary or Keywords match the task. Do not read the rest of `.agent-kb`.

| Area | File | Summary | Keywords |
| --- | --- | --- | --- |
| xcom | areas/xcom.md | One-mission turn-based fight in C for the Kindle Touch 600x800, drawn by the shared e-ink engine; true greyscale (option B) is approved: opt-in `ek_grey()` in the shared engine, ask the user before editing it, never fall back to dither. | xcom, kindle, eink, sprites, touch, xcom-host, run-on-kindle, greyscale, grayscale, grey, dither, 1-bit, ek_grey, ek_mono, expand_lut, option B, shared engine, submodule, constituent, ask first, force monochrome |
| chess | areas/chess.md | You are White on a Kindle Touch 600x800; DeepSeek plays Black over its HTTP API and taunts you. Rules, notation and drawing verified by --selftest and --fake-ai. | chess, deepseek, perft, api, curl, chess-host, run-chess-on-kindle |
| tetris | areas/tetris.md | 10x20 block-stacking on the 600x800 e-ink screen with a landing ghost and a one-ply auto-player. Rules and drawing verified by --selftest and --auto dumps plus check_tetris. | tetris, blocks, ghost, auto-player, check-tetris, tetris-host, run-tetris-on-kindle |
| sudoku | areas/sudoku.md | 9x9 Sudoku on the 600x800 e-ink screen: generated puzzles with unique solutions, EASY/MED/HARD, hints, tap-only UI. Verified by --selftest and check_pgm picture dumps. | sudoku, puzzles, generator, unique-solution, hint, check-pgm, sudoku-host, run-sudoku-on-kindle |
| rockman | areas/rockman.md | 1984 VIC-20 Boulder-Dash-style cave digger; ruleset in docs/ROCKMAN_SPEC.md, data + charset machine-extracted by tools/extract_rockman.py into src/rockman_data.h; no port code yet. | rockman, boulder-dash, vic20, extraction, spec, kindle, extractor |
| boot-splash | areas/boot-splash.md | The frame before the boot PNG is a 2-bit logo gzipped inside both kernel images; later screens and the sleep slideshow are PNGs on the read-only root. | splash, kernel, uImage, screensaver, blanket, mntroot |
| menu | areas/menu.md | The Kindle boots into a five-button launcher (XCOM/CHESS/TETRIS/WIFI/KINDLE) instead of the Amazon UI; the menu-boot session owns the panel all boot. | menu, launcher, menu-boot, menu.conf, wifi-only |
