#!/usr/bin/env python3
"""Bake chess piece art (chess-art/*.png) into the grey-ramp sprite data the
game draws, following the PvZ baked-sprite pipeline but keying the silhouette
off the PNG alpha channel instead of a luminance threshold, so white pieces
(whose only ink is dark shading) bake correctly too.

Output:
  src/chess_pieces.h      static sprite data + accessor (do not edit)

The consumer (src/chess.c) draws sprites itself via ch_at() and the grey
layer, so no separate blit .c is emitted.

Each piece bakes to a CHESS_TILE x CHESS_TILE tile (52 px = the on-board draw
size). 4 bits per pixel: 0 transparent, 1 paper, 2..9 an ink whose value minus
1 is the engine's 0..15 grey level. The shading inside the silhouette is the
art's luminance, normalised per piece so both white and black pieces get the
same contrast. Captured/promotion sprites (drawn smaller) are scaled down at
draw time from this 52 px tile.

Usage: bake_chess_sprites.py [--preview out.png]
"""
import json
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "chess_sprite_sources.json"
OUT_H = ROOT / "src" / "chess_pieces.h"

TILE = 52
FIT_W = FIT_H = 52          # bounding box for the piece, aspect preserved
TRANSPARENT, PAPER = 0, 1
TONE_MAX = 9                # paper(1) .. ink(9)
INKS = TONE_MAX - PAPER     # 8 ink levels
ALPHA_CUT = 100

GREY_RAMP = (255, 238, 221, 204, 187, 170, 153, 136,
             119, 102, 85, 68, 51, 34, 17, 0)


def die(msg):
    print("bake_chess_sprites:", msg, file=sys.stderr)
    sys.exit(1)


def bbox(mask, w, h):
    x0, y0, x1, y1 = w, h, -1, -1
    for y in range(h):
        row = y * w
        for x in range(w):
            if mask[row + x]:
                if x < x0:
                    x0 = x
                if y < y0:
                    y0 = y
                if x > x1:
                    x1 = x
                if y > y1:
                    y1 = y
    if x1 < x0:
        return None
    return x0, y0, x1 - x0 + 1, y1 - y0 + 1


def image_to_tile(path):
    try:
        img = Image.open(path).convert("RGBA")
    except OSError as e:
        die(f"cannot read {path}: {e}")
    w, h = img.size
    lum_full = img.convert("L")
    alpha = img.getchannel("A")
    mask_full = bytearray(1 if a >= ALPHA_CUT else 0 for a in alpha.getdata())
    if sum(mask_full) == 0:
        die(f"{path.name} has no opaque pixels above the alpha cut")
    box = bbox(mask_full, w, h)
    bx, by, bw, bh = box
    s = min(FIT_W / bw, FIT_H / bh)
    dw = max(1, min(FIT_W, int(round(bw * s))))
    dh = max(1, min(FIT_H, int(round(bh * s))))

    lum_crop = lum_full.crop((bx, by, bx + bw, by + bh))
    alp_crop = alpha.crop((bx, by, bx + bw, by + bh))
    small_lum = lum_crop.resize((dw, dh), Image.LANCZOS)
    small_alpha = alp_crop.resize((dw, dh), Image.LANCZOS)
    sl = small_lum.load()
    sa = small_alpha.load()

    vals = [sl[x, y] for y in range(dh) for x in range(dw)
            if sa[x, y] >= ALPHA_CUT]
    if not vals:
        die(f"{path.name} lost its silhouette when scaled to {dw}x{dh}")
    hi, lo = max(vals), min(vals)
    span = hi - lo

    tile = bytearray(TILE * TILE)
    ox = (TILE - dw) // 2
    oy = (TILE - dh) // 2
    for y in range(dh):
        for x in range(dw):
            if sa[x, y] < ALPHA_CUT:
                continue
            L = sl[x, y]
            if span > 30:
                level = ((hi - L) * INKS + span // 2) // span
            else:
                level = (255 - L) * INKS // 255
            if level < 0:
                level = 0
            elif level > INKS:
                level = INKS
            tile[(oy + y) * TILE + ox + x] = PAPER + level
    return tile


def pack(px):
    if len(px) != TILE * TILE:
        die(f"tile has {len(px)} pixels, want {TILE * TILE}")
    out = bytearray(TILE * TILE // 2)
    for i, v in enumerate(px):
        if v < 0 or v > TONE_MAX:
            die(f"pixel value {v} is not 0..{TONE_MAX}")
        out[i >> 1] |= v << ((i & 1) * 4)
    for i, v in enumerate(px):
        if (out[i >> 1] >> ((i & 1) * 4)) & 15 != v:
            die("pack/unpack mismatch")
    return out


def level15(v):
    return (v - PAPER) * 15 // (TONE_MAX - 1)


def write_preview(path, order, tiles):
    """Each piece on a light square and on a dithered dark square, at the exact
    size and grey the panel will draw, so the bake can be eyeballed."""
    cols = 6
    sq = 68
    cell_h = 92
    rows = (len(tiles) + cols - 1) // cols
    pw, ph = cols * 2 * sq, rows * (cell_h + 8) + 20
    img = Image.new("RGB", (pw, ph), (70, 70, 70))
    px = img.load()
    for i, (key, t) in enumerate(zip(order, tiles)):
        cy = 20 + (i // cols) * (cell_h + 8)
        for sqi in range(2):
            cx = (i % cols) * 2 * sq + sqi * sq
            for yy in range(sq):
                for xx in range(sq):
                    if (xx + yy) & 1:
                        px[cx + xx, cy + yy] = (205, 205, 205)
                    else:
                        px[cx + xx, cy + yy] = (24, 24, 24)
            ox = cx + (sq - TILE) // 2
            oy = cy + (sq - TILE) // 2
            for y in range(TILE):
                for x in range(TILE):
                    v = t[y * TILE + x]
                    if v == TRANSPARENT:
                        continue
                    g = GREY_RAMP[level15(v)]
                    px[ox + x, oy + y] = (g, g, g)
    img.save(path)


def main():
    preview = None
    args = list(sys.argv[1:])
    if args[:1] == ["--preview"] and len(args) == 2:
        preview = Path(args[1])
        args = []
    elif args:
        die("usage: bake_chess_sprites.py [--preview out.png]")

    manifest = json.loads(MANIFEST.read_text())
    pieces = manifest["pieces"]
    tiles = []
    order = []
    for p in pieces:
        path = ROOT / p["file"]
        if not path.exists():
            die(f"missing sprite image {path}")
        tile = image_to_tile(path)
        body = sum(1 for v in tile if v != TRANSPARENT)
        if body < 8:
            die(f"{p['color']} {p['type']} baked to only {body} opaque px")
        tone = sum(v - PAPER for v in tile if v > PAPER)
        pct = 100 * tone // (INKS * body) if body else 0
        tiles.append(tile)
        order.append((p["color"], p["type"]))
        print(f"{p['color']} {p['type']}: {tone} tone-px, {pct}% ink")

    n = len(order)
    enumc = [("CHS_%s%s" % ("W" if c == "white" else "B", t.upper()))
             for (c, t) in order]

    H = []
    H.append("/* Generated by tools/bake_chess_sprites.py. Do not edit. */")
    H.append("#ifndef CHESS_PIECES_H")
    H.append("#define CHESS_PIECES_H")
    H.append(f"#define CHESS_TILE {TILE}")
    H.append(f"#define CHESS_N {(TILE * TILE) // 2}")
    H.append(f"#define CHESS_COUNT {n}")
    H.append("#define CHESS_PAPER 1")
    H.append(f"#define CHESS_TONE_MAX {TONE_MAX}")
    H.append("enum {")
    for i, en in enumerate(enumc):
        comma = "," if i + 1 < n else ""
        H.append(f"    {en} = {i}{comma}")
    H.append("};")
    H.append("static const unsigned char ch_px[CHESS_COUNT][CHESS_N] = {")
    for t in tiles:
        blob = pack(t)
        H.append("    {" + ",".join(f"0x{b:02x}" for b in blob) + "},")
    H.append("};")
    H.append("/* 4 bits per pixel: 0 transparent, 1 paper, 2..9 an ink whose")
    H.append(" * value minus 1 is the engine's 0..15 grey level. */")
    H.append("static int ch_at(int id, int x, int y) {")
    H.append("    int i = y * CHESS_TILE + x;")
    H.append("    if (id < 0 || id >= CHESS_COUNT || i < 0 || i >= CHESS_TILE * CHESS_TILE)")
    H.append("        return 0;")
    H.append("    return (ch_px[id][i >> 1] >> ((i & 1) * 4)) & 15;")
    H.append("}")
    H.append("#endif")
    OUT_H.write_text("\n".join(H) + "\n")

    if preview:
        write_preview(preview, order, tiles)
        print(f"preview written to {preview}")


if __name__ == "__main__":
    main()