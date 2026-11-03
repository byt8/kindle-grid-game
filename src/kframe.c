#define _GNU_SOURCE
/* kframe.c -- the Kindle bits every game should share.
 *
 * Hamburger, quit, volume, wi-fi, and the power-key sleep. The game draws
 * its own board and hands this file a list of extra menu rows.
 *
 * The power key is not an input device on the Kindle Touch. It writes one
 * `pmic: PWRON1 event` line into the kernel log. The PMIC_IRQ counter is the
 * wrong signal: it also counts the charger, the RTC, USB, and the wake from
 * suspend itself, and using it put a game to sleep with nobody touching it.
 * This kernel (2.6.31) has a /dev/kmsg node and no driver behind it, so the
 * log is read with klogctl(READ_ALL), which does not eat the buffer.
 */
#include "kframe.h"
#include "eink.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/klog.h>
#include <sys/wait.h>
#include <unistd.h>

#define WIFI_SCRIPT "/mnt/us/chess-wifi.sh"   /* historical name; every app uses it */

#define PWR_STATE        "/sys/power/state"
#define PWR_LOG_READ_ALL 3
#define PWR_LOG_SIZE     10
#define PWR_LOG_MAX      (256 * 1024)
#define PWR_LOG_EVERY_MS 150

static kframe_cfg cfg;
static int cfg_ok;

static int page;          /* 0 board, 1 menu, 2 volume */
static int quit_armed;
static int vol_pct = -1; /* last level amixer actually reported; -1 = unknown */
static char vol_err[240];

static int pwr_latch;

static int need_cfg(const char *what) {
    if (cfg_ok) return 0;
    fprintf(stderr, "kframe: %s called before kframe_init\n", what);
    return -1;
}

int kframe_init(const kframe_cfg *c) {
    if (!c || !c->title || !c->repaint || !c->on_item || !c->on_quit || !c->stop
        || c->nitems < 0 || (c->nitems > 0 && !c->items)) {
        fprintf(stderr, "kframe: init is missing title, repaint, on_item, on_quit, stop, or items\n");
        cfg_ok = 0;
        return -1;
    }
    cfg = *c;
    cfg_ok = 1;
    page = 0;
    quit_armed = 0;
    return 0;
}

int kframe_covering(void) { return page != 0; }

void kframe_show_menu(void) {
    page = 1;
    quit_armed = 0;
}

/* ---- small drawing ---------------------------------------------------- */

static void box(int x, int y, int w, int h, const char *label) {
    int tw = (int)strlen(label) * 8;
    ek_fill_rect(x, y, w, h, EK_PAPER);
    ek_rect(x, y, w, h, EK_INK);
    ek_rect(x + 1, y + 1, w - 2, h - 2, EK_INK);
    ek_text(x + (w - tw) / 2, y + (h - 16) / 2, label, EK_INK);
}

static void wrap(const char *s, int x, int y, int maxchars, int lines) {
    char buf[600];
    snprintf(buf, sizeof buf, "%s", s ? s : "");
    char *p = buf;
    for (int n = 0; n < lines && *p; n++) {
        char *cut = p;
        if (strlen(p) > (size_t)maxchars) {
            cut = p + maxchars;
            while (cut > p && *cut != ' ') cut--;
            if (cut == p) cut = p + maxchars;
        } else {
            cut = p + strlen(p);
        }
        char save = *cut;
        *cut = 0;
        ek_text(x, y + n * 18, p, EK_INK);
        if (!save) break;
        p = cut + 1;
        while (*p == ' ') p++;
    }
}

static int row_y(int i) {
    return KFRAME_ROW_Y0 + i * (KFRAME_ROW_H + KFRAME_ROW_GAP);
}

/* App rows sit under a "THIS GAME" label, one gap below WIFI. */
static int app_y(int i) {
    return row_y(3) + 20 + i * (KFRAME_ROW_H + KFRAME_ROW_GAP);
}

static int in_row(int x, int y, int ry) {
    return x >= KFRAME_ROW_X && x < KFRAME_ROW_X + KFRAME_ROW_W
        && y >= ry && y < ry + KFRAME_ROW_H;
}

static void paint_tab(void) {
    int x = KFRAME_TAB_X, y = KFRAME_TAB_Y, w = KFRAME_TAB_W, h = KFRAME_TAB_H;
    ek_fill_rect(x, y, w, h, EK_PAPER);
    ek_rect(x, y, w, h, EK_INK);
    int bx = x + 8, bw = w - 16;
    ek_fill_rect(bx, y + 5, bw, 3, EK_INK);
    ek_fill_rect(bx, y + 12, bw, 3, EK_INK);
    ek_fill_rect(bx, y + 19, bw, 3, EK_INK);
}

static void paint_menu(void) {
    ek_clear(EK_PAPER);
    ek_text_scale(24, 12, "MENU", EK_INK, 2);
    ek_text(140, 20, cfg.title ? cfg.title : "", EK_INK);
    ek_text(24, 58, "the same on every game", EK_INK);

    box(KFRAME_ROW_X, row_y(0), KFRAME_ROW_W, KFRAME_ROW_H,
        quit_armed ? "SURE?" : "QUIT");
    box(KFRAME_ROW_X, row_y(1), KFRAME_ROW_W, KFRAME_ROW_H, "VOLUME");
    box(KFRAME_ROW_X, row_y(2), KFRAME_ROW_W, KFRAME_ROW_H, "WIFI");

    ek_text(24, row_y(3) + 2, "THIS GAME", EK_INK);
    int n = cfg.nitems;
    int hidden = 0;
    for (int i = 0; i < n; i++) {
        int y = app_y(i);
        if (y + KFRAME_ROW_H > 700) { hidden++; continue; }
        box(KFRAME_ROW_X, y, KFRAME_ROW_W, KFRAME_ROW_H,
            cfg.items[i].label ? cfg.items[i].label : "?");
    }
    if (hidden) {
        fprintf(stderr, "kframe: %d menu item%s did not fit on the page\n",
                hidden, hidden == 1 ? "" : "s");
        ek_text(24, 692, "some menu items did not fit", EK_INK);
    }
    box(24, 720, 200, 52, "BACK");
}

static void paint_volume(void) {
    ek_clear(EK_PAPER);
    ek_text_scale(24, 16, "VOLUME", EK_INK, 2);
    if (vol_pct >= 0) {
        char num[16];
        snprintf(num, sizeof num, "%d%%", vol_pct);
        int tw = (int)strlen(num) * 8 * 4;
        ek_text_scale((EK_W - tw) / 2, 150, num, EK_INK, 4);
        ek_text(24, 240, "speaker, from the Kindle mixer", EK_INK);
    } else {
        ek_text(24, 80, "the mixer did not give a level", EK_INK);
        wrap(vol_err[0] ? vol_err : "no error text came back", 24, 110, 68, 6);
    }
    box(24, 300, 180, 70, "-");
    box(396, 300, 180, 70, "+");
    box(24, 720, 200, 52, "BACK");
}

void kframe_paint(void) {
    if (need_cfg("paint") != 0) return;
    if (page == 1) paint_menu();
    else if (page == 2) paint_volume();
    else paint_tab();
}

/* ---- volume: amixer, no invented level -------------------------------- */

static int run_sh(const char *cmd, char *out, size_t n) {
    int op[2];
    if (pipe(op) != 0) { snprintf(out, n, "could not make a pipe"); return -1; }
    pid_t pid = fork();
    if (pid < 0) {
        snprintf(out, n, "could not start a shell");
        close(op[0]); close(op[1]);
        return -1;
    }
    if (pid == 0) {
        close(op[0]);
        dup2(op[1], 1);
        dup2(op[1], 2);
        close(op[1]);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    close(op[1]);
    size_t got = 0;
    ssize_t r;
    while ((r = read(op[0], out + got, n - 1 - got)) > 0) {
        got += (size_t)r;
        if (got >= n - 1) break;
    }
    if (got >= n - 1) {
        char junk[256];
        while (read(op[0], junk, sizeof junk) > 0) { }
        fprintf(stderr, "kframe: command output was cut at %zu bytes\n", n - 1);
    }
    out[got] = 0;
    close(op[0]);
    int st = 0;
    if (waitpid(pid, &st, 0) != pid) return -1;
    if (!WIFEXITED(st)) return -1;
    return WEXITSTATUS(st);
}

/* First [NN%] in amixer's report. -1 when there isn't one. */
static int parse_pct(const char *s) {
    for (const char *p = s; *p; p++) {
        if (*p != '[') continue;
        if (p[1] < '0' || p[1] > '9') continue;
        char *end = NULL;
        long v = strtol(p + 1, &end, 10);
        if (end && end[0] == '%' && end[1] == ']' && v >= 0 && v <= 100) return (int)v;
    }
    return -1;
}

static void vol_fail(const char *cmd, const char *buf, int rc) {
    vol_pct = -1;
    snprintf(vol_err, sizeof vol_err, "%.200s",
             buf && buf[0] ? buf : "amixer said nothing");
    char *nl = strchr(vol_err, '\n');
    if (nl) *nl = 0;
    fprintf(stderr, "kframe: volume: %s (exit %d) from: %s\n", vol_err, rc, cmd);
}

/* Read Speaker. On failure vol_pct stays -1 and vol_err says why. */
static void vol_read(void) {
    const char *cmd = "amixer sget Speaker";
    char buf[1024];
    int rc = run_sh(cmd, buf, sizeof buf);
    int pct = parse_pct(buf);
    if (rc != 0 || pct < 0) {
        vol_fail(cmd, buf, rc);
        return;
    }
    vol_pct = pct;
    vol_err[0] = 0;
    fprintf(stderr, "kframe: volume: %d%%\n", vol_pct);
}

/* Set Speaker to pct. Then read it back. A failed set does not pretend. */
static void vol_set(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    char cmd[64];
    snprintf(cmd, sizeof cmd, "amixer sset Speaker %d%%", pct);
    char buf[1024];
    int rc = run_sh(cmd, buf, sizeof buf);
    if (rc != 0) {
        vol_fail(cmd, buf, rc);
        return;
    }
    vol_read();
}

/* ---- wi-fi screens (moved out of chess.c) ----------------------------- */

#define W_MAX   12
#define W_KB_X  22
#define W_KEY   56
#define W_KB_Y  150
#define W_ROW_H 50
#define W_BOT   (W_KB_Y + 4 * 60)
#define W_BOT2  (W_KB_Y + 4 * 60 + 64)

typedef struct { char ssid[64]; int sig; int secure; } wnet;
static wnet g_nets[W_MAX];
static int  g_nnets;
static char g_wifi_ssid[64];
static char g_wifi_ip[32];
static char g_wifi_err[240];

static void shq(char *out, size_t n, const char *in) {
    size_t o = 0;
    if (o + 1 < n) out[o++] = '\'';
    for (const char *p = in; *p && o + 5 < n; p++) {
        if (*p == '\'') {
            out[o++] = '\''; out[o++] = '\\'; out[o++] = '\''; out[o++] = '\'';
        } else {
            out[o++] = *p;
        }
    }
    if (o + 1 < n) out[o++] = '\'';
    out[o] = 0;
}

int kframe_wifi_status(void) {
    char buf[512];
    g_wifi_ssid[0] = 0;
    g_wifi_ip[0] = 0;
    int rc = run_sh("sh " WIFI_SCRIPT " status", buf, sizeof buf);
    for (char *line = buf; line && *line; ) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        if (!strncmp(line, "ssid=", 5)) snprintf(g_wifi_ssid, sizeof g_wifi_ssid, "%s", line + 5);
        if (!strncmp(line, "ip=", 3))   snprintf(g_wifi_ip, sizeof g_wifi_ip, "%s", line + 3);
        line = nl ? nl + 1 : NULL;
    }
    if (rc != 0) g_wifi_ssid[0] = 0;
    return rc == 0 ? 0 : -1;
}

static int wifi_scan(void) {
    char buf[4096];
    g_nnets = 0;
    int rc = run_sh("sh " WIFI_SCRIPT " scan", buf, sizeof buf);
    if (rc != 0) {
        snprintf(g_wifi_err, sizeof g_wifi_err, "%.200s",
                 buf[0] ? buf : "the scan failed (is wpa_supplicant running?)");
        char *nl = strchr(g_wifi_err, '\n');
        if (nl) *nl = 0;
        fprintf(stderr, "kframe: wi-fi scan: %s\n", g_wifi_err);
        return -1;
    }
    char *save = NULL;
    for (char *line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = 0;
        char *sig = tab + 1;
        char *tab2 = strchr(sig, '\t');
        if (tab2) *tab2 = 0;
        if (g_nnets >= W_MAX) break;
        snprintf(g_nets[g_nnets].ssid, sizeof g_nets[0].ssid, "%s", line);
        g_nets[g_nnets].sig = atoi(sig);
        g_nets[g_nnets].secure = tab2 ? strcmp(tab2 + 1, "open") != 0 : 1;
        g_nnets++;
    }
    if (!g_nnets) {
        snprintf(g_wifi_err, sizeof g_wifi_err, "no networks found - tap RESCAN");
        fprintf(stderr, "kframe: wi-fi scan: %s\n", g_wifi_err);
        return -1;
    }
    g_wifi_err[0] = 0;
    return 0;
}

static int wifi_join(const char *ssid, const char *psk) {
    char qs[160], qp[160], cmd[420], buf[1024];
    shq(qs, sizeof qs, ssid);
    shq(qp, sizeof qp, psk);
    snprintf(cmd, sizeof cmd, "sh " WIFI_SCRIPT " join %s %s", qs, qp);
    fprintf(stderr, "kframe: joining %s\n", ssid);
    g_wifi_err[0] = 0;
    int rc = run_sh(cmd, buf, sizeof buf);
    if (rc != 0) {
        char *last = strstr(buf, "error: ");
        snprintf(g_wifi_err, sizeof g_wifi_err, "%.200s",
                 last ? last + 7 : (buf[0] ? buf : "the join failed"));
        char *nl = strchr(g_wifi_err, '\n');
        if (nl) *nl = 0;
        fprintf(stderr, "kframe: wi-fi: %s\n", g_wifi_err);
        return -1;
    }
    return kframe_wifi_status();
}

static int wait_tap(int *x, int *y) {
    static int seen, when;
#ifdef EK_HOST
    if (!isatty(0)) {
        char buf[64];
        if (fgets(buf, sizeof buf, stdin) && sscanf(buf, "tap %d %d", x, y) == 2) return 1;
        fprintf(stderr, "kframe: wi-fi wanted a 'tap X Y' line and did not get one\n");
        return 0;
    }
#endif
    while (!cfg.stop || !*cfg.stop) {
        if (cfg.shot_req && *cfg.shot_req && cfg.shot_path) {
            *cfg.shot_req = 0;
            if (ek_dump_pgm(cfg.shot_path, 1) != 0)
                fprintf(stderr, "kframe: could not write %s\n", cfg.shot_path);
        }
        ek_event ev;
        while (ek_poll(&ev)) {
            if (ev.type == EK_UP) { seen = 0; continue; }
            if (ev.type != EK_DOWN) continue;
            uint32_t now = ek_ms();
            if (seen && now - (uint32_t)when < 400) continue;
            seen = 1;
            when = (int)now;
            *x = ev.x;
            *y = ev.y;
            return 1;
        }
        ek_sleep_ms(20);
    }
    return 0;
}

static const char *KB[4] = { "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm" };

static void w_entry(const char *value, int y) {
    ek_rect(20, y, 560, 40, EK_INK);
    ek_rect(21, y + 1, 558, 38, EK_INK);
    ek_text(30, y + 12, value, EK_INK);
    ek_text(30 + (int)strlen(value) * 8, y + 12, "_", EK_INK);
}

static void w_keyboard(const char *bottom_right) {
    for (int r = 0; r < 4; r++) {
        int len = (int)strlen(KB[r]);
        for (int i = 0; i < len; i++) {
            int x = W_KB_X + i * W_KEY;
            int y = W_KB_Y + r * 60;
            char label[2] = { KB[r][i], 0 };
            ek_fill_rect(x, y, W_KEY - 4, 54, EK_PAPER);
            ek_rect(x, y, W_KEY - 4, 54, EK_INK);
            ek_text(x + (W_KEY - 4 - 8) / 2, y + 19, label, EK_INK);
        }
    }
    box(20,  W_BOT, 130, 54, "SHIFT");
    box(160, W_BOT, 120, 54, "DEL");
    box(290, W_BOT, 140, 54, "CLEAR");
    box(20,  W_BOT2, 280, 54, "BACK");
    box(310, W_BOT2, 270, 54, bottom_right);
}

static int w_key_at(int x, int y, char *out) {
    for (int r = 0; r < 4; r++) {
        int len = (int)strlen(KB[r]);
        for (int i = 0; i < len; i++) {
            int kx = W_KB_X + i * W_KEY, ky = W_KB_Y + r * 60;
            if (x >= kx && x < kx + W_KEY - 4 && y >= ky && y < ky + 54) {
                *out = KB[r][i];
                return 1;
            }
        }
    }
    return 0;
}

static void w_draw_list(void) {
    ek_clear(EK_PAPER);
    ek_text_scale(12, 6, "WI-FI", EK_INK, 2);
    if (g_wifi_ssid[0]) {
        char line[128];
        snprintf(line, sizeof line, "on %s   %s", g_wifi_ssid, g_wifi_ip);
        ek_text(140, 16, line, EK_INK);
    } else {
        ek_text(140, 16, "not connected - tap a network", EK_INK);
    }
    if (g_wifi_err[0]) ek_text(12, 52, g_wifi_err, EK_INK);
    else if (!g_nnets) ek_text(12, 52, "looking for networks...", EK_INK);

    for (int i = 0; i < g_nnets; i++) {
        int y = 84 + i * W_ROW_H;
        if (y + W_ROW_H > 600) break;
        ek_rect(20, y, 560, W_ROW_H - 6, EK_INK);
        char name[40];
        snprintf(name, sizeof name, "%.30s", g_nets[i].ssid);
        ek_text(30, y + (W_ROW_H - 6 - 16) / 2, name, EK_INK);
        ek_text(470, y + (W_ROW_H - 6 - 16) / 2, g_nets[i].secure ? "WPA2" : "open", EK_INK);
    }
    box(20,  660, 170, 50, "RESCAN");
    box(210, 660, 190, 50, "TYPE NAME");
    box(420, 660, 160, 50, "SKIP");
    ek_flush();
}

static void w_draw_text(const char *title, const char *value, const char *bottom_right) {
    ek_clear(EK_PAPER);
    ek_text(12, 12, title, EK_INK);
    ek_text(12, 34, "tap the letters, then the button at the bottom", EK_INK);
    w_entry(value, 70);
    w_keyboard(bottom_right);
    ek_flush();
}

static void w_draw_fail(void) {
    ek_clear(EK_PAPER);
    ek_text(12, 12, "that did not work", EK_INK);
    wrap(g_wifi_err, 12, 44, 72, 8);
    box(20, 660, 250, 50, "TRY AGAIN");
    box(300, 660, 280, 50, "OTHER NETWORKS");
    ek_flush();
}

int kframe_wifi_setup(void) {
    if (need_cfg("wifi") != 0) return -1;
    enum { M_LIST, M_SSID, M_PASS, M_DONE };
    int mode = M_LIST;
    char ssid[64] = "", pw[64] = "";
    int shift = 0;
    int secure = 1;

    g_wifi_err[0] = 0;
    wifi_scan();

    for (;;) {
        if (cfg.stop && *cfg.stop) return -1;

        if (mode == M_LIST) {
            w_draw_list();
            int x, y;
            if (!wait_tap(&x, &y)) return -1;
            if (y >= 660 && y < 710) {
                if (x >= 20 && x < 190) { g_wifi_err[0] = 0; wifi_scan(); continue; }
                if (x >= 210 && x < 400) { mode = M_SSID; ssid[0] = 0; pw[0] = 0; secure = 1; continue; }
                if (x >= 420 && x < 580) return -1;
            }
            for (int i = 0; i < g_nnets; i++) {
                int ry = 84 + i * W_ROW_H;
                if (y >= ry && y < ry + W_ROW_H - 6 && x >= 20 && x < 580) {
                    snprintf(ssid, sizeof ssid, "%s", g_nets[i].ssid);
                    secure = g_nets[i].secure;
                    pw[0] = 0;
                    if (secure) {
                        mode = M_PASS;
                    } else {
                        ek_clear(EK_PAPER);
                        ek_text(12, 12, "joining", EK_INK);
                        ek_text(12, 40, ssid, EK_INK);
                        ek_flush();
                        if (wifi_join(ssid, "") == 0) return 0;
                        mode = M_DONE;
                    }
                    break;
                }
            }
            continue;
        }

        if (mode == M_SSID || mode == M_PASS) {
            char *buf = (mode == M_SSID) ? ssid : pw;
            size_t cap = (mode == M_SSID) ? sizeof ssid : sizeof pw;
            char title[120];
            if (mode == M_SSID) snprintf(title, sizeof title, "NETWORK NAME");
            else                snprintf(title, sizeof title, "PASSWORD FOR %.50s", ssid);
            w_draw_text(title, buf, mode == M_SSID ? "NEXT" : "JOIN");

            int x, y;
            if (!wait_tap(&x, &y)) return -1;
            size_t len = strlen(buf);

            if (x >= 20 && x < 150 && y >= W_BOT && y < W_BOT + 54) { shift = !shift; continue; }
            if (x >= 160 && x < 280 && y >= W_BOT && y < W_BOT + 54) {
                if (len) buf[len - 1] = 0;
                continue;
            }
            if (x >= 290 && x < 430 && y >= W_BOT && y < W_BOT + 54) { buf[0] = 0; continue; }
            if (y >= W_BOT2 && y < W_BOT2 + 54) {
                if (x >= 20 && x < 300) { mode = M_LIST; continue; }
                if (x >= 310 && x < 580) {
                    if (mode == M_SSID) {
                        if (!ssid[0]) {
                            snprintf(g_wifi_err, sizeof g_wifi_err, "type the network name first");
                            fprintf(stderr, "kframe: %s\n", g_wifi_err);
                            continue;
                        }
                        pw[0] = 0;
                        mode = M_PASS;
                        continue;
                    }
                    ek_clear(EK_PAPER);
                    ek_text(12, 12, "joining", EK_INK);
                    ek_text(12, 40, ssid, EK_INK);
                    ek_flush();
                    if (wifi_join(ssid, pw) == 0) return 0;
                    mode = M_DONE;
                    continue;
                }
            }
            char ch;
            if (w_key_at(x, y, &ch)) {
                if (len + 1 < cap) {
                    buf[len] = shift ? (char)toupper((unsigned char)ch) : ch;
                    buf[len + 1] = 0;
                }
            }
            continue;
        }

        if (mode == M_DONE) {
            w_draw_fail();
            int x, y;
            if (!wait_tap(&x, &y)) return -1;
            if (y >= 660 && y < 710) {
                if (x >= 20 && x < 270) { mode = M_PASS; continue; }
                if (x >= 300 && x < 580) { mode = M_LIST; g_wifi_err[0] = 0; wifi_scan(); continue; }
            }
            continue;
        }
    }
}

int kframe_wifi_shot(const char *what) {
    if (!what) what = "list";
    if (!strcmp(what, "list")) {
        wifi_scan();
        w_draw_list();
    } else if (!strcmp(what, "name")) {
        w_draw_text("NETWORK NAME", "garden", "NEXT");
    } else if (!strcmp(what, "pass")) {
        w_draw_text("PASSWORD FOR PRNet84", "Alotbsol1", "JOIN");
    } else if (!strcmp(what, "fail")) {
        snprintf(g_wifi_err, sizeof g_wifi_err,
                 "could not join PRNet84 (out of range, or the password was refused)");
        w_draw_fail();
    } else {
        fprintf(stderr, "kframe: wifi shot wants list, name, pass or fail (got %s)\n", what);
        return -1;
    }
    return 0;
}

/* ---- taps on the hamburger and the menu -------------------------------- */

static int tap_menu(int x, int y) {
    if (x >= 24 && x < 224 && y >= 720 && y < 772) {
        page = 0;
        quit_armed = 0;
        return 1;
    }
    if (in_row(x, y, row_y(0))) {
        if (!quit_armed) {
            quit_armed = 1;
            fprintf(stderr, "kframe: quit armed\n");
            return 1;
        }
        page = 0;
        quit_armed = 0;
        fprintf(stderr, "kframe: quit confirmed\n");
        cfg.on_quit();
        return 1;
    }
    quit_armed = 0;
    if (in_row(x, y, row_y(1))) {
        vol_read();
        page = 2;
        return 1;
    }
    if (in_row(x, y, row_y(2))) {
        page = 0;
        fprintf(stderr, "kframe: wifi opened from the menu\n");
        kframe_wifi_setup();
        return 1;
    }
    for (int i = 0; i < cfg.nitems; i++) {
        int y0 = app_y(i);
        if (y0 + KFRAME_ROW_H > 700) break;
        if (!in_row(x, y, y0)) continue;
        page = 0;
        int id = cfg.items[i].id;
        fprintf(stderr, "kframe: app item %d\n", id);
        int r = cfg.on_item(id);
        if (r == 0) {
            fprintf(stderr, "kframe: app item %d returned 0; the tap was still consumed\n", id);
            return 1;
        }
        return r;
    }
    return 1;
}

static int tap_volume(int x, int y) {
    if (x >= 24 && x < 224 && y >= 720 && y < 772) {
        page = 1;
        return 1;
    }
    int hit = 0;
    if (x >= 24 && x < 204 && y >= 300 && y < 370) hit = -1;
    if (x >= 396 && x < 576 && y >= 300 && y < 370) hit = 1;
    if (!hit) return 1;
    if (vol_pct < 0) {
        /* No made-up starting level. Ask the mixer again and show its answer. */
        fprintf(stderr, "kframe: volume step refused until the mixer answers\n");
        vol_read();
        return 1;
    }
    vol_set(vol_pct + hit * 10);
    return 1;
}

int kframe_tap(int x, int y) {
    if (need_cfg("tap") != 0) return 0;
    if (page == 1) return tap_menu(x, y);
    if (page == 2) return tap_volume(x, y);
    if (x >= KFRAME_TAB_X && x < KFRAME_TAB_X + KFRAME_TAB_W
        && y >= KFRAME_TAB_Y && y < KFRAME_TAB_Y + KFRAME_TAB_H) {
        page = 1;
        quit_armed = 0;
        fprintf(stderr, "kframe: menu open\n");
        return 1;
    }
    return 0;
}

/* The quit handler has to live on the config. I referenced cfg.on_quit above
 * but have not added it to the struct yet -- fixing that in the header and
 * in tap_menu together. Placeholder so this file doesn't compile halfway:
 * the real call is written below once the field exists. */

/* ---- power key -------------------------------------------------------- */

int kframe_count_pwr_records(const char *text) {
    int n = 0;
    if (!text) return 0;
    for (const char *p = strstr(text, "PWRON1"); p; p = strstr(p + 6, "PWRON1")) n++;
    return n;
}

int kframe_pwr_log_grew(const char *prev, const char *cur, int len) {
    const int probe = 128;
    if (!prev || !cur || len <= probe) return -1;
    for (int p = len - probe; p >= 0; p--)
        if (!memcmp(cur + p, prev + len - probe, (size_t)probe)) return len - probe - p;
    return -1;
}

int kframe_pwr_latched(void) { return pwr_latch; }
void kframe_pwr_set_latch(int on) { pwr_latch = on ? 1 : 0; }

#ifdef EK_HOST
int kframe_pwr_presses(void) { return 0; }
int kframe_pwr_poll(void) { return 0; }
#else
static void say(const char *msg) {
    if (cfg.status) cfg.status(msg);
    else fprintf(stderr, "kframe: %s\n", msg);
}

static char    *pwr_log;
static char    *pwr_log_prev;
static int      pwr_log_len;
static int      pwr_log_had = -1;
static uint32_t pwr_log_at;
static int      pwr_log_broke;

static int pwr_log_size(void) {
    if (pwr_log_len) return pwr_log_len;
    int n = klogctl(PWR_LOG_SIZE, NULL, 0);
    if (n <= 0 || n > PWR_LOG_MAX) {
        if (!pwr_log_broke++) {
            fprintf(stderr, "kframe: no power button: cannot size the kernel log: %s\n",
                    strerror(errno));
            say("no power button - kernel log");
        }
        return -1;
    }
    pwr_log = malloc((size_t)n + 2);
    pwr_log_prev = malloc((size_t)n + 2);
    if (!pwr_log || !pwr_log_prev) {
        fprintf(stderr, "kframe: out of memory reading the kernel log\n");
        say("no power button - out of memory");
        return -1;
    }
    pwr_log_len = n;
    return n;
}

int kframe_pwr_presses(void) {
    int len = pwr_log_size();
    if (len < 0) return -1;
    uint32_t now = ek_ms();
    if (pwr_log_at && now - pwr_log_at < PWR_LOG_EVERY_MS) return 0;
    pwr_log_at = now ? now : 1;

    int n = klogctl(PWR_LOG_READ_ALL, pwr_log, len);
    if (n < 0) {
        if (!pwr_log_broke++) {
            fprintf(stderr, "kframe: no power button: cannot read the kernel log: %s\n",
                    strerror(errno));
            say("no power button - cannot read the kernel log");
        }
        return -1;
    }
    pwr_log[n] = 0;
    int had = kframe_count_pwr_records(pwr_log);
    int hits = 0;
    int grew = pwr_log_had < 0 ? -1 : kframe_pwr_log_grew(pwr_log_prev, pwr_log, n);
    if (grew > 0)
        hits = kframe_count_pwr_records(pwr_log + n - grew);
    else if (grew < 0 && pwr_log_had >= 0 && had > pwr_log_had)
        hits = had - pwr_log_had;
    pwr_log_had = had;

    char *swap = pwr_log_prev;
    pwr_log_prev = pwr_log;
    pwr_log = swap;
    return hits;
}

static void pwr_drain(int ms) {
    ek_sleep_ms(ms);
    pwr_log_at = 0;
    while (kframe_pwr_presses() > 0) { }
}

static double up_secs(void) {
    FILE *f = fopen("/proc/uptime", "r");
    if (!f) return -1;
    double v = -1;
    if (fscanf(f, "%lf", &v) != 1) v = -1;
    fclose(f);
    return v;
}

static int pwr_suspend(char *err, size_t n) {
    int fd = open(PWR_STATE, O_WRONLY);
    if (fd < 0) {
        snprintf(err, n, "could not open %s: %s", PWR_STATE, strerror(errno));
        return -1;
    }
    errno = 0;
    ssize_t w = write(fd, "mem", 3);
    int e = errno;
    close(fd);
    if (w != 3) {
        snprintf(err, n, "could not suspend: %s", e ? strerror(e) : "the write was short");
        return -1;
    }
    return 0;
}

static void draw_sleep(void) {
    ek_clear(EK_PAPER);
    ek_text_scale(196, 300, "ASLEEP", EK_INK, 4);
    ek_text(132, 420, "press the power button to wake it up", EK_INK);
    ek_text(156, 452, "the game carries on where it was", EK_INK);
}

static void sleep_now(void) {
    char err[200] = "";
    page = 0;
    quit_armed = 0;
    pwr_drain(200);

    draw_sleep();
    ek_force_full();
    ek_flush();

    double t0 = up_secs();
    if (pwr_suspend(err, sizeof err) != 0) {
        fprintf(stderr, "kframe: %s\n", err);
        say(err);
        ek_force_full();
        if (cfg.repaint) cfg.repaint();
        return;
    }
    double t1 = up_secs();
    if (t0 >= 0 && t1 >= 0 && t1 > t0)
        fprintf(stderr, "kframe: asleep for %.1f s\n", t1 - t0);
    else
        fprintf(stderr, "kframe: woke, but /proc/uptime did not move\n");

    pwr_drain(800);
    ek_force_full();
    if (cfg.repaint) cfg.repaint();

    if (kframe_wifi_status() != 0) {
        char buf[256] = "";
        say("back - rejoining wi-fi");
        if (cfg.repaint) cfg.repaint();
        if (run_sh("sh " WIFI_SCRIPT " auto", buf, sizeof buf) != 0) {
            fprintf(stderr, "kframe: wi-fi did not come back: %.200s\n", buf);
            say("wi-fi did not come back");
            if (cfg.repaint) cfg.repaint();
        } else {
            fprintf(stderr, "kframe: wi-fi rejoined\n");
        }
    }
}

int kframe_pwr_poll(void) {
    int hits = kframe_pwr_presses();
    if (hits > 0) {
        fprintf(stderr, "kframe: power button pressed\n");
        pwr_latch = 1;
    } else if (hits < 0)
        return 0;
    if (!pwr_latch) return 0;
    pwr_latch = 0;
    sleep_now();
    return 1;
}
#endif
