#!/bin/sh
# install-menu-on-kindle.sh -- boot the Kindle into the last game.
#
#   ./install-menu-on-kindle.sh [IP]
#
# Builds the games, pushes them, the boot script and the upstart job
# (/etc/upstart/menu.conf), then starts the session. Boot opens
# /mnt/us/last-app (chess the first time). The in-game APPS tab picks
# another game. QUIT twice leaves the reader up until the next boot.
#
# To go back to the stock Amazon UI at boot: QUIT, then remove
# /etc/upstart/menu.conf on the device.
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
make menu chess tetris sudoku rockman grabtest
# xcom is the oldest and largest build; it is required for the XCOM button.
make kindle

echo "install-menu: pushing to $ip"
cat menu        | ssh_run 'cat > /mnt/us/menu && chmod +x /mnt/us/menu'
cat chess       | ssh_run 'cat > /mnt/us/chess && chmod +x /mnt/us/chess'
cat tetris      | ssh_run 'cat > /mnt/us/tetris && chmod +x /mnt/us/tetris'
cat sudoku      | ssh_run 'cat > /mnt/us/sudoku && chmod +x /mnt/us/sudoku'
cat rockman     | ssh_run 'cat > /mnt/us/rockman && chmod +x /mnt/us/rockman'
cat xcom        | ssh_run 'cat > /mnt/us/xcom && chmod +x /mnt/us/xcom'
cat tools/menu-boot.sh   | ssh_run 'cat > /mnt/us/menu-boot.sh && chmod +x /mnt/us/menu-boot.sh'
cat tools/clear-boot-splash.sh | ssh_run 'cat > /mnt/us/clear-boot-splash.sh && chmod +x /mnt/us/clear-boot-splash.sh'
cat tools/menu-boot.conf | ssh_run 'cat > /mnt/us/menu-boot.conf'
cat tools/chess-wifi.sh  | ssh_run 'cat > /mnt/us/chess-wifi.sh && chmod +x /mnt/us/chess-wifi.sh'
cat tools/kindle-run.sh  | ssh_run 'cat > /mnt/us/chess-run.sh && chmod +x /mnt/us/chess-run.sh'
cat tools/kindle-run.sh  | ssh_run 'cat > /mnt/us/xcom-run.sh && chmod +x /mnt/us/xcom-run.sh'
cat tools/kindle-run.sh  | ssh_run 'cat > /mnt/us/tetris-run.sh && chmod +x /mnt/us/tetris-run.sh'
cat tools/kindle-run.sh  | ssh_run 'cat > /mnt/us/sudoku-run.sh && chmod +x /mnt/us/sudoku-run.sh'
cat tools/kindle-run.sh  | ssh_run 'cat > /mnt/us/rockman-run.sh && chmod +x /mnt/us/rockman-run.sh'
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

echo "install-menu: done. The last game should be on the panel now."
echo "     Next boot opens that same game. The top-right button, then APPS, picks another."
echo "     QUIT twice gives the reader back until the next boot."