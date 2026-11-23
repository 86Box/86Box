/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Aureal Vortex 2 (AU8830) PCI sound card, as on the Aureal
 *          SQ2500. Runs Aureal's own Windows drivers and A3D library:
 *          the driver computes the HRTF filters on the host and the
 *          chip model (snd_vortex2_core.c) plays them.
 *
 *          This file connects the chip model to 86Box: PCI identity and
 *          BARs, bus-master DMA, the interrupt line, the sample timer,
 *          the STAC9708 AC'97 codec and the sound output, and the
 *          chip's legacy MPU-401 UART and game port (on 86Box's own
 *          MPU-401 and game port, so MIDI goes to whatever MIDI device
 *          is configured, standing in for a board on the card's Wave
 *          Blaster header).
 *
 *          PCI configuration values were read from an SQ2500 (AU8830,
 *          revision 0xfa).
 *
 * Authors: Z2k Retro and contributors.
 *
 *          Copyright 2026 Z2k Retro.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/timer.h>
#include <86box/sound.h>
#include <86box/snd_ac97.h>
#include <86box/snd_mpu401.h>
#include <86box/gameport.h>
#include <86box/plat_unused.h>
#include <86box/plat.h>
#include <86box/path.h>
#include <86box/snd_opl.h>

#include "snd_vortex2_core.h"

#define V2_VENDOR    0x12eb
#define V2_DEVICE    0x0002
#define V2_REVISION  0xfa
#define V2_IO_SIZE   8

typedef struct vortex2_t {
    v2_chip_t    *chip;
    ac97_codec_t *codec;

    uint8_t  pci_regs[256];
    uint8_t  pci_slot;
    uint8_t  irq_state;
    uint32_t bar0;
    uint16_t bar1, bar2;
    uint16_t io1_mapped, io2_mapped;
    uint32_t index;

    mem_mapping_t mmio;

    mpu_t   *mpu;
    void    *gameport;
    uint16_t mpu_addr, game_addr;
    uint8_t  game_fired; /* unplugged port: one-shots triggered and never timing out */

    /* FM front end (ports 388h-38bh): the chip latches the index, keeps a
       packed copy of the OPL3 registers at 0x29900 and runs the two OPL
       timers; the sound itself is made by the driver's software from that
       copy (sbprobe fm and fmmap, 2026-11-18). */
    int        fm_on;
    uint8_t    fm_index, fm_bank, fm_new;
    uint8_t    fm_t[2], fm_ctl, fm_flags;
    pc_timer_t fm_timer[2];

    pc_timer_t block_timer;
    pc_timer_t irq_timer;
    uint64_t   irq_period;
    uint64_t   block_latch;
    uint64_t   block_tsc;
    uint32_t   frames;

    int   rear_mix;
#ifdef ENABLE_VORTEX2_LOG
    int   log_routes;
#endif
    int   cd_filter;
    /* CD audio on its way into the codec's ADC: 44.1 kHz frames written by
       86Box's CD-audio thread, read by the emulation thread. */
#define CD_RING 16384
    int16_t           cd_ring[CD_RING][2];
    volatile uint32_t cd_wr, cd_rd;
    int16_t           cd_pend;
    double            cd_phase;
    float             cd_prev[2], cd_cur[2];
    int               cd_primed; /* enough buffered to play without gaps */
#ifdef ENABLE_VORTEX2_LOG
    uint32_t          cd_log_n, cd_log_wr; /* diagnostic log: samples since the last line, cd_wr then */
    float             cd_log_peak;
#endif
    float             cd_level;  /* drive's analog output against codec full scale */
    /* The host's audio input, as the codec's microphone and line inputs:
       interleaved stereo at the capture rate, written by 86Box's sound
       thread, read by the emulation thread. Opened only while a record
       channel runs with the record select on Mic or Line In. */
#define IN_RING 32768
    int16_t           in_ring[IN_RING * 2];
    volatile uint32_t in_wr, in_rd;
    double            in_frac;
    int               in_rate, in_primed, in_started, in_idle;
#ifdef ENABLE_VORTEX2_LOG
    FILE *logf;
    FILE *tracef; /* register trace, same format as the real-card sessions */
    FILE *dmaf;   /* sample data the chip fetched: u32 addr, u32 len, bytes */
#endif
    float out[V2_BLOCK * 4];

    int16_t ring[V2_BLOCK * 8 * 2];
    int     ring_rd, ring_n;
    int16_t last[2];

    int32_t buffer[SOUNDBUFLEN * 2];
    int     pos;
} vortex2_t;

#ifdef ENABLE_VORTEX2_LOG
int vortex2_do_log = ENABLE_VORTEX2_LOG;

static void
vortex2_log(const char *fmt, ...)
{
    va_list ap;

    if (vortex2_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define vortex2_log(fmt, ...)
#endif

/* ------------------------------------------------------------------ */
/* Host callbacks for the chip model                                  */
/* ------------------------------------------------------------------ */

static void
host_mem_read(void *priv, uint32_t addr, uint8_t *buf, uint32_t len)
{
    const vortex2_t *dev = (vortex2_t *) priv;

    if (dev->pci_regs[0x04] & PCI_COMMAND_L_BM)
        dma_bm_read(addr, buf, len, 4);
    else
        memset(buf, 0, len);
#ifdef ENABLE_VORTEX2_LOG
    if (dev->dmaf) {
        fwrite(&addr, 4, 1, dev->dmaf);
        fwrite(&len, 4, 1, dev->dmaf);
        fwrite(buf, 1, len, dev->dmaf);
    }
#endif
}

static void
host_mem_write(void *priv, uint32_t addr, const uint8_t *buf, uint32_t len)
{
    const vortex2_t *dev = (vortex2_t *) priv;

    if (dev->pci_regs[0x04] & PCI_COMMAND_L_BM)
        dma_bm_write(addr, buf, len, 4);
}

static void
host_set_irq(void *priv, int level)
{
    vortex2_t *dev = (vortex2_t *) priv;

    if (level)
        pci_set_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
    else
        pci_clear_irq(dev->pci_slot, PCI_INTA, &dev->irq_state);
}

static void
host_timer_arm(void *priv, uint32_t samples)
{
    vortex2_t *dev = (vortex2_t *) priv;

    timer_disable(&dev->irq_timer);
    dev->irq_period = samples ? (uint64_t) ((double) TIMER_USEC * (double) samples * 1000000.0 / (double) V2_RATE) : 0;
    if (dev->irq_period)
        timer_set_delay_u64(&dev->irq_timer, dev->irq_period);
}

static uint32_t
host_clock(void *priv)
{
    const vortex2_t *dev = (vortex2_t *) priv;
    uint64_t         dt  = tsc - dev->block_tsc;
    uint64_t         sub = (dt * 12288ull) / (TIMER_USEC * 1000ull); /* 12.288 MHz */

    if (sub > V2_BLOCK * 256 - 1)
        sub = V2_BLOCK * 256 - 1;
    return dev->frames * 256u + (uint32_t) sub;
}

/* Register trace in the format of the real-card sessions' timeline.txt,
   so the same tools replay either. */
#ifdef ENABLE_VORTEX2_LOG
static double
trace_time(const vortex2_t *dev)
{
    return (double) host_clock((void *) dev) / (256.0 * 48000.0);
}
#endif

/* ------------------------------------------------------------------ */
/* Legacy MIDI and game port                                           */
/*                                                                     */
/* Read on a real SQ2500 from the host (legacyprobe, 2026-11-17): the  */
/* MPU-401 is a UART at 0x28800 (data) and 0x28804 (command/status),   */
/* acknowledging reset (0xff) and UART mode (0x3f) with 0xfe, with a   */
/* 16-byte transmit FIFO and 31,670 baud from the 49.152 MHz crystal / */
/* (16 x the divider in CTRL2 bits 15-8, 0x61 from the driver). The    */
/* game port's byte is at 0x28808 (buttons 7-4, one-shots 3-0), and in */
/* ADC mode (CTRL2 0x2880c bit 6) four 13-bit axis words sit at        */
/* 0x28810. The chip control word 0x2a00c enables the legacy I/O       */
/* decodes: bit 0 the MPU-401 at the address in bits 6-5, bit 3 the    */
/* game port at the address in bits 11-9. The Windows driver sets      */
/* fields 3 and 1 and talks to the MPU-401 only through the legacy     */
/* port. Field 3 is 0x330 (ALSA); the rest of each encoding was not    */
/* readable on the probe machine, so fields map to 0x300 + 0x10 x n    */
/* and 0x200 + n (field 1 = 0x201, the usual game port).               */
/* ------------------------------------------------------------------ */

#define V2_MIDI_DATA   0x28800
#define V2_MIDI_CMD    0x28804
#define V2_GAME_LEGACY 0x28808
#define V2_CTRL2       0x2880c
#define V2_GAME_AXIS   0x28810
#define V2_CHIP_CTRL   0x2a00c
#define V2_IRQ_MIDI    0x2000
#define V2_FM_LATCH 0x29854 /* low byte: last index; bit 8: OPL3 mode; 9-11 kept */
#define V2_FM_FILE  0x29900 /* 256 bytes: the packed register file */
#define V2_FM_TRASH 0xd4    /* where registers the chip does not keep land */
#define V2_IRQ_FMTIMER 0x40 /* set in IRQ_SOURCE when an unmasked timer ran out */
/* Control-word bit 2 enables the FM decode (sbprobe fmbit: 388h answers
   with bit 2 alone and stops with only bit 2 taken away; bits 1, 4, 19 and
   24 do nothing to it). Bit 1, set with it by AU30DOS and the driver, is
   probably the Sound Blaster ports, which this host does not forward. */
#define V2_CTRL_FM_EN 0x4

static uint8_t fm_read(uint16_t port, void *priv);
#ifdef ENABLE_VORTEX2_CORE_LOG
static double  trace_time(const vortex2_t *dev);
#endif

/* The MPU-401 ports go through the device so that the trace option can
   record them (as "port" lines, which v2replay skips): MIDI problems are
   otherwise invisible in a trace. */
static uint8_t
mpu_port_read(uint16_t port, void *priv)
{
    vortex2_t *dev = (vortex2_t *) priv;
    uint8_t    v   = mpu401_read(port, dev->mpu);

#ifdef ENABLE_VORTEX2_LOG
    if (dev->tracef)
        fprintf(dev->tracef, "%11.6f  R 0x%05x port 0x%08x\n", trace_time(dev), port, v);
#endif
    return v;
}

static void
mpu_port_write(uint16_t port, uint8_t val, void *priv)
{
    vortex2_t *dev = (vortex2_t *) priv;

#ifdef ENABLE_VORTEX2_LOG
    if (dev->tracef)
        fprintf(dev->tracef, "%11.6f  W 0x%05x port 0x%08x\n", trace_time(dev), port, val);
#endif
    mpu401_write(port, val, dev->mpu);
}
static void    fm_write(uint16_t port, uint8_t val, void *priv);

static void
legacy_remap(vortex2_t *dev)
{
    uint32_t ctl  = v2_read(dev->chip, V2_CHIP_CTRL);
    uint16_t midi = (ctl & 1) ? (0x300 + 0x10 * ((ctl >> 5) & 3)) : 0;
    uint16_t game = (ctl & 8) ? (0x200 + ((ctl >> 9) & 7)) : 0;

    if (dev->mpu && midi != dev->mpu_addr) {
        if (dev->mpu_addr)
            io_removehandler(dev->mpu_addr, 2, mpu_port_read, NULL, NULL, mpu_port_write, NULL, NULL, dev);
        if (midi)
            io_sethandler(midi, 2, mpu_port_read, NULL, NULL, mpu_port_write, NULL, NULL, dev);
        dev->mpu->addr = midi;
        dev->mpu_addr = midi;
        vortex2_log("Vortex2: MPU-401 at %03x\n", midi);
    }
    if (dev->gameport && game != dev->game_addr) {
        gameport_remap(dev->gameport, game);
        dev->game_addr = game;
        vortex2_log("Vortex2: game port at %03x\n", game);
    }
    int fm = !!(ctl & V2_CTRL_FM_EN);
    if (fm != dev->fm_on) {
        if (fm)
            io_sethandler(0x388, 4, fm_read, NULL, NULL, fm_write, NULL, NULL, dev);
        else
            io_removehandler(0x388, 4, fm_read, NULL, NULL, fm_write, NULL, NULL, dev);
        dev->fm_on = fm;
        vortex2_log("Vortex2: FM ports %s\n", fm ? "on" : "off");
    }
}

/* ---------------------------------------------------------------- FM */

/* Where an OPL3 register lands in the packed file (fmmap): operator groups
   of 18, channel groups of 9, bank 1 0x78 bytes on. */
static int
fm_offset(int bank, uint8_t reg)
{
    static const uint8_t op_base[8] = { 0, 0x02, 0x14, 0x26, 0x38, 0, 0, 0x66 };
    int                  b          = bank ? 0x78 : 0;
    int                  grp        = reg >> 5;

    if (reg == 0x01)
        return b;
    if (!bank && reg == 0x08)
        return 0x01;
    if (bank && reg == 0x04)
        return 0x79;
    if (!bank && reg == 0xbd)
        return 0x5c;
    if (op_base[grp]) {
        int lo = reg & 0x1f, slot;
        if (lo < 6)
            slot = lo;
        else if (lo >= 8 && lo < 0x0e)
            slot = lo - 2;
        else if (lo >= 0x10 && lo < 0x16)
            slot = lo - 4;
        else
            return V2_FM_TRASH;
        return b + op_base[grp] + slot;
    }
    if (reg >= 0xa0 && reg <= 0xa8)
        return b + 0x4a + (reg - 0xa0);
    if (reg >= 0xb0 && reg <= 0xb8)
        return b + 0x53 + (reg - 0xb0);
    if (reg >= 0xc0 && reg <= 0xc8)
        return b + 0x5d + (reg - 0xc0);
    return V2_FM_TRASH;
}

static void
fm_file_or(vortex2_t *dev, int off, uint8_t v, int replace)
{
    uint32_t a = V2_FM_FILE + (off & ~3), sh = (off & 3) * 8;
    uint32_t w = v2_read(dev->chip, a);
    w          = replace ? ((w & ~(0xffu << sh)) | ((uint32_t) v << sh)) : (w | ((uint32_t) v << sh));
    v2_write(dev->chip, a, w);
}

static void
fm_latch_update(vortex2_t *dev)
{
    uint32_t l = v2_read(dev->chip, V2_FM_LATCH);
    v2_write(dev->chip, V2_FM_LATCH, (l & 0xe00) | (dev->fm_new ? 0x100 : 0) | dev->fm_index);
}

static void
fm_timer_start(vortex2_t *dev, int t)
{
    /* timer 1 counts 80 us steps, timer 2 320 us, from the preset to 256 */
    double us = (256 - dev->fm_t[t]) * (t ? 320.0 : 80.0);
    timer_on_auto(&dev->fm_timer[t], us);
}

static void
fm_timer_cb(vortex2_t *dev, int t)
{
    if (!(dev->fm_ctl & (1 << t)))
        return;
    if (!(dev->fm_ctl & (t ? 0x20 : 0x40))) {
        dev->fm_flags |= t ? 0x20 : 0x40;
        v2_ext_irq(dev->chip, V2_IRQ_FMTIMER, 1);
    }
    fm_timer_start(dev, t); /* an OPL timer reloads and keeps running */
}
static void fm_timer1(void *p) { fm_timer_cb((vortex2_t *) p, 0); }
static void fm_timer2(void *p) { fm_timer_cb((vortex2_t *) p, 1); }

static uint8_t
fm_read(uint16_t port, void *priv)
{
    vortex2_t *dev = (vortex2_t *) priv;

    /* A legacy port access takes bus time, as on any AdLib-compatible
       part. Without it the reads cost nothing here, and detection loops
       that wait for timer 1's 80 us by reading the status port (Apogee's:
       about 135 reads) finished before it ran out: "Could not detect FM
       chip" in Duke Nukem 3D's setup. */
    cycles -= ISA_CYCLES(8);
    if (port != 0x388)
        return 0xff; /* as read on the card: only 388h gives the status */
    return dev->fm_flags ? (0x80 | dev->fm_flags) : 0x00;
}

static void
fm_write(uint16_t port, uint8_t val, void *priv)
{
    vortex2_t *dev = (vortex2_t *) priv;

    cycles -= ISA_CYCLES(8);
    if (!(port & 1)) {
        dev->fm_index = val;
        dev->fm_bank  = (port & 2) >> 1;
        fm_latch_update(dev);
        return;
    }
    if (!dev->fm_bank && dev->fm_index >= 0x02 && dev->fm_index <= 0x04) {
        if (dev->fm_index < 0x04) {
            dev->fm_t[dev->fm_index - 2] = val;
            return;
        }
        if (val & 0x80) { /* reset the flags; the rest of the byte is ignored */
            dev->fm_flags = 0;
            return;
        }
        dev->fm_ctl = val;
        for (int t = 0; t < 2; t++) {
            if (val & (1 << t))
                fm_timer_start(dev, t);
            else
                timer_disable(&dev->fm_timer[t]);
        }
        return;
    }
    if (dev->fm_bank && dev->fm_index == 0x05) {
        dev->fm_new = val & 1;
        fm_latch_update(dev);
    }
    /* Bank 1 is kept only in OPL3 mode (sbprobe fmflag: with register
       0x105 clear, a bank-1 write left the file as it was). */
    if (dev->fm_bank && !dev->fm_new && dev->fm_index != 0x05)
        return;
    int off = fm_offset(dev->fm_bank, dev->fm_index);
    if ((dev->fm_index & 0xf0) == 0xb0 && dev->fm_index <= 0xb8 && off != V2_FM_TRASH) {
        /* A key-off write (bit 5 clear) is stored with bits 7-6 set, which
           clear when the word is read; a key-on write is stored as written
           (sbprobe fmflag, 2026-11-18). The renderer polls the file and so
           sees each key-off once, even one written over a key-off. */
        fm_file_or(dev, off, (val & 0x20) ? val : (val | 0xc0), 1);
    } else
        fm_file_or(dev, off, val, 1);
    if (!dev->fm_bank && dev->fm_index == 0xbd && !(val & 0x20))
        for (int k = 0x59; k <= 0x5b; k++) /* rhythm off: key-off on channels 6-8 (fmmap: 0xbd = 0x5a) */
            fm_file_or(dev, k, 0xc0, 0);
}

static void
legacy_mpu_irq(void *priv, int set)
{
    vortex2_t *dev = (vortex2_t *) priv;

    v2_ext_irq(dev->chip, V2_IRQ_MIDI, set);
}

static int
legacy_mpu_irq_pending(void *priv)
{
    vortex2_t *dev = (vortex2_t *) priv;

    return !!(v2_read(dev->chip, 0x2a000) & V2_IRQ_MIDI);
}

/* Registers 0x28800-0x2881f; returns 1 if handled. */
static int
legacy_read(vortex2_t *dev, uint32_t a, uint32_t *v)
{
    switch (a) {
        case V2_MIDI_DATA:
            *v = dev->mpu ? mpu401_read(0, dev->mpu) : 0xff;
            return 1;
        case V2_MIDI_CMD:
            *v = dev->mpu ? mpu401_read(1, dev->mpu) : 0xff;
            return 1;
        case V2_GAME_LEGACY:
            /* With a joystick selected and the legacy decode on, through the
               game port. Otherwise the port as the real card reads with
               nothing plugged in (legacyprobe game): 0xf0 at rest (buttons
               up, one-shots idle), then 0xff from the first trigger write on,
               since with no stick the one-shots never time out. Returning
               0xf0 for good, as before, looked like a stick with four axes
               at zero, which Windows took for a joystick. With a joystick
               selected but the decode off, the stick is not reached here. */
            if (dev->game_addr && joystick_type[0] != JS_TYPE_NONE)
                *v = inb(dev->game_addr);
            else
                *v = dev->game_fired ? 0xff : 0xf0;
            return 1;
        default:
            if (a >= V2_GAME_AXIS && a < V2_GAME_AXIS + 16) {
                /* ADC mode: 13-bit axis values; 0x1fff (read as 0xffff on
                   the card) when nothing is plugged in */
                int i = (a - V2_GAME_AXIS) >> 2;
                if (joystick_type[0] != JS_TYPE_NONE && i < joystick_get_axis_count(joystick_type[0]))
                    *v = ((uint32_t) (joystick_state[0][0].axis[i] + 32768) >> 3) & 0x1fff;
                else
                    *v = 0xffff;
                return 1;
            }
            return 0;
    }
}

static int
legacy_write(vortex2_t *dev, uint32_t a, uint32_t v)
{
    switch (a) {
        case V2_MIDI_DATA:
            if (dev->mpu)
                mpu401_write(0, v & 0xff, dev->mpu);
            return 1;
        case V2_MIDI_CMD:
            if (dev->mpu)
                mpu401_write(1, v & 0xff, dev->mpu);
            return 1;
        case V2_GAME_LEGACY:
            if (dev->game_addr && joystick_type[0] != JS_TYPE_NONE)
                outb(dev->game_addr, v & 0xff);
            else
                dev->game_fired = 1;
            return 1;
        default:
            return 0;
    }
}

static uint32_t
tr_read(vortex2_t *dev, uint32_t a)
{
    uint32_t v;
    if (a >= V2_MIDI_DATA && a < V2_MIDI_DATA + 0x20 && legacy_read(dev, a, &v)) {
#ifdef ENABLE_VORTEX2_LOG
        if (dev->tracef)
            fprintf(dev->tracef, "%11.6f  R 0x%05x mmio 0x%08x\n", trace_time(dev), a, v);
#endif
        return v;
    }
    v = v2_read(dev->chip, a);
#ifdef ENABLE_VORTEX2_LOG
    if (dev->tracef)
        fprintf(dev->tracef, "%11.6f  R 0x%05x mmio 0x%08x\n", trace_time(dev), a, v);
#endif
    if (a >= V2_FM_FILE + 0x50 && a < V2_FM_FILE + 0xd4) {
        /* Key-on bytes (0xb0-0xb8 of each bank) carry bits 7-6 after a
           key-off write; reading the word clears them (sbprobe fmflag: the
           first read gave 0xc5, the next ones 0x05, and unread they stayed
           set for 20 ms). */
        uint32_t m = 0;
        for (int b = 0; b < 4; b++) {
            int off = (a - V2_FM_FILE) + b;
            if ((off >= 0x53 && off <= 0x5b) || (off >= 0xcb && off <= 0xd3))
                m |= 0xc0u << (8 * b);
        }
        if (m && (v & m))
            v2_write(dev->chip, a, v & ~m);
    }
    return v;
}

static void vortex2_chip_reset(vortex2_t *dev);

static void
tr_write(vortex2_t *dev, uint32_t a, uint32_t v)
{
#ifdef ENABLE_VORTEX2_LOG
    if (dev->tracef)
        fprintf(dev->tracef, "%11.6f  W 0x%05x mmio 0x%08x\n", trace_time(dev), a, v);
#endif
    if (a >= V2_MIDI_DATA && a < V2_MIDI_DATA + 0x20 && legacy_write(dev, a, v))
        return;
    if (a == V2_FM_LATCH) {
        /* bits 9-11 hold what is written; the index and the OPL3 bit stay */
        uint32_t l = v2_read(dev->chip, V2_FM_LATCH);
        v2_write(dev->chip, V2_FM_LATCH, (l & 0x1ff) | (v & 0xe00));
        return;
    }
    if (a == 0x29808) {
        /* reads back 0xff; its byte goes to the latch's low byte (sbprobe) */
        dev->fm_index = v & 0xff;
        fm_latch_update(dev);
        return;
    }
    if (a == V2_CHIP_CTRL && v == 0xffffffff) {
        /* All ones resets the chip (Plausible): AU30DOS writes it first thing and then,
           after a pause, the reset word with bit 21 cleared (0x103260); the
           driver writes it on shutting down, and on a start-up without
           AU30DOS writes it, reads the word back and clears bit 21. Kept
           as written, the driver read all ones back and turned every legacy
           decode on, the FM ports included, which then made AU30DOS see
           388h as taken after "Restart in MS-DOS mode" (error #7). */
        vortex2_chip_reset(dev);
        return;
    }
    v2_write(dev->chip, a, v);
    if (a == V2_CHIP_CTRL)
        legacy_remap(dev);
}

static uint16_t
host_codec_read(void *priv, uint8_t reg)
{
    vortex2_t *dev = (vortex2_t *) priv;

    return dev->codec ? ac97_codec_readw(dev->codec, reg) : 0xffff;
}

/* CD audio reaches the SQ2500 as analog into the codec's CD input, which
   the codec mixes straight into its output: so the codec's CD volume
   (0x12) and master volume (0x02) apply to it, and nothing on the
   Vortex itself does. */
static void
vortex2_filter_cd_audio(int channel, double *buffer, void *priv)
{
    const vortex2_t *dev = (const vortex2_t *) priv;
    int              cl = 0, cr = 0, ml = 0, mr = 0;

    if (!dev->codec) {
        *buffer = 0.0;
        return;
    }
    /* The codec also digitises the CD input when its record select picks
       CD; the Aureal driver plays CD audio that way (it mutes the analog CD
       path, register 0x12, and routes the ADC through the Vortex mixer). */
    {
        vortex2_t *d = (vortex2_t *) priv;
        double     v = *buffer;
        int16_t    sv = (int16_t) (v > 32767.0 ? 32767 : v < -32768.0 ? -32768 : v);
        if (channel == 0)
            d->cd_pend = sv;
        else if (d->cd_wr - d->cd_rd < CD_RING) {
            d->cd_ring[d->cd_wr % CD_RING][0] = d->cd_pend;
            d->cd_ring[d->cd_wr % CD_RING][1] = sv;
            d->cd_wr++;
        }
    }
    ac97_codec_getattn(dev->codec, 0x12, &cl, &cr);
    ac97_codec_getattn(dev->codec, 0x02, &ml, &mr);
    *buffer *= channel ? ((double) cr * mr / (32768.0 * 32768.0)) : ((double) cl * ml / (32768.0 * 32768.0));
}

/* The host's input, as sound.c hands it over. Kept within a quarter
   second of the ADC. */
static void
vortex2_in_put_buffer(int16_t *buffer, int len, void *priv)
{
    vortex2_t *dev      = (vortex2_t *) priv;
    const int  cap_rate = al_capture_get_rate();

    if (!dev->in_started)
        return;

    dev->in_rate = (cap_rate > 0) ? cap_rate : SOUND_FREQ;
    for (int c = 0; c < len; c++) {
        const uint32_t i = dev->in_wr % IN_RING;

        dev->in_ring[i * 2]     = buffer[c * 2];
        dev->in_ring[i * 2 + 1] = buffer[c * 2 + 1];
        dev->in_wr++;
    }

    uint32_t keep = (uint32_t) (dev->in_rate / 4);
    if (keep > IN_RING / 2)
        keep = IN_RING / 2;
    if (dev->in_wr - dev->in_rd > keep) {
        dev->in_rd   = dev->in_wr - keep / 2;
        dev->in_frac = 0.0;
    }
}

/* One frame of the host's input at 48 kHz (linear interpolation; silence
   until 40 ms is in hand, as the host's input arrives in 20 ms blocks). */
static void
vortex2_in_frame(vortex2_t *dev, float *l, float *r)
{
    const uint32_t avail = dev->in_wr - dev->in_rd;

    if (!dev->in_started) {
        *l = *r = 0.0f;
        return;
    }
    if (!dev->in_primed && dev->in_rate && avail >= (uint32_t) (dev->in_rate / 25))
        dev->in_primed = 1;
    if (!dev->in_primed || avail < 2) {
        dev->in_primed = 0;
        *l = *r = 0.0f;
        return;
    }

    const uint32_t i0 = dev->in_rd % IN_RING, i1 = (dev->in_rd + 1) % IN_RING;
    const float    f  = (float) dev->in_frac;

    *l = (dev->in_ring[i0 * 2] + (dev->in_ring[i1 * 2] - dev->in_ring[i0 * 2]) * f) / 32768.0f;
    *r = (dev->in_ring[i0 * 2 + 1] + (dev->in_ring[i1 * 2 + 1] - dev->in_ring[i0 * 2 + 1]) * f) / 32768.0f;

    dev->in_frac += (double) dev->in_rate / 48000.0;
    while (dev->in_frac >= 1.0 && dev->in_wr - dev->in_rd > 1) {
        dev->in_frac -= 1.0;
        dev->in_rd++;
    }
    if (dev->in_frac >= 1.0)
        dev->in_frac = 0.0;
}

/* Open the host's input while a record channel runs with the record select
   on Mic (0) or Line In (4), and close it half a second after that stops,
   so a driver that restarts its record channel does not reopen the device
   each time. */
static void
vortex2_in_update(vortex2_t *dev)
{
    uint16_t sel  = dev->codec ? ac97_codec_readw(dev->codec, 0x1a) : 0x0101;
    int      want = v2_capture_active(dev->chip)
               && (((sel >> 8) & 7) == 0 || ((sel >> 8) & 7) == 4 || (sel & 7) == 0 || (sel & 7) == 4);

    if (want) {
        dev->in_idle = 0;
        if (!dev->in_started) {
            dev->in_wr = dev->in_rd = 0;
            dev->in_frac            = 0.0;
            dev->in_primed          = 0;
            sound_in_start_input();
            dev->in_started = 1;
        }
    } else if (dev->in_started && ++dev->in_idle >= 48000 / V2_BLOCK / 2) {
        sound_in_stop_input();
        dev->in_started = 0;
    }
}

static void
host_codec_write(void *priv, uint8_t reg, uint16_t val)
{
    vortex2_t *dev = (vortex2_t *) priv;

    if (dev->codec) {
#ifdef ENABLE_VORTEX2_LOG
        if (dev->log_routes && dev->logf && reg != 0x26 && reg != 0x2c && reg != 0x32) {
            fprintf(dev->logf, "Vortex2: [%9.3f] codec %02x <- %04x\n", (double) dev->frames / 48000.0, reg, val);
            fflush(dev->logf);
        }
#endif
        ac97_codec_writew(dev->codec, reg, val);
        /* Install the CD filter once the driver sets up the CD input, as the
           VIA AC'97 device does; the codec mutes CD at reset, so installing
           it earlier would silence CD audio for software that never sets it. */
        if (reg == 0x12 && !dev->cd_filter) {
            sound_set_cd_audio_filter(vortex2_filter_cd_audio, dev);
            dev->cd_filter = 1;
        }
    }
}

/* The codec's ADC input for one block, after the record gain (0x1c: 1.5 dB
   steps from 0 dB, bit 15 mutes), from what the record select (0x1a) picks
   on each channel: CD audio (source 1), resampled from 44.1 to 48 kHz; the
   host's audio input as the microphone (source 0: mono, with the 20 dB
   boost of 0x0e bit 6) and as Line In (source 4). The other sources (video,
   aux, the stereo and mono mixes, phone) record silence.

   The CD reaches the card as an analog signal, and the driver sets the
   record gain to +12 dB on the real card too, so the level depends on the
   drive's line output and the card's input stage, which have not been
   measured. Taking the drive's digital full scale as the codec's full
   scale left the music about 14 dB above the game sound in the Half-Life
   tram ride and clipping, where real-card recordings have it beneath the
   game sound; the "cd_level" option sets the drive's output against codec
   full scale. The default, -12 dB, matches the balance of the current
   Steam release: with the track located in recordings, the music sits
   about 7 dB below the game sound there, against about 6 dB above in
   86Box at 0 dB with the CD mixer inputs at 0x00. A YouTube recording of
   the CD release gave 18 dB and a rough listening test 8 dB. */
static void
host_codec_adc(void *priv, float *l, float *r, int n)
{
    vortex2_t *dev = (vortex2_t *) priv;
    uint16_t   sel = dev->codec ? ac97_codec_readw(dev->codec, 0x1a) : 0;
    uint16_t   rg  = dev->codec ? ac97_codec_readw(dev->codec, 0x1c) : 0x8000;
    float      k   = dev->cd_level / 32768.0f;
    int        sl  = (sel >> 8) & 7, sr = sel & 7;
    float      rgl = (rg & 0x8000) ? 0.0f : powf(10.0f, ((rg >> 8) & 15) * 1.5f / 20.0f);
    float      rgr = (rg & 0x8000) ? 0.0f : powf(10.0f, (rg & 15) * 1.5f / 20.0f);
    float      gl  = sl == 1 ? rgl * k : 0.0f;
    float      gr  = sr == 1 ? rgr * k : 0.0f;
    float      mb  = (dev->codec && (ac97_codec_readw(dev->codec, 0x0e) & 0x0040)) ? 10.0f : 1.0f;
    int        host_in = sl == 0 || sl == 4 || sr == 0 || sr == 4;
    uint32_t   avail = dev->cd_wr - dev->cd_rd;

    /* CD audio arrives from 86Box's CD thread in chunks of 100 ms
       (CD_BUFLEN), at times late. Hold about 136 ms in reserve before
       playing, and start
       over if it runs dry, so a late chunk does not leave gaps; drop only
       a large backlog, so the delay stays short. */
    if (avail > 13000) {
        dev->cd_rd = dev->cd_wr - 6000;
        avail      = 6000;
    }
    if (!dev->cd_primed && avail >= 6000)
        dev->cd_primed = 1;
    for (int i = 0; i < n; i++) {
        dev->cd_phase += 44100.0 / 48000.0;
        while (dev->cd_phase >= 1.0) {
            dev->cd_phase -= 1.0;
            dev->cd_prev[0] = dev->cd_cur[0];
            dev->cd_prev[1] = dev->cd_cur[1];
            if (dev->cd_primed && dev->cd_wr != dev->cd_rd) {
                dev->cd_cur[0] = dev->cd_ring[dev->cd_rd % CD_RING][0];
                dev->cd_cur[1] = dev->cd_ring[dev->cd_rd % CD_RING][1];
                dev->cd_rd++;
            } else {
                dev->cd_primed = 0; /* ran dry (or nothing playing): refill first */
                dev->cd_cur[0] = dev->cd_cur[1] = 0.0f;
            }
        }
        float f = (float) dev->cd_phase;
        l[i] = (dev->cd_prev[0] + (dev->cd_cur[0] - dev->cd_prev[0]) * f) * gl;
        r[i] = (dev->cd_prev[1] + (dev->cd_cur[1] - dev->cd_prev[1]) * f) * gr;
        if (host_in) {
            float hl, hr;
            vortex2_in_frame(dev, &hl, &hr);
            float mic = (hl + hr) * 0.5f * mb;
            if (sl == 0)
                l[i] += mic * rgl;
            else if (sl == 4)
                l[i] += hl * rgl;
            if (sr == 0)
                r[i] += mic * rgr;
            else if (sr == 4)
                r[i] += hr * rgr;
        }
    }
#ifdef ENABLE_VORTEX2_LOG
    if (dev->log_routes && dev->logf) {
        /* Every 10 s: whether CD audio is arriving from the drive and how loud it is at the ADC. */
        for (int i = 0; i < n; i++)
            if (fabsf(l[i]) > dev->cd_log_peak)
                dev->cd_log_peak = fabsf(l[i]);
        dev->cd_log_n += n;
        if (dev->cd_log_n >= 480000) {
            fprintf(dev->logf, "Vortex2: [%9.3f] cd: %u frames from the drive in 10 s, ADC peak %.1f dBFS (select %04x, gain %04x, level option %.1f dB)\n",
                    (double) dev->frames / 48000.0, (unsigned) (dev->cd_wr - dev->cd_log_wr),
                    dev->cd_log_peak > 0.0f ? 20.0 * log10(dev->cd_log_peak) : -200.0, sel, rg, 20.0 * log10(dev->cd_level));
            fflush(dev->logf);
            dev->cd_log_n    = 0;
            dev->cd_log_wr   = dev->cd_wr;
            dev->cd_log_peak = 0.0f;
        }
    }
#endif
}

#ifdef ENABLE_VORTEX2_LOG
static void
host_log(void *priv, const char *msg)
{
    const vortex2_t *dev = (vortex2_t *) priv;

    if (dev->log_routes && dev->logf) {
        fputs(msg, dev->logf);
        fflush(dev->logf);
    }
}
#endif

/* ------------------------------------------------------------------ */
/* Sound output                                                        */
/* ------------------------------------------------------------------ */

static inline int16_t
to_s16(float v)
{
    int s = (int) lrintf(v * 32768.0f);
    return (s > 32767) ? 32767 : ((s < -32768) ? -32768 : s);
}

static void
vortex2_update(vortex2_t *dev)
{
    for (; dev->pos < sound_pos_global; dev->pos++) {
        if (dev->ring_n > 0) {
            dev->last[0] = dev->ring[dev->ring_rd * 2];
            dev->last[1] = dev->ring[dev->ring_rd * 2 + 1];
            dev->ring_rd = (dev->ring_rd + 1) % (V2_BLOCK * 8);
            dev->ring_n--;
        }
        dev->buffer[dev->pos * 2]     = dev->last[0];
        dev->buffer[dev->pos * 2 + 1] = dev->last[1];
    }
}

static void
vortex2_block(void *priv)
{
    vortex2_t *dev = (vortex2_t *) priv;
    int        ml = 32767, mr = 32767, pl = 32767, pr = 32767, sl = 32767, sr = 32767;

    timer_advance_u64(&dev->block_timer, dev->block_latch);

    vortex2_update(dev);
    dev->block_tsc = tsc;
    v2_run_block(dev->chip, dev->out);
    vortex2_in_update(dev);
    dev->frames += V2_BLOCK;

    if (dev->codec) {
        ac97_codec_getattn(dev->codec, 0x02, &ml, &mr);
        ac97_codec_getattn(dev->codec, 0x18, &pl, &pr);
        ac97_codec_getattn(dev->codec, 0x38, &sl, &sr);
    }
    float gl = (float) ml * (float) pl / (32768.0f * 32768.0f);
    float gr = (float) mr * (float) pr / (32768.0f * 32768.0f);
    float rl = dev->rear_mix ? (float) sl / 32768.0f : 0.0f;
    float rr = dev->rear_mix ? (float) sr / 32768.0f : 0.0f;

    /* Mixing the rear pair in: each output saturates on its own (the chip's
       output clips at digital full scale before the codec's volume; run
       12), and the two pairs are then averaged by the rear volume. In four-
       speaker mode the driver sends the same sound to front and rear, so a
       plain sum would be twice full scale on loud sounds and clip (dxdiag's
       test sounds peak at 0.6 on each pair). */
    float nl = 1.0f / (1.0f + rl), nr = 1.0f / (1.0f + rr);

    for (int n = 0; n < V2_BLOCK; n++) {
        float l = dev->out[n * 4 + 0], r = dev->out[n * 4 + 1];
        float bl = dev->out[n * 4 + 2], br = dev->out[n * 4 + 3];
        l  = (l > 1.0f) ? 1.0f : ((l < -1.0f) ? -1.0f : l);
        r  = (r > 1.0f) ? 1.0f : ((r < -1.0f) ? -1.0f : r);
        bl = (bl > 1.0f) ? 1.0f : ((bl < -1.0f) ? -1.0f : bl);
        br = (br > 1.0f) ? 1.0f : ((br < -1.0f) ? -1.0f : br);
        l  = (l + bl * rl) * nl;
        r  = (r + br * rr) * nr;
        int   w = (dev->ring_rd + dev->ring_n) % (V2_BLOCK * 8);
        if (dev->ring_n >= V2_BLOCK * 8) { /* consumer stalled: drop the oldest */
            dev->ring_rd = (dev->ring_rd + 1) % (V2_BLOCK * 8);
            dev->ring_n--;
            w = (dev->ring_rd + dev->ring_n) % (V2_BLOCK * 8);
        }
        dev->ring[w * 2]     = to_s16(l * gl);
        dev->ring[w * 2 + 1] = to_s16(r * gr);
        dev->ring_n++;
    }
}

static void
vortex2_get_buffer(int32_t *buffer, uint16_t len, void *priv)
{
    vortex2_t *dev = (vortex2_t *) priv;

    vortex2_update(dev);
    for (int c = 0; c < (int) len * 2; c++)
        buffer[c] += dev->buffer[c];
    dev->pos = 0;
}

static void
vortex2_irq_timer(void *priv)
{
    vortex2_t *dev = (vortex2_t *) priv;

    /* The counter reloads and keeps running; the driver normally rewrites
       the period in every interrupt anyway. */
    if (dev->irq_period)
        timer_advance_u64(&dev->irq_timer, dev->irq_period);
    v2_timer_expired(dev->chip);
}

/* ------------------------------------------------------------------ */
/* BAR0: the register file                                            */
/* ------------------------------------------------------------------ */

static uint32_t
vortex2_mmio_readl(uint32_t addr, void *priv)
{
    vortex2_t *dev = (vortex2_t *) priv;

    return tr_read(dev, addr - dev->bar0);
}

static uint16_t
vortex2_mmio_readw(uint32_t addr, void *priv)
{
    return vortex2_mmio_readl(addr & ~3, priv) >> ((addr & 2) * 8);
}

static uint8_t
vortex2_mmio_readb(uint32_t addr, void *priv)
{
    return vortex2_mmio_readl(addr & ~3, priv) >> ((addr & 3) * 8);
}

static void
vortex2_mmio_writel(uint32_t addr, uint32_t val, void *priv)
{
    vortex2_t *dev = (vortex2_t *) priv;

    tr_write(dev, addr - dev->bar0, val);
}

static void
vortex2_mmio_writew(uint32_t addr, uint16_t val, void *priv)
{
    vortex2_t *dev   = (vortex2_t *) priv;
    uint32_t   a     = (addr & ~3) - dev->bar0;
    int        shift = (addr & 2) * 8;
    uint32_t   old   = v2_read(dev->chip, a);

    tr_write(dev, a, (old & ~(0xffffu << shift)) | ((uint32_t) val << shift));
}

static void
vortex2_mmio_writeb(uint32_t addr, uint8_t val, void *priv)
{
    vortex2_t *dev   = (vortex2_t *) priv;
    uint32_t   a     = (addr & ~3) - dev->bar0;
    int        shift = (addr & 3) * 8;
    uint32_t   old   = v2_read(dev->chip, a);

    tr_write(dev, a, (old & ~(0xffu << shift)) | ((uint32_t) val << shift));
}

/* ------------------------------------------------------------------ */
/* BAR1: address (+0) / data (+4) window onto the same registers. The */
/* Windows driver uses it only during early initialisation. With the  */
/* chip control register's auto-increment bit (0x2a00c bit 19) set,   */
/* address bits 9-2 step by one register after each data access (for  */
/* the Sound Blaster TSR's reads of the FM register image).           */
/* BAR2: the modem side of the parallel interface, for the SQ3500's   */
/* daughterboard: eight byte registers laid out like a 16550 UART's.  */
/* The driver sets loopback (0x10 to +4, the modem control register)  */
/* and reads the modem status at +6; with nothing fitted it reads     */
/* 0xff, as on the SQ2500.                                            */
/* ------------------------------------------------------------------ */

static void
io1_step(vortex2_t *dev)
{
    if (v2_read(dev->chip, 0x2a00c) & (1u << 19))
        dev->index = (dev->index & ~0x3fcu) | ((dev->index + 4) & 0x3fcu);
}

static uint32_t
io1_get(vortex2_t *dev, uint16_t port)
{
    if (!(port & 4))
        return dev->index;
    uint32_t v = tr_read(dev, dev->index & 0x3fffc);
    io1_step(dev);
    return v;
}

static void
io1_put(vortex2_t *dev, uint16_t port, uint32_t val, uint32_t mask)
{
    if (port & 4) {
        uint32_t a   = dev->index & 0x3fffc;
        uint32_t old = (mask == 0xffffffff) ? 0 : v2_read(dev->chip, a);
        tr_write(dev, a, (old & ~mask) | (val & mask));
        io1_step(dev);
    } else
        dev->index = (dev->index & ~mask) | (val & mask);
}

static uint8_t
vortex2_io1_inb(uint16_t port, void *priv)
{
    return io1_get((vortex2_t *) priv, port) >> ((port & 3) * 8);
}

static uint16_t
vortex2_io1_inw(uint16_t port, void *priv)
{
    return io1_get((vortex2_t *) priv, port) >> ((port & 2) * 8);
}

static uint32_t
vortex2_io1_inl(uint16_t port, void *priv)
{
    return io1_get((vortex2_t *) priv, port);
}

static void
vortex2_io1_outb(uint16_t port, uint8_t val, void *priv)
{
    int sh = (port & 3) * 8;
    io1_put((vortex2_t *) priv, port, (uint32_t) val << sh, 0xffu << sh);
}

static void
vortex2_io1_outw(uint16_t port, uint16_t val, void *priv)
{
    int sh = (port & 2) * 8;
    io1_put((vortex2_t *) priv, port, (uint32_t) val << sh, 0xffffu << sh);
}

static void
vortex2_io1_outl(uint16_t port, uint32_t val, void *priv)
{
    io1_put((vortex2_t *) priv, port, val, 0xffffffff);
}

static uint8_t
vortex2_io2_inb(UNUSED(uint16_t port), UNUSED(void *priv))
{
    return 0xff;
}

static void
vortex2_io2_outb(UNUSED(uint16_t port), UNUSED(uint8_t val), UNUSED(void *priv))
{
    /* not modelled */
}

/* ------------------------------------------------------------------ */
/* PCI configuration space                                            */
/* ------------------------------------------------------------------ */

static void
vortex2_remap(vortex2_t *dev)
{
    int mem_on = dev->pci_regs[0x04] & PCI_COMMAND_MEM;
    int io_on  = dev->pci_regs[0x04] & PCI_COMMAND_IO;

    mem_mapping_disable(&dev->mmio);
    if (mem_on && dev->bar0)
        mem_mapping_set_addr(&dev->mmio, dev->bar0, V2_REG_SIZE);

    if (dev->io1_mapped) {
        io_removehandler(dev->io1_mapped, V2_IO_SIZE,
                         vortex2_io1_inb, vortex2_io1_inw, vortex2_io1_inl,
                         vortex2_io1_outb, vortex2_io1_outw, vortex2_io1_outl, dev);
        dev->io1_mapped = 0;
    }
    if (dev->io2_mapped) {
        io_removehandler(dev->io2_mapped, V2_IO_SIZE,
                         vortex2_io2_inb, NULL, NULL, vortex2_io2_outb, NULL, NULL, dev);
        dev->io2_mapped = 0;
    }
    if (io_on && dev->bar1) {
        io_sethandler(dev->bar1, V2_IO_SIZE,
                      vortex2_io1_inb, vortex2_io1_inw, vortex2_io1_inl,
                      vortex2_io1_outb, vortex2_io1_outw, vortex2_io1_outl, dev);
        dev->io1_mapped = dev->bar1;
    }
    if (io_on && dev->bar2) {
        io_sethandler(dev->bar2, V2_IO_SIZE,
                      vortex2_io2_inb, NULL, NULL, vortex2_io2_outb, NULL, NULL, dev);
        dev->io2_mapped = dev->bar2;
    }
    vortex2_log("Vortex2: BAR0 %08x BAR1 %04x BAR2 %04x mem %d io %d\n",
                dev->bar0, dev->bar1, dev->bar2, !!mem_on, !!io_on);
}

static uint8_t
vortex2_pci_read(int func, int addr, UNUSED(int len), void *priv)
{
    const vortex2_t *dev = (vortex2_t *) priv;

    if (func)
        return 0xff;

    switch (addr) {
        case 0x10 ... 0x13:
            return dev->bar0 >> ((addr & 3) * 8);
        case 0x14 ... 0x17:
            return (dev->bar1 | 1) >> ((addr & 3) * 8);
        case 0x18 ... 0x1b:
            return (dev->bar2 | 1) >> ((addr & 3) * 8);
        default:
            return dev->pci_regs[addr & 0xff];
    }
}

static void
vortex2_pci_write(int func, int addr, UNUSED(int len), uint8_t val, void *priv)
{
    vortex2_t *dev = (vortex2_t *) priv;
    uint32_t   sh  = (addr & 3) * 8;

    if (func)
        return;

    switch (addr) {
        case 0x04:
            dev->pci_regs[0x04] = val & (PCI_COMMAND_IO | PCI_COMMAND_MEM | PCI_COMMAND_L_BM | 0x40);
            vortex2_remap(dev);
            break;
        case 0x05:
            dev->pci_regs[0x05] = val & 0x01;
            break;
        case 0x07:
            dev->pci_regs[0x07] &= ~(val & 0xf9);
            break;
        case 0x0c:
        case 0x0d:
        case 0x3c:
        case 0x40: /* vendor register (0x80 at reset) */
            dev->pci_regs[addr] = val;
            break;
        case 0x10 ... 0x13:
            dev->bar0 = (dev->bar0 & ~(0xffu << sh)) | ((uint32_t) val << sh);
            dev->bar0 &= ~(V2_REG_SIZE - 1);
            vortex2_remap(dev);
            break;
        case 0x14 ... 0x15:
            dev->bar1 = (dev->bar1 & ~(0xffu << sh)) | ((uint32_t) val << sh);
            dev->bar1 &= ~(V2_IO_SIZE - 1);
            vortex2_remap(dev);
            break;
        case 0x18 ... 0x19:
            dev->bar2 = (dev->bar2 & ~(0xffu << sh)) | ((uint32_t) val << sh);
            dev->bar2 &= ~(V2_IO_SIZE - 1);
            vortex2_remap(dev);
            break;
        case 0xe0: /* PMCSR power state */
            if (((dev->pci_regs[0xe0] & 3) == 3) && !(val & 3))
                v2_reset(dev->chip); /* NoSoftRst-: D3 -> D0 resets the chip */
            dev->pci_regs[0xe0] = val & 0x03;
            break;
        default:
            break;
    }
}

static void
vortex2_pci_init_regs(vortex2_t *dev)
{
    memset(dev->pci_regs, 0, sizeof(dev->pci_regs));
    dev->pci_regs[0x00] = V2_VENDOR & 0xff;
    dev->pci_regs[0x01] = V2_VENDOR >> 8;
    dev->pci_regs[0x02] = V2_DEVICE & 0xff;
    dev->pci_regs[0x03] = V2_DEVICE >> 8;
    dev->pci_regs[0x06] = 0x90; /* capability list, fast back-to-back */
    dev->pci_regs[0x07] = 0x02; /* DEVSEL medium */
    dev->pci_regs[0x08] = V2_REVISION;
    dev->pci_regs[0x09] = 0x00;
    dev->pci_regs[0x0a] = 0x01; /* multimedia audio */
    dev->pci_regs[0x0b] = 0x04;
    dev->pci_regs[0x2c] = V2_VENDOR & 0xff;
    dev->pci_regs[0x2d] = V2_VENDOR >> 8;
    dev->pci_regs[0x2e] = V2_DEVICE & 0xff;
    dev->pci_regs[0x2f] = V2_DEVICE >> 8;
    dev->pci_regs[0x34] = 0xdc;
    dev->pci_regs[0x3d] = PCI_INTA;
    dev->pci_regs[0x3e] = 0x04; /* MIN_GNT */
    dev->pci_regs[0x3f] = 0x0c; /* MAX_LAT */
    dev->pci_regs[0x40] = 0x80;
    dev->pci_regs[0xdc] = 0x01; /* power management, version 1.0 */
    dev->pci_regs[0xdd] = 0x00;
    dev->pci_regs[0xde] = 0x21; /* DSI, D2 supported */
    dev->pci_regs[0xdf] = 0x04;
    dev->bar0           = 0;
    dev->bar1           = 0;
    dev->bar2           = 0;
}

/* ------------------------------------------------------------------ */
/* Device                                                              */
/* ------------------------------------------------------------------ */

static void
vortex2_speed_changed(void *priv)
{
    vortex2_t *dev = (vortex2_t *) priv;

    dev->block_latch = (uint64_t) ((double) TIMER_USEC * (1000000.0 * V2_BLOCK / (double) V2_RATE));
}

static void
vortex2_chip_reset(vortex2_t *dev)
{
    /* Only the control word is put back to its reset value: what else the
       chip clears is not known (the driver reads 0x2a804 straight after and
       rewrites everything it uses), and the legacy decodes follow the word. */
    v2_write(dev->chip, V2_CHIP_CTRL, 0x00303260);
    dev->game_fired = 0;
    timer_disable(&dev->fm_timer[0]);
    timer_disable(&dev->fm_timer[1]);
    dev->fm_ctl = dev->fm_flags = dev->fm_new = dev->fm_index = dev->fm_bank = 0;
    legacy_remap(dev);
}

static void
vortex2_reset(void *priv)
{
    vortex2_t *dev = (vortex2_t *) priv;

    v2_reset(dev->chip);
    if (dev->codec)
        ac97_codec_reset(dev->codec);
    dev->game_fired = 0;
    timer_disable(&dev->fm_timer[0]);
    timer_disable(&dev->fm_timer[1]);
    dev->fm_ctl = dev->fm_flags = dev->fm_new = dev->fm_index = dev->fm_bank = 0;
    legacy_remap(dev);
}

static void *
vortex2_init(UNUSED(const device_t *info))
{
    vortex2_t *dev = calloc(1, sizeof(vortex2_t));
    v2_host_t  host;

    dev->rear_mix   = device_get_config_int("rear_mix");
    dev->cd_level   = powf(10.0f, device_get_config_int("cd_level") / 20.0f);
#ifdef ENABLE_VORTEX2_LOG
    dev->log_routes = device_get_config_int("log");
    if (dev->log_routes) {
        /* Its own file in the machine's folder, so it works in release
           builds and without command-line options. */
        char fn[1024];
        path_append_filename(fn, usr_path, "vortex2.log");
        dev->logf = plat_fopen(fn, "w");
        if (dev->logf) {
            fprintf(dev->logf, "Vortex2: card added, diagnostic log on\n");
            fflush(dev->logf);
        }
    }

    if (device_get_config_int("trace")) {
        char fn[1024];
        path_append_filename(fn, usr_path, "vortex2-trace.txt");
        dev->tracef = plat_fopen(fn, "w");
        path_append_filename(fn, usr_path, "vortex2-dma.bin");
        dev->dmaf = plat_fopen(fn, "wb");
    }
#endif

    memset(&host, 0, sizeof(host));
    host.priv        = dev;
    host.mem_read    = host_mem_read;
    host.mem_write   = host_mem_write;
    host.set_irq     = host_set_irq;
    host.timer_arm   = host_timer_arm;
    host.clock       = host_clock;
    host.codec_read  = host_codec_read;
    host.codec_write = host_codec_write;
#ifdef ENABLE_VORTEX2_LOG
    host.log         = host_log;
#endif
    host.codec_adc   = host_codec_adc;

    sound_in_add_handler(vortex2_in_put_buffer, dev);

    timer_add(&dev->irq_timer, vortex2_irq_timer, dev, 0);

    dev->chip = v2_create(&host);

    vortex2_pci_init_regs(dev);
    pci_add_card(PCI_ADD_NORMAL, vortex2_pci_read, vortex2_pci_write, dev, &dev->pci_slot);

    mem_mapping_add(&dev->mmio, 0, 0,
                    vortex2_mmio_readb, vortex2_mmio_readw, vortex2_mmio_readl,
                    vortex2_mmio_writeb, vortex2_mmio_writew, vortex2_mmio_writel,
                    NULL, MEM_MAPPING_EXTERNAL, dev);
    mem_mapping_disable(&dev->mmio);

    /* SigmaTel STAC9708, as on the SQ2500. */
    ac97_codec       = &dev->codec;
    ac97_codec_count = 1;
    ac97_codec_id    = 0;
    device_add(&stac9708_device);

    sound_add_handler(vortex2_get_buffer, dev);

    /* Legacy MIDI and game port, mapped when the driver enables them. */
    dev->mpu = (mpu_t *) calloc(1, sizeof(mpu_t));
    mpu401_init(dev->mpu, 0, -1, M_UART, device_get_config_int("receive_input401"));
    mpu401_irq_attach(dev->mpu, legacy_mpu_irq, legacy_mpu_irq_pending, dev);
    /* The game port can be left off: Windows' legacy detection finds any
       port answering at 0x201 and adds its own "Gameport Joystick" entry,
       which then holds the address the Vortex's Gameport Interface asks
       for. Off, nothing answers there and the card's legacy byte reads as
       an empty port. */
    timer_add(&dev->fm_timer[0], fm_timer1, dev, 0);
    timer_add(&dev->fm_timer[1], fm_timer2, dev, 0);
    if (device_get_config_int("gameport")) {
        dev->gameport = gameport_add(&gameport_device);
        gameport_remap(dev->gameport, 0);
    }

    vortex2_speed_changed(dev);
    timer_add(&dev->block_timer, vortex2_block, dev, 1);
    timer_set_delay_u64(&dev->block_timer, dev->block_latch);

    return dev;
}

static void
vortex2_close(void *priv)
{
    vortex2_t *dev = (vortex2_t *) priv;

    sound_in_remove_handler(vortex2_in_put_buffer, dev);
    if (dev->in_started)
        sound_in_stop_input();
    v2_destroy(dev->chip);
    free(dev->mpu);
#ifdef ENABLE_VORTEX2_LOG
    if (dev->logf)
        fclose(dev->logf);
    if (dev->tracef)
        fclose(dev->tracef);
    if (dev->dmaf)
        fclose(dev->dmaf);
#endif
    free(dev);
}

static const device_config_t vortex2_config[] = {
    // clang-format off
    {
        .name           = "rear_mix",
        .description    = "Mix rear channels into the stereo output",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "cd_level",
        .description    = "CD audio input level",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = -12,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "0 dB",   .value =   0 },
            { .description = "-4 dB",  .value =  -4 },
            { .description = "-8 dB",  .value =  -8 },
            { .description = "-12 dB", .value = -12 },
            { .description = "-18 dB", .value = -18 },
            { .description = "-24 dB", .value = -24 },
            { .description = ""                     }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "gameport",
        .description    = "Game port",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "receive_input401",
        .description    = "Receive MIDI input (MPU-401)",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
#ifdef ENABLE_VORTEX2_LOG
    {
        .name           = "log",
        .description    = "Write diagnostic log",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "trace",
        .description    = "Write register trace (Warning: Very large!)",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
#endif
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t vortex2_device = {
    .name          = "Aureal Vortex 2 (SQ2500)",
    .internal_name = "vortex2",
    .flags         = DEVICE_PCI | DEVICE_AUDIO_IN,
    .local         = 0,
    .init          = vortex2_init,
    .close         = vortex2_close,
    .reset         = vortex2_reset,
    .available     = NULL,
    .speed_changed = vortex2_speed_changed,
    .force_redraw  = NULL,
    .config        = vortex2_config
};
