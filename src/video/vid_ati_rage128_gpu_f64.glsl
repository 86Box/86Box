/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- GPU backend, software IEEE-754 binary64.
 *
 *          The interpreter steps depth across a span in C doubles, and
 *          the GPU backend must reproduce every bit of that chain. Apple
 *          GPUs have no 64-bit float type, so the kernels that need the
 *          chain (vid_ati_rage128_gpu_seg.comp and the z-ladder pre-pass
 *          vid_ati_rage128_gpu_lad.comp) include this file and run the
 *          arithmetic on uint64 words instead: add, multiply, compares,
 *          truncation to a 32-bit integer and the depth quantize, all
 *          with round-to-nearest-even.
 *
 *          f64_add takes every class of operand: zeros of both signs,
 *          subnormals, infinities and NaN, with the IEEE results (a NaN
 *          result is one quiet NaN pattern, not the operand's payload).
 *          f64_mul takes finite normal or zero inputs only and does not
 *          propagate infinity or NaN, so a caller that can see a NaN
 *          gates on f64_is_nan before it; the quantizer clamps NaN to 0
 *          itself, matching the interpreter's inverted compare.
 *
 *          Requires GL_EXT_shader_explicit_arithmetic_types_int64, which
 *          the including kernel enables.
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */

const uint64_t F64_SIGN = 0x8000000000000000UL;
const uint64_t F64_MANT = 0x000fffffffffffffUL;
const uint64_t F64_HID  = 0x0010000000000000UL;
const uint64_t F64_INF  = 0x7ff0000000000000UL; /* magnitude of an infinity */
const uint64_t F64_QNAN = 0x7ff8000000000000UL; /* the quiet NaN f64_add returns */

const uint64_t F64_ZERO  = 0x0000000000000000UL;
const uint64_t F64_HALF  = 0x3fe0000000000000UL; /* 0.5 */
const uint64_t F64_ONE   = 0x3ff0000000000000UL; /* 1.0 */
const uint64_t F64_65535 = 0x40efffe000000000UL; /* 65535.0    */
const uint64_t F64_ZMAX24 = 0x416fffffe0000000UL; /* 16777215.0 */
const uint64_t F64_255   = 0x406fe00000000000UL; /* 255.0 (fog table quant) */

uint64_t
f64_add(uint64_t a, uint64_t b)
{
    uint64_t amag = a & ~F64_SIGN;
    uint64_t bmag = b & ~F64_SIGN;

    /* NaN and infinity first: a NaN operand gives NaN, so do two
       infinities of opposite sign; otherwise an infinite operand is the
       sum, whatever the other operand. */
    if (amag > F64_INF || bmag > F64_INF)
        return F64_QNAN;
    if (amag == F64_INF)
        return (bmag == F64_INF && a != b) ? F64_QNAN : a;
    if (bmag == F64_INF)
        return b;
    if (amag == 0UL)
        return (bmag == 0UL) ? (a & b) : b; /* +0 unless both -0 */
    if (bmag == 0UL)
        return a;
    if (amag < bmag) {
        uint64_t t = a; a = b; b = t;
        t = amag; amag = bmag; bmag = t;
    }
    int      ea = int(amag >> 52);
    int      eb = int(bmag >> 52);
    /* A subnormal has exponent field 0 and no implicit bit, and its
       significand is spaced like exponent 1, so it aligns and rounds as
       an exponent-1 value. */
    uint64_t ma = ((amag & F64_MANT) | ((ea != 0) ? F64_HID : 0UL)) << 3; /* GRS tail */
    uint64_t mb = ((bmag & F64_MANT) | ((eb != 0) ? F64_HID : 0UL)) << 3;
    uint     sa = uint(a >> 63);
    uint     sb = uint(b >> 63);
    uint64_t mbs;

    ea = max(ea, 1);
    eb = max(eb, 1);
    int sh = ea - eb;

    if (sh >= 56)
        mbs = 1UL; /* pure sticky (bmag nonzero) */
    else if (sh > 0) {
        uint64_t rem = mb & ((uint64_t(1) << sh) - 1UL);
        mbs = (mb >> sh) | ((rem != 0UL) ? 1UL : 0UL);
    } else
        mbs = mb;

    uint64_t mr;
    int      er = ea;

    if (sa == sb)
        mr = ma + mbs;
    else {
        mr = ma - mbs;
        if (mr == 0UL)
            return F64_ZERO; /* exact cancel -> +0 in RNE */
    }
    if (mr >= (uint64_t(1) << 56)) {
        mr = (mr >> 1) | (mr & 1UL); /* keep sticky */
        er++;
    } else {
        /* normalize no further than exponent 1: below it the result is
           a subnormal, kept at exponent 1's spacing */
        while (mr < (uint64_t(1) << 55) && er > 1) {
            mr <<= 1;
            er--;
        }
    }
    /* round-to-nearest-even on the 3 GRS bits */
    uint     grs  = uint(mr & 7UL);
    uint64_t keep = mr >> 3;

    if (grs > 4u || (grs == 4u && (keep & 1UL) != 0UL))
        keep++;
    if (keep >= (uint64_t(1) << 53)) {
        keep >>= 1;
        er++;
    }
    if (er >= 2047)
        return (uint64_t(sa) << 63) | F64_INF; /* overflow */
    if (er == 1 && keep < F64_HID)
        er = 0; /* subnormal: no implicit bit, exponent field 0 */
    return (uint64_t(sa) << 63) | (uint64_t(er) << 52) | (keep & F64_MANT);
}

uint64_t
f64_mul(uint64_t a, uint64_t b)
{
    uint     sr   = uint((a ^ b) >> 63);
    uint64_t amag = a & ~F64_SIGN;
    uint64_t bmag = b & ~F64_SIGN;

    if (amag == 0UL || bmag == 0UL)
        return uint64_t(sr) << 63;

    int      ea = int(amag >> 52);
    int      eb = int(bmag >> 52);
    uint64_t ma = (amag & F64_MANT) | F64_HID;
    uint64_t mb = (bmag & F64_MANT) | F64_HID;

    /* 53x53 -> 106-bit product via 32-bit limbs */
    uint64_t a0 = ma & 0xffffffffUL, a1 = ma >> 32;
    uint64_t b0 = mb & 0xffffffffUL, b1 = mb >> 32;
    uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    uint64_t mid = p01 + p10;
    uint64_t lo  = p00 + (mid << 32);
    uint64_t hi  = p11 + (mid >> 32)
                 + ((mid < p01) ? (uint64_t(1) << 32) : 0UL)
                 + ((lo < p00) ? 1UL : 0UL);

    /* product in [2^104, 2^106); top bit at 105 -> mant in [2,4) */
    int      top105 = (hi >= (uint64_t(1) << 41)) ? 1 : 0;
    int      sh     = 52 + top105;
    uint64_t keep   = (hi << (64 - sh)) | (lo >> sh);
    uint64_t rnd    = (lo >> (sh - 1)) & 1UL;
    uint64_t stk    = lo & ((uint64_t(1) << (sh - 1)) - 1UL);
    int      er     = ea + eb - 1023 + top105;

    if (rnd != 0UL && (stk != 0UL || (keep & 1UL) != 0UL))
        keep++;
    if (keep >= (uint64_t(1) << 53)) {
        keep >>= 1;
        er++;
    }
    return (uint64_t(sr) << 63) | (uint64_t(er) << 52) | (keep & F64_MANT);
}

/* quiet or signaling NaN */
bool
f64_is_nan(uint64_t a)
{
    return (a & ~F64_SIGN) > 0x7ff0000000000000UL;
}

/* a > 0.0 ? (finite a) */
bool
f64_gt_zero(uint64_t a)
{
    return (a & F64_SIGN) == 0UL && (a & ~F64_SIGN) != 0UL;
}

/* a > b for b a positive constant, a finite */
bool
f64_gt_pos(uint64_t a, uint64_t b)
{
    if ((a & F64_SIGN) != 0UL)
        return false;
    return a > b;
}

/* (uint32_t) of a non-negative double < 2^32 (C trunc toward zero) */
uint
f64_to_u32(uint64_t a)
{
    if ((a & F64_SIGN) != 0UL || (a & ~F64_SIGN) == 0UL)
        return 0u;
    int e = int(a >> 52) - 1023;

    if (e < 0)
        return 0u;
    uint64_t m = (a & F64_MANT) | F64_HID;

    if (e <= 52)
        return uint(m >> (52 - e));
    return uint(m << (e - 52));
}

/* the interpreter's z quantize chain on a raw zline value:
   clamp [0,1], * zmax + 0.5, clamp to zmax, trunc. zmaxd is zmax as a
   double; the clamp to 1.0 first is what keeps z*zmax+0.5 exact, so a
   far/sky fragment lands on exactly zmax instead of one ULP past it. */
uint
f64_zq(uint64_t zc, uint64_t zmaxd)
{
    /* NaN passes the raw sign+magnitude gt tests; the oracle's inverted
       compare (!(zc > 0.0)) sends it to 0 before the fog index or the
       quantize can see it */
    if (f64_is_nan(zc) || !f64_gt_zero(zc))
        zc = F64_ZERO;
    if (f64_gt_pos(zc, F64_ONE))
        zc = F64_ONE;
    uint64_t zq = f64_add(f64_mul(zc, zmaxd), F64_HALF);

    if (f64_gt_pos(zq, zmaxd))
        zq = zmaxd;
    return f64_to_u32(zq);
}

uint
f64_zq16(uint64_t zc)
{
    return f64_zq(zc, F64_65535);
}
