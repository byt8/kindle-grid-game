#!/bin/sh
# Push Rockman to the Kindle, take the screen, run it, put the device back.
#
#   ./run-rockman-on-kindle.sh              # the Kindle at $KINDLE_IP
#   ./run-rockman-on-kindle.sh 192.168.0.66 # ...or another one
#
# Any argument after the address goes to the game, so
#   ./run-rockman-on-kindle.sh 192.168.0.66 --seed 1
# plays the same caves again.
#
# The panel belongs to the game until you tap QUIT, then SURE. tools/kindle-run.sh
# does the device-side work: kill a leftover game, stop the screen mirror that
# holds /dev/input/event3, prove the touch device is free, and restore the Kindle
# UI afterwards. It is pushed here as /mnt/us/rockman-run.sh with GAME=rockman.
set -eu
cd "$(dirname "$0")"

KJ=jailbreak-kindle/kindle-tools

if [ ! -f ./rockman ]; then
    echo "run-rockman-on-kindle: no Kindle binary. Run: make rockman-kindle" >&2
    exit 1
fi
if [ ! -f ./grabtest ]; then
    echo "run-rockman-on-kindle: no grabtest. Run: make grabtest" >&2
    exit 1
fi

if [ -f "$KJ/.kfb-secrets" ]; then
    KFB_ENV_FILE="$(pwd)/$KJ/.kfb-secrets"
    export KFB_ENV_FILE
    . "$KJ/env.sh"
else
    echo "run-rockman-on-kindle: no $KJ/.kfb-secrets; using built-in defaults" >&2
fi

ip=${1:-${KINDLE_IP:-192.168.0.66}}
[ $# -gt 0 ] && shift
key=${KINDLE_KEY:-$HOME/.ssh/kindle_touch_ed25519}

if [ ! -f "$key" ]; then
    echo "run-rockman-on-kindle: ssh key $key is missing" >&2
    exit 1
fi

ssh_run() {
    ssh -i "$key" -o BatchMode=yes -o IdentitiesOnly=yes \
        -o StrictHostKeyChecking=no -o UserKnownHostsFile="$HOME/.ssh/known_hosts" \
        -o LogLevel=ERROR -o ConnectTimeout=10 "root@$ip" "$@"
}

echo "run-rockman-on-kindle: pushing the game to $ip"
# Write beside it and rename: a copy over a running binary is "Text file busy".
cat ./rockman            | ssh_run 'cat > /mnt/us/rockman.new && chmod +x /mnt/us/rockman.new && mv /mnt/us/rockman.new /mnt/us/rockman'
cat ./grabtest           | ssh_run 'cat > /mnt/us/grabtest && chmod +x /mnt/us/grabtest'
cat tools/kindle-run.sh  | ssh_run 'cat > /mnt/us/rockman-run.sh && chmod +x /mnt/us/rockman-run.sh'

echo "run-rockman-on-kindle: taking the $ip screen. Tap QUIT, then SURE, to give it back."
# --detach: from here the game belongs to the device. This script only follows
# the game's log, and closing it changes nothing.
ssh_run "GAME=rockman sh /mnt/us/rockman-run.sh --detach $*"
echo "run-rockman-on-kindle: the game is running on $ip; this window only watches."
seen=0
while :; do
    sleep 10
    snap=$(ssh_run 'echo "PID $(pidof rockman 2>/dev/null)"; echo "LOG"; cat /mnt/us/rockman.log 2>/dev/null' 2>/dev/null || true)
    if [ -z "$snap" ]; then
        echo "run-rockman-on-kindle: cannot reach $ip; the game keeps running without us"
        continue
    fi
    pid=$(printf '%s\n' "$snap" | sed -n 's/^PID //p' | head -1)
    log=$(printf '%s\n' "$snap" | sed -n '/^LOG$/,$p' | sed '1d')
    if [ -n "$log" ]; then
        n=$(printf '%s\n' "$log" | wc -l)
        if [ "$n" -gt "$seen" ]; then
            printf '%s\n' "$log" | sed -n "$((seen + 1)),\$p"
            seen=$n
        fi
    fi
    if [ -z "$pid" ]; then
        echo "run-rockman-on-kindle: the game has exited; the Kindle reader is back"
        exit 0
    fi
done
