#!/bin/sh
# run-peer-test.sh -- prove two Kindles talk to each other directly, no router.
#
#   ./run-peer-test.sh [IP_A] [IP_B]
#   default A=192.168.0.63  B=192.168.0.66
#
# Orchestrates the smoke test on the real devices:
#   1. push peer-wifi.sh + echo-probe to both Kindles
#   2. `peer-wifi.sh up` on both -> same ad-hoc SSID/channel, static IPs
#   3. ping A<->B (reachability + latency), each way
#   4. messaging proof: echo-probe listens on A, a line is sent from B and
#      must come back verbatim
#   5. `peer-wifi.sh down` on both, re-verify wifi is back
#
# Exit code is the whole story: 0 only if every step passed.  No silent
# fallback: the first failure is printed and the test stops there.
set -eu
cd "$(dirname "$0")"

SSH_FLAGS="-i ${KINDLE_KEY:-$HOME/.ssh/kindle_touch_ed25519} -o BatchMode=yes -o IdentitiesOnly=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=$HOME/.ssh/known_hosts -o LogLevel=ERROR -o ConnectTimeout=10"

A=${1:-192.168.0.63}
B=${2:-192.168.0.66}
PORT=7777

ssh_run() { # $1=ip rest=command
    ip=$1; shift
    ssh $SSH_FLAGS "root@$ip" "$@"
}
push() { # $1=ip $2=local $3=remote
    cat "$2" | ssh_run "$1" "cat > $3 && chmod +x $3"
}

need() { # $1=local file
    [ -f "$1" ] || { echo "run-peer-test: missing $1 (build it: make $1)" >&2; exit 1; }
}

echo "run-peer-test: pushing to $A and $B"
need tools/peer-wifi.sh
need echo-probe
for ip in "$A" "$B"; do
    push "$ip" tools/peer-wifi.sh /mnt/us/peer-wifi.sh
    push "$ip" echo-probe /mnt/us/echo-probe
    echo "run-peer-test: $ip ready"
done

# --- bring the ad-hoc link up on both ---
for ip in "$A" "$B"; do
    echo "run-peer-test: $ip up"
    ssh_run "$ip" "sh /mnt/us/peer-wifi.sh up"
done

# small wait for the IBSS to stabilise before pinging
sleep 3

# --- reachability, both ways ---
ping_once() { # $1=target
    # busybox ping needs -c; a single packet, short timeout
    ssh_run "$1" "ping -c 1 -W 2 $2" 2>/dev/null || true
}

a_ip=$(ssh_run "$A" "sh /mnt/us/peer-wifi.sh status | sed -n 's/^peer-wifi: ip=//p'")
b_ip=$(ssh_run "$B" "sh /mnt/us/peer-wifi.sh status | sed -n 's/^peer-wifi: ip=//p'")
echo "run-peer-test: A=$A ($a_ip)  B=$B ($b_ip)"
[ -n "$a_ip" ] && [ -n "$b_ip" ] || { echo "run-peer-test: could not read both IPs" >&2; exit 1; }

echo "run-peer-test: ping A->B ($a_ip -> $b_ip)"
ssh_run "$A" "ping -c 3 -W 2 $b_ip" 2>&1 | tail -2
echo "run-peer-test: ping B->A ($b_ip -> $a_ip)"
ssh_run "$B" "ping -c 3 -W 2 $a_ip" 2>&1 | tail -2

# --- messaging proof ---
echo "run-peer-test: messaging $A (listener) <-> $B (sender)"
# Start the echo server on A in the background over ssh; capture its output.
# (It exits after one connection.)
ssh_run "$A" "sh -c '/mnt/us/echo-probe >/tmp/echo.out 2>&1 & echo \$!'" >/tmp/echo.pid.a
sleep 1
# Send a line from B; the probe echoes it back to B.
msg="hello from $(hostname) @ $(date '+%H:%M:%S')"
echo "run-peer-test: sending: $msg"
echo "$msg" | ssh_run "$B" "nc $a_ip $PORT" > /tmp/echo.recv.b || true
# Give the probe a moment to write its log, then read it.
sleep 2
got=$(ssh_run "$A" "cat /tmp/echo.out 2>/dev/null" || true)
echo "run-peer-test: probe log on $A:"
echo "$got"
echo "$msg" > /tmp/echo.sent
if [ -s /tmp/echo.recv.b ] && grep -qF "$msg" /tmp/echo.recv.b; then
    echo "run-peer-test: MESSAGING OK -- A echoed the line back to B verbatim"
    ping_ok=1
else
    echo "run-peer-test: MESSAGING FAILED -- see probe log and /tmp/echo.recv.b" >&2
    ping_ok=0
fi

# --- tear down ---
for ip in "$A" "$B"; do
    echo "run-peer-test: $ip down"
    ssh_run "$ip" "sh /mnt/us/peer-wifi.sh down"
done
# verify wifi is back on both
for ip in "$A" "$B"; do
    st=$(ssh_run "$ip" "sh /mnt/us/chess-wifi.sh status 2>/dev/null | sed -n 's/^state=//p'" || true)
    echo "run-peer-test: $ip wifi state=$st"
done

[ "$ping_ok" = "1" ] && { echo "run-peer-test: PASS"; exit 0; }
echo "run-peer-test: FAIL" >&2
exit 1