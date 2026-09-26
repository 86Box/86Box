/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the FDI floppy stream image format
 *          interface to the FDI2RAW module.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2016-2018 Miran Grca.
 *          Copyright 2018 Fred N. van Kempen.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/plat.h>
#include <86box/fdd.h>
#include <86box/fdd_86f.h>
#include <86box/fdd_img.h>
#include <86box/fdd_fdi.h>
#include <86box/fdc.h>
#include <fdi2raw.h>

typedef struct fdi_t {
    FILE *fp;
    FDI  *h;

    int lasttrack;
    int sides;
    int track;
    int tracklen[2][4];
    int trackindex[2][4];

    uint8_t track_data[2][4][256 * 1024];
    uint8_t track_timing[2][4][256 * 1024];
} fdi_t;

static fdi_t *fdi[FDD_NUM];

#ifdef ENABLE_FDI_LOG
int fdi_do_log = ENABLE_FDI_LOG;

static void
fdi_log(const char *fmt, ...)
{
    va_list ap;

    if (fdi_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define fdi_log(fmt, ...)
#endif

static uint16_t
disk_flags(void *priv)
{
    fdd_drive_t *drv             = (fdd_drive_t *) priv;
    fdi_t *      dev             = fdi[drv->id];
    /* We ALWAYS claim to have extra bit cells, even if the actual amount is 0. */
    uint16_t     temp_disk_flags = 0x80;

    switch (fdi2raw_get_bit_rate(dev->h)) {
        case 500:
            temp_disk_flags |= 2;
            break;

        case 300:
        case 250:
            temp_disk_flags |= 0;
            break;

        case 1000:
            temp_disk_flags |= 4;
            break;

        default:
            temp_disk_flags |= 0;
    }

    if (dev->sides == 2)
        temp_disk_flags |= 8;

    /*
     * Tell the 86F handler that we will handle our
     * data to handle it in reverse byte endianness.
     */
    temp_disk_flags |= 0x800;

    return temp_disk_flags;
}

static uint16_t
side_flags(void *priv)
{
    fdd_drive_t *drv             = (fdd_drive_t *) priv;
    fdi_t *      dev             = fdi[drv->id];
    uint16_t     temp_side_flags = 0;

    switch (fdi2raw_get_bit_rate(dev->h)) {
        case 500:
            temp_side_flags = 0;
            break;

        case 300:
            temp_side_flags = 1;
            break;

        case 250:
            temp_side_flags = 2;
            break;

        case 1000:
            temp_side_flags = 3;
            break;

        default:
            temp_side_flags = 2;
    }

    if (fdi2raw_get_rotation(dev->h) == 360)
        temp_side_flags |= 0x20;

    /*
     * Set the encoding value to match that provided by the FDC.
     * Then if it's wrong, it will sector not found anyway.
     */
    temp_side_flags |= 0x08;

    return temp_side_flags;
}

static int
fdi_density(void *priv)
{
    fdd_drive_t *drv        = (fdd_drive_t *) priv;

    if (!fdc_is_mfm(drv->fdc))
        return 0;

    switch (fdc_get_bit_rate(drv->fdc)) {
        case 0:
            return 2;

        case 1: case 2:
            return 1;

        case 3: case 5:
            return 3;

        default:
            break;
    }

    return 1;
}

static int32_t
extra_bit_cells(void *priv, const int side)
{
    fdd_drive_t *drv        = (fdd_drive_t *) priv;
    const fdi_t *dev        = fdi[drv->id];
    int          density    = 0;
    int          raw_size   = 0;
    int          is_300_rpm = 0;

    density = fdi_density(drv);

    is_300_rpm = (fdd_getrpm(drv) == 300);

    switch (fdc_get_bit_rate(drv->fdc)) {
        case 0:
            raw_size = is_300_rpm ? 200000 : 166666;
            break;

        case 1:
            raw_size = is_300_rpm ? 120000 : 100000;
            break;

        case 2:
            raw_size = is_300_rpm ? 100000 : 83333;
            break;

        case 3:
        case 5:
            raw_size = is_300_rpm ? 400000 : 333333;
            break;

        default:
            raw_size = is_300_rpm ? 100000 : 83333;
    }

    return (dev->tracklen[side][density] - raw_size);
}

static void
read_revolution(void *priv)
{
    fdd_drive_t *drv   = (fdd_drive_t *) priv;
    fdi_t *      dev   = fdi[drv->id];
    const int    track = dev->track;
    int          den;

    if (track > dev->lasttrack) {
        for (den = 0; den < 4; den++) {
            memset(dev->track_data[0][den], 0, 106096);
            memset(dev->track_data[1][den], 0, 106096);
            dev->tracklen[0][den] = dev->tracklen[1][den] = 100000;
        }
        return;
    }

    for (den = 0; den < 4; den++) {
        for (int side = 0; side < dev->sides; side++) {
            const int c = fdi2raw_loadtrack(dev->h,
                                            (uint16_t *) dev->track_data[side][den],
                                            (uint16_t *) dev->track_timing[side][den],
                                            (track * dev->sides) + side,
                                            &dev->tracklen[side][den],
                                            &dev->trackindex[side][den], NULL,
                                            den);
            if (!c)
                memset(dev->track_data[side][den], 0, dev->tracklen[side][den]);
        }

        if (dev->sides == 1) {
            memset(dev->track_data[1][den], 0, 106096);
            dev->tracklen[1][den] = 100000;
        }
    }
}

static uint32_t
index_hole_pos(void *priv, const int side)
{
    fdd_drive_t *drv     = (fdd_drive_t *) priv;
    const fdi_t *dev     = fdi[drv->id];

    const int    density = fdi_density(drv);

    return (dev->trackindex[side][density]);
}

static uint32_t
get_raw_size(void *priv, const int side)
{
    fdd_drive_t *drv     = (fdd_drive_t *) priv;
    const fdi_t *dev     = fdi[drv->id];

    const int    density = fdi_density(drv);

    return (dev->tracklen[side][density]);
}

static uint16_t *
encoded_data(void *priv, const int side)
{
    fdd_drive_t *drv     = (fdd_drive_t *) priv;
    fdi_t *      dev     = fdi[drv->id];

    const int    density = fdi_density(drv);

    return ((uint16_t *) dev->track_data[side][density]);
}

void
fdi_seek(void *priv, int track)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    fdi_t *      dev = fdi[drv->id];

    if (fdd_doublestep_40(drv)) {
        if (fdi2raw_get_tpi(dev->h) < 2)
            track /= 2;
    }

    d86f_set_cur_track(drv, track);

    if (dev->fp == NULL)
        return;

    if (track < 0)
        track = 0;

    dev->track = track;

    read_revolution(drv);
}

void
fdi_load(void *priv, char *fn)
{
    fdd_drive_t *drv        = (fdd_drive_t *) priv;
    char         header[26];

    /* Allocate a drive block. */
    fdi_t *      dev        = (fdi_t *) calloc(1, sizeof(fdi_t));

    if (dev == NULL) {
        memset(drv->image_path, 0, sizeof(drv->image_path));
        return;
    }

    drv->writeprot = drv->fwriteprot = 1;

    d86f_unregister(drv);

    dev->fp = plat_fopen(fn, "rb");
    if (fread(header, 1, 25, dev->fp) != 25)
        fatal("fdi_load(): Error reading header\n");
    if (fseek(dev->fp, 0, SEEK_SET) == -1)
        fatal("fdi_load(): Error seeking to the beginning of the file\n");
    header[25] = 0;
    if (strcmp(header, "Formatted Disk Image file") != 0) {
        /* This is a Japanese FDI file. */
        fdi_log("fdi_load(): Japanese FDI file detected, redirecting to IMG loader\n");
        fclose(dev->fp);
        free(dev);
        img_load(drv, fn);
        return;
    }

    /* Set up the drive unit. */
    fdi[drv->id] = dev;

    dev->h         = fdi2raw_header(dev->fp);
    dev->lasttrack = fdi2raw_get_last_track(dev->h);
    dev->sides     = fdi2raw_get_last_head(dev->h) + 1;

    /* Attach this format to the D86F engine. */
    drv->d86f_handler.disk_flags        = disk_flags;
    drv->d86f_handler.side_flags        = side_flags;
    drv->d86f_handler.writeback         = null_writeback;
    drv->d86f_handler.set_sector        = null_set_sector;
    drv->d86f_handler.write_data        = null_write_data;
    drv->d86f_handler.format_conditions = null_format_conditions;
    drv->d86f_handler.extra_bit_cells   = extra_bit_cells;
    drv->d86f_handler.encoded_data      = encoded_data;
    drv->d86f_handler.read_revolution   = read_revolution;
    drv->d86f_handler.index_hole_pos    = index_hole_pos;
    drv->d86f_handler.get_raw_size      = get_raw_size;
    drv->d86f_handler.check_crc         = 1;
    d86f_set_version(drv, D86FVER);

    d86f_common_handlers(drv);

    drv->seek = fdi_seek;

    fdi_log("Loaded as FDI\n");
}

void
fdi_close(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    fdi_t *dev       = fdi[drv->id];

    if (dev == NULL)
        return;

    d86f_unregister(drv);

    drv->seek = NULL;

    if (dev->h)
        fdi2raw_header_free(dev->h);

    if (dev->fp)
        fclose(dev->fp);

    /* Release the memory. */
    free(dev);
    fdi[drv->id] = NULL;
}
