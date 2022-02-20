/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of a generic Magneto-Optical Disk drive
 *		commands, for both ATAPI and SCSI usage.
 *
 *
 *
 * Authors:	Natalia Portillo <claunia@claunia.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2020,2021 Natalia Portillo.
 *		Copyright 2020,2021 Miran Grca.
 *		Copyright 2020,2021 Fred N. van Kempen
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/config.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/scsi_device.h>
#include <86box/nvr.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/mo.h>
#include <86box/version.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

mo_drive_t	mo_drives[MO_NUM];


/* Table of all SCSI commands and their flags, needed for the new disc change / not ready handler. */
const uint8_t mo_command_flags[0x100] =
{
    IMPLEMENTED | CHECK_READY | NONDATA,			/* 0x00 */
    IMPLEMENTED | ALLOW_UA | NONDATA | SCSI_ONLY,		/* 0x01 */
    0,
    IMPLEMENTED | ALLOW_UA,					/* 0x03 */
    IMPLEMENTED | CHECK_READY | ALLOW_UA | NONDATA | SCSI_ONLY,	/* 0x04 */
    0, 0, 0,
    IMPLEMENTED | CHECK_READY,					/* 0x08 */
    0,
    IMPLEMENTED | CHECK_READY,					/* 0x0A */
    IMPLEMENTED | CHECK_READY | NONDATA,			/* 0x0B */
    0, 0, 0, 0, 0, 0,
    IMPLEMENTED | ALLOW_UA,					/* 0x12 */
    IMPLEMENTED | CHECK_READY | NONDATA | SCSI_ONLY,		/* 0x13 */
    0,
    IMPLEMENTED,						/* 0x15 */
    IMPLEMENTED | SCSI_ONLY,					/* 0x16 */
    IMPLEMENTED | SCSI_ONLY,					/* 0x17 */
    0, 0,
    IMPLEMENTED,						/* 0x1A */
    IMPLEMENTED | CHECK_READY,					/* 0x1B */
    0,
    IMPLEMENTED,						/* 0x1D */
    IMPLEMENTED | CHECK_READY,					/* 0x1E */
    0, 0, 0, 0, 0, 0,
    IMPLEMENTED | CHECK_READY,					/* 0x25 */
    0, 0,
    IMPLEMENTED | CHECK_READY,					/* 0x28 */
    0,
    IMPLEMENTED | CHECK_READY,					/* 0x2A */
    IMPLEMENTED | CHECK_READY | NONDATA,			/* 0x2B */
    IMPLEMENTED | CHECK_READY | NONDATA,			/* 0x2C */
    0,
    IMPLEMENTED | CHECK_READY,					/* 0x2E */
    IMPLEMENTED | CHECK_READY | NONDATA | SCSI_ONLY,		/* 0x2F */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,
    0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    IMPLEMENTED,						/* 0x55 */
    0, 0, 0, 0,
    IMPLEMENTED,						/* 0x5A */
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    IMPLEMENTED | CHECK_READY,					/* 0xA8 */
    0,
    IMPLEMENTED | CHECK_READY,					/* 0xAA */
    0,
    IMPLEMENTED | CHECK_READY | NONDATA,			/* 0xAC */
    0,
    IMPLEMENTED | CHECK_READY,					/* 0xAE */
    IMPLEMENTED | CHECK_READY | NONDATA | SCSI_ONLY,		/* 0xAF */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,
    0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static uint64_t mo_mode_sense_page_flags = (GPMODEP_ALL_PAGES);


static const mode_sense_pages_t mo_mode_sense_pages_default =
{   {
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 }
}   };

static const mode_sense_pages_t mo_mode_sense_pages_default_scsi =
{   {
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 }
}   };


static const mode_sense_pages_t mo_mode_sense_pages_changeable =
{   {
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 }
}   };


static void	mo_command_complete(mo_t *dev);
static void	mo_init(mo_t *dev);


#ifdef ENABLE_MO_LOG
int mo_do_log = ENABLE_MO_LOG;


static void
mo_log(const char *fmt, ...)
{
    va_list ap;

    if (mo_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define mo_log(fmt, ...)
#endif


int
find_mo_for_channel(uint8_t channel)
{
    uint8_t i = 0;

    for (i = 0; i < MO_NUM; i++) {
	if ((mo_drives[i].bus_type == MO_BUS_ATAPI) && (mo_drives[i].ide_channel == channel))
		return i;
    }
    return 0xff;
}


static int
mo_load_abort(mo_t *dev)
{
    if (dev->drv->f)
	fclose(dev->drv->f);
    dev->drv->f = NULL;
    dev->drv->medium_size = 0;
    dev->drv->sector_size = 0;
    mo_eject(dev->id);	/* Make sure the host OS knows we've rejected (and ejected) the image. */
    return 0;
}


int
image_is_mdi(const char *s)
{
    if (! strcasecmp(plat_get_extension((char *) s), "MDI"))
	return 1;
    else
	return 0;
}


int
mo_load(mo_t *dev, char *fn)
{
    int is_mdi;
    uint32_t size = 0;
    unsigned int i, found = 0;

    is_mdi = image_is_mdi(fn);

    dev->drv->f = plat_fopen(fn, dev->drv->read_only ? "rb" : "rb+");
    if (!dev->drv->f) {
	if (!dev->drv->read_only) {
		dev->drv->f = plat_fopen(fn, "rb");
		if (dev->drv->f)
			dev->drv->read_only = 1;
		else
			return mo_load_abort(dev);
	} else
		return mo_load_abort(dev);
    }

    fseek(dev->drv->f, 0, SEEK_END);
    size = (uint32_t) ftell(dev->drv->f);

    if (is_mdi) {
	/* This is a MDI image. */
	size -= 0x1000LL;
	dev->drv->base = 0x1000;
    }

    for (i = 0; i < KNOWN_MO_TYPES; i++) {
	if (size == (mo_types[i].sectors * mo_types[i].bytes_per_sector)) {
	    found = 1;
	    dev->drv->medium_size = mo_types[i].sectors;
	    dev->drv->sector_size = mo_types[i].bytes_per_sector;
	    break;
	}
    }

    if (!found)
	return mo_load_abort(dev);

    if (fseek(dev->drv->f, dev->drv->base, SEEK_SET) == -1)
	fatal("mo_load(): Error seeking to the beginning of the file\n");

    strncpy(dev->drv->image_path, fn, sizeof(dev->drv->image_path) - 1);

    return 1;
}


void
mo_disk_reload(mo_t *dev)
{
    int ret = 0;

    if (strlen(dev->drv->prev_image_path) == 0)
	return;
    else
	ret = mo_load(dev, dev->drv->prev_image_path);

    if (ret)
	dev->unit_attention = 1;
}


void
mo_disk_unload(mo_t *dev)
{
    if (dev->drv->f) {
	fclose(dev->drv->f);
	dev->drv->f = NULL;
    }
}


void
mo_disk_close(mo_t *dev)
{
    if (dev->drv->f) {
	mo_disk_unload(dev);

	memcpy(dev->drv->prev_image_path, dev->drv->image_path, sizeof(dev->drv->prev_image_path));
	memset(dev->drv->image_path, 0, sizeof(dev->drv->image_path));

	dev->drv->medium_size = 0;
    }
}


static void
mo_set_callback(mo_t *dev)
{
    if (dev->drv->bus_type != MO_BUS_SCSI)
	ide_set_callback(ide_drives[dev->drv->ide_channel], dev->callback);
}


static void
mo_init(mo_t *dev)
{
    if (dev->id >= MO_NUM)
	return;

    dev->requested_blocks = 1;
    dev->sense[0] = 0xf0;
    dev->sense[7] = 10;
    dev->drv->bus_mode = 0;
    if (dev->drv->bus_type >= MO_BUS_ATAPI)
	dev->drv->bus_mode |= 2;
    if (dev->drv->bus_type < MO_BUS_SCSI)
	dev->drv->bus_mode |= 1;
    mo_log("MO %i: Bus type %i, bus mode %i\n", dev->id, dev->drv->bus_type, dev->drv->bus_mode);
    if (dev->drv->bus_type < MO_BUS_SCSI) {
	dev->phase = 1;
	dev->request_length = 0xEB14;
    }
    dev->status = READY_STAT | DSC_STAT;
    dev->pos = 0;
    dev->packet_status = PHASE_NONE;
    mo_sense_key = mo_asc = mo_ascq = dev->unit_attention = 0;
}


static int
mo_supports_pio(mo_t *dev)
{
    return (dev->drv->bus_mode & 1);
}


static int
mo_supports_dma(mo_t *dev)
{
    return (dev->drv->bus_mode & 2);
}


/* Returns: 0 for none, 1 for PIO, 2 for DMA. */
static int
mo_current_mode(mo_t *dev)
{
    if (!mo_supports_pio(dev) && !mo_supports_dma(dev))
	return 0;
    if (mo_supports_pio(dev) && !mo_supports_dma(dev)) {
	mo_log("MO %i: Drive does not support DMA, setting to PIO\n", dev->id);
	return 1;
    }
    if (!mo_supports_pio(dev) && mo_supports_dma(dev))
	return 2;
    if (mo_supports_pio(dev) && mo_supports_dma(dev)) {
	mo_log("MO %i: Drive supports both, setting to %s\n", dev->id, (dev->features & 1) ? "DMA" : "PIO");
	return (dev->features & 1) ? 2 : 1;
    }

    return 0;
}


/* Translates ATAPI phase (DRQ, I/O, C/D) to SCSI phase (MSG, C/D, I/O). */
int
mo_atapi_phase_to_scsi(mo_t *dev)
{
    if (dev->status & 8) {
	switch (dev->phase & 3) {
		case 0:
			return 0;
		case 1:
			return 2;
		case 2:
			return 1;
		case 3:
			return 7;
	}
    } else {
	if ((dev->phase & 3) == 3)
		return 3;
	else
		return 4;
    }

    return 0;
}


static void
mo_mode_sense_load(mo_t *dev)
{
    FILE *f;
    char file_name[512];

    memset(&dev->ms_pages_saved, 0, sizeof(mode_sense_pages_t));
    if (mo_drives[dev->id].bus_type == MO_BUS_SCSI)
	memcpy(&dev->ms_pages_saved, &mo_mode_sense_pages_default_scsi, sizeof(mode_sense_pages_t));
    else
	memcpy(&dev->ms_pages_saved, &mo_mode_sense_pages_default, sizeof(mode_sense_pages_t));

    memset(file_name, 0, 512);
    if (dev->drv->bus_type == MO_BUS_SCSI)
	sprintf(file_name, "scsi_mo_%02i_mode_sense_bin", dev->id);
    else
	sprintf(file_name, "mo_%02i_mode_sense_bin", dev->id);
    f = plat_fopen(nvr_path(file_name), "rb");
    if (f) {
	/* Nothing to read, not used by MO. */
	fclose(f);
    }
}


static void
mo_mode_sense_save(mo_t *dev)
{
    FILE *f;
    char file_name[512];

    memset(file_name, 0, 512);
    if (dev->drv->bus_type == MO_BUS_SCSI)
	sprintf(file_name, "scsi_mo_%02i_mode_sense_bin", dev->id);
    else
	sprintf(file_name, "mo_%02i_mode_sense_bin", dev->id);
    f = plat_fopen(nvr_path(file_name), "wb");
    if (f) {
	/* Nothing to write, not used by MO. */
	fclose(f);
    }
}


/*SCSI Mode Sense 6/10*/
static uint8_t
mo_mode_sense_read(mo_t *dev, uint8_t page_control, uint8_t page, uint8_t pos)
{
    switch (page_control) {
	case 0:
	case 3:
		return dev->ms_pages_saved.pages[page][pos];
		break;
	case 1:
		return mo_mode_sense_pages_changeable.pages[page][pos];
		break;
	case 2:
		if (dev->drv->bus_type == MO_BUS_SCSI)
			return mo_mode_sense_pages_default_scsi.pages[page][pos];
		else
			return mo_mode_sense_pages_default.pages[page][pos];
		break;
    }

    return 0;
}


static uint32_t
mo_mode_sense(mo_t *dev, uint8_t *buf, uint32_t pos, uint8_t page, uint8_t block_descriptor_len)
{
    uint64_t pf;
    uint8_t page_control = (page >> 6) & 3;

    pf = mo_mode_sense_page_flags;

    int i = 0;
    int j = 0;

    uint8_t msplen;

    page &= 0x3f;

    if (block_descriptor_len) {
	buf[pos++] = ((dev->drv->medium_size >> 24) & 0xff);
	buf[pos++] = ((dev->drv->medium_size >> 16) & 0xff);
	buf[pos++] = ((dev->drv->medium_size >>  8) & 0xff);
	buf[pos++] = ( dev->drv->medium_size        & 0xff);
	buf[pos++] = 0;		/* Reserved. */
	buf[pos++] = 0;
	buf[pos++] = ((dev->drv->sector_size >>  8) & 0xff);
	buf[pos++] = ( dev->drv->sector_size        & 0xff);
    }

    for (i = 0; i < 0x40; i++) {
        if ((page == GPMODE_ALL_PAGES) || (page == i)) {
		if (pf & (1LL << ((uint64_t) page))) {
			buf[pos++] = mo_mode_sense_read(dev, page_control, i, 0);
			msplen = mo_mode_sense_read(dev, page_control, i, 1);
			buf[pos++] = msplen;
			mo_log("MO %i: MODE SENSE: Page [%02X] length %i\n", dev->id, i, msplen);
			for (j = 0; j < msplen; j++)
				buf[pos++] = mo_mode_sense_read(dev, page_control, i, 2 + j);
		}
	}
    }

    return pos;
}


static void
mo_update_request_length(mo_t *dev, int len, int block_len)
{
    int bt, min_len = 0;

    dev->max_transfer_len = dev->request_length;

    /* For media access commands, make sure the requested DRQ length matches the block length. */
    switch (dev->current_cdb[0]) {
	case 0x08:
	case 0x0a:
	case 0x28:
	case 0x2a:
	case 0xa8:
	case 0xaa:
		/* Round it to the nearest 2048 bytes. */
		dev->max_transfer_len = (dev->max_transfer_len >> 9) << 9;

		/* Make sure total length is not bigger than sum of the lengths of
		   all the requested blocks. */
		bt = (dev->requested_blocks * block_len);
		if (len > bt)
			len = bt;

		min_len = block_len;

		if (len <= block_len) {
			/* Total length is less or equal to block length. */
			if (dev->max_transfer_len < block_len) {
				/* Transfer a minimum of (block size) bytes. */
				dev->max_transfer_len = block_len;
				dev->packet_len = block_len;
				break;
			}
		}
		/*FALLTHROUGH*/
	default:
		dev->packet_len = len;
		break;
    }
    /* If the DRQ length is odd, and the total remaining length is bigger, make sure it's even. */
    if ((dev->max_transfer_len & 1) && (dev->max_transfer_len < len))
	dev->max_transfer_len &= 0xfffe;
    /* If the DRQ length is smaller or equal in size to the total remaining length, set it to that. */
    if (!dev->max_transfer_len)
	dev->max_transfer_len = 65534;

    if ((len <= dev->max_transfer_len) && (len >= min_len))
	dev->request_length = dev->max_transfer_len = len;
    else if (len > dev->max_transfer_len)
	dev->request_length = dev->max_transfer_len;

    return;
}


static double
mo_bus_speed(mo_t *dev)
{
    double ret = -1.0;

    if (dev && dev->drv && (dev->drv->bus_type == MO_BUS_SCSI)) {
	dev->callback = -1.0;	/* Speed depends on SCSI controller */
	return 0.0;
    } else {
	if (dev && dev->drv)
		ret = ide_atapi_get_period(dev->drv->ide_channel);
	if (ret == -1.0) {
		if (dev)
			dev->callback = -1.0;
		return 0.0;
	} else
		return ret * 1000000.0;
    }
}


static void
mo_command_common(mo_t *dev)
{
    double bytes_per_second, period;

    dev->status = BUSY_STAT;
    dev->phase = 1;
    dev->pos = 0;
    if (dev->packet_status == PHASE_COMPLETE)
	dev->callback = 0.0;
    else {
	if (dev->drv->bus_type == MO_BUS_SCSI) {
		dev->callback = -1.0;	/* Speed depends on SCSI controller */
		return;
	} else
		bytes_per_second = mo_bus_speed(dev);

	period = 1000000.0 / bytes_per_second;
	dev->callback = period * (double) (dev->packet_len);
    }

    mo_set_callback(dev);
}


static void
mo_command_complete(mo_t *dev)
{
    ui_sb_update_icon(SB_MO | dev->id, 0);
    dev->packet_status = PHASE_COMPLETE;
    mo_command_common(dev);
}


static void
mo_command_read(mo_t *dev)
{
    dev->packet_status = PHASE_DATA_IN;
    mo_command_common(dev);
}


static void
mo_command_read_dma(mo_t *dev)
{
    dev->packet_status = PHASE_DATA_IN_DMA;
    mo_command_common(dev);
}


static void
mo_command_write(mo_t *dev)
{
    dev->packet_status = PHASE_DATA_OUT;
    mo_command_common(dev);
}


static void
mo_command_write_dma(mo_t *dev)
{
    dev->packet_status = PHASE_DATA_OUT_DMA;
    mo_command_common(dev);
}


/* id = Current MO device ID;
   len = Total transfer length;
   block_len = Length of a single block (why does it matter?!);
   alloc_len = Allocated transfer length;
   direction = Transfer direction (0 = read from host, 1 = write to host). */
static void
mo_data_command_finish(mo_t *dev, int len, int block_len, int alloc_len, int direction)
{
    mo_log("MO %i: Finishing command (%02X): %i, %i, %i, %i, %i\n",
	    dev->id, dev->current_cdb[0], len, block_len, alloc_len, direction, dev->request_length);
    dev->pos = 0;
    if (alloc_len >= 0) {
	if (alloc_len < len)
		len = alloc_len;
    }
    if ((len == 0) || (mo_current_mode(dev) == 0)) {
	if (dev->drv->bus_type != MO_BUS_SCSI)
		dev->packet_len = 0;

	mo_command_complete(dev);
    } else {
	if (mo_current_mode(dev) == 2) {
		if (dev->drv->bus_type != MO_BUS_SCSI)
			dev->packet_len = alloc_len;

		if (direction == 0)
			mo_command_read_dma(dev);
		else
			mo_command_write_dma(dev);
	} else {
		mo_update_request_length(dev, len, block_len);
		if (direction == 0)
			mo_command_read(dev);
		else
			mo_command_write(dev);
	}
    }

    mo_log("MO %i: Status: %i, cylinder %i, packet length: %i, position: %i, phase: %i\n",
	    dev->id, dev->packet_status, dev->request_length, dev->packet_len, dev->pos, dev->phase);
}


static void
mo_sense_clear(mo_t *dev, int command)
{
    mo_sense_key = mo_asc = mo_ascq = 0;
}


static void
mo_set_phase(mo_t *dev, uint8_t phase)
{
    uint8_t scsi_bus = (dev->drv->scsi_device_id >> 4) & 0x0f;
    uint8_t scsi_id = dev->drv->scsi_device_id & 0x0f;

    if (dev->drv->bus_type != MO_BUS_SCSI)
	return;

    scsi_devices[scsi_bus][scsi_id].phase = phase;
}


static void
mo_cmd_error(mo_t *dev)
{
    mo_set_phase(dev, SCSI_PHASE_STATUS);
    dev->error = ((mo_sense_key & 0xf) << 4) | ABRT_ERR;
    if (dev->unit_attention)
	dev->error |= MCR_ERR;
    dev->status = READY_STAT | ERR_STAT;
    dev->phase = 3;
    dev->pos = 0;
    dev->packet_status = PHASE_ERROR;
    dev->callback = 50.0 * MO_TIME;
    mo_set_callback(dev);
    ui_sb_update_icon(SB_MO | dev->id, 0);
    mo_log("MO %i: [%02X] ERROR: %02X/%02X/%02X\n", dev->id, dev->current_cdb[0], mo_sense_key, mo_asc, mo_ascq);
}


static void
mo_unit_attention(mo_t *dev)
{
    mo_set_phase(dev, SCSI_PHASE_STATUS);
    dev->error = (SENSE_UNIT_ATTENTION << 4) | ABRT_ERR;
    if (dev->unit_attention)
	dev->error |= MCR_ERR;
    dev->status = READY_STAT | ERR_STAT;
    dev->phase = 3;
    dev->pos = 0;
    dev->packet_status = PHASE_ERROR;
    dev->callback = 50.0 * MO_TIME;
    mo_set_callback(dev);
    ui_sb_update_icon(SB_MO | dev->id, 0);
    mo_log("MO %i: UNIT ATTENTION\n", dev->id);
}


static void
mo_buf_alloc(mo_t *dev, uint32_t len)
{
    mo_log("MO %i: Allocated buffer length: %i\n", dev->id, len);
    if (!dev->buffer)
	dev->buffer = (uint8_t *) malloc(len);
}


static void
mo_buf_free(mo_t *dev)
{
    if (dev->buffer) {
	mo_log("MO %i: Freeing buffer...\n", dev->id);
	free(dev->buffer);
	dev->buffer = NULL;
    }
}


static void
mo_bus_master_error(scsi_common_t *sc)
{
    mo_t *dev = (mo_t *) sc;

    mo_buf_free(dev);
    mo_sense_key = mo_asc = mo_ascq = 0;
    mo_cmd_error(dev);
}


static void
mo_not_ready(mo_t *dev)
{
    mo_sense_key = SENSE_NOT_READY;
    mo_asc = ASC_MEDIUM_NOT_PRESENT;
    mo_ascq = 0;
    mo_cmd_error(dev);
}


static void
mo_write_protected(mo_t *dev)
{
    mo_sense_key = SENSE_UNIT_ATTENTION;
    mo_asc = ASC_WRITE_PROTECTED;
    mo_ascq = 0;
    mo_cmd_error(dev);
}


static void
mo_invalid_lun(mo_t *dev)
{
    mo_sense_key = SENSE_ILLEGAL_REQUEST;
    mo_asc = ASC_INV_LUN;
    mo_ascq = 0;
    mo_cmd_error(dev);
}


static void
mo_illegal_opcode(mo_t *dev)
{
    mo_sense_key = SENSE_ILLEGAL_REQUEST;
    mo_asc = ASC_ILLEGAL_OPCODE;
    mo_ascq = 0;
    mo_cmd_error(dev);
}


static void
mo_lba_out_of_range(mo_t *dev)
{
    mo_sense_key = SENSE_ILLEGAL_REQUEST;
    mo_asc = ASC_LBA_OUT_OF_RANGE;
    mo_ascq = 0;
    mo_cmd_error(dev);
}


static void
mo_invalid_field(mo_t *dev)
{
    mo_sense_key = SENSE_ILLEGAL_REQUEST;
    mo_asc = ASC_INV_FIELD_IN_CMD_PACKET;
    mo_ascq = 0;
    mo_cmd_error(dev);
    dev->status = 0x53;
}


static void
mo_invalid_field_pl(mo_t *dev)
{
    mo_sense_key = SENSE_ILLEGAL_REQUEST;
    mo_asc = ASC_INV_FIELD_IN_PARAMETER_LIST;
    mo_ascq = 0;
    mo_cmd_error(dev);
    dev->status = 0x53;
}


static int
mo_blocks(mo_t *dev, int32_t *len, int first_batch, int out)
{
    *len = 0;
    int i;

    if (!dev->sector_len) {
	mo_command_complete(dev);
	return -1;
    }

    mo_log("%sing %i blocks starting from %i...\n", out ? "Writ" : "Read", dev->requested_blocks, dev->sector_pos);

    if (dev->sector_pos >= dev->drv->medium_size) {
	mo_log("MO %i: Trying to %s beyond the end of disk\n", dev->id, out ? "write" : "read");
	mo_lba_out_of_range(dev);
	return 0;
    }

    *len = dev->requested_blocks * dev->drv->sector_size;

    for (i = 0; i < dev->requested_blocks; i++) {
	if (fseek(dev->drv->f, dev->drv->base + (dev->sector_pos * dev->drv->sector_size) + (i * dev->drv->sector_size), SEEK_SET) == 1)
		break;

	if (feof(dev->drv->f))
		break;

	if (out) {
		if (fwrite(dev->buffer + (i * dev->drv->sector_size), 1, dev->drv->sector_size, dev->drv->f) != dev->drv->sector_size)
			fatal("mo_blocks(): Error writing data\n");
	} else {
		if (fread(dev->buffer + (i * dev->drv->sector_size), 1, dev->drv->sector_size, dev->drv->f) != dev->drv->sector_size)
			fatal("mo_blocks(): Error reading data\n");
	}
    }

    mo_log("%s %i bytes of blocks...\n", out ? "Written" : "Read", *len);

    dev->sector_pos += dev->requested_blocks;
    dev->sector_len -= dev->requested_blocks;

    return 1;
}


void
mo_insert(mo_t *dev)
{
    dev->unit_attention = 1;
}

void
mo_format(mo_t *dev)
{
    long size;
    int ret;
    int fd;

    mo_log("MO %i: Formatting media...\n", dev->id);

    fseek(dev->drv->f, 0, SEEK_END);
    size = ftell(dev->drv->f);

#ifdef _WIN32
    HANDLE fh;
    LARGE_INTEGER liSize;

    fd = _fileno(dev->drv->f);
    fh = (HANDLE)_get_osfhandle(fd);

    liSize.QuadPart = 0;

    ret = (int)SetFilePointerEx(fh, liSize, NULL, FILE_BEGIN);

    if(!ret) {
	mo_log("MO %i: Failed seek to start of image file\n", dev->id);
	return;
    }

    ret = (int)SetEndOfFile(fh);

    if(!ret) {
	mo_log("MO %i: Failed to truncate image file to 0\n", dev->id);
	return;
    }

    liSize.QuadPart = size;
    ret = (int)SetFilePointerEx(fh, liSize, NULL, FILE_BEGIN);

    if(!ret) {
	mo_log("MO %i: Failed seek to end of image file\n", dev->id);
	return;
    }

    ret = (int)SetEndOfFile(fh);

    if(!ret) {
	mo_log("MO %i: Failed to truncate image file to %llu\n", dev->id, size);
	return;
    }
#else
    fd = fileno(dev->drv->f);

    ret = ftruncate(fd, 0);

    if(ret) {
	mo_log("MO %i: Failed to truncate image file to 0\n", dev->id);
	return;
    }

    ret = ftruncate(fd, size);

    if(ret) {
	mo_log("MO %i: Failed to truncate image file to %llu", dev->id, size);
	return;
    }
#endif
}

static int
mo_erase(mo_t *dev)
{
    int i;

    if (! dev->sector_len) {
	mo_command_complete(dev);
	return -1;
    }

    mo_log("MO %i: Erasing %i blocks starting from %i...\n", dev->id, dev->sector_len, dev->sector_pos);

    if (dev->sector_pos >= dev->drv->medium_size) {
	mo_log("MO %i: Trying to erase beyond the end of disk\n", dev->id);
	mo_lba_out_of_range(dev);
	return 0;
    }

    mo_buf_alloc(dev, dev->drv->sector_size);
    memset(dev->buffer, 0, dev->drv->sector_size);

    fseek(dev->drv->f, dev->drv->base + (dev->sector_pos * dev->drv->sector_size), SEEK_SET);

    for (i = 0; i < dev->requested_blocks; i++) {
	if (feof(dev->drv->f))
	    break;

	fwrite(dev->buffer, 1, dev->drv->sector_size, dev->drv->f);
    }

    mo_log("MO %i: Erased %i bytes of blocks...\n", dev->id, i * dev->drv->sector_size);

    dev->sector_pos += i;
    dev->sector_len -= i;

    return 1;
}

/*SCSI Sense Initialization*/
void
mo_sense_code_ok(mo_t *dev)
{
    mo_sense_key = SENSE_NONE;
    mo_asc = 0;
    mo_ascq = 0;
}


static int
mo_pre_execution_check(mo_t *dev, uint8_t *cdb)
{
    int ready = 0;

    if (dev->drv->bus_type == MO_BUS_SCSI) {
	if ((cdb[0] != GPCMD_REQUEST_SENSE) && (dev->cur_lun == SCSI_LUN_USE_CDB) && (cdb[1] & 0xe0)) {
		mo_log("MO %i: Attempting to execute a unknown command targeted at SCSI LUN %i\n", dev->id, ((dev->request_length >> 5) & 7));
		mo_invalid_lun(dev);
		return 0;
	}
    }

    if (!(mo_command_flags[cdb[0]] & IMPLEMENTED)) {
	mo_log("MO %i: Attempting to execute unknown command %02X over %s\n", dev->id, cdb[0],
		(dev->drv->bus_type == MO_BUS_SCSI) ? "SCSI" : "ATAPI");

	mo_illegal_opcode(dev);
	return 0;
    }

    if ((dev->drv->bus_type < MO_BUS_SCSI) && (mo_command_flags[cdb[0]] & SCSI_ONLY)) {
	mo_log("MO %i: Attempting to execute SCSI-only command %02X over ATAPI\n", dev->id, cdb[0]);
	mo_illegal_opcode(dev);
	return 0;
    }

    if ((dev->drv->bus_type == MO_BUS_SCSI) && (mo_command_flags[cdb[0]] & ATAPI_ONLY)) {
	mo_log("MO %i: Attempting to execute ATAPI-only command %02X over SCSI\n", dev->id, cdb[0]);
	mo_illegal_opcode(dev);
	return 0;
    }

    ready = (dev->drv->f != NULL);

    /* If the drive is not ready, there is no reason to keep the
       UNIT ATTENTION condition present, as we only use it to mark
       disc changes. */
    if (!ready && dev->unit_attention)
	dev->unit_attention = 0;

    /* If the UNIT ATTENTION condition is set and the command does not allow
       execution under it, error out and report the condition. */
    if (dev->unit_attention == 1) {
	/* Only increment the unit attention phase if the command can not pass through it. */
	if (!(mo_command_flags[cdb[0]] & ALLOW_UA)) {
		/* mo_log("MO %i: Unit attention now 2\n", dev->id); */
		dev->unit_attention = 2;
		mo_log("MO %i: UNIT ATTENTION: Command %02X not allowed to pass through\n", dev->id, cdb[0]);
		mo_unit_attention(dev);
		return 0;
	}
    } else if (dev->unit_attention == 2) {
	if (cdb[0] != GPCMD_REQUEST_SENSE) {
		/* mo_log("MO %i: Unit attention now 0\n", dev->id); */
		dev->unit_attention = 0;
	}
    }

    /* Unless the command is REQUEST SENSE, clear the sense. This will *NOT*
       the UNIT ATTENTION condition if it's set. */
    if (cdb[0] != GPCMD_REQUEST_SENSE)
	mo_sense_clear(dev, cdb[0]);

    /* Next it's time for NOT READY. */
    if ((mo_command_flags[cdb[0]] & CHECK_READY) && !ready) {
	mo_log("MO %i: Not ready (%02X)\n", dev->id, cdb[0]);
	mo_not_ready(dev);
	return 0;
    }

    mo_log("MO %i: Continuing with command %02X\n", dev->id, cdb[0]);

    return 1;
}


static void
mo_seek(mo_t *dev, uint32_t pos)
{
    /* mo_log("MO %i: Seek %08X\n", dev->id, pos); */
    dev->sector_pos   = pos;
}


static void
mo_rezero(mo_t *dev)
{
    dev->sector_pos = dev->sector_len = 0;
    mo_seek(dev, 0);
}


void
mo_reset(scsi_common_t *sc)
{
    mo_t *dev = (mo_t *) sc;

    mo_rezero(dev);
    dev->status = 0;
    dev->callback = 0.0;
    mo_set_callback(dev);
    dev->phase = 1;
    dev->request_length = 0xEB14;
    dev->packet_status = PHASE_NONE;
    dev->unit_attention = 0;
    dev->cur_lun = SCSI_LUN_USE_CDB;
}


static void
mo_request_sense(mo_t *dev, uint8_t *buffer, uint8_t alloc_length, int desc)
{
    /*Will return 18 bytes of 0*/
    if (alloc_length != 0) {
	memset(buffer, 0, alloc_length);
	if (!desc)
		memcpy(buffer, dev->sense, alloc_length);
	else {
		buffer[1] = mo_sense_key;
		buffer[2] = mo_asc;
		buffer[3] = mo_ascq;
	}
    }

    buffer[0] = desc ? 0x72 : 0x70;

    if (dev->unit_attention && (mo_sense_key == 0)) {
	buffer[desc ? 1 : 2] = SENSE_UNIT_ATTENTION;
	buffer[desc ? 2 : 12] = ASC_MEDIUM_MAY_HAVE_CHANGED;
	buffer[desc ? 3 : 13] = 0;
    }

    mo_log("MO %i: Reporting sense: %02X %02X %02X\n", dev->id, buffer[2], buffer[12], buffer[13]);

    if (buffer[desc ? 1 : 2] == SENSE_UNIT_ATTENTION) {
	/* If the last remaining sense is unit attention, clear
	   that condition. */
	dev->unit_attention = 0;
    }

    /* Clear the sense stuff as per the spec. */
    mo_sense_clear(dev, GPCMD_REQUEST_SENSE);
}


static void
mo_request_sense_for_scsi(scsi_common_t *sc, uint8_t *buffer, uint8_t alloc_length)
{
    mo_t *dev = (mo_t *) sc;
    int ready = 0;

    ready = (dev->drv->f != NULL);

    if (!ready && dev->unit_attention) {
	/* If the drive is not ready, there is no reason to keep the
	   UNIT ATTENTION condition present, as we only use it to mark
	   disc changes. */
	dev->unit_attention = 0;
    }

    /* Do *NOT* advance the unit attention phase. */

    mo_request_sense(dev, buffer, alloc_length, 0);
}


static void
mo_set_buf_len(mo_t *dev, int32_t *BufLen, int32_t *src_len)
{
    if (dev->drv->bus_type == MO_BUS_SCSI) {
	if (*BufLen == -1)
		*BufLen = *src_len;
	else {
		*BufLen = MIN(*src_len, *BufLen);
		*src_len = *BufLen;
	}
	mo_log("MO %i: Actual transfer length: %i\n", dev->id, *BufLen);
    }
}


static void
mo_command(scsi_common_t *sc, uint8_t *cdb)
{
    mo_t *dev = (mo_t *) sc;
    int pos = 0, block_desc = 0;
    int ret;
    int32_t len, max_len;
    int32_t alloc_length;
    int size_idx, idx = 0;
    unsigned preamble_len;
    char device_identify[9] = { '8', '6', 'B', '_', 'M', 'O', '0', '0', 0 };
    int32_t blen = 0;
    int32_t *BufLen;
    uint32_t previous_pos = 0;
    uint8_t scsi_bus = (dev->drv->scsi_device_id >> 4) & 0x0f;
    uint8_t scsi_id = dev->drv->scsi_device_id & 0x0f;

    if (dev->drv->bus_type == MO_BUS_SCSI) {
	BufLen = &scsi_devices[scsi_bus][scsi_id].buffer_length;
	dev->status &= ~ERR_STAT;
    } else {
	BufLen = &blen;
	dev->error = 0;
    }

    dev->packet_len = 0;
    dev->request_pos = 0;

    device_identify[7] = dev->id + 0x30;

    memcpy(dev->current_cdb, cdb, 12);

    if (cdb[0] != 0) {
	mo_log("MO %i: Command 0x%02X, Sense Key %02X, Asc %02X, Ascq %02X, Unit attention: %i\n",
		dev->id, cdb[0], mo_sense_key, mo_asc, mo_ascq, dev->unit_attention);
	mo_log("MO %i: Request length: %04X\n", dev->id, dev->request_length);

	mo_log("MO %i: CDB: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n", dev->id,
		cdb[0], cdb[1], cdb[2], cdb[3], cdb[4], cdb[5], cdb[6], cdb[7],
		cdb[8], cdb[9], cdb[10], cdb[11]);
    }

    dev->sector_len = 0;

    mo_set_phase(dev, SCSI_PHASE_STATUS);

    /* This handles the Not Ready/Unit Attention check if it has to be handled at this point. */
    if (mo_pre_execution_check(dev, cdb) == 0)
	return;

    switch (cdb[0]) {
	case GPCMD_SEND_DIAGNOSTIC:
		if (!(cdb[1] & (1 << 2))) {
			mo_invalid_field(dev);
			return;
		}
		/*FALLTHROUGH*/
	case GPCMD_SCSI_RESERVE:
	case GPCMD_SCSI_RELEASE:
	case GPCMD_TEST_UNIT_READY:
		mo_set_phase(dev, SCSI_PHASE_STATUS);
		mo_command_complete(dev);
		break;

	case GPCMD_FORMAT_UNIT:
		if (dev->drv->read_only) {
			mo_write_protected(dev);
			return;
		}

		mo_format(dev);
		mo_set_phase(dev, SCSI_PHASE_STATUS);
		mo_command_complete(dev);
		break;

	case GPCMD_REZERO_UNIT:
		dev->sector_pos = dev->sector_len = 0;
		mo_seek(dev, 0);
		mo_set_phase(dev, SCSI_PHASE_STATUS);
		break;

	case GPCMD_REQUEST_SENSE:
		/* If there's a unit attention condition and there's a buffered not ready, a standalone REQUEST SENSE
		   should forget about the not ready, and report unit attention straight away. */
		mo_set_phase(dev, SCSI_PHASE_DATA_IN);
		max_len = cdb[4];

		if (!max_len) {
			mo_set_phase(dev, SCSI_PHASE_STATUS);
			dev->packet_status = PHASE_COMPLETE;
			dev->callback = 20.0 * MO_TIME;
			mo_set_callback(dev);
			break;
		}

		mo_buf_alloc(dev, 256);
		mo_set_buf_len(dev, BufLen, &max_len);
		len = (cdb[1] & 1) ? 8 : 18;
		mo_request_sense(dev, dev->buffer, max_len, cdb[1] & 1);
		mo_data_command_finish(dev, len, len, cdb[4], 0);
		break;

	case GPCMD_MECHANISM_STATUS:
		mo_set_phase(dev, SCSI_PHASE_DATA_IN);
		len = (cdb[8] << 8) | cdb[9];

		mo_buf_alloc(dev, 8);
		mo_set_buf_len(dev, BufLen, &len);

		memset(dev->buffer, 0, 8);
		dev->buffer[5] = 1;

		mo_data_command_finish(dev, 8, 8, len, 0);
		break;

	case GPCMD_READ_6:
	case GPCMD_READ_10:
	case GPCMD_READ_12:
		mo_set_phase(dev, SCSI_PHASE_DATA_IN);
		alloc_length = dev->drv->sector_size;

		switch(cdb[0]) {
			case GPCMD_READ_6:
				dev->sector_len = cdb[4];
				dev->sector_pos = ((((uint32_t) cdb[1]) & 0x1f) << 16) | (((uint32_t) cdb[2]) << 8) | ((uint32_t) cdb[3]);
				if (dev->sector_len == 0)
					dev->sector_len = 256;
				mo_log("MO %i: Length: %i, LBA: %i\n", dev->id, dev->sector_len, dev->sector_pos);
				break;
			case GPCMD_READ_10:
				dev->sector_len = (cdb[7] << 8) | cdb[8];
				dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
				mo_log("MO %i: Length: %i, LBA: %i\n", dev->id, dev->sector_len, dev->sector_pos);
				break;
			case GPCMD_READ_12:
				dev->sector_len = (((uint32_t) cdb[6]) << 24) | (((uint32_t) cdb[7]) << 16) | (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);
				dev->sector_pos = (((uint32_t) cdb[2]) << 24) | (((uint32_t) cdb[3]) << 16) | (((uint32_t) cdb[4]) << 8) | ((uint32_t) cdb[5]);
				mo_log("MO %i: Length: %i, LBA: %i\n", dev->id, dev->sector_len, dev->sector_pos);
				break;
		}

		if (!dev->sector_len) {
			mo_set_phase(dev, SCSI_PHASE_STATUS);
			/* mo_log("MO %i: All done - callback set\n", dev->id); */
			dev->packet_status = PHASE_COMPLETE;
			dev->callback = 20.0 * MO_TIME;
			mo_set_callback(dev);
			break;
		}

		max_len = dev->sector_len;
		dev->requested_blocks = max_len;	/* If we're reading all blocks in one go for DMA, why not also for PIO, it should NOT
							   matter anyway, this step should be identical and only the way the read dat is
							   transferred to the host should be different. */

		dev->packet_len = max_len * alloc_length;
		mo_buf_alloc(dev, dev->packet_len);

		ret = mo_blocks(dev, &alloc_length, 1, 0);
		if (ret <= 0) {
			mo_set_phase(dev, SCSI_PHASE_STATUS);
			dev->packet_status = PHASE_COMPLETE;
			dev->callback = 20.0 * MO_TIME;
			mo_set_callback(dev);
			mo_buf_free(dev);
			return;
		}

		dev->requested_blocks = max_len;
		dev->packet_len = alloc_length;

		mo_set_buf_len(dev, BufLen, (int32_t *) &dev->packet_len);

		mo_data_command_finish(dev, alloc_length, dev->drv->sector_size, alloc_length, 0);

		if (dev->packet_status != PHASE_COMPLETE)
			ui_sb_update_icon(SB_MO | dev->id, 1);
		else
			ui_sb_update_icon(SB_MO | dev->id, 0);
		return;

	case GPCMD_VERIFY_6:
	case GPCMD_VERIFY_10:
	case GPCMD_VERIFY_12:
		/* Data and blank verification cannot be set at the same time */
		if ((cdb[1] & 2) && (cdb[1] & 4)) {
			mo_invalid_field(dev);
			return;
		}
		if (!(cdb[1] & 2) || (cdb[1] & 4)) {
			mo_set_phase(dev, SCSI_PHASE_STATUS);
			mo_command_complete(dev);
			break;
		}
		/*TODO: Implement*/
		mo_invalid_field(dev);
		return;

	case GPCMD_WRITE_6:
	case GPCMD_WRITE_10:
	case GPCMD_WRITE_AND_VERIFY_10:
	case GPCMD_WRITE_12:
	case GPCMD_WRITE_AND_VERIFY_12:
		mo_set_phase(dev, SCSI_PHASE_DATA_OUT);
		alloc_length = dev->drv->sector_size;

		if (dev->drv->read_only) {
			mo_write_protected(dev);
			return;
		}

		switch (cdb[0]) {
			case GPCMD_VERIFY_6:
			case GPCMD_WRITE_6:
				dev->sector_len = cdb[4];
				if (dev->sector_len == 0)
					dev->sector_len = 256;	/* For READ (6) and WRITE (6), a length of 0 indicates a transfer of 256 sector. */
				dev->sector_pos = ((((uint32_t) cdb[1]) & 0x1f) << 16) | (((uint32_t) cdb[2]) << 8) | ((uint32_t) cdb[3]);
				break;
			case GPCMD_VERIFY_10:
			case GPCMD_WRITE_10:
			case GPCMD_WRITE_AND_VERIFY_10:
				dev->sector_len = (cdb[7] << 8) | cdb[8];
				dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
				mo_log("MO %i: Length: %i, LBA: %i\n", dev->id, dev->sector_len, dev->sector_pos);
				break;
			case GPCMD_VERIFY_12:
			case GPCMD_WRITE_12:
			case GPCMD_WRITE_AND_VERIFY_12:
				dev->sector_len = (((uint32_t) cdb[6]) << 24) | (((uint32_t) cdb[7]) << 16) | (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);
				dev->sector_pos = (((uint32_t) cdb[2]) << 24) | (((uint32_t) cdb[3]) << 16) | (((uint32_t) cdb[4]) << 8) | ((uint32_t) cdb[5]);
				break;
		}

		if ((dev->sector_pos >= dev->drv->medium_size)/* ||
		    ((dev->sector_pos + dev->sector_len - 1) >= dev->drv->medium_size)*/) {
			mo_lba_out_of_range(dev);
			return;
		}

		if (!dev->sector_len) {
			mo_set_phase(dev, SCSI_PHASE_STATUS);
			/* mo_log("MO %i: All done - callback set\n", dev->id); */
			dev->packet_status = PHASE_COMPLETE;
			dev->callback = 20.0 * MO_TIME;
			mo_set_callback(dev);
			break;
		}

		max_len = dev->sector_len;
		dev->requested_blocks = max_len;	/* If we're writing all blocks in one go for DMA, why not also for PIO, it should NOT
							   matter anyway, this step should be identical and only the way the read dat is
							   transferred to the host should be different. */

		dev->packet_len = max_len * alloc_length;
		mo_buf_alloc(dev, dev->packet_len);

		dev->requested_blocks = max_len;
		dev->packet_len = max_len << 9;

		mo_set_buf_len(dev, BufLen, (int32_t *) &dev->packet_len);

		mo_data_command_finish(dev, dev->packet_len, dev->drv->sector_size, dev->packet_len, 1);

		if (dev->packet_status != PHASE_COMPLETE)
			ui_sb_update_icon(SB_MO | dev->id, 1);
		else
			ui_sb_update_icon(SB_MO | dev->id, 0);
		return;

	case GPCMD_MODE_SENSE_6:
	case GPCMD_MODE_SENSE_10:
		mo_set_phase(dev, SCSI_PHASE_DATA_IN);

		if (dev->drv->bus_type == MO_BUS_SCSI)
			block_desc = ((cdb[1] >> 3) & 1) ? 0 : 1;
		else
			block_desc = 0;

		if (cdb[0] == GPCMD_MODE_SENSE_6) {
			len = cdb[4];
			mo_buf_alloc(dev, 256);
		} else {
			len = (cdb[8] | (cdb[7] << 8));
			mo_buf_alloc(dev, 65536);
		}

		if (!(mo_mode_sense_page_flags & (1LL << (uint64_t) (cdb[2] & 0x3f)))) {
			mo_invalid_field(dev);
			mo_buf_free(dev);
			return;
		}

		memset(dev->buffer, 0, len);
		alloc_length = len;

		if (cdb[0] == GPCMD_MODE_SENSE_6) {
			len = mo_mode_sense(dev, dev->buffer, 4, cdb[2], block_desc);
			len = MIN(len, alloc_length);
			dev->buffer[0] = len - 1;
			dev->buffer[1] = 0;
			if (block_desc)
				dev->buffer[3] = 8;
		} else {
			len = mo_mode_sense(dev, dev->buffer, 8, cdb[2], block_desc);
			len = MIN(len, alloc_length);
			dev->buffer[0]=(len - 2) >> 8;
			dev->buffer[1]=(len - 2) & 255;
			dev->buffer[2] = 0;
			if (block_desc) {
				dev->buffer[6] = 0;
				dev->buffer[7] = 8;
			}
		}

		mo_set_buf_len(dev, BufLen, &len);

		mo_log("MO %i: Reading mode page: %02X...\n", dev->id, cdb[2]);

		mo_data_command_finish(dev, len, len, alloc_length, 0);
		return;

	case GPCMD_MODE_SELECT_6:
	case GPCMD_MODE_SELECT_10:
		mo_set_phase(dev, SCSI_PHASE_DATA_OUT);

		if (cdb[0] == GPCMD_MODE_SELECT_6) {
			len = cdb[4];
			mo_buf_alloc(dev, 256);
		} else {
			len = (cdb[7] << 8) | cdb[8];
			mo_buf_alloc(dev, 65536);
		}

		mo_set_buf_len(dev, BufLen, &len);

		dev->total_length = len;
		dev->do_page_save = cdb[1] & 1;

		mo_data_command_finish(dev, len, len, len, 1);
		return;

	case GPCMD_START_STOP_UNIT:
		mo_set_phase(dev, SCSI_PHASE_STATUS);

		switch(cdb[4] & 3) {
			case 0:		/* Stop the disk. */
				break;
			case 1:		/* Start the disk and read the TOC. */
				break;
			case 2:		/* Eject the disk if possible. */
				mo_eject(dev->id);
				break;
			case 3:		/* Load the disk (close tray). */
				mo_reload(dev->id);
				break;
		}

		mo_command_complete(dev);
		break;

	case GPCMD_INQUIRY:
		mo_set_phase(dev, SCSI_PHASE_DATA_IN);

		max_len = cdb[3];
		max_len <<= 8;
		max_len |= cdb[4];

		mo_buf_alloc(dev, 65536);

		if (cdb[1] & 1) {
			preamble_len = 4;
			size_idx = 3;

			dev->buffer[idx++] = 7; /*Optical disk*/
			dev->buffer[idx++] = cdb[2];
			dev->buffer[idx++] = 0;

			idx++;

			switch (cdb[2]) {
				case 0x00:
					dev->buffer[idx++] = 0x00;
					dev->buffer[idx++] = 0x80;
					break;
				case 0x80: /*Unit serial number page*/
					dev->buffer[idx++] = strlen("VCM!10") + 1;
					ide_padstr8(dev->buffer + idx, 20, "VCM!10");	/* Serial */
					idx += strlen("VCM!10");
					break;
				default:
					mo_log("INQUIRY: Invalid page: %02X\n", cdb[2]);
					mo_invalid_field(dev);
					mo_buf_free(dev);
					return;
			}
		} else {
			preamble_len = 5;
			size_idx = 4;

			memset(dev->buffer, 0, 8);
			if (cdb[1] & 0xe0)
				dev->buffer[0] = 0x60; /*No physical device on this LUN*/
			else
				dev->buffer[0] = 0x07; /*Optical disk*/
			dev->buffer[1] = 0x80; /*Removable*/
			dev->buffer[2] = (dev->drv->bus_type == MO_BUS_SCSI) ? 0x02 : 0x00; /*SCSI-2 compliant*/
			dev->buffer[3] = (dev->drv->bus_type == MO_BUS_SCSI) ? 0x02 : 0x21;
			// dev->buffer[4] = 31;
			dev->buffer[4] = 0;
			if (dev->drv->bus_type == MO_BUS_SCSI) {
				dev->buffer[6] = 1;	/* 16-bit transfers supported */
				dev->buffer[7] = 0x20;	/* Wide bus supported */
			}
			dev->buffer[7] |= 0x02;

			if (dev->drv->type > 0) {
				ide_padstr8(dev->buffer + 8, 8, mo_drive_types[dev->drv->type].vendor); /* Vendor */
				ide_padstr8(dev->buffer + 16, 16, mo_drive_types[dev->drv->type].model); /* Product */
				ide_padstr8(dev->buffer + 32, 4, mo_drive_types[dev->drv->type].revision); /* Revision */
			} else {
				ide_padstr8(dev->buffer + 8, 8, EMU_NAME); /* Vendor */
				ide_padstr8(dev->buffer + 16, 16, device_identify); /* Product */
				ide_padstr8(dev->buffer + 32, 4, EMU_VERSION_EX); /* Revision */
			}
			idx = 36;

			if (max_len == 96) {
				dev->buffer[4] = 91;
				idx = 96;
			} else if (max_len == 128) {
				dev->buffer[4] = 0x75;
				idx = 128;
			}
		}

		dev->buffer[size_idx] = idx - preamble_len;
		len=idx;

		len = MIN(len, max_len);
		mo_set_buf_len(dev, BufLen, &len);

		mo_data_command_finish(dev, len, len, max_len, 0);
		break;

	case GPCMD_PREVENT_REMOVAL:
		mo_set_phase(dev, SCSI_PHASE_STATUS);
		mo_command_complete(dev);
		break;

	case GPCMD_SEEK_6:
	case GPCMD_SEEK_10:
		mo_set_phase(dev, SCSI_PHASE_STATUS);

		switch(cdb[0]) {
			case GPCMD_SEEK_6:
				pos = (cdb[2] << 8) | cdb[3];
				break;
			case GPCMD_SEEK_10:
				pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
				break;
		}
		mo_seek(dev, pos);
		mo_command_complete(dev);
		break;

	case GPCMD_READ_CDROM_CAPACITY:
		mo_set_phase(dev, SCSI_PHASE_DATA_IN);

		mo_buf_alloc(dev, 8);

		max_len = dev->drv->medium_size - 1;	/* IMPORTANT: What's returned is the last LBA block. */
		memset(dev->buffer, 0, 8);
		dev->buffer[0] = (max_len >> 24) & 0xff;
		dev->buffer[1] = (max_len >> 16) & 0xff;
		dev->buffer[2] = (max_len >> 8) & 0xff;
		dev->buffer[3] = max_len & 0xff;
		dev->buffer[6] = (dev->drv->sector_size >> 8) & 0xff;
		dev->buffer[7] = dev->drv->sector_size & 0xff;
		len = 8;

		mo_set_buf_len(dev, BufLen, &len);

		mo_data_command_finish(dev, len, len, len, 0);
		break;

	case GPCMD_ERASE_10:
	case GPCMD_ERASE_12:
		/*Relative address*/
		if ((cdb[1] & 1))
			previous_pos = dev->sector_pos;

		switch (cdb[0]) {
			case GPCMD_ERASE_10:
				dev->sector_len = (cdb[7] << 8) | cdb[8];
				break;
			case GPCMD_ERASE_12:
				dev->sector_len = (((uint32_t) cdb[6]) << 24) | (((uint32_t) cdb[7]) << 16) | (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);
				break;
		}

		/*Erase all remaining sectors*/
		if ((cdb[1] & 4)) {
			/* Cannot have a sector number when erase all*/
			if (dev->sector_len) {
				mo_invalid_field(dev);
				return;
			}
			mo_format(dev);
			mo_set_phase(dev, SCSI_PHASE_STATUS);
			mo_command_complete(dev);
			break;
		}

		switch (cdb[0]) {
			case GPCMD_ERASE_10:
				dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
				break;
			case GPCMD_ERASE_12:
				dev->sector_pos = (((uint32_t) cdb[2]) << 24) | (((uint32_t) cdb[3]) << 16) | (((uint32_t) cdb[4]) << 8) | ((uint32_t) cdb[5]);
				break;
		}

		dev->sector_pos += previous_pos;

		mo_erase(dev);
		mo_set_phase(dev, SCSI_PHASE_STATUS);
		mo_command_complete(dev);
		break;

	/*Never seen media that supports generations but it's interesting to know if any implementation calls this commmand*/
	case GPCMD_READ_GENERATION:
		mo_set_phase(dev, SCSI_PHASE_DATA_IN);

		mo_buf_alloc(dev, 4);
		len = 4;

		dev->buffer[0] = 0;
		dev->buffer[1] = 0;
		dev->buffer[2] = 0;
		dev->buffer[3] = 0;

		mo_set_buf_len(dev, BufLen, &len);
		mo_data_command_finish(dev, len, len, len, 0);
		break;

	default:
		mo_illegal_opcode(dev);
		break;
    }

    /* mo_log("MO %i: Phase: %02X, request length: %i\n", dev->id, dev->phase, dev->request_length); */

    if (mo_atapi_phase_to_scsi(dev) == SCSI_PHASE_STATUS)
	mo_buf_free(dev);
}


static void
mo_command_stop(scsi_common_t *sc)
{
    mo_t *dev = (mo_t *) sc;

    mo_command_complete(dev);
    mo_buf_free(dev);
}


/* The command second phase function, needed for Mode Select. */
static uint8_t
mo_phase_data_out(scsi_common_t *sc)
{
    mo_t *dev = (mo_t *) sc;

    uint16_t block_desc_len, pos;
    uint16_t param_list_len;

    uint8_t error = 0;
    uint8_t page, page_len;

    uint32_t i = 0;

    uint8_t hdr_len, val, old_val, ch;

    int len = 0;

    switch(dev->current_cdb[0]) {
	case GPCMD_VERIFY_6:
	case GPCMD_VERIFY_10:
	case GPCMD_VERIFY_12:
		break;
	case GPCMD_WRITE_6:
	case GPCMD_WRITE_10:
	case GPCMD_WRITE_AND_VERIFY_10:
	case GPCMD_WRITE_12:
	case GPCMD_WRITE_AND_VERIFY_12:
		if (dev->requested_blocks > 0)
			mo_blocks(dev, &len, 1, 1);
		break;
	case GPCMD_MODE_SELECT_6:
	case GPCMD_MODE_SELECT_10:
		if (dev->current_cdb[0] == GPCMD_MODE_SELECT_10) {
			hdr_len = 8;
			param_list_len = dev->current_cdb[7];
			param_list_len <<= 8;
			param_list_len |= dev->current_cdb[8];
		} else {
			hdr_len = 4;
			param_list_len = dev->current_cdb[4];
		}

		if (dev->drv->bus_type == MO_BUS_SCSI) {
			if (dev->current_cdb[0] == GPCMD_MODE_SELECT_6) {
				block_desc_len = dev->buffer[2];
				block_desc_len <<= 8;
				block_desc_len |= dev->buffer[3];
			} else {
				block_desc_len = dev->buffer[6];
				block_desc_len <<= 8;
				block_desc_len |= dev->buffer[7];
			}
		} else
			block_desc_len = 0;

		pos = hdr_len + block_desc_len;

		while(1) {
			if (pos >= param_list_len) {
				mo_log("MO %i: Buffer has only block descriptor\n", dev->id);
				break;
			}

			page = dev->buffer[pos] & 0x3F;
			page_len = dev->buffer[pos + 1];

			pos += 2;

			if (!(mo_mode_sense_page_flags & (1LL << ((uint64_t) page))))
				error |= 1;
			else {
				for (i = 0; i < page_len; i++) {
					ch = mo_mode_sense_pages_changeable.pages[page][i + 2];
					val = dev->buffer[pos + i];
					old_val = dev->ms_pages_saved.pages[page][i + 2];
					if (val != old_val) {
						if (ch)
							dev->ms_pages_saved.pages[page][i + 2] = val;
						else
							error |= 1;
					}
				}
			}

			pos += page_len;

			if (dev->drv->bus_type == MO_BUS_SCSI)
				val = mo_mode_sense_pages_default_scsi.pages[page][0] & 0x80;
			else
				val = mo_mode_sense_pages_default.pages[page][0] & 0x80;
			if (dev->do_page_save && val)
				mo_mode_sense_save(dev);

			if (pos >= dev->total_length)
				break;
		}

		if (error) {
			mo_buf_free(dev);
			mo_invalid_field_pl(dev);
			return 0;
		}
		break;
    }

    mo_command_stop((scsi_common_t *) dev);
    return 1;
}


/* Peform a master init on the entire module. */
void
mo_global_init(void)
{
    /* Clear the global data. */
    memset(mo_drives, 0x00, sizeof(mo_drives));
}


static int
mo_get_max(int ide_has_dma, int type)
{
    int ret;

    switch(type) {
	case TYPE_PIO:
		ret = ide_has_dma ? 3 : 0;
		break;
	case TYPE_SDMA:
	default:
		ret = -1;
		break;
	case TYPE_MDMA:
		ret = ide_has_dma ? 1 : -1;
		break;
	case TYPE_UDMA:
		ret = ide_has_dma ? 5 : -1;
		break;
    }

    return ret;
}


static int
mo_get_timings(int ide_has_dma, int type)
{
    int ret;

    switch(type) {
	case TIMINGS_DMA:
		ret = ide_has_dma ? 0x96 : 0;
		break;
	case TIMINGS_PIO:
		ret = ide_has_dma ? 0xb4 : 0;
		break;
	case TIMINGS_PIO_FC:
		ret = ide_has_dma ? 0xb4 : 0;
		break;
	default:
		ret = 0;
		break;
    }

    return ret;
}

static void
mo_do_identify(ide_t *ide, int ide_has_dma)
{
    char model[40];

    mo_t* mo = (mo_t*) ide->sc;

    memset(model, 0, 40);

    if (mo_drives[mo->id].type > 0) {
    	snprintf(model, 40, "%s %s", mo_drive_types[mo_drives[mo->id].type].vendor, mo_drive_types[mo_drives[mo->id].type].model);
	ide_padstr((char *) (ide->buffer + 23), mo_drive_types[mo_drives[mo->id].type].revision, 8); /* Firmware */
	ide_padstr((char *) (ide->buffer + 27), model, 40); /* Model */
    } else {
    	snprintf(model, 40, "%s %s%02i", EMU_NAME, "86B_MO", mo->id);
	ide_padstr((char *) (ide->buffer + 23), EMU_VERSION_EX, 8); /* Firmware */
	ide_padstr((char *) (ide->buffer + 27), model, 40); /* Model */
    }

    if (ide_has_dma) {
	ide->buffer[80] = 0x70; /*Supported ATA versions : ATA/ATAPI-4 ATA/ATAPI-6*/
	ide->buffer[81] = 0x19; /*Maximum ATA revision supported : ATA/ATAPI-6 T13 1410D revision 3a*/
    }
}


static void
mo_identify(ide_t *ide, int ide_has_dma)
{
    ide->buffer[0] = 0x8000 | (0 << 8) | 0x80 | (1 << 5);     /* ATAPI device, direct-access device, removable media, interrupt DRQ */
    ide_padstr((char *) (ide->buffer + 10), "", 20); /* Serial Number */
    ide->buffer[49] = 0x200; /* LBA supported */
    ide->buffer[126] = 0xfffe; /* Interpret zero byte count limit as maximum length */
    mo_do_identify(ide, ide_has_dma);
}


static void
mo_drive_reset(int c)
{
    mo_t *dev;
    scsi_device_t *sd;
    ide_t *id;
    uint8_t scsi_bus = (mo_drives[c].scsi_device_id >> 4) & 0x0f;
    uint8_t scsi_id = mo_drives[c].scsi_device_id & 0x0f;

    if (!mo_drives[c].priv) {
	mo_drives[c].priv = (mo_t *) malloc(sizeof(mo_t));
	memset(mo_drives[c].priv, 0, sizeof(mo_t));
    }

    dev = (mo_t *) mo_drives[c].priv;

    dev->id = c;
    dev->cur_lun = SCSI_LUN_USE_CDB;

    if (mo_drives[c].bus_type == MO_BUS_SCSI) {
	/* SCSI MO, attach to the SCSI bus. */
	sd = &scsi_devices[scsi_bus][scsi_id];

	sd->sc = (scsi_common_t *) dev;
	sd->command = mo_command;
	sd->request_sense = mo_request_sense_for_scsi;
	sd->reset = mo_reset;
	sd->phase_data_out = mo_phase_data_out;
	sd->command_stop = mo_command_stop;
	sd->type = SCSI_REMOVABLE_DISK;
    } else if (mo_drives[c].bus_type == MO_BUS_ATAPI) {
	/* ATAPI MO, attach to the IDE bus. */
	id = ide_get_drive(mo_drives[c].ide_channel);
	/* If the IDE channel is initialized, we attach to it,
	   otherwise, we do nothing - it's going to be a drive
	   that's not attached to anything. */
	if (id) {
		id->sc = (scsi_common_t *) dev;
		id->get_max = mo_get_max;
		id->get_timings = mo_get_timings;
		id->identify = mo_identify;
		id->stop = NULL;
		id->packet_command = mo_command;
		id->device_reset = mo_reset;
		id->phase_data_out = mo_phase_data_out;
		id->command_stop = mo_command_stop;
		id->bus_master_error = mo_bus_master_error;
		id->interrupt_drq = 1;

		ide_atapi_attach(id);
	}
    }
}


void
mo_hard_reset(void)
{
    mo_t *dev;
    int c;
    uint8_t scsi_id, scsi_bus;

    for (c = 0; c < MO_NUM; c++) {
	if ((mo_drives[c].bus_type == MO_BUS_ATAPI) || (mo_drives[c].bus_type == MO_BUS_SCSI)) {
		mo_log("MO hard_reset drive=%d\n", c);

		if (mo_drives[c].bus_type == MO_BUS_SCSI) {
			scsi_bus = (mo_drives[c].scsi_device_id >> 4) & 0x0f;
			scsi_id = mo_drives[c].scsi_device_id & 0x0f;

			/* Make sure to ignore any SCSI MO drive that has an out of range SCSI Bus. */
			if (scsi_bus >= SCSI_BUS_MAX)
				continue;

			/* Make sure to ignore any SCSI MO drive that has an out of range ID. */
			if (scsi_id >= SCSI_ID_MAX)
				continue;
		}

		/* Make sure to ignore any ATAPI MO drive that has an out of range IDE channel. */
		if ((mo_drives[c].bus_type == MO_BUS_ATAPI) && (mo_drives[c].ide_channel > 7))
			continue;

		mo_drive_reset(c);

		dev = (mo_t *) mo_drives[c].priv;

		dev->id = c;
		dev->drv = &mo_drives[c];

		mo_init(dev);

		if (strlen(mo_drives[c].image_path))
			mo_load(dev, mo_drives[c].image_path);

		mo_mode_sense_load(dev);

		if (mo_drives[c].bus_type == MO_BUS_SCSI)
			mo_log("SCSI MO drive %i attached to SCSI ID %i\n", c, mo_drives[c].scsi_device_id);
		else if (mo_drives[c].bus_type == MO_BUS_ATAPI)
			mo_log("ATAPI MO drive %i attached to IDE channel %i\n", c, mo_drives[c].ide_channel);
	}
    }
}


void
mo_close(void)
{
    mo_t *dev;
    int c;
    uint8_t scsi_id, scsi_bus;

    for (c = 0; c < MO_NUM; c++) {
	if (mo_drives[c].bus_type == MO_BUS_SCSI) {
		scsi_bus = (mo_drives[c].scsi_device_id >> 4) & 0x0f;
		scsi_id = mo_drives[c].scsi_device_id & 0x0f;

		memset(&scsi_devices[scsi_bus][scsi_id], 0x00, sizeof(scsi_device_t));
	}

	dev = (mo_t *) mo_drives[c].priv;

	if (dev) {
		mo_disk_unload(dev);

		free(dev);
		mo_drives[c].priv = NULL;
	}
    }
}
