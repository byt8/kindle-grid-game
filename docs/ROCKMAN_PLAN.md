# Rockman on the Kindle — implementation plan

Implemented in `src/rockman.c`. Where this brief and `scratch/Rockman.asm`
disagree, the asm won. See `.agent-kb/areas/rockman.md`.

Goal: one new source file, `src/rockman.c`, that plays the 1984 VIC-20 Rockman
ruleset faithfully on a 1-bit 600x800 Kindle Touch, with tap buttons, the house
test target, and a run script. No audio (the VIC tune is not ported; noted as
future work).

Source of truth: `scratch/Rockman.asm` → machine-extracted to
`src/rockman_data.h` by `tools/extract_rockman.py` (`make rockman-data` passes).
Rules doc: `docs/ROCKMAN_SPEC.md`, with the corrections in section 1.

---

## 1. Corrections to the spec (settled this session)

1. **Set bits in the room maps are rocks, not walls** (`DrawMapByte`, asm
   1494-1512: set bit -> `TILE_ROCK`, clear bit -> `TILE_DIRT`). The maps are
   rock + dirt only; monsters, diamonds, traps, the player and the frame are all
   added at runtime. Consequently `RotAt`'s first test is "is there any tile
   here at all" (anything < `TILE_BRICK`), not "is it a wall".
2. **`CELL_UP/HERE/DOWN/LEFT/RIGHT` are screen-buffer offsets** (+22/+44/+66/
   +43/+45 relative to `ptr`, which points two rows above the cell of interest),
   reused as direction codes in `TryLeaveRoom`, `ChangeRoom` and `mmDirOffset`.
   They are not magic screen values.
3. **The screen layout is documented in the asm header (lines 107-112)**:
   room = rows 0-13, cols 3-18 (16x14); timer scale = col 1, rows 3-12;
   lives = col 20 row 1; "SC" = col 20 row 3; score = col 20 rows 5-12;
   "HI" = col 21 row 3; hi-score = col 21 rows 5-12; everything else brick.
   `DrawBorderScreen` (1432-1455) fills every cell with brick first.
4. **The top exit lives in the map's row 0, cols 10-11**: the map has dirt there,
   and `DrawRoom` (1460-1472) overwrites exactly those two cells with brick to
   "seal" them; the cells are still walkable as an exit (see `TryLeaveRoom`,
   404; the ChangeRoom comment at 1115 says so).
5. Screen tiles come from `rm_tiles[0x2D][16]` (8x16 bitmaps). Verified as
   ASCII: brick = brick pattern, dirt = 50% checker, empty = blank, rock =
   filled blob, diamond = hollow diamond, trap/player/monster/explosion = solid
   glyphs, digits = outlined with a dithered fill. All read cleanly in 1 bit.

## 2. Model and data

- `scr[15][22]` uint8 — tile id per screen cell (screen coords, exactly like the
  VIC screen RAM at $1E00). `drawn[15][22]` — last rendered value for diffing
  (tile id, with bit 7 = "gauge marker inverted here").
- Room decode: `rm_room_maps[room][28]` = 14 rows x 2 bytes; bit set -> rock,
  clear -> dirt, written into `scr` at rows 0-13, cols 3-18. Frame + HUD filled
  with brick per `DrawBorderScreen`, then the HUD tiles, then the top-exit seal.
- `ply_r/ply_c`, `dir` (0 up-down, 1 left, 2 right), animation phase,
  `ticks[3]` from `rm_tick_limits[] = {14, 16, 9}` (control, monster, rock).
- `lives`, `score`/`hi` as 4 BCD bytes each (8 digits, exactly the original's
  8-column digit columns), `room` (0 title, 1-20 play, 21 win), `speed`.
- Timer: `marks` 0..9, `steps` 0..119. One mark = 120 steps x 6 passes
  (`TimerTick`, asm 1701-1729), 9 marks to death (~100 s at base speed).
- Exits/entries/spawn data straight from the header:
  `rm_exit_{down,left,up,right}[room-1]`, `rm_entry_row/col[]`,
  `rm_fixed_diamonds[]`, `rm_frame_{a,b}[6]`, `rm_mm_dir_*`, `rm_explode_tiles`,
  `rm_explode_tiles`, `rm_rock_sounds` (unused, silent port).

## 3. Screen geometry (600x800)

- Cell = **27 x 40 px**, grid origin **(3, 6)**: 22 cols x 15 rows = 594 x 600
  (ends at x 597, y 606). Cell (col,row) -> x = 3 + 27*col, y = 6 + 40*row.
- Tile art 8x16 -> cell by nearest neighbour (`sx = x*8/27`, `sy = y*16/40`),
  bits -> `EK_INK`, background -> `EK_PAPER`. Inverting the VIC's black
  background is deliberate: open cells must read as paper on e-ink.
- Gauge marker (was colour-only): invert the current mark's cell, so the marker
  visibly climbs col 1 rows 3-12. Diamond sparkle flicker (colour-only in the
  original) is dropped: e-ink flashing with no gameplay effect is not worth it.
- Buttons: 2 rows x 3, mirroring tetris — x 10/206/402, w 186, h 62,
  y 646 (LEFT / UP / RIGHT) and 716 (DOWN / PUSH / QUIT).

## 4. Per-pass loop and timings

One pass = `ek_ms()` schedule, base **16 ms x (256 - speed)/256** (the shape of
`GameDelay`, asm 1479-1489: X starts at 256-speed, nine inner loops). Order
(locked against `LoopGame`, asm 488-520, during the build):

1. player animation phase (asm 1576-1602)
2. control tick every 14 passes (~224 ms): read pad -> `PlayerMove` (push rock,
   dig dirt, touch diamond/trap, walk into a monster, try to leave the room)
3. monster tick every 16 passes: `MoveMonsters` + decay `monCtr`
4. rock tick every 9 passes: `RockAndDiamondFall` (fall -> roll -> crush),
   traps fire at frame phase 2 (rock still resting above -> `PlayerDies`,
   else 25% chance, refused if the tile below is a falling rock or unblocked
   land with 2 of 4 sides blocked, asm 1365-1383)
5. explosion / death / room-complete processing
6. `TimerTick` every pass
7. render diff + `ek_flush`, throttled to >= 150 ms between flushes

Render only changes: each present() recomputes the wanted tile for all 330 cells
and repaints the ones that differ (the tetris `flush_well` pattern). Engine
setup copied from tetris: kindle `ek_mono(1)`, `ek_ghost(6,30)`,
`ek_policy(60,4,30)`; test `ek_ghost(0,0)`, `ek_policy(180,4,30)`.

## 5. Rules to implement (function for function)

| Port function | Asm | Note |
|---|---|---|
| `build_room` | `DrawBorderScreen`, `DrawRoomMap`, `DrawRoom`, 1432-1535 | brick fill, map bits, HUD cells, seal top exit cols 10-11 |
| `spawn_diamonds` | `SpawnDiamonds` + `rm_fixed_diamonds` | 5 fixed, plus 3 behind rocks in the rooms named by the table (rooms 1-14: 8 total; 15-20: 5 — asserted by the selftest) |
| `spawn_enemies` | `SpawnEnemies`, 1224-1297 | seed -> monster A/B/C/D, fixed diamonds restamped as cell above, frame from `rm_frame_*` |
| `player_move` | `PlayerMove`-region, 587-657 | push: 7-tick count `ldx $0369` + rock animates stages in place; dig only where allowed; blocked by brick/rock/monster; into monster -> die |
| `try_leave_room` / `change_room` | 404, 1107-1135 | exit table per direction, 0 = sealed; entry positions 1-4 (rm_entry_row/col) |
| `move_monsters` | `MonsterMove`/`MonMove`, 989-1094 | blocked by tiles >= brick and by other monsters; seek when the player is below, else random of 4; player-on-target -> die; `monCtr` decay opens blocked monsters to random walking |
| `move_rocks` | `RockAndDiamondFall`, 744-836 | bottom-up, stage -> fall -> rolls under `RotAt` rules (rock/dirt only, diamond = blocked, >=2 of 4 sides blocked = rest, else pick one) -> crush below (diamonds die first, `diamondCtr--`) -> 4-stage explosion |
| `touch_trap` / `fire_traps` | 425, 1330-1426 | trap clears itself then explodes; 4-way blast; `PlayerDies` if the player is caught |
| `tick_death` | `PlayerDies`-region, 664-750 | 4 death frames -> restart current room with `lives-1` (no input freeze: there is no such delay routine) or game over at 0 |
| `room_complete` | `ProcessFire`-region, 523-546 | clear -> top exit unsealed; `diamondCtr` reaches 0 -> clear + top exit opens |
| `timer_tick` | `TimerTick` 1701-1729 | 9 marks x 120 steps x 6 passes, death at wrap |
| `score` | `AddToScore`/`AddTens`, 1309-1360 | BCD add/carry, 8 digits, hi-score takes over on carry; all adders contribute: 100/diamond, 300/clear + 25 x steps left, 1000/room, 5000/life |
| `win` / restart | `PlanInRoom` win branch + `IncSpeed` 1614-1622 | room 21 picture, 5 lives, room 0, speed + 1 (capped: confirm the exact wrap value) |

## 6. Screens, input, CLI

- Room 0 = title picture, room 21 = win picture, both plain `build_room` maps
  (plus `rm_frame_a` on row 0 / row 13 the way the original does).
- Title/game-over: tap anywhere = new game (tetris convention). QUIT asks SURE.
- A tap = one step; holding a button repeats every control tick (the original
  polls a joystick, so hold-to-repeat is the faithful behaviour).
- CLI mirrors tetris: `--selftest`, `--no-takeover`, `--seed N`, `--dump f.pgm`,
  `--auto N` (a simple bot walking to the nearest diamond, for the test),
  stdin `tap X Y` lines when stdin is not a tty.

## 7. Tests and how to see the result

- `make rockman-data` — extractor self-check (already passes).
- `make rockman-host` — gcc build with `-Wall -Wextra -Werror`.
- `make rockman-test` — runs:
  - `./rockman-host --selftest`: map decode (16x14, only rock/dirt), per-room
    diamond totals, exit/entry consistency for all 20 rooms (every non-zero
    exit lands on a valid entry), rock fall/roll/crush, monster blocked/walks,
    trap fire rule, timer death, life loss, BCD score carry.
  - deterministic dumps with `--no-takeover --seed 1`: title, room 1, after a
    scripted tap sequence, `--auto 24` run — checked by
    `tools/check_pgm.py` plus a new `tools/check_rockman.py`.
- Look at it: `./rockman-host --dump /tmp/rockman.pgm` (or the test dumps).
- On the device: `make rockman-kindle` then `./run-rockman-on-kindle.sh <ip>`
  (copied from `run-tetris-on-kindle.sh`), and add the game to `src/menu.c`
  (the install script then pushes five games, not four).

## 8. Build order

1. Makefile: `rockman-host`, `rockman`, `rockman-kindle`, `rockman-test`,
   `.PHONY` entries.
2. Skeleton: state, `build_room`, tile renderer, e-ink loop, title screen.
3. Player: move/dig/push/die + touch rules.
4. Rocks: fall/roll/crush/explosion.
5. Monsters, traps, explosions.
6. Rooms: exits/entries, room clear, HUD (score/hi/lives/gauge), timer.
7. Title/win/game-over, restart with speed+1, hi-score.
8. Selftest + dumps + `tools/check_rockman.py`.
9. `run-rockman-on-kindle.sh`, menu entry, README, `.agent-kb` update, commit.

## 9. Verify verbatim while implementing (line refs, not guesses)

- `PlayerMove` frame branches (~587-657): the exact sealed-top-exit test and
  whether leaving through the top requires the room to be cleared.
- `MonMove` wall test (~1009-1035): the exact tile range a monster may step on
  (blocked by brick/rock, crosses dirt only while seeking) — the spec's claim,
  not yet re-read line by line.
- `LoopGame` (488-520): lock the per-pass order in section 4.
- `SpawnDiamonds` (1440-1455): the 14-entry "3 behind the rocks" table, which
  rooms it covers.
- Monster spawn seeds (~2622) and `IncSpeed` (1614-1622): exact values.
