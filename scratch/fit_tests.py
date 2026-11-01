import gzip, struct, zlib, binascii
from PIL import Image, ImageFilter, ImageOps

data = open('/tmp/mmcblk0-first32m','rb').read()

# ---- 1) verify uImage CRC scheme ----
print("== uImage CRC ==")
for off in (0x41000, 0xE41000):
    hdr = data[off:off+64]
    magic, hcrc, ts, size, load, entry, dcrc = struct.unpack('>IIIIIII', hdr[:28])
    dat = data[off+64:off+64+size]
    calc_d = binascii.crc32(dat) & 0xffffffff
    calc_h_zeroed = binascii.crc32(hdr[:4] + b'\0'*4 + hdr[8:64]) & 0xffffffff
    calc_h_from4 = binascii.crc32(hdr[4:64]) & 0xffffffff
    print(f'@{off:#x} size={size} stored hcrc={hcrc:#08x} dcrc={dcrc:#08x} | calc hcrc(zeroed)={calc_h_zeroed:#08x} hcrc(4:64)={calc_h_from4:#08x} dcrc={calc_d:#08x}')

# ---- 2) fit tests: pack 1-bit 600x800 from various processed versions of boot.png ----
def pack_1bit(levels):
    out = bytearray()
    for i in range(0, len(levels), 4):
        out.append((levels[i]<<6)|(levels[i+1]<<4)|(levels[i+2]<<2)|levels[i+3])
    return bytes(out)

def gzsize(im_large, w=600, h=800, thresh=128, invert=False):
    im = im_large.convert('L').resize((w, h), Image.LANCZOS)
    px = list(im.getdata())
    # mapping for kernel: 0=black, 3=white
    levels = [0 if v < thresh else 3 for v in px]
    if invert:
        levels = [3 - v for v in levels]
    return len(gzip.compress(pack_1bit(levels), 9))

art = Image.open('/home/brendan/workspaces/kindle-grid-game/boot.png')
print("\n== gzip size (bytes) of processed art, kernel slots: plain=2672 wait=4664 ==")
for r in (0, 2, 4, 6, 8, 10, 12, 14, 16):
    im = art if r == 0 else art.filter(ImageFilter.GaussianBlur(r))
    print(f'blur{r:>2} full-bleed : {gzsize(im)}')

print("\n== centered on white, various art sizes (no blur) ==")
for aw, ah in [(600,800),(500,667),(450,600),(400,533),(360,480),(320,427),(300,400),(280,373)]:
    im = art.convert('L').resize((aw, ah), Image.LANCZOS)
    canvas = Image.new('L', (600,800), 255)
    canvas.paste(im, ((600-aw)//2, (800-ah)//2))
    px = [0 if v < 128 else 3 for v in list(canvas.getdata())]
    print(f'art {aw}x{ah} centered : {len(gzip.compress(pack_1bit(px),9))}')