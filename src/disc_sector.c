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
void (*disc_sector_writeback[2])(int drive);

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
        STATE_FORMAT
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

void disc_sector_reset(int drive, int side)
{
        disc_sector_count[drive][side] = 0;

	disc_intersector_delay[drive] = 0;
	disc_postdata_delay[drive] = 0;
	disc_track_delay[drive] = 0;
	disc_gap4_delay[drive] = 0;
	cur_sector[drive] = 0;
	cur_byte[drive] = 0;
	index_count[drive] = 0;
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
        s->rate = rate;
        s->data = data;
        
        disc_sector_count[drive][side]++;
}

static int get_bitcell_period(int drive)
{
        return (disc_sector_data[drive][disc_sector_side[drive]][cur_sector[drive]].rate * 300) / fdd_getrpm(drive);
}

void disc_sector_readsector(int drive, int sector, int track, int side, int rate, int sector_size)
{
        pclog("disc_sector_readsector: fdc_period=%i img_period=%i rate=%i sector=%i track=%i side=%i\n", fdc_get_bitcell_period(), get_bitcell_period(drive), rate, sector, track, side);

        disc_sector_track[drive] = track;
        disc_sector_side[drive]  = side;
        disc_sector_drive = drive;
        disc_sector_sector[drive] = sector;
	disc_sector_n[drive] = sector_size;
	if ((cur_sector[drive] == 0) && (cur_byte[drive] == 0) && !disc_track_delay[drive])  disc_track_delay[drive] = pre_track;
        index_count[drive] = 0;
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
	if ((cur_sector[drive] == 0) && (cur_byte[drive] == 0) && !disc_track_delay[drive])  disc_track_delay[drive] = pre_track;
        index_count[drive] = 0;
        disc_sector_state[drive] = STATE_WRITE_FIND_SECTOR;
}

void disc_sector_readaddress(int drive, int track, int side, int rate)
{
//        pclog("disc_sector_readaddress: fdc_period=%i img_period=%i rate=%i track=%i side=%i\n", fdc_get_bitcell_period(), get_bitcell_period(), rate, track, side);

        disc_sector_track[drive] = track;
        disc_sector_side[drive]  = side;
        disc_sector_drive = drive;
	if ((cur_sector[drive] == 0) && (cur_byte[drive] == 0) && !disc_track_delay[drive])
	{
		disc_track_delay[drive] = pre_track;
		index_count[drive] = -1;
	}
	else
	        index_count[drive] = 0;
        disc_sector_state[drive] = STATE_READ_FIND_ADDRESS;
}

void disc_sector_format(int drive, int track, int side, int rate, uint8_t fill)
{
        disc_sector_track[drive] = track;
        disc_sector_side[drive]  = side;
        disc_sector_drive = drive;
        disc_sector_fill[drive]  = fill;
        index_count[drive] = 0;
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

void disc_sector_prepare_track_layout(int drive, int side)
{
}

static void advance_byte()
{
	int drive = disc_sector_drive;

	if (disc_postdata_delay[drive])
	{
		disc_postdata_delay[drive]--;
		return;
	}
	if (disc_gap4_delay[drive])
	{
		disc_gap4_delay[drive]--;
		return;
	}
	if (disc_track_delay[drive])
	{
		if (disc_track_delay[drive] == pre_track)
		{
			index_pulse(drive);
			if (disc_sector_state[drive] != STATE_IDLE)  index_count[drive]++;
		}
		disc_track_delay[drive]--;
		return;
	}
        if (disc_intersector_delay[drive])
        {
                disc_intersector_delay[drive]--;
                return;
        }
        cur_byte[drive]++;
        if (cur_byte[drive] >= (128 << disc_sector_data[drive][disc_sector_side[drive]][cur_sector[drive]].n))
        {
                cur_byte[drive] = 0;
                cur_sector[drive]++;
		disc_postdata_delay[drive] = post_gap + (gap3_size[drive]);
                if (cur_sector[drive] >= disc_sector_count[drive][disc_sector_side[drive]])
                {
                        cur_sector[drive] = 0;
			disc_gap4_delay[drive] = (gap4_size[drive]);
			disc_track_delay[drive] = pre_track;
			disc_intersector_delay[drive] = pre_gap + gap2_size[drive] + pre_data;
                }
		else
		{
			disc_gap4_delay[drive] = 0;
			disc_track_delay[drive] = 0;
			disc_intersector_delay[drive] = pre_gap + gap2_size[drive] + pre_data;
		}
        }
	return;
}

int disc_gap_has_ended(int drive)
{
	return (disc_postdata_delay[drive] == (post_gap + (gap3_size[drive]) - fdc_get_gap() - length_crc));
}

void disc_sector_poll()
{
        sector_t *s;
        int data;
	int drive = disc_sector_drive;

        if (cur_sector[drive] >= disc_sector_count[drive][disc_sector_side[drive]])
                cur_sector[drive] = 0;
	if (disc_sector_n[drive])
	{
	        if (cur_byte[drive] >= (128 << disc_sector_data[drive][disc_sector_side[drive]][cur_sector[drive]].n))
        	        cur_byte[drive] = 0;
	}
	else
	{
	        if (cur_byte[drive] >= fdc_get_dtl())
        	        cur_byte[drive] = 0;
	}

	/* Note: Side to read from should be chosen from FDC head select rather than from the sector ID. */
        s = &disc_sector_data[drive][disc_sector_side[drive]][cur_sector[drive]];

        switch (disc_sector_state[drive])
        {
                case STATE_IDLE:
		advance_byte();
                break;
                
                case STATE_READ_FIND_SECTOR:
                if (index_count[drive] > 1)
                {
			pclog("READ: Sector (%i %i %i %i) not found (last: %i %i %i %i) (period=%i,%i) (ic=%i)\n", s->c, s->h, s->r, s->n, disc_sector_track[drive], disc_sector_side[drive], disc_sector_sector[drive], disc_sector_n[drive], fdc_get_bitcell_period(), get_bitcell_period(drive), index_count[drive]);
                        fdc_notfound();
                        disc_sector_state[drive] = STATE_IDLE;
                        break;
                }
                if (/*cur_byte[drive] || */ disc_sector_track[drive] != s->c ||
                        disc_sector_side[drive] != s->h ||
                        disc_sector_sector[drive] != s->r ||
		        ((disc_sector_n[drive] != s->n) && (disc_sector_n[drive])) ||
                        (fdc_get_bitcell_period() != get_bitcell_period(drive)) ||
		        !fdd_can_read_medium(drive ^ fdd_swap) ||
                        disc_intersector_delay[drive] || disc_postdata_delay[drive] || disc_track_delay[drive] || disc_gap4_delay[drive])
                {
                        advance_byte();
                        break;
                }
                disc_sector_state[drive] = STATE_READ_SECTOR;

                case STATE_READ_SECTOR:
		if (!disc_postdata_delay[drive])
		{
	                if (fdc_data(s->data[cur_byte[drive]]))
        	        {
                	        return;
	                }
		}
                advance_byte();
                if (!cur_byte[drive] && disc_gap_has_ended(drive))
                {
                        disc_sector_state[drive] = STATE_IDLE;
                        fdc_finishread(drive);
                }
                break;

                case STATE_READ_FIND_FIRST_SECTOR:
		if (!fdd_can_read_medium(drive ^ fdd_swap))
		{
                        fdc_notfound();
                        disc_sector_state[drive] = STATE_IDLE;
                        break;
		}
                if (cur_byte[drive] || !index_count[drive] || fdc_get_bitcell_period() != get_bitcell_period(drive) ||
                        disc_intersector_delay[drive] || disc_postdata_delay[drive] || disc_track_delay[drive] || disc_gap4_delay[drive])
                {
                        advance_byte();
                        break;
                }
                disc_sector_state[drive] = STATE_READ_FIRST_SECTOR;

                case STATE_READ_FIRST_SECTOR:
		if (!disc_postdata_delay[drive])
		{
	                if (fdc_data(s->data[cur_byte[drive]]))
        	                return;
		}
                advance_byte();
                if (!cur_byte[drive] && disc_gap_has_ended(drive))
                {
                        disc_sector_state[drive] = STATE_IDLE;
                        fdc_finishread(drive);
                }
                break;

                case STATE_READ_FIND_NEXT_SECTOR:
		if (!fdd_can_read_medium(drive ^ fdd_swap))
		{
                        fdc_notfound();
                        disc_sector_state[drive] = STATE_IDLE;
                        break;
		}
                if (index_count[drive] > 0)
                {
                        fdc_notfound();
                        disc_sector_state[drive] = STATE_IDLE;
                        break;
                }
                if (cur_byte[drive] || (fdc_get_bitcell_period() != get_bitcell_period(drive)) ||
                        disc_intersector_delay[drive] || disc_postdata_delay[drive] || disc_track_delay[drive] || disc_gap4_delay[drive])
                {
                        advance_byte();
                        break;
                }
                disc_sector_state[drive] = STATE_READ_NEXT_SECTOR;

                case STATE_READ_NEXT_SECTOR:
		if (!disc_postdata_delay[drive])
		{
	                if (fdc_data(s->data[cur_byte[drive]]))
	                        break;
		}
                advance_byte();
                if (!cur_byte[drive] && disc_gap_has_ended(drive))
                {
                        disc_sector_state[drive] = STATE_IDLE;
                        fdc_finishread(drive);
                }
                break;

                case STATE_WRITE_FIND_SECTOR:
		if (!fdd_can_read_medium(drive ^ fdd_swap))
		{
                        fdc_notfound();
                        disc_sector_state[drive] = STATE_IDLE;
                        break;
		}
                if (writeprot[drive] || swwp)
                {
                        fdc_writeprotect();
                }
                if (index_count[drive] > 1)
                {
                        fdc_notfound();
                        disc_sector_state[drive] = STATE_IDLE;
                        break;
                }
                if (cur_byte[drive] || disc_sector_track[drive] != s->c ||
                    disc_sector_side[drive] != s->h ||
                    disc_sector_sector[drive] != s->r ||
		    ((disc_sector_n[drive] != s->n) && (disc_sector_n[drive])) ||
                    (fdc_get_bitcell_period() != get_bitcell_period(drive)) ||
                        disc_intersector_delay[drive] || disc_postdata_delay[drive] || disc_track_delay[drive] || disc_gap4_delay[drive])
                {
                        advance_byte();
                        break;
                }
                disc_sector_state[drive] = STATE_WRITE_SECTOR;

                case STATE_WRITE_SECTOR:
		if (!disc_postdata_delay[drive])
		{
	                data = fdc_getdata(cur_byte[drive] == ((128 << s->n) - 1));
        	        if (data == -1)
                	        break;
	                if (!disable_write)  s->data[cur_byte[drive]] = data;
		}
                advance_byte();
                if (!cur_byte[drive] && disc_gap_has_ended(drive))
                {
                        disc_sector_state[drive] = STATE_IDLE;
                        if (!disable_write)  disc_sector_writeback[drive](drive);
                        fdc_finishread(drive);
                }
                break;

                case STATE_READ_FIND_ADDRESS:
		if (!fdd_can_read_medium(drive ^ fdd_swap))
		{
                        fdc_notfound();
                        disc_sector_state[drive] = STATE_IDLE;
                        break;
		}
                if (index_count[drive] > 0)
                {
                        fdc_notfound();
                        disc_sector_state[drive] = STATE_IDLE;
                        break;
                }
                if (cur_byte[drive] || (fdc_get_bitcell_period() != get_bitcell_period(drive)) ||
                        disc_intersector_delay[drive] || disc_postdata_delay[drive] || disc_track_delay[drive] || disc_gap4_delay[drive])
                {
                        advance_byte();
                        break;
                }
                disc_sector_state[drive] = STATE_READ_ADDRESS;
                case STATE_READ_ADDRESS:
                fdc_sectorid(s->c, s->h, s->r, s->n, 0, 0);
                disc_sector_state[drive] = STATE_IDLE;
                break;
                
                case STATE_FORMAT_FIND:
                if (writeprot[drive] || swwp)
                {
                        fdc_writeprotect();
                }
                if (!index_count[drive] || (fdc_get_bitcell_period() != get_bitcell_period(drive)) ||
                        disc_intersector_delay[drive] || disc_postdata_delay[drive] || disc_track_delay[drive] || disc_gap4_delay[drive])
                {
                        advance_byte();
                        break;
                }
		if (!(fdd_can_read_medium(drive ^ fdd_swap)))
		{
                        fdc_notfound();
                        disc_sector_state[drive] = STATE_IDLE;
                        break;
		}
                if (fdc_get_bitcell_period() != get_bitcell_period(drive))
                {
                        fdc_notfound();
                        disc_sector_state[drive] = STATE_IDLE;
                        break;
                }
                disc_sector_state[drive] = STATE_FORMAT;

                case STATE_FORMAT:
                if (!disc_intersector_delay[drive] && fdc_get_bitcell_period() == get_bitcell_period(drive) && !disable_write)
                        s->data[cur_byte[drive]] = disc_sector_fill[drive];
                advance_byte();
                if (index_count[drive] == 2)
                {
                        if (!disable_write)  disc_sector_writeback[drive](drive);
                        fdc_finishread(drive);
                        disc_sector_state[drive] = STATE_IDLE;
                }
                break;
        }
}