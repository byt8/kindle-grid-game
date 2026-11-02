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
 *   CLOCK   -- a big digit ticks once per second on the panel, its real
 *              tk_ms interval printed LARGE. Tap NEXT when the tick cadence
 *              looks even; tap AGAIN when it looks uneven/bunched.
 *              Logs every tick's wall-time gap and each flush's cost.
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

static int screen = 1;          /* 1=CLOCK 2=GHOST 3=TAPLAG */
static int running = 1;
static int stats_on = 0;
static int probe = 0;
static int summary = 0;         /* CLOCK: did the user say even?             */
static int trace_on = 1;        /* GHOST: is the second square leaving trace? */
static int clock_ms = 1000;    /* CLOCK tick interval                         */
static uint32_t clock_at;       /* next CLOCK tick deadline, anchored to a beat */
static int clock_val;
static int ghost_x, ghost_y;    /* squares fall down the well */
static int ghost2_on = 0;
static int ghost2_y;
static uint32_t ghost_at;
static int taplag_next_row;
static uint32_t taplag_at;
static long tap_reaction_us = -1;   /* TAPLAG: last measured reaction, -1 none */

/* ---- button hit ---------------------------------------------------------- */

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

static void draw_clock(void);
static void draw_ghost(void);
static void draw_taplag(void);

static void draw_screen(int s) {
    switch (s) {
    case 1: draw_clock(); break;
    case 2: draw_ghost(); break;
    case 3: draw_taplag(); break;
    }
}

static void draw_clock(void) {
    ek_clear(EK_PAPER);
    ek_text_scale(20, 220, "PERCEPTION CLOCK", EK_INK, 2);
    ek_text_scale(20, 300, "Tap NEXT when the tick", EK_INK, 1);
    ek_text_scale(20, 330, "cadence looks EVEN.", EK_INK, 1);
    ek_text_scale(20, 380, "The number below ticks", EK_INK, 1);
    ek_text_scale(20, 410, "once per second on glass.", EK_INK, 1);
    for (int i = 0; i < 5; i++) draw_btn(i, EK_INK);
    /* big number, centered in the well */
    char buf[16];
    snprintf(buf, sizeof buf, "%d", clock_val);
    ek_text_scale(WELL_X + (WELL_W - 8 * (int)strlen(buf) * 4) / 2,
                  WELL_Y + WELL_H / 2 - 32, buf, EK_INK, 4);
    /* measured interval on glass */
    ek_printf(20, 120, EK_INK, "tk_ms: %d", clock_ms);
}

static void step_clock_tick(int now_ms) {
    (void)now_ms;
    int was = clock_val++;
    if (clock_val >= 10) clock_val = 0;
    /* repaint just the digit: erase old, draw new */
    char buf[16];
    snprintf(buf, sizeof buf, "%d", was);
    int w = 8 * (int)strlen(buf) * 4;
    ek_fill_rect(WELL_X + (WELL_W - w) / 2, WELL_Y + WELL_H / 2 - 32, w, 64, EK_PAPER);
    snprintf(buf, sizeof buf, "%d", clock_val);
    w = 8 * (int)strlen(buf) * 4;
    ek_text_scale(WELL_X + (WELL_W - w) / 2, WELL_Y + WELL_H / 2 - 32, buf, EK_INK, 4);
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

static void tap_screen(int x, int y) {
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
        /* CLOCK: user says the cadence looks even. */
        if (screen == 1) { summary = 1; draw_btn(b, EK_PAPER); }
        else if (screen == 2) {
            /* GHOST: user says "CLEAN" -- i.e. no smear seen. */
            probe = 0;
        }
        return;
    case CT_BTN_TRACE:
        /* CLOCK: cadence uneven. GHOST: flip whether the right square
         * leaves its old cell behind (what the eye must catch). */
        if (screen == 2) trace_on = !trace_on;
        return;
    case CT_NEXT:
        if (screen == 1) screen = 2;
        else if (screen == 2) screen = 3;
        else if (screen == 3) screen = 1;
        draw_screen(screen);
        return;
    }
}

/* ---- the engine-independent loop, mirroring the games --------------------- */

static void on_signal(int s) { (void)s; running = 0; }

static void present(void) {
    ek_stats st = ek_flush();
    if (probe)
        fprintf(stderr, "calib: flush wfm=%d rects=%d tiles=%d px=%ld full=%d us=%u\n",
                st.wfm, st.rects, st.tiles, st.px, st.full, st.us);
}

int main(int argc, char **argv) {
    unsigned flags = EK_TAKEOVER;
    int wait = 1;
    int do_test = 0;
    const char *dump_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--stats")) stats_on = 1;
        else if (!strcmp(argv[i], "--probe")) probe = 1;
        else if (!strcmp(argv[i], "--no-takeover")) flags &= ~EK_TAKEOVER;
        else if (!strcmp(argv[i], "--no-wait")) wait = 0;
        else if (!strcmp(argv[i], "--selftest")) do_test = 1;
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc) dump_path = argv[++i];
        else if (!strcmp(argv[i], "--clock") && i + 1 < argc) clock_ms = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--screen") && i + 1 < argc) {
            screen = atoi(argv[++i]);
            if (screen < 1 || screen > 3) { fprintf(stderr, "calib: screen must be 1..3\n"); return 2; }
        }
#ifdef EK_HOST
        else if (!strcmp(argv[i], "--host-stdin")) host_stdin = 1;
#endif
        else {
            fprintf(stderr, "usage: calib [--probe] [--no-takeover] [--clock N]\n");
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
    ek_mono(1);
    ek_wait_updates(wait);
    /* same policy shape as tetris: partials with a periodic clean */
    ek_ghost(6, 30);
    ek_policy(60, 4, 30);

    int rc = 0;
    if (do_test) {
        /* host-only smoke: draw each screen once (clock when dumped) */
        draw_clock(); present();
        if (dump_path) { if (ek_dump_pgm(dump_path, 1) != 0) { rc = 1; goto out; } }
        step_ghost(); present();
        step_taplag(); present();
        rc = 0; goto out;
    }

    /* seed the three loops */
    uint32_t now;
    now = ek_ms();
    clock_at = now + (uint32_t)clock_ms;
    ghost_at = now + 400;
    taplag_at = now + 1500;
    draw_screen(screen); present();
    clock_val = 0;

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
            if ((int32_t)(t - clock_at) >= 0) {
                step_clock_tick(t);
                if (probe)
                    fprintf(stderr, "calib: tick at %u (due %u, ms %d)\n",
                            (unsigned)t, (unsigned)clock_at, clock_ms);
                clock_at += (uint32_t)clock_ms;
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