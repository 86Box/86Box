/* Copyright holders: Tenshi
   see COPYING for more details
*/
#include "ibm.h"
#include "disc.h"
#include "disc_86f.h"
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
        
typedef union {
	uint16_t word;
	uint8_t bytes[2];
} crc_t;

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
	uint8_t disk_flags;
        uint8_t track_data[2][25000];
        uint8_t track_layout[2][25000];
        uint8_t track_flags;
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
	uint16_t track_pos;
	uint16_t datac;
	uint16_t id_pos;
	uint16_t section_pos;
	crc_t calc_crc;
	crc_t track_crc;
	uint8_t track_byte;
	uint8_t track_index;
	uint8_t old_track_byte;
	uint8_t old_track_index;
	uint8_t cur_track;
} d86f[2];

/* Needed for formatting! */
int d86f_is_40_track(int drive)
{
	return (d86f[drive].disk_flags & 1) ? 0 : 1;
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

static void d86f_calccrc(int drive, uint8_t byte)
{
	d86f[drive].calc_crc.word = (d86f[drive].calc_crc.word << 8) ^ CRCTable[(d86f[drive].calc_crc.word >> 8)^byte];
}

void d86f_init()
{
        memset(d86f, 0, sizeof(d86f));
        d86f_setupcrc(0x1021, 0xcdb4);
}

void d86f_load(int drive, char *fn)
{
	uint32_t magic = 0;
	uint32_t len = 0;
	uint16_t version = 0;

	int i = 0;
	int j = 0;

	writeprot[drive] = 0;
        d86f[drive].f = fopen(fn, "rb+");
        if (!d86f[drive].f)
        {
                d86f[drive].f = fopen(fn, "rb");
                if (!d86f[drive].f)
                        return;
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

	fread(&version, 2, 1, d86f[drive].f);

	if (version != 0x0100)
	{
		/* File is not of a recognized format version abort. */
		pclog("86F: Unrecognized file version: %04X\n", version);
		fclose(d86f[drive].f);
		return;
	}

	fread(&(d86f[drive].disk_flags), 1, 1, d86f[drive].f);

	if (((d86f[drive].disk_flags >> 1) & 3) == 3)
	{
		/* Invalid disk hole. */
		pclog("86F: Unrecognized disk hole type 3\n");
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
	fseek(d86f[drive].f, d86f[drive].track_offset[0], SEEK_SET);
	fread(&(d86f[drive].track_flags), 1, 1, d86f[drive].f);

	fseek(d86f[drive].f, 0, SEEK_END);
	d86f[drive].file_size = ftell(d86f[drive].f);

	fseek(d86f[drive].f, 0, SEEK_SET);

        drives[drive].seek        = d86f_seek;
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

int d86f_hole(int drive)
{
	return (d86f[drive].disk_flags >> 1) & 3;
}

uint8_t d86f_track_flags(int drive)
{
	uint8_t tf = d86f[drive].track_flags;
	uint8_t rr = tf & 0x27;
	uint8_t dr = fdd_get_type(drive) & 7;
	tf &= ~0x27;

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

int d86f_get_sides(int drive)
{
	return (d86f[drive].disk_flags & 8) ? 2 : 1;
}

int d86f_is_mfm(int drive)
{
	return (d86f[drive].track_flags & 8) ? 1 : 0;
}

uint16_t d86f_get_raw_size(int drive)
{
	double rate = 0.0;
	int mfm = d86f_is_mfm(drive);
	double rpm = (d86f_track_flags(drive) & 0x20) ? 360.0 : 300.0;
	double size = 6250.0;
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
	}
	if (!mfm)  rate /= 2.0;
	size = (size / 250.0) * rate;
	size = (size * 300.0) / rpm;
	return (uint16_t) size;
}

void d86f_seek(int drive, int track)
{
	int sides = d86f_get_sides(drive);
        int side;

        if (d86f_is_40_track(drive) && fdd_doublestep_40(drive))
                track /= 2;

	for (side = 0; side < d86f_get_sides(drive); side++)
	{
		memset(d86f[drive].track_layout[side], BYTE_GAP4, 25000);
		memset(d86f[drive].track_data[side], 0xFF, 25000);
	}

	d86f[drive].cur_track = track;

	if (!(d86f[drive].track_offset[track]))
	{
		/* Track does not exist in the image, initialize it as unformatted. */
		d86f[drive].track_in_file = 0;
		d86f[drive].track_flags = 0x0A;	/* 300 rpm, MFM, 250 kbps */
		return;
	}

	d86f[drive].track_in_file = 1;

	fseek(d86f[drive].f, d86f[drive].track_offset[track], SEEK_SET);

	fread(&(d86f[drive].track_flags), 1, 1, d86f[drive].f);

	for (side = 0; side < d86f_get_sides(drive); side++)
	{
		fseek(d86f[drive].f, d86f[drive].track_offset[track] + (side * 50000) + 2, SEEK_SET);
		fread(d86f[drive].track_layout[side], 1, d86f_get_raw_size(drive), d86f[drive].f);
		fseek(d86f[drive].f, d86f[drive].track_offset[track] + (side * 50000) + 25002, SEEK_SET);
		fread(d86f[drive].track_data[side], 1, d86f_get_raw_size(drive), d86f[drive].f);
	}
}

void d86f_writeback(int drive)
{
	int track = d86f[drive].cur_track;
	int side;

        if (!d86f[drive].f)
                return;
                
        if (!d86f[drive].track_in_file)
                return; /*Should never happen*/

	fseek(d86f[drive].f, 7, SEEK_SET);
	fwrite(d86f[drive].track_offset, 1, 1024, d86f[drive].f);

	fseek(d86f[drive].f, d86f[drive].track_offset[track], SEEK_SET);
	fwrite(&(d86f[drive].track_flags), 1, 1, d86f[drive].f);

	for (side = 0; side < d86f_get_sides(drive); side++)
	{
		fseek(d86f[drive].f, d86f[drive].track_offset[track] + (side * 50000) + 2, SEEK_SET);
		fwrite(d86f[drive].track_layout[side], 1, d86f_get_raw_size(drive), d86f[drive].f);
		fseek(d86f[drive].f, d86f[drive].track_offset[track] + (side * 50000) + 25002, SEEK_SET);
		fwrite(d86f[drive].track_data[side], 1, d86f_get_raw_size(drive), d86f[drive].f);
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
	int mfm = (d86f_track_flags(drive) & 8) ? 1 : 0;
	double rpm = (d86f_track_flags(drive) & 0x20) ? 360.0 : 300.0;
	double size = 8000.0;
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
	}
	if (!mfm)  rate /= 2.0;
	size = (size * 250.0) / rate;
	size = (size * 300.0) / rpm;
	size = (size * fdd_getrpm(drive)) / 300.0;
	return (int) size;
}

void d86f_readsector(int drive, int sector, int track, int side, int rate, int sector_size)
{
        // pclog("d86f_readsector: fdc_period=%i img_period=%i rate=%i sector=%i track=%i side=%i\n", fdc_get_bitcell_period(), d86f_get_bitcell_period(drive), rate, sector, track, side);

        d86f[drive].req_sector.id.c = track;
        d86f[drive].req_sector.id.h = side;

	if (side && (d86f_get_sides(drive) == 1))
	{
		fdc_notfound();
		d86f[drive].state = STATE_IDLE;
		d86f[drive].index_count = 0;
		return;
	}

        d86f_drive = drive;
        d86f[drive].req_sector.id.r = sector;
	d86f[drive].req_sector.id.n = sector_size;
	d86f[drive].index_count = 0;
        if (sector == SECTOR_FIRST)
                d86f[drive].state = STATE_READ_FIND_FIRST_SECTOR;
        else if (sector == SECTOR_NEXT)
                d86f[drive].state = STATE_READ_FIND_NEXT_SECTOR;
        else
                d86f[drive].state = STATE_READ_FIND_SECTOR;
}

void d86f_writesector(int drive, int sector, int track, int side, int rate, int sector_size)
{
        d86f[drive].req_sector.id.c = track;
        d86f[drive].req_sector.id.h = side;
        d86f[drive].req_sector.id.r = sector;
	d86f[drive].req_sector.id.n = sector_size;

        // pclog("d86f_writesector: drive=%c: fdc_period=%i img_period=%i rate=%i chrn=%08X\n", drive + 0x41, fdc_get_bitcell_period(), d86f_get_bitcell_period(drive), rate, d86f[drive].req_sector.dword);

	if (side && (d86f_get_sides(drive) == 1))
	{
		fdc_notfound();
		d86f[drive].state = STATE_IDLE;
		d86f[drive].index_count = 0;
		return;
	}

        d86f_drive = drive;
	d86f[drive].index_count = 0;
        d86f[drive].state = STATE_WRITE_FIND_SECTOR;
}

void d86f_readaddress(int drive, int track, int side, int rate)
{
        // pclog("d86f_readaddress: fdc_period=%i img_period=%i rate=%i track=%i side=%i\n", fdc_get_bitcell_period(), d86f_get_bitcell_period(drive), rate, track, side);

        d86f[drive].req_sector.id.c = track;
        d86f[drive].req_sector.id.h = side;

	if (side && (d86f_get_sides(drive) == 1))
	{
		fdc_notfound();
		d86f[drive].state = STATE_IDLE;
		d86f[drive].index_count = 0;
		return;
	}

        d86f_drive = drive;
	d86f[drive].index_count = 0;
        d86f[drive].state = STATE_READ_FIND_ADDRESS;
}

void d86f_prepare_track_layout(int drive, int side)
{
	int i = 0;
	int j = 0;
	int real_gap0_len = d86f_is_mfm(drive) ? 80 : 40;
	int sync_len = d86f_is_mfm(drive) ? 12 : 6;
	int am_len = d86f_is_mfm(drive) ? 4 : 1;
	int real_gap1_len = d86f_is_mfm(drive) ? 50 : 26;
	int real_gap2_len = (fdc_get_bit_rate() >= 1000) ? 41 : 22;
	int real_gap3_len = fdc_get_gap();

	memset(d86f[drive].track_layout[side], BYTE_GAP4, d86f_get_raw_size(drive));
	i = 0;

	memset(d86f[drive].track_layout[side] + i, BYTE_GAP0, real_gap0_len);
	i += real_gap0_len;
	memset(d86f[drive].track_layout[side] + i, BYTE_I_SYNC, sync_len);
	i += sync_len;
	if (d86f_is_mfm(drive))
	{
		memset(d86f[drive].track_layout[side] + i, BYTE_IAM_SYNC, 3);
		i += 3;
	}
	memset(d86f[drive].track_layout[side] + i, BYTE_IAM, 1);
	i++;
	memset(d86f[drive].track_layout[side] + i, BYTE_GAP1, real_gap1_len);
	i += real_gap1_len;

	d86f[drive].track_layout[side][0] |= BYTE_INDEX_HOLE;
	for (j = 0; j < fdc_get_format_sectors(); j++)
	{
		// pclog("Sector %i (%i)\n", j, s->n);
		memset(d86f[drive].track_layout[side] + i, BYTE_ID_SYNC, sync_len);
		i += sync_len;
		if (d86f_is_mfm(drive))
		{
			memset(d86f[drive].track_layout[side] + i, BYTE_IDAM_SYNC, 3);
			i += 3;
		}
		memset(d86f[drive].track_layout[side] + i, BYTE_IDAM, 1);
		i++;
		memset(d86f[drive].track_layout[side] + i, BYTE_ID, 4);
		i += 4;
		memset(d86f[drive].track_layout[side] + i, BYTE_ID_CRC, 2);
		i += 2;
		memset(d86f[drive].track_layout[side] + i, BYTE_GAP2, real_gap2_len);
		i += real_gap2_len;
		memset(d86f[drive].track_layout[side] + i, BYTE_DATA_SYNC, sync_len);
		i += sync_len;
		if (d86f_is_mfm(drive))
		{
			memset(d86f[drive].track_layout[side] + i, BYTE_DATAAM_SYNC, 3);
			i += 3;
		}
		memset(d86f[drive].track_layout[side] + i, BYTE_DATAAM, 1);
		i++;
		memset(d86f[drive].track_layout[side] + i, BYTE_DATA, (128 << fdc_get_format_n()));
		i += (128 << fdc_get_format_n());
		memset(d86f[drive].track_layout[side] + i, BYTE_DATA_CRC, 2);
		i += 2;
		memset(d86f[drive].track_layout[side] + i, BYTE_GAP3, real_gap3_len);
		i += real_gap3_len;
	}
}

void d86f_format(int drive, int track, int side, int rate, uint8_t fill)
{
        d86f[drive].req_sector.id.c = d86f[drive].cur_track;
        d86f[drive].req_sector.id.h  = side;

	if ((side && (d86f_get_sides(drive) == 1)) || !d86f_valid_bit_rate(drive))
	{
		fdc_notfound();
		d86f[drive].state = STATE_IDLE;
		d86f[drive].index_count = 0;
		return;
	}

	if ((d86f[drive].cur_track < 0) || (d86f[drive].cur_track > 256))
	{
		fdc_writeprotect();
		d86f[drive].state = STATE_IDLE;
		d86f[drive].index_count = 0;
		return;
	}

        d86f_drive = drive;
        d86f[drive].fill  = fill;
	d86f[drive].index_count = 0;
	/* Let's prepare the track space and layout before filling. */
	d86f[drive].track_flags &= 0xc0;
	d86f[drive].track_flags |= (fdd_getrpm(drive ^ fdd_swap) == 360) ? 0x20 : 0;
	d86f[drive].track_flags |= fdc_get_bit_rate();
	d86f[drive].track_flags |= fdc_is_mfm() ? 8 : 0;
	memset(d86f[drive].track_data[side], 0xFF, 25000);
	d86f_prepare_track_layout(drive, side);

	if (!d86f[drive].track_in_file)
	{
		/* Track is absent from the file, let's add it. */
		d86f[drive].track_offset[d86f[drive].cur_track] = d86f[drive].file_size;
		d86f[drive].file_size += 50002;
		if (d86f_get_sides(drive) == 2)
		{
			d86f[drive].file_size += 50000;
		}
		d86f[drive].track_in_file = 1;
	}

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

int d86f_match(int drive)
{
	int temp;
	temp = (d86f[drive].req_sector.dword == d86f[drive].last_sector.dword);
	return temp;
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

int d86f_can_read_address(int drive)
{
	int temp;
	temp = (fdc_get_bitcell_period() == d86f_get_bitcell_period(drive));
	temp = temp && fdd_can_read_medium(drive ^ fdd_swap);
	temp = temp && (fdc_is_mfm() == d86f_is_mfm(drive));
	return temp;
}

int d86f_valid_bit_rate(int drive)
{
	int rate = fdc_get_bit_rate();
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

int d86f_can_format(int drive)
{
	int temp;
	temp = !writeprot[drive];
	temp = temp && !swwp;
	temp = temp && fdd_can_read_medium(drive ^ fdd_swap);
	temp = temp & d86f_valid_bit_rate(drive);
	return temp;
}

int d86f_find_state_nf(int drive)
{
	int temp;
	temp = (d86f[drive].state == STATE_READ_FIND_SECTOR);
	temp = temp || (d86f[drive].state == STATE_READ_FIND_FIRST_SECTOR);
	temp = temp || (d86f[drive].state == STATE_READ_FIND_NEXT_SECTOR);
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

void d86f_poll_write_crc(int drive, int side)
{
	if (d86f[drive].state != STATE_FORMAT)  return;
	d86f[drive].id_pos = d86f[drive].track_pos - d86f[drive].section_pos;
	if (d86f[drive].id_pos)
	{
		d86f[drive].track_data[side][d86f[drive].track_pos] = d86f[drive].calc_crc.bytes[1];
	}
	else
	{
		d86f[drive].track_data[side][d86f[drive].track_pos] = d86f[drive].calc_crc.bytes[0];
	}
}

void d86f_poll_advancebyte(int drive, int side)
{
	d86f[drive].old_track_byte = d86f[drive].track_byte;
	d86f[drive].old_track_index = d86f[drive].track_index;

	d86f[drive].track_pos++;
	d86f[drive].track_pos %= d86f_get_raw_size(drive);

	d86f[drive].track_byte = d86f[drive].track_layout[side][d86f[drive].track_pos] & ~BYTE_INDEX_HOLE;
	d86f[drive].track_index = d86f[drive].track_layout[side][d86f[drive].track_pos] & BYTE_INDEX_HOLE;
}

void d86f_poll_reset(int drive, int side)
{
	d86f[drive].state = STATE_IDLE;
	d86f[drive].index_count = 0;
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

void d86f_poll_finish(int drive, int side)
{
	d86f_poll_reset(drive, side);
	d86f_poll_advancebyte(drive, side);
	d86f[drive].last_sector.dword = 0xFFFFFFFF;
}

void d86f_poll_readwrite(int drive, int side)
{
        int data;
	uint16_t max_len;

	if (d86f_read_state_data(drive))
	{
		max_len = d86f_data_size(drive);
		if (d86f[drive].datac < d86f_get_data_len(drive))
		{
			fdc_data(d86f[drive].track_data[side][d86f[drive].track_pos]);
		}
		d86f_calccrc(drive, d86f[drive].track_data[side][d86f[drive].track_pos]);
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
			d86f[drive].track_data[side][d86f[drive].track_pos] = data;
			d86f[drive].track_layout[side][d86f[drive].track_pos] = BYTE_DATA;
			if (!d86f[drive].track_pos)  d86f[drive].track_layout[side][d86f[drive].track_pos] |= BYTE_INDEX_HOLE;
		}
		d86f_calccrc(drive, d86f[drive].track_data[side][d86f[drive].track_pos]);
	}
	else if (d86f_read_state_crc(drive))
	{
		max_len = 2;
		d86f[drive].track_crc.bytes[d86f[drive].datac] = d86f[drive].track_data[side][d86f[drive].track_pos];
	}
	else if (d86f[drive].state == STATE_WRITE_SECTOR_CRC)
	{
		max_len = 2;
		if (!disable_write)
		{
			d86f[drive].track_data[side][d86f[drive].track_pos] = d86f[drive].calc_crc.bytes[d86f[drive].datac];
			d86f[drive].track_layout[side][d86f[drive].track_pos] = BYTE_DATA_CRC;
			if (!d86f[drive].track_pos)  d86f[drive].track_layout[side][d86f[drive].track_pos] |= BYTE_INDEX_HOLE;
		}
	}
	else if (d86f_read_state_gap3(drive))
	{
		max_len = fdc_get_gap();
		if (d86f[drive].datac == (fdc_get_gap() - 1))
		{
			d86f_poll_finish(drive, side);
			if (d86f[drive].track_crc.word != d86f[drive].calc_crc.word)
			{
				fdc_finishread();
				fdc_datacrcerror();
			}
			else
			{
        	               	fdc_sector_finishread();
			}
			return;
		}
	}
	else if (d86f[drive].state == STATE_WRITE_SECTOR_GAP3)
	{
		max_len = fdc_get_gap();
		if (d86f[drive].datac == (fdc_get_gap() - 1))
		{
    		        if (!disable_write)  d86f_writeback(drive);
			d86f_poll_finish(drive, side);
       	        	fdc_sector_finishread(drive);
			return;
		}
		else
		{
			if (!disable_write)
			{
				d86f[drive].track_data[side][d86f[drive].track_pos] = fdc_is_mfm() ? 0x4E : 0xFF;
				d86f[drive].track_layout[side][d86f[drive].track_pos] = BYTE_GAP3;
				if (!d86f[drive].track_pos)  d86f[drive].track_layout[side][d86f[drive].track_pos] |= BYTE_INDEX_HOLE;
			}
		}
	}

	d86f[drive].datac++;
	d86f_poll_advancebyte(drive, side);

	if (d86f[drive].datac >= max_len)
	{
		d86f[drive].datac = 0;
		d86f[drive].state++;
	}
}

void d86f_poll_find_nf(int drive, int side)
{
	if (d86f[drive].track_index)
	{
		// pclog("d86f_poll_find_nf(): Index pulse\n");
		index_pulse(drive);
		d86f[drive].index_count++;
	}

	switch(d86f[drive].track_byte)
	{
		case BYTE_IDAM:
			d86f[drive].calc_crc.word = fdc_is_mfm() ? 0xcdb4 : 0xffff;
			// pclog("CRC reset: %02X\n", d86f[drive].track_byte);
			d86f_calccrc(drive, d86f[drive].track_data[side][d86f[drive].track_pos]);
			break;

		case BYTE_DATAAM:
			d86f[drive].calc_crc.word = fdc_is_mfm() ? 0xcdb4 : 0xffff;
			// pclog("CRC reset: %02X\n", d86f[drive].track_byte);

			if ((d86f[drive].state &= STATE_WRITE_FIND_SECTOR) && d86f_match(drive) && d86f_can_read_address(drive))
			{
				d86f[drive].track_data[side][d86f[drive].track_pos] = 0xFB;
			}
			d86f_calccrc(drive, d86f[drive].track_data[side][d86f[drive].track_pos]);
			break;
		case BYTE_ID:
			d86f[drive].id_pos = d86f[drive].track_pos - d86f[drive].section_pos;
			d86f[drive].rw_sector_id.byte_array[d86f[drive].id_pos] = d86f[drive].track_data[side][d86f[drive].track_pos];
			d86f_calccrc(drive, d86f[drive].track_data[side][d86f[drive].track_pos]);
			break;
		case BYTE_ID_CRC:
			d86f[drive].id_pos = d86f[drive].track_pos - d86f[drive].section_pos;
			d86f[drive].track_crc.bytes[d86f[drive].id_pos] = d86f[drive].track_data[side][d86f[drive].track_pos];
			break;
	}

	d86f_poll_advancebyte(drive, side);

	if (d86f_poll_check_notfound(drive))  return;

	if (d86f[drive].track_byte != d86f[drive].old_track_byte)
	{
		d86f[drive].section_pos = d86f[drive].track_pos;

		switch(d86f[drive].track_byte)
		{
			case BYTE_GAP2:
				if (d86f_can_read_address(drive))
				{
					if (d86f_match(drive) || (d86f[drive].state == STATE_READ_FIND_ADDRESS))
					{
						if (d86f[drive].track_crc.word != d86f[drive].calc_crc.word)
						{
							// pclog("d86f_poll(): Header CRC error (%i %i %i %i)\n", d86f[drive].req_sector.id.c, d86f[drive].req_sector.id.h, d86f[drive].req_sector.id.r, d86f[drive].req_sector.id.n);
							fdc_finishread();
							fdc_headercrcerror();
							d86f[drive].state = STATE_IDLE;
							d86f[drive].index_count = 0;
							return;
						}
					}
				}

				d86f[drive].last_sector.dword = d86f[drive].rw_sector_id.dword;
				// pclog("Read sector ID in find state: %i %i %i %i (sought: %i, %i, %i, %i)\n", d86f[drive].last_sector.id.c, d86f[drive].last_sector.id.h, d86f[drive].last_sector.id.r, d86f[drive].last_sector.id.n, d86f[drive].req_sector.id.c, d86f[drive].req_sector.id.h, d86f[drive].req_sector.id.r, d86f[drive].req_sector.id.n);

				if ((d86f[drive].state == STATE_READ_FIND_ADDRESS) && d86f_can_read_address(drive))
				{
					// pclog("Reading sector ID...\n");
					fdc_sectorid(d86f[drive].last_sector.id.c, d86f[drive].last_sector.id.h, d86f[drive].last_sector.id.r, d86f[drive].last_sector.id.n, 0, 0);
					d86f[drive].state = STATE_IDLE;
					d86f[drive].index_count = 0;
				}
				break;
			case BYTE_DATA:
				d86f[drive].datac = 0;
				switch (d86f[drive].state)
				{
					case STATE_READ_FIND_SECTOR:
					case STATE_WRITE_FIND_SECTOR:
						if (d86f_match(drive) && d86f_can_read_address(drive))
						{
							d86f[drive].state++;
						}
						break;
					case STATE_READ_FIND_FIRST_SECTOR:
					case STATE_READ_FIND_NEXT_SECTOR:
						if (d86f_can_read_address(drive))
						{
							d86f[drive].state++;
						}
						break;
				}
				break;
		}
	}
}

void d86f_poll_find_format(int drive, int side)
{
	if (!(d86f_can_format(drive)))
	{
		if (d86f_can_read_address(drive))
		{
			// pclog("d86f_poll(): Disk is write protected or attempting to format wrong number of sectors per track\n");
			fdc_writeprotect();
		}
		else
		{
			// pclog("d86f_poll(): Unable to format at the requested density or bitcell period\n");
			fdc_notfound();
		}
		d86f_poll_reset(drive, side);
		d86f_poll_advancebyte(drive, side);
		return;
	}

	if (d86f[drive].track_index && d86f_can_read_address(drive))
	{
		// pclog("Index hole hit, formatting track...\n");
		d86f[drive].state = STATE_FORMAT;
		d86f_poll_advancebyte(drive, side);
		return;
	}

	d86f_poll_advancebyte(drive, side);

	if (d86f_poll_check_notfound(drive))  return;
}

void d86f_poll_format(int drive, int side)
{
        int data;

	if (d86f[drive].track_index)
	{
		// pclog("Index hole hit again, format finished\n");
		d86f[drive].state = STATE_IDLE;
		if (!disable_write)  d86f_writeback(drive);
		fdc_sector_finishread(drive);
		d86f[drive].index_count = 0;
		d86f_poll_advancebyte(drive, side);
		return;
	}

	switch(d86f[drive].track_byte)
	{
		case BYTE_GAP0:
		case BYTE_GAP1:
		case BYTE_GAP2:
		case BYTE_GAP3:
		case BYTE_GAP4:
			if (!disable_write)  d86f[drive].track_data[side][d86f[drive].track_pos] = fdc_is_mfm() ? 0x4E : 0xFF;
			break;
		case BYTE_IAM_SYNC:
			if (!disable_write)  d86f[drive].track_data[side][d86f[drive].track_pos] = 0xC2;
			break;
		case BYTE_IAM:
			if (!disable_write)  d86f[drive].track_data[side][d86f[drive].track_pos] = 0xFC;
			break;
		case BYTE_ID_SYNC:
			d86f[drive].id_pos = d86f[drive].track_pos - d86f[drive].section_pos;
			if (!disable_write)  d86f[drive].track_data[side][d86f[drive].track_pos] = 0;
			if (d86f[drive].id_pos > 3)  break;
               		data = fdc_getdata(0);
       	        	if ((data == -1) && (d86f[drive].id_pos < 3))
			{
				data = 0;
			}
			d86f[drive].format_sector_id.byte_array[d86f[drive].id_pos] = data & 0xff;
			d86f_calccrc(drive, d86f[drive].format_sector_id.byte_array[d86f[drive].id_pos]);
			// pclog("format_sector_id[%i] = %i\n", cur_id_pos, d86f[drive].format_sector_id.byte_array[d86f[drive].id_pos]);
       	        	if (d86f[drive].id_pos == 3)
			{
				fdc_stop_id_request();
				// pclog("Formatting sector: %08X...\n", d86f[drive].format_sector_id.dword);
			}
			break;
		case BYTE_I_SYNC:
		case BYTE_DATA_SYNC:
			if (!disable_write)  d86f[drive].track_data[side][d86f[drive].track_pos] = 0;
			break;
		case BYTE_IDAM_SYNC:
		case BYTE_DATAAM_SYNC:
			if (!disable_write)  d86f[drive].track_data[side][d86f[drive].track_pos] = 0xA1;
			break;
		case BYTE_IDAM:
		case BYTE_DATAAM:
			d86f[drive].calc_crc.word = fdc_is_mfm() ? 0xcdb4 : 0xffff;
			// pclog("CRC reset: %02X\n", d86f[drive].track_byte);

			if (!disable_write)  d86f[drive].track_data[side][d86f[drive].track_pos] = (d86f[drive].track_byte == BYTE_IDAM) ? 0xFE : 0xFB;
			d86f_calccrc(drive, d86f[drive].track_data[side][d86f[drive].track_pos]);
			break;
		case BYTE_ID:
			d86f[drive].id_pos = d86f[drive].track_pos - d86f[drive].section_pos;
			if (!disable_write)
			{
				d86f[drive].track_data[side][d86f[drive].track_pos] = d86f[drive].format_sector_id.byte_array[d86f[drive].id_pos];
			}
			d86f_calccrc(drive, d86f[drive].track_data[side][d86f[drive].track_pos]);
			break;
		case BYTE_DATA:
			if (!disable_write)
			{
				d86f[drive].track_data[side][d86f[drive].track_pos] = d86f[drive].fill;
				d86f_calccrc(drive, d86f[drive].fill);
			}
			break;
		case BYTE_ID_CRC:
			d86f_poll_write_crc(drive, side);
			break;
		case BYTE_DATA_CRC:
			d86f[drive].id_pos = d86f[drive].track_pos - d86f[drive].section_pos;
			d86f_poll_write_crc(drive, side);
			break;
	}

	d86f_poll_advancebyte(drive, side);

	if (d86f[drive].track_byte != d86f[drive].old_track_byte)
	{
		// pclog("Track byte: %02X, old: %02X\n", track_byte, old_track_byte);
		d86f[drive].section_pos = d86f[drive].track_pos;

		switch(d86f[drive].track_byte & ~BYTE_INDEX_HOLE)
		{
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
	int drive = d86f_drive;
	int side = d86f[drive].req_sector.id.h;

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
		d86f_poll_find_nf(drive, side);
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
