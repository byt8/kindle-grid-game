---
area: boot-splash
title: Kindle boot picture
aliases: [splash, blanket, boot-screen]
updated: 2026-11-01
---

# Kindle boot picture

## Map
- `tools/make_splash.py` — builds the 600x800 greyscale PNG. Does not install it.
- `/usr/share/blanket/bg_xsmall_defaultbg.png` on the Kindle — file the splash module opens.
- `/opt/amazon/low_level_screens/600x800/splash.png` — second copy of the same picture. `/var/local/blanket/low_level_screens/images/splash.png` is a symlink to it.
- `/etc/upstart/x.conf` — starts `lxinit`, which loads the splash module.
- `/etc/upstart/splash.conf` — walks the progress bar 0→90 while the reader UI starts.
- `/usr/sbin/mntroot` — `rw` / `ro` remount of the root disk.

## Facts
- [ran 2026-11-01 ssh root@192.168.0.63] Kindle 5.3.7.3. Root is ext3 mounted `ro` (`/dev/mmcblk0p1`). `/var/local` is the writable ext3 (`mmcblk0p3`). No `/boot` directory. `blanket` was running (child of `lxinit` / the `x` job).
- [ran 2026-11-01 strings /usr/lib/blanket/splash.so] The splash module loads an asset named `defaultbg` with extension `png` via `cairo_image_surface_create_from_png`. On disk that is `/usr/share/blanket/splash/bg_xsmall_defaultbg.png` → `/usr/share/blanket/bg_xsmall_defaultbg.png` (600x800, 8-bit palette, the boy under the tree).
- [ran 2026-11-01 md5sum on device] `splash.png`, the `/var/local` symlink, and `/usr/share/blanket/langpicker/bg_xsmall_splash_with_bar.png` are one file (md5 `a5a2363f4bc3274cb5d4136f28705ce3`): same tree, 8-bit greyscale. `defaultbg` is a different file of the same drawing.
- [ran 2026-11-01 cat /etc/upstart/splash.conf] The bar is not in the PNG. `splash_progress` sends `bootSplashProgress` on `com.lab126.hal.splash`. `x.conf` has `LXINIT_REQUIRED_BLANKET_MODULES="splash"` and `lxinit blanket -t "splash screensaver langpicker"`.
- [verified 2026-11-01 src/chess.c] After the game takes the panel it runs `initctl stop x` and one full white flush. `killall blanket` is the repair-reboot path.
- [ran 2026-11-01 /usr/share/early_progress/0.png] A 120x120 spinner. `early_progress.conf` plays it at x=319 y=823, which is just past the 800px glass. Not the full-screen tree.

## Built
- `python3 tools/make_splash.py --logo LOGO.png --out kindle-splash.png` writes the greyscale PNG only. Nothing on `.63` was replaced this session; both tree files are still dated Jul 20 2015.

## Decisions
- Replace both tree files, not one. The splash module names `defaultbg`. An older note said a reboot changed after `splash.png` was swapped. This session did not reboot, so both copies stay in the install step.

## Dead ends
- Writing a real file over the `/var/local/blanket/.../splash.png` symlink does not change the file `splash.so` opens. That symlink is not the splash module's path.
- `killall blanket` while `lxinit` is up: lxinit exits, `x_monitor` restarts the UI, then the device reboots onto `repair.png`. Use `initctl stop x` or do not touch it.

## Do not redo
- SSH to `.63` and `strings` on `splash.so` / `libblanket.so`. Asset name is `defaultbg`. Low-level dir strings are `/opt/amazon/low_level_screens/%dx%d` and `/var/local/blanket/low_level_screens/images`.

## Open
- No reboot yet with a new PNG, so which single file changes the glass is not watched. Greyscale over the palette `defaultbg` is not tried on device.
- Taking `splash` out of `/etc/upstart/x.conf` (the way to drop the poster and the bar) is not tried.
