#!/bin/sh
# kindle-run.sh -- device-side wrapper: run a game without stealing or stranding.
#
# Pushed to /mnt/us/xcom-run.sh (or /mnt/us/chess-run.sh) and run there with a
# pty, so a ^C on the PC hangs up this shell too and the restore below still
# happens. GAME names the binary: /mnt/us/$GAME, killed and restarted by name.
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

GAME=${GAME:-xcom}
say() { echo "$GAME-run: $*"; }

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
leftover=$(pidof "$GAME" 2>/dev/null || true)
if [ -n "$leftover" ]; then
    say "killing leftover game pid $leftover"
    killall "$GAME" 2>/dev/null
    i=0
    while [ -n "$(pidof "$GAME" 2>/dev/null || true)" ] && [ $i -lt 10 ]; do
        i=$((i + 1))
        sleep 1
    done
    if [ -n "$(pidof "$GAME" 2>/dev/null || true)" ]; then
        # A game stuck inside a blocking call never reaches its SIGTERM
        # handler; without this it holds the touch grab and every launch
        # after it fails here.
        say "the leftover game (pid $(pidof "$GAME")) ignored the term; sending KILL"
        kill -9 $(pidof "$GAME") 2>/dev/null
        i=0
        while [ -n "$(pidof "$GAME" 2>/dev/null || true)" ] && [ $i -lt 5 ]; do
            i=$((i + 1))
            sleep 1
        done
    fi
    if [ -n "$(pidof "$GAME" 2>/dev/null || true)" ]; then
        say "ERROR: the leftover game (pid $(pidof "$GAME")) will not exit"
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

# 4. Play. xcom talks, so it gets its clips or an explicit --mute; anything
#    else (chess) has no sound to arrange. Any argument passed to this script
#    goes to the game (--auto 3, --apicheck and friends).
args=""
if [ "$GAME" = xcom ]; then
    sound_dir=/mnt/us/xcom-sound
    args="--sound $sound_dir"
    if [ ! -x /usr/bin/aplay ]; then
        say "no /usr/bin/aplay on this device, so the game will be silent"
        args="$args --mute"
    elif [ ! -d "$sound_dir/words" ]; then
        say "no word clips in $sound_dir, so the game will be silent"
        say "       (run-on-kindle.sh pushes them; 'make sound' bakes them)"
        args="$args --mute"
    fi
fi
say "starting the game"
# Keep a copy of what the game says on the device itself. The ssh session that
# carries the live copy dies with the launcher, and then a fault that happened
# while it ran leaves no evidence at all.
# The status is written to a file from the left-hand side because a pipeline
# only reports tee's exit code.
rm -f /tmp/$GAME.rc
: >/mnt/us/$GAME.log
( /mnt/us/$GAME $args "$@" 2>&1; echo $? >/tmp/$GAME.rc ) | tee -a /mnt/us/$GAME.log
rc=$(cat /tmp/$GAME.rc 2>/dev/null || echo 1)
say "game exited rc=$rc"
say "       the game's own output is in /mnt/us/$GAME.log"
exit $rc
