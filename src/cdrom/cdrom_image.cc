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
 * Version:	@(#)cdrom_image.cc	1.0.10	2019/03/06
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
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../config.h"
#include "../plat.h"
#include "../scsi/scsi_device.h"
#include "cdrom_dosbox.h"
#include "cdrom.h"
#include "cdrom_image.h"


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
#define MSFtoLBA(m,s,f)		((((m*60)+s)*75)+f)


static void
image_get_tracks(cdrom_t *dev, int *first, int *last)
{
    CDROM_Interface_Image *img = (CDROM_Interface_Image *)dev->image;
    TMSF tmsf;

    img->GetAudioTracks(*first, *last, tmsf);
}


static void
image_get_track_info(cdrom_t *dev, uint32_t track, int end, track_info_t *ti)
{
    CDROM_Interface_Image *img = (CDROM_Interface_Image *)dev->image;
    TMSF tmsf;

    if (end)
	img->GetAudioTrackEndInfo(track, ti->number, tmsf, ti->attr);
    else
	img->GetAudioTrackInfo(track, ti->number, tmsf, ti->attr);

    ti->m = tmsf.min;
    ti->s = tmsf.sec;
    ti->f = tmsf.fr;
}


static void
image_get_subchannel(cdrom_t *dev, uint32_t lba, subchannel_t *subc)
{
    CDROM_Interface_Image *img = (CDROM_Interface_Image *)dev->image;
    TMSF rel_pos, abs_pos;

    img->GetAudioSub(lba, subc->attr, subc->track, subc->index,
		     rel_pos, abs_pos);

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
    CDROM_Interface_Image *img = (CDROM_Interface_Image *)dev->image;
    int first_track, last_track;
    int number, c;
    unsigned char attr;
    TMSF tmsf;
    uint32_t lb = 0;
    uint32_t address;

    if (!img)
	return 0;

    img->GetAudioTracks(first_track, last_track, tmsf);

    for (c = 0; c <= last_track; c++) {
	img->GetAudioTrackInfo(c+1, number, tmsf, attr);
	address = MSFtoLBA(tmsf.min, tmsf.sec, tmsf.fr) - 150;	/* Do the - 150 here as well. */
	if (address > lb)
		lb = address;
    }

    return lb;
}


static int
image_is_track_audio(cdrom_t *dev, uint32_t pos, int ismsf)
{
    CDROM_Interface_Image *img = (CDROM_Interface_Image *)dev->image;
    uint8_t attr;
    TMSF tmsf;
    int m, s, f;
    int number;

    if (!img || (dev->cd_status == CD_STATUS_DATA_ONLY))
	return 0;

    if (ismsf) {
	m = (pos >> 16) & 0xff;
	s = (pos >> 8) & 0xff;
	f = pos & 0xff;
	pos = MSFtoLBA(m, s, f) - 150;
    }

    /* GetTrack requires LBA. */
    img->GetAudioTrackInfo(img->GetTrack(pos), number, tmsf, attr);

    return attr == AUDIO_TRACK;
}


static int 
image_sector_size(struct cdrom *dev, uint32_t lba)
{
    CDROM_Interface_Image *img = (CDROM_Interface_Image *)dev->image;

    return img->GetSectorSize(lba);
}


static int
image_read_sector(struct cdrom *dev, int type, uint8_t *b, uint32_t lba)
{
    CDROM_Interface_Image *img = (CDROM_Interface_Image *)dev->image;

    switch (type) {
	case CD_READ_DATA:
		return img->ReadSector(b, false, lba);
	case CD_READ_AUDIO:
		return img->ReadSector(b, true, lba);
	case CD_READ_RAW:
		if (img->GetSectorSize(lba) == 2352)
			return img->ReadSector(b, true, lba);
		else
			return img->ReadSectorSub(b, lba);
	default:
		cdrom_image_log("CD-ROM %i: Unknown CD read type\n", dev->id);
		return 0;
    }
}


static int
image_track_type(cdrom_t *dev, uint32_t lba)
{
    CDROM_Interface_Image *img = (CDROM_Interface_Image *)dev->image;

    if (img) {
	if (image_is_track_audio(dev, lba, 0))
		return CD_TRACK_AUDIO;
	else {
		if (img->IsMode2(lba))
			return CD_TRACK_MODE2 | img->GetMode2Form(lba);
	}
    }

    return 0;
}


static void
image_exit(cdrom_t *dev)
{
    CDROM_Interface_Image *img = (CDROM_Interface_Image *)dev->image;

cdrom_image_log("CDROM: image_exit(%ls)\n", dev->image_path);
    dev->cd_status = CD_STATUS_EMPTY;

    if (img) {
	delete img;
	dev->image = NULL;
    }

    dev->ops = NULL;
}


static const cdrom_ops_t cdrom_image_ops = {
    image_get_tracks,
    image_get_track_info,
    image_get_subchannel,
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
cdrom_image_open(cdrom_t *dev, const wchar_t *fn)
{
    CDROM_Interface_Image *img;

    wcscpy(dev->image_path, fn);

    /* Create new instance of the CDROM_Image class. */
    img = new CDROM_Interface_Image();

    /* This guarantees that if ops is not NULL, then
       neither is the image pointer. */
    if (!img)	
	return image_open_abort(dev);

    dev->image = img;

    /* Open the image. */
    if (! img->SetDevice(fn, false))
	return image_open_abort(dev);

    /* All good, reset state. */
    if (! wcscasecmp(plat_get_extension((wchar_t *) fn), L"ISO"))
	dev->cd_status = CD_STATUS_DATA_ONLY;
    else
	dev->cd_status = CD_STATUS_STOPPED;
    dev->seek_pos = 0;
    dev->cd_buflen = 0;
    dev->cdrom_capacity = image_get_capacity(dev);
    cdrom_image_log("CD-ROM capacity: %i sectors (%i bytes)\n", dev->cdrom_capacity, dev->cdrom_capacity << 11);

    /* Attach this handler to the drive. */
    dev->ops = &cdrom_image_ops;

    return 0;
}


void
cdrom_image_close(cdrom_t *dev)
{
    cdrom_image_log("CDROM: image_close(%ls)\n", dev->image_path);

    if (dev && dev->ops && dev->ops->exit)
	dev->ops->exit(dev);
}
