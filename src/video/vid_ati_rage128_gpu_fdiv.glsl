/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- GPU backend, correctly rounded binary32
 *          divide.
 *
 *          Vulkan permits 2.5 units in the last place of error on a
 *          float divide, while the C interpreter the GPU backend must
 *          match rounds every divide correctly. Every other float
 *          operation the shading kernel uses (add, subtract, multiply,
 *          round-to-even, floor, the int/float conversions) is required
 *          to be correctly rounded, so a divide spelled with `/` is the
 *          one place where the kernel's bits would depend on the host
 *          driver instead of on the source. There is no float-controls
 *          setting for divide accuracy, so the quotient is formed here
 *          in integers instead.
 *
 *          vid_ati_rage128_gpu_seg.comp includes this file, and so does
 *          the fdivtest micro-test in tests/video/rage128/gpu: shared,
 *          not copied, so the test exercises the same source text the
 *          production kernel compiles.
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */

/* fdiv_cr is not a "more accurate" divide, it is the IEEE-754 binary32
   quotient: exact 24-bit long division plus round-to-nearest-even on a
   true guard/sticky pair. A correctly rounded stack (arm64 Metal, the
   C reference) sees no change at all, it returns what `/` already
   returns there, which is what lets this live in the shared kernel
   rather than behind a host-specific switch. Validated bit for bit
   against a double-rounded reference over about 60 million random
   pairs plus a full exponent sweep (subnormal operands and results
   included).

   The special operands (infinity, NaN, zero) are spelled out in
   integers as well, so the function contains no float divide at all
   (`spirv-dis ... | grep OpFDiv` is empty). Delegating them to `/` on
   the reasoning that their results are exact special values the error
   allowance cannot touch fails on at least one driver, which returns
   NaN for infinity divided by a finite value (0xffc00000 where IEEE
   says 0x7f800000); the fdivtest micro-test catches that. */
float
fdiv_cr(float a, float b)
{
    uint ua = floatBitsToUint(a), ub = floatBitsToUint(b);
    uint sr = (ua ^ ub) >> 31;
    uint ea = (ua >> 23) & 0xffu, eb = (ub >> 23) & 0xffu;
    uint ma = ua & 0x7fffffu, mb = ub & 0x7fffffu;

    {
        bool anan = (ea == 0xffu) && (ma != 0u);
        bool bnan = (eb == 0xffu) && (mb != 0u);
        bool ainf = (ea == 0xffu) && (ma == 0u);
        bool binf = (eb == 0xffu) && (mb == 0u);
        bool az   = (ua & 0x7fffffffu) == 0u;
        bool bz   = (ub & 0x7fffffffu) == 0u;

        /* IEEE-754 7.2/6.1, in order: the two invalid forms, then the
           divide-by-zero/overflow form, then the underflow form */
        if (anan || bnan || (ainf && binf) || (az && bz))
            return uintBitsToFloat(0x7fc00000u);
        if (ainf || bz)
            return uintBitsToFloat((sr << 31) | 0x7f800000u);
        if (binf || az)
            return uintBitsToFloat(sr << 31);
    }
    int  xa = int(ea), xb = int(eb);

    /* subnormal operands: renormalize so both significands are 24-bit */
    if (ea == 0u) { int k = 23 - findMSB(ma); ma <<= k; xa = 1 - k; }
    else            ma |= 0x800000u;
    if (eb == 0u) { int k = 23 - findMSB(mb); mb <<= k; xb = 1 - k; }
    else            mb |= 0x800000u;

    /* a/b == (ma/mb) * 2^E; force ma/mb into [1,2) */
    int E = xa - xb;

    if (ma < mb) { ma <<= 1; E--; }

    /* Q = floor(ma * 2^24 / mb) in [2^24, 2^25), by 32-bit long division
       in 7+8+8+1 bit steps. ma < 2*mb <= 2^25 and every remainder is
       < mb < 2^24, so no partial dividend can overflow 32 bits; the
       final 1-bit step is a compare, not a divide. */
    uint t, q1, q2, q3, q4, r1, r2, r3, r4;

    t = ma << 7; q1 = t / mb; r1 = t % mb;
    t = r1 << 8; q2 = t / mb; r2 = t % mb;
    t = r2 << 8; q3 = t / mb; r3 = t % mb;
    t = r3 << 1; q4 = (t >= mb) ? 1u : 0u; r4 = t - q4 * mb;

    uint q  = (q1 << 17) + (q2 << 9) + (q3 << 1) + q4;
    uint st = (r4 != 0u) ? 1u : 0u;
    int  er = E + 127;

    if (er > 0) {
        uint m = q >> 1, rb = q & 1u;

        if (rb != 0u && (st != 0u || (m & 1u) != 0u)) m++;
        if (m >= 0x1000000u) { m >>= 1; er++; }
        if (er >= 255)
            return uintBitsToFloat((sr << 31) | 0x7f800000u);
        return uintBitsToFloat((sr << 31) | (uint(er) << 23) | (m & 0x7fffffu));
    }
    {
        /* subnormal result: q has weight 2^(E-24), the target unit is
           2^-149, so shift right by s and re-round there. The encoding
           is contiguous, so a significand that rounds up to 2^23 lands
           on the smallest normal without any special case. */
        int  s = -(E + 125); /* >= 2 */
        uint m, rb;

        if (s >= 32) {
            m = 0u; rb = 0u;
            st = (q != 0u || st != 0u) ? 1u : 0u;
        } else {
            m  = q >> s;
            rb = (q >> (s - 1)) & 1u;
            st = ((q & ((1u << (s - 1)) - 1u)) != 0u || st != 0u) ? 1u : 0u;
        }
        if (rb != 0u && (st != 0u || (m & 1u) != 0u)) m++;
        return uintBitsToFloat((sr << 31) | m);
    }
}

/* float(n) / 255.0 for n in [0,255], correctly rounded -- the byte
   unpack the interpreter spells `x / 255.0f`. Same contract as
   fdiv_cr, but the divisor is the literal 255 so the whole thing is
   one division by a compile-time constant (a multiply-high on every
   backend) instead of a long division. 255 is odd, so n * 2^k / 255
   can never land on an exact tie and the even rule cannot trigger.
   Exhaustively verified against `/ 255.0f` for all 256 inputs. */
float
div255(uint n)
{
    if (n == 0u)
        return 0.0;

    int  e = findMSB(n);              /* 0..7 */
    uint N = n << (31 - e);           /* [2^31, 2^32) */
    uint m = N / 255u + (((N % 255u) * 2u > 255u) ? 1u : 0u);
    int  E = e - 8;

    if (m >= 0x1000000u) { m >>= 1; E++; }
    return uintBitsToFloat((uint(E + 127) << 23) | (m & 0x7fffffu));
}
