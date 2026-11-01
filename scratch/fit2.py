import gzip, zlib
from PIL import Image, ImageFilter

data = open('/tmp/mmcblk0-first32m','rb').read()

# How much padding sits after the wait blob before the next non-padding byte?
def slot_gap(end):
    # end = offset just after known blob (by member length). print bytes after in wait blob region
    pass

def pack_1bit(levels):
    out = bytearray()
    for i in range(0, len(levels), 4):
        out.append((levels[i]<<6)|(levels[i+1]<<4)|(levels[i+2]<<2)|levels[i+3])
    return bytes(out)

def gzsize(im, thresh=128):
    px = [0 if v < thresh else 3 for v in list(im.getdata())]
    return len(gzip.compress(pack_1bit(px), 9))

art0 = Image.open('/home/brendan/workspaces/kindle-grid-game/boot.png').convert('L')
# blur sweep near 14-20
for r in (13,14,15,16,17,18,19,20):
    im = art0.filter(ImageFilter.GaussianBlur(r)).resize((600,800), Image.LANCZOS)
    print(f'blur{r}: {gzsize(im)}')

# Also: the region between wait blob end and next blob start - show raw bytes at 0xCAF00..0xCAF68
print('\nbytes before repair magic (0xCAF00..0xCAF68):')
print(data[0xCAF00:0xCAF68].hex())