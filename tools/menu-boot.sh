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
# The previous session's log is the evidence after a crash-reboot (the old
# one wiped itself at every boot, so the reboot erased the crash record).
[ -s "$LOG" ] && mv -f "$LOG" "$LOG.prev"
: > "$LOG"
say() { echo "menu-boot: $*" >> "$LOG"; }

# --- stop the screen mirror so our touch grab wins ----------------------
# kfbterm EVIOCGRABs /dev/input/event3 on `started lab126_gui`; if it is up
# when the menu runs, the grab fails (EBUSY) and the menu draws but ignores
# every tap. Same fight the game runners solve, solved here before the menu.
# The xcom RESTORE path (tools/kindle-run.sh) only starts Xorg, not kfbterm,
# so Xorg has event3 open but not grabbed -- open is harmless, only EVIOCGRAB
# conflicts. Xorg is the reader's display server though: killing it would drop
# the framework, so it is never a candidate here; only kfbterm/kfb actually
# GRAB the device.
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

# --- absorb a game that is still dying ------------------------------------
# A game launched from this very session (or left over from a PC session) that
# was just killed is not yet out of the way: its EXIT trap saves progress and
# restores the reader, and the touch grab stays held until the process is
# actually gone. Starting into that window fails grabtest with EBUSY even
# though no game will be here a second later. So grabtest gets a bounded
# retry: fail once, make sure the known games are really gone, try again.
# (seen 2026-11-02: run-menu killed the PC's xcom and the new menu session's
# grabtest ran ~4s into the dying game's ~5s restore, failed once and exited.)
grab_free() {
    i=0
    while [ $i -lt 10 ]; do
        if /mnt/us/grabtest >> "$LOG" 2>&1; then
            return 0
        fi
        say "grabtest failed (attempt $((i + 1))); waiting out the dying game"
        for g in xcom rockman tetris chess sudoku calib pvz brogue-kindle-app menu; do
            p=$(pidof "$g" 2>/dev/null || true)
            [ -n "$p" ] && killall -q "$g" 2>/dev/null || true
        done
        i=$((i + 1))
        sleep 2
    done
    return 1
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

# --- diagnostics ----------------------------------------------------------
# The menu session is the only always-running process once the device boots,
# so it is the natural place to keep the crash breadcrumbs for everything it
# launches (KOReader, the games, the wifid helper). The Kindle's koreader
# crash.log goes silent on a *native* segfault (no Lua traceback), and its
# amazon kernel only marks `reset=user_reboot` without saying who rebooted;
# the five silent minutes between the last KOReader log line and the reboot
# that the user keeps hitting is exactly that: a segfault nobody recorded.
# The heartbeat pins the crash down to a moment: when KOReader segfaults the
# heartbeat keeps ticking (the launcher is alive) until the device watchdog
# reboots ~minutes later, so the gap between the last heartbeat and the
# koreader-launch.log "exited" timestamp is exactly the hang window. (A core
# dump would be ideal, but this kernel's hard core limit is 0 -- ulimit -c is
# a hard error here -- and crash.log is reader.lua's only record.)
say "booted $(date) -- pid=$$"
if [ -w /proc/sys/kernel/core_pattern ]; then
    echo /mnt/us/menu-core.%p > /proc/sys/kernel/core_pattern
    say "core_pattern now $(cat /proc/sys/kernel/core_pattern)"
fi
HB=/mnt/us/menu-heartbeat
[ -s "$HB" ] && mv -f "$HB" "$HB.prev"
: > "$HB"
while [ -e "$HB" ]; do
    echo "hb $$ $(date +%s)" >> "$HB"
    sleep 15
done &
_HB_PID=$!

# The session is leaving; the heartbeat was only for watching it. The loop
# above never exits on its own, so an EXIT trap is what stops the heartbeat
# on every exit path (KINDLE button, the two guard exits, an external TERM).
cleanup() {
    kill "$_HB_PID" 2>/dev/null
    rm -f "$HB"
    say "menu-boot script exiting $(date)"
}
trap cleanup EXIT

while :; do
    if ! stop_mirror; then
        say "cannot free the touch device; leaving the Kindle UI up"
        : > /mnt/us/.menu-stop
        exit 1
    fi
    if [ -x /mnt/us/grabtest ] && ! grab_free; then
        say "touch device still grabbed after 20s; reader stays up"
        for p in /proc/[0-9]*; do
            if ls -l "$p/fd" 2>/dev/null | grep -q event3; then
                say "  event3 is held by: $(cat "$p/cmdline" 2>/dev/null | tr '\0' ' ')"
            fi
        done
        : > /mnt/us/.menu-stop
        exit 1
    fi
    # The power key belongs to the menu while it shows (same rule as the game
    # runner: powerd answers a press with the screen saver, painting over the
    # panel). The runners of the games stop/restart powerd per game; this loop
    # covers the menu phases between them. restore_ui() starts it again.
    /sbin/initctl stop powerd >> "$LOG" 2>&1 || say "could not stop powerd for the menu"
    if [ -x /mnt/us/clear-boot-splash.sh ]; then
        say "unloading the boot splash layer (blanket splash module)"
        sh /mnt/us/clear-boot-splash.sh >> "$LOG" 2>&1 || \
            say "clear-boot-splash.sh rc=$? (splash module may already be gone)"
    fi
    /mnt/us/menu >> "$LOG" 2>&1
    rc=$?
    /sbin/initctl start powerd >> "$LOG" 2>&1 || say "could not restart powerd after the menu"
    case $rc in
        137|143)
            # 143: the menu saw .menu-handoff and refused to start the reader.
            # 137: the runner SIGKILLed the menu (an old menu binary cannot be
            # trusted with SIGTERM). Either way, do not restore and do not
            # draw the menu again on top of the game.
            if [ -f /mnt/us/.menu-handoff ]; then
                say "runner took the panel (menu rc=$rc); not restoring the reader"
                : > /mnt/us/.menu-stop
                exit "$rc"
            fi
            say "menu exited rc=$rc; showing it again" ;;
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
        6)  say "launching KOREADER at $(date)"
            # Framework is already stopped and the panel is ours; koreader.sh
            # runs without --framework_stop and leaves the UI dead on exit,
            # which is fine -- the launcher re-takes the panel next loop. Log
            # tail is our only record on the device. Markers go into the
            # append-only launch log too: a hard reboot wipes menu-boot.log
            # (now rotated to menu-boot.log.prev), but this file survives.
            echo "[[koreader.sh started at $(date)]]" >> /mnt/us/koreader-launch.log
            sh /mnt/us/koreader/koreader.sh >> /mnt/us/koreader-launch.log 2>&1
            rc=$?
            say "KOREADER exited rc=$rc at $(date)"
            echo "[[koreader.sh exited rc=$rc at $(date)]]" >> /mnt/us/koreader-launch.log ;;
        7)  say "WIFI setup"
            /mnt/us/chess --wifi-only --conf /mnt/us/chess.conf >> "$LOG" 2>&1 ;;
        *)  say "menu exited rc=$rc; showing it again" ;;
    esac
    say "back to the menu $(date)"
done
