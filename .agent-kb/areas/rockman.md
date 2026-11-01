# rockman

Status: **extraction done, no code yet** — the full ruleset and all data tables
of the 1984 VIC-20 *Rockman* (Mastertronic) have been pulled out of the
commented disassembly into `docs/ROCKMAN_SPEC.md`. The Kindle port has not been
started.

## What it is

A Boulder-Dash-style cave digger on the VIC-20: 20 rooms of dirt, 8 diamonds
per room (5 fixed + 3 random), rocks that fall/roll/crush, wandering monsters
(2-9, count = 2 + speed&7), traps (kill unless you hold 4 diamonds in the
room), a climbing timer that kills you, 5 lives, 8-digit score + hi-score.
Clear all 20 rooms → +10000, faster speed, replay.

Target port shape (follow the established pattern): single C file, shared e-ink
engine (`EK_HOST` for PC), 600x800 Kindle Touch, tap buttons, `run-rockman-on-kindle.sh`.

## Source of truth

- `scratch/Rockman.asm` — commented KickAssembler source (2959 lines), from a
  VICE snapshot + original tape. Read this (or the spec) for any rule question.
- `scratch/Rockman-unmodified.asm` — same source before this workspace touched
  it (backup).
- `docs/ROCKMAN_SPEC.md` — rules + data extracted this session; use it to build
  the port without re-reading the asm.
- `scratch/rockman.prg` — the assembled original binary (for reference).

## Rules extracted (key facts; full detail in the spec)

- Grid: 14 rows × 16 cols per room. Map bit **1 = wall/rock, 0 = dirt** — the
  source's *comments* say "1 = rock, 0 = dirt" but the *data* (walls are
  `%11111111`) and DrawRoom's sealing logic prove 1 = wall. The walls that the
  player walks out through are real brick tiles on the map edges.
- Exits: `exits` table (down/left/up/right per room, 0 = none); leaving is only
  allowed when the room is cleared. Entry positions: 0 middle, 1 top, 2 right,
  3 bottom, 4 left.
- Rocks: rest → roll left/right if side free + diagonal below not solid (can
  duplicate on both sides); fall 3 stages; landing on player = death, monster =
  +10; crushed things explode 4 stages ($29-$2C, cyan).
- Monsters move every 16th pass, up to 4 random-direction tries, into empty /
  onto player / rarely dirt.
- Timer: 3-byte counter, ~120 steps per mark; hits top → die. Resets per room.
- Tune: one-voice "Popcorn" (A minor), note per 14 passes, gate 9, 32 notes.
- Tick limits: controls 14, monsters 16, rocks 9 (passes per action).
- Constants: COLS 22 (screen), NUM_ROOMS 20, DIAMONDS_PER_ROOM 8, lives 5,
  speed 0 → faster per full clear.

## Full data (in the spec, verbatim from the asm)

- All 22 room maps (room 0 = title picture, 21 = win picture), 14×16 bits.
- The 5 fixed diamond (row,col) per room — all 20 rooms.
- The exits table, start positions, tune notes, tick limits.

## Known quirks / gotchas for the port

- The room maps are drawn onto a screen that is *already* filled with brick;
  only the 14×16 playfield cells are overwritten from the map. The port can
  just render the map + a brick border.
- DrawRoom seals top-wall gaps with brick (rooms whose top row has dirt in the
  exit cells). Not needed for gameplay (the exit logic uses the tile), but it's
  what the original shows.
- Diamonds are placed by screen address in the original (`cell(r,c)`); the
  spec lists them as (row, col) in map coordinates — row 0-13, col 3-18
  (screen columns), i.e. map col = screen col - 3. Careful when porting.
- The 8x16 char tiles are stored in `scratch/Rockman.asm` at `charset:` (~line
  1763) — tile $11 is unused/free, tiles $27/$28 hold ClassifyTile in the
  original memory layout (irrelevant for a rewrite).
- Player faces are encoded in the tile drawn (L/R/UD frames) and PushRock reads
  the player's own tile to decide which side to push.
