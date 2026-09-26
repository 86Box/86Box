/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the IMD floppy image format.
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2018-2019 Fred N. van Kempen.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/plat.h>
#include <86box/fdd.h>
#include <86box/fdd_86f.h>
#include <86box/fdd_imd.h>
#include <86box/fdc.h>

typedef struct imd_track_t {
    uint8_t  is_present;
    uint32_t file_offs;
    uint8_t  params[5];
    uint32_t r_map_offs;
    uint32_t c_map_offs;
    uint32_t h_map_offs;
    uint32_t n_map_offs;
    uint32_t data_offs;
    uint32_t sector_data_offs[255];
    uint32_t sector_data_size[255];
    uint32_t gap3_len;
    uint16_t side_flags;
    uint8_t  max_sector_size;
    uint8_t  spt;
} imd_track_t;

typedef struct imd_t {
    FILE       *fp;
    char       *buffer;
    uint32_t    start_offs;
    int         track_count;
    int         sides;
    int         track;
    uint16_t    disk_flags;
    int         track_width;
    imd_track_t tracks[256][2];
    uint16_t    current_side_flags[2];
    uint8_t     xdf_ordered_pos[256][2];
    uint8_t     interleave_ordered_pos[256][2];
    char       *current_data[2];
    uint8_t     track_buffer[2][25000];
} imd_t;

static imd_t *imd[FDD_NUM];

#ifdef ENABLE_IMD_LOG
int imd_do_log = ENABLE_IMD_LOG;

static void
imd_log(const char *fmt, ...)
{
    va_list ap;

    if (imd_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define imd_log(fmt, ...)
#endif

static uint32_t
get_raw_tsize(int side_flags, int slower_rpm)
{
    uint32_t size;

    switch (side_flags & 0x27) {
        case 0x22:
            size = slower_rpm ? 5314 : 5208;
            break;

        default:
        case 0x02:
        case 0x21:
            size = slower_rpm ? 6375 : 6250;
            break;

        case 0x01:
            size = slower_rpm ? 7650 : 7500;
            break;

        case 0x20:
            size = slower_rpm ? 10629 : 10416;
            break;

        case 0x00:
            size = slower_rpm ? 12750 : 12500;
            break;

        case 0x23:
            size = slower_rpm ? 21258 : 20833;
            break;

        case 0x03:
            size = slower_rpm ? 25500 : 25000;
            break;

        case 0x25:
            size = slower_rpm ? 42517 : 41666;
            break;

        case 0x05:
            size = slower_rpm ? 51000 : 50000;
            break;
    }

    return size;
}

static int
track_is_xdf(void *priv, const int side, const int track)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    imd_t *      dev = imd[drv->id];

    for (uint16_t i = 0; i < 256; i++)
        dev->xdf_ordered_pos[i][side] = 0;

    if (dev->tracks[track][side].params[2] & 0xC0)
        return 0;

    if ((dev->tracks[track][side].params[3] != 16) && (dev->tracks[track][side].params[3] != 19))
        return 0;

    const uint8_t *r_map = (uint8_t *) (dev->buffer + dev->tracks[track][side].r_map_offs);

    if (!track) {
        int            high_sectors        = 0;
        int            low_sectors         = 0;
        int            max_high_id;
        int            expected_high_count;
        int            expected_low_count;

        if (dev->tracks[track][side].params[4] != 2)
            return 0;

        if (!side) {
            max_high_id         = (dev->tracks[track][side].params[3] == 19) ? 0x8B : 0x88;
            expected_high_count = (dev->tracks[track][side].params[3] == 19) ? 0x0B : 0x08;
            expected_low_count  = 8;
        } else {
            max_high_id         = (dev->tracks[track][side].params[3] == 19) ? 0x93 : 0x90;
            expected_high_count = (dev->tracks[track][side].params[3] == 19) ? 0x13 : 0x10;
            expected_low_count  = 0;
        }

        for (uint8_t i = 0; i < dev->tracks[track][side].params[3]; i++) {
            if ((r_map[i] >= 0x81) && (r_map[i] <= max_high_id)) {
                high_sectors++;
                dev->xdf_ordered_pos[(int) r_map[i]][side] = i;
            }
            if ((r_map[i] >= 0x01) && (r_map[i] <= 0x08)) {
                low_sectors++;
                dev->xdf_ordered_pos[(int) r_map[i]][side] = i;
            }
            if ((high_sectors == expected_high_count) && (low_sectors == expected_low_count)) {
                dev->current_side_flags[side] = (dev->tracks[track][side].params[3] == 19) ? 0x08 : 0x28;
                return ((dev->tracks[track][side].params[3] == 19) ? 2 : 1);
            }
        }
        return 0;
    } else {
        if (dev->tracks[track][side].params[4] != 0xFF)
            return 0;

        int            effective_sectors = 0;
        int            xdf_sectors       = 0;

        const uint8_t *n_map             = (uint8_t *) (dev->buffer +
                                                        dev->tracks[track][side].n_map_offs);

        for (uint8_t i = 0; i < dev->tracks[track][side].params[3]; i++) {
            effective_sectors++;
            if (!(r_map[i]) && !(n_map[i]))
                effective_sectors--;

            if (r_map[i] == (n_map[i] | 0x80)) {
                xdf_sectors++;
                dev->xdf_ordered_pos[(int) r_map[i]][side] = i;
            }
        }

        if ((effective_sectors == 3) && (xdf_sectors == 3)) {
            dev->current_side_flags[side] = 0x28;
            return 1; /* 5.25" 2HD XDF */
        }

        if ((effective_sectors == 4) && (xdf_sectors == 4)) {
            dev->current_side_flags[side] = 0x08;
            return 2; /* 3.5" 2HD XDF */
        }

        return 0;
    }
}

static int
track_is_interleave(void *priv, int const side, const int track)
{
    fdd_drive_t *drv               = (fdd_drive_t *) priv;
    imd_t       *dev               = imd[drv->id];
    int          effective_sectors = 0;
    const char  *r_map             = dev->buffer + dev->tracks[track][side].r_map_offs;
    const int    track_spt         = dev->tracks[track][side].params[3];

    for (uint16_t i = 0; i < 256; i++)
        dev->interleave_ordered_pos[i][side] = 0;

    if (dev->tracks[track][side].params[2] & 0xC0)
        return 0;

    if (track_spt != 21)
        return 0;

    if (dev->tracks[track][side].params[4] != 2)
        return 0;

    for (int i = 0; i < track_spt; i++) {
        if ((r_map[i] >= 1) && (r_map[i] <= track_spt)) {
            effective_sectors++;
            dev->interleave_ordered_pos[(int) r_map[i]][side] = i;
        }
    }

    if (effective_sectors == track_spt)
        return 1;

    return 0;
}

static void
sector_to_buffer(void *priv, const int track, const int side, uint8_t *buffer,
                 const int sector, const int len)
{
    fdd_drive_t *drv       = (fdd_drive_t *) priv;
    const imd_t *dev       = imd[drv->id];
    const int    type      = (int) dev->buffer[dev->tracks[track][side].sector_data_offs[sector]];

    if (type == 0)
        memset(buffer, 0x00, len);
    else {
        if (type & 1)
            memcpy(buffer, &(dev->buffer[dev->tracks[track][side].sector_data_offs[sector] + 1]), len);
        else {
            const uint8_t fill_char = dev->buffer[dev->tracks[track][side].sector_data_offs[sector] + 1];
            memset(buffer, fill_char, len);
        }
    }
}

static void
imd_seek(void *priv, int track)
{
    fdd_drive_t *drv             = (fdd_drive_t *) priv;
    uint32_t    track_buf_pos[2] = { 0, 0 };
    uint8_t     id[4]            = { 0, 0, 0, 0 };
    uint8_t     type;
    imd_t      *dev              = imd[drv->id];
    int         c                = 0;
    int         track_rate       = 0;
    int         xdf_type         = 0;
    int         interleave_type  = 0;
    int         is_trackx        = 0;
    int         xdf_spt          = 0;
    int         xdf_sector       = 0;
    int         ordered_pos      = 0;
    int         real_sector      = 0;
    int         actual_sector    = 0;
    const char *c_map            = NULL;
    const char *h_map            = NULL;
    const char *n_map            = NULL;
    int         flags            = 0x00;
    uint8_t    *data;
    int         sector;
    int         ssize;

    if (dev->fp == NULL)
        return;

    if (!dev->track_width && fdd_doublestep_40(drv))
        track /= 2;

    d86f_set_cur_track(drv, track);

    is_trackx = (track == 0) ? 0 : 1;

    dev->track = track;

    dev->current_side_flags[0] = dev->tracks[track][0].side_flags;
    dev->current_side_flags[1] = dev->tracks[track][1].side_flags;

    d86f_reset_index_hole_pos(drv, 0);
    d86f_reset_index_hole_pos(drv, 1);

    d86f_destroy_linked_lists(drv, 0);
    d86f_destroy_linked_lists(drv, 1);

    d86f_zero_track(drv);

    if (track > dev->track_count)
        return;

    for (int side = 0; side < dev->sides; side++) {
        if (!dev->tracks[track][side].is_present)
            continue;

        track_rate = dev->current_side_flags[side] & 7;
        if (!track_rate && (dev->current_side_flags[side] & 0x20))
            track_rate = 4;
        if ((dev->current_side_flags[side] & 0x27) == 0x21)
            track_rate = 2;

        const char *r_map = dev->buffer + dev->tracks[track][side].r_map_offs;
        const int   h     = dev->tracks[track][side].params[2];
        if (h & 0x80)
            c_map = dev->buffer + dev->tracks[track][side].c_map_offs;
        else
            c = dev->tracks[track][side].params[1];

        if (h & 0x40)
            h_map = dev->buffer + dev->tracks[track][side].h_map_offs;

        const int n          = dev->tracks[track][side].params[4];
         int      track_gap3;
        if (n == 0xFF) {
            n_map      = dev->buffer + dev->tracks[track][side].n_map_offs;
            track_gap3 = gap3_sizes[track_rate][(int) n_map[0]][dev->tracks[track][side].params[3]];
        } else
            track_gap3 = gap3_sizes[track_rate][n][dev->tracks[track][side].params[3]];

        if (!track_gap3)
            track_gap3 = (int) dev->tracks[track][side].gap3_len;

        xdf_type = track_is_xdf(drv, side, track);

        interleave_type = track_is_interleave(drv, side, track);

        int current_pos = d86f_prepare_pretrack(drv, side, 0);

        if (!xdf_type) {
            for (sector = 0; sector < dev->tracks[track][side].params[3]; sector++) {
                if (interleave_type == 0) {
                    real_sector   = (int) r_map[sector];
                    actual_sector = sector;
                } else {
                    real_sector   = dmf_r[sector];
                    actual_sector = dev->interleave_ordered_pos[real_sector][side];
                }
                id[0] = ((c_map != NULL) && (h & 0x80)) ? c_map[actual_sector] : c;
                id[1] = ((h_map != NULL) && (h & 0x40)) ? h_map[actual_sector] : (h & 1);
                id[2] = real_sector;
                id[3] = ((n_map != NULL) && (n == 0xff)) ? n_map[actual_sector] : n;
                data  = dev->track_buffer[side] + track_buf_pos[side];
                type  = dev->buffer[dev->tracks[track][side].sector_data_offs[actual_sector]];
                type  = (type >> 1) & 7;
                flags = 0x00;
                if ((type == 2) || (type == 4))
                    flags |= SECTOR_DELETED_DATA;
                if ((type == 3) || (type == 4))
                    flags |= SECTOR_CRC_ERROR;

                if (((flags & 0x02) || (id[3] > dev->tracks[track][side].max_sector_size)) && !fdd_get_turbo(drv))
                    ssize = 3;
                else
                    ssize = 128 << ((uint32_t) id[3]);

                sector_to_buffer(drv, track, side, data, actual_sector, ssize);

                current_pos = d86f_prepare_sector(drv, side, current_pos, id, data, ssize, 22, track_gap3, flags);
                track_buf_pos[side] += ssize;

                if (sector == 0)
                    d86f_initialize_last_sector_id(drv, id[0], id[1], id[2], id[3]);
            }
        } else {
            xdf_type--;
            xdf_spt = xdf_physical_sectors[xdf_type][is_trackx];
            for (sector = 0; sector < xdf_spt; sector++) {
                xdf_sector  = (side * xdf_spt) + sector;
                id[0]       = track;
                id[1]       = side;
                id[2]       = xdf_disk_layout[xdf_type][is_trackx][xdf_sector].id.r;
                id[3]       = is_trackx ? (id[2] & 7) : 2;
                ordered_pos = dev->xdf_ordered_pos[id[2]][side];

                data  = dev->track_buffer[side] + track_buf_pos[side];
                type  = dev->buffer[dev->tracks[track][side].sector_data_offs[ordered_pos]];
                type  = ((type - 1) >> 1) & 7;
                flags = 0x00;
                if (type & 0x01)
                    flags |= SECTOR_DELETED_DATA;
                if (type & 0x02)
                    flags |= SECTOR_CRC_ERROR;

                if (((flags & 0x02) || (id[3] > dev->tracks[track][side].max_sector_size)) && !fdd_get_turbo(drv))
                    ssize = 3;
                else
                    ssize = 128 << ((uint32_t) id[3]);

                sector_to_buffer(drv, track, side, data, ordered_pos, ssize);

                const int track_gap2 = 22;

                if (is_trackx)
                    current_pos = d86f_prepare_sector(drv, side, xdf_trackx_spos[xdf_type][xdf_sector], id, data, ssize, track_gap2, xdf_gap3_sizes[xdf_type][is_trackx], flags);
                else
                    current_pos = d86f_prepare_sector(drv, side, current_pos, id, data, ssize, track_gap2, xdf_gap3_sizes[xdf_type][is_trackx], flags);

                track_buf_pos[side] += ssize;

                if (sector == 0)
                    d86f_initialize_last_sector_id(drv, id[0], id[1], id[2], id[3]);
            }
        }
    }
}

static uint16_t
disk_flags(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    const imd_t *dev = imd[drv->id];

    return (dev->disk_flags);
}

static uint16_t
side_flags(void *priv)
{
    fdd_drive_t *drv    = (fdd_drive_t *) priv;
    const imd_t *dev    = imd[drv->id];
    int          side   = 0;
    uint16_t     sflags = 0;

    side   = fdd_get_head(drv);
    sflags = dev->current_side_flags[side];

    return sflags;
}

static void
set_sector(void *priv, const int side, const uint8_t c, const uint8_t h, const uint8_t r,
           const uint8_t n)
{
    fdd_drive_t *drv   = (fdd_drive_t *) priv;
    imd_t       *dev   = imd[drv->id];
    const int    track = dev->track;
    const int    sc    = dev->tracks[track][side].params[1];
    const int    sh    = dev->tracks[track][side].params[2];
    const int    sn    = dev->tracks[track][side].params[4];
    const char  *c_map = NULL;
    const char  *h_map = NULL;
    const char * r_map = NULL;
    const char * n_map = NULL;
    uint8_t      id[4] = { 0, 0, 0, 0 };

    if (sh & 0x80)
        c_map = dev->buffer + dev->tracks[track][side].c_map_offs;
    if (sh & 0x40)
        h_map = dev->buffer + dev->tracks[track][side].h_map_offs;
    r_map = dev->buffer + dev->tracks[track][side].r_map_offs;

    if (sn == 0xFF)
        n_map = dev->buffer + dev->tracks[track][side].n_map_offs;

    if (c != dev->track)
        return;

    for (uint8_t i = 0; i < dev->tracks[track][side].params[3]; i++) {
        id[0] = ((c_map != NULL) && (sh & 0x80)) ? c_map[i] : sc;
        id[1] = ((h_map != NULL) && (sh & 0x40)) ? h_map[i] : (sh & 1);
        id[2] = r_map[i];
        id[3] = ((n_map != NULL) && (sn == 0xff)) ? n_map[i] : sn;
        if ((id[0] == c) && (id[1] == h) && (id[2] == r) && (id[3] == n))
            dev->current_data[side] = dev->buffer +
                                      dev->tracks[track][side].sector_data_offs[i];
    }
}

static void
imd_writeback(void *priv)
{
    fdd_drive_t *drv   = (fdd_drive_t *) priv;
    imd_t *      dev   = imd[drv->id];
    const int    track = dev->track;
    const char * n_map = 0;

    if (drv->writeprot)
        return;

    for (int side = 0; side < dev->sides; side++) {
        if (dev->tracks[track][side].is_present) {
            fseek(dev->fp, (off_t) dev->tracks[track][side].file_offs, SEEK_SET);
            const uint8_t h   = dev->tracks[track][side].params[2];
            const uint8_t spt = dev->tracks[track][side].params[3];
            const uint8_t n   = dev->tracks[track][side].params[4];
            fwrite(dev->tracks[track][side].params, 1, 5, dev->fp);

            if (h & 0x80)
                fwrite(dev->buffer + dev->tracks[track][side].c_map_offs, 1,
                       spt, dev->fp);

            if (h & 0x40)
                fwrite(dev->buffer + dev->tracks[track][side].h_map_offs, 1,
                       spt, dev->fp);

            if (n == 0xFF) {
                n_map = dev->buffer + dev->tracks[track][side].n_map_offs;
                fwrite(n_map, 1, spt, dev->fp);
            }
            for (uint8_t i = 0; i < spt; i++) {
                const uint32_t ssize = 128 << (((n_map != NULL) && (n == 0xff)) ? n_map[i] : n);
                fwrite(dev->buffer + dev->tracks[track][side].sector_data_offs[i], 1,
                        ssize, dev->fp);
            }
        }
    }

    fflush(dev->fp);
}

static uint8_t
poll_read_data(void *priv, const int side, const uint16_t pos)
{
    fdd_drive_t *drv  = (fdd_drive_t *) priv;
    const imd_t *dev  = imd[drv->id];
    const int    type = (int) dev->current_data[side][0];

    if ((type == 0) || (type > 8))
        return 0xf6; /* Should never happen. */

    if (type & 1)
        return (dev->current_data[side][pos + 1]);
    else
        return (dev->current_data[side][1]);
}

static void
poll_write_data(void *priv, const int side, const uint16_t pos, const uint8_t data)
{
    fdd_drive_t *drv  = (fdd_drive_t *) priv;
    const imd_t *dev  = imd[drv->id];
    const int    type = (int) dev->current_data[side][0];

    if (drv->writeprot)
        return;

    if ((type & 1) || (type == 0) || (type > 8))
        return; /* Should never happen. */

    dev->current_data[side][pos + 1] = (char) data;
}

static int
format_conditions(void *priv)
{
    fdd_drive_t *drv   = (fdd_drive_t *) priv;
    const imd_t *dev   = imd[drv->id];
    const int    track = dev->track;
    const int    side  = fdd_get_head(drv);
    const int    temp  = (fdc_get_format_sectors(drv->fdc) == dev->tracks[track][side].params[3]) &&
                         (fdc_get_format_n(drv->fdc) == dev->tracks[track][side].params[4]);

    return temp;
}

void
imd_init(void)
{
    memset(imd, 0x00, sizeof(imd));
}

void
imd_load(void *priv, char *fn)
{
    fdd_drive_t *drv            = (fdd_drive_t *) priv;
    /* Allocate a drive block. */
    imd_t *      dev            = (imd_t *) calloc(1, sizeof(imd_t));
    uint32_t     magic          = 0;
    uint32_t     fsize          = 0;
    int          track_spt      = 0;
    int          sector_size    = 0;
    int          track          = 0;
    int          side           = 0;
    int          extra          = 0;
    uint32_t     last_offset    = 0;
    uint32_t     mfm            = 0;
    uint32_t     pre_sector     = 0;
    uint32_t     track_total    = 0;
    uint32_t     raw_tsize      = 0;
    uint32_t     minimum_gap3   = 0;
    uint32_t     data_size;
    uint8_t      converted_rate;
    uint8_t      type;

    d86f_unregister(drv);

    drv->writeprot = 0;

    dev->fp = plat_fopen(fn, "rb+");
    if (dev->fp == NULL) {
        dev->fp = plat_fopen(fn, "rb");
        if (dev->fp == NULL) {
            memset(drv->image_path, 0, sizeof(drv->image_path));
            free(dev);
            return;
        }
        drv->writeprot = 1;
    }

    if (drv->read_only)
        drv->writeprot = 1;
    drv->fwriteprot = drv->writeprot;

    if (fseek(dev->fp, 0, SEEK_SET) == -1)
        fatal("imd_load(): Error seeking to the beginning of the file\n");
    if (fread(&magic, 1, 4, dev->fp) != 4)
        fatal("imd_load(): Error reading the magic number\n");
    if (magic != 0x20444D49) {
        imd_log("IMD: Not a valid ImageDisk image\n");
        fclose(dev->fp);
        free(dev);
        memset(drv->image_path, 0, sizeof(drv->image_path));
        return;
    } else
        imd_log("IMD: Valid ImageDisk image\n");

    if (fseek(dev->fp, 0, SEEK_END) == -1)
        fatal("imd_load(): Error seeking to the end of the file\n");
    fsize = ftell(dev->fp);
    if (fsize <= 0) {
        imd_log("IMD: Too small ImageDisk image\n");
        fclose(dev->fp);
        free(dev);
        memset(drv->image_path, 0, sizeof(drv->image_path));
        return;
    }
    if (fseek(dev->fp, 0, SEEK_SET) == -1)
        fatal("imd_load(): Error seeking to the beginning of the file again\n");
    dev->buffer = calloc(1, fsize);
    if (fread(dev->buffer, 1, fsize, dev->fp) != fsize)
        fatal("imd_load(): Error reading data\n");

    const char *buffer  = dev->buffer;
    const char *buffer2 = memchr(buffer, 0x1a, fsize);

    if (buffer2 == NULL) {
        imd_log("IMD: No ASCII EOF character\n");
        fclose(dev->fp);
        free(dev);
        memset(drv->image_path, 0, sizeof(drv->image_path));
        return;
    } else {
        imd_log("IMD: ASCII EOF character found at offset %08X\n", buffer2 - buffer);
    }

    buffer2++;
    if ((buffer2 - buffer) == fsize) {
        imd_log("IMD: File ends after ASCII EOF character\n");
        fclose(dev->fp);
        free(dev);
        memset(drv->image_path, 0, sizeof(drv->image_path));
        return;
    } else {
        imd_log("IMD: File continues after ASCII EOF character\n");
    }

    dev->start_offs  = (buffer2 - buffer);
    dev->disk_flags  = 0x00;
    dev->track_count = 0;
    dev->sides       = 1;

    /* Set up the drive unit. */
    imd[drv->id] = dev;

    while (1) {
        imd_log("In : %02X %02X %02X %02X %02X\n",
                buffer2[0], buffer2[1], buffer2[2], buffer2[3], buffer2[4]);

        track = (int) (uint8_t) buffer2[1];
        side  = (int) (uint8_t) buffer2[2];
        if (side & 1)
            dev->sides = 2;
        extra = side & 0xC0;
        side &= 0x3F;

        track_spt   = (int) (uint8_t) buffer2[3];
        dev->tracks[track][side].spt = track_spt;
        sector_size = (int) (uint8_t) buffer2[4];

        dev->tracks[track][side].side_flags = (buffer2[0] % 3);
        if ((track_spt != 0x00) && (!dev->tracks[track][side].side_flags))
            dev->disk_flags |= 0x02;
        dev->tracks[track][side].side_flags |=
            (!(buffer2[0] - dev->tracks[track][side].side_flags) ? 0 : 8);
        mfm         = dev->tracks[track][side].side_flags & 8;
        track_total = mfm ? 146 : 73;
        pre_sector  = mfm ? 60 : 42;

        imd_log("Out : %02X %02X %02X %02X %02X\n", buffer2[0], track, side, track_spt,
                                                    sector_size);
        if ((track_spt == 15) && (sector_size == 2))
            dev->tracks[track][side].side_flags |= 0x20;
        if ((track_spt == 16) && (sector_size == 2))
            dev->tracks[track][side].side_flags |= 0x20;
        if ((track_spt == 17) && (sector_size == 2))
            dev->tracks[track][side].side_flags |= 0x20;
        if ((track_spt == 8) && (sector_size == 3))
            dev->tracks[track][side].side_flags |= 0x20;
        if ((dev->tracks[track][side].side_flags & 7) == 1)
            dev->tracks[track][side].side_flags |= 0x20;
        if ((dev->tracks[track][side].side_flags & 0x07) == 0x00)
            dev->tracks[track][side].max_sector_size = 6;
        else
            dev->tracks[track][side].max_sector_size = 5;
        if (!mfm)
            dev->tracks[track][side].max_sector_size--;
        imd_log("Side flags for (%02i)(%01i): %02X\n", track, side,
                dev->tracks[track] [side].side_flags);
        dev->tracks[track][side].is_present = 1;
        dev->tracks[track][side].file_offs  = (buffer2 - buffer);
        memcpy(dev->tracks[track][side].params, buffer2, 5);
        dev->tracks[track][side].r_map_offs = dev->tracks[track][side].file_offs + 5;
        last_offset                         = dev->tracks[track][side].r_map_offs + track_spt;

        if (extra & 0x80) {
            dev->tracks[track][side].c_map_offs = last_offset;
            last_offset += track_spt;
        }

        if (extra & 0x40) {
            dev->tracks[track][side].h_map_offs = last_offset;
            last_offset += track_spt;
        }

        if (track_spt == 0x00) {
            last_offset += track_spt;
            dev->tracks[track][side].is_present = 0;
        } else if (sector_size == 0xFF) {
            dev->tracks[track][side].n_map_offs = last_offset;
            buffer2                             = buffer + last_offset;
            last_offset += track_spt;

            dev->tracks[track][side].data_offs = last_offset;

            for (int i = 0; i < track_spt; i++) {
                data_size                                    = (uint32_t) (uint8_t) buffer2[i];
                data_size                                    = 128 << data_size;
                dev->tracks[track][side].sector_data_offs[i] = last_offset;
                dev->tracks[track][side].sector_data_size[i] = 1;
                if (dev->buffer[dev->tracks[track][side].sector_data_offs[i]] > 0x08) {
                    /*
                       Invalid sector data type, possibly a malformed HxC IMG image
                       (it outputs data errored sectors with a variable amount of bytes,
                       against the specification).
                     */
                    imd_log("IMD: Invalid sector data type %02X\n", dev->buffer[dev->tracks[track][side].sector_data_offs[i]]);
                    fclose(dev->fp);
                    free(dev);
                    imd[drv->id] = NULL;
                    memset(drv->image_path, 0, sizeof(drv->image_path));
                    return;
                }
                if (buffer[dev->tracks[track][side].sector_data_offs[i]] != 0)
                    dev->tracks[track][side].sector_data_size[i] += (buffer[dev->tracks[track][side].sector_data_offs[i]] & 1) ? data_size : 1;
                last_offset += dev->tracks[track][side].sector_data_size[i];
                if (!(buffer[dev->tracks[track][side].sector_data_offs[i]] & 1))
                    drv->fwriteprot = drv->writeprot = 1;
                type = dev->buffer[dev->tracks[track][side].sector_data_offs[i]];
                if (type != 0x00) {
                    if (data_size > (128 << dev->tracks[track][side].max_sector_size))
                        track_total += (pre_sector + 3);
                    else
                        track_total += (pre_sector + data_size + 2);
                }
            }
        } else {
            dev->tracks[track][side].data_offs = last_offset;

            for (int i = 0; i < track_spt; i++) {
                data_size                                    = sector_size;
                data_size                                    = 128 << data_size;
                dev->tracks[track][side].sector_data_offs[i] = last_offset;
                dev->tracks[track][side].sector_data_size[i] = 1;
                if (dev->buffer[dev->tracks[track][side].sector_data_offs[i]] > 0x08) {
                    /*
                       Invalid sector data type, possibly a malformed HxC IMG image
                       (it outputs data errored sectors with a variable amount of bytes,
                       against the specification).
                     */
                    imd_log("IMD: Invalid sector data type %02X\n", dev->buffer[dev->tracks[track][side].sector_data_offs[i]]);
                    fclose(dev->fp);
                    free(dev);
                    imd[drv->id] = NULL;
                    memset(drv->image_path, 0, sizeof(drv->image_path));
                    return;
                }
                if (buffer[dev->tracks[track][side].sector_data_offs[i]] != 0)
                    dev->tracks[track][side].sector_data_size[i] +=
                        (buffer[dev->tracks[track][side].sector_data_offs[i]] & 1) ?
                            data_size : 1;
                last_offset += dev->tracks[track][side].sector_data_size[i];
                if (!(buffer[dev->tracks[track][side].sector_data_offs[i]] & 1))
                    drv->fwriteprot = drv->writeprot = 1;
                type = dev->buffer[dev->tracks[track][side].sector_data_offs[i]];
                if (type != 0x00) {
                    if (data_size > (128 << dev->tracks[track][side].max_sector_size))
                        track_total += (pre_sector + 3);
                    else
                        track_total += (pre_sector + data_size + 2);
                }
            }
        }

        buffer2 = buffer + last_offset;

        /*
           - Leaving even GAP4: 80 : 40;
           - Leaving only GAP1: 96 : 47;
           - Not leaving even GAP1: 146 : 73.
         */
        raw_tsize    = get_raw_tsize(dev->tracks[track][side].side_flags, 0);
        minimum_gap3 = 12 * track_spt;

        if ((dev->tracks[track][side].side_flags == 0x0A) || (dev->tracks[track][side].side_flags == 0x29))
            converted_rate = 2;
        else if (dev->tracks[track][side].side_flags == 0x28)
            converted_rate = 4;
        else
            converted_rate = dev->tracks[track][side].side_flags & 0x03;

        if ((track_spt != 0x00) && (gap3_sizes[converted_rate][sector_size][track_spt] == 0x00)) {
            int       size_diff = (int) (raw_tsize - track_total);
            const int gap_sum   = (int) minimum_gap3;

            if (size_diff < gap_sum) {
                /*
                   If we can't fit the sectors with a reasonable minimum gap at perfect RPM,
                   let's try 2% slower.
                 */
                raw_tsize = get_raw_tsize(dev->tracks[track][side].side_flags, 1);
                /* Set disk flags so that rotation speed is 2% slower. */
                dev->disk_flags |= (3 << 5);
                size_diff = (int) (raw_tsize - track_total);
                if (size_diff < gap_sum) {
                    /*
                       If we can't fit the sectors with a reasonable minimum gap even at
                       2% slower RPM, abort.
                     */
                    imd_log("IMD: Unable to fit the %i sectors in a track\n", track_spt);
                    fclose(dev->fp);
                    free(dev);
                    imd[drv->id] = NULL;
                    memset(drv->image_path, 0, sizeof(drv->image_path));
                    return;
                }
            }

            dev->tracks[track][side].gap3_len = (size_diff) / track_spt;
        } else
            dev->tracks[track][side].gap3_len =
                gap3_sizes[converted_rate][sector_size][track_spt];

        if (track > dev->track_count)
            dev->track_count = track;

        if (last_offset >= fsize)
            break;
    }

    /* If more than 43 tracks, then the tracks are thin (96 tpi). */
    dev->track_count++;
    imd_log("In : dev->track_count = %i\n", dev->track_count);

    int empty_tracks = 0;
    for (int i = 0; i < dev->track_count; i++) {
        if (((dev->sides == 2) && (dev->tracks[i][0].spt == 0x00) &&
             (dev->tracks[i][1].spt == 0x00)) ||
            ((dev->sides == 1) && (dev->tracks[i][0].spt == 0x00)))
            empty_tracks++;
    }
    imd_log("empty_tracks = %i\n", empty_tracks);

    if (empty_tracks >= (dev->track_count >> 1)) {
        for (int i = 0; i < dev->track_count; i += 2) {
            imd_log("Thick %02X = Thin %02X\n", i >> 1, i);
            dev->tracks[i >> 1][0] = dev->tracks[i][0];
            dev->tracks[i >> 1][1] = dev->tracks[i][1];
        }
        for (int i = (dev->track_count >> 1); i < dev->track_count; i++) {
            imd_log("Emptying %02X....\n", i);
            memset(&(dev->tracks[i][0]), 1, sizeof(imd_track_t));
            memset(&(dev->tracks[i][1]), 1, sizeof(imd_track_t));
        }
        dev->track_count >>= 1;
    }
    imd_log("Out: dev->track_count = %i\n", dev->track_count);

    dev->track_width = 0;
    if (dev->track_count > 43)
        dev->track_width = 1;

    /* If 2 sides, mark it as such. */
    if (dev->sides == 2)
        dev->disk_flags |= 8;

#if 0
    imd_log("%i tracks, %i sides\n", dev->track_count, dev->sides);
#endif

    /* Attach this format to the D86F engine. */
    drv->d86f_handler.disk_flags        = disk_flags;
    drv->d86f_handler.side_flags        = side_flags;
    drv->d86f_handler.writeback         = imd_writeback;
    drv->d86f_handler.set_sector        = set_sector;
    drv->d86f_handler.read_data         = poll_read_data;
    drv->d86f_handler.write_data        = poll_write_data;
    drv->d86f_handler.format_conditions = format_conditions;
    drv->d86f_handler.extra_bit_cells   = null_extra_bit_cells;
    drv->d86f_handler.encoded_data      = common_encoded_data;
    drv->d86f_handler.read_revolution   = common_read_revolution;
    drv->d86f_handler.index_hole_pos    = null_index_hole_pos;
    drv->d86f_handler.get_raw_size      = common_get_raw_size;
    drv->d86f_handler.check_crc         = 1;
    d86f_set_version(drv, 0x0063);

    drv->seek = imd_seek;

    d86f_common_handlers(drv);
}

void
imd_close(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    imd_t *      dev = imd[drv->id];

    if (dev == NULL)
        return;

    d86f_unregister(drv);

    if (dev->fp != NULL) {
        free(dev->buffer);

        fclose(dev->fp);
    }

    /* Release the memory. */
    free(dev);
    imd[drv->id] = NULL;
}
