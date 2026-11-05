#define _GNU_SOURCE
/* MINESWEEPER -- mines on a 600x800 e-ink panel, taps only.
 *
 * Three boards: 9x9 with 10 mines, 12x12 with 25, 16x16 with 60. The first tap
 * is never a mine, and neither are the eight squares around it.
 *
 * Controls:
 *   - tap a covered square                     opens it
 *   - FLAG button on, then tap a square        puts a flag there (or takes it off)
 *   - tap an open number with the right number
 *     of flags around it                      opens the rest of its neighbours
 *   - chips up top, or the LEVEL tab           start a fresh board at that size
 *
 * Anything wrong is reported, never worked around: a board that cannot hold its
 * mines inside the squares the first tap leaves free stops the game out loud.
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

#define MAXN 16

/* The playing field. Every board is centred in this box. */
#define GRID_X 30
#define GRID_Y 108
#define GRID_W 540

/* Size chips, the same control sudoku uses for its levels. */
#define CHIP_Y 58
#define CHIP_H 34
#define CHIP_W 104
#define CHIP_X(i) (30 + (i) * 112)

/* The two buttons along the bottom. */
#define BTN_Y 660
#define BTN_H 64
#define BTN_W 260
#define BTN_FLAG_X 30
#define BTN_NEW_X 310

/* The counters, right of the chips and clear of the hamburger at x=548. */
#define STAT_X 380

#define LVL_Y0 140   /* first row of the LEVEL chooser */

static const struct { int n, mines; const char *name; } LEVELS[3] = {
    {  9, 10, "EASY" },
    { 12, 25, "MED"  },
    { 16, 60, "HARD" },
};

static int level;              /* 0 EASY, 1 MED, 2 HARD */
static int gn;                 /* squares a side on the current board */
static int gcell;              /* pixels per square */
static int gx0, gy0;           /* where the board starts */

static unsigned char mine[MAXN][MAXN];
static unsigned char open_[MAXN][MAXN];
static unsigned char flag[MAXN][MAXN];
static unsigned char num[MAXN][MAXN];   /* mines around, 0..8 */

static int n_open;             /* squares opened */
static int n_flags;            /* flags placed */
static int started;            /* the first tap has happened; the mines are out */
static int dead;               /* a mine went off */
static int won;                /* every safe square is open */
static int boom_r = -1, boom_c = -1;
static int flag_mode;          /* taps place flags instead of opening */
static int drawn[MAXN][MAXN];  /* what the canvas shows per square, -1 unknown */
static int redraw_all;
static int running = 1;
static int quit_armed;
static int stats_on, force_clean, clean_next;
static int wfm_clean;
static int failures;

static uint32_t rng_state = 1;
static volatile sig_atomic_t stop_req;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "minesweeper: FAIL %s\n", msg);
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

/* ---- geometry ----------------------------------------------------------- */

static void lay_out(void) {
    gn = LEVELS[level].n;
    gcell = GRID_W / gn;                 /* 60, 45, 33 */
    if (gcell < 16) {
        fprintf(stderr, "minesweeper: a %dx%d board gives %d px squares, too small to tap\n",
                gn, gn, gcell);
        exit(1);
    }
    gx0 = GRID_X + (GRID_W - gn * gcell) / 2;
    gy0 = GRID_Y + (GRID_W - gn * gcell) / 2;
}

/* ---- the board ---------------------------------------------------------- */

/* Mines, then the numbers. The squares the first tap touches stay clear.
 * `need` is a parameter so the guard that refuses an impossible board can be
 * exercised directly instead of by rigging a level. */
static int place_mines(int sr, int sc, int need) {
    int r0 = sr > 0 ? sr - 1 : 0, r1 = sr < gn - 1 ? sr + 1 : gn - 1;
    int c0 = sc > 0 ? sc - 1 : 0, c1 = sc < gn - 1 ? sc + 1 : gn - 1;
    int keep = (r1 - r0 + 1) * (c1 - c0 + 1);

    memset(mine, 0, sizeof mine);
    if (need > gn * gn - keep) {
        fprintf(stderr,
                "minesweeper: %d mines was asked for but the first tap leaves only %d squares\n",
                need, gn * gn - keep);
        return -1;
    }

    int placed = 0;
    while (placed < need) {
        int r = (int)(rnd() % (uint32_t)gn);
        int c = (int)(rnd() % (uint32_t)gn);
        if (mine[r][c]) continue;
        if (r >= r0 && r <= r1 && c >= c0 && c <= c1) continue;
        mine[r][c] = 1;
        placed++;
    }
    return 0;
}

static int neighbours(int r, int c, int rr[], int cc[]) {
    int k = 0;
    for (int dr = -1; dr <= 1; dr++)
        for (int dc = -1; dc <= 1; dc++) {
            if (!dr && !dc) continue;
            int y = r + dr, x = c + dc;
            if (y < 0 || y >= gn || x < 0 || x >= gn) continue;
            rr[k] = y;
            cc[k] = x;
            k++;
        }
    return k;
}

static void compute_numbers(void) {
    int rr[8], cc[8];
    memset(num, 0, sizeof num);
    for (int r = 0; r < gn; r++)
        for (int c = 0; c < gn; c++) {
            if (mine[r][c]) { num[r][c] = 9; continue; }   /* 9 = a mine, never shown as a count */
            int k = neighbours(r, c, rr, cc), n = 0;
            for (int i = 0; i < k; i++)
                if (mine[rr[i]][cc[i]]) n++;
            num[r][c] = (unsigned char)n;
        }
}

static void new_game(void) {
    lay_out();
    memset(mine, 0, sizeof mine);
    memset(open_, 0, sizeof open_);
    memset(flag, 0, sizeof flag);
    memset(num, 0, sizeof num);
    n_open = n_flags = 0;
    started = dead = won = 0;
    boom_r = boom_c = -1;
    flag_mode = 0;
    quit_armed = 0;
    running = 1;
    for (int r = 0; r < MAXN; r++)
        for (int c = 0; c < MAXN; c++)
            drawn[r][c] = -1;
    redraw_all = 1;
}

/* The first tap: lay the mines out around it, then open it. The board does not
 * exist before this tap, so it always opens a square, FLAG on or not. */
static void open_square(int r, int c);

static void start_at(int r, int c) {
    if (place_mines(r, c, LEVELS[level].mines) != 0) {
        running = 0;
        return;
    }
    compute_numbers();
    started = 1;
    open_square(r, c);
}

/* Open one square and, if it is empty, everything it touches. */
static void open_square(int r, int c) {
    int sr[MAXN * MAXN], sc[MAXN * MAXN];
    int nq = 0;

    if (open_[r][c] || flag[r][c]) return;
    sr[nq] = r; sc[nq] = c; nq++;

    while (nq > 0) {
        nq--;
        int y = sr[nq], x = sc[nq];
        if (open_[y][x] || flag[y][x]) continue;
        open_[y][x] = 1;
        n_open++;
        if (mine[y][x]) {
            dead = 1;
            boom_r = y;
            boom_c = x;
            return;
        }
        if (num[y][x] != 0) continue;
        int rr[8], cc[8];
        int k = neighbours(y, x, rr, cc);
        for (int i = 0; i < k; i++) {
            if (open_[rr[i]][cc[i]] || flag[rr[i]][cc[i]]) continue;
            if (nq >= MAXN * MAXN) {
                fprintf(stderr, "minesweeper: the open list overflowed at %d squares\n", nq);
                running = 0;
                return;
            }
            sr[nq] = rr[i]; sc[nq] = cc[i]; nq++;
        }
    }
}

static void check_win(void) {
    if (!dead && n_open == gn * gn - LEVELS[level].mines) won = 1;
}

static void chord(int r, int c) {
    int rr[8], cc[8];
    int k = neighbours(r, c, rr, cc);
    int f = 0;
    for (int i = 0; i < k; i++)
        if (flag[rr[i]][cc[i]]) f++;
    if (f != num[r][c]) return;
    for (int i = 0; i < k && !dead; i++)
        if (!flag[rr[i]][cc[i]]) open_square(rr[i], cc[i]);
    check_win();
}

/* ---- styles ------------------------------------------------------------- */

enum { S_COVER = 0, S_FLAG, S_BLANK, S_NUM, S_MINE, S_BOOM, S_BADFLAG };

static int style_of(int r, int c) {
    if (open_[r][c]) {
        if (mine[r][c]) return (r == boom_r && c == boom_c) ? S_BOOM : S_MINE;
        return num[r][c] ? S_NUM : S_BLANK;
    }
    if (dead || won) {
        if (mine[r][c] && !flag[r][c]) return S_MINE;
        if (!mine[r][c] && flag[r][c]) return S_BADFLAG;
    }
    return flag[r][c] ? S_FLAG : S_COVER;
}

/* ---- drawing ------------------------------------------------------------ */

static int digit_scale(void) { return gcell >= 40 ? 2 : 1; }

static void draw_flag_at(int x, int y) {
    int pole_x = x + gcell * 3 / 10;
    int pole_h = gcell * 6 / 10;
    ek_fill_rect(pole_x, y + gcell / 5, 2, pole_h, EK_INK);
    int fh = pole_h / 2, fw = gcell * 4 / 10;
    for (int i = 0; i < fh; i++)
        ek_hline(pole_x + 2, y + gcell / 5 + i, fw - (i * fw) / fh, EK_INK);
    ek_hline(pole_x, y + gcell / 5 + pole_h, gcell * 4 / 10, EK_INK);
}

static void draw_mine_at(int x, int y, int ink) {
    int cx = x + gcell / 2, cy = y + gcell / 2 + gcell / 12;
    int rad = gcell * 12 / 100;
    ek_fill_circle(cx, cy, rad, ink);
    ek_hline(cx - rad - gcell / 8, cy, 2 * rad + gcell / 4 + 1, ink);
    ek_vline(cx, cy - rad - gcell / 8, 2 * rad + gcell / 4 + 1, ink);
    ek_line(cx - rad, cy - rad, cx + rad, cy + rad, ink);
    ek_line(cx + rad, cy - rad, cx - rad, cy + rad, ink);
}

static void draw_cell(int r, int c) {
    const int x = gx0 + c * gcell, y = gy0 + r * gcell;
    int s = style_of(r, c);

    ek_fill_rect(x, y, gcell, gcell, EK_PAPER);
    /* A covered square is a light grey, not a half-black one: at 50% ink the
     * 1px grid line and the 1px flag pole disappear into the dither. */
    if (s == S_COVER) ek_dither_rect(x + 2, y + 2, gcell - 4, gcell - 4, 2);

    switch (s) {
    case S_FLAG:
    case S_BADFLAG:
        draw_flag_at(x, y);
        if (s == S_BADFLAG) {            /* a cross over it: there was no mine here */
            ek_line(x + gcell / 6, y + gcell / 6, x + gcell * 5 / 6, y + gcell * 5 / 6, EK_INK);
            ek_line(x + gcell * 5 / 6, y + gcell / 6, x + gcell / 6, y + gcell * 5 / 6, EK_INK);
        }
        break;
    case S_NUM: {
        char d[2] = { (char)('0' + num[r][c]), 0 };
        int sc = digit_scale();
        ek_text_scale(x + (gcell - 8 * sc) / 2, y + (gcell - 16 * sc) / 2, d, EK_INK, sc);
        break;
    }
    case S_MINE:
        draw_mine_at(x, y, EK_INK);
        break;
    case S_BOOM:
        ek_fill_rect(x, y, gcell, gcell, EK_INK);
        draw_mine_at(x, y, EK_PAPER);
        break;
    default:
        break;
    }

    /* The covered/uncovered edge. Only the right and the bottom belong to this
     * square, so two neighbours never draw the same line twice. */
    ek_fill_rect(x + gcell - 1, y, 1, gcell, EK_INK);
    ek_fill_rect(x, y + gcell - 1, gcell, 1, EK_INK);
}

static void sync_cells(void) {
    for (int r = 0; r < gn; r++)
        for (int c = 0; c < gn; c++) {
            int want = style_of(r, c);
            if (want != drawn[r][c]) {
                draw_cell(r, c);
                drawn[r][c] = want;
            }
        }
}

static void draw_chips(void) {
    for (int i = 0; i < 3; i++) {
        int x = CHIP_X(i);
        ek_fill_rect(x, CHIP_Y, CHIP_W, CHIP_H, EK_PAPER);
        ek_rect(x, CHIP_Y, CHIP_W, CHIP_H, EK_INK);
        ek_text_scale(x + (CHIP_W - (int)strlen(LEVELS[i].name) * 16) / 2, CHIP_Y + 1,
                      LEVELS[i].name, EK_INK, 2);
        if (level == i) ek_invert_rect(x, CHIP_Y, CHIP_W, CHIP_H);
    }
}

static void draw_status(void) {
    char a[24], b[24];
    snprintf(a, sizeof a, "MINES %d", LEVELS[level].mines);
    snprintf(b, sizeof b, "FLAGS %d", n_flags);
    ek_fill_rect(STAT_X - 4, CHIP_Y - 2, 168, 40, EK_PAPER);
    ek_text(STAT_X, CHIP_Y, a, EK_INK);
    ek_text(STAT_X, CHIP_Y + 20, b, EK_INK);
}

static void draw_flag_button(void) {
    const char *label = flag_mode ? "FLAG ON" : "FLAG OFF";
    ek_fill_rect(BTN_FLAG_X, BTN_Y, BTN_W, BTN_H, EK_PAPER);
    ek_rect(BTN_FLAG_X, BTN_Y, BTN_W, BTN_H, EK_INK);
    ek_rect(BTN_FLAG_X + 1, BTN_Y + 1, BTN_W - 2, BTN_H - 2, EK_INK);
    ek_text_scale(BTN_FLAG_X + (BTN_W - (int)strlen(label) * 16) / 2, BTN_Y + (BTN_H - 32) / 2,
                  label, EK_INK, 2);
    if (flag_mode) ek_invert_rect(BTN_FLAG_X, BTN_Y, BTN_W, BTN_H);
}

static void draw_new_button(void) {
    ek_fill_rect(BTN_NEW_X, BTN_Y, BTN_W, BTN_H, EK_PAPER);
    ek_rect(BTN_NEW_X, BTN_Y, BTN_W, BTN_H, EK_INK);
    ek_rect(BTN_NEW_X + 1, BTN_Y + 1, BTN_W - 2, BTN_H - 2, EK_INK);
    ek_text_scale(BTN_NEW_X + (BTN_W - 5 * 16) / 2, BTN_Y + (BTN_H - 32) / 2, "NEW", EK_INK, 2);
}

static void draw_card(const char *title) {
    const int w = 340, h = 200;
    const int x = GRID_X + (GRID_W - w) / 2;
    const int y = GRID_Y + (GRID_W - h) / 2;

    ek_fill_rect(x, y, w, h, EK_PAPER);
    ek_rect(x, y, w, h, EK_INK);
    ek_rect(x + 1, y + 1, w - 2, h - 2, EK_INK);
    ek_text_scale(x + (w - (int)strlen(title) * 24) / 2, y + 34, title, EK_INK, 3);
    ek_text_scale(x + (w - 11 * 16) / 2, y + 128, "TAP FOR NEW", EK_INK, 2);
}

static void draw_all(void) {
    ek_clear(EK_PAPER);
    ek_text_scale(24, 8, "MINESWEEPER", EK_INK, 3);
    draw_chips();
    draw_status();
    draw_flag_button();
    draw_new_button();

    for (int r = 0; r < gn; r++)
        for (int c = 0; c < gn; c++)
            drawn[r][c] = -1;
    ek_rect(gx0 - 2, gy0 - 2, gn * gcell + 4, gn * gcell + 4, EK_INK);
    sync_cells();

    if (dead) draw_card("BOOM");
    else if (won) draw_card("CLEARED");
}

/* ---- presenting --------------------------------------------------------- */

static void present(void) {
    if (kframe_covering()) {
        kframe_paint();
        ek_flush();
        return;
    }
    int clean = force_clean || clean_next || redraw_all;

    if (redraw_all) {
        draw_all();
        redraw_all = 0;
    } else {
        sync_cells();
        draw_status();
        draw_flag_button();
    }
    clean_next = 0;

    ek_waveform(clean ? wfm_clean : 0);
    kframe_paint();
    ek_stats st = ek_flush();
    ek_waveform(0);

    if (stats_on)
        fprintf(stderr,
                "minesweeper: flush wfm=%d rects=%d tiles=%d px=%ld full=%d folded=%d us=%u\n",
                st.wfm, st.rects, st.tiles, st.px, st.full, st.folded, st.us);
}

/* ---- input -------------------------------------------------------------- */

static void tap_square(int r, int c) {
    if (!started) start_at(r, c);
    if (!running) return;

    if (dead || won) return;

    if (flag_mode && !open_[r][c]) {
        flag[r][c] = !flag[r][c];
        n_flags += flag[r][c] ? 1 : -1;
        return;
    }
    if (open_[r][c]) {
        if (num[r][c] >= 1 && num[r][c] <= 8) chord(r, c);
        return;
    }
    if (flag[r][c]) return;           /* a flag is in the way; take it off first */
    open_square(r, c);
    check_win();
}

static void tap_down(int x, int y) {
    if (kframe_tap(x, y)) {
        if (!kframe_covering()) redraw_all = 1;
        return;
    }
    if (dead || won) {                /* the card is dismissed by any tap */
        new_game();
        return;
    }

    for (int i = 0; i < 3; i++)
        if (x >= CHIP_X(i) && x < CHIP_X(i) + CHIP_W &&
            y >= CHIP_Y && y < CHIP_Y + CHIP_H) {
            level = i;
            new_game();
            return;
        }

    if (y >= BTN_Y && y < BTN_Y + BTN_H) {
        if (x >= BTN_FLAG_X && x < BTN_FLAG_X + BTN_W) {
            flag_mode = !flag_mode;
            draw_flag_button();
            return;
        }
        if (x >= BTN_NEW_X && x < BTN_NEW_X + BTN_W) {
            new_game();
            return;
        }
    }

    if (x >= gx0 && x < gx0 + gn * gcell && y >= gy0 && y < gy0 + gn * gcell)
        tap_square((y - gy0) / gcell, (x - gx0) / gcell);
}

/* ---- the computer playing, for --auto ----------------------------------- */

static void auto_steps(int n) {
    for (int i = 0; i < n && !dead && !won && running; i++) {
        int r = -1, c = -1;
        for (int k = 0; k < gn * gn && r < 0; k++) {
            int y = k / gn, x = k % gn;
            if (!open_[y][x] && !flag[y][x]) {
                if (!started) { r = y; c = x; break; }
                if (!mine[y][x]) { r = y; c = x; }
            }
        }
        if (r < 0) break;
        tap_square(r, c);
    }
}

/* ---- self-test ---------------------------------------------------------- */

static void reset_for_test(int lvl) {
    level = lvl;
    new_game();
    redraw_all = 0;
}

static int count_around(int r, int c) {
    int rr[8], cc[8], k = neighbours(r, c, rr, cc), n = 0;
    for (int i = 0; i < k; i++)
        if (mine[rr[i]][cc[i]]) n++;
    return n;
}

static int selftest(void) {
    /* 1. Every board fits the panel and holds its mines outside the first tap. */
    for (int l = 0; l < 3; l++) {
        reset_for_test(l);
        char msg[96];
        snprintf(msg, sizeof msg, "%s board is %dx%d inside %d px", LEVELS[l].name, gn, gn, GRID_W);
        expect(gn * gcell <= GRID_W, msg);
        snprintf(msg, sizeof msg, "%s squares are at least 32 px across", LEVELS[l].name);
        expect(gcell >= 32, msg);
        snprintf(msg, sizeof msg, "%s board leaves room for the counters", LEVELS[l].name);
        expect(gy0 + gn * gcell <= BTN_Y - 8, msg);
    }

    /* 2. The mines and the squares around the first tap. */
    reset_for_test(0);
    start_at(4, 4);
    int placed = 0;
    for (int r = 0; r < gn; r++)
        for (int c = 0; c < gn; c++)
            if (mine[r][c]) placed++;
    expect(placed == LEVELS[0].mines, "the board holds exactly the number of mines asked for");
    int clear = 1;
    for (int r = 3; r <= 5; r++)
        for (int c = 3; c <= 5; c++)
            if (mine[r][c]) clear = 0;
    expect(clear, "the first tap and its eight neighbours are clear");
    expect(open_[4][4], "the first tap is open");

    /* 3. Corner first tap: the safety zone is smaller and still respected. */
    reset_for_test(0);
    start_at(0, 0);
    clear = 1;
    for (int r = 0; r <= 1; r++)
        for (int c = 0; c <= 1; c++)
            if (mine[r][c]) clear = 0;
    expect(clear, "a corner first tap keeps its corner clear");

    /* 4. The numbers match the mines around them. */
    int ok = 1;
    for (int r = 0; r < gn; r++)
        for (int c = 0; c < gn; c++) {
            if (mine[r][c]) continue;
            if (num[r][c] != count_around(r, c)) ok = 0;
        }
    expect(ok, "every open square counts its neighbouring mines");

    /* 5. Same seed, same board. */
    unsigned char first[MAXN][MAXN];
    reset_for_test(1);
    rng_state = 999;
    start_at(6, 6);
    memcpy(first, mine, sizeof mine);
    reset_for_test(1);
    rng_state = 999;
    start_at(6, 6);
    expect(memcmp(first, mine, sizeof mine) == 0, "the same seed lays the same mines");

    /* 6. An empty square opens its whole hole and its rim, and no mine. */
    reset_for_test(0);
    rng_state = 12345;
    start_at(4, 4);
    int zr = -1, zc = -1;
    for (int r = 0; r < gn && zr < 0; r++)
        for (int c = 0; c < gn && zr < 0; c++)
            if (!mine[r][c] && num[r][c] == 0 && !open_[r][c]) { zr = r; zc = c; }
    expect(zr >= 0, "the board has an empty square to open");
    memset(open_, 0, sizeof open_);
    n_open = 0;
    dead = 0;
    open_square(zr, zc);
    expect(!dead, "opening an empty square does not hit a mine");

    /* The hole is every empty square reachable from this one, plus the rim
     * around those. A separate empty region across a wall of numbers is not
     * part of it, so the reachability is walked here rather than assumed. */
    static unsigned char seen[MAXN][MAXN];
    int qr[MAXN * MAXN], qc[MAXN * MAXN], nq = 0;
    memset(seen, 0, sizeof seen);
    qr[nq] = zr; qc[nq] = zc; nq++;
    while (nq > 0) {
        nq--;
        int y = qr[nq], x = qc[nq];
        if (seen[y][x] || mine[y][x]) continue;
        seen[y][x] = 1;
        if (num[y][x] != 0) continue;
        int rr[8], cc[8], k = neighbours(y, x, rr, cc);
        for (int i = 0; i < k; i++)
            if (!seen[rr[i]][cc[i]] && !mine[rr[i]][cc[i]]) {
                qr[nq] = rr[i]; qc[nq] = cc[i]; nq++;
            }
    }
    int want = 0;
    for (int r = 0; r < gn; r++)
        for (int c = 0; c < gn; c++) {
            if (mine[r][c]) continue;
            int rr[8], cc[8], k = neighbours(r, c, rr, cc), part = seen[r][c];
            if (num[r][c] != 0)
                for (int i = 0; i < k && !part; i++)
                    if (seen[rr[i]][cc[i]] && num[rr[i]][cc[i]] == 0) part = 1;
            if (part) want++;
        }
    expect(n_open == want, "the hole holds every empty square and the rim around it");
    for (int r = 0; r < gn; r++)
        for (int c = 0; c < gn; c++)
            if (seen[r][c] && !open_[r][c]) {
                fprintf(stderr, "minesweeper: hole square %d,%d stayed shut\n", r, c);
                failures++;
                r = gn;
                break;
            }

    /* 7. A flag counts once, and comes off. */
    reset_for_test(0);
    start_at(4, 4);
    flag_mode = 1;
    int fr = -1, fc = -1;
    for (int r = 0; r < gn && fr < 0; r++)
        for (int c = 0; c < gn && fr < 0; c++)
            if (!open_[r][c]) { fr = r; fc = c; }
    tap_square(fr, fc);
    expect(flag[fr][fc] && n_flags == 1, "a tap with FLAG on puts one flag down");
    expect(!open_[fr][fc], "a flagged square is not opened");
    tap_square(fr, fc);
    expect(!flag[fr][fc] && n_flags == 0, "a second tap takes the flag off again");
    flag_mode = 0;

    /* 8. Chord: the right flags open the rest, a wrong flag blows it up. */
    reset_for_test(0);
    rng_state = 777;
    start_at(4, 4);
    int nr = -1, nc = -1;
    /* Find an open number that still has a covered, mine-free neighbour: only
     * that one can gain squares from a chord. */
    for (int r = 0; r < gn && nr < 0; r++)
        for (int c = 0; c < gn && nr < 0; c++)
            if (open_[r][c] && num[r][c] >= 1) {
                int rr0[8], cc0[8], k0 = neighbours(r, c, rr0, cc0);
                for (int i = 0; i < k0; i++)
                    if (!mine[rr0[i]][cc0[i]] && !open_[rr0[i]][cc0[i]] && nr < 0) {
                        nr = r; nc = c;
                    }
            }
    expect(nr >= 0, "an open number still has a closed square beside it");
    int rr[8], cc[8], k = neighbours(nr, nc, rr, cc);
    for (int i = 0; i < k; i++)
        if (mine[rr[i]][cc[i]] && !open_[rr[i]][cc[i]]) { flag[rr[i]][cc[i]] = 1; n_flags++; }
    int before = n_open;
    chord(nr, nc);
    expect(!dead, "the right flags make the chord safe");
    expect(n_open > before, "the chord opens the unflagged neighbours");

    /* A chord only acts on a number whose flags match it. */
    reset_for_test(0);
    rng_state = 777;
    start_at(4, 4);
    nr = -1;
    for (int r = 0; r < gn && nr < 0; r++)
        for (int c = 0; c < gn && nr < 0; c++)
            if (open_[r][c] && num[r][c] >= 1) { nr = r; nc = c; }
    k = neighbours(nr, nc, rr, cc);
    before = n_open;
    chord(nr, nc);                     /* no flags at all: nothing to do */
    expect(n_open == before && !dead, "a chord with too few flags does nothing");

    /* 9. Opening every safe square wins, and nothing else does. */
    reset_for_test(0);
    rng_state = 24;
    start_at(4, 4);
    for (int r = 0; r < gn && !won; r++)
        for (int c = 0; c < gn && !won; c++)
            if (!mine[r][c] && !open_[r][c]) {
                open_square(r, c);
                check_win();
            }
    expect(won && !dead, "opening every safe square wins");
    expect(n_open == gn * gn - LEVELS[0].mines, "the win opens exactly the safe squares");

    /* 10. Tapping a mine loses. */
    reset_for_test(0);
    rng_state = 5150;
    start_at(4, 4);
    int mr = -1, mc = -1;
    for (int r = 0; r < gn && mr < 0; r++)
        for (int c = 0; c < gn && mr < 0; c++)
            if (mine[r][c]) { mr = r; mc = c; }
    open_square(mr, mc);
    expect(dead && boom_r == mr && boom_c == mc, "a mine goes off where it was tapped");
    expect(style_of(mr, mc) == S_BOOM, "the mine that went off is drawn as the boom");

    /* 11. A board that cannot hold its mines says so instead of guessing. */
    reset_for_test(2);
    int keep = 9;                       /* a first tap in the middle of a 16x16 */
    expect(LEVELS[2].mines <= gn * gn - keep, "the hard board fits beside the first tap");
    expect(place_mines(8, 8, gn * gn - 4) != 0,
           "asking for more mines than the first tap leaves free is refused");
    expect(place_mines(8, 8, gn * gn - keep) == 0,
           "asking for exactly the spare squares is allowed");
    int placed_hard = 0;
    for (int r = 0; r < gn; r++)
        for (int c = 0; c < gn; c++)
            if (mine[r][c]) placed_hard++;
    expect(placed_hard == gn * gn - keep, "a full board places every mine it was asked for");

    if (failures) fprintf(stderr, "minesweeper: %d check(s) failed\n", failures);
    else fprintf(stderr, "minesweeper: all checks passed\n");
    return failures == 0 ? 0 : 1;
}

/* ---- the shared menu ---------------------------------------------------- */

enum { MS_NEW = 1, MS_FLAG, MS_LEVEL };

static void progress_line(char *dst, size_t n) {
    snprintf(dst, n, "%dx%d board, %d mines, %d flags out",
             gn, gn, LEVELS[level].mines, n_flags);
}

static int on_ms_item(int id) {
    switch (id) {
    case MS_NEW:  new_game(); return 1;
    case MS_FLAG: flag_mode = !flag_mode; return 1;
    case MS_LEVEL: return 1;   /* LEVEL acts on its own three rows */
    }
    fprintf(stderr, "minesweeper: menu item %d is not one of ours\n", id);
    return 1;
}

static void draw_new(const kshell_rect *r) {
    char line[64];
    progress_line(line, sizeof line);
    ek_text_scale(r->x, r->y, "NEW BOARD", EK_INK, 2);
    ek_text(r->x, r->y + 44, "throws this board away and lays a fresh one", EK_INK);
    ek_text(r->x, r->y + 66, line, EK_INK);
    kshell_button(KFRAME_ACT_X, KFRAME_ACT_Y, KFRAME_ACT_W, KFRAME_ACT_H, "START", 0);
}

static int tap_new(const kshell_rect *r, int x, int y) {
    (void)r;
    if (!kshell_inside(x, y, KFRAME_ACT_X, KFRAME_ACT_Y, KFRAME_ACT_W, KFRAME_ACT_H)) return 0;
    on_ms_item(MS_NEW);
    kframe_close();
    return 1;
}

static void draw_flag(const kshell_rect *r) {
    ek_text_scale(r->x, r->y, "FLAG", EK_INK, 2);
    ek_text(r->x, r->y + 44, "with this on, a tap marks a square instead of opening it.", EK_INK);
    ek_text(r->x, r->y + 66, flag_mode ? "it is ON now." : "it is OFF now.", EK_INK);
    kshell_button(KFRAME_ACT_X, KFRAME_ACT_Y, KFRAME_ACT_W, KFRAME_ACT_H,
                  flag_mode ? "TURN OFF" : "TURN ON", 0);
}

static int tap_flag(const kshell_rect *r, int x, int y) {
    (void)r;
    if (!kshell_inside(x, y, KFRAME_ACT_X, KFRAME_ACT_Y, KFRAME_ACT_W, KFRAME_ACT_H)) return 0;
    on_ms_item(MS_FLAG);
    kframe_close();
    return 1;
}

static void draw_level(const kshell_rect *r) {
    ek_text_scale(r->x, r->y, "LEVEL", EK_INK, 2);
    ek_text(r->x, r->y + 44, "a tap starts a fresh board at that size", EK_INK);
    const char *labels[3] = { LEVELS[0].name, LEVELS[1].name, LEVELS[2].name };
    kshell_options(LVL_Y0, labels, 3, level);
}

static int tap_level(const kshell_rect *r, int x, int y) {
    (void)r;
    int i = kshell_option_at(LVL_Y0, 3, x, y);
    if (i < 0) return 0;
    level = i;
    new_game();
    kframe_close();
    return 1;
}

static const kframe_item MS_ITEMS[] = {
    { "NEW",   MS_NEW,   draw_new,   tap_new },
    { "FLAG",  MS_FLAG,  draw_flag,  tap_flag },
    { "LEVEL", MS_LEVEL, draw_level, tap_level },
};

static void kf_status(const char *msg) { fprintf(stderr, "minesweeper: %s\n", msg); }
static void kf_quit(void) { running = 0; stop_req = 1; }

static int open_frame(void) {
    kframe_cfg kf;
    memset(&kf, 0, sizeof kf);
    kf.title = "MINESWEEPER";
    kf.app_id = "mines";              /* must match the id in src/kframe.c */
    kf.items = MS_ITEMS;
    kf.nitems = (int)(sizeof MS_ITEMS / sizeof MS_ITEMS[0]);
    kf.on_item = on_ms_item;
    kf.on_quit = kf_quit;
    kf.repaint = present;
    kf.status = kf_status;
    kf.stop = &stop_req;
    if (kframe_init(&kf) != 0) {
        fprintf(stderr, "minesweeper: the shared menu would not start\n");
        return -1;
    }
    return 0;
}

/* ---- main --------------------------------------------------------------- */

static void usage(void) {
    fprintf(stderr,
            "usage: minesweeper [options]\n"
            "\n"
            "  On the Kindle the panel is the game's until you tap QUIT, then SURE.\n"
            "  Tap a square to open it. FLAG at the bottom turns taps into flags.\n"
            "  Tap an open number with all its mines flagged to open what is left.\n"
            "  EASY / MED / HARD up top start a fresh board at that size.\n"
            "  The hamburger at (572,26) has NEW / FLAG / LEVEL as tabs.\n"
            "  --selftest   check the board, the rules and the flood on this computer\n"
            "  --auto N     open N safe squares by itself, then stop\n"
            "  --dump FILE  write the picture (PGM) and exit\n"
            "  --menu-shot F  write the menu (PGM) and exit\n"
            "  --seed N     same seed, same board\n"
            "  --stats      print what each screen update cost\n"
            "  --clean      refresh the whole picture on every update (slow)\n");
}

static void on_signal(int sig) {
    (void)sig;
    stop_req = 1;
}

int main(int argc, char **argv) {
    unsigned flags = EK_TAKEOVER;
    int wait = 1;
    int do_test = 0;
    int auto_n = -1;
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
            auto_n = (int)v;
        } else if (!strcmp(argv[i], "--dump") && i + 1 < argc) {
            dump_path = argv[++i];
            if (!dump_path[0]) { usage(); return 2; }
        } else if (!strcmp(argv[i], "--menu-shot") && i + 1 < argc) {
            menu_shot = argv[++i];
            if (!menu_shot[0]) { usage(); return 2; }
        } else if (!strcmp(argv[i], "--selftest")) do_test = 1;
        else if (!strcmp(argv[i], "--stats")) stats_on = 1;
        else if (!strcmp(argv[i], "--clean")) force_clean = 1;
        else if (!strcmp(argv[i], "--no-takeover")) flags &= ~EK_TAKEOVER;
        else if (!strcmp(argv[i], "--no-wait")) wait = 0;
        else if (!strcmp(argv[i], "--help")) { usage(); return 0; }
        else { usage(); return 2; }
    }

    if (seed == 0) {
        struct timespec ts;
        if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
            fprintf(stderr, "minesweeper: clock_gettime failed\n");
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

    if (open_frame() != 0) return 1;

    if (ek_open(flags) != 0) {
        fprintf(stderr, "minesweeper: the screen would not open\n");
        ek_restore_ui();
        return 1;
    }
    wfm_clean = ek_waveform_by_name("GC16");
    if (!wfm_clean) {
        fprintf(stderr, "minesweeper: GC16 waveform is missing\n");
        ek_close();
        return 1;
    }
    ek_mono(1);
    ek_wait_updates(wait);
    if (force_clean) {
        ek_waveform(wfm_clean);
        ek_ghost(0, 0);
        ek_policy(180, 4, 30);
    } else {
        ek_waveform(0);
        ek_ghost(6, 30);
        ek_policy(60, 4, 30);
    }

    rng_state = seed;
    int rc = 0;
    if (do_test) {
        rc = selftest();
        new_game();
        present();
    } else {
        new_game();
        if (auto_n >= 0) auto_steps(auto_n);
        present();
    }

    if (menu_shot) {
        kframe_show_menu();
        kframe_paint();
        ek_flush();
        if (ek_dump_pgm(menu_shot, 1) != 0) {
            fprintf(stderr, "minesweeper: could not write %s\n", menu_shot);
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
                fprintf(stderr, "minesweeper: want 'tap X Y' or q\n");
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
        else ek_sleep_ms(15);
    }
#endif

    if (dump_path) {
        if (ek_dump_pgm(dump_path, 1) != 0) {
            fprintf(stderr, "minesweeper: could not write %s\n", dump_path);
            ek_close();
            return 1;
        }
    }

    ek_close();
    return rc;
}
