/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the floppy drive emulation.
 *
 * Version:	@(#)fdd.c	1.0.5	2017/11/04
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "floppy.h"
#include "fdc.h"
#include "fdd.h"


typedef struct {
    int type;
    int track;
    int densel;
    int head;
    int turbo;
    int check_bpb;
} fdd_t;


fdd_t	fdd[FDD_NUM];
int	ui_writeprot[FDD_NUM] = {0, 0, 0, 0};


/* Flags:
   Bit  0:	300 rpm supported;
   Bit  1:	360 rpm supported;
   Bit  2:	size (0 = 3.5", 1 = 5.25");
   Bit  3:	sides (0 = 1, 1 = 2);
   Bit  4:	double density supported;
   Bit  5:	high density supported;
   Bit  6:	extended density supported;
   Bit  7:	double step for 40-track media;
   Bit  8:	invert DENSEL polarity;
   Bit  9:	ignore DENSEL;
   Bit 10:	drive is a PS/2 drive;
*/
#define FLAG_RPM_300		1
#define FLAG_RPM_360		2
#define FLAG_525		   4
#define FLAG_DS			   8
#define FLAG_HOLE0		  16
#define FLAG_HOLE1		  32
#define FLAG_HOLE2		  64
#define FLAG_DOUBLE_STEP	 128
#define FLAG_INVERT_DENSEL	 256
#define FLAG_IGNORE_DENSEL	 512
#define FLAG_PS2		1024

static struct
{
        int max_track;
	int flags;
        char name[64];
        char internal_name[24];
} drive_types[] =
{
        {       /*None*/
                0, 0, "None", "none"
        },
        {       /*5.25" 1DD*/
                43, FLAG_RPM_300 | FLAG_525 | FLAG_HOLE0, "5.25\" 180k", "525_1dd"
        },
        {       /*5.25" DD*/
                43, FLAG_RPM_300 | FLAG_525 | FLAG_DS | FLAG_HOLE0, "5.25\" 360k", "525_2dd"
        },
        {       /*5.25" QD*/
                86, FLAG_RPM_300 | FLAG_525 | FLAG_DS | FLAG_HOLE0 | FLAG_DOUBLE_STEP, "5.25\" 720k", "525_2qd"
        },
        {       /*5.25" HD PS/2*/
                86, FLAG_RPM_360 | FLAG_525 | FLAG_DS | FLAG_HOLE0 | FLAG_HOLE1 | FLAG_DOUBLE_STEP | FLAG_INVERT_DENSEL | FLAG_PS2, "5.25\" 1.2M PS/2", "525_2hd_ps2"
        },
        {       /*5.25" HD*/
                86, FLAG_RPM_360 | FLAG_525 | FLAG_DS | FLAG_HOLE0 | FLAG_HOLE1 | FLAG_DOUBLE_STEP, "5.25\" 1.2M", "525_2hd"
        },
        {       /*5.25" HD Dual RPM*/
                86, FLAG_RPM_300 | FLAG_RPM_360 | FLAG_525 | FLAG_DS | FLAG_HOLE0 | FLAG_HOLE1 | FLAG_DOUBLE_STEP, "5.25\" 1.2M 300/360 RPM", "525_2hd_dualrpm"
        },
        {       /*3.5" 1DD*/
                86, FLAG_RPM_300 | FLAG_HOLE0 | FLAG_DOUBLE_STEP, "3.5\" 360k", "35_1dd"
        },
        {       /*3.5" DD*/
                86, FLAG_RPM_300 | FLAG_DS | FLAG_HOLE0 | FLAG_DOUBLE_STEP, "3.5\" 720k", "35_2dd"
        },
        {       /*3.5" HD PS/2*/
                86, FLAG_RPM_300 | FLAG_DS | FLAG_HOLE0 | FLAG_HOLE1 | FLAG_DOUBLE_STEP | FLAG_INVERT_DENSEL | FLAG_PS2, "3.5\" 1.44M PS/2", "35_2hd_ps2"
        },
        {       /*3.5" HD*/
                86, FLAG_RPM_300 | FLAG_DS | FLAG_HOLE0 | FLAG_HOLE1 | FLAG_DOUBLE_STEP, "3.5\" 1.44M", "35_2hd"
        },
        {       /*3.5" HD PC-98*/
                86, FLAG_RPM_300 | FLAG_RPM_360 | FLAG_DS | FLAG_HOLE0 | FLAG_HOLE1 | FLAG_DOUBLE_STEP | FLAG_INVERT_DENSEL, "3.5\" 1.25M PC-98", "35_2hd_nec"
        },
        {       /*3.5" HD 3-Mode*/
                86, FLAG_RPM_300 | FLAG_RPM_360 | FLAG_DS | FLAG_HOLE0 | FLAG_HOLE1 | FLAG_DOUBLE_STEP, "3.5\" 1.44M 300/360 RPM", "35_2hd_3mode"
        },
        {       /*3.5" ED*/
                86, FLAG_RPM_300 | FLAG_DS | FLAG_HOLE0 | FLAG_HOLE1 | FLAG_HOLE2 | FLAG_DOUBLE_STEP, "3.5\" 2.88M", "35_2ed"
        },
        {       /*End of list*/
		-1, -1, "", ""
        }
};

int fdd_swap = 0;

char *fdd_getname(int type)
{
        return drive_types[type].name;
}

char *fdd_get_internal_name(int type)
{
        return drive_types[type].internal_name;
}

int fdd_get_from_internal_name(char *s)
{
	int c = 0;
	
	while (strlen(drive_types[c].internal_name))
	{
		if (!strcmp(drive_types[c].internal_name, s))
			return c;
		c++;
	}
	
	return 0;
}

void fdd_forced_seek(int drive, int track_diff)
{
        drive = real_drive(drive);

        fdd[drive].track += track_diff;
        
        if (fdd[drive].track < 0)
                fdd[drive].track = 0;

        if (fdd[drive].track > drive_types[fdd[drive].type].max_track)
                fdd[drive].track = drive_types[fdd[drive].type].max_track;

        floppy_seek(drive, fdd[drive].track);
        floppytime = 5000;
}

void fdd_seek(int drive, int track_diff)
{
        drive = real_drive(drive);

	if (!track_diff)
	{
	        floppytime = 5000;
		return;
	}

        fdd[drive].track += track_diff;

        if (fdd[drive].track < 0)
                fdd[drive].track = 0;

        if (fdd[drive].track > drive_types[fdd[drive].type].max_track)
                fdd[drive].track = drive_types[fdd[drive].type].max_track;

	fdc_floppychange_clear(drive);
        floppy_seek(drive, fdd[drive].track);
        floppytime = 5000;
}

int fdd_track0(int drive)
{
        drive = real_drive(drive);

	/* If drive is disabled, TRK0 never gets set. */
	if (!drive_types[fdd[drive].type].max_track)  return 0;

        return !fdd[drive].track;
}

int fdd_track(int drive)
{
	return fdd[drive].track;
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
	int hole = floppy_hole(drive);

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
		/* floppy_hole(drive) returns 0 for double density media, 1 for high density, and 2 for extended density. */
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
	int hole = floppy_hole(drive);

	drive = real_drive(drive);

	hole = 1 << (hole + 3);

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

int fdd_is_dd(int drive)
{
        return (drive_types[fdd[drive].type].flags & 0x70) == 0x10;
}

int fdd_is_ed(int drive)
{
        return drive_types[fdd[drive].type].flags & FLAG_HOLE2;
}

int fdd_is_double_sided(int drive)
{
        return drive_types[fdd[drive].type].flags & FLAG_DS;
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

void fdd_set_turbo(int drive, int turbo)
{
	fdd[drive].turbo = turbo;
}

int fdd_get_turbo(int drive)
{
	return fdd[drive].turbo;
}

void fdd_set_check_bpb(int drive, int check_bpb)
{
	fdd[drive].check_bpb = check_bpb;
}

int fdd_get_check_bpb(int drive)
{
	return fdd[drive].check_bpb;
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
