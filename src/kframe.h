#ifndef KFRAME_H
#define KFRAME_H
/* Shared Kindle shell for one app at a time.
 *
 * The hamburger (top right) opens one menu. The top of that menu is always
 * the same: QUIT, VOLUME, WIFI. Under that, the app lists its own rows.
 * Sleep (the power key) lives here too. Only chess calls this so far.
 *
 * on_item must not return 0. Return 1 when the tap is finished, or any other
 * non-zero value to hand that value back to the app's own tap loop
 * (chess uses -2 to mean "ask the model again").
 */

typedef struct {
    const char *label;
    int id;
} kframe_item;

typedef struct {
    const char *title;
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
int  kframe_covering(void);          /* 1 when the menu or the volume page is up */
void kframe_show_menu(void);         /* for a picture dump; does not flush */
void kframe_paint(void);             /* tab, or the full page. does not flush */
int  kframe_tap(int x, int y);       /* 0 = not ours */

int  kframe_wifi_setup(void);        /* 0 when the link is up */
int  kframe_wifi_status(void);       /* 0 when joined and addressed */
int  kframe_wifi_shot(const char *what); /* 0 drawn; -1 if `what` is not list|name|pass|fail */

int  kframe_pwr_presses(void);       /* presses since last call; -1 if the log is dead */
int  kframe_pwr_latched(void);
void kframe_pwr_set_latch(int on);
int  kframe_pwr_poll(void);          /* 1 after a press was handled (slept or failed in the open) */
int  kframe_count_pwr_records(const char *text);
int  kframe_pwr_log_grew(const char *prev, const char *cur, int len);

/* The hamburger. A tap at (560, 10) lands on it. */
#define KFRAME_TAB_X 548
#define KFRAME_TAB_Y 2
#define KFRAME_TAB_W 48
#define KFRAME_TAB_H 26

/* Menu rows. QUIT is row 0: a tap at (300, 110) hits it. VOLUME is row 1. */
#define KFRAME_ROW_X   24
#define KFRAME_ROW_W   552
#define KFRAME_ROW_H   52
#define KFRAME_ROW_GAP 8
#define KFRAME_ROW_Y0  88

#endif
