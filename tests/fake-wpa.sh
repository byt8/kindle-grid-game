#!/bin/sh
# fake-wpa.sh -- a stand-in for wpa_cli so chess-wifi.sh's join runs on this PC.
#
# It is not a radio simulator. It reproduces the one behaviour that broke a real
# join: a network block keeps every value ever set on it, and a WPA2 access
# point refuses a block whose key_mgmt is not WPA-PSK.
#
# State lives in $FAKE_STATE: blocks/<id>/<key> (one file per set value),
# blocks/<id>/ssid, selected, status, log.
set -u
ST=${FAKE_STATE:?FAKE_STATE is not set}

AP_SSID=${FAKE_AP_SSID:-PRNet84}
AP_PSK=${FAKE_AP_PSK:-Alotbsol1}

# the real wpa_cli is called as `wpa_cli -i wlan0 -p /var/run/wpa_supplicant cmd`
# and answers the command on stdout: drop those options and keep the command.
while [ $# -gt 0 ]; do
    case $1 in
        -i|-p) shift 2 ;;
        -*) shift ;;
        *) break ;;
    esac
done
set -- "$@"

log() { echo "$*" >> "$ST/log"; }
set_state() { printf 'wpa_state=%s\n' "$1" > "$ST/status"; }

psk_ok() {
    ssid=$1; psk=$2
    psk=${psk#\"}; psk=${psk%\"}
    [ "$ssid" = "$AP_SSID" ] && [ "$psk" = "$AP_PSK" ] && return 0
    case $psk in *[!0-9a-fA-F]*) return 1 ;; esac
    [ ${#psk} -eq 64 ]
}

case ${1:-} in
    status)
        if [ -s "$ST/status" ]; then cat "$ST/status"; else echo "wpa_state=DISCONNECTED"; fi
        ;;
    scan_results)
        printf 'bssid\tfreq\tsignal\tflags\tssid\n'
        printf '00:11:22:33:44:55\t2412\t-45\t[WPA2-PSK-CCMP][ESS]\t%s\n' "$AP_SSID"
        printf '00:11:22:33:44:66\t2412\t-70\t[ESS]\tgarden\n'
        ;;
    list_networks)
        printf 'network id / ssid / bssid / flags\n'
        [ -d "$ST/blocks" ] || exit 0
        for id in $(ls "$ST/blocks" | sort -n); do
            s=$(tr -d '"' < "$ST/blocks/$id/ssid" 2>/dev/null || true)
            printf '%s\t%s\tany\t[DISABLED]\n' "$id" "$s"
        done
        ;;
    add_network)
        id=0
        while [ -d "$ST/blocks/$id" ]; do id=$((id + 1)); done
        mkdir -p "$ST/blocks/$id"
        echo "$id"
        ;;
    remove_network) rm -rf "$ST/blocks/$2" ;;
    select_network)
        id=$2
        log "select_network $id"
        if [ ! -d "$ST/blocks/$id" ]; then echo FAIL; exit 0; fi
        mm=$(tr -d '"' < "$ST/blocks/$id/key_mgmt" 2>/dev/null || true)
        ss=$(tr -d '"' < "$ST/blocks/$id/ssid" 2>/dev/null || true)
        pk=$(cat "$ST/blocks/$id/psk" 2>/dev/null || true)
        # wpa_supplicant's own default: a block with a psk and no key_mgmt is
        # WPA-PSK. Only a key_mgmt that was explicitly set to something else
        # (NONE, left by an open-network attempt) makes the AP refuse it.
        [ -z "$mm" ] && [ -n "$pk" ] && mm=WPA-PSK
        echo "$id" > "$ST/selected"
        if [ "$ss" != "$AP_SSID" ]; then
            set_state SCANNING
        elif [ "$mm" = "WPA-PSK" ] && psk_ok "$ss" "$pk"; then
            set_state COMPLETED
            printf 'ssid=%s\nkey_mgmt=WPA2-PSK\n' "$ss" >> "$ST/status"
        else
            # a WPA2-only access point refuses a block marked NONE, and refuses
            # a wrong key, the same way: the four-way handshake never finishes
            set_state 4WAY_HANDSHAKE
            log "refused: key_mgmt=$mm"
        fi
        ;;
    set_network)
        # every value is kept, exactly as the real wpa_supplicant does
        mkdir -p "$ST/blocks/$2"
        printf '%s\n' "$4" > "$ST/blocks/$2/$3"
        echo OK
        ;;
    enable_network) log "enable_network $2"; echo OK ;;
    disable_network|reconfigure|save_config) echo OK ;;
    *) echo "fake-wpa: unsupported $*" >&2; exit 1 ;;
esac
