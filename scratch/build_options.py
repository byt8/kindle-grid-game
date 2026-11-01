import gzip, struct, zlib
from PIL import Image, ImageFilter

def pack_levels_2bit(levels):
    out = bytearray()
    for i in range(0, len(levels), 4):
        out.append((levels[i]<<6)|(levels[i+1]<<4)|(levels[i+2]<<2)|levels[i+3])
    return bytes(out)

def build_raw(levels):
    return struct.pack('<III', 600, 800, 2) + pack_levels_2bit(levels)

def gzsize(levels):
    return len(gzip.compress(build_raw(levels), 9))

def art_to_levels(art, blur=0, thresh=128, canvas_w=600, canvas_h=800, fit=None):
    im = art.convert('L')
    if blur:
        im = im.filter(ImageFilter.GaussianBlur(blur))
    if fit is None:
        im = im.resize((canvas_w, canvas_h), Image.LANCZOS)
        px = [0 if v < thresh else 3 for v in list(im.getdata())]
        return px
    else:
        aw, ah = fit
        im = im.resize((aw, ah), Image.LANCZOS)
        canvas = Image.new('L', (canvas_w, canvas_h), 255)
        canvas.paste(im, ((canvas_w-aw)//2, (canvas_h-ah)//2))
        return [0 if v < thresh else 3 for v in list(canvas.getdata())]

def save_preview(levels, path):
    img = Image.new('L', (600,800))
    img.putdata([v*85 for v in levels])
    img.save(path)

art = Image.open('/home/brendan/workspaces/kindle-grid-game/boot.png')

print("== full-bleed blurred ==")
for r in (12,13,14,15,16):
    lv = art_to_levels(art, blur=r)
    print(f'blur{r}: {gzsize(lv)}')
    save_preview(lv, f'/home/brendan/workspaces/kindle-grid-game/scratch/opt_blur{r}.png')

print("\n== sharp, centered (no blur) ==")
for aw, ah in [(240,320),(260,347),(280,373),(290,387),(300,400)]:
    lv = art_to_levels(art, fit=(aw,ah))
    print(f'{aw}x{ah} centered: {gzsize(lv)}')
    save_preview(lv, f'/home/brendan/workspaces/kindle-grid-game/scratch/opt_{aw}x{ah}.png')

print("\n== plain slot 2672: tiny centered / low-res full ==")
for aw, ah in [(200,267),(180,240),(160,213),(140,187)]:
    lv = art_to_levels(art, fit=(aw,ah))
    print(f'{aw}x{ah} centered: {gzsize(lv)}')
# small full-bleed blocky
for w,h in [(120,160),(150,200),(180,240)]:
    im = art.convert('L').resize((w,h), Image.NEAREST).resize((600,800), Image.NEAREST)
    lv = [0 if v < 128 else 3 for v in list(im.getdata())]
    print(f'{w}x{h} upscaled full: {gzsize(lv)}')