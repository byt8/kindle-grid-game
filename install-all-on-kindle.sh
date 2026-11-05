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
# The device's wifi sleeps, so both the ssh calls and the file streams retry:
# a dropped connection is not allowed to leave half the new binaries behind,
# and every file lands at "<name>.new" and is renamed only once whole.
#
# Exit codes, all deliberate:
#   0  every file is on the device and the boot session is drawing the panel
#   1  nothing was sent: the address, the key or the build failed first
#   2  everything is installed but the panel did not start; the log is printed
set -eu
cd "$(dirname "$0")"

KJ=jailbreak-kindle/kindle-tools
if [ -f "$KJ/.kfb-secrets" ]; then
    KFB_ENV_FILE="$(pwd)/$KJ/.kfb-secrets"
    export KFB_ENV_FILE
    . "$KJ/env.sh"
fi
key=${KINDLE_KEY:-$HOME/.ssh/kindle_touch_ed25519}

no_build=0
no_koreader=0
ip=
for a in "$@"; do
    case "$a" in
        --no-build)    no_build=1 ;;
        --no-koreader) no_koreader=1 ;;
        -h|--help)
            # The header comment, up to the first non-comment line.
            awk 'NR>1 && /^#/ { sub(/^# ?/, ""); print; next } NR>1 { exit }' "$0"
            exit 0 ;;
        -*)
            echo "install-all: unknown option: $a (try --help)" >&2; exit 1 ;;
        *)
            [ -n "$ip" ] && { echo "install-all: more than one address given" >&2; exit 1; }
            ip=$a ;;
    esac
done

# The address is the one argument that is not optional: everything below
# writes to /mnt/us on that device. Take it from the command line, else
# $KINDLE_IP, else ask -- rather than quietly aiming at a hard-coded Kindle
# that is not on this network any more.
if [ -z "$ip" ]; then
    if [ -n "${KINDLE_IP:-}" ]; then
        ip=$KINDLE_IP
    elif [ -t 0 ]; then
        printf 'Kindle IP address: '
        read -r ip || ip=
        [ -n "$ip" ] || { echo "install-all: no address given" >&2; exit 1; }
    else
        echo "install-all: no Kindle address. Usage: $0 <kindle-ip>" >&2
        exit 1
    fi
fi
case $ip in
    *[!0-9.]*|"") echo "install-all: \"$ip\" is not an IP address" >&2; exit 1 ;;
esac

[ -f "$key" ] || { echo "install-all: ssh key $key is missing" >&2; exit 1; }

# A Kindle on wifi sleeps: the radio can drop for ten or twenty seconds in the
# middle of a push, which is not a failure and must not leave the device with
# half the new binaries. Every call retries, and so does every stream (see
# push below). Scale is 1 s, 2 s, 4 s, ... up to 20 s, ten tries.
ssh_try() {
    n=0
    delay=1
    while :; do
        ssh -i "$key" -o BatchMode=yes -o IdentitiesOnly=yes \
            -o StrictHostKeyChecking=no -o UserKnownHostsFile="$HOME/.ssh/known_hosts" \
            -o ServerAliveInterval=5 -o ServerAliveCountMax=3 \
            -o LogLevel=ERROR -o ConnectTimeout=10 "root@$ip" "$@" && return 0
        n=$((n + 1))
        [ $n -ge 10 ] && return 1
        echo "install-all: ssh to $ip failed (try $n); waiting ${delay}s" >&2
        sleep $delay
        delay=$((delay * 2))
        [ $delay -gt 20 ] && delay=20
    done
}

# One shot, no retries: for polling, where "no" is an answer and waiting
# two minutes for it is not.
ssh_once() {
    ssh -i "$key" -o BatchMode=yes -o IdentitiesOnly=yes \
        -o StrictHostKeyChecking=no -o UserKnownHostsFile="$HOME/.ssh/known_hosts" \
        -o LogLevel=ERROR -o ConnectTimeout=10 "root@$ip" "$@"
}

ssh_run() { ssh_try "$@"; }

# Push a file over stdin to a path on the device. The stream is re-sent from
# the start on a dropped connection, so a half-written file never survives:
# everything lands at "$path.new" and is renamed only when whole. Retrying the
# cat is safe because the source is a file we can read again; a pipe that
# cannot be rewound is refused rather than pushed short.
push_file() {
    src=$1
    dst=$2
    mode=${3:-755}
    [ -f "$src" ] || { echo "install-all: no $src to push" >&2; return 1; }
    size=$(wc -c < "$src" | tr -d ' ')
    n=0
    delay=1
    while :; do
        if cat "$src" | ssh_try "cat > $dst.new && chmod $mode $dst.new && mv $dst.new $dst"; then
            got=$(ssh_try "wc -c < $dst" 2>/dev/null | tr -d ' \t')
            if [ "$got" = "$size" ]; then
                return 0
            fi
            echo "install-all: $dst is $got bytes, want $size; sending again" >&2
        fi
        n=$((n + 1))
        [ $n -ge 5 ] && { echo "install-all: could not push $src to $dst" >&2; return 1; }
        echo "install-all: pushing $src failed (try $n); waiting ${delay}s" >&2
        sleep $delay
        delay=$((delay * 2))
        [ $delay -gt 20 ] && delay=20
    done
}

# --------------------------------------------------------------- reach it --
# Check the device before compiling anything: a build is a couple of minutes
# and a wrong address should cost seconds, not that.
echo "install-all: checking root@$ip"
ssh_run 'echo hello' >/dev/null 2>&1 || {
    echo "install-all: cannot ssh to root@$ip with $key" >&2
    echo "            is the Kindle awake, on this network, and is ssh on?" >&2
    exit 1
}
ssh_run 'test -d /mnt/us' 2>/dev/null || {
    echo "install-all: $ip answered but has no /mnt/us -- not a Kindle" >&2
    exit 1
}
echo "install-all: $ip answers; it is a Kindle with /mnt/us"

# ------------------------------------------------------------------ build --
if [ "$no_build" = 0 ]; then
    echo "install-all: building the device binaries"
    # Same group as install-menu-on-kindle.sh, plus the standalone games and
    # pvz/calib/brogue which install-menu does not build.
    make menu chess tetris sudoku mines rockman pvz pipes calib grabtest
    make kindle          # xcom: oldest and largest build
    make brogue-kindle
fi
# The push list. Every binary must exist: a missing one is a silent no-op
# otherwise ("cat: no such file"). Failing loudly here is by design.
need() {
    [ -f "$1" ] || { echo "install-all: missing $1 -- build it first (make)" >&2; exit 1; }
}
need menu; need grabtest; need chess; need tetris; need sudoku; need mines; need rockman
need xcom; need pvz; need pipes; need calib; need brogue-kindle-app
need tools/kindle-run.sh; need tools/menu-boot.sh; need tools/menu-boot.conf
need tools/chess-wifi.sh;  need tools/chess-boot.conf
need "$KJ/fbdaemon/restore-kindle.sh"; need "$KJ/fbdaemon/kfb-watchdog.sh"
need "$KJ/fbdaemon/start-kfb.sh";      need "$KJ/fbdaemon/out/kfb"
need "$KJ/fbdaemon/kfbterm.conf"

echo "install-all: pushing games to $ip"
# Write beside then rename: a copy over a running binary is "Text file busy".
for g in menu chess tetris sudoku mines rockman xcom pvz pipes calib brogue-kindle-app grabtest; do
    push_file "$g" "/mnt/us/$g" 755
    echo "install-all:   $g"
done
# Grabtest was already pushed above in the loop; this is the binary itself.

# The runners: one kindle-run.sh per game name.
for g in chess xcom tetris sudoku mines rockman pvz pipes calib brogue koreader; do
    push_file tools/kindle-run.sh "/mnt/us/$g-run.sh" 755
done
push_file tools/menu-boot.sh    /mnt/us/menu-boot.sh  755
push_file tools/menu-boot.conf  /mnt/us/menu-boot.conf 644
push_file tools/chess-wifi.sh   /mnt/us/chess-wifi.sh  755
push_file tools/chess-boot.conf /mnt/us/chess-boot.conf 644

# chess needs its API key (optional: without chess.conf it uses built-in
# defaults) and the CA bundle the Kindle's own store predates.
if [ -f chess.conf ]; then
    push_file chess.conf /mnt/us/chess.conf 600
else
    echo "install-all: no chess.conf; chess will use its built-in defaults" >&2
fi
ca=${CHESS_CA:-/etc/ssl/certs/ca-certificates.crt}
if [ -f "$ca" ]; then
    echo "install-all: pushing CA bundle $ca"
    push_file "$ca" /mnt/us/chess-ca.pem 644
else
    echo "install-all: no CA bundle at $ca; the device will have to trust" >&2
    echo "              DeepSeek on its own old certificates." >&2
fi

# xcom talks; the speaker reads one file per word, so the whole folder goes
# over in one stream, replaced (never merged).
if [ -d sound/words ]; then
    echo "install-all: pushing $(ls sound/words | wc -l) word clips and the effects"
    # A dropped stream restarts the whole archive: the remote side clears the
    # folder first, so a retry cannot merge two half-copies.
    n=0
    delay=1
    while :; do
        if tar -cf - -C sound words fx | ssh_try '
                rm -rf /mnt/us/xcom-sound
                mkdir -p /mnt/us/xcom-sound
                tar -xf - -C /mnt/us/xcom-sound'; then
            break
        fi
        n=$((n + 1))
        [ $n -ge 5 ] && { echo "install-all: could not push the sound clips" >&2; exit 1; }
        echo "install-all: sound push failed (try $n); waiting ${delay}s" >&2
        sleep $delay
        delay=$((delay * 2))
    done
else
    echo "install-all: no sound/words; xcom will be silent (run: make sound)" >&2
fi

# ------------------------------------------------------------------ fbdaemon --
# The games' kindle-run.sh and the menu's KINDLE button both hand the panel
# back through restore-kindle.sh, and the kfbterm boot job re-asserts ssh +
# the firewall hole every 30 s. A fresh device needs all of it.
echo "install-all: pushing the fbdaemon set"
ssh_run 'mkdir -p /mnt/us/fbdaemon'
push_file "$KJ/fbdaemon/restore-kindle.sh" /mnt/us/fbdaemon/restore-kindle.sh 755
push_file "$KJ/fbdaemon/kfb-watchdog.sh"   /mnt/us/fbdaemon/kfb-watchdog.sh   755
push_file "$KJ/fbdaemon/start-kfb.sh"      /mnt/us/fbdaemon/start-kfb.sh      755
push_file "$KJ/fbdaemon/out/kfb"           /mnt/us/fbdaemon/kfb               755
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
push_file "$KJ/fbdaemon/kfbterm.conf" /mnt/us/kfbterm.conf 644
# trap: a dropped connection halfway through must not leave the rootfs
# read-write.
ssh_run '
    trap "/usr/sbin/mntroot ro" EXIT
    /usr/sbin/mntroot rw
    cp /mnt/us/menu-boot.conf /etc/upstart/menu.conf
    rm -f /etc/upstart/chess.conf          # the old direct-to-chess job
    cp /mnt/us/kfbterm.conf /etc/upstart/kfbterm.conf
    sync
    /usr/sbin/mntroot ro
    trap - EXIT
'

# ----------------------------------------------------------------- koreader --
if [ "$no_koreader" = 0 ] && [ -f ./install-koreader-on-kindle.sh ]; then
    echo "install-all: installing KOReader"
    ./install-koreader-on-kindle.sh "$ip"
else
    echo "install-all: skipping KOReader"
fi

# -------------------------------------------------------------- start it ----
echo "install-all: starting the boot session on $ip"
ssh_run '
    /sbin/initctl list | grep -q "^menu start" && /sbin/initctl stop menu
    killall -q menu-boot.sh menu 2>/dev/null || true
    # Wait for the old session to be gone before starting the new one. Two
    # menu-boot.sh running at once both try to take the e-ink panel and both
    # launch the game, which showed up as "could not stop powerd for the menu"
    # followed by a second "launching rockman" and no app on the panel.
    i=0
    while [ -n "$(pidof menu-boot.sh 2>/dev/null)" ] && [ $i -lt 20 ]; do
        sleep 1
        i=$((i + 1))
    done
    setsid sh /mnt/us/menu-boot.sh >>/mnt/us/menu-boot.log 2>&1 </dev/null &
'

# Confirm the session took the panel. There is no launcher screen to look for:
# menu-boot.sh opens whatever /mnt/us/last-app names (chess the first time),
# and the APPS tab inside each game is the menu. So the success condition is
# the last-app binary being alive, and eating CPU is not the signal -- a
# process that got as far as the e-ink takeover is.
echo "install-all: confirming the session is on the panel"
app=$(ssh_once 'cat /mnt/us/last-app 2>/dev/null' 2>/dev/null | tr -d ' \t\n\r' | tr 'A-Z' 'a-z')
[ -n "$app" ] || app=chess
case $app in
    brogue) bin=brogue-kindle-app ;;
    koreader) bin= ;;
    *) bin=$app ;;
esac
if [ -z "$bin" ]; then
    echo "install-all: last-app is koreader; the reader owns the panel now"
    echo "install-all: done. Next boot opens KOReader."
    exit 0
fi

i=0
while [ $i -lt 120 ]; do
    if pid=$(ssh_once "pidof $bin 2>/dev/null" 2>/dev/null) && [ -n "$pid" ]; then
        # A pid that is there now can still be an app that took the panel and
        # quit a second later -- menu-boot.sh then starts it again, so keep
        # polling instead of giving up on the first short life (an early
        # `break` here said exit 2 while the game was up 4 s later).
        sleep 6
        # "|| true": a wifi blip here is not a failed deploy, and the loop's
        # own ERROR branch prints the log. Under set -e an unguarded call
        # would exit 1 with no message at all.
        pid2=$(ssh_once "pidof $bin 2>/dev/null" 2>/dev/null || true)
        if [ -n "$pid2" ]; then
            echo "install-all: $bin is on the panel on $ip (pid $pid2)"
            echo "install-all: done. Next boot opens $app; APPS inside it switches game."
            exit 0
        fi
        i=$((i + 6))
    else
        sleep 3
        i=$((i + 3))
    fi
done
echo "install-all: ERROR: $app did not come up within 120s; see the log below." >&2
echo "install-all: everything is installed -- the panel just did not start, so" >&2
echo "              try again in a few seconds, or force it:" >&2
echo "                ssh root@$ip 'killall menu-boot.sh; setsid sh /mnt/us/menu-boot.sh &'" >&2
# menu-boot.log also carries the ALSA mixer dump; show only the lines that
# say what the session actually did.
ssh_once 'grep -a "menu-boot:\|app exited\|kframe:\|eink:\|User defined" /mnt/us/menu-boot.log 2>/dev/null | tail -n 20' >&2 || true
exit 2