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

### Boot and device pictures

Every full-screen picture the Kindle shows is a 600x800 8-bit greyscale PNG
(colour type 0, no alpha) on the read-only root disk
(`/usr/sbin/mntroot rw` unlocks it). Five art screens are built from the
workspace's AI art:

```
python3 tools/make_boot_screens.py   # rebuilds all five into screens/
```

The AI art is dense ink (77–96% black), which would swallow the code-drawn
progress bar / labels, so each piece is inverted to a white panel with dark
etching (`tools/make_boot_screens.py` does this). The mapping:

| screen file | device paths it replaces |
|---|---|
| `screens/boot.png` | `/usr/share/blanket/bg_xsmall_defaultbg.png` **and** `/opt/amazon/low_level_screens/600x800/splash.png` (both must match; the `/var/local/blanket` link only points at the latter) |
| `screens/restart-low.png` | `/opt/amazon/low_level_screens/600x800/reboot.png` **and** `/var/local/blanket/low_level_screens/images/reboot.png` (the low-level renderer reads the `/var/local` copy — a real file, not a symlink) — the second restart screen ("rebooting") |
| `screens/restart-shutdown.png` | `/usr/share/blanket/shutdown/bg_xsmall_reboot.png` — the first restart screen (Settings → Restart dialog) |
| `screens/repair-update.png` | `/opt/amazon/low_level_screens/600x800/repair.png` (repair mode) — also a candidate for the `update_*.png` trio |
| `screens/misc.png` | any leftover: `critbatt`, `shipping_mode`, `cust_service`, langpicker, usb, ota |

The **screensaver slideshow** (sleep) is the same idea: 20 pictures on the
read-only root at `/usr/share/blanket/screensaver/bg_xsmall_ss00..19.png`,
each a 600x800 greyscale PNG. Rebuild the whole set from the five newest art
pieces (four zoom/crop variants per piece so the slideshow varies):

```
python3 tools/make_screensaver_screens.py   # rebuilds screens/ss/ss00..ss19.png
mntroot rw
cp screens/ss/ss*.png /usr/share/blanket/screensaver/bg_xsmall_ssNN.png   # per-slot, on the kindle
mntroot ro
```

The stock `ss` pictures were overwritten in place (busybox `cp` has no `-n`,
so the backup copy silently failed); restore them from a firmware image if you
ever need the originals back. See `ss-preview.png` for a contact sheet of all 20.

Install any of them the same way (example: boot pair):

```
mntroot rw
cp screens/boot.png /usr/share/blanket/bg_xsmall_defaultbg.png
cp screens/boot.png /opt/amazon/low_level_screens/600x800/splash.png
mntroot ro
```

All five are now installed on the device — the Settings → Restart flow shows our
`restart-shutdown.png` (dialog) then `restart-low.png` (rebooting) before the
boot art. Every `bg_xsmall_defaultbg*` copy on the device — root, `splash/`,
`langpicker/`, and `ota/` — now carries our art (the `ota` one was the last
stock holdout; a blank white 600x800, backed up to
`/mnt/us/boot-splash-backup/ota/`). Their stock files are backed up at
`/mnt/us/boot-splash-backup/shutdown/` and `/mnt/us/boot-splash-backup/low-level/`.

Reboot to see it. The bar that fills along the bottom of the boot screen is
drawn by code, so a new picture still gets the bar. Verified against the
device's own boot captures (11:59 / 12:09): the only frames are black → our
boot art with the progress bar → home; no stock tree frame remains. Stock
pictures are backed up at `/mnt/us/boot-splash-backup/` — copy them back over
the same paths to revert. A firmware update also puts the tree back.

To skip the picture, take `splash` out of the two lines in `/etc/upstart/x.conf`.
Not tried yet. Do not `killall blanket` — the Kindle reboots into repair mode.
The game already stops the whole `x` job once it owns the screen, and paints one
white frame so the tree does not stay on the glass.

### Language strings (the "Kindle starts up" text)

The boot/restart screens' text comes from gettext catalogs on the read-only
root. The restart dialog's "Please wait a moment while your Kindle starts up."
is `shutdown.reboot.message` in `/usr/share/locale/en/LC_MESSAGES/blanket.mo`
(and `en_GB`). `tools/patch_blanket_mo.py` swaps one msgstr while keeping every
other byte of the file intact — a needed gift, because boot-time tools cannot
read NUL-free text if the pools grow:

```
python3 tools/patch_blanket_mo.py blanket.mo --msgid shutdown.reboot.message --to "Please wait a moment while your device starts up."
```

The patched `en` catalog is installed on the Kindle (backed up to
`/mnt/us/boot-splash-backup/locale/`); `en_GB` was left stock, and the change
shows at the next reboot.

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
