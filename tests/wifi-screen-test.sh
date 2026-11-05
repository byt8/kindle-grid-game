#!/bin/sh
# tests/wifi-screen-test.sh -- walk the wi-fi screen with taps, on this PC.
#
#   sh tests/wifi-screen-test.sh
#
# The screens are the real ones from src/kframe.c; only the script they shell
# out to is a stand-in (written to /tmp below), built with -DKFRAME_WIFI_SCRIPT
# so nothing on /mnt/us is touched.
#
# What this pins, from the "typed the password and it still said connection
# failed" round:
#   * the keyboard can produce the characters a password actually contains
#   * SHIFT is one capital, then back to small
#   * a failed join says which stage failed, on the screen
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
cd "$root" || exit 1
T=$(mktemp -d)
fail=0
ok()  { echo "  ok   $*"; }
bad() { echo "  FAIL $*"; fail=$((fail + 1)); }

# Geometry is the #defines in src/kshell.h and src/kframe.h. Read them so a
# layout change cannot leave this test tapping the old pixels.
hdr() { awk -v k="$1" '$1=="#define" && $2==k { print $3; exit }' "$2"; }
KB_X=$(hdr KSHELL_KB_X src/kshell.h)
KB_Y0=$(hdr KSHELL_KB_Y0 src/kshell.h)
KEY_W=$(hdr KSHELL_KEY_W src/kshell.h)
KEY_GAP=$(hdr KSHELL_KEY_GAP src/kshell.h)
KEY_H=$(hdr KSHELL_KEY_H src/kshell.h)
ROW_H=$(hdr KSHELL_ROW_H src/kshell.h)
ROW2_X=$(hdr KSHELL_ROW2_X src/kshell.h)
PITCH=$((KEY_W + KEY_GAP))
ROW_Y=$(hdr KFRAME_WIFI_ROW_Y src/kframe.h)
ROW_HH=$(hdr KFRAME_WIFI_ROW_H src/kframe.h)
ROW_P=$(hdr KFRAME_WIFI_ROW_P src/kframe.h)
GO_X=$(hdr KFRAME_WIFI_GO_X src/kframe.h)
GO_Y=$(hdr KFRAME_WIFI_GO_Y src/kframe.h)
GO_W=$(hdr KFRAME_WIFI_GO_W src/kframe.h)
GO_H=$(hdr KFRAME_WIFI_GO_H src/kframe.h)
# rows 0 and 1 are ten keys. row 2 is nine, centered. row 3 letters start
# two columns in, because SHIFT occupies the first two.
k01() { printf 'tap %d %d\n' $((KB_X + $2 * PITCH + KEY_W / 2)) $((KB_Y0 + $1 * ROW_H + KEY_H / 2)); }
k2()  { printf 'tap %d %d\n' $((ROW2_X + $1 * PITCH + KEY_W / 2)) $((KB_Y0 + 2 * ROW_H + KEY_H / 2)); }
k3()  { printf 'tap %d %d\n' $((KB_X + ($1 + 2) * PITCH + KEY_W / 2)) $((KB_Y0 + 3 * ROW_H + KEY_H / 2)); }
SW=$((2 * KEY_W + KEY_GAP))
SHIFT="tap $((KB_X + SW / 2)) $((KB_Y0 + 3 * ROW_H + KEY_H / 2))"
SYMKEY="tap $((KB_X + SW / 2)) $((KB_Y0 + 4 * ROW_H + KEY_H / 2))"
JOIN="tap $((GO_X + GO_W / 2)) $((GO_Y + GO_H / 2))"
NET0="tap 300 $((ROW_Y + ROW_HH / 2))"
NET1="tap 300 $((ROW_Y + ROW_P + ROW_HH / 2))"

common="gcc -std=c11 -O2 -Wall -Wextra -Werror -DEK_HOST -Isrc \
  -Ijailbreak-kindle/kindle-tools/fbdaemon/engine \
  -DKFRAME_WIFI_SCRIPT=\"/tmp/fake-wifi-check.sh\""

echo "0. building the screens against a stand-in wifi script"
cat > /tmp/fake-wifi-check.sh <<'EOF'
#!/bin/sh
echo "$*" >> /tmp/fake-wifi-check.log
case $1 in
    status) echo "state=DISCONNECTED"; echo "ssid="; echo "ip="; exit 1 ;;
    scan)   printf 'PRNet84\t-45\tWPA2\n'; printf 'garden\t-70\topen\n' ;;
    join)   if [ "$3" = "Alotbsol1" ]; then
                echo "ok: on $2, ip 192.168.0.66"; exit 0
            fi
            echo "error: the password was refused by $2" >&2; exit 1 ;;
esac
exit 0
EOF
chmod +x /tmp/fake-wifi-check.sh
$common -o /tmp/wifi-screens tests/wifi-screen-main.c src/kframe.c src/kshell.c \
  jailbreak-kindle/kindle-tools/fbdaemon/engine/eink.c -lm 2>&1 | head -40
[ -x /tmp/wifi-screens ] && ok "built" || { bad "build failed"; exit 1; }

echo "1. tap the network, type the password, join"
{
    echo "$NET0"
    echo "$SHIFT"          # A
    k2 0                   # a -> A
    k2 8                   # l
    k01 1 8                # o
    k01 1 4                # t
    k3 4                   # b
    k2 1                   # s
    k01 1 8                # o
    k2 8                   # l
    k01 0 0                # 1
    echo "$JOIN"
} > "$T/taps"
rm -f /tmp/fake-wifi-check.log
/tmp/wifi-screens --no-takeover < "$T/taps" > "$T/out" 2>&1
got=$(grep '^join' /tmp/fake-wifi-check.log 2>/dev/null || echo '<none>')
case $got in
    "join PRNet84 Alotbsol1") ok "typed exactly Alotbsol1 and joined" ;;
    *) bad "the join line was: $got" ;;
esac

echo "2. SHIFT is one capital, then back to small"
rm -f /tmp/fake-wifi-check.log
{ echo "$NET0"; echo "$SHIFT"; k2 0; k2 0; echo "$JOIN"; } > "$T/taps"
/tmp/wifi-screens --no-takeover < "$T/taps" > "$T/out" 2>&1
got=$(grep '^join' /tmp/fake-wifi-check.log 2>/dev/null || echo '<none>')
case $got in
    "join PRNet84 Aa") ok "typed Aa: the shift did not stick" ;;
    *) bad "expected 'Aa', got: $got" ;;
esac

echo "3. the symbol page types a symbol"
rm -f /tmp/fake-wifi-check.log
# 123 swaps the letter keys for punctuation. '@' is the first key on that page.
{ echo "$NET0"; echo "$SYMKEY"; k01 0 0; echo "$JOIN"; } > "$T/taps"
/tmp/wifi-screens --no-takeover < "$T/taps" > "$T/out" 2>&1
got=$(grep '^join' /tmp/fake-wifi-check.log 2>/dev/null || echo '<none>')
case $got in
    "join PRNet84 @") ok "typed @ from the symbol page" ;;
    *) bad "the symbol key did not type: $got" ;;
esac

echo "4. a refused password says so on the screen"
{ echo "$NET0"; k01 0 0; echo "$JOIN"; echo "tap 400 686"; } > "$T/taps"
/tmp/wifi-screens --no-takeover < "$T/taps" > "$T/out" 2>&1
if grep -q 'the password was refused by PRNet84' "$T/out"; then ok "the screen was told why"
else bad "no reason reached the screen: $(cat "$T/out")"; fi

echo "5. an open network joins with no password screen"
rm -f /tmp/fake-wifi-check.log
echo "$NET1" > "$T/taps"
/tmp/wifi-screens --no-takeover < "$T/taps" > "$T/out" 2>&1
got=$(grep '^join' /tmp/fake-wifi-check.log 2>/dev/null || echo '<none>')
case $got in
    "join garden ") ok "joined the open network straight away" ;;
    *) bad "open network did not join: $got" ;;
esac

rm -rf "$T"
if [ $fail -eq 0 ]; then echo "wifi-screen-test: all checks passed"; exit 0; fi
echo "wifi-screen-test: $fail check(s) failed"; exit 1
