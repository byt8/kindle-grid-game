#!/bin/sh
# Push the Brogue port to the Kindle, take the screen, run it, put it back.
#
#   ./run-brogue-on-kindle.sh              # the Kindle at $KINDLE_IP
#   ./run-brogue-on-kindle.sh 192.168.0.64 # ...or another one
#
# The screen belongs to the game until you tap QUIT then SURE. tools/kindle-run.sh
# does the device-side work: kill a leftover game, stop the screen mirror that
# holds /dev/input/event3, prove the touch device is free, and restore the Kindle
# UI afterwards. It is pushed here as /mnt/us/brogue-run.sh with GAME=brogue-kindle-app.
set -eu
cd "$(dirname "$0")"

KJ=jailbreak-kindle/kindle-tools

if [ ! -f ./brogue-kindle-app ]; then
    echo "run-brogue-on-kindle: no Kindle binary. Run: make brogue-kindle" >&2
    exit 1
fi
if [ ! -f ./grabtest ]; then
    echo "run-brogue-on-kindle: no grabtest. Run: make grabtest" >&2
    exit 1
fi

if [ -f "$KJ/.kfb-secrets" ]; then
    KFB_ENV_FILE="$(pwd)/$KJ/.kfb-secrets"
    export KFB_ENV_FILE
    . "$KJ/env.sh"
else
    echo "run-brogue-on-kindle: no $KJ/.kfb-secrets; using built-in defaults" >&2
fi

ip=${1:-${KINDLE_IP:-192.168.0.66}}
[ $# -gt 0 ] && shift
key=${KINDLE_KEY:-$HOME/.ssh/kindle_touch_ed25519}

if [ ! -f "$key" ]; then
    echo "run-brogue-on-kindle: ssh key $key is missing" >&2
    exit 1
fi

ssh_run() {
    ssh -i "$key" -o BatchMode=yes -o IdentitiesOnly=yes \
        -o StrictHostKeyChecking=no -o UserKnownHostsFile="$HOME/.ssh/known_hosts" \
        -o LogLevel=ERROR -o ConnectTimeout=10 "root@$ip" "$@"
}

echo "run-brogue-on-kindle: pushing the game to $ip"
# Write beside it and rename: a copy over a running binary is "Text file busy".
cat ./brogue-kindle-app       | ssh_run 'cat > /mnt/us/brogue-kindle-app.new && chmod +x /mnt/us/brogue-kindle-app.new && mv /mnt/us/brogue-kindle-app.new /mnt/us/brogue-kindle-app'
cat ./grabtest                | ssh_run 'cat > /mnt/us/grabtest && chmod +x /mnt/us/grabtest'
cat tools/kindle-run.sh       | ssh_run 'cat > /mnt/us/brogue-run.sh && chmod +x /mnt/us/brogue-run.sh'

echo "run-brogue-on-kindle: taking the $ip screen. Tap QUIT, then SURE, to give it back."
ssh_run "GAME=brogue-kindle-app sh /mnt/us/brogue-run.sh --detach $*"
echo "run-brogue-on-kindle: the game is running on $ip; this window only watches."
seen=0
while :; do
    sleep 10
    snap=$(ssh_run 'echo "PID $(pidof brogue-kindle-app 2>/dev/null)"; echo "LOG"; cat /mnt/us/brogue-kindle-app.log 2>/dev/null' 2>/dev/null || true)
    if [ -z "$snap" ]; then
        echo "run-brogue-on-kindle: cannot reach $ip; the game keeps running without us"
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
        echo "run-brogue-on-kindle: the game has exited; the Kindle reader is back"
        exit 0
    fi
done