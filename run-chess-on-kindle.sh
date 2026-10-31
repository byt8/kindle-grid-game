#!/bin/sh
# Push the chess game to the Kindle, take the screen, run it, put the device back.
#
#   ./run-chess-on-kindle.sh              # the Kindle at $KINDLE_IP
#   ./run-chess-on-kindle.sh 192.168.0.64 # ...or another one
#
# Any argument after the address goes to the game, so ./run-chess-on-kindle.sh \
#   192.168.0.66 --apicheck   asks the API one question and exits.
#
# The screen belongs to the game until you tap QUIT then SURE. tools/kindle-run.sh
# does the device-side work: kill a leftover game, stop the screen mirror that
# holds /dev/input/event3, prove the touch device is free, and restore the Kindle
# UI afterwards. It is pushed here as /mnt/us/chess-run.sh with GAME=chess.
set -eu
cd "$(dirname "$0")"

KJ=jailbreak-kindle/kindle-tools

if [ ! -f ./chess ]; then
    echo "run-chess-on-kindle: no Kindle binary. Run: make chess-kindle" >&2
    exit 1
fi
if [ ! -f ./grabtest ]; then
    echo "run-chess-on-kindle: no grabtest. Run: make grabtest" >&2
    exit 1
fi
if [ ! -f ./chess.conf ]; then
    echo "run-chess-on-kindle: no chess.conf. Copy chess.conf.example and put" >&2
    echo "                      the API key in it." >&2
    exit 1
fi

if [ -f "$KJ/.kfb-secrets" ]; then
    KFB_ENV_FILE="$(pwd)/$KJ/.kfb-secrets"
    export KFB_ENV_FILE
    . "$KJ/env.sh"
else
    echo "run-chess-on-kindle: no $KJ/.kfb-secrets; using built-in defaults" >&2
fi

ip=${1:-${KINDLE_IP:-192.168.0.66}}
[ $# -gt 0 ] && shift
key=${KINDLE_KEY:-$HOME/.ssh/kindle_touch_ed25519}

if [ ! -f "$key" ]; then
    echo "run-chess-on-kindle: ssh key $key is missing" >&2
    exit 1
fi

ssh_run() {
    ssh -i "$key" -o BatchMode=yes -o IdentitiesOnly=yes \
        -o StrictHostKeyChecking=no -o UserKnownHostsFile="$HOME/.ssh/known_hosts" \
        -o LogLevel=ERROR -o ConnectTimeout=10 "root@$ip" "$@"
}

echo "run-chess-on-kindle: pushing the game to $ip"
# Write beside it and rename: a copy over a running binary is "Text file busy".
cat ./chess                     | ssh_run 'cat > /mnt/us/chess.new && chmod +x /mnt/us/chess.new && mv /mnt/us/chess.new /mnt/us/chess'
cat ./grabtest                  | ssh_run 'cat > /mnt/us/grabtest && chmod +x /mnt/us/grabtest'
cat tools/kindle-run.sh         | ssh_run 'cat > /mnt/us/chess-run.sh && chmod +x /mnt/us/chess-run.sh'
cat ./chess.conf                | ssh_run 'cat > /mnt/us/chess.conf && chmod 600 /mnt/us/chess.conf'
# The wi-fi helper the game shells out to, and the boot job the unit can be
# started from (see the README: /etc/upstart/chess.conf).
cat tools/chess-wifi.sh          | ssh_run 'cat > /mnt/us/chess-wifi.sh && chmod +x /mnt/us/chess-wifi.sh'
cat tools/chess-boot.conf        | ssh_run 'cat > /mnt/us/chess-boot.conf'

# The Kindle's own trust store predates DeepSeek's certificate, so hand it this
# PC's current roots. Override with CHESS_CA=/path/to/bundle.crt if you like.
ca=${CHESS_CA:-/etc/ssl/certs/ca-certificates.crt}
if [ -f "$ca" ]; then
    echo "run-chess-on-kindle: pushing CA bundle $ca"
    cat "$ca" | ssh_run 'cat > /mnt/us/chess-ca.pem && chmod 644 /mnt/us/chess-ca.pem'
else
    echo "run-chess-on-kindle: no CA bundle at $ca; the device will have to" >&2
    echo "                      trust DeepSeek on its own old certificates." >&2
fi

echo "run-chess-on-kindle: taking the $ip screen. Tap QUIT, then SURE, to give it back."
# --detach: from here the game belongs to the device. It used to be a child of
# a `ssh -tt`, so when the Kindle slept and the link dropped, sshd hung the
# session up, the game died of SIGHUP and the reader was left frozen on its
# last board with its own UI shut down -- "my turn but I cannot move". Now this
# script only follows the game's log, and closing it changes nothing.
ssh_run "GAME=chess sh /mnt/us/chess-run.sh --detach --conf /mnt/us/chess.conf $*"
echo "run-chess-on-kindle: the game is running on $ip; this window only watches."
seen=0
while :; do
    sleep 10
    snap=$(ssh_run 'echo "PID $(pidof chess 2>/dev/null)"; echo "LOG"; cat /mnt/us/chess.log 2>/dev/null' 2>/dev/null || true)
    if [ -z "$snap" ]; then
        echo "run-chess-on-kindle: cannot reach $ip; the game keeps running without us"
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
        echo "run-chess-on-kindle: the game has exited; the Kindle reader is back"
        exit 0
    fi
done
