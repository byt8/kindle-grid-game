#include "kframe.h"
#include "eink.h"
#include <stdio.h>
#include <string.h>

/* tests/wifi-screen-main.c -- a tiny app so the shared wi-fi screens can be
 * driven by taps on this PC. It draws nothing of its own: kframe's wi-fi
 * setup is the whole of the program, which is what the test is about. */

static volatile int stop;
static void repaint(void) { }
static void on_quit(void) { stop = 1; }
static int on_item(int id) { return id; }

int main(int argc, char **argv) {
    const char *dump = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dump") && i + 1 < argc) dump = argv[++i];
    }
    kframe_cfg cfg = {
        .title = "wifi screen test",
        .app_id = "chess",
        .items = NULL,
        .nitems = 0,
        .on_item = on_item,
        .on_quit = on_quit,
        .repaint = repaint,
        .status = NULL,
        .stop = &stop,
        .shot_req = NULL,
        .shot_path = NULL,
    };
    if (kframe_init(&cfg) != 0) {
        fprintf(stderr, "wifi-screen-main: kframe_init failed\n");
        return 1;
    }
    if (ek_open(0) != 0) {
        fprintf(stderr, "wifi-screen-main: ek_open failed\n");
        return 1;
    }
    int rc = kframe_wifi_setup();
    if (dump) ek_dump_pgm(dump, 1);
    ek_close();
    return rc == 0 ? 0 : 1;
}
