import gzip, struct
from PIL import Image, ImageFilter

def pack_1bit(levels):
    out = bytearray()
    for i in range(0, len(levels), 4):
        out.append((levels[i]<<6)|(levels[i+1]<<4)|(levels[i+2]<<2)|levels[i+3])
    return bytes(out)

def gzsize(levels):
    raw = struct.pack('<III', 600, 800, 2) + pack_1bit(levels)
    return len(gzip.compress(raw, 9))

def corner_level(im, blur):
    """Average darkness of the four corners of the blurred art -> 0 (black)..3 (white)."""
    w, h = im.size
    px = im.convert('L')
    samples = []
    for cx, cy in [(10,10),(w-11,10),(10,h-11),(w-11,h-11)]:
        vals = [px.getpixel((cx+dx, cy+dy)) for dx in range(-5,6) for dy in range(-5,6)]
        samples.append(sum(vals)/len(vals))
    mean = sum(samples)/len(samples)
    return mean, 0 if mean < 64 else (1 if mean < 128 else (2 if mean < 192 else 3))

art = Image.open('/home/brendan/workspaces/kindle-grid-game/boot.png')
blurred = art.convert('L').filter(ImageFilter.GaussianBlur(15))
mean, bg = corner_level(blurred, 15)
print(f'art corner mean {mean:.1f} -> background level {bg} (0=black)')

for aw, ah in [(300,400),(320,427),(360,480),(380,507),(400,533),(420,560),(450,600),(480,640)]:
    im = blurred.resize((aw, ah), Image.LANCZOS)
    px = im.load()
    levels = [0]*(600*800)
    x0, y0 = (600-aw)//2, (800-ah)//2
    for y in range(ah):
        for x in range(aw):
            v = px[x, y]
            levels[(y0+y)*600 + (x0+x)] = 3 if v >= 128 else 0
    n = gzsize(levels)
    fits = 'BOTH<=2672' if n <= 2672 else ('wait<=4664' if n <= 4664 else 'NO')
    print(f'{aw}x{ah} centered on black: {n} bytes  [{fits}]')
    if aw == 400 and ah == 533:
        img = Image.new('L', (600,800), bg*85)
        img.putdata(levels)
        # put bg 0 -> show black; but levels already 0/3; use preview mapping
        prev = Image.new('L', (600,800), 0)
        prev.putdata([v*85 for v in levels])
        prev.save('/home/brendan/workspaces/kindle-grid-game/scratch/final_400x533.png')
    if aw == 450 and ah == 600:
        prev = Image.new('L', (600,800), 0)
        prev.putdata([v*85 for v in levels])
        prev.save('/home/brendan/workspaces/kindle-grid-game/scratch/final_450x600.png')