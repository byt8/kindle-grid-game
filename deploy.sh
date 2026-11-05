#!/bin/sh
# deploy.sh -- compile every game and put it on a Kindle you name.
#
#   ./deploy.sh                 # asks for the address
#   ./deploy.sh 192.168.0.63    # that Kindle
#   ./deploy.sh 192.168.0.63 --no-build    # push what is already compiled
#
# This is a thin front door to install-all-on-kindle.sh, which is the script
# that actually builds and pushes. Kept separate only so the address is asked
# for, in a loop until it answers, instead of failing on a stale default.
#
# What goes over: every game binary (xcom chess tetris sudoku mines rockman
# pvz calib brogue), their runners, grabtest, the menu session and its boot
# job, the fbdaemon set, chess.conf and its CA, the xcom sound clips, and
# KOReader unless --no-koreader is given.
set -eu
cd "$(dirname "$0")"

ALL=./install-all-on-kindle.sh
[ -x "$ALL" ] || { echo "deploy: $ALL is missing or not executable" >&2; exit 1; }

# Ask until the address is one the copy script can actually reach. The check
# is deliberately the same ssh call install-all-on-kindle.sh opens with, so a
# typo is caught here rather than two minutes into a build.
try() {
    ip=$1
    if ssh -i "${KINDLE_KEY:-$HOME/.ssh/kindle_touch_ed25519}" \
           -o BatchMode=yes -o IdentitiesOnly=yes -o StrictHostKeyChecking=no \
           -o UserKnownHostsFile="$HOME/.ssh/known_hosts" -o LogLevel=ERROR \
           -o ConnectTimeout=5 "root@$ip" 'test -d /mnt/us' 2>/dev/null; then
        return 0
    fi
    return 1
}

case ${1:-} in
    -h|--help)
        awk 'NR>1 && /^#/ { sub(/^# ?/, ""); print; next } NR>1 { exit }' "$0"
        exit 0 ;;
esac

# Only a first argument that is not an option is the address.
case ${1:-} in
    -*) ip= ;;
    *)  ip=${1:-}; [ $# -gt 0 ] && shift ;;
esac

if [ -n "$ip" ]; then
    try "$ip" || {
        echo "deploy: cannot reach a Kindle at $ip" >&2
        echo "        (awake? same network? ssh on? key at \${KINDLE_KEY:-~/.ssh/kindle_touch_ed25519})" >&2
        exit 1
    }
else
    if [ ! -t 0 ]; then
        echo "deploy: no address given and no terminal to ask on." >&2
        echo "        usage: $0 <kindle-ip> [--no-build] [--no-koreader]" >&2
        exit 1
    fi
    n=0
    while :; do
        printf 'Kindle IP address: '
        read -r ip || { echo "deploy: no address given" >&2; exit 1; }
        [ -n "$ip" ] || continue
        try "$ip" && break
        n=$((n + 1))
        echo "  no Kindle answering at $ip -- try again"
        # Three misses is enough: a wrong network is not a typo.
        [ $n -ge 3 ] && { echo "deploy: giving up on $ip" >&2; exit 1; }
    done
fi

echo "deploy: $ip is a Kindle; building and pushing everything"
exec "$ALL" "$ip" "$@"
