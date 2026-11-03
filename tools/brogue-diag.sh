#!/bin/sh
# brogue-diag.sh -- run Brogue on the Kindle with its diagnostics on, then read
# them back.
#
# The device has no terminal, so nothing can be asked of it while it runs.
# Instead it answers three questions on its own, and this script collects them:
#
#   1. did the panel open, and did the grey layer come up?
#   2. did the touch device open, grab, and with what axis range?
#   3. did a finger become an event, and did the event become a key/mouse cell?
#
#   ./tools/brogue-diag.sh                  run it and follow the log
#   ./tools/brogue-diag.sh 192.168.0.63     ...on another Kindle
#   ./tools/brogue-diag.sh --verbose        also log every DOWN/MOVE/UP + button
#   ./tools/brogue-diag.sh --grab           screenshot the game already running
#   ./tools/brogue-diag.sh --status         one line: is it alive, what has it seen
#
# Everything collected lands in ./diag/<timestamp>/ on the PC: the log, the
# status line, and (with --grab, or once at the end of a run) shot.png -- a
# picture of the panel as the game drew it, not as the PC preview draws it.
set -eu
cd "$(dirname "$0")/.."

KJ=jailbreak-kindle/kindle-tools
key=${KINDLE_KEY:-$HOME/.ssh/kindle_touch_ed25519}
ip=${KINDLE_IP:-192.168.0.63}
verbose=0
debug=0
mode=run

for arg in "$@"; do
    case "$arg" in
        --verbose|-v) verbose=1 ;;
        --grab)       mode=grab ;;
        --status)     mode=status ;;
        --debug)      debug=1 ;;
        -*)           echo "brogue-diag: unknown option $arg" >&2; exit 2 ;;
        *)            ip=$arg ;;
    esac
done

if [ ! -f "$key" ]; then
    echo "brogue-diag: ssh key $key is missing" >&2
    exit 1
fi

ssh_run() {
    ssh -i "$key" -o BatchMode=yes -o IdentitiesOnly=yes \
        -o StrictHostKeyChecking=no -o UserKnownHostsFile="$HOME/.ssh/known_hosts" \
        -o LogLevel=ERROR -o ConnectTimeout=10 "root@$ip" "$@"
}

if ! ssh_run true 2>/dev/null; then
    echo "brogue-diag: cannot reach $ip over ssh (is the Kindle awake and on wifi?)" >&2
    exit 1
fi

out=diag/$(date +%Y%m%d-%H%M%S)
mkdir -p "$out"

# The log lives on the device at /mnt/us/brogue-kindle-app.log; copy whatever is
# there into the PC-side run directory and print the part not seen yet.
seen_file=$out/.seen
: >"$seen_file"
pull_log() {
    ssh_run 'cat /mnt/us/brogue-kindle-app.log 2>/dev/null' >"$out/app.log" 2>/dev/null || true
    seen=$(cat "$seen_file" 2>/dev/null || echo 0)
    [ -n "$seen" ] || seen=0
    total=$(wc -l <"$out/app.log" | tr -d ' ')
    if [ "$total" -gt "$seen" ]; then
        sed -n "$((seen + 1)),\$p" "$out/app.log"
        echo "$total" >"$seen_file"
    fi
}

grab_screen() {
    pid=$(ssh_run 'pidof brogue-kindle-app 2>/dev/null' || true)
    if [ -z "$pid" ]; then
        echo "brogue-diag: the game is not running, so there is nothing to photograph"
        return 1
    fi
    ssh_run "kill -10 $pid" 2>/dev/null || true   # SIGUSR1: the game writes /tmp/brogue-shot.pgm
    sleep 2
    if ssh_run 'test -f /tmp/brogue-shot.pgm' 2>/dev/null; then
        ssh_run 'cat /tmp/brogue-shot.pgm' >"$out/shot.pgm"
        python3 tools/pgm2png.py "$out/shot.pgm" "$out/shot.png" >/dev/null
        echo "brogue-diag: the panel is in $out/shot.png"
    else
        echo "brogue-diag: the game did not answer SIGUSR1; no picture" >&2
        return 1
    fi
}

verdict() {
    echo
    echo "--- what the device says -------------------------------------------"
    for probe in \
        'touch grab OK|touch device: grabbed, so taps reach the game' \
        'is grabbed by someone else|touch device: TAKEN by another process -- taps go to it' \
        'ERROR: open /dev/input|touch device: cannot be opened at all' \
        'no memory for the grey layer|grey layer: MISSING, the picture will look speckled' \
        'waiting for the first tap|game reached its input loop and is listening' \
        'still here|heartbeat: alive, and it counts what it has seen' \
        'pause interrupted|a tap arrived mid-animation and ended it' \
        'NO BUTTON covers|a tap landed in the bar but between buttons'
    do
        pat=${probe%%|*}
        msg=${probe#*|}
        if grep -q "$pat" "$out/app.log" 2>/dev/null; then
            echo "  $msg"
        fi
    done
    # "Not at the input loop yet" has two very different causes, and they need
    # different fixes: an animation (the main menu flame loops on 50 ms pauses
    # and pushes its own frames) is healthy, while a process that stopped before
    # the panel opened is not. Say which one this is instead of blaming the
    # screen for both.
    if grep -q 'pausing:' "$out/app.log" 2>/dev/null && \
       ! grep -q 'waiting for the first tap' "$out/app.log" 2>/dev/null; then
        echo "  the game is animating (menu flame / item flash) and drawing -- not waiting for a tap yet"
    elif ! grep -q 'waiting for the first tap' "$out/app.log" 2>/dev/null; then
        echo "  the game never reached its input loop -- it failed before the screen was up"
    fi
    # A crash leaves only "Segmentation fault" in the log; the address is in the
    # kernel's ring buffer, and the debug build can turn it into a source line.
    if grep -q 'Segmentation fault' "$out/app.log" 2>/dev/null; then
        echo "  the game CRASHED (segmentation fault) -- see the address below"
        addr=$(ssh_run 'dmesg | grep -i segfault | tail -1' 2>/dev/null | sed -n 's/.*pc \(.*\)/\1/p' || true)
        if [ -n "$addr" ] && [ -f ./brogue-kindle-app-debug ]; then
            echo "  fault at $addr:"
            docker run --rm -v "$(pwd):/src" -w /src kfb-build \
                arm-linux-musleabi-addr2line -f -C -e brogue-kindle-app-debug "$addr" 2>/dev/null \
                | sed 's/^/    /' || true
        elif [ -n "$addr" ]; then
            echo "  fault at $addr (build brogue-kindle-app-debug to name the line)"
        else
            echo "  no fault address in dmesg; run with --debug to keep symbols"
        fi
    fi
    if grep -q 'DOWN ' "$out/app.log" 2>/dev/null; then
        echo "  a finger was seen (DOWN lines are present)"
    elif grep -q 'waiting for the first tap' "$out/app.log" 2>/dev/null; then
        echo "  no finger has been seen yet (no DOWN line): if you tapped, the events"
        echo "  are not reaching the game -- see the touch lines above"
    fi
    echo "  full log: $out/app.log"
    echo
}

case "$mode" in
status)
    ssh_run 'cat /tmp/brogue-status.txt 2>/dev/null; echo "pid $(pidof brogue-kindle-app 2>/dev/null)"'
    ;;

grab)
    grab_screen
    ;;

run)
    bin=./brogue-kindle-app
    if [ "$debug" = 1 ]; then
        bin=./brogue-kindle-app-debug
        if [ ! -f "$bin" ]; then
            echo "brogue-diag: no debug binary. Run: make brogue-debug" >&2
            exit 1
        fi
    fi
    if [ ! -f "$bin" ]; then
        echo "brogue-diag: no Kindle binary. Run: make brogue-kindle" >&2
        exit 1
    fi
    echo "brogue-diag: pushing $bin to $ip"
    cat "$bin" | ssh_run 'cat > /mnt/us/brogue-kindle-app.new && chmod +x /mnt/us/brogue-kindle-app.new && mv /mnt/us/brogue-kindle-app.new /mnt/us/brogue-kindle-app'
    cat tools/kindle-run.sh | ssh_run 'cat > /mnt/us/brogue-run.sh && chmod +x /mnt/us/brogue-run.sh'
    echo "brogue-diag: starting it with diagnostics on (log: $out/app.log)"
    ssh_run "GAME=brogue-kindle-app KFB_DIAG=$verbose sh /mnt/us/brogue-run.sh --detach" >/dev/null
    sleep 8
    echo
    echo "--- log so far -----------------------------------------------------"
    pull_log
    verdict
    grab_screen || true
    echo "brogue-diag: tap the screen now -- then run: ./tools/brogue-diag.sh --status"
    echo "             (or ./tools/brogue-diag.sh --grab to photograph the panel)"
    ;;
esac
