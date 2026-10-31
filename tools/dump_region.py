import sys

d = open(sys.argv[1], 'rb').read()
hdr = d.split(b'\n', 3)
w, h = map(int, hdr[1].split())
body = hdr[3]
ox, oy = int(sys.argv[2]), int(sys.argv[3])

for y in range(oy, oy + 40):
    row = ''
    for x in range(ox, ox + 40):
        row += '#' if body[y * w + x] < 128 else '.'
    print(row)
