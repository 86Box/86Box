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

#define B V2_BLOCK

/* ------------------------------------------------------------------ */
/* Register map                                                        */
/* ------------------------------------------------------------------ */

/* Wavetable: two banks of 32 voices, 0x8000 apart. */
#define WT_BANK_STRIDE 0x8000
#define WT_DSREG       0x0008 /* direct-stream bits, one per voice */
#define WT_STEREO      0x0080 /* + (voice / 2) * 4 */
#define WT_RUN         0x0180 /* + voice * 4 */
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
#define SRC_PHASES 256
#define SRC_PAD    28  /* SRC_TAPS rounded up to a multiple of 4 (leading zero taps) */

typedef struct v2_src_t {
    double   phase;
    float    hist[2 * SRC_PAD]; /* circular, every sample written twice */
    int      hpos;
    int      zrun;              /* consecutive silent input samples */
    uint32_t last_run;          /* block (glide_frames + 1) it last ran in */
    uint32_t nin;
} v2_src_t;

typedef struct v2_wtv_t {
    double   phase;
    float    hist[4];
    float    z1, z2; /* treble filter state */
    int      fbyte;  /* treble byte the coefficients below were made for, -1 = none */
    float    fb0, fa1, fa2;
    uint32_t nin;
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

typedef struct { float re, im; } cpx;

#define XT_N    64             /* FFT size */
#define XT_P    32             /* partitions of B taps */
typedef struct v2_xt_t {
    int   mode; /* 0 pass-through, 1 wide, 2 narrow */
    float prev[2][B];
    cpx   fdl[2][XT_P][XT_N / 2 + 1]; /* input spectra (real signals: half) */
    int   head;
    int   zb; /* consecutive silent input blocks */
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
    uint32_t timer_missed;
    uint32_t n_fired, n_acked, n_irq_raise, n_zero_timer;
    double   lr[4][3]; /* stereo diagnostics: sum LL, RR, LR at A3D out, canceller out, EQ out, output */
    uint32_t status_frames;

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
    uint8_t  a3d_used[NR_A3D];
    uint32_t graph_hash;
    uint32_t glide_frames; /* sample count for the 64-sample glide tick */
    uint32_t log_clk_last; /* log clock: last 32-bit reading, and the wraps so far */
    uint64_t log_clk_wraps;
    uint32_t wt_routed[2]; /* voices whose own ADB source is routed somewhere */

    int     xt_dirty;
    v2_xt_t xt;
    cpx     xt_h[2][2][XT_P][XT_N]; /* [mode-1][same/cross][partition][bin] */
    cpx     tw[XT_N / 2];
    int     rev[XT_N];

    int     eq_dirty;
    v2_eq_t eq;
    char    eq_logged[512];

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

static inline float
clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

/* Catmull-Rom interpolation between h[1] and h[2]. */
static inline float
interp4(const float *h, float t)
{
    float c1 = 0.5f * (h[2] - h[0]);
    float c2 = h[0] - 2.5f * h[1] + 2.0f * h[2] - 0.5f * h[3];
    float c3 = 0.5f * (h[3] - h[0]) + 1.5f * (h[1] - h[2]);
    return ((c3 * t + c2) * t + c1) * t + h[1];
}

/* Sample-rate converter filter. Measured on a real SQ2500 (run 12, noise
   at 8-44.1 kHz against 48 kHz): flat to 0.45 x the input rate, -3 dB at
   0.48, -6 dB at 0.50, -11 dB at 0.52, -23 dB at 0.55, -37 dB at 0.57 and
   below the recording's noise floor (about -60 dB) from 0.60. Aureal's
   documentation gives the converter 27-point interpolation, which fits the
   driver's samples-per-wing setting (13 each side of the centre sample). A
   27-tap Kaiser-windowed sinc (beta 8.25, cutoff at the input Nyquist)
   matches the measurements to 0.37 dB rms (the earlier 22-tap, beta 6 fit:
   0.48 dB) with its stopband below -86 dB. src_fir[p] holds the taps for
   a fractional position p / SRC_PHASES after tap (SRC_TAPS - 1) / 2. */
static float src_fir[SRC_PHASES + 1][SRC_PAD]; /* taps in [SRC_PAD - SRC_TAPS ..] */

static double
bessel_i0(double x)
{
    double s = 1.0, t = 1.0;
    for (int k = 1; k < 30; k++) {
        t *= (x / (2.0 * k)) * (x / (2.0 * k));
        s += t;
    }
    return s;
}

static void
init_src_fir(void)
{
    const double half = SRC_TAPS / 2.0, beta = 8.25, i0b = bessel_i0(beta);
    const int    ctr  = (SRC_TAPS - 1) / 2; /* the tap just before the output point */

    for (int p = 0; p <= SRC_PHASES; p++) {
        double t = (double) p / SRC_PHASES, sum = 0.0, h[SRC_TAPS];
        for (int k = 0; k < SRC_TAPS; k++) {
            double x = (double) (k - ctr) - t; /* distance from the output point */
            double w = x / half;
            double v = (fabs(x) < 1e-9) ? 1.0 : sin(M_PI * x) / (M_PI * x);
            v *= (fabs(w) < 1.0) ? bessel_i0(beta * sqrt(1.0 - w * w)) / i0b : 0.0;
            h[k] = v;
            sum += v;
        }
        for (int k = 0; k < SRC_PAD - SRC_TAPS; k++)
            src_fir[p][k] = 0.0f;
        for (int k = 0; k < SRC_TAPS; k++)
            src_fir[p][SRC_PAD - SRC_TAPS + k] = (float) (h[k] / sum);
    }
}

/* hist: the last SRC_PAD input samples, oldest first. */
static inline float
src_interp(const float *hist, double phase)
{
    double       fp = phase * SRC_PHASES;
    int          p  = (int) fp;
    float        f  = (float) (fp - p);
    const float *h0 = src_fir[p], *h1 = src_fir[p + 1];
    float        a[4] = { 0.0f, 0.0f, 0.0f, 0.0f }, b[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    /* Four independent lanes, so the compiler can use SSE. */
    for (int k = 0; k < SRC_PAD; k += 4)
        for (int j = 0; j < 4; j++) {
            a[j] += h0[k + j] * hist[k + j];
            b[j] += h1[k + j] * hist[k + j];
        }
    float sa = (a[0] + a[1]) + (a[2] + a[3]);
    float sb = (b[0] + b[1]) + (b[2] + b[3]);
    return sa + (sb - sa) * f;
}

/* Mixer gain byte: a signed 5-bit exponent (bits 7-3) and a 3-bit
   mantissa (bits 2-0), the fixed-point number .1MMM x 2^EXP: 0x08 =
   unity, 0x2f = .1111 x 2^5 = 30 (+29.5 dB) is the largest gain, 0x80 =
   .1 x 2^-16 (-102.4 dB) is mute; steps average 0.75 dB (Aureal's
   description of the mixer, relayed with a register map of the chip;
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

static double v2_now(v2_chip_t *c);

static void
update_irq(v2_chip_t *c)
{
    int level = (R(CHIP_CTRL) & CTRL_IRQ_EN) && (c->irq_src & R(IRQ_CTRL));

    if (level != c->irq_level) {
        c->irq_level = level;
        if (level)
            c->n_irq_raise++;
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
v2_timer_expired(v2_chip_t *c)
{
    if (c->timer_fired) {
        if (++c->timer_missed == 50)
            v2_log(c, "Vortex2: [%9.3f] timer interrupts not being serviced (IRQ_CTRL %08x CTRL %08x)\n",
                   v2_now(c), R(IRQ_CTRL), R(CHIP_CTRL));
    } else
        c->timer_missed = 0;
    c->n_fired++;
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
        dma_log_state(c, d, "ended (page without carry-on)");
        /* The card drops the FIFO's run bits when a stream ends (seen as
           0x42070 -> 0x2680 in the DOS Sound Blaster trace). */
        R(dma_fifo(d)) &= ~(FIFO_VALID | FIFO_U1);
    }
}

static void
dma_begin(v2_chip_t *c, v2_dma_t *d)
{
    d->touched  = 1;
    d->primed   = 0;
    d->frd      = 0;
    d->fn       = 0;
    d->frame_no = 0;
    d->rate_acc = 0;
    d->page  = (R(dma_start(d)) >> ((15 - (d->ch & 15)) * 2)) & 3;
    d->pos   = R(dma_ctrl(d)) & 0xfff;
    d->state = DMA_RUN;
    d->stopped = 0;
    dma_log_state(c, d, "started");
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
            dma_log_state(c, d, "resumed after late carry-on");
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
        dma_begin(c, d);
        if (d->is_wt) {
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
                double   step = pow(2.0, (double) ((p >> 13) & 7) - 5.0) * (1.0 + (p & 0x1fff) / 8192.0);
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

/* Diagnostics: the routing table (source > sink, ADB addresses in hex)
   and every enabled mixer input with its volume byte, logged when either
   changes. */
static void
graph_dump(v2_chip_t *c)
{
    char     line[4096];
    int      n = 0;
    uint32_t h = 2166136261u;

    n += snprintf(line + n, sizeof(line) - n, "Vortex2: routes:");
    for (int r = 0; r < c->nroutes && n < (int) sizeof(line) - 16; r++) {
        n += snprintf(line + n, sizeof(line) - n, " %02x>%02x", c->route[r].src, c->route[r].sink);
        h = (h ^ (uint32_t) (c->route[r].src << 8 | c->route[r].sink)) * 16777619u;
    }
    n += snprintf(line + n, sizeof(line) - n, " | mixers:");
    for (int m = 0; m < NR_MIXOUT && n < (int) sizeof(line) - 64; m++) {
        int any = 0;
        for (int i = 0; i < NR_MIXIN && n < (int) sizeof(line) - 24; i++) {
            if (!(R(MIX_ENIN + ((m << 3) + (i >> 2)) * 4) & (1u << (i & 3))))
                continue;
            uint32_t g = R(MIX_INVOL_A + ((m << 5) + i) * 4) & 0xff;
            n += snprintf(line + n, sizeof(line) - n, "%s%d<%d:%02x", any ? "," : " m", m, i, g);
            h = (h ^ (uint32_t) (m << 16 | i << 8 | g)) * 16777619u;
            any = 1;
        }
        if (any)
            n += snprintf(line + n, sizeof(line) - n, " out:%02x", R(MIX_VOL_A + m * 4) & 0xff);
    }
    if (h == c->graph_hash)
        return;
    c->graph_hash = h;
    snprintf(line + n, sizeof(line) - n, "\n");
    if (c->host.log)
        c->host.log(c->host.priv, line);
}

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
    v2_log(c, "Vortex2: routing rebuilt, %d routes (ADB_SR %06x)\n", c->nroutes, sr);
    graph_dump(c);
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
    if (!dr) {
        memcpy(out, gather(c, sink), sizeof(float) * B);
        return;
    }

    v2_dma_t *d    = &c->adb[dr->src];
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
        if (st->nin < d->frame_no)
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
            float v = dma_take(c, d, dr->idx, c->dma_nroutes[dr->src], &st->nin);
            st->hist[st->hpos] = st->hist[st->hpos + SRC_PAD] = v;
            st->hpos = (st->hpos + 1) % SRC_PAD;
            st->zrun = (v == 0.0f) ? st->zrun + 1 : 0;
        }
        /* A window of silence converts to exact silence. */
        out[n] = (st->zrun >= SRC_PAD) ? 0.0f : src_interp(st->hist + st->hpos, st->phase);
    }
}

/* Mixer output: sum of enabled inputs times their gains, times the
   output gain. Current gains glide toward their targets. The hardware
   sums with 5 bits of headroom and clamps the sum at +-32 (full scale =
   1); the output gain is applied as its exponent shift, clamped at +-2,
   then its mantissa (.1MMM, 0.5-0.94), and the result is clamped to
   full scale (Aureal's description of the mixer pipeline). At unity
   output gain that is a clamp of the sum at full scale. */
static void
mix_ramp(v2_chip_t *c)
{
    for (int i = 0; i < NR_MIXOUT * 32 + NR_MIXOUT; i++) {
        uint32_t a_cur = (i < 512) ? (MIX_INVOL_B + i * 4) : (MIX_VOL_B + (i - 512) * 4);
        uint32_t a_tgt = (i < 512) ? (MIX_INVOL_A + i * 4) : (MIX_VOL_A + (i - 512) * 4);
        if (c->ro[(a_cur >> 2) >> 3] & (1 << ((a_cur >> 2) & 7)))
            continue;
        int cur = (int8_t) (R(a_cur) & 0xff);
        int tgt = (int8_t) (R(a_tgt) & 0xff);
        if (cur == tgt)
            continue;
        cur += (tgt > cur) ? ((tgt - cur > 4) ? 4 : tgt - cur) : ((cur - tgt > 4) ? -4 : tgt - cur);
        R(a_cur) = (R(a_cur) & ~0xffu) | (uint8_t) cur;
    }
}

static void
mix_run(v2_chip_t *c, int m, float *out)
{
    memset(out, 0, sizeof(float) * B);
    for (int i = 0; i < NR_MIXIN; i++) {
        if (!(R(MIX_ENIN + ((m << 3) + (i >> 2)) * 4) & (1u << (i & 3))))
            continue;
        float g = mix_gain_tab[R(MIX_INVOL_B + ((m << 5) + i) * 4) & 0xff];
        if (g == 0.0f && !c->sink_n[AK_MIXIN + i])
            continue;
        const float *in = gather(c, AK_MIXIN + i);
        if (g == 0.0f)
            continue;
        for (int n = 0; n < B; n++)
            out[n] += g * in[n];
    }
    uint8_t     ob = R(MIX_VOL_B + m * 4) & 0xff;
    const float sh = mix_shift_tab[ob], mt = mix_mant_tab[ob];
    for (int n = 0; n < B; n++) {
        float v = out[n];
        v = v > 32.0f ? 32.0f : (v < -32.0f ? -32.0f : v);
        v *= sh;
        v = v > 2.0f ? 2.0f : (v < -2.0f ? -2.0f : v);
        v *= mt;
        out[n] = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
    }
}

/* ------------------------------------------------------------------ */
/* Wavetable voices                                                    */
/*                                                                     */
/* PARM0/PARM1 (identical apart from bit 0 = which side):              */
/*   bits 31-24  treble control: a two-pole low-pass, Q 0.64, whose    */
/*               cut-off follows log2(fc) = 13.505 + 1.817u + 0.139u^2, */
/*               u = (byte - 0xcf) / 16 (fitted to the real card's      */
/*               echoes, test-suite run 14: 0xc2 = 4.7 kHz, 0xac =       */
/*               1.2 kHz, 0x84 = 250 Hz); 0xcf and above = open         */
/*   bits 23-16  gain, 4e4m, 0x10 per 6 dB, 0xcf = unity, 0 = silent   */
/*   bits 15-0   rate: e = bits 15-13, m = bits 12-0,                  */
/*               rate = 48 kHz * 2^(e-5) * (1 + m/8192)  (confirmed     */
/*               against measured DMA consumption)                     */
/* PARM3: bits 20-14 pan, 13-7 chorus send, 6-0 reverb send (7-bit     */
/*   levels, 0x7f = full).                                             */
/* A voice whose own ADB source address (bank base + voice) is routed   */
/* (the driver routes them to A3D inputs) feeds that source. Otherwise  */
/* its DSREG bit picks the bank's buses: set = the direct-stream pair   */
/* 32/33 (PARM0's gain left, PARM1's right); clear = the main pair      */
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

/* Treble low-pass for one voice: RBJ low-pass biquad, unity at DC. The
   two zeros at Nyquist give b1 = 2 b0, b2 = b0. */
static void
wt_filter_set(v2_wtv_t *w, int f)
{
    if (f == w->fbyte)
        return;
    w->fbyte = f;
    if (f >= 0xcf)
        return;
    double u  = (f - 0xcf) / 16.0;
    double fc = pow(2.0, 13.505 + 1.817 * u + 0.139 * u * u);
    double w0 = 2.0 * M_PI * fc / 48000.0;
    double al = sin(w0) / (2.0 * 0.64);
    double cs = cos(w0);
    double a0 = 1.0 + al;
    w->fb0    = (float) ((1.0 - cs) / 2.0 / a0);
    w->fa1    = (float) (-2.0 * cs / a0);
    w->fa2    = (float) ((1.0 - al) / a0);
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
       fields (Plausible: a register map of the chip gives these outputs
       and fields; Half-Life plays voices 4-29 of the second bank with
       DSREG 0, routes that bank's 36 to the left mix and 37 to the right,
       and sets each voice's two send fields per sound).
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
        if (d->state == DMA_IDLE)
            continue;
        c->dbg |= 2;
        uint32_t p0   = R(bb + WT_PARM + lv * 0x10);
        uint32_t p1   = R(bb + WT_PARM + lv * 0x10 + 4);
        uint32_t rate = p0 & 0xffff;
        double   step = ldexp(1.0 + (double) (rate & 0x1fff) / 8192.0, (int) (rate >> 13) - 5);
        float    gl   = wt_gain(p0);
        float    gr   = wt_gain(p1);
        int      f    = (p0 >> 24) & 0xff;
        wt_filter_set(w, f);
        int      open = f >= 0xcf;
        int      dir  = (routed >> lv) & 1;
        int      dsv  = (ds >> lv) & 1;
        float   *od   = c->wt_direct[bank][lv];
        float  **bus  = NULL;
        float    g4[4] = { gl, gr, 0.0f, 0.0f };
        float   *b4[4] = { c->wt_bus[bank][0], c->wt_bus[bank][1], NULL, NULL };
        if (!dir && !dsv) {
            uint32_t p3  = R(bb + WT_PARM + lv * 0x10 + 12);
            float    pan = (float) ((p3 >> 14) & 0x7f) / 127.0f;
            float    g   = gl * hs;
            g4[0] = g * (1.0f - pan);
            g4[1] = g * pan;
            g4[2] = g * (float) (p3 & 0x7f) / 127.0f;
            g4[3] = g * (float) ((p3 >> 7) & 0x7f) / 127.0f;
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
        if (!open) {
            /* transposed direct form II */
            float z1 = w->z1, z2 = w->z2;
            for (int n = 0; n < B; n++) {
                float x = y[n];
                float o = w->fb0 * x + z1;
                z1      = 2.0f * w->fb0 * x - w->fa1 * o + z2;
                z2      = w->fb0 * x - w->fa2 * o;
                y[n]    = o;
            }
            w->z1 = z1;
            w->z2 = z2;
        }
        if (dir)
            for (int n = 0; n < B; n++)
                od[n] = y[n] * gl;
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
/* hold the glide time constants (HRTF, gain, filter, ITD): current    */
/* values step toward targets every 64 samples by TC * 64 / 2^21 of    */
/* the remaining distance for gain, filter and HRTF, and TC * 64 / 2^19 */
/* for the ITD (tau = 2^21 / TC and 2^19 / TC samples): read back from  */
/* the chip's current registers on a real card for every TC word.      */
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
    c->a3d_used[s] = 1;
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
        float ki = glide_k(c, R(ba + 0x388), 524288.0);
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
    c->a3d_done = 1;
}

/* ------------------------------------------------------------------ */
/* Crosstalk canceller                                                 */
/*                                                                     */
/* The coefficient words are not decoded yet, so the driver's three    */
/* settings are recognised and replaced by measured responses: all    */
/* gain words zero = headphones (pass-through); otherwise the delay    */
/* field of 0x24660 tells "satellites" (0xd) from "monitors" (0x7).    */
/* The measured filters run as uniformly partitioned FFT convolution.  */
/* ------------------------------------------------------------------ */

static void
fft(v2_chip_t *c, cpx *x, int inverse)
{
    for (int i = 0; i < XT_N; i++)
        if (c->rev[i] > i) {
            cpx t        = x[i];
            x[i]         = x[c->rev[i]];
            x[c->rev[i]] = t;
        }
    for (int len = 2; len <= XT_N; len <<= 1) {
        int half = len >> 1, step = XT_N / len;
        for (int i = 0; i < XT_N; i += len)
            for (int j = 0; j < half; j++) {
                cpx w = c->tw[j * step];
                if (inverse)
                    w.im = -w.im;
                cpx u = x[i + j], v = x[i + j + half];
                cpx t = { v.re * w.re - v.im * w.im, v.re * w.im + v.im * w.re };
                x[i + j]        = (cpx) { u.re + t.re, u.im + t.im };
                x[i + j + half] = (cpx) { u.re - t.re, u.im - t.im };
            }
    }
}

static void
xt_prepare(v2_chip_t *c)
{
    static float hs[XT_P * B], hc[XT_P * B];
    const float *E[2] = { v2_xt_wide_E, v2_xt_narrow_E };
    const float *X[2] = { v2_xt_wide_X, v2_xt_narrow_X };

    for (int i = 0; i < XT_N; i++) {
        int r = 0;
        for (int b = 1, j = i; b < XT_N; b <<= 1, j >>= 1)
            r = (r << 1) | (j & 1);
        c->rev[i] = r;
    }
    for (int i = 0; i < XT_N / 2; i++)
        c->tw[i] = (cpx) { (float) cos(-2.0 * M_PI * i / XT_N), (float) sin(-2.0 * M_PI * i / XT_N) };

    /* The fitted E filters are normalised to 0 dB between 300 Hz and 3 kHz;
       their absolute level was not part of the fit. Minerva's DirectSound 3D
       tests, played through the canceller on a real card and with headphones
       in 86Box (same source, same HRTF taps), put the real canceller below
       the normalised model by 5.5 dB for satellites and 6.2 dB for monitors,
       each flat within about 1.5 dB from 200 Hz to 10 kHz (24 half-octave
       bands over two tests). Run 2's tap fit (-6.5 dB, satellites) agrees. */
    static const float scale_m[2] = { 0.531f, 0.490f }; /* wide -5.5 dB, narrow -6.2 dB */

    for (int m = 0; m < 2; m++) {
        /* same side: E delayed by the model latency; cross: E * X */
        memset(hs, 0, sizeof(hs));
        memset(hc, 0, sizeof(hc));
        for (int i = 0; i < V2_XT_TAPS && i + V2_XT_LATENCY < XT_P * B; i++)
            hs[i + V2_XT_LATENCY] = scale_m[m] * E[m][i];
        for (int i = 0; i < V2_XT_TAPS; i++)
            for (int j = 0; j < V2_XT_TAPS && i + j < XT_P * B; j++)
                hc[i + j] += scale_m[m] * E[m][i] * X[m][j];
        for (int p = 0; p < XT_P; p++) {
            cpx bs[XT_N], bc[XT_N];
            for (int i = 0; i < XT_N; i++) {
                bs[i] = (cpx) { (i < B) ? hs[p * B + i] : 0.0f, 0.0f };
                bc[i] = (cpx) { (i < B) ? hc[p * B + i] : 0.0f, 0.0f };
            }
            fft(c, bs, 0);
            fft(c, bc, 0);
            memcpy(c->xt_h[m][0][p], bs, sizeof(bs));
            memcpy(c->xt_h[m][1][p], bc, sizeof(bc));
        }
    }
}

static void
xt_detect(v2_chip_t *c)
{
    int mode = 0;

    if ((R(XT_GAIN_EQL) | R(XT_GAIN_EQR) | R(XT_GAIN_XTL) | R(XT_GAIN_XTR)) & 0xffff) {
        int delay = (R(XT_CTRL) >> 8) & 0x1f;
        mode      = (delay == 0x0d) ? 1 : ((delay == 0x07) ? 2 : 0);
    }
    if (mode != c->xt.mode) {
        v2_log(c, "Vortex2: crosstalk canceller %s\n", mode == 1 ? "satellites" : (mode == 2 ? "monitors" : "pass-through"));
        memset(&c->xt, 0, sizeof(c->xt));
        c->xt.mode = mode;
    }
    c->xt_dirty = 0;
}

static void
xt_run(v2_chip_t *c)
{
    float in[2][B];

    for (int s = 0; s < 2; s++) {
        memset(in[s], 0, sizeof(in[s]));
        for (int k = 0; k < 5; k++) {
            int sink = AK_XTIN + s * 5 + k;
            if (!c->sink_n[sink])
                continue;
            const float *g = gather(c, sink);
            for (int n = 0; n < B; n++)
                in[s][n] += g[n];
        }
    }
    if (c->xt_dirty)
        xt_detect(c);

    /* The model's end-to-end 3D levels (headphone pass-through and the
       canceller's measured scale) were matched to the real card with the
       driver's 0x0a on the canceller's mixer inputs taken as +1.5 dB. The
       exponent/mantissa mixer law makes 0x0a +1.94 dB, so the output is
       trimmed by the difference to keep those measured levels. */
    const float trim = 0.950838f; /* 10^(1.5/20) / 1.25 */

    if (c->xt.mode == 0) {
        for (int s = 0; s < 2; s++)
            for (int n = 0; n < B; n++)
                c->xt_out[s][n] = in[s][n] * trim;
    } else {
        v2_xt_t *x = &c->xt;
        int      m = x->mode - 1;
        int      silent = 1;
        for (int i = 0; i < B && silent; i++)
            if (in[0][i] != 0.0f || in[1][i] != 0.0f)
                silent = 0;
        x->zb = silent ? x->zb + 1 : 0;
        if (x->zb > XT_P + 1) {
            /* Every partition holds silence: the output is silence. */
            memset(c->xt_out, 0, sizeof(c->xt_out));
            c->xt_done = 1;
            return;
        }
        x->head = (x->head + 1) % XT_P;

        /* Both channels through one complex FFT (left real, right
           imaginary), then split into the two half spectra. */
        cpx z[XT_N];
        for (int i = 0; i < B; i++) {
            z[i]     = (cpx) { x->prev[0][i], x->prev[1][i] };
            z[i + B] = (cpx) { in[0][i], in[1][i] };
        }
        memcpy(x->prev, in, sizeof(in));
        fft(c, z, 0);
        cpx *f0 = x->fdl[0][x->head], *f1 = x->fdl[1][x->head];
        for (int k = 0; k <= XT_N / 2; k++) {
            cpx a = z[k], b = z[(XT_N - k) & (XT_N - 1)];
            f0[k] = (cpx) { 0.5f * (a.re + b.re), 0.5f * (a.im - b.im) };
            f1[k] = (cpx) { 0.5f * (a.im + b.im), 0.5f * (b.re - a.re) };
        }

        cpx y[2][XT_N / 2 + 1];
        memset(y, 0, sizeof(y));
        for (int s = 0; s < 2; s++)
            for (int p = 0; p < XT_P; p++) {
                int        slot = (x->head - p + XT_P) % XT_P;
                const cpx *xs   = x->fdl[s][slot];
                const cpx *xo   = x->fdl[s ^ 1][slot];
                const cpx *hs   = c->xt_h[m][0][p];
                const cpx *hc   = c->xt_h[m][1][p];
                for (int k = 0; k <= XT_N / 2; k++) {
                    y[s][k].re += xs[k].re * hs[k].re - xs[k].im * hs[k].im + xo[k].re * hc[k].re - xo[k].im * hc[k].im;
                    y[s][k].im += xs[k].re * hs[k].im + xs[k].im * hs[k].re + xo[k].re * hc[k].im + xo[k].im * hc[k].re;
                }
            }

        /* Both outputs through one inverse FFT: W = Y0 + i Y1, with the
           upper bins rebuilt from conjugate symmetry. */
        cpx w[XT_N];
        for (int k = 0; k <= XT_N / 2; k++)
            w[k] = (cpx) { y[0][k].re - y[1][k].im, y[0][k].im + y[1][k].re };
        for (int k = XT_N / 2 + 1; k < XT_N; k++) {
            int j = XT_N - k;
            w[k]  = (cpx) { y[0][j].re + y[1][j].im, y[1][j].re - y[0][j].im };
        }
        fft(c, w, 1);
        for (int n = 0; n < B; n++) {
            c->xt_out[0][n] = w[B + n].re * (trim / XT_N);
            c->xt_out[1][n] = w[B + n].im * (trim / XT_N);
        }
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
    if (c->host.log) {
        /* the band gain may live at +0x28 (target) as well as +0x2c */
        char line[512];
        int  n = snprintf(line, sizeof(line), "Vortex2: [%9.3f] EQ bypass %04x/%04x gains(+28/+2c) L",
                          v2_now(c), R(EQ_BYPASS_L) & 0xffff, R(EQ_BYPASS_R) & 0xffff);
        for (int b = 0; b < 10; b++)
            n += snprintf(line + n, sizeof(line) - n, " %04x/%04x", R(EQ_BASE + b * 0x30 + 0x28) & 0xffff,
                          R(EQ_BASE + b * 0x30 + 0x2c) & 0xffff);
        snprintf(line + n, sizeof(line) - n, "\n");
        if (strcmp(line + 20, c->eq_logged) != 0) {
            snprintf(c->eq_logged, sizeof(c->eq_logged), "%s", line + 20);
            c->host.log(c->host.priv, line);
        }
    }
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
        int  n = snprintf(line, sizeof(line), "Vortex2: [%9.3f] status: timer %u fired %u acked %u irq-raises %u level %d src %04x mask %08x ctrl %08x |",
                          v2_now(c), c->timer_last & 0xffff, c->n_fired, c->n_acked, c->n_irq_raise, c->irq_level,
                          c->irq_src, R(IRQ_CTRL), R(CHIP_CTRL));
        for (int ch = 0; ch < NR_ADBDMA && n < (int) sizeof(line) - 64; ch++) {
            v2_dma_t *d = &c->adb[ch];
            /* a finished stream whose FIFO has drained reads as idle: leave
               it out; one still draining shows as D */
            if (d->state == DMA_IDLE || (d->state == DMA_END && d->fn == 0))
                continue;
            n += snprintf(line + n, sizeof(line) - n, " A%d:%c%d/%03x %02x%02x",
                          ch, d->state == DMA_END ? 'D' : "IRSE"[d->state], d->page, d->pos & 0xfff,
                          R(dma_cfg(d)) >> 24, R(dma_cfg(d) + 4) >> 24);
        }
        int wt_run = 0, wt_stall = 0, wt_end = 0;
        for (int v = 0; v < NR_WTDMA; v++) {
            wt_run += c->wt[v].state == DMA_RUN;
            wt_stall += c->wt[v].state == DMA_STALL;
            wt_end += c->wt[v].state == DMA_END;
        }
        n += snprintf(line + n, sizeof(line) - n, " | WT run %d stall %d end %d | L/R", wt_run, wt_stall, wt_end);
        static const char *const stage[4] = { "a3d", "xtalk", "eq", "out" };
        for (int st = 0; st < 4 && n < (int) sizeof(line) - 48; st++) {
            double ll = c->lr[st][0], rr = c->lr[st][1], lr = c->lr[st][2];
            if (ll + rr < 1e-9)
                n += snprintf(line + n, sizeof(line) - n, " %s -", stage[st]);
            else
                n += snprintf(line + n, sizeof(line) - n, " %s %.0fdB corr %.2f bal %+.1f", stage[st],
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
            n += snprintf(line + n, sizeof(line) - n, " | S%d tc %x/%x/%x/%x ild tgt %+.1f cur %+.1f gain %.2f",
                          sl, R(ba + 0x37c) & 0xffff, R(ba + 0x380) & 0xffff, R(ba + 0x384) & 0xffff,
                          R(ba + 0x388) & 0xffff, 10.0 * log10((tr + 1e-9) / (tl + 1e-9)),
                          10.0 * log10((cr + 1e-12) / (cl + 1e-12)), a->gain);
        }
        memset(c->a3d_used, 0, sizeof(c->a3d_used));
        snprintf(line + n, sizeof(line) - n, "\n");
        memset(c->lr, 0, sizeof(c->lr));
        c->host.log(c->host.priv, line);
        graph_dump(c);
        c->status_frames = 0;
        c->n_fired = c->n_acked = c->n_irq_raise = 0;
    }

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
            c->n_acked++;
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
        case 0x00010:
        case 0x00010 + WT_BANK_STRIDE:
            /* Polled by the MIDI synthesiser about 100 times a second; the real
               card returned all ones throughout run 12 (no hardware voices in use). */
            v = 0xffffffffu;
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
                if (c->n_zero_timer++ < 20)
                    v2_log(c, "Vortex2: [%9.3f] driver wrote 0 to the sample timer; firing at once\n", v2_now(c));
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
    xt_prepare(c);
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
