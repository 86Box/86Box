/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
/*Win32 CD-ROM support via IOCTL*/

#define WINVER 0x0600
#include <windows.h>
#include <io.h>
#include "ntddcdrm.h"
#include "ntddscsi.h"
#include "ibm.h"
#include "cdrom.h"
#include "cdrom_ioctl.h"
#include "scsi.h"

#define MSFtoLBA(m,s,f)  ((((m*60)+s)*75)+f)

static CDROM ioctl_cdrom;

typedef struct
{
	HANDLE hIOCTL;
	CDROM_TOC toc;
} cdrom_ioctl_windows_t;

cdrom_ioctl_windows_t cdrom_ioctl_windows[CDROM_NUM];

enum
{
	CD_STOPPED = 0,
	CD_PLAYING,
	CD_PAUSED
};

int cdrom_ioctl_do_log = 0;

void cdrom_ioctl_log(const char *format, ...)
{
#ifdef ENABLE_CDROM_LOG
	if (cdrom_ioctl_do_log)
	{
		va_list ap;
		va_start(ap, format);
		vprintf(format, ap);
		va_end(ap);
		fflush(stdout);
	}
#endif
}

void ioctl_audio_callback(uint8_t id, int16_t *output, int len)
{
	RAW_READ_INFO in;
	DWORD count;

	if (!cdrom_drives[id].sound_on || (cdrom_ioctl[id].cd_state != CD_PLAYING))
	{
		if (cdrom_ioctl[id].cd_state == CD_PLAYING)
		{
			cdrom[id].seek_pos += (len >> 11);
		}
		memset(output, 0, len * 2);
		return;
	}
	while (cdrom_ioctl[id].cd_buflen < len)
	{
		if (cdrom[id].seek_pos < cdrom_ioctl[id].cd_end)
		{
			in.DiskOffset.LowPart	= (cdrom[id].seek_pos - 150) * 2048;
			in.DiskOffset.HighPart	= 0;
			in.SectorCount		= 1;
			in.TrackMode		= CDDA;		
			ioctl_open(id, 0);
			if (!DeviceIoControl(cdrom_ioctl_windows[id].hIOCTL, IOCTL_CDROM_RAW_READ, &in, sizeof(in), &(cdrom_ioctl[id].cd_buffer[cdrom_ioctl[id].cd_buflen]), 2352, &count, NULL))
			{
				memset(&(cdrom_ioctl[id].cd_buffer[cdrom_ioctl[id].cd_buflen]), 0, (BUF_SIZE - cdrom_ioctl[id].cd_buflen) * 2);
				cdrom_ioctl[id].cd_state = CD_STOPPED;
				cdrom_ioctl[id].cd_buflen = len;
			}
			else
			{
				cdrom[id].seek_pos++;
				cdrom_ioctl[id].cd_buflen += (2352 / 2);
			}
			ioctl_close(id);
		}
		else
		{
			memset(&(cdrom_ioctl[id].cd_buffer[cdrom_ioctl[id].cd_buflen]), 0, (BUF_SIZE - cdrom_ioctl[id].cd_buflen) * 2);
			cdrom_ioctl[id].cd_state = CD_STOPPED;
			cdrom_ioctl[id].cd_buflen = len;                        
		}
	}
	memcpy(output, cdrom_ioctl[id].cd_buffer, len * 2);
	memcpy(&cdrom_ioctl[id].cd_buffer[0], &(cdrom_ioctl[id].cd_buffer[len]), (BUF_SIZE - len) * 2);
	cdrom_ioctl[id].cd_buflen -= len;
}

void ioctl_audio_stop(uint8_t id)
{
	cdrom_ioctl[id].cd_state = CD_STOPPED;
}

static int get_track_nr(uint8_t id, uint32_t pos)
{
	int c;
	int track = 0;

	if (!cdrom_ioctl[id].tocvalid)
	{
		return 0;
	}

	if (cdrom_ioctl[id].last_track_pos == pos)
	{
		return cdrom_ioctl[id].last_track_nr;
	}

	for (c = cdrom_ioctl_windows[id].toc.FirstTrack; c < cdrom_ioctl_windows[id].toc.LastTrack; c++)
	{
		uint32_t track_address = cdrom_ioctl_windows[id].toc.TrackData[c].Address[3] +
						(cdrom_ioctl_windows[id].toc.TrackData[c].Address[2] * 75) +
						(cdrom_ioctl_windows[id].toc.TrackData[c].Address[1] * 75 * 60);

		if (track_address <= pos)
		{
			track = c;
		}
	}
	cdrom_ioctl[id].last_track_pos = pos;
	cdrom_ioctl[id].last_track_nr = track;

	return track;
}

static uint32_t get_track_msf(uint8_t id, uint32_t track_no)
{
	int c;

	if (!cdrom_ioctl[id].tocvalid)
	{
		return 0;
	}

	for (c = cdrom_ioctl_windows[id].toc.FirstTrack; c < cdrom_ioctl_windows[id].toc.LastTrack; c++)
	{
		if (c == track_no)
		{
			return cdrom_ioctl_windows[id].toc.TrackData[c].Address[3] + (cdrom_ioctl_windows[id].toc.TrackData[c].Address[2] << 8) + (cdrom_ioctl_windows[id].toc.TrackData[c].Address[1] << 16);
		}
	}
	return 0xffffffff;
}

static void ioctl_playaudio(uint8_t id, uint32_t pos, uint32_t len, int ismsf)
{
	int m = 0, s = 0, f = 0;
	uint32_t start_msf = 0, end_msf = 0;
	if (!cdrom_drives[id].host_drive)
	{
		return;
	}
	if (ismsf == 2)
	{
		start_msf = get_track_msf(id, pos);
		end_msf = get_track_msf(id, len);
		if (start_msf == 0xffffffff)
		{
			return;
		}
		if (end_msf == 0xffffffff)
		{
			return;
		}
		m = (start_msf >> 16) & 0xff;
		s = (start_msf >> 8) & 0xff;
		f = start_msf & 0xff;
		pos = MSFtoLBA(m, s, f);
		m = (end_msf >> 16) & 0xff;
		s = (end_msf >> 8) & 0xff;
		f = end_msf & 0xff;
		len = MSFtoLBA(m, s, f);
	}
        else if (ismsf == 1)
        {
		m = (pos >> 16) & 0xff;
		s = (pos >> 8) & 0xff;
		f = pos & 0xff;

		if (pos == 0xffffff)
		{
			cdrom_ioctl_log("Playing from current position (MSF)\n");
			pos = cdrom[id].seek_pos;
		}
		else
		{
			pos = MSFtoLBA(m, s, f);
		}

		m = (len >> 16) & 0xff;
		s = (len >> 8) & 0xff;
		f = len & 0xff;
		len = MSFtoLBA(m, s, f);
	}
	else if (ismsf == 0)
	{
		if (pos == 0xffffffff)
		{
			cdrom_ioctl_log("Playing from current position\n");
			pos = cdrom[id].seek_pos;
		}
		len += pos;
	}
	cdrom[id].seek_pos   = pos;
	cdrom_ioctl[id].cd_end   = len;
	if (cdrom[id].seek_pos < 150)
	{
		/* Adjust because the host expects a minimum adjusted LBA of 0 which is equivalent to an absolute LBA of 150. */
		cdrom[id].seek_pos = 150;
	}
	cdrom_ioctl[id].cd_state = CD_PLAYING;
}

static void ioctl_pause(uint8_t id)
{
	if (!cdrom_drives[id].host_drive)
	{
		return;
	}
	if (cdrom_ioctl[id].cd_state == CD_PLAYING)
	{
		cdrom_ioctl[id].cd_state = CD_PAUSED;
	}
}

static void ioctl_resume(uint8_t id)
{
	if (!cdrom_drives[id].host_drive)
	{
		return;
	}
	if (cdrom_ioctl[id].cd_state == CD_PAUSED)
	{
		cdrom_ioctl[id].cd_state = CD_PLAYING;
	}
}

static void ioctl_stop(uint8_t id)
{
	if (!cdrom_drives[id].host_drive)
	{
		return;
	}
	cdrom_ioctl[id].cd_state = CD_STOPPED;
}

static int ioctl_ready(uint8_t id)
{
	unsigned long size;
	int temp;
	CDROM_TOC ltoc;
	if (!cdrom_drives[id].host_drive)
	{
		return 0;
	}
	ioctl_open(id, 0);
	temp = DeviceIoControl(cdrom_ioctl_windows[id].hIOCTL, IOCTL_CDROM_READ_TOC, NULL, 0, &ltoc, sizeof(ltoc), &size, NULL);
	ioctl_close(id);
	if (!temp)
	{
		return 0;
	}
	if ((ltoc.TrackData[ltoc.LastTrack].Address[1] != cdrom_ioctl_windows[id].toc.TrackData[cdrom_ioctl_windows[id].toc.LastTrack].Address[1]) ||
	    (ltoc.TrackData[ltoc.LastTrack].Address[2] != cdrom_ioctl_windows[id].toc.TrackData[cdrom_ioctl_windows[id].toc.LastTrack].Address[2]) ||
	    (ltoc.TrackData[ltoc.LastTrack].Address[3] != cdrom_ioctl_windows[id].toc.TrackData[cdrom_ioctl_windows[id].toc.LastTrack].Address[3]) ||
	    !cdrom_ioctl[id].tocvalid || (cdrom_drives[id].host_drive != cdrom_drives[id].prev_host_drive))
	{
		cdrom_ioctl[id].cd_state = CD_STOPPED;                
		if (cdrom_drives[id].host_drive != cdrom_drives[id].prev_host_drive)
		{
			cdrom_drives[id].prev_host_drive = cdrom_drives[id].host_drive;
		}
		return 1;
	}
	return 1;
}

static int ioctl_get_last_block(uint8_t id, unsigned char starttrack, int msf, int maxlen, int single)
{
	unsigned long size;
	int c, d = 0;
	CDROM_TOC lbtoc;
	int lb = 0;
	if (!cdrom_drives[id].host_drive)
	{
		return 0;
	}
	cdrom_ioctl[id].cd_state = CD_STOPPED;
	ioctl_open(id, 0);
	DeviceIoControl(cdrom_ioctl_windows[id].hIOCTL, IOCTL_CDROM_READ_TOC, NULL, 0, &lbtoc, sizeof(lbtoc), &size, NULL);
	ioctl_close(id);
	cdrom_ioctl[id].tocvalid=1;
	for (c=d; c <= lbtoc.LastTrack; c++)
	{
		uint32_t address;
		address = MSFtoLBA(cdrom_ioctl_windows[id].toc.TrackData[c].Address[1], cdrom_ioctl_windows[id].toc.TrackData[c].Address[2], cdrom_ioctl_windows[id].toc.TrackData[c].Address[3]);
		if (address > lb)
		{
			lb = address;
		}
	}
	return lb;
}

static int ioctl_medium_changed(uint8_t id)
{
	unsigned long size;
	int temp;
	CDROM_TOC ltoc;
	if (!cdrom_drives[id].host_drive)
	{
		return 0;		/* This will be handled by the not ready handler instead. */
	}
	ioctl_open(id, 0);
	temp = DeviceIoControl(cdrom_ioctl_windows[id].hIOCTL, IOCTL_CDROM_READ_TOC, NULL, 0, &ltoc,sizeof(ltoc), &size, NULL);
	ioctl_close(id);
	if (!temp)
	{
		return 0; /* Drive empty, a not ready handler matter, not disc change. */
	}
	if (!cdrom_ioctl[id].tocvalid || (cdrom_drives[id].host_drive != cdrom_drives[id].prev_host_drive))
	{
		cdrom_ioctl[id].cd_state = CD_STOPPED;
		cdrom_ioctl_windows[id].toc = ltoc;
		cdrom_ioctl[id].tocvalid = 1;
		if (cdrom_drives[id].host_drive != cdrom_drives[id].prev_host_drive)
		{
			cdrom_drives[id].prev_host_drive = cdrom_drives[id].host_drive;
		}
		cdrom_ioctl[id].cdrom_capacity = ioctl_get_last_block(id, 0, 0, 4096, 0);
		return 1;
	}
	else
	{
		if ((ltoc.TrackData[ltoc.LastTrack].Address[1] != cdrom_ioctl_windows[id].toc.TrackData[cdrom_ioctl_windows[id].toc.LastTrack].Address[1]) ||
		    (ltoc.TrackData[ltoc.LastTrack].Address[2] != cdrom_ioctl_windows[id].toc.TrackData[cdrom_ioctl_windows[id].toc.LastTrack].Address[2]) ||
		    (ltoc.TrackData[ltoc.LastTrack].Address[3] != cdrom_ioctl_windows[id].toc.TrackData[cdrom_ioctl_windows[id].toc.LastTrack].Address[3]))
		{
			cdrom_ioctl[id].cd_state = CD_STOPPED;
			cdrom_ioctl_log("Setting TOC...\n");
			cdrom_ioctl_windows[id].toc = ltoc;
			cdrom_ioctl[id].cdrom_capacity = ioctl_get_last_block(id, 0, 0, 4096, 0);
			return 1; /* TOC mismatches. */
		}
	}
	return 0; /* None of the above, return 0. */
}

static uint8_t ioctl_getcurrentsubchannel(uint8_t id, uint8_t *b, int msf)
{
	CDROM_SUB_Q_DATA_FORMAT insub;
	SUB_Q_CHANNEL_DATA sub;
	unsigned long size;
	int pos = 0, track;
	uint32_t cdpos, track_address, dat;

	if (!cdrom_drives[id].host_drive) return 0;

	cdpos = cdrom[id].seek_pos;

	if (cdrom_ioctl[id].last_subchannel_pos == cdpos)
	{
		memcpy(&insub, cdrom_ioctl[id].sub_q_data_format, sizeof(insub));
		memcpy(&sub, cdrom_ioctl[id].sub_q_channel_data, sizeof(sub));
	}
	else
	{
		insub.Format = IOCTL_CDROM_CURRENT_POSITION;
		ioctl_open(id, 0);
		DeviceIoControl(cdrom_ioctl_windows[id].hIOCTL,IOCTL_CDROM_READ_Q_CHANNEL,&insub,sizeof(insub),&sub,sizeof(sub),&size,NULL);
		ioctl_close(id);
		memset(cdrom_ioctl[id].sub_q_data_format, 0, 16);
		memcpy(cdrom_ioctl[id].sub_q_data_format, &insub, sizeof(insub));
		memset(cdrom_ioctl[id].sub_q_channel_data, 0, 256);
		memcpy(cdrom_ioctl[id].sub_q_channel_data, &sub, sizeof(sub));
		cdrom_ioctl[id].last_subchannel_pos = cdpos;
	}        

	if (cdrom_ioctl[id].cd_state == CD_PLAYING || cdrom_ioctl[id].cd_state == CD_PAUSED)
	{
		track = get_track_nr(id, cdpos);
		track_address = cdrom_ioctl_windows[id].toc.TrackData[track].Address[3] + (cdrom_ioctl_windows[id].toc.TrackData[track].Address[2] * 75) + (cdrom_ioctl_windows[id].toc.TrackData[track].Address[1] * 75 * 60);

		cdrom_ioctl_log("cdpos = %i, track = %i, track_address = %i\n", cdpos, track, track_address);
		
		b[pos++] = sub.CurrentPosition.Control;
		b[pos++] = track + 1;
		b[pos++] = sub.CurrentPosition.IndexNumber;

		if (msf)
		{
			dat = cdpos + 150;
			b[pos + 3] = (uint8_t)(dat % 75); dat /= 75;
			b[pos + 2] = (uint8_t)(dat % 60); dat /= 60;
			b[pos + 1] = (uint8_t)dat;
			b[pos]     = 0;
			pos += 4;
			dat = cdpos - track_address;
			b[pos + 3] = (uint8_t)(dat % 75); dat /= 75;
			b[pos + 2] = (uint8_t)(dat % 60); dat /= 60;
			b[pos + 1] = (uint8_t)dat;
			b[pos]     = 0;
			pos += 4;
		}
		else
		{
			b[pos++] = (cdpos >> 24) & 0xff;
			b[pos++] = (cdpos >> 16) & 0xff;
			b[pos++] = (cdpos >> 8) & 0xff;
			b[pos++] = cdpos & 0xff;
			cdpos -= track_address;
			b[pos++] = (cdpos >> 24) & 0xff;
			b[pos++] = (cdpos >> 16) & 0xff;
			b[pos++] = (cdpos >> 8) & 0xff;
			b[pos++] = cdpos & 0xff;
		}

		if (cdrom_ioctl[id].cd_state == CD_PLAYING) return 0x11;
		return 0x12;
	}

	b[pos++]=sub.CurrentPosition.Control;
	b[pos++]=sub.CurrentPosition.TrackNumber;
	b[pos++]=sub.CurrentPosition.IndexNumber;

	cdrom_ioctl_log("cdpos = %i, track_address = %i\n", MSFtoLBA(sub.CurrentPosition.AbsoluteAddress[1], sub.CurrentPosition.AbsoluteAddress[2], sub.CurrentPosition.AbsoluteAddress[3]), MSFtoLBA(sub.CurrentPosition.TrackRelativeAddress[1], sub.CurrentPosition.TrackRelativeAddress[2], sub.CurrentPosition.TrackRelativeAddress[3]));
        
	if (msf)
	{
		int c;
		for (c = 0; c < 4; c++)
		{
			b[pos++] = sub.CurrentPosition.AbsoluteAddress[c];
		}
		for (c = 0; c < 4; c++)
		{
			b[pos++] = sub.CurrentPosition.TrackRelativeAddress[c];
		}
	}
	else
	{
		uint32_t temp = MSFtoLBA(sub.CurrentPosition.AbsoluteAddress[1], sub.CurrentPosition.AbsoluteAddress[2], sub.CurrentPosition.AbsoluteAddress[3]);
		b[pos++] = temp >> 24;
		b[pos++] = temp >> 16;
		b[pos++] = temp >> 8;
		b[pos++] = temp;
		temp = MSFtoLBA(sub.CurrentPosition.TrackRelativeAddress[1], sub.CurrentPosition.TrackRelativeAddress[2], sub.CurrentPosition.TrackRelativeAddress[3]);
		b[pos++] = temp >> 24;
		b[pos++] = temp >> 16;
		b[pos++] = temp >> 8;
		b[pos++] = temp;
	}

	return 0x13;
}

static void ioctl_eject(uint8_t id)
{
	unsigned long size;
	if (!cdrom_drives[id].host_drive)
	{
		return;
	}
	cdrom_ioctl[id].cd_state = CD_STOPPED;        
	ioctl_open(id, 0);
	DeviceIoControl(cdrom_ioctl_windows[id].hIOCTL,IOCTL_STORAGE_EJECT_MEDIA,NULL,0,NULL,0,&size,NULL);
	ioctl_close(id);
}

static void ioctl_load(uint8_t id)
{
	unsigned long size;
	if (!cdrom_drives[id].host_drive)
	{
		return;
	}
	cdrom_ioctl[id].cd_state = CD_STOPPED;        
	ioctl_open(id, 0);
	DeviceIoControl(cdrom_ioctl_windows[id].hIOCTL,IOCTL_STORAGE_LOAD_MEDIA,NULL,0,NULL,0,&size,NULL);
	ioctl_close(id);
	cdrom_ioctl[id].cdrom_capacity = ioctl_get_last_block(id, 0, 0, 4096, 0);
}

static int is_track_audio(uint8_t id, uint32_t pos)
{
	int c;
	int control = 0;

	uint32_t track_address = 0;

	if (!cdrom_ioctl[id].tocvalid)
	{
		return 0;
	}

	for (c = 0; c <= cdrom_ioctl_windows[id].toc.LastTrack; c++)
	{
		track_address = MSFtoLBA(cdrom_ioctl_windows[id].toc.TrackData[c].Address[1],cdrom_ioctl_windows[id].toc.TrackData[c].Address[2],cdrom_ioctl_windows[id].toc.TrackData[c].Address[3]);

		if (track_address <= pos)
		{
			control = cdrom_ioctl_windows[id].toc.TrackData[c].Control;
		}
	}

	if ((control & 0xd) <= 1)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

static int ioctl_is_track_audio(uint8_t id, uint32_t pos, int ismsf)
{
	int m = 0, s = 0, f = 0;

	if (ismsf)
	{
		m = (pos >> 16) & 0xff;
		s = (pos >> 8) & 0xff;
		f = pos & 0xff;
		pos = MSFtoLBA(m, s, f);
	}
	else
	{
		pos += 150;
	}
	return is_track_audio(id, pos);
}

/*								  00,   08,   10,   18,   20,   28,   30,   38 */
int flags_to_size[5][32] = { {    0,    0, 2352, 2352, 2352, 2352, 2352, 2352,		/* 00-38 (CD-DA) */
							   2352, 2352, 2352, 2352, 2352, 2352, 2352, 2352,		/* 40-78 */
							   2352, 2352, 2352, 2352, 2352, 2352, 2352, 2352,		/* 80-B8 */
							   2352, 2352, 2352, 2352, 2352, 2352, 2352, 2352 },	/* C0-F8 */
							 {    0,    0, 2048, 2336,    4, -296, 2052, 2344,		/* 00-38 (Mode 1) */
							      8, -296, 2048, 2048,   12, -296, 2052, 2052,		/* 40-78 */
							   -296, -296, -296, -296,   16, -296, 2064, 2344,		/* 80-B8 */
							   -296, -296, 2048, 2048,   24, -296, 2064, 2352 },	/* C0-F8 */
							 {    0,    0, 2336, 2336,    4, -296, 2340, 2340,		/* 00-38 (Mode 2 non-XA) */
							      8, -296, 2336, 2336,   12, -296, 2340, 2340,		/* 40-78 */
							   -296, -296, -296, -296,   16, -296, 2352, 2340,		/* 80-B8 */
							   -296, -296, 2336, 2336,   24, -296, 2352, 2352 },	/* C0-F8 */
							 {    0,    0, 2048, 2336,    4, -296, -296, -296,		/* 00-38 (Mode 2 Form 1) */
							      8, -296, 2056, 2344,   12, -296, 2060, 2340,		/* 40-78 */
							   -296, -296, -296, -296,   16, -296, -296, -296,		/* 80-B8 */
							   -296, -296, -296, -296,   24, -296, 2072, 2352 },	/* C0-F8 */
							 {    0,    0, 2328, 2328,    4, -296, -296, -296,		/* 00-38 (Mode 2 Form 2) */
							      8, -296, 2336, 2336,   12, -296, 2340, 2340,		/* 40-78 */
							   -296, -296, -296, -296,   16, -296, -296, -296,		/* 80-B8 */
							   -296, -296, -296, -296,   24, -296, 2352, 2352 }		/* C0-F8 */
						   };

static int ioctl_get_sector_data_type(uint8_t id, uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3, int ismsf);

static void cdrom_illegal_mode(uint8_t id)
{
	cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
	cdrom_asc = ASC_ILLEGAL_MODE_FOR_THIS_TRACK;
	cdrom_ascq = 0;
}

struct sptd_with_sense
{
    SCSI_PASS_THROUGH s;
	ULONG Filler;
    UCHAR sense[32];
    UCHAR data[65536];
} sptd;

static int ioctl_get_block_length(uint8_t id, const UCHAR *cdb, int number_of_blocks, int no_length_check)
{
  int sector_type = 0;
  int temp_len = 0;

  if (no_length_check)
	{
		switch (cdb[0])
		{
			case 0x25:
				/* READ CAPACITY */
				return 8;
			case 0x42:	/* READ SUBCHANNEL */
			case 0x43:	/* READ TOC */
			case 0x51:	/* READ DISC INFORMATION */
			case 0x52:	/* READ TRACK INFORMATION */
			case 0x5A:	/* MODE SENSE (10) */
				return ((uint16_t) cdb[8]) + (((uint16_t) cdb[7]) << 8);
			default:
				return 65534;
		}
	}

	switch (cdb[0])
	{
		case 0x25:
			/* READ CAPACITY */
			return 8;
		case 0x42:	/* READ SUBCHANNEL */
		case 0x43:	/* READ TOC */
		case 0x51:	/* READ DISC INFORMATION */
		case 0x52:	/* READ TRACK INFORMATION */
		case 0x5A:	/* MODE SENSE (10) */
			return ((uint16_t) cdb[8]) + (((uint16_t) cdb[7]) << 8);
		case 0x08:
		case 0x28:
		case 0xa8:
			/* READ (6), READ (10), READ (12) */
			return 2048 * number_of_blocks;
			break;
		case 0xb9:
			sector_type = (cdb[1] >> 2) & 7;
			if (sector_type == 0)
			{
				sector_type = ioctl_get_sector_data_type(id, 0, cdb[3], cdb[4], cdb[5], 1);
				if (sector_type == 0)
				{
					cdrom_illegal_mode(id);
					return -1;
				}
			}
			goto common_handler;
		case 0xbe:
			/* READ CD MSF, READ CD */
			sector_type = (cdb[1] >> 2) & 7;
			if (sector_type == 0)
			{
				sector_type = ioctl_get_sector_data_type(id, cdb[2], cdb[3], cdb[4], cdb[5], 0);
				if (sector_type == 0)
				{
					cdrom_illegal_mode(id);
					return -1;
				}
			}
common_handler:
			temp_len = flags_to_size[sector_type - 1][cdb[9] >> 3];
			if ((cdb[9] & 6) == 2)
			{
				temp_len += 294;
			}
			else if ((cdb[9] & 6) == 4)
			{
				temp_len += 296;
			}
			if ((cdb[10] & 7) == 1)
			{
				temp_len += 96;
			}
			else if ((cdb[10] & 7) == 2)
			{
				temp_len += 16;
			}
			else if ((cdb[10] & 7) == 4)
			{
				temp_len += 96;
			}
			if (temp_len <= 0)
			{
					cdrom_illegal_mode(id);
					return -1;
			}
			return temp_len * cdrom[id].requested_blocks;
			break;
		default:
			/* Other commands */
			return 65534;
			break;
	}

}

static int SCSICommand(uint8_t id, const UCHAR *cdb, UCHAR *buf, uint32_t *len, int no_length_check)
{
	DWORD ioctl_bytes;
	int ioctl_rv = 0;

	SCSISense.SenseKey = 0;
	SCSISense.Asc = 0;
	SCSISense.Ascq = 0;

	*len = 0;
	memset(&sptd, 0, sizeof(sptd));
	sptd.s.Length = sizeof(SCSI_PASS_THROUGH);
	sptd.s.CdbLength = 12;
	sptd.s.DataIn = SCSI_IOCTL_DATA_IN;
	sptd.s.TimeOutValue = 80 * 60;
	sptd.s.DataTransferLength = ioctl_get_block_length(id, cdb, cdrom_ioctl[id].actual_requested_blocks, no_length_check);
	sptd.s.SenseInfoOffset = (uintptr_t)&sptd.sense - (uintptr_t)&sptd;
	sptd.s.SenseInfoLength = 32;
	sptd.s.DataBufferOffset = (uintptr_t)&sptd.data - (uintptr_t)&sptd;

	memcpy(sptd.s.Cdb, cdb, 12);
	ioctl_rv = DeviceIoControl(cdrom_ioctl_windows[id].hIOCTL, IOCTL_SCSI_PASS_THROUGH, &sptd, sizeof(sptd), &sptd, sizeof(sptd), &ioctl_bytes, NULL);

	if (sptd.s.SenseInfoLength)
	{
		cdrom_sense_key = sptd.sense[2];
		cdrom_asc = sptd.sense[12];
		cdrom_ascq = sptd.sense[13];
	}

	cdrom_ioctl_log("Transferred length: %i (command: %02X)\n", sptd.s.DataTransferLength, cdb[0]);
	cdrom_ioctl_log("Sense length: %i (%02X %02X %02X %02X %02X)\n", sptd.s.SenseInfoLength, sptd.sense[0], sptd.sense[1], sptd.sense[2], sptd.sense[12], sptd.sense[13]);
	cdrom_ioctl_log("IOCTL bytes: %i; SCSI status: %i, status: %i, LastError: %08X\n", ioctl_bytes, sptd.s.ScsiStatus, ioctl_rv, GetLastError());
	cdrom_ioctl_log("DATA:  %02X %02X %02X %02X %02X %02X\n", sptd.data[0], sptd.data[1], sptd.data[2], sptd.data[3], sptd.data[4], sptd.data[5]);
	cdrom_ioctl_log("       %02X %02X %02X %02X %02X %02X\n", sptd.data[6], sptd.data[7], sptd.data[8], sptd.data[9], sptd.data[10], sptd.data[11]);
	cdrom_ioctl_log("       %02X %02X %02X %02X %02X %02X\n", sptd.data[12], sptd.data[13], sptd.data[14], sptd.data[15], sptd.data[16], sptd.data[17]);
	cdrom_ioctl_log("SENSE: %02X %02X %02X %02X %02X %02X\n", sptd.sense[0], sptd.sense[1], sptd.sense[2], sptd.sense[3], sptd.sense[4], sptd.sense[5]);
	cdrom_ioctl_log("       %02X %02X %02X %02X %02X %02X\n", sptd.sense[6], sptd.sense[7], sptd.sense[8], sptd.sense[9], sptd.sense[10], sptd.sense[11]);
	cdrom_ioctl_log("       %02X %02X %02X %02X %02X %02X\n", sptd.sense[12], sptd.sense[13], sptd.sense[14], sptd.sense[15], sptd.sense[16], sptd.sense[17]);
	*len = sptd.s.DataTransferLength;
	if (sptd.s.DataTransferLength != 0)
	{
		memcpy(buf, sptd.data, sptd.s.DataTransferLength);
	}

	return ioctl_rv;
}

static void ioctl_read_capacity(uint8_t id, uint8_t *b)
{
	uint32_t len = 0;

	const UCHAR cdb[] = { 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	UCHAR buf[16];

	if (!cdrom_ioctl[id].capacity_read)
	{
		ioctl_open(id, 0);

		SCSICommand(id, cdb, buf, &len, 1);
	
		memcpy(cdrom_ioctl[id].rcbuf, buf, len);
		cdrom_ioctl[id].capacity_read = 1;
	}
	else
	{
		memcpy(b, cdrom_ioctl[id].rcbuf, 16);
	}
	
	ioctl_close(id);
}

static int ioctl_media_type_id(uint8_t id)
{
	uint8_t old_sense[3] = { 0, 0, 0 };

	UCHAR msbuf[28];
	uint32_t len = 0;
	int sense = 0;
	
	const UCHAR cdb[] = { 0x5A, 0x00, 0x2A, 0, 0, 0, 0, 0, 28, 0, 0, 0 };

	old_sense[0] = cdrom_sense_key;
	old_sense[1] = cdrom_asc;
	old_sense[2] = cdrom_asc;
	
	ioctl_open(id, 0);

	SCSICommand(id, cdb, msbuf, &len, 1);

	ioctl_close(id);

	sense = cdrom_sense_key;
	cdrom_sense_key = old_sense[0];
	cdrom_asc = old_sense[1];
	cdrom_asc = old_sense[2];

	if (sense == 0)
	{
		return msbuf[2];
	}
	else
	{
		return 3;
	}
}

static uint32_t msf_to_lba32(int lba)
{
	int m = (lba >> 16) & 0xff;
	int s = (lba >> 8) & 0xff;
	int f = lba & 0xff;
	return (m * 60 * 75) + (s * 75) + f;
}

static int ioctl_get_type(uint8_t id, UCHAR *cdb, UCHAR *buf)
{
	int i = 0;
	int ioctl_rv = 0;

	uint32_t len = 0;

	for (i = 2; i <= 5; i++)
	{
		cdb[1] = i << 2;
		ioctl_rv = SCSICommand(id, cdb, buf, &len, 1);	/* Bypass length check so we don't risk calling this again and getting stuck in an endless up. */
		if (ioctl_rv)
		{
			return i;
		}
	}
	return 0;
}

static int ioctl_sector_data_type(uint8_t id, int sector, int ismsf)
{
	int ioctl_rv = 0;
	UCHAR cdb_lba[] = { 0xBE, 0, 0, 0, 0, 0, 0, 0, 1, 0x10, 0, 0 };
	UCHAR cdb_msf[] = { 0xB9, 0, 0, 0, 0, 0, 0, 0, 0, 0x10, 0, 0 };
	UCHAR buf[2352];

	cdb_lba[2] = (sector >> 24);
	cdb_lba[3] = ((sector >> 16) & 0xff);
	cdb_lba[4] = ((sector >> 8) & 0xff);
	cdb_lba[5] = (sector & 0xff);

	cdb_msf[3] = cdb_msf[6] = ((sector >> 16) & 0xff);
	cdb_msf[4] = cdb_msf[7] = ((sector >> 8) & 0xff);
	cdb_msf[5] = cdb_msf[8] = (sector & 0xff);

	ioctl_open(id, 0);

	if (ioctl_is_track_audio(id, sector, ismsf))
	{
		return 1;
	}

	if (ismsf)
	{
		ioctl_rv = ioctl_get_type(id, cdb_msf, buf);
	}
	else
	{
		ioctl_rv = ioctl_get_type(id, cdb_lba, buf);
	}

	if (ioctl_rv)
	{
		ioctl_close(id);
		return ioctl_rv;
	}

	if (ismsf)
	{
		sector = msf_to_lba32(sector);
		if (sector < 150)
		{
			ioctl_close(id);
			return 0;
		}
		sector -= 150;
		ioctl_rv = ioctl_get_type(id, (UCHAR *) cdb_lba, buf);
	}

	ioctl_close(id);
	return ioctl_rv;
}

static int ioctl_get_sector_data_type(uint8_t id, uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3, int ismsf)
{
	int sector = b3;
	sector |= ((uint32_t) b2) << 8;
	sector |= ((uint32_t) b1) << 16;
	sector |= ((uint32_t) b0) << 24;
	return ioctl_sector_data_type(id, sector, ismsf);
}

static void ioctl_validate_toc(uint8_t id)
{
	unsigned long size;
	if (!cdrom_drives[id].host_drive)
	{
		return;
	}
	cdrom_ioctl[id].cd_state = CD_STOPPED;        
	ioctl_open(id, 0);
	cdrom_ioctl_log("Validating TOC...\n");
	DeviceIoControl(cdrom_ioctl_windows[id].hIOCTL,IOCTL_CDROM_READ_TOC, NULL,0,&cdrom_ioctl_windows[id].toc,sizeof(cdrom_ioctl_windows[id].toc),&size,NULL);
	ioctl_close(id);
	cdrom_ioctl[id].tocvalid=1;
}

UCHAR buf[262144];

static int ioctl_pass_through(uint8_t id, uint8_t *in_cdb, uint8_t *b, uint32_t *len)
{
	const UCHAR cdb[12];
	
	int ret = 0;

	int block_length = 0;
	
	int temp_block_length = 0;
	int temp_pos = 0;
	
	int blocks_at_once = 0;
	int buffer_pos = 0;
	
	int transferred_blocks = 0;
	
	uint32_t temp_len = 0;
	int chunk = 0;
	
	if (in_cdb[0] == 0x43)
	{
		/* This is a read TOC, so we have to validate the TOC to make the rest of the emulator happy. */
		ioctl_validate_toc(id);
	}

	ioctl_open(id, 0);

	memcpy((void *) cdb, in_cdb, 12);
	
	temp_block_length = ioctl_get_block_length(id, cdb, cdrom[id].requested_blocks, 0);
	*len = 0;
	if (temp_block_length != -1)
	{
		if (temp_block_length > 65534)
		{
			block_length = temp_block_length / cdrom[id].requested_blocks;
			blocks_at_once = 32768 / block_length;
			cdrom_ioctl_log("CD-ROM %i: ioctl_pass_through(): Expected transfer length %i is bigger than 65534, splitting the transfer into chunks of %i blocks...\n", id, temp_block_length, blocks_at_once);

			buffer_pos = 0;
			temp_pos = cdrom[id].sector_pos;
			transferred_blocks = 0;
			temp_len = 0;

split_block_read_iterate:
			chunk = (cdrom[id].requested_blocks - transferred_blocks);
			if (chunk < blocks_at_once)
			{
				cdrom_ioctl_log("CD-ROM %i: ioctl_pass_through(): The remaining chunk (%i blocks) is less than a complete split block\n", id, chunk);
				cdrom_ioctl[id].actual_requested_blocks = chunk;
			}
			else
			{
				cdrom_ioctl_log("CD-ROM %i: ioctl_pass_through(): The remaining chunk (%i blocks) is more or equal than a complete split block\n", id, chunk);
				cdrom_ioctl[id].actual_requested_blocks = blocks_at_once;
			}
			cdrom_ioctl_log("CD-ROM %i: ioctl_pass_through(): Transferring %i blocks...\n", id, cdrom_ioctl[id].actual_requested_blocks);
			cdrom_update_cdb((uint8_t *) cdb, temp_pos, cdrom_ioctl[id].actual_requested_blocks);
			ret = SCSICommand(id, cdb, buf + buffer_pos, &temp_len, 0);
			*len += temp_len;
			transferred_blocks += cdrom_ioctl[id].actual_requested_blocks;
			if (ret && (transferred_blocks < cdrom[id].requested_blocks))
			{
				/* Return value was successful and there are still more blocks left to transfer. */
				temp_pos += cdrom_ioctl[id].actual_requested_blocks;
				buffer_pos += (cdrom_ioctl[id].actual_requested_blocks * block_length);
				goto split_block_read_iterate;
			}
			cdrom_ioctl_log("CD-ROM %i: ioctl_pass_through(): Split transfer done\n", id);
		}
		else
		{
			cdrom_ioctl_log("CD-ROM %i: ioctl_pass_through(): Expected transfer length %i is smaller than 65534, transferring all at once...\n", id, temp_block_length);
			cdrom_ioctl[id].actual_requested_blocks = cdrom[id].requested_blocks;
			ret = SCSICommand(id, cdb, buf, len, 0);
			cdrom_ioctl_log("CD-ROM %i: ioctl_pass_through(): Single transfer done\n", id);
		}
		memcpy(b, buf, *len);
	}
	else
	{
		cdrom_ioctl_log("CD-ROM %i: ioctl_pass_through(): Expected transfer length %i is -1, this indicates an illegal mode\n", id, temp_block_length);
	}

    cdrom_ioctl_log("IOCTL DATA:  %02X %02X %02X %02X %02X %02X %02X %02X\n", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
	
	ioctl_close(id);

	cdrom_ioctl_log("IOCTL Returned value: %i\n", ret);
	
	return ret;
}

static uint32_t ioctl_size(uint8_t id)
{
	uint8_t capacity_buffer[8];
	uint32_t capacity = 0;
	ioctl_read_capacity(id, capacity_buffer);
	capacity = ((uint32_t) capacity_buffer[0]) << 24;
	capacity |= ((uint32_t) capacity_buffer[1]) << 16;
	capacity |= ((uint32_t) capacity_buffer[2]) << 8;
	capacity |= (uint32_t) capacity_buffer[3];
	return capacity + 1;
}

static int ioctl_status(uint8_t id)
{
	if (!(ioctl_ready(id)) && (cdrom_drives[id].host_drive <= 0))
	{
		return CD_STATUS_EMPTY;
	}

	switch(cdrom_ioctl[id].cd_state)
	{
		case CD_PLAYING:
			return CD_STATUS_PLAYING;
		case CD_PAUSED:
			return CD_STATUS_PAUSED;
		case CD_STOPPED:
			return CD_STATUS_STOPPED;
		default:
			return CD_STATUS_EMPTY;
	}
}

void ioctl_reset(uint8_t id)
{
        CDROM_TOC ltoc;
        unsigned long size;

        if (!cdrom_drives[id].host_drive)
        {
                cdrom_ioctl[id].tocvalid = 0;
                return;
        }
        
        ioctl_open(id, 0);
        DeviceIoControl(cdrom_ioctl_windows[id].hIOCTL, IOCTL_CDROM_READ_TOC, NULL, 0, &ltoc, sizeof(ltoc), &size, NULL);
        ioctl_close(id);

        cdrom_ioctl_windows[id].toc = ltoc;
        cdrom_ioctl[id].tocvalid = 1;
}

int ioctl_open(uint8_t id, char d)
{
	if (!cdrom_ioctl[id].ioctl_inited)
	{
		sprintf(cdrom_ioctl[id].ioctl_path,"\\\\.\\%c:",d);
		cdrom_ioctl[id].tocvalid=0;
	}
	cdrom_ioctl_windows[id].hIOCTL = CreateFile(cdrom_ioctl[id].ioctl_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	cdrom_drives[id].handler = &ioctl_cdrom;
	if (!cdrom_ioctl[id].ioctl_inited)
	{
		cdrom_ioctl[id].ioctl_inited=1;
		cdrom_ioctl[id].capacity_read=0;	/* With this two lines, we read the READ CAPACITY command output from the host drive into our cache buffer. */
		ioctl_read_capacity(id, NULL);
		CloseHandle(cdrom_ioctl_windows[id].hIOCTL);
		cdrom_ioctl_windows[id].hIOCTL = NULL;
	}
	return 0;
}

void ioctl_close(uint8_t id)
{
	if (cdrom_ioctl_windows[id].hIOCTL)
	{
		CloseHandle(cdrom_ioctl_windows[id].hIOCTL);
		cdrom_ioctl_windows[id].hIOCTL = NULL;
	}
}

static void ioctl_exit(uint8_t id)
{
	ioctl_stop(id);
	cdrom_ioctl[id].ioctl_inited=0;
	cdrom_ioctl[id].tocvalid=0;
}

static CDROM ioctl_cdrom=
{
	ioctl_ready,
	ioctl_medium_changed,
	ioctl_media_type_id,
	ioctl_audio_callback,
	ioctl_audio_stop,
	NULL,
	NULL,
	NULL,
	ioctl_getcurrentsubchannel,
	ioctl_pass_through,
	NULL,
	ioctl_playaudio,
	ioctl_load,
	ioctl_eject,
	ioctl_pause,
	ioctl_resume,
	ioctl_size,
	ioctl_status,
	ioctl_is_track_audio,
	ioctl_stop,
	ioctl_exit
};
