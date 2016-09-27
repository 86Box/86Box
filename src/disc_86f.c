/* Copyright holders: Tenshi
   see COPYING for more details
*/
#include "ibm.h"
#include "disc.h"
#include "disc_86f.h"
#include "disc_random.h"
#include "fdc.h"
#include "fdd.h"

#define MAX_SECTORS 256

enum
{
        STATE_IDLE,
        STATE_READ_FIND_SECTOR,
        STATE_READ_SECTOR,
	STATE_READ_SECTOR_CRC,
	STATE_READ_SECTOR_GAP3,
        STATE_READ_FIND_FIRST_SECTOR,
        STATE_READ_FIRST_SECTOR,
	STATE_READ_FIRST_SECTOR_CRC,
	STATE_READ_FIRST_SECTOR_GAP3,
        STATE_READ_FIND_NEXT_SECTOR,
        STATE_READ_NEXT_SECTOR,
	STATE_READ_NEXT_SECTOR_CRC,
	STATE_READ_NEXT_SECTOR_GAP3,
        STATE_WRITE_FIND_SECTOR,
        STATE_WRITE_SECTOR,
	STATE_WRITE_SECTOR_CRC,
	STATE_WRITE_SECTOR_GAP3,
        STATE_READ_FIND_ADDRESS,
        STATE_READ_ADDRESS,
        STATE_FORMAT_FIND,
        STATE_FORMAT,
	STATE_SEEK,
};

static uint16_t CRCTable[256];

static int d86f_drive;
        
typedef struct
{
	uint8_t c;
	uint8_t h;
	uint8_t r;
	uint8_t n;
} sector_id_fields_t;

typedef union
{
	uint32_t dword;
	uint8_t byte_array[4];
	sector_id_fields_t id;
} sector_id_t;

static struct
{
        FILE *f;
	uint16_t version;
	uint16_t disk_flags;
        uint8_t track_data[2][50000];
        uint16_t track_encoded_data[2][50000];
        uint8_t track_layout[2][50000];
        uint16_t side_flags[2];
        uint16_t index_hole_pos[2];
	uint8_t track_in_file;
        uint32_t track_offset[256];
	uint32_t file_size;
	sector_id_t format_sector_id;
	sector_id_t rw_sector_id;
	sector_id_t last_sector;
	sector_id_t req_sector;
	uint32_t index_count;
	uint8_t state;
	uint8_t fill;
	uint32_t track_pos;
	uint32_t datac;
	uint32_t id_pos;
	uint32_t section_pos;
	crc_t calc_crc;
	crc_t track_crc;
	uint8_t track_byte;
	uint8_t track_index;
	uint8_t track_is_hole;
	uint8_t track_fuzzy;
	uint8_t track_data_byte;
	uint8_t old_track_byte;
	uint8_t old_track_index;
	uint8_t old_track_is_hole;
	uint8_t old_track_fuzzy;
	uint8_t old_track_data_byte;
	uint8_t cur_track;
	uint8_t side_flag_bytes;
	uint8_t wait_state;
	uint8_t id_am_counter;
	uint8_t id_counter;
	uint8_t id_match;
	uint8_t data_am_counter;
} d86f[2];

uint8_t encoded_fm[64] = {	0xAA, 0xAB, 0xAE, 0xAF, 0xBA, 0xBB, 0xBE, 0xBF, 0xEA, 0xEB, 0xEE, 0xEF, 0xFA, 0xFB, 0xFE, 0xFF,
				0xAA, 0xAB, 0xAE, 0xAF, 0xBA, 0xBB, 0xBE, 0xBF, 0xEA, 0xEB, 0xEE, 0xEF, 0xFA, 0xFB, 0xFE, 0xFF,
				0xAA, 0xAB, 0xAE, 0xAF, 0xBA, 0xBB, 0xBE, 0xBF, 0xEA, 0xEB, 0xEE, 0xEF, 0xFA, 0xFB, 0xFE, 0xFF,
				0xAA, 0xAB, 0xAE, 0xAF, 0xBA, 0xBB, 0xBE, 0xBF, 0xEA, 0xEB, 0xEE, 0xEF, 0xFA, 0xFB, 0xFE, 0xFF };

uint8_t encoded_mfm[64] = {	0xAA, 0xA9, 0xA4, 0xA5, 0x92, 0x91, 0x94, 0x95, 0x4A, 0x49, 0x44, 0x45, 0x52, 0x51, 0x54, 0x55,
				0x2A, 0x29, 0x24, 0x25, 0x12, 0x11, 0x14, 0x15, 0x4A, 0x49, 0x44, 0x45, 0x52, 0x51, 0x54, 0x55,
				0xAA, 0xA9, 0xA4, 0xA5, 0x92, 0x91, 0x94, 0x95, 0x4A, 0x49, 0x44, 0x45, 0x52, 0x51, 0x54, 0x55,
				0x2A, 0x29, 0x24, 0x25, 0x12, 0x11, 0x14, 0x15, 0x4A, 0x49, 0x44, 0x45, 0x52, 0x51, 0x54, 0x55 };

typedef union {
	uint16_t word;
	uint8_t bytes[2];
} encoded_t;

typedef struct {
	unsigned nibble0	:4;
	unsigned nibble1	:4;
} split_byte_t;

typedef struct {
	unsigned area0		:2;
	unsigned area1		:6;
} preceding_byte_t;

typedef union {
	uint8_t byte;
	split_byte_t nibbles;
	preceding_byte_t p;
} decoded_t;

static uint16_t d86f_encode_get_data(uint8_t dat)
{
        uint16_t temp;
        temp = 0;
        if (dat & 0x01) temp |= 256;
        if (dat & 0x02) temp |= 1024;
        if (dat & 0x04) temp |= 4096;
        if (dat & 0x08) temp |= 16384;
        if (dat & 0x10) temp |= 1;
        if (dat & 0x20) temp |= 4;
        if (dat & 0x40) temp |= 16;
        if (dat & 0x80) temp |= 64;
        return temp;
}

static uint16_t d86f_encode_get_clock(uint8_t dat)
{
        uint16_t temp;
        temp = 0;
        if (dat & 0x01) temp |= 512;
        if (dat & 0x02) temp |= 2048;
        if (dat & 0x04) temp |= 8192;
        if (dat & 0x08) temp |= 32768;
        if (dat & 0x10) temp |= 2;
        if (dat & 0x20) temp |= 8;
        if (dat & 0x40) temp |= 32;
        if (dat & 0x80) temp |= 128;
        return temp;
}

uint8_t* d86f_track_data(int drive, int side)
{
	return d86f[drive].track_data[side];
}

uint8_t* d86f_track_layout(int drive, int side)
{
	return d86f[drive].track_layout[side];
}

uint16_t d86f_side_flags(int drive);
int d86f_is_mfm(int drive);
void d86f_writeback(int drive);
void d86f_set_sector(int drive, int side, uint8_t c, uint8_t h, uint8_t r, uint8_t n);
uint8_t d86f_poll_read_data(int drive, int side, uint16_t pos);
void d86f_poll_write_data(int drive, int side, uint16_t pos, uint8_t data);
int d86f_format_conditions(int drive);

uint16_t d86f_disk_flags(int drive)
{
	return d86f[drive].disk_flags;
}

uint16_t null_disk_flags(int drive)
{
	return 0x09;
}

uint16_t null_side_flags(int drive)
{
	return 0x0A;
}

void null_writeback(int drive)
{
	return;
}

void null_set_sector(int drive, int side, uint8_t c, uint8_t h, uint8_t r, uint8_t n)
{
	return;
}

uint8_t null_poll_read_data(int drive, int side, uint16_t pos)
{
	return 0xf6;
}

void null_poll_write_data(int drive, int side, uint16_t pos, uint8_t data)
{
	return;
}

int null_format_conditions(int drive)
{
	return 1;
}

void d86f_unregister(int drive)
{
	d86f_handler[drive].disk_flags = null_disk_flags;
	d86f_handler[drive].side_flags = null_side_flags;
	d86f_handler[drive].writeback = null_writeback;
	d86f_handler[drive].set_sector = null_set_sector;
	d86f_handler[drive].read_data = null_poll_read_data;
	d86f_handler[drive].write_data = null_poll_write_data;
	d86f_handler[drive].format_conditions = null_format_conditions;
	d86f_handler[drive].check_crc = 0;
	d86f[drive].version = 0x0063;						/* Proxied formats report as version 0.99. */
}

void d86f_register_86f(int drive)
{
	d86f_handler[drive].disk_flags = d86f_disk_flags;
	d86f_handler[drive].side_flags = d86f_side_flags;
	d86f_handler[drive].writeback = d86f_writeback;
	d86f_handler[drive].set_sector = d86f_set_sector;
	d86f_handler[drive].read_data = d86f_poll_read_data;
	d86f_handler[drive].write_data = d86f_poll_write_data;
	d86f_handler[drive].format_conditions = d86f_format_conditions;
	d86f_handler[drive].check_crc = 1;
}

/* Needed for formatting! */
int d86f_is_40_track(int drive)
{
	return (d86f_handler[drive].disk_flags(drive) & 1) ? 0 : 1;
}

int d86f_realtrack(int drive, int track)
{
        if (d86f_is_40_track(drive) && fdd_doublestep_40(drive))
                track /= 2;

	return track;
}

static void d86f_setupcrc(uint16_t poly, uint16_t rvalue)
{
	int c = 256, bc;
	uint16_t crctemp;

	while(c--)
	{
		crctemp = c << 8;
		bc = 8;

		while(bc--)
		{
			if(crctemp & 0x8000)
			{
				crctemp = (crctemp << 1) ^ poly;
			}
			else
			{
				crctemp <<= 1;
			}
		}

		CRCTable[c] = crctemp;
	}
}

void disc_calccrc(int drive, uint8_t byte, crc_t *crc_var)
{
	crc_var->word = (crc_var->word << 8) ^ CRCTable[(crc_var->word >> 8)^byte];
}

static void d86f_calccrc(int drive, uint8_t byte)
{
	// d86f[drive].calc_crc.word = (d86f[drive].calc_crc.word << 8) ^ CRCTable[(d86f[drive].calc_crc.word >> 8)^byte];
	disc_calccrc(drive, byte, &(d86f[drive].calc_crc));
}

void d86f_init()
{
	disc_random_init();

        memset(d86f, 0, sizeof(d86f));
        d86f_setupcrc(0x1021, 0xcdb4);

	// d86f_unregister(0);
	// d86f_unregister(1);
}

int d86f_get_sides(int drive)
{
	return (d86f_handler[drive].disk_flags(drive) & 8) ? 2 : 1;
}

int d86f_get_rpm_mode(int drive)
{
	if (d86f[drive].version != 0x0132)  return 0;
	return (d86f_handler[drive].disk_flags(drive) & 0x60) >> 5;
}

int d86f_get_array_size(int drive)
{
	int pos = 0;
	int rm = 0;
	rm = d86f_get_rpm_mode(drive);
	switch (d86f_hole(drive))
	{
		case 0:
		default:
			pos = 7500;
			if (d86f[drive].version != 0x0132)  return pos;
			switch (rm)
			{
				case 1:
					pos = 7575;
					break;
				case 2:
					pos = 7614;
					break;
				case 3:
					pos = 7653;
					break;
				default:
					pos = 7500;
					break;
			}
			break;
		case 1:
			pos = 12500;
			if (d86f[drive].version != 0x0132)  return pos;
			switch (rm)
			{
				case 1:
					pos = 12626;
					break;
				case 2:
					pos = 12690;
					break;
				case 3:
					pos = 12755;
					break;
				default:
					pos = 12500;
					break;
			}
			break;
		case 2:
			pos = 50000;
			if (d86f[drive].version != 0x0132)  return pos;
			switch (rm)
			{
				case 1:
					pos = 50505;
					break;
				case 2:
					pos = 50761;
					break;
				case 3:
					pos = 51020;
					break;
				default:
					pos = 50000;
					break;
			}
			break;
	}
	return pos;
}

int d86f_valid_bit_rate(int drive)
{
	int rate = 0;
	rate = fdc_get_bit_rate();
	switch (d86f_hole(drive))
	{
		case 0:	/* DD */
			if ((rate < 1) || (rate > 2))  return 0;
			return 1;
		case 1:	/* HD */
			if (rate != 0)  return 0;
			return 1;
		case 2:	/* ED */
			if (rate < 3)  return 0;
			return 1;
	}
	return 1;
}

void d86f_common_handlers(int drive)
{
        drives[drive].readsector  = d86f_readsector;
        drives[drive].writesector = d86f_writesector;
        drives[drive].readaddress = d86f_readaddress;
        drives[drive].hole        = d86f_hole;
        drives[drive].byteperiod  = d86f_byteperiod;
        drives[drive].poll        = d86f_poll;
        drives[drive].format      = d86f_format;
        drives[drive].realtrack   = d86f_realtrack;
        drives[drive].stop        = d86f_stop;
}

void d86f_load(int drive, char *fn)
{
	uint32_t magic = 0;
	uint32_t len = 0;

	int i = 0;
	int j = 0;

	uint8_t temp = 0;

	d86f_unregister(drive);

	writeprot[drive] = 0;
        d86f[drive].f = fopen(fn, "rb+");
        if (!d86f[drive].f)
        {
                d86f[drive].f = fopen(fn, "rb");
                if (!d86f[drive].f)
                        return;
                writeprot[drive] = 1;
        }
	if (ui_writeprot[drive])
	{
                writeprot[drive] = 1;
	}
        fwriteprot[drive] = writeprot[drive];

	fseek(d86f[drive].f, 0, SEEK_END);
	len = ftell(d86f[drive].f);
	fseek(d86f[drive].f, 0, SEEK_SET);

	if (len < 52056)
	{
		/* File too small, abort. */
		fclose(d86f[drive].f);
		return;
	}

	fread(&magic, 4, 1, d86f[drive].f);

	if (magic != 0x46423638)
	{
		/* File is not of the valid format, abort. */
		pclog("86F: Unrecognized magic bytes: %08X\n", magic);
		fclose(d86f[drive].f);
		return;
	}

	fread(&(d86f[drive].version), 2, 1, d86f[drive].f);

	if (d86f[drive].version != 0x0132)
	{
		/* File is not of a recognized format version abort. */
		if (d86f[drive].version == 0x0063)
		{
			pclog("86F: File has emulator-internal 86F version 0.99, this version is not valid in a file\n", d86f[drive].version >> 8, d86f[drive].version & 0xFF);
		}
		else ((d86f[drive].version >= 0x0100) && (d86f[drive].version < 0x0132))
		{
			pclog("86F: No longer supported development file version: %i.%02i\n", d86f[drive].version >> 8, d86f[drive].version & 0xFF);
		}
		else
		{
			pclog("86F: Unrecognized file version: %i.%02i\n", d86f[drive].version >> 8, d86f[drive].version & 0xFF);
		}
		fclose(d86f[drive].f);
		return;
	}
	else
	{
		pclog("86F: Recognized file version: %i.%02i\n", d86f[drive].version >> 8, d86f[drive].version & 0xFF);
	}

	fread(&(d86f[drive].disk_flags), 2, 1, d86f[drive].f);

	if (((d86f[drive].disk_flags >> 1) & 3) == 3)
	{
		/* Invalid disk hole. */
		pclog("86F: Unrecognized disk hole type 3\n");
		fclose(d86f[drive].f);
		return;
	}

	if (d86f[drive].disk_flags & 0x100)
	{
		/* Zoned disk. */
		pclog("86F: Disk is zoned (Apple or Sony)\n");
		fclose(d86f[drive].f);
		return;
	}

	if (!writeprot[drive])
	{
		writeprot[drive] = (d86f[drive].disk_flags & 0x10) ? 1 : 0;
	        fwriteprot[drive] = writeprot[drive];
	}

	fread(d86f[drive].track_offset, 1, 1024, d86f[drive].f);

	if (!(d86f[drive].track_offset[0]))
	{
		/* File has no track 0, abort. */
		pclog("86F: No Track 0\n");
		fclose(d86f[drive].f);
		return;
	}

	/* Load track 0 flags as default. */
	d86f[drive].side_flag_bytes = d86f_get_sides(drive);

	fseek(d86f[drive].f, d86f[drive].track_offset[0], SEEK_SET);
	d86f[drive].side_flags[0] = d86f[drive].side_flags[1] = 0;
	fread(&(d86f[drive].side_flags[0]), 2, 1, d86f[drive].f);
	if (d86f_get_sides(drive) == 2)
	{
		fread(&(d86f[drive].side_flags[1]), 2, 1, d86f[drive].f);
	}

	fseek(d86f[drive].f, 0, SEEK_END);
	d86f[drive].file_size = ftell(d86f[drive].f);

	fseek(d86f[drive].f, 0, SEEK_SET);

	d86f_register_86f(drive);

        drives[drive].seek        = d86f_seek;
	d86f_common_handlers(drive);
}

int d86f_hole(int drive)
{
	return (d86f_handler[drive].disk_flags(drive) >> 1) & 3;
}

int d86f_is_encoded(int drive)
{
	return (d86f_handler[drive].disk_flags(drive) >> 7) & 1;
}

uint16_t d86f_side_flags(int drive)
{
	int side = 0;
	side = fdd_get_head(drive);
	return d86f[drive].side_flags[side];
}

uint16_t d86f_track_flags(int drive)
{
	uint16_t tf = 0;
	uint16_t rr = 0;
	uint16_t dr = 0;

	tf = d86f_handler[drive].side_flags(drive);
	rr = tf & 0x67;
	dr = fdd_get_flags(drive) & 7;
	tf &= ~0x67;

	switch (rr)
	{
		case 0x02:
		case 0x21:
			/* 1 MB unformatted medium, treat these two as equivalent. */
			switch (dr)
			{
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

double d86f_byteperiod(int drive)
{
	switch (d86f_track_flags(drive) & 0x0f)
	{
		case 0x02:	/* 125 kbps, FM */
			return 64.0;
		case 0x01:	/* 150 kbps, FM */
			return 320.0 / 6.0;
		case 0x0A:	/* 250 kbps, MFM */
		case 0x00:	/* 250 kbps, FM */
			return 32.0;
		case 0x09:	/* 300 kbps, MFM */
			return 160.0 / 6.0;
		case 0x08:	/* 500 kbps, MFM */
			return 16.0;
		case 0x0B:	/* 1000 kbps, MFM */
			return 8.0;
		case 0x0D:	/* 2000 kbps, MFM */
			return 4.0;
		default:
			return 32.0;
	}
	return 32.0;
}

void d86f_close(int drive)
{
        if (d86f[drive].f)
                fclose(d86f[drive].f);
        d86f[drive].f = NULL;
}

int d86f_is_mfm(int drive)
{
	return (d86f_track_flags(drive) & 8) ? 1 : 0;
}

uint32_t d86f_get_raw_size(int drive)
{
	double rate = 0.0;
	int mfm = 0;
	double rpm = 300.0;
	double rpm_diff = 0.0;
	double size = 6250.0;

	mfm = d86f_is_mfm(drive);
	rpm = ((d86f_track_flags(drive) & 0xE0) == 0x20) ? 360.0 : 300.0;
	rpm_diff = rpm * 0.005;

	if (d86f[drive].version == 0x0132)
	{
		switch (d86f_get_rpm_mode(drive))
		{
			case 1:
				rpm_diff *= 2.0;
				break;
			case 2:
				rpm_diff *= 3.0;
				break;
			case 3:
				rpm_diff *= 4.0;
				break;
			default:
				rpm_diff = 0.0;
				break;
		}
		rpm -= rpm_diff;
	}
	switch (d86f_track_flags(drive) & 7)
	{
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
	if (!mfm)  rate /= 2.0;
	size = (size / 250.0) * rate;
	size = (size * 300.0) / rpm;
	return (uint16_t) size;
}

void d86f_seek(int drive, int track)
{
	int sides;
        int side;
	int full_size, store_size;
	int flag_bytes = 5;
	sides = d86f_get_sides(drive);

	full_size = d86f_get_array_size(drive);
	store_size = full_size << 1;
	if (d86f_is_encoded(drive))  store_size += full_size;
	if (d86f_get_sides(drive) == 2)  flag_bytes += 4;

        if (d86f_is_40_track(drive) && fdd_doublestep_40(drive))
                track /= 2;

	for (side = 0; side < d86f_get_sides(drive); side++)
	{
		memset(d86f[drive].track_layout[side], BYTE_GAP0, 50000);
		if (d86f_is_encoded(drive))
		{
			memset(d86f[drive].track_encoded_data[side], 0xFF, 50000);
		}
		else
		{
			memset(d86f[drive].track_data[side], 0xFF, 50000);
		}
	}

	d86f[drive].cur_track = track;

	if (!(d86f[drive].track_offset[track]))
	{
		/* Track does not exist in the image, initialize it as unformatted. */
		d86f[drive].track_in_file = 0;
		for (side = 0; side < d86f_get_sides(drive); side++)
		{
			d86f[drive].side_flags[side] = 0x0A;	/* 300 rpm, MFM, 250 kbps */
		}
		return;
	}

	d86f[drive].track_in_file = 1;

	fseek(d86f[drive].f, d86f[drive].track_offset[track], SEEK_SET);

	for (side = 0; side < d86f[drive].side_flag_bytes; side++)
	{
		fread(&(d86f[drive].side_flags[side]), 2, 1, d86f[drive].f);
	}

	for (side = 0; side < d86f[drive].side_flag_bytes; side++)
	{
		fread(&(d86f[drive].index_hole_pos[side]), 2, 1, d86f[drive].f);
	}

	for (side = 0; side < d86f_get_sides(drive); side++)
	{
		fseek(d86f[drive].f, d86f[drive].track_offset[track] + (side * store_size) + flag_bytes, SEEK_SET);
		fread(d86f[drive].track_layout[side], 1, d86f_get_raw_size(drive), d86f[drive].f);
		fseek(d86f[drive].f, d86f[drive].track_offset[track] + (side * store_size) + full_size + flag_bytes, SEEK_SET);
		if (d86f_is_encoded(drive))
		{
			fread(d86f[drive].track_encoded_data[side], 1, d86f_get_raw_size(drive) << 1, d86f[drive].f);
		}
		else
		{
			fread(d86f[drive].track_data[side], 1, d86f_get_raw_size(drive), d86f[drive].f);
		}
	}
}

void d86f_writeback(int drive)
{
	int track = d86f[drive].cur_track;
	uint8_t track_id = track;
	int side;
	int full_size, store_size;
	int flag_bytes = 5;

	full_size = d86f_get_array_size(drive);
	store_size = full_size << 1;
	if (d86f_is_encoded(drive))  store_size += full_size;
	if (d86f_get_sides(drive) == 2)  flag_bytes += 4;

        if (!d86f[drive].f)
	{
                return;
	}
                
        if (!d86f[drive].track_in_file)
	{
                return; /*Should never happen*/
	}

	fseek(d86f[drive].f, 7, SEEK_SET);
	fwrite(d86f[drive].track_offset, 1, 1024, d86f[drive].f);

	fseek(d86f[drive].f, d86f[drive].track_offset[track], SEEK_SET);

	for (side = 0; side < d86f[drive].side_flag_bytes; side++)
	{
		fwrite(&(d86f[drive].side_flags[side]), 2, 1, d86f[drive].f);
	}

	for (side = 0; side < d86f[drive].side_flag_bytes; side++)
	{
		fwrite(&(d86f[drive].index_hole_pos[side]), 2, 1, d86f[drive].f);
	}

	fwrite(&track_id, 1, 1, d86f[drive].f);

	for (side = 0; side < d86f_get_sides(drive); side++)
	{
		fseek(d86f[drive].f, d86f[drive].track_offset[track] + (side * store_size) + flag_bytes, SEEK_SET);
		fwrite(d86f[drive].track_layout[side], 1, d86f_get_raw_size(drive), d86f[drive].f);
		fseek(d86f[drive].f, d86f[drive].track_offset[track] + (side * store_size) + full_size + flag_bytes, SEEK_SET);
		if (d86f_is_encoded(drive))
		{
			fwrite(d86f[drive].track_encoded_data[side], 1, d86f_get_raw_size(drive) << 1, d86f[drive].f);
		}
		else
		{
			fwrite(d86f[drive].track_data[side], 1, d86f_get_raw_size(drive), d86f[drive].f);
		}
	}

	// pclog("d86f_writeback(): %08X\n", d86f[drive].track_offset[track]);
}

void d86f_reset(int drive, int side)
{
	if (side == 0)
	{
		d86f[drive].index_count = 0;
		d86f[drive].state = STATE_SEEK;
	}
}

static int d86f_get_bitcell_period(int drive)
{
	double rate = 0.0;
	int mfm = 0;
	int tflags = 0;
	double rpm = 0;
	double size = 8000.0;

	tflags = d86f_track_flags(drive);

	mfm = (tflags & 8) ? 1 : 0;
	rpm = ((tflags & 0xE0) == 0x20) ? 360.0 : 300.0;

	switch (tflags & 7)
	{
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
	if (!mfm)  rate /= 2.0;
	size = (size * 250.0) / rate;
	size = (size * 300.0) / rpm;
	size = (size * fdd_getrpm(drive ^ fdd_swap)) / 300.0;
	return (int) size;
}

void d86f_readsector(int drive, int sector, int track, int side, int rate, int sector_size)
{
        // pclog("d86f_readsector: fdc_period=%i img_period=%i rate=%i sector=%i track=%i side=%i\n", fdc_get_bitcell_period(), d86f_get_bitcell_period(drive), rate, sector, track, side);

        d86f[drive].req_sector.id.c = track;
        d86f[drive].req_sector.id.h = side;

	if (fdd_get_head(drive) && (d86f_get_sides(drive) == 1))
	{
		fdc_notfound();
		d86f[drive].state = STATE_IDLE;
		d86f[drive].index_count = 0;
		return;
	}

        d86f_drive = drive;

        if (sector == SECTOR_FIRST)
                d86f[drive].req_sector.id.r = 1;
        else if (sector == SECTOR_NEXT)
                d86f[drive].req_sector.id.r++;
	else
	        d86f[drive].req_sector.id.r = sector;

	d86f[drive].rw_sector_id.dword = 0xFFFFFFFF;
	d86f[drive].last_sector.dword = 0xFFFFFFFF;
	d86f[drive].req_sector.id.n = sector_size;
	d86f[drive].index_count = 0;
	d86f[drive].wait_state = 1;
        d86f[drive].state = STATE_READ_FIND_SECTOR;
}

void d86f_writesector(int drive, int sector, int track, int side, int rate, int sector_size)
{
        d86f[drive].req_sector.id.c = track;
        d86f[drive].req_sector.id.h = side;
        d86f[drive].req_sector.id.r = sector;
	d86f[drive].req_sector.id.n = sector_size;

        // pclog("d86f_writesector: drive=%c: fdc_period=%i img_period=%i rate=%i chrn=%08X\n", drive + 0x41, fdc_get_bitcell_period(), d86f_get_bitcell_period(drive), rate, d86f[drive].req_sector.dword);

	if (writeprot[drive] || swwp)
	{
		// pclog("Write protected\n");
		fdc_writeprotect();
		return;
	}

	if (fdd_get_head(drive) && (d86f_get_sides(drive) == 1))
	{
		// pclog("Wrong side\n");
		fdc_notfound();
		d86f[drive].state = STATE_IDLE;
		d86f[drive].index_count = 0;
		return;
	}

	d86f[drive].rw_sector_id.dword = 0xFFFFFFFF;
	d86f[drive].last_sector.dword = 0xFFFFFFFF;
        d86f_drive = drive;
	d86f[drive].index_count = 0;
	d86f[drive].wait_state = 1;
        d86f[drive].state = STATE_WRITE_FIND_SECTOR;
}

void d86f_readaddress(int drive, int track, int side, int rate)
{
        // pclog("d86f_readaddress: fdc_period=%i img_period=%i rate=%i track=%i side=%i\n", fdc_get_bitcell_period(), d86f_get_bitcell_period(drive), rate, track, side);

        d86f[drive].req_sector.id.c = track;
        d86f[drive].req_sector.id.h = side;

	if (fdd_get_head(drive) && (d86f_get_sides(drive) == 1))
	{
		fdc_notfound();
		d86f[drive].state = STATE_IDLE;
		d86f[drive].index_count = 0;
		return;
	}

	d86f[drive].rw_sector_id.dword = 0xFFFFFFFF;
	d86f[drive].last_sector.dword = 0xFFFFFFFF;
        d86f_drive = drive;
	d86f[drive].index_count = 0;
	d86f[drive].wait_state = 1;
        d86f[drive].state = STATE_READ_FIND_ADDRESS;
}

uint16_t d86f_prepare_pretrack(int drive, int side, int iso, int write_data)
{
	uint16_t i;

	int mfm = 0;
	int real_gap0_len = 0;
	int sync_len = 0;
	int real_gap1_len = 0;

	mfm = d86f_is_mfm(drive);
	real_gap0_len = mfm ? 80 : 40;
	sync_len = mfm ? 12 : 6;
	real_gap1_len = mfm ? 50 : 26;

	d86f[drive].index_hole_pos[side] = 0;

	if (d86f_is_encoded(drive))  write_data = 0;

	memset(d86f[drive].track_layout[side], BYTE_GAP0, d86f_get_raw_size(drive));
	if (write_data)  memset(d86f[drive].track_data[side], mfm ? 0x4E : 0xFF, d86f_get_raw_size(drive));
	i = 0;

	if (!iso)
	{
		memset(d86f[drive].track_layout[side] + i, BYTE_GAP0, real_gap0_len);
		if (write_data)  memset(d86f[drive].track_data[side] + i, mfm ? 0x4E : 0xFF, real_gap0_len);
		i += real_gap0_len;
		memset(d86f[drive].track_layout[side] + i, BYTE_I_SYNC, sync_len);
		if (write_data)  memset(d86f[drive].track_data[side] + i, 0, sync_len);
		i += sync_len;
		if (mfm)
		{
			memset(d86f[drive].track_layout[side] + i, BYTE_IAM_SYNC, 3);
			if (write_data)  memset(d86f[drive].track_data[side] + i, 0xC2, 3);
			i += 3;
		}
		memset(d86f[drive].track_layout[side] + i, BYTE_IAM, 1);
		if (write_data)  memset(d86f[drive].track_data[side] + i, 0xFC, 1);
		i++;
	}
	memset(d86f[drive].track_layout[side] + i, BYTE_GAP1, real_gap1_len);
	if (write_data)  memset(d86f[drive].track_data[side] + i, mfm ? 0x4E : 0xFF, real_gap1_len);
	i += real_gap1_len;

	return i;
}

void d86f_calccrc_buf(int drive, uint8_t *buf, uint16_t len)
{
	uint16_t i = 0;

	for (i = 0; i < len; i++)
	{
		d86f_calccrc(drive, buf[i]);
	}
}

static void *d86f_memset(void *str, int c, size_t n, size_t s, uint16_t rs, int limit)
{
	void *temp;

	size_t wrap_n[2];
	uint8_t *wrap_str[2];

	if ((n + s - 1) >= rs)
	{
		/* This is going to wrap around, so we need to split the data in two. */
		wrap_n[0] = ((rs - 1) - s) + 1;
		wrap_n[1] = n - wrap_n[0];
		wrap_str[0] = (uint8_t *) str;
		wrap_str[1] = wrap_str[0] - s;
		temp = memset(wrap_str[0], c, wrap_n[0]);
		if (!limit)  temp = memset(wrap_str[1], c, wrap_n[1]);
	}
	else
	{
		/* No wrap around, do a standard memcpy. */
		temp = memset(str, c, n);
	}
	return temp;
}

static void *d86f_memcpy(void *str1, const void *str2, size_t n, size_t s, uint16_t rs, int limit)
{
	void *temp;

	size_t wrap_n[2];
	uint8_t *wrap_str1[2];
	uint8_t *wrap_str2[2];

	if ((n + s - 1) >= rs)
	{
		/* This is going to wrap around, so we need to split the data in two. */
		wrap_n[0] = ((rs - 1) - s) + 1;
		wrap_n[1] = n - wrap_n[0];
		wrap_str1[0] = (uint8_t *) str1;
		wrap_str2[0] = (uint8_t *) str2;
		wrap_str1[1] = wrap_str1[0] - s;
		wrap_str2[1] = wrap_str2[0] + wrap_n[0];
		temp = memcpy(wrap_str1[0], wrap_str2[0], wrap_n[0]);
		if (!limit)  temp = memcpy(wrap_str1[1], wrap_str2[1], wrap_n[1]);
	}
	else
	{
		/* No wrap around, do a standard memcpy. */
		temp = memcpy(str1, str2, n);
	}
	return temp;
}

void d86f_reset_index_hole_pos(int drive, int side)
{
	d86f[drive].index_hole_pos[side] = 0;
}

uint16_t d86f_prepare_sector(int drive, int side, int pos, uint8_t *id_buf, uint8_t *data_buf, int data_len, int write_data, int gap2, int gap3, int limit)
{
	uint16_t i = pos;
	uint16_t j = 0;
	uint16_t rs = 0;

	uint8_t am[4] = { 0xA1, 0xA1, 0xA1 };

	int real_gap2_len = gap2;
	int real_gap3_len = gap3;
	int mfm = 0;
	int sync_len = 0;

	if (d86f_is_encoded(drive))  write_data = 0;

	rs = d86f_get_raw_size(drive);

	mfm = d86f_is_mfm(drive);
	sync_len = mfm ? 12 : 6;

	d86f_memset(d86f[drive].track_layout[side] + i, BYTE_ID_SYNC, sync_len, i, rs, limit);
	if (write_data)  d86f_memset(d86f[drive].track_data[side] + i, 0, sync_len, i, rs, limit);
	i += sync_len;
	if ((i >= rs) && limit)  return 0;
	i %= rs;
	if (mfm)
	{
		d86f_memset(d86f[drive].track_layout[side] + i, BYTE_IDAM_SYNC, 3, i, rs, limit);
		if (write_data)
		{
			d86f[drive].calc_crc.word = 0xffff;
			d86f_memset(d86f[drive].track_data[side] + i, 0xA1, 3, i, rs, limit);
			d86f_calccrc_buf(drive, am, 3);
		}
		i += 3;
		if ((i >= rs) && limit)  return 0;
		i %= rs;
	}
	d86f_memset(d86f[drive].track_layout[side] + i, BYTE_IDAM, 1, i, rs, limit);
	if (write_data)
	{
		if (!mfm)  d86f[drive].calc_crc.word = 0xffff;
		d86f_memset(d86f[drive].track_data[side] + i, 0xFE, 1, i, rs, limit);
		d86f_calccrc(drive, 0xFE);
	}
	i++;
	if ((i >= rs) && limit)  return 0;
	i %= rs;
	d86f_memset(d86f[drive].track_layout[side] + i, BYTE_ID, 4, i, rs, limit);
	if (write_data)
	{
		d86f_memcpy(d86f[drive].track_data[side] + i, id_buf, 4, i, rs, limit);
		d86f_calccrc_buf(drive, id_buf, 4);
		// if ((id_buf[0] == 4) && (id_buf[1] == 0) && (id_buf[2] == 19) && (id_buf[3] == 2))  pclog("Prepare (%i %i %i %i): ID CRC %04X\n", id_buf[0], id_buf[1], id_buf[2], id_buf[3], d86f[drive].calc_crc);
	}
	i += 4;
	if ((i >= rs) && limit)  return 0;
	i %= rs;
	d86f_memset(d86f[drive].track_layout[side] + i, BYTE_ID_CRC, 2, i, rs, limit);
	if (write_data)
	{
		d86f[drive].track_data[side][i] = d86f[drive].calc_crc.bytes[1];
		d86f[drive].track_data[side][(i + 1) % rs] = d86f[drive].calc_crc.bytes[0];
	}
	i += 2;
	if ((i >= rs) && limit)  return 0;
	i %= rs;
	d86f_memset(d86f[drive].track_layout[side] + i, BYTE_GAP2, real_gap2_len, i, rs, limit);
	if (write_data)  d86f_memset(d86f[drive].track_data[side] + i, mfm ? 0x4E : 0xFF, real_gap2_len, i, rs, limit);
	i += real_gap2_len;
	if ((i >= rs) && limit)  return 0;
	i %= rs;
	d86f_memset(d86f[drive].track_layout[side] + i, BYTE_DATA_SYNC, sync_len, i, rs, limit);
	if (write_data)  d86f_memset(d86f[drive].track_data[side] + i, 0, sync_len, i, rs, limit);
	i += sync_len;
	if ((i >= rs) && limit)  return 0;
	i %= rs;
	if (mfm)
	{
		d86f_memset(d86f[drive].track_layout[side] + i, BYTE_DATAAM_SYNC, 3, i, rs, limit);
		if (write_data)
		{
			d86f[drive].calc_crc.word = 0xffff;
			d86f_memset(d86f[drive].track_data[side] + i, 0xA1, 3, i, rs, limit);
			d86f_calccrc_buf(drive, am, 3);
		}
		i += 3;
		if ((i >= rs) && limit)  return 0;
		i %= rs;
	}
	d86f_memset(d86f[drive].track_layout[side] + i, BYTE_DATAAM, 1, i, rs, limit);
	if (write_data)
	{
		if (!mfm)  d86f[drive].calc_crc.word = 0xffff;
		d86f_memset(d86f[drive].track_data[side] + i, 0xFB, 1, i, rs, limit);
		d86f_calccrc(drive, 0xFB);
	}
	i++;
	if ((i >= rs) && limit)  return 0;
	i %= rs;
	d86f_memset(d86f[drive].track_layout[side] + i, BYTE_DATA, data_len, i, rs, limit);
	if (write_data)
	{
		d86f_memcpy(d86f[drive].track_data[side] + i, data_buf, data_len, i, rs, limit);
		d86f_calccrc_buf(drive, data_buf, data_len);
		// if ((id_buf[0] == 4) && (id_buf[1] == 0) && (id_buf[2] == 19) && (id_buf[3] == 2))  pclog("Prepare (%i %i %i %i): Data CRC %04X\n", id_buf[0], id_buf[1], id_buf[2], id_buf[3], d86f[drive].calc_crc);
	}
	i += data_len;
	if ((i >= rs) && limit)  return 0;
	i %= rs;
	d86f_memset(d86f[drive].track_layout[side] + i, BYTE_DATA_CRC, 2, i, rs, limit);
	if (write_data)
	{
		d86f[drive].track_data[side][i] = d86f[drive].calc_crc.bytes[1];
		d86f[drive].track_data[side][(i + 1) % rs] = d86f[drive].calc_crc.bytes[0];
	}
	i += 2;
	if ((i >= rs) && limit)  return 0;
	i %= rs;
	d86f_memset(d86f[drive].track_layout[side] + i, BYTE_GAP3, real_gap3_len, i, rs, limit);
	d86f_memset(d86f[drive].track_data[side] + i, mfm ? 0x4E : 0xFF, real_gap3_len, i, rs, limit);
	i += real_gap3_len;
	if ((i >= rs) && limit)  return 0;
	i %= rs;

	return i;
}

void d86f_prepare_track_layout(int drive, int side)
{
	uint16_t i = 0;
	uint16_t j = 0;
	uint16_t sc = 0;
	uint16_t dtl = 0;
	int real_gap2_len = 0;
	int real_gap3_len = 0;
	sc = fdc_get_format_sectors();
	dtl = 128 << fdc_get_format_n();
	real_gap2_len = fdc_get_gap2(drive ^ fdd_swap);
	real_gap3_len = fdc_get_gap();

	i = d86f_prepare_pretrack(drive, side, 0, 0);

	for (j = 0; j < sc; j++)
	{
		/* Always limit to prevent wraparounds when formatting! */
		i = d86f_prepare_sector(drive, side, i, NULL, NULL, dtl, 0, real_gap2_len, real_gap3_len, 1);
	}
}

int d86f_format_conditions(int drive)
{
	return d86f_valid_bit_rate(drive);
}

int d86f_can_format(int drive)
{
	int temp;
	temp = !writeprot[drive];
	temp = temp && !swwp;
	temp = temp && fdd_can_read_medium(drive ^ fdd_swap);
	temp = temp && d86f_handler[drive].format_conditions(drive);		/* Allows proxied formats to add their own extra conditions to formatting. */
	return temp;
}

void d86f_format(int drive, int track, int side, int rate, uint8_t fill)
{
	int full_size, store_size;
	int flag_bytes = 5;

	full_size = d86f_get_array_size(drive);
	store_size = full_size << 1;
	if (d86f_is_encoded(drive))  store_size += full_size;
	if (d86f_get_sides(drive) == 2)  flag_bytes += 4;

        d86f[drive].req_sector.id.c = d86f[drive].cur_track;
        d86f[drive].req_sector.id.h  = side;

	if ((side && (d86f_get_sides(drive) == 1)) || !(d86f_can_format(drive)))
	{
		fdc_notfound();
		d86f[drive].state = STATE_IDLE;
		d86f[drive].index_count = 0;
		return;
	}

	if ((d86f[drive].cur_track < 0) || (d86f[drive].cur_track > 256))
	{
		// pclog("Track below 0 or above 256\n");
		fdc_writeprotect();
		d86f[drive].state = STATE_IDLE;
		d86f[drive].index_count = 0;
		return;
	}

        d86f_drive = drive;
        d86f[drive].fill  = fill;
	d86f[drive].index_count = 0;
	/* Let's prepare the track space and layout before filling. */
	if (d86f[drive].version != 0x0063)
	{
		d86f[drive].side_flags[side] &= 0xc0;
		d86f[drive].side_flags[side] |= (fdd_getrpm(drive ^ fdd_swap) == 360) ? 0x20 : 0;
		d86f[drive].side_flags[side] |= fdc_get_bit_rate();
		d86f[drive].side_flags[side] |= fdc_is_mfm() ? 8 : 0;

		d86f[drive].index_hole_pos[side] = 0;
	}

	if (!d86f_is_encoded(drive))
	{
		memset(d86f[drive].track_data[side], 0xFF, full_size);
	}
	else
	{
		memset(d86f[drive].track_encoded_data[side], 0xFF, full_size * 2);
	}

	/* For version 0.99 (= another format proxied to the 86F handler), the track layout is fixed. */
	if (d86f[drive].version != 0x0063)
	{
		d86f_prepare_track_layout(drive, side);

		if (!d86f[drive].track_in_file)
		{
			/* Track is absent from the file, let's add it. */
			d86f[drive].track_offset[d86f[drive].cur_track] = d86f[drive].file_size;
			d86f[drive].file_size += store_size + flag_bytes;
			if (d86f_get_sides(drive) == 2)
			{
				d86f[drive].file_size += store_size;
			}
			d86f[drive].track_in_file = 1;
		}
	}

	// pclog("Formatting track %i side %i\n", d86f[drive].cur_track, side);

        d86f[drive].state = STATE_FORMAT_FIND;
}

void d86f_stop(int drive)
{
        d86f[drive].state = STATE_IDLE;
}

static void index_pulse(int drive)
{
	if (d86f[drive].state != STATE_IDLE)  fdc_indexpulse();
}

uint32_t d86f_get_data_len(int drive)
{
	if (d86f[drive].req_sector.id.n)
	{
		return (128 << ((uint32_t) d86f[drive].req_sector.id.n));
	}
	else
	{
		if (fdc_get_dtl() < 128)
		{
			return fdc_get_dtl();
		}
		else
		{
			return (128 << ((uint32_t) d86f[drive].req_sector.id.n));
		}
	}
}

uint8_t d86f_get_encoding(int drive)
{
	return (d86f_track_flags(drive) & 0x18) >> 3;
}

int d86f_can_read_address(int drive)
{
	int temp;
	temp = (fdc_get_bitcell_period() == d86f_get_bitcell_period(drive));
	temp = temp && fdd_can_read_medium(drive ^ fdd_swap);
	temp = temp && (fdc_is_mfm() == d86f_is_mfm(drive));
	temp = temp && (d86f_get_encoding(drive) <= 1);
	return temp;
}

int d86f_find_state_nf_ignore_id(int drive)
{
	int temp;
	temp = temp || (d86f[drive].state == STATE_READ_FIND_FIRST_SECTOR);
	temp = temp || (d86f[drive].state == STATE_READ_FIND_NEXT_SECTOR);
	return temp;
}

int d86f_find_state_nf(int drive)
{
	int temp;
	temp = (d86f[drive].state == STATE_READ_FIND_SECTOR);
	temp = temp || d86f_find_state_nf_ignore_id(drive);
	temp = temp || (d86f[drive].state == STATE_WRITE_FIND_SECTOR);
	temp = temp || (d86f[drive].state == STATE_READ_FIND_ADDRESS);
	return temp;
}

int d86f_find_state(int drive)
{
	int temp;
	temp = d86f_find_state_nf(drive);
	temp = temp || (d86f[drive].state == STATE_FORMAT_FIND);
	return temp;
}

int d86f_read_state_data(int drive)
{
	int temp;
	temp = (d86f[drive].state == STATE_READ_SECTOR);
	temp = temp || (d86f[drive].state == STATE_READ_FIRST_SECTOR);
	temp = temp || (d86f[drive].state == STATE_READ_NEXT_SECTOR);
	return temp;
}

int d86f_read_state_crc(int drive)
{
	int temp;
	temp = (d86f[drive].state == STATE_READ_SECTOR_CRC);
	temp = temp || (d86f[drive].state == STATE_READ_FIRST_SECTOR_CRC);
	temp = temp || (d86f[drive].state == STATE_READ_NEXT_SECTOR_CRC);
	return temp;
}

int d86f_read_state_gap3(int drive)
{
	int temp;
	temp = (d86f[drive].state == STATE_READ_SECTOR_GAP3);
	temp = temp || (d86f[drive].state == STATE_READ_FIRST_SECTOR_GAP3);
	temp = temp || (d86f[drive].state == STATE_READ_NEXT_SECTOR_GAP3);
	return temp;
}

int d86f_write_state(int drive)
{
	int temp;
	temp = (d86f[drive].state == STATE_WRITE_SECTOR);
	temp = temp || (d86f[drive].state == STATE_WRITE_SECTOR_CRC);
	temp = temp || (d86f[drive].state == STATE_WRITE_SECTOR_GAP3);
	return temp;
}

int d86f_read_state(int drive)
{
	int temp;
	temp = d86f_read_state_data(drive);
	temp = temp || d86f_read_state_crc(drive);
	temp = temp || d86f_read_state_gap3(drive);
	return temp;
}

int d86f_data_size(int drive)
{
	int temp;
	temp = d86f[drive].req_sector.id.n;
	temp = 128 << temp;
	return temp;
}

uint32_t d86f_get_pos(int drive)
{
	uint32_t pos;
	if (d86f[drive].track_pos >= d86f[drive].section_pos)
	{
		pos = d86f[drive].track_pos - d86f[drive].section_pos;
	}
	else
	{
		/* A wrap arround has occurred, let's add the raw size to the position. */
		pos = d86f_get_raw_size(drive) + d86f[drive].track_pos;
		pos -= d86f[drive].section_pos;
	}

	return pos;
}

static uint8_t decodefm(int drive, uint16_t dat)
{
        uint8_t temp;
	if (d86f_get_encoding(drive) > 1)
	{
		/* 2 means M2FM encoding, and 3 means GCR encoding, neither of which is supported by this emulator. */
		return 0xFF;
	}
        temp = 0;
	/* We write the encoded bytes in big endian, so we process the two 8-bit halves swapped here. */
        if (dat & 0x0100) temp |= 1;
        if (dat & 0x0400) temp |= 2;
        if (dat & 0x1000) temp |= 4;
        if (dat & 0x4000) temp |= 8;
        if (dat & 0x0001) temp |= 16;
        if (dat & 0x0004) temp |= 32;
        if (dat & 0x0010) temp |= 64;
        if (dat & 0x0040) temp |= 128;
        return temp;
}

uint16_t d86f_encode_byte(int drive, int sync, decoded_t b, decoded_t prev_b)
{
	uint8_t encoding = d86f_get_encoding(drive);
	uint8_t bits89AB = prev_b.nibbles.nibble0;
	uint8_t bits7654 = b.nibbles.nibble1;
	uint8_t bits3210 = b.nibbles.nibble0;
	uint16_t encoded_7654, encoded_3210, result;
	if (encoding > 1)  return 0xFF;
	if (sync)
	{
		result = d86f_encode_get_data(b.byte);
		if (encoding)
		{
			switch(b.byte)
			{
				case 0xA1: return result | d86f_encode_get_clock(0x0A);
				case 0xC2: return result | d86f_encode_get_clock(0x14);
				case 0xF8: return result | d86f_encode_get_clock(0x03);
				case 0xFB: case 0xFE: return result | d86f_encode_get_clock(0x00);
				case 0xFC: return result | d86f_encode_get_clock(0x01);
			}
		}
		else
		{
			switch(b.byte)
			{
				case 0xF8: case 0xFB: case 0xFE: return result | d86f_encode_get_clock(0xC7);
				case 0xFC: return result | d86f_encode_get_clock(0xD7);
			}
		}
	}
	bits3210 += ((bits7654 & 3) << 4);
	bits7654 += ((bits89AB & 3) << 4);
	encoded_3210 = (encoding == 1) ? encoded_mfm[bits3210] : encoded_fm[bits3210];
	encoded_7654 = (encoding == 1) ? encoded_mfm[bits7654] : encoded_fm[bits7654];
	result = (encoded_3210 << 8) | encoded_7654;
	return result;
}

uint8_t d86f_read_byte(int drive, int side)
{
	if (d86f[drive].track_is_hole)
	{
		return 0;
	}

	if (!d86f_is_encoded(drive))
	{
		return d86f[drive].track_data[side][d86f[drive].track_pos];
	}
	else
	{
		return decodefm(drive, d86f[drive].track_encoded_data[side][d86f[drive].track_pos]);
	}
}

void d86f_write_byte(int drive, int side, uint8_t byte)
{
	int sync = 0;
	decoded_t dbyte, dpbyte;
	d86f[drive].track_data_byte = byte;
	dbyte.byte = byte;
	dpbyte.byte = d86f[drive].old_track_data_byte;

	if (d86f[drive].track_is_hole)
	{
		return;
	}

	if (!d86f_is_encoded(drive))
	{
		d86f[drive].track_data[side][d86f[drive].track_pos] = byte;
	}
	else
	{
		if ((d86f[drive].track_byte == BYTE_IAM_SYNC) ||
		    (d86f[drive].track_byte == BYTE_IAM) ||
		    (d86f[drive].track_byte == BYTE_IDAM_SYNC) ||
		    (d86f[drive].track_byte == BYTE_IDAM) ||
		    (d86f[drive].track_byte == BYTE_DATAAM_SYNC) ||
		    (d86f[drive].track_byte == BYTE_DATAAM))
		{
			sync = 1;
		}

		d86f[drive].track_encoded_data[side][d86f[drive].track_pos] = d86f_encode_byte(drive, sync, dbyte, dpbyte);
	}
}

void d86f_poll_write_crc(int drive, int side)
{
	if (d86f[drive].state != STATE_FORMAT)  return;
	d86f[drive].id_pos = d86f_get_pos(drive);
	d86f_write_byte(drive, side, d86f[drive].calc_crc.bytes[d86f[drive].id_pos ^ 1]);
}

void d86f_poll_advancebyte(int drive, int side)
{
	if (d86f_handler[drive].side_flags == NULL)
	{
		fatal("NULL side flags handler\n");
	}
	d86f[drive].old_track_byte = d86f[drive].track_byte;
	d86f[drive].old_track_index = d86f[drive].track_index;
	d86f[drive].old_track_data_byte = d86f[drive].track_data_byte;
	d86f[drive].old_track_fuzzy = d86f[drive].track_fuzzy;
	d86f[drive].old_track_is_hole = d86f[drive].track_is_hole;

	d86f[drive].track_pos++;
	d86f[drive].track_pos %= d86f_get_raw_size(drive);

	d86f[drive].track_byte = d86f[drive].track_layout[side][d86f[drive].track_pos] & ~BYTE_IS_FUZZY;
	d86f[drive].track_byte = d86f[drive].track_layout[side][d86f[drive].track_pos] & ~0x20;
	d86f[drive].track_is_hole = d86f[drive].track_layout[side][d86f[drive].track_pos] & 0x20;
	d86f[drive].track_index = (d86f[drive].track_pos == d86f[drive].index_hole_pos[side]);
	d86f[drive].track_fuzzy = d86f[drive].track_layout[side][d86f[drive].track_pos] & BYTE_IS_FUZZY;

	if (d86f[drive].track_fuzzy && !d86f[drive].track_is_hole)
	{
		d86f[drive].track_data_byte = disc_random_generate();
	}
	else
	{
		d86f[drive].track_data_byte = d86f_read_byte(drive, side);
	}
}

void d86f_poll_reset(int drive, int side)
{
	d86f[drive].state = STATE_IDLE;
	d86f[drive].index_count = 0;
	d86f[drive].datac = 0;
	d86f_poll_advancebyte(drive, side);
}

int d86f_poll_check_notfound(int drive)
{
	if (d86f[drive].index_count > 1)
	{
		/* The index hole has been hit twice and we're still in a find state.
		   This means sector finding has failed for whatever reason.
		   Abort with sector not found and set state to idle. */
		// pclog("d86f_poll(): Sector not found (%i %i %i %i) (%i, %i)\n", d86f[drive].req_sector.id.c, d86f[drive].req_sector.id.h, d86f[drive].req_sector.id.r, d86f[drive].req_sector.id.n, fdc_get_bitcell_period(), d86f_get_bitcell_period(drive));
		fdc_notfound();
		d86f[drive].state = STATE_IDLE;
		d86f[drive].index_count = 0;
		return 1;
	}
	else
	{
		return 0;
	}
}

void d86f_poll_write(int drive, int side, uint8_t data, uint8_t type)
{
	d86f[drive].track_layout[side][d86f[drive].track_pos] = type;
	d86f_write_byte(drive, side, data);
}

void d86f_set_sector(int drive, int side, uint8_t c, uint8_t h, uint8_t r, uint8_t n)
{
	return;
}

uint8_t d86f_poll_read_data(int drive, int side, uint16_t pos)
{
	return d86f[drive].track_data_byte;
}

void d86f_poll_write_data(int drive, int side, uint16_t pos, uint8_t data)
{
	d86f_poll_write(drive, side, data, BYTE_DATA);
}

void d86f_poll_readwrite(int drive, int side)
{
        int data;
	uint16_t max_len;

	if (d86f_read_state_data(drive))
	{
		max_len = d86f_data_size(drive);

		data = d86f_handler[drive].read_data(drive, side, d86f[drive].datac);
		if (d86f[drive].datac < d86f_get_data_len(drive))
		{
			fdc_data(data);
		}
		d86f_calccrc(drive, data);
	}
	else if (d86f[drive].state == STATE_WRITE_SECTOR)
	{
		max_len = d86f_data_size(drive);

		if (d86f[drive].datac < d86f_get_data_len(drive))
		{
			data = fdc_getdata(d86f[drive].datac == ((128 << ((uint32_t) d86f[drive].last_sector.id.n)) - 1));

       		        if (data == -1)
			{
				data = 0;
			}
		}
		else
		{
			data = 0;
		}
		if (!disable_write)
		{
			d86f_handler[drive].write_data(drive, side, d86f[drive].datac, data);
		}
		d86f_calccrc(drive, data & 0xff);
	}
	else if (d86f_read_state_crc(drive))
	{
		max_len = 2;
		d86f[drive].track_crc.bytes[d86f[drive].datac ^ 1] = d86f[drive].track_data_byte;
	}
	else if (d86f[drive].state == STATE_WRITE_SECTOR_CRC)
	{
		max_len = 2;
		if (!disable_write)
		{
			d86f_poll_write(drive, side, d86f[drive].calc_crc.bytes[d86f[drive].datac ^ 1], BYTE_DATA_CRC);
		}
	}
	else if (d86f_read_state_gap3(drive))
	{
		max_len = 1;
		// max_len = fdc_get_gap();
		// if (d86f[drive].datac == (fdc_get_gap() - 1))
		// {
			d86f_poll_reset(drive, side);
			if ((d86f[drive].track_crc.word != d86f[drive].calc_crc.word) && d86f_handler[drive].check_crc)
			{
				// pclog("d86f_poll(): Data CRC error (%i %i %i %i) (%04X %04X)\n", d86f[drive].req_sector.id.c, d86f[drive].req_sector.id.h, d86f[drive].req_sector.id.r, d86f[drive].req_sector.id.n, d86f[drive].track_crc.word, d86f[drive].calc_crc.word);
				fdc_finishread();
				fdc_datacrcerror();
			}
			else
			{
				// pclog("Read finished (%i %i %i %i)\n", d86f[drive].req_sector.id.c, d86f[drive].req_sector.id.h, d86f[drive].req_sector.id.r, d86f[drive].req_sector.id.n);
        	               	fdc_sector_finishread();
			}
			return;
		// }
	}
	else if (d86f[drive].state == STATE_WRITE_SECTOR_GAP3)
	{
		// max_len = fdc_get_gap();
		max_len = 1;
		if (!disable_write && !d86f[drive].datac)
		{
			d86f_poll_write(drive, side, fdc_is_mfm() ? 0x4E : 0xFF, BYTE_GAP3);
		}
		// if (d86f[drive].datac == (max_len - 1))
		// {
    		        if (!disable_write)
			{
				d86f_handler[drive].writeback(drive);
			}
			d86f_poll_reset(drive, side);
			// pclog("Write finished (%i %i %i %i)\n", d86f[drive].req_sector.id.c, d86f[drive].req_sector.id.h, d86f[drive].req_sector.id.r, d86f[drive].req_sector.id.n);
       	        	fdc_sector_finishread(drive);
			return;
		// }
	}

	d86f[drive].datac++;
	d86f_poll_advancebyte(drive, side);

	if (d86f[drive].datac >= max_len)
	{
		d86f[drive].datac = 0;
		d86f[drive].state++;
	}
}

int d86f_end_wait_state(int drive)
{
	int temp = 0;
	int fdc_mfm = 0;
	int disk_mfm = 0;
	fdc_mfm = fdc_is_mfm();
	disk_mfm = d86f_is_mfm(drive);
	temp = temp || ((d86f[drive].track_byte == BYTE_IDAM_SYNC) && fdc_mfm && disk_mfm);
	temp = temp || ((d86f[drive].track_byte == BYTE_IDAM) && !fdc_mfm && !disk_mfm);
	temp = temp && d86f_can_read_address(drive);	/* This is so the wait state never ends if the data rate or encoding is wrong. */
	return temp;
}

void d86f_poll_find_nf_wait(int drive, int side)
{
	if (d86f[drive].track_index)
	{
		// pclog("d86f_poll_find_nf(): Index pulse\n");
		index_pulse(drive);
		d86f[drive].index_count++;
	}

	d86f_poll_advancebyte(drive, side);

	if (d86f_poll_check_notfound(drive))  return;

	if (d86f[drive].track_byte != d86f[drive].old_track_byte)
	{
		if (d86f_end_wait_state(drive))
		{
			d86f[drive].calc_crc.word = 0xffff;
			d86f[drive].id_am_counter = d86f[drive].id_counter = d86f[drive].id_match = d86f[drive].data_am_counter = d86f[drive].wait_state = 0;
		}
	}
}

void d86f_poll_find_nf(int drive, int side)
{
	uint8_t mfm = 0;
	uint8_t am_len = 0;
	mfm = fdc_is_mfm();
	am_len = mfm ? 4 : 1;

	if (d86f[drive].track_index)
	{
		// pclog("d86f_poll_find_nf(): Index pulse\n");
		index_pulse(drive);
		d86f[drive].index_count++;
	}

	switch(d86f[drive].track_byte)
	{
		case BYTE_DATAAM_SYNC:
			if ((d86f[drive].id_match) || d86f_find_state_nf_ignore_id(drive))
			{
				d86f[drive].data_am_counter++;

				if (mfm)
				{
					d86f_calccrc(drive, d86f[drive].track_data_byte);
				}
			}
			break;

		case BYTE_IDAM_SYNC:
			d86f[drive].id_am_counter++;

			if (mfm)
			{
				d86f_calccrc(drive, d86f[drive].track_data_byte);
			}
			break;

		case BYTE_DATAAM:
			if ((d86f[drive].id_match) || d86f_find_state_nf_ignore_id(drive))
			{
				if ((d86f[drive].state == STATE_WRITE_FIND_SECTOR) && (d86f[drive].data_am_counter == (am_len - 1)))
				{
					d86f_poll_write(drive, side, 0xFB, BYTE_DATAAM);
				}

				d86f[drive].data_am_counter++;
				d86f[drive].id_match = 0;

				d86f_calccrc(drive, d86f[drive].track_data_byte);
			}
			break;

		case BYTE_IDAM:
			d86f[drive].id_am_counter++;
			d86f_calccrc(drive, d86f[drive].track_data_byte);
			break;

		case BYTE_ID:
			d86f[drive].id_pos = d86f_get_pos(drive);
			d86f[drive].last_sector.byte_array[d86f[drive].id_pos] = d86f[drive].track_data_byte;
			d86f_calccrc(drive, d86f[drive].track_data_byte);
			if (d86f[drive].id_am_counter == am_len)
			{
				d86f[drive].id_counter++;
			}
			break;
		case BYTE_ID_CRC:
			d86f[drive].id_pos = d86f_get_pos(drive);
			d86f[drive].track_crc.bytes[d86f[drive].id_pos ^ 1] = d86f[drive].track_data_byte;
			break;
	}

	d86f_poll_advancebyte(drive, side);

	if (d86f_poll_check_notfound(drive))  return;

	if (d86f[drive].track_byte != d86f[drive].old_track_byte)
	{
		d86f[drive].section_pos = d86f[drive].track_pos;

		switch(d86f[drive].track_byte)
		{
			case BYTE_IDAM_SYNC:
			case BYTE_DATAAM_SYNC:
				if (mfm)
				{
					d86f[drive].calc_crc.word = 0xffff;
				}
				break;
			case BYTE_IDAM:
			case BYTE_DATAAM:
				if (!mfm)
				{
					d86f[drive].calc_crc.word = 0xffff;
				}
				break;
			case BYTE_GAP2:
				d86f[drive].id_match = 0;
				d86f[drive].data_am_counter = 0;
				if (d86f[drive].id_counter != 4)
				{
					d86f[drive].id_am_counter = d86f[drive].id_counter = 0;
					break;
				}
				d86f[drive].id_am_counter = d86f[drive].id_counter = 0;
				if ((d86f[drive].req_sector.dword == d86f[drive].last_sector.dword) || (d86f[drive].state == STATE_READ_FIND_ADDRESS))
				{
					if ((d86f[drive].track_crc.word != d86f[drive].calc_crc.word) && d86f_handler[drive].check_crc)
					{
						if (d86f[drive].state != STATE_READ_FIND_ADDRESS)
						{
							// pclog("d86f_poll(): Header CRC error (mfm=%i) (%i %i %i %i) (%04X %04X)\n", d86f_is_mfm(drive), d86f[drive].req_sector.id.c, d86f[drive].req_sector.id.h, d86f[drive].req_sector.id.r, d86f[drive].req_sector.id.n, d86f[drive].track_crc.word, d86f[drive].calc_crc.word);
							fdc_finishread();
							fdc_headercrcerror();
							d86f[drive].state = STATE_IDLE;
							d86f[drive].index_count = 0;
							return;
						}
						else
						{
							// pclog("d86f_poll(): Header CRC error at read sector ID (mfm=%i) (%i %i %i %i) (%04X %04X)\n", d86f_is_mfm(drive), d86f[drive].req_sector.id.c, d86f[drive].req_sector.id.h, d86f[drive].req_sector.id.r, d86f[drive].req_sector.id.n, d86f[drive].track_crc.word, d86f[drive].calc_crc.word);
						}
					}
					else
					{
						if (d86f[drive].state != STATE_READ_FIND_ADDRESS)  d86f[drive].id_match = 1;

						// pclog("Read sector ID in find state: %i %i %i %i (sought: %i, %i, %i, %i)\n", d86f[drive].last_sector.id.c, d86f[drive].last_sector.id.h, d86f[drive].last_sector.id.r, d86f[drive].last_sector.id.n, d86f[drive].req_sector.id.c, d86f[drive].req_sector.id.h, d86f[drive].req_sector.id.r, d86f[drive].req_sector.id.n);

						if (d86f[drive].state == STATE_READ_FIND_ADDRESS)
						{
							// pclog("Reading sector ID (%i %i %i %i)...\n", d86f[drive].last_sector.id.c, d86f[drive].last_sector.id.h, d86f[drive].last_sector.id.r, d86f[drive].last_sector.id.n);
							fdc_sectorid(d86f[drive].last_sector.id.c, d86f[drive].last_sector.id.h, d86f[drive].last_sector.id.r, d86f[drive].last_sector.id.n, 0, 0);
							d86f[drive].state = STATE_IDLE;
							d86f[drive].index_count = 0;
						}
						else
						{
							d86f_handler[drive].set_sector(drive, side, d86f[drive].last_sector.id.c, d86f[drive].last_sector.id.h, d86f[drive].last_sector.id.r, d86f[drive].last_sector.id.n);
						}
					}
				}					
				break;
			case BYTE_DATA:
				d86f[drive].datac = 0;
				switch (d86f[drive].state)
				{
					case STATE_READ_FIND_SECTOR:
					case STATE_WRITE_FIND_SECTOR:
					case STATE_READ_FIND_FIRST_SECTOR:
					case STATE_READ_FIND_NEXT_SECTOR:
						/* If the data address mark counter is anything other than 0, then either the ID matches or the FDC is in the ignore sector ID mode (ie. the READ TRACK command).
						   Also, the bitcell period, etc. are already checked during the wait state which is designed to never end in case of a mismatch.
						   Therefore, ensuring the data address mark acounter is at a correct length is all we need to do. */
						if (d86f[drive].data_am_counter == am_len)
						{
							d86f[drive].state++;
						}
						/* Data address mark counter always reset to 0. */
						d86f[drive].data_am_counter = 0;
						break;
				}
				break;
		}
	}
}

void d86f_poll_find_format(int drive, int side)
{
	d86f_poll_advancebyte(drive, side);

	if (d86f[drive].track_index)
	{
		// pclog("Index hole hit, formatting track...\n");
		d86f[drive].state = STATE_FORMAT;
		return;
	}
}

void d86f_poll_format(int drive, int side)
{
        int data;

	switch(d86f[drive].track_byte)
	{
		case BYTE_GAP0:
		case BYTE_GAP1:
		case BYTE_GAP2:
		case BYTE_GAP3:
			if (!disable_write)
			{
				d86f_write_byte(drive, side, fdc_is_mfm() ? 0x4E : 0xFF);
			}
			break;
		case BYTE_IAM_SYNC:
			if (!disable_write)
			{
				d86f_write_byte(drive, side, 0xC2);
			}
			break;
		case BYTE_IAM:
			if (!disable_write)
			{
				d86f_write_byte(drive, side, 0xFC);
			}
			break;
		case BYTE_ID_SYNC:
			d86f[drive].id_pos = d86f_get_pos(drive);

			if (d86f[drive].id_pos <= 3)
			{
	               		data = fdc_getdata(0);
       		        	if ((data == -1) && (d86f[drive].id_pos < 3))
				{
					data = 0;
				}
				if (d86f[drive].version != 0063)
				{
					d86f[drive].format_sector_id.byte_array[d86f[drive].id_pos] = data & 0xff;
				}
				// pclog("format_sector_id[%i] = %i\n", cur_id_pos, d86f[drive].format_sector_id.byte_array[d86f[drive].id_pos]);
       	        		if (d86f[drive].id_pos == 3)
				{
					fdc_stop_id_request();
					if (d86f[drive].version != 0063)
					{
						d86f_handler[drive].set_sector(drive, side, d86f[drive].format_sector_id.id.c, d86f[drive].format_sector_id.id.h, d86f[drive].format_sector_id.id.r, d86f[drive].format_sector_id.id.n);
					}
					// pclog("Formatting sector: %08X...\n", d86f[drive].format_sector_id.dword);
				}
			}

		case BYTE_I_SYNC:
		case BYTE_DATA_SYNC:
			if (!disable_write)
			{
				d86f_write_byte(drive, side, 0);
			}
			break;
		case BYTE_IDAM_SYNC:
		case BYTE_DATAAM_SYNC:
			if (!disable_write)
			{
				d86f_write_byte(drive, side, 0xA1);
			}
			if (d86f_is_mfm(drive))
			{
				d86f_calccrc(drive, d86f[drive].track_data_byte);
			}
			break;
		case BYTE_IDAM:
		case BYTE_DATAAM:
			// pclog("CRC reset: %02X\n", d86f[drive].track_byte);

			if (!disable_write)
			{
				d86f_write_byte(drive, side, (d86f[drive].track_byte == BYTE_IDAM) ? 0xFE : 0xFB);
			}
			d86f_calccrc(drive, d86f[drive].track_data_byte);
			break;
		case BYTE_ID:
			d86f[drive].id_pos = d86f_get_pos(drive);
			if (d86f[drive].version != 0063)
			{
				if (!disable_write)
				{
					d86f_write_byte(drive, side, d86f[drive].format_sector_id.byte_array[d86f[drive].id_pos]);
				}
			}
			else
			{
				d86f[drive].format_sector_id.byte_array[d86f[drive].id_pos] = d86f[drive].track_data_byte;
				if (d86f[drive].id_pos == 3)
				{
					d86f_handler[drive].set_sector(drive, side, d86f[drive].format_sector_id.id.c, d86f[drive].format_sector_id.id.h, d86f[drive].format_sector_id.id.r, d86f[drive].format_sector_id.id.n);
				}
			}
			d86f_calccrc(drive, d86f[drive].track_data_byte);
			break;
		case BYTE_DATA:
			d86f[drive].id_pos = d86f_get_pos(drive);
			if (!disable_write)
			{
				d86f_handler[drive].write_data(drive, side, d86f[drive].id_pos, d86f[drive].fill);
			}
			d86f_calccrc(drive, d86f[drive].fill);
			break;
		case BYTE_ID_CRC:
		case BYTE_DATA_CRC:
			d86f_poll_write_crc(drive, side);
			break;
	}

	d86f_poll_advancebyte(drive, side);

	if (d86f[drive].track_index)
	{
		// pclog("Track position %08X\n", d86f[drive].track_pos);
		// pclog("Index hole hit again, format finished\n");
		d86f[drive].state = STATE_IDLE;
		if (!disable_write)  d86f_handler[drive].writeback(drive);
		fdc_sector_finishread(drive);
		d86f[drive].index_count = 0;
		return;
	}

	if (d86f[drive].track_byte != d86f[drive].old_track_byte)
	{
		// pclog("Track byte: %02X, old: %02X\n", track_byte, old_track_byte);
		d86f[drive].section_pos = d86f[drive].track_pos;

		switch(d86f[drive].track_byte & ~BYTE_INDEX_HOLE)
		{
			case BYTE_IDAM:
			case BYTE_DATAAM:
				if (!d86f_is_mfm(drive))
				{
					d86f[drive].calc_crc.word = 0xffff;
				}
				break;
			case BYTE_IDAM_SYNC:
			case BYTE_DATAAM_SYNC:
				if (d86f_is_mfm(drive))
				{
					d86f[drive].calc_crc.word = 0xffff;
				}
				break;
			case BYTE_ID_SYNC:
				// pclog("Requesting next sector ID...\n");
				fdc_request_next_sector_id();
				break;
 			case BYTE_GAP2:
				d86f[drive].last_sector.dword = d86f[drive].format_sector_id.dword;
				break;
			case BYTE_DATA:
				d86f[drive].datac = 0;
				break;
		}
	}
}

void d86f_poll()
{
	// int drive = d86f_drive;
	int drive = 0;
	int side = 0;
	drive = fdc_get_drive();
	side = fdd_get_head(drive);

	if (d86f[drive].state == STATE_FORMAT)
	{
		d86f_poll_format(drive, side);
		return;
	}

	if (d86f[drive].state == STATE_FORMAT_FIND)
	{
		d86f_poll_find_format(drive, side);
		return;
	}

	if (d86f_find_state_nf(drive))
	{
		if (d86f[drive].wait_state)
		{
			d86f_poll_find_nf_wait(drive, side);
		}
		else
		{
			d86f_poll_find_nf(drive, side);
		}
		return;
	}

	if (d86f_read_state(drive) || d86f_write_state(drive))
	{
		d86f_poll_readwrite(drive, side);
		return;
	}

	if ((d86f[drive].state == STATE_SEEK) || (d86f[drive].state == STATE_IDLE))
	{
		d86f_poll_advancebyte(drive, side);
		return;
	}
}
