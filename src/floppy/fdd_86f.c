/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the 86F floppy image format (stores the
 *		data in the form of FM/MFM-encoded transitions) which also
 *		forms the core of the emulator's floppy disk emulation.
 *
 * Version:	@(#)fdd_86f.c	1.0.20	2019/12/06
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2018,2019 Fred N. van Kempen.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../timer.h"
#include "../dma.h"
#include "../nvr.h"
#include "../random.h"
#include "../plat.h"
#include "../ui.h"
#include "fdd.h"
#include "fdc.h"
#include "fdd_86f.h"
#ifdef D86F_COMPRESS
#include "lzf/lzf.h"
#endif


/*
 * Let's give this some more logic:
 *
 * Bits 4,3 = Read/write (0 = read, 1 = write, 2 = scan, 3 = verify)
 * Bits 6,5 = Sector/track (0 = ID, 1 = sector, 2 = deleted sector, 3 = track)
 * Bit  7   = State type (0 = idle states, 1 = active states)
 */
enum {
    /* 0 ?? ?? ??? */
    STATE_IDLE = 0x00,
    STATE_SECTOR_NOT_FOUND,

    /* 1 00 00 ??? */
    STATE_0A_FIND_ID = 0x80,		/* READ SECTOR ID */
    STATE_0A_READ_ID,

    /* 1 01 00 ??? */
    STATE_06_FIND_ID = 0xA0,		/* READ DATA */
    STATE_06_READ_ID,
    STATE_06_FIND_DATA,
    STATE_06_READ_DATA,

    /* 1 01 01 ??? */
    STATE_05_FIND_ID = 0xA8,		/* WRITE DATA */
    STATE_05_READ_ID,
    STATE_05_FIND_DATA,
    STATE_05_WRITE_DATA,

    /* 1 01 10 ??? */
    STATE_11_FIND_ID = 0xB0,	/* SCAN EQUAL,SCAN LOW/EQUAL,SCAN HIGH/EQUAL */
    STATE_11_READ_ID,
    STATE_11_FIND_DATA,
    STATE_11_SCAN_DATA,

    /* 1 01 11 ??? */
    STATE_16_FIND_ID = 0xB8,		/* VERIFY */
    STATE_16_READ_ID,
    STATE_16_FIND_DATA,
    STATE_16_VERIFY_DATA,

    /* 1 10 00 ??? */
    STATE_0C_FIND_ID = 0xC0,		/* READ DELETED DATA */
    STATE_0C_READ_ID,
    STATE_0C_FIND_DATA,
    STATE_0C_READ_DATA,

    /* 1 10 01 ??? */
    STATE_09_FIND_ID = 0xC8,		/* WRITE DELETED DATA */
    STATE_09_READ_ID,
    STATE_09_FIND_DATA,
    STATE_09_WRITE_DATA,

    /* 1 11 00 ??? */
    STATE_02_SPIN_TO_INDEX = 0xE0,	/* READ TRACK */
    STATE_02_FIND_ID,
    STATE_02_READ_ID,
    STATE_02_FIND_DATA,
    STATE_02_READ_DATA,

    /* 1 11 01 ??? */
    STATE_0D_SPIN_TO_INDEX = 0xE8,	/* FORMAT TRACK */
    STATE_0D_FORMAT_TRACK,

    /* 1 11 11 ??? */
    STATE_0D_NOP_SPIN_TO_INDEX = 0xF8,	/* FORMAT TRACK */
    STATE_0D_NOP_FORMAT_TRACK
};

enum {
    FMT_PRETRK_GAP0,
    FMT_PRETRK_SYNC,
    FMT_PRETRK_IAM,
    FMT_PRETRK_GAP1,

    FMT_SECTOR_ID_SYNC,
    FMT_SECTOR_IDAM,
    FMT_SECTOR_ID,
    FMT_SECTOR_ID_CRC,
    FMT_SECTOR_GAP2,
    FMT_SECTOR_DATA_SYNC,
    FMT_SECTOR_DATAAM,
    FMT_SECTOR_DATA,
    FMT_SECTOR_DATA_CRC,
    FMT_SECTOR_GAP3,

    FMT_POSTTRK_CHECK,
    FMT_POSTTRK_GAP4
};


typedef struct {
    uint8_t	buffer[10];
    uint32_t	pos;
    uint32_t	len;
} sliding_buffer_t;

typedef struct {
    uint32_t	sync_marks;
    uint32_t	bits_obtained;
    uint32_t	bytes_obtained;
    uint32_t	sync_pos;
} find_t;

typedef struct {
    unsigned nibble0	:4;
    unsigned nibble1	:4;
} split_byte_t;

typedef union {
    uint8_t	byte;
    split_byte_t nibbles;
} decoded_t;

typedef struct {
    uint8_t	c, h, r, n;
    uint8_t	flags, pad, pad0, pad1;
    void	*prev;
} sector_t;

/* Disk flags:
 *  Bit 0	Has surface data (1 = yes, 0 = no)
 *  Bits 2, 1	Hole (3 = ED + 2000 kbps, 2 = ED, 1 = HD, 0 = DD)
 *  Bit 3	Sides (1 = 2 sides, 0 = 1 side)
 *  Bit 4	Write protect (1 = yes, 0 = no)
 *  Bits 6, 5	RPM slowdown (3 = 2%, 2 = 1.5%, 1 = 1%, 0 = 0%)
 *  Bit 7	Bitcell mode (1 = Extra bitcells count specified after
 *		disk flags, 0 = No extra bitcells)
 *		The maximum number of extra bitcells is 1024 (which
 *		after decoding translates to 64 bytes)
 *  Bit 8	Disk type (1 = Zoned, 0 = Fixed RPM)
 *  Bits 10, 9	Zone type (3 = Commodore 64 zoned, 2 = Apple zoned,
 *		1 = Pre-Apple zoned #2, 0 = Pre-Apple zoned #1)
 *  Bit 11	Data and surface bits are stored in reverse byte endianness
 *  Bit 12	If bits 6, 5 are not 0, they specify % of speedup instead
 *		of slowdown;
 *		If bits 6, 5 are 0, and bit 7 is 1, the extra bitcell count
 *		specifies the entire bitcell count
 */
typedef struct {
    FILE	*f;
    uint16_t	version;
    uint16_t	disk_flags;
    int32_t	extra_bit_cells[2];
    uint16_t	track_encoded_data[2][53048];
    uint16_t	*track_surface_data[2];
    uint16_t	thin_track_encoded_data[2][2][53048];
    uint16_t	*thin_track_surface_data[2][2];
    uint16_t	side_flags[2];
    uint32_t	index_hole_pos[2];
    uint32_t	track_offset[512];
    uint32_t	file_size;
    sector_id_t	format_sector_id;
    sector_id_t	last_sector;
    sector_id_t	req_sector;
    uint32_t	index_count;
    uint8_t	state;
    uint8_t	fill;
    uint32_t	track_pos;
    uint32_t	datac;
    uint32_t	id_pos;
    uint16_t	last_word[2];
    find_t	id_find;
    find_t	data_find;
    crc_t	calc_crc;
    crc_t	track_crc;
    uint8_t	sector_count;
    uint8_t	format_state;
    uint16_t	satisfying_bytes;
    uint16_t	preceding_bit[2];
    uint16_t	current_byte[2];
    uint16_t	current_bit[2];
    int		cur_track;
    uint32_t	error_condition;
#ifdef D86F_COMPRESS
    int		is_compressed;
#endif
    int		id_found;
    wchar_t	original_file_name[2048];
    uint8_t	*filebuf;
    uint8_t	*outbuf;
    uint32_t	dma_over;
    int		turbo_pos;
    sector_t	*last_side_sector[2];
} d86f_t;


static const uint8_t encoded_fm[64] = {
    0xaa, 0xab, 0xae, 0xaf, 0xba, 0xbb, 0xbe, 0xbf,
    0xea, 0xeb, 0xee, 0xef, 0xfa, 0xfb, 0xfe, 0xff,
    0xaa, 0xab, 0xae, 0xaf, 0xba, 0xbb, 0xbe, 0xbf,
    0xea, 0xeb, 0xee, 0xef, 0xfa, 0xfb, 0xfe, 0xff,
    0xaa, 0xab, 0xae, 0xaf, 0xba, 0xbb, 0xbe, 0xbf,
    0xea, 0xeb, 0xee, 0xef, 0xfa, 0xfb, 0xfe, 0xff,
    0xaa, 0xab, 0xae, 0xaf, 0xba, 0xbb, 0xbe, 0xbf,
    0xea, 0xeb, 0xee, 0xef, 0xfa, 0xfb, 0xfe, 0xff
};
static const uint8_t encoded_mfm[64] = {
    0xaa, 0xa9, 0xa4, 0xa5, 0x92, 0x91, 0x94, 0x95,
    0x4a, 0x49, 0x44, 0x45, 0x52, 0x51, 0x54, 0x55,
    0x2a, 0x29, 0x24, 0x25, 0x12, 0x11, 0x14, 0x15,
    0x4a, 0x49, 0x44, 0x45, 0x52, 0x51, 0x54, 0x55,
    0xaa, 0xa9, 0xa4, 0xa5, 0x92, 0x91, 0x94, 0x95,
    0x4a, 0x49, 0x44, 0x45, 0x52, 0x51, 0x54, 0x55,
    0x2a, 0x29, 0x24, 0x25, 0x12, 0x11, 0x14, 0x15,
    0x4a, 0x49, 0x44, 0x45, 0x52, 0x51, 0x54, 0x55
};

static d86f_t	*d86f[FDD_NUM];
static uint16_t	CRCTable[256];
static fdc_t	*d86f_fdc;
uint64_t	poly = 0x42F0E1EBA9EA3693ll;		/* ECMA normal */
uint64_t	table[256];


uint16_t d86f_side_flags(int drive);
int d86f_is_mfm(int drive);
void d86f_writeback(int drive);
uint8_t d86f_poll_read_data(int drive, int side, uint16_t pos);
void d86f_poll_write_data(int drive, int side, uint16_t pos, uint8_t data);
int d86f_format_conditions(int drive);


#ifdef ENABLE_D86F_LOG
int	d86f_do_log = ENABLE_D86F_LOG;


static void
d86f_log(const char *fmt, ...)
{
    va_list ap;

    if (d86f_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define d86f_log(fmt, ...)
#endif


static void
setup_crc(uint16_t poly)
{
    int c = 256, bc;
    uint16_t temp;

    while(c--) {
	temp = c << 8;
	bc = 8;

	while (bc--) {
		if (temp & 0x8000)
			temp = (temp << 1) ^ poly;
		  else
			temp <<= 1;

		CRCTable[c] = temp;
	}
    }
}


void
d86f_destroy_linked_lists(int drive, int side)
{
    d86f_t *dev = d86f[drive];
    sector_t *s, *t;

    if (dev == NULL) return;

    if (dev->last_side_sector[side]) {
	s = dev->last_side_sector[side];
	while (s) {
		t = s->prev;
		free(s);
		s = NULL;
		if (! t)
			break;
		s = t;
 	}
	dev->last_side_sector[side] = NULL;
    }
}


static int
d86f_has_surface_desc(int drive)
{
    return (d86f_handler[drive].disk_flags(drive) & 1);
}


int
d86f_get_sides(int drive)
{
    return ((d86f_handler[drive].disk_flags(drive) >> 3) & 1) + 1;
}


int
d86f_get_rpm_mode(int drive)
{
    return (d86f_handler[drive].disk_flags(drive) & 0x60) >> 5;
}


int
d86f_get_speed_shift_dir(int drive)
{
    return (d86f_handler[drive].disk_flags(drive) & 0x1000) >> 12;
}


int
d86f_reverse_bytes(int drive)
{
    return (d86f_handler[drive].disk_flags(drive) & 0x800) >> 11;
}


uint16_t
d86f_disk_flags(int drive)
{
    d86f_t *dev = d86f[drive];

    return dev->disk_flags;
}


uint32_t
d86f_index_hole_pos(int drive, int side)
{
    d86f_t *dev = d86f[drive];

    return dev->index_hole_pos[side];
}


uint32_t
null_index_hole_pos(int drive, int side)
{
    return 0;
}


uint16_t
null_disk_flags(int drive)
{
    return 0x09;
}


uint16_t
null_side_flags(int drive)
{
    return 0x0A;
}


void
null_writeback(int drive)
{
    return;
}


void
null_set_sector(int drive, int side, uint8_t c, uint8_t h, uint8_t r, uint8_t n)
{
    return;
}


void
null_write_data(int drive, int side, uint16_t pos, uint8_t data)
{
    return;
}


int
null_format_conditions(int drive)
{
    return 0;
}


int32_t
d86f_extra_bit_cells(int drive, int side)
{
    d86f_t *dev = d86f[drive];

    return dev->extra_bit_cells[side];
}


int32_t
null_extra_bit_cells(int drive, int side)
{
    return 0;
}


uint16_t*
common_encoded_data(int drive, int side)
{
    d86f_t *dev = d86f[drive];

    return dev->track_encoded_data[side];
}


void
common_read_revolution(int drive)
{
    return;
}


uint16_t
d86f_side_flags(int drive)
{
    d86f_t *dev = d86f[drive];
    int side;

    side = fdd_get_head(drive);

    return dev->side_flags[side];
}


uint16_t
d86f_track_flags(int drive)
{
    uint16_t dr, rr, tf;

    tf = d86f_handler[drive].side_flags(drive);
    rr = tf & 0x67;
    dr = fdd_get_flags(drive) & 7;
    tf &= ~0x67;

    switch (rr) {
	case 0x02:
	case 0x21:
		/* 1 MB unformatted medium, treat these two as equivalent. */
		switch (dr) {
			case 0x06:
				/* 5.25" Single-RPM HD drive, treat as 300 kbps, 360 rpm. */
				tf |= 0x21;
				break;

			default:
				/* Any other drive, treat as 250 kbps, 300 rpm. */
				tf |= 0x02;
				break;
		}
		break;

	default:
		tf |= rr;
		break;
    }

    return tf;
}


uint32_t
common_get_raw_size(int drive, int side)
{
    double rate = 0.0;
    double rpm, rpm_diff;
    double size = 100000.0;
    int mfm;
    int rm, ssd;
    uint32_t extra_bc = 0;

    mfm = d86f_is_mfm(drive);
    rpm = ((d86f_track_flags(drive) & 0xE0) == 0x20) ? 360.0 : 300.0;
    rpm_diff = 1.0;
    rm = d86f_get_rpm_mode(drive);
    ssd = d86f_get_speed_shift_dir(drive);

    /* 0% speed shift and shift direction 1: special case where extra bit cells are the entire track size. */
    if (!rm && ssd)
	extra_bc = d86f_handler[drive].extra_bit_cells(drive, side);

    if (extra_bc)
	return extra_bc;

    switch (rm) {
	case 1:
		rpm_diff = 1.01;
		break;

	case 2:
		rpm_diff = 1.015;
		break;

	case 3:
		rpm_diff = 1.02;
		break;

	default:
		rpm_diff = 1.0;
		break;
    }

    if (ssd)
	rpm_diff = 1.0 / rpm_diff;

    switch (d86f_track_flags(drive) & 7) {
	case 0:
		rate = 500.0;
		break;

	case 1:
		rate = 300.0;
		break;

	case 2:
		rate = 250.0;
		break;

	case 3:
		rate = 1000.0;
		break;

	case 5:
		rate = 2000.0;
		break;

	default:
		rate = 250.0;
		break;
    }

    if (! mfm) rate /= 2.0;

    size = (size / 250.0) * rate;
    size = (size * 300.0) / rpm;
    size *= rpm_diff;

    /*
     * Round down to a multiple of 16 and add the extra bit cells,
     * then return.
     */
    return ((((uint32_t) size) >> 4) << 4) + d86f_handler[drive].extra_bit_cells(drive, side);
}


void
d86f_set_version(int drive, uint16_t version)
{
    d86f_t *dev = d86f[drive];

    dev->version = version;
}


void
d86f_unregister(int drive)
{
    d86f_t *dev = d86f[drive];

    if (dev == NULL) return;

    d86f_handler[drive].disk_flags = null_disk_flags;
    d86f_handler[drive].side_flags = null_side_flags;
    d86f_handler[drive].writeback = null_writeback;
    d86f_handler[drive].set_sector = null_set_sector;
    d86f_handler[drive].write_data = null_write_data;
    d86f_handler[drive].format_conditions = null_format_conditions;
    d86f_handler[drive].extra_bit_cells = null_extra_bit_cells;
    d86f_handler[drive].encoded_data = common_encoded_data;
    d86f_handler[drive].read_revolution = common_read_revolution;
    d86f_handler[drive].index_hole_pos = null_index_hole_pos;
    d86f_handler[drive].get_raw_size = common_get_raw_size;
    d86f_handler[drive].check_crc = 0;

    dev->version = 0x0063;	/* Proxied formats report as version 0.99. */
}


void
d86f_register_86f(int drive)
{
    d86f_handler[drive].disk_flags = d86f_disk_flags;
    d86f_handler[drive].side_flags = d86f_side_flags;
    d86f_handler[drive].writeback = d86f_writeback;
    d86f_handler[drive].set_sector = null_set_sector;
    d86f_handler[drive].write_data = null_write_data;
    d86f_handler[drive].format_conditions = d86f_format_conditions;
    d86f_handler[drive].extra_bit_cells = d86f_extra_bit_cells;
    d86f_handler[drive].encoded_data = common_encoded_data;
    d86f_handler[drive].read_revolution = common_read_revolution;
    d86f_handler[drive].index_hole_pos = d86f_index_hole_pos;
    d86f_handler[drive].get_raw_size = common_get_raw_size;
    d86f_handler[drive].check_crc = 1;
}


int
d86f_get_array_size(int drive, int side, int words)
{
    int array_size;
    int hole, rm;
    int ssd;

    rm = d86f_get_rpm_mode(drive);
    ssd = d86f_get_speed_shift_dir(drive);
    hole = (d86f_handler[drive].disk_flags(drive) & 6) >> 1;

    if (!rm && ssd)	/* Special case - extra bit cells size specifies entire array size. */
	array_size = 0;
    else switch (hole) {
	case 0:
	case 1:
	default:
		array_size = 12500;
		switch (rm) {
			case 1:
				array_size = ssd ? 12376 : 12625;
				break;

			case 2:
				array_size = ssd ? 12315 : 12687;
				break;

			case 3:
				array_size = ssd ? 12254 : 12750;
				break;

			default:
				break;
		}
		break;

	case 2:
		array_size = 25000;
		switch (rm) {
			case 1:
				array_size = ssd ? 24752 : 25250;
				break;

			case 2:
				array_size = ssd ? 24630 : 25375;
				break;

			case 3:
				array_size = ssd ? 24509 : 25500;
				break;

			default:
				break;
		}
		break;

	case 3:
		array_size = 50000;
		switch (rm) {
			case 1:
				array_size = ssd ? 49504 : 50500;
				break;

			case 2:
				array_size = ssd ? 49261 : 50750;
				break;

			case 3:
				array_size = ssd ? 49019 : 51000;
				break;

			default:
				break;
		}
		break;
    }

    array_size <<= 4;
    array_size += d86f_handler[drive].extra_bit_cells(drive, side);

    if (array_size & 15)
	array_size = (array_size >> 4) + 1;
    else
	array_size = (array_size >> 4);

    if (!words)
	array_size <<= 1;

    return array_size;
}


int
d86f_valid_bit_rate(int drive)
{
    int hole, rate;

    rate = fdc_get_bit_rate(d86f_fdc);
    hole = (d86f_handler[drive].disk_flags(drive) & 6) >> 1;
    switch (hole) {
	case 0:	/* DD */
		if (!rate && (fdd_get_flags(drive) & 0x10))  return 1;
		if ((rate < 1) || (rate > 2))  return 0;
		return 1;

	case 1:	/* HD */
		if (rate != 0)  return 0;
		return 1;

	case 2:	/* ED */
		if (rate != 3)  return 0;
		return 1;

	case 3:	/* ED with 2000 kbps support */
		if (rate < 3)  return 0;
		return 1;

	default:
		break;
    }

    return 0;
}


int
d86f_hole(int drive)
{
    if (((d86f_handler[drive].disk_flags(drive) >> 1) & 3) == 3)
	return 2;

    return (d86f_handler[drive].disk_flags(drive) >> 1) & 3;
}


uint8_t
d86f_get_encoding(int drive)
{
    return (d86f_track_flags(drive) & 0x18) >> 3;
}


uint64_t
d86f_byteperiod(int drive)
{
    double dusec = (double) TIMER_USEC;
    double p = 2.0;

    switch (d86f_track_flags(drive) & 0x0f) {
	case 0x02:	/* 125 kbps, FM */
		p = 4.0;
		break;
	case 0x01:	/* 150 kbps, FM */
		p = 20.0 / 6.0;
		break;
	case 0x0a:	/* 250 kbps, MFM */
	case 0x00:	/* 250 kbps, FM */
	default:
		p = 2.0;
		break;
	case 0x09:	/* 300 kbps, MFM */
		p = 10.0 / 6.0;
		break;
	case 0x08:	/* 500 kbps, MFM */
		p = 1.0;
		break;
	case 0x0b:	/* 1000 kbps, MFM */
		p = 0.5;
		break;
	case 0x0d:	/* 2000 kbps, MFM */
		p = 0.25;
		break;
    }

    return (uint64_t) (p * dusec);
}


int
d86f_is_mfm(int drive)
{
    return (d86f_track_flags(drive) & 8) ? 1 : 0;
}


uint32_t
d86f_get_data_len(int drive)
{
    d86f_t *dev = d86f[drive];

    if (dev->req_sector.id.n) {
	if (dev->req_sector.id.n == 8)  return 32768;
	return (128 << ((uint32_t) dev->req_sector.id.n));
    } else {
	if (fdc_get_dtl(d86f_fdc) < 128)
		return fdc_get_dtl(d86f_fdc);
	  else
		return (128 << ((uint32_t) dev->req_sector.id.n));
    }
}


uint32_t
d86f_has_extra_bit_cells(int drive)
{
    return (d86f_handler[drive].disk_flags(drive) >> 7) & 1;
}


uint32_t
d86f_header_size(int drive)
{
    return 8;
}


static uint16_t
d86f_encode_get_data(uint8_t dat)
{
    uint16_t temp;
    temp = 0;

    if (dat & 0x01) temp |= 1;
    if (dat & 0x02) temp |= 4;
    if (dat & 0x04) temp |= 16;
    if (dat & 0x08) temp |= 64;
    if (dat & 0x10) temp |= 256;
    if (dat & 0x20) temp |= 1024;
    if (dat & 0x40) temp |= 4096;
    if (dat & 0x80) temp |= 16384;

    return temp;
}


static uint16_t
d86f_encode_get_clock(uint8_t dat)
{
    uint16_t temp;
    temp = 0;

    if (dat & 0x01) temp |= 2;
    if (dat & 0x02) temp |= 8;
    if (dat & 0x40) temp |= 32;
    if (dat & 0x08) temp |= 128;
    if (dat & 0x10) temp |= 512;
    if (dat & 0x20) temp |= 2048;
    if (dat & 0x40) temp |= 8192;
    if (dat & 0x80) temp |= 32768;

    return temp;
}


int
d86f_format_conditions(int drive)
{
    return d86f_valid_bit_rate(drive);
}


int
d86f_wrong_densel(int drive)
{
    int is_3mode = 0;

    if ((fdd_get_flags(drive) & 7) == 3)
	is_3mode = 1;

    switch (d86f_hole(drive)) {
	case 0:
	default:
		if (fdd_is_dd(drive))
			return 0;
		if (fdd_get_densel(drive))
			return 1;
		  else
			return 0;
		break;

	case 1:
		if (fdd_is_dd(drive))
			return 1;
		if (fdd_get_densel(drive))
			return 0;
		else {
			if (is_3mode)
				return 0;
			  else
				return 1;
		}
		break;

	case 2:
		if (fdd_is_dd(drive) || !fdd_is_ed(drive))
			return 1;
		if (fdd_get_densel(drive))
			return 0;
		  else
			return 1;
		break;
    }
}


int
d86f_can_format(int drive)
{
    int temp;

    temp = !writeprot[drive];
    temp = temp && !fdc_get_swwp(d86f_fdc);
    temp = temp && fdd_can_read_medium(real_drive(d86f_fdc, drive));
    temp = temp && d86f_handler[drive].format_conditions(drive);		/* Allows proxied formats to add their own extra conditions to formatting. */
    temp = temp && !d86f_wrong_densel(drive);

    return temp;
}


uint16_t
d86f_encode_byte(int drive, int sync, decoded_t b, decoded_t prev_b)
{
    uint8_t encoding = d86f_get_encoding(drive);
    uint8_t bits89AB = prev_b.nibbles.nibble0;
    uint8_t bits7654 = b.nibbles.nibble1;
    uint8_t bits3210 = b.nibbles.nibble0;
    uint16_t encoded_7654, encoded_3210, result;

    if (encoding > 1) return 0xff;

    if (sync) {
	result = d86f_encode_get_data(b.byte);
	if (encoding) {
		switch(b.byte) {
			case 0xa1:
				return result | d86f_encode_get_clock(0x0a);

			case 0xc2:
				return result | d86f_encode_get_clock(0x14);

			case 0xf8:
				return result | d86f_encode_get_clock(0x03);

			case 0xfb:
			case 0xfe:
				return result | d86f_encode_get_clock(0x00);

			case 0xfc:
				return result | d86f_encode_get_clock(0x01);
		}
	} else {
		switch(b.byte) {
			case 0xf8:
			case 0xfb:
			case 0xfe:
				return result | d86f_encode_get_clock(0xc7);

			case 0xfc:
				return result | d86f_encode_get_clock(0xd7);
		}
	}
    }

    bits3210 += ((bits7654 & 3) << 4);
    bits7654 += ((bits89AB & 3) << 4);
    encoded_3210 = (encoding == 1) ? encoded_mfm[bits3210] : encoded_fm[bits3210];
    encoded_7654 = (encoding == 1) ? encoded_mfm[bits7654] : encoded_fm[bits7654];
    result = (encoded_7654 << 8) | encoded_3210;

    return result;
}


static int
d86f_get_bitcell_period(int drive)
{
    double rate = 0.0;
    int mfm = 0;
    int tflags = 0;
    double rpm = 0;
    double size = 8000.0;

    tflags = d86f_track_flags(drive);

    mfm = (tflags & 8) ? 1 : 0;
    rpm = ((tflags & 0xE0) == 0x20) ? 360.0 : 300.0;

    switch (tflags & 7) {
	case 0:
		rate = 500.0;
		break;

	case 1:
		rate = 300.0;
		break;

	case 2:
		rate = 250.0;
		break;

	case 3:
		rate = 1000.0;
		break;

	case 5:
		rate = 2000.0;
		break;
    }

    if (! mfm)
	rate /= 2.0;
    size = (size * 250.0) / rate;
    size = (size * 300.0) / rpm;
    size = (size * fdd_getrpm(real_drive(d86f_fdc, drive))) / 300.0;

    return (int)size;
}


int
d86f_can_read_address(int drive)
{
    int temp;

    temp = (fdc_get_bitcell_period(d86f_fdc) == d86f_get_bitcell_period(drive));
    temp = temp && fdd_can_read_medium(real_drive(d86f_fdc, drive));
    temp = temp && (fdc_is_mfm(d86f_fdc) == d86f_is_mfm(drive));
    temp = temp && (d86f_get_encoding(drive) <= 1);

    return temp;
}


void
d86f_get_bit(int drive, int side)
{
    d86f_t *dev = d86f[drive];
    uint32_t track_word;
    uint32_t track_bit;
    uint16_t encoded_data;
    uint16_t surface_data = 0;
    uint16_t current_bit;
    uint16_t surface_bit;

    track_word = dev->track_pos >> 4;

    /* We need to make sure we read the bits from MSB to LSB. */
    track_bit = 15 - (dev->track_pos & 15);

    if (d86f_reverse_bytes(drive)) {
	/* Image is in reverse endianness, read the data as is. */
	encoded_data = d86f_handler[drive].encoded_data(drive, side)[track_word];
    } else {
	/* We store the words as big endian, so we need to convert them to little endian when reading. */
	encoded_data = (d86f_handler[drive].encoded_data(drive, side)[track_word] & 0xFF) << 8;
	encoded_data |= (d86f_handler[drive].encoded_data(drive, side)[track_word] >> 8);
    }

    /* In some cases, misindentification occurs so we need to make sure the surface data array is not
       not NULL. */
    if (d86f_has_surface_desc(drive) && dev->track_surface_data[side]) {
	if (d86f_reverse_bytes(drive)) {
		surface_data = dev->track_surface_data[side][track_word] & 0xFF;
	} else {
		surface_data = (dev->track_surface_data[side][track_word] & 0xFF) << 8;
		surface_data |= (dev->track_surface_data[side][track_word] >> 8);
	}
    }

    current_bit = (encoded_data >> track_bit) & 1;
    dev->last_word[side] <<= 1;

    if (d86f_has_surface_desc(drive) && dev->track_surface_data[side]) {
	surface_bit = (surface_data >> track_bit) & 1;
	if (! surface_bit)
		dev->last_word[side] |= current_bit;
	else {
		/* Bit is either 0 or 1 and is set to fuzzy, we randomly generate it. */
		dev->last_word[side] |= (random_generate() & 1);
	}
    } else
	dev->last_word[side] |= current_bit;
}


void
d86f_put_bit(int drive, int side, int bit)
{
    d86f_t *dev = d86f[drive];
    uint32_t track_word;
    uint32_t track_bit;
    uint16_t encoded_data;
    uint16_t surface_data = 0;
    uint16_t current_bit;
    uint16_t surface_bit;

    if (fdc_get_diswr(d86f_fdc))
	return;

    track_word = dev->track_pos >> 4;

    /* We need to make sure we read the bits from MSB to LSB. */
    track_bit = 15 - (dev->track_pos & 15);

    if (d86f_reverse_bytes(drive)) {
	/* Image is in reverse endianness, read the data as is. */
	encoded_data = d86f_handler[drive].encoded_data(drive, side)[track_word];
    } else {
	/* We store the words as big endian, so we need to convert them to little endian when reading. */
	encoded_data = (d86f_handler[drive].encoded_data(drive, side)[track_word] & 0xFF) << 8;
	encoded_data |= (d86f_handler[drive].encoded_data(drive, side)[track_word] >> 8);
    }

    if (d86f_has_surface_desc(drive)) {
	if (d86f_reverse_bytes(drive)) {
		surface_data = dev->track_surface_data[side][track_word] & 0xFF;
	} else {
		surface_data = (dev->track_surface_data[side][track_word] & 0xFF) << 8;
		surface_data |= (dev->track_surface_data[side][track_word] >> 8);
	}
    }

    current_bit = (encoded_data >> track_bit) & 1;
    dev->last_word[side] <<= 1;

    if (d86f_has_surface_desc(drive)) {
	surface_bit = (surface_data >> track_bit) & 1;
	if (! surface_bit) {
		if (! current_bit) {
			/* Bit is 0 and is not set to fuzzy, we overwrite it as is. */
			dev->last_word[side] |= bit;
			current_bit = bit;
		} else {
			/* Bit is 1 and is not set to fuzzy, we overwrite it as is. */
			dev->last_word[side] |= bit;
			current_bit = bit;
		}
	} else {
		if (current_bit) {
			/* Bit is 1 and is set to fuzzy, we overwrite it with a non-fuzzy bit. */
			dev->last_word[side] |= bit;
			current_bit = bit;
			surface_bit = 0;
		}
	}

	surface_data &= ~(1 << track_bit);
	surface_data |= (surface_bit << track_bit);
	if (d86f_reverse_bytes(drive)) {
		dev->track_surface_data[side][track_word] = surface_data;
	} else {
		dev->track_surface_data[side][track_word] = (surface_data & 0xFF) << 8;
		dev->track_surface_data[side][track_word] |= (surface_data >> 8);
	}
    } else {
	dev->last_word[side] |= bit;
	current_bit = bit;
    }

    encoded_data &= ~(1 << track_bit);
    encoded_data |= (current_bit << track_bit);

    if (d86f_reverse_bytes(drive)) {
	d86f_handler[drive].encoded_data(drive, side)[track_word] = encoded_data;
    } else {
	d86f_handler[drive].encoded_data(drive, side)[track_word] = (encoded_data & 0xFF) << 8;
	d86f_handler[drive].encoded_data(drive, side)[track_word] |= (encoded_data >> 8);
    }
}


static uint8_t
decodefm(int drive, uint16_t dat)
{
    uint8_t temp = 0;

    /*
     * We write the encoded bytes in big endian, so we
     * process the two 8-bit halves swapped here.
     */
    if (dat & 0x0001) temp |= 1;
    if (dat & 0x0004) temp |= 2;
    if (dat & 0x0010) temp |= 4;
    if (dat & 0x0040) temp |= 8;
    if (dat & 0x0100) temp |= 16;
    if (dat & 0x0400) temp |= 32;
    if (dat & 0x1000) temp |= 64;
    if (dat & 0x4000) temp |= 128;

    return temp;
}


void
fdd_calccrc(uint8_t byte, crc_t *crc_var)
{
    crc_var->word = (crc_var->word << 8) ^
			CRCTable[(crc_var->word >> 8)^byte];
}


static void
d86f_calccrc(d86f_t *dev, uint8_t byte)
{
    fdd_calccrc(byte, &(dev->calc_crc));
}


int
d86f_word_is_aligned(int drive, int side, uint32_t base_pos)
{
    d86f_t *dev = d86f[drive];
    uint32_t adjusted_track_pos = dev->track_pos;

    if (base_pos == 0xFFFFFFFF)
	return 0;

    /*
     * This is very important, it makes sure alignment is detected
     * correctly even across the index hole of a track whose length
     * is not divisible by 16.
     */
    if (adjusted_track_pos < base_pos)
	adjusted_track_pos += d86f_handler[drive].get_raw_size(drive, side);

    if ((adjusted_track_pos & 15) == (base_pos & 15))
	return 1;

    return 0;
}


/* State 1: Find sector ID */
void
d86f_find_address_mark_fm(int drive, int side, find_t *find, uint16_t req_am, uint16_t other_am, uint16_t wrong_am, uint16_t ignore_other_am)
{
    d86f_t *dev = d86f[drive];

    d86f_get_bit(drive, side);

    if (dev->last_word[side] == req_am) {
	dev->calc_crc.word = 0xFFFF;
	fdd_calccrc(decodefm(drive, dev->last_word[side]), &(dev->calc_crc));
	find->sync_marks = find->bits_obtained = find->bytes_obtained = 0;
	find->sync_pos = 0xFFFFFFFF;
	dev->preceding_bit[side] = dev->last_word[side] & 1;
	dev->state++;
	return;
    }

    if ((wrong_am) && (dev->last_word[side] == wrong_am)) {
	dev->data_find.sync_marks = dev->data_find.bits_obtained = dev->data_find.bytes_obtained = 0;
	dev->error_condition = 0;
	dev->state = STATE_IDLE;
	fdc_finishread(d86f_fdc);
	fdc_nodataam(d86f_fdc);
	return;
    }

    if ((ignore_other_am & 2) && (dev->last_word[side] == other_am)) {
	dev->calc_crc.word = 0xFFFF;
	fdd_calccrc(decodefm(drive, dev->last_word[side]), &(dev->calc_crc));
	find->sync_marks = find->bits_obtained = find->bytes_obtained = 0;
	find->sync_pos = 0xFFFFFFFF;
	if (ignore_other_am & 1) {
		/* Skip mode, let's go back to finding ID. */
		dev->state -= 2;
	} else {
		/* Not skip mode, process the sector anyway. */
		fdc_set_wrong_am(d86f_fdc);
		dev->preceding_bit[side] = dev->last_word[side] & 1;
		dev->state++;
	}
    }
}


/* When writing in FM mode, we find the beginning of the address mark by looking for 352 (22 * 16) set bits (gap fill = 0xFF, 0xFFFF FM-encoded). */
void
d86f_write_find_address_mark_fm(int drive, int side, find_t *find)
{
    d86f_t *dev = d86f[drive];

    d86f_get_bit(drive, side);

    if (dev->last_word[side] & 1) {
	find->sync_marks++;
	if (find->sync_marks == 352) {
		dev->calc_crc.word = 0xFFFF;
		dev->preceding_bit[side] = 1;
		find->sync_marks = 0;
		dev->state++;
		return;
	}
    }

    /* If we hadn't found enough set bits but have found a clear bit, null the counter of set bits. */
    if (!(dev->last_word[side] & 1)) {
	find->sync_marks = find->bits_obtained = find->bytes_obtained = 0;
	find->sync_pos = 0xFFFFFFFF;
    }
}


void
d86f_find_address_mark_mfm(int drive, int side, find_t *find, uint16_t req_am, uint16_t other_am, uint16_t wrong_am, uint16_t ignore_other_am)
{
    d86f_t *dev = d86f[drive];

    d86f_get_bit(drive, side);

    if (dev->last_word[side] == 0x4489) {
	find->sync_marks++;
	find->sync_pos = dev->track_pos;
	return;
    }

    if ((wrong_am) && (dev->last_word[side] == wrong_am) && (find->sync_marks >= 3)) {
	dev->data_find.sync_marks = dev->data_find.bits_obtained = dev->data_find.bytes_obtained = 0;
	dev->error_condition = 0;
	dev->state = STATE_IDLE;
	fdc_finishread(d86f_fdc);
	fdc_nodataam(d86f_fdc);
	return;
    }

    if ((dev->last_word[side] == req_am) && (find->sync_marks >= 3)) {
	if (d86f_word_is_aligned(drive, side, find->sync_pos)) {
		dev->calc_crc.word = 0xCDB4;
		fdd_calccrc(decodefm(drive, dev->last_word[side]), &(dev->calc_crc));
		find->sync_marks = find->bits_obtained = find->bytes_obtained = 0;
		find->sync_pos = 0xFFFFFFFF;
		dev->preceding_bit[side] = dev->last_word[side] & 1;
		dev->state++;
		return;
	}
    }

    if ((ignore_other_am & 2) && (dev->last_word[side] == other_am) && (find->sync_marks >= 3)) {
	if (d86f_word_is_aligned(drive, side, find->sync_pos)) {
		dev->calc_crc.word = 0xCDB4;
		fdd_calccrc(decodefm(drive, dev->last_word[side]), &(dev->calc_crc));
		find->sync_marks = find->bits_obtained = find->bytes_obtained = 0;
		find->sync_pos = 0xFFFFFFFF;
		if (ignore_other_am & 1) {
			/* Skip mode, let's go back to finding ID. */
			dev->state -= 2;
		} else {
			/* Not skip mode, process the sector anyway. */
			fdc_set_wrong_am(d86f_fdc);
			dev->preceding_bit[side] = dev->last_word[side] & 1;
			dev->state++;
		}
		return;
	}
    }

    if (dev->last_word[side] != 0x4489) {
	if (d86f_word_is_aligned(drive, side, find->sync_pos)) {
		find->sync_marks = find->bits_obtained = find->bytes_obtained = 0;
		find->sync_pos = 0xFFFFFFFF;
	}
    }
}


/* When writing in MFM mode, we find the beginning of the address mark by looking for 3 0xA1 sync bytes. */
void
d86f_write_find_address_mark_mfm(int drive, int side, find_t *find)
{
    d86f_t *dev = d86f[drive];

    d86f_get_bit(drive, side);

    if (dev->last_word[side] == 0x4489) {
	find->sync_marks++;
	find->sync_pos = dev->track_pos;
	if (find->sync_marks == 3) {
		dev->calc_crc.word = 0xCDB4;
		dev->preceding_bit[side] = 1;
		find->sync_marks = 0;
		dev->state++;
		return;
	}
    }

    /* If we hadn't found enough address mark sync marks, null the counter. */
    if (dev->last_word[side] != 0x4489) {
	if (d86f_word_is_aligned(drive, side, find->sync_pos)) {
		find->sync_marks = find->bits_obtained = find->bytes_obtained = 0;
		find->sync_pos = 0xFFFFFFFF;
	}
    }
}


/* State 2: Read sector ID and CRC*/
void
d86f_read_sector_id(int drive, int side, int match)
{
    d86f_t *dev = d86f[drive];

    if (dev->id_find.bits_obtained) {
	if (! (dev->id_find.bits_obtained & 15)) {
		/* We've got a byte. */
		if (dev->id_find.bytes_obtained < 4) {
			dev->last_sector.byte_array[dev->id_find.bytes_obtained] = decodefm(drive, dev->last_word[side]);
			fdd_calccrc(dev->last_sector.byte_array[dev->id_find.bytes_obtained], &(dev->calc_crc));
		} else if ((dev->id_find.bytes_obtained >= 4) && (dev->id_find.bytes_obtained < 6)) {
			dev->track_crc.bytes[(dev->id_find.bytes_obtained & 1) ^ 1] = decodefm(drive, dev->last_word[side]);
		}
		dev->id_find.bytes_obtained++;

		if (dev->id_find.bytes_obtained == 6) {
			/* We've got the ID. */
			if ((dev->calc_crc.word != dev->track_crc.word) && (dev->last_sector.dword == dev->req_sector.dword)) {
				dev->id_find.sync_marks = dev->id_find.bits_obtained = dev->id_find.bytes_obtained = 0;
				d86f_log("86F: ID CRC error: %04X != %04X (%08X)\n", dev->track_crc.word, dev->calc_crc.word, dev->last_sector.dword);
				if ((dev->state != STATE_02_READ_ID) && (dev->state != STATE_0A_READ_ID)) {
					dev->error_condition = 0;
					dev->state = STATE_IDLE;
					fdc_finishread(d86f_fdc);
					fdc_headercrcerror(d86f_fdc);
				} else if (dev->state == STATE_0A_READ_ID)
					dev->state--;
				else {
					dev->error_condition |= 1;	/* Mark that there was an ID CRC error. */
					dev->state++;
				}
			} else if ((dev->calc_crc.word == dev->track_crc.word) && (dev->state == STATE_0A_READ_ID)) {
				/* CRC is valid and this is a read sector ID command. */
				dev->id_find.sync_marks = dev->id_find.bits_obtained = dev->id_find.bytes_obtained = dev->error_condition = 0;
				fdc_sectorid(d86f_fdc, dev->last_sector.id.c, dev->last_sector.id.h, dev->last_sector.id.r, dev->last_sector.id.n, 0, 0);
				dev->state = STATE_IDLE;
			} else {
				/* CRC is valid. */
				dev->id_find.sync_marks = dev->id_find.bits_obtained = dev->id_find.bytes_obtained = 0;
				dev->id_found++;
				if ((dev->last_sector.dword == dev->req_sector.dword) || !match) {
					d86f_handler[drive].set_sector(drive, side, dev->last_sector.id.c, dev->last_sector.id.h, dev->last_sector.id.r, dev->last_sector.id.n);
					if (dev->state == STATE_02_READ_ID) {
						/* READ TRACK command, we need some special handling here. */
						/* Code corrected: Only the C, H, and N portions of the sector ID are compared, the R portion (the sector number) is ignored. */
						if ((dev->last_sector.id.c != fdc_get_read_track_sector(d86f_fdc).id.c) || (dev->last_sector.id.h != fdc_get_read_track_sector(d86f_fdc).id.h) || (dev->last_sector.id.n != fdc_get_read_track_sector(d86f_fdc).id.n)) {
							dev->error_condition |= 4;	/* Mark that the sector ID is not the one expected by the FDC. */
							/* Make sure we use the sector size from the FDC. */
							dev->last_sector.id.n = fdc_get_read_track_sector(d86f_fdc).id.n;
						}

						/* If the two ID's are identical, then we do not need to do anything regarding the sector size. */
					}
					dev->state++;
				} else {
					if (dev->last_sector.id.c != dev->req_sector.id.c) {
						if (dev->last_sector.id.c == 0xFF) {
							dev->error_condition |= 8;
						} else {
							dev->error_condition |= 0x10;
						}
					}

					dev->state--;
				}
			}
		}
	}
    }

    d86f_get_bit(drive, side);

    dev->id_find.bits_obtained++;
}


uint8_t
d86f_get_data(int drive, int base)
{
    d86f_t *dev = d86f[drive];
    int data;

    if (dev->data_find.bytes_obtained < (d86f_get_data_len(drive) + base)) {
	data = fdc_getdata(d86f_fdc, dev->data_find.bytes_obtained == (d86f_get_data_len(drive) + base - 1));
	if ((data & DMA_OVER) || (data == -1)) {
		dev->dma_over++;
		if (data == -1)
			data = 0;
		  else
			data &= 0xff;
	}
    } else {
	data = 0;
    }

    return data;
}


void
d86f_compare_byte(int drive, uint8_t received_byte, uint8_t disk_byte)
{
    d86f_t *dev = d86f[drive];

    switch(fdc_get_compare_condition(d86f_fdc)) {
	case 0:		/* SCAN EQUAL */
		if ((received_byte == disk_byte) || (received_byte == 0xFF))
			dev->satisfying_bytes++;
		break;

	case 1:		/* SCAN LOW OR EQUAL */
		if ((received_byte <= disk_byte) || (received_byte == 0xFF))
			dev->satisfying_bytes++;
		break;

	case 2:		/* SCAN HIGH OR EQUAL */
		if ((received_byte >= disk_byte) || (received_byte == 0xFF))
			dev->satisfying_bytes++;
		break;
    }
}


/* State 4: Read sector data and CRC*/
void
d86f_read_sector_data(int drive, int side)
{
    d86f_t *dev = d86f[drive];
    int data = 0;
    int recv_data = 0;
    int read_status = 0;
    uint32_t sector_len = dev->last_sector.id.n;
    uint32_t crc_pos = 0;
    sector_len = 1 << (7 + sector_len);
    crc_pos = sector_len + 2;

    if (dev->data_find.bits_obtained) {
	if (!(dev->data_find.bits_obtained & 15)) {
		/* We've got a byte. */
		d86f_log("86F: We've got a byte.\n");
		if (dev->data_find.bytes_obtained < sector_len) {
			if (d86f_handler[drive].read_data != NULL)
				data = d86f_handler[drive].read_data(drive, side, dev->data_find.bytes_obtained);
			else {
#ifdef HACK_FOR_DBASE_III
				if ((dev->last_sector.id.c == 39) && (dev->last_sector.id.h == 0) &&
				    (dev->last_sector.id.r == 5) && (dev->data_find.bytes_obtained >= 272))
					data = (random_generate() & 0xff);
				else
#endif
					data = decodefm(drive, dev->last_word[side]);
			}
			if (dev->state == STATE_11_SCAN_DATA) {
				/* Scan/compare command. */
				recv_data = d86f_get_data(drive, 0);
				d86f_compare_byte(drive, recv_data, data);
			} else {
				if (dev->data_find.bytes_obtained < d86f_get_data_len(drive)) {
					if (dev->state != STATE_16_VERIFY_DATA) {
						read_status = fdc_data(d86f_fdc, data);
						if (read_status == -1)
							dev->dma_over++;
					}
				}
			}
			fdd_calccrc(data, &(dev->calc_crc));
		} else if (dev->data_find.bytes_obtained < crc_pos)
			dev->track_crc.bytes[(dev->data_find.bytes_obtained - sector_len) ^ 1] = decodefm(drive, dev->last_word[side]);
		dev->data_find.bytes_obtained++;

		if (dev->data_find.bytes_obtained == (crc_pos + fdc_get_gap(d86f_fdc))) {
			/* We've got the data. */
			if ((dev->calc_crc.word != dev->track_crc.word) && (dev->state != STATE_02_READ_DATA)) {
				d86f_log("86F: Data CRC error: %04X != %04X (%08X)\n", dev->track_crc.word, dev->calc_crc.word, dev->last_sector.dword);
				dev->data_find.sync_marks = dev->data_find.bits_obtained = dev->data_find.bytes_obtained = 0;
				dev->error_condition = 0;
				dev->state = STATE_IDLE;
				fdc_finishread(d86f_fdc);
				fdc_datacrcerror(d86f_fdc);
			} else if ((dev->calc_crc.word != dev->track_crc.word) && (dev->state == STATE_02_READ_DATA)) {
				dev->data_find.sync_marks = dev->data_find.bits_obtained = dev->data_find.bytes_obtained = 0;
				dev->error_condition |= 2;	/* Mark that there was a data error. */
				dev->state = STATE_IDLE;
				fdc_track_finishread(d86f_fdc, dev->error_condition);
			} else {
				/* CRC is valid. */
				d86f_log("86F: Data CRC OK: %04X == %04X (%08X)\n", dev->track_crc.word, dev->calc_crc.word, dev->last_sector.dword);
				dev->data_find.sync_marks = dev->data_find.bits_obtained = dev->data_find.bytes_obtained = 0;
				dev->error_condition = 0;
				dev->state = STATE_IDLE;
				if (dev->state == STATE_11_SCAN_DATA)
					fdc_sector_finishcompare(d86f_fdc, (dev->satisfying_bytes == ((128 << ((uint32_t) dev->last_sector.id.n)) - 1)) ? 1 : 0);
				else
					fdc_sector_finishread(d86f_fdc);
			}
		}
	}
    }

    d86f_get_bit(drive, side);

    dev->data_find.bits_obtained++;
}


void
d86f_write_sector_data(int drive, int side, int mfm, uint16_t am)
{
    d86f_t *dev = d86f[drive];
    uint16_t bit_pos;
    uint16_t temp;
    uint32_t sector_len = dev->last_sector.id.n;
    uint32_t crc_pos = 0;
    sector_len = (1 << (7 + sector_len)) + 1;
    crc_pos = sector_len + 2;

    if (! (dev->data_find.bits_obtained & 15)) {
	if (dev->data_find.bytes_obtained < crc_pos) {
		if (! dev->data_find.bytes_obtained) {
			/* We're writing the address mark. */
			dev->current_byte[side] = am;
		} else if (dev->data_find.bytes_obtained < sector_len) {
			/* We're in the data field of the sector, read byte from FDC and request new byte. */
			dev->current_byte[side] = d86f_get_data(drive, 1);
			if (! fdc_get_diswr(d86f_fdc))
				d86f_handler[drive].write_data(drive, side, dev->data_find.bytes_obtained - 1, dev->current_byte[side]);
		} else {
			/* We're in the data field of the sector, use a CRC byte. */
			dev->current_byte[side] = dev->calc_crc.bytes[(dev->data_find.bytes_obtained & 1)];
		}

		dev->current_bit[side] = (15 - (dev->data_find.bits_obtained & 15)) >> 1;

		/* Write the bit. */
		temp = (dev->current_byte[side] >> dev->current_bit[side]) & 1;
		if ((!temp && !dev->preceding_bit[side]) || !mfm) {
			temp |= 2;
		}

		/* This is an even bit, so write the clock. */
		if (! dev->data_find.bytes_obtained) {
			/* Address mark, write bit directly. */
			d86f_put_bit(drive, side, am >> 15);
		} else {
			d86f_put_bit(drive, side, temp >> 1);
		}

		if (dev->data_find.bytes_obtained < sector_len) {
			/* This is a data byte, so CRC it. */
			if (! dev->data_find.bytes_obtained) {
				fdd_calccrc(decodefm(drive, am), &(dev->calc_crc));
			} else {
				fdd_calccrc(dev->current_byte[side], &(dev->calc_crc));
			}
		}
	}
    } else {
	if (dev->data_find.bytes_obtained < crc_pos) {
		/* Encode the bit. */
		bit_pos = 15 - (dev->data_find.bits_obtained & 15);
		dev->current_bit[side] = bit_pos >> 1;

		temp = (dev->current_byte[side] >> dev->current_bit[side]) & 1;
		if ((!temp && !dev->preceding_bit[side]) || !mfm) {
			temp |= 2;
		}

		if (! dev->data_find.bytes_obtained) {
			/* Address mark, write directly. */
			d86f_put_bit(drive, side, am >> bit_pos);
			if (! (bit_pos & 1)) {
				dev->preceding_bit[side] = am >> bit_pos;
			}
		} else {
			if (bit_pos & 1) {
				/* Clock bit */
				d86f_put_bit(drive, side, temp >> 1);
			} else {
				/* Data bit */
				d86f_put_bit(drive, side, temp & 1);
				dev->preceding_bit[side] = temp & 1;
			}
		}
	}

	if ((dev->data_find.bits_obtained & 15) == 15) {
		dev->data_find.bytes_obtained++;

		if (dev->data_find.bytes_obtained == (crc_pos + fdc_get_gap(d86f_fdc))) {
			/* We've written the data. */
			dev->data_find.sync_marks = dev->data_find.bits_obtained = dev->data_find.bytes_obtained = 0;
			dev->error_condition = 0;
			dev->state = STATE_IDLE;
			d86f_handler[drive].writeback(drive);
			fdc_sector_finishread(d86f_fdc);
			return;
		}
	}
    }

    dev->data_find.bits_obtained++;
}


void d86f_advance_bit(int drive, int side)
{
    d86f_t *dev = d86f[drive];

    dev->track_pos++;
    dev->track_pos %= d86f_handler[drive].get_raw_size(drive, side);

    if (dev->track_pos == d86f_handler[drive].index_hole_pos(drive, side)) {
	d86f_handler[drive].read_revolution(drive);

	if (dev->state != STATE_IDLE)
		dev->index_count++;
    }
}


void
d86f_advance_word(int drive, int side)
{
    d86f_t *dev = d86f[drive];

    dev->track_pos += 16;
    dev->track_pos %= d86f_handler[drive].get_raw_size(drive, side);

    if ((dev->track_pos == d86f_handler[drive].index_hole_pos(drive, side)) && (dev->state != STATE_IDLE))
	dev->index_count++;
}


void
d86f_spin_to_index(int drive, int side)
{
    d86f_t *dev = d86f[drive];

    d86f_get_bit(drive, side);
    d86f_get_bit(drive, side ^ 1);

    d86f_advance_bit(drive, side);

    if (dev->track_pos == d86f_handler[drive].index_hole_pos(drive, side)) {
	if ((dev->state == STATE_0D_SPIN_TO_INDEX) || (dev->state == STATE_0D_NOP_SPIN_TO_INDEX)) {
		/* When starting format, reset format state to the beginning. */
		dev->preceding_bit[side] = 1;
		dev->format_state = FMT_PRETRK_GAP0;
	}

	/* This is to make sure both READ TRACK and FORMAT TRACK command don't end prematurely. */
	dev->index_count = 0;
	dev->state++;
    }
}


void
d86f_write_direct_common(int drive, int side, uint16_t byte, uint8_t type, uint32_t pos)
{
    d86f_t *dev = d86f[drive];
    uint16_t encoded_byte = 0, mask_data, mask_surface, mask_hole, mask_fuzzy;
    decoded_t dbyte, dpbyte;

    if (fdc_get_diswr(d86f_fdc)) return;

    dbyte.byte = byte & 0xff;
    dpbyte.byte = dev->preceding_bit[side] & 0xff;

    if (type == 0) {
	/* Byte write. */
	encoded_byte = d86f_encode_byte(drive, 0, dbyte, dpbyte);
	if (! d86f_reverse_bytes(drive)) {
		mask_data = encoded_byte >> 8;
		encoded_byte &= 0xFF;
		encoded_byte <<= 8;
		encoded_byte |= mask_data;
	}
    } else {
	/* Word write. */
	encoded_byte = byte;
	if (d86f_reverse_bytes(drive)) {
		mask_data = encoded_byte >> 8;
		encoded_byte &= 0xFF;
		encoded_byte <<= 8;
		encoded_byte |= mask_data;
	}
    }

    dev->preceding_bit[side] = encoded_byte & 1;

    if (d86f_has_surface_desc(drive)) {
	mask_data = dev->track_encoded_data[side][pos] ^= 0xFFFF;
	mask_surface = dev->track_surface_data[side][pos];
	mask_hole = (mask_surface & mask_data) ^ 0xFFFF;	/* This will retain bits that are both fuzzy and 0, therefore physical holes. */
	encoded_byte &= mask_hole;				/* Filter out physical hole bits from the encoded data. */
	mask_data ^= 0xFFFF;					/* Invert back so bits 1 are 1 again. */
	mask_fuzzy = (mask_surface & mask_data) ^ 0xFFFF;	/* All fuzzy bits are 0. */
	dev->track_surface_data[side][pos] &= mask_fuzzy;	/* Remove fuzzy bits (but not hole bits) from the surface mask, making them regular again. */
    }

    dev->track_encoded_data[side][pos] = encoded_byte;
    dev->last_word[side] = encoded_byte;
}


void
d86f_write_direct(int drive, int side, uint16_t byte, uint8_t type)
{
    d86f_t *dev = d86f[drive];

    d86f_write_direct_common(drive, side, byte, type, dev->track_pos >> 4);
}


uint16_t
endian_swap(uint16_t word)
{
    uint16_t temp;

    temp = word & 0xff;
    temp <<= 8;
    temp |= (word >> 8);

    return temp;
}


void
d86f_format_finish(int drive, int side, int mfm, uint16_t sc, uint16_t gap_fill, int do_write)
{
    d86f_t *dev = d86f[drive];

    if (mfm && do_write) {
	if (do_write && (dev->track_pos == d86f_handler[drive].index_hole_pos(drive, side))) {
		d86f_write_direct_common(drive, side, gap_fill, 0, 0);
	}
    }

    dev->state = STATE_IDLE;

    if (do_write)
	d86f_handler[drive].writeback(drive);

    dev->error_condition = 0;
    dev->datac = 0;
    fdc_sector_finishread(d86f_fdc);
}


void
d86f_format_turbo_finish(int drive, int side, int do_write)
{
    d86f_t *dev = d86f[drive];

    dev->state = STATE_IDLE;

    if (do_write)
	d86f_handler[drive].writeback(drive);

    dev->error_condition = 0;
    dev->datac = 0;
    fdc_sector_finishread(d86f_fdc);
}


void
d86f_format_track(int drive, int side, int do_write)
{
    d86f_t *dev = d86f[drive];
    int data;
    uint16_t max_len;

    int mfm;
    uint16_t sc = 0;
    uint16_t dtl = 0;
    int gap_sizes[4] = { 0, 0, 0, 0 };
    int am_len = 0;
    int sync_len = 0;
    uint16_t iam_mfm[4] = { 0x2452, 0x2452, 0x2452, 0x5255 };
    uint16_t idam_mfm[4] = { 0x8944, 0x8944, 0x8944, 0x5455 };
    uint16_t dataam_mfm[4] = { 0x8944, 0x8944, 0x8944, 0x4555 };
    uint16_t iam_fm = 0xFAF7;
    uint16_t idam_fm = 0x7EF5;
    uint16_t dataam_fm = 0x6FF5;
    uint16_t gap_fill = 0x4E;

    mfm = d86f_is_mfm(drive);
    am_len = mfm ? 4 : 1;
    gap_sizes[0] = mfm ? 80 : 40;
    gap_sizes[1] = mfm ? 50 : 26;
    gap_sizes[2] = fdc_get_gap2(d86f_fdc, real_drive(d86f_fdc, drive));
    gap_sizes[3] = fdc_get_gap(d86f_fdc);
    sync_len = mfm ? 12 : 6;
    sc = fdc_get_format_sectors(d86f_fdc);
    dtl = 128 << fdc_get_format_n(d86f_fdc);
    gap_fill = mfm ? 0x4E : 0xFF;

    switch(dev->format_state) {
	case FMT_POSTTRK_GAP4:
		max_len = 60000;
		if (do_write)
			d86f_write_direct(drive, side, gap_fill, 0);
		break;

	case FMT_PRETRK_GAP0:
		max_len = gap_sizes[0];
		if (do_write)
			d86f_write_direct(drive, side, gap_fill, 0);
		break;

	case FMT_SECTOR_ID_SYNC:
		max_len = sync_len;
		if (dev->datac <= 3) {
		       	data = fdc_getdata(d86f_fdc, 0);
			if (data != -1)
				data &= 0xff;
       			if ((data == -1) && (dev->datac < 3))
				data = 0;
			dev->format_sector_id.byte_array[dev->datac] = data & 0xff;
       			if (dev->datac == 3)
				fdc_stop_id_request(d86f_fdc);
		}
		/*FALLTHROUGH*/

	case FMT_PRETRK_SYNC:
	case FMT_SECTOR_DATA_SYNC:
		max_len = sync_len;
		if (do_write)
			d86f_write_direct(drive, side, 0x00, 0);
		break;

	case FMT_PRETRK_IAM:
		max_len = am_len;
		if (do_write) {
			if (mfm)
				d86f_write_direct(drive, side, iam_mfm[dev->datac], 1);
			else
				d86f_write_direct(drive, side, iam_fm, 1);
		}
		break;

	case FMT_PRETRK_GAP1:
		max_len = gap_sizes[1];
		if (do_write)
			d86f_write_direct(drive, side, gap_fill, 0);
		break;

	case FMT_SECTOR_IDAM:
		max_len = am_len;
		if (mfm) {
			if (do_write)
				d86f_write_direct(drive, side, idam_mfm[dev->datac], 1);
			d86f_calccrc(dev, (dev->datac < 3) ? 0xA1 : 0xFE);
		} else {
			if (do_write)
				d86f_write_direct(drive, side, idam_fm, 1);
			d86f_calccrc(dev, 0xFE);
		}
		break;

	case FMT_SECTOR_ID:
		max_len = 4;
		if (do_write) {
			d86f_write_direct(drive, side, dev->format_sector_id.byte_array[dev->datac], 0);
			d86f_calccrc(dev, dev->format_sector_id.byte_array[dev->datac]);
		} else {
			if (dev->datac == 3) {
				d86f_handler[drive].set_sector(drive, side, dev->format_sector_id.id.c, dev->format_sector_id.id.h, dev->format_sector_id.id.r, dev->format_sector_id.id.n);
			}
		}
		break;

	case FMT_SECTOR_ID_CRC:
	case FMT_SECTOR_DATA_CRC:
		max_len = 2;
		if (do_write)
			d86f_write_direct(drive, side, dev->calc_crc.bytes[dev->datac ^ 1], 0);
		break;

	case FMT_SECTOR_GAP2:
		max_len = gap_sizes[2];
		if (do_write)
			d86f_write_direct(drive, side, gap_fill, 0);
		break;

	case FMT_SECTOR_DATAAM:
		max_len = am_len;
		if (mfm) {
			if (do_write)
				d86f_write_direct(drive, side, dataam_mfm[dev->datac], 1);
			d86f_calccrc(dev, (dev->datac < 3) ? 0xA1 : 0xFB);
		} else {
			if (do_write)
				d86f_write_direct(drive, side, dataam_fm, 1);
			d86f_calccrc(dev, 0xFB);
		}
		break;

	case FMT_SECTOR_DATA:
		max_len = dtl;
		if (do_write) {
			d86f_write_direct(drive, side, dev->fill, 0);
			d86f_handler[drive].write_data(drive, side, dev->datac, dev->fill);
		}
		d86f_calccrc(dev, dev->fill);
		break;

	case FMT_SECTOR_GAP3:
		max_len = gap_sizes[3];
		if (do_write)
			d86f_write_direct(drive, side, gap_fill, 0);
		break;

	default:
		max_len = 0;
		break;
    }

    dev->datac++;

    d86f_advance_word(drive, side);

    if ((dev->index_count) && ((dev->format_state < FMT_SECTOR_ID_SYNC) || (dev->format_state > FMT_SECTOR_GAP3))) {
	d86f_format_finish(drive, side, mfm, sc, gap_fill, do_write);
	return;
    }

    if (dev->datac >= max_len) {
	dev->datac = 0;
	dev->format_state++;

	switch (dev->format_state) {
		case FMT_SECTOR_ID_SYNC:
			fdc_request_next_sector_id(d86f_fdc);
			break;

		case FMT_SECTOR_IDAM:
		case FMT_SECTOR_DATAAM:
			dev->calc_crc.word = 0xffff;
			break;

		case FMT_POSTTRK_CHECK:
			if (dev->index_count) {
				d86f_format_finish(drive, side, mfm, sc, gap_fill, do_write);
				return;
			}
			dev->sector_count++;
			if (dev->sector_count < sc) {
				/* Sector within allotted amount, change state to SECTOR_ID_SYNC. */
				dev->format_state = FMT_SECTOR_ID_SYNC;
				fdc_request_next_sector_id(d86f_fdc);
				break;
			} else {
				dev->format_state = FMT_POSTTRK_GAP4;
				dev->sector_count = 0;
				break;
			}
			break;
	}
    }
}


void
d86f_format_track_normal(int drive, int side)
{
    d86f_t *dev = d86f[drive];

    d86f_format_track(drive, side, (dev->version == D86FVER));
}


void
d86f_format_track_nop(int drive, int side)
{
    d86f_format_track(drive, side, 0);
}


void
d86f_initialize_last_sector_id(int drive, int c, int h, int r, int n)
{
    d86f_t *dev = d86f[drive];

    dev->last_sector.id.c = c;
    dev->last_sector.id.h = h;
    dev->last_sector.id.r = r;
    dev->last_sector.id.n = n;
}


static uint8_t
d86f_sector_flags(int drive, int side, uint8_t c, uint8_t h, uint8_t r, uint8_t n)
{
    d86f_t *dev = d86f[drive];
    sector_t *s, *t;

    if (dev->last_side_sector[side]) {
	s = dev->last_side_sector[side];
	while (s) {
		if ((s->c == c) && (s->h == h) && (s->r == r) && (s->n == n))
			return s->flags;
		if (! s->prev)
			break;
		t = s->prev;
		s = t;
	}
    }

    return 0x00;
}


void
d86f_turbo_read(int drive, int side)
{
    d86f_t *dev = d86f[drive];
    uint8_t dat = 0;
    int recv_data = 0;
    int read_status = 0;
    uint8_t flags = d86f_sector_flags(drive, side, dev->req_sector.id.c, dev->req_sector.id.h, dev->req_sector.id.r, dev->req_sector.id.n);

    if (d86f_handler[drive].read_data != NULL)
	dat = d86f_handler[drive].read_data(drive, side, dev->turbo_pos);
    else
	dat = (random_generate() & 0xff);
    dev->turbo_pos++;

    if (dev->state == STATE_11_SCAN_DATA) {
	/* Scan/compare command. */
	recv_data = d86f_get_data(drive, 0);
	d86f_compare_byte(drive, recv_data, dat);
    } else {
	if (dev->data_find.bytes_obtained < (128UL << dev->last_sector.id.n)) {
		if (dev->state != STATE_16_VERIFY_DATA) {
			read_status = fdc_data(d86f_fdc, dat);
			if (read_status == -1)
				dev->dma_over++;
		}
	}
    }

    if (dev->turbo_pos >= (128 << dev->last_sector.id.n)) {
	dev->data_find.sync_marks = dev->data_find.bits_obtained = dev->data_find.bytes_obtained = 0;
	if ((flags & SECTOR_CRC_ERROR) && (dev->state != STATE_02_READ_DATA)) {
#ifdef ENABLE_D86F_LOG
		d86f_log("86F: Data CRC error in turbo mode\n");
#endif
		dev->error_condition = 0;
		dev->state = STATE_IDLE;
		fdc_finishread(d86f_fdc);
		fdc_datacrcerror(d86f_fdc);
	} else if ((dev->calc_crc.word != dev->track_crc.word) && (dev->state == STATE_02_READ_DATA)) {
		dev->error_condition |= 2;	/* Mark that there was a data error. */
		dev->state = STATE_IDLE;
		fdc_track_finishread(d86f_fdc, dev->error_condition);
	} else {
		/* CRC is valid. */
#ifdef ENABLE_D86F_LOG
		d86f_log("86F: Data CRC OK error in turbo mode\n");
#endif
		dev->error_condition = 0;
		dev->state = STATE_IDLE;
		if (dev->state == STATE_11_SCAN_DATA)
			fdc_sector_finishcompare(d86f_fdc, (dev->satisfying_bytes == ((128 << ((uint32_t) dev->last_sector.id.n)) - 1)) ? 1 : 0);
		else
			fdc_sector_finishread(d86f_fdc);
	}
    }
}


void
d86f_turbo_write(int drive, int side)
{
    d86f_t *dev = d86f[drive];
    uint8_t dat = 0;

    dat = d86f_get_data(drive, 1);
    d86f_handler[drive].write_data(drive, side, dev->turbo_pos, dat);

    dev->turbo_pos++;

    if (dev->turbo_pos >= (128 << dev->last_sector.id.n)) {
	/* We've written the data. */
	dev->data_find.sync_marks = dev->data_find.bits_obtained = dev->data_find.bytes_obtained = 0;
	dev->error_condition = 0;
	dev->state = STATE_IDLE;
	d86f_handler[drive].writeback(drive);
	fdc_sector_finishread(d86f_fdc);
    }
}


void
d86f_turbo_format(int drive, int side, int nop)
{
    d86f_t *dev = d86f[drive];
    int dat;
    uint16_t sc;
    uint16_t dtl;
    int i;

    sc = fdc_get_format_sectors(d86f_fdc);
    dtl = 128 << fdc_get_format_n(d86f_fdc);

    if (dev->datac <= 3) {
	dat = fdc_getdata(d86f_fdc, 0);
	if (dat != -1)
		dat &= 0xff;
	if ((dat == -1) && (dev->datac < 3))
		dat = 0;
	dev->format_sector_id.byte_array[dev->datac] = dat & 0xff;
	if (dev->datac == 3) {
		fdc_stop_id_request(d86f_fdc);
		d86f_handler[drive].set_sector(drive, side, dev->format_sector_id.id.c, dev->format_sector_id.id.h, dev->format_sector_id.id.r, dev->format_sector_id.id.n);
	}
    } else if (dev->datac == 4) {
	if (! nop) {
		for (i = 0; i < dtl; i++)
			d86f_handler[drive].write_data(drive, side, i, dev->fill);
	}

	dev->sector_count++;
    }

    dev->datac++;

    if (dev->datac == 6) {
	dev->datac = 0;

	if (dev->sector_count < sc) {
		/* Sector within allotted amount. */
		fdc_request_next_sector_id(d86f_fdc);
	} else {
		dev->state = STATE_IDLE;
		d86f_format_turbo_finish(drive, side, nop);
	}
    }
}


int
d86f_sector_is_present(int drive, int side, uint8_t c, uint8_t h, uint8_t r, uint8_t n)
{
    d86f_t *dev = d86f[drive];
    sector_t *s, *t;

    if (dev->last_side_sector[side]) {
	s = dev->last_side_sector[side];
	while (s) {
		if ((s->c == c) && (s->h == h) && (s->r == r) && (s->n == n))
			return 1;
		if (! s->prev)
			break;
		t = s->prev;
		s = t;
	}
    }

    return 0;
}


void
d86f_turbo_poll(int drive, int side)
{
    d86f_t *dev = d86f[drive];

    if ((dev->state != STATE_IDLE) && (dev->state != STATE_SECTOR_NOT_FOUND) && ((dev->state & 0xF8) != 0xE8)) {
	if (! d86f_can_read_address(drive)) {
		dev->id_find.sync_marks = dev->id_find.bits_obtained = dev->id_find.bytes_obtained = dev->error_condition = 0;
		fdc_noidam(d86f_fdc);
		dev->state = STATE_IDLE;
		return;
	}
    }

    switch(dev->state) {
	case STATE_0D_SPIN_TO_INDEX:
	case STATE_0D_NOP_SPIN_TO_INDEX:
		dev->sector_count = 0;
		dev->datac = 5;
		/*FALLTHROUGH*/

	case STATE_02_SPIN_TO_INDEX:
		dev->state++;
		return;

	case STATE_02_FIND_ID:
		if (! d86f_sector_is_present(drive, side, fdc_get_read_track_sector(d86f_fdc).id.c, fdc_get_read_track_sector(d86f_fdc).id.h,
					     fdc_get_read_track_sector(d86f_fdc).id.r, fdc_get_read_track_sector(d86f_fdc).id.n)) {
			dev->id_find.sync_marks = dev->id_find.bits_obtained = dev->id_find.bytes_obtained = dev->error_condition = 0;
			fdc_nosector(d86f_fdc);
			dev->state = STATE_IDLE;
			return;
		}
		dev->last_sector.id.c = fdc_get_read_track_sector(d86f_fdc).id.c;
		dev->last_sector.id.h = fdc_get_read_track_sector(d86f_fdc).id.h;
		dev->last_sector.id.r = fdc_get_read_track_sector(d86f_fdc).id.r;
		dev->last_sector.id.n = fdc_get_read_track_sector(d86f_fdc).id.n;
		d86f_handler[drive].set_sector(drive, side, dev->last_sector.id.c, dev->last_sector.id.h, dev->last_sector.id.r, dev->last_sector.id.n);
		dev->turbo_pos = 0;
		dev->state++;
		return;

	case STATE_05_FIND_ID:
	case STATE_09_FIND_ID:
	case STATE_06_FIND_ID:
	case STATE_0C_FIND_ID:
	case STATE_11_FIND_ID:
	case STATE_16_FIND_ID:
		if (! d86f_sector_is_present(drive, side, dev->req_sector.id.c, dev->req_sector.id.h, dev->req_sector.id.r, dev->req_sector.id.n)) {
			dev->id_find.sync_marks = dev->id_find.bits_obtained = dev->id_find.bytes_obtained = dev->error_condition = 0;
			fdc_nosector(d86f_fdc);
			dev->state = STATE_IDLE;
			return;
		} else if (d86f_sector_flags(drive, side, dev->req_sector.id.c, dev->req_sector.id.h, dev->req_sector.id.r, dev->req_sector.id.n) & SECTOR_NO_ID) {
			dev->id_find.sync_marks = dev->id_find.bits_obtained = dev->id_find.bytes_obtained = dev->error_condition = 0;
			fdc_noidam(d86f_fdc);
			dev->state = STATE_IDLE;
			return;
		}
		dev->last_sector.id.c = dev->req_sector.id.c;
		dev->last_sector.id.h = dev->req_sector.id.h;
		dev->last_sector.id.r = dev->req_sector.id.r;
		dev->last_sector.id.n = dev->req_sector.id.n;
		d86f_handler[drive].set_sector(drive, side, dev->last_sector.id.c, dev->last_sector.id.h, dev->last_sector.id.r, dev->last_sector.id.n);
		/*FALLTHROUGH*/

	case STATE_0A_FIND_ID:
		dev->turbo_pos = 0;
		dev->state++;
		return;

	case STATE_0A_READ_ID:
		dev->id_find.sync_marks = dev->id_find.bits_obtained = dev->id_find.bytes_obtained = dev->error_condition = 0;
		fdc_sectorid(d86f_fdc, dev->last_sector.id.c, dev->last_sector.id.h, dev->last_sector.id.r, dev->last_sector.id.n, 0, 0);
		dev->state = STATE_IDLE;
		break;

	case STATE_02_READ_ID:
	case STATE_05_READ_ID:
	case STATE_09_READ_ID:
	case STATE_06_READ_ID:
	case STATE_0C_READ_ID:
	case STATE_11_READ_ID:
	case STATE_16_READ_ID:
		dev->state++;
		break;

	case STATE_02_FIND_DATA:
	case STATE_06_FIND_DATA:
	case STATE_11_FIND_DATA:
	case STATE_16_FIND_DATA:
	case STATE_05_FIND_DATA:
	case STATE_09_FIND_DATA:
	case STATE_0C_FIND_DATA:
		dev->state++;
		break;

	case STATE_02_READ_DATA:
	case STATE_06_READ_DATA:
	case STATE_0C_READ_DATA:
	case STATE_11_SCAN_DATA:
	case STATE_16_VERIFY_DATA:
		d86f_turbo_read(drive, side);
		break;

	case STATE_05_WRITE_DATA:
	case STATE_09_WRITE_DATA:
		d86f_turbo_write(drive, side);
		break;

	case STATE_0D_FORMAT_TRACK:
		d86f_turbo_format(drive, side, 0);
		return;

	case STATE_0D_NOP_FORMAT_TRACK:
		d86f_turbo_format(drive, side, 1);
		return;

	case STATE_IDLE:
	case STATE_SECTOR_NOT_FOUND:
	default:
		break;
    }
}


void
d86f_poll(int drive)
{
    d86f_t *dev = d86f[drive];
    int mfm, side;

    side = fdd_get_head(drive);
    if (! fdd_is_double_sided(drive))
	side = 0;

    mfm = fdc_is_mfm(d86f_fdc);

    if ((dev->state & 0xF8) == 0xE8) {
	if (! d86f_can_format(drive))
		dev->state = STATE_SECTOR_NOT_FOUND;
    }

    if (fdd_get_turbo(drive) && (dev->version == 0x0063)) {
	d86f_turbo_poll(drive, side);
	return;
    }

    if ((dev->state != STATE_IDLE) && (dev->state != STATE_SECTOR_NOT_FOUND) && ((dev->state & 0xF8) != 0xE8)) {
	if (! d86f_can_read_address(drive))
		dev->state = STATE_SECTOR_NOT_FOUND;
    }

    if ((dev->state != STATE_02_SPIN_TO_INDEX) && (dev->state != STATE_0D_SPIN_TO_INDEX))
	d86f_get_bit(drive, side ^ 1);

    switch(dev->state) {
	case STATE_02_SPIN_TO_INDEX:
	case STATE_0D_SPIN_TO_INDEX:
	case STATE_0D_NOP_SPIN_TO_INDEX:
		d86f_spin_to_index(drive, side);
		return;

	case STATE_02_FIND_ID:
	case STATE_05_FIND_ID:
	case STATE_09_FIND_ID:
	case STATE_06_FIND_ID:
	case STATE_0A_FIND_ID:
	case STATE_0C_FIND_ID:
	case STATE_11_FIND_ID:
	case STATE_16_FIND_ID:
		if (mfm)
			d86f_find_address_mark_mfm(drive, side, &(dev->id_find), 0x5554, 0, 0, 0);
		else
			d86f_find_address_mark_fm(drive, side, &(dev->id_find), 0xF57E, 0, 0, 0);
		break;

	case STATE_0A_READ_ID:
	case STATE_02_READ_ID:
		d86f_read_sector_id(drive, side, 0);
		break;

	case STATE_05_READ_ID:
	case STATE_09_READ_ID:
	case STATE_06_READ_ID:
	case STATE_0C_READ_ID:
	case STATE_11_READ_ID:
	case STATE_16_READ_ID:
		d86f_read_sector_id(drive, side, 1);
		break;

	case STATE_02_FIND_DATA:
		if (mfm)
			d86f_find_address_mark_mfm(drive, side, &(dev->data_find), 0x5545, 0x554A, 0x5554, 2);
		else
			d86f_find_address_mark_fm(drive, side, &(dev->data_find), 0xF56F, 0xF56A, 0xF57E, 2);
		break;

	case STATE_06_FIND_DATA:
	case STATE_11_FIND_DATA:
	case STATE_16_FIND_DATA:
		if (mfm)
			d86f_find_address_mark_mfm(drive, side, &(dev->data_find), 0x5545, 0x554A, 0x5554, fdc_is_sk(d86f_fdc) | 2);
		else
			d86f_find_address_mark_fm(drive, side, &(dev->data_find), 0xF56F, 0xF56A, 0xF57E, fdc_is_sk(d86f_fdc) | 2);
		break;

	case STATE_05_FIND_DATA:
	case STATE_09_FIND_DATA:
		if (mfm)
			d86f_write_find_address_mark_mfm(drive, side, &(dev->data_find));
		else
			d86f_write_find_address_mark_fm(drive, side, &(dev->data_find));
		break;

	case STATE_0C_FIND_DATA:
		if (mfm)
			d86f_find_address_mark_mfm(drive, side, &(dev->data_find), 0x554A, 0x5545, 0x5554, fdc_is_sk(d86f_fdc) | 2);
		else
			d86f_find_address_mark_fm(drive, side, &(dev->data_find), 0xF56A, 0xF56F, 0xF57E, fdc_is_sk(d86f_fdc) | 2);
		break;

	case STATE_02_READ_DATA:
	case STATE_06_READ_DATA:
	case STATE_0C_READ_DATA:
	case STATE_11_SCAN_DATA:
	case STATE_16_VERIFY_DATA:
		d86f_read_sector_data(drive, side);
		break;

	case STATE_05_WRITE_DATA:
		if (mfm)
			d86f_write_sector_data(drive, side, mfm, 0x5545);
		else
			d86f_write_sector_data(drive, side, mfm, 0xF56F);
		break;

	case STATE_09_WRITE_DATA:
		if (mfm)
			d86f_write_sector_data(drive, side, mfm, 0x554A);
		else
			d86f_write_sector_data(drive, side, mfm, 0xF56A);
		break;

	case STATE_0D_FORMAT_TRACK:
		if (! (dev->track_pos & 15))
			d86f_format_track_normal(drive, side);
		return;

	case STATE_0D_NOP_FORMAT_TRACK:
		if (! (dev->track_pos & 15))
			d86f_format_track_nop(drive, side);
		return;

	case STATE_IDLE:
	case STATE_SECTOR_NOT_FOUND:
	default:
		d86f_get_bit(drive, side);
		break;
    }

    d86f_advance_bit(drive, side);

    if (d86f_wrong_densel(drive) && (dev->state != STATE_IDLE)) {
	dev->state = STATE_IDLE;
	fdc_noidam(d86f_fdc);
	return;
    }

    if ((dev->index_count == 2) && (dev->state != STATE_IDLE)) {
	switch(dev->state) {
		case STATE_0A_FIND_ID:
		case STATE_SECTOR_NOT_FOUND:
			dev->state = STATE_IDLE;
			fdc_noidam(d86f_fdc);
			break;

		case STATE_02_FIND_DATA:
		case STATE_06_FIND_DATA:
		case STATE_11_FIND_DATA:
		case STATE_16_FIND_DATA:
		case STATE_05_FIND_DATA:
		case STATE_09_FIND_DATA:
		case STATE_0C_FIND_DATA:
			dev->state = STATE_IDLE;
			fdc_nodataam(d86f_fdc);
			break;

		case STATE_02_SPIN_TO_INDEX:
		case STATE_02_READ_DATA:
		case STATE_05_WRITE_DATA:
		case STATE_06_READ_DATA:
		case STATE_09_WRITE_DATA:
		case STATE_0C_READ_DATA:
		case STATE_0D_SPIN_TO_INDEX:
		case STATE_0D_FORMAT_TRACK:
		case STATE_11_SCAN_DATA:
		case STATE_16_VERIFY_DATA:
			/* In these states, we should *NEVER* care about how many index pulses there have been. */
			break;

		default:
			dev->state = STATE_IDLE;
			if (dev->id_found) {
				if (dev->error_condition & 0x18) {
					if ((dev->error_condition & 0x18) == 0x08)
						fdc_badcylinder(d86f_fdc);
					if ((dev->error_condition & 0x10) == 0x10)
						fdc_wrongcylinder(d86f_fdc);
					else
						fdc_nosector(d86f_fdc);
				} else
					fdc_nosector(d86f_fdc);
			} else
				fdc_noidam(d86f_fdc);
			break;
	}
    }
}


void
d86f_reset_index_hole_pos(int drive, int side)
{
    d86f_t *dev = d86f[drive];

    dev->index_hole_pos[side] = 0;
}


uint16_t
d86f_prepare_pretrack(int drive, int side, int iso)
{
    d86f_t *dev = d86f[drive];
    uint16_t i, pos;
    int mfm;
    int real_gap0_len;
    int sync_len;
    int real_gap1_len;
    uint16_t gap_fill;
    uint32_t raw_size;
    uint16_t iam_fm = 0xFAF7;
    uint16_t iam_mfm = 0x5255;

    mfm = d86f_is_mfm(drive);
    real_gap0_len = mfm ? 80 : 40;
    sync_len = mfm ? 12 : 6;
    real_gap1_len = mfm ? 50 : 26;
    gap_fill = mfm ? 0x4E : 0xFF;
    raw_size = d86f_handler[drive].get_raw_size(drive, side);
    if (raw_size & 15)
	raw_size = (raw_size >> 4) + 1;
    else
	raw_size = (raw_size >> 4);

    dev->index_hole_pos[side] = 0;

    d86f_destroy_linked_lists(drive, side);

    for (i = 0; i < raw_size; i++)
	d86f_write_direct_common(drive, side, gap_fill, 0, i);

    pos = 0;

    if (! iso) {
	for (i = 0; i < real_gap0_len; i++) {
		d86f_write_direct_common(drive, side, gap_fill, 0, pos);
		pos = (pos + 1) % raw_size;
	}
	for (i = 0; i < sync_len; i++) {
		d86f_write_direct_common(drive, side, 0, 0, pos);
		pos = (pos + 1) % raw_size;
	}
	if (mfm) {
		for (i = 0; i < 3; i++) {
			d86f_write_direct_common(drive, side, 0x2452, 1, pos);
			pos = (pos + 1) % raw_size;
		}
	}

	d86f_write_direct_common(drive, side, mfm ? iam_mfm : iam_fm, 1, pos);
	pos = (pos + 1) % raw_size;
    }

    for (i = 0; i < real_gap1_len; i++) {
	d86f_write_direct_common(drive, side, gap_fill, 0, pos);
	pos = (pos + 1) % raw_size;
    }

    return pos;
}


uint16_t
d86f_prepare_sector(int drive, int side, int prev_pos, uint8_t *id_buf, uint8_t *data_buf, int data_len, int gap2, int gap3, int flags)
{
    d86f_t *dev = d86f[drive];
    uint16_t pos;
    int i;
    sector_t *s;

    int real_gap2_len = gap2;
    int real_gap3_len = gap3;
    int mfm;
    int sync_len;
    uint16_t gap_fill;
    uint32_t raw_size;
    uint16_t idam_fm = 0x7EF5;
    uint16_t dataam_fm = 0x6FF5;
    uint16_t datadam_fm = 0x6AF5;
    uint16_t idam_mfm = 0x5455;
    uint16_t dataam_mfm = 0x4555;
    uint16_t datadam_mfm = 0x4A55;

    if (fdd_get_turbo(drive) && (dev->version == 0x0063)) {
	s = (sector_t *) malloc(sizeof(sector_t));
	memset(s, 0, sizeof(sector_t));
	s->c = id_buf[0];
	s->h = id_buf[1];
	s->r = id_buf[2];
	s->n = id_buf[3];
	s->flags = flags;
	if (dev->last_side_sector[side])
		s->prev = dev->last_side_sector[side];
	dev->last_side_sector[side] = s;
    }

    mfm = d86f_is_mfm(drive);

    gap_fill = mfm ? 0x4E : 0xFF;
    raw_size = d86f_handler[drive].get_raw_size(drive, side);
    if (raw_size & 15)
	raw_size = (raw_size >> 4) + 1;
    else
	raw_size = (raw_size >> 4);

    pos = prev_pos;

    sync_len = mfm ? 12 : 6;

    if (!(flags & SECTOR_NO_ID)) {
	for (i = 0; i < sync_len; i++) {
		d86f_write_direct_common(drive, side, 0, 0, pos);
		pos = (pos + 1) % raw_size;
	}

	dev->calc_crc.word = 0xffff;
	if (mfm) {
		for (i = 0; i < 3; i++) {
			d86f_write_direct_common(drive, side, 0x8944, 1, pos);
			pos = (pos + 1) % raw_size;
			d86f_calccrc(dev, 0xA1);
		}
	}
	d86f_write_direct_common(drive, side, mfm ? idam_mfm : idam_fm, 1, pos);
	pos = (pos + 1) % raw_size;
	d86f_calccrc(dev, 0xFE);
	for (i = 0; i < 4; i++) {
		d86f_write_direct_common(drive, side, id_buf[i], 0, pos);
		pos = (pos + 1) % raw_size;
		d86f_calccrc(dev, id_buf[i]);
	}
	for (i = 1; i >= 0; i--) {
		d86f_write_direct_common(drive, side, dev->calc_crc.bytes[i], 0, pos);
		pos = (pos + 1) % raw_size;
	}
	for (i = 0; i < real_gap2_len; i++) {
		d86f_write_direct_common(drive, side, gap_fill, 0, pos);
		pos = (pos + 1) % raw_size;
	}
    }

    if (!(flags & SECTOR_NO_DATA)) {
	for (i = 0; i < sync_len; i++) {
		d86f_write_direct_common(drive, side, 0, 0, pos);
		pos = (pos + 1) % raw_size;
	}
	dev->calc_crc.word = 0xffff;
	if (mfm) {
		for (i = 0; i < 3; i++) {
			d86f_write_direct_common(drive, side, 0x8944, 1, pos);
			pos = (pos + 1) % raw_size;
			d86f_calccrc(dev, 0xA1);
		}
	}
	d86f_write_direct_common(drive, side, mfm ? ((flags & SECTOR_DELETED_DATA) ? datadam_mfm : dataam_mfm) : ((flags & SECTOR_DELETED_DATA) ? datadam_fm : dataam_fm), 1, pos);
	pos = (pos + 1) % raw_size;
	d86f_calccrc(dev, (flags & SECTOR_DELETED_DATA) ? 0xF8 : 0xFB);
	if (data_len > 0) {
		for (i = 0; i < data_len; i++) {
			d86f_write_direct_common(drive, side, data_buf[i], 0, pos);
			pos = (pos + 1) % raw_size;
			d86f_calccrc(dev, data_buf[i]);
		}
		if (!(flags & SECTOR_CRC_ERROR)) {
			for (i = 1; i >= 0; i--) {
				d86f_write_direct_common(drive, side, dev->calc_crc.bytes[i], 0, pos);
				pos = (pos + 1) % raw_size;
			}
		}
		for (i = 0; i < real_gap3_len; i++) {
			d86f_write_direct_common(drive, side, gap_fill, 0, pos);
			pos = (pos + 1) % raw_size;
		}
	}
    }

    return pos;
}


/*
 * Note on handling of tracks on thick track drives:
 *
 * - On seek, encoded data is constructed from both (track << 1) and
 *   ((track << 1) + 1);
 *
 * - Any bits that differ are treated as thus:
 *	- Both are regular but contents differ -> Output is fuzzy;
 *	- One is regular and one is fuzzy -> Output is fuzzy;
 *	- Both are fuzzy -> Output is fuzzy;
 *	- Both are physical holes -> Output is a physical hole;
 *	- One is regular and one is a physical hole -> Output is fuzzy,
 *	  the hole half is handled appropriately on writeback;
 *	- One is fuzzy and one is a physical hole -> Output is fuzzy,
 *	  the hole half is handled appropriately on writeback;
 * - On write back, apart from the above notes, the final two tracks
 *   are written;
 * - Destination ALWAYS has surface data even if the image does not.
 *
 * In case of a thin track drive, tracks are handled normally.
 */
void
d86f_construct_encoded_buffer(int drive, int side)
{
    d86f_t *dev = d86f[drive];
    uint32_t i = 0;

    /* *_fuzm are fuzzy bit masks, *_holm are hole masks, dst_neim are masks is mask for bits that are neither fuzzy nor holes in both,
       and src1_d and src2_d are filtered source data. */
    uint16_t src1_fuzm, src2_fuzm, dst_fuzm, src1_holm, src2_holm, dst_holm, dst_neim, src1_d, src2_d;
    uint32_t len;
    uint16_t *dst = dev->track_encoded_data[side];
    uint16_t *dst_s = dev->track_surface_data[side];
    uint16_t *src1 = dev->thin_track_encoded_data[0][side];
    uint16_t *src1_s = dev->thin_track_surface_data[0][side];
    uint16_t *src2 = dev->thin_track_encoded_data[1][side];
    uint16_t *src2_s = dev->thin_track_surface_data[1][side];
    len = d86f_get_array_size(drive, side, 1);

    for (i = 0; i < len; i++) {
	/* The two bits differ. */
	if (d86f_has_surface_desc(drive)) {
		/* Source image has surface description data, so we have some more handling to do. */
		src1_fuzm = src1[i] & src1_s[i];
		src2_fuzm = src2[i] & src2_s[i];
		dst_fuzm = src1_fuzm | src2_fuzm;	/* The bits that remain set are fuzzy in either one or
									   the other or both. */
		src1_holm = src1[i] | (src1_s[i] ^ 0xffff);
		src2_holm = src2[i] | (src2_s[i] ^ 0xffff);
		dst_holm = (src1_holm & src2_holm) ^ 0xffff;		/* The bits that remain set are holes in both. */
		dst_neim = (dst_fuzm | dst_holm) ^ 0xffff;		/* The bits that remain set are those that are neither
										   fuzzy nor are holes in both. */
		src1_d = src1[i] & dst_neim;
		src2_d = src2[i] & dst_neim;

		dst_s[i] = (dst_neim ^ 0xffff);		/* The set bits are those that are either fuzzy or are
										   holes in both. */
		dst[i] = (src1_d | src2_d);				/* Initial data is remaining data from Source 1 and
										   Source 2. */
		dst[i] |= dst_fuzm;					/* Add to it the fuzzy bytes (holes have surface bit set
										   but data bit clear). */
	} else {
		/* No surface data, the handling is much simpler - a simple OR. */
		dst[i] = src1[i] | src2[i];
		dst_s[i] = 0;
	}
    }
}


/* Decomposition is easier since we at most have to care about the holes. */
void
d86f_decompose_encoded_buffer(int drive, int side)
{
    d86f_t *dev = d86f[drive];
    uint32_t i = 0;
    uint16_t temp, temp2;
    uint32_t len;
    uint16_t *dst = dev->track_encoded_data[side];
    uint16_t *src1 = dev->thin_track_encoded_data[0][side];
    uint16_t *src1_s = dev->thin_track_surface_data[0][side];
    uint16_t *src2 = dev->thin_track_encoded_data[1][side];
    uint16_t *src2_s = dev->thin_track_surface_data[1][side];
    dst = d86f_handler[drive].encoded_data(drive, side);
    len = d86f_get_array_size(drive, side, 1);

    for (i = 0; i < len; i++) {
	if (d86f_has_surface_desc(drive)) {
		/* Source image has surface description data, so we have some more handling to do.
		   We need hole masks for both buffers. Holes have data bit clear and surface bit set. */
		temp = src1[i] & (src1_s[i] ^ 0xffff);
		temp2 = src2[i] & (src2_s[i] ^ 0xffff);
		src1[i] = dst[i] & temp;
		src1_s[i] = temp ^ 0xffff;
		src2[i] = dst[i] & temp2;
		src2_s[i] = temp2 ^ 0xffff;
	} else {
		src1[i] = src2[i] = dst[i];
	}
    }
}


int
d86f_track_header_size(int drive)
{
    int temp = 6;

    if (d86f_has_extra_bit_cells(drive))
	temp += 4;

    return temp;
}


void
d86f_read_track(int drive, int track, int thin_track, int side, uint16_t *da, uint16_t *sa)
{
    d86f_t *dev = d86f[drive];
    int logical_track = 0;
    int array_size = 0;

    if (d86f_get_sides(drive) == 2)
	logical_track = ((track + thin_track) << 1) + side;
	else
	logical_track = track + thin_track;

    if (dev->track_offset[logical_track]) {
	if (! thin_track) {
		fseek(dev->f, dev->track_offset[logical_track], SEEK_SET);
		fread(&(dev->side_flags[side]), 2, 1, dev->f);
		if (d86f_has_extra_bit_cells(drive)) {
			fread(&(dev->extra_bit_cells[side]), 4, 1, dev->f);
			/* If RPM shift is 0% and direction is 1, do not adjust extra bit cells,
			   as that is the whole track length. */
			if (d86f_get_rpm_mode(drive) || !d86f_get_speed_shift_dir(drive)) {
				if (dev->extra_bit_cells[side] < -32768)
					dev->extra_bit_cells[side] = -32768;
				if (dev->extra_bit_cells[side] > 32768)
					dev->extra_bit_cells[side] = 32768;
			}
		} else
			dev->extra_bit_cells[side] = 0;
		fread(&(dev->index_hole_pos[side]), 4, 1, dev->f);
	} else
		fseek(dev->f, dev->track_offset[logical_track] + d86f_track_header_size(drive), SEEK_SET);
	array_size = d86f_get_array_size(drive, side, 0);
	if (d86f_has_surface_desc(drive))
		fread(sa, 1, array_size, dev->f);
	fread(da, 1, array_size, dev->f);
    } else {
	if (! thin_track) {
		switch((dev->disk_flags >> 1) & 3) {
			case 0:
			default:
				dev->side_flags[side] = 0x0A;
				break;

			case 1:
				dev->side_flags[side] = 0x00;
				break;

			case 2:
			case 3:
				dev->side_flags[side] = 0x03;
				break;
		}
		dev->extra_bit_cells[side] = 0;
	}
    }
}


void
d86f_zero_track(int drive)
{
    d86f_t *dev = d86f[drive];
    int sides, side;
    sides = d86f_get_sides(drive);

    for (side = 0; side < sides; side++) {
	if (d86f_has_surface_desc(drive))
		memset(dev->track_surface_data[side], 0, 106096);
	memset(dev->track_encoded_data[side], 0, 106096);
    }
}


void
d86f_seek(int drive, int track)
{
    d86f_t *dev = d86f[drive];
    int sides;
    int side, thin_track;
    sides = d86f_get_sides(drive);

    /* If the drive has thick tracks, shift the track number by 1. */
    if (! fdd_doublestep_40(drive)) {
	track <<= 1;

	for (thin_track = 0; thin_track < sides; thin_track++) {
		for (side = 0; side < sides; side++) {
			if (d86f_has_surface_desc(drive))
				memset(dev->thin_track_surface_data[thin_track][side], 0, 106096);
			memset(dev->thin_track_encoded_data[thin_track][side], 0, 106096);
		}
	}
    }

    d86f_zero_track(drive);

    dev->cur_track = track;

    if (! fdd_doublestep_40(drive)) {
	for (side = 0; side < sides; side++) {
		for (thin_track = 0; thin_track < 2; thin_track++)
			d86f_read_track(drive, track, thin_track, side, dev->thin_track_encoded_data[thin_track][side], dev->thin_track_surface_data[thin_track][side]);

		d86f_construct_encoded_buffer(drive, side);
	}
    } else {
	for (side = 0; side < sides; side++)
		d86f_read_track(drive, track, 0, side, dev->track_encoded_data[side], dev->track_surface_data[side]);
    }

    dev->state = STATE_IDLE;
}


void
d86f_write_track(int drive, FILE **f, int side, uint16_t *da0, uint16_t *sa0)
{
    uint32_t array_size = d86f_get_array_size(drive, side, 0);
    uint16_t side_flags = d86f_handler[drive].side_flags(drive);
    uint32_t extra_bit_cells = d86f_handler[drive].extra_bit_cells(drive, side);
    uint32_t index_hole_pos = d86f_handler[drive].index_hole_pos(drive, side);

    fwrite(&side_flags, 1, 2, *f);

    if (d86f_has_extra_bit_cells(drive))
	fwrite(&extra_bit_cells, 1, 4, *f);

    fwrite(&index_hole_pos, 1, 4, *f);

    if (d86f_has_surface_desc(drive))
	fwrite(sa0, 1, array_size, *f);

    fwrite(da0, 1, array_size, *f);
}


int
d86f_get_track_table_size(int drive)
{
    int temp = 2048;

    if (d86f_get_sides(drive) == 1)
	temp >>= 1;

    return temp;
}


void
d86f_set_cur_track(int drive, int track)
{
    d86f_t *dev = d86f[drive];

    dev->cur_track = track;
}


void
d86f_write_tracks(int drive, FILE **f, uint32_t *track_table)
{
    d86f_t *dev = d86f[drive];
    int sides, fdd_side;
    int side, thin_track;
    int logical_track = 0;
    uint32_t *tbl;
    tbl = dev->track_offset;
    fdd_side = fdd_get_head(drive);
    sides = d86f_get_sides(drive);

    if (track_table)
	tbl = track_table;

    if (!fdd_doublestep_40(drive)) {
	d86f_decompose_encoded_buffer(drive, 0);
	if (sides == 2)
		d86f_decompose_encoded_buffer(drive, 1);

	for (thin_track = 0; thin_track < 2; thin_track++) {
		for (side = 0; side < sides; side++) {
			fdd_set_head(drive, side);

			if (sides == 2)
				logical_track = ((dev->cur_track + thin_track) << 1) + side;
			else
				logical_track = dev->cur_track + thin_track;

			if (track_table && !tbl[logical_track]) {
				fseek(*f, 0, SEEK_END);
				tbl[logical_track] = ftell(*f);
			}

			if (tbl[logical_track]) {
				fseek(*f, tbl[logical_track], SEEK_SET);
				d86f_write_track(drive, f, side, dev->thin_track_encoded_data[thin_track][side], dev->thin_track_surface_data[thin_track][side]);
			}
		}
	}
    } else {
	for (side = 0; side < sides; side++) {
		fdd_set_head(drive, side);
		if (sides == 2)
			logical_track = (dev->cur_track << 1) + side;
		else
			logical_track = dev->cur_track;

		if (track_table && !tbl[logical_track]) {
			fseek(*f, 0, SEEK_END);
			tbl[logical_track] = ftell(*f);
		}

		if (tbl[logical_track]) {
			fseek(*f, tbl[logical_track], SEEK_SET);
			d86f_write_track(drive, f, side, d86f_handler[drive].encoded_data(drive, side), dev->track_surface_data[side]);
		}
	}
    }

    fdd_set_head(drive, fdd_side);
}


void
d86f_writeback(int drive)
{
    d86f_t *dev = d86f[drive];
    uint8_t header[32];
    int header_size;
#ifdef D86F_COMPRESS
    uint32_t len;
    int ret = 0;
    FILE *cf;
#endif
    header_size = d86f_header_size(drive);

    if (! dev->f) return;

    /* First write the track offsets table. */
    fseek(dev->f, 0, SEEK_SET);
    fread(header, 1, header_size, dev->f);

    fseek(dev->f, 8, SEEK_SET);
    fwrite(dev->track_offset, 1, d86f_get_track_table_size(drive), dev->f);

    d86f_write_tracks(drive, &dev->f, NULL);

#ifdef D86F_COMPRESS
    if (dev->is_compressed) {
	/* The image is compressed. */

	/* Open the original, compressed file. */
	cf = plat_fopen(dev->original_file_name, L"wb");

	/* Write the header to the original file. */
	fwrite(header, 1, header_size, cf);

	fseek(dev->f, 0, SEEK_END);
	len = ftell(dev->f);
	len -= header_size;

	fseek(dev->f, header_size, SEEK_SET);

	/* Compress data from the temporary uncompressed file to the original, compressed file. */
	dev->filebuf = (uint8_t *) malloc(len);
	dev->outbuf = (uint8_t *) malloc(len - 1);
	fread(dev->filebuf, 1, len, dev->f);
	ret = lzf_compress(dev->filebuf, len, dev->outbuf, len - 1);

	if (! ret)
		d86f_log("86F: Error compressing file\n");

	fwrite(dev->outbuf, 1, ret, cf);
	free(dev->outbuf);
	free(dev->filebuf);
    }
#endif
}


void
d86f_stop(int drive)
{
    d86f_t *dev = d86f[drive];

    if (dev)
	dev->state = STATE_IDLE;
}


int
d86f_common_command(int drive, int sector, int track, int side, int rate, int sector_size)
{
    d86f_t *dev = d86f[drive];

    d86f_log("d86f_common_command (drive %i): fdc_period=%i img_period=%i rate=%i sector=%i track=%i side=%i\n", drive, fdc_get_bitcell_period(d86f_fdc), d86f_get_bitcell_period(drive), rate, sector, track, side);

    dev->req_sector.id.c = track;
    dev->req_sector.id.h = side;
    if (sector == SECTOR_FIRST)
	dev->req_sector.id.r = 1;
    else if (sector == SECTOR_NEXT)
	dev->req_sector.id.r++;
    else
	dev->req_sector.id.r = sector;
    dev->req_sector.id.n = sector_size;

    if (fdd_get_head(drive) && (d86f_get_sides(drive) == 1)) {
	fdc_noidam(d86f_fdc);
	dev->state = STATE_IDLE;
	dev->index_count = 0;
	return 0;
    }

    dev->id_find.sync_marks = dev->id_find.bits_obtained = dev->id_find.bytes_obtained = 0;
    dev->data_find.sync_marks = dev->data_find.bits_obtained = dev->data_find.bytes_obtained = 0;
    dev->index_count = dev->error_condition = dev->satisfying_bytes = 0;
    dev->id_found = 0;
    dev->dma_over = 0;

    return 1;
}


void
d86f_readsector(int drive, int sector, int track, int side, int rate, int sector_size)
{
    d86f_t *dev = d86f[drive];
    int ret = 0;

    ret = d86f_common_command(drive, sector, track, side, rate, sector_size);
    if (! ret)
	return;

    if (sector == SECTOR_FIRST)
	dev->state = STATE_02_SPIN_TO_INDEX;
    else if (sector == SECTOR_NEXT)
	dev->state = STATE_02_FIND_ID;
    else
	dev->state = fdc_is_deleted(d86f_fdc) ? STATE_0C_FIND_ID : (fdc_is_verify(d86f_fdc) ? STATE_16_FIND_ID : STATE_06_FIND_ID);
}


void
d86f_writesector(int drive, int sector, int track, int side, int rate, int sector_size)
{
    d86f_t *dev = d86f[drive];
    int ret = 0;

    if (writeprot[drive]) {
	fdc_writeprotect(d86f_fdc);
	dev->state = STATE_IDLE;
	dev->index_count = 0;
	return;
    }

    ret = d86f_common_command(drive, sector, track, side, rate, sector_size);
    if (! ret) return;

    dev->state = fdc_is_deleted(d86f_fdc) ? STATE_09_FIND_ID : STATE_05_FIND_ID;
}


void
d86f_comparesector(int drive, int sector, int track, int side, int rate, int sector_size)
{
    d86f_t *dev = d86f[drive];
    int ret = 0;

    ret = d86f_common_command(drive, sector, track, side, rate, sector_size);
    if (! ret) return;

    dev->state = STATE_11_FIND_ID;
}


void
d86f_readaddress(int drive, int side, int rate)
{
    d86f_t *dev = d86f[drive];

    if (fdd_get_head(drive) && (d86f_get_sides(drive) == 1)) {
	fdc_noidam(d86f_fdc);
	dev->state = STATE_IDLE;
	dev->index_count = 0;
	return;
    }

    dev->id_find.sync_marks = dev->id_find.bits_obtained = dev->id_find.bytes_obtained = 0;
    dev->data_find.sync_marks = dev->data_find.bits_obtained = dev->data_find.bytes_obtained = 0;
    dev->index_count = dev->error_condition = dev->satisfying_bytes = 0;
    dev->id_found = 0;
    dev->dma_over = 0;

    dev->state = STATE_0A_FIND_ID;
}


void
d86f_add_track(int drive, int track, int side)
{
    d86f_t *dev = d86f[drive];
    uint32_t array_size;
    int logical_track;

    array_size = d86f_get_array_size(drive, side, 0);

    if (d86f_get_sides(drive) == 2) {
	logical_track = (track << 1) + side;
    } else {
	if (side)
		return;
	logical_track = track;
    }

    if (! dev->track_offset[logical_track]) {
	/* Track is absent from the file, let's add it. */
	dev->track_offset[logical_track] = dev->file_size;

	dev->file_size += (array_size + 6);
	if (d86f_has_extra_bit_cells(drive))
		dev->file_size += 4;
	if (d86f_has_surface_desc(drive))
		dev->file_size += array_size;
    }
}


void
d86f_common_format(int drive, int side, int rate, uint8_t fill, int proxy)
{
    d86f_t *dev = d86f[drive];
    uint32_t i = 0;
    uint16_t temp, temp2;
    uint32_t array_size;

    if (writeprot[drive]) {
	fdc_writeprotect(d86f_fdc);
	dev->state = STATE_IDLE;
	dev->index_count = 0;
	return;
    }

    if (! d86f_can_format(drive)) {
	fdc_cannotformat(d86f_fdc);
	dev->state = STATE_IDLE;
	dev->index_count = 0;
	return;
    }

    if (!side || (d86f_get_sides(drive) == 2)) {
	if (! proxy) {
		d86f_reset_index_hole_pos(drive, side);

		if (dev->cur_track > 256) {
			fdc_writeprotect(d86f_fdc);
			dev->state = STATE_IDLE;
			dev->index_count = 0;
			return;
		}

		array_size = d86f_get_array_size(drive, side, 0);

		if (d86f_has_surface_desc(drive)) {
			/* Preserve the physical holes but get rid of the fuzzy bytes. */
			for (i = 0; i < array_size; i++) {
				temp = dev->track_encoded_data[side][i] ^ 0xffff;
				temp2 = dev->track_surface_data[side][i];
				temp &= temp2;
				dev->track_surface_data[side][i] = temp;
			}
		}

		/* Zero the data buffer. */
		memset(dev->track_encoded_data[side], 0, array_size);

		d86f_add_track(drive, dev->cur_track, side);
		if (! fdd_doublestep_40(drive))
			d86f_add_track(drive, dev->cur_track + 1, side);
	}
    }

    dev->fill  = fill;

    if (! proxy) {
	dev->side_flags[side] = 0;
	dev->side_flags[side] |= (fdd_getrpm(real_drive(d86f_fdc, drive)) == 360) ? 0x20 : 0;
	dev->side_flags[side] |= fdc_get_bit_rate(d86f_fdc);
	dev->side_flags[side] |= fdc_is_mfm(d86f_fdc) ? 8 : 0;

	dev->index_hole_pos[side] = 0;
    }

    dev->id_find.sync_marks = dev->id_find.bits_obtained = dev->id_find.bytes_obtained = 0;
    dev->data_find.sync_marks = dev->data_find.bits_obtained = dev->data_find.bytes_obtained = 0;
    dev->index_count = dev->error_condition = dev->satisfying_bytes = dev->sector_count = 0;
    dev->dma_over = 0;

    if (!side || (d86f_get_sides(drive) == 2))
	dev->state = STATE_0D_SPIN_TO_INDEX;
      else
	dev->state = STATE_0D_NOP_SPIN_TO_INDEX;
}


void
d86f_proxy_format(int drive, int side, int rate, uint8_t fill)
{
    d86f_common_format(drive, side, rate, fill, 1);
}


void
d86f_format(int drive, int side, int rate, uint8_t fill)
{
    d86f_common_format(drive, side, rate, fill, 0);
}


void
d86f_common_handlers(int drive)
{
    drives[drive].readsector = d86f_readsector;
    drives[drive].writesector = d86f_writesector;
    drives[drive].comparesector =d86f_comparesector;
    drives[drive].readaddress = d86f_readaddress;
    drives[drive].byteperiod = d86f_byteperiod;
    drives[drive].poll = d86f_poll;
    drives[drive].format = d86f_proxy_format;
    drives[drive].stop = d86f_stop;
}


int
d86f_export(int drive, wchar_t *fn)
{
    uint32_t tt[512];
    d86f_t *dev = d86f[drive];
    d86f_t *temp86;
    FILE *f;
    int tracks = 86;
    int i;
    int inc = 1;
    uint32_t magic = 0x46423638;
    uint16_t version = 0x020C;
    uint16_t disk_flags = d86f_handler[drive].disk_flags(drive);

    memset(tt, 0, 512 * sizeof(uint32_t));

    f = plat_fopen(fn, L"wb");
    if (!f)
	return 0;

    /* Allocate a temporary drive for conversion. */
    temp86 = (d86f_t *)malloc(sizeof(d86f_t));
    memcpy(temp86, dev, sizeof(d86f_t));

    fwrite(&magic, 4, 1, f);
    fwrite(&version, 2, 1, f);
    fwrite(&disk_flags, 2, 1, f);

    fwrite(tt, 1, ((d86f_get_sides(drive) == 2) ? 2048 : 1024), f);

    /* In the case of a thick track drive, always increment track
       by two, since two tracks are going to get output at once. */
    if (!fdd_doublestep_40(drive))
	inc = 2;

    for (i = 0; i < tracks; i += inc) {
	if (inc == 2)
		fdd_do_seek(drive, i >> 1);
	else
		fdd_do_seek(drive, i);
	dev->cur_track = i;
	d86f_write_tracks(drive, &f, tt);
    }

    fclose(f);

    f = plat_fopen(fn, L"rb+");

    fseek(f, 8, SEEK_SET);
    fwrite(tt, 1, ((d86f_get_sides(drive) == 2) ? 2048 : 1024), f);

    fclose(f);

    fdd_do_seek(drive, fdd_current_track(drive));

    /* Restore the drive from temp. */
    memcpy(dev, temp86, sizeof(d86f_t));
    free(temp86);

    return 1;
}


void
d86f_load(int drive, wchar_t *fn)
{
    d86f_t *dev = d86f[drive];
    uint32_t magic = 0;
    uint32_t len = 0;
    int i = 0, j = 0;
#ifdef D86F_COMPRESS
    wchar_t temp_file_name[2048];
    uint16_t temp = 0;
    FILE *tf;
#endif

    d86f_unregister(drive);

    writeprot[drive] = 0;

    dev->f = plat_fopen(fn, L"rb+");
    if (! dev->f) {
	dev->f = plat_fopen(fn, L"rb");
	if (! dev->f) {
		memset(floppyfns[drive], 0, sizeof(floppyfns[drive]));
		free(dev);
		return;
	}
	writeprot[drive] = 1;
    }

    if (ui_writeprot[drive]) {
	writeprot[drive] = 1;
    }
    fwriteprot[drive] = writeprot[drive];

    fseek(dev->f, 0, SEEK_END);
    len = ftell(dev->f);
    fseek(dev->f, 0, SEEK_SET);

    fread(&magic, 4, 1, dev->f);

    if (len < 16) {
	/* File is WAY too small, abort. */
	fclose(dev->f);
	dev->f = NULL;
	memset(floppyfns[drive], 0, sizeof(floppyfns[drive]));
	free(dev);
	return;
    }

    if ((magic != 0x46423638) && (magic != 0x66623638)) {
	/* File is not of the valid format, abort. */
	d86f_log("86F: Unrecognized magic bytes: %08X\n", magic);
	fclose(dev->f);
	memset(floppyfns[drive], 0, sizeof(floppyfns[drive]));
	free(dev);
	return;
    }

    fread(&(dev->version), 2, 1, dev->f);
    if (dev->version != D86FVER) {
	/* File is not of a recognized format version, abort. */
	if (dev->version == 0x0063) {
		d86f_log("86F: File has emulator-internal version 0.99, this version is not valid in a file\n");
	} else if ((dev->version >= 0x0100) && (dev->version < D86FVER)) {
		d86f_log("86F: No longer supported development file version: %i.%02i\n", dev->version >> 8, dev->version & 0xff);
	} else {
		d86f_log("86F: Unrecognized file version: %i.%02i\n", dev->version >> 8, dev->version & 0xff);
	}
	fclose(dev->f);
	dev->f = NULL;
	memset(floppyfns[drive], 0, sizeof(floppyfns[drive]));
	free(dev);
	return;
    } else {
	d86f_log("86F: Recognized file version: %i.%02i\n", dev->version >> 8, dev->version & 0xff);
    }

    fread(&(dev->disk_flags), 2, 1, dev->f);

    if (d86f_has_surface_desc(drive)) {
	for (i = 0; i < 2; i++)
		dev->track_surface_data[i] = (uint16_t *) malloc(53048 * sizeof(uint16_t));

	for (i = 0; i < 2; i++) {
		for (j = 0; j < 2; j++)
			dev->thin_track_surface_data[i][j] = (uint16_t *) malloc(53048 * sizeof(uint16_t));
	}
    }

#ifdef D86F_COMPRESS
    dev->is_compressed = (magic == 0x66623638) ? 1 : 0;
    if ((len < 51052) && !dev->is_compressed) {
#else
    if (len < 51052) {
#endif
	/* File too small, abort. */
	fclose(dev->f);
	dev->f = NULL;
	memset(floppyfns[drive], 0, sizeof(floppyfns[drive]));
	free(dev);
	return;
    }

#ifdef DO_CRC64
    fseek(dev->f, 8, SEEK_SET);
    fread(&read_crc64, 1, 8, dev->f);

    fseek(dev->f, 0, SEEK_SET);

    crc64 = 0xffffffffffffffff;

    dev->filebuf = malloc(len);
    fread(dev->filebuf, 1, len, dev->f);
    *(uint64_t *) &(dev->filebuf[8]) = 0xffffffffffffffff;
    crc64 = (uint64_t) crc64speed(0, dev->filebuf, len);
    free(dev->filebuf);

    if (crc64 != read_crc64) {
	d86f_log("86F: CRC64 error\n");
	fclose(dev->f);
	dev->f = NULL;
	memset(floppyfns[drive], 0, sizeof(floppyfns[drive]));
	free(dev);
	return;
    }
#endif

#ifdef D86F_COMPRESS
    if (dev->is_compressed) {
	memcpy(temp_file_name, drive ? nvr_path(L"TEMP$$$1.$$$") : nvr_path(L"TEMP$$$0.$$$"), 256);
	memcpy(dev->original_file_name, fn, (wcslen(fn) << 1) + 2);

	fclose(dev->f);
	dev->f = NULL;

	dev->f = plat_fopen(temp_file_name, L"wb");
	if (! dev->f) {
		d86f_log("86F: Unable to create temporary decompressed file\n");
		memset(floppyfns[drive], 0, sizeof(floppyfns[drive]));
		free(dev);
		return;
	}

	tf = plat_fopen(fn, L"rb");

	for (i = 0; i < 8; i++) {
		fread(&temp, 1, 2, tf);
		fwrite(&temp, 1, 2, dev->f);
	}

	dev->filebuf = (uint8_t *) malloc(len);
	dev->outbuf = (uint8_t *) malloc(67108864);
	fread(dev->filebuf, 1, len, tf);
	temp = lzf_decompress(dev->filebuf, len, dev->outbuf, 67108864);
	if (temp) {
		fwrite(dev->outbuf, 1, temp, dev->f);
	}
	free(dev->outbuf);
	free(dev->filebuf);

	fclose(tf);
	fclose(dev->f);
	dev->f = NULL;

	if (! temp) {
		d86f_log("86F: Error decompressing file\n");
		plat_remove(temp_file_name);
		memset(floppyfns[drive], 0, sizeof(floppyfns[drive]));
		free(dev);
		return;
	}

	dev->f = plat_fopen(temp_file_name, L"rb+");
    }
#endif

    if (dev->disk_flags & 0x100) {
	/* Zoned disk. */
	d86f_log("86F: Disk is zoned (Apple or Sony)\n");
	fclose(dev->f);
	dev->f = NULL;
#ifdef D86F_COMPRESS
	if (dev->is_compressed)
		plat_remove(temp_file_name);
#endif
	memset(floppyfns[drive], 0, sizeof(floppyfns[drive]));
	free(dev);
	return;
    }

    if (dev->disk_flags & 0x600) {
	/* Zone type is not 0 but the disk is fixed-RPM. */
	d86f_log("86F: Disk is fixed-RPM but zone type is not 0\n");
	fclose(dev->f);
	dev->f = NULL;
#ifdef D86F_COMPRESS
	if (dev->is_compressed)
		plat_remove(temp_file_name);
#endif
	memset(floppyfns[drive], 0, sizeof(floppyfns[drive]));
	free(dev);
	return;
    }

    if (!writeprot[drive]) {
	writeprot[drive] = (dev->disk_flags & 0x10) ? 1 : 0;
	fwriteprot[drive] = writeprot[drive];
    }

    if (writeprot[drive]) {
	fclose(dev->f);
	dev->f = NULL;

#ifdef D86F_COMPRESS
	if (dev->is_compressed)
		dev->f = plat_fopen(temp_file_name, L"rb");
	else
#endif
		dev->f = plat_fopen(fn, L"rb");
    }

    /* OK, set the drive data, other code needs it. */
    d86f[drive] = dev;

    fseek(dev->f, 8, SEEK_SET);

    fread(dev->track_offset, 1, d86f_get_track_table_size(drive), dev->f);

    if (! (dev->track_offset[0])) {
	/* File has no track 0 side 0, abort. */
	d86f_log("86F: No Track 0 side 0\n");
	fclose(dev->f);
	dev->f = NULL;
	memset(floppyfns[drive], 0, sizeof(floppyfns[drive]));
	free(dev);
	d86f[drive] = NULL;
	return;
    }

    if ((d86f_get_sides(drive) == 2) && !(dev->track_offset[1])) {
	/* File is 2-sided but has no track 0 side 1, abort. */
	d86f_log("86F: No Track 0 side 1\n");
	fclose(dev->f);
	dev->f = NULL;
	memset(floppyfns[drive], 0, sizeof(floppyfns[drive]));
	free(dev);
	d86f[drive] = NULL;
	return;
    }

    /* Load track 0 flags as default. */
    fseek(dev->f, dev->track_offset[0], SEEK_SET);
    fread(&(dev->side_flags[0]), 2, 1, dev->f);
    if (dev->disk_flags & 0x80) {
	fread(&(dev->extra_bit_cells[0]), 4, 1, dev->f);
	if ((dev->disk_flags & 0x1060) != 0x1000) {
		if (dev->extra_bit_cells[0] < -32768)  dev->extra_bit_cells[0] = -32768;
		if (dev->extra_bit_cells[0] > 32768)  dev->extra_bit_cells[0] = 32768;
	}
    } else {
	dev->extra_bit_cells[0] = 0;
    }

    if (d86f_get_sides(drive) == 2) {
	fseek(dev->f, dev->track_offset[1], SEEK_SET);
	fread(&(dev->side_flags[1]), 2, 1, dev->f);
	if (dev->disk_flags & 0x80) {
		fread(&(dev->extra_bit_cells[1]), 4, 1, dev->f);
		if ((dev->disk_flags & 0x1060) != 0x1000) {
			if (dev->extra_bit_cells[1] < -32768)  dev->extra_bit_cells[1] = -32768;
			if (dev->extra_bit_cells[1] > 32768)  dev->extra_bit_cells[1] = 32768;
		}
	} else {
		dev->extra_bit_cells[0] = 0;
	}
    } else {
	switch ((dev->disk_flags >> 1) >> 3) {
		case 0:
		default:
			dev->side_flags[1] = 0x0a;
			break;

		case 1:
			dev->side_flags[1] = 0x00;
			break;

		case 2:
		case 3:
			dev->side_flags[1] = 0x03;
			break;
	}

	dev->extra_bit_cells[1] = 0;
    }

    fseek(dev->f, 0, SEEK_END);
    dev->file_size = ftell(dev->f);

    fseek(dev->f, 0, SEEK_SET);

    d86f_register_86f(drive);

    drives[drive].seek = d86f_seek;
    d86f_common_handlers(drive);
    drives[drive].format = d86f_format;

#ifdef D86F_COMPRESS
    d86f_log("86F: Disk is %scompressed and does%s have surface description data\n",
	dev->is_compressed ? "" : "not ",
	d86f_has_surface_desc(drive) ? "" : " not");
#else
    d86f_log("86F: Disk does%s have surface description data\n",
	d86f_has_surface_desc(drive) ? "" : " not");
#endif
}


void
d86f_init(void)
{
    int i;

    setup_crc(0x1021);

    for (i = 0; i < FDD_NUM; i++)
	d86f[i] = NULL;
}


void
d86f_set_fdc(void *fdc)
{
    d86f_fdc = (fdc_t *) fdc;
}


void
d86f_close(int drive)
{
    int i, j;

    wchar_t temp_file_name[2048];
    d86f_t *dev = d86f[drive];

    /* Make sure the drive is alive. */
    if (dev == NULL) return;

    memcpy(temp_file_name, drive ? nvr_path(L"TEMP$$$1.$$$") : nvr_path(L"TEMP$$$0.$$$"), 26);

    if (d86f_has_surface_desc(drive)) {
	for (i = 0; i < 2; i++) {
		if (dev->track_surface_data[i]) {
			free(dev->track_surface_data[i]);
			dev->track_surface_data[i] = NULL;
		}
	}

	for (i = 0; i < 2; i++) {
		for (j = 0; j < 2; j++) {
			if (dev->thin_track_surface_data[i][j]) {
				free(dev->thin_track_surface_data[i][j]);
				dev->thin_track_surface_data[i][j] = NULL;
			}
		}
	}
    }

    if (dev->f) {
	fclose(dev->f);
	dev->f = NULL;
    }
#ifdef D86F_COMPRESS
    if (dev->is_compressed)
	plat_remove(temp_file_name);
#endif
}


/* When an FDD is mounted, set up the D86F data structures. */
void
d86f_setup(int drive)
{
    d86f_t *dev;

    /* Allocate a drive structure. */
    dev = (d86f_t *)malloc(sizeof(d86f_t));
    memset(dev, 0x00, sizeof(d86f_t));
    dev->state = STATE_IDLE;

    dev->last_side_sector[0] = NULL;
    dev->last_side_sector[1] = NULL;

    /* Set the drive as active. */
    d86f[drive] = dev;
}


/* If an FDD is unmounted, unlink the D86F data structures. */
void
d86f_destroy(int drive)
{
    int i, j;

    d86f_t *dev = d86f[drive];

    if (dev == NULL) return;

    if (d86f_has_surface_desc(drive)) {
	for (i = 0; i < 2; i++) {
		if (dev->track_surface_data[i]) {
			free(dev->track_surface_data[i]);
			dev->track_surface_data[i] = NULL;
		}
	}

	for (i = 0; i < 2; i++) {
		for (j = 0; j < 2; j++) {
			if (dev->thin_track_surface_data[i][j]) {
				free(dev->thin_track_surface_data[i][j]);
				dev->thin_track_surface_data[i][j] = NULL;
			}
		}
	}
    }

    d86f_destroy_linked_lists(drive, 0);
    d86f_destroy_linked_lists(drive, 1);

    free(d86f[drive]);
    d86f[drive] = NULL;

    d86f_handler[drive].read_data = NULL;
}
