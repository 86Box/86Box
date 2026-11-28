/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles.
 *
 * Native D88/D77 floppy image loader.
 *
 * The D88 container stores a 0x2b0-byte disk header followed by variable
 * length tracks.  Every sector has a 16-byte CHRN/status header and an
 * explicit payload length.  Tracks are synthesized into the D86F core so
 * all normal 86Box FDC behaviour (READ DATA, READ ID, CRC, deleted DAM,
 * rotational timing, density checks, etc.) remains in the normal path.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/fdd.h>
#include <86box/fdd_86f.h>
#include <86box/fdd_d88.h>

#define D88_TRACKS      164
#define D88_HEADER_SIZE 0x2b0u
#define D88_SECTOR_HDR  16u
#define D88_MAX_SECTORS 256u
#define D88_MAX_SECSIZE 32768u

/* Header media byte.  The low nibble is used by some NP2-derived images to
 * distinguish 1.44 MB 2HD media, so classify by the high nibble. */
#define D88_MEDIA_2D  0x00u
#define D88_MEDIA_2DD 0x10u
#define D88_MEDIA_2HD 0x20u
#define D88_MEDIA_1D  0x30u
#define D88_MEDIA_1DD 0x40u

typedef struct d88_sector_t {
    uint8_t  c, h, r, n;
    uint8_t  density;
    uint8_t  deleted;
    uint8_t  status;
    uint8_t  rpm;
    uint16_t track_sectors;
    uint16_t data_size;
    uint32_t header_offset;
    uint32_t data_offset;
    uint8_t *data;
} d88_sector_t;

typedef struct d88_track_t {
    uint32_t      offset;
    uint32_t      end;
    uint16_t      count;
    uint8_t       present;
    d88_sector_t *sectors;
} d88_track_t;

typedef struct d88_t {
    FILE         *fp;
    uint8_t      *image;
    size_t        file_size;
    uint32_t      disk_size;
    uint32_t      track_offset[D88_TRACKS];
    uint8_t       protect;
    uint8_t       media;
    uint8_t       sides;
    uint8_t       writable;
    uint8_t       dirty;
    uint8_t       building;
    uint8_t       build_side;
    int           track;
    int           max_cylinder;
    int           track_width;
    uint16_t      disk_flags;
    uint16_t      current_side_flags[2];
    d88_sector_t *current_sector[2];
    d88_track_t   tracks[D88_TRACKS];
} d88_t;

static uint16_t
rd16(const uint8_t *p)
{
    return (uint16_t) p[0] | ((uint16_t) p[1] << 8);
}

static uint32_t
rd32(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

static uint32_t
d88_next_track_offset(const d88_t *dev, uint32_t cur)
{
    uint32_t next = dev->disk_size;

    for (int i = 0; i < D88_TRACKS; i++) {
        const uint32_t p = dev->track_offset[i];
        if ((p > cur) && (p < next))
            next = p;
    }
    return next;
}

static void
d88_free_tracks(d88_t *dev)
{
    if (dev == NULL)
        return;
    for (int i = 0; i < D88_TRACKS; i++) {
        free(dev->tracks[i].sectors);
        dev->tracks[i].sectors = NULL;
        dev->tracks[i].count   = 0;
    }
}

/* Return 1 for a valid first D88 disk, 0 for a malformed image.  A D88 file
 * may contain several concatenated disks; one 86Box floppy drv mounts the
 * first disk while preserving the trailing bytes verbatim on writeback. */
static int
d88_parse(d88_t *dev)
{
    const uint8_t *h             = dev->image;
    uint32_t       highest_track = 0;
    int            any_track     = 0;
    int            saw_side1     = 0;

    if (dev->file_size < D88_HEADER_SIZE)
        return 0;

    dev->protect   = h[0x1a];
    dev->media     = h[0x1b];
    dev->disk_size = rd32(h + 0x1c);

    if ((dev->disk_size < D88_HEADER_SIZE) || (dev->disk_size > dev->file_size))
        return 0;

    switch (dev->media & 0xf0u) {
        case D88_MEDIA_2D:
        case D88_MEDIA_2DD:
        case D88_MEDIA_2HD:
        case D88_MEDIA_1D:
        case D88_MEDIA_1DD:
            break;
        default:
            return 0;
    }

    for (int i = 0; i < D88_TRACKS; i++)
        dev->track_offset[i] = rd32(h + 0x20 + (i * 4));

    for (int ti = 0; ti < D88_TRACKS; ti++) {
        d88_track_t *trk = &dev->tracks[ti];
        uint32_t     off = dev->track_offset[ti];
        uint32_t     end;
        uint32_t     pos;
        uint16_t     count;

        if (off == 0)
            continue;
        if ((off < D88_HEADER_SIZE) || (off >= dev->disk_size))
            return 0;

        end = d88_next_track_offset(dev, off);
        if ((end <= off) || (end > dev->disk_size) || ((end - off) < D88_SECTOR_HDR))
            return 0;

        count = rd16(dev->image + off + 4);
        if ((count == 0) || (count > D88_MAX_SECTORS))
            return 0;

        trk->sectors = (d88_sector_t *) calloc(count, sizeof(d88_sector_t));
        if (trk->sectors == NULL)
            return 0;

        trk->offset  = off;
        trk->end     = end;
        trk->count   = count;
        trk->present = 1;
        pos          = off;

        for (uint16_t si = 0; si < count; si++) {
            d88_sector_t  *sec = &trk->sectors[si];
            const uint8_t *sh;
            uint16_t       ssize;

            if ((pos > end) || ((end - pos) < D88_SECTOR_HDR))
                return 0;
            sh    = dev->image + pos;
            ssize = rd16(sh + 14);
            if (ssize > D88_MAX_SECSIZE)
                return 0;
            if ((uint32_t) ssize > (end - pos - D88_SECTOR_HDR))
                return 0;

            sec->c             = sh[0];
            sec->h             = sh[1];
            sec->r             = sh[2];
            sec->n             = sh[3];
            sec->track_sectors = rd16(sh + 4);
            sec->density       = sh[6];
            sec->deleted       = sh[7];
            sec->status        = sh[8];
            sec->rpm           = sh[13];
            sec->data_size     = ssize;
            sec->header_offset = pos;
            sec->data_offset   = pos + D88_SECTOR_HDR;
            sec->data          = dev->image + sec->data_offset;

            /* N is an ID field.  Values beyond the NEC/uPD765 range are not
             * useful to D86F and can otherwise trigger oversized shifts. */
            if (sec->n > 7)
                return 0;

            if (sec->h & 1)
                saw_side1 = 1;

            pos += D88_SECTOR_HDR + ssize;
        }

        any_track     = 1;
        highest_track = (uint32_t) ti;
    }

    if (!any_track)
        return 0;

    dev->sides = saw_side1 ? 2 : 1;
    /* Track table entries are C*2+H even for the ordinary double-sided D88
     * layout.  This also gives a useful upper bound for 40/80-track media. */
    dev->max_cylinder = (int) (highest_track >> 1);
    dev->track_width  = (dev->max_cylinder > 43) ? 1 : 0;

    return 1;
}

static uint16_t
d88_default_side_flags(const d88_t *dev)
{
    uint16_t flags;

    switch (dev->media & 0xf0u) {
        case D88_MEDIA_2HD:
            /* PC-98 1.2/1.23 MB: 500 kbps MFM at 360 rpm.  NP2's 2HD+1
             * extension denotes 1.44 MB and therefore 300 rpm. */
            flags = 0x08;
            if ((dev->media & 0x0fu) == 0)
                flags |= 0x20;
            break;
        case D88_MEDIA_2D:
        case D88_MEDIA_1D:
            flags = 0x02; /* 250 kbps class, encoding corrected per sector. */
            break;
        case D88_MEDIA_2DD:
        case D88_MEDIA_1DD:
        default:
            flags = 0x0a; /* 250 kbps MFM, 300 rpm. */
            break;
    }
    return flags;
}

static uint16_t
d88_flags_for_track(const d88_t *dev, int logical_track)
{
    const d88_track_t *trk;
    uint16_t           flags = d88_default_side_flags(dev);

    if ((logical_track < 0) || (logical_track >= D88_TRACKS))
        return flags;
    trk = &dev->tracks[logical_track];
    if (!trk->present || !trk->count)
        return flags;

    /* D88 density byte: 00h = MFM/double density, 40h = FM/single density.
     * The media byte still determines the physical hole/data-rate class. */
    if (trk->sectors[0].density & 0x40)
        flags &= ~0x08u;
    else
        flags |= 0x08u;

    if ((dev->media & 0xf0u) == D88_MEDIA_2HD) {
        if (trk->sectors[0].rpm)
            flags &= ~0x20u; /* 1.44 MB / 300 rpm */
        else
            flags |= 0x20u; /* 1.2/1.23 MB / 360 rpm */
    }

    return flags;
}

static uint16_t
disk_flags(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    const d88_t *dev = drv->local;
    return dev ? dev->disk_flags : 0;
}

static uint16_t
side_flags(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    const d88_t *dev = drv->local;
    int          side;

    if (dev == NULL)
        return 0x0a;
    side = dev->building ? dev->build_side : fdd_get_head(drv);
    side &= 1;
    return dev->current_side_flags[side];
}

static int
sector_flags(const d88_sector_t *sec)
{
    int flags = 0;

    if ((sec->deleted & 0x10) || (sec->status == 0x10))
        flags |= SECTOR_DELETED_DATA;

    switch (sec->status) {
        case 0xa0: /* ID CRC. D86F's sector helper has one generic CRC-error
                      flag, so preserve an observable CRC fault rather than
                      silently treating the damaged sector as clean. */
        case 0xb0: /* Data CRC */
            flags |= SECTOR_CRC_ERROR;
            break;
        case 0xe0: /* No address mark / IDAM */
            flags |= SECTOR_NO_ID | SECTOR_NO_DATA;
            break;
        case 0xf0: /* No data address mark */
            flags |= SECTOR_NO_DATA;
            break;
        default:
            break;
    }

    if (sec->data_size == 0)
        flags |= SECTOR_NO_DATA;
    return flags;
}

static int
d88_gap3_for_track(void *priv, int side, const d88_track_t *trk, uint16_t sflags)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    uint32_t     raw_bytes;
    uint32_t     used;
    uint32_t     remaining;
    int          mfm;
    int          gap;

    if (!trk->count)
        return 24;

    raw_bytes = common_get_raw_size(drv, side) >> 4;
    mfm       = (sflags & 0x08) != 0;
    used      = mfm ? 146u : 73u; /* GAP0 + sync/IAM + GAP1 */

    for (uint16_t i = 0; i < trk->count; i++) {
        /* Matches the byte count emitted by d86f_prepare_sector(), excluding
         * GAP3 itself.  Use actual D88 payload size because it is explicitly
         * allowed to differ from 128<<N. */
        used += (mfm ? 62u : 44u) + trk->sectors[i].data_size;
    }

    if (raw_bytes <= used + (uint32_t) trk->count * 8u)
        return 8;

    remaining = raw_bytes - used;
    gap       = (int) (remaining / trk->count);
    /* Leave a modest GAP4 before index and avoid absurd gaps in sparse or
     * partially imaged tracks. */
    if (gap > 16)
        gap -= 8;
    if (gap < 8)
        gap = 8;
    if (gap > 255)
        gap = 255;
    return gap;
}

static void
d88_build_side(void *priv, int side)
{
    fdd_drive_t *drv     = (fdd_drive_t *) priv;
    d88_t       *dev     = drv->local;
    int          logical = (dev->track << 1) | side;
    d88_track_t *trk;
    uint16_t     pos;
    int          gap2;
    int          gap3;

    if ((logical < 0) || (logical >= D88_TRACKS))
        return;
    trk = &dev->tracks[logical];
    if (!trk->present)
        return;

    dev->building   = 1;
    dev->build_side = (uint8_t) side;

    pos  = d86f_prepare_pretrack(drv, side, 0);
    gap2 = (dev->current_side_flags[side] & 0x08) ? 22 : 11;
    gap3 = d88_gap3_for_track(drv, side, trk, dev->current_side_flags[side]);

    for (uint16_t i = 0; i < trk->count; i++) {
        d88_sector_t *sec   = &trk->sectors[i];
        uint8_t       id[4] = { sec->c, sec->h, sec->r, sec->n };
        pos                 = d86f_prepare_sector(drv, side, pos, id, sec->data,
                                                  sec->data_size, gap2, gap3,
                                                  sector_flags(sec));
    }

    dev->building = 0;
}

static void
d88_seek(void *priv, int track)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d88_t       *dev = drv->local;

    if (dev == NULL)
        return;

    /* 40-track media in an 80-track double-step drv uses every other
     * physical cylinder, like the IMG/IMD loaders. */
    if (!dev->track_width && fdd_doublestep_40(drv))
        track /= 2;
    if (track < 0)
        track = 0;

    dev->track = track;
    d86f_set_cur_track(drv, track);

    for (int side = 0; side < 2; side++)
        dev->current_side_flags[side] = d88_flags_for_track(dev, (track << 1) | side);

    d86f_zero_track(drv);
    for (int side = 0; side < dev->sides; side++)
        d88_build_side(drv, side);

    dev->building          = 0;
    dev->current_sector[0] = dev->current_sector[1] = NULL;
}

static void
d88_set_sector(void *priv, int side, uint8_t c, uint8_t h, uint8_t r, uint8_t n)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d88_t       *dev = drv->local;
    int          logical;
    d88_track_t *trk;

    if ((dev == NULL) || (side < 0) || (side > 1))
        return;
    dev->current_sector[side] = NULL;
    logical                   = (dev->track << 1) | side;
    if ((logical < 0) || (logical >= D88_TRACKS))
        return;
    trk = &dev->tracks[logical];
    if (!trk->present)
        return;

    for (uint16_t i = 0; i < trk->count; i++) {
        d88_sector_t *sec = &trk->sectors[i];
        if ((sec->c == c) && (sec->h == h) && (sec->r == r) && (sec->n == n)) {
            dev->current_sector[side] = sec;
            return;
        }
    }
}

static uint8_t
d88_read_data(void *priv, int side, uint16_t pos)
{
    fdd_drive_t        *drv = (fdd_drive_t *) priv;
    const d88_t        *dev = drv->local;
    const d88_sector_t *sec;

    if ((dev == NULL) || (side < 0) || (side > 1))
        return 0xf6;
    sec = dev->current_sector[side];
    if ((sec == NULL) || (pos >= sec->data_size))
        return 0xf6;
    return sec->data[pos];
}

static void
d88_write_data(void *priv, int side, uint16_t pos, uint8_t data)
{
    fdd_drive_t  *drv = (fdd_drive_t *) priv;
    d88_t        *dev = drv->local;
    d88_sector_t *sec;

    if ((dev == NULL) || !dev->writable || drv->writeprot || (side < 0) || (side > 1))
        return;
    sec = dev->current_sector[side];
    if ((sec == NULL) || (pos >= sec->data_size))
        return;
    sec->data[pos] = data;
    dev->dirty     = 1;
}

static void
d88_writeback(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d88_t       *dev = drv->local;

    if ((dev == NULL) || !dev->dirty || !dev->writable || (dev->fp == NULL) || drv->writeprot)
        return;

    if ((fseek(dev->fp, 0, SEEK_SET) == 0) && (fwrite(dev->image, 1, dev->file_size, dev->fp) == dev->file_size) && (fflush(dev->fp) == 0)) {
        dev->dirty = 0;
    } else {
        pclog("[PC98-D88] Failed to save floppy image; retaining dirty state\n");
    }
}

static int
d88_format_track(UNUSED(void *priv), UNUSED(int side),
                 UNUSED(const d86f_format_id_t *ids), UNUSED(uint16_t count),
                 UNUSED(uint8_t fill))
{
    /* D88 formatting can resize a track and all following offsets.  Refuse
     * FORMAT TRACK until that structural rewrite is implemented; normal
     * reads and in-place sector writes remain fully supported. */
    return 0;
}

static int
d88_format_conditions(UNUSED(void *priv))
{
    return 0;
}

static uint32_t
d88_index_hole_pos(UNUSED(void *priv), UNUSED(int side))
{
    return 0;
}

static int
d88_open_image(d88_t *dev, const char *fn)
{
    long size;

    dev->fp = plat_fopen(fn, "rb+");
    if (dev->fp != NULL)
        dev->writable = 1;
    else {
        dev->fp       = plat_fopen(fn, "rb");
        dev->writable = 0;
    }
    if (dev->fp == NULL)
        return 0;

    if (fseek(dev->fp, 0, SEEK_END) != 0)
        return 0;
    size = ftell(dev->fp);
    if (size < 0 || (uint64_t) size > SIZE_MAX)
        return 0;
    if (fseek(dev->fp, 0, SEEK_SET) != 0)
        return 0;

    dev->file_size = (size_t) size;
    dev->image     = (uint8_t *) malloc(dev->file_size ? dev->file_size : 1);
    if (dev->image == NULL)
        return 0;
    if (fread(dev->image, 1, dev->file_size, dev->fp) != dev->file_size)
        return 0;
    return 1;
}

void
d88_load(void *priv, char *fn)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d88_t       *dev;
    uint8_t      media_class;

    d86f_unregister(drv);

    dev = (d88_t *) calloc(1, sizeof(d88_t));
    if (dev == NULL)
        goto fail;

    if (!d88_open_image(dev, fn) || !d88_parse(dev))
        goto fail_dev;

    /* Header protection and the UI write-protect toggle both win. */
    if ((dev->protect & 0x10) || drv->read_only)
        dev->writable = 0;
    drv->writeprot = dev->writable ? 0 : 1;

    media_class     = dev->media & 0xf0u;
    dev->disk_flags = (dev->sides == 2) ? 0x08 : 0x00;
    if (media_class == D88_MEDIA_2HD)
        dev->disk_flags |= 0x02; /* HD density hole. */
    if (drv->writeprot)
        dev->disk_flags |= 0x10;

    drv->local = dev;

    dev->current_side_flags[0] = d88_default_side_flags(dev);
    dev->current_side_flags[1] = dev->current_side_flags[0];

    drv->d86f_handler.disk_flags        = disk_flags;
    drv->d86f_handler.side_flags        = side_flags;
    drv->d86f_handler.writeback         = d88_writeback;
    drv->d86f_handler.set_sector        = d88_set_sector;
    drv->d86f_handler.format_track      = d88_format_track;
    drv->d86f_handler.read_data         = d88_read_data;
    drv->d86f_handler.write_data        = d88_write_data;
    drv->d86f_handler.format_conditions = d88_format_conditions;
    drv->d86f_handler.extra_bit_cells   = null_extra_bit_cells;
    drv->d86f_handler.encoded_data      = common_encoded_data;
    drv->d86f_handler.read_revolution   = common_read_revolution;
    drv->d86f_handler.index_hole_pos    = d88_index_hole_pos;
    drv->d86f_handler.get_raw_size      = common_get_raw_size;
    drv->d86f_handler.check_crc         = 1;
    d86f_set_version(drv, D86FVER);
    d86f_common_handlers(drv);

    const fdd_format_t ops = {
        .seek        = d88_seek,
        .readaddress = NULL
    };

    fdd_format_set(drv, &ops);

    /* Populate the first track immediately, matching the other sector-image
     * loaders and avoiding a blank revolution before the first seek. */
    d88_seek(drv, 0);
    return;

fail_dev:
    if (dev != NULL) {
        d88_free_tracks(dev);
        free(dev->image);
        if (dev->fp != NULL)
            fclose(dev->fp);
        free(dev);
    }
fail:
    memset(drv->image_path, 0, sizeof(drv->image_path));
    drv->empty = 1;
}

void
d88_close(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d88_t       *dev = drv->local;

    if (dev == NULL)
        return;

    d88_writeback(drv);
    d86f_unregister(drv);
    drv->fmt.seek = NULL;

    d88_free_tracks(dev);
    free(dev->image);
    if (dev->fp != NULL)
        fclose(dev->fp);
    free(dev);
    drv->local = NULL;
}
