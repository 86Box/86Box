/* Copyright holders: RichardG867, Tenshi, bit
   see COPYING for more details
*/
/*CD-ROM image support*/

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
#include "cdrom_dosbox.h"
#include "cdrom.h"
#include "cdrom_image.h"
#include "cdrom_null.h"

#define CD_STATUS_EMPTY		0
#define CD_STATUS_DATA_ONLY	1
#define CD_STATUS_PLAYING	2
#define CD_STATUS_PAUSED	3
#define CD_STATUS_STOPPED	4

/* The addresses sent from the guest are absolute, ie. a LBA of 0 corresponds to a MSF of 00:00:00. Otherwise, the counter displayed by the guest is wrong:
   there is a seeming 2 seconds in which audio plays but counter does not move, while a data track before audio jumps to 2 seconds before the actual start
   of the audio while audio still plays. With an absolute conversion, the counter is fine. */
#define MSFtoLBA(m,s,f)  ((((m*60)+s)*75)+f)

extern CDROM image_cdrom;

enum
{
    CD_STOPPED = 0,
    CD_PLAYING,
    CD_PAUSED
};

#ifdef ENABLE_CDROM_IMAGE_LOG
int cdrom_image_do_log = ENABLE_CDROM_IMAGE_LOG;
#endif

CDROM_Interface_Image* cdimg[CDROM_NUM] = { NULL, NULL, NULL, NULL };


void cdrom_image_log(const char *format, ...)
{
#ifdef ENABLE_CDROM_IMAGE_LOG
   if (cdrom_image_do_log)
   {
		va_list ap;
		va_start(ap, format);
		vprintf(format, ap);
		va_end(ap);
		fflush(stdout);
   }
#else
   (void)format;
#endif
}

void image_close(uint8_t id);

void image_audio_callback(uint8_t id, int16_t *output, int len)
{
	cdrom_t *dev = cdrom[id];

	return;
        if (!cdrom_drives[id].sound_on || (dev->cd_state != CD_PLAYING) || cdrom_image[id].image_is_iso)
        {
		cdrom_image_log("image_audio_callback(i): Not playing\n", id);
		if (dev->cd_state == CD_PLAYING)
		{
			dev->seek_pos += (len >> 11);
		}
                memset(output, 0, len * 2);
                return;
        }
        while (dev->cd_buflen < len)
        {
                if (dev->seek_pos < dev->cd_end)
                {
                        if (!cdimg[id]->ReadSector((unsigned char*)&dev->cd_buffer[dev->cd_buflen], true, dev->seek_pos))
                        {
                                memset(&dev->cd_buffer[dev->cd_buflen], 0, (BUF_SIZE - dev->cd_buflen) * 2);
                                dev->cd_state = CD_STOPPED;
                                dev->cd_buflen = len;
                        }
                        else
                        {
                                dev->seek_pos++;
                                dev->cd_buflen += (RAW_SECTOR_SIZE / 2);
                        }
                }
                else
                {
                        memset(&dev->cd_buffer[dev->cd_buflen], 0, (BUF_SIZE - dev->cd_buflen) * 2);
                        dev->cd_state = CD_STOPPED;
                        dev->cd_buflen = len;
                }
        }
        memcpy(output, dev->cd_buffer, len * 2);
        memmove(dev->cd_buffer, &dev->cd_buffer[len], (BUF_SIZE - len) * 2);
        dev->cd_buflen -= len;
}

void image_audio_stop(uint8_t id)
{
    cdrom_t *dev = cdrom[id];

    dev->cd_state = CD_STOPPED;
}

static void image_playaudio(uint8_t id, uint32_t pos, uint32_t len, int ismsf)
{
	cdrom_t *dev = cdrom[id];
        if (!cdimg[id]) return;
        int number;
        unsigned char attr;
        TMSF tmsf;
	int m = 0, s = 0, f = 0;
        cdimg[id]->GetAudioTrackInfo(cdimg[id]->GetTrack(pos), number, tmsf, attr);
        if (attr == DATA_TRACK)
        {
                cdrom_image_log("Can't play data track\n");
                dev->seek_pos = 0;
                dev->cd_state = CD_STOPPED;
                return;
        }
        cdrom_image_log("Play audio - %08X %08X %i\n", pos, len, ismsf);
	if (ismsf == 2)
	{
	        cdimg[id]->GetAudioTrackInfo(pos, number, tmsf, attr);
                pos = MSFtoLBA(tmsf.min, tmsf.sec, tmsf.fr) - 150;
	        cdimg[id]->GetAudioTrackInfo(len, number, tmsf, attr);
                len = MSFtoLBA(tmsf.min, tmsf.sec, tmsf.fr) - 150;
	}
        else if (ismsf == 1)
        {
		m = (pos >> 16) & 0xff;
		s = (pos >> 8) & 0xff;
		f = pos & 0xff;

		if (pos == 0xffffff)
		{
			cdrom_image_log("Playing from current position (MSF)\n");
			pos = dev->seek_pos;
		}
		else
		{
			pos = MSFtoLBA(m, s, f) - 150;
		}

		m = (len >> 16) & 0xff;
		s = (len >> 8) & 0xff;
		f = len & 0xff;
		len = MSFtoLBA(m, s, f) - 150;

                cdrom_image_log("MSF - pos = %08X len = %08X\n", pos, len);
        }
        else if (ismsf == 0)
	{
		if (pos == 0xffffffff)
		{
			cdrom_image_log("Playing from current position\n");
			pos = dev->seek_pos;
		}
                len += pos;
	}
        dev->seek_pos   = pos;
        dev->cd_end   = len;
        dev->cd_state = CD_PLAYING;
        dev->cd_buflen = 0;
}

static void image_pause(uint8_t id)
{
	cdrom_t *dev = cdrom[id];

        if (!cdimg[id] || cdrom_image[id].image_is_iso) return;
        if (dev->cd_state == CD_PLAYING)
                dev->cd_state = CD_PAUSED;
}

static void image_resume(uint8_t id)
{
	cdrom_t *dev = cdrom[id];

        if (!cdimg[id] || cdrom_image[id].image_is_iso) return;
        if (dev->cd_state == CD_PAUSED)
                dev->cd_state = CD_PLAYING;
}

static void image_stop(uint8_t id)
{
	cdrom_t *dev = cdrom[id];

        if (!cdimg[id] || cdrom_image[id].image_is_iso) return;
        dev->cd_state = CD_STOPPED;
}

static int image_ready(uint8_t id)
{
	cdrom_t *dev = cdrom[id];

        if (!cdimg[id])
                return 0;

        if (wcslen(cdrom_image[id].image_path) == 0)
                return 0;

	if (cdrom_drives[id].prev_host_drive != cdrom_drives[id].host_drive)
	{
		return 1;
	}

        return 1;
}

static int image_get_last_block(uint8_t id, UNUSED(uint8_t starttrack), UNUSED(int msf), UNUSED(int maxlen), UNUSED(int single))
{
        int c;
        uint32_t lb=0;

        if (!cdimg[id]) return 0;

        int first_track;
        int last_track;
        int number;
        unsigned char attr;
        TMSF tmsf;
        cdimg[id]->GetAudioTracks(first_track, last_track, tmsf);

        for (c = 0; c <= last_track; c++)
        {
                uint32_t address;
                cdimg[id]->GetAudioTrackInfo(c+1, number, tmsf, attr);
                address = MSFtoLBA(tmsf.min, tmsf.sec, tmsf.fr) - 150;	/* Do the - 150 here as well. */
                if (address > lb)
                        lb = address;
        }
        return lb;
}

static int image_medium_changed(uint8_t id)
{
	cdrom_t *dev = cdrom[id];

        if (!cdimg[id])
                return 0;

	if (wcslen(cdrom_image[id].image_path) == 0)
	{
		return 0;
	}

	if (cdrom_drives[id].prev_host_drive != cdrom_drives[id].host_drive)
	{
		cdrom_drives[id].prev_host_drive = cdrom_drives[id].host_drive;
		return 1;
	}

	return 0;
}

static uint8_t image_getcurrentsubchannel(uint8_t id, uint8_t *b, int msf)
{
	cdrom_t *dev = cdrom[id];
        if (!cdimg[id]) return 0;
        uint8_t ret;
        int pos=0;

        uint32_t cdpos = dev->seek_pos;
        TMSF relPos, absPos;
        unsigned char attr, track, index;
        cdimg[id]->GetAudioSub(cdpos, attr, track, index, relPos, absPos);

	if (cdrom_image[id].image_is_iso)
	{
		ret = 0x15;
	}
	else
	{
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

        if (msf)
        {
                b[pos + 3] = (uint8_t) absPos.fr;
                b[pos + 2] = (uint8_t) absPos.sec;
                b[pos + 1] = (uint8_t) absPos.min;
                b[pos]     = 0;
                pos += 4;
                b[pos + 3] = (uint8_t) relPos.fr;
                b[pos + 2] = (uint8_t) relPos.sec;
                b[pos + 1] = (uint8_t) relPos.min;
                b[pos]     = 0;
                pos += 4;
        }
        else
        {
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

static void image_eject(UNUSED(uint8_t id))
{
    return;
}

static void image_load(UNUSED(uint8_t id))
{
    return;
}

static int image_is_track_audio(uint8_t id, uint32_t pos, int ismsf)
{
	int m, s, f;
        unsigned char attr;
        TMSF tmsf;
        int number;

        if (!cdimg[id] || cdrom_image[id].image_is_iso) return 0;

	if (ismsf)
	{
		m = (pos >> 16) & 0xff;
		s = (pos >> 8) & 0xff;
		f = pos & 0xff;
		pos = MSFtoLBA(m, s, f) - 150;
	}

	/* GetTrack requires LBA. */
        cdimg[id]->GetAudioTrackInfo(cdimg[id]->GetTrack(pos), number, tmsf, attr);

        return attr == AUDIO_TRACK;
}

typedef struct __attribute__((__packed__))
{
	uint8_t user_data[2048];
	uint8_t ecc[288];
} m1_data_t;

typedef struct __attribute__((__packed__))
{
	uint8_t sub_header[8];
	uint8_t user_data[2328];
} m2_data_t;

typedef union __attribute__((__packed__))
{
	m1_data_t m1_data;
	m2_data_t m2_data;
	uint8_t raw_data[2336];
} sector_data_t;

typedef struct __attribute__((__packed__))
{
	uint8_t sync[12];
	uint8_t header[4];
	sector_data_t data;
} sector_raw_data_t;

typedef union __attribute__((__packed__))
{
	sector_raw_data_t sector_data;
	uint8_t raw_data[2352];
} sector_t;

typedef struct __attribute__((__packed__))
{
	sector_t sector;
	uint8_t c2[296];
	uint8_t subchannel_raw[96];
	uint8_t subchannel_q[16];
	uint8_t subchannel_rw[96];
} cdrom_sector_t;

typedef union __attribute__((__packed__))
{
	cdrom_sector_t cdrom_sector;
	uint8_t buffer[2856];
} sector_buffer_t;

sector_buffer_t cdrom_sector_buffer;

int cdrom_sector_size;
uint8_t raw_buffer[2448];
uint8_t extra_buffer[296];

static int is_legal(int id, int cdrom_sector_type, int cdrom_sector_flags, int audio, int mode2, int form)
{
	if (!(cdrom_sector_flags & 0x70)) {		/* 0x00/0x08/0x80/0x88 are illegal modes */
		cdrom_image_log("CD-ROM %i: [Any Mode] 0x00/0x08/0x80/0x88 are illegal modes\n", id);
		return 0;
	}

	if ((cdrom_sector_type != 1) && !audio)
	{
		if (!(cdrom_sector_flags & 0x70)) {		/* 0x00/0x08/0x80/0x88 are illegal modes */
			cdrom_image_log("CD-ROM %i: [Any Data Mode] 0x00/0x08/0x80/0x88 are illegal modes\n", id);
			return 0;
		}

		if ((cdrom_sector_flags & 0x06) == 0x06) {
			cdrom_image_log("CD-ROM %i: [Any Data Mode] Invalid error flags\n", id);
			return 0;
		}

		if (((cdrom_sector_flags & 0x700) == 0x300) || ((cdrom_sector_flags & 0x700) > 0x400)) {
			cdrom_image_log("CD-ROM %i: [Any Data Mode] Invalid subchannel data flags (%02X)\n", id, cdrom_sector_flags & 0x700);
			return 0;
		}

		if ((cdrom_sector_flags & 0x18) == 0x08) {		/* EDC/ECC without user data is an illegal mode */
			cdrom_image_log("CD-ROM %i: [Any Data Mode] EDC/ECC without user data is an illegal mode\n", id);
			return 0;
		}

		if (((cdrom_sector_flags & 0xf0) == 0x90) || ((cdrom_sector_flags & 0xf0) == 0xc0)) {		/* 0x90/0x98/0xC0/0xC8 are illegal modes */
			cdrom_image_log("CD-ROM %i: [Any Data Mode] 0x90/0x98/0xC0/0xC8 are illegal modes\n", id);
			return 0;
		}

		if (((cdrom_sector_type > 3) && (cdrom_sector_type != 8)) || (mode2 && form))
		{
			if ((cdrom_sector_flags & 0xf0) == 0x30) {		/* 0x30/0x38 are illegal modes */
				cdrom_image_log("CD-ROM %i: [Any XA Mode 2] 0x30/0x38 are illegal modes\n", id);
				return 0;
			}
			if (((cdrom_sector_flags & 0xf0) == 0xb0) || ((cdrom_sector_flags & 0xf0) == 0xd0)) {	/* 0xBx and 0xDx are illegal modes */
				cdrom_image_log("CD-ROM %i: [Any XA Mode 2] 0xBx and 0xDx are illegal modes\n", id);
				return 0;
			}
		}
	}

	return 1;
}

static void read_sector_to_buffer(uint8_t id, uint8_t *raw_buffer, uint32_t msf, uint32_t lba, int mode2, int form, int len)
{
	cdimg[id]->ReadSector(raw_buffer + 16, false, lba);

	uint8_t *bb = raw_buffer;

	/* sync bytes */
	bb[0] = 0;
	memset(bb + 1, 0xff, 10);
	bb[11] = 0;
	bb += 12;

	bb[0] = (msf >> 16) & 0xff;
	bb[1] = (msf >> 8) & 0xff;
	bb[2] = msf & 0xff;

	bb[3] = 1; /* mode 1 data */
	bb += mode2 ? 12 : 4;
	bb += len;
	if (mode2 && (form == 1))
		memset(bb, 0, 280);
	else if (!mode2)
		memset(bb, 0, 288);
}

static int image_readsector_raw(uint8_t id, uint8_t *buffer, int sector, int ismsf, int cdrom_sector_type, int cdrom_sector_flags, int *len)
{
	uint8_t *b;
	uint8_t *temp_b;
	uint32_t msf;
        uint32_t lba;
	int audio;
	int mode2;
	int m, s, f;
	int form;

	if (!cdimg[id])
		return 0;

	if (!cdrom_drives[id].host_drive)
		return 0;

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

	if (cdrom_image[id].image_is_iso) {
		audio = 0;
		mode2 = cdimg[id]->IsMode2(lba) ? 1 : 0;
	} else {
		audio = image_is_track_audio(id, sector, ismsf);
		mode2 = cdimg[id]->IsMode2(lba) ? 1 : 0;
	}
	form = cdimg[id]->GetMode2Form(lba);

	memset(raw_buffer, 0, 2448);
	memset(extra_buffer, 0, 296);

	if (!(cdrom_sector_flags & 0xf0)) {		/* 0x00 and 0x08 are illegal modes */
		cdrom_image_log("CD-ROM %i: [Mode 1] 0x00 and 0x08 are illegal modes\n", id);
		return 0;
	}

	if ((cdrom_sector_type == 3) || ((cdrom_sector_type > 4) && (cdrom_sector_type != 8))) {
		if (cdrom_sector_type == 3)
			cdrom_image_log("CD-ROM %i: Attempting to read a Yellowbook Mode 2 data sector from an image\n", id);
		if (cdrom_sector_type > 4)
			cdrom_image_log("CD-ROM %i: Attempting to read a XA Mode 2 Form 2 data sector from an image\n", id);
		return 0;
	}
	else if (cdrom_sector_type == 1) {
		if (!audio || cdrom_image[id].image_is_iso) {
			cdrom_image_log("CD-ROM %i: [Audio] Attempting to read an audio sector from a data image\n", id);
			return 0;
		}

read_audio:
		if (!is_legal(id, cdrom_sector_type, cdrom_sector_flags, audio, mode2, form))
			return 0;

		if (cdimg[id]->GetSectorSize(lba) == 2352)
			cdimg[id]->ReadSector(raw_buffer, true, lba);
		else
			cdimg[id]->ReadSectorSub(raw_buffer, lba);

		memcpy(temp_b, raw_buffer, 2352);
		cdrom_sector_size = 2352;
	} else if (cdrom_sector_type == 2) {
		if (audio || mode2) {
			cdrom_image_log("CD-ROM %i: [Mode 1] Attempting to read a sector of another type\n", id);
			return 0;
		}

read_mode1:
		if (!is_legal(id, cdrom_sector_type, cdrom_sector_flags, audio, mode2, form))
			return 0;

		if ((cdrom_image[id].image_is_iso) || (cdimg[id]->GetSectorSize(lba) == 2048))
			read_sector_to_buffer(id, raw_buffer, msf, lba, mode2, form, 2048);
		else if (cdimg[id]->GetSectorSize(lba) == 2352)
			cdimg[id]->ReadSector(raw_buffer, true, lba);
		else
			cdimg[id]->ReadSectorSub(raw_buffer, lba);

		cdrom_sector_size = 0;

		if (cdrom_sector_flags & 0x80) {	/* Sync */
			cdrom_image_log("CD-ROM %i: [Mode 1] Sync\n", id);
			memcpy(temp_b, raw_buffer, 12);
			cdrom_sector_size += 12;
			temp_b += 12;
		}

		if (cdrom_sector_flags & 0x20) {	/* Header */
			cdrom_image_log("CD-ROM %i: [Mode 1] Header\n", id);
			memcpy(temp_b, raw_buffer + 12, 4);
			cdrom_sector_size += 4;
			temp_b += 4;
		}
		
		if (cdrom_sector_flags & 0x40) {	/* Sub-header */
			if (!(cdrom_sector_flags & 0x10)) {		/* No user data */
				cdrom_image_log("CD-ROM %i: [Mode 1] Sub-header\n", id);
				memcpy(temp_b, raw_buffer + 16, 8);
				cdrom_sector_size += 8;
				temp_b += 8;
			}
		}

		if (cdrom_sector_flags & 0x10) {	/* User data */
			cdrom_image_log("CD-ROM %i: [Mode 1] User data\n", id);
			memcpy(temp_b, raw_buffer + 16, 2048);
			cdrom_sector_size += 2048;
			temp_b += 2048;
		}

		if (cdrom_sector_flags & 0x08) {	/* EDC/ECC */
			cdrom_image_log("CD-ROM %i: [Mode 1] EDC/ECC\n", id);
			memcpy(temp_b, raw_buffer + 2064, 288);
			cdrom_sector_size += 288;
			temp_b += 288;
		}
	} else if (cdrom_sector_type == 3) {
		if (audio || !mode2 || form) {
			cdrom_image_log("CD-ROM %i: [Mode 2 Formless] Attempting to read a sector of another type\n", id);
			return 0;
		}

read_mode2_non_xa:
		if (!is_legal(id, cdrom_sector_type, cdrom_sector_flags, audio, mode2, form))
			return 0;

		if ((cdrom_image[id].image_is_iso) || (cdimg[id]->GetSectorSize(lba) == 2336))
			read_sector_to_buffer(id, raw_buffer, msf, lba, mode2, form, 2336);
		else if (cdimg[id]->GetSectorSize(lba) == 2352)
			cdimg[id]->ReadSector(raw_buffer, true, lba);
		else
			cdimg[id]->ReadSectorSub(raw_buffer, lba);

		cdrom_sector_size = 0;

		if (cdrom_sector_flags & 0x80) {	/* Sync */
			cdrom_image_log("CD-ROM %i: [Mode 2 Formless] Sync\n", id);
			memcpy(temp_b, raw_buffer, 12);
			cdrom_sector_size += 12;
			temp_b += 12;
		}

		if (cdrom_sector_flags & 0x20) {	/* Header */
			cdrom_image_log("CD-ROM %i: [Mode 2 Formless] Header\n", id);
			memcpy(temp_b, raw_buffer + 12, 4);
			cdrom_sector_size += 4;
			temp_b += 4;
		}
		
		/* Mode 1 sector, expected type is 1 type. */
		if (cdrom_sector_flags & 0x40) {	/* Sub-header */
			cdrom_image_log("CD-ROM %i: [Mode 2 Formless] Sub-header\n", id);
			memcpy(temp_b, raw_buffer + 16, 8);
			cdrom_sector_size += 8;
			temp_b += 8;
		}

		if (cdrom_sector_flags & 0x10) {	/* User data */
			cdrom_image_log("CD-ROM %i: [Mode 2 Formless] User data\n", id);
			memcpy(temp_b, raw_buffer + 24, 2336);
			cdrom_sector_size += 2336;
			temp_b += 2336;
		}
	} else if (cdrom_sector_type == 4) {
		if (audio || !mode2 || (form != 1)) {
			cdrom_image_log("CD-ROM %i: [XA Mode 2 Form 1] Attempting to read a sector of another type\n", id);
			return 0;
		}

read_mode2_xa_form1:
		if (!is_legal(id, cdrom_sector_type, cdrom_sector_flags, audio, mode2, form))
			return 0;

		if ((cdrom_image[id].image_is_iso) || (cdimg[id]->GetSectorSize(lba) == 2048))
			read_sector_to_buffer(id, raw_buffer, msf, lba, mode2, form, 2048);
		else if (cdimg[id]->GetSectorSize(lba) == 2352)
			cdimg[id]->ReadSector(raw_buffer, true, lba);
		else
			cdimg[id]->ReadSectorSub(raw_buffer, lba);

		cdrom_sector_size = 0;

		if (cdrom_sector_flags & 0x80) {	/* Sync */
			cdrom_image_log("CD-ROM %i: [XA Mode 2 Form 1] Sync\n", id);
			memcpy(temp_b, raw_buffer, 12);
			cdrom_sector_size += 12;
			temp_b += 12;
		}

		if (cdrom_sector_flags & 0x20) {	/* Header */
			cdrom_image_log("CD-ROM %i: [XA Mode 2 Form 1] Header\n", id);
			memcpy(temp_b, raw_buffer + 12, 4);
			cdrom_sector_size += 4;
			temp_b += 4;
		}
		
		if (cdrom_sector_flags & 0x40) {	/* Sub-header */
			cdrom_image_log("CD-ROM %i: [XA Mode 2 Form 1] Sub-header\n", id);
			memcpy(temp_b, raw_buffer + 16, 8);
			cdrom_sector_size += 8;
			temp_b += 8;
		}

		if (cdrom_sector_flags & 0x10) {	/* User data */
			cdrom_image_log("CD-ROM %i: [XA Mode 2 Form 1] User data\n", id);
			memcpy(temp_b, raw_buffer + 24, 2048);
			cdrom_sector_size += 2048;
			temp_b += 2048;
		}

		if (cdrom_sector_flags & 0x08) {	/* EDC/ECC */
			cdrom_image_log("CD-ROM %i: [XA Mode 2 Form 1] EDC/ECC\n", id);
			memcpy(temp_b, raw_buffer + 2072, 280);
			cdrom_sector_size += 280;
			temp_b += 280;
		}
	} else if (cdrom_sector_type == 5) {
		if (audio || !mode2 || (form != 2)) {
			cdrom_image_log("CD-ROM %i: [XA Mode 2 Form 2] Attempting to read a sector of another type\n", id);
			return 0;
		}

read_mode2_xa_form2:
		if (!is_legal(id, cdrom_sector_type, cdrom_sector_flags, audio, mode2, form))
			return 0;

		if ((cdrom_image[id].image_is_iso) || (cdimg[id]->GetSectorSize(lba) == 2324))
			read_sector_to_buffer(id, raw_buffer, msf, lba, mode2, form, 2324);
		else if (cdimg[id]->GetSectorSize(lba) == 2352)
			cdimg[id]->ReadSector(raw_buffer, true, lba);
		else
			cdimg[id]->ReadSectorSub(raw_buffer, lba);

		cdrom_sector_size = 0;

		if (cdrom_sector_flags & 0x80) {	/* Sync */
			cdrom_image_log("CD-ROM %i: [XA Mode 2 Form 2] Sync\n", id);
			memcpy(temp_b, raw_buffer, 12);
			cdrom_sector_size += 12;
			temp_b += 12;
		}

		if (cdrom_sector_flags & 0x20) {	/* Header */
			cdrom_image_log("CD-ROM %i: [XA Mode 2 Form 2] Header\n", id);
			memcpy(temp_b, raw_buffer + 12, 4);
			cdrom_sector_size += 4;
			temp_b += 4;
		}
		
		if (cdrom_sector_flags & 0x40) {	/* Sub-header */
			cdrom_image_log("CD-ROM %i: [XA Mode 2 Form 2] Sub-header\n", id);
			memcpy(temp_b, raw_buffer + 16, 8);
			cdrom_sector_size += 8;
			temp_b += 8;
		}

		if (cdrom_sector_flags & 0x10) {	/* User data */
			cdrom_image_log("CD-ROM %i: [XA Mode 2 Form 2] User data\n", id);
			memcpy(temp_b, raw_buffer + 24, 2328);
			cdrom_sector_size += 2328;
			temp_b += 2328;
		}
	} else if (cdrom_sector_type == 8) {
		if (audio) {
			cdrom_image_log("CD-ROM %i: [Any Data] Attempting to read a data sector from an audio track\n", id);
			return 0;
		}

		if (mode2)
			goto read_mode2_non_xa;
		else
			goto read_mode1;
	} else {
		if (mode2)
			if (form == 1)
				goto read_mode2_xa_form1;
			else if (form == 2)
				goto read_mode2_xa_form2;
			else
				goto read_mode2_non_xa;
		else {
			if (audio)
				goto read_audio;
			else
				goto read_mode1;
		}
	}

	if ((cdrom_sector_flags & 0x06) == 0x02) {
		/* Add error flags. */
		cdrom_image_log("CD-ROM %i: Error flags\n", id);
		memcpy(b + cdrom_sector_size, extra_buffer, 294);
		cdrom_sector_size += 294;
	}
	else if ((cdrom_sector_flags & 0x06) == 0x04) {
		/* Add error flags. */
		cdrom_image_log("CD-ROM %i: Full error flags\n", id);
		memcpy(b + cdrom_sector_size, extra_buffer, 296);
		cdrom_sector_size += 296;
	}
	
	if ((cdrom_sector_flags & 0x700) == 0x100) {
		cdrom_image_log("CD-ROM %i: Raw subchannel data\n", id);
		memcpy(b + cdrom_sector_size, raw_buffer + 2352, 96);
		cdrom_sector_size += 96;
	}
	else if ((cdrom_sector_flags & 0x700) == 0x200) {
		cdrom_image_log("CD-ROM %i: Q subchannel data\n", id);
		memcpy(b + cdrom_sector_size, raw_buffer + 2352, 16);
		cdrom_sector_size += 16;
	}
	else if ((cdrom_sector_flags & 0x700) == 0x400) {
		cdrom_image_log("CD-ROM %i: R/W subchannel data\n", id);
		memcpy(b + cdrom_sector_size, raw_buffer + 2352, 96);
		cdrom_sector_size += 96;
	}

	*len = cdrom_sector_size;
	
	return 1;
}


static uint32_t image_size(uint8_t id)
{
	cdrom_t *dev = cdrom[id];

        return dev->cdrom_capacity;
}

static int image_readtoc(uint8_t id, unsigned char *b, unsigned char starttrack, int msf, int maxlen, int single)
{
        if (!cdimg[id]) return 0;
        int len=4;
        int c,d;
        uint32_t temp;

        int first_track;
        int last_track;
        int number;
        unsigned char attr;
        TMSF tmsf;

        cdimg[id]->GetAudioTracks(first_track, last_track, tmsf);

        b[2] = first_track;
        b[3] = last_track;

        d = 0;
        for (c = 0; c <= last_track; c++)
        {
                cdimg[id]->GetAudioTrackInfo(c+1, number, tmsf, attr);
                if (number >= starttrack)
                {
                        d=c;
                        break;
                }
        }

	if (starttrack != 0xAA)
	{
        	cdimg[id]->GetAudioTrackInfo(c+1, number, tmsf, attr);
        	b[2] = number;
	}

        for (c = d; c <= last_track; c++)
        {
                if ((len + 8) > maxlen)
                        break;
                cdimg[id]->GetAudioTrackInfo(c+1, number, tmsf, attr);

                b[len++] = 0; /* reserved */
                b[len++] = attr;
                b[len++] = number; /* track number */
                b[len++] = 0; /* reserved */

                if (msf)
                {
                        b[len++] = 0;
                        b[len++] = tmsf.min;
                        b[len++] = tmsf.sec;
                        b[len++] = tmsf.fr;
                }
                else
                {
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

static int image_readtoc_session(uint8_t id, unsigned char *b, int msf, int maxlen)
{
        int len = 4;

        int number;
        TMSF tmsf;
        unsigned char attr;

        if (!cdimg[id]) return 0;

        cdimg[id]->GetAudioTrackInfo(1, number, tmsf, attr);

	if (number == 0)
	{
		number = 1;
	}

        b[2] = 1;
        b[3] = 1;
        b[len++] = 0; /* reserved */
        b[len++] = attr;
        b[len++] = number; /* track number */
        b[len++] = 0; /* reserved */
        if (msf)
        {
                b[len++] = 0;
                b[len++] = tmsf.min;
                b[len++] = tmsf.sec;
                b[len++] = tmsf.fr;
        }
        else
        {
                uint32_t temp = MSFtoLBA(tmsf.min, tmsf.sec, tmsf.fr) - 150;	/* Do the - 150. */
                b[len++] = temp >> 24;
                b[len++] = temp >> 16;
                b[len++] = temp >> 8;
                b[len++] = temp;
        }

	if (maxlen < len)
	{
		return maxlen;
	}

        return len;
}

static int image_readtoc_raw(uint8_t id, unsigned char *b, int maxlen)
{
        int track;
        int len = 4;

        int first_track;
        int last_track;
        int number;
        unsigned char attr;
        TMSF tmsf;

        if (!cdimg[id]) return 0;

        cdimg[id]->GetAudioTracks(first_track, last_track, tmsf);

        b[2] = first_track;
        b[3] = last_track;

        for (track = first_track; track <= last_track; track++)
        {
                if ((len + 11) > maxlen)
                {
                        cdrom_image_log("image_readtocraw: This iteration would fill the buffer beyond the bounds, aborting...\n");
                        return len;
                }

                cdimg[id]->GetAudioTrackInfo(track, number, tmsf, attr);

                b[len++] = track;
		if (len == maxlen)  return len;
                b[len++]= attr;
		if (len == maxlen)  return len;
                b[len++]=0;
		if (len == maxlen)  return len;
                b[len++]=0;
		if (len == maxlen)  return len;
                b[len++]=0;
		if (len == maxlen)  return len;
                b[len++]=0;
		if (len == maxlen)  return len;
                b[len++]=0;
		if (len == maxlen)  return len;
                b[len++]=0;
		if (len == maxlen)  return len;
                b[len++] = tmsf.min;
		if (len == maxlen)  return len;
       	        b[len++] = tmsf.sec;
		if (len == maxlen)  return len;
                b[len++] = tmsf.fr;
		if (len == maxlen)  return len;
        }
        return len;
}

static int image_status(uint8_t id)
{
	cdrom_t *dev = cdrom[id];
        if (!cdimg[id])  return CD_STATUS_EMPTY;
	if (cdrom_image[id].image_is_iso)  return CD_STATUS_DATA_ONLY;
        if (cdimg[id]->HasAudioTracks())
        {
                switch(dev->cd_state)
                {
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

void image_reset(UNUSED(uint8_t id))
{
	return;
}

void image_close(uint8_t id)
{
	cdrom_t *dev = cdrom[id];

        dev->cd_state = CD_STOPPED;
        if (cdimg[id])
        {
                delete cdimg[id];
                cdimg[id] = NULL;
        }
        memset(cdrom_image[id].image_path, 0, 2048);
}

static char afn[1024];

int image_open(uint8_t id, wchar_t *fn)
{
	cdrom_t *dev = cdrom[id];

	wcscpy(cdrom_image[id].image_path, fn);

	if (! wcscasecmp(plat_get_extension(fn), L"ISO"))
	{
		cdrom_image[id].image_is_iso = 1;
	}
	else
	{
		cdrom_image[id].image_is_iso = 0;
	}

        cdimg[id] = new CDROM_Interface_Image();
	memset(afn, 0, sizeof(afn));
	wcstombs(afn, fn, sizeof(afn));
        if (!cdimg[id]->SetDevice(afn, false))
        {
                image_close(id);
		cdrom_set_null_handler(id);
                return 1;
        }
        dev->cd_state = CD_STOPPED;
	dev->seek_pos = 0;
        dev->cd_buflen = 0;
        dev->cdrom_capacity = image_get_last_block(id, 0, 0, 4096, 0) + 1;
	cdrom_drives[id].handler = &image_cdrom;

        return 0;
}

static void image_exit(uint8_t id)
{
    cdrom_t *dev = cdrom[id];

    dev->handler_inited = 0;
}

/* TODO: Check for what data type a mixed CD is. */
static int image_media_type_id(uint8_t id)
{
	if (!cdrom_image[id].image_is_iso)
	{
		return 3;	/* Mixed mode CD. */
	}

	if (image_size(id) <= 405000)
	{
		return 1;	/* Data CD. */
	}
	else
	{
		return 65;	/* DVD. */
	}
}

CDROM image_cdrom = 
{
        image_ready,
	image_medium_changed,
	image_media_type_id,
	image_audio_callback,
	image_audio_stop,
        image_readtoc,
        image_readtoc_session,
        image_readtoc_raw,
        image_getcurrentsubchannel,
        NULL,
        image_readsector_raw,
	image_playaudio,
        image_load,
        image_eject,
	image_pause,
	image_resume,
        image_size,
	image_status,
	image_is_track_audio,
        image_stop,
        image_exit
};
