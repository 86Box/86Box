/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
/*Linux CD-ROM support via IOCTL*/

#include <linux/cdrom.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include "ibm.h"
#include "ide.h"
#include "cdrom_ioctl.h"

static ATAPI ioctl_atapi;

static uint32_t last_block = 0;
static uint32_t cdrom_capacity = 0;
static int ioctl_inited = 0;
static char ioctl_path[8];
static int tocvalid = 0;
static struct cdrom_tocentry toc[100];
static int first_track, last_track;

int old_cdrom_drive;

#define MSFtoLBA(m,s,f)  (((((m*60)+s)*75)+f)-150)

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
	int fd;
	struct cdrom_read_audio read_audio;

//        pclog("Audio callback %08X %08X %i %i %i %04X %i\n", ioctl_cd_pos, ioctl_cd_end, ioctl_cd_state, cd_buflen, len, cd_buffer[4], GetTickCount());
        if (ioctl_cd_state != CD_PLAYING) 
        {
                memset(output, 0, len * 2);
                return;
        }
	fd = open("/dev/cdrom", O_RDONLY|O_NONBLOCK);

	if (fd <= 0)
        {
                memset(output, 0, len * 2);
                return;
        }

        while (cd_buflen < len)
        {
                if (ioctl_cd_pos < ioctl_cd_end)
                {
			read_audio.addr.lba = ioctl_cd_pos - 150;
			read_audio.addr_format = CDROM_LBA;
			read_audio.nframes = 1;
			read_audio.buf = (__u8 *)&cd_buffer[cd_buflen];
			
        		if (ioctl(fd, CDROMREADAUDIO, &read_audio) < 0)
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
                }
                else
                {
                        memset(&cd_buffer[cd_buflen], 0, (BUF_SIZE - cd_buflen) * 2);
                        ioctl_cd_state = CD_STOPPED;
                        cd_buflen = len;
                }
        }
	close(fd);
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

        for (c = first_track; c < last_track; c++)
        {
                uint32_t track_address = toc[c].cdte_addr.msf.frame +
                                         (toc[c].cdte_addr.msf.second * 75) +
                                         (toc[c].cdte_addr.msf.minute * 75 * 60);
//pclog("get_track_nr: track=%i pos=%x track_address=%x\n", c, pos, track_address);
                if (track_address <= pos)
                        track = c;
        }
        return track;
}

static int is_track_audio(uint32_t pos)
{
        int c;
        int control = 0;
        
        if (!tocvalid)
                return 0;

        for (c = first_track; c < last_track; c++)
        {
                uint32_t track_address = toc[c].cdte_addr.msf.frame +
                                         (toc[c].cdte_addr.msf.second * 75) +
                                         (toc[c].cdte_addr.msf.minute * 75 * 60);
//pclog("get_track_nr: track=%i pos=%x track_address=%x\n", c, pos, track_address);
                if (track_address <= pos)
                        control = toc[c].cdte_ctrl;
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

static void ioctl_playaudio(uint32_t pos, uint32_t len, int ismsf)
{
//        pclog("Play audio - %08X %08X %i\n", pos, len, ismsf);
        if (ismsf)
        {
                pos = (pos & 0xff) + (((pos >> 8) & 0xff) * 75) + (((pos >> 16) & 0xff) * 75 * 60);
                len = (len & 0xff) + (((len >> 8) & 0xff) * 75) + (((len >> 16) & 0xff) * 75 * 60);
//                pclog("MSF - pos = %08X len = %08X\n", pos, len);
        }
        else
        	len += pos;
        ioctl_cd_pos   = pos;// + 150;
        ioctl_cd_end   = pos+len;// + 150;
        ioctl_cd_state = CD_PLAYING;
	if (ioctl_cd_pos < 150)
		ioctl_cd_pos = 150;
//        pclog("Audio start %08X %08X %i %i %i\n", ioctl_cd_pos, ioctl_cd_end, ioctl_cd_state, 0, len);
}

static void ioctl_pause(void)
{
        if (ioctl_cd_state == CD_PLAYING)
        	ioctl_cd_state = CD_PAUSED;
}

static void ioctl_resume(void)
{
        if (ioctl_cd_state == CD_PAUSED)
        	ioctl_cd_state = CD_PLAYING;
}

static void ioctl_stop(void)
{
        ioctl_cd_state = CD_STOPPED;
}

static void ioctl_seek(uint32_t pos)
{
//        pclog("Seek %08X\n", pos);
        ioctl_cd_pos   = pos;
        ioctl_cd_state = CD_STOPPED;
}

static int read_toc(int fd, struct cdrom_tocentry *btoc)
{
	struct cdrom_tochdr toc_hdr;
	int track, err;
//pclog("read_toc\n");
	err = ioctl(fd, CDROMREADTOCHDR, &toc_hdr);
	if (err == -1)
	{
		pclog("read_toc: CDROMREADTOCHDR failed\n");
		return 0;
	}

	first_track = toc_hdr.cdth_trk0;
	last_track = toc_hdr.cdth_trk1;
//pclog("read_toc: first_track=%i last_track=%i\n", first_track, last_track);
	memset(btoc, 0, sizeof(cdrom_tocentry));

	for (track = toc_hdr.cdth_trk0; track <= toc_hdr.cdth_trk1; track++)
	{
		btoc[track].cdte_track = track;
		btoc[track].cdte_format = CDROM_MSF;
		err = ioctl(fd, CDROMREADTOCENTRY, &btoc[track]);
		if (err == -1)
		{
//			pclog("read_toc: CDROMREADTOCENTRY failed on track %i\n", track);
			return 0;
		}
//		pclog("read_toc: Track %02X - number %02X control %02X adr %02X address %02X %02X %02X %02X\n", track, toc[track].cdte_track, toc[track].cdte_ctrl, toc[track].cdte_adr, 0, toc[track].cdte_addr.msf.minute, toc[track].cdte_addr.msf.second, toc[track].cdte_addr.msf.frame);
	}
	return 1;
}

static int ioctl_ready(void)
{
        long size;
        int temp;
	struct cdrom_tochdr toc_hdr;
	struct cdrom_tocentry toc_entry;
	int err;
	int fd = open("/dev/cdrom", O_RDONLY|O_NONBLOCK);

	if (fd <= 0)
		return 0;

	err = ioctl(fd, CDROMREADTOCHDR, &toc_hdr);
	if (err == -1)
	{
		close(fd);
		return 0;
	}
//	pclog("CDROMREADTOCHDR: start track=%i end track=%i\n", toc_hdr.cdth_trk0, toc_hdr.cdth_trk1);
	toc_entry.cdte_track = toc_hdr.cdth_trk1;
	toc_entry.cdte_format = CDROM_MSF;
	err = ioctl(fd, CDROMREADTOCENTRY, &toc_entry);
	if (err == -1)
	{
		close(fd);
		return 0;
	}
//	pclog("CDROMREADTOCENTRY: addr=%02i:%02i:%02i\n", toc_entry.cdte_addr.msf.minute, toc_entry.cdte_addr.msf.second, toc_entry.cdte_addr.msf.frame);
        if ((toc_entry.cdte_addr.msf.minute != toc[toc_hdr.cdth_trk1].cdte_addr.msf.minute) ||
            (toc_entry.cdte_addr.msf.second != toc[toc_hdr.cdth_trk1].cdte_addr.msf.second) ||
            (toc_entry.cdte_addr.msf.frame  != toc[toc_hdr.cdth_trk1].cdte_addr.msf.frame ) ||
            !tocvalid)
        {
		int track;
                ioctl_cd_state = CD_STOPPED;

		tocvalid = read_toc(fd, toc);
		close(fd);
                return 1;
        }
	close(fd);
        return 1;
}

static int ioctl_get_last_block(unsigned char starttrack, int msf, int maxlen, int single)
{
        int c;
	int lb = 0;
	int tv = 0;
	struct cdrom_tocentry lbtoc[100];
	int fd = open("/dev/cdrom", O_RDONLY|O_NONBLOCK);

	if (fd <= 0)
		return 0;

        ioctl_cd_state = CD_STOPPED;

	tv = read_toc(fd, lbtoc);

	close(fd);

	if (!tv)
		return 0;

        last_block = 0;
        for (c = 0; c <= last_track; c++)
        {
                uint32_t address;
                address = MSFtoLBA(toc[c].cdte_addr.msf.minute, toc[c].cdte_addr.msf.second, toc[c].cdte_addr.msf.frame);
                if (address > last_block)
                        lb = address;
        }
        return lb;
}

static int ioctl_medium_changed(void)
{
        long size;
        int temp;
	struct cdrom_tochdr toc_hdr;
	struct cdrom_tocentry toc_entry;
	int err;
	int fd = open("/dev/cdrom", O_RDONLY|O_NONBLOCK);

	if (fd <= 0)
		return 0;

	err = ioctl(fd, CDROMREADTOCHDR, &toc_hdr);
	if (err == -1)
	{
		close(fd);
		return 0;
	}
	toc_entry.cdte_track = toc_hdr.cdth_trk1;
	toc_entry.cdte_format = CDROM_MSF;
	err = ioctl(fd, CDROMREADTOCENTRY, &toc_entry);
	if (err == -1)
	{
		close(fd);
		return 0;
	}
//	pclog("CDROMREADTOCENTRY: addr=%02i:%02i:%02i\n", toc_entry.cdte_addr.msf.minute, toc_entry.cdte_addr.msf.second, toc_entry.cdte_addr.msf.frame);
        if ((toc_entry.cdte_addr.msf.minute != toc[toc_hdr.cdth_trk1].cdte_addr.msf.minute) ||
            (toc_entry.cdte_addr.msf.second != toc[toc_hdr.cdth_trk1].cdte_addr.msf.second) ||
            (toc_entry.cdte_addr.msf.frame  != toc[toc_hdr.cdth_trk1].cdte_addr.msf.frame ))
	{
		cdrom_capacity = ioctl_get_last_block(0, 0, 4096, 0);
		return 1;
	}
        return 0;
}

static uint8_t ioctl_getcurrentsubchannel(uint8_t *b, int msf)
{
	struct cdrom_subchnl sub;
        uint32_t cdpos = ioctl_cd_pos;
        int track = get_track_nr(cdpos);
        uint32_t track_address = toc[track].cdte_addr.msf.frame +
                                (toc[track].cdte_addr.msf.second * 75) +
                                (toc[track].cdte_addr.msf.minute * 75 * 60);
	long size;
	int pos=0;
	int err;
	uint8_t ret;
//pclog("ioctl_getsubchannel: cdpos=%x track_address=%x track=%i\n", cdpos, track_address, track);
        if (ioctl_cd_state == CD_PLAYING)
		ret = 0x11;
	else if (ioctl_cd_state == CD_PAUSED)
		ret = 0x12;
	else
		ret = 0x13;

        b[pos++] = (toc[track].cdte_adr << 4) | toc[track].cdte_ctrl;
        b[pos++] = track;
        b[pos++] = 0;

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

        return ret;
}

static void ioctl_eject(void)
{
	int fd = open("/dev/cdrom", O_RDONLY|O_NONBLOCK);

	if (fd <= 0)
		return;

	ioctl(fd, CDROMEJECT);

	close(fd);
}

static void ioctl_load(void)
{
	int fd = open("/dev/cdrom", O_RDONLY|O_NONBLOCK);

	if (fd <= 0)
		return;

	ioctl(fd, CDROMEJECT);

	close(fd);

	cdrom_capacity = ioctl_get_last_block(0, 0, 4096, 0);
}

union
{
	struct cdrom_msf *msf;
	char b[CD_FRAMESIZE_RAW];
} raw_read_params;

static int lba_to_msf(int lba)
{
        return (((lba / 75) / 60) << 16) + (((lba / 75) % 60) << 8) + (lba % 75);
}

static int ioctl_sector_data_type(int sector, int ismsf)
{
	return 2;	/* Always Mode 1 */
}

static void ioctl_readsector_raw(uint8_t *b, int sector)
{
	int err;
	int imsf = lba_to_msf(sector);
	int cdrom = open("/dev/cdrom", O_RDONLY|O_NONBLOCK);

        if (cdrom <= 0)
		return;

	raw_read_params.msf = malloc(sizeof(struct cdrom_msf));
	raw_read_params.msf->cdmsf_frame0 = imsf & 0xff;
	raw_read_params.msf->cdmsf_sec0 = (imsf >> 8) & 0xff;
	raw_read_params.msf->cdmsf_min0 = (imsf >> 16) & 0xff;

	/* This will read the actual raw sectors from the disc. */
	err = ioctl(cdrom, CDROMREADRAW, (void *) &raw_read_params);
	if (err == -1)
	{
		pclog("read_toc: CDROMREADTOCHDR failed\n");
		return;
	}

	memcpy(b, raw_read_params.b, 2352);

	close(cdrom);

	free(raw_read_params.msf);
}

static int ioctl_readtoc(unsigned char *b, unsigned char starttrack, int msf, int maxlen, int single)
{
        int len=4;
        long size;
        int c,d;
        uint32_t temp;
	int fd = open("/dev/cdrom", O_RDONLY|O_NONBLOCK);

	if (fd <= 0)
		return 0;

        ioctl_cd_state = CD_STOPPED;

	tocvalid = read_toc(fd, toc);

	close(fd);

	if (!tocvalid)
		return 4;

//        pclog("Read TOC done! %i\n",single);
        b[2] = first_track;
        b[3] = last_track;
        d = 0;
//pclog("Read TOC starttrack=%i\n", starttrack);
        for (c = 1; c <= last_track; c++)
        {
                if (toc[c].cdte_track >= starttrack)
                {
                        d = c;
                        break;
                }
        }
        b[2] = toc[c].cdte_track;
        last_block = 0;
        for (c = d; c <= last_track; c++)
        {
                uint32_t address;
                if ((len + 8) > maxlen)
			break;
//                pclog("Len %i max %i Track %02X - %02X %02X %02i:%02i:%02i %08X\n",len,maxlen,toc[c].cdte_track,toc[c].cdte_adr,toc[c].cdte_ctrl,toc[c].cdte_addr.msf.minute, toc[c].cdte_addr.msf.second, toc[c].cdte_addr.msf.frame,MSFtoLBA(toc[c].cdte_addr.msf.minute, toc[c].cdte_addr.msf.second, toc[c].cdte_addr.msf.frame));
                b[len++] = 0; /*Reserved*/
                b[len++] = (toc[c].cdte_adr << 4) | toc[c].cdte_ctrl;
                b[len++] = toc[c].cdte_track;
                b[len++] = 0; /*Reserved*/
                address = MSFtoLBA(toc[c].cdte_addr.msf.minute, toc[c].cdte_addr.msf.second, toc[c].cdte_addr.msf.frame);
                if (address > last_block)
                        last_block = address;

                if (msf)
                {
                        b[len++] = 0;
                        b[len++] = toc[c].cdte_addr.msf.minute;
                        b[len++] = toc[c].cdte_addr.msf.second;
                        b[len++] = toc[c].cdte_addr.msf.frame;
                }
                else
                {
                        temp = MSFtoLBA(toc[c].cdte_addr.msf.minute, toc[c].cdte_addr.msf.second, toc[c].cdte_addr.msf.frame);
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
/*        pclog("Table of Contents (%i bytes) : \n", size);
        pclog("First track - %02X\n", first_track);
        pclog("Last  track - %02X\n", last_track);
        for (c = 0; c <= last_track; c++)
            pclog("Track %02X - number %02X control %02X adr %02X address %02X %02X %02X %02X\n", c, toc[c].cdte_track, toc[c].cdte_ctrl, toc[c].cdte_adr, 0, toc[c].cdte_addr.msf.minute, toc[c].cdte_addr.msf.second, toc[c].cdte_addr.msf.frame);
        for (c = 0;c <= last_track; c++)
            pclog("Track %02X - number %02X control %02X adr %02X address %06X\n", c, toc[c].cdte_track, toc[c].cdte_ctrl, toc[c].cdte_adr, MSFtoLBA(toc[c].cdte_addr.msf.minute, toc[c].cdte_addr.msf.second, toc[c].cdte_addr.msf.frame));*/
        return len;
}

static int ioctl_readtoc_session(unsigned char *b, int msf, int maxlen)
{
	struct cdrom_multisession session;
        int len = 4;
	int err;
	int fd = open("/dev/cdrom", O_RDONLY|O_NONBLOCK);

	if (fd <= 0)
		return 0;

	session.addr_format = CDROM_MSF;
	err = ioctl(fd, CDROMMULTISESSION, &session);

	if (err == -1)
	{
		close(fd);
		return 0;
	}

        b[2] = 0;
        b[3] = 0;
        b[len++] = 0; /*Reserved*/
        b[len++] = (toc[0].cdte_adr << 4) | toc[0].cdte_ctrl;
        b[len++] = toc[0].cdte_track;
        b[len++] = 0; /*Reserved*/
        if (msf)
        {
                b[len++] = 0;
                b[len++] = session.addr.msf.minute;
                b[len++] = session.addr.msf.second;
                b[len++] = session.addr.msf.frame;
        }
        else
        {
                uint32_t temp = MSFtoLBA(session.addr.msf.minute, session.addr.msf.second, session.addr.msf.frame);
                b[len++] = temp >> 24;
                b[len++] = temp >> 16;
                b[len++] = temp >> 8;
                b[len++] = temp;
        }

	return len;
}

static int ioctl_readtoc_raw(unsigned char *b, int maxlen)
{
	struct cdrom_tochdr toc_hdr;
	struct cdrom_tocentry toc2[100];
	int track, err;
	int len = 4;
	int fd = open("/dev/cdrom", O_RDONLY|O_NONBLOCK);
//pclog("read_toc\n");
	if (fd <= 0)
		return 0;

	err = ioctl(fd, CDROMREADTOCHDR, &toc_hdr);
	if (err == -1)
	{
		pclog("read_toc: CDROMREADTOCHDR failed\n");
		return 0;
	}

	b[2] = toc_hdr.cdth_trk0;
	b[3] = toc_hdr.cdth_trk1;

	//pclog("read_toc: first_track=%i last_track=%i\n", first_track, last_track);
	memset(toc, 0, sizeof(toc));

	for (track = toc_hdr.cdth_trk0; track <= toc_hdr.cdth_trk1; track++)
	{
		if ((len + 11) > maxlen)
		{
			pclog("ioctl_readtocraw: This iteration would fill the buffer beyond the bounds, aborting...\n");
			close(fd);
			return len;
		}

		toc2[track].cdte_track = track;
		toc2[track].cdte_format = CDROM_MSF;
		err = ioctl(fd, CDROMREADTOCENTRY, &toc2[track]);
		if (err == -1)
		{
//			pclog("read_toc: CDROMREADTOCENTRY failed on track %i\n", track);
			close(fd);
			return 0;
		}
//		pclog("read_toc: Track %02X - number %02X control %02X adr %02X address %02X %02X %02X %02X\n", track, toc[track].cdte_track, toc[track].cdte_ctrl, toc[track].cdte_adr, 0, toc[track].cdte_addr.msf.minute, toc[track].cdte_addr.msf.second, toc[track].cdte_addr.msf.frame);

		b[len++] = toc2[track].cdte_track;
		b[len++]= (toc2[track].cdte_adr << 4) | toc[track].cdte_ctrl;
		b[len++]=0;
		b[len++]=0;
		b[len++]=0;
		b[len++]=0;
		b[len++]=0;
		b[len++]=0;
		b[len++] = toc2[track].cdte_addr.msf.minute;
		b[len++] = toc2[track].cdte_addr.msf.second;
		b[len++] = toc2[track].cdte_addr.msf.frame;
	}
	close(fd);
	return len;
}

static uint32_t ioctl_size()
{
	return cdrom_capacity;
}

static int ioctl_status()
{
	if (!(ioctl_ready) && (cdrom_drive <= 0))  return CD_STATUS_EMPTY;

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
	int fd = open("/dev/cdrom", O_RDONLY|O_NONBLOCK);
//pclog("ioctl_reset: fd=%i\n", fd);
	tocvalid = 0;

	if (fd <= 0)
		return;

	tocvalid = read_toc(fd, toc);

	close(fd);
}

int ioctl_open(char d)
{
	atapi=&ioctl_atapi;
        return 0;
}

void ioctl_close(void)
{
}

static void ioctl_exit(void)
{
        ioctl_stop();
        ioctl_inited = 0;
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
        NULL,
        NULL,
        NULL,
		ioctl_sector_data_type,
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
