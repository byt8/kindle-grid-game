#!/bin/sh
# pw-watch.sh -- what does the power button do while a game owns the panel?
#
# The Kindle's power button is NOT /dev/input/event2 (that node is the Home
# button). Stock `powerd` reads /proc/whitney_button and turns a press into a
# suspend through /sys/power/state, but only after the screen-saver step, which
# needs the reader UI (pillowd) -- and a game that stops `x`/`framework` kills
# that. Presses still reach the hardware: `dmesg` shows "pmic: PWRON1 event:".
#
# Push and run on the device (busybox `timeout` does not accept a duration, so
# run it in the foreground and let the local timeout kill the ssh; it keeps
# logging afterwards):
#
#   cat tools/pw-watch.sh | ssh root@IP 'cat > /tmp/pw-watch.sh'
#   timeout 6 ssh root@IP 'sh /tmp/pw-watch.sh'
#   ssh root@IP 'tail /tmp/pw.log'
#
# One line a second: uptime | whitney button | powerd state | /sys/power/state.
i=0
: > /tmp/pw.log
while [ $i -lt 1200 ]; do
    up=$(cut -d' ' -f1 /proc/uptime)
    btn=$(cat /proc/whitney_button 2>&1)
    pd=$(/usr/bin/lipc-get-prop com.lab126.powerd status 2>&1 | head -1)
    st=$(cat /sys/power/state 2>&1)
    echo "$up | $btn | $pd | $st" >> /tmp/pw.log
    i=$((i + 1))
    sleep 1
done
