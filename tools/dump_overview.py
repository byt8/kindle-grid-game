import sys

d = open(sys.argv[1], 'rb').read()
hdr = d.split(b'\n', 3)
w, h = map(int, hdr[1].split())
body = hdr[3]
step = int(sys.argv[2]) if len(sys.argv) > 2 else 8
print('pgm', w, h, 'step', step)
for y in range(0, h, step):
    row = ''
    for x in range(0, w, step):
        n = sum(1 for yy in range(y, min(y + step, h)) for xx in range(x, min(x + step, w)) if body[yy * w + xx] < 128)
        row += '#' if n > step * step * 0.4 else ('+' if n > step * step * 0.12 else ('.' if n else ' '))
    print(f'{y:4d} {row}')
