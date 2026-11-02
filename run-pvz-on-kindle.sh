#!/bin/sh
# Push PvZ to the Kindle, take the screen, run it, put the device back.
#
#   ./run-pvz-on-kindle.sh              # the Kindle at $KINDLE_IP
#   ./run-pvz-on-kindle.sh 192.168.0.66 # ...or another one
#
# Any argument after the address goes to the game, so
#   ./run-pvz-on-kindle.sh 192.168.0.66 --stats
# plays it while printing what every screen update cost.
#
# The panel belongs to the game until you tap QUIT, then SURE. tools/kindle-run.sh
# does the device-side work: kill a leftover game, stop the screen mirror that
# holds /dev/input/event3, prove the touch device is free, and restore the Kindle
# UI afterwards. It is pushed here as /mnt/us/pvz-run.sh with GAME=pvz.
set -eu
cd "$(dirname "$0")"

KJ=jailbreak-kindle/kindle-tools

if [ ! -f ./pvz ]; then
    echo "run-pvz-on-kindle: no Kindle binary. Run: make pvz-kindle" >&2
    exit 1
fi
if [ ! -f ./grabtest ]; then
    echo "run-pvz-on-kindle: no grabtest. Run: make grabtest" >&2
    exit 1
fi

if [ -f "$KJ/.kfb-secrets" ]; then
    KFB_ENV_FILE="$(pwd)/$KJ/.kfb-secrets"
    export KFB_ENV_FILE
    . "$KJ/env.sh"
else
    echo "run-pvz-on-kindle: no $KJ/.kfb-secrets; using built-in defaults" >&2
fi

ip=${1:-${KINDLE_IP:-192.168.0.66}}
[ $# -gt 0 ] && shift
key=${KINDLE_KEY:-$HOME/.ssh/kindle_touch_ed25519}

if [ ! -f "$key" ]; then
    echo "run-pvz-on-kindle: ssh key $key is missing" >&2
    exit 1
fi

ssh_run() {
    ssh -i "$key" -o BatchMode=yes -o IdentitiesOnly=yes \
        -o StrictHostKeyChecking=no -o UserKnownHostsFile="$HOME/.ssh/known_hosts" \
        -o LogLevel=ERROR -o ConnectTimeout=10 "root@$ip" "$@"
}

echo "run-pvz-on-kindle: pushing the game to $ip"
# Write beside it and rename: a copy over a running binary is "Text file busy".
cat ./pvz             | ssh_run 'cat > /mnt/us/pvz.new && chmod +x /mnt/us/pvz.new && mv /mnt/us/pvz.new /mnt/us/pvz'
cat ./grabtest           | ssh_run 'cat > /mnt/us/grabtest && chmod +x /mnt/us/grabtest'
cat tools/kindle-run.sh  | ssh_run 'cat > /mnt/us/pvz-run.sh && chmod +x /mnt/us/pvz-run.sh'

echo "run-pvz-on-kindle: taking the $ip screen. Tap QUIT, then SURE, to give it back."
# --detach: from here the game belongs to the device. This script only follows
# the game's log, and closing it changes nothing.
ssh_run "GAME=pvz sh /mnt/us/pvz-run.sh --detach $*"
echo "run-pvz-on-kindle: the game is running on $ip; this window only watches."
seen=0
settle=0
while :; do
    sleep 10
    snap=$(ssh_run 'echo "PID $(pidof pvz 2>/dev/null)"; echo "LOG"; cat /mnt/us/pvz.log 2>/dev/null' 2>/dev/null || true)
    if [ -z "$snap" ]; then
        echo "run-pvz-on-kindle: cannot reach $ip; the game keeps running without us"
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
        echo "run-pvz-on-kindle: no game pid; waiting to be sure it is gone"
        if [ "$settle" -ge 10 ]; then
            if ssh_run 'grep -q ERROR /mnt/us/pvz-run.log 2>/dev/null'; then
                echo "run-pvz-on-kindle: the runner reported an error:" >&2
                ssh_run 'cat /mnt/us/pvz-run.log' >&2
            fi
            echo "run-pvz-on-kindle: the game has exited; the Kindle reader is back"
            exit 0
        fi
        settle=$((settle + 1))
    else
        settle=0
    fi
done
