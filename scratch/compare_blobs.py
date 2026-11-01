import zlib, struct
data = open('/tmp/mmcblk0-first32m','rb').read()

def inflate(off):
    return zlib.decompressobj(16+zlib.MAX_WBITS).decompress(data[off:off+20000])

def unpack(px):
    im = bytearray(600*800)
    for i, b in enumerate(px):
        for k in range(4):
            im[i*4+k] = (b >> (6-2*k)) & 3
    return im

plain = inflate(0xC92C0)
wait  = inflate(0xC9D30)
repair= inflate(0xCAF68)
print('plain header', struct.unpack('<III', plain[:12]))
print('wait  header', struct.unpack('<III', wait[:12]))
print('repair header', struct.unpack('<III', repair[:12]))
p = unpack(plain[12:]); w = unpack(wait[12:])
diff = [i for i in range(600*800) if p[i] != w[i]]
print('plain vs wait diff pixels:', len(diff))
if diff:
    minx, maxx = min(d%600 for d in diff), max(d%600 for d in diff)
    miny, maxy = min(d//600 for d in diff), max(d//600 for d in diff)
    print('diff bounding box x', minx, maxx, 'y', miny, maxy)
rp = unpack(repair[12:])
diff2 = [i for i in range(600*800) if p[i] != rp[i]]
print('plain vs repair diff:', len(diff2))
if diff2:
    minx, maxx = min(d%600 for d in diff2), max(d%600 for d in diff2)
    miny, maxy = min(d//600 for d in diff2), max(d//600 for d in diff2)
    print('repair diff bbox x', minx, maxx, 'y', miny, maxy)