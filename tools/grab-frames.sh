#!/bin/sh
# grab-frames.sh -- capture the Kindle framebuffer every N ms for M seconds.
# Run ON THE DEVICE in the foreground of an ssh session:  ssh 'sh /mnt/us/grab-frames.sh 200 10'
# Writes raw 608x800 8bpp frames into /mnt/us/frames/ (one per 200ms).
set -u
ms=${1:-200}
secs=${2:-10}
out=/mnt/us/frames
[ -d "$out" ] || mkdir -p "$out"
rm -f "$out"/*.raw
fb=/dev/fb0
stride=608
rows=800
hbytes=$((stride * rows))
n=0
end=$((secs * 1000))
t=0
while [ $t -lt $end ]; do
    dd if="$fb" of="$out/f_$(printf %04d $n).raw" bs=$hbytes count=1 2>/dev/null || true
    n=$((n + 1))
    sleep 0.2
    t=$((t + ms))
done
echo "grabbed $n frames"
