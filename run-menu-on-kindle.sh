#!/bin/sh
# Push the launcher menu to the Kindle and show it, taking over the panel.
#
#   ./run-menu-on-kindle.sh              # the Kindle at $KINDLE_IP
#   ./run-menu-on-kindle.sh 192.168.0.66 # ...or another one
#
# First-time setup is install-menu-on-kindle.sh: it builds everything, installs
# the boot job (so the device boots into the menu) and starts the session.
# This script is for iterating -- it pushes a fresh menu, menu-boot.sh and
# grabtest (menu-boot.sh only ever gets pushed by the installer, which is how
# it goes stale), restarts the launcher session now and confirms the menu drew
# on the panel. Nothing is written outside /mnt/us, so after a reboot a device
# that never had the boot job installed shows the stock reader again.
#
# The session belongs to the device once started; this window only follows its
# log. Tap KINDLE on the device to hand the panel back to the reader UI, and
# the watcher exits.
set -eu
cd "$(dirname "$0")"

KJ=jailbreak-kindle/kindle-tools

if [ ! -f ./menu ]; then
    echo "run-menu-on-kindle: no Kindle binary. Run: make menu" >&2
    exit 1
fi
if [ ! -f ./grabtest ]; then
    echo "run-menu-on-kindle: no grabtest. Run: make grabtest" >&2
    exit 1
fi
if [ ! -f tools/menu-boot.sh ]; then
    echo "run-menu-on-kindle: tools/menu-boot.sh is missing" >&2
    exit 1
fi

if [ -f "$KJ/.kfb-secrets" ]; then
    KFB_ENV_FILE="$(pwd)/$KJ/.kfb-secrets"
    export KFB_ENV_FILE
    . "$KJ/env.sh"
else
    echo "run-menu-on-kindle: no $KJ/.kfb-secrets; using built-in defaults" >&2
fi

ip=${1:-${KINDLE_IP:-192.168.0.66}}
key=${KINDLE_KEY:-$HOME/.ssh/kindle_touch_ed25519}

if [ ! -f "$key" ]; then
    echo "run-menu-on-kindle: ssh key $key is missing" >&2
    exit 1
fi

ssh_run() {
    ssh -i "$key" -o BatchMode=yes -o IdentitiesOnly=yes \
        -o StrictHostKeyChecking=no -o UserKnownHostsFile="$HOME/.ssh/known_hosts" \
        -o LogLevel=ERROR -o ConnectTimeout=10 "root@$ip" "$@"
}

echo "run-menu-on-kindle: pushing a fresh menu to $ip"
# Write beside it and rename: a copy over a running binary is "Text file busy",
# and a running menu-boot.sh keeps its old inode (that is how it goes stale).
cat ./menu               | ssh_run 'cat > /mnt/us/menu.new && chmod +x /mnt/us/menu.new && mv /mnt/us/menu.new /mnt/us/menu'
cat ./grabtest           | ssh_run 'cat > /mnt/us/grabtest && chmod +x /mnt/us/grabtest'
cat tools/menu-boot.sh   | ssh_run 'cat > /mnt/us/menu-boot.sh.new && chmod +x /mnt/us/menu-boot.sh.new && mv /mnt/us/menu-boot.sh.new /mnt/us/menu-boot.sh'

echo "run-menu-on-kindle: restarting the launcher session on $ip"
ssh_run '
    rm -f /mnt/us/.menu-stop
    /sbin/initctl list 2>/dev/null | grep -q "^menu start" && /sbin/initctl stop menu
    killall -q menu-boot.sh menu 2>/dev/null || true
    # setsid + log redirect: a plain `&` is killed when this ssh session
    # closes, and the session must own the panel until a button is tapped.
    setsid sh /mnt/us/menu-boot.sh >>/mnt/us/menu-boot.log 2>&1 </dev/null &
'

echo "run-menu-on-kindle: confirming the menu is on the panel of $ip"
i=0
while [ $i -lt 20 ]; do
    if ssh_run 'pidof menu >/dev/null 2>&1'; then
        echo "run-menu-on-kindle: the launcher is up. Tap KINDLE on the device to"
        echo "                       hand the panel back to the reader UI."
        break
    fi
    # The session's guard exits (touch device still busy) write .menu-stop.
    if ssh_run 'test -e /mnt/us/.menu-stop' 2>/dev/null; then
        echo "run-menu-on-kindle: the menu session refused to start; last log lines:" >&2
        ssh_run 'tail -n 15 /mnt/us/menu-boot.log' >&2
        exit 1
    fi
    sleep 1
    i=$((i + 1))
done
if ! ssh_run 'pidof menu >/dev/null 2>&1' 2>/dev/null; then
    echo "run-menu-on-kindle: no menu after 20s; see /mnt/us/menu-boot.log on $ip" >&2
    exit 1
fi

echo "run-menu-on-kindle: the screen belongs to the menu; this window only watches."
seen=0
while :; do
    sleep 8
    snap=$(ssh_run 'echo "PID $(pidof menu-boot.sh menu 2>/dev/null)"; echo "LOG"; cat /mnt/us/menu-boot.log 2>/dev/null' 2>/dev/null || true)
    if [ -z "$snap" ]; then
        echo "run-menu-on-kindle: cannot reach $ip; the session keeps running without us"
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
        echo "run-menu-on-kindle: the launcher session has ended (KINDLE tapped or a"
        echo "                     guard exit); the Kindle reader is back"
        exit 0
    fi
done