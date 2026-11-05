# XCOM for the Kindle Touch. The e-ink engine stays in jailbreak-kindle.
# This Makefile only compiles it in. It does not change that tree.

ENGINE ?= jailbreak-kindle/kindle-tools/fbdaemon/engine
HOSTCC ?= gcc
PY     ?= python3

HOSTCFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -DEK_HOST -Isrc -I$(ENGINE)

.PHONY: all test kindle sound manual grabtest preview preview-old bold chess chess-test chess-kindle \
        tetris tetris-host tetris-test tetris-kindle sudoku sudoku-host sudoku-test sudoku-kindle \
        mines mines-host mines-test mines-kindle \
        menu-host menu-kindle menu-test rockman-data rockman-host rockman-test rockman-kindle rockman \
        pvz-host pvz pvz-test calib calib-host calib-kindle calib-test \
        brogue-host brogue brogue-test brogue-kindle engine-test clean

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

tetris-host: src/tetris.c src/kframe.c src/kframe.h src/kshell.c src/kshell.h $(ENGINE)/eink.c $(ENGINE)/eink.h $(ENGINE)/font8x16.h
	$(HOSTCC) $(HOSTCFLAGS) -o $@ src/tetris.c src/kframe.c src/kshell.c $(ENGINE)/eink.c -lm

tetris: tetris-kindle

# ---- sudoku: one 9x9 board, generated with a proof it has one solution. ----
# Tap a cell, tap a digit; ERASE / HINT / NEW / QUIT along the bottom.

sudoku-host: src/sudoku.c src/kframe.c src/kframe.h src/kshell.c src/kshell.h $(ENGINE)/eink.c $(ENGINE)/eink.h $(ENGINE)/font8x16.h
	$(HOSTCC) $(HOSTCFLAGS) -o $@ src/sudoku.c src/kframe.c src/kshell.c $(ENGINE)/eink.c -lm

sudoku-kindle: src/sudoku.c src/kframe.c src/kframe.h src/kshell.c src/kshell.h $(ENGINE)/eink.c $(ENGINE)/eink.h
	docker run --rm -u "$$(id -u):$$(id -g)" \
	  -v "$$(pwd):/src" \
	  -v /home/brendan/jailbreak-kindle:/jb \
	  -w /src kfb-build \
	  sh -c 'arm-linux-musleabi-gcc -std=c11 -Os -Wall -Wextra -Werror \
	    -ffunction-sections -fdata-sections \
	    -Isrc -I/jb/kindle-tools/fbdaemon/engine \
	    -o sudoku src/sudoku.c src/kframe.c src/kshell.c /jb/kindle-tools/fbdaemon/engine/eink.c \
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
	# The hamburger is a 48x48 square at (548,2). Centre (572, 26).
	printf 'tap 572 26\n' | ./sudoku-host --no-takeover --seed 7 --dump /tmp/sudoku-menu.pgm \
	    >/tmp/sudoku-menu.out 2>/tmp/sudoku-menu.err
	grep -q 'kframe: menu open' /tmp/sudoku-menu.err
	# APPS is the right-hand group. Centre (500, 24). From sudoku, chess is
	# the second row (xcom is first). Centre (300, 212). See kframe.h.
	printf 'tap 572 26\ntap 500 24\n' | ./sudoku-host --no-takeover --seed 7 --dump /tmp/sudoku-apps.pgm \
	    >/tmp/sudoku-apps.out 2>/tmp/sudoku-apps.err
	grep -q 'kframe: apps open' /tmp/sudoku-apps.err
	$(PY) tools/check_pgm.py /tmp/sudoku-open.pgm /tmp/sudoku-menu.pgm /tmp/sudoku-apps.pgm
	# The menu carries the controls that are not a digit: ERASE, HINT, LEVEL.
	# Left tabs are all 60 wide at KSHELL_TAB_MIN_W. Centres (36,24) NEW,
	# (100,24) ERASE, (164,24) HINT, (228,24) LEVEL. See kshell.h.
	# LEVEL row 2 (HARD) is centred at (300, 344).
	printf 'tap 572 26\ntap 228 24\ntap 300 344\n' | ./sudoku-host --no-takeover --seed 7 \
	    --dump /tmp/sudoku-level.pgm >/tmp/sudoku-level.out 2>/tmp/sudoku-level.err
	grep -q 'kframe: app item 4' /tmp/sudoku-level.err
	# ERASE and HINT act on the picked cell. Pick (60,440), type 5 from the
	# board row, then use the menu: the cell must come back empty / filled.
	printf 'tap 60 440\ntap 300 750\n' | ./sudoku-host --no-takeover --seed 7 \
	    --dump /tmp/sudoku-typed.pgm
	printf 'tap 60 440\ntap 300 750\ntap 572 26\ntap 100 24\ntap 300 236\n' \
	    | ./sudoku-host --no-takeover --seed 7 --dump /tmp/sudoku-erased.pgm
	printf 'tap 60 440\ntap 572 26\ntap 164 24\ntap 300 236\n' \
	    | ./sudoku-host --no-takeover --seed 7 --dump /tmp/sudoku-hinted.pgm
	printf 'tap 60 440\n' | ./sudoku-host --no-takeover --seed 7 --dump /tmp/sudoku-pick.pgm
	cmp -s /tmp/sudoku-pick.pgm /tmp/sudoku-erased.pgm
	! cmp -s /tmp/sudoku-pick.pgm /tmp/sudoku-hinted.pgm
	! cmp -s /tmp/sudoku-open.pgm /tmp/sudoku-typed.pgm
	$(PY) tools/check_pgm.py /tmp/sudoku-open.pgm /tmp/sudoku-level.pgm
	rm -f /tmp/switch-app
	printf 'tap 572 26\ntap 500 24\ntap 300 212\n' | ./sudoku-host --no-takeover --seed 7 \
	    >/tmp/sudoku-switch.out 2>/tmp/sudoku-switch.err
	grep -q 'kframe: switch chess' /tmp/sudoku-switch.err
	grep -qx 'chess' /tmp/switch-app

# ---- minesweeper: 9x9 / 12x12 / 16x16, tap to open, FLAG to mark --------------
# The first tap is always safe: mines go down after it, clear of its 3x3.

mines-host: src/minesweeper.c src/kframe.c src/kframe.h src/kshell.c src/kshell.h $(ENGINE)/eink.c $(ENGINE)/eink.h $(ENGINE)/font8x16.h
	$(HOSTCC) $(HOSTCFLAGS) -o $@ src/minesweeper.c src/kframe.c src/kshell.c $(ENGINE)/eink.c -lm

mines-kindle: src/minesweeper.c src/kframe.c src/kframe.h src/kshell.c src/kshell.h $(ENGINE)/eink.c $(ENGINE)/eink.h
	docker run --rm -u "$$(id -u):$$(id -g)" \
	  -v "$$(pwd):/src" \
	  -v /home/brendan/jailbreak-kindle:/jb \
	  -w /src kfb-build \
	  sh -c 'arm-linux-musleabi-gcc -std=c11 -Os -Wall -Wextra -Werror \
	    -ffunction-sections -fdata-sections \
	    -Isrc -I/jb/kindle-tools/fbdaemon/engine \
	    -o mines src/minesweeper.c src/kframe.c src/kshell.c /jb/kindle-tools/fbdaemon/engine/eink.c \
	    -static -no-pie -Wl,--gc-sections -lm \
	    && arm-linux-musleabi-strip mines'

mines: mines-kindle

# The opening, a tapped board, a flag, and the level screen: each must differ.
mines-test: mines-host
	./mines-host --selftest --no-takeover --seed 7
	./mines-host --no-takeover --seed 7 --dump /tmp/mines-open.pgm
	printf 'tap 300 400\n' | ./mines-host --no-takeover --seed 7 --dump /tmp/mines-tap.pgm
	./mines-host --auto 12 --no-takeover --seed 7 --dump /tmp/mines-play.pgm
	$(PY) tools/check_pgm.py /tmp/mines-open.pgm /tmp/mines-tap.pgm /tmp/mines-play.pgm
	! cmp -s /tmp/mines-open.pgm /tmp/mines-tap.pgm
	! cmp -s /tmp/mines-open.pgm /tmp/mines-play.pgm
	# The hamburger is a 48x48 square at (548,2). Centre (572, 26).
	# BOTH is the right-hand group at (500, 24).
	printf 'tap 572 26\n' | ./mines-host --no-takeover --seed 7 --dump /tmp/mines-menu.pgm \
	    >/tmp/mines-menu.out 2>/tmp/mines-menu.err
	grep -q 'kframe: menu open' /tmp/mines-menu.err
	printf 'tap 572 26\ntap 500 24\n' | ./mines-host --no-takeover --seed 7 \
	    --dump /tmp/mines-apps.pgm >/tmp/mines-apps.out 2>/tmp/mines-apps.err
	grep -q 'kframe: apps open' /tmp/mines-apps.err
	$(PY) tools/check_pgm.py /tmp/mines-menu.pgm /tmp/mines-apps.pgm
	# Left tabs are 60 wide at KSHELL_TAB_MIN_W: (36,24) NEW, (100,24) FLAG,
	# (164,24) LEVEL. LEVEL row 3 (HARD) is centred at (300, 344).
	printf 'tap 572 26\ntap 164 24\ntap 300 344\n' | ./mines-host --no-takeover --seed 7 \
	    --dump /tmp/mines-level.pgm >/tmp/mines-level.out 2>/tmp/mines-level.err
	grep -q 'kframe: app item 3' /tmp/mines-level.err
	$(PY) tools/check_pgm.py /tmp/mines-open.pgm /tmp/mines-level.pgm
	# FLAG marks the square, and the same tap in NEW mode opens squares.
	printf 'tap 300 400\ntap 572 26\ntap 100 24\ntap 300 236\n' \
	    | ./mines-host --no-takeover --seed 7 --dump /tmp/mines-flag.pgm
	! cmp -s /tmp/mines-tap.pgm /tmp/mines-flag.pgm
	$(PY) tools/check_pgm.py /tmp/mines-tap.pgm /tmp/mines-flag.pgm
	# From mines, the chess row in BOTH switches the panel to chess.
	rm -f /tmp/switch-app
	printf 'tap 572 26\ntap 500 24\ntap 300 212\n' | ./mines-host --no-takeover --seed 7 \
	    >/tmp/mines-switch.out 2>/tmp/mines-switch.err
	grep -q 'kframe: switch chess' /tmp/mines-switch.err
	grep -qx 'chess' /tmp/switch-app

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

chess-host: src/chess.c src/kframe.c src/kframe.h src/kshell.c src/kshell.h $(ENGINE)/eink.c $(ENGINE)/eink.h $(ENGINE)/font8x16.h
	$(HOSTCC) $(HOSTCFLAGS) -o $@ src/chess.c src/kframe.c src/kshell.c $(ENGINE)/eink.c -lm

# The device build: static musl, no libcurl, the game runs the device's own
# curl for the API call.
tetris-kindle: src/tetris.c src/kframe.c src/kframe.h src/kshell.c src/kshell.h $(ENGINE)/eink.c $(ENGINE)/eink.h
	docker run --rm -u "$$(id -u):$$(id -g)" \
	  -v "$$(pwd):/src" \
	  -v /home/brendan/jailbreak-kindle:/jb \
	  -w /src kfb-build \
	  sh -c 'arm-linux-musleabi-gcc -std=c11 -Os -Wall -Wextra -Werror \
	    -ffunction-sections -fdata-sections \
	    -Isrc -I/jb/kindle-tools/fbdaemon/engine \
	    -o tetris src/tetris.c src/kframe.c src/kshell.c /jb/kindle-tools/fbdaemon/engine/eink.c \
	    -static -no-pie -Wl,--gc-sections -lm \
	    && arm-linux-musleabi-strip tetris'

# ---- calib: tap-driven perception tests for the panel ----------------------
# Build, push and run like a game, but the screen is a question you answer
# by tapping: a 1 Hz tick, a smear test, and a tap-reaction measurement.

calib-host: src/calib.c $(ENGINE)/eink.c $(ENGINE)/eink.h
	$(HOSTCC) $(HOSTCFLAGS) -o $@ src/calib.c $(ENGINE)/eink.c -lm

calib-kindle: src/calib.c $(ENGINE)/eink.c $(ENGINE)/eink.h
	docker run --rm -u "$$(id -u):$$(id -g)" \
	  -v "$$(pwd):/src" \
	  -v /home/brendan/jailbreak-kindle:/jb \
	  -w /src kfb-build \
	  sh -c 'arm-linux-musleabi-gcc -std=c11 -Os -Wall -Wextra -Werror \
	    -ffunction-sections -fdata-sections \
	    -Isrc -I/jb/kindle-tools/fbdaemon/engine \
	    -o calib src/calib.c /jb/kindle-tools/fbdaemon/engine/eink.c \
	    -static -no-pie -Wl,--gc-sections -lm \
	    && arm-linux-musleabi-strip calib'

calib: calib-kindle

# Rules first, then the picture: the opening, one TURN tap, and 24 pieces
# played by --auto. The three dumps must differ from each other.
tetris-test: tetris-host
	./tetris-host --selftest --no-takeover --seed 1
	./tetris-host --no-takeover --seed 1 --dump /tmp/tetris-open.pgm
	printf 'tap 299 757\n' | ./tetris-host --no-takeover --seed 1 --dump /tmp/tetris-drop.pgm
	./tetris-host --auto 24 --no-takeover --seed 1 --dump /tmp/tetris-play.pgm
	$(PY) tools/check_pgm.py /tmp/tetris-open.pgm /tmp/tetris-drop.pgm /tmp/tetris-play.pgm
	$(PY) tools/check_tetris.py /tmp/tetris-open.pgm /tmp/tetris-play.pgm
	# Hamburger (572, 26) opens the shell. Left tabs are NEW, PAUSE, SPEED
	# (60 px each from x=6: centres 36, 100, 164). See kshell.h.
	printf 'tap 572 26\n' | ./tetris-host --no-takeover --seed 1 --dump /tmp/tetris-menu.pgm \
	    >/tmp/tetris-menu.out 2>/tmp/tetris-menu.err
	grep -q 'kframe: menu open' /tmp/tetris-menu.err
	# PAUSE tab, then the action button (KFRAME_ACT_*) holds the board.
	printf 'tap 572 26\ntap 100 24\ntap 300 236\n' | ./tetris-host --no-takeover --seed 1 \
	    --dump /tmp/tetris-paused.pgm >/tmp/tetris-paused.out 2>/tmp/tetris-paused.err
	grep -q 'kframe: app item 2' /tmp/tetris-paused.err
	# SPEED tab, then the first row (PANEL 320ms) is applied.
	printf 'tap 572 26\ntap 164 24\ntap 300 176\n' | ./tetris-host --no-takeover --seed 1 \
	    --dump /tmp/tetris-speed.pgm >/tmp/tetris-speed.out 2>/tmp/tetris-speed.err
	grep -q 'kframe: app item 3' /tmp/tetris-speed.err
	# The three tabs must look different from each other and from the board.
	$(PY) tools/check_pgm.py /tmp/tetris-open.pgm /tmp/tetris-paused.pgm /tmp/tetris-speed.pgm
	# APPS is the right-hand group. Centre (500, 24). From tetris, chess is
	# the second row (xcom is first). Centre (300, 212). See kframe.h.
	printf 'tap 572 26\ntap 500 24\n' | ./tetris-host --no-takeover --seed 1 --dump /tmp/tetris-apps.pgm \
	    >/tmp/tetris-apps.out 2>/tmp/tetris-apps.err
	grep -q 'kframe: apps open' /tmp/tetris-apps.err
	rm -f /tmp/switch-app
	printf 'tap 572 26\ntap 500 24\ntap 300 212\n' | ./tetris-host --no-takeover --seed 1 \
	    >/tmp/tetris-switch.out 2>/tmp/tetris-switch.err
	grep -q 'kframe: switch chess' /tmp/tetris-switch.err
	grep -qx 'chess' /tmp/switch-app
	$(PY) tools/check_pgm.py /tmp/tetris-open.pgm /tmp/tetris-menu.pgm /tmp/tetris-apps.pgm

chess-kindle: src/chess.c src/kframe.c src/kframe.h src/kshell.c src/kshell.h $(ENGINE)/eink.c $(ENGINE)/eink.h
	docker run --rm -u "$$(id -u):$$(id -g)" \
	  -v "$$(pwd):/src" \
	  -v /home/brendan/jailbreak-kindle:/jb \
	  -w /src kfb-build \
	  sh -c 'arm-linux-musleabi-gcc -std=c11 -Os -Wall -Wextra -Werror \
	    -ffunction-sections -fdata-sections \
	    -Isrc -I/jb/kindle-tools/fbdaemon/engine \
	    -o chess src/chess.c src/kframe.c src/kshell.c /jb/kindle-tools/fbdaemon/engine/eink.c \
	    -static -no-pie -Wl,--gc-sections -lm \
	    && arm-linux-musleabi-strip chess'

# The shared engine's ek_full_refresh() contract (host only: it pins the region
# arithmetic and the one-shot behaviour, not what the glass does).
engine-test:
	$(HOSTCC) $(HOSTCFLAGS) -o engine-test tests/full_refresh_check.c $(ENGINE)/eink.c -lm
	./engine-test

# Rules, notation and the screen. No API needed: --fake-ai plays both sides.
chess-test: chess-host engine-test
	./chess-host --selftest --no-takeover
	./chess-host --fake-ai --auto 8 --no-takeover --no-wifi --dump /tmp/chess-mid.pgm
	printf 'tap 62 96\ntap 62 232\n' | ./chess-host --no-takeover --no-wifi --dump /tmp/chess-sel.pgm
	./chess-host --no-takeover --no-wifi --menu-shot /tmp/chess-menu.pgm
	# VOLUME centre is (368, 24). Tabs are 60px minimum so APPS fits. See kshell.h.
	printf 'tap 572 26\ntap 368 24\n' | ./chess-host --no-takeover --no-wifi --dump /tmp/chess-vol.pgm \
	    >/tmp/chess-vol.out 2>/tmp/chess-vol.err
	grep -q 'kframe: menu open' /tmp/chess-vol.err
	grep -q 'kframe: volume:' /tmp/chess-vol.err
	# QUIT is the far-right tab. Centre (564, 24). The second tap confirms.
	printf 'tap 572 26\ntap 564 24\ntap 564 24\n' | ./chess-host --no-takeover --no-wifi \
	    >/tmp/chess-quit.out 2>/tmp/chess-quit.err
	grep -q 'kframe: quit armed' /tmp/chess-quit.err
	grep -q 'kframe: quit confirmed' /tmp/chess-quit.err
	# APPS centre (500, 24). From chess, tetris is row 1. Centre (300, 212).
	printf 'tap 572 26\ntap 500 24\n' | ./chess-host --no-takeover --no-wifi --dump /tmp/chess-apps.pgm \
	    >/tmp/chess-apps.out 2>/tmp/chess-apps.err
	grep -q 'kframe: apps open' /tmp/chess-apps.err
	rm -f /tmp/switch-app
	printf 'tap 572 26\ntap 500 24\ntap 300 212\n' | ./chess-host --no-takeover --no-wifi \
	    >/tmp/chess-switch.out 2>/tmp/chess-switch.err
	grep -q 'kframe: switch tetris' /tmp/chess-switch.err
	grep -qx 'tetris' /tmp/switch-app
	$(PY) tools/check_pgm.py /tmp/chess-mid.pgm /tmp/chess-sel.pgm /tmp/chess-menu.pgm /tmp/chess-vol.pgm /tmp/chess-apps.pgm
	# CLOCK is the third app tab, centre (164, 24). CHANGE is (300, 236).
	printf 'tap 572 26\ntap 164 24\ntap 300 236\n' | ./chess-host --no-takeover --no-wifi --conf /tmp/chess-clock-test.conf \
	    >/tmp/chess-timer.out 2>/tmp/chess-timer.err
	grep -q 'kframe: app item 3' /tmp/chess-timer.err
	printf 'clock = 60\n' > /tmp/chess-clock-test.conf
	printf 'tap 572 26\ntap 164 24\n' | ./chess-host --no-takeover --no-wifi --conf /tmp/chess-clock-test.conf --dump /tmp/chess-menu-timer.pgm
	$(PY) tools/check_pgm.py /tmp/chess-menu.pgm /tmp/chess-menu-timer.pgm
	# After a move, BOTH squares are cleaned, each as its own box that bleeds
	# into the grid line: one box spanning them left a line down the middle of
	# every square it crossed (the "erroneous lines" on glass).
	./chess-host --fake-ai --auto 1 --no-takeover --no-wifi --trace-refresh \
	    >/tmp/chess-refresh.out 2>/dev/null
	test "$$(grep -c 'clean box:' /tmp/chess-refresh.out)" = 2
	grep -q 'refresh: rects=2' /tmp/chess-refresh.out


# The wi-fi screens the user actually taps: the keyboard has to be able to type
# a real password, and the reason a join failed has to reach the glass. Runs on
# this PC against a stand-in wifi script (see tests/wifi-screen-test.sh).
wifi-test:
	sh tests/wifi-test.sh
	sh tests/wifi-screen-test.sh

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

echo-probe: tools/echo-probe.c
	docker run --rm -u "$$(id -u):$$(id -g)" \
	  -v "$$(pwd):/src" -w /src kfb-build sh -c \
	  'arm-linux-musleabi-gcc -std=c11 -Os -Wall -Wextra -Werror \
	    -o echo-probe tools/echo-probe.c -static -no-pie \
	    && arm-linux-musleabi-strip echo-probe'

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

# ---- pvz: five-lane plant defence. Sprites baked from the webp art. ----

pvz-host: src/pvz.c src/pvz_sprites.h $(ENGINE)/eink.c $(ENGINE)/eink.h
	$(HOSTCC) $(HOSTCFLAGS) -o $@ src/pvz.c $(ENGINE)/eink.c -lm

pvz-kindle: src/pvz.c src/pvz_sprites.h $(ENGINE)/eink.c $(ENGINE)/eink.h
	docker run --rm -u "$$(id -u):$$(id -g)" \
	  -v "$$(pwd):/src" \
	  -v /home/brendan/jailbreak-kindle:/jb \
	  -w /src kfb-build \
	  sh -c 'arm-linux-musleabi-gcc -std=c11 -Os -Wall -Wextra -Werror \
	    -ffunction-sections -fdata-sections \
	    -Isrc -I/jb/kindle-tools/fbdaemon/engine \
	    -o pvz src/pvz.c /jb/kindle-tools/fbdaemon/engine/eink.c \
	    -static -no-pie -Wl,--gc-sections -lm \
	    && arm-linux-musleabi-strip pvz'

pvz: pvz-kindle

src/pvz_sprites.h: tools/bake_pvz_sprites.py pvz_sprite_sources.json pvz-art/*.webp
	$(PY) tools/bake_pvz_sprites.py

# Rules first, then the picture: the opening, a planting tap, and a wave or
# two played by --auto. The dumps must differ from each other.
pvz-test: pvz-host
	./pvz-host --selftest --no-takeover --seed 1
	./pvz-host --no-takeover --seed 1 --dump /tmp/pvz-open.pgm
	printf 'tap 131 297\ntap 260 120\n' | ./pvz-host --no-takeover --seed 1 --dump /tmp/pvz-plant.pgm
	./pvz-host --auto 120 --no-takeover --seed 1 --dump /tmp/pvz-play.pgm
	$(PY) tools/check_pgm.py /tmp/pvz-open.pgm /tmp/pvz-plant.pgm /tmp/pvz-play.pgm

calib-test: calib-host
	./calib-host --selftest --no-takeover --dump /tmp/calib-clock.pgm
	./calib-host --no-takeover --screen 2 --dump /tmp/calib-ghost.pgm < /dev/null
	./calib-host --no-takeover --screen 3 --dump /tmp/calib-taplag.pgm < /dev/null
	./calib-host --no-takeover --screen 4 --dump /tmp/calib-rx.pgm < /dev/null
	./calib-host --no-takeover --screen 4 --probe --speed 40 --steps 2 > /tmp/calib-rx.log 2>&1
	grep -q 'rx grey wfm=2' /tmp/calib-rx.log
	grep -q 'rx ink wfm=1' /tmp/calib-rx.log
	$(PY) tools/check_pgm.py /tmp/calib-clock.pgm /tmp/calib-ghost.pgm /tmp/calib-taplag.pgm /tmp/calib-rx.pgm
	$(PY) -c 'import sys;d=open("/tmp/calib-clock.pgm","rb").read();p=b"P5\n600 800\n255\n";assert d.startswith(p);pix=d[len(p):];assert len(pix)==600*800;print("calib: clock/ghost/taplag/rx pgm ok")'
	# Every screen must be reachable by tapping NEXT. The tuner (1) draws its
	# own knob rows instead of the shared button strip and shipped with no
	# NEXT at all, so the chess screen could not be opened on the glass.
	# Chain: 1 --tap NEXT--> 2 --NEXT--> 3 --NEXT--> 5 (chess).
	printf 'tap 120 529\ntap 500 675\ntap 500 675\n' | ./calib-host --no-takeover --screen 1 --host-stdin --steps 40 --dump /tmp/calib-chain.pgm > /dev/null
	./calib-host --no-takeover --screen 5 --dump /tmp/calib-chess.pgm < /dev/null
	$(PY) tools/check_calib_chain.py /tmp/calib-chain.pgm /tmp/calib-chess.pgm


# --- BrogueCE for the Kindle Touch ----------------------------------------

BROGUE_DIR := brogue
_brogue_src := $(wildcard $(BROGUE_DIR)/src/brogue/*.c) \
               $(BROGUE_DIR)/src/variants/GlobalsBrogue.c \
               $(BROGUE_DIR)/src/variants/GlobalsRapidBrogue.c \
               $(BROGUE_DIR)/src/variants/GlobalsBulletBrogue.c \
               $(BROGUE_DIR)/src/platform/kfb-platform.c \
               $(BROGUE_DIR)/src/platform/kfb-device.c \
               $(BROGUE_DIR)/src/platform/platformdependent.c \
               $(BROGUE_DIR)/src/platform/null-platform.c \
               $(BROGUE_DIR)/src/platform/main.c \
               $(BROGUE_DIR)/external/cJSON/cJSON.c
_brogue_obj := $(_brogue_src:%.c=build/brogue/%.o)
BROGUE_FLAGS := -std=c99 -O2 -Wno-parentheses -Wno-unused-result \
                -Wno-format -Wno-format-security -Wno-format-overflow \
                -I$(BROGUE_DIR)/src/brogue -I$(BROGUE_DIR)/src/platform \
                -I$(BROGUE_DIR)/src/variants -I$(ENGINE) \
                -DDATADIR='"$(CURDIR)/$(BROGUE_DIR)/compiled"'

brogue-host: $(_brogue_obj)
	$(HOSTCC) $(BROGUE_FLAGS) -o $@ $^ $(ENGINE)/eink.c -lm

build/brogue/%.o: %.c
	@mkdir -p $(dir $@)
	$(HOSTCC) $(BROGUE_FLAGS) -DBROGUE_KFB -DEK_HOST \
	  -DBROGUE_EXTRA_VERSION='"-kfb"' -c $< -o $@

brogue-kindle:
	docker run --rm -u "$$(id -u):$$(id -g)" \
	  -v "$$(pwd):/src" \
	  -v /home/brendan/jailbreak-kindle:/jb \
	  -w /src kfb-build \
	  sh -c 'set -e; mkdir -p build/brogue-kindle; \
	    cd brogue; \
	    for f in src/brogue/*.c src/variants/GlobalsBrogue.c src/variants/GlobalsRapidBrogue.c src/variants/GlobalsBulletBrogue.c external/cJSON/cJSON.c; do \
	      arm-linux-musleabi-gcc -std=c99 -Os -ffunction-sections -fdata-sections \
	        -Wno-parentheses -Wno-unused-result -Wno-format -Wno-format-security -Wno-format-overflow \
	        -Isrc/brogue -Isrc/platform -Isrc/variants -I/jb/kindle-tools/fbdaemon/engine \
	        -DBROGUE_KFB -DBROGUE_EXTRA_VERSION=\"-kfb\" -DDATADIR=\"/mnt/us/brogue\" \
	        -c $$f -o ../build/brogue-kindle/$$(basename $$f .c).o; \
	    done; \
	    arm-linux-musleabi-gcc -std=c99 -Os -ffunction-sections -fdata-sections \
	      -Wno-parentheses -Wno-unused-result -Wno-format -Wno-format-security -Wno-format-overflow \
	      -Isrc/brogue -Isrc/platform -Isrc/variants -I/jb/kindle-tools/fbdaemon/engine \
	      -DBROGUE_KFB -DBROGUE_EXTRA_VERSION=\"-kfb\" -DDATADIR=\"/mnt/us/brogue\" \
	      -o ../brogue-kindle-app ../build/brogue-kindle/*.o src/platform/kfb-platform.c src/platform/kfb-device.c src/platform/null-platform.c src/platform/platformdependent.c src/platform/main.c \
	      /jb/kindle-tools/fbdaemon/engine/eink.c \
	      -static -no-pie -Wl,--gc-sections -lm \
	      && arm-linux-musleabi-strip ../brogue-kindle-app'

brogue: brogue-kindle

brogue-kindle-app: brogue-kindle
	@test -f brogue-kindle-app && echo "brogue: device binary ready: brogue-kindle-app"

# Same binary with its symbols kept, for when the game dies on the device and
# the only evidence is "Segmentation fault". Pushed to /mnt/us/brogue-debug and
# run through tools/brogue-diag.sh --debug, which reads the fault address out of
# the log and hands it to addr2line here. Costs a few hundred KB on /mnt/us and
# changes no behaviour, so it is a separate target rather than a flag on the
# shipping build.
brogue-kindle-debug:
	docker run --rm -u "$$(id -u):$$(id -g)" \
	  -v "$$(pwd):/src" \
	  -v /home/brendan/jailbreak-kindle:/jb \
	  -w /src kfb-build \
	  sh -c 'set -e; mkdir -p build/brogue-kindle-debug; \
	    cd brogue; \
	    for f in src/brogue/*.c src/variants/GlobalsBrogue.c src/variants/GlobalsRapidBrogue.c src/variants/GlobalsBulletBrogue.c external/cJSON/cJSON.c; do \
	      arm-linux-musleabi-gcc -std=c99 -Os -g -ffunction-sections -fdata-sections \
	        -Wno-parentheses -Wno-unused-result -Wno-format -Wno-format-security -Wno-format-overflow \
	        -Isrc/brogue -Isrc/platform -Isrc/variants -I/jb/kindle-tools/fbdaemon/engine \
	        -DBROGUE_KFB -DBROGUE_EXTRA_VERSION=\"-kfb\" -DDATADIR=\"/mnt/us/brogue\" \
	        -c $$f -o ../build/brogue-kindle-debug/$$(basename $$f .c).o; \
	    done; \
	    arm-linux-musleabi-gcc -std=c99 -Os -g -ffunction-sections -fdata-sections \
	      -Wno-parentheses -Wno-unused-result -Wno-format -Wno-format-security -Wno-format-overflow \
	      -Isrc/brogue -Isrc/platform -Isrc/variants -I/jb/kindle-tools/fbdaemon/engine \
	      -DBROGUE_KFB -DBROGUE_EXTRA_VERSION=\"-kfb\" -DDATADIR=\"/mnt/us/brogue\" \
	      -o ../brogue-kindle-app-debug ../build/brogue-kindle-debug/*.o src/platform/kfb-platform.c src/platform/kfb-device.c src/platform/null-platform.c src/platform/platformdependent.c src/platform/main.c \
	      /jb/kindle-tools/fbdaemon/engine/eink.c \
	      -static -no-pie -Wl,--gc-sections -lm'

brogue-debug: brogue-kindle-debug
	@test -f brogue-kindle-app-debug && echo "brogue: debug binary ready: brogue-kindle-app-debug"

brogue-test: brogue-host
	rm -rf /tmp/kfb-frames && mkdir -p /tmp/kfb-frames
	rm -f brogue_config.json brogue_config_backup.json
	-KFB_PREVIEW=/tmp/kfb-frames KFB_SCRIPT=' n h j l k k' timeout 20 ./brogue-host -s 7
	rm -f brogue_config.json brogue_config_backup.json
	$(PY) tools/check_brogue_preview.py

clean:
	rm -f xcom-host xcom grabtest chess-host chess tetris-host tetris sudoku-host sudoku \
	      rockman-host rockman pvz-host pvz calib-host calib brogue-host brogue-kindle-app

# --- SimCity POC (600x800 screen mock) -------------------------------------
poc: poc/screen_600x800_grey.png

poc/screen_600x800_grey.png: poc/render_poc.py poc/src_*.png | upstream/tiles.bmp
	python3 poc/render_poc.py

upstream/tiles.bmp:
	mkdir -p upstream
	curl -sL -o $@ "https://raw.githubusercontent.com/SimHacker/MicropolisCore/main/content/micropolis/tilesets/classic/tiles.bmp"
