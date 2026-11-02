# calib

Status: **built, host-tested — not yet run on a device.** Opens on the RX
screen: the same black square stepped on one beat, left with GC16, right with
DU, both times printed on the glass. NEXT is the tuner.

## What it is

`src/calib.c` — one file, same shape as the games (single-file C, shared e-ink
engine `eink.c`, `EK_HOST` PC build, `EK_TAKEOVER` device build).

- **RX (screen 4, the one it opens on)** — two wells. Each step flushes the
  left square alone as GC16, then the right square as DU, then the numbers.
  `ek_ghost(0, 0)` on this screen so the engine cannot swap in its own
  cadence. A forced waveform that does not come back stops the program.
  QUIT and NEXT are the only buttons. `--steps N` quits after N pairs.
- **TUNER (screen 1)** — a block drops at a tuneable speed (deadline-anchored
  exactly like tetris), and six real engine knobs change live on the glass:
  - **SPEED** `-`/`+` — ms between drops (default 800 = tetris level 1, 200–2000).
  - **CLEAN** `-`/`+` — a real GC16 clean fires every N drops (10–90, 0 = never),
    like tetris's `clean_next` frames. `step_n % knob_clean == 0` in `present()`.
  - **WFM** — waveform: AUTO / DU / GC16F / GL16F / GC16, resolved
    `ek_waveform_by_name()` at startup and forced per-flush. Cleans use the
    GC16 id, not a slot number.
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

- [verified 2026-11-02 src/calib.c, jailbreak-kindle/.../eink.c] **Grey motion cannot look even.** GC16 / GC16F / GL16F are a multi-zap that parks the pigment halfway. That zap is the blink. DU (waveform id 1) is one push, black or white, and is what this engine already uses for motion when the caller does not force a grey mode. A2 (id 4, the named animation waveform) is not wired in `ek_waveform_by_name`. It lives in the shared engine. Do not add it without a yes.
- [verified 2026-11-02 eink.c `st.us = ek_ms() - t0`] **`ek_stats.us` is milliseconds** despite the name. The tuner used to print it divided by 1000, so FLASH was always 0. It prints the number directly now.
- [ran 2026-11-02 make calib-test] Two RX pairs on the host submit `rx grey wfm=2` then `rx ink wfm=1`. Layout is `rx-layout.png`.
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

## Decisions

- The comparison is a calib screen, using waveforms the engine already has. Rejected: retuning grey flash length, and editing shared `eink.c` to add A2 before anyone has looked at DU on the glass.

## Dead ends

- Dialing SPEED / GC16F / GL16F until the grey zap looks metronome-even. The blink is the waveform. A faster grey mode is a shorter blink, not a steady picture. Retry only for a still image that needs real grey, not for motion.

## The motion ladder (user-facing, in README)

Gate: `GAP ≈ SPEED` (±~60 ms). Steps: 1) WFM=DU, 2) SPEED 800, raise by 50 until
VERDICT says KEEPS THE BEAT, 3) grey zap = real FLASH cost, not a fix — DU for
motion, grey only for still art, 4) CLEAN 30 only when ghosting shows (drop 20/10
if trails persist), 5) WAIT stays ON (OFF makes GAP lie), 6) bake what the tuner
chose into the game.

## Open / next

- [ran 2026-11-02 @192.168.0.66 port 22 open] `make calib && ./run-calib-on-kindle.sh 192.168.0.66` pushed and the app took the panel (fb0 600x800, touch event3). User said **"du seems to work well"** — DU is the right motion waveform. Next: bake DU into the games' motion flushes (tetris already auto-picks DU when nothing forces grey), and read the two RX ms numbers only if the beat still needs tuning.
- A2 is still unwired. Add it in the shared engine only if DU on this Pearl panel is too slow and the user says to edit jailbreak-kindle.