#define _GNU_SOURCE
/* tetris.c -- Tetris on the Kindle Touch: a 600x800 panel, one bit, taps only.
 *
 * The whole design follows from what the panel charges for: an
 * MXCFB_SEND_UPDATE costs ~16 ms of driver time no matter how few pixels it
 * covers, so the cost of a frame is the *number of rects*, not the area. Two
 * consequences, and the game is built around both:
 *
 *   1. `drawn[][]` is what the canvas is already showing for the 10x20 well.
 *      Each frame `want[][]` is rebuilt from the model (locked blocks, the
 *      falling piece, the landing frame) and only the cells whose value
 *      differs are repainted. A piece falling one row repaints 8 cells and
 *      2 tile rows; the rest of the panel is untouched, so the engine sees a
 *      handful of differing 8x8 tiles and pushes one or two small rects.
 *   2. Score, lines, level and the NEXT box each have a shadow copy too, and
 *      are only redrawn when the number in them actually changes.
 *
 * Nothing here draws the full screen except the opening, a restart and the
 * game-over card, and those are the only frames that ask for a clean GC16
 * waveform. Everything else lets the engine pick (DU / GL16_FAST), with a
 * periodic GC16 clean over the area that keeps re-inking (`ek_ghost`).
 *
 * --stats prints what every flush really cost, in rects / tiles / pixels.
 */
#include "eink.h"
#include "kframe.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ---- geometry ----------------------------------------------------------- */

#define COLS 10
#define ROWS 20
#define CELL 30

#define WELL_X 152
#define WELL_Y 44
#define WELL_W (COLS * CELL)  /* 300 */
#define WELL_H (ROWS * CELL)  /* 600 */

#define HUD_X 10
#define NEXT_X 456
#define NEXT_Y 76
#define NEXT_S 108 /* the NEXT box, border included */

#define BTN_Y1 656
#define BTN_Y2 726
#define BTN_W 186
#define BTN_H 62

/* What a well cell can look like on the canvas. */
enum { W_EMPTY = 0, W_BLOCK = 1, W_GHOST = 2 };

/* ---- pieces ------------------------------------------------------------- */

/* Spawn orientation, 4x4, trimmed to its own bounding box at startup and then
 * rotated four times into SH[piece][state]. Rotating about the tight box (not
 * about a fixed 4x4) is what stops a piece drifting sideways as it turns. */
static const char *const ART[7][4] = {
    {"....", "IIII", "....", "...."}, /* I */
    {"OO..", "OO..", "....", "...."}, /* O */
    {".T..", "TTT.", "....", "...."}, /* T */
    {".SS.", "SS..", "....", "...."}, /* S */
    {"ZZ..", ".ZZ.", "....", "...."}, /* Z */
    {"J...", "JJJ.", "....", "...."}, /* J */
    {"..L.", "LLL.", "....", "...."}, /* L */
};

typedef struct {
    int n;                 /* how many cells (always 4) */
    unsigned char x[4], y[4];
    int w, h;              /* bounding box */
} shape;

static shape SH[7][4];

static void sort_cells(shape *s) {
    /* Stable, so a shape and its rotations compare exactly. */
    for (int i = 0; i < s->n; i++)
        for (int j = i + 1; j < s->n; j++)
            if (s->y[j] < s->y[i] || (s->y[j] == s->y[i] && s->x[j] < s->x[i])) {
                unsigned char t = s->x[i]; s->x[i] = s->x[j]; s->x[j] = t;
                t = s->y[i]; s->y[i] = s->y[j]; s->y[j] = t;
            }
}

static void rotate_cw(const shape *in, shape *out) {
    /* (x,y) -> (h-1-y, x) inside the box, so the box becomes h wide, w tall. */
    out->n = in->n;
    out->w = in->h;
    out->h = in->w;
    for (int i = 0; i < in->n; i++) {
        out->x[i] = (unsigned char)(in->h - 1 - in->y[i]);
        out->y[i] = in->x[i];
    }
    sort_cells(out);
}

static void build_shapes(void) {
    for (int p = 0; p < 7; p++) {
        shape s;
        memset(&s, 0, sizeof s);
        int minx = 99, miny = 99, maxx = -1, maxy = -1;
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++) {
                if (ART[p][r][c] == '.') continue;
                if (c < minx) minx = c;
                if (c > maxx) maxx = c;
                if (r < miny) miny = r;
                if (r > maxy) maxy = r;
            }
        if (maxx < 0) {
            fprintf(stderr, "tetris: piece %d is empty in ART\n", p);
            exit(1);
        }
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                if (ART[p][r][c] != '.') {
                    s.x[s.n] = (unsigned char)(c - minx);
                    s.y[s.n] = (unsigned char)(r - miny);
                    s.n++;
                }
        if (s.n != 4) {
            fprintf(stderr, "tetris: piece %d has %d cells, want 4\n", p, s.n);
            exit(1);
        }
        s.w = maxx - minx + 1;
        s.h = maxy - miny + 1;
        sort_cells(&s);
        SH[p][0] = s;
        for (int k = 1; k < 4; k++) rotate_cw(&SH[p][k - 1], &SH[p][k]);
    }
}

/* ---- game state --------------------------------------------------------- */

static unsigned char board[ROWS][COLS]; /* 0 empty, 1 locked */
static unsigned char drawn[ROWS][COLS]; /* what the canvas shows: W_* */

static int cur_pt, cur_rot, cur_x, cur_y;
static int next_pt;

static int score, lines, level;
static int over;      /* game-over card is up */
static int running;   /* 0 = leave and give the screen back */
static int redraw_all; /* next present() repaints the whole panel */

static int last_score = -1, last_lines = -1, last_level = -1, last_next = -1;

static uint32_t rng_state = 1;
static unsigned char bag[7];
static int bag_left;

static int stats_on, force_clean, clean_next, probe;
static int wfm_clean;

/* Calibration values, settable so the tuned pair can be played as a real
 * game instead of guessed at in the tuner. 0 = keep the level table. */
static int speed_override;   /* ms between drops, 0 = drop_ms() level table */
static int wfm_motion;       /* forced waveform on motion frames, 0 = AUTO   */
static int clean_every;      /* GC16 clean every N drops, 0 = engine's own   */
static int drop_n;           /* drops since the game started (clean cadence) */
static int warned_slow;      /* the panel's real cost was reported once      */
static volatile int stop_req;
#ifndef EK_HOST
static uint32_t drop_at; /* next auto-drop deadline, anchored on a fixed beat */
#endif

static int failures;
static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "tetris: FAIL %s\n", msg);
        failures++;
    }
}

static uint32_t rnd(void) {
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

/* A seven-bag: every piece once before any piece twice. */
static int pick_piece(void) {
    if (bag_left == 0) {
        for (int i = 0; i < 7; i++) bag[i] = (unsigned char)i;
        for (int i = 6; i > 0; i--) {
            int j = (int)(rnd() % (uint32_t)(i + 1));
            unsigned char t = bag[i]; bag[i] = bag[j]; bag[j] = t;
        }
        bag_left = 7;
    }
    return bag[--bag_left];
}

static int fits(int pt, int rot, int x, int y) {
    const shape *s = &SH[pt][rot];
    for (int i = 0; i < s->n; i++) {
        int cx = x + s->x[i], cy = y + s->y[i];
        if (cx < 0 || cx >= COLS || cy >= ROWS) return 0;
        if (cy < 0) continue; /* above the top of the well is legal on spawn */
        if (board[cy][cx]) return 0;
    }
    return 1;
}

static int ghost_y(void) {
    int y = cur_y;
    while (fits(cur_pt, cur_rot, cur_x, y + 1)) y++;
    return y;
}

/* The level table, the panel floor, and the tuned level-1 speed.
 *
 * Measured on the device: a DU repaint of a falling piece costs ~276-312 ms.
 * So any beat at or under ~300 ms is the panel's own limit, not a speed --
 * it plays at ~300 ms whatever was asked for. That is why 200 read as "too
 * fast", and 600 (about double) is the first level-1 beat with a real, even
 * wait between drops.
 *
 * PANEL_FLOOR clamps every level, so a late level cannot ask the glass for
 * something it cannot paint. LEVEL_1_TUNED is what --tuned (and so calib's
 * numbers) start at; the plain table still starts at 800. */
#define LEVEL_STEP     70
#define PANEL_FLOOR    320
#define LEVEL_1_TUNED  600

static int drop_ms(void) {
    int ms;
    if (speed_override) {
        /* A fixed speed still gets faster per level, at the same 70 ms a level
         * the table uses -- otherwise --speed would freeze the difficulty. */
        ms = speed_override - (level - 1) * LEVEL_STEP;
    } else {
        ms = 800 - (level - 1) * LEVEL_STEP;
    }
    return ms < PANEL_FLOOR ? PANEL_FLOOR : ms;
}

static void draw_cell(int cx, int cy, int style);

/* Repaint whatever the canvas must show now. Only the cells that changed are
 * touched, which is the whole point: a one-row fall repaints 8 cells. */
static void flush_well(void) {
    unsigned char want[ROWS][COLS];

    memset(want, W_EMPTY, sizeof want);
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (board[r][c]) want[r][c] = W_BLOCK;

    if (!over) {
        /* The landing frame first, so the piece wins wherever they overlap. */
        const shape *g = &SH[cur_pt][cur_rot];
        int gy = ghost_y();
        for (int i = 0; i < g->n; i++) {
            int cx = cur_x + g->x[i], cy = gy + g->y[i];
            if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) continue;
            if (!board[cy][cx]) want[cy][cx] = W_GHOST;
        }
        const shape *p = &SH[cur_pt][cur_rot];
        for (int i = 0; i < p->n; i++) {
            int cx = cur_x + p->x[i], cy = cur_y + p->y[i];
            if (cx < 0 || cx >= COLS || cy < 0 || cy >= ROWS) continue;
            want[cy][cx] = W_BLOCK;
        }
    }

    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (want[r][c] != drawn[r][c]) {
                draw_cell(c, r, want[r][c]);
                drawn[r][c] = want[r][c];
            }
}

/* ---- drawing ------------------------------------------------------------ */

static void draw_cell(int cx, int cy, int style) {
    const int x = WELL_X + cx * CELL, y = WELL_Y + cy * CELL;

    /* Each cell owns its own square, including the seam ticks at its bottom
     * right corner, so repainting one cell can never leave a neighbour's
     * pixels half-erased. */
    ek_fill_rect(x, y, CELL, CELL, EK_PAPER);

    if (style == W_BLOCK) {
        ek_fill_rect(x + 2, y + 2, CELL - 4, CELL - 4, EK_INK);
    } else if (style == W_GHOST) {
        /* An empty frame where the piece will land. Bold enough to read at a
         * glance, and it cannot be mistaken for a locked block. */
        ek_fill_rect(x + 4, y + 4, CELL - 8, 2, EK_INK);
        ek_fill_rect(x + 4, y + CELL - 6, CELL - 8, 2, EK_INK);
        ek_fill_rect(x + 4, y + 4, 2, CELL - 8, EK_INK);
        ek_fill_rect(x + CELL - 6, y + 4, 2, CELL - 8, EK_INK);
    }

    /* Corner tick: a faint dot grid, so an empty well is still easy to count. */
    ek_fill_rect(x + CELL - 2, y + CELL - 2, 2, 2, EK_INK);
}

static void draw_well_frame(void) {
    ek_fill_rect(WELL_X - 4, WELL_Y - 4, WELL_W + 8, WELL_H + 8, EK_INK);
    ek_fill_rect(WELL_X - 2, WELL_Y - 2, WELL_W + 4, WELL_H + 4, EK_PAPER);
}

static void draw_value(int x, int y, int v) {
    char buf[16];
    snprintf(buf, sizeof buf, "%d", v);
    ek_fill_rect(x, y, 104, 32, EK_PAPER);
    ek_text_scale(x, y, buf, EK_INK, 2);
}

static void draw_next(void) {
    const shape *s = &SH[next_pt][0];
    const int in_x = NEXT_X + 3, in_y = NEXT_Y + 3, in_s = NEXT_S - 6;
    ek_fill_rect(in_x, in_y, in_s, in_s, EK_PAPER);
    int ox = in_x + (in_s - s->w * 20) / 2;
    int oy = in_y + (in_s - s->h * 20) / 2;
    for (int i = 0; i < s->n; i++)
        ek_fill_rect(ox + s->x[i] * 20, oy + s->y[i] * 20, 16, 16, EK_INK);
}

/* ---- buttons ------------------------------------------------------------ */

enum { A_LEFT, A_ROT, A_RIGHT, A_DOWN, A_DROP, N_ACT };

static const struct { int x, y, w, h; } BTN[N_ACT] = {
    {10, BTN_Y1, BTN_W, BTN_H},
    {206, BTN_Y1, BTN_W, BTN_H},
    {402, BTN_Y1, BTN_W, BTN_H},
    {10, BTN_Y2, BTN_W, BTN_H},
    {206, BTN_Y2, BTN_W, BTN_H},
};

static const char *const BTN_LABEL[N_ACT] = {"LEFT", "TURN", "RIGHT", "DOWN", "DROP"};

/* When each button stops being shown pressed. A tap is a press and a lift,
 * often landing inside a single poll, so the flash is timed rather than tied
 * to the lift: the button lights up for a moment whether or not a separate
 * lift event ever arrives. */
static uint32_t btn_flash[N_ACT];

static void draw_button(int i, int pressed);

/* The four numbers and the NEXT box, only when they change. */
static void flush_hud(void) {
    if (score != last_score) { draw_value(HUD_X, 76, score); last_score = score; }
    if (lines != last_lines) { draw_value(HUD_X, 156, lines); last_lines = lines; }
    if (level != last_level) { draw_value(HUD_X, 236, level); last_level = level; }
    if (next_pt != last_next) { draw_next(); last_next = next_pt; }
}

static void draw_over_card(void) {
    const int w = 292, h = 210;
    const int x = WELL_X + (WELL_W - w) / 2;
    const int y = WELL_Y + (WELL_H - h) / 2;

    ek_fill_rect(x, y, w, h, EK_PAPER);
    ek_rect(x, y, w, h, EK_INK);
    ek_rect(x + 1, y + 1, w - 2, h - 2, EK_INK);
    ek_text_scale(x + (w - 9 * 16) / 2, y + 26, "GAME OVER", EK_INK, 2);

    char buf[32];
    snprintf(buf, sizeof buf, "SCORE %d", score);
    ek_text_scale(x + (w - (int)strlen(buf) * 16) / 2, y + 84, buf, EK_INK, 2);
    snprintf(buf, sizeof buf, "LINES %d", lines);
    ek_text_scale(x + (w - (int)strlen(buf) * 16) / 2, y + 124, buf, EK_INK, 2);
    ek_text_scale(x + (w - 11 * 16) / 2, y + 168, "TAP TO PLAY", EK_INK, 2);
}

/* The parts that never move: title, labels, NEXT box, buttons, well frame. */
static void draw_chrome(void) {
    ek_text_scale(14, 8, "TETRIS", EK_INK, 2);

    ek_text(HUD_X, 56, "SCORE", EK_INK);
    ek_text(HUD_X, 136, "LINES", EK_INK);
    ek_text(HUD_X, 216, "LEVEL", EK_INK);
    last_score = last_lines = last_level = -1;

    ek_text(NEXT_X + 4, 56, "NEXT", EK_INK);
    ek_rect(NEXT_X, NEXT_Y, NEXT_S, NEXT_S, EK_INK);
    ek_rect(NEXT_X + 1, NEXT_Y + 1, NEXT_S - 2, NEXT_S - 2, EK_INK);
    last_next = -1;

    draw_well_frame();
    for (int i = 0; i < N_ACT; i++) draw_button(i, 0);
}

static void draw_all(void) {
    ek_clear(EK_PAPER);
    draw_chrome();
    memset(drawn, 0xff, sizeof drawn); /* nothing is on the canvas yet */
    flush_well();
    flush_hud();
    if (over) draw_over_card();
}

static void draw_button(int i, int pressed) {
    const int x = BTN[i].x, y = BTN[i].y, w = BTN[i].w, h = BTN[i].h;
    const char *s = BTN_LABEL[i];
    const int tw = (int)strlen(s) * 16;

    ek_fill_rect(x, y, w, h, EK_PAPER);
    ek_rect(x, y, w, h, EK_INK);
    ek_rect(x + 1, y + 1, w - 2, h - 2, EK_INK);
    ek_text_scale(x + (w - tw) / 2, y + (h - 32) / 2, s, EK_INK, 2);
    if (pressed) ek_invert_rect(x, y, w, h);
}

static int hit(int x, int y) {
    for (int i = 0; i < N_ACT; i++)
        if (x >= BTN[i].x && x < BTN[i].x + BTN[i].w &&
            y >= BTN[i].y && y < BTN[i].y + BTN[i].h)
            return i;
    return -1;
}

/* ---- rules -------------------------------------------------------------- */

static void spawn(void) {
    cur_pt = next_pt;
    next_pt = pick_piece();
    cur_rot = 0;
    cur_x = (COLS - SH[cur_pt][0].w) / 2;
    cur_y = 0;
    if (!fits(cur_pt, cur_rot, cur_x, cur_y)) {
        over = 1;
        redraw_all = 1;
    }
}

static int try_rotate(void) {
    /* A turn that would clip a wall or the stack is nudged sideways, then up:
     * a standing I against the right wall needs three columns of shove. */
    static const int kick_x[] = {0, -1, 1, -2, 2, -3, 3, 0, 0, -3, 3};
    static const int kick_y[] = {0, 0, 0, 0, 0, 0, 0, -1, -2, -1, -1};
    int nr = (cur_rot + 1) % 4;

    for (unsigned i = 0; i < sizeof kick_x / sizeof kick_x[0]; i++)
        if (fits(cur_pt, nr, cur_x + kick_x[i], cur_y + kick_y[i])) {
            cur_rot = nr;
            cur_x += kick_x[i];
            cur_y += kick_y[i];
            return 1;
        }
    return 0;
}

/* Lock the piece where it stands and score whatever it completed. */
static int lock_piece(void) {
    const int LINE_SCORE[5] = {0, 100, 300, 500, 800};
    const shape *s = &SH[cur_pt][cur_rot];
    int cleared = 0;

    for (int i = 0; i < s->n; i++) {
        int cx = cur_x + s->x[i], cy = cur_y + s->y[i];
        if (cy < 0 || cy >= ROWS || cx < 0 || cx >= COLS) continue;
        board[cy][cx] = 1;
    }

    for (int r = ROWS - 1; r >= 0; r--) {
        int full = 1;
        for (int c = 0; c < COLS; c++)
            if (!board[r][c]) { full = 0; break; }
        if (!full) continue;
        cleared++;
        for (int rr = r; rr > 0; rr--) memcpy(board[rr], board[rr - 1], COLS);
        memset(board[0], 0, COLS);
        r++; /* this row now holds what was above it */
    }

    if (cleared) {
        score += LINE_SCORE[cleared] * level;
        lines += cleared;
        int nl = 1 + lines / 10;
        if (nl > 20) nl = 20;
        if (nl != level) {
            level = nl;
            clean_next = 1; /* a new speed: give the panel one clean frame */
        }
    }
    spawn();
    return cleared;
}

static void step_down(void) {
    if (over) return;
    drop_n++;
    if (fits(cur_pt, cur_rot, cur_x, cur_y + 1)) cur_y++;
    else lock_piece();
}

static void hard_drop(void) {
    if (over) return;
    while (fits(cur_pt, cur_rot, cur_x, cur_y + 1)) cur_y++;
    drop_n++;
    lock_piece();
}

static void reset_game(uint32_t seed) {
    memset(board, 0, sizeof board);
    score = lines = 0;
    level = 1;
    over = 0;
    running = 1;
    drop_n = 0;
    rng_state = seed ? seed : 1;
    bag_left = 0;
    next_pt = pick_piece();
    spawn();
    redraw_all = 1;
#ifndef EK_HOST
    /* A new game starts on a fresh beat: after a game-over stall the old
     * deadline is far in the past, and resuming from it would fire the first
     * drop instantly (then the += anchor keeps that early burst), which reads
     * as jitter. Anchor to now+one interval so the first drop comes after a
     * full, even wait. */
    drop_at = ek_ms() + (uint32_t)drop_ms();
#endif
}

/* ---- the computer playing, for --auto ----------------------------------- */

/* One-ply: try every rotation in every column, score the landing, take the
 * best. Enough to clear lines, which is what an on-PC check needs. */
static void bot_move(void) {
    int found = 0, best = 0, best_rot = 0, best_x = 0;

    for (int rot = 0; rot < 4; rot++) {
        const shape *s = &SH[cur_pt][rot];
        for (int x = -s->w + 1; x < COLS; x++) {
            if (!fits(cur_pt, rot, x, 0)) continue;
            int y = 0;
            while (fits(cur_pt, rot, x, y + 1)) y++;

            /* Copy the board, land the piece, measure it. */
            unsigned char t[ROWS][COLS];
            memcpy(t, board, sizeof t);
            for (int i = 0; i < s->n; i++) {
                int cx = x + s->x[i], cy = y + s->y[i];
                if (cy >= 0) t[cy][cx] = 1;
            }
            int full = 0;
            for (int r = 0; r < ROWS; r++) {
                int f = 1;
                for (int c = 0; c < COLS; c++)
                    if (!t[r][c]) { f = 0; break; }
                if (f) full++;
            }
            int holes = 0;
            for (int c = 0; c < COLS; c++) {
                int seen = 0;
                for (int r = 0; r < ROWS; r++) {
                    if (t[r][c]) seen = 1;
                    else if (seen) holes++;
                }
            }
            int bumps = 0;
            for (int c = 0; c + 1 < COLS; c++) {
                int a = 0, b = 0;
                for (int r = 0; r < ROWS; r++) {
                    if (t[r][c]) { a = ROWS - r; break; }
                }
                for (int r = 0; r < ROWS; r++) {
                    if (t[r][c + 1]) { b = ROWS - r; break; }
                }
                bumps += a > b ? a - b : b - a;
            }
            int sc = full * 300 - holes * 120 - bumps * 6 - (y * 2);
            /* Scores can be negative (the floor-height penalty), so a separate
             * flag means "a placement was found", not "the score is positive". */
            if (!found || sc > best) {
                found = 1;
                best = sc;
                best_rot = rot;
                best_x = x;
            }
        }
    }
    if (!found) return; /* nowhere to put it: the game is over anyway */
    cur_rot = best_rot;
    cur_x = best_x;
    cur_y = 0;
    hard_drop();
}

/* ---- presenting --------------------------------------------------------- */

/* Buttons light up on press and go out on their own timer (see btn_flash). */
static void expire_buttons(void) {
    uint32_t now = ek_ms();
    for (int i = 0; i < N_ACT; i++)
        if (btn_flash[i] && (int32_t)(now - btn_flash[i]) >= 0) {
            btn_flash[i] = 0;
            draw_button(i, 0);
        }
}

static void present(void) {
    if (kframe_covering()) {
        kframe_paint();
        ek_flush();
        return;
    }
    int clean = force_clean || clean_next || redraw_all;
    /* A clean fires on the calib cadence too: every N drops, like the tuner's
     * CLEAN knob. count is advanced by the callers that actually drop a piece. */
    if (!clean && clean_every > 0 && drop_n > 0 && drop_n % clean_every == 0)
        clean = 1;

    if (redraw_all) {
        memset(btn_flash, 0, sizeof btn_flash); /* fresh paint, no flashes */
        draw_all();
        redraw_all = 0;
    } else {
        expire_buttons();
        flush_well();
        flush_hud();
    }
    clean_next = 0;

    ek_waveform(clean ? wfm_clean : wfm_motion);
    kframe_paint();
    ek_stats st = ek_flush();
    ek_waveform(0);

    /* An honest floor: if the panel's own refresh costs more than the beat we
     * asked for, the drops cannot run at that speed. Say so once instead of
     * silently playing slower than the number in --speed. */
    if (!warned_slow && (int)st.us > drop_ms() + 20) {
        warned_slow = 1;
        fprintf(stderr,
                "tetris: the panel needs %u ms per update but the beat is %d ms; "
                "drops run as fast as the panel can, not at %d ms. "
                "Fastest honest beat is about %u ms.\n",
                st.us, drop_ms(), drop_ms(), st.us + 10u);
    }

    if (stats_on)
        fprintf(stderr,
                "tetris: flush wfm=%d rects=%d tiles=%d px=%ld full=%d folded=%d us=%u\n",
                st.wfm, st.rects, st.tiles, st.px, st.full, st.folded, st.us);
    if (probe)
        fprintf(stderr, "probe: flush wfm=%d rects=%d tiles=%d px=%ld full=%d us=%u\n",
                st.wfm, st.rects, st.tiles, st.px, st.full, st.us);
}

/* ---- input -------------------------------------------------------------- */

static void act(int a) {
    switch (a) {
    case A_LEFT:
        if (fits(cur_pt, cur_rot, cur_x - 1, cur_y)) cur_x--;
        break;
    case A_ROT: try_rotate(); break;
    case A_RIGHT:
        if (fits(cur_pt, cur_rot, cur_x + 1, cur_y)) cur_x++;
        break;
    case A_DOWN: step_down(); break;
    case A_DROP: hard_drop(); break;
    }
}

static void tap_down(int x, int y) {
    if (kframe_tap(x, y)) return;
    /* The game-over card is dismissed by any tap at all. */
    if (over) {
        reset_game((uint32_t)time(NULL) ^ (uint32_t)ek_ms());
        return;
    }
    int i = hit(x, y);
    if (i < 0) return;
    act(i);
    btn_flash[i] = ek_ms() + 140;
    draw_button(i, 1);
}

/* ---- self-test ---------------------------------------------------------- */

static void clear_board(void) { memset(board, 0, sizeof board); }

static int selftest(void) {
    /* 1. Seven shapes, four cells each, four turns returns the same shape. */
    for (int p = 0; p < 7; p++) {
        for (int k = 0; k < 4; k++)
            expect(SH[p][k].n == 4, "every rotation keeps four cells");
        expect(memcmp(&SH[p][0], &SH[p][3], sizeof(shape)) != 0 ||
                   (SH[p][0].w == SH[p][0].h && SH[p][0].w == 2),
               "four turns move the shape (except the square)");
        shape back;
        rotate_cw(&SH[p][3], &back);
        sort_cells(&SH[p][0]);
        expect(back.n == SH[p][0].n && back.w == SH[p][0].w && back.h == SH[p][0].h &&
                   !memcmp(back.x, SH[p][0].x, 4) && !memcmp(back.y, SH[p][0].y, 4),
               "a full turn returns to the spawn shape");
    }
    expect(SH[0][0].w == 4 && SH[0][0].h == 1, "I is flat at spawn");
    expect(SH[1][0].w == 2 && SH[1][0].h == 2, "O is a square");

    /* 2. The bag deals all seven before repeating. */
    rng_state = 12345;
    bag_left = 0;
    int seen[7] = {0};
    for (int i = 0; i < 70; i++) {
        int p = pick_piece();
        expect(p >= 0 && p < 7, "the bag only deals real pieces");
        seen[p]++;
    }
    for (int p = 0; p < 7; p++) expect(seen[p] == 10, "70 draws give each piece ten times");
    rng_state = 999;
    bag_left = 0;
    for (int g = 0; g < 10; g++) {
        int in[7] = {0};
        for (int i = 0; i < 7; i++) in[pick_piece()]++;
        for (int p = 0; p < 7; p++) expect(in[p] == 1, "a bag of seven holds each piece once");
    }

    /* 3. Walls. */
    clear_board();
    expect(fits(1, 0, 0, 0), "O fits in the top left corner");
    expect(!fits(1, 0, -1, 0), "O cannot hang off the left wall");
    expect(!fits(1, 0, 9, 0), "O cannot hang off the right wall");
    expect(!fits(1, 0, 0, ROWS - 1), "O cannot go through the floor");

    /* 4. A hard drop lands on the floor. */
    clear_board();
    cur_pt = 1; cur_rot = 0; cur_x = 0; cur_y = 0; next_pt = 0;
    while (fits(cur_pt, cur_rot, cur_x, cur_y + 1)) cur_y++;
    expect(cur_y == ROWS - 2, "a square lands on the last two rows");

    /* 5. One line cleared scores 100 at level 1 and shifts the row above down. */
    clear_board();
    for (int c = 0; c < COLS; c++) if (c != 4 && c != 5) board[ROWS - 1][c] = 1;
    cur_pt = 1; cur_rot = 0; cur_x = 4; cur_y = 17; next_pt = 2;
    level = 1; score = 0; lines = 0;
    while (fits(cur_pt, cur_rot, cur_x, cur_y + 1)) cur_y++;
    expect(lock_piece() == 1, "a filled row clears");
    expect(lines == 1, "one line counted");
    expect(score == 100, "one line at level 1 scores 100");
    /* The clear removed row 19; the O's top half (row 18) fell into it. */
    expect(board[ROWS - 1][4] && board[ROWS - 1][5] && !board[ROWS - 2][4],
           "the surviving cells fell into the cleared row");

    /* 6. Four at once is a tetris: 800. */
    clear_board();
    for (int r = ROWS - 4; r < ROWS; r++)
        for (int c = 0; c < COLS - 1; c++) board[r][c] = 1;
    cur_pt = 0; cur_rot = 1; cur_x = 9; cur_y = ROWS - 4; next_pt = 3;
    level = 1; score = 0; lines = 0;
    expect(fits(0, 1, 9, ROWS - 4), "a standing I fits in the last column");
    expect(lock_piece() == 4, "four rows clear at once");
    expect(score == 800, "a tetris at level 1 scores 800");
    expect(lines == 4, "four lines counted");

    /* 7. A standing I at the right wall kicks left when it turns flat. */
    clear_board();
    cur_pt = 0; cur_rot = 1; cur_x = 9; cur_y = 5;
    expect(try_rotate(), "a turn at the wall is allowed");
    expect(SH[0][cur_rot].h == 1, "the I turned flat");
    expect(cur_x == 6, "it kicked from column 9 to column 6");
    expect(fits(cur_pt, cur_rot, cur_x, cur_y), "and it is legally placed");

    /* 8. Nowhere to spawn is game over. */
    clear_board();
    for (int c = 0; c < COLS; c++) { board[0][c] = 1; board[1][c] = 1; }
    cur_pt = 1; cur_rot = 0; cur_x = 4; cur_y = 0; next_pt = 4;
    over = 0;
    spawn();
    expect(over, "a full top row ends the game");

    /* 9. The level rises with the lines and the drop gets quicker. */
    level = 1;
    int slow = drop_ms();
    level = 5;
    int fast = drop_ms();
    expect(fast < slow, "a higher level drops the piece sooner");
    level = 40;
    expect(drop_ms() == PANEL_FLOOR, "no level asks for less than the panel can paint");

    /* Leave the machine in a clean state for whatever runs next. */
    memset(drawn, 0xff, sizeof drawn);
    over = 0;
    return failures;
}

/* ---- the shared menu ---------------------------------------------------- */

enum { TETRIS_NEW = 1 };

static void draw_new(const kshell_rect *r) {
    ek_text_scale(r->x, r->y, "NEW GAME", EK_INK, 2);
    ek_text(r->x, r->y + 44, "throws this game away and starts again", EK_INK);
    kshell_button(KFRAME_ACT_X, KFRAME_ACT_Y, KFRAME_ACT_W, KFRAME_ACT_H, "START", 0);
}

static int on_tetris_item(int id) {
    if (id == TETRIS_NEW) {
        reset_game((uint32_t)time(NULL) ^ (uint32_t)ek_ms());
        return 1;
    }
    fprintf(stderr, "tetris: menu item %d is not one of ours\n", id);
    return 1;
}

static const kframe_item TETRIS_ITEMS[] = {
    { "NEW", TETRIS_NEW, draw_new, NULL },
};

static void kf_status(const char *msg) { fprintf(stderr, "tetris: %s\n", msg); }
static void kf_quit(void) { running = 0; stop_req = 1; }

static int open_frame(void) {
    kframe_cfg kf;
    memset(&kf, 0, sizeof kf);
    kf.title = "TETRIS";
    kf.app_id = "tetris";
    kf.items = TETRIS_ITEMS;
    kf.nitems = (int)(sizeof TETRIS_ITEMS / sizeof TETRIS_ITEMS[0]);
    kf.on_item = on_tetris_item;
    kf.on_quit = kf_quit;
    kf.repaint = present;
    kf.status = kf_status;
    kf.stop = &stop_req;
    if (kframe_init(&kf) != 0) {
        fprintf(stderr, "tetris: the shared menu would not start\n");
        return -1;
    }
    return 0;
}

/* ---- main --------------------------------------------------------------- */

static void usage(void) {
    fprintf(stderr,
            "usage: tetris [options]\n"
            "\n"
            "  The top-right button opens the menu. NEW starts again.\n"
            "  VOLUME, WIFI, APPS and QUIT are on the right. APPS opens\n"
            "  another game, and that game comes back after a restart.\n"
            "  QUIT needs a second tap and gives the screen back until then.\n"
            "  Tap LEFT / TURN / RIGHT to move and turn, DOWN to nudge it down,\n"
            "  DROP to slam it.\n"
            "  --selftest   check the rules on this computer and exit\n"
            "  --auto N     play N pieces by itself, then stop\n"
            "  --dump FILE  write the picture (PGM) and exit\n"
            "  --menu-shot F  write the menu (PGM) and exit\n"
            "  --seed N     same seed, same bag\n"
            "  --stats      print what each screen update cost\n"
            "  --clean      refresh the whole picture on every update (slow)\n"
            "  --speed N    fixed level-1 ms between drops (default 600)\n"
            "  --wfm NAME   waveform for motion frames: DU / GL16F / GC16F / AUTO\n"
            "  --clean-n N  GC16 clean every N drops (0 = engine's own policy)\n"
            "  --tuned      the calibrated set: speed 600, clean 30, WFM DU\n"
            "  --probe      log every drop and flush for timing (debug)\n");
}

static void on_signal(int sig) {
    (void)sig;
    stop_req = 1;
}

int main(int argc, char **argv) {
    unsigned flags = EK_TAKEOVER;
    int wait = 1;
    int do_test = 0;
    int auto_pieces = -1;
    const char *dump_path = NULL;
    const char *menu_shot = NULL;
    uint32_t seed = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            char *end = NULL;
            long v = strtol(argv[++i], &end, 10);
            if (!end || *end || v <= 0 || v > 0x7fffffff) { usage(); return 2; }
            seed = (uint32_t)v;
        } else if (!strcmp(argv[i], "--auto") && i + 1 < argc) {
            char *end = NULL;
            long v = strtol(argv[++i], &end, 10);
            if (!end || *end || v < 0 || v > 5000) { usage(); return 2; }
            auto_pieces = (int)v;
        } else if (!strcmp(argv[i], "--dump") && i + 1 < argc) {
            dump_path = argv[++i];
            if (!dump_path[0]) { usage(); return 2; }
        } else if (!strcmp(argv[i], "--menu-shot") && i + 1 < argc) {
            menu_shot = argv[++i];
            if (!menu_shot[0]) { usage(); return 2; }
        } else if (!strcmp(argv[i], "--selftest")) do_test = 1;
        else if (!strcmp(argv[i], "--stats")) stats_on = 1;
        else if (!strcmp(argv[i], "--clean")) force_clean = 1;
        else if (!strcmp(argv[i], "--tuned")) {
            speed_override = LEVEL_1_TUNED;
            clean_every = 30;
            wfm_motion = -1;   /* DU, resolved by name below */
        } else if (!strcmp(argv[i], "--speed") && i + 1 < argc) {
            char *end = NULL;
            long v = strtol(argv[++i], &end, 10);
            if (!end || *end || v < 50 || v > 5000) { usage(); return 2; }
            /* Refuse rather than clamp: a drop costs ~276 ms to paint, so a
             * smaller number cannot be honoured and quietly becoming 320
             * would hide which number is really in force. */
            if (v < PANEL_FLOOR) {
                fprintf(stderr,
                        "tetris: --speed %ld is under the panel floor of %d ms "
                        "(one DU repaint costs ~276-312 ms); pick %d or more\n",
                        v, PANEL_FLOOR, PANEL_FLOOR);
                return 2;
            }
            speed_override = (int)v;
        } else if (!strcmp(argv[i], "--clean-n") && i + 1 < argc) {
            char *end = NULL;
            long v = strtol(argv[++i], &end, 10);
            if (!end || *end || v < 0 || v > 999) { usage(); return 2; }
            clean_every = (int)v;
        } else if (!strcmp(argv[i], "--wfm") && i + 1 < argc) {
            const char *nm = argv[++i];
            if (!strcasecmp(nm, "AUTO")) wfm_motion = 0;
            else {
                wfm_motion = ek_waveform_by_name(nm);
                if (!wfm_motion) {
                    fprintf(stderr, "tetris: unknown waveform '%s'\n", nm);
                    return 2;
                }
            }
        }
        else if (!strcmp(argv[i], "--no-takeover")) flags &= ~EK_TAKEOVER;
        else if (!strcmp(argv[i], "--no-wait")) wait = 0;
        else if (!strcmp(argv[i], "--probe")) probe = 1;
        else if (!strcmp(argv[i], "--help")) { usage(); return 0; }
        else { usage(); return 2; }
    }

    if (seed == 0) {
        struct timespec ts;
        if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
            fprintf(stderr, "tetris: clock_gettime failed\n");
            return 1;
        }
        seed = (uint32_t)ts.tv_sec ^ (uint32_t)ts.tv_nsec;
        if (seed == 0) seed = 1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    build_shapes();
    if (open_frame() != 0) return 1;

    if (ek_open(flags) != 0) {
        fprintf(stderr, "tetris: the screen would not open\n");
        ek_restore_ui();
        return 1;
    }
    wfm_clean = ek_waveform_by_name("GC16");
    if (!wfm_clean) {
        fprintf(stderr, "tetris: GC16 waveform is missing\n");
        ek_close();
        return 1;
    }
    if (wfm_motion == -1) {            /* --tuned asked for DU by name */
        wfm_motion = ek_waveform_by_name("DU");
        if (!wfm_motion) {
            fprintf(stderr, "tetris: DU waveform is missing\n");
            ek_close();
            return 1;
        }
    }
    ek_mono(1);
    ek_wait_updates(wait);
    if (force_clean) {
        ek_waveform(wfm_clean);
        ek_ghost(0, 0);
        ek_policy(180, 4, 30);
    } else {
        /* The engine tiles the panel itself, so most frames are a handful of
         * 8x8 dirty tiles: let it pick DU / GL16_FAST and clean the area that
         * keeps re-inking every 30 flushes. */
        ek_waveform(wfm_motion);   /* 0 = AUTO, or the tuned waveform */
        ek_ghost(6, 30);
        ek_policy(60, 4, 30);
    }

    int rc = 0;
    if (do_test) {
        rc = selftest() ? 1 : 0;
        reset_game(seed);
        present();
    } else {
        reset_game(seed);
        for (int i = 0; i < auto_pieces && !over && !stop_req; i++) bot_move();
        present();
    }

    if (menu_shot) {
        kframe_show_menu();
        kframe_paint();
        ek_flush();
        if (ek_dump_pgm(menu_shot, 1) != 0) {
            fprintf(stderr, "tetris: could not write %s\n", menu_shot);
            ek_close();
            return 1;
        }
        ek_close();
        return 0;
    }

#ifdef EK_HOST
    /* A pipe of "tap X Y" lines plays the game, then --dump saves the picture. */
    if (!do_test && auto_pieces < 0 && !isatty(0)) {
        char buf[64];
        while (running && !stop_req && fgets(buf, sizeof buf, stdin)) {
            int x, y;
            if (buf[0] == 'q') break;
            if (sscanf(buf, "tap %d %d", &x, &y) != 2) {
                fprintf(stderr, "tetris: want 'tap X Y' or q\n");
                ek_close();
                return 2;
            }
            tap_down(x, y);
            present();
        }
    }
#else
    drop_at = ek_ms() + (uint32_t)drop_ms();
    while (running && !stop_req) {
        ek_event ev;
        int got = 0;

        while (ek_poll(&ev)) {
            if (ev.type == EK_DOWN) {
                tap_down(ev.x, ev.y);
                got = 1;
            } else if (ev.type == EK_UP) {
                int i = hit(ev.x, ev.y);
                if (i >= 0) {
                    draw_button(i, 0);
                    got = 1;
                }
            }
        }
        if (kframe_covering()) {
            /* The menu is up. Push the next drop out so closing the menu
             * does not dump every drop the clock missed. */
            drop_at = ek_ms() + (uint32_t)drop_ms();
        } else if (!over) {
            uint32_t now = ek_ms();
            if ((int32_t)(now - drop_at) >= 0) {
                if (probe) fprintf(stderr, "probe: drop at %u (due %u, ms %d) dir=%d\n",
                                   (unsigned)now, (unsigned)drop_at, drop_ms(),
                                   (int)(now - drop_at));
                step_down();
                /* Anchor the beat to the old deadline, not to `now`. present()
                 * below blocks for the panel's real refresh time (~250-330 ms,
                 * more on a clean), so `now + drop_ms()` let every knob of
                 * that variability push the next deadline: drops drifted late
                 * whenever a refresh ran long, and the piece could stutter
                 * after a GC16 clean. Advancing the *deadline* keeps the drops
                 * exactly drop_ms() apart in wall time, and since the step is
                 * drawn by the present() that follows it (which waits for
                 * completion), the piece moves once per settled refresh -- not
                 * on a timer racing the panel. */
                drop_at += (uint32_t)drop_ms();
                got = 1;
            }
        }
        if (got) present();
        else ek_sleep_ms(15);
    }
#endif

    if (dump_path) {
        if (ek_dump_pgm(dump_path, 1) != 0) {
            fprintf(stderr, "tetris: could not write %s\n", dump_path);
            ek_close();
            return 1;
        }
    }

    ek_close();
    return rc;
}
