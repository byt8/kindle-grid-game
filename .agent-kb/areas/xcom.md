---
area: xcom
title: Kindle XCOM mission
aliases: [kindle-xcom, first-contact]
updated: 2026-11-01
---

# Kindle XCOM mission

## Map
- `src/xcom.c` — the mission, the rules, touch, and the PC self-test.
- `src/audio.c`, `src/audio.h` — the speaker. One forked child owns `aplay`; the game writes request records into a pipe and never waits.
- `sound/words/*.pcm`, `sound/fx/*.pcm` — 91 word clips plus 7 effects, raw S16_LE mono 22050. Committed (2.2 MB); pushed to `/mnt/us/xcom-sound`.
- `tools/make_sound.py` — bakes the clips: words from the local TTS service, effects synthesised.
- `tools/check_sound.py` — derives the spoken vocabulary from `src/xcom.c` and fails if a word has no clip, or a clip has no word.
- `src/sprites.h` — generated 40x40 tiles, now **20** of them. Do not edit.
- `tools/bake_bold_sprites.py` — bakes the tiles from the generated art in `bold/` plus the terrain patterns.
- `bold_sprite_sources.json` — sheet -> grid -> sprite-id map, plus the enum order.
- `bold/{players,enemies,objects,drone}.png` — the generated art. These are the source of truth now.
- `tools/slice_sprites.py` — the old dithered cutter, kept for comparison (`make preview-old`).
- `Makefile` — `make test` on the PC, `make kindle` for the device binary.
- `run-on-kindle.sh` — copy `xcom`, `grabtest`, `tools/kindle-run.sh` and `sound/` (as one tar stream) over SSH and run the game. Takes an optional Kindle IP.
- `tools/kindle-run.sh` — on-device wrapper: kill leftover game, stop `kfbterm`, hard-kill `kfb`, grab-test, run with `--sound /mnt/us/xcom-sound`, then restore the UI. Extra arguments go to the game.
- `tools/grabtest.c` — `EVIOCGRAB` probe for `/dev/input/event3`.
- `jailbreak-kindle/kindle-tools/fbdaemon/engine/eink.c|.h` — the screen. **Not this repo**: it is a symlink to `~/jailbreak-kindle`, a shared constituent (own git repo) that also draws chess/tetris/sudoku/the menu. **Editable, but ask the user first** and say what changes for the other games before writing there; keep this repo's own game out of it (one-way rule).

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
- [ran 2026-10-31] The dither in `slice_sprites.py` is what made units unreadable: at 40x40 a soldier and a crate dither to the same speckle. Hard threshold (120) + one-pixel ink dilation keeps strokes at 2 px and keeps silhouettes apart.
- [ran 2026-10-31] cptr's `image_generate` writes **WebP** bytes into a `*.png` file (RIFF/WEBP header, single `VP8 ` chunk, no alpha). Sniff the format, never trust the extension.
- [ran 2026-10-31] This box had no PIL, no pip, no curl/ffmpeg/ImageMagick. `python3 -m ensurepip --upgrade` then `python3 -m pip install pillow` works (pypi reachable) and gives Pillow 13 with webp support.
- [ran 2026-10-31 make test] `src/sprites.h` rebuilt from scratch (20 sprites) and the whole suite passed: three PGMs ok, pictures differ, 91 words + 7 effects present.
- [ran 2026-10-31] Only 20 sprite ids are ever drawn. `src/sprites.h` now holds exactly those; the other 71 (vehicles, misc, ui, quirky) were dead weight. A future lookup of a dropped id fails loudly: `sp_find` returns -1 and `load_pictures`/`spawn` print `missing sprite X` and exit.
- [ran 2026-10-31] Baked ink coverage: hollow friendlies 33-45% of the body, solid aliens 73-95%. Anything under 15% aborts the bake.
- [verified 2026-11-01 eink.c:131,137,876 + src/xcom.c:1499] The panel is forced to 1-bit by two code choices, not hardware: `expand_lut` stretches each canvas bit to exactly 0 or 255 (`eink.c:876`), and `ek_mono(1)` at `src/xcom.c:1499` sets `EPDC_FLAG_FORCE_MONOCHROME`. The framebuffer is already 8 bpp and the engine already accepts GL16/GC16, so true grey needs no hardware or panel change.
- [verified 2026-11-01 grep ek_grey] No `ek_grey`/grey blit exists anywhere in the engine or `src/` — greyscale has to be written, not found.

## Built
- One mission, FIRST CONTACT: 4 soldiers, 5 aliens, fog, cover, walk or run, shoot, overwatch, hunker, medic heal. Entry is `main` in `src/xcom.c`.
- [verified 2026-11-01 `make preview`] Sprite **supply contract** lives in README.md under "The sprites are drawn bold on purpose": four sheets in `bold/` (players/enemies/objects 2x2, drone 1x1) = 13 cells, 512 px per cell, black-on-white, grey >= 120 is paper, players bake hollow and the rest solid, blank or <15% ink aborts. Terrain is never supplied -- `PATTERNS` owns all seven. Stats come from `tactical_sprite_map_with_descriptions.json`, so new art changes no rules. A new filename or grid means editing `bold_sprite_sources.json` too.
- Device: `./run-on-kindle.sh [ip]`, then QUIT and SURE. That calls `ek_close`, which gives the book screen back. `make grabtest` builds the touch probe it pushes.
- PC: `./xcom-host --selftest` and `--dump`. Pipe `tap X Y` lines before `--dump` to play a picture.
- **Manual:** `MANUAL.md` is the full game rules in plain-spoken prose. `make manual` bakes `manual.wav` via the local TTS service (same one the game's sound clips use).

## Decisions
- Use the existing engine instead of a new framebuffer driver. The update cost is the rect, and that code is already proven on this panel.
- Keep the game in this repo. `jailbreak-kindle` is shared. No KUAL line was added there.
- 1-bit sprites with a paper rim around people (`blit_unit`). A black soldier on a black tree tile disappears without the rim.
- Bold sprites come from generated line art, not from the painted sheet: 512x512 per sprite, threshold, trim to silhouette, dilate 1 px. Terrain is a generator pattern instead, because a picture scaled to 40x40 is noise.
- **Team is read from fill, not from detail:** friendly units are hollow (white body, black outline), aliens solid black. 40 px has no room for interior detail, so the silhouette alone carries it. The medic's cross and the alien eyes are gone by design.
- Sheet mobility is divided by 3, and at least 4. The sheet numbers are for a bigger map.
- Stationary aim is +20. The sheet says "a bonus" and does not give the number. Range numbers live in `tune` because the sheet has none.
- Every frame forces the GC16 waveform and waits. A turn-based picture should be readable, not a fast DU ghost.
  *Revised 2026-10-31:* that was a full-panel clean per flush, and the engine already tiles. Now `ek_waveform(0)` + `ek_policy(180, 6, 35)` + `ek_ghost(4, 30)` lets the engine pick GL16_FAST for a tap, and GC16 is kept only for big frames and the pre-refresh clean.
- **Greyscale = option B, approved by the user 2026-11-01 (chat `d02032d8-70ea-48e3-b0cb-0398f9770ff3`, "Sprite Replacement Requirements").** The user rejected dithered black/white ("thats still balck and white and not greyscale") — do **not** fall back to it because the engine is shared. Ship real grey levels.
- **How to touch the shared engine without breaking the others:** an opt-in `ek_grey()` mode in `eink.c`, leaving the existing 1-bit path byte-identical for chess/tetris/sudoku/menu, and dropping `ek_mono(1)` only on this game's side. Canvas 60KB -> 480KB and an 8x larger dirty-tile diff are the known costs. A `demo-dump` PGM showing mid-grey values (not just 0/255) is the proof.
- **Persevere, but ask before writing in `jailbreak-kindle`.** The engine may be changed — "it is a shared constituent" is not a refusal, it is a *warn-first* rule: state the plan, get the go-ahead (already given for option B), then edit. Never rewrite the shared engine's default behaviour.

## Do not redo
- Do not hunt for a grey path in `eink.h` — none exists (verified 2026-11-01), so re-searching is wasted; the answer is to build the opt-in `ek_grey()` path in Decisions, after asking the user.
- Do not feed the sprite baker a small source and expect detail: below ~200 px per sprite the threshold eats every interior line. Generate at 512 px per cell (a 2x2 sheet).
- Do not add a terrain id without a pattern in `PATTERNS`; the baker exits naming the missing id rather than baking a blank tile.
- Do not use `eips` for on-panel text. The jailbreak KB already measured that it paints nothing on this firmware.
- Do not `killall kfb` and wait. Stop `kfbterm`, then `kill -9`.
- Do not put the user@host after the remote command in an `ssh` line: `ssh` takes the first bare argument as the hostname and dies with `hostname contains invalid characters`.
- Do not trust `set -e` behind a pipe: `./run-on-kindle.sh | tail` hides its exit status. Test by running it plain.
- Do not use `grep -E` on the Kindle: BusyBox 1.17 grep has no `-E`. Use `grep -e PAT -e PAT`.
- Do not run the device binary with `--auto` expecting it to return before this change: it used to fall into the touch loop. `auto_turns >= 0` now skips that loop, which is what makes an on-device smoke test possible.

## "It still shows the old sprites"
- First suspect the device binary, not the art. `./xcom` is the ARM build actually pushed; if its mtime predates `src/sprites.h`, the panel is running yesterday's tiles. `make kindle` then `./run-on-kindle.sh IP`. Check mtimes first: `ls -l --time-style=+%H:%M:%S src/sprites.h xcom`.
- Second suspect a stale screenshot: the `xcom-bold-*.png` files are only as new as the last `pgm2png` run over `/tmp/xcom-*.pgm`. Re-run `make test` (writes the dumps) then `pgm2png`.
- Prove what was really drawn instead of eyeballing: `tools/find_tile.py FILE id` looks for the exact 40x40 sprite in a PGM; `tools/dump_sprite.py id` prints the header tile as ASCII; `tools/dump_region.py FILE X Y` prints a screen patch. Map geometry: `MAP_X 60`, `MAP_Y 36`, `TILE 40`, so tile (col,row) is at `60+40*col, 36+40*row`.
- Unexplored tiles are filled solid black (fog), so a big black block at the top of a dump is the map, not broken art. Units also get a rim/HP bar drawn over the tile, so `find_tile.py` misses them by design — dump the region instead.

## Post-processing (why good art looks bad on the panel)
- [ran 2026-11-01 `tools/sprite_stages.py`] The baker's `cell_to_tile` gained an opt-in `trace=` dict that records `src`, `src_ink`, `small`, `small_ink`, `cut`, `tile` as `(bytes, w, h)`. **Verified inert**: `src/sprites.h` md5 was `a0820c89…` before and after `make preview`.
- [ran 2026-11-01 measured] Seven steps sit between `bold/` art and the panel: threshold(120) -> bbox -> `area_resize` into <=36x34 -> threshold again -> `dilate(thicken=1)` -> `shape_mask` -> (friendlies) `erode(2)` and subtract. `tools/sprite_stages.py` renders each stage magnified 4x plus a mock of the tile on grass/forest with rim + badge + HP bar, so feedback can name the step, not the result.
- [ran 2026-11-01 measured] **Friendlies lose ~40% of their ink to the hollow erosion** (soldier 402 px -> 220, heavy 580 -> 266; tile 33-45% ink) so they read as a thin outline ring. **Solid enemies gain ink** (sectoid 144 -> 361, mutant 289 -> 687; tile 73-95% ink) because dilation at 40x40 fuses interior detail into one black blob. Those two effects are the whole "art is fine, game is terrible" gap. The knobs are `thicken` per sheet in `bold_sprite_sources.json`, `THRESH`, `FIT_W/FIT_H`, and the `erode(..., 2)` depth in `cell_to_tile`.

## Verified on hardware
- Real fingertip taps work and the tap-to-pixel mapping is correct: a full soldier phase was played on the glass (moves, shots, end of turn) and the alien turn fired back. The "cannot go there" line is the normal refusal for a tap on an unreachable tile.
- Second Kindle is `192.168.0.66`, same root ssh key as `.63`. `./run-on-kindle.sh 192.168.0.66` pushes, grabs touch, runs and restores.

## Greyscale (done 2026-11-01, PC-verified only)
- [ran 2026-11-01 `make test`] **The game draws real grey.** `src/xcom.c` calls `ek_grey_enable()` at startup (quits loudly if refused), paints every sprite, terrain square, fog wash and the HP bar through `ek_grey_rect()`, and `ek_mono(1)` is gone. Proof: `/tmp/xcom-open.pgm` has 9 distinct panel values (0/34/68/102/119/153/187/221/255) with 17.5% mid-grey pixels; before, the dump held only 0/255.
- [verified 2026-11-01 eink.c] The grey path is in the **shared** engine: `ek_grey_enable()` allocates a 4-bit level canvas, `grey_ramp[16]` maps level -> panel byte, `ek_grey_rect(x,y,w,h,level0..15)` takes a level (not a pixel value). The default 1-bit path is untouched, so chess/tetris/sudoku/menu render byte-identically. Without the layer the engine falls back to a 4x4 Bayer of `level*2`.
- [ran 2026-11-01] **Sprite tiles are now 4 bits per pixel.** `src/sprites.h`: 0 transparent, 1 paper, 2..9 tone where `v-1` is an ink level 0..8; `SP_N` 400 -> 800. `ink15(level) = (level*15+4)/8` in `src/xcom.c` maps that onto the engine's 0..15 (round-to-nearest, and exact back through the Bayer fallback). Regenerate with `make preview` — **never hand-edit `sprites.h`.**
- [ran 2026-11-01] The baker already stored tone (never thresholded it); only display threw it away. `tools/bake_bold_sprites.py` now mirrors `GREY_RAMP` + `ink15` instead of `BAYER4`, and its emitted header comment is the one `src/sprites.h` carries (edit both or the next bake reverts the header). Same for `tools/sprite_stages.py` (panel columns) and `tools/find_tile.py`, which now matches dumped grey **bytes**, not a 1-bit threshold.
- [ran 2026-11-01] Rebuild + verify in one go: `make preview && make test && python3 tools/find_tile.py /tmp/xcom-open.pgm terrain_tiles/grass` (finds 1011 grass placements on the opening dump).
- **Supersedes** these older notes, which describe the pre-grey pipeline: line 29 ("no grey blit"), 41 (threshold + dilate), 46 (hollow/solid ink %), 47-48 (forced 1-bit, no `ek_grey`), 52 (README "bold on purpose" rules), 60-62 (1-bit / hollow-vs-solid team reading), 72-73, 86 (fog = solid black), 88-91 (seven steps, erosion/dilation).

## Campaign: four missions (done 2026-11-02, PC-verified)
- [ran 2026-11-02 `make test`] **The game is a four-board campaign now.** All data lives in `src/xcom.c`: `M1..M4` board literals (13x12), `MISSIONS[N_MISSIONS]` (one spoken opening line per board), `ALIENS[N_MISSIONS][5]` (per-board alien group, **row-major regex order matters**), `SQUAD[]` unchanged. `draw()` reads `level` (0-based) for the header `M%d/4`; clearing a board calls `advance_level()` (newscale, re-roll seed, full redraw, spoken `mission N/4 seed S`), M4 win prints `campaign won` and exits 0, squad wipe anywhere ends the run (earlier `banner`/`game_over` calls would otherwise fall through past mission 4).
- [ran 2026-11-02] Host flag `./xcom-host --mission N` (clamped 1..N_MISSIONS) opens any board; `--auto N --mission M` plays it out. Selftest now loops **all four** boards (`init_game(level)` per level) and checks `current_level()==4` plus `advance_level()` at N-1.
- [ran 2026-11-02] Terrain grew two chars: **`b` bush** (walkable, cost 1, NO cover — `init_game` gives every char a `BL[]` entry, so an unknown char was a segfault, not a wall) and **`M` mountain** (blocked like `#`, and blocks sight: `strchr("#RT", ch) || ch=='M'` — rubble blocks movement only; mountains give no cover). Sprite `terrain_tiles/mountains` already existed in `load_pictures` (mission 1 never drew an `M`).
- [ran 2026-11-02] `tools/check_sound.py` now also reads the `MISSIONS` table (`mission_messages()` regexes `MISSIONS[N_MISSIONS] = {...};`), else moving the opening line into the table made `first/contact/wipe/them/out` look unsayable and the guard failed. New words (ford cross shallows long road stay cover crown take high ground) went into `tools/make_sound.py WORDS`, baked via the local TTS service `127.0.0.1:8770` (`make sound`); `check_sound` reports 102 word clips + 7 fx.
- [ran 2026-11-02] **Board validation** (inline python, worth reusing): parse `M1..M4` + `ALIENS`/`SQUAD` regexes, assert 13 rows x 12 cols, chars in `. = F B r # ~ R T C M`, all 9 spawns walkable and distinct. **Elite rule:** every enemy `sniper` sits at row 0 so chebyshev distance to the squad (row 12) is 12 > range 11 — missions 2/3/4 first had them at row 1 (distance 11 = in range turn 1, LOS clear over river/road) and were moved. Mutant melee (1), drone 6, assault 7: anything spawning row >= 5 needs this check.
- [ran 2026-11-02] Header proof without eyes: `--seed 1 --mission 1/2` dumps differ only at header columns 69-74 (the `1`/`2` glyph) plus the board — the mission counter is really drawn.

## Open
- **Greyscale is implemented (option B) — see "Greyscale" below.** The old "not implemented" note is closed: engine writes live in `jailbreak-kindle`, this game now calls `ek_grey_enable()`/`ek_grey_rect()` and carries 4-bit tone tiles. What is still unproven is the **device**: only PC dumps have been checked.
- Nothing blocking. The game needs only a LAN, not the internet.
- Nobody has listened to a full mission yet. The device log proves the clips load, the mixers are set and `aplay` accepts every frame; only an ear can confirm the volume suits the room.

## One unexplained kill (watch)
- On 2026-10-31 the game was `Killed` (rc=137) a few seconds after `seed` on `.66`, immediately after a previous session had just restored X/`cvm`. Not reproducible: the next launch ran normally for the whole session.
- Ruled out: no OOM / `lowmemorykiller` line in dmesg (the ring is only 329 lines but `grep -i oom` and `-i killed` are empty), no `xcom` mention, and `watchdog.log` shows only "framework running, nothing to do" (so `kfb-watchdog.sh` did not run `restore-kindle.sh` under us). `restore-kindle.sh` only `killall`s `cvm` and `kfb`, never `xcom`.
- If it returns: suspect a race with the previous session's restore still finishing (`cvm` starting) rather than anything in our scripts. Catch it with `dmesg | grep -i kill` on the device the moment it happens.
