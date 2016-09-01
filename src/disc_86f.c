/* Copyright holders: Tenshi
   see COPYING for more details
*/
#include "ibm.h"
#include "disc.h"
#include "disc_86f.h"
#include "fdd.h"

#define MAX_SECTORS 256

typedef struct
{
        uint8_t c, h, r, n;
        int rate;
        uint8_t *data;
} sector_t;

static sector_t d86f_data[2][2][MAX_SECTORS];
static int d86f_count[2][2];

int d86f_cur_track_pos[2] = {0, 0};

enum
{
        STATE_IDLE,
        STATE_READ_FIND_SECTOR,
        STATE_READ_SECTOR,
        STATE_READ_FIND_FIRST_SECTOR,
        STATE_READ_FIRST_SECTOR,
        STATE_READ_FIND_NEXT_SECTOR,
        STATE_READ_NEXT_SECTOR,
        STATE_WRITE_FIND_SECTOR,
        STATE_WRITE_SECTOR,
        STATE_READ_FIND_ADDRESS,
        STATE_READ_ADDRESS,
        STATE_FORMAT_FIND,
        STATE_FORMAT,
	STATE_SEEK
};

static int d86f_state[2] = {0, 0};
static int d86f_track[2] = {0, 0};
static int d86f_side[2] = {0, 0};
static int d86f_drive;
static int d86f_sector[2] = {0, 0};
static int d86f_n[2] = {0, 0};
static uint8_t d86f_fill[2] = {0, 0};
static int cur_sector[2]/*, cur_byte[2]*/;
static int index_count[2];
static int format_sector_count[2] = {0, 0};
        
static struct
{
        FILE *f;
	uint8_t disk_flags;
        uint8_t track_data[2][25000];
        uint8_t track_layout[2][25000];
        uint8_t track_flags;
	uint8_t track_in_file;
        uint32_t track_pos[256];
	uint16_t raw_track_size;
	uint32_t file_size;
} d86f[2];

/* Needed for formatting! */
int d86f_realtrack(int drive, int track)
{
        if (!(d86f[drive].track_flags & 0x40) && fdd_doublestep_40(drive))
                track /= 2;

	return track;
}

void d86f_writeback(int drive, int track);

static uint16_t CRCTable[256];

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

typedef union {
	uint16_t word;
	uint8_t bytes[2];
} crc_t;

static crc_t crc[2];

static void d86f_calccrc(int drive, uint8_t byte)
{
	crc[drive].word = (crc[drive].word << 8) ^ CRCTable[(crc[drive].word >> 8)^byte];
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

	fread(d86f[drive].track_pos, 1, 1024, d86f[drive].f);

	if (!(d86f[drive].track_pos[0]))
	{
		/* File has no track 0, abort. */
		pclog("86F: No Track 0\n");
		fclose(d86f[drive].f);
		return;
	}

	/* Load track 0 flags as default. */
	fseek(d86f[drive].f, d86f[drive].track_pos[0], SEEK_SET);
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

double d86f_byteperiod(int drive)
{
	switch (d86f[drive].track_flags & 0x0f)
	{
		case 0x02:	/* 125 kbps, FM */
			if (!(d86f[drive].track_flags & 0x20))
			{
				/* 300 rpm, 125 kbps = 360 rpm, 150 kbps; so we do this trick for 360 rpm-only drives to accept them. */
				return ((fdd_get_type(drive) & 3) == 2) ? (320.0 / 6.0) : 64.0;
			}
			return 64.0;
		case 0x01:	/* 150 kbps, FM */
			if (d86f[drive].track_flags & 0x20)
			{
				/* 360 rpm, 150 kbps = 300 rpm, 125 kbps; so we do this trick for 300 rpm-only drives to accept them. */
				return ((fdd_get_type(drive) & 3) == 1) ? 64.0 : (320.0 / 6.0);
			}
			return 320.0 / 6.0;
		case 0x0A:	/* 250 kbps, MFM */
		case 0x00:	/* 250 kbps, FM */
			if (!(d86f[drive].track_flags & 0x20))
			{
				/* 300 rpm, 250 kbps = 360 rpm, 300 kbps; so we do this trick for 360 rpm-only drives to accept them. */
				return ((fdd_get_type(drive) & 3) == 2) ? (160.0 / 6.0) : 32.0;
			}
			return 32.0;
		case 0x09:	/* 300 kbps, MFM */
			if (d86f[drive].track_flags & 0x20)
			{
				/* 360 rpm, 300 kbps = 300 rpm, 250 kbps; so we do this trick for 300 rpm-only drives to accept them. */
				return ((fdd_get_type(drive) & 3) == 1) ? 32.0 : (160.0 / 6.0);
			}
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

char track_layout[2][2][25512];

int id_positions[2][2][MAX_SECTORS];

/* 0 = MFM, 1 = FM, 2 = MFM perpendicular, 3 = reserved */
/* 4 = ISO, 0 = IBM */
int d86f_media_type = 0;

/* Bits 0-3 define byte type, bit 5 defines whether it is a per-track (0) or per-sector (1) byte, if bit 7 is set, the byte is the index hole. */
#define BYTE_GAP0		0x00
#define BYTE_GAP1		0x10
#define BYTE_GAP4		0x20
#define BYTE_GAP2		0x40
#define BYTE_GAP3		0x50
#define BYTE_I_SYNC		0x01
#define BYTE_ID_SYNC		0x41
#define BYTE_DATA_SYNC		0x51
#define BYTE_IAM_SYNC		0x02
#define BYTE_IDAM_SYNC		0x42
#define BYTE_DATAAM_SYNC	0x52
#define BYTE_IAM		0x03
#define BYTE_IDAM		0x43
#define BYTE_DATAAM		0x53
#define BYTE_ID			0x44
#define BYTE_DATA		0x54
#define BYTE_ID_CRC		0x45
#define BYTE_DATA_CRC		0x55

#define BYTE_INDEX_HOLE		0x80	/* 1 = index hole, 0 = regular byte */
#define BYTE_IS_SECTOR		0x40	/* 1 = per-sector, 0 = per-track */
#define BYTE_IS_POST_TRACK	0x20	/* 1 = after all sectors, 0 = before or during all sectors */
#define BYTE_IS_DATA		0x10	/* 1 = data, 0 = id */
#define BYTE_TYPE		0x0F	/* 5 = crc, 4 = data, 3 = address mark, 2 = address mark sync, 1 = sync, 0 = gap */

#define BYTE_TYPE_GAP		0x00
#define BYTE_TYPE_SYNC		0x01
#define BYTE_TYPE_AM_SYNC	0x02
#define BYTE_TYPE_AM		0x03
#define BYTE_TYPE_DATA		0x04
#define BYTE_TYPE_CRC		0x05

int d86f_get_sides(int drive)
{
	return (d86f[drive].disk_flags & 8) ? 2 : 1;
}

int d86f_is_40_track(int drive)
{
	return !(d86f[drive].disk_flags & 1);
}

int d86f_is_mfm(int drive)
{
	return (d86f[drive].track_flags & 8) ? 1 : 0;
}

uint16_t d86f_get_raw_size(int drive)
{
	double rate = 0.0;
	int mfm = d86f_is_mfm(drive);
	double rpm = (d86f[drive].track_flags & 0x20) ? 360.0 : 300.0;
	double size = 6250.0;
	switch (d86f[drive].track_flags & 7)
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

	memset(d86f[drive].track_layout[0], BYTE_GAP4, 25000);
	memset(d86f[drive].track_data[0], 0xFF, 25000);

	if (d86f_get_sides(drive) == 2)
	{
		memset(d86f[drive].track_layout[1], BYTE_GAP4, 25000);
		memset(d86f[drive].track_data[1], 0xFF, 25000);
	}

	if (!(d86f[drive].track_pos[track]))
	{
		/* Track does not exist in the image, initialize it as unformatted. */
		d86f[drive].track_in_file = 0;
		d86f[drive].track_flags = 0x0A;	/* 300 rpm, MFM, 250 kbps */

		d86f[drive].raw_track_size = 6250;
		return;
	}

	d86f[drive].track_in_file = 1;

	fseek(d86f[drive].f, d86f[drive].track_pos[track], SEEK_SET);

	fread(&(d86f[drive].track_flags), 1, 1, d86f[drive].f);

	d86f[drive].raw_track_size = d86f_get_raw_size(drive);

	fseek(d86f[drive].f, d86f[drive].track_pos[track] + 2, SEEK_SET);
	fread(d86f[drive].track_layout[0], 1, d86f[drive].raw_track_size, d86f[drive].f);
	fseek(d86f[drive].f, d86f[drive].track_pos[track] + 25002, SEEK_SET);
	fread(d86f[drive].track_data[0], 1, d86f[drive].raw_track_size, d86f[drive].f);

	if (d86f_get_sides(drive) == 2)
	{
		fseek(d86f[drive].f, d86f[drive].track_pos[track] + 50002, SEEK_SET);
		fread(d86f[drive].track_layout[1], 1, d86f[drive].raw_track_size, d86f[drive].f);
		fseek(d86f[drive].f, d86f[drive].track_pos[track] + 75002, SEEK_SET);
		fread(d86f[drive].track_data[1], 1, d86f[drive].raw_track_size, d86f[drive].f);
	}
}

void d86f_writeback(int drive, int track)
{
        if (!d86f[drive].f)
                return;
                
        if (!d86f[drive].track_in_file)
                return; /*Should never happen*/

	fseek(d86f[drive].f, d86f[drive].track_pos[track], SEEK_SET);

	fwrite(&(d86f[drive].track_flags), 1, 1, d86f[drive].f);

	fseek(d86f[drive].f, d86f[drive].track_pos[track] + 2, SEEK_SET);
	fwrite(d86f[drive].track_layout[0], 1, d86f[drive].raw_track_size, d86f[drive].f);
	fseek(d86f[drive].f, d86f[drive].track_pos[track] + 25002, SEEK_SET);
	fwrite(d86f[drive].track_data[0], 1, d86f[drive].raw_track_size, d86f[drive].f);

	if (d86f_get_sides(drive) == 2)
	{
		fseek(d86f[drive].f, d86f[drive].track_pos[track] + 50002, SEEK_SET);
		fwrite(d86f[drive].track_layout[1], 1, d86f[drive].raw_track_size, d86f[drive].f);
		fseek(d86f[drive].f, d86f[drive].track_pos[track] + 75002, SEEK_SET);
		fwrite(d86f[drive].track_data[1], 1, d86f[drive].raw_track_size, d86f[drive].f);
	}
}

void d86f_reset(int drive, int side)
{
        d86f_count[drive][side] = 0;

	if (side == 0)
	{
		index_count[drive] = 0;
		d86f_state[drive] = STATE_SEEK;
	}
}

static int get_bitcell_period(int drive)
{
	double rate = 0.0;
	int mfm = (d86f[drive].track_flags & 8) ? 1 : 0;
	double rpm = (d86f[drive].track_flags & 0x20) ? 360.0 : 300.0;
	double size = 8000.0;
	switch (d86f[drive].track_flags & 7)
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
        // pclog("d86f_readsector: fdc_period=%i img_period=%i rate=%i sector=%i track=%i side=%i\n", fdc_get_bitcell_period(), get_bitcell_period(drive), rate, sector, track, side);

        d86f_track[drive] = track;
        d86f_side[drive]  = side;

	if (side && (d86f_get_sides(drive) == 1))
	{
		fdc_notfound();
		d86f_state[drive] = STATE_IDLE;
		index_count[drive] = 0;
		return;
	}

        d86f_drive = drive;
        d86f_sector[drive] = sector;
	d86f_n[drive] = sector_size;
	index_count[drive] = 0;
        if (sector == SECTOR_FIRST)
                d86f_state[drive] = STATE_READ_FIND_FIRST_SECTOR;
        else if (sector == SECTOR_NEXT)
                d86f_state[drive] = STATE_READ_FIND_NEXT_SECTOR;
        else
                d86f_state[drive] = STATE_READ_FIND_SECTOR;
}

void d86f_writesector(int drive, int sector, int track, int side, int rate, int sector_size)
{
        // pclog("d86f_writesector: fdc_period=%i img_period=%i rate=%i\n", fdc_get_bitcell_period(), get_bitcell_period(drive), rate);

        d86f_track[drive] = track;
        d86f_side[drive]  = side;

	if (side && (d86f_get_sides(drive) == 1))
	{
		fdc_notfound();
		d86f_state[drive] = STATE_IDLE;
		index_count[drive] = 0;
		return;
	}

        d86f_drive = drive;
        d86f_sector[drive] = sector;
	d86f_n[drive] = sector_size;
	index_count[drive] = 0;
        d86f_state[drive] = STATE_WRITE_FIND_SECTOR;
}

void d86f_readaddress(int drive, int track, int side, int rate)
{
        // pclog("d86f_readaddress: fdc_period=%i img_period=%i rate=%i track=%i side=%i\n", fdc_get_bitcell_period(), get_bitcell_period(drive), rate, track, side);

        d86f_track[drive] = track;
        d86f_side[drive]  = side;

	if (side && (d86f_get_sides(drive) == 1))
	{
		fdc_notfound();
		d86f_state[drive] = STATE_IDLE;
		index_count[drive] = 0;
		return;
	}

        d86f_drive = drive;
	index_count[drive] = 0;
        d86f_state[drive] = STATE_READ_FIND_ADDRESS;
}

int d86f_is_iso(int drive)
{
	return 0;	/* Currently, we always use the IBM format, not ISO. */
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
	if (!(d86f_is_iso(drive)))
	{
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
	}
	else
	{
		memset(d86f[drive].track_layout[side] + i, BYTE_GAP1, real_gap1_len);
		i += real_gap1_len;
	}
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
        d86f_track[drive] = track;
        d86f_side[drive]  = side;

	if (side && (d86f_get_sides(drive) == 1))
	{
		fdc_notfound();
		d86f_state[drive] = STATE_IDLE;
		index_count[drive] = 0;
		return;
	}

	if ((track < 0) || (track > 256))
	{
		fdc_writeprotect();
		d86f_state[drive] = STATE_IDLE;
		index_count[drive] = 0;
		return;
	}

        d86f_drive = drive;
        d86f_fill[drive]  = fill;
	index_count[drive] = 0;
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
		d86f[drive].track_pos[track] = d86f[drive].file_size;
		d86f[drive].file_size += (2 + 50000);
		if (d86f_get_sides(drive) == 2)
		{
			d86f[drive].file_size += 50000;
		}
		d86f[drive].track_in_file = 1;
	}

        d86f_state[drive] = STATE_FORMAT_FIND;
	format_sector_count[drive] = 0;
}

void d86f_stop(int drive)
{
        d86f_state[drive] = STATE_IDLE;
}

static void index_pulse(int drive)
{
	if (d86f_state[drive] != STATE_IDLE)  fdc_indexpulse();
}

typedef struct {
	uint8_t c;
	uint8_t h;
	uint8_t r;
	uint8_t n;
} sector_data_t;

sector_data_t last_sector[2];

int d86f_match(int drive)
{
	int temp;
	temp = (d86f_track[drive] == last_sector[drive].c);
	temp = temp && (d86f_side[drive] == last_sector[drive].h);
	temp = temp && (d86f_sector[drive] == last_sector[drive].r);
	temp = temp && (d86f_n[drive] == last_sector[drive].n);
	return temp;
}

uint32_t d86f_get_data_len(int drive)
{
	if (d86f_n[drive])
	{
		return (128 << ((uint32_t) d86f_n[drive]));
	}
	else
	{
		if (fdc_get_dtl() < 128)
		{
			return fdc_get_dtl();
		}
		else
		{
			return (128 << ((uint32_t) d86f_n[drive]));
		}
	}
}

int d86f_can_read_address(int drive)
{
	int temp;
	temp = (fdc_get_bitcell_period() == get_bitcell_period(drive));
	temp = temp && fdd_can_read_medium(drive ^ fdd_swap);
	temp = temp && (fdc_is_mfm() == d86f_is_mfm(drive));
	return temp;
}

int d86f_can_format(int drive)
{
	int temp;
	temp = !writeprot[drive];
	temp = temp && !swwp;
	temp = temp && fdd_can_read_medium(drive ^ fdd_swap);
	return temp;
}

int d86f_find_state(int drive)
{
	int temp;
	temp = (d86f_state[drive] == STATE_READ_FIND_SECTOR);
	temp = temp || (d86f_state[drive] == STATE_READ_FIND_FIRST_SECTOR);
	temp = temp || (d86f_state[drive] == STATE_READ_FIND_NEXT_SECTOR);
	temp = temp || (d86f_state[drive] == STATE_WRITE_FIND_SECTOR);
	temp = temp || (d86f_state[drive] == STATE_READ_FIND_ADDRESS);
	temp = temp || (d86f_state[drive] == STATE_FORMAT_FIND);
	return temp;
}

int d86f_find_state_nf(int drive)
{
	int temp;
	temp = (d86f_state[drive] == STATE_READ_FIND_SECTOR);
	temp = temp || (d86f_state[drive] == STATE_READ_FIND_FIRST_SECTOR);
	temp = temp || (d86f_state[drive] == STATE_READ_FIND_NEXT_SECTOR);
	temp = temp || (d86f_state[drive] == STATE_WRITE_FIND_SECTOR);
	temp = temp || (d86f_state[drive] == STATE_READ_FIND_ADDRESS);
	return temp;
}

int d86f_read_state(int drive)
{
	int temp;
	temp = (d86f_state[drive] == STATE_READ_SECTOR);
	temp = temp || (d86f_state[drive] == STATE_READ_FIRST_SECTOR);
	temp = temp || (d86f_state[drive] == STATE_READ_NEXT_SECTOR);
	return temp;
}

int d86f_section_pos[2] = {0, 0};

int data_pos[2] = {0, 0};

int d86f_data_size(int drive)
{
	int temp;
	temp = d86f_n[drive];
	temp = 128 << temp;
	return temp;
}

typedef union
{
	uint32_t dword;
	uint8_t byte_array[4];
} sector_id;

sector_id format_sector_id[2];
sector_id rw_sector_id[2];

crc_t crc_bytes[2];

static uint32_t datac[2] = {0, 0};

void d86f_poll()
{
        sector_t *s;
        int data;
	int drive = d86f_drive;
	int side = d86f_side[drive];
	int found_sector = 0;
	int b = 0;

	int cur_id_pos = 0;
	int cur_gap3_pos = 0;

	uint8_t track_byte = 0;
	uint8_t track_index = 0;
	uint8_t track_sector = 0;
	uint8_t track_byte_type = 0;

	uint8_t old_track_byte = 0;
	uint8_t old_track_index = 0;
	uint8_t old_track_sector = 0;
	uint8_t old_track_byte_type = 0;

	if ((d86f_state[drive] == STATE_SEEK) || (d86f_state[drive] == STATE_IDLE))
	{
		d86f_cur_track_pos[drive]++;
		d86f_cur_track_pos[drive] %= d86f_get_raw_size(drive);
		return;
	}

	if (d86f_state[drive] == STATE_FORMAT_FIND)
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
                        d86f_state[drive] = STATE_IDLE;
			index_count[drive] = 0;
			d86f_cur_track_pos[drive]++;
			d86f_cur_track_pos[drive] %= d86f_get_raw_size(drive);
			return;
		}
	}

	track_byte = d86f[drive].track_layout[side][d86f_cur_track_pos[drive]];
	track_index = track_byte & BYTE_INDEX_HOLE;
	track_sector = track_byte & BYTE_IS_SECTOR;
	track_byte_type = track_byte & BYTE_TYPE;

	if (track_index)
	{
		if (d86f_state[drive] != STATE_IDLE)
		{
			index_pulse(drive);
			index_count[drive]++;
		}
		if (d86f_state[drive] == STATE_FORMAT)
		{
			// pclog("Index hole hit again, format finished\n");
              		d86f_state[drive] = STATE_IDLE;
   		        if (!disable_write)  d86f_writeback(drive, d86f_track[drive]);
                        fdc_sector_finishread(drive);
		}
		if ((d86f_state[drive] == STATE_FORMAT_FIND) && d86f_can_read_address(drive))
		{
			// pclog("Index hole hit, formatting track...\n");
			d86f_state[drive] = STATE_FORMAT;
		}
	}

	if (d86f_read_state(drive) || (d86f_state[drive] == STATE_WRITE_SECTOR))
	{
		/* For the first (data size) bytes, send to the FDC or receive from the FDC and write. */
		if (d86f_read_state(drive) && (datac[drive] < d86f_data_size(drive)))
		{
			if (fdc_data(d86f[drive].track_data[side][d86f_cur_track_pos[drive]]))
			{
				/* Data failed to be sent to the FDC, abort. */
				// pclog("d86f_poll(): Unable to send further data to the FDC\n");
				d86f_state[drive] = STATE_IDLE;
				index_count[drive] = 0;
				d86f_cur_track_pos[drive]++;
				d86f_cur_track_pos[drive] %= d86f_get_raw_size(drive);
				return;
			}
		}

		if ((d86f_state[drive] == STATE_WRITE_SECTOR) && (datac[drive] < d86f_data_size(drive)))
		{
       		       	data = fdc_getdata(datac[drive] == ((128 << ((uint32_t) last_sector[drive].n)) - 1));
	       	        if (data == -1)
			{
				/* Data failed to be sent from the FDC, abort. */
				// pclog("d86f_poll(): Unable to receive further data from the FDC\n");
				d86f_state[drive] = STATE_IDLE;
				index_count[drive] = 0;
				d86f_cur_track_pos[drive]++;
				d86f_cur_track_pos[drive] %= d86f_get_raw_size(drive);
				return;
			}
			if (!disable_write)
			{
				d86f[drive].track_data[side][d86f_cur_track_pos[drive]] = data;
				d86f[drive].track_layout[side][d86f_cur_track_pos[drive]] = BYTE_DATA;
				if (!d86f_cur_track_pos[drive])  d86f[drive].track_layout[side][d86f_cur_track_pos[drive]] |= BYTE_INDEX_HOLE;
			}
		}

		if (datac[drive] < d86f_data_size(drive))
		{
			d86f_calccrc(drive, d86f[drive].track_data[side][d86f_cur_track_pos[drive]]);
		}

		if (datac[drive] == d86f_data_size(drive))
		{
			if (d86f_read_state(drive))
			{
				crc_bytes[drive].bytes[0] = d86f[drive].track_data[side][d86f_cur_track_pos[drive]];
			}

			if (d86f_state[drive] == STATE_WRITE_SECTOR)
			{
				if (!disable_write)
				{
					d86f[drive].track_data[side][d86f_cur_track_pos[drive]] = crc[drive].bytes[0];
					d86f[drive].track_layout[side][d86f_cur_track_pos[drive]] = BYTE_DATA_CRC;
					if (!d86f_cur_track_pos[drive])  d86f[drive].track_layout[side][d86f_cur_track_pos[drive]] |= BYTE_INDEX_HOLE;
				}
			}
		}

		if (datac[drive] == (d86f_data_size(drive) + 1))
		{
			if (d86f_read_state(drive))
			{
				crc_bytes[drive].bytes[1] = d86f[drive].track_data[side][d86f_cur_track_pos[drive]];
			}

			if (d86f_state[drive] == STATE_WRITE_SECTOR)
			{
				if (!disable_write)
				{
					d86f[drive].track_data[side][d86f_cur_track_pos[drive]] = crc[drive].bytes[1];
					d86f[drive].track_layout[side][d86f_cur_track_pos[drive]] = BYTE_DATA_CRC;
					if (!d86f_cur_track_pos[drive])  d86f[drive].track_layout[side][d86f_cur_track_pos[drive]] |= BYTE_INDEX_HOLE;
				}
			}
		}

		if ((datac[drive] > (d86f_data_size(drive) + 1)) && (datac[drive] <= (d86f_data_size(drive) + fdc_get_gap() + 1)) && (d86f_state[drive] == STATE_WRITE_SECTOR) && !disable_write)
		{
			d86f[drive].track_data[side][d86f_cur_track_pos[drive]] = fdc_is_mfm() ? 0x4E : 0xFF;
			d86f[drive].track_layout[side][d86f_cur_track_pos[drive]] = BYTE_GAP3;
			if (!d86f_cur_track_pos[drive])  d86f[drive].track_layout[side][d86f_cur_track_pos[drive]] |= BYTE_INDEX_HOLE;
		}

		/* At the last data of the FDC-specified gap length, compare the calculated CRC with the one we read and error out if mismatch,  otherwise finish with success. */
		if (datac[drive] == (d86f_data_size(drive) + fdc_get_gap() + 1))
		{
			if (d86f_read_state(drive))
			{
	       	                d86f_state[drive] = STATE_IDLE;
				if (crc_bytes[drive].word != crc[drive].word)
				{
					// pclog("d86f_poll(): Data CRC error (%04X, %04X) (%i) (%04X)\n", crc_bytes[drive].word, crc[drive].word, d86f_data_size(drive), d86f_cur_track_pos[drive] - fdc_get_gap() - 1);
					fdc_finishread();
					fdc_datacrcerror();
				}
				else
				{
	        	               	fdc_sector_finishread();
				}
				index_count[drive] = 0;
				d86f_cur_track_pos[drive]++;
				d86f_cur_track_pos[drive] %= d86f_get_raw_size(drive);
				return;
			}
			if (d86f_state[drive] == STATE_WRITE_SECTOR)
			{
               		        d86f_state[drive] = STATE_IDLE;
	    		        if (!disable_write)  d86f_writeback(drive, d86f_track[drive]);
        	        	fdc_sector_finishread(drive);
				index_count[drive] = 0;
				d86f_cur_track_pos[drive]++;
				d86f_cur_track_pos[drive] %= d86f_get_raw_size(drive);
				return;
			}
		}

		datac[drive]++;
	}

	switch(track_byte & ~BYTE_INDEX_HOLE)
	{
		case BYTE_GAP0:
		case BYTE_GAP1:
		case BYTE_GAP2:
		case BYTE_GAP3:
		case BYTE_GAP4:
			if ((d86f_state[drive] == STATE_FORMAT) && !disable_write)  d86f[drive].track_data[side][d86f_cur_track_pos[drive]] = fdc_is_mfm() ? 0x4E : 0xFF;
			break;
		case BYTE_IAM_SYNC:
			if ((d86f_state[drive] == STATE_FORMAT) && !disable_write)  d86f[drive].track_data[side][d86f_cur_track_pos[drive]] = 0xC2;
			break;
		case BYTE_IAM:
			if ((d86f_state[drive] == STATE_FORMAT) && !disable_write)  d86f[drive].track_data[side][d86f_cur_track_pos[drive]] = 0xFC;
			break;
		case BYTE_ID_SYNC:
			if (d86f_state[drive] == STATE_FORMAT)
			{
				cur_id_pos = d86f_cur_track_pos[drive] - d86f_section_pos[drive];
				if (!disable_write)  d86f[drive].track_data[side][d86f_cur_track_pos[drive]] = 0;
				if (cur_id_pos > 3)  break;
                		data = fdc_getdata(0);
        	        	if ((data == -1) && (cur_id_pos < 3))
				{
					/* Data failed to be sent from the FDC, abort. */
					// pclog("d86f_poll(): Unable to receive further data from the FDC\n");
					d86f_state[drive] = STATE_IDLE;
					index_count[drive] = 0;
					d86f_cur_track_pos[drive]++;
					d86f_cur_track_pos[drive] %= d86f_get_raw_size(drive);
					return;
				}
				format_sector_id[drive].byte_array[cur_id_pos] = data & 0xff;
				d86f_calccrc(drive, format_sector_id[drive].byte_array[cur_id_pos]);
				// pclog("format_sector_id[%i] = %i\n", cur_id_pos, format_sector_id[drive].byte_array[cur_id_pos]);
        	        	if (cur_id_pos == 3)
				{
					fdc_stop_id_request();
					// pclog("Formatting sector: %08X...\n", format_sector_id[drive].dword);
				}
			}
			break;
		case BYTE_I_SYNC:
		case BYTE_DATA_SYNC:
			if ((d86f_state[drive] == STATE_FORMAT) && !disable_write)  d86f[drive].track_data[side][d86f_cur_track_pos[drive]] = 0;
			break;
		case BYTE_IDAM_SYNC:
		case BYTE_DATAAM_SYNC:
			if ((d86f_state[drive] == STATE_FORMAT) && !disable_write)  d86f[drive].track_data[side][d86f_cur_track_pos[drive]] = 0xA1;
			break;
		case BYTE_IDAM:
		case BYTE_DATAAM:
			if (d86f_find_state_nf(drive) || (d86f_state[drive] == STATE_FORMAT))  crc[drive].word = fdc_is_mfm() ? 0xcdb4 : 0xffff;
			if ((d86f_state[drive] == STATE_FORMAT) && !disable_write)  d86f[drive].track_data[side][d86f_cur_track_pos[drive]] = (track_byte == BYTE_IDAM) ? 0xFE : 0xFB;
			if (d86f_state[drive] == STATE_FORMAT)  d86f_calccrc(drive, d86f[drive].track_data[side][d86f_cur_track_pos[drive]]);
			if (d86f_find_state_nf(drive) && ((d86f_state[drive] != STATE_WRITE_FIND_SECTOR) || ((track_byte & ~BYTE_INDEX_HOLE) == BYTE_IDAM)))
			{
				d86f_calccrc(drive, d86f[drive].track_data[side][d86f_cur_track_pos[drive]]);
			}
			break;
		case BYTE_ID:
			cur_id_pos = d86f_cur_track_pos[drive] - d86f_section_pos[drive];
			if ((d86f_state[drive] == STATE_FORMAT) && !disable_write)
			{
				d86f[drive].track_data[side][d86f_cur_track_pos[drive]] = format_sector_id[drive].byte_array[cur_id_pos];
			}
			if (d86f_find_state_nf(drive))
			{
				rw_sector_id[drive].byte_array[cur_id_pos] = d86f[drive].track_data[side][d86f_cur_track_pos[drive]];
			}
			if ((d86f_state[drive] == STATE_FORMAT) || d86f_find_state_nf(drive))
			{
				d86f_calccrc(drive, d86f[drive].track_data[side][d86f_cur_track_pos[drive]]);
			}
			break;
		case BYTE_DATA:
			if ((d86f_state[drive] == STATE_FORMAT) && !disable_write)
			{
				d86f[drive].track_data[side][d86f_cur_track_pos[drive]] = d86f_fill[drive];
				d86f_calccrc(drive, d86f_fill[drive]);
			}
			break;
		case BYTE_ID_CRC:
			cur_id_pos = d86f_cur_track_pos[drive] - d86f_section_pos[drive];
			if (d86f_find_state_nf(drive))
			{
				crc_bytes[drive].bytes[0] = d86f[drive].track_data[side][(d86f_cur_track_pos[drive] - 1) % d86f_get_raw_size(drive)];
				crc_bytes[drive].bytes[1] = d86f[drive].track_data[side][d86f_cur_track_pos[drive]];
			}
			if ((d86f_state[drive] == STATE_FORMAT) && !disable_write)
			{
				if (cur_id_pos)
				{
					d86f[drive].track_data[side][d86f_cur_track_pos[drive]] = crc[drive].bytes[1];
				}
				else
				{
					d86f[drive].track_data[side][d86f_cur_track_pos[drive]] = crc[drive].bytes[0];
				}
			}
			break;
		case BYTE_DATA_CRC:
			cur_id_pos = d86f_cur_track_pos[drive] - d86f_section_pos[drive];
			if ((d86f_state[drive] == STATE_FORMAT) && !disable_write)
			{
				if (cur_id_pos)
				{
					d86f[drive].track_data[side][d86f_cur_track_pos[drive]] = crc[drive].bytes[1];
				}
				else
				{
					d86f[drive].track_data[side][d86f_cur_track_pos[drive]] = crc[drive].bytes[0];
				}
			}
			break;
	}

	old_track_byte = track_byte;
	old_track_index = track_index;
	old_track_sector = track_sector;
	old_track_byte_type = track_byte_type;

	d86f_cur_track_pos[drive]++;
	d86f_cur_track_pos[drive] %= d86f_get_raw_size(drive);

	track_byte = d86f[drive].track_layout[side][d86f_cur_track_pos[drive]];
	track_index = track_byte & BYTE_INDEX_HOLE;
	track_sector = track_byte & BYTE_IS_SECTOR;
	track_byte_type = track_byte & BYTE_TYPE;

	if ((d86f_state[drive] != STATE_IDLE) && (d86f_state[drive] != STATE_SEEK))
	{
		if (index_count[drive] > 1)
		{
			if (d86f_find_state(drive))
			{
				/* The index hole has been hit twice and we're still in a find state.
				   This means sector finding has failed for whatever reason.
				   Abort with sector not found and set state to idle. */
				// pclog("d86f_poll(): Sector not found (%i %i %i %i)\n", d86f_track[drive], d86f_side[drive], d86f_sector[drive], d86f_n[drive]);
				fdc_notfound();
				d86f_state[drive] = STATE_IDLE;
				index_count[drive] = 0;
				return;
			}
		}
	}

	if ((d86f_state[drive] == STATE_IDLE) || (d86f_state[drive] == STATE_SEEK))  return;

	if (track_byte != old_track_byte)
	{
		// if (d86f_state[drive] == STATE_FORMAT)  pclog("Track byte: %02X, old: %02X\n", track_byte, old_track_byte);
		d86f_section_pos[drive] = d86f_cur_track_pos[drive];

		switch(track_byte & ~BYTE_INDEX_HOLE)
		{
			case BYTE_ID_SYNC:
				if (d86f_state[drive] == STATE_FORMAT)
				{
					// pclog("Requesting next sector ID...\n");
					fdc_request_next_sector_id();
				}
				break;
			case BYTE_GAP2:
				if (d86f_find_state_nf(drive))
				{
					if (d86f_match(drive) && d86f_can_read_address(drive))
					{
						if (crc_bytes[drive].word != crc[drive].word)
						{
							// pclog("d86f_poll(): Header CRC error\n");
							fdc_finishread();
							fdc_headercrcerror();
							d86f_state[drive] = STATE_IDLE;
							index_count[drive] = 0;
							return;
						}
					}

					last_sector[drive].c = rw_sector_id[drive].byte_array[0];
					last_sector[drive].h = rw_sector_id[drive].byte_array[1];
					last_sector[drive].r = rw_sector_id[drive].byte_array[2];
					last_sector[drive].n = rw_sector_id[drive].byte_array[3];
					// pclog("Read sector ID in find state: %i %i %i %i (sought: %i, %i, %i, %i)\n", last_sector[drive].c, last_sector[drive].h, last_sector[drive].r, last_sector[drive].n, d86f_track[drive], d86f_side[drive], d86f_sector[drive], d86f_n[drive]);
				}

				if (d86f_state[drive] == STATE_FORMAT)
				{
					last_sector[drive].c = format_sector_id[drive].byte_array[0];
					last_sector[drive].h = format_sector_id[drive].byte_array[1];
					last_sector[drive].r = format_sector_id[drive].byte_array[2];
					last_sector[drive].n = format_sector_id[drive].byte_array[3];
				}

				if ((d86f_state[drive] == STATE_READ_FIND_ADDRESS) && d86f_can_read_address(drive))
				{
					// pclog("Reading sector ID...\n");
					fdc_sectorid(last_sector[drive].c, last_sector[drive].h, last_sector[drive].r, last_sector[drive].n, 0, 0);
					d86f_state[drive] = STATE_IDLE;
				}
				break;
			case BYTE_DATA:
				if (d86f_read_state(drive) || (d86f_state[drive] == STATE_WRITE_SECTOR))  return;
				datac[drive] = 0;
				switch (d86f_state[drive])
				{
					case STATE_READ_FIND_SECTOR:
						if (d86f_match(drive) && d86f_can_read_address(drive))
						{
							d86f_state[drive] = STATE_READ_SECTOR;
						}
						break;
					case STATE_READ_FIND_FIRST_SECTOR:
						if ((cur_sector[drive] == 0) && d86f_can_read_address(drive))  d86f_state[drive] = STATE_READ_FIRST_SECTOR;
						break;
					case STATE_READ_FIND_NEXT_SECTOR:
						if (d86f_can_read_address(drive))  d86f_state[drive] = STATE_READ_NEXT_SECTOR;
						break;
					case STATE_WRITE_FIND_SECTOR:
						if (d86f_match(drive) && d86f_can_read_address(drive))
						{
							if (!disable_write)
							{
								d86f[drive].track_data[side][(d86f_cur_track_pos[drive] - 1) % d86f_get_raw_size(drive)] = 0xFB;
							}
							d86f_calccrc(drive, d86f[drive].track_data[side][(d86f_cur_track_pos[drive] - 1) % d86f_get_raw_size(drive)]);

							d86f_state[drive] = STATE_WRITE_SECTOR;
							// pclog("Write start: %i %i %i %i, data size: %i (%i)\n", last_sector[drive].c, last_sector[drive].h, last_sector[drive].r, last_sector[drive].n, d86f_data_size(drive), d86f_n[drive]);
						}
						break;
					/* case STATE_FORMAT:
						// pclog("Format: Starting sector fill...\n");
						break; */
				}
				break;
		}
	}
}
