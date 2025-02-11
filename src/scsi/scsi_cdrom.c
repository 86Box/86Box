/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the CD-ROM drive with SCSI(-like)
 *          commands, for both ATAPI and SCSI usage.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2025 Miran Grca.
 */
#include <inttypes.h>
#include <math.h>
#ifdef ENABLE_SCSI_CDROM_LOG
#include <stdarg.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/cdrom.h>
#include <86box/device.h>
#include <86box/log.h>
#include <86box/machine.h>
#include <86box/nvr.h>
#include <86box/plat.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/hdc_ide.h>
#include <86box/scsi_cdrom.h>
#include <86box/ui.h>

#define IDE_ATAPI_IS_EARLY             id->sc->pad0

#pragma pack(push, 1)
typedef struct gesn_cdb_t {
    uint8_t  opcode;
    uint8_t  polled;
    uint8_t  reserved2[2];
    uint8_t  class;
    uint8_t  reserved3[2];
    uint16_t len;
    uint8_t  control;
} gesn_cdb_t;

typedef struct gesn_event_header_t {
    uint16_t len;
    uint8_t  notification_class;
    uint8_t  supported_events;
} gesn_event_header_t;
#pragma pack(pop)

// clang-format off
/*
   Table of all SCSI commands and their flags, needed for the new disc change /
   not ready handler.
 */
uint8_t scsi_cdrom_command_flags[0x100] = {
    [0x00]          = IMPLEMENTED | CHECK_READY,
    [0x01]          = IMPLEMENTED | ALLOW_UA | SCSI_ONLY,
    [0x03]          = IMPLEMENTED | ALLOW_UA,
    [0x08]          = IMPLEMENTED | CHECK_READY,
    [0x0b]          = IMPLEMENTED | CHECK_READY,
    [0x0d]          = IMPLEMENTED | CHECK_READY | SCSI_ONLY,
    [0x12]          = IMPLEMENTED | ALLOW_UA,
    [0x13]          = IMPLEMENTED | CHECK_READY | SCSI_ONLY,
    [0x15]          = IMPLEMENTED,
    [0x1a]          = IMPLEMENTED,
    [0x1b]          = IMPLEMENTED | CHECK_READY,
    [0x1e]          = IMPLEMENTED | CHECK_READY,
    [0x22]          = IMPLEMENTED | CHECK_READY | SCSI_ONLY,
    [0x25]          = IMPLEMENTED | CHECK_READY,
    [0x26]          = IMPLEMENTED | CHECK_READY | SCSI_ONLY,
    [0x28]          = IMPLEMENTED | CHECK_READY,
    [0x2b]          = IMPLEMENTED | CHECK_READY,
    [0x2f]          = IMPLEMENTED | CHECK_READY | SCSI_ONLY,
    [0x42]          = IMPLEMENTED | CHECK_READY,
    /*
       Read TOC/PMA/ATIP - can get through UNIT_ATTENTION, per VIDE-CDD.SYS.

       NOTE: The ATAPI reference says otherwise, but I think this is a question of
             interpreting things right - the UNIT ATTENTION condition we have here
             is a tradition from not ready to ready, by definition the drive
             eventually becomes ready, make the condition go away.
     */
    [0x43 ... 0x45] = IMPLEMENTED | CHECK_READY,
    [0x46]            IMPLEMENTED | ALLOW_UA,
    [0x47 ... 0x49] = IMPLEMENTED | CHECK_READY,
    [0x4a]          = IMPLEMENTED | ALLOW_UA,
    [0x4b]          = IMPLEMENTED | CHECK_READY,
    [0x4e]          = IMPLEMENTED | CHECK_READY,
    [0x51 ... 0x52] = IMPLEMENTED | CHECK_READY,
    [0x55]          = IMPLEMENTED,
    [0x5a]          = IMPLEMENTED,
    [0xa5]          = IMPLEMENTED | CHECK_READY,
    [0xa8 ... 0xa9] = IMPLEMENTED | CHECK_READY,
    [0xad]          = IMPLEMENTED,
    [0xaf]          = IMPLEMENTED | CHECK_READY | SCSI_ONLY,
    [0xb4]          = IMPLEMENTED | CHECK_READY | ATAPI_ONLY,
    [0xb8]          = IMPLEMENTED | CHECK_READY | ATAPI_ONLY,
    [0xb9 ... 0xba] = IMPLEMENTED | CHECK_READY,
    [0xbb]          = IMPLEMENTED,
    [0xbc]          = IMPLEMENTED | CHECK_READY,
    [0xbd]          = IMPLEMENTED,
    [0xbe ... 0xbf] = IMPLEMENTED | CHECK_READY,
    [0xc0 ... 0xcd] = IMPLEMENTED | CHECK_READY | SCSI_ONLY,
    [0xd8 ... 0xde] = IMPLEMENTED | CHECK_READY | SCSI_ONLY,
    [0xe0 ... 0xe1] = IMPLEMENTED | CHECK_READY | SCSI_ONLY,
    [0xe3 ... 0xe9] = IMPLEMENTED | CHECK_READY | SCSI_ONLY,
    [0xeb]          = IMPLEMENTED | CHECK_READY | SCSI_ONLY,
    [0xed ... 0xee] = IMPLEMENTED | CHECK_READY | SCSI_ONLY
};

static uint64_t scsi_cdrom_ms_page_flags           = (GPMODEP_R_W_ERROR_PAGE | GPMODEP_CDROM_PAGE |
                                                      GPMODEP_CDROM_AUDIO_PAGE | (1ULL << 0x0fULL) |
                                                      GPMODEP_CAPABILITIES_PAGE | GPMODEP_ALL_PAGES);
static uint64_t scsi_cdrom_ms_page_flags_scsi      = (GPMODEP_UNIT_ATN_PAGE | GPMODEP_R_W_ERROR_PAGE |
                                                      GPMODEP_DISCONNECT_PAGE | GPMODEP_FORMAT_DEVICE_PAGE |
                                                      GPMODEP_CDROM_PAGE | GPMODEP_CDROM_AUDIO_PAGE |
                                                      (1ULL << 0x0fULL) | GPMODEP_CAPABILITIES_PAGE |
                                                      GPMODEP_ALL_PAGES);
static uint64_t scsi_cdrom_ms_page_flags_sony_scsi = (GPMODEP_R_W_ERROR_PAGE | GPMODEP_DISCONNECT_PAGE |
                                                      GPMODEP_CDROM_PAGE_SONY | GPMODEP_CDROM_AUDIO_PAGE_SONY |
                                                      (1ULL << 0x0fULL) | GPMODEP_CAPABILITIES_PAGE |
                                                      GPMODEP_ALL_PAGES);

static uint64_t scsi_cdrom_drive_status_page_flags    = ((1ULL << 0x01ULL) | (1ULL << 0x02ULL) |
                                                         (1ULL << 0x0fULL) | GPMODEP_ALL_PAGES);

static const mode_sense_pages_t scsi_cdrom_drive_status_pages = {
    { [0x01] = { 0x01, 0x00, 0x02, 0x0f, 0xbf },    /* Drive Status Data Format */
      [0x02] = { 0x02, 0x00, 0x01, 0x00       } }   /* Audio Play Status Format */
};

static const mode_sense_pages_t scsi_cdrom_ms_pages_default = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x06, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00 },
      [0x0d] = { GPMODE_CDROM_PAGE,                   0x06, 0x00, 0x01, 0x00, 0x3c, 0x00, 0x4b },
      [0x0e] = { GPMODE_CDROM_AUDIO_PAGE | 0x80,      0x0e, 0x04, 0x00, 0x00, 0x00, 0x00, 0x4b,
                 0x01,                                0xff, 0x02, 0xff, 0x00, 0x00, 0x00, 0x00 },
      [0x0f] = { 0x0f,                                0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00             },
      [0x2a] = { GPMODE_CAPABILITIES_PAGE,            0x12, 0x07, 0x00, 0x7f, 0x01, 0x0d, 0x03,
                 0x02,                                0xc2, 0x01, 0x00, 0x00, 0x00, 0x02, 0xc2,
                 0x00,                                0x00, 0x00, 0x00                         } }
};

static const mode_sense_pages_t scsi_cdrom_ms_pages_default_scsi = {
    { [0x00] = { GPMODE_UNIT_ATN_PAGE,                0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },     /* Guesswork */
      [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x06, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00 },
      [0x02] = { GPMODE_DISCONNECT_PAGE,              0x0e, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x03] = { GPMODE_FORMAT_DEVICE_PAGE,           0x16, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01,
                 0x00,                                0x01, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x0d] = { GPMODE_CDROM_PAGE,                   0x06, 0x00, 0x01, 0x00, 0x3c, 0x00, 0x4b },
      [0x0e] = { GPMODE_CDROM_AUDIO_PAGE | 0x80,      0x0e, 0x05, 0x04, 0x00, 0x80, 0x00, 0x4b,
                 0x01,                                0xff, 0x02, 0xff, 0x00, 0x00, 0x00, 0x00 },
      [0x0f] = { 0x0f,                                0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00             },
      [0x2a] = { GPMODE_CAPABILITIES_PAGE,            0x12, 0x07, 0x00, 0x7f, 0x01, 0x0d, 0x03,
                 0x02,                                0xc2, 0x01, 0x00, 0x00, 0x00, 0x02, 0xc2,
                 0x00,                                0x00, 0x00, 0x00                         } }
};

static const mode_sense_pages_t scsi_cdrom_ms_pages_default_sony_scsi = {
    { { 0, 0 },
      [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x06, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00 },
      [0x02] = { GPMODE_DISCONNECT_PAGE,              0x0e, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x08] = { GPMODE_CDROM_PAGE_SONY,              0x02, 0x00, 0x05                         },
      [0x09] = { GPMODE_CDROM_AUDIO_PAGE_SONY | 0x80, 0x0e, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x01,                                0xff, 0x02, 0xff, 0x00, 0x00, 0x00, 0x00 },
      [0x0d] = { GPMODE_CDROM_PAGE,                   0x06, 0x00, 0x01, 0x00, 0x3c, 0x00, 0x4b },
      [0x0e] = { GPMODE_CDROM_AUDIO_PAGE | 0x80,      0x0e, 0x05, 0x04, 0x00, 0x80, 0x00, 0x4b,
                 0x01,                                0xff, 0x02, 0xff, 0x00, 0x00, 0x00, 0x00 },
      [0x0f] = { 0x0f,                                0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00             },
      [0x2a] = { GPMODE_CAPABILITIES_PAGE,            0x12, 0x07, 0x00, 0x7f, 0x01, 0x0d, 0x03,
                 0x02,                                0xc2, 0x01, 0x00, 0x00, 0x00, 0x02, 0xc2,
                 0x00,                                0x00, 0x00, 0x00                         } }
};

static const mode_sense_pages_t scsi_cdrom_ms_pages_changeable = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x06, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 },
      [0x02] = { GPMODE_DISCONNECT_PAGE,              0x0e, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 },
      [0x0d] = { GPMODE_CDROM_PAGE,                   0x06, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
      [0x0e] = { GPMODE_CDROM_AUDIO_PAGE | 0x80,      0x0e, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                 0xff,                                0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
      [0x0f] = { 0x0f,                                0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00             },
      [0x2a] = { GPMODE_CAPABILITIES_PAGE,            0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                      0x00, 0x00, 0x00, 0x00, 0x00             } }
};

static const mode_sense_pages_t scsi_cdrom_ms_pages_changeable_scsi = {
    { [0x00] = { GPMODE_UNIT_ATN_PAGE,                0x06, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },    /* Guesswork */
      [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x06, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 },
      [0x02] = { GPMODE_DISCONNECT_PAGE,              0x0e, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 },
      [0x03] = { GPMODE_FORMAT_DEVICE_PAGE,           0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x0d] = { GPMODE_CDROM_PAGE,                   0x06, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
      [0x0e] = { GPMODE_CDROM_AUDIO_PAGE | 0x80,      0x0e, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                 0xff,                                0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
      [0x0f] = { 0x0f,                                0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00             },
      [0x2a] = { GPMODE_CAPABILITIES_PAGE,            0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                      0x00, 0x00, 0x00, 0x00, 0x00             } }
};

static const mode_sense_pages_t scsi_cdrom_ms_pages_changeable_sony_scsi = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE,               0x06, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 },
      [0x02] = { GPMODE_DISCONNECT_PAGE,              0x0e, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 },
      [0x08] = { GPMODE_CDROM_PAGE_SONY,              0x02, 0xff, 0xff },
      [0x09] = { GPMODE_CDROM_AUDIO_PAGE_SONY | 0x80, 0x0e, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                 0xff,                                0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
      [0x0d] = { GPMODE_CDROM_PAGE,                   0x06, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
      [0x0e] = { GPMODE_CDROM_AUDIO_PAGE | 0x80,      0x0e, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                 0xff,                                0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
      [0x0f] = { 0x0f,                                0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00             },
      [0x2a] = { GPMODE_CAPABILITIES_PAGE,            0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00,                                0x00, 0x00, 0x00                         } }
};
// clang-format on

static gesn_cdb_t          *gesn_cdb;
static gesn_event_header_t *gesn_event_header;

static void scsi_cdrom_command_complete(scsi_cdrom_t *dev);

static void scsi_cdrom_mode_sense_load(scsi_cdrom_t *dev);
static void scsi_cdrom_drive_status_load(scsi_cdrom_t *dev);

static void scsi_cdrom_init(scsi_cdrom_t *dev);

#ifdef ENABLE_SCSI_CDROM_LOG
int scsi_cdrom_do_log = ENABLE_SCSI_CDROM_LOG;

static void
scsi_cdrom_log(void *priv, const char *format, ...)
{
    if (scsi_cdrom_do_log) {
        va_list ap;
        va_start(ap, format);
        log_out(priv, format, ap);
        va_end(ap);
    }
}
#else
#    define scsi_cdrom_log(priv, format, ...)
#endif

static void
scsi_cdrom_set_callback(const scsi_cdrom_t *dev)
{
    if (dev && dev->drv && (dev->drv->bus_type != CDROM_BUS_SCSI))
        ide_set_callback(ide_drives[dev->drv->ide_channel], dev->callback);
}

static void
scsi_cdrom_init(scsi_cdrom_t *dev)
{
    if (dev != NULL) {
        /* Do a reset (which will also rezero it). */
        scsi_cdrom_reset((scsi_common_t *) dev);

        /* Configure the drive. */
        dev->requested_blocks = 1;

        dev->drv->bus_mode = 0;
        if (dev->drv->bus_type >= CDROM_BUS_ATAPI)
            dev->drv->bus_mode |= 2;
        if (dev->drv->bus_type < CDROM_BUS_SCSI)
            dev->drv->bus_mode |= 1;
        scsi_cdrom_log(dev->log, "Bus type %i, bus mode %i\n",
                       dev->drv->bus_type, dev->drv->bus_mode);

        dev->sense[0] = 0xf0;
        dev->sense[7] = 10;
        /* NEC only */
        if (dev->drv->is_early)
            dev->tf->status = READY_STAT | DSC_STAT;
        else
            dev->tf->status = 0;
        dev->tf->pos         = 0;
        dev->packet_status   = PHASE_NONE;
        scsi_cdrom_sense_key = scsi_cdrom_asc = scsi_cdrom_ascq = dev->unit_attention = 0;
        scsi_cdrom_info      = 0x00000000;
        dev->drv->cd_status &= ~CD_STATUS_TRANSITION;
        dev->drv->cur_speed  = dev->drv->real_speed;
        scsi_cdrom_mode_sense_load(dev);

        const char *vendor = cdrom_get_vendor(dev->drv->type);

        if ((dev->drv->bus_type == CDROM_BUS_SCSI) && !strcmp(vendor, "PIONEER"))
            scsi_cdrom_drive_status_load(dev);
    }
}

/* Returns: 0 for none, 1 for PIO, 2 for DMA. */
static int
scsi_cdrom_current_mode(const scsi_cdrom_t *dev)
{
    if (dev->drv->bus_type == CDROM_BUS_SCSI)
        return 2;
    else if (dev->drv->bus_type == CDROM_BUS_ATAPI) {
        scsi_cdrom_log(dev->log, "ATAPI drive, setting to %s\n",
                       (dev->tf->features & 1) ? "DMA" : "PIO",
                       dev->id);
        return (dev->tf->features & 1) ? 2 : 1;
    }

    return 0;
}

static uint32_t
scsi_cdrom_get_channel(void *priv, const int channel)
{
    const scsi_cdrom_t *dev = (scsi_cdrom_t *) priv;
    uint32_t ret = channel + 1;

    if (dev != NULL)
        ret = dev->ms_pages_saved.pages[GPMODE_CDROM_AUDIO_PAGE][channel ? 10 : 8];

    return ret;
}

static uint32_t
scsi_cdrom_get_volume(void *priv, const int channel)
{
    const scsi_cdrom_t *dev = (scsi_cdrom_t *) priv;
    uint32_t ret = 255;

    if (dev != NULL)
        ret = dev->ms_pages_saved.pages[GPMODE_CDROM_AUDIO_PAGE][channel ? 11 : 9];

    return ret;
}

static void
scsi_cdrom_mode_sense_load(scsi_cdrom_t *dev)
{
    char  file_name[512] = { 0 };

    memset(&dev->ms_pages_saved, 0x00, sizeof(mode_sense_pages_t));
    memcpy(&dev->ms_pages_saved, &dev->ms_pages_default,
           sizeof(mode_sense_pages_t));

    if (dev->drv->bus_type == CDROM_BUS_SCSI)
        sprintf(file_name, "scsi_cdrom_%02i_mode_sense_bin", dev->id);
    else
        sprintf(file_name, "cdrom_%02i_mode_sense_bin", dev->id);
    FILE *fp = plat_fopen(nvr_path(file_name), "rb");
    if (fp) {
        if (fread(dev->ms_pages_saved.pages[GPMODE_CDROM_AUDIO_PAGE], 1, 0x10, fp) != 0x10)
            log_fatal(dev->log, "scsi_cdrom_mode_sense_load(): Error reading data\n");
        (void) fread(dev->ms_pages_saved.pages[GPMODE_CDROM_AUDIO_PAGE_SONY], 1,
                     0x10, fp);
        fclose(fp);
    }
}

static void
scsi_cdrom_mode_sense_save(const scsi_cdrom_t *dev)
{
    char  file_name[512] = { 0 };

    if (dev->drv->bus_type == CDROM_BUS_SCSI)
        sprintf(file_name, "scsi_cdrom_%02i_mode_sense_bin", dev->id);
    else
        sprintf(file_name, "cdrom_%02i_mode_sense_bin", dev->id);
    FILE *fp = plat_fopen(nvr_path(file_name), "wb");
    if (fp) {
        fwrite(dev->ms_pages_saved.pages[GPMODE_CDROM_AUDIO_PAGE], 1, 0x10, fp);
        fwrite(dev->ms_pages_saved.pages[GPMODE_CDROM_AUDIO_PAGE_SONY], 1, 0x10, fp);
        fclose(fp);
    }
}

/* SCSI Drive Status (Pioneer only). */
static void
scsi_cdrom_drive_status_load(scsi_cdrom_t *dev)
{
    memset(&dev->ms_drive_status_pages_saved, 0x00, sizeof(mode_sense_pages_t));
    memcpy(&dev->ms_drive_status_pages_saved, &scsi_cdrom_drive_status_pages,
           sizeof(mode_sense_pages_t));
}

static uint8_t
scsi_cdrom_drive_status_read(const scsi_cdrom_t *dev, const uint8_t page,
                             const uint8_t pos)
{
    return dev->ms_drive_status_pages_saved.pages[page][pos];
}

static uint32_t
scsi_cdrom_drive_status(const scsi_cdrom_t *dev, uint8_t *buf, uint8_t page)
{
    uint32_t      pos   = 0;

    page &= 0x3f;

    for (uint8_t i = 0; i < 0x40; i++) {
        if ((page == i) && (scsi_cdrom_drive_status_page_flags &
                            (1LL << ((uint64_t) (page & 0x3f))))) {
            buf[pos++]    = scsi_cdrom_drive_status_read(dev, i, 0);
            uint16_t len  = (scsi_cdrom_drive_status_read(dev, i, 1) << 8);
            len          |= scsi_cdrom_drive_status_read(dev, i, 2);
            buf[pos++]    = (len >> 8) & 0xff;
            buf[pos++]    = len & 0xff;
            scsi_cdrom_log(dev->log, "CD-ROM %i: DRIVE STATUS: Page [%02X] length %i\n",
                           i, len);
            for (uint16_t j = 0; j < len; j++) {
                if (i == 0x01) {
                    buf[pos++] = scsi_cdrom_drive_status_read(dev, i, 3 + j);
                    if (!(j & 1)) {               /* MSB of Drive Status. */
                        if (dev->drv->ops)        /* Bit 11 of Drive Status, */
                            buf[pos] &= ~0x08;    /* Disc is present. */
                        else
                            buf[pos] |= 0x08;     /* Disc not present. */
                    }
                } else if ((i == 0x02) && (j == 0))
                    buf[pos++] = ((dev->drv->cd_status == CD_STATUS_PLAYING) ?
                                 0x01 : 0x00);
                else
                    buf[pos++] = scsi_cdrom_drive_status_read(dev, i, 3 + j);
            }
        }
    }

    return pos;
}

/*SCSI Mode Sense 6/10*/
static uint8_t
scsi_cdrom_mode_sense_read(const scsi_cdrom_t *dev, const uint8_t pgctl,
                           const uint8_t page, const uint8_t pos)
{
    uint8_t ret = 0;

    switch (pgctl) {
        case 0: case 3:
            ret = dev->ms_pages_saved.pages[page][pos];
            break;

        case 1:
            ret = dev->ms_pages_changeable.pages[page][pos];
            break;

        case 2:
             ret = dev->ms_pages_default.pages[page][pos];
             break;

        default:
             break;
    }      

    return ret;
}

static uint32_t
scsi_cdrom_mode_sense(const scsi_cdrom_t *dev, uint8_t *buf, uint32_t pos,
                      uint8_t page, const uint8_t block_descriptor_len)
{
    const uint8_t pgctl = (page >> 6) & 3;

    page &= 0x3f;

    if (block_descriptor_len) {
        buf[pos++] = 0x01;                        /* Density code. */
        buf[pos++] = 0x00;                        /* Number of blocks (0 = all). */
        buf[pos++] = 0x00;
        buf[pos++] = 0x00;
        buf[pos++] = 0x00;                        /* Reserved. */
        buf[pos++] = dev->drv->sector_size >> 16; /* Block length (default: 0x800 = 2048 bytes). */
        buf[pos++] = dev->drv->sector_size >> 8;
        buf[pos++] = dev->drv->sector_size;
    }

    for (uint8_t i = 0; i < 0x40; i++) {
        if ((page == GPMODE_ALL_PAGES) || (page == i)) {
            if (dev->ms_page_flags & (1LL << ((uint64_t) (page & 0x3f)))) {
                const uint8_t msplen = scsi_cdrom_mode_sense_read(dev, pgctl, i, 1);

                buf[pos++]           = scsi_cdrom_mode_sense_read(dev, pgctl, i, 0);
                buf[pos++]           = msplen;

                scsi_cdrom_log(dev->log, "MODE SENSE: Page [%02X] length %i\n",
                               i, msplen);

                for (uint8_t j = 0; j < msplen; j++) {
                    /*
                       If we are returning changeable values, always return
                       them from the page, so they are all correct.
                     */
                    if (pgctl == 1)
                        buf[pos++] = scsi_cdrom_mode_sense_read(dev, pgctl, i, 2 + j);
                    else {
                        if ((i == GPMODE_CAPABILITIES_PAGE) && (j == 4)) {
                            buf[pos] = scsi_cdrom_mode_sense_read(dev, pgctl, i, 2 + j) & 0x1f;
                            buf[pos++] |= (cdrom_is_caddy(dev->drv->type) ? 0x00 : 0x20);
                        } else if ((i == GPMODE_CAPABILITIES_PAGE) && (j >= 6) &&
                                   (j <= 7)) {
                            if (j & 1)
                                buf[pos++] = ((dev->drv->real_speed * 176) & 0xff);
                            else
                                buf[pos++] = ((dev->drv->real_speed * 176) >> 8);
                        } else if ((i == GPMODE_CAPABILITIES_PAGE) && (j >= 12) &&
                                   (j <= 13)) {
                            if (j & 1)
                                buf[pos++] = ((dev->drv->cur_speed * 176) & 0xff);
                            else
                                buf[pos++] = ((dev->drv->cur_speed * 176) >> 8);
                        } else if (dev->is_sony && (i == GPMODE_CDROM_AUDIO_PAGE_SONY) &&
                                   (j >= 6) && (j <= 13))
                            buf[pos++] = scsi_cdrom_mode_sense_read(dev, pgctl,
                                                                    GPMODE_CDROM_AUDIO_PAGE, 2 + j);
                        else
                            buf[pos++] = scsi_cdrom_mode_sense_read(dev, pgctl,
                                                                    i, 2 + j);
                    }
                }
            }
        }
    }

    return pos;
}

static void
scsi_cdrom_update_request_length(scsi_cdrom_t *dev, int len, const int block_len)
{
    int32_t min_len = 0;
    int32_t bt;

    dev->max_transfer_len = dev->tf->request_length;

    /*
       For media access commands, make sure the requested DRQ length
       matches the block length.
     */
    switch (dev->current_cdb[0]) {
        case 0x08:
        case 0x28:
        case 0xa8:
        case 0xb9:
        case 0xbe:
            /* Round it to the nearest (block length) bytes. */
            if ((dev->current_cdb[0] == 0xb9) || (dev->current_cdb[0] == 0xbe)) {
                /*
                   READ CD MSF and READ CD: Round the request length to the sector size - the
                   device must ensure that a media access comand does not DRQ in the middle
                   of a sector. One of the drivers that relies on the correctness of this
                   behavior is MTMCDAI.SYS (the Mitsumi CD-ROM driver) for DOS which uses
                   the READ CD command to read data on some CD types.
                 */

                /* Round to sector length. */
                const double dlen     = ((double) dev->max_transfer_len) / ((double) block_len);
                dev->max_transfer_len = ((uint16_t) floor(dlen)) * block_len;
            } else {
                /* Round it to the nearest 2048 bytes. */
                dev->max_transfer_len = (dev->max_transfer_len / dev->drv->sector_size) * dev->drv->sector_size;
            }

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
    /*
       If the DRQ length is odd, and the total remaining length is bigger,
       make sure it's even.
     */
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

    return;
}

static double
scsi_cdrom_bus_speed(scsi_cdrom_t *dev)
{
    double ret = -1.0;

    if (dev && dev->drv && (dev->drv->bus_type == CDROM_BUS_SCSI)) {
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
scsi_cdrom_command_common(scsi_cdrom_t *dev)
{
    const uint8_t cmd        = dev->current_cdb[0];

    /* MAP: BUSY_STAT, no DRQ, phase 1. */
    dev->tf->status    = BUSY_STAT;
    dev->tf->phase     = 1;
    dev->tf->pos       = 0;
    dev->callback      = 0;

    scsi_cdrom_log(dev->log, "Current speed: %ix\n", dev->drv->cur_speed);

    if (dev->packet_status == PHASE_COMPLETE)
        dev->callback = 0;
    else {
        double  bytes_per_second;
        double  period;

        switch (cmd) {
            case GPCMD_REZERO_UNIT:
            case 0x0b:
            case 0x2b:
                /* Seek time is in us. */
                period = cdrom_seek_time(dev->drv);
                scsi_cdrom_log(dev->log, "Seek period: %" PRIu64 " us\n",
                               (uint64_t) period);
                dev->callback += period;
                scsi_cdrom_set_callback(dev);
                return;
            case 0x43:
                dev->drv->seek_diff = dev->drv->seek_pos + 150;
                dev->drv->seek_pos  = 0;
                fallthrough;
            case 0x08:
            case 0x28:
            case 0x42: case 0x44:
            case 0xa8:
                /* Seek time is in us. */
                period = cdrom_seek_time(dev->drv);
                scsi_cdrom_log(dev->log, "Seek period: %" PRIu64 " us\n",
                               (uint64_t) period);
                scsi_cdrom_log(dev->log, "Seek period: %" PRIu64 " us, speed: %"
                               PRIu64 " bytes per second, should be: %"
                               PRIu64 " bytes per second\n",
                               (uint64_t) period, (uint64_t) (1000000.0 / period),
                               (uint64_t) (176400.0 * (double) dev->drv->cur_speed));
                dev->callback += period;
                fallthrough;
            case 0x25:
            // case 0x42 ... 0x44:
            case 0x51 ... 0x52:
            case 0xad:
            case 0xb8 ... 0xb9:
            case 0xbe:
                if (dev->current_cdb[0] == 0x42)
                    dev->callback += 40.0;
                /* Account for seek time. */
                /* 44100 * 16 bits * 2 channels = 176400 bytes per second */
                /*
                   TODO: This is a bit of a lie - the actual period is closer to
                         75 * 2448 bytes per second, because the subchannel data
                         has to be read as well.
                 */
                bytes_per_second = 176400.0;
                bytes_per_second *= (double) dev->drv->cur_speed;
                break;
            case 0xc0 ... 0xc3:
            case 0xc6 ... 0xc7:
            case 0xdd ... 0xde:
                if (dev->ven_cmd_is_data[cmd]) {
                    if (dev->current_cdb[0] == 0xc2)
                        dev->callback += 40.0;
                    /* Account for seek time. */
                    /* 44100 * 16 bits * 2 channels = 176400 bytes per second */
                    bytes_per_second = 176400.0;
                    bytes_per_second *= (double) dev->drv->cur_speed;
                    break;
                }
                fallthrough;
            default:
                bytes_per_second = scsi_cdrom_bus_speed(dev);
                if (bytes_per_second == 0.0) {
                    dev->callback = -1; /* Speed depends on SCSI controller */
                    return;
                }
                break;
        }

        period = 1000000.0 / bytes_per_second;
        scsi_cdrom_log(dev->log, "Byte transfer period: %" PRIu64 " us\n",
                       (uint64_t) period);
        switch (cmd) {
            default:
                period = period * (double) (dev->packet_len);
                break;
            case 0x42: case 0x44:
                /* READ SUBCHANNEL or READ HEADER - period of 1 entire sector. */
                period = period * 2352.0;
                break;
            case 0x43:
                /* READ TOC - period of 175 entire frames. */
                period = period * 150.0 * 2352.0;
                break;
        }
        scsi_cdrom_log(dev->log, "Sector transfer period: %" PRIu64 " us\n",
                       (uint64_t) period);
        dev->callback += period;
    }
    scsi_cdrom_set_callback(dev);
}

static void
scsi_cdrom_command_complete(scsi_cdrom_t *dev)
{
    dev->packet_status = PHASE_COMPLETE;
    scsi_cdrom_command_common(dev);
    dev->tf->phase = 3;
}

static void
scsi_cdrom_command_read(scsi_cdrom_t *dev)
{
    dev->packet_status = PHASE_DATA_IN;
    scsi_cdrom_command_common(dev);
    dev->tf->phase = !(dev->packet_status & 0x01) << 1;
}

static void
scsi_cdrom_command_read_dma(scsi_cdrom_t *dev)
{
    dev->packet_status = PHASE_DATA_IN_DMA;
    scsi_cdrom_command_common(dev);
}

static void
scsi_cdrom_command_write(scsi_cdrom_t *dev)
{
    dev->packet_status = PHASE_DATA_OUT;
    scsi_cdrom_command_common(dev);
    dev->tf->phase = !(dev->packet_status & 0x01) << 1;
}

static void
scsi_cdrom_command_write_dma(scsi_cdrom_t *dev)
{
    dev->packet_status = PHASE_DATA_OUT_DMA;
    scsi_cdrom_command_common(dev);
}

/*
   dev = Pointer to current CD-ROM device;
   len = Total transfer length;
   block_len = Length of a single block (it matters because media access commands on ATAPI);
   alloc_len = Allocated transfer length;
   direction = Transfer direction (0 = read from host, 1 = write to host).
 */
static void
scsi_cdrom_data_command_finish(scsi_cdrom_t *dev, int len, int block_len, int alloc_len, int direction)
{
    scsi_cdrom_log(dev->log, "Finishing command (%02X): %i, %i, %i, %i, %i\n",
                   dev->current_cdb[0], len, block_len, alloc_len, direction,
                   dev->tf->request_length);
    dev->tf->pos = 0;
    if (alloc_len >= 0) {
        if (alloc_len < len)
            len = alloc_len;
    }
    if ((len == 0) || (scsi_cdrom_current_mode(dev) == 0)) {
        if (dev->drv->bus_type != CDROM_BUS_SCSI)
            dev->packet_len = 0;

        scsi_cdrom_command_complete(dev);
    } else {
        if (scsi_cdrom_current_mode(dev) == 2) {
            if (dev->drv->bus_type != CDROM_BUS_SCSI)
                dev->packet_len = alloc_len;

            if (direction == 0)
                scsi_cdrom_command_read_dma(dev);
            else
                scsi_cdrom_command_write_dma(dev);
        } else {
            scsi_cdrom_update_request_length(dev, len, block_len);
            if (direction == 0)
                scsi_cdrom_command_read(dev);
            else
                scsi_cdrom_command_write(dev);
        }
    }

    scsi_cdrom_log(dev->log, "Status: %i, cylinder %i, packet length: %i, position: %i, "
                   "phase: %i\n", dev->packet_status, dev->tf->request_length, dev->packet_len,
                   dev->tf->pos, dev->tf->phase);
}

static void
scsi_cdrom_sense_clear(scsi_cdrom_t *dev, UNUSED(int command))
{
    scsi_cdrom_sense_key = scsi_cdrom_asc = scsi_cdrom_ascq = 0;
    scsi_cdrom_info      = 0x00000000;
}

static void
scsi_cdrom_set_phase(const scsi_cdrom_t *dev, const uint8_t phase)
{
    const uint8_t scsi_bus = (dev->drv->scsi_device_id >> 4) & 0x0f;
    const uint8_t scsi_id  = dev->drv->scsi_device_id & 0x0f;

    if (dev->drv->bus_type == CDROM_BUS_SCSI)
        scsi_devices[scsi_bus][scsi_id].phase = phase;
}

static void
scsi_cdrom_cmd_error(scsi_cdrom_t *dev)
{
    scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
    dev->tf->error     = ((scsi_cdrom_sense_key & 0xf) << 4) | ABRT_ERR;
    dev->tf->status    = READY_STAT | ERR_STAT;
    dev->tf->phase     = 3;
    dev->tf->pos       = 0;
    dev->packet_status = PHASE_ERROR;
    dev->callback      = 50.0 * CDROM_TIME;
    scsi_cdrom_set_callback(dev);
    ui_sb_update_icon(SB_CDROM | dev->id, 0);
    scsi_cdrom_log(dev->log, "ERROR: %02X/%02X/%02X\n", scsi_cdrom_sense_key,
                   scsi_cdrom_asc, scsi_cdrom_ascq);
}

static void
scsi_cdrom_unit_attention(scsi_cdrom_t *dev)
{
    scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
    dev->tf->error     = (SENSE_UNIT_ATTENTION << 4) | ABRT_ERR;
    dev->tf->status    = READY_STAT | ERR_STAT;
    dev->tf->phase     = 3;
    dev->tf->pos       = 0;
    dev->packet_status = PHASE_ERROR;
    dev->callback      = 50.0 * CDROM_TIME;
    scsi_cdrom_set_callback(dev);
    ui_sb_update_icon(SB_CDROM | dev->id, 0);
    scsi_cdrom_log(dev->log, "UNIT ATTENTION\n");
}

static void
scsi_cdrom_buf_alloc(scsi_cdrom_t *dev, const uint32_t len)
{
    if (dev->buffer == NULL)
        dev->buffer = (uint8_t *) malloc(len);

    scsi_cdrom_log(dev->log, "Allocated buffer length: %i, buffer = %p\n",
                   len, dev->buffer);
}

static void
scsi_cdrom_buf_free(scsi_cdrom_t *dev)
{
    if (dev->buffer) {
        scsi_cdrom_log(dev->log, "Freeing buffer...\n");
        free(dev->buffer);
        dev->buffer = NULL;
    }
}

static void
scsi_cdrom_bus_master_error(scsi_common_t *sc)
{
    scsi_cdrom_t *dev = (scsi_cdrom_t *) sc;

    scsi_cdrom_log(dev->log, "Bus master error\n");
    scsi_cdrom_buf_free(dev);
    scsi_cdrom_sense_key = scsi_cdrom_asc = scsi_cdrom_ascq = 0;
    scsi_cdrom_info      =  (dev->sector_pos >> 24)        |
                           ((dev->sector_pos >> 16) <<  8) |
                           ((dev->sector_pos >> 8)  << 16) |
                           ( dev->sector_pos        << 24);
    scsi_cdrom_cmd_error(dev);
}

static void
scsi_cdrom_error_common(scsi_cdrom_t *dev, uint8_t sense_key, uint8_t asc, uint8_t ascq, uint32_t info)
{
    scsi_cdrom_log(dev->log, "Medium not present\n");
    scsi_cdrom_sense_key = sense_key;
    scsi_cdrom_asc       = asc;
    scsi_cdrom_ascq      = ascq;
    scsi_cdrom_info      = info;
    scsi_cdrom_cmd_error(dev);
}

static void
scsi_cdrom_not_ready(scsi_cdrom_t *dev)
{
    scsi_cdrom_log(dev->log, "Medium not present\n");
    scsi_cdrom_sense_key = SENSE_NOT_READY;
    scsi_cdrom_asc       = ASC_MEDIUM_NOT_PRESENT;
    scsi_cdrom_ascq      = 0;
    scsi_cdrom_info      = 0x00000000;
    scsi_cdrom_cmd_error(dev);
}

static void
scsi_cdrom_circ_error(scsi_cdrom_t *dev)
{
    scsi_cdrom_log(dev->log, "CIRC unrecovered error\n");
    scsi_cdrom_sense_key = SENSE_MEDIUM_ERROR;
    scsi_cdrom_asc       = ASC_UNRECOVERED_READ_ERROR;
    scsi_cdrom_ascq      = ASCQ_CIRC_UNRECOVERED_ERROR;
    scsi_cdrom_info      =  (dev->sector_pos >> 24)        |
                           ((dev->sector_pos >> 16) <<  8) |
                           ((dev->sector_pos >> 8)  << 16) |
                           ( dev->sector_pos        << 24);
    scsi_cdrom_cmd_error(dev);
}

static void
scsi_cdrom_invalid_lun(scsi_cdrom_t *dev, const uint8_t lun)
{
    scsi_cdrom_log(dev->log, "Invalid LUN\n");
    scsi_cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_cdrom_asc       = ASC_INV_LUN;
    scsi_cdrom_ascq      = 0;
    scsi_cdrom_info      = lun << 24;
    scsi_cdrom_cmd_error(dev);
}

static void
scsi_cdrom_illegal_opcode(scsi_cdrom_t *dev, const uint8_t opcode)
{
    scsi_cdrom_log(dev->log, "Illegal opcode\n");
    scsi_cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_cdrom_asc       = ASC_ILLEGAL_OPCODE;
    scsi_cdrom_ascq      = 0;
    scsi_cdrom_info      = opcode << 24;
    scsi_cdrom_cmd_error(dev);
}

static void
scsi_cdrom_lba_out_of_range(scsi_cdrom_t *dev)
{
    scsi_cdrom_log(dev->log, "LBA out of range\n");
    scsi_cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_cdrom_asc       = ASC_LBA_OUT_OF_RANGE;
    scsi_cdrom_ascq      = 0;
    scsi_cdrom_info      =  (dev->sector_pos >> 24)        |
                           ((dev->sector_pos >> 16) <<  8) |
                           ((dev->sector_pos >> 8)  << 16) |
                           ( dev->sector_pos        << 24);
    scsi_cdrom_cmd_error(dev);
}

static void
scsi_cdrom_invalid_field(scsi_cdrom_t *dev, const uint32_t field)
{
    scsi_cdrom_log(dev->log, "Invalid field in command packet\n");
    scsi_cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_cdrom_asc       = ASC_INV_FIELD_IN_CMD_PACKET;
    scsi_cdrom_ascq      = 0;
    scsi_cdrom_info      =  (field >> 24)        |
                           ((field >> 16) <<  8) |
                           ((field >> 8)  << 16) |
                           ( field        << 24);
    scsi_cdrom_cmd_error(dev);
    dev->tf->status = 0x53;
}

static void
scsi_cdrom_invalid_field_pl(scsi_cdrom_t *dev, const uint32_t field)
{
    scsi_cdrom_log(dev->log, "Invalid field in parameter list\n");
    scsi_cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_cdrom_asc       = ASC_INV_FIELD_IN_PARAMETER_LIST;
    scsi_cdrom_ascq      = 0;
    scsi_cdrom_info      =  (field >> 24)        |
                           ((field >> 16) <<  8) |
                           ((field >> 8)  << 16) |
                           ( field        << 24);
    scsi_cdrom_cmd_error(dev);
    dev->tf->status = 0x53;
}

static void
scsi_cdrom_incompatible_format(scsi_cdrom_t *dev, const uint32_t val)
{
    scsi_cdrom_log(dev->log, "Incompatible format\n");
    scsi_cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_cdrom_asc       = ASC_INCOMPATIBLE_FORMAT;
    scsi_cdrom_ascq      = 2;
    scsi_cdrom_info      =  (val >> 24)        |
                           ((val >> 16) <<  8) |
                           ((val >> 8)  << 16) |
                           ( val        << 24);
    scsi_cdrom_cmd_error(dev);
}

static void
scsi_cdrom_data_phase_error(scsi_cdrom_t *dev, const uint32_t info)
{
    scsi_cdrom_log(dev->log, "Data phase error\n");
    scsi_cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_cdrom_asc       = ASC_DATA_PHASE_ERROR;
    scsi_cdrom_ascq      = 0;
    scsi_cdrom_info      =  (info >> 24)        |
                           ((info >> 16) <<  8) |
                           ((info >> 8)  << 16) |
                           ( info        << 24);
    scsi_cdrom_cmd_error(dev);
}

static void
scsi_cdrom_illegal_mode(scsi_cdrom_t *dev)
{
    scsi_cdrom_log(dev->log, "Illegal mode for this track\n");
    scsi_cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_cdrom_asc       = ASC_ILLEGAL_MODE_FOR_THIS_TRACK;
    scsi_cdrom_ascq      = 0;
    scsi_cdrom_info      =  (dev->sector_pos >> 24)        |
                           ((dev->sector_pos >> 16) <<  8) |
                           ((dev->sector_pos >> 8)  << 16) |
                           ( dev->sector_pos        << 24);
    scsi_cdrom_cmd_error(dev);
}

static int
scsi_cdrom_read_data(scsi_cdrom_t *dev, const int msf, const int type, const int flags,
                     int32_t *len, const int vendor_type)
{
    int      temp_len = 0;
    int      ret      = 0;

    if (dev->drv->cd_status == CD_STATUS_EMPTY)
        scsi_cdrom_not_ready(dev);
    else {
        const uint32_t cdsize   = dev->drv->cdrom_capacity;

        if (dev->sector_pos >= cdsize) {
            scsi_cdrom_log(dev->log, "Trying to read from beyond the end of "
                           "disc (%i >= %i)\n", dev->sector_pos, cdsize);
            scsi_cdrom_lba_out_of_range(dev);
            ret = -1;
        } else {
            int      data_pos = 0;

            dev->old_len = 0;
            *len         = 0;

            ret = 1;

            for (int i = 0; i < dev->requested_blocks; i++) {
                ret = cdrom_readsector_raw(dev->drv, dev->buffer + data_pos,
                                           dev->sector_pos + i, msf, type,
                                           flags, &temp_len, vendor_type);

                data_pos += temp_len;
                dev->old_len += temp_len;

                *len += temp_len;

                if (ret == 0) {
                    scsi_cdrom_illegal_mode(dev);
                    break;
                }

                if (ret < 0) {
                    scsi_cdrom_circ_error(dev);
                    break;
                }
            }
        }
    }

    return ret;
}

static int
scsi_cdrom_read_blocks(scsi_cdrom_t *dev, int32_t *len, const int vendor_type)
{
    int ret   = 1;
    int msf   = 0;
    int type  = dev->sector_type;
    int flags = dev->sector_flags;

    /* Any of these commands stop the audio playing. */
    cdrom_stop(dev->drv);

    switch (dev->current_cdb[0]) {
        case GPCMD_READ_CD_MSF_OLD:
        case GPCMD_READ_CD_MSF:
            msf = 1;
            fallthrough;
        case GPCMD_READ_CD_OLD:
        case GPCMD_READ_CD:
            type  = (dev->current_cdb[1] >> 2) & 7;
            flags = dev->current_cdb[9] | (((uint32_t) dev->current_cdb[10]) << 8);
            break;
        case GPCMD_READ_HEADER:
            type  = 0x00;
            flags = 0x20;
            break;
        default:
            if (dev->sector_type == 0xff) {
                scsi_cdrom_illegal_mode(dev);
                ret   = 0;
            }
            break;
    }

    if (ret) {
        if (!dev->sector_len) {
            scsi_cdrom_command_complete(dev);
            return -1;
        }

        scsi_cdrom_log(dev->log, "Reading %i blocks starting from %i...\n",
                       dev->requested_blocks, dev->sector_pos);

        ret = scsi_cdrom_read_data(dev, msf, type, flags, len, vendor_type);

        scsi_cdrom_log(dev->log, "Read %i bytes of blocks (ret = %i)...\n", *len, ret);
    }

    if ((ret > 0) && (dev->current_cdb[0] != GPCMD_READ_HEADER)) {
        dev->sector_pos += dev->requested_blocks;
        dev->drv->seek_pos = dev->sector_pos;

        dev->sector_len -= dev->requested_blocks;
    }

    return ret;
}

static void
scsi_cdrom_insert(void *priv)
{
    scsi_cdrom_t *dev = (scsi_cdrom_t *) priv;

    if ((dev == NULL) || (dev->drv == NULL))
        return;

    if (dev->drv->ops == NULL) {
        dev->unit_attention = 0;
        dev->drv->cd_status = CD_STATUS_EMPTY;
        scsi_cdrom_log(dev->log, "Media removal\n");
    } else if (dev->drv->cd_status & CD_STATUS_TRANSITION) {
        dev->unit_attention = 1;
        /* Turn off the medium changed status. */
        dev->drv->cd_status &= ~CD_STATUS_TRANSITION;
        scsi_cdrom_log(dev->log, "Media insert\n");
    } else {
        dev->unit_attention = 0;
        dev->drv->cd_status |= CD_STATUS_TRANSITION;
        scsi_cdrom_log(dev->log, "Media transition\n");
    }
}

static int
scsi_command_check_ready(const scsi_cdrom_t *dev, const uint8_t *cdb)
{
    int ret = 0;

    if (scsi_cdrom_command_flags[cdb[0]] & CHECK_READY) {
        /*
           Note by TC1995: Some vendor commands from X vendor don't really
                           check for ready status but they do on Y vendor.
                           Quite confusing I know.
         */
        if (!dev->is_sony || (cdb[0] != 0xc0))
            ret = 1;
    } else if ((cdb[0] == GPCMD_READ_DVD_STRUCTURE) && (cdb[7] < 0xc0))
        ret = 1;

    return ret;
}

static int
scsi_cdrom_pre_execution_check(scsi_cdrom_t *dev, const uint8_t *cdb)
{
    int       ready;

    if ((cdb[0] != GPCMD_REQUEST_SENSE) && (dev->cur_lun == SCSI_LUN_USE_CDB) &&
        (cdb[1] & 0xe0)) {
        scsi_cdrom_log(dev->log, "Attempting to execute a unknown command targeted "
                       "at SCSI LUN %i\n", ((dev->tf->request_length >> 5) & 7));
        scsi_cdrom_invalid_lun(dev, cdb[1] >> 5);
        return 0;
    }

    if (!(scsi_cdrom_command_flags[cdb[0]] & IMPLEMENTED)) {
        scsi_cdrom_log(dev->log, "Attempting to execute unknown command %02X over %s\n",
                       cdb[0], (dev->drv->bus_type == CDROM_BUS_SCSI) ? "SCSI" : "ATAPI");

        scsi_cdrom_illegal_opcode(dev, cdb[0]);
        return 0;
    }

    if ((dev->drv->bus_type < CDROM_BUS_SCSI) &&
        (scsi_cdrom_command_flags[cdb[0]] & SCSI_ONLY)) {
        scsi_cdrom_log(dev->log, "Attempting to execute SCSI-only command %02X over "
                       "ATAPI\n", cdb[0]);
        scsi_cdrom_illegal_opcode(dev, cdb[0]);
        return 0;
    }

    if ((dev->drv->bus_type == CDROM_BUS_SCSI) &&
        (scsi_cdrom_command_flags[cdb[0]] & ATAPI_ONLY)) {
        scsi_cdrom_log(dev->log, "Attempting to execute ATAPI-only command %02X over "
                       "SCSI\n", cdb[0]);
        scsi_cdrom_illegal_opcode(dev, cdb[0]);
        return 0;
    }

    if ((dev->drv->cd_status == CD_STATUS_PLAYING) ||
        (dev->drv->cd_status == CD_STATUS_PAUSED)) {
        ready = 1;
        goto skip_ready_check;
    }

    if (dev->drv->cd_status & CD_STATUS_TRANSITION) {
        if ((cdb[0] == GPCMD_TEST_UNIT_READY) || (cdb[0] == GPCMD_REQUEST_SENSE))
            ready = 0;
        else {
            if (!(scsi_cdrom_command_flags[cdb[0]] & ALLOW_UA))
                scsi_cdrom_insert((void *) dev);

            ready = (dev->drv->cd_status != CD_STATUS_EMPTY);
        }
    } else
        ready = (dev->drv->cd_status != CD_STATUS_EMPTY);

skip_ready_check:
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
        if (!(scsi_cdrom_command_flags[cdb[0]] & ALLOW_UA)) {
            scsi_cdrom_log(dev->log, "Unit attention now 2\n");
            dev->unit_attention++;
            scsi_cdrom_log(dev->log, "UNIT ATTENTION: Command %02X not allowed to "
                           "pass through\n", cdb[0]);
            scsi_cdrom_unit_attention(dev);
            return 0;
        }
    } else if (dev->unit_attention == 2) {
        if (cdb[0] != GPCMD_REQUEST_SENSE) {
            scsi_cdrom_log(dev->log, "Unit attention now 0\n");
            dev->unit_attention = 0;
        }
    }

    /*
       Unless the command is REQUEST SENSE, clear the sense. This will *NOT* clear
       the UNIT ATTENTION condition if it's set.
     */
    if (cdb[0] != GPCMD_REQUEST_SENSE)
        scsi_cdrom_sense_clear(dev, cdb[0]);

    /* Next it's time for NOT READY. */
    if (ready)
        dev->media_status = dev->unit_attention ? MEC_NEW_MEDIA : MEC_NO_CHANGE;
    else
        dev->media_status = MEC_MEDIA_REMOVAL;

    if (!ready && scsi_command_check_ready(dev, cdb)) {
        scsi_cdrom_log(dev->log, "Not ready (%02X)\n", cdb[0]);
        scsi_cdrom_not_ready(dev);
        return 0;
    }

    scsi_cdrom_log(dev->log, "Continuing with command %02X\n", cdb[0]);
    return 1;
}

static void
scsi_cdrom_rezero(scsi_cdrom_t *dev)
{
    dev->sector_pos = dev->sector_len = 0;
    cdrom_seek(dev->drv, 0, 0);
}

static int
scsi_cdrom_update_sector_flags(scsi_cdrom_t *dev)
{
    int ret = 0;

    switch (dev->drv->sector_size) {
        default:
            dev->sector_type  = 0xff;
            scsi_cdrom_log(dev->log, "Invalid sector size: %i\n", dev->drv->sector_size);
            scsi_cdrom_invalid_field_pl(dev, dev->drv->sector_size);
            ret               = 1;
            break;
        case  128: case  256: case  512: case 2048:
            /*
               Internal type code indicating both Mode 1 and Mode 2 Form 1 are allowed.
               Upper 4 bits indicate the divisor.
             */
            dev->sector_type  = 0x08 | ((2048 / dev->drv->sector_size) << 4);
            dev->sector_flags = 0x0010;
            break;
        case 2056:
            dev->sector_type  = 0x18;
            dev->sector_flags = 0x0050;
            break;
        case 2324: case 2328:
            dev->sector_type  = (dev->drv->sector_size == 2328) ? 0x1a : 0x1b;
            dev->sector_flags = 0x0018;
            break;
        case 2332: case 2336:
            dev->sector_type  = (dev->drv->sector_size == 2336) ? 0x1c : 0x1d;
            dev->sector_flags = 0x0058;
            break;
        case 2340:
            dev->sector_type  = 0x18;
            dev->sector_flags = 0x0078;
            break;
        case 2352:
            dev->sector_type  = 0x00;
            dev->sector_flags = 0x00f8;
            break;
        case 2368:
            dev->sector_type  = 0x00;
            dev->sector_flags = 0x01f8;
            break;
        case 2448:
            dev->sector_type  = 0x00;
            dev->sector_flags = 0x02f8;
            break;
    }

    return ret;
}

void
scsi_cdrom_reset(scsi_common_t *sc)
{
    scsi_cdrom_t *dev = (scsi_cdrom_t *) sc;

    if (dev != NULL) {
        scsi_cdrom_rezero(dev);
        dev->tf->status   = 0;
        dev->callback     = 0.0;
        scsi_cdrom_set_callback(dev);
        dev->tf->phase          = 1;
        dev->tf->request_length = 0xeb14;
        dev->packet_status      = PHASE_NONE;
        dev->unit_attention     = 0xff;
        dev->cur_lun            = SCSI_LUN_USE_CDB;

        dev->drv->sector_size   = 2048;
        (void) scsi_cdrom_update_sector_flags(dev);

        scsi_cdrom_sense_key = scsi_cdrom_asc = scsi_cdrom_ascq = dev->unit_attention = 0;
        scsi_cdrom_info      = 0x00000000;
        dev->drv->cd_status &= ~CD_STATUS_TRANSITION;
    }
}

static void
scsi_cdrom_request_sense(scsi_cdrom_t *dev, uint8_t *buffer, uint8_t alloc_length)
{
    /* Will return 18 bytes of 0x00. */
    if (alloc_length != 0) {
        memset(buffer, 0x00, alloc_length);
        memcpy(buffer, dev->sense, alloc_length);
    }

    buffer[0] = 0xf0;
    buffer[7] = 0x0a;

    if ((scsi_cdrom_sense_key > 0) && (dev->drv->cd_status == CD_STATUS_PLAYING_COMPLETED)) {
        buffer[2]  = SENSE_ILLEGAL_REQUEST;
        buffer[12] = ASC_AUDIO_PLAY_OPERATION;
        buffer[13] = ASCQ_AUDIO_PLAY_OPERATION_COMPLETED;
    } else if ((scsi_cdrom_sense_key == 0) &&
               ((dev->drv->cd_status == CD_STATUS_PAUSED) || ((dev->drv->cd_status >= CD_STATUS_PLAYING) &&
                (dev->drv->cd_status != CD_STATUS_STOPPED)))) {
        buffer[2]  = SENSE_ILLEGAL_REQUEST;
        buffer[12] = ASC_AUDIO_PLAY_OPERATION;
        buffer[13] = (dev->drv->cd_status == CD_STATUS_PLAYING) ? ASCQ_AUDIO_PLAY_OPERATION_IN_PROGRESS : ASCQ_AUDIO_PLAY_OPERATION_PAUSED;
    } else if (dev->unit_attention && ((scsi_cdrom_sense_key == 0) || (scsi_cdrom_sense_key == 2))) {
        buffer[2]  = SENSE_UNIT_ATTENTION;
        buffer[12] = ASC_MEDIUM_MAY_HAVE_CHANGED;
        buffer[13] = 0;
    }

    scsi_cdrom_log(dev->log, "Reporting sense: %02X %02X %02X\n", buffer[2], buffer[12],
                   buffer[13]);

    if (buffer[2] == SENSE_UNIT_ATTENTION) {
        /* If the last remaining sense is unit attention, clear
           that condition. */
        dev->unit_attention = 0;
    }

    if (dev->drv->cd_status & CD_STATUS_TRANSITION) {
        scsi_cdrom_log(dev->log, "CD_STATUS_TRANSITION: scsi_cdrom_insert()\n");
        scsi_cdrom_insert((void *) dev);
    }
}

void
scsi_cdrom_request_sense_for_scsi(scsi_common_t *sc, uint8_t *buffer, uint8_t alloc_length)
{
    scsi_cdrom_t *dev                = (scsi_cdrom_t *) sc;

    if ((dev->drv->cd_status == CD_STATUS_EMPTY) && dev->unit_attention) {
        /*
           If the drive is not ready, there is no reason to keep the UNIT ATTENTION
           condition present, as we only use it to mark disc changes.
         */
        dev->unit_attention = 0;
    }

    /* Do *NOT* advance the unit attention phase. */
    scsi_cdrom_request_sense(dev, buffer, alloc_length);
}

static void
scsi_cdrom_set_buf_len(const scsi_cdrom_t *dev, int32_t *BufLen, int32_t *src_len)
{
    if (dev->drv->bus_type == CDROM_BUS_SCSI) {
        if (*BufLen == -1)
            *BufLen = *src_len;
        else {
            *BufLen  = MIN(*src_len, *BufLen);
            *src_len = *BufLen;
        }
        scsi_cdrom_log(dev->log, "Actual transfer length: %i\n", *BufLen);
    }
}

static void
scsi_cdrom_stop(const scsi_common_t *sc)
{
    const scsi_cdrom_t *dev = (const scsi_cdrom_t *) sc;

    cdrom_stop(dev->drv);
}

static void
scsi_cdrom_set_speed(scsi_cdrom_t *dev, const uint8_t *cdb)
{
    /* Stop the audio playing. */
    cdrom_stop(dev->drv);

    dev->drv->cur_speed = (cdb[3] | (cdb[2] << 8)) / 176;
    if (dev->drv->cur_speed < 1)
        dev->drv->cur_speed = 1;
    else if (dev->drv->cur_speed > dev->drv->real_speed)
        dev->drv->cur_speed = dev->drv->real_speed;
    scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
    scsi_cdrom_command_complete(dev);
}

static uint8_t
scsi_cdrom_command_chinon(void *sc, const uint8_t *cdb, UNUSED(int32_t *BufLen))
{
    scsi_cdrom_t *dev                    = (scsi_cdrom_t *) sc;
    uint8_t       cmd_stat               = 0x00;

    switch (cdb[0]) {
        default:
            break;

        case GPCMD_UNKNOWN_CHINON:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            scsi_cdrom_stop(sc);
            scsi_cdrom_command_complete(dev);
            cmd_stat = 0x01;
            break;

        case GPCMD_EJECT_CHINON:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            scsi_cdrom_stop(sc);
            cdrom_eject(dev->id);
            scsi_cdrom_command_complete(dev);
            cmd_stat = 0x01;
            break;

        case GPCMD_STOP_CHINON:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            scsi_cdrom_stop(sc);
            scsi_cdrom_command_complete(dev);
            cmd_stat = 0x01;
            break;
    }

    return cmd_stat;
}

static uint8_t
scsi_cdrom_command_dec_sony_texel(void *sc, const uint8_t *cdb, int32_t *BufLen)
{
    scsi_cdrom_t *dev                    = (scsi_cdrom_t *) sc;
    int           msf;
    uint8_t       cmd_stat               = 0x00;
    int           len;
    int           max_len;
    int           alloc_length;
    int           real_pos;

    switch (cdb[0]) {
        default:
            break;

        case GPCMD_SET_ADDRESS_FORMAT_SONY:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            dev->drv->sony_msf = cdb[8] & 1;
            scsi_cdrom_command_complete(dev);
            cmd_stat = 0x01;
            break;

        case GPCMD_READ_TOC_SONY:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);
            msf              = dev->drv->sony_msf;

            max_len = cdb[7];
            max_len <<= 8;
            max_len |= cdb[8];

            scsi_cdrom_buf_alloc(dev, 65536);

            if (dev->drv->ops == NULL)
                scsi_cdrom_not_ready(dev);
            else {
                len = cdrom_read_toc_sony(dev->drv, dev->buffer, cdb[5], msf, max_len);

                if (len == -1)
                    /* If the returned length is -1, this means cdrom_read_toc_sony() has encountered an error. */
                    scsi_cdrom_invalid_field(dev, dev->drv->inv_field);
                else {
                    scsi_cdrom_set_buf_len(dev, BufLen, &len);
                    scsi_cdrom_data_command_finish(dev, len, len, len, 0);
                }
            }
            cmd_stat = 0x01;
            break;

        case GPCMD_READ_SUBCHANNEL_SONY:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

            max_len = cdb[7];
            max_len <<= 8;
            max_len |= cdb[8];
            msf = dev->drv->sony_msf;

            scsi_cdrom_log(dev->log, "Getting sub-channel type (%s), code-q = %02x\n",
                           msf ? "MSF" : "LBA", cdb[2] & 0x40);

            if (cdb[2] & 0x40) {
                scsi_cdrom_buf_alloc(dev, 9);
                memset(dev->buffer, 0, 9);
                len = 9;
                cdrom_get_current_subchannel_sony(dev->drv, dev->buffer, msf);
                len = MIN(len, max_len);
                scsi_cdrom_set_buf_len(dev, BufLen, &len);
                scsi_cdrom_data_command_finish(dev, len, len, len, 0);
            } else {
                scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                scsi_cdrom_log(dev->log, "Drive Status All done - callback set\n");
                dev->packet_status = PHASE_COMPLETE;
                dev->callback      = 20.0 * CDROM_TIME;
                scsi_cdrom_set_callback(dev);
            }
            cmd_stat = 0x01;
            break;

        case GPCMD_READ_HEADER_SONY:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

            alloc_length = ((cdb[7] << 8) | cdb[8]);
            scsi_cdrom_buf_alloc(dev, 4);

            dev->sector_len = 1;
            dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
            real_pos        = cdrom_lba_to_msf_accurate(dev->sector_pos);
            dev->buffer[0]  = ((real_pos >> 16) & 0xff);
            dev->buffer[1]  = ((real_pos >> 8) & 0xff);
            dev->buffer[2]  = real_pos & 0xff;
            dev->buffer[3]  = 1; /*2048 bytes user data*/

            len = 4;
            len = MIN(len, alloc_length);

            scsi_cdrom_set_buf_len(dev, BufLen, &len);

            scsi_cdrom_data_command_finish(dev, len, len, len, 0);
            cmd_stat = 0x01;
            break;

        case GPCMD_PLAYBACK_STATUS_SONY:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

            max_len = cdb[7];
            max_len <<= 8;
            max_len |= cdb[8];
            msf = dev->drv->sony_msf;

            scsi_cdrom_buf_alloc(dev, 18);

            len = 18;

            memset(dev->buffer, 0, 18);
            dev->buffer[0] = 0x00;                                    /* Reserved */
            dev->buffer[1] = 0x00;                                    /* Reserved */
            dev->buffer[2] = 0x00;                                    /* Audio Status data length */
            dev->buffer[3] = 0x00;                                    /* Audio Status data length */
            dev->buffer[4] = cdrom_get_audio_status_sony(dev->drv,    /* Audio status */
                                                         &dev->buffer[6], msf);
            dev->buffer[5] = 0x00;

            scsi_cdrom_log(dev->log, "Audio Status = %02x\n", dev->buffer[4]);

            len = MIN(len, max_len);
            scsi_cdrom_set_buf_len(dev, BufLen, &len);

            scsi_cdrom_data_command_finish(dev, len, len, len, 0);
            cmd_stat = 0x01;
            break;

        case GPCMD_PAUSE_SONY:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            cdrom_audio_pause_resume(dev->drv, !(cdb[1] & 0x10));
            scsi_cdrom_command_complete(dev);
            cmd_stat = 0x01;
            break;

        case GPCMD_PLAY_TRACK_SONY:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);

            msf = 3;
            if ((cdb[5] != 1) || (cdb[8] != 1))
                scsi_cdrom_illegal_mode(dev);
            else {
                const int     pos = cdb[4];

                if ((dev->drv->image_path[0] == 0x00) ||
                    (dev->drv->cd_status <= CD_STATUS_DVD))
                    scsi_cdrom_illegal_mode(dev);
                else {
                    /* In this case, len is unused so just pass a fixed value of 1 intead. */
                    const int ret = cdrom_audio_play(dev->drv, pos, 1, msf);

                    if (ret)
                        scsi_cdrom_command_complete(dev);
                    else
                        scsi_cdrom_illegal_mode(dev);
                }
            }
            cmd_stat = 0x01;
            break;

        case GPCMD_PLAY_MSF_SONY:
            dev->current_cdb[0] = GPCMD_PLAY_AUDIO_MSF;
            /*
               Keep cmd_stat at 0x00, therefore, it's going to process it
               as GPCMD_PLAY_AUDIO_MSF.
             */
            break;

        case GPCMD_PLAY_AUDIO_SONY:
            dev->current_cdb[0] = GPCMD_PLAY_AUDIO_10;
            /*
               Keep cmd_stat at 0x00, therefore, it's going to process it
               as GPCMD_PLAY_AUDIO_10.
             */
            break;

        case GPCMD_PLAYBACK_CONTROL_SONY:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_OUT);

            len = (cdb[7] << 8) | cdb[8];
            if (len == 0) {
                scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                scsi_cdrom_log(dev->log, "PlayBack Control Sony All done - "
                               "callback set\n");
                dev->packet_status = PHASE_COMPLETE;
                dev->callback      = 20.0 * CDROM_TIME;
                scsi_cdrom_set_callback(dev);
            } else {
                scsi_cdrom_buf_alloc(dev, 65536);

                scsi_cdrom_set_buf_len(dev, BufLen, &len);
                scsi_cdrom_data_command_finish(dev, len, len, len, 1);
            }
            break;
    }

    return cmd_stat;
}

static uint8_t
scsi_cdrom_command_matsushita(void *sc, const uint8_t *cdb, UNUSED(int32_t *BufLen))
{
    scsi_cdrom_t  *dev      = (scsi_cdrom_t *) sc;
    const uint8_t  cmd_stat = 0x00;

    switch (cdb[0]) {
        default:
            break;

        case GPCMD_READ_SUBCHANNEL_MATSUSHITA:
            dev->current_cdb[0] = GPCMD_READ_SUBCHANNEL;
            /*
               Keep cmd_stat at 0x00, therefore, it's going to process it
               as GPCMD_READ_SUBCHANNEL.
             */
            break;

        case GPCMD_READ_TOC_MATSUSHITA:
            dev->current_cdb[0] = GPCMD_READ_TOC_PMA_ATIP;
            /*
               Keep cmd_stat at 0x00, therefore, it's going to process it
               as GPCMD_READ_TOC_PMA_ATIP.
             */
            break;

        case GPCMD_READ_HEADER_MATSUSHITA:
            dev->current_cdb[0] = GPCMD_READ_HEADER;
            /*
               Keep cmd_stat at 0x00, therefore, it's going to process it
               as GPCMD_READ_HEADER.
             */
            break;

        case GPCMD_PLAY_AUDIO_MATSUSHITA:
            dev->current_cdb[0] = GPCMD_PLAY_AUDIO_10;
            /*
               Keep cmd_stat at 0x00, therefore, it's going to process it
               as GPCMD_PLAY_AUDIO_10.
             */
            break;

        case GPCMD_PLAY_AUDIO_MSF_MATSUSHITA:
            dev->current_cdb[0] = GPCMD_PLAY_AUDIO_MSF;
            /*
               Keep cmd_stat at 0x00, therefore, it's going to process it
               as GPCMD_PLAY_AUDIO_MSF.
             */
            break;

        case GPCMD_PLAY_AUDIO_TRACK_INDEX_MATSUSHITA:
            dev->current_cdb[0] = GPCMD_PLAY_AUDIO_TRACK_INDEX;
            /*
               Keep cmd_stat at 0x00, therefore, it's going to process it
               as GPCMD_PLAY_AUDIO_TRACK_INDEX.
             */
            break;

        case GPCMD_PLAY_AUDIO_TRACK_RELATIVE_10_MATSUSHITA:
            dev->current_cdb[0] = GPCMD_PLAY_AUDIO_TRACK_RELATIVE_10;
            /*
               Keep cmd_stat at 0x00, therefore, it's going to process it
               as GPCMD_PLAY_AUDIO_TRACK_RELATIVE_10.
             */
            break;

        case GPCMD_PAUSE_RESUME_MATSUSHITA:
            dev->current_cdb[0] = GPCMD_PAUSE_RESUME;
            /*
               Keep cmd_stat at 0x00, therefore, it's going to process it
               as GPCMD_PAUSE_RESUME.
             */
            break;

        case GPCMD_PLAY_AUDIO_12_MATSUSHITA:
            dev->current_cdb[0] = GPCMD_PLAY_AUDIO_12;
            /*
               Keep cmd_stat at 0x00, therefore, it's going to process it
               as GPCMD_PLAY_AUDIO_12.
             */
            break;

        case GPCMD_PLAY_AUDIO_TRACK_RELATIVE_12_MATSUSHITA:
            dev->current_cdb[0] = GPCMD_PLAY_AUDIO_TRACK_RELATIVE_12;
            /*
               Keep cmd_stat at 0x00, therefore, it's going to process it
               as GPCMD_PLAY_AUDIO_TRACK_RELATIVE_12.
             */
            break;
    }

    return cmd_stat;
}

static uint8_t
scsi_cdrom_command_nec(void *sc, const uint8_t *cdb, int32_t *BufLen)
{
    scsi_cdrom_t *dev                    = (scsi_cdrom_t *) sc;
    uint8_t       cmd_stat               = 0x00;
    int           pos;
    int           ret;
    int           len;
    int           alloc_length;

    switch (cdb[0]) {
        default:
            break;

        case GPCMD_NO_OPERATION_NEC:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            scsi_cdrom_command_complete(dev);
            cmd_stat = 0x01;
            break;

        case GPCMD_UNKNOWN_SCSI2_NEC:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            scsi_cdrom_command_complete(dev);
            cmd_stat = 0x01;
            break;

        case GPCMD_AUDIO_TRACK_SEARCH_NEC:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            if ((dev->drv->image_path[0] == 0x00) || (dev->drv->cd_status <= CD_STATUS_DVD))
                scsi_cdrom_illegal_mode(dev);
            else {
                pos                = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
                ret                = cdrom_audio_track_search(dev->drv, pos, cdb[9] & 0xc0, cdb[1] & 1);
                dev->drv->audio_op = (cdb[1] & 1) ? 0x03 : 0x02;

                if (ret)
                    scsi_cdrom_command_complete(dev);
                else
                    scsi_cdrom_illegal_mode(dev);
            }
            cmd_stat = 0x01;
            break;

        case GPCMD_PLAY_AUDIO_NEC:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            if ((dev->drv->image_path[0] == 0x00) || (dev->drv->cd_status <= CD_STATUS_DVD))
                ret = 0;
            else {
                pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
                ret = cdrom_audio_play_toshiba(dev->drv, pos, cdb[9] & 0xc0);
            }

            if (ret)
                scsi_cdrom_command_complete(dev);
             else
                scsi_cdrom_illegal_mode(dev);

            cmd_stat = 0x01;
            break;

        case GPCMD_STILL_NEC:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            cdrom_audio_pause_resume(dev->drv, 0x00);
            dev->drv->audio_op = 0x01;
            scsi_cdrom_command_complete(dev);
            cmd_stat = 0x01;
            break;

        case GPCMD_SET_STOP_TIME_NEC:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            scsi_cdrom_command_complete(dev);
            cmd_stat = 0x01;
            break;

        case GPCMD_CADDY_EJECT_NEC:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            scsi_cdrom_stop(sc);
            cdrom_eject(dev->id);
            scsi_cdrom_command_complete(dev);
            cmd_stat = 0x01;
            break;

        case GPCMD_READ_SUBCODEQ_PLAYING_STATUS_NEC:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

            alloc_length = cdb[1] & 0x1f;
            len          = 10;

            if (dev->drv->ops == NULL)
                scsi_cdrom_not_ready(dev);
            else if (alloc_length <= 0) {
                scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                scsi_cdrom_log(dev->log, "Subcode Q All done - callback set\n");
                dev->packet_status = PHASE_COMPLETE;
                dev->callback      = 20.0 * CDROM_TIME;
                scsi_cdrom_set_callback(dev);
            } else {
                scsi_cdrom_buf_alloc(dev, len);
                len = MIN(len, alloc_length);

                memset(dev->buffer, 0, len);
                dev->buffer[0] = cdrom_get_current_subcodeq_playstatus(dev->drv, &dev->buffer[1]);
                scsi_cdrom_log(dev->log, "Audio Status = %02x\n", dev->buffer[0]);

                scsi_cdrom_set_buf_len(dev, BufLen, &alloc_length);
                scsi_cdrom_data_command_finish(dev, len, len, len, 0);
            }
            cmd_stat = 0x01;
            break;

        case GPCMD_READ_DISC_INFORMATION_NEC:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);
            /*
               NEC manual claims 4 bytes but the Linux kernel
               (namely sr_vendor.c) actually states otherwise.
             */
            scsi_cdrom_buf_alloc(dev, 22);

            ret = cdrom_read_disc_info_toc(dev->drv, dev->buffer, cdb[2], cdb[1] & 3);
            len = 22;
            if (ret) {
                scsi_cdrom_set_buf_len(dev, BufLen, &len);
                scsi_cdrom_data_command_finish(dev, len, len, len, 0);
            } else
                scsi_cdrom_invalid_field(dev, dev->drv->inv_field);

            cmd_stat = 0x01;
            break;
    }

    return cmd_stat;
}

static uint8_t
scsi_cdrom_command_pioneer(void *sc, const uint8_t *cdb, int32_t *BufLen)
{
    scsi_cdrom_t *dev                    = (scsi_cdrom_t *) sc;
    uint8_t       cmd_stat               = 0x00;
    int           pos;
    int           ret;
    int           len;
    int           max_len;
    int           alloc_length;

    switch (cdb[0]) {
        default:
            break;

        case GPCMD_MAGAZINE_EJECT_PIONEER:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            scsi_cdrom_stop(sc);
            cdrom_eject(dev->id);
            scsi_cdrom_command_complete(dev);
            cmd_stat = 0x01;
            break;

        case GPCMD_READ_TOC_PIONEER:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);
            scsi_cdrom_buf_alloc(dev, 4);

            if (dev->drv->ops == NULL)
                scsi_cdrom_not_ready(dev);
            else {
                ret = cdrom_read_disc_info_toc(dev->drv, dev->buffer, cdb[2], cdb[1] & 3);
                len = 4;

                if (ret) {
                    scsi_cdrom_set_buf_len(dev, BufLen, &len);
                    scsi_cdrom_data_command_finish(dev, len, len, len, 0);
                } else
                    scsi_cdrom_invalid_field(dev, dev->drv->inv_field);
            }
            cmd_stat = 0x01;
            break;

        case GPCMD_READ_SUBCODEQ_PIONEER:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

            alloc_length = cdb[1] & 0x1f;
            len          = 9;

            if (dev->drv->ops == NULL)
                scsi_cdrom_not_ready(dev);
            else if (!alloc_length) {
                scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                scsi_cdrom_log(dev->log, "Subcode Q All done - callback set\n");
                dev->packet_status = PHASE_COMPLETE;
                dev->callback      = 20.0 * CDROM_TIME;
                scsi_cdrom_set_callback(dev);
            } else {
                scsi_cdrom_buf_alloc(dev, len);
                len = MIN(len, alloc_length);

                memset(dev->buffer, 0, len);
                cdrom_get_current_subcodeq(dev->drv, &dev->buffer[1]);
                scsi_cdrom_log(dev->log, "Audio Status = %02x\n", dev->buffer[0]);

                scsi_cdrom_set_buf_len(dev, BufLen, &alloc_length);
                scsi_cdrom_data_command_finish(dev, len, len, len, 0);
            }
            cmd_stat = 0x01;
            break;

        case GPCMD_AUDIO_TRACK_SEARCH_PIONEER:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            if ((dev->drv->image_path[0] == 0x00) || (dev->drv->cd_status <= CD_STATUS_DVD))
                ret = 0;
            else {
                pos                = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
                ret                = cdrom_audio_track_search_pioneer(dev->drv, pos, cdb[1] & 1);

                dev->drv->audio_op = (cdb[1] & 1) ? 0x03 : 0x02;
            }

            if (ret)
                scsi_cdrom_command_complete(dev);
            else
                scsi_cdrom_illegal_mode(dev);

            cmd_stat = 0x01;
            break;

        case GPCMD_PLAY_AUDIO_PIONEER:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            if ((dev->drv->image_path[0] == 0x00) || (dev->drv->cd_status <= CD_STATUS_DVD))
                ret = 0;
            else {
                pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
                ret = cdrom_audio_play_pioneer(dev->drv, pos);
            }

            if (ret)
                scsi_cdrom_command_complete(dev);
            else
                scsi_cdrom_illegal_mode(dev);
            cmd_stat = 0x01;
            break;

        case GPCMD_PAUSE_PIONEER:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            cdrom_audio_pause_resume(dev->drv, !(cdb[1] & 0x10));
            scsi_cdrom_command_complete(dev);
            cmd_stat = 0x01;
            break;

        case GPCMD_STOP_PIONEER:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            scsi_cdrom_stop(sc);
            scsi_cdrom_command_complete(dev);
            cmd_stat = 0x01;
            break;

        case GPCMD_PLAYBACK_STATUS_PIONEER:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

            max_len = cdb[7];
            max_len <<= 8;
            max_len |= cdb[8];

            scsi_cdrom_buf_alloc(dev, 6);

            len = 6;

            memset(dev->buffer, 0, 6);
            dev->buffer[0] = cdrom_get_audio_status_pioneer(dev->drv, &dev->buffer[1]); /*Audio status*/

            scsi_cdrom_log(dev->log, "Audio Status = %02x\n", dev->buffer[4]);

            len = MIN(len, max_len);
            scsi_cdrom_set_buf_len(dev, BufLen, &len);

            scsi_cdrom_data_command_finish(dev, len, len, len, 0);
            cmd_stat = 0x01;
            break;

        case GPCMD_DRIVE_STATUS_PIONEER:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

            len = (cdb[9] | (cdb[8] << 8));
            scsi_cdrom_buf_alloc(dev, 65536);

            if (!(scsi_cdrom_drive_status_page_flags & (1LL << (uint64_t) (cdb[2] & 0x3f))))
                scsi_cdrom_invalid_field(dev, cdb[2]);
            else if (len <= 0) {
                scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                scsi_cdrom_log(dev->log, "Drive Status All done - callback set\n");
                dev->packet_status = PHASE_COMPLETE;
                dev->callback      = 20.0 * CDROM_TIME;
                scsi_cdrom_set_callback(dev);
            } else {
                memset(dev->buffer, 0, len);
                alloc_length = len;

                len = scsi_cdrom_drive_status(dev, dev->buffer, cdb[2]);
                len = MIN(len, alloc_length);

                scsi_cdrom_set_buf_len(dev, BufLen, &len);

                scsi_cdrom_log(dev->log, "Reading drive status page: %02X...\n",
                               cdb[2]);

                scsi_cdrom_data_command_finish(dev, len, len, alloc_length, 0);
            }
            cmd_stat = 0x01;
            break;
    }

    return cmd_stat;
}

static uint8_t
scsi_cdrom_command_toshiba(void *sc, const uint8_t *cdb, int32_t *BufLen)
{
    scsi_cdrom_t *dev                    = (scsi_cdrom_t *) sc;
    uint8_t       cmd_stat               = 0x00;
    int           pos;
    int           ret;
    int           len;
    int           alloc_length;

    switch (cdb[0]) {
        default:
            break;

        case GPCMD_NO_OPERATION_TOSHIBA:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            scsi_cdrom_command_complete(dev);
            cmd_stat = 0x01;
            break;

        case GPCMD_AUDIO_TRACK_SEARCH_TOSHIBA:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            if ((dev->drv->image_path[0] == 0x00) || (dev->drv->cd_status <= CD_STATUS_DVD)) {
                scsi_cdrom_illegal_mode(dev);
                break;
            }
            pos                = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
            ret                = cdrom_audio_track_search(dev->drv, pos, cdb[9] & 0xc0, cdb[1] & 1);
            dev->drv->audio_op = (cdb[1] & 1) ? 0x03 : 0x02;

            if (ret)
                scsi_cdrom_command_complete(dev);
            else
                scsi_cdrom_illegal_mode(dev);
            cmd_stat = 0x01;
            break;

        case GPCMD_PLAY_AUDIO_TOSHIBA:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);

            if ((dev->drv->image_path[0] == 0x00) || (dev->drv->cd_status <= CD_STATUS_DVD))
                scsi_cdrom_illegal_mode(dev);
            else {
                pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
                ret = cdrom_audio_play_toshiba(dev->drv, pos, cdb[9] & 0xc0);

                if (ret)
                    scsi_cdrom_command_complete(dev);
                else
                    scsi_cdrom_illegal_mode(dev);
            }
            cmd_stat = 0x01;
            break;

        case GPCMD_STILL_TOSHIBA:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            cdrom_audio_pause_resume(dev->drv, 0x00);
            dev->drv->audio_op = 0x01;
            scsi_cdrom_command_complete(dev);
            cmd_stat = 0x01;
            break;

        case GPCMD_SET_STOP_TIME_TOSHIBA:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            scsi_cdrom_command_complete(dev);
            cmd_stat = 0x01;
            break;

        case GPCMD_CADDY_EJECT_TOSHIBA:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            scsi_cdrom_stop(sc);
            cdrom_eject(dev->id);
            scsi_cdrom_command_complete(dev);
            cmd_stat = 0x01;
            break;

        case GPCMD_READ_SUBCODEQ_PLAYING_STATUS_TOSHIBA:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

            alloc_length = cdb[1] & 0x1f;
            len          = 10;

            if (dev->drv->ops == NULL)
                scsi_cdrom_not_ready(dev);
            else if (alloc_length <= 0) {
                scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                scsi_cdrom_log(dev->log, "Subcode Q All done - callback set\n");
                dev->packet_status = PHASE_COMPLETE;
                dev->callback      = 20.0 * CDROM_TIME;
                scsi_cdrom_set_callback(dev);
            } else {
                scsi_cdrom_buf_alloc(dev, len);
                len = MIN(len, alloc_length);

                memset(dev->buffer, 0, len);
                dev->buffer[0] = cdrom_get_current_subcodeq_playstatus(dev->drv, &dev->buffer[1]);
                scsi_cdrom_log(dev->log, "Audio Status = %02x\n", dev->buffer[0]);

                scsi_cdrom_set_buf_len(dev, BufLen, &alloc_length);
                scsi_cdrom_data_command_finish(dev, len, len, len, 0);
            }
            cmd_stat = 0x01;
            break;

        case GPCMD_READ_DISC_INFORMATION_TOSHIBA:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);
            scsi_cdrom_buf_alloc(dev, 4);

            if (dev->drv->ops == NULL)
                scsi_cdrom_not_ready(dev);
            else {
                ret = cdrom_read_disc_info_toc(dev->drv, dev->buffer, cdb[2], cdb[1] & 3);
                len = 4;
                if (ret) {
                    scsi_cdrom_set_buf_len(dev, BufLen, &len);
                    scsi_cdrom_data_command_finish(dev, len, len, len, 0);
                } else
                    scsi_cdrom_invalid_field(dev, dev->drv->inv_field);
            }
            cmd_stat = 0x01;
            break;
    }

    return cmd_stat;
}

void
scsi_cdrom_command(scsi_common_t *sc, const uint8_t *cdb)
{
    scsi_cdrom_t *dev                    = (scsi_cdrom_t *) sc;
    int           pos                    = dev->drv->seek_pos;
    int           idx                    = 0;
    int           ret                    = 1;
    int32_t       blen                   = 0;
    uint32_t      profiles[2]            = { MMC_PROFILE_CD_ROM, MMC_PROFILE_DVD_ROM };
    uint8_t       scsi_bus               = (dev->drv->scsi_device_id >> 4) & 0x0f;
    uint8_t       scsi_id                = dev->drv->scsi_device_id & 0x0f;
    char          model[2048]            = { 0 };
    int           msf;
    int           block_desc;
    int           len;
    int           max_len;
    int           used_len;
    int           alloc_length;
    int           size_idx;
    uint32_t      feature;
    unsigned      preamble_len;
    int           toc_format;
    int32_t      *BufLen;

    if (dev->drv->bus_type == CDROM_BUS_SCSI) {
        BufLen = &scsi_devices[scsi_bus][scsi_id].buffer_length;
        dev->tf->status &= ~ERR_STAT;
    } else {
        BufLen     = &blen;
        dev->tf->error = 0;
    }

    dev->packet_len  = 0;
    dev->request_pos = 0;

    memcpy(dev->current_cdb, cdb, 12);

#if ENABLE_SCSI_CDROM_LOG == 2
        scsi_cdrom_log(dev->log, "Command 0x%02X, Sense Key %02X, Asc %02X, Ascq %02X, "
                       "Unit attention: %i\n", cdb[0], scsi_cdrom_sense_key, scsi_cdrom_asc,
                       scsi_cdrom_ascq, dev->unit_attention);
        scsi_cdrom_log(dev->log, "Request length: %04X\n", dev->tf->request_length);

        scsi_cdrom_log(dev->log, "CDB: %02X %02X %02X %02X %02X %02X %02X "
                       "%02X %02X %02X %02X %02X\n",
                       cdb[0], cdb[1], cdb[2], cdb[3], cdb[4], cdb[5], cdb[6], cdb[7],
                       cdb[8], cdb[9], cdb[10], cdb[11]);
#endif

    msf             = cdb[1] & 2;
    dev->sector_len = 0;

    scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);

    /*
       This handles the Not Ready/Unit Attention check if it has to be
       handled at this point.
     */
    if (scsi_cdrom_pre_execution_check(dev, cdb) == 0)
        return;

    if (cdb[0] != GPCMD_REQUEST_SENSE) {
        /* Clear the sense stuff as per the spec. */
        scsi_cdrom_sense_clear(dev, cdb[0]);
    }

    if ((dev->ven_cmd == NULL) || (dev->ven_cmd(sc, cdb, BufLen) == 0x00))  switch (dev->current_cdb[0]) {
        case GPCMD_TEST_UNIT_READY:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            scsi_cdrom_command_complete(dev);
            break;

        case GPCMD_REZERO_UNIT:
            scsi_cdrom_stop(sc);
            dev->sector_pos = dev->sector_len = 0;
            dev->drv->seek_diff               = dev->drv->seek_pos;
            cdrom_seek(dev->drv, 0, 0);
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            break;

        case GPCMD_REQUEST_SENSE:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);
            max_len = cdb[4];

            if (!max_len) {
                scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                dev->packet_status = PHASE_COMPLETE;
                dev->callback      = 20.0 * CDROM_TIME;
                scsi_cdrom_set_callback(dev);
                break;
            }

            scsi_cdrom_buf_alloc(dev, 256);
            scsi_cdrom_set_buf_len(dev, BufLen, &max_len);
            scsi_cdrom_request_sense(dev, dev->buffer, max_len);
            scsi_cdrom_data_command_finish(dev, 18, 18, cdb[4], 0);
            break;

        case GPCMD_SET_SPEED:
            scsi_cdrom_set_speed(dev, cdb);
            break;

        case GPCMD_SCAN_PIONEER:
        case GPCMD_AUDIO_SCAN:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);

            if ((dev->drv->image_path[0] == 0x00) ||
                (dev->drv->cd_status <= CD_STATUS_DVD)) {
                scsi_cdrom_illegal_mode(dev);
                break;
            }

            pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
            ret = cdrom_audio_scan(dev->drv, pos, 0);

            if (ret)
                scsi_cdrom_command_complete(dev);
            else
                scsi_cdrom_illegal_mode(dev);
            break;

        case GPCMD_MECHANISM_STATUS:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);
            len = (cdb[7] << 16) | (cdb[8] << 8) | cdb[9];

            scsi_cdrom_buf_alloc(dev, 8);

            scsi_cdrom_set_buf_len(dev, BufLen, &len);

            memset(dev->buffer, 0, 8);
            dev->buffer[5] = 1;

            scsi_cdrom_data_command_finish(dev, 8, 8, len, 0);
            break;

        case GPCMD_READ_TOC_PMA_ATIP:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

            max_len = cdb[7];
            max_len <<= 8;
            max_len |= cdb[8];

            scsi_cdrom_buf_alloc(dev, 65536);

            toc_format = cdb[2] & 0xf;

            if (toc_format == 0)
                toc_format = (cdb[9] >> 6) & 3;

            if (dev->drv->ops == NULL)
                scsi_cdrom_not_ready(dev);
            else if (toc_format < 3) {
                len = cdrom_read_toc(dev->drv, dev->buffer, toc_format, cdb[6], msf, max_len);
                /* If the returned length is -1, this means cdrom_read_toc() has encountered an error. */
                if (len == -1)
                    scsi_cdrom_invalid_field(dev, dev->drv->inv_field);
                else {
                    scsi_cdrom_set_buf_len(dev, BufLen, &len);
                    scsi_cdrom_data_command_finish(dev, len, len, len, 0);
                }
            } else
                scsi_cdrom_invalid_field(dev, toc_format);
            break;

        case GPCMD_READ_6:
        case GPCMD_READ_10:
        case GPCMD_READ_12:
        case GPCMD_READ_CD_OLD:
        case GPCMD_READ_CD_MSF_OLD:
        case GPCMD_READ_CD:
        case GPCMD_READ_CD_MSF:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);
            alloc_length = dev->drv->sector_size;

            switch (cdb[0]) {
                case GPCMD_READ_6:
                    dev->sector_len = cdb[4];
                    /*
                       For READ (6) and WRITE (6), a length of 0 indicates a transfer of
                       256 sectors.
                     */
                    if (dev->sector_len == 0)
                        dev->sector_len = 256;
                    dev->sector_pos = ((((uint32_t) cdb[1]) & 0x1f) << 16) |
                                      (((uint32_t) cdb[2]) << 8) | ((uint32_t) cdb[3]);
                    scsi_cdrom_log(dev->log, "READ (6):  Length: %i, LBA: %i\n",
                                   dev->sector_len, dev->sector_pos);
                    break;
                case GPCMD_READ_10:
                    dev->sector_len = (cdb[7] << 8) | cdb[8];
                    dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) |
                                      (cdb[4] << 8) | cdb[5];
                    scsi_cdrom_log(dev->log, "READ (10): Length: %i, LBA: %i\n",
                                   dev->sector_len, dev->sector_pos);
                    break;
                case GPCMD_READ_12:
                    dev->sector_len = (((uint32_t) cdb[6]) << 24) |
                                      (((uint32_t) cdb[7]) << 16) |
                                      (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);
                    dev->sector_pos = (((uint32_t) cdb[2]) << 24) |
                                      (((uint32_t) cdb[3]) << 16) |
                                      (((uint32_t) cdb[4]) << 8) | ((uint32_t) cdb[5]);
                    scsi_cdrom_log(dev->log, "READ (12): Length: %i, LBA: %i\n",
                                   dev->sector_len, dev->sector_pos);
                    break;
                case GPCMD_READ_CD_MSF_OLD:
                case GPCMD_READ_CD_MSF:
                    msf = 1;
                    fallthrough;
                case GPCMD_READ_CD_OLD:
                case GPCMD_READ_CD:
                    alloc_length    = 2856;

                    if (msf) {
                        dev->sector_len = MSFtoLBA(cdb[6], cdb[7], cdb[8]);
                        dev->sector_pos = MSFtoLBA(cdb[3], cdb[4], cdb[5]);

                        dev->sector_len -= dev->sector_pos;
                        dev->sector_len++;
                    } else {
                        dev->sector_len = (cdb[6] << 16) | (cdb[7] << 8) | cdb[8];
                        dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) |
                                          (cdb[4] << 8) | cdb[5];
                    }

                    if (((cdb[9] & 0xf8) == 0x08) || ((cdb[9] == 0x00) &&
                        ((cdb[10] & 0x07) != 0x00))) {
                        /* Illegal mode */
                        scsi_cdrom_invalid_field(dev, cdb[9]);
                        ret = 0;
                    } else if ((cdb[9] == 0x00) && ((cdb[10] & 0x07) == 0x00))
                        /*
                           If all the flag bits are cleared, then treat it as a
                           non-data command.
                         */
                        dev->sector_len = 0;
                    break;

                default:
                    break;
            }

            if (ret) {
                if (dev->sector_len > 0) {
                    max_len               = dev->sector_len;
                    dev->requested_blocks = max_len;

                    dev->packet_len = max_len * alloc_length;
                    scsi_cdrom_buf_alloc(dev, dev->packet_len);

                    dev->drv->seek_diff = ABS((int) (pos - dev->sector_pos));
                    dev->drv->seek_pos  = dev->sector_pos;                   

                    if (dev->use_cdb_9 && ((cdb[0] == GPCMD_READ_10) ||
                        (cdb[0] == GPCMD_READ_12)))
                        ret = scsi_cdrom_read_blocks(dev, &alloc_length,
                                                     cdb[9] & 0xc0);
                    else
                        ret = scsi_cdrom_read_blocks(dev, &alloc_length, 0);

                    if (ret > 0) {
                        dev->requested_blocks = max_len;
                        dev->packet_len       = alloc_length;

                        scsi_cdrom_set_buf_len(dev, BufLen,
                                               (int32_t *) &dev->packet_len);

                        scsi_cdrom_data_command_finish(dev, alloc_length,
                                                       alloc_length / dev->requested_blocks,
                                                       alloc_length, 0);

                        if (dev->packet_status != PHASE_COMPLETE)
                            ui_sb_update_icon(SB_CDROM | dev->id, 1);
                        else
                            ui_sb_update_icon(SB_CDROM | dev->id, 0);
                    } else {
                        scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                        dev->packet_status = (ret < 0) ? PHASE_ERROR : PHASE_COMPLETE;
                        dev->callback      = 20.0 * CDROM_TIME;
                        scsi_cdrom_set_callback(dev);
                    }
                } else {
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                    /* scsi_cdrom_log(dev->log, "All done - callback set\n"); */
                    dev->packet_status = PHASE_COMPLETE;
                    dev->callback      = 20.0 * CDROM_TIME;
                    scsi_cdrom_set_callback(dev);
                }
            }
            break;

        case GPCMD_READ_HEADER:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);
            alloc_length = 2352;

            len             = (cdb[7] << 8) | cdb[8];
            dev->sector_len = 1;
            dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
            scsi_cdrom_log(dev->log, "READ HEADER: Length: %i, LBA: %i\n",
                           dev->sector_len, dev->sector_pos);

            if (len > 0) {
                max_len               = 1;
                dev->requested_blocks = max_len;

                dev->packet_len       = len;
                scsi_cdrom_buf_alloc(dev, 2352);

                dev->drv->seek_diff   = ABS((int) (pos - dev->sector_pos));
                dev->drv->seek_pos    = dev->sector_pos;

                ret = scsi_cdrom_read_blocks(dev, &alloc_length, 0);

                if (ret > 0) {
                    uint8_t header[4] = { 0 };

                    memcpy(header, dev->buffer, 4);

                    dev->buffer[0] = header[3];

                    if (cdb[1] & 0x02) {
                        memset(&(dev->buffer[1]), 0x00, 4);
                        dev->buffer[5] = header[0];
                        dev->buffer[6] = header[1];
                        dev->buffer[7] = header[2];
                    } else {
                        memset(&(dev->buffer[1]), 0x00, 3);
                        uint32_t lba = ((header[0] * 60 * 75) +
                                        (header[1] * 75) +
                                        header[2]) - 150;
                        dev->buffer[4] = (lba >> 24) & 0xff;
                        dev->buffer[5] = (lba >> 16) & 0xff;
                        dev->buffer[6] = (lba >>  8) & 0xff;
                        dev->buffer[7] = lba         & 0xff;
                    }

                    len = MIN(8, len);

                    scsi_cdrom_set_buf_len(dev, BufLen, &len);

                    scsi_cdrom_data_command_finish(dev, len, len,
                                                   len, 0);

                    ui_sb_update_icon(SB_CDROM | dev->id, 0);
                } else {
                    scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                    dev->packet_status = (ret < 0) ? PHASE_ERROR : PHASE_COMPLETE;
                    dev->callback      = 20.0 * CDROM_TIME;
                    scsi_cdrom_set_callback(dev);
                }
            } else {
                scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                /* scsi_cdrom_log(dev->log, "All done - callback set\n"); */
                dev->packet_status = PHASE_COMPLETE;
                dev->callback      = 20.0 * CDROM_TIME;
                scsi_cdrom_set_callback(dev);
            }
            break;

        case GPCMD_MODE_SENSE_6:
        case GPCMD_MODE_SENSE_10:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

            if (dev->drv->bus_type == CDROM_BUS_ATAPI)
                block_desc = 0;
            else
                block_desc = !((cdb[1] >> 3) & 1);

            if (cdb[0] == GPCMD_MODE_SENSE_6) {
                len = cdb[4];
                scsi_cdrom_buf_alloc(dev, 256);
            } else {
                len = (cdb[8] | (cdb[7] << 8));
                scsi_cdrom_buf_alloc(dev, 65536);
            }

            if (!(dev->ms_page_flags & (1LL << (uint64_t) (cdb[2] & 0x3f))))
                ret = 0;

            if (ret == 1) {
                memset(dev->buffer, 0, len);
                alloc_length = len;

                /*
                   This determines the media type ID to return which is a SCSI/ATAPI-specific
                   thing, so it makes the most sense to keep this here.

                   Also, the max_len variable is reused as this command does otherwise not
                   use it, to avoid having to declare another variable.
                 */
                if (dev->drv->cd_status == CD_STATUS_EMPTY)
                    max_len = 70; /* No media inserted. */
                else if (dev->drv->cd_status == CD_STATUS_DVD)
                    max_len = 65; /* DVD. */
                else if (dev->drv->cd_status == CD_STATUS_DATA_ONLY)
                    max_len = 1; /* Data CD. */
                else
                    max_len = 3; /* Audio or mixed-mode CD. */

                if (cdb[0] == GPCMD_MODE_SENSE_6) {
                    len            = scsi_cdrom_mode_sense(dev, dev->buffer, 4, cdb[2], block_desc);
                    len            = MIN(len, alloc_length);
                    dev->buffer[0] = len - 1;
                    dev->buffer[1] = max_len;
                    if (block_desc)
                        dev->buffer[3] = 8;
                } else {
                    len            = scsi_cdrom_mode_sense(dev, dev->buffer, 8,
                                                           cdb[2], block_desc);
                    len            = MIN(len, alloc_length);
                    dev->buffer[0] = (len - 2) >> 8;
                    dev->buffer[1] = (len - 2) & 255;
                    dev->buffer[2] = max_len;
                    if (block_desc) {
                        dev->buffer[6] = 0;
                        dev->buffer[7] = 8;
                    }
                }

                scsi_cdrom_set_buf_len(dev, BufLen, &alloc_length);

                scsi_cdrom_log(dev->log, "Reading mode page: %02X...\n", cdb[2]);

                scsi_cdrom_data_command_finish(dev, len, len, alloc_length, 0);
            } else
                 scsi_cdrom_invalid_field(dev, cdb[2]);
            break;

        case GPCMD_MODE_SELECT_6:
        case GPCMD_MODE_SELECT_10:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_OUT);

            if (cdb[0] == GPCMD_MODE_SELECT_6) {
                len = cdb[4];
                scsi_cdrom_buf_alloc(dev, 256);
            } else {
                len = (cdb[7] << 8) | cdb[8];
                scsi_cdrom_buf_alloc(dev, 65536);
            }

            scsi_cdrom_set_buf_len(dev, BufLen, &len);

            dev->total_length = len;
            dev->do_page_save = cdb[1] & 1;

            scsi_cdrom_data_command_finish(dev, len, len, len, 1);
            break;

        case GPCMD_GET_CONFIGURATION:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

            /* XXX: Could result in alignment problems in some architectures */
            feature = (cdb[2] << 8) | cdb[3];
            max_len = (cdb[7] << 8) | cdb[8];

            /* Only feature 0 is supported */
            if ((feature > 3) && (feature != 0x010) && (feature != 0x1d) && (feature != 0x01e) &&
                (feature != 0x01f) && (feature != 0x103))
                scsi_cdrom_invalid_field(dev, feature);
            else {
                scsi_cdrom_buf_alloc(dev, 65536);
                memset(dev->buffer, 0, max_len);

                uint8_t      *b = dev->buffer;

                alloc_length    = 0;

                if (dev->drv->cd_status != CD_STATUS_EMPTY) {
                    if (dev->drv->cd_status == CD_STATUS_DVD) {
                        b[6] = (MMC_PROFILE_DVD_ROM >> 8) & 0xff;
                        b[7] = MMC_PROFILE_DVD_ROM & 0xff;
                        ret  = 1;
                    } else {
                        b[6] = (MMC_PROFILE_CD_ROM >> 8) & 0xff;
                        b[7] = MMC_PROFILE_CD_ROM & 0xff;
                        ret  = 0;
                    }
                } else
                    ret = 2;

                alloc_length = 8;
                b += 8;

                if ((feature == 0) || ((cdb[1] & 3) < 2)) {
                    /* Persistent and current. */
                    b[2] = (0 << 2) | 0x02 | 0x01;
                    b[3] = 8;

                    alloc_length += 4;
                    b += 4;

                    for (uint8_t i = 0; i < 2; i++) {
                        b[0] = (profiles[i] >> 8) & 0xff;
                        b[1] = profiles[i] & 0xff;

                        if (ret == i)
                            b[2] |= 1;

                        alloc_length += 4;
                       b += 4;
                    }
                }
                if ((feature == 1) || ((cdb[1] & 3) < 2)) {
                   /* Persistent and current. */
                    b[1] = 1;
                    b[2] = (2 << 2) | 0x02 | 0x01;
                    b[3] = 8;

                    if (dev->drv->bus_type == CDROM_BUS_SCSI)
                        b[7] = 1;
                    else
                        b[7] = 2;
                    b[8] = 1;

                    alloc_length += 12;
                    b += 12;
                }
                if ((feature == 2) || ((cdb[1] & 3) < 2)) {
                    b[1] = 2;
                    b[2] = (1 << 2) | 0x02 | 0x01; /* persistent and current */
                    b[3] = 4;
                    b[4] = 2;

                    alloc_length += 8;
                    b += 8;
                }
                if ((feature == 3) || ((cdb[1] & 3) < 2)) {
                    b[1] = 2;
                    /* Persistent and current. */
                    b[2] = (0 << 2) | 0x02 | 0x01;
                    b[3] = 4;
                    b[4] = 0x0d | (cdrom_is_caddy(dev->drv->type) ? 0x00 : 0x20);

                    alloc_length += 8;
                    b += 8;
                }
                if ((feature == 0x10) || ((cdb[1] & 3) < 2)) {
                    b[1] = 0x10;
                    /* Persistent and current. */
                    b[2] = (0 << 2) | 0x02 | 0x01;
                    b[3] = 8;

                    b[6] = 8;
                    b[9] = 0x10;

                    alloc_length += 12;
                    b += 12;
                }
                if ((feature == 0x1d) || ((cdb[1] & 3) < 2)) {
                    b[1] = 0x1d;
                    /* Persistent and current. */
                    b[2] = (0 << 2) | 0x02 | 0x01;
                    b[3] = 0;

                    alloc_length += 4;
                    b += 4;
                }
                if ((feature == 0x1e) || ((cdb[1] & 3) < 2)) {
                    b[1] = 0x1e;
                    /* Persistent and current. */
                    b[2] = (2 << 2) | 0x02 | 0x01;
                    b[3] = 4;
                    b[4] = 0;

                    alloc_length += 8;
                    b += 8;
                }
                if ((feature == 0x1f) || ((cdb[1] & 3) < 2)) {
                    b[1] = 0x1f;
                    b[2] = (0 << 2) | 0x02 | 0x01; /* persistent and current */
                    b[3] = 0;

                    alloc_length += 4;
                    b += 4;
                }
                if ((feature == 0x103) || ((cdb[1] & 3) < 2)) {
                    b[0] = 1;
                    b[1] = 3;
                    /* Persistent and current. */
                    b[2] = (0 << 2) | 0x02 | 0x01;
                    b[3] = 0;
                    b[4] = 7;

                    b[6] = 1;

                    alloc_length += 8;
                }

                dev->buffer[0] = ((alloc_length - 4) >> 24) & 0xff;
                dev->buffer[1] = ((alloc_length - 4) >> 16) & 0xff;
                dev->buffer[2] = ((alloc_length - 4) >> 8) & 0xff;
                dev->buffer[3] = (alloc_length - 4) & 0xff;

                alloc_length = MIN(alloc_length, max_len);

                scsi_cdrom_set_buf_len(dev, BufLen, &alloc_length);

                scsi_cdrom_data_command_finish(dev, alloc_length, alloc_length,
                                               alloc_length, 0);
            }
            break;

        case GPCMD_GET_EVENT_STATUS_NOTIFICATION:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

            scsi_cdrom_buf_alloc(dev, 8 + sizeof(gesn_event_header_t));

            gesn_cdb          = (void *) cdb;
            gesn_event_header = (void *) dev->buffer;

            if (gesn_cdb->polled & 0x01) {
                /*
                   These are the supported events.

                   We currently only support requests of the 'media' type.
                   Notification class requests and supported event classes are bitmasks,
                   but they are built from the same values as the "notification class"
                   field.
                 */
                gesn_event_header->supported_events = 1 << GESN_MEDIA;

                /*
                   We use |= below to set the class field; other bits in this byte
                   are reserved now but this is useful to do if we have to use the
                   reserved fields later.
                 */
                gesn_event_header->notification_class = 0;

                /*
                   Responses to requests are to be based on request priority.  The
                   notification_class_request_type enum above specifies the
                   priority: upper elements are higher prio than lower ones.
                 */
                if (gesn_cdb->class & (1 << GESN_MEDIA)) {
                    gesn_event_header->notification_class |= GESN_MEDIA;

                    /* Bits 7-4 = Reserved, Bits 4-1 = Media Status. */
                    dev->buffer[4] = dev->media_status;
                    /* Power Status (1 = Active). */
                    dev->buffer[5] = 1;
                    dev->buffer[6] = 0;
                    dev->buffer[7] = 0;
                    used_len       = 8;
                } else {
                    /* No event available. */
                    gesn_event_header->notification_class = 0x80;
                    used_len                              = sizeof(*gesn_event_header);
                }
                gesn_event_header->len = used_len - sizeof(*gesn_event_header);

                memmove(dev->buffer, gesn_event_header, 4);

                scsi_cdrom_set_buf_len(dev, BufLen, &used_len);

                scsi_cdrom_data_command_finish(dev, used_len, used_len, used_len, 0);
            } else
                /*
                   Only polling is supported, asynchronous mode is not.
                   It is fine by the MMC spec to not support async mode operations.
                 */
                scsi_cdrom_invalid_field(dev, gesn_cdb->polled);
            break;

        case GPCMD_READ_DISC_INFORMATION:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

            max_len = cdb[7];
            max_len <<= 8;
            max_len |= cdb[8];

            scsi_cdrom_buf_alloc(dev, 65536);

            cdrom_read_disc_information(dev->drv, dev->buffer);

            len = MIN(34, max_len);

            scsi_cdrom_set_buf_len(dev, BufLen, &len);

            scsi_cdrom_data_command_finish(dev, len, len, len, 0);
            break;

        case GPCMD_READ_TRACK_INFORMATION:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

            max_len = cdb[7];
            max_len <<= 8;
            max_len |= cdb[8];

            scsi_cdrom_buf_alloc(dev, 65536);

            ret = cdrom_read_track_information(dev->drv, cdb, dev->buffer);

            if (ret > 0) {
                len = ret;

                if (len > max_len) {
                    len            = max_len;
                    dev->buffer[0] = ((max_len - 2) >> 8) & 0xff;
                    dev->buffer[1] = (max_len - 2) & 0xff;
                }

                scsi_cdrom_set_buf_len(dev, BufLen, &len);
                scsi_cdrom_data_command_finish(dev, len, len, max_len, 0);
            } else
                scsi_cdrom_invalid_field(dev, -ret);
            break;

        case GPCMD_PLAY_AUDIO_10:
        case GPCMD_PLAY_AUDIO_12:
        case GPCMD_PLAY_AUDIO_MSF:
        case GPCMD_PLAY_AUDIO_TRACK_INDEX:
        case GPCMD_PLAY_AUDIO_TRACK_RELATIVE_10:
        case GPCMD_PLAY_AUDIO_TRACK_RELATIVE_12:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);

            len = 0;

            switch (cdb[0]) {
                case GPCMD_PLAY_AUDIO_10:
                    msf = 0;
                    pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
                    len = (cdb[7] << 8) | cdb[8];
                    break;
                case GPCMD_PLAY_AUDIO_12:
                    msf = 0;
                    pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
                    len = (cdb[6] << 24) | (cdb[7] << 16) | (cdb[8] << 8) | cdb[9];
                    break;
                case GPCMD_PLAY_AUDIO_MSF:
                    msf = 1;
                    pos = (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
                    len = (cdb[6] << 16) | (cdb[7] << 8) | cdb[8];
                    break;
                case GPCMD_PLAY_AUDIO_TRACK_INDEX:
                    msf = 2;
                    if ((cdb[5] == 1) && (cdb[8] == 1)) {
                        pos = cdb[4];
                        len = cdb[7];
                    } else
                        ret = 0;
                    break;
                case GPCMD_PLAY_AUDIO_TRACK_RELATIVE_10:
                    msf = 0x100 | cdb[6];
                    pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
                    len = (cdb[7] << 8) | cdb[8];
                    break;
                case GPCMD_PLAY_AUDIO_TRACK_RELATIVE_12:
                    msf = 0x100 | cdb[10];
                    pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
                    len = (cdb[6] << 24) | (cdb[7] << 16) | (cdb[8] << 8) | cdb[9];
                    break;

                default:
                    break;
            }

            if (ret && (dev->drv->image_path[0] != 0x00) &&
                (dev->drv->cd_status > CD_STATUS_DVD))
                ret = cdrom_audio_play(dev->drv, pos, len, msf);
            else
                ret = 0;

            if (ret)
                scsi_cdrom_command_complete(dev);
            else
                scsi_cdrom_illegal_mode(dev);
            break;

        case GPCMD_READ_SUBCHANNEL:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

            max_len = cdb[7];
            max_len <<= 8;
            max_len |= cdb[8];
            msf = (cdb[1] >> 1) & 1;

            scsi_cdrom_buf_alloc(dev, 32);

            scsi_cdrom_log(dev->log, "Getting page %i (%s)\n", cdb[3],
                           msf ? "MSF" : "LBA");

            if (cdb[3] > 3)
                scsi_cdrom_invalid_field(dev, cdb[3]);
            else if ((cdb[3] != 3) && (cdb[6] != 0))
                scsi_cdrom_invalid_field(dev, cdb[6]);
            else if (max_len <= 0) {
                scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                dev->packet_status = PHASE_COMPLETE;
                dev->callback      = 20.0 * CDROM_TIME;
                scsi_cdrom_set_callback(dev);
            } else {
                if (!(cdb[2] & 0x40))
                    alloc_length = 4;
                else  switch (cdb[3]) {
                    case 0:
                        /* SCSI-2: Q-type subchannel, ATAPI: reserved. */
                        alloc_length = (dev->drv->bus_type == CDROM_BUS_SCSI) ? 48 : 4;
                        break;
                    case 1:
                        alloc_length = 16;
                        break;
                    default:
                        alloc_length = 24;
                        break;
                }

                len = alloc_length;

                memset(dev->buffer, 0, 24);
                pos                = 0x00;
                dev->buffer[pos++] = 0x00;
                dev->buffer[pos++] = 0x00;    /* Audio status */
                dev->buffer[pos++] = 0x00;
                dev->buffer[pos++] = 0x00;    /* Subchannel length */
                dev->buffer[pos++] = cdb[3];    /* Format code */

                if (alloc_length != 4) {
                    cdrom_get_current_subchannel(dev->drv, &dev->buffer[4], msf);

                    dev->buffer[2] = alloc_length - 4;
                }

                dev->buffer[1] = cdrom_get_current_status(dev->drv);

                scsi_cdrom_log(dev->log, "Audio Status = %02x\n", dev->buffer[1]);

                len = MIN(len, max_len);
                scsi_cdrom_set_buf_len(dev, BufLen, &len);

                scsi_cdrom_data_command_finish(dev, len, len, len, 0);
            }
            break;

        case GPCMD_READ_DVD_STRUCTURE:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

            alloc_length = (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);

            scsi_cdrom_buf_alloc(dev, alloc_length);

            if ((cdb[7] < 0xc0) && (dev->drv->cd_status != CD_STATUS_DVD))
                scsi_cdrom_incompatible_format(dev, cdb[7]);
            else {
                memset(dev->buffer, 0, alloc_length);

                if ((cdb[7] <= 0x7f) || (cdb[7] == 0xff)) {
                    uint32_t info = 0x00000000;

                    if (cdb[1] == 0) {
                        ret            = cdrom_read_dvd_structure(dev->drv, cdb[6], cdb[7], dev->buffer, &info);
                        if (ret > 0) {
                            dev->buffer[0] = (ret >> 8);
                            dev->buffer[1] = (ret & 0xff);
                            dev->buffer[2] = dev->buffer[3] = 0x00;

                            scsi_cdrom_set_buf_len(dev, BufLen, &alloc_length);
                            scsi_cdrom_data_command_finish(dev, alloc_length,
                                                           alloc_length,
                                                           alloc_length, 0);
                        } else if (ret < 0)
                            scsi_cdrom_error_common(dev, (ret >> 16) & 0xff,
                                                    (ret >> 8) & 0xff, ret & 0xff, info);
                        else {
                            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
                            dev->packet_status = PHASE_COMPLETE;
                            dev->callback      = 20.0 * CDROM_TIME;
                            scsi_cdrom_set_callback(dev);
                        }
                    }
                } else
                    scsi_cdrom_invalid_field(dev, cdb[7]);
            }
            break;

        case GPCMD_START_STOP_UNIT:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);

            switch (cdb[4] & 3) {
                case 0: /* Stop the disc. */
                    scsi_cdrom_stop(sc);
                    break;
                case 1: /* Start the disc and read the TOC. */
                    /*
                       This makes no sense under emulation as this would do
                       absolutely nothing, so just break.
                     */
                    break;
                case 2: /* Eject the disc if possible. */
                    scsi_cdrom_stop(sc);
                    cdrom_eject(dev->id);
                    break;
                case 3: /* Load the disc (close tray). */
                    cdrom_reload(dev->id);
                    break;

                default:
                    break;
            }

            scsi_cdrom_command_complete(dev);
            break;

        case GPCMD_INQUIRY:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

            max_len = cdb[3];
            max_len <<= 8;
            max_len |= cdb[4];

            scsi_cdrom_buf_alloc(dev, 65536);

            if (cdb[1] & 1) {
                preamble_len = 4;
                size_idx     = 3;

                if ((cdb[1] & 0xe0) || ((dev->cur_lun > 0x00) && (dev->cur_lun < 0xff)))
                    dev->buffer[idx++] = 0x7f;    /* No physical device on this LUN */
                else
                    dev->buffer[idx++] = 0x05;    /* CD-ROM */

                dev->buffer[idx++] = cdb[2];

                dev->buffer[idx++] = 0x00;
                dev->buffer[idx++] = 0x00;

                switch (cdb[2]) {
                    case 0x00:
                        dev->buffer[idx++] = 0x00;
                        dev->buffer[idx++] = 0x83;
                        break;
                    case 0x83:
                        if (idx + 24 > max_len) {
                            scsi_cdrom_data_phase_error(dev, idx + 24);
                            scsi_cdrom_buf_free(dev);
                            return;
                        }

                        dev->buffer[idx++] = 0x02;
                        dev->buffer[idx++] = 0x00;
                        dev->buffer[idx++] = 0x00;
                        dev->buffer[idx++] = 0x14;
                        ide_padstr8(dev->buffer + idx, 20, "53R141");                            /* Serial */
                        idx += 20;

                        if (idx + 72 > cdb[4])
                            goto atapi_out;

                        dev->buffer[idx++] = 0x02;
                        dev->buffer[idx++] = 0x01;
                        dev->buffer[idx++] = 0x00;
                        dev->buffer[idx++] = 34;

                        ide_padstr8(dev->buffer + idx,  8,
                                    cdrom_get_vendor(dev->drv->type));    /* Vendor */
                        idx += 8;

                        cdrom_get_model(dev->drv->type, model, dev->id);
                        ide_padstr8(dev->buffer + idx, 16, model);                               /* Product */
                        idx += 16;

                        ide_padstr8(dev->buffer + idx, 10, "53R141");                            /* Serial */
                        idx += 10;
                        break;

                    default:
                        scsi_cdrom_log(dev->log, "INQUIRY: Invalid page: %02X\n", cdb[2]);
                        scsi_cdrom_invalid_field(dev, cdb[2]);
                        scsi_cdrom_buf_free(dev);
                        return;
                }
            } else {
                preamble_len = 5;
                size_idx     = 4;

                memset(dev->buffer, 0, 8);
                if ((cdb[1] & 0xe0) || ((dev->cur_lun > 0x00) && (dev->cur_lun < 0xff)))
                    dev->buffer[0] = 0x7f;    /* No physical device on this LUN */
                else
                    dev->buffer[0] = 0x05;    /* CD-ROM */

                dev->buffer[1] = 0x80;    /* Removable */

                if (dev->drv->bus_type == CDROM_BUS_SCSI) {
                    dev->buffer[3] = cdrom_get_scsi_std(dev->drv->type);

                    if (!strcmp(cdrom_get_vendor(dev->drv->type), "TOSHIBA"))
                        /* Linked Command and Relative Addressing supported */
                        dev->buffer[7] = 0x88;
                } else {
                    dev->buffer[2] = 0x00;
                    dev->buffer[3] = 0x21;
                }

                if (cdrom_is_generic(dev->drv->type)) {
                    dev->buffer[6] = 0x01;    /* 16-bit transfers supported */
                    dev->buffer[7] = 0x20;    /* Wide bus supported */
                }

                ide_padstr8(dev->buffer + 8,   8,
                            cdrom_get_vendor(dev->drv->type));      /* Vendor */
                cdrom_get_model(dev->drv->type, model, dev->id);
                ide_padstr8(dev->buffer + 16, 16, model);                                 /* Product */
                ide_padstr8(dev->buffer + 32,  4,
                            cdrom_get_revision(dev->drv->type));    /* Revision */

                if (cdrom_has_date(dev->drv->type)) {
                    dev->buffer[36] = 0x20;
                    ide_padstr8(dev->buffer + 37, 10, "1991/01/01");                      /* Date */
                }

                if (max_len == 96)
                    idx = 96;
                else
                    idx =  cdrom_get_inquiry_len(dev->drv->bus_type);

                dev->buffer[4] = idx - 5;
            }

atapi_out:
            dev->buffer[size_idx] = idx - preamble_len;
            len                   = idx;

            len = MIN(len, max_len);

            scsi_cdrom_set_buf_len(dev, BufLen, &max_len);
            scsi_cdrom_log(dev->log, "Inquiry = %d, max = %d, BufLen = %d.\n", len,
                           max_len, *BufLen);

            scsi_cdrom_data_command_finish(dev, len, len, max_len, 0);
            break;

        case GPCMD_PREVENT_REMOVAL:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            scsi_cdrom_command_complete(dev);
            break;

        case GPCMD_PAUSE_RESUME:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
            cdrom_audio_pause_resume(dev->drv, cdb[8] & 0x01);
            dev->drv->audio_op = (cdb[8] & 0x01) ? 0x03 : 0x01;
            scsi_cdrom_command_complete(dev);
            break;

        case GPCMD_SEEK_6:
        case GPCMD_SEEK_10:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);

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

            dev->drv->seek_diff = ABS((int) (pos - dev->drv->seek_pos));

            /* Stop the audio playing. */
            cdrom_stop(dev->drv);

            if (dev->use_cdb_9 && (cdb[0] == GPCMD_SEEK_10))
                cdrom_seek(dev->drv, pos, cdb[9] & 0xc0);
            else
                cdrom_seek(dev->drv, pos, 0);

            scsi_cdrom_command_complete(dev);
            break;

        case GPCMD_READ_CDROM_CAPACITY:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

            scsi_cdrom_buf_alloc(dev, 8);

            /* IMPORTANT: What's returned is the last LBA block. */
            memset(dev->buffer, 0, 8);
            dev->buffer[0] = ((dev->drv->cdrom_capacity - 1) >> 24) & 0xff;
            dev->buffer[1] = ((dev->drv->cdrom_capacity - 1) >> 16) & 0xff;
            dev->buffer[2] = ((dev->drv->cdrom_capacity - 1) >> 8) & 0xff;
            dev->buffer[3] = (dev->drv->cdrom_capacity - 1) & 0xff;
            dev->buffer[6] = 8;
            len            = 8;

            scsi_cdrom_log(dev->log, "CD-ROM Capacity: %08X\n",
                           dev->drv->cdrom_capacity - 1);
            scsi_cdrom_set_buf_len(dev, BufLen, &len);

            scsi_cdrom_data_command_finish(dev, len, len, len, 0);
            break;

        case GPCMD_STOP_PLAY_SCAN:
            scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);

            if (dev->drv->cd_status <= CD_STATUS_DVD) {
                scsi_cdrom_illegal_mode(dev);
                break;
            }

            scsi_cdrom_stop(sc);
            scsi_cdrom_command_complete(dev);
            break;

        default:
            scsi_cdrom_illegal_opcode(dev, cdb[0]);
            break;
    }

    /* scsi_cdrom_log(dev->log, "Phase: %02X, request length: %i\n", dev->tf->phase,
                      dev->tf->request_length); */

    if ((dev->packet_status == PHASE_COMPLETE) || (dev->packet_status == PHASE_ERROR))
        scsi_cdrom_buf_free(dev);
}

static void
scsi_cdrom_command_stop(scsi_common_t *sc)
{
    scsi_cdrom_t *dev = (scsi_cdrom_t *) sc;

    scsi_cdrom_command_complete(dev);
    scsi_cdrom_buf_free(dev);
}

/* The command second phase function, needed for Mode Select. */
static uint8_t
scsi_cdrom_phase_data_out(scsi_common_t *sc)
{
    scsi_cdrom_t *dev = (scsi_cdrom_t *) sc;
    uint16_t      block_desc_len;
    uint16_t      pos;
    uint16_t      param_list_len;
    uint16_t      i;
    uint8_t       error          = 0;
    uint8_t       page;
    uint8_t       page_len;
    uint8_t       hdr_len;
    uint8_t       val;
    uint8_t       old_val;
    uint8_t       ch;

    switch (dev->current_cdb[0]) {
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

            if (dev->drv->bus_type == CDROM_BUS_SCSI) {
                if (dev->current_cdb[0] == GPCMD_MODE_SELECT_6) {
                    block_desc_len = dev->buffer[2];
                    block_desc_len <<= 8;
                    block_desc_len |= dev->buffer[3];
                    scsi_cdrom_log(dev->log, "BlockDescLen (6): %d, "
                                   "ParamListLen (6): %d\n", block_desc_len, param_list_len);
                } else {
                    block_desc_len = dev->buffer[6];
                    block_desc_len <<= 8;
                    block_desc_len |= dev->buffer[7];
                }
            } else
                block_desc_len = 0;

            if (block_desc_len >= 8) {
                pos = hdr_len + 5;

                dev->drv->sector_size = (dev->drv->sector_size & 0x0000ffff) |
                                        (dev->buffer[pos++] << 16);
                dev->drv->sector_size = (dev->drv->sector_size & 0x00ff00ff) |
                                        (dev->buffer[pos++] << 8);
                dev->drv->sector_size = (dev->drv->sector_size & 0x00ffff00) |
                                        (dev->buffer[pos]);
                scsi_cdrom_log(dev->log, "Sector size now %i bytes\n",
                               dev->drv->sector_size);

                error |= scsi_cdrom_update_sector_flags(dev);
            }

            pos = hdr_len + block_desc_len;

#ifdef ENABLE_SCSI_CDROM_LOG
            for (uint16_t j = 0; j < pos; j++)
                scsi_cdrom_log(dev->log, "Buffer Mode Select, pos=%d, data=%02x.\n",
                               j, dev->buffer[j]);
#endif
            if (!error)  while (1) {
                if (pos >= param_list_len) {
                    scsi_cdrom_log(dev->log, "Buffer has only block descriptor\n");
                    break;
                }

                page     = dev->buffer[pos] & 0x3F;
                page_len = dev->buffer[pos + 1];

                pos += 2;

                if (dev->is_sony && (page == 0x08) && (page_len == 0x02))
                    dev->drv->sony_msf = dev->buffer[pos] & 0x01;

                if (!(dev->ms_page_flags & (1LL << ((uint64_t) page)))) {
                    scsi_cdrom_log(dev->log, "Unimplemented page %02X\n", page);
                    error |= 1;
                } else {
                    for (i = 0; i < page_len; i++) {
                        uint8_t pg = page;

                        if (dev->is_sony && (page == GPMODE_CDROM_AUDIO_PAGE_SONY) && (i >= 6) && (i <= 13))
                            pg = GPMODE_CDROM_AUDIO_PAGE;

                        ch      = dev->ms_pages_changeable.pages[pg][i + 2];
                        val     = dev->buffer[pos + i];
                        old_val = dev->ms_pages_saved.pages[pg][i + 2];
                        if (val != old_val) {
                            if (ch)
                                dev->ms_pages_saved.pages[pg][i + 2] = val;
                            else {
                                scsi_cdrom_log(dev->log, "Unchangeable value on position "
                                               "%02X on page %02X\n", i + 2, page);
                                scsi_cdrom_invalid_field_pl(dev, val);
                                error |= 1;
                            }
                        }
                    }
                }

                pos += page_len;

                val = dev->ms_pages_default.pages[page][0] & 0x80;

                if (dev->do_page_save && val)
                    scsi_cdrom_mode_sense_save(dev);

                if (pos >= dev->total_length)
                    break;
            }

            if (error) {
                scsi_cdrom_buf_free(dev);
                return 0;
            }
            break;
        case 0xc9:
            if (dev->is_sony) {
                for (i = 0; i < 18; i++) {
                    if ((i >= 8) && (i <= 15))
                        dev->ms_pages_saved.pages[GPMODE_CDROM_AUDIO_PAGE][i] =
                            dev->buffer[i];
                    else
                        dev->ms_pages_saved.pages[GPMODE_CDROM_AUDIO_PAGE_SONY][i] =
                            dev->buffer[i];
                }
            }
            break;

        default:
            break;
    }

    scsi_cdrom_command_stop((scsi_common_t *) dev);
    return 1;
}

static void
scsi_cdrom_close(void *priv)
{
    scsi_cdrom_t *dev = (scsi_cdrom_t *) priv;

    if (dev != NULL) {
        if (dev->tf != NULL)
            free(dev->tf);

        if (dev->log != NULL) {
            scsi_cdrom_log(dev->log, "Log closed\n");

            log_close(dev->log);
            dev->log = NULL;
        }

        free(dev);
    }
}

static int
scsi_cdrom_get_max(const ide_t *ide, UNUSED(const int ide_has_dma), const int type)
{
    const scsi_cdrom_t *dev         = (scsi_cdrom_t *) ide->sc;
    int                 ret;

    switch (type) {
        case TYPE_PIO:  case TYPE_SDMA:
        case TYPE_MDMA: case TYPE_UDMA:
            ret = cdrom_get_transfer_max(dev->drv->type, type);
            break;
        default:
            ret = -1;
            break;
    }

    return ret;
}

static int
scsi_cdrom_get_timings(const ide_t *ide, UNUSED(const int ide_has_dma), const int type)
{
    const scsi_cdrom_t *dev         = (scsi_cdrom_t *) ide->sc;
    int                 has_dma     = cdrom_has_dma(dev->drv->type);
    int                 ret;

    switch (type) {
        case TIMINGS_DMA:
            ret = has_dma ? 120 : 0;
            break;
        case TIMINGS_PIO:
            ret = has_dma ? 120 : 0;
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
scsi_cdrom_identify(const ide_t *ide, UNUSED(const int ide_has_dma))
{
    const scsi_cdrom_t *dev         = (scsi_cdrom_t *) ide->sc;
    char                model[2048] = { 0 };
    const int           has_dma     = cdrom_has_dma(dev->drv->type);

    cdrom_get_identify_model(dev->drv->type, model, dev->id);

    scsi_cdrom_log(dev->log, "ATAPI Identify: %s\n", model);

    if (dev->drv->is_early)
        ide->buffer[0] = 0x8000 | (5 << 8) | 0x80 | (1 << 5); /* ATAPI device, CD-ROM drive, removable media, interrupt DRQ */
    else
        ide->buffer[0] = 0x8000 | (5 << 8) | 0x80 | (2 << 5); /* ATAPI device, CD-ROM drive, removable media, accelerated DRQ */
    ide_padstr((char *) (ide->buffer + 10), "", 20);                                   /* Serial Number */

    ide_padstr((char *) (ide->buffer + 23), cdrom_get_revision(dev->drv->type), 8);    /* Firmware */
    ide_padstr((char *) (ide->buffer + 27), model, 40);                                /* Model */

    ide->buffer[49]  = 0x200;  /* LBA supported */
    ide->buffer[126] = 0xfffe; /* Interpret zero byte count limit as maximum length */

    if (has_dma) {
        ide->buffer[71] = 30;
        ide->buffer[72] = 30;
        ide->buffer[80] = 0x7e; /*ATA-1 to ATA-6 supported*/
        ide->buffer[81] = 0x19; /*ATA-6 revision 3a supported*/
    }
}

void
scsi_cdrom_drive_reset(const int c)
{
    cdrom_t       *drv = &cdrom[c];
    const uint8_t  scsi_bus = (drv->scsi_device_id >> 4) & 0x0f;
    const uint8_t  scsi_id  = drv->scsi_device_id & 0x0f;
    uint8_t        valid = 0;

    if (drv->bus_type == CDROM_BUS_SCSI) {
        /* Make sure to ignore any SCSI CD-ROM drive that has an out of range SCSI bus. */
        if (scsi_bus >= SCSI_BUS_MAX)
            return;

        /* Make sure to ignore any SCSI CD-ROM drive that has an out of range ID. */
        if (scsi_id >= SCSI_ID_MAX)
            return;
    }

    /* Make sure to ignore any ATAPI CD-ROM drive that has an out of range IDE channel. */
    if ((drv->bus_type == CDROM_BUS_ATAPI) && (drv->ide_channel > 7))
        return;

    if (drv->priv == NULL) {
        drv->priv          = (scsi_cdrom_t *) calloc(1, sizeof(scsi_cdrom_t));
        scsi_cdrom_t  *dev = (scsi_cdrom_t *) drv->priv;

        char n[1024]       = { 0 };

        sprintf(n, "CD-ROM %i SCSI ", c + 1);
        dev->log = log_open(n);
    }

    scsi_cdrom_t  *dev = (scsi_cdrom_t *) drv->priv;

    dev->id  = c;
    dev->drv = drv;

    dev->cur_lun = SCSI_LUN_USE_CDB;

    drv->insert      = scsi_cdrom_insert;
    drv->get_volume  = scsi_cdrom_get_volume;
    drv->get_channel = scsi_cdrom_get_channel;
    drv->close       = scsi_cdrom_close;

    drv->sector_size = 2048;
    (void) scsi_cdrom_update_sector_flags(dev);

    if (drv->bus_type == CDROM_BUS_SCSI) {
        char *vendor               = cdrom_get_vendor(dev->drv->type);

        dev->ven_cmd               = NULL;
        memset(dev->ven_cmd_is_data, 0x00, sizeof(dev->ven_cmd_is_data));
        dev->is_sony               = 0;
        dev->use_cdb_9             = 0;
        dev->ms_page_flags         = scsi_cdrom_ms_page_flags_scsi;
        dev->ms_pages_default      = scsi_cdrom_ms_pages_default_scsi;
        dev->ms_pages_changeable   = scsi_cdrom_ms_pages_changeable_scsi;

        if (!strcmp(vendor, "CHINON"))
            dev->ven_cmd               = scsi_cdrom_command_chinon;
        else if (!strcmp(vendor, "DEC") || !strcmp(vendor, "ShinaKen") ||
                 !strcmp(vendor, "SONY") || !strcmp(vendor, "TEXEL")) {
            dev->ven_cmd               = scsi_cdrom_command_dec_sony_texel;
            dev->ven_cmd_is_data[0xc0] = 1;
            dev->ven_cmd_is_data[0xc1] = 1;
            dev->ven_cmd_is_data[0xc2] = 1;
            dev->ven_cmd_is_data[0xc3] = 1;
            dev->is_sony               = 1;
            dev->ms_page_flags         = scsi_cdrom_ms_page_flags_sony_scsi;
            dev->ms_pages_default      = scsi_cdrom_ms_pages_default_sony_scsi;
            dev->ms_pages_changeable   = scsi_cdrom_ms_pages_changeable_sony_scsi;
        } else if (!strcmp(vendor, "MATSHITA"))
            dev->ven_cmd               = scsi_cdrom_command_matsushita;
        else if (!strcmp(vendor, "NEC")) {
            dev->ven_cmd               = scsi_cdrom_command_nec;
            dev->ven_cmd_is_data[0xdd] = 1;
            dev->ven_cmd_is_data[0xde] = 1;
        } else if (!strcmp(vendor, "PIONEER")) {
            dev->ven_cmd               = scsi_cdrom_command_pioneer;
            dev->ven_cmd_is_data[0xc1] = 1;
            dev->ven_cmd_is_data[0xc2] = 1;
            dev->ven_cmd_is_data[0xc3] = 1;
        } else if (!strcmp(vendor, "TOSHIBA")) {
            dev->ven_cmd               = scsi_cdrom_command_toshiba;
            dev->ven_cmd_is_data[0xc6] = 1;
            dev->ven_cmd_is_data[0xc7] = 1;
            dev->use_cdb_9             = 1;
        }

        if (dev->tf == NULL)
            dev->tf                  = (ide_tf_t *) calloc(1, sizeof(ide_tf_t));

        /* SCSI CD-ROM, attach to the SCSI bus. */
        scsi_device_t *sd        = &scsi_devices[scsi_bus][scsi_id];

        sd->sc                   = (scsi_common_t *) dev;
        sd->command              = scsi_cdrom_command;
        sd->request_sense        = scsi_cdrom_request_sense_for_scsi;
        sd->reset                = scsi_cdrom_reset;
        sd->phase_data_out       = scsi_cdrom_phase_data_out;
        sd->command_stop         = scsi_cdrom_command_stop;
        sd->type                 = SCSI_REMOVABLE_CDROM;

        valid                    = 1;

        scsi_cdrom_log(dev->log, "SCSI CD-ROM drive %i attached to SCSI ID %i\n",
                       c, cdrom[c].scsi_device_id);
    } else if (drv->bus_type == CDROM_BUS_ATAPI) {
        /* ATAPI CD-ROM, attach to the IDE bus. */
        ide_t *id = ide_get_drive(drv->ide_channel);

        /*
           If the IDE channel is initialized, we attach to it, otherwise, we do
           nothing - it's going to be a drive that's not attached to anything.
         */
        if (id) {
            dev->ven_cmd               = NULL;
            memset(dev->ven_cmd_is_data, 0x00, sizeof(dev->ven_cmd_is_data));
            dev->is_sony               = 0;
            dev->use_cdb_9             = 0;
            dev->ms_page_flags         = scsi_cdrom_ms_page_flags;
            dev->ms_pages_default      = scsi_cdrom_ms_pages_default;
            dev->ms_pages_changeable   = scsi_cdrom_ms_pages_changeable;

            id->sc                     = (scsi_common_t *) dev;
            dev->tf                    = id->tf;
            IDE_ATAPI_IS_EARLY         = dev->drv->is_early;
            id->get_max                = scsi_cdrom_get_max;
            id->get_timings            = scsi_cdrom_get_timings;
            id->identify               = scsi_cdrom_identify;
            id->stop                   = scsi_cdrom_stop;
            id->packet_command         = scsi_cdrom_command;
            id->device_reset           = scsi_cdrom_reset;
            id->phase_data_out         = scsi_cdrom_phase_data_out;
            id->command_stop           = scsi_cdrom_command_stop;
            id->bus_master_error       = scsi_cdrom_bus_master_error;
            id->interrupt_drq          = dev->drv->is_early;

            valid = 1;

            ide_atapi_attach(id);
        }

        scsi_cdrom_log(dev->log, "ATAPI CD-ROM drive %i attached to IDE channel %i\n",
                       c, cdrom[c].ide_channel);
    }

    if (valid)
        scsi_cdrom_init(dev);
}
