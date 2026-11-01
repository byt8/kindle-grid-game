import gzip, struct, zlib, binascii
from PIL import Image, ImageFilter

data = open('/tmp/mmcblk0-first32m','rb').read()

# --- build a candidate: blur 14 full-bleed 1-bit, plus the stock wait decode, side by side ---
def decode_stock(off):
    buf = data[off:off+20000]
    out = zlib.decompressobj(16 + zlib.MAX_WBITS).decompress(buf)
    return out

def pack_1bit(levels):
    out = bytearray()
    for i in range(0, len(levels), 4):
        out.append((levels[i]<<6)|(levels[i+1]<<4)|(levels[i+2]<<2)|levels[i+3])
    return bytes(out)

def unpack(px):
    im = bytearray(600*800)
    for i, b in enumerate(px):
        for k in range(4):
            im[i*4+k] = (b >> (6-2*k)) & 3
    return im

def to_img(levels):
    img = Image.new('L', (600,800))
    img.putdata([v*85 for v in levels])   # 0->black(0), 3->white(255)
    return img

# stock wait
rd = decode_stock(0xC9D30)
stock = unpack(rd[12:])
stock_img = to_img(stock)

# candidate: blur14 full-detail 1-bit (0=black,3=white)
art = Image.open('/home/brendan/workspaces/kindle-grid-game/boot.png').convert('L').filter(ImageFilter.GaussianBlur(14)).resize((600,800), Image.LANCZOS)
can = [0 if v < 128 else 3 for v in list(art.getdata())]
can_img = to_img(can)
print('candidate gzip size:', len(gzip.compress(pack_1bit(can),9)), '(slot wait=4664)')

# contact sheet
sheet = Image.new('L', (1200, 800), 255)
sheet.paste(stock_img, (0,0))
sheet.paste(can_img, (600,0))
sheet.save('/home/brendan/workspaces/kindle-grid-game/scratch/stock_vs_candidate.png')
print('saved scratch/stock_vs_candidate.png')