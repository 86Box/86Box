/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#include "ibm.h"
#include "disc.h"
#include "fdc.h"
#include "fdd.h"

static struct
{
        int type;
        
        int track;

	int densel;

	int head;
} fdd[FDD_NUM];

/* Flags:
   Bit 0:	300 rpm supported;
   Bit 1:	360 rpm supported;
   Bit 2:	size (0 = 3.5", 1 = 5.25");
   Bit 3:	double density supported;
   Bit 4:	high density supported;
   Bit 5:	extended density supported;
   Bit 6:	double step for 40-track media;
   Bit 7:	invert DENSEL polarity;
   Bit 8:	ignore DENSEL;
   Bit 9:	drive is a PS/2 drive;
*/
#define FLAG_RPM_300		1
#define FLAG_RPM_360		2
#define FLAG_525		4
#define FLAG_HOLE0		8
#define FLAG_HOLE1		16
#define FLAG_HOLE2		32
#define FLAG_DOUBLE_STEP	64
#define FLAG_INVERT_DENSEL	128
#define FLAG_IGNORE_DENSEL	256
#define FLAG_PS2		512

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
                .max_track = 43,
		.flags = FLAG_RPM_300 | FLAG_525 | FLAG_HOLE0
        },
        {       /*5.25" HD*/
                .max_track = 86,
		.flags = FLAG_RPM_360 | FLAG_525 | FLAG_HOLE0 | FLAG_HOLE1 | FLAG_DOUBLE_STEP
        },
        {       /*5.25" HD Dual RPM*/
                .max_track = 86,
		.flags = FLAG_RPM_300 | FLAG_RPM_360 | FLAG_525 | FLAG_HOLE0 | FLAG_HOLE1 | FLAG_DOUBLE_STEP
        },
        {       /*3.5" DD*/
                .max_track = 86,
		.flags = FLAG_RPM_300 | FLAG_HOLE0 | FLAG_DOUBLE_STEP
        },
        {       /*3.5" HD PS/2*/
                .max_track = 86,
		.flags = FLAG_RPM_300 | FLAG_HOLE0 | FLAG_HOLE1 | FLAG_DOUBLE_STEP | FLAG_INVERT_DENSEL | FLAG_PS2
        },
        {       /*3.5" HD*/
                .max_track = 86,
		.flags = FLAG_RPM_300 | FLAG_HOLE0 | FLAG_HOLE1 | FLAG_DOUBLE_STEP
        },
        {       /*3.5" HD PC-98*/
                .max_track = 86,
		.flags = FLAG_RPM_300 | FLAG_RPM_360 | FLAG_HOLE0 | FLAG_HOLE1 | FLAG_DOUBLE_STEP | FLAG_INVERT_DENSEL
        },
        {       /*3.5" HD 3-Mode*/
                .max_track = 86,
		.flags = FLAG_RPM_300 | FLAG_RPM_360 | FLAG_HOLE0 | FLAG_HOLE1 | FLAG_DOUBLE_STEP
        },
        {       /*3.5" ED*/
                .max_track = 86,
		.flags = FLAG_RPM_300 | FLAG_HOLE0 | FLAG_HOLE1 | FLAG_HOLE2 | FLAG_DOUBLE_STEP
        }
};

int fdd_swap = 0;

void fdd_forced_seek(int drive, int track_diff)
{
        drive = real_drive(drive);

        fdd[drive].track += track_diff;

	// pclog("Seeking %i tracks...\n", track_diff);
        
        if (fdd[drive].track < 0)
                fdd[drive].track = 0;

        if (fdd[drive].track > drive_types[fdd[drive].type].max_track)
                fdd[drive].track = drive_types[fdd[drive].type].max_track;

        disc_seek(drive, fdd[drive].track);
        disctime = 5000;
}

void fdd_seek(int drive, int track_diff)
{
        drive = real_drive(drive);

	if (!track_diff)
	{
	        disctime = 5000;
		return;
	}

        fdd[drive].track += track_diff;

	// pclog("Seeking %i tracks...\n", track_diff);
        
        if (fdd[drive].track < 0)
                fdd[drive].track = 0;

        if (fdd[drive].track > drive_types[fdd[drive].type].max_track)
                fdd[drive].track = drive_types[fdd[drive].type].max_track;

	fdc_discchange_clear(drive);
        disc_seek(drive, fdd[drive].track);
        disctime = 5000;
}

int fdd_track0(int drive)
{
        drive = real_drive(drive);

	/* If drive is disabled, TRK0 never gets set. */
	if (!drive_types[fdd[drive].type].max_track)  return 0;

        return !fdd[drive].track;
}

void fdd_set_densel(int densel)
{
	int i = 0;

	for (i = 0; i < 4; i++)
	{
		if (drive_types[fdd[i].type].flags & FLAG_INVERT_DENSEL)
		{
			fdd[i].densel = densel ^ 1;
		}
		else
		{
			fdd[i].densel = densel;
		}
	}
}

int fdd_getrpm(int drive)
{
	int hole = disc_hole(drive);

	int densel = 0;

        drive = real_drive(drive);

	densel = fdd[drive].densel;

	if (drive_types[fdd[drive].type].flags & FLAG_INVERT_DENSEL)
	{
		densel ^= 1;
	}

	if (!(drive_types[fdd[drive].type].flags & FLAG_RPM_360))  return 300;
	if (!(drive_types[fdd[drive].type].flags & FLAG_RPM_300))  return 360;

	if (drive_types[fdd[drive].type].flags & FLAG_525)
	{
		return densel ? 360 : 300;
	}
	else
	{
		/* disc_hole(drive) returns 0 for double density media, 1 for high density, and 2 for extended density. */
		if (hole == 1)
		{
			return densel ? 300 : 360;
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

	drive = real_drive(drive);

	hole = 1 << (hole + 3);

//	pclog("Drive %02X, type %02X, hole flag %02X, flags %02X, result %02X\n", drive, fdd[drive].type, hole, drive_types[fdd[drive].type].flags, drive_types[fdd[drive].type].flags & hole);
	return (drive_types[fdd[drive].type].flags & hole) ? 1 : 0;
}

int fdd_doublestep_40(int drive)
{
        return (drive_types[fdd[drive].type].flags & FLAG_DOUBLE_STEP) ? 1 : 0;
}

void fdd_set_type(int drive, int type)
{
	int old_type = fdd[drive].type;
	fdd[drive].type = type;
	if ((drive_types[old_type].flags ^ drive_types[type].flags) & FLAG_INVERT_DENSEL)
	{
		fdd[drive].densel ^= 1;
	}
}

int fdd_get_type(int drive)
{
	return fdd[drive].type;
}

int fdd_get_flags(int drive)
{
	return drive_types[fdd[drive].type].flags;
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
	drive = real_drive(drive);
	fdd[drive].head = head;
}

int fdd_get_head(int drive)
{
	return fdd[drive].head;
}

int fdd_get_densel(int drive)
{
	if (drive_types[fdd[drive].type].flags & FLAG_INVERT_DENSEL)
	{
		return fdd[drive].densel ^ 1;
	}
	else
	{
		return fdd[drive].densel;
	}
}

void fdd_init()
{
}
