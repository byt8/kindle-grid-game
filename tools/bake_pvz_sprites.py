#!/usr/bin/env python3
"""Bake PvZ sprites for the Kindle panel and write src/pvz_sprites.h.

The art is one 1024x1024 full-bleed image per sprite (no grid), named in
pvz-art/. The pipeline is the one tools/bake_bold_sprites.py proved on the
panel: crop to the silhouette's bounding box, area-resize into a 36x34 box
inside a 40x40 tile, and store the tone that comes out. The picture is
never thresholded into 1 bit -- at 40x40 a hard threshold throws away
exactly the grey an e-ink panel can show.

Two decisions do still need a binary image, and both are explicit, copying
the bold baker:

  - the silhouette mask. A flood fill from the border over the
    thresholded resize decides what is background vs body.
  - the ink guard. A sprite that comes out too faint stops the run.

A tile pixel is 0 transparent, 1 paper, 2..9 a tone where v-1 is the ink
level 0..8. pvz.c scales that level with its ink15() helper and hands it
to ek_grey_rect(), the engine's greyscale layer, so the tone stored here
is the tone the panel draws.

Anything declared in the manifest but missing, or present with no ink,
stops the run. Nothing is substituted.
"""
import json
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "pvz_sprite_sources.json"
OUT = ROOT / "src" / "pvz_sprites.h"

TILE = 40
FIT_W, FIT_H = 36, 34

TRANSPARENT, PAPER = 0, 1
BAYER_LEVELS = 8
TONE_MAX = PAPER + BAYER_LEVELS      # 9
INK = TONE_MAX

GREY_RAMP = (255, 238, 221, 204, 187, 170, 153, 136,
             119, 102, 85, 68, 51, 34, 17, 0)

THRESH = 120


def die(msg):
    print("bake_pvz_sprites: " + msg, file=sys.stderr)
    raise SystemExit(1)


def threshold(g, n):
    return bytearray(1 if v < THRESH else 0 for v in g[:n])


def outside_white(ink, w, h):
    """Flood fill white from the border. Everything white and reachable
    from the edge is background; white that the ink encloses is body."""
    seen = bytearray(w * h)
    stack = []
    for x in range(w):
        stack.append((x, 0))
        stack.append((x, h - 1))
    for y in range(h):
        stack.append((0, y))
        stack.append((w - 1, y))
    while stack:
        x, y = stack.pop()
        i = y * w + x
        if seen[i] or ink[i]:
            continue
        seen[i] = 1
        if x > 0:
            stack.append((x - 1, y))
        if x + 1 < w:
            stack.append((x + 1, y))
        if y > 0:
            stack.append((x, y - 1))
        if y + 1 < h:
            stack.append((x, y + 1))
    return seen


def shape_mask(ink, w, h):
    out = outside_white(ink, w, h)
    return bytearray(0 if out[i] else 1 for i in range(w * h))


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


def area_resize(g, w, h, dw, dh):
    out = bytearray(dw * dh)
    for y in range(dh):
        y0 = y * h / dh
        y1 = (y + 1) * h / dh
        iy0, iy1 = int(y0), max(int(y1), int(y0) + 1)
        if iy1 > h:
            iy1 = h
        for x in range(dw):
            x0 = x * w / dw
            x1 = (x + 1) * w / dw
            ix0, ix1 = int(x0), max(int(x1), int(x0) + 1)
            if ix1 > w:
                ix1 = w
            acc = 0
            n = 0
            for yy in range(iy0, iy1):
                base = yy * w
                for xx in range(ix0, ix1):
                    acc += g[base + xx]
                    n += 1
            if n == 0:
                die("resize box was empty")
            out[y * dw + x] = acc // n
    return out


def dilate(ink, w, h, passes):
    """Grow ink by `passes` pixels (4-neighbour). Guarantees no stroke ends
    up thinner than the outline the eye needs on a 40 px tile."""
    cur = ink
    for _ in range(passes):
        out = bytearray(cur)
        for y in range(h):
            for x in range(w):
                if cur[y * w + x]:
                    continue
                if (x > 0 and cur[y * w + x - 1]) or (x + 1 < w and cur[y * w + x + 1]) \
                        or (y > 0 and cur[(y - 1) * w + x]) or (y + 1 < h and cur[(y + 1) * w + x]):
                    out[y * w + x] = 1
        cur = out
    return cur


def image_to_tile(path):
    """One full-bleed sprite image -> TILE*TILE pixel values 0..9."""
    try:
        img = Image.open(path)
    except OSError as e:
        die(f"cannot read {path}: {e}")
    w, h = img.size
    g = bytearray(img.convert("L").tobytes())
    ink = threshold(g, w * h)
    if sum(ink) == 0:
        die(f"{path.name} has no ink at all")
    flip = sum(ink) * 2 > w * h
    if flip:
        ink = bytearray(0 if v else 1 for v in ink)
    box = bbox(ink, w, h)
    if box is None:
        die(f"{path.name} has no ink")
    bx, by, bw, bh = box
    s = min(FIT_W / bw, FIT_H / bh)
    dw = max(1, min(FIT_W, int(round(bw * s))))
    dh = max(1, min(FIT_H, int(round(bh * s))))
    cut = bytearray(bw * bh)
    for y in range(bh):
        src = (by + y) * w + bx
        cut[y * bw:(y + 1) * bw] = g[src:src + bw]
    if flip:
        cut = bytearray(255 - v for v in cut)
    small = area_resize(cut, bw, bh, dw, dh)
    small_ink = threshold(small, dw * dh)
    if sum(small_ink) == 0:
        die(f"{path.name} vanished when scaled to 40 px")
    mask = shape_mask(dilate(small_ink, dw, dh, 1), dw, dh)

    tile = bytearray(TILE * TILE)
    ox = (TILE - dw) // 2
    oy = (TILE - dh) // 2
    for y in range(dh):
        for x in range(dw):
            i = y * dw + x
            if not mask[i]:
                continue
            level = (255 - small[i]) * BAYER_LEVELS // 255
            tile[(oy + y) * TILE + ox + x] = PAPER + level
    return tile


def pack(px):
    if len(px) != TILE * TILE:
        die(f"tile has {len(px)} pixels, want {TILE * TILE}")
    out = bytearray(TILE * TILE // 2)
    for i, v in enumerate(px):
        if v < TRANSPARENT or v > TONE_MAX:
            die(f"pixel value {v} is not 0..{TONE_MAX}")
        out[i >> 1] |= v << ((i & 1) * 4)
    for i, v in enumerate(px):
        if (out[i >> 1] >> ((i & 1) * 4)) & 15 != v:
            die("pack/unpack mismatch")
    return out


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def write_preview(path, order, tiles):
    """Each tile twice: the tone as stored, and as the panel's grey answer."""
    cols = 7
    cell_w, cell_h = TILE * 2 + 12, TILE + 8
    rows = (len(order) + cols - 1) // cols
    pw, ph = cols * cell_w, rows * cell_h
    img = Image.new("RGB", (pw, ph), (32, 32, 32))
    px = img.load()
    for i, sid in enumerate(order):
        def ink15(level):
            return (level * 15 + 4) // 8
        cx = (i % cols) * cell_w + 4
        cy = (i // cols) * cell_h + 4
        t = tiles[sid]
        for y in range(TILE):
            for x in range(TILE):
                v = t[y * TILE + x]
                if v == TRANSPARENT:
                    px[cx + x, cy + y] = (190, 190, 190)
                    px[cx + TILE + 8 + x, cy + y] = (216, 216, 216)
                    continue
                level = v - PAPER
                tone = 255 - level * 255 // BAYER_LEVELS
                px[cx + x, cy + y] = (tone, tone, tone)
                g = GREY_RAMP[ink15(level)]
                px[cx + TILE + 8 + x, cy + y] = (g, g, g)
    img.save(path)


def main():
    ascii_dump = "--ascii" in sys.argv
    preview = None
    args = [a for a in sys.argv[1:] if a != "--ascii"]
    if args[:1] == ["--preview"] and len(args) == 2:
        preview = Path(args[1])
    elif args:
        die("usage: bake_pvz_sprites.py [--ascii] [--preview out.png]")

    manifest = json.loads(MANIFEST.read_text())
    tiles = {}          # id -> bytearray of TILE*TILE values
    source = {}         # id -> description of where it came from

    for sheet in manifest["sheets"]:
        path = ROOT / sheet["file"]
        if not path.exists():
            die(f"missing sprite image {path}")
        cols, rows = sheet["grid"]
        if cols != 1 or rows != 1:
            die(f"{path.name}: PvZ sprites are single images, grid must be [1, 1]")
        sid = sheet["id"]
        tile = image_to_tile(path)
        if sheet.get("flip_h"):
            for y in range(TILE):
                row = bytearray(tile[y * TILE:(y + 1) * TILE])
                row.reverse()
                tile[y * TILE:(y + 1) * TILE] = row
        tone = sum(v - PAPER for v in tile if v > PAPER)
        body = sum(1 for v in tile if v != TRANSPARENT)
        pct = 100 * tone // (BAYER_LEVELS * body) if body else 0
        if pct < 15:
            die(f"{sid} came out only {pct}% ink; too faint to read on e-ink")
        tiles[sid] = tile
        source[sid] = f"{path.name} ({tone} tone-px, {pct}% ink)"

    missing = [s for s in manifest["required"] if s not in tiles]
    if missing:
        die("manifest requires sprites that were not baked: " + ", ".join(missing))

    order = [s for s in manifest["order"] if s in tiles]
    for sid in tiles:
        if sid not in order:
            die(f"{sid} was baked but is not in the order list")

    lines = []
    lines.append("/* Generated by tools/bake_pvz_sprites.py. Do not edit. */")
    lines.append("#ifndef PVZ_SPRITES_H")
    lines.append("#define PVZ_SPRITES_H")
    lines.append("#include <string.h>")
    lines.append(f"#define PVZ_TILE {TILE}")
    lines.append(f"#define PVZ_N {(TILE * TILE) // 2}")
    lines.append(f"#define PVZ_COUNT {len(order)}")
    lines.append("enum {")
    for i, sid in enumerate(order):
        name = "PZ_" + sid.upper()
        lines.append(f"    {name} = {i},")
    lines.append("};")
    lines.append("static const char *const pz_id[PVZ_COUNT] = {")
    for sid in order:
        lines.append(f"    {c_str(sid)},")
    lines.append("};")
    lines.append("static const unsigned char pz_px[PVZ_COUNT][PVZ_N] = {")
    for sid in order:
        blob = pack(tiles[sid])
        lines.append("    {" + ",".join(f"0x{b:02x}" for b in blob) + "},")
    lines.append("};")
    lines.append("/* 4 bits per pixel: 0 transparent, 1 paper, 2..9 a tone whose")
    lines.append(" * value minus 1 is the ink level 0..8, which pvz rescales to the")
    lines.append(" * 0..15 the engine's grey layer takes. */")
    lines.append("static int pz_at(int id, int x, int y) {")
    lines.append("    int i = y * PVZ_TILE + x;")
    lines.append("    return (pz_px[id][i >> 1] >> ((i & 1) * 4)) & 15;")
    lines.append("}")
    lines.append("static int pz_find(const char *id) {")
    lines.append("    for (int i = 0; i < PVZ_COUNT; i++)")
    lines.append("        if (strcmp(pz_id[i], id) == 0) return i;")
    lines.append("    return -1;")
    lines.append("}")
    lines.append("#endif")
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(lines) + "\n")
    for sid in order:
        print(f"  {sid}: {source[sid]}")
    print(f"wrote {OUT} ({len(order)} sprites)")

    if ascii_dump:
        ascii_ramp = " .:-=+*#%@"
        for sid in order:
            t = tiles[sid]
            print(f"\n== {sid}")
            for y in range(0, TILE, 2):
                row = ""
                for x in range(TILE):
                    row += ascii_ramp[t[y * TILE + x]]
                print(row)

    if preview:
        write_preview(preview, order, tiles)
        print(f"wrote {preview}")


if __name__ == "__main__":
    main()
