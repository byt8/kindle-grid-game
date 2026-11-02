#!/usr/bin/env python3
"""Show every stage between the art in bold/ and the panel.

The sheets look fine and the game looks wrong because the baker runs a few
steps on the way from a 512x512 cell to a 40x40 tile: a threshold for the
bounding box, an area-resize into a 36x34 box, a silhouette mask that says
which pixels are background, and centring. This prints those stages side by
side, magnified with no smoothing, plus a mock of how the tile lands on the
map (terrain, paper rim, team badge, HP bar) so feedback can name the step
that broke the sprite instead of the result.

The picture keeps its tone the whole way. The last three columns are drawn
the way the panel draws them: the tone is turned into the same 4x4 Bayer
dither ek_dither_rect() will lay down, because that is all a 1-bit canvas
can carry.

    python3 tools/sprite_stages.py                     # -> sprite-stages.png
    python3 tools/sprite_stages.py --only soldier      # rows matching "soldier"
    python3 tools/sprite_stages.py --mag 6 --out /tmp/s.png
"""
import argparse
import importlib.util
import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "bold_sprite_sources.json"

TILE = 40
TRANSPARENT, PAPER = 0, 1
INK = 9                            # solid ink, see bake_bold_sprites.pack()

LEFT = 240          # label column: sprite id, tone, and the 1:1 tile
COLW = 168
ROWH_EXTRA = 14
LEGEND_H = 76
HEADER_H = 34
DIVIDER_H = 22

COLUMNS = ["source art", "resized to fit", "silhouette mask",
           "tile 40x40", "in game \u00b7 grass", "in game \u00b7 forest"]


def die(msg):
    print("sprite_stages: " + msg, file=sys.stderr)
    raise SystemExit(1)


def load_baker():
    """Import the baker so the diagnostic uses its code, not a copy of it."""
    path = ROOT / "tools" / "bake_bold_sprites.py"
    spec = importlib.util.spec_from_file_location("bake_bold_sprites", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# The baker is imported once at module level: every renderer below has to
# use its constants (tone values, the Bayer matrix) rather than copy them.
bake = load_baker()


def load_font():
    try:
        return ImageFont.load_default(size=13)
    except TypeError:                      # older Pillow
        return ImageFont.load_default()


# --------------------------------------------------------------------------
# Mock of src/xcom.c's drawing, on one 40x40 buffer.
# --------------------------------------------------------------------------

def fill_rect(px, x, y, w, h, v):
    for yy in range(max(0, y), min(TILE, y + h)):
        for xx in range(max(0, x), min(TILE, x + w)):
            px[yy * TILE + xx] = v


def rect(px, x, y, w, h, v):
    """1 px outline, same as ek_rect."""
    if w <= 0 or h <= 0:
        return
    fill_rect(px, x, y, w, 1, v)
    fill_rect(px, x, y + h - 1, w, 1, v)
    fill_rect(px, x, y, 1, h, v)
    fill_rect(px, x + w - 1, y, 1, h, v)


def sprite_on(spr, x, y):
    if x < 0 or y < 0 or x >= TILE or y >= TILE:
        return 0
    return 1 if spr[y * TILE + x] != TRANSPARENT else 0


def blit_unit(dst, spr):
    """blit_unit(): paper rim first, then the picture. See src/xcom.c."""
    for r in range(TILE):
        for c in range(TILE):
            if sprite_on(spr, c, r):
                continue
            if (sprite_on(spr, c - 1, r) or sprite_on(spr, c + 1, r)
                    or sprite_on(spr, c, r - 1) or sprite_on(spr, c, r + 1)):
                dst[r * TILE + c] = PAPER
    for i, v in enumerate(spr):
        if v != TRANSPARENT:
            dst[i] = v


def blit_plain(dst, spr):
    """blit_sprite(): no rim, no badge, no HP bar. Map furniture only."""
    for i, v in enumerate(spr):
        if v != TRANSPARENT:
            dst[i] = v


def draw_badge(dst, team_you):
    """draw_badge(): 8x8 in the tile's top-right corner."""
    x, y = TILE - 11, 2
    if team_you:
        fill_rect(dst, x, y, 8, 8, PAPER)
        rect(dst, x, y, 8, 8, INK)
        rect(dst, x + 2, y + 2, 4, 4, INK)
    else:
        fill_rect(dst, x, y, 8, 8, INK)
        rect(dst, x + 2, y + 2, 4, 4, PAPER)


def draw_hp(dst, frac=1.0):
    """draw_hp(): a 34x4 bar 6 px off the bottom, so it covers the feet."""
    bw = TILE - 6
    hy = TILE - 6
    filled = int(bw * frac)
    fill_rect(dst, 3, hy, bw, 4, PAPER)
    if filled > 0:
        fill_rect(dst, 3, hy, filled, 4, INK)
    rect(dst, 3, hy, bw, 4, INK)


def mock_unit(spr, terrain, team_you):
    out = bytearray(terrain)
    blit_unit(out, spr)
    draw_badge(out, team_you)
    draw_hp(out)
    return out


def mock_object(spr):
    out = bytearray([PAPER] * (TILE * TILE))
    blit_plain(out, spr)
    return out


# --------------------------------------------------------------------------
# Renderers
# --------------------------------------------------------------------------

def r_art(buf, w, h, cell):
    im = Image.frombytes("L", (w, h), bytes(buf))
    im.thumbnail((cell, cell), Image.BOX)
    return im.convert("RGB")


def r_grey(buf, w, h, mag):
    im = Image.frombytes("L", (w, h), bytes(buf))
    return im.resize((w * mag, h * mag), Image.NEAREST).convert("RGB")


def r_mask(buf, w, h, mag):
    im = Image.frombytes("L", (w, h), bytes(0 if v else 255 for v in buf))
    return im.resize((w * mag, h * mag), Image.NEAREST).convert("RGB")


def r_tile(buf, mag, panel=False):
    """A 40x40 tile, either as stored or as the panel will show it.

    panel=False: the tone as stored, so the grey that reaches sprites.h is
    readable. panel=True: the grey ek_grey_rect() puts on the panel after
    ink15() has scaled the level; transparency stays mid grey so the
    silhouette edge is still visible over terrain.
    """
    data = []
    for y in range(TILE):
        for x in range(TILE):
            v = buf[y * TILE + x]
            if v == TRANSPARENT:
                data.append(190)
            elif panel:
                data.append(bake.GREY_RAMP[bake.ink15(v - PAPER)])
            else:
                data.append(255 - (v - PAPER) * 255 // 8)
    im = Image.new("L", (TILE, TILE))
    im.putdata(data)
    if mag != 1:
        im = im.resize((TILE * mag, TILE * mag), Image.NEAREST)
    return im.convert("RGB")


def put(canvas, draw, x, y, w, h, img, font):
    canvas.paste(img, (x + (w - img.width) // 2, y + (h - img.height) // 2))


def centre_text(draw, x, y, w, h, text, font, fill=(140, 140, 140)):
    tw = draw.textlength(text, font=font)
    draw.text((x + (w - tw) // 2, y + (h - 14) // 2), text, font=font, fill=fill)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="sprite-stages.png")
    ap.add_argument("--only", default="", help="keep rows whose id contains this")
    ap.add_argument("--mag", type=int, default=4, help="magnification of the small stages")
    a = ap.parse_args()

    mag = a.mag
    cell = TILE * mag
    colw = max(COLW, cell + 8)
    rowh = cell + ROWH_EXTRA
    ncol = len(COLUMNS)
    width = LEFT + ncol * colw

    manifest = json.loads(MANIFEST.read_text())
    font = load_font()

    art_rows, terrain_rows = [], []
    for sheet in manifest["sheets"]:
        cols, rws = sheet["grid"]
        path = ROOT / sheet["file"]
        for i, sid in enumerate(sheet["sprites"]):
            if a.only and a.only not in sid:
                continue
            g, gw, gh = bake.load_cell(path, cols, rws, i)
            trace = {}
            tile = bake.cell_to_tile(g, gw, gh, trace=trace)
            if tile is None:
                die(f"{sid} produced no tile")
            body = sum(1 for v in tile if v != bake.TRANSPARENT)
            tone = sum(v - bake.PAPER for v in tile if v > bake.PAPER)
            art_rows.append({
                "sid": sid, "tile": tile, "trace": trace,
                "pct": 100 * tone // (8 * body) if body else 0,
                "unit": sid.startswith(("player_units/", "enemies/")),
                "team_you": sid.startswith("player_units/"),
                "terrain": bake.PATTERNS["terrain_tiles/grass"](),
            })

    for sid, fn in bake.PATTERNS.items():
        if a.only and a.only not in sid:
            continue
        terrain_rows.append({"sid": sid, "tile": fn()})

    rows = art_rows + terrain_rows
    if not rows:
        die("nothing matched --only " + repr(a.only))

    legend = [
        "XCOM sprite pipeline: bold/ art  ->  tools/bake_bold_sprites.py  ->  src/sprites.h  ->  16-level grey e-ink panel.",
        "The picture keeps its tone: crop to the bounding box, area-resize into <=36x34, flood the background out, centre. "
        "Nothing thresholds the picture, dilates it or hollows it -- friendlies and hostiles are told apart by silhouette and badge.",
        "Columns 2-3 are the real working size (<=36x34) blown up %dx with nearest-neighbour, no smoothing. "
        "Column 4 is the stored tone; the last two are the same tile as the panel actually draws it, greys and all. The 40x40 beside each name is actual panel size." % mag,
        "Objects replace their map tile outright (no rim, no badge, no HP bar); the forest column is blank for them on purpose.",
    ]
    legend_h = 14 + len(legend) * 17
    header_y = legend_h
    body_y = header_y + HEADER_H

    height = body_y + len(art_rows) * rowh
    if terrain_rows:
        height += DIVIDER_H + len(terrain_rows) * rowh

    img = Image.new("RGB", (width, height), (255, 255, 255))
    draw = ImageDraw.Draw(img)

    for i, line in enumerate(legend):
        draw.text((8, 8 + i * 17), line, font=font, fill=(60, 60, 60))

    for i, name in enumerate(COLUMNS):
        x = LEFT + i * colw
        draw.text((x + 6, header_y + 8), name, font=font, fill=(0, 0, 0))
    draw.text((8, header_y + 8), "sprite (and its true 40x40 size)", font=font, fill=(0, 0, 0))
    draw.line([(0, header_y + HEADER_H - 2), (width, header_y + HEADER_H - 2)],
              fill=(0, 0, 0), width=2)
    draw.line([(LEFT - 6, 0), (LEFT - 6, height)], fill=(200, 200, 200), width=1)

    def draw_art_row(y, row):
        tr = row["trace"]
        cells = []
        _b, w, h = tr["src"]
        cells.append(("art", r_art(tr["src"][0], w, h, cell)))
        b, w, h = tr["small"]
        cells.append(("small", r_grey(b, w, h, mag)))
        b, w, h = tr["mask"]
        cells.append(("mask", r_mask(b, w, h, mag)))
        cells.append(("tile", r_tile(row["tile"], mag)))
        if row["unit"]:
            grass = mock_unit(row["tile"], row["terrain"], row["team_you"])
            forest = mock_unit(row["tile"], bake.PATTERNS["terrain_tiles/forest"](),
                               row["team_you"])
            cells.append(("grass", r_tile(grass, mag, panel=True)))
            cells.append(("forest", r_tile(forest, mag, panel=True)))
        else:
            cells.append(("grass", r_tile(mock_object(row["tile"]), mag, panel=True)))
            cells.append(("forest", None))   # objects replace the tile; no terrain

        draw.text((8, y + 8), row["sid"], font=font, fill=(0, 0, 0))
        draw.text((8, y + 26), f"{row['pct']}% ink", font=font, fill=(120, 120, 120))
        img.paste(r_tile(row["tile"], 1), (LEFT - 56, y + 6))

        for i, (key, im) in enumerate(cells):
            x = LEFT + i * colw
            if im is None:
                centre_text(draw, x, y, colw, cell, "\u2014", font)
            else:
                put(img, draw, x, y, colw, cell, im, font)

    def draw_terrain_row(y, row):
        draw.text((8, y + 8), row["sid"], font=font, fill=(0, 0, 0))
        draw.text((8, y + 26), "pattern", font=font, fill=(120, 120, 120))
        img.paste(r_tile(row["tile"], 1), (LEFT - 56, y + 6))
        put(img, draw, LEFT + 4 * colw, y, colw, cell, r_tile(row["tile"], mag, panel=True), font)
        put(img, draw, LEFT + 5 * colw, y, colw, cell, r_tile(row["tile"], mag, panel=True), font)

    y = body_y
    for row in art_rows:
        draw_art_row(y, row)
        y += rowh
        draw.line([(0, y - 4), (width, y - 4)], fill=(225, 225, 225), width=1)

    if terrain_rows:
        draw.text((8, y + 2), "terrain \u2014 code patterns, never art", font=font, fill=(0, 0, 0))
        y += DIVIDER_H
        for row in terrain_rows:
            draw_terrain_row(y, row)
            y += rowh

    out = Path(a.out)
    if not out.is_absolute():
        out = ROOT / out
    img.save(out)
    print(f"wrote {out} ({width}x{height}, {len(art_rows)} art + "
          f"{len(terrain_rows)} terrain, {len(COLUMNS)} stages, mag {mag})")


if __name__ == "__main__":
    main()
