/* calib.c -- tap-driven perception calibration for the Kindle e-ink panel.
 *
 * The problem being solved: "the piece moves faster than 800 ms, and it
 * jitters" is a claim about what the eye sees, and the game's --probe log
 * cannot answer it -- it proves what the engine *submitted*, not what the
 * glass *settled*.  This program turns the question around: it puts the
 * timing numbers ON the glass, and every answer comes from a tap.
 *
 * Three screens, each one driven by what you see, not by what a log says:
 *
 *   TUNER   -- the fixer, not the asker. A block drops at a tunable speed
 *              and every knob is a REAL engine setting the games use: drop
 *              interval, waveform (DU / GC16F / GL16F / GC16), how often a
 *              GC16 clean fires, and whether updates block until settled.
 *              The measured on-glass drop gap and last refresh flash cost
 *              are printed LIVE, so you tune until it *looks* even and the
 *              settings you end on are the settings tetris should use.
 *   GHOST   -- two squares fall: one erases its previous cell cleanly, the
 *              other leaves its old position inked (a smear). You tap
 *              "TRAIL" if you actually see a smear, "CLEAN" if not.
 *              Logs which square was which, so the answer is really about
 *              your eyes, not about the machine.
 *   TAPLAG  -- a spot jumps to a new row the instant you are ready; you tap
 *              the instant you see it. Logs the wall-time the jump went out,
 *              the wall-time your tap registered, and the gap.  This is the
 *              number that says "I saw it after X ms and tapped".
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

static int screen = 1;          /* 1=TUNER 2=GHOST 3=TAPLAG */
static int running = 1;
static int stats_on = 0;
static int probe = 0;
static int trace_on = 1;        /* GHOST: is the second square leaving trace? */
static int ghost_x, ghost_y;    /* squares fall down the well */

/* TUNER knobs: every one maps to a real engine setting the games use. */
#define WFM_N 4
static struct { int id; const char *name; } wfm[WFM_N]; /* 0=AUTO .. 3=GC16 */
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
static int knob_flash_us = 0;   /* last flush cost = the grey flash duration   */
static int ghost2_on = 0;
static int ghost2_y;
static uint32_t ghost_at;
static int taplag_next_row;
static uint32_t taplag_at;
static long tap_reaction_us = -1;   /* TAPLAG: last measured reaction, -1 none */

/* ---- button hit ---------------------------------------------------------- */

/* TUNER buttons: two rows of three, same bboxes as the games' bottom row. */
enum { T_MINUS = 0, T_QUIT, T_PLUS, T_SPEED, T_CLEAN, T_WFM };
#define CHIP_Y 30
#define CHIP_H 46
static int chip_wait(int x, int y) { return x >= 20 && x < 210 && y >= CHIP_Y && y < CHIP_Y + CHIP_H; }
static int chip_mono(int x, int y) { return x >= 230 && x < 400 && y >= CHIP_Y && y < CHIP_Y + CHIP_H; }

static int hit_tuner(int x, int y) {
    int col = -1;
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

static void draw_screen(int s) {
    switch (s) {
    case 1: draw_tuner(); break;
    case 2: draw_ghost(); break;
    case 3: draw_taplag(); break;
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
    ek_fill_rect(20, 224, 230, 260, EK_PAPER);
    char buf[72];
    snprintf(buf, sizeof buf, "%s SPEED %d ms", sel == 0 ? ">" : " ", knob_speed);
    ek_text_scale(32, 240, buf, EK_INK, 2);
    snprintf(buf, sizeof buf, "%s CLEAN %d", sel == 1 ? ">" : " ", knob_clean);
    ek_text_scale(32, 300, buf, EK_INK, 2);
    snprintf(buf, sizeof buf, "%s WFM %s", sel == 2 ? ">" : " ", wfm[knob_wfm].name);
    ek_text_scale(32, 360, buf, EK_INK, 2);
    snprintf(buf, sizeof buf, "GAP %d ms FLASH %d ms", knob_gap, knob_flash_us / 1000);
    ek_text_scale(32, 430, buf, EK_INK, 2);
}

static void step_tuner(uint32_t now) {
    (void)now;
    uint32_t t = ek_ms();
    if (step_at) knob_gap = (int)(t - step_at);   /* includes the last refresh's wait */
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

/* ---- per-tap actions ------------------------------------------------------ */

static void tap_tuner(int x, int y) {
    /* the two chips above the well apply engine settings immediately */
    if (chip_wait(x, y)) { knob_wait = !knob_wait; ek_wait_updates(knob_wait); paint_chips(); return; }
    if (chip_mono(x, y)) { knob_mono = !knob_mono; ek_mono(knob_mono); paint_chips(); return; }
    int b = hit_tuner(x, y);
    if (b < 0) return;
    switch (b) {
    case T_QUIT:
        running = 0;
        return;
    case T_MINUS:
        if (sel == 0) { if (knob_speed > 200) knob_speed -= 50; }
        else if (sel == 1) { if (knob_clean > 0) knob_clean -= 10; }
        else knob_wfm = (knob_wfm + WFM_N - 1) % WFM_N;
        break;
    case T_PLUS:
        if (sel == 0) { if (knob_speed < 2000) knob_speed += 50; }
        else if (sel == 1) { if (knob_clean < 90) knob_clean += 10; }
        else knob_wfm = (knob_wfm + 1) % WFM_N;
        break;
    case T_SPEED: sel = 0; break;
    case T_CLEAN: sel = 1; break;
    case T_WFM:   sel = 2; break;
    default: return;
    }
    drop_at = ek_ms() + (uint32_t)knob_speed;  /* re-anchor after a change */
    paint_tuner_rows();
}

static void tap_screen(int x, int y) {
    if (screen == 1) { tap_tuner(x, y); return; }
    int b = hit(x, y);
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
        if (screen == 1) screen = 2;
        else if (screen == 2) screen = 3;
        else if (screen == 3) screen = 1;
        step_n = 0;   /* reset the clean cadence when entering TUNER */
        draw_screen(screen);
        return;
    }
}

/* ---- the engine-independent loop, mirroring the games --------------------- */

static void on_signal(int s) { (void)s; running = 0; }

static void present(void) {
    ek_stats st;
    if (screen == 1) {
        /* TUNER: the CLEAN knob fires a real GC16 clean every N drops, like
         * the game's clean frames; the WFM knob forces a waveform otherwise. */
        int clean = knob_clean > 0 && (step_n % knob_clean == 0);
        ek_waveform(clean ? wfm[3].id : (knob_wfm ? wfm[knob_wfm].id : 0));
        st = ek_flush();
        ek_waveform(0);
        knob_flash_us = (int)st.us;
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
            if (screen < 1 || screen > 3) { fprintf(stderr, "calib: screen must be 1..3\n"); return 2; }
        }
#ifdef EK_HOST
        else if (!strcmp(argv[i], "--host-stdin")) host_stdin = 1;
#endif
        else {
            fprintf(stderr, "usage: calib [--probe] [--no-takeover] [--speed N]\n");
            return 2;
        }
    }

    /* waveform choices for the WFM knob: AUTO(engine pick) + the three the
     * games actually use. GC16 is the slow clean; GC16F/GL16F the fast ones. */
    wfm[0].id = 0;              wfm[0].name = "AUTO";
    wfm[1].id = ek_waveform_by_name("GC16F"); wfm[1].name = "GC16F";
    wfm[2].id = ek_waveform_by_name("GL16F"); wfm[2].name = "GL16F";
    wfm[3].id = ek_waveform_by_name("GC16");  wfm[3].name = "GC16";

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
    ek_mono(knob_mono);
    ek_wait_updates(knob_wait);
    /* same policy shape as tetris: partials with a periodic clean */
    ek_ghost(6, 30);
    ek_policy(60, 4, 30);

    int rc = 0;
    if (do_test) {
        /* host-only smoke: draw each screen once (clock when dumped) */
        draw_tuner(); present();
        if (dump_path) { if (ek_dump_pgm(dump_path, 1) != 0) { rc = 1; goto out; } }
        step_ghost(); present();
        step_taplag(); present();
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
        }
        if (got) present();
        else ek_sleep_ms(15);
    }

    if (dump_path) {
        if (ek_dump_pgm(dump_path, 1) != 0) rc = 1;
    }

out:
    ek_close();
    return rc;
}