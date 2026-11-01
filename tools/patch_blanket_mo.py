#!/usr/bin/env python3
"""Patch blanket.mo gettext catalogs: replace the msgstr of one msgid with a
new string, keeping every other byte of the file intact (resizing the string
table as needed).

The Kindle's "Please wait a moment while your Kindle starts up." reboot text
comes from the `shutdown.reboot.message` gettext entry, not from any image.
This rewrites that msgstr in a .mo file without touching the rest of it.

Layout written (same as the original file): header (28)
+ original-string index (8*n) + translation index (8*n)
+ hash table (copied verbatim; msgids do not change) + msgid pool + msgstr pool.

Usage:
    python3 tools/patch_blanket_mo.py FILE \
        [--msgid shutdown.reboot.message] \
        [--to "Please wait a moment while your device starts up."] \
        [--out FILE]      # default: overwrite FILE
"""
import argparse
import struct
import sys

HDR = '<7I'
REC = '<II'


def parse(d):
    magic, ver, n, o_idx, t_idx, hash_sz, hash_off = struct.unpack(HDR, d[:28])
    return magic, ver, n, o_idx, t_idx, hash_sz, hash_off


def rebuild(d, replacements):
    """Return a new .mo identical to `d` except msgstrs replaced per dict."""
    magic, ver, n, o_idx, t_idx, hash_sz, hash_off = parse(d)
    hash_bytes = d[hash_off:hash_off + hash_sz * 4]

    # read original entries. The pools are NUL-separated (each string
    # is followed by one 0x00 byte), so an entry occupies len+1 bytes.
    oids, ostrs = [], []
    for i in range(n):
        ol, oo = struct.unpack(REC, d[o_idx + i * 8: o_idx + i * 8 + 8])
        tl, to = struct.unpack(REC, d[t_idx + i * 8: t_idx + i * 8 + 8])
        oids.append(d[oo:oo + ol])
        s = d[to:to + tl]
        if oids[-1] in replacements:
            s = replacements[oids[-1]].encode('utf-8')
        ostrs.append(s)

    # layout: header | o-index | t-index | hash | o-pool | t-pool
    hash_off_new = 28 + 16 * n
    o_pool_off = hash_off_new + 4 * hash_sz
    o_recs, o_pool = [], bytearray()
    for oid in oids:
        off = o_pool_off + len(o_pool)
        o_recs.append((len(oid), off))
        o_pool += oid + b'\x00'
    t_pool_off = o_pool_off + len(o_pool)
    t_recs, t_pool = [], bytearray()
    for s in ostrs:
        off = t_pool_off + len(t_pool)
        t_recs.append((len(s), off))
        t_pool += s + b'\x00'

    out = bytearray(struct.pack(HDR, magic, ver, n, 28, 28 + 8 * n, hash_sz, hash_off_new))
    for r in o_recs:
        out += struct.pack(REC, *r)
    for r in t_recs:
        out += struct.pack(REC, *r)
    out += hash_bytes
    out += o_pool
    out += t_pool
    return bytes(out)


def find(d, msgid):
    _, _, n, o_idx, t_idx, _, _ = parse(d)
    for i in range(n):
        ol, oo = struct.unpack(REC, d[o_idx + i * 8: o_idx + i * 8 + 8])
        tl, to = struct.unpack(REC, d[t_idx + i * 8: t_idx + i * 8 + 8])
        if d[oo:oo + ol] == msgid.encode():
            return d[to:to + tl].decode('utf-8', 'replace')
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('file')
    ap.add_argument('--msgid', default='shutdown.reboot.message')
    ap.add_argument('--to', default='Please wait a moment while your device starts up.')
    ap.add_argument('--out', default='')
    args = ap.parse_args()

    d = open(args.file, 'rb').read()
    _, _, n, o_idx, t_idx, _, _ = parse(d)
    before = find(d, args.msgid)
    if before is None:
        print(f'msgid {args.msgid!r} not found in {args.file}', file=sys.stderr)
        sys.exit(2)
    new = rebuild(d, {args.msgid.encode(): args.to})
    out = args.out or args.file
    with open(out, 'wb') as f:
        f.write(new)
    print(f'{args.file}:')
    print(f'  {before!r}\n  -> {args.to!r}')
    print(f'  wrote {out} ({len(d)} -> {len(new)} bytes)')

    # verify: same msgids, only the target msgstr changed
    d2 = open(out, 'rb').read()
    d2_h = parse(d2)
    n2, o2, t2 = d2_h[2], d2_h[3], d2_h[4]
    ok = n2 == n
    diff = []
    for i in range(n):
        ol, oo = struct.unpack(REC, d[o_idx + i * 8: o_idx + i * 8 + 8])
        tl, to = struct.unpack(REC, d[t_idx + i * 8: t_idx + i * 8 + 8])
        ol2, oo2 = struct.unpack(REC, d2[o2 + i * 8: o2 + i * 8 + 8])
        tl2, to2 = struct.unpack(REC, d2[t2 + i * 8: t2 + i * 8 + 8])
        if d[oo:oo + ol] != d2[oo2:oo2 + ol2]:
            ok = False
            print('  FAIL: msgid changed at entry', i)
            break
        if d[to:to + tl] != d2[to2:to2 + tl2]:
            diff.append((d[oo:oo + ol], d[to:to + tl], d2[to2:to2 + tl2]))
    if not ok:
        sys.exit(1)
    print(f'  verified: {n} entries, msgids identical, changed msgstrs: {len(diff)}')
    for oid, a, b in diff:
        print(f'    {oid!r}: {a!r} -> {b!r}')
        if oid != args.msgid.encode():
            print('  WARNING: an unexpected entry changed', file=sys.stderr)


if __name__ == '__main__':
    main()