#!/bin/sh
# peer-wifi.sh -- put the Kindle's wlan0 on an ad-hoc (IBSS) peer link, no router.
#
# Lives on the device as /mnt/us/peer-wifi.sh, called by the smoke-test runner.
# Two Kindles each run `up`; the first to get there creates the IBSS cell, the
# second joins it.  The radio stays on the same channel, both get a static
# 10.30.0.x address (ad-hoc nets do not do DHCP), and they can reach each other
# directly with no access point in the path.
#
#   peer-wifi.sh up     # add an IBSS network, bring the link up
#   peer-wifi.sh down   # back to the remembered wifi network + DHCP
#   peer-wifi.sh status # wpa_state, own ip, link quality
#
# Design notes (all verified on the device, 2026):
#  - The supplicant is wifid's own wpa_supplicant, reached through wpa_cli
#    exactly like chess-wifi.sh does.  chess-wifi.sh proves you can switch this
#    supplicant to a different network while wifid runs, so we never touch
#    wifid: killing it risks taking the supplicant with it.
#  - The AR6003 driver/firmware supports ad-hoc ("Network: Adhoc (Creator)",
#    "Network: Adhoc (Joiner)" in the module), controlled over WEXT, so the
#    network mode is set as a supplicant network property (mode=1) + freq.
#  - No DHCP: IBSS has no server.  Each unit derives a static 10.30.0.x IP
#    from its own MAC, so both sides agree without a config file.
#  - `down` rejoins the remembered network through chess-wifi.sh auto.
set -u

IF=wlan0
WPA=/usr/bin/wpa_cli
SOCK=/var/run/wpa_supplicant
SSID_PREFIX="P2P-KINDLE"
CHANNEL=6            # 2437 MHz, middle of the band

die() { echo "peer-wifi: error: $*" >&2; exit 1; }
say() { echo "peer-wifi: $*"; }

wpa() { $WPA -i $IF -p $SOCK "$@" 2>&1; }

iface_ip() {
    /sbin/ifconfig $IF 2>/dev/null | sed -n 's/.*inet addr:\([0-9.]*\).*/\1/p'
}
iface_mac() {
    /sbin/ifconfig $IF 2>/dev/null | sed -n 's/.*HWaddr \([0-9A-Fa-f:]*\).*/\1/p'
}
wpa_state() { wpa status | sed -n 's/^wpa_state=//p'; }

# last two hex digits of the MAC -> a unique 10.30.0.x octet in 0..255.
# e.g. MAC ...:CD -> .205.  Two units colliding is effectively impossible.
peer_ip() {
    mac=$(iface_mac)
    [ -n "$mac" ] || die "no MAC for $IF"
    tail=$(printf '%s\n' "$mac" | awk -F: '{print $NF}')
    [ -n "$tail" ] || die "could not read MAC tail"
    # busybox ash on this firmware has no $((16#..)) radix syntax;
    # printf %d understands 0x.. instead.
    n=$(printf '%d' "0x${tail}") || die "bad MAC tail '$tail'"
    echo "10.30.0.$n"
}

peer_freq() { echo $((2412 + (CHANNEL - 1) * 5)); }

# Remove a previously configured peer network (by SSID prefix), if any.
clear_peer_net() {
    p="$SSID_PREFIX"
    # busybox awk: no -E; the RHS is a concatenated string, which awk treats
    # as a regex when used with ~
    old=$(wpa list_networks | awk -F"$(printf '\t')" -v p="$p" '$2 ~ "^" p {print $1; exit}')
    [ -z "$old" ] && return 0
    wpa remove_network "$old" >/dev/null
    say "removed stale peer network $old"
}

cmd_up() {
    [ -x $WPA ] || die "no $WPA"

    clear_peer_net

    ssid="${SSID_PREFIX}-$(peer_ip | awk -F. '{print $4}')"
    ip=$(peer_ip)
    freq=$(peer_freq)

    id=$(wpa add_network | sed -n 's/^\([0-9][0-9]*\)$/\1/p' | tail -1)
    [ -n "$id" ] || die "could not add a network"

    # mode=1 is IBSS (ad-hoc).  freq is the channel in MHz.  key_mgmt NONE =
    # open 802.11; app-level crypto comes in a later milestone.
    wpa set_network $id ssid "\"$ssid\"" >/dev/null
    wpa set_network $id mode 1 >/dev/null
    wpa set_network $id freq "$freq" >/dev/null
    wpa set_network $id key_mgmt NONE >/dev/null

    wpa enable_network $id >/dev/null
    wpa select_network $id >/dev/null

    say "joining ad-hoc $ssid (channel $CHANNEL, $freq MHz)..."
    i=0
    while [ $i -lt 30 ]; do
        sleep 1
        st=$(wpa_state)
        # Same guard chess-wifi.sh uses: right after select_network the
        # supplicant still reports the network it is leaving.
        [ "$st" = "COMPLETED" ] && break
        i=$((i + 1))
    done
    [ "$st" = "COMPLETED" ] || die "link did not come up (state ${st:-unknown})"

    /sbin/ifconfig $IF $ip netmask 255.255.255.0 up
    [ "$(iface_ip)" = "$ip" ] || die "could not set $ip on $IF"
    say "up: ssid=$ssid ip=$ip freq=${freq}MHz"
}

cmd_down() {
    clear_peer_net
    /sbin/ifconfig $IF 0.0.0.0 2>/dev/null
    if [ -x /mnt/us/chess-wifi.sh ]; then
        say "rejoining remembered wifi"
        sh /mnt/us/chess-wifi.sh auto >/dev/null 2>&1 || true
    fi
    say "down: wifi restored"
}

cmd_status() {
    st=$(wpa_state)
    say "state=${st:-unknown}"
    say "ip=$(iface_ip)"
    say "quality: $(sed -n '3p' /proc/net/wireless | tr -s ' ')"
}

[ $# -ge 1 ] || die "usage: peer-wifi.sh up|down|status"
case $1 in
    up)   cmd_up ;;
    down) cmd_down ;;
    status) cmd_status ;;
    *)    die "unknown command '$1'" ;;
esac