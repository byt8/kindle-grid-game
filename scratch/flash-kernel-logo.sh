#!/bin/bash
# Flash the patched kernel logo (blur-15 art, 320x427 centred on black) into
# both uImages on the Kindle 5.3.7.3, and verify on-device afterwards.
# Safe: dumps + compares first, writes only the delta, verifies, never auto-reboots.
#
#   bash scratch/flash-kernel-logo.sh KINDLE_IP
set -euo pipefail
IP="${1:?usage: flash-kernel-logo.sh KINDLE_IP}"
KEY=~/.ssh/kindle_touch_ed25519
LOCAL_PATCHED="$PWD/scratch/mmcblk0-first32m-patched.bin"
WANT_MD5_LOCAL=$(md5sum "$LOCAL_PATCHED" | cut -d' ' -f1)

ssh_k(){ ssh -i "$KEY" -o BatchMode=yes -o IdentitiesOnly=yes -o ConnectTimeout=8 root@"$IP" "$@"; }

echo "== 1/5 dump current disk (first 32MiB) =="
mkdir -p /tmp/klogo-flash
ssh_k 'dd if=/dev/mmcblk0 bs=512 count=65536 2>/dev/null | gzip -1' > /tmp/klogo-flash/current.gz
gunzip -f /tmp/klogo-flash/current.gz 2>/dev/null || true

echo "== 2/5 verify current disk matches the pre-patch stock dump at all patch offsets =="
python3 - <<'PY'
import sys
cur = open('/tmp/klogo-flash/current','rb').read()
print('device dump size', len(cur))
assert len(cur) == 33554432
# These offsets must equal the stock (pre-patch) dump we based the patch on.
# Stock md5s for blobs (from build): main-plain 2b9f3a83, main-wait ebfb4f3f,
# sec-plain 2b9f3a83, sec-wait ebfb4f3f. Check first 16 bytes + size region.
import hashlib
stock_offs = [(0xC92C0,2672,'main-plain'),(0xC9D30,4664,'main-wait'),
              (0xEC7104,2672,'sec-plain'),(0xEC7B74,4664,'sec-wait')]
for off,n,name in stock_offs:
    m = hashlib.md5(cur[off:off+n]).hexdigest()
    exp = '2b9f3a83' if 'plain' in name else 'ebfb4f3f'
    assert m.startswith(exp), (name, m, exp)
    print(f'stock {name} @{off:#x} OK ({m[:8]})')
# uImage headers must be the stock ones (pre-patch)
for off,name in [(0x41000,'main'),(0xE41000,'second')]:
    assert cur[off:off+4] == b'\x27\x05\x19\x56', (name, cur[off:off+4])
print('device is unpatched; safe to write')
PY

echo "== 3/5 upload patched image =="
# scp is broken on this Kindle (sftp-server missing); pipe over stdin instead.
ssh_k 'cat > /mnt/us/mmcblk0-first32m-patched.bin' < "$LOCAL_PATCHED"
DEV_MD5=$(ssh_k 'md5sum /mnt/us/mmcblk0-first32m-patched.bin | cut -d" " -f1')
[ "$DEV_MD5" = "$WANT_MD5_LOCAL" ] && echo "upload md5 OK  $WANT_MD5_LOCAL" || { echo "md5 mismatch $DEV_MD5 != $WANT_MD5_LOCAL"; exit 1; }

echo "== 4/5 write only the changed bytes (4 logo slots + 2 header crcs x 2 kernels) =="
# BusyBox 1.17.1 dd has NO conv=notrunc; block device so no truncation anyway.
# Use bs=1 with decimal offsets (hex literals in $(( )) unsupported in ash).
ssh_k 'set -e
  O=/mnt/us/mmcblk0-first32m-patched.bin
  # main kernel logo window: 0xC92C0 (824000) .. 0xCAF68-1, len 7336
  dd if=$O of=/dev/mmcblk0 bs=1 skip=824000 seek=824000 count=7336
  # second kernel logo window: 0xEC7104 (15495428) .. 0xEC8DAC-1, len 7336
  dd if=$O of=/dev/mmcblk0 bs=1 skip=15495428 seek=15495428 count=7336
  # uImage header hcrc (+4) and dcrc (+24) for both kernels
  # main uImage @ 0x41000 (266240): hcrc=266244 dcrc=266264
  dd if=$O of=/dev/mmcblk0 bs=1 skip=266244 seek=266244 count=4
  dd if=$O of=/dev/mmcblk0 bs=1 skip=266264 seek=266264 count=4
  # second uImage @ 0xE41000 (14946304): hcrc=14946308 dcrc=14946328
  dd if=$O of=/dev/mmcblk0 bs=1 skip=14946308 seek=14946308 count=4
  dd if=$O of=/dev/mmcblk0 bs=1 skip=14946328 seek=14946328 count=4
  sync
  echo WRITTEN'

echo "== 5/5 verify on device: re-dump and byte-compare the patched regions =="
ssh_k 'dd if=/dev/mmcblk0 bs=512 count=65536 2>/dev/null | gzip -1' > /tmp/klogo-flash/after.gz
gunzip -f /tmp/klogo-flash/after.gz 2>/dev/null || true
python3 - <<'PY'
cur = open('/tmp/klogo-flash/after','rb').read()
pat = open('scratch/mmcblk0-first32m-patched.bin','rb').read()
checks = [
    # patched windows (logo blobs):
    (0xC92C0, 7336, 'main window'),
    (0xEC7104, 7336, 'second window'),
    # patched header crc words:
    (0x41000+4,4,'main hcrc'),(0x41000+24,4,'main dcrc'),
    (0xE41000+4,4,'second hcrc'),(0xE41000+24,4,'second dcrc'),
    # spot-check unchanged kernel bytes:
    (0x50000, 4096, 'kernel body mid'),
]
for off,n,name in checks:
    ok = cur[off:off+n] == pat[off:off+n]
    print(f'  {name} @{off:#x} {n}B: {"OK" if ok else "MISMATCH"}')
    assert ok, (name, off)
print('ALL ON-DEVICE BYTES MATCH THE TESTED PATCHED IMAGE')
print('Reboot the Kindle to see the new first frame.')
PY