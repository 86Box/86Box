/* Copyright holders: RichardG867, Tenshi
   see COPYING for more details
*/
/*ISO CD-ROM support*/

#include "ibm.h"
#include "cdrom.h"
#include "cdrom-iso.h"
#include <sys/stat.h>

static CDROM iso_cdrom;

uint32_t last_block = 0;
static uint64_t image_size = 0;
static int iso_inited = 0;
char iso_path[1024];
void iso_close(void);
static FILE* iso_image;
static int iso_changed = 0;

static uint32_t iso_cd_pos = 0, iso_cd_end = 0;

void iso_audio_callback(int16_t *output, int len)
{
    memset(output, 0, len * 2);
    return;
}

void iso_audio_stop()
{
    pclog("iso_audio_stop stub\n");
}

static int get_track_nr(uint32_t pos)
{
    pclog("get_track_nr stub\n");
    return 0;
}

static void iso_playaudio(uint32_t pos, uint32_t len, int ismsf)
{
    pclog("iso_playaudio stub\n");
    return;
}

static void iso_pause(void)
{
    pclog("iso_pause stub\n");
    return;
}

static void iso_resume(void)
{
    pclog("iso_resume stub\n");
    return;
}

static void iso_stop(void)
{
    pclog("iso_stop stub\n");
    return;
}

static void iso_seek(uint32_t pos)
{
    pclog("iso_seek stub\n");
    return;
}

static int iso_ready(void)
{
	if (strlen(iso_path) == 0)
	{
		return 0;
	}
	if (old_cdrom_drive != cdrom_drive)
	{
		// old_cdrom_drive = cdrom_drive;
		return 1;
	}
	
	if (iso_changed)
	{
		iso_changed = 0;
		return 1;
	}

    return 1;
}

/* Always return 0, because there is no way to change the ISO without unmounting and remounting it. */
static int iso_medium_changed(void)
{
	if (strlen(iso_path) == 0)
	{
		return 0;
	}
	if (old_cdrom_drive != cdrom_drive)
	{
		old_cdrom_drive = cdrom_drive;
		return 0;
	}
	
	if (iso_changed)
	{
		iso_changed = 0;
		return 0;
	}

	return 0;
}

static uint8_t iso_getcurrentsubchannel(uint8_t *b, int msf)
{
    pclog("iso_getcurrentsubchannel stub\n");
    return 0;
}

static void iso_eject(void)
{
    pclog("iso_eject stub\n");
}

static void iso_load(void)
{
    pclog("iso_load stub\n");
}

static void iso_readsector(uint8_t *b, int sector)
{
    if (!cdrom_drive) return;
    iso_image = fopen(iso_path, "rb");
    fseek(iso_image,sector*2048,SEEK_SET);
    fread(b,2048,1,iso_image);
    fclose(iso_image);
}

static void lba_to_msf(uint8_t *buf, int lba)
{
	double dlba = (double) lba + 150;
	buf[2] = (uint8_t) (((uint32_t) dlba) % 75);
	dlba /= 75;
	buf[1] = (uint8_t) (((uint32_t) dlba) % 60);
	dlba /= 60;
	buf[0] = (uint8_t) dlba;
}

#if 0
static void lba_to_msf(uint8_t *buf, int lba)
{
    lba += 150;
    buf[0] = (lba / 75) / 60;
    buf[1] = (lba / 75) % 60;
    buf[2] = lba % 75;
}
#endif

static void iso_readsector_raw(uint8_t *b, int sector)
{
    uint32_t temp;
    if (!cdrom_drive) return;
    iso_image = fopen(iso_path, "rb");
    fseek(iso_image, sector*2048, SEEK_SET);
    fread(b+16, 2048, 1, iso_image);
    fclose(iso_image);

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
}

static int iso_readtoc(unsigned char *buf, unsigned char start_track, int msf, int maxlen, int single)
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
    last_block = image_size >> 11;
    if (msf) {
        *q++ = 0; /* reserved */
        lba_to_msf(q, last_block);
        q += 3;
    } else {
        *q++ = last_block >> 24;
        *q++ = last_block >> 16;
        *q++ = last_block >> 8;
        *q++ = last_block;
    }
    len = q - buf;
    buf[0] = (uint8_t)(((len-2) >> 8) & 0xff);
    buf[1] = (uint8_t)((len-2) & 0xff);
    return len;
}

static int iso_readtoc_session(unsigned char *buf, int msf, int maxlen)
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

    return 12;
}

static int iso_readtoc_raw(unsigned char *buf, int maxlen)
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
    last_block = image_size >> 11;
    /* this is raw, must be msf */
	*q++ = 0; /* reserved */
    lba_to_msf(q, last_block);
    q += 3;

    *q++ = 1; /* session number */
    *q++ = 0x14; /* ADR, control */
    *q++ = 0;    /* track number */
    *q++ = 1;    /* point */
    *q++ = 0; /* min */
    *q++ = 0; /* sec */
    *q++ = 0; /* frame */
    /* same here */
    *q++ = 0;
    *q++ = 0;
    *q++ = 0;
    *q++ = 0;
    
    len = q - buf;
    buf[0] = (uint8_t)(((len-2) >> 8) & 0xff);
    buf[1] = (uint8_t)((len-2) & 0xff);
    return len;
}

static uint32_t iso_size()
{
    unsigned char b[4096];

    cdrom->readtoc(b, 0, 0, 4096, 0);
    
    return last_block;
}

static int iso_status()
{
	if (!(iso_ready()) && (cdrom_drive != 200))  return CD_STATUS_EMPTY;

	return CD_STATUS_DATA_ONLY;
}

void iso_reset()
{
}

int iso_open(char *fn)
{
    struct stat st;

	if (strcmp(fn, iso_path) != 0)
	{
		iso_changed = 1;
	}
	/* Make sure iso_changed stays when changing from ISO to another ISO. */
	if (!iso_inited && (cdrom_drive != 200))  iso_changed = 0;
    if (!iso_inited || iso_changed)
    {
        sprintf(iso_path, "%s", fn);
        pclog("Path is %s\n", iso_path);
    }
    iso_image = fopen(iso_path, "rb");
    cdrom = &iso_cdrom;
    if (!iso_inited || iso_changed)
    {
        if (!iso_inited)  iso_inited = 1;
        fclose(iso_image);
    }
    
    stat(iso_path, &st);
    image_size = st.st_size;
    
    return 0;
}

void iso_close(void)
{
    if (iso_image)  fclose(iso_image);
    memset(iso_path, 0, 1024);
}

static void iso_exit(void)
{
    // iso_stop();
    iso_inited = 0;
}

static int iso_is_track_audio(uint32_t pos, int ismsf)
{
	return 0;
}

static CDROM iso_cdrom = 
{
        iso_ready,
		iso_medium_changed,
        iso_readtoc,
        iso_readtoc_session,
        iso_readtoc_raw,
        iso_getcurrentsubchannel,
        iso_readsector,
        iso_readsector_raw,
        iso_playaudio,
        iso_seek,
        iso_load,
        iso_eject,
        iso_pause,
        iso_resume,
        iso_size,
		iso_status,
		iso_is_track_audio,
        iso_stop,
        iso_exit
};
