# Rockman (1984) — rules and data extracted from `scratch/Rockman.asm`

Source: the disassembled-and-commented KickAssembler source of the original
Mastertronic VIC-20 game. All rules below are taken from that source; where the
source's own comments disagree with the code, the **code** wins (noted where it
matters).

## 1. The game in one paragraph

A Boulder-Dash-style cave game. You are Rockman, digging through dirt in a cave
of 20 rooms. Every room holds 8 diamonds (5 fixed, 3 random). Collect all 8 to
clear the room; a cleared room lets you walk out through the brick wall at a gap
in its edge into the next room. Clear all 20 rooms to win; the game then starts
again, faster, with more monsters. You have 5 lives, an 8-digit score and a
persistent hi-score. A timer column on the left climbs as time passes; if it
reaches the top you die.

## 2. Tiles (screen codes) and colours

| tile | meaning | colour |
|---|---|---|
| `$05-$0E` | digits 0-9 | cyan (timer/score labels), white (score/hi digits) |
| `$0F` | "SC" label | green |
| `$10` | "HI" label | green |
| `$12` | brick (border and walls) | purple |
| `$13` | dirt | green |
| `$14` | empty | black |
| `$15` | rock at rest | red |
| `$16-$18` | rock falling (3 stages) | red |
| `$19` | diamond | white |
| `$1A` | trap | cyan |
| `$1B/$1C` | player up/down (2 frames) | white |
| `$1D/$1E` | player left (2 frames) | white |
| `$1F/$20` | player right (2 frames) | white |
| `$21/$22` | monster A | purple |
| `$23/$24` | monster B | purple |
| `$25/$26` | monster C | purple |
| `$29-$2C` | explosion (4 stages, biggest to smallest) | cyan |

VIC colours 0-7: black, white, red, cyan, purple, green, blue, yellow.

## 3. Screen layout

- Screen is 22 columns × 15 rows of 8×16-pixel characters.
- **Room area**: rows 0-13, columns 3-18 → the map, 16 cells wide × 14 rows.
- **Timer scale**: column 1, rows 3-12: digits 0-9 drawn vertically; a flashing
  marker climbs it as time passes. When the marker reaches the top you die.
- **Score**: "SC" at row 3, column 20; the 8 score digits below it (rows 5-12).
- **Hi-score**: "HI" at row 3, column 21; 8 digits below.
- **Lives**: a digit at row 1, column 20 (tile `$05`+lives; 5 at start).
- Everything else is brick.

## 4. Rooms, exits, entry positions

- Room 0 is the title picture, rooms 1-20 playable, room 21 the win picture.
- **Map format**: 14 rows × 16 columns, two bytes per row (28 bytes per room),
  22 maps. A set bit is a **rock**, a clear bit is **dirt** (`DrawMapByte`).
  Brick is never in the map. `DrawBorderScreen` fills the whole screen with
  brick first, and the map only overwrites rows 0-13, columns 3-18. A row of
  `0xFF` is a row of rocks, not a brick wall. `DrawRoom` then seals the top
  exit (row 0, columns 10-11) with brick when column 10 was dirt.
- **Exit gaps**: the map's wall rows contain `0` bits where the player can
  leave (once the room is cleared). The gaps are in the border rows/cols of the
  map (top/bottom/left/right edges).
- **Exit table** (rooms 1-20, order down/left/up/right; 0 = no exit):

```
down : 06 07 08 00 00 0B 00 0D 0E 00 | 00 00 12 13 14 01 02 03 00 00
left : 00 00 00 03 04 00 00 00 08 09 | 0F 0B 0C 0D 0E 00 10 11 12 13
up   : 10 11 12 00 00 01 02 03 00 00 | 06 00 08 09 00 00 00 0D 0E 0F
right: 00 00 04 05 00 00 00 09 0A 00 | 0C 0D 0E 0F 0B 11 12 13 14 00
```

- **Entry positions** (where the player appears): 0 = middle (row 7, col 11);
  1 = top (row 1, col 11); 2 = right (row 7, col 18); 3 = bottom (row 13,
  col 11); 4 = left (row 7, col 3).
  - walked off bottom → enter new room at top (pos 1)
  - walked off left → enter at right (pos 2)
  - walked off top → enter at bottom (pos 3)
  - walked off right → enter at left (pos 4)
- **Leave room**: walking into a brick cell on the edge: allowed only when the
  room is cleared (all 8 diamonds). ChangeRoom reads the exit table for the
  direction walked off and places the player at the opposite side.

## 5. Main loop (per frame)

```
StirRandom; PlayTune; TimerTick
every 9th frame:  ScanRocks      (falling rocks, rolls, explosions, diamond sparkle)
every 16th frame: MoveMonsters   (all monsters step once)
CheckPlayerAlive
every 14th frame: read joystick + keyboard (player moves / pushes)
GameDelay (length depends on speed)
```

Speed starts 0 and grows each time all rooms are cleared (faster game, more
monsters).

## 6. Player movement

- Move into empty cell or dirt (digs through it, dirt becomes empty) → move.
- Facing is remembered via the tile drawn (left/right/updown frames).
- Walking into: diamond → collect; trap → TouchTrap; brick → TryLeaveRoom;
  monster → die; anything else (rock…) → blocked.
- Pushing rocks (fire button / non-direction key): the facing frame tells which
  side. Facing left: push a rock one cell left if the cell beyond is empty; the
  rock becomes falling stage 1 there. Facing right: same to the right. Facing
  up/down: push left first, then right. Pushed rock is red, falls if nothing
  under it.

## 7. Diamonds, score, lives

- Collecting a diamond: +100, a sound, the diamond cell becomes empty.
- 8th diamond in a room: room marked cleared (exits open).
- All 20 rooms cleared: +10,000 (100×100 with sounds), show the win picture
  (room 21) with rocks flashing random colours, speed+1, refill all rooms, fly
  through the rooms again and restart at room 1.
- Score: 8 decimal digits, tiles $05-$0E. +10 for crushing a monster with a
  rock. Hi-score kept for the session.
- Lives: 5 (digit tile). Death: player explodes (4 frames), one life off; if
  any left, the room restarts at the entry position; if none, game over → title.

## 8. Rocks

- A rock at rest has something solid underneath. It **rolls** left and/or right
  if the side cell is empty and the cell diagonally below it is not solid
  (empty, player or monster). If both sides are free it ends up on both sides
  (duplicated).
- A resting rock falls if the cell below is empty; 3 falling animation stages.
- Falling rock in stage 3 drops into the cell below if free, and what it lands
  on decides: empty → keeps falling; solid → becomes a resting rock; player →
  player crushed (explosion); monster → monster crushed (+10).
- Explosions: 4 stages ($29-$2C) then a falling rock tile; each stage has a
  noise value. Cyan.
- Scanning: every 9th frame, scan all 12×14 playfield cells. Rock tiles below
  $15 are ignored.

## 9. Monsters

- Spawned 2-9 of them + the same number of traps, placed at random dirt cells.
  Count = 2 + (speed AND 7).
- 3 monster types (A, B, C) with 2 animation frames each; the current frame of
  each type is stored and toggled each move.
- Every 16th frame, every monster tries to move, up to 4 tries in random
  directions (from a random source): into empty cells, onto the player (kills),
  and rarely into dirt (raster < 5). After 4 failures it stays.
- Touching a monster kills the player.

## 10. Traps

- Walking into a trap: if fewer than 4 diamonds collected in this room → die;
  with 4+ diamonds the trap is destroyed (becomes empty, with a sound).

## 11. Timer

- Column 1, rows 3-12 show digits 0-9 (cyan). A marker climbs it; when the
  top is reached time is up and the player dies.
- Timer is a 3-byte counter: timerLo counts frames; each wrap steps timerMid
  (6 passes after the first time); after 120 steps the next scale mark lights
  with a random colour and timerHi steps; when timerHi wraps → death.
- Timer resets at every room start.

## 12. Sounds

- "Sweep" zap (noise+soprano+alto) for most events; length varies by start
  value. Long noise for pauses.
- Background tune: one-voice "Popcorn" in A minor on the soprano voice, one
  note per 14 passes, each note gated for the first 9 passes. 32 notes:

```
A G A E C E A (rest) | A G A E C E A (rest)
A B C B C A B A | B G A G A F A (rest)
```

PAL pitch register values: C4=$BE E4=$CB F4=$CE G4=$D4 A4=$D9 B4=$DD C5=$DF.

- M key toggles music; music starts on.

## 13. Controls (original)

- Joystick port 1, or keys Q A O P (up/down/left/right) — fixed in this build.
- Fire button / any non-direction key = push rock. F1 = pause, F3 = new game,
  M = music toggle.

## 14. Constants

```
COLS = 22            NUM_ROOMS = 20      ROOM_END = 21
DIAMONDS_PER_ROOM = 8
TUNE_SLOT = 14       TUNE_GATE = 9       TUNE_LENGTH = 32
lives = 5            speed starts 0 (0-7 monsters, larger = faster)
Tick limits: controls 14, monsters 16, rocks 9 (passes per action)
```

## 15. Data tables (verbatim)

### Room maps (14 rows × 16 cols = 2 bytes/row, rooms 0-21)

Room 0 (title picture):

```
11110111 01110101
10010101 01000101
10110101 01000111
11100101 01000110
10110101 01000111
10010101 01000101
10010111 01110101
00000000 00000000
01111101 11011110
01010101 01010010
01010101 01010010
01000101 11010010
01000101 01010010
01000101 01010010
```

Room 1:

```
11111110 01111111
10000100 00010101
10000000 00011101
10010000 00001001
10101001 00000001
10010000 00000001
10010000 00100011
10000010 00000001
11000000 00001001
10000000 01000111
10010100 00000001
10101010 00000101
10100010 00100101
11111110 01111111
```

Room 2:

```
11111110 01111111
10101000 00000001
11100011 11000101
10000010 01001001
10100011 11000001
10000000 00000001
10010000 00001001
10100000 00000101
10000001 10000001
10000011 11000001
10100001 10000101
10100000 00000101
11011000 00011011
11111110 01111111
```

Room 3:

```
11111110 01111111
10100000 00000101
11100000 00000111
11000011 11000011
10000110 01100001
10001100 00110001
10001000 00010000
10001000 00010000
10001100 00110001
10000110 01100001
10000011 11000001
10100000 00000101
11011000 00011011
11111110 01111111
```

Room 4:

```
11111111 11111111
10000110 11000011
10000011 10010101
10010001 00000001
10100000 01000001
10000100 00000011
00010100 00010000
00000001 10000000
10010001 10001001
10000000 00000001
10000000 00000001
10100101 10100101
11011011 11011011
11111111 11111111
```

Room 5:

```
11111111 11111111
10011010 01011001
10001111 11110001
10000111 11100001
10100011 11000101
10000001 10000001
00000000 00010001
00001000 00000001
10100000 10001101
10010000 00000101
10000010 00000101
10010000 01000101
10000001 00000111
11111111 11111111
```

Room 6:

```
11111110 01111111
10000000 00000001
10110110 01101101
10010100 00101001
10110110 01101101
10011100 00111001
10110000 00001101
10001000 00010001
10000001 10000001
10000110 01100001
10010000 00000001
10011000 00000101
10010000 00001011
11111110 01111111
```

Room 7:

```
11111110 01111111
11110000 00001111
11100000 00000111
11000011 11000011
10000110 01100001
10000011 11000001
10011111 11111001
10010101 10101001
10011111 11111001
10000000 00000001
10000000 00000001
10000000 00000001
10000000 00000001
11111111 11111111
```

Room 8:

```
11111110 01111111
11101100 00110111
10111001 10011101
10010000 00001001
10010000 00001001
10010011 11001001
10010010 11001000
10010011 11001000
10000000 00000001
11001000 00010011
10000001 10000001
10010000 00001001
10101000 00010101
11111110 01111111
```

Room 9:

```
11111111 11111111
11111111 11111111
11101110 01110111
10111011 11011101
10010001 10001001
10000000 00000001
00000000 00000000
00000100 01000100
10000010 10000001
10010000 00010001
10010000 00000101
10101001 00100001
10101000 00000011
11111110 01111111
```

Room 10:

```
11111111 11111111
11111111 11111111
11011000 00011011
11110100 00101111
11100010 01000111
11000001 10000011
00000010 01000001
00000010 01000001
11000001 10000011
10001000 00010001
10001000 00010001
10010100 10101001
11111111 01111111
11111111 11111111
```

Room 11:

```
11111110 01111111
11000000 00000011
10001000 10001001
10011100 00100001
10110110 10001011
10011100 00000001
00001000 00100100
00000000 10001000
10000000 00000001
10000000 00000001
10010100 00101001
10101010 01010101
10111110 01111101
11111111 11111111
```

Room 12:

```
11111111 11111111
10110101 10111101
11111111 11111111
11000011 11000011
10000001 10001001
10100101 10100001
00000001 10000000
00000001 10001000
10100001 10000001
10000101 10100001
10000001 10000001
10010001 10000101
10000001 10000001
11111111 11111111
```

Room 13:

```
11111110 01111111
10100100 01010101
11000001 00100011
10010000 10010001
10001100 00000001
11010000 10010101
00000101 01001000
00100000 10000000
10010100 00100011
10001010 00001001
10000000 10100001
10100010 00000101
11010000 01001011
11111110 01111111
```

Room 14:

```
11111110 01111111
10000000 00000001
10000000 00000001
10011100 00111001
10010110 01101001
10011111 11111001
00000011 11000000
00000010 11000000
10011111 11111001
10010110 01101001
10011100 00111001
10000000 00000001
10000000 00000001
11111110 01111111
```

Room 15:

```
11111111 11111111
11010100 00101011
10011101 10111001
10000000 00000001
10010000 00001001
10101011 11010101
00000010 11000000
00010011 11001000
10101000 00010101
10000000 00000001
10000001 10000001
10011100 00111001
10010100 00101001
11111110 01111111
```

Room 16:

```
11111111 11111111
10101010 10011101
10010001 00110011
10000000 01100001
10100100 11001001
10001000 11000001
10100001 10001000
10000011 00010000
11010110 01000011
10000110 00000001
11001100 00101001
10011001 01010101
10110000 01111101
11111110 01111111
```

Room 17:

```
11111111 11111111
10000000 00000001
10111001 10011101
10101001 10010101
10111000 00011101
10000000 00000001
01010101 10101010
00101000 00010100
10000000 00000001
10111001 10011101
10101001 10010101
10111000 00011101
10000000 00000001
11111110 01111111
```

Room 18:

```
11111110 01111111
10011110 01111001
10100000 00000101
10000000 00000001
10011111 11111001
10100000 00000101
00000000 00000000
00011111 11111000
10100000 00000101
10000000 00000001
10011111 11111001
10100000 00000101
10000000 00000001
11111110 01111111
```

Room 19:

```
11111110 01111111
10000000 00000001
10100010 01000101
10011100 00111001
10000000 00000001
10010100 00101001
00100010 01000100
00000000 00000000
10100010 01000101
10011100 00111001
10000000 00000001
10010100 00101001
10100010 01000101
11111111 11111111
```

Room 20:

```
11111110 01111111
10010010 01001001
10010000 00001001
10010010 01001001
10010010 01001001
10010000 00001001
00010010 01001001
00010010 01001001
10010000 00001001
10010010 01001001
10010010 01001001
10010000 00001001
10010010 01001001
11111111 11111111
```

Room 21 (win picture):

```
10001011 01000100
10001010 01000100
10001010 01000100
10001011 01000100
10101010 01000100
11111010 01000100
01010011 01110111
00000000 00000000
11100111 01110111
10110101 01010100
10010101 01010110
10010101 01010100
10110101 01010100
11100111 01010111
```

### Diamond positions (5 per room, screen-relative rows/cols)

Format `(row,column)` — cell = row 0-13, column 3-18 (map columns 0-15 = screen
cols 3-18):

```
room 1:  (1,15) (4,6)  (12,6)  (12,8)  (12,17)
room 2:  (1,4)  (3,10) (3,11)  (12,5)  (12,16)
room 3:  (1,4)  (1,17) (6,10)  (12,5)  (12,16)
room 4:  (1,10) (12,5) (12,8)  (12,13) (12,16)
room 5:  (1,8)  (1,10) (1,11)  (1,13)  (11,17)
room 6:  (3,9)  (3,12) (4,7)   (4,14)  (12,16)
room 7:  (4,10) (7,7)  (7,9)   (7,12)  (7,14)
room 8:  (1,6)  (1,15) (6,10)  (12,6)  (12,15)
room 9:  (2,6)  (2,10) (2,11)  (2,15)  (12,6)
room 10: (2,5)  (2,16) (11,7)  (11,14) (12,11)
room 11: (4,7)  (11,6) (11,8)  (11,13) (11,15)
room 12: (1,4)  (1,7)  (1,9)   (1,12)  (1,17)
room 13: (1,4)  (1,17) (6,11)  (12,5)  (12,16)
room 14: (4,7)  (4,14) (7,10)  (9,7)   (9,14)
room 15: (1,7)  (1,14) (6,10)  (12,7)  (12,14)
room 16: (1,6)  (1,10) (1,17)  (11,13) (11,15)
room 17: (3,6)  (3,15) (7,10)  (10,6)  (10,15)
room 18: (1,10) (3,11) (6,10)  (9,11)  (12,10)
room 19: (4,7)  (4,14) (7,10)  (10,7)  (10,14)
room 20: (1,8)  (1,13) (12,4)  (12,10) (12,17)
```

(From the source these are raw screen addresses `cell(r,c) = $1E00 + r*22 + c`;
the port maps them onto its own grid.)

## 16. Random placement

`PlaceRandomTile(A=tile, X=colour)`: repeatedly walks the playfield interior
(12 rows × 14 cells = map rows 1-12, cols 1-14); at each cell stirs the random
accumulator; only tests a cell when the accumulator < 3; first dirt cell found
gets the tile. Used for: 3 random diamonds per room, monsters and traps.

## 17. Things the original does that a port may keep or drop

- Win picture with flashing random colours, score +10000, speed+1, restart.
- Title screen flashes dirt in random colours until a key is pressed, then
  flies through all rooms before starting.
- M toggles the tune; F1 pauses; F3 restarts.
- Explosion noise per stage; sweep sound per event.
- Monsters dig through dirt only when the raster counter is low (randomness).
