---
area: wifi
title: Wi-fi join (station) and the on-screen keyboard
aliases: [chess-wifi, wifi screens, password keyboard, connection failed]
updated: 2026-11-05
---

# Wi-fi join and the on-screen keyboard

Station mode only (for the routerless ad-hoc link see `areas/peer-wifi.md`).

## Files
- `tools/chess-wifi.sh` — `status | scan | join SSID PSK | auto`. Deployed as `/mnt/us/chess-wifi.sh`; the name is historical.
- `src/kframe.c` — the wi-fi screens: `kframe_wifi_setup`, `kframe_wifi_shot list|name|pass|sym|fail`.
- `src/kshell.c` — the shared keyboard those screens type into.
- `tests/wifi-test.sh` — the script against a stand-in `wpa_cli` (`tests/fake-wpa.sh`).
- `tests/wifi-screen-test.sh` — the **real screens** walked by taps against a stand-in script.

## Facts
- [verified 2026-11-03] The helper drives wifid's own `wpa_supplicant` over `/usr/bin/wpa_cli` (socket `/var/run/wpa_supplicant`). **Never start a second supplicant and never kill wifid.**
- [verified 2026-11-03] **A stale open-network block for the same SSID makes the join fail with a correct password.** An earlier attempt leaves a block with `key_mgmt=NONE`; `select_network` on it fails the four-way handshake and the only words available are "could not join (out of range, or the password was refused)". `join` now removes every block for that SSID before adding the new one and sets `key_mgmt` explicitly.
- [verified 2026-11-03] `fake-wpa.sh` models it: a block with a psk and no key_mgmt defaults to WPA-PSK; **only an explicit `key_mgmt=NONE`** makes a WPA2 AP refuse. Keep that rule — it is what makes test case 2 fail on the old script and pass on the new one.
- [verified 2026-11-03] The Kindle rejects a key shorter than 8 chars with a useless message; `join` catches it first and says so in plain words.
- [verified 2026-11-03] `scan` prints `ssid<TAB>signal<TAB>WPA2|WEP|open`, strongest first. The field separator must be a real tab — this busybox awk does not expand `\t` in `-F` and silently returns an empty list.
- [verified 2026-11-05 src/kshell.h] Keyboard geometry lives in `src/kshell.h` (`KSHELL_KB_*`). It is hidden until a field is focused. **123** swaps in one punctuation page; `@` is row 0 column 0 of that page. SHIFT is one capital then back to small, and the key is drawn solid black while armed. `tests/wifi-screen-test.sh` reads those defines. JOIN centre follows `KFRAME_WIFI_GO_*` in `src/kframe.h` (now 438, 228).
- [verified 2026-11-03] `KFRAME_WIFI_SCRIPT` overrides the script path at compile time **for the host build only**, so the tap test drives the real screens without touching `/mnt/us`.

## Ran
- [ran 2026-11-05 `make wifi-test`] Script suite 6/6 and screen suite 5/5 after the keyboard moved into `kshell`. The screen test still types `Alotbsol1`, `Aa`, and `@`. An open network logs `join garden ` (trailing space).
- [ran 2026-11-03 `make wifi-test`] script suite 6/6, screen suite 5/5. The pre-fix script fails case 2 of the screen suite too, so the regression is caught.
- [ran 2026-11-03 `make chess-test`] still green — the keyboard change did not disturb the other screens.

- [ran 2026-11-03 push to .63] `./run-chess-on-kindle.sh 192.168.0.63` rebuilt and pushed. Device md5s match the host build exactly: `/mnt/us/chess` = `2c1977fd…`, `/mnt/us/chess-wifi.sh` = `310573a2…`. `chess-wifi.sh status` on the glass returned `state=COMPLETED ssid=PRNet84 ip=192.168.0.63`, so the rewritten `join` path's parsing works against the real `wpa_cli`. Game running, pid confirmed via `/proc/<pid>/cmdline`, `kill -USR1` grab has 15 distinct grey levels (grey layer healthy, not the `ek_mono` flat 0/255 failure).

## Open
- A **real join** (typing a password on the glass into a network that is not already configured) is still unproven — the deployment only confirmed `status`. Next step: on the device, WI-FI → join an unconfigured SSID and watch the message.
- `./run-chess-on-kindle.sh` reuses an existing `./chess` unless src is newer; run `make chess-kindle` first if unsure.
