/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Tandy 2000 hard disk controller emulation.
 *
 * Authors: Curt Coder
 *          WNT50
 *
 *          Copyright 2010-2026 Curt Coder.
 *          Copyright 2026 WNT50.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/hdc.h>
#include <86box/hdd.h>
#include <86box/io.h>
#include <86box/ui.h>
#include <86box/timer.h>
#include <86box/plat_unused.h>

/* The 2000HD option is a WD1010 with a one-sector buffer in front of it. The
   host moves a sector through the buffer at register 0 and drives the WD1010
   task file at registers 1-7; the board has no DOR and nothing raises INTRQ,
   so the host polls the status register. */
#define REG_DATA         0 /* 270h */
#define REG_ERROR        1 /* 272h, write = precompensation */
#define REG_COUNT        2 /* 274h */
#define REG_SECTOR       3 /* 276h */
#define REG_CYL_LO       4 /* 278h */
#define REG_CYL_HI       5 /* 27ah */
#define REG_SDH          6 /* 27ch */
#define REG_STATUS       7 /* 27eh, write = command */

#define ST_ERR           0x01
#define ST_DRQ           0x08
#define ST_SC            0x10
#define ST_RDY           0x40
#define ST_BUSY          0x80

#define ERR_AC           0x04
#define ERR_ID           0x10
#define ERR_BB           0x80

#define CMD_RESTORE      0x10
#define CMD_READ_SECTOR  0x20
#define CMD_WRITE_SECTOR 0x30
#define CMD_SCAN_ID      0x40
#define CMD_WRITE_FORMAT 0x50
#define CMD_SEEK         0x70

#define SECTOR_SIZE      512

/* The drive has to seek and then wait for the sector to come under the head
   before it can move data, so the board stays busy - and its activity light
   stays on - for as long as the real mechanism would take. */
#define ACCESS_TIME      (20 * TIMER_USEC * 1000)
#define FORMAT_SECTOR    (1000 * TIMER_USEC)

#define STATE_IDLE       0
#define STATE_READ_WAIT  1
#define STATE_WRITE_WAIT 2
#define STATE_FORMAT     3

#ifdef ENABLE_TANDY2K_HDC_LOG
int tandy2k_hdc_do_log = ENABLE_TANDY2K_HDC_LOG;

static void
tandy2k_hdc_log(const char *fmt, ...)
{
    va_list ap;

    if (tandy2k_hdc_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define tandy2k_hdc_log(fmt, ...)
#endif

typedef struct tandy2k_hdc_t {
    uint8_t  buf[SECTOR_SIZE];
    uint16_t ptr;

    uint8_t  error;
    uint8_t  count;
    uint8_t  sector;
    uint8_t  cyl_lo;
    uint8_t  cyl_hi;
    uint8_t  sdh;
    uint8_t  status;

    uint8_t  head;
    uint8_t  unit;

    uint8_t  present[MFM_NUM];
    uint8_t  hdd_num[MFM_NUM];
    uint32_t tracks[MFM_NUM];
    uint32_t spt[MFM_NUM];
    uint32_t hpc[MFM_NUM];

    uint32_t   lba_prev;
    uint32_t   sectors;
    int        state;
    pc_timer_t timer;
} tandy2k_hdc_t;

static void
tandy2k_hdc_led_off(void)
{
    ui_sb_update_icon(SB_HDD | HDD_BUS_MFM, 0);
    ui_sb_update_icon_write(SB_HDD | HDD_BUS_MFM, 0);
}

static void
tandy2k_hdc_led(tandy2k_hdc_t *dev, int write)
{
    ui_sb_update_icon(SB_HDD | HDD_BUS_MFM, !write);
    ui_sb_update_icon_write(SB_HDD | HDD_BUS_MFM, write);
}

static void
tandy2k_hdc_reset_state(tandy2k_hdc_t *dev)
{
    timer_disable(&dev->timer);

    dev->ptr      = 0;
    dev->error    = 0x00;
    dev->count    = 0x00;
    dev->sector   = 0x00;
    dev->cyl_lo   = 0x00;
    dev->cyl_hi   = 0x00;
    dev->sdh      = 0x00;
    dev->head     = 0x00;
    dev->unit     = 0x00;
    dev->lba_prev = 0;
    dev->sectors  = 0;
    dev->state    = STATE_IDLE;

    dev->status = dev->present[0] ? (ST_RDY | ST_SC) : 0x00;
}

/* Turn the task file's CHS into a linear sector number, checking it
   against the geometry of the drive the SDH register selected. */
static int
tandy2k_hdc_lba(tandy2k_hdc_t *dev, uint32_t *lba)
{
    uint32_t cyl;
    uint32_t head;
    uint32_t sector;
    uint8_t  unit = dev->unit;

    if (!dev->present[unit])
        return 0;

    cyl    = ((uint32_t) dev->cyl_hi << 8) | dev->cyl_lo;
    head   = dev->head;
    sector = dev->sector;

    if ((sector < 1) || (sector > dev->spt[unit]) || (head >= dev->hpc[unit]) ||
        (cyl >= dev->tracks[unit]))
        return 0;

    *lba = ((cyl * dev->hpc[unit]) + head) * dev->spt[unit] + (sector - 1);

    return 1;
}

static void
tandy2k_hdc_command(tandy2k_hdc_t *dev, uint8_t command)
{
    uint32_t lba;

    dev->ptr    = 0;
    dev->error  = 0x00;
    dev->status = ST_RDY | ST_SC;

    tandy2k_hdc_log("TANDY2K HDC: command %02X, unit %i, %i/%i/%i, count %i\n",
                    command, dev->unit, ((uint32_t) dev->cyl_hi << 8) | dev->cyl_lo,
                    dev->head, dev->sector, dev->count);

    if (!dev->present[dev->unit]) {
        /* Nothing on that unit: stay not ready so the host gives up instead of
           waiting for a transfer that will never happen. */
        dev->error  = ERR_AC;
        dev->status = 0x00;
        return;
    }

    switch (command & 0xf0) {
        case CMD_RESTORE:
            dev->cyl_lo = 0x00;
            dev->cyl_hi = 0x00;
            break;

        case CMD_SEEK:
            /* The cylinder registers already hold the target. */
            break;

        case CMD_READ_SECTOR:
        case CMD_WRITE_SECTOR:
            if (!tandy2k_hdc_lba(dev, &lba)) {
                dev->error   = ERR_ID;
                dev->status |= ST_ERR;
                break;
            }

            dev->lba_prev = lba;
            dev->status |= ST_BUSY;
            dev->state   = ((command & 0xf0) == CMD_READ_SECTOR) ? STATE_READ_WAIT : STATE_WRITE_WAIT;

            tandy2k_hdc_led(dev, dev->state == STATE_WRITE_WAIT);

            timer_set_delay_u64(&dev->timer, ACCESS_TIME);
            break;

        case CMD_WRITE_FORMAT: {
            /* The Tandy format utility sends one of these per track, with the
               sector count set to the sectors per track and the sector number
               register left at an unused value, so the track is numbered from
               sector one here. */
            uint32_t cyl     = ((uint32_t) dev->cyl_hi << 8) | dev->cyl_lo;
            uint32_t sectors = dev->count ? dev->count : 256;

            if ((dev->head >= dev->hpc[dev->unit]) || (cyl >= dev->tracks[dev->unit])) {
                dev->error   = ERR_ID;
                dev->status |= ST_ERR;
                break;
            }

            if (sectors > dev->spt[dev->unit])
                sectors = dev->spt[dev->unit];

            dev->lba_prev = ((cyl * dev->hpc[dev->unit]) + dev->head) * dev->spt[dev->unit];
            dev->sectors  = sectors;
            dev->status  |= ST_BUSY;
            dev->state    = STATE_FORMAT;

            tandy2k_hdc_led(dev, 1);

            /* A seek plus one pass of the head over the track. */
            timer_set_delay_u64(&dev->timer, ACCESS_TIME + (sectors * FORMAT_SECTOR));
            break;
        }

        case CMD_SCAN_ID:
        default:
            dev->error   = ERR_AC;
            dev->status |= ST_ERR;
            break;
    }
}

/* Runs when the mechanism has finished moving the data; only then does the
   board release the host with DRQ (or with the error the drive reported). */
static void
tandy2k_hdc_callback(void *priv)
{
    tandy2k_hdc_t *dev = (tandy2k_hdc_t *) priv;

    switch (dev->state) {
        case STATE_READ_WAIT:
            dev->state   = STATE_IDLE;
            dev->status &= ~ST_BUSY;

            if (hdd_image_read(dev->hdd_num[dev->unit], dev->lba_prev, 1, dev->buf) < 0) {
                dev->error   = ERR_BB;
                dev->status |= ST_ERR;
                tandy2k_hdc_led_off();
                break;
            }

            dev->status |= ST_DRQ;
            break;

        case STATE_WRITE_WAIT:
            dev->state   = STATE_IDLE;
            dev->status &= ~ST_BUSY;
            dev->status |= ST_DRQ;
            break;

        case STATE_FORMAT:
            dev->state   = STATE_IDLE;
            dev->status &= ~ST_BUSY;

            memset(dev->buf, 0xe5, SECTOR_SIZE);

            for (uint32_t s = 0; s < dev->sectors; s++) {
                if (hdd_image_write(dev->hdd_num[dev->unit], dev->lba_prev + s, 1, dev->buf) < 0) {
                    dev->error   = ERR_BB;
                    dev->status |= ST_ERR;
                    break;
                }
            }

            tandy2k_hdc_led_off();
            break;

        default:
            break;
    }

    tandy2k_hdc_log("TANDY2K HDC: done, status %02X, error %02X\n", dev->status, dev->error);
}

static uint8_t
tandy2k_hdc_read_data(tandy2k_hdc_t *dev)
{
    uint8_t ret = dev->buf[dev->ptr];

    dev->ptr++;
    if (dev->ptr >= SECTOR_SIZE) {
        dev->ptr = 0;
        dev->status &= ~ST_DRQ;
        tandy2k_hdc_led_off();
    }

    return ret;
}

static void
tandy2k_hdc_write_data(tandy2k_hdc_t *dev, uint8_t val)
{
    if (!(dev->status & ST_DRQ))
        return;

    dev->buf[dev->ptr] = val;

    dev->ptr++;
    if (dev->ptr >= SECTOR_SIZE) {
        dev->ptr = 0;
        dev->status &= ~ST_DRQ;
        tandy2k_hdc_led_off();

        if (hdd_image_write(dev->hdd_num[dev->unit], dev->lba_prev, 1, dev->buf) < 0) {
            dev->error   = ERR_BB;
            dev->status |= ST_ERR;
        }
    }
}

static uint8_t
tandy2k_hdc_read(UNUSED(uint16_t addr), void *priv)
{
    tandy2k_hdc_t *dev = (tandy2k_hdc_t *) priv;

    switch ((addr & 0x000f) >> 1) {
        case REG_DATA:
            return tandy2k_hdc_read_data(dev);
        case REG_ERROR:
            return dev->error;
        case REG_COUNT:
            return dev->count;
        case REG_SECTOR:
            return dev->sector;
        case REG_CYL_LO:
            return dev->cyl_lo;
        case REG_CYL_HI:
            return dev->cyl_hi;
        case REG_SDH:
            return dev->sdh;
        case REG_STATUS:
            return dev->status;
        default:
            break;
    }

    return 0xff;
}

static void
tandy2k_hdc_write(UNUSED(uint16_t addr), uint8_t val, void *priv)
{
    tandy2k_hdc_t *dev = (tandy2k_hdc_t *) priv;

    switch ((addr & 0x000f) >> 1) {
        case REG_DATA:
            tandy2k_hdc_write_data(dev, val);
            break;
        case REG_ERROR:
            /* Write only: write precompensation, not modelled. */
            break;
        case REG_COUNT:
            dev->count = val;
            break;
        case REG_SECTOR:
            dev->sector = val;
            break;
        case REG_CYL_LO:
            dev->cyl_lo = val;
            break;
        case REG_CYL_HI:
            dev->cyl_hi = val;
            break;
        case REG_SDH:
            dev->sdh  = val;
            dev->head = val & 0x07;
            dev->unit = (val >> 3) & 0x03;

            /* Selecting a drive clears a transfer that was left pending. */
            if (dev->status & ST_DRQ)
                tandy2k_hdc_led_off();

            dev->status &= ~ST_DRQ;
            break;
        case REG_STATUS:
            /* The chip ignores a new command while it is still busy with the
               previous one. */
            if (!(dev->status & ST_BUSY))
                tandy2k_hdc_command(dev, val);
            break;
        default:
            break;
    }
}

/* The board's reset port answers reads and takes writes; either one resets the
   controller, which is how the BIOS probes for the card. */
static uint8_t
tandy2k_hdc_reset_r(UNUSED(uint16_t addr), void *priv)
{
    tandy2k_hdc_reset_state((tandy2k_hdc_t *) priv);

    return 0xff;
}

static void
tandy2k_hdc_reset_w(UNUSED(uint16_t addr), UNUSED(uint8_t val), void *priv)
{
    tandy2k_hdc_reset_state((tandy2k_hdc_t *) priv);
}

static void
tandy2k_hdc_reset(void *priv)
{
    tandy2k_hdc_t *dev = (tandy2k_hdc_t *) priv;

    tandy2k_hdc_led_off();

    tandy2k_hdc_reset_state(dev);
}

static void *
tandy2k_hdc_init(UNUSED(const device_t *info))
{
    tandy2k_hdc_t *dev = (tandy2k_hdc_t *) calloc(1, sizeof(tandy2k_hdc_t));

    for (uint8_t i = 0; i < HDD_NUM; i++) {
        if ((hdd[i].bus_type != HDD_BUS_MFM) || (hdd[i].mfm_channel >= MFM_NUM))
            continue;

        if (!hdd_image_load(i))
            continue;

        dev->present[hdd[i].mfm_channel] = 1;
        dev->hdd_num[hdd[i].mfm_channel] = i;
        dev->spt[hdd[i].mfm_channel]     = hdd[i].spt;
        dev->hpc[hdd[i].mfm_channel]     = hdd[i].hpc;
        dev->tracks[hdd[i].mfm_channel]  = hdd[i].tracks;
    }

    io_sethandler(0x026c, 0x0002, tandy2k_hdc_reset_r, NULL, NULL,
                  tandy2k_hdc_reset_w, NULL, NULL, dev);
    io_sethandler(0x0270, 0x0010, tandy2k_hdc_read, NULL, NULL,
                  tandy2k_hdc_write, NULL, NULL, dev);

    timer_add(&dev->timer, tandy2k_hdc_callback, dev, 0);

    tandy2k_hdc_reset_state(dev);

    return dev;
}

static void
tandy2k_hdc_close(void *priv)
{
    tandy2k_hdc_t *dev = (tandy2k_hdc_t *) priv;

    for (uint8_t i = 0; i < MFM_NUM; i++) {
        if (dev->present[i])
            hdd_image_close(dev->hdd_num[i]);
    }

    free(dev);
}

const device_t st506_tandy2k_wd1010_device = {
    .name          = "Tandy 2000 Hard Disk Controller",
    .internal_name = "st506_tandy2k_wd1010",
    .flags         = 0,
    .local         = 0,
    .init          = tandy2k_hdc_init,
    .close         = tandy2k_hdc_close,
    .reset         = tandy2k_hdc_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
