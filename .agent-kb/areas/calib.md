# calib

Status: **built, builds clean both ways — not yet run on a device.** Purpose is
now the *fixer*, not the asker: a live on-glass TUNER whose every knob maps to a
real engine setting the games use, so you dial the panel until the motion
*looks* even and the values you end on are the settings tetris should ship.

## What it is

`src/calib.c` — one file, same shape as the games (single-file C, shared e-ink
engine `eink.c`, `EK_HOST` PC build, `EK_TAKEOVER` device build).

- **TUNER (screen 1)** — a block drops at a tuneable speed (deadline-anchored
  exactly like tetris), and six real engine knobs change live on the glass:
  - **SPEED** `-`/`+` — ms between drops (default 800 = tetris level 1, 200–2000).
  - **CLEAN** `-`/`+` — a real GC16 clean fires every N drops (10–90, 0 = never),
    like tetris's `clean_next` frames. `step_n % knob_clean == 0` in `present()`.
  - **WFM** — waveform: AUTO / GC16F / GL16F / GC16, resolved
    `ek_waveform_by_name()` into `wfm[4]` at startup and forced per-flush.
  - **WAIT / MONO** chips — toggle `ek_wait_updates()` and `ek_mono()` live.
  - The **GAP / FLASH** line prints honest on-glass numbers: wall-ms between the
    last two drops (including the wait for the panel to settle) and the last
    flush's `st.us` in ms (the grey flash you see).
  - Bottom row is a 2×3 keypad: `-` QUIT `+` over SPEED / CLEAN / WFM; the `>`
    marks which knob `-`/`+` moves. Knob value rows live at x<=250, reference
    well at x=470..580 so they never overlap.
- **GHOST (screen 2)** — two 30px squares moving down on the same 30px pitch.
  Left always erases cleanly; right leaves its old cell inked when `trace_on`
  (the TRACE ON/OFF button). Tap CLEAN if you see no smear.
- **TAPLAG (screen 3)** — a spot jumps to a new row every 1.5 s; tap the
  instant you see it. `last: N us` shows the measured `(tap_ek_ms −
  taplag_at) * 1000` reaction on the glass too.

`--host-stdin` reads `tap X Y` lines like the games for a PC smoke-test;
`--speed N` sets the starting drop interval. `--probe` prints
`calib: drop at N (due N, ms N)` + flush stats.

## Commands

```
make calib-host          # PC build (-Werror clean)
./calib-host --no-takeover --host-stdin --dump /tmp/calib-tuner.pgm
make calib-kindle        # static ARM musl
make calib-test          # host selftest + tuner PGM check (check_pgm)
./run-calib-on-kindle.sh [IP] [--probe]   # push + take screen + log + restore
```

## Facts learned the hard way

- **Host build has no touch; device build has no `tap X Y` pipe.** Split input
  the way tetris does so all functions are used on both paths (fixes
  `-Werror=unused-function`), and forward-declare the draw functions above
  `draw_screen(s)`.
- **`snprintf` into `char buf[8]` fails `-Werror=format-truncation=` with `%d`.**
  Use `char buf[16]` or bigger.
- **`ek_flush()` cost (`st.us`) is the grey flash duration** — that is what the
  eye reads as "faster than 800 ms". At 800 ms spacing a ~300 ms grey ladder
  means the clear image only shows ~500 ms, so motion *never* looks metronome-
  even on e-ink. This is why the tuner exists: pick a cadence/waveform where
  the flash is acceptable to the eye, then bake it into the game.
- **A screen-switch after NEXT must re-draw the whole target screen**, and
  re-entering the tuner should reset `step_n` so the clean cadence restarts sane.
- **Move the reference well to x>470 in the tuner** — scale-2 knob text runs to
  ~x448, so a well at x=250 overlapped the text. Win (host, verified visually):
  `tuner-layout.png` shows chips top-left, 3 knob rows, GAP/FLASH line, well far
  right, 2×3 keypad, no overlap.
- **Applying a knob re-anchors the drop** (`drop_at = now + knob_speed`) so a
  mid-change doesn't cause an immediate double-drop. Probe on host at
  `--speed 700` confirms drops land exactly 700 ms apart, wfm=1 (or the engine's
  periodic soft-clean wfm=6) per flush.

## Layout (keep in step with `src/calib.c`)

Keypad `BTN_W 186, BTN_H 70` at `BTN_Y1 560 / BTN_Y2 640`, x 0/206/412:
row 1 = `-` QUIT `+`, row 2 = SPEED CLEAN WFM. Chips `WAIT/MONO` at (20..400, 30..76).
Knob value rows y 240/300/360, GAP/FLASH y 430, all x<=230 text / x<=250 fill.
Reference well `ek_rect(470,48,110,470)` with the block at x 482, stepping +30.
`--dump FILE` writes the 600x800 PGM; `tools/pgm2png.py` converts.

## Open / next

- Run on-device: `make calib-kindle && ./run-calib-on-kindle.sh 192.168.0.66
  --probe`, then tap through the tuner and settle on the values that look even.
- Then bake the chosen SPEED/CLEAN/WFM into tetris's `drop_ms()` / `present()`
  / `main()` (the tuner already mirrors those exact code paths).