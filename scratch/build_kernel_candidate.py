import gzip, zlib, struct, hashlib
from PIL import Image, ImageFilter

# Build kernel 2-bit payload 600x800 from our art, blurred & thresholded,
# pack with gzip, and report size + md5.

def pack_levels_2bit(levels):
    # levels: 0 (black) .. 3 (white), 4 px/byte
    out = bytearray()
    for i in range(0, len(levels), 4):
        out.append((levels[i]<<6)|(levels[i+1]<<4)|(levels[i+2]<<2)|levels[i+3])
    return bytes(out)

def build(blur, thresh=128, path_out=None):
    art = Image.open('/home/brendan/workspaces/kindle-grid-game/boot.png').convert('L')
    if blur:
        art = art.filter(ImageFilter.GaussianBlur(blur))
    art = art.resize((600,800), Image.LANCZOS)
    px = [0 if v < thresh else 3 for v in list(art.getdata())]
    payload = pack_levels_2bit(px)
    header = struct.pack('<III', 600, 800, 2)
    raw = header + payload
    gz = gzip.compress(raw, 9)
    if path_out:
        open(path_out,'wb').write(gz)
        # also save a preview PNG of the rendered frame
        img = Image.new('L', (600,800))
        img.putdata([v*85 for v in px])   # 0 black -> 0, 3 white -> 255
        img.save(path_out.replace('.gz','.png'))
    return len(gz)

for r in (15,16,17):
    n = build(r, path_out=f'/home/brendan/workspaces/kindle-grid-game/scratch/kernel_blur{r}.gz')
    print(f'blur{r}: {n} bytes')