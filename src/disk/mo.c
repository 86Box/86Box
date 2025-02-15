/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of a generic Magneto-Optical Disk drive
 *          commands, for both ATAPI and SCSI usage.
 *
 * Authors: Natalia Portillo <claunia@claunia.com>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2020-2025 Natalia Portillo.
 *          Copyright 2020-2025 Miran Grca.
 *          Copyright 2020-2025 Fred N. van Kempen
 */
#ifdef ENABLE_MO_LOG
#include <stdarg.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/log.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/nvr.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/hdc_ide.h>
#include <86box/mo.h>
#include <86box/version.h>

#ifdef _WIN32
#    include <windows.h>
#    include <io.h>
#else
#    include <unistd.h>
#endif

#define IDE_ATAPI_IS_EARLY             id->sc->pad0

mo_drive_t mo_drives[MO_NUM];

// clang-format off
/*
   Table of all SCSI commands and their flags, needed for the new disc change /
   not ready handler.
 */
const uint8_t mo_command_flags[0x100] = {
    [0x00]          = IMPLEMENTED | CHECK_READY,
    [0x01]          = IMPLEMENTED | ALLOW_UA | SCSI_ONLY,
    [0x03]          = IMPLEMENTED | ALLOW_UA,
    [0x04]          = IMPLEMENTED | CHECK_READY | ALLOW_UA | SCSI_ONLY,
    [0x08]          = IMPLEMENTED | CHECK_READY,
    [0x0a]          = IMPLEMENTED | CHECK_READY,
    [0x0b]          = IMPLEMENTED | CHECK_READY,
    [0x12]          = IMPLEMENTED | ALLOW_UA,
    [0x13]          = IMPLEMENTED | CHECK_READY | SCSI_ONLY,
    [0x15]          = IMPLEMENTED,
    [0x16]          = IMPLEMENTED | SCSI_ONLY,
    [0x17]          = IMPLEMENTED | SCSI_ONLY,
    [0x1a]          = IMPLEMENTED,
    [0x1b]          = IMPLEMENTED | CHECK_READY,
    [0x1d]          = IMPLEMENTED,
    [0x1e]          = IMPLEMENTED | CHECK_READY,
    [0x25]          = IMPLEMENTED | CHECK_READY,
    [0x28]          = IMPLEMENTED | CHECK_READY,
    [0x2a ... 0x2c] = IMPLEMENTED | CHECK_READY,
    [0x2e]          = IMPLEMENTED | CHECK_READY,
    [0x2f]          = IMPLEMENTED | CHECK_READY | SCSI_ONLY,
    [0x41]          = IMPLEMENTED | CHECK_READY,
    [0x55]          = IMPLEMENTED,
    [0x5a]          = IMPLEMENTED,
    [0xa8]          = IMPLEMENTED | CHECK_READY,
    [0xaa]          = IMPLEMENTED | CHECK_READY,
    [0xac]          = IMPLEMENTED | CHECK_READY,
    [0xae]          = IMPLEMENTED | CHECK_READY,
    [0xaf]          = IMPLEMENTED | CHECK_READY | SCSI_ONLY
};

static uint64_t mo_mode_sense_page_flags = GPMODEP_ALL_PAGES;

static const mode_sense_pages_t mo_mode_sense_pages_default      = { 0 };

static const mode_sense_pages_t mo_mode_sense_pages_default_scsi = { 0 };

static const mode_sense_pages_t mo_mode_sense_pages_changeable   = { 0 };
// clang-format on

static void mo_command_complete(mo_t *dev);
static void mo_init(mo_t *dev);

#ifdef ENABLE_MO_LOG
int mo_do_log = ENABLE_MO_LOG;

static void
mo_log(void *priv, const char *fmt, ...)
{
    va_list ap;

    if (mo_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define mo_log(priv, fmt, ...)
#endif

static int
mo_load_abort(const mo_t *dev)
{
    if (dev->drv->fp)
        fclose(dev->drv->fp);
    dev->drv->fp           = NULL;
    dev->drv->medium_size = 0;
    dev->drv->sector_size = 0;
    mo_eject(dev->id); /* Make sure the host OS knows we've rejected (and ejected) the image. */
    return 0;
}

int
image_is_mdi(const char *s)
{
    return !strcasecmp(path_get_extension((char *) s), "MDI");
}

int
mo_is_empty(const uint8_t id)
{
    const mo_t *dev = (const mo_t *) mo_drives[id].priv;
    int         ret = 0;

    if ((dev->drv == NULL) || (dev->drv->fp == NULL))
        ret = 1;

    return ret;
}

void
mo_load(const mo_t *dev, const char *fn, const int skip_insert)
{
    const int was_empty = mo_is_empty(dev->id);
    int       ret       = 0;

    if (dev->drv == NULL)
        mo_eject(dev->id);
    else {
        const int is_mdi = image_is_mdi(fn);

        dev->drv->fp     = plat_fopen(fn, dev->drv->read_only ? "rb" : "rb+");
        ret              = 1;

        if (dev->drv->fp == NULL) {
            if (!dev->drv->read_only) {
                dev->drv->fp = plat_fopen(fn, "rb");
                if (dev->drv->fp == NULL)
                    ret = mo_load_abort(dev);
                else
                    dev->drv->read_only = 1;
            } else
                ret = mo_load_abort(dev);
        }

        if (ret) {
            fseek(dev->drv->fp, 0, SEEK_END);

            uint32_t     size  = (uint32_t) ftell(dev->drv->fp);
            unsigned int found = 0;

            if (is_mdi) {
                /* This is a MDI image. */
                size -= 0x1000LL;
                dev->drv->base = 0x1000;
            } else
                dev->drv->base = 0;

            for (uint8_t i = 0; i < KNOWN_MO_TYPES; i++) {
                if (size == (mo_types[i].sectors * mo_types[i].bytes_per_sector)) {
                    found                 = 1;
                    dev->drv->medium_size = mo_types[i].sectors;
                    dev->drv->sector_size = mo_types[i].bytes_per_sector;
                    break;
                }
            }

            if (found) {
                if (fseek(dev->drv->fp, dev->drv->base, SEEK_SET) == -1)
                    log_fatal(dev->log, "mo_load(): Error seeking to the beginning of "
                              "the file\n");

                strncpy(dev->drv->image_path, fn, sizeof(dev->drv->image_path) - 1);

                ret = 1;
            } else
                ret = mo_load_abort(dev);
        }
    }

    if (ret && !skip_insert) {
        /* Signal media change to the emulated machine. */
        mo_insert((mo_t *) dev);

        /* The drive was previously empty, transition directly to UNIT ATTENTION. */
        if (was_empty)
            mo_insert((mo_t *) dev);
    }
}

void
mo_disk_reload(const mo_t *dev)
{
    if (strlen(dev->drv->prev_image_path) != 0)
        (void) mo_load(dev, dev->drv->prev_image_path, 0);
}

static void
mo_disk_unload(const mo_t *dev)
{
    if ((dev->drv != NULL) && (dev->drv->fp != NULL)) {
        fclose(dev->drv->fp);
        dev->drv->fp = NULL;
    }
}

void
mo_disk_close(const mo_t *dev)
{
    if ((dev->drv != NULL) && (dev->drv->fp != NULL)) {
        mo_disk_unload(dev);

        memcpy(dev->drv->prev_image_path, dev->drv->image_path,
               sizeof(dev->drv->prev_image_path));
        memset(dev->drv->image_path, 0, sizeof(dev->drv->image_path));

        dev->drv->medium_size = 0;

        mo_insert((mo_t *) dev);
    }
}

static void
mo_set_callback(const mo_t *dev)
{
    if (dev->drv->bus_type != MO_BUS_SCSI)
        ide_set_callback(ide_drives[dev->drv->ide_channel], dev->callback);
}

static void
mo_init(mo_t *dev)
{
    if (dev->id < MO_NUM) {
        dev->requested_blocks = 1;
        dev->sense[0]         = 0xf0;
        dev->sense[7]         = 10;
        dev->drv->bus_mode    = 0;
        if (dev->drv->bus_type >= MO_BUS_ATAPI)
            dev->drv->bus_mode |= 2;
        if (dev->drv->bus_type < MO_BUS_SCSI)
            dev->drv->bus_mode |= 1;
        mo_log(dev->log, "Bus type %i, bus mode %i\n", dev->drv->bus_type, dev->drv->bus_mode);
        if (dev->drv->bus_type < MO_BUS_SCSI) {
            dev->tf->phase          = 1;
            dev->tf->request_length = 0xEB14;
        }
        dev->tf->status    = READY_STAT | DSC_STAT;
        dev->tf->pos       = 0;
        dev->packet_status = PHASE_NONE;
        mo_sense_key = mo_asc = mo_ascq = dev->unit_attention = dev->transition = 0;
        mo_info      = 0x00000000;
    }
}

static int
mo_supports_pio(const mo_t *dev)
{
    return (dev->drv->bus_mode & 1);
}

static int
mo_supports_dma(const mo_t *dev)
{
    return (dev->drv->bus_mode & 2);
}

/* Returns: 0 for none, 1 for PIO, 2 for DMA. */
static int
mo_current_mode(const mo_t *dev)
{
    if (!mo_supports_pio(dev) && !mo_supports_dma(dev))
        return 0;
    if (mo_supports_pio(dev) && !mo_supports_dma(dev)) {
        mo_log(dev->log, "Drive does not support DMA, setting to PIO\n");
        return 1;
    }
    if (!mo_supports_pio(dev) && mo_supports_dma(dev))
        return 2;
    if (mo_supports_pio(dev) && mo_supports_dma(dev)) {
        mo_log(dev->log, "Drive supports both, setting to %s\n", (dev->tf->features & 1) ?
               "DMA" : "PIO");
        return (dev->tf->features & 1) ? 2 : 1;
    }

    return 0;
}

static void
mo_mode_sense_load(mo_t *dev)
{
    char  fn[512] = { 0 };

    memset(&dev->ms_pages_saved, 0, sizeof(mode_sense_pages_t));
    if (mo_drives[dev->id].bus_type == MO_BUS_SCSI)
        memcpy(&dev->ms_pages_saved, &mo_mode_sense_pages_default_scsi,
               sizeof(mode_sense_pages_t));
    else
        memcpy(&dev->ms_pages_saved, &mo_mode_sense_pages_default,
               sizeof(mode_sense_pages_t));

    if (dev->drv->bus_type == MO_BUS_SCSI)
        sprintf(fn, "scsi_mo_%02i_mode_sense_bin", dev->id);
    else
        sprintf(fn, "mo_%02i_mode_sense_bin", dev->id);
    FILE *fp = plat_fopen(nvr_path(fn), "rb");
    if (fp) {
        /* Nothing to read, not used by MO. */
        fclose(fp);
    }
}

static void
mo_mode_sense_save(const mo_t *dev)
{
    char  fn[512] = { 0 };

    if (dev->drv->bus_type == MO_BUS_SCSI)
        sprintf(fn, "scsi_mo_%02i_mode_sense_bin", dev->id);
    else
        sprintf(fn, "mo_%02i_mode_sense_bin", dev->id);
    FILE *fp = plat_fopen(nvr_path(fn), "wb");
    if (fp) {
        /* Nothing to write, not used by MO. */
        fclose(fp);
    }
}

/* SCSI Mode Sense 6/10. */
static uint8_t
mo_mode_sense_read(const mo_t *dev, const uint8_t pgctl,
                   const uint8_t page, const uint8_t pos)
{
    switch (pgctl) {
        case 0:
        case 3:
            return dev->ms_pages_saved.pages[page][pos];
        case 1:
            return mo_mode_sense_pages_changeable.pages[page][pos];
        case 2:
            if (dev->drv->bus_type == MO_BUS_SCSI)
                return mo_mode_sense_pages_default_scsi.pages[page][pos];
            else
                return mo_mode_sense_pages_default.pages[page][pos];

        default:
            break;
    }

    return 0;
}

static uint32_t
mo_mode_sense(const mo_t *dev, uint8_t *buf, uint32_t pos,
              uint8_t page, const uint8_t block_descriptor_len)
{
    const uint64_t pf    = mo_mode_sense_page_flags;
    const uint8_t  pgctl = (page >> 6) & 3;

    page &= 0x3f;

    if (block_descriptor_len) {
        buf[pos++] = ((dev->drv->medium_size >> 24) & 0xff);
        buf[pos++] = ((dev->drv->medium_size >> 16) & 0xff);
        buf[pos++] = ((dev->drv->medium_size >> 8) & 0xff);
        buf[pos++] = (dev->drv->medium_size & 0xff);
        buf[pos++] = 0; /* Reserved. */
        buf[pos++] = 0;
        buf[pos++] = ((dev->drv->sector_size >> 8) & 0xff);
        buf[pos++] = (dev->drv->sector_size & 0xff);
    }

    for (uint8_t i = 0; i < 0x40; i++) {
        if ((page == GPMODE_ALL_PAGES) || (page == i)) {
            if (pf & (1LL << ((uint64_t) page))) {
                const uint8_t msplen = mo_mode_sense_read(dev, pgctl, i, 1);
                buf[pos++]           = mo_mode_sense_read(dev, pgctl, i, 0);
                buf[pos++]           = msplen;
                mo_log(dev->log, "MODE SENSE: Page [%02X] length %i\n", i, msplen);
                for (uint8_t j = 0; j < msplen; j++)
                    buf[pos++] = mo_mode_sense_read(dev, pgctl, i, 2 + j);
            }
        }
    }

    return pos;
}

static void
mo_update_request_length(mo_t *dev, int len, int block_len)
{
    int bt;
    int min_len = 0;

    dev->max_transfer_len = dev->tf->request_length;

    /*
       For media access commands, make sure the requested DRQ length
       matches the block length.
     */
    switch (dev->current_cdb[0]) {
        case 0x08:
        case 0x0a:
        case 0x28:
        case 0x2a:
        case 0xa8:
        case 0xaa:
            /* Round it to the nearest 2048 bytes. */
            dev->max_transfer_len = (dev->max_transfer_len >> 9) << 9;

            /*
               Make sure total length is not bigger than sum of the lengths of
               all the requested blocks.
             */
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
    /*
       If the DRQ length is smaller or equal in size to the total remaining length,
       set it to that.
     */
    if (!dev->max_transfer_len)
        dev->max_transfer_len = 65534;

    if ((len <= dev->max_transfer_len) && (len >= min_len))
        dev->tf->request_length = dev->max_transfer_len = len;
    else if (len > dev->max_transfer_len)
        dev->tf->request_length = dev->max_transfer_len;
}

static double
mo_bus_speed(mo_t *dev)
{
    double ret = -1.0;

    if (dev && dev->drv && (dev->drv->bus_type == MO_BUS_SCSI)) {
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

static void
mo_command_common(mo_t *dev)
{
    dev->tf->status = BUSY_STAT;
    dev->tf->phase  = 1;
    dev->tf->pos    = 0;
    if (dev->packet_status == PHASE_COMPLETE)
        dev->callback = 0.0;
    else {
        double bytes_per_second;

        if (dev->drv->bus_type == MO_BUS_SCSI) {
            dev->callback = -1.0; /* Speed depends on SCSI controller */
            return;
        } else
            bytes_per_second = mo_bus_speed(dev);

        const double period = 1000000.0 / bytes_per_second;
        dev->callback       = period * (double) (dev->packet_len);
    }

    mo_set_callback(dev);
}

static void
mo_command_complete(mo_t *dev)
{
    dev->packet_status = PHASE_COMPLETE;
    mo_command_common(dev);
}

static void
mo_command_read(mo_t *dev)
{
    dev->packet_status = PHASE_DATA_IN;
    mo_command_common(dev);
}

static void
mo_command_read_dma(mo_t *dev)
{
    dev->packet_status = PHASE_DATA_IN_DMA;
    mo_command_common(dev);
}

static void
mo_command_write(mo_t *dev)
{
    dev->packet_status = PHASE_DATA_OUT;
    mo_command_common(dev);
}

static void
mo_command_write_dma(mo_t *dev)
{
    dev->packet_status = PHASE_DATA_OUT_DMA;
    mo_command_common(dev);
}

/*
   dev = Pointer to current MO device;
   len = Total transfer length;
   block_len = Length of a single block (why does it matter?!);
   alloc_len = Allocated transfer length;
   direction = Transfer direction (0 = read from host, 1 = write to host).
 */
static void
mo_data_command_finish(mo_t *dev, int len, const int block_len,
                       const int alloc_len, const int direction)
{
    mo_log(dev->log, "Finishing command (%02X): %i, %i, %i, %i, %i\n",
           dev->current_cdb[0], len, block_len, alloc_len,
           direction, dev->tf->request_length);
    dev->tf->pos = 0;
    if (alloc_len >= 0) {
        if (alloc_len < len)
            len = alloc_len;
    }
    if ((len == 0) || (mo_current_mode(dev) == 0)) {
        if (dev->drv->bus_type != MO_BUS_SCSI)
            dev->packet_len = 0;

        mo_command_complete(dev);
    } else {
        if (mo_current_mode(dev) == 2) {
            if (dev->drv->bus_type != MO_BUS_SCSI)
                dev->packet_len = alloc_len;

            if (direction == 0)
                mo_command_read_dma(dev);
            else
                mo_command_write_dma(dev);
        } else {
            mo_update_request_length(dev, len, block_len);
            if (direction == 0)
                mo_command_read(dev);
            else
                mo_command_write(dev);
        }
    }

    mo_log(dev->log, "Status: %i, cylinder %i, packet length: %i, position: %i, phase: %i\n",
           dev->packet_status, dev->tf->request_length, dev->packet_len,
           dev->tf->pos, dev->tf->phase);
}

static void
mo_sense_clear(mo_t *dev, UNUSED(int command))
{
    mo_sense_key = mo_asc = mo_ascq = 0;
    mo_info      = 0x00000000;
}

static void
mo_set_phase(const mo_t *dev, const uint8_t phase)
{
    const uint8_t scsi_bus = (dev->drv->scsi_device_id >> 4) & 0x0f;
    const uint8_t scsi_id  = dev->drv->scsi_device_id & 0x0f;

    if (dev->drv->bus_type == MO_BUS_SCSI)
        scsi_devices[scsi_bus][scsi_id].phase = phase;
}

static void
mo_cmd_error(mo_t *dev)
{
    mo_set_phase(dev, SCSI_PHASE_STATUS);
    dev->tf->error = ((mo_sense_key & 0xf) << 4) | ABRT_ERR;
    dev->tf->status        = READY_STAT | ERR_STAT;
    dev->tf->phase         = 3;
    dev->tf->pos           = 0;
    dev->packet_status     = PHASE_ERROR;
    dev->callback          = 50.0 * MO_TIME;
    mo_set_callback(dev);
    ui_sb_update_icon(SB_MO | dev->id, 0);
    mo_log(dev->log, "[%02X] ERROR: %02X/%02X/%02X\n", dev->current_cdb[0], mo_sense_key,
           mo_asc, mo_ascq);
}

static void
mo_unit_attention(mo_t *dev)
{
    mo_set_phase(dev, SCSI_PHASE_STATUS);
    dev->tf->error     = (SENSE_UNIT_ATTENTION << 4) | ABRT_ERR;
    dev->tf->status    = READY_STAT | ERR_STAT;
    dev->tf->phase     = 3;
    dev->tf->pos       = 0;
    dev->packet_status = PHASE_ERROR;
    dev->callback      = 50.0 * MO_TIME;
    mo_set_callback(dev);
    ui_sb_update_icon(SB_MO | dev->id, 0);
    mo_log(dev->log, "UNIT ATTENTION\n");
}

static void
mo_buf_alloc(mo_t *dev, uint32_t len)
{
    mo_log(dev->log, "Allocated buffer length: %i\n", len);
    if (dev->buffer == NULL)
        dev->buffer = (uint8_t *) malloc(len);
}

static void
mo_buf_free(mo_t *dev)
{
    if (dev->buffer) {
        mo_log(dev->log, "Freeing buffer...\n");
        free(dev->buffer);
        dev->buffer = NULL;
    }
}

static void
mo_bus_master_error(scsi_common_t *sc)
{
    mo_t *dev = (mo_t *) sc;

    mo_buf_free(dev);
    mo_sense_key = mo_asc = mo_ascq = 0;
    mo_info      =  (dev->sector_pos >> 24)        |
                   ((dev->sector_pos >> 16) <<  8) |
                   ((dev->sector_pos >> 8)  << 16) |
                   ( dev->sector_pos        << 24);
    mo_cmd_error(dev);
}

static void
mo_not_ready(mo_t *dev)
{
    mo_sense_key = SENSE_NOT_READY;
    mo_asc       = ASC_MEDIUM_NOT_PRESENT;
    mo_ascq      = 0;
    mo_info      = 0x00000000;
    mo_cmd_error(dev);
}

static void
mo_write_protected(mo_t *dev)
{
    mo_sense_key = SENSE_UNIT_ATTENTION;
    mo_asc       = ASC_WRITE_PROTECTED;
    mo_ascq      = 0;
    mo_info      =  (dev->sector_pos >> 24)        |
                   ((dev->sector_pos >> 16) <<  8) |
                   ((dev->sector_pos >> 8)  << 16) |
                   ( dev->sector_pos        << 24);
    mo_cmd_error(dev);
}

static void
mo_write_error(mo_t *dev)
{
    mo_sense_key = SENSE_MEDIUM_ERROR;
    mo_asc       = ASC_WRITE_ERROR;
    mo_ascq      = 0;
    mo_info      =  (dev->sector_pos >> 24)        |
                   ((dev->sector_pos >> 16) <<  8) |
                   ((dev->sector_pos >> 8)  << 16) |
                   ( dev->sector_pos        << 24);
    mo_cmd_error(dev);
}

static void
mo_read_error(mo_t *dev)
{
    mo_sense_key = SENSE_MEDIUM_ERROR;
    mo_asc       = ASC_UNRECOVERED_READ_ERROR;
    mo_ascq      = 0;
    mo_info      =  (dev->sector_pos >> 24)        |
                   ((dev->sector_pos >> 16) <<  8) |
                   ((dev->sector_pos >> 8)  << 16) |
                   ( dev->sector_pos        << 24);
    mo_cmd_error(dev);
}

static void
mo_invalid_lun(mo_t *dev, const uint8_t lun)
{
    mo_sense_key = SENSE_ILLEGAL_REQUEST;
    mo_asc       = ASC_INV_LUN;
    mo_ascq      = 0;
    mo_info      = lun << 24;
    mo_cmd_error(dev);
}

static void
mo_illegal_opcode(mo_t *dev, const uint8_t opcode)
{
    mo_sense_key = SENSE_ILLEGAL_REQUEST;
    mo_asc       = ASC_ILLEGAL_OPCODE;
    mo_ascq      = 0;
    mo_info      = opcode << 24;
    mo_cmd_error(dev);
}

static void
mo_lba_out_of_range(mo_t *dev)
{
    mo_sense_key = SENSE_ILLEGAL_REQUEST;
    mo_asc       = ASC_LBA_OUT_OF_RANGE;
    mo_ascq      = 0;
    mo_info      =  (dev->sector_pos >> 24)        |
                   ((dev->sector_pos >> 16) <<  8) |
                   ((dev->sector_pos >> 8)  << 16) |
                   ( dev->sector_pos        << 24);
    mo_cmd_error(dev);
}

static void
mo_invalid_field(mo_t *dev, const uint32_t field)
{
    mo_sense_key = SENSE_ILLEGAL_REQUEST;
    mo_asc       = ASC_INV_FIELD_IN_CMD_PACKET;
    mo_ascq      = 0;
    mo_info      =  (field >> 24)        |
                   ((field >> 16) <<  8) |
                   ((field >> 8)  << 16) |
                   ( field        << 24);
    mo_cmd_error(dev);
    dev->tf->status = 0x53;
}

static void
mo_invalid_field_pl(mo_t *dev, const uint32_t field)
{
    mo_sense_key = SENSE_ILLEGAL_REQUEST;
    mo_asc       = ASC_INV_FIELD_IN_PARAMETER_LIST;
    mo_ascq      = 0;
    mo_info      =  (field >> 24)        |
                   ((field >> 16) <<  8) |
                   ((field >> 8)  << 16) |
                   ( field        << 24);
    mo_cmd_error(dev);
    dev->tf->status = 0x53;
}

static int
mo_blocks(mo_t *dev, int32_t *len, int out)
{
    int ret = 0;

    *len    = 0;

    if (!dev->sector_len)
        mo_command_complete(dev);
    else {
        mo_log(dev->log, "%sing %i blocks starting from %i...\n", out ? "Writ" : "Read",
               dev->requested_blocks, dev->sector_pos);

        if (dev->sector_pos >= dev->drv->medium_size) {
            mo_log(dev->log, "Trying to %s beyond the end of disk\n", out ? "write" : "read");
            mo_lba_out_of_range(dev);
        } else {
            *len = dev->requested_blocks * dev->drv->sector_size;
            ret  = 1;

            for (int i = 0; i < dev->requested_blocks; i++) {
                if (fseek(dev->drv->fp, dev->drv->base + (dev->sector_pos * dev->drv->sector_size) + (i * dev->drv->sector_size), SEEK_SET) == -1) {
                    if (out)
                        mo_write_error(dev);
                    else
                        mo_read_error(dev);

                    ret = -1;
                } else {
                    if (!feof(dev->drv->fp))
                        break;

                    if (out) {
                        if (fwrite(dev->buffer + (i * dev->drv->sector_size), 1,
                                  dev->drv->sector_size, dev->drv->fp) != dev->drv->sector_size) {
                            mo_log(dev->log, "mo_blocks(): Error writing data\n");
                            mo_write_error(dev);
                            ret = -1;
                        } else
                            fflush(dev->drv->fp);
                    } else {
                        if (fread(dev->buffer + (i * dev->drv->sector_size), 1,
                                  dev->drv->sector_size, dev->drv->fp) != dev->drv->sector_size) {
                            mo_log(dev->log, "mo_blocks(): Error reading data\n");
                            mo_read_error(dev);
                            ret = -1;
                        }
                    }
                }

                if (ret == -1)
                    break;

                dev->sector_pos++;
            }

            if (ret == 1) {
                mo_log(dev->log, "%s %i bytes of blocks...\n", out ? "Written" : "Read", *len);

                dev->sector_len -= dev->requested_blocks;
            }
        }
    }

    return ret;
}

void
mo_insert(mo_t *dev)
{
    if ((dev != NULL) && (dev->drv != NULL)) {
        if (dev->drv->fp == NULL) {
            dev->unit_attention = 0;
            dev->transition     = 0;
            mo_log(dev->log, "Media removal\n");
        } else if (dev->transition) {
            dev->unit_attention = 1;
            /* Turn off the medium changed status. */
            dev->transition     = 0;
            mo_log(dev->log, "Media insert\n");
        } else {
            dev->unit_attention = 0;
            dev->transition     = 1;
            mo_log(dev->log, "Media transition\n");
        }
    }
}

void
mo_format(mo_t *dev)
{
    int  ret;
    int  fd;

    mo_log(dev->log, "Formatting media...\n");

    fseek(dev->drv->fp, 0, SEEK_END);
    long size = ftell(dev->drv->fp);

#ifdef _WIN32
    LARGE_INTEGER liSize;

    fd              = _fileno(dev->drv->fp);
    const HANDLE fh = (HANDLE) _get_osfhandle(fd);

    liSize.QuadPart = 0;

    ret = (int) SetFilePointerEx(fh, liSize, NULL, FILE_BEGIN);

    if (ret) {
        ret = (int) SetEndOfFile(fh);

        if (ret) {
            liSize.QuadPart = size;
            ret             = (int) SetFilePointerEx(fh, liSize, NULL, FILE_BEGIN);

            if (ret) {
                ret = (int) SetEndOfFile(fh);

                if (!ret) {
                    mo_log(dev->log, "Failed to truncate image file to %llu\n", size);
                }
            } else {
                mo_log(dev->log, "Failed seek to end of image file\n");
            }
        } else {
            mo_log(dev->log, "Failed to truncate image file to 0\n");
        }
    } else {
        mo_log(dev->log, "Failed seek to start of image file\n");
    }
#else
    fd = fileno(dev->drv->fp);

    ret = ftruncate(fd, 0);

    if (ret) {
        mo_log(dev->log, "Failed to truncate image file to 0\n");
    } else {
        ret = ftruncate(fd, size);

        if (ret) {
            mo_log(dev->log, "Failed to truncate image file to %llu", size);
        }
    }
#endif
}

static int
mo_erase(mo_t *dev)
{
    int i;

    if (!dev->sector_len) {
        mo_command_complete(dev);
        return -1;
    }

    mo_log(dev->log, "Erasing %i blocks starting from %i...\n",
           dev->sector_len, dev->sector_pos);

    if (dev->sector_pos >= dev->drv->medium_size) {
        mo_log(dev->log, "Trying to erase beyond the end of disk\n");
        mo_lba_out_of_range(dev);
        return 0;
    }

    mo_buf_alloc(dev, dev->drv->sector_size);
    memset(dev->buffer, 0, dev->drv->sector_size);

    fseek(dev->drv->fp, dev->drv->base + (dev->sector_pos * dev->drv->sector_size),
          SEEK_SET);

    for (i = 0; i < dev->requested_blocks; i++) {
        if (feof(dev->drv->fp))
            break;

        fwrite(dev->buffer, 1, dev->drv->sector_size, dev->drv->fp);
    }

    fflush(dev->drv->fp);

    mo_log(dev->log, "Erased %i bytes of blocks...\n", i * dev->drv->sector_size);

    dev->sector_pos += i;
    dev->sector_len -= i;

    return 1;
}

static int
mo_pre_execution_check(mo_t *dev, const uint8_t *cdb)
{
    int ready;

    if ((cdb[0] != GPCMD_REQUEST_SENSE) && (dev->cur_lun == SCSI_LUN_USE_CDB) &&
        (cdb[1] & 0xe0)) {
        mo_log(dev->log, "Attempting to execute a unknown command targeted at SCSI LUN %i\n",
               ((dev->tf->request_length >> 5) & 7));
        mo_invalid_lun(dev, cdb[1] >> 5);
        return 0;
    }

    if (!(mo_command_flags[cdb[0]] & IMPLEMENTED)) {
        mo_log(dev->log, "Attempting to execute unknown command %02X over %s\n",
               cdb[0], (dev->drv->bus_type == MO_BUS_SCSI) ?
               "SCSI" : "ATAPI");

        mo_illegal_opcode(dev, cdb[0]);
        return 0;
    }

    if ((dev->drv->bus_type < MO_BUS_SCSI) &&
        (mo_command_flags[cdb[0]] & SCSI_ONLY)) {
        mo_log(dev->log, "Attempting to execute SCSI-only command %02X "
               "over ATAPI\n", cdb[0]);
        mo_illegal_opcode(dev, cdb[0]);
        return 0;
    }

    if ((dev->drv->bus_type == MO_BUS_SCSI) &&
        (mo_command_flags[cdb[0]] & ATAPI_ONLY)) {
        mo_log(dev->log, "Attempting to execute ATAPI-only command %02X "
               "over SCSI\n", cdb[0]);
        mo_illegal_opcode(dev, cdb[0]);
        return 0;
    }

    if (dev->transition) {
        if ((cdb[0] == GPCMD_TEST_UNIT_READY) || (cdb[0] == GPCMD_REQUEST_SENSE))
            ready = 0;
        else {
            if (!(mo_command_flags[cdb[0]] & ALLOW_UA)) {
                mo_log(dev->log, "(ext_medium_changed != 0): mo_insert()\n");
                mo_insert((void *) dev);
            }

            ready = (dev->drv->fp != NULL);
        }
    } else
        ready = (dev->drv->fp != NULL);

    /*
       If the drive is not ready, there is no reason to keep the
       UNIT ATTENTION condition present, as we only use it to mark
       disc changes.
     */
    if (!ready && (dev->unit_attention > 0))
        dev->unit_attention = 0;

    /*
       If the UNIT ATTENTION condition is set and the command does not allow
       execution under it, error out and report the condition.
     */
    if (dev->unit_attention == 1) {
        /*
           Only increment the unit attention phase if the command can
           not pass through it.
         */
        if (!(mo_command_flags[cdb[0]] & ALLOW_UA)) {
            mo_log(dev->log, "Unit attention now 2\n");
            dev->unit_attention++;
            mo_log(dev->log, "UNIT ATTENTION: Command %02X not allowed to "
                   "pass through\n", cdb[0]);
            mo_unit_attention(dev);
            return 0;
        }
    } else if (dev->unit_attention == 2) {
        if (cdb[0] != GPCMD_REQUEST_SENSE) {
            mo_log(dev->log, "MO %i: Unit attention now 0\n");
            dev->unit_attention = 0;
        }
    }

    /*
       Unless the command is REQUEST SENSE, clear the sense. This will *NOT* clear
       the UNIT ATTENTION condition if it's set.
     */
    if (cdb[0] != GPCMD_REQUEST_SENSE)
        mo_sense_clear(dev, cdb[0]);

    if (!ready && (mo_command_flags[cdb[0]] & CHECK_READY)) {
        mo_log(dev->log, "Not ready (%02X)\n", cdb[0]);
        mo_not_ready(dev);
        return 0;
    }

    mo_log(dev->log, "Continuing with command %02X\n", cdb[0]);
    return 1;
}

static void
mo_seek(mo_t *dev, uint32_t pos)
{
    dev->sector_pos = pos;
}

static void
mo_rezero(mo_t *dev)
{
    dev->sector_pos = dev->sector_len = 0;
    mo_seek(dev, 0);
}

void
mo_reset(scsi_common_t *sc)
{
    mo_t *dev = (mo_t *) sc;

    mo_rezero(dev);
    dev->tf->status         = 0;
    dev->callback           = 0.0;
    mo_set_callback(dev);
    dev->tf->phase          = 1;
    dev->tf->request_length = 0xeb14;
    dev->packet_status      = PHASE_NONE;
    dev->cur_lun            = SCSI_LUN_USE_CDB;
    mo_sense_key = mo_asc = mo_ascq = dev->unit_attention = dev->transition = 0;
    mo_info      = 0x00000000;
}

static void
mo_request_sense(mo_t *dev, uint8_t *buffer, const uint8_t alloc_length, const int desc)
{
    /* Will return 18 bytes of 0. */
    if (alloc_length != 0) {
        memset(buffer, 0x00, alloc_length);
        if (desc) {
            buffer[1] = mo_sense_key;
            buffer[2] = mo_asc;
            buffer[3] = mo_ascq;
        } else
            memcpy(buffer, dev->sense, alloc_length);
    }

    buffer[0] = desc ? 0x72 : 0xf0;
    if (!desc)
        buffer[7] = 10;

    if (dev->unit_attention && (mo_sense_key == 0)) {
        buffer[desc ? 1 : 2]  = SENSE_UNIT_ATTENTION;
        buffer[desc ? 2 : 12] = ASC_MEDIUM_MAY_HAVE_CHANGED;
        buffer[desc ? 3 : 13] = 0;
    }

    mo_log(dev->log, "Reporting sense: %02X %02X %02X\n", buffer[2], buffer[12], buffer[13]);

    if (buffer[desc ? 1 : 2] == SENSE_UNIT_ATTENTION) {
        /* If the last remaining sense is unit attention, clear that condition. */
        dev->unit_attention = 0;
    }

    /* Clear the sense stuff as per the spec. */
    mo_sense_clear(dev, GPCMD_REQUEST_SENSE);

    if (dev->transition) {
        mo_log(dev->log, "MO_TRANSITION: mo_insert()\n");
        mo_insert((void *) dev);
    }
}

static void
mo_request_sense_for_scsi(scsi_common_t *sc, uint8_t *buffer, uint8_t alloc_length)
{
    mo_t      *dev   = (mo_t *) sc;
    const int  ready = (dev->drv->fp != NULL);

    if (!ready && dev->unit_attention) {
        /* If the drive is not ready, there is no reason to keep the
           UNIT ATTENTION condition present, as we only use it to mark
           disc changes. */
        dev->unit_attention = 0;
    }

    /* Do *NOT* advance the unit attention phase. */
    mo_request_sense(dev, buffer, alloc_length, 0);
}

static void
mo_set_buf_len(const mo_t *dev, int32_t *BufLen, int32_t *src_len)
{
    if (dev->drv->bus_type == MO_BUS_SCSI) {
        if (*BufLen == -1)
            *BufLen = *src_len;
        else {
            *BufLen  = MIN(*src_len, *BufLen);
            *src_len = *BufLen;
        }
        mo_log(dev->log, "Actual transfer length: %i\n", *BufLen);
    }
}

static void
mo_command(scsi_common_t *sc, const uint8_t *cdb)
{
    mo_t *        dev                = (mo_t *) sc;
    char          device_identify[9] = { '8', '6', 'B', '_', 'M', 'O', '0', '0', 0 };
    uint32_t      previous_pos       = 0;
    int32_t       blen               = 0;
    const uint8_t scsi_bus           = (dev->drv->scsi_device_id >> 4) & 0x0f;
    const uint8_t scsi_id            = dev->drv->scsi_device_id & 0x0f;
    int           pos                = 0;
    int           idx                = 0;
    int32_t       len;
    int32_t       max_len;
    int32_t       alloc_length;
    unsigned      preamble_len;
    int           block_desc;
    int           size_idx;
    int32_t *     BufLen;

    if (dev->drv->bus_type == MO_BUS_SCSI) {
        BufLen          = &scsi_devices[scsi_bus][scsi_id].buffer_length;
        dev->tf->status &= ~ERR_STAT;
    } else {
        BufLen         = &blen;
        dev->tf->error = 0;
    }

    dev->packet_len  = 0;
    dev->request_pos = 0;

    device_identify[7] = dev->id + 0x30;

    memcpy(dev->current_cdb, cdb, 12);

    if (cdb[0] != 0) {
        mo_log(dev->log, "Command 0x%02X, Sense Key %02X, Asc %02X, Ascq %02X, "
               "Unit attention: %i\n",
               cdb[0], mo_sense_key, mo_asc, mo_ascq, dev->unit_attention);
        mo_log(dev->log, "Request length: %04X\n", dev->tf->request_length);

        mo_log(dev->log, "CDB: %02X %02X %02X %02X %02X %02X %02X %02X "
               "%02X %02X %02X %02X\n",
               cdb[0], cdb[1], cdb[2], cdb[3], cdb[4], cdb[5], cdb[6], cdb[7],
               cdb[8], cdb[9], cdb[10], cdb[11]);
    }

    dev->sector_len = 0;

    mo_set_phase(dev, SCSI_PHASE_STATUS);

    /*
       This handles the Not Ready/Unit Attention check if it has to be
       handled at this point.
     */
    if (mo_pre_execution_check(dev, cdb) == 0)
        return;

    switch (cdb[0]) {
        case GPCMD_SEND_DIAGNOSTIC:
            if (!(cdb[1] & (1 << 2))) {
                mo_invalid_field(dev, cdb[1]);
                return;
            }
            fallthrough;
        case GPCMD_SCSI_RESERVE:
        case GPCMD_SCSI_RELEASE:
        case GPCMD_TEST_UNIT_READY:
            mo_set_phase(dev, SCSI_PHASE_STATUS);
            mo_command_complete(dev);
            break;

        case GPCMD_FORMAT_UNIT:
            if (dev->drv->read_only) {
                mo_write_protected(dev);
                return;
            }

            mo_format(dev);
            mo_set_phase(dev, SCSI_PHASE_STATUS);
            mo_command_complete(dev);
            break;

        case GPCMD_REZERO_UNIT:
            dev->sector_pos = dev->sector_len = 0;
            mo_seek(dev, 0);
            mo_set_phase(dev, SCSI_PHASE_STATUS);
            break;

        case GPCMD_REQUEST_SENSE:
            mo_set_phase(dev, SCSI_PHASE_DATA_IN);
            max_len = cdb[4];

            if (!max_len) {
                mo_set_phase(dev, SCSI_PHASE_STATUS);
                dev->packet_status = PHASE_COMPLETE;
                dev->callback      = 20.0 * MO_TIME;
                mo_set_callback(dev);
                break;
            }

            mo_buf_alloc(dev, 256);
            mo_set_buf_len(dev, BufLen, &max_len);
            len = (cdb[1] & 1) ? 8 : 18;
            mo_request_sense(dev, dev->buffer, max_len, cdb[1] & 1);
            mo_data_command_finish(dev, len, len, cdb[4], 0);
            break;

        case GPCMD_MECHANISM_STATUS:
            mo_set_phase(dev, SCSI_PHASE_DATA_IN);
            len = (cdb[8] << 8) | cdb[9];

            mo_buf_alloc(dev, 8);
            mo_set_buf_len(dev, BufLen, &len);

            memset(dev->buffer, 0, 8);
            dev->buffer[5] = 1;

            mo_data_command_finish(dev, 8, 8, len, 0);
            break;

        case GPCMD_READ_6:
        case GPCMD_READ_10:
        case GPCMD_READ_12:
            mo_set_phase(dev, SCSI_PHASE_DATA_IN);
            alloc_length = dev->drv->sector_size;

            switch (cdb[0]) {
                case GPCMD_READ_6:
                    dev->sector_len = cdb[4];
                    dev->sector_pos = ((((uint32_t) cdb[1]) & 0x1f) << 16) |
                                      (((uint32_t) cdb[2]) << 8) | ((uint32_t) cdb[3]);
                    if (dev->sector_len == 0)
                        dev->sector_len = 256;
                    mo_log(dev->log, "Length: %i, LBA: %i\n", dev->sector_len, dev->sector_pos);
                    break;
                case GPCMD_READ_10:
                    dev->sector_len = (cdb[7] << 8) | cdb[8];
                    dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) |
                                      (cdb[4] << 8) | cdb[5];
                    mo_log(dev->log, "Length: %i, LBA: %i\n", dev->sector_len, dev->sector_pos);
                    break;
                case GPCMD_READ_12:
                    dev->sector_len = (((uint32_t) cdb[6]) << 24) |
                                      (((uint32_t) cdb[7]) << 16) |
                                      (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);
                    dev->sector_pos = (((uint32_t) cdb[2]) << 24) |
                                      (((uint32_t) cdb[3]) << 16) |
                                      (((uint32_t) cdb[4]) << 8) | ((uint32_t) cdb[5]);
                    mo_log(dev->log, "Length: %i, LBA: %i\n", dev->sector_len, dev->sector_pos);
                    break;

                default:
                    break;
            }

            if (dev->sector_len) {
                max_len               = dev->sector_len;
                dev->requested_blocks = max_len;

                dev->packet_len = max_len * alloc_length;
                mo_buf_alloc(dev, dev->packet_len);

                const int ret = mo_blocks(dev, &alloc_length, 0);

                if (ret > 0) {
                    dev->requested_blocks = max_len;
                    dev->packet_len       = alloc_length;

                    mo_set_buf_len(dev, BufLen, (int32_t *) &dev->packet_len);

                    mo_data_command_finish(dev, alloc_length, dev->drv->sector_size, alloc_length, 0);

                    if (dev->packet_status != PHASE_COMPLETE)
                        ui_sb_update_icon(SB_MO | dev->id, 1);
                    else
                        ui_sb_update_icon(SB_MO | dev->id, 0);
                } else {
                    mo_set_phase(dev, SCSI_PHASE_STATUS);
                    dev->packet_status = (ret < 0) ? PHASE_ERROR : PHASE_COMPLETE;
                    dev->callback      = 20.0 * MO_TIME;
                    mo_set_callback(dev);
                    mo_buf_free(dev);
                }
            } else {
                mo_set_phase(dev, SCSI_PHASE_STATUS);
                /* mo_log(dev->log, "All done - callback set\n"); */
                dev->packet_status = PHASE_COMPLETE;
                dev->callback      = 20.0 * MO_TIME;
                mo_set_callback(dev);
            }
            break;

        case GPCMD_VERIFY_6:
        case GPCMD_VERIFY_10:
        case GPCMD_VERIFY_12:
            if (!(cdb[1] & 2)) {
                mo_set_phase(dev, SCSI_PHASE_STATUS);
                mo_command_complete(dev);
                break;
            }
            fallthrough;
        case GPCMD_WRITE_6:
        case GPCMD_WRITE_10:
        case GPCMD_WRITE_AND_VERIFY_10:
        case GPCMD_WRITE_12:
        case GPCMD_WRITE_AND_VERIFY_12:
            mo_set_phase(dev, SCSI_PHASE_DATA_OUT);
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
                    mo_log(dev->log, "Length: %i, LBA: %i\n", dev->sector_len, dev->sector_pos);
                    break;
                case GPCMD_VERIFY_10:
                case GPCMD_WRITE_10:
                case GPCMD_WRITE_AND_VERIFY_10:
                    dev->sector_len = (cdb[7] << 8) | cdb[8];
                    dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) |
                                      (cdb[4] << 8) | cdb[5];
                    mo_log(dev->log, "Length: %i, LBA: %i\n", dev->sector_len, dev->sector_pos);
                    break;
                case GPCMD_VERIFY_12:
                case GPCMD_WRITE_12:
                case GPCMD_WRITE_AND_VERIFY_12:
                    dev->sector_len = (((uint32_t) cdb[6]) << 24) |
                                      (((uint32_t) cdb[7]) << 16) |
                                      (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);
                    dev->sector_pos = (((uint32_t) cdb[2]) << 24) |
                                      (((uint32_t) cdb[3]) << 16) |
                                      (((uint32_t) cdb[4]) << 8) | ((uint32_t) cdb[5]);
                    break;

                default:
                    break;
            }

            if (dev->sector_pos > (mo_types[dev->drv->type].sectors - 1))
                mo_lba_out_of_range(dev);
            else {
                if (dev->sector_len) {
                    max_len               = dev->sector_len;
                    dev->requested_blocks = max_len;

                    dev->packet_len = max_len * alloc_length;
                    mo_buf_alloc(dev, dev->packet_len);

                    dev->requested_blocks = max_len;
                    dev->packet_len       = max_len << 9;

                    mo_set_buf_len(dev, BufLen, (int32_t *) &dev->packet_len);

                    mo_data_command_finish(dev, dev->packet_len, 512,
                                           dev->packet_len, 1);

                    ui_sb_update_icon(SB_MO | dev->id,
                                      dev->packet_status != PHASE_COMPLETE);
                } else {
                    mo_set_phase(dev, SCSI_PHASE_STATUS);
                    mo_log(dev->log, "All done - callback set\n");
                    dev->packet_status = PHASE_COMPLETE;
                    dev->callback      = 20.0 * SCSI_TIME;
                    mo_set_callback(dev);
                }
            }
            break;

        case GPCMD_WRITE_SAME_10:
            alloc_length = 512;

            if ((cdb[1] & 6) == 6)
                mo_invalid_field(dev, cdb[1]);
            else {
                dev->sector_len = (cdb[7] << 8) | cdb[8];
                dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];

                if (dev->sector_pos > (mo_types[dev->drv->type].sectors - 1))
                    mo_lba_out_of_range(dev);
                else if (dev->sector_len) {
                    mo_buf_alloc(dev, alloc_length);
                    mo_set_buf_len(dev, BufLen, (int32_t *) &dev->packet_len);

                    dev->requested_blocks = 1;
                    dev->packet_len = alloc_length;

                    mo_set_phase(dev, SCSI_PHASE_DATA_OUT);

                    mo_data_command_finish(dev, 512, 512,
                                           alloc_length, 1);

                    ui_sb_update_icon(SB_MO | dev->id,
                                      dev->packet_status != PHASE_COMPLETE);
                } else {
                    mo_set_phase(dev, SCSI_PHASE_STATUS);
                    mo_log(dev->log, "All done - callback set\n");
                    dev->packet_status = PHASE_COMPLETE;
                    dev->callback      = 20.0 * SCSI_TIME;
                    mo_set_callback(dev);
                }
            }
            break;

        case GPCMD_MODE_SENSE_6:
        case GPCMD_MODE_SENSE_10:
            mo_set_phase(dev, SCSI_PHASE_DATA_IN);

            if (dev->drv->bus_type == MO_BUS_SCSI)
                block_desc = ((cdb[1] >> 3) & 1) ? 0 : 1;
            else
                block_desc = 0;

            if (cdb[0] == GPCMD_MODE_SENSE_6) {
                len = cdb[4];
                mo_buf_alloc(dev, 256);
            } else {
                len = (cdb[8] | (cdb[7] << 8));
                mo_buf_alloc(dev, 65536);
            }

            if (!(mo_mode_sense_page_flags & (1LL << (uint64_t) (cdb[2] & 0x3f)))) {
                mo_invalid_field(dev, cdb[2]);
                mo_buf_free(dev);
                return;
            }

            memset(dev->buffer, 0, len);
            alloc_length = len;

            if (cdb[0] == GPCMD_MODE_SENSE_6) {
                len            = mo_mode_sense(dev, dev->buffer, 4,
                                               cdb[2], block_desc);
                len            = MIN(len, alloc_length);
                dev->buffer[0] = len - 1;
                dev->buffer[1] = 0;
                if (block_desc)
                    dev->buffer[3] = 8;
            } else {
                len            = mo_mode_sense(dev, dev->buffer, 8,
                                               cdb[2], block_desc);
                len            = MIN(len, alloc_length);
                dev->buffer[0] = (len - 2) >> 8;
                dev->buffer[1] = (len - 2) & 255;
                dev->buffer[2] = 0;
                if (block_desc) {
                    dev->buffer[6] = 0;
                    dev->buffer[7] = 8;
                }
            }

            mo_set_buf_len(dev, BufLen, &len);

            mo_log(dev->log, "Reading mode page: %02X...\n", cdb[2]);

            mo_data_command_finish(dev, len, len, alloc_length, 0);
            return;

        case GPCMD_MODE_SELECT_6:
        case GPCMD_MODE_SELECT_10:
            mo_set_phase(dev, SCSI_PHASE_DATA_OUT);

            if (cdb[0] == GPCMD_MODE_SELECT_6) {
                len = cdb[4];
                mo_buf_alloc(dev, 256);
            } else {
                len = (cdb[7] << 8) | cdb[8];
                mo_buf_alloc(dev, 65536);
            }

            mo_set_buf_len(dev, BufLen, &len);

            dev->total_length = len;
            dev->do_page_save = cdb[1] & 1;

            mo_data_command_finish(dev, len, len, len, 1);
            return;

        case GPCMD_START_STOP_UNIT:
            mo_set_phase(dev, SCSI_PHASE_STATUS);

            switch (cdb[4] & 3) {
                case 0: /* Stop the disk. */
                    break;
                case 1: /* Start the disk and read the TOC. */
                    break;
                case 2: /* Eject the disk if possible. */
                    mo_eject(dev->id);
                    break;
                case 3: /* Load the disk (close tray). */
                    mo_reload(dev->id);
                    break;

                default:
                    break;
            }

            mo_command_complete(dev);
            break;

        case GPCMD_INQUIRY:
            mo_set_phase(dev, SCSI_PHASE_DATA_IN);

            max_len = cdb[3];
            max_len <<= 8;
            max_len |= cdb[4];

            mo_buf_alloc(dev, 65536);

            if (cdb[1] & 1) {
                preamble_len = 4;
                size_idx     = 3;

                dev->buffer[idx++] = 7;         /* Optical disk */
                dev->buffer[idx++] = cdb[2];
                dev->buffer[idx++] = 0;

                idx++;

                switch (cdb[2]) {
                    case 0x00:
                        dev->buffer[idx++] = 0x00;
                        dev->buffer[idx++] = 0x80;
                        break;
                    case 0x80:    /*Unit serial number page*/
                        dev->buffer[idx++] = strlen("VCM!10") + 1;
                        /* Serial */
                        ide_padstr8(dev->buffer + idx, 20, "VCM!10");
                        idx += strlen("VCM!10");
                        break;
                    default:
                        mo_log(dev->log, "INQUIRY: Invalid page: %02X\n", cdb[2]);
                        mo_invalid_field(dev, cdb[2]);
                        mo_buf_free(dev);
                        return;
                }
            } else {
                preamble_len = 5;
                size_idx     = 4;

                memset(dev->buffer, 0, 8);
                if ((cdb[1] & 0xe0) || ((dev->cur_lun > 0x00) && (dev->cur_lun < 0xff)))
                    dev->buffer[0] = 0x7f;    /* No physical device on this LUN */
                else
                    dev->buffer[0] = 0x07;    /* Optical disk */
                dev->buffer[1] = 0x80;        /* Removable */
                /* SCSI-2 compliant */
                dev->buffer[2] = (dev->drv->bus_type == MO_BUS_SCSI) ? 0x02 : 0x00;
                dev->buffer[3] = (dev->drv->bus_type == MO_BUS_SCSI) ? 0x02 : 0x21;
                dev->buffer[4] = 0;
                if (dev->drv->bus_type == MO_BUS_SCSI) {
                    dev->buffer[6] = 1;       /* 16-bit transfers supported */
                    dev->buffer[7] = 0x20;    /* Wide bus supported */
                }
                dev->buffer[7] |= 0x02;

                if (dev->drv->type > 0) {
                    ide_padstr8(dev->buffer + 8, 8,
                                mo_drive_types[dev->drv->type].vendor);      /* Vendor */
                    ide_padstr8(dev->buffer + 16, 16,
                             mo_drive_types[dev->drv->type].model);          /* Product */
                    ide_padstr8(dev->buffer + 32, 4,
                                mo_drive_types[dev->drv->type].revision);    /* Revision */
                } else {
                    ide_padstr8(dev->buffer + 8, 8,
                                EMU_NAME);          /* Vendor */
                    ide_padstr8(dev->buffer + 16, 16,
                                device_identify);      /* Product */
                    ide_padstr8(dev->buffer + 32, 4,
                                EMU_VERSION_EX);    /* Revision */
                }
                idx = 36;

                if (max_len == 96) {
                    dev->buffer[4] = 91;
                    idx            = 96;
                } else if (max_len == 128) {
                    dev->buffer[4] = 0x75;
                    idx            = 128;
                }
            }

            dev->buffer[size_idx] = idx - preamble_len;
            len                   = idx;

            len = MIN(len, max_len);
            mo_set_buf_len(dev, BufLen, &len);

            mo_data_command_finish(dev, len, len, max_len, 0);
            break;

        case GPCMD_PREVENT_REMOVAL:
            mo_set_phase(dev, SCSI_PHASE_STATUS);
            mo_command_complete(dev);
            break;

        case GPCMD_SEEK_6:
        case GPCMD_SEEK_10:
            mo_set_phase(dev, SCSI_PHASE_STATUS);

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
            mo_seek(dev, pos);
            mo_command_complete(dev);
            break;

        case GPCMD_READ_CDROM_CAPACITY:
            mo_set_phase(dev, SCSI_PHASE_DATA_IN);

            mo_buf_alloc(dev, 8);

            /* IMPORTANT: What's returned is the last LBA block. */
            max_len = dev->drv->medium_size - 1;
            memset(dev->buffer, 0, 8);
            dev->buffer[0] = (max_len >> 24) & 0xff;
            dev->buffer[1] = (max_len >> 16) & 0xff;
            dev->buffer[2] = (max_len >> 8) & 0xff;
            dev->buffer[3] = max_len & 0xff;
            dev->buffer[6] = (dev->drv->sector_size >> 8) & 0xff;
            dev->buffer[7] = dev->drv->sector_size & 0xff;
            len            = 8;

            mo_set_buf_len(dev, BufLen, &len);

            mo_data_command_finish(dev, len, len, len, 0);
            break;

        case GPCMD_ERASE_10:
        case GPCMD_ERASE_12:
            /* Relative address. */
            if (cdb[1] & 1)
                previous_pos = dev->sector_pos;

            switch (cdb[0]) {
                case GPCMD_ERASE_10:
                    dev->sector_len = (cdb[7] << 8) | cdb[8];
                    break;
                case GPCMD_ERASE_12:
                    dev->sector_len = (((uint32_t) cdb[6]) << 24) |
                                      (((uint32_t) cdb[7]) << 16) |
                                      (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);
                    break;

                default:
                    break;
            }

            /* Erase all remaining sectors. */
            if (cdb[1] & 4) {
                /* Cannot have a sector number when erase all. */
                if (dev->sector_len) {
                    mo_invalid_field(dev, dev->sector_len);
                    return;
                }
                mo_format(dev);
                mo_set_phase(dev, SCSI_PHASE_STATUS);
                mo_command_complete(dev);
                break;
            }

            switch (cdb[0]) {
                case GPCMD_ERASE_10:
                    dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) |
                                      (cdb[4] << 8) | cdb[5];
                    break;
                case GPCMD_ERASE_12:
                    dev->sector_pos = (((uint32_t) cdb[2]) << 24) |
                                      (((uint32_t) cdb[3]) << 16) |
                                      (((uint32_t) cdb[4]) << 8) | ((uint32_t) cdb[5]);
                    break;

                default:
                    break;
            }

            dev->sector_pos += previous_pos;

            mo_erase(dev);
            mo_set_phase(dev, SCSI_PHASE_STATUS);
            mo_command_complete(dev);
            break;

        /*
           Never seen media that supports generations but it's interesting to know if any
           implementation calls this commmand.
         */
        case GPCMD_READ_GENERATION:
            mo_set_phase(dev, SCSI_PHASE_DATA_IN);

            mo_buf_alloc(dev, 4);
            len = 4;

            dev->buffer[0] = 0;
            dev->buffer[1] = 0;
            dev->buffer[2] = 0;
            dev->buffer[3] = 0;

            mo_set_buf_len(dev, BufLen, &len);
            mo_data_command_finish(dev, len, len, len, 0);
            break;

        default:
            mo_illegal_opcode(dev, cdb[0]);
            break;
    }

#if 0
    mo_log(dev->log, "Phase: %02X, request length: %i\n",
           dev->tf->phase, dev->tf->request_length);
#endif

    if ((dev->packet_status == PHASE_COMPLETE) || (dev->packet_status == PHASE_ERROR))
        mo_buf_free(dev);
}

static void
mo_command_stop(scsi_common_t *sc)
{
    mo_t *dev = (mo_t *) sc;

    mo_command_complete(dev);
    mo_buf_free(dev);
}

/* The command second phase function, needed for Mode Select. */
static uint8_t
mo_phase_data_out(scsi_common_t *sc)
{
    mo_t *         dev         = (mo_t *) sc;
    const uint32_t last_sector = mo_types[dev->drv->type].sectors - 1;
    int            len         = 0;
    uint8_t        error       = 0;
    uint32_t       last_to_write;
    uint16_t       block_desc_len;
    uint16_t       pos;
    uint16_t       param_list_len;
    uint8_t        hdr_len;
    uint8_t        val;

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
                mo_blocks(dev, &len, 1);
            break;
        case GPCMD_WRITE_SAME_10:
            if (!dev->current_cdb[7] && !dev->current_cdb[8])
                last_to_write = last_sector;
            else
                last_to_write = dev->sector_pos + dev->sector_len - 1;

            for (int i = dev->sector_pos; i <= (int) last_to_write; i++) {
                if (dev->current_cdb[1] & 2) {
                    dev->buffer[0] = (i >> 24) & 0xff;
                    dev->buffer[1] = (i >> 16) & 0xff;
                    dev->buffer[2] = (i >> 8) & 0xff;
                    dev->buffer[3] = i & 0xff;
                } else if (dev->current_cdb[1] & 4) {
                    uint32_t s          = (i % 63);
                    uint32_t h          = ((i - s) / 63) % 16;
                    uint32_t c          = ((i - s) / 63) / 16;
                    dev->buffer[0] = (c >> 16) & 0xff;
                    dev->buffer[1] = (c >> 8) & 0xff;
                    dev->buffer[2] = c & 0xff;
                    dev->buffer[3] = h & 0xff;
                    dev->buffer[4] = (s >> 24) & 0xff;
                    dev->buffer[5] = (s >> 16) & 0xff;
                    dev->buffer[6] = (s >> 8) & 0xff;
                    dev->buffer[7] = s & 0xff;
                }
                if (fseek(dev->drv->fp, (i * dev->drv->sector_size), SEEK_SET) == -1)
                    mo_write_error(dev);
                if (feof(dev->drv->fp))
                    break;
                if (fwrite(dev->buffer, 1, dev->drv->sector_size, dev->drv->fp) != dev->drv->sector_size)
                    mo_write_error(dev);
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

            if (dev->drv->bus_type == MO_BUS_SCSI) {
                if (dev->current_cdb[0] == GPCMD_MODE_SELECT_6) {
                    block_desc_len = dev->buffer[2];
                    block_desc_len <<= 8;
                    block_desc_len |= dev->buffer[3];
                } else {
                    block_desc_len = dev->buffer[6];
                    block_desc_len <<= 8;
                    block_desc_len |= dev->buffer[7];
                }
            } else
                block_desc_len = 0;

            pos = hdr_len + block_desc_len;

            while (1) {
                if (pos >= param_list_len) {
                    mo_log(dev->log, "Buffer has only block descriptor\n");
                    break;
                }

                const uint8_t page     = dev->buffer[pos] & 0x3F;
                const uint8_t page_len = dev->buffer[pos + 1];

                pos += 2;

                if (!(mo_mode_sense_page_flags & (1LL << ((uint64_t) page))))
                    error |= 1;
                else for (uint8_t i = 0; i < page_len; i++) {
                    const uint8_t ch      = mo_mode_sense_pages_changeable.pages[page][i + 2];
                    const uint8_t old_val = dev->ms_pages_saved.pages[page][i + 2];
                    val                   = dev->buffer[pos + i];
                    if (val != old_val) {
                        if (ch)
                            dev->ms_pages_saved.pages[page][i + 2] = val;
                        else {
                            error |= 1;
                            mo_invalid_field_pl(dev, val);
                        }
                    }
                }

                pos += page_len;

                if (dev->drv->bus_type == MO_BUS_SCSI)
                    val = mo_mode_sense_pages_default_scsi.pages[page][0] & 0x80;
                else
                    val = mo_mode_sense_pages_default.pages[page][0] & 0x80;
                if (dev->do_page_save && val)
                    mo_mode_sense_save(dev);

                if (pos >= dev->total_length)
                    break;
            }

            if (error) {
                mo_buf_free(dev);
                return 0;
            }
            break;

        default:
            break;
    }

    mo_command_stop((scsi_common_t *) dev);
    return 1;
}

/* Peform a master init on the entire module. */
void
mo_global_init(void)
{
    /* Clear the global data. */
    memset(mo_drives, 0x00, sizeof(mo_drives));
}

static int
mo_get_max(UNUSED(const ide_t *ide), const int ide_has_dma, const int type)
{
    int ret;

    switch (type) {
        case TYPE_PIO:
            ret = ide_has_dma ? 3 : 0;
            break;
        case TYPE_SDMA:
        default:
            ret = -1;
            break;
        case TYPE_MDMA:
            ret = ide_has_dma ? 1 : -1;
            break;
        case TYPE_UDMA:
            ret = ide_has_dma ? 5 : -1;
            break;
    }

    return ret;
}

static int
mo_get_timings(UNUSED(const ide_t *ide), const int ide_has_dma, const int type)
{
    int ret;

    switch (type) {
        case TIMINGS_DMA:
            ret = ide_has_dma ? 0x96 : 0;
            break;
        case TIMINGS_PIO:
            ret = ide_has_dma ? 0xb4 : 0;
            break;
        case TIMINGS_PIO_FC:
            ret = ide_has_dma ? 0xb4 : 0;
            break;
        default:
            ret = 0;
            break;
    }

    return ret;
}

static void
mo_do_identify(const ide_t *ide, const int ide_has_dma)
{
    char model[40];

    const mo_t *mo = (mo_t *) ide->sc;

    memset(model, 0, 40);

    if (mo_drives[mo->id].type > 0) {
        snprintf(model, 40, "%s %s", mo_drive_types[mo_drives[mo->id].type].vendor,
                 mo_drive_types[mo_drives[mo->id].type].model);
        /* Firmware */
        ide_padstr((char *) (ide->buffer + 23),
                   mo_drive_types[mo_drives[mo->id].type].revision, 8);
        ide_padstr((char *) (ide->buffer + 27), model, 40);                                          /* Model */
    } else {
        snprintf(model, 40, "%s %s%02i", EMU_NAME, "86B_MO", mo->id);
        ide_padstr((char *) (ide->buffer + 23), EMU_VERSION_EX, 8);    /* Firmware */
        ide_padstr((char *) (ide->buffer + 27), model, 40);               /* Model */
    }

    if (ide_has_dma) {
        /* Supported ATA versions : ATA/ATAPI-4 ATA/ATAPI-6 */
        ide->buffer[80] = 0x70;
        /* Maximum ATA revision supported : ATA/ATAPI-6 T13 1410D revision 3a */
        ide->buffer[81] = 0x19;
    }
}

static void
mo_identify(const ide_t *ide, const int ide_has_dma)
{
    /* ATAPI device, direct-access device, removable media, interrupt DRQ */
    ide->buffer[0] = 0x8000 | (0 << 8) | 0x80 | (1 << 5);
    ide_padstr((char *) (ide->buffer + 10), "", 20);    /* Serial Number */
    ide->buffer[49]  = 0x200;                                 /* LBA supported */
    /* Interpret zero byte count limit as maximum length */
    ide->buffer[126] = 0xfffe;
    mo_do_identify(ide, ide_has_dma);
}

static void
mo_drive_reset(const int c)
{
    const uint8_t scsi_bus = (mo_drives[c].scsi_device_id >> 4) & 0x0f;
    const uint8_t scsi_id  = mo_drives[c].scsi_device_id & 0x0f;

    if (mo_drives[c].priv == NULL) {
        mo_drives[c].priv = (mo_t *) calloc(1, sizeof(mo_t));
        mo_t *dev         = (mo_t *) mo_drives[c].priv;

        char n[1024]      = { 0 };

        sprintf(n, "MO %i", c + 1);
        dev->log          = log_open(n);
    }

    mo_t *dev    = (mo_t *) mo_drives[c].priv;

    dev->id      = c;
    dev->cur_lun = SCSI_LUN_USE_CDB;

    if (mo_drives[c].bus_type == MO_BUS_SCSI) {
        if (dev->tf == NULL)
            dev->tf        = (ide_tf_t *) calloc(1, sizeof(ide_tf_t));

        /* SCSI MO, attach to the SCSI bus. */
        scsi_device_t *sd  = &scsi_devices[scsi_bus][scsi_id];

        sd->sc             = (scsi_common_t *) dev;
        sd->command        = mo_command;
        sd->request_sense  = mo_request_sense_for_scsi;
        sd->reset          = mo_reset;
        sd->phase_data_out = mo_phase_data_out;
        sd->command_stop   = mo_command_stop;
        sd->type           = SCSI_REMOVABLE_DISK;
    } else if (mo_drives[c].bus_type == MO_BUS_ATAPI) {
        /* ATAPI MO, attach to the IDE bus. */
        ide_t *id = ide_get_drive(mo_drives[c].ide_channel);
        /* If the IDE channel is initialized, we attach to it,
           otherwise, we do nothing - it's going to be a drive
           that's not attached to anything. */
        if (id) {
            id->sc               = (scsi_common_t *) dev;
            dev->tf              = id->tf;
            IDE_ATAPI_IS_EARLY   = 0;
            id->get_max          = mo_get_max;
            id->get_timings      = mo_get_timings;
            id->identify         = mo_identify;
            id->stop             = NULL;
            id->packet_command   = mo_command;
            id->device_reset     = mo_reset;
            id->phase_data_out   = mo_phase_data_out;
            id->command_stop     = mo_command_stop;
            id->bus_master_error = mo_bus_master_error;
            id->interrupt_drq    = 1;

            ide_atapi_attach(id);
        }
    }
}

void
mo_hard_reset(void)
{
    for (uint8_t c = 0; c < MO_NUM; c++) {
        if ((mo_drives[c].bus_type == MO_BUS_ATAPI) || (mo_drives[c].bus_type == MO_BUS_SCSI)) {
            if (mo_drives[c].bus_type == MO_BUS_SCSI) {
                const uint8_t scsi_bus = (mo_drives[c].scsi_device_id >> 4) & 0x0f;
                const uint8_t scsi_id  = mo_drives[c].scsi_device_id & 0x0f;

                /* Make sure to ignore any SCSI MO drive that has an out of range SCSI Bus. */
                if (scsi_bus >= SCSI_BUS_MAX)
                    continue;

                /* Make sure to ignore any SCSI MO drive that has an out of range ID. */
                if (scsi_id >= SCSI_ID_MAX)
                    continue;
            }

            /* Make sure to ignore any ATAPI MO drive that has an out of range IDE channel. */
            if ((mo_drives[c].bus_type == MO_BUS_ATAPI) && (mo_drives[c].ide_channel > 7))
                continue;

            mo_drive_reset(c);

            mo_t *dev = (mo_t *) mo_drives[c].priv;

            mo_log(dev->log, "MO hard_reset drive=%d\n", c);

            if (dev->tf == NULL)
                continue;

            dev->id  = c;
            dev->drv = &mo_drives[c];

            mo_init(dev);

            if (strlen(mo_drives[c].image_path))
                mo_load(dev, mo_drives[c].image_path, 0);

            mo_mode_sense_load(dev);

            if (mo_drives[c].bus_type == MO_BUS_SCSI)
                mo_log(dev->log, "SCSI MO drive %i attached to SCSI ID %i\n",
                       c, mo_drives[c].scsi_device_id);
            else if (mo_drives[c].bus_type == MO_BUS_ATAPI)
                mo_log(dev->log, "ATAPI MO drive %i attached to IDE channel %i\n",
                       c, mo_drives[c].ide_channel);
        }
    }
}

void
mo_close(void)
{
    for (uint8_t c = 0; c < MO_NUM; c++) {
        if (mo_drives[c].bus_type == MO_BUS_SCSI) {
            const uint8_t scsi_bus = (mo_drives[c].scsi_device_id >> 4) & 0x0f;
            const uint8_t scsi_id  = mo_drives[c].scsi_device_id & 0x0f;

            memset(&scsi_devices[scsi_bus][scsi_id], 0x00, sizeof(scsi_device_t));
        }

        mo_t *dev = (mo_t *) mo_drives[c].priv;

        if (dev) {
            mo_disk_unload(dev);

            if (dev->tf)
                free(dev->tf);

            if (dev->log != NULL) {
                mo_log(dev->log, "Log closed\n");

                log_close(dev->log);
                dev->log = NULL;
            }

            free(dev);
            mo_drives[c].priv = NULL;
        }
    }
}
