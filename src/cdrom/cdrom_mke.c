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
    bool tray_open;

    uint8_t enable_register;

    uint8_t command_buffer[7];
    uint8_t command_buffer_pending;

    uint8_t vol0, vol1, patch0, patch1;
    uint8_t mode_select[5];

    uint8_t data_select;

    uint8_t media_selected; // temporary hack

    Fifo8 data_fifo;
    Fifo8 info_fifo;
    Fifo8 errors_fifo;

    cdrom_t *cdrom_dev;

    uint32_t sector_type;
    uint32_t sector_flags;

    uint32_t unit_attention;

    uint8_t cdbuffer[624240 * 2];

    uint32_t data_to_push;

    pc_timer_t timer;
} mke_t;
mke_t mke;

#define mke_log(x, ...)

#define CHECK_READY()                                      \
    {                                                      \
        if (mke.cdrom_dev->cd_status == CD_STATUS_EMPTY) { \
            fifo8_push(&mke.errors_fifo, 0x03);            \
            return;                                        \
        }                                                  \
    }

static uint8_t temp_buf[65536];

void
mke_get_subq(cdrom_t *dev, uint8_t *b)
{
#if 0
    dev->ops->get_subchannel(dev, dev->seek_pos, &subc);
    cdrom_get_current_subchannel(dev, &subc);
    cdrom_get_current_subcodeq(dev, b);
#endif
    cdrom_get_current_subchannel_sony(dev, temp_buf, 1);
    b[0]  = 0x80; //?
    b[1]  = ((temp_buf[0] & 0xf) << 4) | ((temp_buf[0] & 0xf0) >> 4);
    b[2]  = temp_buf[1];
    b[3]  = temp_buf[2];
    b[4]  = temp_buf[6];
    b[5]  = temp_buf[7];
    b[6]  = temp_buf[8];
    b[7]  = temp_buf[3];
    b[8]  = temp_buf[4];
    b[9]  = temp_buf[5];
    b[10] = 0; //??
    pclog("mke_get_subq: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10]);
}

// Lifted from FreeBSD

static void blk_to_msf(int blk, unsigned char *msf)
{
	blk = blk + 150;			/*2 seconds skip required to
					  reach ISO data*/
	msf[0] = blk / 4500;
	blk = blk % 4500;
	msf[1] = blk / 75;
	msf[2] = blk % 75;
	return;
}

uint8_t mke_read_toc(cdrom_t *dev, unsigned char *b, uint8_t track) {
    track_info_t ti;
    int last_track;
    cdrom_read_toc(dev, temp_buf, CD_TOC_NORMAL, 0, 0, 65536);
    last_track = temp_buf[3];
    if(track > last_track)  return 0;    //should we allow +1 here?
    dev->ops->get_track_info(dev->local, track, 0, &ti);
    b[0]=0;
    b[1]=ti.attr;
    b[2]=ti.number;
    b[3]=0;
    b[4]=ti.m;
    b[5]=ti.s;
    b[6]=ti.f;
    b[7]=0;
    pclog("mke_read_toc: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
    return 1;    
}


uint8_t
mke_disc_info(cdrom_t *dev, unsigned char *b)
{
    uint8_t disc_type_buf[34];
    int          first_track;
    int          last_track;
    cdrom_read_toc(dev, temp_buf, CD_TOC_NORMAL, 0, 2 << 8, 65536);
    cdrom_read_disc_information(dev, disc_type_buf);
    first_track = temp_buf[2];
    last_track  = temp_buf[3];
    // dev->ops->get_track_info(dev, last_track + 1, 0, &ti);
    b[0] = disc_type_buf[8];
    b[1] = first_track;
    b[2] = last_track;
    b[3] = 0;
    b[4] = 0;
    b[5] = 0;
    blk_to_msf(dev->cdrom_capacity, &b[3]);
    pclog("mke_disc_info: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n", b[0], b[1], b[2], b[3], b[4], b[5]);
    return 1;
}

uint8_t
mke_disc_capacity(cdrom_t *dev, unsigned char *b)
{
    track_info_t ti;
    int          last_track;
    // dev->ops->get_tracks(dev, &first_track, &last_track);
    cdrom_read_toc(dev, temp_buf, CD_TOC_NORMAL, 0, 2 << 8, 65536);
    last_track  = temp_buf[3];
    dev->ops->get_track_info(dev, last_track + 1, 0, &ti);
    b[0] = ti.m;
    b[1] = ti.s;
    b[2] = ti.f - 1; // TODO THIS NEEDS TO HANDLE   FRAME 0,  JUST BEING LAZY 6AM
    b[3] = 0x08;
    b[4] = 0x00;

    blk_to_msf(dev->cdrom_capacity, &b[0]);
    pclog("mke_disc_capacity: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n", b[0], b[1], b[2], b[3], b[4]);
    return 1;
}

uint8_t
mke_cdrom_status(cdrom_t *dev, mke_t *mke)
{
    uint8_t status = 0;
    status |= 2; // this bit seems to always be set?
                 // bit 4 never set?
    if (dev->cd_status == CD_STATUS_PLAYING)
        status |= STAT_PLAY;
    if (dev->cd_status == CD_STATUS_PAUSED)
        status |= STAT_PLAY;
    if (fifo8_num_used(&mke->errors_fifo))
        status |= 0x10;
    status |= 0x20; // always set?
    status |= STAT_TRAY;
    if (mke->cdrom_dev->cd_status != CD_STATUS_EMPTY) {
        status |= STAT_DISK;
        status |= STAT_READY;
    }

    return status;
}

void
mke_read_multisess(void)
{
    if ((temp_buf[9] != 0) || (temp_buf[10] != 0) || (temp_buf[11] != 0)) {
        /* Multi-session disc. */
        fifo8_push(&mke.info_fifo, 0x80);
        fifo8_push(&mke.info_fifo, temp_buf[9]);
        fifo8_push(&mke.info_fifo, temp_buf[10]);
        fifo8_push(&mke.info_fifo, temp_buf[11]);
        fifo8_push(&mke.info_fifo, 0);
        fifo8_push(&mke.info_fifo, 0);
    } else {
        uint8_t no_multisess[6] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
        fifo8_push_all(&mke.info_fifo, no_multisess, 6);
    }
}

uint8_t ver[10] = "CR-5630.75";

static void
mke_reset(void)
{
    cdrom_stop(mke.cdrom_dev);
    timer_disable(&mke.timer);
    mke.sector_type            = 0x08 | (1 << 4);
    mke.sector_flags           = 0x10;
    memset(mke.mode_select, 0, 5);
    mke.mode_select[2]         = 0x08;
    mke.patch0                 = 0x01;
    mke.patch1                 = 0x02;
    mke.vol0                   = 255;
    mke.vol1                   = 255;
    mke.cdrom_dev->sector_size = 2048;
}

void
mke_command_callback(void* priv)
{
    switch (mke.command_buffer[0]) {
        case CMD1_SEEK: {
            fifo8_push(&mke.info_fifo, mke_cdrom_status(mke.cdrom_dev, &mke));
            break;
        }
        case CMD1_READ: {
            fifo8_push_all(&mke.data_fifo, mke.cdbuffer, mke.data_to_push);
            fifo8_push(&mke.info_fifo, mke_cdrom_status(mke.cdrom_dev, &mke));
            mke.data_to_push = 0;
            ui_sb_update_icon(SB_CDROM | mke.cdrom_dev->id, 0);
            break;
        }
    }
}

void
mke_command(uint8_t value)
{
    uint16_t     i;
    uint8_t      x[12]; // this is wasteful handling of buffers for compatibility, but will optimize later.
    int          old_cd_status;

    if (mke.command_buffer_pending) {
        mke.command_buffer[6 - mke.command_buffer_pending + 1] = value;
        mke.command_buffer_pending--;
    }

    if (mke.command_buffer[0] == CMD1_ABORT) {
        mke_log("CMD_ABORT\n");
        // fifo8_reset(&mke.info_fifo);
        fifo8_reset(&mke.info_fifo);
        fifo8_reset(&mke.data_fifo);
        timer_disable(&mke.timer);
        mke.command_buffer[0]      = 0;
        mke.command_buffer_pending = 7;
        // fifo8_push(&mke.info_fifo, mke_cdrom_status(mke.cdrom_dev, &mke));
        fifo8_push(&mke.info_fifo, mke_cdrom_status(mke.cdrom_dev, &mke));
    }

    if (!mke.command_buffer_pending && mke.command_buffer[0]) {
        mke.command_buffer_pending = 7;
        pclog("mke_command: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n", mke.command_buffer[0], mke.command_buffer[1], mke.command_buffer[2], mke.command_buffer[3], mke.command_buffer[4], mke.command_buffer[5], mke.command_buffer[6]);
        switch (mke.command_buffer[0]) {
            case 06:
                {
                    fifo8_reset(&mke.info_fifo);
                    cdrom_stop(mke.cdrom_dev);
                    cdrom_eject(mke.cdrom_dev->id);
                    fifo8_push(&mke.info_fifo, mke_cdrom_status(mke.cdrom_dev, &mke));
                    break;
                }
            case 07:
                {
                    fifo8_reset(&mke.info_fifo);
                    cdrom_reload(mke.cdrom_dev->id);
                    fifo8_push(&mke.info_fifo, mke_cdrom_status(mke.cdrom_dev, &mke));
                    break;
                }
            case CMD1_RESET:
            {
                mke_reset();
                break;
            }
            case CMD1_READ:
                {
                    uint32_t count = mke.command_buffer[6];
                    uint8_t *buf   = mke.cdbuffer;
                    int      res   = 0;
                    uint64_t lba   = MSFtoLBA(mke.command_buffer[1], mke.command_buffer[2], mke.command_buffer[3]) - 150;
                    int      len __attribute__((unused)) = 0;
                    CHECK_READY();
                    mke.data_to_push = 0;
                    while (count) {
                        if ((res = cdrom_readsector_raw(mke.cdrom_dev, buf, lba, 0, mke.sector_type, mke.sector_flags, &len, 0)) > 0) {
                            //fifo8_push_all(&mke.data_fifo, buf, mke.cdrom_dev->sector_size);
                            lba++;
                            buf += mke.cdrom_dev->sector_size;
                            mke.data_to_push += mke.cdrom_dev->sector_size;
                        } else {
                            fifo8_push(&mke.errors_fifo, res == 0 ? 0x10 : 0x05);
                            break;
                        }
                        count--;
                    }
                    if (count != 0) {
                        fifo8_reset(&mke.data_fifo);
                        mke.data_to_push = 0;
                    } else {
                        //fifo8_push(&mke.info_fifo, mke_cdrom_status(mke.cdrom_dev, &mke));
                        ui_sb_update_icon(SB_CDROM | mke.cdrom_dev->id, 1);
                        timer_on_auto(&mke.timer, (1000000.0 / (176400.0 * 2.)) * mke.data_to_push);
                    }
                    break;
                }
            case CMD1_READSUBQ:
                CHECK_READY();
                mke_get_subq(mke.cdrom_dev, (uint8_t *) &x);
                fifo8_reset(&mke.info_fifo);
                // fifo8_push_all(&cdrom.info_fifo, x, 11);
                fifo8_push_all(&mke.info_fifo, x, 11);
#if 0
                for (i=0; i < 11; i++) {
                    cdrom_fifo_write(&cdrom.info_fifo,x[i]);
                }
#endif
                fifo8_push(&mke.info_fifo, mke_cdrom_status(mke.cdrom_dev, &mke));
                break;
            case CMD1_SETMODE: // Returns 1
                fifo8_reset(&mke.info_fifo);
                mke_log("CMD: SET MODE:");
                for (i = 0; i < 6; i++) {
                    mke_log("%02x ", mke.command_buffer[i + 1]);
                }
                mke_log("\n");
                switch (mke.command_buffer[1]) {
                    case 0:
                    {
                        switch (mke.command_buffer[2]) {
                            case 0x00: /* Cooked */ {
                                mke.sector_type  = 0x08 | (1 << 4);
                                mke.sector_flags = 0x10;
                                mke.cdrom_dev->sector_size = 2048;
                                break;
                            }
                            case 0x81: /* XA */
                            case 0x01: /* User */ {
                                uint32_t sector_size = (mke.command_buffer[3] << 8) | mke.command_buffer[4];
                                if (!sector_size) {
                                    fifo8_push(&mke.errors_fifo, 0x0e);
                                    return;
                                } else {
                                    switch (sector_size) {
                                        case 2048: {
                                            mke.sector_type  = 0x08 | (1 << 4);
                                            mke.sector_flags = 0x10;
                                            mke.cdrom_dev->sector_size = 2048;
                                            break;
                                        }
                                        case 2052: {
                                            mke.sector_type  = 0x18;
                                            mke.sector_flags = 0x30;
                                            mke.cdrom_dev->sector_size = 2052;
                                            break;
                                        }
                                        case 2324: {
                                            mke.sector_type  = 0x1b;
                                            mke.sector_flags = 0x18;
                                            mke.cdrom_dev->sector_size = 2324;
                                            break;
                                        }
                                        case 2336: {
                                            mke.sector_type  = 0x1c;
                                            mke.sector_flags = 0x58;
                                            mke.cdrom_dev->sector_size = 2336;
                                            break;
                                        }
                                        case 2340: {
                                            mke.sector_type  = 0x18;
                                            mke.sector_flags = 0x78;
                                            mke.cdrom_dev->sector_size = 2340;
                                            break;
                                        }
                                        case 2352: {
                                            mke.sector_type  = 0x00;
                                            mke.sector_flags = 0xf8;
                                            mke.cdrom_dev->sector_size = 2352;
                                            break;
                                        }
                                        default: {
                                            fifo8_push(&mke.errors_fifo, 0x0e);
                                            return;
                                        }
                                    }
                                }
                            }
                            case 0x82: /* DA */ {
                                mke.sector_type  = 0x00;
                                mke.sector_flags = 0xf8;
                                mke.cdrom_dev->sector_size = 2352;
                                break;
                            }
                            default: {
                                fifo8_push(&mke.errors_fifo, 0x0e);
                                return;
                            }
                        }

                        memcpy(mke.mode_select, &mke.command_buffer[2], 5);
                    }
                    case 5: {
                        mke.vol0 = mke.command_buffer[4];
                        mke.vol1 = mke.command_buffer[6];
                        mke.patch0 = mke.command_buffer[3];
                        mke.patch1 = mke.command_buffer[5];
                        break;
                    }
                }
                fifo8_push(&mke.info_fifo, mke_cdrom_status(mke.cdrom_dev, &mke));
                break;
            case CMD1_GETMODE: // 6
                mke_log("GET MODE\n");
                if (mke.command_buffer[1] == 5) {
                    uint8_t volsettings[5] = { 0, mke.patch0, mke.vol0, mke.patch1, mke.vol1 };
                    fifo8_push_all(&mke.info_fifo, volsettings, 5);
                }
                else
                    fifo8_push_all(&mke.info_fifo, mke.mode_select, 5);
                fifo8_push(&mke.info_fifo, mke_cdrom_status(mke.cdrom_dev, &mke));
                break;
            case CMD1_PAUSERESUME:
                CHECK_READY();
                cdrom_audio_pause_resume(mke.cdrom_dev, mke.command_buffer[1] >> 7);
                fifo8_push(&mke.info_fifo, mke_cdrom_status(mke.cdrom_dev, &mke));
                break;
            case CMD1_CAPACITY: // 6
                mke_log("DISK CAPACITY\n");
                CHECK_READY();
                mke_disc_capacity(mke.cdrom_dev, (uint8_t *) &x);
                // fifo8_push_all(&cdrom.info_fifo, x, 5);
                fifo8_push_all(&mke.info_fifo, x, 5);
                fifo8_push(&mke.info_fifo, mke_cdrom_status(mke.cdrom_dev, &mke));
                break;
            case CMD1_DISKINFO: // 7
                mke_log("DISK INFO\n");
                CHECK_READY();
                mke_disc_info(mke.cdrom_dev, (uint8_t *) &x);
                fifo8_push_all(&mke.info_fifo, x, 6);
#if 0
                for (i=0; i<6; i++) {
                    mke_log("%02x ",x[i]);
                    cdrom_fifo_write(&cdrom.info_fifo,x[i]);
                }
                mke_log("\n");
#endif
                fifo8_push(&mke.info_fifo, mke_cdrom_status(mke.cdrom_dev, &mke));
                break;
            case CMD1_READTOC:
                CHECK_READY();
                fifo8_reset(&mke.info_fifo);
#if 0
                mke_log("READ TOC:");  
                for (i=0; i<6; i++) {
                    mke_log("%02x ",mke.command_buffer[i+1]);
                }
                mke_log(" | ");
#endif
                mke_read_toc(mke.cdrom_dev, (uint8_t *) &x, mke.command_buffer[2]);
                fifo8_push_all(&mke.info_fifo, x, 8);
#if 0
                for (i=0; i<8; i++) {
                    mke_log("%02x ",x[i]);
                    cdrom_fifo_write(&cdrom.info_fifo,x[i]);
                }
                mke_log("\n");
#endif
                fifo8_push(&mke.info_fifo, mke_cdrom_status(mke.cdrom_dev, &mke));
                break;
            case CMD1_PLAY_TI:
            {
                CHECK_READY();
                /* Index is ignored for now. */
                fifo8_reset(&mke.info_fifo);
                if (!cdrom_audio_play(mke.cdrom_dev, mke.command_buffer[1], mke.command_buffer[3], 2)) {
                    fifo8_push(&mke.errors_fifo, 0x0E);
                    fifo8_push(&mke.errors_fifo, 0x10);
                } else {
                    fifo8_push(&mke.info_fifo, mke_cdrom_status(mke.cdrom_dev, &mke));
                }
                break;
            }
            case CMD1_PLAY_MSF:
                CHECK_READY();
                fifo8_reset(&mke.info_fifo);
                mke_log("PLAY MSF:");
                for (i = 0; i < 6; i++) {
                    mke_log("%02x ", mke.command_buffer[i + 1]);
                }
                mke_log("\n");
#if 0
                cdrom_audio_playmsf(&cdrom,
                                    mke.command_buffer[1],
                                    mke.command_buffer[2],
                                    mke.command_buffer[3],
                                    mke.command_buffer[4],
                                    mke.command_buffer[5],
                                    mke.command_buffer[6]
                );
#endif
                {
                    int msf = 1;
                    int pos = (mke.command_buffer[1] << 16) | (mke.command_buffer[2] << 8) | mke.command_buffer[3];
                    int len = (mke.command_buffer[4] << 16) | (mke.command_buffer[5] << 8) | mke.command_buffer[6];
                    if (!cdrom_audio_play(mke.cdrom_dev, pos, len, msf)) {
                        fifo8_push(&mke.errors_fifo, 0x0E);
                        fifo8_push(&mke.errors_fifo, 0x10);
                    } else {
                        fifo8_push(&mke.info_fifo, mke_cdrom_status(mke.cdrom_dev, &mke));
                    }
                }
                break;
            case CMD1_SEEK:
                CHECK_READY();
                old_cd_status = mke.cdrom_dev->cd_status;
                fifo8_reset(&mke.info_fifo);
                mke_log("SEEK MSF:"); // TODO: DOES THIS IMPACT CURRENT PLAY LENGTH?
                for (i = 0; i < 6; i++) {
                    mke_log("%02x ", mke.command_buffer[i + 1]);
                }

                cdrom_stop(mke.cdrom_dev);
                /* Note for self: Panasonic/MKE drives send seek commands in MSF format. */
                cdrom_seek(mke.cdrom_dev, MSFtoLBA(mke.command_buffer[1], mke.command_buffer[2], mke.command_buffer[3]) - 150, 0);
                if (old_cd_status == CD_STATUS_PLAYING || old_cd_status == CD_STATUS_PAUSED) {
                    cdrom_audio_play(mke.cdrom_dev, mke.cdrom_dev->seek_pos, -1, 0);
                    cdrom_audio_pause_resume(mke.cdrom_dev, old_cd_status == CD_STATUS_PLAYING);
                }
                fifo8_push(&mke.info_fifo, mke_cdrom_status(mke.cdrom_dev, &mke));
                break;
            case CMD1_SESSINFO:
                CHECK_READY();
                fifo8_reset(&mke.info_fifo);
                mke_log("CMD: READ SESSION INFO\n");
                mke_read_multisess();
                fifo8_push(&mke.info_fifo, mke_cdrom_status(mke.cdrom_dev, &mke));
                break;
            case CMD1_READ_UPC:
                CHECK_READY();
                fifo8_reset(&mke.info_fifo);
                mke_log("CMD: READ UPC\n");
                uint8_t upc[8] = { [0] = 80 };
                fifo8_push_all(&mke.info_fifo, upc, 8);
                fifo8_push(&mke.info_fifo, mke_cdrom_status(mke.cdrom_dev, &mke));
                break;
            case CMD1_READ_ERR:
                fifo8_reset(&mke.info_fifo);
                mke_log("CMD: READ ERR\n");
                // cdrom_read_errors(&cdrom,(uint8_t *)x);
                memset(x, 0, 8);
                if (fifo8_num_used(&mke.errors_fifo)) {
                    fifo8_pop_buf(&mke.errors_fifo, x, fifo8_num_used(&mke.errors_fifo));
                }
                fifo8_push_all(&mke.info_fifo, x, 8);
                fifo8_push(&mke.info_fifo, mke_cdrom_status(mke.cdrom_dev, &mke));
                fifo8_reset(&mke.errors_fifo);
                break;
            case CMD1_READ_VER:
                /*
                SB2CD Expects 12 bytes, but drive only returns 11.
                */
                fifo8_reset(&mke.info_fifo);
                // pclog("CMD: READ VER\n");
                fifo8_push_all(&mke.info_fifo, ver, 10);
                fifo8_push(&mke.info_fifo, mke_cdrom_status(mke.cdrom_dev, &mke));
                break;
            case CMD1_STATUS:
                fifo8_reset(&mke.info_fifo);
                fifo8_push(&mke.info_fifo, mke_cdrom_status(mke.cdrom_dev, &mke));
                break;
            default:
                mke_log("MKE: Unknown Commnad [%02x]\n", mke.command_buffer[0]);
        }
    } else if (!mke.command_buffer_pending) { // we are done byt not in a command.  should we make sure it is a valid command here?
        mke.command_buffer[0]      = value;
        mke.command_buffer_pending = 6;
    }
}

void
mke_write(uint16_t address, uint8_t value, void *priv)
{
    //pclog("MKEWRITE: 0x%X, 0x%02X\n", address & 0xf, value);
    if (mke.enable_register && ((address & 0xF) != 3)) {
        // mke_log("Ignore Write Unit %u\n",mke.enable_register);
        return;
    }
    // mke_log("MKE WRITE: %02x => %03x\n",value,address);
    switch (address & 0xF) {
        case 0:
            mke_command(value);
            break;
        case 1:
            mke.data_select = value;
            break;
        case 2:
            mke_reset();
            break;
        case 3:
            mke.enable_register = value;
            break;
        default:
            pclog("w %03x %02x\n", address, value);
            break;
    }
}

uint8_t
mke_read(uint16_t address, void *priv)
{
    uint8_t x;
    if (mke.enable_register) {
        // pclog("Ignore Read Unit %u\n",mke.enable_register);
        return 0;
    }
    // pclog("MKEREAD: 0x%X\n", address & 0xf);
    switch (address & 0xF) {
        case 0: // Info
            if (mke.data_select) {
                x = fifo8_num_used(&mke.data_fifo) ? fifo8_pop(&mke.data_fifo) : 0; // cdrom_fifo_read(&cdrom.data_fifo);
            } else {
                x = fifo8_num_used(&mke.info_fifo) ? fifo8_pop(&mke.info_fifo) : 0;
                // return cdrom_fifo_read(&cdrom.info_fifo);
            }
            // pclog("Read FIFO 0x%X, %d\n", x, mke.data_select);
            return x;
            break;
        case 1: // Status
            /*
            1 = Status Change
            2 = Data Ready
            4 = Response Ready
            8 = Attention / Issue ?
            */
            x = 0xFF;
            // if(cdrom.media_changed) x ^= 1;
            if (fifo8_num_used(&mke.data_fifo))
                x ^= 2; // DATA FIFO
            if (fifo8_num_used(&mke.info_fifo))
                x ^= 4; // STATUS FIFO
            if (fifo8_num_used(&mke.errors_fifo))
                x ^= 8;
            return x;
            break;
        case 2: // Data
            return fifo8_num_used(&mke.data_fifo) ? fifo8_pop(&mke.data_fifo) : 0;
        case 3:
            return mke.enable_register;
            break;
        default:
            /* mke_log("MKE Unknown Read Address: %03x\n",address); */
            pclog("MKE Unknown Read Address: %03x\n", address);
            break;
    }
    return 0xff;
}

void
mke_close(void *priv)
{
    fifo8_destroy(&mke.info_fifo);
    fifo8_destroy(&mke.data_fifo);
    fifo8_destroy(&mke.errors_fifo);
    timer_disable(&mke.timer);
}

static void
mke_cdrom_insert(void *priv)
{
    mke_t *dev = (mke_t *) priv;

    if ((dev == NULL) || (dev->cdrom_dev == NULL))
        return;

    if (dev->cdrom_dev->ops == NULL) {
        // dev->unit_attention = 0;
        dev->cdrom_dev->cd_status = CD_STATUS_EMPTY;
        if (timer_is_enabled(&dev->timer)) {
            timer_disable(&dev->timer);
            mke.data_to_push = 0;
            fifo8_push(&dev->errors_fifo, 0x15);
        }
        fifo8_push(&dev->errors_fifo, 0x11);
    } else {
        // dev->unit_attention = 1;
        /* Turn off the medium changed status. */
        dev->cdrom_dev->cd_status &= ~CD_STATUS_TRANSITION;
        fifo8_push(&dev->errors_fifo, 0x11);
    }
}

uint32_t
mke_get_volume(void *priv, int channel)
{
    return channel == 0 ? mke.vol0 : mke.vol1;
}

uint32_t
mke_get_channel(void *priv, int channel)
{
    return channel == 0 ? mke.patch0 : mke.patch1;
}

void *
mke_init(const device_t *info)
{
    cdrom_t *dev = NULL;
    memset(&mke, 0, sizeof(mke_t));

    for (uint8_t i = 0; i < CDROM_NUM; i++) {
        if (cdrom[i].bus_type == CDROM_BUS_MKE) {
            dev = &cdrom[i];
            break;
        }
    }

    if (!dev)
        return NULL;

    fifo8_create(&mke.info_fifo, 128);
    fifo8_create(&mke.data_fifo, 624240 * 2);
    fifo8_create(&mke.errors_fifo, 8);
    fifo8_reset(&mke.info_fifo);
    fifo8_reset(&mke.data_fifo);
    fifo8_reset(&mke.errors_fifo);
    mke.cdrom_dev              = dev;
    mke.command_buffer_pending = 7;
    mke.sector_type            = 0x08 | (1 << 4);
    mke.sector_flags           = 0x10;
    mke.mode_select[2]         = 0x08;
    mke.patch0                 = 0x01;
    mke.patch1                 = 0x02;
    mke.vol0                   = 255;
    mke.vol1                   = 255;
    dev->sector_size           = 2048;

    dev->priv          = &mke;
    dev->insert        = mke_cdrom_insert;
    dev->get_volume    = mke_get_volume;
    dev->get_channel   = mke_get_channel;
    dev->cached_sector = -1;

    timer_add(&mke.timer, mke_command_callback, &mke, 0);
    uint16_t base = device_get_config_hex16("base");
    io_sethandler(base, 16, mke_read, NULL, NULL, mke_write, NULL, NULL, &mke);
    return &mke;
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
            { .description = "230H", .value = 0x230 },
            { .description = "250H", .value = 0x250 },
            { .description = "260H", .value = 0x260 },
            { .description = "270H", .value = 0x270 },
            { .description = "290H", .value = 0x290 },
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
    .flags         = DEVICE_ISA16,
    .local         = 0,
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
    .flags         = DEVICE_ISA16,
    .local         = 0,
    .init          = mke_init,
    .close         = mke_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = mke_config
};
