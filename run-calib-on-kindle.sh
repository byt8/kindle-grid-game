#!/bin/sh
# Push the calibration app to the Kindle, take the screen, run it, and put
# the device back.
#
#   ./run-calib-on-kindle.sh              # the Kindle at $KINDLE_IP
#   ./run-calib-on-kindle.sh 192.168.0.66 # ...or another one
#   ./run-calib-on-kindle.sh 192.168.0.66 --probe   # also log every flush
#
# The app owns the panel until you tap QUIT. tools/kindle-run.sh does the
# device-side work (kill leftovers, stop the mirror that holds the touch,
# restore the UI afterwards) exactly like the games.
set -eu
cd "$(dirname "$0")"

KJ=jailbreak-kindle/kindle-tools

if [ ! -f ./calib ]; then
    echo "run-calib-on-kindle: no Kindle binary. Run: make calib" >&2
    exit 1
fi
if [ ! -f ./grabtest ]; then
    echo "run-calib-on-kindle: no grabtest. Run: make grabtest" >&2
    exit 1
fi

if [ -f "$KJ/.kfb-secrets" ]; then
    KFB_ENV_FILE="$(pwd)/$KJ/.kfb-secrets"
    export KFB_ENV_FILE
    . "$KJ/env.sh"
else
    echo "run-calib-on-kindle: no $KJ/.kfb-secrets; using built-in defaults" >&2
fi

ip=${1:-${KINDLE_IP:-192.168.0.66}}
[ $# -gt 0 ] && shift
key=${KINDLE_KEY:-$HOME/.ssh/kindle_touch_ed25519}

if [ ! -f "$key" ]; then
    echo "run-calib-on-kindle: ssh key $key is missing" >&2
    exit 1
fi

ssh_run() {
    ssh -i "$key" -o BatchMode=yes -o IdentitiesOnly=yes \
        -o StrictHostKeyChecking=no -o UserKnownHostsFile="$HOME/.ssh/known_hosts" \
        -o LogLevel=ERROR -o ConnectTimeout=10 "root@$ip" "$@"
}

echo "run-calib-on-kindle: pushing the app to $ip"
# Write beside it and rename: a copy over a running binary is "Text file busy".
cat ./calib              | ssh_run 'cat > /mnt/us/calib.new && chmod +x /mnt/us/calib.new && mv /mnt/us/calib.new /mnt/us/calib'
cat ./grabtest           | ssh_run 'cat > /mnt/us/grabtest && chmod +x /mnt/us/grabtest'
cat tools/kindle-run.sh  | ssh_run 'cat > /mnt/us/calib-run.sh && chmod +x /mnt/us/calib-run.sh'

echo "run-calib-on-kindle: taking the $ip screen. Tap QUIT to give it back."
ssh_run "GAME=calib sh /mnt/us/calib-run.sh --detach $*"
echo "run-calib-on-kindle: calib is running on $ip; this window only watches."
seen=0
while :; do
    sleep 10
    snap=$(ssh_run 'echo "PID $(pidof calib 2>/dev/null)"; echo "LOG"; cat /mnt/us/calib.log 2>/dev/null' 2>/dev/null || true)
    if [ -z "$snap" ]; then
        echo "run-calib-on-kindle: cannot reach $ip; the app keeps running without us"
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
        echo "run-calib-on-kindle: calib has exited; the Kindle reader is back"
        exit 0
    fi
done