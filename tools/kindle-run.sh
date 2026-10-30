#!/bin/sh
# kindle-run.sh -- device-side wrapper: run xcom without stealing or stranding.
#
# Pushed to /mnt/us/xcom-run.sh by ../../run-on-kindle.sh and run there with a
# pty, so a ^C on the PC hangs up this shell too and the restore below still
# happens.
#
# Why any of this is needed:
#   * /dev/input/event3 allows exactly one EVIOCGRAB. The screen mirror
#     (`kfb serve --stop-framework`, started by the `kfbterm` upstart job) owns
#     it on this device, so the game's grab fails with EBUSY and *touch dies
#     silently* -- the picture still draws, so it looks like a frozen game.
#     The mirror therefore has to be stopped before the game runs.
#   * A previous game killed over ssh (no tty, so no SIGHUP) stays alive and
#     keeps the grab. It is killed here too.
#   * Whatever happens, the framework and the mirror go back the way they were.
set -u

say() { echo "xcom-run: $*"; }

# Was the mirror running when we arrived? Decides whether to start it again.
mirror_was=0
if /sbin/initctl list 2>/dev/null | grep -q '^kfbterm start'; then
    mirror_was=1
fi

restore() {
    rm -f /mnt/us/DONT_START_FRAMEWORK
    if [ -x /mnt/us/fbdaemon/restore-kindle.sh ]; then
        sh /mnt/us/fbdaemon/restore-kindle.sh
    else
        say "no /mnt/us/fbdaemon/restore-kindle.sh; starting the framework alone"
        /sbin/initctl start framework
    fi
    if [ "$mirror_was" = 1 ]; then
        say "restarting the screen mirror (kfbterm)"
        /sbin/initctl start kfbterm
    fi
}
trap restore EXIT
trap 'exit 1' HUP INT TERM

# 1. A game left behind by a killed ssh session still holds the touch grab.
leftover=$(pidof xcom 2>/dev/null || true)
if [ -n "$leftover" ]; then
    say "killing leftover game pid $leftover"
    killall xcom 2>/dev/null
    i=0
    while [ -n "$(pidof xcom 2>/dev/null || true)" ] && [ $i -lt 10 ]; do
        i=$((i + 1))
        sleep 1
    done
    if [ -n "$(pidof xcom 2>/dev/null || true)" ]; then
        say "ERROR: the leftover game (pid $(pidof xcom)) will not exit"
        exit 1
    fi
fi

# 2. The mirror owns the touch device. Stop its job first so nothing respawns
#    it, then kill the daemon. Straight to SIGKILL on purpose: kfb's SIGTERM
#    handler restores the whole Kindle UI before it exits, which takes far
#    longer than we want to wait, and the game takes the panel anyway -- the
#    restore below is what puts the UI back.
if [ "$mirror_was" = 1 ]; then
    say "stopping the screen mirror (kfbterm) so it lets go of the touch device"
    /sbin/initctl stop kfbterm
fi
if [ -n "$(pidof kfb 2>/dev/null || true)" ]; then
    say "killing the screen mirror (pid $(pidof kfb))"
    kill -9 $(pidof kfb) 2>/dev/null
    i=0
    while [ -n "$(pidof kfb 2>/dev/null || true)" ] && [ $i -lt 10 ]; do
        i=$((i + 1))
        sleep 1
    done
    if [ -n "$(pidof kfb 2>/dev/null || true)" ]; then
        say "ERROR: the screen mirror (pid $(pidof kfb)) will not exit, so touch"
        say "       would stay locked. Stop it with: initctl stop kfbterm"
        exit 1
    fi
fi

# 3. Prove the touch device is free before drawing anything. A refusal here is
#    a hard stop: running anyway gives a game whose screen responds to nothing.
if [ ! -x /mnt/us/grabtest ]; then
    say "ERROR: /mnt/us/grabtest is missing, cannot check the touch device"
    exit 1
fi
if ! /mnt/us/grabtest; then
    say "ERROR: /dev/input/event3 is still grabbed by:"
    for p in /proc/[0-9]*; do
        if ls -l "$p/fd" 2>/dev/null | grep -q event3; then
            echo "  $(cat "$p/cmdline" 2>/dev/null | tr '\0' ' ')"
        fi
    done
    exit 1
fi

# 4. Play.
say "starting the game"
/mnt/us/xcom
rc=$?
say "game exited rc=$rc"
exit $rc
