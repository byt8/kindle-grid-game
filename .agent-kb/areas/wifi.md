---
area: wifi
title: Wi-fi join (station) and the on-screen keyboard
aliases: [chess-wifi, wifi screens, password keyboard, connection failed]
updated: 2026-11-03
---

# Wi-fi join and the on-screen keyboard

Station mode only (for the routerless ad-hoc link see `areas/peer-wifi.md`).

## Files
- `tools/chess-wifi.sh` — `status | scan | join SSID PSK | auto`. Deployed as `/mnt/us/chess-wifi.sh`; the name is historical.
- `src/kframe.c` — the screens: `kframe_wifi_setup`, `kframe_wifi_shot list|name|pass|sym|fail`.
- `tests/wifi-test.sh` — the script against a stand-in `wpa_cli` (`tests/fake-wpa.sh`).
- `tests/wifi-screen-test.sh` — the **real screens** walked by taps against a stand-in script.

## Facts
- [verified 2026-11-03] The helper drives wifid's own `wpa_supplicant` over `/usr/bin/wpa_cli` (socket `/var/run/wpa_supplicant`). **Never start a second supplicant and never kill wifid.**
- [verified 2026-11-03] **A stale open-network block for the same SSID makes the join fail with a correct password.** An earlier attempt leaves a block with `key_mgmt=NONE`; `select_network` on it fails the four-way handshake and the only words available are "could not join (out of range, or the password was refused)". `join` now removes every block for that SSID before adding the new one and sets `key_mgmt` explicitly.
- [verified 2026-11-03] `fake-wpa.sh` models it: a block with a psk and no key_mgmt defaults to WPA-PSK; **only an explicit `key_mgmt=NONE`** makes a WPA2 AP refuse. Keep that rule — it is what makes test case 2 fail on the old script and pass on the new one.
- [verified 2026-11-03] The Kindle rejects a key shorter than 8 chars with a useless message; `join` catches it first and says so in plain words.
- [verified 2026-11-03] `scan` prints `ssid<TAB>signal<TAB>WPA2|WEP|open`, strongest first. The field separator must be a real tab — this busybox awk does not expand `\t` in `-F` and silently returns an empty list.
- [verified 2026-11-03] Keyboard geometry: letters at `W_KB_X=22`, `W_KEY=56`, `W_KB_Y=150`, 4 rows of 60. **Symbol row at `W_KB_Y+240` (y 390)**, buttons at `W_BOT` y 454 and `W_BOT2` y 518. Two symbol pages of 12 keys (`@.-_!#$%&*` and `+=/?;:,()`), toggled by the `SYM 1/2` key at x 510.
- [verified 2026-11-03] SHIFT is one capital then back to small, and the button reads `SHIFT ON` while armed. A silent caps state mistypes the first character of every password.
- [verified 2026-11-03] `KFRAME_WIFI_SCRIPT` overrides the script path at compile time **for the host build only**, so the tap test drives the real screens without touching `/mnt/us`.

## Ran
- [ran 2026-11-03 `make wifi-test`] script suite 6/6, screen suite 5/5. The pre-fix script fails case 2 of the screen suite too, so the regression is caught.
- [ran 2026-11-03 `make chess-test`] still green — the keyboard change did not disturb the other screens.

## Open
- The fixed script and binary are **not yet pushed**. `./run-chess-on-kindle.sh` reuses an existing `./chess` unless rebuilt, so run `make chess-kindle` first. No real join has been done on the glass since the fix.
