---
area: xcom
title: Kindle XCOM mission
aliases: [kindle-xcom, first-contact]
updated: 2026-10-30
---

# Kindle XCOM mission

## Map
- `src/xcom.c` — the mission, the rules, touch, and the PC self-test.
- `src/sprites.h` — generated 40x40 tiles. Do not edit.
- `tools/slice_sprites.py` — cuts `spritesheet.png` using the JSON rects.
- `Makefile` — `make test` on the PC, `make kindle` for the device binary.
- `run-on-kindle.sh` — copy `xcom` over SSH and run it.
- `jailbreak-kindle/kindle-tools/fbdaemon/engine/eink.h` — the screen. Not this repo. Do not edit it for this game.

## Facts
- [verified 2026-10-30 jailbreak-kindle/.agent-kb/areas/kindle-fb-daemon.md] The panel is 600x800, 8 bpp, stride 608. The engine draws 1-bit and pushes dirty tiles. Touch is `/dev/input/event3`.
- [verified 2026-10-30 eink.h] There is no grey blit. Greys are dither or they do not exist. A second renderer is not hiding in that file.
- [verified 2026-10-30 tactical_sprite_map_with_descriptions.json] Crops are approximate. The sheet is 1536x1024 RGBA. Unit HP, aim, mobility, armour, and damage are in the descriptions. Weapon range is not.
- [ran 2026-10-30 make test] Host self-test passed. Opening, selected-soldier, and auto-play PGM dumps are 600x800 and not blank. Selecting the soldier changes the picture.
- [ran 2026-10-30 file xcom] `make kindle` produced a 124044-byte static ARM EABI5 binary. It was not started on the device.

## Built
- One mission, FIRST CONTACT: 4 soldiers, 5 aliens, fog, cover, walk or run, shoot, overwatch, hunker, medic heal. Entry is `main` in `src/xcom.c`.
- Device: `./run-on-kindle.sh`, then QUIT and SURE. That calls `ek_close`, which gives the book screen back.
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

## Open
- A real finger on the glass is not verified. Host taps and the rule checks are.
