#!/bin/sh
# tests/wifi-test.sh -- prove the join path on this PC, with no Kindle.
#
#   sh tests/wifi-test.sh
#
# Two things are under test, both from the "typed the password and it still said
# connection failed" round:
#
#  1. A password join must build its own block. wpa_supplicant keeps every value
#     ever set on a network id, so reusing the saved block for an ssid carried
#     its old key_mgmt into the new attempt and the access point refused it.
#     The second case below is exactly that: a block for PRNet84 saved by an
#     earlier open-network attempt, then a join with the right password.
#
#  2. A failure must say which stage failed, not "out of range, or the password
#     was refused" for every case.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
script=$root/tools/chess-wifi.sh
fake=$here/fake-wpa.sh
fail=0

ok()   { echo "  ok   $*"; }
bad()  { echo "  FAIL $*"; fail=$((fail + 1)); }

# a fresh fake device per case
setup() {
    T=$(mktemp -d)
    export FAKE_STATE=$T/state
    mkdir -p "$FAKE_STATE/blocks"
    cat > "$T/wpa_cli" <<EOF
#!/bin/sh
exec sh $fake "\$@"
EOF
    chmod +x "$T/wpa_cli"
    # an ifconfig that answers with an address once the fake is associated
    cat > "$T/ifconfig" <<'EOF'
#!/bin/sh
if grep -q 'wpa_state=COMPLETED' "$FAKE_STATE/status" 2>/dev/null; then
    echo "wlan0     Link encap:Ethernet  HWaddr 00:11:22:33:44:55"
    echo "          inet addr:192.168.0.66  Bcast:192.168.0.255  Mask:255.255.255.0"
fi
EOF
    chmod +x "$T/ifconfig"
    cat > "$T/udhcpc" <<'EOF'
#!/bin/sh
exit 0
EOF
    chmod +x "$T/udhcpc"
    # wpa_passphrase's only job here is the same PBKDF2 the device computes
    cat > "$T/wpa_passphrase" <<'EOF'
#!/bin/sh
printf 'network={\n\tssid="%s"\n\t#psk="hidden"\n\tpsk=%s\n}\n' "$1" \
  "$(python3 -c 'import hashlib,sys;print(hashlib.pbkdf2_hmac("sha1",sys.argv[2].encode(),sys.argv[1].encode(),4096,32).hex())' "$1" "$2")"
EOF
    chmod +x "$T/wpa_passphrase"
}

# run the helper against the fake, with a short join wait
run() {
    WPA_CLI=$T/wpa_cli IFCONFIG=$T/ifconfig UDHCPC=$T/udhcpc \
    WPA_PASSPHRASE=$T/wpa_passphrase WIFI_CONF=$T/wifi.conf WIFI_JOIN_WAIT=2 \
    sh $script "$@" 2>&1
}

echo "1. join a WPA2 network with the right password"
setup
out=$(run join PRNet84 Alotbsol1); rc=$?
[ $rc -eq 0 ] && ok "join returned 0" || bad "join returned $rc: $out"
case $out in *"ok: on PRNet84, ip 192.168.0.66"*) ok "says it is on the network with an address" ;;
  *) bad "unexpected output: $out" ;; esac
grep -q '^ssid=PRNet84' "$T/wifi.conf" 2>/dev/null && ok "remembered the ssid" || bad "did not remember the ssid"
# only the hash goes to disk, never the password
grep -q 'Alotbsol1' "$T/wifi.conf" 2>/dev/null && bad "the password is in wifi.conf in clear" || ok "password not stored in clear"

echo "2. a stale open-network block for the same ssid must not break the join"
setup
mkdir -p "$FAKE_STATE/blocks/7"
printf '"PRNet84"\n' > "$FAKE_STATE/blocks/7/ssid"
printf 'NONE\n'      > "$FAKE_STATE/blocks/7/key_mgmt"   # left by an open attempt
printf 'SHARED\n'    > "$FAKE_STATE/blocks/7/auth_alg"   # and its other leftovers
out=$(run join PRNet84 Alotbsol1); rc=$?
[ $rc -eq 0 ] && ok "join returned 0 despite the stale block" || bad "join returned $rc: $out"
[ -d "$FAKE_STATE/blocks/7" ] && bad "the stale block is still there" || ok "the stale block was removed"

echo "3. a wrong password says the password was refused"
setup
out=$(run join PRNet84 wrongpassword); rc=$?
[ $rc -ne 0 ] && ok "join failed as it should" || bad "join returned 0 with a wrong password"
case $out in *"password was refused"*) ok "names the password, not the range" ;;
  *) bad "unhelpful failure: $out" ;; esac

echo "4. an ssid that is not there says it was not found"
setup
out=$(run join NowhereNet Alotbsol1); rc=$?
[ $rc -ne 0 ] && ok "join failed as it should" || bad "join returned 0 for a missing network"
case $out in *"could not find NowhereNet"*) ok "names the ssid it could not find" ;;
  *) bad "unhelpful failure: $out" ;; esac

echo "5. a failed join gives the other saved networks back"
setup
mkdir -p "$FAKE_STATE/blocks/1"
printf '"garden"\n' > "$FAKE_STATE/blocks/1/ssid"
run join PRNet84 wrongpassword >/dev/null
grep -q 'enable_network' "$FAKE_STATE/log" && ok "other networks were re-enabled" \
  || bad "the other saved network stayed disabled"

echo "6. a short password is refused before anything is touched"
setup
out=$(run join PRNet84 short); rc=$?
[ $rc -ne 0 ] && ok "join failed as it should" || bad "join returned 0 with a short password"
case $out in *"at least 8 characters"*) ok "says the reason" ;; *) bad "unhelpful: $out" ;; esac
[ -d "$FAKE_STATE/blocks/0" ] && bad "a block was created for a refused password" || ok "no block created"

rm -rf "$T" 2>/dev/null
if [ $fail -eq 0 ]; then echo "wifi-test: all checks passed"; exit 0; fi
echo "wifi-test: $fail check(s) failed"; exit 1
