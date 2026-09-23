/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Promise PDC20269: the Ultra133 TX2.
 *
 *          The card's identity is a parameter, so that the rest of the
 *          PDC202xx family, which differ mainly in what they say they
 *          are and in the option ROM they carry, can be added beside it.
 *
 *          WHAT IS PROMISE'S RATHER THAN SFF'S. Underneath, this is an
 *          ordinary bus-master IDE controller and 86Box's own SFF-8038i
 *          core drives it. What the part adds:
 *
 *            - an index and data register pair in the two bytes SFF leaves
 *              reserved in each channel's bus-master block, and a register
 *              file behind it holding the per-drive transfer timing, the
 *              cable sense and the PLL;
 *            - a PLL that firmware does not read but MEASURES, by starting
 *              a counter and looking at it again later, and whose output
 *              sets the bus speed for the fast transfer modes;
 *            - a sixteen kilobyte memory window, on this generation only,
 *              carrying a second view of the task file and of the bus
 *              master, which Promise's own Windows driver uses in
 *              preference to the ports.
 *
 *          No data sheet for this family was ever published. The register
 *          numbers and the timing values are the Linux drivers'; the PLL
 *          arithmetic and the identity gates are from disassembling
 *          Promise's option ROMs and their Windows drivers, which is also
 *          where the memory window came from, since no driver that reaches
 *          the chip through ports knows it is there.
 *
 * Authors: Michael Pratte, <mpratte@makefox.group>
 *
 *          Copyright 2026 Michael Pratte.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/hdc_ide_sff8038i.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/plat_unused.h>
#include <86box/rom.h>
#include <86box/timer.h>

#define PDC20269_B15_ROM    "roms/hdd/ide/u133b15.bin"
#define PDC20269_B14_ROM    "roms/hdd/ide/u133b14.bin"
#define PDC20269_B12_ROM    "roms/hdd/ide/u133b12.bin"
#define PDC20269_MAXTOR_ROM "roms/hdd/ide/u133_maxtor_b10.bin"

/* Which card this instance is. */
#define PDC_ULTRA133  0

/* The index and data registers sit in the bytes SFF-8038i calls reserved,
   at one and three of each channel's eight. The secondary channel's pair is
   therefore at nine and eleven, which is why a driver that wants a register
   the chip keeps once rather than per channel writes it through the
   secondary window. */
#define PDC_IDX_OFF   1
#define PDC_DATA_OFF  3

/* Indexed registers, by number. Per drive unless said otherwise, with eight
   added for the slave. */
#define PDC_I_CTL     0x01 /* bit 6 runs the PLL counter, bits 3:2 reset a channel */
#define PDC_I_PLL_F   0x02 /* the whole chip's, and only decoded on channel 1 */
#define PDC_I_PLL_R   0x03
#define PDC_I_CABLE   0x0b /* bit 2 set: forty conductors */
#define PDC_I_PIO_A   0x0c
#define PDC_I_PIO_B   0x0d
#define PDC_I_MW_A    0x0e
#define PDC_I_MW_B    0x0f
#define PDC_I_UDMA_A  0x10
#define PDC_I_UDMA_B  0x11
#define PDC_I_UDMA_C  0x12
#define PDC_I_PIO_C   0x13
#define PDC_I_COUNT_L 0x20 /* the PLL counter, in four pieces across both */
#define PDC_I_COUNT_H 0x21 /* channels: see pdc_counter() */

/* The memory window, sixteen kilobytes, on this generation alone. Every
   offset here is Promise's and none of it is regular; the task file in
   particular is not on a stride, which is a fact about the hardware and not
   a misreading -- the driver's own code computes base+0x0F, base+0x10 and
   base+0x15 for three consecutive registers. */
#define PDC_MEM_SIZE  0x4000
#define PDC_MEM_BM    0x1000 /* plus eight for the secondary channel */
#define PDC_MEM_TF0   0x17c0
#define PDC_MEM_TF1   0x15c0
#define PDC_MEM_CTL0  0x1fd8
#define PDC_MEM_CTL1  0x1dd8

/* Half the PCI clock, which is what the counter counts and what firmware
   expects to measure. A part in a machine with a 33 MHz bus reports about
   16.9 MHz; both option ROMs and both Linux drivers are content anywhere
   between five and twenty-five. */
#define PDC_PLL_INPUT 16666667ULL

typedef struct pdc_t {
    uint8_t     card;      /* which of the two */
    int         ch_base;   /* IDE channels 0-1, or 2-3 where those are taken */
    uint8_t     pci_slot;
    uint8_t     irq_state;
    uint8_t     cable80;   /* what the cable sense answers */

    uint8_t     pci_regs[256];
    uint8_t     idx[2];      /* the index register of each channel */
    uint8_t     ireg[2][256];

    sff8038i_t *bm[2];
    uint16_t    bm_base;
    uint16_t    tf_base[2];
    uint16_t    ctl_base[2];

    uint32_t      mem_base;
    mem_mapping_t mem_mapping;

    /* The PLL counter runs from the moment test mode is turned on, and is
       read as the difference from where it started. */
    uint64_t    pll_started;
    uint8_t     pll_running;
    uint32_t    pll_held;    /* what it stopped at */

    rom_t       bios_rom;
    uint32_t    rom_addr;
    uint8_t     rom_enabled;
} pdc_t;


#ifdef ENABLE_PDC202XX_LOG
int pdc202xx_do_log = ENABLE_PDC202XX_LOG;

static void
pdc_log(const char *fmt, ...)
{
    va_list ap;

    if (!pdc202xx_do_log)
        return;

    va_start(ap, fmt);
    pclog_ex(fmt, ap);
    va_end(ap);
}
#else
#    define pdc_log(fmt, ...)
#endif

/* ------------------------------------------------------------------ */
/* The PLL counter                                                     */
/* ------------------------------------------------------------------ */

/* THIRTY BITS THAT COUNT DOWN, and the reason this part needs a clock at
   all rather than a register.
 *
 * Firmware does not ask the chip what its input clock is. It starts this
 * counter, waits a known time by some clock of its own, reads the counter
 * and divides. So what has to be right here is not a value but a RATE, and
 * it has to be a rate against emulated time, because the interval the
 * firmware waits is emulated time too.
 *
 * The option ROM takes no reading before it starts, which means it assumes
 * the counter is at its top when test mode is entered: it computes the
 * elapsed count as the one's complement of what it reads. So the counter
 * must begin at 0x3FFFFFFF each time the bit goes up, and that is what
 * latching the timestamp there gives.
 */
static uint32_t
pdc_counter(const pdc_t *dev)
{
    uint64_t usec;
    uint64_t ticks;
    uint64_t per_usec = TIMER_USEC >> 32;

    /* A COUNTER THAT STOPS HOLDS ITS VALUE, and that is not a detail: the
       option ROM turns test mode OFF and only then reads the count. A
       counter that returned to its top when the bit went down would tell
       the firmware that no time had passed, and the firmware divides by
       what it is told -- by zero, on a sixteen-bit divide, which ends the
       machine there and then. */
    if (!dev->pll_running || (per_usec == 0))
        return dev->pll_held;

    /* IN CYCLES, BUT ASK THE TIMER HOW MANY MAKE A MICROSECOND. The
       counter is derived from the processor's own cycle count, and
       TIMER_USEC is a microsecond expressed in that same count -- but in
       thirty-two thirty-two fixed point, so its integer half is the
       cycles-per-microsecond wanted here. Dividing by the whole of it
       gives zero, and a measurement of zero is what the firmware then
       divides by. */
    usec  = (tsc - dev->pll_started) / per_usec;
    ticks = (usec * PDC_PLL_INPUT) / 1000000ULL;

    pdc_log("PDC202xx: PLL counter read at %llu us, %llu ticks\n",
            (unsigned long long) usec, (unsigned long long) ticks);

    /* AND IT RESUMES, it does not restart. The counter is free running
       hardware that test mode only gates; software that reads it, lets it
       run and reads it again expects the second value to be BELOW the
       first. Anchoring each run back at the top makes the difference wrap
       to very nearly the whole thirty bits, which a driver then reports as
       a clock of a hundred gigahertz. */
    return (uint32_t) ((dev->pll_held - ticks) & 0x3fffffffULL);
}

/* FOUR BYTES, TWO CHANNELS, AND NOT A BYTE EACH. The counter is thirty bits
   and comes back in four pieces of eight, seven, eight and seven, two from
   each channel's window. Get the shifts wrong and the value a driver
   assembles jumps about, which it tests for and rejects. */
static uint8_t
pdc_counter_byte(const pdc_t *dev, int channel, int high)
{
    uint32_t count = pdc_counter(dev);

    if (channel == 0)
        return high ? (uint8_t) ((count >> 8) & 0x7f) : (uint8_t) (count & 0xff);

    return high ? (uint8_t) ((count >> 23) & 0x7f) : (uint8_t) ((count >> 15) & 0xff);
}

/* ------------------------------------------------------------------ */
/* The indexed register file                                           */
/* ------------------------------------------------------------------ */

static uint8_t
pdc_indexed_read(pdc_t *dev, int channel)
{
    uint8_t index = dev->idx[channel];

    if ((index != PDC_I_COUNT_L) && (index != PDC_I_COUNT_H)) {
        pdc_log("PDC202xx: ch%i indexed %02X -> %02X\n", channel, index,
                dev->ireg[channel][index]);
    }

    switch (index) {
        case PDC_I_COUNT_L:
            return pdc_counter_byte(dev, channel, 0);
        case PDC_I_COUNT_H:
            return pdc_counter_byte(dev, channel, 1);

        /* THE CABLE SENSE, and note which way round it is: the bit is set
           for FORTY conductors, so clear is the good answer and the one a
           machine with no cable to inspect should give. Bits 7 and 5 must
           never read as 0x20 together whatever the cable says, because
           Promise's driver reads that combination as a wedged engine and
           stops the bus master out from under the transfer. */
        case PDC_I_CABLE:
            return (dev->ireg[channel][index] & ~0xa4) | (dev->cable80 ? 0x00 : 0x04);

        default:
            break;
    }

    return dev->ireg[channel][index];
}

static void
pdc_indexed_write(pdc_t *dev, int channel, uint8_t val)
{
    uint8_t index = dev->idx[channel];
    uint8_t old   = dev->ireg[channel][index];

    dev->ireg[channel][index] = val;
    pdc_log("PDC202xx: ch%i indexed %02X <- %02X\n", channel, index, val);

    if (index == PDC_I_CTL) {
        /* Test mode is a GATE, not a reset. Up, the counter carries on
           from the value it was holding; down, it stops there and keeps
           it, because the firmware reads the count only after clearing
           the bit and a driver may read it again before the next run. */
        if ((val & 0x40) && !(old & 0x40)) {
            dev->pll_started = tsc;
            dev->pll_running = 1;
            pdc_log("PDC202xx: PLL counter started on channel %i\n", channel);
        } else if (!(val & 0x40) && (old & 0x40)) {
            dev->pll_held    = pdc_counter(dev);
            dev->pll_running = 0;
            pdc_log("PDC202xx: PLL counter stopped at %08X\n", dev->pll_held);
        }
    }

    /* The timing registers and the PLL pair have no effect worth modelling:
       nothing downstream of here counts cycles, so a transfer runs at the
       same speed whatever they say. They must read back what was written,
       because a driver that has just programmed the PLL reads it again to
       be sure, and Promise's own driver copies the option ROM's values out
       at probe and writes them back after every reset. */
}

/* ------------------------------------------------------------------ */
/* The bus-master block and the Promise registers beside it            */
/* ------------------------------------------------------------------ */

/* The interrupt and the DMA go to the SFF core exactly as they would
   have without this, but through here so a raise and a lower are seen
   in the trace next to the register traffic that caused them. */
static void
pdc_set_irq_0(uint8_t status, void *priv)
{
    pdc_t *dev = (pdc_t *) priv;

    pdc_log("PDC202xx: ch0 irq %s\n", (status & 0x04) ? "raise" : "lower");
    sff_bus_master_set_irq(status, dev->bm[0]);
}

static void
pdc_set_irq_1(uint8_t status, void *priv)
{
    pdc_t *dev = (pdc_t *) priv;

    pdc_log("PDC202xx: ch1 irq %s\n", (status & 0x04) ? "raise" : "lower");
    sff_bus_master_set_irq(status, dev->bm[1]);
}

/* THE START BIT DROPS BY ITSELF when the descriptor list is finished. The
   SFF core leaves it to software; this part does not. Promise's own
   drivers wait for command bit 0 to fall after a transfer and force it
   down only as recovery from a stuck engine, and the option ROM, having
   set it, never touches it again: its interrupt handler reads the bit
   and, finding it up, decides the interrupt belongs to someone else,
   chains it away and never marks the transfer complete. */
static int
pdc_bm_dma(pdc_t *dev, int channel, uint8_t *data, int transfer_length, int total_length, int out)
{
    sff8038i_t *bm  = dev->bm[channel];
    int         ret = sff_bus_master_dma(data, transfer_length, total_length, out, bm);

    if (!(bm->status & 0x01))
        bm->command &= ~0x01;

    return ret;
}

static int
pdc_bm_dma_0(uint8_t *data, int transfer_length, int total_length, int out, void *priv)
{
    return pdc_bm_dma((pdc_t *) priv, 0, data, transfer_length, total_length, out);
}

static int
pdc_bm_dma_1(uint8_t *data, int transfer_length, int total_length, int out, void *priv)
{
    return pdc_bm_dma((pdc_t *) priv, 1, data, transfer_length, total_length, out);
}

static uint8_t
pdc_bm_read(uint16_t port, void *priv)
{
    pdc_t  *dev    = (pdc_t *) priv;
    uint16_t offset = port - dev->bm_base;
    int      channel = (offset & 0x08) ? 1 : 0;
    uint8_t  ret     = 0xff;

    if (offset < 0x10) {
        switch (offset & 7) {
            case PDC_IDX_OFF:
                ret = dev->idx[channel];
                break;
            case PDC_DATA_OFF:
                ret = pdc_indexed_read(dev, channel);
                break;
            default:
                ret = sff_bus_master_read(port, dev->bm[channel]);
                /* The engine is active only while it has been started.
                   The SFF core comes out of reset with the bit up, and
                   this part's firmware refuses to start a transfer on a
                   channel that says one is already running. */
                if (((offset & 7) == 2) && !(dev->bm[channel]->command & 0x01))
                    ret &= ~0x01;
                if ((offset & 7) == 2) {
                    pdc_log("PDC202xx: ch%i bus master status -> %02X\n", channel, ret);
                } else if ((offset & 7) == 0) {
                    pdc_log("PDC202xx: ch%i bus master command -> %02X\n", channel, ret);
                }
                break;
        }
    } else switch (offset) {
        /* Interrupt and FIFO status, four bits to a channel: empty, full,
           interrupting, error. The interrupting bit is the one that
           matters, because it is how a driver sharing the line decides an
           interrupt is its own -- and how this part's own option ROM
           decides a transfer has finished. It has to rise and fall with
           the bus master's own status bit, or firmware waits out its
           timeout on a transfer that already completed. */
        case 0x1d:
            ret = 0x00;
            if (dev->bm[0] && (sff_bus_master_read(dev->bm_base + 2, dev->bm[0]) & 0x04))
                ret |= 0x04;
            if (dev->bm[1] && (sff_bus_master_read(dev->bm_base + 0x0a, dev->bm[1]) & 0x04))
                ret |= 0x40;
            /* Nothing here has a FIFO to be empty, and firmware that waits
               for one waits forever, so both channels say theirs is. */
            ret |= 0x11;
            break;

        default:
            ret = dev->ireg[0][0x80 + (offset & 0x3f)];
            break;
    }

    return ret;
}

static void
pdc_bm_write(uint16_t port, uint8_t val, void *priv)
{
    pdc_t   *dev     = (pdc_t *) priv;
    uint16_t offset  = port - dev->bm_base;
    int      channel = (offset & 0x08) ? 1 : 0;

    if (offset < 0x10) {
        switch (offset & 7) {
            case PDC_IDX_OFF:
                dev->idx[channel] = val;
                break;
            case PDC_DATA_OFF:
                pdc_indexed_write(dev, channel, val);
                break;
            default:
                sff_bus_master_write(port, val, dev->bm[channel]);
                break;
        }
        return;
    }

    /* Above the bus master the part has a scattering of registers that
       firmware writes and never reads: enables whose meaning was not
       recovered, and the transfer length and direction at 0x20 and 0x24
       that the chip needs for the two cases its engine cannot work out for
       itself, a forty-eight bit command and a packet command. Kept so they
       read back, and otherwise ignored. */
    dev->ireg[0][0x80 + (offset & 0x3f)] = val;
}

static uint16_t
pdc_bm_readw(uint16_t port, void *priv)
{
    return pdc_bm_read(port, priv) | (pdc_bm_read(port + 1, priv) << 8);
}

static uint32_t
pdc_bm_readl(uint16_t port, void *priv)
{
    return pdc_bm_readw(port, priv) | (pdc_bm_readw(port + 2, priv) << 16);
}

static void
pdc_bm_writew(uint16_t port, uint16_t val, void *priv)
{
    pdc_bm_write(port, val & 0xff, priv);
    pdc_bm_write(port + 1, (val >> 8) & 0xff, priv);
}

static void
pdc_bm_writel(uint16_t port, uint32_t val, void *priv)
{
    pdc_bm_writew(port, val & 0xffff, priv);
    pdc_bm_writew(port + 2, (val >> 16) & 0xffff, priv);
}

static void
pdc_bm_handler(pdc_t *dev, int enable)
{
    if (dev->bm_base != 0x0000)
        io_removehandler(dev->bm_base, 0x40, pdc_bm_read, pdc_bm_readw, pdc_bm_readl,
                         pdc_bm_write, pdc_bm_writew, pdc_bm_writel, dev);

    dev->bm_base = (dev->pci_regs[0x21] << 8) | (dev->pci_regs[0x20] & 0xc0);

    if (enable && (dev->bm_base != 0x0000) && (dev->pci_regs[0x04] & PCI_COMMAND_IO))
        io_sethandler(dev->bm_base, 0x40, pdc_bm_read, pdc_bm_readw, pdc_bm_readl,
                      pdc_bm_write, pdc_bm_writew, pdc_bm_writel, dev);
}

/* ------------------------------------------------------------------ */
/* The memory window                                                   */
/* ------------------------------------------------------------------ */

/* A SECOND VIEW OF THE SAME REGISTERS, which is all this is. Promise's
   Windows driver maps it on this part and then does its plain register work
   through it rather than through the ports, so a model that offers only the
   five I/O windows loads under Linux and fails under Windows. What it
   carries is a copy of each channel's task file, the alternate status, and
   the bus master, at offsets that are not on any stride. */
static int
pdc_mem_tf(uint32_t addr, int *channel)
{
    uint32_t base;

    if ((addr >= PDC_MEM_TF0) && (addr < PDC_MEM_TF0 + 0x20)) {
        *channel = 0;
        base     = PDC_MEM_TF0;
    } else if ((addr >= PDC_MEM_TF1) && (addr < PDC_MEM_TF1 + 0x20)) {
        *channel = 1;
        base     = PDC_MEM_TF1;
    } else
        return -1;

    switch (addr - base) {
        case 0x00:
            return 0; /* data */
        case 0x0a:
            return 2; /* sector count */
        case 0x0f:
            return 3; /* sector number, LBA 7:0 */
        case 0x10:
            return 4; /* cylinder low, LBA 15:8 */
        case 0x15:
            return 5; /* cylinder high, LBA 23:16 */
        case 0x1a:
            return 6; /* drive and head */
        case 0x1f:
            return 7; /* command on write, status on read */
        default:
            break;
    }

    return -1;
}

static uint8_t
pdc_mem_readb(uint32_t addr, void *priv)
{
    pdc_t *dev     = (pdc_t *) priv;
    int    channel = 0;
    int    reg;

    addr &= (PDC_MEM_SIZE - 1);

    if ((addr >= PDC_MEM_BM) && (addr < PDC_MEM_BM + 0x10)) {
        channel = (addr & 0x08) ? 1 : 0;
        return sff_bus_master_read(dev->bm_base + (addr - PDC_MEM_BM), dev->bm[channel]);
    }

    if ((addr == PDC_MEM_CTL0 + 2) || (addr == PDC_MEM_CTL1 + 2)) {
        channel = (addr == PDC_MEM_CTL1 + 2) ? 1 : 0;
        return inb(dev->ctl_base[channel]);
    }

    reg = pdc_mem_tf(addr, &channel);
    if (reg >= 0)
        return inb(dev->tf_base[channel] + reg);

    return 0xff;
}

static void
pdc_mem_writeb(uint32_t addr, uint8_t val, void *priv)
{
    pdc_t *dev     = (pdc_t *) priv;
    int    channel = 0;
    int    reg;

    addr &= (PDC_MEM_SIZE - 1);

    if ((addr >= PDC_MEM_BM) && (addr < PDC_MEM_BM + 0x10)) {
        channel = (addr & 0x08) ? 1 : 0;
        sff_bus_master_write(dev->bm_base + (addr - PDC_MEM_BM), val, dev->bm[channel]);
        return;
    }

    if ((addr == PDC_MEM_CTL0 + 2) || (addr == PDC_MEM_CTL1 + 2)) {
        channel = (addr == PDC_MEM_CTL1 + 2) ? 1 : 0;
        outb(dev->ctl_base[channel], val);
        return;
    }

    reg = pdc_mem_tf(addr, &channel);
    if (reg >= 0)
        outb(dev->tf_base[channel] + reg, val);
}

static uint16_t
pdc_mem_readw(uint32_t addr, void *priv)
{
    pdc_t *dev     = (pdc_t *) priv;
    int    channel = 0;

    if (pdc_mem_tf(addr & (PDC_MEM_SIZE - 1), &channel) == 0)
        return inw(dev->tf_base[channel]);

    return pdc_mem_readb(addr, priv) | (pdc_mem_readb(addr + 1, priv) << 8);
}

static void
pdc_mem_writew(uint32_t addr, uint16_t val, void *priv)
{
    pdc_t *dev     = (pdc_t *) priv;
    int    channel = 0;

    if (pdc_mem_tf(addr & (PDC_MEM_SIZE - 1), &channel) == 0) {
        outw(dev->tf_base[channel], val);
        return;
    }

    pdc_mem_writeb(addr, val & 0xff, priv);
    pdc_mem_writeb(addr + 1, (val >> 8) & 0xff, priv);
}

static uint32_t
pdc_mem_readl(uint32_t addr, void *priv)
{
    return pdc_mem_readw(addr, priv) | (pdc_mem_readw(addr + 2, priv) << 16);
}

static void
pdc_mem_writel(uint32_t addr, uint32_t val, void *priv)
{
    pdc_mem_writew(addr, val & 0xffff, priv);
    pdc_mem_writew(addr + 2, (val >> 16) & 0xffff, priv);
}

static void
pdc_mem_handler(pdc_t *dev)
{
    mem_mapping_disable(&dev->mem_mapping);

    dev->mem_base = (dev->pci_regs[0x27] << 24) | (dev->pci_regs[0x26] << 16) |
                    (dev->pci_regs[0x25] << 8);
    dev->mem_base &= ~(PDC_MEM_SIZE - 1);

    if ((dev->mem_base != 0x00000000) && (dev->pci_regs[0x04] & PCI_COMMAND_MEM)) {
        mem_mapping_set_addr(&dev->mem_mapping, dev->mem_base, PDC_MEM_SIZE);
        mem_mapping_enable(&dev->mem_mapping);
        pdc_log("PDC202xx: the memory window is at %08X\n", dev->mem_base);
    }
}

/* ------------------------------------------------------------------ */
/* The IDE channels                                                    */
/* ------------------------------------------------------------------ */

static void
pdc_ide_handler(pdc_t *dev)
{
    ide_handlers(dev->ch_base, 0);
    ide_handlers(dev->ch_base + 1, 0);

    dev->tf_base[0]  = (dev->pci_regs[0x11] << 8) | (dev->pci_regs[0x10] & 0xf8);
    dev->ctl_base[0] = ((dev->pci_regs[0x15] << 8) | (dev->pci_regs[0x14] & 0xfc)) + 2;
    dev->tf_base[1]  = (dev->pci_regs[0x19] << 8) | (dev->pci_regs[0x18] & 0xf8);
    dev->ctl_base[1] = ((dev->pci_regs[0x1d] << 8) | (dev->pci_regs[0x1c] & 0xfc)) + 2;

    if (!(dev->pci_regs[0x04] & PCI_COMMAND_IO))
        return;

    if (dev->tf_base[0] != 0x0000) {
        ide_set_base(dev->ch_base, dev->tf_base[0]);
        ide_set_side(dev->ch_base, dev->ctl_base[0]);
        ide_handlers(dev->ch_base, 1);
    }

    if (dev->tf_base[1] != 0x0000) {
        ide_set_base(dev->ch_base + 1, dev->tf_base[1]);
        ide_set_side(dev->ch_base + 1, dev->ctl_base[1]);
        ide_handlers(dev->ch_base + 1, 1);
    }
}

static void
pdc_rom_handler(pdc_t *dev)
{
    if (dev->rom_enabled)
        mem_mapping_disable(&dev->bios_rom.mapping);

    dev->rom_enabled = 0;
    dev->rom_addr    = ((dev->pci_regs[0x33] << 24) | (dev->pci_regs[0x32] << 16) |
                        (dev->pci_regs[0x31] << 8));
    dev->rom_addr &= 0xffffc000;

    if ((dev->pci_regs[0x30] & 0x01) && (dev->pci_regs[0x04] & PCI_COMMAND_MEM) &&
        (dev->rom_addr != 0x00000000)) {
        mem_mapping_set_addr(&dev->bios_rom.mapping, dev->rom_addr,
                             0x4000);
        mem_mapping_enable(&dev->bios_rom.mapping);
        dev->rom_enabled = 1;
    }
}

/* ------------------------------------------------------------------ */
/* Configuration space                                                 */
/* ------------------------------------------------------------------ */

static uint8_t
pdc_pci_read(int func, int addr, UNUSED(int len), void *priv)
{
    const pdc_t *dev = (pdc_t *) priv;

    /* A single-function device. Answering on every function makes the
       BIOS find a second controller and run the option ROM twice. */
    if (func != 0)
        return 0xff;

    return dev->pci_regs[addr & 0xff];
}

static void
pdc_pci_write(int func, int addr, UNUSED(int len), uint8_t val, void *priv)
{
    pdc_t *dev = (pdc_t *) priv;

    if (func != 0)
        return;

    switch (addr) {
        case 0x04:
            dev->pci_regs[0x04] = val & 0x17;
            pdc_ide_handler(dev);
            pdc_bm_handler(dev, 1);
            pdc_mem_handler(dev);
            pdc_rom_handler(dev);
            break;
        case 0x05:
            dev->pci_regs[0x05] = val & 0x01;
            break;

        case 0x07:
            dev->pci_regs[0x07] &= ~(val & 0xf9);
            break;

        /* The five I/O windows and the memory window. The low bits of each
           say how wide it is, and are not the guest's to change. */
        case 0x10: case 0x18:
            dev->pci_regs[addr] = (val & 0xf8) | 0x01;
            pdc_ide_handler(dev);
            break;
        case 0x14: case 0x1c:
            dev->pci_regs[addr] = (val & 0xfc) | 0x01;
            pdc_ide_handler(dev);
            break;
        case 0x11: case 0x15: case 0x19: case 0x1d:
            dev->pci_regs[addr] = val;
            pdc_ide_handler(dev);
            break;

        case 0x20:
            dev->pci_regs[0x20] = (val & 0xc0) | 0x01;
            pdc_bm_handler(dev, 1);
            break;
        case 0x21:
            dev->pci_regs[0x21] = val;
            pdc_bm_handler(dev, 1);
            break;

        case 0x24:
            dev->pci_regs[0x24] = 0x00;
            pdc_mem_handler(dev);
            break;
        case 0x25:
            dev->pci_regs[0x25] = val & 0xc0;
            pdc_mem_handler(dev);
            break;
        case 0x26: case 0x27:
            dev->pci_regs[addr] = val;
            pdc_mem_handler(dev);
            break;

        /* THE EXPANSION ROM WINDOW, AND HOW BIG IT SAYS IT IS. There is no
           size register anywhere in PCI: firmware writes all ones into a
           base address register and reads it back, and the bits that stay
           zero are the ones the device does not decode. So the bits below
           the window's size must be unwritable here, or firmware reads its
           own ones back, concludes the card wants four gigabytes of the
           address space, and declines to place the ROM at all -- which
           looks exactly like a card with no ROM on it. Sixteen kilobytes
           here. */
        case 0x30:
            dev->pci_regs[0x30] = val & 0x01;
            pdc_rom_handler(dev);
            break;
        case 0x31:
            dev->pci_regs[0x31] = val & 0xc0;
            pdc_rom_handler(dev);
            break;
        case 0x32: case 0x33:
            dev->pci_regs[addr] = val;
            pdc_rom_handler(dev);
            break;

        case 0x3c:
            dev->pci_regs[0x3c] = val;
            pdc_log("PDC202xx: interrupt line <- %02X\n", val);
            if (dev->bm[0] != NULL)
                sff_set_irq_line(dev->bm[0], val);
            if (dev->bm[1] != NULL)
                sff_set_irq_line(dev->bm[1], val);
            break;

        /* Promise's own. 0x50 carries the cable sense in bits 10 and 11 on
           the older parts of this family and a strap in bit 6; the timing
           block at 0x60 belongs to those parts and not to this one, which
           keeps its timing in the indexed registers. Both are kept so they
           read back, because a driver written for the whole family writes
           them without asking which part it has. */
        case 0x40: case 0x41:
        case 0x42:
        case 0x50: case 0x51:
        case 0x60 ... 0x7f:
            dev->pci_regs[addr] = val;
            break;

        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/* The device                                                          */
/* ------------------------------------------------------------------ */

static void
pdc_reset(void *priv)
{
    pdc_t   *dev = (pdc_t *) priv;
    uint16_t id  = 0x4d69;
    uint8_t  pll_f;
    uint8_t  pll_r;

    /* THE PLL OUTLIVES A RESET, which is not an assumption but something
       the drivers require. Promise's RAID driver reads the multiplier and
       divider once, at power on, and puts them back only when the machine
       resumes from sleep -- never after its own reset of the chip. A part
       that came back from reset with them cleared would run at the wrong
       speed for the rest of the session and nothing would put it right. */
    pll_f = dev->ireg[1][PDC_I_PLL_F];
    pll_r = dev->ireg[1][PDC_I_PLL_R];

    memset(dev->pci_regs, 0x00, sizeof(dev->pci_regs));
    memset(dev->ireg, 0x00, sizeof(dev->ireg));

    dev->pci_regs[0x00] = 0x5a;
    dev->pci_regs[0x01] = 0x10;
    dev->pci_regs[0x02] = id & 0xff;
    dev->pci_regs[0x03] = id >> 8;
    dev->pci_regs[0x06] = 0x80;
    dev->pci_regs[0x07] = 0x02;
    dev->pci_regs[0x08] = 0x02; /* revision */

    /* Native on both channels, programmable, bus master. The option ROM
       refuses a card whose task-file windows are not I/O, and says so:
       "please use compatible mode". */
    dev->pci_regs[0x09] = 0x8f;

    /* WHAT THE CARD SAYS IT IS. Promise's driver tests the class against
       the device identifier and refuses a part whose pair does not match,
       so this is not cosmetic. */
    dev->pci_regs[0x0a] = 0x80;
    dev->pci_regs[0x0b] = 0x01;

    dev->pci_regs[0x10] = 0x01;
    dev->pci_regs[0x14] = 0x01;
    dev->pci_regs[0x18] = 0x01;
    dev->pci_regs[0x1c] = 0x01;
    dev->pci_regs[0x20] = 0x01;

    /* The retail Ultra133 TX2 carries subsystem 105A:4D68, not its own
       device ID, and BIOS 2.20.0.12 and the Maxtor 2.20.0050.10 build
       require it: they read config word 2Eh, pass over any PDC20269 that
       says otherwise, then report that they cannot find the card. */
    dev->pci_regs[0x2c] = 0x5a;
    dev->pci_regs[0x2d] = 0x10;
    dev->pci_regs[0x2e] = 0x68;
    dev->pci_regs[0x2f] = 0x4d;

    dev->pci_regs[0x3d] = PCI_INTA;
    dev->pci_regs[0x3e] = 0x04;
    dev->pci_regs[0x3f] = 0x12;

    /* The same answer in the place the older parts of the family keep it:
       bits 10 and 11 of configuration word 0x50, one per channel, set for
       forty conductors. This part keeps its cable sense in an indexed
       register instead, but a driver written for the whole family may look
       here, and an answer that contradicts the other one would be worse
       than either. */
    dev->pci_regs[0x50] = 0x00;
    dev->pci_regs[0x51] = dev->cable80 ? 0x00 : 0x0c;

    dev->idx[0] = dev->idx[1] = 0x00;
    dev->pll_running = 0;
    dev->pll_held    = 0x3fffffff;

    /* Put back what the PLL held, or, the first time, what the option ROM
       would have left there had one run: a divider of eight and a
       multiplier of seventy-eight, which is a hundred and thirty-three
       megahertz from a sixteen and two thirds input. Promise's Windows
       drivers never compute these, so a machine that boots without running
       the card's ROM still finds something sensible. */
    dev->ireg[1][PDC_I_PLL_F] = pll_f ? pll_f : 78;
    dev->ireg[1][PDC_I_PLL_R] = pll_r ? pll_r : 8;

    pdc_ide_handler(dev);
    pdc_bm_handler(dev, 0);
    pdc_mem_handler(dev);
    pdc_rom_handler(dev);

    sff_bus_master_reset(dev->bm[0]);
    sff_bus_master_reset(dev->bm[1]);

    /* THE SLOT IS NOT KNOWN UNTIL AFTER INIT. Adding the card only queues
       it; the slot is handed out later, when the cards are registered, so
       a bus master told its slot at init is told the invalid one, and an
       interrupt raised from an invalid slot is dropped at the door, in
       silence. Reset runs after registration, so it is told here. */
    sff_set_slot(dev->bm[0], dev->pci_slot);
    sff_set_slot(dev->bm[1], dev->pci_slot);
    sff_set_irq_pin(dev->bm[0], PCI_INTA);
    sff_set_irq_pin(dev->bm[1], PCI_INTA);
    sff_set_irq_mode(dev->bm[0], IRQ_MODE_PCI_IRQ_PIN);
    sff_set_irq_mode(dev->bm[1], IRQ_MODE_PCI_IRQ_PIN);
}

static void
pdc_close(void *priv)
{
    pdc_t *dev = (pdc_t *) priv;

    free(dev);
}

static void *
pdc_init(const device_t *info)
{
    pdc_t *dev = (pdc_t *) calloc(1, sizeof(pdc_t));

    const char *rom_file;
    uint32_t    rom_size;

    dev->card    = info->local & 0xff;
    dev->cable80 = !!device_get_config_int("cable80");

    rom_size = 0x4000;
    rom_file = device_get_bios_file(info, device_get_config_bios("bios_rev"), 0);
    if ((rom_file == NULL) || (rom_file[0] == '\0'))
        rom_file = PDC20269_B15_ROM;

    rom_init(&dev->bios_rom, (char *) rom_file, 0x000c8000, rom_size,
             rom_size - 1, 0, MEM_MAPPING_EXTERNAL);
    mem_mapping_disable(&dev->bios_rom.mapping);

    /* THE PRIMARY AND SECONDARY WHERE THEY ARE FREE, the tertiary and
       quaternary where they are not. A board with its own IDE (PIIX, VIA,
       ALi...) or another controller has already claimed the first two by
       now, and with them bus master instances 1 and 2; the card then takes
       the next two, as any add-in PCI IDE controller does. Where nothing
       has, the card is the machine's IDE, as it always was. */
    dev->ch_base = (ide_board_claimed(0) || ide_board_claimed(1)) ? 2 : 0;
    if (dev->ch_base)
        device_add(&ide_pci_ter_qua_2ch_device);
    dev->bm[0] = device_add_inst(&sff8038i_device, dev->ch_base + 1);
    dev->bm[1] = device_add_inst(&sff8038i_device, dev->ch_base + 2);
    /* The SFF core adds the primary and secondary itself, but only for
       the first bus master in the machine. */
    if (!dev->ch_base && !ide_board_claimed(0))
        device_add(&ide_pci_2ch_device);

    ide_set_bus_master(dev->ch_base, pdc_bm_dma_0, pdc_set_irq_0, dev);
    ide_set_bus_master(dev->ch_base + 1, pdc_bm_dma_1, pdc_set_irq_1, dev);

    /* The sixteen kilobyte memory window, through which Promise's driver
       does its register work on this generation. */
    mem_mapping_add(&dev->mem_mapping, 0, 0,
                    pdc_mem_readb, pdc_mem_readw, pdc_mem_readl,
                    pdc_mem_writeb, pdc_mem_writew, pdc_mem_writel,
                    NULL, MEM_MAPPING_EXTERNAL, dev);
    mem_mapping_disable(&dev->mem_mapping);

    /* The two channels come with the bus master: the SFF core adds them
       itself for the first instance of it, so a card that added them again
       would be refused as a duplicate. */

    pci_add_card(PCI_ADD_NORMAL, pdc_pci_read, pdc_pci_write, dev, &dev->pci_slot);

    pdc_reset(dev);

    return dev;
}

static int
pdc20269_available(void)
{
    return rom_present(PDC20269_B15_ROM);
}

/* EIGHTY CONDUCTORS OR FORTY, which the chip senses on a pin and which
   nothing in an emulated machine can be inspected to decide. Eighty is the
   default because it is what the fast modes need and what anybody putting a
   UDMA133 card in a machine meant to have; turning it off is how to see a
   guest clamp itself to UDMA2 and complain about the cable, which is a real
   behaviour worth being able to reproduce. */
#define PDC_CABLE_OPTION                                                     \
    {                                                                        \
        .name           = "cable80",                                         \
        .description    = "80-conductor cable",                              \
        .type           = CONFIG_BINARY,                                     \
        .default_string = NULL,                                              \
        .default_int    = 1,                                                 \
        .file_filter    = NULL,                                              \
        .spinner        = { 0 },                                             \
        .selection      = { { 0 } },                                         \
        .bios           = { { 0 } }                                          \
    }

static const device_config_t pdc20269_config[] = {
    // clang-format off
    PDC_CABLE_OPTION,
    {
        .name           = "bios_rev",
        .description    = "BIOS Revision",
        .type           = CONFIG_BIOS,
        .default_string = "b15",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .bios           = {
            {
                .name          = "Version 2.20.0.15",
                .internal_name = "b15",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 16384,
                .files         = { PDC20269_B15_ROM, "" }
            },
            {
                .name          = "Version 2.20.0.14",
                .internal_name = "b14",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 16384,
                .files         = { PDC20269_B14_ROM, "" }
            },
            {
                .name          = "Version 2.20.0.12",
                .internal_name = "b12",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 16384,
                .files         = { PDC20269_B12_ROM, "" }
            },
            {
                .name          = "Version 2.20.0050.10 (Maxtor)",
                .internal_name = "maxtor_b10",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 16384,
                .files         = { PDC20269_MAXTOR_ROM, "" }
            },
            { .files_no = 0 }
        },
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t pdc20269_device = {
    .name          = "Promise Ultra133 TX2 (PDC20269)",
    .internal_name = "pdc20269",
    .flags         = DEVICE_PCI,
    .local         = PDC_ULTRA133,
    .init          = pdc_init,
    .close         = pdc_close,
    .reset         = pdc_reset,
    .available     = pdc20269_available,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pdc20269_config
};

