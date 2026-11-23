/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Ensoniq Soundscape (ODIE-based, S-2000) emulation.
 *
 *          The board is an ES5706 "ODIE" host interface gate array, a
 *          68EC000 on-board processor (OBP) running firmware downloaded by
 *          the host driver, an ES5506 "OTTO" wavetable synthesizer with a
 *          2M x 8 compressed sample ROM and 64K words of DRAM shared by the
 *          OBP and OTTO, and an AD1848 codec for wave audio.
 *
 *          Host ports (BASE = 320/330/340/350h):
 *            BASE+0/1  MIDI emulation port (MC6850 or MPU-401 style)
 *            BASE+2/3  HOST interface port (OBP command channel)
 *            BASE+4/5  ODIE indirect register address/data
 *          The AD1848 sits at its own wave port (534/608/E84/F44h), with
 *          its IRQ and DMA routed by ODIE's CD-ROM interface register.
 *
 *          OBP memory map (16 MB, decoded by ODIE):
 *            000000-3FFFFF  memory device 0 (DRAM)
 *            400000-7FFFFF  selectable 2M-word OTTO memory bank
 *            800000-9FFFFF  OTTO registers (odd bytes)
 *            A00000-BFFFFF  ODIE registers
 *            C00000-FFFFFF  selectable memory device
 */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/gameport.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/midi.h>
#include <86box/pic.h>
#include <86box/rom.h>
#include <86box/sound.h>
#include <86box/timer.h>
#include <86box/snd_ad1848.h>
#include <86box/snd_es5506.h>
#include <86box/plat_unused.h>
#include "musashi/m68k.h"

/* from cpu.h, which defines macros that clash with Musashi */
extern void nmi_raise(void);
extern int  isa_cycles;
#ifdef USE_DYNAREC
extern void update_tsc(void);
#endif

#define SSCAPE_ROM_PATH "roms/sound/ensoniq/soundscape/ensoniq.2m"

#define OBP_CLOCK  8000000.0 /* 32 MHz / 4 */
#define OTTO_CLOCK 16000000   /* 32 MHz / 2 */

#define DRAM_WORDS 0x10000    /* 4x 64K x 4 DRAM = 64K words */
#define ROM_BYTES  0x200000   /* 2M x 8, on the high byte lane */

#define RING_SIZE 8192

#ifdef ENABLE_SSCAPE_LOG
int sscape_do_log = ENABLE_SSCAPE_LOG;

static void
sscape_log(const char *fmt, ...)
{
    va_list ap;

    if (sscape_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define sscape_log(fmt, ...)
#endif

/* ODIE IRQ select 0-3 as wired on the S-2000; SSINIT and VSNDSCP.386
   encode IRQ 5 as select 1 and probe only 5, 7, 9 and 10. */
static const int sscape_irqs[4] = { 9, 5, 7, 10 };

/* Host indirect registers. */
enum {
    GA_INTSTAT = 0,
    GA_INTENA  = 1,
    GA_DMAA    = 2,
    GA_DMAB    = 3,
    GA_INTCFG  = 4,
    GA_DMACFG  = 5,
    GA_CDCFG   = 6,
    GA_SMCFGA  = 7,
    GA_SMCFGB  = 8,
    GA_HMCTL   = 9,
    GA_FMADDR  = 10,
    GA_FMDATA  = 11,
    GA_FMSTAT  = 12
};

/* Latched interrupt sources. */
enum {
    LATCH_TIMER   = 0x01,
    LATCH_DMAA    = 0x02,
    LATCH_DMAB    = 0x04,
    LATCH_MIDICLK = 0x08
};

/* A host <-> OBP byte mailbox (MIDI emulation port or HOST port). */
typedef struct sscape_port_t {
    uint8_t to_obp;
    uint8_t to_host;
    uint8_t obp_rx;   /* host wrote, OBP has not read */
    uint8_t host_rx;  /* OBP wrote, host has not read */
    uint8_t is_cmd;   /* last host byte went to the command register */
    uint8_t host_ctrl;
    uint8_t obp_ctrl;
} sscape_port_t;

typedef struct sscape_dma_t {
    uint8_t  ctrl;
    uint32_t addr;     /* 23-bit word address in OTTO memory space */
    uint8_t  host_reg; /* host GA_DMAx: channel select and width */
    uint8_t  active;
    uint8_t  done;
    uint8_t  stereo_phase;
    uint8_t  byte_phase;
    uint16_t partial;
} sscape_dma_t;

typedef struct sscape_t {
    uint16_t base;
    uint16_t wave_base;

    /* host side */
    uint8_t       index;
    uint8_t       regs[16];
    uint8_t       his_latch;    /* HIS bits 4-6 */
    uint16_t      irq_asserted; /* host IRQ lines we drive */
    uint8_t       sb_host_irq;
    uint8_t       nmi_pending;
    sscape_port_t port[2];

    /* SoundBlaster emulation */
    uint8_t sb_ctrl;
    uint8_t sb_bit4;
    uint8_t sb_to_obp;
    uint8_t sb_to_host;
    uint8_t sb_obp_rx;
    uint8_t sb_host_rx;
    uint8_t sb_reset;
    uint8_t sb_reset_flag;
    uint8_t sb_reset_irq;
    uint8_t sb_mapped;
    uint8_t fm_mapped;

    /* YM3812 emulation latches */
    uint8_t ym_addr;
    uint8_t ym_data;
    uint8_t ym_status;

    /* OBP side */
    uint8_t      int_ctrl;
    uint8_t      latch;
    sscape_dma_t dma[2];
    uint16_t     midi_clock;
    uint8_t      pwm[4];
    uint8_t      adctl;
    uint8_t      misc;
    uint8_t      test;
    uint8_t      latch3;

    /* MIDI UART */
    uint8_t uart_ctrl;
    uint8_t uart_reset;
    double  uart_tx_busy;
    uint8_t uart_fifo[256];
    uint8_t uart_fifo_r;
    uint8_t uart_fifo_w;
    int     uart_fifo_n;
    int     midi_in;

    /* memory */
    uint16_t *dram;
    uint8_t  *rom;

    /* OTTO */
    es5506_t otto;
    int      otto_irq;
    int      otto_rate;

    /* OBP CPU */
    int    obp_running;
    int    obp_needs_reset;
    double   obp_cycles;
    uint64_t obp_tsc; /* emulated time the OBP has been run up to */
    int    obp_ipl;

    /* scheduling */
    pc_timer_t tick_timer;
    uint64_t   tick_period;
    double     tick_us;
    double     timer_us;
    double     midiclk_us;

    /* OTTO output ring at the OTTO rate */
    int32_t ring[RING_SIZE][2];
    int     ring_r;
    int     ring_w;
    int     ring_n;
    double  ring_pos;

    ad1848_t ad1848;
    void    *gameport;
    int      capture_rate;
    int      capture_freq;
    int      capture_phase;
    int16_t  capture_prev[2];
} sscape_t;

static sscape_t *sscape_cur = NULL;

static void sscape_update_host_irqs(sscape_t *dev);
static void sscape_update_obp_irq(sscape_t *dev);

/* ------------------------------------------------------------------ */
/* Sound memory                                                        */
/* ------------------------------------------------------------------ */

/* Word address width of one memory device, from HMCA bits 4-6. */
static inline int
sscape_dev_shift(const sscape_t *dev)
{
    return 23 - ((dev->regs[GA_SMCFGA] >> 4) & 7);
}

static uint16_t
sscape_dev_read(sscape_t *dev, int device, uint32_t off)
{
    switch (device) {
        case 0:
            return dev->dram[off & (DRAM_WORDS - 1)];
        case 1:
            /* 8-bit ROM on D8-D15 */
            return dev->rom[off & (ROM_BYTES - 1)] << 8;
        default:
            return 0xffff;
    }
}

static void
sscape_dev_write(sscape_t *dev, int device, uint32_t off, uint16_t val, uint16_t mask)
{
    switch (device) {
        case 0:
            off &= (DRAM_WORDS - 1);
            dev->dram[off] = (dev->dram[off] & ~mask) | (val & mask);
            break;
        case 3:
            /* output latch on CAS3 */
            if (mask & 0xff00)
                dev->latch3 = val >> 8;
            else
                dev->latch3 = val & 0xff;
            break;
        default:
            break;
    }
}

/* OTTO memory space: 23-bit word address. */
static uint16_t
sscape_mem_read(sscape_t *dev, uint32_t addr)
{
    const int      shift  = sscape_dev_shift(dev);
    const uint32_t device = (addr & 0x7fffff) >> shift;

    if (device > 3)
        return 0xffff;
    return sscape_dev_read(dev, device, addr & ((1 << shift) - 1));
}

static void
sscape_mem_write(sscape_t *dev, uint32_t addr, uint16_t val, uint16_t mask)
{
    const int      shift  = sscape_dev_shift(dev);
    const uint32_t device = (addr & 0x7fffff) >> shift;

    if (device <= 3)
        sscape_dev_write(dev, device, addr & ((1 << shift) - 1), val, mask);
}

static uint16_t
sscape_otto_read_sample(void *priv, int bank, uint32_t addr)
{
    sscape_t *dev = (sscape_t *) priv;

    return sscape_mem_read(dev, ((uint32_t) bank << 21) | (addr & 0x1fffff));
}

static void
sscape_otto_irq(void *priv, int state)
{
    sscape_t *dev = (sscape_t *) priv;

    dev->otto_irq = state;
    sscape_update_obp_irq(dev);
}

static void
sscape_otto_rate(void *priv, int rate)
{
    sscape_t *dev = (sscape_t *) priv;

    if (rate <= 0)
        return;
    dev->otto_rate   = rate;
    dev->tick_us     = 1000000.0 / (double) rate;
    dev->tick_period = (uint64_t) (dev->tick_us * (double) TIMER_USEC);
}

/* ------------------------------------------------------------------ */
/* Mailbox ports                                                       */
/* ------------------------------------------------------------------ */

static inline int
port_is_mpu(const sscape_port_t *p)
{
    return (p->obp_ctrl & 0x18) == 0x10;
}

static inline int
port_host_reset(const sscape_port_t *p)
{
    return !port_is_mpu(p) && ((p->host_ctrl & 3) == 3);
}

static int
port_host_irq(const sscape_port_t *p)
{
    if (port_is_mpu(p))
        return p->host_rx;
    if (port_host_reset(p))
        return 0;
    return ((p->host_ctrl & 0x80) && p->host_rx) || (((p->host_ctrl >> 5) & 3) == 1 && !p->obp_rx);
}

static int
port_obp_irq(const sscape_port_t *p)
{
    return ((p->obp_ctrl & 0x80) && p->obp_rx) || ((p->obp_ctrl & 0x40) && !p->host_rx);
}

/* reg is the port offset: 0 = control/status, 1 = data in MC6850 mode;
   in MPU-401 mode the roles swap (0 = data, 1 = command/status). */
static uint8_t
port_host_read(sscape_t *dev, sscape_port_t *p, int reg)
{
    const int mpu  = port_is_mpu(p);
    const int data = mpu ? !reg : reg;
    uint8_t   ret;

    if (data) {
        ret        = p->to_host;
        p->host_rx = 0;
    } else if (mpu)
        ret = 0x3f | (p->obp_rx ? 0x40 : 0x00) | (p->host_rx ? 0x00 : 0x80);
    else {
        ret = (p->obp_ctrl & 0x08) ? 0x04 : 0x00;
        if (!port_host_reset(p)) {
            if (p->host_rx)
                ret |= 0x01;
            if (!p->obp_rx)
                ret |= 0x02;
        }
        if (port_host_irq(p))
            ret |= 0x80;
    }

    sscape_update_host_irqs(dev);
    sscape_update_obp_irq(dev);
    return ret;
}

static void
port_host_write(sscape_t *dev, sscape_port_t *p, int reg, uint8_t val)
{
    const int mpu  = port_is_mpu(p);
    const int data = mpu ? !reg : reg;

    if (data || mpu) {
        p->to_obp = val;
        p->obp_rx = 1;
        p->is_cmd = !data;
    } else {
        p->host_ctrl = val;
        if ((val & 3) == 3) {
            p->obp_rx  = 0;
            p->host_rx = 0;
        }
    }

    sscape_update_host_irqs(dev);
    sscape_update_obp_irq(dev);
}

static uint8_t
port_obp_read(sscape_t *dev, sscape_port_t *p, int data)
{
    uint8_t ret;

    if (data) {
        ret       = p->to_obp;
        p->obp_rx = 0;
        sscape_update_host_irqs(dev);
        sscape_update_obp_irq(dev);
        return ret;
    }

    ret = p->obp_ctrl & 0xd8;
    if (!port_host_reset(p)) {
        ret |= 0x20;
        if (p->obp_rx)
            ret |= 0x01;
        if (!p->host_rx)
            ret |= 0x02;
    }
    if (p->is_cmd)
        ret |= 0x04;
    return ret;
}

static void
port_obp_write(sscape_t *dev, sscape_port_t *p, int data, uint8_t val)
{
    if (data) {
        p->to_host = val;
        p->host_rx = 1;
    } else {
        p->obp_ctrl = val & 0xd8;
        if ((val & 0x18) == 0x18) {
            p->obp_rx  = 0;
            p->host_rx = 0;
            p->is_cmd  = 0;
        }
    }

    sscape_update_host_irqs(dev);
    sscape_update_obp_irq(dev);
}

/* ------------------------------------------------------------------ */
/* Interrupts                                                          */
/* ------------------------------------------------------------------ */

static void
sscape_set_irq_lines(sscape_t *dev, uint16_t want)
{
    const uint16_t changed = dev->irq_asserted ^ want;

    for (int i = 0; i < 16; i++) {
        if (!(changed & (1 << i)))
            continue;
        if (want & (1 << i))
            picint(1 << i);
        else
            picintc(1 << i);
    }
    dev->irq_asserted = want;
}

static uint8_t
sscape_his(const sscape_t *dev)
{
    uint8_t ret = dev->his_latch & 0x70;

    if (port_host_irq(&dev->port[0]))
        ret |= 0x01;
    if (port_host_irq(&dev->port[1]))
        ret |= 0x02;
    if (ret)
        ret |= 0x80;
    return ret;
}

/* HIC interrupt mode select (bits 4-5 for the host/MIDI lines, 6-7 for
   the CD-ROM interface line). */
#define HIC_MODE_LOW_DRIVEN 2 /* low level sensitive, non-sharable */

static void
sscape_update_host_irqs(sscape_t *dev)
{
    const uint8_t  his      = sscape_his(dev);
    const uint8_t  hic      = dev->regs[GA_INTCFG];
    const uint8_t  cd       = dev->regs[GA_CDCFG];
    const uint16_t midi_irq = 1 << sscape_irqs[hic & 3];
    const uint16_t host_irq = 1 << sscape_irqs[(hic >> 2) & 3];
    uint16_t       want     = 0;

    if (dev->regs[GA_INTENA] & 0x80) {
        if (his & 0x01)
            want |= midi_irq;
        if (his & 0x72)
            want |= host_irq;
    }

    /* The DSP signals the host IRQ even when the separate codec IRQ is disabled. */
    if (dev->sb_host_irq)
        want |= host_irq;

    /* In the non-sharable low level mode ODIE always drives its lines and
       holds them high while nothing is pending, which the edge-triggered
       ISA PIC sees as a request. SNDSCAPE.VXD finds the codec IRQ this way:
       it switches HIC bits 6-7 from 11b to 10b and looks for the IRR bit. */
    if (((hic >> 4) & 3) == HIC_MODE_LOW_DRIVEN)
        want = (midi_irq | host_irq) & ~want;
    if ((cd & 0x01) && (((hic >> 6) & 3) == HIC_MODE_LOW_DRIVEN) && !(dev->ad1848.status & 0x01))
        want |= 1 << sscape_irqs[(cd >> 1) & 3];

    sscape_set_irq_lines(dev, want);
}

typedef struct obp_source_t {
    int level;
    int vector;
} obp_source_t;

/* Highest priority first. */
static int
sscape_obp_sources(sscape_t *dev, obp_source_t *out)
{
    int n = 0;

    if (((dev->uart_ctrl & 0x80) && dev->uart_fifo_n) || ((dev->uart_ctrl & 0x40) && (dev->uart_tx_busy <= 0.0)))
        out[n++] = (obp_source_t) { 6, 0x47 };
    if ((dev->int_ctrl & 0x08) && dev->otto_irq)
        out[n++] = (obp_source_t) { 5, 0x46 };
    if (port_obp_irq(&dev->port[1]))
        out[n++] = (obp_source_t) { 4, 0x45 };
    if (port_obp_irq(&dev->port[0]))
        out[n++] = (obp_source_t) { 4, 0x44 };
    if (dev->latch & LATCH_TIMER)
        out[n++] = (obp_source_t) { 3, 0x43 };
    if (dev->latch & (LATCH_DMAA | LATCH_DMAB))
        out[n++] = (obp_source_t) { 2, 0x42 };
    if (dev->latch & LATCH_MIDICLK)
        out[n++] = (obp_source_t) { 1, 0x41 };
    if (((dev->sb_ctrl & 0x80) && (dev->sb_obp_rx || dev->sb_reset_irq)) || ((dev->sb_ctrl & 0x40) && !dev->sb_host_rx))
        out[n++] = (obp_source_t) { 1, 0x40 };

    return n;
}

static void
sscape_update_obp_irq(sscape_t *dev)
{
    obp_source_t src[8];
    const int    n   = sscape_obp_sources(dev, src);
    int          ipl = 0;

    for (int i = 0; i < n; i++)
        if (src[i].level > ipl)
            ipl = src[i].level;

    if (ipl != dev->obp_ipl) {
        dev->obp_ipl = ipl;
        if (dev == sscape_cur)
            m68k_set_irq(ipl);
    }
}


int
sscape_m68k_int_ack(int level)
{
    sscape_t    *dev = sscape_cur;
    obp_source_t src[8];
    int          n;

    if (!dev)
        return M68K_INT_ACK_SPURIOUS;

    n = sscape_obp_sources(dev, src);
    for (int i = 0; i < n; i++)
        if (src[i].level == level)
            return src[i].vector;

    return M68K_INT_ACK_SPURIOUS;
}

/* ------------------------------------------------------------------ */
/* DMA                                                                 */
/* ------------------------------------------------------------------ */

static inline int
sscape_dma_host_channel(const sscape_dma_t *d)
{
    const int ch = (d->host_reg >> 4) & 7;

    return ((ch == 2) || (ch == 4)) ? -1 : ch;
}

static void
sscape_dma_trigger(sscape_t *dev, int n)
{
    sscape_dma_t *d   = &dev->dma[n];
    const int     mode = d->ctrl & 3;

    d->done         = 0;
    d->stereo_phase = 0;
    d->byte_phase   = 0;
    d->active       = ((mode == 1) || (mode == 2)) && !(d->ctrl & 0x80);
    sscape_log("SSCAPE: DMA %c trigger ctrl=%02x addr=%06x host=%02x\n", 'A' + n, d->ctrl, d->addr, d->host_reg);
}

static void
sscape_dma_finish(sscape_t *dev, int n)
{
    sscape_dma_t *d = &dev->dma[n];

    d->active = 0;
    d->done   = 1;

    if (dev->int_ctrl & (0x20 << n))
        dev->latch |= (n ? LATCH_DMAB : LATCH_DMAA);
    if (dev->regs[GA_INTENA] & (0x10 << n))
        dev->his_latch |= (0x10 << n);

    sscape_log("SSCAPE: DMA %c done addr=%06x\n", 'A' + n, d->addr);
    sscape_update_obp_irq(dev);
    sscape_update_host_irqs(dev);
}

/* Move one sample and report whether DMA can continue this tick. */
static int
sscape_dma_step(sscape_t *dev, int n)
{
    sscape_dma_t *d      = &dev->dma[n];
    const int     ch     = sscape_dma_host_channel(d);
    const int     wide   = !(d->host_reg & 0x80);
    const int     size   = (d->ctrl >> 4) & 3;
    const int     pcm    = (d->ctrl & 3) == 2;
    const int     stereo = (d->ctrl & 0x08) && pcm;
    const int     out    = d->ctrl & 0x04;
    const int     bytes  = (size >= 2) ? 1 : 2;
    /* the right channel goes to the next 4K-word buffer */
    const uint32_t addr  = (stereo && d->stereo_phase) ? ((d->addr + 0x1000) & 0x7fffff) : d->addr;
    int           tc     = 0;
    int           r;

    if (ch < 0)
        return 0;

    /* PCM buffers finish at an ODIE half-buffer boundary. The ISA DMA
       controller can wrap its auto-init buffer independently of that. */
    if (!out) {
        uint16_t word;

        if (wide && (ch >= 4)) {
            r = dma_channel_read(ch);
            if (r == DMA_NODATA)
                return 0;
            tc   = !pcm && (r & DMA_OVER);
            word = (bytes == 1) ? ((r & 0xff) << 8) : (r & 0xffff);
        } else {
            while (d->byte_phase < bytes) {
                r = dma_channel_read(ch);
                if (r == DMA_NODATA)
                    return 0;
                if (!pcm && (r & DMA_OVER))
                    tc = 1;
                if (bytes == 1)
                    d->partial = (r & 0xff) << 8;
                else if (d->byte_phase == 0)
                    d->partial = r & 0xff;
                else
                    d->partial |= (r & 0xff) << 8;
                d->byte_phase++;
                if (tc)
                    break;
            }
            d->byte_phase = 0;
            word          = d->partial;
        }

        if (size == 1)
            word &= 0xfff0;
        if ((d->ctrl & 0x40) && (size != 3))
            word ^= 0x8000;

        sscape_mem_write(dev, addr, word, 0xffff);
    } else {
        uint16_t word = sscape_mem_read(dev, addr);

        if ((d->ctrl & 0x40) && (size != 3))
            word ^= 0x8000;

        if (wide && (ch >= 4)) {
            r = dma_channel_write(ch, (bytes == 1) ? (word >> 8) : word);
            if (r == DMA_NODATA)
                return 0;
            tc = !pcm && (r & DMA_OVER);
        } else {
            while (d->byte_phase < bytes) {
                const uint8_t b = (bytes == 1) ? (word >> 8) : ((d->byte_phase == 0) ? (word & 0xff) : (word >> 8));

                r = dma_channel_write(ch, b);
                if (r == DMA_NODATA)
                    return 0;
                if (!pcm && (r & DMA_OVER))
                    tc = 1;
                d->byte_phase++;
                if (tc)
                    break;
            }
            d->byte_phase = 0;
        }
    }

    if (stereo && !d->stereo_phase) {
        d->stereo_phase = 1;
        return 1;
    }
    d->stereo_phase = 0;

    if (pcm) {
        const uint32_t next = d->addr + 1;

        /* 4K-word circular buffer, halting at each 2K-word half */
        d->addr = (d->addr & 0x7ff000) | (next & 0xfff);
        if (!(next & 0x7ff))
            tc = 1;
    } else
        d->addr = (d->addr + 1) & 0x7fffff;

    if (tc) {
        sscape_dma_finish(dev, n);
        return 0;
    }

    /* HALT lets the sample (or stereo pair) in progress finish. */
    if (d->ctrl & 0x80)
        d->active = 0;
    return d->active;
}

/* In the SoundBlaster + OPL-2 modes (HMC bits 0-1 = 01 or 10) ODIE feeds
   DMA A from the YM3812 ports instead of the ISA bus. Each host write
   stores one word, with bit 8 set for an address write, which the OBP
   firmware decodes. PCM mode wraps in a 4K-word buffer and interrupts at
   each 2K-word boundary; the first write after the firmware arms the
   buffer one word below a boundary signals that FM is in use. */
static int
sscape_fm_capturing(const sscape_t *dev)
{
    const uint8_t hmc = dev->regs[GA_HMCTL];

    return (((hmc & 3) == 1) || ((hmc & 3) == 2)) && ((hmc & 0xc0) != 0x80);
}

/* Run the OBP up to the current emulated time. The sample tick calls this
   once per OTTO sample (32 us), and every host access to the card's ports
   calls it too, so that the firmware has run exactly as long as it would
   have on the board when the host looks at its mailboxes, SoundBlaster DSP
   or OPL status. Its 32 us slices alone were too coarse for the polling
   loops of SoundBlaster and AdLib drivers. */
static void
sscape_obp_sync(sscape_t *dev)
{
    const uint64_t now     = tsc;
    const uint64_t elapsed = now - dev->obp_tsc;

    dev->obp_tsc = now;
    if (!dev->obp_running || (dev != sscape_cur) || !elapsed)
        return;

    dev->obp_cycles += (double) elapsed * 4294967296.0 * (OBP_CLOCK / 1000000.0) / (double) TIMER_USEC;
    if (dev->obp_cycles >= 1.0)
        dev->obp_cycles -= m68k_execute((int) dev->obp_cycles);
}

/* The same, from a host port access. ODIE's ports take a standard 8-bit
   ISA cycle, as on the board; drivers bound their DSP and OPL status polls
   by a count of reads, which the OBP firmware needs that much real time to
   answer. A compiled block's cycles then reach the TSC only through
   update_tsc(). */
static void
sscape_obp_sync_io(sscape_t *dev)
{
    sub_cycles(isa_cycles * 8);
#ifdef USE_DYNAREC
    if (cpu_use_dynarec)
        update_tsc();
#endif
    sscape_obp_sync(dev);
}

static void
sscape_fm_capture(sscape_t *dev, uint16_t word)
{
    sscape_dma_t *d    = &dev->dma[0];
    const int     mode = d->ctrl & 3;

    if (!sscape_fm_capturing(dev) || !mode || (mode == 3) || (d->ctrl & 0x84))
        return;

    sscape_mem_write(dev, d->addr, word, 0xffff);

    if (mode == 2) {
        const uint32_t next = d->addr + 1;

        d->addr = (d->addr & 0x7ff000) | (next & 0xfff);
        if (!(next & 0x7ff))
            sscape_dma_finish(dev, 0);
    } else
        d->addr = (d->addr + 1) & 0x7fffff;
}

static void
sscape_dma_reset(sscape_t *dev)
{
    for (int n = 0; n < 2; n++) {
        dev->dma[n].ctrl         = 0x01; /* 16-bit normal in */
        dev->dma[n].addr         = 0;
        dev->dma[n].active       = 0;
        dev->dma[n].done         = 0;
        dev->dma[n].stereo_phase = 0;
        dev->dma[n].byte_phase   = 0;
    }
}

/* ------------------------------------------------------------------ */
/* ODIE registers seen by the OBP                                      */
/* ------------------------------------------------------------------ */

static uint8_t
sscape_dma_status(const sscape_t *dev, int n)
{
    const sscape_dma_t *d   = &dev->dma[n];
    uint8_t             ret = d->done ? 0x01 : 0x00;

    if (!n && ((dev->regs[GA_HMCTL] & 0xc0) == 0x80))
        ret |= 0x04;
    if (d->host_reg & 0x80)
        ret |= 0x08;
    if (d->active)
        ret |= 0x10;
    return ret;
}

static void
sscape_set_int_ctrl(sscape_t *dev, uint8_t val)
{
    dev->int_ctrl = val & 0xf8;
    if (!(val & 0x10))
        dev->latch &= ~LATCH_TIMER;
    if (!(val & 0x20))
        dev->latch &= ~LATCH_DMAA;
    if (!(val & 0x40))
        dev->latch &= ~LATCH_DMAB;
    if (!(val & 0x80))
        dev->latch &= ~LATCH_MIDICLK;
    sscape_update_obp_irq(dev);
}

static uint8_t
sscape_uart_ctrl_read(const sscape_t *dev)
{
    uint8_t ret = dev->uart_ctrl & 0xc0;

    if (!dev->uart_reset) {
        ret |= 0x20;
        if (dev->uart_fifo_n)
            ret |= 0x01;
        if (dev->uart_tx_busy <= 0.0)
            ret |= 0x02;
    }
    return ret;
}

static uint8_t
sscape_odie_read(sscape_t *dev, uint8_t reg)
{
    uint8_t ret = 0xff;

    switch (reg & 0x1f) {
        case 0x00:
        case 0x01:
            ret = port_obp_read(dev, &dev->port[0], reg & 1);
            break;
        case 0x02:
        case 0x03:
            ret = port_obp_read(dev, &dev->port[1], reg & 1);
            break;
        case 0x04:
            ret = sscape_uart_ctrl_read(dev);
            break;
        case 0x05:
            ret = 0;
            if (dev->uart_fifo_n) {
                ret = dev->uart_fifo[dev->uart_fifo_r++];
                dev->uart_fifo_n--;
                sscape_update_obp_irq(dev);
            }
            break;
        case 0x06:
            ret = dev->int_ctrl;
            break;
        case 0x07:
            ret = 0;
            if (((dev->uart_ctrl & 0x80) && dev->uart_fifo_n) || ((dev->uart_ctrl & 0x40) && (dev->uart_tx_busy <= 0.0)))
                ret |= 0x01;
            if (dev->otto_irq)
                ret |= 0x02;
            if (port_obp_irq(&dev->port[1]))
                ret |= 0x04;
            if (port_obp_irq(&dev->port[0]))
                ret |= 0x08;
            if (dev->latch & LATCH_TIMER)
                ret |= 0x10;
            if (dev->latch & LATCH_DMAA)
                ret |= 0x20;
            if (dev->latch & LATCH_DMAB)
                ret |= 0x40;
            if ((dev->latch & LATCH_MIDICLK) || ((dev->sb_ctrl & 0x80) && (dev->sb_obp_rx || dev->sb_reset_irq)) || ((dev->sb_ctrl & 0x40) && !dev->sb_host_rx))
                ret |= 0x80;
            break;
        case 0x08:
            ret = dev->dma[0].ctrl;
            break;
        case 0x09:
            ret = sscape_dma_status(dev, 0);
            break;
        case 0x0a:
            ret = dev->ym_addr;
            break;
        case 0x0b:
            ret = (dev->dma[0].addr >> 16) & 0x7f;
            break;
        case 0x0c:
            ret = (dev->dma[0].addr >> 8) & 0xff;
            break;
        case 0x0d:
            ret = dev->dma[0].addr & 0xff;
            break;
        case 0x0e:
            ret = dev->dma[1].ctrl;
            break;
        case 0x0f:
            ret = sscape_dma_status(dev, 1);
            break;
        case 0x10:
            ret = dev->ym_data;
            break;
        case 0x11:
            ret = (dev->dma[1].addr >> 16) & 0x7f;
            break;
        case 0x12:
            ret = (dev->dma[1].addr >> 8) & 0xff;
            break;
        case 0x13:
            ret = dev->dma[1].addr & 0xff;
            break;
        case 0x14:
            ret = (dev->midi_clock >> 8) & 0x0f;
            break;
        case 0x15:
            ret = dev->midi_clock & 0xff;
            break;
        case 0x16:
        case 0x17:
        case 0x18:
        case 0x19:
            ret = dev->pwm[reg - 0x16];
            break;
        case 0x1a:
            ret = dev->adctl;
            break;
        case 0x1b:
            ret = dev->misc;
            break;
        case 0x1c:
            ret = dev->regs[GA_SMCFGA] & 0x7f;
            break;
        case 0x1d:
            ret = dev->test;
            break;
        case 0x1e:
            ret = dev->sb_ctrl & 0xc8;
            if (dev->sb_obp_rx)
                ret |= 0x01;
            if (!dev->sb_host_rx)
                ret |= 0x02;
            if (dev->sb_reset_flag)
                ret |= 0x04;
            if (dev->sb_reset)
                ret |= 0x20;
            break;
        case 0x1f:
            ret            = dev->sb_to_obp;
            dev->sb_obp_rx = 0;
            sscape_update_obp_irq(dev);
            break;
        default:
            break;
    }

    return ret;
}

static void
sscape_odie_write(sscape_t *dev, uint8_t reg, uint8_t val)
{
    switch (reg & 0x1f) {
        case 0x00:
        case 0x01:
            port_obp_write(dev, &dev->port[0], reg & 1, val);
            break;
        case 0x02:
        case 0x03:
            port_obp_write(dev, &dev->port[1], reg & 1, val);
            break;
        case 0x04:
            dev->uart_ctrl  = val & 0xc0;
            dev->uart_reset = ((val & 0x18) == 0x18);
            if (dev->uart_reset) {
                dev->uart_fifo_n = 0;
                dev->uart_fifo_r = dev->uart_fifo_w = 0;
            }
            sscape_update_obp_irq(dev);
            break;
        case 0x05:
            if (!dev->uart_reset) {
                midi_raw_out_byte(val);
                dev->uart_tx_busy = 320.0; /* 10 bits at 31250 baud */
                sscape_update_obp_irq(dev);
            }
            break;
        case 0x06:
            sscape_set_int_ctrl(dev, val);
            break;
        case 0x07:
            dev->ym_status = val;
            break;
        case 0x08:
        case 0x0e:
        {
            sscape_dma_t *d = &dev->dma[reg == 0x0e];

            d->ctrl = val;
            if (!(val & 3)) {
                d->active = 0;
                d->done   = 0;
            }
            break;
        }
        case 0x09:
        case 0x0f:
        {
            const int n = reg == 0x0f;

            sscape_dma_trigger(dev, n);
            /* The OBP can write HALT before its current CPU slice ends.
               Start the first transfer before processing that write. */
            if (dev->dma[n].active && (n || !sscape_fm_capturing(dev)))
                sscape_dma_step(dev, n);
            break;
        }
        case 0x0b:
            dev->dma[0].addr = (dev->dma[0].addr & 0x00ffff) | ((val & 0x7f) << 16);
            break;
        case 0x0c:
            dev->dma[0].addr = (dev->dma[0].addr & 0x7f00ff) | (val << 8);
            break;
        case 0x0d:
            dev->dma[0].addr = (dev->dma[0].addr & 0x7fff00) | val;
            break;
        case 0x11:
            dev->dma[1].addr = (dev->dma[1].addr & 0x00ffff) | ((val & 0x7f) << 16);
            break;
        case 0x12:
            dev->dma[1].addr = (dev->dma[1].addr & 0x7f00ff) | (val << 8);
            break;
        case 0x13:
            dev->dma[1].addr = (dev->dma[1].addr & 0x7fff00) | val;
            break;
        case 0x14:
            dev->midi_clock = (dev->midi_clock & 0x00ff) | ((val & 0x0f) << 8);
            break;
        case 0x15:
            dev->midi_clock = (dev->midi_clock & 0x0f00) | val;
            break;
        case 0x16:
        case 0x17:
        case 0x18:
        case 0x19:
            dev->pwm[reg - 0x16] = val;
            break;
        case 0x1a:
            dev->adctl = val;
            break;
        case 0x1b:
            dev->misc = val;
            break;
        case 0x1d:
            dev->test = val;
            break;
        case 0x1e:
            if ((val & 0x10) && !dev->sb_bit4) {
                dev->sb_host_irq = 1;
                sscape_update_host_irqs(dev);
            }
            dev->sb_bit4       = val & 0x10;
            dev->sb_ctrl       = val & 0xc8;
            dev->sb_reset_flag = 0;
            if (!(val & 0x80))
                dev->sb_reset_irq = 0;
            sscape_update_obp_irq(dev);
            break;
        case 0x1f:
            dev->sb_to_host = val;
            dev->sb_host_rx = 1;
            sscape_update_obp_irq(dev);
            break;
        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/* OBP bus                                                             */
/* ------------------------------------------------------------------ */

static uint8_t
obp_read8(sscape_t *dev, uint32_t a)
{
    a &= 0xffffff;

    switch (a >> 22) {
        case 0:
        {
            const uint16_t w = sscape_dev_read(dev, 0, (a >> 1) & 0x1fffff);
            return (a & 1) ? (w & 0xff) : (w >> 8);
        }
        case 1:
        {
            const uint16_t w = sscape_mem_read(dev, ((dev->misc & 3) << 21) | ((a >> 1) & 0x1fffff));
            return (a & 1) ? (w & 0xff) : (w >> 8);
        }
        case 2:
            if (a < 0xa00000)
                return (a & 1) ? es5506_read(&dev->otto, (a >> 1) & 0x3f) : 0xff;
            return sscape_odie_read(dev, a & 0x1f);
        default:
        {
            const uint16_t w = sscape_dev_read(dev, (dev->misc >> 2) & 3, (a >> 1) & 0x1fffff);
            return (a & 1) ? (w & 0xff) : (w >> 8);
        }
    }
}

static uint16_t
obp_read16(sscape_t *dev, uint32_t a)
{
    a &= 0xfffffe;

    switch (a >> 22) {
        case 0:
            return sscape_dev_read(dev, 0, (a >> 1) & 0x1fffff);
        case 1:
            return sscape_mem_read(dev, ((dev->misc & 3) << 21) | ((a >> 1) & 0x1fffff));
        case 2:
            return (obp_read8(dev, a) << 8) | obp_read8(dev, a + 1);
        default:
            return sscape_dev_read(dev, (dev->misc >> 2) & 3, (a >> 1) & 0x1fffff);
    }
}

static void
obp_write16_mask(sscape_t *dev, uint32_t a, uint16_t val, uint16_t mask)
{
    a &= 0xfffffe;

    switch (a >> 22) {
        case 0:
            sscape_dev_write(dev, 0, (a >> 1) & 0x1fffff, val, mask);
            break;
        case 1:
            sscape_mem_write(dev, ((dev->misc & 3) << 21) | ((a >> 1) & 0x1fffff), val, mask);
            break;
        case 2:
            if (a < 0xa00000) {
                if (mask & 0x00ff)
                    es5506_write(&dev->otto, (a >> 1) & 0x3f, val & 0xff);
            } else {
                if (mask & 0xff00)
                    sscape_odie_write(dev, a & 0x1f, val >> 8);
                if (mask & 0x00ff)
                    sscape_odie_write(dev, (a + 1) & 0x1f, val & 0xff);
            }
            break;
        default:
            sscape_dev_write(dev, (dev->misc >> 2) & 3, (a >> 1) & 0x1fffff, val, mask);
            break;
    }
}

unsigned int
m68k_read_memory_8(unsigned int address)
{
    return sscape_cur ? obp_read8(sscape_cur, address) : 0xff;
}

unsigned int
m68k_read_memory_16(unsigned int address)
{
    return sscape_cur ? obp_read16(sscape_cur, address) : 0xffff;
}

unsigned int
m68k_read_memory_32(unsigned int address)
{
    if (!sscape_cur)
        return 0xffffffff;
    return (obp_read16(sscape_cur, address) << 16) | obp_read16(sscape_cur, address + 2);
}

void
m68k_write_memory_8(unsigned int address, unsigned int value)
{
    if (sscape_cur)
        obp_write16_mask(sscape_cur, address, (value & 0xff) * 0x0101, (address & 1) ? 0x00ff : 0xff00);
}

void
m68k_write_memory_16(unsigned int address, unsigned int value)
{
    if (sscape_cur)
        obp_write16_mask(sscape_cur, address, value, 0xffff);
}

void
m68k_write_memory_32(unsigned int address, unsigned int value)
{
    if (sscape_cur) {
        obp_write16_mask(sscape_cur, address, value >> 16, 0xffff);
        obp_write16_mask(sscape_cur, address + 2, value & 0xffff, 0xffff);
    }
}

unsigned int
m68k_read_disassembler_8(unsigned int address)
{
    return m68k_read_memory_8(address);
}

unsigned int
m68k_read_disassembler_16(unsigned int address)
{
    return m68k_read_memory_16(address);
}

unsigned int
m68k_read_disassembler_32(unsigned int address)
{
    return m68k_read_memory_32(address);
}

/* ------------------------------------------------------------------ */
/* Subsystem control                                                   */
/* ------------------------------------------------------------------ */

static void
sscape_subsystem_reset(sscape_t *dev)
{
    sscape_log("SSCAPE: subsystem reset\n");

    for (int i = 0; i < 2; i++) {
        dev->port[i].obp_rx    = 0;
        dev->port[i].host_rx   = 0;
        dev->port[i].is_cmd    = 0;
        dev->port[i].obp_ctrl  = 0;
    }

    sscape_dma_reset(dev);

    dev->int_ctrl      = 0;
    dev->latch         = 0;
    dev->midi_clock    = 0;
    dev->adctl         = 0;
    dev->misc          = 0;
    dev->test          = 0;
    dev->uart_ctrl     = 0;
    dev->uart_reset    = 0;
    dev->uart_fifo_n   = 0;
    dev->sb_ctrl       = 0;
    dev->sb_bit4       = 0;
    dev->sb_obp_rx     = 0;
    dev->sb_host_rx    = 0;
    dev->sb_reset_flag = 0;
    dev->sb_reset_irq  = 0;
    dev->sb_host_irq   = 0;
    dev->nmi_pending   = 0;
    dev->ym_status     = 0;
    memset(dev->pwm, 0, sizeof(dev->pwm));

    es5506_reset(&dev->otto);
    dev->otto_irq = 0;

    dev->obp_running     = 0;
    dev->obp_needs_reset = 1;
    dev->obp_cycles      = 0.0;

    sscape_update_obp_irq(dev);
    sscape_update_host_irqs(dev);
}

static uint8_t sscape_sb_read(uint16_t addr, void *priv);
static void    sscape_sb_write(uint16_t addr, uint8_t val, void *priv);

static void
sscape_remap(sscape_t *dev, uint8_t previous)
{
    const uint8_t hmc = dev->regs[GA_HMCTL];
    const int     sb  = (hmc & 3) != 0;
    const int     fm  = sb && (hmc & 4);

    if (dev->sb_mapped)
        io_removehandler(0x0226, 0x0009, sscape_sb_read, NULL, NULL, sscape_sb_write, NULL, NULL, dev);
    if (dev->fm_mapped)
        io_removehandler(0x0388, 0x0002, sscape_sb_read, NULL, NULL, sscape_sb_write, NULL, NULL, dev);

    dev->sb_mapped = sb;
    dev->fm_mapped = fm;

    if (sb)
        io_sethandler(0x0226, 0x0009, sscape_sb_read, NULL, NULL, sscape_sb_write, NULL, NULL, dev);
    if (fm)
        io_sethandler(0x0388, 0x0002, sscape_sb_read, NULL, NULL, sscape_sb_write, NULL, NULL, dev);

    if ((hmc ^ previous) & 0x08)
        gameport_remap(dev->gameport, (hmc & 0x08) ? 0x201 : 0);
}

static void
sscape_write_hmc(sscape_t *dev, uint8_t val)
{
    const uint8_t mode     = (val >> 6) & 3;
    const uint8_t previous = dev->regs[GA_HMCTL];

    dev->regs[GA_HMCTL] = val;
    if ((val & 3) != 3)
        dev->nmi_pending = 0;
    if (!(val & 3)) {
        dev->sb_host_irq = 0;
        sscape_update_host_irqs(dev);
    }
    sscape_remap(dev, previous);

    if (mode == 0) {
        sscape_subsystem_reset(dev);
        return;
    }

    dev->obp_running = mode & 1;
    if (dev->obp_running && dev->obp_needs_reset) {
        dev->obp_needs_reset = 0;
        dev->obp_cycles      = 0.0;
        m68k_pulse_reset();
        sscape_log("SSCAPE: OBP start, PC=%06x\n", m68k_get_reg(NULL, M68K_REG_PC));
    }
}

static void
sscape_update_codec_routing(sscape_t *dev)
{
    const uint8_t cd  = dev->regs[GA_CDCFG];
    const int     dma = (cd >> 4) & 7;

    ad1848_setirq(&dev->ad1848, sscape_irqs[(cd >> 1) & 3]);
    if ((cd & 0x08) && (dma != 2) && (dma != 4))
        ad1848_setdma(&dev->ad1848, dma);
}

/* ------------------------------------------------------------------ */
/* Host I/O                                                            */
/* ------------------------------------------------------------------ */

static uint8_t
sscape_codec_read(uint16_t addr, void *priv)
{
    sscape_t     *dev = (sscape_t *) priv;
    const uint8_t ret = ad1848_read(addr, &dev->ad1848);

    if ((addr & 3) != 2)
        sscape_log("SSCAPE: codec in  %03X -> %02X\n", addr, ret);
    return ret;
}

static void
sscape_codec_write(uint16_t addr, uint8_t val, void *priv)
{
    sscape_t *dev       = (sscape_t *) priv;
    const int recording = dev->ad1848.rec_enable;

    sscape_log("SSCAPE: codec out %03X <- %02X\n", addr, val);
    ad1848_write(addr, val, &dev->ad1848);
    if (dev->ad1848.rec_enable && !recording) {
        dev->capture_rate = 0;
        sound_in_start_input();
    } else if (!dev->ad1848.rec_enable && recording)
        sound_in_stop_input();
}

static uint8_t
sscape_indirect_read(sscape_t *dev, uint8_t idx)
{
    switch (idx) {
        case GA_INTSTAT:
            return sscape_his(dev);
        case GA_INTENA:
            return dev->regs[GA_INTENA] & 0xf0;
        case GA_DMAA:
        case GA_DMAB:
        {
            const sscape_dma_t *d   = &dev->dma[idx - GA_DMAA];
            uint8_t             ret = (d->host_reg & 0xf0) | (d->done ? 0x01 : 0x00);

            if ((idx == GA_DMAA) && ((dev->regs[GA_HMCTL] & 0xc0) == 0x80))
                ret |= 0x04;
            if (d->active)
                ret |= 0x08;
            return ret;
        }
        case GA_DMACFG:
            return dev->regs[GA_DMACFG] & 0xf0;
        case GA_FMADDR:
            return dev->ym_addr;
        case GA_FMDATA:
            return dev->ym_data;
        default:
            if (idx <= GA_HMCTL)
                return dev->regs[idx];
            /* write-only and unused indices read back the index; the
               Soundscape drivers use this to detect ODIE */
            return idx;
    }
}

static void
sscape_indirect_write(sscape_t *dev, uint8_t idx, uint8_t val)
{
    sscape_log("SSCAPE: reg %X = %02X\n", idx, val);

    switch (idx) {
        case GA_INTENA:
            dev->regs[GA_INTENA] = val & 0xf0;
            dev->his_latch &= val & 0x70;
            /* any write re-arms the edge-triggered interrupt */
            sscape_set_irq_lines(dev, 0);
            sscape_update_host_irqs(dev);
            break;
        case GA_DMAA:
        case GA_DMAB:
        {
            sscape_dma_t *d   = &dev->dma[idx - GA_DMAA];
            const uint8_t old = dev->regs[idx];

            dev->regs[idx] = val;
            d->host_reg    = val & 0xf0;
            if ((val & 1) && !(old & 1))
                sscape_dma_trigger(dev, idx - GA_DMAA);
            break;
        }
        case GA_INTCFG:
        case GA_DMACFG:
        case GA_SMCFGA:
        case GA_SMCFGB:
            dev->regs[idx] = val;
            sscape_update_host_irqs(dev);
            break;
        case GA_CDCFG:
            dev->regs[idx] = val;
            sscape_update_codec_routing(dev);
            sscape_update_host_irqs(dev);
            break;
        case GA_HMCTL:
            sscape_write_hmc(dev, val);
            break;
        case GA_FMSTAT:
            dev->ym_status   = val;
            dev->nmi_pending = 0;
            break;
        default:
            break;
    }
}

static uint8_t
sscape_read(uint16_t addr, void *priv)
{
    sscape_t *dev = (sscape_t *) priv;
    uint8_t   ret = 0xff;

    sscape_obp_sync_io(dev);

    switch (addr - dev->base) {
        case 0:
        case 1:
            ret = port_host_read(dev, &dev->port[0], addr & 1);
            break;
        case 2:
        case 3:
            ret = port_host_read(dev, &dev->port[1], addr & 1);
            break;
        case 4:
            ret = dev->index & 0x0f;
            break;
        case 5:
            ret = sscape_indirect_read(dev, dev->index);
            break;
        default:
            break;
    }

    sscape_log("SSCAPE: in  %03X -> %02X\n", addr, ret);
    return ret;
}

static void
sscape_write(uint16_t addr, uint8_t val, void *priv)
{
    sscape_t *dev = (sscape_t *) priv;

    sscape_obp_sync_io(dev);

    sscape_log("SSCAPE: out %03X <- %02X\n", addr, val);

    switch (addr - dev->base) {
        case 0:
        case 1:
            port_host_write(dev, &dev->port[0], addr & 1, val);
            break;
        case 2:
        case 3:
            port_host_write(dev, &dev->port[1], addr & 1, val);
            break;
        case 4:
            dev->index = val & 0x0f;
            break;
        case 5:
            sscape_indirect_write(dev, dev->index, val);
            break;
        default:
            break;
    }
}

static uint8_t
sscape_sb_read(uint16_t addr, void *priv)
{
    sscape_t *dev = (sscape_t *) priv;
    uint8_t   ret = 0xff;

    sscape_obp_sync_io(dev);

    switch (addr) {
        case 0x228:
        case 0x388:
            /* In OPL-2 emulation mode the host NMI handler supplies the
               status; otherwise the OBP firmware writes it to the CAS3
               latch. */
            ret = ((dev->regs[GA_HMCTL] & 3) == 3) ? dev->ym_status : dev->latch3;
            break;
        case 0x22a:
            ret             = dev->sb_to_host;
            dev->sb_host_rx = 0;
            sscape_update_obp_irq(dev);
            break;
        case 0x22c:
            ret = dev->sb_obp_rx ? 0xff : 0x7f;
            break;
        case 0x22e:
            ret              = dev->sb_host_rx ? 0xff : 0x7f;
            dev->sb_host_irq = 0;
            sscape_update_host_irqs(dev);
            break;
        default:
            break;
    }

    return ret;
}

static void
sscape_sb_write(uint16_t addr, uint8_t val, void *priv)
{
    sscape_t *dev = (sscape_t *) priv;

    sscape_obp_sync_io(dev);

    switch (addr) {
        case 0x226:
            dev->sb_reset      = val & 1;
            dev->sb_reset_flag = 1;
            dev->sb_reset_irq  = 1;
            sscape_update_obp_irq(dev);
            break;
        case 0x228:
        case 0x388:
            dev->ym_addr = val;
            if ((dev->regs[GA_HMCTL] & 3) != 3)
                sscape_fm_capture(dev, 0x100 | val);
            break;
        case 0x229:
        case 0x389:
            dev->ym_data = val;
            if ((dev->regs[GA_HMCTL] & 3) != 3)
                sscape_fm_capture(dev, val);
            else if (!dev->nmi_pending) {
                dev->nmi_pending = 1;
                nmi_raise();
            }
            break;
        case 0x22c:
            dev->sb_to_obp = val;
            dev->sb_obp_rx = 1;
            sscape_update_obp_irq(dev);
            break;
        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/* MIDI input                                                          */
/* ------------------------------------------------------------------ */

static void
sscape_uart_queue(sscape_t *dev, const uint8_t *buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        if (dev->uart_fifo_n >= 256)
            break;
        dev->uart_fifo[dev->uart_fifo_w++] = buf[i];
        dev->uart_fifo_n++;
    }
    sscape_update_obp_irq(dev);
}

static void
sscape_input_msg(void *priv, uint8_t *msg, uint32_t len)
{
    sscape_t *dev = (sscape_t *) priv;

    if (!dev->uart_reset)
        sscape_uart_queue(dev, msg, len);
}

static int
sscape_input_sysex(void *priv, uint8_t *buffer, uint32_t len, int abort)
{
    sscape_t *dev  = (sscape_t *) priv;
    uint32_t  room = 256 - dev->uart_fifo_n;

    if (abort || dev->uart_reset)
        return 0;
    if (room > len)
        room = len;
    sscape_uart_queue(dev, buffer, room);
    return len - room;
}

static int
sscape_input_remain(void *priv)
{
    const sscape_t *dev = (sscape_t *) priv;

    return 256 - dev->uart_fifo_n;
}

/* ------------------------------------------------------------------ */
/* Board tick: one OTTO sample period                                  */
/* ------------------------------------------------------------------ */

static void
sscape_tick(void *priv)
{
    sscape_t *dev = (sscape_t *) priv;
    int32_t   out[2 * ES5506_MAX_CHANNELS];
    double    period;

    /* run the OBP */
    sscape_obp_sync(dev);

    /* host DMA */
    for (int n = sscape_fm_capturing(dev); n < 2; n++)
        for (int i = 0; (i < 16) && dev->dma[n].active; i++)
            if (!sscape_dma_step(dev, n))
                break;

    /* OTTO */
    es5506_generate(&dev->otto, out);
    if (dev->ring_n < RING_SIZE) {
        dev->ring[dev->ring_w][0] = out[0];
        dev->ring[dev->ring_w][1] = out[1];
        dev->ring_w               = (dev->ring_w + 1) & (RING_SIZE - 1);
        dev->ring_n++;
    }

    /* interval timer */
    period = 1000.0 * (double) (1 << ((dev->misc >> 4) & 3));
    dev->timer_us += dev->tick_us;
    if (dev->timer_us >= period) {
        dev->timer_us -= period;
        if (dev->int_ctrl & 0x10)
            dev->latch |= LATCH_TIMER;
    }

    /* MIDI clock: 1 MHz 12-bit counter */
    if ((dev->int_ctrl & 0x80) || (dev->regs[GA_INTENA] & 0x40)) {
        period = (double) ((dev->midi_clock & 0xfff) + 1);
        dev->midiclk_us += dev->tick_us;
        if (dev->midiclk_us >= period) {
            dev->midiclk_us = fmod(dev->midiclk_us, period);
            if (dev->int_ctrl & 0x80)
                dev->latch |= LATCH_MIDICLK;
            if (dev->regs[GA_INTENA] & 0x40) {
                dev->his_latch |= 0x40;
                sscape_update_host_irqs(dev);
            }
        }
    } else
        dev->midiclk_us = 0.0;

    if (dev->uart_tx_busy > 0.0)
        dev->uart_tx_busy -= dev->tick_us;

    sscape_update_obp_irq(dev);

    timer_advance_u64(&dev->tick_timer, dev->tick_period);
}

/* ------------------------------------------------------------------ */
/* Audio input and output                                              */
/* ------------------------------------------------------------------ */

static void
sscape_capture(int16_t *buffer, int len, void *priv)
{
    sscape_t *dev       = (sscape_t *) priv;
    ad1848_t *codec     = &dev->ad1848;
    const int host_rate = al_capture_get_rate();
    const int rate      = host_rate > 0 ? host_rate : SOUND_FREQ;
    const int freq      = codec->freq;
    double    gain[2];

    if (!codec->rec_enable || freq <= 0)
        return;

    for (int ch = 0; ch < 2; ch++) {
        const uint8_t ctrl = codec->regs[ch];
        const int boost = (ctrl & 0xe0) == 0xa0 ? 20 : 0;

        gain[ch] = pow(10.0, ((ctrl & 15) * 1.5 + boost) / 20.0);
    }

    if (rate != dev->capture_rate || freq != dev->capture_freq) {
        dev->capture_rate = rate;
        dev->capture_freq = freq;
        dev->capture_phase = 0;
        memcpy(dev->capture_prev, buffer, sizeof(dev->capture_prev));
    }

    for (int i = 0; i < len; i++) {
        const int16_t *sample = &buffer[2 * i];

        dev->capture_phase += freq;
        while (dev->capture_phase >= rate) {
            dev->capture_phase -= rate;
            const double fraction = 1.0 - (double) dev->capture_phase / freq;
            const int pos = codec->record_pos_write_mic;
            double input[2];

            for (int ch = 0; ch < 2; ch++)
                input[ch] = dev->capture_prev[ch] + (sample[ch] - dev->capture_prev[ch]) * fraction;

            for (int ch = 0; ch < 2; ch++) {
                double value = 0.0;

                switch (codec->regs[ch] >> 6) {
                    case 0: /* Line in */
                        value = input[ch];
                        break;
                    case 2: /* Microphone */
                        value = (input[0] + input[1]) / 2.0;
                        break;
                    case 3: /* Codec DAC loopback */
                        value = ch ? codec->out_r : codec->out_l;
                        break;
                    default: /* AUX1 has no capture source. */
                        break;
                }

                value *= gain[ch];
                codec->record_buffer[pos + ch] = (int16_t) fmax(INT16_MIN, fmin(INT16_MAX, value));
            }
            codec->record_pos_write_mic = (pos + 2) & 0xffff;
        }
        memcpy(dev->capture_prev, sample, sizeof(dev->capture_prev));
    }
}

static void
sscape_get_buffer(int32_t *buffer, uint16_t len, void *priv)
{
    sscape_t    *dev  = (sscape_t *) priv;
    const double step = (double) dev->otto_rate / (double) SOUND_FREQ;

    ad1848_update(&dev->ad1848);
    for (int c = 0; c < len * 2; c++)
        buffer[c] += dev->ad1848.buffer[c] / 2;
    dev->ad1848.pos = 0;

    /* keep the backlog bounded */
    while (dev->ring_n > (int) (len * step) + 512) {
        dev->ring_r = (dev->ring_r + 1) & (RING_SIZE - 1);
        dev->ring_n--;
    }

    for (uint16_t c = 0; c < len; c++) {
        const int    r0 = dev->ring_r;
        const int    r1 = (dev->ring_n > 1) ? ((r0 + 1) & (RING_SIZE - 1)) : r0;
        const double f  = dev->ring_pos;

        if (dev->ring_n > 0) {
            const double l = dev->ring[r0][0] * (1.0 - f) + dev->ring[r1][0] * f;
            const double r = dev->ring[r0][1] * (1.0 - f) + dev->ring[r1][1] * f;

            /* 20-bit OTTO output to 16-bit */
            buffer[c * 2]     += (int32_t) (l / 16.0);
            buffer[c * 2 + 1] += (int32_t) (r / 16.0);
        }

        dev->ring_pos += step;
        while (dev->ring_pos >= 1.0) {
            dev->ring_pos -= 1.0;
            if (dev->ring_n > 1) {
                dev->ring_r = (dev->ring_r + 1) & (RING_SIZE - 1);
                dev->ring_n--;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Device                                                              */
/* ------------------------------------------------------------------ */

static void *
sscape_init(UNUSED(const device_t *info))
{
    sscape_t *dev;
    FILE     *fp;

    if (sscape_cur) {
        /* the 68000 core is a single global instance */
        return NULL;
    }

    dev = calloc(1, sizeof(sscape_t));

    dev->base      = device_get_config_hex16("base");
    dev->wave_base = device_get_config_hex16("wave_base");
    dev->midi_in   = device_get_config_int("receive_input");

    dev->dram = calloc(DRAM_WORDS, sizeof(uint16_t));
    dev->rom  = malloc(ROM_BYTES);
    memset(dev->rom, 0xff, ROM_BYTES);
    fp = rom_fopen(SSCAPE_ROM_PATH, "rb");
    if (fp) {
        if (fread(dev->rom, 1, ROM_BYTES, fp) != ROM_BYTES)
            sscape_log("SSCAPE: short sample ROM\n");
        fclose(fp);
    }

    sscape_cur = dev;

    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);

    dev->otto.read_sample = sscape_otto_read_sample;
    dev->otto.irq_cb      = sscape_otto_irq;
    dev->otto.rate_cb     = sscape_otto_rate;
    dev->otto.priv        = dev;
    es5506_init(&dev->otto, OTTO_CLOCK, 1);

    ad1848_init(&dev->ad1848, AD1848_TYPE_DEFAULT);
    ad1848_setirq(&dev->ad1848, 7);
    ad1848_setdma(&dev->ad1848, 1);

    dev->gameport = gameport_add(&gameport_201_device);
    gameport_remap(dev->gameport, 0);

    io_sethandler(dev->base, 6, sscape_read, NULL, NULL, sscape_write, NULL, NULL, dev);
    io_sethandler(dev->wave_base, 4, sscape_codec_read, NULL, NULL, sscape_codec_write, NULL, NULL, dev);

    sscape_subsystem_reset(dev);

    timer_add(&dev->tick_timer, sscape_tick, dev, 0);
    timer_set_delay_u64(&dev->tick_timer, dev->tick_period);

    sound_add_handler(sscape_get_buffer, dev);
    sound_in_add_handler(sscape_capture, dev);

    if (dev->midi_in)
        midi_in_handler(1, sscape_input_msg, sscape_input_sysex, sscape_input_remain, dev);

    return dev;
}

static void
sscape_close(void *priv)
{
    sscape_t *dev = (sscape_t *) priv;

    if (sscape_cur == dev)
        sscape_cur = NULL;

    sound_in_remove_handler(sscape_capture, dev);
    if (dev->ad1848.rec_enable)
        sound_in_stop_input();
    free(dev->dram);
    free(dev->rom);
    free(dev);
}

static void
sscape_speed_changed(void *priv)
{
    sscape_t *dev = (sscape_t *) priv;

    ad1848_speed_changed(&dev->ad1848);
    sscape_otto_rate(dev, dev->otto_rate);
}

static int
sscape_available(void)
{
    return rom_present(SSCAPE_ROM_PATH);
}

static const device_config_t sscape_config[] = {
  // clang-format off
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x330,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0x320", .value = 0x320 },
            { .description = "0x330", .value = 0x330 },
            { .description = "0x340", .value = 0x340 },
            { .description = "0x350", .value = 0x350 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "wave_base",
        .description    = "Wave port",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x534,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0x534", .value = 0x534 },
            { .description = "0x608", .value = 0x608 },
            { .description = "0xE84", .value = 0xe84 },
            { .description = "0xF44", .value = 0xf44 },
            { .description = ""                      }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "receive_input",
        .description    = "Receive MIDI input",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t sscape_device = {
    .name          = "Ensoniq Soundscape S-2000",
    .internal_name = "sscape",
    .flags         = DEVICE_ISA16 | DEVICE_AUDIO_IN,
    .local         = 0,
    .init          = sscape_init,
    .close         = sscape_close,
    .reset         = NULL,
    .available     = sscape_available,
    .speed_changed = sscape_speed_changed,
    .force_redraw  = NULL,
    .config        = sscape_config
};
