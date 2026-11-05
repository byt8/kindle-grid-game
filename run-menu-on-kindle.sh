#!/bin/sh
# Push the launcher menu to the Kindle and show it, taking over the panel.
#
#   ./run-menu-on-kindle.sh              # the Kindle at $KINDLE_IP
#   ./run-menu-on-kindle.sh 192.168.0.66 # ...or another one
#
# First-time setup is install-menu-on-kindle.sh: it builds everything, installs
# the boot job (so the device boots into the menu) and starts the session.
# This script is for iterating -- it pushes the full launcher set (menu,
# menu-boot.sh, grabtest, every game binary and its runner, the wifi helper,
# chess.conf), frees the touch device of any leftover from a dead session,
# restarts the launcher session now and confirms the menu drew on the panel.
# Pushing everything matters: a unit installed by an older installer carries
# stale runners and missing game binaries, and its buttons die with "sh: can't
# open '/mnt/us/rockman-run.sh'" (seen 2026-11-02 on .63). Nothing is written
# outside /mnt/us, so after a reboot a device that never had the boot job
# installed shows the stock reader again.
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

echo "run-menu-on-kindle: freeing the touch device on $ip"
# A live game holds the touch grab and its binary is then "Text file busy" to
# overwrite, so this must happen BEFORE the build push. A leftover from a dead
# session (a game that outlived its watcher) also holds /dev/input/event3 and
# would make the menu session fail grabtest forever with "Resource busy",
# giving up with the reader left up. Clear every known game, plus the menu
# itself (the new session will start its own), and WAIT for each to be gone:
# a kill is only the request -- the game's EXIT trap (saving, restoring the
# Kindle UI) runs after SIGTERM and holds the grab until it finishes, so the
# menu session must not be started into that window. (seen 2026-11-02 on .63:
# xcom took ~4s to die after TERM; the menu's grabtest ran inside that window
# and failed EBUSY.) TERM first, then KILL for anything that ignores it.
ssh_run '
    for g in xcom rockman tetris chess sudoku mines calib pvz brogue-kindle-app menu; do
        p=$(pidof "$g" 2>/dev/null || true)
        [ -n "$p" ] || continue
        echo "  killing leftover $g pid $p"
        killall -q "$g" 2>/dev/null || true
        i=0
        while [ -n "$(pidof "$g" 2>/dev/null || true)" ] && [ $i -lt 15 ]; do
            i=$((i + 1)); sleep 1
        done
        if [ -n "$(pidof "$g" 2>/dev/null || true)" ]; then
            echo "  $g ignored TERM; sending KILL"
            kill -9 $(pidof "$g") 2>/dev/null || true
            sleep 1
        fi
    done
    sleep 1
    rm -f /mnt/us/.menu-stop
'

# With the games dead the grab is released, but Xorg still has event3 open
# (restore-kindle.sh starts X even without kfbterm). That is fine -- open is
# not grabbed -- yet it is worth a zero-cost sanity check before the session
# starts, so a stale holder shows up here as a named process instead of a
# generic EBUSY from grabtest inside the session.
ssh_run '
    for p in /proc/[0-9]*; do
        if ls -l "$p/fd" 2>/dev/null | grep -q event3; then
            echo "  event3 is open by: $(cat "$p/cmdline" 2>/dev/null | tr "\0" " ")"
        fi
    done
' 2>/dev/null || true

echo "run-menu-on-kindle: pushing the full launcher set to $ip"
# Write the ones that can be running beside it and rename: a copy over a
# running binary is "Text file busy", and a running menu-boot.sh keeps its
# old inode (that is how it goes stale). The game binaries and runners are
# never running at push time, so a plain copy is fine for them.
cat ./menu               | ssh_run 'cat > /mnt/us/menu.new && chmod +x /mnt/us/menu.new && mv /mnt/us/menu.new /mnt/us/menu'
cat ./grabtest           | ssh_run 'cat > /mnt/us/grabtest && chmod +x /mnt/us/grabtest'
cat tools/menu-boot.sh   | ssh_run 'cat > /mnt/us/menu-boot.sh.new && chmod +x /mnt/us/menu-boot.sh.new && mv /mnt/us/menu-boot.sh.new /mnt/us/menu-boot.sh'
for f in chess tetris sudoku mines rockman xcom; do
    if [ -f "./$f" ]; then
        cat "./$f" | ssh_run "cat > /mnt/us/$f && chmod +x /mnt/us/$f"
    else
        echo "run-menu-on-kindle: no ./$f; /mnt/us/$f keeps its old copy" >&2
    fi
done
for r in chess xcom tetris sudoku mines rockman; do
    cat tools/kindle-run.sh | ssh_run "cat > /mnt/us/$r-run.sh && chmod +x /mnt/us/$r-run.sh"
done
cat tools/chess-wifi.sh  | ssh_run 'cat > /mnt/us/chess-wifi.sh && chmod +x /mnt/us/chess-wifi.sh'
if [ -f chess.conf ]; then
    cat chess.conf        | ssh_run 'cat > /mnt/us/chess.conf'
fi
if ! ssh_run 'test -x /mnt/us/koreader/koreader.sh'; then
    echo "run-menu-on-kindle: no KOReader on $ip; the KOREADER button will fail" >&2
    echo "                     until ./install-koreader-on-kindle.sh $ip is run" >&2
fi

echo "run-menu-on-kindle: restarting the launcher session on $ip"
# The session itself re-checks the grab right before drawing (grabtest in
# menu-boot.sh), so the wait-and-verify above plus that check gives this two
# chances to catch a slow release.
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
while [ $i -lt 45 ]; do
    # menu-boot.sh retries the grab for up to ~20s (a freshly killed game's
    # restore holds the device), so a menu that has not drawn yet is not a
    # failure as long as the session is still alive.
    if ssh_run 'pidof menu-boot.sh >/dev/null 2>&1' 2>/dev/null; then
        : # session alive; keep waiting for the menu process itself
    fi
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
    echo "run-menu-on-kindle: session still starting or waiting on the grab; log:" >&2
    ssh_run 'tail -n 15 /mnt/us/menu-boot.log' >&2
    echo "run-menu-on-kindle: no menu after 45s; see /mnt/us/menu-boot.log on $ip" >&2
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