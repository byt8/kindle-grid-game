# Running the games on this PC (a local "emulator")

Investigated 2026-11-06. Answer to "can we effectively have an emulator locally, how much work".

## What already exists (measured, not assumed)

| Piece | State | Where |
|---|---|---|
| Panel faked in RAM | **done** — `-DEK_HOST` makes `submit_update`/`init_fb`/takeover no-ops and gives a `calloc`'d 608x800 8bpp "fb" | `jailbreak-kindle/.../engine/eink.c:304`, `:958` |
| Frame to a file | **done** — `ek_dump_pgm()` writes canvas + grey layer at full fidelity | `eink.c:1372` |
| Taps on the PC | **done per game** — a pipe of `tap X Y` lines → `tap_down()` → `present()` | `src/*.c` `#ifdef EK_HOST` block |
| Per-*repaint* frame output | **missing** for every game except Brogue (`KFB_PREVIEW=/tmp/kfb-frames`) | `Makefile:647` |
| Live clock / game loop on host | **missing** — the host path is one frame per tap, no timer | e.g. `src/tetris.c:1114-1129` |
| Interactive taps | **blocked** by `!isatty(0)` on the host path — a pipe works, a terminal does not | `src/*.c` |
| `ek_poll` on the PC | reads `touch_fd` (`/dev/input/event3`) only; nothing feeds it on host | `eink.c:425` |
| Viewers | `tools/pgm2png.py`, `poc/view.html`, fbdaemon `panel.html` / `kfb-host` | — |

`make tetris-host` + `printf 'tap 100 700\n' | ./tetris-host --no-takeover --dump f.pgm`
already produces the exact glass frame. Verified: `emu-probe.png`.

## What an emulator would be

A small server that (1) spawns the host game with stdin as a pipe, (2) writes `tap X Y` lines
to it, (3) serves the newest frame to a browser page as a PNG. The panel is already faked, so
nothing device-related needs writing.

## Cost, by tier

- **Tier 1 — click-to-play, one frame per tap. ~½ day.** Needs a frame-write hook at flush
  time and the tiny web front end. Fine for all turn-based work: menu, chess, sudoku, mines,
  pipes, pvz, and layout/art checks.
- **Tier 2 — live clock and live taps (Tetris falling, Rockman in CLOCK mode). ~+1 day.**
  The real main loop has to run on the host: `ek_poll()` must read stdin, and host mode must
  fake the panel's ~300 ms repaint so the beat the game sees matches the glass.
- **Tier 3 — faithful panel behaviour (ghosting, DU trails, GC16 clears, the line a
  full-mode update leaves). Not achievable locally at any price.** Those are EPDC hardware
  behaviours; the README's standing caveat ("host mode stubs the panel ioctls, so the
  on-glass result still needs one look on a device") stays true.

## The one lever worth knowing

The clean shape is a **guarded hook inside shared `engine/eink.c`**: in the `if (host_mode)`
branch of `ek_flush()` write the frame (and let host `ek_poll()` read stdin). That is shared
constituent code — one option, no behaviour change for the device or any other consumer — so
it needs the user's go-ahead first. The cost of avoiding it is a 1–2 line call site in each of
the 11 `src/*.c` files instead.
