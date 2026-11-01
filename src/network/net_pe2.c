/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Xircom Pocket Ethernet Adapter II (PE2) LPT NIC emulation.
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2026 RichardG.
 */
#include <inttypes.h>
#include <memory.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/lpt.h>
#include <86box/thread.h>
#include <86box/network.h>
#include <86box/net_dp8390.h>
#include <86box/log.h>
#include <86box/plat_unused.h>
#include <86box/random.h>

/* Control port bits. */
#define PE2_CTRL_STROBE   0x01
#define PE2_CTRL_AUTOFEED 0x02
#define PE2_CTRL_INIT     0x04
#define PE2_CTRL_SELECT   0x08
#define PE2_CTRL_DIR      0x20

/* Use the length to tell PE1 and PE2 FPGA bitstreams apart. */
#define PE1_FW_LEN 1507
#define PE2_FW_LEN 2780

/* Fingerprints of PE2 bitstreams, one for each transfer type.
   There are other bitstreams used solely for port probing. */
#define PE2_FW_FNV_BIT2   0x6eb7ba47
#define PE2_FW_FNV_NIBBLE 0x5d39eb28
#define PE2_FW_FNV_EPP    0x993e00c2

/* Which line the adapter counts for each of the 16 strobe lines it
   can be set to, as a port offset and the bit that moves on it. */
static const uint8_t pe2_strobe_port[16] = {
    0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2, 4, 0
};
static const uint8_t pe2_strobe_bit[16] = {
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
    0x01, 0x02, 0x08, 0x01, 0x02, 0x08, 0x01, 0x00
};

/* FPGA register space. */
#define PE2_GATE_PULSES 0x10

typedef struct pe2_t {
    void      *log;
    void      *lpt;
    netcard_t *card;
    dp8390_t  *dp8390;

    uint8_t  mac[6];
    uint16_t eeprom_data[16];

    uint8_t  is_pe1;
    uint16_t fw_len;
    uint8_t  fw_done;
    uint32_t fw_bits;
    uint32_t fw_fnv;
    uint8_t  fw_byte;
    uint8_t  fw_nbits;

    uint8_t mode;
    uint8_t counting;
    uint8_t pulses;

    uint8_t running;
    uint8_t irq;

    uint8_t pe1_access;
    uint8_t pe1_block;
    uint8_t pe1_irq_cfg;

    uint8_t data;
    uint8_t ctrl;
    uint8_t reg;
    uint8_t pend_read;
    uint8_t pend_write;
    uint8_t wr_reg;
    uint8_t status;
    uint8_t rd_phase;
    uint8_t rd_bidir;
    uint8_t latch;

    uint8_t cr;
    uint8_t cr_seen;
    uint8_t ramop;

    uint8_t  ee;
    uint16_t ee_sr;
    uint8_t  ee_cnt;
    uint16_t ee_out;
    uint8_t  ee_obits;
    uint8_t  ee_bit;
} pe2_t;

#ifdef ENABLE_PE2_LOG
int pe2_do_log = ENABLE_PE2_LOG;

static void
pe2_log(void *priv, const char *fmt, ...)
{
    va_list ap;

    if (pe2_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define pe2_log(priv, fmt, ...)
#endif

static uint8_t
pe2_pulse_readback(const pe2_t *dev)
{
    /* Drivers probe this bitstream-specific behavior.
       Implement just enough of it to satisfy them. */
    if (dev->pulses >= 200) {
        switch (pe2_strobe_port[dev->mode]) {
            case 2:
                return 0xf9; /* 248 latched data writes + the strobe edge */
            case 4:
                return 0xf8; /* one per EPP bus cycle */
            default:
                break;
        }
    }

    return dev->pulses;
}

static uint8_t
pe2_gate_read(const pe2_t *dev, uint8_t reg)
{
    return (reg == PE2_GATE_PULSES) ? dev->pulses : 0x00;
}

static uint8_t
pe2_read_reg(pe2_t *dev, uint8_t reg)
{
    if (!reg) {
        /* DP8390 command register. Reads back the last byte given for it,
           that is the command register once the controller is being driven,
           except for TXP, which the controller clears when it is done. */
        if (dev->cr_seen)
            return (dev->cr & ~0x04) | (dev->dp8390->CR.tx_packet ? 0x04 : 0x00);
        return dev->latch;
    }

    /* DP8390 paged registers. */
    switch (dev->dp8390->CR.pgsel) {
        case 0:
            return dp8390_page0_read(dev->dp8390, reg, 1);
        case 1:
            return dp8390_page1_read(dev->dp8390, reg, 1);
        case 2:
            return dp8390_page2_read(dev->dp8390, reg, 1);
        default:
            return 0xff;
    }
}

static void
pe2_write_reg(pe2_t *dev, uint8_t reg, uint8_t val)
{
    if (!reg) {
        /* DP8390 command register. */
        dev->cr      = val;
        dev->cr_seen = 1;
        dp8390_write_cr(dev->dp8390, val);

        /* [5:3] start or end a remote DMA. */
        switch (dev->dp8390->CR.rdma_cmd) {
            case 1:
                dev->ramop = 2;
                break;
            case 2:
                dev->ramop = 1;
                break;
            default:
                dev->ramop = 0;
                break;
        }
        return;
    }

    /* DP8390 paged registers. */
    switch (dev->dp8390->CR.pgsel) {
        case 0:
            dp8390_page0_write(dev->dp8390, reg, val, 1);
            break;
        case 1:
            dp8390_page1_write(dev->dp8390, reg, val, 1);
            break;
        case 2:
            dp8390_page2_write(dev->dp8390, reg, val, 1);
            break;
        default:
            break;
    }
}

static void
pe2_dma_advance(pe2_t *dev)
{
    dp8390_t *dp = dev->dp8390;

    if (++dp->remote_dma == (dp->page_stop << 8))
        dp->remote_dma = dp->page_start << 8;
    if (dp->remote_bytes && !--dp->remote_bytes) {
        dp->ISR.rdma_done = 1;
        dev->ramop        = 0;
    }
}

static int
pe2_irq_level(const pe2_t *dev)
{
    /* PE1 commands 00-03 set up the line: [1] passes the controller's
       interrupt through and [0] inverts it, which with [1] clear
       drives nAck by hand. The driver probes for its IRQ that way. */
    if (dev->is_pe1)
        return (dev->pe1_irq_cfg & 0x01) ^ ((dev->pe1_irq_cfg & 0x02) && dev->irq);

    return dev->irq;
}

static void
pe2_interrupt(void *priv, int set)
{
    pe2_t *dev = (pe2_t *) priv;

    /* FPGA wires the controller's interrupt to nAck. */
    dev->irq = !!set;
    lpt_irq(dev->lpt, pe2_irq_level(dev));
}

static void
pe2_reset(pe2_t *dev)
{
    pe2_log(dev->log, "reset()\n");

    dev->fw_done   = 0; /* nothing answers until a bitstream arrives */
    dev->fw_bits   = 0;
    dev->fw_fnv    = 2166136261u;
    dev->fw_nbits  = 0;
    dev->fw_byte   = 0;

    dev->mode     = 0;
    dev->counting = 0;
    dev->pulses   = 0;
    dev->running  = 0;

    dev->pe1_access  = 0;
    dev->pe1_block   = 0;
    dev->pe1_irq_cfg = 0;

    dev->data       = 0;
    dev->ctrl       = 0;
    dev->reg        = 0;
    dev->pend_read  = 0;
    dev->pend_write = 0;
    dev->wr_reg     = 0;
    dev->status     = 0;
    dev->rd_phase   = 0;
    dev->rd_bidir   = 0;
    dev->latch      = 0;

    dev->cr      = 0;
    dev->cr_seen = 0;
    dev->ramop   = 0;

    dev->ee       = 0;
    dev->ee_sr    = 0;
    dev->ee_cnt   = 0;
    dev->ee_out   = 0;
    dev->ee_obits = 0;
    dev->ee_bit   = 0;

    if (dev->dp8390)
        dp8390_reset(dev->dp8390);
}

static void
pe2_command(pe2_t *dev, uint8_t val)
{
    /* 1x and Dx both pick strobe line x and restart the pulse counter. The
       driver profiles the port with the former and times it with the latter. */
    int select = !dev->running && ((((val) & 0xf0) == 0x10) || (((val) & 0xf0) == 0xd0));

    if (!select)
        dev->counting = 0; /* any other command ends the measurement */

    if (dev->is_pe1 && dev->running) {
        /* PE1 command byte only addresses: [5] is a single byte access and [6]
           picks register 40, the block data register, over [4:0]. Which way the
           byte moves is up to nStrobe during the operand strobe, so 20|reg and
           A0|reg are the same access; [7] only says the driver will turn the
           port around to read it byte-wide. With [5] clear, [6] starts a block
           transfer, and the rest drive the EEPROM (08/18) or the interrupt. */
        dev->ee         = 0;
        dev->pe1_access = 0;
        dev->pe1_block  = 0;

        if (val & 0x60) {
            dev->reg = (val & 0x40) ? 0x40 : (val & 0x1f);
            if (val & 0x20) {
                dev->pe1_access = 1;
            } else {
                /* Remote DMA streamed through the port, in the direction the
                   command register last asked for and as long as RBCR says.
                   A read has the first byte waiting before the first strobe. */
                dev->pe1_block = 1;
                if (dev->dp8390->CR.rdma_cmd == 1)
                    dev->status = dp8390_chipmem_read(dev->dp8390, dev->dp8390->remote_dma, 1);
            }
            pe2_log(dev->log, "command(%02X) %s register %02X\n", val,
                    dev->pe1_block ? "block" : "access", dev->reg);
        } else if (val & 0x08) {
            dev->ee = (val & 0x10) ? 1 : 2;
            if (dev->ee == 1) { /* selecting starts a fresh command */
                dev->ee_sr    = 0;
                dev->ee_cnt   = 0;
                dev->ee_obits = 0;
                dev->ee_bit   = 0;
            }
            pe2_log(dev->log, "command(%02X) EEPROM chip select %s\n", val, (val & 0x10) ? "on" : "off");
        } else {
            dev->pe1_irq_cfg = val & 0x03;
            lpt_irq(dev->lpt, pe2_irq_level(dev));
            pe2_log(dev->log, "command(%02X) interrupt %s%s\n", val,
                    (val & 0x02) ? "enabled" : "disabled", (val & 0x01) ? ", inverted" : "");
        }
        return;
    }

    /* A running mode addresses the controller directly: the command byte is the
       register, with bit 4 set to read it back, and register 0 is its command
       register. The driver initializes by writing and reading back 25 of them. */
    if (dev->running && !(val & 0xf0)) {
        dev->pend_read  = 0;
        dev->pend_write = 1;
        dev->wr_reg     = 1;
        dev->reg        = val & 0x0f;
        return;
    }

    if (dev->running && ((val & 0xf0) == 0x10)) {
        dev->pend_read  = 1;
        dev->pend_write = 0;
        dev->reg        = val & 0x1f;
        dev->rd_phase   = 1;
        dev->status     = pe2_read_reg(dev, val & 0x0f);
        pe2_log(dev->log, "command(%02X) read page %u register %X = %02X\n",
                val, dev->dp8390->CR.pgsel, val & 0x0f, dev->status);
        return;
    }

    /* With a mode running, 9x asserts the EEPROM's chip select and 8x
       drops it; the clock and data then ride the data port directly, so
       the bus belongs to the EEPROM until some other command takes it back. */
    if (dev->running && (((val & 0xf0) == 0x80) || ((val & 0xf0) == 0x90))) {
        dev->ee = (val & 0x10) ? 1 : 2;
        if (dev->ee == 1) { /* selecting starts a fresh command */
            dev->ee_sr    = 0;
            dev->ee_cnt   = 0;
            dev->ee_obits = 0;
            dev->ee_bit   = 0;
        }
        pe2_log(dev->log, "command(%02X) EEPROM chip select %s\n", val, (val & 0x10) ? "on" : "off");
        return;
    }
    dev->ee = 0;

    if (select) {
        dev->mode       = val & 0x0f;
        dev->counting   = 1;
        dev->pulses     = 0;
        dev->pend_read  = 0;
        dev->pend_write = 0;
        pe2_log(dev->log, "command(%02X) strobe line %u, pulse count reset\n", val, dev->mode);
        return;
    }

    /* Anything left addresses the FPGA's own registers, with the operation in
       [7:5]. The driver only ever reads the pulse counter (0x70) back out. */
    dev->pend_read  = 0;
    dev->pend_write = 0;
    dev->wr_reg     = 0;
    dev->reg        = val & 0x1f;

    switch (val >> 5) {
        case 2: /* [40:5F] write */
        case 4: /* [80:9F] write (used while probing) */
            dev->pend_write = 1;
            break;

        case 3: /* [60:7F] read */
        case 7: /* [E0:FF] read (byte-wide) */
            dev->pend_read = 1;
            dev->rd_phase  = 1; /* PE2 sends the low half first */
            dev->rd_bidir  = 0; /* assume not bidirectional at first */
            dev->status    = pe2_gate_read(dev, dev->reg);
            break;

        default:
            break;
    }

    pe2_log(dev->log, "command(%02X)\n", val);
}

static void
pe2_write_data(uint8_t val, void *priv)
{
    pe2_t *dev = (pe2_t *) priv;

    if ((dev->ramop == 1) && dev->running && !dev->is_pe1) {
        /* The running mode writes the buffer by walking the
           data port with no strobe at all, one byte per write. */
        dp8390_chipmem_write(dev->dp8390, dev->dp8390->remote_dma, val, 1);
        pe2_dma_advance(dev);
        dev->data = val;
        return;
    }

    if (dev->ee) { /* [ ] [ ] [ ] [ ] [ ] [ ] [CLK] [DI] */
        if ((dev->ee == 1) && (val & 0x02) && !(dev->data & 0x02)) { /* rising clock */
            if (dev->ee_obits) {
                /* The driver clocks and then reads, so present the bit
                   the clock shifted out rather than the one after it. */
                dev->ee_bit   = (dev->ee_out >> 15) & 0x01;
                dev->ee_out <<= 1;
                dev->ee_obits--;
            } else {
                dev->ee_sr = (dev->ee_sr << 1) | (val & 0x01);
                /* Two lead bits, then a byte holding 0x40 | the word wanted. */
                if (++dev->ee_cnt == 10) {
                    /* Lead 0, start 1, then the usual 93C46 opcode and address. */
                    uint8_t op   = (dev->ee_sr >> 6) & 0x03;
                    uint8_t word = dev->ee_sr & 0x0f;
                    if (op == 0x02) { /* read */
                        dev->ee_out   = dev->eeprom_data[word];
                        dev->ee_obits = 16;
                    }
                    pe2_log(dev->log, "EEPROM op %u word %u -> %04X\n", op, word, dev->ee_out);
                }
            }
        }
        dev->data = val;
        return;
    }

    if (dev->is_pe1 && dev->running) { /* everything else happens on the control lines */
        dev->data = val;
        return;
    }

    if (dev->pend_read && !dev->counting) {
        dev->rd_phase++; /* a data write picks the other half of the byte */
        if (!dev->running) /* a running mode already latched its own byte */
            dev->status = pe2_gate_read(dev, dev->reg);
    }

    /* The counter tallies latch events, not just edges. With a data line as
       the strobe, that means its own rising edges; with a control line as the
       strobe, it means every data write made while that line is asserted. */
    if (dev->counting) {
        uint8_t bit = pe2_strobe_bit[dev->mode];
        if (!pe2_strobe_port[dev->mode]) {
            if ((val & bit) && !(dev->data & bit))
                dev->pulses++;
        } else if (pe2_strobe_port[dev->mode] == 2)
            dev->pulses++; /* a control line as the strobe latches every data write */
    }

    dev->data = val;
}

static int
pe2_firmware_write_ctrl(pe2_t *dev, uint8_t prev, uint8_t val)
{
    /* Dropping every line resets the gate array and starts a new upload,
       which the driver does for each transfer mode it tries, since each
       has its own bitstream. Miss this and the next 22000 clocks are read
       as commands. Mask off control[7:4] which aren't actual control pins. */
    if (!(val & 0x0f)) {
        if (dev->fw_bits || dev->fw_done)
            pe2_log(dev->log, "gate array reset after %u bits, reconfiguring\n", dev->fw_bits);
        dev->fw_bits  = 0;
        dev->fw_done  = 0;
        dev->fw_fnv   = 2166136261u;
        dev->fw_nbits = 0;
        return 1; /* still in progress */
    }

    if (dev->fw_done)
        return 0;

    if ((val & PE2_CTRL_SELECT) && !(prev & PE2_CTRL_SELECT)) { /* rising edge clocks a bit */
        /* Fingerprint the image as it arrives: the driver has one
           bitstream for probing the port and different ones for each
           transfer mode, and the same command means different things
           under each. The bit is on nAutoFeed, inverted, MSB first. */
        dev->fw_byte = (dev->fw_byte << 1) | !(val & PE2_CTRL_AUTOFEED);
        if (++dev->fw_nbits == 8) {
            dev->fw_fnv   = (dev->fw_fnv ^ dev->fw_byte) * 16777619;
            dev->fw_nbits = 0;
        }

        dev->fw_bits++;
        /* PE2 sends one framing bit ahead of its image. PE1 does not, and the count
           has to be exact there: it latches its commands on the same nSelectIn edge
           the image is clocked with, so taking one clock too many swallows the first
           command it sends. PE2 latches on nAutoFeed instead and never notices. */
        if (dev->fw_bits >= ((dev->fw_len * 8) + !dev->is_pe1)) {
            dev->running = (dev->fw_len == PE1_FW_LEN) || /* PE1 has only the one image */
                           (dev->fw_fnv == PE2_FW_FNV_NIBBLE) ||
                           (dev->fw_fnv == PE2_FW_FNV_BIT2) ||
                           (dev->fw_fnv == PE2_FW_FNV_EPP);
            pe2_log(dev->log, "bitstream loaded, fnv=%08X, %s\n", dev->fw_fnv,
                    dev->running ? "transfer mode" : "probing");
            if (dev->running)
                dev->counting = 0; /* profiling is over */
            dev->fw_done = 1;
        }
    }

    return 1;
}

static void
pe2_write_ctrl(uint8_t val, void *priv)
{
    pe2_t *dev = (pe2_t *) priv;

    uint8_t prev = dev->ctrl;
    dev->ctrl    = val;

    if (pe2_firmware_write_ctrl(dev, prev, val))
        return;

    if (dev->is_pe1 && dev->running) {
        /* Command is latched as nSelectIn rises, with the byte already
           on the data lines; its operand is then strobed with nAutoFeed. */
        if (((prev ^ val) & PE2_CTRL_SELECT) && (val & PE2_CTRL_SELECT)) {
            pe2_command(dev, dev->data);
            return;
        }

        if ((prev ^ val) & PE2_CTRL_AUTOFEED) {
            if (dev->pe1_block) {
                /* Every nAutoFeed edge, either way, moves one byte. A read
                   presents the next byte as soon as the edge asking for
                   it lands, as the driver reads it with no further writes. */
                dp8390_t *dp = dev->dp8390;
                if (dp->CR.rdma_cmd == 2) {
                    dp8390_chipmem_write(dp, dp->remote_dma, dev->data, 1);
                    pe2_dma_advance(dev);
                } else if (dp->CR.rdma_cmd == 1) {
                    pe2_dma_advance(dev);
                    dev->status = dp8390_chipmem_read(dp, dp->remote_dma, 1);
                }
            } else if (dev->pe1_access && (val & PE2_CTRL_AUTOFEED)) {
                /* nStrobe held through the operand strobe makes it a read,
                   which loads the output byte the status lines show. Outside
                   a block, register 40 writes straight into that byte, a
                   loopback the driver's presence test reads back with no read
                   cycle at all. Only a write may load it that way, as the driver
                   parks the command byte or FF on the lines around a read. */
                dev->pe1_access = 0;
                if (val & PE2_CTRL_STROBE) {
                    if (dev->reg != 0x40)
                        dev->status = pe2_read_reg(dev, dev->reg & 0x0f);
                } else if (dev->reg == 0x40)
                    dev->status = dev->data;
                else
                    pe2_write_reg(dev, dev->reg & 0x0f, dev->data);
            }
        }

        if (val & PE2_CTRL_DIR) /* a byte-wide read takes it straight off the data lines */
            lpt_write_to_dat(dev->lpt, dev->status);
        return;
    }

    /* The driver reads a whole byte back by turning the port around and reading
       the data port, rather than through the status lines; four variants of that
       are what it picks its transfer mode from. Hand it the byte as it turns. */
    if (val & PE2_CTRL_DIR) {
        uint8_t v = dev->counting ? pe2_pulse_readback(dev)
                        : dev->running  ? dev->status /* already latched from the controller */
                                        : pe2_gate_read(dev, dev->reg);
        lpt_write_to_dat(dev->lpt, v);
    }

    if (dev->counting && (pe2_strobe_port[dev->mode] == 2)) {
        uint8_t bit = pe2_strobe_bit[dev->mode];
        if ((val & bit) && !(prev & bit))
            dev->pulses++;
        /* Lines 0A and 0D count nSelectIn, which is also the command
           latch. The driver raises nStrobe alongside it for a command
           and not for a pulse, so only the former may fall through. */
        if ((bit != PE2_CTRL_SELECT) || !(prev & PE2_CTRL_STROBE))
            return;
    }

    if ((dev->ramop == 2) && dev->running && (val & PE2_CTRL_DIR)) {
        /* Reading the buffer back is bidirectional: whole bytes
           off the data port, with nStrobe stepping to the next
           one. Only while the port is turned around, as the NT
           driver arms a dummy two-byte read before every remote
           write and never drains it; owning every control write
           from then on would swallow the commands that follow. */
        if ((prev ^ val) & PE2_CTRL_STROBE) {
            /* The transfer is as long as the remote byte count said:
               once it runs out, the operation disarms, so the
               driver's next command is not mistaken for another step. */
            if (dev->dp8390->remote_bytes <= 1) {
                dev->dp8390->ISR.rdma_done = 1;
                dev->ramop                 = 0;
                return;
            }
            pe2_dma_advance(dev);
        }
        lpt_write_to_dat(dev->lpt, dp8390_chipmem_read(dev->dp8390, dev->dp8390->remote_dma, 1));
        return;
    }

    if (dev->running) {
        /* A running transfer mode latches the command as nAutoFeed
           is asserted, with the byte already on the data lines, and
           then strobes the operand with nSelectIn; not by releasing
           nSelectIn the way PE2's own profiling phase and PE3 do it. */
        if (((prev ^ val) & PE2_CTRL_AUTOFEED) && (val & PE2_CTRL_AUTOFEED)) {
            pe2_command(dev, dev->data);
            return;
        }
        if (((prev ^ val) & PE2_CTRL_SELECT) && (val & PE2_CTRL_SELECT)) {
            if (dev->pend_write) {
                dev->pend_write = 0;
                if (dev->wr_reg)
                    pe2_write_reg(dev, dev->reg, dev->data);
                dev->wr_reg = 0;
            }
            return;
        }
        /* Anything else, nStrobe included, falls through to latch data below. */
    } else if ((prev ^ val) & PE2_CTRL_SELECT) { /* SelectIn = command */
        if (prev & PE2_CTRL_SELECT)
            pe2_command(dev, dev->data);
        return;
    }

    if (dev->pend_read && (val & PE2_CTRL_DIR)) { /* bidirectional read engaged and port is reading */
        if ((prev ^ val) & PE2_CTRL_STROBE) {
            dev->status   = pe2_gate_read(dev, dev->reg);
            dev->rd_bidir = 1;
            lpt_write_to_dat(dev->lpt, dev->status);
        }
        return;
    }

    if (dev->pend_read && dev->rd_bidir) /* bidirectional read engaged but port is not reading */
        return;

    if ((prev ^ val) & ~(PE2_CTRL_STROBE | PE2_CTRL_AUTOFEED | PE2_CTRL_INIT)) /* ignore changes to just direction or interrupt enable */
        return;

    if (dev->pend_read) { /* nibble read engaged */
        /* A running mode has already latched the byte from the controller;
           re-reading here would hand back the gate array's answer instead. */
        if (!(dev->rd_phase & 1) && !dev->running)
            dev->status = pe2_gate_read(dev, dev->reg);
        dev->rd_phase++;
    }

    if (!((prev ^ val) & PE2_CTRL_STROBE)) /* fixed strobe pin unlike PE3 */
        return;

    dev->pulses++;
    dev->latch = dev->data;

    /* FPGA register writes commit here, but we don't care about
       anything the driver puts there, so only the latch above matters. */
    dev->pend_write = 0;
}

static void
pe2_epp_write_data(uint8_t is_addr, uint8_t val, void *priv)
{
    pe2_t *dev = (pe2_t *) priv;

    if (!is_addr && dev->counting && (pe2_strobe_port[dev->mode] == 4)) {
        dev->pulses++; /* line 0E counts EPP data cycles, one per bus cycle */
        return;
    }

    if (is_addr) /* command on EPP address cycle */
        pe2_command(dev, val);
}

static void
pe2_epp_request_read(uint8_t is_addr, void *priv)
{
    pe2_t *dev = (pe2_t *) priv;

    lpt_write_to_dat(dev->lpt, is_addr ? 0xff : pe2_gate_read(dev, dev->reg)); /* nothing reads address back */
}

static uint8_t
pe2_read_status(void *priv)
{
    pe2_t *dev = (pe2_t *) priv;

    /* The port only passes the interrupt on while its enable is set,
       and the NT driver only opens that window around its critical
       sections, so a level raised once while it was shut would be lost.
       Reassert it here, after the port has taken the latest control write. */
    if (pe2_irq_level(dev))
        lpt_irq(dev->lpt, 1);

    /* PE1 reads a byte back as a nibble pair like everything else, but
       picks the half with data[7] instead of counting reads. nAck carries
       the EEPROM's data out, block transfer pacing (high when the next
       byte may move) or the interrupt; both halves of a read mask it off. */
    if (dev->is_pe1 && dev->running) {
        uint8_t v = dev->status;
        uint8_t ack;
        uint8_t ret;

        if (dev->ee)
            ack = dev->ee_bit;
        else if (dev->pe1_block)
            ack = 1;
        else
            ack = pe2_irq_level(dev);

        if (dev->data & 0x80) /* [7] [ ] [5] [4] [3] [ ] [ ] [ ] */
            ret = v & 0xb8;
        else /* [6] [ ] [2] [1] [0] [ ] [ ] [ ] */
            ret = ((v & 0x07) << 3) | ((v & 0x40) << 1);
        return ret | (ack ? 0x40 : 0x00);
    }

    if (dev->ee) /* the EEPROM's data out is status[3] */
        return dev->ee_bit ? 0x08 : 0x00;

    /* While profiling, the counter comes back as a plain
       nibble pair on status[6:3], half selected by data[7]. */
    if (dev->counting) {
        uint8_t v      = pe2_pulse_readback(dev);
        uint8_t nibble = (dev->data & 0x80) ? (v >> 4) : (v & 0x0f);
        return nibble << 3; /* [2:0] are the port's own, [7] is shifted out */
    }

    /* PE2 register read encoding. */
    uint8_t ret;
    if (dev->rd_phase & 1) /* [6] [ ] [2] [1] [0] [ ] [ ] [ ] */
        ret = ((dev->status & 0x07) << 3) | ((dev->status & 0x40) << 1);
    else /* [7] [ ] [5] [4] [3] [ ] [ ] [ ] */
        ret = (dev->status & 0x38) | (dev->status & 0x80);
    return ret | 0x40; /* ack left alone */
}

static void *
pe2_init(const device_t *info)
{
    pe2_t *dev = (pe2_t *) calloc(1, sizeof(pe2_t));

    dev->is_pe1 = (info->local == 1);
    dev->fw_len = dev->is_pe1 ? PE1_FW_LEN : PE2_FW_LEN;
    dev->log    = log_open(dev->is_pe1 ? "PE1" : "PE2");
    pe2_log(dev->log, "init()\n");

    dev->lpt = lpt_attach_ex(device_get_config_int("port"), pe2_write_data, pe2_write_ctrl,
                             NULL, pe2_read_status, NULL, pe2_epp_write_data,
                             pe2_epp_request_read, dev);
    if (!dev->lpt) {
        pe2_log(dev->log, "parallel port already claimed by another device\n");
        log_close(dev->log);
        free(dev);
        return NULL;
    }

    dev->mac[0] = 0x00; /* Xircom OUI, checked by drivers */
    dev->mac[1] = 0x80;
    dev->mac[2] = 0xc7;

    int mac = device_get_config_mac("mac", -1);
    if (mac & 0xff000000) {
        dev->mac[3] = random_generate();
        dev->mac[4] = random_generate();
        dev->mac[5] = random_generate();
        mac         = (((int) dev->mac[3]) << 16) | (((int) dev->mac[4]) << 8) | ((int) dev->mac[5]);
        device_set_config_mac("mac", mac);
    } else {
        dev->mac[3] = (mac >> 16) & 0xff;
        dev->mac[4] = (mac >> 8) & 0xff;
        dev->mac[5] = mac & 0xff;
    }

    memset(dev->eeprom_data, 0x00, sizeof(dev->eeprom_data));

    /* EEPROM words are read in reverse order, so the layout is a mess. */
    dev->eeprom_data[3]  = dev->mac[0] | (dev->mac[1] << 8);
    dev->eeprom_data[2]  = dev->mac[2] | (dev->mac[3] << 8);
    dev->eeprom_data[1]  = dev->mac[4] | (dev->mac[5] << 8);

    dev->eeprom_data[4]  = 0;             /* second */
    dev->eeprom_data[5]  = 9 | (50 << 8); /* hour and minute */
    dev->eeprom_data[6]  = 1995;          /* full year */
    dev->eeprom_data[7]  = 11 | (7 << 8); /* day and month */

    dev->eeprom_data[9]  = dev->mac[5] | (dev->mac[4] << 8); /* serial */
    dev->eeprom_data[8]  = dev->mac[3];

    if (dev->is_pe1) {
        dev->eeprom_data[15] = 'P' | ('E' << 8); /* PE10BT (twisted pair only model) */
        dev->eeprom_data[14] = '1' | ('0' << 8);
        dev->eeprom_data[13] = 'B' | ('T' << 8);
    } else {
        dev->eeprom_data[15] = 'P' | ('E' << 8); /* PE2-10BT (twisted pair only model) */
        dev->eeprom_data[14] = '2' | ('-' << 8);
        dev->eeprom_data[13] = '1' | ('0' << 8);
        dev->eeprom_data[12] = 'B' | ('T' << 8);
    }

    for (uint8_t i = 1; i < 16; i++) /* checksum */
        dev->eeprom_data[0] -= dev->eeprom_data[i];

    dev->dp8390            = device_add_inst(&dp8390_device, dp3890_inst++);
    dev->dp8390->priv      = dev;
    dev->dp8390->interrupt = pe2_interrupt;
    dp8390_set_defaults(dev->dp8390, DP8390_FLAG_CHECK_CR | DP8390_FLAG_CLEAR_IRQ | DP8390_FLAG_LOOP_TSR);
    dp8390_mem_alloc(dev->dp8390, 0x2000, 0x8000); /* memory test walks 2000 onwards */

    dev->card         = network_attach(dev->dp8390, dev->mac, dp8390_rx, NULL);
    dev->dp8390->card = dev->card;

    pe2_reset(dev);

    return dev;
}

static void
pe2_close(void *priv)
{
    pe2_t *dev = (pe2_t *) priv;

    pe2_log(dev->log, "close()\n");

    if (dev->card)
        netcard_close(dev->card);

    log_close(dev->log);

    free(dev);
}

// clang-format off
static const device_config_t pe2_config[] = {
    {
        .name           = "port",
        .description    = "Parallel Port",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "LPT1", .value = 0 },
            { .description = "LPT2", .value = 1 },
            { .description = "LPT3", .value = 2 },
            { .description = "LPT4", .value = 3 },
            { .description = ""                 }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "mac",
        .description    = "MAC Address",
        .type           = CONFIG_MAC,
        .default_string = NULL,
        .default_int    = -1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t pe1_device = {
    .name          = "Xircom Pocket Ethernet",
    .internal_name = "pe1",
    .flags         = DEVICE_LPT,
    .local         = 1,
    .init          = pe2_init,
    .close         = pe2_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pe2_config
};

const device_t pe2_device = {
    .name          = "Xircom Pocket Ethernet II",
    .internal_name = "pe2",
    .flags         = DEVICE_LPT,
    .local         = 2,
    .init          = pe2_init,
    .close         = pe2_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = pe2_config
};
