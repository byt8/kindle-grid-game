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
//    This is one single program that loads at $1001 and starts with 'SYS 7680'.
//    The original was a BASIC loader plus a 3.5K machine-code file that expected
//    three small blocks of code to have been put in place by the loader (see the
//    comment above 'Init' near the end of the file).
//
//  OPTIMISED VERSION
//    This is the optimised source.  The game plays exactly as the original did (it
//    was checked by running the original and this version side by side in an emulator
//    with the same inputs and comparing the screen, colour and game variables every
//    pass).  The changes:
//      * unused code and data removed (NOPs, the map address tables, one animation
//        entry, a few leftover instructions)
//      * the four move routines share one routine (DoMove), the four explosion
//        stages and the two falling-rock stages are looked up instead of tested
//        one by one, ChangeRoom uses one table lookup, the two screen-fill loops
//        are one loop, and SealTopExit lives in DrawRoom
//      * redundant CLC/SEC instructions removed (the carry flag was checked)
//      * the three pass counters (controls, monsters, rocks) share TickCount, the
//        'empty a cell' code is shared (EraseAtY), and routines that ended by jumping
//        to the next routine now simply run on into it
//      * the start-up code now sits at the front of the screen area so it never moves
//      * the enemy/trap/pause/tune code (old block A) and the room timer (old block B)
//        now run where they are stored instead of being copied to low memory
//    Only one block (the monster mover, old block C) is still stored in the screen
//    area and copied, so the program now ends at $1F95 instead of the top of RAM:
//    about 106 bytes of the 4K are free after the end of the file.  Inside the file,
//    7 bytes are free at 'freeSpace' (just before the exit tables) and a tile of its
//    own is free: tile $11 (16 bytes at 'tile11').  Everything after 'freeSpace' stays
//    at its original address.
//
//    Code stored in the screen area gets overwritten by the text BASIC prints (see the
//    warning above 'Init'), so only the screen rows below the text (rows 11 on) can hold
//    the relocated block.  Any new code you add can go in the free bytes after the end
//    of the file only if it is also relocated (see RelocateCode); otherwise it has to
//    fit in 'freeSpace'.
//
//  MUSIC
//    The M key switches the background tune on and off (it starts on).
//
//  MEMORY MAP (unexpanded VIC-20)
//    $0243-$03FF   game variables (see the .label list below), and block C
//    $0356-$03FA   block C: MoveMonsters, with its two direction tables
//    $1001-$100C   BASIC stub
//    $100D-$1031   sound effects
//    $1032-$10AE   title screen and fly-through, room diamonds
//    $10AF-$1135   collecting diamonds, leaving a room, game won
//    $1136-$1187   random placement of tiles
//    $1188-$1282   rock rolling, player death, pushing rocks
//    $1283-$1322   the scan for rocks, diamonds and explosions
//    $1323-$1380   random numbers, delays, digit columns, the pass counters
//    $1381-$1441   start-up, main loop, keyboard, joystick, pause, music switch
//    $1442-$14C1   game delay, player movement
//    $14C2-$15AF   changing rooms, game start, drawing the player, animation frames
//    $15B0-$15C0   code relocation
//    $15C1-$162D   score
//    $162E-$1719   pointer arithmetic, screen set-up, drawing tiles, room map drawing
//    $171A-$179C   SpawnEnemies, TouchTrap, PlayTune and the tune
//    $179D-$17F8   the room timer
//    $17F9-$17FF   FREE (7 bytes)
//    $1800-$184F   room exit tables
//    $1850-$1ACF   the 8x16 character set (tile $11 is free; ClassifyTile is hidden in
//                  the space of tiles $27 and $28, with 4 spare bytes)
//    $1AD0-$1B97   positions of the five fixed diamonds in each room
//    $1B98-$1DFF   22 room maps
//    $1E00-$1FFF   screen memory (22 x 15 = 330 bytes used)
//                   -> before the game starts: Init (rows 0-3) and block C (rows 11+)
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
.label room          = $033D   // current room (0 = title picture, 1-20 = playable, 21 = end picture)
.label tileColour    = $033F   // colour to write with the next tile
.label speed         = $0340   // game speed; larger = shorter delay per frame
.label lives         = $0341   // lives left, stored as a digit tile (TILE_0 = none left)
.label scoreDigits   = $0342   // 8 digit tiles, most significant first ($0342-$0349)
.label hiDigits      = $034A   // 8 digit tiles of the hi-score ($034A-$0351)
.label musicMask     = $0263   // $FF = music on, $00 = music off (the M key flips it)
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
.const KEY_M           = $4D   // toggles the music on and off
.const JOY_UP          = $04
.const JOY_DOWN        = $08
.const JOY_LEFT        = $10
.const JOY_FIRE        = $20
.const JOY_RIGHT       = $80
// The background tune
.const TUNE_SLOT       = 14   // passes of the main loop per note
.const TUNE_GATE       = 9    // a note sounds for this many of them, then stops
.const TUNE_LENGTH     = 32   // notes in the tune table
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
    .text "7680"                    // = Init (checked by the .errorif at Init)
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
    asl                           // (room < 32 so the carry is clear here)
    adc room
    asl
    tax
    lda #$FB                      // 5 fixed diamonds (tmp counts $FB..$FF)
    sta tmp
prd_loop:
    jsr PlaceFixedDiamond
    inx
    inx
    inc tmp
    bne prd_loop
    jmp PlaceRandomDiamonds       // plus 3 random ones

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
    jsr EraseAtY                  // remove the diamond (draw an empty cell)
    inc diamonds                  // one more diamond collected in this room
    lda diamonds
    cmp #DIAMONDS_PER_ROOM
    bcc cd_rts                    // fewer than 8: carry on
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
    inc speed                     // game gets faster: shorter delay in GameDelay, more enemies in SpawnEnemies
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
    inc ptr                       // next cell
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
    jmp EraseAtY                  // and empty it (black, empty tile)

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
    jmp EraseAtY
push_rts:
    rts

// ======================================================================
// SCANNING THE PLAYFIELD FOR ROCKS (every 9th frame)
// ======================================================================
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
    txa                           // ... and the tile for this stage (TILE_ROCK + stage)
    clc
    adc #TILE_ROCK
    ldx #$02                      // red
    stx tileColour
    jmp PutTile
rar_fall:
    jsr RockStartFall             // begin to fall
rar_done:
    jmp sr_next

// Bass pitch for each rock stage (the tile of a stage is TILE_ROCK + stage).
rockSounds:   .byte $00,$D2,$D7,$DC                                // bass pitch for each stage

// PutRockFrameX0
// Puts a resting rock (stage 0) into the cell at offset Y.
PutRockFrameX0:
    ldx #$00
    jmp PutRockFrame

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
    cmp #TILE_ROCK                // resting rock
    beq RockAtRest
    bcc sr_next                   // lower tiles: nothing to do
    cmp #TILE_FALL3               // falling rock, stage 3
    beq RockFalling
    bcc sr_tumble                 // falling rock, stage 1 or 2
    cmp #TILE_DIAMOND             // diamond
    bne sr_notdiamond
    jsr Random7                   // gives the diamond a new random colour (so it glitters)
    jsr PutColour
    jmp sr_next
sr_notdiamond:
    cmp #TILE_EXPLODE1            // explosion stages 1-4
    bcc sr_next
    cmp #TILE_EXPLODE1+4
    bcs sr_next
    tax                           // X = the tile number: ExplodeAt looks up the next stage
    jmp ExplodeAt
sr_tumble:
    sbc #TILE_ROCK-2              // (carry is clear) X = stage 2 or 3
    tax
    jsr PutRockFrameHere          // the rock moves on to its next animation stage
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
    ldy #CELL_DOWN                // start an explosion in the cell below
    ldx #TILE_EXPLODE1-1

// ExplodeAt
// X = tile number of an explosion stage ($28 for a new one), Y = offset of the cell.
// Makes the noise for that stage and writes the next stage's tile, in cyan.  The
// last stage ($2C) turns into a falling rock (tile $16).
ExplodeAt:
    lda explodeSounds-(TILE_EXPLODE1-1),x   // noise volume for the step ...
    sta VIC_NOISE
    lda explodeTiles-(TILE_EXPLODE1-1),x    // ... and the next tile
    ldx #COL_CYAN
    stx tileColour
    jsr PutTile
    jmp sr_next
explodeTiles:  .byte TILE_EXPLODE1,TILE_EXPLODE1+1,TILE_EXPLODE1+2,TILE_EXPLODE1+3,TILE_FALL1   // tile written for explosion step 0-4 (step 4 becomes a falling rock)
explodeSounds: .byte $C8,$D7,$E6,$F5,$00   // noise voice value for each step

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
    rts

// ======================================================================
// MAIN LOOP
// ======================================================================
// TickCount divides the main loop's speed down.  X selects a counter (0 = controls,
// 1 = monsters, 2 = rocks); the counter is stepped, and when it reaches its limit it is
// reset and the carry flag comes back set: the controls are read every 14th pass, the
// monsters move every 16th pass, and the rocks are scanned every 9th pass.
TickCount:
    inc inputTick,x               // count this pass (inputTick, monsterTick and rockTick are in a row)
    lda inputTick,x
    cmp tickLimits,x              // time for this one?  (carry set = yes)
    bcc tc_wait
    lda #$00
    sta inputTick,x
tc_wait:
    rts
tickLimits: .byte 14,16,9         // passes between reads of the controls, monster moves, rock scans

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
    jsr PlayTune                  // background tune
    jsr TimerTick                 // room timer
    ldx #$02                      // rocks every 9th pass:
    jsr TickCount
    bcc ml_monsters
    jsr ScanRocks                 //   move rocks, explode, sparkle
ml_monsters:
    ldx #$01                      // monsters every 16th pass:
    jsr TickCount
    bcc ml_alive
    jsr MoveMonsters              //   move all the monsters one step
ml_alive:
    jsr CheckPlayerAlive          // is the player still alive?
    ldx #$00                      // controls every 14th pass:
    jsr TickCount
    bcc ml_stop
    jsr ReadJoystick              //   read the joystick ...
    jsr ReadKeyboard              //   ... and the keyboard
ml_stop:
    lda #$83                      // keep the RUN/STOP key switched off (points the STOP-key
    sta ISTOP                     // test vector at a harmless routine)
    jsr StirRandom                // random number
    jsr GameDelay                 // game speed delay
    jmp MainLoop

// ======================================================================
// CONTROLS
// ======================================================================
// ReadKeyboard
// Direction keys are the four keys held in keyUp-keyRight; F1 pauses, F3 starts a
// new game, M switches the music on and off; any other key acts like the joystick
// fire button (push a rock).
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
    beq PauseGame
    cmp #KEY_F3                   // F3 = new game
    beq rk_reset
    cmp #KEY_M                    // M = music on / off
    beq ToggleMusic
    cmp keyUp                     // is it one of the four direction keys?
    beq go_up
    cmp keyDown
    beq go_down
    cmp keyLeft
    beq go_left
    cmp keyRight
    beq go_right
    jmp PushRock                  // any other key = push

// PauseGame
// F1 pressed: ticks until another key is pressed.
PauseGame:
    lda #$EB
    jsr SfxDelay
    jsr GETIN
    beq PauseGame
    rts

// ToggleMusic
// Flips the music switch.  PlayTune keeps writing the tune, ANDed with musicMask, so
// when the mask is $00 the voice is silent.
ToggleMusic:
    lda musicMask
    eor #$FF
    sta musicMask
    rts
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
// The four moves.  Each loads the offset of the cell it wants to enter (Y), the
// animation number (X) and the distance to move (A), and carries on in DoMove.  The
// carry flag says whether the move is backwards (set = subtract from plyPtr).
go_push:
    jmp PushRock
go_up:
    ldy #CELL_UP
    ldx #$02                      // up/down animation
    lda #COLS                     // one row back
    sec
    bcs DoMove
go_down:
    ldy #CELL_DOWN
    ldx #$02
    lda #COLS                     // one row on
    clc
    bcc DoMove
go_left:
    ldy #CELL_LEFT
    ldx #$00                      // left animation
    lda #$01                      // one cell back
    sec
    bcs DoMove
go_right:
    ldy #CELL_RIGHT
    ldx #$01                      // right animation
    lda #$01                      // one cell on
    clc
// DoMove
// The player may move into an empty cell or into dirt (which he digs through).
// Anything else is dealt with by MoveBlocked.
// Moving = work out the next animation frame for that direction, erase the
// player, move plyPtr and redraw him in white in the new cell.
DoMove:
    sta tmp                       // distance to move
    php                           // and the direction (carry)
    lda (plyPtr),y                // what is in the cell we want to enter?
    cmp #TILE_EMPTY
    beq dm_move
    cmp #TILE_DIRT
    beq dm_move
    plp
    jmp MoveBlocked
dm_move:
    jsr PlayerFrame               // next animation frame (A)
    pha
    jsr ErasePlayer               // erase the player ...
    pla
    tay
    plp
    ldx #plyPtr
    lda tmp
    bcs dm_back                   // ... move the pointer ...
    jsr AddToZP
dm_draw:
    tya                           // ... and draw the player in the new cell
    jmp DrawTileWhite
dm_back:
    jsr SubFromZP
    jmp dm_draw
// MoveBlocked
// A = the tile the player ran into.  Y = its offset from plyPtr.
//   diamond:         CollectDiamond
//   trap:            TouchTrap
//   brick wall:      TryLeaveRoom (a way out, if the room is cleared)
//   monster:         the player dies
//   anything else (rock...) cannot be walked on.
MoveBlocked:
    cmp #TILE_DIAMOND
    bne mb_notdiamond
    jmp CollectDiamond
mb_notdiamond:
    cmp #TILE_TRAP
    beq mb_trap
    cmp #TILE_BRICK
    beq mb_brick
    cmp #TILE_MONSTER_A           // tiles $21-$25 are monsters
    bcc mb_rts
    cmp #TILE_MONSTER_C+1
    bcs mb_rts
    jmp PlayerDies
mb_trap:
    jmp TouchTrap
mb_brick:
    jmp TryLeaveRoom
mb_rts:
    rts

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
// (DrawRoom closes a gap in the top wall with bricks, but they are still bricks: once
// the room is cleared the player can walk into them to leave through the top.)
ChangeRoom:
    ldx #$01                      // X = entry position
    lda room                      // A = current room number
    cpy #CELL_DOWN
    beq cr_find
    inx
    cpy #CELL_LEFT
    beq cr_find
    inx
    cpy #CELL_UP
    beq cr_find
    inx                           // right
cr_find:
    tay                           // Y = room; the entry position also says which exit table to use:
    stx tmp                       // table 1 = down, 2 = left, 3 = up, 4 = right (20 bytes each)
cr_table:
    dec tmp
    beq cr_read
    tya
    clc
    adc #20                       // next table
    tay
    bne cr_table
cr_read:
    lda exits-1,y                 // the room on the other side
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
    sta speed                     // slowest speed

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
// EraseAtY       : draws an empty cell (black) at plyPtr+Y
// DrawTileAtY    : draws tile A in colour X at plyPtr+Y
// PutTileAtPlayer  : draws tile A in colour tileColour at plyPtr+Y
DrawPlayer:
    lda #TILE_PLAYER_UD
DrawTileWhite:
    ldx #$01
    ldy #CELL_HERE
    bne DrawTileAtY               // (always)
ErasePlayer:
    ldy #CELL_HERE
EraseAtY:
    lda #TILE_EMPTY
    ldx #$00
DrawTileAtY:
    stx tileColour
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
    cmp #TILE_MONSTER_A
    bcs cpa_dead
    rts
cpa_dead:
    jmp PlayerDies

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
// 3 = monster A, 4 = monster C, 5 = monster B.
// frameA is the first frame, frameB the second.
frameA: .byte TILE_PLAYER_L,TILE_PLAYER_R,TILE_PLAYER_UD,TILE_MONSTER_A,TILE_MONSTER_C,TILE_MONSTER_B   // first frame: player left, right, up/down; monsters A, C, B
frameB: .byte TILE_PLAYER_L+1,TILE_PLAYER_R+1,TILE_PLAYER_UD+1,TILE_MONSTER_A+1,TILE_MONSTER_C+1,TILE_MONSTER_B+1   // second frame of each animation

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
    sta plyPtr+1
    stx entryPos                  // remember the entry position so the room can be restarted
    rts

// Start position tables (low and high bytes of plyPtr).
startPtrLo: .byte $79,$F5,$80,$FD,$71   // plyPtr (low byte) for start positions 0-4
startPtrHi: .byte $1E,$1D,$1E,$1E,$1E   // plyPtr (high byte) for start positions 0-4

// ======================================================================
// SCORE
// ======================================================================
// RelocateCode
// Copies the code of MoveMonsters (block C) that was loaded into the screen area
// down to $0356.  The screen is about to be cleared, which would destroy them.
RelocateCode:
    ldx #blockC_end-blockC_src    // length of block C
rc_loop:
    lda blockC_src-1,x
    sta MoveMonsters-1,x
    dex
    bne rc_loop
    lda #$83                      // and switch the RUN/STOP key off, as MainLoop does
    sta ISTOP
    rts

// ScoreAdd10
// Adds 10 to the score: the last digit is incremented ten times with decimal carry.
// The digits are tiles: TILE_0 ($05) .. TILE_0+9 ($0E).  It then runs on into DrawScore,
// so the score column is redrawn every time.
ScoreAdd10:
    ldy #$0A                      // 10 increments
sa_outer:
    ldx #$08                      // start at the last digit
sa_digit:
    inc lives,x                   // add one
    lda lives,x
    cmp #TILE_0+10                // passed '9'?
    bcc sa_next
    lda #TILE_0                   // yes: back to '0' and carry into the next digit
    sta lives,x
    dex
    bne sa_digit
sa_next:
    dey
    bne sa_outer

// DrawScore  (entered from ScoreAdd10)
// Draws the 8 score digits in screen column 20.
DrawScore:
    ldx #$00
    ldy #CELL_HERE
    lda #$08
    sta colPtr
    jmp DrawDigitColumn

// ClearAllScores / ClearScore
// ClearAllScores sets the score and hi-score to 0; ClearScore only the score.
ClearAllScores:
    ldx #$10
    bne cs_fill
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

// UpdateHiScore
// Compares the score with the hi-score digit by digit; if the score is higher it is
// copied to the hi-score.  Then the hi-score column (screen column 21) is redrawn.
UpdateHiScore:
    ldx #$00
uh_cmp:
    lda scoreDigits,x
    cmp hiDigits,x
    beq uh_equal
    bcc uh_draw
uh_copy:
    lda scoreDigits,x
    sta hiDigits,x
    inx
    cpx #$08
    bcc uh_copy
uh_draw:
DrawHiScore:
    ldx #$08
    ldy #CELL_RIGHT
    lda #$10
    sta colPtr
    jmp DrawDigitColumn
uh_equal:
    inx
    cpx #$08
    bcc uh_cmp
    bcs uh_draw                   // (always: the carry is set here)

// ScoreAdd10Hi
// Adds 10 points and updates the hi-score (used when a monster is crushed).
ScoreAdd10Hi:
    jsr ScoreAdd10
    jmp UpdateHiScore

// ======================================================================
// 16-BIT POINTER ARITHMETIC AND SCREEN OUTPUT
// ======================================================================
// AddToZP / SubFromZP
// Adds A to (or subtracts it from) the 16-bit zero page value at X.
AddToZP:
    clc
    adc ptr,x                     // low byte
    sta ptr,x
    lda ptr+1,x                   // high byte plus the carry
    adc #$00
    sta ptr+1,x
    rts
SubFromZP:
    eor #$FF                      // ptr - A = ptr + (NOT A) + 1
    sec
    adc ptr,x
    sta ptr,x
    lda ptr+1,x
    adc #$FF                      // high byte: add $FF plus the carry
    sta ptr+1,x
    rts

// DrawBorderScreen
// Fills the whole screen with brick, colours it, puts the labels "SC" and "HI" and the
// lives digit in place, draws the score and hi-score columns and finally the map of the
// current room.
DrawBorderScreen:
    ldx #165                      // 2 x 165 cells (22 x 15)
dbs_fill:
    lda #TILE_BRICK
    sta SCREEN-1,x
    sta SCREEN+$A4,x
    lda #COL_PURPLE               // the whole screen purple ...
    sta COLOURRAM-1,x
    sta COLOURRAM+$A4,x
    dex
    bne dbs_fill
    lda #COL_GREEN                // ... except the lives digit, SC and HI, which are green
    sta COLOURRAM+$2A
    sta COLOURRAM+$56
    sta COLOURRAM+$57
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
// exit cells) the gap is closed with bricks.
DrawRoom:
    jsr DrawRoomMap
    lda SCREEN+$0A                // the top exit cell: dirt?
    cmp #TILE_DIRT
    bne dr_rts
    lda #TILE_BRICK               // yes: seal both top exit cells with brick
    sta SCREEN+$0A
    sta SCREEN+$0B
    lda #COL_PURPLE
    sta COLOURRAM+$0A
    sta COLOURRAM+$0B
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
    lda tmp                       // next bit
    lsr
    bne dmb_bit
    inc ptr2                      // next map byte
    bne dmb_rts
    inc ptr2+1
dmb_rts:
    rts

// DrawRoomMap
// Draws the map of the current room: 14 rows of 16 cells (two bytes per row), starting at
// row 0, column 3 of the screen (the room's map is at roomMaps + 28 * room).
DrawRoomMap:
    lda #<roomMaps                // ptr2 = roomMaps + 28 * room
    sta ptr2
    lda #>roomMaps
    sta ptr2+1
    ldx room
    beq drm_go
drm_add:
    lda ptr2
    clc
    adc #28
    sta ptr2
    bcc drm_nc
    inc ptr2+1
drm_nc:
    dex
    bne drm_add
drm_go:
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
// ======================================================================
// ENEMIES, TRAPS, PAUSE AND TUNE
// (On the tape these four routines and the tune were a block of code that the loader
// stored in the stack page at $0118.  They now simply sit here and run in place.)
// ======================================================================
// SpawnEnemies
// Places 2-9 monsters (tile $21, purple) and just as many traps (tile $1A, cyan) at
// random places in the dirt.  The number is 2 + (speed AND 7), so it grows as the game
// speeds up.
SpawnEnemies:
    lda speed                     // tmp = $FE - (speed AND 7) ...
    and #$07
    eor #$FF
    sec
    sbc #$01
    sta tmp
se_loop:
    ldx #COL_PURPLE               // purple
    lda #TILE_MONSTER_A           // a monster
    jsr PlaceRandomTile
    ldx #COL_CYAN                 // cyan
    lda #TILE_TRAP                // a trap
    jsr PlaceRandomTile
    inc tmp
    bne se_loop
    rts

// TouchTrap
// The player has walked into a trap (Y = its offset).  It kills the player, unless
// 4 or more diamonds have been collected in this room: then the trap is destroyed
// instead.
TouchTrap:
    lda diamonds                  // how many diamonds does the player have?
    cmp #4
    bcc tt_die                    // fewer than 4: the player dies
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

// PlayTune
// Plays the background tune - a one-voice version of "Popcorn" - on the soprano voice.
// It is called on every pass of the main loop.  The tune is a table of notes, one per
// slot of TUNE_SLOT passes; each note sounds for the first TUNE_GATE passes of its slot
// and is then silent (so the notes are detached, as in the original).  A value below
// $80 is a rest.  When the table runs out (TUNE_LENGTH slots) the tune starts again,
// and it also starts again at the beginning of every room (PlaceRoomDiamonds).
PlayTune:
    inc tuneTick                  // count passes
    lda tuneTick
    cmp #TUNE_SLOT                // time for the next note?
    bcc pt_play
    lda #$00
    sta tuneTick
    inc tunePos                   // next note
    lda tunePos
    cmp #TUNE_LENGTH              // end of the tune?
    bcc tune_store
    lda #$00                      // yes: back to the start
tune_store:
    sta tunePos
pt_play:
    ldx tunePos
    lda tuneNotes,x               // the note of this slot ...
    ldy tuneTick
    cpy #TUNE_GATE                // ... sounds only for the first part of it
    bcc pt_sound
    lda #$00
pt_sound:
    and musicMask                 // silent when the music is switched off (M key)
    sta VIC_SOPRANO
    rts

// The tune: Popcorn in A minor, as soprano voice values (PAL pitch, each within about
// 16 cents of the true note).  The register value is 128 + (128 - period) so a bigger
// value is a higher note:
//   C4=$BE  E4=$CB  F4=$CE  G4=$D4  A4=$D9  B4=$DD  C5=$DF
// Phrase 1 is A G A E C E A (rest) and is played twice, then phrase 2 follows.
tuneNotes:
    .byte $D9,$D4,$D9,$CB,$BE,$CB,$D9,$00   // A  G  A  E  C  E  A  (rest)
    .byte $D9,$D4,$D9,$CB,$BE,$CB,$D9,$00   // phrase 1 again
    .byte $D9,$DD,$DF,$DD,$DF,$D9,$DD,$D9   // A  B  C  B  C  A  B  A
    .byte $DD,$D4,$D9,$D4,$D9,$CE,$D9,$00   // B  G  A  G  A  F  A  (rest)
tuneEnd:
    .errorif tuneEnd-tuneNotes != TUNE_LENGTH, "the tune table must be TUNE_LENGTH bytes"

// ======================================================================
// ROOM TIMER
// (On the tape this was a block of code that the loader stored at $02A2.)
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
    lda #$00
    sta timerMid                  // start counting again and redraw the scale
    jsr DrawTimerScale
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

// FREE SPACE: everything from here up to the exit tables ($1800) is unused (7 bytes
// in this version).  New code can go here; it shrinks the gap, and the check below
// stops the build if the code ever grows past $1800.
freeSpace:
    .fill $1800-*,0
    .errorif * != $1800, "the code before the exit tables is too big"

// Exits.  For each room 1-20 (index room-1) these give the room reached by walking out
// of the room through the bottom, left, top or right edge.  0 means there is no exit.
// The order is the same as the entry positions used by ChangeRoom (1 = down ... 4 = right).
exits:
exitDown:
    .byte $06,$07,$08,$00,$00,$0B,$00,$0D,$0E,$00    // rooms 1-10
    .byte $00,$00,$12,$13,$14,$01,$02,$03,$00,$00    // rooms 11-20
exitLeft:
    .byte $00,$00,$00,$03,$04,$00,$00,$00,$08,$09    // rooms 1-10
    .byte $0F,$0B,$0C,$0D,$0E,$00,$10,$11,$12,$13    // rooms 11-20
exitUp:
    .byte $10,$11,$12,$00,$00,$01,$02,$03,$00,$00    // rooms 1-10
    .byte $06,$00,$08,$09,$00,$00,$00,$0D,$0E,$0F    // rooms 11-20
exitRight:
    .byte $00,$00,$04,$05,$00,$00,$00,$09,$0A,$00    // rooms 1-10
    .byte $0C,$0D,$0E,$0F,$0B,$11,$12,$13,$14,$00    // rooms 11-20

// ======================================================================
// CHARACTER SET   (the VIC fetches characters from $1800 up: $9005 = $FE)
// ======================================================================
// The video chip is in 8x16 character mode ($9003 bit 0 = 1), so every character is
// 16 bytes and character N is at $1800 + N*16.  Characters $00-$04, $27, $28
// and everything from $2D on overlap tables and routines (those are never displayed).
// Characters $05-$10 come first, then the unused tile $11, then the rest.  The tile numbers are the screen codes in SCREEN.
    .errorif * != $1850, "the character set must start at $1850"
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

    // $11  unused (it held the SealTopExit routine, now part of DrawRoom): free for a new tile
tile11:
    .fill 16,0
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
    cmp #TILE_EMPTY
    beq ct_empty
    cmp #TILE_PLAYER_UD
    bcc ct_other
    cmp #TILE_MONSTER_A
    bcc ct_player
    cmp #TILE_MONSTER_D
    bcc ct_monster
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
classifyEnd:
    .errorif * > $1A90, "ClassifyTile must fit in the space of tiles $27 and $28"
    .fill $1A90-*,0                // (unused bytes of the tile space)
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
// SCREEN AREA ($1E00-$1FFF): START-UP ROUTINE AND RELOCATED CODE
// ======================================================================
// On the original tape the game was a second file, loaded after a BASIC loader program.
// The loader had already put machine code into memory and set up the video chip.  The game needs the memory at $1E00 as its screen, but it clears the
// screen only after copying its own part (block C) out of it.
//
// This version is a single program, so that code (block C) is stored here, in the
// memory that becomes the screen, and the game copies it to where it runs from.  'Init'
// replaces the tape loader and is the target of the BASIC stub.
//
// WARNING - where things can go in this area.  When the program is run from BASIC the
// screen editor writes text into this memory (READY., the RUN you type, a flashing
// cursor) before Init has run, and anything stored under that text is destroyed.  That
// text appears in screen rows 4 to 10 ($1E58-$1EF1).  So:
//   $1E00-$1E57   rows 0-3: Init lives here (it must end before $1E58)
//   $1E58-$1EF1   rows 4-10: nothing may be stored here
//   $1EF2-$1FFF   rows 11 on: block C is stored here
//
//   block C  runs at $0356  - copied by the game itself (RelocateCode)
// ======================================================================
// Init  (replaces the tape's BASIC loader)
// Sets up everything the loader used to: the BASIC vectors, the video chip, the
// screen colour, the music switch and the keys.  Then it starts the game.
// ======================================================================
Init:
    .errorif Init != 7680, "the BASIC stub must SYS to Init"
    lda  #$C2
    sta  $0314                    // IRQ vector low byte: $EAC2 skips the clock / STOP-key update at $EABF (the high byte is already $EA)
    lda  #$03
    sta  $911E                    // VIA 1 interrupt enable: switch off the RESTORE key (NMI)
    sta  $0286                    // KERNAL text colour = cyan (the screen clear at Start uses it for the colour RAM)
    lda  #$FF
    sta  musicMask                // music on (the M key switches it off and on)
    lda  #$12
    sta  $9001                    // vertical screen origin
    lda  #$1F
    sta  $9003                    // 15 rows, 8x16 characters
    lda  #$FE
    sta  $9005                    // screen at $1E00 (with $9002 bit 7), characters at $1800
    lda  #$3B
    sta  VIC_COLOURS              // cyan background and border (the game sets black in InitGame)
    ldx  #3
!:  lda  defaultKeys,x            // default direction keys
    sta  keyUp,x
    dex
    bpl  !-
    jmp  Start

// The tape loader asked the player to choose the keys (or to use a joystick).
// With no loader the keys are fixed here; change them if you like (PETSCII codes).
defaultKeys: .byte 'Q','A','O','P'   // up, down, left, right

    .errorif * > $1E58, "Init must end before screen row 4 (the screen editor writes text there)"
    .fill $1EF2-*,0               // rows 4-10 are not used (see the warning above)

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
    lda #$D5                      // ptr = $1DD5 (first cell is $1E01)
    sta ptr
    lda #$1D
    sta ptr+1
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
    inc ptr                       // no: next cell
    bne mm_step
    inc ptr+1
mm_step:
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
    lda ptr                       // ptr2 = ptr + $7600: the direction values come from $9400 up
    sta ptr2
    lda ptr+1
    clc
    adc #$76
    sta ptr2+1
    ldx #$04                      // 4 tries
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
    cmp #TILE_PLAYER_R+1
    bcs mm_fail
mm_move:
    ldx #COL_PURPLE               // move the monster: purple ...
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
    pla                           // failed: try again with a direction from the raster counter
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
blockC_end:


    .errorif * > $2000, "the blocks do not fit in the screen area"
