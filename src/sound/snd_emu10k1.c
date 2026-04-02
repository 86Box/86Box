/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Creative EMU10K1 (SB Live) audio controller emulation.
 *
 *          Based on the emu10k1 ALSA driver written by Jaroslav Kysela
 *          in turn based on Creative's original open source driver.
 *          Some portions based on the kX driver and Creative literature.
 *
 *
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2023 RichardG.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <inttypes.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/thread.h>
#include <86box/i2c.h>
#include <86box/random.h>
#include <86box/nmi.h>
#include <86box/pci.h>
#include <86box/gameport.h>
#include <86box/sound.h>
#include <86box/snd_ac97.h>
#include <86box/snd_emu8k.h>
#include <86box/snd_mpu401.h>
#include <86box/version.h>
#include <86box/plat_fallthrough.h>
#include <86box/plat_unused.h>

enum {
    EMU10K1 = 0x0002
};
enum {
    SB_LIVE_CT4670 = (EMU10K1 << 16) | 0x0020,
    SB_LIVE_CT4620 = (EMU10K1 << 16) | 0x0021,
    SB_LIVE_CT4780 = (EMU10K1 << 16) | 0x8022,
    SB_LIVE_CT4760 = (EMU10K1 << 16) | 0x8024,
    SB_LIVE_SB0060 = (EMU10K1 << 16) | 0x8061,
    SB_LIVE_SB0220 = (EMU10K1 << 16) | 0x8065,
};

enum {
    TRAP_DMA1 = 0,
    TRAP_DMA2,
    TRAP_PIC1,
    TRAP_PIC2,
    TRAP_SB,
    TRAP_OPL,
    TRAP_MPU,
    TRAP_MAX
};

static const struct {
    const device_t *codec;
    uint32_t        id;
    const uint8_t   rev[2];
} emu10k1_models[] = {
    {
        .id    = SB_LIVE_CT4670,
        .rev   = { 0x04, 0x01 },
        .codec = &ct1297_device
    },
    {
        .id    = SB_LIVE_CT4620,
        .rev   = { 0x04, 0x01 },
        .codec = &ct1297_device
    },
    {
        .id    = SB_LIVE_CT4780,
        .rev   = { 0x07, 0x07 },
        .codec = &cs4297a_device
    },
    {
        .id    = SB_LIVE_CT4760,
        .rev   = { 0x07, 0x07 },
        .codec = &stac9721_device
    },
    {
        .id    = SB_LIVE_SB0060,
        .rev   = { 0x07, 0x07 },
        .codec = &stac9708_device
    },
    {
        .id    = SB_LIVE_SB0220,
        .rev   = { 0x0a, 0x0a },
        .codec = &stac9708_device
    }
};

typedef struct {
    struct _emu10k1_ *dev;
    void             *trap;
    uint8_t           flag;
} emu10k1_io_trap_t;

typedef struct _emu10k1_ {
    emu8k_t emu8k; /* at the beginning so we can cast back */

    int      type;
    int      model;
    uint8_t  pci_slot;
    uint8_t  irq_state;
    uint16_t io_base;

    uint8_t  pci_regs[256];
    uint8_t  pci_game_regs[256];
    uint8_t  io_regs[32];
    uint32_t indirect_regs[4096];
    uint32_t pagemask;
    uint32_t temp_ipr;
    int      timer_interval;
    int      timer_count;
    int      mpu_irq;
    int      fxbuf_half_looped : 1;
    int      adcbuf_half_looped : 1;
    int      micbuf_half_looped : 1;

    struct {
        int64_t  acc; /* claims to be 67-bit in hardware */
        uint32_t regs[256];
        uint32_t etram_mask;
        uint16_t etram[32];
        uint16_t itram[8192]; /* internal TRAM */
        int      skip;
        int      interrupt;
        int      pause : 1, stop : 1;

        thread_t *thread;
        event_t  *wake_dsp_thread;
        event_t  *wake_main_thread;
    } dsp;

    pc_timer_t poll_timer;
    uint64_t   timer_latch;

    ac97_codec_t     *codec;
    mpu_t             mpu[2];
    void             *gameport;
    emu10k1_io_trap_t io_traps[TRAP_MAX];

    int master_vol_l;
    int master_vol_r;
    int pcm_vol_l;
    int pcm_vol_r;
    int cd_vol_l;
    int cd_vol_r;
} emu10k1_t;

#define ENABLE_EMU10K1_LOG 1
#ifdef ENABLE_EMU10K1_LOG
int emu10k1_do_log = ENABLE_EMU10K1_LOG;

static void
emu10k1_log(const char *fmt, ...)
{
    va_list ap;

    if (emu10k1_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}

#    define emu10k1_log_push() if (emu10k1_do_log >= 1) { emu10k1_do_log++; };
#    define emu10k1_log_pop()  if (emu10k1_do_log >= 2) { emu10k1_do_log--; };
#else
#    define emu10k1_log(fmt, ...)
#    define emu10k1_log_push()
#    define emu10k1_log_pop()
#endif

//#define EMU10K1_SAMPLE_DUMP 1
#ifdef EMU10K1_SAMPLE_DUMP
#    ifdef _WIN32
#        include <windows.h>
#    endif
static const char *sample_dump_fn[2]                                                 = { "e10k1fx.wav", "e10k1out.wav" }; /* short names for Audacity UI */
static FILE       *sample_dump_f[sizeof(sample_dump_fn) / sizeof(sample_dump_fn[0])] = { 0 };
static struct {
    struct {
        char     signature[4];
        uint32_t size;
        char     type[4];
    } riff;
    struct {
        char     signature[4];
        uint32_t size;
        uint16_t format;
        uint16_t channels;
        uint32_t sample_rate;
        uint32_t byte_rate;
        uint16_t block_align;
        uint16_t bits_sample;
    } fmt;
    struct {
        char     signature[4];
        uint32_t size;
    } data;
} wav_header = {
    .riff = {
        .signature = {'R', 'I', 'F', 'F'},
        .size = 0,
        .type = {'W', 'A', 'V', 'E'}
    },
    .fmt = {
        .signature = {'f', 'm', 't', ' '},
        .size = 16,
        .format = 1,
        .bits_sample = 16
    },
    .data = {
        .signature = {'d', 'a', 't', 'a'},
        .size = 0
    }
};
#endif

static const uint32_t dsp_constants[] = {
    0x00000000, 0x00000001, 0x00000002, 0x00000003, 0x00000004, 0x00000008, 0x00000010, 0x00000020, /* 40 */
    0x00000100, 0x00010000, 0x00080000, 0x10000000, 0x20000000, 0x40000000, 0x80000000, 0x7fffffff, /* 48 */
    0xffffffff, 0xfffffffe, 0xc0000000, 0x4f1bbcdc, 0x5a7ef9db, 0x00100000                          /* 50 */
};
static const int record_buffer_sizes[] = {
    0, 384, 448, 512,
    640, 384 * 2, 448 * 2, 512 * 2,
    640 * 2, 384 * 4, 448 * 4, 512 * 4,
    640 * 4, 384 * 8, 448 * 8, 512 * 8,
    640 * 8, 384 * 16, 448 * 16, 512 * 16,
    640 * 16, 384 * 32, 448 * 32, 512 * 32,
    640 * 32, 384 * 64, 448 * 64, 512 * 64,
    640 * 64, 384 * 128, 448 * 128, 512 * 128
};

static uint16_t emu10k1_readw(uint16_t addr, void *priv);
static uint32_t emu10k1_readl(uint16_t addr, void *priv);
static void     emu10k1_writew(uint16_t addr, uint16_t val, void *priv);
static void     emu10k1_writel(uint16_t addr, uint32_t val, void *priv);

static inline int32_t
emu10k1_dsp_saturate(emu10k1_t *dev, int64_t i)
{
    /* Check for overflow. */
    if (i > 2147483647)
        i = 2147483647;
    else if (i < -2147483648)
        i = -2147483648;
    else
        return i; /* no overflow */

    /* Set saturation flag. */
    dev->dsp.regs[0x57] |= 0x10; /* S */

    return i;
}

static inline int64_t
emu10k1_dsp_add(emu10k1_t *dev, int64_t a, int64_t b)
{
    /* The borrow flag follows this truth table:
       1) a + b = always set
       2) a + -b = a < abs(b)
       3) -a + b = b < abs(a)
       4) -a + -b = never set */
    if (((a >= 0) && (b >= 0)) || ((a >= 0) && (b < 0) && (a < llabs(b))) || ((a < 0) && (b >= 0) && (b < llabs(a))))
        dev->dsp.regs[0x57] |= 0x02; /* B */
    return a + b;
}

static int32_t
emu10k1_dsp_opMACS(emu10k1_t *dev, int64_t a, int32_t x, int32_t y)
{
    dev->dsp.acc = emu10k1_dsp_saturate(dev, emu10k1_dsp_add(dev, a, ((int64_t) x * y) >> 31));
    return dev->dsp.acc;
}

static int32_t
emu10k1_dsp_opMACS1(emu10k1_t *dev, int64_t a, int32_t x, int32_t y)
{
    dev->dsp.acc = emu10k1_dsp_saturate(dev, emu10k1_dsp_add(dev, a, ((int64_t) -x * y) >> 31));
    return dev->dsp.acc;
}

static int32_t
emu10k1_dsp_opMACW(emu10k1_t *dev, int64_t a, int32_t x, int32_t y)
{
    dev->dsp.acc = emu10k1_dsp_add(dev, a, ((int64_t) x * y) >> 31);
    emu10k1_dsp_saturate(dev, dev->dsp.acc); /* saturation flag still calculated */
    return dev->dsp.acc;
}

static int32_t
emu10k1_dsp_opMACW1(emu10k1_t *dev, int64_t a, int32_t x, int32_t y)
{
    dev->dsp.acc = emu10k1_dsp_add(dev, a, ((int64_t) -x * y) >> 31);
    emu10k1_dsp_saturate(dev, dev->dsp.acc); /* saturation flag still calculated */
    return dev->dsp.acc;
}

static int32_t
emu10k1_dsp_opMACINTS(emu10k1_t *dev, int64_t a, int32_t x, int32_t y)
{
    /* MACINT operations have weird borrow flag handling, seemingly a >= 0... */
    int64_t ret = a + ((int64_t) x * y);
    if (a >= 0)
        dev->dsp.regs[0x57] |= 0x02; /* B */
    /* ...and set the accumulator to the result's [62:31] bits. */
    dev->dsp.acc = ret >> 31;
    return emu10k1_dsp_saturate(dev, ret);
}

static int32_t
emu10k1_dsp_opMACINTW(emu10k1_t *dev, int64_t a, int32_t x, int32_t y)
{
    int64_t ret = a + ((int64_t) x * y);
    if (a >= 0)
        dev->dsp.regs[0x57] |= 0x02; /* B */
    dev->dsp.acc = ret >> 31;
    emu10k1_dsp_saturate(dev, ret); /* saturation flag still calculated */
    return ret & 0x7fffffff;
}

static int32_t
emu10k1_dsp_opACC3(emu10k1_t *dev, int64_t a, int32_t x, int32_t y)
{
    /* The accumulator's lower 32 bits are used, despite documentation.
       X and Y are added first without borrow flag calculation, then A is added
       to X+Y with saturation at the accumulator and borrow flag calculation. */
    dev->dsp.acc = emu10k1_dsp_saturate(dev, emu10k1_dsp_add(dev, a, (int64_t) x + y));
    return dev->dsp.acc;
}

static int32_t
emu10k1_dsp_opMACMV(emu10k1_t *dev, int64_t a, int32_t x, int32_t y)
{
    /* Clearing up unclear documentation:
       - The order is MAC *then* move.
       - The multiplication result is shifted like MACS/MACW, then saturated. */
    dev->dsp.acc = emu10k1_dsp_saturate(dev, emu10k1_dsp_add(dev, dev->dsp.acc, ((int64_t) x * y) >> 31));
    return a;
}

static int32_t
emu10k1_dsp_opANDXOR(emu10k1_t *dev, int64_t a, int32_t x, int32_t y)
{
    /* Borrow flag apparently always set. */
    dev->dsp.regs[0x57] |= 0x02; /* B */
    /* The A operand is copied to the accumulator, which is
       subtracted by 1 if a is positive and y is negative. */
    dev->dsp.acc = a - ((a >= 0) && (y < 0));
    return (a & x) ^ y;
}

static int32_t
emu10k1_dsp_opTSTNEG(emu10k1_t *dev, int64_t a, int32_t x, int32_t y)
{
    /* For the 3 test operations, the operands are subtracted
       into the accumulator and the comparison is done on that.
       The borrow flag is set if the accumulator is negative. */
    dev->dsp.acc = a - y;
    if (dev->dsp.acc < 0) {
        x = ~x;
        dev->dsp.regs[0x57] |= 0x02; /* B */
    }
    return x;
}

static int32_t
emu10k1_dsp_opLIMIT(emu10k1_t *dev, int64_t a, int32_t x, int32_t y)
{
    dev->dsp.acc = a - y;
    if (dev->dsp.acc < 0) {
        dev->dsp.regs[0x57] |= 0x02; /* B */
        return y;
    }
    return x;
}

static int32_t
emu10k1_dsp_opLIMIT1(emu10k1_t *dev, int64_t a, int32_t x, int32_t y)
{
    dev->dsp.acc = a - y;
    if (dev->dsp.acc < 0) {
        dev->dsp.regs[0x57] |= 0x02; /* B */
        return x;
    }
    return y;
}

static uint32_t
emu10k1_dsp_logcompress(int32_t val, int max_exp)
{
    /* Special case: 0 divides the value by 2. */
    if (UNLIKELY(max_exp == 0))
        return val >> 1;

    /* Tweaked from a kX plugin API function written by someone smarter than me. */
    int      exp_bits = log2i(max_exp) + 1;
    uint32_t ret      = (val < 0) ? ~val : val; /* actually's one complement */
    int      msb      = 31 - log2i(ret);
    ret <<= msb;
    int exp = max_exp - msb;
    if (exp >= 0) {
        ret <<= 1;
        exp++;
    } else {
        ret >>= -1 - exp;
        exp = 0;
    }
    ret = (exp << (31 - exp_bits)) | (ret >> (exp_bits + 1));
    return (val < 0) ? ~ret : ret; /* same here */
}

static inline void
emu10k1_dsp_logexp_acc(emu10k1_t *dev, int64_t a, int32_t x, int32_t y)
{
    /* Both LOG and EXP are meant to be used with X = 2~31 and Y = 0~3. While their intended main
       behavior just bit-masks X and Y into range, there is a whole other secondary behavior with
       regards to the accumulator involving unmasked X and Y. Accessing the accumulator result of
       these instructions could be undefined for all I know, but we don't have internal docs to
       prove it, and we won't know if any real-world DSP programs abuse this until one shows up.
       This code is an imperfect approximation (values start deviating from hardware when X < 0
       or Y < 0), but I'm not burning any more time on what appears to be an unlikely scenario. */
    int    point = 31 - (log2i(x & 0x7fffffff) + 1);                 /* how much to shift Y by (including sign bit) */
    double scale = (unsigned int) ((x + 1) << point) / 2147483648.0; /* factor for scaling Y, shifted towards MSB so Y can be scaled in place */
    dev->dsp.acc = a + ((int64_t) (y * scale) >> point);             /* scale Y in place, then shift towards LSB */
    if (x < 0) {
        dev->dsp.acc -= y;              /* subtract Y when X is negative */
        if (dev->dsp.acc < -2147483648) /* saturate without flag */
            dev->dsp.acc = -2147483648;
    }
}

static int32_t
emu10k1_dsp_opLOG(emu10k1_t *dev, int64_t a, int32_t x, int32_t y)
{
    int32_t r = emu10k1_dsp_logcompress(a, x & 0x1f);
    emu10k1_dsp_logexp_acc(dev, a, x, y);

    /* The borrow flag is also always set. */
    dev->dsp.regs[0x57] |= 0x02; /* B */

    /* Apply one's complement transformations. */
    switch (y & 0x3) {
        case 0x1:
            if (r < 0)
                r = ~r;
            break;

        case 0x2:
            if (r < 0)
                break;
            fallthrough;

        case 0x3:
            r = ~r;
            break;
    }

    return r;
}

static uint32_t
emu10k1_dsp_logdecompress(int32_t val, int max_exp)
{
    /* Special case: 0 multiplies the value by 2, and adds 1 if negative. */
    if (UNLIKELY(max_exp == 0))
        return (val << 1) + (val < 0);

    /* Also based on kX and validated on hardware. */
    uint32_t ret      = (val < 0) ? ~val : val; /* still two's complement */
    int      exp_bits = log2i(max_exp) + 1;
    int      msb      = 32 - (log2i(ret) + 1);
    if (msb <= exp_bits) {
        int exp = ret >> (31 - exp_bits);
        ret <<= exp_bits + 1;
        ret >>= exp_bits + 1;
        ret <<= exp_bits + 1;
        ret >>= 1;
        ret += 2147483648;
        ret >>= max_exp + 1 - exp;
    } else {
        uint64_t ret64 = (uint64_t) ret << (exp_bits + 1);
        ret64 <<= msb - exp_bits - 1;
        ret64 >>= msb + max_exp - exp_bits;
        ret = ret64;
    }
    return (val < 0) ? ~ret : ret;
}

static int32_t
emu10k1_dsp_opEXP(emu10k1_t *dev, int64_t a, int32_t x, int32_t y)
{
    emu10k1_dsp_logexp_acc(dev, a, x, y);
    dev->dsp.regs[0x57] |= 0x02; /* B */

    /* Apply one's complement transformations. */
    switch (y & 0x3) {
        case 0x1:
            if (a < 0)
                a = ~a;
            break;

        case 0x2:
            if (a < 0)
                break;
            fallthrough;

        case 0x3:
            a = ~a;
            break;
    }

    return emu10k1_dsp_logdecompress(a, x & 0x1f);
}

static int32_t
emu10k1_dsp_opINTERP(emu10k1_t *dev, int64_t a, int32_t x, int32_t y)
{
    /* Borrow flag always set. The minus flag has further nonsense; hardware sometimes
       doesn't match the result, but my attempts at reverse engineering it failed. */
    dev->dsp.regs[0x57] |= 0x02; /* B */
    dev->dsp.acc = a + (((int64_t) x * (y - a)) >> 31);
    return emu10k1_dsp_saturate(dev, dev->dsp.acc);
}

static int32_t
emu10k1_dsp_opSKIP(emu10k1_t *dev, int64_t a, int32_t x, int32_t y)
{
    /* Generate CC bit string. */
    uint32_t cmp = a & 0x1f;                   /* S Z M B N */
    cmp          = cmp | ((~cmp & 0x1f) << 5); /* S Z M B N S' Z' M' B' N' (hereinafter flags) */

    /* Perform bit testing. */
    uint32_t i = x & 0x3ff;
    uint32_t j = (x >> 10) & 0x3ff;
    uint32_t k = (x >> 20) & 0x3ff;
    switch ((uint32_t) x >> 30) { /* boolean equation */
        case 0x0:                 /* OR(AND(flags), AND(flags), AND(flags)) => used most of the time... */
            cmp = (i && ((i & cmp) == i)) || (j && ((j & cmp) == j)) || (k && ((k & cmp) == k));
            break;

        case 0x1: /* AND(OR(flags), OR(flags), OR(flags)) => ...notable exception of this one as a magic always skip (0x7fffffff) */
            cmp = (i & cmp) && (j & cmp) && (k & cmp);
            break;

        case 0x2: /* OR(AND(flags), AND(flags), OR(flags)) */
            cmp = (i && ((i & cmp) == i)) || (j && ((j & cmp) == j)) || (k & cmp);
            break;

        case 0x3: /* AND(OR(flags), OR(flags), AND(flags)) */
            cmp = (i & cmp) && (j & cmp) && (k && ((k & cmp) == k));
            break;
    }

    /* Mark instruction skip if the test resulted in true. */
    if (cmp)
        dev->dsp.skip = y & 0x1ff; /* can go past the end, skipping all remaining instructions, but the value is masked */

    /* Continuing accumulator behavior from the fetch process.
       The post-skip behavior is handled there as well. */
    dev->dsp.acc += x >> 29;
    if (dev->dsp.acc > 2147483647)
        dev->dsp.acc = 2147483647;
    else if (dev->dsp.acc < -2147483648)
        dev->dsp.acc = -2147483648;

    return a;
}

static int32_t (*emu10k1_dsp_ops[])(emu10k1_t *dev, int64_t a, int32_t x, int32_t y) = {
    emu10k1_dsp_opMACS,
    emu10k1_dsp_opMACS1,
    emu10k1_dsp_opMACW,
    emu10k1_dsp_opMACW1,
    emu10k1_dsp_opMACINTS,
    emu10k1_dsp_opMACINTW,
    emu10k1_dsp_opACC3,
    emu10k1_dsp_opMACMV,
    emu10k1_dsp_opANDXOR,
    emu10k1_dsp_opTSTNEG,
    emu10k1_dsp_opLIMIT,
    emu10k1_dsp_opLIMIT1,
    emu10k1_dsp_opLOG,
    emu10k1_dsp_opEXP,
    emu10k1_dsp_opINTERP,
    emu10k1_dsp_opSKIP
};

static inline uint16_t
emu10k1_dsp_tramcompress(int32_t val)
{
    /* Based on the ALSA DSP code's ETRAM-based playback handler. */
    int32_t ret = emu10k1_dsp_logcompress(val << 12, 7);
    if (ret & 0x80000000)
        ret ^= 0x70000000;
    return ret >> 16;
}

static inline uint32_t
emu10k1_dsp_tramdecompress(int16_t val)
{
    /* Extrapolated from compression. The added 0xffff for negative values reduces error. */
    if (val & 0x8000)
        val ^= 0x7000;
    return emu10k1_dsp_logdecompress((val << 16) | (0xffff * (val < 0)), 7) >> 12;
}

static inline uint32_t
emu10k1_dsp_read(emu10k1_t *dev, int addr, int last_wo_reg, uint32_t last_wo_val)
{
    switch (addr) {
        case 0x58 ... 0x59: /* RNGs */
            return random_generate() << 12;

        case 0x5b: /* DBAC */
            /* Justified to second most significant bit from the DSP's point of view. */
            return dev->dsp.regs[addr] << 11;

        case 0x100 ... 0x1ff: /* GPR */
            return dev->indirect_regs[addr];

        case 0x200 ... 0x2ff: /* TRAM data */
            /* Justified to most significant bit from the DSP's point of view */
            return dev->indirect_regs[addr] << 12;

        case 0x300 ... 0x3ff: /* TRAM address */
            /* Justified to second most significant bit from the DSP's point of view.
               The opcode in the upper bits is discarded and therefore not visible. */
            return (dev->indirect_regs[addr] & 0x000fffff) << 11;

        case 0x00 ... 0x3f: /* inputs and outputs */
        case 0x80 ... 0xff: /* unmapped */
            /* These return the last written value *only* when read back during the next instruction. */
            if (addr == last_wo_reg)
                return last_wo_val;
            fallthrough;

        default: /* DSP registers */
            return dev->dsp.regs[addr];
    }
}

#define SAMPLE_CONV_FACTOR -15406.5397154191 /* average observed through probing 218k sample values (>3 laps around the full range) \
                                                on hardware; produces an average ~0.498 and maximum ~0.996 sample value error */
#define CLAMP(x, min, max) (((x) < (min)) ? (min) : (((x) > (max)) ? (max) : (x)))
#define SAMPLE_16_TO_32(x) CLAMP((int64_t) ((x) *SAMPLE_CONV_FACTOR), -2147483648, 2147483647)
#define SAMPLE_32_TO_16(x) CLAMP((int32_t) (x) / (SAMPLE_CONV_FACTOR * 4), -32768, 32767)

/* Calculation of effective TRAM addresses (addr + DBAC +- align bit) */
#define tram_addr(align, mask) ((tram_op + dev->dsp.regs[0x5b] + (!!(tram_op & 0x00400000) * (align))) & (mask))
#define itram_addr(align)      tram_addr(align, (sizeof(dev->dsp.itram) / sizeof(dev->dsp.itram[0])) - 1)
#define etram_addr(align)      (dev->indirect_regs[0x41] + (tram_addr(align, dev->dsp.etram_mask) << 1))

void
emu10k1_dsp_exec(emu10k1_t *dev, int pos, int32_t *buf)
{
    /* Wait for DSP thread. */
    thread_wait_event(dev->dsp.wake_main_thread, -1);
    thread_reset_event(dev->dsp.wake_main_thread);

    /* Send DSP outputs from the previous run to the audio buffer.
       This should actually be 20 bits sent to the AC97 codec. */
    buf[0] = SAMPLE_32_TO_16(dev->dsp.regs[0x20]);
    buf[1] = SAMPLE_32_TO_16(dev->dsp.regs[0x21]);

    /* Loop DSP outputs back into the FX capture buffer if enabled. */
    int i;
    if (dev->indirect_regs[0x43] && dev->indirect_regs[0x4b]) { /* any FXWC and FXBS set? */
        /* Note that for all capture buffers, neither the base
           nor the size are necessarily aligned to a power of 2. */
        int      buf_size  = record_buffer_sizes[dev->indirect_regs[0x4b]];
        uint32_t fxwc      = dev->indirect_regs[0x43];
        uint32_t base_addr = dev->indirect_regs[0x47];
        uint32_t idx       = dev->indirect_regs[0x65];
        for (i = 0; fxwc; i++) {
            if (fxwc & 1) {                                                      /* loopback enabled for this output? */
                mem_writew_phys(base_addr + idx, dev->dsp.regs[0x20 | i] >> 16); /* upper 16 bits of output */
                idx = (idx + 2) % buf_size;
            }
            fxwc >>= 1;
        }
        dev->indirect_regs[0x65] = idx; /* write new position */

        /* Process loop interrupts. */
        buf_size /= 2; /* compare against half buffer size */
        if (!dev->fxbuf_half_looped && (idx >= buf_size)) {
            dev->temp_ipr |= (*((uint32_t *) &dev->io_regs[0x0c]) & 0x00000020) << 7; /* INTE_EFXBUFENABLE into IPR_EFXBUFHALFFULL = 0x00001000 */
            dev->fxbuf_half_looped = 1;
        } else if (dev->fxbuf_half_looped && (idx < buf_size)) {
            dev->temp_ipr |= (*((uint32_t *) &dev->io_regs[0x0c]) & 0x00000020) << 8; /* INTE_EFXBUFENABLE into IPR_EFXBUFFULL = 0x00002000 */
            dev->fxbuf_half_looped = 0;
        }
    }

    /* Feed samples into the regular capture buffers. */
    if (UNLIKELY(dev->indirect_regs[0x49]) && !(pos % 6)) { /* any MICBS set and we're in an 8 KHz aligned sample? */
        int      buf_size  = record_buffer_sizes[dev->indirect_regs[0x49]];
        uint32_t base_addr = dev->indirect_regs[0x45];
        uint32_t idx       = dev->indirect_regs[0x63 + (dev->type != EMU10K1)];

        mem_writew_phys(base_addr + idx, SAMPLE_32_TO_16(dev->dsp.regs[0x2c]));
        dev->indirect_regs[0x63 + (dev->type != EMU10K1)] = (idx + 2) % buf_size; /* write new position */

        /* Process loop interrupts. */
        buf_size /= 2; /* compare against half buffer size */
        if (!dev->micbuf_half_looped && (idx >= buf_size)) {
            dev->temp_ipr |= (*((uint32_t *) &dev->io_regs[0x0c]) & 0x00000080) << 9; /* INTE_MICBUFENABLE into IPR_MICBUFHALFFULL = 0x00010000 */
            dev->micbuf_half_looped = 1;
        } else if (dev->micbuf_half_looped && (idx < buf_size)) {
            dev->temp_ipr |= (*((uint32_t *) &dev->io_regs[0x0c]) & 0x00000080) << 10; /* INTE_MICBUFENABLE into IPR_MICBUFFULL = 0x00020000 */
            dev->micbuf_half_looped = 0;
        }
    }

    if (UNLIKELY(dev->indirect_regs[0x4a])) { /* any ADCBS set? */
        int      buf_size  = record_buffer_sizes[dev->indirect_regs[0x4a]];
        uint32_t base_addr = dev->indirect_regs[0x46];
        uint32_t idx       = dev->indirect_regs[0x64 - (dev->type != EMU10K1)];

        mem_writel_phys(base_addr + idx, (uint16_t) SAMPLE_32_TO_16(dev->dsp.regs[0x2a]) | ((uint16_t) SAMPLE_32_TO_16(dev->dsp.regs[0x2b]) << 16));
        dev->indirect_regs[0x64 - (dev->type != EMU10K1)] = (idx + 4) % buf_size; /* write new position */

        /* Process loop interrupts. */
        buf_size /= 2; /* compare against half buffer size */
        if (!dev->adcbuf_half_looped && (idx >= buf_size)) {
            dev->temp_ipr |= (*((uint32_t *) &dev->io_regs[0x0c]) & 0x00000040) << 8; /* INTE_ADCBUFENABLE into IPR_ADCBUFHALFFULL = 0x00004000 */
            dev->adcbuf_half_looped = 1;
        } else if (dev->adcbuf_half_looped && (idx < buf_size)) {
            dev->temp_ipr |= (*((uint32_t *) &dev->io_regs[0x0c]) & 0x00000040) << 9; /* INTE_ADCBUFENABLE into IPR_ADCBUFFULL = 0x00008000 */
            dev->adcbuf_half_looped = 0;
        }
    }

    /* Populate FX bus inputs. */
#ifdef EMU10K1_SAMPLE_DUMP
    int16_t fx_samples[dev->emu8k.emu10k1_fxbuses];
    int16_t out_samples[dev->emu8k.emu10k1_fxbuses];
#endif
    for (i = 0; i < dev->emu8k.emu10k1_fxbuses; i++) {
        /* Each FX bus is copied to an equivalent output by default. The
           DSP program can overwrite the outputs to do its own routing. */
#ifdef EMU10K1_SAMPLE_DUMP
        fx_samples[i]  = CLAMP(dev->emu8k.fx_buffer[pos][i], -32768, 32767);
        out_samples[i] = SAMPLE_32_TO_16(dev->dsp.regs[0x20 | i]); /* from previous run */
#endif
        dev->dsp.regs[i] = dev->dsp.regs[0x20 | i] = SAMPLE_16_TO_32(dev->emu8k.fx_buffer[pos][i]);
    }
#ifdef EMU10K1_SAMPLE_DUMP
    if (sample_dump_f[0])
        fwrite(fx_samples, sizeof(fx_samples[0]), dev->emu8k.emu10k1_fxbuses, sample_dump_f[0]);
    if (sample_dump_f[1])
        fwrite(out_samples, sizeof(out_samples[0]), dev->emu8k.emu10k1_fxbuses, sample_dump_f[1]);
#endif

    /* Don't execute if the DSP is paused. */
    if ((dev->indirect_regs[0x52] & 0x00008000) || UNLIKELY(dev->dsp.pause)) {
        memset(&dev->dsp.regs[0x20], 0, 32 * sizeof(dev->dsp.regs[0])); /* null samples are output when DSP is off */
        goto end;
    }

    /* Decrement DBAC. */
    dev->dsp.regs[0x5b] = (dev->dsp.regs[0x5b] - 1) & 0xfffff;

    /* Perform external TRAM operations, unless HCFG_LOCKTANKCACHE is set. */
    if (!(dev->io_regs[0x14] & 0x04)) {
        for (int tram = 0; tram < 0x20; tram++) {
            uint32_t tram_op = dev->indirect_regs[0x380 | tram];
            if (tram_op & 0x00100000) /* READ */
                dev->dsp.etram[tram] = mem_readw_phys(etram_addr(-1));
            if (tram_op & 0x00a00000) /* CLEAR/WRITE */
                mem_writew_phys(etram_addr(1), dev->dsp.etram[tram]);
        }
    }

end:
    /* Wake DSP thread. */
    thread_set_event(dev->dsp.wake_dsp_thread);
}

void
emu10k1_dsp_thread(void *priv)
{
    emu10k1_t *dev = (emu10k1_t *) priv;

#define RUNNING_CODE() (fetch)
#define ANY_REG(v)     ((r == (v)) || (a == (v)) || (x == (v)) || (y == (v)))
#define ANY_REG_VAL(v) ((rval == (v)) || (aval == (v)) || (xval == (v)) || (yval == (v)))
#if 0
#define EMU10K1_DSP_TRACE dev->dsp.regs[0] && /*RUNNING_CODE() &&*/ (ANY_REG(0x00) || ANY_REG(0x04) || ANY_REG(0x20) || ANY_REG(0x100) || ANY_REG(0x102) || ANY_REG(0x10c) || ANY_REG(0x114))
#endif
extern uint8_t keyboard_get_shift(void);
#define EMU10K1_DSP_TRACE (keyboard_get_shift() & 0x01)

    thread_set_event(dev->dsp.wake_main_thread);

    while (!dev->dsp.stop) {
        /* Wait for main thread. */
        thread_wait_event(dev->dsp.wake_dsp_thread, -1);
        thread_reset_event(dev->dsp.wake_dsp_thread);

#ifdef EMU10K1_DSP_TRACE
        if (EMU10K1_DSP_TRACE)
            pclog("EMU10K1: DSP in %08" PRIX32 " %08" PRIX32 "\n", dev->dsp.regs[0], dev->dsp.regs[1]);
#endif

        /* Update TRAM. */
        for (int tram = 0; tram < 0x80; tram++) {
            uint32_t tram_op = dev->indirect_regs[0x300 | tram];
            if (tram_op & 0x00100000) /* READ */
                dev->indirect_regs[0x200 | tram] = emu10k1_dsp_tramdecompress(dev->dsp.itram[itram_addr(-1)]);
            if (tram_op & 0x00800000) /* CLEAR */
                dev->dsp.itram[itram_addr(1)] = 0;
            else if (tram_op & 0x00200000) /* WRITE */
                dev->dsp.itram[itram_addr(1)] = emu10k1_dsp_tramcompress(dev->indirect_regs[0x200 | tram]);
        }
        for (int tram = 0; tram < 0x20; tram++) {
            uint32_t tram_op = dev->indirect_regs[0x380 | tram];
            if (tram_op & 0x00100000) /* READ */
                dev->indirect_regs[0x280 | tram] = emu10k1_dsp_tramdecompress(dev->dsp.etram[tram]);
            if (tram_op & 0x00800000) /* CLEAR */
                dev->dsp.etram[tram] = 0;
            else if (tram_op & 0x00200000) /* WRITE */
                dev->dsp.etram[tram] = emu10k1_dsp_tramcompress(dev->indirect_regs[0x280 | tram]);
        }

        /* Execute DSP instruction stream. */
        const uint64_t *code        = (uint64_t *) &dev->indirect_regs[0x400];
        int             pc          = 0;
        int             last_wo_reg = -1;
        uint32_t        last_wo_val = 0;
        while (pc < 0x200) {
            /* Decode instruction. */
            uint64_t fetch = code[pc];
            int      y     = fetch & 0x3ff;
            int      x     = (fetch >> 10) & 0x3ff;
            int      a     = (fetch >> 32) & 0x3ff;
            int      r     = (fetch >> 42) & 0x3ff;
            int      op    = (fetch >> (*OP_BASE ^ 014)) & 0xf;

            /* Read operands.
               The A operand has some special cases which read as 0 if not fulfilled. */
            int64_t aval = (int32_t) emu10k1_dsp_read(dev, a, last_wo_reg, last_wo_val);
            if ((a == 0x56) && (op != 0x07)) {
                /* Accumulator can only be specified as A, except on MACMV where it can't be read. */
                aval = dev->dsp.acc;
            } else if (op == 0x0f) {
                if (UNLIKELY(a != 0x57)) {
                    /* Despite documentation, SKIP can only read the flags register as A.
                       If a different A is provided, its value is moved to the accumulator. */
                    dev->dsp.acc = aval;
                    aval         = 0;
                } else {
                    /* Accumulator is 0 when reading flags as A. */
                    dev->dsp.acc = 0;
                }
            }
            int32_t xval = emu10k1_dsp_read(dev, x, last_wo_reg, last_wo_val);
            int32_t yval = emu10k1_dsp_read(dev, y, last_wo_reg, last_wo_val);
            last_wo_reg  = -1;

            /* Fetch but don't execute the last instruction before a skip target... */
            if (UNLIKELY(dev->dsp.skip)) {
                dev->dsp.skip = 0;

                /* ...as the accumulator is set to the last Y value fetched before the target. */
                dev->dsp.acc = yval;

                /* Move on to the next instruction. */
                pc++;
                continue;
            }

            /* Clear flags now, as operation code may set them, unless
               we're in a SKIP operation, which always preserves flags. */
            if (LIKELY(op != 0xf))
                dev->dsp.regs[0x57] = 0;

            /* Execute operation. */
            int32_t rval = emu10k1_dsp_ops[op](dev, aval, xval, yval);

            /* Calculate remaining flags, unless we just processed a SKIP. */
            if (LIKELY(op != 0xf))
                dev->dsp.regs[0x57] |= ((rval < -0x40000000) || (rval >= 0x40000000)) | /* N = 0x01 */
                    ((rval < 0) << 2) |                                                 /* M = 0x04 */
                    ((rval == 0) << 3);                                                 /* Z = 0x08 */

#ifdef EMU10K1_DSP_TRACE
            if (EMU10K1_DSP_TRACE)
                pclog("EMU10K1: %03X OP(%X, %03X:%08" PRIX32 ", %03X:%08" PRIX64 ", %03X:%08" PRIX32 ", %03X:%08" PRIX32 ") fl=%02" PRIX32 "\n",
                      pc, op, r, rval, a, aval, x, xval, y, yval, dev->dsp.regs[0x57] & 0x1f);
#endif

            /* Set debug register. */
            uint32_t debug = (dev->indirect_regs[0x52] & ~0x01ff0000) | (dev->dsp.regs[0x57] << 9);
            if (dev->dsp.regs[0x57] & 0x10)
                debug |= 0x02000000 | (r << 16);
            dev->indirect_regs[0x52] = debug;

            /* Write result operand. */
            switch (r) {
                case 0x20 ... 0x3f: /* external and FX bus outputs */
                    dev->dsp.regs[r] = rval;
                    fallthrough;

                case 0x00 ... 0x1f: /* external and FX bus inputs */
                case 0x80 ... 0xff: /* unmapped */
                    /* Save last written value for special read handling. */
                    last_wo_reg = r;
                    last_wo_val = rval;
                    break;

                case 0x5a: /* interrupt register */
                    if (LIKELY(rval & 0x80000000))
                        dev->dsp.interrupt = 1;
                    break;

                case 0x100 ... 0x1ff: /* GPR */
                    dev->indirect_regs[r] = rval;
                    break;

                case 0x200 ... 0x2ff: /* TRAM data */
                    /* Justified to most significant bit from the DSP's point of view. */
                    dev->indirect_regs[r] = rval >> 12;
                    break;

                case 0x300 ... 0x3ff: /* TRAM address */
                    /* Justified to second most significant bit from the DSP's point of view.
                       The opcode in the upper bits is inaccessible and must be kept intact. */
                    dev->indirect_regs[r] = (dev->indirect_regs[r] & 0xfff00000) | ((rval >> 11) & 0x000fffff);
                    break;

                    /* default: not writable */
            }

            /* Increment program counter. If we're skipping instructions, leave the last one
               out of the direct skip as we need it to be fetched for accumulator behavior. */
            pc += MAX(dev->dsp.skip, 1);
        }

        /* Wake main thread. */
        thread_set_event(dev->dsp.wake_main_thread);
    }
}

static void
emu10k1_update_irqs(emu10k1_t *dev)
{
    /* Load interrupt enable flags and current status. */
    uint32_t ipr  = *((uint32_t *) &dev->io_regs[0x08]);
    uint32_t inte = *((uint32_t *) &dev->io_regs[0x0c]);

    /* Set channel loop interrupts. */
    if (dev->emu8k.lip) {
        /* Calculate highest active channel for IPR_CHANNELNUMBER. */
        uint64_t any_ip  = *((uint64_t *) &dev->indirect_regs[0x5a]) | *((uint64_t *) &dev->indirect_regs[0x68]);
        int      channel = -1;
        while (any_ip) {
            any_ip >>= 1;
            channel++;
        }

        /* Does any channel still have pending interrupts? */
        if (channel > -1) {
            ipr = (ipr & ~0x0000007f) | 0x40 | channel; /* IPR_CHANNELLOOP | IPR_CHANNELNUMBER */
        } else {
            /* Clear interrupt if no channels are left. */
            dev->emu8k.lip = 0;
            ipr &= ~0x0000007f; /* clear IPR_CHANNELLOOP | IPR_CHANNELNUMBER */
        }
    }

    /* Set MPU-401 receive buffer non-empty interrupt as long as there's a byte in the queue. */
    ipr &= ~0x08000080; /* automatically cleared once conditions clear */
    if (dev->mpu[0].queue_used)
        ipr |= (inte & 0x00000001) << 7; /* INTE_MIDIRXENABLE into IPR_MIDIRECVBUFEMPTY (a misnomer) = 0x00000080 */
    if (dev->mpu[1].queue_used)
        ipr |= (inte & 0x00010000) << 11; /* INTE_A_MIDIRXENABLE2 into IPR_A_MIDIRECVBUFEMPTY2 (a misnomer) = 0x08000000 */

    /* Set forced interrupt flag. */
    ipr |= (inte & 0x00100000) << 2; /* INTE_FORCEINT into IPR_FORCEINT = 0x00400000 */

    /* Save new interrupt pending status. */
    *((uint32_t *) &dev->io_regs[0x08]) = ipr;

    /* Raise or lower IRQ according to interrupt flags. */
    if (ipr) {
        pci_set_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
        emu10k1_log("EMU10K1: Raising IRQ\n");
    } else {
        pci_clear_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
        emu10k1_log("EMU10K1: Lowering IRQ\n");
    }
}

static void
emu10k1_mpu_irq_update(void *priv, UNUSED(int set))
{
    emu10k1_t *dev = (emu10k1_t *) priv;
    /* Our current MPU-401 implementation calls this from a thread.
       Set a flag so that interrupts can be updated from the poll timer.
       Timing will be slightly off, but that's the best we can do for now. */
    dev->mpu_irq = 1;
}

static int
emu10k1_mpu0_irq_pending(void *priv)
{
    const emu10k1_t *dev = (emu10k1_t *) priv;

    return dev->io_regs[0x08] & 0x80;
}

static int
emu10k1_mpu1_irq_pending(void *priv)
{
    const emu10k1_t *dev = (emu10k1_t *) priv;

    return dev->io_regs[0x0b] & 0x08;
}

static void
emu10k1_io_trap(int size, uint16_t addr, uint8_t write, uint8_t val, void *priv)
{
    emu10k1_io_trap_t *trap = (emu10k1_io_trap_t *) priv;

#ifdef ENABLE_EMU10K1_LOG
    if (write)
        emu10k1_log("EMU10K1: io_trap(%04X, %02X)\n", addr, val);
    else
        emu10k1_log("EMU10K1: io_trap(%04X)\n", addr);
#endif

    /* Set trap event data in HCFG. */
    trap->dev->io_regs[0x16] = (trap->dev->io_regs[0x16] & ~0xc0) | (write ? 0x80 : 0x00) | ((size > 1) ? 0x40 : 0x00) | 0x20;
    trap->dev->io_regs[0x17] = trap->flag | (addr & 0x1f); /* irrelevant address bits (such as OPL[4:3]) are still passed through */

    /* Raise NMI. */
    nmi = 1;
}

static void
emu10k1_remap_traps(emu10k1_t *dev)
{
    io_trap_remap(dev->io_traps[TRAP_DMA1].trap, dev->io_regs[0x0f] & 0x08, 0x00, 16);
    io_trap_remap(dev->io_traps[TRAP_DMA2].trap, dev->io_regs[0x0f] & 0x04, 0xc0, 32);
    io_trap_remap(dev->io_traps[TRAP_PIC1].trap, dev->io_regs[0x0f] & 0x02, 0x20, 2);
    io_trap_remap(dev->io_traps[TRAP_PIC2].trap, dev->io_regs[0x0f] & 0x01, 0xa0, 2);
    io_trap_remap(dev->io_traps[TRAP_SB].trap, dev->io_regs[0x0e] & 0x80, 0x220 + ((dev->io_regs[0x0f] & 0xc0) >> 1), 16);
    io_trap_remap(dev->io_traps[TRAP_OPL].trap, dev->io_regs[0x0e] & 0x40, 0x388, 4);
    io_trap_remap(dev->io_traps[TRAP_MPU].trap, dev->io_regs[0x0e] & 0x20, 0x300 | (dev->io_regs[0x0f] & 0x30), 2);
}

#define EMU10K1_MMU_UNMAPPED ((uint32_t) -1)

static inline uint32_t
emu10k1_mmutranslate(emu10k1_t *dev, emu8k_voice_t *voice, uint32_t page)
{
    /* Check TLB entries. */
    uint32_t pte;
    for (int i = 0; i < (sizeof(voice->map) / sizeof(voice->map[0])); i++) {
        pte = voice->map[i];
        if ((pte & dev->pagemask) == page)
            goto found_page;
    }

    /* Perform page walk. */
    uint32_t ptb     = dev->indirect_regs[0x40];
    uint32_t ptb_end = ptb + (dev->pagemask << 2);
    for (; ptb <= ptb_end; ptb += 4) {
        pte = mem_readl_phys(ptb);
        if ((pte & dev->pagemask) == page) {
            /* Add TLB entry. */
            voice->map[voice->tlb_pos] = pte;
            voice->tlb_pos             = (voice->tlb_pos + 1) & ((sizeof(voice->map) / sizeof(voice->map[0])) - 1);
            goto found_page;
        }
    }

    /* Nothing found. */
    return EMU10K1_MMU_UNMAPPED;

found_page:
    /* EMU10K1 is notorious for its "31-bit" DMA, where pte[31:13] = addr[30:12] */
    return (pte >> !(dev->io_regs[0x16] & 0x04)) & 0xfffff000;
}

static uint32_t
emu10k1_mem_readl(emu8k_t *emu8k, emu8k_voice_t *voice, uint32_t addr)
{
    emu10k1_t *dev = (emu10k1_t *) emu8k;

    uint32_t page = UNLIKELY(dev->io_regs[0x14] & 0x08) /* HCFG_LOCKSOUNDCACHE */ ? EMU10K1_MMU_UNMAPPED : emu10k1_mmutranslate(dev, voice, addr >> 12);
    if (UNLIKELY(page == EMU10K1_MMU_UNMAPPED))
        return 0;

    return mem_readl_phys(page | (addr & 0x00000fff));
}

static uint8_t
emu10k1_readb(uint16_t addr, void *priv)
{
    emu10k1_t *dev = (emu10k1_t *) priv;
    addr &= 0x1f;
    uint8_t ret = 0;
    int     reg;
#ifdef ENABLE_EMU10K1_LOG
    reg = -1;
#endif

    switch (addr) {
        case 0x04 ... 0x07: /* DATA */
            reg = *((uint16_t *) &dev->io_regs[0x02]);
            switch (reg) {
                case 0x70 ... 0x73: /* A_MUDATA1 ... A_MUCMD2 */
                    if ((dev->type != EMU10K1) && !(addr & 3))
                        ret = mpu401_read(reg, &dev->mpu[(reg & 2) >> 1]);
                    break;

                default:
                    goto readb_fallback;
            }
            break;

        case 0x08: /* IPR */
            /* If no interrupt flags are read, update interrupts beforehand. This
               helps synchronize our threaded MPU-401 input on the ALSA driver. */
            if (!*((uint32_t *) &dev->io_regs[0x08]))
                emu10k1_update_irqs(dev);
            goto io_reg;

        case 0x18: /* MUDATA */
        case 0x19: /* MUSTAT */
            if (dev->type == EMU10K1)
                ret = mpu401_read(addr, &dev->mpu[0]);
            else
                goto io_reg;
            break;

        case 0x1e: /* AC97ADDRESS */
            if (dev->codec)
                ret = 0x80; /* AC97ADDRESS_READY */
            goto io_reg;

        case 0x10: /* 16/32-bit registers */
        case 0x1c ... 0x1d:
readb_fallback:
            emu10k1_log_push();
            ret = emu10k1_readw(addr & ~0x01, priv) >> ((addr & 1) << 3);
            emu10k1_log_pop();
            break;

        default:
io_reg:
            ret |= dev->io_regs[addr];
            break;
    }

#ifdef ENABLE_EMU10K1_LOG
    if (emu10k1_do_log == 1) {
        if (reg > -1)
            emu10k1_log("EMU10K1: read_i(%d, %03X) = %02X\n", dev->emu8k.cur_voice, reg, ret);
        else
            emu10k1_log("EMU10K1: read(%02X) = %02X\n", addr, ret);
    }
#endif
    return ret;
}

static uint16_t
emu10k1_readw(uint16_t addr, void *priv)
{
    emu10k1_t *dev = (emu10k1_t *) priv;
    addr &= 0x1f;
    uint16_t ret;
    int      reg;
#ifdef ENABLE_EMU10K1_LOG
    reg = -1;
#endif

    switch (addr) {
        case 0x04: /* DATA */
        case 0x06:
            reg = *((uint16_t *) &dev->io_regs[0x02]);
            switch (reg) {
                case 0x00 ... 0x07: /* CPF ... DSL */
                    ret = emu8k_inw(0x600 | (addr & 2), &dev->emu8k);
                    break;

                case 0x08: /* CCCA */
                    ret = emu8k_inw(0xa00 | (addr & 2), &dev->emu8k);
                    break;

                case 0x10 ... 0x17: /* ENVVOL ... LFO2VAL */
                    dev->emu8k.cur_reg = 4 | (dev->emu8k.cur_reg >> 1);
                    ret                = (addr & 2) ? 0 : emu8k_inw(0xa00 | ((reg & 1) << 1), &dev->emu8k);
                    break;

                case 0x18 ... 0x1e: /* IP ... TEMPENV */
                case 0x1f:          /* EMU8000 ID register (unknown whether or not it applies here!) */
                    ret = (addr & 2) ? 0 : emu8k_inw(0xe00, &dev->emu8k);
                    break;

                case 0x70 ... 0x73: /* 8-bit registers */
                    goto readw_fallback8;

                default: /* 32-bit registers */
                    goto readw_fallback32;
            }
            break;

        case 0x1c: /* AC97DATA */
            /* Codec functions discard the MSB and LSB of AC97ADDRESS. */
            ret = dev->codec ? ac97_codec_readw(dev->codec, dev->io_regs[0x1e]) : 0xffff;
            break;

        case 0x10: /* 32-bit registers */
        case 0x12:
readw_fallback32:
            emu10k1_log_push();
            ret = emu10k1_readl(addr & ~0x03, priv) >> ((addr & 2) << 3);
            emu10k1_log_pop();
            break;

        default: /* 8-bit registers or unaligned operation */
readw_fallback8:
            emu10k1_log_push();
            ret = emu10k1_readb(addr, priv);
            ret |= emu10k1_readb(addr + 1, priv) << 8;
            emu10k1_log_pop();
            break;
    }

#ifdef ENABLE_EMU10K1_LOG
    if (emu10k1_do_log == 1) {
        if (reg > -1)
            emu10k1_log("EMU10K1: read_i(%d, %03X) = %04X\n", dev->emu8k.cur_voice, reg, ret);
        else
            emu10k1_log("EMU10K1: read(%02X) = %04X\n", addr, ret);
    }
#endif
    return ret;
}

static uint32_t
emu10k1_readl(uint16_t addr, void *priv)
{
    emu10k1_t *dev = (emu10k1_t *) priv;
    addr &= 0x1f;
    uint32_t ret;
    int      reg;
#ifdef ENABLE_EMU10K1_LOG
    reg = -1;
#endif

    switch (addr) {
        case 0x04: /* DATA */
            reg = *((uint16_t *) &dev->io_regs[0x02]);
            switch (reg) {
                case 0x09: /* CCR */
                    ret = dev->emu8k.voice[dev->emu8k.cur_voice].ccr;
                    break;

                case 0x0a ... 0x0b:                                        /* CLP ... FXRT */
                    ret = dev->emu8k.voice[dev->emu8k.cur_voice].clp_fxrt; /* two registers share one 32-bit word */
                    break;

                case 0x0c ... 0x0d: /* MAPA ... MAPB */
                    ret = dev->emu8k.voice[dev->emu8k.cur_voice].map[reg & 0x01];
                    break;

                case 0x20 ... 0x3f: /* CD */
                    ret = ((uint32_t *) dev->emu8k.voice[dev->emu8k.cur_voice].cd)[reg & 0x1f];
                    break;

                case 0x7d: /* A_SENDAMOUNTS */
                    if (dev->type == EMU10K1)
                        goto indirect_reg;
                    ret = dev->emu8k.voice[dev->emu8k.cur_voice].sendamounts;
                    break;

                case 0x00 ... 0x08: /* 8/16-bit registers */
                case 0x10 ... 0x1f:
                case 0x70 ... 0x73:
                    goto readl_fallback;

                default:
indirect_reg:
                    ret = dev->indirect_regs[reg];
                    break;
            }
            break;

        case 0x10: /* WC */
            /* [5:0] is channel being processed, but we service all in one go */
            ret = (dev->emu8k.wc << 6) & 0x03ffffc0;
            break;

        default: /* 8/16-bit registers or unaligned operation */
readl_fallback:
            emu10k1_log_push();
            ret = emu10k1_readw(addr, priv);
            ret |= emu10k1_readw(addr + 2, priv) << 16;
            emu10k1_log_pop();
            break;
    }

#ifdef ENABLE_EMU10K1_LOG
    if (emu10k1_do_log == 1) {
        if (reg > -1)
            emu10k1_log("EMU10K1: read_i(%d, %03X) = %08X\n", dev->emu8k.cur_voice, reg, ret);
        else
            emu10k1_log("EMU10K1: read(%02X) = %08X\n", addr, ret);
    }
#endif
    return ret;
}

static void
emu10k1_writeb(uint16_t addr, uint8_t val, void *priv)
{
    emu10k1_t *dev = (emu10k1_t *) priv;
    addr &= 0x1f;
#ifdef ENABLE_EMU10K1_LOG
    if ((emu10k1_do_log == 1) && (addr >= 0x08))
        emu10k1_log("EMU10K1: write(%02X, %02X)\n", addr, val);
#endif
    int reg;

    switch (addr) {
        case 0x00: /* PTR_CHANNELNUM */
            val &= dev->emu8k.nvoices - 1;
            dev->emu8k.cur_voice = val;
            break;

        case 0x02: /* PTR_ADDRESS[7:0] */
            dev->emu8k.cur_reg = val & 7;
            break;

        case 0x03: /* PTR_ADDRESS[10:8] */
            val &= (dev->type == EMU10K1) ? 0x07 : 0x0f;
            break;

        case 0x04 ... 0x07: /* DATA */
            reg = *((uint16_t *) &dev->io_regs[0x02]);
            emu10k1_log("EMU10K1: write_i(%d, %03X, %02X)\n", dev->emu8k.cur_voice, reg, val);
            switch (reg) {
                case 0x70 ... 0x73: /* A_MUDATA1 ... A_MUCMD2 */
                    if ((dev->type != EMU10K1) && !(addr & 3)) {
                        mpu401_write(reg, val, &dev->mpu[(reg & 2) >> 1]);

                        /* We have no transmit buffer, so raise the transmit buffer empty interrupt after every data write. */
                        if (!(reg & 1) && (dev->io_regs[!(reg & 2) ? 0x0c : 0x0e] & 0x02)) { /* INTE_MIDITXENABLE : IPR_MIDITRANSBUFEMPTY */
                            if (!(reg & 2))
                                dev->io_regs[0x09] |= 0x01; /* IPR_MIDITRANSBUFEMPTY */
                            else
                                dev->io_regs[0x0b] |= 0x10; /* IPR_A_MIDITRANSBUFEMPTY2 */
                            emu10k1_update_irqs(dev);
                        }
                    }
                    break;

                default: /* 16/32-bit registers */
                    goto writeb_fallback;
            }
            return;

        case 0x08: /* IPR[7:0] */
            dev->io_regs[addr] &= ~(val & 0xc0);
            /* Clear pending interrupt flags for a channel when it's written back. */
            if (val & 0x40) {
                val &= 0x3f;
                *((uint64_t *) &dev->indirect_regs[0x5a]) &= ~(1ULL << val);
                *((uint64_t *) &dev->indirect_regs[0x68]) &= ~(1ULL << val);
            }
            emu10k1_update_irqs(dev);
            return;

        case 0x09 ... 0x0b: /* IPR[31:8] */
            dev->io_regs[addr] &= ~val;
            emu10k1_update_irqs(dev);
            return;

        case 0x0c: /* INTE[7:0] */
            dev->io_regs[addr] = val;
            emu10k1_update_irqs(dev);
            return;

        case 0x0d: /* INTE[15:8] */
            dev->io_regs[addr] = val & 0x3f;
            emu10k1_update_irqs(dev);
            return;

        case 0x0e: /* INTE[23:16] */
            dev->io_regs[addr] = val & ((dev->type == EMU10K1) ? 0xf8 : 0xfb);
            emu10k1_update_irqs(dev);
            emu10k1_remap_traps(dev);
            return;

        case 0x0f: /* INTE[31:24] */
            dev->io_regs[addr] = val;
            emu10k1_remap_traps(dev);
            return;

        case 0x15: /* HCFG[15:8] */
            val = (val & 0x1f) | (dev->io_regs[addr] & ~0x1f);
            break;

        case 0x16:            /* HCFG[23:16] */
            if (val & 0x20) { /* clear LEGACYINT */
                dev->io_regs[addr] &= ~0x20;
                nmi = 0;
            }
            reg = (dev->type == EMU10K1) ? 0x19 : 0x1d;
            val = (val & reg) | (dev->io_regs[addr] & ~reg);
            break;

        case 0x17: /* HCFG[31:24] */
            val &= 0xfd;
            break;

        case 0x18: /* MUDATA / A_GPOUTPUT */
        case 0x19: /* MUCMD / A_GPINPUT */
            if (dev->type == EMU10K1) {
                mpu401_write(addr, val, &dev->mpu[0]);

                /* We have no transmit buffer, so raise the transmit buffer empty interrupt after every data write. */
                if (!(addr & 1) && (dev->io_regs[0x0c] & 0x02)) { /* IPR_MIDITRANSBUFEMPTY gated by INTE_MIDITXENABLE */
                    dev->io_regs[0x09] |= 0x01;
                    emu10k1_update_irqs(dev);
                }
                return;
            } else if (addr & 1) {
                return;
            }
            break;

        case 0x1b: /* TIMER[9:8] */
            val &= 0x03;
            fallthrough;

        case 0x1a: /* TIMER[7:0] */
            dev->io_regs[addr]  = val;
            dev->timer_interval = *((uint16_t *) &dev->io_regs[0x1a]);
            if (dev->timer_interval == 0) /* wrap-around */
                dev->timer_interval = 1024;
            dev->timer_count = 0;
            return;

        case 0x1e: /* AC97ADDRESS */
            val &= 0x7f;
            break;

        case 0x1c ... 0x1d: /* 16-bit registers */
writeb_fallback:
            emu10k1_log_push();
            if (!(addr & 1))
                emu10k1_writew(addr & ~0x01, val | (emu10k1_readw(addr, priv) & 0xff00), priv);
            else
                emu10k1_writew(addr & ~0x01, (val << 8) | (emu10k1_readw(addr & ~0x01, priv) & 0x00ff), priv);
            emu10k1_log_pop();
            return;

        case 0x14: /* HCFG[7:0] */
            break;

        default:
            return;
    }

    dev->io_regs[addr] = val;
}

static void
emu10k1_writew(uint16_t addr, uint16_t val, void *priv)
{
    emu10k1_t *dev = (emu10k1_t *) priv;
    addr &= 0x1f;
#ifdef ENABLE_EMU10K1_LOG
    if ((emu10k1_do_log == 1) && (addr != 0x00) && (addr != 0x02) && (addr != 0x04) && (addr != 0x06))
        emu10k1_log("EMU10K1: write(%02X, %04X)\n", addr, val);
#endif
    int reg;

    switch (addr) {
        case 0x04: /* DATA */
        case 0x06:
            reg = *((uint16_t *) &dev->io_regs[0x02]);
            emu10k1_log("EMU10K1: write_i(%d, %03X, %04X)\n", dev->emu8k.cur_voice, reg, val);
            switch (reg) {
                case 0x00 ... 0x07: /* CPF ... DSL */
                    emu8k_outw(0x600 | (addr & 2), val, &dev->emu8k);
                    break;

                case 0x08: /* CCCA */
                    emu8k_outw(0xa00 | (addr & 2), val, &dev->emu8k);
                    break;

                case 0x10 ... 0x17: /* ENVVOL ... LFO2VAL */
                    dev->emu8k.cur_reg = 4 | (dev->emu8k.cur_reg >> 1);
                    if (!(addr & 2))
                        emu8k_outw(0xa00 | ((reg & 1) << 1), val, &dev->emu8k);
                    break;

                case 0x18 ... 0x1e: /* IP ... TEMPENV */
                case 0x1f:          /* EMU8000 ID register (unknown whether or not it applies here!) */
                    if (!(addr & 2))
                        emu8k_outw(0xe00, val, &dev->emu8k);
                    break;

                case 0x70 ... 0x73: /* 8-bit registers */
                    goto writew_fallback;

                default: /* 32-bit registers */
                    if (!(addr & 2))
                        emu10k1_writel(addr & ~0x03, val | (emu10k1_readl(addr, priv) & 0xffff0000), priv);
                    else
                        emu10k1_writel(addr & ~0x03, (val << 16) | (emu10k1_readl(addr, priv) & 0x0000ffff), priv);
                    break;
            }
            return;

        case 0x1c: /* AC97DATA */
            /* Codec functions discard the MSB and LSB of AC97ADDRESS. */
            if (dev->codec) {
                ac97_codec_writew(dev->codec, dev->io_regs[0x1e], val);

                /* Update volumes. */
                ac97_codec_getattn(dev->codec, 0x02, &dev->master_vol_l, &dev->master_vol_r);
                ac97_codec_getattn(dev->codec, 0x18, &dev->pcm_vol_l, &dev->pcm_vol_r);
                ac97_codec_getattn(dev->codec, 0x12, &dev->cd_vol_l, &dev->cd_vol_r);
            }
            break;

        default: /* 8-bit registers or unaligned operation */
writew_fallback:
            emu10k1_log_push();
            emu10k1_writeb(addr, val, priv);
            emu10k1_writeb(addr + 1, val >> 8, priv);
            emu10k1_log_pop();
            break;
    }
}

static void
emu10k1_writel(uint16_t addr, uint32_t val, void *priv)
{
    emu10k1_t *dev = (emu10k1_t *) priv;
    addr &= 0x1f;
#ifdef ENABLE_EMU10K1_LOG
    if ((emu10k1_do_log == 1) && (addr != 0x00) && (addr != 0x04))
        emu10k1_log("EMU10K1: write(%02X, %08X)\n", addr, val);
#endif
    int reg;
    int i;

    switch (addr) {
        case 0x04:
            reg = *((uint16_t *) &dev->io_regs[0x02]);
            emu10k1_log("EMU10K1: write_i(%d, %03X, %08X)\n", dev->emu8k.cur_voice, reg, val);
            switch (reg) {
                case 0x09: /* CCR */
                    dev->emu8k.voice[dev->emu8k.cur_voice].ccr = (val & 0xfe3f0000) | (dev->emu8k.voice[dev->emu8k.cur_voice].ccr & ~0xfe3f0000);
                    return;

                case 0x0b:      /* FXRT */
                    val >>= 16; /* upper bits are used, lower bits are shared with CLP */
                    dev->emu8k.voice[dev->emu8k.cur_voice].fxrt = val;
                    for (i = 0; i < 4; i++)
                        dev->emu8k.voice[dev->emu8k.cur_voice].fx_send_bus[i] = (val >> (i << 2)) & 0xf;
                    return;

                case 0x0c ... 0x0d: /* MAPA ... MAPB */
                    dev->emu8k.voice[dev->emu8k.cur_voice].map[reg & 0x01] = val;
                    return;

                case 0x00 ... 0x08: /* 16-bit registers */
                case 0x10 ... 0x1f:
                    goto writel_fallback;

                case 0x20 ... 0x3f: /* CD */
                    ((uint32_t *) dev->emu8k.voice[dev->emu8k.cur_voice].cd)[reg & 0x1f] = val;
                    return;

                case 0x40 ... 0x41: /* PTB ... TCB */
                case 0x45 ... 0x47: /* MICBA ... FXBA */
                    val &= 0xfffff000;
                    break;

                case 0x42: /* ADCCR */
                    val &= (dev->type == EMU10K1) ? 0x0000001f : 0x0000003f;
                    break;

                case 0x44: /* TCBS */
                    val &= 0x00000007;
                    dev->dsp.etram_mask = (8192 << val) - 1;
                    break;

                case 0x48: /* A_HWM */
                    if (dev->type == EMU10K1)
                        return;
                    break;

                case 0x49 ... 0x4b: /* MICBS ... FXBS */
                    val &= 0x0000001f;
                    if (!dev->indirect_regs[reg] && val) {
                        dev->indirect_regs[reg + 0x1a] = 0; /* buffer position is reset when transitioning from 0 to non-0 */
                        if (reg == 0x49)
                            dev->micbuf_half_looped = 0;
                        else if (reg == 0x4a)
                            dev->adcbuf_half_looped = 0;
                        else
                            dev->fxbuf_half_looped = 0;
                    }
                    break;

                case 0x52: /* DBG / A_SPSC */
                    if (dev->type != EMU10K1)
                        return;

                    /* Reset DBAC if requested. */
                    if (val & 0x80000000)
                        dev->dsp.regs[0x5b] = 0;

                    val &= 0x03ffffff;
                    break;

                case 0x53: /* REG53 / A_DBG */
                    if (dev->type != EMU10K1) {
                        /* Reset DBAC if requested. */
                        if (val & 0x40000000)
                            dev->dsp.regs[0x5b] = 0;
                        val &= 0x2ffe03ff;
                    }
                    break;

                case 0x54 ... 0x56: /* SPCS0 ... SPCS2 */
                    val &= 0x3fffffff;
                    break;

                case 0x58 ... 0x59: /* CLIEL ... CLIEH */
                case 0x66 ... 0x67: /* HLIEL ... HLIEH */
                    /* Clear any pending interrupts that are being disabled. */
                    dev->indirect_regs[reg + 2] &= val;
                    emu10k1_update_irqs(dev);
                    break;

                case 0x5a ... 0x5b: /* CLIPL ... CLIPH */
                case 0x68 ... 0x69: /* HLIPL ... HLIPH */
                    dev->indirect_regs[reg] &= ~val;
                    emu10k1_update_irqs(dev);
                    return;

                case 0x5e: /* SPBYPASS */
                    val &= 0x00000f0f;
                    break;

                case 0x5f: /* AC97SLOT / A_PCB */
                    if (dev->type == EMU10K1)
                        val &= 0x00000033;
                    else
                        return;
                    break;

                case 0x6a ... 0x6f: /* A_SPRI ... A_TDOF */
                case 0x74 ... 0x75: /* A_FXWC1 ... A_FXWC2 */
                case 0x77 ... 0x7b: /* A_SRT3 ... A_TTDD */
                    if (dev->type == EMU10K1)
                        return;
                    break;

                case 0x76: /* A_SPDIF_SAMPLERATE */
                    if (dev->type == EMU10K1)
                        return;
                    val &= 0xf003eee1;
                    break;

                case 0x7c: /* A_FXRT2 */
                case 0x7e: /* A_FXRT1 */
                    if (dev->type == EMU10K1)
                        return;
                    val &= 0xbf3f3f3f; /* whatever "high bit is used for filtering" means */
                    for (i = 0; i < 4; i++)
                        dev->emu8k.voice[dev->emu8k.cur_voice].fx_send_bus[((reg & 2) << 1) | i] = (val >> (i << 3)) & 0x3f;
                    break;

                case 0x7d: /* A_SENDAMOUNTS */
                    if (dev->type == EMU10K1)
                        return;
                    dev->emu8k.voice[dev->emu8k.cur_voice].sendamounts = val;
                    break;

                case 0x100 ... 0x1ff: /* FXGPREG / A_TANKMEMCTLREG */
                    if (dev->type != EMU10K1)
                        val &= 0x0000001f;
                    break;

                case 0x2a0 ... 0x2ff: /* A_TANKMEMDATAREG */
                    if (dev->type == EMU10K1)
                        return;
                    fallthrough;

                case 0x200 ... 0x29f: /* TANKMEMDATAREG */
                    val &= 0x000fffff;
                    break;

                case 0x3a0 ... 0x3ff: /* A_TANKMEMADDRREG */
                    if (dev->type == EMU10K1)
                        return;
                    fallthrough;

                case 0x300 ... 0x39f: /* TANKMEMADDRREG */
                    val &= 0x00ffffff;
                    break;

                case 0x400 ... 0x5ff: /* MICROCODE / A_FXGPREG */
                    if (dev->type == EMU10K1)
                        val &= (reg & 1) ? 0x00ffffff : 0x000fffff;
                    break;

                case 0x600 ... 0x7ff: /* overlapped MICROCODE / A_MICROCODE */
                    if (dev->type == EMU10K1)
                        val &= (reg & 1) ? 0x00ffffff : 0x000fffff;
                    else
                        val &= 0x0fffffff;
                    break;

                case 0x800 ... 0x9ff: /* A_MICROCODE */
                    if (dev->type == EMU10K1)
                        return;
                    val &= 0x0fffffff;
                    break;

                case 0x70 ... 0x73: /* 8/16-bit registers */
                    goto writel_fallback;

                case 0x43:          /* FXWC */
                case 0x5c ... 0x5d: /* SOLEL ... SOLEH */
                    break;

                default:
                    return;
            }

            dev->indirect_regs[reg] = val;
            return;

        default: /* 8/16-bit registers or unaligned operation */
writel_fallback:
            emu10k1_log_push();
            emu10k1_writew(addr, val, priv);
            emu10k1_writew(addr + 2, val >> 16, priv);
            emu10k1_log_pop();
            break;
    }
}

static void
emu10k1_remap(emu10k1_t *dev)
{
    if (dev->io_base)
        io_removehandler(dev->io_base, 32, emu10k1_readb, emu10k1_readw, emu10k1_readl, emu10k1_writeb, emu10k1_writew, emu10k1_writel, dev);

    dev->io_base = (dev->pci_regs[0x04] & 0x01) ? ((dev->pci_regs[0x10] & 0xe0) | (dev->pci_regs[0x11] << 8)) : 0;
    emu10k1_log("EMU10K1: remap(%04X)\n", dev->io_base);

    if (dev->io_base)
        io_sethandler(dev->io_base, 32, emu10k1_readb, emu10k1_readw, emu10k1_readl, emu10k1_writeb, emu10k1_writew, emu10k1_writel, dev);
}

static uint8_t
emu10k1_pci_read(int func, int addr, void *priv)
{
    const emu10k1_t *dev = (emu10k1_t *) priv;
    uint8_t          ret;

    switch (func) {
        case 0:
            ret = dev->pci_regs[addr];
            break;

        case 1:
            ret = dev->pci_game_regs[addr];
            break;

        default:
            return 0xff;
    }

    emu10k1_log("EMU10K1: pci_read(%d, %02X) = %02X\n", func, addr, ret);
    return ret;
}

static void
emu10k1_pci_write(int func, int addr, uint8_t val, void *priv)
{
    emu10k1_t *dev = (emu10k1_t *) priv;

    emu10k1_log("EMU10K1: pci_write(%d, %02X, %02X)\n", func, addr, val);

    switch (func) {
        case 0:
            switch (addr) {
                case 0x04:
                    dev->pci_regs[addr] = val & 0x05;
                    emu10k1_remap(dev);
                    return;

                case 0x05:
                    val &= 0x05;
                    break;

                case 0x10:
                    dev->pci_regs[addr] = (val & 0xe0) | (dev->pci_regs[addr] & ~0xe0);
                    emu10k1_remap(dev);
                    return;

                case 0x11:
                    dev->pci_regs[addr] = val;
                    emu10k1_remap(dev);
                    return;

                case 0x0c:
                case 0x0d:
                case 0x3c:
                    break;

                default:
                    return;
            }

            dev->pci_regs[addr] = val;
            break;

        case 1:
            switch (addr) {
                case 0x04:
                    dev->pci_game_regs[addr] = val & 0x05;
remap_gameport:
                    gameport_remap(dev->gameport, (dev->pci_game_regs[0x04] & 0x01) ? ((dev->pci_game_regs[0x10] & 0xf8) | (dev->pci_game_regs[0x11] << 8)) : 0);
                    return;

                case 0x05:
                    val &= 0x01;
                    break;

                case 0x10:
                    dev->pci_game_regs[addr] = (val & 0xf8) | (dev->pci_game_regs[addr] & ~0xf8);
                    goto remap_gameport;

                case 0x11:
                    dev->pci_game_regs[addr] = val;
                    goto remap_gameport;

                case 0x0c:
                case 0x0d:
                    break;

                default:
                    return;
            }

            dev->pci_game_regs[addr] = val;
            break;
    }
}

static void
emu10k1_poll(void *priv)
{
    emu10k1_t *dev = (emu10k1_t *) priv;

    /* Schedule next run. */
    timer_advance_u64(&dev->poll_timer, dev->timer_latch);

    /* Run EMU8000 update routine. */
    uint32_t prev_wc = dev->emu8k.wc;
    emu8k_update(&dev->emu8k);

    /* Process interrupts, starting with those coming from the DSP executor. */
    uint32_t ipr  = dev->temp_ipr;
    uint32_t inte = *((uint32_t *) &dev->io_regs[0x0c]);

    /* Process channel loop interrupts. */
    ipr |= dev->emu8k.lip << 6; /* IPR_CHANNELLOOP = 0x00000040 */

    /* Check sample timer. */
    dev->timer_count += dev->emu8k.wc - prev_wc;
    if (dev->timer_count >= dev->timer_interval) {
        ipr |= (inte & 0x00000004) << 7; /* INTE_INTERVALTIMERENB into IPR_INTERVALTIMER = 0x00000200 */
        dev->timer_count -= dev->timer_interval;
    }

    /* Process DSP interrupt. */
    if (UNLIKELY(dev->dsp.interrupt)) {
        dev->dsp.interrupt = 0;
        ipr |= (inte & 0x00001000) << 11; /* INTE_FXDSPENABLE into IPR_FXDSP = 0x00800000 */
    }

    /* Update interrupts if requested. */
    if (ipr || dev->mpu_irq) {
        dev->mpu_irq = 0;
        *((uint32_t *) &dev->io_regs[0x08]) |= ipr;
        emu10k1_update_irqs(dev);
    }
}

static void
emu10k1_filter_cd_audio(int channel, double *buffer, void *priv)
{
    const emu10k1_t *dev = (emu10k1_t *) priv;
    double           c;
    double           volume = channel ? dev->cd_vol_r : dev->cd_vol_l;

    c       = ((*buffer) * volume) / 65536.0;
    *buffer = c;
}

static void
emu10k1_get_buffer(int32_t *buffer, int len, void *priv)
{
    emu10k1_t *dev = (emu10k1_t *) priv;

    /* Run EMU8000 update routine. */
    emu8k_update(&dev->emu8k);

    /* Apply HCFG_AUDIOENABLE mute. */
    if (dev->io_regs[0x14] & 0x01) {
        /* Fill buffer. */
        for (int c = 0; c < len * 2; c += 2) {
            buffer[c] += (((dev->emu8k.buffer[c] * dev->pcm_vol_l) >> 15) * dev->master_vol_l) >> 15;
            buffer[c + 1] += (((dev->emu8k.buffer[c + 1] * dev->pcm_vol_r) >> 15) * dev->master_vol_r) >> 15;
        }
    }

    dev->emu8k.pos = 0;
}

static void
emu10k1_speed_changed(void *priv)
{
    emu10k1_t *dev = (emu10k1_t *) priv;

    dev->timer_latch = (uint64_t) ((double) TIMER_USEC * (1000000.0 / dev->emu8k.freq));
}

static void
emu10k1_reset(void *priv)
{
    emu10k1_t *dev = (emu10k1_t *) priv;
    int        i;

    /* Reset PCI configuration registers. */
    memset(dev->pci_regs, 0, sizeof(dev->pci_regs));
    dev->pci_regs[0x00]                  = 0x02;
    dev->pci_regs[0x01]                  = 0x11;
    *((uint16_t *) &dev->pci_regs[0x02]) = dev->type;
    dev->pci_regs[0x06]                  = 0x90;
    dev->pci_regs[0x07]                  = 0x02;
    dev->pci_regs[0x08]                  = emu10k1_models[dev->model].rev[0];
    dev->pci_regs[0x0a]                  = 0x01;
    dev->pci_regs[0x0b]                  = 0x04;
    dev->pci_regs[0x0d]                  = 0x20;
    dev->pci_regs[0x0e]                  = 0x80;
    dev->pci_regs[0x10]                  = 0x01;
    dev->pci_regs[0x2c]                  = 0x02;
    dev->pci_regs[0x2d]                  = 0x11;
    *((uint16_t *) &dev->pci_regs[0x2e]) = emu10k1_models[dev->model].id;
    dev->pci_regs[0x34]                  = 0xdc;
    dev->pci_regs[0x3d]                  = 0x01;
    dev->pci_regs[0x3e]                  = 0x02;
    dev->pci_regs[0x3f]                  = 0x14;
    dev->pci_regs[0xdc]                  = 0x01;
    dev->pci_regs[0xde]                  = 0x22;
    dev->pci_regs[0xdf]                  = 0x06;

    memset(dev->pci_game_regs, 0, sizeof(dev->pci_game_regs));
    dev->pci_game_regs[0x00] = 0x02;
    dev->pci_game_regs[0x01] = 0x11;
    dev->pci_game_regs[0x02] = (dev->type == EMU10K1) ? 0x02 : 0x03;
    dev->pci_game_regs[0x03] = 0x70;
    dev->pci_game_regs[0x06] = 0x90;
    dev->pci_game_regs[0x07] = 0x02;
    dev->pci_game_regs[0x08] = emu10k1_models[dev->model].rev[1];
    dev->pci_game_regs[0x0a] = 0x80;
    dev->pci_game_regs[0x0b] = 0x09;
    dev->pci_game_regs[0x0d] = 0x20;
    dev->pci_game_regs[0x0e] = 0x80;
    dev->pci_game_regs[0x10] = 0x01;
    dev->pci_game_regs[0x2c] = 0x02;
    dev->pci_game_regs[0x2d] = 0x11;
    dev->pci_game_regs[0x2e] = (dev->type == EMU10K1) ? 0x20 : 0x40;
    dev->pci_game_regs[0x34] = 0xdc;
    dev->pci_game_regs[0xdc] = 0x01;
    dev->pci_game_regs[0xde] = 0x22;
    dev->pci_game_regs[0xdf] = 0x06;

    /* Reset I/O space registers. */
    memset(dev->io_regs, 0, sizeof(dev->io_regs));
    dev->io_regs[0x02]  = 0xff; /* PTR_ADDRESS maxed out */
    dev->io_regs[0x03]  = 0x07;
    dev->io_regs[0x14]  = 0x1e; /* HCFG_MUTEBUTTONENABLE | HCFG_LOCKTANKCACHE | HCFG_LOCKSOUNDCACHE | HCFG_AUTOMUTE */
    dev->timer_interval = 1024;
    dev->timer_count    = 0;

    /* Default state of voice-specific registers is unclear. */

    /* Reset indirect and DSP registers. */
    memset(dev->indirect_regs, 0, sizeof(dev->indirect_regs));
    dev->indirect_regs[0x50]  = 0xffffffff; /* CDCS */
    dev->indirect_regs[0x51]  = 0xffffffff; /* GPSCS */
    dev->indirect_regs[0x52]  = 0x00069400; /* DBG_STEP=0x0a | DBG_SINGLE_STEP | SATURATION_ADDR=0x06 */
    dev->indirect_regs[0x11c] = 0xfffe0000; /* default GPRs */
    dev->indirect_regs[0x11d] = 0xfffc0000;
    dev->indirect_regs[0x126] = 0xfffff000;
    dev->indirect_regs[0x127] = 0xfffff000;
    dev->indirect_regs[0x128] = 0x70000000;
    dev->indirect_regs[0x129] = 0x00000007;
    dev->indirect_regs[0x12a] = 0x0000f800;
    dev->indirect_regs[0x12b] = 0x0000e000;
    dev->indirect_regs[0x12c] = 0x00000020;
    dev->indirect_regs[0x12d] = 0x0000001b;
    dev->indirect_regs[0x12e] = 0x02001000;
    dev->indirect_regs[0x12f] = 0x04001000;
    dev->indirect_regs[0x130] = 0x00000800;
    dev->indirect_regs[0x131] = 0x00000019;
    dev->indirect_regs[0x133] = 0x7fffffff;
    dev->indirect_regs[0x134] = 0x7fffffff;
    dev->indirect_regs[0x13d] = 0x7fffffff;
    dev->indirect_regs[0x13e] = 0x7fffffff;
    dev->indirect_regs[0x143] = 0x7fffffff;
    dev->indirect_regs[0x144] = 0x7fffffff;
    dev->indirect_regs[0x148] = 0x7fffffff;
    dev->indirect_regs[0x149] = 0x7fffffff;
    dev->indirect_regs[0x14a] = 0x7fffffff;
    dev->indirect_regs[0x14b] = 0x7fffffff;
    dev->indirect_regs[0x152] = 0x7fffffff;
    dev->indirect_regs[0x153] = 0x7fffffff;
    dev->indirect_regs[0x18c] = 0x40000000;
    dev->indirect_regs[0x18d] = 0x40000000;
    dev->indirect_regs[0x18e] = 0x82a36037;
    dev->indirect_regs[0x18f] = 0x82a36037;
    dev->indirect_regs[0x190] = 0x3d67a012;
    dev->indirect_regs[0x191] = 0x3d67a012;
    dev->indirect_regs[0x192] = 0x7d5c9fc9;
    dev->indirect_regs[0x193] = 0x7d5c9fc9;
    dev->indirect_regs[0x194] = 0xc2985fee;
    dev->indirect_regs[0x195] = 0xc2985fee;
    dev->indirect_regs[0x196] = 0x08000000;
    dev->indirect_regs[0x197] = 0x08000000;
    dev->indirect_regs[0x198] = 0xf4a6bd88;
    dev->indirect_regs[0x199] = 0xf4a6bd88;
    dev->indirect_regs[0x19a] = 0x0448a161;
    dev->indirect_regs[0x19b] = 0x0448a161;
    dev->indirect_regs[0x19c] = 0x0b594278;
    dev->indirect_regs[0x19d] = 0x0b594278;
    dev->indirect_regs[0x19e] = 0xfbb75e9f;
    dev->indirect_regs[0x19f] = 0xfbb75e9f;
    if (dev->type == EMU10K1) {
        /* Default DSP program: ACC3 C_00000000, C_00000000, C_00000000, C_00000000 */
        for (i = 0x400; i < 0x800; i += 2) {
            dev->indirect_regs[i]     = 0x00010040;
            dev->indirect_regs[i | 1] = 0x00610040;
        }
    }
    dev->dsp.etram_mask = (8192 << 0) - 1;

    /* Reset I/O mappings. */
    emu10k1_remap(dev);
    emu10k1_remap_traps(dev);
    gameport_remap(dev->gameport, 0);

    /* Invalidate any existing TLB. */
    for (i = 0; i < dev->emu8k.nvoices; i++) {
        dev->emu8k.voice[i].tlb_pos = 0;
        memset(dev->emu8k.voice[i].map, 0, sizeof(dev->emu8k.voice[0].map));
    }
}

static void *
emu10k1_init(const device_t *info)
{
    emu10k1_t *dev = malloc(sizeof(emu10k1_t));
    memset(dev, 0, sizeof(emu10k1_t));

    /* Set the chip type and parameters. */
    int id = info->local;
    if (!(id & 0xffff)) /* config-specified ID */
        id |= device_get_config_int("model") & 0xffff;
    int i;
    for (i = 0; i < (sizeof(emu10k1_models) / sizeof(emu10k1_models[0])); i++) {
        if (emu10k1_models[i].id == id) {
            dev->type  = emu10k1_models[i].id >> 16;
            dev->model = i;
            break;
        }
    }
    if (i >= (sizeof(emu10k1_models) / sizeof(emu10k1_models[0]))) {
        fatal("EMU10K1: Unknown type 0x%05X selected\n", id);
        return NULL;
    }
    emu10k1_log("EMU10K1: init(%01X, %04X)\n", dev->type, emu10k1_models[dev->model].id & 0xffff);

    dev->pagemask = (dev->type == EMU10K1) ? 8191 : 4095;
    memcpy(&dev->dsp.regs[(dev->type == EMU10K1) ? 0x40 : 0xc0], dsp_constants, sizeof(dsp_constants));
    memcpy(&dev->dsp.regs[(dev->type == EMU10K1) ? 0x60 : 0xe0], dsp_constants, sizeof(dsp_constants)); /* undocumented shadow (unknown on non-EMU10K1) */
    dev->dsp.wake_dsp_thread = thread_create_event();
    dev->dsp.wake_main_thread = thread_create_event();
    dev->dsp.thread = thread_create(emu10k1_dsp_thread, dev);

    /* Initialize EMU8000 synth. */
    emu8k_init_standalone(&dev->emu8k, 64, FREQ_48000);
    dev->emu8k.emu10k1_fxbuses = (dev->type == EMU10K1) ? 16 : 64;
    dev->emu8k.emu10k1_fxsends = (dev->type == EMU10K1) ? 4 : 8;
    dev->emu8k.readl           = emu10k1_mem_readl;
    dev->emu8k.clie            = (uint64_t *) &dev->indirect_regs[0x58];
    dev->emu8k.clip            = (uint64_t *) &dev->indirect_regs[0x5a];
    dev->emu8k.hlie            = (uint64_t *) &dev->indirect_regs[0x66];
    dev->emu8k.hlip            = (uint64_t *) &dev->indirect_regs[0x68];
    dev->emu8k.sole            = (uint64_t *) &dev->indirect_regs[0x5c];

    if (emu10k1_models[dev->model].codec) {
        /* Initialize AC97 codec. */
        ac97_codec       = &dev->codec;
        ac97_codec_count = 1;
        ac97_codec_id    = 0;
        device_add(emu10k1_models[dev->model].codec);
    } else {
        /* No volume control in non-AC97 mode. */
        dev->master_vol_l = dev->master_vol_r = 32768;
        dev->pcm_vol_l = dev->pcm_vol_r = 32768;
        dev->cd_vol_l = dev->cd_vol_r = 32768;
    }

    /* Initialize playback timer. */
    timer_add(&dev->poll_timer, emu10k1_poll, dev, 0);
    emu10k1_speed_changed(dev);
    timer_advance_u64(&dev->poll_timer, dev->timer_latch);

    /* Initialize playback handler and CD audio filter. */
    sound_add_handler(emu10k1_get_buffer, dev);
    sound_set_cd_audio_filter(emu10k1_filter_cd_audio, dev);

    /* Initialize MPU-401. */
    mpu401_init(&dev->mpu[0], 0, 0, M_UART, device_get_config_int("receive_input"));
    mpu401_irq_attach(&dev->mpu[0], emu10k1_mpu_irq_update, emu10k1_mpu0_irq_pending, dev);
    if (dev->type != EMU10K1) {
        mpu401_init(&dev->mpu[1], 0, 0, M_UART, 0);
        mpu401_irq_attach(&dev->mpu[1], emu10k1_mpu_irq_update, emu10k1_mpu1_irq_pending, dev);
    }

    /* Initialize game port. */
    dev->gameport = gameport_add(&gameport_pnp_device);

    /* Initialize I/O traps. */
    for (i = 0; i < (sizeof(dev->io_traps) / sizeof(dev->io_traps[0])); i++) {
        dev->io_traps[i].trap = io_trap_add(emu10k1_io_trap, &dev->io_traps[i]);
        dev->io_traps[i].dev  = dev;
    }
    dev->io_traps[TRAP_DMA1].flag = 0xa0;
    dev->io_traps[TRAP_DMA2].flag = 0xe0;
    dev->io_traps[TRAP_PIC1].flag = 0x80;
    dev->io_traps[TRAP_PIC2].flag = 0xc0;
    dev->io_traps[TRAP_SB].flag   = 0x40;
    dev->io_traps[TRAP_OPL].flag  = 0x60;
    /* TRAP_MPU flag is 0x00 */

    /* Add PCI card. */
    pci_add_card(PCI_ADD_NORMAL, emu10k1_pci_read, emu10k1_pci_write, dev, &dev->pci_slot);

    /* Perform initial reset. */
    emu10k1_reset(dev);

#ifdef EMU10K1_SAMPLE_DUMP
    wav_header.fmt.sample_rate = dev->emu8k.freq;
    wav_header.fmt.channels    = dev->emu8k.emu10k1_fxbuses;
    wav_header.fmt.byte_rate   = (wav_header.fmt.sample_rate * wav_header.fmt.bits_sample * wav_header.fmt.channels) / 8;
    wav_header.fmt.block_align = (wav_header.fmt.bits_sample * wav_header.fmt.channels) / 8;

    for (i = 0; i < (sizeof(sample_dump_f) / sizeof(sample_dump_f[0])); i++) {
        sample_dump_f[i] = fopen(sample_dump_fn[i], "wb");
        if (!sample_dump_f[i])
            continue;

#    ifdef _WIN32
        /* Enable NTFS compression on dump files. This works despite the file being open. */
        HANDLE *hf = CreateFileA(sample_dump_fn[i], GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (hf) {
            SHORT method = COMPRESSION_FORMAT_DEFAULT;
            DWORD ret;
            DeviceIoControl(hf, FSCTL_SET_COMPRESSION, &method, sizeof(method), NULL, 0, &ret, NULL);
            CloseHandle(hf);
        }
#    endif

        /* Write initial WAVE header. */
        fwrite(&wav_header, sizeof(wav_header), 1, sample_dump_f[i]);
    }
#endif

    return dev;
}

static void
emu10k1_close(void *priv)
{
    emu10k1_t *dev = (emu10k1_t *) priv;

    emu10k1_log("EMU10K1: close()\n");

#ifdef EMU10K1_SAMPLE_DUMP
    for (int i = 0; i < (sizeof(sample_dump_f) / sizeof(sample_dump_f[0])); i++) {
        if (!sample_dump_f[i])
            continue;

        /* Retroactively fill data sizes in the WAVE header. */
        uint64_t pos         = ftello64(sample_dump_f[i]);
        wav_header.riff.size = MIN(pos - 8, (uint32_t) -1);
        wav_header.data.size = MIN(pos - sizeof(wav_header), (uint32_t) -1);
        fseek(sample_dump_f[i], 0, SEEK_SET);
        fwrite(&wav_header, sizeof(wav_header), 1, sample_dump_f[i]);

        fclose(sample_dump_f[i]);
        sample_dump_f[i] = NULL;
    }
#endif

    dev->dsp.stop = 1;
    thread_set_event(dev->dsp.wake_dsp_thread);
    thread_wait(dev->dsp.thread);
    thread_destroy_event(dev->dsp.wake_dsp_thread);
    thread_destroy_event(dev->dsp.wake_main_thread);

    free(dev);
}

static const device_config_t sb_live_config[] = {
    { .name           = "model",
     .description    = "Model",
     .type           = CONFIG_SELECTION,
     .default_string = "",
     .default_int    = SB_LIVE_CT4670,
     .file_filter    = "",
     .spinner        = { 0 },
     .selection      = {
          { .description = "CT4620 (Creative CT1297)",
                 .value       = SB_LIVE_CT4620 },
          { .description = "CT4670 (Creative CT1297)",
                 .value       = SB_LIVE_CT4670 },
          { .description = "CT4760 (SigmaTel STAC9721)",
                 .value       = SB_LIVE_CT4760 },
          { .description = "CT4780 (Crystal CS4297A)",
                 .value       = SB_LIVE_CT4780 },
          { .description = "SB0060 (SigmaTel STAC9708)",
                 .value       = SB_LIVE_SB0060 },
          { .description = "SB0220 (SigmaTel STAC9708)",
                 .value       = SB_LIVE_SB0220 },
          { .description = "" } } },
    { .name = "receive_input", .description = "Receive input (MPU-401)", .type = CONFIG_BINARY, .default_string = "", .default_int = 1 },
    { .name = "", .description = "", .type = CONFIG_END }
};

const device_t sb_live_device = {
    .name          = "Sound Blaster Live!",
    .internal_name = "sb_live",
    .flags         = DEVICE_PCI,
    .local         = EMU10K1 << 16,
    .init          = emu10k1_init,
    .close         = emu10k1_close,
    .reset         = emu10k1_reset,
    { .available = NULL },
    .speed_changed = emu10k1_speed_changed,
    .force_redraw  = NULL,
    .config        = sb_live_config
};
