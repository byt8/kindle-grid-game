# SimCity port

Status: **POC screen only** — no game code yet.

## What exists
- `poc/render_poc.py` — draws a fake 600x800 e-ink screen (header, 20x14 map, toolbars) from the classic Micropolis tileset, bakes to 4-bit grey. Outputs `poc/screen_600x800_grey.png` (16-level) and `poc/screen_600x800_ink.png` (true 2-bit ink toolbar).
- `poc/view.html` — side-by-side viewer (serve with `python3 -m http.server 8765 --directory poc`).
- `Makefile` `poc:` target; `upstream/tiles.bmp` fetched from SimHacker/MicropolisCore `content/micropolis/tilesets/classic/tiles.bmp` (via curl).
- Upstream sim core to vendor: `SimHacker/MicropolisCore` (C++, SDL) — engine lives at `patches/simcore/` in the repo; the core class is `SimCity`, compiles with the bundled `simcore.c`/`simcity.c` + zlib.

## The contrast finding (why the tile layout works)
The classic Micropolis palette is **pastel**: naive luma → 4-bit grey lands EVERY tile at ~140–152 (mid-grey), so water/grass/road are indistinguishable. Fix used in the POC: **per-tile contrast stretch** — each tile's own min..max remapped to the visible grey band (levels 2..13 of 0..15). Result: water ~44 (dark), grass ~139 (light). A single global stretch across the whole map does NOT help (all tiles average mid-grey).

## Shape of the 600x800 screen
600 wide => 20 tiles of 30px, 14 rows of ~32px in the map area; header + bottom toolbar. All glyphs (toolbar "icons" + text) render fine in the engine font — no missing-glyph issue.

## Next steps
1. Vendor `MicropolisCore` core (C++), build against the ARM cross container (kfb-build) or as a host binary.
2. Replace fake map with real `SimCity` simulation state; draw from the tile map each tick.
3. Decide input model (tap tile = tool, etc.) using the existing menu/touch conventions.