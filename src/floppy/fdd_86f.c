/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the 86F floppy image format (stores the
 *          data in the form of FM/MFM-encoded transitions) which also
 *          forms the core of the emulator's floppy disk emulation.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2018-2019 Fred N. van Kempen.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/crc.h>
#include <86box/dma.h>
#include <86box/nvr.h>
#include <86box/random.h>
#include <86box/plat.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdd_86f.h>

/*
 * Let's give this some more logic:
 *
 * Bits 4,3 = Read/write (0 = read, 1 = write, 2 = scan, 3 = verify)
 * Bits 6,5 = Sector/track (0 = ID, 1 = sector, 2 = deleted sector, 3 = track)
 * Bit  7   = State type (0 = idle states, 1 = active states)
 */
enum {
    /* 0 ?? ?? ??? */
    STATE_IDLE = 0x00,
    STATE_SECTOR_NOT_FOUND,

    /* 1 00 00 ??? */
    STATE_0A_FIND_ID = 0x80, /* READ SECTOR ID */
    STATE_0A_READ_ID,

    /* 1 01 00 ??? */
    STATE_06_FIND_ID = 0xA0, /* READ DATA */
    STATE_06_READ_ID,
    STATE_06_FIND_DATA,
    STATE_06_READ_DATA,

    /* 1 01 01 ??? */
    STATE_05_FIND_ID = 0xA8, /* WRITE DATA */
    STATE_05_READ_ID,
    STATE_05_FIND_DATA,
    STATE_05_WRITE_DATA,

    /* 1 01 10 ??? */
    STATE_11_FIND_ID = 0xB0, /* SCAN EQUAL,SCAN LOW/EQUAL,SCAN HIGH/EQUAL */
    STATE_11_READ_ID,
    STATE_11_FIND_DATA,
    STATE_11_SCAN_DATA,

    /* 1 01 11 ??? */
    STATE_16_FIND_ID = 0xB8, /* VERIFY */
    STATE_16_READ_ID,
    STATE_16_FIND_DATA,
    STATE_16_VERIFY_DATA,

    /* 1 10 00 ??? */
    STATE_0C_FIND_ID = 0xC0, /* READ DELETED DATA */
    STATE_0C_READ_ID,
    STATE_0C_FIND_DATA,
    STATE_0C_READ_DATA,

    /* 1 10 01 ??? */
    STATE_09_FIND_ID = 0xC8, /* WRITE DELETED DATA */
    STATE_09_READ_ID,
    STATE_09_FIND_DATA,
    STATE_09_WRITE_DATA,

    /* 1 11 00 ??? */
    STATE_02_SPIN_TO_INDEX = 0xE0, /* READ TRACK */
    STATE_02_FIND_ID,
    STATE_02_READ_ID,
    STATE_02_FIND_DATA,
    STATE_02_READ_DATA,

    /* 1 11 01 ??? */
    STATE_0D_SPIN_TO_INDEX = 0xE8, /* FORMAT TRACK */
    STATE_0D_FORMAT_TRACK,
};

enum {
    FMT_PRETRK_GAP0,
    FMT_PRETRK_SYNC,
    FMT_PRETRK_IAM,
    FMT_PRETRK_GAP1,

    FMT_SECTOR_ID_SYNC,
    FMT_SECTOR_IDAM,
    FMT_SECTOR_ID,
    FMT_SECTOR_ID_CRC,
    FMT_SECTOR_GAP2,
    FMT_SECTOR_DATA_SYNC,
    FMT_SECTOR_DATAAM,
    FMT_SECTOR_DATA,
    FMT_SECTOR_DATA_CRC,
    FMT_SECTOR_GAP3,

    FMT_POSTTRK_CHECK,
    FMT_POSTTRK_GAP4
};

typedef struct find_t {
    uint32_t bits_obtained;
    uint16_t bytes_obtained;
    uint16_t sync_marks;
    uint32_t sync_pos;
} find_t;

typedef struct split_byte_t {
    unsigned nibble0 : 4;
    unsigned nibble1 : 4;
} split_byte_t;

typedef union decoded_t {
    uint8_t      byte;
    split_byte_t nibbles;
} decoded_t;

typedef struct sector_t {
    uint8_t c;
    uint8_t h;
    uint8_t r;
    uint8_t n;
    uint8_t flags;
    uint8_t pad;
    uint8_t pad0;
    uint8_t pad1;
    void   *prev;
} sector_t;

/* Disk flags:
    Bit 0       Has surface data (1 = yes, 0 = no)
    Bits 2, 1   Hole (3 = ED + 2000 kbps, 2 = ED, 1 = HD, 0 = DD)
    Bit 3       Sides (1 = 2 sides, 0 = 1 side)
    Bit 4       Write protect (1 = yes, 0 = no)
    Bits 6, 5   RPM slowdown (3 = 2%, 2 = 1.5%, 1 = 1%, 0 = 0%)
    Bit 7       Bitcell mode (1 = Extra bitcells count specified after
                              disk flags, 0 = No extra bitcells)
                The maximum number of extra bitcells is 1024 (which
                after decoding translates to 64 bytes)
    Bit 8       Disk type (1 = Zoned, 0 = Fixed RPM)
    Bits 10, 9  Zone type (3 = Commodore 64 zoned, 2 = Apple zoned,
                           1 = Pre-Apple zoned #2, 0 = Pre-Apple zoned #1)
    Bit 11      Data and surface bits are stored in reverse byte endianness
    Bit 12      If bits 6, 5 are not 0, they specify % of speedup instead
                of slowdown;
                If bits 6, 5 are 0, and bit 7 is 1, the extra bitcell count
                specifies the entire bitcell count.
 */
typedef struct d86f_t {
    FILE *           fp;
    uint8_t          state;
    uint8_t          fill;
    uint8_t          sector_count;
    uint8_t          format_state;
    uint8_t          error_condition;
    uint8_t          id_found;
    uint16_t         version;
    uint16_t         disk_flags;
    uint16_t         satisfying_bytes;
    uint16_t         turbo_pos;
    uint16_t         cur_track;
    uint16_t         format_id_count;
    d86f_format_id_t format_ids[256];
    uint16_t         track_encoded_data[2][53048];
    uint16_t *       track_surface_data[2];
    uint16_t         thin_track_encoded_data[2][2][53048];
    uint16_t *       thin_track_surface_data[2][2];
    uint16_t         side_flags[2];
    uint16_t         preceding_bit[2];
    uint16_t         current_byte[2];
    uint16_t         current_bit[2];
    uint16_t         last_word[2];
    int32_t          extra_bit_cells[2];
    uint32_t         file_size;
    uint32_t         index_count;
    uint32_t         track_pos;
    uint32_t         datac;
    uint32_t         dma_over;
    uint32_t         index_hole_pos[2];
    uint32_t         track_offset[512];
    sector_id_t      last_sector;
    sector_id_t      req_sector;
    find_t           id_find;
    find_t           data_find;
    crc_t            calc_crc;
    crc_t            track_crc;
    sector_t *       last_side_sector[2];
    uint16_t         crc_table[256];
} d86f_t;

static const uint8_t encoded_fm[64] = {
    0xaa, 0xab, 0xae, 0xaf, 0xba, 0xbb, 0xbe, 0xbf,
    0xea, 0xeb, 0xee, 0xef, 0xfa, 0xfb, 0xfe, 0xff,
    0xaa, 0xab, 0xae, 0xaf, 0xba, 0xbb, 0xbe, 0xbf,
    0xea, 0xeb, 0xee, 0xef, 0xfa, 0xfb, 0xfe, 0xff,
    0xaa, 0xab, 0xae, 0xaf, 0xba, 0xbb, 0xbe, 0xbf,
    0xea, 0xeb, 0xee, 0xef, 0xfa, 0xfb, 0xfe, 0xff,
    0xaa, 0xab, 0xae, 0xaf, 0xba, 0xbb, 0xbe, 0xbf,
    0xea, 0xeb, 0xee, 0xef, 0xfa, 0xfb, 0xfe, 0xff
};
static const uint8_t encoded_mfm[64] = {
    0xaa, 0xa9, 0xa4, 0xa5, 0x92, 0x91, 0x94, 0x95,
    0x4a, 0x49, 0x44, 0x45, 0x52, 0x51, 0x54, 0x55,
    0x2a, 0x29, 0x24, 0x25, 0x12, 0x11, 0x14, 0x15,
    0x4a, 0x49, 0x44, 0x45, 0x52, 0x51, 0x54, 0x55,
    0xaa, 0xa9, 0xa4, 0xa5, 0x92, 0x91, 0x94, 0x95,
    0x4a, 0x49, 0x44, 0x45, 0x52, 0x51, 0x54, 0x55,
    0x2a, 0x29, 0x24, 0x25, 0x12, 0x11, 0x14, 0x15,
    0x4a, 0x49, 0x44, 0x45, 0x52, 0x51, 0x54, 0x55
};

uint64_t        poly = 0x42F0E1EBA9EA3693LL; /* ECMA normal */

int      d86f_is_mfm(void *priv);
void     d86f_writeback(void *priv);
uint8_t  d86f_poll_read_data(void *priv, int side, uint16_t pos);
void     d86f_poll_write_data(void *priv, int side, uint16_t pos, uint8_t data);
int      d86f_format_conditions(void *priv);

#ifdef ENABLE_D86F_LOG
int d86f_do_log = ENABLE_D86F_LOG;

static void
d86f_log(const char *fmt, ...)
{
    va_list ap;

    if (d86f_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define d86f_log(fmt, ...)
#endif

void
d86f_destroy_linked_lists(void *priv, int side)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    if (drv == NULL)
        return;

    d86f_t *     dev = (d86f_t *) drv->d86f_priv;

    if (dev == NULL)
        return;

    if (dev->last_side_sector[side]) {
        sector_t *s = dev->last_side_sector[side];
        while (s) {
            sector_t *t = s->prev;
            free(s);
            s = NULL;
            if (!t)
                break;
            s = t;
        }
        dev->last_side_sector[side] = NULL;
    }
}

static int
d86f_has_surface_desc(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    return (drv->d86f_handler.disk_flags(drv) & 1);
}

int
d86f_get_sides(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    return ((drv->d86f_handler.disk_flags(drv) >> 3) & 1) + 1;
}

int
d86f_get_rpm_mode(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    return (drv->d86f_handler.disk_flags(drv) & 0x60) >> 5;
}

int
d86f_get_speed_shift_dir(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    return (drv->d86f_handler.disk_flags(drv) & 0x1000) >> 12;
}

int
d86f_reverse_bytes(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    return (drv->d86f_handler.disk_flags(drv) & 0x800) >> 11;
}

uint16_t
d86f_disk_flags(void *priv)
{
    fdd_drive_t * drv = (fdd_drive_t *) priv;
    const d86f_t *dev = (d86f_t *) drv->d86f_priv;

    return dev->disk_flags;
}

uint32_t
d86f_index_hole_pos(void *priv, int side)
{
    fdd_drive_t * drv = (fdd_drive_t *) priv;
    const d86f_t *dev = (d86f_t *) drv->d86f_priv;

    return dev->index_hole_pos[side];
}

uint32_t
null_index_hole_pos(UNUSED(void *priv), UNUSED(int side))
{
    return 0;
}

uint16_t
null_disk_flags(UNUSED(void *priv))
{
    return 0x09;
}

uint16_t
null_side_flags(UNUSED(void *priv))
{
    return 0x0A;
}

void
null_writeback(UNUSED(void *priv))
{
}

void
null_set_sector(UNUSED(void *priv), UNUSED(int side), UNUSED(uint8_t c), UNUSED(uint8_t h), UNUSED(uint8_t r), UNUSED(uint8_t n))
{
}

int
null_format_track(UNUSED(void *priv), UNUSED(int side),
                  UNUSED(const d86f_format_id_t *ids), UNUSED(uint16_t count),
                  UNUSED(uint8_t fill))
{
    return 1;
}

void
null_write_data(UNUSED(void *priv), UNUSED(int side), UNUSED(uint16_t pos), UNUSED(uint8_t data))
{
}

int
null_format_conditions(UNUSED(void *priv))
{
    return 0;
}

int32_t
d86f_extra_bit_cells(void *priv, const int side)
{
    fdd_drive_t *drv  = (fdd_drive_t *) priv;
    const d86f_t *dev = (d86f_t *) drv->d86f_priv;

    return dev->extra_bit_cells[side];
}

int32_t
null_extra_bit_cells(UNUSED(void *priv), UNUSED(int side))
{
    return 0;
}

uint16_t *
common_encoded_data(void *priv, int side)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *     dev = (d86f_t *) drv->d86f_priv;

    return dev->track_encoded_data[side];
}

void
common_read_revolution(UNUSED(void *priv))
{
}

uint16_t
d86f_side_flags(void *priv)
{
    fdd_drive_t * drv  = (fdd_drive_t *) priv;
    const d86f_t *dev  = (d86f_t *) drv->d86f_priv;

    int           side = fdd_get_head(drv);

    return dev->side_flags[side];
}

uint16_t
d86f_track_flags(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    uint16_t       tf = drv->d86f_handler.side_flags(drv);
    const uint16_t rr = tf & 0x67;
    const uint16_t dr = fdd_get_flags(drv) & 7;
    tf &= ~0x67;

    switch (rr) {
        default:
            tf |= rr;
            break;
        case 0x02:
        case 0x21:
            /* 1 MB unformatted medium, treat these two as equivalent. */
            if (dr == 0x06)
                /* 5.25" Single-RPM HD drive, treat as 300 kbps, 360 rpm. */
                tf |= 0x21;
            else
                /* Any other drive, treat as 250 kbps, 300 rpm. */
                tf |= 0x02;
            break;
    }

    return tf;
}

uint32_t
common_get_raw_size(void *priv, int const side)
{
    fdd_drive_t *  drv      = (fdd_drive_t *) priv;
    double         rate     = 0.0;
    double         size     = 100000.0;
    uint32_t       extra_bc = 0;

    const int      mfm      = d86f_is_mfm(drv);
    const double   rpm      = ((d86f_track_flags(drv) & 0xE0) == 0x20) ? 360.0 : 300.0;
    double         rpm_diff = 1.0;
    const int      rm       = d86f_get_rpm_mode(drv);
    const int      ssd      = d86f_get_speed_shift_dir(drv);

    /* 0% speed shift and shift direction 1: special case where extra bit cells are the entire track size. */
    if (!rm && ssd)
        extra_bc = drv->d86f_handler.extra_bit_cells(drv, side);

    if (extra_bc)
        return extra_bc;

    switch (rm) {
        case 1:
            rpm_diff = 1.01;
            break;

        case 2:
            rpm_diff = 1.015;
            break;

        case 3:
            rpm_diff = 1.02;
            break;

        default:
            rpm_diff = 1.0;
            break;
    }

    if (ssd)
        rpm_diff = 1.0 / rpm_diff;

    switch (d86f_track_flags(drv) & 7) {
        case 0:
            rate = 500.0;
            break;

        case 1:
            rate = 300.0;
            break;

        case 2:
            rate = 250.0;
            break;

        case 3:
            rate = 1000.0;
            break;

        case 5:
            rate = 2000.0;
            break;

        default:
            rate = 250.0;
            break;
    }

    if (!mfm)
        rate /= 2.0;

    size = (size / 250.0) * rate;
    size = (size * 300.0) / rpm;
    size *= rpm_diff;

    /*
     * Round down to a multiple of 16 and add the extra bit cells,
     * then return.
     */
    return ((((uint32_t) size) >> 4) << 4) +
              drv->d86f_handler.extra_bit_cells(drv, side);
}

void
d86f_set_version(void *priv, uint16_t version)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *     dev = (d86f_t *) drv->d86f_priv;

    dev->version = version;
}

void
d86f_unregister(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *dev      = (d86f_t *) drv->d86f_priv;

    if (dev == NULL)
        return;

    drv->d86f_handler.disk_flags        = null_disk_flags;
    drv->d86f_handler.side_flags        = null_side_flags;
    drv->d86f_handler.writeback         = null_writeback;
    drv->d86f_handler.set_sector        = null_set_sector;
    drv->d86f_handler.format_track      = null_format_track;
    drv->d86f_handler.write_data        = null_write_data;
    drv->d86f_handler.format_conditions = null_format_conditions;
    drv->d86f_handler.extra_bit_cells   = null_extra_bit_cells;
    drv->d86f_handler.encoded_data      = common_encoded_data;
    drv->d86f_handler.read_revolution   = common_read_revolution;
    drv->d86f_handler.index_hole_pos    = null_index_hole_pos;
    drv->d86f_handler.get_raw_size      = common_get_raw_size;
    drv->d86f_handler.check_crc         = 0;

    dev->version = 0x0063; /* Proxied formats report as version 0.99. */
}

void
d86f_register_86f(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    drv->d86f_handler.disk_flags        = d86f_disk_flags;
    drv->d86f_handler.side_flags        = d86f_side_flags;
    drv->d86f_handler.writeback         = d86f_writeback;
    drv->d86f_handler.set_sector        = null_set_sector;
    drv->d86f_handler.format_track      = null_format_track;
    drv->d86f_handler.write_data        = null_write_data;
    drv->d86f_handler.format_conditions = d86f_format_conditions;
    drv->d86f_handler.extra_bit_cells   = d86f_extra_bit_cells;
    drv->d86f_handler.encoded_data      = common_encoded_data;
    drv->d86f_handler.read_revolution   = common_read_revolution;
    drv->d86f_handler.index_hole_pos    = d86f_index_hole_pos;
    drv->d86f_handler.get_raw_size      = common_get_raw_size;
    drv->d86f_handler.check_crc         = 1;
}

int
d86f_get_array_size(void *priv, const int side, const int words)
{
    fdd_drive_t *drv  = (fdd_drive_t *) priv;
    int          array_size;

    const int    rm   = d86f_get_rpm_mode(drv);
    const int    ssd  = d86f_get_speed_shift_dir(drv);
    const int    hole = (drv->d86f_handler.disk_flags(drv) >> 1) & 3;
    const int    mpc  = (drv->d86f_handler.disk_flags(drv) >> 13) & 1;

    if (!rm && ssd)
        /* Special case - extra bit cells size specifies entire array size. */
        array_size = 0;
    else
        switch (hole) {
            default:
            case 0:
            case 1:
                array_size = 12500;
                switch (rm) {
                    case 1:
                        array_size = ssd ? 12376 : 12625;
                        break;

                    case 2:
                        array_size = ssd ? 12315 : 12687;
                        break;

                    case 3:
                        array_size = ssd ? 12254 : 12750;
                        break;

                    default:
                        break;
                }
                break;

            case 2:
                array_size = 25000;
                switch (rm) {
                    case 1:
                        array_size = ssd ? 24752 : 25250;
                        break;

                    case 2:
                        array_size = ssd ? 24630 : 25375;
                        break;

                    case 3:
                        array_size = ssd ? 24509 : 25500;
                        break;

                    default:
                        break;
                }
                break;

            case 3:
                array_size = 50000;
                switch (rm) {
                    case 1:
                        array_size = ssd ? 49504 : 50500;
                        break;

                    case 2:
                        array_size = ssd ? 49261 : 50750;
                        break;

                    case 3:
                        array_size = ssd ? 49019 : 51000;
                        break;

                    default:
                        break;
                }
                break;
        }

    array_size <<= 4;
    array_size += drv->d86f_handler.extra_bit_cells(drv, side);

    if (mpc && !words) {
        if (array_size & 7)
            array_size = (array_size >> 3) + 1;
        else
            array_size = (array_size >> 3);
    } else {
        if (array_size & 15)
            array_size = (array_size >> 4) + 1;
        else
            array_size = (array_size >> 4);

        if (!words)
            array_size <<= 1;
    }

    return array_size;
}

int
d86f_valid_bit_rate(void *priv)
{
    fdd_drive_t *drv  = (fdd_drive_t *) priv;

    const int    rate = fdc_get_bit_rate(drv->fdc);
    const int    hole = (drv->d86f_handler.disk_flags(drv) & 6) >> 1;

    switch (hole) {
        case 0: /* DD */
            if (!rate && (fdd_get_flags(drv) & 0x10))
                return 1;
            if ((rate < 1) || (rate > 2))
                return 0;
            return 1;

        case 1: /* HD */
            if (rate != 0)
                return 0;
            return 1;

        case 2: /* ED */
            if (rate != 3)
                return 0;
            return 1;

        case 3: /* ED with 2000 kbps support */
            if (rate < 3)
                return 0;
            return 1;

        default:
            break;
    }

    return 0;
}

int
d86f_hole(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    if (((drv->d86f_handler.disk_flags(drv) >> 1) & 3) == 3)
        return 2;

    return (drv->d86f_handler.disk_flags(drv) >> 1) & 3;
}

void
d86f_set_track_pos(void *priv, const uint32_t track_pos)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *     dev = (d86f_t *) drv->d86f_priv;

    if (dev != NULL)
        dev->track_pos = track_pos;
}

uint32_t
d86f_get_track_pos(void *priv)
{
    fdd_drive_t * drv = (fdd_drive_t *) priv;
    const d86f_t *dev = (d86f_t *) drv->d86f_priv;

    uint32_t      ret = 0;

    if (dev != NULL)
        ret = dev->track_pos;

    return ret;
}

uint32_t
d86f_get_raw_size(void *priv, const int side)
{
    fdd_drive_t * drv = (fdd_drive_t *) priv;
    const d86f_t *dev = (d86f_t *) drv->d86f_priv;

    uint32_t      ret = 12500;

    if (dev != NULL)
        ret = drv->d86f_handler.get_raw_size(drv, side);

    return ret;
}

uint8_t
d86f_get_encoding(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    return (d86f_track_flags(drv) & 0x18) >> 3;
}

uint64_t
d86f_byteperiod(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *     dev = (d86f_t *) drv->d86f_priv;

    uint64_t     ret = 32ULL * TIMER_USEC;

    if (!fdd_get_turbo(drv) || (dev->version != 0x0063) ||
        (dev->state == STATE_SECTOR_NOT_FOUND)) {
        const double dusec = (double) TIMER_USEC;
        double       p     = 2.0;

        switch (d86f_track_flags(drv) & 0x0f) {
            case 0x02: /* 125 kbps, FM */
                p = 4.0;
                break;
            case 0x01: /* 150 kbps, FM */
                p = 20.0 / 6.0;
                break;
            case 0x0a: /* 250 kbps, MFM */
            case 0x00: /* 250 kbps, FM */
                default:
                p = 2.0;
                break;
            case 0x09: /* 300 kbps, MFM */
                p = 10.0 / 6.0;
                break;
            case 0x08: /* 500 kbps, MFM */
                p = 1.0;
                break;
            case 0x0b: /* 1000 kbps, MFM */
                p = 0.5;
                break;
            case 0x0d: /* 2000 kbps, MFM */
                p = 0.25;
                break;
        }

        ret = (uint64_t) (p * dusec);
    }

    return ret;
}

int
d86f_is_mfm(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    return ((d86f_track_flags(drv) & 0x18) == 0x08) ? 1 : 0;
}

uint32_t
d86f_get_data_len(void *priv)
{
    fdd_drive_t * drv = (fdd_drive_t *) priv;
    const d86f_t *dev = (d86f_t *) drv->d86f_priv;
    uint32_t      i;
    uint32_t      ret = 128;

    if (dev->req_sector.id.n)
        ret = (uint32_t) 128 << dev->req_sector.id.n;
    else if ((i = fdc_get_dtl(drv->fdc)) < 128)
        ret = i;

    return ret;
}

uint32_t
d86f_has_extra_bit_cells(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    return (drv->d86f_handler.disk_flags(drv) >> 7) & 1;
}

uint32_t
d86f_header_size(UNUSED(void *priv))
{
    return 8;
}

static uint16_t
d86f_encode_get_data(const uint8_t dat)
{
    uint16_t temp = 0;

    if (dat & 0x01)
        temp |= 1;
    if (dat & 0x02)
        temp |= 4;
    if (dat & 0x04)
        temp |= 16;
    if (dat & 0x08)
        temp |= 64;
    if (dat & 0x10)
        temp |= 256;
    if (dat & 0x20)
        temp |= 1024;
    if (dat & 0x40)
        temp |= 4096;
    if (dat & 0x80)
        temp |= 16384;

    return temp;
}

static uint16_t
d86f_encode_get_clock(const uint8_t dat)
{
    uint16_t temp = 0;

    if (dat & 0x01)
        temp |= 2;
    if (dat & 0x02)
        temp |= 8;
    if (dat & 0x40)
        temp |= 32;
    if (dat & 0x08)
        temp |= 128;
    if (dat & 0x10)
        temp |= 512;
    if (dat & 0x20)
        temp |= 2048;
    if (dat & 0x40)
        temp |= 8192;
    if (dat & 0x80)
        temp |= 32768;

    return temp;
}

int
d86f_format_conditions(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    return d86f_valid_bit_rate(drv);
}

int
d86f_wrong_densel(void *priv)
{
    fdd_drive_t *drv      = (fdd_drive_t *) priv;
    int          is_3mode = 0;

    int          ret      = 0;

    if ((fdd_get_flags(drv) & 7) == 3)
        is_3mode = 1;

    switch (d86f_hole(drv)) {
        default:
        case 0:
            if (!fdd_is_dd(drv) && fdd_get_densel(drv))
                ret = 1;
            break;

        case 1:
            if (fdd_is_dd(drv) || (!fdd_get_densel(drv) && !is_3mode))
                ret = 1;
            break;

        case 2:
            if (fdd_is_dd(drv) || !fdd_is_ed(drv) || !fdd_get_densel(drv))
                ret = 1;
            break;
    }

    return ret;
}

int
d86f_can_format(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    int          ret = !drv->writeprot;

    ret = ret && !fdc_get_swwp(drv->fdc);
    ret = ret && fdd_can_read_medium(&drives[real_drive(drv->fdc, drv->id)]);
    /* Allows proxied formats to add their own extra conditions to formatting. */
    ret = ret && drv->d86f_handler.format_conditions(drv);
    ret = ret && !d86f_wrong_densel(drv);

    return ret;
}

uint16_t
d86f_encode_byte(void *priv, const int sync, const decoded_t b, const decoded_t prev_b)
{
    fdd_drive_t *  drv      = (fdd_drive_t *) priv;

    const uint8_t  encoding = d86f_get_encoding(drv);
    const uint8_t  bits89AB = prev_b.nibbles.nibble0;
    uint8_t        bits7654 = b.nibbles.nibble1;
    uint8_t        bits3210 = b.nibbles.nibble0;
    uint16_t       result;

    if (encoding > 1)
        return 0xffff;

    if (sync) {
        result = d86f_encode_get_data(b.byte);
        if (encoding) {
            switch (b.byte) {
                case 0xa1:
                    return result | d86f_encode_get_clock(0x0a);

                case 0xc2:
                    return result | d86f_encode_get_clock(0x14);

                case 0xf8:
                    return result | d86f_encode_get_clock(0x03);

                case 0xfb:
                case 0xfe:
                    return result | d86f_encode_get_clock(0x00);

                case 0xfc:
                    return result | d86f_encode_get_clock(0x01);

                default:
                    break;
            }
        } else {
            switch (b.byte) {
                case 0xf8:
                case 0xfb:
                case 0xfe:
                    return result | d86f_encode_get_clock(0xc7);

                case 0xfc:
                    return result | d86f_encode_get_clock(0xd7);

                default:
                    break;
            }
        }
    }

    bits3210                    += ((bits7654 & 3) << 4);
    bits7654                    += ((bits89AB & 3) << 4);
    const uint16_t encoded_3210  = (encoding == 1) ? encoded_mfm[bits3210] :
                                                     encoded_fm[bits3210];
    const uint16_t encoded_7654  = (encoding == 1) ? encoded_mfm[bits7654] :
                                                     encoded_fm[bits7654];
    result                       = (encoded_7654 << 8) | encoded_3210;

    return result;
}

static int
d86f_get_bitcell_period(void *priv)
{
    fdd_drive_t *drv      = (fdd_drive_t *) priv;
    double       rate     = 0.0;
    int          mfm      = 0;
    int          tflags   = 0;
    double       rpm      = 0;
    double       size     = 8000.0;

    tflags = d86f_track_flags(drv);

    mfm = (tflags & 8) ? 1 : 0;
    rpm = ((tflags & 0xE0) == 0x20) ? 360.0 : 300.0;

    switch (tflags & 7) {
        case 0:
            rate = 500.0;
            break;

        case 1:
            rate = 300.0;
            break;

        case 2:
            rate = 250.0;
            break;

        case 3:
            rate = 1000.0;
            break;

        case 5:
            rate = 2000.0;
            break;

        default:
            break;
    }

    if (!mfm)
        rate /= 2.0;
    size = (size * 250.0) / rate;
    size = (size * 300.0) / rpm;
    size = (size * fdd_getrpm(&drives[real_drive(drv->fdc, drv->id)])) / 300.0;

    return (int) size;
}

int
d86f_can_read_address(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    int          ret = (fdc_get_bitcell_period(drv->fdc) == d86f_get_bitcell_period(drv));

    ret = ret && fdd_can_read_medium(&drives[real_drive(drv->fdc, drv->id)]);
    ret = ret && (fdc_is_mfm(drv->fdc) == d86f_is_mfm(drv));
    ret = ret && (d86f_get_encoding(drv) <= 1);

    return ret;
}

void
d86f_get_bit(void *priv, const int side)
{
    fdd_drive_t *  drv          = (fdd_drive_t *) priv;
    d86f_t *       dev          = (d86f_t *) drv->d86f_priv;
    uint32_t       track_word   = dev->track_pos >> 4;
    /* We need to make sure we read the bits from MSB to LSB. */
    const uint32_t track_bit    = 15 - (dev->track_pos & 15);
    uint16_t       encoded_data;
    uint16_t       surface_data = 0;

    if (d86f_reverse_bytes(drv)) {
        /* Image is in reverse endianness, read the data as is. */
        encoded_data = drv->d86f_handler.encoded_data(drv, side)[track_word];
    } else {
        /*
           We store the words as big endian, so we need to convert them to little endian
           when reading.
         */
        encoded_data = (drv->d86f_handler.encoded_data(drv, side)[track_word] & 0xff) << 8;
        encoded_data |= (drv->d86f_handler.encoded_data(drv, side)[track_word] >> 8);
    }

    /*
       In some cases, misindentification occurs so we need to make sure the surface data
       array is not NULL.
     */
    if (d86f_has_surface_desc(drv) && dev->track_surface_data[side]) {
        if (d86f_reverse_bytes(drv))
            surface_data = dev->track_surface_data[side][track_word] & 0xff;
        else {
            surface_data = (dev->track_surface_data[side][track_word] & 0xff) << 8;
            surface_data |= (dev->track_surface_data[side][track_word] >> 8);
        }
    }

    const uint16_t current_bit = (encoded_data >> track_bit) & 1;
    dev->last_word[side] <<= 1;

    if (d86f_has_surface_desc(drv) && dev->track_surface_data[side]) {
        const uint16_t surface_bit = (surface_data >> track_bit) & 1;
        if (!surface_bit)
            dev->last_word[side] |= current_bit;
        else {
            /* Bit is either 0 or 1 and is set to fuzzy, we randomly generate it. */
            dev->last_word[side] |= (random_generate() & 1);
        }
    } else
        dev->last_word[side] |= current_bit;
}

void
d86f_put_bit(void *priv, const int side, const int bit)
{
    fdd_drive_t *  drv          = (fdd_drive_t *) priv;
    d86f_t *       dev          = (d86f_t *) drv->d86f_priv;
    uint16_t       surface_data = 0;
    uint16_t       encoded_data;

    if (fdc_get_diswr(drv->fdc))
        return;

    uint32_t       track_word   = dev->track_pos >> 4;

    /* We need to make sure we read the bits from MSB to LSB. */
    const uint32_t track_bit = 15 - (dev->track_pos & 15);

    if (d86f_reverse_bytes(drv)) {
        /* Image is in reverse endianness, read the data as is. */
        encoded_data = drv->d86f_handler.encoded_data(drv, side)[track_word];
    } else {
        /* We store the words as big endian, so we need to convert them to little endian when reading. */
        encoded_data = (drv->d86f_handler.encoded_data(drv, side)[track_word] & 0xff) << 8;
        encoded_data |= (drv->d86f_handler.encoded_data(drv, side)[track_word] >> 8);
    }

    if (d86f_has_surface_desc(drv)) {
        if (d86f_reverse_bytes(drv))
            surface_data = dev->track_surface_data[side][track_word] & 0xff;
        else {
            surface_data = (dev->track_surface_data[side][track_word] & 0xff) << 8;
            surface_data |= (dev->track_surface_data[side][track_word] >> 8);
        }
    }

    uint16_t current_bit = (encoded_data >> track_bit) & 1;
    dev->last_word[side] <<= 1;

    if (d86f_has_surface_desc(drv)) {
        uint16_t surface_bit = (surface_data >> track_bit) & 1;
        if (!surface_bit) {
            dev->last_word[side] |= bit;
            current_bit = bit;
        } else {
            if (current_bit) {
                /* Bit is 1 and is set to fuzzy, we overwrite it with a non-fuzzy bit. */
                dev->last_word[side] |= bit;
                current_bit = bit;
                surface_bit = 0;
            }
        }

        surface_data &= ~(1 << track_bit);
        surface_data |= (surface_bit << track_bit);
        if (d86f_reverse_bytes(drv))
            dev->track_surface_data[side][track_word] = surface_data;
        else {
            dev->track_surface_data[side][track_word] = (surface_data & 0xFF) << 8;
            dev->track_surface_data[side][track_word] |= (surface_data >> 8);
        }
    } else {
        dev->last_word[side] |= bit;
        current_bit = bit;
    }

    encoded_data &= ~(1 << track_bit);
    encoded_data |= (current_bit << track_bit);

    if (d86f_reverse_bytes(drv))
        drv->d86f_handler.encoded_data(drv, side)[track_word] = encoded_data;
    else {
        drv->d86f_handler.encoded_data(drv, side)[track_word] = (encoded_data & 0xFF) << 8;
        drv->d86f_handler.encoded_data(drv, side)[track_word] |= (encoded_data >> 8);
    }
}

static uint8_t
decodefm(UNUSED(void *priv), const uint16_t dat)
{
    uint8_t temp = 0;

    /*
     * We write the encoded bytes in big endian, so we
     * process the two 8-bit halves swapped here.
     */
    if (dat & 0x0001)
        temp |= 1;
    if (dat & 0x0004)
        temp |= 2;
    if (dat & 0x0010)
        temp |= 4;
    if (dat & 0x0040)
        temp |= 8;
    if (dat & 0x0100)
        temp |= 16;
    if (dat & 0x0400)
        temp |= 32;
    if (dat & 0x1000)
        temp |= 64;
    if (dat & 0x4000)
        temp |= 128;

    return temp;
}

static void
d86f_calccrc(d86f_t *dev, const uint8_t byte)
{
    crc16_calc(dev->crc_table, byte, &(dev->calc_crc));
}

int
d86f_word_is_aligned(void *priv, const int side, uint32_t base_pos)
{
    fdd_drive_t * drv                = (fdd_drive_t *) priv;
    const d86f_t *dev                = (d86f_t *) drv->d86f_priv;
    uint32_t      adjusted_track_pos = dev->track_pos;

    if (base_pos == 0xffffffff)
        return 0;

    /*
       This is very important, it makes sure alignment is detected correctly even across
       the index hole of a track whose length is not divisible by 16.
     */
    if (adjusted_track_pos < base_pos)
        adjusted_track_pos += drv->d86f_handler.get_raw_size(drv, side);

    if ((adjusted_track_pos & 15) == (base_pos & 15))
        return 1;

    return 0;
}

/* State 1: Find sector ID */
void
d86f_find_address_mark_fm(void *priv, const int side, find_t *find,
                          const uint16_t req_am, const uint16_t other_am,
                          const uint16_t wrong_am, const uint16_t ignore_other_am)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *     dev = (d86f_t *) drv->d86f_priv;

    d86f_get_bit(drv, side);

    if (dev->last_word[side] == req_am) {
        dev->calc_crc.word = 0xffff;
        crc16_calc(dev->crc_table, decodefm(drv, dev->last_word[side]),
                   &(dev->calc_crc));
        find->sync_marks = find->bits_obtained =
        find->bytes_obtained                   = 0;
        find->sync_pos                         = 0xFFFFFFFF;
        dev->preceding_bit[side]               = dev->last_word[side] & 1;
        dev->state++;
        return;
    }

    if (wrong_am && (dev->last_word[side] == wrong_am)) {
        dev->data_find.sync_marks = dev->data_find.bits_obtained =
        dev->data_find.bytes_obtained                            = 0;
        dev->error_condition                                     = 0;
        dev->state                                               = STATE_IDLE;
        fdc_nodataam(drv->fdc);
        return;
    }

    if ((ignore_other_am & 2) && (dev->last_word[side] == other_am)) {
        dev->calc_crc.word = 0xffff;
        crc16_calc(dev->crc_table, decodefm(drv, dev->last_word[side]),
                   &(dev->calc_crc));
        find->sync_marks = find->bits_obtained = find->bytes_obtained = 0;
        find->sync_pos                                                = 0xffffffff;
        if (ignore_other_am & 1) {
            /* Skip mode, let's go back to finding ID. */
            fdc_set_wrong_am(drv->fdc);
            dev->data_find.sync_marks = dev->data_find.bits_obtained  =
                                        dev->data_find.bytes_obtained = 0;
            dev->error_condition                                      = 0;
            dev->state                                                = STATE_IDLE;
            if (dev->state == STATE_02_READ_DATA)
                fdc_track_finishread(drv->fdc, dev->error_condition);
            else if (dev->state == STATE_11_SCAN_DATA)
                fdc_sector_finishcompare(drv->fdc, (dev->satisfying_bytes ==
                                         ((128 << ((uint32_t) dev->last_sector.id.n)) - 1)) ? 1 : 0);
            else
                fdc_sector_finishread(drv->fdc);
        } else {
            /* Not skip mode, process the sector anyway. */
            fdc_set_wrong_am(drv->fdc);
            dev->preceding_bit[side] = dev->last_word[side] & 1;
            dev->state++;
        }
    }
}

/*
   When writing in FM mode, we find the beginning of the address mark by looking for
   352 (22 * 16) set bits (gap fill = 0xFF, 0xFFFF FM-encoded).
 */
void
d86f_write_find_address_mark_fm(void *priv, const int side, find_t *find)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *     dev = (d86f_t *) drv->d86f_priv;

    d86f_get_bit(drv, side);

    if (dev->last_word[side] & 1) {
        find->sync_marks++;
        if (find->sync_marks == 352) {
            dev->calc_crc.word       = 0xffff;
            dev->preceding_bit[side] = 1;
            find->sync_marks         = 0;
            dev->state++;
            return;
        }
    }

    /* If we hadn't found enough set bits but have found a clear bit, null the counter of set bits. */
    if (!(dev->last_word[side] & 1)) {
        find->sync_marks = find->bits_obtained =
        find->bytes_obtained                   = 0;
        find->sync_pos                         = 0xffffffff;
    }
}

void
d86f_find_address_mark_mfm(void *priv, const int side, find_t *find,
                           const uint16_t req_am, const uint16_t other_am,
                           const uint16_t wrong_am, const uint16_t ignore_other_am)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *     dev = (d86f_t *) drv->d86f_priv;

    d86f_get_bit(drv, side);

    if (dev->last_word[side] == 0x4489) {
        find->sync_marks++;
        find->sync_pos = dev->track_pos;
        return;
    }

    if (wrong_am && (dev->last_word[side] == wrong_am) && (find->sync_marks >= 3)) {
        dev->data_find.sync_marks = dev->data_find.bits_obtained =
        dev->data_find.bytes_obtained                            = 0;
        dev->error_condition                                     = 0;
        dev->state                                               = STATE_IDLE;
        fdc_nodataam(drv->fdc);
        return;
    }

    if ((dev->last_word[side] == req_am) && (find->sync_marks >= 3)) {
        if (d86f_word_is_aligned(drv, side, find->sync_pos)) {
            dev->calc_crc.word = 0xcdb4;
            crc16_calc(dev->crc_table, decodefm(drv, dev->last_word[side]),
                       &(dev->calc_crc));
            find->sync_marks = find->bits_obtained  =
                               find->bytes_obtained = 0;
            find->sync_pos                          = 0xffffffff;
            dev->preceding_bit[side]                = dev->last_word[side] & 1;
            dev->state++;
            return;
        }
    }

    if ((ignore_other_am & 2) && (dev->last_word[side] == other_am) &&
        (find->sync_marks >= 3)) {
        if (d86f_word_is_aligned(drv, side, find->sync_pos)) {
            dev->calc_crc.word = 0xCDB4;
            crc16_calc(dev->crc_table, decodefm(drv, dev->last_word[side]),
                       &(dev->calc_crc));
            find->sync_marks = find->bits_obtained = find->bytes_obtained = 0;
            find->sync_pos                                                = 0xffffffff;
            if (ignore_other_am & 1) {
                /* Skip mode, let's go back to finding ID. */
                fdc_set_wrong_am(drv->fdc);
                dev->data_find.sync_marks = dev->data_find.bits_obtained  =
                                            dev->data_find.bytes_obtained = 0;
                dev->error_condition                                                                     = 0;
                dev->state                                                                               = STATE_IDLE;
                if (dev->state == STATE_02_READ_DATA)
                    fdc_track_finishread(drv->fdc, dev->error_condition);
                else if (dev->state == STATE_11_SCAN_DATA)
                    fdc_sector_finishcompare(drv->fdc, (dev->satisfying_bytes ==
                                             ((128 << ((uint32_t) dev->last_sector.id.n)) - 1)) ? 1 : 0);
                else
                    fdc_sector_finishread(drv->fdc);
            } else {
                /* Not skip mode, process the sector anyway. */
                fdc_set_wrong_am(drv->fdc);
                dev->preceding_bit[side] = dev->last_word[side] & 1;
                dev->state++;
            }
            return;
        }
    }

    if (dev->last_word[side] != 0x4489) {
        if (d86f_word_is_aligned(drv, side, find->sync_pos)) {
            find->sync_marks = find->bits_obtained = find->bytes_obtained = 0;
            find->sync_pos                                                = 0xffffffff;
        }
    }
}

/* When writing in MFM mode, we find the beginning of the address mark by looking for 3 0xA1 sync bytes. */
void
d86f_write_find_address_mark_mfm(void *priv, const int side, find_t *find)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *     dev = (d86f_t *) drv->d86f_priv;

    d86f_get_bit(drv, side);

    if (dev->last_word[side] == 0x4489) {
        find->sync_marks++;
        find->sync_pos = dev->track_pos;
        if (find->sync_marks == 3) {
            dev->calc_crc.word       = 0xcdb4;
            dev->preceding_bit[side] = 1;
            find->sync_marks         = 0;
            dev->state++;
            return;
        }
    }

    /* If we hadn't found enough address mark sync marks, null the counter. */
    if (dev->last_word[side] != 0x4489) {
        if (d86f_word_is_aligned(drv, side, find->sync_pos)) {
            find->sync_marks = find->bits_obtained = find->bytes_obtained = 0;
            find->sync_pos                                                = 0xffffffff;
        }
    }
}

/* State 2: Read sector ID and CRC*/
void
d86f_read_sector_id(void *priv, const int side, const int match)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *     dev = (d86f_t *) drv->d86f_priv;

    if (dev->id_find.bits_obtained) {
        if (!(dev->id_find.bits_obtained & 15)) {
            /* We've got a byte. */
            if (dev->id_find.bytes_obtained < 4) {
                dev->last_sector.byte_array[dev->id_find.bytes_obtained] =
                decodefm(drv, dev->last_word[side]);
                crc16_calc(dev->crc_table,
                           dev->last_sector.byte_array[dev->id_find.bytes_obtained],
                           &(dev->calc_crc));
            } else if ((dev->id_find.bytes_obtained >= 4) &&
                       (dev->id_find.bytes_obtained < 6)) {
                dev->track_crc.bytes[(dev->id_find.bytes_obtained & 1) ^ 1] =
                decodefm(drv, dev->last_word[side]);
            }
            dev->id_find.bytes_obtained++;

            if (dev->id_find.bytes_obtained == 6) {
                /* We've got the ID. */
                if ((dev->calc_crc.word != dev->track_crc.word) &&
                    (dev->last_sector.dword == dev->req_sector.dword)) {
                    dev->id_find.sync_marks = dev->id_find.bits_obtained =
                    dev->id_find.bytes_obtained = 0;
                    d86f_log("86F: ID CRC error: %04X != %04X (%08X)\n",
                             dev->track_crc.word, dev->calc_crc.word,
                             dev->last_sector.dword);
                    if ((dev->state != STATE_02_READ_ID) && (dev->state != STATE_0A_READ_ID)) {
                        dev->error_condition = 0;
                        dev->state           = STATE_IDLE;
                        fdc_headercrcerror(drv->fdc);
                    } else if (dev->state == STATE_0A_READ_ID)
                        dev->state--;
                    else {
                        dev->error_condition |= 1; /* Mark that there was an ID CRC error. */
                        dev->state++;
                    }
                } else if ((dev->calc_crc.word == dev->track_crc.word) &&
                           (dev->state == STATE_0A_READ_ID)) {
                    /* CRC is valid and this is a read sector ID command. */
                    dev->id_find.sync_marks = dev->id_find.bits_obtained =
                    dev->id_find.bytes_obtained = dev->error_condition = 0;
                    fdc_sectorid(drv->fdc,
                                 dev->last_sector.id.c, dev->last_sector.id.h,
                                 dev->last_sector.id.r, dev->last_sector.id.n, 0, 0);
                    dev->state = STATE_IDLE;
                } else {
                    /* CRC is valid. */
                    dev->id_find.sync_marks = dev->id_find.bits_obtained =
                    dev->id_find.bytes_obtained = 0;
                    dev->id_found |= 1;
                    if ((dev->last_sector.dword == dev->req_sector.dword) || !match) {
                        drv->d86f_handler.set_sector(drv, side,
                                                     dev->last_sector.id.c,
                                                     dev->last_sector.id.h,
                                                     dev->last_sector.id.r,
                                                     dev->last_sector.id.n);
                        if (dev->state == STATE_02_READ_ID) {
                            /* READ TRACK command, we need some special handling here.

                               Code corrected: Only the C, H, and N portions of the
                                               sector ID are compared, the R portion
                                               (the sector number) is ignored.
                             */
                            if ((dev->last_sector.id.c != fdc_get_read_track_sector(drv->fdc).id.c) ||
                                (dev->last_sector.id.h != fdc_get_read_track_sector(drv->fdc).id.h) ||
                                (dev->last_sector.id.n != fdc_get_read_track_sector(drv->fdc).id.n)) {
                                /* Mark that the sector ID is not the one expected by the FDC. */
                                dev->error_condition |= 4;
                                /* Make sure we use the sector size from the FDC. */
                                dev->last_sector.id.n = fdc_get_read_track_sector(drv->fdc).id.n;
                            }

                            /*
                               If the two ID's are identical, then we do not need to do
                               anything regarding the sector size.
                             */
                        }
                        dev->state++;
                    } else {
                        if (dev->last_sector.id.c != dev->req_sector.id.c) {
                            if (dev->last_sector.id.c == 0xFF) {
                                dev->error_condition |= 8;
                            } else {
                                dev->error_condition |= 0x10;
                            }
                        }

                        dev->state--;
                    }
                }
            }
        }
    }

    d86f_get_bit(drv, side);

    dev->id_find.bits_obtained++;
}

uint8_t
d86f_get_data(void *priv, const int base)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *    dev  = (d86f_t *) drv->d86f_priv;
    int     data;
    int     byte_count;

    if (fdd_get_turbo(drv) && (dev->version == 0x0063))
        byte_count = dev->turbo_pos;
    else
        byte_count = dev->data_find.bytes_obtained;

    if (byte_count < (d86f_get_data_len(drv) + base)) {
        data = fdc_getdata(drv->fdc, byte_count == (d86f_get_data_len(drv) + base - 1));
        if ((data & DMA_OVER) || (data == -1)) {
            dev->dma_over++;
            if (data == -1)
                data = 0;
            else
                data &= 0xff;
        }
    } else {
        data = 0;
    }

    return data;
}

void
d86f_compare_byte(void *priv, const uint8_t received_byte, const uint8_t disk_byte)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *     dev = (d86f_t *) drv->d86f_priv;

    switch (fdc_get_compare_condition(drv->fdc)) {
        case 0: /* SCAN EQUAL */
            if ((received_byte == disk_byte) || (received_byte == 0xff))
                dev->satisfying_bytes++;
            break;

        case 1: /* SCAN LOW OR EQUAL */
            if ((received_byte <= disk_byte) || (received_byte == 0xff))
                dev->satisfying_bytes++;
            break;

        case 2: /* SCAN HIGH OR EQUAL */
            if ((received_byte >= disk_byte) || (received_byte == 0xff))
                dev->satisfying_bytes++;
            break;

        default:
            break;
    }
}

/* State 4: Read sector data and CRC*/
void
d86f_read_sector_data(void *priv, const int side)
{
    fdd_drive_t *drv        = (fdd_drive_t *) priv;
    d86f_t *     dev        = (d86f_t *) drv->d86f_priv;
    uint32_t     sector_len = dev->last_sector.id.n;
    uint32_t     crc_pos    = 0;

    sector_len              = 1 << (7 + sector_len);
    crc_pos                 = sector_len + 2;

    if (dev->data_find.bits_obtained) {
        if (!(dev->data_find.bits_obtained & 15)) {
            /* We've got a byte. */
            d86f_log("86F: We've got a byte.\n");
            if (dev->data_find.bytes_obtained < sector_len) {
                int data = 0;
                if (drv->d86f_handler.read_data != NULL)
                    data = drv->d86f_handler.read_data(drv, side,
                                                       dev->data_find.bytes_obtained);
                else {
#ifdef HACK_FOR_DBASE_III
                    if ((dev->last_sector.id.c == 39) && (dev->last_sector.id.h == 0) &&
                        (dev->last_sector.id.r == 5) && (dev->data_find.bytes_obtained >= 272))
                        data = (random_generate() & 0xff);
                    else
#endif
                        data = decodefm(drv, dev->last_word[side]);
                }
                if (dev->state == STATE_11_SCAN_DATA) {
                    /* Scan/compare command. */
                    const int recv_data = d86f_get_data(drv, 0);
                    d86f_compare_byte(drv, recv_data, data);
                } else {
                    if (dev->data_find.bytes_obtained < d86f_get_data_len(drv)) {
                        if (dev->state != STATE_16_VERIFY_DATA) {
                            const int read_status = fdc_data(drv->fdc, data,
                                                    dev->data_find.bytes_obtained ==
                                                    (d86f_get_data_len(drv) - 1));
                            if (read_status == -1)
                                dev->dma_over++;
                        }
                    }
                }
                crc16_calc(dev->crc_table, data, &(dev->calc_crc));
            } else if (dev->data_find.bytes_obtained < crc_pos)
                dev->track_crc.bytes[(dev->data_find.bytes_obtained - sector_len) ^ 1] =
                decodefm(drv, dev->last_word[side]);
            dev->data_find.bytes_obtained++;

            if (dev->data_find.bytes_obtained == (crc_pos + fdc_get_gap(drv->fdc))) {
                /* We've got the data. */
                if ((dev->calc_crc.word != dev->track_crc.word) &&
                    (dev->state != STATE_02_READ_DATA)) {
                    d86f_log("86F: Data CRC error: %04X != %04X (%08X)\n", dev->track_crc.word,
                             dev->calc_crc.word, dev->last_sector.dword);
                    dev->data_find.sync_marks = dev->data_find.bits_obtained  =
                                                dev->data_find.bytes_obtained = 0;
                    dev->error_condition          = 0;
                    dev->state                    = STATE_IDLE;
                    fdc_datacrcerror(drv->fdc);
                } else if ((dev->calc_crc.word != dev->track_crc.word) &&
                           (dev->state == STATE_02_READ_DATA)) {
                    dev->data_find.sync_marks = dev->data_find.bits_obtained  =
                                                dev->data_find.bytes_obtained = 0;
                    dev->error_condition |= 2; /* Mark that there was a data error. */
                    dev->state = STATE_IDLE;
                    fdc_track_finishread(drv->fdc, dev->error_condition);
                } else {
                    /* CRC is valid. */
                    d86f_log("86F: Data CRC OK: %04X == %04X (%08X)\n",
                             dev->track_crc.word, dev->calc_crc.word,
                             dev->last_sector.dword);
                    dev->data_find.sync_marks = dev->data_find.bits_obtained  =
                                                dev->data_find.bytes_obtained = 0;
                    dev->error_condition                                                                     = 0;
                    dev->state                                                                               = STATE_IDLE;
                    if (dev->state == STATE_02_READ_DATA)
                        fdc_track_finishread(drv->fdc, dev->error_condition);
                    else if (dev->state == STATE_11_SCAN_DATA)
                        fdc_sector_finishcompare(drv->fdc, (dev->satisfying_bytes ==
                                                 ((128 << ((uint32_t) dev->last_sector.id.n)) - 1)) ? 1 : 0);
                    else
                        fdc_sector_finishread(drv->fdc);
                }
            }
        }
    }

    d86f_get_bit(drv, side);

    dev->data_find.bits_obtained++;
}

void
d86f_write_sector_data(void *priv, const int side, const int mfm, const uint16_t am)
{
    fdd_drive_t *drv        = (fdd_drive_t *) priv;
    d86f_t  *    dev        = (d86f_t *) drv->d86f_priv;
    uint32_t     sector_len = dev->last_sector.id.n;
    uint32_t     crc_pos    = 0;
    uint16_t temp;

    sector_len              = (1 << (7 + sector_len)) + 1;
    crc_pos                 = sector_len + 2;

    if (!(dev->data_find.bits_obtained & 15)) {
        if (dev->data_find.bytes_obtained < crc_pos) {
            if (!dev->data_find.bytes_obtained) {
                /* We're writing the address mark. */
                dev->current_byte[side] = am;
            } else if (dev->data_find.bytes_obtained < sector_len) {
                /* We're in the data field of the sector, read byte from FDC and request new byte. */
                dev->current_byte[side] = d86f_get_data(drv, 1);
                if (!fdc_get_diswr(drv->fdc))
                    drv->d86f_handler.write_data(drv, side,
                                                 dev->data_find.bytes_obtained - 1,
                                                 dev->current_byte[side]);
            } else {
                /* We're in the data field of the sector, use a CRC byte. */
                dev->current_byte[side] = dev->calc_crc.bytes[dev->data_find.bytes_obtained & 1];
            }

            dev->current_bit[side] = (15 - (dev->data_find.bits_obtained & 15)) >> 1;

            /* Write the bit. */
            temp = (dev->current_byte[side] >> dev->current_bit[side]) & 1;
            if ((!temp && !dev->preceding_bit[side]) || !mfm) {
                temp |= 2;
            }

            /* This is an even bit, so write the clock. */
            if (!dev->data_find.bytes_obtained) {
                /* Address mark, write bit directly. */
                d86f_put_bit(drv, side, am >> 15);
            } else {
                d86f_put_bit(drv, side, temp >> 1);
            }

            if (dev->data_find.bytes_obtained < sector_len) {
                /* This is a data byte, so CRC it. */
                if (!dev->data_find.bytes_obtained)
                    crc16_calc(dev->crc_table, decodefm(drv, am),
                               &(dev->calc_crc));
                else
                    crc16_calc(dev->crc_table, dev->current_byte[side],
                               &(dev->calc_crc));
            }
        }
    } else {
        if (dev->data_find.bytes_obtained < crc_pos) {
            /* Encode the bit. */
            const uint16_t bit_pos = 15 - (dev->data_find.bits_obtained & 15);
            dev->current_bit[side] = bit_pos >> 1;

            temp = (dev->current_byte[side] >> dev->current_bit[side]) & 1;
            if ((!temp && !dev->preceding_bit[side]) || !mfm) {
                temp |= 2;
            }

            if (!dev->data_find.bytes_obtained) {
                /* Address mark, write directly. */
                d86f_put_bit(drv, side, am >> bit_pos);
                if (!(bit_pos & 1))
                    dev->preceding_bit[side] = am >> bit_pos;
            } else {
                if (bit_pos & 1)
                    /* Clock bit */
                    d86f_put_bit(drv, side, temp >> 1);
                else {
                    /* Data bit */
                    d86f_put_bit(drv, side, temp & 1);
                    dev->preceding_bit[side] = temp & 1;
                }
            }
        }

        if ((dev->data_find.bits_obtained & 15) == 15) {
            dev->data_find.bytes_obtained++;

            if (dev->data_find.bytes_obtained == (crc_pos + fdc_get_gap(drv->fdc))) {
                /* We've written the data. */
                dev->data_find.sync_marks = dev->data_find.bits_obtained = dev->data_find.bytes_obtained = 0;
                dev->error_condition                                                                     = 0;
                dev->state                                                                               = STATE_IDLE;
                fdc_sector_finishread(drv->fdc);
                return;
            }
        }
    }

    dev->data_find.bits_obtained++;
}

void
d86f_advance_bit(void *priv, const int side)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *     dev = (d86f_t *) drv->d86f_priv;

    dev->track_pos++;
    dev->track_pos %= drv->d86f_handler.get_raw_size(drv, side);

    if (dev->track_pos == drv->d86f_handler.index_hole_pos(drv, side)) {
        drv->d86f_handler.read_revolution(drv);

        if (dev->state != STATE_IDLE)
            dev->index_count++;
    }
}

void
d86f_advance_word(void *priv, const int side)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *     dev = (d86f_t *) drv->d86f_priv;

    dev->track_pos += 16;
    dev->track_pos %= drv->d86f_handler.get_raw_size(drv, side);

    if ((dev->track_pos == drv->d86f_handler.index_hole_pos(drv, side)) &&
                           (dev->state != STATE_IDLE))
        dev->index_count++;
}

void
d86f_spin_to_index(void *priv, const int side)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *     dev = (d86f_t *) drv->d86f_priv;

    d86f_get_bit(drv, side);
    d86f_get_bit(drv, side ^ 1);

    d86f_advance_bit(drv, side);

    if (dev->track_pos == drv->d86f_handler.index_hole_pos(drv, side)) {
        if (dev->state == STATE_0D_SPIN_TO_INDEX) {
            /* When starting format, reset format state to the beginning. */
            dev->preceding_bit[side] = 1;
            dev->format_state        = FMT_PRETRK_GAP0;
        }

        /* This is to make sure both READ TRACK and FORMAT TRACK command don't end prematurely. */
        dev->index_count = 0;
        dev->state++;
    }
}

void
d86f_write_direct_common(void *priv, const int side, const uint16_t byte, const uint8_t type,
                         const uint32_t pos)
{
    fdd_drive_t *drv          = (fdd_drive_t *) priv;
    d86f_t *     dev          = (d86f_t *) drv->d86f_priv;
    uint16_t     encoded_byte = 0;
    uint16_t     mask_data;
    decoded_t    dbyte;
    decoded_t    dpbyte;

    if (fdc_get_diswr(drv->fdc))
        return;

    dbyte.byte  = byte & 0xff;
    dpbyte.byte = dev->preceding_bit[side] & 0xff;

    if (type == 0) {
        /* Byte write. */
        encoded_byte = d86f_encode_byte(drv, 0, dbyte, dpbyte);
        dev->preceding_bit[side] = encoded_byte & 1;
        if (!d86f_reverse_bytes(drv)) {
            mask_data = encoded_byte >> 8;
            encoded_byte &= 0xFF;
            encoded_byte <<= 8;
            encoded_byte |= mask_data;
        }
    } else {
        /* Word write. */
        encoded_byte = byte;
        dev->preceding_bit[side] = (encoded_byte >> 8) & 1;
        if (d86f_reverse_bytes(drv)) {
            mask_data = encoded_byte >> 8;
            encoded_byte &= 0xFF;
            encoded_byte <<= 8;
            encoded_byte |= mask_data;
        }
    }

    if (d86f_has_surface_desc(drv)) {
        /* Inverted track data, clear bits are now set. */
        mask_data                          = ~dev->track_encoded_data[side][pos];
        /* Surface data. */
        const uint16_t mask_surface        = dev->track_surface_data[side][pos];

        /* Hole = surface & ~data, so holes are one. */
        const uint16_t mask_hole           = mask_surface & mask_data;
        /* Hole bits are ones again, set the surface data to that. */
        dev->track_surface_data[side][pos] = mask_hole;

        /* Force the data of any hole to zero. */
        encoded_byte &= ~mask_hole;
    }

    dev->track_encoded_data[side][pos] = encoded_byte;
    dev->last_word[side]               = encoded_byte;
}

void
d86f_write_direct(void *priv, const int side, const uint16_t byte, const uint8_t type)
{
    fdd_drive_t * drv = (fdd_drive_t *) priv;
    const d86f_t *dev = (d86f_t *) drv->d86f_priv;

    d86f_write_direct_common(drv, side, byte, type, dev->track_pos >> 4);
}

uint16_t
endian_swap(const uint16_t word)
{
    uint16_t temp = word & 0xff;

    temp <<= 8;
    temp |= (word >> 8);

    return temp;
}

void
d86f_format_finish(void *priv, const int side, const int mfm, UNUSED(uint16_t sc),
                   const uint16_t gap_fill, const int do_write)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *     dev = (d86f_t *) drv->d86f_priv;

    if (mfm && do_write) {
        if (do_write && (dev->track_pos == drv->d86f_handler.index_hole_pos(drv, side)))
            d86f_write_direct_common(drv, side, gap_fill, 0, 0);
    }

    dev->state = STATE_IDLE;

    int format_ok = drv->d86f_handler.format_track(drv, side,
                                                   dev->format_ids, dev->format_id_count,
                                                   dev->fill);

    if (format_ok && do_write)
        drv->d86f_handler.writeback(drv);

    dev->error_condition = 0;
    dev->datac           = 0;
    dev->format_id_count = 0;
    if (format_ok)
        fdc_sector_finishread(drv->fdc);
    else
        fdc_cannotformat(drv->fdc);
}

void
d86f_format_turbo_finish(void *priv, const int side, const int do_write)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *     dev = (d86f_t *) drv->d86f_priv;

    dev->state = STATE_IDLE;

    int format_ok = drv->d86f_handler.format_track(drv, side,
                                                   dev->format_ids, dev->format_id_count,
                                                   dev->fill);

    if (format_ok && do_write)
        drv->d86f_handler.writeback(drv);

    dev->error_condition = 0;
    dev->datac           = 0;
    dev->format_id_count = 0;
    if (format_ok)
        fdc_sector_finishread(drv->fdc);
    else
        fdc_cannotformat(drv->fdc);
}

void
d86f_format_track(void *priv, const int side, int do_write)
{
    fdd_drive_t *drv          = (fdd_drive_t *) priv;
    d86f_t *     dev          = (d86f_t *) drv->d86f_priv;
    uint16_t     sc           = 0;
    uint16_t     dtl          = 0;
    uint16_t     gap_fill     = 0x4e;
    int          gap_sizes[4] = { 0, 0, 0, 0 };
    int          am_len       = 0;
    int          sync_len     = 0;
    static int   id_count     = 4;
    const int    mfm          = d86f_is_mfm(drv);
    uint16_t     max_len;

    am_len                    = mfm ? 4 : 1;
    gap_sizes[0]              = mfm ? 80 : 40;
    gap_sizes[1]              = mfm ? 50 : 26;
    gap_sizes[2]              = fdc_get_gap2(drv->fdc, real_drive(drv->fdc, drv->id));
    gap_sizes[3]              = fdc_get_gap(drv->fdc);
    sync_len                  = mfm ? 12 : 6;
    sc                        = fdc_get_format_sectors(drv->fdc);
    dtl                       = 128 << fdc_get_format_n(drv->fdc);
    gap_fill                  = mfm ? 0x4e : 0xff;

    /* HD-COPY's "Is the data rate correct?" format. */
    if ((dev->version == 0x0063) && (fdc_get_format_n(drv->fdc) == 3) && (sc == 2))
        do_write = 0;

    switch (dev->format_state) {
        default:
            break;
        case FMT_PRETRK_GAP0:
            id_count = 4;
            break;
    }

    if (id_count < 4) {
        fdc_t *fdc = (fdc_t *) drv->fdc;
        if (fdc_data_available(fdc)) {
            int data = fdc_getdata(fdc, 0);
            if (data != -1)
                data &= 0xff;
            if ((data == -1) && (id_count < 3))
                data = 0;
            fdc->format_sector_id.byte_array[id_count] = data & 0xff;
            if (id_count == 3)
                fdc_stop_id_request(fdc);
            else
                fdc_request_next_sector_id(fdc);
            id_count++;
        }
    }

    switch (dev->format_state) {
        case FMT_POSTTRK_GAP4:
            max_len = 60000;
            if (do_write)
                d86f_write_direct(drv, side, gap_fill, 0);
            break;

        case FMT_PRETRK_GAP0:
            max_len = gap_sizes[0];
            if (do_write)
                d86f_write_direct(drv, side, gap_fill, 0);
            break;

        case FMT_SECTOR_ID_SYNC:
        case FMT_PRETRK_SYNC:
        case FMT_SECTOR_DATA_SYNC:
            max_len = sync_len;
            if (do_write)
                d86f_write_direct(drv, side, 0x00, 0);
            break;

        case FMT_PRETRK_IAM:
            max_len = am_len;
            if (mfm) {
                if (do_write) {
                    const uint16_t iam_mfm[4] = { 0x2452, 0x2452, 0x2452, 0x5255 };
                    d86f_write_direct(drv, side, iam_mfm[dev->datac], 1);
                }
            } else {
                if (do_write) {
                    const uint16_t iam_fm = 0xfaf7;
                    d86f_write_direct(drv, side, iam_fm, 1);
                }
            }
            break;

        case FMT_PRETRK_GAP1:
            max_len = gap_sizes[1];
            if (do_write)
                d86f_write_direct(drv, side, gap_fill, 0);
            break;

        case FMT_SECTOR_IDAM:
            max_len = am_len;
            if (mfm) {
                if (do_write) {
                    const uint16_t idam_mfm[4] = { 0x8944, 0x8944, 0x8944, 0x5455 };
                    d86f_write_direct(drv, side, idam_mfm[dev->datac], 1);
                }
                d86f_calccrc(dev, (dev->datac < 3) ? 0xa1 : 0xfe);
            } else {
                if (do_write) {
                    const uint16_t idam_fm = 0x7ef5;
                    d86f_write_direct(drv, side, idam_fm, 1);
                }
                d86f_calccrc(dev, 0xfe);
            }
            break;

        case FMT_SECTOR_ID: {
            fdc_t *fdc = (fdc_t *) drv->fdc;
            max_len = 4;
            if ((dev->datac == 3) && (dev->format_id_count < 256)) {
                memcpy(dev->format_ids[dev->format_id_count],
                       fdc->format_sector_id.byte_array,
                       sizeof(d86f_format_id_t));
                dev->format_id_count++;
            }
            if (do_write) {
                d86f_write_direct(drv, side,
                                  fdc->format_sector_id.byte_array[dev->datac], 0);
                d86f_calccrc(dev, fdc->format_sector_id.byte_array[dev->datac]);
            } else {
                if (dev->datac == 3) {
                    drv->d86f_handler.set_sector(drv, side,
                                                 fdc->format_sector_id.id.c,
                                                 fdc->format_sector_id.id.h,
                                                 fdc->format_sector_id.id.r,
                                                 fdc->format_sector_id.id.n);
                }
            }
            break;
        }
        case FMT_SECTOR_ID_CRC:
        case FMT_SECTOR_DATA_CRC:
            max_len = 2;
            if (do_write)
                d86f_write_direct(drv, side, dev->calc_crc.bytes[dev->datac ^ 1], 0);
            break;

        case FMT_SECTOR_GAP2:
            max_len = gap_sizes[2];
            if (do_write)
                d86f_write_direct(drv, side, gap_fill, 0);
            break;

        case FMT_SECTOR_DATAAM:
            max_len = am_len;
            if (mfm) {
                if (do_write) {
                    const uint16_t dataam_mfm[4] = { 0x8944, 0x8944, 0x8944, 0x4555 };
                    d86f_write_direct(drv, side, dataam_mfm[dev->datac], 1);
                }
                d86f_calccrc(dev, (dev->datac < 3) ? 0xA1 : 0xFB);
            } else {
                if (do_write) {
                    const uint16_t dataam_fm = 0x6ff5;
                    d86f_write_direct(drv, side, dataam_fm, 1);
                }
                d86f_calccrc(dev, 0xfb);
            }
            break;

        case FMT_SECTOR_DATA:
            max_len = dtl;
            if (do_write) {
                d86f_write_direct(drv, side, dev->fill, 0);
                drv->d86f_handler.write_data(drv, side, dev->datac, dev->fill);
            }
            d86f_calccrc(dev, dev->fill);
            break;

        case FMT_SECTOR_GAP3:
            max_len = gap_sizes[3];
            if (do_write)
                d86f_write_direct(drv, side, gap_fill, 0);
            break;

        default:
            max_len = 0;
            break;
    }

    dev->datac++;

    d86f_advance_word(drv, side);

    if ((dev->index_count) && ((dev->format_state < FMT_SECTOR_ID_SYNC) ||
                               (dev->format_state > FMT_SECTOR_GAP3))) {
        d86f_format_finish(drv, side, mfm, sc, gap_fill, do_write);
        return;
    }

    if (dev->datac >= max_len) {
        dev->datac = 0;
        dev->format_state++;

        switch (dev->format_state) {
            case FMT_SECTOR_IDAM:
            case FMT_SECTOR_DATAAM:
                dev->calc_crc.word = 0xffff;
                break;

            case FMT_POSTTRK_CHECK:
                if (dev->index_count) {
                    d86f_format_finish(drv, side, mfm, sc, gap_fill, do_write);
                    return;
                }
                dev->sector_count++;
                if (dev->sector_count < sc) {
                    /* Sector within allotted amount, change state to SECTOR_ID_SYNC. */
                    dev->format_state = FMT_SECTOR_ID_SYNC;
                } else {
                    dev->format_state = FMT_POSTTRK_GAP4;
                    dev->sector_count = 0;
                }
                break;

            case FMT_SECTOR_GAP3:
                if ((dev->sector_count + 1) >= sc)
                    break;
                fallthrough;
            case FMT_PRETRK_GAP1:
                if (id_count == 4) {
                    id_count = 0;
                    fdc_request_next_sector_id(drv->fdc);
                }
                break;

            default:
                break;
        }
    }
}

void
d86f_initialize_last_sector_id(void *priv, const int c, const int h,
                               const int r, const int n)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *     dev = (d86f_t *) drv->d86f_priv;

    dev->last_sector.id.c = c;
    dev->last_sector.id.h = h;
    dev->last_sector.id.r = r;
    dev->last_sector.id.n = n;
}

static uint8_t
d86f_sector_flags(void *priv, const int side, const uint8_t c,
                  const uint8_t h, const uint8_t r, const uint8_t n)
{
    fdd_drive_t * drv = (fdd_drive_t *) priv;
    const d86f_t *dev = (d86f_t *) drv->d86f_priv;

    if (dev->last_side_sector[side]) {
        sector_t *s = dev->last_side_sector[side];
        while (s) {
            if ((s->c == c) && (s->h == h) && (s->r == r) && (s->n == n))
                return s->flags;
            if (!s->prev)
                break;
            sector_t *t = s->prev;
            s = t;
        }
    }

    return 0x00;
}

void
d86f_turbo_read(void *priv, const int side)
{
    fdd_drive_t * drv   = (fdd_drive_t *) priv;
    d86f_t *      dev   = (d86f_t *) drv->d86f_priv;
    uint8_t       dat   = 0;
    const uint8_t flags = d86f_sector_flags(drv, side,
                                            dev->req_sector.id.c, dev->req_sector.id.h,
                                            dev->req_sector.id.r, dev->req_sector.id.n);

    if (drv->d86f_handler.read_data != NULL)
        dat = drv->d86f_handler.read_data(drv, side, dev->turbo_pos);
    else
        dat = (random_generate() & 0xff);

    if (dev->state == STATE_11_SCAN_DATA) {
        /* Scan/compare command. */
        const int recv_data = d86f_get_data(drv, 0);
        d86f_compare_byte(drv, recv_data, dat);
    } else {
        if (dev->turbo_pos < (128UL << dev->req_sector.id.n)) {
            if (dev->state != STATE_16_VERIFY_DATA) {
                const int read_status = fdc_data(drv->fdc, dat,
                                                 dev->turbo_pos == ((128UL << dev->req_sector.id.n) - 1));
                if (read_status == -1)
                    dev->dma_over++;
            }
        }
    }

    dev->turbo_pos++;

    if (dev->turbo_pos >= (128UL << dev->req_sector.id.n)) {
        dev->data_find.sync_marks = dev->data_find.bits_obtained  =
                                    dev->data_find.bytes_obtained = 0;
        if ((flags & SECTOR_CRC_ERROR) && (dev->state != STATE_02_READ_DATA)) {
#ifdef ENABLE_D86F_LOG
            d86f_log("86F: Data CRC error in turbo mode (%02X)\n", dev->state);
#endif
            dev->error_condition = 0;
            dev->state           = STATE_IDLE;
            fdc_datacrcerror(drv->fdc);
        } else if ((flags & SECTOR_CRC_ERROR) && (dev->state == STATE_02_READ_DATA)) {
#ifdef ENABLE_D86F_LOG
            d86f_log("86F: Data CRC error in turbo mode at READ TRACK command\n");
#endif
            dev->error_condition |= 2; /* Mark that there was a data error. */
            dev->state = STATE_IDLE;
            fdc_track_finishread(drv->fdc, dev->error_condition);
        } else {
            /* CRC is valid. */
#ifdef ENABLE_D86F_LOG
            d86f_log("86F: Data CRC OK in turbo mode\n");
#endif
            dev->error_condition = 0;
            dev->state           = STATE_IDLE;
            if (dev->state == STATE_11_SCAN_DATA)
                fdc_sector_finishcompare(drv->fdc,
                                         (dev->satisfying_bytes ==
                                          ((128 << ((uint32_t) dev->last_sector.id.n)) - 1)) ? 1 : 0);
            else
                fdc_sector_finishread(drv->fdc);
        }
    }
}

void
d86f_turbo_write(void *priv, const int side)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *     dev = (d86f_t *) drv->d86f_priv;
    uint8_t      dat = 0;

    dat = d86f_get_data(drv, 1);
    drv->d86f_handler.write_data(drv, side, dev->turbo_pos, dat);

    dev->turbo_pos++;

    if (dev->turbo_pos >= (128 << dev->last_sector.id.n)) {
        /* We've written the data. */
        dev->data_find.sync_marks = dev->data_find.bits_obtained  =
                                    dev->data_find.bytes_obtained = 0;
        dev->error_condition                                                                     = 0;
        dev->state                                                                               = STATE_IDLE;
        drv->d86f_handler.writeback(drv);
        fdc_sector_finishread(drv->fdc);
    }
}

void
d86f_turbo_format(void *priv, const int side, int nop)
{
    fdd_drive_t *  drv = (fdd_drive_t *) priv;
    d86f_t *       dev = (d86f_t *) drv->d86f_priv;
    const uint16_t sc  = fdc_get_format_sectors(drv->fdc);
    const uint16_t dtl = 128 << fdc_get_format_n(drv->fdc);

    /* HD-COPY's "Is the data rate correct?" format. */
    if ((dev->version == 0x0063) && (fdc_get_format_n(drv->fdc) == 3) && (sc == 2))
        nop = 1;

    if (dev->datac <= 3) {
        fdc_t *fdc = (fdc_t *) drv->fdc;
        int dat = fdc_getdata(fdc, 0);
        if (dat != -1)
            dat &= 0xff;
        if ((dat == -1) && (dev->datac < 3))
            dat = 0;
        fdc->format_sector_id.byte_array[dev->datac] = dat & 0xff;
        if (dev->datac == 3) {
            fdc_stop_id_request(fdc);
            if (dev->format_id_count < 256) {
                memcpy(dev->format_ids[dev->format_id_count],
                       fdc->format_sector_id.byte_array,
                       sizeof(d86f_format_id_t));
                dev->format_id_count++;
            }
            drv->d86f_handler.set_sector(drv, side,
                                         fdc->format_sector_id.id.c, fdc->format_sector_id.id.h,
                                         fdc->format_sector_id.id.r, fdc->format_sector_id.id.n);
        }
    } else if (dev->datac == 4) {
        if (!nop) {
            for (uint16_t i = 0; i < dtl; i++)
                drv->d86f_handler.write_data(drv, side, i, dev->fill);
        }

        dev->sector_count++;
    }

    dev->datac++;

    if (dev->datac == 6) {
        dev->datac = 0;

        if (dev->sector_count < sc) {
            /* Sector within allotted amount. */
            fdc_request_next_sector_id(drv->fdc);
        } else {
            dev->state = STATE_IDLE;
            d86f_format_turbo_finish(drv, side, !nop);
        }
    }
}

int
d86f_sector_is_present(void *priv, const int side, const uint8_t c, const uint8_t h,
                       const uint8_t r, const uint8_t n)
{
    fdd_drive_t * drv = (fdd_drive_t *) priv;
    const d86f_t *dev = (d86f_t *) drv->d86f_priv;

    if (dev->last_side_sector[side]) {
        sector_t *s = dev->last_side_sector[side];
        while (s) {
            if ((s->c == c) && (s->h == h) && (s->r == r) && (s->n == n))
                return 1;
            if (!s->prev)
                break;
            sector_t *t = s->prev;
            s = t;
        }
    }

    return 0;
}

void
d86f_turbo_poll(void *priv, const int side)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *     dev = (d86f_t *) drv->d86f_priv;

    if ((dev->state != STATE_IDLE) && (dev->state != STATE_SECTOR_NOT_FOUND) &&
        ((dev->state & 0xF8) != 0xE8)) {
        if (!d86f_can_read_address(drv)) {
            dev->id_find.sync_marks = dev->id_find.bits_obtained  =
                                      dev->id_find.bytes_obtained =
                                      dev->error_condition        = 0;
            fdc_noidam(drv->fdc);
            dev->state = STATE_IDLE;
            return;
        }
    }

    switch (dev->state) {
        case STATE_0D_SPIN_TO_INDEX:
            dev->sector_count = 0;
            dev->datac        = 5;
            fallthrough;

        case STATE_02_SPIN_TO_INDEX:
            dev->state++;
            return;

        case STATE_02_FIND_ID:
            if (!d86f_sector_is_present(drv, side,
                                        fdc_get_read_track_sector(drv->fdc).id.c,
                                        fdc_get_read_track_sector(drv->fdc).id.h,
                                        fdc_get_read_track_sector(drv->fdc).id.r,
                                        fdc_get_read_track_sector(drv->fdc).id.n)) {
                dev->id_find.sync_marks = dev->id_find.bits_obtained = dev->id_find.bytes_obtained = dev->error_condition = 0;
                fdc_nosector(drv->fdc);
                dev->state = STATE_IDLE;
                return;
            }
            dev->last_sector.id.c = fdc_get_read_track_sector(drv->fdc).id.c;
            dev->last_sector.id.h = fdc_get_read_track_sector(drv->fdc).id.h;
            dev->last_sector.id.r = fdc_get_read_track_sector(drv->fdc).id.r;
            dev->last_sector.id.n = fdc_get_read_track_sector(drv->fdc).id.n;
            drv->d86f_handler.set_sector(drv, side,
                                         dev->last_sector.id.c,
                                         dev->last_sector.id.h,
                                         dev->last_sector.id.r,
                                         dev->last_sector.id.n);
            dev->turbo_pos = 0;
            dev->state++;
            return;

        case STATE_05_FIND_ID:
        case STATE_09_FIND_ID:
        case STATE_06_FIND_ID:
        case STATE_0C_FIND_ID:
        case STATE_11_FIND_ID:
        case STATE_16_FIND_ID: {
            fdc_t *fdc = (fdc_t *) drv->fdc;
            if (!d86f_sector_is_present(drv, side,
                                        dev->req_sector.id.c,
                                        dev->req_sector.id.h,
                                        dev->req_sector.id.r,
                                        dev->req_sector.id.n)) {
                dev->id_find.sync_marks = dev->id_find.bits_obtained  =
                                          dev->id_find.bytes_obtained =
                                          dev->error_condition        = 0;
                if (d86f_sector_is_present(drv, side,
                                           fdc->pcn[dev->req_sector.id.h],
                                           dev->req_sector.id.h,
                                           dev->req_sector.id.r,
                                           dev->req_sector.id.n))
                    fdc_wrongcylinder(fdc);
                else
                    fdc_nosector(fdc);
                dev->state = STATE_IDLE;
                return;
                                        } else if (d86f_sector_flags(drv, side,
                                                                     dev->req_sector.id.c,
                                                                     dev->req_sector.id.h,
                                                                     dev->req_sector.id.r,
                                                                     dev->req_sector.id.n) & SECTOR_NO_ID) {
                                            dev->id_find.sync_marks = dev->id_find.bits_obtained  =
                                                                      dev->id_find.bytes_obtained =
                                                                      dev->error_condition        = 0;
                                            fdc_noidam(fdc);
                                            dev->state = STATE_IDLE;
                                            return;
                                                                     }
            dev->last_sector.id.c = dev->req_sector.id.c;
            dev->last_sector.id.h = dev->req_sector.id.h;
            dev->last_sector.id.r = dev->req_sector.id.r;
            dev->last_sector.id.n = dev->req_sector.id.n;
            drv->d86f_handler.set_sector(drv, side,
                                         dev->last_sector.id.c,
                                         dev->last_sector.id.h,
                                         dev->last_sector.id.r,
                                         dev->last_sector.id.n);
            fallthrough;
        }
        case STATE_0A_FIND_ID:
            dev->turbo_pos = 0;
            dev->state++;
            return;

        case STATE_0A_READ_ID:
            dev->id_find.sync_marks = dev->id_find.bits_obtained  =
                                      dev->id_find.bytes_obtained =
                                      dev->error_condition        = 0;
            fdc_sectorid(drv->fdc, dev->last_sector.id.c, dev->last_sector.id.h,
                                   dev->last_sector.id.r, dev->last_sector.id.n,
                                   0, 0);
            dev->state = STATE_IDLE;
            break;

        case STATE_02_READ_ID: case STATE_02_FIND_DATA:
        case STATE_05_READ_ID: case STATE_05_FIND_DATA:
        case STATE_06_READ_ID: case STATE_06_FIND_DATA:
        case STATE_09_READ_ID: case STATE_09_FIND_DATA:
        case STATE_0C_READ_ID: case STATE_0C_FIND_DATA:
        case STATE_11_READ_ID: case STATE_11_FIND_DATA:
        case STATE_16_READ_ID: case STATE_16_FIND_DATA:
            dev->state++;
            break;

        case STATE_02_READ_DATA:
        case STATE_06_READ_DATA:
        case STATE_0C_READ_DATA:
        case STATE_11_SCAN_DATA:
        case STATE_16_VERIFY_DATA:
            if (fdc_is_dma(drv->fdc))
                for (int i = 0; i < (128 << dev->last_sector.id.n); i++)
                    d86f_turbo_read(drv, side);
            else
                d86f_turbo_read(drv, side);
            break;

        case STATE_05_WRITE_DATA:
        case STATE_09_WRITE_DATA:
            if (fdc_is_dma(drv->fdc))
                for (int i = 0; i < (128 << dev->last_sector.id.n); i++)
                    d86f_turbo_write(drv, side);
            else
                d86f_turbo_write(drv, side);
            break;

        case STATE_0D_FORMAT_TRACK:
            if (fdc_is_dma(drv->fdc))
                while (dev->state == STATE_0D_FORMAT_TRACK)
                    d86f_turbo_format(drv, side, (side && (d86f_get_sides(drv) != 2)));
            else
                d86f_turbo_format(drv, side, (side && (d86f_get_sides(drv) != 2)));
            return;

        case STATE_IDLE:
        case STATE_SECTOR_NOT_FOUND:
        default:
            break;
    }
}

void
d86f_poll(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *     dev = (d86f_t *) drv->d86f_priv;

    int side = fdd_get_head(drv);
    if (!fdd_is_double_sided(drv))
        side = 0;

    const int mfm = fdc_is_mfm(drv->fdc);

    if ((dev->state & 0xF8) == 0xE8) {
        if (!d86f_can_format(drv))
            dev->state = STATE_SECTOR_NOT_FOUND;
    }

    if ((dev->state != STATE_IDLE) && (dev->state != STATE_SECTOR_NOT_FOUND) &&
        ((dev->state & 0xF8) != 0xE8) && !d86f_can_read_address(drv))
        dev->state = STATE_SECTOR_NOT_FOUND;

    /*
       Do normal poll if DENSEL is wrong, because Windows 95 is very strict about
       timings there.
     */
    if (fdd_get_turbo(drv) && (dev->version == 0x0063) &&
        (dev->state != STATE_SECTOR_NOT_FOUND)) {
        d86f_turbo_poll(drv, side);
        return;
    }

    if ((dev->state != STATE_02_SPIN_TO_INDEX) && (dev->state != STATE_0D_SPIN_TO_INDEX))
        d86f_get_bit(drv, side ^ 1);

    switch (dev->state) {
        case STATE_02_SPIN_TO_INDEX:
        case STATE_0D_SPIN_TO_INDEX:
            d86f_spin_to_index(drv, side);
            return;

        case STATE_02_FIND_ID:
        case STATE_05_FIND_ID:
        case STATE_09_FIND_ID:
        case STATE_06_FIND_ID:
        case STATE_0A_FIND_ID:
        case STATE_0C_FIND_ID:
        case STATE_11_FIND_ID:
        case STATE_16_FIND_ID:
            if (mfm)
                d86f_find_address_mark_mfm(drv, side, &(dev->id_find),0x5554, 0,
                                           0, 0);
            else
                d86f_find_address_mark_fm(drv, side, &(dev->id_find), 0xf57e, 0,
                                          0, 0);
            break;

        case STATE_0A_READ_ID:
        case STATE_02_READ_ID:
            d86f_read_sector_id(drv, side, 0);
            break;

        case STATE_05_READ_ID:
        case STATE_09_READ_ID:
        case STATE_06_READ_ID:
        case STATE_0C_READ_ID:
        case STATE_11_READ_ID:
        case STATE_16_READ_ID:
            d86f_read_sector_id(drv, side, 1);
            break;

        case STATE_02_FIND_DATA:
            if (mfm)
                d86f_find_address_mark_mfm(drv, side, &(dev->data_find), 0x5545,
                                           0x554a, 0x5554, 2);
            else
                d86f_find_address_mark_fm(drv, side, &(dev->data_find), 0xf56f,
                                          0xf56a, 0xf57e, 2);
            break;

        case STATE_06_FIND_DATA:
        case STATE_11_FIND_DATA:
        case STATE_16_FIND_DATA:
            if (mfm)
                d86f_find_address_mark_mfm(drv, side, &(dev->data_find), 0x5545,
                                           0x554a, 0x5554,
                                           fdc_is_sk(drv->fdc) | 2);
            else
                d86f_find_address_mark_fm(drv, side, &(dev->data_find), 0xf56f,
                                          0xf56a,0xf57e,
                                          fdc_is_sk(drv->fdc) | 2);
            break;

        case STATE_05_FIND_DATA:
        case STATE_09_FIND_DATA:
            if (mfm)
                d86f_write_find_address_mark_mfm(drv, side, &(dev->data_find));
            else
                d86f_write_find_address_mark_fm(drv, side, &(dev->data_find));
            break;

        case STATE_0C_FIND_DATA:
            if (mfm)
                d86f_find_address_mark_mfm(drv, side, &(dev->data_find), 0x554a,
                                           0x5545, 0x5554,
                                           fdc_is_sk(drv->fdc) | 2);
            else
                d86f_find_address_mark_fm(drv, side, &(dev->data_find), 0xf56a,
                                          0xf56f, 0xf57e,
                                          fdc_is_sk(drv->fdc) | 2);
            break;

        case STATE_02_READ_DATA:
        case STATE_06_READ_DATA:
        case STATE_0C_READ_DATA:
        case STATE_11_SCAN_DATA:
        case STATE_16_VERIFY_DATA:
            d86f_read_sector_data(drv, side);
            break;

        case STATE_05_WRITE_DATA:
            if (mfm)
                d86f_write_sector_data(drv, side, mfm, 0x5545);
            else
                d86f_write_sector_data(drv, side, mfm, 0xF56F);
            break;

        case STATE_09_WRITE_DATA:
            if (mfm)
                d86f_write_sector_data(drv, side, mfm, 0x554A);
            else
                d86f_write_sector_data(drv, side, mfm, 0xF56A);
            break;

        case STATE_0D_FORMAT_TRACK:
            if (!(dev->track_pos & 15))
                d86f_format_track(drv, side, (!side || (d86f_get_sides(drv) == 2)) &&
                                             (dev->version == D86FVER));
            return;

        case STATE_IDLE:
        case STATE_SECTOR_NOT_FOUND:
        default:
            d86f_get_bit(drv, side);
            break;
    }

    d86f_advance_bit(drv, side);

    if ((dev->index_count == 2) && (dev->state != STATE_IDLE)) {
        switch (dev->state) {
            case STATE_0A_FIND_ID:
            case STATE_SECTOR_NOT_FOUND:
                dev->state = STATE_IDLE;
                fdc_noidam(drv->fdc);
                break;

            case STATE_02_FIND_DATA:
            case STATE_06_FIND_DATA:
            case STATE_11_FIND_DATA:
            case STATE_16_FIND_DATA:
            case STATE_05_FIND_DATA:
            case STATE_09_FIND_DATA:
            case STATE_0C_FIND_DATA:
                dev->state = STATE_IDLE;
                fdc_nodataam(drv->fdc);
                break;

            case STATE_02_SPIN_TO_INDEX:
            case STATE_02_READ_DATA:
            case STATE_05_WRITE_DATA:
            case STATE_06_READ_DATA:
            case STATE_09_WRITE_DATA:
            case STATE_0C_READ_DATA:
            case STATE_0D_SPIN_TO_INDEX:
            case STATE_0D_FORMAT_TRACK:
            case STATE_11_SCAN_DATA:
            case STATE_16_VERIFY_DATA:
                /*
                   In these states, we should *NEVER* care about how many index pulses
                   there have been.
                 */
                break;

            default:
                dev->state = STATE_IDLE;
                if (dev->id_found) {
                    if (dev->error_condition & 0x18) {
                        if ((dev->error_condition & 0x18) == 0x08)
                            fdc_badcylinder(drv->fdc);
                        if ((dev->error_condition & 0x10) == 0x10)
                            fdc_wrongcylinder(drv->fdc);
                        else
                            fdc_nosector(drv->fdc);
                    } else
                        fdc_nosector(drv->fdc);
                } else
                    fdc_noidam(drv->fdc);
                break;
        }
    }
}

void
d86f_reset_index_hole_pos(void *priv, const int side)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *     dev = (d86f_t *) drv->d86f_priv;

    dev->index_hole_pos[side] = 0;
}

uint16_t
d86f_prepare_pretrack(void *priv, const int side, const int iso)
{
    fdd_drive_t *  drv           = (fdd_drive_t *) priv;
    d86f_t  *      dev           = (d86f_t *) drv->d86f_priv;

    const int      mfm           = d86f_is_mfm(drv);
    const int      real_gap0_len = mfm ? 80 : 40;
    const int      sync_len      = mfm ? 12 : 6;
    const int      real_gap1_len = mfm ? 50 : 26;
    const uint16_t gap_fill      = mfm ? 0x4e : 0xff;
    uint32_t       raw_size      = drv->d86f_handler.get_raw_size(drv, side);

    if (raw_size & 15)
        raw_size = (raw_size >> 4) + 1;
    else
        raw_size = (raw_size >> 4);

    dev->index_hole_pos[side] = 0;

    d86f_destroy_linked_lists(drv, side);

    for (uint32_t i = 0; i < raw_size; i++)
        d86f_write_direct_common(drv, side, gap_fill, 0, i);

    uint16_t pos = 0;

    if (!iso) {
        for (int i = 0; i < real_gap0_len; i++) {
            d86f_write_direct_common(drv, side, gap_fill, 0, pos);
            pos = (pos + 1) % raw_size;
        }
        for (int i = 0; i < sync_len; i++) {
            d86f_write_direct_common(drv, side, 0, 0, pos);
            pos = (pos + 1) % raw_size;
        }
        if (mfm) {
            for (uint8_t i = 0; i < 3; i++) {
                d86f_write_direct_common(drv, side, 0x2452, 1, pos);
                pos = (pos + 1) % raw_size;
            }
        }

        const uint16_t iam_fm  = 0xfaf7;
        const uint16_t iam_mfm = 0x5255;

        d86f_write_direct_common(drv, side, mfm ? iam_mfm : iam_fm, 1, pos);
        pos = (pos + 1) % raw_size;
    }

    for (int i = 0; i < real_gap1_len; i++) {
        d86f_write_direct_common(drv, side, gap_fill, 0, pos);
        pos = (pos + 1) % raw_size;
    }

    return pos;
}

uint16_t
d86f_prepare_sector(void *priv, const int side, const int prev_pos, uint8_t *id_buf,
                    uint8_t *data_buf, const int data_len, const int gap2, const int gap3,
                    const int flags)
{
    fdd_drive_t *  drv = (fdd_drive_t *) priv;
    d86f_t *       dev = (d86f_t *) drv->d86f_priv;
    int            i;

    if (fdd_get_turbo(drv) && (dev->version == 0x0063)) {
        sector_t *s = (sector_t *) calloc(1, sizeof(sector_t));
        s->c     = id_buf[0];
        s->h     = id_buf[1];
        s->r     = id_buf[2];
        s->n     = id_buf[3];
        s->flags = flags;
        if (dev->last_side_sector[side])
            s->prev = dev->last_side_sector[side];
        dev->last_side_sector[side] = s;
    }

    const int      mfm      = d86f_is_mfm(drv);
    const int      sync_len = mfm ? 12 : 6;
    const uint16_t gap_fill = mfm ? 0x4e : 0xff;
    uint32_t       raw_size = drv->d86f_handler.get_raw_size(drv, side);
    uint16_t       pos      = prev_pos;

    if (raw_size & 15)
        raw_size = (raw_size >> 4) + 1;
    else
        raw_size = (raw_size >> 4);

    if (!(flags & SECTOR_NO_ID)) {
        for (i = 0; i < sync_len; i++) {
            d86f_write_direct_common(drv, side, 0, 0, pos);
            pos = (pos + 1) % raw_size;
        }

        dev->calc_crc.word = 0xffff;
        if (mfm) {
            for (i = 0; i < 3; i++) {
                d86f_write_direct_common(drv, side, 0x8944, 1, pos);
                pos = (pos + 1) % raw_size;
                d86f_calccrc(dev, 0xa1);
            }
        }
        const uint16_t idam_fm  = 0x7ef5;
        const uint16_t idam_mfm = 0x5455;
        d86f_write_direct_common(drv, side, mfm ? idam_mfm : idam_fm, 1, pos);
        pos = (pos + 1) % raw_size;
        d86f_calccrc(dev, 0xfe);
        for (i = 0; i < 4; i++) {
            d86f_write_direct_common(drv, side, id_buf[i], 0, pos);
            pos = (pos + 1) % raw_size;
            d86f_calccrc(dev, id_buf[i]);
        }
        for (i = 1; i >= 0; i--) {
            d86f_write_direct_common(drv, side, dev->calc_crc.bytes[i], 0, pos);
            pos = (pos + 1) % raw_size;
        }
        for (i = 0; i < gap2; i++) {
            d86f_write_direct_common(drv, side, gap_fill, 0, pos);
            pos = (pos + 1) % raw_size;
        }
    }

    if (!(flags & SECTOR_NO_DATA)) {
        for (i = 0; i < sync_len; i++) {
            d86f_write_direct_common(drv, side, 0, 0, pos);
            pos = (pos + 1) % raw_size;
        }
        dev->calc_crc.word = 0xffff;
        if (mfm) {
            for (i = 0; i < 3; i++) {
                d86f_write_direct_common(drv, side, 0x8944, 1, pos);
                pos = (pos + 1) % raw_size;
                d86f_calccrc(dev, 0xA1);
            }
        }
        const uint16_t dataam_fm   = 0x6ff5;
        const uint16_t datadam_fm  = 0x6af5;
        const uint16_t dataam_mfm  = 0x4555;
        const uint16_t datadam_mfm = 0x4a55;
        d86f_write_direct_common(drv, side,
                                 mfm ? ((flags & SECTOR_DELETED_DATA) ?
                                     datadam_mfm : dataam_mfm) :
                                     ((flags & SECTOR_DELETED_DATA) ? datadam_fm :
                                         dataam_fm),
                                 1, pos);
        pos = (pos + 1) % raw_size;
        d86f_calccrc(dev, (flags & SECTOR_DELETED_DATA) ? 0xF8 : 0xFB);
        if (data_len > 0) {
            for (i = 0; i < data_len; i++) {
                d86f_write_direct_common(drv, side, data_buf[i], 0, pos);
                pos = (pos + 1) % raw_size;
                d86f_calccrc(dev, data_buf[i]);
            }
            if (!(flags & SECTOR_CRC_ERROR)) {
                for (i = 1; i >= 0; i--) {
                    d86f_write_direct_common(drv, side, dev->calc_crc.bytes[i], 0, pos);
                    pos = (pos + 1) % raw_size;
                }
            }
            for (i = 0; i < gap3; i++) {
                d86f_write_direct_common(drv, side, gap_fill, 0, pos);
                pos = (pos + 1) % raw_size;
            }
        }
    }

    return pos;
}

/*
   Note on handling of tracks on thick track drives:

   - On seek, encoded data is constructed from both (track << 1) and
     ((track << 1) + 1);

   - Any bits that differ are treated as thus:
   - Both are regular but contents differ -> Output is fuzzy;
   - One is regular and one is fuzzy -> Output is fuzzy;
   - Both are fuzzy -> Output is fuzzy;
   - Both are physical holes -> Output is a physical hole;
   - One is regular and one is a physical hole -> Output is fuzzy,
     the hole half is handled appropriately on writeback;
   - One is fuzzy and one is a physical hole -> Output is fuzzy,
     the hole half is handled appropriately on writeback;
   - On write back, apart from the above notes, the final two tracks
     are written;
   - Destination ALWAYS has surface data even if the image does not.

   In case of a thin track drive, tracks are handled normally.
 */
void
d86f_construct_encoded_buffer(void *priv, const int side)
{
    fdd_drive_t *   drv    = (fdd_drive_t *) priv;
    d86f_t *        dev    = (d86f_t *) drv->d86f_priv;

    /*
       *_fuzm are fuzzy bit masks, *_holm are hole masks, dst_neim are masks is mask for
       bits that are neither fuzzy nor holes in both, and src1_d and src2_d are filtered
       source data.
     */
    uint16_t       *dst    = dev->track_encoded_data[side];
    uint16_t       *dst_s  = dev->track_surface_data[side];
    const uint16_t *src1   = dev->thin_track_encoded_data[0][side];
    const uint16_t *src1_s = dev->thin_track_surface_data[0][side];
    const uint16_t *src2   = dev->thin_track_encoded_data[1][side];
    const uint16_t *src2_s = dev->thin_track_surface_data[1][side];
    const uint32_t  len    = d86f_get_array_size(drv, side, 1);

    for (uint32_t i = 0; i < len; i++) {
        /* The two bits differ. */
        if (d86f_has_surface_desc(drv)) {
            /* Source image has surface description data, so we have some more handling to do. */
            const uint16_t src1_fuzm = src1[i] & src1_s[i];
            const uint16_t src2_fuzm = src2[i] & src2_s[i];
            /* The bits that remain set are fuzzy in either one or the other or both. */
            const uint16_t dst_fuzm  = src1_fuzm | src2_fuzm;
            const uint16_t src1_holm = ~src1[i] & src1_s[i];
            const uint16_t src2_holm = ~src2[i] & src2_s[i];
            /* The bits that remain set are holes in both. */
            const uint16_t dst_holm  = src1_holm & src2_holm;
            /*
               The bits that remain set are those that are neither fuzzy nor are holes
               in both.
             */
            const uint16_t dst_neim  = ~(dst_fuzm | dst_holm);
            const uint16_t src1_d    = src1[i] & dst_neim;
            const uint16_t src2_d    = src2[i] & dst_neim;

            dst_s[i] = ~dst_neim;                  /* The set bits are those that are either fuzzy or are
                                                      holes in both. */
            dst[i] = (src1_d | src2_d);            /* Initial data is remaining data from Source 1 and
                                                      Source 2. */
            dst[i] |= dst_fuzm;                    /* Add to it the fuzzy bytes (holes have surface bit set
                                                      but data bit clear). */
        } else {
            /* No surface data, the handling is much simpler - a simple OR. */
            dst[i]   = src1[i] | src2[i];
            dst_s[i] = 0;
        }
    }
}

/* Decomposition is easier since we at most have to care about the holes. */
void
d86f_decompose_encoded_buffer(void *priv, const int side)
{
    fdd_drive_t *   drv    = (fdd_drive_t *) priv;
    d86f_t *        dev    = (d86f_t *) drv->d86f_priv;
    const uint32_t  len    = d86f_get_array_size(drv, side, 1);
    const uint16_t *dst    = drv->d86f_handler.encoded_data(drv, side);
    const uint16_t *dst_s  = dev->track_surface_data[side];
    uint16_t       *src1   = dev->thin_track_encoded_data[0][side];
    uint16_t       *src1_s = dev->thin_track_surface_data[0][side];
    uint16_t       *src2   = dev->thin_track_encoded_data[1][side];
    uint16_t       *src2_s = dev->thin_track_surface_data[1][side];

    for (uint32_t i = 0; i < len; i++) {
        if (d86f_has_surface_desc(drv)) {
            /* Source image has surface description data, so we have some more handling to do.
               We need hole masks for both buffers. Holes have data bit clear and surface bit set. */
            src1_s[i] = src2_s[i] = dst_s[i]; /* Write the new holes and weak bits. */
            const uint16_t temp      = ~src1[i] & src1_s[i]; /* Bits that are clear in data and set in surface are holes. */
            const uint16_t temp2     = ~src2[i] & src2_s[i]; /* Bits that are clear in data and set in surface are holes. */
            src1[i]   = dst[i] & ~temp;       /* Make sure the holes' bits are cleared in the decomposed buffer. */
            src1_s[i] |= temp;                /* Make sure the holes' bits are set in the decomposed surface. */
            src2[i]   = dst[i] & ~temp2;      /* Make sure the holes' bits are cleared in the decomposed buffer. */
            src2_s[i] |= temp2;               /* Make sure the holes' bits are set in the decomposed surface. */
        } else
            src1[i] = src2[i] = dst[i];
    }
}

int
d86f_track_header_size(void *priv)
{
    fdd_drive_t *drv  = (fdd_drive_t *) priv;
    int          temp = 6;

    if (d86f_has_extra_bit_cells(drv))
        temp += 4;

    return temp;
}

void
d86f_read_track(void *priv, const int track, const int thin_track, const int side,
                uint16_t *da, uint16_t *sa)
{
    fdd_drive_t *drv           = (fdd_drive_t *) priv;
    d86f_t *     dev           = (d86f_t *) drv->d86f_priv;
    int          logical_track = 0;

    if (d86f_get_sides(drv) == 2)
        logical_track = ((track + thin_track) << 1) + side;
    else
        logical_track = track + thin_track;

    if (dev->track_offset[logical_track]) {
        if (!thin_track) {
            if (fseek(dev->fp, (off_t) dev->track_offset[logical_track], SEEK_SET) == -1)
                fatal("d86f_read_track(): Error seeking to offset "
                          "dev->track_offset[logical_track]\n");
            if (fread(&(dev->side_flags[side]), 1, 2, dev->fp) != 2)
                fatal("d86f_read_track(): Error reading side flags\n");
            if (d86f_has_extra_bit_cells(drv)) {
                if (fread(&(dev->extra_bit_cells[side]), 1, 4, dev->fp) != 4)
                    fatal("d86f_read_track(): Error reading number of extra bit cells\n");
                /*
                   If RPM shift is 0% and direction is 1, do not adjust extra bit cells,
                   as that is the whole track length.
                 */
                if (d86f_get_rpm_mode(drv) || !d86f_get_speed_shift_dir(drv)) {
                    if (dev->extra_bit_cells[side] < -32768)
                        dev->extra_bit_cells[side] = -32768;
                    if (dev->extra_bit_cells[side] > 32768)
                        dev->extra_bit_cells[side] = 32768;
                }
            } else
                dev->extra_bit_cells[side] = 0;
            (void) !fread(&(dev->index_hole_pos[side]), 4, 1, dev->fp);
        } else
            fseek(dev->fp, (off_t) dev->track_offset[logical_track] +
                                         d86f_track_header_size(drv), SEEK_SET);
        const int array_size = d86f_get_array_size(drv, side, 0);
        (void) !fread(da, 1, array_size, dev->fp);
        if (d86f_has_surface_desc(drv))
            (void) !fread(sa, 1, array_size, dev->fp);
    } else {
        if (!thin_track) {
            switch ((dev->disk_flags >> 1) & 3) {
                default:
                case 0:
                    dev->side_flags[side] = 0x0A;
                    break;

                case 1:
                    dev->side_flags[side] = 0x00;
                    break;

                case 2:
                case 3:
                    dev->side_flags[side] = 0x03;
                    break;
            }
            dev->extra_bit_cells[side] = 0;
        }
    }
}

void
d86f_zero_track(void *priv)
{
    fdd_drive_t *drv   = (fdd_drive_t *) priv;
    d86f_t *     dev   = (d86f_t *) drv->d86f_priv;
    const int    sides = d86f_get_sides(drv);

    for (int side = 0; side < sides; side++) {
        if (d86f_has_surface_desc(drv))
            memset(dev->track_surface_data[side], 0, 106096);
        memset(dev->track_encoded_data[side], 0, 106096);
    }
}

void
d86f_seek(void *priv, int track)
{
    fdd_drive_t *drv   = (fdd_drive_t *) priv;
    d86f_t *     dev   = (d86f_t *) drv->d86f_priv;
    const int    sides = d86f_get_sides(drv);;
    int          side;
    int          thin_track;

    /* If the drive has thick tracks, shift the track number by 1. */
    if (!fdd_doublestep_40(drv)) {
        track <<= 1;

        for (thin_track = 0; thin_track < sides; thin_track++) {
            for (side = 0; side < sides; side++) {
                if (d86f_has_surface_desc(drv))
                    memset(dev->thin_track_surface_data[thin_track][side], 0, 106096);
                memset(dev->thin_track_encoded_data[thin_track][side], 0, 106096);
            }
        }
    }

    d86f_zero_track(drv);

    dev->cur_track = track;

    if (!fdd_doublestep_40(drv)) {
        for (side = 0; side < sides; side++) {
            for (thin_track = 0; thin_track < 2; thin_track++)
                d86f_read_track(drv, track, thin_track, side,
                dev->thin_track_encoded_data[thin_track][side],
                dev->thin_track_surface_data[thin_track][side]);

            d86f_construct_encoded_buffer(drv, side);
        }
    } else {
        for (side = 0; side < sides; side++)
            d86f_read_track(drv, track, 0, side, dev->track_encoded_data[side],
                            dev->track_surface_data[side]);
    }

    dev->state = STATE_IDLE;
}

void
d86f_write_track(void *priv, FILE **fp, const int side, uint16_t *da0, uint16_t *sa0)
{
    fdd_drive_t *  drv             = (fdd_drive_t *) priv;
    const uint32_t array_size      = d86f_get_array_size(drv, side, 0);
    const uint16_t side_flags      = drv->d86f_handler.side_flags(drv);
    const uint32_t extra_bit_cells = drv->d86f_handler.extra_bit_cells(drv, side);
    const uint32_t index_hole_pos  = drv->d86f_handler.index_hole_pos(drv, side);

    fwrite(&side_flags, 1, 2, *fp);

    if (d86f_has_extra_bit_cells(drv))
        fwrite(&extra_bit_cells, 1, 4, *fp);

    fwrite(&index_hole_pos, 1, 4, *fp);

    fwrite(da0, 1, array_size, *fp);

    if (d86f_has_surface_desc(drv))
        fwrite(sa0, 1, array_size, *fp);
}

int
d86f_get_track_table_size(void *priv)
{
    fdd_drive_t *drv  = (fdd_drive_t *) priv;
    int          temp = 2048;

    if (d86f_get_sides(drv) == 1)
        temp >>= 1;

    return temp;
}

void
d86f_set_cur_track(void *priv, const int track)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *     dev = (d86f_t *) drv->d86f_priv;

    dev->cur_track = track;
}

void
d86f_write_tracks(void *priv, FILE **fp, uint32_t *track_table)
{
    fdd_drive_t *  drv           = (fdd_drive_t *) priv;
    d86f_t *       dev           = (d86f_t *) drv->d86f_priv;
    const int      sides         = d86f_get_sides(drv);
    const int      fdd_side      = fdd_get_head(drv);
    uint32_t *     tbl           = dev->track_offset;
    int            logical_track = 0;
    int            side;

    if (track_table != NULL)
        tbl = track_table;

    if (!fdd_doublestep_40(drv)) {
        d86f_decompose_encoded_buffer(drv, 0);
        if (sides == 2)
            d86f_decompose_encoded_buffer(drv, 1);

        for (uint8_t thin_track = 0; thin_track < 2; thin_track++) {
            for (side = 0; side < sides; side++) {
                fdd_set_head(drv, side);

                if (sides == 2)
                    logical_track = ((dev->cur_track + thin_track) << 1) + side;
                else
                    logical_track = dev->cur_track + thin_track;

                if (track_table && !tbl[logical_track]) {
                    fseek(*fp, 0, SEEK_END);
                    tbl[logical_track] = ftell(*fp);
                }

                if (tbl[logical_track]) {
                    fseek(*fp, (off_t) tbl[logical_track], SEEK_SET);
                    d86f_write_track(drv, fp,
                                     side, dev->thin_track_encoded_data[thin_track][side],
                                     dev->thin_track_surface_data[thin_track][side]);
                }
            }
        }
    } else {
        for (side = 0; side < sides; side++) {
            fdd_set_head(drv, side);
            if (sides == 2)
                logical_track = (dev->cur_track << 1) + side;
            else
                logical_track = dev->cur_track;

            if (track_table && !tbl[logical_track]) {
                fseek(*fp, 0, SEEK_END);
                tbl[logical_track] = ftell(*fp);
            }

            if (tbl[logical_track]) {
                if (fseek(*fp, (off_t) tbl[logical_track], SEEK_SET) == -1)
                    fatal("d86f_write_tracks(): Error seeking to offset tbl[logical_track]\n");
                d86f_write_track(drv, fp,
                                 side, drv->d86f_handler.encoded_data(drv, side),
                                 dev->track_surface_data[side]);
            }
        }
    }

    fdd_set_head(drv, fdd_side);
}

void
d86f_writeback(void *priv)
{
    fdd_drive_t *drv         = (fdd_drive_t *) priv;
    d86f_t *     dev         = (d86f_t *) drv->d86f_priv;
    int          header_size = (int) d86f_header_size(drv);
    uint8_t      header[32];
    int          size;

    if (!dev->fp)
        return;

    /* First write the track offsets table. */
    if (fseek(dev->fp, 0, SEEK_SET) == -1)
        fatal("86F write_back(): Error seeking to the beginning of the file\n");
    if (fread(header, 1, header_size, dev->fp) != header_size)
        fatal("86F write_back(): Error reading header size\n");

    if (fseek(dev->fp, 8, SEEK_SET) == -1)
        fatal("86F write_back(): Error seeking\n");
    size = d86f_get_track_table_size(drv);
    if (fwrite(dev->track_offset, 1, size, dev->fp) != size)
        fatal("86F write_back(): Error writing data\n");

    d86f_write_tracks(drv, &dev->fp, NULL);

    fflush(dev->fp);
}

void
d86f_stop(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *     dev = (d86f_t *) drv->d86f_priv;

    if (dev)
        dev->state = STATE_IDLE;
}

int
d86f_common_command(void *priv, const int sector, const int track, const int side,
                    UNUSED(int rate), const int sector_size)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *     dev = (d86f_t *) drv->d86f_priv;

    d86f_log("d86f_common_command (drive %i): fdc_period=%i img_period=%i rate=%i sector=%i "
             "track=%i side=%i\n", drive,
             fdc_get_bitcell_period(drv->fdc), d86f_get_bitcell_period(drive), rate, sector,
             track, side);

    dev->req_sector.id.c = track;
    dev->req_sector.id.h = side;
    if (sector == SECTOR_FIRST)
        dev->req_sector.id.r = 1;
    else if (sector == SECTOR_NEXT)
        dev->req_sector.id.r++;
    else
        dev->req_sector.id.r = sector;
    dev->req_sector.id.n = sector_size;

    if (fdd_get_head(drv) && (d86f_get_sides(drv) == 1)) {
        fdc_noidam(drv->fdc);
        dev->state       = STATE_IDLE;
        dev->index_count = 0;
        return 0;
    }

    dev->id_find.sync_marks = dev->id_find.bits_obtained = dev->id_find.bytes_obtained = 0;
    dev->data_find.sync_marks = dev->data_find.bits_obtained = dev->data_find.bytes_obtained = 0;
    dev->index_count = dev->error_condition = dev->satisfying_bytes = 0;
    dev->id_found                                                   = 0;
    dev->dma_over                                                   = 0;

    return 1;
}

void
d86f_readsector(void *priv, const int sector, const int track, const int side,
                const int rate, const int sector_size)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *     dev = (d86f_t *) drv->d86f_priv;
    int     ret = 0;

    ret = d86f_common_command(drv, sector, track, side, rate, sector_size);
    if (!ret)
        return;

    if (d86f_wrong_densel(drv)) {
        dev->state = STATE_SECTOR_NOT_FOUND;

        if (fdd_get_turbo(drv))
            dev->track_pos = 0;
    } else if (sector == SECTOR_FIRST)
        dev->state = STATE_02_SPIN_TO_INDEX;
    else if (sector == SECTOR_NEXT)
        dev->state = STATE_02_FIND_ID;
    else
        dev->state = fdc_is_deleted(drv->fdc) ?
                         STATE_0C_FIND_ID : (fdc_is_verify(drv->fdc) ?
                             STATE_16_FIND_ID : STATE_06_FIND_ID);
}

void
d86f_writesector(void *priv, const int sector, const int track, const int side,
                 const int rate, const int sector_size)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *     dev = (d86f_t *) drv->d86f_priv;
    int          ret = 0;

    if (drv->writeprot) {
        fdc_writeprotect(drv->fdc);
        dev->state       = STATE_IDLE;
        dev->index_count = 0;
        return;
    }

    ret = d86f_common_command(drv, sector, track, side, rate, sector_size);
    if (!ret)
        return;

    if (d86f_wrong_densel(drv)) {
        dev->state = STATE_SECTOR_NOT_FOUND;

        if (fdd_get_turbo(drv))
            dev->track_pos = 0;
    } else
        dev->state = fdc_is_deleted(drv->fdc) ? STATE_09_FIND_ID : STATE_05_FIND_ID;
}

void
d86f_comparesector(void *priv, const int sector, const int track, const int side,
                   const int rate, const int sector_size)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *     dev = (d86f_t *) drv->d86f_priv;
    int          ret = 0;

    ret = d86f_common_command(drv, sector, track, side, rate, sector_size);
    if (!ret)
        return;

    if (d86f_wrong_densel(drv)) {
        dev->state = STATE_SECTOR_NOT_FOUND;

        if (fdd_get_turbo(drv))
            dev->track_pos = 0;
    } else
        dev->state = STATE_11_FIND_ID;
}

void
d86f_readaddress(void *priv, UNUSED(int side), UNUSED(int rate))
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *     dev = (d86f_t *) drv->d86f_priv;

    if (fdd_get_head(drv) && (d86f_get_sides(drv) == 1)) {
        fdc_noidam(drv->fdc);
        dev->state       = STATE_IDLE;
        dev->index_count = 0;
        return;
    }

    dev->id_find.sync_marks = dev->id_find.bits_obtained  =
                              dev->id_find.bytes_obtained = 0;
    dev->data_find.sync_marks = dev->data_find.bits_obtained  =
                                dev->data_find.bytes_obtained = 0;
    dev->index_count = dev->error_condition = dev->satisfying_bytes = 0;
    dev->id_found                                                   = 0;
    dev->dma_over                                                   = 0;

    if (d86f_wrong_densel(drv)) {
        dev->state = STATE_SECTOR_NOT_FOUND;

        if (fdd_get_turbo(drv))
            dev->track_pos = 0;
    } else
        dev->state = STATE_0A_FIND_ID;
}

void
d86f_add_track(void *priv, const int track, const int side)
{
    fdd_drive_t *drv           = (fdd_drive_t *) priv;
    d86f_t *     dev           = (d86f_t *) drv->d86f_priv;
    uint32_t     array_size    = d86f_get_array_size(drv, side, 0);
    int          logical_track;

    if (d86f_get_sides(drv) == 2) {
        logical_track = (track << 1) + side;
    } else {
        if (side)
            return;
        logical_track = track;
    }

    if (!dev->track_offset[logical_track]) {
        /* Track is absent from the file, let's add it. */
        dev->track_offset[logical_track] = dev->file_size;

        dev->file_size += (array_size + 6);
        if (d86f_has_extra_bit_cells(drv))
            dev->file_size += 4;
        if (d86f_has_surface_desc(drv))
            dev->file_size += array_size;
    }
}

void
d86f_common_format(void *priv, const int side, UNUSED(int rate), const uint8_t fill,
                   const int proxy)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *     dev = (d86f_t *) drv->d86f_priv;

    if (drv->writeprot) {
        fdc_writeprotect(drv->fdc);
        dev->state       = STATE_IDLE;
        dev->index_count = 0;
        return;
    }

    if (!d86f_can_format(drv)) {
        fdc_cannotformat(drv->fdc);
        dev->state       = STATE_IDLE;
        dev->index_count = 0;
        return;
    }

    if (!side || (d86f_get_sides(drv) == 2)) {
        if (!proxy) {
            d86f_reset_index_hole_pos(drv, side);

            if (dev->cur_track > 256) {
                fdc_writeprotect(drv->fdc);
                dev->state       = STATE_IDLE;
                dev->index_count = 0;
                return;
            }

            const uint32_t array_size = d86f_get_array_size(drv, side, 0);

            if (d86f_has_surface_desc(drv)) {
                /* Preserve the physical holes but get rid of the fuzzy bytes. */
                for (uint32_t i = 0; i < array_size; i++) {
                    uint16_t       temp  = dev->track_encoded_data[side][i] ^ 0xffff;
                    const uint16_t temp2 = dev->track_surface_data[side][i];
                    temp &= temp2;
                    dev->track_surface_data[side][i] = temp;
                }
            }

            /* Zero the data buffer. */
            memset(dev->track_encoded_data[side], 0, array_size);

            d86f_add_track(drv, dev->cur_track, side);
            if (!fdd_doublestep_40(drv))
                d86f_add_track(drv, dev->cur_track + 1, side);
        }
    }

    dev->fill = fill;

    if (!proxy) {
        dev->side_flags[side] = 0;
        dev->side_flags[side] |= (fdd_getrpm(&(drives[real_drive(drv->fdc, drv->id)])) == 360) ? 0x20 : 0;
        dev->side_flags[side] |= fdc_get_bit_rate(drv->fdc);
        dev->side_flags[side] |= fdc_is_mfm(drv->fdc) ? 8 : 0;

        dev->index_hole_pos[side] = 0;
    }

    dev->id_find.sync_marks = dev->id_find.bits_obtained = dev->id_find.bytes_obtained = 0;
    dev->data_find.sync_marks = dev->data_find.bits_obtained = dev->data_find.bytes_obtained = 0;
    dev->index_count = dev->error_condition = dev->satisfying_bytes = dev->sector_count = 0;
    dev->dma_over        = 0;
    dev->format_id_count = 0;

    if (d86f_wrong_densel(drv) && !proxy) {
        dev->state = STATE_SECTOR_NOT_FOUND;

        if (fdd_get_turbo(drv))
            dev->track_pos = 0;
    } else
        dev->state = STATE_0D_SPIN_TO_INDEX;
}

void
d86f_proxy_format(void *priv, const int side, const int rate, const uint8_t fill)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    d86f_common_format(drv, side, rate, fill, 1);
}

void
d86f_format(void *priv, const int side, const int rate, const uint8_t fill)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    d86f_common_format(drv, side, rate, fill, 0);
}

void
d86f_common_handlers(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;

    drv->readsector    = d86f_readsector;
    drv->writesector   = d86f_writesector;
    drv->comparesector = d86f_comparesector;
    drv->readaddress   = d86f_readaddress;
    drv->byteperiod    = d86f_byteperiod;
    drv->poll          = d86f_poll;
    drv->format        = d86f_proxy_format;
    drv->stop          = d86f_stop;
    drv->hole          = d86f_hole;
}

int
d86f_export(void *priv, char *fn)
{
    fdd_drive_t *  drv        = (fdd_drive_t *) priv;
    d86f_t *       dev        = (d86f_t *) drv->d86f_priv;
    const int      tracks     = 86;
    const uint32_t magic      = 0x46423638;
    const uint16_t version    = 0x020C;
    const uint16_t disk_flags = drv->d86f_handler.disk_flags(drv);
    int            inc        = 1;
    uint32_t       tt[512];
    FILE *         fp;

    memset(tt, 0, 512 * sizeof(uint32_t));

    fp = plat_fopen(fn, "wb");
    if (!fp)
        return 0;

    /* Allocate a temporary drive for conversion. */
    d86f_t *temp86 = (d86f_t *) calloc(1, sizeof(d86f_t));
    memcpy(temp86, dev, sizeof(d86f_t));

    fwrite(&magic, 4, 1, fp);
    fwrite(&version, 2, 1, fp);
    fwrite(&disk_flags, 2, 1, fp);

    fwrite(tt, 1, ((d86f_get_sides(drv) == 2) ? 2048 : 1024), fp);

    /* In the case of a thick track drive, always increment track
       by two, since two tracks are going to get output at once. */
    if (!fdd_doublestep_40(drv))
        inc = 2;

    for (int i = 0; i < tracks; i += inc) {
        if (inc == 2)
            fdd_do_seek(drv, i >> 1);
        else
            fdd_do_seek(drv, i);
        dev->cur_track = i;
        d86f_write_tracks(drv, &fp, tt);
    }

    fclose(fp);

    fp = plat_fopen(fn, "rb+");

    fseek(fp, 8, SEEK_SET);
    fwrite(tt, 1, ((d86f_get_sides(drv) == 2) ? 2048 : 1024), fp);

    fclose(fp);

    fdd_do_seek(drv, fdd_current_track(drv));

    /* Restore the drive from temp. */
    memcpy(dev, temp86, sizeof(d86f_t));
    free(temp86);

    return 1;
}

void
d86f_load(void *priv, char *fn)
{
    fdd_drive_t *drv   = (fdd_drive_t *) priv;
    d86f_t *     dev   = (d86f_t *) drv->d86f_priv;
    uint32_t     magic = 0;
    uint32_t     len   = 0;

    d86f_unregister(drv);

    drv->writeprot = 0;

    dev->fp = plat_fopen(fn, "rb+");
    if (!dev->fp) {
        dev->fp = plat_fopen(fn, "rb");
        if (!dev->fp) {
            memset(drv->image_path, 0, sizeof(drv->image_path));
            free(dev);
            return;
        }
        drv->writeprot = 1;
    }

    if (drv->read_only)
        drv->writeprot = 1;

    drv->fwriteprot = drv->writeprot;

    fseek(dev->fp, 0, SEEK_END);
    len = ftell(dev->fp);
    fseek(dev->fp, 0, SEEK_SET);

    (void) !fread(&magic, 4, 1, dev->fp);

    if (len < 16) {
        /* File is WAY too small, abort. */
        fclose(dev->fp);
        dev->fp = NULL;
        memset(drv->image_path, 0, sizeof(drv->image_path));
        free(dev);
        return;
    }

    if ((magic != 0x46423638) && (magic != 0x66623638)) {
        /* File is not of the valid format, abort. */
        d86f_log("86F: Unrecognized magic bytes: %08X\n", magic);
        fclose(dev->fp);
        memset(drv->image_path, 0, sizeof(drv->image_path));
        free(dev);
        return;
    }

    if (fread(&(dev->version), 1, 2, dev->fp) != 2)
        fatal("d86f_load(): Error reading format version\n");

    if (dev->version != D86FVER) {
        /* File is not of a recognized format version, abort. */
#ifdef ENABLE_D86F_LOG
        if (dev->version == 0x0063) {
            d86f_log("86F: File has emulator-internal version 0.99, "
                     "this version is not valid in a file\n");
        } else if ((dev->version >= 0x0100) && (dev->version < D86FVER)) {
            d86f_log("86F: No longer supported development file version: %i.%02i\n",
                     dev->version >> 8, dev->version & 0xff);
        } else {
            d86f_log("86F: Unrecognized file version: %i.%02i\n",
                     dev->version >> 8, dev->version & 0xff);
        }
#endif
        fclose(dev->fp);
        dev->fp = NULL;
        memset(drv->image_path, 0, sizeof(drv->image_path));
        free(dev);
        return;
    } else {
        d86f_log("86F: Recognized file version: %i.%02i\n",
                 dev->version >> 8, dev->version & 0xff);
    }

    (void) !fread(&(dev->disk_flags), 2, 1, dev->fp);

    if (d86f_has_surface_desc(drv)) {
        for (uint8_t i = 0; i < 2; i++)
            dev->track_surface_data[i] = (uint16_t *) calloc(53048,
                                                             sizeof(uint16_t));

        for (uint8_t i = 0; i < 2; i++) {
            for (uint8_t j = 0; j < 2; j++)
                dev->thin_track_surface_data[i][j] = (uint16_t *) calloc(53048,
                                                                         sizeof(uint16_t));
        }
    }

    if (len < 51052) {
        /* File too small, abort. */
        fclose(dev->fp);
        dev->fp = NULL;
        memset(drv->image_path, 0, sizeof(drv->image_path));
        free(dev);
        return;
    }

    if (dev->disk_flags & 0x100) {
        /* Zoned disk. */
        d86f_log("86F: Disk is zoned (Apple or Sony)\n");
        fclose(dev->fp);
        dev->fp = NULL;
        memset(drv->image_path, 0, sizeof(drv->image_path));
        free(dev);
        return;
    }

    if (dev->disk_flags & 0x600) {
        /* Zone type is not 0 but the disk is fixed-RPM. */
        d86f_log("86F: Disk is fixed-RPM but zone type is not 0\n");
        fclose(dev->fp);
        dev->fp = NULL;
        memset(drv->image_path, 0, sizeof(drv->image_path));
        free(dev);
        return;
    }

    if (!drv->writeprot) {
        drv->writeprot  = (dev->disk_flags & 0x10) ? 1 : 0;
        drv->fwriteprot = drv->writeprot;
    }

    if (drv->writeprot) {
        fclose(dev->fp);
        dev->fp = NULL;

        dev->fp = plat_fopen(fn, "rb");
    }

    /* OK, set the drive data, other code needs it. */
    drv->d86f_priv = dev;

    fseek(dev->fp, 8, SEEK_SET);

    (void) !fread(dev->track_offset, 1, d86f_get_track_table_size(drv), dev->fp);

    if (!(dev->track_offset[0])) {
        /* File has no track 0 side 0, abort. */
        d86f_log("86F: No Track 0 side 0\n");
        fclose(dev->fp);
        dev->fp = NULL;
        memset(drv->image_path, 0, sizeof(drv->image_path));
        free(dev);
        drv->d86f_priv = NULL;
        return;
    }

    if ((d86f_get_sides(drv) == 2) && !(dev->track_offset[1])) {
        /* File is 2-sided but has no track 0 side 1, abort. */
        d86f_log("86F: No Track 0 side 1\n");
        fclose(dev->fp);
        dev->fp = NULL;
        memset(drv->image_path, 0, sizeof(drv->image_path));
        free(dev);
        drv->d86f_priv = NULL;
        return;
    }

    /* Load track 0 flags as default. */
    if (fseek(dev->fp, (off_t) dev->track_offset[0], SEEK_SET) == -1)
        fatal("d86f_load(): Track 0: Error seeking to the beginning of the file\n");
    if (fread(&(dev->side_flags[0]), 1, 2, dev->fp) != 2)
        fatal("d86f_load(): Track 0: Error reading side flags\n");
    if (dev->disk_flags & 0x80) {
        if (fread(&(dev->extra_bit_cells[0]), 1, 4, dev->fp) != 4)
            fatal("d86f_load(): Track 0: Error reading the amount of extra bit cells\n");
        if ((dev->disk_flags & 0x1060) != 0x1000) {
            if (dev->extra_bit_cells[0] < -32768)
                dev->extra_bit_cells[0] = -32768;
            if (dev->extra_bit_cells[0] > 32768)
                dev->extra_bit_cells[0] = 32768;
        }
    } else {
        dev->extra_bit_cells[0] = 0;
    }

    if (d86f_get_sides(drv) == 2) {
        if (fseek(dev->fp, (off_t) dev->track_offset[1], SEEK_SET) == -1)
            fatal("d86f_load(): Track 1: Error seeking to the beginning of the file\n");
        if (fread(&(dev->side_flags[1]), 1, 2, dev->fp) != 2)
            fatal("d86f_load(): Track 1: Error reading side flags\n");
        if (dev->disk_flags & 0x80) {
            if (fread(&(dev->extra_bit_cells[1]), 1, 4, dev->fp) != 4)
                fatal("d86f_load(): Track 4: Error reading the amount of extra bit cells\n");
            if ((dev->disk_flags & 0x1060) != 0x1000) {
                if (dev->extra_bit_cells[1] < -32768)
                    dev->extra_bit_cells[1] = -32768;
                if (dev->extra_bit_cells[1] > 32768)
                    dev->extra_bit_cells[1] = 32768;
            }
        } else {
            dev->extra_bit_cells[1] = 0;
        }
    } else {
        switch ((dev->disk_flags >> 1) >> 3) {
            default:
            case 0:
                dev->side_flags[1] = 0x0a;
                break;

            case 1:
                dev->side_flags[1] = 0x00;
                break;

            case 2:
            case 3:
                dev->side_flags[1] = 0x03;
                break;
        }

        dev->extra_bit_cells[1] = 0;
    }

    fseek(dev->fp, 0, SEEK_END);
    dev->file_size = ftell(dev->fp);

    fseek(dev->fp, 0, SEEK_SET);

    d86f_register_86f(drv);

    drv->seek = d86f_seek;
    d86f_common_handlers(drv);
    drv->format = d86f_format;

    d86f_log("86F: Disk does%s have surface description data\n",
             d86f_has_surface_desc(drive) ? "" : " not");
}

void
d86f_init(void)
{
    for (uint8_t i = 0; i < FDD_NUM; i++) {
        fdd_drive_t *drv = &drives[i];
        drv->d86f_priv = NULL;
    }
}

void
d86f_close(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *     dev = (d86f_t *) drv->d86f_priv;

    char         temp_file_name[2048];

    /* Make sure the drive is alive. */
    if (dev == NULL)
        return;

    memcpy(temp_file_name, drv->id ? nvr_path("TEMP$$$1.$$$") :
                                     nvr_path("TEMP$$$0.$$$"), 26);

    if (d86f_has_surface_desc(drv)) {
        for (uint8_t i = 0; i < 2; i++) {
            if (dev->track_surface_data[i]) {
                free(dev->track_surface_data[i]);
                dev->track_surface_data[i] = NULL;
            }
        }

        for (uint8_t i = 0; i < 2; i++) {
            for (uint8_t j = 0; j < 2; j++) {
                if (dev->thin_track_surface_data[i][j]) {
                    free(dev->thin_track_surface_data[i][j]);
                    dev->thin_track_surface_data[i][j] = NULL;
                }
            }
        }
    }

    if (dev->fp) {
        fclose(dev->fp);
        dev->fp = NULL;
    }
}

/* When an FDD is mounted, set up the D86F data structures. */
void
d86f_setup(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    /* Allocate a drive structure. */
    d86f_t *     dev = (d86f_t *) calloc(1, sizeof(d86f_t));

    dev->state = STATE_IDLE;

    dev->last_side_sector[0] = NULL;
    dev->last_side_sector[1] = NULL;

    crc16_setup(dev->crc_table, 0x1021);

    /* Set the drive as active. */
    drv->d86f_priv = dev;
}

/* If an FDD is unmounted, unlink the D86F data structures. */
void
d86f_destroy(void *priv)
{
    fdd_drive_t *drv = (fdd_drive_t *) priv;
    d86f_t *     dev = (d86f_t *) drv->d86f_priv;

    if (dev == NULL)
        return;

    if (d86f_has_surface_desc(drv)) {
        for (uint8_t i = 0; i < 2; i++) {
            if (dev->track_surface_data[i]) {
                free(dev->track_surface_data[i]);
                dev->track_surface_data[i] = NULL;
            }
        }

        for (uint8_t i = 0; i < 2; i++) {
            for (uint8_t j = 0; j < 2; j++) {
                if (dev->thin_track_surface_data[i][j]) {
                    free(dev->thin_track_surface_data[i][j]);
                    dev->thin_track_surface_data[i][j] = NULL;
                }
            }
        }
    }

    d86f_destroy_linked_lists(drv, 0);
    d86f_destroy_linked_lists(drv, 1);

    free(drv->d86f_priv);
    drv->d86f_priv = NULL;

    drv->d86f_handler.read_data = NULL;
}
