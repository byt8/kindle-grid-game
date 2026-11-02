# XCOM for the Kindle Touch. The e-ink engine stays in jailbreak-kindle.
# This Makefile only compiles it in. It does not change that tree.

ENGINE ?= jailbreak-kindle/kindle-tools/fbdaemon/engine
HOSTCC ?= gcc
PY     ?= python3

HOSTCFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -DEK_HOST -Isrc -I$(ENGINE)

.PHONY: all test kindle sound manual grabtest preview preview-old bold chess chess-test chess-kindle \
        tetris tetris-host tetris-test tetris-kindle sudoku sudoku-host sudoku-test sudoku-kindle \
        menu-host menu-kindle menu-test rockman-data rockman-host rockman-test rockman-kindle rockman clean

# ---- rockman data: regenerate from the commented VIC-20 disassembly --------

.PHONY: rockman-data
rockman-data:
	$(PY) tools/extract_rockman.py --check

# ---- rockman: the 1984 cave digger, same panel, tap buttons -------------

rockman-host: src/rockman.c src/rockman_data.h $(ENGINE)/eink.c $(ENGINE)/eink.h
	$(HOSTCC) $(HOSTCFLAGS) -o $@ src/rockman.c $(ENGINE)/eink.c -lm

rockman-kindle: src/rockman.c src/rockman_data.h $(ENGINE)/eink.c $(ENGINE)/eink.h
	docker run --rm -u "$$(id -u):$$(id -g)" \
	  -v "$$(pwd):/src" \
	  -v /home/brendan/jailbreak-kindle:/jb \
	  -w /src kfb-build \
	  sh -c 'arm-linux-musleabi-gcc -std=c11 -Os -Wall -Wextra -Werror \
	    -ffunction-sections -fdata-sections \
	    -Isrc -I/jb/kindle-tools/fbdaemon/engine \
	    -o rockman src/rockman.c /jb/kindle-tools/fbdaemon/engine/eink.c \
	    -static -no-pie -Wl,--gc-sections -lm \
	    && arm-linux-musleabi-strip rockman'

rockman: rockman-kindle

# Rules first, then three pictures: the title, room 1, and 24 bot steps.
rockman-test: rockman-host
	./rockman-host --selftest --no-takeover --seed 1
	./rockman-host --no-takeover --seed 1 --dump /tmp/rockman-title.pgm
	printf 'tap 300 400\n' | ./rockman-host --no-takeover --seed 1 --dump /tmp/rockman-room.pgm
	./rockman-host --auto 24 --no-takeover --seed 1 --dump /tmp/rockman-play.pgm
	$(PY) tools/check_pgm.py /tmp/rockman-title.pgm /tmp/rockman-room.pgm /tmp/rockman-play.pgm
	$(PY) tools/check_rockman.py /tmp/rockman-title.pgm /tmp/rockman-room.pgm /tmp/rockman-play.pgm

# ---- tetris: 10x20 well, on-screen buttons, no keyboard ------------------

tetris-host: src/tetris.c $(ENGINE)/eink.c $(ENGINE)/eink.h $(ENGINE)/font8x16.h
	$(HOSTCC) $(HOSTCFLAGS) -o $@ src/tetris.c $(ENGINE)/eink.c -lm

tetris: tetris-kindle

# ---- sudoku: one 9x9 board, generated with a proof it has one solution. ----
# Tap a cell, tap a digit; ERASE / HINT / NEW / QUIT along the bottom.

sudoku-host: src/sudoku.c $(ENGINE)/eink.c $(ENGINE)/eink.h $(ENGINE)/font8x16.h
	$(HOSTCC) $(HOSTCFLAGS) -o $@ src/sudoku.c $(ENGINE)/eink.c -lm

sudoku-kindle: src/sudoku.c $(ENGINE)/eink.c $(ENGINE)/eink.h
	docker run --rm -u "$$(id -u):$$(id -g)" \
	  -v "$$(pwd):/src" \
	  -v /home/brendan/jailbreak-kindle:/jb \
	  -w /src kfb-build \
	  sh -c 'arm-linux-musleabi-gcc -std=c11 -Os -Wall -Wextra -Werror \
	    -ffunction-sections -fdata-sections \
	    -Isrc -I/jb/kindle-tools/fbdaemon/engine \
	    -o sudoku src/sudoku.c /jb/kindle-tools/fbdaemon/engine/eink.c \
	    -static -no-pie -Wl,--gc-sections -lm \
	    && arm-linux-musleabi-strip sudoku'

sudoku: sudoku-kindle

# Rules first, then the picture: the opening, a cell tap + a digit, and a few
# --auto hints. The three dumps must differ from each other.
sudoku-test: sudoku-host
	./sudoku-host --selftest --no-takeover --seed 7
	./sudoku-host --no-takeover --seed 7 --dump /tmp/sudoku-open.pgm
	printf 'tap 60 200\ntap 20 750\n' | ./sudoku-host --no-takeover --seed 7 --dump /tmp/sudoku-digit.pgm
	./sudoku-host --auto 3 --no-takeover --seed 7 --dump /tmp/sudoku-hint.pgm
	$(PY) tools/check_pgm.py /tmp/sudoku-open.pgm /tmp/sudoku-digit.pgm /tmp/sudoku-hint.pgm

# ---- launcher menu: what the Kindle boots into ---------------------------------

menu-host: src/menu.c $(ENGINE)/eink.c $(ENGINE)/eink.h
	$(HOSTCC) $(HOSTCFLAGS) -o $@ src/menu.c $(ENGINE)/eink.c -lm

menu-kindle: src/menu.c $(ENGINE)/eink.c $(ENGINE)/eink.h
	docker run --rm -u "$$(id -u):$$(id -g)" \
	  -v "$$(pwd):/src" \
	  -v /home/brendan/jailbreak-kindle:/jb \
	  -w /src kfb-build \
	  sh -c 'arm-linux-musleabi-gcc -std=c11 -Os -Wall -Wextra -Werror \
	    -ffunction-sections -fdata-sections \
	    -Isrc -I/jb/kindle-tools/fbdaemon/engine \
	    -o menu src/menu.c /jb/kindle-tools/fbdaemon/engine/eink.c \
	    -static -no-pie -Wl,--gc-sections -lm \
	    && arm-linux-musleabi-strip menu'

menu: menu-kindle

# Layout + walk: draw it, dump it, then tap each button and check the exit code.
menu-test: menu-host
	./menu-host --no-takeover --dump /tmp/menu.pgm
	printf ''                        | ./menu-host --no-takeover; test $$? -eq 0
	printf 'tap 300 202\n'           | ./menu-host --no-takeover; test $$? -eq 1
	printf 'tap 300 274\n'           | ./menu-host --no-takeover; test $$? -eq 2
	printf 'tap 300 346\n'           | ./menu-host --no-takeover; test $$? -eq 3
	printf 'tap 300 418\n'           | ./menu-host --no-takeover; test $$? -eq 4
	printf 'tap 300 490\n'           | ./menu-host --no-takeover; test $$? -eq 5
	printf 'tap 300 562\n'           | ./menu-host --no-takeover; test $$? -eq 6
	printf 'tap 300 627\n'           | ./menu-host --no-takeover; test $$? -eq 7
	printf 'tap 300 687\n'           | ./menu-host --no-takeover; test $$? -eq 0
	$(PY) -c 'import sys;d=open("/tmp/menu.pgm","rb").read();p=b"P5\n600 800\n255\n";assert d.startswith(p);pix=d[len(p):];assert len(pix)==600*800;assert pix.count(0)>1000 and pix.count(255)>1000;print("menu.pgm ok")'

all: xcom-host

# ---- chess: you are white, DeepSeek plays black over its API --------------

chess-host: src/chess.c $(ENGINE)/eink.c $(ENGINE)/eink.h $(ENGINE)/font8x16.h
	$(HOSTCC) $(HOSTCFLAGS) -o $@ src/chess.c $(ENGINE)/eink.c -lm

# The device build: static musl, no libcurl, the game runs the device's own
# curl for the API call.
tetris-kindle: src/tetris.c $(ENGINE)/eink.c $(ENGINE)/eink.h
	docker run --rm -u "$$(id -u):$$(id -g)" \
	  -v "$$(pwd):/src" \
	  -v /home/brendan/jailbreak-kindle:/jb \
	  -w /src kfb-build \
	  sh -c 'arm-linux-musleabi-gcc -std=c11 -Os -Wall -Wextra -Werror \
	    -ffunction-sections -fdata-sections \
	    -Isrc -I/jb/kindle-tools/fbdaemon/engine \
	    -o tetris src/tetris.c /jb/kindle-tools/fbdaemon/engine/eink.c \
	    -static -no-pie -Wl,--gc-sections -lm \
	    && arm-linux-musleabi-strip tetris'

# Rules first, then the picture: the opening, one TURN tap, and 24 pieces
# played by --auto. The three dumps must differ from each other.
tetris-test: tetris-host
	./tetris-host --selftest --no-takeover --seed 1
	./tetris-host --no-takeover --seed 1 --dump /tmp/tetris-open.pgm
	printf 'tap 299 757\n' | ./tetris-host --no-takeover --seed 1 --dump /tmp/tetris-drop.pgm
	./tetris-host --auto 24 --no-takeover --seed 1 --dump /tmp/tetris-play.pgm
	$(PY) tools/check_pgm.py /tmp/tetris-open.pgm /tmp/tetris-drop.pgm /tmp/tetris-play.pgm
	$(PY) tools/check_tetris.py /tmp/tetris-open.pgm /tmp/tetris-play.pgm

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


# sprites.h is baked from the bold art in bold/ plus the patterns inside the
# baker. The generated sheets are committed, so this rule needs no network.
src/sprites.h: tools/bake_bold_sprites.py bold_sprite_sources.json \
               bold/players.png bold/enemies.png bold/objects.png bold/drone.png \
               tactical_sprite_map_with_descriptions.json
	$(PY) tools/bake_bold_sprites.py

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
	printf 'tap 450 16\n'  | ./xcom-host --no-takeover --seed 1 --dump /tmp/xcom-mute.pgm
	./xcom-host --auto 2 --no-takeover --seed 1 --dump /tmp/xcom-mid.pgm
	$(PY) tools/check_pgm.py /tmp/xcom-open.pgm /tmp/xcom-sel.pgm /tmp/xcom-mute.pgm /tmp/xcom-mid.pgm
	$(PY) tools/check_sound.py --sound sound

# Every baked tile twice: raw 2-bit, and as it looks on the panel.
preview: src/sprites.h
	$(PY) tools/bake_bold_sprites.py --preview bold-preview.png

# The old dithered look, kept only for comparison. Needs spritesheet.png and
# writes to /tmp so it never overwrites the current src/sprites.h.
preview-old:
	$(PY) tools/slice_sprites.py --preview /tmp/xcom-sprites-old.png

clean:
	rm -f xcom-host xcom grabtest chess-host chess tetris-host tetris sudoku-host sudoku \
	      rockman-host rockman

# --- SimCity POC (600x800 screen mock) -------------------------------------
poc: poc/screen_600x800_grey.png poc/screen_600x800_ink.png

poc/screen_600x800_grey.png poc/screen_600x800_ink.png: poc/render_poc.py poc/src_*.png | upstream/tiles.bmp
	python3 poc/render_poc.py

upstream/tiles.bmp:
	mkdir -p upstream
	curl -sL -o $@ "https://raw.githubusercontent.com/SimHacker/MicropolisCore/main/content/micropolis/tilesets/classic/tiles.bmp"
