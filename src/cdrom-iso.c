/* Copyright holders: RichardG867, Tenshi
   see COPYING for more details
*/
/*ISO CD-ROM support*/

#include <stdarg.h>

#include "ibm.h"
#include "cdrom.h"
#include "cdrom-iso.h"

#define __USE_LARGEFILE64
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE

#include <sys/stat.h>

static CDROM iso_cdrom;

int cdrom_iso_do_log = 0;

void cdrom_iso_log(const char *format, ...)
{
#ifdef ENABLE_CDROM_ISO_LOG
   if (cdrom_iso_do_log)
   {
		va_list ap;
		va_start(ap, format);
		vprintf(format, ap);
		va_end(ap);
		fflush(stdout);
   }
#endif
}

void iso_close(uint8_t id);

void iso_audio_callback(uint8_t id, int16_t *output, int len)
{
    memset(output, 0, len * 2);
    return;
}

void iso_audio_stop(uint8_t id)
{
    return;
}

static int iso_ready(uint8_t id)
{
	if (strlen(cdrom_iso[id].iso_path) == 0)
	{
		return 0;
	}
	if (cdrom_drives[id].prev_host_drive != cdrom_drives[id].host_drive)
	{
		return 1;
	}
	
	if (cdrom_iso[id].iso_changed)
	{
		cdrom_iso[id].iso_changed = 0;
		return 1;
	}

    return 1;
}

static int iso_medium_changed(uint8_t id)
{
	if (strlen(cdrom_iso[id].iso_path) == 0)
	{
		return 0;
	}

	if (cdrom_drives[id].prev_host_drive != cdrom_drives[id].host_drive)
	{
		cdrom_drives[id].prev_host_drive = cdrom_drives[id].host_drive;
		return 1;
	}
	
	if (cdrom_iso[id].iso_changed)
	{
		cdrom_iso[id].iso_changed = 0;
		return 1;
	}

	return 0;
}

static void lba_to_msf(uint8_t *buf, int lba)
{
    lba += 150;
    buf[0] = (lba / 75) / 60;
    buf[1] = (lba / 75) % 60;
	buf[2] = lba % 75;
}

static uint8_t iso_getcurrentsubchannel(uint8_t id, uint8_t *b, int msf)
{
	int pos=0;
	int32_t temp;
	if (strlen(cdrom_iso[id].iso_path) == 0)
	{
		return 0;
	}
        
	b[pos++]=0;
	b[pos++]=0;
	b[pos++]=0;
        
	temp = cdrom[id].seek_pos;
	if (msf)
	{
		memset(&(b[pos]), 0, 8);
		lba_to_msf(&(b[pos]), temp);
		pos += 4;
		lba_to_msf(&(b[pos]), temp);
		pos += 4;
	}
	else
	{
		b[pos++] = temp >> 24;
		b[pos++] = temp >> 16;
		b[pos++] = temp >> 8;
		b[pos++] = temp;
		b[pos++] = temp >> 24;
		b[pos++] = temp >> 16;
		b[pos++] = temp >> 8;
		b[pos++] = temp;
	}

	return 0x15;
}

static void iso_eject(uint8_t id)
{
    return;
}

static void iso_load(uint8_t id)
{
    return;
}

static int iso_sector_data_type(uint8_t id, int sector, int ismsf)
{
	return 2;	/* Always Mode 1 */
}

static void iso_readsector(uint8_t id, uint8_t *b, int sector)
{
	uint64_t file_pos = sector;
	if (!cdrom_drives[id].host_drive)
	{
		return;
	}
	file_pos <<= 11;
	memset(b, 0, 2856);
	cdrom_iso[id].iso_image = fopen(cdrom_iso[id].iso_path, "rb");
	fseeko64(cdrom_iso[id].iso_image, file_pos, SEEK_SET);
	fread(b + 16, 2048, 1, cdrom_iso[id].iso_image);
	fclose(cdrom_iso[id].iso_image);

	/* sync bytes */
	b[0] = 0;
	memset(b + 1, 0xff, 10);
	b[11] = 0;
	b += 12;
	lba_to_msf(b, sector);
	b[3] = 1; /* mode 1 data */
	b += 4;
	b += 2048;
	memset(b, 0, 288);
	b += 288;
	memset(b, 0, 392);
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
	uint8_t raw_data[2352];
} sector_data_t;

typedef struct __attribute__((__packed__))
{
	uint8_t sync[12];
	uint8_t header[4];
	sector_data_t data;
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

static int iso_readsector_raw(uint8_t id, uint8_t *buffer, int sector, int ismsf, int cdrom_sector_type, int cdrom_sector_flags, int *len)
{
	uint8_t *b;
	uint8_t *temp_b;
	int real_pos;
	
	b = temp_b = buffer;

	*len = 0;
	
	if (ismsf)
	{
		real_pos = cdrom_lba_to_msf_accurate(sector);
	}
	else
	{
		real_pos = sector;
	}

	memset(cdrom_sector_buffer.buffer, 0, 2856);

	if ((cdrom_sector_type == 1) || (cdrom_sector_type > 2))
	{
		if (cdrom_sector_type == 1)
		{
			cdrom_iso_log("CD-ROM %i: Attempting to read an audio sector from an ISO\n", id);
		}
		if (cdrom_sector_type >= 2)
		{
			cdrom_iso_log("CD-ROM %i: Attempting to read a non-mode 1 data sector from an ISO\n", id);
		}
		return 0;
	}

	if (!(cdrom_sector_flags & 0xf0))		/* 0x00 and 0x08 are illegal modes */
	{
		cdrom_iso_log("CD-ROM %i: 0x00 and 0x08 are illegal modes\n", id);
		return 0;
	}

	if ((cdrom_sector_flags & 0x06) == 0x06)
	{
		cdrom_iso_log("CD-ROM %i: Invalid error flags\n", id);
		return 0;
	}

	if (((cdrom_sector_flags & 0x700) == 0x300) || ((cdrom_sector_flags & 0x700) > 0x400))
	{
		cdrom_iso_log("CD-ROM %i: Invalid subchannel data flags (%02X)\n", id, cdrom_sector_flags & 0x700);
		return 0;
	}

	if ((cdrom_sector_flags & 0x18) == 0x08)		/* EDC/ECC without user data is an illegal mode */
	{
		cdrom_iso_log("CD-ROM %i: EDC/ECC without user data is an illegal mode\n", id);
		return 0;
	}

	iso_readsector(id, cdrom_sector_buffer.buffer, real_pos);

	cdrom_sector_size = 0;

	if (cdrom_sector_flags & 0x80)		/* Sync */
	{
		cdrom_iso_log("CD-ROM %i: Sync\n", id);
		memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.sync, 12);
		cdrom_sector_size += 12;
		temp_b += 12;
	}
	if (cdrom_sector_flags & 0x20)		/* Header */
	{
		cdrom_iso_log("CD-ROM %i: Header\n", id);
		memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.header, 4);
		cdrom_sector_size += 4;
		temp_b += 4;
	}
		
	/* Mode 1 sector, expected type is 1 type. */
	if (cdrom_sector_flags & 0x40)		/* Sub-header */
	{
		if (!(cdrom_sector_flags & 0x10))		/* No user data */
		{
			cdrom_iso_log("CD-ROM %i: Sub-header\n", id);
			memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.data.m1_data.user_data, 8);
			cdrom_sector_size += 8;
			temp_b += 8;
		}
	}
	if (cdrom_sector_flags & 0x10)		/* User data */
	{
		cdrom_iso_log("CD-ROM %i: User data\n", id);
		memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.data.m1_data.user_data, 2048);
		cdrom_sector_size += 2048;
		temp_b += 2048;
	}
	if (cdrom_sector_flags & 0x08)		/* EDC/ECC */
	{
		cdrom_iso_log("CD-ROM %i: EDC/ECC\n", id);
		memcpy(temp_b, cdrom_sector_buffer.cdrom_sector.data.m1_data.ecc, 288);
		cdrom_sector_size += 288;
		temp_b += 288;
	}

	if ((cdrom_sector_flags & 0x06) == 0x02)
	{
		/* Add error flags. */
		cdrom_iso_log("CD-ROM %i: Error flags\n", id);
		memcpy(b + cdrom_sector_size, cdrom_sector_buffer.cdrom_sector.c2, 294);
		cdrom_sector_size += 294;
	}
	else if ((cdrom_sector_flags & 0x06) == 0x04)
	{
		/* Add error flags. */
		cdrom_iso_log("CD-ROM %i: Full error flags\n", id);
		memcpy(b + cdrom_sector_size, cdrom_sector_buffer.cdrom_sector.c2, 296);
		cdrom_sector_size += 296;
	}
	
	if ((cdrom_sector_flags & 0x700) == 0x100)
	{
		cdrom_iso_log("CD-ROM %i: Raw subchannel data\n", id);
		memcpy(b + cdrom_sector_size, cdrom_sector_buffer.cdrom_sector.subchannel_raw, 96);
		cdrom_sector_size += 96;
	}
	else if ((cdrom_sector_flags & 0x700) == 0x200)
	{
		cdrom_iso_log("CD-ROM %i: Q subchannel data\n", id);
		memcpy(b + cdrom_sector_size, cdrom_sector_buffer.cdrom_sector.subchannel_q, 16);
		cdrom_sector_size += 16;
	}
	else if ((cdrom_sector_flags & 0x700) == 0x400)
	{
		cdrom_iso_log("CD-ROM %i: R/W subchannel data\n", id);
		memcpy(b + cdrom_sector_size, cdrom_sector_buffer.cdrom_sector.subchannel_rw, 96);
		cdrom_sector_size += 96;
	}

	*len = cdrom_sector_size;
	
	return 1;
}

static int iso_readtoc(uint8_t id, unsigned char *buf, unsigned char start_track, int msf, int maxlen, int single)
{
    uint8_t *q;
    int len;

    if (start_track > 1 && start_track != 0xaa)
        return -1;
    q = buf + 2;
    *q++ = 1; /* first session */
    *q++ = 1; /* last session */
    if (start_track <= 1) {
        *q++ = 0; /* reserved */
        *q++ = 0x14; /* ADR, control */
        *q++ = 1;    /* track number */
        *q++ = 0; /* reserved */
        if (msf) {
            *q++ = 0; /* reserved */
            lba_to_msf(q, 0);
            q += 3;
        } else {
            /* sector 0 */
            *q++ = 0;
            *q++ = 0;
            *q++ = 0;
            *q++ = 0;
        }
    }
    /* lead out track */
    *q++ = 0; /* reserved */
    *q++ = 0x16; /* ADR, control */
    *q++ = 0xaa; /* track number */
    *q++ = 0; /* reserved */
    cdrom_iso[id].last_block = cdrom_iso[id].image_size >> 11;
    if (msf) {
        *q++ = 0; /* reserved */
        lba_to_msf(q, cdrom_iso[id].last_block);
        q += 3;
    } else {
        *q++ = cdrom_iso[id].last_block >> 24;
        *q++ = cdrom_iso[id].last_block >> 16;
        *q++ = cdrom_iso[id].last_block >> 8;
        *q++ = cdrom_iso[id].last_block;
    }
    len = q - buf;
	if (len > maxlen)
	{
		len = maxlen;
	}
    buf[0] = (uint8_t)(((len-2) >> 8) & 0xff);
    buf[1] = (uint8_t)((len-2) & 0xff);
    return len;
}

static int iso_readtoc_session(uint8_t id, unsigned char *buf, int msf, int maxlen)
{
    uint8_t *q;

    q = buf + 2;
    *q++ = 1; /* first session */
    *q++ = 1; /* last session */

    *q++ = 1; /* session number */
    *q++ = 0x14; /* data track */
    *q++ = 0; /* track number */
    *q++ = 0xa0; /* lead-in */
    *q++ = 0; /* min */
    *q++ = 0; /* sec */
    *q++ = 0; /* frame */
    *q++ = 0;

	if (maxlen < 12)
	{
		return maxlen;
	}
    return 12;
}

static int iso_readtoc_raw(uint8_t id, unsigned char *buf, int msf, int maxlen)
{
    uint8_t *q;
    int len;

    q = buf + 2;
    *q++ = 1; /* first session */
    *q++ = 1; /* last session */

    *q++ = 1; /* session number */
    *q++ = 0x14; /* data track */
    *q++ = 0; /* track number */
    *q++ = 0xa0; /* lead-in */
    *q++ = 0; /* min */
    *q++ = 0; /* sec */
    *q++ = 0; /* frame */
    *q++ = 0;
    *q++ = 1; /* first track */
    *q++ = 0x00; /* disk type */
    *q++ = 0x00;

    *q++ = 1; /* session number */
    *q++ = 0x14; /* data track */
    *q++ = 0; /* track number */
    *q++ = 0xa1;
    *q++ = 0; /* min */
    *q++ = 0; /* sec */
    *q++ = 0; /* frame */
    *q++ = 0;
    *q++ = 1; /* last track */
    *q++ = 0x00;
    *q++ = 0x00;

    *q++ = 1; /* session number */
    *q++ = 0x14; /* data track */
    *q++ = 0; /* track number */
    *q++ = 0xa2; /* lead-out */
    *q++ = 0; /* min */
    *q++ = 0; /* sec */
    *q++ = 0; /* frame */
    cdrom_iso[id].last_block = cdrom_iso[id].image_size >> 11;
    /* this is raw, must be msf */
	if (msf)
	{
		*q++ = 0; /* reserved */
		lba_to_msf(q, cdrom_iso[id].last_block);
		q += 3;
	}
	else
	{
		*q++ = (cdrom_iso[id].last_block >> 24) & 0xff;
		*q++ = (cdrom_iso[id].last_block >> 16) & 0xff;
		*q++ = (cdrom_iso[id].last_block >> 8) & 0xff;
		*q++ = cdrom_iso[id].last_block & 0xff;
	}

    *q++ = 1; /* session number */
    *q++ = 0x14; /* ADR, control */
    *q++ = 0;    /* track number */
    *q++ = 1;    /* point */
    *q++ = 0; /* min */
    *q++ = 0; /* sec */
    *q++ = 0; /* frame */
    /* same here */
	if (msf)
	{
		*q++ = 0; /* reserved */
		lba_to_msf(q, 0);
		q += 3;
	}
	else
	{
		*q++ = 0;
		*q++ = 0;
		*q++ = 0;
		*q++ = 0;
	}
    
    len = q - buf;
	if (len > maxlen)
	{
		len = maxlen;
	}
    buf[0] = (uint8_t)(((len-2) >> 8) & 0xff);
    buf[1] = (uint8_t)((len-2) & 0xff);
    return len;
}

static uint32_t iso_size(uint8_t id)
{
	uint64_t iso_size;

    cdrom_iso[id].iso_image = fopen(cdrom_iso[id].iso_path, "rb");
    fseeko64(cdrom_iso[id].iso_image, 0, SEEK_END);
	iso_size = ftello64(cdrom_iso[id].iso_image);
	iso_size >>= 11;
    fclose(cdrom_iso[id].iso_image);

	return (uint32_t) (iso_size);
}

static int iso_status(uint8_t id)
{
	if (!(iso_ready(id)) && (cdrom_drives[id].host_drive != 200))  return CD_STATUS_EMPTY;

	return CD_STATUS_DATA_ONLY;
}

void iso_reset(uint8_t id)
{
}

int iso_open(uint8_t id, char *fn)
{
    struct stat64 st;

	if (strcmp(fn, cdrom_iso[id].iso_path) != 0)
	{
		cdrom_iso[id].iso_changed = 1;
	}
	/* Make sure iso_changed stays when changing from ISO to another ISO. */
	if (!cdrom_iso[id].iso_inited && (cdrom_drives[id].host_drive != 200))  cdrom_iso[id].iso_changed = 0;
    if (!cdrom_iso[id].iso_inited || cdrom_iso[id].iso_changed)
    {
        sprintf(cdrom_iso[id].iso_path, "%s", fn);
    }
    cdrom_iso[id].iso_image = fopen(cdrom_iso[id].iso_path, "rb");
    cdrom_drives[id].handler = &iso_cdrom;
    if (!cdrom_iso[id].iso_inited || cdrom_iso[id].iso_changed)
    {
        if (!cdrom_iso[id].iso_inited)  cdrom_iso[id].iso_inited = 1;
        fclose(cdrom_iso[id].iso_image);
    }
    
    stat64(cdrom_iso[id].iso_path, &st);
    cdrom_iso[id].image_size = st.st_size;
    
    return 0;
}

void iso_close(uint8_t id)
{
    if (cdrom_iso[id].iso_image)  fclose(cdrom_iso[id].iso_image);
    memset(cdrom_iso[id].iso_path, 0, 1024);
}

static void iso_exit(uint8_t id)
{
    cdrom_iso[id].iso_inited = 0;
}

static int iso_is_track_audio(uint8_t id, uint32_t pos, int ismsf)
{
	return 0;
}

static int iso_media_type_id(uint8_t id)
{
	if (iso_size(id) <= 405000)
	{
		return 1;	/* Data CD. */
	}
	else
	{
		return 65;	/* DVD. */
	}
}

static CDROM iso_cdrom = 
{
        iso_ready,
		iso_medium_changed,
		iso_media_type_id,
		NULL,
		NULL,
        iso_readtoc,
        iso_readtoc_session,
        iso_readtoc_raw,
        iso_getcurrentsubchannel,
        NULL,
		iso_sector_data_type,
        iso_readsector_raw,
        NULL,
        iso_load,
        iso_eject,
        NULL,
        NULL,
        iso_size,
		iso_status,
		iso_is_track_audio,
        NULL,
        iso_exit
};
