/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Sony MD DATA (MiniDisc) SCSI drives,
 *          the MDM-110 mechanism and the MDH-10 external drive.
 *
 *          Reconstructed from bus captures of a real MDH-10, raw
 *          responses of a real MDM-110 (Aaru device report), and
 *          Sony's MD DATA drivers for DOS, Windows 95 and Mac OS.
 *
 *          The disc is a raw image of its 70464 logical blocks of
 *          2048 bytes. The User TOC, which is outside the logical
 *          blocks, is kept next to it as <image>.utoc.bin, as Aaru
 *          reads and writes it.
 *
 * Authors: Natalia Portillo <claunia@claunia.com>
 *
 *          Copyright 2026 Natalia Portillo.
 */
#define _GNU_SOURCE
#include <inttypes.h>
#ifdef ENABLE_MD_LOG
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
#include <86box/md.h>

md_drive_t md_drives[MD_NUM];

/* Volume descriptor of the MiniDisc File System, 32 blocks into the data track. */
#define MD_VD_LBA        0x420
/* Clusters of the system and data tracks on an MD DATA disc. */
#define MD_FIRST_CLUSTER 50
#define MD_DATA_CLUSTER  82
#define MD_LEADOUT       2252

/* Additional sense codes not in scsi_device.h */
#define ASC_SAVING_PARAMETERS_NOT_SUPPORTED 0x39
#define ASC_MEDIUM_REMOVAL_PREVENTED        0x53

#define md_sense_error dev->sense[0]
#define md_sense_key   dev->sense[2]
#define md_asc         dev->sense[12]
#define md_ascq        dev->sense[13]

// clang-format off
static const uint8_t md_command_flags[0x100] = {
    [0x00]          = IMPLEMENTED | CHECK_READY,                /* TEST UNIT READY */
    [0x01]          = IMPLEMENTED | CHECK_READY,                /* REZERO UNIT */
    [0x03]          = IMPLEMENTED | ALLOW_UA,                   /* REQUEST SENSE */
    [0x04]          = IMPLEMENTED | CHECK_READY,                /* FORMAT UNIT */
    [0x08]          = IMPLEMENTED | CHECK_READY,                /* READ (6) */
    [0x0a]          = IMPLEMENTED | CHECK_READY,                /* WRITE (6) */
    [0x0b]          = IMPLEMENTED | CHECK_READY,                /* SEEK (6) */
    [0x12]          = IMPLEMENTED | ALLOW_UA,                   /* INQUIRY */
    [0x15]          = IMPLEMENTED,                              /* MODE SELECT (6) */
    [0x16 ... 0x17] = IMPLEMENTED,                              /* RESERVE, RELEASE */
    [0x1a]          = IMPLEMENTED,                              /* MODE SENSE (6) */
    [0x1b]          = IMPLEMENTED,                              /* START STOP UNIT */
    [0x1d]          = IMPLEMENTED,                              /* SEND DIAGNOSTIC */
    [0x1e]          = IMPLEMENTED,                              /* PREVENT/ALLOW MEDIUM REMOVAL */
    [0x25]          = IMPLEMENTED | CHECK_READY,                /* READ CAPACITY */
    [0x28]          = IMPLEMENTED | CHECK_READY,                /* READ (10) */
    [0x2a ... 0x2b] = IMPLEMENTED | CHECK_READY,                /* WRITE (10), SEEK (10) */
    [0x2e ... 0x2f] = IMPLEMENTED | CHECK_READY,                /* WRITE AND VERIFY (10), VERIFY (10) */
    [0x34 ... 0x36] = IMPLEMENTED | CHECK_READY,                /* PRE-FETCH, SYNCHRONIZE/LOCK CACHE */
    [0x45]          = IMPLEMENTED | CHECK_READY,                /* PLAY AUDIO (10) */
    [0x48]          = IMPLEMENTED | CHECK_READY,                /* PLAY AUDIO TRACK/INDEX */
    [0x4b]          = IMPLEMENTED | CHECK_READY,                /* PAUSE/RESUME */
    [0xd1 ... 0xd2] = IMPLEMENTED | CHECK_READY,                /* Read/write U-TOC */
    [0xd4 ... 0xd5] = IMPLEMENTED | CHECK_READY,                /* Read P-TOC, format result */
    [0xd6 ... 0xd7] = IMPLEMENTED,                              /* Stop playback, play position */
    [0xd8]          = IMPLEMENTED | CHECK_READY                 /* Disc type */
};

/*
   Mode pages of the MDM-110 as it returns them (Aaru device report), and of the MDH-10 as captured on the bus
   from a real drive.
 */
static const mode_sense_pages_t mdm110_mode_sense_pages_default = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE, 0x0a, 0x80, 0x03, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00 },
      [0x07] = { 0x07,                  0x0a, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x08] = { GPMODE_CACHING_PAGE,   0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x20] = { 0x20,                  0x0e, 0x00, 0x00, 0x00, 0x20, 0x08, 0x00, 0x64, 0x14, 0x00, 0x00,
                 0x00, 0x00, 0x00, 0x00 } }
};

static const mode_sense_pages_t mdh10_mode_sense_pages_default = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE, 0x0a, 0x80, 0x05, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00 },
      [0x07] = { 0x07,                  0x0a, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x08] = { GPMODE_CACHING_PAGE,   0x0a, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x20] = { 0x20,                  0x0e, 0x00, 0x00, 0x00, 0x20, 0x09, 0x20, 0x25, 0x00, 0x00, 0x00,
                 0x00, 0x00, 0x00, 0x00 },
      /* Audio volume, channel 1 and 2 */
      [0x21] = { 0x21,                  0x0e, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x10, 0x02, 0x10,
                 0x00, 0x00, 0x00, 0x00 } }
};

/* What MODE SELECT can change, as the MDM-110 reports it. */
static const mode_sense_pages_t md_mode_sense_pages_changeable = {
    { [0x01] = { GPMODE_R_W_ERROR_PAGE, 0x0a, 0xb7, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00 },
      [0x07] = { 0x07,                  0x0a, 0x07, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x08] = { GPMODE_CACHING_PAGE,   0x0a, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
      [0x20] = { 0x20,                  0x0e, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00,
                 0x00, 0x00, 0x00, 0x00 },
      [0x21] = { 0x21,                  0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xff,
                 0x00, 0x00, 0x00, 0x00 } }
};
// clang-format on

static void md_command_complete(md_t *dev);
static void md_init(md_t *dev);

#ifdef ENABLE_MD_LOG
int md_do_log = ENABLE_MD_LOG;

static void
md_log(void *priv, const char *fmt, ...)
{
    va_list ap;

    if (md_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define md_log(priv, fmt, ...)
#endif

static uint64_t
md_page_flags(const md_t *dev)
{
    uint64_t flags = GPMODEP_R_W_ERROR_PAGE | (1ULL << 0x07) | GPMODEP_CACHING_PAGE | (1ULL << 0x20) |
                     GPMODEP_ALL_PAGES;

    if (dev->drv->type == MD_TYPE_SONY_MDH10)
        flags |= (1ULL << 0x21);

    return flags;
}

static const mode_sense_pages_t *
md_pages_default(const md_t *dev)
{
    return (dev->drv->type == MD_TYPE_SONY_MDH10) ? &mdh10_mode_sense_pages_default :
                                                    &mdm110_mode_sense_pages_default;
}

/* TOC helpers */

/* Packs a cluster, sector and sound group into a 24-bit big-endian TOC address. */
static void
md_toc_address(uint8_t *p, const uint32_t cluster, const uint32_t sector, const uint32_t sound_group)
{
    const uint32_t addr = (cluster << 10) | (sector << 4) | sound_group;

    p[0] = (addr >> 16) & 0xff;
    p[1] = (addr >> 8) & 0xff;
    p[2] = addr & 0xff;
}

/* P-TOC sector 0 of a recordable MD DATA disc. */
static void
md_build_ptoc(uint8_t *ptoc)
{
    memset(ptoc, 0, MD_TOC_SECTOR_SIZE);
    memcpy(ptoc + 0x08, "MINX", 4);        /* MD DATA */
    ptoc[0x0c] = 0x3e;                     /* Recordable disc */
    ptoc[0x0d] = 0x0d;
    ptoc[0x10] = MD_LEADOUT >> 8;          /* Lead-out cluster */
    ptoc[0x11] = MD_LEADOUT & 0xff;
    ptoc[0x13] = 0x01;                     /* Sectors in use: 0 */
    ptoc[0x15] = 0x02;
    ptoc[0x17] = 0x11;
    ptoc[0x19] = 0x03;
    ptoc[0x1d] = MD_FIRST_CLUSTER;         /* First recordable cluster */
}

/* The U-TOC written by Sony's formatters: a data track and a system track. */
static void
md_build_formatted_utoc(uint8_t *utoc)
{
    memset(utoc, 0, MD_UTOC_SECTORS * MD_TOC_SECTOR_SIZE);
    utoc[0x0c] = 0x01;                     /* Maker */
    utoc[0x0d] = 0x81;                     /* Model */
    utoc[0x0e] = 1;                        /* First track */
    utoc[0x0f] = 2;                        /* Last track */
    utoc[0x13] = 0x01;                     /* Sectors in use: 0 */
    utoc[0x1f] = 3;                        /* First empty part slot */
    utoc[0x21] = 1;                        /* Track 1: part slot 1 */
    utoc[0x22] = 2;                        /* Track 2: part slot 2 */

    /* Slot 1: data track */
    md_toc_address(utoc + 0x128, MD_DATA_CLUSTER, 0, 0);
    utoc[0x12b] = 0xd0;
    md_toc_address(utoc + 0x12c, MD_LEADOUT - 1, 31, 10);

    /* Slot 2: system track */
    md_toc_address(utoc + 0x130, MD_FIRST_CLUSTER, 0, 0);
    utoc[0x133] = 0xdc;
    md_toc_address(utoc + 0x134, MD_DATA_CLUSTER - 1, 31, 10);

    /* Empty slots 3 to 255 are chained */
    for (int i = 3; i < 255; i++)
        utoc[0x120 + (8 * i) + 7] = i + 1;
}

/* The U-TOC a formatter writes before formatting. */
static void
md_build_cleared_utoc(uint8_t *utoc)
{
    memset(utoc, 0, MD_UTOC_SECTORS * MD_TOC_SECTOR_SIZE);
    utoc[0x13] = 0x01;
}

/* Gets the first logical block of the data track from the U-TOC, or -1. */
static int32_t
md_data_track_start(const md_t *dev)
{
    const uint8_t *utoc  = dev->drv->utoc;
    const int      first = utoc[0x0e];
    const int      last  = utoc[0x0f];

    if ((first == 0) || (last < first))
        return -1;

    for (int track = first; track <= last; track++) {
        const int      part  = utoc[0x20 + track];
        const uint8_t *entry = utoc + 0x120 + (8 * part);

        if ((part == 0) || ((entry[3] & 0x5c) != 0x50))
            continue;

        const uint32_t addr    = (entry[0] << 16) | (entry[1] << 8) | entry[2];
        const uint32_t cluster = addr >> 10;
        const uint32_t sector  = (addr >> 4) & 0x3f;

        if ((cluster < MD_FIRST_CLUSTER) || (sector >= 32))
            continue;

        return (int32_t) (((cluster - MD_FIRST_CLUSTER) * 32) + sector);
    }

    return -1;
}

/* Gets the path of the U-TOC of an image, <image without extension>.utoc.bin */
static void
md_utoc_path(const char *image, char *utoc, const size_t len)
{
    const char *ext = path_get_extension((char *) image);

    if ((ext != NULL) && (*ext != '\0') && (ext > image)) {
        const size_t base = (size_t) (ext - image) - 1;
        snprintf(utoc, len, "%.*s.utoc.bin", (int) base, image);
    } else
        snprintf(utoc, len, "%s.utoc.bin", image);
}

static void
md_utoc_load(const md_t *dev)
{
    char     fn[MAX_IMAGE_PATH_LEN + 16];
    uint8_t  vd[8];

    md_utoc_path(dev->drv->image_path, fn, sizeof(fn));

    FILE *fp = plat_fopen(fn, "rb");

    memset(dev->drv->utoc, 0, sizeof(dev->drv->utoc));

    if (fp != NULL) {
        const size_t read = fread(dev->drv->utoc, 1, sizeof(dev->drv->utoc), fp);
        fclose(fp);

        if (read >= MD_TOC_SECTOR_SIZE)
            return;
    }

    /* No U-TOC: a disc with an MDFS volume has the formatters' one, anything else a cleared one */
    if ((fseeko64(dev->drv->fp, (uint64_t) MD_VD_LBA * MD_BLOCK_SIZE, SEEK_SET) == 0) &&
        (fread(vd, 1, sizeof(vd), dev->drv->fp) == sizeof(vd)) && !memcmp(vd + 1, "MD001", 5))
        md_build_formatted_utoc(dev->drv->utoc);
    else
        md_build_cleared_utoc(dev->drv->utoc);
}

static int
md_utoc_save(const md_t *dev)
{
    char fn[MAX_IMAGE_PATH_LEN + 16];
    int  sectors = 1;

    /* Keep the sectors in use */
    for (int i = 1; i < MD_UTOC_SECTORS; i++)
        if (dev->drv->utoc[0x13] & (1 << i))
            sectors = i + 1;

    md_utoc_path(dev->drv->image_path, fn, sizeof(fn));

    FILE *fp = plat_fopen(fn, "wb");

    if (fp == NULL) {
        md_log(dev->log, "Unable to write U-TOC to %s\n", fn);
        return 0;
    }

    const size_t len     = (size_t) sectors * MD_TOC_SECTOR_SIZE;
    const size_t written = fwrite(dev->drv->utoc, 1, len, fp);

    fclose(fp);

    return written == len;
}

/* Image handling */

static int
md_load_abort(const md_t *dev)
{
    if (dev->drv->fp)
        fclose(dev->drv->fp);
    dev->drv->fp          = NULL;
    dev->drv->medium_size = 0;
    md_eject(dev->id); /* Make sure the host OS knows we've rejected (and ejected) the image. */
    return 0;
}

int
md_create_image(const char *fn)
{
    FILE *fp = plat_fopen(fn, "wb");

    if (fp == NULL)
        return -1;

    /* A blank, unformatted disc reads back as zeros */
    int ret = (fseeko64(fp, ((uint64_t) MD_BLOCKS * MD_BLOCK_SIZE) - 1, SEEK_SET) == 0) &&
              (fputc(0, fp) != EOF);

    fclose(fp);

    if (ret) {
        /* Remove any stale U-TOC of a previous image of the same name */
        char utoc[MAX_IMAGE_PATH_LEN + 16];

        md_utoc_path(fn, utoc, sizeof(utoc));
        remove(utoc);
    }

    return ret ? 0 : -1;
}

int
md_is_empty(const uint8_t id)
{
    const md_t *dev = (const md_t *) md_drives[id].priv;

    return (dev == NULL) || (dev->drv == NULL) || (dev->drv->fp == NULL);
}

void
md_load(md_t *dev, const char *fn, const int skip_insert)
{
    const int was_empty = md_is_empty(dev->id);
    int       offs      = 0;
    int       ret       = 1;

    if (strstr(fn, "wp://") == fn) {
        offs                = 5;
        dev->drv->read_only = 1;
    }

    fn += offs;

    dev->drv->fp = plat_fopen(fn, dev->drv->read_only ? "rb" : "rb+");

    if (dev->drv->fp == NULL) {
        if (!dev->drv->read_only) {
            dev->drv->fp = plat_fopen(fn, "rb");
            if (dev->drv->fp == NULL)
                ret = md_load_abort(dev);
            else
                dev->drv->read_only = 1;
        } else
            ret = md_load_abort(dev);
    }

    if (ret) {
        fseeko64(dev->drv->fp, 0, SEEK_END);

        const uint64_t size = (uint64_t) ftello64(dev->drv->fp);

        if ((size == 0) || (size % MD_BLOCK_SIZE) || (size > ((uint64_t) MD_BLOCKS * MD_BLOCK_SIZE))) {
            md_log(dev->log, "Not an MD DATA image: %" PRIu64 " bytes\n", size);
            ret = md_load_abort(dev);
        } else {
            dev->drv->medium_size = (uint32_t) (size / MD_BLOCK_SIZE);

            if (dev->drv->image_path != (fn - offs)) {
                const size_t len = MIN(strlen(fn - offs), sizeof(dev->drv->image_path) - 1);
                memcpy(dev->drv->image_path, fn - offs, len);
                dev->drv->image_path[len] = '\0';
            }

            md_utoc_load(dev);
        }
    }

    if (ret && !skip_insert) {
        /* Signal media change to the emulated machine. */
        md_insert(dev);

        /* The drive was previously empty, transition directly to UNIT ATTENTION. */
        if (was_empty)
            md_insert(dev);
    }

    if (ret)
        ui_sb_update_icon_wp(SB_MD | dev->id, dev->drv->read_only);
}

void
md_disk_reload(const md_t *dev)
{
    if (strlen(dev->drv->prev_image_path) != 0)
        md_load((md_t *) dev, dev->drv->prev_image_path, 0);
}

static void
md_disk_unload(const md_t *dev)
{
    if ((dev->drv != NULL) && (dev->drv->fp != NULL)) {
        fclose(dev->drv->fp);
        dev->drv->fp = NULL;
    }
}

void
md_disk_close(const md_t *dev)
{
    if ((dev->drv != NULL) && (dev->drv->fp != NULL)) {
        md_disk_unload(dev);

        memcpy(dev->drv->prev_image_path, dev->drv->image_path, sizeof(dev->drv->image_path));
        memset(dev->drv->image_path, 0, sizeof(dev->drv->image_path));

        dev->drv->medium_size = 0;

        md_insert((md_t *) dev);
    }
}

void
md_insert(md_t *dev)
{
    if ((dev != NULL) && (dev->drv != NULL)) {
        if (dev->drv->fp == NULL) {
            dev->unit_attention = 0;
            dev->transition     = 0;
            md_log(dev->log, "Media removal\n");
        } else if (dev->transition) {
            dev->unit_attention = 1;
            /* Turn off the medium changed status. */
            dev->transition     = 0;
            md_log(dev->log, "Media insert\n");
        } else {
            dev->unit_attention = 0;
            dev->transition     = 1;
            md_log(dev->log, "Media transition\n");
        }
    }
}

/* Device state */

static void
md_init(md_t *dev)
{
    if (dev->id < MD_NUM) {
        dev->requested_blocks = 1;
        dev->sense[0]         = 0x70;
        dev->sense[7]         = 10;
        dev->tf->status       = READY_STAT | DSC_STAT;
        dev->tf->pos          = 0;
        dev->packet_status    = PHASE_NONE;
        dev->prevent          = 0;
        md_sense_key = md_asc = md_ascq = dev->unit_attention = dev->transition = 0;
    }
}

static void
md_mode_sense_load(md_t *dev)
{
    memcpy(&dev->ms_pages_saved, md_pages_default(dev), sizeof(mode_sense_pages_t));
}

static uint8_t
md_mode_sense_read(const md_t *dev, const uint8_t pgctl, const uint8_t page, const uint8_t pos)
{
    switch (pgctl) {
        case 0:
        case 3:
            return dev->ms_pages_saved.pages[page][pos];
        case 1:
            return md_mode_sense_pages_changeable.pages[page][pos];
        case 2:
            return md_pages_default(dev)->pages[page][pos];

        default:
            break;
    }

    return 0;
}

static uint32_t
md_mode_sense(const md_t *dev, uint8_t *buf, uint32_t pos, uint8_t page, const uint8_t block_descriptor_len)
{
    const uint64_t pf    = md_page_flags(dev);
    const uint8_t  pgctl = (page >> 6) & 3;

    page &= 0x3f;

    if (block_descriptor_len) {
        /* The drive reports no block count, and a changeable block length */
        memset(buf + pos, 0, 8);
        if (pgctl == 1) {
            buf[pos + 5] = 0xff;
            buf[pos + 6] = 0xff;
            buf[pos + 7] = 0xff;
        } else {
            buf[pos + 6] = (MD_BLOCK_SIZE >> 8) & 0xff;
            buf[pos + 7] = MD_BLOCK_SIZE & 0xff;
        }
        pos += 8;
    }

    for (uint8_t i = 0; i < GPMODE_ALL_PAGES; i++) {
        if (((page == GPMODE_ALL_PAGES) || (page == i)) && (pf & (1ULL << ((uint64_t) i)))) {
            const uint8_t msplen = md_mode_sense_read(dev, pgctl, i, 1);

            buf[pos++] = md_mode_sense_read(dev, pgctl, i, 0);
            buf[pos++] = msplen;
            for (uint8_t j = 0; j < msplen; j++)
                buf[pos++] = md_mode_sense_read(dev, pgctl, i, 2 + j);
        }
    }

    return pos;
}

/* Command phases */

static void
md_set_phase(const md_t *dev, const uint8_t phase)
{
    const uint8_t scsi_bus = (dev->drv->scsi_device_id >> 4) & 0x0f;
    const uint8_t scsi_id  = dev->drv->scsi_device_id & 0x0f;

    scsi_devices[scsi_bus][scsi_id].phase = phase;
}

static void
md_command_common(md_t *dev)
{
    dev->tf->status = BUSY_STAT;
    dev->tf->phase  = 1;
    dev->tf->pos    = 0;
    if (dev->packet_status == PHASE_COMPLETE)
        dev->callback = 0.0;
    else
        dev->callback = -1.0; /* Speed depends on SCSI controller */
}

static void
md_command_complete(md_t *dev)
{
    dev->packet_status = PHASE_COMPLETE;
    md_command_common(dev);
}

static void
md_command_read(md_t *dev)
{
    dev->packet_status = PHASE_DATA_IN;
    md_command_common(dev);
}

static void
md_command_write(md_t *dev)
{
    dev->packet_status = PHASE_DATA_OUT;
    md_command_common(dev);
}

/* direction: 0 = to the host, 1 = from the host. */
static void
md_data_command_finish(md_t *dev, int len, const int alloc_len, const int direction)
{
    dev->tf->pos = 0;

    if ((alloc_len >= 0) && (alloc_len < len))
        len = alloc_len;

    dev->packet_len = len;

    if (len == 0)
        md_command_complete(dev);
    else if (direction == 0)
        md_command_read(dev);
    else
        md_command_write(dev);
}

static void
md_buf_alloc(md_t *dev, const uint32_t len)
{
    if (dev->buffer == NULL) {
        dev->buffer    = (uint8_t *) calloc(1, len);
        dev->buffer_sz = len;
    }

    if (len > dev->buffer_sz) {
        dev->buffer    = (uint8_t *) realloc(dev->buffer, len);
        dev->buffer_sz = len;
    }
}

static void
md_buf_free(md_t *dev)
{
    if (dev->buffer) {
        free(dev->buffer);
        dev->buffer = NULL;
    }
}

static void
md_set_buf_len(const md_t *dev, int32_t *BufLen, int32_t *src_len)
{
    if (*BufLen == -1)
        *BufLen = *src_len;
    else {
        *BufLen  = MIN(*src_len, *BufLen);
        *src_len = *BufLen;
    }
    md_log(dev->log, "Actual transfer length: %i\n", *BufLen);
}

/* Errors */

static void
md_sense_clear(md_t *dev)
{
    memset(dev->sense, 0, 18);
    dev->sense[0] = 0x70;
    dev->sense[7] = 10;
}

/* Sets fixed-format sense as the drive returns it and finishes the command with CHECK CONDITION. */
static void
md_error(md_t *dev, const uint8_t key, const uint8_t asc, const uint8_t ascq, const uint8_t fru,
         const int info_valid, const uint32_t info, const uint8_t *sks)
{
    md_sense_clear(dev);

    dev->sense[0]  = info_valid ? 0xf0 : 0x70;
    dev->sense[2]  = key;
    if (info_valid) {
        dev->sense[3] = (info >> 24) & 0xff;
        dev->sense[4] = (info >> 16) & 0xff;
        dev->sense[5] = (info >> 8) & 0xff;
        dev->sense[6] = info & 0xff;
    }
    dev->sense[12] = asc;
    dev->sense[13] = ascq;
    dev->sense[14] = fru;
    if (sks != NULL)
        memcpy(dev->sense + 15, sks, 3);

    md_set_phase(dev, SCSI_PHASE_STATUS);
    dev->tf->error     = ((key & 0xf) << 4) | ABRT_ERR;
    dev->tf->status    = READY_STAT | ERR_STAT;
    dev->tf->phase     = 3;
    dev->tf->pos       = 0;
    dev->packet_status = PHASE_ERROR;
    dev->callback      = 50.0 * MD_TIME;
    ui_sb_update_icon(SB_MD | dev->id, 0);
    ui_sb_update_icon_write(SB_MD | dev->id, 0);
    md_log(dev->log, "[%02X] ERROR: %02X/%02X/%02X\n", dev->current_cdb[0], key, asc, ascq);
}

static void
md_unit_attention(md_t *dev)
{
    md_error(dev, SENSE_UNIT_ATTENTION, ASC_MEDIUM_MAY_HAVE_CHANGED, 0, 0xcc, 0, 0, NULL);
}

static void
md_not_ready(md_t *dev)
{
    md_error(dev, SENSE_NOT_READY, ASC_MEDIUM_NOT_PRESENT, 0, 0, 0, 0, NULL);
}

static void
md_write_protected(md_t *dev)
{
    md_error(dev, SENSE_DATA_PROTECT, ASC_WRITE_PROTECTED, 0, 0, 0, 0, NULL);
}

static void
md_write_error(md_t *dev)
{
    md_error(dev, SENSE_MEDIUM_ERROR, ASC_WRITE_ERROR, 0, 0, 1, dev->sector_pos, NULL);
}

static void
md_read_error(md_t *dev)
{
    md_error(dev, SENSE_MEDIUM_ERROR, ASC_UNRECOVERED_READ_ERROR, 0, 0, 1, dev->sector_pos, NULL);
}

static void
md_illegal_opcode(md_t *dev)
{
    md_error(dev, SENSE_ILLEGAL_REQUEST, ASC_ILLEGAL_OPCODE, 0, 0, 0, 0, NULL);
}

static void
md_invalid_lun(md_t *dev)
{
    md_error(dev, SENSE_ILLEGAL_REQUEST, ASC_INV_LUN, 0, 0, 0, 0, NULL);
}

static void
md_lba_out_of_range(md_t *dev)
{
    md_error(dev, SENSE_ILLEGAL_REQUEST, ASC_LBA_OUT_OF_RANGE, 0, 0, 1, dev->sector_pos, NULL);
}

/* Invalid field in the CDB, with the field pointer in the sense-key specific bytes. */
static void
md_invalid_field(md_t *dev, const int byte)
{
    const uint8_t sks[3] = { 0xc0, 0x00, (uint8_t) byte };

    md_error(dev, SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_CMD_PACKET, 0, 0xc7, 0, 0, sks);
}

static void
md_invalid_field_pl(md_t *dev)
{
    md_error(dev, SENSE_ILLEGAL_REQUEST, ASC_INV_FIELD_IN_PARAMETER_LIST, 0, 0, 0, 0, NULL);
}

/* Media access */

static int
md_blocks(md_t *dev, int32_t *len, const int out)
{
    *len = 0;

    if (dev->sector_len == 0) {
        md_command_complete(dev);
        return 0;
    }

    if (((uint64_t) dev->sector_pos + dev->requested_blocks) > dev->drv->medium_size) {
        md_lba_out_of_range(dev);
        return -1;
    }

    *len = dev->requested_blocks * MD_BLOCK_SIZE;

    if (fseeko64(dev->drv->fp, (uint64_t) dev->sector_pos * MD_BLOCK_SIZE, SEEK_SET) == -1) {
        out ? md_write_error(dev) : md_read_error(dev);
        return -1;
    }

    if (out) {
        if (fwrite(dev->buffer, 1, *len, dev->drv->fp) != (size_t) *len) {
            md_write_error(dev);
            return -1;
        }
        fflush(dev->drv->fp);
    } else {
        const size_t read = fread(dev->buffer, 1, *len, dev->drv->fp);

        /* A short image reads back as unwritten sectors */
        if (read < (size_t) *len)
            memset(dev->buffer + read, 0, *len - read);
    }

    dev->sector_pos += dev->requested_blocks;
    dev->sector_len -= dev->requested_blocks;

    return 1;
}

/* Erases the disc and lays down the MD DATA U-TOC, as the drive does on FORMAT UNIT. */
static int
md_format(md_t *dev)
{
    static const uint8_t zeros[65536] = { 0 };
    const uint64_t       total        = (uint64_t) dev->drv->medium_size * MD_BLOCK_SIZE;

    md_log(dev->log, "Formatting media...\n");

    if (fseeko64(dev->drv->fp, 0, SEEK_SET) == -1)
        return 0;

    for (uint64_t done = 0; done < total;) {
        const size_t n = (size_t) MIN(total - done, sizeof(zeros));

        if (fwrite(zeros, 1, n, dev->drv->fp) != n)
            return 0;

        done += n;
    }

    fflush(dev->drv->fp);

    md_build_formatted_utoc(dev->drv->utoc);

    return md_utoc_save(dev);
}

/* Command processing */

static int
md_pre_execution_check(md_t *dev, const uint8_t *cdb)
{
    int ready;

    if ((cdb[0] != GPCMD_REQUEST_SENSE) && (dev->cur_lun == SCSI_LUN_USE_CDB) && (cdb[1] & 0xe0)) {
        md_invalid_lun(dev);
        return 0;
    }

    if (!(md_command_flags[cdb[0]] & IMPLEMENTED)) {
        md_log(dev->log, "Unknown command %02X\n", cdb[0]);
        md_illegal_opcode(dev);
        return 0;
    }

    if (dev->transition) {
        if ((cdb[0] == GPCMD_TEST_UNIT_READY) || (cdb[0] == GPCMD_REQUEST_SENSE))
            ready = 0;
        else {
            if (!(md_command_flags[cdb[0]] & ALLOW_UA))
                md_insert(dev);

            ready = (dev->drv->fp != NULL);
        }
    } else
        ready = (dev->drv->fp != NULL);

    /* A drive without a disc has no reason to keep the UNIT ATTENTION, it only marks disc changes. */
    if (!ready && (dev->unit_attention > 0))
        dev->unit_attention = 0;

    if (dev->unit_attention == 1) {
        if (!(md_command_flags[cdb[0]] & ALLOW_UA)) {
            dev->unit_attention++;
            md_unit_attention(dev);
            return 0;
        }
    } else if ((dev->unit_attention == 2) && (cdb[0] != GPCMD_REQUEST_SENSE))
        dev->unit_attention = 0;

    /* Sense only survives until the next command. */
    if (cdb[0] != GPCMD_REQUEST_SENSE)
        md_sense_clear(dev);

    if (!ready && (md_command_flags[cdb[0]] & CHECK_READY)) {
        md_not_ready(dev);
        return 0;
    }

    return 1;
}

void
md_reset(scsi_common_t *sc)
{
    md_t *dev = (md_t *) sc;

    dev->sector_pos         = 0;
    dev->sector_len         = 0;
    dev->tf->status         = 0;
    dev->callback           = 0.0;
    dev->tf->phase          = 1;
    dev->tf->request_length = 0xeb14;
    dev->packet_status      = PHASE_NONE;
    dev->cur_lun            = SCSI_LUN_USE_CDB;
    dev->prevent            = 0;
    md_sense_clear(dev);
    dev->unit_attention = dev->transition = 0;
}

static void
md_request_sense(md_t *dev, uint8_t *buffer, const uint8_t alloc_length)
{
    if (alloc_length != 0) {
        memset(buffer, 0x00, alloc_length);
        memcpy(buffer, dev->sense, MIN(alloc_length, 18));
    }

    if (dev->unit_attention && (md_sense_key == 0)) {
        buffer[0]  = 0x70;
        buffer[2]  = SENSE_UNIT_ATTENTION;
        buffer[7]  = 10;
        buffer[12] = ASC_MEDIUM_MAY_HAVE_CHANGED;
        buffer[13] = 0;
        buffer[14] = 0xcc;
    }

    /* The last remaining sense is the unit attention, clear that condition. */
    if (buffer[2] == SENSE_UNIT_ATTENTION)
        dev->unit_attention = 0;

    md_sense_clear(dev);

    if (dev->transition)
        md_insert(dev);
}

static void
md_request_sense_for_scsi(scsi_common_t *sc, uint8_t *buffer, uint8_t alloc_length)
{
    md_t *dev = (md_t *) sc;

    if ((dev->drv->fp == NULL) && dev->unit_attention)
        dev->unit_attention = 0;

    md_request_sense(dev, buffer, alloc_length);
}

/* Returns data to the host. */
static void
md_data_in(md_t *dev, int32_t *BufLen, int32_t len, const int32_t alloc_len)
{
    len = MIN(len, alloc_len);
    md_set_buf_len(dev, BufLen, &len);
    md_data_command_finish(dev, len, alloc_len, 0);
}

/* Gets data from the host, processed by md_phase_data_out(). */
static void
md_data_out(md_t *dev, int32_t *BufLen, int32_t len)
{
    md_set_phase(dev, SCSI_PHASE_DATA_OUT);
    md_buf_alloc(dev, MAX(len, 1));
    md_set_buf_len(dev, BufLen, &len);
    dev->total_length = len;
    md_data_command_finish(dev, len, len, 1);
}

static void
md_status_only(md_t *dev)
{
    md_set_phase(dev, SCSI_PHASE_STATUS);
    md_command_complete(dev);
}

static void
md_command(scsi_common_t *sc, const uint8_t *cdb)
{
    md_t         *dev      = (md_t *) sc;
    const uint8_t scsi_bus = (dev->drv->scsi_device_id >> 4) & 0x0f;
    const uint8_t scsi_id  = dev->drv->scsi_device_id & 0x0f;
    int32_t      *BufLen   = &scsi_devices[scsi_bus][scsi_id].buffer_length;
    int32_t       len;
    int32_t       alloc_len;
    int           ret;
    uint32_t      sector;

    dev->tf->status &= ~ERR_STAT;
    dev->packet_len  = 0;
    dev->request_pos = 0;

    memcpy(dev->current_cdb, cdb, 12);

    md_log(dev->log, "CDB: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
           cdb[0], cdb[1], cdb[2], cdb[3], cdb[4], cdb[5], cdb[6], cdb[7], cdb[8], cdb[9]);

    dev->sector_len = 0;

    md_set_phase(dev, SCSI_PHASE_STATUS);

    if (md_pre_execution_check(dev, cdb) == 0)
        return;

    switch (cdb[0]) {
        case GPCMD_SEND_DIAGNOSTIC:
        case GPCMD_SCSI_RESERVE:
        case GPCMD_SCSI_RELEASE:
        case GPCMD_TEST_UNIT_READY:
        case GPCMD_REZERO_UNIT:
        case 0x34: /* PRE-FETCH */
        case 0x36: /* LOCK/UNLOCK CACHE */
        case 0x45: /* PLAY AUDIO (10): audio is not emulated */
        case 0x48: /* PLAY AUDIO TRACK/INDEX */
        case 0x4b: /* PAUSE/RESUME */
        case 0xd6: /* Stop audio playback */
            md_status_only(dev);
            break;

        case 0x35: /* SYNCHRONIZE CACHE */
            fflush(dev->drv->fp);
            md_status_only(dev);
            break;

        case GPCMD_REQUEST_SENSE:
            md_set_phase(dev, SCSI_PHASE_DATA_IN);
            alloc_len = cdb[4] ? cdb[4] : 4;
            md_buf_alloc(dev, 256);
            md_request_sense(dev, dev->buffer, (uint8_t) MIN(alloc_len, 18));
            md_data_in(dev, BufLen, 18, alloc_len);
            break;

        case GPCMD_INQUIRY:
            if (cdb[1] & 1) {
                md_invalid_field(dev, 1);
                break;
            }

            md_set_phase(dev, SCSI_PHASE_DATA_IN);
            md_buf_alloc(dev, 36);
            memset(dev->buffer, 0, 36);
            dev->buffer[0] = 0x10;       /* Peripheral device type, as the drive reports it */
            dev->buffer[1] = 0x85;       /* Removable, type modifier 5 */
            dev->buffer[2] = 0x02;       /* SCSI-2 */
            dev->buffer[3] = 0x01;       /* Response data format */
            dev->buffer[4] = 31;
            ide_padstr8(dev->buffer + 8, 8, md_drive_types[dev->drv->type].vendor);
            ide_padstr8(dev->buffer + 16, 16, md_drive_types[dev->drv->type].model);
            ide_padstr8(dev->buffer + 32, 4, md_drive_types[dev->drv->type].revision);
            md_data_in(dev, BufLen, 36, cdb[4]);
            break;

        case GPCMD_FORMAT_UNIT:
            if (dev->drv->read_only) {
                md_write_protected(dev);
                break;
            }

            /* With FmtData the parameter list header follows */
            if (cdb[1] & 0x10) {
                md_data_out(dev, BufLen, 4);
                break;
            }

            if (!md_format(dev))
                md_write_error(dev);
            else
                md_status_only(dev);
            break;

        case GPCMD_READ_6:
        case GPCMD_READ_10:
            md_set_phase(dev, SCSI_PHASE_DATA_IN);

            if (cdb[0] == GPCMD_READ_6) {
                dev->sector_len = cdb[4] ? cdb[4] : 256;
                dev->sector_pos = ((cdb[1] & 0x1f) << 16) | (cdb[2] << 8) | cdb[3];
            } else {
                dev->sector_len = (cdb[7] << 8) | cdb[8];
                dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
            }

            if (dev->sector_len == 0) {
                md_status_only(dev);
                break;
            }

            dev->requested_blocks = dev->sector_len;
            md_buf_alloc(dev, dev->sector_len * MD_BLOCK_SIZE);

            ret = md_blocks(dev, &len, 0);

            if (ret > 0) {
                md_set_buf_len(dev, BufLen, &len);
                md_data_command_finish(dev, len, len, 0);
                ui_sb_update_icon(SB_MD | dev->id, dev->packet_status != PHASE_COMPLETE);
            }
            break;

        case GPCMD_WRITE_6:
        case GPCMD_WRITE_10:
        case GPCMD_WRITE_AND_VERIFY_10:
            if (dev->drv->read_only) {
                md_write_protected(dev);
                break;
            }

            if (cdb[0] == GPCMD_WRITE_6) {
                dev->sector_len = cdb[4] ? cdb[4] : 256;
                dev->sector_pos = ((cdb[1] & 0x1f) << 16) | (cdb[2] << 8) | cdb[3];
            } else {
                /* Control byte bit 7 is a hint for newly allocated clusters, it changes nothing here */
                dev->sector_len = (cdb[7] << 8) | cdb[8];
                dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
            }

            if (dev->sector_len == 0) {
                md_status_only(dev);
                break;
            }

            if (((uint64_t) dev->sector_pos + dev->sector_len) > dev->drv->medium_size) {
                md_lba_out_of_range(dev);
                break;
            }

            dev->requested_blocks = dev->sector_len;
            md_data_out(dev, BufLen, dev->sector_len * MD_BLOCK_SIZE);
            ui_sb_update_icon_write(SB_MD | dev->id, dev->packet_status != PHASE_COMPLETE);
            break;

        case GPCMD_VERIFY_10:
            dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
            dev->sector_len = (cdb[7] << 8) | cdb[8];

            if (((uint64_t) dev->sector_pos + dev->sector_len) > dev->drv->medium_size)
                md_lba_out_of_range(dev);
            else
                md_status_only(dev);
            break;

        case GPCMD_SEEK_6:
        case GPCMD_SEEK_10:
            if (cdb[0] == GPCMD_SEEK_6)
                sector = ((cdb[1] & 0x1f) << 16) | (cdb[2] << 8) | cdb[3];
            else
                sector = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];

            dev->sector_pos = sector;

            if (sector >= dev->drv->medium_size)
                md_lba_out_of_range(dev);
            else
                md_status_only(dev);
            break;

        case GPCMD_READ_CDROM_CAPACITY:
            md_set_phase(dev, SCSI_PHASE_DATA_IN);
            md_buf_alloc(dev, 8);
            memset(dev->buffer, 0, 8);
            /* What's returned is the last block */
            len            = dev->drv->medium_size - 1;
            dev->buffer[0] = (len >> 24) & 0xff;
            dev->buffer[1] = (len >> 16) & 0xff;
            dev->buffer[2] = (len >> 8) & 0xff;
            dev->buffer[3] = len & 0xff;
            dev->buffer[6] = (MD_BLOCK_SIZE >> 8) & 0xff;
            dev->buffer[7] = MD_BLOCK_SIZE & 0xff;
            md_data_in(dev, BufLen, 8, 8);
            break;

        case GPCMD_MODE_SENSE_6: {
            const int block_desc = !(cdb[1] & 0x08);
            const int pgctl      = (cdb[2] >> 6) & 3;

            if (pgctl == 3) {
                md_error(dev, SENSE_ILLEGAL_REQUEST, ASC_SAVING_PARAMETERS_NOT_SUPPORTED, 0, 0, 0, 0, NULL);
                break;
            }

            if (!(md_page_flags(dev) & (1ULL << (uint64_t) (cdb[2] & 0x3f)))) {
                md_invalid_field(dev, 2);
                break;
            }

            md_set_phase(dev, SCSI_PHASE_DATA_IN);
            md_buf_alloc(dev, 256);
            memset(dev->buffer, 0, 256);

            len            = md_mode_sense(dev, dev->buffer, 4, cdb[2], block_desc);
            dev->buffer[0] = len - 1;
            dev->buffer[1] = (dev->drv->fp != NULL) ? 0x03 : 0x00;  /* Medium type: MD DATA */
            /* Write protected, also without a disc */
            dev->buffer[2] = ((dev->drv->fp == NULL) || dev->drv->read_only) ? 0x80 : 0x00;
            dev->buffer[3] = block_desc ? 8 : 0;

            md_data_in(dev, BufLen, len, cdb[4]);
            break;
        }

        case GPCMD_MODE_SELECT_6:
            dev->do_page_save = cdb[1] & 1;
            md_data_out(dev, BufLen, cdb[4]);
            break;

        case GPCMD_START_STOP_UNIT:
            if ((cdb[4] & 3) == 2) {
                /* Eject */
                if (dev->prevent) {
                    md_error(dev, SENSE_ILLEGAL_REQUEST, ASC_MEDIUM_REMOVAL_PREVENTED, 2, 0, 0, 0, NULL);
                    break;
                }
                md_status_only(dev);
                md_eject(dev->id);
            } else if ((cdb[4] & 3) == 3) {
                /* Load */
                md_status_only(dev);
                md_reload(dev->id);
            } else
                md_status_only(dev);
            break;

        case GPCMD_PREVENT_REMOVAL:
            dev->prevent = cdb[4] & 1;
            md_status_only(dev);
            break;

        case 0xd1: /* Read U-TOC sectors */
            sector    = cdb[5];
            alloc_len = (cdb[7] << 8) | cdb[8];

            if (sector >= MD_UTOC_SECTORS) {
                md_invalid_field(dev, 5);
                break;
            }

            md_set_phase(dev, SCSI_PHASE_DATA_IN);
            len = (MD_UTOC_SECTORS - sector) * MD_TOC_SECTOR_SIZE;
            md_buf_alloc(dev, len);
            memcpy(dev->buffer, dev->drv->utoc + (sector * MD_TOC_SECTOR_SIZE), len);
            md_data_in(dev, BufLen, len, alloc_len);
            break;

        case 0xd2: /* Write U-TOC sectors */
            if (dev->drv->read_only) {
                md_write_protected(dev);
                break;
            }

            if (cdb[5] >= MD_UTOC_SECTORS) {
                md_invalid_field(dev, 5);
                break;
            }

            md_data_out(dev, BufLen, MIN((cdb[7] << 8) | cdb[8], (MD_UTOC_SECTORS - cdb[5]) * MD_TOC_SECTOR_SIZE));
            break;

        case 0xd4: /* Read a P-TOC sector, recordable discs only have sector 0 */
            if (cdb[5] != 0) {
                md_invalid_field(dev, 5);
                break;
            }

            md_set_phase(dev, SCSI_PHASE_DATA_IN);
            md_buf_alloc(dev, MD_TOC_SECTOR_SIZE);
            md_build_ptoc(dev->buffer);
            md_data_in(dev, BufLen, MD_TOC_SECTOR_SIZE, (cdb[7] << 8) | cdb[8]);
            break;

        case 0xd5: /* Result of the last format: no defect entries */
        case 0xd7: /* Audio play position: not playing */
            alloc_len = (cdb[7] << 8) | cdb[8];
            len       = (cdb[0] == 0xd7) ? 4 : MAX(alloc_len, 4);

            md_set_phase(dev, SCSI_PHASE_DATA_IN);
            md_buf_alloc(dev, len);
            memset(dev->buffer, 0, len);
            md_data_in(dev, BufLen, len, alloc_len);
            break;

        case 0xd8: { /* Disc type: 01h and the first block of the data track on MD DATA discs */
            const int32_t start = md_data_track_start(dev);

            md_set_phase(dev, SCSI_PHASE_DATA_IN);
            md_buf_alloc(dev, 8);
            memset(dev->buffer, 0, 8);

            if (start < 0)
                dev->buffer[0] = 0xff;
            else {
                dev->buffer[0] = 0x01;
                dev->buffer[4] = (start >> 24) & 0xff;
                dev->buffer[5] = (start >> 16) & 0xff;
                dev->buffer[6] = (start >> 8) & 0xff;
                dev->buffer[7] = start & 0xff;
            }

            md_data_in(dev, BufLen, 8, (cdb[7] << 8) | cdb[8]);
            break;
        }

        default:
            md_illegal_opcode(dev);
            break;
    }

    if ((dev->packet_status == PHASE_COMPLETE) || (dev->packet_status == PHASE_ERROR))
        md_buf_free(dev);
}

static void
md_command_stop(scsi_common_t *sc)
{
    md_t *dev = (md_t *) sc;

    md_command_complete(dev);
    md_buf_free(dev);
}

static uint8_t
md_mode_select(md_t *dev)
{
    const uint16_t param_list_len = dev->current_cdb[4];

    if (param_list_len == 0)
        return 1;

    if (param_list_len < 4) {
        md_invalid_field_pl(dev);
        return 0;
    }

    const uint8_t block_desc_len = dev->buffer[3];
    uint16_t      pos            = 4 + block_desc_len;

    if (pos > param_list_len) {
        md_invalid_field_pl(dev);
        return 0;
    }

    /* The block length can only be the one of the disc */
    if (block_desc_len >= 8) {
        const uint32_t block_len = (dev->buffer[9] << 16) | (dev->buffer[10] << 8) | dev->buffer[11];

        if ((block_len != 0) && (block_len != MD_BLOCK_SIZE)) {
            md_invalid_field_pl(dev);
            return 0;
        }
    }

    while ((pos + 2) <= param_list_len) {
        /* The PS bit, which Sony's Mac OS driver sends back, is ignored */
        const uint8_t page     = dev->buffer[pos] & 0x3f;
        const uint8_t page_len = dev->buffer[pos + 1];

        if (!(md_page_flags(dev) & (1ULL << (uint64_t) page)) || ((pos + 2 + page_len) > param_list_len)) {
            md_invalid_field_pl(dev);
            return 0;
        }

        for (uint8_t i = 0; (i < page_len) && ((i + 2) < 0x40); i++) {
            const uint8_t ch      = md_mode_sense_pages_changeable.pages[page][i + 2];
            const uint8_t old_val = dev->ms_pages_saved.pages[page][i + 2];
            const uint8_t val     = dev->buffer[pos + 2 + i];

            if (((val ^ old_val) & ~ch) != 0) {
                md_invalid_field_pl(dev);
                return 0;
            }

            dev->ms_pages_saved.pages[page][i + 2] = (old_val & ~ch) | (val & ch);
        }

        pos += 2 + page_len;
    }

    return 1;
}

/* The command second phase function, for commands that get data from the host. */
static uint8_t
md_phase_data_out(scsi_common_t *sc)
{
    md_t   *dev = (md_t *) sc;
    int32_t len = 0;

    switch (dev->current_cdb[0]) {
        case GPCMD_WRITE_6:
        case GPCMD_WRITE_10:
        case GPCMD_WRITE_AND_VERIFY_10:
            if ((dev->requested_blocks > 0) && (md_blocks(dev, &len, 1) < 0)) {
                md_buf_free(dev);
                return 0;
            }
            break;

        case GPCMD_FORMAT_UNIT:
            if (!md_format(dev)) {
                md_write_error(dev);
                md_buf_free(dev);
                return 0;
            }
            break;

        case GPCMD_MODE_SELECT_6:
            if (!md_mode_select(dev)) {
                md_buf_free(dev);
                return 0;
            }
            break;

        case 0xd2: {
            const uint32_t sector = dev->current_cdb[5];

            memcpy(dev->drv->utoc + (sector * MD_TOC_SECTOR_SIZE), dev->buffer, dev->total_length);

            if (!md_utoc_save(dev)) {
                md_write_error(dev);
                md_buf_free(dev);
                return 0;
            }
            break;
        }

        default:
            break;
    }

    md_command_stop((scsi_common_t *) dev);
    return 1;
}

/* Perform a master init on the entire module. */
void
md_global_init(void)
{
    memset(md_drives, 0x00, sizeof(md_drives));
}

static void
md_drive_reset(const int c)
{
    const uint8_t scsi_bus = (md_drives[c].scsi_device_id >> 4) & 0x0f;
    const uint8_t scsi_id  = md_drives[c].scsi_device_id & 0x0f;

    if (md_drives[c].priv == NULL) {
        md_drives[c].priv = (md_t *) calloc(1, sizeof(md_t));
        md_t *dev         = (md_t *) md_drives[c].priv;
        char  n[1024]     = { 0 };

        sprintf(n, "MD %i", c + 1);
        dev->log = log_open(n);
    }

    md_t *dev = (md_t *) md_drives[c].priv;

    dev->id      = c;
    dev->cur_lun = SCSI_LUN_USE_CDB;

    if (dev->tf == NULL)
        dev->tf = (ide_tf_t *) calloc(1, sizeof(ide_tf_t));

    scsi_device_t *sd = &scsi_devices[scsi_bus][scsi_id];

    sd->sc             = (scsi_common_t *) dev;
    sd->command        = md_command;
    sd->request_sense  = md_request_sense_for_scsi;
    sd->reset          = md_reset;
    sd->phase_data_out = md_phase_data_out;
    sd->command_stop   = md_command_stop;
    sd->type           = SCSI_REMOVABLE_DISK;
}

void
md_hard_reset(void)
{
    for (uint8_t c = 0; c < MD_NUM; c++) {
        if (md_drives[c].bus_type != MD_BUS_SCSI)
            continue;

        const uint8_t scsi_bus = (md_drives[c].scsi_device_id >> 4) & 0x0f;
        const uint8_t scsi_id  = md_drives[c].scsi_device_id & 0x0f;

        /* Make sure to ignore any drive that has an out of range SCSI bus or ID. */
        if ((scsi_bus >= SCSI_BUS_MAX) || (scsi_id >= SCSI_ID_MAX))
            continue;

        if (md_drives[c].type >= KNOWN_MD_DRIVE_TYPES)
            md_drives[c].type = MD_TYPE_SONY_MDH10;

        md_drive_reset(c);

        md_t *dev = (md_t *) md_drives[c].priv;

        dev->id  = c;
        dev->drv = &md_drives[c];

        md_init(dev);

        if (strlen(md_drives[c].image_path))
            md_load(dev, md_drives[c].image_path, 0);

        md_mode_sense_load(dev);

        md_log(dev->log, "SCSI MD drive %i attached to SCSI ID %i\n", c, md_drives[c].scsi_device_id);
    }
}

void
md_close(void)
{
    for (uint8_t c = 0; c < MD_NUM; c++) {
        if (md_drives[c].bus_type == MD_BUS_SCSI) {
            const uint8_t scsi_bus = (md_drives[c].scsi_device_id >> 4) & 0x0f;
            const uint8_t scsi_id  = md_drives[c].scsi_device_id & 0x0f;

            if ((scsi_bus < SCSI_BUS_MAX) && (scsi_id < SCSI_ID_MAX))
                memset(&scsi_devices[scsi_bus][scsi_id], 0x00, sizeof(scsi_device_t));
        }

        md_t *dev = (md_t *) md_drives[c].priv;

        if (dev) {
            md_disk_unload(dev);

            if (dev->tf)
                free(dev->tf);

            if (dev->log != NULL) {
                log_close(dev->log);
                dev->log = NULL;
            }

            md_buf_free(dev);
            free(dev);
            md_drives[c].priv = NULL;
        }
    }
}
