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
cdrom_ioctl_get_tracks(UNUSED(cdrom_t *dev), int *first, int *last)
{
    TMSF      tmsf;

    plat_cdrom_get_audio_tracks(first, last, &tmsf);
}

static void
cdrom_ioctl_get_track_info(UNUSED(cdrom_t *dev), uint32_t track, int end, track_info_t *ti)
{
    TMSF      tmsf;

    plat_cdrom_get_audio_track_info(end, track, &ti->number, &tmsf, &ti->attr);

    ti->m = tmsf.min;
    ti->s = tmsf.sec;
    ti->f = tmsf.fr;
}

static void
cdrom_ioctl_get_subchannel(UNUSED(cdrom_t *dev), uint32_t lba, subchannel_t *subc)
{
    TMSF      rel_pos;
    TMSF      abs_pos;

    plat_cdrom_get_audio_sub(lba, &subc->attr, &subc->track, &subc->index,
                      &rel_pos, &abs_pos);

    subc->abs_m = abs_pos.min;
    subc->abs_s = abs_pos.sec;
    subc->abs_f = abs_pos.fr;

    subc->rel_m = rel_pos.min;
    subc->rel_s = rel_pos.sec;
    subc->rel_f = rel_pos.fr;
}

static int
cdrom_ioctl_is_track_audio(uint32_t pos, int ismsf)
{
    uint8_t   attr;
    int       m;
    int       s;
    int       f;
    int       track;

    if (ismsf) {
        m   = (pos >> 16) & 0xff;
        s   = (pos >> 8) & 0xff;
        f   = pos & 0xff;
        pos = MSFtoLBA(m, s, f) - 150;
    }

    /* GetTrack requires LBA. */
    return plat_cdrom_get_audio_track(pos);
}

static int
cdrom_ioctl_sector_size(UNUSED(cdrom_t *dev), UNUSED(uint32_t lba))
{
    return plat_get_sector_size();
}

static int
cdrom_ioctl_read_sector(UNUSED(cdrom_t *dev), int type, uint8_t *b, uint32_t lba)
{
    switch (type) {
        case CD_READ_DATA:
            return plat_cdrom_read_sector(b, 0, lba);
        case CD_READ_AUDIO:
            return plat_cdrom_read_sector(b, 1, lba);
        case CD_READ_RAW:
            if (plat_get_sector_size() == 2352)
                return plat_cdrom_read_sector(b, 1, lba);
            else
                return plat_cdrom_read_sector(b, 0, lba);
            break;
        default:
            cdrom_ioctl_log("cdrom_ioctl_read_sector(): Unknown CD read type.\n");
            return 0;
    }
}

static int
cdrom_ioctl_track_type(UNUSED(cdrom_t *dev), uint32_t lba)
{
    if (cdrom_ioctl_is_track_audio(lba, 0))
        return CD_TRACK_AUDIO;

    return 0;
}

static void
cdrom_ioctl_exit(cdrom_t *dev)
{
    dev->cd_status = CD_STATUS_EMPTY;

    plat_cdrom_exit();

    dev->ops = NULL;
}

static const cdrom_ops_t cdrom_ioctl_ops = {
    cdrom_ioctl_get_tracks,
    cdrom_ioctl_get_track_info,
    cdrom_ioctl_get_subchannel,
    NULL,
    cdrom_ioctl_sector_size,
    cdrom_ioctl_read_sector,
    cdrom_ioctl_track_type,
    cdrom_ioctl_exit
};

void
cdrom_ioctl_eject(void)
{
    plat_cdrom_eject();
}

void
cdrom_ioctl_load(void)
{
    plat_cdrom_load();
}

static int
cdrom_ioctl_open_abort(cdrom_t *dev)
{
    if (dev && dev->ops && dev->ops->exit)
        dev->ops->exit(dev);

    dev->ops           = NULL;
    dev->host_drive    = 0;
    dev->ioctl_path[0] = 0;
    return 1;
}

int
cdrom_ioctl_open(cdrom_t *dev, const char *path)
{
    /* Open the drive. */
    if (plat_cdrom_open())
        return cdrom_ioctl_open_abort(dev);

    /* Make sure to not STRCPY if the two are pointing
       at the same place. */
    if (path != dev->ioctl_path)
        strcpy(dev->ioctl_path, path);

    /* All good, reset state. */
    dev->cd_status      = CD_STATUS_STOPPED;
    dev->is_dir         = 0;
    dev->seek_pos       = 0;
    dev->cd_buflen      = 0;
    plat_cdrom_reset();
    dev->cdrom_capacity = plat_cdrom_get_capacity();
    pclog("CD-ROM capacity: %i sectors (%" PRIi64 " bytes)\n", dev->cdrom_capacity, ((uint64_t) dev->cdrom_capacity) << 11ULL);

    /* Attach this handler to the drive. */
    dev->ops = &cdrom_ioctl_ops;

    return 0;
}
