---
area: xcom
title: Kindle XCOM mission
aliases: [kindle-xcom, first-contact]
updated: 2026-10-31
---

# Kindle XCOM mission

## Map
- `src/xcom.c` — the mission, the rules, touch, and the PC self-test.
- `src/sprites.h` — generated 40x40 tiles. Do not edit.
- `tools/slice_sprites.py` — cuts `spritesheet.png` using the JSON rects.
- `Makefile` — `make test` on the PC, `make kindle` for the device binary.
- `run-on-kindle.sh` — copy `xcom`, `grabtest` and `tools/kindle-run.sh` over SSH and run the game. Takes an optional Kindle IP.
- `tools/kindle-run.sh` — on-device wrapper: kill leftover game, stop `kfbterm`, hard-kill `kfb`, grab-test, run, then restore the UI.
- `tools/grabtest.c` — `EVIOCGRAB` probe for `/dev/input/event3`.
- `jailbreak-kindle/kindle-tools/fbdaemon/engine/eink.h` — the screen. Not this repo. Do not edit it for this game.

## Facts
- [verified 2026-10-30 jailbreak-kindle/.agent-kb/areas/kindle-fb-daemon.md] The panel is 600x800, 8 bpp, stride 608. The engine draws 1-bit and pushes dirty tiles. Touch is `/dev/input/event3`.
- [verified 2026-10-30 eink.h] There is no grey blit. Greys are dither or they do not exist. A second renderer is not hiding in that file.
- [verified 2026-10-30 tactical_sprite_map_with_descriptions.json] Crops are approximate. The sheet is 1536x1024 RGBA. Unit HP, aim, mobility, armour, and damage are in the descriptions. Weapon range is not.
- [ran 2026-10-30 make test] Host self-test passed. Opening, selected-soldier, and auto-play PGM dumps are 600x800 and not blank. Selecting the soldier changes the picture.
- [ran 2026-10-30 file xcom] `make kindle` produced a 124044-byte static ARM EABI5 binary. It was not started on the device.
- [ran 2026-10-31 device 192.168.0.63] The game now starts with touch working: `eink: touch /dev/input/event3 ...` with no `EVIOCGRAB` warning, then `xcom: FIRST CONTACT`.
- [ran 2026-10-31 device] `kfbterm` (upstart job, `/etc/upstart/kfbterm.conf`) restarts `kfb` in session mode within a second whenever `/mnt/us/fbdaemon/DISPLAY` exists and Wi-Fi is up. `killall kfb` is useless on its own: the job respawns it. Stop the job first.
- [ran 2026-10-31 device] `killall kfb` (SIGTERM) leaves `kfb` running for minutes: its handler restores the whole Kindle UI first. `kill -9 $(pidof kfb)` frees `/dev/input/event3` in under a second and `grabtest` passes immediately. Safe because the game takes over the panel anyway and `restore-kindle.sh` puts the UI back.
- [ran 2026-10-31 device] A dead ssh session leaves `xcom` running with the touch grab, so the next launch gets `EVIOCGRAB refused: Resource busy` and a game with no touch. Killing the old `xcom` was necessary before touch came back.
- [ran 2026-10-31 device] Killing the local ssh (the tool did it at a timeout) killed the remote game with SIGKILL, and the wrapper's EXIT trap still restored the UI and restarted `kfbterm`. Even the ugly path leaves the Kindle usable.
- [verified 2026-10-31 device] `/mnt/us/DONT_START_FRAMEWORK` left behind after a run is not a bug: a running `kfb` session sets that flag itself and clears it when it exits.

## Built
- One mission, FIRST CONTACT: 4 soldiers, 5 aliens, fog, cover, walk or run, shoot, overwatch, hunker, medic heal. Entry is `main` in `src/xcom.c`.
- Device: `./run-on-kindle.sh [ip]`, then QUIT and SURE. That calls `ek_close`, which gives the book screen back. `make grabtest` builds the touch probe it pushes.
- PC: `./xcom-host --selftest` and `--dump`. Pipe `tap X Y` lines before `--dump` to play a picture.

## Decisions
- Use the existing engine instead of a new framebuffer driver. The update cost is the rect, and that code is already proven on this panel.
- Keep the game in this repo. `jailbreak-kindle` is shared. No KUAL line was added there.
- 1-bit sprites with a paper rim around people (`blit_unit`). A black soldier on a black tree tile disappears without the rim.
- Sheet mobility is divided by 3, and at least 4. The sheet numbers are for a bigger map.
- Stationary aim is +20. The sheet says "a bonus" and does not give the number. Range numbers live in `tune` because the sheet has none.
- Every frame forces the GC16 waveform and waits. A turn-based picture should be readable, not a fast DU ghost.

## Do not redo
- Do not hunt for a grey path in `eink.h`. It refuses greys on purpose.
- Do not use `eips` for on-panel text. The jailbreak KB already measured that it paints nothing on this firmware.
- Do not `killall kfb` and wait. Stop `kfbterm`, then `kill -9`.
- Do not put the user@host after the remote command in an `ssh` line: `ssh` takes the first bare argument as the hostname and dies with `hostname contains invalid characters`.
- Do not trust `set -e` behind a pipe: `./run-on-kindle.sh | tail` hides its exit status. Test by running it plain.

## Verified on hardware
- Real fingertip taps work and the tap-to-pixel mapping is correct: a full soldier phase was played on the glass (moves, shots, end of turn) and the alien turn fired back. The "cannot go there" line is the normal refusal for a tap on an unreachable tile.
- Second Kindle is `192.168.0.66`, same root ssh key as `.63`. `./run-on-kindle.sh 192.168.0.66` pushes, grabs touch, runs and restores.

## Open
- Nothing blocking. The game needs only a LAN, not the internet.
