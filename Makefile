# XCOM for the Kindle Touch. The e-ink engine stays in jailbreak-kindle.
# This Makefile only compiles it in. It does not change that tree.

ENGINE ?= jailbreak-kindle/kindle-tools/fbdaemon/engine
HOSTCC ?= gcc
PY     ?= python3

HOSTCFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -DEK_HOST -Isrc -I$(ENGINE)

.PHONY: all test kindle preview clean

all: xcom-host

src/sprites.h: tools/slice_sprites.py spritesheet.png tactical_sprite_map_with_descriptions.json
	$(PY) tools/slice_sprites.py

xcom-host: src/xcom.c src/sprites.h $(ENGINE)/eink.c $(ENGINE)/eink.h $(ENGINE)/font8x16.h
	$(HOSTCC) $(HOSTCFLAGS) -o $@ src/xcom.c $(ENGINE)/eink.c -lm

# Static musl ARM, same shape as ekdemo. The engine lives outside this
# folder, so the container gets its own mount at /jb.
kindle: src/xcom.c src/sprites.h
	docker run --rm -u "$$(id -u):$$(id -g)" \
	  -v "$$(pwd):/src" \
	  -v /home/brendan/jailbreak-kindle:/jb \
	  -w /src kfb-build \
	  sh -c 'arm-linux-musleabi-gcc -std=c11 -Os -Wall -Wextra -Werror \
	    -ffunction-sections -fdata-sections \
	    -Isrc -I/jb/kindle-tools/fbdaemon/engine \
	    -o xcom src/xcom.c /jb/kindle-tools/fbdaemon/engine/eink.c \
	    -static -no-pie -Wl,--gc-sections -lm \
	    && arm-linux-musleabi-strip xcom'

test: xcom-host
	./xcom-host --selftest --no-takeover --seed 1
	./xcom-host --no-takeover --seed 1 --dump /tmp/xcom-open.pgm
	printf 'tap 120 536\n' | ./xcom-host --no-takeover --seed 1 --dump /tmp/xcom-sel.pgm
	./xcom-host --auto 2 --no-takeover --seed 1 --dump /tmp/xcom-mid.pgm
	$(PY) tools/check_pgm.py /tmp/xcom-open.pgm /tmp/xcom-sel.pgm /tmp/xcom-mid.pgm

preview: src/sprites.h
	$(PY) tools/slice_sprites.py --preview /tmp/xcom-sprites.png

clean:
	rm -f xcom-host xcom
