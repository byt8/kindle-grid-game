# SimCity port

Status: **POC screen only** — no game code yet.

## What exists
- `poc/render_poc.py` — draws a fake 600x800 e-ink screen (header, 20x21 map, status strip, 8-tool toolbar) from the classic Micropolis tileset. Output: `poc/screen_600x800_grey.png`.
- `poc/view.html` — viewer for the mock (serve with `python3 -m http.server 8765 --directory poc`, service `poc-viewer`).
- Tile crops `poc/src_*.png` (water, water_edge, grass, tree, road_h/v, residential, commercial, industrial, power, park, barren) cut from `upstream/tiles.bmp` (fetched from SimHacker/MicropolisCore `content/micropolis/tilesets/classic/tiles.bmp`).
- Makefile `poc:` target runs the render.
- Upstream sim core to vendor: `SimHacker/MicropolisCore` — engine at `patches/simcore/`, class `SimCity`, needs zlib.

## Contrast: the stretch approach FAILED, use per-tile-type tone targets
First attempt (rejected by the user as "horrendous"): **per-tile contrast stretch** —
remap each 16px tile's own min..max to the full grey band 2..13. Two reasons it fails:
1. It **amplifies noise** — a nearly-flat tile's 1-2 level texture jitter spreads over
   11 levels → speckle. Measured 11% isolated strong-contrast pixels on water.
2. The classic palette separates water/grass by **HUE**, which greyscale destroys —
   stretching doesn't create the missing separation, it just re-labels mid-grey.

What works (current POC, measured: grass mean 177, water 110, road 122, speckle ~0-1%):
**give each tile type its own target darkness band** (`TONE` dict in render_poc.py) and
remap the tile's own luma range *into that band*: grass light (2..7), water mid-dark
(7..11, keep the band narrow — it is the largest area on screen), road near-black with
light dashes (4..13). If a tile's raw range is <3 levels, fill one flat tone (no noise).
Global stretch never worked either (everything already spans black..white via road lines).

## Engine grey polarity (this cost a whole broken render)
eink engine `grey_ramp[16]`: **level 0 = paper (byte 255), level 15 = ink (byte 0)** —
higher level = darker. The POC first converted luma→level the naive way (white→15) which
inverts everything: header band came out black-but-labelled-paper, status text rendered
paper-on-paper (invisible). Correct: `level = 15 - luma/17`.

## Shape of the 600x800 screen
Fixed layout in `render_poc.py`: header 56px (black band, paper text) → map 56..686
(21 rows x 20 cols, 30px cells) → status strip 686..736 → toolbar 736..800 (8 slots of
75px, 56px icon boxes, selected = inverted). 1px grid lines at level 13 between cells so
it reads as a grid game. Fonts: DejaVuSans-Bold 26/22/34 render fine (no missing glyphs).

## Next steps
1. Vendor `MicropolisCore` core (C++), build against the ARM cross container (kfb-build) or as a host binary.
2. Replace fake map with real `SimCity` simulation state; draw from the tile map each tick.
3. Decide input model (tap tile = tool) using the existing menu/touch conventions.
4. If classic tiles still read poorly on device, fall back to the XCOM pattern-terrain
   convention: "terrain is a pattern, not a picture" (dots=grass, waves=water) — proven
   readable on this panel (see areas/xcom.md).
