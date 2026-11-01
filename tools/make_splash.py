#!/usr/bin/env python3
"""Make a Kindle boot picture (600x800, 8-bit greyscale PNG) from any PNG.

    python3 tools/make_splash.py --logo ~/aijly.com/static/logo.png \
                                 --out kindle-splash.png

The splash program opens /usr/share/blanket/bg_xsmall_defaultbg.png.
A second copy of the same picture is
/opt/amazon/low_level_screens/600x800/splash.png
(the /var/local/blanket link only points at that file). Both sit on the
read-only root: /usr/sbin/mntroot rw, copy over both, /usr/sbin/mntroot ro.

Why this script exists: the box has no PIL, no ImageMagick and no ffmpeg, so
the decode/scale/encode is done here with zlib and struct only. Output is PNG,
8-bit, colour type 0 (greyscale), 600x800, no alpha. Colour type 6 draws
nothing. The stock defaultbg file is a palette PNG of the same picture; this
writes the greyscale form the stock splash.png already uses.
"""
import argparse
import struct
import zlib

SCREEN_W, SCREEN_H = 600, 800


# ---- minimal PNG reader (8-bit, non-interlaced, any colour type) -----------

def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def read_png(path):
    """-> (w, h, rows) where rows is a list of RGBA bytearrays."""
    data = open(path, 'rb').read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise SystemExit(f'{path}: not a PNG')
    pos, idat = 8, []
    w = h = depth = ctype = interlace = None
    while pos < len(data):
        (length,) = struct.unpack('>I', data[pos:pos + 4])
        tag = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if tag == b'IHDR':
            w, h, depth, ctype, _comp, _filt, interlace = struct.unpack(
                '>IIBBBBB', body)
        elif tag == b'IDAT':
            idat.append(body)
        elif tag == b'IEND':
            break
    if depth != 8:
        raise SystemExit(f'{path}: only 8-bit PNGs are supported (depth {depth})')
    if interlace:
        raise SystemExit(f'{path}: interlaced PNGs are not supported')
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(ctype)
    if channels is None:
        raise SystemExit(f'{path}: unsupported colour type {ctype}')
    if ctype == 3:
        raise SystemExit(f'{path}: palette PNGs are not supported')

    raw = zlib.decompress(b''.join(idat))
    stride = w * channels
    rows, prev, p = [], bytearray(stride), 0
    for _ in range(h):
        ftype = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        if ftype == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xff
        elif ftype == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xff
        elif ftype == 3:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xff
        elif ftype == 4:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                upleft = prev[i - channels] if i >= channels else 0
                line[i] = (line[i] + _paeth(left, prev[i], upleft)) & 0xff
        elif ftype != 0:
            raise SystemExit(f'{path}: bad filter type {ftype}')
        rows.append(line)
        prev = line

    out = []
    for line in rows:
        row = bytearray(w)
        for x in range(w):
            px = line[x * channels:(x + 1) * channels]
            if channels == 1:
                row[x] = px[0]
            elif channels == 2:
                row[x] = px[0]          # grey, alpha dropped
            elif channels == 3:
                row[x] = (px[0] * 299 + px[1] * 587 + px[2] * 114) // 1000
            else:                        # RGBA over white
                a = px[3]
                g = (px[0] * 299 + px[1] * 587 + px[2] * 114) // 1000
                row[x] = (g * a + 255 * (255 - a)) // 255
        out.append(row)
    return w, h, out


def write_png(path, w, h, pix):
    raw = b''.join(b'\x00' + bytes(pix[y * w:(y + 1) * w]) for y in range(h))

    def chunk(tag, body):
        return (struct.pack('>I', len(body)) + tag + body
                + struct.pack('>I', zlib.crc32(tag + body) & 0xffffffff))

    png = (b'\x89PNG\r\n\x1a\n'
           + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 0, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(raw, 9))
           + chunk(b'IEND', b''))
    open(path, 'wb').write(png)


def crop_to_ink(rows, w, h, thresh=235):
    """Trim near-white borders so the artwork fills the screen."""
    minx, miny, maxx, maxy = w, h, -1, -1
    for y, row in enumerate(rows):
        for x, v in enumerate(row):
            if v < thresh:
                minx, maxx = min(minx, x), max(maxx, x)
                miny, maxy = min(miny, y), max(maxy, y)
    if maxx < 0:
        raise SystemExit('logo is blank: nothing darker than the crop threshold')
    return ([r[minx:maxx + 1] for r in rows[miny:maxy + 1]],
            maxx - minx + 1, maxy - miny + 1)


def fit(w, h, box_w, box_h):
    s = min(box_w / w, box_h / h)
    return max(1, int(w * s)), max(1, int(h * s))


def sample(w, h, rows, dw, dh):
    """Box-average downscale."""
    out = bytearray(dw * dh)
    for y in range(dh):
        y0, y1 = y * h // dh, max(y * h // dh + 1, (y + 1) * h // dh)
        for x in range(dw):
            x0, x1 = x * w // dw, max(x * w // dw + 1, (x + 1) * w // dw)
            tot = n = 0
            for yy in range(y0, y1):
                r = rows[yy]
                for xx in range(x0, x1):
                    tot += r[xx]
                    n += 1
            out[y * dw + x] = tot // n
    return out


def preview(canvas, w, h, cols=56):
    step = max(1, w // cols)
    rows = h // step // 2
    chars = ' .:-=+*#%@'
    for ry in range(rows):
        line = ''
        for rx in range(cols):
            tot = n = 0
            for y in range(ry * step * 2, min(h, (ry + 1) * step * 2)):
                base = y * w
                for x in range(rx * step, min(w, (rx + 1) * step)):
                    tot += canvas[base + x]
                    n += 1
            line += chars[min(9, (255 - tot // n) * 10 // 256)] if n else ' '
        print(line)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--logo', required=True)
    ap.add_argument('--out', default='kindle-splash.png')
    ap.add_argument('--width', type=int, default=SCREEN_W)
    ap.add_argument('--height', type=int, default=SCREEN_H)
    ap.add_argument('--margin', type=int, default=60,
                    help='free space kept around the logo, in pixels')
    ap.add_argument('--preview', action='store_true')
    ap.add_argument('--no-crop', action='store_true',
                    help='keep the logo\'s own white border')
    args = ap.parse_args()

    w, h, rows = read_png(args.logo)
    if not args.no_crop:
        rows, w, h = crop_to_ink(rows, w, h)
    dw, dh = fit(w, h, args.width - 2 * args.margin, args.height - 2 * args.margin)
    art = sample(w, h, rows, dw, dh)

    canvas = bytearray(b'\xff' * (args.width * args.height))
    ox, oy = (args.width - dw) // 2, (args.height - dh) // 2
    for y in range(dh):
        canvas[(oy + y) * args.width + ox:(oy + y) * args.width + ox + dw] = \
            art[y * dw:(y + 1) * dw]

    write_png(args.out, args.width, args.height, canvas)
    print(f'{args.out}: {args.width}x{args.height} greyscale, '
          f'logo {dw}x{dh} at {ox},{oy}')
    if args.preview:
        preview(canvas, args.width, args.height)


if __name__ == '__main__':
    main()
