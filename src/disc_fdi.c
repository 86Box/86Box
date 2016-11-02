/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#include <stdio.h>
#include <stdint.h>
#include "ibm.h"
#include "disc.h"
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
} fdi[2];

uint16_t fdi_disk_flags(int drive)
{
	uint16_t temp_disk_flags = 0x80;	/* We ALWAYS claim to have extra bit cells, even if the actual amount is 0. */

	switch (fdi2raw_get_bit_rate(fdi[drive].h))
	{
		case 500:
			temp_disk_flags |= 2;
		case 300:
		case 250:
			temp_disk_flags |= 0;
		case 1000:
			temp_disk_flags |= 4;
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
		case 300:
			temp_side_flags = 1;
		case 250:
			temp_side_flags = 2;
		case 1000:
			temp_side_flags = 3;
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

int32_t fdi_extra_bit_cells(int drive)
{
	int side = 0;
	int density = 0;

	int raw_size = 0;

	side = fdd_get_head(drive);

	switch (fdc_get_bit_rate())
	{
		case 0:
			raw_size = (fdd_getrpm(drive ^ fdd_swap) == 300) ? 200000 : 166666;
			density = 2;
		case 1:
			raw_size = (fdd_getrpm(drive ^ fdd_swap) == 300) ? 120000 : 100000;
			density = 1;
		case 2:
			raw_size = (fdd_getrpm(drive ^ fdd_swap) == 300) ? 100000 : 83333;
			density = 1;
		case 3:
		case 5:
			raw_size = (fdd_getrpm(drive ^ fdd_swap) == 300) ? 400000 : 333333;
			density = 3;
		default:
			raw_size = (fdd_getrpm(drive ^ fdd_swap) == 300) ? 100000 : 83333;
			density = 1;
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
		// pclog("Track is bigger than last track\n");
		return;
	}

        for (density = 0; density < 4; density++)
        {
		for (side = 0; side < fdi[drive].sides; side++)
		{
	                int c = fdi2raw_loadtrack(fdi[drive].h, (uint16_t *)fdi[drive].track_data[side][density],
        	                      (uint16_t *)fdi[drive].track_timing[side][density],
	                              track * fdi[drive].sides,
	                              &fdi[drive].tracklen[side][density],
	                              &fdi[drive].trackindex[side][density], NULL, density);
			// pclog("Side 0 [%i]: len %i, index %i\n", density, fdi[drive].tracklen[side][density], fdi[drive].trackindex[side][density]);
	                if (!c)
	                        memset(fdi[drive].track_data[side][density], 0, fdi[drive].tracklen[side][density]);
		}
	}

	if (fdi[drive].sides == 1)
	{
		memset(fdi[drive].track_data[1][density], 0, 106096);
		fdi[drive].tracklen[1][density] = 100000;
	}
}

uint32_t fdi_index_hole_pos(int drive, int side)
{
	int density;

	switch (fdc_get_bit_rate())
	{
		case 0:
			density = 2;
		case 1:
			density = 1;
		case 2:
			density = 1;
		case 3:
		case 5:
			density = 3;
		default:
			density = 1;
	}

	return fdi[drive].trackindex[side][density];
}

uint32_t fdi_get_raw_size(int drive)
{
	int side = 0;
	int density;

	side = fdd_get_head(drive);

	switch (fdc_get_bit_rate())
	{
		case 0:
			density = 2;
		case 1:
			density = 1;
		case 2:
			density = 1;
		case 3:
		case 5:
			density = 3;
		default:
			density = 1;
	}

	return fdi[drive].tracklen[side][density];
}

uint16_t* fdi_encoded_data(int drive, int side)
{
	int density = 0;

        if (fdc_get_bit_rate() == 2)
                density = 1;
        if (fdc_get_bit_rate() == 0)
                density = 2;
        if (fdc_get_bit_rate() == 3)
                density = 3;

	if (!fdc_is_mfm())
	{
		density = 0;
	}

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
}

void fdi_load(int drive, char *fn)
{
	char header[26];

        writeprot[drive] = fwriteprot[drive] = 1;
        fdi[drive].f = fopen(fn, "rb");
        if (!fdi[drive].f) return;

	d86f_unregister(drive);

	fread(header, 1, 25, fdi[drive].f);
	fseek(fdi[drive].f, 0, SEEK_SET);
	header[25] = 0;
	if (strcmp(header, "Formatted Disk Image file") != 0)
	{
		/* This is a Japanese FDI file. */
		pclog("fdi_load(): Japanese FDI file detected, redirecting to IMG loader\n");
		fclose(fdi[drive].f);
		img_load(drive, fn);
		return;
	}

        fdi[drive].h = fdi2raw_header(fdi[drive].f);
//        if (!fdih[drive]) printf("Failed to load!\n");
        fdi[drive].lasttrack = fdi2raw_get_last_track(fdi[drive].h);
        fdi[drive].sides = fdi2raw_get_last_head(fdi[drive].h) + 1;
//        printf("Last track %i\n",fdilasttrack[drive]);

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
                fclose(fdi[drive].f);
        fdi[drive].f = NULL;
}

void fdi_seek(int drive, int track)
{
	if (fdd_doublestep_40(drive))
	{
		if (fdi[drive].lasttrack < 43)
		{
			track /= 2;
		}
		// pclog("fdi_seek(): %i %i\n", fdi[drive].lasttrack, track);
	}
        
        if (!fdi[drive].f)
                return;
//        printf("Track start %i\n",track);
        if (track < 0)
                track = 0;
        if (track > fdi[drive].lasttrack)
                track = fdi[drive].lasttrack - 1;

	fdi[drive].track = track;

	fdi_read_revolution(drive);
}

void fdi_init()
{
//        printf("FDI reset\n");
}
