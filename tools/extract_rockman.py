#!/usr/bin/env python3
"""Extract every data table from scratch/Rockman.asm into src/rockman_data.h.

The disassembly is the single source of truth for the 1984 VIC-20 Rockman
ruleset.  The port must never hand-type the maps, diamond positions or exit
tables: this script parses them out of the commented KickAssembler source and
writes them as C arrays.  Run `make rockman-data` (or this script directly)
after touching the asm; the header is committed.

    tools/extract_rockman.py            # write src/rockman_data.h
    tools/extract_rockman.py --check    # write header and self-check tables
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ASM = ROOT / "scratch" / "Rockman.asm"
OUT = ROOT / "src" / "rockman_data.h"

# ---------------------------------------------------------------- constants

CONSTS = {
    "COLS": 22, "TILE_0": 0x05, "TILE_SC": 0x0F, "TILE_HI": 0x10,
    "TILE_BRICK": 0x12, "TILE_DIRT": 0x13, "TILE_EMPTY": 0x14,
    "TILE_ROCK": 0x15, "TILE_FALL1": 0x16, "TILE_FALL2": 0x17,
    "TILE_FALL3": 0x18, "TILE_DIAMOND": 0x19, "TILE_TRAP": 0x1A,
    "TILE_PLAYER_UD": 0x1B, "TILE_PLAYER_L": 0x1D, "TILE_PLAYER_R": 0x1F,
    "TILE_MONSTER_A": 0x21, "TILE_MONSTER_B": 0x23, "TILE_MONSTER_C": 0x25,
    "TILE_MONSTER_D": 0x27, "TILE_EXPLODE1": 0x29,
    "CELL_UP": 0x16, "CELL_LEFT": 0x2B, "CELL_HERE": 0x2C,
    "CELL_RIGHT": 0x2D, "CELL_DOWN": 0x42,
    "TUNE_SLOT": 14, "TUNE_GATE": 9, "TUNE_LENGTH": 32,
    "NUM_ROOMS": 20, "ROOM_END": 21, "DIAMONDS_PER_ROOM": 8,
}

SIMPLE_CONSTS = {
    "COLS", "TILE_0", "TILE_SC", "TILE_HI", "TILE_BRICK", "TILE_DIRT",
    "TILE_EMPTY", "TILE_ROCK", "TILE_FALL1", "TILE_FALL2", "TILE_FALL3",
    "TILE_DIAMOND", "TILE_TRAP", "TILE_PLAYER_UD", "TILE_PLAYER_L",
    "TILE_PLAYER_R", "TILE_MONSTER_A", "TILE_MONSTER_B", "TILE_MONSTER_C",
    "TILE_EXPLODE1", "CELL_UP", "CELL_LEFT", "CELL_HERE", "CELL_RIGHT",
    "CELL_DOWN", "TUNE_SLOT", "TUNE_GATE", "TUNE_LENGTH", "NUM_ROOMS",
    "ROOM_END", "DIAMONDS_PER_ROOM",
}


def eval_const(expr, line):
    """Evaluate a small assembler expression (hex, +, names)."""
    expr = expr.strip().replace(" ", "")
    if not expr:
        raise ValueError(f"empty expression at {line}")
    if re.fullmatch(r"\$\$?[0-9A-Fa-f]+", expr):
        return int(expr[1:], 16)
    parts = re.split(r"([+-])", expr)
    total = None
    op = "+"
    for part in parts:
        if part in "+-":
            op = part
            continue
        if not part:
            continue
        val = None
        if re.fullmatch(r"\$[0-9A-Fa-f]+", part):
            val = int(part[1:], 16)
        elif re.fullmatch(r"\d+", part):
            val = int(part, 10)
        elif part in CONSTS:
            val = CONSTS[part]
        else:
            raise ValueError(f"unknown symbol {part!r} at {line}")
        total = val if total is None else (total + val if op == "+" else total - val)
    if total is None:
        raise ValueError(f"empty expression at {line}")
    return total


def parse_bytes(prefix, source, line_no, want=None):
    """Find .byte <prefix>... lines and return the byte values.

    Stops at the end of a line when that line also carries a label
    ("frameA: .byte ..."), so a single-line table never swallows the
    single-line table on the next line.
    """
    out = []
    i = line_no
    while i < len(source):
        line = source[i]
        m = re.search(r"\.byte\s+(.*?)\s*(?://.*)?$", line)
        if not m:
            break
        rest = m.group(1)
        if prefix:
            if not rest.startswith(prefix + ","):
                break
            rest = rest[len(prefix) + 1:]
        for item in rest.split(","):
            item = item.strip()
            if not item:
                continue
            out.append(eval_const(item, f"line {i+1}"))
        if re.match(r"\s*\w+:\s*\.byte\b", line):
            i += 1
            if want is not None and len(out) == want:
                break
            continue
        i += 1
        if want is not None and len(out) >= want:
            break
    if want is not None and len(out) != want:
        raise ValueError(f"{prefix} wanted {want} bytes, got {len(out)}")
    return out


def parse_map(room, source, line):
    """Parse 14 rows of '.byte %bbbbbbbb,%bbbbbbbb' for one room. Returns 28 bytes.
    Skips over comment lines first."""
    rows = []
    i = line
    while len(rows) < 28:
        m = re.search(r"\.byte\s+(%[01]{8})\s*,\s*(%[01]{8})", source[i])
        if not m:
            i += 1
            if i >= len(source):
                raise ValueError(f"room {room}: no map row on line {line+1}")
            continue
        rows.append(int(m.group(1)[1:], 2))
        rows.append(int(m.group(2)[1:], 2))
        i += 1
    return rows


def parse_charset_tile(source, line):
    """Parse 16 lines of '.byte %bbbbbbbb' for a tile; returns list of ints."""
    rows = []
    i = line
    while len(rows) < 16:
        m = re.search(r"\.byte\s+(%[01]{8})", source[i])
        if not m:
            raise ValueError(f"charset tile: no row on line {i+1}")
        rows.append(int(m.group(1)[1:], 2))
        i += 1
    return rows


def main():
    source = ASM.read_text().splitlines()

    # ---- colored data: the maps sit in sequence under roomMaps:, one "// room N"
    # comment followed by 14 .byte rows.  Walk them in order.
    rooms = {}
    for i, line in enumerate(source):
        if line.startswith("roomMaps:"):
            j = i + 1
            while j < len(source) and len(rooms) < 22:
                m = re.search(r"//\s*room\s+(\d+)", source[j])
                if m:
                    room = int(m.group(1))
                    rooms[room] = parse_map(room, source, j + 1)
                    j += 1 + 14
                else:
                    j += 1
    if set(rooms) != set(range(22)):
        raise ValueError(f"expected maps for rooms 0-21, got {sorted(rooms)}")

    # ---- diamond positions (word cell(r,c) pairs)
    diamonds = {}
    for i, line in enumerate(source):
        m = re.search(r"\.word\s+(cell\(.*?\))\s*//\s*room\s+(\d+)", line)
        if m:
            room = int(m.group(2))
            cells = re.findall(r"cell\((\d+),(\d+)\)", m.group(1))
            assert len(cells) == 5
            diamonds[room] = [(int(r), int(c)) for r, c in cells]
    if set(diamonds) != set(range(1, 21)):
        raise ValueError(f"expected diamonds for rooms 1-20, got {sorted(diamonds)}")

    # ---- exits (the .byte lines carry no label prefix; each table is exactly
    # 20 bytes and is followed by the next label or a comment)
    exits = {}
    for label in ("exitDown", "exitLeft", "exitUp", "exitRight"):
        for i, line in enumerate(source):
            if line.startswith(label + ":"):
                exits[label] = parse_bytes("", source, i + 1, want=20)
                break
        else:
            raise ValueError(f"no {label} table")

    # ---- start positions (label and .byte share one line: "startPtrLo: .byte ...")
    starts = {}
    for i, line in enumerate(source):
        if line.strip().startswith("startPtrLo:"):
            starts["lo"] = parse_bytes("", [line], 0, want=5)
        if line.strip().startswith("startPtrHi:"):
            starts["hi"] = parse_bytes("", [line], 0, want=5)
    if len(starts) != 2:
        raise ValueError("start pointer tables not found")

    # ---- tune
    tune = None
    for i, line in enumerate(source):
        if line.strip().startswith("tuneNotes:"):
            tune = parse_bytes("", source, i + 1, want=32)
            break
    if tune is None:
        raise ValueError("tuneNotes not found")

    # ---- charset tiles: comment "// $XX  name" followed by 16 .byte rows,
    # covering $05-$26 (digits, labels, brick, dirt, empty, rock x4, diamond,
    # trap, player x6, monsters x6; $11 is zero) and the explosion tiles $29-$2C.
    charset = {}
    for i, line in enumerate(source):
        m = re.match(r"\s*//\s*\$([0-9A-Fa-f]{2})\s", line)
        if not m:
            continue
        tile = int(m.group(1), 16)
        if not (0x05 <= tile <= 0x26 or 0x29 <= tile <= 0x2C):
            continue
        rows = []
        j = i + 1
        while len(rows) < 16 and j < len(source):
            bm = re.search(r"\.byte\s+(%[01]{8})", source[j])
            if bm:
                rows.append(int(bm.group(1)[1:], 2))
            fm = re.search(r"\.fill\s+(\d+)\s*,\s*(\d+)", source[j])
            if fm and int(fm.group(1)) <= 16 - len(rows):
                rows.extend([int(fm.group(2))] * int(fm.group(1)))
            j += 1
        if len(rows) == 16:
            charset[tile] = rows
    wanted = set(range(0x05, 0x27)) | set(range(0x29, 0x2D))
    if set(charset) != wanted:
        missing = sorted(wanted - set(charset))
        raise ValueError(f"charset missing tiles: {[hex(t) for t in missing]}")

    # ---- small tables
    def one_line(name, want):
        for i, line in enumerate(source):
            if line.strip().startswith(name + ":"):
                return parse_bytes("", source, i, want=want)
        raise ValueError(f"{name} not found")

    tick_limits = one_line("tickLimits", 3)
    frame_a = one_line("frameA", 6)
    frame_b = one_line("frameB", 6)
    mm_dir_frame = one_line("mmDirFrame", 4)
    mm_dir_offset = one_line("mmDirOffset", 4)
    rock_sounds = one_line("rockSounds", 4)
    explode_tiles = one_line("explodeTiles", 5)
    explode_sounds = one_line("explodeSounds", 5)

    # ---- screen-address of a map cell: $1E00 + r*22 + c
    def cell_addr(r, c):
        return 0x1E00 + r * CONSTS["COLS"] + c

    # ---- self checks
    assert len(rooms) == 22 and all(len(v) == 28 for v in rooms.values())
    assert len(exits["exitDown"]) == 20
    for room, cells in diamonds.items():
        for (r, c) in cells:
            assert 0 <= r < 14 and 0 <= c < 22
    # verify the starting pointer tables against cell addresses
    for pos, (r, c) in enumerate(((7, 11), (1, 11), (7, 18), (13, 11), (7, 3))):
        lo = (cell_addr(r, c) - 44) & 0xFF
        hi = (cell_addr(r, c) - 44) >> 8
        assert starts["lo"][pos] == lo and starts["hi"][pos] == hi, (
            f"start pos {pos}: got ${starts['lo'][pos]:02X}${starts['hi'][pos]:02X}, "
            f"expected ${lo:02X}${hi:02X}")

    # ---- emit C header
    def fmt_bytes(vals, per=12):
        lines = []
        for i in range(0, len(vals), per):
            chunk = ", ".join(f"0x{v:02X}" for v in vals[i:i + per])
            lines.append("    " + chunk + ("," if i + per < len(vals) else ""))
        return "\n".join(lines)

    def fmt_diamonds():
        out = []
        for room in range(1, 21):
            cells = ", ".join(f"{{{r}, {c}}}"
                              for r, c in diamonds[room])
            out.append(f"    {{ {cells} }},  // room {room}")
        return "\n".join(out)

    def fmt_charset():
        out = []
        names = {
            0x05: "digit 0", 0x06: "digit 1", 0x07: "digit 2", 0x08: "digit 3",
            0x09: "digit 4", 0x0A: "digit 5", 0x0B: "digit 6", 0x0C: "digit 7",
            0x0D: "digit 8", 0x0E: "digit 9", 0x0F: "SC label", 0x10: "HI label",
            0x12: "brick", 0x13: "dirt", 0x14: "empty", 0x15: "rock",
            0x16: "fall 1", 0x17: "fall 2", 0x18: "fall 3", 0x19: "diamond",
            0x1A: "trap", 0x1B: "player UD f1", 0x1C: "player UD f2",
            0x1D: "player L f1", 0x1E: "player L f2", 0x1F: "player R f1",
            0x20: "player R f2", 0x21: "monster A f1", 0x22: "monster A f2",
            0x23: "monster B f1", 0x24: "monster B f2", 0x25: "monster C f1",
            0x26: "monster C f2", 0x29: "explosion 1", 0x2A: "explosion 2",
            0x2B: "explosion 3", 0x2C: "explosion 4", 0x11: "unused",
        }
        for tile in range(0x2D):
            if tile in charset:
                rows = ", ".join(f"0x{b:02X}" for b in charset[tile])
                out.append(f"    /* 0x{tile:02X} */ {{ {rows} }},  // {names.get(tile, '')}")
            else:
                # Tiles 0x00-0x04 overlap code in the original and are never
                # displayed; blank rows keep rm_tiles[] indexable by tile number.
                out.append(f"    /* 0x{tile:02X} */ {{ {', '.join(['0x00'] * 16)} }},  // never displayed")
        return "\n".join(out)

    hdr = f"""/* Rockman data tables, generated by tools/extract_rockman.py from
 * scratch/Rockman.asm (the commented KickAssembler source of the 1984
 * Mastertronic VIC-20 game).  Do not edit by hand: change the asm and run
 * tools/extract_rockman.py (or `make rockman-data`).  All bitmaps and values
 * below are verbatim from the original.
 */
#ifndef ROCKMAN_DATA_H
#define ROCKMAN_DATA_H

#include <stdint.h>

#define RM_COLS              {CONSTS['COLS']}   // screen columns in the original
#define RM_NUM_ROOMS         {CONSTS['NUM_ROOMS']}
#define RM_ROOM_END          {CONSTS['ROOM_END']}
#define RM_DIAMONDS_PER_ROOM {CONSTS['DIAMONDS_PER_ROOM']}
#define RM_TUNE_SLOT         {CONSTS['TUNE_SLOT']}
#define RM_TUNE_GATE         {CONSTS['TUNE_GATE']}
#define RM_TUNE_LENGTH       {CONSTS['TUNE_LENGTH']}
#define RM_TILE_0            {CONSTS['TILE_0']:#04x}
#define RM_TILE_BRICK        {CONSTS['TILE_BRICK']:#04x}
#define RM_TILE_DIRT         {CONSTS['TILE_DIRT']:#04x}
#define RM_TILE_EMPTY        {CONSTS['TILE_EMPTY']:#04x}
#define RM_TILE_ROCK         {CONSTS['TILE_ROCK']:#04x}
#define RM_TILE_FALL1        {CONSTS['TILE_FALL1']:#04x}
#define RM_TILE_FALL3        {CONSTS['TILE_FALL3']:#04x}
#define RM_TILE_DIAMOND      {CONSTS['TILE_DIAMOND']:#04x}
#define RM_TILE_TRAP         {CONSTS['TILE_TRAP']:#04x}
#define RM_TILE_PLAYER_UD    {CONSTS['TILE_PLAYER_UD']:#04x}
#define RM_TILE_PLAYER_L     {CONSTS['TILE_PLAYER_L']:#04x}
#define RM_TILE_PLAYER_R     {CONSTS['TILE_PLAYER_R']:#04x}
#define RM_TILE_MONSTER_A    {CONSTS['TILE_MONSTER_A']:#04x}
#define RM_TILE_MONSTER_C    {CONSTS['TILE_MONSTER_C']:#04x}
#define RM_TILE_EXPLODE1     {CONSTS['TILE_EXPLODE1']:#04x}

/* Timer: 120 steps per scale mark (see TimerTick). */
#define RM_TIMER_STEPS_PER_MARK {CONSTS['TUNE_LENGTH'] * 4 - 8}

/* Room exits: for each room 1-20, the room reached by leaving through the
 * bottom, left, top or right wall.  0 = no exit. */
static const uint8_t rm_exit_down[RM_NUM_ROOMS] = {{
{fmt_bytes(exits['exitDown'], 10)}
}};
static const uint8_t rm_exit_left[RM_NUM_ROOMS] = {{
{fmt_bytes(exits['exitLeft'], 10)}
}};
static const uint8_t rm_exit_up[RM_NUM_ROOMS] = {{
{fmt_bytes(exits['exitUp'], 10)}
}};
static const uint8_t rm_exit_right[RM_NUM_ROOMS] = {{
{fmt_bytes(exits['exitRight'], 10)}
}};

/* Entry positions: the row/col in the room the player appears at for entry
 * positions 0-4 (middle, top, right, bottom, left). */
static const uint8_t rm_entry_row[5] = {{ 7, 1, 7, 13, 7 }};
static const uint8_t rm_entry_col[5] = {{ 11, 11, 18, 11, 3 }};

/* The five fixed diamonds of each room, as (row, col) in screen coordinates
 * (row 0-13, col 3-18: the room spans screen columns 3-18).  Three more
 * diamonds are placed randomly per room. */
static const int8_t rm_fixed_diamonds[RM_NUM_ROOMS][5][2] = {{
{fmt_diamonds()}
}};

/* Room maps: 22 maps (room 0 = title picture, 1-20 play, 21 = win picture)
 * of 14 rows x 16 columns, packed 8 bits per map byte.  In the map a 1 bit is
 * brick (wall/rock), a 0 bit is dirt. */
static const uint8_t rm_room_maps[22][28] = {{
{'\n'.join('    {' + ', '.join(f'0x{b:02X}' for b in rooms[n]) + '},  // room %d' % n for n in range(22))}
}};

/* 8x16 tiles, verbatim from the original character set.  Index by the RM_TILE_*
 * constants (0x05-0x26, 0x29-0x2C); a bit set = ink.  rm_tiles[0x14] is blank. */
static const uint8_t rm_tiles[0x2D][16] = {{
{fmt_charset()}
}};

/* Passes between actions (controls, monster moves, rock scans). */
static const uint8_t rm_tick_limits[3] = {{ {', '.join(str(v) for v in tick_limits)} }};

/* First/second animation frames: player left/right/updown, then monsters A/C/B. */
static const uint8_t rm_frame_a[6] = {{ {', '.join(f'0x{v:02X}' for v in frame_a)} }};
static const uint8_t rm_frame_b[6] = {{ {', '.join(f'0x{v:02X}' for v in frame_b)} }};

/* Monster direction tables (up, right, down, left): animation index and
 * neighbour cell offset. */
static const uint8_t rm_mm_dir_frame[4] = {{ {', '.join(str(v) for v in mm_dir_frame)} }};
static const uint8_t rm_mm_dir_offset[4] = {{ {', '.join(str(v) for v in mm_dir_offset)} }};

/* Rock falling stages: bass voice pitch per stage (stage 0 = at rest, 1-3 = falling). */
static const uint8_t rm_rock_sounds[4] = {{ {', '.join(f'0x{v:02X}' for v in rock_sounds)} }};

/* Explosion steps 0-4: tile written at each step (step 4 becomes a falling
 * rock) and the noise value for the step. */
static const uint8_t rm_explode_tiles[5] = {{ {', '.join(f'0x{v:02X}' for v in explode_tiles)} }};
static const uint8_t rm_explode_sounds[5] = {{ {', '.join(f'0x{v:02X}' for v in explode_sounds)} }};

/* The background tune: one-voice "Popcorn" (A minor).  PAL pitch values. */
static const uint8_t rm_tune[RM_TUNE_LENGTH] = {{
{fmt_bytes(tune, 8)}
}};

#endif /* ROCKMAN_DATA_H */
"""
    OUT.write_text(hdr)
    print(f"wrote {OUT.relative_to(ROOT)}")
    print(f"  rooms   : 22 maps x 28 bytes")
    print(f"  exits   : 4 x 20 rooms")
    print(f"  diamonds: 20 rooms x 5")
    print(f"  tune    : {len(tune)} notes")
    if "--check" in sys.argv:
        print("self-checks passed")


if __name__ == "__main__":
    main()
