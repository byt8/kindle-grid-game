---
area: xcom
title: Kindle XCOM mission
aliases: [kindle-xcom, first-contact]
updated: 2026-10-31
---

# Kindle XCOM mission

## Map
- `src/xcom.c` — the mission, the rules, touch, and the PC self-test.
- `src/audio.c`, `src/audio.h` — the speaker. One forked child owns `aplay`; the game writes request records into a pipe and never waits.
- `sound/words/*.pcm`, `sound/fx/*.pcm` — 91 word clips plus 7 effects, raw S16_LE mono 22050. Committed (2.2 MB); pushed to `/mnt/us/xcom-sound`.
- `tools/make_sound.py` — bakes the clips: words from the local TTS service, effects synthesised.
- `tools/check_sound.py` — derives the spoken vocabulary from `src/xcom.c` and fails if a word has no clip, or a clip has no word.
- `src/sprites.h` — generated 40x40 tiles. Do not edit.
- `tools/slice_sprites.py` — cuts `spritesheet.png` using the JSON rects.
- `Makefile` — `make test` on the PC, `make kindle` for the device binary.
- `run-on-kindle.sh` — copy `xcom`, `grabtest`, `tools/kindle-run.sh` and `sound/` (as one tar stream) over SSH and run the game. Takes an optional Kindle IP.
- `tools/kindle-run.sh` — on-device wrapper: kill leftover game, stop `kfbterm`, hard-kill `kfb`, grab-test, run with `--sound /mnt/us/xcom-sound`, then restore the UI. Extra arguments go to the game.
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
- [ran 2026-10-31 device + host] Sound path works end to end with no complaint from the game: `aplay -q -D plughw:0,0 -t raw -f S16_LE -c 1 -r 22050 -` on stdin returns 0, the WM8962 needs `SPKOUTL/R Mixer DACL/R`, `Speaker Mixer`, `Speaker 100`, `Speaker Boost 6` before the speaker is audible, and a 4-turn device run logged every `xcom:` line with zero `sound:` lines (so no missing clip, no mixer failure, no dying player).
- [ran 2026-10-31 device] The TTS service on this PC (`http://127.0.0.1:8770/speak?text=one&voice=Samantha`) answers 200 with a mono WAV; 91 words bake in about 28 s. No network is needed on the device.
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
  *Revised 2026-10-31:* that was a full-panel clean per flush, and the engine already tiles. Now `ek_waveform(0)` + `ek_policy(180, 6, 35)` + `ek_ghost(4, 30)` lets the engine pick GL16_FAST for a tap, and GC16 is kept only for big frames and the pre-refresh clean.

## Do not redo
- Do not hunt for a grey path in `eink.h`. It refuses greys on purpose.
- Do not use `eips` for on-panel text. The jailbreak KB already measured that it paints nothing on this firmware.
- Do not `killall kfb` and wait. Stop `kfbterm`, then `kill -9`.
- Do not put the user@host after the remote command in an `ssh` line: `ssh` takes the first bare argument as the hostname and dies with `hostname contains invalid characters`.
- Do not trust `set -e` behind a pipe: `./run-on-kindle.sh | tail` hides its exit status. Test by running it plain.
- Do not use `grep -E` on the Kindle: BusyBox 1.17 grep has no `-E`. Use `grep -e PAT -e PAT`.
- Do not run the device binary with `--auto` expecting it to return before this change: it used to fall into the touch loop. `auto_turns >= 0` now skips that loop, which is what makes an on-device smoke test possible.

## Verified on hardware
- Real fingertip taps work and the tap-to-pixel mapping is correct: a full soldier phase was played on the glass (moves, shots, end of turn) and the alien turn fired back. The "cannot go there" line is the normal refusal for a tap on an unreachable tile.
- Second Kindle is `192.168.0.66`, same root ssh key as `.63`. `./run-on-kindle.sh 192.168.0.66` pushes, grabs touch, runs and restores.

## Open
- Nothing blocking. The game needs only a LAN, not the internet.
- Nobody has listened to a full mission yet. The device log proves the clips load, the mixers are set and `aplay` accepts every frame; only an ear can confirm the volume suits the room.

## One unexplained kill (watch)
- On 2026-10-31 the game was `Killed` (rc=137) a few seconds after `seed` on `.66`, immediately after a previous session had just restored X/`cvm`. Not reproducible: the next launch ran normally for the whole session.
- Ruled out: no OOM / `lowmemorykiller` line in dmesg (the ring is only 329 lines but `grep -i oom` and `-i killed` are empty), no `xcom` mention, and `watchdog.log` shows only "framework running, nothing to do" (so `kfb-watchdog.sh` did not run `restore-kindle.sh` under us). `restore-kindle.sh` only `killall`s `cvm` and `kfb`, never `xcom`.
- If it returns: suspect a race with the previous session's restore still finishing (`cvm` starting) rather than anything in our scripts. Catch it with `dmesg | grep -i kill` on the device the moment it happens.
