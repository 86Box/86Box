/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Mitsumi CD-ROM emulation for the ISA bus.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2022       Miran Grca.
 *          Copyright 2024-2025 Jasmine Iwanek.
 */

#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <limits.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/dma.h>
#include <86box/cdrom.h>
#include <86box/cdrom_interface.h>
#include <86box/cdrom_mitsumi.h>
#include <86box/plat.h>
#include <86box/sound.h>
#include <86box/timer.h>

#define RAW_SECTOR_SIZE    2352
#define COOKED_SECTOR_SIZE 2048
#define DMA_SERVICE_BUDGET 256

enum {
    STAT_CMD_CHECK = 0x01,
    STAT_PLAY_CDDA = 0x02,
    STAT_ERROR     = 0x04,
    STAT_DISK_CDDA = 0x08,
    STAT_SERVO     = 0x10,
    STAT_CHANGE    = 0x20,
    STAT_READY     = 0x40,
    STAT_OPEN      = 0x80
};
enum {
    CMD_GET_INFO   = 0x10,
    CMD_DISC_INFO  = 0x11,
    CMD_GET_Q      = 0x20,
    CMD_REQ_SENSE  = 0x30,
    CMD_GET_STAT   = 0x40,
    CMD_SET_MODE   = 0x50,
    CMD_SOFT_RESET = 0x60,
    CMD_STOPCDDA   = 0x70,
    CMD_GET_VOL    = 0x8e,
    CMD_CONFIG     = 0x90,
    CMD_SET_SMODE  = 0xa0, // sets mode of sector to read.
    CMD_SET_VOL    = 0xae,
    CMD_READ1X     = 0xc0,
    CMD_READ2X     = 0xc1,
    CMD_GET_VER    = 0xdc,
    CMD_STOP       = 0xf0,
    CMD_EJECT      = 0xf6,
    CMD_CLOSE      = 0xf8,
    CMD_LOCK       = 0xfe
};
enum {
    MODE_MUTE    = 0x01,
    MODE_GET_TOC = 0x04,
    MODE_STOP    = 0x08,
    MODE_ECC     = 0x20,
    MODE_DATA    = 0x40
};
enum {
    DRV_MODE_STOP,
    DRV_MODE_READ,
    DRV_MODE_CDDA
};
enum {
    FLAG_NODATA = 2,
    FLAG_NOSTAT = 4,
    FLAG_UNK  = 8,
    FLAG_OPEN   = 16
};
enum {
    IRQ_DATAREADY = 1,
    IRQ_DATACOMP  = 2,
    IRQ_ERROR     = 4
};

typedef struct mcd_t {
    uint16_t base;
    int      dma;
    int      irq;
    int      change;
    int      data;
    uint8_t  stat;
    uint8_t  buf[RAW_SECTOR_SIZE];
    int      buf_count;
    int      buf_idx;
    uint8_t  cmdbuf[32];
    int      cmdbuf_count;
    int      cmdrd_count;
    int      cmdbuf_idx;
    uint8_t  mode;
    uint8_t  smode;
    uint8_t  cmd;
    uint8_t  conf;
    uint8_t  enable_irq;
    uint8_t  enable_dma;
    uint8_t  early_status;
    uint16_t dmalen;
    uint32_t readmsf;
    uint32_t readcount;
    uint32_t readbuflen;
    int      locked;
    int      drvmode;
    int      cur_toc_track;
    int      tray_open;
    uint32_t dma_retries;
    pc_timer_t dma_timer;

    uint8_t  cur_control;
    uint8_t  cur_sense;

    uint8_t  temp_buf[0x10000];

    struct
    {
        uint8_t att0;
        uint8_t att1;
        uint8_t att2;
        uint8_t att3;
    } cdrom_vols;

    cdrom_t *cdrom_dev;
} mcd_t;

#define CD_DCB(x)         ((((x) &0xf0) >> 4) * 10 + ((x) &0x0f))

#ifdef ENABLE_MITSUMI_CDROM_LOG
int mitsumi_cdrom_do_log = ENABLE_MITSUMI_CDROM_LOG;

void
mitsumi_cdrom_log(const char *fmt, ...)
{
    va_list ap;

    if (mitsumi_cdrom_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define mitsumi_cdrom_log(fmt, ...)
#endif

static int
mitsumi_cdrom_is_ready(const mcd_t *dev)
{
    return (dev->cdrom_dev != NULL) && (dev->cdrom_dev->ops != NULL) &&
           ((dev->cdrom_dev->cd_status & CD_STATUS_MASK) != CD_STATUS_EMPTY) &&
           ((dev->cdrom_dev->cd_status & CD_STATUS_MASK) != CD_STATUS_DVD_REJECTED);
}

static void
mitsumi_set_irq(const mcd_t *dev, const uint8_t mask)
{
    if (dev->enable_irq & mask)
        picint(1 << dev->irq);
}

static uint8_t
mitsumi_status(const mcd_t *dev)
{
    uint8_t status = 0;

    if (dev->tray_open)
        status |= STAT_OPEN;
    if (mitsumi_cdrom_is_ready(dev)) {
        status |= STAT_READY;
        if (dev->cdrom_dev->cd_status & CD_STATUS_HAS_AUDIO)
            status |= STAT_DISK_CDDA;
    }
    if (dev->change)
        status |= STAT_CHANGE;
    if (dev->cdrom_dev && (dev->cdrom_dev->cd_status == CD_STATUS_PLAYING))
        status |= STAT_PLAY_CDDA;

    return status;
}

static void
mitsumi_abort_read(mcd_t *dev)
{
    timer_disable(&dev->dma_timer);
    if ((dev->dma >= 0) && (dev->dma < 8))
        dma_set_drq(dev->dma, 0);
    dev->readcount  = 0;
    dev->buf_count  = 0;
    dev->buf_idx    = 0;
    dev->readbuflen = 0;
    dev->data       = 0;
    dev->drvmode    = DRV_MODE_STOP;
}

static int
mitsumi_msf_to_lba(const uint32_t msf, uint32_t *lba)
{
    const uint8_t minute_bcd = (msf >> 16) & 0xff;
    const uint8_t second_bcd = (msf >> 8) & 0xff;
    const uint8_t frame_bcd  = msf & 0xff;

    if (((minute_bcd & 0x0f) > 9) || ((minute_bcd >> 4) > 9) ||
        ((second_bcd & 0x0f) > 9) || ((second_bcd >> 4) > 9) ||
        ((frame_bcd & 0x0f) > 9) || ((frame_bcd >> 4) > 9))
        return 0;

    const uint8_t minute = CD_DCB(minute_bcd);
    const uint8_t second = CD_DCB(second_bcd);
    const uint8_t frame  = CD_DCB(frame_bcd);
    const uint32_t absolute_lba = MSFtoLBA(minute, second, frame);

    if ((second >= 60) || (frame >= 75) || (absolute_lba < 150))
        return 0;

    *lba = absolute_lba - 150;
    return 1;
}

static void
mitsumi_cdrom_insert(void *priv)
{
    mcd_t *dev = (mcd_t *) priv;

    if ((dev == NULL) || (dev->cdrom_dev == NULL))
        return;

    mitsumi_abort_read(dev);
    cdrom_stop(dev->cdrom_dev);
    dev->change    = 1;
    dev->tray_open = !mitsumi_cdrom_is_ready(dev);
    dev->stat      = mitsumi_status(dev);
    dev->cmdbuf_count = 0;
    picintc(1 << dev->irq);
}

static void
mitsumi_print_cmd(mcd_t* dev, uint8_t command) {
    char cmd_print[32] = { 0 };
    snprintf(cmd_print, sizeof(cmd_print) - 1, "0x%02x", command);

#define CASE(e) case e: \
                    mitsumi_cdrom_log("Mitsumi: command %s, params %d\n", #e, dev->cmdrd_count); \
                    break;
    
    switch (command) {
        default:
            mitsumi_cdrom_log("Mitsumi: command %s, params %d\n", cmd_print, dev->cmdrd_count);
            break;
        CASE(CMD_GET_INFO)
        CASE(CMD_DISC_INFO)
        CASE(CMD_GET_Q)
        CASE(CMD_REQ_SENSE)
        CASE(CMD_GET_STAT)
        CASE(CMD_SET_MODE)
        CASE(CMD_SOFT_RESET)
        CASE(CMD_STOPCDDA)
        CASE(CMD_GET_VOL)
        CASE(CMD_CONFIG)
        CASE(CMD_SET_SMODE)
        CASE(CMD_SET_VOL)
        CASE(CMD_READ1X)
        CASE(CMD_READ2X)
        CASE(CMD_GET_VER)
        CASE(CMD_STOP)
        CASE(CMD_EJECT)
        CASE(CMD_CLOSE)
        CASE(CMD_LOCK)
    }
}

static void
mitsumi_cdrom_reset(mcd_t *dev)
{
    picintc(1 << dev->irq);
    dev->cmdrd_count   = 0;
    dev->cmdbuf_count  = 0;
    dev->cmdbuf_idx    = 0;
    mitsumi_abort_read(dev);
    dev->cur_toc_track = 0;
    dev->enable_dma    = 0;
    dev->enable_irq    = 0;
    dev->early_status  = 0;
    dev->mode          = 0;
    dev->cmd           = 0;
    dev->conf          = 0;
    dev->dmalen        = COOKED_SECTOR_SIZE - 1;
    dev->readmsf       = 0;
    dev->dma_retries   = 0;
    dev->locked        = 0;
    dev->change        = 1;
    dev->data          = 0;
    dev->smode         = 1;
    dev->cur_control   = 0x0c;
    dev->cur_sense     = 0;
    dev->tray_open     = !mitsumi_cdrom_is_ready(dev);

    dev->cdrom_vols.att0 = 255;
    dev->cdrom_vols.att1 = 0;
    dev->cdrom_vols.att2 = 255;
    dev->cdrom_vols.att3 = 0;
    dev->stat = mitsumi_status(dev);
}

uint8_t
mitsumi_disc_info(mcd_t *mcd, unsigned char *b)
{
    cdrom_t *dev               = mcd->cdrom_dev;
    uint8_t  track_type_buf[34];
    int      first_track;
    int      last_track;

    cdrom_read_toc(dev, mcd->temp_buf, CD_TOC_NORMAL, 0, 2 << 8, 65536);
    cdrom_get_track_buffer(mcd->cdrom_dev, track_type_buf);
    first_track = mcd->temp_buf[2];
    last_track  = mcd->temp_buf[3];

    // Yes, it returns first and last tracks in BCD format.
    b[0] = bin2bcd(first_track);
    b[1] = bin2bcd(last_track);


    b[5] = bin2bcd(track_type_buf[2]);
    b[6] = bin2bcd(track_type_buf[3]);
    b[7] = bin2bcd(track_type_buf[4]);
    uint32_t lo = cdrom_lba_to_msf_accurate(dev->cdrom_capacity + 1);
    b[2] = bin2bcd((lo >> 16) & 0xff);
    b[3] = bin2bcd((lo >> 8) & 0xff);
    b[4] = bin2bcd(lo & 0xff);
    return 1;
}

static int
mitsumi_cdrom_read_sector(mcd_t *dev, int first)
{
    uint8_t  status;
    int      ret = 0;
    uint32_t lba;
    uint32_t end_lba;
    uint32_t offset = 0;
    uint32_t available;

    dev->data    = 0;

    if (!mitsumi_cdrom_is_ready(dev))
        return -1;

    if (dev->drvmode == DRV_MODE_CDDA) {
        /* C0 is both PLAY AUDIO and the single-speed data-read command.
           Data drivers conventionally pass FF:FF:FF as an open-ended range;
           only select audio mode when both addresses are valid and the start
           is actually on an audio track. */
        if (mitsumi_msf_to_lba(dev->readmsf, &lba) &&
            mitsumi_msf_to_lba(dev->readcount, &end_lba) && (end_lba > lba) &&
            (dev->cdrom_dev->ops->get_track_type(dev->cdrom_dev->local, lba) == CD_TRACK_AUDIO)) {
            status = cdrom_audio_play(dev->cdrom_dev, lba, end_lba - lba, 0);
            if (status == 1) {
                mitsumi_cdrom_log("Mitsumi read sector: Playing audio.\n");
                return status;
            }
        }
        dev->drvmode = DRV_MODE_READ;
    }

    if (!dev->readcount) {
        mitsumi_set_irq(dev, IRQ_DATACOMP);
        mitsumi_cdrom_log("Mitsumi read complete at sector %u.\n", dev->cdrom_dev->seek_pos);
        dev->cur_toc_track = INT32_MIN;
        return 0;
    }

    if (!mitsumi_msf_to_lba(dev->readmsf, &lba))
        return -2;

    cdrom_stop(dev->cdrom_dev);
    cdrom_seek(dev->cdrom_dev, lba, 0);
    dev->cur_toc_track = INT32_MIN;
    if (dev->cdrom_dev->seek_pos > dev->cdrom_dev->cdrom_capacity) {
        return -2;
    }
    ret = cdrom_readsector_raw(dev->cdrom_dev, dev->buf, dev->cdrom_dev->seek_pos, 0, (dev->smode == 2) ? 3 : 2, (dev->mode & 0x80) ? 0xF8 : 0x10, (int *) &dev->readbuflen, 0);
    mitsumi_cdrom_log("Mitsumi read sector @ %u, ret = %d, readlen = %u, blocklen = %u\n",
                       dev->cdrom_dev->seek_pos, ret, dev->readbuflen, dev->dmalen + 1);
    if (ret <= 0)
        return ret == 0 ? -1 : -3;
    dev->readmsf   = cdrom_lba_to_msf_accurate(dev->cdrom_dev->seek_pos + 1);
    dev->buf_idx   = 0;
    if (dev->mode & 0x80) {
        if (!(dev->mode & MODE_DATA)) {
            // Skip the main header.
            offset = 16;
        }
    }

    available      = MIN(dev->readbuflen, RAW_SECTOR_SIZE);
    offset         = MIN(offset, available);
    dev->buf_idx   = offset;
    available     -= offset;
    if (!(dev->mode & 0x80))
        available = MIN(available, COOKED_SECTOR_SIZE);
    dev->buf_count = MIN((uint32_t) dev->dmalen + 1, available);
    if (dev->buf_count == 0)
        return -3;

    dev->data      = 1;
    dev->readcount--;
    if (first && !dev->early_status)
        mitsumi_set_irq(dev, IRQ_DATAREADY);
    return 1;
}

static int
mitsumi_dma_transfer(mcd_t *dev)
{
    int      dma_result = 0;
    int      read_result = 1;
    uint32_t dma_terminal_budget;
    uint32_t service_budget;

    /* Auto-initialize DMA reloads the terminal count and, on the legacy DMA
       implementation, does not report DMA_OVER.  Bound this synchronous
       service to the count programmed by the guest so it can never spin
       indefinitely while processing an open-ended Mitsumi read command. */
    if ((dev->dma < 0) || (dev->dma >= 8) || (dma[dev->dma].cc < 0))
        return 1;
    dma_terminal_budget = (uint32_t) dma[dev->dma].cc + 1;
    service_budget = MIN(dma_terminal_budget, DMA_SERVICE_BUDGET);

    while (read_result > 0) {
        while (dev->buf_count > 0) {
            if (!cpu_thread_run || is_quit || hard_reset_pending) {
                mitsumi_abort_read(dev);
                return 0;
            }
            const int transfer_bytes = ((dev->dma >= 4) && (dev->buf_count > 1)) ? 2 : 1;
            uint16_t  value = dev->buf[dev->buf_idx];

            if (transfer_bytes == 2)
                value |= dev->buf[dev->buf_idx + 1] << 8;

            dma_result = dma_channel_write(dev->dma, value);
            if (dma_result == DMA_NODATA)
                return 1;
            dev->buf_idx += transfer_bytes;
            dev->buf_count -= transfer_bytes;

            if (dma_result & DMA_OVER) {
                mitsumi_abort_read(dev);
                mitsumi_set_irq(dev, IRQ_DATACOMP);
                return 0;
            }
            if (--service_budget == 0) {
                if (dma_terminal_budget <= DMA_SERVICE_BUDGET) {
                    /* Legacy auto-init DMA reloads its counter without
                       returning DMA_OVER; reaching the original terminal
                       count still completes this device request. */
                    mitsumi_abort_read(dev);
                    mitsumi_set_irq(dev, IRQ_DATACOMP);
                    return 0;
                }
                return 2;
            }
        }

        read_result = mitsumi_cdrom_read_sector(dev, 0);
    }

    return read_result;
}

static void
mitsumi_dma_callback(void *priv)
{
    mcd_t    *dev = (mcd_t *) priv;

    if (!cpu_thread_run || is_quit || hard_reset_pending) {
        mitsumi_abort_read(dev);
        return;
    }

    const int result = mitsumi_dma_transfer(dev);

    if (result == 2) {
        /* Yield between chunks so reset, pause and shutdown requests can be
           handled even during a large or open-ended DMA transfer. */
        dev->dma_retries = 0;
        timer_set_delay_u64(&dev->dma_timer, 10 * TIMER_USEC);
    } else if (result > 0) {
        /* The channel may still be masked or owned by another requester.
           Keep DRQ asserted and retry without blocking the emulation thread. */
        if (++dev->dma_retries < 10000) {
            timer_set_delay_u64(&dev->dma_timer, 100 * TIMER_USEC);
            return;
        }
        dev->cur_sense = 3;
        dev->stat      = mitsumi_status(dev) | STAT_ERROR | STAT_CMD_CHECK;
        mitsumi_abort_read(dev);
        mitsumi_set_irq(dev, IRQ_ERROR);
    } else if (result < 0) {
        dev->cur_sense = abs(result);
        dev->stat      = mitsumi_status(dev) | STAT_ERROR | STAT_CMD_CHECK;
        mitsumi_abort_read(dev);
        mitsumi_set_irq(dev, IRQ_ERROR);
    }
}

static uint8_t
mitsumi_cdrom_get_flags(mcd_t* dev)
{
    uint8_t ret = 0;
    if (!dev->buf_count || !dev->data || dev->enable_dma)
        ret |= FLAG_NODATA;
    if (!dev->cmdbuf_count)
        ret |= FLAG_NOSTAT;
    if (!(ret & FLAG_NODATA) && !(ret & FLAG_NOSTAT))
        ret |= dev->early_status ? FLAG_NODATA : FLAG_NOSTAT;

    if (dev->tray_open)
        ret |= FLAG_OPEN;

    return ret | FLAG_UNK | 1;
}

static uint8_t
mitsumi_cdrom_in(uint16_t port, void *priv)
{
    mcd_t  *dev = (mcd_t *) priv;
    uint8_t ret = 0xff;

    switch (port & 3) {
        case 0:
            if (dev->buf_count && !(mitsumi_cdrom_get_flags(dev) & FLAG_NODATA)) {
                ret = dev->buf[dev->buf_idx];
                dev->buf_idx++;
                dev->buf_count--;
                if (!dev->buf_count) {
                    const int read_result = mitsumi_cdrom_read_sector(dev, 0);
                    if (read_result < 0) {
                        dev->cur_sense = abs(read_result);
                        dev->stat = mitsumi_status(dev) | STAT_ERROR | STAT_CMD_CHECK;
                        mitsumi_set_irq(dev, IRQ_ERROR);
                    }
                }

                //pclog("Read port 0 data\n");
                return ret;
            } else if (dev->cmdbuf_count && !(mitsumi_cdrom_get_flags(dev) & FLAG_NOSTAT)) {
                dev->cmdbuf_count--;
                return dev->cmdbuf[dev->cmdbuf_idx++];
            }
            return 0xFF;
        case 1:
            picintc(1 << dev->irq);
            ret = mitsumi_cdrom_get_flags(dev);
            return ret;
        case 2:
            return 0xFF;
        case 3:
            return 0xFF;
        default:
            break;
    }

    return ret;
}

void
mitsumi_read_multisess(mcd_t* mcd, uint8_t* b)
{
    cdrom_t      *dev            = mcd->cdrom_dev;
    const raw_track_info_t *trti = (raw_track_info_t *) mcd->temp_buf;
    int           num            = 0;
    int           first_sess     = 0;
    int           last_sess      = 0;

    dev->ops->get_raw_track_info(dev->local, &num, mcd->temp_buf);

    memset(b, 0x00, 4);
    if (num > 0) {
        int trk = -1;

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
            if ((trti[i].point >= 1) && (trti[i].point <= 99) &&
                (trti[i].session == last_sess)) {
                trk = i;
                break;
            }
        }

        if ((first_sess > 0) && (last_sess > first_sess) && (trk != -1)) {
            b[0] = 0x01;
            b[1] = bin2bcd(trti[trk].pm);
            b[2] = bin2bcd(trti[trk].ps);
            b[3] = bin2bcd(trti[trk].pf);
        }
    }
    mitsumi_cdrom_log("Mitsumi multisession: %02X %02X %02X %02X\n", b[0], b[1], b[2], b[3]);
}

static void
mitsumi_cdrom_out(uint16_t port, uint8_t val, void *priv)
{
    mcd_t   *dev      = (mcd_t *) priv;
    int      read_res = -1;

    mitsumi_cdrom_log("Mitsumi CD-ROM OUT=%03x, val=%02x\n", port, val);
    switch (port & 3) {
        case 0:
            if (dev->cmdrd_count) {
                dev->cmdrd_count--;
                switch (dev->cmd) {
                    case CMD_SET_SMODE:
                        dev->smode        = val;
                        break;
                    case CMD_SET_MODE:
                        dev->mode         = val;
                        dev->cmdbuf[1]    = 0;
                        dev->cmdbuf_count = 2;
                        break;
                    case CMD_LOCK:
                        dev->locked       = val & 1;
                        dev->cmdbuf[1]    = 0;
                        dev->cmdbuf[2]    = 0;
                        dev->cmdbuf_count = 3;
                        break;
                    case CMD_SET_VOL:
                        switch (dev->cmdrd_count) {
                            case 3:
                                dev->cdrom_vols.att0 = val;
                                break;
                            case 2:
                                dev->cdrom_vols.att1 = val;
                                break;
                            case 1:
                                dev->cdrom_vols.att2 = val;
                                break;
                            case 0:
                                dev->cdrom_vols.att3 = val;
                                break;
                        }
                        break;
                    case CMD_CONFIG:
                        switch (dev->cmdrd_count) {
                            case 0:
                                switch (dev->conf) {
                                    case 0x01:
                                        dev->dmalen |= val;
                                        break;
                                    case 0x02:
                                        dev->enable_dma = val;
                                        if (!dev->enable_dma)
                                            mitsumi_abort_read(dev);
                                        break;
                                    case 0x10:
                                        dev->enable_irq = val;
                                        break;
                                    default:
                                        break;
                                }
                                dev->cmdbuf[1]    = 0;
                                dev->cmdbuf_count = 2;
                                dev->conf         = 0;
                                break;
                            case 1:
                                if (dev->conf == 1) {
                                    dev->dmalen = val << 8;
                                    break;
                                }
                                dev->conf = val;
                                if (dev->conf == 1)
                                    dev->cmdrd_count++;
                                break;
                            default:
                                break;
                        }
                        break;
                    case CMD_READ1X:
                    case CMD_READ2X:
                        switch (dev->cmdrd_count) {
                            case 0:
                                dev->readcount |= val;
                                if (!dev->readcount && dev->early_status) {
                                    dev->readcount = 0xFFFFFFFF; // keep fetching sectors indefinitely.
                                }
                                read_res = mitsumi_cdrom_read_sector(dev, 1);
                                if (dev->enable_dma && read_res > 0) {
                                    dev->dma_retries = 0;
                                    dma_set_drq(dev->dma, 1);
                                    timer_set_delay_u64(&dev->dma_timer, TIMER_USEC);
                                }
                                dev->cmdbuf_count = 1;
                                if (read_res < 0) {
                                    dev->cur_sense = abs(read_res);
                                    mitsumi_set_irq(dev, IRQ_ERROR);
                                }
                                dev->cmdbuf[0] = (read_res < 0) ? (STAT_ERROR | STAT_CMD_CHECK | dev->stat) : (STAT_READY | dev->stat);
                                break;
                            case 1:
                                dev->readcount |= (val << 8);
                                break;
                            case 2:
                                dev->readcount    = ((val & 0x0f) << 16);
                                dev->early_status = ((val & 0xf0) == 0xf0);
                                if (dev->early_status)
                                    mitsumi_cdrom_log("Mitsumi early-status read\n");
                                break;
                            case 5:
                                dev->readmsf = 0;
                                fallthrough;
                            case 4:
                            case 3:
                                /* Keep the command address in its native BCD form.
                                   mitsumi_msf_to_lba() validates and converts it
                                   when the transfer starts. */
                                dev->readmsf |= val << ((dev->cmdrd_count - 3) << 3);
                                break;
                            default:
                                break;
                        }
                        break;
                    default:
                        break;
                }
                if (!dev->cmdrd_count)
                    dev->stat = mitsumi_status(dev);
                return;
            }
            dev->cmd          = val;
            dev->cmdbuf_idx   = 0;
            dev->cmdrd_count  = 0;
            dev->cmdbuf_count = 1;
            dev->stat         = mitsumi_status(dev);
            dev->cmdbuf[0]    = dev->stat;
            dev->data         = 0;
            switch (val) {
                case CMD_REQ_SENSE:
                    dev->cmdbuf[1]    = dev->cur_sense;
                    dev->cmdbuf_count = 2;
                    dev->cur_sense    = 0;
                    break;
                case CMD_SET_VOL:
                    dev->cmdrd_count = 4;
                    break;
                case CMD_DISC_INFO:
                    if (mitsumi_cdrom_is_ready(dev)) {
                        mitsumi_read_multisess(dev, &dev->cmdbuf[1]);
                        dev->cmdbuf_count = 5;
                        dev->readcount    = 0;
                    } else {
                        dev->cmdbuf_count = 1;
                        dev->cmdbuf[0]    = STAT_CMD_CHECK | dev->stat;
                    }
                    break;
                case CMD_GET_INFO:
                    if (mitsumi_cdrom_is_ready(dev)) {
                        dev->cmdbuf_count = 9;
                        dev->cmdbuf[0] = dev->stat;
                        mitsumi_disc_info(dev, &dev->cmdbuf[1]);
                        dev->readcount = 0;
                    } else {
                        dev->cmdbuf_count = 1;
                        dev->cmdbuf[0]    = STAT_CMD_CHECK | dev->stat;
                    }
                    break;
                case CMD_GET_VOL:
                    dev->cmdbuf_count = 5;
                    dev->cmdbuf[1]    = dev->cdrom_vols.att0;
                    dev->cmdbuf[2]    = dev->cdrom_vols.att1;
                    dev->cmdbuf[3]    = dev->cdrom_vols.att2;
                    dev->cmdbuf[4]    = dev->cdrom_vols.att3;
                    break;
                case CMD_GET_Q:
                    if (mitsumi_cdrom_is_ready(dev)) {
                        dev->cur_toc_track = cdrom_get_q(dev->cdrom_dev, &dev->cmdbuf[1], dev->cur_toc_track, dev->mode & MODE_GET_TOC);
                        dev->cmdbuf_count  = 11;
                        dev->readcount     = 0;
                        dev->cmdbuf[0]     = dev->stat;
                    } else {
                        dev->cmdbuf_count  = 1;
                        dev->cmdbuf[0]     = STAT_CMD_CHECK | dev->stat;
                    }
                    break;
                case CMD_GET_STAT:
                    dev->change = 0;
                    dev->stat   = mitsumi_status(dev);
                    break;
                case CMD_SET_MODE:
                    dev->cmdrd_count = 1;
                    break;
                case CMD_STOPCDDA:
                case CMD_STOP:
                    mitsumi_abort_read(dev);
                    cdrom_stop(dev->cdrom_dev);
                    dev->cur_toc_track = 0;
                    break;
                case CMD_CONFIG:
                    dev->cmdrd_count = 2;
                    break;
                case CMD_READ1X:
                case CMD_READ2X:
                    if (mitsumi_cdrom_is_ready(dev)) {
                        mitsumi_abort_read(dev);
                        dev->readcount   = 0;
                        dev->drvmode     = (val == CMD_READ1X) ? DRV_MODE_CDDA : DRV_MODE_READ;
                        dev->cmdrd_count = 6;
                    } else {
                        dev->cmdbuf_count = 1;
                        dev->cmdbuf[0]    = STAT_CMD_CHECK;
                    }
                    break;
                case CMD_GET_VER:
                    dev->cmdbuf[0]    = dev->stat;
                    dev->cmdbuf[1]    = 'D';
                    dev->cmdbuf[2]    = 0x10;
                    dev->cmdbuf_count = 3;
                    break;
                case CMD_CLOSE:
                    if (dev->tray_open) {
                        dev->tray_open = 0;
                        cdrom_reload(dev->cdrom_dev->id);
                        dev->change = 1;
                    }
                    dev->stat = mitsumi_status(dev);
                    dev->cmdbuf[0] = dev->stat;
                    break;
                case CMD_EJECT:
                    if (dev->locked) {
                        dev->cur_sense = 4;
                        dev->cmdbuf[0] = dev->stat | STAT_ERROR | STAT_CMD_CHECK;
                        mitsumi_set_irq(dev, IRQ_ERROR);
                    } else {
                        mitsumi_abort_read(dev);
                        cdrom_stop(dev->cdrom_dev);
                        cdrom_eject(dev->cdrom_dev->id);
                        dev->tray_open = 1;
                        dev->change = 1;
                        dev->stat = mitsumi_status(dev);
                        dev->cmdbuf[0] = dev->stat;
                    }
                    break;
                case CMD_LOCK:
                case CMD_SET_SMODE:
                    dev->cmdrd_count = 1;
                    break;
                case CMD_SOFT_RESET:
                    mitsumi_cdrom_log("Mitsumi soft reset\n");
                    mitsumi_cdrom_reset(dev);
                    dev->cmdbuf_count = 1;
                    dev->cmdbuf[0]    = dev->stat;
                    break;
                default:
                    dev->cmdbuf[0] = dev->stat | STAT_CMD_CHECK;
                    mitsumi_cdrom_log("Mitsumi: unhandled command %02X\n", val);
                    break;
            }
            mitsumi_print_cmd(dev, val);
            break;
        case 1:
            dev->cmdbuf_count = 1;
            dev->cmdbuf[0]    = dev->stat;
            break;
        case 2:
            dev->cur_control  = val;
            break;
        case 3:
            mitsumi_cdrom_reset(dev);
            break;
        default:
            break;
    }
}

uint32_t
mitsumi_get_volume(void *priv, int channel)
{
    mcd_t   *dev      = (mcd_t *) priv;

    if (dev->mode & MODE_MUTE)
        return 0;

    switch (channel & 3) {
        case 0:
            return dev->cdrom_vols.att0;
        case 1:
            return dev->cdrom_vols.att2;
        case 2:
            return dev->cdrom_vols.att1;
        case 3:
            return dev->cdrom_vols.att3;
    }
    return dev->cdrom_vols.att0;
}

uint32_t
mitsumi_get_channel(void *priv, int channel)
{
    mcd_t   *dev      = (mcd_t *) priv;

    return (channel == 0) ? ((!!(dev->cdrom_vols.att0)) | ((!!(dev->cdrom_vols.att1)) << 1)) :
                            ((!!(dev->cdrom_vols.att2)) | ((!!(dev->cdrom_vols.att3)) << 1));
}

static void *
mitsumi_cdrom_init(UNUSED(const device_t *info))
{
    mcd_t *dev = calloc(1, sizeof(mcd_t));

    for (uint8_t i = 0; i < CDROM_NUM; i++) {
        if (cdrom[i].bus_type == CDROM_BUS_MITSUMI) {
            dev->cdrom_dev = &cdrom[i];
            break;
        }
    }

    if (!dev->cdrom_dev) {
        free(dev);
        return NULL;
    }

    dev->cdrom_dev->priv        = dev;
    dev->cdrom_dev->insert      = mitsumi_cdrom_insert;
    dev->cdrom_dev->get_volume  = mitsumi_get_volume;
    dev->cdrom_dev->get_channel = mitsumi_get_channel;
    dev->cdrom_dev->cached_sector = -1;
    dev->cdrom_dev->subc_sector = -1;

    dev->base = device_get_config_hex16("base");
    dev->irq  = device_get_config_int("irq");
    dev->dma  = device_get_config_int("dma");

    io_sethandler(dev->base, 4,
                  mitsumi_cdrom_in, NULL, NULL, mitsumi_cdrom_out, NULL, NULL, dev);

    timer_add(&dev->dma_timer, mitsumi_dma_callback, dev, 0);
    mitsumi_cdrom_reset(dev);

    return dev;
}

static void
mitsumi_cdrom_close(void *priv)
{
    mcd_t *dev = (mcd_t *) priv;

    if (dev) {
        mitsumi_abort_read(dev);
        io_removehandler(dev->base, 4,
                         mitsumi_cdrom_in, NULL, NULL, mitsumi_cdrom_out, NULL, NULL, dev);
        picintc(1 << dev->irq);
        if (dev->cdrom_dev && (dev->cdrom_dev->priv == dev)) {
            dev->cdrom_dev->priv        = NULL;
            dev->cdrom_dev->insert      = NULL;
            dev->cdrom_dev->get_volume  = NULL;
            dev->cdrom_dev->get_channel = NULL;
        }
        free(dev);
    }
}

static void
mitsumi_cdrom_device_reset(void *priv)
{
    mcd_t *dev = (mcd_t *) priv;

    if (dev)
        mitsumi_cdrom_reset(dev);
}

static const device_config_t mitsumi_config[] = {
    // clang-format off
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x310,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "300H", .value = 0x300 },
            { .description = "310H", .value = 0x310 },
            { .description = "320H", .value = 0x320 },
            { .description = "340H", .value = 0x340 },
            { .description = "350H", .value = 0x350 },
            { NULL                                  }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "irq",
        .description    = "IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 5,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "IRQ 3",  .value =  3 },
            { .description = "IRQ 5",  .value =  5 },
            { .description = "IRQ 9",  .value =  9 },
            { .description = "IRQ 10", .value = 10 },
            { .description = "IRQ 11", .value = 11 },
            { .description = ""                    }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "dma",
        .description    = "DMA",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 5,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "DMA 5", .value = 5 },
            { .description = "DMA 6", .value = 6 },
            { .description = "DMA 7", .value = 7 },
            { .description = ""                  }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
// clang-format off
};

const device_t mitsumi_cdrom_device = {
    .name          = "Mitsumi interface",
    .internal_name = "mcd",
    .flags         = DEVICE_ISA16,
    .local         = 0,
    .init          = mitsumi_cdrom_init,
    .close         = mitsumi_cdrom_close,
    .reset         = mitsumi_cdrom_device_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = mitsumi_config
};
