---
area: boot-splash
title: Kindle boot picture
aliases: [splash, blanket, boot-screen]
updated: 2026-11-01
---

# Kindle boot picture

## Map
- `tools/make_boot_screens.py` — rebuilds all five art screens into `screens/` (invert dense ink art -> `make_splash`). Does not install.
- `tools/make_splash.py` — builds the 600x800 greyscale PNG. Does not install it.
- `/usr/share/blanket/bg_xsmall_defaultbg.png` on the Kindle — file the splash module opens.
- `/opt/amazon/low_level_screens/600x800/splash.png` — second copy of the same picture. `/var/local/blanket/low_level_screens/images/splash.png` is a symlink to it.
- `/var/local/blanket/low_level_screens/images/` — where the low-level renderer actually reads `reboot.png` / `repair.png` / `update_*.png` from. These were REAL files from the 2012 factory tree, NOT symlinks (unlike `splash.png`/`shipping_mode.png`/`critbatt.png` which symlink to `/opt/amazon`). The `/opt/amazon/.../reboot.png` twin must match the `/var/local` one.
- `/etc/upstart/x.conf` — starts `lxinit`, which loads the splash module.
- `/etc/upstart/splash.conf` — walks the progress bar 0→90 while the reader UI starts.
- `/usr/sbin/mntroot` — `rw` / `ro` remount of the root disk.

## Facts
- [ran 2026-11-01 ssh root@192.168.0.63] Kindle 5.3.7.3. Root is ext3 mounted `ro` (`/dev/mmcblk0p1`). `/var/local` is the writable ext3 (`mmcblk0p3`). No `/boot` directory. `blanket` was running (child of `lxinit` / the `x` job).
- [ran 2026-11-01 strings /usr/lib/blanket/splash.so] The splash module loads an asset named `defaultbg` with extension `png` via `cairo_image_surface_create_from_png`. On disk that is `/usr/share/blanket/splash/bg_xsmall_defaultbg.png` → `/usr/share/blanket/bg_xsmall_defaultbg.png` (600x800, 8-bit palette, the boy under the tree).
- [ran 2026-11-01 md5sum on device] `splash.png`, the `/var/local` symlink, and `/usr/share/blanket/langpicker/bg_xsmall_splash_with_bar.png` are one file (md5 `a5a2363f4bc3274cb5d4136f28705ce3`): same tree, 8-bit greyscale. `defaultbg` is a different file of the same drawing.
- [ran 2026-11-01 cat /etc/upstart/splash.conf] The bar is not in the PNG. `splash_progress` sends `bootSplashProgress` on `com.lab126.hal.splash`. `x.conf` has `LXINIT_REQUIRED_BLANKET_MODULES="splash"` and `lxinit blanket -t "splash screensaver langpicker"`.
- [verified 2026-11-01 src/chess.c] After the game takes the panel it runs `initctl stop x` and one full white flush. `killall blanket` is the repair-reboot path.
- [verified 2026-11-01 ssh .63 after reboot] Replacing BOTH files (`bg_xsmall_defaultbg.png` + `low_level_screens/600x800/splash.png`) with one greyscale png puts OUR picture on the glass at boot, and it survives the reboot. Stock files backed up to `/mnt/us/boot-splash-backup/`; restore by copying them back over the two paths. Firmware update would put the tree back.
- [ran 2026-11-01 /usr/share/early_progress/0.png] A 120x120 spinner. `early_progress.conf` plays it at x=319 y=823, which is just past the 800px glass. Not the full-screen tree.
- [installed 2026-11-01 .63] The restart frame after the "device restarting" dialog was STILL stock even though `/opt/amazon/.../reboot.png` had been replaced — because the low-level renderer reads `/var/local/blanket/low_level_screens/images/reboot.png` (real 2012 file, 8807B), separate from the `/opt` twin. Copied `restart-low.png` over the `/var/local` file; both paths now md5 `445a2836…`. Stock backed up at `/mnt/us/boot-splash-backup/var-local-low-level/reboot.png`. The other `/var/local` files (repair 9148B, update_initial/success/failure, all 2012) are still stock and NOT installed.

## Built
- `python3 tools/make_splash.py --logo LOGO.png --out kindle-splash.png` writes the greyscale PNG (600x800, 8-bit, md5 `4a669252…`).
- Installed on `.63` over BOTH tree files, `mntroot rw` round-trip, rebooted, boot-shot captured → our picture confirmed on glass. Backups in `/mnt/us/boot-splash-backup/`. One-off boot-capture upstart job (`/etc/upstart/bootcap.conf`) and `/mnt/us/bootshots*` captures deleted after verification.
- Five 600x800 8-bit greyscale screens built 2026-11-01 by `tools/make_boot_screens.py` (each verified colour type 0, content present): `screens/boot.png` (fouloud art), `restart-low.png` (danirolli art → `reboot.png`), `restart-shutdown.png` (langdd art → blanket `shutdown/bg_xsmall_reboot.png`), `repair-update.png` (bilbilsu art → `repair.png` / update trio), `misc.png` (melira art → critbatt/shipping_mode/cust_service/langpicker/usb/ota). Rebuild is byte-identical (md5-checked). NOT yet installed on a device — `.63` was offline ("No route to host") at commit time.
- `mntroot` is NOT on PATH over non-interactive ssh — use the full path `/usr/sbin/mntroot rw|ro`.

## Reboot text ("Kindle starts up")
- The "Please wait a moment while your Kindle starts up." dialog text is the gettext msgstr `shutdown.reboot.message` in `/usr/share/locale/en/LC_MESSAGES/blanket.mo` (+ `en_GB`), read-only root, not the images.
- `tools/patch_blanket_mo.py` swaps one msgstr while keeping every other byte intact (rebuilds header/index/hash/pools; original pools are NUL-separated, entries are len+1 bytes). Verify pass is inside the tool: msgids identical, only target msgstr changed, strict byte-diff outside the window.
- [installed 2026-11-01 .63] patched `en` catalog over the stock one (md5 8c3a08ce… vs c3b94fe7…), backup at `/mnt/us/boot-splash-backup/locale/blanket-en.mo`, root back `ro`. `en_GB` left stock. Change shows at next reboot.
- Keys must be bytes in the replacements dict (`{msgid.encode(): to}`), or the lookup silently no-ops.

## Decisions
- Replace both tree files, not one. The splash module names `defaultbg`. An older note said a reboot changed after `splash.png` was swapped. This session did not reboot, so both copies stay in the install step.

## Dead ends
- Writing a real file over the `/var/local/blanket/.../splash.png` symlink does not change the file `splash.so` opens. That symlink is not the splash module's path.
- `killall blanket` while `lxinit` is up: lxinit exits, `x_monitor` restarts the UI, then the device reboots onto `repair.png`. Use `initctl stop x` or do not touch it.

## Screensaver slideshow (sleep pictures)
- The sleep screensaver is 20 pictures on the read-only root: `/usr/share/blanket/screensaver/bg_xsmall_ss00..19.png` (600x800, 8-bit palette originally; greyscale works). `mntroot rw` to write, `mntroot ro` after. Stock `ss` files backed up at `/mnt/us/boot-splash-backup/screensaver/` — but the busybox `cp` on this Kindle has NO `-n`, so the `cp -n` backup line silently copied nothing before the overwrite. Originals are not restorable from that dir; a firmware image would restore them.
- Install path that worked: `tar -C screens/ss -cf - -- * | ssh root@KINDLE 'cd /tmp && tar -xf -'` (scp is broken — sftp-server missing), then `mntroot rw; for i in $(seq -w 0 19); do cp /tmp/ss$i.png /usr/share/blanket/screensaver/bg_xsmall_ss$i.png; done; sync; mntroot ro`. Verified: md5 of on-device `bg_xsmall_ss0{0,5,19}.png` == local.
- The framework (`/etc/upstart/framework_setup.conf`) names `com.lab126.hal.screensaver`; slideshow order/selection was not inspected (stock files already replaced).

## Built
- [2026-11-01] `tools/make_screensaver_screens.py` builds all 20 from the five newest AI art PNGs (4 zoom/crop variants each: full/centre, zoom-top, zoom-bottom, deep-zoom) into `screens/ss/ss00..ss19.png` (600x800 8-bit greyscale, inverted-to-white-etching like the boot screens). All 20 distinct (md5 of 60x80 resized hash — no duplicate groups). Installed on `.63` over all 20 `ss` files, md5-verified on device, rootfs back `ro`. Contact sheet: `ss-preview.png`.

## Do not redo
- SSH to `.63` and `strings` on `splash.so` / `libblanket.so`. Asset name is `defaultbg`. Low-level dir strings are `/opt/amazon/low_level_screens/%dx%d` and `/var/local/blanket/low_level_screens/images`.

## Open
- Greyscale png over the palette `defaultbg` is confirmed on device.
- Taking `splash` out of `/etc/upstart/x.conf` (the way to drop the poster and the bar) is not tried.
