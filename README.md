# Four games on a Kindle

Four soldiers, aliens in the fog, one small e-ink screen. Tap to walk, tap an alien to shoot, wipe them out.
Chess against DeepSeek, which talks trash after every move. Tetris: tap to stack blocks and clear lines.
And Sudoku: a generated puzzle with exactly one solution, three difficulties, hints, tap-to-play.
**Rockman** (in progress): the 1984 VIC-20 cave digger — rules in `docs/ROCKMAN_SPEC.md`, all data + pixel art machine-extracted into `src/rockman_data.h`, build plan in `docs/ROCKMAN_PLAN.md`; the port itself is not written yet.

---

## Quick Start

**What you need:** a jailbroken Kindle Touch with SSH on your network, this PC,
Docker image `kfb-build` (ARM cross-compiler), and `python3`.

```bash
make kindle                      # XCOM  -> ./xcom      (ARM binary)
make chess-kindle                # Chess -> ./chess     (needs chess.conf first)
make tetris-kindle               # Tetris -> ./tetris   (no key needed)
make sudoku-kindle               # Sudoku -> ./sudoku  (no key needed)
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

**Play Sudoku on the Kindle:**

```bash
./run-sudoku-on-kindle.sh 192.168.0.66   # pass the IP
```

Tap a cell, then a digit **1–9** to write it. **ERASE** clears the cell, **HINT** writes the answer.
The three chips up top (**EASY / MED / HARD**) start a fresh puzzle; **NEW** restarts, **QUIT** then **SURE** exits.
A wrong digit gets a box, and a completed, conflict-free board is the win.
Pass `--seed N` to the runner to play the same puzzle again.

**Boot straight into the launcher menu** (instead of the Amazon UI):

```bash
./install-menu-on-kindle.sh [IP]
```

Builds the menu + all four games, pushes them, installs `/etc/upstart/menu.conf`
(replacing the old direct-to-chess boot job), and shows the menu now. Six
buttons: **XCOM / CHESS / TETRIS / SUDOKU / WIFI / KINDLE**. Play a game; when you quit
you land back on the menu. **WIFI** reuses chess's wi-fi screens (join a
network, it remembers it). **KINDLE** hands the screen back to the Amazon
reader. Undo (stock boot again):

```
ssh root@KINDLE_IP '/usr/sbin/mntroot rw; rm /etc/upstart/menu.conf; /usr/sbin/mntroot ro'
```

**No Kindle, no key — try it on this PC:**

```bash
make                                    # builds ./xcom-host
./xcom-host --seed 1 --dump /tmp/x.pgm  # draw the opening screen
make preview                            # every 40x40 sprite, side by side -> bold-preview.png
make test                               # plays a pretend game, checks pictures + clips
make tetris-host && make tetris-test    # self-test + three screens the checker reads
make sudoku-host && make sudoku-test    # self-test + three screens the checker reads
make menu-host && make menu-test        # menu: draw + tap every button on the PC
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

To redo the art, drop four sheets into `bold/` — 13 cells total, no code changes:

| file | grid | cells, left-to-right top-to-bottom |
|---|---|---|
| `bold/players.png` | 2x2 | soldier, sniper, heavy, medic |
| `bold/enemies.png` | 2x2 | sectoid, sniper, mutant, assault |
| `bold/objects.png` | 2x2 | rocks, tree, crates, bush |
| `bold/drone.png` | 1x1 | drone |

Terrain (**grass / forest / city / road / water / rubble / mountains**) is **not** yours to
supply — those seven come from `PATTERNS` in the baker.

Rules the baker enforces, so the art has to be drawn to them:

- **512x512 per cell** (1024x1024 sheets). Below ~200 px the hard threshold eats every
  interior line. Any size divides as long as it splits evenly into the grid.
- **Black line art on white.** Grey >= 120 becomes paper, so shading is invisible. The file
  is sniffed by content, not extension — WebP named `.png` is fine.
- `players.png` bakes **hollow**: your outline is kept, the inside turns white. Give every
  figure a heavy closed outline or the bake dies with *"hollow outline disappeared at 40 px"*.
- `enemies` / `objects` / `drone` bake **solid**: the silhouette becomes black. Interior
  detail is discarded, so the outline alone has to be recognisable.
- A cell with no ink, or under 15% ink once fitted to 40x40, **aborts the bake** rather than
  shipping a blank tile. Nothing is substituted.

### Why the art looks fine but the game does not

The baker runs seven steps on the way down to 40x40, and only the last one is
what the panel shows. See all of them at once:

```bash
python3 tools/sprite_stages.py                 # -> sprite-stages.png
python3 tools/sprite_stages.py --only soldier  # one row
```

One row per sprite: source art, the box-resize into <=36x34, the hard
threshold that survives it, the dilation and (for friendlies) the hollow
erosion, the stored 40x40 tile, then a mock of that tile on grass and on
forest with the paper rim, team badge and HP bar the game draws over it. The
40x40 beside each name is true panel size; every other column is magnified
4x with nearest-neighbour, no smoothing.

Measured on the current art: friendlies lose about 40% of their ink to the
hollow erosion (~400 px down to ~240) and end up as a thin outline; solid
enemies *gain* ink (144-289 px up to 361-687) because dilation fuses their
interior into one black blob, 73-95% of the tile. Those two effects are the
whole difference between "looks fine" and "looks terrible".

Then `make preview && make test`. Unit stats (HP, aim, damage) come from
`tactical_sprite_map_with_descriptions.json`, not the art, so new pictures change no rules.
Different filenames or grids mean editing `bold_sprite_sources.json` too.

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

## Sudoku: how it fits together

One file, `src/sudoku.c`: the generator, the solver proof, drawing, touch and a PC self-test.
Every puzzle is **generated on the device** (seeded) and is proven to have exactly one
solution before it is shown: `solve_count() == 1` decides which cells may stay blank.
`make sudoku-test` checks rows/columns/boxes, unique solvability, error marking, hints and
wins on the PC, then draws three screens (open, one typed digit, a few auto hints) for the
picture checker. The menu's SUDOKU button and the boot dispatcher (`tools/menu-boot.sh`
case 4) launch the same runner as the other games.

---

## Tetris: how it fits together

One file, `src/tetris.c`: rules, drawing, the auto-player and a PC self-test. A falling
piece shows a ghost where it will land. `make tetris-test` runs the self-test plus three
screens (open, one tap, 24 auto pieces) and checks the well frame is drawn and the blocks
really stack.

---

## Rockman: status

Rules and data are fully extracted; the port is not written yet. Read the two docs in
this order before touching `src/rockman.c`:

1. `docs/ROCKMAN_SPEC.md` — rules + every data table, verbatim from the disassembly.
2. `docs/ROCKMAN_PLAN.md` — the reviewed build plan: spec corrections (map bit 1 =
   **rock**, not wall; `CELL_*` = screen offsets), the 27x40 cell layout for 600x800,
   tick timings (16 ms pass, 14/16/9 action ticks, 9 x 120 x 6 timer passes),
   function-by-function build order, tests, and the handful of asm spots still to
   verify line by line.

The data tables and 8x16 pixel art are regenerated straight from the asm, not hand-typed:

```bash
make rockman-data   # tools/extract_rockman.py --check -> rewrites src/rockman_data.h
python3 scratch/show_tiles.py   # every tile as ASCII: check 1-bit legibility
```

`src/rockman_data.h` is generated — any room/tile change goes in `scratch/Rockman.asm`
first, then `make rockman-data`. Once the port exists it joins the others:
`make rockman-host && make rockman-test` on the PC, `make rockman-kindle` +
`./run-rockman-on-kindle.sh <ip>` on the device.

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
| `tools/chess-boot.conf` | (superseded by `menu-boot.conf`) the old direct-to-chess boot job |
| `tools/menu-boot.conf` | upstart job that boots the Kindle into the launcher menu |
| `tools/menu-boot.sh` | the menu session: stop mirror, run menu, launch game, loop back |
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
`/tmp/chess-shot.pgm`). `--wifi-only` opens the wi-fi screens and exits without
a board (what the menu's WIFI button uses).

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

### The launcher menu boot job

`tools/menu-boot.conf` (installed as `/etc/upstart/menu.conf`) is what makes the
Kindle boot into the menu. `tools/menu-boot.sh` is the always-on session: it
stops the screen mirror so the menu's touch grab wins, waits briefly for
wi-fi, runs the menu, launches the chosen game, and loops back when the game
exits. The menu itself is one file, `src/menu.c` (same e-ink engine as the
games). Progress is in `/mnt/us/menu-boot.log`.

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
each a 600x800 greyscale PNG. Unlike the boot screens, these are **not**
inverted — the dark art is shown as-is (nothing is painted over a
screensaver). Rebuild the whole set from the five newest art pieces (four
zoom/crop variants per piece so the slideshow varies):

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

Reboot to see the PNG swap. The bar along the bottom is drawn by code, so a
new picture still gets the bar. Stock PNG backups are at
`/mnt/us/boot-splash-backup/`. A firmware update puts those trees back.

The screen **before** that one is not one of these files. The kernel paints it
from a picture baked into the kernel, before Linux opens any PNG. Both kernel
copies on the raw disk carry it (main at `0x41000`, second at `0xE41000`);
gzip members at `0xC92C0` (plain logo, 2672 B) and `0xC9D30` (logo with
"Please wait a moment while your Kindle starts up." text, 4664 B), duplicated
at `0xEC7104` / `0xEC7B74`. Payload is a 12-byte LE header (`600,800,2`) plus
2-bit pixels at 4 px/byte (0=black, 3=white). Dense art never fits: even a
1-bit full screen is ~10 KB. The working render is the boot art
GaussianBlur(15), resized to 320x427, centred on pure black (the art's own
corners are black, so no border) — that gzips to 2470 bytes and fits the
plain slot, so one blob fits all four. The patch builds in
`scratch/patch_kernel_build.py` (stock blobs + old CRCs saved to
`scratch/kernel-logo-rollback.json`; output `scratch/mmcblk0-first32m-patched.bin`),
and `scratch/flash-kernel-logo.sh KINDLE_IP` verifies the device is still
stock, writes only the changed bytes, re-verifies, and does not auto-reboot.
The uImage data CRC (`dcrc` at +24) and header CRC (`hcrc` at +4) both have to
be recomputed, which the builder does.

To skip the picture, take `splash` out of the two lines in `/etc/upstart/x.conf`.
Not tried yet. Do not `killall blanket` — the Kindle reboots into repair mode.
Games never stop the `x` job: stopping X makes restore-kindle.sh restart it on
exit, which makes blanket redraw the boot splash (the flicker after QUIT). They
take the screen with the engine's `EK_TAKEOVER` (framework + pillow only), and
the shared runner (`tools/kindle-run.sh`) stops powerd before each game so the
power key cannot raise the screen saver over the board — same clean start/stop
as XCOM for every game.

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
shows on the restart dialog. It does not change the first screen. Those same
words are painted into the kernel picture, not read from this catalog.

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
