// ======================================================================
//  ROCKMAN  -  Commodore VIC-20 (unexpanded)   (C) 1984 Mastertronic
//  Disassembled from a VICE snapshot of the running game plus the original
//  tape image (Rockman_0017.tap), and commented for KickAssembler.
// ======================================================================
//
//  THE GAME
//    A Boulder-Dash style cave game.  You are Rockman and you dig through the
//    dirt of a cave made of 20 rooms.  Every room holds 8 diamonds (5 in fixed
//    places and 3 at random).  Collect all 8 to clear the room, which lets you
//    leave it by walking into the brick wall at a gap in its edge.  Clear all
//    20 rooms to win; the game then starts again, faster and with more enemies.
//
//    * Rocks lie in the dirt.  A rock with nothing solid under it falls, rolls
//      off other rocks, and crushes whatever it lands on (you, or a monster for
//      10 points).  You can push a rock sideways with the fire button.
//    * Monsters ($21-$26) wander about, now and then digging through the dirt;
//      touching one kills you.
//    * Traps ($1A) kill you too, unless you already hold 4 diamonds in the room:
//      then walking into one destroys it.
//    * A diamond scores 100.  A timer column (digits 0-9 on the left of the
//      screen) has a marker that climbs it as time passes; if it reaches the
//      top you die.
//    * You have 5 lives.  The score is 8 digits; the hi-score is kept until the
//      computer is switched off.
//
//  CONTROLS    Joystick (port 1) or the four direction keys.  The fire button or
//              any other key pushes a rock.  F1 = pause, F3 = restart the game.
//              The original tape loader asked which four keys to use; this build
//              has them fixed in 'defaultKeys' at the end of the file (Q A O P).
//
//  BUILDING THIS FILE
//    This is one single program that loads at $1001 and starts with 'SYS 8114'.
//    The original was a BASIC loader plus a 3.5K machine-code file that expected
//    three small blocks of code to have been put in place by the loader (see the
//    comment above 'Init' at the end of the file).  The game code itself ($100D-$1DFF)
//    is exactly the original tape file, byte for byte.
//
//  MEMORY MAP (unexpanded VIC-20)
//    $0118-$01C1   block A: SpawnEnemies, trap and diamond touch, PauseGame, PlayTune, tune
//    $02A2-$02FF   block B: the room timer
//    $0243-$03FF   game variables (see the .label list below), and block C
//    $0356-$03FF   block C: MoveMonsters, with its two direction tables
//    $1001-$100C   BASIC stub
//    $100D-$1031   sound effects
//    $1032-$10B5   title screen and fly-through, room diamonds
//    $10B6-$1148   collecting diamonds, leaving a room, game won
//    $114B-$119D   random placement of tiles
//    $119E-$1271   rock rolling, player death, pushing rocks
//    $1272-$1370   the scan for rocks, diamonds and explosions
//    $1371-$13FF   random numbers, delays, digit columns, timing ticks
//    $1400-$14AF   start-up, main loop, keyboard and joystick
//    $14B0-$1557   player movement
//    $155A-$15E6   changing rooms, game initialisation
//    $15E7-$1657   drawing the player, animation frames, start positions
//    $1658-$16FD   code relocation, score
//    $1704-$17A5   pointer arithmetic, screen set-up, drawing tiles
//    $17A6-$17FF   room map drawing, map address tables
//    $1800-$184F   room exit tables
//    $1850-$1ACF   the 8x16 character set (with two routines hidden in it)
//    $1AD0-$1B97   positions of the five fixed diamonds in each room
//    $1B98-$1DFF   22 room maps
//    $1E00-$1FFF   screen memory (22 x 15 = 330 bytes used)
//                   -> before the game starts: the three relocated blocks and Init
//    $9600-$97FF   colour memory
//
//  VIDEO SETUP
//    The screen is 22 columns x 15 rows of 8x16 pixel characters ($9003 = $1F).
//    Screen memory is at $1E00 and the character set at $1800 ($9005 = $FE), so
//    character N is the 16 bytes at $1800 + N*16.  The tiles in the table below
//    are those screen codes.
//
//  SCREEN LAYOUT
//    rows 0-13, columns 3-18    the room: 16 x 14 cells
//    column 1, rows 3-12        the timer scale (digits 0-9) with a flashing marker
//    column 20                  row 1: lives (a digit), row 3: "SC", rows 5-12: score
//    column 21                  row 3: "HI",  rows 5-12: hi-score
//    everything else            brick wall
//
//  TILES (screen codes)
//    $05-$0E digits 0-9       $0F "SC"  $10 "HI"      $12 brick   $13 dirt   $14 empty
//    $15 rock                 $16-$18 rock falling (3 stages)
//    $19 diamond              $1A trap
//    $1B-$1C Rockman up/down  $1D-$1E left   $1F-$20 right
//    $21-$22 monster A        $23-$24 monster B     $25-$26 monster C
//    $29-$2C explosion (4 stages)
//
//  FOR DEV20:  Rockman.chr (next to this file) holds the character set in the
//  Dev20 .chr format, so it can be edited in the character editor.  Because the
//  game uses 8x16 characters, each 16-byte tile is stored as two consecutive 8x8
//  characters: tile N = Dev20 characters 2N (top half) and 2N+1 (bottom half).
//  Dev20's .chr files hold 128 characters, which is exactly the 1024 bytes from
//  $1800 to $1BFF.  Only the tiles listed above contain graphics; the rest is
//  zero because in the original that space holds tables and code.
//
// ---- Hardware and system addresses -----------------------------------
.label SCREEN        = $1E00   // screen memory (22 x 15 cells, one 8x16 character each)
.label COLOURRAM     = $9600   // colour memory for a screen at $1E00
.label VIC_RASTER    = $9004   // raster line (used as a source of random numbers)
.label VIC_BASS      = $900A   // bass voice  ($80+ = on)
.label VIC_ALTO      = $900B   // alto voice
.label VIC_SOPRANO   = $900C   // soprano voice
.label VIC_NOISE     = $900D   // noise voice
.label VIC_VOLUME    = $900E   // volume (bits 0-3) and auxiliary colour
.label VIC_COLOURS   = $900F   // background / border colour
.label VIA1_PORTA    = $9111   // joystick: bit2 up, bit3 down, bit4 left, bit5 fire (active low)
.label VIA1_DDRA     = $9113   // data direction register for port A
.label VIA2_PORTB    = $9120   // joystick RIGHT is bit 7 (active low)
.label VIA2_DDRB     = $9122   // data direction register for port B
.label CHROUT        = $FFD2   // KERNAL: print a character
.label GETIN         = $FFE4   // KERNAL: get a key from the keyboard buffer
.label IERROR        = $0300   // BASIC error-message vector
.label ISTOP         = $0328   // KERNAL STOP-key test vector
.label KEYREPEAT     = $028A   // key repeat: $80 = all keys repeat
.label SHFLAG        = $0291   // $80 = SHIFT / C= character-set switching disabled

// ---- Zero page (the game uses these freely) --------------------------
// Two pointers into the screen are used everywhere.  A pointer 'p' is always set to
// (cell address - 44), i.e. two rows above the cell it is working on, so the
// neighbours of the cell can be read with  lda (p),y  using the CELL_ offsets below.
.label ptr           = $00     // $00/$01  scan pointer (rocks, monsters, map drawing)
.label ptr2          = $02     // $02/$03  second pointer (map data / random-direction source)
.label tmp           = $04     // scratch
.label tuneTick      = $3B     // frame counter for the background tune
.label tunePos       = $3C     // position in the tune
.label colPtr        = $FB     // $FB/$FC  colour-RAM version of a screen pointer
.label plyPtr        = $FD     // $FD/$FE  pointer to the player (player cell = plyPtr + 44)

// ---- Game variables (page 2 and 3) -----------------------------------
.label roomFlags     = $0243   // [room 1..20]: 1 = room not yet cleared, 0 = cleared
.label keyUp         = $0258   // keys chosen by the player ('move up', 'down', 'left', 'right')
.label keyDown       = $0259
.label keyLeft       = $025A
.label keyRight      = $025B
.label inputTick     = $025D   // counts main-loop passes up to 14 between reads of the controls
.label monsterTick   = $025E   // counts main-loop passes up to 16 between moves of the monsters
.label rockTick      = $025F   // counts main-loop passes up to 9 between scans for falling rocks
.label diamonds      = $0260   // diamonds collected in the current room
.label randAcc       = $0261   // running 'random' accumulator
.label entryPos      = $0262   // which start position the player entered the room at (0-4)
.label timerLo       = $026C   // three-byte room timer
.label timerMid      = $026D
.label timerHi       = $026E
.label addTmp        = $033C   // scratch for AddToZP / SubFromZP
.label room          = $033D   // current room (0 = title picture, 1-20 = playable, 21 = end picture)
.label tileColour    = $033F   // colour to write with the next tile
.label speed         = $0340   // game speed; larger = shorter delay per frame
.label lives         = $0341   // lives left, stored as a digit tile (TILE_0 = none left)
.label scoreDigits   = $0342   // 8 digit tiles, most significant first ($0342-$0349)
.label hiDigits      = $034A   // 8 digit tiles of the hi-score ($034A-$0351)
.label animFrame     = $034F   // animFrame+3/+4/+5 = tile each monster type is currently showing

// ---- Colours ---------------------------------------------------------
.const COL_BLACK = 0
.const COL_WHITE = 1
.const COL_RED = 2
.const COL_CYAN = 3
.const COL_PURPLE = 4
.const COL_GREEN = 5
.const COL_BLUE = 6
.const COL_YELLOW = 7

// ---- Tile (screen code) numbers -------------------------------------
// The character set is 8x16, so each tile is 16 bytes at $1800 + tile*16.
.const COLS            = 22
.const TILE_0          = $05   // $05-$0E are the digits 0-9
.const TILE_SC         = $0F   // "SC" label above the score
.const TILE_HI         = $10   // "HI" label above the hi-score
.const TILE_BRICK      = $12
.const TILE_DIRT       = $13
.const TILE_EMPTY      = $14
.const TILE_ROCK       = $15
.const TILE_FALL1      = $16   // $16-$18: a rock tumbling through three stages
.const TILE_FALL2      = $17
.const TILE_FALL3      = $18
.const TILE_DIAMOND    = $19
.const TILE_TRAP       = $1A
.const TILE_PLAYER_UD  = $1B   // $1B/$1C facing up or down
.const TILE_PLAYER_L   = $1D   // $1D/$1E facing left
.const TILE_PLAYER_R   = $1F   // $1F/$20 facing right
.const TILE_MONSTER_A  = $21   // $21/$22
.const TILE_MONSTER_B  = $23   // $23/$24
.const TILE_MONSTER_C  = $25   // $25/$26
.const TILE_MONSTER_D  = $27   // $27/$28 (never drawn: the space holds ClassifyTile)
.const TILE_EXPLODE1   = $29   // $29-$2C explosion stages
.const KEY_F1          = $85
.const KEY_F3          = $86
.const JOY_UP          = $04
.const JOY_DOWN        = $08
.const JOY_LEFT        = $10
.const JOY_FIRE        = $20
.const JOY_RIGHT       = $80
.const NUM_ROOMS       = 20
.const ROOM_END        = 21
.const DIAMONDS_PER_ROOM = 8

// Offsets (from a pointer 'p' = cell address - 44) of the cell and its neighbours
.const CELL_UP         = $16   // 22 bytes on from p      = the cell above the player
.const CELL_LEFT       = $2B
.const CELL_HERE       = $2C   // 44 bytes on from p      = the cell itself
.const CELL_RIGHT      = $2D
.const CELL_DOWN       = $42   // 66 bytes on from p      = the cell below

// Screen address of the cell in row r, column c
.function cell(r,c) { .return SCREEN + r*COLS + c }
.pc = $1001 "Rockman"

// BASIC stub:   0 SYS <Init>
    .word $100B                     // pointer to the next BASIC line (= the end-of-program marker)
    .word 0                         // line number 0
    .byte $9E                       // SYS token
    .text "8114"                    // = Init (checked by the .errorif at Init)
    .byte $00                       // end of line
    .word $0000                     // end of program

// ======================================================================
// SOUND
// ======================================================================
// SfxDefault / SfxSweep
// Plays a short "zap" sound effect and then (by falling into Silence) switches
// all the voices off again.  A = start value for the noise voice: the smaller it
// is, the longer the effect lasts (the noise register is stepped up to 0).
// Used for nearly every sound in the game; the A values are just different lengths.
SfxDefault:
    lda #$F9                      // A = $F9: the standard short effect
SfxSweep:
    sta VIC_NOISE                 // start value of the noise voice
sfx_outer:
    ldx #$DC                      // soprano voice starts at $DC ...
sfx_inner:
    ldy #$AF                      // ... alto voice (the buzz) starts at $AF and slides down to 0
sfx_tone:
    sty VIC_ALTO                  // write the alto voice
    dey                           // next lower pitch
    bne sfx_tone
    stx VIC_SOPRANO               // soprano steps up one notch ...
    inx                           // ... until it wraps ($DC..$FF, 36 steps)
    bne sfx_inner
    inc VIC_NOISE                 // then step the noise voice up; stop when it wraps to 0
    bne sfx_outer

// Silence
// Switches off the bass, alto, soprano and noise voices.
Silence:
    ldx #$04                      // 4 voices: $900A-$900D
    lda #$00
silence_loop:
    sta VIC_BASS-1,x              // VIC_BASS-1,x : x = 4..1 = $900D..$900A
    dex
    bne silence_loop
    rts

// ======================================================================
// TITLE SCREEN AND FLY-THROUGH
// ======================================================================
// IntroScreen
// Draws the border screen (with room 0 - the title picture - in the middle)
// and flashes the dirt colour at random until a key is pressed.  It then flicks
// through the maps of rooms 1-20 (with their diamonds) as a quick preview.
IntroScreen:
    jsr Silence
    jsr DrawBorderScreen
    lda #$64                      // long sweep
    jsr SfxSweep
intro_flash:
    jsr Random7                   // flash loop: random colour 1-7 for the dirt ...
    sta dirtColour                // ... (self-modified colour, see DrawMapByte)
    jsr DrawRoomMap               // redraw the map of the current room (room 0 = title picture)
    lda #$FC
    jsr SfxSweep                  // short sweep as a delay
    jsr GETIN                     // any key pressed?
    beq intro_flash
    lda #COL_GREEN                // restore the normal dirt colour (green)
    sta dirtColour
intro_fly:
    lda #$EB                      // fly-through: delay ...
    jsr SfxDelay
    inc room                      // ... next room
    jsr DrawRoomMap               // draw its map and its fixed diamonds
    jsr PlaceRoomDiamonds
    lda room
    cmp #NUM_ROOMS                // until room 20 has been shown
    bcc intro_fly
    clc
    jmp Silence                   // turn the sound off and return

// PlaceRoomDiamonds
// Starts a room: resets the tune and the diamond counter, and (if the room has
// not been cleared yet) puts the room's 5 fixed diamonds on the screen plus 3 more
// at random places.  A room is cleared by collecting all 8 (DIAMONDS_PER_ROOM).
PlaceRoomDiamonds:
    lda #$00                      // tune and diamond counter back to 0
    sta tuneTick
    sta tunePos
    sta diamonds
    ldx room                      // has this room already been cleared?
    lda roomFlags,x
    bne prd_draw
    rts                           // cleared: no diamonds
prd_draw:
    lda room                      // X = room * 10: each room has 5 screen addresses (10 bytes) in diamondPos
    asl
    asl
    asl
    clc
    adc room
    clc
    adc room
    tax
    lda #$FB                      // 5 fixed diamonds (tmp counts $FB..$FF)
    sta tmp
prd_loop:
    jsr PlaceFixedDiamond
    inx
    inx
    inc tmp
    bne prd_loop
    jsr PlaceRandomDiamonds       // plus 3 random ones
    rts

// PlaceFixedDiamond
// X = offset of one of the room's 5 diamond screen addresses in diamondPos.
// Draws a white diamond tile at that address.
PlaceFixedDiamond:
    lda diamondPos-10,x           // fetch the screen address of the diamond ...
    sta ptr
    lda diamondPos-9,x
    sta ptr+1
    lda #COL_WHITE                // white
    sta tileColour
    lda #TILE_DIAMOND
    ldy #$00                      // Y = 0: the pointer is the cell itself here
    jmp PutTile                   // store tile and colour

// CollectDiamond
// The player has walked into a diamond.  Y = offset of the diamond's cell.
// Scores 100, makes a sound and removes the diamond.  When the 8th diamond of the
// room is collected the room is marked cleared (which opens its exits); if that was
// the last uncleared room the game has been won (AllRoomsCleared).
CollectDiamond:
    tya                           // keep the cell offset
    pha
    jsr ScoreAdd100               // +100 points
    lda #$FE                      // sound
    jsr SfxSweep
    pla                           // get the cell offset back
    tay
    lda #$00                      // colour black
    sta tileColour
    lda #TILE_EMPTY               // remove the diamond (draw an empty cell)
    jsr PutTileAtPlayer
    inc diamonds                  // one more diamond collected in this room
    lda diamonds
    cmp #DIAMONDS_PER_ROOM
    bcc cd_rts                    // fewer than 8: carry on
    clc
    ldx room
    lda #$00
    sta roomFlags,x               // room cleared: clear its flag
    ldx #NUM_ROOMS
cd_check:
    lda roomFlags,x               // are all 20 rooms cleared now?
    bne cd_rts
    dex
    bne cd_check
    jsr AllRoomsCleared           // yes - game won
cd_rts:
    rts

// TryLeaveRoom
// The player has walked into a brick cell on the edge of the playfield.  If the
// room has been cleared the player may walk out through it into the next room.
// Y = offset of the cell that was hit (it tells ChangeRoom which way the player went).
TryLeaveRoom:
    ldx room
    lda roomFlags,x               // room still has diamonds in it?
    bne tlr_rts
    jsr ChangeRoom                // no: change room
tlr_rts:
    rts

// AllRoomsCleared
// The game has been won.  Awards 10,000 points (100 x 100 points, with a sound for
// each), then shows the final picture (room 21) with its rocks flashing in random
// colours.  The game then starts again, faster (speed+1), with all rooms refilled.
AllRoomsCleared:
    jsr Silence
    lda #$9C                      // 100 passes
    sta ptr
acr_loop:
    jsr ScoreAdd100               // +100 points ...
    jsr SfxLong                   // ... and a sound
    inc ptr
    bne acr_loop
    lda #$80                      // long final sound
    jsr SfxDelay
    lda #ROOM_END                 // room 21 = end-of-game picture
    sta room
    lda #$B0                      // 80 flashes (tuneTick used as the counter)
    sta tuneTick
acr_flash:
    jsr Random7                   // random colour for the rocks of the picture
    sta rockColour                // (self-modified colour, see DrawMapByte)
    jsr DrawRoomMap               // draw the picture
    lda #$E9
    jsr SfxDelay
    inc tuneTick
    bne acr_flash
    lda #$02                      // back to the normal rock colour (red)
    sta rockColour
    lda #$C8
    jsr SfxSweep
    lda speed                     // game gets faster: shorter delay in GameDelay, more enemies in SpawnEnemies
    clc
    adc #$01
    sta speed
    jsr InitRoomFlags             // all 20 rooms need clearing again
    lda #$01                      // fly through the rooms again, then start in room 1
    sta room
    jsr intro_fly
    jmp sp_room1

// PlaceRandomTile
// A = tile, X = colour.  Puts the tile at a random place that currently holds dirt.
// It walks over the middle of the playfield (12 rows of 14 cells) again and again;
// at every cell it stirs the random accumulator and only tests the cell when the
// accumulator is below 3, so the cell finally chosen is effectively random.
// Here ptr is the address of the cell itself.
PlaceRandomTile:
    pha
    stx tileColour                // colour for PutTile
prt_restart:
    lda #$1A                      // start at row 1, column 4 ($1E1A) ...
    sta ptr
    lda #$1E
    sta ptr+1
    lda #$F4                      // 12 rows (counted up to 0 in ptr2) ...
    sta ptr2
prt_row:
    lda #$F2                      // ... of 14 cells (counted in ptr2+1)
    sta ptr2+1
prt_cell:
    jsr StirRandom                // stir the random number
    lda randAcc
    cmp #$03                      // below 3 (about 1 in 85): look at this cell
    bcc prt_test
prt_next:
    clc                           // next cell
    inc ptr
    bne prt_step
    inc ptr+1
prt_step:
    inc ptr2+1
    bne prt_cell
    ldx #$00                      // end of row: skip the 8 cells that are not part of it
    lda #$08
    jsr AddToZP
    inc ptr2
    bne prt_row
    jmp prt_restart               // nothing found in a whole pass: start again
prt_test:
    ldy #$00                      // is the cell dirt?
    lda (ptr),y
    cmp #TILE_DIRT
    bne prt_next
    pla                           // yes: draw the tile
    jmp PutTile

// PlaceRandomDiamonds
// Puts 3 diamonds at random places.
PlaceRandomDiamonds:
    lda #$FD                      // 3 passes ($FD,$FE,$FF)
    sta tmp
prd2_loop:
    ldx #COL_WHITE
    lda #TILE_DIAMOND
    jsr PlaceRandomTile
    inc tmp
    bne prd2_loop
    rts

// ======================================================================
// ROCK PHYSICS AND PUSHING
// ======================================================================
// RockRoll
// A rock at rest has something solid underneath it.  It rolls off to the left and/or
// the right if that side is free (see RockRollSide).
RockRoll:
    ldy #CELL_LEFT                // try the left ...
    jsr RockRollSide
    ldy #CELL_RIGHT               // ... and the right
    jsr RockRollSide
    jmp sr_next

// RockRollSide
// Y = CELL_LEFT or CELL_RIGHT.  The rock rolls to that side if the side cell is
// empty and the cell diagonally below it is not solid (empty, player or monster).
// (If both sides are free the rock ends up on both sides.)
RockRollSide:
    sty tmp                       // remember the side
    lda (ptr),y                   // what is in the side cell?
    jsr ClassifyTile
    cmp #$01                      // must be empty
    bne rrs_rts
    lda tmp                       // look at the cell below the side cell
    clc
    adc #COLS                     // one row down
    tay
    lda (ptr),y
    jsr ClassifyTile
    bne rrs_roll                  // not solid: roll
rrs_rts:
    rts
rrs_roll:
    ldy #CELL_HERE                // empty the rock's own cell ...
    ldx #$00
    stx tileColour
    lda #TILE_EMPTY
    jsr PutTile                   // (PutTile with Y = CELL_HERE)
    ldy tmp                       // ... and put a resting rock in the side cell
    jmp PutRockFrameX0

// ======================================================================
// PLAYER DEATH
// ======================================================================
// PlayerDies
// Shows the player exploding (4 frames), takes away a life and, if any are left,
// restarts the current room at the position the player came in at.  With no lives
// left the game starts again from the title screen.
PlayerDies:
    jsr Silence
    lda #TILE_EXPLODE1            // first explosion frame
pd_explode:
    pha                           // draw it over the player in white ...
    jsr DrawTileWhite
    lda #$C8                      // ... and wait a little
    jsr SfxDelay
    pla
    clc
    adc #$01
    cmp #TILE_EXPLODE1+4          // 4 frames ($29-$2C)
    bcc pd_explode
    clc
    lda #$AF                      // sound
    jsr SfxSweep
    ldx lives                     // one life less
    dex
    stx lives
    cpx #TILE_0                   // lives counter has reached '0'?
    beq pd_gameover               // yes: game over
    ldx entryPos                  // restart the room at the entry position
    jsr EnterRoom
    lda lives                     // redraw the lives digit
    sta SCREEN+$2A
    lda #$BE                      // pause
    jmp SfxDelay
pd_gameover:
    jmp NewGame                   // game over: new game

// SetPlayerPtr
// A = high byte of the player pointer (the low byte is already in plyPtr),
// X = entry position number, kept in entryPos so the room can be restarted.
SetPlayerPtr:
    sta plyPtr+1
    stx entryPos
    rts

// PushRock
// Fire button (or any key that is not a direction key).  Pushes a rock sideways if the
// player is facing it: the frame the player is showing tells which way he faces.
// When he is facing up or down he pushes on both sides.  The pushed rock becomes the
// first 'falling' frame in the next cell, so it falls if there is nothing under it.
PushRock:
    ldy #CELL_HERE                // the player's own tile
    lda (plyPtr),y
    cmp #TILE_PLAYER_R
    beq PushRight
    cmp #TILE_PLAYER_R+1
    beq PushRight
    cmp #TILE_PLAYER_L
    beq PushLeft
    cmp #TILE_PLAYER_L+1
    beq PushLeft
    jsr PushLeft                  // facing up/down: push left, then right
    ldy #CELL_HERE
    jmp PushRight

// PushLeft
// Rock in the cell to the left and an empty cell beyond it?
PushLeft:
    dey                           // cell to the left
    lda (plyPtr),y
    cmp #TILE_ROCK
    bne push_rts
    dey                           // cell beyond
    lda (plyPtr),y
    cmp #TILE_EMPTY
    bne push_rts
    ldx #$02                      // red
    stx tileColour
    lda #TILE_FALL1               // the rock becomes a falling rock one cell further left ...
    jsr PutTileAtPlayer
    iny                           // back to the rock's old cell

// PushClear
// Empties the cell the rock came from (black, empty tile).
PushClear:
    ldx #$00
    stx tileColour
    lda #TILE_EMPTY
    jmp PutTileAtPlayer

// PushRight
// Same as PushLeft but to the right.
PushRight:
    iny
    lda (plyPtr),y
    cmp #TILE_ROCK
    bne push_rts
    iny
    lda (plyPtr),y
    cmp #TILE_EMPTY
    bne push_rts
    ldx #$02
    stx tileColour
    lda #TILE_FALL1
    jsr PutTileAtPlayer
    dey
    jsr PushClear
push_rts:
    rts

// ======================================================================
// SCANNING THE PLAYFIELD FOR ROCKS (every 9th frame)
// ======================================================================
// SparkleDiamond
// Gives the diamond in the current cell a new random colour (so they glitter).
SparkleDiamond:
    jsr Random7
    jsr PutColour
    jmp sr_next

// RockAtRest
// A resting rock.  Dirt under it holds it up.  If the cell below is empty, the
// player or a monster the rock starts to fall; if it is something solid it rolls.
RockAtRest:
    ldy #CELL_DOWN                // the cell below
    lda (ptr),y
    cmp #TILE_DIRT                // dirt: stays put
    beq rar_done
    jsr ClassifyTile              // classify the cell below
    bne rar_fall                  // not solid: the rock begins to fall
    jmp RockRoll                  // solid: try to roll off sideways

// RockStartFall / PutRockFrameHere / PutRockFrame
// X = stage of the rock (0 = at rest, 1-3 = falling stage 1-3).  Writes the tile of
// that stage, in red, to the cell at offset Y from ptr (CELL_HERE by default) and makes
// the bass voice play the stage's pitch.
RockStartFall:
    ldx #$01                      // stage 1
PutRockFrameHere:
    ldy #CELL_HERE                // the rock's own cell
PutRockFrame:
    lda rockSounds,x              // bass pitch ...
    sta VIC_BASS
    lda rockFrames,x              // ... and the tile for this stage
    ldx #$02                      // red
    stx tileColour
    jmp PutTile
rar_fall:
    jsr RockStartFall             // begin to fall
rar_done:
    jmp sr_next

// These two tables are indexed by the rock stage.
rockFrames:   .byte TILE_ROCK,TILE_FALL1,TILE_FALL2,TILE_FALL3   // tile for rock stage 0-3 (at rest, falling 1-3)
rockSounds:   .byte $00,$D2,$D7,$DC                                // bass pitch for each stage

// RockAnimate
// Moves a falling rock on to its next animation stage (in the same cell).
RockAnimate:
    jsr PutRockFrameHere
    jmp sr_next

// PutRockFrameX0
// Puts a resting rock (stage 0) into the cell at offset Y.
PutRockFrameX0:
    ldx #$00
    jmp PutRockFrame

// RockFrame2
// Tile $16 (stage 1) -> stage 2.
RockFrame2:
    ldx #$02
    jmp RockAnimate

// RockFrame3
// Tile $17 (stage 2) -> stage 3.
RockFrame3:
    ldx #$03
    jmp RockAnimate

// ScanRocks
// Runs every 9th frame.  Looks at every cell of the playfield (12 rows x 14
// columns, starting at the bottom right and working left and up so a rock can only
// move one cell per scan) and updates what needs it:
//   diamonds sparkle; rocks fall, roll and tumble; explosions shrink.
// Here ptr is (cell address - 44) so that CELL_UP, CELL_DOWN etc. reach the neighbours.
ScanRocks:
    lda #$ED                      // start at $1EED (= the cell at row 12, column 17)
    sta ptr
    lda #$1E
    sta ptr+1
    lda #$F4                      // ptr2 counts the 12 rows, ptr2+1 the 14 cells of a row
    sta ptr2
sr_row:
    lda #$F2
    sta ptr2+1
sr_cell:
    ldy #CELL_HERE                // the tile in this cell
    lda (ptr),y
    cmp #TILE_DIAMOND             // diamond
    beq SparkleDiamond
    cmp #TILE_FALL1               // falling rock, stage 1
    beq RockFrame2
    cmp #TILE_FALL2               // falling rock, stage 2
    beq RockFrame3
    cmp #TILE_ROCK                // resting rock
    beq RockAtRest
    cmp #TILE_FALL3               // falling rock, stage 3
    beq RockFalling
    cmp #TILE_EXPLODE1            // explosion stages 1-4
    beq ExplodeFrame1
    cmp #TILE_EXPLODE1+1
    beq ExplodeFrame2
    cmp #TILE_EXPLODE1+2
    beq ExplodeFrame3
    cmp #TILE_EXPLODE1+3
    beq ExplodeFrame4
sr_next:
    ldx #$00                      // next cell to the left
    lda #$01
    jsr SubFromZP
    inc ptr2+1
    bne sr_cell
    ldx #$00                      // end of the row: back 8 cells to the end of the row above
    lda #$08
    jsr SubFromZP
    inc ptr2
    bne sr_row
    rts                           // whole playfield done

// RockFalling
// A rock in its third falling stage drops into the cell below, if that is free.
// What it lands on decides what happens:
//   empty:   it moves down and goes on falling
//   solid:   it stops and becomes a resting rock
//   player:  the player is crushed (explosion)
//   monster: the monster is crushed (explosion, +10 points)
RockFalling:
    lda #TILE_EMPTY               // leave the old cell empty ...
    sta (ptr),y
    ldy #CELL_DOWN                // look at the cell below
    lda (ptr),y
    jsr ClassifyTile              // what is it?
    bne rf_notempty               // solid
    ldx #$00
    jsr PutRockFrameHere          // stay where it is as a resting rock
    jmp sr_next
rf_notempty:
    cmp #$01                      // empty?
    bne rf_notplayer
    ldx #$03                      // yes: move down as stage 3 again (PutRockFrame with Y = CELL_DOWN)
    jsr PutRockFrame
    jmp sr_next
rf_notplayer:
    cmp #$02                      // player below?
    beq rf_explode
    jsr ScoreAdd10Hi              // no, a monster: +10 points
rf_explode:
    ldx #$00                      // start an explosion in the cell below (KillAtBelow)
    jmp KillAtBelow

// ExplodeFrame1-4
// The explosion tiles $29-$2C are handled here, one animation step each time they are
// met by ScanRocks.  Each step makes a noise, writes the next stage (ExplodeAt) and
// the last stage turns into a falling rock (tile $16, cyan).
ExplodeFrame2:
    ldx #$02                      // stage 2 seen -> becomes stage 3
    jmp ExplodeStep
ExplodeFrame3:
    ldx #$03                      // stage 3 seen -> becomes stage 4
    jmp ExplodeStep
ExplodeFrame4:
    ldx #$04                      // stage 4 seen -> becomes tile $16
    jmp ExplodeStep
ExplodeFrame1:
    ldx #$01                      // stage 1 seen -> becomes stage 2
ExplodeStep:
    ldy #CELL_HERE                // noise volume for the step ...
ExplodeAt:
    lda explodeSounds,x
    sta VIC_NOISE
    lda explodeTiles,x            // ... and the next tile
    ldx #$03                      // cyan
    stx tileColour
    jsr PutTile
    jmp sr_next
explodeTiles:  .byte TILE_EXPLODE1,TILE_EXPLODE1+1,TILE_EXPLODE1+2,TILE_EXPLODE1+3,TILE_FALL1   // tile written for explosion step 0-4 (step 4 becomes a falling rock)
explodeSounds: .byte $C8,$D7,$E6,$F5,$00   // noise voice value for each step
    .byte $00                       // (unused)

// ======================================================================
// RANDOM NUMBERS, DELAYS AND SMALL HELPERS
// ======================================================================
// StirRandom
// Adds the raster line, X and Y into randAcc.  Called all the time from everywhere
// (with whatever X and Y happen to hold), so randAcc is a cheap random number.
StirRandom:
    lda VIC_RASTER                // raster line ...
    jsr stir_add
    txa                           // ... plus X ...
    jsr stir_add
    tya                           // ... plus Y
stir_add:
    clc                           // add A to the accumulator
    adc randAcc
    sta randAcc
    clc
    rts

// SfxLong / SfxDelay
// A longer noise effect, used as a "pause with a sound" in many places.  A = start
// value of the noise voice (smaller = longer); the voices are switched off at the end.
// (SfxLong enters with A = $EC.)
SfxLong:
    lda #$EC
SfxDelay:
    sta VIC_NOISE
sd_outer:
    ldx #$32                      // each noise step takes 50 x 256 counts of delay
sd_mid:
    tay
sd_inner:
    iny
    bne sd_inner
    dex
    bne sd_mid
    inc VIC_NOISE                 // next noise value
    bne sd_outer
    jmp Silence                   // voices off, then return

// Random7
// Returns a random colour 1-7 in A, and also stores it in tileColour.
// (Taken from the low 3 bits of the raster counter; 0 is rejected.)
Random7:
    lda VIC_RASTER
    and #$07
    beq Random7
    cmp #$00
    beq Random7
    sta tileColour
    rts

// DrawDigitColumn
// Draws a column of 8 digit tiles from the table at scoreDigits, starting with
// digit X and stopping when X reaches colPtr.  Y = CELL_HERE ($2C) draws the column at
// screen column 20, Y = CELL_RIGHT ($2D) draws it at column 21.  The digits are
// cyan, starting in screen row 5.
DrawDigitColumn:
    lda scoreDigits,x             // the digit tile ...
    sta SCREEN+$56,y              // ... goes into the screen ($1E56 + Y is row 3, column 20 + Y)
    lda #COL_CYAN
    sta COLOURRAM+$56,y
    tya                           // next row down
    clc
    adc #COLS
    tay
    inx
    cpx colPtr                    // until X reaches the end value in colPtr
    bcc DrawDigitColumn
    clc
    rts

// ======================================================================
// MAIN LOOP
// ======================================================================
// The three Tick routines divide the main loop's speed down: the controls are read
// every 14th pass, the monsters move every 16th pass, and the rocks are scanned every
// 9th pass.
// TickInput
TickInput:
    inc inputTick
    lda inputTick
    cmp #$0E                      // 14 passes since the last time?
    bcc ti_rts
    clc
    lda #$00
    sta inputTick
    jsr ReadJoystick              // read the joystick ...
    jsr ReadKeyboard              // ... and the keyboard
ti_rts:
    rts

// TickMonsters
TickMonsters:
    inc monsterTick
    lda monsterTick
    cmp #$10                      // 16 passes?
    bcc tm_rts
    clc
    lda #$00
    sta monsterTick
    jsr MoveMonsters              // move all the monsters one step
tm_rts:
    rts

// TickRocks
TickRocks:
    inc rockTick
    lda rockTick
    cmp #$09                      // 9 passes?
    bcc tr_rts
    clc
    lda #$00
    sta rockTick
    jsr ScanRocks                 // move rocks, explode, sparkle
tr_rts:
    rts

// Start  (entry point of the original game)
// Copies the monster-mover (MoveMonsters) from the screen RAM to $0356, clears the
// screen, clears the scores, and falls into NewGame.
Start:
    jsr RelocateCode              // copy the code that was loaded into the screen area
    lda #147                      // clear the screen
    jsr CHROUT
    jsr ClearAllScores            // score and hi-score = 0

// NewGame
// Initialises the game variables, shows the title screen and starts the first room.
// (F3 and "game over" also come here.)
NewGame:
    jsr ResetAndIntro

// MainLoop
// One pass of the game.  Everything that moves is driven from here.
MainLoop:
    jsr StirRandom                // random number
    jsr PlayTune                  // background tune (block at $0118)
    jsr TimerTick                 // room timer (block at $02A2)
    jsr TickRocks                 // rocks every 9th pass
    jsr TickMonsters              // monsters every 16th pass
    jsr CheckPlayerAlive          // is the player still alive?
    nop                           // (three NOPs in the original)
    nop
    nop
    jsr TickInput                 // controls every 14th pass
    jsr DisableStop               // keep the RUN/STOP key switched off
    jsr StirRandom                // random number
    jsr GameDelay                 // game speed delay
    jmp MainLoop

// ======================================================================
// CONTROLS
// ======================================================================
// ReadKeyboard
// Direction keys are the four keys held in keyUp-keyRight; F1 pauses, F3 starts a
// new game; any other key acts like the joystick fire button (push a rock).
ReadKeyboard:
    lda #$FF                      // port B: all outputs for the keyboard scan
    sta VIA2_DDRB
    lda #$80                      // all keys repeat
    sta KEYREPEAT
    jsr GETIN                     // a key?
    bne rk_key
    rts                           // no key
rk_key:
    cmp #KEY_F1                   // F1 = pause
    beq rk_pause
    cmp #KEY_F3                   // F3 = new game
    beq rk_reset
    cmp keyUp                     // is it one of the four direction keys?
    beq go_up
    cmp keyDown
    beq go_down
    cmp keyLeft
    beq go_left
    cmp keyRight
    beq go_right
    jmp PushRock                  // any other key = push
rk_pause:
    jmp PauseGame
rk_reset:
    jmp NewGame

// ReadJoystick
// Reads joystick port 1: up, down, left, then fire, then right (which is on VIA2).
ReadJoystick:
    lda #$00                      // port A all inputs
    sta VIA1_DDRA
    lda #$7F                      // port B: bit 7 (joystick right) is an input
    sta VIA2_DDRB
    lda VIA1_PORTA                // the joystick switches (0 = pressed)
    tax
    and #JOY_UP
    beq go_up
    txa
    and #JOY_DOWN
    beq go_down
    txa
    and #JOY_LEFT
    beq go_left
    txa
    and #JOY_FIRE
    beq go_push
    lda VIA2_PORTB
    and #JOY_RIGHT                // joystick right
    beq go_right
    lda #$FF                      // nothing pressed: port B back to all outputs for the keyboard
    sta VIA2_DDRB
    rts

// Jump trampolines: the branches above are too short to reach the move routines.
go_up:
    jmp MoveUp
go_down:
    jmp MoveDown
go_left:
    jmp MoveLeft
go_push:
    jmp PushRock
go_right:
    jmp MoveRight

// GameDelay
// Wastes time between passes of the main loop.  X starts at 'speed' and counts up to
// 0 nine times, so a bigger 'speed' gives a shorter delay (the game gets faster).
GameDelay:
    ldy #$09
gd_outer:
    ldx speed
gd_inner:
    inx
    bne gd_inner
    dey
    bne gd_outer
    rts

// ======================================================================
// PLAYER MOVEMENT
// ======================================================================
// MoveUp  (MoveDown, MoveLeft and MoveRight are the same with other offsets)
// The player may move into an empty cell or into dirt (which he digs through).
// Anything else is dealt with by MoveBlocked.
// Moving = work out the next animation frame for that direction, erase the
// player, move plyPtr one cell and redraw him in white.
MoveUp:
    ldy #CELL_UP                  // the cell above (all the move routines look at the cell they want to enter)
    lda (plyPtr),y
    cmp #TILE_EMPTY
    beq mu_move
    cmp #TILE_DIRT
    bne MoveBlocked
mu_move:
    ldx #$02                      // frames for up/down movement
    jsr PlayerFrame               // next animation frame (A)
    pha
    jsr ErasePlayer               // erase the player ...
    ldx #plyPtr                   // ... move the pointer up one row ...
    lda #COLS
    jsr SubFromZP
    pla                           // ... and draw the player in the new cell
    jmp DrawTileWhite

// MoveBlocked
// A = the tile the player ran into.  Y = its offset from plyPtr.
//   diamond or trap: TouchTrapOrDiamond
//   brick wall:      TryLeaveRoom (a way out, if the room is cleared)
//   monster:         the player dies
//   anything else (rock...) cannot be walked on.
MoveBlocked:
    cmp #TILE_DIAMOND
    beq mb_trap
    cmp #TILE_TRAP
    beq mb_trap
    cmp #TILE_BRICK
    beq mb_brick
    cmp #TILE_MONSTER_A           // tiles $21-$25 are monsters
    bcc mb_rts
    clc
    cmp #TILE_MONSTER_C+1
    bcs mb_rts
    clc
    jsr PlayerDies
mb_rts:
    clc
    rts
mb_trap:
    jmp TouchTrapOrDiamond        // diamond or trap
mb_brick:
    jmp TryLeaveRoom              // brick

// MoveDown
MoveDown:
    ldy #CELL_DOWN
    lda (plyPtr),y
    cmp #TILE_EMPTY
    beq md_move
    cmp #TILE_DIRT
    bne md_other
md_move:
    ldx #$02
    jsr PlayerFrame               // frames for up/down movement
    pha
    jsr ErasePlayer
    ldx #plyPtr
    lda #COLS
    jsr AddToZP                   // move the pointer down one row (+22)
    pla
    jmp DrawTileWhite
md_other:
    jmp MoveBlocked

// MoveLeft
MoveLeft:
    ldy #CELL_LEFT
    lda (plyPtr),y
    cmp #TILE_EMPTY
    beq ml_move
    cmp #TILE_DIRT
    bne ml_other
ml_move:
    ldx #$00                      // frames for left movement
    jsr PlayerFrame
    pha
    jsr ErasePlayer
    ldx #plyPtr
    lda #$01
    jsr SubFromZP                 // move the pointer left (-1)
    pla
    jmp DrawTileWhite
ml_other:
    jmp MoveBlocked

// MoveRight
MoveRight:
    ldy #CELL_RIGHT
    lda (plyPtr),y
    cmp #TILE_EMPTY
    beq mr_move
    cmp #TILE_DIRT
    bne mr_other
mr_move:
    ldx #$01                      // frames for right movement
    jsr PlayerFrame
    pha
    jsr ErasePlayer
    ldx #plyPtr
    lda #$01
    jsr AddToZP                   // move the pointer right (+1)
    pla
    jmp DrawTileWhite
mr_other:
    jmp MoveBlocked

// ======================================================================
// ROOMS
// ======================================================================
// ChangeRoom
// Y = the direction the player walked off in (CELL_DOWN/LEFT/UP/RIGHT).  The new room
// is read from the exit table for that direction, and the player appears at the
// start position on the opposite side of the new room:
//    walked off the bottom  -> arrives at the top      (entry position 1)
//    walked off the left    -> arrives on the right    (entry position 2)
//    walked off the top     -> arrives at the bottom   (entry position 3)
//    walked off the right   -> arrives on the left     (entry position 4)
// (Entry position 0 is the middle of the room, used when a game starts.)
// (Rooms have their upper exits sealed with bricks by DrawRoom, so in practice
// the player cannot walk off the top.)
ChangeRoom:
    ldx #$01                      // X = entry position
    lda room                      // A = current room number
    cpy #CELL_DOWN
    beq cr_down
    inx
    cpy #CELL_LEFT
    beq cr_left
    inx
    cpy #CELL_UP
    beq cr_up
    inx                           // right: exitRight[room]
    tay
    lda exitRight-1,y
cr_set:
    sta room                      // this is the new room

// EnterRoom
// X = entry position.  Starts the room again from the beginning at that position.
EnterRoom:
    jsr SetStartPos               // set plyPtr from the start tables
    jsr SfxDefault                // sound

// StartRoom
// Draws the current room and everything in it.
StartRoom:
    jsr DrawRoom                  // map (and walls)
    jsr TimerReset                // reset the room timer
    jsr PlaceRoomDiamonds         // diamonds
    jsr SpawnEnemies              // monsters and traps
    jsr DrawPlayer                // the player
    jmp SfxDefault
cr_down:
    tay
    lda exitDown-1,y
    jmp cr_set
cr_left:
    tay
    lda exitLeft-1,y
    jmp cr_set
cr_up:
    tay
    lda exitUp-1,y
    jmp cr_set

// DisableStop
// Points the STOP-key test vector at a harmless routine so RUN/STOP does nothing.
DisableStop:
    lda #$83
    sta ISTOP
    rts

// ======================================================================
// START-UP
// ======================================================================
// InitGame
// Sets the system up for a new game: BASIC error vector, shift/C= lock, volume 15,
// black screen and border, 5 lives, room 0, speed 0, and all rooms uncleared.
InitGame:
    lda #$C8                      // BASIC error-message vector (low byte)
    sta IERROR
    lda #$80                      // stop SHIFT / C= from changing the character set
    sta SHFLAG
    lda #$0F                      // volume 15
    sta VIC_VOLUME
    lda #$08                      // black border and background
    sta VIC_COLOURS
    lda #TILE_0+5                 // five lives (TILE_0 + 5)
    sta lives
    lda #$00                      // room 0 = title picture
    sta room
    lda #$00                      // slowest speed
    sta speed

// InitRoomFlags
// Marks all 20 rooms as not yet cleared.
InitRoomFlags:
    ldx #NUM_ROOMS
    lda #$01
ig_flags:
    sta roomFlags,x
    dex
    bne ig_flags
    rts

// StartPlaying
// Starts a game after the title screen: score back to 0, room 1, player in the middle.
StartPlaying:
    jsr ClearScore
sp_room1:
    lda #1
    sta room
    jsr SetStartCenter
    jmp StartRoom

// ResetAndIntro
ResetAndIntro:
    jsr InitGame
    jsr IntroScreen
    jmp StartPlaying

// ======================================================================
// DRAWING THE PLAYER
// ======================================================================
// DrawPlayer
// Draws the player (facing up) at the start position.
// DrawTileWhite  : draws tile A at the player's cell in white
// ErasePlayer    : draws an empty cell there
// DrawTileAtPlayer : draws tile A in colour X
// PutTileAtPlayer  : draws tile A in colour tileColour at plyPtr+Y
DrawPlayer:
    lda #TILE_PLAYER_UD
DrawTileWhite:
    ldx #$01
    jmp DrawTileAtPlayer
ErasePlayer:
    lda #TILE_EMPTY
    ldx #$00
DrawTileAtPlayer:
    stx tileColour
    ldy #CELL_HERE
PutTileAtPlayer:
    sta (plyPtr),y
    lda plyPtr                    // the matching colour RAM address is the screen address + $7800
    sta colPtr
    lda plyPtr+1
    clc
    adc #$78
    sta colPtr+1
    lda tileColour
    sta (colPtr),y
    rts

// CheckPlayerAlive
// The player is dead if the player's cell no longer holds one of the player tiles
// ($1B-$20), e.g. because a rock, an explosion or a monster is on it.
CheckPlayerAlive:
    ldy #CELL_HERE
    lda (plyPtr),y
    cmp #TILE_PLAYER_UD
    bcc cpa_dead
    clc
    cmp #TILE_MONSTER_A
    bcc cpa_rts
cpa_dead:
    clc
    jsr PlayerDies
cpa_rts:
    rts

// PlayerFrame / ToggleFrame
// PlayerFrame returns in A the next animation frame for direction X (0 = left,
// 1 = right, 2 = up/down), based on what the player is showing now.
// ToggleFrame (A = current tile, X = animation number) returns the other frame of
// animation X: frameB[X] if A is frameA[X], otherwise frameA[X].
PlayerFrame:
    ldy #CELL_HERE
    lda (plyPtr),y
ToggleFrame:
    cmp frameA,x
    beq tf_second
    lda frameA,x
    rts
tf_second:
    lda frameB,x
    rts

// Animation frames.  Index 0-2 = the player (left, right, up/down),
// 3 = monster A, 4 = monster C, 5 = monster B, 6 = unused.
// frameA is the first frame, frameB the second.
frameA: .byte TILE_PLAYER_L,TILE_PLAYER_R,TILE_PLAYER_UD,TILE_MONSTER_A,TILE_MONSTER_C,TILE_MONSTER_B,TILE_MONSTER_D   // first frame: player left, right, up/down; monsters A, C, B, D
frameB: .byte TILE_PLAYER_L+1,TILE_PLAYER_R+1,TILE_PLAYER_UD+1,TILE_MONSTER_A+1,TILE_MONSTER_C+1,TILE_MONSTER_B+1,TILE_MONSTER_D+1   // second frame of each animation

// SetStartCenter / SetStartPos
// Puts plyPtr at start position X.  The positions are:
//   0 = middle of the room (row 7, column 11)   1 = top (row 1, column 11)
//   2 = right (row 7, column 18)                3 = bottom (row 13, column 11)
//   4 = left (row 7, column 3)
// (plyPtr is the cell address minus 44.)
SetStartCenter:
    ldx #$00
SetStartPos:
    lda startPtrLo,x
    sta plyPtr
    lda startPtrHi,x
    jmp SetPlayerPtr

// Start position tables (low and high bytes of plyPtr).
startPtrLo: .byte $79,$F5,$80,$FD,$71   // plyPtr (low byte) for start positions 0-4
startPtrHi: .byte $1E,$1D,$1E,$1E,$1E   // plyPtr (high byte) for start positions 0-4

// ======================================================================
// SCORE
// ======================================================================
// RelocateCode
// Copies the 170 bytes of MoveMonsters that were loaded into the screen area at $1E00
// down to $0356.  The screen is about to be cleared, which would destroy them.
RelocateCode:
    ldx #$AA
rc_loop:
    lda SCREEN-1,x
    sta MoveMonsters-1,x
    dex
    bne rc_loop
    jmp DisableStop

// ScoreAdd10
// Adds 10 to the score: the last digit is incremented ten times with decimal carry.
// The digits are tiles: TILE_0 ($05) .. TILE_0+9 ($0E).
ScoreAdd10:
    ldy #$0A                      // 10 increments
sa_outer:
    ldx #$08                      // start at the last digit
sa_digit:
    lda lives,x                   // add one
    clc
    adc #$01
    sta lives,x
    cmp #TILE_0+10                // passed '9'?
    bcc sa_next
    clc
    lda #TILE_0                   // yes: back to '0' and carry into the next digit
    sta lives,x
    dex
    bne sa_digit
sa_next:
    dey
    bne sa_outer

// DrawScore
// Draws the 8 score digits in screen column 20.
DrawScore:
    ldx #$00
    ldy #CELL_HERE
    lda #$08
    sta colPtr
    clc
    jmp DrawDigitColumn

// ClearAllScores / ClearScore
// ClearAllScores sets the score and hi-score to 0; ClearScore only the score.
ClearAllScores:
    ldx #$10
    jmp cs_fill
ClearScore:
    ldx #$08
cs_fill:
    lda #TILE_0
cs_loop:
    sta lives,x
    dex
    bne cs_loop
    rts

// ScoreAdd100
// Adds 100 points (ten times ScoreAdd10) and updates the hi-score.
ScoreAdd100:
    lda #$F6
    sta tmp
sa100_loop:
    jsr ScoreAdd10
    inc tmp
    bne sa100_loop
    jmp UpdateHiScore

// UpdateHiScore
// Compares the score with the hi-score digit by digit; if the score is higher it is
// copied to the hi-score.  Then the hi-score column (screen column 21) is redrawn.
// (Note that the score column is only drawn when a new game starts; once the score
// beats the hi-score the current score shows in the HI column.)
UpdateHiScore:
    ldx #$00
uh_cmp:
    lda scoreDigits,x
    cmp hiDigits,x
    beq uh_equal
    bcc uh_draw
    clc
uh_copy:
    lda scoreDigits,x
    sta hiDigits,x
    inx
    cpx #$08
    bcc uh_copy
uh_draw:
    clc
DrawHiScore:
    ldx #$08
    ldy #CELL_RIGHT
    lda #$10
    sta colPtr
    clc
    jmp DrawDigitColumn
uh_equal:
    inx
    cpx #$08
    bcc uh_cmp
    clc
    jmp uh_draw

// ScoreAdd10Hi
// Adds 10 points and updates the hi-score (used when a monster is crushed).
ScoreAdd10Hi:
    jsr ScoreAdd10
    jmp UpdateHiScore

// InitColours
// Colours the whole screen purple and the lives digit and the SC/HI labels green.
InitColours:
    ldx #165                      // 165 + 165 = 330 cells (22 x 15)
    lda #COL_PURPLE
ic_loop:
    sta COLOURRAM-1,x
    sta COLOURRAM+$A4,x
    dex
    bne ic_loop
    lda #COL_GREEN                // lives digit, SC and HI labels green
    sta COLOURRAM+$2A
    sta COLOURRAM+$56
    sta COLOURRAM+$57
    rts

// SetColour960B
// Stores A as the colour of the cell at row 0, column 11 ($960B).  Used by SealTopExit.
SetColour960B:
    sta COLOURRAM+$0B
    rts
    .byte $EA,$EA,$EA,$EA,$EA,$EA   // unused (NOPs)

// ======================================================================
// 16-BIT POINTER ARITHMETIC AND SCREEN OUTPUT
// ======================================================================
// AddToZP / SubFromZP
// Adds A to (or subtracts it from) the 16-bit zero page value at X.
AddToZP:
    sta addTmp
    lda ptr,x
    clc
    adc addTmp
    sta ptr,x
    lda ptr+1,x
    adc #$00
    sta ptr+1,x
    clc
    rts
SubFromZP:
    sta addTmp
    lda ptr,x
    sec
    sbc addTmp
    sta ptr,x
    lda ptr+1,x
    sbc #$00
    sta ptr+1,x
    clc
    rts

// DrawBorderScreen
// Fills the whole screen with brick, colours it, puts the labels "SC" and "HI" and the
// lives digit in place, draws the score and hi-score columns and finally the map of the
// current room.
DrawBorderScreen:
    ldx #165                      // 2 x 165 cells
    lda #TILE_BRICK
dbs_fill:
    sta SCREEN-1,x
    sta SCREEN+$A4,x
    dex
    bne dbs_fill
    jsr InitColours
    ldx #TILE_SC                  // "SC" in row 3, column 20 ...
    stx SCREEN+$56
    inx                           // ... and "HI" in column 21
    stx SCREEN+$57
    lda lives                     // lives digit: row 1, column 20
    sta SCREEN+$2A
    jsr DrawScore                 // score column
    jsr DrawHiScore               // hi-score column
    jmp DrawRoomMap               // draw the map of the current room

// DrawRoom
// Draws the current room.  If the map has a gap in the top wall (dirt in the top
// exit cells) the gap is closed with bricks, so the player can never leave upwards.
DrawRoom:
    jsr DrawRoomMap
    lda SCREEN+$0A                // the top exit cell: dirt?
    cmp #TILE_DIRT
    bne dr_rts
    jsr SealTopExit               // yes: seal it
dr_rts:
    rts

// PutTile / PutColour
// PutTile stores tile A at (ptr)+Y and, with PutColour, the colour tileColour at the same
// place in colour RAM (screen address + $7800).
PutTile:
    sta (ptr),y
PutColour:
    lda ptr
    sta colPtr
    lda ptr+1
    clc
    adc #$78
    sta colPtr+1
    lda tileColour
    sta (colPtr),y
    rts

// DrawMapByte
// Draws 8 cells of a map row.  ptr2 points to the map byte, ptr to the screen.  A set
// bit is a rock (red), a clear bit is dirt.  (The colour bytes of the two LDAs
// are modified by the game, they are labelled rockColour and dirtColour.)
DrawMapByte:
    lda #$80                      // start with the leftmost bit
dmb_bit:
    sta tmp                       // test one bit of the map
    ldy #$00
    and (ptr2),y
    beq dmb_dirt
dmb_rock:
.label rockColour = *+1       // ditto (this LDA holds the rock colour)
    lda #COL_RED                  // rock colour (self-modified)
    sta tileColour
    lda #TILE_ROCK
    jmp dmb_put
dmb_dirt:
.label dirtColour = *+1       // the colour byte of this LDA is changed by the game
    lda #COL_GREEN                // dirt colour (self-modified)
    sta tileColour
    lda #TILE_DIRT
dmb_put:
    jsr PutTile                   // store tile and colour
    inc ptr                       // next screen cell
    bne dmb_next
    inc ptr+1
dmb_next:
    clc
    lda tmp                       // next bit
    lsr
    clc
    bne dmb_bit
    inc ptr2                      // next map byte
    bne dmb_rts
    inc ptr2+1
dmb_rts:
    clc
    rts

// DrawRoomMap
// Draws the map of the current room: 14 rows of 16 cells (two bytes per row), starting at
// row 0, column 3 of the screen (the room map is at mapPtr[room]).
DrawRoomMap:
    ldx room                      // map address of this room
    lda mapPtrLo,x
    sta ptr2
    lda mapPtrHi,x
    sta ptr2+1
    lda #$03                      // screen address row 0, column 3
    sta ptr
    lda #$1E
    sta ptr+1
    ldx #$0E                      // 14 rows
drm_row:
    jsr DrawMapByte               // 16 cells per row
    jsr DrawMapByte
    lda ptr                       // skip to the start of the next row (+6)
    clc
    adc #$06
    sta ptr
    lda ptr+1
    adc #$00
    sta ptr+1
    dex
    bne drm_row
    rts

// ======================================================================
// DATA TABLES
// ======================================================================
// Address of each room's map (low and high bytes), rooms 0-21.  Every map is 28 bytes.
mapPtrLo:
    .byte $98,$B4,$D0,$EC,$08,$24,$40,$5C,$78,$94,$B0,$CC,$E8,$04,$20,$3C,$58,$74,$90,$AC,$C8,$E4
mapPtrHi:
    .byte $1B,$1B,$1B,$1B,$1C,$1C,$1C,$1C,$1C,$1C,$1C,$1C,$1C,$1D,$1D,$1D,$1D,$1D,$1D,$1D,$1D,$1D

// Exits.  exitUp[n] ... exitRight[n] give the room reached by walking out of room n
// (rooms 1-20) through the top, bottom, left or right edge.  0 means there is no exit.
// (The routines index them with room-1 bytes into the table: exitUp-1,y with y = room.)
exitUp:
    .byte $10,$11,$12,$00,$00,$01,$02,$03,$00,$00    // rooms 1-10
    .byte $06,$00,$08,$09,$00,$00,$00,$0D,$0E,$0F    // rooms 11-20
exitDown:
    .byte $06,$07,$08,$00,$00,$0B,$00,$0D,$0E,$00    // rooms 1-10
    .byte $00,$00,$12,$13,$14,$01,$02,$03,$00,$00    // rooms 11-20
exitLeft:
    .byte $00,$00,$00,$03,$04,$00,$00,$00,$08,$09    // rooms 1-10
    .byte $0F,$0B,$0C,$0D,$0E,$00,$10,$11,$12,$13    // rooms 11-20
exitRight:
    .byte $00,$00,$04,$05,$00,$00,$00,$09,$0A,$00    // rooms 1-10
    .byte $0C,$0D,$0E,$0F,$0B,$11,$12,$13,$14,$00    // rooms 11-20

// ======================================================================
// CHARACTER SET   (the VIC fetches characters from $1800 up: $9005 = $FE)
// ======================================================================
// The video chip is in 8x16 character mode ($9003 bit 0 = 1), so every character is
// 16 bytes and character N is at $1800 + N*16.  Characters $00-$04 and $11, $27, $28
// and everything from $2D on overlap tables and routines (those are never displayed).
// Characters $05-$10 are the 12 characters below $1910; $11 is the SealTopExit
// routine; the rest follow it.  The tile numbers are the screen codes in SCREEN.
charset:
    // $05  digit 0
    .byte %00000000   // ........
    .byte %01111111   // .#######
    .byte %01000001   // .#.....#
    .byte %01011101   // .#.###.#
    .byte %01010101   // .#.#.#.#
    .byte %01010101   // .#.#.#.#
    .byte %01010101   // .#.#.#.#
    .byte %01010101   // .#.#.#.#
    .byte %01010101   // .#.#.#.#
    .byte %01010101   // .#.#.#.#
    .byte %01010101   // .#.#.#.#
    .byte %01010101   // .#.#.#.#
    .byte %01010101   // .#.#.#.#
    .byte %01011101   // .#.###.#
    .byte %01000001   // .#.....#
    .byte %01111111   // .#######
    // $06  digit 1
    .byte %00000000   // ........
    .byte %00011100   // ...###..
    .byte %00110100   // ..##.#..
    .byte %01100100   // .##..#..
    .byte %01000100   // .#...#..
    .byte %01110100   // .###.#..
    .byte %00010100   // ...#.#..
    .byte %00010100   // ...#.#..
    .byte %00010100   // ...#.#..
    .byte %00010100   // ...#.#..
    .byte %00010100   // ...#.#..
    .byte %00010100   // ...#.#..
    .byte %00010100   // ...#.#..
    .byte %01100111   // .##..###
    .byte %01000001   // .#.....#
    .byte %01111111   // .#######
    // $07  digit 2
    .byte %00000000   // ........
    .byte %01111111   // .#######
    .byte %01000001   // .#.....#
    .byte %01011001   // .#.##..#
    .byte %01110101   // .###.#.#
    .byte %00000101   // .....#.#
    .byte %00000101   // .....#.#
    .byte %01111101   // .#####.#
    .byte %01000001   // .#.....#
    .byte %01011111   // .#.#####
    .byte %01010000   // .#.#....
    .byte %01010000   // .#.#....
    .byte %01010000   // .#.#....
    .byte %01011111   // .#.#####
    .byte %01000001   // .#.....#
    .byte %01111111   // .#######
    // $08  digit 3
    .byte %00000000   // ........
    .byte %01111111   // .#######
    .byte %01000001   // .#.....#
    .byte %01111101   // .#####.#
    .byte %00000101   // .....#.#
    .byte %00000101   // .....#.#
    .byte %00000101   // .....#.#
    .byte %00111101   // ..####.#
    .byte %00100001   // ..#....#
    .byte %00111101   // ..####.#
    .byte %00000101   // .....#.#
    .byte %00000101   // .....#.#
    .byte %00000101   // .....#.#
    .byte %01111101   // .#####.#
    .byte %01000001   // .#.....#
    .byte %01111111   // .#######
    // $09  digit 4
    .byte %00000000   // ........
    .byte %00001110   // ....###.
    .byte %00011010   // ...##.#.
    .byte %00110010   // ..##..#.
    .byte %01100010   // .##...#.
    .byte %01001010   // .#..#.#.
    .byte %01011010   // .#.##.#.
    .byte %01011010   // .#.##.#.
    .byte %01011011   // .#.##.##
    .byte %01000001   // .#.....#
    .byte %01111011   // .####.##
    .byte %00001010   // ....#.#.
    .byte %00001010   // ....#.#.
    .byte %00001010   // ....#.#.
    .byte %00001010   // ....#.#.
    .byte %00001110   // ....###.
    // $0A  digit 5
    .byte %00000000   // ........
    .byte %01111111   // .#######
    .byte %01000001   // .#.....#
    .byte %01011111   // .#.#####
    .byte %01010000   // .#.#....
    .byte %01010000   // .#.#....
    .byte %01010000   // .#.#....
    .byte %01011111   // .#.#####
    .byte %01000001   // .#.....#
    .byte %01111101   // .#####.#
    .byte %00000101   // .....#.#
    .byte %00000101   // .....#.#
    .byte %00000101   // .....#.#
    .byte %01111101   // .#####.#
    .byte %01000001   // .#.....#
    .byte %01111111   // .#######
    // $0B  digit 6
    .byte %00000000   // ........
    .byte %01110000   // .###....
    .byte %01010000   // .#.#....
    .byte %01010000   // .#.#....
    .byte %01010000   // .#.#....
    .byte %01010000   // .#.#....
    .byte %01010000   // .#.#....
    .byte %01011111   // .#.#####
    .byte %01000001   // .#.....#
    .byte %01011101   // .#.###.#
    .byte %01010101   // .#.#.#.#
    .byte %01010101   // .#.#.#.#
    .byte %01010101   // .#.#.#.#
    .byte %01011101   // .#.###.#
    .byte %01000001   // .#.....#
    .byte %01111111   // .#######
    // $0C  digit 7
    .byte %00000000   // ........
    .byte %01111111   // .#######
    .byte %01000001   // .#.....#
    .byte %01111101   // .#####.#
    .byte %00000101   // .....#.#
    .byte %00000101   // .....#.#
    .byte %00001001   // ....#..#
    .byte %00010010   // ...#..#.
    .byte %00100100   // ..#..#..
    .byte %01001000   // .#..#...
    .byte %01010000   // .#.#....
    .byte %01010000   // .#.#....
    .byte %01010000   // .#.#....
    .byte %01010000   // .#.#....
    .byte %01010000   // .#.#....
    .byte %01110000   // .###....
    // $0D  digit 8
    .byte %00000000   // ........
    .byte %01111111   // .#######
    .byte %01000001   // .#.....#
    .byte %01011101   // .#.###.#
    .byte %01010101   // .#.#.#.#
    .byte %01010101   // .#.#.#.#
    .byte %01011101   // .#.###.#
    .byte %01000001   // .#.....#
    .byte %00100010   // ..#...#.
    .byte %01000001   // .#.....#
    .byte %01011101   // .#.###.#
    .byte %01010101   // .#.#.#.#
    .byte %01010101   // .#.#.#.#
    .byte %01011101   // .#.###.#
    .byte %01000001   // .#.....#
    .byte %01111111   // .#######
    // $0E  digit 9
    .byte %00000000   // ........
    .byte %01111111   // .#######
    .byte %01000001   // .#.....#
    .byte %01011101   // .#.###.#
    .byte %01010101   // .#.#.#.#
    .byte %01010101   // .#.#.#.#
    .byte %01010101   // .#.#.#.#
    .byte %01011101   // .#.###.#
    .byte %01000001   // .#.....#
    .byte %01111101   // .#####.#
    .byte %00000101   // .....#.#
    .byte %00000101   // .....#.#
    .byte %00000101   // .....#.#
    .byte %00000101   // .....#.#
    .byte %00000101   // .....#.#
    .byte %00000111   // .....###
    // $0F  "SC" label (score)
    .byte %11111111   // ########
    .byte %11000011   // ##....##
    .byte %11011111   // ##.#####
    .byte %11011111   // ##.#####
    .byte %11000011   // ##....##
    .byte %11111011   // #####.##
    .byte %11111011   // #####.##
    .byte %11000011   // ##....##
    .byte %11111111   // ########
    .byte %11000011   // ##....##
    .byte %11011111   // ##.#####
    .byte %11011111   // ##.#####
    .byte %11011111   // ##.#####
    .byte %11011111   // ##.#####
    .byte %11000011   // ##....##
    .byte %11111111   // ########
    // $10  "HI" label (hi-score)
    .byte %11111111   // ########
    .byte %11011011   // ##.##.##
    .byte %11011011   // ##.##.##
    .byte %11011011   // ##.##.##
    .byte %11000011   // ##....##
    .byte %11011011   // ##.##.##
    .byte %11011011   // ##.##.##
    .byte %11011011   // ##.##.##
    .byte %11111111   // ########
    .byte %11000111   // ##...###
    .byte %11101111   // ###.####
    .byte %11101111   // ###.####
    .byte %11101111   // ###.####
    .byte %11101111   // ###.####
    .byte %11000111   // ##...###
    .byte %11111111   // ########

// SealTopExit
// Fills the two top exit cells (row 0, columns 10 and 11) with purple brick.
// It sits where the (unused) tile $11 would be.
SealTopExit:
    lda #TILE_BRICK
    sta SCREEN+$0A                // the exit cell on the left ...
    sta SCREEN+$0B                // ... and the one to its right
    lda #COL_PURPLE               // purple
    sta COLOURRAM+$0A
    jmp SetColour960B
    // $12  brick wall (border and the sealed top exits)
    .byte %00000000   // ........
    .byte %11011111   // ##.#####
    .byte %11011111   // ##.#####
    .byte %11011111   // ##.#####
    .byte %00000000   // ........
    .byte %11111011   // #####.##
    .byte %11111011   // #####.##
    .byte %11111011   // #####.##
    .byte %00000000   // ........
    .byte %11011111   // ##.#####
    .byte %11011111   // ##.#####
    .byte %11011111   // ##.#####
    .byte %00000000   // ........
    .byte %11111011   // #####.##
    .byte %11111011   // #####.##
    .byte %11111011   // #####.##
    // $13  dirt
    .byte %10101010   // #.#.#.#.
    .byte %10101010   // #.#.#.#.
    .byte %01010101   // .#.#.#.#
    .byte %01010101   // .#.#.#.#
    .byte %10101010   // #.#.#.#.
    .byte %10101010   // #.#.#.#.
    .byte %01010101   // .#.#.#.#
    .byte %01010101   // .#.#.#.#
    .byte %10101010   // #.#.#.#.
    .byte %10101010   // #.#.#.#.
    .byte %01010101   // .#.#.#.#
    .byte %01010101   // .#.#.#.#
    .byte %10101010   // #.#.#.#.
    .byte %10101010   // #.#.#.#.
    .byte %01010101   // .#.#.#.#
    .byte %01010101   // .#.#.#.#
    // $14  empty space
    .byte %00000000   // ........
    .byte %00000000   // ........
    .byte %00000000   // ........
    .byte %00000000   // ........
    .byte %00000000   // ........
    .byte %00000000   // ........
    .byte %00000000   // ........
    .byte %00000000   // ........
    .byte %00000000   // ........
    .byte %00000000   // ........
    .byte %00000000   // ........
    .byte %00000000   // ........
    .byte %00000000   // ........
    .byte %00000000   // ........
    .byte %00000000   // ........
    .byte %00000000   // ........
    // $15  rock (at rest)
    .byte %00111100   // ..####..
    .byte %01100110   // .##..##.
    .byte %11011111   // ##.#####
    .byte %10111101   // #.####.#
    .byte %11110111   // ####.###
    .byte %11011111   // ##.#####
    .byte %10111101   // #.####.#
    .byte %11110111   // ####.###
    .byte %10111111   // #.######
    .byte %11101101   // ###.##.#
    .byte %11111101   // ######.#
    .byte %10111001   // #.###..#
    .byte %11110001   // ####...#
    .byte %11100011   // ###...##
    .byte %01111110   // .######.
    .byte %00111100   // ..####..
    // $16  rock, falling stage 1
    .byte %00111100   // ..####..
    .byte %01100110   // .##..##.
    .byte %11111011   // #####.##
    .byte %10111101   // #.####.#
    .byte %11101111   // ###.####
    .byte %11111011   // #####.##
    .byte %10111101   // #.####.#
    .byte %11101111   // ###.####
    .byte %11111101   // ######.#
    .byte %10110111   // #.##.###
    .byte %10111111   // #.######
    .byte %10011101   // #..###.#
    .byte %10001111   // #...####
    .byte %11000111   // ##...###
    .byte %01111110   // .######.
    .byte %00111100   // ..####..
    // $17  rock, falling stage 2
    .byte %00111100   // ..####..
    .byte %01111110   // .######.
    .byte %11000111   // ##...###
    .byte %10001111   // #...####
    .byte %10011101   // #..###.#
    .byte %10111111   // #.######
    .byte %10110111   // #.##.###
    .byte %11111101   // ######.#
    .byte %11101111   // ###.####
    .byte %10111101   // #.####.#
    .byte %11111011   // #####.##
    .byte %11101111   // ###.####
    .byte %10111101   // #.####.#
    .byte %11111011   // #####.##
    .byte %01100110   // .##..##.
    .byte %00111100   // ..####..
    // $18  rock, falling stage 3
    .byte %00111100   // ..####..
    .byte %01111110   // .######.
    .byte %11100011   // ###...##
    .byte %11110001   // ####...#
    .byte %10111001   // #.###..#
    .byte %11111101   // ######.#
    .byte %11101101   // ###.##.#
    .byte %10111111   // #.######
    .byte %11110111   // ####.###
    .byte %10111101   // #.####.#
    .byte %11011111   // ##.#####
    .byte %11110111   // ####.###
    .byte %10111101   // #.####.#
    .byte %11011111   // ##.#####
    .byte %01100110   // .##..##.
    .byte %00111100   // ..####..
    // $19  diamond
    .byte %00011000   // ...##...
    .byte %00111100   // ..####..
    .byte %00111100   // ..####..
    .byte %01111110   // .######.
    .byte %01100110   // .##..##.
    .byte %11100111   // ###..###
    .byte %11000011   // ##....##
    .byte %11000011   // ##....##
    .byte %11000011   // ##....##
    .byte %11000011   // ##....##
    .byte %11100111   // ###..###
    .byte %01100110   // .##..##.
    .byte %01111110   // .######.
    .byte %00111100   // ..####..
    .byte %00111100   // ..####..
    .byte %00011000   // ...##...
    // $1A  trap
    .byte %01111110   // .######.
    .byte %11111111   // ########
    .byte %10011001   // #..##..#
    .byte %10111101   // #.####.#
    .byte %11111111   // ########
    .byte %11100111   // ###..###
    .byte %11111111   // ########
    .byte %11000011   // ##....##
    .byte %01111110   // .######.
    .byte %00111100   // ..####..
    .byte %00000000   // ........
    .byte %11000011   // ##....##
    .byte %11100111   // ###..###
    .byte %00111100   // ..####..
    .byte %11100111   // ###..###
    .byte %11000011   // ##....##
    // $1B  Rockman facing up/down, frame 1
    .byte %01111110   // .######.
    .byte %11111111   // ########
    .byte %10011001   // #..##..#
    .byte %01000100   // .#...#..
    .byte %10011001   // #..##..#
    .byte %11111111   // ########
    .byte %10111101   // #.####.#
    .byte %11000011   // ##....##
    .byte %11111111   // ########
    .byte %11111111   // ########
    .byte %11111111   // ########
    .byte %01111110   // .######.
    .byte %01100111   // .##..###
    .byte %01100111   // .##..###
    .byte %11100000   // ###.....
    .byte %11100000   // ###.....
    // $1C  Rockman facing up/down, frame 2
    .byte %01111110   // .######.
    .byte %11111111   // ########
    .byte %10011001   // #..##..#
    .byte %00100010   // ..#...#.
    .byte %10011001   // #..##..#
    .byte %11111111   // ########
    .byte %10111101   // #.####.#
    .byte %10000001   // #......#
    .byte %10000001   // #......#
    .byte %11000011   // ##....##
    .byte %11111111   // ########
    .byte %01111110   // .######.
    .byte %11100110   // ###..##.
    .byte %11100110   // ###..##.
    .byte %00000111   // .....###
    .byte %00000111   // .....###
    // $1D  Rockman facing left, frame 1
    .byte %01111110   // .######.
    .byte %11111111   // ########
    .byte %10001111   // #...####
    .byte %00100001   // ..#....#
    .byte %10001101   // #...##.#
    .byte %11111111   // ########
    .byte %11011001   // ##.##..#
    .byte %00011001   // ...##..#
    .byte %00011001   // ...##..#
    .byte %00110001   // ..##...#
    .byte %11111011   // #####.##
    .byte %01110010   // .###..#.
    .byte %00111100   // ..####..
    .byte %00011000   // ...##...
    .byte %00111000   // ..###...
    .byte %00111000   // ..###...
    // $1E  Rockman facing left, frame 2
    .byte %01111110   // .######.
    .byte %11111111   // ########
    .byte %10001111   // #...####
    .byte %01000001   // .#.....#
    .byte %10001101   // #...##.#
    .byte %11111111   // ########
    .byte %11011001   // ##.##..#
    .byte %00111001   // ..###..#
    .byte %11110001   // ####...#
    .byte %11100011   // ###...##
    .byte %11110111   // ####.###
    .byte %01100110   // .##..##.
    .byte %00111111   // ..######
    .byte %01110011   // .###..##
    .byte %11100111   // ###..###
    .byte %11100111   // ###..###
    // $1F  Rockman facing right, frame 1
    .byte %01111110   // .######.
    .byte %11111111   // ########
    .byte %11110001   // ####...#
    .byte %10000100   // #....#..
    .byte %10110001   // #.##...#
    .byte %11111111   // ########
    .byte %10011011   // #..##.##
    .byte %10011000   // #..##...
    .byte %10011000   // #..##...
    .byte %10001100   // #...##..
    .byte %11011111   // ##.#####
    .byte %01001110   // .#..###.
    .byte %00111100   // ..####..
    .byte %00011000   // ...##...
    .byte %00011100   // ...###..
    .byte %00011100   // ...###..
    // $20  Rockman facing right, frame 2
    .byte %01111110   // .######.
    .byte %11111111   // ########
    .byte %11110001   // ####...#
    .byte %10000010   // #.....#.
    .byte %10110001   // #.##...#
    .byte %11111111   // ########
    .byte %10011011   // #..##.##
    .byte %10011100   // #..###..
    .byte %10001111   // #...####
    .byte %11000111   // ##...###
    .byte %11101111   // ###.####
    .byte %01100110   // .##..##.
    .byte %11111100   // ######..
    .byte %11001110   // ##..###.
    .byte %11100111   // ###..###
    .byte %11100111   // ###..###
    // $21  monster A, frame 1
    .byte %01111110   // .######.
    .byte %01011010   // .#.##.#.
    .byte %01111110   // .######.
    .byte %01111110   // .######.
    .byte %01100110   // .##..##.
    .byte %11111111   // ########
    .byte %11111111   // ########
    .byte %11111111   // ########
    .byte %11111111   // ########
    .byte %11111111   // ########
    .byte %11111111   // ########
    .byte %01100110   // .##..##.
    .byte %01100111   // .##..###
    .byte %01100111   // .##..###
    .byte %11100000   // ###.....
    .byte %11100000   // ###.....
    // $22  monster A, frame 2
    .byte %01111110   // .######.
    .byte %01011010   // .#.##.#.
    .byte %01111110   // .######.
    .byte %01111110   // .######.
    .byte %01000010   // .#....#.
    .byte %01000010   // .#....#.
    .byte %11111111   // ########
    .byte %11111111   // ########
    .byte %11111111   // ########
    .byte %11111111   // ########
    .byte %11111111   // ########
    .byte %01100110   // .##..##.
    .byte %11100110   // ###..##.
    .byte %11100110   // ###..##.
    .byte %00000111   // .....###
    .byte %00000111   // .....###
    // $23  monster B, frame 1
    .byte %01111110   // .######.
    .byte %01111111   // .#######
    .byte %01011101   // .#.###.#
    .byte %01111101   // .#####.#
    .byte %01111111   // .#######
    .byte %00011001   // ...##..#
    .byte %01111001   // .####..#
    .byte %01111001   // .####..#
    .byte %01111001   // .####..#
    .byte %01111101   // .#####.#
    .byte %01111001   // .####..#
    .byte %01111111   // .#######
    .byte %00111110   // ..#####.
    .byte %00011000   // ...##...
    .byte %00111000   // ..###...
    .byte %00111000   // ..###...
    // $24  monster B, frame 2
    .byte %01111110   // .######.
    .byte %01111111   // .#######
    .byte %01011101   // .#.###.#
    .byte %01111101   // .#####.#
    .byte %01111111   // .#######
    .byte %00011001   // ...##..#
    .byte %00011001   // ...##..#
    .byte %01110001   // .###...#
    .byte %01000011   // .#....##
    .byte %01100111   // .##..###
    .byte %01001111   // .#..####
    .byte %01111110   // .######.
    .byte %00110111   // ..##.###
    .byte %01110011   // .###..##
    .byte %11100111   // ###..###
    .byte %11100111   // ###..###
    // $25  monster C, frame 1
    .byte %01111110   // .######.
    .byte %11111110   // #######.
    .byte %10111010   // #.###.#.
    .byte %10111110   // #.#####.
    .byte %11111110   // #######.
    .byte %10011000   // #..##...
    .byte %10011110   // #..####.
    .byte %10011110   // #..####.
    .byte %10011110   // #..####.
    .byte %10111110   // #.#####.
    .byte %10011110   // #..####.
    .byte %11111110   // #######.
    .byte %01111100   // .#####..
    .byte %00011000   // ...##...
    .byte %00011100   // ...###..
    .byte %00011100   // ...###..
    // $26  monster C, frame 2
    .byte %01111110   // .######.
    .byte %11111110   // #######.
    .byte %10111010   // #.###.#.
    .byte %10111110   // #.#####.
    .byte %11111110   // #######.
    .byte %10011000   // #..##...
    .byte %10011000   // #..##...
    .byte %10001110   // #...###.
    .byte %11000010   // ##....#.
    .byte %11100110   // ###..##.
    .byte %11110010   // ####..#.
    .byte %01111110   // .######.
    .byte %11101100   // ###.##..
    .byte %11001110   // ##..###.
    .byte %11100111   // ###..###
    .byte %11100111   // ###..###

// ClassifyTile
// A = a tile.  Returns in A:
//    1 = empty cell     2 = a player tile ($1B-$20)     3 = a monster tile ($21-$26)
//    0 = anything else (solid: bricks, rocks, diamonds, traps, explosions ...)
// (It sits in the space of the never-drawn tiles $27 and $28.)
ClassifyTile:
    clc
    cmp #TILE_EMPTY
    beq ct_empty
    cmp #TILE_PLAYER_UD
    bcc ct_other
    clc
    cmp #TILE_MONSTER_A
    bcc ct_player
    clc
    cmp #TILE_MONSTER_D
    bcc ct_monster
    clc
ct_other:
    lda #$00
    rts
ct_empty:
    lda #$01
    rts
ct_player:
    lda #$02
    rts
ct_monster:
    lda #$03
    rts
    // $29  explosion 1 (biggest)
    .byte %00111100   // ..####..
    .byte %01111110   // .######.
    .byte %01111110   // .######.
    .byte %11100111   // ###..###
    .byte %11100111   // ###..###
    .byte %11000011   // ##....##
    .byte %11000011   // ##....##
    .byte %11100111   // ###..###
    .byte %11100111   // ###..###
    .byte %11100111   // ###..###
    .byte %11100111   // ###..###
    .byte %11100111   // ###..###
    .byte %11100111   // ###..###
    .byte %11111111   // ########
    .byte %11111111   // ########
    .byte %11111111   // ########
    // $2A  explosion 2
    .byte %00000000   // ........
    .byte %00000000   // ........
    .byte %00111100   // ..####..
    .byte %01111110   // .######.
    .byte %01111110   // .######.
    .byte %11100111   // ###..###
    .byte %11000011   // ##....##
    .byte %11000011   // ##....##
    .byte %11100111   // ###..###
    .byte %11100111   // ###..###
    .byte %11100111   // ###..###
    .byte %11100111   // ###..###
    .byte %11111111   // ########
    .byte %11111111   // ########
    .byte %00000000   // ........
    .byte %00000000   // ........
    // $2B  explosion 3
    .byte %00000000   // ........
    .byte %00000000   // ........
    .byte %00000000   // ........
    .byte %00111100   // ..####..
    .byte %01111110   // .######.
    .byte %01100110   // .##..##.
    .byte %01000010   // .#....#.
    .byte %01000010   // .#....#.
    .byte %01100110   // .##..##.
    .byte %01100110   // .##..##.
    .byte %01100110   // .##..##.
    .byte %01111110   // .######.
    .byte %01111110   // .######.
    .byte %00000000   // ........
    .byte %00000000   // ........
    .byte %00000000   // ........
    // $2C  explosion 4 (smallest)
    .byte %00000000   // ........
    .byte %00000000   // ........
    .byte %00000000   // ........
    .byte %00000000   // ........
    .byte %00000000   // ........
    .byte %00011100   // ...###..
    .byte %00110110   // ..##.##.
    .byte %00100010   // ..#...#.
    .byte %00110110   // ..##.##.
    .byte %00110110   // ..##.##.
    .byte %00111110   // ..#####.
    .byte %00000000   // ........
    .byte %00000000   // ........
    .byte %00000000   // ........
    .byte %00000000   // ........
    .byte %00000000   // ........

// The five fixed diamond positions of each room (screen addresses, rooms 1-20).
// cell(row,column) is the screen address of that cell.
diamondPos:
    .word cell(1,15),cell(4,6),cell(12,6),cell(12,8),cell(12,17)   // room 1
    .word cell(1,4),cell(3,10),cell(3,11),cell(12,5),cell(12,16)   // room 2
    .word cell(1,4),cell(1,17),cell(6,10),cell(12,5),cell(12,16)   // room 3
    .word cell(1,10),cell(12,5),cell(12,8),cell(12,13),cell(12,16)   // room 4
    .word cell(1,8),cell(1,10),cell(1,11),cell(1,13),cell(11,17)   // room 5
    .word cell(3,9),cell(3,12),cell(4,7),cell(4,14),cell(12,16)   // room 6
    .word cell(4,10),cell(7,7),cell(7,9),cell(7,12),cell(7,14)   // room 7
    .word cell(1,6),cell(1,15),cell(6,10),cell(12,6),cell(12,15)   // room 8
    .word cell(2,6),cell(2,10),cell(2,11),cell(2,15),cell(12,6)   // room 9
    .word cell(2,5),cell(2,16),cell(11,7),cell(11,14),cell(12,11)   // room 10
    .word cell(4,7),cell(11,6),cell(11,8),cell(11,13),cell(11,15)   // room 11
    .word cell(1,4),cell(1,7),cell(1,9),cell(1,12),cell(1,17)   // room 12
    .word cell(1,4),cell(1,17),cell(6,11),cell(12,5),cell(12,16)   // room 13
    .word cell(4,7),cell(4,14),cell(7,10),cell(9,7),cell(9,14)   // room 14
    .word cell(1,7),cell(1,14),cell(6,10),cell(12,7),cell(12,14)   // room 15
    .word cell(1,6),cell(1,10),cell(1,17),cell(11,13),cell(11,15)   // room 16
    .word cell(3,6),cell(3,15),cell(7,10),cell(10,6),cell(10,15)   // room 17
    .word cell(1,10),cell(3,11),cell(6,10),cell(9,11),cell(12,10)   // room 18
    .word cell(4,7),cell(4,14),cell(7,10),cell(10,7),cell(10,14)   // room 19
    .word cell(1,8),cell(1,13),cell(12,4),cell(12,10),cell(12,17)   // room 20

// ROOM MAPS.  22 maps of 14 rows x 16 columns (2 bytes per row), 28 bytes each.
// A 1 bit is a rock (red), a 0 bit is dirt.  The map is drawn at screen row 0,
// column 3.  The gaps in the walls are where the player leaves the room.
// (Room 0 is the title picture, rooms 1-20 are the playing rooms and room 21 is the
//  picture shown when the game has been won.)
roomMaps:
    // room 0  - title picture shown while waiting for a key
    .byte %11110111,%01110101   // ####.###.###.#.#
    .byte %10010101,%01000101   // #..#.#.#.#...#.#
    .byte %10110101,%01000111   // #.##.#.#.#...###
    .byte %11100101,%01000110   // ###..#.#.#...##.
    .byte %10110101,%01000111   // #.##.#.#.#...###
    .byte %10010101,%01000101   // #..#.#.#.#...#.#
    .byte %10010111,%01110101   // #..#.###.###.#.#
    .byte %00000000,%00000000   // ................
    .byte %01111101,%11011110   // .#####.###.####.
    .byte %01010101,%01010010   // .#.#.#.#.#.#..#.
    .byte %01010101,%01010010   // .#.#.#.#.#.#..#.
    .byte %01000101,%11010010   // .#...#.###.#..#.
    .byte %01000101,%01010010   // .#...#.#.#.#..#.
    .byte %01000101,%01010010   // .#...#.#.#.#..#.
    // room 1
    .byte %11111110,%01111111   // #######..#######
    .byte %10000100,%00010101   // #....#.....#.#.#
    .byte %10000000,%00011101   // #..........###.#
    .byte %10010000,%00001001   // #..#........#..#
    .byte %10101001,%00000001   // #.#.#..#.......#
    .byte %10010000,%00000001   // #..#...........#
    .byte %10010000,%00100011   // #..#......#...##
    .byte %10000010,%00000001   // #.....#........#
    .byte %11000000,%00001001   // ##..........#..#
    .byte %10000000,%01000111   // #........#...###
    .byte %10010100,%00000001   // #..#.#.........#
    .byte %10101010,%00000101   // #.#.#.#......#.#
    .byte %10100010,%00100101   // #.#...#...#..#.#
    .byte %11111110,%01111111   // #######..#######
    // room 2
    .byte %11111110,%01111111   // #######..#######
    .byte %10101000,%00000001   // #.#.#..........#
    .byte %11100011,%11000101   // ###...####...#.#
    .byte %10000010,%01001001   // #.....#..#..#..#
    .byte %10100011,%11000001   // #.#...####.....#
    .byte %10000000,%00000001   // #..............#
    .byte %10010000,%00001001   // #..#........#..#
    .byte %10100000,%00000101   // #.#..........#.#
    .byte %10000001,%10000001   // #......##......#
    .byte %10000011,%11000001   // #.....####.....#
    .byte %10100001,%10000101   // #.#....##....#.#
    .byte %10100000,%00000101   // #.#..........#.#
    .byte %11011000,%00011011   // ##.##......##.##
    .byte %11111110,%01111111   // #######..#######
    // room 3
    .byte %11111110,%01111111   // #######..#######
    .byte %10100000,%00000101   // #.#..........#.#
    .byte %11100000,%00000111   // ###..........###
    .byte %11000011,%11000011   // ##....####....##
    .byte %10000110,%01100001   // #....##..##....#
    .byte %10001100,%00110001   // #...##....##...#
    .byte %10001000,%00010000   // #...#......#....
    .byte %10001000,%00010000   // #...#......#....
    .byte %10001100,%00110001   // #...##....##...#
    .byte %10000110,%01100001   // #....##..##....#
    .byte %10000011,%11000001   // #.....####.....#
    .byte %10100000,%00000101   // #.#..........#.#
    .byte %11011000,%00011011   // ##.##......##.##
    .byte %11111110,%01111111   // #######..#######
    // room 4
    .byte %11111111,%11111111   // ################
    .byte %10000110,%11000011   // #....##.##....##
    .byte %10000011,%10010101   // #.....###..#.#.#
    .byte %10010001,%00000001   // #..#...#.......#
    .byte %10100000,%01000001   // #.#......#.....#
    .byte %10000100,%00000011   // #....#........##
    .byte %00010100,%00010000   // ...#.#.....#....
    .byte %00000001,%10000000   // .......##.......
    .byte %10010001,%10001001   // #..#...##...#..#
    .byte %10000000,%00000001   // #..............#
    .byte %10000000,%00000001   // #..............#
    .byte %10100101,%10100101   // #.#..#.##.#..#.#
    .byte %11011011,%11011011   // ##.##.####.##.##
    .byte %11111111,%11111111   // ################
    // room 5
    .byte %11111111,%11111111   // ################
    .byte %10011010,%01011001   // #..##.#..#.##..#
    .byte %10001111,%11110001   // #...########...#
    .byte %10000111,%11100001   // #....######....#
    .byte %10100011,%11000101   // #.#...####...#.#
    .byte %10000001,%10000001   // #......##......#
    .byte %00000000,%00010001   // ...........#...#
    .byte %00001000,%00000001   // ....#..........#
    .byte %10100000,%10001101   // #.#.....#...##.#
    .byte %10010000,%00000101   // #..#.........#.#
    .byte %10000010,%00000101   // #.....#......#.#
    .byte %10010000,%01000101   // #..#.....#...#.#
    .byte %10000001,%00000111   // #......#.....###
    .byte %11111111,%11111111   // ################
    // room 6
    .byte %11111110,%01111111   // #######..#######
    .byte %10000000,%00000001   // #..............#
    .byte %10110110,%01101101   // #.##.##..##.##.#
    .byte %10010100,%00101001   // #..#.#....#.#..#
    .byte %10110110,%01101101   // #.##.##..##.##.#
    .byte %10011100,%00111001   // #..###....###..#
    .byte %10110000,%00001101   // #.##........##.#
    .byte %10001000,%00010001   // #...#......#...#
    .byte %10000001,%10000001   // #......##......#
    .byte %10000110,%01100001   // #....##..##....#
    .byte %10010000,%00000001   // #..#...........#
    .byte %10011000,%00000101   // #..##........#.#
    .byte %10010000,%00001011   // #..#........#.##
    .byte %11111110,%01111111   // #######..#######
    // room 7
    .byte %11111110,%01111111   // #######..#######
    .byte %11110000,%00001111   // ####........####
    .byte %11100000,%00000111   // ###..........###
    .byte %11000011,%11000011   // ##....####....##
    .byte %10000110,%01100001   // #....##..##....#
    .byte %10000011,%11000001   // #.....####.....#
    .byte %10011111,%11111001   // #..##########..#
    .byte %10010101,%10101001   // #..#.#.##.#.#..#
    .byte %10011111,%11111001   // #..##########..#
    .byte %10000000,%00000001   // #..............#
    .byte %10000000,%00000001   // #..............#
    .byte %10000000,%00000001   // #..............#
    .byte %10000000,%00000001   // #..............#
    .byte %11111111,%11111111   // ################
    // room 8
    .byte %11111110,%01111111   // #######..#######
    .byte %11101100,%00110111   // ###.##....##.###
    .byte %10111001,%10011101   // #.###..##..###.#
    .byte %10010000,%00001001   // #..#........#..#
    .byte %10010000,%00001001   // #..#........#..#
    .byte %10010011,%11001001   // #..#..####..#..#
    .byte %10010010,%11001000   // #..#..#.##..#...
    .byte %10010011,%11001000   // #..#..####..#...
    .byte %10000000,%00000001   // #..............#
    .byte %11001000,%00010011   // ##..#......#..##
    .byte %10000001,%10000001   // #......##......#
    .byte %10010000,%00001001   // #..#........#..#
    .byte %10101000,%00010101   // #.#.#......#.#.#
    .byte %11111110,%01111111   // #######..#######
    // room 9
    .byte %11111111,%11111111   // ################
    .byte %11111111,%11111111   // ################
    .byte %11101110,%01110111   // ###.###..###.###
    .byte %10111011,%11011101   // #.###.####.###.#
    .byte %10010001,%10001001   // #..#...##...#..#
    .byte %10000000,%00000001   // #..............#
    .byte %00000000,%00000000   // ................
    .byte %00000100,%01000100   // .....#...#...#..
    .byte %10000010,%10000001   // #.....#.#......#
    .byte %10010000,%00010001   // #..#.......#...#
    .byte %10010000,%00000101   // #..#.........#.#
    .byte %10101001,%00100001   // #.#.#..#..#....#
    .byte %10101000,%00000011   // #.#.#.........##
    .byte %11111110,%01111111   // #######..#######
    // room 10
    .byte %11111111,%11111111   // ################
    .byte %11111111,%11111111   // ################
    .byte %11011000,%00011011   // ##.##......##.##
    .byte %11110100,%00101111   // ####.#....#.####
    .byte %11100010,%01000111   // ###...#..#...###
    .byte %11000001,%10000011   // ##.....##.....##
    .byte %00000010,%01000001   // ......#..#.....#
    .byte %00000010,%01000001   // ......#..#.....#
    .byte %11000001,%10000011   // ##.....##.....##
    .byte %10001000,%00010001   // #...#......#...#
    .byte %10001000,%00010001   // #...#......#...#
    .byte %10010100,%10101001   // #..#.#..#.#.#..#
    .byte %11111111,%01111111   // ########.#######
    .byte %11111111,%11111111   // ################
    // room 11
    .byte %11111110,%01111111   // #######..#######
    .byte %11000000,%00000011   // ##............##
    .byte %10001000,%10001001   // #...#...#...#..#
    .byte %10011100,%00100001   // #..###....#....#
    .byte %10110110,%10001011   // #.##.##.#...#.##
    .byte %10011100,%00000001   // #..###.........#
    .byte %00001000,%00100100   // ....#.....#..#..
    .byte %00000000,%10001000   // ........#...#...
    .byte %10000000,%00000001   // #..............#
    .byte %10000000,%00000001   // #..............#
    .byte %10010100,%00101001   // #..#.#....#.#..#
    .byte %10101010,%01010101   // #.#.#.#..#.#.#.#
    .byte %10111110,%01111101   // #.#####..#####.#
    .byte %11111111,%11111111   // ################
    // room 12
    .byte %11111111,%11111111   // ################
    .byte %10110101,%10111101   // #.##.#.##.####.#
    .byte %11111111,%11111111   // ################
    .byte %11000011,%11000011   // ##....####....##
    .byte %10000001,%10001001   // #......##...#..#
    .byte %10100101,%10100001   // #.#..#.##.#....#
    .byte %00000001,%10000000   // .......##.......
    .byte %00000001,%10001000   // .......##...#...
    .byte %10100001,%10000001   // #.#....##......#
    .byte %10000101,%10100001   // #....#.##.#....#
    .byte %10000001,%10000001   // #......##......#
    .byte %10010001,%10000101   // #..#...##....#.#
    .byte %10000001,%10000001   // #......##......#
    .byte %11111111,%11111111   // ################
    // room 13
    .byte %11111110,%01111111   // #######..#######
    .byte %10100100,%01010101   // #.#..#...#.#.#.#
    .byte %11000001,%00100011   // ##.....#..#...##
    .byte %10010000,%10010001   // #..#....#..#...#
    .byte %10001100,%00000001   // #...##.........#
    .byte %11010000,%10010101   // ##.#....#..#.#.#
    .byte %00000101,%01001000   // .....#.#.#..#...
    .byte %00100000,%10000000   // ..#.....#.......
    .byte %10010100,%00100011   // #..#.#....#...##
    .byte %10001010,%00001001   // #...#.#.....#..#
    .byte %10000000,%10100001   // #.......#.#....#
    .byte %10100010,%00000101   // #.#...#......#.#
    .byte %11010000,%01001011   // ##.#.....#..#.##
    .byte %11111110,%01111111   // #######..#######
    // room 14
    .byte %11111110,%01111111   // #######..#######
    .byte %10000000,%00000001   // #..............#
    .byte %10000000,%00000001   // #..............#
    .byte %10011100,%00111001   // #..###....###..#
    .byte %10010110,%01101001   // #..#.##..##.#..#
    .byte %10011111,%11111001   // #..##########..#
    .byte %00000011,%11000000   // ......####......
    .byte %00000010,%11000000   // ......#.##......
    .byte %10011111,%11111001   // #..##########..#
    .byte %10010110,%01101001   // #..#.##..##.#..#
    .byte %10011100,%00111001   // #..###....###..#
    .byte %10000000,%00000001   // #..............#
    .byte %10000000,%00000001   // #..............#
    .byte %11111110,%01111111   // #######..#######
    // room 15
    .byte %11111111,%11111111   // ################
    .byte %11010100,%00101011   // ##.#.#....#.#.##
    .byte %10011101,%10111001   // #..###.##.###..#
    .byte %10000000,%00000001   // #..............#
    .byte %10010000,%00001001   // #..#........#..#
    .byte %10101011,%11010101   // #.#.#.####.#.#.#
    .byte %00000010,%11000000   // ......#.##......
    .byte %00010011,%11001000   // ...#..####..#...
    .byte %10101000,%00010101   // #.#.#......#.#.#
    .byte %10000000,%00000001   // #..............#
    .byte %10000001,%10000001   // #......##......#
    .byte %10011100,%00111001   // #..###....###..#
    .byte %10010100,%00101001   // #..#.#....#.#..#
    .byte %11111110,%01111111   // #######..#######
    // room 16
    .byte %11111111,%11111111   // ################
    .byte %10101010,%10011101   // #.#.#.#.#..###.#
    .byte %10010001,%00110011   // #..#...#..##..##
    .byte %10000000,%01100001   // #........##....#
    .byte %10100100,%11001001   // #.#..#..##..#..#
    .byte %10001000,%11000001   // #...#...##.....#
    .byte %10100001,%10001000   // #.#....##...#...
    .byte %10000011,%00010000   // #.....##...#....
    .byte %11010110,%01000011   // ##.#.##..#....##
    .byte %10000110,%00000001   // #....##........#
    .byte %11001100,%00101001   // ##..##....#.#..#
    .byte %10011001,%01010101   // #..##..#.#.#.#.#
    .byte %10110000,%01111101   // #.##.....#####.#
    .byte %11111110,%01111111   // #######..#######
    // room 17
    .byte %11111111,%11111111   // ################
    .byte %10000000,%00000001   // #..............#
    .byte %10111001,%10011101   // #.###..##..###.#
    .byte %10101001,%10010101   // #.#.#..##..#.#.#
    .byte %10111000,%00011101   // #.###......###.#
    .byte %10000000,%00000001   // #..............#
    .byte %01010101,%10101010   // .#.#.#.##.#.#.#.
    .byte %00101000,%00010100   // ..#.#......#.#..
    .byte %10000000,%00000001   // #..............#
    .byte %10111001,%10011101   // #.###..##..###.#
    .byte %10101001,%10010101   // #.#.#..##..#.#.#
    .byte %10111000,%00011101   // #.###......###.#
    .byte %10000000,%00000001   // #..............#
    .byte %11111110,%01111111   // #######..#######
    // room 18
    .byte %11111110,%01111111   // #######..#######
    .byte %10011110,%01111001   // #..####..####..#
    .byte %10100000,%00000101   // #.#..........#.#
    .byte %10000000,%00000001   // #..............#
    .byte %10011111,%11111001   // #..##########..#
    .byte %10100000,%00000101   // #.#..........#.#
    .byte %00000000,%00000000   // ................
    .byte %00011111,%11111000   // ...##########...
    .byte %10100000,%00000101   // #.#..........#.#
    .byte %10000000,%00000001   // #..............#
    .byte %10011111,%11111001   // #..##########..#
    .byte %10100000,%00000101   // #.#..........#.#
    .byte %10000000,%00000001   // #..............#
    .byte %11111110,%01111111   // #######..#######
    // room 19
    .byte %11111110,%01111111   // #######..#######
    .byte %10000000,%00000001   // #..............#
    .byte %10100010,%01000101   // #.#...#..#...#.#
    .byte %10011100,%00111001   // #..###....###..#
    .byte %10000000,%00000001   // #..............#
    .byte %10010100,%00101001   // #..#.#....#.#..#
    .byte %00100010,%01000100   // ..#...#..#...#..
    .byte %00000000,%00000000   // ................
    .byte %10100010,%01000101   // #.#...#..#...#.#
    .byte %10011100,%00111001   // #..###....###..#
    .byte %10000000,%00000001   // #..............#
    .byte %10010100,%00101001   // #..#.#....#.#..#
    .byte %10100010,%01000101   // #.#...#..#...#.#
    .byte %11111111,%11111111   // ################
    // room 20
    .byte %11111110,%01111111   // #######..#######
    .byte %10010010,%01001001   // #..#..#..#..#..#
    .byte %10010000,%00001001   // #..#........#..#
    .byte %10010010,%01001001   // #..#..#..#..#..#
    .byte %10010010,%01001001   // #..#..#..#..#..#
    .byte %10010000,%00001001   // #..#........#..#
    .byte %00010010,%01001001   // ...#..#..#..#..#
    .byte %00010010,%01001001   // ...#..#..#..#..#
    .byte %10010000,%00001001   // #..#........#..#
    .byte %10010010,%01001001   // #..#..#..#..#..#
    .byte %10010010,%01001001   // #..#..#..#..#..#
    .byte %10010000,%00001001   // #..#........#..#
    .byte %10010010,%01001001   // #..#..#..#..#..#
    .byte %11111111,%11111111   // ################
    // room 21  - end-of-game picture (all rooms cleared)
    .byte %10001011,%01000100   // #...#.##.#...#..
    .byte %10001010,%01000100   // #...#.#..#...#..
    .byte %10001010,%01000100   // #...#.#..#...#..
    .byte %10001011,%01000100   // #...#.##.#...#..
    .byte %10101010,%01000100   // #.#.#.#..#...#..
    .byte %11111010,%01000100   // #####.#..#...#..
    .byte %01010011,%01110111   // .#.#..##.###.###
    .byte %00000000,%00000000   // ................
    .byte %11100111,%01110111   // ###..###.###.###
    .byte %10110101,%01010100   // #.##.#.#.#.#.#..
    .byte %10010101,%01010110   // #..#.#.#.#.#.##.
    .byte %10010101,%01010100   // #..#.#.#.#.#.#..
    .byte %10110101,%01010100   // #.##.#.#.#.#.#..
    .byte %11100111,%01010111   // ###..###.#.#.###

    .errorif * != $1E00, "game image must end at $1DFF"


    .errorif * != $1E00, "the game image must end at $1DFF"

// ======================================================================
// SCREEN AREA ($1E00-$1FFF): RELOCATED CODE AND START-UP ROUTINE
// ======================================================================
// On the original tape the game was a second file, loaded after a BASIC loader program.
// The loader had already put the machine code below into memory and set up the video
// chip.  The game itself needs the memory at $1E00 as its screen, but it clears the
// screen only after copying its own part (block C) out of it.
//
// This version is a single program, so the blocks are stored here, in the memory
// that becomes the screen, and the 'Init' routine (which replaces the tape loader)
// moves them to the places they run from.  Init is the target of the BASIC stub, so
// nothing may touch $1E00-$1FFF before it has run.
//
//   $1E00  block C  (170 bytes)   runs at $0356  - copied by the game (RelocateCode)
//   $1EAA  block A  (170 bytes)   runs at $0118  - copied by Init
//   $1F54  block B  ( 94 bytes)   runs at $02A2  - copied by Init
//   $1FB2  Init                                  - tells the machine about the new screen

// ---- Block C: the monster mover, assembled for $0356 ------------------
blockC_src:
.pseudopc $0356 {

// ======================================================================
// BLOCK C - runs at $0356-$03FF (copied there by RelocateCode)
// ======================================================================
// MoveMonsters
// Runs every 16th pass.  First the three monster types are switched to their other
// animation frame.  Then every cell of the screen is looked at; each monster found
// tries to move one cell in a random direction (4 tries): into an empty cell, onto the
// player (that cell is then no longer a player tile, so the player dies), and only rarely into
// dirt.  Directions are taken from the first half of the 1K colour RAM at $9400, which holds
// unpredictable values and is rewritten by the routine itself.
// Here ptr is (cell address - 44).
MoveMonsters:
    ldx #$03                      // animFrame+3, +4, +5
mm_toggle:
    lda animFrame,x
    jsr ToggleFrame               // the other frame ...
    sta animFrame,x
    inx
    cpx #$06
    bne mm_toggle
    lda #$D5                      // ptr = $1DD5 (first cell is $1E01) ...
    sta ptr
    sta ptr2
    lda #$1D
    sta ptr+1
    lda #$93                      // ptr2 = $93D5: direction values come from $9400+
    sta ptr2+1
mm_cell:
    ldy #CELL_HERE                // the tile in the cell: one of the three current monster frames?
    lda (ptr),y
    cmp animFrame+3
    beq mm_found
    cmp animFrame+4
    beq mm_found
    cmp animFrame+5
    beq mm_found
mm_next:
    inc ptr2                      // no: next cell
    bne mm_adv
    inc ptr2+1
mm_adv:
    clc
    inc ptr
    bne mm_step
    inc ptr+1
mm_step:
    clc
    lda ptr+1
    cmp #$1F                      // up to $1F07 (last cell $1F33)
    bne mm_cell
    lda ptr
    cmp #$07
    bne mm_cell
    rts
mm_found:
    lda #TILE_EMPTY               // found one: empty its cell
    sta (ptr),y
    ldx #$04                      // 4 tries
mm_try:
    lda (ptr2),y                  // pick a direction from the random table: 0 = up, 1 = right, 2 = down, 3 = left ...
mm_dir:
    and #$03
    pha
    tay
    lda mmDirFrame,y              // ... the animation number for it ...
    sta tmp
    lda mmDirOffset,y             // ... and the offset of the neighbouring cell
    tay
    lda (ptr),y                   // what is in that cell?
    cmp #TILE_DIRT                // dirt?
    beq mm_dirt
    cmp #TILE_EMPTY               // empty?
    beq mm_move
    cmp #TILE_PLAYER_UD           // the player?
    bcc mm_fail
    clc
    cmp #TILE_PLAYER_R+1
    bcs mm_fail
mm_move:
    clc                           // move the monster: purple ...
    ldx #COL_PURPLE
    stx tileColour
    ldx tmp                       // draw it, in its other frame
    lda animFrame,x
    jsr ToggleFrame
    jsr PutTile
    pla                           // put the direction back into the random table
    sta (ptr2),y
    jmp mm_next
mm_dirt:
    lda VIC_RASTER                // dirt: only if the raster counter is below 5 ...
    cmp #$05
    bcc mm_move
mm_fail:
    clc                           // failed: try again with a direction from the raster counter
    pla
    lda VIC_RASTER
    dex
    bne mm_dir
    and #$03                      // 4 failures: the monster stays where it is
    pha
    ldy #CELL_HERE
    jmp mm_move

// Direction tables: animation number for the monster and the neighbour offset,
// for up, right, down, left.
mmDirFrame:  .byte 3,4,3,5        // animation number (index into frameA/frameB) for up, right, down, left
mmDirOffset: .byte CELL_UP,CELL_RIGHT,CELL_DOWN,CELL_LEFT   // neighbour offset for up, right, down, left
}

// ---- Block A: enemies, traps, pause and tune, assembled for $0118 -----
blockA_src:
.pseudopc $0118 {

// ======================================================================
// BLOCK A - runs at $0118-$01C1 (in the stack page, copied there by Init)
// ======================================================================
// The original tape loader stored this block in the stack page.  It holds the code that
// places the enemies, handles walking into a trap or diamond, the pause routine, and the
// background tune with its note table.
// SpawnEnemies
// Places 2-9 monsters (tile $21, purple) and just as many traps (tile $1A, cyan) at
// random places in the dirt.  The number is 2 + (speed AND 7), so it grows as the game
// speeds up.
SpawnEnemies:
    lda speed                     // tmp = $FE - (speed AND 7) ...
    and #$07
    sta tmp
    lda #$FE
    sec
    sbc tmp
    sta tmp
se_loop:
    clc                           // ... counts up to 0, so 2 + (speed AND 7) passes
    ldx #COL_PURPLE               // purple
    lda #TILE_MONSTER_A           // a monster
    jsr PlaceRandomTile
    ldx #COL_CYAN                 // cyan
    lda #TILE_TRAP                // a trap
    jsr PlaceRandomTile
    inc tmp
    bne se_loop
    rts

// KillAtBelow
// Starts an explosion in the cell below.  Used when a rock lands on the player or a
// monster.
KillAtBelow:
    ldy #CELL_DOWN
    jmp ExplodeAt

// TouchTrapOrDiamond
// The player has walked into a diamond or a trap (A = the tile, Y = its offset).
// A diamond is collected.  A trap kills the player, unless 4 or more diamonds have been
// collected in this room: then the trap is destroyed instead.
TouchTrapOrDiamond:
    cmp #TILE_DIAMOND             // diamond?
    bne tt_trap
    jmp CollectDiamond            // yes: collect it
tt_trap:
    lda diamonds                  // a trap: how many diamonds does the player have?
    cmp #4
    bcc tt_die                    // fewer than 4: the player dies (015C)
    clc
    tya                           // enough: remove the trap, with a sound
    pha
    lda #$E6
    jsr SfxDelay
    pla
    tay
    lda #TILE_EMPTY               // empty cell
    sta (plyPtr),y
    rts
tt_die:
    jmp PlayerDies                // too few diamonds: the player dies

// PauseGame
// F1 pressed: ticks until another key is pressed.
PauseGame:
    lda #$EB
    jsr SfxDelay
    jsr GETIN
    beq PauseGame
    rts

// PlayTune
// Plays the background tune on the alto voice.  Every 16th call the next note of
// tuneNotes is played (52 notes, then it starts again).  A value below $80 is a rest.
PlayTune:
    inc tuneTick                  // count frames
    lda tuneTick
    cmp #$10                      // 16 frames per note
    bcc pt_play
    clc
    lda #$00
    sta tuneTick
    inc tunePos                   // next note
    lda tunePos
    cmp #$34                      // 52 notes
    bcc tune_store
    clc
    lda #$00
tune_store:
    sta tunePos
pt_play:
    clc
    ldx tunePos
    lda tuneNotes,x               // play it
    sta VIC_ALTO
    rts

// The tune, as values for the alto voice ($900B).
tuneNotes:
    .byte $D7,$C9,$D7,$BD,$78,$E6,$E0,$E6   // bar 1
    .byte $D7,$C9,$D7,$BD,$78,$E6   // bar 2
    .byte $E9,$EB,$E9,$EB,$E5,$E9,$E5,$E9,$E1,$E5,$E1,$E5,$E1,$E5,$E1   // bar 3
    .byte $D7,$D1,$D7,$C7,$B3,$C7,$A1,$78   // bar 4
    .byte $D7,$D1,$D7,$C7,$B3,$C7,$9F,$78   // bar 5
    .byte $D7,$DB,$DF,$DB,$DF,$D1,$D5   // bar 6
}

// ---- Block B: the room timer, assembled for $02A2 ---------------------
blockB_src:
.pseudopc $02A2 {

// ======================================================================
// BLOCK B - runs at $02A2-$02FF (copied there by Init)
// ======================================================================
// The room timer.  A scale of the digits 0-9 is drawn in column 1; a marker climbs it
// as time passes, and when the top is reached time is up and the player dies.
// TimerReset
// Restarts the timer and draws the scale.
TimerReset:
    lda #$00                      // clear the 3-byte timer ...
    sta timerLo
    sta timerMid
    lda #$F7                      // ... high byte $F7: 9 steps from the top
    sta timerHi

// DrawTimerScale
// Draws the tiles for the digits 0-9 (tiles 5-14) down column 1, from row 3, in cyan.
DrawTimerScale:
    ldy #TILE_0                   // first digit tile
    ldx #$00
dts_loop:
    tya                           // tile
    sta SCREEN+$43,x
    lda #COL_CYAN                 // cyan
    sta COLOURRAM+$43,x
    txa                           // next row
    clc
    adc #COLS
    tax
    iny
    cpy #TILE_0+10                // until 10 tiles are done
    bne dts_loop
    rts

// TimerTick
// Called every pass of the main loop.  timerLo counts frames; every time it wraps
// timerMid steps (it takes 6 passes after the first time).  After 120 steps the next
// mark on the scale is lit with a random colour and the high byte steps; when the high
// byte wraps the time has run out.
TimerTick:
    inc timerLo                   // count
    bne tt_rts
    lda #$FA                      // next wrap in 6 passes
    sta timerLo
    inc timerMid                  // next step
    lda timerMid
    cmp #120                      // 120 steps = one mark
    bcc tt_mark
    clc
    lda #$00
    sta timerMid                  // start counting again and redraw the scale
    jsr DrawTimerScale
    clc
    inc timerHi                   // next mark
    beq tt_timeup                 // wrapped: time is up
tt_mark:
    ldy timerHi                   // row of the mark = 22 x (256 - timerHi) ...
    lda #$00
tt_add:
    clc
    adc #COLS
    iny
    bne tt_add
    tay
    jsr Random7                   // random colour
    sta COLOURRAM+$43,y           // ... light that cell on the scale
tt_rts:
    rts
tt_timeup:
    jmp PlayerDies                // time is up
}

// ======================================================================
// Init  (replaces the tape's BASIC loader)
// Sets up everything the loader used to: the BASIC vectors, the video chip, the
// screen colour, the two code blocks, and the keys.  Then it starts the game.
// ======================================================================
Init:
    .errorif Init != 8114, "the BASIC stub must SYS to Init"
    lda  #$C2
    sta  $0314                    // IRQ vector low byte: $EAC2 skips the cursor flash (the high byte is already $EA)
    lda  #$03
    sta  $911E                    // VIA 1 interrupt enable: switch off the RESTORE key (NMI)
    sta  $0286                    // KERNAL text colour = cyan (the screen clear at Start uses it for the colour RAM)
    lda  #$12
    sta  $9001                    // vertical screen origin
    lda  #$1F
    sta  $9003                    // 15 rows, 8x16 characters
    lda  #$FE
    sta  $9005                    // screen at $1E00 (with $9002 bit 7), characters at $1800
    lda  #$3B
    sta  VIC_COLOURS              // cyan background and border (the game sets black in InitGame)
    ldx  #170
!:  lda  blockA_src-1,x           // block A: 170 bytes to $0118
    sta  $0118-1,x
    dex
    bne  !-
    ldx  #94
!:  lda  blockB_src-1,x           // block B: 94 bytes to $02A2
    sta  $02A2-1,x
    dex
    bne  !-
    ldx  #3
!:  lda  defaultKeys,x            // default direction keys
    sta  keyUp,x
    dex
    bpl  !-
    jmp  Start

// The tape loader asked the player to choose the keys (or to use a joystick).
// With no loader the keys are fixed here; change them if you like (PETSCII codes).
defaultKeys: .byte 'Q','A','O','P'   // up, down, left, right
    .errorif * > $2000, "Init does not fit in the screen area"
