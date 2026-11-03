/* ek_full_refresh() contract check (host only, no panel needed).
 *
 * What it proves:
 *   1. an ordinary flush never asks for a full-mode refresh, and
 *   2. ek_full_refresh() makes exactly the NEXT flush send one rectangle,
 *      clipped to the part of the requested box that actually changed,
 *   3. and it is one shot: the flush after that is back to normal.
 *
 * Host mode stubs the mxcfb ioctls out, so this says nothing about what the
 * glass does -- it only pins the region arithmetic and the one-shot behaviour.
 */
#include <stdio.h>
#include <string.h>
#include "eink.h"

#define PANEL ((long)EK_W * EK_H)
#define SQ    52                 /* a board square's piece sprite */
#define TILE  8

static int fail(const char *what) { printf("FAIL: %s\n", what); return 1; }

int main(void) {
    ek_open(0);
    ek_dirty_all(); ek_flush();                 /* first paint: the whole panel */
    ek_clear(EK_PAPER); ek_flush();             /* settle to a blank page */

    /* A sprite that moved: one square inked, the square it left now empty. */
    ek_fill_rect(100, 100, SQ, SQ, EK_INK);
    ek_fill_rect(300, 300, SQ, SQ, EK_INK);
    ek_stats plain = ek_flush();
    printf("plain flush:    rects=%d px=%ld full=%d wfm=%s\n",
           plain.rects, plain.px, plain.full, ek_waveform_name(plain.wfm));
    if (plain.full || plain.px >= PANEL / 2) return fail("a plain flush went full-panel");

    ek_clear(EK_PAPER);
    ek_fill_rect(300, 300, SQ, SQ, EK_INK);     /* 100,100 is the vacated square */
    ek_full_refresh(100, 100, 252, 252);        /* ask for the box around both */
    ek_stats full = ek_flush();
    printf("full refresh:   rects=%d px=%ld full=%d wfm=%s\n",
           full.rects, full.px, full.full, ek_waveform_name(full.wfm));
    if (full.rects != 1) return fail("a full refresh must be exactly one update");
    if (full.wfm != ek_waveform_by_name("GC16")) return fail("expected the GC16 waveform");
    long lo = (long)SQ * SQ, hi = (long)(SQ + 2 * TILE) * (SQ + 2 * TILE);
    if (full.px < lo || full.px > hi)
        return fail("the pushed region was not clipped to the changed tiles");

    ek_stats after = ek_flush();
    printf("next flush:     rects=%d px=%ld full=%d\n", after.rects, after.px, after.full);
    if (after.rects || after.px || after.full) return fail("a full refresh must be one shot");

    /* Nothing moved inside the box: a full refresh stays silent. */
    ek_fill_rect(400, 600, SQ, SQ, EK_INK);
    ek_flush();
    ek_full_refresh(0, 0, 100, 100);
    ek_stats idle = ek_flush();
    printf("untouched box:  rects=%d px=%ld\n", idle.rects, idle.px);
    if (idle.rects) return fail("a full refresh over unchanged pixels pushed something");

    printf("full_refresh ok\n");
    return 0;
}
