#!/bin/sh
# Copy the Kindle binary over SSH and run it. The Kindle screen stays on
# the game until you tap QUIT, then SURE. That hands the books screen back.
set -eu
cd "$(dirname "$0")"
if [ ! -f ./xcom ]; then
    echo "xcom: no Kindle binary. Run: make kindle" >&2
    exit 1
fi
k=./jailbreak-kindle/kindle-tools/k.sh
if [ ! -x "$k" ]; then
    echo "xcom: cannot find $k" >&2
    exit 1
fi
echo "Taking the Kindle screen. Tap QUIT, then SURE, to give it back."
cat ./xcom | "$k" 'cat > /mnt/us/xcom && chmod +x /mnt/us/xcom && exec /mnt/us/xcom'
