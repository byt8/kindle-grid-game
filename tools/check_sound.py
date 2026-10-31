#!/usr/bin/env python3
"""Fail unless every word the game can speak has a clip in sound/words/.

This is the guard that keeps tools/make_sound.py honest. It reads the sources,
not the sound folder, so it catches a new log line the moment it is written:

  * every string literal passed to say(...) in src/xcom.c
  * the name of every unit the SQUAD and ALIENS tables put on the map

and it turns them into words with the rules in src/audio.c: lower case, letters
and digits only, damage numbers spelled out, and a percentage or the bracketed
roll left out (they are on the screen, not in the ear).

    python3 tools/check_sound.py                 # check, exit 1 on a problem
    python3 tools/check_sound.py --print-words   # what it expects, one a line
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(HERE, "tools"))
from make_sound import WORDS, FX, NUMBERS  # noqa: E402


def words_in(fmt):
    """The words audio.c would speak for this format string.

    Returns (words, needs_any_number). A %d stands for a damage number, which
    can be any value, so the caller has to require every number word.
    """
    out = []
    need_numbers = False
    text = fmt.replace("%%", "\x00")          # a literal percent sign
    paren = 0
    i = 0
    while i < len(text):
        c = text[i]
        if c == "(":
            paren += 1
        elif c == ")":
            paren = max(0, paren - 1)
        elif c == "%":
            # a conversion: %s is a name, %d is a number. Percent means the
            # number is a chance, so it stays on the screen.
            j = i + 1
            while j < len(text) and text[j] in "-+ #0123456789.":
                j += 1
            if j < len(text) and text[j] in "sd":
                if text[j] == "d" and paren == 0 and text[j + 1:j + 2] != "\x00":
                    need_numbers = True
                i = j + 1
                continue
            i += 1
            continue
        elif c.isdigit():
            j = i
            while j < len(text) and text[j].isdigit():
                j += 1
            value = int(text[i:j])
            after = j
            while after < len(text) and text[after] == " ":
                after += 1
            if after < len(text) and text[after] == "\x00":
                i = j                       # a chance, not a number
                continue
            if paren == 0:
                if value > 99:
                    sys.exit(f"check_sound: {value} cannot be spoken in {fmt!r}")
                need_numbers = True
            i = j
            continue
        elif c.isalpha():
            j = i
            while j < len(text) and (text[j].isalnum() or text[j] == "'"):
                j += 1
            if paren == 0:
                out.append(text[i:j].lower())
            i = j
            continue
        i += 1
    return out, need_numbers


def say_literals(src):
    """Every string literal inside a say(...) call, which here is one line."""
    for line in src.splitlines():
        k = line.find("say(")
        if k < 0:
            continue
        for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', line[k:]):
            yield lit


def unit_names():
    """The names the units on the map are given, via name_from(sp_label[sp])."""
    sprites = open(os.path.join(HERE, "src", "sprites.h")).read()
    game = open(os.path.join(HERE, "src", "xcom.c")).read()
    ids = re.search(r"sp_id\[SP_COUNT\] = \{(.*?)\};", sprites, re.S)
    labs = re.search(r"sp_label\[SP_COUNT\] = \{(.*?)\};", sprites, re.S)
    if not ids or not labs:
        sys.exit("check_sound: cannot find sp_id/sp_label in src/sprites.h")
    ids = re.findall(r'"([^"]+)"', ids.group(1))
    labs = re.findall(r'"([^"]+)"', labs.group(1))
    if len(ids) != len(labs):
        sys.exit("check_sound: sp_id and sp_label disagree on how many sprites")
    names = []
    for m in re.finditer(r'\{ *"([a-z_]+/[a-z_]+)" *, *\d+ *, *\d+ *\}', game):
        uid = m.group(1)
        if uid not in ids:
            sys.exit(f"check_sound: {uid} is not in sp_id")
        names.append(labs[ids.index(uid)][:15])   # name_from truncates at 15
    return names


def wanted_words():
    game = open(os.path.join(HERE, "src", "xcom.c")).read()
    words = set()
    numbers = False
    for lit in say_literals(game):
        found, need = words_in(lit)
        words.update(found)
        numbers = numbers or need
    for name in unit_names():
        found, need = words_in(name)
        words.update(found)
        if need:
            sys.exit(f"check_sound: a unit is called {name!r}, which has a number in it")
    if numbers:
        words.update(NUMBERS)
    if not words:
        sys.exit("check_sound: found no words at all; the parser is broken")
    return words


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sound", default=os.path.join(HERE, "sound"))
    ap.add_argument("--print-words", action="store_true",
                    help="print the words it expects and stop")
    args = ap.parse_args()

    wanted = wanted_words()
    if args.print_words:
        for w in sorted(wanted):
            print(w)
        return

    spelled = set(WORDS) | set(NUMBERS)
    bad = []

    for w in sorted(wanted - spelled):
        bad.append(f"the game can say '{w}' and tools/make_sound.py has no clip for it")
    for w in sorted(spelled - wanted):
        bad.append(f"tools/make_sound.py bakes '{w}' but the game cannot say it")

    for w in sorted(spelled & wanted):
        path = os.path.join(args.sound, "words", w + ".pcm")
        if not os.path.isfile(path):
            bad.append(f"missing clip {path}")
        elif os.path.getsize(path) < 1000:
            bad.append(f"clip {path} is only {os.path.getsize(path)} bytes")

    for fx in FX:
        path = os.path.join(args.sound, "fx", fx + ".pcm")
        if not os.path.isfile(path):
            bad.append(f"missing clip {path}")

    if bad:
        for line in bad:
            print("check_sound: " + line, file=sys.stderr)
        sys.exit(f"check_sound: {len(bad)} problem(s)")

    print(f"check_sound: {len(wanted)} words and {len(FX)} effects all present")


if __name__ == "__main__":
    main()
