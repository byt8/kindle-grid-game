#!/bin/sh
# sleep-switch.sh -- disable (or re-enable) auto sleep on the Kindle, for
# connection debugging. Same key/args as the run-*-on-kindle.sh scripts.
#
#   ./tools/sleep-switch.sh            # disable sleep (preventScreenSaver=1)
#   ./tools/sleep-switch.sh on         # same
#   ./tools/sleep-switch.sh off        # re-enable sleep (preventScreenSaver=0)
#   ./tools/sleep-switch.sh status     # show the current powerd state
#
# What it does: the stock `~ds` search-bar trick is exactly
#   lipc-set-prop com.lab126.powerd preventScreenSaver 1
# (see /usr/bin/ds.sh on the device). While set, powerd never leaves the
# Active state, so the Kindle never enters screen-saver -> suspend and the
# wifi/ssh link stays up. It resets on reboot -- run this again (or a game
# runner) after one.
set -eu
cd "$(dirname "$0")/.."

ip=${KINDLE_IP:-192.168.0.66}
key=${KINDLE_KEY:-$HOME/.ssh/kindle_touch_ed25519}

if [ ! -f "$key" ]; then
    echo "sleep-switch: ssh key $key is missing" >&2
    exit 1
fi

ssh_run() {
    ssh -i "$key" -o BatchMode=yes -o IdentitiesOnly=yes \
        -o StrictHostKeyChecking=no -o UserKnownHostsFile="$HOME/.ssh/known_hosts" \
        -o LogLevel=ERROR -o ConnectTimeout=10 "root@$ip" "$@"
}

cmd=${1:-on}
case "$cmd" in
    on|disable)
        ssh_run 'lipc-set-prop com.lab126.powerd preventScreenSaver 1'
        echo "sleep-switch: sleep disabled on $ip (preventScreenSaver=1)"
        ;;
    off|enable)
        ssh_run 'lipc-set-prop com.lab126.powerd preventScreenSaver 0'
        echo "sleep-switch: sleep re-enabled on $ip (preventScreenSaver=0)"
        ;;
    status)
        ssh_run 'lipc-get-prop com.lab126.powerd preventScreenSaver; echo; lipc-get-prop com.lab126.powerd status'
        ;;
    *)
        echo "sleep-switch: usage: $0 [on|off|status]" >&2
        exit 1
        ;;
esac