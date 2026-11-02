#!/bin/sh
# Push Sudoku to the Kindle, take the screen, run it, put the device back.
#
#   ./run-sudoku-on-kindle.sh              # the Kindle at $KINDLE_IP
#   ./run-sudoku-on-kindle.sh 192.168.0.66 # ...or another one
#
# Any argument after the address goes to the game, so
#   ./run-sudoku-on-kindle.sh 192.168.0.66 --seed 5
# plays the same puzzle every time.
#
# The panel belongs to the game until you tap QUIT, then SURE. tools/kindle-run.sh
# does the device-side work: kill a leftover game, stop the screen mirror that
# holds /dev/input/event3, prove the touch device is free, and restore the Kindle
# UI afterwards. It is pushed here as /mnt/us/sudoku-run.sh with GAME=sudoku.
set -eu
cd "$(dirname "$0")"

KJ=jailbreak-kindle/kindle-tools

if [ ! -f ./sudoku ]; then
    echo "run-sudoku-on-kindle: no Kindle binary. Run: make sudoku-kindle" >&2
    exit 1
fi
if [ ! -f ./grabtest ]; then
    echo "run-sudoku-on-kindle: no grabtest. Run: make grabtest" >&2
    exit 1
fi

if [ -f "$KJ/.kfb-secrets" ]; then
    KFB_ENV_FILE="$(pwd)/$KJ/.kfb-secrets"
    export KFB_ENV_FILE
    . "$KJ/env.sh"
else
    echo "run-sudoku-on-kindle: no $KJ/.kfb-secrets; using built-in defaults" >&2
fi

ip=${1:-${KINDLE_IP:-192.168.0.66}}
[ $# -gt 0 ] && shift
key=${KINDLE_KEY:-$HOME/.ssh/kindle_touch_ed25519}

if [ ! -f "$key" ]; then
    echo "run-sudoku-on-kindle: ssh key $key is missing" >&2
    exit 1
fi

ssh_run() {
    ssh -i "$key" -o BatchMode=yes -o IdentitiesOnly=yes \
        -o StrictHostKeyChecking=no -o UserKnownHostsFile="$HOME/.ssh/known_hosts" \
        -o LogLevel=ERROR -o ConnectTimeout=10 "root@$ip" "$@"
}

echo "run-sudoku-on-kindle: pushing the game to $ip"
# Write beside it and rename: a copy over a running binary is "Text file busy".
cat ./sudoku            | ssh_run 'cat > /mnt/us/sudoku.new && chmod +x /mnt/us/sudoku.new && mv /mnt/us/sudoku.new /mnt/us/sudoku'
cat ./grabtest          | ssh_run 'cat > /mnt/us/grabtest && chmod +x /mnt/us/grabtest'
cat tools/kindle-run.sh | ssh_run 'cat > /mnt/us/sudoku-run.sh && chmod +x /mnt/us/sudoku-run.sh'

echo "run-sudoku-on-kindle: taking the $ip screen. Tap QUIT, then SURE, to give it back."
# --detach: from here the game belongs to the device. This script only follows
# the game's log, and closing it changes nothing.
ssh_run "GAME=sudoku sh /mnt/us/sudoku-run.sh --detach $*"
echo "run-sudoku-on-kindle: the game is running on $ip; this window only watches."
seen=0
settle=0
while :; do
    sleep 10
    snap=$(ssh_run 'echo "PID $(pidof sudoku 2>/dev/null)"; echo "LOG"; cat /mnt/us/sudoku.log 2>/dev/null' 2>/dev/null || true)
    if [ -z "$snap" ]; then
        echo "run-sudoku-on-kindle: cannot reach $ip; the game keeps running without us"
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
        echo "run-sudoku-on-kindle: no game pid; waiting to be sure it is gone"
        if [ "$settle" -ge 10 ]; then
            if ssh_run 'grep -q ERROR /mnt/us/sudoku-run.log 2>/dev/null'; then
                echo "run-sudoku-on-kindle: the runner reported an error:" >&2
                ssh_run 'cat /mnt/us/sudoku-run.log' >&2
            fi
            echo "run-sudoku-on-kindle: the game has exited; the Kindle reader is back"
            exit 0
        fi
        settle=$((settle + 1))
    else
        settle=0
    fi
done
