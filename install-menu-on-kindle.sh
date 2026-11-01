#!/bin/sh
# install-menu-on-kindle.sh -- boot the Kindle straight into the launcher menu.
#
#   ./install-menu-on-kindle.sh [IP]
#
# Builds the menu and chess for the device, pushes the menu, the menu boot
# driver, the upstart job, and the wifi helper, installs the job
# (/etc/upstart/menu.conf, replacing the old direct-to-chess /etc/upstart/
# chess.conf), then starts the menu session immediately so the device shows
# the menu now rather than on the next boot.
#
# The old chess job is removed without being started again, so a reboot lands
# on the menu. To go back to stock Amazon UI at boot (e.g. after a factory
# reset or to use the reader normally): reboot, or tap KINDLE in the menu and
# remove /etc/upstart/menu.conf on the device.
set -eu
cd "$(dirname "$0")"

KJ=jailbreak-kindle/kindle-tools
if [ -f "$KJ/.kfb-secrets" ]; then
    KFB_ENV_FILE="$(pwd)/$KJ/.kfb-secrets"
    export KFB_ENV_FILE
    . "$KJ/env.sh"
fi
ip=${1:-${KINDLE_IP:-192.168.0.63}}
key=${KINDLE_KEY:-$HOME/.ssh/kindle_touch_ed25519}

[ -f "$key" ] || { echo "install-menu: ssh key $key is missing" >&2; exit 1; }

ssh_run() {
    ssh -i "$key" -o BatchMode=yes -o IdentitiesOnly=yes \
        -o StrictHostKeyChecking=no -o UserKnownHostsFile="$HOME/.ssh/known_hosts" \
        -o LogLevel=ERROR -o ConnectTimeout=10 "root@$ip" "$@"
}

echo "install-menu: building for the device"
make menu chess tetris grabtest
# xcom is the oldest and largest build; it is required for the XCOM button.
make kindle

echo "install-menu: pushing to $ip"
cat menu        | ssh_run 'cat > /mnt/us/menu && chmod +x /mnt/us/menu'
cat chess       | ssh_run 'cat > /mnt/us/chess && chmod +x /mnt/us/chess'
cat tetris      | ssh_run 'cat > /mnt/us/tetris && chmod +x /mnt/us/tetris'
cat xcom        | ssh_run 'cat > /mnt/us/xcom && chmod +x /mnt/us/xcom'
cat tools/menu-boot.sh   | ssh_run 'cat > /mnt/us/menu-boot.sh && chmod +x /mnt/us/menu-boot.sh'
cat tools/menu-boot.conf | ssh_run 'cat > /mnt/us/menu-boot.conf'
cat tools/chess-wifi.sh  | ssh_run 'cat > /mnt/us/chess-wifi.sh && chmod +x /mnt/us/chess-wifi.sh'
cat tools/kindle-run.sh  | ssh_run 'cat > /mnt/us/chess-run.sh && chmod +x /mnt/us/chess-run.sh'
cat tools/kindle-run.sh  | ssh_run 'cat > /mnt/us/xcom-run.sh && chmod +x /mnt/us/xcom-run.sh'
cat tools/kindle-run.sh  | ssh_run 'cat > /mnt/us/tetris-run.sh && chmod +x /mnt/us/tetris-run.sh'
if [ -f chess.conf ]; then
    cat chess.conf | ssh_run 'cat > /mnt/us/chess.conf'
else
    echo "install-menu: no chess.conf; chess will use its built-in defaults" >&2
fi
# The menu-boot driver needs grabtest to prove the touch device is free.
cat grabtest      | ssh_run 'cat > /mnt/us/grabtest && chmod +x /mnt/us/grabtest'

echo "install-menu: installing the boot job (replaces chess.conf job)"
ssh_run '
    /usr/sbin/mntroot rw
    cp /mnt/us/menu-boot.conf /etc/upstart/menu.conf
    rm -f /etc/upstart/chess.conf
    sync
    /usr/sbin/mntroot ro
    # Run the menu now (the job itself only fires at boot). setsid + the
    # log redirect: a plain `&` is killed when this ssh session closes, and
    # the menu must own the panel until a button is tapped.
    /sbin/initctl list | grep -q "^menu start" && /sbin/initctl stop menu
    killall -q menu-boot.sh menu 2>/dev/null || true
    setsid sh /mnt/us/menu-boot.sh >>/mnt/us/menu-boot.log 2>&1 </dev/null &
'

echo "install-menu: done. The menu should be on the panel now."
echo "     Next boot will land on the menu; tap KINDLE for the reader UI."