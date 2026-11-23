/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Aureal Vortex 2 (AU8830) chip model.
 *
 *          Everything here was established by tracing a real SQ2500
 *          under the stock Windows 98 driver (4.06.2048): every register
 *          access logged through QEMU/VFIO with the card's line output
 *          recorded alongside, plus host-side register pokes. Register
 *          names follow the Linux ALSA au88x0 driver. Where a behaviour
 *          is a working hypothesis rather than a measurement, the comment
 *          says so.
 *
 *          Signal flow. The chip moves samples over an internal "ADB" bus
 *          according to a route table: each route copies one source
 *          address (DMA channel, sample-rate converter output, mixer
 *          output, wavetable output, A3D output, crosstalk canceller or EQ
 *          output, codec input) to one sink address (converter input,
 *          mixer input, A3D input, canceller input, EQ input, codec
 *          output, capture DMA). The model runs the whole graph at 48 kHz
 *          in blocks of V2_BLOCK frames, evaluating each source on demand;
 *          a feedback loop in the graph gets the previous block's values.
 *
 * Authors: Z2k Retro and contributors.
 *
 *          Copyright 2026 Z2k Retro.
 */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snd_vortex2_core.h"
#include "snd_vortex2_tables.h"
#include "snd_vortex2_srctab.h"

#define B V2_BLOCK

/* ------------------------------------------------------------------ */
/* Register map                                                        */
/* ------------------------------------------------------------------ */

/* Wavetable: two banks of 32 voices, 0x8000 apart. */
#define WT_BANK_STRIDE 0x8000
#define WT_DSREG       0x0008 /* direct-stream bits, one per voice */
#define WT_STEREO      0x0080 /* + (voice / 2) * 4 */
#define WT_RUN         0x0180 /* + voice * 4 */
#define WT_MRAMP       0x000c /* mute-ramp rate: added to a 32-bit phase each sample */
#define WT_GMODE       0x0010 /* gain-mode bits, one per voice */
#define WT_ARAMP       0x0014 /* attack-ramp rate, likewise */
#define WT_MUTE        0x0100 /* + voice * 4: 1 starts a smooth mute */
#define WT_PARM        0x0200 /* + voice * 0x10 + n * 4 */

#define FIFO_WTCTRL    0x16000 /* + channel * 4, 64 channels */
#define WT_FIFO_RAM    0x10000 /* + channel * 0x100: each channel's 64-word FIFO */
#define FIFO_ADBCTRL   0x16100 /* + channel * 4, 32 channels */
#define FIFO_VALID     0x00000010
#define FIFO_EMPTY     0x00000020
#define FIFO_RDY2      0x00000040 /* idle channel set up with bit 13: see v2_read */
#define FIFO_U1        0x00040000

#define A3D_BASE       0x18000
#define A3D_END        0x20000

#define MIX_INVOL_B    0x20000 /* current: + ((mix << 5) + in) * 4 */
#define MIX_VOL_B      0x20800 /* current: + mix * 4 */
#define MIX_INVOL_A    0x21000 /* target */
#define MIX_VOL_A      0x21800 /* target */
#define MIX_ENIN       0x21a00 /* + ((mix << 3) + (in >> 2)) * 4, bit in & 3 */

#define XT_BASE        0x24000
#define XT_END         0x24680
#define XT_GAIN_EQL    0x24538
#define XT_GAIN_EQR    0x24540
#define XT_GAIN_XTL    0x24548
#define XT_GAIN_XTR    0x24550
#define XT_CTRL        0x24660

#define SRC_CONVRATIO  0x26e40 /* + src * 4, Q14 input rate / 48 kHz */

#define WTDMA_BUFBASE  0x27000
#define WTDMA_BUFCFG   0x27600
#define WTDMA_CTRL     0x27900
#define WTDMA_START    0x27b00
#define WTDMA_STAT     0x27d00
#define ADBDMA_BUFBASE 0x27400
#define ADBDMA_BUFCFG  0x27800
#define ADBDMA_CTRL    0x27a00
#define ADBDMA_START   0x27c00
#define ADBDMA_STAT    0x27e00
#define WTDMA_PAGES    0x27a80 /* 4 words, 2 bits per channel, MSB first */
#define ADBDMA_PAGES   0x27a90 /* 2 words */

#define ADB_RTBASE     0x28000
#define ADB_CHNBASE    0x282b4
#define ADB_NCHN       24
#define ADB_SR         0x28400

#define CODEC_CTRL     0x29184
#define CODEC_IO       0x29188
#define SMP_TIME       0x29198
#define SMP_TIMER      0x2919c

#define IRQ_SOURCE     0x2a000
#define IRQ_CTRL       0x2a004
#define IRQ_STAT       0x2a008
#define CHIP_CTRL      0x2a00c
#define CTRL_IRQ_EN    0x00004000

#define IRQ_PCMOUT     0x00000020 /* DMA page completed (interrupt-enabled channel) */
#define IRQ_TIMER      0x00001000
#define IRQ_ALWAYS     0x00004000 /* set in every read during operation; meaning unknown */

#define EQ_BASE        0x2b000
#define EQ_END         0x2b450
#define EQ_BYPASS_L    0x2b3d4
#define EQ_BYPASS_R    0x2b3ec

#define DECODE_END     0x2c000 /* above: not decoded, reads return the last bus value */

/* ADB source addresses */
#define AS_SRCOUT  0x20
#define AS_MIXOUT  0x30
#define AS_WT0     0x40
#define AS_XTOUT   0x66
#define AS_CODECIN 0x70 /* codec ADC, left and right */
#define AS_EQOUT   0x7e
#define AS_WT1     0x80
#define AS_A3DOUT  0xa6
/* ADB sink addresses */
#define AK_DMAB    0x20
#define AK_SRCIN   0x40
#define AK_MIXIN   0x50
#define AK_A3DIN   0x70
#define AK_CODEC   0x88
#define AK_XTIN    0x96
#define AK_EQIN    0xa0

#define NR_ADBDMA  32
#define NR_WTDMA   64
#define NR_SRC     16
#define NR_MIXIN   32
#define NR_MIXOUT  16
#define NR_A3D     16
#define MAX_ROUTES 256
#define MAX_PER_SINK 8

#define HRTF_TAPS  56
#define ITD_LEN    128 /* delay line, power of two */
#define FIFO_LEN   512
#define FRAME_RING 512 /* frames kept per DMA channel: covers a block at the highest ratio */

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

enum { DMA_IDLE = 0, DMA_RUN, DMA_STALL, DMA_END };

typedef struct v2_dma_t {
    uint8_t  is_wt;
    uint8_t  ch;
    uint8_t  state;
    uint8_t  page;
    uint8_t  touched; /* driver has started it since reset */
    uint8_t  stopped; /* stopped by the driver, FIFO playing out */
    uint32_t pos;     /* byte offset in the current page */
    uint8_t  fifo[FIFO_LEN];
    uint32_t frd, fn;
    float    frame[FRAME_RING][4]; /* recent frames, shared by the routes reading them */
    uint32_t frame_no;
    uint32_t rate_acc; /* capture: phase accumulator (16.16) */
    uint32_t since_fetch; /* samples taken from the FIFO since the last fetch */
    uint32_t drain_end;   /* glide_frames when a stopped voice's FIFO has played out */
    uint8_t  primed;      /* initial prefetch done since the stream started */
} v2_dma_t;

typedef struct v2_route_t {
    uint8_t  src, sink, idx;
    uint32_t cnt; /* frames this route has taken from a DMA source */
} v2_route_t;

#define SRC_TAPS   27  /* converter FIR length in input samples */
#define SRC_PHASES 16384 /* the converter's 14-bit time fraction */
#define SRC_PAD    28  /* SRC_TAPS rounded up to a multiple of 4 (leading zero taps) */

typedef struct v2_src_t {
    double   phase;
    int32_t  hist[2 * SRC_PAD]; /* 16-bit input samples, circular, every sample written twice */
    int      hpos;
    int      zrun;              /* consecutive silent input samples */
    uint32_t last_run;          /* block (glide_frames + 1) it last ran in */
    uint32_t nin;
} v2_src_t;

typedef struct v2_wtv_t {
    double   phase;
    float    hist[4];
    float    fs[4];  /* filter state: the four one-pole stages */
    int      fbyte;  /* cut-off byte the coefficient below was made for, -1 = none */
    float    fcoef;
    uint32_t nin;
    int      ramp; /* the gain ramp under way: 3 attack, 6 mute, 0 none */
    float    env;  /* attack: fraction of the target gain reached */
    float    rate; /* per-sample step of the ramp under way */
    float    mpos; /* mute: fraction of the starting level left */
    float    mlvl; /* the voice's gain as it last played, where a mute starts from */
} v2_wtv_t;

typedef struct v2_a3d_t {
    int   loaded;
    float h[2][HRTF_TAPS]; /* current taps, [ear][tap], ear 0 = left */
    float gain;
    float co[5]; /* a1, a2, b0, b1, b2 */
    float itd[2];
    float x1, x2, y1, y2;
    float dl[2][ITD_LEN];
    int   dlpos;
    float fh[2][HRTF_TAPS - 1 + B]; /* FIR history */
    int   active;
    int   zrun;  /* consecutive silent input samples */
    int   settled; /* every glide has reached its target: nothing to update */
    int   quiet; /* silent input and empty filters: the signal path is skipped */
} v2_a3d_t;

typedef struct v2_xt_t {
    float  co[4][5][5]; /* [EQ L, EQ R, XT L, XT R][section][a1 a2 b0 b1 b2] */
    float  k[4], sh[4]; /* direct gain (shift included) and 2^shift */
    float  gin[10];
    int    delay[2];
    double y[4][5][2];
    float  x1[4], x2[4];
    float  dl[2][32];
    int    dpos;
} v2_xt_t;

typedef struct v2_eq_t {
    float co[2][10][5];
    float st[2][10][4];
    float gain[2][10];
    float bypass[2];
} v2_eq_t;

struct v2_chip_t {
    v2_host_t host;
    uint32_t  regs[V2_REG_SIZE / 4];
    uint8_t   ro[V2_REG_SIZE / 4 / 8];
    uint32_t  last_bus;

    /* interrupts and timer */
    uint32_t irq_src;
    int      irq_level;
    int      timer_fired;
    uint32_t timer_last;
    uint32_t timer_t0;
#ifdef ENABLE_VORTEX2_LOG
    uint32_t timer_missed;
    uint32_t n_fired, n_acked, n_irq_raise, n_zero_timer;
    double   lr[4][3]; /* stereo diagnostics: sum LL, RR, LR at A3D out, canceller out, EQ out, output */
    uint32_t status_frames;
#endif

    v2_dma_t adb[NR_ADBDMA];
    v2_dma_t wt[NR_WTDMA];

    /* routing graph */
    int        graph_dirty;
    v2_route_t route[MAX_ROUTES];
    int        nroutes;
    uint8_t    sink_n[256];
    uint16_t   sink_r[256][MAX_PER_SINK];
    uint8_t    dma_nroutes[NR_ADBDMA];

    v2_src_t src[NR_SRC];
    v2_wtv_t wtv[NR_WTDMA];
    v2_a3d_t a3d[NR_A3D];
    uint8_t  a3d_cur_dirty[NR_A3D];
    uint8_t  a3d_tgt_dirty[NR_A3D]; /* a target or glide-rate word was written */
    int16_t  src_owner[NR_SRC]; /* source each converter last converted */
    float    adc[2][B];         /* codec ADC input for this block */
    int      adc_done;
#ifdef ENABLE_VORTEX2_LOG
    uint8_t  a3d_used[NR_A3D];
    uint32_t graph_hash;
#endif
    uint32_t glide_frames; /* sample count for the 64-sample glide tick */
    uint8_t  mix_sign[NR_MIXOUT * 32 + NR_MIXOUT]; /* sign of each gain's last sample */
    uint8_t  mix_ran[NR_MIXOUT];
#ifdef ENABLE_VORTEX2_LOG
    uint32_t log_clk_last; /* log clock: last 32-bit reading, and the wraps so far */
    uint64_t log_clk_wraps;
#endif
    uint32_t wt_routed[2]; /* voices whose own ADB source is routed somewhere */
    uint32_t wt_aacc[2];   /* attack- and mute-ramp phase accumulators, per bank */
    uint32_t wt_macc[2];
    uint32_t wt_gmode[2];  /* gain-mode bits: 1 = the voice's gain ramp is done */

    int     xt_dirty;
    v2_xt_t xt;

    int     eq_dirty;
    v2_eq_t eq;
#ifdef ENABLE_VORTEX2_LOG
    char    eq_logged[512];
#endif

    /* per-block evaluation */
    float   sbuf[256][B];
    uint8_t sstate[256];
    float   kbuf[256][B];
    uint8_t kstate[256];
    int     wt_done[2];
    float   wt_direct[2][32][B];
    float   wt_bus[2][6][B];
    int     a3d_done, xt_done, eq_done;
    float   a3d_out[8][B];
    float   xt_out[2][B];
    float   eq_out[2][B];

    uint32_t dbg;
};

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

#define R(a)  (c->regs[((a) & (V2_REG_SIZE - 1)) >> 2])
#define LO16(w) ((int16_t) ((w) & 0xffff))
#define HI16(w) ((int16_t) ((w) >> 16))

#ifdef ENABLE_VORTEX2_LOG
/* Append to a log line, stopping at the end of the buffer (snprintf
   returns the length it wanted, which may be more than fitted). */
static int
v2_cat(char *buf, size_t size, int n, const char *fmt, ...)
{
    va_list ap;
    int     w;

    if (n < 0 || (size_t) n >= size)
        return (size > 0) ? (int) size - 1 : 0;
    va_start(ap, fmt);
    w = vsnprintf(buf + n, size - (size_t) n, fmt, ap);
    va_end(ap);
    if (w < 0)
        return n;
    return ((size_t) n + (size_t) w < size) ? n + w : (int) size - 1;
}

static void
v2_log(v2_chip_t *c, const char *fmt, ...)
{
    char    buf[256];
    va_list ap;

    if (!c->host.log)
        return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    c->host.log(c->host.priv, buf);
}
#endif

static inline float
clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

/* Wavetable voices' 4-point interpolation: the cubic Lagrange polynomial
   through h[0..3], evaluated between h[1] and h[2] (Aureal documents a
   "4 point interpolation"). Measured on a real SQ2500 (run 18): tones at
   0.2 x the voice's rate on a wavetable voice, at 8 to 44.1 kHz, left
   images that match this kernel line by line, within 2.2 dB rms over 22
   lines at 11.025 kHz (median 2.2 dB over all 46), against 10 dB for
   Catmull-Rom, which the model used before and which leaves the images
   5-20 dB too low. Read digitally (lastprobe interp, 2026-11-17): an
   impulse at step 1 with the pitch phase set to 64 fractions gave this
   kernel's four weights at each, times 0x3330 / 0x4000, within 1.6 LSB of
   13,104, and the driver's 44.1 kHz step gave the impulse response this
   predicts within 5 LSB; so the 44.1 kHz image line that recorded 6 dB
   under the model is not in the chip's arithmetic. */
static inline float
interp4(const float *h, float t)
{
    float tm1 = t - 1.0f, tm2 = t - 2.0f, tp1 = t + 1.0f;
    return -t * tm1 * tm2 * (1.0f / 6.0f) * h[0]
           + tp1 * tm1 * tm2 * 0.5f * h[1]
           - tp1 * t * tm2 * 0.5f * h[2]
           + tp1 * t * tm1 * (1.0f / 6.0f) * h[3];
}

/* Sample-rate converter filter: the card's own coefficients. The
   converter interpolates with a 27-point windowed sinc (13 input samples
   each side of the centre, the driver's samples-per-wing setting), and its
   time fraction is 14 bits. The coefficients were read digitally off a real
   SQ2500 (srcprobe, 2026-11-17): with the card idle on the host, a single
   impulse was routed FIFO -> converter -> output double buffer at a
   conversion ratio of 1 and the converter's time fraction set to each of
   its 16384 values in turn, which gives the 26 taps at that phase exactly
   as the chip uses them (snd_vortex2_srctab.h). They are 14-bit fractions,
   and each output is (sum of coefficient x sample + 0x2200) >> 14, rounded
   to nearest with an extra 1/32 LSB (srcprobe ampsweep: 576 impulse heights
   at two phases, every central-tap output exact). The model does the same
   in integers on 16-bit input samples (converters only take DMA streams),
   so a stream comes out bit for bit as on the card. Past full scale (srcprobe clip, 2026-11-17) the output
   saturates at 32767 / -32768, but the sum itself wraps first: a sum
   beyond +-65536 (2.26 x 32767 at the half-sample phase) comes out with
   the wrong sign and then saturates, as if the accumulator held 31 bits. A Kaiser window (beta 7.70 over a
   half-width of 12.85 samples) fits the table to 0.65 LSB; Aureal
   described the converter as a Kaiser-windowed sinc. src_fir[p] holds the
   taps for a fractional position p / SRC_PHASES after tap
   (SRC_TAPS - 1) / 2; the chip uses the fraction as it stands, without
   interpolating between phases. */
static int16_t src_fir[SRC_PHASES][SRC_PAD]; /* coefficients (16384 = 1.0) in [SRC_PAD - SRC_TAPS ..]; 0.9 MB */

static void
init_src_fir(void)
{
    static int16_t t[V2_SRCTAB_PHASES / 2 + 1][V2_SRCTAB_TAPS];
    int            row = 1, tap = 0;

    memset(t[0], 0, sizeof(t[0]));
    t[0][12] = 16384;
    for (int l = 0; v2_srctab_delta[l] && row <= V2_SRCTAB_PHASES / 2; l++)
        for (const char *q = v2_srctab_delta[l]; *q && row <= V2_SRCTAB_PHASES / 2; q++) {
            int code = *q - 'A';
            for (int h = 0; h < 2; h++) {
                int d = (h ? code % 5 : code / 5) - 2;
                t[row][tap] = (int16_t) (t[row - 1][tap] + d);
                if (++tap == V2_SRCTAB_TAPS) {
                    tap = 0;
                    row++;
                }
            }
        }
    for (int p = 0; p < SRC_PHASES; p++) {
        for (int k = 0; k < SRC_PAD; k++)
            src_fir[p][k] = 0;
        /* tap i (input sample i - 12 from the one before the output point)
           is model tap k = i + 1 of SRC_TAPS, stored at SRC_PAD - SRC_TAPS + k */
        for (int i = 0; i < V2_SRCTAB_TAPS; i++) {
            int v = (p <= V2_SRCTAB_PHASES / 2) ? t[p][i] : t[V2_SRCTAB_PHASES - p][V2_SRCTAB_TAPS - 1 - i];
            src_fir[p][SRC_PAD - SRC_TAPS + i + 1] = (int16_t) v;
        }
    }
}

/* hist: the last SRC_PAD input samples, oldest first, as 16-bit values. The
   chip's arithmetic in integers: products summed exactly, rounded with
   0x2200, the 31-bit accumulator's wrap, then saturation (see above). */
static inline float
src_interp(const int32_t *hist, double phase)
{
    int            p = (int) (phase * SRC_PHASES);
    const int16_t *h0;
    int64_t        a[4] = { 0, 0, 0, 0 };

    if (p < 0)
        p = 0;
    else if (p >= SRC_PHASES)
        p = SRC_PHASES - 1;
    h0 = src_fir[p];
    /* Four independent lanes, so the compiler can vectorise. */
    for (int k = 0; k < SRC_PAD; k += 4)
        for (int j = 0; j < 4; j++)
            a[j] += (int64_t) h0[k + j] * hist[k + j];
    int64_t y = ((a[0] + a[1]) + (a[2] + a[3]) + 0x2200) >> 14;
    y = ((y + 65536) & 131071) - 65536; /* the 31-bit accumulator wraps */
    if (y > 32767)
        y = 32767;
    else if (y < -32768)
        y = -32768;
    return (float) y / 32768.0f;
}

/* Mixer gain byte: a signed 5-bit exponent (bits 7-3) and a 3-bit
   mantissa (bits 2-0), the fixed-point number .1MMM x 2^EXP: 0x08 =
   unity, 0x2f = .1111 x 2^5 = 30 (+29.5 dB) is the largest gain, 0x80 =
   .1 x 2^-16 (-102.4 dB) is mute; steps average 0.75 dB (Aureal's
   description of the mixer and the full table of its gains, relayed with
   a register map of the chip;
   the real card's measured gains at 0x08, 0x00, 0xf8, 0xe8 and 0xc8 fit
   it). Codes above 0x2f are outside the documented range; the model
   holds them at exponent 5. Half-Life's 0x1f on the second wavetable
   bank's send buses is +17.5 dB and makes up for those buses' headroom
   (see wt_bank_run). Mute is taken as zero: -102 dB is below the
   16-bit output's last bit. mix_mant_tab and mix_exp_tab split the
   output gains for the output stage (see mix_run). */
static void  init_wt_gain(void);
static float mix_gain_tab[256];
static float mix_mant_tab[256], mix_shift_tab[256];

static void
init_tables(void)
{
    static int done = 0;

    if (done)
        return;
    init_src_fir();
    init_wt_gain();
    for (int b = 0; b < 256; b++) {
        if (b == 0x80)
            mix_gain_tab[b] = mix_mant_tab[b] = mix_shift_tab[b] = 0.0f;
        else {
            int e = (int8_t) b >> 3, m = b & 7;
            if (e > 5)
                e = 5;
            mix_mant_tab[b]  = (float) (0.5 + m / 16.0);
            mix_shift_tab[b] = (float) ldexp(1.0, e);
            mix_gain_tab[b]  = mix_mant_tab[b] * mix_shift_tab[b];
        }
    }
    done = 1;
}

/* ------------------------------------------------------------------ */
/* Interrupts                                                          */
/* ------------------------------------------------------------------ */

#ifdef ENABLE_VORTEX2_LOG
static double v2_now(v2_chip_t *c);
#endif

static void
update_irq(v2_chip_t *c)
{
    int level = (R(CHIP_CTRL) & CTRL_IRQ_EN) && (c->irq_src & R(IRQ_CTRL));

    if (level != c->irq_level) {
        c->irq_level = level;
#ifdef ENABLE_VORTEX2_LOG
        if (level)
            c->n_irq_raise++;
#endif
        if (c->host.set_irq)
            c->host.set_irq(c->host.priv, level);
    }
}

static void
raise_irq(v2_chip_t *c, uint32_t bits)
{
    c->irq_src |= bits;
    update_irq(c);
}

void
v2_ext_irq(v2_chip_t *c, uint32_t bits, int set)
{
    if (set)
        c->irq_src |= bits;
    else
        c->irq_src &= ~bits;
    update_irq(c);
}

void
v2_timer_expired(v2_chip_t *c)
{
#ifdef ENABLE_VORTEX2_LOG
    if (c->timer_fired) {
        if (++c->timer_missed == 50)
            v2_log(c, "Vortex2: [%9.3f] timer interrupts not being serviced (IRQ_CTRL %08x CTRL %08x)\n",
                   v2_now(c), R(IRQ_CTRL), R(CHIP_CTRL));
    } else
        c->timer_missed = 0;
    c->n_fired++;
#endif
    c->timer_fired = 1;
    raise_irq(c, IRQ_TIMER);
}

/* ------------------------------------------------------------------ */
/* DMA engines                                                         */
/*                                                                     */
/* Each channel has four page addresses and two configuration words.   */
/* Per page: a 12-bit size (bytes - 1) and a nibble:                   */
/*   bit 3  meaning unknown (not needed to play the page)              */
/*   bit 2  carry on to the next page when this one is done            */
/*   bits 1-0  next page                                               */
/* Playback starts at the page named in the START register, plays each */
/* page reached, and ends after a page without "carry on". Streaming   */
/* drivers keep the page they have not refilled yet as the one without */
/* "carry on"; one-shot sounds (DOS Sound Blaster blocks, wavetable    */
/* voices) end on purpose. Decoded from the Windows driver's traces: a */
/* wavetable voice started on page 1 = 0x4, page 0 = 0xe, page 2 = 0x7 */
/* was seen playing page 2.                                            */
/* ------------------------------------------------------------------ */

static inline uint32_t dma_base(const v2_dma_t *d) { return (d->is_wt ? WTDMA_BUFBASE : ADBDMA_BUFBASE) + d->ch * 0x10; }
static inline uint32_t dma_cfg(const v2_dma_t *d) { return (d->is_wt ? WTDMA_BUFCFG : ADBDMA_BUFCFG) + d->ch * 8; }
static inline uint32_t dma_ctrl(const v2_dma_t *d) { return (d->is_wt ? WTDMA_CTRL : ADBDMA_CTRL) + d->ch * 4; }
static inline uint32_t dma_start(const v2_dma_t *d) { return (d->is_wt ? WTDMA_START : ADBDMA_START) + d->ch * 4; }
static inline uint32_t dma_fifo(const v2_dma_t *d) { return (d->is_wt ? FIFO_WTCTRL : FIFO_ADBCTRL) + d->ch * 4; }

static inline int
dma_nibble(v2_chip_t *c, const v2_dma_t *d, int page)
{
    uint32_t w = R(dma_cfg(d) + ((page & 2) ? 4 : 0));
    return (w >> ((page & 1) ? 24 : 28)) & 0xf;
}

static inline uint32_t
dma_size(v2_chip_t *c, const v2_dma_t *d, int page)
{
    uint32_t w = R(dma_cfg(d) + ((page & 2) ? 4 : 0));
    return ((w >> ((page & 1) ? 0 : 12)) & 0xfff) + 1;
}

static inline int
dma_fmt(v2_chip_t *c, const v2_dma_t *d)
{
    return (R(dma_ctrl(d)) >> 14) & 0xf;
}

static inline int
dma_bps(int fmt)
{
    return (fmt == 1 || fmt == 2 || fmt == 3) ? 1 : 2;
}

#ifdef ENABLE_VORTEX2_LOG
static double
v2_now(v2_chip_t *c)
{
    /* The 12.288 MHz counter wraps every 349.5 s, as on the real chip; the
       log keeps counting past the wrap. */
    uint32_t now;

    if (!c->host.clock)
        return 0.0;
    now = c->host.clock(c->host.priv);
    if (now < c->log_clk_last)
        c->log_clk_wraps++;
    c->log_clk_last = now;
    return ((double) (c->log_clk_wraps << 32) + now) / 12288000.0;
}

static void
dma_log_state(v2_chip_t *c, v2_dma_t *d, const char *what)
{
    if (d->is_wt)
        return; /* wavetable one-shots end all the time */
    v2_log(c, "Vortex2: [%9.3f] ADB DMA %2d %s, page %d pos %03x, cfg %08x %08x, ctrl %08x\n",
           v2_now(c), d->ch, what, d->page, d->pos, R(dma_cfg(d)), R(dma_cfg(d) + 4), R(dma_ctrl(d)));
}
#endif

static void
dma_enter_page(v2_chip_t *c, v2_dma_t *d, int page)
{
    d->page  = page & 3;
    d->pos   = 0;
    d->state = DMA_RUN;
}

static void
dma_page_end(v2_chip_t *c, v2_dma_t *d)
{
    int nib = dma_nibble(c, d, d->page);

    if (R(dma_ctrl(d)) & 0x1000)
        raise_irq(c, IRQ_PCMOUT);
    if (nib & 4)
        dma_enter_page(c, d, nib & 3);
    else {
        d->state = DMA_END;
#ifdef ENABLE_VORTEX2_LOG
        dma_log_state(c, d, "ended (page without carry-on)");
#endif
        /* The card drops the FIFO's run bits when a stream ends (seen as
           0x42070 -> 0x2680 in the DOS Sound Blaster trace). */
        R(dma_fifo(d)) &= ~(FIFO_VALID | FIFO_U1);
    }
}

/* Start a stream. `cont`: the stream had reached a page without carry-on
   and the driver restarts it on a new page while its FIFO still holds the
   old page's last samples. Aureal's Sound Blaster emulation streams this
   way, in 128-byte pages each started from the end interrupt of the one
   before (Duke Nukem 3D in a Windows DOS box: 17,000 restarts a minute). The
   FIFO plays on into the new page, as the samples it holds were fetched
   for this stream; dropping them lost 56 of every 128 bytes and reset the
   converter each time, which played the sound too fast and distorted.
   (Real card, DOS Sound Blaster test: at each page's interrupt the stream
   is still draining, 10 to 26 samples into its FIFO, and it is restarted
   before the FIFO empties.) */
static void
dma_begin(v2_chip_t *c, v2_dma_t *d, int cont)
{
    d->touched  = 1;
    /* Either way the card fills its FIFO from the new page before it
       plays on (a continued stream whose FIFO had run low otherwise kept
       it low, and the next page end left almost no time to restart:
       Duke Nukem 3D's pages then alternated 6 and 3 ms, with dropouts). */
    d->primed = 0;
    if (!cont) {
        d->frd      = 0;
        d->fn       = 0;
        d->frame_no = 0;
        d->rate_acc = 0;
    }
    d->page  = (R(dma_start(d)) >> ((15 - (d->ch & 15)) * 2)) & 3;
    d->pos   = R(dma_ctrl(d)) & 0xfff;
    d->state = DMA_RUN;
    d->stopped = 0;
#ifdef ENABLE_VORTEX2_LOG
    dma_log_state(c, d, "started");
#endif
}

/* Pull bytes from guest memory into the FIFO until it holds `need`. */
static void
dma_fill(v2_chip_t *c, v2_dma_t *d, uint32_t need)
{
    uint8_t tmp[128];
    int     guard = 8;

    while (d->fn < need && guard-- > 0) {
        if (d->state == DMA_END) {
            if (d->stopped)
                return;
            /* Robustness: if the driver links the finished page after
               all (it was late), carry on instead of staying silent. */
            int nib = dma_nibble(c, d, d->page);
            if (!(nib & 4))
                return;
            R(dma_fifo(d)) |= FIFO_VALID | FIFO_U1;
            dma_enter_page(c, d, nib & 3);
#ifdef ENABLE_VORTEX2_LOG
            dma_log_state(c, d, "resumed after late carry-on");
#endif
            continue;
        }
        if (d->state != DMA_RUN)
            return;
        uint32_t size = dma_size(c, d, d->page);
        if (d->pos >= size) {
            dma_page_end(c, d);
            continue;
        }
        uint32_t n = size - d->pos;
        if (n > sizeof(tmp))
            n = sizeof(tmp);
        if (n > FIFO_LEN - d->fn)
            n = FIFO_LEN - d->fn;
        if (n > need - d->fn)
            n = need - d->fn; /* fetch what was asked for, like the card */
        if (n == 0)
            return;
        c->host.mem_read(c->host.priv, R(dma_base(d) + d->page * 4) + d->pos, tmp, n);
        for (uint32_t i = 0; i < n; i++)
            d->fifo[(d->frd + d->fn + i) & (FIFO_LEN - 1)] = tmp[i];
        d->fn += n;
        d->pos += n;
        d->since_fetch = 0;
        if (d->pos >= size)
            dma_page_end(c, d);
        guard = 8;
    }
}

static inline uint8_t
fifo_pop(v2_dma_t *d)
{
    uint8_t v = d->fifo[d->frd];
    d->frd    = (d->frd + 1) & (FIFO_LEN - 1);
    d->fn--;
    return v;
}

static float
ulaw_dec(uint8_t u)
{
    int t;
    u = ~u;
    t = ((u & 0x0f) << 3) + 0x84;
    t <<= (u & 0x70) >> 4;
    return (float) ((u & 0x80) ? (0x84 - t) : (t - 0x84)) / 32768.0f;
}

static float
alaw_dec(uint8_t a)
{
    int t, seg;
    a ^= 0x55;
    t   = (a & 0x0f) << 4;
    seg = (a & 0x70) >> 4;
    if (seg == 0)
        t += 8;
    else if (seg == 1)
        t += 0x108;
    else
        t = (t + 0x108) << (seg - 1);
    return (float) ((a & 0x80) ? t : -t) / 32768.0f;
}

/* Fetch the next frame of nch samples from a playback channel. */
static void
dma_next_frame(v2_chip_t *c, v2_dma_t *d, int nch)
{
    int fmt = dma_fmt(c, d);
    int bps = dma_bps(fmt);

    float *fr = d->frame[d->frame_no & (FRAME_RING - 1)];

    if (nch < 1)
        nch = 1;
    if (nch > 4)
        nch = 4;
    /* The card prefetches 56 samples when a stream starts and then fetches
       18 samples each time 18 have been played (real-card status reads:
       position 0x070 within 0.15 ms of a 16-bit start, then +0x24 steps
       while the samples-since-fetch field cycles 0..17). The driver reads
       both fields to find the play position, so fetch the same way. */
    if (!d->primed) {
        d->primed = 1;
        dma_fill(c, d, 56 * bps);
    }
    if (d->fn < (uint32_t) (nch * bps))
        dma_fill(c, d, d->fn + (uint32_t) (18 * bps > nch * bps ? 18 * bps : nch * bps)); /* ran dry */
    if (d->fn < (uint32_t) (nch * bps)) {
        if (d->state == DMA_END)
            d->fn = 0; /* a finished stream's last partial frame: drained */
        for (int i = 0; i < 4; i++)
            fr[i] = 0.0f; /* underrun */
        d->frame_no++;
        return;
    }
    for (int i = 0; i < nch; i++) {
        float v;
        if (fmt == 1)
            v = ((float) fifo_pop(d) - 128.0f) / 128.0f;
        else if (fmt == 2)
            v = alaw_dec(fifo_pop(d));
        else if (fmt == 3)
            v = ulaw_dec(fifo_pop(d));
        else {
            uint8_t lo = fifo_pop(d);
            uint8_t hi = fifo_pop(d);
            v          = (float) (int16_t) (lo | (hi << 8)) / 32768.0f;
        }
        fr[i] = v;
    }
    d->since_fetch += nch;
    d->frame_no++;
    if (d->since_fetch >= 18)
        dma_fill(c, d, d->fn + 18 * bps);
}

/* A consumer (route, converter, voice) takes sample idx of its next
   frame. Each frame is fetched once; the consumers of one channel (the
   left and right converters of a stereo stream) each read it in turn,
   even though the model runs them a block apart. */
static float
dma_take(v2_chip_t *c, v2_dma_t *d, int idx, int nch, uint32_t *cnt)
{
    float v;

    if (d->state == DMA_IDLE)
        return 0.0f;
    if (d->frame_no - *cnt > FRAME_RING - 1 && *cnt < d->frame_no)
        *cnt = d->frame_no - 1; /* fell too far behind: resynchronise */
    if (*cnt > d->frame_no)
        *cnt = d->frame_no;
    if (*cnt == d->frame_no)
        dma_next_frame(c, d, nch);
    v = d->frame[*cnt & (FRAME_RING - 1)][idx & 3];
    (*cnt)++;
    return v;
}

/* A wavetable voice's phase step per output sample, as a 0.16 fraction
   (1.0 = 0x10000 = 48 kHz), from the rate field of its base word: 3-bit
   exponent in bits 15-13, 12-bit mantissa in bits 12-1 (bit 0 is the
   state bit). Like the chip's other exponent/mantissa codes it has an
   implicit leading one followed by the mantissa repeated, and the result
   is cut to 16 fraction bits; exponent 0 is the mantissa alone.
   Measured on a real SQ2500 (run 18): seven rate words from 0x4aaa to
   0x9acc played tones whose frequencies all sit 3-14 ppm above this
   (the recorder's clock), where a mantissa without the repeat leaves
   them scattered from 3 to 109 ppm. 0x9acc (44.1 kHz) is 0xeb36. */
static uint32_t
wt_step(uint32_t rate)
{
    uint32_t e = (rate >> 13) & 7, m = (rate >> 1) & 0xfff;

    return e ? ((1u << 24) | (m << 12) | m) >> (13 - e) : m;
}

/* Capture: write one frame to guest memory. */
static void
dma_put_frame(v2_chip_t *c, v2_dma_t *d, const float *v, int nch)
{
    uint8_t buf[8];
    int     fmt = dma_fmt(c, d);
    int     bps = dma_bps(fmt);
    int     n   = 0;

    if (d->state != DMA_RUN)
        return;
    for (int i = 0; i < nch; i++) {
        int s = (int) lrintf(clampf(v[i], -1.0f, 32767.0f / 32768.0f) * 32768.0f);
        if (bps == 1)
            buf[n++] = (uint8_t) ((s >> 8) + 128);
        else {
            buf[n++] = s & 0xff;
            buf[n++] = (s >> 8) & 0xff;
        }
    }
    uint32_t size = dma_size(c, d, d->page);
    if (d->pos + n > size)
        n = size - d->pos;
    if (n > 0)
        c->host.mem_write(c->host.priv, R(dma_base(d) + d->page * 4) + d->pos, buf, n);
    d->pos += n;
    if (d->pos >= size)
        dma_page_end(c, d);
}

static uint32_t
dma_stat(v2_chip_t *c, v2_dma_t *d)
{
    /* bits 19-14: samples played from the FIFO since its last fetch
       (the real card fetches 18 samples whenever that many have been
       played, so the field climbs 3..17 and drops) */
    uint32_t v = ((uint32_t) d->page << 12) | (d->pos & 0xfff) | ((d->since_fetch > 0x3f ? 0x3f : d->since_fetch) << 14);

    (void) c;
    switch (d->state) {
        case DMA_RUN:
        case DMA_STALL:
            return v | 0x00200000;
        case DMA_END:
            /* Stream finished. While the FIFO still drains, the card reports
               neither run nor idle, with the position cleared (0x000b1000);
               once it is empty it reports idle (0x00501000), and the driver
               waits for that before it returns the buffer to Windows. A model
               that never went idle left system sounds stuck "playing". */
            if (d->fn > 0 || (d->stopped && (int32_t) (d->drain_end - c->glide_frames) > 0))
                return ((uint32_t) d->page << 12) | ((d->since_fetch > 0x3f ? 0x3f : d->since_fetch) << 14);
            return 0x00500000 | ((uint32_t) d->page << 12);
        default:
            return 0x00500000 | ((uint32_t) d->page << 12); /* idle: page only */
    }
}

static uint32_t
dma_pages_word(v2_chip_t *c, uint32_t addr)
{
    int       wt   = addr < ADBDMA_PAGES;
    int       w    = (addr - (wt ? WTDMA_PAGES : ADBDMA_PAGES)) >> 2;
    uint32_t  v    = R(addr);
    v2_dma_t *bank = wt ? c->wt : c->adb;

    for (int i = 0; i < 16; i++) {
        v2_dma_t *d = &bank[w * 16 + i];
        if (!d->touched)
            continue;
        int sh = 30 - 2 * i;
        v      = (v & ~(3u << sh)) | ((uint32_t) d->page << sh);
    }
    return v;
}

static void
fifo_ctrl_write(v2_chip_t *c, v2_dma_t *d, uint32_t old, uint32_t val)
{
    if ((val & FIFO_VALID) && !(old & FIFO_VALID)) {
        int cont = !d->is_wt && d->state == DMA_END && !d->stopped && d->fn > 0;
        dma_begin(c, d, cont);
        if (cont)
            ;
        else if (d->is_wt) {
            v2_wtv_t *v = &c->wtv[d->ch];
            memset(v, 0, sizeof(*v));
            v->fbyte = -1;
        } else {
            for (int r = 0; r < c->nroutes; r++) {
                v2_route_t *rt = &c->route[r];
                if (rt->src != d->ch)
                    continue;
                rt->cnt = 0;
                if (rt->sink >= AK_SRCIN && rt->sink < AK_SRCIN + NR_SRC)
                    memset(&c->src[rt->sink - AK_SRCIN], 0, sizeof(v2_src_t));
            }
        }
    } else if (!(val & FIFO_VALID) && (old & FIFO_VALID)) {
        /* Stopping a stream does not make it idle at once: the card plays
           out what its FIFO holds, reporting "neither run nor idle", and
           goes idle a millisecond or two later (SiN trace: 0x000bd05c,
           0x000c105c, 0x000f1000, then 0x00500000). The driver acts on
           that difference when it reuses the channel for the next sound. */
        if (d->state == DMA_RUN || d->state == DMA_STALL) {
            d->state   = d->fn > 0 ? DMA_END : DMA_IDLE;
            d->stopped = 1;
            if (d->is_wt) {
                /* The real card keeps a voice's FIFO nearly full (64 samples) and plays
                   it out after a stop, at the voice's rate: it reports
                   draining for that long (median 5.5 ms in Half-Life, whose
                   voices run at 11 and 22 kHz) before going idle. */
                uint32_t p = R((d->ch >> 5) * WT_BANK_STRIDE + WT_PARM + (d->ch & 31) * 0x10);
                double   step = (double) wt_step(p & 0xffff) / 65536.0;
                if (step < 0.05)
                    step = 0.05;
                d->drain_end = c->glide_frames + (uint32_t) (56.0 / step);
                d->state     = DMA_END;
            }
        } else if (d->state != DMA_END)
            d->state = DMA_IDLE;
    }
}

/* ------------------------------------------------------------------ */
/* Routing graph                                                       */
/* ------------------------------------------------------------------ */

#ifdef ENABLE_VORTEX2_LOG
/* Diagnostics: the routing table (source > sink, ADB addresses in hex)
   and every enabled mixer input with its volume byte, logged when either
   changes. */
static void
graph_dump(v2_chip_t *c)
{
    char     line[4096];
    int      n = 0;
    uint32_t h = 2166136261u;

    if (!c->host.log)
        return;
    n = v2_cat(line, sizeof(line), n, "Vortex2: routes:");
    for (int r = 0; r < c->nroutes && n < (int) sizeof(line) - 16; r++) {
        n = v2_cat(line, sizeof(line), n, " %02x>%02x", c->route[r].src, c->route[r].sink);
        h = (h ^ (uint32_t) (c->route[r].src << 8 | c->route[r].sink)) * 16777619u;
    }
    n = v2_cat(line, sizeof(line), n, " | mixers:");
    for (int m = 0; m < NR_MIXOUT && n < (int) sizeof(line) - 64; m++) {
        int any = 0;
        for (int i = 0; i < NR_MIXIN && n < (int) sizeof(line) - 24; i++) {
            if (!(R(MIX_ENIN + ((m << 3) + (i >> 2)) * 4) & (1u << (i & 3))))
                continue;
            uint32_t g = R(MIX_INVOL_A + ((m << 5) + i) * 4) & 0xff;
            n = v2_cat(line, sizeof(line), n, "%s%d<%d:%02x", any ? "," : " m", m, i, g);
            h = (h ^ (uint32_t) (m << 16 | i << 8 | g)) * 16777619u;
            any = 1;
        }
        if (any)
            n = v2_cat(line, sizeof(line), n, " out:%02x", R(MIX_VOL_A + m * 4) & 0xff);
    }
    if (h == c->graph_hash)
        return;
    c->graph_hash = h;
    v2_cat(line, sizeof(line), n, "\n");
    c->host.log(c->host.priv, line);
}
#endif

static void
graph_rebuild(v2_chip_t *c)
{
    uint32_t sr = R(ADB_SR);

    c->nroutes = 0;
    c->wt_routed[0] = c->wt_routed[1] = 0;
    memset(c->sink_n, 0, sizeof(c->sink_n));
    memset(c->dma_nroutes, 0, sizeof(c->dma_nroutes));

    for (int ch = 0; ch < ADB_NCHN; ch++) {
        if (!(sr & (1u << ch)))
            continue;
        uint32_t r = R(ADB_CHNBASE + ch * 4) & 0xffff;
        for (int guard = 0; guard < 256; guard++) {
            if ((r & 0xff) == 0xff || c->nroutes >= MAX_ROUTES)
                break;
            uint8_t src = r >> 8, sink = r & 0xff;
            if (src != 0xff && c->sink_n[sink] < MAX_PER_SINK) {
                v2_route_t *rt = &c->route[c->nroutes];
                rt->src        = src;
                rt->sink       = sink;
                rt->idx        = 0;
                rt->cnt        = 0;
                if (src < NR_ADBDMA) {
                    rt->idx = c->dma_nroutes[src]++;
                    rt->cnt = c->adb[src].frame_no;
                }
                c->sink_r[sink][c->sink_n[sink]++] = c->nroutes;
                c->nroutes++;
            }
            r = R(ADB_RTBASE + (r & 0xff) * 4) & 0xffff;
        }
    }
    c->graph_dirty = 0;
    /* A converter handed a different source starts empty: on the chip its
       history would hold the new source's recent input, not the tail of
       whatever it converted before (heard as a short burst). */
    for (int r = 0; r < c->nroutes; r++) {
        int sink = c->route[r].sink;
        if (sink >= AK_SRCIN && sink < AK_SRCIN + NR_SRC && c->src_owner[sink - AK_SRCIN] != c->route[r].src) {
            memset(&c->src[sink - AK_SRCIN], 0, sizeof(v2_src_t));
            c->src_owner[sink - AK_SRCIN] = c->route[r].src;
        }
    }
    for (int r = 0; r < c->nroutes; r++) {
        int src = c->route[r].src;
        if (src >= AS_WT0 && src < AS_WT0 + 32)
            c->wt_routed[0] |= 1u << (src - AS_WT0);
        else if (src >= AS_WT1 && src < AS_WT1 + 32)
            c->wt_routed[1] |= 1u << (src - AS_WT1);
    }
#ifdef ENABLE_VORTEX2_LOG
    v2_log(c, "Vortex2: routing rebuilt, %d routes (ADB_SR %06x)\n", c->nroutes, sr);
    graph_dump(c);
#endif
}

/* ------------------------------------------------------------------ */
/* Block evaluation                                                    */
/* ------------------------------------------------------------------ */

static const float *eval_src(v2_chip_t *c, int a);

static const float *
gather(v2_chip_t *c, int sink)
{
    float *o = c->kbuf[sink];

    if (c->kstate[sink])
        return o;
    c->kstate[sink] = 1;
    memset(o, 0, sizeof(float) * B);
    for (int i = 0; i < c->sink_n[sink]; i++) {
        v2_route_t *rt = &c->route[c->sink_r[sink][i]];
        if (rt->src < NR_ADBDMA) {
            /* A DMA channel feeding a 48 kHz sink directly. A converter
               input pulls at its own rate instead (see src_run). */
            if (sink >= AK_SRCIN && sink < AK_SRCIN + NR_SRC)
                continue;
            v2_dma_t *d = &c->adb[rt->src];
            for (int n = 0; n < B; n++)
                o[n] += dma_take(c, d, rt->idx, c->dma_nroutes[rt->src], &rt->cnt);
        } else {
            const float *s = eval_src(c, rt->src);
            for (int n = 0; n < B; n++)
                o[n] += s[n];
        }
    }
    return o;
}

/* A mixer between a stream and a converter. For a DirectSound buffer with
   3D control the driver downmixes a stereo stream to mono ahead of the
   converter: DMA -> two inputs of a mixer -> mixer output -> converter ->
   A3D input (dxdiag's hardware-buffer test: routes 02>62 02>63 3a>44,
   mixer 10 taking inputs 18 and 19). That mixer has to run at the stream's
   rate, one output per sample the converter takes, or the stream plays at
   48 kHz (2.2 times too fast at 22 kHz). If a converter's only input is a
   mixer whose enabled inputs are fed by playback channels alone, return
   that mixer and one of its channels; otherwise -1. */
static int
src_pulled_mixer(v2_chip_t *c, int sink, v2_dma_t **dp)
{
    if (c->sink_n[sink] != 1)
        return -1;
    int a = c->route[c->sink_r[sink][0]].src;
    if (a < AS_MIXOUT || a >= AS_MIXOUT + NR_MIXOUT)
        return -1;
    int       m = a - AS_MIXOUT, any = 0;
    v2_dma_t *d = NULL;
    for (int i = 0; i < NR_MIXIN; i++) {
        if (!(R(MIX_ENIN + ((m << 3) + (i >> 2)) * 4) & (1u << (i & 3))))
            continue;
        for (int k = 0; k < c->sink_n[AK_MIXIN + i]; k++) {
            v2_route_t *rt = &c->route[c->sink_r[AK_MIXIN + i][k]];
            if (rt->src >= NR_ADBDMA)
                return -1;
            if (!d || d->state == DMA_IDLE)
                d = &c->adb[rt->src];
            any = 1;
        }
    }
    if (!any)
        return -1;
    *dp = d;
    return m;
}

/* One output sample of such a mixer, taking one frame from each of its
   streams: the arithmetic of mix_run, with gain changes taken at once
   (the zero-crossing rule is not followed on this path). */
static float
mix_pull(v2_chip_t *c, int m)
{
    float acc = 0.0f;

    c->mix_ran[m] = 1;
    for (int i = 0; i < NR_MIXIN; i++) {
        if (!(R(MIX_ENIN + ((m << 3) + (i >> 2)) * 4) & (1u << (i & 3))))
            continue;
        uint32_t a_cur = MIX_INVOL_B + ((m << 5) + i) * 4;
        uint8_t  tb    = R(MIX_INVOL_A + ((m << 5) + i) * 4) & 0xff;
        if (!((c->ro[(a_cur >> 2) >> 3] >> ((a_cur >> 2) & 7)) & 1))
            R(a_cur) = (R(a_cur) & ~0xffu) | tb;
        float g  = mix_gain_tab[R(a_cur) & 0xff];
        float in = 0.0f;
        for (int k = 0; k < c->sink_n[AK_MIXIN + i]; k++) {
            v2_route_t *rt = &c->route[c->sink_r[AK_MIXIN + i][k]];
            in += dma_take(c, &c->adb[rt->src], rt->idx, c->dma_nroutes[rt->src], &rt->cnt);
        }
        acc += floorf(g * in * 32768.0f) * (1.0f / 32768.0f);
    }
    uint32_t a_cur = MIX_VOL_B + m * 4;
    if (!((c->ro[(a_cur >> 2) >> 3] >> ((a_cur >> 2) & 7)) & 1))
        R(a_cur) = (R(a_cur) & ~0xffu) | (R(MIX_VOL_A + m * 4) & 0xff);
    uint8_t b = R(a_cur) & 0xff;
    float   v = acc > 32.0f ? 32.0f : (acc < -32.0f ? -32.0f : acc);
    v *= mix_shift_tab[b];
    v = v > 2.0f ? 2.0f : (v < -2.0f ? -2.0f : v);
    v *= mix_mant_tab[b];
    v = floorf(v * 32768.0f) * (1.0f / 32768.0f);
    return v > 32767.0f / 32768.0f ? 32767.0f / 32768.0f : (v < -1.0f ? -1.0f : v);
}

/* Sample-rate converter: CONVRATIO is the input rate over 48 kHz in Q14.
   Fed from a DMA channel it pulls input at that ratio; fed from a 48 kHz
   source (recording paths) it passes samples through and the capture
   DMA applies the ratio. */
static void
src_run(v2_chip_t *c, int s, float *out)
{
    int         sink = AK_SRCIN + s;
    v2_src_t   *st   = &c->src[s];
    v2_route_t *dr   = NULL;

    for (int i = 0; i < c->sink_n[sink]; i++) {
        v2_route_t *rt = &c->route[c->sink_r[sink][i]];
        if (rt->src < NR_ADBDMA) {
            dr = rt;
            break;
        }
    }
    v2_dma_t *d  = NULL;
    int       pm = -1;
    if (dr)
        d = &c->adb[dr->src];
    else if ((pm = src_pulled_mixer(c, sink, &d)) < 0) {
        memcpy(out, gather(c, sink), sizeof(float) * B);
        return;
    }

    double    step = (double) (R(SRC_CONVRATIO + s * 4) & 0x3ffff) / 16384.0;
    if (step > 8.0)
        step = 8.0;

    /* The model runs a converter only when something downstream reads it;
       the chip runs every converter every sample. If this one missed
       blocks, the chip would have consumed that input meanwhile: skip it
       rather than play it late (heard as a burst of the previous sound's
       tail after silence). */
    uint32_t now = c->glide_frames + 1;
    if (st->last_run && st->last_run != now && st->last_run + B != now) {
        if (dr && st->nin < d->frame_no)
            st->nin = d->frame_no;
        memset(st->hist, 0, sizeof(st->hist));
        st->zrun = SRC_PAD;
    }
    st->last_run = now;

    if (d->state == DMA_IDLE) {
        /* An idle source feeds the converter silence, so its history
           empties; otherwise the next sound would start with the tail of
           an older one (heard as a short burst). */
        memset(out, 0, sizeof(float) * B);
        st->phase = 0.0;
        if (st->zrun < SRC_PAD) {
            memset(st->hist, 0, sizeof(st->hist));
            st->zrun = SRC_PAD;
        }
        return;
    }
    c->dbg |= 4;
    for (int n = 0; n < B; n++) {
        st->phase += step;
        while (st->phase >= 1.0) {
            st->phase -= 1.0;
            float   v = dr ? dma_take(c, d, dr->idx, c->dma_nroutes[dr->src], &st->nin) : mix_pull(c, pm);
            int32_t x = (int32_t) lrintf(clampf(v, -1.0f, 32767.0f / 32768.0f) * 32768.0f);
            st->hist[st->hpos] = st->hist[st->hpos + SRC_PAD] = x;
            st->hpos = (st->hpos + 1) % SRC_PAD;
            st->zrun = x ? 0 : st->zrun + 1;
        }
        /* A window of silence converts to exact silence. */
        out[n] = (st->zrun >= SRC_PAD) ? 0.0f : src_interp(st->hist + st->hpos, st->phase);
    }
}

/* Mixer output: sum of enabled inputs times their gains, times the
   output gain. The hardware sums with 5 bits of headroom and clamps the
   sum at +-32 (full scale = 1); the output gain is applied as its
   exponent shift, clamped at +-2, then its mantissa (.1MMM, 0.5-0.94),
   and the result is clamped to full scale (Aureal's description of the
   mixer pipeline). At unity output gain that is a clamp of the sum at
   full scale.

   Read on a real SQ2500 (restprobe mix, 2026-11-17): every input gain
   from 0x00 to 0x1f gave exactly floor(x x gain) for values from 1 LSB
   to full scale, both signs (0x04 on 7 gave 5, on -3 gave -3; 0x1f on
   3 gave 22, on -1 gave -8), the result saturating at 16 bits; two
   inputs summed and saturated (24,576 + 24,576 = 32,767); the output
   gain followed the same law. The model floors each product and the
   output to the 16-bit step.

   A current gain takes its target's value, in one step, at the next
   zero crossing of the signal it scales (the chip keeps the previous
   sample's sign beside each current gain; there is no ramp, so a driver
   that wants one writes the targets in small steps). Here a sample of
   exactly zero also counts as a crossing, so that a silent input does
   not hold its old gain for ever, and gains nothing listens to (input
   disabled, or the mixer's output unrouted) follow their targets at
   once. */
static inline int
mix_cross(v2_chip_t *c, int idx, const float *x)
{
    int sg = c->mix_sign[idx], n0 = B;

    for (int n = 0; n < B; n++) {
        int s = x[n] < 0.0f;
        if ((s != sg || x[n] == 0.0f) && n0 == B)
            n0 = n;
        sg = s;
    }
    c->mix_sign[idx] = (uint8_t) sg;
    return n0;
}

static void
mix_ramp(v2_chip_t *c)
{
    for (int i = 0; i < NR_MIXOUT * 32 + NR_MIXOUT; i++) {
        uint32_t a_cur = (i < 512) ? (MIX_INVOL_B + i * 4) : (MIX_VOL_B + (i - 512) * 4);
        uint32_t a_tgt = (i < 512) ? (MIX_INVOL_A + i * 4) : (MIX_VOL_A + (i - 512) * 4);
        int      m     = (i < 512) ? (i >> 5) : (i - 512);
        if (c->ro[(a_cur >> 2) >> 3] & (1 << ((a_cur >> 2) & 7)))
            continue;
        if (c->mix_ran[m] && (i >= 512 || (R(MIX_ENIN + ((m << 3) + ((i & 31) >> 2)) * 4) & (1u << (i & 3)))))
            continue; /* mix_run saw it */
        if ((R(a_cur) ^ R(a_tgt)) & 0xff)
            R(a_cur) = (R(a_cur) & ~0xffu) | (R(a_tgt) & 0xff);
    }
    memset(c->mix_ran, 0, sizeof(c->mix_ran));
}

static void
mix_run(v2_chip_t *c, int m, float *out)
{
    memset(out, 0, sizeof(float) * B);
    c->mix_ran[m] = 1;
    for (int i = 0; i < NR_MIXIN; i++) {
        if (!(R(MIX_ENIN + ((m << 3) + (i >> 2)) * 4) & (1u << (i & 3))))
            continue;
        uint32_t a_cur = MIX_INVOL_B + ((m << 5) + i) * 4;
        uint8_t  cb    = R(a_cur) & 0xff, tb = R(MIX_INVOL_A + ((m << 5) + i) * 4) & 0xff;
        int      fixed = (c->ro[(a_cur >> 2) >> 3] >> ((a_cur >> 2) & 7)) & 1;
        float    g     = mix_gain_tab[cb];
        if (cb == tb || fixed) {
            if (g == 0.0f && !c->sink_n[AK_MIXIN + i])
                continue;
            const float *in = gather(c, AK_MIXIN + i);
            /* each input's product is floored to the 16-bit step, as the
               card does (restprobe mix: 0x00 on 1, 3, -1, -3 gave 0, 1,
               -1, -2; 0x0c on 3 and -3 gave 4 and -5) */
            if (g != 0.0f)
                for (int n = 0; n < B; n++)
                    out[n] += floorf(g * in[n] * 32768.0f) * (1.0f / 32768.0f);
            if (!fixed)
                (void) mix_cross(c, (m << 5) + i, in);
            continue;
        }
        const float *in = gather(c, AK_MIXIN + i);
        int          n0 = mix_cross(c, (m << 5) + i, in);
        float        g2 = mix_gain_tab[tb];
        for (int n = 0; n < n0; n++)
            out[n] += floorf(g * in[n] * 32768.0f) * (1.0f / 32768.0f);
        for (int n = n0; n < B; n++)
            out[n] += floorf(g2 * in[n] * 32768.0f) * (1.0f / 32768.0f);
        if (n0 < B)
            R(a_cur) = (R(a_cur) & ~0xffu) | tb;
    }
    uint32_t a_cur = MIX_VOL_B + m * 4;
    uint8_t  ob = R(a_cur) & 0xff, tb = R(MIX_VOL_A + m * 4) & 0xff;
    int      n0 = mix_cross(c, 512 + m, out);
    if (ob == tb || ((c->ro[(a_cur >> 2) >> 3] >> ((a_cur >> 2) & 7)) & 1))
        n0 = B;
    else if (n0 < B)
        R(a_cur) = (R(a_cur) & ~0xffu) | tb;
    for (int n = 0; n < B; n++) {
        uint8_t b = n < n0 ? ob : tb;
        float   v = out[n];
        v = v > 32.0f ? 32.0f : (v < -32.0f ? -32.0f : v);
        v *= mix_shift_tab[b];
        v = v > 2.0f ? 2.0f : (v < -2.0f ? -2.0f : v);
        v *= mix_mant_tab[b];
        v      = floorf(v * 32768.0f) * (1.0f / 32768.0f);
        out[n] = v > 32767.0f / 32768.0f ? 32767.0f / 32768.0f : (v < -1.0f ? -1.0f : v);
    }
}

/* ------------------------------------------------------------------ */
/* Wavetable voices                                                    */
/*                                                                     */
/* PARM0 is the voice's base word and PARM1 its target word. The chip   */
/* glides from base to target on the bank's SRAMP only while their      */
/* bit 0s (state bits) are equal; the Windows driver always writes the  */
/* same values with the bits differing, which selects the base word     */
/* (a register map of the chip; every PARM0/PARM1 pair in the traces).  */
/* The glide is not modelled. Fields of the base word:                 */
/*   bits 31-24  filter cut-off, 4e4m: the coefficient of a four-stage */
/*               one-pole low-pass (see wt_filter_set); 0xcf = open     */
/*   bits 23-16  gain, 4e4m, 0x10 per 6 dB, 0xcf = unity, 0 = silent   */
/*   bits 15-1   rate: e = bits 15-13, m = bits 12-1; the phase step   */
/*               is a 0.16 fraction, 1.m with m repeated, shifted by   */
/*               e - 5 (see wt_step); e = 0 has no leading one         */
/* PARM2: bits 31-16 filter feedback gain (resonance; the driver       */
/*   leaves it 0; see wt_bank_run), 15-0 the pitch oscillator's phase.  */
/* PARM3: bits 31-24 filter input gain (same code as the gain; the      */
/*   driver writes 0xcf, unity), 23-21 gain state (0 = hard mute, 7 =   */
/*   normal; the attack and mute ramps in between are never used by    */
/*   the driver and not modelled), 20-14 pan, 13-7 chorus send, 6-0     */
/*   reverb send (7-bit levels, 0x7f = full).                          */
/* A voice whose own ADB source address (bank base + voice) is routed   */
/* (the driver routes them to A3D inputs) feeds that source. Otherwise  */
/* its DSREG bit picks the bank's buses: set = the direct-stream pair   */
/* 32/33, panned like the main pair (wtprobe); clear = the main pair    */
/* 34/35 (panned) plus the reverb (36) and chorus (37) sends, which run */
/* with less headroom (see wt_bank_run).                                */
/* ------------------------------------------------------------------ */

/* Gain byte, 4-bit exponent over 4-bit mantissa. The chip expands it to
   a linear value with an implicit leading one followed by the mantissa's
   bits repeated to fill every place below it (exponent 0: the mantissa
   alone), which puts 0xcf at exactly 0xffff = unity (Strong: the method
   is Aureal's, relayed with a register map of the chip; it is also the
   inverse of the driver's encoding, since its reflectance steps 0.75,
   0.35 and 0.1 decode to 2.50, 9.19 and 19.96 dB below 0x93, and the
   real card's echoes fell by those amounts within the 1 dB the
   measurement allows). Within 0.4 dB of 2^((g - 0xcf) / 16). */
static float wt_gain_tab[256];

static void
init_wt_gain(void)
{
    for (int g = 0; g < 256; g++) {
        int      e = g >> 4, m = g & 15;
        uint32_t v = m;
        if (e) {
            int n = 4 + e - 1; /* places below the leading one */
            v     = 1u << n;
            for (int i = 0; i < n; i++)
                v |= (uint32_t) ((m >> (3 - i % 4)) & 1) << (n - 1 - i);
        }
        wt_gain_tab[g] = (float) v / 65535.0f;
    }
}

static inline float
wt_gain(uint32_t p)
{
    return wt_gain_tab[(p >> 16) & 0xff];
}

/* A voice's low-pass filter: four one-pole stages in series, each
   y += F * (x - y), with F the cut-off byte expanded like the gain byte
   and taken as a 0.16 fraction (0xcf = 0xffff: open). Measured on a real
   SQ2500 (run 19: noise on a wavetable voice while the host stepped the
   byte through 27 values from 0xcc to 0x80; each response against the
   untouched voice): this matches every step to 0.36 dB rms with nothing
   fitted, where a free coefficient per step comes out at 1.000-1.010 x
   the expanded byte. Codes 0xbf, 0xaf and 0x9f measured the same as
   0xc0, 0xb0 and 0xa0, which is the mantissa repeating. The feedback
   word (PARM2 bits 31-16) adds resonance around the four stages (see
   wt_bank_run); the driver leaves it 0. */
static void
wt_filter_set(v2_wtv_t *w, int f)
{
    if (f == w->fbyte)
        return;
    w->fbyte = f;
    w->fcoef = wt_gain_tab[f] * (65535.0f / 65536.0f);
}

/* A voice's own output (its ADB source, which the driver routes to A3D
   inputs) is 0.7998 x its input at gain and input gain 0xcf, whatever
   DSREG and the pan say (wtprobe own: 4096 -> 3276, 8192 -> 6552). The
   mixed pairs take that sample x 2 (the driver's headroom on the direct-
   stream pair) x the pan or send field as a 0.7 fraction: (127 - p) / 128
   left, p / 128 right, f / 128 on a send; the main pair and sends 16 x
   lower. That reproduces every level wtprobe read: 1.5871 x the input at
   pan 0, 0.7998 and 0.7873 at the driver's centre pan 0x3f. */
/* The 0.7998 is 0x3330 / 0x4000 exactly (lastprobe interp, 2026-11-17: a
   single-sample 16,384 at phase 0 read back 13,104 = 0x3330, and at every
   other phase the four weights were Lagrange's cubic times that, within
   1.6 LSB, so the scale rides on the interpolator's coefficients). */
#define WT_OWN_SCALE (13104.0f / 16384.0f)

/* Gain states and ramps (PARM3 bits 23-21). Aureal's description, relayed
   by the map's author, gives the attack and smooth-mute sequences; wtprobe
   ramp timed them on a real SQ2500 (2026-11-17), reading the voice's output
   and registers as they changed:
     0  hard mute.  1, 4: silent, never leave by themselves.
     2  attack_init: at the next 1/32 of the attack-ramp period the voice
        goes to 3, and PARM0's gain byte (in attack mode: phase in bits 7-3,
        slope in bits 2-0) takes that interval number as its phase.
     3  attack: the gain rises linearly in amplitude from 0 to PARM1's gain
        over (8 - slope) / 8 of the period (slope 7 is the fastest, 1/8;
        measured 62.9, 125, 188, 250, 314, 372, 433 and 485 samples for
        slopes 7 to 0 at the driver's rate, against 500 for the full
        period), then the voice goes to 7, PARM0's gain byte takes PARM1's
        and the voice's gain-mode bit is set. Written directly, 3 finishes
        at once.
     5  mute_init: entered when 1 is written to the voice's mute word (one
        word per voice, as ALSA has it; a bit in voice 0's word did
        nothing), which also sets both gain bytes to 0xff. At the next wrap
        of the mute-ramp phase the voice goes to 6.
     6  mute: the level falls linearly to 0 over one mute-ramp period, then
        the voice goes to 0, its mute word clears and its gain-mode bit is
        set.
     7  play (interpolation) at PARM0's gain, or PARM1's if PARM1's bit 0
        is clear (the driver always sets it).
   Each period is 2^32 / rate samples, the rate added to a 32-bit phase each
   sample: mute rates 0x20000000, 0x10000000, 0x08000000, 0x01000000 and
   0x00100000 gave 8, 16, 32, 256 and 4096 samples, attack rates 0x01060000,
   0x00830000, 0x00410000 and 0x00030000 at slope 4 gave 125, 250, 504 and
   10,921, all as 2^32 / rate predicts. The driver writes 0x10000000 and
   0x00830000 (16 samples and 10.4 ms) and uses only states 0 and 7, fading
   by the gain byte itself; the ramps are for other drivers (DOS, MIDI). */
static void
wt_mute_write(v2_chip_t *c, int bank, int lv, uint32_t val)
{
    uint32_t a = bank * WT_BANK_STRIDE + WT_PARM + lv * 0x10;
    if (!(val & 1))
        return;
    if (((R(a + 12) >> 21) & 7) == 0) {
        /* already hard-muted: nothing to do */
        R(bank * WT_BANK_STRIDE + WT_MUTE + lv * 4) = 0;
        return;
    }
    R(a + 12) = (R(a + 12) & ~(7u << 21)) | (5u << 21);
    R(a)      = R(a) | 0x00ff0000u;
    R(a + 4)  = R(a + 4) | 0x00ff0000u;
    c->wt_gmode[bank] &= ~(1u << lv);
    c->wtv[bank * 32 + lv].ramp = 0;
}

/* One block of a voice's gain: fills g[] and moves the state machine. */
static void
wt_gain_run(v2_chip_t *c, int bank, int lv, float *g)
{
    uint32_t  bb   = bank * WT_BANK_STRIDE;
    uint32_t  a    = bb + WT_PARM + lv * 0x10;
    v2_wtv_t *w    = &c->wtv[bank * 32 + lv];
    uint32_t  p3   = R(a + 12);
    int       st   = (p3 >> 21) & 7, st0 = st;
    uint32_t  ainc = R(bb + WT_ARAMP), minc = R(bb + WT_MRAMP);
    uint32_t  aa   = c->wt_aacc[bank], ma = c->wt_macc[bank];

    for (int n = 0; n < B; n++) {
        uint32_t na = aa + ainc, nm = ma + minc;
        switch (st) {
            case 2:
                g[n] = 0.0f;
                if ((na >> 27) != (aa >> 27)) {
                    int slope = (R(a) >> 16) & 7;
                    st        = 3;
                    w->ramp   = 3;
                    w->env    = 0.0f;
                    w->rate   = (float) ((double) ainc / 4294967296.0 * 8.0 / (8 - slope));
                    R(a)      = (R(a) & ~0x00ff0000u) | ((((na >> 27) << 3) | slope) << 16);
                    c->wt_gmode[bank] &= ~(1u << lv);
                }
                break;
            case 3:
                if (w->ramp == 3)
                    w->env += w->rate;
                if (w->ramp != 3 || w->env >= 1.0f) {
                    w->env  = 1.0f;
                    w->ramp = 0;
                    st      = 7;
                    R(a)    = (R(a) & ~0x00ff0000u) | (R(a + 4) & 0x00ff0000u);
                    c->wt_gmode[bank] |= 1u << lv;
                    g[n] = w->mlvl = wt_gain(R(a));
                } else
                    g[n] = w->mlvl = w->env * wt_gain(R(a + 4));
                break;
            case 5:
                g[n] = w->mlvl;
                if (nm < ma) {
                    st      = 6;
                    w->ramp = 6;
                    w->mpos = 1.0f;
                    w->rate = (float) ((double) minc / 4294967296.0);
                    c->wt_gmode[bank] &= ~(1u << lv);
                }
                break;
            case 6:
                if (w->ramp != 6) {
                    w->ramp = 6;
                    w->mpos = 1.0f;
                    w->rate = (float) ((double) minc / 4294967296.0);
                }
                w->mpos -= w->rate;
                if (w->mpos <= 0.0f) {
                    w->mpos = 0.0f;
                    w->ramp = 0;
                    w->mlvl = 0.0f;
                    st      = 0;
                    R(bb + WT_MUTE + lv * 4) = 0;
                    c->wt_gmode[bank] |= 1u << lv;
                }
                g[n] = w->mlvl * w->mpos;
                break;
            case 7:
                /* PARM1 bit 0 set (as the driver always writes it): PARM0's
                   gain; clear: PARM1's (one pair of wtprobe ramp cases,
                   PARM0's byte 0x04 and PARM1's 0xcf: silent, full). */
                g[n] = w->mlvl = wt_gain((R(a + 4) & 1) ? R(a) : R(a + 4));
                break;
            default: /* 0, 1, 4 */
                g[n] = w->mlvl = 0.0f;
                w->ramp = 0;
                break;
        }
        aa = na;
        ma = nm;
    }
    if (st != st0)
        R(a + 12) = (R(a + 12) & ~(7u << 21)) | ((uint32_t) st << 21);
}

static void
wt_bank_run(v2_chip_t *c, int bank)
{
    uint32_t bb = bank * WT_BANK_STRIDE;
    /* Where a voice goes. A voice whose own ADB source (0x40/0x80 + voice)
       is in the routing table plays only into that route: in A3D mode the
       Windows driver feeds 3D inputs that way, and those voices must stay
       out of the bank's buses. Any other voice goes to buses chosen by its
       DSREG bit. Set = the direct-stream pair, outputs 32/33 (Confirmed:
       test-suite run 16 on a real SQ2500, where clearing the bit under a
       playing DirectSound voice silenced it, 87 dB down, with only 32/33
       routed). Clear = the main pair 34/35, panned by PARM3, plus the
       reverb send on 36 and the chorus send on 37, scaled by PARM3's send
       fields (Confirmed, wtprobe on a real SQ2500, 2026-11-17: a voice
       with DSREG clear read digitally on each output while PARM3 was
       stepped. Pan is a linear crossfade, 0 = output 34 (left); each send
       is linear, full scale equal to one side of the main pair, and does
       not change with pan. Half-Life plays voices 4-29 of the second bank
       with DSREG 0, routes that bank's 36 to the left mix and 37 to the
       right, and pans with the two send fields).
       Headroom: the control word (bank + 0) gives the direct-stream pair
       bits 12-10 and the other buses bits 8-6, each the number of bits
       the 32-voice sum is shifted up before its top 16 bits are taken.
       The driver writes 0x1aa3: 6 for the direct-stream pair and 2 for
       the rest, so the main and send buses run 24 dB below the
       direct-stream pair, which is the level measured at gain 0xcf. That
       is why Half-Life turns the send buses' mixer inputs up to 0x1f
       (+17.5 dB). */
    uint32_t routed = c->wt_routed[bank];
    uint32_t ds     = R(bb + WT_DSREG);
    uint32_t ctl    = R(bb);
    float    hs     = ldexpf(1.0f, (int) ((ctl >> 6) & 7) - (int) ((ctl >> 10) & 7));

    memset(c->wt_direct[bank], 0, sizeof(c->wt_direct[bank]));
    memset(c->wt_bus[bank], 0, sizeof(c->wt_bus[bank]));
    for (int lv = 0; lv < 32; lv++) {
        int       v = bank * 32 + lv;
        v2_dma_t *d = &c->wt[v];
        v2_wtv_t *w = &c->wtv[v];
        if (d->state == DMA_IDLE) {
            /* a ramp still runs to its end on a stopped voice */
            uint32_t st = (R(bb + WT_PARM + lv * 0x10 + 12) >> 21) & 7;
            if (st == 2 || st == 3 || st == 5 || st == 6) {
                float ge[B];
                wt_gain_run(c, bank, lv, ge);
            }
            continue;
        }
        c->dbg |= 2;
        uint32_t p0   = R(bb + WT_PARM + lv * 0x10);
        uint32_t p3   = R(bb + WT_PARM + lv * 0x10 + 12);
        uint32_t rate = p0 & 0xffff;
        double   step = (double) wt_step(rate) / 65536.0;
        /* The base word alone sets the voice (see the header); the input
           gain scales the sample ahead of the filter. The voice's own gain
           and its state and ramps (above) are applied per sample, below. */
        float    ge[B];
        wt_gain_run(c, bank, lv, ge);
        p0            = R(bb + WT_PARM + lv * 0x10);
        float    gl   = wt_gain_tab[p3 >> 24];
        int      f    = (p0 >> 24) & 0xff;
        wt_filter_set(w, f);
        int      open = f >= 0xcf;
        int      dir  = (routed >> lv) & 1;
        int      dsv  = (ds >> lv) & 1;
        float   *od   = c->wt_direct[bank][lv];
        float  **bus  = NULL;
        /* Mixed outputs (wtprobe, read digitally on a real SQ2500 with the
           mixer and the plain ADB path for reference): a full voice at gain
           and input gain 0xcf comes out at 1.587 x its input on the
           direct-stream pair when panned to one side, and 1/16 of that on
           the main pair and each send; the pan field is a linear crossfade,
           (127 - p) / 128 left and p / 128 right, on both pairs. So a
           direct-stream voice at the driver's centre pan (0x3f) plays at
           0.80 and 0.79 of the ADB path's level on the two sides. */
        float    pan  = (float) ((p3 >> 14) & 0x7f);
        float    gv   = gl * WT_OWN_SCALE * 2.0f / 128.0f;
        float    g4[4] = { gv * (127.0f - pan), gv * pan, 0.0f, 0.0f };
        float   *b4[4] = { c->wt_bus[bank][0], c->wt_bus[bank][1], NULL, NULL };
        if (!dir && !dsv) {
            float    g   = gv * hs;
            g4[0] = g * (127.0f - pan);
            g4[1] = g * pan;
            g4[2] = g * (float) (p3 & 0x7f);
            g4[3] = g * (float) ((p3 >> 7) & 0x7f);
            for (int k = 0; k < 4; k++)
                b4[k] = c->wt_bus[bank][2 + k];
        }
        bus = b4;

        float y[B];
        for (int n = 0; n < B; n++) {
            w->phase += step;
            while (w->phase >= 1.0) {
                w->phase -= 1.0;
                w->hist[0] = w->hist[1];
                w->hist[1] = w->hist[2];
                w->hist[2] = w->hist[3];
                w->hist[3] = dma_take(c, d, 0, 1, &w->nin);
            }
            y[n] = interp4(w->hist, (float) w->phase);
        }
        int quiet = 1;
        for (int n = 0; n < B; n++)
            quiet &= ge[n] == 0.0f;
        /* Resonance: PARM2 bits 31-16 feed the last stage back to the
           first, u = x - k * y[n-1], with k = (-word mod 2^16) / 2^14: 0 off,
           0xffff almost 0, 0xc000 1, 0x8000 2, 0x0400 3.94 (wtprobe reso,
           2026-11-17: a step through cut-offs 0xb0, 0xb8 and 0xc0 at 16
           words fitted k within about 0.15 of this law, the shape of a
           four-pole ladder). With the filter open the loop is a single
           delay: k 0.5 (0xe000) gave 2/3 of the level, k 1 (0xc000) a
           tone at half the sample rate, and k above 1 silence, which is
           what this gives; any other loop that runs away is cleared. */
        uint32_t fw = R(bb + WT_PARM + lv * 0x10 + 8) >> 16;
        float    kr = (float) ((0x10000u - fw) & 0xffffu) / 16384.0f;
        if (!open || kr != 0.0f) {
            const float F  = w->fcoef > 1.0f ? 1.0f : w->fcoef;
            float       s0 = w->fs[0], s1 = w->fs[1], s2 = w->fs[2], s3 = w->fs[3];
            for (int n = 0; n < B; n++) {
                float u = y[n] - kr * s3;
                s0 += F * (u - s0);
                s1 += F * (s0 - s1);
                s2 += F * (s1 - s2);
                s3 += F * (s2 - s3);
                if (!(fabsf(s3) < 4.0f) || (open && kr > 1.0f))
                    s0 = s1 = s2 = s3 = 0.0f;
                y[n] = s3;
            }
            if (fabsf(s3) < 1e-20f && fabsf(s0) < 1e-20f)
                s0 = s1 = s2 = s3 = 0.0f;
            w->fs[0] = s0;
            w->fs[1] = s1;
            w->fs[2] = s2;
            w->fs[3] = s3;
        }
        if (quiet)
            continue;
        for (int n = 0; n < B; n++)
            y[n] *= ge[n];
        if (dir)
            for (int n = 0; n < B; n++)
                od[n] = y[n] * gl * WT_OWN_SCALE;
        else
            for (int k = 0; k < 4; k++) {
                if (!bus[k] || g4[k] == 0.0f)
                    continue;
                float  gk = g4[k];
                float *o  = bus[k];
                for (int n = 0; n < B; n++)
                    o[n] += y[n] * gk;
            }
    }
    c->wt_aacc[bank] += (uint32_t) B * R(bb + WT_ARAMP);
    c->wt_macc[bank] += (uint32_t) B * R(bb + WT_MRAMP);
    c->wt_done[bank] = 1;
}

/* ------------------------------------------------------------------ */
/* A3D engine                                                          */
/*                                                                     */
/* 16 sources (4 slices x 4). Bank B per source at 0x19000 + slice *   */
/* 0x2000 + source * 0x2c8:                                            */
/*   +0x000 HrtfCurrent[56]  +0x100 HrtfTarget[56]: Q15 taps; in the  */
/*          targets low half = left ear, high half = right ear; every  */
/*          current register has its halves the other way round        */
/*   +0x0e0/0x0e4 Gain current/target: Q15 (16 bits)                   */
/*   +0x0e8/0x0ec atmospheric a1 (low) / a2 (high), Q14                */
/*   +0x0f0/0x0f4 b0 (low) / b1 (high), Q14; +0x0f8/0x0fc b2           */
/*   +0x1e0/0x1e4 ITD current/target: delay in 1/512 sample, low half  */
/*          left ear, high half right ear                              */
/* Bank A (0x18000 + slice * 0x2000 + source * 0x3a4) +0x37c..0x388    */
/* hold the tracking rates (HRTF, gain, filter, ITD), 1.15 fractions:   */
/* each update moves a current value TC / 32768 of the way to its      */
/* target. Gain, filter and HRTF are updated every 64 samples and the  */
/* ITD every 16 (Aureal's description, relayed with a register map of  */
/* the chip), which is what reading the current registers back from a  */
/* real card gave for every TC word: time constants of 2^21 / TC and   */
/* 2^19 / TC samples.                                                  */
/* Outputs: slice s left/right = ADB sources A3DOUT(2s)/(2s+1).        */
/* ------------------------------------------------------------------ */

static inline uint32_t a3d_b(int s) { return A3D_BASE + 0x1000 + (s >> 2) * 0x2000 + (s & 3) * 0x2c8; }
static inline uint32_t a3d_a(int s) { return A3D_BASE + (s >> 2) * 0x2000 + (s & 3) * 0x3a4; }

static void
a3d_load_current(v2_chip_t *c, int s)
{
    v2_a3d_t *a  = &c->a3d[s];
    uint32_t  bb = a3d_b(s);

    /* The current registers hold their two 16-bit halves the other way
       round from the targets (ALSA's A12/A21 and B01/B10 names hint at
       it; the driver's start-of-sound writes confirm it for every pair). */
    for (int k = 0; k < HRTF_TAPS; k++) {
        uint32_t w = R(bb + k * 4);
        a->h[0][k] = HI16(w) / 32768.0f;
        a->h[1][k] = LO16(w) / 32768.0f;
    }
    a->gain   = HI16(R(bb + 0xe0)) / 32768.0f;
    a->co[0]  = HI16(R(bb + 0xe8)) / 16384.0f;
    a->co[1]  = LO16(R(bb + 0xe8)) / 16384.0f;
    a->co[2]  = HI16(R(bb + 0xf0)) / 16384.0f;
    a->co[3]  = LO16(R(bb + 0xf0)) / 16384.0f;
    a->co[4]  = HI16(R(bb + 0xf8)) / 16384.0f;
    a->itd[0] = (float) (uint16_t) (R(bb + 0x1e0) >> 16) / 512.0f;
    a->itd[1] = (float) (uint16_t) R(bb + 0x1e0) / 512.0f;
    a->x1 = a->x2 = a->y1 = a->y2 = 0.0f;
    a->loaded = 1;
}

static inline uint32_t
pack16(float lo, float hi, float scale)
{
    int l = (int) lrintf(clampf(lo * scale, -32768.0f, 32767.0f));
    int h = (int) lrintf(clampf(hi * scale, -32768.0f, 32767.0f));
    return ((uint32_t) (uint16_t) h << 16) | (uint16_t) l;
}

static inline float
glide_k(const v2_chip_t *c, uint32_t tc, double shift)
{
    double k;

    /* The chip updates current values once every 64 samples, moving them
       tc * 64 / shift of the way to the target (read back from a real
       card: at tc 0xc00, exactly 0.09375 per 1.33 ms step). */
    tc &= 0xffff;
    if (!tc || (c->glide_frames % 64) + B < 64)
        return 0.0f;
    k = (double) tc * 64.0 / shift;
    return (float) (k >= 1.0 ? 1.0 : k);
}

static void
a3d_slot_run(v2_chip_t *c, int s, float *outl, float *outr)
{
    v2_a3d_t    *a  = &c->a3d[s];
    uint32_t     bb = a3d_b(s);
    uint32_t     ba = a3d_a(s);
    const float *x  = gather(c, AK_A3DIN + s);

    if (!a->loaded || c->a3d_cur_dirty[s]) {
        a3d_load_current(c, s);
        a->settled = 0;
    }
    if (c->a3d_tgt_dirty[s]) {
        a->settled = 0;
        c->a3d_tgt_dirty[s] = 0;
    }
#ifdef ENABLE_VORTEX2_LOG
    c->a3d_used[s] = 1;
#endif
    c->a3d_cur_dirty[s] = 0;

    /* Glide current values toward the targets. Once nothing moves, the
       slot is settled until the driver writes a target, a glide rate or a
       current value. */
    /* Glides step once every 64 samples, so only a block with a step can
       show that nothing moves any more. */
    if (!a->settled && (c->glide_frames % 64) + B >= 64) {
        int   moved = 0;
        float kh = glide_k(c, R(ba + 0x37c), 2097152.0);
        float kg = glide_k(c, R(ba + 0x380), 2097152.0);
        float kc = glide_k(c, R(ba + 0x384), 2097152.0);
        /* The delay is tracked every 16 samples, the rest every 64:
           four steps of TC / 32768 here. */
        float ki = glide_k(c, R(ba + 0x388), 2097152.0);
        ki       = 1.0f - (1.0f - ki) * (1.0f - ki) * (1.0f - ki) * (1.0f - ki);
        if (kh > 0.0f)
            for (int k = 0; k < HRTF_TAPS; k++) {
                uint32_t w  = R(bb + 0x100 + k * 4);
                float    h0 = a->h[0][k] + (LO16(w) / 32768.0f - a->h[0][k]) * kh;
                float    h1 = a->h[1][k] + (HI16(w) / 32768.0f - a->h[1][k]) * kh;
                moved |= (h0 != a->h[0][k]) | (h1 != a->h[1][k]);
                a->h[0][k] = h0;
                a->h[1][k] = h1;
            }
        if (kg > 0.0f) {
            float g = a->gain + (LO16(R(bb + 0xe4)) / 32768.0f - a->gain) * kg;
            moved |= g != a->gain;
            a->gain = g;
        }
        if (kc > 0.0f) {
            float t[5] = { LO16(R(bb + 0xec)) / 16384.0f, HI16(R(bb + 0xec)) / 16384.0f,
                           LO16(R(bb + 0xf4)) / 16384.0f, HI16(R(bb + 0xf4)) / 16384.0f,
                           LO16(R(bb + 0xfc)) / 16384.0f };
            for (int i = 0; i < 5; i++) {
                float v = a->co[i] + (t[i] - a->co[i]) * kc;
                moved |= v != a->co[i];
                a->co[i] = v;
            }
        }
        if (ki > 0.0f) {
            float i0 = a->itd[0] + ((float) (uint16_t) R(bb + 0x1e4) / 512.0f - a->itd[0]) * ki;
            float i1 = a->itd[1] + ((float) (uint16_t) (R(bb + 0x1e4) >> 16) / 512.0f - a->itd[1]) * ki;
            moved |= (i0 != a->itd[0]) | (i1 != a->itd[1]);
            a->itd[0] = i0;
            a->itd[1] = i1;
        }

        if (moved) {
            /* Make the current values visible to the driver (halves
               swapped, as in the registers). */
            for (int k = 0; k < HRTF_TAPS; k++)
                R(bb + k * 4) = pack16(a->h[1][k], a->h[0][k], 32768.0f);
            R(bb + 0xe0)  = pack16(a->gain, a->gain, 32768.0f);
            R(bb + 0xe8)  = pack16(a->co[1], a->co[0], 16384.0f);
            R(bb + 0xf0)  = pack16(a->co[3], a->co[2], 16384.0f);
            R(bb + 0xf8)  = pack16(0.0f, a->co[4], 16384.0f);
            R(bb + 0x1e0) = ((uint32_t) (uint16_t) lrintf(clampf(a->itd[0] * 512.0f, 0.0f, 65535.0f)) << 16)
                | (uint16_t) lrintf(clampf(a->itd[1] * 512.0f, 0.0f, 65535.0f));
        } else
            a->settled = 1;
    }

    /* Never let a bad coefficient set (or a glide through one) poison
       the filter for good: an unstable pole would otherwise drive the
       state to infinity and silence the source until it is restarted. */
    if (!isfinite(a->y1) || !isfinite(a->y2) || fabsf(a->y1) > 64.0f || fabsf(a->y2) > 64.0f) {
        a->x1 = a->x2 = a->y1 = a->y2 = 0.0f;
        memset(a->dl, 0, sizeof(a->dl));
        memset(a->fh, 0, sizeof(a->fh));
    }

    c->dbg |= 1;

    /* An idle source the driver leaves routed gets silent input. Once its
       filters have emptied, its output is exactly zero, so skip the signal
       path (most slots, most of the time); the glides above still run. */
    int silent = 1;
    for (int n = 0; n < B; n++)
        if (x[n] != 0.0f) {
            silent = 0;
            break;
        }
    if (silent && a->quiet)
        return;

    /* Atmospheric filter and gain, then per ear: ITD delay and the FIR. */
    float d[2];
    for (int e = 0; e < 2; e++) {
        d[e] = a->itd[e];
        if (d[e] > ITD_LEN - 4)
            d[e] = ITD_LEN - 4;
    }
    for (int n = 0; n < B; n++) {
        float y = a->co[2] * x[n] + a->co[3] * a->x1 + a->co[4] * a->x2 + a->co[0] * a->y1 + a->co[1] * a->y2;
        a->x2   = a->x1;
        a->x1   = x[n];
        a->y2   = a->y1;
        a->y1   = y;
        y *= a->gain;
        a->dlpos = (a->dlpos + 1) & (ITD_LEN - 1);
        for (int e = 0; e < 2; e++) {
            a->dl[e][a->dlpos] = y;
            int   di           = (int) d[e];
            float fr           = d[e] - (float) di;
            float s0           = a->dl[e][(a->dlpos - di) & (ITD_LEN - 1)];
            float s1           = a->dl[e][(a->dlpos - di - 1) & (ITD_LEN - 1)];
            a->fh[e][HRTF_TAPS - 1 + n] = s0 + (s1 - s0) * fr;
        }
    }
    for (int e = 0; e < 2; e++) {
        float       *o = e ? outr : outl;
        float       *h = a->fh[e];
        const float *t = a->h[e];
        float        acc[B];
        /* Tap-outer order, so the inner loop vectorises. */
        for (int n = 0; n < B; n++)
            acc[n] = 0.0f;
        for (int k = 0; k < HRTF_TAPS; k++) {
            const float  tk  = t[k];
            const float *src = h + HRTF_TAPS - 1 - k;
            for (int n = 0; n < B; n++)
                acc[n] += tk * src[n];
        }
        for (int n = 0; n < B; n++)
            o[n] += acc[n];
        memmove(h, h + B, sizeof(float) * (HRTF_TAPS - 1));
    }

    /* Track silence: once the input has been silent for longer than the
       delay line and the FIR, and the filter state has decayed, clear the
       state and mark the slot quiet. */
    if (silent) {
        a->zrun += B;
        if (a->zrun >= ITD_LEN + HRTF_TAPS + B && fabsf(a->y1) < 1e-9f && fabsf(a->y2) < 1e-9f) {
            a->x1 = a->x2 = a->y1 = a->y2 = 0.0f;
            memset(a->dl, 0, sizeof(a->dl));
            memset(a->fh, 0, sizeof(a->fh));
            a->quiet = 1;
        }
    } else {
        a->zrun  = 0;
        a->quiet = 0;
    }
}

static void
a3d_run(v2_chip_t *c)
{
    memset(c->a3d_out, 0, sizeof(c->a3d_out));
    for (int s = 0; s < NR_A3D; s++) {
        if (!c->sink_n[AK_A3DIN + s]) {
            if (c->a3d[s].active) {
                memset(&c->a3d[s], 0, sizeof(c->a3d[s]));
                c->a3d_cur_dirty[s] = 1;
            }
            continue;
        }
        c->a3d[s].active = 1;
        a3d_slot_run(c, s, c->a3d_out[(s >> 2) * 2], c->a3d_out[(s >> 2) * 2 + 1]);
    }
    /* Each slice's outputs saturate at 16 bits and are then halved
       (a3dprobe on a real SQ2500, 2026-11-17: with one source set up as the
       driver's pass-through, one tap 0x7fff, gain 0x7fff, b0 0x4000, any
       input x read back as x / 2 rounded toward zero, from 1 LSB to full
       scale, both ears; with b0 0x7fff (2.0) a full-scale input read
       16,383, so the clip comes before the shift. This is the one-bit
       shift on the A3D outputs in the map's author's register map.) The
       same run confirmed the arithmetic above: taps and gain Q15 with
       0x8000 = -1, the filter's coefficients Q14 (b0 0x2000, 0x4000,
       0x7fff = 0.5, 1, 2; a1 0x2000 a pole at 0.5), the delay in 1/512
       sample with linear interpolation (0x80: 0.75 / 0.25), a tap's
       index its delay in samples, and the targets' low half and the
       currents' high half the left ear. */
    for (int o = 0; o < 8; o++)
        for (int n = 0; n < B; n++)
            c->a3d_out[o][n] = 0.5f * clampf(c->a3d_out[o][n], -1.0f, 32767.0f / 32768.0f);
    c->a3d_done = 1;
}

/* ------------------------------------------------------------------ */
/* Crosstalk canceller                                                 */
/*                                                                     */
/* Per side, five inputs are mixed with their gains (0x244d0 +, 1.15), */
/* pass that side's EQ block, and go to that side's output. The other  */
/* side's EQ output, delayed by its delay line (0x24660: left bits     */
/* 17-13, right 12-8, in samples), passes that side's XT block and is  */
/* added:                                                              */
/*   out_L = EQ_L(mix_L) + XT_R(delay_R(EQ_R(mix_R)))  and mirrored.   */
/* A block is five second-order sections in parallel plus a direct     */
/* path, all on the block's input:                                     */
/*   block(x) = 2^shift * (k * x + sum of the five sections),          */
/*   section: y = b0 x + b1 x1 + b2 x2 + a1 y1 + a2 y2,                */
/* with a1, a2, b0, b1, b2 and k as 2.14 words (sections: EQ left      */
/* 0x24200, EQ right 0x242b4, XT left 0x24368, XT right 0x2441c, nine  */
/* words apart; k and shift from 0x24538). "XT left" filters the left  */
/* side's signal into the right output.                                */
/*                                                                     */
/* Confirmed on a real card (test-suite runs 2, 3, 5 and 11): with the */
/* driver's three presets this gives the measured same-side response   */
/* of satellites and monitors relative to headphones to 0.2 and 0.5 dB */
/* rms with no fitted constant (level included: scale 1.02 and 0.99),  */
/* the cross path to 0.6 dB with exactly the register's delay, and a   */
/* front source's absolute spectrum to 0.3 dB per band; host writes of */
/* single coefficients moved the output as this arithmetic says. The   */
/* headphone preset is the same arithmetic with flat sections (sum     */
/* 1.25), k = 0 and no XT, which makes it a plain gain.                */
/*                                                                     */
/* Level (Confirmed, pathprobe on a real SQ2500, 2026-11-17): with the */
/* driver's registers from test-suite run 4 a constant through a 3D    */
/* slot read 2,273 at the slice (the slices' output shift, a3d_run),   */
/* 2,841 at the canceller (x 1.25, headphones) and 2,663 at mixer 0    */
/* (x 0.9375, the driver's 0x07), against 4,096 through the native     */
/* path (mixer 6's 0x00, x 0.5): exactly this arithmetic, with nothing */
/* added. An earlier factor of 0.75 here came from a hand calculation  */
/* that took the canceller's mixer input as 0x0a and the native path   */
/* as unity; it made 3D sound 3.5 dB too loud and is gone.             */
/*                                                                     */
/* The chip works in fixed point with error feedback; this is the same */
/* arithmetic in floating point. The enable bit (bit 0 of 0x24660) is  */
/* not modelled: the driver only drops it while loading a preset.      */
/* ------------------------------------------------------------------ */


static const uint32_t xt_block_base[4] = { 0x24200, 0x242b4, 0x24368, 0x2441c }; /* EQ L, EQ R, XT L, XT R */

static void
xt_load(v2_chip_t *c)
{
    v2_xt_t *x = &c->xt;

    for (int b = 0; b < 4; b++) {
        for (int i = 0; i < 5; i++)
            for (int j = 0; j < 5; j++)
                x->co[b][i][j] = (float) (int16_t) R(xt_block_base[b] + i * 0x24 + j * 4) / 16384.0f;
        x->k[b] = ldexpf((float) (int16_t) R(XT_GAIN_EQL + b * 8) / 16384.0f, (int) (R(XT_GAIN_EQL + b * 8 + 4) & 15));
        x->sh[b] = ldexpf(1.0f, (int) (R(XT_GAIN_EQL + b * 8 + 4) & 15));
    }
    for (int i = 0; i < 10; i++)
        x->gin[i] = (float) (int16_t) R(0x244d0 + i * 4) / 32768.0f;
    x->delay[0] = (R(XT_CTRL) >> 13) & 0x1f;
    x->delay[1] = (R(XT_CTRL) >> 8) & 0x1f;
    c->xt_dirty = 0;
}

static inline float
xt_block(v2_xt_t *x, int b, float in)
{
    double acc = 0.0;

    for (int i = 0; i < 5; i++) {
        const float *q = x->co[b][i];
        double      *y = x->y[b][i];
        double       v = q[2] * in + q[3] * x->x1[b] + q[4] * x->x2[b] + q[0] * y[0] + q[1] * y[1];
        if (!(fabs(v) >= 1e-20 && fabs(v) < 1e3)) /* denormals; and garbage coefficients before a preset is loaded */
            v = 0.0;
        y[1] = y[0];
        y[0] = v;
        acc += v;
    }
    x->x2[b] = x->x1[b];
    x->x1[b] = in;
    return x->k[b] * in + x->sh[b] * (float) acc;
}

static void
xt_run(v2_chip_t *c)
{
    v2_xt_t *x = &c->xt;
    float    in[2][B];

    if (c->xt_dirty)
        xt_load(c);
    for (int s = 0; s < 2; s++) {
        memset(in[s], 0, sizeof(in[s]));
        for (int k = 0; k < 5; k++) {
            int sink = AK_XTIN + s * 5 + k;
            if (!c->sink_n[sink] || x->gin[s * 5 + k] == 0.0f)
                continue;
            const float *g  = gather(c, sink);
            float        gk = x->gin[s * 5 + k];
            for (int n = 0; n < B; n++)
                in[s][n] += g[n] * gk;
        }
    }
    for (int n = 0; n < B; n++) {
        float e[2], d[2];
        for (int s = 0; s < 2; s++) {
            e[s] = xt_block(x, s, in[s][n]);
            d[s] = x->dl[s][(x->dpos - x->delay[s]) & 31];
            x->dl[s][x->dpos] = e[s];
        }
        if (!x->delay[0])
            d[0] = e[0];
        if (!x->delay[1])
            d[1] = e[1];
        x->dpos = (x->dpos + 1) & 31;
        c->xt_out[0][n] = e[0] + xt_block(x, 3, d[1]);
        c->xt_out[1][n] = e[1] + xt_block(x, 2, d[0]);
    }
    c->xt_done = 1;
}

/* ------------------------------------------------------------------ */
/* Hardware EQ: output = bypass (Q15) x input + sum over ten octave     */
/* band-pass sections (a1, a2, b0, b1, b2 in Q14) times band gain      */
/* (Q15). Left at 0x2b000, right at 0x2b1e0, 0x30 per band, gain at    */
/* +0x2c.                                                              */
/* ------------------------------------------------------------------ */

static void
eq_load(v2_chip_t *c)
{
    for (int s = 0; s < 2; s++) {
        uint32_t base = s ? 0x2b1e0 : EQ_BASE;
        for (int b = 0; b < 10; b++) {
            for (int j = 0; j < 5; j++)
                c->eq.co[s][b][j] = LO16(R(base + b * 0x30 + j * 4)) / 16384.0f;
            c->eq.gain[s][b] = LO16(R(base + b * 0x30 + 0x2c)) / 32768.0f;
        }
        c->eq.bypass[s] = LO16(R(s ? EQ_BYPASS_R : EQ_BYPASS_L)) / 32768.0f;
    }
    c->eq_dirty = 0;
#ifdef ENABLE_VORTEX2_LOG
    if (c->host.log) {
        /* the band gain may live at +0x28 (target) as well as +0x2c */
        char line[512];
        int  n = v2_cat(line, sizeof(line), 0, "Vortex2: [%9.3f] EQ bypass %04x/%04x gains(+28/+2c) L",
                        v2_now(c), R(EQ_BYPASS_L) & 0xffff, R(EQ_BYPASS_R) & 0xffff);
        for (int b = 0; b < 10; b++)
            n = v2_cat(line, sizeof(line), n, " %04x/%04x", R(EQ_BASE + b * 0x30 + 0x28) & 0xffff,
                       R(EQ_BASE + b * 0x30 + 0x2c) & 0xffff);
        v2_cat(line, sizeof(line), n, "\n");
        if (strcmp(line + 20, c->eq_logged) != 0) {
            snprintf(c->eq_logged, sizeof(c->eq_logged), "%s", line + 20);
            c->host.log(c->host.priv, line);
        }
    }
#endif
}

static void
eq_run(v2_chip_t *c)
{
    if (c->eq_dirty)
        eq_load(c);
    for (int s = 0; s < 2; s++) {
        const float *x = gather(c, AK_EQIN + s);
        float       *o = c->eq_out[s];
        for (int n = 0; n < B; n++)
            o[n] = c->eq.bypass[s] * x[n];
        for (int b = 0; b < 10; b++) {
            float g = c->eq.gain[s][b];
            if (g == 0.0f)
                continue;
            float *co = c->eq.co[s][b], *st = c->eq.st[s][b];
            if (!isfinite(st[2]) || fabsf(st[2]) > 64.0f)
                memset(st, 0, sizeof(float) * 4); /* recover from an unstable coefficient set */
            for (int n = 0; n < B; n++) {
                float y = co[2] * x[n] + co[3] * st[0] + co[4] * st[1] + co[0] * st[2] + co[1] * st[3];
                st[1]   = st[0];
                st[0]   = x[n];
                st[3]   = st[2];
                st[2]   = y;
                o[n] += g * y;
            }
        }
    }
    c->eq_done = 1;
}

/* ------------------------------------------------------------------ */
/* Source evaluation                                                   */
/* ------------------------------------------------------------------ */

static const float *
eval_src(v2_chip_t *c, int a)
{
    float *o = c->sbuf[a];

    if (c->sstate[a] == 2)
        return o;
    if (c->sstate[a] == 1)
        return o; /* feedback loop: previous block's values */
    c->sstate[a] = 1;

    if (a < NR_ADBDMA) {
        /* handled in gather() */
        memset(o, 0, sizeof(float) * B);
    } else if (a < AS_SRCOUT + NR_SRC) {
        src_run(c, a - AS_SRCOUT, o);
    } else if (a < AS_MIXOUT + NR_MIXOUT) {
        mix_run(c, a - AS_MIXOUT, o);
    } else if (a < AS_XTOUT || (a >= AS_WT1 && a < AS_A3DOUT)) {
        int bank = (a >= AS_WT1);
        int i    = a - (bank ? AS_WT1 : AS_WT0);
        if (!c->wt_done[bank])
            wt_bank_run(c, bank);
        if (i < 32)
            memcpy(o, c->wt_direct[bank][i], sizeof(float) * B);
        else
            memcpy(o, c->wt_bus[bank][i - 32], sizeof(float) * B);
    } else if (a < AS_XTOUT + 2) {
        if (!c->xt_done)
            xt_run(c);
        memcpy(o, c->xt_out[a - AS_XTOUT], sizeof(float) * B);
    } else if (a == AS_EQOUT || a == AS_EQOUT + 1) {
        if (!c->eq_done)
            eq_run(c);
        memcpy(o, c->eq_out[a - AS_EQOUT], sizeof(float) * B);
    } else if (a == AS_CODECIN || a == AS_CODECIN + 1) {
        if (!c->adc_done) {
            if (c->host.codec_adc)
                c->host.codec_adc(c->host.priv, c->adc[0], c->adc[1], B);
            else
                memset(c->adc, 0, sizeof(c->adc));
            c->adc_done = 1;
        }
        memcpy(o, c->adc[a - AS_CODECIN], sizeof(float) * B);
    } else if (a >= AS_A3DOUT && a < AS_A3DOUT + 8) {
        if (!c->a3d_done)
            a3d_run(c);
        memcpy(o, c->a3d_out[a - AS_A3DOUT], sizeof(float) * B);
    } else {
        /* codec, S/PDIF, serial port and effects inputs: silence */
        memset(o, 0, sizeof(float) * B);
    }
    c->sstate[a] = 2;
    return o;
}

/* Capture DMA: sink d (left) and d + 0x20 (right) at the rate of the
   converter that feeds them, or 48 kHz. */
static void
capture_run(v2_chip_t *c, int ch)
{
    v2_dma_t *d = &c->adb[ch];
    int       nch;
    uint32_t  step = 0x10000;

    if (d->state == DMA_IDLE || d->state == DMA_END || (R(dma_ctrl(d)) & 0x2000))
        return;
    nch = c->sink_n[AK_DMAB + ch] ? 2 : 1;
    const float *l = gather(c, ch);
    const float *r = (nch == 2) ? gather(c, AK_DMAB + ch) : l;
    for (int i = 0; i < c->sink_n[ch]; i++) {
        v2_route_t *rt = &c->route[c->sink_r[ch][i]];
        if (rt->src >= AS_SRCOUT && rt->src < AS_SRCOUT + NR_SRC) {
            uint32_t cr = R(SRC_CONVRATIO + (rt->src - AS_SRCOUT) * 4) & 0x3ffff;
            if (cr && cr < 0x4000)
                step = cr << 2;
            else if (cr > 0x4000)
                step = (uint32_t) ((0x4000ull << 16) / cr);
        }
    }
    for (int n = 0; n < B; n++) {
        d->rate_acc += step;
        if (d->rate_acc >= 0x10000) {
            float v[2] = { l[n], r[n] };
            d->rate_acc -= 0x10000;
            dma_put_frame(c, d, v, nch);
        }
    }
}

void
v2_run_block(v2_chip_t *c, float *out)
{
    if (c->graph_dirty)
        graph_rebuild(c);

    c->dbg = 0;
    memset(c->sstate, 0, sizeof(c->sstate));
    memset(c->kstate, 0, sizeof(c->kstate));
    c->wt_done[0] = c->wt_done[1] = 0;
    c->a3d_done = c->xt_done = c->eq_done = c->adc_done = 0;
    c->glide_frames += B;
    mix_ramp(c);

    /* A mixer that runs at a stream's rate ahead of a converter (see
       src_pulled_mixer) is run only by that converter: its inputs and the
       converter's input are not gathered at 48 kHz, which would take the
       stream's samples a second time. */
    for (int s = 0; s < NR_SRC; s++) {
        v2_dma_t *d;
        int       m = src_pulled_mixer(c, AK_SRCIN + s, &d);
        if (m < 0)
            continue;
        c->kstate[AK_SRCIN + s] = 1;
        memset(c->kbuf[AK_SRCIN + s], 0, sizeof(float) * B);
        for (int i = 0; i < NR_MIXIN; i++)
            if (R(MIX_ENIN + ((m << 3) + (i >> 2)) * 4) & (1u << (i & 3))) {
                c->kstate[AK_MIXIN + i] = 1;
                memset(c->kbuf[AK_MIXIN + i], 0, sizeof(float) * B);
            }
    }

    const float *fl = gather(c, AK_CODEC + 0);
    const float *fr = gather(c, AK_CODEC + 1);
    const float *rl = gather(c, AK_CODEC + 4);
    const float *rr = gather(c, AK_CODEC + 5);
    for (int n = 0; n < B; n++) {
        out[n * 4 + 0] = fl[n];
        out[n * 4 + 1] = fr[n];
        out[n * 4 + 2] = rl[n];
        out[n * 4 + 3] = rr[n];
    }

    /* Everything else with routes still runs, so streams keep moving
       even when their path is muted or unrouted. */
    for (int s = 0; s < 256; s++)
        if (c->sink_n[s] && !c->kstate[s] && !(s < 0x40))
            gather(c, s);
    for (int ch = 0; ch < NR_ADBDMA; ch++)
        if (c->sink_n[ch])
            capture_run(c, ch);
    for (int bank = 0; bank < 2; bank++)
        if (!c->wt_done[bank])
            wt_bank_run(c, bank);
#ifdef ENABLE_VORTEX2_LOG
    /* Stereo diagnostics: how alike left and right are at each stage. */
    {
        const float *sl[4] = { NULL, c->xt_done ? c->xt_out[0] : NULL, c->eq_done ? c->eq_out[0] : NULL, fl };
        const float *sr[4] = { NULL, c->xt_done ? c->xt_out[1] : NULL, c->eq_done ? c->eq_out[1] : NULL, fr };
        float al[B], ar[B];
        if (c->a3d_done) {
            memset(al, 0, sizeof(al));
            memset(ar, 0, sizeof(ar));
            for (int k = 0; k < 4; k++)
                for (int n = 0; n < B; n++) {
                    al[n] += c->a3d_out[2 * k][n];
                    ar[n] += c->a3d_out[2 * k + 1][n];
                }
            sl[0] = al;
            sr[0] = ar;
        }
        for (int st = 0; st < 4; st++)
            if (sl[st])
                for (int n = 0; n < B; n++) {
                    c->lr[st][0] += (double) sl[st][n] * sl[st][n];
                    c->lr[st][1] += (double) sr[st][n] * sr[st][n];
                    c->lr[st][2] += (double) sl[st][n] * sr[st][n];
                }
    }

    /* Diagnostics: a status line every two seconds. */
    c->status_frames += B;
    if (c->host.log && c->status_frames >= 2 * V2_RATE) {
        char line[1024];
        int  n = v2_cat(line, sizeof(line), 0, "Vortex2: [%9.3f] status: timer %u fired %u acked %u irq-raises %u level %d src %04x mask %08x ctrl %08x |",
                          v2_now(c), c->timer_last & 0xffff, c->n_fired, c->n_acked, c->n_irq_raise, c->irq_level,
                          c->irq_src, R(IRQ_CTRL), R(CHIP_CTRL));
        for (int ch = 0; ch < NR_ADBDMA && n < (int) sizeof(line) - 64; ch++) {
            v2_dma_t *d = &c->adb[ch];
            /* a finished stream whose FIFO has drained reads as idle: leave
               it out; one still draining shows as D */
            if (d->state == DMA_IDLE || (d->state == DMA_END && d->fn == 0))
                continue;
            n = v2_cat(line, sizeof(line), n, " A%d:%c%d/%03x %02x%02x",
                          ch, d->state == DMA_END ? 'D' : "IRSE"[d->state], d->page, d->pos & 0xfff,
                          R(dma_cfg(d)) >> 24, R(dma_cfg(d) + 4) >> 24);
        }
        int wt_run = 0, wt_stall = 0, wt_end = 0;
        for (int v = 0; v < NR_WTDMA; v++) {
            wt_run += c->wt[v].state == DMA_RUN;
            wt_stall += c->wt[v].state == DMA_STALL;
            wt_end += c->wt[v].state == DMA_END;
        }
        n = v2_cat(line, sizeof(line), n, " | WT run %d stall %d end %d | L/R", wt_run, wt_stall, wt_end);
        static const char *const stage[4] = { "a3d", "xtalk", "eq", "out" };
        for (int st = 0; st < 4 && n < (int) sizeof(line) - 48; st++) {
            double ll = c->lr[st][0], rr = c->lr[st][1], lr = c->lr[st][2];
            if (ll + rr < 1e-9)
                n = v2_cat(line, sizeof(line), n, " %s -", stage[st]);
            else
                n = v2_cat(line, sizeof(line), n, " %s %.0fdB corr %.2f bal %+.1f", stage[st],
                              10.0 * log10((ll + rr) / (2.0 * 2 * V2_RATE)), lr / sqrt(ll * rr + 1e-30),
                              10.0 * log10((rr + 1e-12) / (ll + 1e-12)));
        }
        /* Active 3D sources: glide-rate words (bank A) and the left/right
           level difference of the HRTF target versus the filter in use. */
        for (int sl = 0; sl < NR_A3D && n < (int) sizeof(line) - 96; sl++) {
            v2_a3d_t *a = &c->a3d[sl];
            if (!a->loaded || !c->a3d_used[sl])
                continue;
            uint32_t bb = a3d_b(sl), ba = a3d_a(sl);
            double tl = 0, tr = 0, cl = 0, cr = 0;
            for (int k = 0; k < HRTF_TAPS; k++) {
                uint32_t w = R(bb + 0x100 + k * 4);
                tl += (double) LO16(w) * LO16(w);
                tr += (double) HI16(w) * HI16(w);
                cl += (double) a->h[0][k] * a->h[0][k];
                cr += (double) a->h[1][k] * a->h[1][k];
            }
            n = v2_cat(line, sizeof(line), n, " | S%d tc %x/%x/%x/%x ild tgt %+.1f cur %+.1f gain %.2f",
                          sl, R(ba + 0x37c) & 0xffff, R(ba + 0x380) & 0xffff, R(ba + 0x384) & 0xffff,
                          R(ba + 0x388) & 0xffff, 10.0 * log10((tr + 1e-9) / (tl + 1e-9)),
                          10.0 * log10((cr + 1e-12) / (cl + 1e-12)), a->gain);
        }
        memset(c->a3d_used, 0, sizeof(c->a3d_used));
        v2_cat(line, sizeof(line), n, "\n");
        memset(c->lr, 0, sizeof(c->lr));
        c->host.log(c->host.priv, line);
        graph_dump(c);
        c->status_frames = 0;
        c->n_fired = c->n_acked = c->n_irq_raise = 0;
    }
#endif

    /* Playback DMA channels with no consumer at all still drain. That
       includes a finished stream whose routes the driver removed before
       its FIFO emptied: on the real card it goes idle within a few
       milliseconds (Half-Life trace), and a model that left it "draining"
       for good made the driver treat the channel as busy for the rest of
       the game. */
    for (int ch = 0; ch < NR_ADBDMA; ch++) {
        v2_dma_t *d = &c->adb[ch];
        if ((d->state == DMA_RUN || d->state == DMA_STALL || (d->state == DMA_END && d->fn > 0)) && !c->dma_nroutes[ch] && (R(dma_ctrl(d)) & 0x2000)) {
            uint32_t cnt = d->frame_no;
            for (int n = 0; n < B; n++)
                dma_take(c, d, 0, 1, &cnt);
        }
    }
}

/* Whether the codec's ADC (ADB sources 0x70/0x71) reaches sink k through
   converters and mixers. */
static int
reaches_codecin(v2_chip_t *c, int k, int depth)
{
    if (depth > 8)
        return 0;
    for (int i = 0; i < c->sink_n[k]; i++) {
        int a = c->route[c->sink_r[k][i]].src;
        if (a == AS_CODECIN || a == AS_CODECIN + 1)
            return 1;
        if (a >= AS_SRCOUT && a < AS_SRCOUT + NR_SRC && reaches_codecin(c, AK_SRCIN + a - AS_SRCOUT, depth + 1))
            return 1;
        if (a >= AS_MIXOUT && a < AS_MIXOUT + NR_MIXOUT) {
            int m = a - AS_MIXOUT;
            for (int j = 0; j < NR_MIXIN; j++)
                if ((R(MIX_ENIN + ((m << 3) + (j >> 2)) * 4) & (1u << (j & 3)))
                    && reaches_codecin(c, AK_MIXIN + j, depth + 1))
                    return 1;
        }
    }
    return 0;
}

/* Whether a capture (record) DMA channel is running and records the codec's
   ADC. Aureal's Sound Blaster emulation also runs a capture channel, from
   the mixers alone, which needs no input from the host. */
int
v2_capture_active(v2_chip_t *c)
{
    for (int ch = 0; ch < NR_ADBDMA; ch++) {
        v2_dma_t *d = &c->adb[ch];
        if (!c->sink_n[ch] || d->state == DMA_IDLE || d->state == DMA_END || (R(dma_ctrl(d)) & 0x2000))
            continue;
        if (reaches_codecin(c, ch, 0) || reaches_codecin(c, AK_DMAB + ch, 0))
            return 1;
    }
    return 0;
}

uint32_t
v2_debug_flags(v2_chip_t *c)
{
    return c->dbg;
}

/* ------------------------------------------------------------------ */
/* Register access                                                     */
/* ------------------------------------------------------------------ */

static inline int
is_ro(v2_chip_t *c, uint32_t a)
{
    uint32_t w = a >> 2;
    return (c->ro[w >> 3] >> (w & 7)) & 1;
}

uint32_t
v2_read(v2_chip_t *c, uint32_t addr)
{
    uint32_t v;

    addr &= (V2_REG_SIZE - 4);
    if (addr >= DECODE_END)
        return c->last_bus;

    switch (addr) {
        case SMP_TIME:
            v = c->host.clock ? c->host.clock(c->host.priv) : 0;
            break;
        case SMP_TIMER:
            /* bit 31: timer has fired; the read acknowledges it */
            /* bits 15-0: samples left in the current period */
            v = (c->timer_fired ? 0x80000000u : 0);
            if ((c->timer_last & 0xffff) && c->host.clock) {
                uint32_t n  = c->timer_last & 0xffff;
                uint32_t el = ((c->host.clock(c->host.priv) - c->timer_t0) >> 8) % n;
                v |= n - el;
            }
            c->timer_fired = 0;
#ifdef ENABLE_VORTEX2_LOG
            c->n_acked++;
#endif
            c->irq_src &= ~IRQ_TIMER;
            update_irq(c);
            break;
        case IRQ_SOURCE:
            v = c->irq_src | IRQ_ALWAYS;
            break;
        case IRQ_STAT:
            /* Bits 0x4200 read set in nearly every real-card read (f2d8/f2d9);
               bits 8-10 and 6-7 vary with activity and are not modelled. */
            v = (R(IRQ_STAT) & ~1u) | 0x4200u | (c->irq_level ? 1 : 0);
            break;
        case WT_GMODE:
        case WT_GMODE + WT_BANK_STRIDE:
            /* Gain-mode bits, one per voice: cleared when an attack or a
               smooth mute starts, set when it ends (wtprobe ramp). Polled by
               the MIDI synthesiser about 100 times a second; with no ramp
               under way it reads all ones, as the real card did in run 12. */
            v = c->wt_gmode[addr >> 15];
            break;
        case CODEC_CTRL:
            v = R(CODEC_CTRL) | 0x100; /* codec ready */
            break;
        default:
            /* Channels the driver has not used yet: the real card reports
               ADB channels idle (0x0050p000) and wavetable channels as
               0x000ff000 (Half-Life trace). */
            if (addr == 0x2205c)
                v = R(addr) | 1;
            else if (addr >= WTDMA_STAT && addr < WTDMA_STAT + NR_WTDMA * 4)
                v = c->wt[(addr - WTDMA_STAT) >> 2].touched ? dma_stat(c, &c->wt[(addr - WTDMA_STAT) >> 2]) : 0x000ff000;
            else if (addr >= ADBDMA_STAT && addr < ADBDMA_STAT + NR_ADBDMA * 4)
                v = c->adb[(addr - ADBDMA_STAT) >> 2].touched ? dma_stat(c, &c->adb[(addr - ADBDMA_STAT) >> 2]) : 0x00500000;
            else if (addr >= WTDMA_PAGES && addr < ADBDMA_PAGES + 8)
                v = dma_pages_word(c, addr);
            else if (addr >= FIFO_WTCTRL && addr < FIFO_ADBCTRL + NR_ADBDMA * 4) {
                int       wt = addr < FIFO_ADBCTRL;
                v2_dma_t *d  = wt ? &c->wt[(addr - FIFO_WTCTRL) >> 2] : &c->adb[(addr - FIFO_ADBCTRL) >> 2];
                /* Only bits 19-0 read back: the top bits of the driver's
                   stop sequence (0x1c4xxxxx, then 0x0c4xxxxx) are commands
                   and never appear in a real card's reads. */
                v            = R(addr) & 0x000fffff & ~FIFO_EMPTY;
                /* An idle channel reads back "empty" (0x20); one whose control
                   word has bit 13 set (0x42000, the driver's stereo/16-bit set-up)
                   also reads 0x40 (every real trace: 0x41000 -> 0x41020,
                   0x42000 -> 0x42060). The driver's DirectSound path waits for
                   0x40 before starting the channel: without it Minerva's
                   streaming buffer never started in 86Box. */
                if (d->fn == 0 && d->state != DMA_RUN) {
                    v |= FIFO_EMPTY;
                    if (R(addr) & 0x2000)
                        v |= FIFO_RDY2;
                }
            } else
                v = R(addr);
            break;
    }
    c->last_bus = v;
    return v;
}

void
v2_write(v2_chip_t *c, uint32_t addr, uint32_t val)
{
    uint32_t old;

    addr &= (V2_REG_SIZE - 4);
    c->last_bus = val;
    if (addr >= DECODE_END || is_ro(c, addr))
        return;
    old     = R(addr);
    R(addr) = val;

    if (addr >= WT_FIFO_RAM && addr < WT_FIFO_RAM + NR_WTDMA * 0x100) {
        /* The driver clears a wavetable channel's FIFO memory (64 words)
           whenever it stops or restarts the voice. A stopped channel plays
           out what its FIFO holds, so on a real card that play-out is now
           silence; the model keeps its own copy of the FIFO, so clear that
           (state and fill level stay, for the status the driver reads). */
        int       ch = (addr - WT_FIFO_RAM) >> 8;
        v2_dma_t *d  = &c->wt[ch];
        if (d->state != DMA_RUN) {
            /* everything downstream of the FIFO that still holds the old
               sound: decoded frames and the voice's interpolation history */
            memset(d->fifo, 0, sizeof(d->fifo));
            memset(d->frame, 0, sizeof(d->frame));
            memset(c->wtv[ch].hist, 0, sizeof(c->wtv[ch].hist));
        }
        return;
    }
    if (addr < 2 * WT_BANK_STRIDE && (addr & (WT_BANK_STRIDE - 1)) >= WT_MUTE && (addr & (WT_BANK_STRIDE - 1)) < WT_MUTE + 32 * 4) {
        wt_mute_write(c, addr >> 15, ((addr & (WT_BANK_STRIDE - 1)) - WT_MUTE) >> 2, val);
        return;
    }
    if (addr >= A3D_BASE && addr < A3D_END) {
        uint32_t off = (addr - A3D_BASE) & 0x1fff;
        if (off >= 0x1000 && off < 0x1000 + 4 * 0x2c8) {
            int      s   = ((addr - A3D_BASE) >> 13) * 4 + (off - 0x1000) / 0x2c8;
            uint32_t rel = (off - 0x1000) % 0x2c8;
            if (rel < 0xe0 || rel == 0xe0 || rel == 0xe8 || rel == 0xf0 || rel == 0xf8 || rel == 0x1e0)
                c->a3d_cur_dirty[s] = 1;
            else
                c->a3d_tgt_dirty[s] = 1;
        } else if (off < 0x1000) {
            /* bank A (glide rates): mark the slice's four sources */
            int sl = ((addr - A3D_BASE) >> 13) * 4;
            if (sl + 4 <= NR_A3D)
                memset(&c->a3d_tgt_dirty[sl], 1, 4);
        }
        return;
    }
    if (addr >= FIFO_WTCTRL && addr < FIFO_ADBCTRL + NR_ADBDMA * 4) {
        int       wt = addr < FIFO_ADBCTRL;
        v2_dma_t *d  = wt ? &c->wt[(addr - FIFO_WTCTRL) >> 2] : &c->adb[(addr - FIFO_ADBCTRL) >> 2];
        fifo_ctrl_write(c, d, old, val);
        return;
    }
    if ((addr >= ADB_RTBASE && addr < ADB_CHNBASE + ADB_NCHN * 4) || addr == ADB_SR) {
        if (addr != ADB_SR)
            R(addr) = val &= 0xffff; /* route words are 16 bits wide */
        if (old != val)
            c->graph_dirty = 1;
        return;
    }
    if (addr >= XT_BASE && addr < XT_END) {
        c->xt_dirty = 1;
        return;
    }
    if (addr >= EQ_BASE && addr < EQ_END) {
        c->eq_dirty = 1;
        return;
    }
    switch (addr) {
        case CODEC_IO:
            if (val & 0x00800000) {
                if (c->host.codec_write)
                    c->host.codec_write(c->host.priv, (val >> 16) & 0x7f, val & 0xffff);
            } else {
                uint8_t  reg = (val >> 16) & 0x7f;
                uint16_t d   = c->host.codec_read ? c->host.codec_read(c->host.priv, reg) : 0xffff;
                R(addr)      = 0x00800000 | ((uint32_t) reg << 16) | d; /* bit 23 set when the reply is in */
            }
            break;
        case SMP_TIMER:
            if (!(val & 0xffff) && (c->timer_last & 0xffff)) {
                /* The driver computes the next period as "deadline minus
                   now" and writes 0 when the deadline has already passed.
                   A real card then fires at once: in a Half-Life session
                   (A3D) all 21,510 zero writes were followed by a fired
                   timer about 0.13 ms later, before the next write. */
#ifdef ENABLE_VORTEX2_LOG
                if (c->n_zero_timer++ < 20)
                    v2_log(c, "Vortex2: [%9.3f] driver wrote 0 to the sample timer; firing at once\n", v2_now(c));
#endif
                val = (val & ~0xffffu) | 1;
            }
            c->timer_last  = val;
            c->timer_t0    = c->host.clock ? c->host.clock(c->host.priv) : 0;
            c->timer_fired = 0;
            c->irq_src &= ~IRQ_TIMER;
            update_irq(c);
            if (c->host.timer_arm)
                c->host.timer_arm(c->host.priv, val & 0xffff);
            break;
        case IRQ_SOURCE:
            R(addr) = old;
            c->irq_src &= ~val;
            update_irq(c);
            break;
        case IRQ_CTRL:
        case CHIP_CTRL:
            update_irq(c);
            break;
        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/* Lifetime                                                            */
/* ------------------------------------------------------------------ */

void
v2_reset(v2_chip_t *c)
{
    memset(c->regs, 0, sizeof(c->regs));
    memset(c->ro, 0, sizeof(c->ro));
    for (size_t i = 0; i < sizeof(vortex2_reset_runs) / sizeof(vortex2_reset_runs[0]); i++) {
        const vortex2_reset_run_t *r = &vortex2_reset_runs[i];
        for (uint32_t w = r->word; w < (uint32_t) r->word + r->count; w++) {
            c->regs[w] = r->value;
            if (r->ro)
                c->ro[w >> 3] |= 1 << (w & 7);
        }
    }
    R(CODEC_CTRL) = 0x00008100;
    /* 0x22034 reads 0xff whatever is written (every real trace: written 0,
       read 0xff). The driver write-tests it at start-up; when it held the
       values, the driver took that as extra hardware and, among other
       things, advertised 16 3D buffers instead of 76 and 7 stereo 3D
       sources instead of 9 (Minerva). 0x2205c (SPDIF_FLAGS) starts at
       0x138015 and keeps bit 0 set when written (0x13801c reads 0x13801d). */
    R(0x22034) = 0x000000ff;
    c->ro[(0x22034 >> 2) >> 3] |= 1 << ((0x22034 >> 2) & 7);
    R(0x2205c) = 0x00138015;
    c->last_bus   = 0xdeadbabe;
    c->irq_src    = R(IRQ_SOURCE) & ~IRQ_ALWAYS;
    c->irq_level  = 0;
    c->timer_fired = 0;
    c->timer_last  = 0;
    if (c->host.set_irq)
        c->host.set_irq(c->host.priv, 0);
    if (c->host.timer_arm)
        c->host.timer_arm(c->host.priv, 0);

    for (int i = 0; i < NR_ADBDMA; i++) {
        memset(&c->adb[i], 0, sizeof(v2_dma_t));
        c->adb[i].ch = i;
    }
    for (int i = 0; i < NR_WTDMA; i++) {
        memset(&c->wt[i], 0, sizeof(v2_dma_t));
        c->wt[i].ch    = i;
        c->wt[i].is_wt = 1;
    }
    memset(c->src, 0, sizeof(c->src));
    memset(c->wtv, 0, sizeof(c->wtv));
    for (int i = 0; i < NR_WTDMA; i++)
        c->wtv[i].fbyte = -1;
    c->wt_aacc[0] = c->wt_aacc[1] = c->wt_macc[0] = c->wt_macc[1] = 0;
    c->wt_gmode[0] = c->wt_gmode[1] = 0xffffffffu;
    memset(c->a3d, 0, sizeof(c->a3d));
    memset(c->a3d_cur_dirty, 1, sizeof(c->a3d_cur_dirty));
    memset(c->a3d_tgt_dirty, 1, sizeof(c->a3d_tgt_dirty));
    memset(c->src_owner, 0xff, sizeof(c->src_owner));
    memset(&c->xt, 0, sizeof(c->xt));
    memset(&c->eq, 0, sizeof(c->eq));
    memset(c->sbuf, 0, sizeof(c->sbuf));
    c->graph_dirty = 1;
    c->xt_dirty    = 1;
    c->eq_dirty    = 1;
}

v2_chip_t *
v2_create(const v2_host_t *host)
{
    v2_chip_t *c = calloc(1, sizeof(v2_chip_t));

    if (!c)
        return NULL;
    init_tables();
    c->host = *host;
    v2_reset(c);
    return c;
}

void
v2_destroy(v2_chip_t *c)
{
    free(c);
}

/* Diagnostics: last block's samples at an ADB source address. */
const float *
v2_debug_source(v2_chip_t *c, int addr)
{
    return c->sbuf[addr & 0xff];
}

/* Diagnostics: whether that source was evaluated in the last block. */
int
v2_debug_source_live(v2_chip_t *c, int addr)
{
    return c->sstate[addr & 0xff] == 2;
}
