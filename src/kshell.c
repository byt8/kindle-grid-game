#include "kshell.h"
#include "eink.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define KSHELL_MAX 16

typedef struct {
    const char *label;
    int id;
    int side;
    int x, y, w, h;
} tab_box;

static tab_box boxes[KSHELL_MAX];
static int nboxes;
static int selected;
static int ready;

static char *focus_buf;
static size_t focus_cap;
static int shift_on;
static int sym_page; /* 0 letters, 1 punctuation */

_Static_assert(KSHELL_ROW2_X == KSHELL_KB_X +
    ((10 * KSHELL_KEY_W + 9 * KSHELL_KEY_GAP) - (9 * KSHELL_KEY_W + 8 * KSHELL_KEY_GAP)) / 2,
    "the asdf row must sit centered under the ten-key rows");

static int pitch(void) { return KSHELL_KEY_W + KSHELL_KEY_GAP; }

static const char *row_text(int page, int row) {
    static const char *let[4] = { "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm" };
    static const char *sym[4] = { "@.-_!#$%&*", "+=/?;:,()'", "\"~^|\\<>{}", "[]`" };
    if (row < 0 || row > 3) return "";
    return page ? sym[row] : let[row];
}

static int row_slots(int row) {
    if (row == 2) return 9;
    if (row == 3) return 7; /* between SHIFT and DEL */
    return 10;
}

static int rows_fit(void) {
    for (int page = 0; page < 2; page++) {
        for (int row = 0; row < 4; row++) {
            int n = (int)strlen(row_text(page, row));
            int slots = row_slots(row);
            if (n > slots) {
                fprintf(stderr, "kshell: page %d row %d has %d keys and %d slots\n",
                        page, row, n, slots);
                return 0;
            }
        }
    }
    return 1;
}

static int tab_width(const char *label) {
    int n = label ? (int)strlen(label) : 1;
    if (n > 14) n = 14;
    int w = n * 8 + 20;
    if (w < KSHELL_TAB_MIN_W) w = KSHELL_TAB_MIN_W;
    return w;
}

static void fit_label(char *dst, size_t n, const char *src, int px) {
    int maxc = px / 8;
    if (maxc < 1) maxc = 1;
    if (maxc > (int)n - 1) maxc = (int)n - 1;
    snprintf(dst, n, "%s", src ? src : "");
    if ((int)strlen(dst) > maxc) dst[maxc] = 0;
}

int kshell_init(const kshell_tab *tabs, int n) {
    ready = 0;
    nboxes = 0;
    selected = 0;
    focus_buf = NULL;
    focus_cap = 0;
    if (!rows_fit()) return -1;
    if (!tabs || n < 1 || n > KSHELL_MAX) {
        fprintf(stderr, "kshell: need between 1 and %d tabs (got %d)\n", KSHELL_MAX, n);
        return -1;
    }
    for (int i = 0; i < n; i++) {
        if (!tabs[i].label || !tabs[i].label[0]) {
            fprintf(stderr, "kshell: tab %d has no name\n", i);
            return -1;
        }
        if (tabs[i].id == 0) {
            fprintf(stderr, "kshell: tab \"%s\" uses id 0, which means 'missed'\n", tabs[i].label);
            return -1;
        }
        if (tabs[i].side != KSHELL_APP && tabs[i].side != KSHELL_GENERAL) {
            fprintf(stderr, "kshell: tab \"%s\" side %d is not app or general\n",
                    tabs[i].label, tabs[i].side);
            return -1;
        }
        for (int j = 0; j < i; j++) {
            if (tabs[j].id == tabs[i].id) {
                fprintf(stderr, "kshell: two tabs use id %d\n", tabs[i].id);
                return -1;
            }
        }
        boxes[i].label = tabs[i].label;
        boxes[i].id = tabs[i].id;
        boxes[i].side = tabs[i].side;
        boxes[i].w = tab_width(tabs[i].label);
        boxes[i].y = KSHELL_TAB_Y;
        boxes[i].h = KSHELL_TAB_H;
    }
    nboxes = n;

    int x = KSHELL_MARGIN;
    int app_right = KSHELL_MARGIN;
    int first_app = 0;
    for (int i = 0; i < n; i++) {
        if (boxes[i].side != KSHELL_APP) continue;
        boxes[i].x = x;
        x += boxes[i].w + KSHELL_TAB_GAP;
        app_right = boxes[i].x + boxes[i].w;
        if (!first_app) first_app = boxes[i].id;
    }

    int gx = EK_W - KSHELL_MARGIN;
    int gen_left = EK_W - KSHELL_MARGIN;
    int any_gen = 0;
    for (int i = n - 1; i >= 0; i--) {
        if (boxes[i].side != KSHELL_GENERAL) continue;
        gx -= boxes[i].w;
        boxes[i].x = gx;
        gx -= KSHELL_TAB_GAP;
        gen_left = boxes[i].x;
        any_gen = 1;
    }
    /* General tabs were placed right-to-left, so the leftmost general tab
     * is the last one visited only if we walked backwards. gen_left is that. */
    if (any_gen && app_right + 8 > gen_left) {
        fprintf(stderr, "kshell: tabs overlap (app ends at %d, general starts at %d)\n",
                app_right, gen_left);
        nboxes = 0;
        return -1;
    }
    selected = first_app ? first_app : boxes[0].id;
    ready = 1;
    return 0;
}

void kshell_select(int id) {
    if (!ready) {
        fprintf(stderr, "kshell: select before init\n");
        return;
    }
    for (int i = 0; i < nboxes; i++) {
        if (boxes[i].id == id) { selected = id; return; }
    }
    fprintf(stderr, "kshell: no tab with id %d\n", id);
}

int kshell_selected(void) { return selected; }

int kshell_tab_at(int x, int y) {
    if (!ready) return 0;
    for (int i = 0; i < nboxes; i++) {
        if (x >= boxes[i].x && x < boxes[i].x + boxes[i].w &&
            y >= boxes[i].y && y < boxes[i].y + boxes[i].h)
            return boxes[i].id;
    }
    return 0;
}

void kshell_focus(char *buf, size_t cap) {
    if (buf == focus_buf) {
        focus_cap = cap;
        return;
    }
    focus_buf = buf;
    focus_cap = cap;
    if (buf) { shift_on = 0; sym_page = 0; }
}

int kshell_keyboard_on(void) { return focus_buf != NULL; }

void kshell_keyboard_config(int page, int shift) {
    if (page < 0 || page > 1) {
        fprintf(stderr, "kshell: symbol page %d is not 0 or 1\n", page);
        return;
    }
    sym_page = page;
    shift_on = shift ? 1 : 0;
}

void kshell_content(kshell_rect *out) {
    out->x = 12;
    out->y = KSHELL_TAB_Y + KSHELL_TAB_H + 14;
    out->w = EK_W - 24;
    int bottom = kshell_keyboard_on() ? KSHELL_KB_TOP : EK_H;
    out->h = bottom - 8 - out->y;
    if (out->h < 0) out->h = 0;
}

static void paint_one_tab(const tab_box *t, int on) {
    char shown[16];
    fit_label(shown, sizeof shown, t->label, t->w - 12);
    int tw = (int)strlen(shown) * 8;
    int tx = t->x + (t->w - tw) / 2;
    int ty = t->y + (t->h - 16) / 2;
    if (on) {
        ek_fill_rect(t->x, t->y, t->w, t->h, EK_INK);
        ek_text(tx, ty, shown, EK_PAPER);
    } else {
        ek_fill_rect(t->x, t->y, t->w, t->h, EK_PAPER);
        ek_rect(t->x, t->y, t->w, t->h, EK_INK);
        ek_text(tx, ty, shown, EK_INK);
    }
}

void kshell_paint_tabs(void) {
    if (!ready) {
        fprintf(stderr, "kshell: paint before init\n");
        return;
    }
    ek_dither_rect(0, 0, EK_W, KSHELL_TAB_Y + KSHELL_TAB_H + 6, 1);
    int app_right = KSHELL_MARGIN;
    int gen_left = EK_W - KSHELL_MARGIN;
    int any_app = 0, any_gen = 0;
    for (int i = 0; i < nboxes; i++) {
        paint_one_tab(&boxes[i], boxes[i].id == selected);
        if (boxes[i].side == KSHELL_APP) {
            int r = boxes[i].x + boxes[i].w;
            if (r > app_right) app_right = r;
            any_app = 1;
        } else {
            if (!any_gen || boxes[i].x < gen_left) gen_left = boxes[i].x;
            any_gen = 1;
        }
    }
    if (any_app && any_gen && gen_left - app_right > 16) {
        int mid = (app_right + gen_left) / 2;
        ek_vline(mid, 10, 28, EK_INK);
    }
    ek_hline(0, KSHELL_TAB_Y + KSHELL_TAB_H + 6, EK_W, EK_INK);
}

void kshell_button(int x, int y, int w, int h, const char *label, int on) {
    if (w <= 0 || h <= 0) return;
    const char *s = label ? label : "";
    int tw = (int)strlen(s) * 8;
    int tx = x + (w - tw) / 2;
    int ty = y + (h - 16) / 2;
    if (on) {
        ek_fill_rect(x, y, w, h, EK_INK);
        ek_text(tx, ty, s, EK_PAPER);
        return;
    }
    ek_fill_rect(x, y, w, h, EK_PAPER);
    if (h > 10) ek_dither_rect(x + 2, y + h - 6, w - 4, 4, 4);
    ek_rect(x, y, w, h, EK_INK);
    ek_rect(x + 1, y + 1, w - 2, h - 2, EK_INK);
    ek_text(tx, ty, s, EK_INK);
}

void kshell_field(int x, int y, int w, int h, const char *value, int focused) {
    const char *s = value ? value : "";
    int maxc = (w - 24) / 8;
    if (maxc < 1) maxc = 1;
    if ((int)strlen(s) > maxc) s += (int)strlen(s) - maxc;
    ek_fill_rect(x, y, w, h, EK_PAPER);
    ek_rect(x, y, w, h, EK_INK);
    if (focused) ek_rect(x + 2, y + 2, w - 4, h - 4, EK_INK);
    int ty = y + (h - 16) / 2;
    ek_text(x + 8, ty, s, EK_INK);
    if (focused) {
        int cx = x + 8 + (int)strlen(s) * 8;
        if (cx <= x + w - 16) ek_fill_rect(cx, ty, 8, 16, EK_INK);
    }
}

int kshell_inside(int x, int y, int bx, int by, int bw, int bh) {
    return x >= bx && y >= by && x < bx + bw && y < by + bh;
}

void kshell_options(int y0, const char *const *labels, int n, int chosen) {
    for (int i = 0; i < n; i++)
        kshell_button(KSHELL_OPT_X, y0 + i * KSHELL_OPT_STEP,
                      KSHELL_OPT_W, KSHELL_OPT_H, labels[i], i == chosen);
}

int kshell_option_at(int y0, int n, int x, int y) {
    for (int i = 0; i < n; i++)
        if (kshell_inside(x, y, KSHELL_OPT_X, y0 + i * KSHELL_OPT_STEP,
                          KSHELL_OPT_W, KSHELL_OPT_H))
            return i;
    return -1;
}

static void paint_key(int x, int y, int w, int h, const char *label, int on) {
    kshell_button(x, y, w, h, label, on);
}

static void key_xy(int row, int index, int *x, int *y) {
    *y = KSHELL_KB_Y0 + row * KSHELL_ROW_H;
    if (row == 2) *x = KSHELL_ROW2_X + index * pitch();
    else if (row == 3) *x = KSHELL_KB_X + (index + 2) * pitch();
    else *x = KSHELL_KB_X + index * pitch();
}

static int shift_w(void) { return 2 * KSHELL_KEY_W + KSHELL_KEY_GAP; }

void kshell_paint_keyboard(void) {
    if (!focus_buf) return;
    ek_dither_rect(0, KSHELL_KB_TOP, EK_W, EK_H - KSHELL_KB_TOP, 1);
    ek_hline(0, KSHELL_KB_TOP, EK_W, EK_INK);
    ek_hline(0, KSHELL_KB_TOP + 1, EK_W, EK_INK);

    for (int row = 0; row < 4; row++) {
        const char *s = row_text(sym_page, row);
        int len = (int)strlen(s);
        for (int i = 0; i < len; i++) {
            int x, y;
            key_xy(row, i, &x, &y);
            char label[2];
            unsigned char ch = (unsigned char)s[i];
            if (!sym_page && shift_on && islower(ch)) ch = (unsigned char)toupper(ch);
            label[0] = (char)ch;
            label[1] = 0;
            paint_key(x, y, KSHELL_KEY_W, KSHELL_KEY_H, label, 0);
        }
    }
    int y3 = KSHELL_KB_Y0 + 3 * KSHELL_ROW_H;
    paint_key(KSHELL_KB_X, y3, shift_w(), KSHELL_KEY_H, "SHIFT", shift_on);
    int del_x = KSHELL_KB_X + 9 * pitch();
    paint_key(del_x, y3, KSHELL_KEY_W, KSHELL_KEY_H, "DEL", 0);

    int y4 = KSHELL_KB_Y0 + 4 * KSHELL_ROW_H;
    int wide = shift_w();
    paint_key(KSHELL_KB_X, y4, wide, KSHELL_KEY_H, sym_page ? "ABC" : "123", sym_page);
    int space_x = KSHELL_KB_X + wide + KSHELL_KEY_GAP;
    int done_x = KSHELL_KB_X + (10 * KSHELL_KEY_W + 9 * KSHELL_KEY_GAP) - wide;
    int space_w = done_x - KSHELL_KEY_GAP - space_x;
    paint_key(space_x, y4, space_w, KSHELL_KEY_H, "SPACE", 0);
    paint_key(done_x, y4, wide, KSHELL_KEY_H, "DONE", 0);
}

static int insert_char(char ch) {
    if (!focus_buf || focus_cap < 2) {
        fprintf(stderr, "kshell: a key was tapped with no text field\n");
        return -1;
    }
    size_t n = strlen(focus_buf);
    if (n + 1 >= focus_cap) {
        fprintf(stderr, "kshell: the field is full (%zu characters)\n", focus_cap - 1);
        return -1;
    }
    focus_buf[n] = ch;
    focus_buf[n + 1] = 0;
    return 0;
}

int kshell_keyboard_tap(int x, int y, int *done) {
    if (done) *done = 0;
    if (!focus_buf) return 0;
    if (y < KSHELL_KB_TOP) return 0;

    int y3 = KSHELL_KB_Y0 + 3 * KSHELL_ROW_H;
    int y4 = KSHELL_KB_Y0 + 4 * KSHELL_ROW_H;
    int wide = shift_w();

    if (kshell_inside(x, y, KSHELL_KB_X, y3, wide, KSHELL_KEY_H)) {
        if (!sym_page) shift_on = !shift_on;
        return 1;
    }
    int del_x = KSHELL_KB_X + 9 * pitch();
    if (kshell_inside(x, y, del_x, y3, KSHELL_KEY_W, KSHELL_KEY_H)) {
        size_t n = strlen(focus_buf);
        if (n) focus_buf[n - 1] = 0;
        return 1;
    }
    if (kshell_inside(x, y, KSHELL_KB_X, y4, wide, KSHELL_KEY_H)) {
        sym_page = sym_page ? 0 : 1;
        return 1;
    }
    int space_x = KSHELL_KB_X + wide + KSHELL_KEY_GAP;
    int done_x = KSHELL_KB_X + (10 * KSHELL_KEY_W + 9 * KSHELL_KEY_GAP) - wide;
    int space_w = done_x - KSHELL_KEY_GAP - space_x;
    if (kshell_inside(x, y, space_x, y4, space_w, KSHELL_KEY_H)) {
        insert_char(' ');
        shift_on = 0;
        return 1;
    }
    if (kshell_inside(x, y, done_x, y4, wide, KSHELL_KEY_H)) {
        if (done) *done = 1;
        return 1;
    }

    for (int row = 0; row < 4; row++) {
        const char *s = row_text(sym_page, row);
        int len = (int)strlen(s);
        for (int i = 0; i < len; i++) {
            int kx, ky;
            key_xy(row, i, &kx, &ky);
            if (!kshell_inside(x, y, kx, ky, KSHELL_KEY_W, KSHELL_KEY_H)) continue;
            unsigned char ch = (unsigned char)s[i];
            if (!sym_page && shift_on && islower(ch)) ch = (unsigned char)toupper(ch);
            if (insert_char((char)ch) == 0) shift_on = 0;
            return 1;
        }
    }
    return 1;
}
