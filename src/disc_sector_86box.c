#include "ibm.h"
#include "disc.h"
#include "disc_sector.h"
#include "fdd.h"

/*Handling for 'sector based' image formats (like .IMG) as opposed to 'stream based' formats (eg .FDI)*/

#define MAX_SECTORS 256

typedef struct
{
        uint8_t c, h, r, n;
        int rate;
        uint8_t *data;
} sector_t;

static sector_t disc_sector_data[2][2][MAX_SECTORS];
static int disc_sector_count[2][2];
void (*disc_sector_writeback[2])(int drive, int track);

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

static int disc_sector_state[2] = {0, 0};
static int disc_sector_track[2] = {0, 0};
static int disc_sector_side[2] = {0, 0};
static int disc_sector_drive;
static int disc_sector_sector[2] = {0, 0};
static int disc_sector_n[2] = {0, 0};
static int disc_intersector_delay[2] = {0, 0};
static int disc_postdata_delay[2] = {0, 0};
static int disc_track_delay[2] = {0, 0};
static int disc_gap4_delay[2] = {0, 0};
static uint8_t disc_sector_fill[2] = {0, 0};
static int cur_sector[2], cur_byte[2];
static int index_count[2];
        
int raw_tsize[2] = {6250, 6250};
int gap2_size[2] = {22, 22};
int gap3_size[2] = {0, 0};
int gap4_size[2] = {0, 0};

int disc_sector_reset_state(int drive);

void disc_sector_reset(int drive, int side)
{
        disc_sector_count[drive][side] = 0;

	if (side == 0)
	{
		disc_sector_reset_state(drive);
		// cur_track_pos[drive] = 0;
		disc_sector_state[drive] = STATE_SEEK;
	}
}

void disc_sector_add(int drive, int side, uint8_t c, uint8_t h, uint8_t r, uint8_t n, int rate, uint8_t *data)
{
        sector_t *s = &disc_sector_data[drive][side][disc_sector_count[drive][side]];
//pclog("disc_sector_add: drive=%i side=%i %i r=%i\n", drive, side,         disc_sector_count[drive][side],r );
        if (disc_sector_count[drive][side] >= MAX_SECTORS)
                return;

        s->c = c;
        s->h = h;
        s->r = r;
        s->n = n;
	// pclog("Adding sector: %i %i %i %i\n", c, h, r, n);
        s->rate = rate;
        s->data = data;
        
        disc_sector_count[drive][side]++;
}

static int get_bitcell_period(int drive)
{
        // return (disc_sector_data[drive][disc_sector_side[drive]][cur_sector[drive]].rate * 300) / fdd_getrpm(drive);
	return ((&disc_sector_data[drive][0][0])->rate * 300) / fdd_getrpm(drive);
        return (cur_rate[drive] * 300) / fdd_getrpm(drive);
}

void disc_sector_readsector(int drive, int sector, int track, int side, int rate, int sector_size)
{
        // pclog("disc_sector_readsector: fdc_period=%i img_period=%i rate=%i sector=%i track=%i side=%i\n", fdc_get_bitcell_period(), get_bitcell_period(drive), rate, sector, track, side);

        disc_sector_track[drive] = track;
        disc_sector_side[drive]  = side;
        disc_sector_drive = drive;
        disc_sector_sector[drive] = sector;
	disc_sector_n[drive] = sector_size;
	disc_sector_reset_state(drive);
        if (sector == SECTOR_FIRST)
                disc_sector_state[drive] = STATE_READ_FIND_FIRST_SECTOR;
        else if (sector == SECTOR_NEXT)
                disc_sector_state[drive] = STATE_READ_FIND_NEXT_SECTOR;
        else
                disc_sector_state[drive] = STATE_READ_FIND_SECTOR;
}

void disc_sector_writesector(int drive, int sector, int track, int side, int rate, int sector_size)
{
//        pclog("disc_sector_writesector: fdc_period=%i img_period=%i rate=%i\n", fdc_get_bitcell_period(), get_bitcell_period(), rate);

        disc_sector_track[drive] = track;
        disc_sector_side[drive]  = side;
        disc_sector_drive = drive;
        disc_sector_sector[drive] = sector;
	disc_sector_n[drive] = sector_size;
	disc_sector_reset_state(drive);
        disc_sector_state[drive] = STATE_WRITE_FIND_SECTOR;
}

void disc_sector_readaddress(int drive, int track, int side, int rate)
{
//        pclog("disc_sector_readaddress: fdc_period=%i img_period=%i rate=%i track=%i side=%i\n", fdc_get_bitcell_period(), get_bitcell_period(), rate, track, side);

        disc_sector_track[drive] = track;
        disc_sector_side[drive]  = side;
        disc_sector_drive = drive;
	disc_sector_reset_state(drive);
        disc_sector_state[drive] = STATE_READ_FIND_ADDRESS;
}

void disc_sector_format(int drive, int track, int side, int rate, uint8_t fill)
{
        disc_sector_track[drive] = track;
        disc_sector_side[drive]  = side;
        disc_sector_drive = drive;
        disc_sector_fill[drive]  = fill;
	disc_sector_reset_state(drive);
        disc_sector_state[drive] = STATE_FORMAT_FIND;
}

void disc_sector_stop(int drive)
{
        disc_sector_state[drive] = STATE_IDLE;
}

static void index_pulse(int drive)
{
	if (disc_sector_state[drive] != STATE_IDLE)  fdc_indexpulse();
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

void disc_sector_prepare_track_layout(int drive, int side)
{
	sector_t *s;
	int i = 0;
	int j = 0;
	int real_gap0_len = ((media_type & 3) == 1) ? 40 : 80;
	int sync_len = ((media_type & 3) == 1) ? 6 : 12;
	int am_len = ((media_type & 3) == 1) ? 1 : 4;
	int real_gap1_len = ((media_type & 3) == 1) ? 26 : 50;
	// track_layout[drive][side] = (char *) malloc(raw_tsize[drive]);
	// id_positions[drive][side] = (int *) malloc(disc_sector_count[drive][side] * 4);
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
	for (j = 0; j < disc_sector_count[drive][side]; j++)
	{
		s = &disc_sector_data[drive][side][j];
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

	if (side == 0)  disc_sector_state[drive] = STATE_IDLE;

#if 0
	FILE *f = fopen("layout.dmp", "wb");
	fwrite(track_layout[drive][side], 1, raw_tsize[drive], f);
	fclose(f);
	fatal("good getpccache!\n");
#endif
}

int disc_sector_reset_state(int drive)
{
	id_counter[drive] = data_counter[drive] = index_count[drive] = gap3_counter[drive] = cur_rate[drive] = 0;
	last_sector[drive] = NULL;
}

int disc_sector_find_sector(int drive)
{
	int side = disc_sector_side[drive];
	int i = 0;
	for (i = 0; i < disc_sector_count[drive][side]; i++)
	{
		if (id_positions[drive][side][i] == cur_track_pos[drive])
		{
			return i;
		}
	}
	return -1;
}

int disc_sector_match(int drive)
{
	int temp;
	if (last_sector[drive] == NULL)  return 0;
	temp = (disc_sector_track[drive] == last_sector[drive]->c);
	temp = temp && (disc_sector_side[drive] == last_sector[drive]->h);
	temp = temp && (disc_sector_sector[drive] == last_sector[drive]->r);
	if (disc_sector_n[drive])
	{
		temp = temp && (disc_sector_n[drive] == last_sector[drive]->n);
	}
	return temp;
}

int disc_sector_can_read_address(int drive)
{
	int temp;
	temp = (fdc_get_bitcell_period() == get_bitcell_period(drive));
	temp = temp && fdd_can_read_medium(drive ^ fdd_swap);
	return temp;
}

int disc_sector_can_format(int drive)
{
	int temp;
	temp = !writeprot[drive];
	temp = temp && !swwp;
	temp = temp && disc_sector_can_read_address(drive);
	temp = temp && (fdc_get_format_sectors() == disc_sector_count[drive][disc_sector_side[drive]]);
	return temp;
}

int disc_sector_find_state(int drive)
{
	int temp;
	temp = (disc_sector_state[drive] == STATE_READ_FIND_SECTOR);
	temp = temp || (disc_sector_state[drive] == STATE_READ_FIND_FIRST_SECTOR);
	temp = temp || (disc_sector_state[drive] == STATE_READ_FIND_NEXT_SECTOR);
	temp = temp || (disc_sector_state[drive] == STATE_WRITE_FIND_SECTOR);
	temp = temp || (disc_sector_state[drive] == STATE_READ_FIND_ADDRESS);
	temp = temp || (disc_sector_state[drive] == STATE_FORMAT_FIND);
	return temp;
}

int disc_sector_read_state(int drive)
{
	int temp;
	temp = (disc_sector_state[drive] == STATE_READ_SECTOR);
	temp = temp || (disc_sector_state[drive] == STATE_READ_FIRST_SECTOR);
	temp = temp || (disc_sector_state[drive] == STATE_READ_NEXT_SECTOR);
	return temp;
}

void disc_sector_poll()
{
        sector_t *s;
        int data;
	int drive = disc_sector_drive;
	int side = disc_sector_side[drive];
	int found_sector = 0;
	int b = 0;

	if (disc_sector_state[drive] == STATE_SEEK)
	{
		cur_track_pos[drive]++;
		cur_track_pos[drive] %= raw_tsize[drive];
		return;
	}

	if (disc_sector_state[drive] == STATE_FORMAT_FIND)
	{
		if (!(disc_sector_can_format(drive)))
		{
			if (disc_sector_can_read_address(drive))
			{
				pclog("disc_sector_poll(): Disk is write protected or attempting to format wrong number of sectors per track\n");
				fdc_writeprotect();
			}
			else
			{
				pclog("disc_sector_poll(): Unable to format at the requested density or bitcell period\n");
				fdc_notfound();
			}
                        disc_sector_state[drive] = STATE_IDLE;
			disc_sector_reset_state(drive);
			cur_track_pos[drive]++;
			cur_track_pos[drive] %= raw_tsize[drive];
			return;
		}
	}
	// if (disc_sector_state[drive] != STATE_IDLE)  pclog("%04X: %01X\n", cur_track_pos[drive], track_layout[drive][side][cur_track_pos[drive]]);
	if (track_layout[drive][side][cur_track_pos[drive]] == BYTE_GAP)
	{
		if (disc_sector_read_state(drive) || (disc_sector_state[drive] == STATE_WRITE_SECTOR) || (disc_sector_state[drive] == STATE_FORMAT))
		{
			/* We're at GAP4b or even GAP4a and still in a read, write, or format state, this means we've overrun the gap.
			   Return with sector not found. */
			// pclog("disc_sector_poll(): Gap overrun at GAP4\n");
			fdc_notfound();
                        disc_sector_state[drive] = STATE_IDLE;
			disc_sector_reset_state(drive);
			cur_track_pos[drive]++;
			cur_track_pos[drive] %= raw_tsize[drive];
			return;
		}
	}
	else if (track_layout[drive][side][cur_track_pos[drive]] == BYTE_INDEX_HOLE)
	{
		index_pulse(drive);
		if (disc_sector_state[drive] != STATE_IDLE)  index_count[drive]++;
		if (disc_sector_read_state(drive) || (disc_sector_state[drive] == STATE_WRITE_SECTOR) || (disc_sector_state[drive] == STATE_FORMAT))
		{
			/* We're at the index address mark and still in a read, write, or format state, this means we've overrun the gap.
			   Return with sector not found. */
			// pclog("disc_sector_poll(): Gap overrun at IAM\n");
			fdc_notfound();
                        disc_sector_state[drive] = STATE_IDLE;
			disc_sector_reset_state(drive);
			cur_track_pos[drive]++;
			cur_track_pos[drive] %= raw_tsize[drive];
			return;
		}
	}
	else if (track_layout[drive][side][cur_track_pos[drive]] == BYTE_IDAM)
	{
		found_sector = disc_sector_find_sector(drive);
		// pclog("Found sector: %i\n", found_sector);
		cur_sector[drive] = found_sector;
		last_sector[drive] = &disc_sector_data[drive][disc_sector_side[drive]][found_sector];
		cur_rate[drive] = last_sector[drive]->rate;
		if (!(disc_sector_can_read_address(drive)))  last_sector[drive] = NULL;
		if (disc_sector_read_state(drive) || (disc_sector_state[drive] == STATE_WRITE_SECTOR) || (disc_sector_state[drive] == STATE_FORMAT))
		{
			/* We're at a sector ID address mark and still in a read, write, or format state, this means we've overrun the gap.
			   Return with sector not found. */
			pclog("disc_sector_poll(): Gap (%i) overrun at IDAM\n", fdc_get_gap());
			fdc_notfound();
                        disc_sector_state[drive] = STATE_IDLE;
			disc_sector_reset_state(drive);
			cur_track_pos[drive]++;
			cur_track_pos[drive] %= raw_tsize[drive];
			return;
		}
		if ((disc_sector_state[drive] == STATE_FORMAT_FIND) && disc_sector_can_read_address(drive))  disc_sector_state[drive] = STATE_FORMAT;
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
			if ((disc_sector_state[drive] == STATE_READ_FIND_ADDRESS) && !(disc_sector_can_read_address(drive)))
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
			if ((disc_sector_state[drive] == STATE_READ_FIND_ADDRESS) && disc_sector_can_read_address(drive))
			{
				// pclog("Reading sector ID...\n");
				fdc_sectorid(last_sector[drive]->c, last_sector[drive]->h, last_sector[drive]->r, last_sector[drive]->n, 0, 0);
				disc_sector_state[drive] = STATE_IDLE;
			}
			id_counter[drive] = 0;
		}
	}
	else if (track_layout[drive][side][cur_track_pos[drive]] == BYTE_DATA_AM)
	{
		data_counter[drive] = 0;
		switch (disc_sector_state[drive])
		{
			case STATE_READ_FIND_SECTOR:
				if (disc_sector_match(drive) && disc_sector_can_read_address(drive))  disc_sector_state[drive] = STATE_READ_SECTOR;
				break;
			case STATE_READ_FIND_FIRST_SECTOR:
				if ((cur_sector[drive] == 0) && disc_sector_can_read_address(drive))  disc_sector_state[drive] = STATE_READ_FIRST_SECTOR;
				break;
			case STATE_READ_FIND_NEXT_SECTOR:
				if (disc_sector_can_read_address(drive))  disc_sector_state[drive] = STATE_READ_NEXT_SECTOR;
				break;
			case STATE_WRITE_FIND_SECTOR:
				if (disc_sector_match(drive) && disc_sector_can_read_address(drive))  disc_sector_state[drive] = STATE_WRITE_SECTOR;
				break;
		}
	}
	else if (track_layout[drive][side][cur_track_pos[drive]] == BYTE_DATA)
	{
		if (disc_sector_read_state(drive) && (last_sector[drive] != NULL))
		{
			if (fdc_data(last_sector[drive]->data[data_counter[drive]]))
			{
				/* Data failed to be sent to the FDC, abort. */
				pclog("disc_sector_poll(): Unable to send further data to the FDC\n");
				disc_sector_state[drive] = STATE_IDLE;
				disc_sector_reset_state(drive);
				cur_track_pos[drive]++;
				cur_track_pos[drive] %= raw_tsize[drive];
				return;
			}
		}
		if ((disc_sector_state[drive] == STATE_WRITE_SECTOR) && (last_sector[drive] != NULL))
		{
	                data = fdc_getdata(cur_byte[drive] == ((128 << ((uint32_t) last_sector[drive]->n)) - 1));
        	        if (data == -1)
			{
				/* Data failed to be sent from the FDC, abort. */
				pclog("disc_sector_poll(): Unable to receive further data from the FDC\n");
				disc_sector_state[drive] = STATE_IDLE;
				disc_sector_reset_state(drive);
				cur_track_pos[drive]++;
				cur_track_pos[drive] %= raw_tsize[drive];
				return;
			}
			if (!disable_write)  last_sector[drive]->data[data_counter[drive]] = data;
		}
		if ((disc_sector_state[drive] == STATE_FORMAT) && (last_sector[drive] != NULL))
		{
	                if (!disable_write)  last_sector[drive]->data[data_counter[drive]] = disc_sector_fill[drive];
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
				if (disc_sector_read_state(drive) && (last_sector[drive] != NULL))
				{
	        	                disc_sector_state[drive] = STATE_IDLE;
	                        	fdc_finishread(drive);
				}
				if ((disc_sector_state[drive] == STATE_WRITE_SECTOR) && (last_sector[drive] != NULL))
				{
	                	        disc_sector_state[drive] = STATE_IDLE;
        	                	if (!disable_write)  disc_sector_writeback[drive](drive, disc_sector_track[drive]);
	                	        fdc_finishread(drive);
				}
				if ((disc_sector_state[drive] == STATE_FORMAT) && (last_sector[drive] != NULL))
				{
                		        disc_sector_state[drive] = STATE_IDLE;
        		                if (!disable_write)  disc_sector_writeback[drive](drive, disc_sector_track[drive]);
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
	if ((disc_sector_state[drive] != STATE_IDLE) && (disc_sector_state[drive] != STATE_SEEK))
	{
		if (index_count[drive] > 1)
		{
			if (disc_sector_find_state(drive))
			{
				/* The index hole has been hit twice and we're still in a find state.
				   This means sector finding has failed for whatever reason.
				   Abort with sector not found and set state to idle. */
				// pclog("disc_sector_poll(): Sector not found (%i %i %i %i)\n", disc_sector_track[drive], disc_sector_side[drive], disc_sector_sector[drive], disc_sector_n[drive]);
				fdc_notfound();
				disc_sector_state[drive] = STATE_IDLE;
				disc_sector_reset_state(drive);
				return;
			}
		}
	}
	if ((b != BYTE_GAP3) && (track_layout[drive][side][cur_track_pos[drive]] == BYTE_GAP3))
	{
		gap3_counter[drive] = 0;
	}
}
