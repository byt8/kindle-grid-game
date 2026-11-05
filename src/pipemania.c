#define _GNU_SOURCE
/* pipemania.c -- Pipemania on the Kindle Touch: 600x800, one bit, taps only.
 *
 * The puzzle: a pipeline runs from the IN spout on one edge of the grid to the
 * OUT drain on the other. Every pipe in it is already in the right cell but
 * most of them have been turned to a wrong angle. Tap a pipe to turn it a
 * quarter turn clockwise. When the whole run looks right, press RUN THE WATER:
 * the water fills the pipe it can reach and spills out of the first open end.
 * Reaching the drain with no spill clears the level.
 *
 * Why it is built this way:
 *
 *   1. It is a rotation puzzle, not a dispenser puzzle. The board is complete
 *      from the first frame, so a tap changes exactly one cell -- one small
 *      rect on the wire, ~100 ms on the glass -- and nothing has to be drawn
 *      from a queue. A dispenser would need a strip of pieces and a placement
 *      rule, and would still spend the same pixels per tap.
 *   2. The level is generated, never drawn by hand: a randomised depth-first
 *      walk lays a self-avoiding path from the spout to the drain through the
 *      free cells. The pieces written on that path are the multiset the
 *      player needs, so a solution always exists -- --selftest proves it for
 *      every seed it is given instead of trusting the generator.
 *   3. Water is a flood, not a walk: it splits at a junction, so every open
 *      arm is a real part of the picture and a stray arm is a real spill.
 *
 * The shared Kindle menu (hamburger, top right) carries NEW, LEVEL and HELP
 * plus VOLUME / WIFI / APPS / QUIT. NEW throws the level away, LEVEL picks
 * the level a new game starts on, and the WATER tab is this game's own.
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
#define ROWS 7
#define CELL 54

#define GRID_X 30
#define GRID_Y 92
#define GRID_W (COLS * CELL)   /* 540, so the grid ends at x=570 */
#define GRID_H (ROWS * CELL)   /* 378, so the grid ends at y=470 */

#define HEAD_Y   6
#define LINE_Y   44
#define HINT_Y   64
#define STAT_Y1  496
#define STAT_Y2  520

#define GO_X 24
#define GO_Y 640
#define GO_W 552
#define GO_H 76

/* ---- directions --------------------------------------------------------- */

enum { DIR_N = 0, DIR_E = 1, DIR_S = 2, DIR_W = 3 };

static const int DR[4] = { -1, 0, 1, 0 };
static const int DC[4] = { 0, 1, 0, -1 };

static int opp(int d) { return (d + 2) & 3; }
static unsigned bit(int d) { return 1u << d; }
/* Clockwise quarter turn: N->E->S->W->N. */
static unsigned char rot90(unsigned char m) {
    return (unsigned char)(((m << 1) | (m >> 3)) & 0xF);
}

/* ---- board -------------------------------------------------------------- */

enum { K_EMPTY = 0, K_PIPE, K_WALL, K_SRC, K_DRAIN };

static unsigned char kind[ROWS][COLS];
static unsigned char mask[ROWS][COLS];     /* arms, bit d = opens to direction d */
static unsigned char wetm[ROWS][COLS];     /* 1 = the water got here */

static int src_r, src_c, in_dir;           /* the spout and the way it pushes */
static int drain_r, drain_c;

static int wet_drain;                      /* water arrived at the drain */
static int leaks;                          /* open ends the water fell out of */
static int flowing;                        /* the last RUN THE WATER is on screen */

/* The solution, kept so --selftest can prove every generated level has one. */
#define MAXPATH (ROWS * COLS)
static signed char sol_r[MAXPATH], sol_c[MAXPATH];
static unsigned char sol_m[MAXPATH];
static int sol_n;

static int level, score, turns;
static int card;                           /* the level-cleared card is up */
static int running = 1;
static volatile int stop_req;

static uint32_t base_seed;
static int stats_on;

static int failures;
static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "pipemania: FAIL %s\n", msg);
        failures++;
    }
}

/* ---- random ------------------------------------------------------------- */

static uint32_t rng_state = 1;

static void rng_seed(uint32_t s) { rng_state = s ? s : 0x9e3779b9u; }

static uint32_t rnd(void) {
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

/* ---- the level ---------------------------------------------------------- */

static int flood(int *wet_n);   /* defined under water; the generator asks it */

static int pr[MAXPATH], pc[MAXPATH], pn;   /* the walked path, drain included */
static unsigned char vis[ROWS][COLS];

/* Randomised depth-first walk from the cell after the spout to the drain.
 * Only free cells and the drain itself may be entered, and a cell is visited
 * once, so the path never crosses itself -- which is what makes the pieces on
 * it a chain with exactly one way through. */
static int path_dfs(int r, int c, int len) {
    if (len > MAXPATH) return 0;
    vis[r][c] = 1;
    pr[pn] = r;
    pc[pn] = c;
    pn++;
    if (r == drain_r && c == drain_c) return 1;

    int d[4] = { DIR_N, DIR_E, DIR_S, DIR_W };
    for (int i = 3; i > 0; i--) {
        int j = (int)(rnd() % (uint32_t)(i + 1));
        int t = d[i];
        d[i] = d[j];
        d[j] = t;
    }
    for (int i = 0; i < 4; i++) {
        int nr = r + DR[d[i]], nc = c + DC[d[i]];
        if (nr < 0 || nr >= ROWS || nc < 0 || nc >= COLS) continue;
        if (vis[nr][nc]) continue;
        if (kind[nr][nc] != K_EMPTY && !(nr == drain_r && nc == drain_c)) continue;
        if (path_dfs(nr, nc, len + 1)) return 1;
    }
    pn--;
    vis[r][c] = 0;
    return 0;
}

static int gen_path(void) {
    memset(vis, 0, sizeof vis);
    pn = 0;
    int r0 = src_r + DR[in_dir], c0 = src_c + DC[in_dir];
    if (r0 < 0 || r0 >= ROWS || c0 < 0 || c0 >= COLS) return 0;
    if (kind[r0][c0] != K_EMPTY) return 0;
    if (!path_dfs(r0, c0, 1)) return 0;
    return pn >= 2;          /* at least one pipe, then the drain */
}

static int dir_between(int r1, int c1, int r2, int c2) {
    if (r2 == r1 - 1 && c2 == c1) return DIR_N;
    if (r2 == r1 + 1 && c2 == c1) return DIR_S;
    if (r2 == r1 && c2 == c1 + 1) return DIR_E;
    if (r2 == r1 && c2 == c1 - 1) return DIR_W;
    fprintf(stderr, "pipemania: %d,%d and %d,%d are not neighbours\n", r1, c1, r2, c2);
    exit(1);
}

static void clear_grid(void) {
    memset(kind, 0, sizeof kind);
    memset(mask, 0, sizeof mask);
    memset(wetm, 0, sizeof wetm);
    wet_drain = 0;
    leaks = 0;
    flowing = 0;
    sol_n = 0;
}

static void place_spout_and_drain(void) {
    int side = (int)(rnd() % 4);
    switch (side) {
    case 0:
        src_r = (int)(rnd() % ROWS); src_c = 0; in_dir = DIR_E;
        drain_r = (int)(rnd() % ROWS); drain_c = COLS - 1;
        break;
    case 1:
        src_r = (int)(rnd() % ROWS); src_c = COLS - 1; in_dir = DIR_W;
        drain_r = (int)(rnd() % ROWS); drain_c = 0;
        break;
    case 2:
        src_r = 0; src_c = (int)(rnd() % COLS); in_dir = DIR_S;
        drain_r = ROWS - 1; drain_c = (int)(rnd() % COLS);
        break;
    default:
        src_r = ROWS - 1; src_c = (int)(rnd() % COLS); in_dir = DIR_N;
        drain_r = 0; drain_c = (int)(rnd() % COLS);
        break;
    }
    kind[src_r][src_c] = K_SRC;
    kind[drain_r][drain_c] = K_DRAIN;
}

/* Lay one level. Retries until the walk finds a path; if it cannot, it says so
 * and stops. It does not quietly hand back a half-built board. */
static void gen_level(int lvl, uint32_t seed) {
    rng_seed(seed ^ ((uint32_t)lvl * 0x9e3779b9u));
    int attempt;
    for (attempt = 0; attempt < 200; attempt++) {
        clear_grid();
        place_spout_and_drain();
        if (src_r == drain_r && src_c == drain_c) continue;

        int nw = lvl - 1;
        if (nw > 6) nw = 6;
        for (int i = 0; i < nw; i++) {
            int r = (int)(rnd() % ROWS), c = (int)(rnd() % COLS);
            if (kind[r][c] != K_EMPTY) continue;
            kind[r][c] = K_WALL;
        }
        if (gen_path()) break;
    }
    if (attempt == 200) {
        fprintf(stderr, "pipemania: no path through this board for level %d; "
                        "refusing to start a level that cannot be solved\n", lvl);
        exit(1);
    }

    int np = pn - 1;               /* the last entry in the walk is the drain */
    sol_n = np;
    for (int i = 0; i < np; i++) {
        int r = pr[i], c = pc[i];
        int entry;
        if (i == 0) entry = opp(in_dir);
        else entry = opp(dir_between(pr[i - 1], pc[i - 1], r, c));
        int out;
        if (i + 1 < np) out = dir_between(r, c, pr[i + 1], pc[i + 1]);
        else out = dir_between(r, c, drain_r, drain_c);
        unsigned char m = (unsigned char)(bit(entry) | bit(out));
        sol_r[i] = (signed char)r;
        sol_c[i] = (signed char)c;
        sol_m[i] = m;
        kind[r][c] = K_PIPE;
        mask[r][c] = m;
    }

    /* Scramble: every pipe turned to a random angle, with at least one wrong
     * so no level opens already solved. */
    int wrong = 0;
    for (int i = 0; i < np; i++) {
        int k = (int)(rnd() % 4);
        unsigned char m = mask[sol_r[i]][sol_c[i]];
        for (int t = 0; t < k; t++) m = rot90(m);
        mask[sol_r[i]][sol_c[i]] = m;
        if (m != sol_m[i]) wrong++;
    }
    if (wrong == 0 && np > 0) {
        int r = sol_r[0], c = sol_c[0];
        mask[r][c] = rot90(mask[r][c]);
        if (mask[r][c] == sol_m[0]) mask[r][c] = rot90(mask[r][c]);
    }

    /* A board that already runs would be cleared by pressing RUN THE WATER
     * without turning anything. Water can only enter the first pipe through
     * the arm facing the spout, so turning that pipe once always stops it --
     * unless the turn happens to land on the solved angle, which the loop
     * walks past. If it cannot be broken the level is broken: say so. */
    int wet_n = 0;
    for (int guard = 0; flood(&wet_n); guard++) {
        if (guard >= 4) {
            fprintf(stderr, "pipemania: level %d opens already running and cannot "
                            "be scrambled; refusing it\n", lvl);
            exit(1);
        }
        mask[sol_r[0]][sol_c[0]] = rot90(mask[sol_r[0]][sol_c[0]]);
    }
}

/* ---- water -------------------------------------------------------------- */

typedef struct { int r, c, edge; } wstate;
static wstate wq[ROWS * COLS * 4];
static unsigned char wseen[ROWS * COLS];

/* Flood from the spout. Water enters a pipe through the edge it came from and
 * leaves by every other arm; an arm that meets a wall, the grid edge or a pipe
 * that is not open to it is a spill. Returns 1 only when the drain is reached
 * and nothing leaked. */
static int flood(int *wet_n) {
    memset(wetm, 0, sizeof wetm);
    memset(wseen, 0, sizeof wseen);
    wet_drain = 0;
    leaks = 0;

    wetm[src_r][src_c] = 1;

    int r0 = src_r + DR[in_dir], c0 = src_c + DC[in_dir];
    int sp = 0;
    if (r0 < 0 || r0 >= ROWS || c0 < 0 || c0 >= COLS) {
        leaks++;
    } else if (r0 == drain_r && c0 == drain_c) {
        wet_drain = 1;   /* the spout points straight into the drain */
    } else {
        int e = opp(in_dir);
        wq[sp].r = r0; wq[sp].c = c0; wq[sp].edge = e;
        sp++;
        wseen[r0 * COLS + c0] |= (unsigned char)bit(e);
    }

    while (sp > 0) {
        wstate w = wq[--sp];
        int r = w.r, c = w.c;
        if (kind[r][c] != K_PIPE || !(mask[r][c] & bit(w.edge))) {
            leaks++;
            continue;
        }
        wetm[r][c] = 1;
        for (int d = 0; d < 4; d++) {
            if (d == w.edge) continue;
            if (!(mask[r][c] & bit(d))) continue;
            int nr = r + DR[d], nc = c + DC[d];
            if (nr < 0 || nr >= ROWS || nc < 0 || nc >= COLS) { leaks++; continue; }
            if (nr == drain_r && nc == drain_c) { wet_drain = 1; continue; }
            if (kind[nr][nc] != K_PIPE) { leaks++; continue; }
            int e2 = opp(d);
            if (!(mask[nr][nc] & bit(e2))) { leaks++; continue; }
            if (wseen[nr * COLS + nc] & (unsigned char)bit(e2)) continue;
            wseen[nr * COLS + nc] |= (unsigned char)bit(e2);
            if (sp >= (int)(sizeof wq / sizeof wq[0])) {
                fprintf(stderr, "pipemania: the water queue overflowed\n");
                exit(1);
            }
            wq[sp].r = nr; wq[sp].c = nc; wq[sp].edge = e2;
            sp++;
        }
    }

    int n = 0;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (wetm[r][c]) n++;
    if (wet_n) *wet_n = n;
    return wet_drain && leaks == 0;
}

static void run_water(void) {
    int wet_n = 0;
    int win = flood(&wet_n);
    flowing = 1;
    if (win) {
        score += 100;
        card = 1;
        fprintf(stderr, "pipemania: level %d complete, %d wet cells, score %d\n",
                level, wet_n, score);
    } else {
        fprintf(stderr, "pipemania: the water spilled, %d leak(s), %d wet cell(s), "
                        "drain reached %d\n", leaks, wet_n, wet_drain);
    }
}

static void next_level(int lvl) {
    level = lvl;
    turns = 0;
    card = 0;
    gen_level(level, base_seed + (uint32_t)level * 7919u);
}

/* ---- drawing ------------------------------------------------------------ */

static void text_line(int x, int y, const char *s, int w) {
    ek_fill_rect(x, y, w, 16, EK_PAPER);
    ek_text(x, y, s, EK_INK);
}

static void draw_pipe(int r, int c) {
    const int x = GRID_X + c * CELL, y = GRID_Y + r * CELL;
    const int cx = x + CELL / 2, cy = y + CELL / 2;
    const unsigned char m = mask[r][c];
    const int t = wetm[r][c] ? 18 : 10;   /* a wet pipe is drawn fat */

    if (m & bit(DIR_N)) ek_fill_rect(cx - t / 2, y, t, CELL / 2, EK_INK);
    if (m & bit(DIR_E)) ek_fill_rect(cx, cy - t / 2, CELL / 2, t, EK_INK);
    if (m & bit(DIR_S)) ek_fill_rect(cx - t / 2, cy, t, CELL / 2, EK_INK);
    if (m & bit(DIR_W)) ek_fill_rect(x, cy - t / 2, CELL / 2, t, EK_INK);
    ek_fill_rect(cx - t / 2, cy - t / 2, t, t, EK_INK);
}

static void draw_cell(int r, int c) {
    const int x = GRID_X + c * CELL, y = GRID_Y + r * CELL;

    ek_fill_rect(x, y, CELL, CELL, EK_PAPER);

    switch (kind[r][c]) {
    case K_WALL:
        ek_dither_rect(x + 2, y + 2, CELL - 4, CELL - 4, 6);
        ek_rect(x + 2, y + 2, CELL - 4, CELL - 4, EK_INK);
        break;
    case K_SRC: {
        const int t = 18;
        const int cx = x + CELL / 2, cy = y + CELL / 2;
        if (in_dir == DIR_E) ek_fill_rect(cx, cy - t / 2, CELL / 2, t, EK_INK);
        else if (in_dir == DIR_W) ek_fill_rect(x, cy - t / 2, CELL / 2, t, EK_INK);
        else if (in_dir == DIR_N) ek_fill_rect(cx - t / 2, y, t, CELL / 2, EK_INK);
        else ek_fill_rect(cx - t / 2, cy, t, CELL / 2, EK_INK);
        ek_fill_rect(cx - t / 2, cy - t / 2, t, t, EK_INK);
        ek_rect(x + 1, y + 1, CELL - 2, CELL - 2, EK_INK);
        if (in_dir == DIR_N) text_line(x + 4, y + CELL - 20, "IN", 16);
        else text_line(x + 4, y + 4, "IN", 16);
        break;
    }
    case K_DRAIN: {
        ek_rect(x + 8, y + 8, CELL - 16, CELL - 16, EK_INK);
        ek_rect(x + 9, y + 9, CELL - 18, CELL - 18, EK_INK);
        if (wet_drain) ek_fill_rect(x + 16, y + 16, CELL - 32, CELL - 32, EK_INK);
        text_line(x + CELL - 28, y + CELL - 20, "OUT", 24);
        break;
    }
    case K_PIPE:
        draw_pipe(r, c);
        break;
    default:
        break;
    }

    /* One corner tick per cell, so an empty board is still countable. */
    ek_fill_rect(x + CELL - 3, y + CELL - 3, 2, 2, EK_INK);
}

static void draw_grid(void) {
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            draw_cell(r, c);
    ek_rect(GRID_X - 3, GRID_Y - 3, GRID_W + 6, GRID_H + 6, EK_INK);
}

static void draw_go_button(void) {
    ek_fill_rect(GO_X, GO_Y, GO_W, GO_H, EK_PAPER);
    ek_rect(GO_X, GO_Y, GO_W, GO_H, EK_INK);
    ek_rect(GO_X + 1, GO_Y + 1, GO_W - 2, GO_H - 2, EK_INK);
    ek_text_scale(GO_X + (GO_W - 14 * 16) / 2, GO_Y + (GO_H - 32) / 2,
                  "RUN THE WATER", EK_INK, 2);
}

static void draw_chrome(void) {
    text_line(14, HEAD_Y, "PIPEMANIA", 9 * 16);
    ek_text_scale(14, HEAD_Y, "PIPEMANIA", EK_INK, 2);

    text_line(14, HINT_Y, "tap a pipe to turn it a quarter turn", 42 * 8);
    text_line(14, HINT_Y + 18, "then RUN THE WATER to fill it", 34 * 8);
}

static void draw_stats(void) {
    char line[64];
    snprintf(line, sizeof line, "SCORE %d   LEVEL %d   TURNS %d", score, level, turns);
    text_line(14, LINE_Y, line, 40 * 8);

    const char *msg;
    if (card) msg = "the water reached the drain";
    else if (!flowing) msg = "no water in the pipes yet";
    else if (wet_drain && leaks == 0) msg = "clean run, drain reached";
    else if (leaks > 0) msg = "SPILL: an open end is leaking";
    else msg = "the water stopped short of the drain";

    text_line(30, STAT_Y1, msg, 46 * 8);

    char num[48];
    snprintf(num, sizeof num, "spills %d", leaks);
    text_line(30, STAT_Y2, num, 20 * 8);
}

static void draw_card(void) {
    const int w = 380, h = 210;
    const int x = GRID_X + (GRID_W - w) / 2;
    const int y = GRID_Y + (GRID_H - h) / 2;
    char buf[48];

    ek_fill_rect(x, y, w, h, EK_PAPER);
    ek_rect(x, y, w, h, EK_INK);
    ek_rect(x + 1, y + 1, w - 2, h - 2, EK_INK);

    ek_text_scale(x + 30, y + 28, "PIPELINE DONE", EK_INK, 2);
    snprintf(buf, sizeof buf, "LEVEL %d CLEAR, SCORE %d", level, score);
    ek_text(x + 30, y + 82, buf, EK_INK);
    snprintf(buf, sizeof buf, "tap for level %d", level + 1);
    ek_text_scale(x + 30, y + 124, buf, EK_INK, 2);
}

static void present(void) {
    if (kframe_covering()) {
        kframe_paint();
        ek_flush();
        return;
    }

    ek_clear(EK_PAPER);
    draw_chrome();
    draw_grid();
    draw_stats();
    draw_go_button();
    if (card) draw_card();

    kframe_paint();
    ek_stats st = ek_flush();
    if (stats_on)
        fprintf(stderr, "pipemania: flush wfm=%d rects=%d tiles=%d px=%ld full=%d us=%u\n",
                st.wfm, st.rects, st.tiles, st.px, st.full, (unsigned)st.us);
}

/* ---- input -------------------------------------------------------------- */

static int inside(int x, int y, int bx, int by, int bw, int bh) {
    return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

/* Turning a pipe throws the last flood away: what the pipes held is no longer
 * what they are. */
static void drop_water(void) {
    if (!flowing) return;
    flowing = 0;
    memset(wetm, 0, sizeof wetm);
    wet_drain = 0;
    leaks = 0;
}

static void tap_down(int x, int y) {
    if (kframe_tap(x, y)) return;

    if (card) {
        next_level(level + 1);
        return;
    }
    if (inside(x, y, GO_X, GO_Y, GO_W, GO_H)) {
        run_water();
        return;
    }
    if (!inside(x, y, GRID_X, GRID_Y, GRID_W, GRID_H)) return;

    int c = (x - GRID_X) / CELL;
    int r = (y - GRID_Y) / CELL;
    if (r < 0 || r >= ROWS || c < 0 || c >= COLS) return;
    if (kind[r][c] != K_PIPE) {
        fprintf(stderr, "pipemania: %d,%d is not a pipe\n", r, c);
        return;
    }
    mask[r][c] = rot90(mask[r][c]);
    turns++;
    drop_water();
#ifdef EK_HOST
    /* On the glass every tap would land in the log; in a test run the line is
     * what proves the tap reached a pipe instead of a wall. */
    fprintf(stderr, "pipemania: turned %d,%d, turns %d\n", r, c, turns);
#endif
}

/* ---- the shared menu ---------------------------------------------------- */

enum { PIPE_NEW = 1, PIPE_LEVEL, PIPE_HELP };

#define LEVEL_Y0 140
#define N_START_LEVELS 6

static int start_level = 1;

static void draw_new(const kshell_rect *r) {
    char line[64];
    snprintf(line, sizeof line, "score %d, on level %d, %d turns", score, level, turns);
    ek_text_scale(r->x, r->y, "NEW PIPELINE", EK_INK, 2);
    ek_text(r->x, r->y + 44, "throws this board away and lays a fresh one", EK_INK);
    ek_text(r->x, r->y + 66, line, EK_INK);
    kshell_button(KFRAME_ACT_X, KFRAME_ACT_Y, KFRAME_ACT_W, KFRAME_ACT_H, "START", 0);
}

static int tap_new(const kshell_rect *r, int x, int y) {
    (void)r;
    if (!kshell_inside(x, y, KFRAME_ACT_X, KFRAME_ACT_Y, KFRAME_ACT_W, KFRAME_ACT_H))
        return 0;
    score = 0;
    next_level(start_level);
    kframe_close();
    return 1;
}

static void draw_level(const kshell_rect *r) {
    static const char *labels[N_START_LEVELS] = {
        "LEVEL 1", "LEVEL 2", "LEVEL 3", "LEVEL 4", "LEVEL 5", "LEVEL 6",
    };
    ek_text_scale(r->x, r->y, "STARTING LEVEL", EK_INK, 2);
    ek_text(r->x, r->y + 44, "only NEW uses it; more levels, more rock", EK_INK);
    kshell_options(LEVEL_Y0, labels, N_START_LEVELS, start_level - 1);
}

static int tap_level(const kshell_rect *r, int x, int y) {
    (void)r;
    int i = kshell_option_at(LEVEL_Y0, N_START_LEVELS, x, y);
    if (i < 0) return 0;
    start_level = i + 1;
    kframe_close();
    return 1;
}

static void draw_help(const kshell_rect *r) {
    ek_text_scale(r->x, r->y, "HOW TO PLAY", EK_INK, 2);
    ek_text(r->x, r->y + 48, "water comes in at IN and must reach OUT.", EK_INK);
    ek_text(r->x, r->y + 70, "tap any pipe: it turns a quarter turn.", EK_INK);
    ek_text(r->x, r->y + 92, "RUN THE WATER floods them. it splits at a", EK_INK);
    ek_text(r->x, r->y + 114, "junction, so one open arm spills the lot.", EK_INK);
    ek_text(r->x, r->y + 136, "reaching OUT with no spill clears the level.", EK_INK);
    ek_text(r->x, r->y + 174, "APP_NAME and the arrows above: where to go next.", EK_INK);
    ek_text(r->x, r->y + 210, "QUIT twice gives the reader back until boot.", EK_INK);
}

static int tap_help(const kshell_rect *r, int x, int y) {
    (void)r;
    (void)x;
    (void)y;
    return 0;   /* nothing to press; the tap misses and the tab stays */
}

static int on_pipes_item(int id) {
    fprintf(stderr, "pipemania: menu item %d has no action of its own\n", id);
    return 1;
}

static const kframe_item PIPE_ITEMS[] = {
    { "NEW",   PIPE_NEW,   draw_new,   tap_new },
    { "LEVEL", PIPE_LEVEL, draw_level, tap_level },
    { "HELP",  PIPE_HELP,  draw_help,  tap_help },
};

static void kf_status(const char *msg) { fprintf(stderr, "pipemania: %s\n", msg); }
static void kf_quit(void) { running = 0; stop_req = 1; }

static int open_frame(void) {
    kframe_cfg kf;
    memset(&kf, 0, sizeof kf);
    kf.title = "PIPEMANIA";
    kf.app_id = "pipes";
    kf.items = PIPE_ITEMS;
    kf.nitems = (int)(sizeof PIPE_ITEMS / sizeof PIPE_ITEMS[0]);
    kf.on_item = on_pipes_item;
    kf.on_quit = kf_quit;
    kf.repaint = present;
    kf.status = kf_status;
    kf.stop = &stop_req;
    if (kframe_init(&kf) != 0) {
        fprintf(stderr, "pipemania: the shared menu would not start\n");
        return -1;
    }
    return 0;
}

/* ---- self-test ---------------------------------------------------------- */

static int selftest(void) {
    for (unsigned m = 1; m < 16; m++) {
        unsigned r = m;
        for (int i = 0; i < 4; i++) r = rot90((unsigned char)r);
        expect(r == m, "four turns put a piece back where it started");

        int arms = 0;
        for (int d = 0; d < 4; d++) if (m & bit(d)) arms++;
        if (arms == 2) expect(rot90((unsigned char)m) != m, "one turn changes a two-arm pipe");
    }

    int solved = 0;
    for (uint32_t seed = 1; seed <= 8; seed++) {
        for (int lvl = 1; lvl <= 6; lvl++) {
            gen_level(lvl, seed);

            int wrong = 0;
            for (int i = 0; i < sol_n; i++)
                if (mask[sol_r[i]][sol_c[i]] != sol_m[i]) wrong++;
            expect(wrong > 0, "the level opens scrambled");

            int wet_n = 0;
            for (int i = 0; i < sol_n; i++)
                mask[sol_r[i]][sol_c[i]] = sol_m[i];
            int win = flood(&wet_n);
            int wp = 0;
            for (int r = 0; r < ROWS; r++)
                for (int c = 0; c < COLS; c++)
                    if (wetm[r][c] && kind[r][c] == K_PIPE) wp++;
            expect(win == 1, "the solution reaches the drain with no spill");
            expect(leaks == 0, "the solution leaks nothing");
            expect(wp == sol_n, "the water fills every pipe the walk laid");
            /* wet_n counts the pipes plus the spout cell the water sits in. */
            expect(wet_n == sol_n + 1, "the water wets exactly the path");

            /* The ends of the run are the only pipes whose turn is certain to
             * stop the water: the first is the only way in from the spout and
             * the last is the only way out to the drain. A pipe in the middle
             * can be turned and still find a way through its other arms. */
            for (int end = 0; end < 2; end++) {
                int i = end ? sol_n - 1 : 0;
                int r = sol_r[i], c = sol_c[i];
                unsigned char keep = mask[r][c];
                mask[r][c] = rot90(keep);
                expect(flood(&wet_n) == 0,
                       end ? "a turned drain pipe stops the water"
                           : "a turned spout pipe stops the water");
                mask[r][c] = keep;
            }
            expect(flood(&wet_n) == 1, "turning them back lets it through");
            solved++;
        }
    }
    fprintf(stderr, "pipemania: %d levels generated and solved\n", solved);
    return failures;
}

/* ---- main --------------------------------------------------------------- */

static void usage(void) {
    fprintf(stderr,
            "usage: pipes [options]\n"
            "\n"
            "  Tap a pipe to turn it a quarter turn. RUN THE WATER floods the\n"
            "  pipes; reaching OUT with no spill clears the level.\n"
            "  The top-right button opens the menu: NEW, LEVEL, HELP, and\n"
            "  VOLUME / WIFI / APPS / QUIT on the right.\n"
            "  --selftest    check the rules on this computer and exit\n"
            "  --dump FILE   write the picture (PGM) and exit\n"
            "  --menu-shot F write the menu (PGM) and exit\n"
            "  --seed N      same seed, same boards\n"
            "  --level N     start on level N (1..99)\n"
            "  --solve       put every pipe back the way the walk laid it\n"
            "  --turn N      turn the first N pipes once (row by row)\n"
            "  --go          run the water before the first picture\n"
            "  --stats       print what each screen update cost\n");
}

static void on_signal(int sig) {
    (void)sig;
    stop_req = 1;
}

int main(int argc, char **argv) {
    unsigned flags = EK_TAKEOVER;
    int wait = 1;
    int do_test = 0;
    int do_solve = 0, do_go = 0, turn_n = 0, start_at = 1;
    const char *dump_path = NULL;
    const char *menu_shot = NULL;
    uint32_t seed = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            char *end = NULL;
            long v = strtol(argv[++i], &end, 10);
            if (!end || *end || v <= 0 || v > 0x7fffffff) { usage(); return 2; }
            seed = (uint32_t)v;
        } else if (!strcmp(argv[i], "--level") && i + 1 < argc) {
            char *end = NULL;
            long v = strtol(argv[++i], &end, 10);
            if (!end || *end || v < 1 || v > 99) { usage(); return 2; }
            start_at = (int)v;
        } else if (!strcmp(argv[i], "--turn") && i + 1 < argc) {
            char *end = NULL;
            long v = strtol(argv[++i], &end, 10);
            if (!end || *end || v < 0 || v > 200) { usage(); return 2; }
            turn_n = (int)v;
        } else if (!strcmp(argv[i], "--dump") && i + 1 < argc) {
            dump_path = argv[++i];
            if (!dump_path[0]) { usage(); return 2; }
        } else if (!strcmp(argv[i], "--menu-shot") && i + 1 < argc) {
            menu_shot = argv[++i];
            if (!menu_shot[0]) { usage(); return 2; }
        } else if (!strcmp(argv[i], "--selftest")) do_test = 1;
        else if (!strcmp(argv[i], "--solve")) do_solve = 1;
        else if (!strcmp(argv[i], "--go")) do_go = 1;
        else if (!strcmp(argv[i], "--stats")) stats_on = 1;
        else if (!strcmp(argv[i], "--no-takeover")) flags &= ~EK_TAKEOVER;
        else if (!strcmp(argv[i], "--no-wait")) wait = 0;
        else if (!strcmp(argv[i], "--help")) { usage(); return 0; }
        else { usage(); return 2; }
    }

    if (seed == 0) {
        struct timespec ts;
        if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
            fprintf(stderr, "pipemania: clock_gettime failed\n");
            return 1;
        }
        seed = (uint32_t)ts.tv_sec ^ (uint32_t)ts.tv_nsec;
        if (seed == 0) seed = 1;
    }
    base_seed = seed;

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    if (open_frame() != 0) return 1;

    if (ek_open(flags) != 0) {
        fprintf(stderr, "pipemania: the screen would not open\n");
        ek_restore_ui();
        return 1;
    }
    ek_mono(1);
    ek_wait_updates(wait);
    ek_waveform(0);
    ek_ghost(6, 30);
    ek_policy(60, 4, 30);

    int rc = 0;
    score = 0;
    turns = 0;
    next_level(start_at);
    start_level = start_at;

    if (do_test) rc = selftest() ? 1 : 0;

    if (do_solve)
        for (int i = 0; i < sol_n; i++) mask[sol_r[i]][sol_c[i]] = sol_m[i];

    for (int k = 0; k < turn_n; k++) {
        int done = 0;
        for (int r = 0; r < ROWS && !done; r++)
            for (int c = 0; c < COLS && !done; c++)
                if (kind[r][c] == K_PIPE) {
                    mask[r][c] = rot90(mask[r][c]);
                    turns++;
                    done = 1;
                }
    }
    if (do_go) run_water();

    present();

    if (menu_shot) {
        kframe_show_menu();
        kframe_paint();
        ek_flush();
        if (ek_dump_pgm(menu_shot, 1) != 0) {
            fprintf(stderr, "pipemania: could not write %s\n", menu_shot);
            ek_close();
            return 1;
        }
        ek_close();
        return 0;
    }

#ifdef EK_HOST
    if (!do_test && !isatty(0)) {
        char buf[64];
        while (running && !stop_req && fgets(buf, sizeof buf, stdin)) {
            int x, y;
            if (buf[0] == 'q') break;
            if (sscanf(buf, "tap %d %d", &x, &y) != 2) {
                fprintf(stderr, "pipemania: want 'tap X Y' or q\n");
                ek_close();
                return 2;
            }
            tap_down(x, y);
            present();
        }
    }
#else
    while (running && !stop_req) {
        ek_event ev;
        int got = 0;
        while (ek_poll(&ev)) {
            if (ev.type == EK_DOWN) {
                tap_down(ev.x, ev.y);
                got = 1;
            }
        }
        if (got) present();
        else ek_sleep_ms(20);
    }
#endif

    if (dump_path) {
        if (ek_dump_pgm(dump_path, 1) != 0) {
            fprintf(stderr, "pipemania: could not write %s\n", dump_path);
            ek_close();
            return 1;
        }
    }

    ek_close();
    return rc;
}
