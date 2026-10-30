# XCOM on a Kindle

## Quick Start

Four soldiers. Aliens in the fog. Tap to walk. Tap an alien to shoot. Wipe them out.

The Kindle is already jailbroken and has SSH. The picture is 600 by 800.

### Install and start

```
./run-on-kindle.sh
```

That copies the game over and starts it. The books screen stays away until you leave.

### Play

1. Tap a soldier along the bottom.
2. Tap a boxed square to walk. A thick box is a long run. After a long run you cannot shoot.
3. Tap an alien. The button shows the chance to hit. Tap FIRE.
4. END: the aliens move. WATCH: shoot the next alien who runs into view. HUNKER: harder to hit. HEAL: the medic only, twice, on a hurt friend next to them.
5. Tap QUIT, then SURE. The books screen comes back.

Pale squares are grass. Black trees and rocks block the way. Standing in trees makes shots miss more. Water is closed. The dark fog is what you have not seen yet.

You win when every alien is down. You lose when every soldier is down.

### Build

`xcom` is the Kindle program, already built. Check the rules on this PC, or build the Kindle program again:

```
make            # PC program, xcom-host
make test       # plays a pretend game and checks the pictures
make kindle     # needs the kfb-build docker image
```

Drawing uses the e-ink engine in `jailbreak-kindle/kindle-tools/fbdaemon/engine`. This game does not change that folder.

### On this computer

```
make
./xcom-host --seed 1 --dump /tmp/xcom.pgm
printf 'tap 120 536\n' | ./xcom-host --seed 1 --dump /tmp/picked.pgm
```

`--selftest` checks the rules. `--auto 2` lets the computer play two turns. Same `--seed`, same dice.
