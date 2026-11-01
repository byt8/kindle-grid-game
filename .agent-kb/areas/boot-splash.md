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
- Leave the kernel blobs alone until a same-size-or-smaller gzip is ready and the uImage header CRC and data CRC are both recomputed. A longer blob overwrites the kernel. Two copies must stay in agreement or a bad main kernel drops the device into the second image.

## Dead ends
- Writing a real file over the `/var/local/blanket/.../splash.png` symlink does not change the file `splash.so` opens. That symlink is not the splash module's path.
- `killall blanket` while `lxinit` is up: lxinit exits, `x_monitor` restarts the UI, then the device reboots onto `repair.png`. Use `initctl stop x` or do not touch it.

## Screensaver slideshow (sleep pictures)
- The sleep screensaver is 20 pictures on the read-only root: `/usr/share/blanket/screensaver/bg_xsmall_ss00..19.png` (600x800, 8-bit palette originally; greyscale works). `mntroot rw` to write, `mntroot ro` after. Stock `ss` files backed up at `/mnt/us/boot-splash-backup/screensaver/` — but the busybox `cp` on this Kindle has NO `-n`, so the `cp -n` backup line silently copied nothing before the overwrite. Originals are not restorable from that dir; a firmware image would restore them.
- Install path that worked: `tar -C screens/ss -cf - -- * | ssh root@KINDLE 'cd /tmp && tar -xf -'` (scp is broken — sftp-server missing), then `mntroot rw; for i in $(seq -w 0 19); do cp /tmp/ss$i.png /usr/share/blanket/screensaver/bg_xsmall_ss$i.png; done; sync; mntroot ro`. Verified: md5 of on-device `bg_xsmall_ss0{0,5,19}.png` == local.
- The framework (`/etc/upstart/framework_setup.conf`) names `com.lab126.hal.screensaver`; slideshow order/selection was not inspected (stock files already replaced).

## Built
- [2026-11-01] `tools/make_screensaver_screens.py` builds all 20 from the five newest AI art PNGs (4 zoom/crop variants each: full/centre, zoom-top, zoom-bottom, deep-zoom) into `screens/ss/ss00..ss19.png` (600x800 8-bit greyscale, inverted-to-white-etching like the boot screens). All 20 distinct (md5 of 60x80 resized hash — no duplicate groups). Installed on `.63` over all 20 `ss` files, md5-verified on device, rootfs back `ro`. Contact sheet: `ss-preview.png`.

## OTA defaultbg (last stock copy)
- [installed 2026-11-01 .63] `/usr/share/blanket/ota/bg_xsmall_defaultbg.png` was the FINAL stock `bg_xsmall_defaultbg*` on the device: a 2382-byte 600x800 8-bit palette PNG (md5 `600a7b8a…`) that renders pure white (dark%=0) — a blank OTA background, NOT the tree. Replaced it with `screens/boot.png`; on-device md5 now `9dd7823b` (= all other defaultbg copies). Stock backed up at `/mnt/us/boot-splash-backup/ota/bg_xsmall_defaultbg.png`.
- All `bg_xsmall_defaultbg*` copies are now custom `9dd7823b`: root, `splash/`, `langpicker/` (symlink to root), `ota/`, plus low-level `splash.png` and langpicker `bg_xsmall_splash_with_bar.png`. Device sweep confirms no file with stock md5 `600a7b8a` remains under `/usr/share/blanket`.
- [ran 2026-11-01 dd /dev/mmcblk0 first 32MiB] The screen before the PNG boot art is baked into the kernel, not a file. Main uImage at disk `0x41000` (Linux-2.6.31-rt11-lab126, uncompressed, 5197856 bytes). Inside it, three gzip members of a 600x800 2-bit image (12-byte LE header width, height, 2, then 120000 bytes, 4 pixels/byte): plain tree logo at `0xC92C0` (2668 bytes), tree plus "Please wait a moment while your Kindle starts up." at `0xC9D30` (4660 bytes), repair page at `0xCAF68` (4088 bytes). Second kernel at `0xE41000` has the same logo and wait blobs (`0xEC7104` 2668 bytes, `0xEC7B74` 4660 bytes, byte-identical). Boot captures start too late to see this frame. PNG swaps and the blanket.mo string patch do not touch it.
- Shutdown `bg_xsmall_reboot.png` md5 is `86bdefa6` = `screens/restart-shutdown.png` (already custom).

## Do not redo
- SSH to `.63` and `strings` on `splash.so` / `libblanket.so`. Asset name is `defaultbg`. Low-level dir strings are `/opt/amazon/low_level_screens/%dx%d` and `/var/local/blanket/low_level_screens/images`.

## Kernel logo patch (the frame BEFORE the boot PNG)
- The kernel boots from `/dev/mmcblk0` raw: main uImage at `0x41000` (uncompressed, size at header), second at `0xE41000`. Each contains (at identical relative offsets) three little gzip members: plain logo `0xC92C0` (2672B), logo-with-text `0xC9D30` (4664B), repair page `0xCAF68` (4088B); second kernel `0xEC7104` / `0xEC7B74`. **Slot sizes are the gaps to the next gzip magic** (2672 / 4664 / 4088) — verified 2026-11-01 from a fresh 32MiB dd dump while the device was online.
- Payload format: 12-byte LE header `w,h,bpp=600,800,2`, then 120,000 bytes, **4 pixels/byte, MSB-first pairs, values 0=black, 3=white**. `struct.pack('<III',600,800,2)`.
- uImage header: 64B big-endian; `hcrc` at +4 (CRC32 over bytes 0..64 with that field zeroed), `dcrc` at +24 (CRC32 over the data after the header). Changing a blob inside the kernel means recomputing BOTH `dcrc` (data) and `hcrc` (header, since the header holds dcrc). Verified matches stock.
- The plain logo and the text logo are DIFFERENT pictures, not one picture plus text: diff 13,560 px spanning the full frame. The text one ("Please wait a moment…") is the one users see first.
- **Art fitting**: dense AI art does NOT fit even at 1-bit (10,111B full-bleed vs 2672/4664 slots). The fit that works: GaussianBlur(15) on the art, resize 320x427, centre on pure black (level 0, matches the art's own black corners) → gzip9 = **2470 bytes**, fits the 2672 plain slot, so it fits all 4 slots. 360x480=2780 (wait-only). Blur without black surround can't reach 2672. A white surround is wrong on this display (bordered).
- Built 2026-11-01 in `scratch/patch_kernel_build.py`: builds blob, checks stock md5s (plain `2b9f3a83`, wait `ebfb4f3f`, identical in both kernels), patches 4 slots + zero-pads only within each slot, recomputes all 4 CRCs, verifies streams end exactly at slot edges and the next members (repair page) stay intact. Outputs `scratch/mmcblk0-first32m-patched.bin` (32MiB, ready to dd) + `scratch/kernel-logo-rollback.json` (stock blobs hex + old CRCs for reversal). All local checks pass.
- **Not yet flashed** (device `.63` offline when the image was ready): `bash scratch/flash-kernel-logo.sh KINDLE_IP` re-dumps the device, checks the on-disk offsets are still stock, uploads with md5, writes only the two kernel windows + 4 header CRC words via dd conv=notrunc, re-dumps and byte-verifies, and does NOT auto-reboot. Rollback = write the stock blob hex from `kernel-logo-rollback.json` over the same offsets and restore the old CRCs.
- The blob lives in the raw disk, so no mntroot or firmware file to edit; a firmware update rewrites kernels.

## Open
- Flash the patched kernels when the Kindle is back online, then reboot and capture the first frame to confirm.
- Taking `splash` out of `/etc/upstart/x.conf` (the way to drop the later poster and the bar) is not tried. That would not remove the kernel frame.
