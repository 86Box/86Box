#!/usr/bin/env bash
#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          ATI Rage 128 Pro -- build script for the span JIT host
#          harness.
#
# Authors: skiretic.
#
#          Copyright 2026 skiretic.
#

# Build the Rage 128 ARM64 and x86-64 span-JIT host harness against the
# device and emitter headers in ../../../../src/include.
#
# The reference must round multiply and add separately, as the interpreter
# and emitted code do. -ffp-contract=off prevents fused operations in the
# reference from creating false mismatches.
set -euo pipefail
cd "$(dirname "$0")"
CC="${CC:-clang}"
# multi-word CC (e.g. CC='clang -arch x86_64'): the quoted "$CC" below must
# stay quoted, so route through a generated one-line wrapper script
case "$CC" in *" "*) CCW=$(mktemp); printf '#!/bin/sh\nexec %s "$@"\n' "$CC" >"$CCW"; chmod +x "$CCW"; CC="$CCW";; esac
"$CC" -O2 -ffp-contract=off -Wall -DENABLE_RAGE128_LOG=1 -I ../../../../src/include -o jit_host_test jit_host_test.c -lpthread -lm
echo "built $(pwd)/jit_host_test"
