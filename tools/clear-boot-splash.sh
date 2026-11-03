#!/bin/sh
# clear-boot-splash.sh -- stop Amazon's boot progress bar from lingering.
#
# The bar is drawn by the `splash` module of blanket (the lab126 display
# layer), which the `x` job loads at boot (`LXINIT_REQUIRED_BLANKET_MODULES=
# "splash"`, `lxinit blanket -t "splash screensaver langpicker"`) and which
# `lab126_gui` then animates through splash.conf's 0..90 progress walk.
#
# On a stock boot the framework tears this down: framework_setup.conf runs
#   f_blanket_unload_module splash
#   lipc-send-event -r 5 com.lab126.hal bootSplashCleanup
# With the framework disabled (DONT_START_FRAMEWORK) that never happens, so
# the splash layer stays registered and blanket keeps painting the bar over
# whatever the menu draws. This script is that same cleanup, called from
# menu-boot.sh right before the menu takes the panel.
#
# It is safe to run more than once: blanketing a module that is not loaded is
# a no-op with rc=0 (verified live: load/unload/cleanup all rc=0).

# Splash is gone on a re-taken panel (a game's runner already did this); a
# failure here must not stop the boot -- the menu drawing a white frame over
# the bar is the fallback the main script already has.
lipc-set-prop com.lab126.blanket unload -s splash 2>&1 || exit 0
lipc-send-event -r 5 com.lab126.hal bootSplashCleanup 2>&1 || exit 0
exit 0
