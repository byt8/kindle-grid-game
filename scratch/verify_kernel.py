import gzip, struct, zlib
data = open('/tmp/mmcblk0-first32m','rb').read()

for name, off in [('main-plain',0xC92C0),('main-wait',0xC9D30),('main-repair',0xCAF68),
                  ('sec-plain',0xEC7104),('sec-wait',0xEC7B74)]:
    buf = data[off:off+20000]
    d = zlib.decompressobj(16 + zlib.MAX_WBITS)
    try:
        out = d.decompress(buf)
        consumed = len(buf) - len(d.unconsumed_tail)
    except Exception as e:
        print(name, hex(off), 'inflate ERR', e)
        continue
    print(name, hex(off), 'consumed', consumed, 'out len', len(out), 'first16', out[:16].hex())
    if len(out) >= 12:
        w, h, bpp = struct.unpack('<III', out[:12])
        print('   header', w, h, bpp)
        # if header not plausible (600x800x2), try treating whole thing as pixels or 2-bit rows
    # try finding a 12-byte w/h/bpp header inside the output
    for j in range(min(len(out)-12, 200)):
        w, h, bpp = struct.unpack('<III', out[j:j+12])
        if w == 600 and h == 800 and bpp == 2:
            print('   embedded header at', j)
            break