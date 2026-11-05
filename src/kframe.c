#define _GNU_SOURCE
/* kframe.c -- the Kindle bits every game should share.
 *
 * Hamburger, the tabbed shell (the app's screens on the left, volume /
 * wi-fi / apps / quit on the right), and the power-key sleep. The on-screen
 * keyboard lives in kshell.c and comes up only while a field is focused.
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

/* historical name; every app uses it. KFRAME_WIFI_SCRIPT is for the host build,
 * where the test drives the screens against a stand-in on this PC. */
#ifdef KFRAME_WIFI_SCRIPT
#define WIFI_SCRIPT KFRAME_WIFI_SCRIPT
#else
#define WIFI_SCRIPT "/mnt/us/chess-wifi.sh"
#endif

#define PWR_STATE        "/sys/power/state"
#define PWR_LOG_READ_ALL 3
#define PWR_LOG_SIZE     10
#define PWR_LOG_MAX      (256 * 1024)
#define PWR_LOG_EVERY_MS 150

static kframe_cfg cfg;
static int cfg_ok;

static int page;          /* 0 board, 1 shell */
static int quit_armed;
static int vol_pct = -1; /* last level amixer actually reported; -1 = unknown */
static char vol_err[240];

static int wifi_mode;     /* 0 off, 1 list, 2 name, 3 password, 4 failed */
static int ssid_focus, pass_focus;
static int wifi_blocking; /* kframe_wifi_setup is in its tap loop */
static int wifi_result;   /* 0 working, 1 joined, -1 user left */

static int pwr_latch;

#define KFRAME_MAX_APP 8
enum { TAB_VOLUME = -1, TAB_WIFI = -2, TAB_QUIT = -3, TAB_APPS = -4 };

/* Host tests read and write these under /tmp. The Kindle copies live in
 * the user store, which survives a reboot. tools/menu-boot.sh reads the
 * same names. */
#ifdef EK_HOST
#define KFRAME_DIR "/tmp"
#else
#define KFRAME_DIR "/mnt/us"
#endif

enum { APP_HOME = 1, APP_VISIT = 2, APP_READER = 3 };

/* Ids must match the case list in tools/menu-boot.sh.
 * HOME: this shell. Quit writes .menu-stop (reader until next boot).
 * VISIT: no shell. A clean exit returns to the app that opened it.
 * READER: the Amazon screen. It is not saved as the boot app. */
static const struct {
    const char *id;
    const char *label;
    int kind;
} APPS[] = {
    { "xcom",     "XCOM",     APP_VISIT },
    { "chess",    "CHESS",    APP_HOME },
    { "tetris",   "TETRIS",   APP_HOME },
    { "sudoku",   "SUDOKU",   APP_VISIT },
    { "rockman",  "ROCKMAN",  APP_VISIT },
    { "pvz",      "PVZ",      APP_VISIT },
    { "brogue",   "BROGUE",   APP_VISIT },
    { "calib",    "CALIB",    APP_VISIT },
    { "koreader", "KOREADER", APP_VISIT },
    { "kindle",   "KINDLE",   APP_READER },
};

static kshell_tab shell_tabs[KFRAME_MAX_APP + 4];
static int shell_ntabs;
static char quit_label[8] = "QUIT";

static int app_slot(const char *id) {
    if (!id || !id[0]) return -1;
    for (int i = 0; i < (int)(sizeof APPS / sizeof APPS[0]); i++)
        if (!strcmp(APPS[i].id, id)) return i;
    return -1;
}

static int stamp_path(char *dst, size_t n, const char *name) {
    int wr = snprintf(dst, n, "%s/%s", KFRAME_DIR, name);
    if (wr < 0 || (size_t)wr >= n) {
        fprintf(stderr, "kframe: path for %s does not fit\n", name);
        return -1;
    }
    return 0;
}

static int write_stamp(const char *name, const char *body) {
    char path[160];
    if (stamp_path(path, sizeof path, name) != 0) return -1;
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "kframe: could not write %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (fprintf(f, "%s\n", body ? body : "") < 0) {
        fprintf(stderr, "kframe: could not write %s: %s\n", path, strerror(errno));
        fclose(f);
        return -1;
    }
    if (fflush(f) != 0 || fsync(fileno(f)) != 0) {
        fprintf(stderr, "kframe: could not save %s: %s\n", path, strerror(errno));
        fclose(f);
        return -1;
    }
    if (fclose(f) != 0) {
        fprintf(stderr, "kframe: could not close %s: %s\n", path, strerror(errno));
        return -1;
    }
    fprintf(stderr, "kframe: wrote %s (%s)\n", path, body ? body : "");
    return 0;
}

/* Missing is fine. Any other error is reported and fails the caller. */
static int clear_stamp(const char *name) {
    char path[160];
    if (stamp_path(path, sizeof path, name) != 0) return -1;
    if (unlink(path) != 0 && errno != ENOENT) {
        fprintf(stderr, "kframe: could not remove %s: %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}

static int apps_fit(void) {
    int n = (int)(sizeof APPS / sizeof APPS[0]) - 1; /* the running app is hidden */
    int bottom = KFRAME_APP_ROW_Y + (n - 1) * KFRAME_APP_ROW_P + KFRAME_APP_ROW_H;
    if (bottom > KFRAME_BACK_Y - 8) {
        fprintf(stderr, "kframe: %d other apps do not fit (last row ends at %d, back is at %d)\n",
                n, bottom, KFRAME_BACK_Y);
        return -1;
    }
    return 0;
}

static int build_shell(void) {
    if (cfg.nitems > KFRAME_MAX_APP) {
        fprintf(stderr, "kframe: %d app tabs do not fit (max %d)\n",
                cfg.nitems, KFRAME_MAX_APP);
        return -1;
    }
    int n = 0;
    for (int i = 0; i < cfg.nitems; i++) {
        if (!cfg.items[i].label || !cfg.items[i].label[0]) {
            fprintf(stderr, "kframe: app tab %d has no name\n", i);
            return -1;
        }
        int id = cfg.items[i].id;
        if (id == 0 || id == TAB_VOLUME || id == TAB_WIFI || id == TAB_QUIT || id == TAB_APPS) {
            fprintf(stderr, "kframe: app tab \"%s\" uses reserved id %d\n",
                    cfg.items[i].label, id);
            return -1;
        }
        shell_tabs[n].label = cfg.items[i].label;
        shell_tabs[n].id = id;
        shell_tabs[n].side = KSHELL_APP;
        n++;
    }
    shell_tabs[n].label = "VOLUME";
    shell_tabs[n].id = TAB_VOLUME;
    shell_tabs[n].side = KSHELL_GENERAL;
    n++;
    shell_tabs[n].label = "WIFI";
    shell_tabs[n].id = TAB_WIFI;
    shell_tabs[n].side = KSHELL_GENERAL;
    n++;
    shell_tabs[n].label = "APPS";
    shell_tabs[n].id = TAB_APPS;
    shell_tabs[n].side = KSHELL_GENERAL;
    n++;
    snprintf(quit_label, sizeof quit_label, "QUIT");
    shell_tabs[n].label = quit_label;
    shell_tabs[n].id = TAB_QUIT;
    shell_tabs[n].side = KSHELL_GENERAL;
    n++;
    shell_ntabs = n;
    return kshell_init(shell_tabs, n);
}

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
    int slot = app_slot(c->app_id);
    if (slot < 0 || APPS[slot].kind == APP_READER) {
        fprintf(stderr, "kframe: app_id \"%s\" is not a game this shell can be\n",
                c->app_id ? c->app_id : "(none)");
        cfg_ok = 0;
        return -1;
    }
    cfg = *c;
    cfg_ok = 1;
    page = 0;
    quit_armed = 0;
    if (build_shell() != 0 || apps_fit() != 0) {
        cfg_ok = 0;
        return -1;
    }
    /* A stamp left by an earlier quit or switch is stale once this app is
     * actually up. Failing to clear one would make the next exit look like
     * that old request. */
    if (clear_stamp(".menu-stop") != 0 || clear_stamp("switch-app") != 0
        || clear_stamp("return-app") != 0 || write_stamp("last-app", cfg.app_id) != 0) {
        cfg_ok = 0;
        return -1;
    }
    return 0;
}

int kframe_covering(void) { return page != 0; }

static void disarm_quit(void) {
    quit_armed = 0;
    snprintf(quit_label, sizeof quit_label, "QUIT");
}

void kframe_close(void) {
    page = 0;
    disarm_quit();
    wifi_mode = 0;
    ssid_focus = 0;
    pass_focus = 0;
    kshell_focus(NULL, 0);
}

void kframe_show_menu(void) {
    page = 1;
    disarm_quit();
    wifi_mode = 0;
    ssid_focus = 0;
    pass_focus = 0;
    kshell_focus(NULL, 0);
    for (int i = 0; i < shell_ntabs; i++) {
        if (shell_tabs[i].side == KSHELL_APP) {
            kshell_select(shell_tabs[i].id);
            return;
        }
    }
    kshell_select(TAB_VOLUME);
}

/* ---- small drawing ---------------------------------------------------- */

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

static void paint_shell(void);

static void paint_tab(void) {
    int x = KFRAME_TAB_X, y = KFRAME_TAB_Y, w = KFRAME_TAB_W, h = KFRAME_TAB_H;
    ek_fill_rect(x, y, w, h, EK_PAPER);
    ek_rect(x, y, w, h, EK_INK);
    /* Three bars, centred in whatever height the tab is. */
    const int bar = 3, gap = 4;
    int bx = x + 8, bw = w - 16;
    int top = y + (h - (3 * bar + 2 * gap)) / 2;
    for (int i = 0; i < 3; i++)
        ek_fill_rect(bx, top + i * (bar + gap), bw, bar, EK_INK);
}

void kframe_paint(void) {
    if (need_cfg("paint") != 0) return;
    if (page == 1) paint_shell();
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

enum { WIFI_OFF = 0, WIFI_LIST = 1, WIFI_SSID = 2, WIFI_PASS = 3, WIFI_FAIL = 4 };

static char typed_ssid[64];
static char typed_pw[64];
static int typed_secure = 1;

static const kframe_item *find_item(int id) {
    for (int i = 0; i < cfg.nitems; i++)
        if (cfg.items[i].id == id) return &cfg.items[i];
    return NULL;
}

static void wifi_enter(void) {
    wifi_mode = WIFI_LIST;
    ssid_focus = 0;
    pass_focus = 0;
    g_wifi_err[0] = 0;
    wifi_scan();
}

static int leave_shell(void) {
    if (wifi_blocking) wifi_result = -1;
    else kframe_close();
    return 1;
}

static void select_tab(int id) {
    if (id != TAB_QUIT) disarm_quit();
    int prev = kshell_selected();
    kshell_select(id);
    if (id == TAB_VOLUME && prev != TAB_VOLUME) vol_read();
    if (id == TAB_WIFI && wifi_mode == WIFI_OFF) wifi_enter();
    if (id == TAB_APPS && prev != TAB_APPS) fprintf(stderr, "kframe: apps open\n");
}

static int quit_press(void) {
    if (!quit_armed) {
        quit_armed = 1;
        snprintf(quit_label, sizeof quit_label, "SURE?");
        kshell_select(TAB_QUIT);
        fprintf(stderr, "kframe: quit armed\n");
        return 1;
    }
    if (write_stamp(".menu-stop", "quit") != 0) return 1;
    fprintf(stderr, "kframe: quit confirmed\n");
    kframe_close();
    cfg.on_quit();
    return 1;
}

/* Row in the APPS list, skipping the app that is already running. */
static int visible_app(int row) {
    int seen = 0;
    int n = (int)(sizeof APPS / sizeof APPS[0]);
    for (int i = 0; i < n; i++) {
        if (!strcmp(APPS[i].id, cfg.app_id)) continue;
        if (seen == row) return i;
        seen++;
    }
    return -1;
}

static int switch_to(int idx) {
    const char *id = APPS[idx].id;
    if (APPS[idx].kind == APP_READER) {
        if (write_stamp(".menu-stop", "quit") != 0) return 1;
        fprintf(stderr, "kframe: kindle\n");
        kframe_close();
        cfg.on_quit();
        return 1;
    }
    if (!strcmp(id, cfg.app_id)) {
        fprintf(stderr, "kframe: %s is already running\n", id);
        return 1;
    }
    if (APPS[idx].kind == APP_VISIT) {
        if (write_stamp("return-app", cfg.app_id) != 0) return 1;
    } else if (clear_stamp("return-app") != 0) {
        return 1;
    }
    if (write_stamp("switch-app", id) != 0) return 1;
    fprintf(stderr, "kframe: switch %s\n", id);
    kframe_close();
    cfg.on_quit();
    return 1;
}

static void draw_apps(const kshell_rect *r) {
    ek_text_scale(r->x, r->y, "APPS", EK_INK, 2);
    ek_text(r->x, r->y + 36, "tap one. that game comes back after a restart.", EK_INK);
    ek_text(r->x, r->y + 54, "KINDLE hands the screen back until the next boot.", EK_INK);
    int n = (int)(sizeof APPS / sizeof APPS[0]) - 1;
    for (int row = 0; row < n; row++) {
        int idx = visible_app(row);
        if (idx < 0) {
            fprintf(stderr, "kframe: apps row %d has no app\n", row);
            return;
        }
        int y = KFRAME_APP_ROW_Y + row * KFRAME_APP_ROW_P;
        kshell_button(KFRAME_APP_ROW_X, y, KFRAME_APP_ROW_W, KFRAME_APP_ROW_H,
                      APPS[idx].label, 0);
    }
}

static int tap_apps(int x, int y) {
    int n = (int)(sizeof APPS / sizeof APPS[0]) - 1;
    for (int row = 0; row < n; row++) {
        int ry = KFRAME_APP_ROW_Y + row * KFRAME_APP_ROW_P;
        if (!kshell_inside(x, y, KFRAME_APP_ROW_X, ry, KFRAME_APP_ROW_W, KFRAME_APP_ROW_H))
            continue;
        int idx = visible_app(row);
        if (idx < 0) {
            fprintf(stderr, "kframe: apps row %d has no app\n", row);
            return 1;
        }
        return switch_to(idx);
    }
    return 1;
}

static void shell_sync_focus(void) {
    if (page != 1 || kshell_selected() != TAB_WIFI) {
        kshell_focus(NULL, 0);
        return;
    }
    if (wifi_mode == WIFI_PASS && pass_focus)
        kshell_focus(typed_pw, sizeof typed_pw);
    else if (wifi_mode == WIFI_SSID && ssid_focus)
        kshell_focus(typed_ssid, sizeof typed_ssid);
    else
        kshell_focus(NULL, 0);
}

static void draw_volume(const kshell_rect *r) {
    ek_text_scale(r->x, r->y, "VOLUME", EK_INK, 2);
    if (vol_pct >= 0) {
        char num[16];
        snprintf(num, sizeof num, "%d%%", vol_pct);
        int tw = (int)strlen(num) * 8 * 4;
        ek_text_scale((EK_W - tw) / 2, r->y + 70, num, EK_INK, 4);
        ek_text(r->x, r->y + 160, "speaker, from the Kindle mixer", EK_INK);
    } else {
        ek_text(r->x, r->y + 50, "the mixer did not give a level", EK_INK);
        wrap(vol_err[0] ? vol_err : "no error text came back", r->x, r->y + 80, 68, 5);
    }
    kshell_button(24, 320, 200, 70, "-", 0);
    kshell_button(376, 320, 200, 70, "+", 0);
}

static void draw_quit(const kshell_rect *r) {
    ek_text_scale(r->x, r->y, quit_armed ? "SURE?" : "QUIT", EK_INK, 2);
    ek_text(r->x, r->y + 48, "tap QUIT again to leave the game", EK_INK);
    kshell_button(KFRAME_ACT_X, KFRAME_ACT_Y, KFRAME_ACT_W, KFRAME_ACT_H,
                  quit_armed ? "SURE?" : "QUIT", quit_armed);
}

static void draw_app(const kshell_rect *r, int id) {
    const kframe_item *it = find_item(id);
    if (!it) {
        fprintf(stderr, "kframe: no screen for tab %d\n", id);
        return;
    }
    if (it->draw) {
        it->draw(r);
        return;
    }
    ek_text_scale(r->x, r->y, it->label, EK_INK, 2);
    kshell_button(KFRAME_ACT_X, KFRAME_ACT_Y, KFRAME_ACT_W, KFRAME_ACT_H, it->label, 0);
}

static void draw_wifi_list(void) {
    ek_text_scale(16, 58, "WI-FI", EK_INK, 2);
    if (g_wifi_err[0]) wrap(g_wifi_err, 16, 96, 70, 1);
    else if (g_wifi_ssid[0]) {
        char line[128];
        snprintf(line, sizeof line, "on %.60s   %.30s", g_wifi_ssid, g_wifi_ip);
        ek_text(16, 96, line, EK_INK);
    } else if (!g_nnets) {
        ek_text(16, 96, "no networks yet - tap RESCAN", EK_INK);
    } else {
        ek_text(16, 96, "tap a network", EK_INK);
    }

    int hidden = 0;
    for (int i = 0; i < g_nnets; i++) {
        int y = KFRAME_WIFI_ROW_Y + i * KFRAME_WIFI_ROW_P;
        if (y + KFRAME_WIFI_ROW_H > KFRAME_WIFI_BTN_Y - 8) { hidden++; continue; }
        ek_fill_rect(16, y, 568, KFRAME_WIFI_ROW_H, EK_PAPER);
        ek_rect(16, y, 568, KFRAME_WIFI_ROW_H, EK_INK);
        char name[40];
        snprintf(name, sizeof name, "%.28s", g_nets[i].ssid);
        ek_text(28, y + (KFRAME_WIFI_ROW_H - 16) / 2, name, EK_INK);
        ek_text(480, y + (KFRAME_WIFI_ROW_H - 16) / 2,
                g_nets[i].secure ? "WPA2" : "open", EK_INK);
    }
    if (hidden)
        fprintf(stderr, "kframe: %d network%s did not fit on the wi-fi list\n",
                hidden, hidden == 1 ? "" : "s");
    kshell_button(16,  KFRAME_WIFI_BTN_Y, 180, 52, "RESCAN", 0);
    kshell_button(206, KFRAME_WIFI_BTN_Y, 180, 52, "TYPE NAME", 0);
    kshell_button(396, KFRAME_WIFI_BTN_Y, 188, 52, "SKIP", 0);
}

static void text_back_at(int *x, int *y) {
    kshell_rect r;
    kshell_content(&r);
    *x = r.x + r.w - 120;
    *y = r.y;
}

static void draw_wifi_text(void) {
    char title[80];
    if (wifi_mode == WIFI_SSID) snprintf(title, sizeof title, "NETWORK NAME");
    else snprintf(title, sizeof title, "PASSWORD FOR %.40s", typed_ssid);
    ek_text(16, 64, title, EK_INK);
    int bx, by;
    text_back_at(&bx, &by);
    kshell_button(bx, by, 120, 40, "BACK", 0);
    int focused = (wifi_mode == WIFI_PASS && pass_focus) ||
                  (wifi_mode == WIFI_SSID && ssid_focus);
    const char *value = (wifi_mode == WIFI_SSID) ? typed_ssid : typed_pw;
    kshell_field(KFRAME_FIELD_X, KFRAME_FIELD_Y, KFRAME_FIELD_W, KFRAME_FIELD_H,
                 value, focused);
    if (g_wifi_err[0]) ek_text(16, 160, g_wifi_err, EK_INK);
    kshell_button(KFRAME_WIFI_GO_X, KFRAME_WIFI_GO_Y, KFRAME_WIFI_GO_W, KFRAME_WIFI_GO_H,
                  wifi_mode == WIFI_SSID ? "NEXT" : "JOIN", 0);
}

static void draw_wifi_fail(void) {
    ek_text_scale(16, 58, "NO JOIN", EK_INK, 2);
    wrap(g_wifi_err[0] ? g_wifi_err : "the join failed", 16, 110, 70, 6);
    kshell_button(16,  KFRAME_WIFI_BTN_Y, 270, 52, "TRY AGAIN", 0);
    kshell_button(300, KFRAME_WIFI_BTN_Y, 284, 52, "OTHER NETWORKS", 0);
}

static void draw_wifi(const kshell_rect *r) {
    (void)r;
    if (wifi_mode == WIFI_OFF) wifi_enter();
    if (wifi_mode == WIFI_LIST) draw_wifi_list();
    else if (wifi_mode == WIFI_FAIL) draw_wifi_fail();
    else draw_wifi_text();
}

static void paint_shell_now(void) {
    ek_clear(EK_PAPER);
    kshell_paint_tabs();
    kshell_rect r;
    kshell_content(&r);
    int id = kshell_selected();
    if (id == TAB_VOLUME) draw_volume(&r);
    else if (id == TAB_WIFI) draw_wifi(&r);
    else if (id == TAB_APPS) draw_apps(&r);
    else if (id == TAB_QUIT) draw_quit(&r);
    else draw_app(&r, id);
    if (!kshell_keyboard_on())
        kshell_button(KFRAME_BACK_X, KFRAME_BACK_Y, KFRAME_BACK_W, KFRAME_BACK_H, "BACK", 0);
    kshell_paint_keyboard();
}

static void paint_shell(void) {
    shell_sync_focus();
    paint_shell_now();
}

static int do_join(const char *ssid, const char *pw) {
    ek_clear(EK_PAPER);
    ek_text(16, 60, "joining", EK_INK);
    ek_text(16, 90, ssid, EK_INK);
    ek_flush();
    if (wifi_join(ssid, pw) == 0) {
        if (wifi_blocking) wifi_result = 1;
        else {
            wifi_mode = WIFI_LIST;
            pass_focus = 0;
            ssid_focus = 0;
        }
        return 1;
    }
    wifi_mode = WIFI_FAIL;
    pass_focus = 0;
    ssid_focus = 0;
    return 1;
}

static int tap_volume(int x, int y) {
    int hit = 0;
    if (kshell_inside(x, y, 24, 320, 200, 70)) hit = -1;
    if (kshell_inside(x, y, 376, 320, 200, 70)) hit = 1;
    if (!hit) return 1;
    if (vol_pct < 0) {
        fprintf(stderr, "kframe: volume step refused until the mixer answers\n");
        vol_read();
        return 1;
    }
    vol_set(vol_pct + hit * 10);
    return 1;
}

static int tap_wifi_list(int x, int y) {
    if (kshell_inside(x, y, 16, KFRAME_WIFI_BTN_Y, 180, 52)) {
        g_wifi_err[0] = 0;
        wifi_scan();
        return 1;
    }
    if (kshell_inside(x, y, 206, KFRAME_WIFI_BTN_Y, 180, 52)) {
        typed_ssid[0] = 0;
        typed_pw[0] = 0;
        typed_secure = 1;
        g_wifi_err[0] = 0;
        wifi_mode = WIFI_SSID;
        ssid_focus = 1;
        pass_focus = 0;
        return 1;
    }
    if (kshell_inside(x, y, 396, KFRAME_WIFI_BTN_Y, 188, 52)) return leave_shell();
    for (int i = 0; i < g_nnets; i++) {
        int ry = KFRAME_WIFI_ROW_Y + i * KFRAME_WIFI_ROW_P;
        if (ry + KFRAME_WIFI_ROW_H > KFRAME_WIFI_BTN_Y - 8) break;
        if (!kshell_inside(x, y, 16, ry, 568, KFRAME_WIFI_ROW_H)) continue;
        snprintf(typed_ssid, sizeof typed_ssid, "%s", g_nets[i].ssid);
        typed_secure = g_nets[i].secure;
        typed_pw[0] = 0;
        g_wifi_err[0] = 0;
        if (typed_secure) {
            wifi_mode = WIFI_PASS;
            pass_focus = 1;
            ssid_focus = 0;
            return 1;
        }
        return do_join(typed_ssid, "");
    }
    return 1;
}

static int tap_wifi_text(int x, int y) {
    int bx, by;
    text_back_at(&bx, &by);
    if (kshell_inside(x, y, bx, by, 120, 40)) {
        wifi_mode = WIFI_LIST;
        pass_focus = 0;
        ssid_focus = 0;
        g_wifi_err[0] = 0;
        return 1;
    }
    if (kshell_inside(x, y, KFRAME_FIELD_X, KFRAME_FIELD_Y, KFRAME_FIELD_W, KFRAME_FIELD_H)) {
        if (wifi_mode == WIFI_SSID) ssid_focus = 1;
        else pass_focus = 1;
        return 1;
    }
    if (!kshell_inside(x, y, KFRAME_WIFI_GO_X, KFRAME_WIFI_GO_Y,
                       KFRAME_WIFI_GO_W, KFRAME_WIFI_GO_H))
        return 1;
    if (wifi_mode == WIFI_SSID) {
        if (!typed_ssid[0]) {
            snprintf(g_wifi_err, sizeof g_wifi_err, "type the network name first");
            fprintf(stderr, "kframe: %s\n", g_wifi_err);
            return 1;
        }
        typed_pw[0] = 0;
        wifi_mode = WIFI_PASS;
        pass_focus = 1;
        ssid_focus = 0;
        g_wifi_err[0] = 0;
        return 1;
    }
    return do_join(typed_ssid, typed_pw);
}

static int tap_wifi_fail(int x, int y) {
    if (kshell_inside(x, y, 16, KFRAME_WIFI_BTN_Y, 270, 52)) {
        if (typed_ssid[0] && typed_secure) {
            wifi_mode = WIFI_PASS;
            pass_focus = 1;
        } else {
            wifi_mode = WIFI_LIST;
        }
        return 1;
    }
    if (kshell_inside(x, y, 300, KFRAME_WIFI_BTN_Y, 284, 52)) {
        g_wifi_err[0] = 0;
        wifi_mode = WIFI_LIST;
        pass_focus = 0;
        wifi_scan();
        return 1;
    }
    return 1;
}

static int tap_wifi(int x, int y) {
    if (wifi_mode == WIFI_OFF) wifi_enter();
    if (wifi_mode == WIFI_LIST) return tap_wifi_list(x, y);
    if (wifi_mode == WIFI_FAIL) return tap_wifi_fail(x, y);
    return tap_wifi_text(x, y);
}

static int tap_app(const kshell_rect *r, int x, int y) {
    const kframe_item *it = find_item(kshell_selected());
    if (!it) {
        fprintf(stderr, "kframe: tap on tab %d, which has no screen\n", kshell_selected());
        return 1;
    }
    if (it->tap) {
        int rc = it->tap(r, x, y);
        if (rc == 0) return 1;
        fprintf(stderr, "kframe: app item %d\n", it->id);
        return rc;
    }
    if (!kshell_inside(x, y, KFRAME_ACT_X, KFRAME_ACT_Y, KFRAME_ACT_W, KFRAME_ACT_H))
        return 1;
    fprintf(stderr, "kframe: app item %d\n", it->id);
    int rc = cfg.on_item(it->id);
    kframe_close();
    if (rc == 0) {
        fprintf(stderr, "kframe: app item %d returned 0; the tap was still consumed\n", it->id);
        return 1;
    }
    return rc;
}

static int tap_shell(int x, int y) {
    int id = kshell_tab_at(x, y);
    if (id == TAB_QUIT) return quit_press();
    if (id) { select_tab(id); return 1; }

    int done = 0;
    if (kshell_keyboard_tap(x, y, &done)) {
        if (done) { pass_focus = 0; ssid_focus = 0; }
        return 1;
    }
    if (!kshell_keyboard_on() &&
        kshell_inside(x, y, KFRAME_BACK_X, KFRAME_BACK_Y, KFRAME_BACK_W, KFRAME_BACK_H))
        return leave_shell();

    id = kshell_selected();
    if (id == TAB_VOLUME) return tap_volume(x, y);
    if (id == TAB_WIFI) return tap_wifi(x, y);
    if (id == TAB_APPS) return tap_apps(x, y);
    if (id == TAB_QUIT) {
        if (kshell_inside(x, y, KFRAME_ACT_X, KFRAME_ACT_Y, KFRAME_ACT_W, KFRAME_ACT_H))
            return quit_press();
        return 1;
    }
    kshell_rect r;
    kshell_content(&r);
    return tap_app(&r, x, y);
}

int kframe_wifi_setup(void) {
    if (need_cfg("wifi") != 0) return -1;
    wifi_blocking = 1;
    wifi_result = 0;
    page = 1;
    disarm_quit();
    wifi_enter();
    kshell_select(TAB_WIFI);
    while (wifi_result == 0) {
        if (cfg.stop && *cfg.stop) { wifi_result = -1; break; }
        paint_shell();
        ek_flush();
        int x, y;
        if (!wait_tap(&x, &y)) { wifi_result = -1; break; }
        kframe_tap(x, y);
        if (cfg.stop && *cfg.stop) { wifi_result = -1; break; }
    }
    int rc = wifi_result == 1 ? 0 : -1;
    wifi_blocking = 0;
    wifi_result = 0;
    kframe_close();
    return rc;
}

int kframe_wifi_shot(const char *what) {
    if (need_cfg("wifi shot") != 0) return -1;
    if (!what) what = "list";
    page = 1;
    kshell_select(TAB_WIFI);
    pass_focus = 0;
    ssid_focus = 0;
    g_wifi_err[0] = 0;
    if (!strcmp(what, "list")) {
        wifi_enter();
    } else if (!strcmp(what, "name")) {
        wifi_mode = WIFI_SSID;
        ssid_focus = 1;
        snprintf(typed_ssid, sizeof typed_ssid, "garden");
        typed_pw[0] = 0;
    } else if (!strcmp(what, "pass")) {
        wifi_mode = WIFI_PASS;
        pass_focus = 1;
        snprintf(typed_ssid, sizeof typed_ssid, "PRNet84");
        snprintf(typed_pw, sizeof typed_pw, "Alotbsol1");
    } else if (!strcmp(what, "sym")) {
        wifi_mode = WIFI_PASS;
        pass_focus = 1;
        snprintf(typed_ssid, sizeof typed_ssid, "PRNet84");
        snprintf(typed_pw, sizeof typed_pw, "a@b.c");
    } else if (!strcmp(what, "fail")) {
        wifi_mode = WIFI_FAIL;
        snprintf(g_wifi_err, sizeof g_wifi_err,
                 "could not join PRNet84 (out of range, or the password was refused)");
    } else {
        fprintf(stderr, "kframe: wifi shot wants list, name, pass, sym or fail (got %s)\n", what);
        return -1;
    }
    shell_sync_focus();
    if (!strcmp(what, "sym")) kshell_keyboard_config(1, 1);
    paint_shell_now();
    return 0;
}

int kframe_tap(int x, int y) {
    if (need_cfg("tap") != 0) return 0;
    if (page == 1) return tap_shell(x, y);
    if (x >= KFRAME_TAB_X && x < KFRAME_TAB_X + KFRAME_TAB_W
        && y >= KFRAME_TAB_Y && y < KFRAME_TAB_Y + KFRAME_TAB_H) {
        kframe_show_menu();
        fprintf(stderr, "kframe: menu open\n");
        return 1;
    }
    return 0;
}


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
    kframe_close();
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
