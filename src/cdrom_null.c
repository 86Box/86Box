/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the CD-ROM null interface for unmounted
 *		guest CD-ROM drives.
 *
 * Version:	@(#)cdrom_null.c	1.0.0	2017/05/30
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2016 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */

#include "ibm.h"
#include "cdrom.h"
#include "cdrom_ioctl.h"

static CDROM null_cdrom;

static int null_ready(uint8_t id)
{
        return 0;
}

/* Always return 0, the contents of a null CD-ROM drive never change. */
static int null_medium_changed(uint8_t id)
{
        return 0;
}

static uint8_t null_getcurrentsubchannel(uint8_t id, uint8_t *b, int msf)
{
        return 0x13;
}

static void null_eject(uint8_t id)
{
}

static void null_load(uint8_t id)
{
}

static int null_readsector_raw(uint8_t id, uint8_t *buffer, int sector, int ismsf, int cdrom_sector_type, int cdrom_sector_flags, int *len)
{
	*len = 0;
	return 0;
}

static int null_readtoc(uint8_t id, unsigned char *b, unsigned char starttrack, int msf, int maxlen, int single)
{
        return 0;
}

static int null_readtoc_session(uint8_t id, unsigned char *b, int msf, int maxlen)
{
		return 0;
}

static int null_readtoc_raw(uint8_t id, unsigned char *b, int maxlen)
{
		return 0;
}

static uint32_t null_size(uint8_t id)
{
        return 0;
}

static int null_status(uint8_t id)
{
	return CD_STATUS_EMPTY;
}

void cdrom_null_reset(uint8_t id)
{
}

void cdrom_set_null_handler(uint8_t id);

int cdrom_null_open(uint8_t id, char d)
{
	cdrom_set_null_handler(id);
        return 0;
}

void null_close(uint8_t id)
{
}

static void null_exit(uint8_t id)
{
}

static int null_is_track_audio(uint8_t id, uint32_t pos, int ismsf)
{
	return 0;
}

static int null_pass_through(uint8_t id, uint8_t *in_cdb, uint8_t *b, uint32_t *len)
{
	return 0;
}

static int null_media_type_id(uint8_t id)
{
	return 0x70;
}

void cdrom_set_null_handler(uint8_t id)
{
	cdrom_drives[id].handler = &null_cdrom;
	cdrom_drives[id].host_drive = 0;
	memset(cdrom_image[id].image_path, 0, sizeof(cdrom_image[id].image_path));
}

static CDROM null_cdrom =
{
	null_ready,
	null_medium_changed,
	null_media_type_id,
	NULL,
	NULL,
        null_readtoc,
        null_readtoc_session,
	null_readtoc_raw,
        null_getcurrentsubchannel,
        null_pass_through,
	null_readsector_raw,
        NULL,
        null_load,
        null_eject,
        NULL,
        NULL,
        null_size,
	null_status,
	null_is_track_audio,
        NULL,
        null_exit
};
