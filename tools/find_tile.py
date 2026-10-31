import re
import sys

h = open('src/sprites.h').read()
ids = re.findall(r'^    "([a-z_]+/[a-z_]+)",$', h, re.M)
blobs = re.findall(r'^    \{((?:0x[0-9a-f]{2},){399}0x[0-9a-f]{2})\},$', h, re.M)
print('ids', len(ids), 'blobs', len(blobs))
want = sys.argv[2] if len(sys.argv) > 2 else 'terrain_tiles/grass'
i = ids.index(want)
vals = [int(b, 16) for b in blobs[i].split(',')]
px = [(vals[k >> 2] >> ((k & 3) * 2)) & 3 for k in range(1600)]

d = open(sys.argv[1], 'rb').read()
hdr = d.split(b'\n', 3)
w, hh = map(int, hdr[1].split())
body = hdr[3]
print('pgm', w, hh, 'looking for', want)


def at(x, y):
    return 1 if body[y * w + x] < 128 else 0


def match(ox, oy):
    for y in range(40):
        for x in range(40):
            wv = 1 if px[y * 40 + x] == 2 else 0
            if at(ox + x, oy + y) != wv:
                return False
    return True


hits = []
for y in range(0, hh - 40):
    for x in range(0, w - 40):
        if match(x, y):
            hits.append((x, y))
print('found', len(hits), 'places; first', hits[:6])
