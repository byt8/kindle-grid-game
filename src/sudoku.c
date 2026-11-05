#define _GNU_SOURCE
/* sudoku.c -- Sudoku on the Kindle Touch: a 600x800 panel, one bit, taps only.
 *
 * The screen is one big 9x9 grid with three rows of controls:
 *   EASY / MED / HARD  chips up top -- tap one for a fresh puzzle at that size
 *   NEW / ERASE / HINT / QUIT in the middle row
 *   1..9 digits along the bottom
 *
 * The hamburger (top right) opens the shared shell, where the same controls
 * live as tabs: NEW, ERASE, HINT and LEVEL. Digits stay on the board.
 *
 * Flow: tap a cell to select it (thick ink ring), tap a digit to write it.
 * ERASE empties the selected cell (givens are locked). HINT writes the
 * answer into the selected cell (or the first empty one). A digit that
 * clashes with a row / column / box mate is boxed as an error; a complete
 * board with no errors is the win, which puts up a "PUZZLE DONE" card.
 *
 * Puzzles are generated on the device: fill_board() builds a full grid with a
 * seeded random walk, then cells are blanked one at a time, keeping a cell
 * only when the puzzle stays uniquely solvable (solve_count() == 1). "Easy"
 * keeps 45 clues, "medium" 32, "hard" 26 -- every puzzle is guaranteed to
 * have exactly one solution, which is also the answer HINT draws from.
 *
 * Same shape as tetris.c: rules, drawing, input and a PC self-test in one
 * file, and the EK_HOST build reads `tap X Y` lines so a run is replayable
 * with the same seed. The whole board regenerates per seed, so the host
 * tests never depend on the puzzle content (see --auto and the HINT tap).
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

#define X0   30
#define Y0   110
#define CELL 60
#define SIZE (9 * CELL)              /* 540 */

#define CHIP_Y 58
#define CHIP_H 34
#define CHIP_W 104
#define CHIP_X(i) (30 + (i) * 112)

#define AC_Y 662
#define AC_H 44
#define AC_W 144
#define AC_X(i) (3 + (i) * 148)

#define DIG_Y 712
#define DIG_H 78
#define DIG_W 64
#define DIG_X(i) (3 + (i) * 66)

#define STATUS_X 470
#define STATUS_Y 64

/* What a cell can look like on the canvas. */
enum { ST_EMPTY = 0, ST_GIVEN, ST_TYPED, ST_ERR };

/* ---- game state --------------------------------------------------------- */

static unsigned char sol[9][9];    /* the puzzle's unique solution */
static unsigned char given[9][9];  /* the clues shown at start (0 = open) */
static unsigned char cell[9][9];   /* what the player has typed so far */
static unsigned char err[9][9];   /* 1 = this typed digit clashes */
static int drawn_style[9][9];      /* what the canvas shows per cell */

static int sel_r = -1, sel_c = -1; /* selected cell, -1 = none */
static int difficulty;              /* 0 EASY, 1 MED, 2 HARD */
static int n_filled;               /* non-empty cells (clues + typed) */
static int win;                    /* the PUZZLE DONE card is up */
static int quit_armed;             /* QUIT tapped once; SURE to leave */
static int running;                /* 0 = leave and give the screen back */
static int redraw_all;             /* next present() repaints everything */

static int last_filled = -1;
static uint32_t rng_state = 1;

static int stats_on, force_clean, clean_next;
static int wfm_clean;
static volatile sig_atomic_t stop_req;

static int failures;
static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "sudoku: FAIL %s\n", msg);
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

/* ---- generator and solver ----------------------------------------------- */

/* 1..9 in a freshly shuffled order. */
static void shuffled(int *out, int n) {
    for (int i = 0; i < n; i++) out[i] = i + 1;
    for (int i = n - 1; i > 0; i--) {
        int j = (int)(rnd() % (uint32_t)(i + 1));
        int t = out[i]; out[i] = out[j]; out[j] = t;
    }
}

static int valid_at(const unsigned char b[9][9], int r, int c, int v) {
    for (int i = 0; i < 9; i++) {
        if (b[r][i] == v || b[i][c] == v) return 0;
    }
    int br = (r / 3) * 3, bc = (c / 3) * 3;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            if (b[br + i][bc + j] == v) return 0;
    return 1;
}

/* Backtracking fill; returns 1 when the whole board is a legal solution. */
static int fill_board(unsigned char b[9][9], int pos) {
    while (pos < 81 && b[pos / 9][pos % 9]) pos++;
    if (pos == 81) return 1;
    int r = pos / 9, c = pos % 9;
    int vals[9];
    shuffled(vals, 9);
    for (int k = 0; k < 9; k++) {
        if (!valid_at(b, r, c, vals[k])) continue;
        b[r][c] = (unsigned char)vals[k];
        if (fill_board(b, pos + 1)) return 1;
        b[r][c] = 0;
    }
    return 0;
}

/* Counts solutions up to `limit` (mutates b, restores it). */
static int solve_count(unsigned char b[9][9], int limit) {
    int r = -1, c = -1;
    for (int i = 0; i < 81 && r < 0; i++)
        if (!b[i / 9][i % 9]) { r = i / 9; c = i % 9; }
    if (r < 0) return 1;
    int total = 0;
    for (int v = 1; v <= 9 && total <= limit; v++) {
        if (!valid_at(b, r, c, v)) continue;
        b[r][c] = (unsigned char)v;
        total += solve_count(b, limit - total);
        b[r][c] = 0;
    }
    return total;
}

static const int CLUES[3] = {45, 32, 26};

static void gen_puzzle(void) {
    memset(sol, 0, sizeof sol);
    int ok = fill_board(sol, 0);
    if (!ok) {
        fprintf(stderr, "sudoku: could not build a full grid\n");
        exit(1);
    }
    memcpy(given, sol, sizeof given);

    /* Blank cells in shuffled order; a cell stays blank only while the
     * puzzle keeps exactly one solution. A second pass retries with a
     * rotated order, so a stubborn grid can still shed its clues. */
    int order[81];
    for (int i = 0; i < 81; i++) order[i] = i;
    for (int i = 80; i > 0; i--) {
        int j = (int)(rnd() % (uint32_t)(i + 1));
        int t = order[i]; order[i] = order[j]; order[j] = t;
    }
    int target = CLUES[difficulty];
    int clues = 81;
    for (int pass = 0; pass < 5 && clues > target; pass++) {
        for (int i = 0; i < 81 && clues > target; i++) {
            int p = order[(pass * 17 + i * 7) % 81];
            int r = p / 9, c = p % 9;
            if (!given[r][c]) continue;
            unsigned char keep = given[r][c];
            given[r][c] = 0;
            unsigned char probe[9][9];
            memcpy(probe, given, sizeof probe);
            if (solve_count(probe, 2) == 1) {
                clues--;
            } else {
                given[r][c] = keep;
            }
        }
    }
}

/* ---- rules -------------------------------------------------------------- */

static int style_of(int r, int c) {
    if (given[r][c]) return ST_GIVEN;
    if (!cell[r][c]) return ST_EMPTY;
    return err[r][c] ? ST_ERR : ST_TYPED;
}

/* Recompute which typed digits clash with a row / column / box mate.
 * Givens come from the solution, so they never clash with each other. */
static void mark_errors(void) {
    memset(err, 0, sizeof err);
    for (int r = 0; r < 9; r++) {
        for (int c = 0; c < 9; c++) {
            int v = cell[r][c];
            if (!v) continue;
            for (int i = 0; i < 9; i++) {
                if (i != c && (cell[r][i] == v || given[r][i] == v)) err[r][c] = 1;
                if (i != r && (cell[i][c] == v || given[i][c] == v)) err[r][c] = 1;
            }
            int br = (r / 3) * 3, bc = (c / 3) * 3;
            for (int i = 0; i < 3 && !err[r][c]; i++)
                for (int j = 0; j < 3; j++) {
                    int rr = br + i, cc = bc + j;
                    if (rr == r && cc == c) continue;
                    if (cell[rr][cc] == v || given[rr][cc] == v) { err[r][c] = 1; break; }
                }
        }
    }
}

static void recount(void) {
    int n = 0;
    for (int r = 0; r < 9; r++)
        for (int c = 0; c < 9; c++)
            if (cell[r][c] || given[r][c]) n++;
    n_filled = n;
}

static void draw_cell(int r, int c);
static void draw_win_card(void);

static void select_cell(int r, int c) {
    if (r == sel_r && c == sel_c) return;
    if (sel_r >= 0) drawn_style[sel_r][sel_c] = -1;
    sel_r = r;
    sel_c = c;
    drawn_style[r][c] = -1;
}

static void check_win(void) {
    if (win) return;
    for (int r = 0; r < 9; r++)
        for (int c = 0; c < 9; c++)
            if (!cell[r][c] && !given[r][c]) return; /* still an empty cell */
    for (int r = 0; r < 9; r++)
        for (int c = 0; c < 9; c++)
            if (err[r][c]) return; /* conflicting entry: not done yet */
    win = 1;
    draw_win_card();
}

static void set_digit(int r, int c, int v) {
    cell[r][c] = (unsigned char)v;
    mark_errors();
    recount();
    drawn_style[r][c] = -1;
    check_win();
}

static void type_digit(int v) {
    if (sel_r < 0 || win) return;
    if (given[sel_r][sel_c]) return; /* clues are locked */
    if (cell[sel_r][sel_c] == v) return;
    set_digit(sel_r, sel_c, v);
}

static void erase_cell(void) {
    if (sel_r < 0 || win) return;
    if (given[sel_r][sel_c] || !cell[sel_r][sel_c]) return;
    cell[sel_r][sel_c] = 0;
    mark_errors();
    recount();
    drawn_style[sel_r][sel_c] = -1;
}

static void hint(void) {
    if (win) return;
    int r = sel_r, c = sel_c;
    if (r < 0 || given[r][c] || cell[r][c]) {
        /* Nothing useful selected: take the first empty cell. */
        r = c = -1;
        for (int i = 0; i < 81 && r < 0; i++)
            if (!cell[i / 9][i % 9] && !given[i / 9][i % 9]) { r = i / 9; c = i % 9; }
        if (r < 0) return; /* the board is full */
        if (r != sel_r || c != sel_c) select_cell(r, c);
    }
    if (cell[r][c]) return;
    set_digit(r, c, sol[r][c]);
}

static void new_puzzle(void) {
    gen_puzzle();
    memset(cell, 0, sizeof cell);
    mark_errors();
    recount();
    sel_r = sel_c = -1;
    for (int i = 0; i < 81 && sel_r < 0; i++)
        if (!cell[i / 9][i % 9] && !given[i / 9][i % 9]) { sel_r = i / 9; sel_c = i % 9; }
    win = 0;
    quit_armed = 0;
    running = 1;
    redraw_all = 1;
}

/* ---- drawing ------------------------------------------------------------ */

static void draw_cell(int r, int c) {
    const int x = X0 + c * CELL, y = Y0 + r * CELL;

    /* Each cell owns its square: paper fill, then its top and left edge at
     * box-lines 2px, inner lines 1px (the right/bottom edges belong to the
     * neighbours, and the outer frame is drawn once around the whole grid). */
    ek_fill_rect(x, y, CELL, CELL, EK_PAPER);
    ek_fill_rect(x, y, (c % 3 == 0) ? 2 : 1, CELL, EK_INK);
    ek_fill_rect(x, y, CELL, (r % 3 == 0) ? 2 : 1, EK_INK);

    int v = cell[r][c] ? cell[r][c] : given[r][c];
    if (v) {
        char d[2];
        d[0] = (char)('0' + v);
        d[1] = 0;
        if (given[r][c]) {
            /* Clues are big and solid; typed digits are smaller, and an
             * error is boxed, so the three read apart at a glance. */
            ek_text_scale(x + (CELL - 24) / 2, y + (CELL - 48) / 2, d, EK_INK, 3);
        } else if (err[r][c]) {
            ek_rect(x + 5, y + 5, CELL - 10, CELL - 10, EK_INK);
            ek_text_scale(x + (CELL - 16) / 2, y + (CELL - 32) / 2, d, EK_INK, 2);
        } else {
            ek_text_scale(x + (CELL - 16) / 2, y + (CELL - 32) / 2, d, EK_INK, 2);
        }
    }

    if (r == sel_r && c == sel_c) {
        /* A 3px band on each edge, inside the cell, so it never bleeds into
         * a neighbour and a paper refill always covers it. */
        ek_fill_rect(x, y, CELL, 3, EK_INK);
        ek_fill_rect(x, y + CELL - 3, CELL, 3, EK_INK);
        ek_fill_rect(x, y, 3, CELL, EK_INK);
        ek_fill_rect(x + CELL - 3, y, 3, CELL, EK_INK);
    }
}

/* Repaint every cell whose value or style differs from the canvas. */
static void sync_cells(void) {
    for (int r = 0; r < 9; r++)
        for (int c = 0; c < 9; c++) {
            int want = style_of(r, c);
            if (want != drawn_style[r][c]) {
                draw_cell(r, c);
                drawn_style[r][c] = want;
            }
        }
}

static void draw_status(void) {
    char buf[24];
    snprintf(buf, sizeof buf, "FILLED %d/81", n_filled);
    ek_fill_rect(STATUS_X - 4, STATUS_Y - 2, 136, 20, EK_PAPER);
    ek_text(STATUS_X, STATUS_Y, buf, EK_INK);
}

static const char *const DIFF_NAME[3] = {"EASY", "MED", "HARD"};

static void draw_chips(void) {
    for (int i = 0; i < 3; i++) {
        int x = CHIP_X(i);
        ek_fill_rect(x, CHIP_Y, CHIP_W, CHIP_H, EK_PAPER);
        ek_rect(x, CHIP_Y, CHIP_W, CHIP_H, EK_INK);
        const char *s = DIFF_NAME[i];
        ek_text_scale(x + (CHIP_W - (int)strlen(s) * 16) / 2, CHIP_Y + 1, s, EK_INK, 2);
        if (difficulty == i) ek_invert_rect(x, CHIP_Y, CHIP_W, CHIP_H); /* picked */
    }
}

static void draw_frame(void) {
    ek_fill_rect(X0 - 3, Y0 - 3, SIZE + 6, SIZE + 6, EK_INK);
    ek_fill_rect(X0 - 1, Y0 - 1, SIZE + 2, SIZE + 2, EK_PAPER);
}

static void draw_win_card(void) {
    const int w = 320, h = 224;
    const int x = X0 + (SIZE - w) / 2;
    const int y = Y0 + (SIZE - h) / 2;

    ek_fill_rect(x, y, w, h, EK_PAPER);
    ek_rect(x, y, w, h, EK_INK);
    ek_rect(x + 1, y + 1, w - 2, h - 2, EK_INK);
    ek_text_scale(x + (w - 11 * 24) / 2, y + 36, "PUZZLE DONE", EK_INK, 3);
    ek_text_scale(x + (w - 11 * 16) / 2, y + 150, "TAP FOR NEW", EK_INK, 2);
}

/* ---- buttons ------------------------------------------------------------ */

enum { AC_NEW = 0, AC_ERASE, AC_HINT, AC_QUIT, N_AC };
enum { DIG_0 = N_AC, N_BTN = N_AC + 9 };

static uint32_t btn_flash[N_BTN];

static void draw_button(int i, int pressed) {
    int x, y, w, h;
    const char *label;
    char d[2];
    if (i < N_AC) {
        x = AC_X(i); y = AC_Y; w = AC_W; h = AC_H;
        if (i == AC_QUIT && quit_armed) label = "SURE";
        else if (i == AC_NEW) label = "NEW";
        else if (i == AC_ERASE) label = "ERASE";
        else if (i == AC_HINT) label = "HINT";
        else label = "QUIT";
    } else {
        d[0] = (char)('0' + (i - N_AC) + 1);
        d[1] = 0;
        label = d;
        x = DIG_X(i - N_AC); y = DIG_Y; w = DIG_W; h = DIG_H;
    }

    ek_fill_rect(x, y, w, h, EK_PAPER);
    ek_rect(x, y, w, h, EK_INK);
    ek_rect(x + 1, y + 1, w - 2, h - 2, EK_INK);
    ek_text_scale(x + (w - (int)strlen(label) * 16) / 2, y + (h - 32) / 2, label, EK_INK, 2);
    if (pressed) ek_invert_rect(x, y, w, h);
}

static void draw_all(void) {
    ek_clear(EK_PAPER);
    ek_text_scale(24, 8, "SUDOKU", EK_INK, 3);
    draw_chips();
    draw_status();
    draw_frame();
    for (int r = 0; r < 9; r++)
        for (int c = 0; c < 9; c++)
            drawn_style[r][c] = -1;
    sync_cells();
    for (int i = 0; i < N_BTN; i++) draw_button(i, 0);
    if (win) draw_win_card();
    last_filled = n_filled;
}

/* ---- presenting --------------------------------------------------------- */

static void expire_buttons(void) {
    uint32_t now = ek_ms();
    for (int i = 0; i < N_BTN; i++)
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

    if (redraw_all) {
        memset(btn_flash, 0, sizeof btn_flash); /* fresh paint, no flashes */
        draw_all();
        redraw_all = 0;
    } else {
        expire_buttons();
        sync_cells();
        if (n_filled != last_filled) {
            draw_status();
            last_filled = n_filled;
        }
    }
    clean_next = 0;

    ek_waveform(clean ? wfm_clean : 0);
    kframe_paint();
    ek_stats st = ek_flush();
    ek_waveform(0);

    if (stats_on)
        fprintf(stderr,
                "sudoku: flush wfm=%d rects=%d tiles=%d px=%ld full=%d folded=%d us=%u\n",
                st.wfm, st.rects, st.tiles, st.px, st.full, st.folded, st.us);
}

/* ---- input -------------------------------------------------------------- */

static void tap_down(int x, int y) {
    /* The hamburger, and the shell it opens, come first. Closing the shell
     * leaves its page on the glass, so the next present() repaints it all. */
    if (kframe_tap(x, y)) {
        if (!kframe_covering()) redraw_all = 1;
        return;
    }

    /* The win card is dismissed by any tap at all. */
    if (win) {
        new_puzzle();
        return;
    }

    for (int i = 0; i < 3; i++)
        if (x >= CHIP_X(i) && x < CHIP_X(i) + CHIP_W &&
            y >= CHIP_Y && y < CHIP_Y + CHIP_H) {
            if (difficulty != i) difficulty = i;
            new_puzzle(); /* tapping a chip also starts that difficulty */
            return;
        }

    int a = -1;
    for (int i = 0; i < N_AC; i++)
        if (x >= AC_X(i) && x < AC_X(i) + AC_W && y >= AC_Y && y < AC_Y + AC_H) { a = i; break; }
    if (a >= 0) {
        if (quit_armed && a != AC_QUIT) {
            quit_armed = 0;
            draw_button(AC_QUIT, 0);
            return;
        }
        switch (a) {
        case AC_NEW:  new_puzzle(); break;
        case AC_ERASE: erase_cell(); break;
        case AC_HINT: hint(); break;
        case AC_QUIT:
            /* Two taps, because there is no way back from this one. */
            if (quit_armed) running = 0;
            else { quit_armed = 1; draw_button(AC_QUIT, 1); }
            break;
        }
        btn_flash[a] = ek_ms() + 140;
        draw_button(a, 1);
        return;
    }
    if (quit_armed) {
        quit_armed = 0;
        draw_button(AC_QUIT, 0);
    }

    int d = -1;
    for (int i = 0; i < 9; i++)
        if (x >= DIG_X(i) && x < DIG_X(i) + DIG_W && y >= DIG_Y && y < DIG_Y + DIG_H) { d = i; break; }
    if (d >= 0) {
        type_digit(d + 1);
        btn_flash[N_AC + d] = ek_ms() + 140;
        draw_button(N_AC + d, 1);
        return;
    }

    if (x >= X0 && x < X0 + SIZE && y >= Y0 && y < Y0 + SIZE)
        select_cell((y - Y0) / CELL, (x - X0) / CELL);
}

/* ---- the computer playing, for --auto ----------------------------------- */

static void auto_steps(int n) {
    for (int i = 0; i < n && !win && !stop_req && running; i++) {
        int r = -1, c = -1;
        for (int k = 0; k < 81 && r < 0; k++)
            if (!cell[k / 9][k % 9] && !given[k / 9][k % 9]) { r = k / 9; c = k % 9; }
        if (r < 0) break;
        if (r != sel_r || c != sel_c) select_cell(r, c);
        type_digit(sol[r][c]);
    }
}

/* ---- self-test ---------------------------------------------------------- */

static int selftest(void) {
    /* 1. A full grid builds and is legal in rows, columns and boxes. */
    unsigned char b[9][9];
    rng_state = 4242;
    memset(b, 0, sizeof b);
    expect(fill_board(b, 0), "a full grid can be built");

    int ok = 1;
    for (int r = 0; r < 9 && ok; r++) {
        int seen[10] = {0};
        for (int c = 0; c < 9; c++) seen[b[r][c]]++;
        for (int v = 1; v <= 9; v++)
            if (seen[v] != 1) ok = 0;
    }
    expect(ok, "every row is a permutation of 1..9");

    ok = 1;
    for (int c = 0; c < 9 && ok; c++) {
        int seen[10] = {0};
        for (int r = 0; r < 9; r++) seen[b[r][c]]++;
        for (int v = 1; v <= 9; v++)
            if (seen[v] != 1) ok = 0;
    }
    expect(ok, "every column is a permutation of 1..9");

    ok = 1;
    for (int br = 0; br < 3 && ok; br++)
        for (int bc = 0; bc < 3 && ok; bc++) {
            int seen[10] = {0};
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++)
                    seen[b[br * 3 + i][bc * 3 + j]]++;
            for (int v = 1; v <= 9; v++)
                if (seen[v] != 1) ok = 0;
        }
    expect(ok, "every box is a permutation of 1..9");
    expect(solve_count(b, 2) == 1, "the full grid is its own unique solution");

    /* 2. Generation: seeded, uniquely solvable, clue counts get sparser. */
    int prev = 999;
    for (int d = 0; d < 3; d++) {
        rng_state = 1000 + d;
        difficulty = d;
        gen_puzzle();
        int clues = 0;
        for (int k = 0; k < 81; k++)
            if (given[k / 9][k % 9]) clues++;
        unsigned char probe[9][9];
        memcpy(probe, given, sizeof probe);
        expect(solve_count(probe, 2) == 1, "a generated puzzle is uniquely solvable");
        expect(clues >= 22 && clues <= 81, "clue count is in range");
        if (d == 0) expect(clues >= 40, "EASY keeps at least 40 clues");
        if (d == 2) expect(clues <= 34, "HARD leaves at most 34 clues");
        expect(clues <= prev, "a harder difficulty never adds clues");
        prev = clues;
    }

    /* 3. A duplicated digit is flagged; removing it clears the error. */
    memset(given, 0, sizeof given);
    memset(cell, 0, sizeof cell);
    cell[0][0] = 5;
    cell[0][1] = 5;
    mark_errors();
    expect(err[0][0] && err[0][1], "a duplicated digit in a row is flagged");
    cell[0][1] = 0;
    mark_errors();
    expect(!err[0][0], "removing the duplicate clears the error");

    /* 4. HINT writes the answer into an empty cell. */
    difficulty = 1;
    rng_state = 31337;
    gen_puzzle();
    memset(cell, 0, sizeof cell);
    int r0 = -1, c0 = -1;
    for (int i = 0; i < 81 && r0 < 0; i++)
        if (!given[i / 9][i % 9]) { r0 = i / 9; c0 = i % 9; }
    expect(r0 >= 0, "a generated puzzle has empty cells");
    sel_r = r0;
    sel_c = c0;
    hint();
    expect(cell[r0][c0] == sol[r0][c0], "hint writes the solution digit");
    expect(!err[r0][c0], "hint never creates a conflict");

    /* 5. A complete, consistent board is a win. */
    unsigned char full[9][9];
    rng_state = 5555;
    memset(full, 0, sizeof full);
    fill_board(full, 0);
    memcpy(given, full, sizeof given);
    memset(cell, 0, sizeof cell);
    mark_errors();
    for (int r = 0; r < 9; r++)
        for (int c = 0; c < 9; c++)
            expect(!err[r][c], "a solved board has no errors");
    recount();
    check_win();
    expect(win, "a complete consistent board wins");

    /* Leave the machine in a clean state for whatever runs next. */
    win = 0;
    sel_r = sel_c = -1;
    for (int r = 0; r < 9; r++)
        for (int c = 0; c < 9; c++)
            drawn_style[r][c] = -1;
    return failures;
}

/* ---- the shared menu ---------------------------------------------------- */

/* The controls that are not a digit live here too: NEW, ERASE, HINT and the
 * level. The shell's own QUIT tab ends the game. Digits stay on the board --
 * they are the thing you tap most, and a menu trip for each one is no good. */
enum { SUDOKU_NEW = 1, SUDOKU_ERASE, SUDOKU_HINT, SUDOKU_LEVEL };

#define LVL_X   24
#define LVL_W   552
#define LVL_H   72
#define LVL_Y(i) (140 + (i) * 84)

static int act_hit(int x, int y) {
    return kshell_inside(x, y, KFRAME_ACT_X, KFRAME_ACT_Y, KFRAME_ACT_W, KFRAME_ACT_H);
}

/* One line about the board, so a menu screen says what the action will hit. */
static void progress_line(char *dst, size_t n) {
    snprintf(dst, n, "filled %d/81, level %s", n_filled, DIFF_NAME[difficulty]);
}

static void cell_line(char *dst, size_t n) {
    if (sel_r < 0) snprintf(dst, n, "no cell picked -- tap the grid, then come back");
    else snprintf(dst, n, "the cell picked is row %d, column %d", sel_r + 1, sel_c + 1);
}

static int on_sudoku_item(int id) {
    switch (id) {
    case SUDOKU_NEW:   new_puzzle(); return 1;
    case SUDOKU_ERASE: erase_cell(); return 1;
    case SUDOKU_HINT:  hint();       return 1;
    case SUDOKU_LEVEL:
        return 1;        /* LEVEL acts on its own three buttons, not on a big one */
    }
    fprintf(stderr, "sudoku: menu item %d is not one of ours\n", id);
    return 1;
}

static void draw_new(const kshell_rect *r) {
    char line[64];
    progress_line(line, sizeof line);
    ek_text_scale(r->x, r->y, "NEW PUZZLE", EK_INK, 2);
    ek_text(r->x, r->y + 44, "throws this puzzle away and deals a fresh one", EK_INK);
    ek_text(r->x, r->y + 66, line, EK_INK);
    kshell_button(KFRAME_ACT_X, KFRAME_ACT_Y, KFRAME_ACT_W, KFRAME_ACT_H, "START", 0);
}

static int tap_new(const kshell_rect *r, int x, int y) {
    (void)r;
    if (!act_hit(x, y)) return 0;
    on_sudoku_item(SUDOKU_NEW);
    kframe_close();
    return 1;
}

static void draw_erase(const kshell_rect *r) {
    char line[64];
    cell_line(line, sizeof line);
    ek_text_scale(r->x, r->y, "ERASE", EK_INK, 2);
    ek_text(r->x, r->y + 44, "empties the picked cell. clues are locked.", EK_INK);
    ek_text(r->x, r->y + 66, line, EK_INK);
    kshell_button(KFRAME_ACT_X, KFRAME_ACT_Y, KFRAME_ACT_W, KFRAME_ACT_H, "ERASE CELL", 0);
}

static int tap_erase(const kshell_rect *r, int x, int y) {
    (void)r;
    if (!act_hit(x, y)) return 0;
    on_sudoku_item(SUDOKU_ERASE);
    kframe_close();
    return 1;
}

static void draw_hint(const kshell_rect *r) {
    char line[64];
    cell_line(line, sizeof line);
    ek_text_scale(r->x, r->y, "HINT", EK_INK, 2);
    ek_text(r->x, r->y + 44, "writes the answer into the empty cell", EK_INK);
    ek_text(r->x, r->y + 66, line, EK_INK);
    kshell_button(KFRAME_ACT_X, KFRAME_ACT_Y, KFRAME_ACT_W, KFRAME_ACT_H, "HINT ME", 0);
}

static int tap_hint(const kshell_rect *r, int x, int y) {
    (void)r;
    if (!act_hit(x, y)) return 0;
    on_sudoku_item(SUDOKU_HINT);
    kframe_close();
    return 1;
}

static void draw_level(const kshell_rect *r) {
    ek_text_scale(r->x, r->y, "LEVEL", EK_INK, 2);
    ek_text(r->x, r->y + 44, "a tap starts a fresh puzzle at that level", EK_INK);
    for (int i = 0; i < 3; i++)
        kshell_button(LVL_X, LVL_Y(i), LVL_W, LVL_H, DIFF_NAME[i], difficulty == i);
}

static int tap_level(const kshell_rect *r, int x, int y) {
    (void)r;
    for (int i = 0; i < 3; i++)
        if (kshell_inside(x, y, LVL_X, LVL_Y(i), LVL_W, LVL_H)) {
            difficulty = i;
            new_puzzle();
            kframe_close();
            return 1;
        }
    return 0;
}

static const kframe_item SUDOKU_ITEMS[] = {
    { "NEW",   SUDOKU_NEW,   draw_new,   tap_new },
    { "ERASE", SUDOKU_ERASE, draw_erase, tap_erase },
    { "HINT",  SUDOKU_HINT,  draw_hint,  tap_hint },
    { "LEVEL", SUDOKU_LEVEL, draw_level, tap_level },
};

static void kf_status(const char *msg) { fprintf(stderr, "sudoku: %s\n", msg); }
static void kf_quit(void) { running = 0; stop_req = 1; }

static int open_frame(void) {
    kframe_cfg kf;
    memset(&kf, 0, sizeof kf);
    kf.title = "SUDOKU";
    kf.app_id = "sudoku";
    kf.items = SUDOKU_ITEMS;
    kf.nitems = (int)(sizeof SUDOKU_ITEMS / sizeof SUDOKU_ITEMS[0]);
    kf.on_item = on_sudoku_item;
    kf.on_quit = kf_quit;
    kf.repaint = present;
    kf.status = kf_status;
    kf.stop = &stop_req;
    if (kframe_init(&kf) != 0) {
        fprintf(stderr, "sudoku: the shared menu would not start\n");
        return -1;
    }
    return 0;
}

/* ---- main --------------------------------------------------------------- */

static void usage(void) {
    fprintf(stderr,
            "usage: sudoku [options]\n"
            "\n"
            "  On the Kindle the panel is the game's until you tap QUIT, then SURE.\n"
            "  Tap a cell, then a digit 1-9. ERASE clears it, HINT writes the answer,\n"
            "  EASY / MED / HARD start a fresh puzzle at that difficulty, NEW restarts.\n"
            "  The hamburger at (572,26) has NEW / ERASE / HINT / LEVEL as tabs.\n"
            "  --selftest   check the generator and rules on this computer and exit\n"
            "  --auto N     fill N cells by itself, then stop\n"
            "  --dump FILE  write the picture (PGM) and exit\n"
            "  --menu-shot F  write the menu (PGM) and exit\n"
            "  --seed N     same seed, same puzzle\n"
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
            fprintf(stderr, "sudoku: clock_gettime failed\n");
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
        fprintf(stderr, "sudoku: the screen would not open\n");
        ek_restore_ui();
        return 1;
    }
    wfm_clean = ek_waveform_by_name("GC16");
    if (!wfm_clean) {
        fprintf(stderr, "sudoku: GC16 waveform is missing\n");
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
        /* Only changed 8x8 tiles reach the glass; clean the area that keeps
         * re-inking every 30 flushes. */
        ek_waveform(0);
        ek_ghost(6, 30);
        ek_policy(60, 4, 30);
    }

    rng_state = seed;
    int rc = 0;
    if (do_test) {
        rc = selftest() ? 1 : 0;
        new_puzzle();
        present();
    } else {
        new_puzzle();
        if (auto_n >= 0) auto_steps(auto_n);
        present();
    }

    if (menu_shot) {
        kframe_show_menu();
        kframe_paint();
        ek_flush();
        if (ek_dump_pgm(menu_shot, 1) != 0) {
            fprintf(stderr, "sudoku: could not write %s\n", menu_shot);
            ek_close();
            return 1;
        }
        ek_close();
        return 0;
    }

#ifdef EK_HOST
    /* A pipe of "tap X Y" lines plays the game, then --dump saves the picture. */
    if (!do_test && auto_n < 0 && !isatty(0)) {
        char buf[64];
        while (!stop_req && fgets(buf, sizeof buf, stdin)) {
            int x, y;
            if (buf[0] == 'q') break;
            if (sscanf(buf, "tap %d %d", &x, &y) != 2) {
                fprintf(stderr, "sudoku: want 'tap X Y' or q\n");
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
            fprintf(stderr, "sudoku: could not write %s\n", dump_path);
            ek_close();
            return 1;
        }
    }

    ek_close();
    return rc;
}
