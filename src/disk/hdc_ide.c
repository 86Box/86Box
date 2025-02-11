/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the IDE emulation for hard disks and ATAPI
 *          CD-ROM devices.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2016-2020 Miran Grca.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/machine.h>
#include <86box/io.h>
#include <86box/mca.h>
#include <86box/mem.h>
#include <86box/pic.h>
#include <86box/rom.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/scsi_device.h>
#include <86box/isapnp.h>
#include <86box/cdrom.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/hdd.h>
#include <86box/zip.h>
#include <86box/version.h>

/* Bits of 'atastat' */
#define ERR_STAT     0x01 /* Error */
#define IDX_STAT     0x02 /* Index */
#define CORR_STAT    0x04 /* Corrected data */
#define DRQ_STAT     0x08 /* Data request */
#define DSC_STAT     0x10 /* Drive seek complete */
#define SERVICE_STAT 0x10 /* ATAPI service */
#define DWF_STAT     0x20 /* Drive write fault */
#define DRDY_STAT    0x40 /* Ready */
#define BSY_STAT     0x80 /* Busy */

/* Bits of 'error' */
#define AMNF_ERR  0x01 /* Address mark not found */
#define TK0NF_ERR 0x02 /* Track 0 not found */
#define ABRT_ERR  0x04 /* Command aborted */
#define MCR_ERR   0x08 /* Media change request */
#define IDNF_ERR  0x10 /* Sector ID not found */
#define MC_ERR    0x20 /* Media change */
#define UNC_ERR   0x40 /* Uncorrectable data error */
#define BBK_ERR   0x80 /* Bad block mark detected */

/* ATA Commands */
#define WIN_NOP                        0x00
#define WIN_SRST                       0x08 /* ATAPI Device Reset */
#define WIN_RECAL                      0x10
#define WIN_READ                       0x20 /* 28-Bit Read */
#define WIN_READ_NORETRY               0x21 /* 28-Bit Read - no retry */
#define WIN_WRITE                      0x30 /* 28-Bit Write */
#define WIN_WRITE_NORETRY              0x31 /* 28-Bit Write - no retry */
#define WIN_VERIFY                     0x40 /* 28-Bit Verify */
#define WIN_VERIFY_ONCE                0x41 /* Added by OBattler - deprected older ATA command, according to the specification I found, it is identical to 0x40 */
#define WIN_FORMAT                     0x50
#define WIN_SEEK                       0x70
#define WIN_DRIVE_DIAGNOSTICS          0x90 /* Execute Drive Diagnostics */
#define WIN_SPECIFY                    0x91 /* Initialize Drive Parameters */
#define WIN_PACKETCMD                  0xa0 /* Send a packet command. */
#define WIN_PIDENTIFY                  0xa1 /* Identify ATAPI device */
#define WIN_READ_MULTIPLE              0xc4
#define WIN_WRITE_MULTIPLE             0xc5
#define WIN_SET_MULTIPLE_MODE          0xc6
#define WIN_READ_DMA                   0xc8
#define WIN_READ_DMA_ALT               0xc9
#define WIN_WRITE_DMA                  0xca
#define WIN_WRITE_DMA_ALT              0xcb
#define WIN_STANDBYNOW1                0xe0
#define WIN_IDLENOW1                   0xe1
#define WIN_SETIDLE1                   0xe3
#define WIN_CHECKPOWERMODE1            0xe5
#define WIN_SLEEP1                     0xe6
#define WIN_IDENTIFY                   0xec /* Ask drive to identify itself */
#define WIN_SET_FEATURES               0xef
#define WIN_READ_NATIVE_MAX            0xf8

#define FEATURE_SET_TRANSFER_MODE      0x03
#define FEATURE_ENABLE_IRQ_OVERLAPPED  0x5d
#define FEATURE_ENABLE_IRQ_SERVICE     0x5e
#define FEATURE_DISABLE_REVERT         0x66
#define FEATURE_ENABLE_REVERT          0xcc
#define FEATURE_DISABLE_IRQ_OVERLAPPED 0xdd
#define FEATURE_DISABLE_IRQ_SERVICE    0xde

#define IDE_TIME                       10.0

#define IDE_ATAPI_IS_EARLY             ide->sc->pad0

#define ROM_PATH_MCIDE                 "roms/hdd/xtide/ide_ps2 R1.1.bin"

typedef struct ide_bm_t {
    int (*dma)(uint8_t *data, int transfer_length, int out, void *priv);
    void (*set_irq)(uint8_t status, void *priv);
    void *priv;
} ide_bm_t;

typedef struct ide_board_t {
    uint8_t    devctl;
    uint8_t    pad;
    uint16_t   base[2];
    int        bit32;
    int        cur_dev;
    int        irq;
    int        inited;
    int        diag;
    int        force_ata3;

    pc_timer_t timer;

    ide_t     *ide[2];
    ide_bm_t  *bm;
} ide_board_t;

typedef struct mcide_t {
    uint8_t    pos_regs[8];
    uint32_t   bios_addr;
    rom_t bios_rom;
} mcide_t;

ide_board_t *ide_boards[IDE_BUS_MAX] = { 0 };

static uint8_t ide_ter_pnp_rom[] = {
    /* BOX0001, serial 0, dummy checksum (filled in by isapnp_add_card) */
    0x09, 0xf8, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* PnP version 1.0, vendor version 1.0 */
    0x0a, 0x10, 0x10,
    /* ANSI identifier */
    0x82, 0x0e, 0x00, 'I', 'D', 'E', ' ', 'C', 'o', 'n', 't', 'r', 'o',
                      'l', 'l', 'e', 'r',

    /* Logical device BOX0001 */
    0x15, 0x09, 0xf8, 0x00, 0x01, 0x00,
    /* Compatible device PNP0600 */
    0x1c, 0x41, 0xd0, 0x06, 0x00,
    /* Start dependent functions, preferred */
    0x31, 0x00,
    /* IRQ 11 */
    0x22, 0x00, 0x08,
    /* I/O 0x1E8, decodes 16-bit, 1-byte alignment, 8 addresses */
    0x47, 0x01, 0xe8, 0x01, 0xe8, 0x01, 0x01, 0x08,
    /* I/O 0x3EE, decodes 16-bit, 1-byte alignment, 1 address */
    0x47, 0x01, 0xee, 0x03, 0xee, 0x03, 0x01, 0x01,
   /* Start dependent functions, acceptable */
    0x30,
    /* IRQ 3/4/5/7/9/10/11/12 */
    0x22, 0xb8, 0x1e,
    /* I/O 0x1E8, decodes 16-bit, 1-byte alignment, 8 addresses */
    0x47, 0x01, 0xe8, 0x01, 0xe8, 0x01, 0x01, 0x08,
    /* I/O 0x3EE, decodes 16-bit, 1-byte alignment, 1 address */
    0x47, 0x01, 0xee, 0x03, 0xee, 0x03, 0x01, 0x01,
    /* Start dependent functions, acceptable */
    0x30,
    /* IRQ 3/4/5/7/9/10/11/12 */
    0x22, 0xb8, 0x1e,
    /* I/O 0x100-0xFFF8, decodes 16-bit, 8-byte alignment, 8 addresses */
    0x47, 0x01, 0x00, 0x01, 0xf8, 0xff, 0x08, 0x08,
    /* I/O 0x100-0xFFFF, decodes 16-bit, 1-byte alignment, 1 address */
    0x47, 0x01, 0x00, 0x01, 0xff, 0xff, 0x01, 0x01,
    /* End dependent functions */
    0x38,

    /* End tag, dummy checksum (filled in by isapnp_add_card) */
    0x79, 0x00
};
static uint8_t ide_qua_pnp_rom[] = {
    /* BOX0001, serial 1, dummy checksum (filled in by isapnp_add_card) */
    0x09, 0xf8, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00,
    /* PnP version 1.0, vendor version 1.0 */
    0x0a, 0x10, 0x10,
    /* ANSI identifier */
    0x82, 0x0e, 0x00, 'I', 'D', 'E', ' ', 'C', 'o', 'n', 't', 'r', 'o',
                      'l', 'l', 'e', 'r',

    /* Logical device BOX0001 */
    0x15, 0x09, 0xf8, 0x00, 0x01, 0x00,
    /* Compatible device PNP0600 */
    0x1c, 0x41, 0xd0, 0x06, 0x00,
    /* Start dependent functions, preferred */
    0x31, 0x00,
    /* IRQ 10 */
    0x22, 0x00, 0x04,
    /* I/O 0x168, decodes 16-bit, 1-byte alignment, 8 addresses */
    0x47, 0x01, 0x68, 0x01, 0x68, 0x01, 0x01, 0x08,
    /* I/O 0x36E, decodes 16-bit, 1-byte alignment, 1 address */
    0x47, 0x01, 0x6e, 0x03, 0x6e, 0x03, 0x01, 0x01,
    /* Start dependent functions, acceptable */
    0x30,
    /* IRQ 3/4/5/7/9/10/11/12 */
    0x22, 0xb8, 0x1e,
    /* I/O 0x168, decodes 16-bit, 1-byte alignment, 8 addresses */
    0x47, 0x01, 0x68, 0x01, 0x68, 0x01, 0x01, 0x08,
    /* I/O 0x36E, decodes 16-bit, 1-byte alignment, 1 address */
    0x47, 0x01, 0x6e, 0x03, 0x6e, 0x03, 0x01, 0x01,
    /* Start dependent functions, acceptable */
    0x30,
    /* IRQ 3/4/5/7/9/10/11/12 */
    0x22, 0xb8, 0x1e,
    /* I/O 0x100-0xFFF8, decodes 16-bit, 8-byte alignment, 8 addresses */
    0x47, 0x01, 0x00, 0x01, 0xf8, 0xff, 0x08, 0x08,
    /* I/O 0x100-0xFFFF, decodes 16-bit, 1-byte alignment, 1 address */
    0x47, 0x01, 0x00, 0x01, 0xff, 0xff, 0x01, 0x01,
    /* End dependent functions */
    0x38,

    /* End tag, dummy checksum (filled in by isapnp_add_card) */
    0x79, 0x00
};

ide_t *ide_drives[IDE_NUM];
int    ide_ter_enabled = 0;
int    ide_qua_enabled = 0;

static void ide_atapi_callback(ide_t *ide);
static void ide_callback(void *priv);

#ifdef ENABLE_IDE_LOG
int ide_do_log = ENABLE_IDE_LOG;

static void
ide_log(const char *fmt, ...)
{
    va_list ap;

    if (ide_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ide_log(fmt, ...)
#endif

uint8_t
getstat(ide_t *ide)
{
    return ide->tf->atastat;
}

ide_t *
ide_get_drive(int ch)
{
    if (ch >= 8)
        return NULL;

    return ide_drives[ch];
}

double
ide_get_xfer_time(ide_t *ide, int size)
{
    double period = (10.0 / 3.0);

    /* We assume that 1 MB = 1000000 B in this case, so we have as
       many B/us as there are MB/s because 1 s = 1000000 us. */
    switch (ide->mdma_mode & 0x300) {
        case 0x000: /* PIO */
            switch (ide->mdma_mode & 0xff) {
                case 0x01:
                    period = (10.0 / 3.0);
                    break;
                case 0x02:
                    period = (20.0 / 3.83);
                    break;
                case 0x04:
                    period = (25.0 / 3.0);
                    break;
                case 0x08:
                    period = (100.0 / 9.0);
                    break;
                case 0x10:
                    period = (50.0 / 3.0);
                    break;

                default:
                    break;
            }
            break;
        case 0x100: /* Single Word DMA */
            switch (ide->mdma_mode & 0xff) {
                case 0x01:
                    period = (25.0 / 12.0);
                    break;
                case 0x02:
                    period = (25.0 / 6.0);
                    break;
                case 0x04:
                    period = (25.0 / 3.0);
                    break;

                default:
                    break;
            }
            break;
        case 0x200: /* Multiword DMA */
            switch (ide->mdma_mode & 0xff) {
                case 0x01:
                    period = (25.0 / 6.0);
                    break;
                case 0x02:
                    period = (40.0 / 3.0);
                    break;
                case 0x04:
                    period = (50.0 / 3.0);
                    break;

                default:
                    break;
            }
            break;
        case 0x300: /* Ultra DMA */
            switch (ide->mdma_mode & 0xff) {
                case 0x01:
                    period = (50.0 / 3.0);
                    break;
                case 0x02:
                    period = 25.0;
                    break;
                case 0x04:
                    period = (100.0 / 3.0);
                    break;
                case 0x08:
                    period = (400.0 / 9.0);
                    break;
                case 0x10:
                    period = (200.0 / 3.0);
                    break;
                case 0x20:
                    period = 100.0;
                    break;

                default:
                    break;
            }
            break;

        default:
            break;
    }

    period = (1.0 / period);         /* get us for 1 byte */
    return period * ((double) size); /* multiply by bytes to get period for the entire transfer */
}

double
ide_atapi_get_period(uint8_t channel)
{
    ide_t *ide = ide_drives[channel];

    ide_log("ide_atapi_get_period(%i)\n", channel);

    if (ide == NULL) {
        ide_log("Get period failed\n");
        return -1.0;
    }

    return ide_get_xfer_time(ide, 1);
}

static void
ide_irq_update(ide_board_t *dev, UNUSED(int log))
{
    ide_t *ide;
    uint8_t set;

    if (dev == NULL)
        return;

#ifdef ENABLE_IDE_LOG
    if (log)
        ide_log("IDE %i: IRQ update (%i)\n", dev->cur_dev >> 1, dev->irq);
#endif

    ide = ide_drives[dev->cur_dev];
    set = !(ide_boards[ide->board]->devctl & 2) && ide->irqstat;

    if (!dev->force_ata3 && dev->bm && dev->bm->set_irq)
        dev->bm->set_irq(set << 2, dev->bm->priv);
    else if (ide_boards[ide->board]->irq != -1)
        picint_common(1 << dev->irq, PIC_IRQ_EDGE, set, NULL);
}

void
ide_irq(ide_t *ide, int set, int log)
{
    if (ide_boards[ide->board] == NULL)
        return;

#if defined(ENABLE_IDE_LOG) && (ENABLE_IDE_LOG == 2)
    ide_log("IDE %i: IRQ %s\n", ide->channel, set ? "raise" : "lower");
#endif

    ide->irqstat = set;

    if (set)
        ide->service = 1;

    if (ide->selected)
        ide_irq_update(ide_boards[ide->board], log);
}

/**
 * Copy a string into a buffer, padding with spaces, and placing characters as
 * if they were packed into 16-bit values, stored little-endian.
 *
 * @param str Destination buffer
 * @param src Source string
 * @param len Length of destination buffer to fill in. Strings shorter than
 *            this length will be padded with spaces.
 */
void
ide_padstr(char *str, const char *src, const int len)
{
    int v;

    for (int i = 0; i < len; i++) {
        if (*src != '\0')
            v = *src++;
        else
            v = ' ';
        str[i ^ 1] = v;
    }
}

/**
 * Copy a string into a buffer, padding with spaces. Does not add string
 * terminator.
 *
 * @param buf      Destination buffer
 * @param buf_size Size of destination buffer to fill in. Strings shorter than
 *                 this length will be padded with spaces.
 * @param src      Source string
 */
void
ide_padstr8(uint8_t *buf, int buf_size, const char *src)
{
    for (int i = 0; i < buf_size; i++) {
        if (*src != '\0')
            buf[i] = *src++;
        else
            buf[i] = ' ';
    }
}

static int
ide_is_ata4(const ide_board_t *board)
{
    const ide_bm_t *bm = board->bm;

    return (!board->force_ata3 && (bm != NULL));
}

static int
ide_get_max(const ide_t *ide, const int type)
{
    const int       ata_4     = ide_is_ata4(ide_boards[ide->board]);
    const int       max[2][4] = { { 0, -1, -1, -1 }, { 4, 2, 2, 5 } };
    int             ret;

    if (ide->type == IDE_ATAPI)
        ret = ide->get_max(ide, !IDE_ATAPI_IS_EARLY && ata_4, type);
    else
        ret = max[ata_4][type];

    return ret;
}

static int
ide_get_timings(const ide_t *ide, const int type)
{
    const int       ata_4         = ide_is_ata4(ide_boards[ide->board]);
    const int       timings[2][3] = { { 0, 0, 0 }, { 120, 120, 0 } };
    int             ret;

    if (ide->type == IDE_ATAPI)
        ret = ide->get_timings(ide, !IDE_ATAPI_IS_EARLY && ata_4, type);
    else
        ret = timings[ata_4][type];

    return ret;
}

/**
 * Fill in ide->buffer with the output of the "IDENTIFY DEVICE" command
 */
static void
ide_hd_identify(const ide_t *ide)
{
    char device_identify[9] = { '8', '6', 'B', '_', 'H', 'D', '0', '0', 0 };
    const ide_bm_t *bm      = ide_boards[ide->board]->bm;
    uint64_t full_size      = (((uint64_t) hdd[ide->hdd_num].tracks) *
                              hdd[ide->hdd_num].hpc * hdd[ide->hdd_num].spt);

    device_identify[6] = (ide->hdd_num / 10) + 0x30;
    device_identify[7] = (ide->hdd_num % 10) + 0x30;
    ide_log("IDE Identify: %s\n", device_identify);

    uint32_t d_hpc    = ide->hpc;
    uint32_t d_spt    = ide->spt;
    uint32_t d_tracks;

    if ((ide->hpc <= 16) && (ide->spt <= 63)) {
        /* HPC <= 16, report as needed. */
        d_tracks = ide->tracks;
    } else {
        /* HPC > 16, convert to 16 HPC. */
        if (ide->hpc > 16)
            d_hpc = 16;
        if (ide->spt > 63)
            d_spt = 63;
        d_tracks = (ide->tracks * ide->hpc * ide->spt) / (16 * 63);
        if (d_tracks > 16383)
            d_tracks = 16383;
    }

    /* Specify default CHS translation */
    if (full_size <= 16514064) {
        ide->buffer[1] = d_tracks; /* Tracks in default CHS translation. */
        ide->buffer[3] = d_hpc;    /* Heads in default CHS translation. */
        ide->buffer[6] = d_spt;    /* Heads in default CHS translation. */
    } else {
        ide->buffer[1] = 16383; /* Tracks in default CHS translation. */
        ide->buffer[3] = 16;    /* Heads in default CHS translation. */
        ide->buffer[6] = 63;    /* Heads in default CHS translation. */
    }
    ide_log("Default CHS translation: %i, %i, %i\n", ide->buffer[1], ide->buffer[3], ide->buffer[6]);

    /* Serial Number */
    ide_padstr((char *) (ide->buffer + 10), "", 20);
    /* Firmware */
    ide_padstr((char *) (ide->buffer + 23), EMU_VERSION_EX, 8);
    /* Model */
    if (hdd[ide->hdd_num].model)
        ide_padstr((char *) (ide->buffer + 27), hdd[ide->hdd_num].model, 40);
    else
        ide_padstr((char *) (ide->buffer + 27), device_identify, 40);
    /* Fixed drive */
    ide->buffer[0]  = (1 << 6);
    /* Buffer type */
    ide->buffer[20] = 3;
    /* Buffer size */
    ide->buffer[21] = hdd[ide->hdd_num].cache.num_segments * hdd[ide->hdd_num].cache.segment_size;
    /* Capabilities */
    ide->buffer[50] = 0x4000;
    ide->buffer[59] = ide->blocksize ? (ide->blocksize | 0x100) : 0;

    if ((ide->tracks >= 1024) || (ide->hpc > 16) || (ide->spt > 63)) {
        ide->buffer[49] = (1 << 9);
        ide_log("LBA supported\n");

        ide->buffer[60] = full_size & 0xFFFF; /* Total addressable sectors (LBA) */
        ide->buffer[61] = (full_size >> 16) & 0x0FFF;
        ide_log("Full size: %" PRIu64 "\n", full_size);

        /*
           Bit 0 = The fields reported in words 54-58 are valid;
           Bit 1 = The fields reported in words 64-70 are valid;
           Bit 2 = The fields reported in word 88 are valid.
         */
        ide->buffer[53] = 1;

        if (ide->params_specified) {
            ide->buffer[54] = (full_size / ide->cfg_hpc) / ide->cfg_spt;
            ide->buffer[55] = ide->cfg_hpc;
            ide->buffer[56] = ide->cfg_spt;
        } else {
            if (full_size <= 16514064) {
                ide->buffer[54] = d_tracks;
                ide->buffer[55] = d_hpc;
                ide->buffer[56] = d_spt;
            } else {
                ide->buffer[54] = 16383;
                ide->buffer[55] = 16;
                ide->buffer[56] = 63;
            }
        }

        full_size = ((uint64_t) ide->buffer[54]) * ((uint64_t) ide->buffer[55]) *
                    ((uint64_t) ide->buffer[56]);

        /* Total addressable sectors (LBA) */
        ide->buffer[57] = full_size & 0xFFFF;
        ide->buffer[58] = (full_size >> 16) & 0x0FFF;

        ide_log("Current CHS translation: %i, %i, %i\n", ide->buffer[54], ide->buffer[55], ide->buffer[56]);
    }

    /* Max sectors on multiple transfer command */
    ide->buffer[47] = hdd[ide->hdd_num].max_multiple_block | 0x8000;
    if (!ide_boards[ide->board]->force_ata3 && (bm != NULL)) {
        ide->buffer[80] = 0x7e; /*ATA-1 to ATA-6 supported*/
        ide->buffer[81] = 0x19; /*ATA-6 revision 3a supported*/
    } else
        ide->buffer[80] = 0x0e; /*ATA-1 to ATA-3 supported*/

    ide->buffer[83] = ide->buffer[84] = 0x4000;
    ide->buffer[86] = 0x0000;
    ide->buffer[87] = 0x4000;
}

static void
ide_identify(ide_t *ide)
{
    int          d;
    int          i;
    int          max_pio;
    int          max_sdma;
    int          max_mdma;
    int          max_udma;
    const ide_t *ide_other = ide_drives[ide->channel ^ 1];
    ide_bm_t *bm = ide_boards[ide->board]->bm;

    ide_log("IDE IDENTIFY or IDENTIFY PACKET DEVICE on board %i (channel %i)\n", ide->board, ide->channel);

    memset(ide->buffer, 0, 512);

    if (ide->type == IDE_ATAPI)
        ide->identify((const ide_t *) ide, !IDE_ATAPI_IS_EARLY && !ide_boards[ide->board]->force_ata3 && (bm != NULL));
    else if (ide->type == IDE_HDD)
        ide_hd_identify(ide);
    else {
        fatal("IDE IDENTIFY or IDENTIFY PACKET DEVICE on non-attached IDE device\n");
        return;
    }

    max_pio  = ide_get_max(ide, TYPE_PIO);
    max_sdma = ide_get_max(ide, TYPE_SDMA);
    max_mdma = ide_get_max(ide, TYPE_MDMA);
    max_udma = ide_get_max(ide, TYPE_UDMA);
    ide_log("IDE %i: max_pio = %i, max_sdma = %i, max_mdma = %i, max_udma = %i\n",
            ide->channel, max_pio, max_sdma, max_mdma, max_udma);

    if (ide_boards[ide->board]->bit32)
        ide->buffer[48] |= 1; /*Dword transfers supported*/
    ide->buffer[51] = ide_get_timings(ide, TIMINGS_PIO);
    ide->buffer[53] &= 0xfff9;
    ide->buffer[52] = ide->buffer[62] = ide->buffer[63] = ide->buffer[64] = 0x0000;
    ide->buffer[65] = ide->buffer[66] = ide_get_timings(ide, TIMINGS_DMA);
    ide->buffer[67] = ide->buffer[68] = 0x0000;
    ide->buffer[88]                   = 0x0000;

    if (max_pio >= 3) {
        ide->buffer[53] |= 0x0002;
        ide->buffer[67] = ide_get_timings(ide, TIMINGS_PIO);
        ide->buffer[68] = ide_get_timings(ide, TIMINGS_PIO_FC);
        for (i = 3; i <= max_pio; i++)
            ide->buffer[64] |= (1 << (i - 3));
    }
    if (max_sdma != -1) {
        for (i = 0; i <= max_sdma; i++)
            ide->buffer[62] |= (1 << i);
    }
    if (max_mdma != -1) {
        for (i = 0; i <= max_mdma; i++)
            ide->buffer[63] |= (1 << i);
    }
    if (max_udma != -1) {
        ide->buffer[53] |= 0x0004;
        for (i = 0; i <= max_udma; i++)
            ide->buffer[88] |= (1 << i);
        if (max_udma >= 4)
            ide->buffer[93] = 0x6000; /* Drive reports 80-conductor cable */

        if (ide->channel & 1)
            ide->buffer[93] |= 0x0b00;
        else {
            ide->buffer[93] |= 0x000b;
            /* PDIAG- is assered by device 1, so the bit should be 1 if there's a device 1,
               so it should be |= 0x001b if device 1 is present. */
            if (ide_other != NULL)
                ide->buffer[93] |= 0x0010;
        }
    }

    if ((max_sdma != -1) || (max_mdma != -1) || (max_udma != -1)) {
        /* DMA supported */
        ide->buffer[49] |= 0x100;
        ide->buffer[52] = ide_get_timings(ide, TIMINGS_DMA);
    }

    if ((max_mdma != -1) || (max_udma != -1)) {
        ide->buffer[65] = ide_get_timings(ide, TIMINGS_DMA);
        ide->buffer[66] = ide_get_timings(ide, TIMINGS_DMA);
    }

    if (ide->mdma_mode != -1) {
        d = (ide->mdma_mode & 0xff);
        d <<= 8;
        if ((ide->mdma_mode & 0x300) == 0x000) {
            if ((ide->mdma_mode & 0xff) >= 3)
                ide->buffer[64] |= d;
        } else if ((ide->mdma_mode & 0x300) == 0x100)
            ide->buffer[62] |= d;
        else if ((ide->mdma_mode & 0x300) == 0x200)
            ide->buffer[63] |= d;
        else if ((ide->mdma_mode & 0x300) == 0x300)
            ide->buffer[88] |= d;
        ide_log("PIDENTIFY DMA Mode: %04X, %04X\n", ide->buffer[62], ide->buffer[63]);
    }
}

/*
 * Return the sector offset for the current register values
 */
static off64_t
ide_get_sector(ide_t *ide)
{
    uint32_t heads;
    uint32_t sectors;

    if (ide->tf->lba)
        return (off64_t) ide->lba_addr;
    else {
        heads   = ide->cfg_hpc;
        sectors = ide->cfg_spt;

        uint8_t sector = ide->tf->sector ? (ide->tf->sector - 1) : 0;

        return ((((off64_t) ide->tf->cylinder * heads) + (off64_t) ide->tf->head) * sectors) +
               (off64_t) sector;
    }
}

static off64_t
ide_get_sector_format(ide_t *ide)
{
    uint32_t heads;
    uint32_t sectors;

    if (ide->tf->lba)
        return (off64_t) ide->lba_addr;
    else {
        heads   = ide->cfg_hpc;
        sectors = ide->cfg_spt;

        return ((((off64_t) ide->tf->cylinder * heads) + (off64_t) ide->tf->head) * sectors);
    }
}

/**
 * Move to the next sector using CHS addressing
 */
static void
ide_next_sector(ide_t *ide)
{
    uint32_t sector = ide->tf->sector;
    uint32_t head   = ide->tf->head;

    if (ide->tf->lba)
        ide->lba_addr++;
    else {
        sector++;
        if ((sector == 0) || (sector == (ide->cfg_spt + 1))) {
            sector = 1;
            head++;
            if (head == ide->cfg_hpc) {
                head = 0;
                ide->tf->cylinder++;
            }
        }
    }

    ide->tf->sector = sector & 0xff;
    ide->tf->head   = head & 0x0f;
}

static void
loadhd(ide_t *ide, int d, UNUSED(const char *fn))
{
    if (!hdd_image_load(d)) {
        ide->type = IDE_NONE;
        return;
    }

    hdd_preset_apply(d);

    ide->spt = ide->cfg_spt = hdd[d].spt;
    ide->hpc = ide->cfg_hpc = hdd[d].hpc;
    ide->tracks             = hdd[d].tracks;
    ide->type               = IDE_HDD;
    ide->hdd_num            = d;
}

void
ide_set_signature(ide_t *ide)
{
    uint16_t ide_signatures[4] = { 0x7f7f, 0x0000, 0xeb14, 0x7f7f };

    ide->tf->sector   = 1;
    ide->tf->head     = 0;
    ide->tf->secount  = 1;
    ide->tf->cylinder = ide_signatures[ide->type & ~IDE_SHADOW];

    if (ide->type == IDE_HDD)
        ide->drive = 0;
}

static int
ide_set_features(ide_t *ide)
{
    uint8_t features;
    uint8_t features_data;
    int     mode;
    int     submode;
    int     max;

    features      = ide->tf->cylprecomp;
    features_data = ide->tf->secount;

    ide_log("IDE %02X: Set features: %02X, %02X\n", ide->channel, features, features_data);

    switch (features) {
        case FEATURE_SET_TRANSFER_MODE: /* Set transfer mode. */
            ide_log("Transfer mode %02X\n", features_data >> 3);

            mode    = (features_data >> 3);
            submode = features_data & 7;

            switch (mode) {
                case 0x00: /* PIO default */
                    if (submode != 0)
                        return 0;
                    max            = ide_get_max(ide, TYPE_PIO);
                    ide->mdma_mode = (1 << max);
                    ide_log("IDE %02X: Setting DPIO mode: %02X, %08X\n", ide->channel,
                            submode, ide->mdma_mode);
                    break;

                case 0x01: /* PIO mode */
                    max = ide_get_max(ide, TYPE_PIO);
                    if (submode > max)
                        return 0;
                    ide->mdma_mode = (1 << submode);
                    ide_log("IDE %02X: Setting  PIO mode: %02X, %08X\n", ide->channel,
                            submode, ide->mdma_mode);
                    break;

                case 0x02: /* Singleword DMA mode */
                    max = ide_get_max(ide, TYPE_SDMA);
                    if (submode > max)
                        return 0;
                    ide->mdma_mode = (1 << submode) | 0x100;
                    ide_log("IDE %02X: Setting SDMA mode: %02X, %08X\n", ide->channel,
                            submode, ide->mdma_mode);
                    break;

                case 0x04: /* Multiword DMA mode */
                    max = ide_get_max(ide, TYPE_MDMA);
                    if (submode > max)
                        return 0;
                    ide->mdma_mode = (1 << submode) | 0x200;
                    ide_log("IDE %02X: Setting MDMA mode: %02X, %08X\n", ide->channel,
                            submode, ide->mdma_mode);
                    break;

                case 0x08: /* Ultra DMA mode */
                    max = ide_get_max(ide, TYPE_UDMA);
                    if (submode > max)
                        return 0;
                    ide->mdma_mode = (1 << submode) | 0x300;
                    ide_log("IDE %02X: Setting UDMA mode: %02X, %08X\n", ide->channel,
                            submode, ide->mdma_mode);
                    break;

                default:
                    return 0;
            }
            break;

        case FEATURE_ENABLE_IRQ_OVERLAPPED:
        case FEATURE_ENABLE_IRQ_SERVICE:
        case FEATURE_DISABLE_IRQ_OVERLAPPED:
        case FEATURE_DISABLE_IRQ_SERVICE:
            max = ide_get_max(ide, TYPE_MDMA);
            if (max == -1)
                return 0;
            else
                return 1;

        case FEATURE_DISABLE_REVERT: /* Disable reverting to power on defaults. */
        case FEATURE_ENABLE_REVERT:  /* Enable reverting to power on defaults. */
            return 1;

        default:
            return 0;
    }

    return 1;
}

void
ide_set_sector(ide_t *ide, int64_t sector_num)
{
    unsigned int cyl;
    unsigned int r;
    if (ide->tf->lba) {
        ide->tf->head     = (sector_num >> 24) & 0xff;
        ide->tf->cylinder = (sector_num >> 8) & 0xffff;
        ide->tf->sector   = sector_num & 0xff;
    } else {
        cyl               = sector_num / (hdd[ide->hdd_num].hpc * hdd[ide->hdd_num].spt);
        r                 = sector_num % (hdd[ide->hdd_num].hpc * hdd[ide->hdd_num].spt);
        ide->tf->cylinder = cyl & 0xffff;
        ide->tf->head      = ((r / hdd[ide->hdd_num].spt) & 0x0f) & 0xff;
        ide->tf->sector   = ((r % hdd[ide->hdd_num].spt) + 1) & 0xff;
    }
}

static void
ide_zero(int d)
{
    ide_t *dev;

    if (ide_drives[d] == NULL)
        ide_drives[d] = (ide_t *) calloc(1, sizeof(ide_t));

    dev                                = ide_drives[d];
    dev->tf                            = (ide_tf_t *) calloc(1, sizeof(ide_tf_t));
    dev->channel                       = d;
    dev->type                          = IDE_NONE;
    dev->hdd_num                       = -1;
    dev->tf->atastat                   = DRDY_STAT | DSC_STAT;
    dev->service                       = 0;
    dev->board                         = d >> 1;
    dev->selected                      = !(d & 1);
    ide_boards[dev->board]->ide[d & 1] = dev;
    timer_add(&dev->timer, ide_callback, dev, 0);
}

void
ide_allocate_buffer(ide_t *dev)
{
    if (dev->buffer == NULL)
        dev->buffer = (uint16_t *) calloc(1, 65536 * sizeof(uint16_t));
}

void
ide_atapi_attach(ide_t *ide)
{
    ide_bm_t *bm = ide_boards[ide->board]->bm;

    if (ide->type != IDE_NONE)
        return;

    ide->type = IDE_ATAPI;
    ide_allocate_buffer(ide);
    ide_set_signature(ide);
    ide->mdma_mode = (1 << ide->get_max((const ide_t *) ide, !IDE_ATAPI_IS_EARLY &&
                      !ide_boards[ide->board]->force_ata3 && (bm != NULL), TYPE_PIO));
    ide->tf->error = 1;
    ide->cfg_spt = ide->cfg_hpc = 0;
    if (!IDE_ATAPI_IS_EARLY)
        ide->tf->atastat = 0;
}

void
ide_set_callback(ide_t *ide, double callback)
{
    if (ide == NULL) {
        ide_log("ide_set_callback(NULL): Set callback failed\n");
        return;
    }

    ide_log("ide_set_callback(%i)\n", ide->channel);

    if (callback == 0.0)
        timer_stop(&ide->timer);
    else
        timer_on_auto(&ide->timer, callback);
}

void
ide_set_board_callback(uint8_t board, double callback)
{
    ide_board_t *dev = ide_boards[board];

    ide_log("ide_set_board_callback(%i)\n", board);

    if (dev == NULL) {
        ide_log("Set board callback failed\n");
        return;
    }

    if (callback == 0.0)
        timer_stop(&dev->timer);
    else
        timer_on_auto(&dev->timer, callback);
}

static void
ide_atapi_command_bus(ide_t *ide)
{
    ide->tf->atastat  = BUSY_STAT;
    ide->tf->phase    = 1;
    ide->tf->pos      = 0;
    ide->sc->callback = 1.0 * IDE_TIME;
    ide_set_callback(ide, ide->sc->callback);
}

static void
ide_atapi_callback(ide_t *ide)
{
    int out;
    int ret = 0;
    ide_bm_t *bm = ide_boards[ide->board]->bm;
#ifdef ENABLE_IDE_LOG
    char *phases[7] = { "Idle", "Command", "Data in", "Data out", "Data in DMA", "Data out DMA",
                     "Complete" };
    char *phase;

    switch (ide->sc->packet_status) {
        default:
            phase = "Unknown";
            break;
        case PHASE_IDLE ... PHASE_COMPLETE:
            phase = phases[ide->sc->packet_status];
            break;
        case PHASE_ERROR:
            phase = "Error";
            break;
        case PHASE_NONE:
            phase = "None";
            break;
    }

    ide_log("Phase: %02X (%s)\n", ide->sc->packet_status, phase);
#endif

    switch (ide->sc->packet_status) {
        default:
            break;

        case PHASE_IDLE:
            ide->tf->pos     = 0;
            ide->tf->phase   = 1;
            ide->tf->atastat = READY_STAT | DRQ_STAT | (ide->tf->atastat & ERR_STAT);
            break;
        case PHASE_COMMAND:
            ide->tf->atastat = BUSY_STAT | (ide->tf->atastat & ERR_STAT);
            if (ide->packet_command) {
                ide->packet_command(ide->sc, ide->sc->atapi_cdb);
                if ((ide->sc->packet_status == PHASE_COMPLETE) && (ide->sc->callback == 0.0))
                    ide_atapi_callback(ide);
            }
            break;
        case PHASE_COMPLETE:
        case PHASE_ERROR:
            ide->tf->atastat = READY_STAT;
            if (ide->sc->packet_status == PHASE_ERROR)
                ide->tf->atastat       |= ERR_STAT;
            ide->tf->phase         = 3;
            ide->sc->packet_status = PHASE_NONE;
            ide_irq_raise(ide);
            break;
        case PHASE_DATA_IN:
        case PHASE_DATA_OUT:
            ide->tf->atastat = READY_STAT | DRQ_STAT | (ide->tf->atastat & ERR_STAT);
            ide->tf->phase   = !(ide->sc->packet_status & 0x01) << 1;
            ide_irq_raise(ide);
            break;
        case PHASE_DATA_IN_DMA:
        case PHASE_DATA_OUT_DMA:
            out = (ide->sc->packet_status & 0x01);

            if (!IDE_ATAPI_IS_EARLY && !ide_boards[ide->board]->force_ata3 &&
                (bm != NULL) && bm->dma) {
                ret = bm->dma(ide->sc->temp_buffer, ide->sc->packet_len, out, bm->priv);
            }
            /* Else, DMA command without a bus master, ret = 0 (default). */

            switch (ret) {
                default:
                    break;
                case 0:
                    if (ide->bus_master_error)
                        ide->bus_master_error(ide->sc);
                    break;
                case 1:
                    if (out && ide->phase_data_out)
                        (void) ide->phase_data_out(ide->sc);
                    else if (!out && ide->command_stop)
                        ide->command_stop(ide->sc);

                    if ((ide->sc->packet_status == PHASE_COMPLETE) && (ide->sc->callback == 0.0))
                        ide_atapi_callback(ide);
                    break;
                case 2:
                    ide_atapi_command_bus(ide);
                    break;
            }
            break;
    }
}

/* This is the general ATAPI PIO request function. */
static void
ide_atapi_pio_request(ide_t *ide, uint8_t out)
{
    scsi_common_t *dev = ide->sc;

    ide_irq_lower(ide);

    ide->tf->atastat = BSY_STAT;

    if (ide->tf->pos >= dev->packet_len) {
        ide_log("%i bytes %s, command done\n", ide->tf->pos, out ? "written" : "read");

        ide->tf->pos = dev->request_pos = 0;
        if (out && ide->phase_data_out)
            ide->phase_data_out(dev);
        else if (!out && ide->command_stop)
            ide->command_stop(dev);

        if ((ide->sc->packet_status == PHASE_COMPLETE) && (ide->sc->callback == 0.0))
            ide_atapi_callback(ide);
    } else {
        ide_log("%i bytes %s, %i bytes are still left\n", ide->tf->pos,
                out ? "written" : "read", dev->packet_len - ide->tf->pos);

        /* If less than (packet length) bytes are remaining, update packet length
           accordingly. */
        if ((dev->packet_len - ide->tf->pos) < (dev->max_transfer_len)) {
            dev->max_transfer_len = dev->packet_len - ide->tf->pos;
            /* Also update the request length so the host knows how many bytes to transfer. */
            ide->tf->request_length = dev->max_transfer_len;
        }
        ide_log("CD-ROM %i: Packet length %i, request length %i\n", dev->id, dev->packet_len,
                dev->max_transfer_len);

        dev->packet_status = PHASE_DATA_IN | out;

        ide->tf->atastat = BSY_STAT;
        ide->tf->phase  = 1;
        ide_atapi_callback(ide);
        ide_set_callback(ide, 0.0);

        dev->request_pos = 0;
    }
}

static uint16_t
ide_atapi_packet_read(ide_t *ide)
{
    scsi_common_t *dev = ide->sc;
    const uint16_t *bufferw;
    uint16_t ret = 0;

    if (dev && dev->temp_buffer && (dev->packet_status == PHASE_DATA_IN)) {
        ide_log("PHASE_DATA_IN read: %i, %i, %i, %i\n",
                dev->request_pos, dev->max_transfer_len, ide->tf->pos, dev->packet_len);

        bufferw = (uint16_t *) dev->temp_buffer;

        /* Make sure we return a 0 and don't attempt to read from the buffer if
           we're transferring bytes beyond it, which can happen when issuing media
           access commands with an allocated length below minimum request length
           (which is 1 sector = 2048 bytes). */
        ret = (ide->tf->pos < dev->packet_len) ? bufferw[ide->tf->pos >> 1] : 0;
        ide->tf->pos += 2;

        dev->request_pos += 2;

        if ((dev->request_pos >= dev->max_transfer_len) || (ide->tf->pos >= dev->packet_len)) {
            /* Time for a DRQ. */
            ide_atapi_pio_request(ide, 0);
        }
    }

    return ret;
}

static void
ide_atapi_packet_write(ide_t *ide, const uint16_t val)
{
    scsi_common_t *dev = ide->sc;

    uint8_t  *bufferb = NULL;
    uint16_t *bufferw = NULL;

    if (dev) {
        if (dev->packet_status == PHASE_IDLE)
            bufferb = dev->atapi_cdb;
        else if (dev->temp_buffer)
            bufferb = dev->temp_buffer;

        bufferw = (uint16_t *) bufferb;
    }

    if ((bufferb != NULL) && (dev->packet_status != PHASE_DATA_IN)) {
        bufferw[ide->tf->pos >> 1] = val & 0xffff;

        ide->tf->pos += 2;
        dev->request_pos += 2;

        if (dev->packet_status == PHASE_DATA_OUT) {
            if ((dev->request_pos >= dev->max_transfer_len) || (ide->tf->pos >= dev->packet_len)) {
                /* Time for a DRQ. */
                ide_atapi_pio_request(ide, 1);
            }
        } else if (dev->packet_status == PHASE_IDLE) {
            if (ide->tf->pos >= 12) {
                ide->tf->pos       = 0;
                ide->tf->atastat   = BSY_STAT;
                dev->packet_status = PHASE_COMMAND;
                ide_atapi_callback(ide);
            }
        }
    }
}

static void
ide_write_data(ide_t *ide, const uint16_t val)
{
    uint16_t *idebufferw = ide->buffer;

    if ((ide->type != IDE_NONE) && !(ide->type & IDE_SHADOW) && ide->buffer) {
        if (ide->command == WIN_PACKETCMD) {
            if (ide->type == IDE_ATAPI)
                ide_atapi_packet_write(ide, val);
            else
                ide->tf->pos = 0;
        } else {
            idebufferw[ide->tf->pos >> 1] = val & 0xffff;
            ide->tf->pos += 2;

            if (ide->tf->pos >= 512) {
                ide->tf->pos     = 0;
                ide->tf->atastat = BSY_STAT;
                const double seek_time = hdd_timing_write(&hdd[ide->hdd_num], ide_get_sector(ide), 1);
                const double xfer_time = ide_get_xfer_time(ide, 512);
                const double wait_time = seek_time + xfer_time;
                if (ide->command == WIN_WRITE_MULTIPLE) {
                    if (hdd[ide->hdd_num].speed_preset == 0) {
                        ide->pending_delay = 0;
                        ide_callback(ide);
                    } else if ((ide->blockcount + 1) >= ide->blocksize || ide->tf->secount == 1) {
                        ide_set_callback(ide, seek_time + xfer_time + ide->pending_delay);
                        ide->pending_delay = 0;
                    } else {
                        ide->pending_delay += wait_time;
                        ide_callback(ide);
                    }
                } else
                    ide_set_callback(ide, wait_time);
            }
        }
    }
}

void
ide_writew(uint16_t addr, uint16_t val, void *priv)
{
    const ide_board_t *dev = (ide_board_t *) priv;

    ide_t *ide;
    int    ch;

    ch  = dev->cur_dev;
    ide = ide_drives[ch];

#if defined(ENABLE_IDE_LOG) && (ENABLE_IDE_LOG == 2)
    ide_log("ide_writew(%04X, %04X, %08X)\n", addr, val, priv);
#endif

    addr &= 0x7;

    if ((ide->type == IDE_NONE) && ((addr == 0x0) || (addr == 0x7)))
        return;

    switch (addr) {
        case 0x0: /* Data */
            ide_write_data(ide, val);
            break;
        case 0x7:
            ide_writeb(addr, val & 0xff, priv);
            break;
        default:
            ide_writeb(addr, val & 0xff, priv);
            ide_writeb(addr + 1, (val >> 8) & 0xff, priv);
            break;
    }
}

static void
ide_writel(uint16_t addr, uint32_t val, void *priv)
{
    const ide_board_t *dev = (ide_board_t *) priv;

    ide_t *ide;
    int    ch;

    ch  = dev->cur_dev;
    ide = ide_drives[ch];

#if defined(ENABLE_IDE_LOG) && (ENABLE_IDE_LOG == 2)
    ide_log("ide_writel(%04X, %08X, %08X)\n", addr, val, priv);
#endif

    addr &= 0x7;

    if ((ide->type == IDE_NONE) && ((addr == 0x0) || (addr == 0x7)))
        return;

    switch (addr) {
        case 0x0: /* Data */
            ide_write_data(ide, val & 0xffff);
            if (dev->bit32)
                ide_write_data(ide, val >> 16);
            else
                ide_writew(addr + 2, (val >> 16) & 0xffff, priv);
            break;
        case 0x6:
        case 0x7:
            ide_writew(addr, val & 0xffff, priv);
            break;
        default:
            ide_writew(addr, val & 0xffff, priv);
            ide_writew(addr + 2, (val >> 16) & 0xffff, priv);
            break;
    }
}

static void
dev_reset(ide_t *ide)
{
    ide_set_signature(ide);

    if ((ide->type == IDE_ATAPI) && ide->stop)
        ide->stop(ide->sc);
}

void
ide_write_devctl(UNUSED(uint16_t addr), uint8_t val, void *priv)
{
    ide_board_t *dev = (ide_board_t *) priv;

    ide_t  *ide;
    ide_t  *ide_other;
    int     ch;
    uint8_t old;

    ch        = dev->cur_dev;
    ide       = ide_drives[ch];
    ide_other = ide_drives[ch ^ 1];

    ide_log("ide_write_devctl(%04X, %02X, %08X)\n", addr, val, priv);

    if ((ide->type == IDE_NONE) && (ide_other->type == IDE_NONE))
        return;

    dev->diag = 0;

    if ((val & 4) && !(dev->devctl & 4)) {
        /* Reset toggled from 0 to 1, initiate reset procedure. */
        if (ide->type == IDE_ATAPI)
            ide->sc->callback = 0.0;
        ide_set_callback(ide, 0.0);
        ide_set_callback(ide_other, 0.0);

        /* We must set set the status to busy in reset mode or
           some 286 and 386 machines error out. */
        if (!(ch & 1)) {
            if (ide->type != IDE_NONE) {
                ide->tf->atastat = BSY_STAT;
                ide->tf->error   = 1;
            }

            if (ide_other->type != IDE_NONE) {
                ide_other->tf->atastat = BSY_STAT;
                ide_other->tf->error   = 1;
            }
        }
    } else if (!(val & 4) && (dev->devctl & 4)) {
        /* Reset toggled from 1 to 0. */
        if (!(ch & 1)) {
            /* Currently active device is 0, use the device 0 reset protocol. */
            /* Device 0. */
            dev_reset(ide);
            ide->tf->atastat = BSY_STAT;
            ide->tf->error   = 1;

            /* Device 1. */
            dev_reset(ide_other);
            ide_other->tf->atastat = BSY_STAT;
            ide_other->tf->error   = 1;

            /* Fire the timer. */
            dev->diag  = 0;
            ide->reset = 1;
            ide_set_callback(ide, 0.0);
            ide_set_callback(ide_other, 0.0);
            ide_set_board_callback(ide->board, 1000.4); /* 1 ms + 400 ns, per the specification */
        } else {
            /* Currently active device is 1, simply reset the status and the active device. */
            dev_reset(ide);
            if (ide->type == IDE_ATAPI) {
                /* Non-early ATAPI devices have DRDY clear after SRST. */
                ide->tf->atastat = 0;
                if (IDE_ATAPI_IS_EARLY)
                    ide->tf->atastat |= DRDY_STAT;
            } else
                ide->tf->atastat = DRDY_STAT | DSC_STAT;
            ide->tf->error   = 1;
            ide_other->tf->error   = 1;    /* Assert PDIAG-. */
            dev->cur_dev &= ~1;
            ch = dev->cur_dev;

            ide           = ide_drives[ch];
            ide->selected = 1;

            ide_other           = ide_drives[ch ^ 1];
            ide_other->selected = 0;
        }
    }

    old         = dev->devctl;
    dev->devctl = val;
    if (!(val & 0x02) && (old & 0x02))
        ide_irq_update(ide_boards[ide->board], 1);
}

static void
ide_reset_registers(ide_t *ide)
{
    uint16_t ide_signatures[4] = { 0x7f7f, 0x0000, 0xeb14, 0x7f7f };

    ide->tf->atastat  = DRDY_STAT | DSC_STAT;
    ide->tf->error    = 1;
    ide->tf->secount  = 1;
    ide->tf->cylinder = ide_signatures[ide->type & ~IDE_SHADOW];
    ide->tf->sector   = 1;
    ide->tf->head     = 0;

    ide->reset        = 0;

    if (ide->type == IDE_ATAPI)
        ide->sc->callback       = 0.0;

    ide_set_callback(ide, 0.0);
}

void
ide_writeb(uint16_t addr, uint8_t val, void *priv)
{
    ide_board_t *dev = (ide_board_t *) priv;
    ide_t *ide;
    ide_t *ide_other;
    int    ch;
    int    bad = 0;
    int    reset = 0;

    ch        = dev->cur_dev;
    ide       = ide_drives[ch];
    ide_other = ide_drives[ch ^ 1];

    ide_log("ide_writeb(%04X, %02X, %08X)\n", addr, val, priv);

    addr &= 0x7;

    if ((ide->type != IDE_NONE) || ((addr != 0x0) && (addr != 0x7)))  switch (addr) {
        case 0x0: /* Data */
            ide_write_data(ide, val | (val << 8));
            break;

        /* Note to self: for ATAPI, bit 0 of this is DMA if set, PIO if clear. */
        case 0x1: /* Features */
            if (!(ide->tf->atastat & (BSY_STAT | DRQ_STAT))) {
                ide->tf->cylprecomp = val;
                if (ide->type == IDE_ATAPI)
                    ide_log("ATAPI transfer mode: %s\n", (val & 1) ? "DMA" : "PIO");
            }

            if (!(ide_other->tf->atastat & (BSY_STAT | DRQ_STAT)))
                ide_other->tf->cylprecomp = val;
            break;

        case 0x2: /* Sector count */
            if (!(ide->tf->atastat & (BSY_STAT | DRQ_STAT)))
                ide->tf->secount       = val;
            if (!(ide_other->tf->atastat & (BSY_STAT | DRQ_STAT)))
                ide_other->tf->secount = val;
            break;

        case 0x3: /* Sector */
            if (!(ide->tf->atastat & (BSY_STAT | DRQ_STAT))) {
                ide->tf->sector        = val;
                ide->lba_addr          = (ide->lba_addr & 0xfffff00) | val;
            }

            if (!(ide_other->tf->atastat & (BSY_STAT | DRQ_STAT))) {
                ide_other->tf->sector  = val;
                ide_other->lba_addr    = (ide_other->lba_addr & 0xfffff00) | val;
            }
            break;

        case 0x4: /* Cylinder low */
            if (ide->type & IDE_SHADOW)
                break;

            if (!(ide->tf->atastat & (BSY_STAT | DRQ_STAT))) {
                ide->tf->cylinder = (ide->tf->cylinder & 0xff00) | val;
                ide->lba_addr     = (ide->lba_addr & 0xfff00ff) | (val << 8);
            }

            if (!(ide_other->tf->atastat & (BSY_STAT | DRQ_STAT))) {
                ide_other->tf->cylinder = (ide_other->tf->cylinder & 0xff00) | val;
                ide_other->lba_addr     = (ide_other->lba_addr & 0xfff00ff) | (val << 8);
            }
            break;

        case 0x5: /* Cylinder high */
            if (ide->type & IDE_SHADOW)
                break;

            if (!(ide->tf->atastat & (BSY_STAT | DRQ_STAT))) {
                ide->tf->cylinder = (ide->tf->cylinder & 0xff) | (val << 8);
                ide->lba_addr     = (ide->lba_addr & 0xf00ffff) | (val << 16);
            }

            if (!(ide_other->tf->atastat & (BSY_STAT | DRQ_STAT))) {
                ide_other->tf->cylinder = (ide_other->tf->cylinder & 0xff) | (val << 8);
                ide_other->lba_addr     = (ide_other->lba_addr & 0xf00ffff) | (val << 16);
            }
            break;

        case 0x6: /* Drive/Head */
            if (ch != ((val >> 4) & 1) + (ide->board << 1)) {
                if (!ide->reset && !ide_other->reset && ide->irqstat) {
                    ide_irq_lower(ide);
                    ide->irqstat = 1;
                }

                ide_boards[ide->board]->cur_dev = ((val >> 4) & 1) + (ide->board << 1);
                ch                              = ide_boards[ide->board]->cur_dev;

                ide           = ide_drives[ch];
                ide->selected = 1;

                ide_other           = ide_drives[ch ^ 1];
                ide_other->selected = 0;

                if (ide->reset || ide_other->reset) {
                    ide_reset_registers(ide);
                    ide_reset_registers(ide_other);

                    ide_set_board_callback(ide->board, 0.0);
                    reset = 1;
                } else
                    ide_irq_update(ide_boards[ide->board], 1);
            }

            if (!reset) {
                if (!(ide->tf->atastat & (BSY_STAT | DRQ_STAT))) {
                    ide->tf->drvsel     = val & 0xef;
                    ide->lba_addr       = (ide->lba_addr & 0x0ffffff) |
                                          (ide->tf->head << 24);
                }

                if (!(ide_other->tf->atastat & (BSY_STAT | DRQ_STAT))) {
                    ide_other->tf->drvsel = val & 0xef;
                    ide_other->lba_addr = (ide_other->lba_addr & 0x0ffffff) |
                                          (ide->tf->head << 24);
                }
            }
            break;

        case 0x7: /* Command register */
            if (ide->tf->atastat & (BSY_STAT | DRQ_STAT))
                break;

            if ((ide->type == IDE_NONE) || ((ide->type & IDE_SHADOW) && (val != WIN_DRIVE_DIAGNOSTICS)))
                break;

            ide_irq_lower(ide);
            ide->command = val;

            ide->tf->error = 0;

            switch (val) {
                case WIN_RECAL ... 0x1f:
                case WIN_SEEK ... 0x7f:
                    if (ide->type == IDE_ATAPI)
                        ide->tf->atastat = DRDY_STAT;
                    else
                        ide->tf->atastat = READY_STAT | BSY_STAT;

                    if (ide->type == IDE_ATAPI) {
                        ide->sc->callback = 100.0 * IDE_TIME;
                        ide_set_callback(ide, 100.0 * IDE_TIME);
                    } else {
                        if (hdd[ide->hdd_num].speed_preset == 0)
                            ide_set_callback(ide, 100.0 * IDE_TIME);
                        else {
                            double seek_time = hdd_seek_get_time(&hdd[ide->hdd_num], (val & 0x60) ?
                                                                 ide_get_sector(ide) : 0, HDD_OP_SEEK, 0, 0.0);
                            ide_set_callback(ide, seek_time);
                        }
                    }
                    break;

                case WIN_SRST: /* ATAPI Device Reset */
                    if (ide->type == IDE_ATAPI) {
                        ide->tf->atastat  = BSY_STAT;
                        ide->sc->callback = 100.0 * IDE_TIME;
                    } else
                        ide->tf->atastat  = DRDY_STAT;

                    ide_set_callback(ide, 100.0 * IDE_TIME);
                    break;

                case WIN_READ_MULTIPLE:
                    /* Fatal removed in accordance with the official ATAPI reference:
                       If the Read Multiple command is attempted before the Set Multiple Mode
                       command  has  been  executed  or  when  Read  Multiple  commands  are
                       disabled, the Read Multiple operation is rejected with an Aborted Com-
                       mand error. */
                    ide->blockcount = 0;
                    fallthrough;

                case WIN_READ:
                case WIN_READ_NORETRY:
                case WIN_READ_DMA:
                case WIN_READ_DMA_ALT:
                    ide->tf->atastat = BSY_STAT;

                    if (ide->type == IDE_ATAPI)
                        ide->sc->callback = 200.0 * IDE_TIME;

                    if (ide->type == IDE_HDD) {
                        ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus_type, 1);
                        uint32_t sec_count;
                        double   wait_time;
                        if ((val == WIN_READ_DMA) || (val == WIN_READ_DMA_ALT)) {
                            /* TODO: Make DMA timing more accurate. */
                            sec_count        = ide->tf->secount ? ide->tf->secount : 256;
                            double seek_time = hdd_timing_read(&hdd[ide->hdd_num],
                                                               ide_get_sector(ide), sec_count);
                            double xfer_time = ide_get_xfer_time(ide, 512 * sec_count);
                            wait_time        = seek_time > xfer_time ? seek_time : xfer_time;
                        } else if ((val == WIN_READ_MULTIPLE) && (hdd[ide->hdd_num].speed_preset == 0)) {
                           ide_set_callback(ide, 200.0 * IDE_TIME);
                           ide->do_initial_read = 1;
                           break;
                        } else if ((val == WIN_READ_MULTIPLE) && (ide->blocksize > 0)) {
                            sec_count = ide->tf->secount ? ide->tf->secount : 256;
                            if (sec_count > ide->blocksize)
                                sec_count = ide->blocksize;
                            double seek_time = hdd_timing_read(&hdd[ide->hdd_num],
                                                               ide_get_sector(ide), sec_count);
                            double xfer_time = ide_get_xfer_time(ide, 512 * sec_count);
                            wait_time        = seek_time + xfer_time;
                        } else if ((val == WIN_READ_MULTIPLE) && (ide->blocksize == 0))
                            wait_time = 200.0;
                        else {
                            sec_count        = 1;
                            double seek_time = hdd_timing_read(&hdd[ide->hdd_num],
                                                               ide_get_sector(ide), sec_count);
                            double xfer_time = ide_get_xfer_time(ide, 512 * sec_count);
                            wait_time        = seek_time + xfer_time;
                        }
                        ide_set_callback(ide, wait_time);
                    } else
                        ide_set_callback(ide, 200.0 * IDE_TIME);
                    ide->do_initial_read = 1;
                    break;

                case WIN_WRITE_MULTIPLE:
                    /* Fatal removed for the same reason as for WIN_READ_MULTIPLE. */
                    ide->blockcount = 0;
                    /* Turn on the activity indicator *here* so that it gets turned on
                       less times. */
                    ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus_type, 1);
                    fallthrough;

                case WIN_WRITE:
                case WIN_WRITE_NORETRY:
                    ide->tf->atastat = DRQ_STAT | DSC_STAT | DRDY_STAT;
                    ide->tf->pos    = 0;
                    break;

                case WIN_WRITE_DMA:
                case WIN_WRITE_DMA_ALT:
                case WIN_VERIFY:
                case WIN_VERIFY_ONCE:
                case WIN_IDENTIFY:     /* Identify Device */
                case WIN_SET_FEATURES: /* Set Features */
                case WIN_READ_NATIVE_MAX:
                    ide->tf->atastat = BSY_STAT;

                    if (ide->type == IDE_ATAPI)
                        ide->sc->callback = 200.0 * IDE_TIME;

                    if ((ide->type == IDE_HDD) && ((val == WIN_WRITE_DMA) || (val == WIN_WRITE_DMA_ALT))) {
                        uint32_t sec_count = ide->tf->secount ? ide->tf->secount : 256;
                        double   seek_time = hdd_timing_read(&hdd[ide->hdd_num],
                                                             ide_get_sector(ide), sec_count);
                        double   xfer_time = ide_get_xfer_time(ide, 512 * sec_count);
                        double   wait_time = seek_time > xfer_time ? seek_time : xfer_time;
                        ide_set_callback(ide, wait_time);
                    } else if ((ide->type == IDE_HDD) && ((val == WIN_VERIFY) ||
                               (val == WIN_VERIFY_ONCE))) {
                        uint32_t sec_count = ide->tf->secount ? ide->tf->secount : 256;
                        double   seek_time = hdd_timing_read(&hdd[ide->hdd_num],
                                                             ide_get_sector(ide), sec_count);
                        ide_set_callback(ide, seek_time + ide_get_xfer_time(ide, 2));
                    } else if ((val == WIN_IDENTIFY) || (val == WIN_SET_FEATURES))
                        ide_callback(ide);
                    else
                        ide_set_callback(ide, 200.0 * IDE_TIME);
                    break;

                case WIN_FORMAT:
                    if (ide->type == IDE_ATAPI)
                        bad = 1;
                    else {
                        ide->tf->atastat = DRQ_STAT;
                        ide->tf->pos     = 0;
                    }
                    break;

                case WIN_SPECIFY: /* Initialize Drive Parameters */
                    ide->tf->atastat = BSY_STAT;

                    if (ide->type == IDE_ATAPI)
                        ide->sc->callback = 30.0 * IDE_TIME;

                    ide_set_callback(ide, 30.0 * IDE_TIME);
                    break;

                case WIN_DRIVE_DIAGNOSTICS: /* Execute Drive Diagnostics */
                    dev->cur_dev &= ~1;
                    ide                 = ide_drives[ch & ~1];
                    ide->selected       = 1;
                    ide_other           = ide_drives[ch | 1];
                    ide_other->selected = 0;

                    /* Device 0. */
                    dev_reset(ide);
                    ide->tf->atastat = BSY_STAT;
                    ide->tf->error   = 1;

                    /* Device 1. */
                    dev_reset(ide_other);
                    ide_other->tf->atastat = BSY_STAT;
                    ide_other->tf->error   = 1;

                    /* Fire the timer. */
                    dev->diag  = 1;
                    ide->reset = 1;
                    ide_set_callback(ide, 0.0);
                    ide_set_callback(ide_other, 0.0);
                    ide_set_board_callback(ide->board, 200.0 * IDE_TIME);
                    break;

                case WIN_PIDENTIFY:         /* Identify Packet Device */
                case WIN_SET_MULTIPLE_MODE: /* Set Multiple Mode */
                case WIN_NOP:
                case WIN_STANDBYNOW1:
                case WIN_IDLENOW1:
                case WIN_SETIDLE1:          /* Idle */
                case WIN_CHECKPOWERMODE1:
                case WIN_SLEEP1:
                    ide->tf->atastat = BSY_STAT;
                    ide_callback(ide);
                    break;

                case WIN_PACKETCMD: /* ATAPI Packet */
                    /* Skip the command callback wait, and process immediately. */
                    ide->tf->pos           = 0;
                    if (ide->type == IDE_ATAPI) {
                        ide->sc->packet_status = PHASE_IDLE;
                        ide->tf->secount       = 1;
                        ide->tf->atastat       = DRDY_STAT | DRQ_STAT;
                        if (ide->interrupt_drq)
                            ide_irq_raise(ide); /* Interrupt DRQ, requires IRQ on any DRQ. */
                    } else {
                        ide->tf->atastat       = BSY_STAT;
                        ide_set_callback(ide, 200.0 * IDE_TIME);
                    }
                    break;

                case 0xf0:
                default:
                    bad = 1;
                    break;
            }

            if (bad) {
                ide->tf->atastat = DRDY_STAT | ERR_STAT | DSC_STAT;
                ide->tf->error   = ABRT_ERR;
                ide_irq_raise(ide);
            }
            break;

        default:
            break;
    }
}

static uint16_t
ide_read_data(ide_t *ide)
{
    const uint16_t *idebufferw = ide->buffer;
    uint16_t ret = 0x0000;

#if defined(ENABLE_IDE_LOG) && (ENABLE_IDE_LOG == 2)
    ide_log("ide_read_data(): ch = %i, board = %i, type = %i\n", ide->channel,
            ide->board, ide->type);
#endif

    if ((ide->type == IDE_NONE) || (ide->type & IDE_SHADOW) || (ide->buffer == NULL))
        ret = 0xff7f;
    else if (ide->command == WIN_PACKETCMD) {
        if (ide->type == IDE_ATAPI)
            ret = ide_atapi_packet_read(ide);
        else {
            ide_log("Drive not ATAPI (position: %i)\n", ide->tf->pos);
            ide->tf->pos = 0;
        }
    } else {
        ret = idebufferw[ide->tf->pos >> 1];
        ide->tf->pos += 2;

        if (ide->tf->pos >= 512) {
            ide->tf->pos     = 0;
            ide->tf->atastat = DRDY_STAT | DSC_STAT;
            if (ide->type == IDE_ATAPI)
                ide->sc->packet_status = PHASE_IDLE;

            if ((ide->command == WIN_READ) ||
                (ide->command == WIN_READ_NORETRY) ||
                (ide->command == WIN_READ_MULTIPLE)) {

                ide->tf->secount--;

                if (ide->tf->secount) {
                    ide_next_sector(ide);
                    ide->tf->atastat = BSY_STAT | READY_STAT | DSC_STAT;
                    if (ide->command == WIN_READ_MULTIPLE) {
                        if (hdd[ide->hdd_num].speed_preset == 0)
                            ide_callback(ide);
                        else if (!ide->blockcount) {
                            uint32_t cnt = ide->tf->secount ?
                                           ide->tf->secount : 256;
                            if (cnt > ide->blocksize)
                                cnt = ide->blocksize;
                            const double seek_us = hdd_timing_read(&hdd[ide->hdd_num],
                                                   ide_get_sector(ide), cnt);
                            const double xfer_us = ide_get_xfer_time(ide, 512 * cnt);
                            ide_set_callback(ide, seek_us + xfer_us);
                        } else
                            ide_callback(ide);
                    } else {
                        const double seek_us = hdd_timing_read(&hdd[ide->hdd_num],
                                                               ide_get_sector(ide), 1);
                        const double xfer_us = ide_get_xfer_time(ide, 512);
                        ide_set_callback(ide, seek_us + xfer_us);
                    }
                } else
                    ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus_type, 0);
            }
        }
    }

    return ret;
}

static uint8_t
ide_status(ide_t *ide, UNUSED(ide_t *ide_other), UNUSED(int ch))
{
    uint8_t ret;

    /* Absent and is master or both are absent. */
    if (ide->type == IDE_NONE) {
        /* Bit 7 pulled down, all other bits pulled up, per the spec. */
        ret = 0x7f;
    /* Absent and is slave and master is present. */
    } else if (ide->type & IDE_SHADOW) {
        /* On real hardware, a slave with a present master always
           returns a status of 0x00.
           Confirmed by the ATA-3 and ATA-4 specifications. */
        ret = 0x00;
    } else {
        ret = ide->tf->atastat;
        if (ide->type == IDE_ATAPI)
            ret = (ret & ~DSC_STAT) | (ide->service << 4);
    }

    return ret;
}

uint8_t
ide_readb(uint16_t addr, void *priv)
{
    const ide_board_t *dev = (ide_board_t *) priv;
    int    ch;
    ide_t *ide;
    uint8_t  ret = 0xff;

    ch  = dev->cur_dev;
    ide = ide_drives[ch];

    switch (addr & 0x7) {
        case 0x0: /* Data */
            ret = ide_read_data(ide) & 0xff;
            break;

        /* For ATAPI: Bits 7-4 = sense key, bit 3 = MCR (media change requested),
                      Bit 2 = ABRT (aborted command), Bit 1 = EOM (end of media),
                      and Bit 0 = ILI (illegal length indication). */
        case 0x1: /* Error */
            if (ide->type == IDE_NONE)
                ret = 0x7f;
            else
                ret = ide->tf->error;
            break;

        /* For ATAPI:
                Bit 0: Command or Data:
                        Data if clear, Command if set;
                Bit 1: I/OB
                        Direction:
                                To device if set;
                                From device if clear.
                IO      DRQ     CoD
                0       1       1       Ready to accept command packet
                1       1       1       Message - ready to send message to host
                1       1       0       Data to host
                0       1       0       Data from host
                1       0       1       Status. */
        case 0x2: /* Sector count */
            if (ide->type == IDE_NONE)
                ret = 0x7f;
            else
                ret = ide->tf->secount;
            break;

        case 0x3: /* Sector */
            if (ide->type == IDE_NONE)
                ret = 0x7f;
            else
                ret = (uint8_t) ide->tf->sector;
            break;

        case 0x4: /* Cylinder low */
            if (ide->type == IDE_NONE)
                ret = 0x7f;
            else
                ret = ide->tf->cylinder & 0xff;
#if defined(ENABLE_IDE_LOG) && (ENABLE_IDE_LOG == 2)
            ide_log("Cylinder low  @ board %i, channel %i: ide->type = %i, "
                    "ret = %02X\n", ide->board, ide->channel, ide->type, ret);
#endif
            break;

        case 0x5: /* Cylinder high */
            if (ide->type == IDE_NONE)
                ret = 0x7f;
            else
                ret = ide->tf->cylinder >> 8;
#if defined(ENABLE_IDE_LOG) && (ENABLE_IDE_LOG == 2)
            ide_log("Cylinder high @ board %i, channel %i: ide->type = %i, "
                    "ret = %02X\n", ide->board, ide->channel, ide->type, ret);
#endif
            break;

        case 0x6: /* Drive/Head */
            if (ide->type == IDE_NONE)
                ret = 0x7f;
            else
                ret = ide->tf->drvsel | ((ch & 1) ? 0xb0 : 0xa0);
            break;

        /* For ATAPI: Bit 5 is DMA ready, but without overlapped or interlaved DMA, it is
                      DF (drive fault). */
        case 0x7: /* Status */
            ide_irq(ide, 0, 0);
            ret = ide_status(ide, ide_drives[ch ^ 1], ch);
            break;

        default:
            break;
    }

    ide_log("ide_readb(%04X, %08X) = %02X\n", addr, priv, ret);

    return ret;
}

uint8_t
ide_read_alt_status(UNUSED(const uint16_t addr), void *priv)
{
    const ide_board_t *dev = (ide_board_t *) priv;

    const int     ch  = dev->cur_dev;
    ide_t *       ide = ide_drives[ch];

    /* Per the Seagate ATA-3 specification:
       Reading the alternate status does *NOT* clear the IRQ. */
    const uint8_t ret = ide_status(ide, ide_drives[ch ^ 1], ch);

    ide_log("ide_read_alt_status(%04X, %08X) = %02X\n", addr, priv, ret);

    return ret;
}

uint16_t
ide_readw(uint16_t addr, void *priv)
{
    const ide_board_t *dev = (ide_board_t *) priv;
    const int          ch  = dev->cur_dev;
    ide_t *            ide = ide_drives[ch];
    uint16_t           ret;

    switch (addr & 0x7) {
        default:
            ret = ide_readb(addr, priv) | (ide_readb(addr + 1, priv) << 8);
            break;
        case 0x0: /* Data */
            ret = ide_read_data(ide);
            break;
        case 0x7:
            ret = ide_readb(addr, priv) | 0xff00;
            break;
    }

#if defined(ENABLE_IDE_LOG) && (ENABLE_IDE_LOG == 2)
    ide_log("ide_readw(%04X, %08X) = %04X\n", addr, priv, ret);
#endif
    return ret;
}

static uint32_t
ide_readl(uint16_t addr, void *priv)
{
    const ide_board_t *dev = (ide_board_t *) priv;
    const int          ch  = dev->cur_dev;
    ide_t *            ide = ide_drives[ch];
    uint32_t           ret;

    switch (addr & 0x7) {
        case 0x0: /* Data */
            ret = ide_read_data(ide);
            if (dev->bit32)
                ret |= (ide_read_data(ide) << 16);
            else
                ret |= (ide_readw(addr + 2, priv) << 16);
            break;
        case 0x6:
        case 0x7:
            ret = ide_readw(addr, priv) | 0xffff0000;
            break;
        default:
            ret = ide_readw(addr, priv) | (ide_readw(addr + 2, priv) << 16);
            break;
    }

#if defined(ENABLE_IDE_LOG) && (ENABLE_IDE_LOG == 2)
    ide_log("ide_readl(%04X, %08X) = %04X\n", addr, priv, ret);
#endif
    return ret;
}

static void
ide_board_callback(void *priv)
{
    ide_board_t *dev = (ide_board_t *) priv;
    ide_t *ide;

    ide_log("ide_board_callback(%i)\n", dev->cur_dev >> 1);

    dev->cur_dev &= ~1;

    /* Reset the devices in reverse so if there's a slave without a master,
       its copy of the master's task file gets reset first. */
    for (int8_t i = 1; i >= 0; i--) {
        ide = dev->ide[i];
        if (ide->type == IDE_ATAPI) {
            ide->tf->atastat = 0;
            if (IDE_ATAPI_IS_EARLY)
                ide->tf->atastat |= DRDY_STAT | DSC_STAT;
        } else
            ide->tf->atastat = DRDY_STAT | DSC_STAT;

        ide->reset = 0;
    }

    ide = dev->ide[0];
    if (dev->diag) {
        dev->diag = 0;
        if ((ide->type != IDE_ATAPI) || IDE_ATAPI_IS_EARLY)
            ide_irq_raise(ide);
    }
}

static void
atapi_error_no_ready(ide_t *ide)
{
    ide->command = 0;
    ide->tf->atastat = ERR_STAT | DSC_STAT;
    ide->tf->error   = ABRT_ERR;
    ide->tf->pos     = 0;

    ide_irq_raise(ide);
}

static void
ide_callback(void *priv)
{
    ide_t *         ide = (ide_t *) priv;
    const ide_bm_t *bm  = ide_boards[ide->board]->bm;
    int             chk_chs;
    int             ret;
    uint8_t         err = 0x00;

    ide_log("ide_callback(%i): %02X\n", ide->channel, ide->command);

    switch (ide->command) {
        case WIN_SEEK ... 0x7f:
            chk_chs = !ide->tf->lba;
            if (ide->type == IDE_ATAPI)
                atapi_error_no_ready(ide);
            else {
                /* The J-Bond PCI400C-A Phoenix BIOS implies that this command is supposed to
                   ignore the sector number. */
                if (chk_chs && ((ide->tf->cylinder >= ide->tracks) || (ide->tf->head >= ide->hpc)))
                    err = IDNF_ERR;
                else {
                    ide->tf->atastat = DRDY_STAT | DSC_STAT;
                    ide_irq_raise(ide);
                }
            }
            break;

        case WIN_RECAL ... 0x1f:
            if (ide->type == IDE_ATAPI)
                atapi_error_no_ready(ide);
            else {
                ide->tf->atastat = DRDY_STAT | DSC_STAT;
                ide_irq_raise(ide);
            }
            break;

        /* Initialize the Task File Registers as follows:
           Status = 00h, Error = 01h, Sector Count = 01h, Sector Number = 01h,
           Cylinder Low = 14h, Cylinder High = EBh and Drive/Head = 00h. */
        case WIN_SRST: /*ATAPI Device Reset */
            ide->tf->error     = 1; /*Device passed*/

            ide->tf->secount   = 1;
            ide->tf->sector    = 1;

            ide_set_signature(ide);

            ide->tf->atastat = DRDY_STAT | DSC_STAT;
            if (ide->type == IDE_ATAPI) {
                if (ide->device_reset)
                    ide->device_reset(ide->sc);
                if (!IDE_ATAPI_IS_EARLY)
                    ide->tf->atastat = 0;
            }

            ide_irq_raise(ide);

            if ((ide->type == IDE_ATAPI) && !IDE_ATAPI_IS_EARLY)
                ide->service = 0;
            break;

        case WIN_NOP:
        case WIN_STANDBYNOW1:
        case WIN_IDLENOW1:
        case WIN_SETIDLE1:
            ide->tf->atastat = DRDY_STAT | DSC_STAT;
            ide_irq_raise(ide);
            break;

        case WIN_CHECKPOWERMODE1:
        case WIN_SLEEP1:
            ide->tf->secount = 0xff;
            ide->tf->atastat = DRDY_STAT | DSC_STAT;
            ide_irq_raise(ide);
            break;

        case WIN_READ:
        case WIN_READ_NORETRY:
            if (ide->type == IDE_ATAPI) {
                ide_set_signature(ide);
                err = ABRT_ERR;
            } else if (!ide->tf->lba && (ide->cfg_spt == 0))
                err = IDNF_ERR;
            else {
                if (ide->do_initial_read) {
                    ide->do_initial_read = 0;
                    ide->sector_pos      = 0;
                    ret = hdd_image_read(ide->hdd_num, ide_get_sector(ide),
                                         ide->tf->secount ? ide->tf->secount : 256, ide->sector_buffer);
                } else
                    ret = 0;

                memcpy(ide->buffer, &ide->sector_buffer[ide->sector_pos * 512], 512);

                ide->sector_pos++;

                ide->tf->pos = 0;
                ide->tf->atastat = DRQ_STAT | DRDY_STAT | DSC_STAT;
                if (ret < 0) {
                    ide_log("IDE %i: Read aborted (image read error)\n", ide->channel);
                    err = UNC_ERR;
                }

                ide_irq_raise(ide);

                ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus_type, 1);
            }
            break;

        case WIN_READ_DMA:
        case WIN_READ_DMA_ALT:
            if ((ide->type == IDE_ATAPI) || ide_boards[ide->board]->force_ata3 || (bm == NULL)) {
                ide_log("IDE %i: DMA read aborted (bad device or board)\n", ide->channel);
                err = ABRT_ERR;
            } else if (!ide->tf->lba && (ide->cfg_spt == 0)) {
                ide_log("IDE %i: DMA read aborted (SPECIFY failed)\n", ide->channel);
                err = IDNF_ERR;
            } else {
                ide->sector_pos = 0;
                if (ide->tf->secount)
                    ide->sector_pos = ide->tf->secount;
                else
                    ide->sector_pos = 256;

                ide->tf->pos = 0;

                if (hdd_image_read(ide->hdd_num, ide_get_sector(ide), ide->sector_pos, ide->sector_buffer) < 0) {
                    ide_log("IDE %i: DMA read aborted (image read error)\n", ide->channel);
                    err = UNC_ERR;
                } else if (!ide_boards[ide->board]->force_ata3 && bm->dma) {
                    /* We should not abort - we should simply wait for the host to start DMA. */
                    ret = bm->dma(ide->sector_buffer, ide->sector_pos * 512, 0, bm->priv);
                    if (ret == 2) {
                        /* Bus master DMA disabled, simply wait for the host to enable DMA. */
                        ide->tf->atastat = DRQ_STAT | DRDY_STAT | DSC_STAT;
                        ide_set_callback(ide, 6.0 * IDE_TIME);
                        return;
                    } else if (ret == 1) {
                        /* DMA successful */
                        ide_log("IDE %i: DMA read successful\n", ide->channel);

                        ide->tf->atastat = DRDY_STAT | DSC_STAT;

                        ide_irq_raise(ide);
                        ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus_type, 0);
                    } else {
                        /* Bus master DMAS error, abort the command. */
                        ide_log("IDE %i: DMA read aborted (failed)\n", ide->channel);
                        err = ABRT_ERR;
                    }
                } else {
                    ide_log("IDE %i: DMA read aborted (no bus master)\n", ide->channel);
                    err = ABRT_ERR;
                }
            }
            break;

        case WIN_READ_MULTIPLE:
            /* According to the official ATA reference:

               If the Read Multiple command is attempted before the Set Multiple Mode
               command  has  been  executed  or  when  Read  Multiple  commands  are
               disabled, the Read Multiple operation is rejected with an Aborted Com-
               mand error. */
            if ((ide->type == IDE_ATAPI) || !ide->blocksize)
                err = ABRT_ERR;
            else if (!ide->tf->lba && (ide->cfg_spt == 0))
                err = IDNF_ERR;
            else {
                if (ide->do_initial_read) {
                    ide->do_initial_read = 0;
                    ide->sector_pos      = 0;
                    ret = hdd_image_read(ide->hdd_num, ide_get_sector(ide),
                                         ide->tf->secount ? ide->tf->secount : 256, ide->sector_buffer);
                } else {
                    ret = 0;
                }

                memcpy(ide->buffer, &ide->sector_buffer[ide->sector_pos * 512], 512);

                ide->sector_pos++;
                ide->tf->pos     = 0;

                ide->tf->atastat = DRQ_STAT | DRDY_STAT | DSC_STAT;
                if (ret < 0) {
                    ide_log("IDE %i: Read aborted (image read error)\n", ide->channel);
                    err = UNC_ERR;
                }
                if (!ide->blockcount)
                    ide_irq_raise(ide);
                ide->blockcount++;
                if (ide->blockcount >= ide->blocksize)
                    ide->blockcount = 0;
            }
            break;

        case WIN_WRITE:
        case WIN_WRITE_NORETRY:
#ifdef ENABLE_IDE_LOG
            off64_t sector = ide_get_sector(ide);
#endif
            if (ide->type == IDE_ATAPI)
                err = ABRT_ERR;
            else if (!ide->tf->lba && (ide->cfg_spt == 0))
                err = IDNF_ERR;
            else {
                ret = hdd_image_write(ide->hdd_num, ide_get_sector(ide), 1, (uint8_t *) ide->buffer);
                ide_irq_raise(ide);
                ide->tf->secount--;
                if (ide->tf->secount) {
                    ide->tf->atastat = DRQ_STAT | DRDY_STAT | DSC_STAT;
                    ide->tf->pos     = 0;
                    ide_next_sector(ide);
                    ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus_type, 1);
                } else {
                    ide->tf->atastat = DRDY_STAT | DSC_STAT;
                    ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus_type, 0);
                }
                if (ret < 0)
                    err = UNC_ERR;
            }
            ide_log("Write: %02X, %i, %08X, %" PRIi64 "\n", err, ide->hdd_num, ide->lba_addr, sector);
            break;

        case WIN_WRITE_DMA:
        case WIN_WRITE_DMA_ALT:
            if ((ide->type == IDE_ATAPI) || ide_boards[ide->board]->force_ata3 || (bm == NULL)) {
                ide_log("IDE %i: DMA write aborted (bad device type or board)\n", ide->channel);
                err = ABRT_ERR;
            } else if (!ide->tf->lba && (ide->cfg_spt == 0)) {
                ide_log("IDE %i: DMA write aborted (SPECIFY failed)\n", ide->channel);
                err = IDNF_ERR;
            } else {
                if (!ide_boards[ide->board]->force_ata3 && bm->dma) {
                    if (ide->tf->secount)
                        ide->sector_pos = ide->tf->secount;
                    else
                        ide->sector_pos = 256;

                    ret = bm->dma(ide->sector_buffer, ide->sector_pos * 512, 1, bm->priv);

                    if (ret == 2) {
                        /* Bus master DMA disabled, simply wait for the host to enable DMA. */
                        ide->tf->atastat = DRQ_STAT | DRDY_STAT | DSC_STAT;
                        ide_set_callback(ide, 6.0 * IDE_TIME);
                        return;
                    } else if (ret == 1) {
                        /* DMA successful */
                        ret = hdd_image_write(ide->hdd_num, ide_get_sector(ide),
                                              ide->sector_pos, ide->sector_buffer);

                        ide_log("IDE %i: DMA write %ssuccessful\n", ide->channel, (ret < 0) ? "un" : "");

                        ide->tf->atastat = DRDY_STAT | DSC_STAT;
                        if (ret < 0)
                            err = UNC_ERR;

                        ide_irq_raise(ide);
                        ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus_type, 0);
                    } else {
                        /* Bus master DMA error, abort the command. */
                        ide_log("IDE %i: DMA read aborted (failed)\n", ide->channel);
                        err = ABRT_ERR;
                    }
                } else {
                    ide_log("IDE %i: DMA write aborted (no bus master)\n", ide->channel);
                    err = ABRT_ERR;
                }
            }
            break;

        case WIN_WRITE_MULTIPLE:
            /* According to the official ATA reference:

               If the Read Multiple command is attempted before the Set Multiple Mode
               command  has  been  executed  or  when  Read  Multiple  commands  are
               disabled, the Read Multiple operation is rejected with an Aborted Com-
               mand error. */
            if ((ide->type == IDE_ATAPI) || !ide->blocksize)
                err = ABRT_ERR;
            else if (!ide->tf->lba && (ide->cfg_spt == 0))
                err = IDNF_ERR;
            else {
                ret = hdd_image_write(ide->hdd_num, ide_get_sector(ide), 1, (uint8_t *) ide->buffer);
                ide->blockcount++;
                if (ide->blockcount >= ide->blocksize || ide->tf->secount == 1) {
                    ide->blockcount = 0;
                    ide_irq_raise(ide);
                }
                ide->tf->secount--;
                if (ide->tf->secount) {
                    ide->tf->atastat = DRQ_STAT | DRDY_STAT | DSC_STAT;
                    ide->tf->pos     = 0;
                    ide_next_sector(ide);
                } else {
                    ide->tf->atastat = DRDY_STAT | DSC_STAT;
                    ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus_type, 0);
                }
                if (ret < 0)
                    err = UNC_ERR;
            }
            break;

        case WIN_VERIFY:
        case WIN_VERIFY_ONCE:
            if (ide->type == IDE_ATAPI)
                err = ABRT_ERR;
            else if (!ide->tf->lba && (ide->cfg_spt == 0))
                err = IDNF_ERR;
            else {
                ide->tf->pos     = 0;
                ide->tf->atastat = DRDY_STAT | DSC_STAT;
                ide_irq_raise(ide);
                ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus_type, 1);
            }
            break;

        case WIN_FORMAT:
            if (ide->type == IDE_ATAPI)
                err = ABRT_ERR;
            else if (!ide->tf->lba && (ide->cfg_spt == 0))
                err = IDNF_ERR;
            else {
                ret = hdd_image_zero(ide->hdd_num, ide_get_sector_format(ide), ide->tf->secount);

                ide->tf->atastat = DRDY_STAT | DSC_STAT;
                if (ret < 0)
                    err = UNC_ERR;
                ide_irq_raise(ide);

                ui_sb_update_icon(SB_HDD | hdd[ide->hdd_num].bus_type, 1);
            }
            break;

        case WIN_SPECIFY: /* Initialize Drive Parameters */
            if (ide->type == IDE_ATAPI)
                err = ABRT_ERR;
            else {
                /* Only accept after RESET or DIAG. */
                if (ide->params_specified) {
                    ide->cfg_spt = ide->tf->secount;
                    ide->cfg_hpc = ide->tf->head + 1;

                    ide->params_specified = 1;
                }
                ide->command = 0x00;
                ide->tf->atastat = DRDY_STAT | DSC_STAT;
                ide->tf->error   = 1;
                ide_irq_raise(ide);
            }
            break;

        case WIN_PIDENTIFY: /* Identify Packet Device */
            if (ide->type == IDE_ATAPI) {
                ide_identify(ide);
                ide->tf->pos     = 0;
                ide->tf->phase   = 2;
                ide->tf->error   = 0;
                ide->tf->atastat = DRQ_STAT | DRDY_STAT | DSC_STAT;
                ide_irq_raise(ide);
            } else
                err = ABRT_ERR;
            break;

        case WIN_SET_MULTIPLE_MODE:
            if ((ide->type == IDE_ATAPI) || (ide->tf->secount < 2) ||
                 (ide->tf->secount > hdd[ide->hdd_num].max_multiple_block))
                err = ABRT_ERR;
            else {
                ide->blocksize     = ide->tf->secount;
                ide->tf->atastat   = DRDY_STAT | DSC_STAT;

                ide_irq_raise(ide);
            }
            break;

        case WIN_SET_FEATURES:
            if ((ide->type == IDE_NONE) || !ide_set_features(ide))
                err = ABRT_ERR;
            else {
                ide->tf->atastat = DRDY_STAT | DSC_STAT;

                if (ide->type == IDE_ATAPI)
                    ide->tf->pos     = 0;

                ide_irq_raise(ide);
            }
            break;

        case WIN_READ_NATIVE_MAX:
            if (ide->type == IDE_HDD) {
                int snum = hdd[ide->hdd_num].spt;
                snum *= hdd[ide->hdd_num].hpc;
                snum *= hdd[ide->hdd_num].tracks;
                ide_set_sector(ide, snum - 1);
                ide->tf->atastat = DRDY_STAT | DSC_STAT;
                ide_irq_raise(ide);
            } else
                err = ABRT_ERR;
            break;

        case WIN_IDENTIFY: /* Identify Device */
            if (ide->type == IDE_HDD) {
                ide_identify(ide);
                ide->tf->pos     = 0;
                ide->tf->atastat = DRQ_STAT | DRDY_STAT | DSC_STAT;
                ide_irq_raise(ide);
            } else {
                ide_set_signature(ide);
                err = ABRT_ERR;
            }
            break;

        case WIN_PACKETCMD: /* ATAPI Packet */
            if (ide->type == IDE_ATAPI)
                ide_atapi_callback(ide);
            else
                err = ABRT_ERR;
            break;

        default:
        case 0xff:
            err = ABRT_ERR;
            break;
    }

    if (err != 0x00) {
        ide->tf->atastat = DRDY_STAT | ERR_STAT | DSC_STAT;
        ide->tf->error   = err;

        ide->tf->pos    = 0;

        ide_irq_raise(ide);
    }
}

uint8_t
ide_read_ali_75(void)
{
    const ide_t *ide0;
    const ide_t *ide1;
    int          ch0;
    int          ch1;
    uint8_t      ret = 0x00;

    ch0  = ide_boards[0]->cur_dev;
    ch1  = ide_boards[1]->cur_dev;
    ide0 = ide_drives[ch0];
    ide1 = ide_drives[ch1];

    if (ch1)
        ret |= 0x08;
    if (ch0)
        ret |= 0x04;
    if (ide1->irqstat)
        ret |= 0x02;
    if (ide0->irqstat)
        ret |= 0x01;

    return ret;
}

uint8_t
ide_read_ali_76(void)
{
    const ide_t  *ide0;
    const ide_t  *ide1;
    int           ch0;
    int           ch1;
    uint8_t       ret = 0x00;

    ch0  = ide_boards[0]->cur_dev;
    ch1  = ide_boards[1]->cur_dev;
    ide0 = ide_drives[ch0];
    ide1 = ide_drives[ch1];

    if (ide1->tf->atastat & BSY_STAT)
        ret |= 0x40;
    if (ide1->tf->atastat & DRQ_STAT)
        ret |= 0x20;
    if (ide1->tf->atastat & ERR_STAT)
        ret |= 0x10;
    if (ide0->tf->atastat & BSY_STAT)
        ret |= 0x04;
    if (ide0->tf->atastat & DRQ_STAT)
        ret |= 0x02;
    if (ide0->tf->atastat & ERR_STAT)
        ret |= 0x01;

    return ret;
}

void
ide_handlers(uint8_t board, int set)
{
    if (ide_boards[board] != NULL) {
        if (ide_boards[board]->base[0]) {
            io_handler(set, ide_boards[board]->base[0], 8,
                       ide_readb, ide_readw, ide_readl,
                       ide_writeb, ide_writew, ide_writel,
                       ide_boards[board]);
        }

        if (ide_boards[board]->base[1]) {
            io_handler(set, ide_boards[board]->base[1], 1,
                       ide_read_alt_status, NULL, NULL,
                       ide_write_devctl, NULL, NULL,
                       ide_boards[board]);
       }
    }
}

void
ide_set_base_addr(int board, int base, uint16_t port)
{
    ide_log("ide_set_base_addr(%i, %i, %04X)\n", board, base, port);

    if (ide_boards[board] != NULL)
        ide_boards[board]->base[base] = port;
}

void
ide_set_irq(int board, int irq)
{
    ide_log("ide_set_irq(%i, %i)\n", board, irq);

    if (ide_boards[board] != NULL)
        ide_boards[board]->irq = irq;
}

static void
ide_clear_bus_master(int board)
{
    ide_bm_t *bm = ide_boards[board]->bm;

    if (bm != NULL) {
        free(bm);
        ide_boards[board]->bm = NULL;
    }
}

/*
   This so drives can be forced to ATA-3 (no DMA) for machines that hide the
   on-board PCI IDE controller (eg. Packard Bell PB640 and ASUS P/I-P54TP4XE),
   breaking DMA drivers unless this is done.
 */
extern void
ide_board_set_force_ata3(int board, int force_ata3)
{
    ide_log("ide_board_set_force_ata3(%i, %i)\n", board, force_ata3);

    if ((ide_boards[board] != NULL) && ide_boards[board]->inited)
        ide_boards[board]->force_ata3 = force_ata3;
}

static void
ide_board_close(int board)
{
    ide_t *dev;
    int    c;

    ide_log("ide_board_close(%i)\n", board);

    if ((ide_boards[board] == NULL) || !ide_boards[board]->inited)
        return;

    ide_log("IDE: Closing board %i...\n", board);

    timer_stop(&ide_boards[board]->timer);

    ide_clear_bus_master(board);

    /* Close hard disk image files (if previously open) */
    for (uint8_t d = 0; d < 2; d++) {
        c = (board << 1) + d;

        ide_boards[board]->ide[d] = NULL;

        dev = ide_drives[c];

        if (dev != NULL) {
            if ((dev->type == IDE_HDD) && (dev->hdd_num != -1))
                hdd_image_close(dev->hdd_num);

            if (dev->type == IDE_ATAPI)
                dev->tf->atastat = DRDY_STAT | DSC_STAT;
            else if (!(dev->type & IDE_SHADOW) && (dev->tf != NULL)) {
                free(dev->tf);
                dev->tf = NULL;
            }

            if (dev->buffer) {
                free(dev->buffer);
                dev->buffer = NULL;
            }

            if (dev->sector_buffer) {
                free(dev->sector_buffer);
                dev->buffer = NULL;
            }

            free(dev);
            ide_drives[c] = NULL;
        }
    }

    free(ide_boards[board]);
    ide_boards[board] = NULL;
}

static void
ide_board_setup(const int board)
{
    const int min_ch = (board << 1);
    const int max_ch = min_ch + 1;
    int       c;
    int       d;

    ide_log("IDE: board %i: loading disks...\n", board);
    for (d = 0; d < 2; d++) {
        c = (board << 1) + d;
        ide_zero(c);
    }

    c = 0;
    for (d = 0; d < HDD_NUM; d++) {
        const int is_ide   = (hdd[d].bus_type == HDD_BUS_IDE);
        const int ch       = hdd[d].ide_channel;

        const int valid_ch = ((ch >= min_ch) && (ch <= max_ch));

        if (is_ide && valid_ch) {
            ide_log("Found IDE hard disk on channel %i\n", ch);
            loadhd(ide_drives[ch], d, hdd[d].fn);
            if (ide_drives[ch]->sector_buffer == NULL)
                ide_drives[ch]->sector_buffer = (uint8_t *) calloc(1, 256 * 512);
            if (++c >= 2)
                break;
        }
    }
    ide_log("IDE: board %i: done, loaded %d disks.\n", board, c);

    for (d = 0; d < 2; d++) {
        c          = (board << 1) + d;
        ide_t *dev = ide_drives[c];

        if (dev->type == IDE_NONE)
            continue;

        ide_allocate_buffer(dev);

        ide_set_signature(dev);

        dev->mdma_mode = (1 << ide_get_max(dev, TYPE_PIO));
        dev->tf->error = 1;
        if (dev->type != IDE_HDD)
            dev->cfg_spt = dev->cfg_hpc = 0;
        if (dev->type == IDE_HDD) {
            dev->blocksize = hdd[dev->hdd_num].max_multiple_block;

            /* Calculate the default heads and sectors. */
            uint32_t d_hpc    = dev->hpc;
            uint32_t d_spt    = dev->spt;

            if ((dev->hpc > 16) || (dev->spt > 63)) {
                /* HPC > 16, convert to 16 HPC. */
                if (dev->hpc > 16)
                    d_hpc = 16;
                if (dev->spt > 63)
                    d_spt = 63;
            }

            dev->cfg_spt = d_spt;
            dev->cfg_hpc = d_hpc;
        }

        dev->params_specified = 0;
    }
}

static void
ide_board_init(int board, int irq, int base_main, int side_main, int type, int bus)
{
    ide_log("ide_board_init(%i, %i, %04X, %04X, %i, %i)\n", board, irq, base_main, side_main, type, bus);

    if ((ide_boards[board] != NULL) && ide_boards[board]->inited)
        return;

    ide_log("IDE: Initializing board %i...\n", board);

    if (ide_boards[board] == NULL)
        ide_boards[board] = (ide_board_t *) calloc(1, sizeof(ide_board_t));

    ide_boards[board]->irq     = irq;
    ide_boards[board]->cur_dev = board << 1;
    if (type & 6)
        ide_boards[board]->bit32 = 1;
    ide_boards[board]->base[0] = base_main;
    ide_boards[board]->base[1] = side_main;

    if (!(bus & DEVICE_MCA))
        ide_set_handlers(board);

    timer_add(&ide_boards[board]->timer, ide_board_callback, ide_boards[board], 0);

    ide_board_setup(board);

    ide_boards[board]->inited = 1;
}

/* Needed for ESS ES1688/968 PnP. */
void
ide_pnp_config_changed_1addr(uint8_t ld, isapnp_device_config_t *config, void *priv)
{
    intptr_t board = (intptr_t) priv;

    if (ld)
        return;

    if (ide_boards[board]->base[0] || ide_boards[board]->base[1]) {
        ide_remove_handlers(board);
        ide_boards[board]->base[0] = ide_boards[board]->base[1] = 0;
    }

    ide_boards[board]->irq = -1;

    if (config->activate) {
        ide_boards[board]->base[0] = (config->io[0].base != ISAPNP_IO_DISABLED) ?
                                     config->io[0].base : 0x0000;
        ide_boards[board]->base[1] = (config->io[0].base != ISAPNP_IO_DISABLED) ?
                                     (config->io[0].base + 0x0206) : 0x0000;

        if (ide_boards[board]->base[0] && ide_boards[board]->base[1])
            ide_set_handlers(board);

        if (config->irq[0].irq != ISAPNP_IRQ_DISABLED)
            ide_boards[board]->irq = config->irq[0].irq;
    }
}

void
ide_pnp_config_changed(uint8_t ld, isapnp_device_config_t *config, void *priv)
{
    intptr_t board = (intptr_t) priv;

    if (ld)
        return;

    if (ide_boards[board]->base[0] || ide_boards[board]->base[1]) {
        ide_remove_handlers(board);
        ide_boards[board]->base[0] = ide_boards[board]->base[1] = 0;
    }

    ide_boards[board]->irq = -1;

    if (config->activate) {
        ide_boards[board]->base[0] = (config->io[0].base != ISAPNP_IO_DISABLED) ?
                                     config->io[0].base : 0x0000;
        ide_boards[board]->base[1] = (config->io[1].base != ISAPNP_IO_DISABLED) ?
                                     config->io[1].base : 0x0000;

        if (ide_boards[board]->base[0] && ide_boards[board]->base[1])
            ide_set_handlers(board);

        if (config->irq[0].irq != ISAPNP_IRQ_DISABLED)
            ide_boards[board]->irq = config->irq[0].irq;
    }
}

static void *
ide_sec_init(const device_t *info)
{
    /* Don't claim this channel again if it was already claimed. */
    if (ide_boards[1])
        return (NULL);

    ide_board_init(1, HDC_SECONDARY_IRQ, HDC_SECONDARY_BASE, HDC_SECONDARY_SIDE, info->local, info->flags);

    return (ide_boards[1]);
}

/* Close a standalone IDE unit. */
static void
ide_sec_close(UNUSED(void *priv))
{
    ide_board_close(1);
}

static void *
ide_ter_init(const device_t *info)
{
    /* Don't claim this channel again if it was already claimed. */
    if (ide_boards[2])
        return (NULL);

    int irq;
    if (info->local)
        irq = -2;
    else
        irq = device_get_config_int("irq");

    if (irq < 0) {
        ide_board_init(2, -1, 0, 0, 0, 0);
        if (irq == -1)
            isapnp_add_card(ide_ter_pnp_rom, sizeof(ide_ter_pnp_rom),
                            ide_pnp_config_changed, NULL, NULL, NULL, (void *) 2);
    } else
        ide_board_init(2, irq, HDC_TERTIARY_BASE, HDC_TERTIARY_SIDE, 0, 0);

    return (ide_boards[2]);
}

/* Close a standalone IDE unit. */
static void
ide_ter_close(UNUSED(void *priv))
{
    ide_board_close(2);
}

static void *
ide_qua_init(const device_t *info)
{
    /* Don't claim this channel again if it was already claimed. */
    if (ide_boards[3])
        return (NULL);

    int irq;
    if (info->local)
        irq = -2;
    else
        irq = device_get_config_int("irq");

    if (irq < 0) {
        ide_board_init(3, -1, 0, 0, 0, 0);
        if (irq == -1)
            isapnp_add_card(ide_qua_pnp_rom, sizeof(ide_qua_pnp_rom),
                            ide_pnp_config_changed, NULL, NULL, NULL, (void *) 3);
    } else
        ide_board_init(3, irq, HDC_QUATERNARY_BASE, HDC_QUATERNARY_SIDE, 0, 0);

    return (ide_boards[3]);
}

/* Close a standalone IDE unit. */
static void
ide_qua_close(UNUSED(void *priv))
{
    ide_board_close(3);
}

void *
ide_xtide_init(void)
{
    ide_board_init(0, -1, 0, 0, 0, 0);

    return ide_boards[0];
}

void
ide_xtide_close(void)
{
    ide_board_close(0);
}

void
ide_set_bus_master(int board,
                   int (*dma)(uint8_t *data, int transfer_length, int out, void *priv),
                   void (*set_irq)(uint8_t status, void *priv), void *priv)
{
    ide_bm_t *bm;

    if (ide_boards[board]->bm == NULL) {
        bm = (ide_bm_t *) calloc(1, sizeof(ide_bm_t));
        ide_boards[board]->bm = bm;
    } else
        bm = ide_boards[board]->bm;

    bm->dma     = dma;
    bm->set_irq = set_irq;
    bm->priv    = priv;
}

static void *
ide_init(const device_t *info)
{
    ide_log("Initializing IDE...\n");

    switch (info->local) {
        case 0 ... 5:
            ide_board_init(0, HDC_PRIMARY_IRQ, HDC_PRIMARY_BASE, HDC_PRIMARY_SIDE, info->local, info->flags);

            if (info->local & 1)
                ide_board_init(1, HDC_SECONDARY_IRQ, HDC_SECONDARY_BASE, HDC_SECONDARY_SIDE, info->local, info->flags);
            break;

        default:
            break;
    }

    return (void *) (intptr_t) -1;
}

static void
ide_drive_reset(int d)
{
    ide_log("Resetting IDE drive %i...\n", d);

    if ((d & 1) && (ide_drives[d]->type == IDE_NONE) && (ide_drives[d ^ 1]->type != IDE_NONE)) {
        ide_drives[d]->type = ide_drives[d ^ 1]->type | IDE_SHADOW;
        free(ide_drives[d]->tf);
        ide_drives[d]->tf = ide_drives[d ^ 1]->tf;
    } else
        ide_drives[d]->tf->atastat  = DRDY_STAT | DSC_STAT;

    ide_drives[d]->channel      = d;
    ide_drives[d]->service      = 0;
    ide_drives[d]->board        = d >> 1;
    ide_drives[d]->selected     = !(d & 1);
    timer_stop(&ide_drives[d]->timer);

    if (ide_boards[d >> 1]) {
        ide_boards[d >> 1]->cur_dev = d & ~1;
        timer_stop(&ide_boards[d >> 1]->timer);
    }

    ide_set_signature(ide_drives[d]);

    if (ide_drives[d]->sector_buffer)
        memset(ide_drives[d]->sector_buffer, 0, 256 * 512);

    if (ide_drives[d]->buffer)
        memset(ide_drives[d]->buffer, 0, 65536 * sizeof(uint16_t));
}

static void
ide_board_reset(int board)
{
    int min;
    int max;

    ide_log("Resetting IDE board %i...\n", board);

    timer_stop(&ide_boards[board]->timer);

    min = (board << 1);
    max = min + 2;

    for (int d = min; d < max; d++)
        ide_drive_reset(d);
}

void
ide_drives_set_shadow(void)
{
    for (uint8_t d = 0; d < IDE_NUM; d++) {
        if (ide_drives[d] == NULL)
            continue;

        if ((d & 1) && (ide_drives[d]->type == IDE_NONE) && (ide_drives[d ^ 1]->type != IDE_NONE)) {
            ide_drives[d]->type = ide_drives[d ^ 1]->type | IDE_SHADOW;
            if (ide_drives[d]->tf != NULL)
                free(ide_drives[d]->tf);
            ide_drives[d]->tf = ide_drives[d ^ 1]->tf;
        }
    }
}

/* Reset a standalone IDE unit. */
static void
ide_reset(UNUSED(void *priv))
{
    ide_log("Resetting IDE...\n");

    for (uint8_t i = 0; i < 2; i++) {
        if (ide_boards[i] != NULL)
            ide_board_reset(i);
    }
}

/* Close a standalone IDE unit. */
static void
ide_close(UNUSED(void *priv))
{
    ide_log("Closing IDE...\n");

    for (uint8_t i = 0; i < 2; i++) {
        if (ide_boards[i] != NULL) {
            ide_board_close(i);
            ide_boards[i] = NULL;
        }
    }
}

static uint8_t
mcide_mca_read(const int port, void *priv)
{
    const mcide_t *dev = (mcide_t *) priv;

    ide_log("IDE: mcard(%04x)\n", port);

    return (dev->pos_regs[port & 7]);
}

static void
mcide_mca_write(const int port, const uint8_t val, void *priv)
{
    mcide_t *dev      = (mcide_t *) priv;
    uint16_t bases[4] = { HDC_PRIMARY_BASE, HDC_SECONDARY_BASE, HDC_TERTIARY_BASE, HDC_QUATERNARY_BASE };
    int irqs[4]       = { HDC_QUATERNARY_IRQ, HDC_TERTIARY_IRQ, HDC_PRIMARY_IRQ, HDC_SECONDARY_IRQ };

    if ((port >= 0x102) && (dev->pos_regs[port & 7] != val)) {
        ide_log("IDE: mcawr(%04x, %02x)  pos[2]=%02x pos[3]=%02x\n",
                port, val, dev->pos_regs[2], dev->pos_regs[3]);

        /* Save the new value. */
        dev->pos_regs[port & 7] = val;

        mem_mapping_disable(&dev->bios_rom.mapping);
        dev->bios_addr          = 0x00000000;

        ide_remove_handlers(0);
        ide_boards[0]->base[0]  = ide_boards[0]->base[1] = 0x0000;

        ide_boards[0]->irq      = -1;

        ide_remove_handlers(1);
        ide_boards[1]->base[0]  = ide_boards[1]->base[1] = 0x0000;

        ide_boards[1]->irq      = -1;

        if (dev->pos_regs[2] & 1) {
            if (dev->pos_regs[2] & 0x80)
                dev->bios_addr = 0x000c0000 + (0x00004000 * (uint32_t) ((dev->pos_regs[2] >> 4) & 0x07));

            if (dev->pos_regs[3] & 0x08) {
                ide_boards[0]->base[0] = bases[dev->pos_regs[3] & 0x03];
                ide_boards[0]->base[1] = bases[dev->pos_regs[3] & 0x03] + 0x0206;
            }

            if (dev->pos_regs[3] & 0x80)
                ide_boards[0]->irq = irqs[(dev->pos_regs[3] >> 4) & 0x03];

            if (dev->pos_regs[4] & 0x08) {
                ide_boards[1]->base[0] = bases[dev->pos_regs[4] & 0x03];
                ide_boards[1]->base[1] = bases[dev->pos_regs[4] & 0x03] + 0x0206;
            }

            if (dev->pos_regs[4] & 0x80)
                ide_boards[1]->irq = irqs[(dev->pos_regs[4] >> 4) & 0x03];

            ide_set_handlers(0);

            ide_set_handlers(1);

            if (dev->bios_addr)
                mem_mapping_set_addr(&dev->bios_rom.mapping, dev->bios_addr, 0x00004000);

            /* Say hello. */
            ide_log("McIDE: Primary Master I/O=%03X, Primary IRQ=%02i, "
                    "Secondary Master I/O=%03X, Secondary IRQ=%02i, "
                    "BIOS @%05X\n",
                    ide_boards[0]->base[0], ide_boards[0]->irq,
                    ide_boards[1]->base[0], ide_boards[1]->irq,
                    dev->bios_addr);
        }
    }
}

static uint8_t
mcide_mca_feedb(void *priv)
{
    const mcide_t *dev = (mcide_t *) priv;

    return (dev->pos_regs[2] & 1);
}

static void
mcide_mca_reset(void *priv)
{
    mcide_t *dev = (mcide_t *) priv;

    for (uint8_t i = 0; i < 2; i++) {
        if (ide_boards[i] != NULL)
            ide_board_reset(i);
    }

    ide_log("McIDE: MCA Reset.\n");
    mem_mapping_disable(&dev->bios_rom.mapping);
    mcide_mca_write(0x102, 0, dev);
}

static void
mcide_reset(UNUSED(void *priv))
{
    for (uint8_t i = 0; i < 2; i++) {
        if (ide_boards[i] != NULL)
            ide_board_reset(i);
    }

    ide_log("McIDE: Reset.\n");
}

static void *
mcide_init(const device_t *info)
{
    ide_log("Initializing McIDE...\n");
    mcide_t *dev = (mcide_t *) calloc(1, sizeof(mcide_t));

    ide_board_init(0, -1, 0, 0, info->local, info->flags);
    ide_board_init(1, -1, 0, 0, info->local, info->flags);

    rom_init(&dev->bios_rom, ROM_PATH_MCIDE,
                         0xc8000, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);
    mem_mapping_disable(&dev->bios_rom.mapping);

    /* Set the MCA ID for this controller, 0xF171. */
    dev->pos_regs[0] = 0xf1;
    dev->pos_regs[1] = 0x71;

    /* Enable the device. */
    mca_add(mcide_mca_read, mcide_mca_write, mcide_mca_feedb, mcide_mca_reset, dev);

    return dev;
}

static int
mcide_available(void)
{
    return (rom_present(ROM_PATH_MCIDE));
}

static void
mcide_close(void *priv)
{
    mcide_t *dev = (mcide_t *) priv;

    for (uint8_t i = 0; i < 2; i++) {
        if (ide_boards[i] != NULL) {
            ide_board_close(i);
            ide_boards[i] = NULL;
        }
    }

    free(dev);
}

const device_t ide_isa_device = {
    .name          = "ISA PC/AT IDE Controller",
    .internal_name = "ide_isa",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = 0,
    .init          = ide_init,
    .close         = ide_close,
    .reset         = ide_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ide_isa_sec_device = {
    .name          = "ISA PC/AT IDE Controller (Secondary)",
    .internal_name = "ide_isa_sec",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = 0,
    .init          = ide_sec_init,
    .close         = ide_sec_close,
    .reset         = ide_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ide_isa_2ch_device = {
    .name          = "ISA PC/AT IDE Controller (Dual-Channel)",
    .internal_name = "ide_isa_2ch",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = 1,
    .init          = ide_init,
    .close         = ide_close,
    .reset         = ide_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ide_vlb_device = {
    .name          = "VLB IDE Controller",
    .internal_name = "ide_vlb",
    .flags         = DEVICE_VLB | DEVICE_AT,
    .local         = 2,
    .init          = ide_init,
    .close         = ide_close,
    .reset         = ide_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ide_vlb_sec_device = {
    .name          = "VLB IDE Controller (Secondary)",
    .internal_name = "ide_vlb_sec",
    .flags         = DEVICE_VLB | DEVICE_AT,
    .local         = 2,
    .init          = ide_sec_init,
    .close         = ide_sec_close,
    .reset         = ide_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ide_vlb_2ch_device = {
    .name          = "VLB IDE Controller (Dual-Channel)",
    .internal_name = "ide_vlb_2ch",
    .flags         = DEVICE_VLB | DEVICE_AT,
    .local         = 3,
    .init          = ide_init,
    .close         = ide_close,
    .reset         = ide_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ide_pci_device = {
    .name          = "PCI IDE Controller",
    .internal_name = "ide_pci",
    .flags         = DEVICE_PCI | DEVICE_AT,
    .local         = 4,
    .init          = ide_init,
    .close         = ide_close,
    .reset         = ide_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ide_pci_sec_device = {
    .name          = "PCI IDE Controller (Secondary)",
    .internal_name = "ide_pci_sec",
    .flags         = DEVICE_PCI | DEVICE_AT,
    .local         = 4,
    .init          = ide_sec_init,
    .close         = ide_sec_close,
    .reset         = ide_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ide_pci_2ch_device = {
    .name          = "PCI IDE Controller (Dual-Channel)",
    .internal_name = "ide_pci_2ch",
    .flags         = DEVICE_PCI | DEVICE_AT,
    .local         = 5,
    .init          = ide_init,
    .close         = ide_close,
    .reset         = ide_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t mcide_device = {
    .name          = "MCA McIDE Controller",
    .internal_name = "ide_mcide",
    .flags         = DEVICE_MCA,
    .local         = 3,
    .init          = mcide_init,
    .close         = mcide_close,
    .reset         = mcide_reset,
    .available     = mcide_available,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

// clang-format off
static const device_config_t ide_ter_config[] = {
    {
        .name           = "irq",
        .description    = "IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = HDC_TERTIARY_IRQ,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Plug and Play", .value = -1 },
            { .description = "IRQ 2",         .value =  2 },
            { .description = "IRQ 3",         .value =  3 },
            { .description = "IRQ 4",         .value =  4 },
            { .description = "IRQ 5",         .value =  5 },
            { .description = "IRQ 7",         .value =  7 },
            { .description = "IRQ 9",         .value =  9 },
            { .description = "IRQ 10",        .value = 10 },
            { .description = "IRQ 11",        .value = 11 },
            { .description = "IRQ 12",        .value = 12 },
            { .description = ""                           }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t ide_qua_config[] = {
    {
        .name           = "irq",
        .description    = "IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = HDC_QUATERNARY_IRQ,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Plug and Play", .value = -1 },
            { .description = "IRQ 2",         .value =  2 },
            { .description = "IRQ 3",         .value =  3 },
            { .description = "IRQ 4",         .value =  4 },
            { .description = "IRQ 5",         .value =  5 },
            { .description = "IRQ 7",         .value =  7 },
            { .description = "IRQ 9",         .value =  9 },
            { .description = "IRQ 10",        .value = 10 },
            { .description = "IRQ 11",        .value = 11 },
            { .description = "IRQ 12",        .value = 12 },
            { .description = ""                           }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t ide_ter_device = {
    .name          = "Tertiary IDE Controller",
    .internal_name = "ide_ter",
    .flags         = DEVICE_AT,
    .local         = 0,
    .init          = ide_ter_init,
    .close         = ide_ter_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ide_ter_config
};

const device_t ide_ter_pnp_device = {
    .name          = "Tertiary IDE Controller (Plug and Play only)",
    .internal_name = "ide_ter_pnp",
    .flags         = DEVICE_AT,
    .local         = 1,
    .init          = ide_ter_init,
    .close         = ide_ter_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ide_qua_device = {
    .name          = "Quaternary IDE Controller",
    .internal_name = "ide_qua",
    .flags         = DEVICE_AT,
    .local         = 0,
    .init          = ide_qua_init,
    .close         = ide_qua_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ide_qua_config
};

const device_t ide_qua_pnp_device = {
    .name          = "Quaternary IDE Controller (Plug and Play only)",
    .internal_name = "ide_qua_pnp",
    .flags         = DEVICE_AT,
    .local         = 1,
    .init          = ide_qua_init,
    .close         = ide_qua_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
