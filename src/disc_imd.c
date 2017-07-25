/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the IMD floppy image format.
 *
 * Version:	@(#)disc_imd.c	1.0.0	2017/05/30
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2016-2017 Miran Grca.
 */

#include "ibm.h"
#include "disc.h"
#include "disc_imd.h"
#include "fdc.h"
#include "fdd.h"

#include <malloc.h>
#include <wchar.h>

typedef struct
{
	uint8_t is_present;
	uint32_t file_offs;
	uint8_t params[5];
	uint32_t r_map_offs;
	uint32_t c_map_offs;
	uint32_t h_map_offs;
	uint32_t n_map_offs;
	uint32_t data_offs;
	uint32_t sector_data_offs[255];
	uint32_t sector_data_size[255];
	uint32_t gap3_len;
	uint16_t side_flags;
} imd_track_t;

static struct
{
        FILE *f;
        char *buffer;
	uint32_t start_offs;
        int track_count, sides;
	int track;
	uint16_t disk_flags;
	int track_width;
	imd_track_t tracks[256][2];
	uint16_t current_side_flags[2];
	uint8_t xdf_ordered_pos[256][2];
	uint8_t interleave_ordered_pos[256][2];
	char *current_data[2];
	uint8_t track_buffer[2][25000];
} imd[FDD_NUM];

void imd_init()
{
        memset(imd, 0, sizeof(imd));
}

void d86f_register_imd(int drive);

void imd_load(int drive, wchar_t *fn)
{
	uint32_t magic = 0;
	uint32_t fsize = 0;
	char *buffer;
	char *buffer2;
	int i = 0;
	int track_spt = 0;
	int sector_size = 0;
	int track = 0;
	int side = 0;
	int extra = 0;
	uint32_t last_offset = 0;
	uint32_t data_size = 512;
	uint32_t mfm = 0;
	uint32_t pre_sector = 0;
	uint32_t track_total = 0;
	uint32_t raw_tsize = 0;
	uint32_t minimum_gap3 = 0;
	uint32_t minimum_gap4 = 0;

	d86f_unregister(drive);

	writeprot[drive] = 0;
        imd[drive].f = _wfopen(fn, L"rb+");
        if (!imd[drive].f)
        {
                imd[drive].f = _wfopen(fn, L"rb");
                if (!imd[drive].f)
		{
			memset(discfns[drive], 0, sizeof(discfns[drive]));
                        return;
		}
                writeprot[drive] = 1;
        }
	if (ui_writeprot[drive])
	{
                writeprot[drive] = 1;
	}
        fwriteprot[drive] = writeprot[drive];

	fseek(imd[drive].f, 0, SEEK_SET);
	fread(&magic, 1, 4, imd[drive].f);
	if (magic != 0x20444D49)
	{
		pclog("IMD: Not a valid ImageDisk image\n");
		fclose(imd[drive].f);
		memset(discfns[drive], 0, sizeof(discfns[drive]));
		return;
	}
	else
	{
		pclog("IMD: Valid ImageDisk image\n");
	}

	fseek(imd[drive].f, 0, SEEK_END);
	fsize = ftell(imd[drive].f);

	fseek(imd[drive].f, 0, SEEK_SET);
	imd[drive].buffer = malloc(fsize);
	fread(imd[drive].buffer, 1, fsize, imd[drive].f);
	buffer = imd[drive].buffer;

	buffer2 = strchr(buffer, 0x1A);
	if (buffer2 == NULL)
	{
		pclog("IMD: No ASCII EOF character\n");
		fclose(imd[drive].f);
		memset(discfns[drive], 0, sizeof(discfns[drive]));
		return;
	}
	else
	{
		pclog("IMD: ASCII EOF character found at offset %08X\n", buffer2 - buffer);
	}

	buffer2++;
	if ((buffer2 - buffer) == fsize)
	{
		pclog("IMD: File ends after ASCII EOF character\n");
		fclose(imd[drive].f);
		memset(discfns[drive], 0, sizeof(discfns[drive]));
		return;
	}
	else
	{
		pclog("IMD: File continues after ASCII EOF character\n");
	}

	imd[drive].start_offs = (buffer2 - buffer);
	imd[drive].disk_flags = 0x00;
	imd[drive].track_count = 0;
	imd[drive].sides = 1;

	while(1)
	{
		track = buffer2[1];
		side = buffer2[2];
		if (side & 1)  imd[drive].sides = 2;
		extra = side & 0xC0;
		side &= 0x3F;

		imd[drive].tracks[track][side].side_flags = (buffer2[0] % 3);
		if (!imd[drive].tracks[track][side].side_flags)  imd[drive].disk_flags |= (0x02);
		imd[drive].tracks[track][side].side_flags |= (!(buffer2[0] - imd[drive].tracks[track][side].side_flags) ? 0 : 8);
		mfm = imd[drive].tracks[track][side].side_flags & 8;
		track_total = mfm ? 146 : 73;
		pre_sector = mfm ? 60 : 42;
		
		track_spt = buffer2[3];
		sector_size = buffer2[4];
		if ((track_spt == 15) && (sector_size == 2))  imd[drive].tracks[track][side].side_flags |= 0x20;
		if ((track_spt == 16) && (sector_size == 2))  imd[drive].tracks[track][side].side_flags |= 0x20;
		if ((track_spt == 17) && (sector_size == 2))  imd[drive].tracks[track][side].side_flags |= 0x20;
		if ((track_spt == 8) && (sector_size == 3))  imd[drive].tracks[track][side].side_flags |= 0x20;
		if ((imd[drive].tracks[track][side].side_flags & 7) == 1)  imd[drive].tracks[track][side].side_flags |= 0x20;
		imd[drive].tracks[track][side].is_present = 1;
		imd[drive].tracks[track][side].file_offs = (buffer2 - buffer);
		memcpy(imd[drive].tracks[track][side].params, buffer2, 5);
		imd[drive].tracks[track][side].r_map_offs = imd[drive].tracks[track][side].file_offs + 5;
		last_offset = imd[drive].tracks[track][side].r_map_offs + track_spt;
		
		if (extra & 0x80)
		{
			imd[drive].tracks[track][side].c_map_offs = last_offset;
			last_offset += track_spt;
		}

		if (extra & 0x40)
		{
			imd[drive].tracks[track][side].h_map_offs = last_offset;
			last_offset += track_spt;
		}

		if (sector_size == 0xFF)
		{
			imd[drive].tracks[track][side].n_map_offs = last_offset;
			buffer2 = buffer + last_offset;
			last_offset += track_spt;

			imd[drive].tracks[track][side].data_offs = last_offset;

			for (i = 0; i < track_spt; i++)
			{
				data_size = buffer2[i];
				data_size = 128 << data_size;
				imd[drive].tracks[track][side].sector_data_offs[i] = last_offset;
				imd[drive].tracks[track][side].sector_data_size[i] = 1;
				if (buffer[imd[drive].tracks[track][side].sector_data_offs[i]] != 0)
				{
					imd[drive].tracks[track][side].sector_data_size[i] += (buffer[imd[drive].tracks[track][side].sector_data_offs[i]] & 1) ? data_size : 1;
				}
				last_offset += imd[drive].tracks[track][side].sector_data_size[i];
				if (!(buffer[imd[drive].tracks[track][side].sector_data_offs[i]] & 1))
				{
					fwriteprot[drive] = writeprot[drive] = 1;
				}
				track_total += (pre_sector + data_size + 2);
			}
		}
		else
		{
			imd[drive].tracks[track][side].data_offs = last_offset;

			for (i = 0; i < track_spt; i++)
			{
				data_size = sector_size;
				data_size = 128 << data_size;
				imd[drive].tracks[track][side].sector_data_offs[i] = last_offset;
				imd[drive].tracks[track][side].sector_data_size[i] = 1;
				if (buffer[imd[drive].tracks[track][side].sector_data_offs[i]] != 0)
				{
					imd[drive].tracks[track][side].sector_data_size[i] += (buffer[imd[drive].tracks[track][side].sector_data_offs[i]] & 1) ? data_size : 1;
				}
				last_offset += imd[drive].tracks[track][side].sector_data_size[i];
				if (!(buffer[imd[drive].tracks[track][side].sector_data_offs[i]] & 1))
				{
					fwriteprot[drive] = writeprot[drive] = 1;
				}
				track_total += (pre_sector + data_size + 2);
			}
		}
		buffer2 = buffer + last_offset;

		/* Leaving even GAP4: 80 : 40 */
		/* Leaving only GAP1: 96 : 47 */
		/* Not leaving even GAP1: 146 : 73 */
		raw_tsize = td0_get_raw_tsize(imd[drive].tracks[track][side].side_flags, 0);
		minimum_gap3 = 12 * track_spt;
		if ((raw_tsize - track_total + (mfm ? 146 : 73)) < (minimum_gap3 + minimum_gap4))
		{
			/* If we can't fit the sectors with a reasonable minimum gap at perfect RPM, let's try 2% slower. */
			raw_tsize = td0_get_raw_tsize(imd[drive].tracks[track][side].side_flags, 1);
			/* Set disk flags so that rotation speed is 2% slower. */
			imd[drive].disk_flags |= (3 << 5);
			if ((raw_tsize - track_total + (mfm ? 146 : 73)) < (minimum_gap3 + minimum_gap4))
			{
				/* If we can't fit the sectors with a reasonable minimum gap even at 2% slower RPM, abort. */
				pclog("IMD: Unable to fit the %i sectors in a track\n", track_spt);
				fclose(imd[drive].f);
				memset(discfns[drive], 0, sizeof(discfns[drive]));
				return;
			}
		}
		imd[drive].tracks[track][side].gap3_len = (raw_tsize - track_total - minimum_gap4 + (mfm ? 146 : 73)) / track_spt;

		imd[drive].track_count++;

		if (last_offset >= fsize)
		{
			break;
		}
	}

	imd[drive].track_width = 0;
	if (imd[drive].track_count > 43)  imd[drive].track_width = 1;	/* If the image has more than 43 tracks, then the tracks are thin (96 tpi). */
	if (imd[drive].sides == 2)   imd[drive].disk_flags |= 8;	/* If the has 2 sides, mark it as such. */

	d86f_register_imd(drive);

        drives[drive].seek        = imd_seek;

	d86f_common_handlers(drive);
}

void imd_close(int drive)
{
	int i = 0;
	d86f_unregister(drive);
        if (imd[drive].f)
	{
		free(imd[drive].buffer);
		for (i = 0; i < 256; i++)
		{
			memset(&(imd[drive].tracks[i][0]), 0, sizeof(imd_track_t));
			memset(&(imd[drive].tracks[i][1]), 0, sizeof(imd_track_t));
		}
                fclose(imd[drive].f);
	}
}

int imd_track_is_xdf(int drive, int side, int track)
{
	int i, effective_sectors, xdf_sectors;
	int high_sectors, low_sectors;
	int max_high_id, expected_high_count, expected_low_count;
	char *r_map;
	char *n_map;
	char *data_base;
	char *cur_data;

	effective_sectors = xdf_sectors = high_sectors = low_sectors = 0;

	for (i = 0; i < 256; i++)
	{
		imd[drive].xdf_ordered_pos[i][side] = 0;
	}

	if (imd[drive].tracks[track][side].params[2] & 0xC0)
	{
		return 0;
	}
	if ((imd[drive].tracks[track][side].params[3] != 16) && (imd[drive].tracks[track][side].params[3] != 19))
	{
		return 0;
	}
	r_map = imd[drive].buffer + imd[drive].tracks[track][side].r_map_offs;
	data_base = imd[drive].buffer + imd[drive].tracks[track][side].data_offs;

	if (!track)
	{
		if (imd[drive].tracks[track][side].params[4] != 2)
		{
			return 0;
		}
		if (!side)
		{
			max_high_id = (imd[drive].tracks[track][side].params[3] == 19) ? 0x8B : 0x88;
			expected_high_count = (imd[drive].tracks[track][side].params[3] == 19) ? 0x0B : 0x08;
			expected_low_count = 8;
		}
		else
		{
			max_high_id = (imd[drive].tracks[track][side].params[3] == 19) ? 0x93 : 0x90;
			expected_high_count = (imd[drive].tracks[track][side].params[3] == 19) ? 0x13 : 0x10;
			expected_low_count = 0;
		}
		for (i = 0; i < imd[drive].tracks[track][side].params[3]; i++)
		{
			if ((r_map[i] >= 0x81) && (r_map[i] <= max_high_id))
			{
				high_sectors++;
				imd[drive].xdf_ordered_pos[(int) r_map[i]][side] = i;
			}
			if ((r_map[i] >= 0x01) && (r_map[i] <= 0x08))
			{
				low_sectors++;
				imd[drive].xdf_ordered_pos[(int) r_map[i]][side] = i;
			}
			if ((high_sectors == expected_high_count) && (low_sectors == expected_low_count))
			{
				imd[drive].current_side_flags[side] = (imd[drive].tracks[track][side].params[3] == 19) ?  0x08 : 0x28;
				return (imd[drive].tracks[track][side].params[3] == 19) ? 2 : 1;
			}
			return 0;
		}
	}
	else
	{
		if (imd[drive].tracks[track][side].params[4] != 0xFF)
		{
			return 0;
		}

		n_map = imd[drive].buffer + imd[drive].tracks[track][side].n_map_offs;

		cur_data = data_base;
		for (i = 0; i < imd[drive].tracks[track][side].params[3]; i++)
		{
			effective_sectors++;
			if (!(r_map[i]) && !(n_map[i]))
			{
				effective_sectors--;
			}
			if ((r_map[i] == (n_map[i] | 0x80)))
			{
				xdf_sectors++;
				imd[drive].xdf_ordered_pos[(int) r_map[i]][side] = i;
			}
			cur_data += (128 << ((uint32_t) n_map[i]));
		}
		if ((effective_sectors == 3) && (xdf_sectors == 3))
		{
			imd[drive].current_side_flags[side] = 0x28;
			return 1;		/* 5.25" 2HD XDF */
		}
		if ((effective_sectors == 4) && (xdf_sectors == 4))
		{
			imd[drive].current_side_flags[side] = 0x08;
			return 2;		/* 3.5" 2HD XDF */
		}
		return 0;
	}

	return 0;
}

int imd_track_is_interleave(int drive, int side, int track)
{
	int i, effective_sectors;
	char *r_map;
	int track_spt;

	effective_sectors = 0;

	for (i = 0; i < 256; i++)
	{
		imd[drive].interleave_ordered_pos[i][side] = 0;
	}

	track_spt = imd[drive].tracks[track][side].params[3];

	r_map = imd[drive].buffer + imd[drive].tracks[track][side].r_map_offs;

	if (imd[drive].tracks[track][side].params[2] & 0xC0)
	{
		return 0;
	}
	if (track_spt != 21)
	{
		return 0;
	}
	if (imd[drive].tracks[track][side].params[4] != 2)
	{
		return 0;
	}

	for (i = 0; i < track_spt; i++)
	{
		if ((r_map[i] >= 1) && (r_map[i] <= track_spt))
		{
			effective_sectors++;
			imd[drive].interleave_ordered_pos[(int) r_map[i]][side] = i;
		}
	}

	if (effective_sectors == track_spt)
	{
		return 1;
	}
	return 0;
}

void imd_sector_to_buffer(int drive, int track, int side, uint8_t *buffer, int sector, int len)
{
	int type = imd[drive].buffer[imd[drive].tracks[track][side].sector_data_offs[sector]];
	if (type == 0)
	{
		memset(buffer, 0, len);
	}
	else
	{
		if (type & 1)
		{
			memcpy(buffer, &(imd[drive].buffer[imd[drive].tracks[track][side].sector_data_offs[sector] + 1]), len);
		}
		else
		{
			memset(buffer, imd[drive].buffer[imd[drive].tracks[track][side].sector_data_offs[sector] + 1], len);
		}
	}
}

void imd_seek(int drive, int track)
{
        int side;

	uint8_t id[4] = { 0, 0, 0, 0 };
	uint8_t type, deleted, bad_crc;

	int sector, current_pos;

	int c = 0;
	int h = 0;
	int n = 0;
	int ssize = 512;

	int track_rate = 0;

	int track_gap2 = 22;
	int track_gap3 = 12;

	int xdf_type = 0;
	int interleave_type = 0;

	int is_trackx = 0;

	int xdf_spt = 0;
	int xdf_sector = 0;

	int ordered_pos = 0;

	int real_sector = 0;
	int actual_sector = 0;

	char *c_map = NULL;
	char *h_map = NULL;
	char *r_map;
	char *n_map = NULL;
	uint8_t *data;
	uint32_t track_buf_pos[2] = { 0, 0 };
       
        if (!imd[drive].f)
                return;

        if (!imd[drive].track_width && fdd_doublestep_40(drive))
                track /= 2;

	is_trackx = (track == 0) ? 0 : 1;

	imd[drive].track = track;

	imd[drive].current_side_flags[0] = imd[drive].tracks[track][0].side_flags;
	imd[drive].current_side_flags[1] = imd[drive].tracks[track][1].side_flags;

	d86f_reset_index_hole_pos(drive, 0);
	d86f_reset_index_hole_pos(drive, 1);

	d86f_zero_bit_field(drive, 0);
	d86f_zero_bit_field(drive, 1);

	for (side = 0; side < imd[drive].sides; side++)
	{
		track_rate = imd[drive].current_side_flags[side] & 7;
		if (!track_rate && (imd[drive].current_side_flags[side] & 0x20))  track_rate = 4;
		if ((imd[drive].current_side_flags[side] & 0x27) == 0x21)  track_rate = 2;

		r_map = imd[drive].buffer + imd[drive].tracks[track][side].r_map_offs;
		h = imd[drive].tracks[track][side].params[2];
		if (h & 0x80)
		{
			c_map = imd[drive].buffer + imd[drive].tracks[track][side].c_map_offs;
		}
		else
		{
			c = imd[drive].tracks[track][side].params[1];
		}
		if (h & 0x40)
		{
			h_map = imd[drive].buffer + imd[drive].tracks[track][side].h_map_offs;
		}
		n = imd[drive].tracks[track][side].params[4];
		if (n == 0xFF)
		{
			n_map = imd[drive].buffer + imd[drive].tracks[track][side].n_map_offs;
			track_gap3 = gap3_sizes[track_rate][(int) n_map[0]][imd[drive].tracks[track][side].params[3]];
		}
		else
		{
			track_gap3 = gap3_sizes[track_rate][n][imd[drive].tracks[track][side].params[3]];
		}
		if (!track_gap3)
		{
			track_gap3 = imd[drive].tracks[track][side].gap3_len;
		}

		xdf_type = imd_track_is_xdf(drive, side, track);

		interleave_type = imd_track_is_interleave(drive, side, track);

		current_pos = d86f_prepare_pretrack(drive, side, 0);

		if (!xdf_type)
		{
			for (sector = 0; sector < imd[drive].tracks[track][side].params[3]; sector++)
			{
				if (interleave_type == 0)
				{
					real_sector = r_map[sector];
					actual_sector = sector;
				}
				else
				{
					real_sector = dmf_r[sector];
					actual_sector = imd[drive].interleave_ordered_pos[real_sector][side];
				}
				id[0] = (h & 0x80) ? c_map[actual_sector] : c;
				id[1] = (h & 0x40) ? h_map[actual_sector] : (h & 1);
				id[2] = real_sector;
				id[3] = (n == 0xFF) ? n_map[actual_sector] : n;
				ssize = 128 << ((uint32_t) id[3]);
				data = imd[drive].track_buffer[side] + track_buf_pos[side];
				type = imd[drive].buffer[imd[drive].tracks[track][side].sector_data_offs[actual_sector]];
				type = (type >> 1) & 7;
				deleted = bad_crc = 0;
				if ((type == 2) || (type == 4))  deleted = 1;
				if ((type == 3) || (type == 4))  bad_crc = 1;
				
				imd_sector_to_buffer(drive, track, side, data, actual_sector, ssize);
				current_pos = d86f_prepare_sector(drive, side, current_pos, id, data, ssize, 22, track_gap3, deleted, bad_crc);
				track_buf_pos[side] += ssize;

				if (sector == 0)
				{
					d86f_initialize_last_sector_id(drive, id[0], id[1], id[2], id[3]);
				}
			}
		}
		else
		{
			xdf_type--;
			xdf_spt = xdf_physical_sectors[xdf_type][is_trackx];
			for (sector = 0; sector < xdf_spt; sector++)
			{
				xdf_sector = (side * xdf_spt) + sector;
				id[0] = track;
				id[1] = side;
				id[2] = xdf_disk_layout[xdf_type][is_trackx][xdf_sector].id.r;
				id[3] = is_trackx ? (id[2] & 7) : 2;
				ssize = 128 << ((uint32_t) id[3]);
				ordered_pos = imd[drive].xdf_ordered_pos[id[2]][side];

				data = imd[drive].track_buffer[side] + track_buf_pos[side];
				type = imd[drive].buffer[imd[drive].tracks[track][side].sector_data_offs[ordered_pos]];
				type = (type >> 1) & 7;
				deleted = bad_crc = 0;
				if ((type == 2) || (type == 4))  deleted = 1;
				if ((type == 3) || (type == 4))  bad_crc = 1;
				imd_sector_to_buffer(drive, track, side, data, ordered_pos, ssize);

				if (is_trackx)
				{
					current_pos = d86f_prepare_sector(drive, side, xdf_trackx_spos[xdf_type][xdf_sector], id, data, ssize, track_gap2, xdf_gap3_sizes[xdf_type][is_trackx], deleted, bad_crc);
				}
				else
				{
					current_pos = d86f_prepare_sector(drive, side, current_pos, id, data, ssize, track_gap2, xdf_gap3_sizes[xdf_type][is_trackx], deleted, bad_crc);
				}

				track_buf_pos[side] += ssize;

				if (sector == 0)
				{
					d86f_initialize_last_sector_id(drive, id[0], id[1], id[2], id[3]);
				}
			}
		}
	}
}

uint16_t imd_disk_flags(int drive)
{
	return imd[drive].disk_flags;
}

uint16_t imd_side_flags(int drive)
{
	int side = 0;
	uint8_t sflags = 0;
	side = fdd_get_head(drive);
	sflags = imd[drive].current_side_flags[side];
	return sflags;
}

void imd_set_sector(int drive, int side, uint8_t c, uint8_t h, uint8_t r, uint8_t n)
{
	int i = 0;
	int track = imd[drive].track;
	int sc = 0;
	int sh = 0;
	int sn = 0;
	char *c_map, *h_map, *r_map, *n_map;
	uint8_t id[4] = { 0, 0, 0, 0 };
	sc = imd[drive].tracks[track][side].params[1];
	sh = imd[drive].tracks[track][side].params[2];
	sn = imd[drive].tracks[track][side].params[4];
	if (sh & 0x80)
	{
		c_map = imd[drive].buffer + imd[drive].tracks[track][side].c_map_offs;
	}
	if (sh & 0x40)
	{
		h_map = imd[drive].buffer + imd[drive].tracks[track][side].h_map_offs;
	}
	r_map = imd[drive].buffer + imd[drive].tracks[track][side].r_map_offs;
	if (sn == 0xFF)
	{
		n_map = imd[drive].buffer + imd[drive].tracks[track][side].n_map_offs;
	}
	if (c != imd[drive].track)  return;
	for (i = 0; i < imd[drive].tracks[track][side].params[3]; i++)
	{
		id[0] = (sh & 0x80) ? c_map[i] : sc;
		id[1] = (sh & 0x40) ? h_map[i] : (sh & 1);
		id[2] = r_map[i];
		id[3] = (sn == 0xFF) ? n_map[i] : sn;
		if ((id[0] == c) &&
		    (id[1] == h) &&
		    (id[2] == r) &&
		    (id[3] == n))
		{
			imd[drive].current_data[side] = imd[drive].buffer + imd[drive].tracks[track][side].sector_data_offs[i];
		}
	}
	return;
}

void imd_writeback(int drive)
{
	int side;
	int track = imd[drive].track;

	int i = 0;

	char *n_map;

	uint8_t h, n, spt;
	uint32_t ssize;

	if (writeprot[drive])
	{
		return;
	}

	for (side = 0; side < imd[drive].sides; side++)
	{
		if (imd[drive].tracks[track][side].is_present)
		{
			fseek(imd[drive].f, imd[drive].tracks[track][side].file_offs, SEEK_SET);
			h = imd[drive].tracks[track][side].params[2];
			spt = imd[drive].tracks[track][side].params[3];
			n = imd[drive].tracks[track][side].params[4];
			fwrite(imd[drive].tracks[track][side].params, 1, 5, imd[drive].f);
			if (h & 0x80)
			{
				fwrite(imd[drive].buffer + imd[drive].tracks[track][side].c_map_offs, 1, spt, imd[drive].f);
			}
			if (h & 0x40)
			{
				fwrite(imd[drive].buffer + imd[drive].tracks[track][side].h_map_offs, 1, spt, imd[drive].f);
			}
			if (n == 0xFF)
			{
				n_map = imd[drive].buffer + imd[drive].tracks[track][side].n_map_offs;
				fwrite(n_map, 1, spt, imd[drive].f);
			}
			for (i = 0; i < spt; i++)
			{
				ssize = (n == 0xFF) ? n_map[i] : n;
				ssize = 128 << ssize;
				fwrite(imd[drive].buffer + imd[drive].tracks[track][side].sector_data_offs[i], 1, ssize, imd[drive].f);
			}
		}
	}
}

uint8_t imd_poll_read_data(int drive, int side, uint16_t pos)
{
	int type = imd[drive].current_data[side][0];
	if (!(type & 1))
	{
		return 0xf6;		/* Should never happen. */
	}
	return imd[drive].current_data[side][pos + 1];
}

void imd_poll_write_data(int drive, int side, uint16_t pos, uint8_t data)
{
	int type = imd[drive].current_data[side][0];
	if (writeprot[drive])
	{
		return;
	}
	if (!(type & 1))
	{
		return;		/* Should never happen. */
	}
	imd[drive].current_data[side][pos + 1] = data;
}

int imd_format_conditions(int drive)
{
	int track = imd[drive].track;
	int side = 0;
	int temp = 0;
	side = fdd_get_head(drive);
	temp = (fdc_get_format_sectors() == imd[drive].tracks[track][side].params[3]);
	temp = temp && (fdc_get_format_n() == imd[drive].tracks[track][side].params[4]);
	return temp;
}

void d86f_register_imd(int drive)
{
	d86f_handler[drive].disk_flags = imd_disk_flags;
	d86f_handler[drive].side_flags = imd_side_flags;
	d86f_handler[drive].writeback = imd_writeback;
	d86f_handler[drive].set_sector = imd_set_sector;
	d86f_handler[drive].read_data = imd_poll_read_data;
	d86f_handler[drive].write_data = imd_poll_write_data;
	d86f_handler[drive].format_conditions = imd_format_conditions;
	d86f_handler[drive].extra_bit_cells = null_extra_bit_cells;
	d86f_handler[drive].encoded_data = common_encoded_data;
	d86f_handler[drive].read_revolution = common_read_revolution;
	d86f_handler[drive].index_hole_pos = null_index_hole_pos;
	d86f_handler[drive].get_raw_size = common_get_raw_size;
	d86f_handler[drive].check_crc = 1;
	d86f_set_version(drive, 0x0063);
}
