#!/usr/bin/env python3
"""POC: SimCity-on-Kindle 600x800 screen mock.

Bakes REAL classic tileset art (upstream/tiles.bmp) through the same
luma -> 4-bit grey pipeline the engine will run (ink level 0..15 ->
grey_ramp bytes, the exact ramp from jailbreak-kindle engine eink.c),
then lays out the full panel screen at 600x800.

Two output PNGs:
  poc/screen_600x800_grey.png  -- greyscale, exactly as the panel shows it
  poc/screen_600x800_ink.png   -- toolbar + icons stamped as TRUE on/off ink
                                   (what the 2-bit eink path shows),
                                   everything else greyscale
"""
import math
from PIL import Image, ImageDraw, ImageFont

# --- panel geometry (372x475 @ 8 bpp greyscale; drawn at 8x8 dots per px -> 600x800)
PW, PH = 372, 475
UPSCALE = 8
W, H = 600, 800

# ink level 0..15 -> pixel byte; 255 = paper. EXACT ramp from engine eink.c
GREY_RAMP = [255, 238, 221, 204, 187, 170, 153, 136,
             119, 102,  85,  68,  51,  34,  17,   0]

TILE = 16  # classic micropolis tiles are 16x16, drawn 1:1 => 23x27 tiles visible

# --- helpers -------------------------------------------------------------
def load_tile(name):
    im = Image.open(f'poc/src_{name}.png').convert('RGB')
    return im.resize((TILE, TILE))

def to_grey(px):
    """RGB pixel -> ink level 0..15 (paper = 15, solid ink = 0)."""
    return round((px[0] * 299 + px[1] * 587 + px[2] * 114) / 1000 / 17)

def bake(tile):
    """tile RGB -> 4-bit ink levels (0..15), nearest-level match to luma."""
    px = list(tile.getdata())
    return [to_grey(p) for p in px]

def stretch(levels, lo=2, hi=13):
    """global contrast stretch of a 16-level set to [lo..hi]."""
    vals = [v for g in levels for v in g]
    mn, mx = min(vals), max(vals)
    if mx <= mn:
        return levels
    span = mx - mn
    out = {}
    for v in range(16):
        out[v] = lo + round((v - mn) * (hi - lo) / span)
        out[v] = min(hi, max(lo, out[v]))
    return

# --- map ------------------------------------------------------------------
# 23 x 27 city tiles; letters: . grass, T tree, r road, R residential,
# C commercial, I industrial, w water, W water edge, B barren, P power, k park
MAP = [
    '.......................',
    '.......T.....T........r.',
    '..wwwwwwwwww..........r.',
    '.wwwwwwwwwwww.T.......rr',
    '.wwwwwwwwwwwww........rr',
    '.wwwwwwwwwwwwww........r',
    '..wwwwwwwwwww...........',
    '.......................',
    '..rrrrrrrr.............',
    '....R..R...T.....C.....',
    '....R..RR..TT....C..C..',
    '....RR.RR...TT...CC....',
    '.....RRR.....T.........',
    '...rrrrrrrrrrrrrr......',
    '.....I..I..............',
    '....II..II.....TTTT....',
    '....I...I......TTTT....',
    '.......................',
    '...kkkk....BBB...P.....',
    '...k..k....BBB..PP.....',
    '...k..k....BBB..P......',
    '...kkkk...........P....',
    '.......................',
    '..BBBBBB..W.W.W.W......',
    '.BBBBBBB..W.W.W.W......',
    '..........WWWWWWWW.....',
    '.......................',
]
ROWS, COLS = len(MAP), len(MAP[0])
assert 23 <= COLS and ROWS == 27

TILES = {
    'w': bake(load_tile('water')),
    'W': bake(load_tile('water_edge')),
    'T': bake(load_tile('tree')),
    '.': bake(load_tile('grass')),
    'B': bake(load_tile('barren')),
    'r': bake(load_tile('road_h')),
    'R': bake(load_tile('residential')),
    'C': bake(load_tile('commercial')),
    'I': bake(load_tile('industrial')),
    'P': bake(load_tile('power')),
    'k': bake(load_tile('park')),
}
# per-tile contrast stretch: the classic palette is pastel, so straight luma
# baking would land every tile mid-grey. Each tile gets its own full
# light->dark range mapped into the visible grey band (levels 2..13), which
# is what makes water, trees, roads and buildings distinct on eink.
_lo, _hi = 2, 13
for k in TILES:
    vals = TILES[k]
    mn, mx = min(vals), max(vals)
    if mx <= mn:
        continue
    span = mx - mn
    remap = {}
    for v in range(16):
        remap[v] = min(_hi, max(_lo, _lo + round((v - mn) * (_hi - _lo) / span)))
    TILES[k] = [remap[v] for v in vals]

def fill_rect(px, x, y, w, h, v):
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            px[yy * W + xx] = v

def draw_tick(px, x, y, v):
    """small 4x2 dark tick"""
    fill_rect(px, x, y, 4, 2, v)

def draw_text(px, font, x, y, s, v=0):
    tmp = Image.new('L', (400, 40))
    d = ImageDraw.Draw(tmp)
    d.text((0, 0), s, font=font, fill=0)
    x0, y0, x1, y1 = d.textbbox((0, 0), s, font=font)
    for yy in range(y0, y1):
        row = list(tmp.crop((0, yy, 400, yy + 1)).getdata())
        for xx, p in enumerate(row):
            if p:
                tx, ty = x + xx, y + yy - y0
                if 0 <= tx < W and 0 <= ty < H:
                    px[ty * W + tx] = v

def render(ink_mode=False):
    # pixel rows; each entry is a GREY LEVEL 0..15 (16 = glass/paper sentinel)
    px = [16] * (W * H)

    # --- header: budget strip -------------------------------------------
    fill_rect(px, 0, 0, W, 8, 15)                    # paper band
    draw_tick(px, 8, 7, 0)                          # bottom hairline
    draw_tick(px, 8 + 40, 7, 0)
    draw_tick(px, 8 + 80, 7, 0)
    # power icon: solid square with pole
    fill_rect(px, 14, 2, 2, 5, 0)                   # pole
    fill_rect(px, 16, 1, 1, 2, 0)                   # bolt top
    fill_rect(px, 17, 3, 1, 2, 0)
    # $-sign as budget icon (simple $ drawn with lines)
    d = '$'
    # bars: each budget (power, water, road, fire) as 10px segment
    seg = [(60, 70), (68, 78), (76, 86), (84, 92)]
    for i, (x0, x1) in enumerate(seg):
        fill_rect(px, x0, 3, x1 - x0, 2, 0 if i % 2 == 0 else 8)

    # --- map ---------------------------------------------------------------
    for yy in range(ROWS):
        for xx in range(COLS):
            g = TILES[MAP[yy][xx]]
            ox, oy = xx * TILE, 12 + yy * TILE
            for ty in range(TILE):
                row = g[ty * TILE:(ty + 1) * TILE]
                base = (oy + ty) * W + ox
                for txx, v in enumerate(row):
                    px[base + txx] = v

    # --- status strip -------------------------------------------------------
    y0 = 12 + ROWS * TILE                                  # 444
    fill_rect(px, 0, y0, W, 452 - y0, 15)
    draw_tick(px, 0, y0, 0)
    draw_tick(px, 0, 452 - 1, 0)
    # text
    fill_rect(px, 4, y0 + 2, 0, 0, 0)  # noop
    draw_text(px, FONT_SM, 8, y0 + 1, '\u2666S 100k  \u25C8Pop 5k  \u25B2Pr 60k')
    draw_text(px, FONT_SM, 8, y0 + 9, 'FUNDS $10,000')

    # --- toolbar ------------------------------------------------------------
    yt = 462
    fill_rect(px, 0, yt - 2, W, 466 - (yt - 2), 15)     # gap band
    draw_tick(px, 0, yt - 2, 0)
    # 10 tool squares 24x24, 30px stride, starting x=6
    x = 6
    tools = ['\u22A0', '\u219F', '\u25C7', '\u25A2', '\u25A3', '\u2588', '\u25B2', '\u2605', '\u2699', '\u274E']
    for ti, sym in enumerate(tools):
        fill_rect(px, x, yt, 24, 24, 15)
        draw_tick(px, x, yt, 0)
        draw_tick(px, x + 23, yt, 0)
        draw_tick(px, x, yt + 23, 0)
        draw_tick(px, x + 23, yt + 23, 0)
        if ink_mode:
            fill_rect(px, x, yt, 23, 23, 0 if ti == 0 else 15)  # selected = black square
        else:
            draw_text(px, FONT_SYM, x + 8, yt + 4, sym, 0)
        x += 30
    # status line above toolbar (map coords + speed)
    draw_text(px, FONT_SM, 8, 452, 'x 12  y 20   [---]', 12)

    # --- compose -------------------------------------------------------------
    img = Image.new('L', (W, H), 255)
    buf = img.load()
    for y in range(H):
        for x in range(W):
            v = px[y * W + x]
            if v == 16:
                continue
            g = GREY_RAMP[min(v, 15)]
            if ink_mode and v < 8:
                g = 0
            buf[x, y] = g
    return img

# fonts
FONT_SM = None
FONT_SYM = None
for p in ('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',
          '/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf'):
    try:
        FONT_SM = ImageFont.truetype(p, 9)
        FONT_SYM = ImageFont.truetype(p, 16)
        break
    except Exception:
        continue
if FONT_SM is None:
    FONT_SM = ImageFont.load_default()
    FONT_SYM = ImageFont.load_default()

# sepia-friendly: override ramp? no - keep exact ramp, engine will render identically.

render(ink_mode=False).save('poc/screen_600x800_grey.png')
render(ink_mode=True).save('poc/screen_600x800_ink.png')
print('wrote poc/screen_600x800_grey.png and poc/screen_600x800_ink.png')