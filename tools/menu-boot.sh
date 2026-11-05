#!/bin/sh
# menu-boot.sh -- the always-on session, run on the Kindle.
#
# Started by the menu upstart job (tools/menu-boot.conf installed as
# /etc/upstart/menu.conf). There is no launcher screen. Boot opens the game
# named in /mnt/us/last-app (chess, the first time that file is missing).
# The game's APPS tab writes /mnt/us/switch-app and exits; this loop opens
# that game and saves it as last-app, so a restart comes back to it.
# QUIT writes /mnt/us/.menu-stop: the reader stays up until the next boot,
# and that boot opens last-app again.
#
# A game with no APPS tab (everything except chess and tetris) is a visit.
# Leaving it returns to /mnt/us/return-app, which is the game that opened it.
# The ids below must match the catalog in src/kframe.c.
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

# One token, lowercase letters only. Empty or unreadable is a failure.
read_token() {
    [ -f "$1" ] || return 1
    IFS= read -r line < "$1" || return 1
    printf '%s' "$line" | tr 'A-Z' 'a-z' | tr -cd 'a-z'
}

# Same ids as src/kframe.c. home apps have the in-game menu.
known_app() {
    case $1 in
        xcom|chess|tetris|sudoku|rockman|pvz|brogue|calib|koreader) return 0 ;;
        *) return 1 ;;
    esac
}
home_app() {
    case $1 in
        chess|tetris) return 0 ;;
        *) return 1 ;;
    esac
}
runner_for() {
    case $1 in
        xcom) echo /mnt/us/xcom-run.sh ;;
        chess) echo /mnt/us/chess-run.sh ;;
        tetris) echo /mnt/us/tetris-run.sh ;;
        sudoku) echo /mnt/us/sudoku-run.sh ;;
        rockman) echo /mnt/us/rockman-run.sh ;;
        pvz) echo /mnt/us/pvz-run.sh ;;
        brogue) echo /mnt/us/brogue-run.sh ;;
        calib) echo /mnt/us/calib-run.sh ;;
        koreader) echo /mnt/us/koreader/koreader.sh ;;
        *) return 1 ;;
    esac
}
bin_for() {
    case $1 in
        xcom) echo /mnt/us/xcom ;;
        chess) echo /mnt/us/chess ;;
        tetris) echo /mnt/us/tetris ;;
        sudoku) echo /mnt/us/sudoku ;;
        rockman) echo /mnt/us/rockman ;;
        pvz) echo /mnt/us/pvz ;;
        brogue) echo /mnt/us/brogue-kindle-app ;;
        calib) echo /mnt/us/calib ;;
        koreader) echo /mnt/us/koreader/koreader.sh ;;
        *) return 1 ;;
    esac
}
# 0 when this Kindle can actually start the app. A missing file is a no,
# said out loud by the caller. It is not a reason to open a different app.
launchable() {
    r=$(runner_for "$1") || return 1
    b=$(bin_for "$1") || return 1
    [ -f "$r" ] && [ -x "$b" ]
}

leave_to_reader() {
    say "$*"
    : > /mnt/us/.menu-stop
    /sbin/initctl start powerd >> "$LOG" 2>&1 || say "could not restart powerd for the reader"
    if [ -x /mnt/us/fbdaemon/restore-kindle.sh ]; then
        sh /mnt/us/fbdaemon/restore-kindle.sh >> "$LOG" 2>&1
    else
        say "no restore-kindle.sh; starting the framework"
        /sbin/initctl start framework >> "$LOG" 2>&1 || say "could not start framework"
    fi
    exit 0
}

save_last() {
    printf '%s\n' "$1" > /mnt/us/last-app
}

launch_app() {
    say "launching $1"
    case $1 in
        xcom) GAME=xcom sh /mnt/us/xcom-run.sh ;;
        chess) GAME=chess sh /mnt/us/chess-run.sh --conf /mnt/us/chess.conf ;;
        tetris) GAME=tetris sh /mnt/us/tetris-run.sh ;;
        sudoku) GAME=sudoku sh /mnt/us/sudoku-run.sh ;;
        rockman) GAME=rockman sh /mnt/us/rockman-run.sh ;;
        pvz) GAME=pvz sh /mnt/us/pvz-run.sh ;;
        brogue) GAME=brogue-kindle-app sh /mnt/us/brogue-run.sh ;;
        calib) GAME=calib sh /mnt/us/calib-run.sh ;;
        koreader)
            echo "[[koreader.sh started at $(date)]]" >> /mnt/us/koreader-launch.log
            sh /mnt/us/koreader/koreader.sh >> /mnt/us/koreader-launch.log 2>&1
            rc=$?
            say "KOREADER exited rc=$rc at $(date)"
            echo "[[koreader.sh exited rc=$rc at $(date)]]" >> /mnt/us/koreader-launch.log
            return $rc
            ;;
        *)
            say "no launch line for $1"
            return 2
            ;;
    esac
}

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
    # A runner that is handing the panel back wrote this and is, by
    # definition, still alive. If one is, leave it be. If none is, the stamp
    # is a leftover -- from a forced reboot, or from a runner killed before
    # its EXIT trap could delete it -- and honouring it would write
    # .menu-stop and leave the reader up on every boot, so the saved game
    # would never come back. Clear it and carry on in that case only.
    if [ -f /mnt/us/.menu-handoff ]; then
        if [ -n "$(pidof chess tetris xcom rockman sudoku pvz calib brogue-kindle-app 2>/dev/null || true)" ]; then
            say "runner took the panel; not starting an app over it"
            : > /mnt/us/.menu-stop
            exit 143
        fi
        say "clearing a stale .menu-handoff (no runner alive to hand the panel back)"
        rm -f /mnt/us/.menu-handoff
    fi

    if [ -f /mnt/us/switch-app ]; then
        next=$(read_token /mnt/us/switch-app) || next=
        rm -f /mnt/us/switch-app
        if ! known_app "$next"; then
            say "switch-app \"$next\" is not an app; keeping the saved one"
        elif ! launchable "$next"; then
            say "$next is not installed on this Kindle; keeping the saved one"
        else
            save_last "$next" || {
                say "could not save last-app"
                leave_to_reader "could not save the chosen app"
            }
            say "saved last-app $next"
        fi
    fi

    if [ ! -s /mnt/us/last-app ]; then
        say "no last-app; starting chess"
        save_last chess || {
            say "could not write last-app"
            leave_to_reader "could not write last-app"
        }
    fi
    app=$(read_token /mnt/us/last-app) || {
        say "could not read last-app"
        leave_to_reader "could not read last-app"
    }
    if ! known_app "$app"; then
        say "last-app \"$app\" is not an app"
        leave_to_reader "bad last-app; reader stays up"
    fi
    if ! launchable "$app"; then
        say "$app is not installed on this Kindle"
        leave_to_reader "$app is not on this Kindle; reader stays up"
    fi

    launch_app "$app" >> "$LOG" 2>&1
    rc=$?
    say "$app exited rc=$rc"

    if [ -f /mnt/us/switch-app ]; then
        say "switch requested"
        continue
    fi
    if [ -e /mnt/us/.menu-stop ]; then
        leave_to_reader "quit; reader until the next boot. $app comes back then"
    fi
    if ! home_app "$app" && [ "$rc" -eq 0 ]; then
        back=$(read_token /mnt/us/return-app) || back=
        rm -f /mnt/us/return-app
        if ! known_app "$back" || ! home_app "$back"; then
            say "$app ended and return-app \"$back\" is not a home app"
            leave_to_reader "nowhere to return; reader stays up"
        fi
        save_last "$back" || {
            say "could not save return to $back"
            leave_to_reader "could not save the return app"
        }
        say "$app ended; back to $back"
        continue
    fi
    if ! launchable "$app"; then
        say "$app is gone after exit rc=$rc"
        leave_to_reader "$app is not on this Kindle any more"
    fi
    say "$app exited rc=$rc; starting it again"
done
