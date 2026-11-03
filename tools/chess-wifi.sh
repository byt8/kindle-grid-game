#!/bin/sh
# chess-wifi.sh -- the Kindle's Wi-Fi, without the Kindle's UI.
#
# Lives on the device as /mnt/us/chess-wifi.sh and is called by the game (and by
# the boot job) when it has no network. Everything goes through wpa_cli against
# the wpa_supplicant that wifid already runs, so nothing here starts a second
# supplicant or fights wifid for the radio.
#
#   chess-wifi.sh status            # state=, ssid=, ip=
#   chess-wifi.sh scan              # SSID <tab> signal <tab> secure  (best first)
#   chess-wifi.sh join SSID [PSK]   # join, get an address, remember it
#   chess-wifi.sh auto              # join the remembered network, if any
#   chess-wifi.sh forget            # drop the remembered network
#   chess-wifi.sh diag              # tools, status, ifconfig, scan: for a join that fails
#
# Exit status is the whole story: 0 worked, non-zero printed the reason.
# The password is never stored in clear: only the wpa_passphrase hash is kept.
set -u

# Every path below is overridable so the join logic can be tested on this PC
# against a fake supplicant (make wifi-test). The defaults are the device's.
IF=wlan0
WPA=${WPA_CLI:-/usr/bin/wpa_cli}
CONF=${WIFI_CONF:-/mnt/us/wifi.conf}
SOCK=${WPA_SOCK:-/var/run/wpa_supplicant}
IFCONFIG=${IFCONFIG:-/sbin/ifconfig}
PASSPHRASE=${WPA_PASSPHRASE:-/usr/bin/wpa_passphrase}
PSK_MAX=63
# How long to wait for wpa_state=COMPLETED after select_network.
JOIN_WAIT=${WIFI_JOIN_WAIT:-30}

die() { echo "error: $*" >&2; exit 1; }
say() { echo "$*"; }

wpa() { $WPA -i $IF -p $SOCK "$@" 2>&1; }

# "wpa_state=COMPLETED" out of `wpa_cli status`
wpa_state() { wpa status | sed -n 's/^wpa_state=//p'; }
wpa_ssid()  { wpa status | sed -n 's/^ssid=//p'; }
iface_ip() {
    $IFCONFIG $IF 2>/dev/null | sed -n 's/.*inet addr:\([0-9.]*\).*/\1/p'
}

# busybox puts udhcpc in one of several places depending on the firmware.
# Missing entirely is said out loud by ensure_ip instead of being read as
# "the address never came".
find_udhcpc() {
    for p in ${UDHCPC:-} /sbin/udhcpc /usr/sbin/udhcpc /usr/bin/udhcpc /bin/udhcpc; do
        [ -n "$p" ] || continue
        [ -x "$p" ] && { echo "$p"; return 0; }
    done
    return 1
}

# A joined network is not usable until there is an address: the game cannot
# reach DeepSeek on a link-local lease.
ensure_ip() {
    [ -n "$(iface_ip)" ] && return 0
    say "getting an address"
    if d=$(find_udhcpc); then
        "$d" -i $IF -b -t 10 -T 3 -q >/dev/null 2>&1
    else
        say "no udhcpc on this Kindle; waiting for wifid's own DHCP"
    fi
    [ -n "$(iface_ip)" ] && return 0
    # wifid normally owns DHCP; give it a moment before calling this a failure.
    i=0
    while [ $i -lt 10 ]; do
        sleep 1
        [ -n "$(iface_ip)" ] && return 0
        i=$((i + 1))
    done
    return 1
}

cmd_status() {
    [ -x $WPA ] || die "no $WPA"
    st=$(wpa_state)
    say "state=${st:-unknown}"
    say "ssid=$(wpa_ssid)"
    say "ip=$(iface_ip)"
    [ "$st" = "COMPLETED" ]
}

cmd_scan() {
    [ -x $WPA ] || die "no $WPA"
    wpa scan >/dev/null
    # the scan takes a few seconds; busybox has no timeout, so poll
    i=0
    while [ $i -lt 15 ]; do
        sleep 1
        n=$(wpa scan_results | grep -c 'WPA\|ESS')
        [ "${n:-0}" -gt 1 ] && break
        i=$((i + 1))
    done
    # bssid freq signal flags ssid -> one line per SSID, strongest first.
    # The FS has to be a real tab: this busybox awk does not expand "\t" in -F,
    # which silently yields one field per line and an empty list.
    T=$(printf '\t')
    wpa scan_results \
      | awk -F"$T" 'NF >= 5 && $5 != "" && $5 !~ /%/ {
            ssid = $5; sig = $3; flags = $4;
            sec = (flags ~ /WPA/) ? "WPA2" : ((flags ~ /WEP/) ? "WEP" : "open");
            if (!(ssid in best) || sig > best[ssid]) { best[ssid] = sig; key[ssid] = sec }
        }
        END { for (s in best) printf "%s\t%d\t%s\n", s, best[s], key[s] }' \
      | sort -t"$T" -k2,2nr
}

# join SSID [PSK] -- returns 0 only when the link is up AND has an address.
cmd_join() {
    ssid=${1:-}
    psk=${2:-}
    [ -n "$ssid" ] || die "join needs an ssid"
    [ -x $WPA ] || die "no $WPA"
    # wpa_supplicant refuses a key shorter than 8 characters, and the way it
    # says so is useless on the game's screen: catch it here, in plain words.
    if [ -n "$psk" ] && [ ${#psk} -ne 64 ] && [ ${#psk} -lt 8 ]; then
        die "the password must be at least 8 characters"
    fi

    # Already there? Then this is a no-op, not a reconnect.
    if [ "$(wpa_state)" = "COMPLETED" ] && [ "$(wpa_ssid)" = "$ssid" ]; then
        ensure_ip || die "joined $ssid but got no address"
        persist "$ssid" "$psk"
        say "ok: already on $ssid"
        return 0
    fi

    # A network block is set from scratch, every time. wpa_supplicant keeps
    # every value ever set on a network id, so reusing the saved block for this
    # ssid carried its old security settings into the new attempt -- a block
    # first tried as an open network kept key_mgmt=NONE, and the WPA2 access
    # point then refused it however right the password was. That is the
    # "typed the password, still connection failed" case. Only this ssid's
    # blocks are removed: other saved networks are left alone.
    net_clear "$ssid"
    id=$(wpa add_network | sed -n 's/^\([0-9][0-9]*\)$/\1/p' | tail -1)
    [ -n "$id" ] || die "could not add a network"
    wpa set_network $id ssid "\"$ssid\"" >/dev/null || die "could not set the network name"
    if [ -n "$psk" ]; then
        # key_mgmt is written explicitly: left unset it is whatever the block
        # already had, which is what broke every retry.
        wpa set_network $id key_mgmt WPA-PSK >/dev/null || die "could not set WPA-PSK"
        if [ ${#psk} -eq 64 ]; then
            wpa set_network $id psk $psk >/dev/null || die "could not set the key"
        else
            wpa set_network $id psk "\"$psk\"" >/dev/null || die "could not set the key"
        fi
    else
        wpa set_network $id key_mgmt NONE >/dev/null || die "could not set an open network"
    fi
    wpa enable_network $id >/dev/null
    wpa select_network $id >/dev/null || die "could not select the network"

    say "joining $ssid"
    joined=0
    i=0
    while [ $i -lt $JOIN_WAIT ]; do
        sleep 1
        # COMPLETED on its own is not enough: for a second after select_network
        # the supplicant still reports the network it is about to leave, which
        # would make every join look successful.
        if [ "$(wpa_state)" = "COMPLETED" ] && [ "$(wpa_ssid)" = "$ssid" ]; then
            joined=1
            break
        fi
        i=$((i + 1))
    done
    if [ $joined -ne 1 ]; then
        reenable_others "$ssid"
        die "$(join_reason "$ssid")"
    fi

    ensure_ip || die "joined $ssid but got no address"
    persist "$ssid" "$psk"
    say "ok: on $ssid, ip $(iface_ip)"
}

# Remove every saved block for this ssid (normally zero or one).
net_clear() {
    wpa list_networks \
      | awk -F"$(printf '\t')" -v s="$1" 'NR > 1 && $2 == s { print $1 }' \
      | while read -r n; do
            [ -n "$n" ] && wpa remove_network "$n" >/dev/null 2>&1
        done
}

# select_network turns every other network off for this session. A failed join
# must not leave the unit with nothing left to fall back to.
reenable_others() {
    wpa list_networks \
      | awk -F"$(printf '\t')" -v s="$1" 'NR > 1 && $2 != s { print $1 }' \
      | while read -r n; do
            [ -n "$n" ] && wpa enable_network "$n" >/dev/null 2>&1
        done
}

# Why the join stopped, in words that fit on a 600x800 panel. wpa_state names
# the stage: 4WAY_HANDSHAKE means the access point refused the password,
# SCANNING/DISCONNECTED means it was never found.
join_reason() {
    st=$(wpa_state)
    case $st in
        4WAY_HANDSHAKE)
            echo "the password was refused by $1" ;;
        ASSOCIATING|ASSOCIATED|AUTHENTICATING)
            echo "$1 let us in but the password check failed (security type?)" ;;
        SCANNING|INACTIVE|DISCONNECTED|"")
            echo "could not find $1 - out of range?" ;;
        *)
            echo "could not join $1 (wpa_state=$st)" ;;
    esac
}

# Remember the network so a booted gift rejoins by itself. Only the hash is
# kept: same form wpa_supplicant itself uses, so it can be handed straight back.
persist() {
    ssid=$1
    psk=${2:-}
    echo "# written by chess-wifi.sh; ssid on one line, psk hash on the next" > $CONF
    printf 'ssid=%s\n' "$ssid" >> $CONF
    if [ -n "$psk" ]; then
        if [ ${#psk} -eq 64 ]; then
            printf 'psk=%s\n' "$psk" >> $CONF
        else
            hash=$($PASSPHRASE "$ssid" "$psk" | sed -n 's/^[[:space:]]*psk=//p')
            [ -n "$hash" ] || die "could not hash the password"
            printf 'psk=%s\n' "$hash" >> $CONF
        fi
    fi
    chmod 600 $CONF
}

cmd_auto() {
    [ "$(wpa_state)" = "COMPLETED" ] && { ensure_ip && exit 0; }
    [ -f $CONF ] || die "no remembered network"
    s=$(sed -n 's/^ssid=//p' $CONF | head -1)
    p=$(sed -n 's/^psk=//p'  $CONF | head -1)
    [ -n "$s" ] || die "$CONF has no ssid"
    cmd_join "$s" "$p"
}

cmd_forget() {
    rm -f $CONF
    say "forgot the remembered network"
}

# One command to paste back when a join will not work. The password is never
# printed: only the ssid of the remembered network.
cmd_diag() {
    say "-- tools --"
    if [ -x $WPA ]; then say "wpa_cli: $WPA (present)"; else say "wpa_cli: $WPA (MISSING)"; fi
    if [ -d $SOCK ]; then say "supplicant socket dir: $SOCK (present)"; else say "supplicant socket: $SOCK (MISSING)"; fi
    if d=$(find_udhcpc); then say "udhcpc: $d"; else say "udhcpc: not found (wifid does the DHCP)"; fi
    say "-- status --"
    wpa status
    say "-- interface --"
    $IFCONFIG $IF 2>&1
    say "-- remembered network --"
    if [ -f $CONF ]; then
        sed -n 's/^ssid=/ssid=/p' $CONF
    else
        say "no $CONF"
    fi
    say "-- saved networks --"
    wpa list_networks
    say "-- scan --"
    cmd_scan
}

[ $# -ge 1 ] || die "usage: chess-wifi.sh status|scan|join SSID [PSK]|auto|forget|diag"
c=$1; shift
case $c in
    status) cmd_status ;;
    scan)   cmd_scan ;;
    join)   cmd_join "$@" ;;
    auto)   cmd_auto ;;
    forget) cmd_forget ;;
    diag)   cmd_diag ;;
    *)      die "unknown command '$c'" ;;
esac
