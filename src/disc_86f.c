/* Copyright holders: Tenshi
   see COPYING for more details
*/
#include "ibm.h"
#include "disc.h"
#include "disc_d86f.h"
#include "fdd.h"

/*Handling for 'sector based' image formats (like .IMG) as opposed to 'stream based' formats (eg .FDI)*/

#define MAX_SECTORS 256

typedef struct
{
        uint8_t c, h, r, n;
        int rate;
        uint8_t *data;
} sector_t;

static sector_t d86f_data[2][2][MAX_SECTORS];
static int d86f_count[2][2];

int cur_track_pos[2] = {0, 0};
int id_counter[2] = {0, 0};
int data_counter[2] = {0, 0};
int gap3_counter[2] = {0, 0};
int cur_rate[2] = {0, 0};

sector_t *last_sector[2];

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
static int disc_intersector_delay[2] = {0, 0};
static int disc_postdata_delay[2] = {0, 0};
static int disc_track_delay[2] = {0, 0};
static int disc_gap4_delay[2] = {0, 0};
static uint8_t d86f_fill[2] = {0, 0};
static int cur_sector[2], cur_byte[2];
static int index_count[2];
        
int raw_tsize[2] = {6250, 6250};
int gap2_size[2] = {22, 22};
int gap3_size[2] = {0, 0};
int gap4_size[2] = {0, 0};

int d86f_reset_state(int drive);

static struct
{
        FILE *f;
        uint8_t track_data[2][50000];
        uint8_t track_layout[2][50000];
        uint8_t track_flags[2][256];
        uint32_t track_pos[2][256];
} d86f[2];

/* Needed for formatting! */
int d86f_realtrack(int drive, int track)
{
        if (!(d86f[drive].track_flags[track] & 0x40) && fdd_doublestep_40(drive))
                track /= 2;

	return track;
}

void d86f_writeback(int drive, int track);

void d86f_init()
{
        memset(d86f, 0, sizeof(d86f));
//        adl[0] = adl[1] = 0;
}

void d86f_load(int drive, char *fn)
{
	uint32_t magic = 0;
	uint32_t len = 0;
	uint16_t version = 0;

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
	len = ftell(f);
	fseek(d86f[drive].f, 0, SEEK_SET);

	if (len < 52056)
	{
		/* File too small, abort. */
		fclose(d86f[drive].f);
		return;
	}

	fread(&magic, 4, 1, d86f[drive].f);

	if (magic != 0x464236368)
	{
		/* File is not of the valid format, abort. */
		fclose(d86f[drive].f);
		return;
	}

	fread(&version, 2, 1, d86f[drive].f);

	if (version != 0x0100)
	{
		/* File is not of a recognized format version. */
		fclose(d86f[drive].f);
		return;
	}

	if (strcmp(ext, "fdi") == 0)
	{
		/* This is a Japanese FDI image, so let's read the header */
		pclog("d86f_load(): File is a Japanese FDI image...\n");
		fseek(d86f[drive].f, 0x10, SEEK_SET);
		fread(&bpb_bps, 1, 2, d86f[drive].f);
		fseek(d86f[drive].f, 0x0C, SEEK_SET);
		fread(&size, 1, 4, d86f[drive].f);
		bpb_total = size / bpb_bps;
		fseek(d86f[drive].f, 0x08, SEEK_SET);
		fread(&(d86f[drive].base), 1, 4, d86f[drive].f);
		fseek(d86f[drive].f, d86f[drive].base + 0x15, SEEK_SET);
		bpb_mid = fgetc(d86f[drive].f);
		if (bpb_mid < 0xF0)  bpb_mid = 0xF0;
		fseek(d86f[drive].f, 0x14, SEEK_SET);
		bpb_sectors = fgetc(d86f[drive].f);
		fseek(d86f[drive].f, 0x18, SEEK_SET);
		bpb_sides = fgetc(d86f[drive].f);

		fdi = 1;
	}
	else
	{
		/* Read the BPB */
		pclog("d86f_load(): File is a raw image...\n");
		fseek(d86f[drive].f, 0x0B, SEEK_SET);
		fread(&bpb_bps, 1, 2, d86f[drive].f);
		fseek(d86f[drive].f, 0x13, SEEK_SET);
		fread(&bpb_total, 1, 2, d86f[drive].f);
		fseek(d86f[drive].f, 0x15, SEEK_SET);
		bpb_mid = fgetc(d86f[drive].f);
		fseek(d86f[drive].f, 0x18, SEEK_SET);
		bpb_sectors = fgetc(d86f[drive].f);
		fseek(d86f[drive].f, 0x1A, SEEK_SET);
		bpb_sides = fgetc(d86f[drive].f);

		d86f[drive].base = 0;
		fdi = 0;

	        fseek(d86f[drive].f, -1, SEEK_END);
	        size = ftell(d86f[drive].f) + 1;
	}

        d86f[drive].sides = 2;
        d86f[drive].sector_size = 512;

	d86f[drive].hole = 0;

	pclog("BPB reports %i sides and %i bytes per sector\n", bpb_sides, bpb_bps);

	if (((bpb_sides < 1) || (bpb_sides > 2) || (bpb_bps < 128) || (bpb_bps > 2048)) && !fdi)
	{
		/* The BPB is giving us a wacky number of sides and/or bytes per sector, therefore it is most probably
		   not a BPB at all, so we have to guess the parameters from file size. */

		if (size <= (160*1024))        { d86f[drive].sectors = 8;  d86f[drive].tracks = 40; d86f[drive].sides = 1; bit_rate_300 = 250; raw_tsize[drive] = 6250; }
	        else if (size <= (180*1024))   { d86f[drive].sectors = 9;  d86f[drive].tracks = 40; d86f[drive].sides = 1; bit_rate_300 = 250; raw_tsize[drive] = 6250; }
	        else if (size <= (320*1024))   { d86f[drive].sectors = 8;  d86f[drive].tracks = 40; bit_rate_300 = 250; raw_tsize[drive] = 6250; }
	        else if (size <= (360*1024))   { d86f[drive].sectors = 9;  d86f[drive].tracks = 40; bit_rate_300 = 250; raw_tsize[drive] = 6250; } /*Double density*/
	        else if (size <= (640*1024))   { d86f[drive].sectors = 8;  d86f[drive].tracks = 80; bit_rate_300 = 250; raw_tsize[drive] = 6250; } /*Double density 640k*/
	        else if (size < (1024*1024))   { d86f[drive].sectors = 9;  d86f[drive].tracks = 80; bit_rate_300 = 250; raw_tsize[drive] = 6250; } /*Double density*/
	        else if (size <= 1228800)      { d86f[drive].sectors = 15; d86f[drive].tracks = 80; bit_rate_300 = (500.0 * 300.0) / 360.0; raw_tsize[drive] = 10416; } /*High density 1.2MB*/
	        else if (size <= 1261568)      { d86f[drive].sectors =  8; d86f[drive].tracks = 77; d86f[drive].sector_size = 1024; bit_rate_300 = (500.0 * 300.0) / 360.0; raw_tsize[drive] = 10416; } /*High density 1.25MB Japanese format*/
	        else if (size <= (0x1A4000-1)) { d86f[drive].sectors = 18; d86f[drive].tracks = 80; bit_rate_300 = 500; raw_tsize[drive] = 12500; } /*High density (not supported by Tandy 1000)*/
	        else if (size <= 1556480)      { d86f[drive].sectors = 19; d86f[drive].tracks = 80; bit_rate_300 = 500; raw_tsize[drive] = 12500; } /*High density (not supported by Tandy 1000)*/
	        else if (size <= 1638400)      { d86f[drive].sectors = 10; d86f[drive].tracks = 80; d86f[drive].sector_size = 1024; bit_rate_300 = 500; raw_tsize[drive] = 12500; } /*High density (not supported by Tandy 1000)*/
	        // else if (size == 1884160)      { d86f[drive].sectors = 23; d86f[drive].tracks = 80; bit_rate_300 = 500; } /*XDF format - used by OS/2 Warp*/
	        // else if (size == 1763328)      { d86f[drive].sectors = 21; d86f[drive].tracks = 82; bit_rate_300 = 500; } /*XDF format - used by OS/2 Warp*/
	        else if (size <= 2000000)   { d86f[drive].sectors = 21; d86f[drive].tracks = 80; bit_rate_300 = 500; raw_tsize[drive] = 12500; } /*DMF format - used by Windows 95 - changed by OBattler to 2000000, ie. the real unformatted capacity @ 500 kbps and 300 rpm */
	        else if (size <= 2949120)   { d86f[drive].sectors = 36; d86f[drive].tracks = 80; bit_rate_300 = 1000; raw_tsize[drive] = 25000; } /*E density*/

		temp_rate = 2;
		bpb_bps = d86f[drive].sector_size;
		bpt = bpb_bps * d86f[drive].sectors;
		if (bpt <= (maximum_sectors[sector_size_code(bpb_bps)][0] * bpb_bps))
		{
			temp_rate = 2;
			raw_tsize[drive] = 5208;
		}
		else if (bpt <= (maximum_sectors[sector_size_code(bpb_bps)][1] * bpb_bps))
		{
			temp_rate = 2;
			raw_tsize[drive] = 6250;
		}
		else if (bpt <= (maximum_sectors[sector_size_code(bpb_bps)][2] * bpb_bps))
		{
			temp_rate = 1;
			raw_tsize[drive] = 7500;
		}
		else if (bpt <= (maximum_sectors[sector_size_code(bpb_bps)][3] * bpb_bps))
		{
			if (bpb_bps == 512)  max_spt = (bit_rate_300 == 500) ? 21 : 17;
			temp_rate = (bit_rate_300 == 500) ? 0 : 4;
			raw_tsize[drive] = (bit_rate_300 == 500) ? 12500 : 10416;
		}
		else if (bpt <= (maximum_sectors[sector_size_code(bpb_bps)][4] * bpb_bps))
		{
			if (bpb_bps == 512)  max_spt = 21;
			pclog("max_spt is %i\n", max_spt);
			temp_rate = 0;
			raw_tsize[drive] = 12500;
		}
		else if (bpt <= (maximum_sectors[sector_size_code(bpb_bps)][5] * bpb_bps))
		{
			if (bpb_bps == 512)  max_spt = 41;
			temp_rate = 3;
			raw_tsize[drive] = 25000;
		}
		else					/* Image too big, eject */
		{
			pclog("Image is bigger than can fit on an ED floppy, ejecting...\n");
			fclose(d86f[drive].f);
			return;
		}

		pclog("Temporary rate: %i (%i bytes per track)\n", temp_rate, bpt);

		d86f[drive].xdf_type = 0;
	}
	else
	{
		/* The BPB readings appear to be valid, so let's set the values. */
		/* Number of tracks = number of total sectors divided by sides times sectors per track. */
		if (fdi)
		{
			/* The image is a Japanese FDI, therefore we read the number of tracks from the header. */
			fseek(d86f[drive].f, 0x1C, SEEK_SET);
			fread(&(d86f[drive].tracks), 1, 4, d86f[drive].f);
		}
		else
		{
			d86f[drive].tracks = ((uint32_t) bpb_total) / (((uint32_t) bpb_sides) * ((uint32_t) bpb_sectors));
		}
		/* The rest we just set directly from the BPB. */
		d86f[drive].sectors = bpb_sectors;
		d86f[drive].sides = bpb_sides;
		/* The sector size. */
		d86f[drive].sector_size = bpb_bps;
		/* Now we calculate bytes per track, which is bpb_sectors * bpb_bps. */
		bpt = (uint32_t) bpb_sectors * (uint32_t) bpb_bps;
		/* Now we should be able to calculate the bit rate. */
		pclog("The image has %i bytes per track\n", bpt);

		temp_rate = 2;
		if (bpt <= (maximum_sectors[sector_size_code(bpb_bps)][0] * bpb_bps))
		{
			bit_rate_300 = ((250.0 * 300.0) / 360.0);
			temp_rate = 2;
			raw_tsize[drive] = 5208;
		}
		else if (bpt <= (maximum_sectors[sector_size_code(bpb_bps)][1] * bpb_bps))
		{
			bit_rate_300 = 250;
			temp_rate = 2;
			raw_tsize[drive] = 6250;
		}
		else if (bpt <= (maximum_sectors[sector_size_code(bpb_bps)][2] * bpb_bps))
		{
			bit_rate_300 = 300;
			temp_rate = 1;
			raw_tsize[drive] = 7500;
		}
		else if (bpt <= (maximum_sectors[sector_size_code(bpb_bps)][3] * bpb_bps))
		{
			bit_rate_300 = (bpb_mid == 0xF0) ? 500 : ((500.0 * 300.0) / 360.0);
			if (bpb_bps == 512)  max_spt = (bit_rate_300 == 500) ? 21 : 17;
			temp_rate = (bit_rate_300 == 500) ? 0 : 4;
			raw_tsize[drive] = (bit_rate_300 == 500) ? 12500 : 10416;
		}
		else if (bpt <= (maximum_sectors[sector_size_code(bpb_bps)][4] * bpb_bps))
		{
			bit_rate_300 = 500;
			if (bpb_bps == 512)  max_spt = 21;
			pclog("max_spt is %i\n", max_spt);
			temp_rate = 0;
			raw_tsize[drive] = 12500;
		}
		else if (bpt <= (maximum_sectors[sector_size_code(bpb_bps)][5] * bpb_bps))
		{
			bit_rate_300 = 1000;
			if (bpb_bps == 512)  max_spt = 41;
			temp_rate = 3;
			raw_tsize[drive] = 25000;
		}
		else					/* Image too big, eject */
		{
			pclog("Image is bigger than can fit on an ED floppy, ejecting...\n");
			fclose(d86f[drive].f);
			return;
		}

		if (bpb_bps == 512)			/* BPB reports 512 bytes per sector, let's see if it's XDF or not */
		{
			if (bit_rate_300 <= 300)	/* Double-density disk, not XDF */
			{
				d86f[drive].xdf_type = 0;
			}
			else
			{
				pclog("bpb_sectors is %i\n", bpb_sectors);
				if (bpb_sectors > max_spt)
				{
					switch(bpb_sectors)
					{
						case 19:	/* High density XDF @ 360 rpm */
							d86f[drive].xdf_type = 1;
							break;
						case 23:	/* High density XDF @ 300 rpm */
							d86f[drive].xdf_type = 2;
							pclog("XDF type is 2 @ %i kbps\n", bit_rate_300);
							break;
#if 0
						case 24:	/* High density XXDF @ 300 rpm */
							d86f[drive].xdf_type = 4;
							break;
#endif
						case 46:	/* Extended density XDF */
							d86f[drive].xdf_type = 3;
							break;
#if 0
						case 48:	/* Extended density XXDF */
							d86f[drive].xdf_type = 5;
							break;
#endif
						default:	/* Unknown, as we're beyond maximum sectors, get out */
							fclose(d86f[drive].f);
							return;
					}
				}
				else			/* Amount of sectors per track that fits into a track, therefore not XDF */
				{
					d86f[drive].xdf_type = 0;
				}
			}
		}
		else					/* BPB reports sector size other than 512, can't possibly be XDF */
		{
			d86f[drive].xdf_type = 0;
		}
	}

	gap2_size[drive] = (temp_rate == 3) ? 41 : 22;
	pclog("GAP2 size: %i bytes\n", gap2_size[drive]);
	gap3_size[drive] = gap3_sizes[temp_rate][sector_size_code(d86f[drive].sector_size)][d86f[drive].sectors];
	if (gap3_size)
	{
		pclog("GAP3 size: %i bytes\n", gap3_size[drive]);
	}
	else
	{
		// fclose(d86f[drive].f);
		gap3_size[drive] = 40;
		pclog("WARNING: Floppy image of unknown format was inserted into drive %c:!\n", drive + 0x41);
	}
	gap4_size[drive] = raw_tsize[drive] - (((pre_gap + gap2_size[drive] + pre_data + d86f[drive].sector_size + post_gap + gap3_size[drive]) * d86f[drive].sectors) + pre_track);
	pclog("GAP4 size: %i bytes\n", gap4_size[drive]);
	if (d86f[drive].xdf_type)
	{
		gap4_size[drive] = 1;
	}

	if (bit_rate_300 == 250)
	{
		d86f[drive].hole = 0;
		/* If drive does not support 300 RPM, the medium is to be read at a period of 26 (300 kbps). */
		d86f[drive].byte_period = 29;
	}
	else if (bit_rate_300 == 300)
	{
		d86f[drive].hole = 0;
		d86f[drive].byte_period = 26;
	}
	else if (bit_rate_300 == 1000)
	{
		d86f[drive].hole = 2;
		d86f[drive].byte_period = 8;
	}
	else if (bit_rate_300 < 250)
	{
		d86f[drive].hole = 0;
		d86f[drive].byte_period = 32;
	}
	else
	{
		d86f[drive].hole = 1;
		d86f[drive].byte_period = 16;
	}

	if (d86f[drive].xdf_type)			/* In case of XDF-formatted image, write-protect */
	{
                writeprot[drive] = 1;
	        fwriteprot[drive] = writeprot[drive];
	}

        drives[drive].seek        = d86f_seek;
        drives[drive].readsector  = disc_sector_readsector;
        drives[drive].writesector = disc_sector_writesector;
        drives[drive].readaddress = disc_sector_readaddress;
        drives[drive].hole        = d86f_hole;
        drives[drive].byteperiod  = d86f_byteperiod;
        drives[drive].poll        = disc_sector_poll;
        drives[drive].format      = disc_sector_format;
        drives[drive].realtrack   = d86f_realtrack;
        drives[drive].stop        = disc_sector_stop;
        disc_sector_writeback[drive] = d86f_writeback;
        
        d86f[drive].bitcell_period_300rpm = 1000000.0 / bit_rate_300*2.0;
        pclog("bit_rate_300=%g\n", bit_rate_300);
        pclog("bitcell_period_300=%g\n", d86f[drive].bitcell_period_300rpm);
//        d86f[drive].bitcell_period_300rpm = disc_get_bitcell_period(d86f[drive].rate);
        pclog("d86f_load %d %p sectors=%i tracks=%i sides=%i sector_size=%i hole=%i\n", drive, drives, d86f[drive].sectors, d86f[drive].tracks, d86f[drive].sides, d86f[drive].sector_size, d86f[drive].hole);
}

int d86f_hole(int drive)
{
	return d86f[drive].hole;
}

int d86f_byteperiod(int drive)
{
	if (d86f[drive].byte_period == 29)
	{
		return (fdd_get_type(drive) & 1) ? 32 : 26;
	}
	return d86f[drive].byte_period;
}

void d86f_close(int drive)
{
        if (d86f[drive].f)
                fclose(d86f[drive].f);
        d86f[drive].f = NULL;
}

void d86f_seek(int drive, int track)
{
        int side;
	int current_xdft = d86f[drive].xdf_type - 1;

	uint8_t sectors_fat, effective_sectors, sector_gap;		/* Needed for XDF */

	int sector, current_pos, sh, sr, spos, sside;
        
        if (!d86f[drive].f)
                return;
        // pclog("Seek drive=%i track=%i sectors=%i sector_size=%i sides=%i\n", drive, track, d86f[drive].sectors,d86f[drive].sector_size, d86f[drive].sides);
//        pclog("  %i %i\n", drive_type[drive], d86f[drive].tracks);
#ifdef MAINLINE
        if ((d86f[drive].tracks <= 41) && fdd_doublestep_40(drive))
#else
        if ((d86f[drive].tracks <= 43) && fdd_doublestep_40(drive))
#endif
                track /= 2;

	// pclog("Disk seeked to track %i\n", track);
        disc_track[drive] = track;

        if (d86f[drive].sides == 2)
        {
                fseek(d86f[drive].f, d86f[drive].base + (track * d86f[drive].sectors * d86f[drive].sector_size * 2), SEEK_SET);
		// pclog("Seek: Current file position (H0) is: %08X\n", ftell(d86f[drive].f));
                fread(d86f[drive].track_data[0], d86f[drive].sectors * d86f[drive].sector_size, 1, d86f[drive].f);
		// pclog("Seek: Current file position (H1) is: %08X\n", ftell(d86f[drive].f));
                fread(d86f[drive].track_data[1], d86f[drive].sectors * d86f[drive].sector_size, 1, d86f[drive].f);
        }
        else
        {
                fseek(d86f[drive].f, d86f[drive].base + (track * d86f[drive].sectors * d86f[drive].sector_size), SEEK_SET);
                fread(d86f[drive].track_data[0], d86f[drive].sectors * d86f[drive].sector_size, 1, d86f[drive].f);
        }
        
        disc_sector_reset(drive, 0);
        disc_sector_reset(drive, 1);
        
	if (d86f[drive].xdf_type)
	{
		sectors_fat = xdf_track0[current_xdft][0];
		effective_sectors = xdf_track0[current_xdft][1];
		sector_gap = xdf_track0[current_xdft][2];

		if (!track)
		{
			/* Track 0, register sectors according to track 0 layout. */
			current_pos = 0;
			for (sector = 0; sector < (d86f[drive].sectors * 2); sector++)
			{
				if (xdf_track0_layout[current_xdft][sector])
				{
					sh = xdf_track0_layout[current_xdft][sector] & 0xFF;
					sr = xdf_track0_layout[current_xdft][sector] >> 8;
					spos = current_pos;
					sside = 0;
					if (spos > (d86f[drive].sectors * d86f[drive].sector_size))
					{
						spos -= (d86f[drive].sectors * d86f[drive].sector_size);
						sside = 1;
					}
		                        disc_sector_add(drive, sh, track, sh, sr, 2,
       			                                d86f[drive].bitcell_period_300rpm, 
               			                        &d86f[drive].track_data[sside][spos]);
				}
				current_pos += 512;
			}
#if 0
			/* Track 0, register sectors according to track 0 map. */
			/* First, the "Side 0" buffer, will also contain one sector from side 1. */
			current_pos = 0;
	                for (sector = 0; sector < sectors_fat; sector++)
			{
				if ((sector+0x81) >= 0x91)
				{
		                        disc_sector_add(drive, 0, track, 0, sector+0x85, 2,
       			                                d86f[drive].bitcell_period_300rpm, 
               			                        &d86f[drive].track_data[0][current_pos]);
				}
				else
				{
		                        disc_sector_add(drive, 0, track, 0, sector+0x81, 2,
       			                                d86f[drive].bitcell_period_300rpm, 
               			                        &d86f[drive].track_data[0][current_pos]);
				}
				current_pos += 512;
			}
                        disc_sector_add(drive, 1, track, 1, 0x81, 2,
				d86f[drive].bitcell_period_300rpm, 
				&d86f[drive].track_data[0][current_pos]);
			current_pos += 512;
	                for (sector = 0; sector < 7; sector++)
			{
	                        disc_sector_add(drive, 0, track, 0, sector+1, 2,
       		                                d86f[drive].bitcell_period_300rpm, 
               		                        &d86f[drive].track_data[0][current_pos]);
				current_pos += 512;
			}
                        disc_sector_add(drive, 0, track, 0, 0x9B, 2,
				d86f[drive].bitcell_period_300rpm, 
				&d86f[drive].track_data[0][current_pos]);
			current_pos += 512;
                        disc_sector_add(drive, 0, track, 0, 0x9C, 2,
				d86f[drive].bitcell_period_300rpm, 
				&d86f[drive].track_data[0][current_pos]);
			current_pos += 512;
                        disc_sector_add(drive, 0, track, 0, 0x9D, 2,
				d86f[drive].bitcell_period_300rpm, 
				&d86f[drive].track_data[0][current_pos]);
			current_pos += 512;
                        disc_sector_add(drive, 0, track, 0, 0x9E, 2,
				d86f[drive].bitcell_period_300rpm, 
				&d86f[drive].track_data[0][current_pos]);
			current_pos += 512;
			/* Now the "Side 1" buffer, will also contain one sector from side 0. */
			current_pos = 0;
	                for (sector = 0; (sector < effective_sectors - 1); sector++)
			{
	                        disc_sector_add(drive, 1, track, 1, sector+0x82, 2,
       		                                d86f[drive].bitcell_period_300rpm, 
               		                        &d86f[drive].track_data[1][current_pos]);
				current_pos += 512;
			}
                        disc_sector_add(drive, 0, track, 0, 8, 2,
				d86f[drive].bitcell_period_300rpm, 
				&d86f[drive].track_data[1][current_pos]);
			current_pos += 512;
#endif
		}
		else
		{
			/* Non-zero track, this will have sectors of various sizes. */
			/* First, the "Side 0" buffer. */
			current_pos = 0;
	                for (sector = 0; sector < xdf_spt[current_xdft]; sector++)
			{
	                        disc_sector_add(drive, xdf_map[current_xdft][sector][0], track, xdf_map[current_xdft][sector][0],
						xdf_map[current_xdft][sector][2] + 0x80, xdf_map[current_xdft][sector][2],
       		                                d86f[drive].bitcell_period_300rpm, 
               		                        &d86f[drive].track_data[0][current_pos]);
				current_pos += (128 << xdf_map[current_xdft][sector][2]);
			}
			/* Then, the "Side 1" buffer. */
			current_pos = 0;
	                for (sector = xdf_spt[current_xdft]; sector < (xdf_spt[current_xdft] << 1); sector++)
			{
	                        disc_sector_add(drive, xdf_map[current_xdft][sector][0], track, xdf_map[current_xdft][sector][0],
						xdf_map[current_xdft][sector][2] + 0x80, xdf_map[current_xdft][sector][2],
       		                                d86f[drive].bitcell_period_300rpm, 
               		                        &d86f[drive].track_data[1][current_pos]);
				current_pos += (128 << xdf_map[current_xdft][sector][2]);
			}
		}
	}
	else
	{                
	        for (side = 0; side < d86f[drive].sides; side++)
       		{
	                for (sector = 0; sector < d86f[drive].sectors; sector++)
        	                disc_sector_add(drive, side, track, side, sector+1, d86f_sector_size_code(drive),
       	        	                        d86f[drive].bitcell_period_300rpm, 
               	        	                &d86f[drive].track_data[side][sector * d86f[drive].sector_size]);
		}
	}
	for (side = d86f[drive].sides - 1; side >= 0; side--)
	{
		disc_sector_prepare_track_layout(drive, side);
	}
}

void d86f_writeback(int drive, int track)
{
        if (!d86f[drive].f)
                return;
                
        if (d86f[drive].xdf_type)
                return; /*Should never happen*/

        if (d86f[drive].sides == 2)
        {
                fseek(d86f[drive].f, d86f[drive].base + (track * d86f[drive].sectors * d86f[drive].sector_size * 2), SEEK_SET);
                fwrite(d86f[drive].track_data[0], d86f[drive].sectors * d86f[drive].sector_size, 1, d86f[drive].f);
                fwrite(d86f[drive].track_data[1], d86f[drive].sectors * d86f[drive].sector_size, 1, d86f[drive].f);
        }
        else
        {
                fseek(d86f[drive].f, d86f[drive].base + (track * d86f[drive].sectors * d86f[drive].sector_size), SEEK_SET);
                fwrite(d86f[drive].track_data[0], d86f[drive].sectors * d86f[drive].sector_size, 1, d86f[drive].f);
        }
}

void d86f_reset(int drive, int side)
{
        d86f_count[drive][side] = 0;

	if (side == 0)
	{
		d86f_reset_state(drive);
		// cur_track_pos[drive] = 0;
		d86f_state[drive] = STATE_SEEK;
	}
}

void d86f_add(int drive, int side, uint8_t c, uint8_t h, uint8_t r, uint8_t n, int rate, uint8_t *data)
{
        sector_t *s = &d86f_data[drive][side][d86f_count[drive][side]];
//pclog("d86f_add: drive=%i side=%i %i r=%i\n", drive, side,         d86f_count[drive][side],r );
        if (d86f_count[drive][side] >= MAX_SECTORS)
                return;

        s->c = c;
        s->h = h;
        s->r = r;
        s->n = n;
	// pclog("Adding sector: %i %i %i %i\n", c, h, r, n);
        s->rate = rate;
        s->data = data;
        
        d86f_count[drive][side]++;
}

static int get_bitcell_period(int drive)
{
        // return (d86f_data[drive][d86f_side[drive]][cur_sector[drive]].rate * 300) / fdd_getrpm(drive);
	return ((&d86f_data[drive][0][0])->rate * 300) / fdd_getrpm(drive);
        return (cur_rate[drive] * 300) / fdd_getrpm(drive);
}

void d86f_readsector(int drive, int sector, int track, int side, int rate, int sector_size)
{
        // pclog("d86f_readsector: fdc_period=%i d86f_period=%i rate=%i sector=%i track=%i side=%i\n", fdc_get_bitcell_period(), get_bitcell_period(drive), rate, sector, track, side);

        d86f_track[drive] = track;
        d86f_side[drive]  = side;
        d86f_drive = drive;
        d86f_sector[drive] = sector;
	d86f_n[drive] = sector_size;
	d86f_reset_state(drive);
        if (sector == SECTOR_FIRST)
                d86f_state[drive] = STATE_READ_FIND_FIRST_SECTOR;
        else if (sector == SECTOR_NEXT)
                d86f_state[drive] = STATE_READ_FIND_NEXT_SECTOR;
        else
                d86f_state[drive] = STATE_READ_FIND_SECTOR;
}

void d86f_writesector(int drive, int sector, int track, int side, int rate, int sector_size)
{
//        pclog("d86f_writesector: fdc_period=%i d86f_period=%i rate=%i\n", fdc_get_bitcell_period(), get_bitcell_period(), rate);

        d86f_track[drive] = track;
        d86f_side[drive]  = side;
        d86f_drive = drive;
        d86f_sector[drive] = sector;
	d86f_n[drive] = sector_size;
	d86f_reset_state(drive);
        d86f_state[drive] = STATE_WRITE_FIND_SECTOR;
}

void d86f_readaddress(int drive, int track, int side, int rate)
{
//        pclog("d86f_readaddress: fdc_period=%i d86f_period=%i rate=%i track=%i side=%i\n", fdc_get_bitcell_period(), get_bitcell_period(), rate, track, side);

        d86f_track[drive] = track;
        d86f_side[drive]  = side;
        d86f_drive = drive;
	d86f_reset_state(drive);
        d86f_state[drive] = STATE_READ_FIND_ADDRESS;
}

void d86f_format(int drive, int track, int side, int rate, uint8_t fill)
{
        d86f_track[drive] = track;
        d86f_side[drive]  = side;
        d86f_drive = drive;
        d86f_fill[drive]  = fill;
	d86f_reset_state(drive);
        d86f_state[drive] = STATE_FORMAT_FIND;
}

void d86f_stop(int drive)
{
        d86f_state[drive] = STATE_IDLE;
}

static void index_pulse(int drive)
{
	if (d86f_state[drive] != STATE_IDLE)  fdc_indexpulse();
}

// char *track_buffer[2][2][25512];
char track_layout[2][2][25512];

int id_positions[2][2][MAX_SECTORS];

/* 0 = MFM, 1 = FM, 2 = MFM perpendicular, 3 = reserved */
/* 4 = ISO, 0 = IBM */
int media_type = 0;

#define BYTE_GAP	0
#define BYTE_SYNC	1
#define BYTE_IAM	2
#define BYTE_IDAM	3
#define BYTE_ID		4
#define BYTE_ID_CRC	5
#define BYTE_DATA_AM	6
#define BYTE_DATA	7
#define BYTE_DATA_CRC	8
#define BYTE_SECTOR_GAP	9
#define BYTE_GAP3	10
#define BYTE_AM_SYNC	11
#define BYTE_INDEX_HOLE	12
#define BYTE_EMPTY	255

void d86f_prepare_track_layout(int drive, int side)
{
	sector_t *s;
	int i = 0;
	int j = 0;
	int real_gap0_len = ((media_type & 3) == 1) ? 40 : 80;
	int sync_len = ((media_type & 3) == 1) ? 6 : 12;
	int am_len = ((media_type & 3) == 1) ? 1 : 4;
	int real_gap1_len = ((media_type & 3) == 1) ? 26 : 50;
	// track_layout[drive][side] = (char *) malloc(raw_tsize[drive]);
	// id_positions[drive][side] = (int *) malloc(d86f_count[drive][side] * 4);
	memset(track_layout[drive][side], BYTE_GAP, raw_tsize[drive]);
	memset(id_positions[drive][side], 0, 1024);
	i = 0;
	if (!(media_type & 4))
	{
		memset(track_layout[drive][side] + i, BYTE_INDEX_HOLE, 1);
		i++;
		memset(track_layout[drive][side] + i, BYTE_GAP, real_gap0_len - 1);
		i += real_gap0_len - 1;
		memset(track_layout[drive][side] + i, BYTE_SYNC, sync_len);
		i += sync_len;
		if ((media_type & 3) != 1)
		{
			memset(track_layout[drive][side] + i, BYTE_AM_SYNC, 3);
			i += 3;
		}
		memset(track_layout[drive][side] + i, BYTE_IAM, 1);
		i++;
		memset(track_layout[drive][side] + i, BYTE_GAP, real_gap1_len);
		i += real_gap1_len;
	}
	else
	{
		memset(track_layout[drive][side] + i, BYTE_INDEX_HOLE, 1);
		i++;
		memset(track_layout[drive][side] + i, BYTE_GAP, real_gap1_len - 1);
		i += real_gap1_len - 1;
	}
	for (j = 0; j < d86f_count[drive][side]; j++)
	{
		s = &d86f_data[drive][side][j];
		// pclog("Sector %i (%i)\n", j, s->n);
		memset(track_layout[drive][side] + i, BYTE_SYNC, sync_len);
		i += sync_len;
		if ((media_type & 3) != 1)
		{
			memset(track_layout[drive][side] + i, BYTE_AM_SYNC, 3);
			i += 3;
		}
		id_positions[drive][side][j] = i;
		memset(track_layout[drive][side] + i, BYTE_IDAM, 1);
		i++;
		memset(track_layout[drive][side] + i, BYTE_ID, 4);
		i += 4;
		memset(track_layout[drive][side] + i, BYTE_ID_CRC, 2);
		i += 2;
		memset(track_layout[drive][side] + i, BYTE_SECTOR_GAP, gap2_size[drive]);
		i += gap2_size[drive];
		memset(track_layout[drive][side] + i, BYTE_SYNC, sync_len);
		i += sync_len;
		if ((media_type & 3) != 1)
		{
			memset(track_layout[drive][side] + i, BYTE_AM_SYNC, 3);
			i += 3;
		}
		memset(track_layout[drive][side] + i, BYTE_DATA_AM, 1);
		i++;
		memset(track_layout[drive][side] + i, BYTE_DATA, (128 << ((int) s->n)));
		i += (128 << ((int) s->n));
		memset(track_layout[drive][side] + i, BYTE_DATA_CRC, 2);
		i += 2;
		memset(track_layout[drive][side] + i, BYTE_GAP3, gap3_size[drive]);
		i += gap3_size[drive];
	}

	if (side == 0)  d86f_state[drive] = STATE_IDLE;

#if 0
	FILE *f = fopen("layout.dmp", "wb");
	fwrite(track_layout[drive][side], 1, raw_tsize[drive], f);
	fclose(f);
	fatal("good getpccache!\n");
#endif
}

int d86f_reset_state(int drive)
{
	id_counter[drive] = data_counter[drive] = index_count[drive] = gap3_counter[drive] = cur_rate[drive] = 0;
	last_sector[drive] = NULL;
}

int d86f_find_sector(int drive)
{
	int side = d86f_side[drive];
	int i = 0;
	for (i = 0; i < d86f_count[drive][side]; i++)
	{
		if (id_positions[drive][side][i] == cur_track_pos[drive])
		{
			return i;
		}
	}
	return -1;
}

int d86f_match(int drive)
{
	int temp;
	if (last_sector[drive] == NULL)  return 0;
	temp = (d86f_track[drive] == last_sector[drive]->c);
	temp = temp && (d86f_side[drive] == last_sector[drive]->h);
	temp = temp && (d86f_sector[drive] == last_sector[drive]->r);
	if (d86f_n[drive])
	{
		temp = temp && (d86f_n[drive] == last_sector[drive]->n);
	}
	return temp;
}

int d86f_can_read_address(int drive)
{
	int temp;
	temp = (fdc_get_bitcell_period() == get_bitcell_period(drive));
	temp = temp && fdd_can_read_medium(drive ^ fdd_swap);
	return temp;
}

int d86f_can_format(int drive)
{
	int temp;
	temp = !writeprot[drive];
	temp = temp && !swwp;
	temp = temp && d86f_can_read_address(drive);
	temp = temp && (fdc_get_format_sectors() == d86f_count[drive][d86f_side[drive]]);
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

int d86f_read_state(int drive)
{
	int temp;
	temp = (d86f_state[drive] == STATE_READ_SECTOR);
	temp = temp || (d86f_state[drive] == STATE_READ_FIRST_SECTOR);
	temp = temp || (d86f_state[drive] == STATE_READ_NEXT_SECTOR);
	return temp;
}

void d86f_poll()
{
        sector_t *s;
        int data;
	int drive = d86f_drive;
	int side = d86f_side[drive];
	int found_sector = 0;
	int b = 0;

	if (d86f_state[drive] == STATE_SEEK)
	{
		cur_track_pos[drive]++;
		cur_track_pos[drive] %= raw_tsize[drive];
		return;
	}

	if (d86f_state[drive] == STATE_FORMAT_FIND)
	{
		if (!(d86f_can_format(drive)))
		{
			if (d86f_can_read_address(drive))
			{
				pclog("d86f_poll(): Disk is write protected or attempting to format wrong number of sectors per track\n");
				fdc_writeprotect();
			}
			else
			{
				pclog("d86f_poll(): Unable to format at the requested density or bitcell period\n");
				fdc_notfound();
			}
                        d86f_state[drive] = STATE_IDLE;
			d86f_reset_state(drive);
			cur_track_pos[drive]++;
			cur_track_pos[drive] %= raw_tsize[drive];
			return;
		}
	}
	// if (d86f_state[drive] != STATE_IDLE)  pclog("%04X: %01X\n", cur_track_pos[drive], track_layout[drive][side][cur_track_pos[drive]]);
	if (track_layout[drive][side][cur_track_pos[drive]] == BYTE_GAP)
	{
		if (d86f_read_state(drive) || (d86f_state[drive] == STATE_WRITE_SECTOR) || (d86f_state[drive] == STATE_FORMAT))
		{
			/* We're at GAP4b or even GAP4a and still in a read, write, or format state, this means we've overrun the gap.
			   Return with sector not found. */
			// pclog("d86f_poll(): Gap overrun at GAP4\n");
			fdc_notfound();
                        d86f_state[drive] = STATE_IDLE;
			d86f_reset_state(drive);
			cur_track_pos[drive]++;
			cur_track_pos[drive] %= raw_tsize[drive];
			return;
		}
	}
	else if (track_layout[drive][side][cur_track_pos[drive]] == BYTE_INDEX_HOLE)
	{
		index_pulse(drive);
		if (d86f_state[drive] != STATE_IDLE)  index_count[drive]++;
		if (d86f_read_state(drive) || (d86f_state[drive] == STATE_WRITE_SECTOR) || (d86f_state[drive] == STATE_FORMAT))
		{
			/* We're at the index address mark and still in a read, write, or format state, this means we've overrun the gap.
			   Return with sector not found. */
			// pclog("d86f_poll(): Gap overrun at IAM\n");
			fdc_notfound();
                        d86f_state[drive] = STATE_IDLE;
			d86f_reset_state(drive);
			cur_track_pos[drive]++;
			cur_track_pos[drive] %= raw_tsize[drive];
			return;
		}
	}
	else if (track_layout[drive][side][cur_track_pos[drive]] == BYTE_IDAM)
	{
		found_sector = d86f_find_sector(drive);
		// pclog("Found sector: %i\n", found_sector);
		cur_sector[drive] = found_sector;
		last_sector[drive] = &d86f_data[drive][d86f_side[drive]][found_sector];
		cur_rate[drive] = last_sector[drive]->rate;
		if (!(d86f_can_read_address(drive)))  last_sector[drive] = NULL;
		if (d86f_read_state(drive) || (d86f_state[drive] == STATE_WRITE_SECTOR) || (d86f_state[drive] == STATE_FORMAT))
		{
			/* We're at a sector ID address mark and still in a read, write, or format state, this means we've overrun the gap.
			   Return with sector not found. */
			pclog("d86f_poll(): Gap (%i) overrun at IDAM\n", fdc_get_gap());
			fdc_notfound();
                        d86f_state[drive] = STATE_IDLE;
			d86f_reset_state(drive);
			cur_track_pos[drive]++;
			cur_track_pos[drive] %= raw_tsize[drive];
			return;
		}
		if ((d86f_state[drive] == STATE_FORMAT_FIND) && d86f_can_read_address(drive))  d86f_state[drive] = STATE_FORMAT;
		id_counter[drive] = 0;
	}
	else if (track_layout[drive][side][cur_track_pos[drive]] == BYTE_ID)
	{
		id_counter[drive]++;
	}
	else if (track_layout[drive][side][cur_track_pos[drive]] == BYTE_ID_CRC)
	{
		id_counter[drive]++;
		if (id_counter[drive] == 6)
		{
			/* ID CRC read, if state is read address, return address */
			if ((d86f_state[drive] == STATE_READ_FIND_ADDRESS) && !(d86f_can_read_address(drive)))
			{
				if (fdc_get_bitcell_period() != get_bitcell_period(drive))
				{
					pclog("Unable to read sector ID: Bitcell period mismatch (%i != %i)...\n", fdc_get_bitcell_period(), get_bitcell_period(drive));
				}
				else
				{
					pclog("Unable to read sector ID: Media type not supported by the drive...\n");
				}
			}
			if ((d86f_state[drive] == STATE_READ_FIND_ADDRESS) && d86f_can_read_address(drive))
			{
				// pclog("Reading sector ID...\n");
				fdc_sectorid(last_sector[drive]->c, last_sector[drive]->h, last_sector[drive]->r, last_sector[drive]->n, 0, 0);
				d86f_state[drive] = STATE_IDLE;
			}
			id_counter[drive] = 0;
		}
	}
	else if (track_layout[drive][side][cur_track_pos[drive]] == BYTE_DATA_AM)
	{
		data_counter[drive] = 0;
		switch (d86f_state[drive])
		{
			case STATE_READ_FIND_SECTOR:
				if (d86f_match(drive) && d86f_can_read_address(drive))  d86f_state[drive] = STATE_READ_SECTOR;
				break;
			case STATE_READ_FIND_FIRST_SECTOR:
				if ((cur_sector[drive] == 0) && d86f_can_read_address(drive))  d86f_state[drive] = STATE_READ_FIRST_SECTOR;
				break;
			case STATE_READ_FIND_NEXT_SECTOR:
				if (d86f_can_read_address(drive))  d86f_state[drive] = STATE_READ_NEXT_SECTOR;
				break;
			case STATE_WRITE_FIND_SECTOR:
				if (d86f_match(drive) && d86f_can_read_address(drive))  d86f_state[drive] = STATE_WRITE_SECTOR;
				break;
		}
	}
	else if (track_layout[drive][side][cur_track_pos[drive]] == BYTE_DATA)
	{
		if (d86f_read_state(drive) && (last_sector[drive] != NULL))
		{
			if (fdc_data(last_sector[drive]->data[data_counter[drive]]))
			{
				/* Data failed to be sent to the FDC, abort. */
				pclog("d86f_poll(): Unable to send further data to the FDC\n");
				d86f_state[drive] = STATE_IDLE;
				d86f_reset_state(drive);
				cur_track_pos[drive]++;
				cur_track_pos[drive] %= raw_tsize[drive];
				return;
			}
		}
		if ((d86f_state[drive] == STATE_WRITE_SECTOR) && (last_sector[drive] != NULL))
		{
	                data = fdc_getdata(cur_byte[drive] == ((128 << ((uint32_t) last_sector[drive]->n)) - 1));
        	        if (data == -1)
			{
				/* Data failed to be sent from the FDC, abort. */
				pclog("d86f_poll(): Unable to receive further data from the FDC\n");
				d86f_state[drive] = STATE_IDLE;
				d86f_reset_state(drive);
				cur_track_pos[drive]++;
				cur_track_pos[drive] %= raw_tsize[drive];
				return;
			}
			if (!disable_write)  last_sector[drive]->data[data_counter[drive]] = data;
		}
		if ((d86f_state[drive] == STATE_FORMAT) && (last_sector[drive] != NULL))
		{
	                if (!disable_write)  last_sector[drive]->data[data_counter[drive]] = d86f_fill[drive];
		}
		data_counter[drive]++;
		if (last_sector[drive] == NULL)
		{
			data_counter[drive] = 0;
		}
		else
		{
			data_counter[drive] %= (128 << ((uint32_t) last_sector[drive]->n));
			if (!data_counter[drive])
			{
				if (d86f_read_state(drive) && (last_sector[drive] != NULL))
				{
	        	                d86f_state[drive] = STATE_IDLE;
	                        	fdc_finishread(drive);
				}
				if ((d86f_state[drive] == STATE_WRITE_SECTOR) && (last_sector[drive] != NULL))
				{
	                	        d86f_state[drive] = STATE_IDLE;
        	                	if (!disable_write)  d86f_writeback[drive](drive, d86f_track[drive]);
	                	        fdc_finishread(drive);
				}
				if ((d86f_state[drive] == STATE_FORMAT) && (last_sector[drive] != NULL))
				{
                		        d86f_state[drive] = STATE_IDLE;
        		                if (!disable_write)  d86f_writeback[drive](drive, d86f_track[drive]);
		                        fdc_finishread(drive);
				}
			}
		}
	}
	else if (track_layout[drive][side][cur_track_pos[drive]] == BYTE_GAP3)
	{
		if (gap3_counter[drive] == fdc_get_gap())
		{
		}
		gap3_counter[drive]++;
		gap3_counter[drive] %= gap3_size[drive];
		// pclog("GAP3 counter = %i\n", gap3_counter[drive]);
	}
	else if (track_layout[drive][side][cur_track_pos[drive]] == BYTE_GAP)
	{
		if (last_sector[drive] != NULL)  last_sector[drive] = NULL;
	}
	b = track_layout[drive][side][cur_track_pos[drive]];
	cur_track_pos[drive]++;
	cur_track_pos[drive] %= raw_tsize[drive];
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
				d86f_reset_state(drive);
				return;
			}
		}
	}
	if ((b != BYTE_GAP3) && (track_layout[drive][side][cur_track_pos[drive]] == BYTE_GAP3))
	{
		gap3_counter[drive] = 0;
	}
}
