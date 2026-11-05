#ifndef KSHELL_H
#define KSHELL_H

#include <stddef.h>
/* Shared config / options / inventory shell for one Kindle app.
 *
 * Tabs sit on the top. The app's own tabs are packed from the left.
 * The general tabs (volume, wi-fi, quit) are packed from the right.
 * The gap between the two groups is the split.
 *
 * The keyboard covers the bottom only while a text field is focused.
 * Geometry below is what the tap tests hit. Change a number here and
 * the test script reads this file, so the two cannot drift apart.
 */

#define KSHELL_APP     0
#define KSHELL_GENERAL 1

#define KSHELL_MARGIN    6
#define KSHELL_TAB_Y     4
#define KSHELL_TAB_H     40
#define KSHELL_TAB_GAP   4
/* 60, not 72: chess has four left tabs and the right side is
 * VOLUME WIFI APPS QUIT. At 72 those eight tabs overlap. */
#define KSHELL_TAB_MIN_W 60

#define KSHELL_KB_TOP    468
#define KSHELL_KB_X      12
#define KSHELL_KB_Y0     478
#define KSHELL_KEY_W     54
#define KSHELL_KEY_GAP   4
#define KSHELL_KEY_H     48
#define KSHELL_ROW_H     52
/* asdf row, centered under the 10-key rows. See the assert in kshell.c. */
#define KSHELL_ROW2_X    41

typedef struct {
    int x, y, w, h;
} kshell_rect;

typedef struct {
    const char *label;
    int id;     /* must be non-zero, and unique inside one shell */
    int side;   /* KSHELL_APP or KSHELL_GENERAL */
} kshell_tab;

int  kshell_init(const kshell_tab *tabs, int n); /* 0 ok, -1 if the tabs do not fit */
void kshell_select(int id);
int  kshell_selected(void);
int  kshell_tab_at(int x, int y);                /* id, or 0 when the tap missed the bar */

void kshell_content(kshell_rect *out);           /* the screen under the tabs */
void kshell_paint_tabs(void);                    /* does not clear the page */

/* NULL hides the keyboard. The same buffer on the next call keeps
 * shift and the symbol page where the user left them. */
void kshell_focus(char *buf, size_t cap);
int  kshell_keyboard_on(void);
void kshell_keyboard_config(int sym_page, int shift_on); /* sym_page is 0 or 1 */
void kshell_paint_keyboard(void);
/* 1 when the tap landed on the keyboard (even between keys).
 * *done is 1 when that key was DONE. */
int  kshell_keyboard_tap(int x, int y, int *done);

void kshell_button(int x, int y, int w, int h, const char *label, int on);
void kshell_field(int x, int y, int w, int h, const char *value, int focused);
int  kshell_inside(int x, int y, int bx, int by, int bw, int bh);

#endif
