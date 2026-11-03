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
    /* The box goes out as asked. Snapping it to the 8x8 tile grid dragged an
     * edge through whatever sat beside the square; the requested rectangle is
     * the contract now. */
    if (full.px != 252L * 252L)
        return fail("the pushed region was not the requested rectangle");

    /* And a box that is not aligned to the tile grid is neither rounded down
     * nor grown: only the caller's own rectangle is refreshed. */
    ek_clear(EK_PAPER); ek_flush();
    /* Ink runs past the box so the last partial byte of the box is not empty:
     * a shadow copy that stopped at whole bytes would leave those pixels
     * stale, and the box would report itself dirty on the next flush. */
    ek_fill_rect(201, 205, 46, 40, EK_INK);
    ek_full_refresh(201, 205, 40, 40);          /* odd origin, odd size */
    ek_stats odd = ek_flush();
    printf("unaligned box:  rects=%d px=%ld\n", odd.rects, odd.px);
    if (odd.px != 40L * 40L)
        return fail("a non-aligned rect was rounded to the tile grid");
    /* The shadow must now agree with the canvas right up to the last byte the
     * box covers. Touch that same row band again: only the new 4x4 should go
     * out. A shadow that stopped at whole bytes leaves the box's tail byte
     * stale, and that part of the row is pushed again for nothing -- a
     * spurious flash inside the square. */
    ek_fill_rect(210, 206, 4, 4, EK_PAPER);
    ek_stats odd2 = ek_flush();
    printf("  then a touch: rects=%d px=%ld\n", odd2.rects, odd2.px);
    /* The 4x4 lives in one tile, so exactly one rectangle may go out. A
     * second one is the box's tail byte, left stale by a shadow copy that
     * stopped at whole bytes. */
    if (odd2.rects != 1)
        return fail("the shadow lagged an unaligned rect; dead area went out again");

    /* Two boxes asked for in one frame must both go out. Storing only the last
     * one silently dropped a chess move's vacated square. */
    ek_clear(EK_PAPER); ek_flush();
    ek_fill_rect(100, 100, SQ, SQ, EK_INK);     /* drawn, not yet flushed: */
    ek_fill_rect(400, 100, SQ, SQ, EK_INK);     /* exactly as a move leaves it */
    ek_full_refresh(90, 90, 80, 80);
    ek_full_refresh(300, 300, 80, 80);          /* nothing changed in this one */
    ek_full_refresh(390, 90, 80, 80);
    ek_stats two = ek_flush();
    printf("two boxes:      rects=%d px=%ld\n", two.rects, two.px);
    if (two.rects != 2) return fail("a queued full refresh was dropped");

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
