/* Copyright holders: Tenshi
   see COPYING for more details
*/
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
//        pclog("disc_sector_writesector: fdc_period=%i img_period=%i rate=%i\n", fdc_get_bitcell_period(), get_bitcell_period(drive), rate);

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
        // pclog("disc_sector_readaddress: fdc_period=%i img_period=%i rate=%i track=%i side=%i\n", fdc_get_bitcell_period(), get_bitcell_period(drive), rate, track, side);

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
	memset(track_layout[drive][side], BYTE_GAP4, raw_tsize[drive]);
	memset(id_positions[drive][side], 0, 1024);
	i = 0;
	if (!(media_type & 4))
	{
		memset(track_layout[drive][side] + i, BYTE_GAP0, real_gap0_len);
		i += real_gap0_len - 1;
		memset(track_layout[drive][side] + i, BYTE_I_SYNC, sync_len);
		i += sync_len;
		if ((media_type & 3) != 1)
		{
			memset(track_layout[drive][side] + i, BYTE_IAM_SYNC, 3);
			i += 3;
		}
		memset(track_layout[drive][side] + i, BYTE_IAM, 1);
		i++;
		memset(track_layout[drive][side] + i, BYTE_GAP1, real_gap1_len);
		i += real_gap1_len;
	}
	else
	{
		memset(track_layout[drive][side] + i, BYTE_GAP1, real_gap1_len);
		i += real_gap1_len - 1;
	}
	track_layout[drive][side][0] |= BYTE_INDEX_HOLE;
	for (j = 0; j < disc_sector_count[drive][side]; j++)
	{
		s = &disc_sector_data[drive][side][j];
		// pclog("Sector %i (%i)\n", j, s->n);
		memset(track_layout[drive][side] + i, BYTE_ID_SYNC, sync_len);
		i += sync_len;
		if ((media_type & 3) != 1)
		{
			memset(track_layout[drive][side] + i, BYTE_IDAM_SYNC, 3);
			i += 3;
		}
		memset(track_layout[drive][side] + i, BYTE_IDAM, 1);
		i++;
		memset(track_layout[drive][side] + i, BYTE_ID, 4);
		i += 4;
		memset(track_layout[drive][side] + i, BYTE_ID_CRC, 2);
		i += 2;
		id_positions[drive][side][j] = i;
		memset(track_layout[drive][side] + i, BYTE_GAP2, gap2_size[drive]);
		i += gap2_size[drive];
		memset(track_layout[drive][side] + i, BYTE_DATA_SYNC, sync_len);
		i += sync_len;
		if ((media_type & 3) != 1)
		{
			memset(track_layout[drive][side] + i, BYTE_DATAAM_SYNC, 3);
			i += 3;
		}
		memset(track_layout[drive][side] + i, BYTE_DATAAM, 1);
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
	temp = temp && (disc_sector_n[drive] == last_sector[drive]->n);
	return temp;
}

uint32_t disc_sector_get_data_len(int drive)
{
	if (disc_sector_n[drive])
	{
		return (128 << ((uint32_t) disc_sector_n[drive]));
	}
	else
	{
		if (fdc_get_dtl() < 128)
		{
			return fdc_get_dtl();
		}
		else
		{
			return (128 << ((uint32_t) disc_sector_n[drive]));
		}
	}
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

int section_pos[2] = {0, 0};

typedef union
{
	uint32_t dword;
	uint8_t byte_array[4];
} sector_id;

sector_id format_sector_id;

void disc_sector_poll()
{
        sector_t *s;
        int data;
	int drive = disc_sector_drive;
	int side = disc_sector_side[drive];
	int found_sector = 0;
	int b = 0;

	int cur_id_pos = 0;
	int cur_data_pos = 0;
	int cur_gap3_pos = 0;

	uint8_t track_byte = 0;
	uint8_t track_index = 0;
	uint8_t track_sector = 0;
	uint8_t track_byte_type = 0;

	uint8_t old_track_byte = 0;
	uint8_t old_track_index = 0;
	uint8_t old_track_sector = 0;
	uint8_t old_track_byte_type = 0;

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
				// pclog("disc_sector_poll(): Disk is write protected or attempting to format wrong number of sectors per track\n");
				fdc_writeprotect();
			}
			else
			{
				// pclog("disc_sector_poll(): Unable to format at the requested density or bitcell period\n");
				fdc_notfound();
			}
                        disc_sector_state[drive] = STATE_IDLE;
			disc_sector_reset_state(drive);
			cur_track_pos[drive]++;
			cur_track_pos[drive] %= raw_tsize[drive];
			return;
		}
	}

	track_byte = track_layout[drive][side][cur_track_pos[drive]];
	track_index = track_byte & BYTE_INDEX_HOLE;
	track_sector = track_byte & BYTE_IS_SECTOR;
	track_byte_type = track_byte & BYTE_TYPE;

	if (track_index)
	{
		if (disc_sector_state[drive] != STATE_IDLE)
		{
			index_pulse(drive);
			index_count[drive]++;
		}
		if (disc_sector_state[drive] == STATE_FORMAT)
		{
			// pclog("Index hole hit again, format finished\n");
              		disc_sector_state[drive] = STATE_IDLE;
   		        if (!disable_write)  disc_sector_writeback[drive](drive, disc_sector_track[drive]);
                        fdc_sector_finishread(drive);
		}
		if ((disc_sector_state[drive] == STATE_FORMAT_FIND) && disc_sector_can_read_address(drive))
		{
			// pclog("Index hole hit, formatting track...\n");
			disc_sector_state[drive] = STATE_FORMAT;
		}
	}

	switch(track_byte)
	{
		case BYTE_ID_SYNC:
			if (disc_sector_state[drive] != STATE_FORMAT)  break;
			cur_id_pos = cur_track_pos[drive] - section_pos[drive];
			if (cur_id_pos > 3)  break;
                	data = fdc_getdata(0);
        	        if ((data == -1) && (cur_id_pos < 3))
			{
				/* Data failed to be sent from the FDC, abort. */
				// pclog("disc_sector_poll(): Unable to receive further data from the FDC\n");
				disc_sector_state[drive] = STATE_IDLE;
				disc_sector_reset_state(drive);
				cur_track_pos[drive]++;
				cur_track_pos[drive] %= raw_tsize[drive];
				return;
			}
			format_sector_id.byte_array[cur_id_pos] = data & 0xff;
			// pclog("format_sector_id[%i] = %i\n", cur_id_pos, format_sector_id.byte_array[cur_id_pos]);
        	        if (cur_id_pos == 3)
			{
				fdc_stop_id_request();
				// pclog("Formatting sector: %08X...\n", format_sector_id.dword);
			}
			break;
		case BYTE_DATA:
			cur_data_pos = cur_track_pos[drive] - section_pos[drive];
			if (disc_sector_read_state(drive) && (last_sector[drive] != NULL))
			{
				if (fdc_data(last_sector[drive]->data[data_counter[drive]]))
				{
					/* Data failed to be sent to the FDC, abort. */
					// pclog("disc_sector_poll(): Unable to send further data to the FDC\n");
					disc_sector_state[drive] = STATE_IDLE;
					disc_sector_reset_state(drive);
					cur_track_pos[drive]++;
					cur_track_pos[drive] %= raw_tsize[drive];
					return;
				}
			}
			if ((disc_sector_state[drive] == STATE_WRITE_SECTOR) && (last_sector[drive] != NULL))
			{
	                	data = fdc_getdata(data_counter[drive] == ((128 << ((uint32_t) last_sector[drive]->n)) - 1));
	        	        if (data == -1)
				{
					/* Data failed to be sent from the FDC, abort. */
					// pclog("disc_sector_poll(): Unable to receive further data from the FDC\n");
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
			}
			break;
		case BYTE_GAP3:
			cur_gap3_pos = cur_track_pos[drive] - section_pos[drive];
			if (cur_gap3_pos == (fdc_get_gap() - 1))
			{
				if (disc_sector_read_state(drive) && (last_sector[drive] != NULL))
				{
	        	                disc_sector_state[drive] = STATE_IDLE;
	                        	fdc_sector_finishread(drive);
				}
				if ((disc_sector_state[drive] == STATE_WRITE_SECTOR) && (last_sector[drive] != NULL))
				{
	                	        disc_sector_state[drive] = STATE_IDLE;
       		                	if (!disable_write)  disc_sector_writeback[drive](drive, disc_sector_track[drive]);
                		        fdc_sector_finishread(drive);
				}
			}
			break;
	}

	old_track_byte = track_byte;
	old_track_index = track_index;
	old_track_sector = track_sector;
	old_track_byte_type = track_byte_type;

	cur_track_pos[drive]++;
	cur_track_pos[drive] %= raw_tsize[drive];

	track_byte = track_layout[drive][side][cur_track_pos[drive]];
	track_index = track_byte & BYTE_INDEX_HOLE;
	track_sector = track_byte & BYTE_IS_SECTOR;
	track_byte_type = track_byte & BYTE_TYPE;

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

	if (track_byte != old_track_byte)
	{
		// if (disc_sector_state[drive] == STATE_FORMAT)  pclog("Track byte: %02X, old: %02X\n", track_byte, old_track_byte);
		section_pos[drive] = cur_track_pos[drive];
		switch(track_byte)
		{
			case BYTE_ID_SYNC:
				if (disc_sector_state[drive] == STATE_FORMAT)
				{
					// pclog("Requesting next sector ID...\n");
					fdc_request_next_sector_id();
				}
				break;
			case BYTE_GAP2:
				found_sector = disc_sector_find_sector(drive);
				// pclog("Found sector: %i\n", found_sector);
				cur_sector[drive] = found_sector;
				last_sector[drive] = &disc_sector_data[drive][disc_sector_side[drive]][found_sector];
				cur_rate[drive] = last_sector[drive]->rate;
				if (!(disc_sector_can_read_address(drive)))  last_sector[drive] = NULL;

				/* ID CRC read, if state is read address, return address */
				if ((disc_sector_state[drive] == STATE_READ_FIND_ADDRESS) && !(disc_sector_can_read_address(drive)))
				{
					if (fdc_get_bitcell_period() != get_bitcell_period(drive))
					{
						// pclog("Unable to read sector ID: Bitcell period mismatch (%i != %i)...\n", fdc_get_bitcell_period(), get_bitcell_period(drive));
					}
					else
					{
						// pclog("Unable to read sector ID: Media type not supported by the drive...\n");
					}
				}
				if ((disc_sector_state[drive] == STATE_READ_FIND_ADDRESS) && disc_sector_can_read_address(drive))
				{
					// pclog("Reading sector ID...\n");
					fdc_sectorid(last_sector[drive]->c, last_sector[drive]->h, last_sector[drive]->r, last_sector[drive]->n, 0, 0);
					disc_sector_state[drive] = STATE_IDLE;
				}
				break;
			case BYTE_DATA:
				// data_counter[drive] = 0;
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
					/* case STATE_FORMAT:
						// pclog("Format: Starting sector fill...\n");
						break; */
				}
				break;
			case BYTE_GAP4:
				if (last_sector[drive] != NULL)  last_sector[drive] = NULL;
				break;
		}
	}
}
