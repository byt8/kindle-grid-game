# rockman

Status: **analysis complete, implementation plan written, no game code yet**.
Ruleset `docs/ROCKMAN_SPEC.md`; data machine-extracted to `src/rockman_data.h`
by `tools/extract_rockman.py` (`make rockman-data` / `--check` passes);
**build plan `docs/ROCKMAN_PLAN.md`** — read that before writing
`src/rockman.c`; it carries the spec corrections (§1), the decided port design
(§2-§6), the build order (§8) and the open verifications (§9).

## What it is

A Boulder-Dash-style cave digger on the VIC-20: 20 rooms of dirt and rocks,
rocks that fall/roll/crush, wandering monsters, traps, a climbing timer that
kills you, 5 lives, 8-digit score + hi-score. Diamonds per room: 5 fixed + 3
behind rocks in the rooms named by the table (rooms 1-14; 15-20 have 5 —
assert in the selftest). Clear all 20 rooms → win picture, lives restored,
room 0, speed +1, replay.

Port shape (follow the established pattern): single `src/rockman.c`, shared
e-ink engine (`EK_HOST` for PC), 600x800 Kindle Touch, tap buttons,
`run-rockman-on-kindle.sh`, menu entry (the install script then pushes five
games, not four). No audio.

## Source of truth

- `scratch/Rockman.asm` — commented KickAssembler source (2959 lines). Read
  this for any rule question; the spec paraphrases it.
- `docs/ROCKMAN_PLAN.md` — the reviewed build plan (2026-11-01).
- `docs/ROCKMAN_SPEC.md` — rules + data extracted earlier; **two sections are
  wrong, see Corrections below**.
- `tools/extract_rockman.py` → `src/rockman_data.h` — generated, never
  hand-edit (`make rockman-data`, self-checks with `--check`).

## Corrections to the spec (settled from the asm this session)

- **Map bit 1 = rock, 0 = dirt** (`DrawMapByte`, 1494-1512: set bit →
  TILE_ROCK/red, clear → TILE_DIRT/green). The earlier "1 = wall" reading was
  wrong: brick is never on a map — `DrawBorderScreen` (1432) fills all 330
  cells with brick, `DrawRoomMap` overwrites only rows 0-13 × cols 3-18 with
  rock/dirt. So `RotAt`'s first test is "is there any tile here at all"
  (anything < TILE_BRICK), not "is it a wall".
- `CELL_UP/HERE/DOWN/LEFT/RIGHT` = **screen-buffer offsets** +22/44/66/43/45
  from `ptr` (which sits 2 rows above the cell), reused as direction codes in
  `TryLeaveRoom`, `ChangeRoom`, `mmDirOffset`. Not screen values.
- Screen layout (asm header 107-112): room rows 0-13 × cols 3-18; timer scale
  col 1 rows 3-12; lives col 20 row 1; "SC" col 20 row 3; score col 20 rows
  5-12; "HI" col 21 row 3; hi-score col 21 rows 5-12; everything else brick.
- Top exit = map row 0, cols 10-11 (dirt); `DrawRoom` (1460-1472) overwrites
  exactly those two cells with brick to seal them.
- No input freeze after death: no delay routine exists on the restart path.
- Timer: 9 marks × 120 steps × 6 passes per mark (TimerTick 1701-1729) ≈ 100 s
  per room at base speed.

## Verified facts for the build

- Pass ≈ 16 ms at speed 0; `GameDelay` (1479-1489) runs X = 256-speed × 9
  inner loops → period = 16 ms × (256-speed)/256. Controls tick every 14
  passes, monsters 16, rocks 9 (`rm_tick_limits`).
- Tile art reads cleanly in 1 bit (ASCII-checked against `rm_tiles`): brick =
  brick pattern, dirt = 50% checker, empty = blank, rock = solid blob,
  diamond = hollow diamond, digits = outlined w/ dither fill, glyphs solid.
  Port renders bits → ink on paper (inverse of the VIC's black background).
- Diamonds are placed by screen address in the original; map col = screen
  col − 3 (`cell(r,c)` in the spec).
- Per-pass order in `LoopGame` (488-520) still needs to be locked verbatim
  during the build (plan §4 proposes one).

## Open verifications (plan §9, do these line by line while implementing)

- `PlayerMove` frame branches (~587-657): sealed-top-exit test, and whether
  leaving through the top requires the room cleared (KB previously assumed
  "any exit needs clearance"; the ChangeRoom comment only states it for the
  top).
- `MonMove` wall test (~1009-1035): exact tile range a monster may enter.
- `SpawnDiamonds` (1440-1455) 14-entry behind-rocks table, `SpawnEnemies`
  seeds (~2622), `IncSpeed` wrap (1614-1622).

## Gotchas

- Score is 4 BCD bytes (8 digits) with carry → hi-score takeover; same adders
  for player and hi: 100/diamond, 300 + 25×steps-left/room, 1000/room,
  5000/life.
- `PushRock` reads the player's own tile to decide which side to push; the
  7-tick count lives in `$0369`.
- Room maps are drawn onto an already-brick screen; room 0 = title picture,
  21 = win picture, both plain maps.
- Tile $11 is unused (`.fill 16,0`); extractor covers $05-$26 + $29-$2C as
  `rm_tiles[0x2D][16]`; each room block needs its `// room N` comment line and
  `.fill` honoured (or the next `.byte` block leaks into the tile).
