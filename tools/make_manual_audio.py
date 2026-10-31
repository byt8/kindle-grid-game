#!/usr/bin/env python3
"""Bake MANUAL.md into one spoken audio file, so the manual can be listened to.

Each paragraph is sent to the local TTS service on 127.0.0.1:8770 (the same one
tools/make_sound.py uses) and the returned WAVs are joined with a short pause
between them. Markdown is stripped first: a '#' heading, a **bold** marker or a
backtick would otherwise be read out as punctuation.

    python3 tools/make_manual_audio.py                 # -> manual.wav
    python3 tools/make_manual_audio.py --out /tmp/m.wav --voice Tom

A paragraph the service refuses is an error: this exits non-zero and names the
paragraph. Nothing is skipped and no paragraph is swapped for a placeholder.
"""
import argparse
import io
import re
import sys
import urllib.error
import urllib.parse
import urllib.request
import wave

TTS = "http://127.0.0.1:8770/speak"
MAX_CHARS = 900          # keep the GET url short
GAP_MS = 450             # the pause between paragraphs


def spoken(text):
    """Markdown out, plain sentences left. Read aloud, not rendered."""
    text = re.sub(r"\*\*(.+?)\*\*", r"\1", text)
    text = text.replace("`", "")
    text = re.sub(r"^#+\s*", "", text, flags=re.M)
    text = re.sub(r"^(?:[-*]|\d+\.)\s+", "", text, flags=re.M)
    text = re.sub(r"\s+", " ", text)
    return text.strip()


def paragraphs(path):
    """Every block of prose in the file, in order, as one-line strings."""
    out = []
    for block in open(path, encoding="utf-8").read().split("\n\n"):
        text = spoken(block)
        if not text:
            continue
        for piece in split_long(text):
            out.append(piece)
    if not out:
        sys.exit("make_manual_audio: %s has no prose to speak" % path)
    return out


def split_long(text):
    """Cut a paragraph at sentence ends so one request stays under MAX_CHARS."""
    if len(text) <= MAX_CHARS:
        return [text]
    pieces, current = [], ""
    for sentence in re.split(r"(?<=[.!?])\s+", text):
        while len(sentence) > MAX_CHARS:
            cut = sentence.rfind(", ", 0, MAX_CHARS)
            if cut < 200:
                cut = sentence.rfind(" ", 0, MAX_CHARS)
            if cut < 200:
                sys.exit("make_manual_audio: a sentence has no break to split on")
            if current:
                pieces.append(current)
                current = ""
            pieces.append(sentence[:cut + 1].strip())
            sentence = sentence[cut + 1:].strip()
        if current and len(current) + 1 + len(sentence) > MAX_CHARS:
            pieces.append(current)
            current = sentence
        else:
            current = (current + " " + sentence).strip()
    if current:
        pieces.append(current)
    return pieces


def fetch(text, voice):
    """One paragraph from the speech service, as WAV bytes."""
    url = "%s?%s" % (TTS, urllib.parse.urlencode({"text": text, "voice": voice}))
    try:
        with urllib.request.urlopen(url, timeout=120) as r:
            status, body, kind = r.status, r.read(), r.headers.get("Content-Type")
    except urllib.error.URLError as e:
        sys.exit("make_manual_audio: cannot reach the speech service at %s: %s\n"
                 "                 start it, or see tools/make_sound.py for the same service."
                 % (TTS, e))
    if status != 200:
        sys.exit("make_manual_audio: the speech service answered %d for: %s" % (status, text[:60]))
    if not kind or "audio" not in kind or not body.startswith(b"RIFF"):
        sys.exit("make_manual_audio: the speech service did not return a WAV (got %r) for: %s"
                 % (kind, text[:60]))
    return body


def decode(blob, where):
    """The raw sample frames and the WAV parameters, checked, not assumed."""
    with wave.open(io.BytesIO(blob), "rb") as w:
        params = (w.getnchannels(), w.getsampwidth(), w.getframerate())
        frames = w.readframes(w.getnframes())
    if params != (1, 2, 22050):
        sys.exit("make_manual_audio: unexpected audio format %s in %s" % (params, where))
    return frames, params





def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default="MANUAL.md")
    ap.add_argument("--out", default="manual.wav")
    ap.add_argument("--voice", default="Samantha")
    args = ap.parse_args()

    chunks = paragraphs(args.src)
    silence_cache = {}
    frames = []
    params = None
    for i, text in enumerate(chunks, 1):
        print("  %2d/%d  %s" % (i, len(chunks), text[:58]))
        blob = fetch(text, args.voice)
        piece, got = decode(blob, "paragraph %d" % i)
        if params and got != params:
            sys.exit("make_manual_audio: paragraph %d is %s, the rest are %s" % (i, got, params))
        params = got
        frames.append(piece)
        if got not in silence_cache:
            silence_cache[got] = b"\x00\x00" * (got[0] * got[2] * GAP_MS // 1000)
        frames.append(silence_cache[got])

    with wave.open(args.out, "wb") as w:
        w.setnchannels(params[0])
        w.setsampwidth(params[1])
        w.setframerate(params[2])
        w.writeframes(b"".join(frames))

    with wave.open(args.out, "rb") as w:
        seconds = w.getnframes() / float(w.getframerate())
    print("make_manual_audio: %d passages, %d:%02d, %s"
          % (len(chunks), seconds // 60, seconds % 60, args.out))


if __name__ == "__main__":
    main()
