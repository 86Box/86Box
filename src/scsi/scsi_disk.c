/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          Emulation of SCSI fixed disks.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2017-2018 Miran Grca.
 */
#include <stdarg.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/machine.h>
#include <86box/nvr.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/sound.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/hdd.h>
#include <86box/scsi_disk.h>
#include <86box/version.h>

#define IDE_ATAPI_IS_EARLY             id->sc->pad0

#define scsi_disk_sense_error dev->sense[0]
#define scsi_disk_sense_key   dev->sense[2]
#define scsi_disk_asc         dev->sense[12]
#define scsi_disk_ascq        dev->sense[13]

/* Table of all SCSI commands and their flags, needed for the new disc change / not ready handler. */
const uint8_t scsi_disk_command_flags[0x100] = {
    IMPLEMENTED | CHECK_READY | NONDATA,          /* 0x00 */
    IMPLEMENTED | ALLOW_UA | NONDATA | SCSI_ONLY, /* 0x01 */
    0,
    IMPLEMENTED | ALLOW_UA,                                     /* 0x03 */
    IMPLEMENTED | CHECK_READY | ALLOW_UA | NONDATA | SCSI_ONLY, /* 0x04 */
    0, 0, 0,
    IMPLEMENTED | CHECK_READY, /* 0x08 */
    0,
    IMPLEMENTED | CHECK_READY,           /* 0x0A */
    IMPLEMENTED | CHECK_READY | NONDATA, /* 0x0B */
    0, 0, 0, 0, 0, 0,
    IMPLEMENTED | ALLOW_UA,                          /* 0x12 */
    IMPLEMENTED | CHECK_READY | NONDATA | SCSI_ONLY, /* 0x13 */
    0,
    IMPLEMENTED,             /* 0x15 */
    IMPLEMENTED | SCSI_ONLY, /* 0x16 */
    IMPLEMENTED | SCSI_ONLY, /* 0x17 */
    0, 0,
    IMPLEMENTED, /* 0x1A */
    0, 0,
    IMPLEMENTED,               /* 0x1D */
    IMPLEMENTED | CHECK_READY, /* 0x1E */
    0, 0, 0, 0, 0, 0,
    IMPLEMENTED | CHECK_READY, /* 0x25 */
    0, 0,
    IMPLEMENTED | CHECK_READY, /* 0x28 */
    0,
    IMPLEMENTED | CHECK_READY,           /* 0x2A */
    IMPLEMENTED | CHECK_READY | NONDATA, /* 0x2B */
    0, 0,
    IMPLEMENTED | CHECK_READY,                       /* 0x2E */
    IMPLEMENTED | CHECK_READY | NONDATA | SCSI_ONLY, /* 0x2F */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,
    IMPLEMENTED | CHECK_READY, /* 0x41 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    IMPLEMENTED, /* 0x55 */
    0, 0, 0, 0,
    IMPLEMENTED, /* 0x5A */
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    IMPLEMENTED | CHECK_READY, /* 0xA8 */
    0,
    IMPLEMENTED | CHECK_READY, /* 0xAA */
    0, 0, 0,
    IMPLEMENTED | CHECK_READY,                       /* 0xAE */
    IMPLEMENTED | CHECK_READY | NONDATA | SCSI_ONLY, /* 0xAF */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    IMPLEMENTED, /* 0xBD */
    0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

uint64_t scsi_disk_mode_sense_page_flags = (GPMODEP_FORMAT_DEVICE_PAGE | GPMODEP_RIGID_DISK_PAGE | GPMODEP_UNK_VENDOR_PAGE | GPMODEP_ALL_PAGES);

/* This should be done in a better way but for time being, it's been done this way so it's not as huge and more readable. */
static const mode_sense_pages_t scsi_disk_mode_sense_pages_default = {
    {[GPMODE_FORMAT_DEVICE_PAGE] = { GPMODE_FORMAT_DEVICE_PAGE, 0x16, 0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
     [GPMODE_RIGID_DISK_PAGE]    = { GPMODE_RIGID_DISK_PAGE, 0x16, 0, 0x10, 0, 64, 0, 0, 0, 0, 0, 0, 0, 200, 0xff, 0xff, 0xff, 0, 0, 0, 0x15, 0x18, 0, 0 },
     [GPMODE_UNK_VENDOR_PAGE]    = { 0xB0, 0x16, '8', '6', 'B', 'o', 'x', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' }}
};

static const mode_sense_pages_t scsi_disk_mode_sense_pages_changeable = {
    {[GPMODE_FORMAT_DEVICE_PAGE] = { GPMODE_FORMAT_DEVICE_PAGE, 0x16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
     [GPMODE_RIGID_DISK_PAGE]    = { GPMODE_RIGID_DISK_PAGE, 0x16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
     [GPMODE_UNK_VENDOR_PAGE]    = { 0xB0, 0x16, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }}
};

static void scsi_disk_command_complete(scsi_disk_t *dev);

static void scsi_disk_mode_sense_load(scsi_disk_t *dev);

static void scsi_disk_init(scsi_disk_t *dev);

#ifdef ENABLE_SCSI_DISK_LOG
int scsi_disk_do_log = ENABLE_SCSI_DISK_LOG;

static void
scsi_disk_log(const char *fmt, ...)
{
    va_list ap;

    if (scsi_disk_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define scsi_disk_log(fmt, ...)
#endif

static void
scsi_disk_set_callback(scsi_disk_t *dev)
{
    if (dev->drv->bus != HDD_BUS_SCSI)
        ide_set_callback(ide_drives[dev->drv->ide_channel], dev->callback);
}

static void
scsi_disk_init(scsi_disk_t *dev)
{
    if (!dev)
        return;

    /* Do a reset (which will also rezero it). */
    scsi_disk_reset((scsi_common_t *) dev);

    /* Configure the drive. */
    dev->requested_blocks = 1;

    dev->drv->bus_mode = 0;
    if (dev->drv->bus >= HDD_BUS_ATAPI)
        dev->drv->bus_mode |= 2;
    if (dev->drv->bus < HDD_BUS_SCSI)
        dev->drv->bus_mode |= 1;
    scsi_disk_log("SCSI HDD %i: Bus type %i, bus mode %i\n",
                  dev->id, dev->drv->bus, dev->drv->bus_mode);

    dev->sense[0] = 0xf0;
    dev->sense[7] = 10;
    /* NEC only */
    dev->tf->status      = 0;
    dev->tf->pos         = 0;
    dev->packet_status   = PHASE_NONE;
    scsi_disk_sense_key = scsi_disk_asc = scsi_disk_ascq = dev->unit_attention = 0;
    scsi_disk_mode_sense_load(dev);
}

/* Returns: 0 for none, 1 for PIO, 2 for DMA. */
static int
scsi_disk_current_mode(scsi_disk_t *dev)
{
    if (dev->drv->bus == HDD_BUS_SCSI)
        return 2;
    else if (dev->drv->bus == HDD_BUS_ATAPI) {
        scsi_disk_log("SCSI DISK %i: ATAPI drive, setting to %s\n", dev->id,
                      (dev->tf->features & 1) ? "DMA" : "PIO",
                      dev->id);
        return (dev->tf->features & 1) ? 2 : 1;
    }

    return 0;
}

void
scsi_disk_mode_sense_load(scsi_disk_t *dev)
{
    FILE *fp;
    char  file_name[512];

    memset(&dev->ms_pages_saved, 0, sizeof(mode_sense_pages_t));
    memcpy(&dev->ms_pages_saved, &scsi_disk_mode_sense_pages_default, sizeof(mode_sense_pages_t));

    memset(file_name, 0, 512);
    sprintf(file_name, "scsi_disk_%02i_mode_sense.bin", dev->id);
    fp = plat_fopen(nvr_path(file_name), "rb");
    if (fp) {
        if (fread(dev->ms_pages_saved.pages[0x30], 1, 0x18, fp) != 0x18)
            fatal("scsi_disk_mode_sense_load(): Error reading data\n");
        fclose(fp);
    }
}

void
scsi_disk_mode_sense_save(scsi_disk_t *dev)
{
    FILE *fp;
    char  file_name[512];

    memset(file_name, 0, 512);
    sprintf(file_name, "scsi_disk_%02i_mode_sense.bin", dev->id);
    fp = plat_fopen(nvr_path(file_name), "wb");
    if (fp) {
        fwrite(dev->ms_pages_saved.pages[0x30], 1, 0x18, fp);
        fclose(fp);
    }
}

/*SCSI Mode Sense 6/10*/
uint8_t
scsi_disk_mode_sense_read(scsi_disk_t *dev, uint8_t page_control, uint8_t page, uint8_t pos)
{
    if (page_control == 1)
        return scsi_disk_mode_sense_pages_changeable.pages[page][pos];

    if (page == GPMODE_RIGID_DISK_PAGE)
        switch (page_control) {
            /* Rigid disk geometry page. */
            case 0:
            case 2:
            case 3:
                switch (pos) {
                    default:
                    case 0:
                    case 1:
                        return scsi_disk_mode_sense_pages_default.pages[page][pos];
                    case 2:
                    case 6:
                    case 9:
                        return (dev->drv->tracks >> 16) & 0xff;
                    case 3:
                    case 7:
                    case 10:
                        return (dev->drv->tracks >> 8) & 0xff;
                    case 4:
                    case 8:
                    case 11:
                        return dev->drv->tracks & 0xff;
                    case 5:
                        return dev->drv->hpc & 0xff;
                }

            default:
                break;
        }
    else if (page == GPMODE_FORMAT_DEVICE_PAGE)
        switch (page_control) {
            /* Format device page. */
            case 0:
            case 2:
            case 3:
                switch (pos) {
                    default:
                    case 0:
                    case 1:
                        return scsi_disk_mode_sense_pages_default.pages[page][pos];
                    /* Actual sectors + the 1 "alternate sector" we report. */
                    case 10:
                        return ((dev->drv->spt + 1) >> 8) & 0xff;
                    case 11:
                        return (dev->drv->spt + 1) & 0xff;
                }

            default:
                break;
        }
    else
        switch (page_control) {
            case 0:
            case 3:
                return dev->ms_pages_saved.pages[page][pos];
            case 2:
                return scsi_disk_mode_sense_pages_default.pages[page][pos];

            default:
                break;
        }

    return 0;
}

uint32_t
scsi_disk_mode_sense(scsi_disk_t *dev, uint8_t *buf, uint32_t pos, uint8_t page, uint8_t block_descriptor_len)
{
    uint8_t msplen;
    uint8_t page_control = (page >> 6) & 3;
    int     size = 0;

    page &= 0x3f;

    size = hdd_image_get_last_sector(dev->id);

    if (block_descriptor_len) {
        buf[pos++] = 1;                   /* Density code. */
        buf[pos++] = (size >> 16) & 0xff; /* Number of blocks (0 = all). */
        buf[pos++] = (size >> 8) & 0xff;
        buf[pos++] = size & 0xff;
        buf[pos++] = 0; /* Reserved. */
        buf[pos++] = 0; /* Block length (0x200 = 512 bytes). */
        buf[pos++] = 2;
        buf[pos++] = 0;
    }

    for (uint8_t i = 0; i < 0x40; i++) {
        if ((page == GPMODE_ALL_PAGES) || (page == i)) {
            if (scsi_disk_mode_sense_page_flags & (1LL << (uint64_t) page)) {
                buf[pos++] = scsi_disk_mode_sense_read(dev, page_control, i, 0);
                msplen     = scsi_disk_mode_sense_read(dev, page_control, i, 1);
                buf[pos++] = msplen;
                scsi_disk_log("SCSI HDD %i: MODE SENSE: Page [%02X] length %i\n", dev->id, i, msplen);
                for (uint8_t j = 0; j < msplen; j++)
                    buf[pos++] = scsi_disk_mode_sense_read(dev, page_control, i, 2 + j);
            }
        }
    }

    return pos;
}

static void
scsi_disk_update_request_length(scsi_disk_t *dev, int len, int block_len)
{
    int bt;
    int min_len = 0;

    dev->max_transfer_len = dev->tf->request_length;

    /* For media access commands, make sure the requested DRQ length matches the block length. */
    switch (dev->current_cdb[0]) {
        case 0x08:
        case 0x0a:
        case 0x28:
        case 0x2a:
        case 0xa8:
        case 0xaa:
            /* Round it to the nearest 2048 bytes. */
            dev->max_transfer_len = (dev->max_transfer_len >> 9) << 9;

            /* Make sure total length is not bigger than sum of the lengths of
               all the requested blocks. */
            bt = (dev->requested_blocks * block_len);
            if (len > bt)
                len = bt;

            min_len = block_len;

            if (len <= block_len) {
                /* Total length is less or equal to block length. */
                if (dev->max_transfer_len < block_len) {
                    /* Transfer a minimum of (block size) bytes. */
                    dev->max_transfer_len = block_len;
                    dev->packet_len       = block_len;
                    break;
                }
            }
            fallthrough;

        default:
            dev->packet_len = len;
            break;
    }
    /* If the DRQ length is odd, and the total remaining length is bigger, make sure it's even. */
    if ((dev->max_transfer_len & 1) && (dev->max_transfer_len < len))
        dev->max_transfer_len &= 0xfffe;
    /* If the DRQ length is smaller or equal in size to the total remaining length, set it to that. */
    if (!dev->max_transfer_len)
        dev->max_transfer_len = 65534;

    if ((len <= dev->max_transfer_len) && (len >= min_len))
        dev->tf->request_length = dev->max_transfer_len = len;
    else if (len > dev->max_transfer_len)
        dev->tf->request_length = dev->max_transfer_len;

    return;
}

static double
scsi_disk_bus_speed(scsi_disk_t *dev)
{
    double ret = -1.0;

    if (dev && dev->drv && (dev->drv->bus == HDD_BUS_SCSI)) {
        dev->callback = -1.0; /* Speed depends on SCSI controller */
        return 0.0;
    } else {
        if (dev && dev->drv)
            ret = ide_atapi_get_period(dev->drv->ide_channel);
        if (ret == -1.0) {
            if (dev)
                dev->callback = -1.0;
            return 0.0;
        } else
            return ret * 1000000.0;
    }
}

void
scsi_disk_command_common(scsi_disk_t *dev)
{
    double bytes_per_second = 0.0;
    double period;

    /* MAP: BUSY_STAT, no DRQ, phase 1. */
    dev->tf->status    = BUSY_STAT;
    dev->tf->phase     = 1;
    dev->tf->pos       = 0;
    dev->callback      = 0;

    if (dev->packet_status == PHASE_COMPLETE) {
        switch (dev->current_cdb[0]) {
            case GPCMD_VERIFY_6:
            case GPCMD_VERIFY_10:
            case GPCMD_VERIFY_12:
            case GPCMD_WRITE_6:
            case GPCMD_WRITE_10:
            case GPCMD_WRITE_AND_VERIFY_10:
            case GPCMD_WRITE_12:
            case GPCMD_WRITE_AND_VERIFY_12:
            case GPCMD_WRITE_SAME_10:
                /* Seek time is in us. */
                period = hdd_timing_write(dev->drv, dev->drv->seek_pos, dev->drv->seek_len);
                scsi_disk_log("SCSI HD %i: Seek period: %" PRIu64 " us\n",
                              dev->id, (uint64_t) period);
                dev->callback += period;
                /* Account for seek time. */
                bytes_per_second = scsi_bus_get_speed(dev->drv->scsi_id >> 4);

                period = 1000000.0 / bytes_per_second;
                scsi_disk_log("SCSI HD %i: Byte transfer period: %" PRIu64 " us\n", dev->id,
                              (uint64_t) period);
                period = period * (double) (dev->packet_len);
                scsi_disk_log("CD-ROM %i: Sector transfer period: %" PRIu64 " us\n", dev->id,
                              (uint64_t) period);
                dev->callback += period;
                break;
            default:    
                dev->callback = 0;
                break;
        }
    } else {
        switch (dev->current_cdb[0]) {
            case GPCMD_REZERO_UNIT:
            case 0x0b:
            case 0x2b:
                /* Seek time is in us. */
                period = hdd_seek_get_time(dev->drv, (dev->current_cdb[0] == GPCMD_REZERO_UNIT) ?
                                           0 : dev->sector_pos, HDD_OP_SEEK, 0, 0.0);
                scsi_disk_log("SCSI HD %i: Seek period: %" PRIu64 " us\n",
                              dev->id, (uint64_t) period);
                dev->callback += period;
                scsi_disk_set_callback(dev);
                return;
            case 0x08:
            case 0x28:
            case 0xa8:
                /* Seek time is in us. */
                period = hdd_timing_read(dev->drv, dev->drv->seek_pos, dev->drv->seek_len);
                scsi_disk_log("SCSI HD %i: Seek period: %" PRIu64 " us\n",
                              dev->id, (uint64_t) period);
                dev->callback += period;
                /* Account for seek time. */
                bytes_per_second = scsi_bus_get_speed(dev->drv->scsi_id >> 4);
                break;
            case 0x25:
            default:
                bytes_per_second = scsi_disk_bus_speed(dev);
                if (bytes_per_second == 0.0) {
                    dev->callback = -1; /* Speed depends on SCSI controller */
                    return;
                }
                break;
        }

        period = 1000000.0 / bytes_per_second;
        scsi_disk_log("SCSI HD %i: Byte transfer period: %" PRIu64 " us\n", dev->id, (uint64_t) period);
        period = period * (double) (dev->packet_len);
        scsi_disk_log("SCSI HD %i: Sector transfer period: %" PRIu64 " us\n", dev->id, (uint64_t) period);
        dev->callback += period;
    }
    scsi_disk_set_callback(dev);
}

static void
scsi_disk_command_complete(scsi_disk_t *dev)
{
    ui_sb_update_icon(SB_HDD | dev->drv->bus, 0);
    dev->packet_status = PHASE_COMPLETE;
    scsi_disk_command_common(dev);
}

static void
scsi_disk_command_read(scsi_disk_t *dev)
{
    dev->packet_status = PHASE_DATA_IN;
    scsi_disk_command_common(dev);
}

static void
scsi_disk_command_read_dma(scsi_disk_t *dev)
{
    dev->packet_status = PHASE_DATA_IN_DMA;
    scsi_disk_command_common(dev);
}

static void
scsi_disk_command_write(scsi_disk_t *dev)
{
    dev->packet_status = PHASE_DATA_OUT;
    scsi_disk_command_common(dev);
}

static void
scsi_disk_command_write_dma(scsi_disk_t *dev)
{
    dev->packet_status = PHASE_DATA_OUT_DMA;
    scsi_disk_command_common(dev);
}

/* id = Current ZIP device ID;
   len = Total transfer length;
   block_len = Length of a single block (why does it matter?!);
   alloc_len = Allocated transfer length;
   direction = Transfer direction (0 = read from host, 1 = write to host). */
static void
scsi_disk_data_command_finish(scsi_disk_t *dev, int len, int block_len, int alloc_len, int direction)
{
    scsi_disk_log("SCSI HD %i: Finishing command (%02X): %i, %i, %i, %i, %i\n",
                 dev->id, dev->current_cdb[0], len, block_len, alloc_len, direction,
                 dev->tf->request_length);
    dev->tf->pos = 0;
    if (alloc_len >= 0) {
        if (alloc_len < len)
            len = alloc_len;
    }
    if ((len == 0) || (scsi_disk_current_mode(dev) == 0)) {
        if (dev->drv->bus != HDD_BUS_SCSI)
            dev->packet_len = 0;

        scsi_disk_command_complete(dev);
    } else {
        if (scsi_disk_current_mode(dev) == 2) {
            if (dev->drv->bus != HDD_BUS_SCSI)
                dev->packet_len = alloc_len;

            if (direction == 0)
                scsi_disk_command_read_dma(dev);
            else
                scsi_disk_command_write_dma(dev);
        } else {
            scsi_disk_update_request_length(dev, len, block_len);
            if (direction == 0)
                scsi_disk_command_read(dev);
            else
                scsi_disk_command_write(dev);
        }
    }

    scsi_disk_log("SCSI HD %i: Status: %i, cylinder %i, packet length: %i, position: %i, phase: %i\n",
                  dev->id, dev->packet_status, dev->tf->request_length, dev->packet_len, dev->tf->pos,
                  dev->tf->phase);
}

static void
scsi_disk_sense_clear(scsi_disk_t *dev, UNUSED(int command))
{
    scsi_disk_sense_key = scsi_disk_asc = scsi_disk_ascq = 0;
}

static void
scsi_disk_set_phase(scsi_disk_t *dev, uint8_t phase)
{
    uint8_t scsi_bus = (dev->drv->scsi_id >> 4) & 0x0f;
    uint8_t scsi_id  = dev->drv->scsi_id & 0x0f;

    if (dev->drv->bus != HDD_BUS_SCSI)
        return;

    scsi_devices[scsi_bus][scsi_id].phase = phase;
}

static void
scsi_disk_cmd_error(scsi_disk_t *dev)
{
    scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);
    dev->tf->error     = ((scsi_disk_sense_key & 0xf) << 4) | ABRT_ERR;
    dev->tf->status    = READY_STAT | ERR_STAT;
    dev->tf->phase     = 3;
    dev->packet_status = PHASE_ERROR;
    dev->callback      = 50.0 * SCSI_TIME;
    scsi_disk_set_callback(dev);
    ui_sb_update_icon(SB_HDD | dev->drv->bus, 0);
    scsi_disk_log("SCSI HD %i: ERROR: %02X/%02X/%02X\n", dev->id, scsi_disk_sense_key, scsi_disk_asc, scsi_disk_ascq);
}

static void
scsi_disk_buf_alloc(scsi_disk_t *dev, uint32_t len)
{
    scsi_disk_log("SCSI HD %i: Allocated buffer length: %i\n", dev->id, len);
    if (!dev->temp_buffer)
        dev->temp_buffer = (uint8_t *) malloc(len);
}

static void
scsi_disk_buf_free(scsi_disk_t *dev)
{
    if (dev->temp_buffer) {
        scsi_disk_log("SCSI HD %i: Freeing buffer...\n", dev->id);
        free(dev->temp_buffer);
        dev->temp_buffer = NULL;
    }
}

static void
scsi_disk_bus_master_error(scsi_common_t *sc)
{
    scsi_disk_t *dev = (scsi_disk_t *) sc;

    scsi_disk_buf_free(dev);
    scsi_disk_sense_key = scsi_disk_asc = scsi_disk_ascq = 0;
    scsi_disk_cmd_error(dev);
}

static void
scsi_disk_invalid_lun(scsi_disk_t *dev)
{
    scsi_disk_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_disk_asc       = ASC_INV_LUN;
    scsi_disk_ascq      = 0;
    scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);
    scsi_disk_cmd_error(dev);
}

static void
scsi_disk_illegal_opcode(scsi_disk_t *dev)
{
    scsi_disk_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_disk_asc       = ASC_ILLEGAL_OPCODE;
    scsi_disk_ascq      = 0;
    scsi_disk_cmd_error(dev);
}

static void
scsi_disk_lba_out_of_range(scsi_disk_t *dev)
{
    scsi_disk_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_disk_asc       = ASC_LBA_OUT_OF_RANGE;
    scsi_disk_ascq      = 0;
    scsi_disk_cmd_error(dev);
}

static void
scsi_disk_invalid_field(scsi_disk_t *dev)
{
    scsi_disk_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_disk_asc       = ASC_INV_FIELD_IN_CMD_PACKET;
    scsi_disk_ascq      = 0;
    scsi_disk_cmd_error(dev);
    dev->tf->status     = 0x53;
}

static void
scsi_disk_invalid_field_pl(scsi_disk_t *dev)
{
    scsi_disk_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_disk_asc       = ASC_INV_FIELD_IN_PARAMETER_LIST;
    scsi_disk_ascq      = 0;
    scsi_disk_cmd_error(dev);
    dev->tf->status     = 0x53;
}

static void
scsi_disk_data_phase_error(scsi_disk_t *dev)
{
    scsi_disk_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_disk_asc       = ASC_DATA_PHASE_ERROR;
    scsi_disk_ascq      = 0;
    scsi_disk_cmd_error(dev);
}

static int
scsi_disk_blocks(scsi_disk_t *dev, int32_t *len, UNUSED(int first_batch), int out)
{
    *len = 0;
    uint32_t medium_size = hdd_image_get_last_sector(dev->id) + 1;

    if (!dev->sector_len) {
        scsi_disk_command_complete(dev);
        return -1;
    }

    scsi_disk_log("%sing %i blocks starting from %i...\n", out ? "Writ" : "Read",
                  dev->requested_blocks, dev->sector_pos);

    if (dev->sector_pos >= medium_size) {
        scsi_disk_log("SCSI HD %i: Trying to %s beyond the end of disk\n", dev->id, out ? "write" : "read");
        scsi_disk_lba_out_of_range(dev);
        return 0;
    }

    *len = dev->requested_blocks << 9;

    for (int i = 0; i < dev->requested_blocks; i++) {
        if (out)
            hdd_image_write(dev->id, dev->sector_pos + i, 1, dev->temp_buffer + (i << 9));
        else
            hdd_image_read(dev->id, dev->sector_pos + i, 1, dev->temp_buffer + (i << 9));
    }

    scsi_disk_log("%s %i bytes of blocks...\n", out ? "Written" : "Read", *len);

    dev->sector_pos += dev->requested_blocks;
    dev->sector_len -= dev->requested_blocks;

    return 1;
}

static int
scsi_disk_pre_execution_check(scsi_disk_t *dev, uint8_t *cdb)
{
    if ((cdb[0] != GPCMD_REQUEST_SENSE) && (dev->cur_lun == SCSI_LUN_USE_CDB) && (cdb[1] & 0xe0)) {
        scsi_disk_log("SCSI HD %i: Attempting to execute a unknown command targeted at SCSI LUN %i\n",
                      dev->id, ((dev->tf->request_length >> 5) & 7));
        scsi_disk_invalid_lun(dev);
        return 0;
    }

    if (!(scsi_disk_command_flags[cdb[0]] & IMPLEMENTED)) {
        scsi_disk_log("SCSI HD %i: Attempting to execute unknown command %02X over %s\n", dev->id, cdb[0],
                      (dev->drv->bus == HDD_BUS_SCSI) ? "SCSI" : "ATAPI");
        scsi_disk_illegal_opcode(dev);
        return 0;
    }

    /* Unless the command is REQUEST SENSE, clear the sense. This will *NOT*
       the UNIT ATTENTION condition if it's set. */
    if (cdb[0] != GPCMD_REQUEST_SENSE)
        scsi_disk_sense_clear(dev, cdb[0]);

    scsi_disk_log("SCSI HD %i: Continuing with command\n", dev->id);

    return 1;
}

static void
scsi_disk_seek(scsi_disk_t *dev, uint32_t pos)
{
    /* scsi_disk_log("SCSI HD %i: Seek %08X\n", dev->id, pos); */
    hdd_image_seek(dev->id, pos);
}

static void
scsi_disk_rezero(scsi_disk_t *dev)
{
    if (dev->id == 0xff)
        return;

    dev->sector_pos = dev->sector_len = 0;
    scsi_disk_seek(dev, 0);
}

void
scsi_disk_reset(scsi_common_t *sc)
{
    scsi_disk_t *dev = (scsi_disk_t *) sc;

    scsi_disk_rezero(dev);
    dev->tf->status         = 0;
    dev->callback           = 0.0;
    scsi_disk_set_callback(dev);
    dev->tf->phase          = 1;
    dev->tf->request_length = 0xEB14;
    dev->packet_status      = PHASE_NONE;
    dev->unit_attention     = 0;
    dev->cur_lun            = SCSI_LUN_USE_CDB;
}

void
scsi_disk_request_sense(scsi_disk_t *dev, uint8_t *buffer, uint8_t alloc_length, int desc)
{
    /*Will return 18 bytes of 0*/
    if (alloc_length != 0) {
        memset(buffer, 0, alloc_length);
        if (!desc)
            memcpy(buffer, dev->sense, alloc_length);
        else {
            buffer[1] = scsi_disk_sense_key;
            buffer[2] = scsi_disk_asc;
            buffer[3] = scsi_disk_ascq;
        }
    } else
        return;

    buffer[0] = 0x70;

    scsi_disk_log("SCSI HD %i: Reporting sense: %02X %02X %02X\n", dev->id, buffer[2], buffer[12], buffer[13]);

    /* Clear the sense stuff as per the spec. */
    scsi_disk_sense_clear(dev, GPCMD_REQUEST_SENSE);
}

static void
scsi_disk_request_sense_for_scsi(scsi_common_t *sc, uint8_t *buffer, uint8_t alloc_length)
{
    scsi_disk_t *dev = (scsi_disk_t *) sc;

    scsi_disk_request_sense(dev, buffer, alloc_length, 0);
}

static void
scsi_disk_set_buf_len(scsi_disk_t *dev, int32_t *BufLen, int32_t *src_len)
{
    if (dev->drv->bus == HDD_BUS_SCSI) {
        if (*BufLen == -1)
            *BufLen = *src_len;
        else {
            *BufLen  = MIN(*src_len, *BufLen);
            *src_len = *BufLen;
        }
        scsi_disk_log("SCSI HD %i: Actual transfer length: %i\n", dev->id, *BufLen);
    }
}

static void
scsi_disk_command(scsi_common_t *sc, uint8_t *cdb)
{
    scsi_disk_t *dev = (scsi_disk_t *) sc;
    int      ret;
    int32_t      blen = 0;
    int32_t     *BufLen;
    int32_t      len;
    int32_t      max_len;
    int32_t      alloc_length;
    int          pos = 0;
    int          idx = 0;
    unsigned     size_idx;
    unsigned     preamble_len;
    uint32_t     last_sector            = 0;
    char         device_identify[9]     = { '8', '6', 'B', '_', 'H', 'D', '0', '0', 0 };
    char         device_identify_ex[15] = { '8', '6', 'B', '_', 'H', 'D', '0', '0', ' ', 'v', '1', '.', '0', '0', 0 };
    int          block_desc             = 0;
    uint8_t      scsi_bus               = (dev->drv->scsi_id >> 4) & 0x0f;
    uint8_t      scsi_id                = dev->drv->scsi_id & 0x0f;

    if (dev->drv->bus == HDD_BUS_SCSI) {
        BufLen = &scsi_devices[scsi_bus][scsi_id].buffer_length;
        dev->tf->status &= ~ERR_STAT;
    } else {
        BufLen           = &blen;
        dev->tf->error   = 0;
    }

    last_sector = hdd_image_get_last_sector(dev->id);

    dev->packet_len = 0;
    dev->request_pos = 0;

    device_identify[6] = (dev->id / 10) + 0x30;
    device_identify[7] = (dev->id % 10) + 0x30;

    device_identify_ex[6]  = (dev->id / 10) + 0x30;
    device_identify_ex[7]  = (dev->id % 10) + 0x30;
    device_identify_ex[10] = EMU_VERSION_EX[0];
    device_identify_ex[12] = EMU_VERSION_EX[2];
    device_identify_ex[13] = EMU_VERSION_EX[3];

    memcpy(dev->current_cdb, cdb, 12);

    if (cdb[0] != 0) {
        scsi_disk_log("SCSI HD %i: Command 0x%02X, Sense Key %02X, Asc %02X, Ascq %02X\n",
                      dev->id, cdb[0], scsi_disk_sense_key, scsi_disk_asc, scsi_disk_ascq);
        scsi_disk_log("SCSI HD %i: Request length: %04X\n", dev->id, dev->tf->request_length);

        scsi_disk_log("SCSI HD %i: CDB: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n", dev->id,
                      cdb[0], cdb[1], cdb[2], cdb[3], cdb[4], cdb[5], cdb[6], cdb[7],
                      cdb[8], cdb[9], cdb[10], cdb[11]);
    }

    dev->sector_len = 0;

    scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);

    /* This handles the Not Ready/Unit Attention check if it has to be handled at this point. */
    if (scsi_disk_pre_execution_check(dev, cdb) == 0)
        return;

    switch (cdb[0]) {
        case GPCMD_SEND_DIAGNOSTIC:
            if (!(cdb[1] & (1 << 2))) {
                scsi_disk_invalid_field(dev);
                return;
            }
            fallthrough;
        case GPCMD_SCSI_RESERVE:
        case GPCMD_SCSI_RELEASE:
        case GPCMD_TEST_UNIT_READY:
        case GPCMD_FORMAT_UNIT:
            scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);
            scsi_disk_command_complete(dev);
            break;

        case GPCMD_REZERO_UNIT:
            dev->sector_pos = dev->sector_len = 0;

            dev->drv->seek_pos = dev->sector_pos;
            dev->drv->seek_len = dev->sector_len;

            scsi_disk_seek(dev, 0);
            scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);
            break;

        case GPCMD_REQUEST_SENSE:
            /* If there's a unit attention condition and there's a buffered not
               ready, a standalone REQUEST SENSE should forget about the not
               ready, and report unit attention straight away. */
            len = cdb[4];

            if (!len) {
                scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);
                dev->packet_status = PHASE_COMPLETE;
                dev->callback      = 20.0 * SCSI_TIME;
                scsi_disk_set_callback(dev);
                break;
            }

            scsi_disk_buf_alloc(dev, 256);
            scsi_disk_set_buf_len(dev, BufLen, &len);

            if (*BufLen < cdb[4])
                cdb[4] = *BufLen;

            len = (cdb[1] & 1) ? 8 : 18;

            scsi_disk_request_sense(dev, dev->temp_buffer, *BufLen, cdb[1] & 1);
            scsi_disk_set_phase(dev, SCSI_PHASE_DATA_IN);
            scsi_disk_data_command_finish(dev, len, len, cdb[4], 0);
            break;

        case GPCMD_MECHANISM_STATUS:
            scsi_disk_set_phase(dev, SCSI_PHASE_DATA_IN);
            len = (cdb[8] << 8) | cdb[9];

            scsi_disk_buf_alloc(dev, 8);
            scsi_disk_set_buf_len(dev, BufLen, &len);

            memset(dev->temp_buffer, 0, 8);
            dev->temp_buffer[5] = 1;

            scsi_disk_data_command_finish(dev, 8, 8, len, 0);
            break;

        case GPCMD_READ_6:
        case GPCMD_READ_10:
        case GPCMD_READ_12:
            scsi_disk_set_phase(dev, SCSI_PHASE_DATA_IN);
            alloc_length = 512;

            switch (cdb[0]) {
                case GPCMD_READ_6:
                    dev->sector_len = cdb[4];
                    /*
                       For READ (6) and WRITE (6), a length of 0 indicates a
                       transfer of 256 sectors.
                     */
                    if (dev->sector_len == 0)
                        dev->sector_len = 256;
                    dev->sector_pos = ((((uint32_t) cdb[1]) & 0x1f) << 16) |
                                      (((uint32_t) cdb[2]) << 8) | ((uint32_t) cdb[3]);
                    break;
                case GPCMD_READ_10:
                    dev->sector_len = (cdb[7] << 8) | cdb[8];
                    dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
                    break;
                case GPCMD_READ_12:
                    dev->sector_len = (((uint32_t) cdb[6]) << 24) | (((uint32_t) cdb[7]) << 16) |
                                      (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);
                    dev->sector_pos = (((uint32_t) cdb[2]) << 24) | (((uint32_t) cdb[3]) << 16) |
                                      (((uint32_t) cdb[4]) << 8) | ((uint32_t) cdb[5]);
                    break;

                default:
                    break;
            }

            if (dev->sector_pos > last_sector) {
                scsi_disk_lba_out_of_range(dev);
                return;
            }

            if (!dev->sector_len) {
                scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);
                scsi_disk_log("SCSI HD %i: All done - callback set\n", dev->id);
                dev->packet_status = PHASE_COMPLETE;
                dev->callback      = 20.0 * SCSI_TIME;
                scsi_disk_set_callback(dev);
                break;
            }

            max_len               = dev->sector_len;
            /*
               If we're reading all blocks in one go for DMA, why not also for
               PIO, it should NOT matter anyway, this step should be identical
               and only the way the read dat is transferred to the host should
               be different.
             */
            dev->requested_blocks = max_len;

            dev->packet_len = max_len * alloc_length;
            scsi_disk_buf_alloc(dev, dev->packet_len);

            dev->drv->seek_pos = dev->sector_pos;
            dev->drv->seek_len = dev->sector_len;

            ret = scsi_disk_blocks(dev, &alloc_length, 1, 0);
            if (ret <= 0) {
                scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);
                dev->packet_status = PHASE_COMPLETE;
                dev->callback      = 20.0 * SCSI_TIME;
                scsi_disk_set_callback(dev);
                scsi_disk_buf_free(dev);
                return;
            }

            dev->requested_blocks = max_len;
            dev->packet_len       = alloc_length;

            scsi_disk_set_buf_len(dev, BufLen, (int32_t *) &dev->packet_len);

            scsi_disk_data_command_finish(dev, alloc_length, 512, alloc_length, 0);

            ui_sb_update_icon(SB_HDD | dev->drv->bus, dev->packet_status != PHASE_COMPLETE);
            return;

        case GPCMD_VERIFY_6:
        case GPCMD_VERIFY_10:
        case GPCMD_VERIFY_12:
            if (!(cdb[1] & 2)) {
                scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);
                scsi_disk_command_complete(dev);
                break;
            }
            fallthrough;
        case GPCMD_WRITE_6:
        case GPCMD_WRITE_10:
        case GPCMD_WRITE_AND_VERIFY_10:
        case GPCMD_WRITE_12:
        case GPCMD_WRITE_AND_VERIFY_12:
            scsi_disk_set_phase(dev, SCSI_PHASE_DATA_OUT);
            alloc_length = 512;

            switch (cdb[0]) {
                case GPCMD_VERIFY_6:
                case GPCMD_WRITE_6:
                    dev->sector_len = cdb[4];
                    /*
                       For READ (6) and WRITE (6), a length of 0 indicates a
                       transfer of 256 sectors.
                     */
                    if (dev->sector_len == 0)
                        dev->sector_len = 256;
                    dev->sector_pos = ((((uint32_t) cdb[1]) & 0x1f) << 16) |
                                      (((uint32_t) cdb[2]) << 8) | ((uint32_t) cdb[3]);
                    scsi_disk_log("SCSI HD %i: Length: %i, LBA: %i\n", dev->id,
                                  dev->sector_len, dev->sector_pos);
                    break;
                case GPCMD_VERIFY_10:
                case GPCMD_WRITE_10:
                case GPCMD_WRITE_AND_VERIFY_10:
                    dev->sector_len = (cdb[7] << 8) | cdb[8];
                    dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
                    scsi_disk_log("SCSI HD %i: Length: %i, LBA: %i\n", dev->id,
                                  dev->sector_len, dev->sector_pos);
                    break;
                case GPCMD_VERIFY_12:
                case GPCMD_WRITE_12:
                case GPCMD_WRITE_AND_VERIFY_12:
                    dev->sector_len = (((uint32_t) cdb[6]) << 24) | (((uint32_t) cdb[7]) << 16) |
                                      (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);
                    dev->sector_pos = (((uint32_t) cdb[2]) << 24) | (((uint32_t) cdb[3]) << 16) |
                                      (((uint32_t) cdb[4]) << 8) | ((uint32_t) cdb[5]);
                    break;

                default:
                    break;
            }

            if (dev->sector_pos > last_sector) {
                scsi_disk_lba_out_of_range(dev);
                return;
            }

            if (!dev->sector_len) {
                scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);
                scsi_disk_log("SCSI HD %i: All done - callback set\n", dev->id);
                dev->packet_status = PHASE_COMPLETE;
                dev->callback      = 20.0 * SCSI_TIME;
                scsi_disk_set_callback(dev);
                break;
            }

            dev->drv->seek_pos = dev->sector_pos;
            dev->drv->seek_len = dev->sector_len;

            max_len               = dev->sector_len;
            /*
               If we're writing all blocks in one go for DMA, why not also for
               PIO, it should NOT matter anyway, this step should be identical
               and only the way the read dat is transferred to the host should
               be different.
             */
            dev->requested_blocks = max_len;

            dev->packet_len = max_len * alloc_length;
            scsi_disk_buf_alloc(dev, dev->packet_len);

            dev->requested_blocks = max_len;
            dev->packet_len       = max_len << 9;

            scsi_disk_set_buf_len(dev, BufLen, (int32_t *) &dev->packet_len);

            scsi_disk_data_command_finish(dev, dev->packet_len, 512, dev->packet_len, 1);

            ui_sb_update_icon(SB_HDD | dev->drv->bus, dev->packet_status != PHASE_COMPLETE);
            return;

        case GPCMD_WRITE_SAME_10:
            alloc_length = 512;

            if ((cdb[1] & 6) == 6) {
                scsi_disk_invalid_field(dev);
                return;
            }

            dev->sector_len = (cdb[7] << 8) | cdb[8];
            dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];

            if (dev->sector_pos > last_sector) {
                scsi_disk_lba_out_of_range(dev);
                return;
            }

            if (!dev->sector_len) {
                scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);
                scsi_disk_log("SCSI HD %i: All done - callback set\n", dev->id);
                dev->packet_status = PHASE_COMPLETE;
                dev->callback      = 20.0 * SCSI_TIME;
                scsi_disk_set_callback(dev);
                break;
            }

            scsi_disk_buf_alloc(dev, alloc_length);
            scsi_disk_set_buf_len(dev, BufLen, (int32_t *) &dev->packet_len);

            max_len               = 1;
            dev->requested_blocks = 1;

            dev->packet_len = alloc_length;

            scsi_disk_set_phase(dev, SCSI_PHASE_DATA_OUT);

            scsi_disk_data_command_finish(dev, 512, 512, alloc_length, 1);

            ui_sb_update_icon(SB_HDD | dev->drv->bus, dev->packet_status != PHASE_COMPLETE);
            return;

        case GPCMD_MODE_SENSE_6:
        case GPCMD_MODE_SENSE_10:
            scsi_disk_set_phase(dev, SCSI_PHASE_DATA_IN);

            if (dev->drv->bus == HDD_BUS_SCSI)
                block_desc = ((cdb[1] >> 3) & 1) ? 0 : 1;
            else
                block_desc = 0;

            if (cdb[0] == GPCMD_MODE_SENSE_6) {
                len = cdb[4];
                scsi_disk_buf_alloc(dev, 256);
            } else {
                len = (cdb[8] | (cdb[7] << 8));
                scsi_disk_buf_alloc(dev, 65536);
            }

            memset(dev->temp_buffer, 0, len);
            alloc_length = len;

            if (cdb[0] == GPCMD_MODE_SENSE_6) {
                len = scsi_disk_mode_sense(dev, dev->temp_buffer, 4, cdb[2], block_desc);
                if (len > alloc_length)
                    len = alloc_length;
                dev->temp_buffer[0] = len - 1;
                dev->temp_buffer[1] = 0;
                if (block_desc)
                    dev->temp_buffer[3] = 8;
            } else {
                len = scsi_disk_mode_sense(dev, dev->temp_buffer, 8, cdb[2], block_desc);
                if (len > alloc_length)
                    len = alloc_length;
                dev->temp_buffer[0] = (len - 2) >> 8;
                dev->temp_buffer[1] = (len - 2) & 255;
                dev->temp_buffer[2] = 0;
                if (block_desc) {
                    dev->temp_buffer[6] = 0;
                    dev->temp_buffer[7] = 8;
                }
            }

            if (len > alloc_length)
                len = alloc_length;
            else if (len < alloc_length)
                alloc_length = len;

            scsi_disk_set_buf_len(dev, BufLen, &alloc_length);
            scsi_disk_log("SCSI HDD %i: Reading mode page: %02X...\n", dev->id, cdb[2]);

            scsi_disk_data_command_finish(dev, len, len, alloc_length, 0);
            return;

        case GPCMD_MODE_SELECT_6:
        case GPCMD_MODE_SELECT_10:
            scsi_disk_set_phase(dev, SCSI_PHASE_DATA_OUT);

            if (cdb[0] == GPCMD_MODE_SELECT_6) {
                len = cdb[4];
                scsi_disk_buf_alloc(dev, 256);
            } else {
                len = (cdb[7] << 8) | cdb[8];
                scsi_disk_buf_alloc(dev, 65536);
            }

            scsi_disk_set_buf_len(dev, BufLen, &len);
            dev->total_length = len;
            dev->do_page_save = cdb[1] & 1;
            scsi_disk_data_command_finish(dev, len, len, len, 1);
            return;

        case GPCMD_INQUIRY:
            max_len = cdb[3];
            max_len <<= 8;
            max_len |= cdb[4];

            if ((!max_len) || (*BufLen == 0)) {
                scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);
                /* scsi_disk_log("SCSI HD %i: All done - callback set\n", dev->id); */
                dev->packet_status = PHASE_COMPLETE;
                dev->callback      = 20.0 * SCSI_TIME;
                break;
            }

            scsi_disk_buf_alloc(dev, 65536);

            if (cdb[1] & 1) {
                preamble_len = 4;
                size_idx     = 3;

                dev->temp_buffer[idx++] = 05;
                dev->temp_buffer[idx++] = cdb[2];
                dev->temp_buffer[idx++] = 0;

                idx++;

                switch (cdb[2]) {
                    case 0x00:
                        dev->temp_buffer[idx++] = 0x00;
                        dev->temp_buffer[idx++] = 0x83;
                        break;
                    case 0x83:
                        if (idx + 24 > max_len) {
                            scsi_disk_buf_free(dev);
                            scsi_disk_data_phase_error(dev);
                            return;
                        }

                        dev->temp_buffer[idx++] = 0x02;
                        dev->temp_buffer[idx++] = 0x00;
                        dev->temp_buffer[idx++] = 0x00;
                        dev->temp_buffer[idx++] = 20;
                        ide_padstr8(dev->temp_buffer + idx, 20, "53R141"); /* Serial */
                        idx += 20;

                        if (idx + 72 > cdb[4])
                            goto atapi_out;
                        dev->temp_buffer[idx++] = 0x02;
                        dev->temp_buffer[idx++] = 0x01;
                        dev->temp_buffer[idx++] = 0x00;
                        dev->temp_buffer[idx++] = 68;
                        ide_padstr8(dev->temp_buffer + idx, 8, EMU_NAME); /* Vendor */
                        idx += 8;
                        ide_padstr8(dev->temp_buffer + idx, 40, device_identify_ex); /* Product */
                        idx += 40;
                        ide_padstr8(dev->temp_buffer + idx, 20, "53R141"); /* Product */
                        idx += 20;
                        break;
                    default:
                        scsi_disk_log("INQUIRY: Invalid page: %02X\n", cdb[2]);
                        scsi_disk_invalid_field(dev);
                        scsi_disk_buf_free(dev);
                        return;
                }
            } else {
                preamble_len = 5;
                size_idx     = 4;

                memset(dev->temp_buffer, 0, 8);
                dev->temp_buffer[0] = 0;    /*SCSI HD*/
                dev->temp_buffer[1] = 0;    /*Fixed*/
                dev->temp_buffer[2] = (dev->drv->bus == HDD_BUS_SCSI) ? 0x02 : 0x00; /*SCSI-2 compliant*/
                dev->temp_buffer[3] = (dev->drv->bus == HDD_BUS_SCSI) ? 0x02 : 0x21;
                dev->temp_buffer[4] = 31;
                dev->temp_buffer[6] = 1;    /* 16-bit transfers supported */
                dev->temp_buffer[7] = 0x20; /* Wide bus supported */

                ide_padstr8(dev->temp_buffer + 8, 8, EMU_NAME);          /* Vendor */
                ide_padstr8(dev->temp_buffer + 16, 16, device_identify); /* Product */
                ide_padstr8(dev->temp_buffer + 32, 4, EMU_VERSION_EX);   /* Revision */
                idx = 36;

                if (max_len == 96) {
                    dev->temp_buffer[4] = 91;
                    idx                 = 96;
                }
            }

atapi_out:
            dev->temp_buffer[size_idx] = idx - preamble_len;
            len                        = idx;

            if (len > max_len)
                len = max_len;

            scsi_disk_set_buf_len(dev, BufLen, &len);

            if (len > *BufLen)
                len = *BufLen;

            scsi_disk_set_phase(dev, SCSI_PHASE_DATA_IN);
            scsi_disk_data_command_finish(dev, len, len, max_len, 0);
            break;

        case GPCMD_PREVENT_REMOVAL:
            scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);
            scsi_disk_command_complete(dev);
            break;

        case GPCMD_SEEK_6:
        case GPCMD_SEEK_10:
            switch (cdb[0]) {
                case GPCMD_SEEK_6:
                    pos = (cdb[2] << 8) | cdb[3];
                    break;
                case GPCMD_SEEK_10:
                    pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
                    break;

                default:
                    break;
            }

            dev->drv->seek_pos = dev->sector_pos;
            dev->drv->seek_len = 0;

            scsi_disk_seek(dev, pos);

            scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);
            scsi_disk_command_complete(dev);
            break;

        case GPCMD_READ_CDROM_CAPACITY:
            scsi_disk_buf_alloc(dev, 8);

            max_len = hdd_image_get_last_sector(dev->id);
            memset(dev->temp_buffer, 0, 8);
            dev->temp_buffer[0] = (max_len >> 24) & 0xff;
            dev->temp_buffer[1] = (max_len >> 16) & 0xff;
            dev->temp_buffer[2] = (max_len >> 8) & 0xff;
            dev->temp_buffer[3] = max_len & 0xff;
            dev->temp_buffer[6] = 2;
            len                 = 8;

            scsi_disk_set_buf_len(dev, BufLen, &len);

            scsi_disk_set_phase(dev, SCSI_PHASE_DATA_IN);
            scsi_disk_data_command_finish(dev, len, len, len, 0);
            break;

        default:
            scsi_disk_illegal_opcode(dev);
            break;
    }

    /* scsi_disk_log("SCSI HD %i: Phase: %02X, request length: %i\n", dev->id, dev->tf->phase,
                     dev->tf->request_length); */

    if ((dev->packet_status == PHASE_COMPLETE) || (dev->packet_status == PHASE_ERROR))
        scsi_disk_buf_free(dev);
}

static void
scsi_disk_command_stop(scsi_common_t *sc)
{
    scsi_disk_t *dev = (scsi_disk_t *) sc;

    scsi_disk_command_complete(dev);
    scsi_disk_buf_free(dev);
}

static uint8_t
scsi_disk_phase_data_out(scsi_common_t *sc)
{
    scsi_disk_t   *dev      = (scsi_disk_t *) sc;
    uint8_t        scsi_bus = (dev->drv->scsi_id >> 4) & 0x0f;
    uint8_t        scsi_id  = dev->drv->scsi_id & 0x0f;
    int            i;
    const int32_t *BufLen      = &scsi_devices[scsi_bus][scsi_id].buffer_length;
    uint32_t       last_sector = hdd_image_get_last_sector(dev->id);
    uint32_t       c;
    uint32_t       h;
    uint32_t       s;
    int            len = 0;
    uint32_t       last_to_write = 0;
    uint16_t       block_desc_len;
    uint16_t       pos;
    uint16_t       param_list_len;
    uint8_t        hdr_len;
    uint8_t        val;
    uint8_t        old_val;
    uint8_t        ch;
    uint8_t        error = 0;
    uint8_t        page;
    uint8_t        page_len;

    if (!*BufLen) {
        scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);

        return 1;
    }

    switch (dev->current_cdb[0]) {
        case GPCMD_VERIFY_6:
        case GPCMD_VERIFY_10:
        case GPCMD_VERIFY_12:
            break;
        case GPCMD_WRITE_6:
        case GPCMD_WRITE_10:
        case GPCMD_WRITE_AND_VERIFY_10:
        case GPCMD_WRITE_12:
        case GPCMD_WRITE_AND_VERIFY_12:
            if (dev->requested_blocks > 0)
                scsi_disk_blocks(dev, &len, 1, 1);
            break;
        case GPCMD_WRITE_SAME_10:
            if (!dev->current_cdb[7] && !dev->current_cdb[8])
                last_to_write = last_sector;
            else
                last_to_write = dev->sector_pos + dev->sector_len - 1;

            for (i = dev->sector_pos; i <= (int) last_to_write; i++) {
                if (dev->current_cdb[1] & 2) {
                    dev->temp_buffer[0] = (i >> 24) & 0xff;
                    dev->temp_buffer[1] = (i >> 16) & 0xff;
                    dev->temp_buffer[2] = (i >> 8) & 0xff;
                    dev->temp_buffer[3] = i & 0xff;
                } else if (dev->current_cdb[1] & 4) {
                    s                   = (i % dev->drv->spt);
                    h                   = ((i - s) / dev->drv->spt) % dev->drv->hpc;
                    c                   = ((i - s) / dev->drv->spt) / dev->drv->hpc;
                    dev->temp_buffer[0] = (c >> 16) & 0xff;
                    dev->temp_buffer[1] = (c >> 8) & 0xff;
                    dev->temp_buffer[2] = c & 0xff;
                    dev->temp_buffer[3] = h & 0xff;
                    dev->temp_buffer[4] = (s >> 24) & 0xff;
                    dev->temp_buffer[5] = (s >> 16) & 0xff;
                    dev->temp_buffer[6] = (s >> 8) & 0xff;
                    dev->temp_buffer[7] = s & 0xff;
                }
                hdd_image_write(dev->id, i, 1, dev->temp_buffer);
            }
            break;
        case GPCMD_MODE_SELECT_6:
        case GPCMD_MODE_SELECT_10:
            if (dev->current_cdb[0] == GPCMD_MODE_SELECT_10) {
                hdr_len        = 8;
                param_list_len = dev->current_cdb[7];
                param_list_len <<= 8;
                param_list_len |= dev->current_cdb[8];
            } else {
                hdr_len        = 4;
                param_list_len = dev->current_cdb[4];
            }

            if (dev->drv->bus == HDD_BUS_SCSI) {
                if (dev->current_cdb[0] == GPCMD_MODE_SELECT_6) {
                    block_desc_len = dev->temp_buffer[2];
                    block_desc_len <<= 8;
                    block_desc_len |= dev->temp_buffer[3];
                } else {
                    block_desc_len = dev->temp_buffer[6];
                    block_desc_len <<= 8;
                    block_desc_len |= dev->temp_buffer[7];
                }
            } else
                block_desc_len = 0;

            pos = hdr_len + block_desc_len;

            while (1) {
                if (pos >= param_list_len) {
                    scsi_disk_log("SCSI HD %i: Buffer has only block descriptor\n", dev->id);
                    break;
                }

                page     = dev->temp_buffer[pos] & 0x3F;
                page_len = dev->temp_buffer[pos + 1];

                pos += 2;

                if (!(scsi_disk_mode_sense_page_flags & (1LL << ((uint64_t) page))))
                    error |= 1;
                else {
                    for (i = 0; i < page_len; i++) {
                        ch      = scsi_disk_mode_sense_pages_changeable.pages[page][i + 2];
                        val     = dev->temp_buffer[pos + i];
                        old_val = dev->ms_pages_saved.pages[page][i + 2];
                        if (val != old_val) {
                            if (ch)
                                dev->ms_pages_saved.pages[page][i + 2] = val;
                            else
                                error |= 1;
                        }
                    }
                }

                pos += page_len;

                val = scsi_disk_mode_sense_pages_default.pages[page][0] & 0x80;
                if (dev->do_page_save && val)
                    scsi_disk_mode_sense_save(dev);

                if (pos >= dev->total_length)
                    break;
            }

            if (error) {
                scsi_disk_buf_free(dev);
                scsi_disk_invalid_field_pl(dev);
            }
            break;
        default:
            fatal("SCSI HDD %i: Bad Command for phase 2 (%02X)\n", dev->id, dev->current_cdb[0]);
            break;
    }

    scsi_disk_command_stop((scsi_common_t *) dev);
    return 1;
}

static int
scsi_disk_get_max(int ide_has_dma, int type)
{
    int ret;

    switch (type) {
        case TYPE_PIO:
            ret = ide_has_dma ? 4 : 0;
            break;
        case TYPE_SDMA:
            ret = ide_has_dma ? 2 : -1;
            break;
        case TYPE_MDMA:
            ret = ide_has_dma ? 2 : -1;
            break;
        case TYPE_UDMA:
            ret = ide_has_dma ? 5 : -1;
            break;
        default:
            ret = -1;
            break;
    }

    return ret;
}

static int
scsi_disk_get_timings(int ide_has_dma, int type)
{
    int ret;

    switch (type) {
        case TIMINGS_DMA:
            ret = ide_has_dma ? 120 : 0;
            break;
        case TIMINGS_PIO:
            ret = ide_has_dma ? 120 : 0;
            break;
        case TIMINGS_PIO_FC:
            ret = 0;
            break;
        default:
            ret = 0;
            break;
    }

    return ret;
}

/**
 * Fill in ide->buffer with the output of the "IDENTIFY PACKET DEVICE" command
 */
static void
scsi_disk_identify(ide_t *ide, int ide_has_dma)
{
    const scsi_disk_t *dev;
    char          device_identify[9] = { '8', '6', 'B', '_', 'H', 'D', '0', '0', 0 };

    dev = (scsi_disk_t *) ide->sc;

    device_identify[7] = dev->id + 0x30;
    scsi_disk_log("ATAPI Identify: %s\n", device_identify);

    /* ATAPI device, direct-access device, non-removable media, accelerated DRQ */
    ide->buffer[0] = 0x8000 | (0 << 8) | 0x00 | (2 << 5); 
    ide_padstr((char *) (ide->buffer + 10), "", 20);          /* Serial Number */

    ide_padstr((char *) (ide->buffer + 23), EMU_VERSION_EX, 8);   /* Firmware */
    ide_padstr((char *) (ide->buffer + 27), device_identify, 40); /* Model */

    ide->buffer[49]  = 0x200;  /* LBA supported */
    ide->buffer[126] = 0xfffe; /* Interpret zero byte count limit as maximum length */

    if (ide_has_dma) {
        ide->buffer[71] = 30;
        ide->buffer[72] = 30;
        ide->buffer[80] = 0x7e; /*ATA-1 to ATA-6 supported*/
        ide->buffer[81] = 0x19; /*ATA-6 revision 3a supported*/
    }
}

void
scsi_disk_hard_reset(void)
{
    scsi_disk_t   *dev;
    scsi_device_t *sd;
    ide_t         *id;
    uint8_t        scsi_bus;
    uint8_t        scsi_id;
    uint8_t        valid = 0;

    for (uint8_t c = 0; c < HDD_NUM; c++) {
        valid = 0;
        if (hdd[c].bus == HDD_BUS_SCSI) {
            scsi_disk_log("SCSI disk hard_reset drive=%d\n", c);

            scsi_bus = (hdd[c].scsi_id >> 4) & 0x0f;
            scsi_id  = hdd[c].scsi_id & 0x0f;

            /* Make sure to ignore any SCSI disk that has an out of range SCSI bus. */
            if (scsi_bus >= SCSI_BUS_MAX)
                continue;

            /* Make sure to ignore any SCSI disk that has an out of range ID. */
            if (scsi_id >= SCSI_ID_MAX)
                continue;

            /* Make sure to ignore any SCSI disk whose image file name is empty. */
            if (strlen(hdd[c].fn) == 0)
                continue;

            /* Make sure to ignore any SCSI disk whose image fails to load. */
            if (!hdd_image_load(c))
                continue;

            valid = 1;

            hdd_preset_apply(c);

            if (!hdd[c].priv)
                hdd[c].priv = (scsi_disk_t *) calloc(1, sizeof(scsi_disk_t));

            dev = (scsi_disk_t *) hdd[c].priv;

            if (!dev->tf)
                dev->tf     = (ide_tf_t *) calloc(1, sizeof(ide_tf_t));

            /* SCSI disk, attach to the SCSI bus. */
            sd = &scsi_devices[scsi_bus][scsi_id];

            sd->sc             = (scsi_common_t *) dev;
            sd->command        = scsi_disk_command;
            sd->request_sense  = scsi_disk_request_sense_for_scsi;
            sd->reset          = scsi_disk_reset;
            sd->phase_data_out = scsi_disk_phase_data_out;
            sd->command_stop   = scsi_disk_command_stop;
            sd->type           = SCSI_FIXED_DISK;

            scsi_disk_log("SCSI disk %i attached to SCSI ID %i\n", c, hdd[c].scsi_id);
        } else if (hdd[c].bus == HDD_BUS_ATAPI) {
            /* Make sure to ignore any SCSI disk whose image file name is empty. */
            if (strlen(hdd[c].fn) == 0)
                continue;

            /* Make sure to ignore any SCSI disk whose image fails to load. */
            /* ATAPI hard disk, attach to the IDE bus. */
            id = ide_get_drive(hdd[c].ide_channel);
            /* If the IDE channel is initialized, we attach to it,
               otherwise, we do nothing - it's going to be a drive
               that's not attached to anything. */
            if (id) {
                if (!hdd_image_load(c))
                    continue;

                valid = 1;

                hdd_preset_apply(c);

                if (!hdd[c].priv)
                    hdd[c].priv = (scsi_disk_t *) calloc(1, sizeof(scsi_disk_t));

                dev = (scsi_disk_t *) hdd[c].priv;

                id->sc               = (scsi_common_t *) dev;
                dev->tf              = id->tf;
                IDE_ATAPI_IS_EARLY   = 0;
                id->get_max          = scsi_disk_get_max;
                id->get_timings      = scsi_disk_get_timings;
                id->identify         = scsi_disk_identify;
                id->stop             = NULL;
                id->packet_command   = scsi_disk_command;
                id->device_reset     = scsi_disk_reset;
                id->phase_data_out   = scsi_disk_phase_data_out;
                id->command_stop     = scsi_disk_command_stop;
                id->bus_master_error = scsi_disk_bus_master_error;
                id->interrupt_drq    = 0;

                ide_atapi_attach(id);
            }

            scsi_disk_log("ATAPI hard disk drive %i attached to IDE channel %i\n", c, hdd[c].ide_channel);
        }

        if (valid) {
            dev->id      = c;
            dev->drv     = &hdd[c];

            dev->cur_lun = SCSI_LUN_USE_CDB;

            scsi_disk_init(dev);

            scsi_disk_mode_sense_load(dev);
        }
    }
}

void
scsi_disk_close(void)
{
    scsi_disk_t *dev;
    uint8_t      scsi_bus;
    uint8_t      scsi_id;

    for (uint8_t c = 0; c < HDD_NUM; c++) {
        if ((hdd[c].bus == HDD_BUS_SCSI) || (hdd[c].bus == HDD_BUS_ATAPI)) {
            if (hdd[c].bus == HDD_BUS_SCSI) {
                scsi_bus = (hdd[c].scsi_id >> 4) & 0x0f;
                scsi_id  = hdd[c].scsi_id & 0x0f;

                memset(&scsi_devices[scsi_bus][scsi_id], 0x00, sizeof(scsi_device_t));
            }

            hdd_image_close(c);

            dev = hdd[c].priv;

            if (dev) {
                if (dev->tf)
                    free(dev->tf);

                free(dev);
                hdd[c].priv = NULL;
            }
        }
    }
}
