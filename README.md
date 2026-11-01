# Three games on a Kindle

Four soldiers, aliens in the fog, one small e-ink screen. Tap to walk, tap an alien to shoot, wipe them out.
Chess against DeepSeek, which talks trash after every move. And Tetris: tap to stack blocks and clear lines.

---

## Quick Start

**What you need:** a jailbroken Kindle Touch with SSH on your network, this PC,
Docker image `kfb-build` (ARM cross-compiler), and `python3`.

```bash
make kindle                      # XCOM  -> ./xcom      (ARM binary)
make chess-kindle                # Chess -> ./chess     (needs chess.conf first)
make tetris-kindle               # Tetris -> ./tetris   (no key needed)
```

```bash
cp chess.conf.example chess.conf # chess only: paste your key, key = sk-...
```

**Play XCOM on the Kindle:**

```bash
./run-on-kindle.sh               # Kindle at 192.168.0.63
./run-on-kindle.sh 192.168.0.66  # second Kindle
```

It copies the game and its voice clips over, frees the touch screen, runs it, and
puts the Kindle back to normal when you quit. **Tap a soldier**, tap a boxed square
to walk, tap an alien then **FIRE**. **QUIT** then **SURE** exits.

**Play chess on the Kindle:**

```bash
./run-chess-on-kindle.sh 192.168.0.66   # always pass the IP
```

Tap a piece, tap a square. **QUIT** then **SURE** gives the screen back.

**Play Tetris on the Kindle:**

```bash
./run-tetris-on-kindle.sh 192.168.0.66   # pass the IP
```

Tap **LEFT / TURN / RIGHT** to steer, **DROP** to slam it down, **QUIT** then **SURE** to exit.

**No Kindle, no key — try it on this PC:**

```bash
make                                    # builds ./xcom-host
./xcom-host --seed 1 --dump /tmp/x.pgm  # draw the opening screen
make preview                            # every 40x40 sprite, side by side -> bold-preview.png
make test                               # plays a pretend game, checks pictures + clips
make tetris-host && make tetris-test    # self-test + three screens the checker reads
```

Both host programs take `tap X Y` lines on stdin and `--dump FILE` writes the
screen, so a bug is replayable: same `--seed`, same dice.

---

## XCOM: how it fits together

| File | What it is |
|---|---|
| `src/xcom.c` | the whole game: rules, drawing, touch, PC self-test |
| `src/audio.c` | the speaker: one child process owns `aplay`, the game never waits on it |
| `src/sprites.h` | 20 baked 40x40 tiles, generated — do not hand-edit |
| `tools/bake_bold_sprites.py` | bakes the tiles from `bold/` art plus the terrain patterns |
| `bold_sprite_sources.json` | which generated cell becomes which sprite |
| `bold/` | the generated art sheets (players, enemies, objects, drone) |
| `tools/make_sound.py` | bakes one `.pcm` per spoken word, plus the effects |
| `tools/check_sound.py` | reads the source and fails the build if a word has no clip |
| `sound/` | the baked clips, pushed to `/mnt/us/xcom-sound` |
| `tools/grabtest.c` | "is the touch device free right now?" probe |
| `MANUAL.md` | plain-prose game manual for TTS |

### The sprites are drawn bold on purpose

The panel is 1 bit: black or white, no grey. A painted picture dithered to 40x40
turns into speckle, and a soldier stops looking different from a crate. So:

- **Units and objects** are bold line art generated at 512x512, hard-thresholded
  (no dither), trimmed to the silhouette, then ink-dilated one pixel so no stroke
  is thinner than 2 px.
- **Friendlies are hollow** (white body, black outline). **Aliens are solid black.**
  At 40 px there is no room for interior detail, so team is read from black vs white.
- **Terrain is a pattern, not a picture**: dots = grass, trunks = forest, waves =
  water, dashes = road, windows = city, pluses = rubble, peaks = mountains.

To redo the art: generate new sheets into `bold/`, point
`bold_sprite_sources.json` at them, then `make preview && make test`.

The Kindle keeps the last binary you pushed, so new art needs **both** steps:

```bash
make kindle && ./run-on-kindle.sh 192.168.0.66
```

Check what is really on a rendered screen (a sprite in the header is not proof it
was drawn): `python3 tools/find_tile.py /tmp/xcom-open.pgm terrain_tiles/grass`,
`tools/dump_sprite.py player_units/soldier`, `tools/dump_region.py FILE X Y`.

The baker needs Pillow (`python3 -m pip install pillow`). That is a build-time
tool on this PC only; nothing extra ships to the Kindle.

`tools/slice_sprites.py` and `spritesheet.png` are the old dithered set, kept for
comparison only (`make preview-old`).

Sound is spliced from files, not generated on the device: `audio.c` turns a log
line into lower-case words, a number into "for four", and streams the matching
clips into `aplay`. A missing clip is reported by name, never swapped for
something else. No internet needed at runtime.

Painting uses the e-ink engine in
`jailbreak-kindle/kindle-tools/fbdaemon/engine`; that folder is shared and never
modified from here.

---

## Tetris: how it fits together

One file, `src/tetris.c`: rules, drawing, the auto-player and a PC self-test. A falling
piece shows a ghost where it will land. `make tetris-test` runs the self-test plus three
screens (open, one tap, 24 auto pieces) and checks the well frame is drawn and the blocks
really stack.

---

## Chess: how it fits together

Rules, board and the model call are all in `src/chess.c`. The Kindle talks to
`https://api.deepseek.com/v1/chat/completions` itself — nothing goes through this
PC. The push also drops this PC's CA bundle at `/mnt/us/chess-ca.pem`, because the
Kindle's own trust store is older than DeepSeek's certificate.

| File | What it is |
|---|---|
| `src/chess.c` | the whole game: rules, drawing, touch, wi-fi screen |
| `tools/chess-wifi.sh` | scan / join / remember, over `wpa_cli` |
| `tools/chess-boot.conf` | upstart job that boots into the game |
| `tools/make_splash.py` | turns a PNG into the Kindle's 600x800 greyscale boot picture |
| `tools/kindle-run.sh` | on-device launcher: frees the screen, runs, restores |
| `chess.conf.example` | key, model, url, ca |

```bash
make chess-host
./chess-host --selftest --no-takeover             # rules, notation, perft
./chess-host --fake-ai --auto 8 --dump /tmp/c.pgm # both sides play, draw the board
./chess-host --apicheck --conf chess.conf         # ask the API one question
```

Check the screen of a game you cannot see (`kill -USR1 <pid of chess>` writes
`/tmp/chess-shot.pgm`).

### Boot straight into the game

On the Kindle:

```
mntroot rw
cp /mnt/us/chess-boot.conf /etc/upstart/chess.conf
mntroot ro
```

Undo: `mntroot rw; rm /etc/upstart/chess.conf; mntroot ro`. It waits up to 3
minutes for Wi-Fi and starts anyway if it never comes up, because the game draws
its own Wi-Fi screen. One game per boot (`/tmp/chess.started`); after QUIT, start
it again over ssh or reboot. Progress is in `/mnt/us/chess-boot.log`.

### Wi-fi without the Kindle UI

The game draws its own screen: scan, tap a network, type the password on a drawn
keyboard, result saved to `/mnt/us/wifi.conf` so the next boot joins by itself.

```
sh /mnt/us/chess-wifi.sh status          # state / ssid / ip, exit 0 when online
sh /mnt/us/chess-wifi.sh join SSID PSK   # joins and remembers
sh /mnt/us/chess-wifi.sh auto            # rejoin what was remembered
```

### Boot picture

Checked on `192.168.0.63` (Kindle 5.3.7.3). The boy-under-the-tree is still the
2015 file. The root disk is read-only (`/usr/sbin/mntroot rw` unlocks it).

The splash program opens `/usr/share/blanket/bg_xsmall_defaultbg.png`. A second
copy of the same picture is `/opt/amazon/low_level_screens/600x800/splash.png`
(the link under `/var/local/blanket` only points there). Replace both. The file
must be 600x800, 8-bit, no alpha. The bar that fills along the bottom is drawn
by code, so a new picture still gets the bar.

```
python3 tools/make_splash.py --logo LOGO.png --out kindle-splash.png
```

Then on the Kindle: `mntroot rw`, copy that png over both paths above,
`mntroot ro`, reboot. A firmware update puts the tree back.

To skip the picture, take `splash` out of the two lines in `/etc/upstart/x.conf`.
Not tried yet. Do not `killall blanket` — the Kindle reboots into repair mode.
The game already stops the whole `x` job once it owns the screen, and paints one
white frame so the tree does not stay on the glass.

---

## When it goes wrong

**The Kindle still shows the old sprites** — the device is running the binary from
the last push. `make kindle`, then push again; rebuilding on this PC alone changes
nothing on the panel.

**`EVIOCGRAB refused: Resource busy`** — something still holds
`/dev/input/event3`. The launcher handles the two usual culprits; if not, on the
Kindle: `initctl stop kfbterm; kill -9 $(pidof kfb)`.

**Screen draws but taps do nothing** — the screen mirror grabbed touch. A reboot
does not fix it (the mirror starts with the reader UI). `initctl stop kfbterm`.

**Dead game and dead reader** (panel holding the last picture forever):

```
ssh root@KINDLE_IP 'pidof chess; /sbin/initctl status framework; pidof cvm'
ssh root@KINDLE_IP 'sh /mnt/us/fbdaemon/restore-kindle.sh'
```

Run the restore twice if `cvm` still does not appear. Kill the game first
(`kill $(pidof chess)`) or it keeps the panel.

**No sound** — the launcher says why before it starts. By hand:

```
amixer sget Speaker          # must read Playback [on]
```

A `sound: no clip words/x.pcm` line means the game said a word with no file: run
`make sound`, then push again. `make test` catches that before the device does.

---

## On this computer

```bash
./xcom-host --selftest                           # check the rules
./xcom-host --auto 2 --no-takeover               # let the computer play 2 turns
printf 'tap 120 536\n' | ./xcom-host --seed 1 --dump /tmp/picked.pgm
./xcom-host --soundcheck --auto 1                 # print the words it would say
```

No speaker on the PC: sound is off unless you ask for `--soundcheck`, which only
prints the words.
