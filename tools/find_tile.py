import re
import sys

h = open('src/sprites.h').read()
ids = re.findall(r'^    "([a-z_]+/[a-z_]+)",$', h, re.M)
blobs = re.findall(r'^    \{((?:0x[0-9a-f]{2},){799}0x[0-9a-f]{2})\},$', h, re.M)
print('ids', len(ids), 'blobs', len(blobs))
want = sys.argv[2] if len(sys.argv) > 2 else 'terrain_tiles/grass'
i = ids.index(want)
vals = [int(b, 16) for b in blobs[i].split(',')]
px = [(vals[k >> 1] >> ((k & 1) * 4)) & 15 for k in range(1600)]

# The grey eink.c's grey layer lays down for a tone; a transparent pixel
# has no say, because whatever is under it on the map decides that square.
GREY_RAMP = (255, 238, 221, 204, 187, 170, 153, 136,
             119, 102, 85, 68, 51, 34, 17, 0)


def want_grey(x, y):
    v = px[y * 40 + x]
    if v == 0:
        return None                     # transparent: terrain decides
    level = 0 if v == 1 else v - 1      # paper is ink level 0
    return GREY_RAMP[(level * 15 + 4) // 8]

d = open(sys.argv[1], 'rb').read()
hdr = d.split(b'\n', 3)
w, hh = map(int, hdr[1].split())
body = hdr[3]
print('pgm', w, hh, 'looking for', want)


def match(ox, oy):
    for y in range(40):
        for x in range(40):
            g = want_grey(x, y)
            if g is None:
                continue
            if body[(oy + y) * w + ox + x] != g:
                return False
    return True


hits = []
for y in range(0, hh - 40):
    for x in range(0, w - 40):
        if match(x, y):
            hits.append((x, y))
print('found', len(hits), 'places; first', hits[:6])
