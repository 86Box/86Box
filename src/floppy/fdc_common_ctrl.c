/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Adapter between the drive and bitstream layers and the
 *          uPD765-compatible floppy disk controller. The drive layer
 *          reaches its controller through fdd_ctrl_t (see fdd.h); this
 *          file is the uPD765's implementation of that table.
 *
 * Authors: WNT50
 *
 *          Copyright 2026 WNT50.
 */
#include <stdint.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/fdd.h>
#include <86box/fdc.h>

/* ------------------------------------------------------------------------- */
/* Mechanical side                                                           */
/* ------------------------------------------------------------------------- */

static int
fdc_ctrl_real_drive(void *priv, int drive)
{
    return real_drive((fdc_t *) priv, drive);
}

static uint8_t
fdc_ctrl_get_swwp(void *priv)
{
    return fdc_get_swwp((fdc_t *) priv);
}

static void
fdc_ctrl_seek_complete_interrupt(void *priv, int drive)
{
    fdc_seek_complete_interrupt((fdc_t *) priv, drive);
}

static void
fdc_ctrl_diskchange_interrupt(void *priv, int drive)
{
    fdc_diskchange_interrupt((fdc_t *) priv, drive);
}

/* ------------------------------------------------------------------------- */
/* Flux side                                                                 */
/* ------------------------------------------------------------------------- */

static int
fdc_ctrl_is_mfm(void *priv)
{
    return fdc_is_mfm((fdc_t *) priv);
}

static int
fdc_ctrl_get_bit_rate(void *priv)
{
    return fdc_get_bit_rate((fdc_t *) priv);
}

static int
fdc_ctrl_get_bitcell_period(void *priv)
{
    return fdc_get_bitcell_period((fdc_t *) priv);
}

static uint8_t
fdc_ctrl_get_diswr(void *priv)
{
    return fdc_get_diswr((fdc_t *) priv);
}

static int
fdc_ctrl_get_gap(void *priv)
{
    return fdc_get_gap((fdc_t *) priv);
}

static int
fdc_ctrl_get_gap2(void *priv, int drive)
{
    return fdc_get_gap2((fdc_t *) priv, drive);
}

static int
fdc_ctrl_data(void *priv, uint8_t data, int last)
{
    return fdc_data((fdc_t *) priv, data, last);
}

static int
fdc_ctrl_getdata(void *priv, int last)
{
    return fdc_getdata((fdc_t *) priv, last);
}

static int
fdc_ctrl_data_available(const void *priv)
{
    return fdc_data_available((const fdc_t *) priv);
}

static void
fdc_ctrl_sector_finishread(void *priv)
{
    fdc_sector_finishread((fdc_t *) priv);
}

static void
fdc_ctrl_sector_finishcompare(void *priv, int satisfying)
{
    fdc_sector_finishcompare((fdc_t *) priv, satisfying);
}

static void
fdc_ctrl_track_finishread(void *priv, int condition)
{
    fdc_track_finishread((fdc_t *) priv, condition);
}

static void
fdc_ctrl_noidam(void *priv)
{
    fdc_noidam((fdc_t *) priv);
}

static void
fdc_ctrl_nosector(void *priv)
{
    fdc_nosector((fdc_t *) priv);
}

static void
fdc_ctrl_nodataam(void *priv)
{
    fdc_nodataam((fdc_t *) priv);
}

static void
fdc_ctrl_cannotformat(void *priv)
{
    fdc_cannotformat((fdc_t *) priv);
}

static void
fdc_ctrl_datacrcerror(void *priv)
{
    fdc_datacrcerror((fdc_t *) priv);
}

static void
fdc_ctrl_headercrcerror(void *priv)
{
    fdc_headercrcerror((fdc_t *) priv);
}

static void
fdc_ctrl_writeprotect(void *priv)
{
    fdc_writeprotect((fdc_t *) priv);
}

static void
fdc_ctrl_wrongcylinder(void *priv)
{
    fdc_wrongcylinder((fdc_t *) priv);
}

static void
fdc_ctrl_badcylinder(void *priv)
{
    fdc_badcylinder((fdc_t *) priv);
}

static void
fdc_ctrl_set_wrong_am(void *priv)
{
    fdc_set_wrong_am((fdc_t *) priv);
}

static void
fdc_ctrl_sectorid(void *priv, uint8_t track, uint8_t side, uint8_t sector,
                  uint8_t size, uint8_t crc1, uint8_t crc2)
{
    fdc_sectorid((fdc_t *) priv, track, side, sector, size, crc1, crc2);
}

/* sector_id_t and fdd_sector_id_t have the same layout; the copy keeps
   the two headers independent of each other. */
static fdd_sector_id_t
fdc_ctrl_get_read_track_sector(void *priv)
{
    fdd_sector_id_t ret;
    sector_id_t     id = fdc_get_read_track_sector((fdc_t *) priv);

    memcpy(&ret, &id, sizeof(ret));

    return ret;
}

static void
fdc_ctrl_request_next_sector_id(void *priv)
{
    fdc_request_next_sector_id((fdc_t *) priv);
}

static void
fdc_ctrl_stop_id_request(void *priv)
{
    fdc_stop_id_request((fdc_t *) priv);
}

/* ------------------------------------------------------------------------- */
/* Command context                                                           */
/* ------------------------------------------------------------------------- */

static int
fdc_ctrl_get_dtl(void *priv)
{
    return fdc_get_dtl((fdc_t *) priv);
}

static int
fdc_ctrl_get_compare_condition(void *priv)
{
    return fdc_get_compare_condition((fdc_t *) priv);
}

static int
fdc_ctrl_get_format_n(void *priv)
{
    return fdc_get_format_n((fdc_t *) priv);
}

static int
fdc_ctrl_get_format_sectors(void *priv)
{
    return fdc_get_format_sectors((fdc_t *) priv);
}

static int
fdc_ctrl_is_dma(void *priv)
{
    return fdc_is_dma((fdc_t *) priv);
}

static int
fdc_ctrl_is_verify(void *priv)
{
    return fdc_is_verify((fdc_t *) priv);
}

static int
fdc_ctrl_is_sk(void *priv)
{
    return fdc_is_sk((fdc_t *) priv);
}

static int
fdc_ctrl_is_deleted(void *priv)
{
    return fdc_is_deleted((fdc_t *) priv);
}

/* ------------------------------------------------------------------------- */
/* The table                                                                 */
/* ------------------------------------------------------------------------- */

const fdd_ctrl_t fdc_ctrl = {
    /* Mechanical side. */
    .real_drive              = fdc_ctrl_real_drive,
    .get_swwp                = fdc_ctrl_get_swwp,
    .seek_complete_interrupt = fdc_ctrl_seek_complete_interrupt,
    .diskchange_interrupt    = fdc_ctrl_diskchange_interrupt,

    /* Flux side. */
    .is_mfm                  = fdc_ctrl_is_mfm,
    .get_bit_rate            = fdc_ctrl_get_bit_rate,
    .get_bitcell_period      = fdc_ctrl_get_bitcell_period,
    .get_diswr               = fdc_ctrl_get_diswr,
    .get_gap                 = fdc_ctrl_get_gap,
    .get_gap2                = fdc_ctrl_get_gap2,

    .data                    = fdc_ctrl_data,
    .getdata                 = fdc_ctrl_getdata,
    .data_available          = fdc_ctrl_data_available,

    .sector_finishread       = fdc_ctrl_sector_finishread,
    .sector_finishcompare    = fdc_ctrl_sector_finishcompare,
    .track_finishread        = fdc_ctrl_track_finishread,
    .noidam                  = fdc_ctrl_noidam,
    .nosector                = fdc_ctrl_nosector,
    .nodataam                = fdc_ctrl_nodataam,
    .cannotformat            = fdc_ctrl_cannotformat,
    .datacrcerror            = fdc_ctrl_datacrcerror,
    .headercrcerror          = fdc_ctrl_headercrcerror,
    .writeprotect            = fdc_ctrl_writeprotect,
    .wrongcylinder           = fdc_ctrl_wrongcylinder,
    .badcylinder             = fdc_ctrl_badcylinder,
    .set_wrong_am            = fdc_ctrl_set_wrong_am,
    .sectorid                = fdc_ctrl_sectorid,

    .get_read_track_sector   = fdc_ctrl_get_read_track_sector,
    .request_next_sector_id  = fdc_ctrl_request_next_sector_id,
    .stop_id_request         = fdc_ctrl_stop_id_request,

    /* Command context, temporary. */
    .get_compare_condition   = fdc_ctrl_get_compare_condition,
    .get_format_sectors      = fdc_ctrl_get_format_sectors,
    .get_format_n            = fdc_ctrl_get_format_n,
    .get_dtl                 = fdc_ctrl_get_dtl,
    .is_dma                  = fdc_ctrl_is_dma,
    .is_sk                   = fdc_ctrl_is_sk,
    .is_verify               = fdc_ctrl_is_verify,
    .is_deleted              = fdc_ctrl_is_deleted
};
