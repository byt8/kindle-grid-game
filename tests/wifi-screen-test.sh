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

# screen geometry, matching the #defines in src/kframe.c
KB_X=22; KEY=56; KB_Y=150; BOT=454; BOT2=518; SYM_Y=390
# a letter key: row (0..3), column
key() { printf 'tap %d %d\n' $((KB_X + $2 * KEY + 10)) $((KB_Y + $1 * 60 + 10)); }
# a symbol key on the current page, by column
sym() { printf 'tap %d %d\n' $((KB_X + $1 * KEY + 10)) $((SYM_Y + 10)); }
SHIFT="tap 35 $((BOT + 10))"
SYMKEY="tap 510 $((BOT + 10))"
JOIN="tap 445 $((BOT2 + 10))"
NET0="tap 300 100"         # PRNet84, first row of the list
NET1="tap 300 150"         # garden, second row (open)

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
$common -o /tmp/wifi-screens tests/wifi-screen-main.c src/kframe.c \
  jailbreak-kindle/kindle-tools/fbdaemon/engine/eink.c -lm 2>&1 | head -20
[ -x /tmp/wifi-screens ] && ok "built" || { bad "build failed"; exit 1; }

echo "1. tap the network, type the password, join"
{
    echo "$NET0"
    echo "$SHIFT"          # A
    key 2 0                # a -> A
    key 2 8; key 1 8; key 1 4; key 3 4; key 2 1; key 1 8; key 2 8   # lotbsol
    key 0 0                # 1
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
{ echo "$NET0"; echo "$SHIFT"; key 2 0; key 2 0; echo "$JOIN"; } > "$T/taps"
/tmp/wifi-screens --no-takeover < "$T/taps" > "$T/out" 2>&1
got=$(grep '^join' /tmp/fake-wifi-check.log 2>/dev/null || echo '<none>')
case $got in
    "join PRNet84 Aa") ok "typed Aa: the shift did not stick" ;;
    *) bad "expected 'Aa', got: $got" ;;
esac

echo "3. the symbol page types a symbol"
rm -f /tmp/fake-wifi-check.log
# the keyboard opens on the letter page, whose keys are letters; the symbols
# are one press of MORE away. '@' is the first key of the first symbol page.
{ echo "$NET0"; sym 0; echo "$JOIN"; } > "$T/taps"
/tmp/wifi-screens --no-takeover < "$T/taps" > "$T/out" 2>&1
got=$(grep '^join' /tmp/fake-wifi-check.log 2>/dev/null || echo '<none>')
case $got in
    "join PRNet84 @") ok "typed @ from the symbol page" ;;
    *) bad "the symbol key did not type: $got" ;;
esac

echo "4. a refused password says so on the screen"
{ echo "$NET0"; key 0 0; echo "$JOIN"; echo "tap 400 685"; } > "$T/taps"
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
