/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the FDI floppy stream image format
 *		interface to the FDI2RAW module.
 *
 * Version:	@(#)disc_fdi.c	1.0.0	2017/05/30
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */

#include <stdio.h>
#include <stdint.h>
#include <wchar.h>
#include "ibm.h"
#include "disc.h"
#include "disc_86f.h"
#include "disc_img.h"
#include "disc_fdi.h"
#include "fdc.h"
#include "fdd.h"
#include "fdi2raw.h"

static struct
{
        FILE *f;
        FDI *h;
        uint8_t track_data[2][4][256*1024];
        uint8_t track_timing[2][4][256*1024];
        
        int sides;
	int track;
        int tracklen[2][4];
        int trackindex[2][4];
        
        int lasttrack;
} fdi[FDD_NUM];

uint16_t fdi_disk_flags(int drive)
{
	uint16_t temp_disk_flags = 0x80;	/* We ALWAYS claim to have extra bit cells, even if the actual amount is 0. */

	switch (fdi2raw_get_bit_rate(fdi[drive].h))
	{
		case 500:
			temp_disk_flags |= 2;
			break;
		case 300:
		case 250:
			temp_disk_flags |= 0;
			break;
		case 1000:
			temp_disk_flags |= 4;
			break;
		default:
			temp_disk_flags |= 0;
	}

	if (fdi[drive].sides == 2)
	{
		temp_disk_flags |= 8;
	}

	/* Tell the 86F handler that will handle our data to handle it in reverse byte endianness. */
	temp_disk_flags |= 0x800;

	return temp_disk_flags;
}

uint16_t fdi_side_flags(int drive)
{
	uint16_t temp_side_flags = 0;
	switch (fdi2raw_get_bit_rate(fdi[drive].h))
	{
		case 500:
			temp_side_flags = 0;
			break;
		case 300:
			temp_side_flags = 1;
			break;
		case 250:
			temp_side_flags = 2;
			break;
		case 1000:
			temp_side_flags = 3;
			break;
		default:
			temp_side_flags = 2;
	}
	if (fdi2raw_get_rotation(fdi[drive].h) == 360)
	{
		temp_side_flags |= 0x20;
	}

	/* Set the encoding value to match that provided by the FDC. Then if it's wrong, it will sector not found anyway. */
	temp_side_flags |= 0x08;

	return temp_side_flags;
}

int fdi_density()
{
	if (!fdc_is_mfm())
	{
		return 0;
	}

	switch (fdc_get_bit_rate())
	{
		case 0:
			return 2;
		case 1:
			return 1;
		case 2:
			return 1;
		case 3:
		case 5:
			return 3;
		default:
			return 1;
	}
}

int32_t fdi_extra_bit_cells(int drive, int side)
{
	int density = 0;

	int raw_size = 0;

	int is_300_rpm = 0;

	density = fdi_density();

	is_300_rpm = (fdd_getrpm(real_drive(drive)) == 300);

	switch (fdc_get_bit_rate())
	{
		case 0:
			raw_size = is_300_rpm ? 200000 : 166666;
			break;
		case 1:
			raw_size = is_300_rpm ? 120000 : 100000;
			break;
		case 2:
			raw_size = is_300_rpm ? 100000 : 83333;
		case 3:
		case 5:
			raw_size = is_300_rpm ? 400000 : 333333;
			break;
		default:
			raw_size = is_300_rpm ? 100000 : 83333;
	}

	return (fdi[drive].tracklen[side][density] - raw_size);
}

int fdi_hole(int drive)
{
	switch (fdi2raw_get_bit_rate(fdi[drive].h))
	{
		case 1000:
			return 2;
		case 500:
			return 1;
		default:
			return 0;
	}
}

void fdi_read_revolution(int drive)
{
        int density;
	int track = fdi[drive].track;

	int side = 0;

	if (track > fdi[drive].lasttrack)
	{
	        for (density = 0; density < 4; density++)
        	{
			memset(fdi[drive].track_data[0][density], 0, 106096);
			memset(fdi[drive].track_data[1][density], 0, 106096);
			fdi[drive].tracklen[0][density] = fdi[drive].tracklen[1][density] = 100000;
		}
		return;
	}

        for (density = 0; density < 4; density++)
        {
		for (side = 0; side < fdi[drive].sides; side++)
		{
	                int c = fdi2raw_loadtrack(fdi[drive].h, (uint16_t *)fdi[drive].track_data[side][density],
        	                      (uint16_t *)fdi[drive].track_timing[side][density],
	                              (track * fdi[drive].sides) + side,
	                              &fdi[drive].tracklen[side][density],
	                              &fdi[drive].trackindex[side][density], NULL, density);
	                if (!c)
	                        memset(fdi[drive].track_data[side][density], 0, fdi[drive].tracklen[side][density]);
		}

		if (fdi[drive].sides == 1)
		{
			memset(fdi[drive].track_data[1][density], 0, 106096);
			fdi[drive].tracklen[1][density] = 100000;
		}
	}
}

uint32_t fdi_index_hole_pos(int drive, int side)
{
	int density;

	density = fdi_density();

	return fdi[drive].trackindex[side][density];
}

uint32_t fdi_get_raw_size(int drive, int side)
{
	int density;

	density = fdi_density();

	return fdi[drive].tracklen[side][density];
}

uint16_t* fdi_encoded_data(int drive, int side)
{
	int density = 0;

	density = fdi_density();

	return (uint16_t *)fdi[drive].track_data[side][density];
}

void d86f_register_fdi(int drive)
{
	d86f_handler[drive].disk_flags = fdi_disk_flags;
	d86f_handler[drive].side_flags = fdi_side_flags;
	d86f_handler[drive].writeback = null_writeback;
	d86f_handler[drive].set_sector = null_set_sector;
	d86f_handler[drive].write_data = null_write_data;
	d86f_handler[drive].format_conditions = null_format_conditions;
	d86f_handler[drive].extra_bit_cells = fdi_extra_bit_cells;
	d86f_handler[drive].encoded_data = fdi_encoded_data;
	d86f_handler[drive].read_revolution = fdi_read_revolution;
	d86f_handler[drive].index_hole_pos = fdi_index_hole_pos;
	d86f_handler[drive].get_raw_size = fdi_get_raw_size;
	d86f_handler[drive].check_crc = 1;
	d86f_set_version(drive, D86FVER);
}

void fdi_load(int drive, wchar_t *fn)
{
	char header[26];

        writeprot[drive] = fwriteprot[drive] = 1;
        fdi[drive].f = _wfopen(fn, L"rb");
        if (!fdi[drive].f)
	{
		memset(discfns[drive], 0, sizeof(discfns[drive]));
		return;
	}

	d86f_unregister(drive);

	fread(header, 1, 25, fdi[drive].f);
	fseek(fdi[drive].f, 0, SEEK_SET);
	header[25] = 0;
	if (strcmp(header, "Formatted Disk Image file") != 0)
	{
		/* This is a Japanese FDI file. */
		pclog("fdi_load(): Japanese FDI file detected, redirecting to IMG loader\n");
		fclose(fdi[drive].f);
		fdi[drive].f = NULL;
		img_load(drive, fn);
		return;
	}

        fdi[drive].h = fdi2raw_header(fdi[drive].f);
        fdi[drive].lasttrack = fdi2raw_get_last_track(fdi[drive].h);
        fdi[drive].sides = fdi2raw_get_last_head(fdi[drive].h) + 1;

	d86f_register_fdi(drive);

        drives[drive].seek        = fdi_seek;
	d86f_common_handlers(drive);

        pclog("Loaded as FDI\n");
}

void fdi_close(int drive)
{
	d86f_unregister(drive);
        drives[drive].seek        = NULL;
        if (fdi[drive].h)
                fdi2raw_header_free(fdi[drive].h);
        if (fdi[drive].f)
	{
                fclose(fdi[drive].f);
		fdi[drive].f = NULL;
	}
}

void fdi_seek(int drive, int track)
{
	if (fdd_doublestep_40(drive))
	{
		if (fdi2raw_get_tpi(fdi[drive].h) < 2)
		{
			track /= 2;
		}
	}
        
        if (!fdi[drive].f)
                return;
        if (track < 0)
                track = 0;
        if (track > fdi[drive].lasttrack)
                track = fdi[drive].lasttrack - 1;

	fdi[drive].track = track;

	fdi_read_revolution(drive);
}

void fdi_init()
{
	return;
}
