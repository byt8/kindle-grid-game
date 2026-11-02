# PvZ: five-lane plant defence on the Kindle

One file `src/pvz.c` (engine API in `jailbreak-kindle/kindle-tools/fbdaemon/engine`).
Art: `pvz/*.webp` -> `tools/bake_pvz_sprites.py` -> `src/pvz_sprites.h` (generated,
do not hand-edit). 600x800 panel, 12x5 lawn of 40px tiles.

## How it plays

- Tap a seed card (8 plants), then a lawn tile, to plant if you have the sun.
- Sun falls from the sky and sunflowers make it; tap it to collect.
- Peashooter / Repeater / Snow Pea shoot right at the nearest zombie in their lane.
- Wall-nut / Tall-nut block; Cherry Bomb (3 tiles) and Potato Mine (1 tile) blast.
- Zombies walk left, eat plants, reach the house: -1 brain. 5 brains = lose.
- 5 waves; clearing the last wave wins.

## Verified by

`make pvz-test`: `--selftest` covers planting, costs, planting on an occupied tile,
snow slow, zombies chewing plants, brain loss, cherry bomb + consumption, wave
clear win, five-brain lose; then three `--dump` pgms checked by `tools/check_pgm.py`.

## Known traps

- Hand-placed zombies in tests: `new_lawn()` memsets `g`, so a test zombie needs an
  explicit `speed` or it never walks.
- Dead-plant / dead-zombie sweep runs at the **end** of `step_game` (after combat),
  or cherry bombs re-explode and zombies resurrect.
- Wave logic re-spawns when the field is empty: isolated combat tests must
  `g.to_spawn = 0` or a fresh zombie walks in and pollutes the assertion.
- Bomb radius must exceed a tile; `r = 3*T` for Cherry Bomb.
- `--selftest` uses seed 1 only; seeds pass any seed through.

## Runner

`run-pvz-on-kindle.sh [ip]` — copy of the tetris runner, pushes `./pvz` + `grabtest`
+ `tools/kindle-run.sh`, runs with `GAME=pvz sh /mnt/us/pvz-run.sh --detach`.
