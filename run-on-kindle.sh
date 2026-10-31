#!/bin/sh
# Copy the game to the Kindle, free the touch device, run it, put the device back.
#
#   ./run-on-kindle.sh              # the Kindle in kindle-tools/.kfb-secrets
#   ./run-on-kindle.sh 192.168.0.64 # ...or the other one
#
# The screen belongs to the game until you tap QUIT then SURE. Two things have
# to happen first, and tools/kindle-run.sh does both on the device:
#   * kill a leftover game (a killed ssh session leaves it holding the touch
#     grab, and the next run then gets EBUSY and dead touch),
#   * stop the screen mirror (kfbterm / kfb), which owns /dev/input/event3.
# Everything is put back afterwards.
#
# The game is run over a pty (-tt) on purpose: with no tty, ^C on this PC only
# drops the connection and leaves the game running forever with the panel. With
# a tty the device gets a hangup and the game closes the screen properly.
set -eu
cd "$(dirname "$0")"

KJ=jailbreak-kindle/kindle-tools

if [ ! -f ./xcom ]; then
    echo "run-on-kindle: no Kindle binary. Run: make kindle" >&2
    exit 1
fi
if [ ! -f ./grabtest ]; then
    echo "run-on-kindle: no grabtest. Run: make grabtest" >&2
    exit 1
fi
if [ ! -f tools/kindle-run.sh ]; then
    echo "run-on-kindle: tools/kindle-run.sh is missing" >&2
    exit 1
fi
if [ ! -d sound/words ] || [ ! -d sound/fx ]; then
    echo "run-on-kindle: no sound clips. Run: make sound" >&2
    exit 1
fi

# KINDLE_IP / KINDLE_KEY come from the shared secrets file when it exists.
# KFB_ENV_FILE is set explicitly because this script runs under /bin/sh, where
# env.sh cannot work out its own repo root (it looks for BASH_SOURCE).
if [ -f "$KJ/.kfb-secrets" ]; then
    KFB_ENV_FILE="$(pwd)/$KJ/.kfb-secrets"
    export KFB_ENV_FILE
    . "$KJ/env.sh"
else
    echo "run-on-kindle: no $KJ/.kfb-secrets; using built-in defaults" >&2
fi

ip=${1:-${KINDLE_IP:-192.168.0.63}}
key=${KINDLE_KEY:-$HOME/.ssh/kindle_touch_ed25519}

if [ ! -f "$key" ]; then
    echo "run-on-kindle: ssh key $key is missing" >&2
    exit 1
fi

ssh_run() {
    ssh -i "$key" -o BatchMode=yes -o IdentitiesOnly=yes \
        -o StrictHostKeyChecking=no -o UserKnownHostsFile="$HOME/.ssh/known_hosts" \
        -o LogLevel=ERROR -o ConnectTimeout=10 "root@$ip" "$@"
}

echo "run-on-kindle: pushing the game to $ip"
cat ./xcom         | ssh_run 'cat > /mnt/us/xcom && chmod +x /mnt/us/xcom'
cat ./grabtest     | ssh_run 'cat > /mnt/us/grabtest && chmod +x /mnt/us/grabtest'
cat tools/kindle-run.sh | ssh_run 'cat > /mnt/us/xcom-run.sh && chmod +x /mnt/us/xcom-run.sh'

# The speaker reads one file per word, so the whole folder goes over in one
# stream. It is replaced rather than merged: a renamed clip must not linger.
echo "run-on-kindle: pushing $(ls sound/words | wc -l) word clips and the effects"
tar -cf - -C sound words fx | ssh_run '
    rm -rf /mnt/us/xcom-sound
    mkdir -p /mnt/us/xcom-sound
    tar -xf - -C /mnt/us/xcom-sound'

echo "run-on-kindle: taking the $ip screen. Tap QUIT, then SURE, to give it back."
exec ssh -tt -i "$key" -o BatchMode=yes -o IdentitiesOnly=yes \
    -o StrictHostKeyChecking=no -o UserKnownHostsFile="$HOME/.ssh/known_hosts" \
    -o LogLevel=ERROR -o ConnectTimeout=10 "root@$ip" 'sh /mnt/us/xcom-run.sh'
