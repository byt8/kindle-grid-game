/* calib.c -- tap-driven perception calibration for the Kindle e-ink panel.
 *
 * The problem being solved: "the piece moves faster than 800 ms, and it
 * jitters" is a claim about what the eye sees, and the game's --probe log
 * cannot answer it -- it proves what the engine *submitted*, not what the
 * glass *settled*.  This program turns the question around: it puts the
 * timing numbers ON the glass, and every answer comes from a tap.
 *
 * Four screens. The first one is the industry recipe, not another knob:
 *
 *   RX      -- two black squares step on the same beat. The left one is
 *              pushed with GC16 (the grey flash every "fast" grey mode is a
 *              shorter copy of). The right one is pushed with DU, the 1-bit
 *              direct update e-ink products use for motion. Each side prints
 *              how many milliseconds that zap took. This is the optician
 *              test: same picture, two standard lenses.
 *   TUNER   -- a block drops at a tunable speed and every knob is a REAL
 *              engine setting the games use: drop interval, waveform
 *              (AUTO / DU / GC16F / GL16F / GC16), how often a GC16 clean
 *              fires, and whether updates block until settled. The measured
 *              on-glass drop gap and last refresh cost are printed live.
 *   GHOST   -- two squares fall: one erases its previous cell cleanly, the
 *              other leaves its old position inked (a smear). You tap
 *              "TRAIL" if you actually see a smear, "CLEAN" if not.
 *              Logs which square was which, so the answer is really about
 *              your eyes, not about the machine.
 *   TAPLAG  -- a spot jumps to a new row the instant you are ready; you tap
 *              the instant you see it. Logs the wall-time the jump went out,
 *              the wall-time your tap registered, and the gap.  This is the
 *              number that says "I saw it after X ms and tapped".
 *   CHESS   -- the real chess board, at chess.c's own geometry, with the real
 *              baked piece sprites. Tap a piece and tap a square to move it;
 *              four chips tune the sprite live (SIZE, PAD, OFS, FULL) and the
 *              flush cost of every move is printed. This is where you find the
 *              piece size/offset that reads on the glass, then bake it back
 *              into src/chess.c.
 *
 * QUIT (bottom-right) + a confirm gives the panel back, like the games.
 *
 * Build like the other games: host with -DEK_HOST for a PC smoke-test
 * (no glass, dump-only), and the static-musl docker build for the Kindle.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include "eink.h"
/* The real baked piece art, so what you tweak here is what chess draws. */
#include "chess_pieces.h"

/* ---- layout (same shape as tetris: a well area + a row of buttons) ------- */

#define WELL_X 0
#define WELL_Y 0
#define WELL_W 600
#define WELL_H 520

/* Three big buttons along the bottom + QUIT on the right, exactly like the
 * games: a wide tap target, labelled in the 8x16 font. */
#define BTN_W 186
#define BTN_H 70
#define BTN_Y1 560
#define BTN_Y2 640
#define BTN_X1 0
#define BTN_X2 206
#define BTN_X3 412

enum { CT_QUIT = 0, CT_QUIT_SURE, CT_BTN_SUM, CT_BTN_TRACE, CT_NEXT };

typedef struct { int x, y; } pt;

static const pt BTN[5] = {
    {BTN_X1, BTN_Y1}, /* QUIT        */
    {BTN_X1, BTN_Y2}, /* SURE        */
    {BTN_X2, BTN_Y1}, /* summary: ✓/✗ */
    {BTN_X3, BTN_Y1}, /* trace: ON/OFF */
    {BTN_X3, BTN_Y2}, /* NEXT        */
};

static const char *B_LABEL[5] = {
    "QUIT", "SURE", "CLEAN", "TRACE ON/OFF", "NEXT",
};

/* ---- state --------------------------------------------------------------- */

static int screen = 4;          /* 4=RX 1=TUNER 2=GHOST 3=TAPLAG */
static int running = 1;
static int stats_on = 0;
static int probe = 0;
static int trace_on = 1;        /* GHOST: is the second square leaving trace? */
static int ghost_x, ghost_y;    /* squares fall down the well */

/* TUNER knobs: every one maps to a real engine setting the games use.
 * 0=AUTO is "let the engine pick". DU is the motion waveform. */
#define WFM_N 5
static struct { int id; const char *name; } wfm[WFM_N];
static int wfm_du;              /* forced on the RX ink square */
static int wfm_gc16;            /* forced on the RX grey square, and cleans */
static int knob_speed = 800;    /* ms between drops (tetris level-1 default)  */
static int knob_clean = 30;     /* GC16 clean every N drops, 0 = never        */
static int knob_wfm = 0;        /* index into wfm[] (0 = auto)                */
static int knob_wait = 1;       /* ek_wait_updates: block until settled       */
static int knob_mono = 1;       /* ek_mono                                    */
static int sel = 0;             /* 0=SPEED 1=CLEAN 2=WFM: what -/+ adjusts     */
static uint32_t drop_at;        /* next drop deadline, anchored like tetris   */
static int block_y = 60;        /* the falling block's row                     */
static int step_n = 0;          /* drop counter (drives the CLEAN cadence)     */
static uint32_t step_at;        /* wall time the last drop fired               */
static int knob_gap = 0;        /* measured ms between drops, on the glass     */
static int knob_flash_us = 0;   /* last flush cost, milliseconds (stats field is named us) */
static int steps_left = -1;     /* --steps N: quit after N moves; -1 = forever */
static int rx_flushed;          /* screen 4 already pushed its own updates */
static int rx_fail;             /* a forced waveform did not come back */
static int rx_n;                /* pairs completed */
static int rx_y1, rx_y2;
static int rx_grey_ms = -1, rx_ink_ms = -1;
/* The grey layer is only needed by the chess screen (screen 5). It must NOT be
 * on for RX: eink.c's grey_capable() clamps every non-GC16 waveform to GC16
 * while a grey canvas exists, so a forced DU comes back as GC16 and RX's own
 * "did the waveform I asked for come back?" check aborts the app. That is what
 * dropped the whole program to the reader after one step (2026-11-03). */
static int grey_on;             /* ek_grey_enable() has allocated the layer */
static int ch_grey_fail;        /* ek_grey_enable() returned -1             */
static int ghost2_on = 0;
static int ghost2_y;
static uint32_t ghost_at;
static int taplag_next_row;
static uint32_t taplag_at;
static long tap_reaction_us = -1;   /* TAPLAG: last measured reaction, -1 none */

/* VERDICT ring: the last N measured drop gaps (screen 1). With WAIT ON the
 * gap is the panel's REAL beat, so a gap that keeps running past SPEED is
 * proof the cadence is unholdable -- no eye required. */
#define GAP_RING 16
static int gap_ring[GAP_RING];
static int gap_n = 0, gap_i = 0;

/* ---- button hit ---------------------------------------------------------- */

/* TUNER buttons: two rows of three, same bboxes as the games' bottom row.
 *
 * The second row is full, so NEXT gets its own bar above it: without one the
 * tuner was a dead end (no path from screen 1 to 2/3/5 at all) -- found on the
 * glass 2026-11-03, where the chess screen could never be reached. */
enum { T_MINUS = 0, T_QUIT, T_PLUS, T_SPEED, T_CLEAN, T_WFM, T_NEXT };
#define TUNER_NEXT_X 0
#define TUNER_NEXT_Y 500
#define TUNER_NEXT_W 250
#define TUNER_NEXT_H 58
#define CHIP_Y 30
#define CHIP_H 46
static int chip_wait(int x, int y) { return x >= 20 && x < 210 && y >= CHIP_Y && y < CHIP_Y + CHIP_H; }
static int chip_mono(int x, int y) { return x >= 230 && x < 400 && y >= CHIP_Y && y < CHIP_Y + CHIP_H; }

static int hit_tuner(int x, int y) {
    int col = -1;
    if (x >= TUNER_NEXT_X && x < TUNER_NEXT_X + TUNER_NEXT_W &&
        y >= TUNER_NEXT_Y && y < TUNER_NEXT_Y + TUNER_NEXT_H)
        return T_NEXT;
    if (x >= BTN_X1 && x < BTN_X1 + BTN_W) col = 0;
    else if (x >= BTN_X2 && x < BTN_X2 + BTN_W) col = 1;
    else if (x >= BTN_X3 && x < BTN_X3 + BTN_W) col = 2;
    if (col < 0) return -1;
    if (y >= BTN_Y1 && y < BTN_Y1 + BTN_H) return col;           /* - QUIT + */
    if (y >= BTN_Y2 && y < BTN_Y2 + BTN_H) return 3 + col;        /* S C W    */
    return -1;
}

static int hit(int x, int y) {
    for (int i = 0; i < 5; i++)
        if (x >= BTN[i].x && x < BTN[i].x + BTN_W &&
            y >= BTN[i].y && y < BTN[i].y + BTN_H)
            return i;
    return -1;
}

static void draw_btn(int i, int ink) {
    ek_rect(BTN[i].x, BTN[i].y, BTN_W, BTN_H, ink);
    ek_text_scale(BTN[i].x + 12, BTN[i].y + (BTN_H - 16) / 2, B_LABEL[i], ink, 1);
}

/* ---- screens ------------------------------------------------------------- */

static int host_stdin = 0;   /* --host-stdin: read 'tap X Y' lines like the games (host only) */

static void draw_tuner(void);
static void draw_ghost(void);
static void draw_taplag(void);
static void draw_rx(void);
static void ch_draw(void);

/* Screen 4 must not let the engine swap in its own GL16/GC16 cadence:
 * each square is one waveform, on purpose. Other screens use tetris's. */
static void apply_screen_policy(int s) {
    if (s == 4) {
        /* RX forces DU. A grey canvas makes eink.c submit GC16 instead, so
         * the RX comparison cannot be made once the layer is allocated:
         * say so plainly rather than letting the flush check look like a bug. */
        if (grey_on)
            fprintf(stderr, "calib: the grey layer is allocated, so DU cannot be "
                            "forced; restart the app for the RX screen\n");
        ek_ghost(0, 0);
        ek_wait_updates(1);
        ek_mono(1);
    } else if (s == 5) {
        /* Exactly chess.c's engine setup. No ek_mono(1) ever: it forces
         * EPDC_FLAG_FORCE_MONOCHROME and collapses the sprite's grey levels
         * to flat black/white on the real panel (host cannot show this).
         * The layer is allocated here and only here, so the RX screen keeps
         * its plain DU: chess's blit is the only consumer of ek_grey_rect(). */
        if (!grey_on) {
            if (ek_grey_enable() != 0) ch_grey_fail = 1;
            else grey_on = 1;
        }
        ek_ghost(4, 30);
        ek_policy(180, 6, 35);
        ek_wait_updates(1);
        ek_mono(0);
    } else {
        ek_ghost(6, 30);
        ek_wait_updates(knob_wait);
        ek_mono(knob_mono);
    }
}

static void draw_screen(int s) {
    apply_screen_policy(s);
    switch (s) {
    case 1: draw_tuner(); break;
    case 2: draw_ghost(); break;
    case 3: draw_taplag(); break;
    case 4: draw_rx(); break;
    case 5: ch_draw(); break;
    }
}

static void paint_chips(void);   /* prototype for tap_tuner */
static void paint_tuner_rows(void);
static void draw_tuner(void) {
    ek_clear(EK_PAPER);
    paint_chips();
    ek_text_scale(20, 92, "TUNER: make it look EVEN", EK_INK, 2);
    ek_text_scale(20, 134, "Pick SPEED/CLEAN/WFM, then -", EK_INK, 1);
    ek_text_scale(20, 158, "and +. Watch the block drop.", EK_INK, 1);
    paint_tuner_rows();
    /* reference frame + the falling block, far right so the value rows
     * (which run to ~x=448 at scale 2) never touch it */
    ek_rect(470, 48, 110, 470, EK_INK);
    ek_fill_rect(482, block_y, 44, 44, EK_INK);
    /* NEXT: the tuner's only way off this screen (see T_NEXT). */
    ek_rect(TUNER_NEXT_X, TUNER_NEXT_Y, TUNER_NEXT_W, TUNER_NEXT_H, EK_INK);
    ek_text_scale(TUNER_NEXT_X + 12, TUNER_NEXT_Y + (TUNER_NEXT_H - 16) / 2,
                  "NEXT", EK_INK, 1);
    /* buttons */
    static const char *tl[6] = {"-", "QUIT", "+", "SPEED", "CLEAN", "WFM"};
    for (int i = 0; i < 6; i++) {
        int x = i % 3 == 0 ? BTN_X1 : i % 3 == 1 ? BTN_X2 : BTN_X3;
        int y = i < 3 ? BTN_Y1 : BTN_Y2;
        ek_rect(x, y, BTN_W, BTN_H, EK_INK);
        ek_text_scale(x + 12, y + (BTN_H - 16) / 2, tl[i], EK_INK, 1);
    }
}

static void paint_chips(void) {
    ek_fill_rect(20, CHIP_Y, 380, CHIP_H, EK_PAPER);
    ek_rect(20, CHIP_Y, 190, CHIP_H, EK_INK);
    ek_printf(32, CHIP_Y + 14, EK_INK, "WAIT %s", knob_wait ? "ON " : "OFF");
    ek_rect(230, CHIP_Y, 170, CHIP_H, EK_INK);
    ek_printf(242, CHIP_Y + 14, EK_INK, "MONO %s", knob_mono ? "ON " : "OFF");
}

static void paint_tuner_rows(void) {
    /* value area only -- clear of the reference well (x>=250) */
    ek_fill_rect(20, 224, 230, 300, EK_PAPER);
    char buf[72];
    snprintf(buf, sizeof buf, "%s SPEED %d ms", sel == 0 ? ">" : " ", knob_speed);
    ek_text_scale(32, 240, buf, EK_INK, 2);
    snprintf(buf, sizeof buf, "%s CLEAN %d", sel == 1 ? ">" : " ", knob_clean);
    ek_text_scale(32, 300, buf, EK_INK, 2);
    snprintf(buf, sizeof buf, "%s WFM %s", sel == 2 ? ">" : " ", wfm[knob_wfm].name);
    ek_text_scale(32, 360, buf, EK_INK, 2);
    /* st.us is an ek_ms() delta, so it is already milliseconds. */
    snprintf(buf, sizeof buf, "GAP %d ms FLASH %d ms", knob_gap, knob_flash_us);
    ek_text_scale(32, 430, buf, EK_INK, 2);

    /* VERDICT: numbers, not vibes. When the measured gap keeps running past
     * SPEED the panel cannot hold the beat -- raise SPEED, or if a grey
     * waveform is forced, switch to DU first (the zap is the cost). */
    if (gap_n >= 4) {
        int m = gap_n < 8 ? gap_n : 8;
        long sum = 0;
        for (int k = 0; k < m; k++)
            sum += gap_ring[(gap_i - m + k + GAP_RING) % GAP_RING];
        long avg = sum / m;
        int grey = knob_wfm == 2 || knob_wfm == 3 || knob_wfm == 4;
        if (avg > knob_speed + 60L) {
            ek_text_scale(32, 462, "PANEL LAGGING: GAP > SPEED", EK_INK, 1);
            char v[64];
            if (grey)
                snprintf(v, sizeof v, "grey zap is the cost: use DU first");
            else
                snprintf(v, sizeof v, "raise SPEED to %ld ms", avg + 50L);
            ek_text_scale(32, 482, v, EK_INK, 1);
        } else {
            ek_text_scale(32, 462, "PANEL KEEPS THE BEAT", EK_INK, 1);
        }
    }
}

static void step_tuner(uint32_t now) {
    (void)now;
    uint32_t t = ek_ms();
    if (step_at) {
        knob_gap = (int)(t - step_at);   /* includes the last refresh's wait */
        gap_ring[gap_i] = knob_gap;
        gap_i = (gap_i + 1) % GAP_RING;
        if (gap_n < GAP_RING) gap_n++;
    }
    step_at = t;
    ek_fill_rect(482, block_y, 44, 44, EK_PAPER);
    block_y += 30;
    if (block_y >= 440) block_y = 60;
    ek_fill_rect(482, block_y, 44, 44, EK_INK);
    step_n++;
}

static void draw_ghost(void) {
    ek_clear(EK_PAPER);
    ek_text_scale(20, 60, "GHOST TEST", EK_INK, 2);
    ek_text_scale(20, 130, "Two squares drop. Left", EK_INK, 1);
    ek_text_scale(20, 160, "always erases cleanly.", EK_INK, 1);
    /* left square at x=200, right at x=400, each 3x3 cells tall */
    ghost_x = 200; ghost_y = 20;
    ghost2_on = 0; ghost2_y = 20;
    for (int i = 0; i < 5; i++) draw_btn(i, EK_INK);
    ek_printf(20, 250, EK_INK, "trace_on: %d", trace_on);
}

static void step_ghost(void) {
    /* left square: erase its top cell, draw its bottom cell (clean) */
    ek_fill_rect(ghost_x, ghost_y, 30, 30, EK_PAPER);
    ghost_y += 30;
    if (ghost_y >= WELL_H - 120) ghost_y = 20;
    ek_fill_rect(ghost_x, ghost_y, 30, 30, EK_INK);
    /* right square: if trace_on, leave its old cell inked (the smear) */
    if (trace_on) {
        ek_fill_rect(400, ghost2_y, 30, 30, EK_INK);
    } else {
        ek_fill_rect(400, ghost2_y, 30, 30, EK_PAPER);
    }
    ghost2_y += 30;
    if (ghost2_y >= WELL_H - 120) ghost2_y = 20;
    /* draw the current cell of the right square */
    ek_fill_rect(400, ghost2_y, 30, 30, EK_INK);
}

static void draw_taplag(void) {
    ek_clear(EK_PAPER);
    ek_text_scale(20, 60, "TAP-LAG TEST", EK_INK, 2);
    ek_text_scale(20, 130, "A spot jumps to a NEW row", EK_INK, 1);
    ek_text_scale(20, 160, "when you are ready. Tap the", EK_INK, 1);
    ek_text_scale(20, 190, "instant you see it move.", EK_INK, 1);
    if (tap_reaction_us >= 0)
        ek_printf(20, 260, EK_INK, "last: %ld us", tap_reaction_us);
    else
        ek_text_scale(20, 260, "last: none yet", EK_INK, 1);
    for (int i = 0; i < 5; i++) draw_btn(i, EK_INK);
    /* draw the spot at its current row */
    taplag_next_row = 200;
    ek_fill_rect(40, taplag_next_row, 40, 4, EK_INK);
}

static void step_taplag(void) {
    /* erase old spot, draw at a new random-ish row */
    ek_fill_rect(40, taplag_next_row, 40, 4, EK_PAPER);
    taplag_next_row = 60 + (int)((ek_ms() * 7919u) % 400u);
    ek_fill_rect(40, taplag_next_row, 40, 4, EK_INK);
}

/* ---- RX: same square, two standard waveforms ----------------------------- */

#define RX_WELL_Y 168
#define RX_WELL_H 390
#define RX_BW 64
#define RX_X1 98
#define RX_X2 438

static void paint_rx_stats(void) {
    char buf[64];
    ek_fill_rect(16, 566, 568, 84, EK_PAPER);
    if (rx_grey_ms < 0)
        ek_text_scale(24, 568, "1 GREY  -- ms", EK_INK, 2);
    else {
        snprintf(buf, sizeof buf, "1 GREY  %d ms", rx_grey_ms);
        ek_text_scale(24, 568, buf, EK_INK, 2);
    }
    if (rx_ink_ms < 0)
        ek_text_scale(24, 608, "2 INK   -- ms", EK_INK, 2);
    else {
        snprintf(buf, sizeof buf, "2 INK   %d ms", rx_ink_ms);
        ek_text_scale(24, 608, buf, EK_INK, 2);
    }
    if (rx_n < 1)
        ek_text_scale(360, 608, "BEAT --", EK_INK, 2);
    else {
        snprintf(buf, sizeof buf, "BEAT %d", knob_gap);
        ek_text_scale(360, 608, buf, EK_INK, 2);
    }
}

static void draw_rx(void) {
    ek_clear(EK_PAPER);
    ek_text_scale(24, 24, "WHICH ONE IS STEADY?", EK_INK, 2);
    ek_text_scale(24, 68, "Same black square. Same beat.", EK_INK, 1);
    ek_text_scale(24, 88, "1 is the grey zap. It blinks.", EK_INK, 1);
    ek_text_scale(24, 108, "2 is plain ink. It jumps.", EK_INK, 1);
    ek_text_scale(24, 132, "2 is how e-ink motion is done.", EK_INK, 1);
    ek_text_scale(48, 152, "1 GREY", EK_INK, 1);
    ek_text_scale(388, 152, "2 INK", EK_INK, 1);
    ek_rect(40, RX_WELL_Y, 180, RX_WELL_H, EK_INK);
    ek_rect(380, RX_WELL_Y, 180, RX_WELL_H, EK_INK);
    rx_y1 = rx_y2 = RX_WELL_Y + 16;
    ek_fill_rect(RX_X1, rx_y1, RX_BW, RX_BW, EK_INK);
    ek_fill_rect(RX_X2, rx_y2, RX_BW, RX_BW, EK_INK);
    paint_rx_stats();
    ek_rect(0, 660, 290, 90, EK_INK);
    ek_text_scale(24, 696, "QUIT", EK_INK, 2);
    ek_rect(310, 660, 290, 90, EK_INK);
    ek_text_scale(334, 696, "NEXT", EK_INK, 2);
}

static int flush_forced(int wfm, const char *tag, int need_pixels) {
    ek_stats st;
    ek_waveform(wfm);
    st = ek_flush();
    ek_waveform(0);
    if (probe)
        fprintf(stderr, "calib: rx %s wfm=%d rects=%d us=%u\n",
                tag, st.wfm, st.rects, st.us);
    if ((need_pixels && st.rects < 1) || (st.rects > 0 && st.wfm != wfm)) {
        fprintf(stderr,
                "calib: %s flush failed: wanted wfm %d and pixels, got wfm %d rects %d\n",
                tag, wfm, st.wfm, st.rects);
        rx_fail = 1;
        running = 0;
        return -1;
    }
    return (int)st.us;
}

static void move_rx_block(int x, int *y) {
    ek_fill_rect(x, *y, RX_BW, RX_BW, EK_PAPER);
    *y += 32;
    if (*y > RX_WELL_Y + RX_WELL_H - RX_BW - 12)
        *y = RX_WELL_Y + 16;
    ek_fill_rect(x, *y, RX_BW, RX_BW, EK_INK);
}

/* Left square goes out alone as GC16, then the right square as DU.
 * A third DU push writes the numbers, which are known only after both zaps. */
static void step_rx(void) {
    int g, k;
    rx_flushed = 1;
    move_rx_block(RX_X1, &rx_y1);
    g = flush_forced(wfm_gc16, "grey", 1);
    if (g < 0) return;
    rx_grey_ms = g;
    move_rx_block(RX_X2, &rx_y2);
    paint_rx_stats();
    k = flush_forced(wfm_du, "ink", 1);
    if (k < 0) return;
    rx_ink_ms = k;
    rx_n++;
    paint_rx_stats();
    (void)flush_forced(wfm_du, "nums", 0);
}

static int rx_hit(int x, int y) {
    if (y < 660 || y >= 750) return -1;
    if (x < 290) return CT_QUIT;
    if (x >= 310) return CT_NEXT;
    return -1;
}

/* ---- CHESS: the real board, the real sprites, live sprite knobs ---------- */
/* Geometry and draw are copied from src/chess.c on purpose: the number you
 * find here is the number chess will use. SQ_PX/BOARD_X/BOARD_Y are chess's. */

#define SQ_PX    68
#define BOARD_X  28
#define BOARD_Y  64
#define BOARD_END (BOARD_Y + 8 * SQ_PX)

enum { PAWN = 1, KNIGHT, BISHOP, ROOK, QUEEN, KING };

/* bottom keypad: three rows of three, right under the board */
#define CB_W 186
#define CB_H 52
#define CB_Y1 612
#define CB_Y2 668
#define CB_Y3 724
enum { C_MINUS = 0, C_QUIT, C_PLUS, C_SIZE, C_OFSX, C_OFSY, C_WFM, C_FULL, C_NEXT };
static const char *C_LABEL[9] = {
    "-", "QUIT", "+", "SIZE", "OFS X", "OFS Y", "WFM", "FULL", "NEXT",
};

static int ch_bd[64];            /* +type white, -type black, 0 empty */
static int ch_sel = -1;          /* selected square, -1 = none        */
static int ch_size = 52;         /* drawn piece size, px (chess: 52)  */
static int ch_ofsx = 0;          /* nudge on top of the centring      */
static int ch_ofsy = 0;
static int ch_wfmi = 2;          /* index into wfm[]; 2 = GC16F       */
static int ch_full = 1;          /* ek_full_refresh() the squares a move touched */
static int ch_moves;
static uint32_t ch_move_at;
static int ch_gap;               /* ms between the last two moves      */
static int ch_flash;             /* ms cost of the last flush          */
static int ch_last_wfm;          /* waveform the engine really submitted */
static int ch_csel = 0;          /* 0=SIZE 1=OFSX 2=OFSY 3=WFM        */

static void ch_reset(void) {
    static const int back[8] = {ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK};
    memset(ch_bd, 0, sizeof ch_bd);
    for (int f = 0; f < 8; f++) {
        ch_bd[f] = back[f];                     /* rank 1, white */
        ch_bd[8 + f] = PAWN;
        ch_bd[48 + f] = -PAWN;
        ch_bd[56 + f] = -back[f];               /* rank 8, black */
    }
    ch_sel = -1;
}

static int ch_sprite(int type, int black) {
    switch (type) {
    case PAWN:   return black ? CHS_BPAWN   : CHS_WPAWN;
    case ROOK:   return black ? CHS_BROOK   : CHS_WROOK;
    case KNIGHT: return black ? CHS_BKNIGHT : CHS_WKNIGHT;
    case BISHOP: return black ? CHS_BBISHOP : CHS_WBISHOP;
    case QUEEN:  return black ? CHS_BQUEEN  : CHS_WQUEEN;
    case KING:   return black ? CHS_BKING   : CHS_WKING;
    default:     return -1;
    }
}

/* Same blit as chess.c's draw_piece: one grey pixel at a time, tile scaled
 * to `size`, art centred inside the square plus the OFS nudge. */
static void ch_piece(int type, int black, int sqx, int sqy) {
    int id = ch_sprite(type, black);
    if (id < 0) return;
    int size = ch_size;
    if (size > SQ_PX) size = SQ_PX;
    int ox = sqx + (SQ_PX - size) / 2 + ch_ofsx;
    int oy = sqy + (SQ_PX - size) / 2 + ch_ofsy;
    for (int dy = 0; dy < size; dy++) {
        int sy = dy * CHESS_TILE / size;
        for (int dx = 0; dx < size; dx++) {
            int sx = dx * CHESS_TILE / size;
            int v = ch_at(id, sx, sy);
            if (v == 0) continue;
            int level = (v - CHESS_PAPER) * 15 / (CHESS_TONE_MAX - 1);
            ek_grey_rect(ox + dx, oy + dy, 1, 1, level);
        }
    }
}

static void ch_sq_xy(int sq, int *x, int *y) {
    *x = BOARD_X + (sq & 7) * SQ_PX;
    *y = BOARD_Y + (7 - (sq >> 3)) * SQ_PX;
}

static int ch_sq_at(int x, int y) {
    if (x < BOARD_X || x >= BOARD_X + 8 * SQ_PX) return -1;
    if (y < BOARD_Y || y >= BOARD_END) return -1;
    return (7 - (y - BOARD_Y) / SQ_PX) * 8 + (x - BOARD_X) / SQ_PX;
}

static void advance_screen(void);   /* NEXT on the chess screen */

static void ch_draw(void) {
    char buf[72];
    ek_clear(EK_PAPER);

    /* top bar 0..64, exactly the strip chess leaves free above the board */
    ek_text_scale(0, 0, "CHESS SPRITE CALIB", EK_INK, 2);
    snprintf(buf, sizeof buf, "SIZE %d OFS %+d/%+d WFM %s%s",
             ch_size, ch_ofsx, ch_ofsy, wfm[ch_wfmi].name, ch_full ? " FULL" : "");
    ek_text_scale(0, 34, buf, EK_INK, 1);
    snprintf(buf, sizeof buf, "GAP %d FLASH %d", ch_gap, ch_flash);
    ek_text_scale(0, 48, buf, EK_INK, 1);
    if (ch_grey_fail)
        ek_text_scale(300, 48, "GREY LAYER FAILED: DITHERED", EK_INK, 1);
    else {
        snprintf(buf, sizeof buf, "MOVE %d wfm %d", ch_moves, ch_last_wfm);
        ek_text_scale(380, 48, buf, EK_INK, 1);
    }

    /* board: dark squares dithered, 1px borders, like chess.c */
    for (int r = 0; r < 8; r++)
        for (int f = 0; f < 8; f++) {
            int x = BOARD_X + f * SQ_PX, y = BOARD_Y + (7 - r) * SQ_PX;
            if ((f + r) & 1) ek_dither_rect(x, y, SQ_PX, SQ_PX, 3);
            else ek_fill_rect(x, y, SQ_PX, SQ_PX, EK_PAPER);
            ek_rect(x, y, SQ_PX, SQ_PX, EK_INK);
        }
    /* selection: the same triple ring chess draws */
    if (ch_sel >= 0) {
        int x, y;
        ch_sq_xy(ch_sel, &x, &y);
        ek_rect(x, y, SQ_PX, SQ_PX, EK_INK);
        ek_rect(x + 1, y + 1, SQ_PX - 2, SQ_PX - 2, EK_INK);
        ek_rect(x + 2, y + 2, SQ_PX - 4, SQ_PX - 4, EK_INK);
    }
    for (int sq = 0; sq < 64; sq++) {
        int pc = ch_bd[sq];
        if (!pc) continue;
        int x, y;
        ch_sq_xy(sq, &x, &y);
        ch_piece(pc > 0 ? pc : -pc, pc < 0, x, y);
    }

    for (int i = 0; i < 9; i++) {
        int x = (i % 3 == 0) ? 0 : (i % 3 == 1) ? 206 : 412;
        int y = i < 3 ? CB_Y1 : i < 6 ? CB_Y2 : CB_Y3;
        int on = (i == C_SIZE && ch_csel == 0) || (i == C_OFSX && ch_csel == 1) ||
                 (i == C_OFSY && ch_csel == 2) || (i == C_WFM && ch_csel == 3) ||
                 (i == C_FULL && ch_full);
        ek_rect(x, y, CB_W, CB_H, EK_INK);
        if (on) ek_rect(x + 3, y + 3, CB_W - 6, CB_H - 6, EK_INK);
        ek_text_scale(x + 12, y + (CB_H - 16) / 2, C_LABEL[i], EK_INK, 1);
    }
}

static int ch_cell(int x, int y) {
    int col = x < 206 ? 0 : x < 412 ? 1 : 2;
    if (y >= CB_Y1 && y < CB_Y1 + CB_H) return col;
    if (y >= CB_Y2 && y < CB_Y2 + CB_H) return 3 + col;
    if (y >= CB_Y3 && y < CB_Y3 + CB_H) return 6 + col;
    return -1;
}

static void tap_chess(int x, int y) {
    int b = ch_cell(x, y);
    if (b >= 0) {
        switch (b) {
        case C_QUIT:
            running = 0;
            return;
        case C_MINUS:
            if (ch_csel == 0) { if (ch_size > 36) ch_size -= 2; }
            else if (ch_csel == 1) { if (ch_ofsx > -8) ch_ofsx--; }
            else if (ch_csel == 2) { if (ch_ofsy > -8) ch_ofsy--; }
            else ch_wfmi = (ch_wfmi + WFM_N - 1) % WFM_N;
            break;
        case C_PLUS:
            if (ch_csel == 0) { if (ch_size < SQ_PX) ch_size += 2; }
            else if (ch_csel == 1) { if (ch_ofsx < 8) ch_ofsx++; }
            else if (ch_csel == 2) { if (ch_ofsy < 8) ch_ofsy++; }
            else ch_wfmi = (ch_wfmi + 1) % WFM_N;
            break;
        case C_SIZE: ch_csel = 0; break;
        case C_OFSX: ch_csel = 1; break;
        case C_OFSY: ch_csel = 2; break;
        case C_WFM:  ch_csel = 3; break;
        case C_FULL: ch_full = !ch_full; break;
        case C_NEXT:
            advance_screen();
            return;
        default:
            return;
        }
        ch_draw();
        return;
    }
    /* a tap on the board: pick up, then put down. No rules -- this is a
     * sprite/eye test, not a game, and moving a piece anywhere is the point. */
    int sq = ch_sq_at(x, y);
    if (sq < 0) return;
    if (ch_sel < 0) { if (ch_bd[sq]) ch_sel = sq; }
    else if (sq == ch_sel) ch_sel = -1;
    else {
        uint32_t now = ek_ms();
        if (ch_move_at) ch_gap = (int)(now - ch_move_at);
        ch_move_at = now;
        ch_bd[sq] = ch_bd[ch_sel];
        ch_bd[ch_sel] = 0;
        if (ch_full) {
            /* chess's clean_mark: one genuine UPDATE_MODE_FULL over the two
             * squares, clipped by the engine to what actually differs. */
            int x1, y1, x2, y2;
            ch_sq_xy(ch_sel, &x1, &y1);
            ch_sq_xy(sq, &x2, &y2);
            int bx = x1 < x2 ? x1 : x2, by = y1 < y2 ? y1 : y2;
            ek_full_refresh(bx, by, (x1 > x2 ? x1 : x2) + SQ_PX - bx,
                            (y1 > y2 ? y1 : y2) + SQ_PX - by);
        }
        ch_moves++;
        ch_sel = -1;
    }
    ch_draw();
}

/* ---- per-tap actions ------------------------------------------------------ */

static void tap_tuner(int x, int y) {
    /* the two chips above the well apply engine settings immediately */
    if (chip_wait(x, y)) { knob_wait = !knob_wait; ek_wait_updates(knob_wait); paint_chips(); return; }
    if (chip_mono(x, y)) { knob_mono = !knob_mono; ek_mono(knob_mono); paint_chips(); return; }
    int b = hit_tuner(x, y);
    if (b < 0) return;
    int speed_changed = 0;
    switch (b) {
    case T_QUIT:
        running = 0;
        return;
    case T_MINUS:
        if (sel == 0) { if (knob_speed > 200) { knob_speed -= 50; speed_changed = 1; } }
        else if (sel == 1) { if (knob_clean > 0) knob_clean -= 10; }
        else knob_wfm = (knob_wfm + WFM_N - 1) % WFM_N;
        break;
    case T_PLUS:
        if (sel == 0) { if (knob_speed < 2000) { knob_speed += 50; speed_changed = 1; } }
        else if (sel == 1) { if (knob_clean < 90) knob_clean += 10; }
        else knob_wfm = (knob_wfm + 1) % WFM_N;
        break;
    case T_SPEED: sel = 0; break;   /* selecting a knob never disturbs the beat */
    case T_CLEAN: sel = 1; break;
    case T_WFM:   sel = 2; break;
    case T_NEXT:
        advance_screen();
        return;
    default: return;
    }
    /* Only a real SPEED change re-anchors the metronome. Selecting a knob or
     * adjusting CLEAN/WFM in mid-drop used to reset the timer here, which made
     * the block visibly pause on every tap. */
    if (speed_changed) drop_at = ek_ms() + (uint32_t)knob_speed;
    paint_tuner_rows();
}

static void advance_screen(void) {
    if (screen == 4) screen = 1;
    else if (screen == 1) screen = 2;
    else if (screen == 2) screen = 3;
    else if (screen == 3) screen = 5;
    else if (grey_on) {
        /* RX forces DU, and eink.c clamps a forced DU to GC16 for as long as a
         * grey canvas exists. ek_grey_enable() cannot be undone, so once the
         * chess screen has allocated it RX cannot be measured in this run.
         * Skip it with a reason instead of flashing an abort on the glass. */
        fprintf(stderr, "calib: skipping RX, the chess screen left the grey layer "
                        "on and RX needs a 1-bit canvas to force DU\n");
        screen = 1;
    } else screen = 4;
    step_n = 0;
    step_at = 0;
    knob_gap = 0;
    rx_n = 0;
    rx_grey_ms = -1;
    rx_ink_ms = -1;
    drop_at = ek_ms() + (uint32_t)knob_speed;
    draw_screen(screen);
}

static void tap_screen(int x, int y) {
    int b;
    if (screen == 1) { tap_tuner(x, y); return; }
    if (screen == 5) { tap_chess(x, y); return; }
    if (screen == 4) b = rx_hit(x, y);
    else b = hit(x, y);
    if (b < 0) return;
    switch (b) {
    case CT_QUIT:
        running = 0;
        return;
    case CT_QUIT_SURE:
        /* reached only from QUIT: real app asks SURE. Not wired into the
         * simple flow here; the games two-tap, this prototype just QUITs
         * from the QUIT button. */
        return;
    case CT_BTN_SUM:
        /* screen 2: user says "CLEAN" -- i.e. no smear seen. */
        if (screen == 2) probe = 0;
        return;
    case CT_BTN_TRACE:
        /* screen 2: flip whether the right square leaves its old cell
         * behind (what the eye must catch). */
        if (screen == 2) trace_on = !trace_on;
        return;
    case CT_NEXT:
        advance_screen();
        return;
    }
}

/* ---- the engine-independent loop, mirroring the games --------------------- */

static void on_signal(int s) { (void)s; running = 0; }

static void present(void) {
    ek_stats st;
    if (screen == 4 && rx_flushed) {
        /* step_rx already submitted the two waveforms. A second flush here
         * would send leftover pixels with whatever mode the engine picked. */
        rx_flushed = 0;
        return;
    }
    if (screen == 1) {
        /* TUNER: the CLEAN knob fires a real GC16 clean every N drops, like
         * the game's clean frames; the WFM knob forces a waveform otherwise. */
        int clean = knob_clean > 0 && (step_n % knob_clean == 0);
        ek_waveform(clean ? wfm_gc16 : (knob_wfm ? wfm[knob_wfm].id : 0));
        st = ek_flush();
        ek_waveform(0);
        knob_flash_us = (int)st.us; /* milliseconds; the field is named us */
    } else if (screen == 5) {
        /* CHESS: the WFM knob forces a waveform, 0 = let the engine pick.
         * A grey canvas is clamped to GC16 by grey_capable(), so a forced
         * DU does not sneak in where the sprites would lose their tones. */
        ek_waveform(ch_wfmi ? wfm[ch_wfmi].id : 0);
        st = ek_flush();
        ek_waveform(0);
        ch_flash = (int)st.us;
        ch_last_wfm = st.wfm;
    } else {
        st = ek_flush();
    }
    if (probe)
        fprintf(stderr, "calib: flush wfm=%d rects=%d tiles=%d px=%ld full=%d us=%u\n",
                st.wfm, st.rects, st.tiles, st.px, st.full, st.us);
}

int main(int argc, char **argv) {
    unsigned flags = EK_TAKEOVER;
    int do_test = 0;
    const char *dump_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--stats")) stats_on = 1;
        else if (!strcmp(argv[i], "--probe")) probe = 1;
        else if (!strcmp(argv[i], "--no-takeover")) flags &= ~EK_TAKEOVER;
        else if (!strcmp(argv[i], "--selftest")) do_test = 1;
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc) dump_path = argv[++i];
        else if (!strcmp(argv[i], "--speed") && i + 1 < argc) knob_speed = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--screen") && i + 1 < argc) {
            screen = atoi(argv[++i]);
            if (screen < 1 || screen > 5) { fprintf(stderr, "calib: screen must be 1..5\n"); return 2; }
        }
        else if (!strcmp(argv[i], "--steps") && i + 1 < argc) {
            steps_left = atoi(argv[++i]);
            if (steps_left < 1) { fprintf(stderr, "calib: --steps needs a number > 0\n"); return 2; }
        }
#ifdef EK_HOST
        else if (!strcmp(argv[i], "--host-stdin")) host_stdin = 1;
#endif
        else {
            fprintf(stderr, "usage: calib [--probe] [--no-takeover] [--speed N] [--screen 1..5] [--steps N]\n");
            return 2;
        }
    }

    /* AUTO is id 0 (engine picks). Everything else must be a real mode.
     * DU is the 1-bit motion waveform. GC16 is the slow clean flash. */
    wfm[0].id = 0;                                 wfm[0].name = "AUTO";
    wfm[1].id = ek_waveform_by_name("DU");         wfm[1].name = "DU";
    wfm[2].id = ek_waveform_by_name("GC16F");      wfm[2].name = "GC16F";
    wfm[3].id = ek_waveform_by_name("GL16F");      wfm[3].name = "GL16F";
    wfm[4].id = ek_waveform_by_name("GC16");       wfm[4].name = "GC16";
    wfm_du = wfm[1].id;
    wfm_gc16 = wfm[4].id;
    for (int i = 1; i < WFM_N; i++) {
        if (wfm[i].id == 0) {
            fprintf(stderr, "calib: waveform %s is missing\n", wfm[i].name);
            return 2;
        }
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    if (ek_open(flags) != 0) {
        fprintf(stderr, "calib: the screen would not open\n");
        ek_restore_ui();
        return 1;
    }
    /* The grey layer is enabled by apply_screen_policy(5), not here: the RX
     * screen needs a 1-bit canvas or the engine refuses to submit DU at all. */
    ch_reset();
    ek_mono(knob_mono);
    ek_wait_updates(knob_wait);
    /* same policy shape as tetris until a screen applies its own */
    ek_ghost(6, 30);
    ek_policy(60, 4, 30);
    apply_screen_policy(screen);

    int rc = 0;
    if (do_test) {
        /* host-only smoke: draw each screen once (clock when dumped) */
        draw_tuner(); present();
        if (dump_path) { if (ek_dump_pgm(dump_path, 1) != 0) { rc = 1; goto out; } }
        step_ghost(); present();
        step_taplag(); present();
        apply_screen_policy(5);   /* the grey layer is screen 5's, not main's */
        ch_draw(); present();
        rc = 0; goto out;
    }

    /* seed the three loops */
    uint32_t now;
    now = ek_ms();
    drop_at = now + (uint32_t)knob_speed;
    ghost_at = now + 400;
    taplag_at = now + 1500;
    draw_screen(screen); present();

    if (dump_path && !host_stdin) {   /* one-frame snapshot, like tetris --dump */
        if (ek_dump_pgm(dump_path, 1) != 0) rc = 1;
        goto out;
    }

    while (running) {
#ifdef EK_HOST
        /* Host test driver: read 'tap X Y' lines from stdin, closing when EOF. */
        if (host_stdin) {
            char buf[64];
            if (!fgets(buf, sizeof buf, stdin)) break;
            if (buf[0] == 'q') break;
            int x, y;
            if (sscanf(buf, "tap %d %d", &x, &y) == 2) {
                if (screen == 3 && tap_reaction_us < 0) {
                    long now_us = (long)ek_ms();
                    tap_reaction_us = (now_us - (long)taplag_at) * 1000L;
                }
                tap_screen(x, y);
                present();
                continue;
            }
        }
#endif
        ek_event ev;
        int got = 0;

        /* drain the touch queue: a DOWN is a tap in this app */
        while (ek_poll(&ev)) {
#ifndef EK_HOST
            if (ev.type == EK_DOWN) {
                if (screen == 3 && tap_reaction_us < 0) {
                    long now_us = (long)ek_ms();
                    tap_reaction_us = (now_us - (long)taplag_at) * 1000L;
                }
                tap_screen(ev.x, ev.y);
                got = 1;
            }
#endif
        }

        uint32_t t = ek_ms();

        switch (screen) {
        case 1:
            if ((int32_t)(t - drop_at) >= 0) {
                step_tuner(t);
                if (probe)
                    fprintf(stderr, "calib: drop at %u (due %u, ms %d)\n",
                            (unsigned)t, (unsigned)drop_at, knob_speed);
                drop_at += (uint32_t)knob_speed;   /* anchor like tetris */
                got = 1;
            }
            break;
        case 2:
            if ((int32_t)(t - ghost_at) >= 0) {
                step_ghost();
                ghost_at += 400;
                got = 1;
            }
            break;
        case 3:
            if ((int32_t)(t - taplag_at) >= 0) {
                step_taplag();
                taplag_at += 1500;
                got = 1;
            }
            break;
        case 4:
            if ((int32_t)(t - drop_at) >= 0) {
                if (step_at) knob_gap = (int)(t - step_at);
                step_at = t;
                step_rx();
                {
                    uint32_t done = ek_ms();
                    drop_at += (uint32_t)knob_speed;
                    /* A pair slower than the beat must not queue a burst. */
                    if ((int32_t)(done - drop_at) >= 0)
                        drop_at = done;
                }
                got = 1;
            }
            break;
        }
        if (got && steps_left > 0 && --steps_left == 0) running = 0;
        if (got) present();
        else ek_sleep_ms(15);
    }

    if (dump_path) {
        if (ek_dump_pgm(dump_path, 1) != 0) rc = 1;
    }

out:
    if (rx_fail) rc = 1;
    ek_close();
    return rc;
}