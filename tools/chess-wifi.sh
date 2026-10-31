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
#
# Exit status is the whole story: 0 worked, non-zero printed the reason.
# The password is never stored in clear: only the wpa_passphrase hash is kept.
set -u

IF=wlan0
WPA=/usr/bin/wpa_cli
CONF=/mnt/us/wifi.conf
SOCK=/var/run/wpa_supplicant
PSK_MAX=63

die() { echo "error: $*" >&2; exit 1; }
say() { echo "$*"; }

wpa() { $WPA -i $IF -p $SOCK "$@" 2>&1; }

# "wpa_state=COMPLETED" out of `wpa_cli status`
wpa_state() { wpa status | sed -n 's/^wpa_state=//p'; }
wpa_ssid()  { wpa status | sed -n 's/^ssid=//p'; }
iface_ip() {
    /sbin/ifconfig $IF 2>/dev/null | sed -n 's/.*inet addr:\([0-9.]*\).*/\1/p'
}

# A joined network is not usable until there is an address: the game cannot
# reach DeepSeek on a link-local lease.
ensure_ip() {
    [ -n "$(iface_ip)" ] && return 0
    say "getting an address"
    /sbin/udhcpc -i $IF -b -t 10 -T 3 -q >/dev/null 2>&1
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

    # Reuse the profile with this ssid if one is already saved, so repeated
    # joins do not pile up networks in the supplicant.
    id=$(wpa list_networks | awk -F"$(printf '\t')" -v s="$ssid" '$2 == s { print $1; exit }')
    if [ -z "$id" ]; then
        id=$(wpa add_network | sed -n 's/^\([0-9][0-9]*\)$/\1/p' | tail -1)
        [ -n "$id" ] || die "could not add a network"
    fi
    wpa set_network $id ssid "\"$ssid\"" >/dev/null
    if [ -n "$psk" ]; then
        if [ ${#psk} -eq 64 ]; then
            wpa set_network $id psk $psk >/dev/null || die "could not set the key"
        else
            wpa set_network $id psk "\"$psk\"" >/dev/null || die "could not set the key"
        fi
    else
        wpa set_network $id key_mgmt NONE >/dev/null
    fi
    wpa enable_network $id >/dev/null
    wpa select_network $id >/dev/null || die "could not select the network"

    say "joining $ssid"
    i=0
    while [ $i -lt 30 ]; do
        sleep 1
        st=$(wpa_state)
        # COMPLETED on its own is not enough: for a second after select_network
        # the supplicant still reports the network it is about to leave, which
        # would make every join look successful.
        [ "$st" = "COMPLETED" ] && [ "$(wpa_ssid)" = "$ssid" ] && break
        i=$((i + 1))
    done
    [ "$(wpa_state)" = "COMPLETED" ] && [ "$(wpa_ssid)" = "$ssid" ] \
        || die "could not join $ssid (out of range, or the password was refused)"

    ensure_ip || die "joined $ssid but got no address"
    persist "$ssid" "$psk"
    say "ok: on $ssid, ip $(iface_ip)"
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
            hash=$(/usr/bin/wpa_passphrase "$ssid" "$psk" | sed -n 's/^[[:space:]]*psk=//p')
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

[ $# -ge 1 ] || die "usage: chess-wifi.sh status|scan|join SSID [PSK]|auto|forget"
c=$1; shift
case $c in
    status) cmd_status ;;
    scan)   cmd_scan ;;
    join)   cmd_join "$@" ;;
    auto)   cmd_auto ;;
    forget) cmd_forget ;;
    *)      die "unknown command '$c'" ;;
esac
