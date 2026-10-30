#!/usr/bin/env python3
"""Cut spritesheet.png into 40x40 tiles and write src/sprites.h.

The sheet is a picture, not a finished atlas. Rects come from
tactical_sprite_map_with_descriptions.json. A bad rect, a missing
stat, or a crop with no pixels stops the script. It does not invent
a stand-in tile.
"""
import json
import re
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHEET = ROOT / "spritesheet.png"
META = ROOT / "tactical_sprite_map_with_descriptions.json"
OUT = ROOT / "src" / "sprites.h"

TILE = 40
EXPECT_W, EXPECT_H = 1536, 1024

# 2 bits per pixel: 0 transparent, 1 paper, 2 ink.
PAPER, INK = 1, 2

BAYER = (
    (0, 8, 2, 10),
    (12, 4, 14, 6),
    (3, 11, 1, 9),
    (15, 7, 13, 5),
)

PREFIX = {
    "player_units": "P",
    "enemies": "E",
    "terrain_tiles": "T",
    "objects_decor": "O",
    "vehicles_support": "V",
    "misc_ui_objects": "M",
    "ui_markers": "U",
    "quirky_extras": "Q",
}


def die(msg):
    print("slice_sprites: " + msg, file=sys.stderr)
    raise SystemExit(1)


def read_png(path):
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        die(f"{path} is not a PNG")
    pos = 8
    w = h = color = bitd = inter = None
    idat = b""
    while pos + 8 <= len(data):
        ln, typ = struct.unpack(">I4s", data[pos:pos + 8])
        pos += 8
        chunk = data[pos:pos + ln]
        pos += ln + 4
        if typ == b"IHDR":
            w, h, bitd, color, comp, filt, inter = struct.unpack(">IIBBBBB", chunk)
        elif typ == b"IDAT":
            idat += chunk
        elif typ == b"IEND":
            break
    if None in (w, h, color) or not idat:
        die("PNG is missing IHDR or IDAT")
    if bitd != 8 or color != 6 or inter != 0:
        die(f"PNG is {w}x{h} bit {bitd} color {color} inter {inter}; need 8-bit RGBA")
    if (w, h) != (EXPECT_W, EXPECT_H):
        die(f"PNG is {w}x{h}, the map says {EXPECT_W}x{EXPECT_H}")
    raw = zlib.decompress(idat)
    bpp = 4
    stride = w * bpp
    rows = bytearray(h * stride)

    def paeth(a, b, c):
        p = a + b - c
        pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
        if pa <= pb and pa <= pc:
            return a
        if pb <= pc:
            return b
        return c

    i = 0
    prev = bytearray(stride)
    for y in range(h):
        if i >= len(raw):
            die("PNG ran out of pixels")
        f = raw[i]
        i += 1
        row = bytearray(raw[i:i + stride])
        i += stride
        if len(row) != stride:
            die("PNG row was short")
        if f == 1:
            for x in range(stride):
                left = row[x - bpp] if x >= bpp else 0
                row[x] = (row[x] + left) & 255
        elif f == 2:
            for x in range(stride):
                row[x] = (row[x] + prev[x]) & 255
        elif f == 3:
            for x in range(stride):
                left = row[x - bpp] if x >= bpp else 0
                row[x] = (row[x] + ((left + prev[x]) // 2)) & 255
        elif f == 4:
            for x in range(stride):
                left = row[x - bpp] if x >= bpp else 0
                up = prev[x]
                ul = prev[x - bpp] if x >= bpp else 0
                row[x] = (row[x] + paeth(left, up, ul)) & 255
        elif f != 0:
            die(f"PNG filter {f} is not supported")
        prev = row
        rows[y * stride:(y + 1) * stride] = row
    return w, h, rows


def pix(rows, w, x, y):
    o = (y * w + x) * 4
    return rows[o], rows[o + 1], rows[o + 2], rows[o + 3]


def crop(rows, w, h, rect):
    x, y, rw, rh = rect["x"], rect["y"], rect["width"], rect["height"]
    if rw <= 0 or rh <= 0 or x < 0 or y < 0 or x + rw > w or y + rh > h:
        die(f"rect {x},{y} {rw}x{rh} is outside the {w}x{h} sheet")
    out = bytearray(rw * rh * 4)
    for row in range(rh):
        src = ((y + row) * w + x) * 4
        dst = row * rw * 4
        out[dst:dst + rw * 4] = rows[src:src + rw * 4]
    return rw, rh, out


def box_resize(src, sw, sh, dw, dh):
    """Area-average scale. dw/dh are the destination size and must be > 0."""
    if dw <= 0 or dh <= 0 or sw <= 0 or sh <= 0:
        die(f"bad resize {sw}x{sh} -> {dw}x{dh}")
    out = bytearray(dw * dh * 4)
    for y in range(dh):
        y0 = y * sh / dh
        y1 = (y + 1) * sh / dh
        iy0 = int(y0)
        iy1 = int(y1)
        if iy1 <= iy0:
            iy1 = iy0 + 1
        if iy1 > sh:
            iy1 = sh
        for x in range(dw):
            x0 = x * sw / dw
            x1 = (x + 1) * sw / dw
            ix0 = int(x0)
            ix1 = int(x1)
            if ix1 <= ix0:
                ix1 = ix0 + 1
            if ix1 > sw:
                ix1 = sw
            acc = [0, 0, 0, 0]
            n = 0
            for yy in range(iy0, iy1):
                row = yy * sw
                for xx in range(ix0, ix1):
                    o = (row + xx) * 4
                    acc[0] += src[o]
                    acc[1] += src[o + 1]
                    acc[2] += src[o + 2]
                    acc[3] += src[o + 3]
                    n += 1
            if n == 0:
                die("resize box was empty")
            o = (y * dw + x) * 4
            out[o] = acc[0] // n
            out[o + 1] = acc[1] // n
            out[o + 2] = acc[2] // n
            out[o + 3] = acc[3] // n
    return out


def luma(r, g, b):
    v = (r * 77 + g * 150 + b * 29) >> 8
    v = int((v - 128) * 1.7 + 128)
    if v < 0:
        return 0
    if v > 255:
        return 255
    return v


def dither_buf(src, w, h, keep_alpha):
    """Return TILE*TILE bytes of 0/1/2. src is already TILE*TILE RGBA, or smaller
    and placed by the caller into a TILE buffer before this runs."""
    px = [0] * (w * h)
    for y in range(h):
        for x in range(w):
            o = (y * w + x) * 4
            a = src[o + 3]
            if keep_alpha and a < 140:
                continue
            lum = luma(src[o], src[o + 1], src[o + 2])
            thr = (BAYER[y & 3][x & 3] + 0.5) / 16.0 * 255.0
            px[y * w + x] = PAPER if lum >= thr else INK
    if keep_alpha:
        # A 1px ink rim so a soldier still reads when the ground is busy.
        rim = px[:]
        for y in range(h):
            for x in range(w):
                if px[y * w + x] == 0:
                    continue
                edge = False
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    xx, yy = x + dx, y + dy
                    if xx < 0 or yy < 0 or xx >= w or yy >= h or px[yy * w + xx] == 0:
                        edge = True
                        break
                if edge:
                    rim[y * w + x] = INK
        px = rim
    return px


def bbox_alpha(src, w, h, thresh):
    x0, y0, x1, y1 = w, h, -1, -1
    for y in range(h):
        row = y * w
        for x in range(w):
            if src[(row + x) * 4 + 3] >= thresh:
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


def blit_into(dst, dw, dh, src, sw, sh, ox, oy):
    for y in range(sh):
        if oy + y < 0 or oy + y >= dh:
            continue
        for x in range(sw):
            if ox + x < 0 or ox + x >= dw:
                continue
            s = (y * sw + x) * 4
            d = ((oy + y) * dw + (ox + x)) * 4
            dst[d:d + 4] = src[s:s + 4]


def fit_character(src, sw, sh):
    box = bbox_alpha(src, sw, sh, 140)
    if box is None:
        return None
    x, y, bw, bh = box
    # One pixel of padding when the crop has room, so the rim is the
    # character and not the rectangle edge.
    if x > 0:
        x -= 1
        bw += 1
    if y > 0:
        y -= 1
        bh += 1
    if x + bw < sw:
        bw += 1
    if y + bh < sh:
        bh += 1
    cut = bytearray(bw * bh * 4)
    for row in range(bh):
        s = ((y + row) * sw + x) * 4
        cut[row * bw * 4:(row + 1) * bw * 4] = src[s:s + bw * 4]
    aw, ah = 36, 32
    s = min(aw / bw, ah / bh)
    dw = max(1, int(round(bw * s)))
    dh = max(1, int(round(bh * s)))
    if dw > aw:
        dw = aw
    if dh > ah:
        dh = ah
    scaled = box_resize(cut, bw, bh, dw, dh)
    canvas = bytearray(TILE * TILE * 4)
    blit_into(canvas, TILE, TILE, scaled, dw, dh, (TILE - dw) // 2, 2 + (ah - dh) // 2)
    return canvas


def flatten(src):
    """Low-alpha pixels become the average colour of the solid ones.

    Terrain tiles are pictures of ground, not stickers. A see-through edge
    would turn into a black speck after dithering, so it is filled first.
    """
    acc = [0, 0, 0]
    n = 0
    for i in range(0, len(src), 4):
        if src[i + 3] >= 140:
            acc[0] += src[i]
            acc[1] += src[i + 1]
            acc[2] += src[i + 2]
            n += 1
    if n == 0:
        die("terrain crop has no opaque pixels")
    mr, mg, mb = acc[0] // n, acc[1] // n, acc[2] // n
    out = bytearray(src)
    for i in range(0, len(out), 4):
        if out[i + 3] < 140:
            out[i] = mr
            out[i + 1] = mg
            out[i + 2] = mb
        out[i + 3] = 255
    return out


def fit_terrain(src, sw, sh):
    side = min(sw, sh)
    ox = (sw - side) // 2
    oy = (sh - side) // 2
    cut = bytearray(side * side * 4)
    for row in range(side):
        s = ((oy + row) * sw + ox) * 4
        cut[row * side * 4:(row + 1) * side * 4] = src[s:s + side * 4]
    return flatten(box_resize(cut, side, side, TILE, TILE))


def pack(px):
    if len(px) != TILE * TILE:
        die(f"tile has {len(px)} pixels, want {TILE * TILE}")
    out = bytearray(TILE * TILE // 4)
    for i, v in enumerate(px):
        if v not in (0, 1, 2):
            die(f"pixel value {v}")
        out[i >> 2] |= v << ((i & 3) * 2)
    for i, v in enumerate(px):
        got = (out[i >> 2] >> ((i & 3) * 2)) & 3
        if got != v:
            die("pack/unpack mismatch")
    return out


def c_ident(prefix, sid):
    raw = f"{prefix}_{sid}".upper()
    ident = []
    for ch in raw:
        ident.append(ch if ch.isalnum() else "_")
    name = "".join(ident)
    if not name or name[0].isdigit():
        die(f"bad enum name {name}")
    return "SP_" + name


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def parse_unit(desc, where):
    text = desc.replace("\u2013", "-").replace("\u2014", "-")
    hp = re.search(r"(\d+) HP", text)
    aim = re.search(r"(\d+) aim", text)
    mob = re.search(r"(\d+)-tile", text)
    if not hp or not aim or not mob:
        die(f"{where} is missing HP, aim, or mobility: {desc}")
    if re.search(r"no armour", text):
        armor = 0
    else:
        arm = re.search(r"(\d+) armour", text)
        if not arm:
            die(f"{where} does not say its armour: {desc}")
        armor = int(arm.group(1))
    dmg = re.search(r"(\d+)\s*-\s*(\d+)", text)
    if not dmg:
        die(f"{where} does not say its damage: {desc}")
    dlo, dhi = int(dmg.group(1)), int(dmg.group(2))
    if dhi < dlo:
        die(f"{where} damage {dlo}-{dhi} is backwards")
    mobility = int(mob.group(1))
    # The sheet's mobility is for a big map. This mission is 12 tiles wide,
    # so a step budget is about a third of that number, and never under 4
    # or the squad cannot cross a forest tile and still move.
    budget = mobility // 3
    if budget < 4:
        budget = 4
    heal_hp, heal_n = 0, 0
    heal = re.search(r"restoring (\d+) HP", text)
    if heal:
        heal_hp = int(heal.group(1))
        heal_n = 2 if "twice" in text else 1
    flags = 0
    if re.search(r"flying|aerial|flight|ignoring ground", text):
        flags |= 4
    if re.search(r"\bmelee\b", text):
        flags |= 8
    if "stationary" in text:
        flags |= 16
    if heal_hp:
        flags |= 32
    return {
        "hp": int(hp.group(1)),
        "aim": int(aim.group(1)),
        "mv": budget,
        "armor": armor,
        "dlo": dlo,
        "dhi": dhi,
        "heal_hp": heal_hp,
        "heal_n": heal_n,
        "flags": flags,
        "sheet_move": mobility,
    }


def write_preview(path, tiles):
    """tiles: list of (label, rgba TILE*TILE or None, packed pixel list)."""
    cols = 8
    cell_w, cell_h = TILE * 2 + 8, TILE + 4
    rows = (len(tiles) + cols - 1) // cols
    pw, ph = cols * cell_w, rows * cell_h
    img = bytearray([40, 40, 40, 255]) * (pw * ph)

    def put(x, y, r, g, b):
        if x < 0 or y < 0 or x >= pw or y >= ph:
            return
        o = (y * pw + x) * 4
        img[o] = r
        img[o + 1] = g
        img[o + 2] = b
        img[o + 3] = 255

    for i, (_label, _rgba, px) in enumerate(tiles):
        cx = (i % cols) * cell_w + 2
        cy = (i // cols) * cell_h + 2
        for y in range(TILE):
            for x in range(TILE):
                v = px[y * TILE + x]
                if v == 0:
                    col = (180, 60, 180)
                elif v == PAPER:
                    col = (255, 255, 255)
                else:
                    col = (0, 0, 0)
                put(cx + x, cy + y, *col)
                put(cx + TILE + 4 + x, cy + y, *col)
    raw = bytearray()
    stride = pw * 4
    for y in range(ph):
        raw.append(0)
        raw += img[y * stride:(y + 1) * stride]

    def chunk(tag, payload):
        return struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", pw, ph, 8, 6, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    path.write_bytes(png)


def main():
    preview = None
    args = sys.argv[1:]
    if args[:1] == ["--preview"] and len(args) == 2:
        preview = Path(args[1])
    elif args:
        die("usage: slice_sprites.py [--preview out.png]")
    w, h, rows = read_png(SHEET)
    doc = json.loads(META.read_text())
    sections = doc.get("sections")
    if not sections:
        die("sprite map has no sections")
    player_labels = set()
    for sec in sections:
        if sec.get("id") == "player_units":
            for sp in sec["sprites"]:
                player_labels.add(sp["label"].lower())

    enum_names = []
    ids = []
    labels = []
    stats = []
    blobs = []
    preview_tiles = []
    seen_ids = set()
    seen_enums = set()

    for sec in sections:
        sid = sec.get("id")
        if sid not in PREFIX:
            die(f"unknown section {sid}")
        kind = sec.get("kind")
        is_unit = sid in ("player_units", "enemies")
        is_terrain = sid == "terrain_tiles"
        for sp in sec["sprites"]:
            full_id = f"{sid}/{sp['id']}"
            if full_id in seen_ids:
                die(f"duplicate id {full_id}")
            seen_ids.add(full_id)
            enum = c_ident(PREFIX[sid], sp["id"])
            if enum in seen_enums:
                die(f"duplicate enum {enum}")
            seen_enums.add(enum)
            rect = sp.get("source_rect")
            if not rect:
                die(f"{full_id} has no source_rect")
            sw, sh, cut = crop(rows, w, h, rect)
            label = sp["label"]
            if sid == "enemies" and label.lower() in player_labels:
                label = "A." + label
            if is_unit:
                st = parse_unit(sp.get("description", ""), full_id)
                st["flags"] |= 1 if sid == "player_units" else 2
                canvas = fit_character(cut, sw, sh)
                if canvas is None:
                    die(f"{full_id} crop has no opaque pixels")
                px = dither_buf(canvas, TILE, TILE, True)
                ink = sum(1 for v in px if v == INK)
                if ink < 8:
                    die(f"{full_id} dithered to only {ink} ink pixels")
            elif is_terrain:
                st = {k: 0 for k in ("hp", "aim", "mv", "armor", "dlo", "dhi", "heal_hp", "heal_n", "flags")}
                canvas = fit_terrain(cut, sw, sh)
                px = dither_buf(canvas, TILE, TILE, False)
            else:
                st = {k: 0 for k in ("hp", "aim", "mv", "armor", "dlo", "dhi", "heal_hp", "heal_n", "flags")}
                canvas = fit_character(cut, sw, sh)
                if canvas is None:
                    die(f"{full_id} crop has no opaque pixels")
                px = dither_buf(canvas, TILE, TILE, True)
                if sum(1 for v in px if v) < 4:
                    die(f"{full_id} dithered to nothing")
            if kind is None:
                die(f"{sid} has no kind")
            enum_names.append(enum)
            ids.append(full_id)
            labels.append(label)
            stats.append(st)
            blobs.append(pack(px))
            preview_tiles.append((full_id, None, px))
            if is_unit:
                print(
                    f"{full_id}: hp {st['hp']} aim {st['aim']} move {st['mv']} "
                    f"(sheet {st['sheet_move']}) arm {st['armor']} dmg {st['dlo']}-{st['dhi']} "
                    f"flags {st['flags']}"
                )

    lines = []
    lines.append("/* Generated by tools/slice_sprites.py. Do not edit. */")
    lines.append("#ifndef XCOM_SPRITES_H")
    lines.append("#define XCOM_SPRITES_H")
    lines.append("#include <string.h>")
    lines.append(f"#define SP_TILE {TILE}")
    lines.append(f"#define SP_N {(TILE * TILE) // 4}")
    lines.append(f"#define SP_COUNT {len(enum_names)}")
    lines.append("enum {")
    for i, name in enumerate(enum_names):
        lines.append(f"    {name} = {i},")
    lines.append("};")
    lines.append("enum {")
    lines.append("    SPF_PLAYER = 1,")
    lines.append("    SPF_ENEMY = 2,")
    lines.append("    SPF_FLY = 4,")
    lines.append("    SPF_MELEE = 8,")
    lines.append("    SPF_STEADY = 16,")
    lines.append("    SPF_HEAL = 32")
    lines.append("};")
    lines.append("typedef struct {")
    lines.append("    int hp, aim, mv, armor, dlo, dhi;")
    lines.append("    int heal_hp, heal_n, flags;")
    lines.append("} sp_info_t;")
    lines.append("static const char *const sp_id[SP_COUNT] = {")
    for s in ids:
        lines.append(f"    {c_str(s)},")
    lines.append("};")
    lines.append("static const char *const sp_label[SP_COUNT] = {")
    for s in labels:
        lines.append(f"    {c_str(s)},")
    lines.append("};")
    lines.append("static const sp_info_t sp_info[SP_COUNT] = {")
    for st in stats:
        lines.append(
            "    {%d, %d, %d, %d, %d, %d, %d, %d, %d},"
            % (st["hp"], st["aim"], st["mv"], st["armor"], st["dlo"], st["dhi"],
               st["heal_hp"], st["heal_n"], st["flags"])
        )
    lines.append("};")
    lines.append("static const unsigned char sp_px[SP_COUNT][SP_N] = {")
    for blob in blobs:
        hexes = ",".join(f"0x{b:02x}" for b in blob)
        lines.append("    {" + hexes + "},")
    lines.append("};")
    lines.append("static int sp_at(int id, int x, int y) {")
    lines.append("    int i = y * SP_TILE + x;")
    lines.append("    return (sp_px[id][i >> 2] >> ((i & 3) * 2)) & 3;")
    lines.append("}")
    lines.append("static int sp_find(const char *id) {")
    lines.append("    for (int i = 0; i < SP_COUNT; i++)")
    lines.append("        if (strcmp(sp_id[i], id) == 0) return i;")
    lines.append("    return -1;")
    lines.append("}")
    lines.append("#endif")
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(lines) + "\n")
    print(f"wrote {OUT} ({len(enum_names)} sprites)")
    if preview:
        write_preview(preview, preview_tiles)
        print(f"wrote {preview}")


if __name__ == "__main__":
    main()
