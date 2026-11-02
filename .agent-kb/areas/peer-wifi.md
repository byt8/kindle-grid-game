# Peer wifi (ad-hoc kindle-to-kindle)

Direct kindle-to-kindle link, no router, for games and messaging. One success so far: `.63` entered an IBSS cell.

## What the hardware allows

- AR6003, kernel 2.6.31, wpa_supplicant 0.6.10, wifid as the supplicant owner.
- Module `ar6003.ko` pulled off the device contains `Network: Adhoc (Creator)` and `Network: Adhoc (Joiner)` code paths, plus `Infrastructure` and AP paths. Ad-hoc is supported, over WEXT (no nl80211, no `iw`).
- No DHCP on the link: each unit derives a static `10.30.0.x` IP from its own MAC tail byte.

## The way in (proven, live 2026-11-02)

`tools/peer-wifi.sh` on the device: `up | down | status`.

- Radio is controlled through `wpa_cli -i wlan0 -p /var/run/wpa_supplicant` exactly like `chess-wifi.sh` — **never kill wifid** (it owns the supplicant; stopping it risks the whole stack, and chess-wifi.sh proves you can swap networks while it runs).
- Network block for ad-hoc: `mode 1` (IBSS), `freq 2437` (channel 6), SSID `P2P-KINDLE-<octet>`, `key_mgmt NONE`. `enable_network` + `select_network`, then wait for `wpa_state=COMPLETED`.
- `[ran 2026-11-02 .63]` `up` succeeded: `ssid=P2P-KINDLE-205 ip=10.30.0.205 freq=2437MHz`, `up-exit=0`. **First live proof the AR6003 does ad-hoc via wpa_cli.**
- COMPLETED with no peer is normal for an IBSS creator (no AP to wait on) — do NOT gate on signal strength, or the creator hangs forever.

## Gotchas

- Busybox ash has NO `$((16#..))` radix arithmetic — syntax error. Use `printf '%d' "0x$tail"` instead.
- Switching wlan0 off the LAN severs ssh from the PC mid-command: the `up` returns, then the *next* ssh to `.63` hangs (no way back over the LAN path while on the ad-hoc link; USB net 192.168.15.244 also unreachable from this PC). The link is real, but remote administration of that unit dies until you can get physical/other access — restore by `peer-wifi.sh down` from the device, or reboot.
- busybox `timeout` on this firmware is `timeout -t` — a plain `timeout 45 cmd` fails with "can't execute '45'". Peer-wifi.sh self-limits at 30 s instead.
- Device stops answering ssh for long stretches when it drops off the LAN (a known quirk, seen on `.66`).

## Blocker

Full two-unit messaging proof (`run-peer-test.sh`) needs BOTH units reachable on the LAN first. As of 2026-11-02 `.66` was unreachable for the whole session; `.63` went off-LAN deliberately during the `up` test and could not be restored remotely. Re-run `run-peer-test.sh 192.168.0.63 192.168.0.66` once both are back.

## Files

- `echo-probe.c` — static ARM TCP echo server (`make echo-probe`) for the messaging proof; busybox `nc` on the Kindle is client-only, so a real listener is required.
- `run-peer-test.sh [A] [B]` — pushes both files to both units, `up`s the link, pings both ways, runs the echo proof A(listener)<-B(sender), tears `down` and verifies wifi restore.
