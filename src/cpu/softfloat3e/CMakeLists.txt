#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          CMake build script.
#
# Authors: David Hrdlička, <hrdlickadavid@outlook.com>
#
#          Copyright 2020-2021 David Hrdlička.
#

add_library(softfloat3e OBJECT primitives.c softfloat-specialize.c
    extF80_addsub.cc extF80_class.cc extF80_compare.cc
    extF80_div.cc extF80_extract.cc extF80_mul.cc extF80_rem.cc extF80_roundToInt.cc
    extF80_scale.cc extF80_sqrt.cc extF80_to_f16.cc extF80_to_f32.cc extF80_to_f64.cc
    extF80_to_f128.cc extF80_to_i32.cc extF80_to_i32_r_minMag.cc extF80_to_i64.cc
    extF80_to_i64_r_minMag.cc extF80_to_ui32.cc extF80_to_ui32_r_minMag.cc extF80_to_ui64.cc
    extF80_to_ui64_r_minMag.cc f16_addsub.c f16_class.c f16_compare.c f16_div.c f16_getExp.c
    f16_getMant.c f16_minmax.c f16_mul.c f16_mulAdd.c f16_range.c f16_roundToInt.c
    f16_sqrt.c f16_to_extF80.cc f16_to_f32.c f16_to_f64.c f16_to_i32.c f16_to_i32_r_minMag.c
    f16_to_i64.c f16_to_i64_r_minMag.c f16_to_ui32.c f16_to_ui32_r_minMag.c f16_to_ui64.c
    f16_to_ui64_r_minMag.c f32_addsub.c f32_class.c f32_compare.c f32_div.c f32_frc.c
    f32_getExp.c f32_getMant.c f32_minmax.c f32_mul.c f32_mulAdd.c f32_range.c
    f32_roundToInt.c f32_scalef.c f32_sqrt.c f32_to_extF80.cc f32_to_f16.c f32_to_f64.c
    f32_to_f128.cc f32_to_i32.c f32_to_i32_r_minMag.c f32_to_i64.c f32_to_i64_r_minMag.c
    f32_to_ui32.c f32_to_ui32_r_minMag.c f32_to_ui64.c f32_to_ui64_r_minMag.c f64_addsub.c
    f64_class.c f64_compare.c f64_div.c f64_frc.c f64_getExp.c f64_getMant.c f64_minmax.c
    f64_mul.c f64_mulAdd.c f64_range.c f64_roundToInt.c f64_scalef.c f64_sqrt.c f64_to_extF80.cc
    f64_to_f16.c f64_to_f32.c f64_to_f128.cc f64_to_i32.c f64_to_i32_r_minMag.c f64_to_i64.c
    f64_to_i64_r_minMag.c f64_to_ui32.c f64_to_ui32_r_minMag.c f64_to_ui64.c f64_to_ui64_r_minMag.c
    f128_addsub.cc f128_div.cc f128_mul.cc f128_mulAdd.cc f128_roundToInt.cc f128_to_extF80.cc
    f128_to_f32.cc f128_to_f64.cc f128_to_i32.cc f128_to_i32_r_minMag.cc f128_to_i64.cc
    f128_to_i64_r_minMag.cc f128_to_ui32.cc f128_to_ui32_r_minMag.cc f128_to_ui64.cc 
    f128_to_ui64_r_minMag.cc i32_to_extF80.cc i32_to_f16.c i32_to_f32.c i32_to_f64.c
    i32_to_f128.cc i64_to_extF80.cc i64_to_f16.c i64_to_f32.c i64_to_f64.c i64_to_f128.cc
    isNaN.cc isSignalingNaN.cc s_add128.cc s_add256M.c s_addMagsExtF80.cc s_addMagsF16.c
    s_addMagsF32.c s_addMagsF64.c s_addMagsF128.cc s_approxRecip_1Ks.c s_approxRecipSqrt_1Ks.c
    s_approxRecipSqrt32_1.c s_commonNaNToExtF80UI.cc s_commonNaNToF16UI.c s_commonNaNToF32UI.c
    s_commonNaNToF64UI.c s_commonNaNToF128UI.cc s_countLeadingZeros8.c s_countLeadingZeros16.c
    s_countLeadingZeros32.c s_countLeadingZeros64.c s_eq128.c s_le128.c s_lt128.c
    s_mul64ByShifted32To128.cc s_mul64To128.cc s_mul128By32.cc s_mul128To256M.cc
    s_normRoundPackToExtF80.cc s_normRoundPackToF16.c s_normRoundPackToF32.c s_normRoundPackToF64.c
    s_normRoundPackToF128.cc s_normSubnormalExtF80Sig.cc s_normSubnormalF16Sig.c
    s_normSubnormalF32Sig.c s_normSubnormalF64Sig.c s_normSubnormalF128Sig.cc s_packToExtF80.cc
    s_propagateNaNExtF80UI.cc s_propagateNaNF16UI.c s_propagateNaNF32UI.c s_propagateNaNF64UI.c
    s_propagateNaNF128UI.cc s_roundPackToExtF80.cc s_roundPackToF16.c s_roundPackToF32.c
    s_roundPackToF64.c s_roundPackToF128.cc s_roundToI32.c s_roundToI64.c s_roundToUI32.c
    s_roundToUI64.c s_shiftRightJam32.c s_shiftRightJam64.c s_shiftRightJam64Extra.c
    s_shiftRightJam256M.c s_shortShiftLeft128.cc s_shortShiftRight128.cc s_shortShiftRightJam64.c 
    s_shortShiftRightJam64Extra.c s_sub128.cc s_sub256M.c s_subMagsExtF80.cc s_subMagsF16.c
    s_subMagsF32.c s_subMagsF64.c s_subMagsF128.cc ui32_to_extF80.cc ui32_to_f16.c ui32_to_f32.c
    ui32_to_f64.c ui32_to_f128.cc ui64_to_extF80.cc ui64_to_f16.c ui64_to_f32.c ui64_to_f64.c
    ui64_to_f128.cc f2xm1.cc fpatan.cc fprem.cc fsincos.cc fyl2x.cc poly.cc consts.cc)