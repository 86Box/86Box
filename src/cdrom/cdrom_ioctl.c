/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          CD-ROM passthrough support.
 *
 *
 *
 * Authors: TheCollector1995, <mariogplayer@gmail.com>,
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2023 TheCollector1995.
 *          Copyright 2023 Miran Grca.
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/plat_cdrom.h>
#include <86box/scsi_device.h>
#include <86box/cdrom.h>

#ifdef ENABLE_CDROM_IOCTL_LOG
int cdrom_ioctl_do_log = ENABLE_CDROM_IOCTL_LOG;

void
cdrom_ioctl_log(const char *fmt, ...)
{
    va_list ap;

    if (cdrom_ioctl_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define cdrom_ioctl_log(fmt, ...)
#endif

/* The addresses sent from the guest are absolute, ie. a LBA of 0 corresponds to a MSF of 00:00:00. Otherwise, the counter displayed by the guest is wrong:
   there is a seeming 2 seconds in which audio plays but counter does not move, while a data track before audio jumps to 2 seconds before the actual start
   of the audio while audio still plays. With an absolute conversion, the counter is fine. */
#define MSFtoLBA(m, s, f) ((((m * 60) + s) * 75) + f)

static void
ioctl_get_tracks(UNUSED(cdrom_t *dev), int *first, int *last)
{
    TMSF        tmsf;

    plat_cdrom_get_audio_tracks(first, last, &tmsf);
}

static void
ioctl_get_track_info(UNUSED(cdrom_t *dev), uint32_t track, int end, track_info_t *ti)
{
    TMSF      tmsf;

    plat_cdrom_get_audio_track_info(end, track, &ti->number, &tmsf, &ti->attr);

    ti->m = tmsf.min;
    ti->s = tmsf.sec;
    ti->f = tmsf.fr;
}

static void
ioctl_get_subchannel(UNUSED(cdrom_t *dev), uint32_t lba, subchannel_t *subc)
{
    TMSF      rel_pos;
    TMSF      abs_pos;

    if ((dev->cd_status == CD_STATUS_PLAYING) || (dev->cd_status == CD_STATUS_PAUSED)) {
        const uint32_t trk = plat_cdrom_get_track_start(lba, &subc->attr, &subc->track);

        FRAMES_TO_MSF(lba + 150, &abs_pos.min, &abs_pos.sec, &abs_pos.fr);

        /* Absolute position should be adjusted by 150, not the relative ones. */
        FRAMES_TO_MSF(lba - trk, &rel_pos.min, &rel_pos.sec, &rel_pos.fr);

        subc->index  = 1;
    } else
        plat_cdrom_get_audio_sub(lba, &subc->attr, &subc->track, &subc->index,
                                 &rel_pos, &abs_pos);

    subc->abs_m = abs_pos.min;
    subc->abs_s = abs_pos.sec;
    subc->abs_f = abs_pos.fr;

    subc->rel_m = rel_pos.min;
    subc->rel_s = rel_pos.sec;
    subc->rel_f = rel_pos.fr;

    cdrom_ioctl_log("ioctl_get_subchannel(): %02X, %02X, %02i, %02i:%02i:%02i, %02i:%02i:%02i\n",
                    subc->attr, subc->track, subc->index, subc->abs_m, subc->abs_s, subc->abs_f, subc->rel_m, subc->rel_s, subc->rel_f);
}

static int
ioctl_get_capacity(UNUSED(cdrom_t *dev))
{
    int ret;

    ret = plat_cdrom_get_last_block();
    cdrom_ioctl_log("GetCapacity=%x.\n", ret);
    return ret;
}

static int
ioctl_is_track_audio(cdrom_t *dev, uint32_t pos, int ismsf)
{
    int       m;
    int       s;
    int       f;

    if (dev->cd_status == CD_STATUS_DATA_ONLY)
        return 0;

    if (ismsf) {
        m   = (pos >> 16) & 0xff;
        s   = (pos >> 8) & 0xff;
        f   = pos & 0xff;
        pos = MSFtoLBA(m, s, f) - 150;
    }

    /* GetTrack requires LBA. */
    return plat_cdrom_is_track_audio(pos);
}

static int
ioctl_is_track_pre(UNUSED(cdrom_t *dev), uint32_t lba)
{
    return plat_cdrom_is_track_pre(lba);
}

static int
ioctl_sector_size(UNUSED(cdrom_t *dev), uint32_t lba)
{
    cdrom_ioctl_log("LBA=%x.\n", lba);
    return plat_cdrom_get_sector_size(lba);
}

static int
ioctl_read_sector(UNUSED(cdrom_t *dev), int type, uint8_t *b, uint32_t lba)
{
    switch (type) {
        case CD_READ_DATA:
            cdrom_ioctl_log("cdrom_ioctl_read_sector(): Data.\n");
            return plat_cdrom_read_sector(b, 0, lba);
        case CD_READ_AUDIO:
            cdrom_ioctl_log("cdrom_ioctl_read_sector(): Audio.\n");
            return plat_cdrom_read_sector(b, 1, lba);
        case CD_READ_RAW:
            cdrom_ioctl_log("cdrom_ioctl_read_sector(): Raw.\n");
            return plat_cdrom_read_sector(b, 1, lba);
        default:
            cdrom_ioctl_log("cdrom_ioctl_read_sector(): Unknown CD read type.\n");
            break;
    }
    return 0;
}

static int
ioctl_track_type(cdrom_t *dev, uint32_t lba)
{
    int ret = 0;

    if (ioctl_is_track_audio(dev, lba, 0))
        ret = CD_TRACK_AUDIO;

    cdrom_ioctl_log("cdrom_ioctl_track_type(): %i\n", ret);

    return ret;
}

static int
ioctl_ext_medium_changed(cdrom_t *dev)
{
    int ret;

    if ((dev->cd_status == CD_STATUS_PLAYING) || (dev->cd_status == CD_STATUS_PAUSED))
        ret = 0;
    else
        ret = plat_cdrom_ext_medium_changed();

    if (ret == 1) {
        dev->cd_status      = CD_STATUS_STOPPED;
        dev->cdrom_capacity = ioctl_get_capacity(dev);
    } else if (ret == -1)
        dev->cd_status      = CD_STATUS_EMPTY;

    return ret;
}

static void
ioctl_exit(cdrom_t *dev)
{
    cdrom_ioctl_log("CDROM: ioctl_exit(%s)\n", dev->image_path);
    dev->cd_status = CD_STATUS_EMPTY;

    plat_cdrom_close();

    dev->ops = NULL;
}

static const cdrom_ops_t cdrom_ioctl_ops = {
    ioctl_get_tracks,
    ioctl_get_track_info,
    ioctl_get_subchannel,
    ioctl_is_track_pre,
    ioctl_sector_size,
    ioctl_read_sector,
    ioctl_track_type,
    ioctl_ext_medium_changed,
    ioctl_exit
};

static int
cdrom_ioctl_open_abort(cdrom_t *dev)
{
    cdrom_ioctl_close(dev);
    dev->ops           = NULL;
    dev->image_path[0] = 0;
    return 1;
}

int
cdrom_ioctl_open(cdrom_t *dev, const char *drv)
{
    const char *actual_drv = &(drv[8]);

    /* Make sure to not STRCPY if the two are pointing
   at the same place. */
    if (drv != dev->image_path)
        strcpy(dev->image_path, drv);

    /* Open the image. */
    if (strstr(drv, "ioctl://") != drv)
        return cdrom_ioctl_open_abort(dev);
    cdrom_ioctl_log("actual_drv = %s\n", actual_drv);
    int i = plat_cdrom_set_drive(actual_drv);
    if (!i)
        return cdrom_ioctl_open_abort(dev);

    /* All good, reset state. */
    dev->cd_status      = CD_STATUS_STOPPED;
    dev->is_dir         = 0;
    dev->seek_pos       = 0;
    dev->cd_buflen      = 0;
    dev->cdrom_capacity = ioctl_get_capacity(dev);
    cdrom_ioctl_log("CD-ROM capacity: %i sectors (%" PRIi64 " bytes)\n",
                    dev->cdrom_capacity, ((uint64_t) dev->cdrom_capacity) << 11ULL);

    /* Attach this handler to the drive. */
    dev->ops = &cdrom_ioctl_ops;

    return 0;
}

void
cdrom_ioctl_close(cdrom_t *dev)
{
    cdrom_ioctl_log("CDROM: ioctl_close(%s)\n", dev->image_path);

    if (dev && dev->ops && dev->ops->exit)
        dev->ops->exit(dev);
}

