/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include "ibm.h"
#include "cdrom.h"
#include "cdrom-ioctl.h"

int cdrom_drive;

static CDROM null_cdrom;

void cdrom_null_audio_callback(int16_t *output, int len)
{
        memset(output, 0, len * 2);
}

void cdrom_null_audio_stop()
{
}

static void null_playaudio(uint32_t pos, uint32_t len, int ismsf)
{
}

static void null_pause(void)
{
}

static void null_resume(void)
{
}

static void null_stop(void)
{
}

static void null_seek(uint32_t pos)
{
}

static int null_ready(void)
{
        return 0;
}

/* Always return 0, the contents of a null CD-ROM drive never change. */
static int null_medium_changed(void)
{
        return 0;
}

static uint8_t null_getcurrentsubchannel(uint8_t *b, int msf)
{
        return 0x13;
}

static void null_eject(void)
{
}

static void null_load(void)
{
}

static int null_sector_data_type(int sector, int ismsf)
{
	return 0;
}

static void null_readsector_raw(uint8_t *b, int sector, int ismsf)
{
}

static int null_readtoc(unsigned char *b, unsigned char starttrack, int msf, int maxlen, int single)
{
        return 0;
}

static int null_readtoc_session(unsigned char *b, int msf, int maxlen)
{
		return 0;
}

static int null_readtoc_raw(unsigned char *b, int msf, int maxlen)
{
		return 0;
}

static uint32_t null_size()
{
        return 0;
}

static int null_status()
{
	return CD_STATUS_EMPTY;
}

void cdrom_null_reset()
{
}

int cdrom_null_open(char d)
{
        cdrom = &null_cdrom;
        return 0;
}

void null_close(void)
{
}

static void null_exit(void)
{
}

static int null_is_track_audio(uint32_t pos, int ismsf)
{
	return 0;
}

static CDROM null_cdrom =
{
        null_ready,
		null_medium_changed,
        null_readtoc,
        null_readtoc_session,
		null_readtoc_raw,
        null_getcurrentsubchannel,
        NULL,
        NULL,
        NULL,
		NULL,
		null_sector_data_type,
		null_readsector_raw,
        null_playaudio,
        null_seek,
        null_load,
        null_eject,
        null_pause,
        null_resume,
        null_size,
		null_status,
		null_is_track_audio,
        null_stop,
        null_exit
};
