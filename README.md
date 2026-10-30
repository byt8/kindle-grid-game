# XCOM on a Kindle

Four soldiers, aliens in the fog, one small e-ink screen. Tap to walk. Tap an alien to shoot. Wipe them out.

## Quick Start

**1. Install and start it** (Kindle is already jailbroken with SSH, on your network):

```
./run-on-kindle.sh                 # the Kindle at 192.168.0.63
./run-on-kindle.sh 192.168.0.64    # a different Kindle
```

It copies the game over, frees the touch screen from the screen mirror, runs the game, and puts the Kindle back to normal when you quit.

Needs no internet — only this PC and the Kindle on the same Wi-Fi/LAN. Nothing is downloaded and the game makes no network calls.

**2. Play:**

1. Tap a soldier along the bottom.
2. Tap a boxed square to walk. A thick box is a long run — after a long run you cannot shoot.
3. Tap an alien, then tap FIRE. The button shows your chance to hit.
4. END: aliens move. WATCH: shoot the next alien who runs into view. HUNKER: harder to hit. HEAL: the medic only, twice, on a hurt friend next to them.
5. Tap QUIT, then SURE. The books screen comes back.

Pale squares are grass. Black trees and rocks block the way. Standing in trees makes your shots miss more. Water is closed. Dark fog is what you have not seen yet.

Win = every alien down. Lose = every soldier down.

**3. Build it again (optional):**

```
make            # PC program, xcom-host
make test       # plays a pretend game and checks the pictures
make kindle     # Kindle program, needs the kfb-build docker image
```

## How it fits together

| File | What it is |
|---|---|
| `src/xcom.c` | the whole game: rules, drawing, touch, PC self-test |
| `src/sprites.h` | 40x40 dithered tiles, generated — do not hand-edit |
| `tools/slice_sprites.py` | cuts `spritesheet.png` into those tiles |
| `tools/kindle-run.sh` | on-device launcher: frees the screen, runs, restores |
| `tools/grabtest.c` | "is the touch device free right now?" probe |

The screen is 600x800, 8 bits per pixel, and the engine draws pure black/white only — shading is done with dither patterns. Painting uses the e-ink engine in `jailbreak-kindle/kindle-tools/fbdaemon/engine`; that folder is shared and never modified from here.

## When it goes wrong

`run-on-kindle.sh` pushes `grabtest` first. If it prints `EVIOCGRAB refused: Resource busy`, something still holds `/dev/input/event3`. The launcher handles the two usual culprits (a leftover game from a killed session, and the `kfb` screen mirror); if it still fails, on the Kindle:

```
initctl stop kfbterm
kill -9 $(pidof kfb)
```

The screen goes blank-ish and touch belongs to you. `sh /mnt/us/fbdaemon/restore-kindle.sh` gives the Kindle back.

A leftover game is the other trap: killing the ssh on this PC can leave `xcom` running forever with the screen. The launcher kills it before starting.

## On this computer

```
make
./xcom-host --seed 1 --dump /tmp/xcom.pgm            # draw the first screen
./xcom-host --selftest                               # check the rules
./xcom-host --auto 2                                 # let the computer play 2 turns
printf 'tap 120 536\n' | ./xcom-host --seed 1 --dump /tmp/picked.pgm
```

Same `--seed`, same dice, so a bug is repeatable.
