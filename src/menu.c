/* menu.c -- the Kindle's home screen: XCOM / CHESS / TETRIS / SUDOKU / ROCKMAN /
 * KOREADER / WIFI / KINDLE.
 *
 * This is what the Kindle boots into instead of the Amazon UI. It shares the
 * e-ink engine with the games and the same takeover rules:
 *
 *   - EK_TAKEOVER stops the framework, stops the `x` job (blanket is its
 *     child and would keep repainting the splash over us -- the same fix chess
 *     uses), and grabs /dev/input/event3. The screen mirror (kfbterm) holds
 *     the grab first, so menu-boot.sh stops it before this runs (the same
 *     fight the game runners solve). We then paint one forced full white
 *     frame so the boot splash cannot linger on the glass.
 *   - When a game is chosen we do NOT keep the touch grab: the game needs it
 *     and would fail its own grab with EBUSY. The menu instead draws the
 *     "starting..." card, flushes, then exits; the boot job starts the game.
 *     The grab is gone the moment this process exits, so the game's own
 *     ek_open(TAKEOVER) succeeds.
 *   - Exit codes tell the boot job what to launch:
 *        0  back to the Kindle (restore the UI)
 *        1  XCOM
 *        2  CHESS
 *        3  TETRIS
 *        4  SUDOKU
 *        5  ROCKMAN
 *        6  KOREADER
 *        7  WIFI
 *      143  a runner stopped us (SIGTERM). Do not restore the reader.
 *   - The KINDLE button restores the Amazon UI; the menu comes back after any
 *     game ends because the boot job restarts it.
 *
 * Host build (EK_HOST): draws to a canvas, reads `tap X Y` lines on stdin, and
 * --dump writes the picture -- so the layout is checkable without a device.
 * Button geometry in host mode matches the panel exactly (600x800), so a tap
 * script that walks the menu works on both.
 */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "eink.h"

static void usage(void) {
    fprintf(stderr,
        "usage: menu [--dump FILE] [--list] [--no-takeover]\n"
        "  --dump FILE  draw the menu, write the picture (PGM), and exit\n"
        "  --list       print the button rects as \"x y w h name\" and exit\n"
        "  --no-takeover  do not hold the Kindle's screen (host testing)\n"
        "\n"
        "On the Kindle it owns the panel until a button is tapped, then exits "
        "with:\n"
        "  0 back to the Kindle UI, 1 XCOM, 2 CHESS, 3 TETRIS, 4 SUDOKU,\n"
        "  5 ROCKMAN, 6 KOREADER, 7 WIFI\n");
}

/* Eight buttons. Six apps, then WIFI and KINDLE. */
enum { B_XCOM = 0, B_CHESS, B_TETRIS, B_SUDOKU, B_ROCKMAN, B_KOREADER, B_WIFI, B_KINDLE, B_N };

/* Geometry (panel is 600x800). Kept in one place so the drawing, the hit test
 * and --list all agree. */
#define MX     28
#define LIST_T  170        /* top of the game buttons */
#define ROW_H   72
#define BTN_H   64
#define WIFI_T  600
#define WIFI_H  54
#define KB_T    660
#define KB_H    54
#define B_W      (600 - 2 * MX)

static int g_signal = 0;

static void on_signal(int s) { (void)s; g_signal = 1; }

static int btn_top(int i) {
    switch (i) {
        case B_XCOM:   return LIST_T;
        case B_CHESS:  return LIST_T + ROW_H;
        case B_TETRIS:  return LIST_T + 2 * ROW_H;
        case B_SUDOKU:  return LIST_T + 3 * ROW_H;
        case B_ROCKMAN: return LIST_T + 4 * ROW_H;
        case B_KOREADER: return LIST_T + 5 * ROW_H;
        case B_WIFI:    return WIFI_T;
        default:       return KB_T;        /* B_KINDLE */
    }
}
static int btn_w(int i) {
    return (i == B_KINDLE) ? B_W - 120 : (i == B_WIFI) ? B_W - 180 : B_W;
}
static int btn_x(int i) {
    return MX + (B_W - btn_w(i)) / 2;
}
static int btn_h(int i) {
    return (i == B_WIFI || i == B_KINDLE) ? WIFI_H : BTN_H;
}

static const char *btn_name(int i) {
    static const char *names[B_N] = {
        "XCOM", "CHESS", "TETRIS", "SUDOKU", "ROCKMAN", "KOREADER", "WIFI", "KINDLE"
    };
    return names[i];
}

static void draw_menu(void) {
    ek_clear(EK_PAPER);
    ek_text_scale(24, 18, "GAMES", EK_INK, 3);
    ek_text(36, 84, "tap to play", EK_INK);

    for (int i = 0; i < B_N; i++) {
        int x = btn_x(i), y = btn_top(i), w = btn_w(i), h = btn_h(i);
        ek_rect(x, y, w, h, EK_INK);
        ek_text_scale(x + 20, y + (h - 24) / 2, btn_name(i), EK_INK, 2);
    }
    ek_flush();
}

static int tap_button(int x, int y) {
    for (int i = 0; i < B_N; i++) {
        int bx = btn_x(i), by = btn_top(i);
        if (x >= bx && x < bx + btn_w(i) && y >= by && y < by + btn_h(i))
            return i;
    }
    return -1;
}

static void draw_launch(const char *what) {
    ek_clear(EK_PAPER);
    ek_text_scale(48, 340, what, EK_INK, 3);
    ek_text(60, 430, "starting...", EK_INK);
    ek_flush();
}

int main(int argc, char **argv) {
    unsigned flags = EK_TAKEOVER;
    const char *dump_path = NULL;
    int list_only = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dump") && i + 1 < argc) {
            dump_path = argv[++i];
            if (!dump_path[0]) { usage(); return 2; }
        } else if (!strcmp(argv[i], "--list")) list_only = 1;
        else if (!strcmp(argv[i], "--no-takeover")) flags &= ~EK_TAKEOVER;
        else { usage(); return 2; }
    }

    if (list_only) {
        for (int i = 0; i < B_N; i++)
            printf("%d %d %d %d %s\n",
                   btn_x(i), btn_top(i), btn_w(i), btn_h(i), btn_name(i));
        return 0;
    }

    if (ek_open(flags) != 0) {
        fprintf(stderr, "menu: could not open the e-ink panel\n");
        return 1;
    }
    /* Full black-then-white GC16 pair to reset every e-ink particle, eradicating
     * the framework's progress bar / boot splash from the glass. */
    ek_clear(EK_INK);
    ek_waveform(ek_waveform_by_name("GC16"));
    ek_force_full();
    ek_flush();
    ek_clear(EK_PAPER);
    ek_flush();
    ek_waveform(0);

    if (dump_path) {
        draw_menu();
        if (ek_dump_pgm(dump_path, 1) != 0) {
            fprintf(stderr, "menu: could not write %s\n", dump_path);
            ek_close();
            return 1;
        }
        ek_close();
        return 0;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    int rc = 0;
    int chosen = -1;
    draw_menu();

#ifdef EK_HOST
    if (!isatty(0)) {
        char buf[64];
        while (fgets(buf, sizeof buf, stdin)) {
            int x, y;
            if (buf[0] == 'q') break;
            if (sscanf(buf, "tap %d %d", &x, &y) != 2) {
                fprintf(stderr, "menu: want 'tap X Y' or q\n");
                ek_close();
                return 2;
            }
            int b = tap_button(x, y);
            if (b < 0) continue;
            chosen = b;
            break;
        }
    } else {
#endif
        for (;;) {
            if (g_signal) break;
            ek_event ev;
            if (ek_poll(&ev)) {
                if (ev.type != EK_DOWN) continue;
                int b = tap_button(ev.x, ev.y);
                if (b < 0) continue;
                chosen = b;
                break;
            }
            ek_sleep_ms(10);
        }
#ifdef EK_HOST
    }
#endif

    if (g_signal && access("/mnt/us/.menu-handoff", F_OK) == 0) {
        /* A game runner stamped .menu-handoff and then stopped us so it
         * can take the panel. This is not the KINDLE button. ek_close()
         * starts the reader, and menu-boot.sh would restore over the game.
         * Dying releases the touch grab. The reader stays down. */
        _exit(143);
    }

    if (chosen == B_KINDLE || (chosen < 0 && rc == 0)) {
        /* KINDLE button, or the host test closed stdin: hand the panel back.
         * ek_close() runs release_ui(), which starts the framework again. */
        ek_clear(EK_PAPER);
        ek_force_full();
        ek_flush();
        ek_close();
        return 0;
    }

    if (chosen >= 0) {
        rc = chosen + 1;                    /* 1 XCOM .. 5 ROCKMAN, 6 KOREADER, 7 WIFI */
        draw_launch(btn_name(chosen));
        ek_sleep_ms(300);
        /* Let go of the touch device and the panel, so the game's own
         * ek_open(TAKEOVER) succeeds. The grab is gone at exit. */
        ek_close();
        _exit(rc);
    }

    ek_close();
    return rc;
}
