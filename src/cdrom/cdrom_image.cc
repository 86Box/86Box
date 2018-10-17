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
 * Version:	@(#)cdrom_image.cc	1.0.3	2018/10/17
 *
 * Author:	RichardG867,
 *		Miran Grca, <mgrca8@gmail.com>
 *		bit,
 *
 *		Copyright 2015-2018 Richardg867.
 *		Copyright 2015-2018 Miran Grca.
 *		Copyright 2017,2018 bit.
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


#define CD_STATUS_EMPTY		0
#define CD_STATUS_DATA_ONLY	1
#define CD_STATUS_PLAYING	2
#define CD_STATUS_PAUSED	3
#define CD_STATUS_STOPPED	4


/* The addresses sent from the guest are absolute, ie. a LBA of 0 corresponds to a MSF of 00:00:00. Otherwise, the counter displayed by the guest is wrong:
   there is a seeming 2 seconds in which audio plays but counter does not move, while a data track before audio jumps to 2 seconds before the actual start
   of the audio while audio still plays. With an absolute conversion, the counter is fine. */
#define MSFtoLBA(m,s,f)		((((m*60)+s)*75)+f)


#pragma pack(push,1)
typedef struct {
    uint8_t user_data[2048],
	    ecc[288];
} m1_data_t;

typedef struct {
    uint8_t sub_header[8],
    user_data[2328];
} m2_data_t;

typedef union {
    m1_data_t m1_data;
    m2_data_t m2_data;
    uint8_t raw_data[2336];
} sector_data_t;

typedef struct {
    uint8_t sync[12];
    uint8_t header[4];
    sector_data_t data;
} sector_raw_data_t;

typedef union {
    sector_raw_data_t sector_data;
    uint8_t raw_data[2352];
} sector_t;

typedef struct {
    sector_t sector;
    uint8_t c2[296];
    uint8_t subchannel_raw[96];
    uint8_t subchannel_q[16];
    uint8_t subchannel_rw[96];
} cdrom_sector_t;

typedef union {
    cdrom_sector_t cdrom_sector;
    uint8_t buffer[2856];
} sector_buffer_t;
#pragma pack(pop)


enum {
    CD_STOPPED = 0,
    CD_PLAYING,
    CD_PAUSED
};


static int		cdrom_sector_size;
static uint8_t		raw_buffer[2448];
static uint8_t		extra_buffer[296];


static int
audio_callback(cdrom_t *dev, int16_t *output, int len)
{
    CDROM_Interface_Image *img = (CDROM_Interface_Image *)dev->image;
    int ret = 1;

    if (!dev->sound_on || (dev->cd_state != CD_PLAYING) || dev->img_is_iso) {
	cdrom_image_log("image_audio_callback(i): Not playing\n", dev->id);
	if (dev->cd_state == CD_PLAYING)
		dev->seek_pos += (len >> 11);
	memset(output, 0, len * 2);
	return 0;
    }

    while (dev->cd_buflen < len) {
	if (dev->seek_pos < dev->cd_end) {
		if (!img->ReadSector((uint8_t *)&dev->cd_buffer[dev->cd_buflen],
				     true, dev->seek_pos)) {
			memset(&dev->cd_buffer[dev->cd_buflen],
			       0x00, (BUF_SIZE - dev->cd_buflen) * 2);
			dev->cd_state = CD_STOPPED;
			dev->cd_buflen = len;
			ret = 0;
		} else {
			dev->seek_pos++;
			dev->cd_buflen += (RAW_SECTOR_SIZE / 2);
			ret = 1;
		}
	} else {
		memset(&dev->cd_buffer[dev->cd_buflen],
		       0x00, (BUF_SIZE - dev->cd_buflen) * 2);
		dev->cd_state = CD_STOPPED;
		dev->cd_buflen = len;
		ret = 0;
	}
    }

    memcpy(output, dev->cd_buffer, len * 2);
    memmove(dev->cd_buffer, &dev->cd_buffer[len], (BUF_SIZE - len) * 2);
    dev->cd_buflen -= len;

    return ret;
}


static void
audio_stop(cdrom_t *dev)		/* audio_stop */
{
    dev->cd_state = CD_STOPPED;
}


static uint8_t
audio_play(cdrom_t *dev, uint32_t pos, uint32_t len, int ismsf)
{
    CDROM_Interface_Image *img = (CDROM_Interface_Image *)dev->image;
    int number;
    uint8_t attr;
    TMSF tmsf;
    int m = 0, s = 0, f = 0;

    if (!img) return 0;

    img->GetAudioTrackInfo(img->GetTrack(pos), number, tmsf, attr);
    if (attr == DATA_TRACK) {
	cdrom_image_log("Can't play data track\n");
	dev->seek_pos = 0;
	dev->cd_state = CD_STOPPED;
	return 0;
    }

    cdrom_image_log("Play audio - %08X %08X %i\n", pos, len, ismsf);
    if (ismsf == 2) {
	img->GetAudioTrackInfo(pos, number, tmsf, attr);
	pos = MSFtoLBA(tmsf.min, tmsf.sec, tmsf.fr) - 150;
	img->GetAudioTrackInfo(len, number, tmsf, attr);
	len = MSFtoLBA(tmsf.min, tmsf.sec, tmsf.fr) - 150;
    } else if (ismsf == 1) {
	m = (pos >> 16) & 0xff;
	s = (pos >> 8) & 0xff;
	f = pos & 0xff;

	if (pos == 0xffffff) {
		cdrom_image_log("Playing from current position (MSF)\n");
		pos = dev->seek_pos;
	} else
		pos = MSFtoLBA(m, s, f) - 150;

	m = (len >> 16) & 0xff;
	s = (len >> 8) & 0xff;
	f = len & 0xff;
	len = MSFtoLBA(m, s, f) - 150;

	cdrom_image_log("MSF - pos = %08X len = %08X\n", pos, len);
    } else if (ismsf == 0) {
	if (pos == 0xffffffff) {
		cdrom_image_log("Playing from current position\n");
		pos = dev->seek_pos;
	}
	len += pos;
    }

    dev->seek_pos = pos;
    dev->cd_end = len;
    dev->cd_state = CD_PLAYING;
    dev->cd_buflen = 0;

    return 1;
}


static void
audio_pause(cdrom_t *dev)
{
    CDROM_Interface_Image *img = (CDROM_Interface_Image *)dev->image;

    if (!img || dev->img_is_iso) return;

    if (dev->cd_state == CD_PLAYING)
	dev->cd_state = CD_PAUSED;
}


static void
audio_resume(cdrom_t *dev)
{
    CDROM_Interface_Image *img = (CDROM_Interface_Image *)dev->image;

    if (!img || dev->img_is_iso) return;

    if (dev->cd_state == CD_PAUSED)
	dev->cd_state = CD_PLAYING;
}


static int
image_ready(cdrom_t *dev)
{
    CDROM_Interface_Image *img = (CDROM_Interface_Image *)dev->image;

    if (!img || (wcslen(dev->image_path) == 0))
	return 0;

    return 1;
}


static int
image_get_last_block(cdrom_t *dev)
{
    CDROM_Interface_Image *img = (CDROM_Interface_Image *)dev->image;
    int first_track, last_track;
    int number, c;
    unsigned char attr;
    TMSF tmsf;
    uint32_t lb = 0;
    uint32_t address;

    if (!img) return 0;

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
image_medium_changed(UNUSED(cdrom_t *dev))
{
    /* There is no way to change the medium within an already mounted image. */
    return 0;
}


static uint8_t
image_getcurrentsubchannel(cdrom_t *dev, uint8_t *b, int msf)
{
    CDROM_Interface_Image *img = (CDROM_Interface_Image *)dev->image;
    uint8_t attr, track, index, ret;
    TMSF relPos, absPos;
    uint32_t cdpos;
    int pos = 0;

    cdpos = dev->seek_pos;

    if (!img) return 0;

    img->GetAudioSub(cdpos, attr, track, index, relPos, absPos);

    if (dev->img_is_iso)
	ret = 0x15;
    else {
	if (dev->cd_state == CD_PLAYING)
		ret = 0x11;
	else if (dev->cd_state == CD_PAUSED)
		ret = 0x12;
	else
		ret = 0x13;
    }

    b[pos++] = attr;
    b[pos++] = track;
    b[pos++] = index;

    if (msf) {
	b[pos + 3] = (uint8_t)absPos.fr;
	b[pos + 2] = (uint8_t)absPos.sec;
	b[pos + 1] = (uint8_t)absPos.min;
	b[pos]     = 0;
	pos += 4;
	b[pos + 3] = (uint8_t)relPos.fr;
	b[pos + 2] = (uint8_t)relPos.sec;
	b[pos + 1] = (uint8_t)relPos.min;
	b[pos]     = 0;
	pos += 4;
    } else {
	uint32_t dat = MSFtoLBA(absPos.min, absPos.sec, absPos.fr) - 150;
	b[pos++] = (dat >> 24) & 0xff;
	b[pos++] = (dat >> 16) & 0xff;
	b[pos++] = (dat >> 8) & 0xff;
	b[pos++] = dat & 0xff;
	dat = MSFtoLBA(relPos.min, relPos.sec, relPos.fr);
	b[pos++] = (dat >> 24) & 0xff;
	b[pos++] = (dat >> 16) & 0xff;
	b[pos++] = (dat >> 8) & 0xff;
	b[pos++] = dat & 0xff;
    }

    return ret;
}


static int
image_is_track_audio(cdrom_t *dev, uint32_t pos, int ismsf)
{
    CDROM_Interface_Image *img = (CDROM_Interface_Image *)dev->image;
    uint8_t attr;
    TMSF tmsf;
    int m, s, f;
    int number;

    if (!img || dev->img_is_iso) return 0;

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
is_legal(uint8_t id, int type, int flags, int audio, int mode2)
{
    if (!(flags & 0x70)) {		/* 0x00/0x08/0x80/0x88 are illegal modes */
	cdrom_image_log("CD-ROM %i: [Any Mode] 0x00/0x08/0x80/0x88 are illegal modes\n", id);
	return 0;
    }

    if ((type != 1) && !audio) {
	if (!(flags & 0x70)) {		/* 0x00/0x08/0x80/0x88 are illegal modes */
		cdrom_image_log("CD-ROM %i: [Any Data Mode] 0x00/0x08/0x80/0x88 are illegal modes\n", id);
		return 0;
	}

	if ((flags & 0x06) == 0x06) {
		cdrom_image_log("CD-ROM %i: [Any Data Mode] Invalid error flags\n", id);
		return 0;
	}

	if (((flags & 0x700) == 0x300) || ((flags & 0x700) > 0x400)) {
		cdrom_image_log("CD-ROM %i: [Any Data Mode] Invalid subchannel data flags (%02X)\n", id, flags & 0x700);
		return 0;
	}

	if ((flags & 0x18) == 0x08) {		/* EDC/ECC without user data is an illegal mode */
		cdrom_image_log("CD-ROM %i: [Any Data Mode] EDC/ECC without user data is an illegal mode\n", id);
		return 0;
	}

	if (((flags & 0xf0) == 0x90) || ((flags & 0xf0) == 0xc0)) {		/* 0x90/0x98/0xC0/0xC8 are illegal modes */
		cdrom_image_log("CD-ROM %i: [Any Data Mode] 0x90/0x98/0xC0/0xC8 are illegal modes\n", id);
		return 0;
	}

	if (((type > 3) && (type != 8)) || (mode2 && (mode2 & 0x03))) {
		if ((flags & 0xf0) == 0x30) {		/* 0x30/0x38 are illegal modes */
			cdrom_image_log("CD-ROM %i: [Any XA Mode 2] 0x30/0x38 are illegal modes\n", id);
			return 0;
		}
		if (((flags & 0xf0) == 0xb0) || ((flags & 0xf0) == 0xd0)) {	/* 0xBx and 0xDx are illegal modes */
			cdrom_image_log("CD-ROM %i: [Any XA Mode 2] 0xBx and 0xDx are illegal modes\n", id);
			return 0;
		}
	}
    }

    return 1;
}


static void
read_sector_to_buffer(cdrom_t *dev, uint8_t *rbuf, uint32_t msf, uint32_t lba, int mode2, int len)
{
    CDROM_Interface_Image *img = (CDROM_Interface_Image *)dev->image;
    uint8_t *bb = rbuf;

    img->ReadSector(rbuf + 16, false, lba);

    /* Sync bytes */
    bb[0] = 0;
    memset(bb + 1, 0xff, 10);
    bb[11] = 0;
    bb += 12;

    /* Sector header */
    bb[0] = (msf >> 16) & 0xff;
    bb[1] = (msf >> 8) & 0xff;
    bb[2] = msf & 0xff;

    bb[3] = 1; /* mode 1 data */
    bb += mode2 ? 12 : 4;
    bb += len;
    if (mode2 && ((mode2 & 0x03) == 1))
	memset(bb, 0, 280);
    else if (!mode2)
	memset(bb, 0, 288);
}


static void
read_audio(cdrom_t *dev, uint32_t lba, uint8_t *b)
{
    CDROM_Interface_Image *img = (CDROM_Interface_Image *)dev->image;

    if (img->GetSectorSize(lba) == 2352)
	img->ReadSector(raw_buffer, true, lba);
    else
	img->ReadSectorSub(raw_buffer, lba);

    memcpy(b, raw_buffer, 2352);

    cdrom_sector_size = 2352;
}


static void
read_mode1(cdrom_t *dev, int cdrom_sector_flags, uint32_t lba, uint32_t msf, int mode2, uint8_t *b)
{
    CDROM_Interface_Image *img = (CDROM_Interface_Image *)dev->image;

    if ((dev->img_is_iso) || (img->GetSectorSize(lba) == 2048))
	read_sector_to_buffer(dev, raw_buffer, msf, lba, mode2, 2048);
    else if (img->GetSectorSize(lba) == 2352)
	img->ReadSector(raw_buffer, true, lba);
    else
	img->ReadSectorSub(raw_buffer, lba);

    cdrom_sector_size = 0;

    if (cdrom_sector_flags & 0x80) {	/* Sync */
	cdrom_image_log("CD-ROM %i: [Mode 1] Sync\n", dev->id);
	memcpy(b, raw_buffer, 12);
	cdrom_sector_size += 12;
	b += 12;
    }

    if (cdrom_sector_flags & 0x20) {	/* Header */
	cdrom_image_log("CD-ROM %i: [Mode 1] Header\n", dev->id);
	memcpy(b, raw_buffer + 12, 4);
	cdrom_sector_size += 4;
	b += 4;
    }

    if (cdrom_sector_flags & 0x40) {	/* Sub-header */
	if (!(cdrom_sector_flags & 0x10)) {		/* No user data */
		cdrom_image_log("CD-ROM %i: [Mode 1] Sub-header\n", dev->id);
		memcpy(b, raw_buffer + 16, 8);
		cdrom_sector_size += 8;
		b += 8;
	}
    }

    if (cdrom_sector_flags & 0x10) {	/* User data */
	cdrom_image_log("CD-ROM %i: [Mode 1] User data\n", dev->id);
	memcpy(b, raw_buffer + 16, 2048);
	cdrom_sector_size += 2048;
	b += 2048;
    }

    if (cdrom_sector_flags & 0x08) {	/* EDC/ECC */
	cdrom_image_log("CD-ROM %i: [Mode 1] EDC/ECC\n", dev->id);
	memcpy(b, raw_buffer + 2064, 288);
	cdrom_sector_size += 288;
	b += 288;
    }
}


static void
read_mode2_non_xa(cdrom_t *dev, int cdrom_sector_flags, uint32_t lba, uint32_t msf, int mode2, uint8_t *b)
{
    CDROM_Interface_Image *img = (CDROM_Interface_Image *)dev->image;

    if ((dev->img_is_iso) || (img->GetSectorSize(lba) == 2336))
	read_sector_to_buffer(dev, raw_buffer, msf, lba, mode2, 2336);
    else if (img->GetSectorSize(lba) == 2352)
	img->ReadSector(raw_buffer, true, lba);
    else
	img->ReadSectorSub(raw_buffer, lba);

    cdrom_sector_size = 0;

    if (cdrom_sector_flags & 0x80) {	/* Sync */
	cdrom_image_log("CD-ROM %i: [Mode 2 Formless] Sync\n", dev->id);
	memcpy(b, raw_buffer, 12);
	cdrom_sector_size += 12;
	b += 12;
    }

    if (cdrom_sector_flags & 0x20) {	/* Header */
	cdrom_image_log("CD-ROM %i: [Mode 2 Formless] Header\n", dev->id);
	memcpy(b, raw_buffer + 12, 4);
	cdrom_sector_size += 4;
	b += 4;
    }

    /* Mode 1 sector, expected type is 1 type. */
    if (cdrom_sector_flags & 0x40) {	/* Sub-header */
	cdrom_image_log("CD-ROM %i: [Mode 2 Formless] Sub-header\n", dev->id);
	memcpy(b, raw_buffer + 16, 8);
	cdrom_sector_size += 8;
	b += 8;
    }

    if (cdrom_sector_flags & 0x10) {	/* User data */
	cdrom_image_log("CD-ROM %i: [Mode 2 Formless] User data\n", dev->id);
	memcpy(b, raw_buffer + 24, 2336);
	cdrom_sector_size += 2336;
	b += 2336;
    }
}


static void
read_mode2_xa_form1(cdrom_t *dev, int cdrom_sector_flags, uint32_t lba, uint32_t msf, int mode2, uint8_t *b)
{
    CDROM_Interface_Image *img = (CDROM_Interface_Image *)dev->image;

    if ((dev->img_is_iso) || (img->GetSectorSize(lba) == 2048))
	read_sector_to_buffer(dev, raw_buffer, msf, lba, mode2, 2048);
    else if (img->GetSectorSize(lba) == 2352)
	img->ReadSector(raw_buffer, true, lba);
    else
	img->ReadSectorSub(raw_buffer, lba);

    cdrom_sector_size = 0;

    if (cdrom_sector_flags & 0x80) {	/* Sync */
	cdrom_image_log("CD-ROM %i: [XA Mode 2 Form 1] Sync\n", dev->id);
	memcpy(b, raw_buffer, 12);
	cdrom_sector_size += 12;
	b += 12;
    }

    if (cdrom_sector_flags & 0x20) {	/* Header */
	cdrom_image_log("CD-ROM %i: [XA Mode 2 Form 1] Header\n", dev->id);
	memcpy(b, raw_buffer + 12, 4);
	cdrom_sector_size += 4;
	b += 4;
    }

    if (cdrom_sector_flags & 0x40) {	/* Sub-header */
	cdrom_image_log("CD-ROM %i: [XA Mode 2 Form 1] Sub-header\n", dev->id);
	memcpy(b, raw_buffer + 16, 8);
	cdrom_sector_size += 8;
	b += 8;
    }

    if (cdrom_sector_flags & 0x10) {	/* User data */
	cdrom_image_log("CD-ROM %i: [XA Mode 2 Form 1] User data\n", dev->id);
	memcpy(b, raw_buffer + 24, 2048);
	cdrom_sector_size += 2048;
	b += 2048;
    }

    if (cdrom_sector_flags & 0x08) {	/* EDC/ECC */
	cdrom_image_log("CD-ROM %i: [XA Mode 2 Form 1] EDC/ECC\n", dev->id);
	memcpy(b, raw_buffer + 2072, 280);
	cdrom_sector_size += 280;
	b += 280;
    }
}


static void
read_mode2_xa_form2(cdrom_t *dev, int cdrom_sector_flags, uint32_t lba, uint32_t msf, int mode2, uint8_t *b)
{
    CDROM_Interface_Image *img = (CDROM_Interface_Image *)dev->image;

    if ((dev->img_is_iso) || (img->GetSectorSize(lba) == 2324))
	read_sector_to_buffer(dev, raw_buffer, msf, lba, mode2, 2324);
    else if (img->GetSectorSize(lba) == 2352)
	img->ReadSector(raw_buffer, true, lba);
    else
	img->ReadSectorSub(raw_buffer, lba);

    cdrom_sector_size = 0;

    if (cdrom_sector_flags & 0x80) {	/* Sync */
	cdrom_image_log("CD-ROM %i: [XA Mode 2 Form 2] Sync\n", dev->id);
	memcpy(b, raw_buffer, 12);
	cdrom_sector_size += 12;
	b += 12;
    }

    if (cdrom_sector_flags & 0x20) {	/* Header */
	cdrom_image_log("CD-ROM %i: [XA Mode 2 Form 2] Header\n", dev->id);
	memcpy(b, raw_buffer + 12, 4);
	cdrom_sector_size += 4;
	b += 4;
    }

    if (cdrom_sector_flags & 0x40) {	/* Sub-header */
	cdrom_image_log("CD-ROM %i: [XA Mode 2 Form 2] Sub-header\n", dev->id);
	memcpy(b, raw_buffer + 16, 8);
	cdrom_sector_size += 8;
	b += 8;
    }

    if (cdrom_sector_flags & 0x10) {	/* User data */
	cdrom_image_log("CD-ROM %i: [XA Mode 2 Form 2] User data\n", dev->id);
	memcpy(b, raw_buffer + 24, 2328);
	cdrom_sector_size += 2328;
	b += 2328;
    }
}


static int
image_readsector_raw(cdrom_t *dev, uint8_t *buffer, int sector, int ismsf, int cdrom_sector_type,
		     int cdrom_sector_flags, int *len)
{
    CDROM_Interface_Image *img = (CDROM_Interface_Image *)dev->image;
    uint8_t *b, *temp_b;
    uint32_t msf, lba;
    int audio, mode2;
    int m, s, f;

    if (!img || !dev->host_drive) return 0;

    b = temp_b = buffer;

    *len = 0;

    if (ismsf) {
	m = (sector >> 16) & 0xff;
	s = (sector >> 8) & 0xff;
	f = sector & 0xff;
	lba = MSFtoLBA(m, s, f) - 150;
	msf = sector;
    } else {
	lba = sector;
	msf = cdrom_lba_to_msf_accurate(sector);
    }

    if (dev->img_is_iso) {
	audio = 0;
	mode2 = img->IsMode2(lba) ? 1 : 0;
    } else {
	audio = image_is_track_audio(dev, sector, ismsf);
	mode2 = img->IsMode2(lba) ? 1 : 0;
    }
    mode2 <<= 2;
    mode2 |= img->GetMode2Form(lba);

    memset(raw_buffer, 0, 2448);
    memset(extra_buffer, 0, 296);

    if (!(cdrom_sector_flags & 0xf0)) {		/* 0x00 and 0x08 are illegal modes */
	cdrom_image_log("CD-ROM %i: [Mode 1] 0x00 and 0x08 are illegal modes\n", dev->id);
	return 0;
    }

    if (!is_legal(dev->id, cdrom_sector_type, cdrom_sector_flags, audio, mode2))
	return 0;

    if ((cdrom_sector_type == 3) || ((cdrom_sector_type > 4) && (cdrom_sector_type != 8))) {
	if (cdrom_sector_type == 3) {
		cdrom_image_log("CD-ROM %i: Attempting to read a Yellowbook Mode 2 data sector from an image\n", dev->id);
	}
	if (cdrom_sector_type > 4) {
		cdrom_image_log("CD-ROM %i: Attempting to read a XA Mode 2 Form 2 data sector from an image\n", dev->id);
	}
	return 0;
    } else if (cdrom_sector_type == 1) {
	if (!audio || dev->img_is_iso) {
		cdrom_image_log("CD-ROM %i: [Audio] Attempting to read an audio sector from a data image\n", dev->id);
		return 0;
	}

	read_audio(dev, lba, temp_b);
    } else if (cdrom_sector_type == 2) {
	if (audio || mode2) {
		cdrom_image_log("CD-ROM %i: [Mode 1] Attempting to read a sector of another type\n", dev->id);
		return 0;
	}

	read_mode1(dev, cdrom_sector_flags, lba, msf, mode2, temp_b);
    } else if (cdrom_sector_type == 3) {
	if (audio || !mode2 || (mode2 & 0x03)) {
		cdrom_image_log("CD-ROM %i: [Mode 2 Formless] Attempting to read a sector of another type\n", dev->id);
		return 0;
	}

	read_mode2_non_xa(dev, cdrom_sector_flags, lba, msf, mode2, temp_b);
    } else if (cdrom_sector_type == 4) {
	if (audio || !mode2 || ((mode2 & 0x03) != 1)) {
		cdrom_image_log("CD-ROM %i: [XA Mode 2 Form 1] Attempting to read a sector of another type\n", dev->id);
		return 0;
	}

	read_mode2_xa_form1(dev, cdrom_sector_flags, lba, msf, mode2, temp_b);
    } else if (cdrom_sector_type == 5) {
	if (audio || !mode2 || ((mode2 & 0x03) != 2)) {
		cdrom_image_log("CD-ROM %i: [XA Mode 2 Form 2] Attempting to read a sector of another type\n", dev->id);
		return 0;
	}

	read_mode2_xa_form2(dev, cdrom_sector_flags, lba, msf, mode2, temp_b);
    } else if (cdrom_sector_type == 8) {
	if (audio) {
		cdrom_image_log("CD-ROM %i: [Any Data] Attempting to read a data sector from an audio track\n", dev->id);
		return 0;
	}

	if (mode2 && ((mode2 & 0x03) == 1))
		read_mode2_xa_form1(dev, cdrom_sector_flags, lba, msf, mode2, temp_b);
	else if (!mode2)
		read_mode1(dev, cdrom_sector_flags, lba, msf, mode2, temp_b);
	else {
		cdrom_image_log("CD-ROM %i: [Any Data] Attempting to read a data sector whose cooked size is not 2048 bytes\n", dev->id);
		return 0;
	}
    } else {
	if (mode2) {
		if ((mode2 & 0x03) == 0x01)
			read_mode2_xa_form1(dev, cdrom_sector_flags, lba, msf, mode2, temp_b);
		else if ((mode2 & 0x03) == 0x02)
			read_mode2_xa_form2(dev, cdrom_sector_flags, lba, msf, mode2, temp_b);
		else
			read_mode2_non_xa(dev, cdrom_sector_flags, lba, msf, mode2, temp_b);
	} else {
		if (audio)
			read_audio(dev, lba, temp_b);
		else
			read_mode1(dev, cdrom_sector_flags, lba, msf, mode2, temp_b);
	}
    }

    if ((cdrom_sector_flags & 0x06) == 0x02) {
	/* Add error flags. */
	cdrom_image_log("CD-ROM %i: Error flags\n", dev->id);
	memcpy(b + cdrom_sector_size, extra_buffer, 294);
	cdrom_sector_size += 294;
    } else if ((cdrom_sector_flags & 0x06) == 0x04) {
	/* Add error flags. */
	cdrom_image_log("CD-ROM %i: Full error flags\n", dev->id);
	memcpy(b + cdrom_sector_size, extra_buffer, 296);
	cdrom_sector_size += 296;
    }

    if ((cdrom_sector_flags & 0x700) == 0x100) {
	cdrom_image_log("CD-ROM %i: Raw subchannel data\n", dev->id);
	memcpy(b + cdrom_sector_size, raw_buffer + 2352, 96);
	cdrom_sector_size += 96;
    } else if ((cdrom_sector_flags & 0x700) == 0x200) {
	cdrom_image_log("CD-ROM %i: Q subchannel data\n", dev->id);
	memcpy(b + cdrom_sector_size, raw_buffer + 2352, 16);
	cdrom_sector_size += 16;
    } else if ((cdrom_sector_flags & 0x700) == 0x400) {
	cdrom_image_log("CD-ROM %i: R/W subchannel data\n", dev->id);
	memcpy(b + cdrom_sector_size, raw_buffer + 2352, 96);
	cdrom_sector_size += 96;
    }

    *len = cdrom_sector_size;

    return 1;
}


static uint32_t
image_size(cdrom_t *dev)
{
    return dev->cdrom_capacity;
}


static int
image_readtoc(cdrom_t *dev, unsigned char *b, unsigned char starttrack, int msf, UNUSED(int maxlen), int single)
{
    CDROM_Interface_Image *img = (CDROM_Interface_Image *)dev->image;
    int number, len = 4;
    int c, d, first_track, last_track;
    uint32_t temp;
    unsigned char attr;
    TMSF tmsf;

    if (!img) return 0;

    img->GetAudioTracks(first_track, last_track, tmsf);

    b[2] = first_track;
    b[3] = last_track;

    d = 0;
    for (c = 0; c <= last_track; c++) {
	img->GetAudioTrackInfo(c+1, number, tmsf, attr);
	if (number >= starttrack) {
		d=c;
		break;
	}
    }

    if (starttrack != 0xAA) {
	img->GetAudioTrackInfo(c+1, number, tmsf, attr);
	b[2] = number;
    }

    for (c = d; c <= last_track; c++) {
	img->GetAudioTrackInfo(c+1, number, tmsf, attr);

	b[len++] = 0; /* reserved */
	b[len++] = attr;
	b[len++] = number; /* track number */
	b[len++] = 0; /* reserved */

	if (msf) {
		b[len++] = 0;
		b[len++] = tmsf.min;
		b[len++] = tmsf.sec;
		b[len++] = tmsf.fr;
	} else {
		temp = MSFtoLBA(tmsf.min, tmsf.sec, tmsf.fr) - 150;
		b[len++] = temp >> 24;
		b[len++] = temp >> 16;
		b[len++] = temp >> 8;
		b[len++] = temp;
	}

	if (single)
		break;
    }

    b[0] = (uint8_t)(((len-2) >> 8) & 0xff);
    b[1] = (uint8_t)((len-2) & 0xff);

    return len;
}


static int
image_readtoc_session(cdrom_t *dev, unsigned char *b, int msf, int maxlen)
{
    CDROM_Interface_Image *img = (CDROM_Interface_Image *)dev->image;
    int number, len = 4;
    uint8_t attr;
    uint32_t temp;
    TMSF tmsf;

    if (!img) return 0;

    img->GetAudioTrackInfo(1, number, tmsf, attr);

    if (number == 0)
	number = 1;

    b[2] = b[3] = 1;
    b[len++] = 0; /* reserved */
    b[len++] = attr;
    b[len++] = number; /* track number */
    b[len++] = 0; /* reserved */
    if (msf) {
	b[len++] = 0;
	b[len++] = tmsf.min;
	b[len++] = tmsf.sec;
	b[len++] = tmsf.fr;
    } else {
	temp = MSFtoLBA(tmsf.min, tmsf.sec, tmsf.fr) - 150;	/* Do the - 150. */
	b[len++] = temp >> 24;
	b[len++] = temp >> 16;
	b[len++] = temp >> 8;
	b[len++] = temp;
    }

    if (maxlen < len)
	return maxlen;

    return len;
}


static int
image_readtoc_raw(cdrom_t *dev, unsigned char *b, UNUSED(int maxlen))
{
    CDROM_Interface_Image *img = (CDROM_Interface_Image *)dev->image;
    int first_track, last_track;
    int number, track, len = 4;
    uint8_t attr;
    TMSF tmsf;

    if (!img) return 0;

    img->GetAudioTracks(first_track, last_track, tmsf);

    b[2] = first_track;
    b[3] = last_track;

    for (track = first_track; track <= last_track; track++) {
	img->GetAudioTrackInfo(track, number, tmsf, attr);

	b[len++] = track;
	b[len++]= attr;
	b[len++]=0;
	b[len++]=0;
	b[len++]=0;
	b[len++]=0;
	b[len++]=0;
	b[len++]=0;
	b[len++] = tmsf.min;
	b[len++] = tmsf.sec;
	b[len++] = tmsf.fr;
    }

    return len;
}


static int
image_status(cdrom_t *dev)
{
    CDROM_Interface_Image *img = (CDROM_Interface_Image *)dev->image;

    if (!img) return CD_STATUS_EMPTY;

    if (dev->img_is_iso)
	return CD_STATUS_DATA_ONLY;

    if (img->HasAudioTracks()) {
	switch(dev->cd_state) {
		case CD_PLAYING:
			return CD_STATUS_PLAYING;

		case CD_PAUSED:
			return CD_STATUS_PAUSED;

		case CD_STOPPED:
		default:
			return CD_STATUS_STOPPED;
	}
    }

    return CD_STATUS_DATA_ONLY;
}


static void
image_stop(cdrom_t *dev)
{
    CDROM_Interface_Image *img = (CDROM_Interface_Image *)dev->image;

    if (!img || dev->img_is_iso) return;

    dev->cd_state = CD_STOPPED;
}


static void
image_exit(cdrom_t *dev)
{
    CDROM_Interface_Image *img = (CDROM_Interface_Image *)dev->image;

cdrom_image_log("CDROM: image_exit(%ls)\n", dev->image_path);
    dev->cd_state = CD_STOPPED;

    if (img) {
	delete img;
	dev->image = NULL;
    }

    dev->ops = NULL;
}


/* TODO: Check for what data type a mixed CD is. */
static int
image_media_type_id(cdrom_t *dev)
{
    if (image_size(dev) > 405000)
	return 65;	/* DVD. */

    if (dev->img_is_iso)
	return 1;	/* Data CD. */

    return 3;		/* Mixed mode CD. */
}


static const cdrom_ops_t cdrom_image_ops = {
    image_ready,
    image_medium_changed,
    image_media_type_id,
    audio_callback,
    audio_stop,
    image_readtoc,
    image_readtoc_session,
    image_readtoc_raw,
    image_getcurrentsubchannel,
    image_readsector_raw,
    audio_play,
    audio_pause,
    audio_resume,
    image_size,
    image_status,
    image_stop,
    image_exit
};


int
cdrom_image_open(cdrom_t *dev, const wchar_t *fn)
{
    char temp[1024];
    CDROM_Interface_Image *img;

    wcscpy(dev->image_path, fn);

    if (! wcscasecmp(plat_get_extension((wchar_t *) fn), L"ISO"))
	dev->img_is_iso = 1;
    else
	dev->img_is_iso = 0;

    /* Create new instance of the CDROM_Image class. */
    img = new CDROM_Interface_Image();
    dev->image = img;

    /* Convert filename and open the image. */
    memset(temp, '\0', sizeof(temp));
    wcstombs(temp, fn, sizeof(temp));
    if (!img->SetDevice(temp, false)) {
	cdrom_image_close(dev);
	cdrom->ops = NULL;
	cdrom_image_log("[f] image_open(): cdrom[%i]->ops = %08X\n", dev->id, dev->ops);
	return 1;
    }

    /* All good, reset state. */
    dev->cd_state = CD_STOPPED;
    dev->seek_pos = 0;
    dev->cd_buflen = 0;
    dev->cdrom_capacity = image_get_last_block(dev) + 1;

    /* Attach this handler to the drive. */
    dev->ops = &cdrom_image_ops;

    return 0;
}


void
cdrom_image_close(cdrom_t *dev)
{
cdrom_image_log("CDROM: image_close(%ls)\n", dev->image_path);
    if (dev->ops->exit)
	dev->ops->exit(dev);
}


void
cdrom_image_reset(UNUSED(cdrom_t *dev))
{
cdrom_image_log("CDROM: image_reset(%ls)\n", dev->image_path);
    /* Nothing to do. */
}
