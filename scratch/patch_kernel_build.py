import gzip, struct, zlib, binascii, json
from PIL import Image, ImageFilter

SRC = '/tmp/mmcblk0-first32m'
OUT = '/home/brendan/workspaces/kindle-grid-game/scratch'

orig = open(SRC,'rb').read()

# --- true member lengths: next gzip magic after each offset ---
def next_magic(off, start_from=2):
    i = orig.find(b'\x1f\x8b\x08', off+start_from, off+20000)
    return orig.find(b'\x1f\x8b\x08', off+start_from, off+2) if False else i

def member_len(off):
    nxt = orig.find(b'\x1f\x8b\x08', off+2, off+20000)
    assert nxt != -1, hex(off)
    return nxt - off

slots = {
    'main-plain': 0xC92C0,
    'main-wait':  0xC9D30,
    'sec-plain':  0xEC7104,
    'sec-wait':   0xEC7B74,
}
gaps = {k: member_len(v) for k,v in slots.items()}
print('true gaps:', {k: hex(v) for k,v in gaps.items()})

# --- build the final 2-bit frame again ---
art = Image.open('/home/brendan/workspaces/kindle-grid-game/boot.png').convert('L')
blur = art.filter(ImageFilter.GaussianBlur(15))
aw, ah = 320, 427
im = blur.resize((aw, ah), Image.LANCZOS)
px = im.load()
levels = [0]*(600*800)
x0, y0 = (600-aw)//2, (800-ah)//2
for y in range(ah):
    for x in range(aw):
        levels[(y0+y)*600 + (x0+x)] = 3 if px[x, y] >= 128 else 0

def pack(levels):
    out = bytearray()
    for i in range(0, len(levels), 4):
        out.append((levels[i]<<6)|(levels[i+1]<<4)|(levels[i+2]<<2)|levels[i+3])
    return bytes(out)

raw = struct.pack('<III', 600, 800, 2) + pack(levels)
gz = gzip.compress(raw, 9)
print('final blob:', len(gz), 'bytes; smallest slot', min(gaps.values()), 'bytes -> fits:', len(gz) <= min(gaps.values()))
assert len(gz) <= min(gaps.values())

def hashlib_md5(b):
    import hashlib
    return hashlib.md5(b).hexdigest()

# --- stock backups ---
stock = {k: orig[v:v+gaps[k]] for k,v in slots.items()}
for k in stock:
    print(f'stock {k}: {len(stock[k])} bytes md5={hashlib_md5(stock[k])[:8]}')

meta = {
    'new_blob': gz.hex(),
    'new_blob_len': len(gz),
    'slots': {k: {'offset': hex(v), 'gap': gaps[k],
                  'stock_md5': hashlib_md5(stock[k])} for k,v in slots.items()},
    'dcrc_main_old': None, 'dcrc_sec_old': None,
}
# dcrc old values
meta['dcrc_main_old'] = orig[0x41000+24:0x41000+28].hex()
meta['dcrc_sec_old']  = orig[0xE41000+24:0xE41000+28].hex()

# --- patch a fresh copy ---
data = bytearray(orig)
for k, off in slots.items():
    gap = gaps[k]
    data[off:off+len(gz)] = gz
    # zero-pad only the remaining space up to the NEXT member start
    if gap > len(gz):
        data[off+len(gz):off+gap] = b'\x00'*(gap-len(gz))
    print(f'patched {k} @{off:#x}: {len(gz)}B + {gap-len(gz)}B zero pad (slot {gap}B)')

# recompute dcrcs
def new_dcrc(data, uimg_off):
    hdr = bytes(data[uimg_off:uimg_off+64])
    magic, hcrc, ts, size = struct.unpack('>IIII', hdr[:16])
    dat = bytes(data[uimg_off+64:uimg_off+64+size])
    crc = binascii.crc32(dat) & 0xffffffff
    struct.pack_into('>I', data, uimg_off+24, crc)
    return crc

def new_hcrc(data, uimg_off):
    struct.pack_into('>I', data, uimg_off+4, 0)
    hdr = bytes(data[uimg_off:uimg_off+64])
    crc = binascii.crc32(hdr[:4] + b'\0'*4 + hdr[8:64]) & 0xffffffff  # all 64 bytes, hcrc field zeroed
    struct.pack_into('>I', data, uimg_off+4, crc)
    return crc

c1 = new_dcrc(data, 0x41000)
c2 = new_dcrc(data, 0xE41000)
h1 = new_hcrc(data, 0x41000)
h2 = new_hcrc(data, 0xE41000)
print(f'recomputed main kernel: dcrc={c1:#x} hcrc={h1:#x} | second: dcrc={c2:#x} hcrc={h2:#x}')
meta['dcrc_main_new'] = hex(c1); meta['dcrc_sec_new'] = hex(c2)
meta['hcrc_main_new'] = hex(h1); meta['hcrc_sec_new'] = hex(h2)

# --- verify every slot inflates to exactly our levels, and nothing else changed ---
for k, off in slots.items():
    gap = gaps[k]
    buf = bytes(data[off:off+gap])
    d = zlib.decompressobj(16+zlib.MAX_WBITS)
    out = d.decompress(buf)
    consumed = len(buf) - len(d.unconsumed_tail)
    assert consumed == gap, (k, consumed, gap)  # stream ends exactly at slot boundary? (should since next is a magic)
    w,h,bpp = struct.unpack('<III', out[:12])
    px = out[12:]
    got = []
    for b in px:
        got += [(b>>6)&3,(b>>4)&3,(b>>2)&3,b&3]
    assert (w,h,bpp)==(600,800,2), (w,h,bpp)
    assert got == levels, f'pixel mismatch {k}'
    print(f'verify {k}: OK ({w}x{h} bpp{bpp}, exact pixels, stream ends at slot edge)')

# confirm the byte RIGHT after the last slot's gap is still 1f 8b (next member intact)
for k, off in slots.items():
    nxt = off + gaps[k]
    assert data[nxt:nxt+2] == b'\x1f\x8b', (k, hex(nxt), data[nxt:nxt+2].hex())
    print(f'next member after {k} @{nxt:#x} intact: {data[nxt:nxt+2].hex()}')

# verify dcrc correctness
for off, name in [(0x41000,'main'),(0xE41000,'second')]:
    hdr = bytes(data[off:off+64])
    magic,hcrc,ts,size,load,entry,dcrc = struct.unpack('>IIIIIII', hdr[:28])
    dat = bytes(data[off+64:off+64+size])
    hz = binascii.crc32(hdr[:4]+b'\0'*4+hdr[8:64]) & 0xffffffff
    dz = binascii.crc32(dat) & 0xffffffff
    assert hz == hcrc and dz == dcrc, (name, hex(hz), hex(hcrc), hex(dz), hex(dcrc))
    print(f'uImage {name} CRCs valid: hcrc={hcrc:#x} dcrc={dcrc:#x}')

open(f'{OUT}/kernel-logo-rollback.json','w').write(json.dumps(meta, indent=1))
open(f'{OUT}/mmcblk0-first32m-patched.bin','wb').write(bytes(data))
prev = Image.new('L', (600,800), 0)
prev.putdata([v*85 for v in levels])
prev.save(f'{OUT}/final_preview.png')
print('saved kernel-logo-rollback.json, mmcblk0-first32m-patched.bin, final_preview.png')