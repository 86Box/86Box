#include "ibm.h"
#include "disc.h"
#include "fdc.h"
#include "fdd.h"
#include "timer.h"

static struct
{
        int type;
        
        int track;

	int densel;

	int drate;

	int kbps;
	int fdc_kbps;

	int head;
} fdd[2];

/* Flags:
   Bit 0:	300 rpm supported;
   Bit 1:	360 rpm supported;
   Bit 2:	size (0 = 3.5", 1 = 5.25");
   Bit 3:	double density supported;
   Bit 4:	high density supported;
   Bit 5:	extended density supported;
   Bit 6:	double step for 40-track media;
*/
#define FLAG_RPM_300		1
#define FLAG_RPM_360		2
#define FLAG_525		4
#define FLAG_HOLE0		8
#define FLAG_HOLE1		16
#define FLAG_HOLE2		32
#define FLAG_DOUBLE_STEP	64

static struct
{
        int max_track;
	int flags;
} drive_types[] =
{
        {       /*None*/
                .max_track = 0,
		.flags = 0
        },
        {       /*5.25" DD*/
#ifdef MAINLINE
                .max_track = 41,
#else
                .max_track = 43,
#endif
		.flags = FLAG_RPM_300 | FLAG_525 | FLAG_HOLE0
        },
        {       /*5.25" HD*/
#ifdef MAINLINE
                .max_track = 82,
#else
                .max_track = 86,
#endif
		.flags = FLAG_RPM_360 | FLAG_525 | FLAG_HOLE0 | FLAG_HOLE1 | FLAG_DOUBLE_STEP
        },
        {       /*5.25" HD Dual RPM*/
#ifdef MAINLINE
                .max_track = 82,
#else
                .max_track = 86,
#endif
		.flags = FLAG_RPM_300 | FLAG_RPM_360 | FLAG_525 | FLAG_HOLE0 | FLAG_HOLE1 | FLAG_DOUBLE_STEP
        },
        {       /*3.5" DD*/
#ifdef MAINLINE
                .max_track = 82,
#else
                .max_track = 86,
#endif
		.flags = FLAG_RPM_300 | FLAG_HOLE0
        },
        {       /*3.5" HD*/
#ifdef MAINLINE
                .max_track = 82,
#else
                .max_track = 86,
#endif
		.flags = FLAG_RPM_300 | FLAG_HOLE0 | FLAG_HOLE1
        },
        {       /*3.5" HD 3-Mode*/
#ifdef MAINLINE
                .max_track = 82,
#else
                .max_track = 86,
#endif
		.flags = FLAG_RPM_300 | FLAG_RPM_360 | FLAG_HOLE0 | FLAG_HOLE1
        },
        {       /*3.5" ED*/
#ifdef MAINLINE
                .max_track = 82,
#else
                .max_track = 86,
#endif
		.flags = FLAG_RPM_300 | FLAG_HOLE0 | FLAG_HOLE1 | FLAG_HOLE2
        }
};

int fdd_swap = 0;

int fdd_stepping_motor_on[2] = {0, 0};
int fdd_track_diff[2] = {0, 0};
int fdd_track_direction[2] = {0, 0};
int fdd_old_track[2] = {0, 0};

int fdd_poll_time[2] = {0, 0};

void fdd_seek_poll(int poll_drive)
{
	if (!fdd_track_diff[poll_drive])
	{
		fdd_stepping_motor_on[poll_drive] = 0;
		return;
	}

	/* 80-track drive takes 6 µs per step, 40-track drive takes 10 µs. */
	// fdd_poll_time[poll_drive] += (drive_types[fdd[poll_drive].type].max_track <= 43) ? (10 * TIMER_USEC) : (6 * TIMER_USEC);
	fdd_poll_time[poll_drive] += (drive_types[fdd[poll_drive].type].max_track <= 43) ? (5 * TIMER_USEC) : (3 * TIMER_USEC);

	if (fdd_track_direction[poll_drive])
	{
		fdd[poll_drive].track++;

	        if (fdd[poll_drive].track > drive_types[fdd[poll_drive].type].max_track)
        	        fdd[poll_drive].track = drive_types[fdd[poll_drive].type].max_track;
	}
	else
	{
		fdd[poll_drive].track--;

        	if (fdd[poll_drive].track < 0)
	                fdd[poll_drive].track = 0;
	}

	fdd_track_diff[poll_drive]--;

	if (!fdd_track_diff[poll_drive])
	{
       	        fdc_discchange_clear(poll_drive);

		disc_seek(poll_drive, fdd[poll_drive].track);
		fdd_stepping_motor_on[poll_drive] = 0;
	}
}

void fdd_seek_poll_0()
{
	fdd_seek_poll(0);
}

void fdd_seek_poll_1()
{
	fdd_seek_poll(1);
}

#if 0
void fdd_seek(int drive, int track_diff)
{
        drive ^= fdd_swap;

	fdd_old_track[drive] = fdd[drive].track;

	if (!track_diff)
	{
		/* Do not turn on motor if there are no pulses to be sent. */
       	        fdc_discchange_clear(drive);
		return;
	}

	fdd_stepping_motor_on[drive] = (track_diff == 0) ? 0 : 1;
	if (fdd_stepping_motor_on[drive])  pclog("fdd_seek(): Stepping motor now on\n");

	if (track_diff < 0)
	{
		fdd_track_diff[drive] = -track_diff;
		fdd_track_direction[drive] = 0;
	}
	else
	{
		fdd_track_diff[drive] = track_diff;
		fdd_track_direction[drive] = 1;
	}

	fdd_old_track[drive] = fdd[drive].track;
}
#endif

void fdd_seek(int drive, int track_diff)
{
        int old_track;

        drive ^= fdd_swap;

        old_track = fdd[drive].track;

        fdd[drive].track += track_diff;
        
        if (fdd[drive].track < 0)
                fdd[drive].track = 0;

        if (fdd[drive].track > drive_types[fdd[drive].type].max_track)
                fdd[drive].track = drive_types[fdd[drive].type].max_track;

        // pclog("fdd_seek: drive=%i track_diff=%i old_track=%i track=%i\n", drive, track_diff, old_track, fdd[drive].track);
        // if (fdd[drive].track != old_track)
                // fdc_discchange_clear(drive);
	fdc_discchange_clear(drive);
        disc_seek(drive, fdd[drive].track);
        // disctime = 5000;
	disctime = 50;
}

int fdd_track0(int drive)
{
        drive ^= fdd_swap;

	/* If drive is disabled, TRK0 never gets set. */
	if (!drive_types[fdd[drive].type].max_track)  return 0;

        return !fdd[drive].track;
}

void fdd_set_densel(int densel)
{
	fdd[0].densel = densel;
	fdd[1].densel = densel;
}

int fdd_getrpm(int drive)
{
	int hole = disc_hole(drive);

        drive ^= fdd_swap;

	if (!(drive_types[fdd[drive].type].flags & FLAG_RPM_360))  return 300;
	if (!(drive_types[fdd[drive].type].flags & FLAG_RPM_300))  return 360;

	if (drive_types[fdd[drive].type].flags & FLAG_525)
	{
		return fdd[drive].densel ? 360 : 300;
	}
	else
	{
		/* disc_hole(drive) returns 0 for double density media, 1 for high density, and 2 for extended density. */
		if (hole == 1)
		{
			return fdd[drive].densel ? 300 : 360;
		}
		else
		{
			return 300;
		}
	}
}

void fdd_setswap(int swap)
{
        fdd_swap = swap ? 1 : 0;
}

int fdd_can_read_medium(int drive)
{
	int hole = disc_hole(drive);

	drive ^= fdd_swap;

	hole = 1 << (hole + 3);

//	pclog("Drive %02X, type %02X, hole flag %02X, flags %02X, result %02X\n", drive, fdd[drive].type, hole, drive_types[fdd[drive].type].flags, drive_types[fdd[drive].type].flags & hole);
	return (drive_types[fdd[drive].type].flags & hole) ? 1 : 0;
}

int fdd_doublestep_40(int drive)
{
        return drive_types[fdd[drive].type].flags & FLAG_DOUBLE_STEP;
}

void fdd_set_type(int drive, int type)
{
	fdd[drive].type = type;
}

int fdd_get_type(int drive)
{
	return fdd[drive].type;
}

int fdd_is_525(int drive)
{
        return drive_types[fdd[drive].type].flags & FLAG_525;
}

int fdd_is_ed(int drive)
{
        return drive_types[fdd[drive].type].flags & FLAG_HOLE2;
}

void fdd_set_head(int drive, int head)
{
	drive ^= fdd_swap;
	fdd[drive].head = head;
}

int fdd_get_head(int drive)
{
	return fdd[drive].head;
}

void fdd_init()
{
	fdd_stepping_motor_on[0] = fdd_stepping_motor_on[1] = 0;
	fdd_track_diff[0] = fdd_track_diff[1] = 0;

	timer_add(fdd_seek_poll_0, &(fdd_poll_time[0]), &(fdd_stepping_motor_on[0]), NULL);
	timer_add(fdd_seek_poll_1, &(fdd_poll_time[1]), &(fdd_stepping_motor_on[1]), NULL);
}
