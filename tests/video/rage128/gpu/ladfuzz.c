/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ATI Rage 128 Pro -- GPU backend, host check of the z-ladder
 *          closed form.
 *
 *          The interpreter walks zline += dZdx in double, one rounded add
 *          per pixel. The ladder pre-pass kernel reproduces that chain in
 *          software binary64, with a closed form evaluated per lane and a
 *          serial fallback. While z stays in one binade, the aligned step
 *          has a constant integer part and a constant discarded tail, so
 *          round-to-nearest-even makes the same decision at every step (a
 *          tie settles after one step, because the mantissa is even from
 *          then on). Rung q is therefore m_entry + (q - q_entry) * inc,
 *          with a full-precision add only at binade entry and at each
 *          crossing. This tool runs the closed form, in a serial and a
 *          per-lane version, and a transcription of the kernel's software
 *          add on the host, over fixed vectors and random cases, and
 *          compares every rung of every case bit for bit with the double
 *          chain.
 *
 *          Build: cc -O2 -ffp-contract=off -o ladfuzz ladfuzz.c
 *          Usage: ladfuzz [cases]
 *
 * Authors: skiretic.
 *
 *          Copyright 2026 skiretic.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define NMAX 256

static uint64_t rs = 0x9e3779b97f4a7c15ull;
static uint64_t
rng(void)
{
    rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
    return rs;
}

static uint64_t
d2u(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
static double
u2d(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }

/* One generic rounded step: the host uses the FPU; the kernel uses the
   same software f64_add. */
static uint64_t
step_generic(uint64_t z, uint64_t dz)
{
    return d2u(u2d(z) + u2d(dz));
}

/* The kernel's f64_add (vid_ati_rage128_gpu_f64.glsl), transcribed statement for statement
   so the serial walk's arithmetic can be checked here against the
   FPU on every operand class; the two texts must stay in step. */
#define F64_SIGN 0x8000000000000000ull
#define F64_MANT 0x000fffffffffffffull
#define F64_HID  0x0010000000000000ull
#define F64_INF  0x7ff0000000000000ull
#define F64_QNAN 0x7ff8000000000000ull

static uint64_t
soft_add(uint64_t a, uint64_t b)
{
    uint64_t amag = a & ~F64_SIGN;
    uint64_t bmag = b & ~F64_SIGN;

    if (amag > F64_INF || bmag > F64_INF)
        return F64_QNAN;
    if (amag == F64_INF)
        return (bmag == F64_INF && a != b) ? F64_QNAN : a;
    if (bmag == F64_INF)
        return b;
    if (amag == 0)
        return (bmag == 0) ? (a & b) : b;
    if (bmag == 0)
        return a;
    if (amag < bmag) {
        uint64_t t = a; a = b; b = t;
        t = amag; amag = bmag; bmag = t;
    }
    int      ea = (int) (amag >> 52);
    int      eb = (int) (bmag >> 52);
    uint64_t ma = ((amag & F64_MANT) | (ea != 0 ? F64_HID : 0)) << 3;
    uint64_t mb = ((bmag & F64_MANT) | (eb != 0 ? F64_HID : 0)) << 3;
    unsigned sa = (unsigned) (a >> 63);
    unsigned sb = (unsigned) (b >> 63);
    uint64_t mbs;

    ea = ea > 1 ? ea : 1;
    eb = eb > 1 ? eb : 1;
    int sh = ea - eb;

    if (sh >= 56)
        mbs = 1;
    else if (sh > 0) {
        uint64_t rem = mb & ((1ull << sh) - 1);
        mbs = (mb >> sh) | (rem != 0 ? 1 : 0);
    } else
        mbs = mb;

    uint64_t mr;
    int      er = ea;

    if (sa == sb)
        mr = ma + mbs;
    else {
        mr = ma - mbs;
        if (mr == 0)
            return 0;
    }
    if (mr >= (1ull << 56)) {
        mr = (mr >> 1) | (mr & 1);
        er++;
    } else {
        while (mr < (1ull << 55) && er > 1) {
            mr <<= 1;
            er--;
        }
    }
    unsigned grs  = (unsigned) (mr & 7);
    uint64_t keep = mr >> 3;

    if (grs > 4 || (grs == 4 && (keep & 1) != 0))
        keep++;
    if (keep >= (1ull << 53)) {
        keep >>= 1;
        er++;
    }
    if (er >= 2047)
        return ((uint64_t) sa << 63) | F64_INF;
    if (er == 1 && keep < F64_HID)
        er = 0;
    return ((uint64_t) sa << 63) | ((uint64_t) er << 52) | (keep & F64_MANT);
}

/* soft_add against the FPU: bit-exact for every non-NaN sum, by class
   for a NaN. */
static int
add_same(uint64_t got, uint64_t want)
{
    if ((want & ~F64_SIGN) > F64_INF)
        return (got & ~F64_SIGN) > F64_INF;
    return got == want;
}

static void walk_ref(uint64_t z, uint64_t dz, int n, uint64_t *out);

/* The serial walk's arithmetic: the exceptional operand classes as
   single sums, random normal pairs, and whole chains stepped with
   soft_add against the double chain, rung by rung. */
static int
serial_vectors(void)
{
    static const struct {
        const char *name;
        uint64_t    a, b;
    } pairs[] = {
        { "+0 + -0",            0x0000000000000000ull, 0x8000000000000000ull },
        { "-0 + -0",            0x8000000000000000ull, 0x8000000000000000ull },
        { "minsub + minsub",    0x0000000000000001ull, 0x0000000000000001ull },
        { "sub + sub carry",    0x0008000000000000ull, 0x0008000000000000ull },
        { "minnorm - half",     0x0010000000000000ull, 0x8008000000000000ull },
        { "norm cancel to sub", 0x0010000000000001ull, 0x8010000000000000ull },
        { "sub exact cancel",   0x0000000000000005ull, 0x8000000000000005ull },
        { "sub - smaller sub",  0x0000000000000005ull, 0x8000000000000003ull },
        { "minnorm + minsub",   0x0010000000000000ull, 0x0000000000000001ull },
        { "one + minsub",       0x3ff0000000000000ull, 0x0000000000000001ull },
        { "one + tie",          0x3ff0000000000000ull, 0x3ca0000000000000ull },
        { "one + tie + sticky", 0x3ff0000000000000ull, 0x3ca0000000000001ull },
        { "max + max",          0x7fefffffffffffffull, 0x7fefffffffffffffull },
        { "-max + -max",        0xffefffffffffffffull, 0xffefffffffffffffull },
        { "max + half ulp",     0x7fefffffffffffffull, 0x7c90000000000000ull },
        { "max + below half",   0x7fefffffffffffffull, 0x7c8fffffffffffffull },
        { "+inf + +inf",        0x7ff0000000000000ull, 0x7ff0000000000000ull },
        { "-inf + -inf",        0xfff0000000000000ull, 0xfff0000000000000ull },
        { "+inf + -inf",        0x7ff0000000000000ull, 0xfff0000000000000ull },
        { "+inf + one",         0x7ff0000000000000ull, 0x3ff0000000000000ull },
        { "one + -inf",         0x3ff0000000000000ull, 0xfff0000000000000ull },
        { "+inf + -0",          0x7ff0000000000000ull, 0x8000000000000000ull },
        { "qnan + one",         0x7ff8000000000000ull, 0x3ff0000000000000ull },
        { "one + snan",         0x3ff0000000000000ull, 0x7ff0000000000001ull },
        { "qnan + +inf",        0x7ff8000000000000ull, 0x7ff0000000000000ull },
    };
    static const struct {
        const char *name;
        double      z, dz;
        int         n;
    } chains[] = {
        { "inf-chain",      INFINITY,                INFINITY,    8 },
        { "inf-step",       0x1.fffffffffffffp+1023, 0x1.fffffffffffffp+1023, 8 },
        { "neg-inf-chain", -INFINITY,                0x1p-3,      8 },
        { "nan-chain",      NAN,                     0x1p-3,      8 },
        { "nan-step",       0x1p-1,                  NAN,         8 },
        { "inf-minus-inf",  INFINITY,               -INFINITY,    8 },
        { "sub-chain",      0x1p-1022,              -0x1p-1023,   8 },
        { "sub-climb",      0x0.0000000000001p-1022, 0x0.0000000000003p-1022, 64 },
        { "to-zero",        0x1p-1074,              -0x1p-1074,   4 },
    };
    int bad = 0;

    for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
        uint64_t want = d2u(u2d(pairs[i].a) + u2d(pairs[i].b));
        uint64_t got  = soft_add(pairs[i].a, pairs[i].b);

        if (!add_same(got, want)) {
            printf("serial %-20s ref=%016llx got=%016llx\n", pairs[i].name,
                   (unsigned long long) want, (unsigned long long) got);
            bad++;
        }
    }
    for (long c = 0; c < 200000; c++) {
        /* random normal pairs across the exponent range, both signs */
        uint64_t a = (rng() & 0x7fffffffffffffffull) % 0x7ff0000000000000ull | (rng() << 63);
        uint64_t b = (rng() & 0x7fffffffffffffffull) % 0x7ff0000000000000ull | (rng() << 63);
        uint64_t want = d2u(u2d(a) + u2d(b));
        uint64_t got  = soft_add(a, b);

        if (!add_same(got, want)) {
            if (bad < 10)
                printf("serial random a=%016llx b=%016llx ref=%016llx got=%016llx\n",
                       (unsigned long long) a, (unsigned long long) b,
                       (unsigned long long) want, (unsigned long long) got);
            bad++;
        }
    }
    for (size_t i = 0; i < sizeof(chains) / sizeof(chains[0]); i++) {
        uint64_t ref[NMAX], z = d2u(chains[i].z), dz = d2u(chains[i].dz);
        int      cbad = 0;

        walk_ref(z, dz, chains[i].n, ref);
        for (int q = 0; q < chains[i].n; q++) {
            if (!add_same(z, ref[q]) && cbad++ == 0)
                printf("serial chain %s: q=%d ref=%016llx got=%016llx\n",
                       chains[i].name, q, (unsigned long long) ref[q],
                       (unsigned long long) z);
            z = soft_add(z, dz);
        }
        bad += cbad;
    }
    printf("serial mismatches=%d\n", bad);
    return bad;
}

/* Closed-form walk. Writes n rungs. Returns 0 and falls back to the
   serial walk when the fast-path preconditions fail (non-finite, zero or
   subnormal operands, sign change, or an exponent gap the increment
   model does not cover). */
static int
walk_fast(uint64_t z, uint64_t dz, int n, uint64_t *out)
{
    int      ez  = (int) ((z >> 52) & 0x7ff);
    int      edz = (int) ((dz >> 52) & 0x7ff);
    uint64_t sz  = z >> 63, sdz = dz >> 63;
    int      q   = 0;

    if (ez == 0 || ez == 0x7ff || edz == 0x7ff)
        return 0;
    if (edz == 0) {
        /* dz zero (or subnormal: treat as unsupported unless exactly 0) */
        if ((dz & 0x000fffffffffffffull) != 0)
            return 0;
        for (; q < n; q++)
            out[q] = z;
        return 1;
    }
    while (q < n) {
        /* binade entry: one generic step settles parity/ties, and the
           rung stored at q is the current z before the step */
        out[q++] = z;
        if (q >= n)
            break;
        {
            uint64_t z1 = step_generic(z, dz);
            int      e1 = (int) ((z1 >> 52) & 0x7ff);

            if (e1 == 0 || e1 == 0x7ff || (z1 >> 63) != sz)
                return 0;
            if (e1 != ez) {
                /* crossed on the entry step: re-enter the new binade */
                z  = z1;
                ez = e1;
                continue;
            }
            /* constant increment for this binade, in ulps of ez */
            {
                int      s     = ez - edz;   /* dz alignment shift */
                uint64_t mant  = (dz & 0x000fffffffffffffull) | (1ull << 52);
                uint64_t a, tail, half;
                int64_t  inc;

                if (s < 0) {
                    /* dz coarser than z's ulp: exact add of a multiple of
                       the ulp, no rounding; cannot overflow 11 bits here
                       because a crossing would have happened above */
                    if (-s > 10)
                        return 0;
                    a    = mant << (-s);
                    tail = 0;
                    half = 1;
                } else if (s > 63) {
                    a    = 0;
                    tail = 0;
                    half = 1;  /* tail < half: increment 0 */
                } else {
                    a    = mant >> s;
                    tail = s ? (mant & ((1ull << s) - 1)) : 0;
                    half = s ? (1ull << (s - 1)) : 0;
                    if (!s) { tail = 0; half = 1; }
                }
                if (sz == sdz) {
                    /* magnitude grows: m += a, tail decides the +1 */
                    if (tail > half)
                        inc = (int64_t) a + 1;
                    else if (tail < half)
                        inc = (int64_t) a;
                    else
                        inc = (int64_t) a + ((a & 1) ? 1 : 0); /* even after z1 */
                } else {
                    /* magnitude shrinks: exact S = m - a - tail/2^s */
                    if (tail == 0)
                        inc = -(int64_t) a;
                    else if (tail > half)
                        inc = -(int64_t) a - 1;
                    else if (tail < half)
                        inc = -(int64_t) a;
                    else
                        inc = -(int64_t) a - ((a & 1) ? 1 : 0);
                }
                /* tie with a even, magnitude shrinking: m - a is even
                   only if m even; after z1 m is even, so -a holds */
                z = z1;
                {
                    uint64_t m = z & 0x000fffffffffffffull; /* 52-bit field */
                    uint64_t hi = z & 0xfff0000000000000ull;

                    /* steps that stay in the binade: field stays in
                       [0, 2^52) */
                    while (q < n) {
                        int64_t nm;

                        out[q++] = hi | m;  /* rung q is the current z */
                        nm = (int64_t) m + inc;
                        /* A nonzero tail can round below the binade floor
                           at its finer spacing; the full add owns that step. */
                        if (nm < 0 || nm >= (int64_t) (1ull << 52)
                            || (inc < 0 && tail != 0 && nm == 0))
                            break;
                        m = (uint64_t) nm;
                    }
                    z = hi | m;
                    if (q >= n)
                        return 1;
                    /* crossing step: generic, then re-enter. The rung
                       for the crossed value is stored by the entry. */
                    z1 = step_generic(z, dz);
                    e1 = (int) ((z1 >> 52) & 0x7ff);
                    if (e1 == 0 || e1 == 0x7ff || (z1 >> 63) != sz)
                        return 0;
                    z  = z1;
                    ez = e1;
                }
            }
        }
    }
    return 1;
}

/* Mirror of the kernel's 32-lane form: the run after binade entry is
   stored as m + j*inc with the run length k from a division, not by
   stepping; every lane derives the same segments. */
static int
walk_lanes(uint64_t z, uint64_t dz, int n, uint64_t *out)
{
    int      ez  = (int) ((z >> 52) & 0x7ff);
    int      edz = (int) ((dz >> 52) & 0x7ff);
    uint64_t sz  = z >> 63, sdz = dz >> 63;
    int      q   = 0;

    if (ez == 0 || ez == 0x7ff || edz == 0x7ff)
        return 0;
    if (edz == 0) {
        if ((dz & 0x000fffffffffffffull) != 0)
            return 0;
        for (; q < n; q++)
            out[q] = z;
        return 1;
    }
    while (q < n) {
        out[q++] = z;
        if (q >= n)
            return 1;
        uint64_t z1 = step_generic(z, dz);
        int      e1 = (int) ((z1 >> 52) & 0x7ff);

        if (e1 == 0 || e1 == 0x7ff || (z1 >> 63) != sz)
            return 0;
        if (e1 != ez) { z = z1; ez = e1; continue; }
        int      s    = ez - edz;
        uint64_t mant = (dz & 0x000fffffffffffffull) | (1ull << 52);
        uint64_t a, tail, half;
        int64_t  inc;

        if (s < 0) {
            if (-s > 10) return 0;
            a = mant << (-s); tail = 0; half = 1;
        } else if (s > 63) {
            a = 0; tail = 0; half = 1;
        } else if (s == 0) {
            a = mant; tail = 0; half = 1;
        } else {
            a = mant >> s; tail = mant & ((1ull << s) - 1); half = 1ull << (s - 1);
        }
        if (sz == sdz) {
            if (tail > half) inc = (int64_t) a + 1;
            else if (tail < half) inc = (int64_t) a;
            else inc = (int64_t) a + ((a & 1) ? 1 : 0);
        } else {
            if (tail == 0) inc = -(int64_t) a;
            else if (tail > half) inc = -(int64_t) a - 1;
            else if (tail < half) inc = -(int64_t) a;
            else inc = -(int64_t) a - ((a & 1) ? 1 : 0);
        }
        {
            uint64_t m  = z1 & 0x000fffffffffffffull;
            uint64_t hi = z1 & 0xfff0000000000000ull;
            uint64_t k;
            int      cnt;

            if (inc > 0) k = ((1ull << 52) - 1 - m) / (uint64_t) inc;
            else if (inc < 0) {
                k = m / (uint64_t) (-inc);
                /* Exclude a floor landing with discarded bits so the
                   full add rounds it at the lower binade's spacing. */
                if (tail != 0 && k > 0 && m % (uint64_t) (-inc) == 0)
                    k--;
            }
            else k = (uint64_t) n;
            cnt = (k + 1 < (uint64_t) (n - q)) ? (int) k + 1 : n - q;
            for (int j = 0; j < cnt; j++)
                out[q + j] = hi | (uint64_t) ((int64_t) m + (int64_t) j * inc);
            q += cnt;
            if (q >= n)
                return 1;
            z  = hi | (uint64_t) ((int64_t) m + (int64_t) (cnt - 1) * inc);
            z1 = step_generic(z, dz);
            e1 = (int) ((z1 >> 52) & 0x7ff);
            if (e1 == 0 || e1 == 0x7ff || (z1 >> 63) != sz)
                return 0;
            z = z1; ez = e1;
        }
    }
    return 1;
}

static void
walk_ref(uint64_t z, uint64_t dz, int n, uint64_t *out)
{
    double d = u2d(z), s = u2d(dz);

    for (int q = 0; q < n; q++) {
        out[q] = d2u(d);
        d += s;
    }
}

static double
rnd_log(double lo, double hi)
{
    double u = (double) (rng() >> 11) / 9007199254740992.0;

    return exp(log(lo) + u * (log(hi) - log(lo)));
}

static int
fixed_vectors(void)
{
    static const struct {
        const char *name;
        double      z, dz;
        int         n;
    } vectors[] = {
        { "floor-one",       0x1.0000000000003p+0, -0x1.6p-52, 8 },
        { "floor-half",      0x1.0000000000003p-1, -0x1.6p-53, 8 },
        { "mirror-one",     -0x1.0000000000003p+0,  0x1.6p-52, 8 },
        { "mirror-half",    -0x1.0000000000003p-1,  0x1.6p-53, 8 },
        { "exact-one",       0x1.0000000000003p+0, -0x1p-52,   8 },
        { "exact-half",      0x1.0000000000003p-1, -0x1p-53,   8 },
        { "exact-mirror",   -0x1.0000000000003p-1,  0x1p-53,   8 },
        { "zero-run",        0x1.0000000000002p-1, -0x1.ap-53, 8 },
        { "lane-before",     0x1.000000000001fp-1, -0x1.6p-53, 65 },
        { "lane-at",         0x1.0000000000020p-1, -0x1.6p-53, 65 },
        { "lane-after",      0x1.0000000000021p-1, -0x1.6p-53, 65 },
        { "lane-mirror",    -0x1.0000000000020p-1,  0x1.6p-53, 65 },
        { "lane-exact",      0x1.0000000000020p-1, -0x1p-53,   65 },
        { "lane-second",     0x1.0000000000040p-1, -0x1.6p-53, NMAX },
    };
    uint64_t ref[NMAX], fast[NMAX], lanes[NMAX];
    int      mismatches = 0;

    for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
        uint64_t z = d2u(vectors[i].z), dz = d2u(vectors[i].dz);
        int      n = vectors[i].n;
        int      bad[2] = { 0, 0 };
        int      ok[2];
        uint64_t *out[2] = { fast, lanes };
        const char *names[2] = { "fast", "lanes" };

        walk_ref(z, dz, n, ref);
        ok[0] = walk_fast(z, dz, n, fast);
        ok[1] = walk_lanes(z, dz, n, lanes);
        for (int w = 0; w < 2; w++) {
            if (!ok[w]) {
                printf("fixed %s %s: unexpected fallback\n", vectors[i].name, names[w]);
                bad[w]++;
            } else {
                for (int q = 0; q < n; q++) {
                    if (out[w][q] == ref[q])
                        continue;
                    if (bad[w]++ == 0)
                        printf("fixed %s %s: q=%d ref=%a got=%a\n",
                               vectors[i].name, names[w], q, u2d(ref[q]), u2d(out[w][q]));
                }
            }
        }
        printf("fixed %s: n=%d fast=%d lanes=%d mismatches\n",
               vectors[i].name, n, bad[0], bad[1]);
        mismatches += bad[0] + bad[1];
    }
    printf("fixed-vector mismatches=%d\n", mismatches);
    return mismatches;
}

int
main(int argc, char **argv)
{
    long     cases = argc > 1 ? atol(argv[1]) : 2000000;
    uint64_t ref[NMAX], fast[NMAX], lanes[NMAX];
    long     fails = fixed_vectors() + serial_vectors(), fallback = 0, ties = 0;
    long     fast_fail = 0, lanes_fail = 0;
    /* Checker control: LADFUZZ_FAULT=fast, lanes or both flips one rung
       of the named walker's output on the first accepted case, which
       must be reported against that walker and no other. */
    const char *fault = getenv("LADFUZZ_FAULT");
    int         fault_fast  = fault && (!strcmp(fault, "fast") || !strcmp(fault, "both"));
    int         fault_lanes = fault && (!strcmp(fault, "lanes") || !strcmp(fault, "both"));

    for (long c = 0; c < cases; c++) {
        double z0, dz;
        int    n = 1 + (int) (rng() % NMAX);
        int    mode = (int) (rng() % 8);

        switch (mode) {
            case 0: z0 = (double) (rng() >> 11) / 9007199254740992.0; break;   /* [0,1) */
            case 1: z0 = rnd_log(1e-9, 1.0); break;
            case 2: z0 = rnd_log(1e-9, 1.0) * ((rng() & 1) ? -1 : 1); break;
            case 3: z0 = rnd_log(1.0, 1e7); break;
            case 4: z0 = 0.5 + (double) (rng() >> 11) / 9007199254740992.0 * 1e-6; break;
            default: z0 = (double) (rng() >> 11) / 9007199254740992.0 * 65536.0; break;
        }
        switch (rng() % 6) {
            case 0: dz = rnd_log(1e-12, 1e-1); break;
            case 1: dz = -rnd_log(1e-12, 1e-1); break;
            case 2: dz = rnd_log(1e-20, 1e3) * ((rng() & 1) ? -1 : 1); break;
            case 3: dz = 0.0; break;
            /* constructed exact-tie tails: dz = (odd) * 2^k */
            case 4: dz = ldexp((double) ((rng() & 0xffff) | 1), -(int) (40 + rng() % 30)); ties++; break;
            default: dz = u2d(d2u(z0) & ~0x000fffffffffffffull) * rnd_log(1e-18, 1e-2); break;
        }
        /* Each walker runs on its own and is compared against the
           reference on its own buffer, so a wrong rung from either one
           is seen whatever the other produces, and a lane-only accept
           or refusal is tested too. */
        int ok_fast, ok_lanes;

        walk_ref(d2u(z0), d2u(dz), n, ref);
        ok_fast  = walk_fast(d2u(z0), d2u(dz), n, fast);
        ok_lanes = walk_lanes(d2u(z0), d2u(dz), n, lanes);
        if (!ok_fast)
            fallback++;
        if (ok_fast != ok_lanes) {
            printf("FAIL %s fallback where %s ran: z0=%a dz=%a n=%d\n",
                   ok_fast ? "LANES" : "FAST", ok_fast ? "fast" : "lanes", z0, dz, n);
            if (ok_fast)
                lanes_fail++;
            else
                fast_fail++;
        }
        if (fault_fast && ok_fast) {
            fast[n - 1] ^= 1;
            fault_fast = 0;
        }
        if (fault_lanes && ok_lanes) {
            lanes[n - 1] ^= 1;
            fault_lanes = 0;
        }
        if (ok_fast && memcmp(ref, fast, (size_t) n * 8)) {
            if (fast_fail++ < 10) {
                int q;
                for (q = 0; q < n && ref[q] == fast[q]; q++) ;
                printf("FAIL FAST z0=%a dz=%a n=%d at q=%d ref=%a fast=%a\n",
                       z0, dz, n, q, u2d(ref[q]), u2d(fast[q]));
            }
        }
        if (ok_lanes && memcmp(ref, lanes, (size_t) n * 8)) {
            if (lanes_fail++ < 10) {
                int q;
                for (q = 0; q < n && ref[q] == lanes[q]; q++) ;
                printf("FAIL LANES z0=%a dz=%a n=%d at q=%d ref=%a lanes=%a\n",
                       z0, dz, n, q, u2d(ref[q]), u2d(lanes[q]));
            }
        }
    }
    fails += fast_fail + lanes_fail;
    printf("%ld cases, %ld fast, %ld fallback (%.2f%%), %ld tie-constructed, "
           "fast_fail=%ld lanes_fail=%ld, %ld FAILS\n",
           cases, cases - fallback, fallback, 100.0 * fallback / cases, ties,
           fast_fail, lanes_fail, fails);
    return fails != 0;
}
