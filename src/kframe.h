#ifndef KFRAME_H
#define KFRAME_H
/* Shared Kindle shell for one app at a time.
 *
 * The hamburger (top right) opens one full-screen shell. The app's own
 * tabs sit on the left. VOLUME, WIFI, APPS and QUIT sit on the right.
 * APPS lists the other games. Tapping one writes switch-app and leaves;
 * tools/menu-boot.sh opens that game and remembers it for the next boot.
 * QUIT (second tap) writes .menu-stop and leaves to the reader until then.
 * A screen fills the space under the tabs. The keyboard covers the bottom
 * only while a text field is focused.
 *
 * on_item is used when an app tab has no tap of its own. It must not
 * return 0. Return 1 when the tap is finished, or any other non-zero
 * value to hand that value back to the app (chess uses -2 to mean
 * "ask the model again"). A custom tap does the same: 0 means the tap
 * missed that screen's buttons.
 */

#include "kshell.h"

typedef struct {
    const char *label;
    int id;                                          /* non-zero, not -1/-2/-3/-4 */
    void (*draw)(const kshell_rect *r);              /* NULL: name + one button */
    int (*tap)(const kshell_rect *r, int x, int y);  /* NULL: the button calls on_item */
} kframe_item;

typedef struct {
    const char *title;
    const char *app_id;           /* "chess" or "tetris": this process's name */
    const kframe_item *items;
    int nitems;
    int (*on_item)(int id);
    void (*on_quit)(void);            /* second tap on QUIT */
    void (*repaint)(void);            /* redraw the app and flush */
    void (*status)(const char *msg);  /* one already-formatted line */
    volatile int *stop;              /* signal handler sets this; wifi watches it */
    volatile int *shot_req;           /* SIGUSR1 flag, or NULL */
    const char *shot_path;
} kframe_cfg;

int  kframe_init(const kframe_cfg *cfg);   /* 0 ok, -1 if the config is incomplete */
int  kframe_covering(void);          /* 1 while the shell is up */
void kframe_close(void);             /* back to the app */
void kframe_show_menu(void);         /* for a picture dump; does not flush */
void kframe_paint(void);             /* hamburger, or the shell. does not flush */
int  kframe_tap(int x, int y);       /* 0 = not ours */

int  kframe_wifi_setup(void);        /* 0 when the link is up */
int  kframe_wifi_status(void);       /* 0 when joined and addressed */
int  kframe_wifi_shot(const char *what); /* 0 drawn; -1 if `what` is not list|name|pass|sym|fail */

int  kframe_pwr_presses(void);       /* presses since last call; -1 if the log is dead */
int  kframe_pwr_latched(void);
void kframe_pwr_set_latch(int on);
int  kframe_pwr_poll(void);          /* 1 after a press was handled (slept or failed in the open) */
int  kframe_count_pwr_records(const char *text);
int  kframe_pwr_log_grew(const char *prev, const char *cur, int len);

/* The hamburger, drawn on the game. A tap at (560, 10) lands on it. */
#define KFRAME_TAB_X 548
#define KFRAME_TAB_Y 2
#define KFRAME_TAB_W 48
#define KFRAME_TAB_H 26

/* The big button on an app screen (NEW, RETRY, CLOCK). Centre (300, 236). */
#define KFRAME_ACT_X 24
#define KFRAME_ACT_Y 200
#define KFRAME_ACT_W 552
#define KFRAME_ACT_H 72

/* Bottom-left of every screen, hidden while the keyboard is up. */
#define KFRAME_BACK_X 12
#define KFRAME_BACK_Y 736
#define KFRAME_BACK_W 160
#define KFRAME_BACK_H 48

/* Wi-fi list. A tap at (300, 141) hits the first network, (300, 193) the second. */
#define KFRAME_WIFI_ROW_Y 118
#define KFRAME_WIFI_ROW_H 46
#define KFRAME_WIFI_ROW_P 52
#define KFRAME_WIFI_BTN_Y 660

/* APPS rows. The running app is left off the list.
 * Row i is centred at
 * (KFRAME_APP_ROW_X + KFRAME_APP_ROW_W/2,
 *  KFRAME_APP_ROW_Y + i*KFRAME_APP_ROW_P + KFRAME_APP_ROW_H/2).
 * xcom is first in the catalog, so from chess or tetris the other one
 * is row 1. Centre (300, 212). */
#define KFRAME_APP_ROW_X 16
#define KFRAME_APP_ROW_W 568
#define KFRAME_APP_ROW_Y 140
#define KFRAME_APP_ROW_H 44
#define KFRAME_APP_ROW_P 50

/* JOIN / NEXT, under the text field. Centre (438, 228). */
#define KFRAME_WIFI_GO_X 300
#define KFRAME_WIFI_GO_Y 200
#define KFRAME_WIFI_GO_W 276
#define KFRAME_WIFI_GO_H 56

#define KFRAME_FIELD_X 16
#define KFRAME_FIELD_Y 108
#define KFRAME_FIELD_W 568
#define KFRAME_FIELD_H 44

#endif
