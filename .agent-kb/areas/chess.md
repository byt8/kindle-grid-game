# chess

Status: **works on the PC and on the Kindle; the AI needs a real key** — the
`[[secret:openai_key]]` placeholder never expanded, so every API test so far
used either the local proxy (PC only) or `--fake-ai`.

## What it is

`src/chess.c` — one file, you are White on a 600x800 touchscreen, DeepSeek is
Black and writes one line of trash talk under the board. Same shape as
`src/xcom.c`: single-file game + shared e-ink engine (`EK_HOST` for the PC
build), tap-to-pick/tap-to-move, QUIT+SURE to give the screen back.

## Commands that matter

```
make chess-host            # PC build, -Werror clean
./chess-host --selftest --no-takeover      # rules + perft, must print "selftest clean"
./chess-host --fake-ai --auto 8 --no-takeover --dump /tmp/c.pgm
./chess-host --apicheck --conf chess.conf  # plays e2e4, then asks the API once
make chess-kindle          # static ARM musl, docker kfb-build, 83 KB
./run-chess-on-kindle.sh [IP] [--args]     # push + take screen + run + restore
```

## Facts learned the hard way

- **Setup bugs look like engine bugs.** Perft depth 1 returning 34 instead of 20
  was the initial position written with the pawn ranks *and* the back ranks
  reversed. Kiwipete perft passed the whole time — always also run perft from
  the real opening position.
- **`san_of` must not append the destination for a castle.** It produced
  `O-Og1`; the `--selftest` "e1g1 is O-O" check caught it.
- **A reasoning model can answer with empty `content`.** deepseek-flash emits
  `reasoning_content` first; with a long prompt it can spend the budget and
  return `content: ""`. There is one automatic retry (`g_transient`) and the raw
  body is printed on failure (`reveal_reply`).
- **`--dump` has to run before the batch early-return** on the device, otherwise
  `--fake-ai --dump` produces no file. The `#else` branch now falls through to
  the dump instead of returning.
- **Device config path is `/mnt/us/chess.conf`**, not `/mnt/us/chess/...` — the
  binary itself is `/mnt/us/chess`.
- **`tools/kindle-run.sh` is game-generic**: `GAME=chess sh /mnt/us/chess-run.sh`
  runs `/mnt/us/chess` and skips the xcom sound setup. `run-chess-on-kindle.sh`
  pushes chess, grabtest, the wrapper and `chess.conf`.
- **White pieces on white squares are nearly invisible** with a 1-pixel rim: the
  rim is now two rings (12 offsets), so a white pawn on a light square still
  reads (ink per square went 385 -> ~500).
- **Kindle API reachability**: `https://api.deepseek.com` answers **401** from
  the device with both `-k` and a copied fresh CA bundle, so TLS is fine; only a
  real key is missing. `scp` does not work (no sftp-server) — push with
  `cat file | ssh ... 'cat > /path'`.
- **PC proxy** `http://127.0.0.1:4020/v1/chat/completions` works from the PC but
  is not reachable from the Kindle (WSL2 localhost), so it is only useful for
  `--apicheck` on this machine.

## Screen layout

600x800: top bar 0-64, board 28,64 with 68 px squares, comment lines from 624,
QUIT/button row at 744. Shapes are drawn in a 0..100 box and scaled; squares are
dithered, not grey.

## Next

- Get a DeepSeek key into `chess.conf`, then `./run-chess-on-kindle.sh` and play.
- The promotion picker exists (`draw_promo`/`do_promo`, taps at 1311) but has no
  automated test; the tap test in `make chess-test` only covers select+move.
