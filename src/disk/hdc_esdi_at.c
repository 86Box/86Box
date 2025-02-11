/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Driver for the ESDI controller (WD1007-vse1) for PC/AT.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2017-2019 Fred N. van Kempen.
 */
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pic.h>
#include <86box/rom.h>
#include "cpu.h"
#include <86box/machine.h>
#include <86box/timer.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/hdc.h>
#include <86box/hdd.h>

#define HDC_TIME            10.0
#define BIOS_FILE           "roms/hdd/esdi_at/62-000279-061.bin"

#define STAT_ERR            0x01
#define STAT_INDEX          0x02
#define STAT_CORRECTED_DATA 0x04
#define STAT_DRQ            0x08 /* Data request */
#define STAT_DSC            0x10
#define STAT_SEEK_COMPLETE  0x20
#define STAT_READY          0x40
#define STAT_BUSY           0x80

#define ERR_DAM_NOT_FOUND   0x01 /* Data Address Mark not found */
#define ERR_TR000           0x02 /* track 0 not found */
#define ERR_ABRT            0x04 /* command aborted */
#define ERR_ID_NOT_FOUND    0x10 /* ID not found */
#define ERR_DATA_CRC        0x40 /* data CRC error */
#define ERR_BAD_BLOCK       0x80 /* bad block detected */

#define CMD_NOP             0x00
#define CMD_RESTORE         0x10
#define CMD_READ            0x20
#define CMD_WRITE           0x30
#define CMD_VERIFY          0x40
#define CMD_FORMAT          0x50
#define CMD_SEEK            0x70
#define CMD_DIAGNOSE        0x90
#define CMD_SET_PARAMETERS  0x91
#define CMD_READ_PARAMETERS 0xec

typedef struct drive_t {
    int cfg_spt;
    int cfg_hpc;
    int current_cylinder;
    int real_spt;
    int real_hpc;
    int real_tracks;
    int present;
    int hdd_num;
} drive_t;

typedef struct esdi_t {
    uint8_t status;
    uint8_t error;
    int     secount;
    int     sector;
    int     cylinder;
    int     head;
    int     cylprecomp;
    uint8_t command;
    uint8_t fdisk;
    int     pos;

    int      drive_sel;
    int      reset;
    uint16_t buffer[256];
    int      irqstat;

    pc_timer_t callback_timer;

    drive_t drives[2];

    rom_t bios_rom;
} esdi_t;

static uint8_t esdi_read(uint16_t port, void *priv);
static void    esdi_write(uint16_t port, uint8_t val, void *priv);

#ifdef ENABLE_ESDI_AT_LOG
int esdi_at_do_log = ENABLE_ESDI_AT_LOG;

static void
esdi_at_log(const char *fmt, ...)
{
    va_list ap;

    if (esdi_at_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define esdi_at_log(fmt, ...)
#endif

static __inline void
irq_raise(esdi_t *esdi)
{
    esdi->irqstat = 1;
    if (!(esdi->fdisk & 2))
        picint_common(1 << 14, PIC_IRQ_EDGE, 1, NULL);
}

static __inline void
irq_lower(esdi_t *esdi)
{
    esdi->irqstat = 0;
    if (!(esdi->fdisk & 2))
        picint_common(1 << 14, PIC_IRQ_EDGE, 0, NULL);
}

static __inline void
irq_update(esdi_t *esdi)
{
    uint8_t set = !(esdi->fdisk & 2) && esdi->irqstat;
    picint_common(1 << 14, PIC_IRQ_EDGE, set, NULL);
}

static void
esdi_set_callback(esdi_t *esdi, double callback)
{
    if (!esdi) {
        esdi_at_log("esdi_set_callback(NULL): Set callback failed\n");
        return;
    }

    if (callback == 0.0)
        timer_stop(&esdi->callback_timer);
    else
        timer_on_auto(&esdi->callback_timer, callback);
}

double
esdi_get_xfer_time(UNUSED(esdi_t *esdi), int size)
{
    /* 390.625 us per sector at 10 Mbit/s = 1280 kB/s. */
    return (3125.0 / 8.0) * (double) size;
}

/* Return the sector offset for the current register values. */
static int
get_sector(esdi_t *esdi, off64_t *addr)
{
    const drive_t *drive   = &esdi->drives[esdi->drive_sel];
    int            heads   = drive->cfg_hpc;
    int            sectors = drive->cfg_spt;
    int            c;
    int            h;
    int            s;
    int            sector;

    if (esdi->head > heads) {
        esdi_at_log("esdi_get_sector: past end of configured heads\n");
        return 1;
    }

    if (esdi->sector >= sectors + 1) {
        esdi_at_log("esdi_get_sector: past end of configured sectors\n");
        return 1;
    }

    sector = esdi->sector ? esdi->sector : 1;

    if (drive->cfg_spt == drive->real_spt && drive->cfg_hpc == drive->real_hpc) {
        *addr = ((((off64_t) esdi->cylinder * heads) + esdi->head) * sectors) + (sector - 1);
    } else {
        /*
         * When performing translation, the firmware seems to leave 1
         * sector per track inaccessible (spare sector)
         */

        *addr = ((((off64_t) esdi->cylinder * heads) + esdi->head) * sectors) + (sector - 1);

        s = *addr % (drive->real_spt - 1);
        h = (*addr / (drive->real_spt - 1)) % drive->real_hpc;
        c = (*addr / (drive->real_spt - 1)) / drive->real_hpc;

        *addr = ((((off64_t) c * drive->real_hpc) + h) * drive->real_spt) + s;
    }

    return 0;
}

static int
get_sector_format(esdi_t *esdi, off64_t *addr)
{
    const drive_t *drive   = &esdi->drives[esdi->drive_sel];
    int            heads   = drive->cfg_hpc;
    int            sectors = drive->cfg_spt;
    int            c;
    int            h;
    int            s;

    if (esdi->head > heads) {
        esdi_at_log("esdi_get_sector: past end of configured heads\n");
        return 1;
    }

    if (drive->cfg_spt == drive->real_spt && drive->cfg_hpc == drive->real_hpc) {
        *addr = ((((off64_t) esdi->cylinder * heads) + esdi->head) * sectors);
    } else {
        /*
         * When performing translation, the firmware seems to leave 1
         * sector per track inaccessible (spare sector)
         */

        *addr = ((((off64_t) esdi->cylinder * heads) + esdi->head) * sectors);

        s = *addr % (drive->real_spt - 1);
        h = (*addr / (drive->real_spt - 1)) % drive->real_hpc;
        c = (*addr / (drive->real_spt - 1)) / drive->real_hpc;

        *addr = ((((off64_t) c * drive->real_hpc) + h) * drive->real_spt) + s;
    }

    return 0;
}

/* Move to the next sector using CHS addressing. */
static void
next_sector(esdi_t *esdi)
{
    drive_t *drive = &esdi->drives[esdi->drive_sel];

    esdi->sector++;
    if (esdi->sector == (drive->cfg_spt + 1)) {
        esdi->sector = 1;
        if (++esdi->head == drive->cfg_hpc) {
            esdi->head = 0;
            esdi->cylinder++;
            if (drive->current_cylinder < drive->real_tracks)
                drive->current_cylinder++;
        }
    }
}

static void
esdi_writew(uint16_t port, uint16_t val, void *priv)
{
    esdi_t *esdi = (esdi_t *) priv;
    off64_t addr;

    if (port > 0x01f0) {
        esdi_write(port, val & 0xff, priv);
        if (port != 0x01f7)
            esdi_write(port + 1, (val >> 8) & 0xff, priv);
    } else {
        esdi->buffer[esdi->pos >> 1] = val;
        esdi->pos += 2;

        if (esdi->pos >= 512) {
            esdi->pos    = 0;
            esdi->status = STAT_BUSY;
            get_sector(esdi, &addr);
            double seek_time = hdd_timing_write(&hdd[esdi->drives[esdi->drive_sel].hdd_num], addr, 1);
            double xfer_time = esdi_get_xfer_time(esdi, 1);
            esdi_set_callback(esdi, seek_time + xfer_time);
        }
    }
}

static void
esdi_write(uint16_t port, uint8_t val, void *priv)
{
    esdi_t *esdi = (esdi_t *) priv;
    double  seek_time;
    double  xfer_time;
    off64_t addr;
    uint8_t old;

    esdi_at_log("WD1007 write(%04x, %02x)\n", port, val);

    switch (port) {
        case 0x1f0: /* data */
            esdi_writew(port, val | (val << 8), priv);
            return;

        case 0x1f1: /* write precompensation */
            esdi->cylprecomp = val;
            return;

        case 0x1f2: /* sector count */
            esdi->secount = val;
            return;

        case 0x1f3: /* sector */
            esdi->sector = val;
            return;

        case 0x1f4: /* cylinder low */
            esdi->cylinder = (esdi->cylinder & 0xFF00) | val;
            return;

        case 0x1f5: /* cylinder high */
            esdi->cylinder = (esdi->cylinder & 0xFF) | (val << 8);
            return;

        case 0x1f6: /* drive/Head */
            esdi->head      = val & 0xF;
            esdi->drive_sel = (val & 0x10) ? 1 : 0;
            if (esdi->drives[esdi->drive_sel].present)
                esdi->status = STAT_READY | STAT_DSC;
            else
                esdi->status = 0;
            return;

        case 0x1f7: /* command register */
            irq_lower(esdi);
            esdi->command = val;
            esdi->error   = 0;

            esdi_at_log("WD1007: command %02x\n", val & 0xf0);

            switch (val & 0xf0) {
                case CMD_RESTORE:
                    esdi->command &= ~0x0f; /*mask off step rate*/
                    esdi->status = STAT_BUSY;
                    esdi_set_callback(esdi, 200 * HDC_TIME);
                    ui_sb_update_icon(SB_HDD | HDD_BUS_ESDI, 1);
                    break;

                case CMD_SEEK:
                    esdi->command &= ~0x0f; /*mask off step rate*/
                    esdi->status = STAT_BUSY;
                    get_sector(esdi, &addr);
                    seek_time = hdd_seek_get_time(&hdd[esdi->drives[esdi->drive_sel].hdd_num], addr, HDD_OP_SEEK, 0, 0.0);
                    esdi_set_callback(esdi, seek_time);
                    ui_sb_update_icon(SB_HDD | HDD_BUS_ESDI, 1);
                    break;

                default:
                    switch (val) {
                        case CMD_NOP:
                            esdi->status = STAT_BUSY;
                            esdi_set_callback(esdi, 200 * HDC_TIME);
                            break;

                        case CMD_READ:
                        case CMD_READ + 1:
                        case CMD_READ + 2:
                        case CMD_READ + 3:
                            esdi->command &= ~0x03;
                            if (val & 0x02)
                                fatal("Read with ECC\n");
                           fallthrough;

                        case 0xa0:
                            esdi->status = STAT_BUSY;
                            get_sector(esdi, &addr);
                            seek_time = hdd_timing_read(&hdd[esdi->drives[esdi->drive_sel].hdd_num], addr, 1);
                            xfer_time = esdi_get_xfer_time(esdi, 1);
                            esdi_set_callback(esdi, seek_time + xfer_time);
                            ui_sb_update_icon(SB_HDD | HDD_BUS_ESDI, 1);
                            break;

                        case CMD_WRITE:
                        case CMD_WRITE + 1:
                        case CMD_WRITE + 2:
                        case CMD_WRITE + 3:
                            esdi->command &= ~0x03;
                            if (val & 0x02)
                                fatal("Write with ECC\n");
                            esdi->status = STAT_READY | STAT_DRQ | STAT_DSC;
                            esdi->pos    = 0;
                            ui_sb_update_icon(SB_HDD | HDD_BUS_ESDI, 1);
                            break;

                        case CMD_VERIFY:
                        case CMD_VERIFY + 1:
                            esdi->command &= ~0x01;
                            esdi->status = STAT_BUSY;
                            get_sector(esdi, &addr);
                            seek_time = hdd_timing_read(&hdd[esdi->drives[esdi->drive_sel].hdd_num], addr, 1);
                            xfer_time = esdi_get_xfer_time(esdi, 1);
                            esdi_set_callback(esdi, seek_time + xfer_time);
                            ui_sb_update_icon(SB_HDD | HDD_BUS_ESDI, 1);
                            break;

                        case CMD_FORMAT:
                            esdi->status = STAT_DRQ;
                            esdi->pos    = 0;
                            ui_sb_update_icon(SB_HDD | HDD_BUS_ESDI, 1);
                            break;

                        case CMD_SET_PARAMETERS: /* Initialize Drive Parameters */
                            esdi->status = STAT_BUSY;
                            esdi_set_callback(esdi, 30 * HDC_TIME);
                            break;

                        case CMD_DIAGNOSE: /* Execute Drive Diagnostics */
                            esdi->status = STAT_BUSY;
                            esdi_set_callback(esdi, 200 * HDC_TIME);
                            ui_sb_update_icon(SB_HDD | HDD_BUS_ESDI, 1);
                            break;

                        case 0xe0: /*???*/
                        case CMD_READ_PARAMETERS:
                            esdi->status = STAT_BUSY;
                            esdi_set_callback(esdi, 200 * HDC_TIME);
                            break;

                        default:
                            esdi_at_log("WD1007: bad command %02X\n", val);
                            fallthrough;
                        case 0xe8: /*???*/
                            esdi->status = STAT_BUSY;
                            esdi_set_callback(esdi, 200 * HDC_TIME);
                            break;
                    }
            }
            break;

        case 0x3f6: /* Device control */
            if ((esdi->fdisk & 0x04) && !(val & 0x04)) {
                esdi_set_callback(esdi, 500 * HDC_TIME);
                esdi->reset  = 1;
                esdi->status = STAT_BUSY;
            } else if (!(esdi->fdisk & 0x04) && (val & 0x04)) {
                /* Drive held in reset. */
                esdi_set_callback(esdi, 0);
                esdi->status = STAT_BUSY;
            }
            old = esdi->fdisk;
            esdi->fdisk = val;
            if (!(val & 0x02) && (old & 0x02))
                irq_update(esdi);
            break;

        default:
            break;
    }
}

static uint16_t
esdi_readw(uint16_t port, void *priv)
{
    esdi_t  *esdi = (esdi_t *) priv;
    uint16_t temp;
    off64_t  addr;

    if (port > 0x01f0) {
        temp = esdi_read(port, priv);
        if (port == 0x01f7)
            temp |= 0xff00;
        else
            temp |= (esdi_read(port + 1, priv) << 8);
    } else {
        temp = esdi->buffer[esdi->pos >> 1];
        esdi->pos += 2;

        if (esdi->pos >= 512) {
            esdi->pos    = 0;
            esdi->status = STAT_READY | STAT_DSC;
            if (esdi->command == CMD_READ || esdi->command == 0xa0) {
                esdi->secount = (esdi->secount - 1) & 0xff;
                if (esdi->secount) {
                    next_sector(esdi);
                    esdi->status = STAT_BUSY;
                    get_sector(esdi, &addr);
                    double seek_time = hdd_timing_read(&hdd[esdi->drives[esdi->drive_sel].hdd_num], addr, 1);
                    double xfer_time = esdi_get_xfer_time(esdi, 1);
                    /* 390.625 us per sector at 10 Mbit/s = 1280 kB/s. */
                    esdi_set_callback(esdi, seek_time + xfer_time);
                } else
                    ui_sb_update_icon(SB_HDD | HDD_BUS_ESDI, 0);
            }
        }
    }

    return temp;
}

static uint8_t
esdi_read(uint16_t port, void *priv)
{
    esdi_t *esdi = (esdi_t *) priv;
    uint8_t temp = 0xff;

    switch (port) {
        case 0x1f0: /* data */
            temp = esdi_readw(port, esdi) & 0xff;
            break;

        case 0x1f1: /* error */
            temp = esdi->error;
            break;

        case 0x1f2: /* sector count */
            temp = esdi->secount;
            break;

        case 0x1f3: /* sector */
            temp = esdi->sector;
            break;

        case 0x1f4: /* cylinder low */
            temp = (uint8_t) (esdi->cylinder & 0xff);
            break;

        case 0x1f5: /* cylinder high */
            temp = (uint8_t) (esdi->cylinder >> 8);
            break;

        case 0x1f6: /* drive/Head */
            temp = (uint8_t) (esdi->head | (esdi->drive_sel ? 0x10 : 0) | 0xa0);
            break;

        case 0x1f7: /* status */
            irq_lower(esdi);
            temp = esdi->status;
            break;

        default:
            break;
    }

    esdi_at_log("WD1007 read(%04x) = %02x\n", port, temp);

    return temp;
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
static void
esdi_padstr(char *str, const char *src, const int len)
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

static void
esdi_callback(void *priv)
{
    esdi_t  *esdi  = (esdi_t *) priv;
    drive_t *drive = &esdi->drives[esdi->drive_sel];
    off64_t  addr;
    double   seek_time;

    if (esdi->reset) {
        esdi->status   = STAT_READY | STAT_DSC;
        esdi->error    = 1;
        esdi->secount  = 1;
        esdi->sector   = 1;
        esdi->head     = 0;
        esdi->cylinder = 0;
        esdi->reset    = 0;

        ui_sb_update_icon(SB_HDD | HDD_BUS_ESDI, 0);
        return;
    }

    esdi_at_log("WD1007: command %02x on drive %i\n", esdi->command, esdi->drive_sel);

    switch (esdi->command) {
        case CMD_RESTORE:
            if (!drive->present) {
                esdi->status = STAT_READY | STAT_ERR | STAT_DSC;
                esdi->error  = ERR_ABRT;
            } else {
                drive->current_cylinder = 0;
                esdi->status            = STAT_READY | STAT_DSC;
            }
            irq_raise(esdi);
            ui_sb_update_icon(SB_HDD | HDD_BUS_ESDI, 0);
            break;

        case CMD_SEEK:
            if (!drive->present) {
                esdi->status = STAT_READY | STAT_ERR | STAT_DSC;
                esdi->error  = ERR_ABRT;
            } else
                esdi->status = STAT_READY | STAT_DSC;
            irq_raise(esdi);
            ui_sb_update_icon(SB_HDD | HDD_BUS_ESDI, 0);
            break;

        case CMD_READ:
            if (!drive->present) {
                esdi->status = STAT_READY | STAT_ERR | STAT_DSC;
                esdi->error  = ERR_ABRT;
                irq_raise(esdi);
            } else {
                if (get_sector(esdi, &addr)) {
                    esdi->error  = ERR_ID_NOT_FOUND;
read_error:
                    esdi->status = STAT_READY | STAT_DSC | STAT_ERR;
                    irq_raise(esdi);
                    break;
                }

                if (hdd_image_read(drive->hdd_num, addr, 1, (uint8_t *) esdi->buffer) < 0) {
                    esdi->error = ERR_BAD_BLOCK;
                    goto read_error;
                }
                esdi->pos    = 0;
                esdi->status = STAT_DRQ | STAT_READY | STAT_DSC;
                irq_raise(esdi);
            }
            break;

        case CMD_WRITE:
            if (!drive->present) {
                esdi->status = STAT_READY | STAT_ERR | STAT_DSC;
                esdi->error  = ERR_ABRT;
                irq_raise(esdi);
                ui_sb_update_icon(SB_HDD | HDD_BUS_ESDI, 0);
                break;
            } else {
                if (get_sector(esdi, &addr)) {
                    esdi->error  = ERR_ID_NOT_FOUND;
write_error:
                    esdi->status = STAT_READY | STAT_DSC | STAT_ERR;
                    irq_raise(esdi);
                    ui_sb_update_icon(SB_HDD | HDD_BUS_ESDI, 0);
                    break;
                }

                if (hdd_image_write(drive->hdd_num, addr, 1, (uint8_t *) esdi->buffer) < 0) {
                    esdi->error = ERR_BAD_BLOCK;
                    goto write_error;
                }
                irq_raise(esdi);
                esdi->secount = (esdi->secount - 1) & 0xff;
                if (esdi->secount) {
                    esdi->status = STAT_DRQ | STAT_READY | STAT_DSC;
                    esdi->pos    = 0;
                    next_sector(esdi);
                    ui_sb_update_icon(SB_HDD | HDD_BUS_ESDI, 1);
                } else {
                    esdi->status = STAT_READY | STAT_DSC;
                    ui_sb_update_icon(SB_HDD | HDD_BUS_ESDI, 0);
                }
            }
            break;

        case CMD_VERIFY:
            if (!drive->present) {
                esdi->status = STAT_READY | STAT_ERR | STAT_DSC;
                esdi->error  = ERR_ABRT;
                irq_raise(esdi);
                ui_sb_update_icon(SB_HDD | HDD_BUS_ESDI, 0);
                break;
            } else {
                if (get_sector(esdi, &addr)) {
                    esdi->error  = ERR_ID_NOT_FOUND;
verify_error:
                    esdi->status = STAT_READY | STAT_DSC | STAT_ERR;
                    irq_raise(esdi);
                    ui_sb_update_icon(SB_HDD | HDD_BUS_ESDI, 0);
                    break;
                }

                if (hdd_image_read(drive->hdd_num, addr, 1, (uint8_t *) esdi->buffer) < 0) {
                    esdi->error = ERR_BAD_BLOCK;
                    goto verify_error;
                }
                ui_sb_update_icon(SB_HDD | HDD_BUS_ESDI, 1);
                next_sector(esdi);
                esdi->secount = (esdi->secount - 1) & 0xff;
                if (esdi->secount) {
                    get_sector(esdi, &addr);
                    seek_time = hdd_timing_read(&hdd[esdi->drives[esdi->drive_sel].hdd_num], addr, 1);
                    esdi_set_callback(esdi, seek_time + HDC_TIME);
                } else {
                    esdi->pos    = 0;
                    esdi->status = STAT_READY | STAT_DSC;
                    irq_raise(esdi);
                    ui_sb_update_icon(SB_HDD | HDD_BUS_ESDI, 0);
                }
            }
            break;

        case CMD_FORMAT:
            ui_sb_update_icon(SB_HDD | HDD_BUS_ESDI, 0);
            if (!drive->present) {
                esdi->status = STAT_READY | STAT_ERR | STAT_DSC;
                esdi->error  = ERR_ABRT;
                irq_raise(esdi);
                break;
            } else {
                if (get_sector_format(esdi, &addr)) {
                    esdi->error  = ERR_ID_NOT_FOUND;
format_error:
                    esdi->status = STAT_READY | STAT_DSC | STAT_ERR;
                    irq_raise(esdi);
                    break;
                }

                if (hdd_image_zero(drive->hdd_num, addr, esdi->secount) < 0) {
                    esdi->error = ERR_BAD_BLOCK;
                    goto format_error;
                }
                esdi->status = STAT_READY | STAT_DSC;
                irq_raise(esdi);
            }
            break;

        case CMD_DIAGNOSE:
            /* This is basically controller diagnostics - it resets drive select to 0,
               and resets error and status to ready, DSC, and no error detected. */
            esdi->drive_sel = 0;
            drive           = &esdi->drives[esdi->drive_sel];

            esdi->error  = 1; /*no error detected*/
            esdi->status = STAT_READY | STAT_DSC;
            irq_raise(esdi);
            ui_sb_update_icon(SB_HDD | HDD_BUS_ESDI, 0);
            break;

        case CMD_SET_PARAMETERS: /* Initialize Drive Parameters */
            ui_sb_update_icon(SB_HDD | HDD_BUS_ESDI, 0);
            if (!drive->present) {
                esdi->status = STAT_READY | STAT_ERR | STAT_DSC;
                esdi->error  = ERR_ABRT;
                irq_raise(esdi);
            } else {
                drive->cfg_spt = esdi->secount;
                drive->cfg_hpc = esdi->head + 1;

                esdi_at_log("WD1007: parameters: spt=%i hpc=%i\n", drive->cfg_spt, drive->cfg_hpc);

                if (!esdi->secount)
                    fatal("WD1007: secount=0\n");
                esdi->status = STAT_READY | STAT_DSC;
                irq_raise(esdi);
            }
            break;

        case CMD_NOP:
            esdi->status = STAT_READY | STAT_ERR | STAT_DSC;
            esdi->error  = ERR_ABRT;
            irq_raise(esdi);
            ui_sb_update_icon(SB_HDD | HDD_BUS_ESDI, 0);
            break;

        case 0xe0:
            ui_sb_update_icon(SB_HDD | HDD_BUS_ESDI, 0);
            if (!drive->present) {
                esdi->status = STAT_READY | STAT_ERR | STAT_DSC;
                esdi->error  = ERR_ABRT;
                irq_raise(esdi);
                break;
            } else {
                switch (esdi->cylinder >> 8) {
                    case 0x31:
                        esdi->cylinder = drive->real_tracks;
                        break;

                    case 0x33:
                        esdi->cylinder = drive->real_hpc;
                        break;

                    case 0x35:
                        esdi->cylinder = 0x200;
                        break;

                    case 0x36:
                        esdi->cylinder = drive->real_spt;
                        break;

                    default:
                        esdi_at_log("WD1007: bad read config %02x\n", esdi->cylinder >> 8);
                }
                esdi->status = STAT_READY | STAT_DSC;
                irq_raise(esdi);
            }
            break;

        case 0xa0:
            if (!drive->present) {
                esdi->status = STAT_READY | STAT_ERR | STAT_DSC;
                esdi->error  = ERR_ABRT;
            } else {
                memset(esdi->buffer, 0x00, 512);
                memset(&esdi->buffer[3], 0xff, 512 - 6);
                esdi->pos    = 0;
                esdi->status = STAT_DRQ | STAT_READY | STAT_DSC;
            }
            irq_raise(esdi);
            ui_sb_update_icon(SB_HDD | HDD_BUS_ESDI, 0);
            break;

        case CMD_READ_PARAMETERS:
            if (!drive->present) {
                esdi->status = STAT_READY | STAT_ERR | STAT_DSC;
                esdi->error  = ERR_ABRT;
                irq_raise(esdi);
            } else {
                memset(esdi->buffer, 0x00, 512);
                esdi->buffer[0] = 0x3244;                            /*
                                                                        Soft sectored (0x0004),
                                                                        Fixed drive (0x0040),
                                                                        Transfer rate > 5 Mbps but <= 10 Mbps (0x0200),
                                                                        Data strobe offset option (0x1000),
                                                                        Track offset option (0x2000).
                                                                      */
                if (drive->real_spt >= 26)
                    esdi->buffer[0] |= 0x0008;                       /* Not MFM encoded. */
                esdi->buffer[1] = drive->real_tracks;                /* Fixed cylinders - the BIOS lists 2 less. */
                esdi->buffer[2] = 0;                                 /* Removable cylinders. */
                esdi->buffer[3] = drive->real_hpc;                   /* Heads. */
                esdi->buffer[5] = 600;                               /* Unformatted bytes per sector. */
                esdi->buffer[4] = esdi->buffer[5] * drive->real_spt; /* Unformatted bytes per track. */
                esdi->buffer[6] = drive->real_spt;                   /* Sectors per track - the BIOS lists 1 less. */
                esdi->buffer[7] = 3088;                              /* Bytes in inter-sector gap. */
                esdi->buffer[8] = 11;                                /* Byce in sync fileds. */
                esdi->buffer[9] = 0xf;                               /* Number of vendor unique words. */
                /* Serial Number */
                esdi_padstr((char *) (esdi->buffer + 10), "00000000000000000000", 20);
                /* Controller information. */
                esdi->buffer[20] = 3;                                /* Buffer type. */
                esdi->buffer[21] = 64;                               /* Buffer size in 512-byte increments. */
                esdi->buffer[22] = 4;                                /* Bytes of ECC. */
                /* Firmware */
                esdi_padstr((char *) (esdi->buffer + 23), "REV. A5", 8);
                /* Model */
                esdi_padstr((char *) (esdi->buffer + 27), "WD1007V", 40);
                esdi->buffer[47] = 1;                                /* Sectors per interrupt. */
                esdi->buffer[48] = 0;                                /* Can use DWord read/write? */
                esdi->pos        = 0;
                esdi->status     = STAT_DRQ | STAT_READY | STAT_DSC;
                irq_raise(esdi);
            }
            ui_sb_update_icon(SB_HDD | HDD_BUS_ESDI, 0);
            break;

        default:
            esdi_at_log("WD1007: callback on unknown command %02x\n", esdi->command);
            fallthrough;

        case 0xe8:
            esdi->status = STAT_READY | STAT_ERR | STAT_DSC;
            esdi->error  = ERR_ABRT;
            irq_raise(esdi);
            ui_sb_update_icon(SB_HDD | HDD_BUS_ESDI, 0);
            break;
    }
}

static void
loadhd(esdi_t *esdi, int hdd_num, int d, UNUSED(const char *fn))
{
    drive_t *drive = &esdi->drives[hdd_num];

    if (!hdd_image_load(d)) {
        esdi_at_log("WD1007: drive %d not present!\n", d);
        drive->present = 0;
        return;
    }

    hdd_preset_apply(d);

    drive->cfg_spt = drive->real_spt = hdd[d].spt;
    drive->cfg_hpc = drive->real_hpc = hdd[d].hpc;
    drive->real_tracks               = hdd[d].tracks;
    drive->hdd_num                   = d;
    drive->present                   = 1;
}

static void
esdi_rom_write(uint32_t addr, uint8_t val, void *priv)
{
    rom_t *rom = (rom_t *) priv;

    addr &= rom->mask;

    if (addr >= 0x1f00 && addr < 0x2000)
        rom->rom[addr] = val;
}

static void *
wd1007vse1_init(UNUSED(const device_t *info))
{
    int c;

    esdi_t *esdi = calloc(1, sizeof(esdi_t));

    c = 0;
    for (uint8_t d = 0; d < HDD_NUM; d++) {
        if ((hdd[d].bus_type == HDD_BUS_ESDI) && (hdd[d].esdi_channel < ESDI_NUM)) {
            loadhd(esdi, hdd[d].esdi_channel, d, hdd[d].fn);

            if (++c >= ESDI_NUM)
                break;
        }
    }

    esdi->status = STAT_READY | STAT_DSC;
    esdi->error  = 1;

    rom_init(&esdi->bios_rom,
             BIOS_FILE, 0xc8000, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);

    mem_mapping_set_handler(&esdi->bios_rom.mapping,
                            rom_read, rom_readw, rom_readl,
                            esdi_rom_write, NULL, NULL);

    io_sethandler(0x01f0, 1,
                  esdi_read, esdi_readw, NULL,
                  esdi_write, esdi_writew, NULL, esdi);
    io_sethandler(0x01f1, 7,
                  esdi_read, esdi_readw, NULL,
                  esdi_write, esdi_writew, NULL, esdi);
    io_sethandler(0x03f6, 1, NULL, NULL, NULL,
                  esdi_write, NULL, NULL, esdi);

    timer_add(&esdi->callback_timer, esdi_callback, esdi, 0);

    ui_sb_update_icon(SB_HDD | HDD_BUS_ESDI, 0);

    return esdi;
}

static void
wd1007vse1_close(void *priv)
{
    esdi_t        *esdi = (esdi_t *) priv;
    const drive_t *drive;

    for (uint8_t d = 0; d < 2; d++) {
        drive = &esdi->drives[d];

        hdd_image_close(drive->hdd_num);
    }

    free(esdi);

    ui_sb_update_icon(SB_HDD | HDD_BUS_ESDI, 0);
}

static int
wd1007vse1_available(void)
{
    return (rom_present(BIOS_FILE));
}

const device_t esdi_at_wd1007vse1_device = {
    .name          = "Western Digital WD1007V-SE1 (ESDI)",
    .internal_name = "esdi_at",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = 0,
    .init          = wd1007vse1_init,
    .close         = wd1007vse1_close,
    .reset         = NULL,
    .available     = wd1007vse1_available,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
