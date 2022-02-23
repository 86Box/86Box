/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		CD-ROM image support.
 *
 * Author:	RichardG867,
 *		Miran Grca, <mgrca8@gmail.com>
 *		bit,
 *
 *		Copyright 2015-2019 Richardg867.
 *		Copyright 2015-2019 Miran Grca.
 *		Copyright 2017-2019 bit.
 */
#define __USE_LARGEFILE64
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
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
#include <86box/plat.h>
#include <86box/scsi_device.h>
#include <86box/cdrom_image_backend.h>
#include <86box/cdrom.h>
#include <86box/cdrom_image.h>


#ifdef ENABLE_CDROM_IMAGE_LOG
int cdrom_image_do_log = ENABLE_CDROM_IMAGE_LOG;


void
cdrom_image_log(const char *fmt, ...)
{
    va_list ap;

    if (cdrom_image_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#define cdrom_image_log(fmt, ...)
#endif


/* The addresses sent from the guest are absolute, ie. a LBA of 0 corresponds to a MSF of 00:00:00. Otherwise, the counter displayed by the guest is wrong:
   there is a seeming 2 seconds in which audio plays but counter does not move, while a data track before audio jumps to 2 seconds before the actual start
   of the audio while audio still plays. With an absolute conversion, the counter is fine. */
#define MSFtoLBA(m,s,f)         ((((m * 60) + s) * 75) + f)


static void
image_get_tracks(cdrom_t *dev, int *first, int *last)
{
    cd_img_t *img = (cd_img_t *)dev->image;
    TMSF tmsf;

    cdi_get_audio_tracks(img, first, last, &tmsf);
}


static void
image_get_track_info(cdrom_t *dev, uint32_t track, int end, track_info_t *ti)
{
    cd_img_t *img = (cd_img_t *)dev->image;
    TMSF tmsf;

    cdi_get_audio_track_info(img, end, track, &ti->number, &tmsf, &ti->attr);

    ti->m = tmsf.min;
    ti->s = tmsf.sec;
    ti->f = tmsf.fr;
}


static void
image_get_subchannel(cdrom_t *dev, uint32_t lba, subchannel_t *subc)
{
    cd_img_t *img = (cd_img_t *)dev->image;
    TMSF rel_pos, abs_pos;

    cdi_get_audio_sub(img, lba, &subc->attr, &subc->track, &subc->index,
                      &rel_pos, &abs_pos);

    subc->abs_m = abs_pos.min;
    subc->abs_s = abs_pos.sec;
    subc->abs_f = abs_pos.fr;

    subc->rel_m = rel_pos.min;
    subc->rel_s = rel_pos.sec;
    subc->rel_f = rel_pos.fr;
}


static int
image_get_capacity(cdrom_t *dev)
{
    cd_img_t *img = (cd_img_t *)dev->image;
    int first_track, last_track;
    int number, c;
    unsigned char attr;
    uint32_t address = 0, lb = 0;

    if (!img)
        return 0;

    cdi_get_audio_tracks_lba(img, &first_track, &last_track, &lb);

    for (c = 0; c <= last_track; c++) {
        cdi_get_audio_track_info_lba(img, 0, c + 1, &number, &address, &attr);
        if (address > lb)
            lb = address;
    }

    return lb;
}


static int
image_is_track_audio(cdrom_t *dev, uint32_t pos, int ismsf)
{
    cd_img_t *img = (cd_img_t *)dev->image;
    uint8_t attr;
    TMSF tmsf;
    int m, s, f;
    int number, track;

    if (!img || (dev->cd_status == CD_STATUS_DATA_ONLY))
        return 0;

    if (ismsf) {
        m = (pos >> 16) & 0xff;
        s = (pos >> 8) & 0xff;
        f = pos & 0xff;
        pos = MSFtoLBA(m, s, f) - 150;
    }

    /* GetTrack requires LBA. */
    track = cdi_get_track(img, pos);
    if (track == -1)
        return 0;
    else {
        cdi_get_audio_track_info(img, 0, track, &number, &tmsf, &attr);
        return attr == AUDIO_TRACK;
    }
}


static int
image_is_track_pre(cdrom_t *dev, uint32_t lba)
{
    cd_img_t *img = (cd_img_t *)dev->image;
    int track;

    /* GetTrack requires LBA. */
    track = cdi_get_track(img, lba);

    if (track != -1)
        return cdi_get_audio_track_pre(img, track);

    return 0;
}


static int
image_sector_size(struct cdrom *dev, uint32_t lba)
{
    cd_img_t *img = (cd_img_t *)dev->image;

    return cdi_get_sector_size(img, lba);
}


static int
image_read_sector(struct cdrom *dev, int type, uint8_t *b, uint32_t lba)
{
    cd_img_t *img = (cd_img_t *)dev->image;

    switch (type) {
        case CD_READ_DATA:
            return cdi_read_sector(img, b, 0, lba);
        case CD_READ_AUDIO:
            return cdi_read_sector(img, b, 1, lba);
        case CD_READ_RAW:
            if (cdi_get_sector_size(img, lba) == 2352)
                return cdi_read_sector(img, b, 1, lba);
            else
                return cdi_read_sector_sub(img, b, lba);
        default:
            cdrom_image_log("CD-ROM %i: Unknown CD read type\n", dev->id);
            return 0;
    }
}


static int
image_track_type(cdrom_t *dev, uint32_t lba)
{
    cd_img_t *img = (cd_img_t *)dev->image;

    if (img) {
        if (image_is_track_audio(dev, lba, 0))
            return CD_TRACK_AUDIO;
        else {
            if (cdi_is_mode2(img, lba))
                return CD_TRACK_MODE2 | cdi_get_mode2_form(img, lba);
	}
    }

    return 0;
}


static void
image_exit(cdrom_t *dev)
{
    cd_img_t *img = (cd_img_t *)dev->image;

    cdrom_image_log("CDROM: image_exit(%s)\n", dev->image_path);
    dev->cd_status = CD_STATUS_EMPTY;

    if (img) {
        cdi_close(img);
        dev->image = NULL;
    }

    dev->ops = NULL;
}


static const cdrom_ops_t cdrom_image_ops = {
    image_get_tracks,
    image_get_track_info,
    image_get_subchannel,
    image_is_track_pre,
    image_sector_size,
    image_read_sector,
    image_track_type,
    image_exit
};


static int
image_open_abort(cdrom_t *dev)
{
    cdrom_image_close(dev);
    dev->ops = NULL;
    dev->host_drive = 0;
    return 1;
}


int
cdrom_image_open(cdrom_t *dev, const char *fn)
{
    cd_img_t *img;

    /* Make sure to not STRCPY if the two are pointing
       at the same place. */
    if (fn != dev->image_path)
        strcpy(dev->image_path, fn);

    /* Create new instance of the CDROM_Image class. */
    img = (cd_img_t *) malloc(sizeof(cd_img_t));

    /* This guarantees that if ops is not NULL, then
       neither is the image pointer. */
    if (!img)
        return image_open_abort(dev);

    memset(img, 0, sizeof(cd_img_t));
    dev->image = img;

    /* Open the image. */
    if (!cdi_set_device(img, fn))
        return image_open_abort(dev);

    /* All good, reset state. */
    if (! strcasecmp(plat_get_extension((char *) fn), "ISO"))
	dev->cd_status = CD_STATUS_DATA_ONLY;
    else
	dev->cd_status = CD_STATUS_STOPPED;
    dev->seek_pos = 0;
    dev->cd_buflen = 0;
    dev->cdrom_capacity = image_get_capacity(dev);
    cdrom_image_log("CD-ROM capacity: %i sectors (%" PRIi64 " bytes)\n", dev->cdrom_capacity, ((uint64_t) dev->cdrom_capacity) << 11ULL);

    /* Attach this handler to the drive. */
    dev->ops = &cdrom_image_ops;

    return 0;
}


void
cdrom_image_close(cdrom_t *dev)
{
    cdrom_image_log("CDROM: image_close(%s)\n", dev->image_path);

    if (dev && dev->ops && dev->ops->exit)
        dev->ops->exit(dev);
}
