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
 * Version:	@(#)fdd_imd.c	1.0.9	2019/12/05
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2018,2019 Fred N. van Kempen.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../timer.h"
#include "../plat.h"
#include "fdd.h"
#include "fdd_86f.h"
#include "fdd_imd.h"
#include "fdc.h"


typedef struct {
    uint8_t	is_present;
    uint32_t	file_offs;
    uint8_t	params[5];
    uint32_t	r_map_offs;
    uint32_t	c_map_offs;
    uint32_t	h_map_offs;
    uint32_t	n_map_offs;
    uint32_t	data_offs;
    uint32_t	sector_data_offs[255];
    uint32_t	sector_data_size[255];
    uint32_t	gap3_len;
    uint16_t	side_flags;
    uint8_t	max_sector_size;
} imd_track_t;

typedef struct {
    FILE	*f;
    char	*buffer;
    uint32_t	start_offs;
    int		track_count, sides;
    int		track;
    uint16_t	disk_flags;
    int		track_width;
    imd_track_t	tracks[256][2];
    uint16_t	current_side_flags[2];
    uint8_t	xdf_ordered_pos[256][2];
    uint8_t	interleave_ordered_pos[256][2];
    char	*current_data[2];
    uint8_t	track_buffer[2][25000];
} imd_t;


static imd_t	*imd[FDD_NUM];
static fdc_t	*imd_fdc;


#ifdef ENABLE_IMD_LOG
int imd_do_log = ENABLE_IMD_LOG;


static void
imd_log(const char *fmt, ...)
{
   va_list ap;

   if (imd_do_log)
   {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
   }
}
#else
#define imd_log(fmt, ...)
#endif


static uint32_t
get_raw_tsize(int side_flags, int slower_rpm)
{
    uint32_t size;

    switch(side_flags & 0x27) {
	case 0x22:
		size = slower_rpm ?  5314 :  5208;
		break;

	default:
	case 0x02:
	case 0x21:
		size = slower_rpm ?  6375 :  6250;
		break;

	case 0x01:
		size = slower_rpm ?  7650 :  7500;
		break;

	case 0x20:
		size = slower_rpm ? 10629 : 10416;
		break;

	case 0x00:
		size = slower_rpm ? 12750 : 12500;
		break;

	case 0x23:
		size = slower_rpm ? 21258 : 20833;
		break;

	case 0x03:
		size = slower_rpm ? 25500 : 25000;
		break;

	case 0x25:
		size = slower_rpm ? 42517 : 41666;
		break;

	case 0x05:
		size = slower_rpm ? 51000 : 50000;
		break;
    }

    return(size);
}


static int
track_is_xdf(int drive, int side, int track)
{
    imd_t *dev = imd[drive];
    int i, effective_sectors, xdf_sectors;
    int high_sectors, low_sectors;
    int max_high_id, expected_high_count, expected_low_count;
    uint8_t *r_map;
    uint8_t *n_map;
    char *data_base;
    char *cur_data;

    effective_sectors = xdf_sectors = high_sectors = low_sectors = 0;

    for (i = 0; i < 256; i++)
	dev->xdf_ordered_pos[i][side] = 0;

    if (dev->tracks[track][side].params[2] & 0xC0) return(0);

    if ((dev->tracks[track][side].params[3] != 16) &&
	(dev->tracks[track][side].params[3] != 19)) return(0);

    r_map = (uint8_t *)(dev->buffer + dev->tracks[track][side].r_map_offs);
    data_base = dev->buffer + dev->tracks[track][side].data_offs;

    if (! track) {
	if (dev->tracks[track][side].params[4] != 2) return(0);

	if (! side) {
		max_high_id = (dev->tracks[track][side].params[3] == 19) ? 0x8B : 0x88;
		expected_high_count = (dev->tracks[track][side].params[3] == 19) ? 0x0B : 0x08;
		expected_low_count = 8;
	} else {
		max_high_id = (dev->tracks[track][side].params[3] == 19) ? 0x93 : 0x90;
		expected_high_count = (dev->tracks[track][side].params[3] == 19) ? 0x13 : 0x10;
		expected_low_count = 0;
	}

	for (i = 0; i < dev->tracks[track][side].params[3]; i++) {
		if ((r_map[i] >= 0x81) && (r_map[i] <= max_high_id)) {
			high_sectors++;
			dev->xdf_ordered_pos[(int) r_map[i]][side] = i;
		}
		if ((r_map[i] >= 0x01) && (r_map[i] <= 0x08)) {
			low_sectors++;
			dev->xdf_ordered_pos[(int) r_map[i]][side] = i;
		}
		if ((high_sectors == expected_high_count) && (low_sectors == expected_low_count)) {
			dev->current_side_flags[side] = (dev->tracks[track][side].params[3] == 19) ?  0x08 : 0x28;
			return((dev->tracks[track][side].params[3] == 19) ? 2 : 1);
		}
		return(0);
	}
    } else {
	if (dev->tracks[track][side].params[4] != 0xFF) return(0);

	n_map = (uint8_t *) (dev->buffer + dev->tracks[track][side].n_map_offs);

	cur_data = data_base;
	for (i = 0; i < dev->tracks[track][side].params[3]; i++) {
		effective_sectors++;
		if (!(r_map[i]) && !(n_map[i]))
			effective_sectors--;

		if (r_map[i] == (n_map[i] | 0x80)) {
			xdf_sectors++;
			dev->xdf_ordered_pos[(int) r_map[i]][side] = i;
		}
		cur_data += (128 << ((uint32_t) n_map[i]));
	}

	if ((effective_sectors == 3) && (xdf_sectors == 3)) {
		dev->current_side_flags[side] = 0x28;
		return(1);		/* 5.25" 2HD XDF */
	}

	if ((effective_sectors == 4) && (xdf_sectors == 4)) {
		dev->current_side_flags[side] = 0x08;
		return(2);		/* 3.5" 2HD XDF */
	}

	return(0);
    }

    return(0);
}


static int
track_is_interleave(int drive, int side, int track)
{
    imd_t *dev = imd[drive];
    int i, effective_sectors;
    char *r_map;
    int track_spt;

    effective_sectors = 0;

    for (i = 0; i < 256; i++)
	dev->interleave_ordered_pos[i][side] = 0;

    track_spt = dev->tracks[track][side].params[3];

    r_map = dev->buffer + dev->tracks[track][side].r_map_offs;

    if (dev->tracks[track][side].params[2] & 0xC0) return(0);

    if (track_spt != 21) return(0);

    if (dev->tracks[track][side].params[4] != 2) return(0);

    for (i = 0; i < track_spt; i++) {
	if ((r_map[i] >= 1) && (r_map[i] <= track_spt)) {
		effective_sectors++;
		dev->interleave_ordered_pos[(int) r_map[i]][side] = i;
	}
    }

    if (effective_sectors == track_spt) return(1);

    return(0);
}


static void
sector_to_buffer(int drive, int track, int side, uint8_t *buffer, int sector, int len)
{
    imd_t *dev = imd[drive];
    int type = dev->buffer[dev->tracks[track][side].sector_data_offs[sector]];

    if (type == 0)
	memset(buffer, 0x00, len);
      else {
	if (type & 1)
		memcpy(buffer, &(dev->buffer[dev->tracks[track][side].sector_data_offs[sector] + 1]), len);
	  else
		memset(buffer, dev->buffer[dev->tracks[track][side].sector_data_offs[sector] + 1], len);
    }
}


static void
imd_seek(int drive, int track)
{
    uint32_t track_buf_pos[2] = { 0, 0 };
    uint8_t id[4] = { 0, 0, 0, 0 };
    uint8_t type;
    imd_t *dev = imd[drive];
    int sector, current_pos;
    int side, c = 0, h, n;
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
    int flags = 0x00;

    if (dev->f == NULL) return;

    if (!dev->track_width && fdd_doublestep_40(drive))
	track /= 2;

    d86f_set_cur_track(drive, track);

    is_trackx = (track == 0) ? 0 : 1;

    dev->track = track;

    dev->current_side_flags[0] = dev->tracks[track][0].side_flags;
    dev->current_side_flags[1] = dev->tracks[track][1].side_flags;

    d86f_reset_index_hole_pos(drive, 0);
    d86f_reset_index_hole_pos(drive, 1);

    d86f_destroy_linked_lists(drive, 0);
    d86f_destroy_linked_lists(drive, 1);

    if (track > dev->track_count) {
	d86f_zero_track(drive);
	return;
    }

    for (side = 0; side < dev->sides; side++) {
	track_rate = dev->current_side_flags[side] & 7;
	if (!track_rate && (dev->current_side_flags[side] & 0x20))
		track_rate = 4;
	if ((dev->current_side_flags[side] & 0x27) == 0x21)
		track_rate = 2;

	r_map = dev->buffer + dev->tracks[track][side].r_map_offs;
	h = dev->tracks[track][side].params[2];
	if (h & 0x80)
		c_map = dev->buffer + dev->tracks[track][side].c_map_offs;
	else
		c = dev->tracks[track][side].params[1];

	if (h & 0x40)
		h_map = dev->buffer + dev->tracks[track][side].h_map_offs;

	n = dev->tracks[track][side].params[4];
	if (n == 0xFF) {
		n_map = dev->buffer + dev->tracks[track][side].n_map_offs;
		track_gap3 = gap3_sizes[track_rate][(int) n_map[0]][dev->tracks[track][side].params[3]];
	} else {
		track_gap3 = gap3_sizes[track_rate][n][dev->tracks[track][side].params[3]];
	}

	if (! track_gap3)
		track_gap3 = dev->tracks[track][side].gap3_len;

	xdf_type = track_is_xdf(drive, side, track);

	interleave_type = track_is_interleave(drive, side, track);

	current_pos = d86f_prepare_pretrack(drive, side, 0);

	if (! xdf_type) {
		for (sector = 0; sector < dev->tracks[track][side].params[3]; sector++) {
			if (interleave_type == 0) {
				real_sector = r_map[sector];
				actual_sector = sector;
			} else {
				real_sector = dmf_r[sector];
				actual_sector = dev->interleave_ordered_pos[real_sector][side];
			}
			id[0] = (h & 0x80) ? c_map[actual_sector] : c;
			id[1] = (h & 0x40) ? h_map[actual_sector] : (h & 1);
			id[2] = real_sector;
			id[3] = (n == 0xFF) ? n_map[actual_sector] : n;
			data = dev->track_buffer[side] + track_buf_pos[side];
			type = dev->buffer[dev->tracks[track][side].sector_data_offs[actual_sector]];
			type = (type >> 1) & 7;
			flags = 0x00;
			if ((type == 2) || (type == 4))
				flags |= SECTOR_DELETED_DATA;
			if ((type == 3) || (type == 4))
				flags |= SECTOR_CRC_ERROR;

			if (((flags & 0x02) || (id[3] > dev->tracks[track][side].max_sector_size)) && !fdd_get_turbo(drive))
				ssize = 3;
			else
				ssize = 128 << ((uint32_t) id[3]);

			sector_to_buffer(drive, track, side, data, actual_sector, ssize);
			current_pos = d86f_prepare_sector(drive, side, current_pos, id, data, ssize, 22, track_gap3, flags);
			track_buf_pos[side] += ssize;

			if (sector == 0)
				d86f_initialize_last_sector_id(drive, id[0], id[1], id[2], id[3]);
		}
	} else {
		xdf_type--;
		xdf_spt = xdf_physical_sectors[xdf_type][is_trackx];
		for (sector = 0; sector < xdf_spt; sector++) {
			xdf_sector = (side * xdf_spt) + sector;
			id[0] = track;
			id[1] = side;
			id[2] = xdf_disk_layout[xdf_type][is_trackx][xdf_sector].id.r;
			id[3] = is_trackx ? (id[2] & 7) : 2;
			ordered_pos = dev->xdf_ordered_pos[id[2]][side];

			data = dev->track_buffer[side] + track_buf_pos[side];
			type = dev->buffer[dev->tracks[track][side].sector_data_offs[ordered_pos]];
			type = (type >> 1) & 7;
			flags = 0x00;
			if ((type == 2) || (type == 4))
				flags |= SECTOR_DELETED_DATA;
			if ((type == 3) || (type == 4))
				flags |= SECTOR_CRC_ERROR;

			if (((flags & 0x02) || (id[3] > dev->tracks[track][side].max_sector_size)) && !fdd_get_turbo(drive))
				ssize = 3;
			else
				ssize = 128 << ((uint32_t) id[3]);

			sector_to_buffer(drive, track, side, data, ordered_pos, ssize);

			if (is_trackx)
				current_pos = d86f_prepare_sector(drive, side, xdf_trackx_spos[xdf_type][xdf_sector], id, data, ssize, track_gap2, xdf_gap3_sizes[xdf_type][is_trackx], flags);
			else
				current_pos = d86f_prepare_sector(drive, side, current_pos, id, data, ssize, track_gap2, xdf_gap3_sizes[xdf_type][is_trackx], flags);

			track_buf_pos[side] += ssize;

			if (sector == 0)
				d86f_initialize_last_sector_id(drive, id[0], id[1], id[2], id[3]);
		}
	}
    }
}


static uint16_t
disk_flags(int drive)
{
    imd_t *dev = imd[drive];

    return(dev->disk_flags);
}


static uint16_t
side_flags(int drive)
{
    imd_t *dev = imd[drive];
    int side = 0;
    uint16_t sflags = 0;

    side = fdd_get_head(drive);
    sflags = dev->current_side_flags[side];

    return(sflags);
}


static void
set_sector(int drive, int side, uint8_t c, uint8_t h, uint8_t r, uint8_t n)
{
    imd_t *dev = imd[drive];
    int track = dev->track;
    int i, sc, sh, sn;
    char *c_map = NULL, *h_map = NULL, *r_map = NULL, *n_map = NULL;
    uint8_t id[4] = { 0, 0, 0, 0 };
    sc = dev->tracks[track][side].params[1];
    sh = dev->tracks[track][side].params[2];
    sn = dev->tracks[track][side].params[4];

    if (sh & 0x80)
	c_map = dev->buffer + dev->tracks[track][side].c_map_offs;
    if (sh & 0x40)
	h_map = dev->buffer + dev->tracks[track][side].h_map_offs;
    r_map = dev->buffer + dev->tracks[track][side].r_map_offs;

    if (sn == 0xFF)
	n_map = dev->buffer + dev->tracks[track][side].n_map_offs;

    if (c != dev->track) return;

    for (i = 0; i < dev->tracks[track][side].params[3]; i++) {
	id[0] = (sh & 0x80) ? c_map[i] : sc;
	id[1] = (sh & 0x40) ? h_map[i] : (sh & 1);
	id[2] = r_map[i];
	id[3] = (sn == 0xFF) ? n_map[i] : sn;
	if ((id[0] == c) && (id[1] == h) && (id[2] == r) && (id[3] == n)) {
		dev->current_data[side] = dev->buffer + dev->tracks[track][side].sector_data_offs[i];
	}
    }
}


static void
imd_writeback(int drive)
{
    imd_t *dev = imd[drive];
    int side;
    int track = dev->track;
    int i = 0;
    char *n_map = 0;
    uint8_t h, n, spt;
    uint32_t ssize;

    if (writeprot[drive]) return;

    for (side = 0; side < dev->sides; side++) {
	if (dev->tracks[track][side].is_present) {
		fseek(dev->f, dev->tracks[track][side].file_offs, SEEK_SET);
		h = dev->tracks[track][side].params[2];
		spt = dev->tracks[track][side].params[3];
		n = dev->tracks[track][side].params[4];
		fwrite(dev->tracks[track][side].params, 1, 5, dev->f);

		if (h & 0x80)
			fwrite(dev->buffer + dev->tracks[track][side].c_map_offs, 1, spt, dev->f);

		if (h & 0x40)
			fwrite(dev->buffer + dev->tracks[track][side].h_map_offs, 1, spt, dev->f);

		if (n == 0xFF) {
			n_map = dev->buffer + dev->tracks[track][side].n_map_offs;
			fwrite(n_map, 1, spt, dev->f);
		}
		for (i = 0; i < spt; i++) {
			ssize = (n == 0xFF) ? n_map[i] : n;
			ssize = 128 << ssize;
			fwrite(dev->buffer + dev->tracks[track][side].sector_data_offs[i], 1, ssize, dev->f);
		}
	}
    }
}


static uint8_t
poll_read_data(int drive, int side, uint16_t pos)
{
    imd_t *dev = imd[drive];
    int type = dev->current_data[side][0];

    if (! (type & 1)) return(0xf6);		/* Should never happen. */

    return(dev->current_data[side][pos + 1]);
}


static void
poll_write_data(int drive, int side, uint16_t pos, uint8_t data)
{
    imd_t *dev = imd[drive];
    int type = dev->current_data[side][0];

    if (writeprot[drive]) return;

    if (! (type & 1)) return;		/* Should never happen. */

    dev->current_data[side][pos + 1] = data;
}


static int
format_conditions(int drive)
{
    imd_t *dev = imd[drive];
    int track = dev->track;
    int side, temp;

    side = fdd_get_head(drive);
    temp = (fdc_get_format_sectors(imd_fdc) == dev->tracks[track][side].params[3]);
    temp = temp && (fdc_get_format_n(imd_fdc) == dev->tracks[track][side].params[4]);

    return(temp);
}


void
imd_init(void)
{
    memset(imd, 0x00, sizeof(imd));
}


void
imd_load(int drive, wchar_t *fn)
{
    uint32_t magic = 0;
    uint32_t fsize = 0;
    char *buffer;
    char *buffer2;
    imd_t *dev;
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
    uint8_t converted_rate;
    uint8_t type;
    int size_diff, gap_sum;

    d86f_unregister(drive);

    writeprot[drive] = 0;

    /* Allocate a drive block. */
    dev = (imd_t *)malloc(sizeof(imd_t));
    memset(dev, 0x00, sizeof(imd_t));

    dev->f = plat_fopen(fn, L"rb+");
    if (dev->f == NULL) {
	dev->f = plat_fopen(fn, L"rb");
	if (dev->f == NULL) {
		memset(floppyfns[drive], 0, sizeof(floppyfns[drive]));
		return;
	}
	writeprot[drive] = 1;
    }

    if (ui_writeprot[drive])
	writeprot[drive] = 1;
    fwriteprot[drive] = writeprot[drive];

    fseek(dev->f, 0, SEEK_SET);
    fread(&magic, 1, 4, dev->f);
    if (magic != 0x20444D49) {
	imd_log("IMD: Not a valid ImageDisk image\n");
	fclose(dev->f);
	free(dev);
	memset(floppyfns[drive], 0, sizeof(floppyfns[drive]));
	return;
    } else
	imd_log("IMD: Valid ImageDisk image\n");

    fseek(dev->f, 0, SEEK_END);
    fsize = ftell(dev->f);
    fseek(dev->f, 0, SEEK_SET);
    dev->buffer = malloc(fsize);
    fread(dev->buffer, 1, fsize, dev->f);
    buffer = dev->buffer;

    buffer2 = strchr(buffer, 0x1A);
    if (buffer2 == NULL) {
	imd_log("IMD: No ASCII EOF character\n");
	fclose(dev->f);
	free(dev);
	memset(floppyfns[drive], 0, sizeof(floppyfns[drive]));
	return;
    } else {
	imd_log("IMD: ASCII EOF character found at offset %08X\n", buffer2 - buffer);
    }

    buffer2++;
    if ((buffer2 - buffer) == fsize) {
	imd_log("IMD: File ends after ASCII EOF character\n");
	fclose(dev->f);
	free(dev);
	memset(floppyfns[drive], 0, sizeof(floppyfns[drive]));
	return;
    } else {
	imd_log("IMD: File continues after ASCII EOF character\n");
    }

    dev->start_offs = (buffer2 - buffer);
    dev->disk_flags = 0x00;
    dev->track_count = 0;
    dev->sides = 1;

    /* Set up the drive unit. */
    imd[drive] = dev;

    while(1) {
	track = buffer2[1];
	side = buffer2[2];
	if (side & 1)
		dev->sides = 2;
	extra = side & 0xC0;
	side &= 0x3F;

	dev->tracks[track][side].side_flags = (buffer2[0] % 3);
	if (! dev->tracks[track][side].side_flags)
		dev->disk_flags |= (0x02);
	dev->tracks[track][side].side_flags |= (!(buffer2[0] - dev->tracks[track][side].side_flags) ? 0 : 8);
	mfm = dev->tracks[track][side].side_flags & 8;
	track_total = mfm ? 146 : 73;
	pre_sector = mfm ? 60 : 42;

	track_spt = buffer2[3];
	sector_size = buffer2[4];
	if ((track_spt == 15) && (sector_size == 2))
		dev->tracks[track][side].side_flags |= 0x20;
	if ((track_spt == 16) && (sector_size == 2))
		dev->tracks[track][side].side_flags |= 0x20;
	if ((track_spt == 17) && (sector_size == 2))
		dev->tracks[track][side].side_flags |= 0x20;
	if ((track_spt == 8) && (sector_size == 3))
		dev->tracks[track][side].side_flags |= 0x20;
	if ((dev->tracks[track][side].side_flags & 7) == 1)
		dev->tracks[track][side].side_flags |= 0x20;
	if ((dev->tracks[track][side].side_flags & 0x07) == 0x00)
		dev->tracks[track][side].max_sector_size = 6;
	else
		dev->tracks[track][side].max_sector_size = 5;
	if (!mfm)
		dev->tracks[track][side].max_sector_size--;
	/* imd_log("Side flags for (%02i)(%01i): %02X\n", track, side, dev->tracks[track][side].side_flags); */
	dev->tracks[track][side].is_present = 1;
	dev->tracks[track][side].file_offs = (buffer2 - buffer);
	memcpy(dev->tracks[track][side].params, buffer2, 5);
	dev->tracks[track][side].r_map_offs = dev->tracks[track][side].file_offs + 5;
	last_offset = dev->tracks[track][side].r_map_offs + track_spt;

	if (extra & 0x80) {
		dev->tracks[track][side].c_map_offs = last_offset;
		last_offset += track_spt;
	}

	if (extra & 0x40) {
		dev->tracks[track][side].h_map_offs = last_offset;
		last_offset += track_spt;
	}

	if (sector_size == 0xFF) {
		dev->tracks[track][side].n_map_offs = last_offset;
		buffer2 = buffer + last_offset;
		last_offset += track_spt;

		dev->tracks[track][side].data_offs = last_offset;

		for (i = 0; i < track_spt; i++) {
			data_size = buffer2[i];
			data_size = 128 << data_size;
			dev->tracks[track][side].sector_data_offs[i] = last_offset;
			dev->tracks[track][side].sector_data_size[i] = 1;
			if (buffer[dev->tracks[track][side].sector_data_offs[i]] != 0)
				dev->tracks[track][side].sector_data_size[i] += (buffer[dev->tracks[track][side].sector_data_offs[i]] & 1) ? data_size : 1;
			last_offset += dev->tracks[track][side].sector_data_size[i];
			if (!(buffer[dev->tracks[track][side].sector_data_offs[i]] & 1))
				fwriteprot[drive] = writeprot[drive] = 1;
			type = dev->buffer[dev->tracks[track][side].sector_data_offs[i]];
			type = (type >> 1) & 7;
			if ((type == 3) || (type == 4) || (data_size > (128 << dev->tracks[track][side].max_sector_size)))
				track_total += (pre_sector + 3);
			else
				track_total += (pre_sector + data_size + 2);
		}
	} else {
		dev->tracks[track][side].data_offs = last_offset;

		for (i = 0; i < track_spt; i++) {
			data_size = sector_size;
			data_size = 128 << data_size;
			dev->tracks[track][side].sector_data_offs[i] = last_offset;
			dev->tracks[track][side].sector_data_size[i] = 1;
			if (buffer[dev->tracks[track][side].sector_data_offs[i]] != 0)
				dev->tracks[track][side].sector_data_size[i] += (buffer[dev->tracks[track][side].sector_data_offs[i]] & 1) ? data_size : 1;
			last_offset += dev->tracks[track][side].sector_data_size[i];
			if (!(buffer[dev->tracks[track][side].sector_data_offs[i]] & 1))
				fwriteprot[drive] = writeprot[drive] = 1;
			type = dev->buffer[dev->tracks[track][side].sector_data_offs[i]];
			type = (type >> 1) & 7;
			if ((type == 3) || (type == 4) || (sector_size > dev->tracks[track][side].max_sector_size))
				track_total += (pre_sector + 3);
			else
				track_total += (pre_sector + data_size + 2);
		}
	}
	buffer2 = buffer + last_offset;

	/* Leaving even GAP4: 80 : 40 */
	/* Leaving only GAP1: 96 : 47 */
	/* Not leaving even GAP1: 146 : 73 */
	raw_tsize = get_raw_tsize(dev->tracks[track][side].side_flags, 0);
	minimum_gap3 = 12 * track_spt;

	if ((dev->tracks[track][side].side_flags == 0x0A) || (dev->tracks[track][side].side_flags == 0x29))
		converted_rate = 2;
	else if (dev->tracks[track][side].side_flags == 0x28)
		converted_rate = 4;
	else
		converted_rate = dev->tracks[track][side].side_flags & 0x03;

	if (gap3_sizes[converted_rate][sector_size][track_spt] == 0x00) {
		size_diff = raw_tsize - track_total;
		gap_sum = minimum_gap3 + minimum_gap4;
		if (size_diff < gap_sum) {
			/* If we can't fit the sectors with a reasonable minimum gap at perfect RPM, let's try 2% slower. */
			raw_tsize = get_raw_tsize(dev->tracks[track][side].side_flags, 1);
			/* Set disk flags so that rotation speed is 2% slower. */
			dev->disk_flags |= (3 << 5);
			size_diff = raw_tsize - track_total;
			if (size_diff < gap_sum) {
				/* If we can't fit the sectors with a reasonable minimum gap even at 2% slower RPM, abort. */
				imd_log("IMD: Unable to fit the %i sectors in a track\n", track_spt);
				fclose(dev->f);
				free(dev);
				imd[drive] = NULL;
				memset(floppyfns[drive], 0, sizeof(floppyfns[drive]));
				return;
			}
		}

		dev->tracks[track][side].gap3_len = (size_diff - minimum_gap4) / track_spt;
	} else if (gap3_sizes[converted_rate][sector_size][track_spt] != 0x00)
		dev->tracks[track][side].gap3_len = gap3_sizes[converted_rate][sector_size][track_spt];

	/* imd_log("GAP3 length for (%02i)(%01i): %i bytes\n", track, side, dev->tracks[track][side].gap3_len); */

	if (track > dev->track_count)
		dev->track_count = track;

	if (last_offset >= fsize) break;
    }

    /* If more than 43 tracks, then the tracks are thin (96 tpi). */
    dev->track_count++;
    dev->track_width = 0;
    if (dev->track_count > 43)
	dev->track_width = 1;

    /* If 2 sides, mark it as such. */
    if (dev->sides == 2)
	dev->disk_flags |= 8;

    /* imd_log("%i tracks, %i sides\n", dev->track_count, dev->sides); */

    /* Attach this format to the D86F engine. */
    d86f_handler[drive].disk_flags = disk_flags;
    d86f_handler[drive].side_flags = side_flags;
    d86f_handler[drive].writeback = imd_writeback;
    d86f_handler[drive].set_sector = set_sector;
    d86f_handler[drive].read_data = poll_read_data;
    d86f_handler[drive].write_data = poll_write_data;
    d86f_handler[drive].format_conditions = format_conditions;
    d86f_handler[drive].extra_bit_cells = null_extra_bit_cells;
    d86f_handler[drive].encoded_data = common_encoded_data;
    d86f_handler[drive].read_revolution = common_read_revolution;
    d86f_handler[drive].index_hole_pos = null_index_hole_pos;
    d86f_handler[drive].get_raw_size = common_get_raw_size;
    d86f_handler[drive].check_crc = 1;
    d86f_set_version(drive, 0x0063);

    drives[drive].seek = imd_seek;

    d86f_common_handlers(drive);
}


void
imd_close(int drive)
{
    imd_t *dev = imd[drive];

    if (dev == NULL) return;

    d86f_unregister(drive);

    if (dev->f != NULL) {
	free(dev->buffer);

	fclose(dev->f);
    }

    /* Release the memory. */
    free(dev);
    imd[drive] = NULL;
}


void
imd_set_fdc(void *fdc)
{
    imd_fdc = (fdc_t *) fdc;
}
