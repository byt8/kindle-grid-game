#define _GNU_SOURCE
/* rockman.c -- the 1984 VIC-20 cave game, on the Kindle Touch.
 *
 * Rules are the routines in scratch/Rockman.asm (CollectDiamond, TryLeaveRoom,
 * ScanRocks, MoveMonsters, TouchTrap, TimerTick, PushRock). The picture is
 * 22x15 cells of the original 8x16 tiles, scaled to 27x40 pixels from (3, 6)
 * so the whole screen fits a 600x800 panel, with tap buttons underneath.
 * Sound is not played.
 *
 * Two deliberate differences, both recorded in .agent-kb/areas/rockman.md:
 *   - an exit of 0 stays in the room (the table's own "no exit"), and
 *   - a diamond sitting on the cell the player appears in is collected,
 *     not painted over and lost.
 */
#include "eink.h"
#include "kframe.h"
#include "rockman_data.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SCR_ROWS 15
#define SCR_COLS 22
#define CELL_W   27
#define CELL_H   40
#define ORIGIN_X 3
#define ORIGIN_Y 6

_Static_assert(ORIGIN_X + SCR_COLS * CELL_W <= 600, "grid fits across");
_Static_assert(ORIGIN_Y + SCR_ROWS * CELL_H <= 640, "grid sits above the buttons");
_Static_assert(RM_TILE_FALL1 == RM_TILE_ROCK + 1, "fall stage 1 follows the rock tile");
_Static_assert(RM_TILE_FALL3 == RM_TILE_ROCK + 3, "fall stage 3 is rock+3");

enum { DIR_UP = 0, DIR_RIGHT, DIR_DOWN, DIR_LEFT };
enum { ACT_LEFT = 0, ACT_UP, ACT_RIGHT, ACT_DOWN, ACT_PUSH, ACT_QUIT, ACT_N };
enum { PHASE_TITLE = 0, PHASE_PLAY, PHASE_WIN };

static const int MDR[4] = {-1, 0, 1, 0};
static const int MDC[4] = {0, 1, 0, -1};
static const int BX[3] = {10, 206, 402};
static const int BY[2] = {646, 716};
#define BW 186
#define BH 62

static const char *const ACT_NAME[ACT_N] = {
    "LEFT", "UP", "RIGHT", "DOWN", "PUSH", "QUIT"
};

static uint8_t scr[SCR_ROWS][SCR_COLS];
static uint8_t drawn[SCR_ROWS][SCR_COLS];
static uint8_t score[8], hi[8];
static uint8_t lives, speed, ticks[3];
static uint8_t timer_lo, timer_mid, timer_hi;
static uint8_t anim[6];
static uint8_t room_flags[RM_NUM_ROOMS + 1];
static uint8_t rand_acc;
static uint32_t rng, game_seed;
static int room, entry_pos, ply_r, ply_c, diamonds, gauge_row;
static int phase, dying, quit_armed, running, redraw_all, clean_next, buttons_dirty;
static int wfm_clean, wfm_motion, failures;
static int stats_on, probe, warned_slow;   /* the calibrated knobs, as tetris */
static volatile sig_atomic_t stop_req;

/* How long to leave the panel alone between repaints.
 *
 * This was a bare 150 in the loop. Measured on the device, one flush costs
 * ~276-312 ms whatever it paints, so a 150 ms throttle asked for a repaint
 * twice as often as the glass can deliver: the loop spent its life blocked in
 * ioctl and the game's pace became the panel's, not the game's. 320 is the
 * panel floor; --refresh can raise it but never lower it past that. */
#define PANEL_FLOOR_MS 320
static int refresh_ms = PANEL_FLOOR_MS;

/* How long one player step takes, in ms. A step is 14 world passes. The
 * panel paints in about 320 ms, so that is the fastest step that still gets
 * its own picture. 224 is the VIC-20's own step: quicker than the glass, so
 * two taps can share one picture. 450 was the old default (a step and a half). */
#define RM_TICK_CONTROL 14          /* rm_tick_limits[0]: passes per step */
static int move_ms = PANEL_FLOOR_MS;

/* Turn-based mode. Off: the cave runs on its own clock (the original game) and
 * a press joins in when the control tick comes round. On: the clock stops
 * entirely and nothing moves until a press arrives -- one press is one turn,
 * in which the player acts and then the rocks, the monsters and the cave's
 * timer each advance once. The timer advances by one step's worth of passes
 * per turn, so it still kills you, after about 480 turns instead of 6730
 * passes. move_ms and refresh_ms mean nothing in this mode: the panel is
 * repainted once per turn, as fast as it can. */
static int turn_based;

static void note_pace(void) {
    fprintf(stderr, "rockman: step %d ms\n", move_ms);
    if (move_ms < PANEL_FLOOR_MS)
        fprintf(stderr,
                "rockman: %d ms is faster than the panel (%d ms); "
                "two taps can share one picture\n",
                move_ms, PANEL_FLOOR_MS);
}

static void note_mode(void) {
    fprintf(stderr, "rockman: %s\n", turn_based
            ? "turn based: one press moves the player, the rocks and the monsters"
            : "clock based: the cave runs on its own");
}

/* Key buffer: one slot per press, drained one action per control tick. The
 * screen refresh outlives several taps, so a single pending slot would let a
 * second tap overwrite the first; this queue keeps them all, in order, and
 * the game's own tick decides when each press lands. Full drops the newest
 * press rather than reordering the queue. */
#define KEYQ_MAX 16
static int keyq[KEYQ_MAX];
static int keyq_head, keyq_n;

static void keyq_clear(void) { keyq_head = 0; keyq_n = 0; }
static int keyq_push(int a) {
    if (keyq_n >= KEYQ_MAX) return 0;
    keyq[(keyq_head + keyq_n) % KEYQ_MAX] = a;
    keyq_n++;
    return 1;
}
static int keyq_peek(void) { return keyq_n ? keyq[keyq_head] : -1; }
static void keyq_pop(void) {
    if (!keyq_n) return;
    keyq_head = (keyq_head + 1) % KEYQ_MAX;
    keyq_n--;
}

static int in_screen(int r, int c) {
    return r >= 0 && r < SCR_ROWS && c >= 0 && c < SCR_COLS;
}

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "rockman: FAIL %s\n", msg);
        failures++;
    }
}

static uint8_t rnd(void) {
    rng = rng * 1664525u + 1013904223u;
    rand_acc = (uint8_t)(rand_acc + (uint8_t)(rng >> 24));
    return (uint8_t)(rng >> 24);
}

static void reseed(void) {
    rng = game_seed ? game_seed : 1;
    rand_acc = 0;
}

/* 0 solid, 1 empty, 2 player, 3 monster. Same cuts as ClassifyTile. */
static int classify(uint8_t t) {
    if (t == RM_TILE_EMPTY) return 1;
    if (t >= RM_TILE_PLAYER_UD && t < RM_TILE_MONSTER_A) return 2;
    if (t >= RM_TILE_MONSTER_A && t < 0x27) return 3;
    return 0;
}

static uint8_t toggle_frame(uint8_t cur, int anim_i) {
    if (cur == rm_frame_a[anim_i]) return rm_frame_b[anim_i];
    return rm_frame_a[anim_i];
}

static void clear_digits(uint8_t *d) {
    for (int i = 0; i < 8; i++) d[i] = RM_TILE_0;
}

static void init_flags(void) {
    room_flags[0] = 0;
    for (int i = 1; i <= RM_NUM_ROOMS; i++) room_flags[i] = 1;
}

static void add_ones(int n) {
    for (int k = 0; k < n; k++) {
        for (int i = 7; i >= 0; i--) {
            score[i]++;
            if (score[i] < RM_TILE_0 + 10) break;
            score[i] = RM_TILE_0;
        }
    }
}

static void update_hi(void) {
    for (int i = 0; i < 8; i++) {
        if (score[i] == hi[i]) continue;
        if (score[i] > hi[i])
            for (; i < 8; i++) hi[i] = score[i];
        break;
    }
}

static void add10(void) { add_ones(10); }

static void add100(void) {
    for (int i = 0; i < 10; i++) add10();
    update_hi();
}

static void add10_hi(void) {
    add10();
    update_hi();
}

static void fill_brick(void) {
    for (int r = 0; r < SCR_ROWS; r++)
        for (int c = 0; c < SCR_COLS; c++) scr[r][c] = RM_TILE_BRICK;
}

static void paint_map(int rm) {
    if (rm < 0 || rm > RM_ROOM_END) {
        fprintf(stderr, "rockman: room %d is not a map\n", rm);
        exit(1);
    }
    const uint8_t *map = rm_room_maps[rm];
    for (int r = 0; r < 14; r++) {
        for (int byte = 0; byte < 2; byte++) {
            uint8_t b = map[r * 2 + byte];
            for (int bit = 0; bit < 8; bit++) {
                int c = 3 + byte * 8 + bit;
                scr[r][c] = (b & (uint8_t)(0x80 >> bit)) ? RM_TILE_ROCK : RM_TILE_DIRT;
            }
        }
    }
}

/* DrawRoom seals the top exit only when col 10 of row 0 was dug as dirt. */
static void seal_top(void) {
    if (scr[0][10] == RM_TILE_DIRT) {
        scr[0][10] = RM_TILE_BRICK;
        scr[0][11] = RM_TILE_BRICK;
    }
}

static void draw_hud(void) {
    scr[1][20] = lives;
    scr[3][20] = 0x0F;
    scr[3][21] = 0x10;
    for (int i = 0; i < 8; i++) {
        scr[5 + i][20] = score[i];
        scr[5 + i][21] = hi[i];
    }
    for (int i = 0; i < 10; i++) scr[3 + i][1] = (uint8_t)(RM_TILE_0 + i);
}

static void timer_reset(void) {
    timer_lo = 0;
    timer_mid = 0;
    timer_hi = 0xF7;
    gauge_row = -1;
}

static void show_picture(int rm) {
    room = rm;
    timer_reset();
    fill_brick();
    paint_map(rm);
    draw_hud();
    redraw_all = 1;
    clean_next = 1;
    buttons_dirty = 1;
}

static void show_title(void) {
    phase = PHASE_TITLE;
    show_picture(0);
}

static void win_game(void);

static void note_diamond(void) {
    diamonds++;
    if (diamonds < RM_DIAMONDS_PER_ROOM) return;
    if (room < 1 || room > RM_NUM_ROOMS) return;
    room_flags[room] = 0;
    for (int i = 1; i <= RM_NUM_ROOMS; i++)
        if (room_flags[i]) return;
    win_game();
}

static void win_game(void) {
    for (int i = 0; i < 100; i++) add100();
    speed++;
    init_flags();
    phase = PHASE_WIN;
    show_picture(RM_ROOM_END);
}

/* Dirt cells in rows 1-12, cols 4-17. The original walks that rectangle and
 * keeps going until one is free. Giving up is a real failure: say so. */
static void place_random(uint8_t tile) {
    for (int pass = 0; pass < 10000; pass++) {
        for (int r = 1; r <= 12; r++) {
            for (int c = 4; c <= 17; c++) {
                rnd();
                if (rand_acc >= 3) continue;
                if (r == ply_r && c == ply_c) continue;
                if (scr[r][c] != RM_TILE_DIRT) continue;
                scr[r][c] = tile;
                return;
            }
        }
    }
    fprintf(stderr, "rockman: no dirt left for tile 0x%02x in room %d\n", tile, room);
    exit(1);
}

static void player_dies(void);

static void start_room(void) {
    if (entry_pos < 0 || entry_pos > 4) {
        fprintf(stderr, "rockman: bad entry %d\n", entry_pos);
        exit(1);
    }
    if (room < 1 || room > RM_NUM_ROOMS) {
        fprintf(stderr, "rockman: cannot start room %d\n", room);
        exit(1);
    }
    ply_r = rm_entry_row[entry_pos];
    ply_c = rm_entry_col[entry_pos];
    fill_brick();
    paint_map(room);
    seal_top();
    draw_hud();
    timer_reset();
    diamonds = 0;
    keyq_clear();   /* presses queued for the old room mean nothing here */
    /* Spawn writes monster A. Reset the frame memory so the first
     * MoveMonsters looks for A and finds them. */
    anim[3] = anim[4] = anim[5] = 0;
    if (room_flags[room]) {
        for (int i = 0; i < 5; i++) {
            int r = rm_fixed_diamonds[room - 1][i][0];
            int c = rm_fixed_diamonds[room - 1][i][1];
            if (!in_screen(r, c)) {
                fprintf(stderr, "rockman: diamond %d of room %d is off the screen\n", i, room);
                exit(1);
            }
            scr[r][c] = RM_TILE_DIAMOND;
        }
        for (int i = 0; i < 3; i++) place_random(RM_TILE_DIAMOND);
    }
    int n = 2 + (speed & 7);
    for (int i = 0; i < n; i++) {
        place_random(RM_TILE_MONSTER_A);
        place_random(RM_TILE_TRAP);
    }
    if (scr[ply_r][ply_c] == RM_TILE_DIAMOND) {
        scr[ply_r][ply_c] = RM_TILE_EMPTY;
        add100();
        note_diamond();
    }
    if (phase == PHASE_PLAY) scr[ply_r][ply_c] = RM_TILE_PLAYER_UD;
    redraw_all = 1;
    clean_next = 1;
    buttons_dirty = 1;
}

static void begin_room1(void) {
    phase = PHASE_PLAY;
    room = 1;
    entry_pos = 0;
    ticks[0] = ticks[1] = ticks[2] = 0;
    quit_armed = 0;
    start_room();
}

static void new_game(void) {
    reseed();
    speed = 0;
    lives = (uint8_t)(RM_TILE_0 + 5);
    clear_digits(score);
    init_flags();
    begin_room1();
}

static void game_over(void) {
    speed = 0;
    lives = (uint8_t)(RM_TILE_0 + 5);
    init_flags();
    quit_armed = 0;
    keyq_clear();
    show_title();
}

static void player_dies(void) {
    if (phase != PHASE_PLAY || dying) return;
    dying = 1;
    if (lives <= RM_TILE_0) {
        dying = 0;
        game_over();
        return;
    }
    lives--;
    if (lives == RM_TILE_0) {
        dying = 0;
        game_over();
        return;
    }
    start_room();
    dying = 0;
}

static void collect_at(int r, int c) {
    scr[r][c] = RM_TILE_EMPTY;
    add100();
    note_diamond();
}

static void touch_trap(int r, int c) {
    if (diamonds < 4) {
        player_dies();
        return;
    }
    scr[r][c] = RM_TILE_EMPTY;
}

static void try_leave(int dir) {
    if (room < 1 || room > RM_NUM_ROOMS) return;
    if (room_flags[room]) return;
    const uint8_t *table = NULL;
    int arrive = 0;
    if (dir == DIR_DOWN) { table = rm_exit_down; arrive = 1; }
    else if (dir == DIR_LEFT) { table = rm_exit_left; arrive = 2; }
    else if (dir == DIR_UP) { table = rm_exit_up; arrive = 3; }
    else if (dir == DIR_RIGHT) { table = rm_exit_right; arrive = 4; }
    else return;
    uint8_t dest = table[room - 1];
    if (dest == 0) return;
    if (dest < 1 || dest > RM_NUM_ROOMS) {
        fprintf(stderr, "rockman: room %d direction %d leaves to %u\n", room, dir, dest);
        exit(1);
    }
    room = dest;
    entry_pos = arrive;
    start_room();
}

static void try_move(int dir) {
    int nr = ply_r + MDR[dir];
    int nc = ply_c + MDC[dir];
    if (!in_screen(nr, nc)) return;
    uint8_t t = scr[nr][nc];
    int anim_i = (dir == DIR_LEFT) ? 0 : (dir == DIR_RIGHT) ? 1 : 2;
    if (t == RM_TILE_EMPTY || t == RM_TILE_DIRT) {
        uint8_t frame = toggle_frame(scr[ply_r][ply_c], anim_i);
        scr[ply_r][ply_c] = RM_TILE_EMPTY;
        ply_r = nr;
        ply_c = nc;
        scr[ply_r][ply_c] = frame;
        return;
    }
    if (t == RM_TILE_DIAMOND) { collect_at(nr, nc); return; }
    if (t == RM_TILE_TRAP) { touch_trap(nr, nc); return; }
    if (t == RM_TILE_BRICK) { try_leave(dir); return; }
    if (t >= RM_TILE_MONSTER_A && t <= 0x26) player_dies();
}

static int push_side(int dc) {
    int rc = ply_c + dc;
    int bc = ply_c + 2 * dc;
    if (!in_screen(ply_r, rc) || !in_screen(ply_r, bc)) return 0;
    if (scr[ply_r][rc] != RM_TILE_ROCK) return 0;
    if (scr[ply_r][bc] != RM_TILE_EMPTY) return 0;
    scr[ply_r][bc] = RM_TILE_FALL1;
    scr[ply_r][rc] = RM_TILE_EMPTY;
    return 1;
}

static void push_rock(void) {
    uint8_t t = scr[ply_r][ply_c];
    if (t == RM_TILE_PLAYER_R || t == (uint8_t)(RM_TILE_PLAYER_R + 1)) push_side(1);
    else if (t == RM_TILE_PLAYER_L || t == (uint8_t)(RM_TILE_PLAYER_L + 1)) push_side(-1);
    else {
        push_side(-1);
        push_side(1);
    }
}

static void rock_roll(int r, int c) {
    for (int side = 0; side < 2; side++) {
        int dc = side == 0 ? -1 : 1;
        int sc = c + dc;
        if (!in_screen(r, sc) || !in_screen(r + 1, sc)) continue;
        if (scr[r][sc] != RM_TILE_EMPTY) continue;
        if (classify(scr[r + 1][sc]) == 0) continue;
        scr[r][c] = RM_TILE_EMPTY;
        scr[r][sc] = RM_TILE_ROCK;
    }
}

static void explode_at(int r, int c, int from_tile) {
    int idx = from_tile - (RM_TILE_EXPLODE1 - 1);
    if (idx < 0 || idx > 4) {
        fprintf(stderr, "rockman: bad explosion step %d\n", from_tile);
        exit(1);
    }
    scr[r][c] = rm_explode_tiles[idx];
}

static void scan_rocks(void) {
    for (int r = 12; r >= 1; r--) {
        for (int c = 17; c >= 4; c--) {
            uint8_t t = scr[r][c];
            if (t == RM_TILE_ROCK) {
                uint8_t below = scr[r + 1][c];
                if (below == RM_TILE_DIRT) continue;
                if (classify(below) != 0) scr[r][c] = RM_TILE_FALL1;
                else rock_roll(r, c);
                continue;
            }
            if (t < RM_TILE_ROCK) continue;
            if (t == RM_TILE_FALL3) {
                scr[r][c] = RM_TILE_EMPTY;
                int k = classify(scr[r + 1][c]);
                if (k == 0) scr[r][c] = RM_TILE_ROCK;
                else if (k == 1) scr[r + 1][c] = RM_TILE_FALL3;
                else {
                    if (k == 3) add10_hi();
                    explode_at(r + 1, c, RM_TILE_EXPLODE1 - 1);
                }
                continue;
            }
            if (t < RM_TILE_FALL3) {
                scr[r][c] = (uint8_t)(t + 1);
                continue;
            }
            if (t == RM_TILE_DIAMOND) continue;
            if (t >= RM_TILE_EXPLODE1 && t < RM_TILE_EXPLODE1 + 4)
                explode_at(r, c, t);
        }
    }
}

/* Monsters step onto empty, onto player frames 0x1B-0x1F (not the second
 * right-facing frame 0x20: the original compare stops before it), and rarely
 * onto dirt. Anything else blocks. Four failures and it stays. */
static void move_monsters(void) {
    for (int i = 3; i <= 5; i++) anim[i] = toggle_frame(anim[i], i);
    for (int r = 0; r <= 13; r++) {
        int c0 = (r == 0) ? 1 : 0;
        for (int c = c0; c <= 21; c++) {
            uint8_t t = scr[r][c];
            if (t != anim[3] && t != anim[4] && t != anim[5]) continue;
            scr[r][c] = RM_TILE_EMPTY;
            int moved = 0;
            uint8_t dirv = rnd();
            for (int tries = 0; tries < 4; tries++) {
                int d = dirv & 3;
                int nr = r + MDR[d];
                int nc = c + MDC[d];
                int ok = 0;
                if (in_screen(nr, nc)) {
                    uint8_t n = scr[nr][nc];
                    if (n == RM_TILE_EMPTY) ok = 1;
                    else if (n >= RM_TILE_PLAYER_UD && n <= RM_TILE_PLAYER_R) ok = 1;
                    else if (n == RM_TILE_DIRT && rnd() < 5) ok = 1;
                }
                if (ok) {
                    int af = rm_mm_dir_frame[d];
                    scr[nr][nc] = toggle_frame(anim[af], af);
                    moved = 1;
                    break;
                }
                dirv = rnd();
            }
            if (!moved) {
                int d = rnd() & 3;
                int af = rm_mm_dir_frame[d];
                scr[r][c] = toggle_frame(anim[af], af);
            }
        }
    }
}

static void check_alive(void) {
    if (phase != PHASE_PLAY) return;
    uint8_t t = scr[ply_r][ply_c];
    if (t < RM_TILE_PLAYER_UD || t >= RM_TILE_MONSTER_A) player_dies();
}

static void timer_tick(void) {
    if (phase != PHASE_PLAY) return;
    timer_lo++;
    if (timer_lo != 0) return;
    timer_lo = 0xFA;
    timer_mid++;
    if (timer_mid == 120) {
        timer_mid = 0;
        for (int i = 0; i < 10; i++) scr[3 + i][1] = (uint8_t)(RM_TILE_0 + i);
        timer_hi++;
        if (timer_hi == 0) {
            gauge_row = -1;
            player_dies();
            return;
        }
    }
    gauge_row = 3 + (256 - (int)timer_hi);
}

static void do_action(int a) {
    if (a != ACT_QUIT) quit_armed = 0;
    switch (a) {
    case ACT_LEFT: try_move(DIR_LEFT); break;
    case ACT_UP: try_move(DIR_UP); break;
    case ACT_RIGHT: try_move(DIR_RIGHT); break;
    case ACT_DOWN: try_move(DIR_DOWN); break;
    case ACT_PUSH: push_rock(); break;
    case ACT_QUIT:
        if (quit_armed) running = 0;
        else quit_armed = 1;
        buttons_dirty = 1;
        break;
    default:
        fprintf(stderr, "rockman: bad action %d\n", a);
        exit(1);
    }
}

static int tick(int which) {
    ticks[which]++;
    if (ticks[which] < rm_tick_limits[which]) return 0;
    ticks[which] = 0;
    return 1;
}

/* One main-loop pass. Returns 1 when the control slot fired. */
static int one_pass(int action, int apply) {
    if (phase != PHASE_PLAY) return 0;
    timer_tick();
    if (phase != PHASE_PLAY) return 0;
    if (tick(2)) scan_rocks();
    if (phase != PHASE_PLAY) return 0;
    if (tick(1)) move_monsters();
    if (phase != PHASE_PLAY) return 0;
    check_alive();
    if (phase != PHASE_PLAY) return 0;
    int fired = tick(0);
    if (fired && apply) do_action(action);
    return fired;
}

/* One turn, for turn-based mode: the player acts, then the cave answers
 * exactly once -- one rock scan, one monster step, and one step's worth of the
 * death timer. Nothing runs between presses, so the whole cave waits for the
 * finger.
 *
 * On the clock the rocks scan every 9 passes, the monsters step every 16 and a
 * player step is 14, so the ratios here are not the clock's. This is the turn
 * version of the rule instead: every turn, the player moves once and the cave
 * answers once. The timer gets RM_TICK_CONTROL (14) passes per turn, which is
 * what one player step was worth, so a cave still runs out of time after about
 * the same number of moves (6730 / 14 = 481 turns) rather than after the same
 * number of seconds. */
static void take_turn(int action) {
    if (phase != PHASE_PLAY) return;
    if (action == ACT_QUIT) {
        do_action(action);
        return;
    }
    if (action >= 0) do_action(action);
    if (phase != PHASE_PLAY) return;
    check_alive();
    if (phase != PHASE_PLAY) return;
    scan_rocks();
    if (phase != PHASE_PLAY) return;
    move_monsters();
    if (phase != PHASE_PLAY) return;
    check_alive();
    if (phase != PHASE_PLAY) return;
    for (int i = 0; i < RM_TICK_CONTROL; i++) {
        timer_tick();
        if (phase != PHASE_PLAY) return;
    }
}

static void play_action(int action) {
    if (phase != PHASE_PLAY) return;
    if (action == ACT_QUIT) {
        do_action(action);
        return;
    }
    if (turn_based) {
        take_turn(action);
        return;
    }
    for (int n = 0; n < 64; n++) {
        if (phase != PHASE_PLAY) return;
        if (one_pass(action, 1)) return;
    }
    fprintf(stderr, "rockman: the control tick never arrived\n");
    exit(1);
}

/* Milliseconds between world passes. One player step is RM_TICK_CONTROL
 * passes, so at speed 0 this is move_ms / 14. */
static uint32_t pass_interval_ms(void) {
    int lim = (int)rm_tick_limits[0];
    if (lim <= 0) {
        fprintf(stderr, "rockman: control tick limit is %d\n", lim);
        exit(1);
    }
    int sp = (int)speed;
    if (sp < 0) sp = 0;
    if (sp > 255) sp = 255;
    uint32_t iv = (uint32_t)((int64_t)move_ms * (256 - sp) / (lim * 256));
    if (iv == 0) iv = 1;
    return iv;
}

/* Run every world pass the clock says is due. The caller paints once.
 *
 * present() blocks for the panel's own ~300 ms. The old loop did one pass
 * and then painted, so the 14 passes of a step took 14 frames — about 4
 * seconds a move, whatever --move said. Catching up here makes move_ms the
 * real gap between steps. A stall longer than two steps is dropped: the
 * death timer must not jump through a hitch. The menu holds the clock.
 * Returns how many passes ran. */
static int pump_passes(uint32_t now, uint32_t *next_pass) {
    if (kframe_covering()) {
        *next_pass = now;
        return 0;
    }
    if (phase != PHASE_PLAY) return 0;
    if (turn_based) {
        /* The cave does not run on its own: the press already took its turn.
         * Keep the anchor at now so switching back does not fire a burst. */
        *next_pass = now;
        return 0;
    }
    uint32_t iv = pass_interval_ms();
    int lim = (int)rm_tick_limits[0];
    int burst = 0;
    int burst_max = lim * 2;
    while ((int32_t)(now - *next_pass) >= 0 && burst < burst_max) {
        int act = keyq_peek();
        if (one_pass(act, act >= 0) && act >= 0) keyq_pop();
        *next_pass += iv;
        burst++;
    }
    if ((int32_t)(now - *next_pass) >= 0)
        *next_pass = now + iv;
    return burst;
}

static int dir_to_act(int d) {
    if (d == DIR_UP) return ACT_UP;
    if (d == DIR_DOWN) return ACT_DOWN;
    if (d == DIR_LEFT) return ACT_LEFT;
    return ACT_RIGHT;
}

static int walkable(int r, int c) {
    if (!in_screen(r, c)) return 0;
    uint8_t t = scr[r][c];
    return t == RM_TILE_EMPTY || t == RM_TILE_DIRT || t == RM_TILE_DIAMOND;
}

static int bot_action(void) {
    int br = -1, bc = -1, best = 1000;
    int order[4], n = 0;
    for (int r = 0; r < SCR_ROWS; r++)
        for (int c = 0; c < SCR_COLS; c++)
            if (scr[r][c] == RM_TILE_DIAMOND) {
                int d = abs(r - ply_r) + abs(c - ply_c);
                if (d < best) { best = d; br = r; bc = c; }
            }
    if (br >= 0) {
        int dr = br - ply_r, dc = bc - ply_c;
        if (abs(dr) >= abs(dc)) {
            if (dr < 0) order[n++] = DIR_UP;
            if (dr > 0) order[n++] = DIR_DOWN;
            if (dc < 0) order[n++] = DIR_LEFT;
            if (dc > 0) order[n++] = DIR_RIGHT;
        } else {
            if (dc < 0) order[n++] = DIR_LEFT;
            if (dc > 0) order[n++] = DIR_RIGHT;
            if (dr < 0) order[n++] = DIR_UP;
            if (dr > 0) order[n++] = DIR_DOWN;
        }
    }
    for (int d = 0; d < 4; d++) {
        int seen = 0;
        for (int i = 0; i < n; i++) if (order[i] == d) seen = 1;
        if (!seen) order[n++] = d;
    }
    for (int i = 0; i < n; i++) {
        int d = order[i];
        if (walkable(ply_r + MDR[d], ply_c + MDC[d])) return dir_to_act(d);
    }
    return ACT_PUSH;
}

/* ---- drawing ------------------------------------------------------------ */

static int btn_x(int i) { return BX[i % 3]; }
static int btn_y(int i) { return BY[i / 3]; }

static void draw_button(int i) {
    int x = btn_x(i), y = btn_y(i);
    const char *s = (i == ACT_QUIT && quit_armed) ? "SURE" : ACT_NAME[i];
    int tw = (int)strlen(s) * 16;
    ek_fill_rect(x, y, BW, BH, EK_PAPER);
    ek_rect(x, y, BW, BH, EK_INK);
    ek_rect(x + 1, y + 1, BW - 2, BH - 2, EK_INK);
    ek_text_scale(x + (BW - tw) / 2, y + (BH - 32) / 2, s, EK_INK, 2);
}

static void paint_cell(int r, int c) {
    int x0 = ORIGIN_X + c * CELL_W;
    int y0 = ORIGIN_Y + r * CELL_H;
    int invert = (r == gauge_row && c == 1);
    uint8_t t = scr[r][c];
    if (t >= 0x2D) {
        fprintf(stderr, "rockman: tile 0x%02x at %d,%d is not in the set\n", t, c, r);
        exit(1);
    }
    const uint8_t *glyph = rm_tiles[t];
    int blank = 1;
    for (int i = 0; i < 16; i++) if (glyph[i]) { blank = 0; break; }
    if (blank) {
        ek_fill_rect(x0, y0, CELL_W, CELL_H, invert ? EK_INK : EK_PAPER);
        return;
    }
    for (int y = 0; y < CELL_H; y++) {
        int sy = y * 16 / CELL_H;
        uint8_t bits = glyph[sy];
        int run_ink = -1, run_x = 0;
        for (int x = 0; x < CELL_W; x++) {
            int sx = x * 8 / CELL_W;
            int on = (bits & (uint8_t)(0x80 >> sx)) != 0;
            int ink = on ? EK_INK : EK_PAPER;
            if (invert) ink = (ink == EK_INK) ? EK_PAPER : EK_INK;
            if (ink != run_ink) {
                if (run_ink >= 0) ek_hline(x0 + run_x, y0 + y, x - run_x, run_ink);
                run_ink = ink;
                run_x = x;
            }
        }
        if (run_ink >= 0) ek_hline(x0 + run_x, y0 + y, CELL_W - run_x, run_ink);
    }
}

static uint8_t want_cell(int r, int c) {
    uint8_t t = scr[r][c];
    if (r == gauge_row && c == 1) t |= 0x80;
    return t;
}

static void present(void) {
    if (kframe_covering()) {
        kframe_paint();
        ek_flush();
        return;
    }
    int clean = clean_next || redraw_all;
    if (redraw_all) {
        ek_clear(EK_PAPER);
        for (int r = 0; r < SCR_ROWS; r++)
            for (int c = 0; c < SCR_COLS; c++) {
                paint_cell(r, c);
                drawn[r][c] = want_cell(r, c);
            }
        for (int i = 0; i < ACT_N; i++) draw_button(i);
        redraw_all = 0;
        buttons_dirty = 0;
    } else {
        for (int r = 0; r < SCR_ROWS; r++)
            for (int c = 0; c < SCR_COLS; c++) {
                uint8_t w = want_cell(r, c);
                if (w == drawn[r][c]) continue;
                paint_cell(r, c);
                drawn[r][c] = w;
            }
        if (buttons_dirty) {
            for (int i = 0; i < ACT_N; i++) draw_button(i);
            buttons_dirty = 0;
        }
    }
    clean_next = 0;
    ek_waveform(clean ? wfm_clean : wfm_motion);
    kframe_paint();
    ek_stats st = ek_flush();
    ek_waveform(0);

    /* Same honest floor as tetris: if the panel's own refresh outlasts the
     * gap we are leaving, the repaint throttle is a lie. Say so once.
     *
     * Two deliberate costs are exempt, or this cries wolf on frame two: a full
     * repaint (~670 ms, on a room change and the title) and a clean frame
     * (~700 ms, on the engine's own every-30-flush GC16). Both are one-off
     * swallows, not the steady per-frame cost this warning is about. */
    if (!st.full && !clean && !warned_slow && (int)st.us > refresh_ms + 20) {
        warned_slow = 1;
        fprintf(stderr,
                "rockman: the panel needs %u ms per update but the refresh gap is "
                "%d ms; the screen updates as fast as the panel can, not every %d ms. "
                "Fastest honest gap is about %u ms.\n",
                st.us, refresh_ms, refresh_ms, st.us + 10u);
    }
    if (stats_on || probe)
        fprintf(stderr, "rockman: flush wfm=%d rects=%d tiles=%d px=%ld full=%d folded=%d us=%u\n",
                st.wfm, st.rects, st.tiles, st.px, st.full, st.folded, st.us);
}

static int hit(int x, int y) {
    for (int i = 0; i < ACT_N; i++) {
        int bx = btn_x(i), by = btn_y(i);
        if (x >= bx && x < bx + BW && y >= by && y < by + BH) return i;
    }
    return -1;
}

#ifdef EK_HOST
/* Pipe-driven taps ("tap X Y" on stdin) exist only in the host build; the
   Kindle build drives the same transitions from its press/release loop. */
static void tap_at(int x, int y) {
    /* The hamburger, and the shell it opens, come first. Closing the shell
     * leaves its page on the glass, so the next present() repaints the cave. */
    if (kframe_tap(x, y)) {
        if (!kframe_covering()) redraw_all = 1;
        return;
    }
    if (kframe_covering()) return;
    int b = hit(x, y);
    if (phase != PHASE_PLAY) {
        if (b == ACT_QUIT) {
            do_action(ACT_QUIT);
            return;
        }
        if (quit_armed) { quit_armed = 0; buttons_dirty = 1; }
        if (phase == PHASE_WIN) begin_room1();
        else new_game();
        return;
    }
    if (b < 0) {
        if (quit_armed) { quit_armed = 0; buttons_dirty = 1; }
        return;
    }
    if (b == ACT_QUIT) do_action(ACT_QUIT);
    else play_action(b);
}
#endif

/* ---- self-test ---------------------------------------------------------- */

static int count_tile(uint8_t t) {
    int n = 0;
    for (int r = 0; r < SCR_ROWS; r++)
        for (int c = 0; c < SCR_COLS; c++)
            if (scr[r][c] == t) n++;
    return n;
}

static int selftest(void) {
    failures = 0;
    keyq_clear();
    expect(keyq_peek() == -1, "key buffer starts empty");
    expect(keyq_push(ACT_LEFT) && keyq_push(ACT_UP) && keyq_push(ACT_RIGHT),
           "key buffer takes three presses");
    expect(keyq_peek() == ACT_LEFT, "key buffer serves the first press first");
    keyq_pop();
    expect(keyq_peek() == ACT_UP, "key buffer pops in order");
    keyq_clear();
    for (int i = 0; i < KEYQ_MAX; i++) expect(keyq_push(i), "key buffer fills");
    expect(!keyq_push(99), "a full key buffer drops the newest press");
    expect(keyq_peek() == 0, "a full key buffer still holds the first press");
    keyq_clear();
    expect(keyq_peek() == -1, "key buffer clears");
    expect(rm_tune[0] == 0xD9, "tune table loaded");
    expect(rm_rock_sounds[1] == 0xD2, "rock pitches loaded");
    expect(rm_explode_sounds[0] == 0xC8, "explosion noise loaded");
    expect(rm_mm_dir_offset[0] == 22, "monster up offset is one row");
    expect(rm_tick_limits[0] == 14 && rm_tick_limits[1] == 16 && rm_tick_limits[2] == 9,
           "tick limits are 14, 16 and 9");

    /* Map bits: set = rock, clear = dirt. Room 1's top row starts 0xFE 0x7F. */
    fill_brick();
    paint_map(1);
    expect(scr[0][3] == RM_TILE_ROCK && scr[0][9] == RM_TILE_ROCK, "room 1 top rocks");
    expect(scr[0][10] == RM_TILE_DIRT && scr[0][11] == RM_TILE_DIRT, "room 1 top exit is dirt");
    expect(scr[0][12] == RM_TILE_ROCK, "the bit after the exit is a rock");
    for (int r = 0; r < 14; r++)
        for (int c = 3; c <= 18; c++)
            expect(scr[r][c] == RM_TILE_ROCK || scr[r][c] == RM_TILE_DIRT,
                   "a fresh map is only rock and dirt");
    seal_top();
    expect(scr[0][10] == RM_TILE_BRICK && scr[0][11] == RM_TILE_BRICK, "DrawRoom seals the top exit");

    /* Every non-zero exit points at a real room, and every entry cell is inside the map. */
    for (int rm = 1; rm <= RM_NUM_ROOMS; rm++) {
        const uint8_t *tables[4] = {rm_exit_down, rm_exit_left, rm_exit_up, rm_exit_right};
        for (int d = 0; d < 4; d++) {
            uint8_t dest = tables[d][rm - 1];
            expect(dest == 0 || (dest >= 1 && dest <= RM_NUM_ROOMS), "exit stays inside rooms 1-20");
        }
        for (int e = 0; e < 5; e++) {
            int r = rm_entry_row[e], c = rm_entry_col[e];
            expect(r >= 0 && r <= 13 && c >= 3 && c <= 18, "entry cell is inside the map");
        }
    }

    /* Eight diamonds in every room from the middle door, plus the two monsters
     * and two traps speed 0 spawns. Room 5's top door sits on a fixed diamond. */
    lives = (uint8_t)(RM_TILE_0 + 5);
    clear_digits(score);
    clear_digits(hi);
    speed = 0;
    phase = PHASE_PLAY;
    init_flags();
    for (int rm = 1; rm <= RM_NUM_ROOMS; rm++) {
        reseed();
        room = rm;
        entry_pos = 0;
        start_room();
        int on_map = count_tile(RM_TILE_DIAMOND);
        expect(on_map + diamonds == RM_DIAMONDS_PER_ROOM, "room holds 8 diamonds");
        expect(count_tile(RM_TILE_MONSTER_A) == 2, "speed 0 spawns two monsters");
        expect(count_tile(RM_TILE_TRAP) == 2, "speed 0 spawns two traps");
        expect(phase == PHASE_PLAY, "starting a room is not a win");
    }
    reseed();
    room = 5;
    entry_pos = 1;
    clear_digits(score);
    clear_digits(hi);
    start_room();
    expect(diamonds == 1, "room 5's top door collects the diamond it covers");
    expect(score[5] == RM_TILE_0 + 1, "that diamond scores 100");

    /* Score carry, and the hi-score copies a higher score. */
    clear_digits(score);
    clear_digits(hi);
    add100();
    expect(score[5] == RM_TILE_0 + 1 && hi[5] == RM_TILE_0 + 1, "+100 shows in the hundreds");
    score[6] = (uint8_t)(RM_TILE_0 + 9);
    add10_hi();
    expect(score[5] == RM_TILE_0 + 2 && score[6] == RM_TILE_0, "+10 carries out of a 9");
    expect(hi[5] == RM_TILE_0 + 2, "hi-score follows a carry");

    /* Dig, push, and a sealed exit. */
    fill_brick();
    paint_map(1);
    seal_top();
    phase = PHASE_PLAY;
    room = 1;
    room_flags[1] = 1;
    diamonds = 0;
    ply_r = 7;
    ply_c = 10;
    scr[7][10] = RM_TILE_PLAYER_UD;
    scr[7][11] = RM_TILE_DIRT;
    try_move(DIR_RIGHT);
    expect(ply_c == 11 && scr[7][10] == RM_TILE_EMPTY, "digging steps into the dirt");
    expect(scr[7][11] == RM_TILE_PLAYER_L || scr[7][11] == RM_TILE_PLAYER_R ||
               scr[7][11] == RM_TILE_PLAYER_UD || scr[7][11] == (uint8_t)(RM_TILE_PLAYER_UD + 1),
           "the player is drawn in the new cell");

    ply_r = 5;
    ply_c = 10;
    scr[5][10] = RM_TILE_PLAYER_L;
    scr[5][9] = RM_TILE_ROCK;
    scr[5][8] = RM_TILE_EMPTY;
    push_rock();
    expect(scr[5][8] == RM_TILE_FALL1 && scr[5][9] == RM_TILE_EMPTY, "a push shoves the rock and starts it falling");
    expect(scr[5][10] == RM_TILE_PLAYER_L, "pushing does not move the player");

    ply_r = 1;
    ply_c = 10;
    scr[1][10] = RM_TILE_PLAYER_UD;
    room_flags[1] = 1;
    try_move(DIR_UP);
    expect(room == 1 && ply_r == 1, "a room with diamonds still in it has no exit");
    room_flags[1] = 0;
    diamonds = 8;
    ply_r = 7;
    ply_c = 3;
    scr[7][3] = RM_TILE_PLAYER_UD;
    try_move(DIR_LEFT);
    expect(room == 1 && ply_c == 3, "exit 0 does not leave the room");
    ply_r = 1;
    ply_c = 10;
    scr[1][10] = RM_TILE_PLAYER_UD;
    scr[0][10] = RM_TILE_BRICK;
    try_move(DIR_UP);
    expect(room == 16 && ply_r == 13 && ply_c == 11, "the cleared top exit of room 1 enters room 16 at the bottom");

    /* Rocks: dirt holds, empty falls through the stages, both free sides duplicate. */
    fill_brick();
    scr[5][10] = RM_TILE_ROCK;
    scr[6][10] = RM_TILE_DIRT;
    scan_rocks();
    expect(scr[5][10] == RM_TILE_ROCK, "dirt holds a rock up");

    fill_brick();
    scr[5][10] = RM_TILE_ROCK;
    scr[6][10] = RM_TILE_EMPTY;
    scan_rocks();
    expect(scr[5][10] == RM_TILE_FALL1, "a rock over empty space starts falling");
    scan_rocks();
    expect(scr[5][10] == (uint8_t)(RM_TILE_FALL1 + 1), "the next scan is stage 2");
    scan_rocks();
    expect(scr[5][10] == RM_TILE_FALL3, "the next scan is stage 3");
    scan_rocks();
    expect(scr[5][10] == RM_TILE_EMPTY && scr[6][10] == RM_TILE_FALL3, "stage 3 drops into the cell below");

    fill_brick();
    scr[5][8] = RM_TILE_ROCK;
    scr[6][8] = RM_TILE_BRICK;
    scr[5][7] = RM_TILE_EMPTY;
    scr[6][7] = RM_TILE_EMPTY;
    scr[5][9] = RM_TILE_EMPTY;
    scr[6][9] = RM_TILE_EMPTY;
    scan_rocks();
    /* The scan walks right to left, so the copy on the left is seen again in
     * this same pass and, with nothing under it, starts to fall. The copy on
     * the right was already passed and stays a resting rock. */
    expect(scr[5][8] == RM_TILE_EMPTY && scr[5][9] == RM_TILE_ROCK &&
               scr[5][7] == RM_TILE_FALL1,
           "a rock with both sides free is copied to both, and the left copy starts falling");

    /* A falling rock lands on the player and that costs a life. */
    lives = (uint8_t)(RM_TILE_0 + 5);
    phase = PHASE_PLAY;
    room = 1;
    entry_pos = 0;
    room_flags[1] = 1;
    speed = 0;
    reseed();
    fill_brick();
    scr[5][10] = RM_TILE_FALL3;
    scr[6][10] = RM_TILE_PLAYER_UD;
    ply_r = 6;
    ply_c = 10;
    scan_rocks();
    expect(scr[6][10] == RM_TILE_EXPLODE1, "a rock landing on the player explodes");
    check_alive();
    expect(lives == RM_TILE_0 + 4 && phase == PHASE_PLAY, "the explosion costs one life and restarts the room");

    /* Trap: under 4 diamonds it kills, at 4 it just goes away. */
    lives = (uint8_t)(RM_TILE_0 + 4);
    phase = PHASE_PLAY;
    diamonds = 4;
    ply_r = 5;
    ply_c = 10;
    scr[5][10] = RM_TILE_PLAYER_UD;
    scr[5][11] = RM_TILE_TRAP;
    try_move(DIR_RIGHT);
    expect(ply_c == 10 && scr[5][11] == RM_TILE_EMPTY && lives == RM_TILE_0 + 4,
           "four diamonds destroy a trap");
    diamonds = 3;
    scr[5][11] = RM_TILE_TRAP;
    int lives_before = lives;
    try_move(DIR_RIGHT);
    expect(lives == lives_before - 1, "three diamonds and a trap costs a life");

    /* A boxed-in monster stays. A monster with one open cell can step into it. */
    fill_brick();
    anim[3] = anim[4] = anim[5] = 0;
    scr[5][10] = RM_TILE_MONSTER_A;
    move_monsters();
    expect(scr[5][10] >= RM_TILE_MONSTER_A && scr[5][10] <= 0x26, "a walled-in monster stays put");
    int moved = 0;
    for (uint32_t s = 1; s < 400 && !moved; s++) {
        fill_brick();
        scr[5][10] = RM_TILE_MONSTER_A;
        scr[5][11] = RM_TILE_EMPTY;
        anim[3] = anim[4] = anim[5] = 0;
        rng = s;
        rand_acc = 0;
        move_monsters();
        if (scr[5][11] >= RM_TILE_MONSTER_A && scr[5][11] <= 0x26) moved = 1;
    }
    expect(moved, "a monster steps into an empty neighbour");

    /* The gauge climbs 9 marks and then the player dies. */
    phase = PHASE_PLAY;
    room = 1;
    entry_pos = 0;
    room_flags[1] = 1;
    speed = 0;
    lives = (uint8_t)(RM_TILE_0 + 5);
    reseed();
    timer_reset();
    ply_r = 7;
    ply_c = 11;
    int guard = 0;
    while (lives == RM_TILE_0 + 5 && guard < 20000) {
        timer_tick();
        guard++;
    }
    expect(lives == RM_TILE_0 + 4, "the timer costs a life");
    {
        char buf[64];
        snprintf(buf, sizeof buf, "timer death on pass %d (want 6730)", guard);
        expect(guard == 6730, buf);
    }

    /* Last life ends the game and keeps the hi-score. */
    lives = (uint8_t)(RM_TILE_0 + 1);
    hi[5] = (uint8_t)(RM_TILE_0 + 4);
    phase = PHASE_PLAY;
    player_dies();
    expect(phase == PHASE_TITLE, "no lives left returns to the title");
    expect(hi[5] == RM_TILE_0 + 4, "game over keeps the hi-score");

    /* One pass is not a step. One panel frame is. A long stall must not
     * spend a life: the cap drops the leftover time. */
    phase = PHASE_PLAY;
    room = 1;
    dying = 0;
    speed = 0;
    lives = (uint8_t)(RM_TILE_0 + 5);
    ticks[0] = ticks[1] = ticks[2] = 0;
    timer_reset();
    quit_armed = 0;
    fill_brick();
    ply_r = 7;
    ply_c = 11;
    scr[7][11] = RM_TILE_PLAYER_UD;
    scr[7][10] = RM_TILE_EMPTY;
    keyq_clear();
    expect(keyq_push(ACT_LEFT), "the pace test can queue a step");
    move_ms = PANEL_FLOOR_MS;
    uint32_t np = 0;
    int npass = pump_passes(pass_interval_ms(), &np);
    expect(npass > 0 && npass < RM_TICK_CONTROL, "one tick interval is not a player step");
    expect(ply_c == 11, "one tick interval does not move the player");

    ticks[0] = ticks[1] = ticks[2] = 0;
    keyq_clear();
    expect(keyq_push(ACT_LEFT), "the frame test can queue a step");
    np = 0;
    npass = pump_passes(PANEL_FLOOR_MS, &np);
    expect(npass >= RM_TICK_CONTROL, "one frame runs a full player step");
    expect(ply_c == 10, "one frame of clock moves the player");

    {
        int lives_before = (int)lives;
        ticks[0] = ticks[1] = ticks[2] = 0;
        timer_reset();
        keyq_clear();
        np = 0;
        pump_passes(60000, &np);
        expect((int)lives == lives_before, "a stalled clock does not spend a life");
        expect((int32_t)(np - 60000) > 0, "a stall past the cap drops the leftover time");
    }

    /* Turn based: the clock is stopped, so a frame moves nothing, and one
     * press takes one whole turn -- the player, then the cave. */
    fill_brick();
    phase = PHASE_PLAY;
    room = 1;
    dying = 0;
    speed = 0;
    lives = (uint8_t)(RM_TILE_0 + 5);
    ticks[0] = ticks[1] = ticks[2] = 0;
    timer_reset();
    ply_r = 7;
    ply_c = 11;
    scr[7][11] = RM_TILE_PLAYER_UD;
    scr[7][10] = RM_TILE_EMPTY;
    keyq_clear();
    turn_based = 1;
    np = 0;
    expect(pump_passes(600000, &np) == 0, "turn based: the clock runs no passes");
    expect(ply_c == 11, "turn based: time alone moves nobody");

    take_turn(ACT_LEFT);
    expect(ply_c == 10, "turn based: one press is one step");

    /* The timer still kills, in turns rather than in seconds: one turn is
     * worth RM_TICK_CONTROL passes, so 6730 passes is 481 turns. */
    fill_brick();
    phase = PHASE_PLAY;
    room = 1;
    entry_pos = 0;
    room_flags[1] = 1;
    dying = 0;
    speed = 0;
    lives = (uint8_t)(RM_TILE_0 + 5);
    reseed();
    timer_reset();
    ply_r = 7;
    ply_c = 11;
    scr[7][11] = RM_TILE_PLAYER_UD;
    int turns = 0;
    while (lives == RM_TILE_0 + 5 && turns < 5000) {
        take_turn(-1);
        turns++;
    }
    expect(lives == RM_TILE_0 + 4, "turn based: the timer still costs a life");
    {
        char buf[64];
        snprintf(buf, sizeof buf, "timer death on turn %d (want 481)", turns);
        expect(turns == 481, buf);
    }
    turn_based = 0;

    if (failures == 0) fprintf(stderr, "rockman: selftest ok\n");
    else fprintf(stderr, "rockman: %d selftest failure%s\n", failures, failures == 1 ? "" : "s");
    return failures;
}

/* ---- the shared menu ---------------------------------------------------- */

enum { ROCK_NEW = 1, ROCK_SPEED, ROCK_MODE };

/* Same beats --move takes. Fast is one picture per step. Old is the previous
 * default. VIC is the 1984 step, which is quicker than this panel paints. */
static const struct { const char *label; int ms; } PACES[] = {
    { "FAST  320 ms", PANEL_FLOOR_MS },
    { "OLD   450 ms", 450 },
    { "VIC   224 ms", 224 },
};
#define N_PACES ((int)(sizeof PACES / sizeof PACES[0]))
#define PACE_Y0 140

static int pace_choice(void) {
    for (int i = 0; i < N_PACES; i++)
        if (PACES[i].ms == move_ms) return i;
    return -1;
}

static void draw_new(const kshell_rect *r) {
    char line[64];
    int nlife = (int)lives - RM_TILE_0;
    if (nlife < 0) nlife = 0;
    snprintf(line, sizeof line, "room %d, lives %d", room, nlife);
    ek_text_scale(r->x, r->y, "NEW GAME", EK_INK, 2);
    ek_text(r->x, r->y + 44, "throws this cave away and starts again", EK_INK);
    ek_text(r->x, r->y + 66, line, EK_INK);
    kshell_button(KFRAME_ACT_X, KFRAME_ACT_Y, KFRAME_ACT_W, KFRAME_ACT_H, "START", 0);
}

static int tap_new(const kshell_rect *r, int x, int y) {
    (void)r;
    if (!kshell_inside(x, y, KFRAME_ACT_X, KFRAME_ACT_Y, KFRAME_ACT_W, KFRAME_ACT_H))
        return 0;
    new_game();
    fprintf(stderr, "rockman: new game\n");
    kframe_close();
    return 1;
}

static void draw_speed(const kshell_rect *r) {
    static const char *labels[N_PACES];
    for (int i = 0; i < N_PACES; i++) labels[i] = PACES[i].label;
    ek_text_scale(r->x, r->y, "SPEED", EK_INK, 2);
    ek_text(r->x, r->y + 44, "how long one step waits. the cave stays.", EK_INK);
    if (turn_based)
        ek_text(r->x, r->y + 66, "unused while MODE is TURN: a press is the step.", EK_INK);
    kshell_options(PACE_Y0, labels, N_PACES, pace_choice());
}

static int tap_speed(const kshell_rect *r, int x, int y) {
    (void)r;
    int i = kshell_option_at(PACE_Y0, N_PACES, x, y);
    if (i < 0) return 0;
    move_ms = PACES[i].ms;
    note_pace();
    kframe_close();
    return 1;
}

/* MODE: when the cave moves. CLOCK is the original -- the world runs on its
 * own and a press joins in. TURN freezes the world: one press is one turn, and
 * the rocks, the monsters and the timer all advance on that press. */
static const struct { const char *label; int turn; } MODES[] = {
    { "CLOCK  the cave runs on its own", 0 },
    { "TURN   one press moves everything", 1 },
};
#define N_MODES ((int)(sizeof MODES / sizeof MODES[0]))
#define MODE_Y0 140

static int mode_choice(void) {
    for (int i = 0; i < N_MODES; i++)
        if (MODES[i].turn == turn_based) return i;
    return -1;
}

static void draw_mode(const kshell_rect *r) {
    static const char *labels[N_MODES];
    for (int i = 0; i < N_MODES; i++) labels[i] = MODES[i].label;
    ek_text_scale(r->x, r->y, "MODE", EK_INK, 2);
    ek_text(r->x, r->y + 44, "when the cave moves. the game carries on either way.", EK_INK);
    kshell_options(MODE_Y0, labels, N_MODES, mode_choice());
}

static int tap_mode(const kshell_rect *r, int x, int y) {
    (void)r;
    int i = kshell_option_at(MODE_Y0, N_MODES, x, y);
    if (i < 0) return 0;
    turn_based = MODES[i].turn;
    note_mode();
    kframe_close();
    return 1;
}

static int on_rock_item(int id) {
    fprintf(stderr, "rockman: menu item %d has no action of its own\n", id);
    return 1;
}

static const kframe_item ROCK_ITEMS[] = {
    { "NEW",   ROCK_NEW,   draw_new,   tap_new },
    { "SPEED", ROCK_SPEED, draw_speed, tap_speed },
    { "MODE",  ROCK_MODE,  draw_mode,  tap_mode },
};

static void kf_status(const char *msg) { fprintf(stderr, "rockman: %s\n", msg); }
static void kf_quit(void) { running = 0; stop_req = 1; }

static int open_frame(void) {
    kframe_cfg kf;
    memset(&kf, 0, sizeof kf);
    kf.title = "ROCKMAN";
    kf.app_id = "rockman";
    kf.items = ROCK_ITEMS;
    kf.nitems = (int)(sizeof ROCK_ITEMS / sizeof ROCK_ITEMS[0]);
    kf.on_item = on_rock_item;
    kf.on_quit = kf_quit;
    kf.repaint = present;
    kf.status = kf_status;
    kf.stop = &stop_req;
    if (kframe_init(&kf) != 0) {
        fprintf(stderr, "rockman: the shared menu would not start\n");
        return -1;
    }
    return 0;
}

/* ---- main --------------------------------------------------------------- */

static void on_signal(int sig) {
    (void)sig;
    stop_req = 1;
}

static void usage(void) {
    fprintf(stderr,
            "usage: rockman [options]\n"
            "\n"
            "  Dig dirt, grab the diamonds, don't get crushed.\n"
            "  LEFT UP RIGHT DOWN move. PUSH shoves a rock. QUIT then SURE stops.\n"
            "  The top-right square opens the menu: NEW, SPEED and MODE on the left,\n"
            "  VOLUME, WIFI, APPS and QUIT on the right.\n"
            "  A tap anywhere on the title starts a game.\n"
            "  --selftest   check the rules on this computer and exit\n"
            "  --auto N     play N steps toward the nearest diamond, then stop\n"
            "  --dump FILE  write the picture (PGM) and exit\n"
            "  --menu-shot F  write the menu (PGM) and exit\n"
            "  --seed N     same seed, same caves\n"
            "  --stats      print what each screen update cost\n"
            "  --refresh N  ms to leave the panel alone between updates\n"
            "               (default and floor: %d, one flush costs ~276-312 ms)\n"
            "  --wfm NAME   waveform for moving frames: DU / GL16F / GC16F / AUTO\n"
            "  --move N     ms per player step (default %d, one picture per step;\n"
            "               224 is the VIC-20 step and can share a picture)\n"
            "  --turn       turn based: nothing moves until a press, then the player,\n"
            "               the rocks, the monsters and the timer all advance once\n"
            "  --clock      the opposite of --turn (the default)\n"
            "  --tuned      the calibrated set: refresh %d, move %d, WFM DU\n",
            PANEL_FLOOR_MS, PANEL_FLOOR_MS, PANEL_FLOOR_MS, PANEL_FLOOR_MS);
}

int main(int argc, char **argv) {
    unsigned flags = EK_TAKEOVER;
    int do_test = 0;
    int auto_n = -1;
    const char *dump_path = NULL;
    const char *menu_shot = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            char *end = NULL;
            long v = strtol(argv[++i], &end, 10);
            if (!end || *end || v <= 0 || v > 0x7fffffff) { usage(); return 2; }
            game_seed = (uint32_t)v;
        } else if (!strcmp(argv[i], "--auto") && i + 1 < argc) {
            char *end = NULL;
            long v = strtol(argv[++i], &end, 10);
            if (!end || *end || v < 0 || v > 5000) { usage(); return 2; }
            auto_n = (int)v;
        } else if (!strcmp(argv[i], "--dump") && i + 1 < argc) {
            dump_path = argv[++i];
            if (!dump_path[0]) { usage(); return 2; }
        } else if (!strcmp(argv[i], "--move") && i + 1 < argc) {
            char *end = NULL;
            long v = strtol(argv[++i], &end, 10);
            if (!end || *end || v < 50 || v > 5000) { usage(); return 2; }
            move_ms = (int)v;
            note_pace();
        } else if (!strcmp(argv[i], "--menu-shot") && i + 1 < argc) {
            menu_shot = argv[++i];
            if (!menu_shot[0]) { usage(); return 2; }
        } else if (!strcmp(argv[i], "--stats")) stats_on = 1;
        else if (!strcmp(argv[i], "--probe")) { probe = 1; stats_on = 1; }
        else if (!strcmp(argv[i], "--refresh") && i + 1 < argc) {
            char *end = NULL;
            long v = strtol(argv[++i], &end, 10);
            if (!end || *end || v < 50 || v > 5000) { usage(); return 2; }
            /* Refuse rather than clamp: one flush costs ~276-312 ms, so a
             * smaller gap cannot be honoured, and quietly becoming 320 would
             * hide which number is really in force. */
            if (v < PANEL_FLOOR_MS) {
                fprintf(stderr,
                        "rockman: --refresh %ld is under the panel floor of %d ms "
                        "(one flush costs ~276-312 ms); pick %d or more\n",
                        v, PANEL_FLOOR_MS, PANEL_FLOOR_MS);
                return 2;
            }
            refresh_ms = (int)v;
        } else if (!strcmp(argv[i], "--wfm") && i + 1 < argc) {
            const char *nm = argv[++i];
            if (!strcasecmp(nm, "AUTO")) wfm_motion = 0;
            else {
                wfm_motion = ek_waveform_by_name(nm);
                if (!wfm_motion) {
                    fprintf(stderr, "rockman: unknown waveform '%s'\n", nm);
                    return 2;
                }
            }
        } else if (!strcmp(argv[i], "--tuned")) {
            refresh_ms = PANEL_FLOOR_MS;
            move_ms = PANEL_FLOOR_MS;
            wfm_motion = -1;   /* DU, resolved by name below */
        } else if (!strcmp(argv[i], "--turn")) { turn_based = 1; note_mode(); }
        else if (!strcmp(argv[i], "--clock")) { turn_based = 0; note_mode(); }
        else if (!strcmp(argv[i], "--selftest")) do_test = 1;
        else if (!strcmp(argv[i], "--no-takeover")) flags &= ~EK_TAKEOVER;
        else if (!strcmp(argv[i], "--help")) { usage(); return 0; }
        else { usage(); return 2; }
    }

    if (game_seed == 0) {
        struct timespec ts;
        if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
            fprintf(stderr, "rockman: clock_gettime failed\n");
            return 1;
        }
        game_seed = (uint32_t)ts.tv_sec ^ (uint32_t)ts.tv_nsec;
        if (game_seed == 0) game_seed = 1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    if (open_frame() != 0) return 1;

    if (ek_open(flags) != 0) {
        fprintf(stderr, "rockman: the screen would not open\n");
        ek_restore_ui();
        return 1;
    }
    wfm_clean = ek_waveform_by_name("GC16");
    if (!wfm_clean) {
        fprintf(stderr, "rockman: GC16 waveform is missing\n");
        ek_close();
        return 1;
    }
    if (wfm_motion == -1) {            /* --tuned asked for DU by name */
        wfm_motion = ek_waveform_by_name("DU");
        if (!wfm_motion) {
            fprintf(stderr, "rockman: DU waveform is missing\n");
            ek_close();
            return 1;
        }
    }
    ek_mono(1);
#ifdef EK_HOST
    ek_ghost(0, 0);
    ek_policy(180, 4, 30);
#else
    ek_waveform(wfm_motion);   /* 0 = AUTO, or the tuned waveform */
    ek_ghost(6, 30);
    ek_policy(60, 4, 30);
#endif

    running = 1;
    lives = (uint8_t)(RM_TILE_0 + 5);
    clear_digits(score);
    clear_digits(hi);
    init_flags();
    reseed();
    memset(drawn, 0xff, sizeof drawn);

    int rc = 0;
    if (do_test) {
        rc = selftest() ? 1 : 0;
        show_title();
    } else if (auto_n >= 0) {
        new_game();
        /* Present as it plays, not once at the end: otherwise --stats reports
         * the title screen's one full paint and nothing about the game, which
         * is the number that decides the refresh gap. */
        for (int i = 0; i < auto_n && phase == PHASE_PLAY && !stop_req; i++) {
            play_action(bot_action());
            present();
        }
    } else {
        show_title();
    }
    present();

    if (menu_shot) {
        kframe_show_menu();
        kframe_paint();
        ek_flush();
        if (ek_dump_pgm(menu_shot, 1) != 0) {
            fprintf(stderr, "rockman: could not write %s\n", menu_shot);
            ek_close();
            return 1;
        }
        ek_close();
        return rc;
    }

#ifdef EK_HOST
    if (!do_test && auto_n < 0 && !isatty(0)) {
        char buf[64];
        while (!stop_req && running && fgets(buf, sizeof buf, stdin)) {
            int x, y;
            if (buf[0] == 'q') break;
            if (sscanf(buf, "tap %d %d", &x, &y) != 2) {
                fprintf(stderr, "rockman: want 'tap X Y' or q\n");
                ek_close();
                return 2;
            }
            tap_at(x, y);
            present();
        }
    }
#else
    if (!do_test && auto_n < 0) {
        int held = -1;
        unsigned spins = 0;
        uint32_t loop_start = ek_ms();
        uint32_t next_pass = ek_ms();
        uint32_t next_flush = 0;
        int dirty = 1;
        while (running && !stop_req) {
            ek_event ev;
            while (ek_poll(&ev)) {
                if (ev.type == EK_DOWN) {
                    if (kframe_tap(ev.x, ev.y)) {
                        if (!kframe_covering()) redraw_all = 1;
                        held = -1;
                        dirty = 1;
                        continue;
                    }
                    if (kframe_covering()) continue;
                    int b = hit(ev.x, ev.y);
                    if (phase != PHASE_PLAY) {
                        keyq_clear();
                        if (b == ACT_QUIT) do_action(ACT_QUIT);
                        else if (phase == PHASE_WIN) begin_room1();
                        else new_game();
                        dirty = 1;
                    } else if (held >= 0) {
                        /* Finger never lifted (this panel repeats DOWN until
                         * UP): still the same press, so no second move. */
                    } else if (b >= 0) {
                        held = b;
                        if (turn_based) {
                            /* One press is one whole turn: the cave answers
                             * now, not on the next clock tick. Paint at once
                             * too -- there is nothing else to wait for, and a
                             * held-back frame would just be lag. */
                            play_action(b);
                            next_flush = 0;
                        } else {
                            keyq_push(b);   /* one press -> one queued move */
                        }
                        buttons_dirty = 1;
                        dirty = 1;
                    } else if (quit_armed) {
                        quit_armed = 0;
                        buttons_dirty = 1;
                        dirty = 1;
                    }
                } else if (ev.type == EK_UP) {
                    if (held >= 0) { held = -1; buttons_dirty = 1; dirty = 1; }
                }
            }
            uint32_t now = ek_ms();
            /* Catch up every pass the paint blocked through, then draw once.
             * One pass per loop made a step take 14 frames. The queue still
             * drives: holding a button does not walk. */
            if (pump_passes(now, &next_pass)) dirty = 1;
            if (dirty && (int32_t)(now - next_flush) >= 0) {
                /* refresh_ms is the frame PERIOD, not a sleep added after the
                 * paint: present() blocks for the panel's own ~300 ms, so
                 * timing from after it would make "refresh 320" really mean
                 * ~620 ms per frame. Anchor on the time the paint started. */
                uint32_t t0 = ek_ms();
                present();
                next_flush = t0 + (uint32_t)refresh_ms;
                dirty = 0;
            } else {
                ek_sleep_ms(5);
            }
            spins++;
            if (probe && (spins % 500) == 0)
                fprintf(stderr, "probe: %u loop iterations in %u ms = %.1f/s\n",
                        spins, ek_ms() - loop_start,
                        (double)spins * 1000.0 / (double)(ek_ms() - loop_start));
        }
    }
#endif

    if (dump_path) {
        if (ek_dump_pgm(dump_path, 1) != 0) {
            fprintf(stderr, "rockman: could not write %s\n", dump_path);
            ek_close();
            return 1;
        }
    }
    ek_close();
    return rc;
}
