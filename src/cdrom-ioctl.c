/*Win32 CD-ROM support via IOCTL*/

#include <windows.h>
#include <io.h>
#include "ntddcdrm.h"
#include "ibm.h"
#include "ide.h"
#include "cdrom-ioctl.h"

int cdrom_drive;
int old_cdrom_drive;

static ATAPI ioctl_atapi;

static uint32_t last_block = 0;
static uint32_t cdrom_capacity = 0;
static int ioctl_inited = 0;
static char ioctl_path[8];
void ioctl_close(void);
static HANDLE hIOCTL;
static CDROM_TOC toc;
static int tocvalid = 0;

// #define MSFtoLBA(m,s,f)  (((((m*60)+s)*75)+f)-150)
/* The addresses sent from the guest are absolute, ie. a LBA of 0 corresponds to a MSF of 00:00:00. Otherwise, the counter displayed by the guest is wrong:
   there is a seeming 2 seconds in which audio plays but counter does not move, while a data track before audio jumps to 2 seconds before the actual start
   of the audio while audio still plays. With an absolute conversion, the counter is fine. */
#define MSFtoLBA(m,s,f)  ((((m*60)+s)*75)+f)

enum
{
    CD_STOPPED = 0,
    CD_PLAYING,
    CD_PAUSED
};

static int ioctl_cd_state = CD_STOPPED;
static uint32_t ioctl_cd_pos = 0, ioctl_cd_end = 0;

#define BUF_SIZE 32768
static int16_t cd_buffer[BUF_SIZE];
static int cd_buflen = 0;
void ioctl_audio_callback(int16_t *output, int len)
{
	RAW_READ_INFO in;
	DWORD count;

//	return;
//        pclog("Audio callback %08X %08X %i %i %i %04X %i\n", ioctl_cd_pos, ioctl_cd_end, ioctl_cd_state, cd_buflen, len, cd_buffer[4], GetTickCount());
        if (ioctl_cd_state != CD_PLAYING) 
        {
                memset(output, 0, len * 2);
                return;
        }
        while (cd_buflen < len)
        {
                if (ioctl_cd_pos < ioctl_cd_end)
                {
		        in.DiskOffset.LowPart  = (ioctl_cd_pos - 150) * 2048;
        		in.DiskOffset.HighPart = 0;
        		in.SectorCount	       = 1;
        		in.TrackMode	       = CDDA;		
        		ioctl_open(0);
//        		pclog("Read to %i\n", cd_buflen);
        		if (!DeviceIoControl(hIOCTL, IOCTL_CDROM_RAW_READ, &in, sizeof(in), &cd_buffer[cd_buflen], 2352, &count, NULL))
        		{
//                                pclog("DeviceIoControl returned false\n");
                                memset(&cd_buffer[cd_buflen], 0, (BUF_SIZE - cd_buflen) * 2);
                                ioctl_cd_state = CD_STOPPED;
                                cd_buflen = len;
                        }
                        else
                        {
//                                pclog("DeviceIoControl returned true\n");
                                ioctl_cd_pos++;
                                cd_buflen += (2352 / 2);
                        }
                        ioctl_close();
                }
                else
                {
                        memset(&cd_buffer[cd_buflen], 0, (BUF_SIZE - cd_buflen) * 2);
                        ioctl_cd_state = CD_STOPPED;
                        cd_buflen = len;                        
                }
        }
        memcpy(output, cd_buffer, len * 2);
//        for (c = 0; c < BUF_SIZE - len; c++)
//            cd_buffer[c] = cd_buffer[c + cd_buflen];
        memcpy(&cd_buffer[0], &cd_buffer[len], (BUF_SIZE - len) * 2);
        cd_buflen -= len;
//        pclog("Done %i\n", GetTickCount());
}

void ioctl_audio_stop()
{
        ioctl_cd_state = CD_STOPPED;
}

static int get_track_nr(uint32_t pos)
{
        int c;
        int track = 0;
        
        if (!tocvalid)
                return 0;

        for (c = toc.FirstTrack; c < toc.LastTrack; c++)
        {
                uint32_t track_address = toc.TrackData[c].Address[3] +
                                         (toc.TrackData[c].Address[2] * 75) +
                                         (toc.TrackData[c].Address[1] * 75 * 60);

                if (track_address <= pos)
                        track = c;
        }
        return track;
}

static void ioctl_playaudio(uint32_t pos, uint32_t len, int ismsf)
{
        if (!cdrom_drive) return;
        // pclog("Play audio - %08X %08X %i\n", pos, len, ismsf);
        if (ismsf)
        {
                int m = (pos >> 16) & 0xff;
                int s = (pos >> 8) & 0xff;
                int f = pos & 0xff;
                pos = MSFtoLBA(m, s, f);
                m = (len >> 16) & 0xff;
                s = (len >> 8) & 0xff;
                f = len & 0xff;
                len = MSFtoLBA(m, s, f);
                // pclog("MSF - pos = %08X len = %08X\n", pos, len);
        }
        else
           len += pos;
        ioctl_cd_pos   = pos;// + 150;
        ioctl_cd_end   = len;// + 150;
		if (ioctl_cd_pos < 150)
		{
			/* Adjust because the host expects a minimum adjusted LBA of 0 which is equivalent to an absolute LBA of 150. */
			ioctl_cd_pos = 150;
		}
        ioctl_cd_state = CD_PLAYING;
        // pclog("Audio start %08X %08X %i %i %i\n", ioctl_cd_pos, ioctl_cd_end, ioctl_cd_state, cd_buflen, len);        
/*        CDROM_PLAY_AUDIO_MSF msf;
        long size;
        BOOL b;
        if (ismsf)
        {
                msf.StartingF=pos&0xFF;
                msf.StartingS=(pos>>8)&0xFF;
                msf.StartingM=(pos>>16)&0xFF;
                msf.EndingF=len&0xFF;
                msf.EndingS=(len>>8)&0xFF;
                msf.EndingM=(len>>16)&0xFF;
        }
        else
        {
                msf.StartingF=(uint8_t)(addr%75); addr/=75;
                msf.StartingS=(uint8_t)(addr%60); addr/=60;
                msf.StartingM=(uint8_t)(addr);
                addr=pos+len+150;
                msf.EndingF=(uint8_t)(addr%75); addr/=75;
                msf.EndingS=(uint8_t)(addr%60); addr/=60;
                msf.EndingM=(uint8_t)(addr);
        }
        ioctl_open(0);
        b = DeviceIoControl(hIOCTL,IOCTL_CDROM_PLAY_AUDIO_MSF,&msf,sizeof(msf),NULL,0,&size,NULL);
        pclog("DeviceIoControl returns %i\n", (int) b);
        ioctl_close();*/
}

static void ioctl_pause(void)
{
        if (!cdrom_drive) return;
        if (ioctl_cd_state == CD_PLAYING)
           ioctl_cd_state = CD_PAUSED;
//        ioctl_open(0);
//        DeviceIoControl(hIOCTL,IOCTL_CDROM_PAUSE_AUDIO,NULL,0,NULL,0,&size,NULL);
//        ioctl_close();
}

static void ioctl_resume(void)
{
        if (!cdrom_drive) return;
        if (ioctl_cd_state == CD_PAUSED)
           ioctl_cd_state = CD_PLAYING;
//        ioctl_open(0);
//        DeviceIoControl(hIOCTL,IOCTL_CDROM_RESUME_AUDIO,NULL,0,NULL,0,&size,NULL);
//        ioctl_close();
}

static void ioctl_stop(void)
{
        if (!cdrom_drive) return;
        ioctl_cd_state = CD_STOPPED;
//        ioctl_open(0);
//        DeviceIoControl(hIOCTL,IOCTL_CDROM_STOP_AUDIO,NULL,0,NULL,0,&size,NULL);
//        ioctl_close();
}

static void ioctl_seek(uint32_t pos)
{
        if (!cdrom_drive) return;
 //       ioctl_cd_state = CD_STOPPED;
        pclog("Seek %08X\n", pos);
        ioctl_cd_pos   = pos;
        ioctl_cd_state = CD_STOPPED;
/*        pos+=150;
        CDROM_SEEK_AUDIO_MSF msf;
        msf.F=(uint8_t)(pos%75); pos/=75;
        msf.S=(uint8_t)(pos%60); pos/=60;
        msf.M=(uint8_t)(pos);
//        pclog("Seek to %02i:%02i:%02i\n",msf.M,msf.S,msf.F);
        ioctl_open(0);
        DeviceIoControl(hIOCTL,IOCTL_CDROM_SEEK_AUDIO_MSF,&msf,sizeof(msf),NULL,0,&size,NULL);
        ioctl_close();*/
}

static int ioctl_ready(void)
{
        long size;
        int temp;
        CDROM_TOC ltoc;
//        pclog("Ready? %i\n",cdrom_drive);
        if (!cdrom_drive) return 0;
        ioctl_open(0);
        temp=DeviceIoControl(hIOCTL,IOCTL_CDROM_READ_TOC, NULL,0,&ltoc,sizeof(ltoc),&size,NULL);
        ioctl_close();
        if (!temp)
                return 0;
	//pclog("ioctl_ready(): Drive opened successfully\n");
	//if ((cdrom_drive != old_cdrom_drive)) pclog("Drive has changed\n");
        if ((ltoc.TrackData[ltoc.LastTrack].Address[1] != toc.TrackData[toc.LastTrack].Address[1]) ||
            (ltoc.TrackData[ltoc.LastTrack].Address[2] != toc.TrackData[toc.LastTrack].Address[2]) ||
            (ltoc.TrackData[ltoc.LastTrack].Address[3] != toc.TrackData[toc.LastTrack].Address[3]) ||
            !tocvalid || (cdrom_drive != old_cdrom_drive))
        {
		//pclog("ioctl_ready(): Disc or drive changed\n");
                ioctl_cd_state = CD_STOPPED;                
  /*              pclog("Not ready %02X %02X %02X  %02X %02X %02X  %i\n",ltoc.TrackData[ltoc.LastTrack].Address[1],ltoc.TrackData[ltoc.LastTrack].Address[2],ltoc.TrackData[ltoc.LastTrack].Address[3],
                                                                        toc.TrackData[ltoc.LastTrack].Address[1], toc.TrackData[ltoc.LastTrack].Address[2], toc.TrackData[ltoc.LastTrack].Address[3],tocvalid);*/
        //        atapi_discchanged();
/*                ioctl_open(0);
                temp=DeviceIoControl(hIOCTL,IOCTL_CDROM_READ_TOC, NULL,0,&toc,sizeof(toc),&size,NULL);
                ioctl_close();*/
		if (cdrom_drive != old_cdrom_drive)
                        old_cdrom_drive = cdrom_drive;
                return 1;
        }
//        pclog("IOCTL says ready\n");
//        pclog("ioctl_ready(): All is good\n");
        return 1;
}

static int ioctl_get_last_block(unsigned char starttrack, int msf, int maxlen, int single)
{
        int len=4;
        long size;
        int c,d;
        uint32_t temp;
		CDROM_TOC lbtoc;
		int lb=0;
        if (!cdrom_drive) return 0;
		ioctl_cd_state = CD_STOPPED;
		pclog("ioctl_readtoc(): IOCtl state now CD_STOPPED\n");
        ioctl_open(0);
        DeviceIoControl(hIOCTL,IOCTL_CDROM_READ_TOC, NULL,0,&lbtoc,sizeof(lbtoc),&size,NULL);
        ioctl_close();
        tocvalid=1;
        for (c=d;c<=lbtoc.LastTrack;c++)
        {
                uint32_t address;
                address = MSFtoLBA(toc.TrackData[c].Address[1],toc.TrackData[c].Address[2],toc.TrackData[c].Address[3]);
                if (address > lb)
                        lb = address;
		}
		return lb;
}

static int ioctl_medium_changed(void)
{
        long size;
        int temp;
        CDROM_TOC ltoc;
        if (!cdrom_drive) return 0;		/* This will be handled by the not ready handler instead. */
        ioctl_open(0);
        temp=DeviceIoControl(hIOCTL,IOCTL_CDROM_READ_TOC, NULL,0,&ltoc,sizeof(ltoc),&size,NULL);
        ioctl_close();
        if (!temp)
                return 0; /* Drive empty, a not ready handler matter, not disc change. */
        if (!tocvalid || (cdrom_drive != old_cdrom_drive))
        {
                ioctl_cd_state = CD_STOPPED;
                toc = ltoc;
                tocvalid = 1;
                if (cdrom_drive != old_cdrom_drive)
                        old_cdrom_drive = cdrom_drive;
				cdrom_capacity = ioctl_get_last_block(0, 0, 4096, 0);
                return 0;
        }
        if ((ltoc.TrackData[ltoc.LastTrack].Address[1] != toc.TrackData[toc.LastTrack].Address[1]) ||
            (ltoc.TrackData[ltoc.LastTrack].Address[2] != toc.TrackData[toc.LastTrack].Address[2]) ||
            (ltoc.TrackData[ltoc.LastTrack].Address[3] != toc.TrackData[toc.LastTrack].Address[3]))
	{
                ioctl_cd_state = CD_STOPPED;
                toc = ltoc;
				cdrom_capacity = ioctl_get_last_block(0, 0, 4096, 0);
		return 1; /* TOC mismatches. */
	}
        return 0; /* None of the above, return 0. */
}

static uint8_t ioctl_getcurrentsubchannel(uint8_t *b, int msf)
{
	CDROM_SUB_Q_DATA_FORMAT insub;
	SUB_Q_CHANNEL_DATA sub;
	long size;
	int pos=0;
        if (!cdrom_drive) return 0;
        
	insub.Format = IOCTL_CDROM_CURRENT_POSITION;
        ioctl_open(0);
        DeviceIoControl(hIOCTL,IOCTL_CDROM_READ_Q_CHANNEL,&insub,sizeof(insub),&sub,sizeof(sub),&size,NULL);
        ioctl_close();

        if (ioctl_cd_state == CD_PLAYING || ioctl_cd_state == CD_PAUSED)
        {
                uint32_t cdpos = ioctl_cd_pos;
                int track = get_track_nr(cdpos);
                uint32_t track_address = toc.TrackData[track].Address[3] +
                                         (toc.TrackData[track].Address[2] * 75) +
                                         (toc.TrackData[track].Address[1] * 75 * 60);

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

                if (ioctl_cd_state == CD_PLAYING) return 0x11;
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

static void ioctl_eject(void)
{
        long size;
        if (!cdrom_drive) return;
        ioctl_cd_state = CD_STOPPED;        
        ioctl_open(0);
        DeviceIoControl(hIOCTL,IOCTL_STORAGE_EJECT_MEDIA,NULL,0,NULL,0,&size,NULL);
        ioctl_close();
}

static void ioctl_load(void)
{
        long size;
        if (!cdrom_drive) return;
        ioctl_cd_state = CD_STOPPED;        
        ioctl_open(0);
        DeviceIoControl(hIOCTL,IOCTL_STORAGE_LOAD_MEDIA,NULL,0,NULL,0,&size,NULL);
        ioctl_close();
		cdrom_capacity = ioctl_get_last_block(0, 0, 4096, 0);
}

static void ioctl_readsector(uint8_t *b, int sector)
{
        LARGE_INTEGER pos;
        long size;
        if (!cdrom_drive) return;

	if (ioctl_cd_state == CD_PLAYING)
		return;

        ioctl_cd_state = CD_STOPPED;        
        pos.QuadPart=sector*2048;
        ioctl_open(0);
        SetFilePointer(hIOCTL,pos.LowPart,&pos.HighPart,FILE_BEGIN);
        ReadFile(hIOCTL,b,2048,&size,NULL);
        ioctl_close();
}

static int is_track_audio(uint32_t pos)
{
        int c;
        int control = 0;
        
        if (!tocvalid)
                return 0;

        for (c = toc.FirstTrack; c < toc.LastTrack; c++)
        {
                uint32_t track_address = toc.TrackData[c].Address[3] +
                                         (toc.TrackData[c].Address[2] * 75) +
                                         (toc.TrackData[c].Address[1] * 75 * 60);

                if (track_address <= pos)
                        control = toc.TrackData[c].Control;
        }
        return (control & 4) ? 0 : 1;
}

static int ioctl_is_track_audio(uint32_t pos, int ismsf)
{
	if (ismsf)
	{
		int m = (pos >> 16) & 0xff;
		int s = (pos >> 8) & 0xff;
		int f = pos & 0xff;
		pos = MSFtoLBA(m, s, f);
	}
	return is_track_audio(pos);
}

static void ioctl_readsector_raw(uint8_t *b, int sector)
{
        LARGE_INTEGER pos;
        long size;
	uint32_t temp;
        if (!cdrom_drive) return;
	if (ioctl_cd_state == CD_PLAYING)
		return;

        ioctl_cd_state = CD_STOPPED;        
		pos.QuadPart=sector*2048;		/* Yes, 2048, the API specifies that. */
        ioctl_open(0);
		/* This should read the actual raw sectors from the disc. */
		DeviceIoControl( hIOCTL, IOCTL_CDROM_RAW_READ, NULL, 0, b, 1, &size, NULL );						   
        ioctl_close();
}

static int ioctl_readtoc(unsigned char *b, unsigned char starttrack, int msf, int maxlen, int single)
{
        int len=4;
        long size;
        int c,d;
        uint32_t temp;
        if (!cdrom_drive) return 0;
        ioctl_cd_state = CD_STOPPED;        
        ioctl_open(0);
        DeviceIoControl(hIOCTL,IOCTL_CDROM_READ_TOC, NULL,0,&toc,sizeof(toc),&size,NULL);
        ioctl_close();
        tocvalid=1;
//        pclog("Read TOC done! %i\n",single);
        b[2]=toc.FirstTrack;
        b[3]=toc.LastTrack;
        d=0;
        for (c=0;c<=toc.LastTrack;c++)
        {
                if (toc.TrackData[c].TrackNumber>=starttrack)
                {
                        d=c;
                        break;
                }
        }
        b[2]=toc.TrackData[c].TrackNumber;
        last_block = 0;
        for (c=d;c<=toc.LastTrack;c++)
        {
                uint32_t address;
                if ((len+8)>maxlen) break;
//                pclog("Len %i max %i Track %02X - %02X %02X %i %i %i %i %08X\n",len,maxlen,toc.TrackData[c].TrackNumber,toc.TrackData[c].Adr,toc.TrackData[c].Control,toc.TrackData[c].Address[0],toc.TrackData[c].Address[1],toc.TrackData[c].Address[2],toc.TrackData[c].Address[3],MSFtoLBA(toc.TrackData[c].Address[1],toc.TrackData[c].Address[2],toc.TrackData[c].Address[3]));
                b[len++]=0; /*Reserved*/
                b[len++]=(toc.TrackData[c].Adr<<4)|toc.TrackData[c].Control;
                b[len++]=toc.TrackData[c].TrackNumber;
                b[len++]=0; /*Reserved*/
                address = MSFtoLBA(toc.TrackData[c].Address[1],toc.TrackData[c].Address[2],toc.TrackData[c].Address[3]);
                if (address > last_block)
                        last_block = address;

                if (msf)
                {
                        b[len++]=toc.TrackData[c].Address[0];
                        b[len++]=toc.TrackData[c].Address[1];
                        b[len++]=toc.TrackData[c].Address[2];
                        b[len++]=toc.TrackData[c].Address[3];
                }
                else
                {
                        temp=MSFtoLBA(toc.TrackData[c].Address[1],toc.TrackData[c].Address[2],toc.TrackData[c].Address[3]);
                        b[len++]=temp>>24;
                        b[len++]=temp>>16;
                        b[len++]=temp>>8;
                        b[len++]=temp;
                }
                if (single) break;
        }
        b[0] = (uint8_t)(((len-2) >> 8) & 0xff);
        b[1] = (uint8_t)((len-2) & 0xff);
/*        pclog("Table of Contents (%i bytes) : \n",size);
        pclog("First track - %02X\n",toc.FirstTrack);
        pclog("Last  track - %02X\n",toc.LastTrack);
        for (c=0;c<=toc.LastTrack;c++)
            pclog("Track %02X - number %02X control %02X adr %02X address %02X %02X %02X %02X\n",c,toc.TrackData[c].TrackNumber,toc.TrackData[c].Control,toc.TrackData[c].Adr,toc.TrackData[c].Address[0],toc.TrackData[c].Address[1],toc.TrackData[c].Address[2],toc.TrackData[c].Address[3]);
        for (c=0;c<=toc.LastTrack;c++)
            pclog("Track %02X - number %02X control %02X adr %02X address %06X\n",c,toc.TrackData[c].TrackNumber,toc.TrackData[c].Control,toc.TrackData[c].Adr,MSFtoLBA(toc.TrackData[c].Address[1],toc.TrackData[c].Address[2],toc.TrackData[c].Address[3]));*/
        return len;
}

static int ioctl_readtoc_session(unsigned char *b, int msf, int maxlen)
{
        int len=4;
        int size;
        uint32_t temp;
        CDROM_READ_TOC_EX toc_ex;
        CDROM_TOC_SESSION_DATA toc;
        if (!cdrom_drive) return 0;
        ioctl_cd_state = CD_STOPPED;        
        memset(&toc_ex,0,sizeof(toc_ex));
        memset(&toc,0,sizeof(toc));
        toc_ex.Format=CDROM_READ_TOC_EX_FORMAT_SESSION;
        toc_ex.Msf=msf;
        toc_ex.SessionTrack=0;
        ioctl_open(0);
        DeviceIoControl(hIOCTL,IOCTL_CDROM_READ_TOC_EX, &toc_ex,sizeof(toc_ex),&toc,sizeof(toc),(PDWORD)&size,NULL);
        ioctl_close();
//        pclog("Read TOC session - %i %02X %02X %i %i %02X %02X %02X\n",size,toc.Length[0],toc.Length[1],toc.FirstCompleteSession,toc.LastCompleteSession,toc.TrackData[0].Adr,toc.TrackData[0].Control,toc.TrackData[0].TrackNumber);
        b[2]=toc.FirstCompleteSession;
        b[3]=toc.LastCompleteSession;
        b[len++]=0; /*Reserved*/
        b[len++]=(toc.TrackData[0].Adr<<4)|toc.TrackData[0].Control;
        b[len++]=toc.TrackData[0].TrackNumber;
        b[len++]=0; /*Reserved*/
        if (msf)
        {
                b[len++]=toc.TrackData[0].Address[0];
                b[len++]=toc.TrackData[0].Address[1];
                b[len++]=toc.TrackData[0].Address[2];
                b[len++]=toc.TrackData[0].Address[3];
        }
        else
        {
                temp=MSFtoLBA(toc.TrackData[0].Address[1],toc.TrackData[0].Address[2],toc.TrackData[0].Address[3]);
                b[len++]=temp>>24;
                b[len++]=temp>>16;
                b[len++]=temp>>8;
                b[len++]=temp;
        }

	return len;
}

static int ioctl_readtoc_raw(unsigned char *b, int maxlen)
{
        int len=4;
        int size;
        uint32_t temp;
	int i;
	int BytesRead = 0;
        CDROM_READ_TOC_EX toc_ex;
        CDROM_TOC_FULL_TOC_DATA toc;
        if (!cdrom_drive) return 0;
        ioctl_cd_state = CD_STOPPED;        
        memset(&toc_ex,0,sizeof(toc_ex));
        memset(&toc,0,sizeof(toc));
        toc_ex.Format=CDROM_READ_TOC_EX_FORMAT_FULL_TOC;
        toc_ex.Msf=1;
        toc_ex.SessionTrack=0;
        ioctl_open(0);
        DeviceIoControl(hIOCTL,IOCTL_CDROM_READ_TOC_EX, &toc_ex,sizeof(toc_ex),&toc,sizeof(toc),(PDWORD)&size,NULL);
        ioctl_close();
//        pclog("Read TOC session - %i %02X %02X %i %i %02X %02X %02X\n",size,toc.Length[0],toc.Length[1],toc.FirstCompleteSession,toc.LastCompleteSession,toc.TrackData[0].Adr,toc.TrackData[0].Control,toc.TrackData[0].TrackNumber);
        b[2]=toc.FirstCompleteSession;
        b[3]=toc.LastCompleteSession;

	size -= sizeof(CDROM_TOC_FULL_TOC_DATA);
	size /= sizeof(toc.Descriptors[0]);

	for (i = 0; i <= size; i++)
	{
                b[len++]=toc.Descriptors[i].SessionNumber;
                b[len++]=(toc.Descriptors[i].Adr<<4)|toc.Descriptors[i].Control;
                b[len++]=0;
                b[len++]=toc.Descriptors[i].Reserved1; /*Reserved*/
		b[len++]=toc.Descriptors[i].MsfExtra[0];
		b[len++]=toc.Descriptors[i].MsfExtra[1];
		b[len++]=toc.Descriptors[i].MsfExtra[2];
		b[len++]=toc.Descriptors[i].Zero;
		b[len++]=toc.Descriptors[i].Msf[0];
		b[len++]=toc.Descriptors[i].Msf[1];
		b[len++]=toc.Descriptors[i].Msf[2];
	}

	return len;
}

static uint32_t ioctl_size()
{
        return cdrom_capacity;
}

static int ioctl_status()
{
	if (!(ioctl_ready) && (cdrom_drive <= 0))
                return CD_STATUS_EMPTY;

	switch(ioctl_cd_state)
	{
		case CD_PLAYING:
			return CD_STATUS_PLAYING;
		case CD_PAUSED:
			return CD_STATUS_PAUSED;
		case CD_STOPPED:
			return CD_STATUS_STOPPED;
	}
}

void ioctl_reset()
{
        CDROM_TOC ltoc;
        int temp;
        long size;

        if (!cdrom_drive)
        {
                tocvalid = 0;
                return;
        }
        
        ioctl_open(0);
        temp = DeviceIoControl(hIOCTL, IOCTL_CDROM_READ_TOC, NULL, 0, &ltoc, sizeof(ltoc), &size, NULL);
        ioctl_close();

        toc = ltoc;
        tocvalid = 1;
}

int ioctl_open(char d)
{
//        char s[8];
        if (!ioctl_inited)
        {
                sprintf(ioctl_path,"\\\\.\\%c:",d);
                pclog("Path is %s\n",ioctl_path);
                tocvalid=0;
        }
//        pclog("Opening %s\n",ioctl_path);
	hIOCTL	= CreateFile(/*"\\\\.\\g:"*/ioctl_path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
	if (!hIOCTL)
	{
                //fatal("IOCTL");
        }
        atapi=&ioctl_atapi;
        if (!ioctl_inited)
        {
                ioctl_inited=1;
                CloseHandle(hIOCTL);
                hIOCTL = NULL;
        }
        return 0;
}

void ioctl_close(void)
{
        if (hIOCTL)
        {
                CloseHandle(hIOCTL);
                hIOCTL = NULL;
        }
}

static void ioctl_exit(void)
{
        ioctl_stop();
        ioctl_inited=0;
        tocvalid=0;
}

static ATAPI ioctl_atapi=
{
        ioctl_ready,
	ioctl_medium_changed,
        ioctl_readtoc,
        ioctl_readtoc_session,
	ioctl_readtoc_raw,
        ioctl_getcurrentsubchannel,
        ioctl_readsector,
	ioctl_readsector_raw,
        ioctl_playaudio,
        ioctl_seek,
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
