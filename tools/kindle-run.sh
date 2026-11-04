#!/bin/sh
# kindle-run.sh -- device-side wrapper: run a game without stealing or stranding.
#
# Pushed to /mnt/us/xcom-run.sh (or /mnt/us/chess-run.sh, /mnt/us/koreader-run.sh)
# and run there with a pty, so a ^C on the PC hangs up this shell too and the
# restore below still happens. GAME names the binary: /mnt/us/$GAME, killed and
# restarted by name -- except GAME=koreader, which runs KOReader's own launcher
# script (see step 5).
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

# A game must not die with the PC's ssh session. The Kindle sleeps when the
# power button is tapped, and while it is asleep the connection drops; sshd then
# hangs this session up, the game is killed by SIGHUP, and the reader is left
# frozen on the last frame with its own UI shut down. With --detach the wrapper
# re-runs itself in a session of its own -- setsid, so it has no controlling
# terminal and nothing can hang it up -- with its output in a log file, and
# answers at once. The game, the QUIT path and the restore trap below then
# belong to the device instead of to the PC.
if [ "${1:-}" = --detach ]; then
    shift
    log=/mnt/us/$GAME-run.log
    : >"$log"
    setsid sh "$0" "$@" >"$log" 2>&1 </dev/null &
    echo "detached from the PC: log $log"
    exit 0
fi

# Was the mirror running when we arrived? Decides whether to start it again.
mirror_was=0
if /sbin/initctl list 2>/dev/null | grep -q '^kfbterm start'; then
    mirror_was=1
fi

# Was the launcher session (tools/menu-boot.sh) showing its menu when we
# arrived? Decides whether to start it again. Defined before the trap below so
# an early exit still gives the launcher back.
menu_was=0

restore() {
    # The game stops powerd so the power key belongs to it (chess sleeps and
    # wakes the Kindle itself). Give the stock button behaviour back.
    if ! /sbin/initctl status powerd 2>/dev/null | grep -q 'start/running'; then
        if /sbin/initctl start powerd >/dev/null 2>&1; then
            say "powerd restarted"
        else
            say "WARNING: could not restart powerd; the power button will do"
            say "         nothing until it is: /sbin/initctl start powerd"
        fi
    fi
    rm -f /mnt/us/DONT_START_FRAMEWORK
    # Drop the "a runner owns the panel" stamp before anything starts the
    # menu again. Left in place, the next menu death looks like a handoff
    # and the session exits instead of coming back.
    rm -f /mnt/us/.menu-handoff

    if [ -x /mnt/us/menu ] && [ -f /mnt/us/menu-boot.sh ]; then
        # Hand the panel back to the launcher session, not the stock reader.
        # This is not conditional on having found a menu running on arrival:
        # a game started from the PC never touched the menu, and a .menu-stop
        # left behind by an earlier session keeps the job down -- both used to
        # send this exit down the reader path, and Amazon's reader opens on its
        # own store. The menu is where a game gets chosen, so it is the screen
        # the panel comes back to. The reader restore below still exists, but
        # only for a device that cannot run the launcher at all: the menu job
        # pulls the panel out from under the reader (it stops the mirror and
        # keeps the Amazon UI hidden), so starting both would leave them
        # fighting over the glass.
        rm -f /mnt/us/.menu-stop
        if ! /sbin/initctl status menu 2>/dev/null | grep -q 'start/running'; then
            say "giving the panel back to the launcher session (menu)"
            /sbin/initctl start menu >/dev/null 2>&1
            i=0
            while ! /sbin/initctl status menu 2>/dev/null | grep -q 'start/running' && [ $i -lt 5 ]; do
                i=$((i + 1))
                sleep 1
            done
        fi
        if /sbin/initctl status menu 2>/dev/null | grep -q 'start/running'; then
            return 0
        fi
        say "ERROR: the launcher (menu job) would not restart; falling back to"
        say "       restoring the stock reader so the panel is not left dead"
    fi

    if [ -x /mnt/us/fbdaemon/restore-kindle.sh ]; then
        # Two goes at it. The first has been seen to bring X back and then leave
        # the reader down, right after a wake, so ask again rather than hand the
        # device back with a dead panel.
        sh /mnt/us/fbdaemon/restore-kindle.sh
        if [ -z "$(pidof cvm 2>/dev/null || true)" ]; then
            say "the reader did not come back; running the restore once more"
            sh /mnt/us/fbdaemon/restore-kindle.sh
        fi
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
#    The leftover is not necessarily $GAME: the session that died may have
#    been running rockman, tetris or any other game (seen 2026-11-02: an
#    orphaned rockman made every xcom deploy fail with EBUSY), so every
#    known game is cleared, one by one. The launcher menu is NOT here --
#    killing it would respawn it; step 2's stop of the menu job is what
#    takes the grab away from it.
kill_leftover() {
    g=$1
    leftover=$(pidof "$g" 2>/dev/null || true)
    [ -n "$leftover" ] || return 0
    say "killing leftover game $g pid $leftover"
    killall "$g" 2>/dev/null
    i=0
    while [ -n "$(pidof "$g" 2>/dev/null || true)" ] && [ $i -lt 10 ]; do
        i=$((i + 1))
        sleep 1
    done
    if [ -n "$(pidof "$g" 2>/dev/null || true)" ]; then
        # A game stuck inside a blocking call never reaches its SIGTERM
        # handler; without this it holds the touch grab and every launch
        # after it fails here.
        say "the leftover game $g (pid $(pidof "$g")) ignored the term; sending KILL"
        kill -9 $(pidof "$g") 2>/dev/null
        i=0
        while [ -n "$(pidof "$g" 2>/dev/null || true)" ] && [ $i -lt 5 ]; do
            i=$((i + 1))
            sleep 1
        done
    fi
    if [ -n "$(pidof "$g" 2>/dev/null || true)" ]; then
        say "ERROR: the leftover game $g (pid $(pidof "$g")) will not exit"
        exit 1
    fi
}
for g in xcom rockman tetris chess sudoku calib pvz brogue-kindle-app luajit koreader koreader.sh; do
    kill_leftover "$g"
done

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

# Pids whose command line contains $1. Busybox pidof only sees the 15-character
# comm name, so a shell script (menu-boot.sh shows up as "sh") is invisible to it.
pids_cmdline() {
    needle=$1
    for d in /proc/[0-9]*; do
        cmd=$(tr '\0' ' ' < "$d/cmdline" 2>/dev/null) || continue
        case "$cmd" in
            *"$needle"*) echo "${d#/proc/}";;
        esac
    done
}

# 2b. The launcher session draws its menu with the touch grab held, so a game
#     started from a PC while that session is at its menu phase is refused here
#     exactly like the mirror: grabtest reports EBUSY and the run dies before
#     drawing anything. The menu process is only alive in that phase -- when the
#     session launches a game itself the menu exits first (that is the handover,
#     see tools/menu-boot.sh) -- so a live `menu` here is always someone else's
#     session holding the panel, never our own parent.
#
#     Do not SIGTERM the menu. menu.c treats SIGTERM as the KINDLE button:
#     it exits 0 and ek_close() starts the reader, then menu-boot.sh runs
#     restore-kindle.sh while this game is taking the panel. The two fight,
#     cvm never comes back, and the screen stays frozen. SIGKILL cannot be
#     caught, so the grab dies with the process and the reader stays down.
#     The session script is killed in the same command so it cannot see the
#     menu die and run that restore. restore() above puts the menu back on
#     the way out.
job_up=0
if /sbin/initctl status menu 2>/dev/null | grep -q 'start/running'; then
    job_up=1
fi
menus=$(pids_cmdline '/mnt/us/menu ')
boots=$(pids_cmdline 'menu-boot.sh')
if [ "$job_up" = 1 ] || [ -n "$menus$boots" ]; then
    say "stopping the launcher session (menu) so it lets go of the touch device"
    menu_was=1
    : > /mnt/us/.menu-handoff
    # Supervisor loop in menu.conf relaunches menu-boot unless this stamp exists.
    # The loop itself deletes the stamp at the top of every pass, and its shell
    # is not named menu or menu-boot, so one kill is not enough: the supervisor
    # starts a new menu during the wait and the old check reports "will not exit".
    : > /mnt/us/.menu-stop
    i=0
    while :; do
        menus=$(pids_cmdline '/mnt/us/menu ')
        boots=$(pids_cmdline 'menu-boot.sh')
        jobpid=$(/sbin/initctl status menu 2>/dev/null | sed -n 's/.*process //p')
        [ -z "$menus$boots$jobpid" ] && break
        kill -9 $boots $menus $jobpid 2>/dev/null
        /sbin/initctl stop menu >/dev/null 2>&1
        i=$((i + 1))
        [ "$i" -ge 10 ] && break
        sleep 1
    done
    if [ -n "$menus$boots" ]; then
        say "ERROR: the launcher will not exit (menu: ${menus:-none} menu-boot: ${boots:-none}),"
        say "       so touch would stay locked. Stop it with: initctl stop menu"
        exit 1
    fi
fi

# 3. Every game owns the power key while it runs. powerd answers a press
#    with the Kindle screen saver, which would paint over the board (chess
#    used to stop powerd in-game; doing it once here covers every game the
#    same way, and restore() on exit starts it again).
if /sbin/initctl status powerd 2>/dev/null | grep -q 'start/running'; then
    say "stopping powerd so the power key belongs to the game"
    /sbin/initctl stop powerd >/dev/null 2>&1 || \
        say "WARNING: could not stop powerd; a power press may raise the screen saver"
fi

# 4. Prove the touch device is free before drawing anything. A refusal here is
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

# 5. Play. xcom talks, so it gets its clips or an explicit --mute; anything
#    else (chess) has no sound to arrange. GAME=koreader is not a game binary:
#    it runs KOReader's own launcher, deliberately WITHOUT --framework_stop --
#    koreader.sh's STOP_FRAMEWORK=no path just works over a dead UI, exactly as
#    the launcher session runs it (see tools/menu-boot.sh). Any argument passed
#    to this script goes to the app (--auto 3, --apicheck, --asap and friends).
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
say "starting the app"
# Keep a copy of what the app says on the device itself. The ssh session that
# carries the live copy dies with the launcher, and then a fault that happened
# while it ran leaves no evidence at all.
# The status is written to a file from the left-hand side because a pipeline
# only reports tee's exit code.
rm -f /tmp/$GAME.rc
# Marks this boot as having run an app, so the boot job does not start a second
# one when a QUIT restores the reader. /tmp is ram, so it clears at reboot.
: >/tmp/$GAME.started
: >/mnt/us/$GAME.log
if [ "$GAME" = koreader ]; then
    ( sh /mnt/us/koreader/koreader.sh "$@" 2>&1; echo $? >/tmp/$GAME.rc ) | tee -a /mnt/us/$GAME.log
else
    ( /mnt/us/$GAME $args "$@" 2>&1; echo $? >/tmp/$GAME.rc ) | tee -a /mnt/us/$GAME.log
fi
rc=$(cat /tmp/$GAME.rc 2>/dev/null || echo 1)
say "app exited rc=$rc"
say "       the app's own output is in /mnt/us/$GAME.log"
exit $rc
