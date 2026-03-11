/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of a generic SCSI QIC Tape drive
 *          commands, for SCSI usage. Uses SIMH .tap file format.
 *
 * Authors: Plamen Ivanov
 *
 *          Copyright 2025-2026 Plamen Ivanov.
 */
#define _GNU_SOURCE
#include <inttypes.h>
#ifdef ENABLE_TAPE_LOG
#include <stdarg.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/log.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/nvr.h>
#include <86box/hdc_ide.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/hdc_ide.h>
#include <86box/scsi_tape.h>
#include <86box/version.h>

#ifdef _WIN32
#    include <windows.h>
#    include <io.h>
#else
#    include <unistd.h>
#endif

#define IDE_ATAPI_IS_EARLY             id->sc->pad0

tape_drive_t tape_drives[TAPE_NUM];

/* Default block size for fixed-mode operations. */
#define TAPE_DEFAULT_BLOCK_SIZE 512

// clang-format off
/*
   Table of all SCSI commands and their flags, needed for the new disc change /
   not ready handler.
 */
const uint8_t tape_command_flags[0x100] = {
    [0x00]          = IMPLEMENTED | CHECK_READY,             /* TEST UNIT READY */
    [0x01]          = IMPLEMENTED | CHECK_READY | SCSI_ONLY, /* REWIND */
    [0x02]          = IMPLEMENTED | CHECK_READY,             /* REQUEST BLOCK ADDRESS */
    [0x03]          = IMPLEMENTED | ALLOW_UA,                /* REQUEST SENSE */
    [0x05]          = IMPLEMENTED | CHECK_READY,             /* READ BLOCK LIMITS */
    [0x08]          = IMPLEMENTED | CHECK_READY,             /* READ(6) */
    [0x0a]          = IMPLEMENTED | CHECK_READY,             /* WRITE(6) */
    [0x0c]          = IMPLEMENTED | CHECK_READY,             /* SEEK BLOCK */
    [0x10]          = IMPLEMENTED | CHECK_READY,             /* WRITE FILEMARKS(6) */
    [0x11]          = IMPLEMENTED | CHECK_READY,             /* SPACE(6) */
    [0x12]          = IMPLEMENTED | ALLOW_UA,                /* INQUIRY */
    [0x15]          = IMPLEMENTED,                           /* MODE SELECT(6) */
    [0x16]          = IMPLEMENTED,                           /* RESERVE */
    // [0x16]          = IMPLEMENTED | SCSI_ONLY,               /* RESERVE */
    [0x17]          = IMPLEMENTED,                           /* RELEASE */
    // [0x17]          = IMPLEMENTED | SCSI_ONLY,               /* RELEASE */
    [0x19]          = IMPLEMENTED | CHECK_READY,             /* ERASE(6) */
    [0x1a]          = IMPLEMENTED,                           /* MODE SENSE(6) */
    [0x1b]          = IMPLEMENTED | CHECK_READY,             /* LOAD/UNLOAD */
    [0x1d]          = IMPLEMENTED,                           /* SEND DIAGNOSTIC */
    [0x1e]          = IMPLEMENTED | CHECK_READY,             /* PREVENT/ALLOW MEDIUM REMOVAL */
    [0x34]          = IMPLEMENTED | CHECK_READY,             /* READ POSITION */
    [0x55]          = IMPLEMENTED,                           /* MODE SELECT(10) */
    [0x5a]          = IMPLEMENTED,                           /* MODE SENSE(10) */
};

static uint64_t tape_mode_sense_page_flags =
    GPMODEP_UNIT_ATN_PAGE |
    GPMODEP_R_W_ERROR_PAGE |
    GPMODEP_DISCONNECT_PAGE |
    GPMODEP_DATA_COMPRESS_PAGE |
    GPMODEP_DEVICE_CONFIG_PAGE |
    GPMODEP_ALL_PAGES;

static const mode_sense_pages_t tape_mode_sense_pages_default_scsi = {
    .pages = {
        [0x00] = {
            GPMODE_UNIT_ATN_PAGE, 0x06,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        },     /* Guesswork */
        [GPMODE_R_W_ERROR_PAGE] = {
            GPMODE_R_W_ERROR_PAGE, 0x0A,
            0x00, 0x03, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00
        },
        [GPMODE_DISCONNECT_PAGE] = {
            GPMODE_DISCONNECT_PAGE, 0x0E,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00
        },
        [GPMODE_DATA_COMPRESS_PAGE] = {
            GPMODE_DATA_COMPRESS_PAGE, 0x0E,
            0x00, 0x00,                         /* DCE=0, DCC=0 (no compression) */
            0x00, 0x00, 0x00, 0x00,             /* Compression algorithm */
            0x00, 0x00, 0x00, 0x00,             /* Decompression algorithm */
            0x00, 0x00, 0x00, 0x00
        },
        [GPMODE_DEVICE_CONFIG_PAGE] = {
            GPMODE_DEVICE_CONFIG_PAGE, 0x0E,
            0x00,                               /* Active format */
            0x00,                               /* Active partition */
            0x00,                               /* Write buffer full ratio */
            0x00,                               /* Read buffer empty ratio */
            0x00, 0x00,                         /* Write delay time */
            0x00,                               /* DBR/BIS/RSmk/AVC/SOCF/RBO/REW/EEG */
            0x00,                               /* Gap size */
            0x00,                               /* EOD defined / EEG / SEW */
            0x00, 0x00, 0x00,                   /* Buffer size at early warning */
            0x00,                               /* Select data compression algorithm */
            0x00                                /* Reserved */
        },
    }
};

static const mode_sense_pages_t tape_mode_sense_pages_changeable = {
    .pages = {
        [0x00] = {
            GPMODE_UNIT_ATN_PAGE, 0x06,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff
        },    /* Guesswork */
        [GPMODE_R_W_ERROR_PAGE] = {
            GPMODE_R_W_ERROR_PAGE, 0x0A,
            0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00
        },
        [GPMODE_DISCONNECT_PAGE] = {
            GPMODE_DISCONNECT_PAGE, 0x0E,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00
        },
        [GPMODE_DATA_COMPRESS_PAGE] = {
            GPMODE_DATA_COMPRESS_PAGE, 0x0E,
            0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00
        },
        [GPMODE_DEVICE_CONFIG_PAGE] = {
            GPMODE_DEVICE_CONFIG_PAGE, 0x0E,
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00
        },
    }
};
// clang-format on

static void tape_command_complete(tape_t *dev);
static void tape_init(tape_t *dev);

#ifdef ENABLE_TAPE_LOG
int tape_do_log = ENABLE_TAPE_LOG;

#ifdef TAPE_FILE_LOG
static FILE *tape_log_file = NULL;

static void
tape_log(void *priv, const char *fmt, ...)
{
    va_list ap;

    (void) priv;

    if (!tape_do_log)
        return;

    if (!tape_log_file) {
        tape_log_file = fopen("/tmp/tape_debug.log", "a");
        if (!tape_log_file)
            return;
        time_t     now = time(NULL);
        struct tm *tm  = localtime(&now);
        char       timebuf[64];
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm);
        fprintf(tape_log_file, "\n=== Tape debug log started at %s ===\n", timebuf);
        fflush(tape_log_file);
    }

    va_start(ap, fmt);
    vfprintf(tape_log_file, fmt, ap);
    va_end(ap);
    fflush(tape_log_file);
}
#else
static void
tape_log(void *priv, const char *fmt, ...)
{
    va_list ap;

    if (tape_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#endif /* TAPE_FILE_LOG */
#else
#    define tape_log(priv, fmt, ...)
#endif

static int
tape_load_abort(const tape_t *dev)
{
    if (dev->drv->fp)
        fclose(dev->drv->fp);
    dev->drv->fp = NULL;
    tape_eject(dev->id);
    return 0;
}

int
tape_is_empty(const uint8_t id)
{
    const tape_t *dev = (const tape_t *) tape_drives[id].priv;
    int            ret = 0;

    if ((dev == NULL) || (dev->drv == NULL) || (dev->drv->fp == NULL))
        ret = 1;

    return ret;
}

void
tape_load(const tape_t *dev, const char *fn, const int skip_insert)
{
    const int was_empty = tape_is_empty(dev->id);
    int       ret       = 0;
    int       offs      = 0;

    if (strstr(fn, "wp://") == fn) {
        offs                = 5;
        dev->drv->read_only = 1;
    }

    fn += offs;

    if (dev->drv == NULL)
        tape_eject(dev->id);
    else {
        dev->drv->fp = plat_fopen(fn, dev->drv->read_only ? "rb" : "rb+");
        ret          = 1;

        if (dev->drv->fp == NULL) {
            if (!dev->drv->read_only) {
                dev->drv->fp = plat_fopen(fn, "rb");
                if (dev->drv->fp == NULL)
                    ret = tape_load_abort(dev);
                else
                    dev->drv->read_only = 1;
            } else
                ret = tape_load_abort(dev);
        }

        if (ret) {
            fseeko64(dev->drv->fp, 0, SEEK_END);
            ((tape_t *) dev)->tape_length = (uint32_t) ftello64(dev->drv->fp);
            fseeko64(dev->drv->fp, 0, SEEK_SET);

            ((tape_t *) dev)->tape_pos   = 0;
            ((tape_t *) dev)->bot        = 1;
            ((tape_t *) dev)->eot        = 0;
            ((tape_t *) dev)->num_blocks = 0;

            strncpy(dev->drv->image_path, fn - offs, sizeof(dev->drv->image_path) - 1);
        }
    }

    if (ret && !skip_insert) {
        tape_insert((tape_t *) dev);
        if (was_empty)
            tape_insert((tape_t *) dev);
    }

    if (ret)
        ui_sb_update_icon_wp(SB_TAPE | dev->id, dev->drv->read_only);
}

void
tape_disk_reload(const tape_t *dev)
{
    if (strlen(dev->drv->prev_image_path) != 0)
        (void) tape_load(dev, dev->drv->prev_image_path, 0);
}

static void
tape_disk_unload(const tape_t *dev)
{
    if ((dev->drv != NULL) && (dev->drv->fp != NULL)) {
        fclose(dev->drv->fp);
        dev->drv->fp = NULL;
    }
}

void
tape_disk_close(const tape_t *dev)
{
    if ((dev->drv != NULL) && (dev->drv->fp != NULL)) {
        tape_disk_unload(dev);

        memcpy(dev->drv->prev_image_path, dev->drv->image_path,
               sizeof(dev->drv->image_path));
        memset(dev->drv->image_path, 0, sizeof(dev->drv->image_path));

        tape_insert((tape_t *) dev);
    }
}

static void
tape_set_callback(const tape_t *dev)
{
    if (dev->drv->bus_type != TAPE_BUS_SCSI)
        ide_set_callback(ide_drives[dev->drv->ide_channel], dev->callback);
}

static void
tape_init(tape_t *dev)
{
    if (dev->id < TAPE_NUM) {
        dev->requested_blocks = 1;
        dev->sense[0]         = 0xf0;
        dev->sense[7]         = 10;

        dev->drv->bus_mode = 0;
        if (dev->drv->bus_type >= TAPE_BUS_ATAPI)
            dev->drv->bus_mode |= 2;
        if (dev->drv->bus_type < TAPE_BUS_SCSI)
            dev->drv->bus_mode |= 1;

        tape_log(dev->log, "Bus type %i, bus mode %i\n", dev->drv->bus_type, dev->drv->bus_mode);
        if (dev->drv->bus_type < TAPE_BUS_SCSI) {
            dev->tf->phase          = 1;
            dev->tf->request_length = 0xEB14;
        }
        dev->tf->status    = READY_STAT | DSC_STAT;
        dev->tf->pos       = 0;
        dev->packet_status = PHASE_NONE;
        tape_sense_key = tape_asc = tape_ascq = dev->unit_attention = dev->transition = 0;
        tape_info      = 0x00000000;
        dev->block_size  = TAPE_DEFAULT_BLOCK_SIZE;
        dev->tape_pos    = 0;
        dev->num_blocks  = 0;
        dev->bot         = 1;
        dev->eot         = 0;
        dev->filemark_pending = 0;
        dev->rec_remaining    = 0;
    }
}

static int
tape_supports_pio(const tape_t *dev)
{
    return (dev->drv->bus_mode & 1);
}

static int
tape_supports_dma(const tape_t *dev)
{
    return (dev->drv->bus_mode & 2);
}

/* Returns: 0 for none, 1 for PIO, 2 for DMA. */
static int
tape_current_mode(const tape_t *dev)
{
    if (!tape_supports_pio(dev) && !tape_supports_dma(dev))
        return 0;
    if (tape_supports_pio(dev) && !tape_supports_dma(dev)) {
        tape_log(dev->log, "Drive does not support DMA, setting to PIO\n");
        return 1;
    }
    if (!tape_supports_pio(dev) && tape_supports_dma(dev))
        return 2;
    if (tape_supports_pio(dev) && tape_supports_dma(dev)) {
        tape_log(dev->log, "Drive supports both, setting to %s\n",
                 (dev->tf->features & 1) ? "DMA" : "PIO");
        return (dev->tf->features & 1) ? 2 : 1;
    }

    return 0;
}

static void
tape_mode_sense_load(tape_t *dev)
{
    char fn[512] = { 0 };

    memset(&dev->ms_pages_saved, 0, sizeof(mode_sense_pages_t));
    memcpy(&dev->ms_pages_saved, &tape_mode_sense_pages_default_scsi,
           sizeof(mode_sense_pages_t));

    sprintf(fn, "scsi_tape_%02i_mode_sense_bin", dev->id);
    FILE *fp = plat_fopen(nvr_path(fn), "rb");
    if (fp) {
        /* Nothing to read for now. */
        fclose(fp);
    }
}

static void
tape_mode_sense_save(const tape_t *dev)
{
    char fn[512] = { 0 };

    sprintf(fn, "scsi_tape_%02i_mode_sense_bin", dev->id);
    FILE *fp = plat_fopen(nvr_path(fn), "wb");
    if (fp) {
        /* Nothing to write for now. */
        fclose(fp);
    }
}

static uint8_t
tape_mode_sense_read(const tape_t *dev, const uint8_t pgctl,
                     const uint8_t page, const uint8_t pos)
{
    switch (pgctl) {
        case 0:
        case 3:
            return dev->ms_pages_saved.pages[page][pos];
        case 1:
            return tape_mode_sense_pages_changeable.pages[page][pos];
        case 2:
            return tape_mode_sense_pages_default_scsi.pages[page][pos];
        default:
            break;
    }

    return 0;
}

static uint32_t
tape_mode_sense(const tape_t *dev, uint8_t *buf, uint32_t pos,
                uint8_t page, const uint8_t block_descriptor_len)
{
    const uint64_t pf    = tape_mode_sense_page_flags;
    const uint8_t  pgctl = (page >> 6) & 3;

    page &= 0x3f;

    if (block_descriptor_len) {
        /* Tape block descriptor: density code, num blocks, block length. */
        const uint8_t density = tape_types[dev->drv->medium_type < KNOWN_TAPE_TYPES ?
                                           dev->drv->medium_type : 0].density_code;
        buf[pos++] = density;         /* Density code */
        buf[pos++] = 0;              /* Number of blocks (0 = unspecified for tape) */
        buf[pos++] = 0;
        buf[pos++] = 0;
        buf[pos++] = 0;              /* Reserved */
        buf[pos++] = (dev->block_size >> 16) & 0xff; /* Block length */
        buf[pos++] = (dev->block_size >> 8) & 0xff;
        buf[pos++] = dev->block_size & 0xff;
    }

    for (uint8_t i = 0; i < 0x40; i++) {
        if ((page == GPMODE_ALL_PAGES) || (page == i)) {
            if (pf & (1LL << ((uint64_t) i))) {
                const uint8_t msplen = tape_mode_sense_read(dev, pgctl, i, 1);
                buf[pos++]           = tape_mode_sense_read(dev, pgctl, i, 0);
                buf[pos++]           = msplen;
                tape_log(dev->log, "MODE SENSE: Page [%02X] length %i\n", i, msplen);
                for (uint8_t j = 0; j < msplen; j++)
                    buf[pos++] = tape_mode_sense_read(dev, pgctl, i, 2 + j);
            }
        }
    }

    return pos;
}

static void
tape_update_request_length(tape_t *dev, int len, const int block_len)
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
            dev->max_transfer_len = (dev->max_transfer_len / dev->block_size) * dev->block_size;

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
tape_bus_speed(tape_t *dev)
{
    double ret = -1.0;

    if (dev && dev->drv)
        ret = ide_atapi_get_period(dev->drv->ide_channel);

    if (ret == -1.0) {
        if (dev)
            dev->callback = -1.0;
        ret = 0.0;
    }

    return ret;
}

static void
tape_command_common(tape_t *dev)
{
    dev->tf->status = BUSY_STAT;
    dev->tf->phase  = 1;
    dev->tf->pos    = 0;
    if (dev->packet_status == PHASE_COMPLETE)
        dev->callback = 0.0;
    else if (dev->drv->bus_type == TAPE_BUS_SCSI)
        dev->callback = -1.0; /* Speed depends on SCSI controller */
    else
        dev->callback = tape_bus_speed(dev) * (double) (dev->packet_len);

    tape_set_callback(dev);
}

static void
tape_command_complete(tape_t *dev)
{
    dev->packet_status = PHASE_COMPLETE;
    tape_command_common(dev);
}

static void
tape_command_read(tape_t *dev)
{
    dev->packet_status = PHASE_DATA_IN;
    tape_command_common(dev);
}

static void
tape_command_read_dma(tape_t *dev)
{
    dev->packet_status = PHASE_DATA_IN_DMA;
    tape_command_common(dev);
}

static void
tape_command_write(tape_t *dev)
{
    dev->packet_status = PHASE_DATA_OUT;
    tape_command_common(dev);
}

static void
tape_command_write_dma(tape_t *dev)
{
    dev->packet_status = PHASE_DATA_OUT_DMA;
    tape_command_common(dev);
}

/*
   dev = Pointer to current SCSI disk device;
   len = Total transfer length;
   block_len = Length of a single block (why does it matter?!);
   alloc_len = Allocated transfer length;
   direction = Transfer direction (0 = read from host, 1 = write to host).
 */
static void
tape_data_command_finish(tape_t *dev, int len, const int block_len,
                         const int alloc_len, const int direction)
{
    tape_log(dev->log, "Finishing command (%02X): %i, %i, %i, %i, %i\n",
             dev->current_cdb[0], len, block_len, alloc_len, direction,
             dev->tf->request_length);
    dev->tf->pos = 0;
    if (alloc_len >= 0) {
        if (alloc_len < len)
            len = alloc_len;
    }
    if ((len == 0) || (tape_current_mode(dev) == 0)) {
        if (dev->drv->bus_type != TAPE_BUS_SCSI)
            dev->packet_len = 0;

        tape_command_complete(dev);
    } else {
        if (tape_current_mode(dev) == 2) {
            if (dev->drv->bus_type != TAPE_BUS_SCSI)
                dev->packet_len = alloc_len;

            if (direction == 0)
                tape_command_read_dma(dev);
            else
                tape_command_write_dma(dev);
        } else {
            tape_update_request_length(dev, len, block_len);
            if ((dev->drv->bus_type != TAPE_BUS_SCSI) &&
                (dev->tf->request_length == 0))
                tape_command_complete(dev);
            else if (direction == 0)
                tape_command_read(dev);
            else
                tape_command_write(dev);
        }
    }

    tape_log(dev->log, "Status: %i, cylinder %i, packet length: %i, position: %i, "
             "phase: %i\n",
             dev->packet_status, dev->tf->request_length, dev->packet_len,
             dev->tf->pos, dev->tf->phase);
}

static void
tape_sense_clear(tape_t *dev, UNUSED(int command))
{
    tape_sense_key = tape_asc = tape_ascq = 0;
    tape_info      = 0x00000000;
    /* Clear the filemark and EOM bits. */
    dev->sense[2] &= 0x0f;
}

static void
tape_set_phase(const tape_t *dev, const uint8_t phase)
{
    const uint8_t scsi_bus = (dev->drv->scsi_device_id >> 4) & 0x0f;
    const uint8_t scsi_id  = dev->drv->scsi_device_id & 0x0f;

    scsi_devices[scsi_bus][scsi_id].phase = phase;
}

static void
tape_cmd_error(tape_t *dev)
{
    tape_set_phase(dev, SCSI_PHASE_STATUS);
    dev->tf->error     = ((tape_sense_key & 0xf) << 4) | ABRT_ERR;
    dev->tf->status    = READY_STAT | ERR_STAT;
    dev->tf->phase     = 3;
    dev->tf->pos       = 0;
    dev->packet_status = PHASE_ERROR;
    dev->callback      = 50.0 * TAPE_TIME;
    tape_set_callback(dev);
    ui_sb_update_icon(SB_TAPE | dev->id, 0);
    ui_sb_update_icon_write(SB_TAPE | dev->id, 0);
    tape_log(dev->log, "[%02X] ERROR: %02X/%02X/%02X\n", dev->current_cdb[0],
             tape_sense_key, tape_asc, tape_ascq);
}

static void
tape_unit_attention(tape_t *dev)
{
    tape_set_phase(dev, SCSI_PHASE_STATUS);
    dev->tf->error     = (SENSE_UNIT_ATTENTION << 4) | ABRT_ERR;
    dev->tf->status    = READY_STAT | ERR_STAT;
    dev->tf->phase     = 3;
    dev->tf->pos       = 0;
    dev->packet_status = PHASE_ERROR;
    dev->callback      = 50.0 * TAPE_TIME;
    tape_set_callback(dev);
    ui_sb_update_icon(SB_TAPE | dev->id, 0);
    ui_sb_update_icon_write(SB_TAPE | dev->id, 0);
    tape_log(dev->log, "UNIT ATTENTION\n");
}

static void
tape_buf_alloc(tape_t *dev, uint32_t len)
{
    tape_log(dev->log, "Allocated buffer length: %i\n", len);

    if (dev->buffer == NULL) {
        dev->buffer    = (uint8_t *) malloc(len);
        dev->buffer_sz = len;
    }

    if (len > dev->buffer_sz) {
        uint8_t *buf   = (uint8_t *) realloc(dev->buffer, len);
        dev->buffer    = buf;
        dev->buffer_sz = len;
    }
}

static void
tape_buf_free(tape_t *dev)
{
    if (dev->buffer) {
        tape_log(dev->log, "Freeing buffer...\n");
        free(dev->buffer);
        dev->buffer = NULL;
    }
}

static void
tape_bus_master_error(scsi_common_t *sc)
{
    tape_t *dev = (tape_t *) sc;

    tape_buf_free(dev);
    tape_sense_key = tape_asc = tape_ascq = 0;
    tape_info      =  (dev->sector_pos >> 24)        |
                     ((dev->sector_pos >> 16) <<  8) |
                     ((dev->sector_pos >> 8)  << 16) |
                     ( dev->sector_pos        << 24);
    tape_cmd_error(dev);
}

static void
tape_not_ready(tape_t *dev)
{
    tape_sense_key = SENSE_NOT_READY;
    tape_asc       = ASC_MEDIUM_NOT_PRESENT;
    tape_ascq      = 0;
    tape_info      = 0x00000000;
    tape_cmd_error(dev);
}

static void
tape_write_protected(tape_t *dev)
{
    tape_sense_key = SENSE_DATA_PROTECT;
    tape_asc       = ASC_WRITE_PROTECTED;
    tape_ascq      = 0;
    tape_info      = 0x00000000;
    tape_cmd_error(dev);
}

static void
tape_invalid_lun(tape_t *dev, const uint8_t lun)
{
    tape_sense_key = SENSE_ILLEGAL_REQUEST;
    tape_asc       = ASC_INV_LUN;
    tape_ascq      = 0;
    tape_info      = lun << 24;
    tape_cmd_error(dev);
}

static void
tape_illegal_opcode(tape_t *dev, const uint8_t opcode)
{
    tape_sense_key = SENSE_ILLEGAL_REQUEST;
    tape_asc       = ASC_ILLEGAL_OPCODE;
    tape_ascq      = 0;
    tape_info      = opcode << 24;
    tape_cmd_error(dev);
}

static void
tape_invalid_field(tape_t *dev, const uint32_t field)
{
    tape_sense_key = SENSE_ILLEGAL_REQUEST;
    tape_asc       = ASC_INV_FIELD_IN_CMD_PACKET;
    tape_ascq      = 0;
    tape_info      =  (field >> 24)        |
                     ((field >> 16) <<  8) |
                     ((field >> 8)  << 16) |
                     ( field        << 24);
    tape_cmd_error(dev);
    dev->tf->status = 0x53;
}

static void
tape_invalid_field_pl(tape_t *dev, const uint32_t field)
{
    tape_sense_key = SENSE_ILLEGAL_REQUEST;
    tape_asc       = ASC_INV_FIELD_IN_PARAMETER_LIST;
    tape_ascq      = 0;
    tape_info      =  (field >> 24)        |
                     ((field >> 16) <<  8) |
                     ((field >> 8)  << 16) |
                     ( field        << 24);
    tape_cmd_error(dev);
    dev->tf->status = 0x53;
}

/* Report a filemark-detected condition. */
static void
tape_filemark_detected(tape_t *dev, uint32_t residual)
{
    tape_sense_key = SENSE_NONE;
    tape_asc       = ASC_NONE;
    tape_ascq      = ASCQ_FILEMARK_DETECTED;
    /* Set the FILEMARK bit in sense byte 2. */
    dev->sense[2] |= 0x80;
    tape_info      =  (residual >> 24)        |
                     ((residual >> 16) <<  8) |
                     ((residual >> 8)  << 16) |
                     ( residual        << 24);
    tape_cmd_error(dev);
}

/* Report a blank check / end-of-data condition. */
static void
tape_blank_check(tape_t *dev, uint32_t residual)
{
    tape_sense_key = SENSE_BLANK_CHECK;
    tape_asc       = ASC_NONE;
    tape_ascq      = ASCQ_EOD_DETECTED;
    tape_info      =  (residual >> 24)        |
                     ((residual >> 16) <<  8) |
                     ((residual >> 8)  << 16) |
                     ( residual        << 24);
    tape_cmd_error(dev);
}

/* Report beginning-of-partition. */
static void
tape_bop_detected(tape_t *dev, uint32_t residual)
{
    tape_sense_key = SENSE_NONE;
    tape_asc       = ASC_NONE;
    tape_ascq      = ASCQ_BOP_DETECTED;
    /* Set the EOM bit in sense byte 2. */
    dev->sense[2] |= 0x40;
    tape_info      =  (residual >> 24)        |
                     ((residual >> 16) <<  8) |
                     ((residual >> 8)  << 16) |
                     ( residual        << 24);
    tape_cmd_error(dev);
}

/* ==================== SIMH .tap I/O helpers ==================== */

/*
 * Read a 4-byte little-endian record length marker from the .tap file.
 * Returns 0 on success, -1 on read error or EOF.
 */
static int
tape_read_marker(const tape_t *dev, uint32_t *marker)
{
    uint8_t buf[4];

    if (fread(buf, 1, 4, dev->drv->fp) != 4)
        return -1;

    *marker = (uint32_t) buf[0] |
              ((uint32_t) buf[1] << 8) |
              ((uint32_t) buf[2] << 16) |
              ((uint32_t) buf[3] << 24);

    return 0;
}

/*
 * Write a 4-byte little-endian record length marker to the .tap file.
 */
static int
tape_write_marker(const tape_t *dev, uint32_t marker)
{
    uint8_t buf[4];

    buf[0] = marker & 0xff;
    buf[1] = (marker >> 8) & 0xff;
    buf[2] = (marker >> 16) & 0xff;
    buf[3] = (marker >> 24) & 0xff;

    if (fwrite(buf, 1, 4, dev->drv->fp) != 4)
        return -1;

    return 0;
}

/*
 * Read one SIMH record from the tape at the current position.
 * Returns: record length on success, 0 for filemark, -1 for EOD, -2 for error.
 */
static int32_t
tape_simh_read_record(tape_t *dev, uint8_t *buf, uint32_t buf_size)
{
    uint32_t leading_len;
    uint32_t trailing_len;

    if (tape_read_marker(dev, &leading_len) < 0) {
        tape_log(dev->log, "  read_record: read_marker failed (past EOF), tape_pos=%u\n",
                 dev->tape_pos);
        return -1; /* Past end of file = EOD. */
    }

    tape_log(dev->log, "  read_record: leading_len=0x%08X (%u), tape_pos=%u\n",
             leading_len, leading_len, dev->tape_pos);

    /* Filemark. */
    if (leading_len == TAPE_SIMH_FILEMARK) {
        tape_log(dev->log, "  read_record: FILEMARK at tape_pos=%u\n", dev->tape_pos);
        dev->tape_pos += 4;
        return 0;
    }

    /* End of data. */
    if (leading_len == TAPE_SIMH_EOD || leading_len == TAPE_SIMH_GAP) {
        tape_log(dev->log, "  read_record: EOD/GAP at tape_pos=%u\n", dev->tape_pos);
        return -1;
    }

    /* Limit to buffer size. */
    const uint32_t to_read = (leading_len > buf_size) ? buf_size : leading_len;

    if (fread(buf, 1, to_read, dev->drv->fp) != to_read)
        return -2;

    /* If the record was larger than our buffer, skip remaining bytes. */
    if (leading_len > buf_size)
        fseeko64(dev->drv->fp, (int64_t)(leading_len - buf_size), SEEK_CUR);

    /* Read the trailing length marker. */
    if (tape_read_marker(dev, &trailing_len) < 0)
        return -2;

    /* Advance position. */
    dev->tape_pos += 4 + leading_len + 4;
    dev->num_blocks++;
    dev->bot = 0;

    return (int32_t) leading_len;
}

/*
 * Write one SIMH record to the tape at the current position.
 * Returns 0 on success, -1 on error.
 */
static int
tape_simh_write_record(tape_t *dev, const uint8_t *buf, uint32_t len)
{
    if (tape_write_marker(dev, len) < 0)
        return -1;

    if (fwrite(buf, 1, len, dev->drv->fp) != len)
        return -1;

    if (tape_write_marker(dev, len) < 0)
        return -1;

    /* Write EOD marker after the record. */
    if (tape_write_marker(dev, TAPE_SIMH_EOD) < 0)
        return -1;

    fflush(dev->drv->fp);

    /* Seek back to just after the trailing length (before the EOD marker). */
    fseeko64(dev->drv->fp, -4, SEEK_CUR);

    dev->tape_pos += 4 + len + 4;
    dev->num_blocks++;
    dev->bot = 0;

    return 0;
}

/*
 * Write a filemark to the tape.
 */
static int
tape_simh_write_filemark(tape_t *dev)
{
    if (tape_write_marker(dev, TAPE_SIMH_FILEMARK) < 0)
        return -1;

    /* Write EOD marker after the filemark. */
    if (tape_write_marker(dev, TAPE_SIMH_EOD) < 0)
        return -1;

    fflush(dev->drv->fp);

    /* Seek back to just after the filemark (before the EOD marker). */
    fseeko64(dev->drv->fp, -4, SEEK_CUR);

    dev->tape_pos += 4;
    dev->bot = 0;

    return 0;
}

static void
tape_seek_blocks_forward(tape_t *dev, int32_t count)
{
    while (1) {
        uint32_t leading_len;

        if (tape_read_marker(dev, &leading_len) < 0) {
            /* Past end of file = EOD. */
            return;
        }

        if (leading_len == TAPE_SIMH_EOD || leading_len == TAPE_SIMH_GAP) {
            /* End of data. */
            return;
        }

        if (leading_len == TAPE_SIMH_FILEMARK) {
            /* Filemark encountered, ignore. */
            dev->tape_pos += 4;
            continue;
        }

        /* Skip over the data and trailing length. */
        dev->bot = 0;
        if (dev->num_blocks == count) {
            /* Seek back to just after the filemark (before the EOD marker). */
            fseeko64(dev->drv->fp, -4, SEEK_CUR);
            tape_log(dev->log, "First data block after %i blocks, seek back\n", dev->num_blocks);
            break;
        } else {
            fseeko64(dev->drv->fp, (int64_t)(leading_len + 4), SEEK_CUR);
            dev->tape_pos += 4 + leading_len + 4;
            tape_log(dev->log, "Found %i blocks, increasing to %i\n", dev->num_blocks, dev->num_blocks + 1);
            dev->num_blocks++;
       }
    }

    return;
}

/*
 * Space forward N blocks (data records). Stops at filemarks.
 * Returns: number of blocks actually spaced, or negative on error/filemark.
 */
static int32_t
tape_space_blocks_forward(tape_t *dev, int32_t count)
{
    int32_t spaced = 0;

    for (int32_t i = 0; i < count; i++) {
        uint32_t leading_len;

        if (tape_read_marker(dev, &leading_len) < 0) {
            /* Past end of file = EOD. */
            return -(count - spaced);
        }

        if (leading_len == TAPE_SIMH_EOD || leading_len == TAPE_SIMH_GAP) {
            /* End of data. */
            return -(count - spaced);
        }

        if (leading_len == TAPE_SIMH_FILEMARK) {
            /* Filemark encountered. */
            dev->tape_pos += 4;
            dev->filemark_pending = 1;
            return -(count - spaced);
        }

        /* Skip over the data and trailing length. */
        dev->bot = 0;
        fseeko64(dev->drv->fp, (int64_t)(leading_len + 4), SEEK_CUR);
        dev->tape_pos += 4 + leading_len + 4;
        dev->num_blocks++;
        spaced++;
    }

    return spaced;
}

/*
 * Space backward N blocks (data records).
 * Returns: number of blocks actually spaced, or negative on BOP/error.
 */
static int32_t
tape_space_blocks_backward(tape_t *dev, int32_t count)
{
    int32_t spaced = 0;

    for (int32_t i = 0; i < count; i++) {
        if (dev->tape_pos == 0) {
            dev->bot = 1;
            return -(count - spaced);
        }

        /* Read the trailing length of the previous record. */
        fseeko64(dev->drv->fp, (int64_t)(dev->tape_pos - 4), SEEK_SET);

        uint32_t trailing_len;
        if (tape_read_marker(dev, &trailing_len) < 0)
            return -(count - spaced);

        if (trailing_len == TAPE_SIMH_FILEMARK) {
            /* The previous item is a filemark. Back up past it. */
            dev->tape_pos -= 4;
            fseeko64(dev->drv->fp, (int64_t) dev->tape_pos, SEEK_SET);
            if (dev->num_blocks > 0)
                dev->num_blocks--;
            dev->filemark_pending = 1;
            return -(count - spaced);
        }

        /* Back up over: trailing_len(4) + data(trailing_len) + leading_len(4). */
        dev->tape_pos -= (4 + trailing_len + 4);
        fseeko64(dev->drv->fp, (int64_t) dev->tape_pos, SEEK_SET);
        if (dev->num_blocks > 0)
            dev->num_blocks--;
        spaced++;
    }

    if (dev->tape_pos == 0)
        dev->bot = 1;

    return spaced;
}

/*
 * Space forward N filemarks.
 * Returns: number of filemarks actually spaced, or negative on EOD.
 */
static int32_t
tape_space_filemarks_forward(tape_t *dev, int32_t count)
{
    int32_t found = 0;

    while (found < count) {
        uint32_t leading_len;

        if (tape_read_marker(dev, &leading_len) < 0)
            return -(count - found);

        if (leading_len == TAPE_SIMH_EOD || leading_len == TAPE_SIMH_GAP)
            return -(count - found);

        if (leading_len == TAPE_SIMH_FILEMARK) {
            dev->tape_pos += 4;
            found++;
        } else {
            /* Skip data record. */
            fseeko64(dev->drv->fp, (int64_t)(leading_len + 4), SEEK_CUR);
            dev->tape_pos += 4 + leading_len + 4;
            dev->num_blocks++;
        }

        dev->bot = 0;
    }

    return found;
}

/*
 * Space forward N filemarks.
 * Returns: number of filemarks actually spaced, or negative on EOD.
 */
static int32_t
tape_space_filemarks_backward(tape_t *dev, int32_t count)
{
    int32_t found = 0;

    /* Read the trailing length of the previous record. */
    fseeko64(dev->drv->fp, (int64_t)(dev->tape_pos - 4), SEEK_SET);

    while (found < count) {
        uint32_t trailing_len;

        if (tape_read_marker(dev, &trailing_len) < 0)
            return -(count - found);

        if (trailing_len == TAPE_SIMH_FILEMARK) {
            dev->tape_pos -= 4;
            found++;
        } else {
            /* Skip data record. */
            fseeko64(dev->drv->fp, (int64_t)(-trailing_len - 4), SEEK_CUR);
            dev->tape_pos -= 4 + trailing_len + 4;
            dev->num_blocks--;
        }

        dev->bot = 0;
    }

    return found;
}

/*
 * Space to end of data.
 */
static void
tape_space_to_eod(tape_t *dev)
{
    while (1) {
        uint32_t leading_len;

        if (tape_read_marker(dev, &leading_len) < 0)
            break;

        if (leading_len == TAPE_SIMH_EOD || leading_len == TAPE_SIMH_GAP) {
            /* Back up to before the EOD marker so writes append here. */
            fseeko64(dev->drv->fp, -4, SEEK_CUR);
            break;
        }

        if (leading_len == TAPE_SIMH_FILEMARK) {
            dev->tape_pos += 4;
        } else {
            fseeko64(dev->drv->fp, (int64_t)(leading_len + 4), SEEK_CUR);
            dev->tape_pos += 4 + leading_len + 4;
            dev->num_blocks++;
        }

        dev->bot = 0;
    }
}

/* ==================== End SIMH .tap I/O helpers ==================== */

void
tape_insert(tape_t *dev)
{
    if ((dev != NULL) && (dev->drv != NULL)) {
        if (dev->drv->fp == NULL) {
            dev->unit_attention = 0;
            dev->transition     = 0;
            tape_log(dev->log, "Media removal\n");
        } else if (dev->transition) {
            dev->unit_attention = 1;
            dev->transition     = 0;
            tape_log(dev->log, "Media insert\n");
        } else {
            dev->unit_attention = 0;
            dev->transition     = 1;
            tape_log(dev->log, "Media transition\n");
        }
    }
}

static int
tape_pre_execution_check(tape_t *dev, const uint8_t *cdb)
{
    int ready;

    if ((cdb[0] != GPCMD_REQUEST_SENSE) && (dev->cur_lun == SCSI_LUN_USE_CDB) &&
        (cdb[1] & 0xe0)) {
        tape_log(dev->log, "Attempting to execute a unknown command targeted at "
                 "SCSI LUN %i\n", ((dev->tf->request_length >> 5) & 7));
        tape_invalid_lun(dev, cdb[1] >> 5);
        return 0;
    }

    if (!(tape_command_flags[cdb[0]] & IMPLEMENTED)) {
        tape_log(dev->log, "REJECTED unknown command %02X over SCSI - CDB: "
                 "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
                 cdb[0], cdb[0], cdb[1], cdb[2], cdb[3], cdb[4], cdb[5],
                 cdb[6], cdb[7], cdb[8], cdb[9], cdb[10], cdb[11]);
        tape_illegal_opcode(dev, cdb[0]);
        return 0;
    }

    if ((dev->drv->bus_type < TAPE_BUS_SCSI) &&
        (tape_command_flags[cdb[0]] & SCSI_ONLY)) {
        tape_log(dev->log, "Attempting to execute SCSI-only command %02X "
                 "over ATAPI\n", cdb[0]);
        tape_illegal_opcode(dev, cdb[0]);
        return 0;
    }

    if ((dev->drv->bus_type == TAPE_BUS_SCSI) &&
        (tape_command_flags[cdb[0]] & ATAPI_ONLY)) {
        tape_log(dev->log, "Attempting to execute ATAPI-only command %02X "
                 "over SCSI\n", cdb[0]);
        tape_illegal_opcode(dev, cdb[0]);
        return 0;
    }

    if (dev->transition) {
        if ((cdb[0] == GPCMD_TEST_UNIT_READY) || (cdb[0] == GPCMD_REQUEST_SENSE))
            ready = 0;
        else {
            if (!(tape_command_flags[cdb[0]] & ALLOW_UA)) {
                tape_log(dev->log, "(ext_medium_changed != 0): tape_insert()\n");
                tape_insert((void *) dev);
            }
            ready = (dev->drv->fp != NULL);
        }
    } else
        ready = (dev->drv->fp != NULL);

    if (!ready && (dev->unit_attention > 0))
        dev->unit_attention = 0;

    if (dev->unit_attention == 1) {
        if (!(tape_command_flags[cdb[0]] & ALLOW_UA)) {
            tape_log(dev->log, "Unit attention now 2\n");
            dev->unit_attention++;
            tape_log(dev->log, "UNIT ATTENTION: Command %02X not allowed to "
                     "pass through\n", cdb[0]);
            tape_unit_attention(dev);
            return 0;
        }
    } else if (dev->unit_attention == 2) {
        if (cdb[0] != GPCMD_REQUEST_SENSE) {
            tape_log(dev->log, "Tape %i: Unit attention now 0\n", dev->id);
            dev->unit_attention = 0;
        }
    }

    if (cdb[0] != GPCMD_REQUEST_SENSE)
        tape_sense_clear(dev, cdb[0]);

    if (!ready && (tape_command_flags[cdb[0]] & CHECK_READY)) {
        tape_log(dev->log, "Not ready (%02X)\n", cdb[0]);
        tape_not_ready(dev);
        return 0;
    }

    tape_log(dev->log, "Continuing with command %02X\n", cdb[0]);
    return 1;
}

void
tape_reset(scsi_common_t *sc)
{
    tape_t *dev = (tape_t *) sc;

    dev->sector_pos     = 0;
    dev->sector_len     = 0;
    dev->tf->status     = 0;
    dev->callback       = 0.0;
    tape_set_callback(dev);
    dev->tf->phase      = 1;
    dev->tf->request_length = 0xeb14;
    dev->packet_status  = PHASE_NONE;
    dev->cur_lun        = SCSI_LUN_USE_CDB;
    tape_sense_key = tape_asc = tape_ascq = dev->unit_attention = dev->transition = 0;
    tape_info      = 0x00000000;
}

static void
tape_request_sense(tape_t *dev, uint8_t *buffer, const uint8_t alloc_length, const int desc)
{
    if (alloc_length != 0) {
        memset(buffer, 0x00, alloc_length);
        if (desc) {
            buffer[1] = tape_sense_key;
            buffer[2] = tape_asc;
            buffer[3] = tape_ascq;
        } else
            memcpy(buffer, dev->sense, alloc_length);
    }

    buffer[0] = desc ? 0x72 : 0xf0;
    if (!desc)
        buffer[7] = 10;

    if (dev->unit_attention && (tape_sense_key == 0)) {
        buffer[desc ? 1 : 2]  = SENSE_UNIT_ATTENTION;
        buffer[desc ? 2 : 12] = ASC_MEDIUM_MAY_HAVE_CHANGED;
        buffer[desc ? 3 : 13] = 0;
    }

    tape_log(dev->log, "Reporting sense: %02X %02X %02X\n", buffer[2], buffer[12], buffer[13]);

    if (buffer[desc ? 1 : 2] == SENSE_UNIT_ATTENTION)
        dev->unit_attention = 0;

    tape_sense_clear(dev, GPCMD_REQUEST_SENSE);

    if (dev->transition) {
        tape_log(dev->log, "TAPE_TRANSITION: tape_insert()\n");
        tape_insert((void *) dev);
    }
}

static void
tape_request_sense_for_scsi(scsi_common_t *sc, uint8_t *buffer, uint8_t alloc_length)
{
    tape_t    *dev   = (tape_t *) sc;
    const int  ready = (dev->drv->fp != NULL);

    if (!ready && dev->unit_attention)
        dev->unit_attention = 0;

    tape_request_sense(dev, buffer, alloc_length, 0);
}

static void
tape_set_buf_len(const tape_t *dev, int32_t *BufLen, int32_t *src_len)
{
    if (dev->drv->bus_type == TAPE_BUS_SCSI) {
        if (*BufLen == -1)
            *BufLen = *src_len;
        else {
            *BufLen  = MIN(*src_len, *BufLen);
            *src_len = *BufLen;
        }
        tape_log(dev->log, "Actual transfer length: %i\n", *BufLen);
    }
}

static void
tape_rewind(tape_t *dev)
{
    fseeko64(dev->drv->fp, 0, SEEK_SET);
    dev->tape_pos         = 0;
    dev->num_blocks       = 0;
    dev->bot              = 1;
    dev->eot              = 0;
    dev->filemark_pending = 0;
    dev->rec_remaining    = 0;
}

static void
tape_command(scsi_common_t *sc, const uint8_t *cdb)
{
    tape_t       *dev                = (tape_t *) sc;
    char          device_identify[9] = { '8', '6', 'B', '_', 'T', 'P', '0', '0', 0 };
    const uint8_t scsi_bus           = (dev->drv->scsi_device_id >> 4) & 0x0f;
    const uint8_t scsi_id            = dev->drv->scsi_device_id & 0x0f;
    int32_t       blen               = 0;
    int32_t       count              = 0;
    int           idx                = 0;
    int32_t       len;
    int32_t       max_len;
    int32_t       alloc_length;
    unsigned      preamble_len;
    int           block_desc;
    int           size_idx;
    int32_t      *BufLen;

    if (dev->drv->bus_type == TAPE_BUS_SCSI) {
        BufLen = &scsi_devices[scsi_bus][scsi_id].buffer_length;
        dev->tf->status &= ~ERR_STAT;
    } else {
        BufLen           = &blen;
        dev->tf->error   = 0;
    }

    dev->packet_len  = 0;
    dev->request_pos = 0;

    device_identify[7] = dev->id + 0x30;

    memcpy(dev->current_cdb, cdb, 12);

    if (cdb[0] != 0) {
        tape_log(dev->log, "Command 0x%02X, Sense Key %02X, Asc %02X, Ascq %02X, "
                 "Unit attention: %i\n",
                 cdb[0], tape_sense_key, tape_asc, tape_ascq, dev->unit_attention);

        tape_log(dev->log, "CDB: %02X %02X %02X %02X %02X %02X %02X %02X "
                 "%02X %02X %02X %02X\n",
                 cdb[0], cdb[1], cdb[2], cdb[3], cdb[4], cdb[5], cdb[6], cdb[7],
                 cdb[8], cdb[9], cdb[10], cdb[11]);
    }

    dev->sector_len = 0;

    tape_set_phase(dev, SCSI_PHASE_STATUS);

    if (tape_pre_execution_check(dev, cdb) == 0)
        return;

    switch (cdb[0]) {
        case GPCMD_SEND_DIAGNOSTIC:
            if (!(cdb[1] & (1 << 2))) {
                tape_invalid_field(dev, cdb[1]);
                return;
            }
            fallthrough;
        case GPCMD_SCSI_RESERVE:
        case GPCMD_SCSI_RELEASE:
        case GPCMD_TEST_UNIT_READY:
        case GPCMD_PREVENT_REMOVAL:
            tape_set_phase(dev, SCSI_PHASE_STATUS);
            tape_command_complete(dev);
            break;

        case GPCMD_REZERO_UNIT: /* 0x01 = REWIND for tape devices. */
            tape_log(dev->log, "REWIND\n");
            tape_rewind(dev);
            tape_set_phase(dev, SCSI_PHASE_STATUS);
            tape_command_complete(dev);
            break;

        case GPCMD_REQUEST_BLOCK_ADDRESS:
            tape_set_phase(dev, SCSI_PHASE_DATA_IN);

            max_len = MIN(3, cdb[4]);
            if (!max_len)
                max_len = 3;

            tape_buf_alloc(dev, 3);
            memset(dev->buffer, 0, 3);

            dev->buffer[0] = ((dev->num_blocks + 1) >> 16) & 0xff;
            dev->buffer[1] = ((dev->num_blocks + 1) >> 8) & 0xff;
            dev->buffer[2] = (dev->num_blocks + 1) & 0xff;

            len            = max_len;
            tape_set_buf_len(dev, BufLen, &len);

            tape_data_command_finish(dev, len, len, max_len, 0);
            break;

        case GPCMD_READ_BLOCK_LIMITS:
            tape_log(dev->log, "READ BLOCK LIMITS\n");
            tape_set_phase(dev, SCSI_PHASE_DATA_IN);

            len = 6;
            tape_buf_alloc(dev, len);
            memset(dev->buffer, 0, len);

            /* Byte 0: reserved */
            /* Bytes 1-3: maximum block length (32KB = 0x008000) */
            dev->buffer[1] = 0x00;
            dev->buffer[2] = 0x80;
            dev->buffer[3] = 0x00;
            /* Bytes 4-5: minimum block length (512 bytes = 0x0200) */
            dev->buffer[4] = 0x02;
            dev->buffer[5] = 0x00;

            tape_set_buf_len(dev, BufLen, &len);

            tape_log(dev->log, "Block limits: min=512, max=32768\n");
            tape_data_command_finish(dev, len, len, len, 0);
            break;

        case GPCMD_REQUEST_SENSE:
            max_len = cdb[4];

            if (!max_len) {
                tape_set_phase(dev, SCSI_PHASE_STATUS);
                dev->packet_status = PHASE_COMPLETE;
                dev->callback      = 20.0 * SCSI_TIME;
                tape_set_callback(dev);
                break;
            }

            tape_buf_alloc(dev, 256);
            tape_set_buf_len(dev, BufLen, &max_len);

            max_len = (cdb[1] & 1) ? 8 : 18;

            tape_request_sense(dev, dev->buffer, *BufLen, cdb[1] & 1);
            tape_set_phase(dev, SCSI_PHASE_DATA_IN);
            tape_data_command_finish(dev, max_len, max_len, *BufLen, 0);
            break;

        case GPCMD_SEEK_BLOCK:
            count = ((cdb[2] << 16) | (cdb[3] << 8) | cdb[4]) - 1;

            if (count > 0) {
                tape_rewind(dev);

                tape_seek_blocks_forward(dev, count);

                tape_set_phase(dev, SCSI_PHASE_STATUS);
                tape_command_complete(dev);
            } else
                tape_invalid_field_pl(dev, 0x00000000);
            break;

        /* LOCATE on tapes. */
        case GPCMD_SEEK_10:
            count = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];

            if (count > 0) {
                tape_rewind(dev);

                tape_seek_blocks_forward(dev, count);

                tape_set_phase(dev, SCSI_PHASE_STATUS);
                tape_command_complete(dev);
            } else
                tape_invalid_field_pl(dev, 0x00000000);
            break;

        case GPCMD_READ_6: {
            /* Tape READ(6):
               CDB byte 1 bit 0: Fixed = 1 (fixed block), 0 (variable block)
               CDB bytes 2-4: Transfer length
                 Fixed: number of blocks
                 Variable: number of bytes (reads one record)
             */
            const int fixed = cdb[1] & 1;
            uint32_t  xfer_len = ((uint32_t) cdb[2] << 16) |
                                 ((uint32_t) cdb[3] << 8) |
                                  (uint32_t) cdb[4];

            tape_log(dev->log, "READ(6): fixed=%d, xfer_len=%u, block_size=%u, "
                     "filemark_pending=%d\n",
                     fixed, xfer_len, dev->block_size, dev->filemark_pending);

            /* Check for deferred filemark from a previous partial read. */
            if (dev->filemark_pending) {
                dev->filemark_pending = 0;
                tape_log(dev->log, "  deferred filemark, returning CHECK CONDITION\n");
                tape_filemark_detected(dev, xfer_len);
                return;
            }

            if (xfer_len == 0) {
                tape_set_phase(dev, SCSI_PHASE_STATUS);
                tape_command_complete(dev);
                break;
            }

            tape_set_phase(dev, SCSI_PHASE_DATA_IN);

            if (fixed) {
                /* Fixed block mode: read xfer_len blocks of block_size bytes.
                   SIMH records may be larger than block_size, so we re-block:
                   read whole SIMH records and split them into fixed blocks.
                   Unconsumed data is kept in dev->rec_buf for subsequent reads. */
                const uint32_t total_bytes = xfer_len * dev->block_size;
                tape_buf_alloc(dev, total_bytes);
                memset(dev->buffer, 0, total_bytes);

                uint32_t blocks_read   = 0;
                uint32_t buf_offset    = 0;
                int      hit_filemark  = 0;
                int      hit_eod       = 0;

                fseeko64(dev->drv->fp, (int64_t) dev->tape_pos, SEEK_SET);

                while (blocks_read < xfer_len) {
                    if (dev->rec_remaining == 0) {
                        /* Need to read the next SIMH record. */
                        uint32_t peek_len;
                        if (tape_read_marker(dev, &peek_len) < 0) {
                            hit_eod = 1;
                            break;
                        }
                        if (peek_len == TAPE_SIMH_FILEMARK) {
                            dev->tape_pos += 4;
                            hit_filemark = 1;
                            break;
                        }
                        if (peek_len == TAPE_SIMH_EOD || peek_len == TAPE_SIMH_GAP) {
                            hit_eod = 1;
                            break;
                        }

                        /* Allocate/reallocate record buffer if needed. */
                        if (peek_len > dev->rec_buf_size) {
                            free(dev->rec_buf);
                            dev->rec_buf      = (uint8_t *) malloc(peek_len);
                            dev->rec_buf_size = peek_len;
                        }

                        /* Read the record data. */
                        if (fread(dev->rec_buf, 1, peek_len, dev->drv->fp) != peek_len) {
                            hit_eod = 1;
                            break;
                        }

                        /* Read trailing marker. */
                        uint32_t trail;
                        if (tape_read_marker(dev, &trail) < 0) {
                            hit_eod = 1;
                            break;
                        }

                        dev->tape_pos += 4 + peek_len + 4;
                        dev->num_blocks++;
                        dev->bot = 0;

                        dev->rec_remaining = peek_len;
                        dev->rec_offset    = 0;

                        tape_log(dev->log, "  fixed read: got SIMH record %u bytes, "
                                 "splitting into %u-byte blocks, first 8: "
                                 "%02X %02X %02X %02X %02X %02X %02X %02X\n",
                                 peek_len, dev->block_size,
                                 dev->rec_buf[0], dev->rec_buf[1],
                                 dev->rec_buf[2], dev->rec_buf[3],
                                 dev->rec_buf[4], dev->rec_buf[5],
                                 dev->rec_buf[6], dev->rec_buf[7]);
                    }

                    /* Copy one block from the record buffer. */
                    uint32_t copy_len = (dev->rec_remaining >= dev->block_size) ?
                                         dev->block_size : dev->rec_remaining;
                    memcpy(dev->buffer + buf_offset,
                           dev->rec_buf + dev->rec_offset, copy_len);

                    buf_offset         += dev->block_size;
                    dev->rec_offset    += copy_len;
                    dev->rec_remaining -= copy_len;
                    blocks_read++;
                }

                /* Log data checksum for debugging. */
                {
                    uint32_t cksum = 0;
                    uint32_t bytes_total = blocks_read * dev->block_size;
                    for (uint32_t ci = 0; ci < bytes_total; ci++)
                        cksum += dev->buffer[ci];
                    tape_log(dev->log, "  fixed read: %u/%u blocks read, filemark=%d, "
                             "eod=%d, bytes=%u, cksum=0x%08X, rec_remaining=%u\n",
                             blocks_read, xfer_len, hit_filemark, hit_eod,
                             bytes_total, cksum, dev->rec_remaining);
                }

                if (hit_filemark) {
                    if (blocks_read > 0) {
                        /* Transfer the data we read with GOOD status, and defer
                           the filemark to the next read. This is how many real
                           tape drives work -- the host gets the data now and
                           discovers the filemark on the next read attempt. */
                        uint32_t bytes_read = blocks_read * dev->block_size;
                        tape_log(dev->log, "  filemark after %u blocks, "
                                 "deferring filemark to next read\n", blocks_read);
                        dev->filemark_pending = 1;
                        tape_set_buf_len(dev, BufLen, (int32_t *) &total_bytes);
                        tape_data_command_finish(dev, bytes_read, dev->block_size,
                                                 bytes_read, 0);
                        ui_sb_update_icon(SB_TAPE | dev->id, 1);
                    } else {
                        /* No data before filemark -- report it immediately. */
                        tape_filemark_detected(dev, xfer_len);
                        tape_buf_free(dev);
                    }
                    return;
                } else if (hit_eod) {
                    if (blocks_read > 0) {
                        uint32_t bytes_read = blocks_read * dev->block_size;
                        tape_log(dev->log, "  EOD after %u blocks, "
                                 "deferring EOD to next read\n", blocks_read);
                        dev->eot = 1;
                        tape_set_buf_len(dev, BufLen, (int32_t *) &total_bytes);
                        tape_data_command_finish(dev, bytes_read, dev->block_size,
                                                 bytes_read, 0);
                        ui_sb_update_icon(SB_TAPE | dev->id, 1);
                    } else {
                        tape_blank_check(dev, xfer_len);
                        tape_buf_free(dev);
                    }
                    return;
                }

                dev->packet_len       = total_bytes;
                dev->requested_blocks = xfer_len;
                tape_set_buf_len(dev, BufLen, (int32_t *) &dev->packet_len);
                tape_data_command_finish(dev, total_bytes, dev->block_size, total_bytes, 0);

                if (dev->packet_status != PHASE_COMPLETE)
                    ui_sb_update_icon(SB_TAPE | dev->id, 1);
                else
                    ui_sb_update_icon(SB_TAPE | dev->id, 0);
            } else {
                /* Variable block mode: read one record, xfer_len = max bytes. */
                tape_buf_alloc(dev, xfer_len);
                memset(dev->buffer, 0, xfer_len);

                fseeko64(dev->drv->fp, (int64_t) dev->tape_pos, SEEK_SET);

                int32_t rec_len = tape_simh_read_record(dev, dev->buffer, xfer_len);

                if (rec_len == 0) {
                    tape_filemark_detected(dev, 0);
                    tape_buf_free(dev);
                    return;
                } else if (rec_len < 0) {
                    tape_blank_check(dev, 0);
                    tape_buf_free(dev);
                    return;
                }

                int32_t actual_len = rec_len;
                if (actual_len > (int32_t) xfer_len)
                    actual_len = (int32_t) xfer_len;

                dev->packet_len       = actual_len;
                dev->requested_blocks = 1;
                tape_set_buf_len(dev, BufLen, &actual_len);
                tape_data_command_finish(dev, actual_len, actual_len, xfer_len, 0);

                if (dev->packet_status != PHASE_COMPLETE)
                    ui_sb_update_icon(SB_TAPE | dev->id, 1);
                else
                    ui_sb_update_icon(SB_TAPE | dev->id, 0);
            }
            break;
        }

        case GPCMD_WRITE_6: {
            /* Tape WRITE(6):
               CDB byte 1 bit 0: Fixed = 1 (fixed block), 0 (variable block)
               CDB bytes 2-4: Transfer length
             */
            if (dev->drv->read_only) {
                tape_write_protected(dev);
                return;
            }

            const int fixed = cdb[1] & 1;
            uint32_t  xfer_len = ((uint32_t) cdb[2] << 16) |
                                 ((uint32_t) cdb[3] << 8) |
                                  (uint32_t) cdb[4];

            tape_log(dev->log, "WRITE(6): fixed=%d, xfer_len=%u, block_size=%u\n",
                     fixed, xfer_len, dev->block_size);

            if (xfer_len == 0) {
                tape_set_phase(dev, SCSI_PHASE_STATUS);
                tape_command_complete(dev);
                break;
            }

            tape_set_phase(dev, SCSI_PHASE_DATA_OUT);

            if (fixed) {
                const uint32_t total_bytes = xfer_len * dev->block_size;
                tape_buf_alloc(dev, total_bytes);

                dev->requested_blocks = xfer_len;
                dev->sector_len       = xfer_len;
                dev->packet_len       = total_bytes;

                tape_set_buf_len(dev, BufLen, (int32_t *) &dev->packet_len);
                tape_data_command_finish(dev, total_bytes, dev->block_size, total_bytes, 1);

                ui_sb_update_icon_write(SB_TAPE | dev->id,
                                        dev->packet_status != PHASE_COMPLETE);
            } else {
                /* Variable: xfer_len is the number of bytes for one record. */
                tape_buf_alloc(dev, xfer_len);

                dev->requested_blocks = 1;
                dev->sector_len       = 1;
                dev->packet_len       = xfer_len;

                tape_set_buf_len(dev, BufLen, (int32_t *) &dev->packet_len);
                tape_data_command_finish(dev, xfer_len, xfer_len, xfer_len, 1);

                ui_sb_update_icon_write(SB_TAPE | dev->id,
                                        dev->packet_status != PHASE_COMPLETE);
            }
            break;
        }

        case GPCMD_WRITE_FILEMARKS_6: {
            /* WRITE FILEMARKS(6):
               CDB bytes 2-4: Number of filemarks to write.
             */
            if (dev->drv->read_only) {
                tape_write_protected(dev);
                return;
            }

            uint32_t count = ((uint32_t) cdb[2] << 16) |
                             ((uint32_t) cdb[3] << 8) |
                              (uint32_t) cdb[4];

            tape_log(dev->log, "WRITE FILEMARKS: count=%u\n", count);

            fseeko64(dev->drv->fp, (int64_t) dev->tape_pos, SEEK_SET);

            for (uint32_t i = 0; i < count; i++) {
                if (tape_simh_write_filemark(dev) < 0) {
                    tape_sense_key = SENSE_MEDIUM_ERROR;
                    tape_asc       = ASC_WRITE_ERROR;
                    tape_ascq      = 0;
                    tape_cmd_error(dev);
                    return;
                }
            }

            tape_set_phase(dev, SCSI_PHASE_STATUS);
            tape_command_complete(dev);
            break;
        }

        case GPCMD_SPACE_6: {
            /* SPACE(6):
               CDB byte 1 bits 2-0: Code
                 0 = Blocks
                 1 = Filemarks
                 3 = End-of-data
               CDB bytes 2-4: Count (signed 24-bit, negative = reverse)
             */
            const uint8_t code = cdb[1] & 0x07;
            int32_t count = ((int32_t) cdb[2] << 16) |
                            ((int32_t) cdb[3] << 8) |
                             (int32_t) cdb[4];

            /* Sign-extend 24-bit to 32-bit. */
            if (count & 0x800000)
                count |= (int32_t) 0xFF000000;

            tape_log(dev->log, "SPACE: code=%d, count=%d, filemark_pending=%d\n",
                     code, count, dev->filemark_pending);

            dev->rec_remaining = 0; /* Invalidate read-ahead buffer. */

            /* If a filemark was deferred from a previous read, the tape
               position is already past the filemark.  Account for it
               before doing the actual SPACE operation. */
            int had_pending_filemark = dev->filemark_pending;
            dev->filemark_pending    = 0;

            if (had_pending_filemark && code == 1 && count > 0) {
                /* The pending filemark counts as the first one. */
                count--;
                tape_log(dev->log, "  consumed pending filemark, remaining count=%d\n", count);
                if (count == 0) {
                    /* Already past the filemark -- nothing more to do. */
                    tape_set_phase(dev, SCSI_PHASE_STATUS);
                    tape_command_complete(dev);
                    break;
                }
            }

            fseeko64(dev->drv->fp, (int64_t) dev->tape_pos, SEEK_SET);

            switch (code) {
                case 0: /* Space blocks. */
                    if (count > 0) {
                        int32_t result = tape_space_blocks_forward(dev, count);
                        if (result < 0) {
                            if (dev->filemark_pending) {
                                dev->filemark_pending = 0;
                                tape_filemark_detected(dev, (uint32_t) (-result));
                            } else
                                tape_blank_check(dev, (uint32_t) (-result));
                            return;
                        }
                    } else if (count < 0) {
                        int32_t result = tape_space_blocks_backward(dev, -count);
                        if (result < 0) {
                            if (dev->filemark_pending) {
                                dev->filemark_pending = 0;
                                tape_filemark_detected(dev, (uint32_t) (-result));
                            } else
                                tape_bop_detected(dev, (uint32_t) (-result));
                            return;
                        }
                    }
                    break;
                case 1: /* Space filemarks. */
                    if (count > 0) {
                        int32_t result = tape_space_filemarks_forward(dev, count);
                        if (result < 0) {
                            tape_blank_check(dev, (uint32_t) (-result));
                            return;
                        }
                    } else if (count < 0) {
                        int32_t result = tape_space_filemarks_backward(dev, -count);
                        if (result < 0) {
                            tape_blank_check(dev, (uint32_t) (-result));
                            return;
                        }
                    }
                    break;
                case 3: /* Space to end-of-data. */
                    tape_space_to_eod(dev);
                    break;
                default:
                    tape_invalid_field(dev, cdb[1]);
                    return;
            }

            tape_set_phase(dev, SCSI_PHASE_STATUS);
            tape_command_complete(dev);
            break;
        }

        case GPCMD_ERASE_6:
            /* ERASE: truncate all data from current position. */
            if (dev->drv->read_only) {
                tape_write_protected(dev);
                return;
            }

            tape_log(dev->log, "ERASE\n");

            fseeko64(dev->drv->fp, (int64_t) dev->tape_pos, SEEK_SET);
            tape_write_marker(dev, TAPE_SIMH_EOD);
            fflush(dev->drv->fp);

            /* Truncate the file at the current position. */
#ifdef _WIN32
            {
                int fd = _fileno(dev->drv->fp);
                HANDLE fh = (HANDLE) _get_osfhandle(fd);
                LARGE_INTEGER li;
                li.QuadPart = (int64_t)(dev->tape_pos + 4);
                SetFilePointerEx(fh, li, NULL, FILE_BEGIN);
                SetEndOfFile(fh);
            }
#else
            {
                int fd = fileno(dev->drv->fp);
                if (ftruncate(fd, (off_t)(dev->tape_pos + 4)) != 0)
                    tape_log(dev->log, "Failed to truncate tape image\n");
            }
#endif
            /* Seek back to just before the EOD. */
            fseeko64(dev->drv->fp, (int64_t) dev->tape_pos, SEEK_SET);

            tape_set_phase(dev, SCSI_PHASE_STATUS);
            tape_command_complete(dev);
            break;

        case GPCMD_START_STOP_UNIT: /* 0x1B = LOAD/UNLOAD for tape. */
            tape_set_phase(dev, SCSI_PHASE_STATUS);

            if (cdb[4] & 1) {
                /* Load: if no tape is loaded, try to reload the previous image.
                   If a tape is already loaded, this is a no-op. */
                tape_log(dev->log, "LOAD\n");
                if (dev->drv->fp == NULL)
                    tape_reload(dev->id);
                tape_rewind(dev);
                /* Host explicitly loaded the tape, clear any pending
                   media change notification. */
                dev->unit_attention = 0;
                dev->transition     = 0;
            } else {
                /* Unload/eject. */
                tape_log(dev->log, "UNLOAD\n");
                tape_eject(dev->id);
            }

            tape_command_complete(dev);
            break;

        case GPCMD_INQUIRY:
            tape_set_phase(dev, SCSI_PHASE_DATA_IN);

            max_len = cdb[3];
            max_len <<= 8;
            max_len |= cdb[4];

            tape_buf_alloc(dev, 65536);

            if (cdb[1] & 1) {
                preamble_len = 4;
                size_idx     = 3;

                dev->buffer[idx++] = 0x01;     /* Sequential access */
                dev->buffer[idx++] = cdb[2];
                dev->buffer[idx++] = 0;

                idx++;

                switch (cdb[2]) {
                    case 0x00:
                        dev->buffer[idx++] = 0x00;
                        dev->buffer[idx++] = 0x80;
                        break;
                    case 0x80:
                        dev->buffer[idx++] = strlen("VCM!10") + 1;
                        ide_padstr8(dev->buffer + idx, 20, "VCM!10");
                        idx += strlen("VCM!10");
                        break;
                    default:
                        tape_log(dev->log, "INQUIRY: Invalid page: %02X\n", cdb[2]);
                        tape_invalid_field(dev, cdb[2]);
                        tape_buf_free(dev);
                        return;
                }
            } else {
                preamble_len = 5;
                size_idx     = 4;

                memset(dev->buffer, 0, 8);
                if ((cdb[1] & 0xe0) || ((dev->cur_lun > 0x00) && (dev->cur_lun < 0xff)))
                    dev->buffer[0] = 0x7f;    /* No physical device on this LUN */
                else
                    dev->buffer[0] = 0x01;    /* Sequential access */
                dev->buffer[1] = 0x80;        /* Removable */
                /* SCSI-2 compliant */
                // dev->buffer[2] = (dev->drv->bus_type == TAPE_BUS_SCSI) ? 0x02 : 0x00;
                // dev->buffer[3] = (dev->drv->bus_type == TAPE_BUS_SCSI) ? 0x02 : 0x21;
                dev->buffer[2] = 0x02;
                dev->buffer[3] = 0x02;
#if 0
                dev->buffer[4] = 31;
#endif
                dev->buffer[4] = 0;
                if (dev->drv->bus_type == TAPE_BUS_SCSI) {
                    dev->buffer[6] = 1;       /* 16-bit transfers supported */
                    dev->buffer[7] = 0x20;    /* Wide bus supported */
                }
                dev->buffer[7] |= 0x02;

                if (dev->drv->type > 0) {
                    ide_padstr8(dev->buffer + 8, 8,
                                tape_drive_types[dev->drv->type].vendor);
                    ide_padstr8(dev->buffer + 16, 16,
                                tape_drive_types[dev->drv->type].model);
                    ide_padstr8(dev->buffer + 32, 4,
                                tape_drive_types[dev->drv->type].revision);
                } else {
                    ide_padstr8(dev->buffer + 8, 8, EMU_NAME);
                    ide_padstr8(dev->buffer + 16, 16, device_identify);
                    ide_padstr8(dev->buffer + 32, 4, EMU_VERSION_EX);
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
            tape_set_buf_len(dev, BufLen, &len);

            tape_data_command_finish(dev, len, len, max_len, 0);
            break;

        case GPCMD_MODE_SENSE_6:
        case GPCMD_MODE_SENSE_10:
            tape_set_phase(dev, SCSI_PHASE_DATA_IN);

            // if (dev->drv->bus_type == TAPE_BUS_SCSI)
                block_desc = ((cdb[1] >> 3) & 1) ? 0 : 1;
            // else
                // block_desc = 0;

            if (cdb[0] == GPCMD_MODE_SENSE_6) {
                len = cdb[4];
                tape_buf_alloc(dev, 256);
            } else {
                len = (cdb[8] | (cdb[7] << 8));
                tape_buf_alloc(dev, 65536);
            }

            if ((cdb[2] & 0x3f) != 0 &&
                (cdb[2] & 0x3f) != GPMODE_ALL_PAGES &&
                !(tape_mode_sense_page_flags & (1LL << (uint64_t) (cdb[2] & 0x3f)))) {
                tape_invalid_field(dev, cdb[2]);
                tape_buf_free(dev);
                return;
            }

            memset(dev->buffer, 0, len);
            alloc_length = len;

            if (cdb[0] == GPCMD_MODE_SENSE_6) {
                len            = tape_mode_sense(dev, dev->buffer, 4, cdb[2], block_desc);
                len            = MIN(len, alloc_length);
                dev->buffer[0] = len - 1;
                dev->buffer[1] = 0; /* Medium type */
                dev->buffer[2] = dev->drv->read_only ? 0x80 : 0x00; /* WP bit */
                if (block_desc)
                    dev->buffer[3] = 8;
            } else {
                len            = tape_mode_sense(dev, dev->buffer, 8, cdb[2], block_desc);
                len            = MIN(len, alloc_length);
                dev->buffer[0] = (len - 2) >> 8;
                dev->buffer[1] = (len - 2) & 255;
                dev->buffer[2] = 0; /* Medium type */
                dev->buffer[3] = dev->drv->read_only ? 0x80 : 0x00; /* WP bit */
                if (block_desc) {
                    dev->buffer[6] = 0;
                    dev->buffer[7] = 8;
                }
            }

            tape_set_buf_len(dev, BufLen, &len);

            tape_log(dev->log, "Reading mode page: %02X...\n", cdb[2]);

            tape_data_command_finish(dev, len, len, alloc_length, 0);
            return;

        case GPCMD_MODE_SELECT_6:
        case GPCMD_MODE_SELECT_10:
            tape_set_phase(dev, SCSI_PHASE_DATA_OUT);

            if (cdb[0] == GPCMD_MODE_SELECT_6) {
                len = cdb[4];
                tape_buf_alloc(dev, 256);
            } else {
                len = (cdb[7] << 8) | cdb[8];
                tape_buf_alloc(dev, 65536);
            }

            tape_set_buf_len(dev, BufLen, &len);

            dev->total_length = len;
            dev->do_page_save = cdb[1] & 1;

            tape_data_command_finish(dev, len, len, len, 1);
            return;

        case GPCMD_READ_POSITION: {
            /* READ POSITION: return 20 bytes of position data. */
            tape_set_phase(dev, SCSI_PHASE_DATA_IN);

            tape_buf_alloc(dev, 20);
            memset(dev->buffer, 0, 20);

            /* Byte 0: BOP / EOP / BPU flags. */
            if (dev->bot)
                dev->buffer[0] |= 0x80; /* BOP */
            if (dev->eot)
                dev->buffer[0] |= 0x40; /* EOP */

            /* Bytes 4-7: First block location (big-endian). */
            dev->buffer[4] = (dev->num_blocks >> 24) & 0xff;
            dev->buffer[5] = (dev->num_blocks >> 16) & 0xff;
            dev->buffer[6] = (dev->num_blocks >> 8) & 0xff;
            dev->buffer[7] = dev->num_blocks & 0xff;

            /* Bytes 8-11: Last block location (same as first for us). */
            dev->buffer[8]  = dev->buffer[4];
            dev->buffer[9]  = dev->buffer[5];
            dev->buffer[10] = dev->buffer[6];
            dev->buffer[11] = dev->buffer[7];

            len = 20;
            tape_set_buf_len(dev, BufLen, &len);

            tape_data_command_finish(dev, len, len, len, 0);
            break;
        }

        default:
            tape_illegal_opcode(dev, cdb[0]);
            break;
    }

    if ((dev->packet_status == PHASE_COMPLETE) || (dev->packet_status == PHASE_ERROR))
        tape_buf_free(dev);
}

static void
tape_command_stop(scsi_common_t *sc)
{
    tape_t *dev = (tape_t *) sc;

    tape_command_complete(dev);
    tape_buf_free(dev);
}

static uint8_t
tape_phase_data_out(scsi_common_t *sc)
{
    tape_t        *dev            = (tape_t *) sc;
    uint16_t       block_desc_len;
    uint16_t       pos;
    uint16_t       param_list_len;
    uint8_t        hdr_len;
    uint8_t        val;
    uint8_t        error          = 0;

    switch (dev->current_cdb[0]) {
        case GPCMD_WRITE_6: {
            const int fixed = dev->current_cdb[1] & 1;
            uint32_t  xfer_len = ((uint32_t) dev->current_cdb[2] << 16) |
                                 ((uint32_t) dev->current_cdb[3] << 8) |
                                  (uint32_t) dev->current_cdb[4];

            fseeko64(dev->drv->fp, (int64_t) dev->tape_pos, SEEK_SET);

            if (fixed) {
                /* Write xfer_len blocks of block_size bytes each. */
                for (uint32_t i = 0; i < xfer_len; i++) {
                    if (tape_simh_write_record(dev, dev->buffer + (i * dev->block_size),
                                               dev->block_size) < 0) {
                        tape_sense_key = SENSE_MEDIUM_ERROR;
                        tape_asc       = ASC_WRITE_ERROR;
                        tape_ascq      = 0;
                        tape_cmd_error(dev);
                        tape_buf_free(dev);
                        return 0;
                    }
                }
            } else {
                /* Variable: write one record of xfer_len bytes. */
                if (tape_simh_write_record(dev, dev->buffer, xfer_len) < 0) {
                    tape_sense_key = SENSE_MEDIUM_ERROR;
                    tape_asc       = ASC_WRITE_ERROR;
                    tape_ascq      = 0;
                    tape_cmd_error(dev);
                    tape_buf_free(dev);
                    return 0;
                }
            }

            ui_sb_update_icon_write(SB_TAPE | dev->id, 0);
            break;
        }

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

            // if (dev->drv->bus_type == TAPE_BUS_SCSI) {
                if (dev->current_cdb[0] == GPCMD_MODE_SELECT_6) {
                    block_desc_len = dev->buffer[2];
                    block_desc_len <<= 8;
                   block_desc_len |= dev->buffer[3];
                } else {
                    block_desc_len = dev->buffer[6];
                    block_desc_len <<= 8;
                    block_desc_len |= dev->buffer[7];
                }
            // } else
                // block_desc_len = 0;

            /* If there's a block descriptor, parse the block size from it. */
            if (block_desc_len >= 8) {
                uint32_t new_block_size = ((uint32_t) dev->buffer[hdr_len + 5] << 16) |
                                          ((uint32_t) dev->buffer[hdr_len + 6] << 8) |
                                           (uint32_t) dev->buffer[hdr_len + 7];
                if (new_block_size > 0)
                    dev->block_size = new_block_size;
                else
                    dev->block_size = 0; /* Variable block mode. */
                tape_log(dev->log, "MODE SELECT: block size set to %u\n", dev->block_size);
            }

            pos = hdr_len + block_desc_len;

            while (1) {
                if (pos >= param_list_len) {
                    tape_log(dev->log, "Buffer has only block descriptor\n");
                    break;
                }

                const uint8_t page     = dev->buffer[pos] & 0x3F;
                const uint8_t page_len = dev->buffer[pos + 1];

                pos += 2;

                if (!(tape_mode_sense_page_flags & (1LL << ((uint64_t) page))))
                    error |= 1;
                else for (uint8_t i = 0; i < page_len; i++) {
                    const uint8_t ch      = tape_mode_sense_pages_changeable.pages[page][i + 2];
                    const uint8_t old_val = dev->ms_pages_saved.pages[page][i + 2];
                    val                   = dev->buffer[pos + i];
                    if (val != old_val) {
                        if (ch)
                            dev->ms_pages_saved.pages[page][i + 2] = val;
                        else {
                            error |= 1;
                            tape_invalid_field_pl(dev, val);
                        }
                    }
                }

                pos += page_len;

                val = tape_mode_sense_pages_default_scsi.pages[page][0] & 0x80;
                if (dev->do_page_save && val)
                    tape_mode_sense_save(dev);

                if (pos >= dev->total_length)
                    break;
            }

            if (error) {
                tape_buf_free(dev);
                return 0;
            }
            break;

        default:
            break;
    }

    tape_command_stop((scsi_common_t *) dev);
    return 1;
}

static int
tape_get_max(UNUSED(const ide_t *ide), int ide_has_dma, const int type)
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
tape_get_timings(UNUSED(const ide_t *ide), const int ide_has_dma, const int type)
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

/*
   Fill in ide->buffer with the output of the "IDENTIFY PACKET DEVICE" command
 */
static void
tape_identify(const ide_t *ide, const int ide_has_dma)
{
    const tape_t *dev                = (tape_t *) ide->sc;
    char          model[40];

    memset(model, 0, 40);

    /* ATAPI device, sequential access device, removable media, accelerated DRQ */
    ide->buffer[0] = 0x8000 | (1 << 8) | 0x80 | (2 << 5);
    ide_padstr((char *) (ide->buffer + 10), "", 20);               /* Serial Number */

    if (tape_drives[dev->id].type > 0) {
        snprintf(model, 40, "%s %s", tape_drive_types[tape_drives[dev->id].type].vendor,
                 tape_drive_types[tape_drives[dev->id].type].model);
        /* Firmware */
        ide_padstr((char *) (ide->buffer + 23),
                   tape_drive_types[tape_drives[dev->id].type].revision, 8);
        ide_padstr((char *) (ide->buffer + 27), model, 40);                                          /* Model */
    } else {
        snprintf(model, 40, "%s %s%02i", EMU_NAME, "86B_TP", dev->id);
        ide_padstr((char *) (ide->buffer + 23), EMU_VERSION_EX, 8);    /* Firmware */
        ide_padstr((char *) (ide->buffer + 27), model, 40);               /* Model */
    }

    ide->buffer[49]  = 0x200;                                            /* LBA supported */
    /* Interpret zero byte count limit as maximum length. */
    ide->buffer[126] = 0xfffe;

    if (ide_has_dma) {
        ide->buffer[71] = 30;
        ide->buffer[72] = 30;
        ide->buffer[80] = 0x7e; /*ATA-1 to ATA-6 supported*/
        ide->buffer[81] = 0x19; /*ATA-6 revision 3a supported*/
    }
}

/* Perform a master init on the entire module. */
void
tape_global_init(void)
{
    memset(tape_drives, 0x00, sizeof(tape_drives));
}

static void
tape_drive_reset(const int c)
{
    const uint8_t scsi_bus = (tape_drives[c].scsi_device_id >> 4) & 0x0f;
    const uint8_t scsi_id  = tape_drives[c].scsi_device_id & 0x0f;

    if (tape_drives[c].priv == NULL) {
        tape_drives[c].priv = (tape_t *) calloc(1, sizeof(tape_t));
        tape_t *dev         = (tape_t *) tape_drives[c].priv;

        char n[1024]        = { 0 };

        sprintf(n, "Tape %i", c + 1);
        dev->log            = log_open(n);
    }

    tape_t *dev  = (tape_t *) tape_drives[c].priv;

    dev->id      = c;
    dev->cur_lun = SCSI_LUN_USE_CDB;

    if (tape_drives[c].bus_type == TAPE_BUS_SCSI) {
        if (dev->tf == NULL)
            dev->tf        = (ide_tf_t *) calloc(1, sizeof(ide_tf_t));

        /* SCSI tape, attach to the SCSI bus. */
        scsi_device_t *sd  = &scsi_devices[scsi_bus][scsi_id];

        sd->sc             = (scsi_common_t *) dev;
        sd->command        = tape_command;
        sd->request_sense  = tape_request_sense_for_scsi;
        sd->reset          = tape_reset;
        sd->phase_data_out = tape_phase_data_out;
        sd->command_stop   = tape_command_stop;
        sd->type           = SCSI_REMOVABLE_TAPE;

        tape_log(dev->log, "SCSI Tape drive %i attached to SCSI ID %i\n",
                 c, tape_drives[c].scsi_device_id);
    } else if (tape_drives[c].bus_type == TAPE_BUS_ATAPI) {
        /* ATAPI tape, attach to the IDE bus. */
        ide_t *id = ide_get_drive(tape_drives[c].ide_channel);

        /*
           If the IDE channel is initialized, we attach to it,
           otherwise, we do nothing - it's going to be a drive
           that's not attached to anything.
         */
        if (id) {
            dev = (tape_t *) tape_drives[c].priv;

            id->sc               = (scsi_common_t *) dev;
            dev->tf              = id->tf;
            IDE_ATAPI_IS_EARLY   = 0;
            id->get_max          = tape_get_max;
            id->get_timings      = tape_get_timings;
            id->identify         = tape_identify;
            id->stop             = NULL;
            id->packet_command   = tape_command;
            id->device_reset     = tape_reset;
            id->phase_data_out   = tape_phase_data_out;
            id->command_stop     = tape_command_stop;
            id->bus_master_error = tape_bus_master_error;
            id->interrupt_drq    = 0;

            ide_atapi_attach(id);

            tape_log(dev->log, "ATAPI tape drive %i attached to IDE channel %i\n",
                     c, tape_drives[c].ide_channel);
        }
    }
}

void
tape_hard_reset(void)
{
    for (uint8_t c = 0; c < TAPE_NUM; c++) {
        if (tape_drives[c].bus_type == TAPE_BUS_SCSI) {
            const uint8_t scsi_bus = (tape_drives[c].scsi_device_id >> 4) & 0x0f;
            const uint8_t scsi_id  = tape_drives[c].scsi_device_id & 0x0f;

            if (scsi_bus >= SCSI_BUS_MAX)
                continue;

            if (scsi_id >= SCSI_ID_MAX)
                continue;
        } else if (tape_drives[c].bus_type == TAPE_BUS_ATAPI) {
            /* ATAPI tape, attach to the IDE bus. */
            ide_t *id = ide_get_drive(tape_drives[c].ide_channel);

            if (id == NULL)
                continue;
        } else
            continue;

        tape_drive_reset(c);
        tape_t *dev = (tape_t *) tape_drives[c].priv;

        tape_log(dev->log, "Tape hard_reset drive=%d\n", c);

        if (dev->tf == NULL)
            continue;

        dev->id  = c;
        dev->drv = &tape_drives[c];

        tape_init(dev);

        if (strlen(tape_drives[c].image_path))
            tape_load(dev, tape_drives[c].image_path, 0);

        /* Tape is already present at boot -- no surprise media change. */
        dev->unit_attention = 0;
        dev->transition     = 0;

        tape_mode_sense_load(dev);
    }
}

void
tape_close(void)
{
    for (uint8_t c = 0; c < TAPE_NUM; c++) {
        if ((tape_drives[c].bus_type == TAPE_BUS_SCSI) || (tape_drives[c].bus_type == TAPE_BUS_ATAPI)) {
            if (tape_drives[c].bus_type == TAPE_BUS_SCSI) {
                const uint8_t scsi_bus = (tape_drives[c].scsi_device_id >> 4) & 0x0f;
                const uint8_t scsi_id  = tape_drives[c].scsi_device_id & 0x0f;

                memset(&scsi_devices[scsi_bus][scsi_id], 0x00, sizeof(scsi_device_t));
            }

            tape_t *dev = (tape_t *) tape_drives[c].priv;

            if (dev) {
                tape_disk_unload(dev);

                if (dev->rec_buf)
                    free(dev->rec_buf);

                if (dev->tf)
                    free(dev->tf);

                if (dev->log != NULL) {
                    tape_log(dev->log, "Log closed\n");
                    log_close(dev->log);
                    dev->log = NULL;
                }

                free(dev);
                tape_drives[c].priv = NULL;
            }
        }

#if defined(ENABLE_TAPE_LOG) && defined(TAPE_FILE_LOG)
        if (tape_log_file) {
            fprintf(tape_log_file, "=== Tape debug log closed ===\n");
            fclose(tape_log_file);
            tape_log_file = NULL;
        }
#endif
    }
}
