# calib

Status: **built, builds clean both ways — not yet run on a device.** Purpose is
to answer, by tapping the glass, what `--probe` logs cannot: does the panel's
visible motion match the engine's submitted timing, and does the eye see a
tick/step cadence the log says is even?

## What it is

`src/calib.c` — one file, same shape as the games (single-file C, shared e-ink
engine `eink.c`, `EK_HOST` PC build, `EK_TAKEOVER` device build). Three screens
you drive by tapping, plus QUIT bottom-right to give the panel back:

- **CLOCK (screen 1)** — a 1 Hz digit on the glass, its measured `tk_ms`
  printed large. Tap NEXT when cadence looks even / the trace button when
  bunched. Per-tick `calib: tick at N (due N, ms N)` logs in `--probe` mode.
- **GHOST (screen 2)** — two 30px squares moving down on the same 30px pitch.
  Left always erases cleanly; right leaves its old cell inked when
  `trace_on` (the TRACE ON/OFF button). Tap CLEAN if you see no smear, to
  answer whether the eye catches the engine's erase cost. This is a *choice*
  the machine does not predict.
- **TAPLAG (screen 3)** — a spot jumps to a new row every 1.5 s; tap the
  instant you see it. `last: N us` shows the measured `(tap_ek_ms −
  taplag_at) * 1000` reaction on the glass too.

There is also `--host-stdin` for a PC smoke-test: it reads `tap X Y` lines
like the games, so the whole loop (except real touch) runs off-device.
`--clock N` sets the tick interval; `--probe` prints flush/tick timing to
stderr.

## Commands

```
make calib-host          # PC build (-Werror clean)
./calib-host --no-takeover --host-stdin --dump /tmp/calib-clock.pgm
make calib-kindle        # static ARM musl
make calib-test          # host selftest + clock PGM check (check_pgm)
./run-calib-on-kindle.sh [IP] [--probe]   # push + take screen + log + restore
```

`make calib-test` needs `calib-host` built first.

## Facts learned the hard way

- **The host build has no touch; the device build has no `tap X Y` pipe.** The
  first compile failed `-Werror` with "defined but not used" for three draw
  functions and `tap_screen` because the only caller was under the device
  `#ifndef EK_HOST` path. Fix: split input the way tetris does — host reads a
  `tap X Y` line (here `--host-stdin`), device polls the touch queue; then all
  the same functions are used on *both* paths and the warnings vanish. A
  shared `draw_screen(s)` dispatcher also needs *forward* declarations of the
  three draw functions, since C needs the decl above first use.
- **`snprintf` into `char buf[8]` fails `-Werror=format-truncation=`** with
  `%d` (the compiler can't prove a single digit won't exceed the buffer).
  Use `char buf[16]`.
- **The engine's `ek_flush()` cost is the answer to "is the piece moving too
  fast for the panel": the panel settles every ~250-330 ms, and a `tk`
  interval can be smaller. That is what the CLOCK screen is for — a smaller
  `--clock N` shows whether drops visibly bunch.
- **A screen-switch after NEXT must re-draw the whole target screen** — just
  mutating `screen` and waiting for the next periodic step leaves the old
  screen's content + new buttons mixed.

## Layout (keep in step with `src/calib.c`)

Buttons `BTN_W 186, BTN_H 70` at `BTN_Y1 560 / BTN_Y2 640`, x 0/206/412;
well area the top 520 rows. Clock digit centered at `(300, 220)` scale-4.
`BTN[0]` = QUIT (bottom-left), `BTN[4]` = NEXT. Host stdin is `tap X Y`;
`--dump FILE` writes the 600x800 PGM. `tools/pgm2png.py` converts.

## Open / next

- Run on-device: `make calib-kindle && ./run-calib-on-kindle.sh 192.168.0.66
  --probe`, then the user taps through the three screens. Answer the
  CLOCK cadence and TAPLAG `last:` numbers from the log.
- If CLOCK tells the user the ticks bunch at some `--clock N`, the fix is in
  the engine/game flush policy (fewer/auto fulls, waveform choice), not the
  `drop_at +=` anchor — that fix is already proven on-device (see tetris KB).