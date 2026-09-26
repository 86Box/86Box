/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the HxC MFM image format.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2018-2019 Miran Grca.
 */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/plat.h>
#include <86box/fdd.h>
#include <86box/fdd_86f.h>
#include <86box/fdd_mfm.h>
#include <86box/fdc.h>

#pragma pack(push, 1)
typedef struct mfm_header_t {
    uint8_t hdr_name[7];

    uint16_t tracks_no;
    uint8_t  sides_no;

    uint16_t rpm;
    uint16_t bit_rate;
    uint8_t  if_type;

    uint32_t track_list_offset;
} mfm_header_t;

typedef struct mfm_track_t {
    uint16_t track_no;
    uint8_t  side_no;
    uint32_t track_size;
    uint32_t track_offset;
} mfm_track_t;

typedef struct mfm_adv_track_t {
    uint16_t track_no;
    uint8_t  side_no;
    uint16_t rpm;
    uint16_t bit_rate;
    uint32_t track_size;
    uint32_t track_offset;
} mfm_adv_track_t;
#pragma pack(pop)

typedef struct mfm_t {
    FILE *fp;

    mfm_header_t     hdr;
    mfm_track_t     *tracks;
    mfm_adv_track_t *adv_tracks;

    uint16_t disk_flags;
    uint16_t pad;
    uint16_t side_flags[2];

    int br_rounded;
    int rpm_rounded;
    int total_tracks;
    int cur_track;

    uint8_t track_data[2][256 * 1024];
} mfm_t;

static mfm_t *mfm[FDD_NUM];

#ifdef ENABLE_MFM_LOG
int mfm_do_log = ENABLE_MFM_LOG;

static void
mfm_log(const char *fmt, ...)
{
    va_list ap;

    if (mfm_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define mfm_log(fmt, ...)
#endif

static int
get_track_index(void *priv, const int side, const int track)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    const mfm_t *dev = mfm[drv->id];
    int          ret = -1;

    for (int i = 0; i < dev->total_tracks; i++) {
        if ((dev->tracks[i].track_no == track) && (dev->tracks[i].side_no == side)) {
            ret = i;
            break;
        }
    }

    return ret;
}

static int
get_adv_track_index(void *priv, const int side, const int track)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    const mfm_t *dev = mfm[drv->id];
    int          ret = -1;

    for (int i = 0; i < dev->total_tracks; i++) {
        if ((dev->adv_tracks[i].track_no == track) && (dev->adv_tracks[i].side_no == side)) {
            ret = i;
            break;
        }
    }

    return ret;
}

static void
get_adv_track_bitrate(void *priv, const int side, const int track, int *br, int *rpm)
{
    fdd_drive_t *drv         = (fdd_drive_t *) priv;
    const mfm_t *dev         = mfm[drv->id];
    const int    track_index = get_adv_track_index(drv, side, track);

    if (track_index == -1) {
        *br         = 250;
        *rpm        = 300;
    } else {
        double dbr  = round(((double) dev->adv_tracks[track_index].bit_rate) / 50.0) * 50.0;
        *br         = ((int) dbr);
        dbr         = round(((double) dev->adv_tracks[track_index].rpm) / 60.0) * 60.0;
        *rpm        = ((int) dbr);
    }
}

static void
set_disk_flags(void *priv)
{
    fdd_drive_t *drv             = (fdd_drive_t *) priv;
    mfm_t *      dev             = mfm[drv->id];
    int          br              = 250;
    int          rpm             = 300;
    /*
       We ALWAYS claim to have extra bit cells, even if the actual amount is 0;
       Bit 12 = 1, bits 6, 5 = 0 - extra bit cells field specifies the entire
       amount of bit cells per track.
     */
    uint16_t     temp_disk_flags = 0x1080;

    /* If this is the modified MFM format, get bit rate (and RPM) from track 0 instead. */
    if (dev->hdr.if_type & 0x80)
        get_adv_track_bitrate(drv, 0, 0, &br, &rpm);
    else {
        br  = dev->br_rounded;
        rpm = dev->rpm_rounded;
    }

    switch (br) {
        default:
        case 250:
        case 300:
            temp_disk_flags |= 0;
            break;

        case 500:
            temp_disk_flags |= 2;
            break;

        case 1000:
            temp_disk_flags |= 4;
            break;
    }

    if (dev->hdr.sides_no == 2)
        temp_disk_flags |= 8;

    dev->disk_flags = temp_disk_flags;
}

static uint16_t
disk_flags(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    const mfm_t *dev = mfm[drv->id];

    return dev->disk_flags;
}

static void
set_side_flags(void *priv, const int side)
{
    fdd_drive_t *drv             = (fdd_drive_t *) priv;
    mfm_t   *    dev             = mfm[drv->id];
    uint16_t     temp_side_flags = 0;
    int          br              = 250;
    int          rpm             = 300;

    if (dev->hdr.if_type & 0x80)
        get_adv_track_bitrate(drv, side, dev->cur_track, &br, &rpm);
    else {
        br  = dev->br_rounded;
        rpm = dev->rpm_rounded;
    }

    /* 300 kbps @ 360 rpm = 250 kbps @ 200 rpm */
    if ((br == 300) && (rpm == 360)) {
        br  = 250;
        rpm = 300;
    }

    switch (br) {
        case 500:
            temp_side_flags = 0;
            break;

        case 300:
            temp_side_flags = 1;
            break;

        case 250:
        default:
            temp_side_flags = 2;
            break;

        case 1000:
            temp_side_flags = 3;
            break;
    }

    if (rpm == 360)
        temp_side_flags |= 0x20;

    /*
     * Set the encoding value to match that provided by the FDC.
     * Then if it's wrong, it will sector not found anyway.
     */
    temp_side_flags |= 0x08;

    dev->side_flags[side] = temp_side_flags;
}

static uint16_t
side_flags(void *priv)
{
    fdd_drive_t *drv  = (fdd_drive_t *) priv;
    const mfm_t *dev  = mfm[drv->id];

    const int    side = fdd_get_head(drv);

    return dev->side_flags[side];
}

static uint32_t
get_raw_size(void *priv, const int side)
{
    fdd_drive_t *drv         = (fdd_drive_t *) priv;
    const mfm_t *dev         = mfm[drv->id];
    int          br          = 250;
    int          rpm         = 300;
    int          track_index;

    if (dev->hdr.if_type & 0x80) {
        track_index = get_adv_track_index(drv, side, dev->cur_track);
        get_adv_track_bitrate(drv, 0, 0, &br, &rpm);
    } else {
        track_index = get_track_index(drv, side, dev->cur_track);
        br          = dev->br_rounded;
        rpm         = dev->rpm_rounded;
    }

    const int    is_300_rpm  = (rpm == 300);

    if (track_index == -1) {
        mfm_log("MFM: Unable to find track (%i, %i)\n", dev->cur_track, side);
        switch (br) {
            default:
            case 250:
                return is_300_rpm ? 100000 : 83333;
            case 300:
                return is_300_rpm ? 120000 : 100000;
            case 500:
                return is_300_rpm ? 200000 : 166666;
            case 1000:
                return is_300_rpm ? 400000 : 333333;
        }
    }

    /* Bit 7 on - my extension of the HxC MFM format to output exact bitcell counts
       for each track instead of rounded byte counts. */
    if (dev->hdr.if_type & 0x80)
        return dev->adv_tracks[track_index].track_size;
    else
        return dev->tracks[track_index].track_size * 8;
}

static int32_t
extra_bit_cells(void *priv, const int side)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    return (int32_t) get_raw_size(drv, side);
}

static uint16_t *
encoded_data(void *priv, const int side)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    mfm_t *      dev = mfm[drv->id];

    return ((uint16_t *) dev->track_data[side]);
}

void
mfm_read_side(void *priv, const int side)
{
    fdd_drive_t *drv         = (fdd_drive_t *) priv;
    mfm_t *      dev         = mfm[drv->id];
    int          track_index;

    if (dev->hdr.if_type & 0x80)
        track_index = get_adv_track_index(drv, side, dev->cur_track);
    else
        track_index = get_track_index(drv, side, dev->cur_track);

    const int    track_size  = (int) get_raw_size(drv, side);
    int          track_bytes = track_size >> 3;

    if (track_size & 0x07)
        track_bytes++;

    if (track_index == -1)
        memset(dev->track_data[side], 0x00, track_bytes);
    else {
        int ret;

        if (dev->hdr.if_type & 0x80)
            ret = fseek(dev->fp, (off_t) dev->adv_tracks[track_index].track_offset, SEEK_SET);
        else
            ret = fseek(dev->fp, (off_t) dev->tracks[track_index].track_offset, SEEK_SET);

        if (ret == -1)
            fatal("mfm_read_side(): Error seeking to the beginning of the file\n");

        if (fread(dev->track_data[side], 1, track_bytes, dev->fp) != track_bytes)
            fatal("mfm_read_side(): Error reading track bytes\n");
    }

    mfm_log("drive = %i, side = %i, dev->cur_track = %i, track_index = %i, track_size = %i\n",
            drive, side, dev->cur_track, track_index, track_size);
}

void
mfm_seek(void *priv, int track)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    mfm_t *      dev = mfm[drv->id];

    mfm_log("mfm_seek(%i, %i)\n", drive, track);

    if (fdd_doublestep_40(drv)) {
        if (dev->hdr.tracks_no <= 43)
            track /= 2;
    }

    dev->cur_track = track;
    d86f_set_cur_track(drv, track);

    if (dev->fp == NULL)
        return;

    mfm_read_side(drv, 0);
    mfm_read_side(drv, 1);

    set_side_flags(drv, 0);
    set_side_flags(drv, 1);
}

void
mfm_load(void *priv, char *fn)
{
    fdd_drive_t *drv  = (fdd_drive_t *) priv;

    drv->writeprot = drv->fwriteprot = 1;

    /* Allocate a drive block. */
    mfm_t *      dev  = (mfm_t *) calloc(1, sizeof(mfm_t));

    dev->fp = plat_fopen(fn, "rb");
    if (dev->fp == NULL) {
        free(dev);
        memset(drv->image_path, 0, sizeof(drv->image_path));
        return;
    }

    d86f_unregister(drv);

    /* Read the header. */
    int          size = sizeof(mfm_header_t);
    if (fread(&dev->hdr, 1, size, dev->fp) != size)
        fatal("mfm_load(): Error reading header\n");

    /* Calculate tracks * sides, allocate the tracks array, and read it. */
    dev->total_tracks = dev->hdr.tracks_no * dev->hdr.sides_no;
    if (dev->hdr.if_type & 0x80) {
        dev->adv_tracks = (mfm_adv_track_t *) calloc(dev->total_tracks, sizeof(mfm_adv_track_t));
        size            = (int) (dev->total_tracks * sizeof(mfm_adv_track_t));
        if (fread(dev->adv_tracks, 1, size, dev->fp) != size)
            fatal("mfm_load(): Error reading advanced tracks\n");
    } else {
        dev->tracks = (mfm_track_t *) calloc(dev->total_tracks, sizeof(mfm_track_t));
        size        = (int) (dev->total_tracks * sizeof(mfm_track_t));
        if (fread(dev->tracks, 1, size, dev->fp) != size)
            fatal("mfm_load(): Error reading tracks\n");
    }

    /* The chances of finding a HxC MFM image of a single-sided thin track
       disk are much smaller than the chances of finding a HxC MFM image
       incorrectly converted from a SCP image, erroneously indicating 1
       side and 80+ tracks instead of 2 sides and <= 43 tracks, so if we
       have detected such an image, convert the track numbers. */
    if ((dev->hdr.tracks_no > 43) && (dev->hdr.sides_no == 1)) {
        dev->hdr.tracks_no >>= 1;
        dev->hdr.sides_no <<= 1;

        for (int i = 0; i < dev->total_tracks; i++) {
            if (dev->hdr.if_type & 0x80) {
                dev->adv_tracks[i].side_no <<= 1;
                dev->adv_tracks[i].side_no |= (dev->adv_tracks[i].track_no & 1);
                dev->adv_tracks[i].track_no >>= 1;
            } else {
                dev->tracks[i].side_no <<= 1;
                dev->tracks[i].side_no |= (dev->tracks[i].track_no & 1);
                dev->tracks[i].track_no >>= 1;
            }
        }
    }

    if (!(dev->hdr.if_type & 0x80)) {
        double dbr      = round(((double) dev->hdr.bit_rate) / 50.0) * 50.0;
        dev->br_rounded = (int) dbr;
        mfm_log("Rounded bit rate: %i kbps\n", dev->br_rounded);

        if (dev->hdr.rpm != 0)
            dbr              = round(((double) dev->hdr.rpm) / 60.0) * 60.0;
        else
            dbr              = (dev->br_rounded == 300) ? 360.0 : 300.0;
        dev->rpm_rounded = (int) dbr;
        mfm_log("Rounded RPM: %i rpm\n", dev->rpm_rounded);
    }

    /* Set up the drive unit. */
    mfm[drv->id] = dev;

    set_disk_flags(drv);

    /* Attach this format to the D86F engine. */
    drv->d86f_handler.disk_flags        = disk_flags;
    drv->d86f_handler.side_flags        = side_flags;
    drv->d86f_handler.writeback         = null_writeback;
    drv->d86f_handler.set_sector        = null_set_sector;
    drv->d86f_handler.write_data        = null_write_data;
    drv->d86f_handler.format_conditions = null_format_conditions;
    drv->d86f_handler.extra_bit_cells   = extra_bit_cells;
    drv->d86f_handler.encoded_data      = encoded_data;
    drv->d86f_handler.read_revolution   = common_read_revolution;
    drv->d86f_handler.index_hole_pos    = null_index_hole_pos;
    drv->d86f_handler.get_raw_size      = get_raw_size;
    drv->d86f_handler.check_crc         = 1;
    d86f_set_version(drv, D86FVER);

    d86f_common_handlers(drv);

    drv->seek = mfm_seek;

    mfm_log("Loaded as MFM\n");
}

void
mfm_close(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    mfm_t *      dev = mfm[drv->id];

    if (dev == NULL)
        return;

    d86f_unregister(drv);

    drv->seek = NULL;

    if (dev->tracks)
        free(dev->tracks);

    if (dev->adv_tracks)
        free(dev->adv_tracks);

    if (dev->fp)
        fclose(dev->fp);

    /* Release the memory. */
    free(dev);
    mfm[drv->id] = NULL;
}
