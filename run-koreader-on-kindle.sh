#!/bin/sh
# Push the runner to the Kindle and run KOReader on it, then give the device back.
#
#   ./run-koreader-on-kindle.sh              # the Kindle at $KINDLE_IP
#   ./run-koreader-on-kindle.sh 192.168.0.66 # ...or another one
#
# KOReader must already be installed on the device (install-koreader-on-kindle.sh).
# Any argument after the address goes to koreader.sh (--asap and friends).
#
# The panel belongs to the reader until you exit KOReader. tools/kindle-run.sh
# does the device-side work: kill a leftover app, stop the screen mirror that
# holds /dev/input/event3, prove the touch device is free, and restore the
# Kindle UI (or the launcher session, on menu devices) afterwards. It is pushed
# here as /mnt/us/koreader-run.sh with GAME=koreader, which makes it run
# koreader.sh instead of a game binary -- deliberately without --framework_stop,
# exactly like the menu session runs it.
set -eu
cd "$(dirname "$0")"

KJ=jailbreak-kindle/kindle-tools

if [ ! -f ./grabtest ]; then
    echo "run-koreader-on-kindle: no grabtest. Run: make grabtest" >&2
    exit 1
fi
if [ ! -f tools/kindle-run.sh ]; then
    echo "run-koreader-on-kindle: tools/kindle-run.sh is missing" >&2
    exit 1
fi

if [ -f "$KJ/.kfb-secrets" ]; then
    KFB_ENV_FILE="$(pwd)/$KJ/.kfb-secrets"
    export KFB_ENV_FILE
    . "$KJ/env.sh"
else
    echo "run-koreader-on-kindle: no $KJ/.kfb-secrets; using built-in defaults" >&2
fi

ip=${1:-${KINDLE_IP:-192.168.0.66}}
[ $# -gt 0 ] && shift
key=${KINDLE_KEY:-$HOME/.ssh/kindle_touch_ed25519}

if [ ! -f "$key" ]; then
    echo "run-koreader-on-kindle: ssh key $key is missing" >&2
    exit 1
fi

ssh_run() {
    ssh -i "$key" -o BatchMode=yes -o IdentitiesOnly=yes \
        -o StrictHostKeyChecking=no -o UserKnownHostsFile="$HOME/.ssh/known_hosts" \
        -o LogLevel=ERROR -o ConnectTimeout=10 "root@$ip" "$@"
}

echo "run-koreader-on-kindle: checking KOReader is installed on $ip"
if ! ssh_run 'test -x /mnt/us/koreader/koreader.sh'; then
    echo "run-koreader-on-kindle: /mnt/us/koreader/koreader.sh is missing on $ip" >&2
    echo "                        run ./install-koreader-on-kindle.sh $ip first" >&2
    exit 1
fi

echo "run-koreader-on-kindle: pushing the runner to $ip"
cat ./grabtest           | ssh_run 'cat > /mnt/us/grabtest && chmod +x /mnt/us/grabtest'
cat tools/kindle-run.sh  | ssh_run 'cat > /mnt/us/koreader-run.sh && chmod +x /mnt/us/koreader-run.sh'

echo "run-koreader-on-kindle: taking the $ip screen. Exit KOReader to give it back."
# --detach: from here KOReader belongs to the device. This script only follows
# its log, and closing it changes nothing.
ssh_run "GAME=koreader sh /mnt/us/koreader-run.sh --detach $*"
echo "run-koreader-on-kindle: KOReader is running on $ip; this window only watches."
seen=0
while :; do
    sleep 10
    snap=$(ssh_run 'echo "PID $(pidof koreader.sh reader.lua luajit 2>/dev/null)"; echo "LOG"; cat /mnt/us/koreader.log 2>/dev/null' 2>/dev/null || true)
    if [ -z "$snap" ]; then
        echo "run-koreader-on-kindle: cannot reach $ip; KOReader keeps running without us"
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
        echo "run-koreader-on-kindle: KOReader has exited; the Kindle reader is back"
        exit 0
    fi
done