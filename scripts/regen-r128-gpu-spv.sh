#!/bin/sh
# Regenerate the embedded SPIR-V header for the rage128 optional GPU
# raster backend: ONE module compiled from the kernel source; the
# V_TEX/V_LOD/V_BLEND/V_Z axes are specialization constants
# (constant_id 0..3), folded per pipeline at creation time from the
# r128_gpu_variants table below. The generated header is committed, so
# glslc is only needed when the kernel source changes; adding a variant
# is a table edit with zero SPIR-V change.
# Variant ids here are the kernel ids gpu_state_kernel() returns; keep
# the two tables in lockstep.
set -e
cd "$(dirname "$0")/.."

GLSLC=${GLSLC:-glslc}
# Different SPIRV-Tools releases optimize the modules differently, and
# glslc carries its own SPIRV-Tools and glslang (lad, rmw and ov0 are
# optimized by `glslc -O` alone), so both tool identities are recorded
# in the header: the three glslc --version lines and the standalone
# spirv-opt (SPIRV_TOOLS_DIR picks that install; default PATH). A regen
# with a different glslc or spirv-opt than the recorded ones refuses
# unless R128_SPV_NEW_TOOLS=1: a toolchain change ships different
# kernels and needs its own gates.
ST=${SPIRV_TOOLS_DIR:+$SPIRV_TOOLS_DIR/}
SPIRV_OPT=${ST}spirv-opt
SPIRV_AS=${ST}spirv-as
SPIRV_DIS=${ST}spirv-dis
SPIRV_VAL=${ST}spirv-val
SRC=src/video/vid_ati_rage128_gpu_seg.comp
LAD=src/video/vid_ati_rage128_gpu_lad.comp
RMW=src/video/vid_ati_rage128_gpu_rmw.comp
OV0=src/video/vid_ati_rage128_gpu_ov0.comp
OUT=src/include/86box/vid_ati_rage128_gpu_spv.h
# glslc prints "shaderc ...", "spirv-tools ...", "glslang ..." then a
# blank line and the target; the first three lines are its identity
GLSLC_VER=$($GLSLC --version | head -n 1)
GLSLC_ST_VER=$($GLSLC --version | sed -n '2p')
GLSLC_GL_VER=$($GLSLC --version | sed -n '3p')
# it prints on stderr; the trailing build timestamp ("..., 2026-07-22T20:34:54+00:00")
# is dropped so the banner carries the release and hash only (the comment
# checker reads a date as a staleness marker, and the release identifies
# the toolchain on its own)
OPT_VER=$($SPIRV_OPT --version 2>&1 | head -n 1 | sed 's/, [0-9][0-9-]*T[0-9:+-]*$//')
OLD_VER=$(sed -n 's/^   spirv-opt: //p' "$OUT" 2>/dev/null | head -n 1)
OLD_GLSLC=$(sed -n 's/^   glslc: //p' "$OUT" 2>/dev/null | head -n 1)
OLD_GLSLC_ST=$(sed -n 's/^   glslc spirv-tools: //p' "$OUT" 2>/dev/null | head -n 1)
OLD_GLSLC_GL=$(sed -n 's/^   glslc glslang: //p' "$OUT" 2>/dev/null | head -n 1)
if [ -n "$OLD_VER" ] && [ "$OLD_VER" != "$OPT_VER" ] && [ -z "$R128_SPV_NEW_TOOLS" ]; then
    echo "regen: $OUT was built with spirv-opt '$OLD_VER'," >&2
    echo "regen: this run would use '$OPT_VER' ($SPIRV_OPT)." >&2
    echo "regen: set SPIRV_TOOLS_DIR to the recorded install, or R128_SPV_NEW_TOOLS=1" >&2
    exit 1
fi
# a header from before the embedded-tool lines were recorded compares
# the glslc line alone; the first regen records the other two
if [ -n "$OLD_GLSLC" ] && [ -z "$R128_SPV_NEW_TOOLS" ] \
   && { [ "$OLD_GLSLC" != "$GLSLC_VER" ] \
        || { [ -n "$OLD_GLSLC_ST" ] && [ "$OLD_GLSLC_ST" != "$GLSLC_ST_VER" ]; } \
        || { [ -n "$OLD_GLSLC_GL" ] && [ "$OLD_GLSLC_GL" != "$GLSLC_GL_VER" ]; }; }; then
    echo "regen: $OUT was built with glslc '$OLD_GLSLC' / '$OLD_GLSLC_ST' / '$OLD_GLSLC_GL'," >&2
    echo "regen: this run would use '$GLSLC_VER' / '$GLSLC_ST_VER' / '$GLSLC_GL_VER' ($GLSLC)." >&2
    echo "regen: set GLSLC to the recorded build, or R128_SPV_NEW_TOOLS=1" >&2
    exit 1
fi
# explicit template: `mktemp -t prefix` is a BSD/macOS spelling that GNU
# coreutils (MSYS2 UCRT64, the supported Windows toolchain) rejects
TT=${TMPDIR:-/tmp}
TMP=$(mktemp "$TT/r128spv.XXXXXX")
TMPL=$(mktemp "$TT/r128lad.XXXXXX")
TMPR=$(mktemp "$TT/r128rmw.XXXXXX")
TMPO=$(mktemp "$TT/r128ov0.XXXXXX")

# id:name:tex:lod:blend:z
# Complete matrix: untextured is [blend][z] (lod is meaningless with no
# sampler), one- and two-stage are [lod][blend][z]. Ids 0-9 keep their
# historical values so a persisted r128gpu.tuples list stays valid.
VARIANTS="\
0:t1_mip:1:1:0:1
1:t1_mip_ab:1:1:1:1
2:t1_nolod:1:0:0:1
3:t1_nolod_ab_noz:1:0:1:0
4:gouraud_z:0:0:0:1
5:t2_mip:2:1:0:1
6:t1_nolod_ab_z:1:0:1:1
7:t1_mip_noz:1:1:0:0
8:t1_mip_ab_noz:1:1:1:0
9:t1_nolod_noz:1:0:0:0
10:gouraud_ab_z:0:0:1:1
11:gouraud_noz:0:0:0:0
12:gouraud_ab_noz:0:0:1:0
13:t2_mip_ab:2:1:1:1
14:t2_nolod:2:0:0:1
15:t2_nolod_ab_z:2:0:1:1
16:t2_mip_noz:2:1:0:0
17:t2_mip_ab_noz:2:1:1:0
18:t2_nolod_noz:2:0:0:0
19:t2_nolod_ab_noz:2:0:1:0"

# -O is safe: the kernel's `precise` qualifiers plus
# MVK_CONFIG_FAST_MATH_ENABLED=0 carry the bit-exactness contract
# (verified in the spike and by the gputri fuzz gate).
# The seg kernel compiles -O0 first so texel() survives as a real
# function, then DontInline pins it through spirv-opt -O -- both here
# and in the per-pipeline freeze, which registers the same pass set.
# One shared texel body instead of ~20 inline copies is a ~4x module
# shrink; texel is all-integer, so the outline cannot touch FP
# exactness, and backends that re-inline at codegen lose no runtime.
$GLSLC -O0 --target-env=vulkan1.2 -o "$TMP" "$SRC"
$SPIRV_DIS "$TMP" -o "$TMP.di.asm"
# R128_SPV_INLINE_TEXEL=1 skips the pin (bench A/B of the outlined call
# against a fully inlined texel; never commit a header built with it)
if [ -n "$R128_SPV_INLINE_TEXEL" ]; then
    sed 's/^\(%texel_[^ ]* = OpFunction %uint \)None /\1Inline /' \
        "$TMP.di.asm" > "$TMP.di2.asm"
    NDI=1
else
sed 's/^\(%texel_[^ ]* = OpFunction %uint \)None /\1DontInline /' \
    "$TMP.di.asm" > "$TMP.di2.asm"
# exactly one, not just at least one: 24 helpers return uint, and a
# rename that widened the match would silently outline the wrong body
NDI=$(grep -c ' OpFunction %uint DontInline ' "$TMP.di2.asm" || true)
fi
if [ "$NDI" != "1" ]; then
    echo "regen: expected 1 DontInline function, got $NDI" >&2
    exit 1
fi
$SPIRV_AS --target-env vulkan1.2 "$TMP.di2.asm" -o "$TMP"
$SPIRV_OPT -O "$TMP" -o "$TMP"
rm -f "$TMP.di.asm" "$TMP.di2.asm"
$GLSLC -O --target-env=vulkan1.2 -o "$TMPL" "$LAD"
# integer-only kernels: no NoContraction pass needed
$GLSLC -O --target-env=vulkan1.2 -o "$TMPR" "$RMW"
$SPIRV_VAL --target-env vulkan1.2 "$TMPR"
$GLSLC -O --target-env=vulkan1.2 -o "$TMPO" "$OV0"
$SPIRV_VAL --target-env vulkan1.2 "$TMPO"

# Strip NoContraction from ops with no reachable fusion partner. SPIRV-Cross
# emits a noinline optnone MSL helper per decorated op, so a dead decoration
# costs a call per pixel and constrains nothing. Re-derived from the module
# every regen, so it cannot go stale; `precise` propagates backward through
# glslc, so this cannot be expressed in the GLSL source.
$SPIRV_DIS "$TMP" -o "$TMP.asm"
python3 scripts/nocon-min.py "$TMP.asm" "$TMP.min.asm"
$SPIRV_AS --target-env vulkan1.2 "$TMP.min.asm" -o "$TMP"
$SPIRV_VAL --target-env vulkan1.2 "$TMP"
rm -f "$TMP.asm" "$TMP.min.asm"

# Standing check: the z chain is SOFTWARE f64 (vid_ati_rage128_gpu_f64.glsl)
# because Apple GPUs report shaderFloat64=false. A module that declared the Float64 capability
# would take a native path wherever the device does advertise it, so
# bit-exactness would change per GPU instead of per kernel. Absence of the
# capability is what makes "always the soft path" a property of the module
# rather than a promise about the source.
for m in "$TMP" "$TMPL" "$TMPR" "$TMPO"; do
    if $SPIRV_DIS "$m" | grep -q '^ *OpCapability Float64'; then
        echo "regen: $m declares Float64 -- the z chain must stay software" >&2
        exit 1
    fi
done

# The header is built beside the committed one and renamed over it only
# when every step succeeded: a failure inside the group (no xxd on the
# PATH, a tool dying) must leave the tree's header untouched, not
# truncated to the preamble.
NEW="$OUT.new"
trap 'rm -f "$NEW"' EXIT
{
    echo "/* Generated by scripts/regen-r128-gpu-spv.sh from"
    echo "   vid_ati_rage128_gpu_seg.comp + _lad.comp + _rmw.comp + _ov0.comp"
    echo "   -- do not edit. Tools:"
    echo "   glslc: $GLSLC_VER"
    echo "   glslc spirv-tools: $GLSLC_ST_VER"
    echo "   glslc glslang: $GLSLC_GL_VER"
    echo "   spirv-opt: $OPT_VER"
    echo "*/"
    echo "static const unsigned char r128_gpu_seg_spv[] = {"
    xxd -i < "$TMP"
    echo "};"
    echo "static const unsigned char r128_gpu_lad_spv[] = { /* z-ladder pre-pass */"
    xxd -i < "$TMPL"
    echo "};"
    echo "static const unsigned char r128_gpu_rmw_spv[] = { /* generic 2D RMW */"
    xxd -i < "$TMPR"
    echo "};"
    echo "static const unsigned char r128_gpu_ov0_spv[] = { /* OV0 overlay scaler */"
    xxd -i < "$TMPO"
    echo "};"
    echo "/* kernel id -> specialization (constant_id 0..3) */"
    echo "static const struct r128_gpu_variant_t {"
    echo "    const char *name;"
    echo "    int         tex, lod, blend, z;"
    echo "} r128_gpu_variants[] = {"
    echo "$VARIANTS" | while IFS=: read -r id name tex lod blend z; do
        echo "    { \"$name\", $tex, $lod, $blend, $z }, /* $id */"
    done
    echo "};"
} > "$NEW"
mv -f "$NEW" "$OUT"
rm -f "$TMP" "$TMPL" "$TMPR" "$TMPO"
wc -c "$OUT"
