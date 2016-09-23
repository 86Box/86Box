#include "ibm.h"
#include "fdd.h"
#include "disc.h"
#include "disc_img.h"
#include "disc_sector.h"

static struct
{
        FILE *f;
        // uint8_t track_data[2][20*1024];
        uint8_t track_data[2][50000];
        int sectors, tracks, sides;
        int sector_size;
        int rate;
	int xdf_type;	/* 0 = not XDF, 1-5 = one of the five XDF types */
	int hole;
	int byte_period;
        double bitcell_period_300rpm;
	uint32_t base;
} img[2];

static uint8_t xdf_track0[3][3];
static uint8_t xdf_spt[3] = { 6, 8, 8 };
static uint8_t xdf_map[3][24][3];
static uint16_t xdf_track0_layout[3][92] = {	{ 0x8100, 0x8200, 0x8300, 0x8400, 0x8500, 0x8600, 0x8700, 0x8800,
						  0x8101, 0x8201, 0x0100, 0x0200, 0x0300, 0x0400, 0x0500, 0x0600,
						  0x0700, 0x0800,      0, 0x8301, 0x8401, 0x8501, 0x8601, 0x8701,
						  0x8801, 0x8901, 0x8A01, 0x8B01, 0x8C01, 0x8D01, 0x8E01, 0x8F01,
						  0x9001 },	/* 5.25" 2HD */
						{ 0x8100, 0x8200, 0x8300, 0x8400, 0x8500, 0x8600, 0x8700, 0x8800,
						  0x8900, 0x8A00, 0x8B00, 0x8101, 0x0100, 0x0200, 0x0300, 0x0400,
						  0x0500, 0x0600, 0x0700, 0x0800,      0,      0,      0, 0x8201,
						  0x8301, 0x8401, 0x8501, 0x8601, 0x8701, 0x8801, 0x8901, 0x8A01,
						  0x8B01, 0x8C01, 0x8D01, 0x8E01, 0x8F01,      0,      0,      0,
						       0,      0, 0x9001, 0x9101, 0x9201, 0x9301 },	/* 3.5" 2HD */
						{ 0x8100, 0x8200, 0x8300, 0x8400, 0x8500, 0x8600, 0x8700, 0x8800,
						  0x8900, 0x8A00, 0x8B00, 0x8C00, 0x8D00, 0x8E00, 0x8F00, 0x9000,
						  0x9100, 0x9200, 0x9300, 0x9400, 0x9500, 0x9600, 0x9700, 0x0100,
						  0x0200, 0x0300, 0x0400, 0x0500, 0x0600, 0x0700, 0x0800,      0,
                                                       0,      0,      0,      0,      0,      0,      0,      0,
						       0,      0,      0,      0,      0, 0x9800, 0x9900, 0x9A00,
						  0x9B00, 0x9C00, 0x9D00, 0x9E00, 0x8101, 0x8201, 0x8301, 0x8401,
						  0x8501, 0x8601, 0x8701, 0x8801,      0,      0,      0,      0,
						  0x8901, 0x8A01, 0x8B01, 0x8C01, 0x8D01, 0x8E01, 0x8F01, 0x9001,
						  0x9101, 0x9201, 0x9301, 0x9401, 0x9501, 0x9601, 0x9701, 0x9801,
						  0x9901, 0x9A01, 0x9B01, 0x9C01, 0x9D01, 0x9E01, 0x9F01, 0xA001,
						  0xA101, 0xA201, 0xA301, 0xA401 },
						};
static uint16_t xdf_sector_pos[256];

/* First dimension is possible sector sizes (0 = 128, 7 = 16384), second is possible bit rates (250/360, 250, 300, 500/360, 500, 1000). */
/* Disks formatted at 250 kbps @ 360 RPM can be read with a 360 RPM single-RPM 5.25" drive by setting the rate to 250 kbps.
   Disks formatted at 300 kbps @ 300 RPM can be read with any 300 RPM single-RPM drive by setting the rate rate to 300 kbps. */
static uint8_t maximum_sectors[8][6] = { { 26, 31, 38, 53, 64, 118 },	/*   128 */
                                         { 15, 19, 23, 32, 38,  73 },	/*   256 */
                                         {  7, 10, 12, 19, 23,  46 },	/*   512 */
                                         {  3,  5,  6,  9, 11,  22 },	/*  1024 */
                                         {  2,  2,  3,  4,  5,  11 },	/*  2048 */
                                         {  1,  1,  1,  2,  2,   5 },	/*  4096 */
					 {  0,  0,  0,  1,  1,   3 },	/*  8192 */
					 {  0,  0,  0,  0,  0,   1 } };	/* 16384 */

static int gap3_sizes[5][8][256] = {	[0][1][16] = 0x54,
				[0][2][18] = 0x6C,
				[0][2][19] = 0x48,
				[0][2][20] = 0x2A,
				[0][2][21] = 0x0C,
				// [0][2][23] = 0x7A,
				[0][2][23] = 0x01,
				// [0][2][24] = 0x38,
				[2][1][10] = 0x32,
				[2][1][11] = 0x0C,
				[2][1][15] = 0x36,
				[2][1][16] = 0x32,
				[2][2][8]  = 0x58,
				[2][2][9]  = 0x50,
				[2][2][10] = 0x2E,
				[2][2][11] = 0x02,
				[2][2][21] = 0x1C,
				[2][3][4]  = 0xF0,
				[2][3][5]  = 0x74,
				[3][2][36] = 0x53,
				[3][2][39] = 0x20,
				// [3][2][46] = 0x0E,
				[3][2][46] = 0x01,
				// [3][2][48] = 0x51,
				[4][1][32] = 0x36,
				[4][2][15] = 0x54,
				[4][2][17] = 0x23,
				[4][2][18] = 0x02,
				// [4][2][19] = 0x29,
				[4][2][19] = 0x01,
				[4][3][8]  = 0x74,
				[4][3][9]  = 0x74,
				[4][3][10] = 0x74
};

/* Needed for formatting! */
int img_realtrack(int drive, int track)
{
        if ((img[drive].tracks <= 43) && fdd_doublestep_40(drive))
                track /= 2;

	return track;
}

void img_writeback(int drive);

static int sector_size_code(int sector_size)
{
	switch(sector_size)
	{
		case 128:
			return 0;
		case 256:
			return 1;
		default:
		case 512:
			return 2;
		case 1024:
			return 3;
		case 2048:
			return 4;
		case 4096:
			return 5;
		case 8192:
			return 6;
		case 16384:
			return 7;
	}
}

static int img_sector_size_code(int drive)
{
	return sector_size_code(img[drive].sector_size);
}

void img_init()
{
        memset(img, 0, sizeof(img));
//        adl[0] = adl[1] = 0;
}

static void add_to_map(uint8_t *arr, uint8_t p1, uint8_t p2, uint8_t p3)
{
	arr[0] = p1;
	arr[1] = p2;
	arr[2] = p3;
}

static int xdf_maps_initialized = 0;

static uint16_t xdf_trackx_layout[3][8] = {	{ 0x8300, 0x8600, 0x8201, 0x8200, 0x8601, 0x8301},
						{ 0x8300, 0x8400, 0x8601, 0x8200, 0x8201, 0x8600, 0x8401, 0x8301},
						{ 0x8300, 0x8400, 0x8500, 0x8700, 0x8301, 0x8401, 0x8501, 0x8701} };

void img_load(int drive, char *fn)
{
        int size;
        double bit_rate_300;
	uint16_t bpb_bps;
	uint16_t bpb_total;
	uint8_t bpb_mid;	/* Media type ID. */
	uint8_t bpb_sectors;
	uint8_t bpb_sides;
	uint32_t bpt;
	uint8_t max_spt;	/* Used for XDF detection. */
	int temp_rate;
	char ext[4];
	int fdi;

	ext[0] = fn[strlen(fn) - 3] | 0x60;
	ext[1] = fn[strlen(fn) - 2] | 0x60;
	ext[2] = fn[strlen(fn) - 1] | 0x60;
	ext[3] = 0;

	writeprot[drive] = 0;
        img[drive].f = fopen(fn, "rb+");
        if (!img[drive].f)
        {
                img[drive].f = fopen(fn, "rb");
                if (!img[drive].f)
                        return;
                writeprot[drive] = 1;
        }
	if (ui_writeprot[drive])
	{
                writeprot[drive] = 1;
	}
        fwriteprot[drive] = writeprot[drive];

	if (strcmp(ext, "fdi") == 0)
	{
		/* This is a Japanese FDI image, so let's read the header */
		pclog("img_load(): File is a Japanese FDI image...\n");
		fseek(img[drive].f, 0x10, SEEK_SET);
		fread(&bpb_bps, 1, 2, img[drive].f);
		fseek(img[drive].f, 0x0C, SEEK_SET);
		fread(&size, 1, 4, img[drive].f);
		bpb_total = size / bpb_bps;
		fseek(img[drive].f, 0x08, SEEK_SET);
		fread(&(img[drive].base), 1, 4, img[drive].f);
		fseek(img[drive].f, img[drive].base + 0x15, SEEK_SET);
		bpb_mid = fgetc(img[drive].f);
		if (bpb_mid < 0xF0)  bpb_mid = 0xF0;
		fseek(img[drive].f, 0x14, SEEK_SET);
		bpb_sectors = fgetc(img[drive].f);
		fseek(img[drive].f, 0x18, SEEK_SET);
		bpb_sides = fgetc(img[drive].f);

		fdi = 1;
	}
	else
	{
		/* Read the BPB */
		pclog("img_load(): File is a raw image...\n");
		fseek(img[drive].f, 0x0B, SEEK_SET);
		fread(&bpb_bps, 1, 2, img[drive].f);
		fseek(img[drive].f, 0x13, SEEK_SET);
		fread(&bpb_total, 1, 2, img[drive].f);
		fseek(img[drive].f, 0x15, SEEK_SET);
		bpb_mid = fgetc(img[drive].f);
		fseek(img[drive].f, 0x18, SEEK_SET);
		bpb_sectors = fgetc(img[drive].f);
		fseek(img[drive].f, 0x1A, SEEK_SET);
		bpb_sides = fgetc(img[drive].f);

		img[drive].base = 0;
		fdi = 0;

	        fseek(img[drive].f, -1, SEEK_END);
	        size = ftell(img[drive].f) + 1;
	}

        img[drive].sides = 2;
        img[drive].sector_size = 512;

	img[drive].hole = 0;

	pclog("BPB reports %i sides and %i bytes per sector\n", bpb_sides, bpb_bps);

	if (((bpb_sides < 1) || (bpb_sides > 2) || (bpb_bps < 128) || (bpb_bps > 2048)) && !fdi)
	{
		/* The BPB is giving us a wacky number of sides and/or bytes per sector, therefore it is most probably
		   not a BPB at all, so we have to guess the parameters from file size. */

		if (size <= (160*1024))        { img[drive].sectors = 8;  img[drive].tracks = 40; img[drive].sides = 1; bit_rate_300 = 250; raw_tsize[drive] = 6250; }
	        else if (size <= (180*1024))   { img[drive].sectors = 9;  img[drive].tracks = 40; img[drive].sides = 1; bit_rate_300 = 250; raw_tsize[drive] = 6250; }
	        else if (size <= (320*1024))   { img[drive].sectors = 8;  img[drive].tracks = 40; bit_rate_300 = 250; raw_tsize[drive] = 6250; }
	        else if (size <= (360*1024))   { img[drive].sectors = 9;  img[drive].tracks = 40; bit_rate_300 = 250; raw_tsize[drive] = 6250; } /*Double density*/
	        else if (size <= (640*1024))   { img[drive].sectors = 8;  img[drive].tracks = 80; bit_rate_300 = 250; raw_tsize[drive] = 6250; } /*Double density 640k*/
	        else if (size < (1024*1024))   { img[drive].sectors = 9;  img[drive].tracks = 80; bit_rate_300 = 250; raw_tsize[drive] = 6250; } /*Double density*/
	        else if (size <= 1228800)      { img[drive].sectors = 15; img[drive].tracks = 80; bit_rate_300 = (500.0 * 300.0) / 360.0; raw_tsize[drive] = 10416; } /*High density 1.2MB*/
	        else if (size <= 1261568)      { img[drive].sectors =  8; img[drive].tracks = 77; img[drive].sector_size = 1024; bit_rate_300 = (500.0 * 300.0) / 360.0; raw_tsize[drive] = 10416; } /*High density 1.25MB Japanese format*/
	        else if (size <= (0x1A4000-1)) { img[drive].sectors = 18; img[drive].tracks = 80; bit_rate_300 = 500; raw_tsize[drive] = 12500; } /*High density (not supported by Tandy 1000)*/
	        else if (size <= 1556480)      { img[drive].sectors = 19; img[drive].tracks = 80; bit_rate_300 = 500; raw_tsize[drive] = 12500; } /*High density (not supported by Tandy 1000)*/
	        else if (size <= 1638400)      { img[drive].sectors = 10; img[drive].tracks = 80; img[drive].sector_size = 1024; bit_rate_300 = 500; raw_tsize[drive] = 12500; } /*High density (not supported by Tandy 1000)*/
	        // else if (size == 1884160)      { img[drive].sectors = 23; img[drive].tracks = 80; bit_rate_300 = 500; } /*XDF format - used by OS/2 Warp*/
	        // else if (size == 1763328)      { img[drive].sectors = 21; img[drive].tracks = 82; bit_rate_300 = 500; } /*XDF format - used by OS/2 Warp*/
	        else if (size <= 2000000)   { img[drive].sectors = 21; img[drive].tracks = 80; bit_rate_300 = 500; raw_tsize[drive] = 12500; } /*DMF format - used by Windows 95 - changed by OBattler to 2000000, ie. the real unformatted capacity @ 500 kbps and 300 rpm */
	        else if (size <= 2949120)   { img[drive].sectors = 36; img[drive].tracks = 80; bit_rate_300 = 1000; raw_tsize[drive] = 25000; } /*E density*/

		temp_rate = 2;
		bpb_bps = img[drive].sector_size;
		bpt = bpb_bps * img[drive].sectors;
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
			fclose(img[drive].f);
			return;
		}

		pclog("Temporary rate: %i (%i bytes per track)\n", temp_rate, bpt);

		img[drive].xdf_type = 0;
	}
	else
	{
		/* The BPB readings appear to be valid, so let's set the values. */
		/* Number of tracks = number of total sectors divided by sides times sectors per track. */
		if (fdi)
		{
			/* The image is a Japanese FDI, therefore we read the number of tracks from the header. */
			fseek(img[drive].f, 0x1C, SEEK_SET);
			fread(&(img[drive].tracks), 1, 4, img[drive].f);
		}
		else
		{
			img[drive].tracks = ((uint32_t) bpb_total) / (((uint32_t) bpb_sides) * ((uint32_t) bpb_sectors));
		}
		/* The rest we just set directly from the BPB. */
		img[drive].sectors = bpb_sectors;
		img[drive].sides = bpb_sides;
		/* The sector size. */
		img[drive].sector_size = bpb_bps;
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
			fclose(img[drive].f);
			return;
		}

		if (bpb_bps == 512)			/* BPB reports 512 bytes per sector, let's see if it's XDF or not */
		{
			if (bit_rate_300 <= 300)	/* Double-density disk, not XDF */
			{
				img[drive].xdf_type = 0;
			}
			else
			{
				pclog("bpb_sectors is %i\n", bpb_sectors);
				if (bpb_sectors > max_spt)
				{
					switch(bpb_sectors)
					{
						case 19:	/* High density XDF @ 360 rpm */
							img[drive].xdf_type = 1;
							break;
						case 23:	/* High density XDF @ 300 rpm */
							img[drive].xdf_type = 2;
							pclog("XDF type is 2 @ %i kbps\n", bit_rate_300);
							break;
						case 46:	/* Extended density XDF */
							img[drive].xdf_type = 3;
							break;
						default:	/* Unknown, as we're beyond maximum sectors, get out */
							fclose(img[drive].f);
							return;
					}
					pclog("XDF type: %i\n", img[drive].xdf_type);
				}
				else			/* Amount of sectors per track that fits into a track, therefore not XDF */
				{
					img[drive].xdf_type = 0;
				}
			}
		}
		else					/* BPB reports sector size other than 512, can't possibly be XDF */
		{
			img[drive].xdf_type = 0;
		}
	}

	gap2_size[drive] = (temp_rate == 3) ? 41 : 22;
	// pclog("GAP2 size: %i bytes\n", gap2_size[drive]);
	gap3_size[drive] = gap3_sizes[temp_rate][sector_size_code(img[drive].sector_size)][img[drive].sectors];
	if (!gap3_size[drive])
	{
		gap3_size[drive] = 40;
		pclog("WARNING: Floppy image of unknown format was inserted into drive %c:!\n", drive + 0x41);
	}

	if (bit_rate_300 == 250)
	{
		img[drive].hole = 0;
		/* If drive does not support 300 RPM, the medium is to be read at a period of 26 (300 kbps). */
		img[drive].byte_period = 29;
	}
	else if (bit_rate_300 == 300)
	{
		img[drive].hole = 0;
		img[drive].byte_period = 26;
	}
	else if (bit_rate_300 == 1000)
	{
		img[drive].hole = 2;
		img[drive].byte_period = 8;
	}
	else if (bit_rate_300 < 250)
	{
		img[drive].hole = 0;
		img[drive].byte_period = 32;
	}
	else
	{
		img[drive].hole = 1;
		img[drive].byte_period = 16;
	}

        drives[drive].seek        = img_seek;
        drives[drive].readsector  = disc_sector_readsector;
        drives[drive].writesector = disc_sector_writesector;
        drives[drive].readaddress = disc_sector_readaddress;
        drives[drive].hole        = img_hole;
        drives[drive].byteperiod  = img_byteperiod;
        drives[drive].poll        = disc_sector_poll;
        drives[drive].format      = disc_sector_format;
        drives[drive].realtrack   = img_realtrack;
        drives[drive].stop        = disc_sector_stop;
        disc_sector_writeback[drive] = img_writeback;
        
        img[drive].bitcell_period_300rpm = 1000000.0 / bit_rate_300*2.0;
        pclog("bit_rate_300=%g\n", bit_rate_300);
        pclog("bitcell_period_300=%g\n", img[drive].bitcell_period_300rpm);
//        img[drive].bitcell_period_300rpm = disc_get_bitcell_period(img[drive].rate);
        pclog("img_load %d %p sectors=%i tracks=%i sides=%i sector_size=%i hole=%i\n", drive, drives, img[drive].sectors, img[drive].tracks, img[drive].sides, img[drive].sector_size, img[drive].hole);
}

int img_hole(int drive)
{
	return img[drive].hole;
}

double img_byteperiod(int drive)
{
	if (img[drive].byte_period == 29)
	{
		return (fdd_get_type(drive) & 1) ? 32.0 : (160.0 / 6.0);
	}
	return (double) img[drive].byte_period;
}

void img_close(int drive)
{
        if (img[drive].f)
                fclose(img[drive].f);
        img[drive].f = NULL;
}

void img_seek(int drive, int track)
{
        int side;
	int current_xdft = img[drive].xdf_type - 1;

	int sector, current_pos, sh, sr, sn, sside, total, max_pos;
        
        if (!img[drive].f)
                return;
        // pclog("Seek drive=%i track=%i sectors=%i sector_size=%i sides=%i\n", drive, track, img[drive].sectors,img[drive].sector_size, img[drive].sides);
//        pclog("  %i %i\n", drive_type[drive], img[drive].tracks);
        if ((img[drive].tracks <= 43) && fdd_doublestep_40(drive))
                track /= 2;

	// pclog("Disk seeked to track %i\n", track);
        disc_track[drive] = track;

        if (img[drive].sides == 2)
        {
                fseek(img[drive].f, img[drive].base + (track * img[drive].sectors * img[drive].sector_size * 2), SEEK_SET);
		// pclog("Seek: Current file position (H0) is: %08X\n", ftell(img[drive].f));
                fread(img[drive].track_data[0], img[drive].sectors * img[drive].sector_size, 1, img[drive].f);
		// pclog("Seek: Current file position (H1) is: %08X\n", ftell(img[drive].f));
                fread(img[drive].track_data[1], img[drive].sectors * img[drive].sector_size, 1, img[drive].f);
        }
        else
        {
                fseek(img[drive].f, img[drive].base + (track * img[drive].sectors * img[drive].sector_size), SEEK_SET);
                fread(img[drive].track_data[0], img[drive].sectors * img[drive].sector_size, 1, img[drive].f);
        }
        
        disc_sector_reset(drive, 0);
        disc_sector_reset(drive, 1);
        
	if (img[drive].xdf_type)
	{
		max_pos = (img[drive].sectors * 512);
		if (!track)
		{
			/* Track 0, register sectors according to track 0 layout. */
			total = img[drive].sectors;
			current_pos = 0;
			for (sector = 0; sector < (total << 1); sector++)
			{
				current_pos = (sector % total) << 9;
				sside = (sector >= total) ? 1 : 0;
				if (xdf_track0_layout[current_xdft][sector])
				{
					sh = xdf_track0_layout[current_xdft][sector] & 0xFF;
					sr = xdf_track0_layout[current_xdft][sector] >> 8;
					xdf_sector_pos[(sh << 8) | sr] = current_pos;
		                        disc_sector_add(drive, sh, track, sh, sr, 2,
       			                                img[drive].bitcell_period_300rpm, 
               			                        &img[drive].track_data[sside][current_pos]);
				}
			}
		}
		else
		{
			/* Non-zero track, this will have sectors of various sizes. */
			total = xdf_spt[current_xdft] >> 1;
			current_pos = 0;
			for (sector = 0; sector < xdf_spt[current_xdft]; sector++)
			{
				sside = (sector >= total) ? 1 : 0;
				sh = xdf_trackx_layout[current_xdft][sector] & 0xFF;
				sr = xdf_trackx_layout[current_xdft][sector] >> 8;
				sn = sr & 7;
	                        disc_sector_add(drive, sh, track, sh, sr, sn,
   			                        img[drive].bitcell_period_300rpm, 
              			                &img[drive].track_data[sside][current_pos]);
				current_pos += (128 << sn);
				current_pos %= max_pos;
			}
		}
	}
	else
	{                
	        for (side = 0; side < img[drive].sides; side++)
       		{
	                for (sector = 0; sector < img[drive].sectors; sector++)
        	                disc_sector_add(drive, side, track, side, sector+1, img_sector_size_code(drive),
       	        	                        img[drive].bitcell_period_300rpm, 
               	        	                &img[drive].track_data[side][sector * img[drive].sector_size]);
		}
	}
	for (side = img[drive].sides - 1; side >= 0; side--)
	{
		disc_sector_prepare_track_layout(drive, side, track);
	}
}

void img_writeback(int drive)
{
        if (!img[drive].f)
                return;
                
        // if (img[drive].xdf_type)
                // return; /*Should never happen*/

        if (img[drive].sides == 2)
        {
       	        fseek(img[drive].f, img[drive].base + (disc_track[drive] * img[drive].sectors * img[drive].sector_size * 2), SEEK_SET);
                fwrite(img[drive].track_data[0], img[drive].sectors * img[drive].sector_size, 1, img[drive].f);
                fwrite(img[drive].track_data[1], img[drive].sectors * img[drive].sector_size, 1, img[drive].f);
        }
        else
       	{
                fseek(img[drive].f, img[drive].base + (disc_track[drive] * img[drive].sectors * img[drive].sector_size), SEEK_SET);
       	        fwrite(img[drive].track_data[0], img[drive].sectors * img[drive].sector_size, 1, img[drive].f);
        }
}

int img_xdf_type(int drive)
{
	return img[drive].xdf_type;
}
