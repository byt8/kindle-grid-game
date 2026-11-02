#!/bin/sh
# menu-boot.sh -- the always-on launcher session, run on the Kindle.
#
# Started by the menu upstart job (tools/menu-boot.conf installed as
# /etc/upstart/menu.conf). This one script owns the whole session:
#
#   boot -> stop the screen mirror -> menu -> game -> menu -> game -> ...
#                                        \_-> KINDLE button -> restore the
#                                            reader UI and stop
#
# The menu process gives up the panel + touch grab when it exits with a game's
# code, so handing from menu to game is just process death: the game's own
# ek_open(TAKEOVER) re-grabs. When the game quits, its runner restores the
# Kindle UI (that is what tools/kindle-run.sh does on exit) and this loop
# re-takes it with the menu -- so the Amazon UI stays invisible all boot.
#
# Everything is logged to /mnt/us/menu-boot.log; at boot there is no shell to
# watch anything.
set -u

LOG=/mnt/us/menu-boot.log
: > "$LOG"
say() { echo "menu-boot: $*" >> "$LOG"; }

# --- stop the screen mirror so our touch grab wins ----------------------
# kfbterm EVIOCGRABs /dev/input/event3 on `started lab126_gui`; if it is up
# when the menu runs, the grab fails (EBUSY) and the menu draws but ignores
# every tap. Same fight the game runners solve, solved here before the menu.
stop_mirror() {
    if /sbin/initctl list 2>/dev/null | grep -q '^kfbterm start'; then
        say "stopping the screen mirror (kfbterm)"
        /sbin/initctl stop kfbterm >> "$LOG" 2>&1
    fi
    [ -z "$(pidof kfb 2>/dev/null || true)" ] && return 0
    say "killing the screen mirror (pid $(pidof kfb))"
    killall -q kfb 2>/dev/null
    i=0
    while [ -n "$(pidof kfb 2>/dev/null || true)" ] && [ $i -lt 10 ]; do
        i=$((i + 1))
        sleep 1
    done
    if [ -n "$(pidof kfb 2>/dev/null || true)" ]; then
        say "ERROR: kfb will not die; touch stays locked"
        return 1
    fi
    return 0
}

# --- give wifid a moment to finish associating ---------------------------
# Stopping the framework while wifid is still associating has left this unit
# with no link at all (learned on the chess boot). The menu does not need the
# network, so wait briefly -- but the menu must win the screen either way.
wait_wifi() {
    [ -x /mnt/us/chess-wifi.sh ] || return 0
    i=0
    while [ $i -lt 20 ]; do
        if sh /mnt/us/chess-wifi.sh status >/dev/null 2>&1; then
            say "wifi up after ${i}s"
            return 0
        fi
        [ $((i % 10)) -eq 0 ] && sh /mnt/us/chess-wifi.sh auto >> "$LOG" 2>&1
        i=$((i + 2))
        sleep 2
    done
    say "no wifi after 20s; the menu's WIFI button can fix that"
    return 0
}

wait_wifi

while :; do
    if ! stop_mirror; then
        say "cannot free the touch device; leaving the Kindle UI up"
        : > /mnt/us/.menu-stop
        exit 1
    fi
    if [ -x /mnt/us/grabtest ] && ! /mnt/us/grabtest >> "$LOG" 2>&1; then
        say "touch device still grabbed (grabtest failed); reader stays up"
        : > /mnt/us/.menu-stop
        exit 1
    fi
    # The power key belongs to the menu while it shows (same rule as the game
    # runner: powerd answers a press with the screen saver, painting over the
    # panel). The runners of the games stop/restart powerd per game; this loop
    # covers the menu phases between them. restore_ui() starts it again.
    /sbin/initctl stop powerd >> "$LOG" 2>&1 || say "could not stop powerd for the menu"
    /mnt/us/menu >> "$LOG" 2>&1
    rc=$?
    /sbin/initctl start powerd >> "$LOG" 2>&1 || say "could not restart powerd after the menu"
    case $rc in
        0)  say "KINDLE chosen; restoring the reader UI"
            : > /mnt/us/.menu-stop
            /sbin/initctl start powerd >> "$LOG" 2>&1 || \
                say "could not restart powerd for the reader UI"
            sh /mnt/us/fbdaemon/restore-kindle.sh >> "$LOG" 2>&1
            exit 0 ;;
        1)  say "launching XCOM"
            GAME=xcom sh /mnt/us/xcom-run.sh >> "$LOG" 2>&1 ;;
        2)  say "launching CHESS"
            GAME=chess sh /mnt/us/chess-run.sh --conf /mnt/us/chess.conf >> "$LOG" 2>&1 ;;
        3)  say "launching TETRIS"
            GAME=tetris sh /mnt/us/tetris-run.sh >> "$LOG" 2>&1 ;;
        4)  say "launching SUDOKU"
            GAME=sudoku sh /mnt/us/sudoku-run.sh >> "$LOG" 2>&1 ;;
        5)  say "launching ROCKMAN"
            GAME=rockman sh /mnt/us/rockman-run.sh >> "$LOG" 2>&1 ;;
        6)  say "launching KOREADER"
            # Framework is already stopped and the panel is ours; koreader.sh
            # runs without --framework_stop and leaves the UI dead on exit,
            # which is fine -- the launcher re-takes the panel next loop. Log
            # tail is our only record on the device.
            sh /mnt/us/koreader/koreader.sh >> /mnt/us/koreader-launch.log 2>&1 ;;
        7)  say "WIFI setup"
            /mnt/us/chess --wifi-only --conf /mnt/us/chess.conf >> "$LOG" 2>&1 ;;
        *)  say "menu exited rc=$rc; showing it again" ;;
    esac
    say "back to the menu"
done
