#!/usr/bin/env bash
#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          ATI Rage 128 Pro -- build script for the register harness.
#
# Authors: skiretic.
#
#          Copyright 2026 skiretic.
#

# Build the Rage 128 host-side register harness.
#
# Compiles the in-tree device sources directly (single source of truth, no
# vendored copies) plus host_stubs.c, which supplies only the 86Box host
# services the device calls (mem/io/pci/timer/rom/video plumbing). Real
# pthread-backed 86Box thread primitives come from src/unix/unix_thread.c.
#
# -ffp-contract=off matches the production build flags for these files.
#
# The GPU backend compiles in only when Vulkan headers are present -- the
# same gate CMake uses, and the same one the shipped binary is built under,
# so the harness exercises the configuration users actually run (backend
# present, dev->gpu null) rather than the header-less stub. gpu_probe.c
# includes that translation unit and exports the GPU-lane probes. Without
# headers it exports "unavailable" stubs and the GPU vectors report the hole.
set -euo pipefail
cd "$(dirname "$0")"
CC="${CC:-clang}"
# the toolchain prefix (<compiler dir>/../include) covers sysroots the fixed
# list below cannot name -- e.g. an MSYS2 ucrt64 gcc reached from Git Bash,
# where /usr/include is the MSYS runtime's, not the toolchain's
CCPREFIX=""
CCBIN=$(command -v "${CC%% *}" 2>/dev/null || true)
[ -n "$CCBIN" ] && CCPREFIX=$(cd "$(dirname "$CCBIN")/.." 2>/dev/null && pwd || true)
# multi-word CC (e.g. CC='clang -arch x86_64'): the quoted "$CC" below must
# stay quoted, so route through a generated one-line wrapper script
case "$CC" in *" "*) CCW=$(mktemp); printf '#!/bin/sh\nexec %s "$@"\n' "$CC" >"$CCW"; chmod +x "$CCW"; CC="$CCW";; esac
SRC=../../../../src

VKINC=""
for d in "${VULKAN_SDK:-}/include" "${CCPREFIX:-/nonexistent}/include" \
         /opt/homebrew/include /usr/local/include \
         /usr/include; do
    if [ -f "$d/vulkan/vulkan.h" ]; then
        VKINC="-DR128_GPU_HAVE_VULKAN -I $d"
        echo "vulkan headers: $d (GPU backend + probes enabled)"
        break
    fi
done
[ -n "$VKINC" ] || echo "vulkan headers: none (GPU backend stubbed, GPU vectors skipped)"

"$CC" -O1 -g -ffp-contract=off -fno-common -Wall -Wno-unused-function \
    -I include -I "$SRC/include" -I "$SRC/video" -I "$SRC/cpu" -I "$SRC" \
    -DREG_HARNESS -DENABLE_RAGE128_LOG=1 $VKINC \
    -o reg_harness \
    reg_harness.c \
    host_stubs.c \
    gpu_probe.c \
    "$SRC/video/vid_ati_rage128_vga.c" \
    "$SRC/video/vid_ati_rage128_display.c" \
    "$SRC/video/vid_ati_rage128_ov0.c" \
    "$SRC/video/vid_ati_rage128_2d.c" \
    "$SRC/video/vid_ati_rage128_pm4.c" \
    "$SRC/video/vid_ati_rage128_3d.c" \
    "$SRC/video/vid_ati_rage128_mpeg.c" \
    "$SRC/video/vid_ati_rage128_raster.c" \
    "$SRC/video/vid_ati_rage128_mem.c" \
    "$SRC/video/vid_ati_rage128_jit.c" \
    "$SRC/video/vid_ati_rage128_synctel.c" \
    "$SRC/video/vid_ati_rage128_census.c" \
    "$SRC/device/i2c.c" \
    "$SRC/device/i2c_gpio.c" \
    "$SRC/mem/i2c_eeprom.c" \
    "$SRC/video/vid_ddc.c" \
    "$SRC/unix/unix_thread.c" \
    -lpthread -lm
echo "built $(pwd)/reg_harness"
