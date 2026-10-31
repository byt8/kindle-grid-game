# XCOM for the Kindle Touch. The e-ink engine stays in jailbreak-kindle.
# This Makefile only compiles it in. It does not change that tree.

ENGINE ?= jailbreak-kindle/kindle-tools/fbdaemon/engine
HOSTCC ?= gcc
PY     ?= python3

HOSTCFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -DEK_HOST -Isrc -I$(ENGINE)

.PHONY: all test kindle sound manual grabtest preview chess chess-test chess-kindle clean

all: xcom-host

# ---- chess: you are white, DeepSeek plays black over its API --------------

chess-host: src/chess.c $(ENGINE)/eink.c $(ENGINE)/eink.h $(ENGINE)/font8x16.h
	$(HOSTCC) $(HOSTCFLAGS) -o $@ src/chess.c $(ENGINE)/eink.c -lm

# The device build: static musl, no libcurl, the game runs the device's own
# curl for the API call.
chess-kindle: src/chess.c $(ENGINE)/eink.c $(ENGINE)/eink.h
	docker run --rm -u "$$(id -u):$$(id -g)" \
	  -v "$$(pwd):/src" \
	  -v /home/brendan/jailbreak-kindle:/jb \
	  -w /src kfb-build \
	  sh -c 'arm-linux-musleabi-gcc -std=c11 -Os -Wall -Wextra -Werror \
	    -ffunction-sections -fdata-sections \
	    -Isrc -I/jb/kindle-tools/fbdaemon/engine \
	    -o chess src/chess.c /jb/kindle-tools/fbdaemon/engine/eink.c \
	    -static -no-pie -Wl,--gc-sections -lm \
	    && arm-linux-musleabi-strip chess'

# Rules, notation and the screen. No API needed: --fake-ai plays both sides.
chess-test: chess-host
	./chess-host --selftest --no-takeover
	./chess-host --fake-ai --auto 8 --no-takeover --dump /tmp/chess-mid.pgm
	printf 'tap 62 96\ntap 62 232\n' | ./chess-host --no-takeover --dump /tmp/chess-sel.pgm
	$(PY) tools/check_pgm.py /tmp/chess-mid.pgm /tmp/chess-sel.pgm


src/sprites.h: tools/slice_sprites.py spritesheet.png tactical_sprite_map_with_descriptions.json
	$(PY) tools/slice_sprites.py

xcom-host: src/xcom.c src/audio.c src/audio.h src/sprites.h $(ENGINE)/eink.c $(ENGINE)/eink.h $(ENGINE)/font8x16.h
	$(HOSTCC) $(HOSTCFLAGS) -o $@ src/xcom.c src/audio.c $(ENGINE)/eink.c -lm

# Bake the speaker clips: the words the log can say, plus the effects.
# Needs the local TTS service; the device itself only reads the files.
sound:
	$(PY) tools/make_sound.py --out sound

# Spoken manual for offline listening. Needs the same local TTS service.
manual: manual.wav

manual.wav: MANUAL.md tools/make_manual_audio.py
	$(PY) tools/make_manual_audio.py

# Static musl ARM, same shape as ekdemo. The engine lives outside this
# folder, so the container gets its own mount at /jb.
kindle: src/xcom.c src/audio.c src/audio.h src/sprites.h
	docker run --rm -u "$$(id -u):$$(id -g)" \
	  -v "$$(pwd):/src" \
	  -v /home/brendan/jailbreak-kindle:/jb \
	  -w /src kfb-build \
	  sh -c 'arm-linux-musleabi-gcc -std=c11 -Os -Wall -Wextra -Werror \
	    -ffunction-sections -fdata-sections \
	    -Isrc -I/jb/kindle-tools/fbdaemon/engine \
	    -o xcom src/xcom.c src/audio.c /jb/kindle-tools/fbdaemon/engine/eink.c \
	    -static -no-pie -Wl,--gc-sections -lm \
	    && arm-linux-musleabi-strip xcom'

# Diagnostic: can /dev/input/event3 be grabbed right now? Pushed by
# run-on-kindle.sh as the touch pre-flight.
grabtest: tools/grabtest.c
	docker run --rm -u "$$(id -u):$$(id -g)" \
	  -v "$$(pwd):/src" -w /src kfb-build sh -c \
	  'arm-linux-musleabi-gcc -std=c11 -Os -Wall -Wextra -Werror \
	    -o grabtest tools/grabtest.c -static -no-pie \
	    && arm-linux-musleabi-strip grabtest'

keyprobe: tools/keyprobe.c
	docker run --rm -u "$$(id -u):$$(id -g)" \
	  -v "$$(pwd):/src" -w /src kfb-build sh -c \
	  'arm-linux-musleabi-gcc -std=c11 -Os -Wall -Wextra -Werror \
	    -o keyprobe tools/keyprobe.c -static -no-pie \
	    && arm-linux-musleabi-strip keyprobe'

test: xcom-host
	./xcom-host --selftest --no-takeover --seed 1
	./xcom-host --no-takeover --seed 1 --dump /tmp/xcom-open.pgm
	printf 'tap 120 536\n' | ./xcom-host --no-takeover --seed 1 --dump /tmp/xcom-sel.pgm
	./xcom-host --auto 2 --no-takeover --seed 1 --dump /tmp/xcom-mid.pgm
	$(PY) tools/check_pgm.py /tmp/xcom-open.pgm /tmp/xcom-sel.pgm /tmp/xcom-mid.pgm
	$(PY) tools/check_sound.py --sound sound

preview: src/sprites.h
	$(PY) tools/slice_sprites.py --preview /tmp/xcom-sprites.png

clean:
	rm -f xcom-host xcom grabtest chess-host chess
