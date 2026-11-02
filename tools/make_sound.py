#!/usr/bin/env python3
"""Bake the Kindle's sound folder: one PCM clip per spoken word, plus effects.

The game speaks by splicing clips: it looks for sound/words/<word>.pcm for
every word of every log line. So the vocabulary here has to match what
src/xcom.c can say -- tools/check_sound.py reads the sources and fails the
build if a word the game can say has no clip.

Speech comes from the local TTS service (127.0.0.1:8770), which is free and
needs no network. The effects are synthesised here, so they work even when the
service is down.

    python3 tools/make_sound.py            # into ./sound
    python3 tools/make_sound.py --out /tmp/snd --only words/one
"""
import argparse
import array
import io
import math
import os
import random
import sys
import urllib.parse
import urllib.request
import wave

RATE = 22050
TTS = "http://127.0.0.1:8770/speak"
VOICE = "Samantha"

# Every word the game can put in front of the player, apart from numbers.
# tools/check_sound.py derives this list from src/xcom.c and fails the build if
# the two drift apart, so a new say() line puts itself in the check.
WORDS = [
    # what the log says
    "first", "contact", "wipe", "them", "out",
    # the mission opening lines (MISSIONS table in src/xcom.c)
    "ford", "cross", "shallows", "long", "road", "stay", "cover",
    "crown", "take", "high", "ground",
    "is", "down", "missed", "soaked", "crit", "hit", "for", "panics",
    "pinned", "freezes", "something", "in", "the", "way", "cannot", "go",
    "there", "too", "far", "that", "shot", "gone", "heal", "not", "a",
    "hurt", "friend", "heals", "tap", "sure", "to", "leave", "pick",
    "soldier", "watch", "watching", "hunker", "hunkers", "only", "medic",
    "and", "twice", "no", "action", "left", "beside", "you", "who",
    "see", "already", "walked", "at",
    # the mute button in the top bar (say() next to MUTE_X)
    "sound", "off", "on",
    # the names of the units on the map, from sp_label through name_from()
    "sniper", "heavy", "medic", "assault", "sectoid", "mutant", "drone",
]

# Damage numbers are spoken as words, so every number the game can print needs
# a clip. audio.c refuses anything over 99, so 0 to 99 is the whole alphabet.
UNITS = ["zero", "one", "two", "three", "four", "five", "six", "seven",
         "eight", "nine"]
TEENS = ["ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen",
         "sixteen", "seventeen", "eighteen", "nineteen"]
TENS = ["twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty",
        "ninety"]
NUMBERS = UNITS + TEENS + TENS

# One name per file in sound/fx/. audio.c refuses any other name.
FX = ["shot", "hit", "miss", "down", "turn", "win", "lose"]


def tts_bytes(word):
    """Ask the local TTS service for one word and return raw 16-bit samples."""
    query = urllib.parse.urlencode({"text": word, "voice": VOICE})
    try:
        with urllib.request.urlopen(f"{TTS}?{query}", timeout=60) as r:
            blob = r.read()
    except OSError as exc:
        sys.exit(f"make_sound: cannot reach the TTS service at {TTS}: {exc}")
    with wave.open(io.BytesIO(blob)) as w:
        if w.getsampwidth() != 2 or w.getnchannels() != 1:
            sys.exit("make_sound: the service returned something other than mono 16-bit")
        rate = w.getframerate()
        samples = array.array("h")
        samples.frombytes(w.readframes(w.getnframes()))
    if rate != RATE:
        samples = resample(samples, rate, RATE)
    return samples


def resample(samples, src, dst):
    n = int(len(samples) * dst / src)
    out = array.array("h", bytes(2 * n))
    step = src / dst
    for i in range(n):
        pos = i * step
        j = int(pos)
        frac = pos - j
        a = samples[j]
        b = samples[j + 1] if j + 1 < len(samples) else a
        out[i] = int(a + (b - a) * frac)
    return out


def trim(samples, floor=200):
    """Drop the silence the engine leaves at both ends, keep 25 ms of it."""
    peak = max((abs(s) for s in samples), default=0)
    if peak == 0:
        return samples
    level = max(floor, peak // 40)
    start, end = 0, len(samples) - 1
    while start < end and abs(samples[start]) < level:
        start += 1
    while end > start and abs(samples[end]) < level:
        end -= 1
    pad = int(RATE * 0.025)
    start = max(0, start - pad)
    end = min(len(samples) - 1, end + pad)
    return samples[start:end + 1]


def normalise(samples, target=12000):
    peak = max((abs(s) for s in samples), default=0)
    if peak == 0:
        return samples
    gain = min(target / peak, 8.0)
    return array.array("h", (max(-32768, min(32767, int(s * gain))) for s in samples))


def save(path, samples):
    with open(path, "wb") as f:
        f.write(samples.tobytes())


def synth(name):
    """The seven effects, built from square-ish noise and sine tones."""
    rng = random.Random(name)
    out = array.array("h")

    def tone(freq, ms, vol=0.5, decay=0.0, shape="sine"):
        n = int(RATE * ms / 1000)
        for i in range(n):
            t = i / RATE
            env = (1.0 - decay * i / n) if n else 1.0
            phase = 2 * math.pi * freq * t
            if shape == "square":
                v = 1.0 if math.sin(phase) >= 0 else -1.0
            else:
                v = math.sin(phase)
            out.append(int(32767 * vol * env * v))

    def noise(ms, vol=0.5, decay=3.0, lowpass=0.4):
        n = int(RATE * ms / 1000)
        last = 0.0
        for i in range(n):
            env = math.exp(-decay * i / n)
            last += lowpass * ((rng.uniform(-1, 1)) - last)
            out.append(int(32767 * vol * env * last))

    if name == "shot":
        noise(90, vol=0.75, decay=4.0, lowpass=0.85)
        tone(90, 70, vol=0.45, decay=0.8, shape="sine")
    elif name == "hit":
        tone(140, 90, vol=0.6, decay=0.9, shape="sine")
        noise(50, vol=0.3, decay=5.0, lowpass=0.5)
    elif name == "miss":
        noise(140, vol=0.35, decay=2.0, lowpass=0.25)
    elif name == "down":
        tone(320, 110, vol=0.5, decay=0.2)
        tone(200, 140, vol=0.5, decay=0.5)
        tone(120, 200, vol=0.45, decay=0.7)
    elif name == "turn":
        tone(660, 70, vol=0.4, decay=0.3)
        tone(880, 110, vol=0.4, decay=0.5)
    elif name == "win":
        for f, ms in ((523, 110), (659, 110), (784, 110), (1047, 260)):
            tone(f, ms, vol=0.42, decay=0.25)
    elif name == "lose":
        for f, ms in ((392, 150), (330, 150), (262, 300)):
            tone(f, ms, vol=0.42, decay=0.35)
    else:
        sys.exit(f"make_sound: no recipe for the effect '{name}'")
    return normalise(out, 20000)


def main():
    ap = argparse.ArgumentParser(description="bake the Kindle sound folder")
    ap.add_argument("--out", default="sound", help="where to write the clips")
    ap.add_argument("--only", default=None,
                    help="bake one clip, e.g. words/one or fx/shot")
    args = ap.parse_args()

    words = os.path.join(args.out, "words")
    fx = os.path.join(args.out, "fx")
    os.makedirs(words, exist_ok=True)
    os.makedirs(fx, exist_ok=True)

    if args.only is None or args.only.startswith("fx/"):
        for name in FX:
            path = os.path.join(fx, name + ".pcm")
            save(path, synth(name))
            print(f"fx/{name}.pcm {os.path.getsize(path)} bytes")

    if args.only is None or args.only.startswith("words/"):
        wanted = list(WORDS) + NUMBERS
        if args.only:
            wanted = [args.only.split("/", 1)[1]]
        done = 0
        for word in sorted(set(wanted)):
            path = os.path.join(words, word + ".pcm")
            samples = normalise(trim(tts_bytes(word)))
            if len(samples) < RATE // 20:
                sys.exit(f"make_sound: '{word}' came back as {len(samples)} samples")
            save(path, samples)
            done += 1
            if done % 10 == 0:
                print(f"  {done}/{len(set(wanted))} words", flush=True)
        print(f"words: {len(set(wanted))} clips")


if __name__ == "__main__":
    main()
