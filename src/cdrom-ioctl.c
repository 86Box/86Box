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
#include "cdrom-ioctl.h"
#include "scsi.h"

#define MSFtoLBA(m,s,f)  ((((m*60)+s)*75)+f)

static CDROM ioctl_cdrom;

typedef struct
{
	HANDLE hIOCTL;
	CDROM_TOC toc;
} cdrom_ioctl_windows_t;

cdrom_ioctl_windows_t cdrom_ioctl_windows[CDROM_NUM];

// #define MSFtoLBA(m,s,f)  (((((m*60)+s)*75)+f)-150)
/* The addresses sent from the guest are absolute, ie. a LBA of 0 corresponds to a MSF of 00:00:00. Otherwise, the counter displayed by the guest is wrong:
   there is a seeming 2 seconds in which audio plays but counter does not move, while a data track before audio jumps to 2 seconds before the actual start
   of the audio while audio still plays. With an absolute conversion, the counter is fine. */

enum
{
    CD_STOPPED = 0,
    CD_PLAYING,
    CD_PAUSED
};

int cdrom_ioctl_do_log = 1;

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

//	return;
//        cdrom_ioctl_log("Audio callback %08X %08X %i %i %i %04X %i\n", ioctl_cd_pos, ioctl_cd_end, ioctl_cd_state, cd_buflen, len, cd_buffer[4], GetTickCount());
        if (cdrom_ioctl[id].cd_state != CD_PLAYING) 
        {
                memset(output, 0, len * 2);
                return;
        }
        while (cdrom_ioctl[id].cd_buflen < len)
        {
                if (cdrom[id].seek_pos < cdrom_ioctl[id].cd_end)
                {
		        in.DiskOffset.LowPart  = (cdrom[id].seek_pos - 150) * 2048;
        		in.DiskOffset.HighPart = 0;
        		in.SectorCount	       = 1;
        		in.TrackMode	       = CDDA;		
        		ioctl_open(id, 0);
//        		cdrom_ioctl_log("Read to %i\n", cd_buflen);
        		if (!DeviceIoControl(cdrom_ioctl_windows[id].hIOCTL, IOCTL_CDROM_RAW_READ, &in, sizeof(in), &(cdrom_ioctl[id].cd_buffer[cdrom_ioctl[id].cd_buflen]), 2352, &count, NULL))
        		{
//                                cdrom_ioctl_log("DeviceIoControl returned false\n");
                                memset(&(cdrom_ioctl[id].cd_buffer[cdrom_ioctl[id].cd_buflen]), 0, (BUF_SIZE - cdrom_ioctl[id].cd_buflen) * 2);
                                cdrom_ioctl[id].cd_state = CD_STOPPED;
                                cdrom_ioctl[id].cd_buflen = len;
                        }
                        else
                        {
//                                cdrom_ioctl_log("DeviceIoControl returned true\n");
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
//        for (c = 0; c < BUF_SIZE - len; c++)
//            cd_buffer[c] = cd_buffer[c + cd_buflen];
        memcpy(&cdrom_ioctl[id].cd_buffer[0], &(cdrom_ioctl[id].cd_buffer[len]), (BUF_SIZE - len) * 2);
        cdrom_ioctl[id].cd_buflen -= len;
//        cdrom_ioctl_log("Done %i\n", GetTickCount());
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
                return 0;

        for (c = cdrom_ioctl_windows[id].toc.FirstTrack; c < cdrom_ioctl_windows[id].toc.LastTrack; c++)
        {
                uint32_t track_address = cdrom_ioctl_windows[id].toc.TrackData[c].Address[3] +
                                         (cdrom_ioctl_windows[id].toc.TrackData[c].Address[2] * 75) +
                                         (cdrom_ioctl_windows[id].toc.TrackData[c].Address[1] * 75 * 60);

                if (track_address <= pos)
                        track = c;
        }
        return track;
}

static uint32_t get_track_msf(uint8_t id, uint32_t track_no)
{
        int c;
        int track = 0;
        
        if (!cdrom_ioctl[id].tocvalid)
                return 0;

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
        if (!cdrom_drives[id].host_drive) return;
        // cdrom_ioctl_log("Play audio - %08X %08X %i\n", pos, len, ismsf);
		if (ismsf == 2)
		{
				uint32_t start_msf = get_track_msf(id, pos);
				uint32_t end_msf = get_track_msf(id, len);
				if (start_msf == 0xffffffff)
				{
					return;
				}
				if (end_msf == 0xffffffff)
				{
					return;
				}
                int m = (start_msf >> 16) & 0xff;
                int s = (start_msf >> 8) & 0xff;
                int f = start_msf & 0xff;
                pos = MSFtoLBA(m, s, f);
                m = (end_msf >> 16) & 0xff;
                s = (end_msf >> 8) & 0xff;
                f = end_msf & 0xff;
                len = MSFtoLBA(m, s, f);
		}
        else if (ismsf == 1)
        {
				int m = (pos >> 16) & 0xff;
				int s = (pos >> 8) & 0xff;
				int f = pos & 0xff;

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
                // cdrom_ioctl_log("MSF - pos = %08X len = %08X\n", pos, len);
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
        cdrom[id].seek_pos   = pos;// + 150;
        cdrom_ioctl[id].cd_end   = len;// + 150;
		if (cdrom[id].seek_pos < 150)
		{
			/* Adjust because the host expects a minimum adjusted LBA of 0 which is equivalent to an absolute LBA of 150. */
			cdrom[id].seek_pos = 150;
		}
        cdrom_ioctl[id].cd_state = CD_PLAYING;
        // cdrom_ioctl_log("Audio start %08X %08X %i %i %i\n", ioctl_cd_pos, ioctl_cd_end, ioctl_cd_state, cd_buflen, len);        
}

static void ioctl_pause(uint8_t id)
{
        if (!cdrom_drives[id].host_drive) return;
        if (cdrom_ioctl[id].cd_state == CD_PLAYING)
           cdrom_ioctl[id].cd_state = CD_PAUSED;
}

static void ioctl_resume(uint8_t id)
{
        if (!cdrom_drives[id].host_drive) return;
        if (cdrom_ioctl[id].cd_state == CD_PAUSED)
           cdrom_ioctl[id].cd_state = CD_PLAYING;
}

static void ioctl_stop(uint8_t id)
{
        if (!cdrom_drives[id].host_drive) return;
        cdrom_ioctl[id].cd_state = CD_STOPPED;
}

static int ioctl_ready(uint8_t id)
{
        long size;
        int temp;
        CDROM_TOC ltoc;
		// cdrom_ioctl_log("Ready? %i\n",cdrom_drives[id].host_drive);
        if (!cdrom_drives[id].host_drive) return 0;
        ioctl_open(id, 0);
        temp=DeviceIoControl(cdrom_ioctl_windows[id].hIOCTL,IOCTL_CDROM_READ_TOC, NULL,0,&ltoc,sizeof(ltoc),&size,NULL);
        ioctl_close(id);
        if (!temp)
                return 0;
		// cdrom_ioctl_log("ioctl_ready(): Drive opened successfully\n");
		// if ((cdrom_drives[id].host_drive != cdrom_drives[id].prev_host_drive)) cdrom_ioctl_log("Drive has changed\n");
        if ((ltoc.TrackData[ltoc.LastTrack].Address[1] != cdrom_ioctl_windows[id].toc.TrackData[cdrom_ioctl_windows[id].toc.LastTrack].Address[1]) ||
            (ltoc.TrackData[ltoc.LastTrack].Address[2] != cdrom_ioctl_windows[id].toc.TrackData[cdrom_ioctl_windows[id].toc.LastTrack].Address[2]) ||
            (ltoc.TrackData[ltoc.LastTrack].Address[3] != cdrom_ioctl_windows[id].toc.TrackData[cdrom_ioctl_windows[id].toc.LastTrack].Address[3]) ||
            !cdrom_ioctl[id].tocvalid || (cdrom_drives[id].host_drive != cdrom_drives[id].prev_host_drive))
        {
			// cdrom_ioctl_log("ioctl_ready(): Disc or drive changed\n");
			// cdrom_ioctl_log("ioctl_ready(): Stopped\n");
			cdrom_ioctl[id].cd_state = CD_STOPPED;                
			if (cdrom_drives[id].host_drive != cdrom_drives[id].prev_host_drive)
				cdrom_drives[id].prev_host_drive = cdrom_drives[id].host_drive;
			return 1;
        }
		// cdrom_ioctl_log("ioctl_ready(): All is good\n");
        return 1;
}

static int ioctl_get_last_block(uint8_t id, unsigned char starttrack, int msf, int maxlen, int single)
{
        int len=4;
        long size;
        int c,d;
        uint32_t temp;
		CDROM_TOC lbtoc;
		int lb=0;
        if (!cdrom_drives[id].host_drive) return 0;
		cdrom_ioctl[id].cd_state = CD_STOPPED;
		// cdrom_ioctl_log("ioctl_readtoc(): IOCtl state now CD_STOPPED\n");
        ioctl_open(id, 0);
        DeviceIoControl(cdrom_ioctl_windows[id].hIOCTL,IOCTL_CDROM_READ_TOC, NULL,0,&lbtoc,sizeof(lbtoc),&size,NULL);
        ioctl_close(id);
        cdrom_ioctl[id].tocvalid=1;
        for (c=d;c<=lbtoc.LastTrack;c++)
        {
                uint32_t address;
                address = MSFtoLBA(cdrom_ioctl_windows[id].toc.TrackData[c].Address[1],cdrom_ioctl_windows[id].toc.TrackData[c].Address[2],cdrom_ioctl_windows[id].toc.TrackData[c].Address[3]);
                if (address > lb)
                        lb = address;
		}
		return lb;
}

static int ioctl_medium_changed(uint8_t id)
{
        long size;
        int temp;
        CDROM_TOC ltoc;
        if (!cdrom_drives[id].host_drive) return 0;		/* This will be handled by the not ready handler instead. */
        ioctl_open(id, 0);
        temp=DeviceIoControl(cdrom_ioctl_windows[id].hIOCTL,IOCTL_CDROM_READ_TOC, NULL,0,&ltoc,sizeof(ltoc),&size,NULL);
        ioctl_close(id);
        if (!temp)
                return 0; /* Drive empty, a not ready handler matter, not disc change. */
        if (!cdrom_ioctl[id].tocvalid || (cdrom_drives[id].host_drive != cdrom_drives[id].prev_host_drive))
        {
                cdrom_ioctl[id].cd_state = CD_STOPPED;
                cdrom_ioctl_windows[id].toc = ltoc;
                cdrom_ioctl[id].tocvalid = 1;
                if (cdrom_drives[id].host_drive != cdrom_drives[id].prev_host_drive)
                        cdrom_drives[id].prev_host_drive = cdrom_drives[id].host_drive;
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
	long size;
	int pos=0;
	if (!cdrom_drives[id].host_drive) return 0;
        
	insub.Format = IOCTL_CDROM_CURRENT_POSITION;
	ioctl_open(id, 0);
	DeviceIoControl(cdrom_ioctl_windows[id].hIOCTL,IOCTL_CDROM_READ_Q_CHANNEL,&insub,sizeof(insub),&sub,sizeof(sub),&size,NULL);
	ioctl_close(id);

	if (cdrom_ioctl[id].cd_state == CD_PLAYING || cdrom_ioctl[id].cd_state == CD_PAUSED)
	{
		uint32_t cdpos = cdrom[id].seek_pos;
		int track = get_track_nr(id, cdpos);
		uint32_t track_address = cdrom_ioctl_windows[id].toc.TrackData[track].Address[3] + (cdrom_ioctl_windows[id].toc.TrackData[track].Address[2] * 75) + (cdrom_ioctl_windows[id].toc.TrackData[track].Address[1] * 75 * 60);

                b[pos++] = sub.CurrentPosition.Control;
                b[pos++] = track + 1;
                b[pos++] = sub.CurrentPosition.IndexNumber;

                if (msf)
                {
                        uint32_t dat = cdpos;
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
        
        if (msf)
        {
                int c;
                for (c = 0; c < 4; c++)
                        b[pos++] = sub.CurrentPosition.AbsoluteAddress[c];
                for (c = 0; c < 4; c++)
                        b[pos++] = sub.CurrentPosition.TrackRelativeAddress[c];
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
        long size;
        if (!cdrom_drives[id].host_drive) return;
        cdrom_ioctl[id].cd_state = CD_STOPPED;        
        ioctl_open(id, 0);
        DeviceIoControl(cdrom_ioctl_windows[id].hIOCTL,IOCTL_STORAGE_EJECT_MEDIA,NULL,0,NULL,0,&size,NULL);
        ioctl_close(id);
}

static void ioctl_load(uint8_t id)
{
        long size;
        if (!cdrom_drives[id].host_drive) return;
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
        
        if (!cdrom_ioctl[id].tocvalid)
                return 0;

        for (c = 0; c <= cdrom_ioctl_windows[id].toc.LastTrack; c++)
        {
                uint32_t track_address = MSFtoLBA(cdrom_ioctl_windows[id].toc.TrackData[c].Address[1],cdrom_ioctl_windows[id].toc.TrackData[c].Address[2],cdrom_ioctl_windows[id].toc.TrackData[c].Address[3]);
										 
                if (track_address <= pos)
                        control = cdrom_ioctl_windows[id].toc.TrackData[c].Control;
        }
		// cdrom_ioctl_log("Control: %i\n", control);
		if ((control & 0xd) == 0)
		{
			return 1;
		}
		else if ((control & 0xd) == 1)
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
	if (ismsf)
	{
		int m = (pos >> 16) & 0xff;
		int s = (pos >> 8) & 0xff;
		int f = pos & 0xff;
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

static int SCSICommand(uint8_t id, const UCHAR *cdb, UCHAR *buf, uint32_t *len, int no_length_check)
{
  HANDLE fh;
  DWORD ioctl_bytes;
  DWORD out_size;
  int ioctl_rv = 0;
  int sector_type = 0;
  int temp_len = 0;

	SCSISense.SenseKey = 0;
	SCSISense.Asc = 0;
	SCSISense.Ascq = 0;

  *len = 0;
  memset(&sptd, 0, sizeof(sptd));
  sptd.s.Length = sizeof(SCSI_PASS_THROUGH);
  sptd.s.CdbLength = 12;
  sptd.s.DataIn = SCSI_IOCTL_DATA_IN;
  sptd.s.TimeOutValue = 80 * 60;
  goto bypass_check;
  if (no_length_check)  goto bypass_check;
	switch (cdb[0])
	{
		case 0x08:
		case 0x28:
		case 0xa8:
			/* READ (6), READ (10), READ (12) */
			sptd.s.DataTransferLength = 2048 * cdrom[id].requested_blocks;
			break;
		case 0xb9:
			sector_type = (cdb[1] >> 2) & 7;
			if (sector_type == 0)
			{
				sector_type = ioctl_get_sector_data_type(id, 0, cdb[3], cdb[4], cdb[5], 1);
				if (sector_type == 0)
				{
					cdrom_illegal_mode(id);
					return 1;
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
					return 1;
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
					return 1;
			}
			sptd.s.DataTransferLength = temp_len * cdrom[id].requested_blocks;
			break;
		default:
bypass_check:
			/* Other commands */
			sptd.s.DataTransferLength = 65536;
			break;
	}
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
	int len = 0;

	const UCHAR cdb[] = { 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	UCHAR buf[16];

	ioctl_open(id, 0);

	SCSICommand(id, cdb, buf, &len, 1);
	
	memcpy(b, buf, len);
	
	ioctl_close(id);
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

  int len = 0;
  
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
  const UCHAR cdb_lba[] = { 0xBE, 0, (sector >> 24), ((sector >> 16) & 0xff), ((sector >> 8) & 0xff), (sector & 0xff), 0, 0, 1, 0x10, 0, 0 };
  const UCHAR cdb_msf[] = { 0xB9, 0, 0, ((sector >> 16) & 0xff), ((sector >> 8) & 0xff), (sector & 0xff), ((sector >> 16) & 0xff), ((sector >> 8) & 0xff), (sector & 0xff), 0x10, 0, 0 };
  UCHAR buf[2352];

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
	  ioctl_rv = ioctl_get_type(id, cdb_lba, buf);
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
	long size;
	if (!cdrom_drives[id].host_drive)
	{
		return;
	}
	cdrom_ioctl[id].cd_state = CD_STOPPED;        
	ioctl_open(id, 0);
	DeviceIoControl(cdrom_ioctl_windows[id].hIOCTL,IOCTL_CDROM_READ_TOC, NULL,0,&cdrom_ioctl_windows[id].toc,sizeof(cdrom_ioctl_windows[id].toc),&size,NULL);
	ioctl_close(id);
	cdrom_ioctl[id].tocvalid=1;
}

UCHAR buf[65536];

static int ioctl_pass_through(uint8_t id, uint8_t *in_cdb, uint8_t *b, uint32_t *len)
{
	const UCHAR cdb[12];
	
	int ret;
	
	if (cdb[0] == 0x43)
	{
		/* This is a read TOC, so we have to validate the TOC to make the rest of the emulator happy. */
		ioctl_validate_toc(id);
	}

	ioctl_open(id, 0);

	memcpy(cdb, in_cdb, 12);
	ret = SCSICommand(id, cdb, buf, len, 0);
	
	memcpy(b, buf, *len);

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
        // return cdrom_capacity;
}

static int ioctl_status(uint8_t id)
{
	if (!(ioctl_ready(id)) && (cdrom_drives[id].host_drive <= 0))
                return CD_STATUS_EMPTY;

	switch(cdrom_ioctl[id].cd_state)
	{
		case CD_PLAYING:
			return CD_STATUS_PLAYING;
		case CD_PAUSED:
			return CD_STATUS_PAUSED;
		case CD_STOPPED:
			return CD_STATUS_STOPPED;
	}
}

void ioctl_reset(uint8_t id)
{
        CDROM_TOC ltoc;
        int temp;
        long size;

        if (!cdrom_drives[id].host_drive)
        {
                cdrom_ioctl[id].tocvalid = 0;
                return;
        }
        
        ioctl_open(id, 0);
        temp = DeviceIoControl(cdrom_ioctl_windows[id].hIOCTL, IOCTL_CDROM_READ_TOC, NULL, 0, &ltoc, sizeof(ltoc), &size, NULL);
        ioctl_close(id);

        cdrom_ioctl_windows[id].toc = ltoc;
        cdrom_ioctl[id].tocvalid = 1;
}

int ioctl_open(uint8_t id, char d)
{
	// char s[8];
	if (!cdrom_ioctl[id].ioctl_inited)
	{
		sprintf(cdrom_ioctl[id].ioctl_path,"\\\\.\\%c:",d);
		// cdrom_ioctl_log("Path is %s\n",ioctl_path);
		cdrom_ioctl[id].tocvalid=0;
	}
	// cdrom_ioctl_log("Opening %s\n",ioctl_path);
	cdrom_ioctl_windows[id].hIOCTL = CreateFile(cdrom_ioctl[id].ioctl_path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (!cdrom_ioctl_windows[id].hIOCTL)
	{
		// fatal("IOCTL");
	}
	cdrom_drives[id].handler = &ioctl_cdrom;
	if (!cdrom_ioctl[id].ioctl_inited)
	{
		cdrom_ioctl[id].ioctl_inited=1;
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
		ioctl_audio_callback,
		ioctl_audio_stop,
        NULL,
        NULL,
		NULL,
        ioctl_getcurrentsubchannel,
		ioctl_pass_through,
		ioctl_sector_data_type,
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
