---
area: reader-removal
title: Removing the Amazon books app / reader framework
aliases: [books-app, reader, framework, cvm, books-removal, lab126_gui]
updated: 2026-11-03
---

# Removing the Amazon "books" app (the reader framework)

Goal: can we strip the Kindle's built-in book/reader UI entirely and boot
straight into the game menu? Short answer: **yes, and it is already 90%
removed by our boot job.** The "books app" is not a single binary or a kernel
module — it is a layered user-space boot chain with the Java VM as the heavy
part. The kernel does NOT run it and does not need it.

## What the "books app" actually is

There is NO `/usr/bin/cvm` "book app." There is a Java VM `/usr/java/bin/cvm`
(an 8.5 MB ARM binary, the J2ME "super-C" VM). The reader/books UI is a Java
OSGi framework stack (Apache Concierge, `ch.ethz.iks.concierge.framework.Framework`)
launched by the upstart `framework` job. All the real book features (home,
library, store, settings) are Java `.jar` bundles under `/opt/amazon/ebook/`.
`/usr/bin/webreader` (370 KB) is a separate small native daemon for the
"experimental browser", started by its own `webreader.conf` job.

## The boot chain to the books UI

Kernel loads → `/sbin/init` (upstart) → jobs fire by emitted events:

```
kernel (/dev/mmcblk0, Linux 2.6.31-rt11-lab126)
  -> init
  -> x.conf   "start on started dbus" -> lxinit blanket (Xorg + awesome + blanket)
  -> langpicker_ready  (blanket emits it)
  -> lab126_gui.conf  "start on langpicker_ready"  (the UI controller job)
        -> lab126_gui script calls /etc/upstart/framework
        -> framework.conf  "start on started lab126_gui"
              -> /etc/upstart/framework script  -> /usr/java/bin/cvm java app
        -> webreader.conf  "start on started lab126_gui"  -> /usr/bin/webreader
```

So the books app's *only* trigger is the upstart `framework` job, which starts
only `started lab126_gui`. Kill/disable `framework` and the books UI never
comes up — while Xorg/awesome/blanket (the display server + window manager)
stay, which is fine (games use the framebuffer directly, not X).

## Kernel involvement — essentially none

- Kernel cmdline (live): `root=/dev/mmcblk0p1 ro quiet eink=fslepdc
  video=mxcepdcfb:E60,bpp=8,x_mem=2M ...` — no UI, no book, no VM.
- Root is ext3 on `/dev/mmcblk0p1`, read-only. The books Java stack lives in
  `/opt/amazon/ebook/` and `/usr/java/` on that read-only root.
- `/proc/bd/*` files (framework_running/stopped/started, boot_milestone, etc.)
  look like "kernel" but are a user-space module probing the kernel; they are
  NOT load-bearing for the UI. `framework.conf` writes
  `0/1 > /proc/bd/framework_*` purely as milestone flags.
- The only hard kernel link to our setup is the **boot logo gzipped inside the
  kernel image** (see `areas/boot-splash.md`) — that is the frame BEFORE any
  UI and is unrelated to the books app.

## How we already disable it (the working mechanism)

- `/mnt/us/menu-boot.sh` runs the launcher and stops `powerd` and the mirror.
- `/etc/upstart/menu.conf` starts `on started lab126_gui`, so it waits for the
  reader's controller then takes the panel — the Amazon UI never shows beyond
  the first frames.
- **`/mnt/us/DONT_START_FRAMEWORK`** (empty marker file, currently present on
  `.63`): `framework.conf`'s pre-start checks `if [ -f /mnt/us/DONT_START_FRAMEWORK ]`
  and `stop`s. This single file is the clean "kill the books app" switch —
  with it present, `cvm` never launches (verified live: `cvm NOT running`,
  `framework stop/waiting`, `DONT_START_FRAMEWORK` exists).
- The Kindle currently boots into the launcher menu and `cvm` is NOT running.

## To "completely remove" the books app — the options, easiest first

1. **Keep the `DONT_START_FRAMEWORK` marker.** Already the effect of "no books
   app", zero risk, reversible. Do not actually delete the Amazon files.
2. **Disable the jobs** so nothing even tries: remove `framework.conf` /
   `import` it off or add a stop guard. Or make `lab126_gui.conf` not start
   the framework. `mntroot rw` then edit under `/etc/upstart/`.
3. **Free disk/boot time** by deleting the jars — NOT recommended: it is on
   the read-only root, a firmware update repaints it, and it buys almost
   nothing (the VMs just don't run). The app is already not running.

## What to NOT do / gotchas

- Do NOT touch `/usr/java/bin/cvm`, `/opt/amazon/ebook/`, or `/usr/bin/webreader`
  — they are inert once DONT_START_FRAMEWORK is in place, and they live on the
  read-only root that needs `mntroot rw` to change.
- Do NOT kill Xorg/awesome/blanket thinking they are the books app — they are
  the display/window system, and the games bypass them anyway but other
  low-level screens (langpicker, splash, repair) rely on them.
- `killall blanket` while `lxinit` is up causes a reboot to `repair.png` —
  the books app is NOT blanket (see `areas/boot-splash.md`).
- The `DONT_START_FRAMEWORK` file name is hard-coded in `framework.conf`
  pre-start; both `restore-kindle.sh` and `kindle-run.sh` `rm -f` it so the
  reader can be brought back when wanted (the KINDLE button / game exit).

## Live facts (2026-11-03, `.63` online)

- Kernel 2.6.31-rt11-lab126, firmware 5.3.7.3. Device boots to the menu
  launcher. `cvm NOT running`, `framework stop/waiting` because
  `DONT_START_FRAMEWORK` exists. `Xorg`/`awesome`/`blanket`/`lxinit` running.
- `webreader` job exists but the process was seen (`webreader start/running,
  process 1632`) — it is independent of `cvm` and low overhead; can be left.
- BusyBox on this Kindle: no `grep -E` (use `grep -ie "a" -e "b"`), no `base64`,
  no `scp`, no `/usr/sbin/mntroot` on PATH (use full path), `ps w` is unreliable
  (read `/proc/*/cmdline` instead).

## Open
- Decide whether "remove" means (a) keep it disabled via the marker (current),
  or (b) actually strip the jobs/files off the read-only root. (a) is the safe,
  already-working answer.