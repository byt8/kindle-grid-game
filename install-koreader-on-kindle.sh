#!/bin/sh
# install-koreader-on-kindle.sh -- install KOReader on the Kindle Touch.
#
#   ./install-koreader-on-kindle.sh [IP]
#
# Pushes the KOReader package (jailbreak-kindle/kindle-tools/
# koreader-kindle-v2026.07.1.zip, the plain K4/Touch/PW1 build) to /mnt/us,
# unzips it there (top level is koreader/ + extensions/koreader/, so unzipping
# at the userstore root IS the install), verifies the hash first, then removes
# the zip. Nothing is written outside /mnt/us; no reboot. KOReader then shows
# in KUAL ("Start KOReader") AND as the menu's KOREADER button.
#
# Depends on the device being jailbroken (root ssh) and reachable.
set -eu
cd "$(dirname "$0")"

KJ=jailbreak-kindle/kindle-tools
ZIP="$KJ/koreader-kindle-v2026.07.1.zip"
HASH=${KOREADER_ZIP_MD5:-af87904b4b47007fecfe907bbd15fe05}

if [ -f "$KJ/.kfb-secrets" ]; then
    KFB_ENV_FILE="$(pwd)/$KJ/.kfb-secrets"
    export KFB_ENV_FILE
    . "$KJ/env.sh"
fi
ip=${1:-${KINDLE_IP:-192.168.0.66}}
key=${KINDLE_KEY:-$HOME/.ssh/kindle_touch_ed25519}

[ -f "$key" ] || { echo "install-koreader: ssh key $key is missing" >&2; exit 1; }
[ -f "$ZIP" ] || { echo "install-koreader: $ZIP is missing" >&2; exit 1; }

ssh_run() {
    ssh -i "$key" -o BatchMode=yes -o IdentitiesOnly=yes \
        -o StrictHostKeyChecking=no -o UserKnownHostsFile="$HOME/.ssh/known_hosts" \
        -o LogLevel=ERROR -o ConnectTimeout=10 "root@$ip" "$@"
}

if ! command -v md5sum >/dev/null 2>&1; then
    echo "install-koreader: md5sum not found on this host" >&2
    exit 1
fi
local_md5=$(md5sum "$ZIP" | awk '{print $1}')
if [ "$local_md5" != "$HASH" ]; then
    echo "install-koreader: local zip md5 $local_md5 != expected $HASH" >&2
    exit 1
fi

echo "install-koreader: pushing $ZIP to $ip (/mnt/us)"
cat "$ZIP" | ssh_run 'cat > /mnt/us/koreader-kindle.zip'

echo "install-koreader: verifying hash on the device"
dev_md5=$(ssh_run 'md5sum /mnt/us/koreader-kindle.zip | awk "{print \$1}"')
if [ "$dev_md5" != "$HASH" ]; then
    echo "install-koreader: device zip md5 $dev_md5 != expected $HASH" >&2
    ssh_run 'rm -f /mnt/us/koreader-kindle.zip'
    exit 1
fi

echo "install-koreader: installing (unzip at the userstore root)"
ssh_run '
    cd /mnt/us
    /usr/bin/unzip -q -o koreader-kindle.zip
    rm -f koreader-kindle.zip
    chmod -R a+rX /mnt/us/koreader /mnt/us/extensions/koreader
    sync
'

echo "install-koreader: done. Files on the device:"
ssh_run 'ls -d /mnt/us/koreader /mnt/us/extensions/koreader; ls -la /mnt/us/koreader/koreader.sh'
