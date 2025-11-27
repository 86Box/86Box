/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Panasonic/MKE CD-ROM emulation for the ISA bus.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Kevin Moonlight, <me@yyzkevin.com>
 *          Cacodemon345
 *
 *          Copyright (C) 2025 Miran Grca.
 *          Copyright (C) 2025 Cacodemon345.
 *          Copyright (C) 2024 Kevin Moonlight.
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <stdbool.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/dma.h>
#include <86box/cdrom.h>
#include <86box/cdrom_interface.h>
#include <86box/cdrom_mke.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/sound.h>
#include <86box/fifo8.h>
#include <86box/timer.h>
#ifdef ENABLE_MKE_LOG
#include "cpu.h"
#endif

/*
https://elixir.bootlin.com/linux/2.0.29/source/include/linux/sbpcd.h
CR-562-B is classified as Family1 in this driver, so uses the CMD1_ prefix.
*/
#define CDROM_STATUS_DOOR         0x80
#define CDROM_STATUS_DISC_IN      0x40
#define CDROM_STATUS_SPIN_UP      0x20
#define CDROM_STATUS_ERROR        0x10
#define CDROM_STATUS_DOUBLE_SPEED 0x02
#define CDROM_STATUS_READY        0x01

// Status returned from device
#define STAT_READY       0x01
#define STAT_PLAY        0x08
#define STAT_ERROR       0x10
#define STAT_DISK        0x40
#define STAT_TRAY        0x80 // Seems Correct

#define CMD1_PAUSERESUME 0x0D
#define CMD1_RESET       0x0a
#define CMD1_LOCK_CTL    0x0c
#define CMD1_TRAY_CTL    0x07
#define CMD1_MULTISESS   0x8d
#define CMD1_SUBCHANINF  0x11
#define CMD1_ABORT       0x08
// #define CMD1_PATH_CHECK 0x???
#define CMD1_SEEK     0x01
#define CMD1_READ     0x10
#define CMD1_SPINUP   0x02
#define CMD1_SPINDOWN 0x06
#define CMD1_READ_UPC 0x88
// #define CMD1_PLAY     0x???
#define CMD1_PLAY_MSF 0x0e
#define CMD1_PLAY_TI  0x0f
#define CMD1_STATUS   0x05
#define CMD1_READ_ERR 0x82
#define CMD1_READ_VER 0x83
#define CMD1_SETMODE  0x09
#define CMD1_GETMODE  0x84
#define CMD1_CAPACITY 0x85
#define CMD1_READSUBQ 0x87
#define CMD1_DISKINFO 0x8b
#define CMD1_READTOC  0x8c
#define CMD1_PAU_RES  0x0d
#define CMD1_PACKET   0x8e
#define CMD1_SESSINFO 0x8d

typedef struct mke_t {
    bool       present;
    bool       tray_open;

    uint8_t    command_buffer[7];
    uint8_t    command_buffer_pending;

    uint8_t    medium_changed;

    uint8_t    vol0, vol1, patch0, patch1;
    uint8_t    mode_select[5];

    uint8_t    media_selected; // temporary hack

    Fifo8      data_fifo;
    Fifo8      info_fifo;

    cdrom_t *  cdrom_dev;

    uint32_t   sector_type;
    uint32_t   sector_flags;

    uint32_t   unit_attention;

    uint8_t    cdbuffer[624240 * 2];

    uint32_t   data_to_push;

    pc_timer_t timer;

    char       ver[512];

    uint8_t    is_error;
    uint8_t    sense[8];

    uint8_t    temp_buf[65536];
} mke_t;

typedef struct mke_interface_t {
    mke_t      mke[4];

    uint8_t    is_sb;

    uint8_t    drvsel;
    uint8_t    data_select;
} mke_interface_t;

#ifdef ENABLE_MKE_LOG
int mke_do_log = ENABLE_MKE_LOG;

static void
mke_log(const char *fmt, ...)
{
    va_list ap;

    if (mke_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define mke_log(fmt, ...)
#endif

#define CHECK_READY()                                       \
    {                                                       \
        if (!mke_pre_execution_check(mke))                  \
            return;                                         \
    }

#define REPORT_IF_NOT_READY()                                                   \
    {                                                                           \
        if (!mke_pre_execution_check(mke)) {                                    \
            fifo8_push(&mke->info_fifo, mke_cdrom_status(mke->cdrom_dev, mke)); \
        }                                                                       \
    }

#define CHECK_READY_READ()                                                      \
    {                                                                           \
        if (!mke_pre_execution_check(mke)) {                                    \
            fifo8_push(&mke->info_fifo, mke_cdrom_status(mke->cdrom_dev, mke)); \
            return;                                                             \
        }                                                                       \
    }

static void
mke_update_sense(mke_t *mke, uint8_t error)
{
    /* FreeBSD calls this addrval, but what is it? */
    mke->sense[0] = 0x00;
    mke->sense[1] = mke->command_buffer[0];
    mke->sense[2] = error;

    mke->is_error = 1;
}

static void
mke_cdrom_insert(void *priv)
{
    mke_t *dev = (mke_t *) priv;

    if ((dev == NULL) || (dev->cdrom_dev == NULL))
        return;

    if (dev->cdrom_dev->ops == NULL) {
        dev->medium_changed = 0;
        dev->cdrom_dev->cd_status = CD_STATUS_EMPTY;
        if (timer_is_enabled(&dev->timer)) {
            timer_disable(&dev->timer);
            dev->data_to_push = 0;
        }
        mke_log("Media removal\n");
    } else if (dev->cdrom_dev->cd_status & CD_STATUS_TRANSITION) {
        dev->medium_changed = 1;
        /* Turn off the medium changed status. */
        dev->cdrom_dev->cd_status &= ~CD_STATUS_TRANSITION;
        mke_log("Media insert\n");
    } else {
        dev->medium_changed = 0;
        dev->cdrom_dev->cd_status |= CD_STATUS_TRANSITION;
        mke_log("Media transition\n");
    }
}

static int
mke_pre_execution_check(mke_t *mke)
{
    int ready = 1;

    if ((mke->cdrom_dev->cd_status == CD_STATUS_PLAYING) ||
        (mke->cdrom_dev->cd_status == CD_STATUS_PAUSED)) {
        ready = 1;
        goto skip_ready_check;
    }

    if (mke->cdrom_dev->cd_status & CD_STATUS_TRANSITION) {
        if (mke->command_buffer[0] == 0x82)
            ready = 0;
        else {
            mke_cdrom_insert(mke);

            ready = ((mke->cdrom_dev->cd_status != CD_STATUS_EMPTY) && (mke->cdrom_dev->cd_status != CD_STATUS_DVD_REJECTED));
        }
    } else
        ready = ((mke->cdrom_dev->cd_status != CD_STATUS_EMPTY) && (mke->cdrom_dev->cd_status != CD_STATUS_DVD_REJECTED));

skip_ready_check:
    /*
       If the drive is not ready, there is no reason to keep the
       UNIT ATTENTION condition present, as we only use it to mark
       disc changes.
     */
    if (!ready && (mke->medium_changed > 0))
        mke->medium_changed = 0;

    /*
       If the UNIT ATTENTION condition is set and the command does not allow
       execution under it, error out and report the condition.
     */
    if (mke->medium_changed == 1) {
        /*
           Only increment the unit attention phase if the command can
           not pass through it.
         */
        mke_log("Unit attention now 2\n");
        mke->medium_changed++;
        mke_update_sense(mke, 0x11);                                        \
        return 0;
    } else if (mke->medium_changed == 2) {
        if (mke->command_buffer[0] != 0x82) {
            mke_log("Unit attention now 0\n");
            mke->medium_changed = 0;
        }
    }

    /*
       Unless the command is REQUEST SENSE, clear the sense. This will *NOT* clear
       the UNIT ATTENTION condition if it's set.
     */
    if (mke->command_buffer[0] != 0x82) {
        memset(mke->sense, 0x00, 8);
        mke->is_error = 0;
    }

    if (!ready && (mke->command_buffer[0] != 0x05)) {
        mke_log("Not ready (%02X)\n", mke->command_buffer[0]);
        mke_update_sense(mke, 0x03);
        return 0;
    }

    return 1;
}

uint8_t
mke_cdrom_status(cdrom_t *dev, mke_t *mke)
{
    uint8_t status = 0;
    /*
       This bit seems to always be set?
       Bit 4 never set?
     */
    status |= 2;
    if (dev->cd_status == CD_STATUS_PLAYING)
        status |= STAT_PLAY;
    if (dev->cd_status == CD_STATUS_PAUSED)
        status |= STAT_PLAY;
    if (mke->is_error)
        status |= 0x10;
    /* Always set? */
    status |= 0x20;
    status |= STAT_TRAY;
    if (mke->cdrom_dev->cd_status != CD_STATUS_EMPTY) {
        status |= STAT_DISK;
        status |= STAT_READY;
    }

    return status;
}

void
mke_get_subq(mke_t *mke, uint8_t *b)
{
    cdrom_t *dev = mke->cdrom_dev;

    cdrom_get_current_subchannel_sony(dev, mke->temp_buf, 1);
    /* ? */
    b[0]  = 0x80;
    b[1]  = ((mke->temp_buf[0] & 0xf) << 4) | ((mke->temp_buf[0] & 0xf0) >> 4);
    b[2]  = mke->temp_buf[1];
    b[3]  = mke->temp_buf[2];
    b[4]  = mke->temp_buf[6];
    b[5]  = mke->temp_buf[7];
    b[6]  = mke->temp_buf[8];
    b[7]  = mke->temp_buf[3];
    b[8]  = mke->temp_buf[4];
    b[9]  = mke->temp_buf[5];
    /* ? */
    b[10] = 0;
    mke_log("mke_get_subq: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n",
            b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10]);
}

/* Lifted from FreeBSD */
static void
blk_to_msf(int blk, unsigned char *msf)
{
    blk = blk + 150;        /* 2 seconds skip required to
                               reach ISO data */
    msf[0] = blk / 4500;
    blk = blk % 4500;
    msf[1] = blk / 75;
    msf[2] = blk % 75;

    return;
}

uint8_t
mke_read_toc(mke_t *mke, unsigned char *b, uint8_t track) {
    cdrom_t      *dev            = mke->cdrom_dev;
#if 0
    track_info_t  ti;
    int           last_track;
#endif
    const raw_track_info_t *trti = (raw_track_info_t *) mke->temp_buf;
    int           num            = 0;
    int           ret            = 0;

    dev->ops->get_raw_track_info(dev->local, &num, mke->temp_buf);

    if (num > 0) {
        if (track == 0xaa)
            track = 0xa2;

        int trk = - 1;

        for (int i = (num - 1); i >= 0; i--) {
            if (trti[i].point == track) {
                trk = i;
                break;
            }
        }

        if (trk != -1) {
            b[0] = 0;
            b[1] = trti[trk].adr_ctl;
            b[2] = (trti[trk].point == 0xa2) ? 0xaa : trti[trk].point;
            b[3] = 0;
            b[4] = trti[trk].pm;
            b[5] = trti[trk].ps;
            b[6] = trti[trk].pf;
            b[7] = 0;

            ret  = 1;
        }

        mke_log("mke_read_toc: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n",
                b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
    }

    return ret;
}


uint8_t
mke_disc_info(mke_t *mke, unsigned char *b)
{
    cdrom_t *dev               = mke->cdrom_dev;
    uint8_t  disc_type_buf[34];
    int      first_track;
    int      last_track;

    cdrom_read_toc(dev, mke->temp_buf, CD_TOC_NORMAL, 0, 2 << 8, 65536);
    cdrom_read_disc_information(dev, disc_type_buf);
    first_track = mke->temp_buf[2];
    last_track  = mke->temp_buf[3];

    b[0] = disc_type_buf[8];
    b[1] = first_track;
    b[2] = last_track;
    b[3] = 0;
    b[4] = 0;
    b[5] = 0;
    blk_to_msf(dev->cdrom_capacity, &b[3]);
    mke_log("mke_disc_info: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n",
            b[0], b[1], b[2], b[3], b[4], b[5]);
    return 1;
}

uint8_t
mke_disc_capacity(cdrom_t *dev, unsigned char *b)
{
    b[0] = 0x00;
    b[1] = 0x00;
    b[2] = 0x00;
    b[3] = 0x08;
    b[4] = 0x00;

    blk_to_msf(dev->cdrom_capacity, &b[0]);
    mke_log("mke_disc_capacity: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n",
            b[0], b[1], b[2], b[3], b[4]);
    return 1;
}

void
mke_read_multisess(mke_t *mke)
{
    cdrom_t      *dev            = mke->cdrom_dev;
    uint8_t      *b              = (uint8_t *) &(mke->temp_buf[32768]);
    const raw_track_info_t *trti = (raw_track_info_t *) mke->temp_buf;
    int           num            = 0;
    int           first_sess     = 0;
    int           last_sess      = 0;

    dev->ops->get_raw_track_info(dev->local, &num, mke->temp_buf);

    if (num > 0) {
        int trk = - 1;

        for (int i = 0; i < num; i++) {
            if (trti[i].point == 0xa2) {
                first_sess = trti[i].session;
                break;
            }
        }

        for (int i = (num - 1); i >= 0; i--) {
            if (trti[i].point == 0xa2) {
                last_sess = trti[i].session;
                break;
            }
        }

        for (int i = 0; i < num; i++) {
            if ((trti[i].point >= 1) && (trti[i].point >= 99) &&
                (trti[i].session == last_sess)) {
                trk = i;
                break;
            }
        }

        if ((first_sess > 0) && (last_sess < 0) && (trk != -1)) {
            b[0] = (first_sess == last_sess) ? 0x00 : 0x80;
            b[1] = trti[trk].pm;
            b[2] = trti[trk].ps;
            b[3] = trti[trk].pf;
            b[4] = 0;
            b[5] = 0;
        }

        fifo8_push_all(&mke->info_fifo, b, 6);

        mke_log("mke_read_multisess: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n",
                b[0], b[1], b[2], b[3], b[4], b[5]);
    } else {
        memset(b, 0x00, 6);
        fifo8_push_all(&mke->info_fifo, b, 6);
    }
}

static void
mke_reset(mke_t *mke)
{
    cdrom_stop(mke->cdrom_dev);
    timer_disable(&mke->timer);
    mke->sector_type            = 0x08 | (1 << 4);
    mke->sector_flags           = 0x10;
    memset(mke->mode_select, 0, 5);
    mke->mode_select[2]         = 0x08;
    mke->patch0                 = 0x01;
    mke->patch1                 = 0x02;
    mke->vol0                   = 255;
    mke->vol1                   = 255;
    mke->cdrom_dev->sector_size = 2048;
}

void
mke_command_callback(void *priv)
{
    mke_t *mke = (mke_t *) priv;

    switch (mke->command_buffer[0]) {
        case CMD1_SEEK: {
            fifo8_push(&mke->info_fifo, mke_cdrom_status(mke->cdrom_dev, mke));
            break;
        }
        case CMD1_READ: {
            fifo8_push_all(&mke->data_fifo, mke->cdbuffer, mke->data_to_push);
            fifo8_push(&mke->info_fifo, mke_cdrom_status(mke->cdrom_dev, mke));
            mke->data_to_push = 0;
            ui_sb_update_icon(SB_CDROM | mke->cdrom_dev->id, 0);
            break;
        }
    }
}

void
mke_command(mke_t *mke, uint8_t value)
{
    uint16_t     i;
    /* This is wasteful handling of buffers for compatibility, but will optimize later. */
    uint8_t      x[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    if (mke->command_buffer_pending) {
        mke->command_buffer[6 - mke->command_buffer_pending + 1] = value;
        mke->command_buffer_pending--;
    }

    if (mke->command_buffer[0] == CMD1_ABORT) {
        mke_log("CMD_ABORT\n");
        fifo8_reset(&mke->info_fifo);
        fifo8_reset(&mke->data_fifo);
        timer_disable(&mke->timer);
        mke->command_buffer[0]      = 0;
        mke->command_buffer_pending = 7;
        fifo8_push(&mke->info_fifo, mke_cdrom_status(mke->cdrom_dev, mke));
    }

    if (!mke->command_buffer_pending && mke->command_buffer[0]) {
        mke->command_buffer_pending = 7;
        mke_log("mke_command: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n",
                mke->command_buffer[0], mke->command_buffer[1],
                mke->command_buffer[2], mke->command_buffer[3],
                mke->command_buffer[4], mke->command_buffer[5],
                mke->command_buffer[6]);
        switch (mke->command_buffer[0]) {
            case 0x03:
                fifo8_reset(&mke->info_fifo);
                cdrom_stop(mke->cdrom_dev);
                fifo8_push(&mke->info_fifo, mke_cdrom_status(mke->cdrom_dev, mke));
                break;
            case 0x06:
                fifo8_reset(&mke->info_fifo);
                cdrom_stop(mke->cdrom_dev);
                cdrom_eject(mke->cdrom_dev->id);
                fifo8_push(&mke->info_fifo, mke_cdrom_status(mke->cdrom_dev, mke));
                break;
            case 0x07:
                fifo8_reset(&mke->info_fifo);
                cdrom_reload(mke->cdrom_dev->id);
                fifo8_push(&mke->info_fifo, mke_cdrom_status(mke->cdrom_dev, mke));
                break;
            case CMD1_RESET:
                mke_reset(mke);
                break;
            case CMD1_READ: {
                uint32_t count = mke->command_buffer[6];
                uint8_t *buf   = mke->cdbuffer;
                int      res   = 0;
                uint64_t lba   = MSFtoLBA(mke->command_buffer[1], mke->command_buffer[2],
                                          mke->command_buffer[3]) - 150;
                int      len __attribute__((unused)) = 0;

                CHECK_READY_READ();
                mke->data_to_push = 0;

                while (count) {
                    if ((res = cdrom_readsector_raw(mke->cdrom_dev, buf, lba, 0,
                                                    mke->sector_type, mke->sector_flags, &len, 0)) > 0) {
                        lba++;
                        buf += mke->cdrom_dev->sector_size;
                        mke->data_to_push += mke->cdrom_dev->sector_size;
                    } else {
                        mke_update_sense(mke, (res == 0) ? 0x10 : 0x05);
                        fifo8_push(&mke->info_fifo, mke_cdrom_status(mke->cdrom_dev, mke));
                        break;
                    }
                    count--;
                }
                if (count != 0) {
                    fifo8_reset(&mke->data_fifo);
                    mke->data_to_push = 0;
                } else {
                    ui_sb_update_icon(SB_CDROM | mke->cdrom_dev->id, 1);
                    timer_on_auto(&mke->timer, (1000000.0 / (176400.0 * 2.)) * mke->data_to_push);
                }
                break;
            } case CMD1_READSUBQ:
                if (mke_pre_execution_check(mke)) {
                    mke_get_subq(mke, (uint8_t *) &x);
                }
                fifo8_reset(&mke->info_fifo);
                fifo8_push_all(&mke->info_fifo, x, 11);
                fifo8_push(&mke->info_fifo, mke_cdrom_status(mke->cdrom_dev, mke));
                break;
            case CMD1_SETMODE:
                /* Returns 1 */
                fifo8_reset(&mke->info_fifo);
                mke_log("CMD: SET MODE:");
                for (i = 0; i < 6; i++) {
                    mke_log("%02x ", mke->command_buffer[i + 1]);
                }
                mke_log("\n");
                switch (mke->command_buffer[1]) {
                    case 0:
                        switch (mke->command_buffer[2]) {
                            case 0x00: /* Cooked */
                                mke->sector_type  = 0x08 | (1 << 4);
                                mke->sector_flags = 0x10;
                                mke->cdrom_dev->sector_size = 2048;
                                break;
                            case 0x81: /* XA */
                            case 0x01: /* User */ {
                                uint32_t sector_size = (mke->command_buffer[3] << 8) |
                                                       mke->command_buffer[4];

                                if (!sector_size) {
                                    mke_update_sense(mke, 0x0e);
                                    fifo8_push(&mke->info_fifo, mke_cdrom_status(mke->cdrom_dev, mke));
                                    return;
                                } else {
                                    switch (sector_size) {
                                        case 2048:
                                            mke->sector_type  = 0x08 | (1 << 4);
                                            mke->sector_flags = 0x10;
                                            mke->cdrom_dev->sector_size = 2048;
                                            break;
                                        case 2052:
                                            mke->sector_type  = 0x18;
                                            mke->sector_flags = 0x30;
                                            mke->cdrom_dev->sector_size = 2052;
                                            break;
                                        case 2324:
                                            mke->sector_type  = 0x1b;
                                            mke->sector_flags = 0x18;
                                            mke->cdrom_dev->sector_size = 2324;
                                            break;
                                        case 2336:
                                            mke->sector_type  = 0x1c;
                                            mke->sector_flags = 0x58;
                                            mke->cdrom_dev->sector_size = 2336;
                                            break;
                                        case 2340:
                                            mke->sector_type  = 0x18;
                                            mke->sector_flags = 0x78;
                                            mke->cdrom_dev->sector_size = 2340;
                                            break;
                                        case 2352:
                                            mke->sector_type  = 0x00;
                                            mke->sector_flags = 0xf8;
                                            mke->cdrom_dev->sector_size = 2352;
                                            break;
                                        default:
                                            mke_update_sense(mke, 0x0e);
                                            fifo8_push(&mke->info_fifo, mke_cdrom_status(mke->cdrom_dev, mke));
                                            return;
                                    }
                                }
                                break;
                            } case 0x82: /* DA */
                                mke->sector_type  = 0x00;
                                mke->sector_flags = 0xf8;
                                mke->cdrom_dev->sector_size = 2352;
                                break;
                            default:
                                mke_update_sense(mke, 0x0e);
                                fifo8_push(&mke->info_fifo, mke_cdrom_status(mke->cdrom_dev, mke));
                                return;
                        }

                        memcpy(mke->mode_select, &mke->command_buffer[2], 5);
                        break;
                    case 5:
                        mke->vol0 = mke->command_buffer[4];
                        mke->vol1 = mke->command_buffer[6];
                        mke->patch0 = mke->command_buffer[3];
                        mke->patch1 = mke->command_buffer[5];
                        break;
                }
                fifo8_push(&mke->info_fifo, mke_cdrom_status(mke->cdrom_dev, mke));
                break;
            case CMD1_GETMODE:
                /* 6 */
                mke_log("GET MODE\n");
                if (mke->command_buffer[1] == 5) {
                    uint8_t volsettings[5] = { 0, mke->patch0, mke->vol0, mke->patch1, mke->vol1 };
                    fifo8_push_all(&mke->info_fifo, volsettings, 5);
                } else
                    fifo8_push_all(&mke->info_fifo, mke->mode_select, 5);
                fifo8_push(&mke->info_fifo, mke_cdrom_status(mke->cdrom_dev, mke));
                break;
            case CMD1_PAUSERESUME:
                CHECK_READY_READ();
                cdrom_audio_pause_resume(mke->cdrom_dev, mke->command_buffer[1] >> 7);
                fifo8_push(&mke->info_fifo, mke_cdrom_status(mke->cdrom_dev, mke));
                break;
            case CMD1_CAPACITY:
                /* 6 */
                mke_log("DISK CAPACITY\n");
                if (mke_pre_execution_check(mke))
                    mke_disc_capacity(mke->cdrom_dev, (uint8_t *) &x);
                fifo8_push_all(&mke->info_fifo, x, 5);
                fifo8_push(&mke->info_fifo, mke_cdrom_status(mke->cdrom_dev, mke));
                break;
            case CMD1_DISKINFO:
                /* 7 */
                mke_log("DISK INFO\n");
                fifo8_reset(&mke->info_fifo);
                if (mke_pre_execution_check(mke))
                    mke_disc_info(mke, (uint8_t *) &x);
                fifo8_push_all(&mke->info_fifo, x, 6);
                fifo8_push(&mke->info_fifo, mke_cdrom_status(mke->cdrom_dev, mke));
                break;
            case CMD1_READTOC:
                fifo8_reset(&mke->info_fifo);
                if (mke_pre_execution_check(mke))
                    mke_read_toc(mke, (uint8_t *) &x, mke->command_buffer[2]);
                fifo8_push_all(&mke->info_fifo, x, 8);
                fifo8_push(&mke->info_fifo, mke_cdrom_status(mke->cdrom_dev, mke));
                break;
            case CMD1_PLAY_TI:
                /* Index is ignored for now. */
                fifo8_reset(&mke->info_fifo);
                CHECK_READY_READ();
                if (!cdrom_audio_play(mke->cdrom_dev, mke->command_buffer[1], mke->command_buffer[3], 2))
                    mke_update_sense(mke, 0x10);
                fifo8_push(&mke->info_fifo, mke_cdrom_status(mke->cdrom_dev, mke));
                break;
            case CMD1_PLAY_MSF:
                fifo8_reset(&mke->info_fifo);
                CHECK_READY_READ();
                mke_log("PLAY MSF:");
                for (i = 0; i < 6; i++) {
                    mke_log("%02x ", mke->command_buffer[i + 1]);
                }
                mke_log("\n");
                {
                    int msf = 1;
                    int pos = (mke->command_buffer[1] << 16) | (mke->command_buffer[2] << 8) |
                              mke->command_buffer[3];
                    int len = (mke->command_buffer[4] << 16) | (mke->command_buffer[5] << 8) |
                              mke->command_buffer[6];
                    if (!cdrom_audio_play(mke->cdrom_dev, pos, len, msf)){
                        mke_update_sense(mke, 0x10);
                    }
                    fifo8_push(&mke->info_fifo, mke_cdrom_status(mke->cdrom_dev, mke));
                }
                break;
            case CMD1_SEEK:
                fifo8_reset(&mke->info_fifo);
                CHECK_READY_READ();
                /* TODO: DOES THIS IMPACT CURRENT PLAY LENGTH? */
                mke_log("SEEK MSF:");
                for (i = 0; i < 6; i++) {
                    mke_log("%02x ", mke->command_buffer[i + 1]);
                }

                cdrom_stop(mke->cdrom_dev);
                /* Note for self: Panasonic/MKE drives send seek commands in MSF format. */
                cdrom_seek(mke->cdrom_dev, MSFtoLBA(mke->command_buffer[1], mke->command_buffer[2],
                           mke->command_buffer[3]) - 150, 0);
                fifo8_push(&mke->info_fifo, mke_cdrom_status(mke->cdrom_dev, mke));
                break;
            case CMD1_SESSINFO:
                fifo8_reset(&mke->info_fifo);
                mke_log("CMD: READ SESSION INFO\n");
                if (mke_pre_execution_check(mke))
                    mke_read_multisess(mke);
                else
                    fifo8_push_all(&mke->info_fifo, x, 6);
                fifo8_push(&mke->info_fifo, mke_cdrom_status(mke->cdrom_dev, mke));
                break;
            case CMD1_READ_UPC:
                fifo8_reset(&mke->info_fifo);
                mke_log("CMD: READ UPC\n");
                uint8_t upc[8] = { [0] = 80 };
                fifo8_push_all(&mke->info_fifo, upc, 8);
                REPORT_IF_NOT_READY();
                fifo8_push(&mke->info_fifo, mke_cdrom_status(mke->cdrom_dev, mke));
                break;
            case CMD1_READ_ERR:
                fifo8_reset(&mke->info_fifo);
                mke_log("CMD: READ ERR\n");
                mke_log("ERROR: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                        mke->sense[0], mke->sense[1], mke->sense[2], mke->sense[3],
                        mke->sense[4], mke->sense[5], mke->sense[6], mke->sense[7]);
                {
                    uint8_t temp[8];
                    memset(temp, mke->sense[2], 8);
                    fifo8_push_all(&mke->info_fifo, mke->sense, 8);
                }
                mke->is_error = 0;
                fifo8_push(&mke->info_fifo, mke_cdrom_status(mke->cdrom_dev, mke));
                break;
            case CMD1_READ_VER:
                /* SB2CD Expects 12 bytes, but drive only returns 11. */
                fifo8_reset(&mke->info_fifo);
                fifo8_push_all(&mke->info_fifo, (uint8_t *) mke->ver, 10);
                fifo8_push(&mke->info_fifo, mke_cdrom_status(mke->cdrom_dev, mke));
                break;
            case CMD1_STATUS:
                fifo8_reset(&mke->info_fifo);
                CHECK_READY_READ();
                fifo8_push(&mke->info_fifo, mke_cdrom_status(mke->cdrom_dev, mke));
                break;
            default:
                mke_log("MKE: Unknown Commnad [%02x]\n", mke->command_buffer[0]);
                fifo8_reset(&mke->info_fifo);
                fifo8_push_all(&mke->info_fifo, x, 6);
                fifo8_push(&mke->info_fifo, mke_cdrom_status(mke->cdrom_dev, mke));
                break;
        }
    } else if (!mke->command_buffer_pending) {
        /*
           We are done but not in a command.
           Should we make sure it is a valid command here?
         */
        mke->command_buffer[0]      = value;
        mke->command_buffer_pending = 6;
    }
}

void
mke_write(uint16_t port, uint8_t val, void *priv)
{
    mke_interface_t *mki   = (mke_interface_t *) priv;
    mke_t           *mke   = &(mki->mke[mki->drvsel & 0x03]);
    uint8_t          sb[8] = { 0x00, 0x02, 0x01, 0x03 };

    mke_log("[%04X:%08X] [W] %04X = %02X\n", CS, cpu_state.pc, port, val);

    /* if (mke->present || ((port & 0x0003) == 0x0003)) */  switch (port & 0x0003) {
        case 0:
            if (mke->present)
                mke_command(mke, val);
            break;
        case 1:
            if (mki->is_sb)
                mki->data_select = val;
            break;
        case 2:
            if (mke->present)
                mke_reset(mke);
            break;
        case 3:
            if (mki->is_sb)
                mki->drvsel = (val & 0xfc) | sb[val & 0x03];
            else
                mki->drvsel = val;
            break;
        default:
            break;
    }
}

uint8_t
mke_read(uint16_t port, void *priv)
{
    mke_interface_t *mki = (mke_interface_t *) priv;
    mke_t           *mke = &(mki->mke[mki->drvsel & 0x03]);
    uint8_t          ret = 0x00;

    if (mke->present)  switch (port & 0x0003) {
        case 0:
            /* Info */
            if (mki->is_sb && mki->data_select)
                ret = fifo8_num_used(&mke->data_fifo) ? fifo8_pop(&mke->data_fifo) : 0x00;
            else
                ret = fifo8_num_used(&mke->info_fifo) ? fifo8_pop(&mke->info_fifo) : 0x00;
            break;
        case 1:
            /*
               Status:
                   - 1 = Status Change;
                   - 2 = Data Ready;
                   - 4 = Response Ready;
                   - 8 = Attention / Issue?
            */
            ret = 0xff;
            if (fifo8_num_used(&mke->data_fifo))
                /* Data FIFO */
                ret ^= 2;
            if (fifo8_num_used(&mke->info_fifo))
                /* Status FIFO */
                ret ^= 4;
            if (mke->is_error)
                ret ^= 8;
            break;
        case 2:
            /* Data */
            if (!mki->is_sb)
                ret = fifo8_num_used(&mke->data_fifo) ? fifo8_pop(&mke->data_fifo) : 0x00;
            break;
        default:
            mke_log("MKE Unknown Read Port: %04X\n", port);
            ret = 0xff;
            break;
    } else if ((port & 0x0003) == 0x0003)
        /* This is needed for the Windows 95 built-in driver to function correctly. */
        ret = 0xff;

    mke_log("[%04X:%08X] [R] %04X = %02X\n", CS, cpu_state.pc, port, ret);

    return ret;
}

uint32_t
mke_get_volume(void *priv, int channel)
{
    mke_t *dev = (mke_t *) priv;

    return channel == 0 ? dev->vol0 : dev->vol1;
}

uint32_t
mke_get_channel(void *priv, int channel)
{
    mke_t *dev = (mke_t *) priv;

    return channel == 0 ? dev->patch0 : dev->patch1;
}

void
mke_close(void *priv)
{
    mke_interface_t *mki = (mke_interface_t *) calloc(1, sizeof(mke_interface_t));

    for (uint8_t i = 0; i < 4; i++) {
        mke_t *mke = &(mki->mke[i]);

        if (mke->present) {
            timer_disable(&mke->timer);

            fifo8_destroy(&mke->data_fifo);
            fifo8_destroy(&mke->info_fifo);
        }
    }

    free(mki);
}

void *
mke_init(const device_t *info)
{
    mke_interface_t *mki = (mke_interface_t *) calloc(1, sizeof(mke_interface_t));
    int              num = 0;

    for (uint8_t i = 0; i < CDROM_NUM; i++) {
        if (cdrom[i].bus_type == CDROM_BUS_MKE) {
            cdrom_t *dev = &cdrom[i];

            mke_t *mke = &(mki->mke[dev->mke_channel]);

            mke->present = 1;

            memset(mke->ver, 0x00, 512);
            cdrom_generate_name_mke(dev->type, mke->ver);
            mke->ver[10] = 0x00;

            fifo8_create(&mke->info_fifo, 128);
            fifo8_create(&mke->data_fifo, 624240 * 2);
            fifo8_reset(&mke->info_fifo);
            fifo8_reset(&mke->data_fifo);
            mke->cdrom_dev              = dev;
            mke->command_buffer_pending = 7;
            mke->sector_type            = 0x08 | (1 << 4);
            mke->sector_flags           = 0x10;
            mke->mode_select[2]         = 0x08;
            mke->patch0                 = 0x01;
            mke->patch1                 = 0x02;
            mke->vol0                   = 255;
            mke->vol1                   = 255;
            dev->sector_size            = 2048;

            dev->priv          = mke;
            dev->insert        = mke_cdrom_insert;
            dev->get_volume    = mke_get_volume;
            dev->get_channel   = mke_get_channel;
            dev->cached_sector = -1;

            timer_add(&mke->timer, mke_command_callback, mke, 0);

            num++;

            if (num == 4)
                break;
        }
    }

    mki->is_sb                  = info->local;

    uint16_t base = device_get_config_hex16("base");
    io_sethandler(base, 4, mke_read, NULL, NULL, mke_write, NULL, NULL, mki);

    return mki;
}

static const device_config_t mke_config[] = {
    // clang-format off
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x250,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "220H", .value = 0x220 },
            { .description = "230H", .value = 0x230 },
            { .description = "250H", .value = 0x250 },
            { .description = "260H", .value = 0x260 },
            { .description = "270H", .value = 0x270 },
            { .description = "290H", .value = 0x290 },
            { .description = "300H", .value = 0x300 },
            { .description = "310H", .value = 0x310 },
            { .description = "320H", .value = 0x320 },
            { .description = "330H", .value = 0x330 },
            { .description = "340H", .value = 0x340 },
            { .description = "630H", .value = 0x630 },
            { NULL                                  }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
// clang-format off
};

const device_t mke_cdrom_device = {
    .name          = "Panasonic/MKE CD-ROM interface (Creative)",
    .internal_name = "mkecd",
    .flags         = DEVICE_ISA,
    .local         = 1,
    .init          = mke_init,
    .close         = mke_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = mke_config
};

const device_t mke_cdrom_noncreative_device = {
    .name          = "Panasonic/MKE CD-ROM interface",
    .internal_name = "mkecd_normal",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = mke_init,
    .close         = mke_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = mke_config
};
