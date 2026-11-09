/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and IBM PC systems and compatibles.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Voice modem audio codecs and framing.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *          Xeon3D
 *
 *          Copyright 2026 Xeon3D.
 *          Copyright 2025-2026 Jasmine Iwanek.
 *
 * Voice modem audio support (char_modem.c).
 *
 * Rockwell ADPCM is what a Rockwell voice modem speaks on its serial port in
 * voice mode (AT#VBS=2/3/4, 7200 samples a second), and the only format
 * Windows 9x's Unimodem/V asks a Rockwell part for.  Its decoder is
 * Rockwell's, in Windows' SERWAVE.VXD, so the coder here has to be bit-exact
 * with Rockwell's -- which is what Peter Jaeckel's fixed-point port of
 * Rockwell's D.ASM in mgetty/vgetty (voice/libpvf/rockwell.c, GPL) achieved,
 * "identical to that of the DOS executables distributed by Rockwell".  The
 * coder below is that port, made reentrant: the arithmetic, its 16-bit
 * "decimal point adjustments", roundings and clips are kept as they are, down
 * to -1 * -1 = -1 (see rockwell.c's notes).  Silence codewords are not used:
 * Unimodem/V turns silence deletion off.
 *
 * The line itself is G.711 mu-law at 8000 Hz, in 20 ms frames on the
 * connection.
 *
 * Released under the GNU General Public License version 2 or later.  See
 * COPYING for more information.
 */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <86box/modem/modem_voice.h>

#ifndef M_PI
#    define M_PI 3.14159265358979323846
#endif

/* --------------------------------------------------------- Rockwell ADPCM */

#define RV_PNT98    32113  /* 0.98  */
#define RV_PNT012   393    /* 0.012 */
#define RV_PNT006   197    /* 0.006 */
#define RV_QDLMN    0x1F   /* 2.87 mV */
#define RV_DEMPCF   0x3333 /* 0.4 */
#define RV_PDEMPCF  0x3333 /* 0.4 */

static const int16_t rv_mul[3][16] = {
    { 0x3333, 0x199A, 0x199A, 0x3333 },
    { 0x3800, 0x2800, 0x1CCD, 0x1CCD, 0x1CCD, 0x1CCD, 0x2800, 0x3800 },
    { 0x4CCD, 0x4000, 0x3333, 0x2666, 0x1CCD, 0x1CCD, 0x1CCD, 0x1CCD,
      0x1CCD, 0x1CCD, 0x1CCD, 0x1CCD, 0x2666, 0x3333, 0x4000, 0x4CCD }
};

/* Inverse quantiser multipliers: unsigned in Rockwell's table, used as
   signed. */
static const int16_t rv_zeta[3][16] = {
    { (int16_t) 0xCFAE, (int16_t) 0xF183, 0x0E7D, 0x3052 },
    { (int16_t) 0xBB23, (int16_t) 0xD4FE, (int16_t) 0xE7CF, (int16_t) 0xF828, 0x07D8, 0x1831, 0x2B02, 0x44DD },
    { (int16_t) 0xA88B, (int16_t) 0xBDCB, (int16_t) 0xCC29, (int16_t) 0xD7CF, (int16_t) 0xE1D8, (int16_t) 0xEAFB,
      (int16_t) 0xF395, (int16_t) 0xFBE4, 0x041C, 0x0C6B, 0x1505, 0x1E28, 0x2831, 0x33C7, 0x4235, 0x5775 }
};

static const int16_t rv_qdelay[3] = { 0x54C4, 0x3B7A, 0x2ED5 }; /* 2.01 V, 1.41 V, 1.11 V */

static const int16_t rv_cd[3][7] = {
    { 0x1F69 },
    { 0x1005, 0x219A, 0x37F0 },
    { 0x0843, 0x10B8, 0x1996, 0x232B, 0x2DFC, 0x3B02, 0x4CD6 }
};
static const int rv_cd_len[3] = { 1, 3, 7 };

static int32_t
rv_clip16(int64_t a)
{
    return (a < -32768) ? -32768 : ((a > 32767) ? 32767 : (int32_t) a);
}

static int64_t
rv_clip32(int64_t a)
{
    return (a < INT32_MIN) ? INT32_MIN : ((a > INT32_MAX) ? INT32_MAX : a);
}

/* A product of two Q15 numbers with Rockwell's "adjustment": shifted left
   once, in 32 bits, wrapping as the 8086 does. */
static int32_t
rv_mul_adj(int32_t a, int32_t b)
{
    return (int32_t) ((uint32_t) (a * b) << 1);
}

/* The upper half, rounded on the lower: RV_round_32_into_16. */
static int16_t
rv_round16(int32_t x)
{
    const uint32_t u = (uint32_t) x;

    return (int16_t) (uint16_t) ((u >> 16) + ((u >> 15) & 1));
}

static int16_t
rv_hiword(int64_t sum)
{
    return (int16_t) (uint16_t) ((uint32_t) sum >> 16);
}

/* Linear predictor coefficient update (RV_pzPred). */
static void
rv_pz_pred(rv_adpcm_t *s, int16_t cx)
{
    int di = s->qidx;

    for (int i = 0; i < 8; i++) {
        int32_t x = rv_round16(rv_mul_adj(s->pz[i], RV_PNT98));
        int32_t y = rv_round16(rv_mul_adj(cx, s->qdata[di]));

        x += (y < 0 ? -1 : 1) * (i < 2 ? RV_PNT012 : RV_PNT006);
        s->pz[i] = (int16_t) rv_clip16(x);
        di       = (di + 1) % 8;
    }
}

/* Sum of the pole and zero predictor's products (RV_XpzCalc). */
static int32_t
rv_xpz_calc(rv_adpcm_t *s, int16_t cx)
{
    int64_t sum = (int64_t) cx * 65536;

    s->di = s->qidx;
    for (int i = 0; i < 8; i++) {
        sum   = rv_clip32(sum + rv_mul_adj(s->pz[i], s->qdata[s->di]));
        s->di = (int16_t) ((s->di + 1) % 8);
    }
    s->di = (int16_t) ((s->qidx + 7) % 8);
    return (int32_t) sum;
}

static int16_t
rv_nu_next(const rv_adpcm_t *s, int16_t m, int32_t nu)
{
    int32_t x = rv_clip16((int64_t) rv_round16(rv_mul_adj(m, nu)) * 4);

    if (x < RV_QDLMN)
        x = RV_QDLMN;
    else if (x > s->qdelay_mx)
        x = s->qdelay_mx;
    return (int16_t) x;
}

void
rv_adpcm_init(rv_adpcm_t *s, int bps)
{
    if ((bps < 2) || (bps > 4))
        bps = 4;
    memset(s, 0, sizeof(*s));
    s->bps       = bps;
    s->last_nu   = RV_QDLMN;
    s->qdelay_mx = rv_qdelay[bps - 2];
    s->mul       = rv_mul[bps - 2];
    s->zeta      = rv_zeta[bps - 2];
    s->cd        = rv_cd[bps - 2];
    s->cd_len    = rv_cd_len[bps - 2];
}

/* RV_DecomOne. */
int16_t
rv_adpcm_decode_one(rv_adpcm_t *s, int code)
{
    const int16_t ax     = s->mul[code];
    const int16_t bx     = s->zeta[code];
    const int32_t nu_bak = s->last_nu;
    int64_t       sum;
    int16_t       si;

    s->last_nu   = rv_nu_next(s, ax, s->last_nu);
    s->new_qdata = (int16_t) rv_clip16((int64_t) rv_round16(rv_mul_adj(bx, nu_bak)) * 4);
    sum          = rv_xpz_calc(s, s->new_qdata); /* (Xp+z)(n) + Q(n) */
    si           = rv_hiword(sum);               /* Y(n) */
    /* The de-emphasis filter, undoing the coder's emphasis. */
    sum      = rv_clip32(sum + rv_mul_adj(RV_DEMPCF, s->dempz));
    s->dempz = rv_hiword(sum);
    rv_pz_pred(s, s->new_qdata);
    s->qdata[s->di] = si; /* drop b6, now a1 */
    s->qidx         = s->di;
    s->di           = (int16_t) ((s->di + 2) % 8);
    s->qdata[s->di] = s->new_qdata; /* drop a2, now b1 */
    return s->dempz;
}

/* RV_ComOne.  Do not tidy: every wrap and clip is Rockwell's. */
int
rv_adpcm_encode_one(rv_adpcm_t *s, int16_t ax)
{
    int64_t sum = rv_xpz_calc(s, 0);
    int16_t new_xpz;
    int16_t cx;
    int16_t bx;
    int32_t y;
    int     i;

    new_xpz = rv_hiword(sum);
    sum     = rv_clip32(sum + rv_mul_adj(s->adc16z1, RV_PDEMPCF));
    cx      = rv_hiword(sum);
    cx      = (int16_t) rv_clip16((int64_t) cx - ax);
    bx      = cx;
    if (bx < 0)
        bx = (int16_t) -bx;
    cx         = (int16_t) -cx;
    s->adc16z1 = ax;
    y          = s->last_nu;
    for (i = 0; i < s->cd_len; i++) {
        const int16_t dx = (int16_t) rv_clip16((int64_t) rv_round16(rv_mul_adj(s->cd[i], y)) * 4);

        if (bx < dx)
            break;
    }
    i++;
    if (cx < 0) {
        i -= s->cd_len;
        i--;
        i = -i;
    } else
        i += s->cd_len;
    s->last_nu = rv_nu_next(s, s->mul[i], y);
    cx         = (int16_t) rv_clip16((int64_t) rv_round16(rv_mul_adj(s->zeta[i], y)) * 4);
    rv_pz_pred(s, cx);
    s->qidx                          = (int16_t) ((s->qidx + 7) % 8);
    s->qdata[s->qidx]                = (int16_t) rv_clip16((int64_t) new_xpz + cx); /* drop b6, now a1 */
    s->qdata[(s->qidx + 2) % 8]      = cx;                                         /* drop a2, now b1 */
    return i;
}

size_t
rv_adpcm_decode(rv_adpcm_t *s, const uint8_t *in, size_t n, int16_t *out, size_t max)
{
    const uint32_t mask = (1u << s->bps) - 1;
    size_t         made = 0;

    for (size_t i = 0; i < n; i++) {
        s->bits |= (uint32_t) in[i] << s->nbits;
        s->nbits += 8;
        while ((s->nbits >= s->bps) && (made < max)) {
            out[made++] = rv_adpcm_decode_one(s, (int) (s->bits & mask));
            s->bits >>= s->bps;
            s->nbits -= s->bps;
        }
    }
    return made;
}

size_t
rv_adpcm_encode(rv_adpcm_t *s, const int16_t *in, size_t n, uint8_t *out, size_t max)
{
    size_t made = 0;

    for (size_t i = 0; i < n; i++) {
        s->bits |= (uint32_t) rv_adpcm_encode_one(s, in[i]) << s->nbits;
        s->nbits += s->bps;
        while ((s->nbits >= 8) && (made < max)) {
            out[made++] = (uint8_t) s->bits;
            s->bits >>= 8;
            s->nbits -= 8;
        }
    }
    return made;
}

size_t
rv_adpcm_flush(rv_adpcm_t *s, uint8_t *out, size_t max)
{
    if ((s->nbits <= 0) || (max == 0))
        return 0;
    out[0]   = (uint8_t) s->bits;
    s->bits  = 0;
    s->nbits = 0;
    return 1;
}

/* ------------------------------------------------------------ G.711 mu-law */

uint8_t
voice_ulaw_encode(int16_t x)
{
    int     sign = 0;
    int     mag  = x;
    int     exp  = 7;
    uint8_t u;

    if (mag < 0) {
        sign = 0x80;
        mag  = -mag;
    }
    if (mag > 32635)
        mag = 32635;
    mag += 0x84;
    for (int m = 0x4000; ((mag & m) == 0) && (exp > 0); m >>= 1)
        exp--;
    u = (uint8_t) (sign | (exp << 4) | ((mag >> (exp + 3)) & 0x0f));
    return (uint8_t) ~u;
}

int16_t
voice_ulaw_decode(uint8_t u)
{
    int mag;

    u   = (uint8_t) ~u;
    mag = ((((u & 0x0f) << 3) + 0x84) << ((u >> 4) & 7)) - 0x84;
    return (int16_t) ((u & 0x80) ? -mag : mag);
}

uint8_t
voice_alaw_encode(int16_t x)
{
    int     mag  = x;
    int     sign = 0x80;
    int     exp;
    uint8_t a;

    if (mag < 0) {
        sign = 0;
        mag  = -mag - 1;
    }
    mag >>= 3; /* 13 bits */
    if (mag > 0xfff)
        mag = 0xfff;
    if (mag < 32)
        a = (uint8_t) (mag >> 1);
    else {
        exp = 1;
        while ((mag >> (exp + 4)) > 1)
            exp++;
        a = (uint8_t) ((exp << 4) | ((mag >> exp) & 0x0f));
    }
    return (uint8_t) ((a | sign) ^ 0x55);
}

int16_t
voice_alaw_decode(uint8_t a)
{
    int mag;
    int exp;

    a ^= 0x55;
    exp = (a >> 4) & 7;
    mag = ((a & 0x0f) << 4) + 8;
    if (exp > 0)
        mag = (mag + 0x100) << (exp - 1);
    return (int16_t) ((a & 0x80) ? mag : -mag);
}

/* ------------------------------------------------------------- resampling */

void
voice_resampler_init(voice_resampler_t *r, uint32_t from, uint32_t to)
{
    memset(r, 0, sizeof(*r));
    r->from = from ? from : 1;
    r->to   = to ? to : 1;
}

size_t
voice_resample(voice_resampler_t *r, const int16_t *in, size_t n, int16_t *out, size_t max)
{
    size_t made = 0;

    for (size_t i = 0; i < n; i++) {
        if (!r->primed) {
            r->prev   = in[i];
            r->next   = in[i];
            r->primed = 1;
            continue;
        }
        r->prev = r->next;
        r->next = in[i];
        /* Output samples fall every `from` units of 1/to between prev and
           next, which are `to` units apart. */
        while ((r->acc < r->to) && (made < max)) {
            out[made++] = (int16_t) (r->prev + (((int32_t) (r->next - r->prev) * (int32_t) r->acc) / (int32_t) r->to));
            r->acc += r->from;
        }
        r->acc -= r->to;
    }
    return made;
}

/* -------------------------------------------------------------------- tones */

int
voice_dtmf_freqs(char digit, double *f1, double *f2)
{
    static const char  *keys  = "123A456B789C*0#D";
    static const double row[] = { 697, 770, 852, 941 };
    static const double col[] = { 1209, 1336, 1477, 1633 };
    const char         *k;

    if ((digit >= 'a') && (digit <= 'd'))
        digit = (char) (digit - 'a' + 'A');
    k = (digit != '\0') ? strchr(keys, digit) : NULL;
    if (k == NULL)
        return 0;
    *f1 = row[(k - keys) / 4];
    *f2 = col[(k - keys) % 4];
    return 1;
}

void
voice_tone_start(voice_tone_t *t, double f1, double f2, uint32_t ms, double amp)
{
    t->f1   = f1;
    t->f2   = f2;
    t->ph1  = 0.0;
    t->ph2  = 0.0;
    t->left = ms * (VOICE_LINE_RATE / 1000);
    t->amp  = amp;
}

int
voice_tone_mix(voice_tone_t *t, int16_t *buf, size_t n)
{
    for (size_t i = 0; (i < n) && (t->left > 0); i++, t->left--) {
        double v = sin(t->ph1);
        int32_t x;

        if (t->f2 > 0.0)
            v = 0.5 * (v + sin(t->ph2));
        t->ph1 += 2.0 * M_PI * t->f1 / VOICE_LINE_RATE;
        t->ph2 += 2.0 * M_PI * t->f2 / VOICE_LINE_RATE;
        if (t->ph1 > 2.0 * M_PI)
            t->ph1 -= 2.0 * M_PI;
        if (t->ph2 > 2.0 * M_PI)
            t->ph2 -= 2.0 * M_PI;
        x      = buf[i] + (int32_t) (v * t->amp);
        buf[i] = (int16_t) ((x > 32767) ? 32767 : ((x < -32768) ? -32768 : x));
    }
    return t->left > 0;
}

/* ------------------------------------------------------------------ silence */

void
voice_silence_init(voice_silence_t *s, int sensitivity)
{
    /* Rockwell's #VSS: 0 hears silence only in near-quiet, 3 already in a
       murmur.  A mean level, out of 32768. */
    static const int thresholds[4] = { 60, 150, 300, 600 };

    memset(s, 0, sizeof(*s));
    s->threshold = thresholds[(sensitivity < 0) ? 0 : ((sensitivity > 3) ? 3 : sensitivity)];
}

void
voice_silence_feed(voice_silence_t *s, const int16_t *buf, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        s->sum += abs(buf[i]);
        if (++s->count < (VOICE_LINE_RATE / 100))
            continue;
        if ((s->sum / s->count) < s->threshold)
            s->quiet_ms += 10;
        else {
            s->quiet_ms = 0;
            s->heard_ms += 10;
        }
        s->sum   = 0;
        s->count = 0;
    }
}

/* ------------------------------------------------------------------- frames */

size_t
voice_frame(uint8_t *out, int type, const uint8_t *payload, size_t len)
{
    if (len > VOICE_FRAME_MAX)
        len = VOICE_FRAME_MAX;
    out[0] = (uint8_t) type;
    out[1] = (uint8_t) len;
    out[2] = (uint8_t) (len >> 8);
    if (len > 0)
        memcpy(&out[VOICE_FRAME_HDR], payload, len);
    return VOICE_FRAME_HDR + len;
}

int
voice_deframe(voice_deframer_t *d, const uint8_t *in, size_t n,
              void (*fn)(int type, const uint8_t *payload, size_t len, void *priv), void *priv)
{
    for (size_t i = 0; i < n; i++) {
        d->buf[d->have++] = in[i];
        if (d->have < VOICE_FRAME_HDR)
            continue;
        {
            const size_t len = d->buf[1] | ((size_t) d->buf[2] << 8);

            if (len > VOICE_FRAME_MAX) {
                d->have = 0;
                return -1;
            }
            if (d->have < (VOICE_FRAME_HDR + len))
                continue;
            fn(d->buf[0], &d->buf[VOICE_FRAME_HDR], len, priv);
            d->have = 0;
        }
    }
    return 0;
}
