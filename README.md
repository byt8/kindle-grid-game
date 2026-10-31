# Two games on a Kindle

Four soldiers, aliens in the fog, one small e-ink screen. Tap to walk. Tap an alien to shoot. Wipe them out.
And chess against DeepSeek, which talks trash after every move.

## Quick Start

Everything runs on the Kindle; this PC only builds and copies. Kindle must be
jailbroken, with SSH, on the same network (`192.168.0.66` by default).

```
cp chess.conf.example chess.conf      # paste your DeepSeek key: key = sk-...
make chess-kindle                     # the ARM binary (needs the kfb-build docker image)
./run-chess-on-kindle.sh 192.168.0.66 # copy it over, take the screen, play
```

Pass the Kindle's address: with more than one Kindle on the network the default
comes from `jailbreak-kindle/.kfb-secrets` and may be the other one.

**Play:** tap a piece, tap a square. Black's move and its comment appear under
the board. Tap QUIT, then SURE, to give the screen back to the reader.

**Power button:** puts the Kindle to sleep (screen says ASLEEP) and wakes it
again with the position kept. Pressing power during the model's turn cancels
that question and it is asked again after the wake. The key is *not* an input
device on this Kindle — no `/dev/input/eventN` ever sees it (measured with
`tools/keyprobe.c`) — so the game watches the `PMIC_IRQ` counter in
`/proc/interrupts` and sleeps the machine itself, which is why the stock
`powerd` stays stopped while the game runs.

**No Kindle, no key, on this PC:**
```
make chess-host
./chess-host --selftest --no-takeover             # rules, notation, perft
./chess-host --fake-ai --auto 8 --dump /tmp/c.pgm # both sides play, draw the board
./chess-host --apicheck --conf chess.conf         # ask the API one question
./chess-host --wifi-shot /tmp/w.pgm pass          # draw a wi-fi screen (list|name|pass|fail)
```

**Check the screen of a game you cannot see** (it usually runs with the reader UI
switched off): `kill -USR1 <pid of chess>` writes `/tmp/chess-shot.pgm`.

**Kindle screen draws but ignores taps?** The screen mirror (`kfbterm`) has
grabbed the touch device. A reboot does not fix it — the mirror starts with the
reader UI. Stop the job instead:
```
ssh root@KINDLE_IP /sbin/initctl stop kfbterm   # then /mnt/us/grabtest says "grab OK"
```

### Boot straight into the game (gift unit)

```
mntroot rw
cp /mnt/us/chess-boot.conf /etc/upstart/chess.conf
mntroot ro
```
(that is on the Kindle; `tools/chess-boot.conf` is pushed to `/mnt/us` by
`run-chess-on-kindle.sh`.) The Kindle then waits up to **3 minutes** for Wi-Fi
and starts the game. It starts anyway if Wi-Fi never comes up, because on a
fresh unit the only place to join a network is the game's own Wi-Fi screen.
Tapping GIVE UP ends it and the reader comes back — that exit is deliberate, it
is the only way out.

What it did is written to `/mnt/us/chess-boot.log` (`killing the boot splash …`,
`wifi up after 10s` … `starting the game`). Read it when a boot does not do what
you expect:

```
cat /mnt/us/chess-boot.log
```

Undo: `mntroot rw; rm /etc/upstart/chess.conf; mntroot ro`.

### Change the boot picture (and why the progress bar no longer sticks)

The process that paints the start-up picture is `blanket` (the `x` job runs
`lxinit blanket -t "splash screensaver langpicker"`), and it reads
**`/opt/amazon/low_level_screens/600x800/splash.png`** — the copy in
`/var/local/blanket/...` is *not* what you see at boot.

`blanket` also paints the boot progress bar, and it does not stop when
`framework` is stopped: it kept repainting its splash over the game, leaving a
half-finished bar as a black blob on the board. The game now stops the whole `x`
job once it owns the panel, and does one forced full-panel white flush before
its first paint. (Do not just `killall blanket`: lxinit then exits abnormally,
`x_monitor` restarts the GUI five times and reboots the device into repair
mode.)

```
python3 tools/make_splash.py --logo ~/aijly.com/static/logo.png --out kindle-splash.png
cat kindle-splash.png | ssh root@192.168.0.66 '
  /usr/sbin/mntroot rw
  cp /opt/amazon/low_level_screens/600x800/splash.png /mnt/us/splash-amazon-orig.png
  cat > /opt/amazon/low_level_screens/600x800/splash.png
  sync; /usr/sbin/mntroot ro'
```

The image must be a 600x800 **8-bit greyscale** PNG (colour type 0, no alpha) or
it draws nothing. `tools/make_splash.py` crops the logo, centres it on white and
writes exactly that; `--preview` prints it as text first. Undo:
`cp /mnt/us/splash-amazon-orig.png /opt/amazon/low_level_screens/600x800/splash.png`
(with `mntroot rw`/`ro` around it).

### Wi-fi without the Kindle UI

The game draws its own wi-fi screen: scan, tap a network, type the password on a
drawn keyboard. It writes the result to `/mnt/us/wifi.conf`, so the next boot
joins by itself and the screen never appears again. `SKIP` plays offline (no AI
moves). The joining is done by `/mnt/us/chess-wifi.sh`:

```
sh /mnt/us/chess-wifi.sh status          # state / ssid / ip, exit 0 when online
sh /mnt/us/chess-wifi.sh scan            # ssid \t signal \t wpa2|open
sh /mnt/us/chess-wifi.sh join SSID PSK   # joins and remembers
sh /mnt/us/chess-wifi.sh auto            # rejoin what was remembered
```

## Chess: how it fits together

The Kindle talks to `https://api.deepseek.com/v1/chat/completions` itself, model
`deepseek-flash`. Nothing goes through this PC — no proxy, no relay. The push
also drops this PC's CA bundle at `/mnt/us/chess-ca.pem`, because the Kindle's
own trust store is older than DeepSeek's certificate.

| File | What it is |
|---|---|
| `src/chess.c` | the whole game: rules, drawing, touch, wi-fi screen |
| `tools/chess-wifi.sh` | scan / join / remember, over `wpa_cli` |
| `tools/chess-boot.conf` | upstart job that boots into the game |
| `tools/make_splash.py` | turns a PNG into the Kindle's 600x800 greyscale boot picture |
| `tools/kindle-run.sh` | on-device launcher: frees the screen, runs, restores |
| `chess.conf.example` | key, model, url, ca |

## Quick Start (XCOM)

```
./run-on-kindle.sh                 # the Kindle at 192.168.0.63
./run-on-kindle.sh 192.168.0.64    # a different Kindle
```

It copies the game and its voice clips over, frees the touch screen from the
screen mirror, runs the game, and puts the Kindle back to normal when you quit.

**Play:** tap a soldier along the bottom, tap a boxed square to walk (a thick box
is a long run, and you cannot shoot after one). Tap an alien then FIRE. END
moves the aliens, WATCH shoots the next one that appears, HUNKER is harder to
hit, HEAL is the medic's two heals. Tap QUIT, then SURE.

Pale squares are grass, black trees and rocks block the way, water is closed,
dark fog is what you have not seen. Win = every alien down, lose = every soldier
down.

Sound: the Kindle reads the log out loud, word by word, and plays a bang for a
shot and a thud for a hit. No `/mnt/us/xcom-sound`? Then it says so and runs
silently. `--mute` is the quiet switch. Needs no internet at runtime.

```
make            # PC program, xcom-host
make test       # plays a pretend game and checks pictures, words and clips
make sound      # bakes the spoken words + effects into sound/ (needs TTS)
make manual     # bakes MANUAL.md -> manual.wav (needs TTS)
make kindle     # Kindle program, needs the kfb-build docker image
```

## How it fits together (XCOM)

| File | What it is |
|---|---|
| `src/xcom.c` | the whole game: rules, drawing, touch, PC self-test |
| `src/audio.c` | the speaker: one child process owns `aplay`, the game never waits on it |
| `src/sprites.h` | 40x40 dithered tiles, generated — do not hand-edit |
| `tools/slice_sprites.py` | cuts `spritesheet.png` into those tiles |
| `tools/make_sound.py` | bakes one `.pcm` per spoken word, plus the effects |
| `tools/check_sound.py` | reads the source and fails the build if a word has no clip |
| `sound/` | the baked clips, pushed to `/mnt/us/xcom-sound` |
| `tools/grabtest.c` | "is the touch device free right now?" probe |
| `MANUAL.md` | plain-prose game manual for TTS |
| `tools/make_manual_audio.py` | bakes MANUAL.md into one spoken WAV |

Sound is spliced from files, not generated on the device: `audio.c` turns a log
line into lower-case words, a number into "for four", streams the matching clips
into `aplay`. A missing clip is reported by name, never swapped for something
else.

The screen is 600x800, 8 bits per pixel, and the engine draws pure black/white
only — shading is done with dither patterns. Painting uses the e-ink engine in
`jailbreak-kindle/kindle-tools/fbdaemon/engine`; that folder is shared and never
modified from here.

## When it goes wrong

The launcher pushes `grabtest` first. If it prints `EVIOCGRAB refused: Resource
busy`, something still holds `/dev/input/event3`. The launcher handles the two
usual culprits (a leftover game from a killed session, and the `kfb` screen
mirror); if it still fails, on the Kindle:

```
initctl stop kfbterm
kill -9 $(pidof kfb)
```

`sh /mnt/us/fbdaemon/restore-kindle.sh` gives the Kindle back — but kill the
game first, or it keeps the panel: `kill $(pidof chess)`, then the restore, then
`initctl stop kfbterm`. Stopping the workspace service can leave exactly that
state (the game survives, the reader UI stays down).

If the picture is fine but there is no sound, the launcher says why before it
starts. To check the speaker by hand:

```
amixer sget Speaker          # should read Playback [on]
cat /mnt/us/xcom-sound/words/contact.pcm | aplay -q -D plughw:0,0 -t raw -f S16_LE -c 1 -r 22050 -
```

A `sound: no clip words/x.pcm` line means the game said a word with no file: run
`make sound`, then push again. `make test` catches that before the device does.

Wi-fi trouble: `/mnt/us/chess-wifi.sh status` prints why (`state=`,
`ssid=`, `ip=`); the game's own join errors come from the same script, and its
boot log is on the console. `kill -USR1` on a running game shows the screen.

## On this computer

```
make
./xcom-host --seed 1 --dump /tmp/xcom.pgm            # draw the first screen
./xcom-host --selftest                               # check the rules
./xcom-host --auto 2                                 # let the computer play 2 turns
printf 'tap 120 536\n' | ./xcom-host --seed 1 --dump /tmp/picked.pgm
./xcom-host --soundcheck --auto 1                    # print the words it would say
```

The PC has no speaker, so sound is off there unless you ask for `--soundcheck`,
which only prints the words. On the Kindle it is on — `--auto 3` plays three
turns by itself and quits, which is the quickest way to test it.

Both host programmes take `tap X Y` lines on stdin, so a whole flow can be
replayed without a device; `--dump FILE` writes the screen at the end.
Same `--seed`, same dice, so a bug is repeatable.
