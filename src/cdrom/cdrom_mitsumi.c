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
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2022 Miran Grca.
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
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

#define MCD_DEFAULT_IOPORT 0x310
#define MCD_DEFAULT_IRQ    5
#define MCD_DEFAULT_DMA    5

#define RAW_SECTOR_SIZE    2352
#define COOKED_SECTOR_SIZE 2048

enum {
    STAT_CMD_CHECK = 0x01,
    STAT_PLAY_CDDA = 0x02,
    STAT_ERROR     = 0x04,
    STAT_DISK_CDDA = 0x08,
    STAT_SPIN      = 0x10,
    STAT_CHANGE    = 0x20,
    STAT_READY     = 0x40,
    STAT_OPEN      = 0x80
};
enum {
    CMD_GET_INFO   = 0x10,
    CMD_GET_Q      = 0x20,
    CMD_GET_STAT   = 0x40,
    CMD_SET_MODE   = 0x50,
    CMD_SOFT_RESET = 0x60,
    CMD_STOPCDDA   = 0x70,
    CMD_CONFIG     = 0x90,
    CMD_SET_VOL    = 0xae,
    CMD_READ1X     = 0xc0,
    CMD_READ2X     = 0xc1,
    CMD_GET_VER    = 0xdc,
    CMD_STOP       = 0xf0,
    CMD_EJECT      = 0xf6,
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
    FLAG_UNK    = 8, //??
    FLAG_OPEN   = 16
};
enum {
    IRQ_DATAREADY = 1,
    IRQ_DATACOMP  = 2,
    IRQ_ERROR     = 4
};

typedef struct mcd_t {
    int      dma;
    int      irq;
    int      change;
    int      data;
    uint8_t  stat;
    uint8_t  buf[RAW_SECTOR_SIZE];
    int      buf_count;
    int      buf_idx;
    uint8_t  cmdbuf[16];
    int      cmdbuf_count;
    int      cmdrd_count;
    int      cmdbuf_idx;
    uint8_t  mode;
    uint8_t  cmd;
    uint8_t  conf;
    uint8_t  enable_irq;
    uint8_t  enable_dma;
    uint16_t dmalen;
    uint32_t readmsf;
    uint32_t readcount;
    int      locked;
    int      drvmode;
    int      cur_toc_track;
    int      pos;
    int      newstat;
} mcd_t;

#define CD_BCD(x)         (((x) % 10) | (((x) / 10) << 4))
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
mitsumi_cdrom_is_ready(const cdrom_t *dev)
{
    return (dev->image_path[0] != 0x00);
}

static void
mitsumi_cdrom_reset(mcd_t *dev)
{
    cdrom_t *cdrom = calloc(1, sizeof(cdrom_t));

    dev->stat          = mitsumi_cdrom_is_ready(cdrom) ? (STAT_READY | STAT_CHANGE) : 0;
    dev->cmdrd_count   = 0;
    dev->cmdbuf_count  = 0;
    dev->buf_count     = 0;
    dev->cur_toc_track = 0;
    dev->enable_dma    = 0;
    dev->enable_irq    = 0;
    dev->conf          = 0;
    dev->dmalen        = COOKED_SECTOR_SIZE;
    dev->locked        = 0;
    dev->change        = 1;
    dev->newstat       = 1;
    dev->data          = 0;
}

static int
mitsumi_cdrom_read_sector(mcd_t *dev, int first)
{
    cdrom_t *cdrom = calloc(1, sizeof(cdrom_t));
    uint8_t  status;
    int      ret = 0;

    if (dev->drvmode == DRV_MODE_CDDA) {
        status = cdrom_mitsumi_audio_play(cdrom, dev->readmsf, dev->readcount);
        if (status == 1)
            return status;
        else
            dev->drvmode = DRV_MODE_READ;
    }

    if ((dev->enable_irq & IRQ_DATACOMP) && !first) {
        picint(1 << dev->irq);
    }
    if (!dev->readcount) {
        dev->data = 0;
        return 0;
    }
    cdrom_stop(cdrom);
    ret = cdrom_readsector_raw(cdrom, dev->buf, cdrom->seek_pos, 0, 2, 0x10, (int *) &dev->readcount, 0);
    if (ret <= 0)
        return 0;
    if (dev->mode & 0x40) {
        dev->buf[12] = CD_BCD((dev->readmsf >> 16) & 0xff);
        dev->buf[13] = CD_BCD((dev->readmsf >> 8) & 0xff);
    }
    dev->readmsf   = cdrom_lba_to_msf_accurate(cdrom->seek_pos + 1);
    dev->buf_count = dev->dmalen + 1;
    dev->buf_idx   = 0;
    dev->data      = 1;
    if (dev->enable_dma) {
        while (dev->pos < dev->readcount) {
            dma_channel_write(dev->dma, dev->buf[dev->pos]);
            dev->pos++;
        }
        dev->pos = 0;
    }
    dev->readcount--;
    if ((dev->enable_irq & IRQ_DATAREADY) && first)
        picint(1 << dev->irq);
    return 1;
}

static uint8_t
mitsumi_cdrom_in(uint16_t port, void *priv)
{
    mcd_t  *dev = (mcd_t *) priv;
    uint8_t ret = 0xff;

    pclog("Mitsumi CD-ROM IN=%03x\n", port);
    switch (port & 1) {
        case 0:
            if (dev->cmdbuf_count) {
                dev->cmdbuf_count--;
                return dev->cmdbuf[dev->cmdbuf_idx++];
            } else if (dev->buf_count) {
                ret = (dev->buf_idx < RAW_SECTOR_SIZE) ? dev->buf[dev->buf_idx] : 0;
                dev->buf_idx++;
                dev->buf_count--;
                if (!dev->buf_count)
                    mitsumi_cdrom_read_sector(dev, 0);

                pclog("Read port 0: ret = %02x\n", ret);
                return ret;
            }
            pclog("Read port 0: stat = %02x\n", dev->stat);
            return dev->stat;
        case 1:
            ret = 0;
            picintc(1 << dev->irq);
            if (!dev->buf_count || !dev->data || dev->enable_dma)
                ret |= FLAG_NODATA;
            if (!dev->cmdbuf_count || !dev->newstat)
                ret |= FLAG_NOSTAT;
            pclog("Read port 1: ret = %02x\n", ret | FLAG_UNK);
            return ret | FLAG_UNK;
        case 2:
            break;
        default:
            break;
    }

    return ret;
}

static void
mitsumi_cdrom_out(uint16_t port, uint8_t val, void *priv)
{
    mcd_t   *dev   = (mcd_t *) priv;
    cdrom_t *cdrom = calloc(1, sizeof(cdrom_t));

    pclog("Mitsumi CD-ROM OUT=%03x, val=%02x\n", port, val);
    switch (port & 1) {
        case 0:
            if (dev->cmdrd_count) {
                dev->cmdrd_count--;
                switch (dev->cmd) {
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
                    case CMD_CONFIG:
                        switch (dev->cmdrd_count) {
                            case 0:
                                switch (dev->conf) {
                                    case 0x01:
                                        dev->dmalen |= val;
                                        break;
                                    case 0x02:
                                        dev->enable_dma = val;
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
                                mitsumi_cdrom_read_sector(dev, 1);
                                dev->cmdbuf_count = 1;
                                dev->cmdbuf[0]    = STAT_SPIN | STAT_READY;
                                break;
                            case 1:
                                dev->readcount |= (val << 8);
                                break;
                            case 2:
                                dev->readcount = (val << 16);
                                break;
                            case 5:
                                dev->readmsf = 0;
                                fallthrough;
                            case 4:
                            case 3:
                                dev->readmsf |= CD_DCB(val) << ((dev->cmdrd_count - 3) << 3);
                                break;
                            default:
                                break;
                        }
                        break;
                    default:
                        break;
                }
                if (!dev->cmdrd_count)
                    dev->stat = mitsumi_cdrom_is_ready(cdrom) ? (STAT_READY | (dev->change ? STAT_CHANGE : 0)) : 0;
                return;
            }
            dev->cmd          = val;
            dev->cmdbuf_idx   = 0;
            dev->cmdrd_count  = 0;
            dev->cmdbuf_count = 1;
            dev->cmdbuf[0]    = mitsumi_cdrom_is_ready(cdrom) ? (STAT_READY | (dev->change ? STAT_CHANGE : 0)) : 0;
            dev->data         = 0;
            switch (val) {
                case CMD_GET_INFO:
                    if (mitsumi_cdrom_is_ready(cdrom)) {
                        cdrom_get_track_buffer(cdrom, &(dev->cmdbuf[1]));
                        dev->cmdbuf_count = 10;
                        dev->readcount    = 0;
                    } else {
                        dev->cmdbuf_count = 1;
                        dev->cmdbuf[0]    = STAT_CMD_CHECK;
                    }
                    break;
                case CMD_GET_Q:
                    if (mitsumi_cdrom_is_ready(cdrom)) {
                        cdrom_get_q(cdrom, &(dev->cmdbuf[1]), &dev->cur_toc_track, dev->mode & MODE_GET_TOC);
                        dev->cmdbuf_count = 11;
                        dev->readcount    = 0;
                    } else {
                        dev->cmdbuf_count = 1;
                        dev->cmdbuf[0]    = STAT_CMD_CHECK;
                    }
                    break;
                case CMD_GET_STAT:
                    dev->change = 0;
                    break;
                case CMD_SET_MODE:
                    dev->cmdrd_count = 1;
                    break;
                case CMD_STOPCDDA:
                case CMD_STOP:
                    cdrom_stop(cdrom);
                    dev->drvmode       = DRV_MODE_STOP;
                    dev->cur_toc_track = 0;
                    break;
                case CMD_CONFIG:
                    dev->cmdrd_count = 2;
                    break;
                case CMD_READ1X:
                case CMD_READ2X:
                    if (mitsumi_cdrom_is_ready(cdrom)) {
                        dev->readcount   = 0;
                        dev->drvmode     = (val == CMD_READ1X) ? DRV_MODE_CDDA : DRV_MODE_READ;
                        dev->cmdrd_count = 6;
                    } else {
                        dev->cmdbuf_count = 1;
                        dev->cmdbuf[0]    = STAT_CMD_CHECK;
                    }
                    break;
                case CMD_GET_VER:
                    dev->cmdbuf[0]    = 1;
                    dev->cmdbuf[1]    = 'D';
                    dev->cmdbuf[2]    = 0;
                    dev->cmdbuf_count = 3;
                    break;
                case CMD_EJECT:
                    cdrom_stop(cdrom);
                    cdrom_eject(0);
                    dev->readcount = 0;
                    break;
                case CMD_LOCK:
                    dev->cmdrd_count = 1;
                    break;
                case CMD_SOFT_RESET:
                    pclog("Soft Reset\n");
                    mitsumi_cdrom_reset(dev);
                    break;
                default:
                    dev->cmdbuf[0] = dev->stat | STAT_CMD_CHECK;
                    break;
            }
            break;
        case 1:
            mitsumi_cdrom_reset(dev);
            break;
        case 2:
            break;
        default:
            break;
    }
}

static void *
mitsumi_cdrom_init(UNUSED(const device_t *info))
{
    mcd_t *dev = calloc(1, sizeof(mcd_t));

    dev->irq = MCD_DEFAULT_IRQ;
    dev->dma = MCD_DEFAULT_DMA;

    io_sethandler(MCD_DEFAULT_IOPORT, 3,
                  mitsumi_cdrom_in, NULL, NULL, mitsumi_cdrom_out, NULL, NULL, dev);

    mitsumi_cdrom_reset(dev);

    return dev;
}

static void
mitsumi_cdrom_close(void *priv)
{
    mcd_t *dev = (mcd_t *) priv;

    if (dev) {
        free(dev);
        dev = NULL;
    }
}

const device_t mitsumi_cdrom_device = {
    .name          = "Mitsumi CD-ROM interface",
    .internal_name = "mcd",
    .flags         = DEVICE_ISA | DEVICE_AT,
    .local         = 0,
    .init          = mitsumi_cdrom_init,
    .close         = mitsumi_cdrom_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
