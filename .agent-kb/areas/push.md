# Pushing to a Kindle (the deploy loop that actually works)

## Quick recipe
```bash
./run-chess-on-kindle.sh 192.168.0.63      # rebuilds, pushes, takes the panel, detaches
```
- It builds first (`make chess-kindle`), so a stale `./chess` is not a trap here.
- `ping` **fails** on these Kindles even when they are up — do not use ping as the
  readiness check. Test with `ssh` directly.
- Key is `~/.ssh/kindle_touch_ed25519` (there is no `kb_chess_test_ed25519`).

## Verify it actually landed (never trust "running")
```bash
ssh -i ~/.ssh/kindle_touch_ed25519 -o BatchMode=yes -o IdentitiesOnly=yes \
    -o StrictHostKeyChecking=no root@<IP> \
    'md5sum /mnt/us/chess /mnt/us/chess-wifi.sh; for p in $(pidof chess); do tr "\0" " " < /proc/$p/cmdline; done'
```
Compare the md5s against `md5sum chess tools/chess-wifi.sh` on the host. A push
whose md5s match but with no `chess` pid means the app died — read
`/mnt/us/chess-run.log` and `/mnt/us/chess.log`.

## Traps seen
- [verified 2026-11-03 .63] `chess-run.log` ends with
  `can't open /proc/<n>/cmdline: no such file` right after taking the panel. That is
  **normal**: it is `pidof`-then-`kill` racing a dead pid while clearing a leftover
  `xcom`. Harmless — the game still came up.
- [verified 2026-11-03 .63] `initctl: Unknown instance:` twice during start is also
  normal (the `menu` job was not running).
- The runner detaches and then **only follows the log**; ending it does not stop the
  game. Kill the game with `kill <pid>` on the device.
- On the cptr dashboard the runner is service `chess-on-kindle-63`
  (`./run-chess-on-kindle.sh 192.168.0.63`). `run_command` cannot detach, so start
  the push through `start_service`, not `run_command`.

## Seeing the screen without the glass in front of you
```bash
ssh ... 'kill -USR1 <chess pid>'            # writes /tmp/chess-shot.pgm on the device
ssh ... 'cat /tmp/chess-shot.pgm' > /tmp/chess-shot.pgm
python3 -c "from PIL import Image; Image.open('/tmp/chess-shot.pgm').save('shot.png')"
```
600x800, `L` mode. **15 distinct levels = healthy grey.** Only 0 and 255 means the
`ek_mono(1)` flat-mono failure is back.
