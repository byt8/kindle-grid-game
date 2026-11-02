#!/usr/bin/env python3
"""POC: SimCity-on-Kindle 600x800 screen mock.

Draws the full 600x800 e-ink screen from the real classic Micropolis
tileset (upstream/tiles.bmp), baked for greyscale readability:

- CORRECT polarity: ink level 0 = paper (255), level 15 = ink (0) —
  the exact convention of the eink engine's grey_ramp.
- PER-TILE-TYPE tone targets. The classic palette separates water from
  grass by HUE, which greyscale destroys (both land at luma ~150). Each
  tile type therefore gets its own target darkness band: grass stays
  light, water sits mid-dark, roads are near-black with light dashes.
  Texture inside the tile is remapped around that target.
- FULL-SCREEN layout: header 56px, map 21x20 tiles @30px = 630px,
  status 50px, toolbar 64px. 56+630+50+64 = 800.

Output: poc/screen_600x800_grey.png
"""
from PIL import Image, ImageDraw, ImageFont

W, H = 600, 800

# ink level 0..15 -> framebuffer byte. EXACT ramp from engine eink.c:
# 0 = paper(255), 15 = ink(0). Higher level = darker.
GREY_RAMP = [255, 238, 221, 204, 187, 170, 153, 136,
             119, 102,  85,  68,  51,  34,  17,   0]
PAPER, INK = 0, 15

TILE = 30          # drawn cell size
COLS, ROWS = 20, 21
MAP_Y = 56
MAP_H = ROWS * TILE          # 630 -> 56..686
STATUS_Y = MAP_Y + MAP_H     # 686
TOOL_Y = 736

# --- tile tone targets: (darkness_lo, darkness_hi) per tile type -----------
# The tile's own luma range is remapped into this band, so texture survives
# but every type sits at a distinct, readable tone on the panel.
TONE = {
    'w': (7, 11),    # water: mid-dark, gentle waves (band kept narrow: it is
                    # the largest area on screen, speckle reads as noise)
    'W': (5, 9),     # water edge
    'T': (3, 12),    # trees: dark trunks, light ground
    '.': (2, 7),     # grass: light with dots
    'B': (6, 11),    # barren/dirt
    'r': (4, 13),    # road: dark asphalt, light dashes
    'R': (3, 10),    # residential
    'C': (2, 9),     # commercial
    'I': (4, 11),    # industrial
    'P': (5, 12),    # power
    'k': (2, 8),     # park
}

SRC = {
    'w': 'water', 'W': 'water_edge', 'T': 'tree', '.': 'grass',
    'B': 'barren', 'r': 'road_h', 'R': 'residential', 'C': 'commercial',
    'I': 'industrial', 'P': 'power', 'k': 'park',
}

def load_tile(name):
    return Image.open(f'poc/src_{SRC[name]}.png').convert('L').resize((TILE, TILE))

def bake(name):
    """classic tile -> 30x30 list of ink levels, tone-targeted for greyscale."""
    lo, hi = TONE[name]
    px = list(load_tile(name).getdata())
    # raw darkness level: luma 255 -> 0 (paper), luma 0 -> 15 (ink)
    raw = [15 - (p * 15 + 127) // 255 for p in px]
    mn, mx = min(raw), max(raw)
    if mx - mn < 3:
        return [(lo + hi) // 2] * (TILE * TILE)   # flat tile: one clean tone
    span = mx - mn
    return [lo + (v - mn) * (hi - lo) // span for v in raw]

TILES = {k: bake(k) for k in TONE}

# --- the city map (painted, not hand-counted) ------------------------------
grid = [['.'] * COLS for _ in range(ROWS)]

def rect(x, y, w, h, c):
    for yy in range(y, min(y + h, ROWS)):
        for xx in range(x, min(x + w, COLS)):
            grid[yy][xx] = c

rect(16, 0, 2, ROWS, 'w')            # river down the right side
rect(0, 0, 6, 4, 'T')                # forest top-left
rect(10, 1, 3, 3, 'T')               # second copse
rect(0, 6, 20, 1, 'r')               # cross-town road
rect(0, 14, 20, 1, 'r')              # second road
rect(4, 0, 1, ROWS, 'r')             # vertical road
rect(11, 0, 1, 14, 'r')              # vertical road to the river
rect(16, 6, 2, 1, 'r')               # bridge
rect(16, 14, 2, 1, 'r')              # bridge
rect(6, 8, 4, 5, 'R')                # residential block
rect(12, 8, 3, 5, 'C')               # commercial block
rect(6, 16, 4, 5, 'I')               # industrial block
rect(12, 17, 3, 4, 'k')              # park
rect(0, 17, 3, 4, 'P')               # power plant
rect(16, 0, 2, 4, 'W')               # shoreline stretch of the river

MAP = [''.join(row) for row in grid]
assert all(len(r) == COLS for r in MAP) and len(MAP) == ROWS

# --- drawing helpers -------------------------------------------------------
def fill(px, x, y, w, h, v):
    for yy in range(max(0, y), min(H, y + h)):
        row = yy * W
        for xx in range(max(0, x), min(W, x + w)):
            px[row + xx] = v

def hline(px, y, x0, x1, v):
    fill(px, x0, y, x1 - x0, 1, v)

def vline(px, x, y0, y1, v):
    fill(px, x, y0, 1, y1 - y0, v)

def text(px, font, x, y, s, v, anchor='la'):
    tmp = Image.new('L', (900, 80), 0)
    d = ImageDraw.Draw(tmp)
    d.text((10, 10), s, font=font, fill=255, anchor='la')
    x0, y0, x1, y1 = d.textbbox((10, 10), s, font=font, anchor='la')
    off = (x1 - x0) // 2 if anchor == 'ma' else 0
    base = tmp.load()
    for yy in range(y0, y1):
        for xx in range(x0, x1):
            if base[xx, yy]:
                tx, ty = x + (xx - x0) - off, y + (yy - y0)
                if 0 <= tx < W and 0 <= ty < H:
                    px[ty * W + tx] = v

def render():
    px = [PAPER] * (W * H)

    # --- header: black band, paper text ------------------------------------
    fill(px, 0, 0, W, 56, INK)
    hline(px, 55, 0, W, PAPER)
    text(px, FONT_LG, 12, 14, '$10,000', PAPER)
    text(px, FONT_LG, 210, 14, 'POP 5k', PAPER)
    text(px, FONT_LG, 380, 14, 'PR 60k', PAPER)
    text(px, FONT_LG, 545, 14, '12', PAPER)

    # --- map ----------------------------------------------------------------
    for ry, row in enumerate(MAP):
        oy = MAP_Y + ry * TILE
        for cx, ch in enumerate(row):
            g = TILES[ch]
            ox = cx * TILE
            for ty in range(TILE):
                base = (oy + ty) * W + ox
                src = ty * TILE
                px[base:base + TILE] = g[src:src + TILE]
    # grid: thin dark lines so it reads as a grid game
    for ry in range(1, ROWS + 1):
        hline(px, MAP_Y + ry * TILE - 1, 0, W, 13)
    for cx in range(1, COLS + 1):
        vline(px, cx * TILE - 1, MAP_Y, MAP_Y + MAP_H, 13)

    # --- status strip --------------------------------------------------------
    text(px, FONT_MD, 12, STATUS_Y + 14, 'x 12  y 20    [PAUSE]    TAP TOOL BELOW', INK)
    hline(px, STATUS_Y, 0, W, INK)

    # --- toolbar -------------------------------------------------------------
    hline(px, TOOL_Y - 1, 0, W, INK)
    tools = ['X', '=', 'R', 'C', 'I', 'P', 'T', '?']
    slot = W // len(tools)           # 75
    for i, sym in enumerate(tools):
        x = i * slot + (slot - 56) // 2
        sel = (i == 0)
        fill(px, x, TOOL_Y + 4, 56, 56, INK if sel else PAPER)
        if not sel:
            for e in range(4):
                hline(px, TOOL_Y + 4 + e, x, x + 56, INK)
                hline(px, TOOL_Y + 59 - e, x, x + 56, INK)
                vline(px, x + e, TOOL_Y + 4, TOOL_Y + 60, INK)
                vline(px, x + 55 - e, TOOL_Y + 4, TOOL_Y + 60, INK)
        text(px, FONT_SYM, x + 28, TOOL_Y + 14, sym, PAPER if sel else INK, anchor='ma')

    img = Image.new('L', (W, H), 255)
    buf = img.load()
    for y in range(H):
        for x in range(W):
            buf[x, y] = GREY_RAMP[px[y * W + x]]
    return img

# --- fonts -----------------------------------------------------------------
def font(path, size):
    try:
        return ImageFont.truetype(path, size)
    except Exception:
        return ImageFont.load_default()

BOLD = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
REG = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
FONT_LG = font(BOLD, 26)
FONT_MD = font(BOLD, 22)
FONT_SYM = font(BOLD, 34)

if __name__ == '__main__':
    render().save('poc/screen_600x800_grey.png')
    print('wrote poc/screen_600x800_grey.png')
