# Plan: Kindle XCOM gets a voice, and the screen stops flashing

## What I measured first (so the plan is not guesswork)

**The drawing is already cheap.** I dumped pictures from the PC build and counted changed pixels:

| Action | Screen changed | Of the whole panel |
|---|---|---|
| opening → tap a soldier | 14 668 px | 3.1% |
| tap a soldier → walk | 41 545 px | 8.7% |
| small log/button update | 279 px | 0.06% |

The shared engine already only pushes the 8×8 tiles that actually changed. So we are **not** redrawing the whole screen per tap — that part is fine.

**The slow bit is the e-ink waveform.** `src/xcom.c` forces the cleanest, slowest mode (`GC16`) on *every* update and waits for it to finish:

```c
ek_waveform(wfm);      /* forces GC16 on every flush */
ek_ghost(0, 0);        /* disables the engine's fast-update cadence */
ek_wait_updates(wait); /* then block until the panel is done */
```

A partial GC16 takes ~150–300 ms and flashes. That is the "why does it feel slow" answer.

**Sound is genuinely possible on the Kindle alone.** Verified on the device just now:

- `/usr/bin/aplay` 1.0.22 and `/usr/bin/amixer` exist, `-t raw -f S16_LE -c 1 -` supported (can be fed on stdin)
- `/dev/snd` has `pcmC0D0p` (playback); `Speaker Mixer` control is present and currently on
- 2.8 GB free on `/mnt/us`

So: **bake the audio once on this PC, play it on the Kindle. No network, no PC at playtime.**

Baking works today: one test line through the local speech service (`127.0.0.1:8770`, voice Samantha, model `full_155mrf22`) made a 2.57 s clip, 113 KB, in 0.77 s.

## Part 1 — Screen: stop forcing the slow waveform

1. Delete the forced `ek_waveform(GC16)`; let the engine choose per frame (fast `DU` for small changes, `GL16_FAST` every few frames, `GC16` only where ghosting actually collected).
2. Tune with `ek_ghost(4, 30)` and `ek_policy(180, 6, 35)`.
3. Add `--clean` — restores today's always-GC16 behaviour for comparing, and `--stats` — prints rects/pixels/waveform/ms after each flush, so the improvement is measured, not asserted.
4. Verify on hardware by eye: no ghosting on text or sprites, taps feel quicker.

## Part 2 — Sound: baked clips, played by the game

**New file `tools/make_sound.py`** (run on this PC, once per release):

- Synthesises the effects with plain maths (no dependencies): shot, hit, miss, unit down, turn over, win, lose, button click.
- Bakes the spoken fragments through the local speech service: 9 unit names, ~20 phrases ("hits", "misses", "is down", "your turn", "first contact. wipe them out", warnings), and the damage numbers one–twelve.
- Trims leading/trailing silence, converts everything to 22 050 Hz mono 16-bit raw PCM, writes `sound/*.pcm` (~1.5 MB total).
- Generates `src/clips.h` (clip ids + file names) so the C code names clips, not paths.
- **Fails loudly** if the speech service is unreachable — no silent empty clips.

**New files `src/audio.c` / `src/audio.h`** (this repo only; the shared engine is untouched):

- `audio_start()`: switches the speaker route on once with `amixer`, then forks one player child.
- Player child: runs `aplay -q -D plughw:0,0 -t raw -f S16_LE -c 1 -r 22050 -` and feeds it PCM; fragments of one line are spliced with a short pause, lines with a longer one. If nothing is said for a few seconds it closes `aplay` and reopens on the next line, so no idle power drain.
- Game side: `audio_say(CLIP_ANA, CLIP_HITS, CLIP_MR_THIN_MAN, CLIP_NUM_4)`. Anything the game cannot name a clip for prints `sound: silent <line>` rather than pretending.
- Host build (`EK_HOST`): no audio device, so it prints the clip script instead — that is what the new `--soundcheck` uses to prove the wiring without a speaker.

**Wiring:** every existing `say()` site that matters gets a matching spoken line (hit, miss, soaking a hit, unit down, panic, freeze, overwatch, hunker, heal, turn end, warnings, win, lose). Ones deliberately left silent (seed, coordinates, refusal to leave) get an explicit `/* on-screen only */` marker. Numbers stay spoken for damage; hit chance stays on screen only (splicing three number fragments per line sounds robotic — say the word if you want it anyway).

**Push:** `run-on-kindle.sh` sends `sound/` once (skips if unchanged). `--mute` turns the voice off.

## Part 3 — Finish
5. `make sound && make kindle && ./run-on-kindle.sh` — listen on the device, play a full turn including the alien turn.
6. Update `README.md` (what it sounds like, `make sound`, `--clean`/`--stats`/`--mute`), `.agent-kb`, commit.

## Risks, stated plainly
- **Ghosting.** Fast waveforms leave faint residue; that is the trade. `--clean` is the escape hatch, and if text ghosts I will force GC16 only on frames that change text.
- **Speech prosody.** Spliced fragments are choppier than a sentence read whole. Baked whole-line clips are used wherever the line is fixed; only names and numbers are spliced.
- **aplay latency.** Feeding one long stream keeps start-up cost to once per session; if the buffer misbehaves I fall back to one `aplay` per line (~200 ms start) — still listenable for a turn-based game.
