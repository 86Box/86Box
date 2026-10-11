#!/usr/bin/env bash
#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          ATI Rage 128 Pro -- build script for the GPU backend test
#          tools.
#
# Authors: skiretic.
#
#          Copyright 2026 skiretic.
#

# Build gputri, fdivtest and ladfuzz, and compile the two test kernels the
# first two load at run time (f64test.spv, fdivtest.spv).
#
# gputri compiles in the production SPIR-V header from src/include/86box,
# so the kernels it checks are the bytes the emulator ships. Every C file
# here builds with -ffp-contract=off: the C reference must round each
# multiply and add on its own, as the interpreter and the kernels do.
#
# Vulkan headers and loader come from VULKAN_SDK when it is set, else from
# the compiler's own prefix, Homebrew, /usr/local or /usr; glslc from
# VULKAN_SDK/bin or the PATH. ladfuzz needs neither and always builds.
set -euo pipefail
cd "$(dirname "$0")"
CC="${CC:-cc}"
CCPREFIX=""
CCBIN=$(command -v "${CC%% *}" 2>/dev/null || true)
[ -n "$CCBIN" ] && CCPREFIX=$(cd "$(dirname "$CCBIN")/.." 2>/dev/null && pwd || true)
# multi-word CC (e.g. CC='clang -arch x86_64'): the quoted "$CC" below must
# stay quoted, so route through a generated one-line wrapper script
case "$CC" in *" "*) CCW=$(mktemp); printf '#!/bin/sh\nexec %s "$@"\n' "$CC" >"$CCW"; chmod +x "$CCW"; CC="$CCW";; esac
SRC=../../../../src
EXE=""
VKLIB=vulkan
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) EXE=.exe; VKLIB=vulkan-1;; esac

"$CC" -O2 -ffp-contract=off -Wall -o ladfuzz$EXE ladfuzz.c -lm
echo "built $(pwd)/ladfuzz$EXE"

VKDIR=""
for d in "${VULKAN_SDK:-}" "${CCPREFIX:-/nonexistent}" /opt/homebrew /usr/local /usr; do
    if [ -n "$d" ] && [ -f "$d/include/vulkan/vulkan.h" ]; then
        VKDIR=$d
        break
    fi
done
GLSLC=""
if [ -n "${VULKAN_SDK:-}" ] && [ -x "$VULKAN_SDK/bin/glslc" ]; then
    GLSLC=$VULKAN_SDK/bin/glslc
else
    GLSLC=$(command -v glslc 2>/dev/null || true)
fi
if [ -z "$VKDIR" ] || [ -z "$GLSLC" ]; then
    echo "Vulkan headers or glslc not found (set VULKAN_SDK): gputri and fdivtest not built" >&2
    exit 1
fi
echo "vulkan: $VKDIR, glslc: $GLSLC"

VKFLAGS="-I $VKDIR/include"
VKLINK="-L $VKDIR/lib -l$VKLIB"
# Homebrew and SDK installs keep the loader outside the default search path.
[ "$(uname -s)" = Darwin ] && VKLINK="$VKLINK -Wl,-rpath,$VKDIR/lib"

for k in f64test fdivtest; do
    "$GLSLC" -O --target-env=vulkan1.2 -I "$SRC/video" -o $k.spv $k.comp
done
# shellcheck disable=SC2086
"$CC" -O2 -ffp-contract=off -Wall $VKFLAGS -I "$SRC/include" -o gputri$EXE gputri.c $VKLINK -lpthread -lm
echo "built $(pwd)/gputri$EXE"
# shellcheck disable=SC2086
"$CC" -O2 -ffp-contract=off -Wall $VKFLAGS -o fdivtest$EXE fdivtest.c $VKLINK -lm
echo "built $(pwd)/fdivtest$EXE"
