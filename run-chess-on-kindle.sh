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
exec ssh -tt -i "$key" -o BatchMode=yes -o IdentitiesOnly=yes \
    -o StrictHostKeyChecking=no -o UserKnownHostsFile="$HOME/.ssh/known_hosts" \
    -o LogLevel=ERROR -o ConnectTimeout=10 "root@$ip" \
    "GAME=chess sh /mnt/us/chess-run.sh --conf /mnt/us/chess.conf $*"
