#!/usr/bin/env bash
#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          ATI Rage 128 Pro -- build script for the capture replay
#          harness.
#
# Authors: skiretic.
#
#          Copyright 2026 skiretic.
#

# Build the Rage 128 offline capture replay harness.
#
# Compiles the device sources at ../../../../src and shares host_stubs.c
# and include/ with ../reg-harness, so both use the same host services.
# Real pthread-backed 86Box thread primitives come from src/unix/unix_thread.c.
#
# -ffp-contract=off matches the production build flags for these files.
#
# -O2 rather than the register harness's -O1: a replay processes tens of
# millions of records and the difference is minutes.
set -euo pipefail
cd "$(dirname "$0")"
CC="${CC:-clang}"
# multi-word CC (e.g. CC='clang -arch x86_64'): the quoted "$CC" below must
# stay quoted, so route through a generated one-line wrapper script
case "$CC" in *" "*) CCW=$(mktemp); printf '#!/bin/sh\nexec %s "$@"\n' "$CC" >"$CCW"; chmod +x "$CCW"; CC="$CCW";; esac
SRC=../../../../src
STUBS=../reg-harness

VKINC=""
for d in "${VULKAN_SDK:-}/include" /opt/homebrew/include /usr/local/include \
         /usr/include; do
    if [ -f "$d/vulkan/vulkan.h" ]; then
        VKINC="-DR128_GPU_HAVE_VULKAN -I $d"
        echo "vulkan headers: $d (GPU backend enabled)"
        break
    fi
done
[ -n "$VKINC" ] || echo "vulkan headers: none (GPU backend stubbed)"

"$CC" -O2 -g -ffp-contract=off -fno-common -Wall -Wno-unused-function \
    -I "$STUBS/include" -I "$SRC/include" -I "$SRC/video" -I "$SRC/cpu" -I "$SRC" \
    -DREG_HARNESS -DENABLE_RAGE128_LOG=1 $VKINC \
    -o replay \
    replay.c \
    "$STUBS/host_stubs.c" \
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
    "$SRC/video/vid_ati_rage128_gpu.c" \
    "$SRC/device/i2c.c" \
    "$SRC/device/i2c_gpio.c" \
    "$SRC/mem/i2c_eeprom.c" \
    "$SRC/video/vid_ddc.c" \
    "$SRC/unix/unix_thread.c" \
    -lpthread -lm
echo "built $(pwd)/replay"
