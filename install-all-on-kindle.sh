#!/bin/sh
# install-all-on-kindle.sh -- build and install EVERYTHING on a fresh Kindle.
#
#   ./install-all-on-kindle.sh [IP]
#   ./install-all-on-kindle.sh 192.168.0.63 --no-build   # push what's compiled
#   ./install-all-on-kindle.sh 192.168.0.63 --no-koreader
#
# "Everything" = this repo's whole setup on top of a jailbroken Kindle that
# already has ssh up (see README Quick Start). One run makes the device look
# like the working one:
#
#   * every game binary: xcom chess tetris sudoku mines rockman pvz calib brogue
#   * their runners (tools/kindle-run.sh as /mnt/us/*-run.sh), grabtest
#   * the launcher session + boot job  -> /etc/upstart/menu.conf
#   * the screen-mirror boot job       -> /etc/upstart/kfbterm.conf
#   * the fbdaemon set                 restore-kindle.sh kfb start-kfb.sh
#                                      kfb-watchdog.sh (in /mnt/us/fbdaemon)
#   * game support files               chess-wifi.sh chess-boot.conf chess.conf
#                                      chess-ca.pem, xcom sound clips
#   * KOReader (from the staged zip)   via install-koreader-on-kindle.sh
#
# What it does NOT do: the jailbreak itself and the ssh injection. Those are
# USB/screen steps from jailbreak-kindle (kindle-tools/sshsetup.sh), and the
# device has to be on your network with ssh for this script to reach it.
#
# Exit codes, all deliberate:
#   0  installed and the menu session is running on the panel
#   1  something failed (the message says what)
set -eu
cd "$(dirname "$0")"

KJ=jailbreak-kindle/kindle-tools
if [ -f "$KJ/.kfb-secrets" ]; then
    KFB_ENV_FILE="$(pwd)/$KJ/.kfb-secrets"
    export KFB_ENV_FILE
    . "$KJ/env.sh"
fi
ip=${1:-${KINDLE_IP:-192.168.0.63}}
shift 2>/dev/null || true
no_build=0
no_koreader=0
for a in "$@"; do
    case "$a" in
        --no-build)   no_build=1 ;;
        --no-koreader) no_koreader=1 ;;
        *) echo "install-all: unknown argument: $a" >&2; exit 1 ;;
    esac
done
key=${KINDLE_KEY:-$HOME/.ssh/kindle_touch_ed25519}

[ -f "$key" ] || { echo "install-all: ssh key $key is missing" >&2; exit 1; }

ssh_run() {
    ssh -i "$key" -o BatchMode=yes -o IdentitiesOnly=yes \
        -o StrictHostKeyChecking=no -o UserKnownHostsFile="$HOME/.ssh/known_hosts" \
        -o LogLevel=ERROR -o ConnectTimeout=10 "root@$ip" "$@"
}

# ------------------------------------------------------------------ build --
if [ "$no_build" = 0 ]; then
    echo "install-all: building the device binaries"
    # Same group as install-menu-on-kindle.sh, plus the standalone games and
    # pvz/calib/brogue which install-menu does not build.
    make menu chess tetris sudoku mines rockman pvz calib grabtest
    make kindle          # xcom: oldest and largest build
    make brogue-kindle
fi
# The push list. Every binary must exist: a missing one is a silent no-op
# otherwise ("cat: no such file"). Failing loudly here is by design.
need() {
    [ -f "$1" ] || { echo "install-all: missing $1 -- build it first (make)" >&2; exit 1; }
}
need menu; need grabtest; need chess; need tetris; need sudoku; need mines; need rockman
need xcom; need pvz; need calib; need brogue-kindle-app
need tools/kindle-run.sh; need tools/menu-boot.sh; need tools/menu-boot.conf
need tools/chess-wifi.sh;  need tools/chess-boot.conf
need "$KJ/fbdaemon/restore-kindle.sh"; need "$KJ/fbdaemon/kfb-watchdog.sh"
need "$KJ/fbdaemon/start-kfb.sh";      need "$KJ/fbdaemon/out/kfb"
need "$KJ/fbdaemon/kfbterm.conf"

echo "install-all: pushing games to $ip"
# Write beside then rename: a copy over a running binary is "Text file busy".
for g in menu chess tetris sudoku mines rockman xcom pvz calib brogue-kindle-app grabtest; do
    cat "$g" | ssh_run "cat > /mnt/us/$g.new && chmod +x /mnt/us/$g.new && mv /mnt/us/$g.new /mnt/us/$g"
done
# Grabtest was already pushed above in the loop; this is the binary itself.

# The runners: one kindle-run.sh per game name.
for g in chess xcom tetris sudoku mines rockman pvz calib brogue koreader; do
    cat tools/kindle-run.sh | ssh_run "cat > /mnt/us/$g-run.sh && chmod +x /mnt/us/$g-run.sh"
done
cat tools/menu-boot.sh   | ssh_run 'cat > /mnt/us/menu-boot.sh && chmod +x /mnt/us/menu-boot.sh'
cat tools/menu-boot.conf | ssh_run 'cat > /mnt/us/menu-boot.conf'
cat tools/chess-wifi.sh  | ssh_run 'cat > /mnt/us/chess-wifi.sh && chmod +x /mnt/us/chess-wifi.sh'
cat tools/chess-boot.conf| ssh_run 'cat > /mnt/us/chess-boot.conf'

# chess needs its API key (optional: without chess.conf it uses built-in
# defaults) and the CA bundle the Kindle's own store predates.
if [ -f chess.conf ]; then
    cat chess.conf | ssh_run 'cat > /mnt/us/chess.conf && chmod 600 /mnt/us/chess.conf'
else
    echo "install-all: no chess.conf; chess will use its built-in defaults" >&2
fi
ca=${CHESS_CA:-/etc/ssl/certs/ca-certificates.crt}
if [ -f "$ca" ]; then
    echo "install-all: pushing CA bundle $ca"
    cat "$ca" | ssh_run 'cat > /mnt/us/chess-ca.pem && chmod 644 /mnt/us/chess-ca.pem'
else
    echo "install-all: no CA bundle at $ca; the device will have to trust" >&2
    echo "              DeepSeek on its own old certificates." >&2
fi

# xcom talks; the speaker reads one file per word, so the whole folder goes
# over in one stream, replaced (never merged).
if [ -d sound/words ]; then
    echo "install-all: pushing $(ls sound/words | wc -l) word clips and the effects"
    tar -cf - -C sound words fx | ssh_run '
        rm -rf /mnt/us/xcom-sound
        mkdir -p /mnt/us/xcom-sound
        tar -xf - -C /mnt/us/xcom-sound'
else
    echo "install-all: no sound/words; xcom will be silent (run: make sound)" >&2
fi

# ------------------------------------------------------------------ fbdaemon --
# The games' kindle-run.sh and the menu's KINDLE button both hand the panel
# back through restore-kindle.sh, and the kfbterm boot job re-asserts ssh +
# the firewall hole every 30 s. A fresh device needs all of it.
echo "install-all: pushing the fbdaemon set"
ssh_run 'mkdir -p /mnt/us/fbdaemon'
cat "$KJ/fbdaemon/restore-kindle.sh" | ssh_run 'cat > /mnt/us/fbdaemon/restore-kindle.sh && chmod +x /mnt/us/fbdaemon/restore-kindle.sh'
cat "$KJ/fbdaemon/kfb-watchdog.sh"   | ssh_run 'cat > /mnt/us/fbdaemon/kfb-watchdog.sh && chmod +x /mnt/us/fbdaemon/kfb-watchdog.sh'
cat "$KJ/fbdaemon/start-kfb.sh"      | ssh_run 'cat > /mnt/us/fbdaemon/start-kfb.sh && chmod +x /mnt/us/fbdaemon/start-kfb.sh'
cat "$KJ/fbdaemon/out/kfb"           | ssh_run 'cat > /mnt/us/fbdaemon/kfb.new && chmod +x /mnt/us/fbdaemon/kfb.new && mv /mnt/us/fbdaemon/kfb.new /mnt/us/fbdaemon/kfb'
# kfb refuses WebSocket upgrades without a token; the mirror is optional, so a
# missing token just leaves the mirror off (the daemon is started on demand,
# and kfb-remote.sh on the PC expects it).
if [ -f "$KJ/.kfb-secrets" ]; then
    # kfb refuses WebSocket upgrades without a token; the mirror is optional,
    # so a missing token just leaves the mirror off.
    tok=$(grep -E '^KFB_TOKEN=' "$KJ/.kfb-secrets" | head -1 | cut -d= -f2-)
    [ -n "$tok" ] && printf '%s' "$tok" | ssh_run 'cat > /mnt/us/fbdaemon/token && chmod 600 /mnt/us/fbdaemon/token' || \
        echo "install-all: WARNING: no KFB_TOKEN in $KJ/.kfb-secrets; the mirror will not start" >&2
fi

# ------------------------------------------------------------- boot jobs ----
echo "install-all: installing the boot jobs"
# kfbterm.conf lives in the shared tree; push it into /mnt/us first so the
# rootfs copy has a source (same move as deploy-boot.sh).
cat "$KJ/fbdaemon/kfbterm.conf" | ssh_run 'cat > /mnt/us/kfbterm.conf'
ssh_run '
    /usr/sbin/mntroot rw
    cp /mnt/us/menu-boot.conf /etc/upstart/menu.conf
    rm -f /etc/upstart/chess.conf          # the old direct-to-chess job
    cp /mnt/us/kfbterm.conf /etc/upstart/kfbterm.conf
    sync
    /usr/sbin/mntroot ro
'

# ----------------------------------------------------------------- koreader --
if [ "$no_koreader" = 0 ] && [ -f ./install-koreader-on-kindle.sh ]; then
    echo "install-all: installing KOReader"
    ./install-koreader-on-kindle.sh "$ip"
else
    echo "install-all: skipping KOReader"
fi

# -------------------------------------------------------------- start it ----
echo "install-all: starting the launcher session on $ip"
ssh_run '
    /sbin/initctl list | grep -q "^menu start" && /sbin/initctl stop menu
    killall -q menu-boot.sh menu 2>/dev/null || true
    setsid sh /mnt/us/menu-boot.sh >>/mnt/us/menu-boot.log 2>&1 </dev/null &
'

# The menu session draws the panel; give it a moment and confirm.
echo "install-all: confirming the menu is up"
i=0
while [ $i -lt 45 ]; do
    if ssh_run 'pidof menu >/dev/null 2>&1' 2>/dev/null; then
        echo "install-all: menu is running on $ip (pid $(ssh_run pidof menu))"
        echo "install-all: done. Next boot lands on the menu."
        exit 0
    fi
    sleep 2
    i=$((i + 2))
done
echo "install-all: ERROR: the menu did not come up within 45s; see the log:" >&2
ssh_run 'tail -n 25 /mnt/us/menu-boot.log 2>/dev/null' >&2 || true
exit 1