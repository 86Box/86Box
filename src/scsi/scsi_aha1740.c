/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Adaptec AHA-1740, 1740A, 1742A and 1744 EISA SCSI host
 *          adapters.
 *
 *          The card has two personalities. In standard mode it pretends
 *          to be an AHA-1542 at an ISA port and is driven by the mailbox
 *          and CCB scheme every 154x driver knows. In enhanced mode --
 *          which is the one every EISA-aware driver uses, and the only
 *          one Linux will touch -- it is a different machine: the host
 *          hands it the physical address of an Enhanced Control Block
 *          through a thirty-two bit mailbox port, and the card fetches
 *          the block, runs the command, writes a status block back into
 *          host memory and interrupts.
 *
 *          The whole register set lives inside the slot's own I/O, at
 *          the identifier and above, so the card occupies no ISA address
 *          at all in enhanced mode. That is the point of EISA: firmware
 *          reads the identifier, finds an ADP0000 in slot three, and
 *          knows where its registers are without anything being jumpered.
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
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/dma.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/plat.h>
#include <86box/eisa.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/rom.h>

#define AHA1740_V140_ROM "roms/scsi/adaptec/aha1740_v140.bin"
#include <86box/scsi_aha1740.h>
#include <86box/plat_unused.h>

#ifdef ENABLE_AHA1740_LOG
int aha1740_do_log = ENABLE_AHA1740_LOG;

static void
aha1740_log(const char *fmt, ...)
{
    va_list ap;

    if (aha1740_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define aha1740_log(fmt, ...)
#endif

/* Everything the card answers to sits at the slot's identifier and above,
   so these are offsets from zC80 exactly as the drivers write them. */
#define AHA_HID0     0x00 /* the four identifier bytes */
#define AHA_EBCNTRL  0x04
#define AHA_PORTADR  0x40
#define AHA_BIOSADR  0x41
#define AHA_INTDEF   0x42
#define AHA_SCSIDEF  0x43
#define AHA_BUSDEF   0x44
#define AHA_RESV0    0x45
#define AHA_RESV1    0x46
#define AHA_RESV2    0x47
#define AHA_MBOXOUT  0x50 /* four bytes, written as one long */
#define AHA_ATTN     0x54
#define AHA_G2CNTRL  0x55
#define AHA_G2INTST  0x56
#define AHA_G2STAT   0x57
#define AHA_MBOXIN   0x58 /* four bytes, read as one long */
#define AHA_G2STAT2  0x5c
#define AHA_SLOTSIZE 0x5c

/* PORTADR. Bit 7 says the card is in enhanced mode; the low bits choose
   the compatibility port it would answer at otherwise. */
#define PORTADDR_ENH 0x80

/* G2STAT */
#define G2STAT_MBXOUT  0x04 /* the outgoing mailbox is free */
#define G2STAT_INTPEND 0x02
#define G2STAT_BUSY    0x01

/* G2STAT2 */
#define G2STAT2_READY 0x01 /* the host has said it is ready */

/* ATTN. The low three bits are the target. */
#define ATTN_IMMED 0x10
#define ATTN_START 0x40
#define ATTN_ABORT 0x50

/* G2CNTRL */
#define G2CNTRL_HRST 0x80
#define G2CNTRL_IRST 0x40
#define G2CNTRL_HRDY 0x20

/* The high nibble of G2INTST says what happened. */
#define G2INTST_CCBGOOD  0x10
#define G2INTST_CCBRETRY 0x50
#define G2INTST_HARDFAIL 0x70
#define G2INTST_CMDGOOD  0xa0
#define G2INTST_CCBERROR 0xc0
#define G2INTST_ASNEVENT 0xd0
#define G2INTST_CMDERROR 0xe0

/* Enhanced Control Block, as the card reads it out of host memory. */
#define ECB_CMDW      0x00
#define ECB_FLAGS1    0x02
#define ECB_FLAGS2    0x04
#define ECB_DATAPTR   0x08
#define ECB_DATALEN   0x0c
#define ECB_STATUSPTR 0x10
#define ECB_LINKPTR   0x14
#define ECB_SENSEPTR  0x1c
#define ECB_SENSELEN  0x20
#define ECB_CDBLEN    0x21
#define ECB_DATACHECK 0x22
#define ECB_CDB       0x24
#define ECB_SIZE      0x30

/* Flag word one. */
#define F1_CNE 0x0001 /* this block is chained to the next */
#define F1_DI  0x0080 /* do not interrupt when it is done */
#define F1_SES 0x0400 /* an underrun is not an error */
#define F1_SG  0x1000 /* dataptr is a scatter-gather list */
#define F1_DSB 0x4000 /* do not write a status block */
#define F1_ARS 0x8000 /* fetch sense automatically on an error */

/* Flag word two. */
#define F2_LUN 0x0007
#define F2_TAG 0x0008
#define F2_TT  0x0030
#define F2_ND  0x0040 /* the target may not disconnect */
#define F2_DAT 0x0100
#define F2_DIR 0x0200 /* set means data in */
#define F2_ST  0x0400 /* suppress the transfer */
#define F2_CHK 0x0800
#define F2_REC 0x4000

/* Command word. */
#define CMD_NOP     0x00
#define CMD_INIT    0x01 /* an initiator SCSI command */
#define CMD_DIAG    0x05
#define CMD_SCSI    0x06
#define CMD_SENSE   0x08
#define CMD_DOWN    0x09
#define CMD_RINQ    0x0a
#define CMD_TARG    0x10

#define AHA_SCATTER 16

typedef struct aha1740_t {
    uint8_t slot;
    uint8_t bus;
    uint8_t irq;
    uint8_t irq_state;
    uint8_t board; /* which of the family this is */

    uint8_t regs[AHA_SLOTSIZE + 4]; /* the EISA configuration bytes */

    uint32_t mbox_out;
    uint32_t mbox_in;
    uint8_t  g2stat;
    uint8_t  g2intst;
    uint8_t  g2stat2;
    uint8_t  attn;

    uint8_t id;       /* the adapter's own SCSI address */
    uint8_t enhanced; /* PORTADR bit 7 */

    rom_t   bios;
    uint8_t has_bios;
    /* The last two kilobytes of the window are RAM the firmware keeps its
       working copy in, overlaying the ROM underneath. */
    uint8_t bios_ram[2048];
    uint8_t bios_rom_top[2048]; /* what the ROM has underneath it */

    pc_timer_t timer;
    uint32_t   pending_ecb;
    uint8_t    pending_target;
    uint8_t    busy;
} aha1740_t;

/* The identifier of each member of the family. The product number is what
   the EISA configuration file is named after. */
#define BOARD_1740  0
#define BOARD_1740A 1
#define BOARD_1742A 2
#define BOARD_1744  3

static const uint16_t aha1740_product[4] = { 0x0000, 0x0001, 0x0002, 0x0400 };

/* The interrupt the card may be configured for, as INTDEF selects it. */
static const uint8_t aha1740_intab[8] = { 9, 10, 11, 12, 0, 14, 15, 0 };

/* The overlay: the top two kilobytes of the window answer from RAM once
   RAMEN is set, and take writes while WRTPRT is clear. The rest of the
   window is the ROM. */
/* The manual's scan reads "RAMEN (BIOSADDR bit 6)", but the card's own
   option ROM sets bit 5 to switch the overlay in and clears it again when
   it gives up on it, so bit 5 is what this is. */
#define AHA_BIOS_RAMEN  0x20
#define AHA_BIOS_WRTPRT 0x80
#define AHA_BIOS_RAMOFF 0x3800

/* Put whichever of the two is showing into the image the rest of the
   emulator reads, so that the processor fetching code and a bus master
   fetching a command block both see the same thing. */
static void
aha1740_bios_overlay(aha1740_t *dev)
{
    if (!dev->has_bios)
        return;

    memcpy(&dev->bios.rom[AHA_BIOS_RAMOFF],
           (dev->regs[AHA_BIOSADR] & AHA_BIOS_RAMEN) ? dev->bios_ram
                                                     : dev->bios_rom_top,
           sizeof(dev->bios_ram));
}

static uint8_t
aha1740_bios_read(uint32_t addr, void *priv)
{
    const aha1740_t *dev = (const aha1740_t *) priv;

    return dev->bios.rom[(addr & 0x3fff) & dev->bios.mask];
}

static void
aha1740_bios_write(uint32_t addr, uint8_t val, void *priv)
{
    aha1740_t *dev = (aha1740_t *) priv;
    uint32_t   off = addr & 0x3fff;

    if ((off < AHA_BIOS_RAMOFF) || (dev->regs[AHA_BIOSADR] & AHA_BIOS_WRTPRT))
        return;

    dev->bios_ram[off - AHA_BIOS_RAMOFF] = val;
    if (dev->regs[AHA_BIOSADR] & AHA_BIOS_RAMEN)
        dev->bios.rom[off] = val;
}

static uint16_t
aha1740_bios_readw(uint32_t addr, void *priv)
{
    return aha1740_bios_read(addr, priv) | ((uint16_t) aha1740_bios_read(addr + 1, priv) << 8);
}

static uint32_t
aha1740_bios_readl(uint32_t addr, void *priv)
{
    return aha1740_bios_readw(addr, priv) | ((uint32_t) aha1740_bios_readw(addr + 2, priv) << 16);
}

static void
aha1740_bios_writew(uint32_t addr, uint16_t val, void *priv)
{
    aha1740_bios_write(addr, val & 0xff, priv);
    aha1740_bios_write(addr + 1, (val >> 8) & 0xff, priv);
}

static void
aha1740_bios_writel(uint32_t addr, uint32_t val, void *priv)
{
    aha1740_bios_writew(addr, val & 0xffff, priv);
    aha1740_bios_writew(addr + 2, (val >> 16) & 0xffff, priv);
}

/* BIOSADDR (zCC1): the low four bits are BIOSSEL, which picks a sixteen
   kilobyte boundary, and zero means the BIOS is not mapped at all. Bit 5
   is RAMEN and bit 7 WRTPRT, which belong to the two kilobytes of overlay
   RAM at the top of the window. Every one of them arrives from the
   firmware replaying the EISA configuration; none is chosen here. */
static uint32_t
aha1740_bios_base(const aha1740_t *dev)
{
    return 0xc0000 + (((uint32_t) dev->regs[AHA_BIOSADR] & 0x0f) << 14);
}

static void
aha1740_bios_remap(aha1740_t *dev)
{
    if (!dev->has_bios)
        return;

    if (dev->regs[AHA_BIOSADR] & 0x0f) {
        mem_mapping_set_addr(&dev->bios.mapping, aha1740_bios_base(dev), 0x4000);
        mem_mapping_enable(&dev->bios.mapping);
        aha1740_log("AHA1740: BIOS at %05x\n", aha1740_bios_base(dev));
    } else {
        mem_mapping_disable(&dev->bios.mapping);
        aha1740_log("AHA1740: BIOS off\n");
    }
}

static void
aha1740_irq(aha1740_t *dev, int set)
{
    if (dev->irq == 0)
        return;

    if (set) {
        picint(1 << dev->irq);
        dev->irq_state = 1;
    } else {
        picintc(1 << dev->irq);
        dev->irq_state = 0;
    }
}

static void
aha1740_interrupt(aha1740_t *dev, uint8_t code, uint32_t ecb)
{
    dev->mbox_in = ecb;
    dev->g2intst = code;
    dev->g2stat |= G2STAT_INTPEND;
    aha1740_irq(dev, 1);
}

/* Write the status block the way the driver's decoder reads it: byte zero
   and one are flags, byte two is the adapter's own error code, byte three
   is whatever the target said. */
static void
aha1740_status(uint32_t ptr, uint8_t f0, uint8_t f1, uint8_t code,
               uint8_t scsi_status)
{
    uint8_t status[4];

    if (ptr == 0)
        return;

    status[0] = f0;
    status[1] = f1;
    status[2] = code;
    status[3] = scsi_status;

    dma_bm_write(ptr, status, 4, 4);
}

/* How much data the block describes, and where. A scatter-gather block
   points at a list of address and length pairs and its own length is the
   length of that list, not of the transfer. */
static uint32_t
aha1740_datalen(const uint8_t *ecb, uint16_t flags1)
{
    uint32_t dataptr = *(uint32_t *) &ecb[ECB_DATAPTR];
    uint32_t datalen = *(uint32_t *) &ecb[ECB_DATALEN];
    uint32_t total   = 0;
    uint8_t  entry[8];

    if (!(flags1 & F1_SG))
        return datalen;

    for (uint32_t off = 0; off < datalen; off += 8) {
        if (off >= (AHA_SCATTER * 8))
            break;
        dma_bm_read(dataptr + off, entry, 8, 4);
        total += *(uint32_t *) &entry[4];
    }

    return total;
}

/* Move the data either way, walking the list if there is one. */
static void
aha1740_move(const uint8_t *ecb, uint16_t flags1, uint8_t *buf, uint32_t len,
             int to_host)
{
    uint32_t dataptr = *(uint32_t *) &ecb[ECB_DATAPTR];
    uint32_t datalen = *(uint32_t *) &ecb[ECB_DATALEN];
    uint32_t done    = 0;
    uint8_t  entry[8];

    if (len == 0)
        return;

    if (!(flags1 & F1_SG)) {
        if (to_host)
            dma_bm_write(dataptr, buf, len, 4);
        else
            dma_bm_read(dataptr, buf, len, 4);
        return;
    }

    for (uint32_t off = 0; (off < datalen) && (done < len); off += 8) {
        uint32_t piece;
        uint32_t addr;

        if (off >= (AHA_SCATTER * 8))
            break;

        dma_bm_read(dataptr + off, entry, 8, 4);
        addr  = *(uint32_t *) &entry[0];
        piece = *(uint32_t *) &entry[4];

        if (piece > (len - done))
            piece = len - done;
        if (piece == 0)
            continue;

        if (to_host)
            dma_bm_write(addr, buf + done, piece, 4);
        else
            dma_bm_read(addr, buf + done, piece, 4);

        done += piece;
    }
}

/* Run one control block. */
static void
aha1740_run_ecb(aha1740_t *dev, uint32_t addr, uint8_t target)
{
    uint8_t        ecb[ECB_SIZE];
    uint8_t        cdb[16];
    scsi_device_t *sd;
    uint16_t       cmdw;
    uint16_t       flags1;
    uint16_t       flags2;
    uint32_t       statusptr;
    uint32_t       senseptr;
    uint32_t       want;
    uint8_t        lun;
    uint8_t        cdblen;
    uint8_t        senselen;

    dma_bm_read(addr, ecb, ECB_SIZE, 4);

    cmdw      = *(uint16_t *) &ecb[ECB_CMDW];
    flags1    = *(uint16_t *) &ecb[ECB_FLAGS1];
    flags2    = *(uint16_t *) &ecb[ECB_FLAGS2];
    statusptr = *(uint32_t *) &ecb[ECB_STATUSPTR];
    senseptr  = *(uint32_t *) &ecb[ECB_SENSEPTR];
    senselen  = ecb[ECB_SENSELEN];
    cdblen    = ecb[ECB_CDBLEN];
    lun       = (uint8_t) (flags2 & F2_LUN);

    if (cdblen > 16)
        cdblen = 16;
    memset(cdb, 0, sizeof(cdb));
    memcpy(cdb, &ecb[ECB_CDB], cdblen);

    aha1740_log("AHA1740: ecb %08x cmd %02x target %i lun %i cdb %02x len %i\n",
                addr, cmdw, target, lun, cdb[0], cdblen);

    /* Anything that is not an initiator command is answered without
       going near the bus. The drivers do not issue them, but the card
       does have them and a diagnostic might. */
    if (cmdw != CMD_INIT) {
        if (flags1 & F1_DSB)
            aha1740_status(statusptr, 0x01, 0x00, 0x00, 0x00);
        else
            aha1740_status(statusptr, 0x81, 0x00, 0x00, 0x00);
        aha1740_interrupt(dev, G2INTST_CCBGOOD, addr);
        return;
    }

    if (target > 15) {
        aha1740_status(statusptr, 0x00, 0x18, 0x0a, 0x00);
        aha1740_interrupt(dev, G2INTST_CCBERROR, addr);
        return;
    }

    sd = &scsi_devices[dev->bus][target];

    if (!scsi_device_present(sd) || (lun > 0)) {
        /* Nothing there: selection timed out, which the decoder reads as
           a bad target. */
        aha1740_status(statusptr, 0x00, 0x18, 0x0a, 0x00);
        aha1740_interrupt(dev, G2INTST_CCBERROR, addr);
        return;
    }

    scsi_device_identify(sd, lun);
    sd->buffer_length = -1;
    scsi_device_command_phase0(sd, cdb);

    want = aha1740_datalen(ecb, flags1);

    if ((sd->phase == SCSI_PHASE_DATA_IN) && (sd->buffer_length > 0)) {
        uint32_t len = (uint32_t) sd->buffer_length;

        if (len > want)
            len = want;
        if ((len > 0) && !(flags2 & F2_ST))
            aha1740_move(ecb, flags1, sd->sc->temp_buffer, len, 1);
        scsi_device_command_phase1(sd);
    } else if ((sd->phase == SCSI_PHASE_DATA_OUT) && (sd->buffer_length > 0)) {
        uint32_t len = (uint32_t) sd->buffer_length;

        if (len > want)
            len = want;
        if ((len > 0) && !(flags2 & F2_ST))
            aha1740_move(ecb, flags1, sd->sc->temp_buffer, len, 0);
        scsi_device_command_phase1(sd);
    }

    scsi_device_identify(sd, SCSI_LUN_USE_CDB);

    if (sd->status == SCSI_STATUS_OK) {
        /* Done, no error. Bit zero of the first status byte is what the
           driver looks at before anything else. */
        if (!(flags1 & F1_DSB))
            aha1740_status(statusptr, 0x81, 0x00, 0x00, 0x00);
        aha1740_interrupt(dev, G2INTST_CCBGOOD, addr);
        return;
    }

    /* Something to report. The card fetches the sense itself when asked
       to, which is why the driver never issues REQUEST SENSE. */
    if ((flags1 & F1_ARS) && (senseptr != 0) && (senselen > 0)) {
        uint8_t sense[18];
        uint8_t len = senselen;

        if (len > sizeof(sense))
            len = sizeof(sense);
        memset(sense, 0, sizeof(sense));
        scsi_device_request_sense(sd, sense, len);
        dma_bm_write(senseptr, sense, len, 4);
    }

    if (!(flags1 & F1_DSB)) {
        /* Not done; additional status available and sense stored; no
           adapter level fault, so the target's own status is what
           matters. */
        aha1740_status(statusptr, 0x00, 0x03, 0x00, sd->status);
    }
    aha1740_interrupt(dev, G2INTST_CCBERROR, addr);
}

/* The card takes a moment over a command, which is also what keeps the
   host's wait loops from being entered with everything already done. */
static void
aha1740_callback(void *priv)
{
    aha1740_t *dev = (aha1740_t *) priv;

    dev->busy = 0;
    dev->g2stat &= ~G2STAT_BUSY;
    dev->g2stat |= G2STAT_MBXOUT;

    if (dev->pending_ecb != 0) {
        uint32_t ecb    = dev->pending_ecb;
        uint8_t  target = dev->pending_target;

        dev->pending_ecb = 0;
        aha1740_run_ecb(dev, ecb, target);
    }
}

static void
aha1740_reset_card(aha1740_t *dev)
{
    dev->mbox_out    = 0;
    dev->mbox_in     = 0;
    dev->g2intst     = 0;
    dev->g2stat      = G2STAT_MBXOUT;
    dev->g2stat2     = 0;
    dev->attn        = 0;
    dev->pending_ecb = 0;
    dev->busy        = 0;

    aha1740_irq(dev, 0);
    timer_disable(&dev->timer);
}

static uint8_t
aha1740_read(uint16_t port, void *priv)
{
    aha1740_t *dev = (aha1740_t *) priv;
    uint16_t   off = (uint16_t) ((port & 0x0fff) - 0x0c80);

    switch (off) {
        case AHA_HID0:
        case AHA_HID0 + 1:
        case AHA_HID0 + 2:
        case AHA_HID0 + 3:
            /* The bus serves these; if the card is asked directly it
               answers the same thing. */
            return eisa_slot_id(dev->slot, (uint8_t) off);

        case AHA_G2STAT:
            return dev->g2stat;
        case AHA_G2INTST:
            return dev->g2intst;
        case AHA_G2STAT2:
            return dev->g2stat2;

        case AHA_MBOXIN:
            return (uint8_t) (dev->mbox_in & 0xff);
        case AHA_MBOXIN + 1:
            return (uint8_t) ((dev->mbox_in >> 8) & 0xff);
        case AHA_MBOXIN + 2:
            return (uint8_t) ((dev->mbox_in >> 16) & 0xff);
        case AHA_MBOXIN + 3:
            return (uint8_t) ((dev->mbox_in >> 24) & 0xff);

        case AHA_MBOXOUT:
            return (uint8_t) (dev->mbox_out & 0xff);
        case AHA_MBOXOUT + 1:
            return (uint8_t) ((dev->mbox_out >> 8) & 0xff);
        case AHA_MBOXOUT + 2:
            return (uint8_t) ((dev->mbox_out >> 16) & 0xff);
        case AHA_MBOXOUT + 3:
            return (uint8_t) ((dev->mbox_out >> 24) & 0xff);

        case AHA_ATTN:
            return dev->attn;

        default:
            if (off < sizeof(dev->regs))
                return dev->regs[off];
            return 0xff;
    }
}

static void
aha1740_write(uint16_t port, uint8_t val, void *priv)
{
    aha1740_t *dev = (aha1740_t *) priv;
    uint16_t   off = (uint16_t) ((port & 0x0fff) - 0x0c80);

    switch (off) {
        case AHA_MBOXOUT:
            dev->mbox_out = (dev->mbox_out & 0xffffff00) | val;
            break;
        case AHA_MBOXOUT + 1:
            dev->mbox_out = (dev->mbox_out & 0xffff00ff) | ((uint32_t) val << 8);
            break;
        case AHA_MBOXOUT + 2:
            dev->mbox_out = (dev->mbox_out & 0xff00ffff) | ((uint32_t) val << 16);
            break;
        case AHA_MBOXOUT + 3:
            dev->mbox_out = (dev->mbox_out & 0x00ffffff) | ((uint32_t) val << 24);
            /* The whole address has arrived; the mailbox is no longer
               free until the card has taken it. */
            dev->g2stat &= ~G2STAT_MBXOUT;
            break;

        case AHA_ATTN:
            dev->attn = val;
            switch (val & 0xf0) {
                case ATTN_START:
                    dev->pending_ecb    = dev->mbox_out;
                    dev->pending_target = (uint8_t) (val & 0x0f);
                    dev->busy           = 1;
                    dev->g2stat |= G2STAT_BUSY;
                    timer_set_delay_u64(&dev->timer, 10ULL * TIMER_USEC);
                    break;

                case ATTN_ABORT:
                    /* Nothing is ever outstanding long enough here for an
                       abort to find it, so it is answered as complete. */
                    aha1740_interrupt(dev, G2INTST_CCBGOOD, dev->mbox_out);
                    dev->g2stat |= G2STAT_MBXOUT;
                    break;

                case ATTN_IMMED:
                    aha1740_interrupt(dev, G2INTST_CMDGOOD, dev->mbox_out);
                    dev->g2stat |= G2STAT_MBXOUT;
                    break;

                default:
                    break;
            }
            break;

        case AHA_G2CNTRL:
            if (val & G2CNTRL_HRST)
                aha1740_reset_card(dev);
            if (val & G2CNTRL_IRST) {
                dev->g2stat &= ~G2STAT_INTPEND;
                dev->g2intst = 0;
                aha1740_irq(dev, 0);
            }
            if (val & G2CNTRL_HRDY)
                dev->g2stat2 |= G2STAT2_READY;
            break;

        case AHA_PORTADR:
            dev->regs[off] = val;
            dev->enhanced  = !!(val & PORTADDR_ENH);
            break;

        case AHA_BIOSADR:
            dev->regs[off] = val;
            aha1740_bios_overlay(dev);
            aha1740_bios_remap(dev);
            break;

        case AHA_INTDEF:
            dev->regs[off] = val;
            dev->irq       = aha1740_intab[val & 0x07];
            break;

        case AHA_SCSIDEF:
            dev->regs[off] = val;
            dev->id        = (uint8_t) (val & 0x0f);
            break;

        case AHA_G2STAT:
        case AHA_G2INTST:
        case AHA_G2STAT2:
        case AHA_HID0:
        case AHA_HID0 + 1:
        case AHA_HID0 + 2:
        case AHA_HID0 + 3:
            break;

        default:
            if (off < sizeof(dev->regs))
                dev->regs[off] = val;
            break;
    }
}

static void
aha1740_reset(void *priv)
{
    aha1740_t *dev = (aha1740_t *) priv;

    /* A board comes out of reset unconfigured. Which interrupt it drives,
       where its option ROM answers, which address its ports are at and
       what its own SCSI address is are all written into the registers
       below by the system firmware, replaying what the configuration
       utility worked out and left in the EISA store. Nothing here decides
       any of it. */
    memset(dev->regs, 0, sizeof(dev->regs));

    dev->enhanced = 0;
    dev->irq      = 0;
    dev->id       = 7;

    aha1740_bios_remap(dev);
    aha1740_reset_card(dev);
}

static void *
aha1740_init(const device_t *info)
{
    aha1740_t  *dev = (aha1740_t *) calloc(1, sizeof(aha1740_t));
    const char *rev;
    const char *fn;
    uint8_t     id[4];
    uint8_t     slot;

    dev->board = (uint8_t) (info->local & 0xff);
    dev->bus   = scsi_get_bus();

    slot = (uint8_t) device_get_config_int("slot");
    if (slot == 0)
        slot = 1;
    dev->slot = slot;

    aha1740_reset(dev);

    eisa_make_id(id, "ADP", aha1740_product[dev->board], 0);

    if (!eisa_add(dev->slot, id, aha1740_read, aha1740_write, aha1740_reset,
                  dev)) {
        free(dev);
        return NULL;
    }

    /* The BIOS is a part soldered to the board, so it is always read in.
       Whether it answers anywhere, and at which address, is BIOSADDR's
       business, and that register is written by the firmware from what
       the configuration utility stored. Out of reset it reads zero and
       the window stays closed. */
    rev = device_get_config_bios("bios_rev");
    fn  = device_get_bios_file(info, rev, 0);

    if ((fn != NULL) && (fn[0] != '\0')) {
        if (rom_init(&dev->bios, fn, aha1740_bios_base(dev), 0x4000,
                     0x3fff, 0, MEM_MAPPING_EXTERNAL)
            >= 0) {
            dev->has_bios = 1;
            mem_mapping_set_handler(&dev->bios.mapping,
                                    aha1740_bios_read, aha1740_bios_readw,
                                    aha1740_bios_readl, aha1740_bios_write,
                                    aha1740_bios_writew, aha1740_bios_writel);
            mem_mapping_set_p(&dev->bios.mapping, dev);
            memcpy(dev->bios_rom_top, &dev->bios.rom[AHA_BIOS_RAMOFF],
                   sizeof(dev->bios_rom_top));
            aha1740_bios_remap(dev);
        } else
            aha1740_log("AHA1740: could not read %s\n", fn);
    }

    timer_add(&dev->timer, aha1740_callback, dev, 0);

    aha1740_log("AHA1740: slot %i, id %02x%02x%02x%02x\n", dev->slot, id[0],
                id[1], id[2], id[3]);

    return dev;
}

static void
aha1740_close(void *priv)
{
    aha1740_t *dev = (aha1740_t *) priv;

    if (dev == NULL)
        return;

    eisa_remove(dev->slot);
    free(dev);
}

static const device_config_t aha1740_config[] = {
    // clang-format off
    {
        .name           = "bios_rev",
        .description    = "BIOS Revision",
        .type           = CONFIG_BIOS,
        .default_string = "v1_40",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .bios           = {
            {
                .name          = "Version 1.40",
                .internal_name = "v1_40",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 16384,
                .files         = { AHA1740_V140_ROM, "" }
            },
            { .files_no = 0 }
        }
    },
    {
        .name           = "slot",
        .description    = "EISA slot",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Slot 1", .value = 1 },
            { .description = "Slot 2", .value = 2 },
            { .description = "Slot 3", .value = 3 },
            { .description = "Slot 4", .value = 4 },
            { .description = ""                   }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t aha1740_device = {
    .name          = "Adaptec AHA-1740",
    .internal_name = "aha1740",
    .flags         = DEVICE_EISA,
    .local         = BOARD_1740,
    .init          = aha1740_init,
    .close         = aha1740_close,
    .reset         = aha1740_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = aha1740_config,
    .short_name    = "AHA-1740"
};

const device_t aha1740a_device = {
    .name          = "Adaptec AHA-1740A",
    .internal_name = "aha1740a",
    .flags         = DEVICE_EISA,
    .local         = BOARD_1740A,
    .init          = aha1740_init,
    .close         = aha1740_close,
    .reset         = aha1740_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = aha1740_config,
    .short_name    = "AHA-1740A"
};

const device_t aha1742a_device = {
    .name          = "Adaptec AHA-1742A",
    .internal_name = "aha1742a",
    .flags         = DEVICE_EISA,
    .local         = BOARD_1742A,
    .init          = aha1740_init,
    .close         = aha1740_close,
    .reset         = aha1740_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = aha1740_config,
    .short_name    = "AHA-1742A"
};

const device_t aha1744_device = {
    .name          = "Adaptec AHA-1744",
    .internal_name = "aha1744",
    .flags         = DEVICE_EISA,
    .local         = BOARD_1744,
    .init          = aha1740_init,
    .close         = aha1740_close,
    .reset         = aha1740_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = aha1740_config,
    .short_name    = "AHA-1744"
};
