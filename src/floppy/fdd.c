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
 * Version:	@(#)fdd.c	1.0.15	2019/10/20
 *
 * Authors:	Sarah Walker, <tommowalker@tommowalker.co.uk>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2018,2019 Fred N. van Kempen.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../timer.h"
#include "../plat.h"
#include "../ui.h"
#include "fdd.h"
#include "fdd_86f.h"
#include "fdd_fdi.h"
#include "fdd_imd.h"
#include "fdd_img.h"
#include "fdd_json.h"
#include "fdd_mfm.h"
#include "fdd_td0.h"
#include "fdc.h"


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
#define FLAG_RPM_300		   1
#define FLAG_RPM_360		   2
#define FLAG_525		   4
#define FLAG_DS			   8
#define FLAG_HOLE0		  16
#define FLAG_HOLE1		  32
#define FLAG_HOLE2		  64
#define FLAG_DOUBLE_STEP	 128
#define FLAG_INVERT_DENSEL	 256
#define FLAG_IGNORE_DENSEL	 512
#define FLAG_PS2		1024


typedef struct {
    int type;
    int track;
    int densel;
    int head;
    int turbo;
    int check_bpb;
} fdd_t;


fdd_t		fdd[FDD_NUM];

wchar_t		floppyfns[FDD_NUM][512];

pc_timer_t	fdd_poll_time[FDD_NUM];

static int	fdd_notfound = 0,
		driveloaders[FDD_NUM];

int		writeprot[FDD_NUM], fwriteprot[FDD_NUM],
		fdd_changed[FDD_NUM], ui_writeprot[FDD_NUM] = {0, 0, 0, 0},
		drive_empty[FDD_NUM] = {1, 1, 1, 1};

DRIVE		drives[FDD_NUM];

uint64_t	motoron[FDD_NUM];

fdc_t		*fdd_fdc;

d86f_handler_t   d86f_handler[FDD_NUM];


static const struct
{
    wchar_t *ext;
    void (*load)(int drive, wchar_t *fn);
    void (*close)(int drive);
    int size;
} loaders[]=
{
    {L"001",	 img_load,	 img_close, -1},
    {L"002",	 img_load,	 img_close, -1},
    {L"003",	 img_load,	 img_close, -1},
    {L"004",	 img_load,	 img_close, -1},
    {L"005",	 img_load,	 img_close, -1},
    {L"006",	 img_load,	 img_close, -1},
    {L"007",	 img_load,	 img_close, -1},
    {L"008",	 img_load,	 img_close, -1},
    {L"009",	 img_load,	 img_close, -1},
    {L"010",	 img_load,	 img_close, -1},
    {L"12",	 img_load,	 img_close, -1},
    {L"144",	 img_load,	 img_close, -1},
    {L"360",	 img_load,	 img_close, -1},
    {L"720",	 img_load,	 img_close, -1},
    {L"86F",	d86f_load,	d86f_close, -1},
    {L"BIN",	 img_load,	 img_close, -1},
    {L"CQ",	 img_load,	 img_close, -1},
    {L"CQM",	 img_load,	 img_close, -1},
    {L"DDI",	 img_load,	 img_close, -1},
    {L"DSK",	 img_load,	 img_close, -1},
    {L"FDI",	 fdi_load,	 fdi_close, -1},
    {L"FDF",	 img_load,	 img_close, -1},
    {L"FLP",	 img_load,	 img_close, -1},
    {L"HDM",	 img_load,	 img_close, -1},
    {L"IMA",	 img_load,	 img_close, -1},
    {L"IMD",	 imd_load,	 imd_close, -1},
    {L"IMG",	 img_load,	 img_close, -1},
    {L"JSON",	json_load,	json_close, -1},
    {L"MFM",	 mfm_load,	 mfm_close, -1},
    {L"TD0",	 td0_load,	 td0_close, -1},
    {L"VFD",	 img_load,	 img_close, -1},
    {L"XDF",	 img_load,	 img_close, -1},
    {0,		 0,		 0,	     0}
};


static const struct
{
    int max_track;
    int flags;
    const char *name;
    const char *internal_name;
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


#ifdef ENABLE_FDD_LOG
int fdd_do_log = ENABLE_FDD_LOG;


static void
fdd_log(const char *fmt, ...)
{
   va_list ap;

   if (fdd_do_log)
   {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
   }
}
#else
#define fdd_log(fmt, ...)
#endif


char *
fdd_getname(int type)
{
    return (char *)drive_types[type].name;
}


char *
fdd_get_internal_name(int type)
{
    return (char *)drive_types[type].internal_name;
}


int
fdd_get_from_internal_name(char *s)
{
    int c = 0;

    while (strlen(drive_types[c].internal_name)) {
	if (!strcmp((char *)drive_types[c].internal_name, s))
		return c;
	c++;
    }

    return 0;
}


/* This is needed for the dump as 86F feature. */
void
fdd_do_seek(int drive, int track)
{
    if (drives[drive].seek)
	drives[drive].seek(drive, track);
}


void
fdd_forced_seek(int drive, int track_diff)
{
    fdd[drive].track += track_diff;

    if (fdd[drive].track < 0)
	fdd[drive].track = 0;

    if (fdd[drive].track > drive_types[fdd[drive].type].max_track)
	fdd[drive].track = drive_types[fdd[drive].type].max_track;

    fdd_do_seek(drive, fdd[drive].track);
}


void
fdd_seek(int drive, int track_diff)
{
    if (!track_diff)
	return;

    fdd[drive].track += track_diff;

    if (fdd[drive].track < 0)
	fdd[drive].track = 0;

    if (fdd[drive].track > drive_types[fdd[drive].type].max_track)
	fdd[drive].track = drive_types[fdd[drive].type].max_track;

    fdd_changed[drive] = 0;

    fdd_do_seek(drive, fdd[drive].track);
}


int
fdd_track0(int drive)
{
    /* If drive is disabled, TRK0 never gets set. */
    if (!drive_types[fdd[drive].type].max_track)  return 0;

    return !fdd[drive].track;
}


int
fdd_current_track(int drive)
{
    return fdd[drive].track;
}


void
fdd_set_densel(int densel)
{
    int i = 0;

    for (i = 0; i < 4; i++) {
	if (drive_types[fdd[i].type].flags & FLAG_INVERT_DENSEL)
		fdd[i].densel = densel ^ 1;
	else
		fdd[i].densel = densel;
    }
}


int
fdd_getrpm(int drive)
{
    int densel = 0;
    int hole;

    hole = fdd_hole(drive);
    densel = fdd[drive].densel;

    if (drive_types[fdd[drive].type].flags & FLAG_INVERT_DENSEL)
	densel ^= 1;

    if (!(drive_types[fdd[drive].type].flags & FLAG_RPM_360))
	return 300;
    if (!(drive_types[fdd[drive].type].flags & FLAG_RPM_300))
	return 360;

    if (drive_types[fdd[drive].type].flags & FLAG_525)
	return densel ? 360 : 300;
    else {
	/* fdd_hole(drive) returns 0 for double density media, 1 for high density, and 2 for extended density. */
	if (hole == 1)
		return densel ? 300 : 360;
	else
		return 300;
    }
}


int
fdd_can_read_medium(int drive)
{
    int hole = fdd_hole(drive);

    hole = 1 << (hole + 4);

    return !!(drive_types[fdd[drive].type].flags & hole);
}


int
fdd_doublestep_40(int drive)
{
    return !!(drive_types[fdd[drive].type].flags & FLAG_DOUBLE_STEP);
}


void
fdd_set_type(int drive, int type)
{
    int old_type = fdd[drive].type;
    fdd[drive].type = type;
    if ((drive_types[old_type].flags ^ drive_types[type].flags) & FLAG_INVERT_DENSEL)
	fdd[drive].densel ^= 1;
}


int
fdd_get_type(int drive)
{
    return fdd[drive].type;
}


int
fdd_get_flags(int drive)
{
    return drive_types[fdd[drive].type].flags;
}


int
fdd_is_525(int drive)
{
    return drive_types[fdd[drive].type].flags & FLAG_525;
}


int
fdd_is_dd(int drive)
{
    return (drive_types[fdd[drive].type].flags & 0x70) == 0x10;
}


int
fdd_is_ed(int drive)
{
    return drive_types[fdd[drive].type].flags & FLAG_HOLE2;
}


int
fdd_is_double_sided(int drive)
{
    return drive_types[fdd[drive].type].flags & FLAG_DS;
}


void
fdd_set_head(int drive, int head)
{
    if (head && !fdd_is_double_sided(drive))
	fdd[drive].head = 0;
    else
	fdd[drive].head = head;
}


int
fdd_get_head(int drive)
{
    if (!fdd_is_double_sided(drive))
	return 0;
    return fdd[drive].head;
}


void
fdd_set_turbo(int drive, int turbo)
{
    fdd[drive].turbo = turbo;
}


int
fdd_get_turbo(int drive)
{
    return fdd[drive].turbo;
}


void fdd_set_check_bpb(int drive, int check_bpb)
{
    fdd[drive].check_bpb = check_bpb;
}


int
fdd_get_check_bpb(int drive)
{
    return fdd[drive].check_bpb;
}


int
fdd_get_densel(int drive)
{
    return fdd[drive].densel;
}


void
fdd_load(int drive, wchar_t *fn)
{
    int c = 0, size;
    wchar_t *p;
    FILE *f;

    fdd_log("FDD: loading drive %d with '%ls'\n", drive, fn);

    if (!fn)
	return;
    p = plat_get_extension(fn);
    if (!p)
	return;
    f = plat_fopen(fn, L"rb");
    if (!f)
	return;
    fseek(f, -1, SEEK_END);
    size = ftell(f) + 1;
    fclose(f);        
    while (loaders[c].ext) {
	if (!wcscasecmp(p, (wchar_t *) loaders[c].ext) && (size == loaders[c].size || loaders[c].size == -1)) {
		driveloaders[drive] = c;
		memcpy(floppyfns[drive], fn, (wcslen(fn) << 1) + 2);
		d86f_setup(drive);
		loaders[c].load(drive, floppyfns[drive]);
		drive_empty[drive] = 0;
		fdd_forced_seek(drive, 0);
		fdd_changed[drive] = 1;
		return;
	}
	c++;
    }
    fdd_log("FDD: could not load '%ls' %s\n",fn,p);
    drive_empty[drive] = 1;
    fdd_set_head(drive, 0);
    memset(floppyfns[drive], 0, sizeof(floppyfns[drive]));
    ui_sb_update_icon_state(drive, 1);
}


void
fdd_close(int drive)
{
    fdd_log("FDD: closing drive %d\n", drive);

    d86f_stop(drive);	/* Call this first of all to make sure the 86F poll is back to idle state. */
    if (loaders[driveloaders[drive]].close)
	loaders[driveloaders[drive]].close(drive);
    drive_empty[drive] = 1;
    fdd_set_head(drive, 0);
    floppyfns[drive][0] = 0;
    drives[drive].hole = NULL;
    drives[drive].poll = NULL;
    drives[drive].seek = NULL;
    drives[drive].readsector = NULL;
    drives[drive].writesector = NULL;
    drives[drive].comparesector = NULL;
    drives[drive].readaddress = NULL;
    drives[drive].format = NULL;
    drives[drive].byteperiod = NULL;
    drives[drive].stop = NULL;
    d86f_destroy(drive);
    ui_sb_update_icon_state(drive, 1);
}


int
fdd_hole(int drive)
{
    if (drives[drive].hole)
	return drives[drive].hole(drive);
    else
	return 0;
}


uint64_t
fdd_byteperiod(int drive)
{
    if (!fdd_get_turbo(drive) && drives[drive].byteperiod)
	return drives[drive].byteperiod(drive);
    else
	return 32ULL * TIMER_USEC;
}


void
fdd_set_motor_enable(int drive, int motor_enable)
{
    /* I think here is where spin-up and spin-down should be implemented. */
    if (motor_enable && !motoron[drive])
	timer_set_delay_u64(&fdd_poll_time[drive], fdd_byteperiod(drive));
    else if (!motor_enable)
	timer_disable(&fdd_poll_time[drive]);
    motoron[drive] = motor_enable;
}


static void
fdd_poll(void *priv)
{
    int drive;
    DRIVE *drv = (DRIVE *) priv;

    drive = drv->id;

    if (drive >= FDD_NUM)
	fatal("Attempting to poll floppy drive %i that is not supposed to be there\n", drive);

    timer_advance_u64(&fdd_poll_time[drive], fdd_byteperiod(drive));

    if (drv->poll)
	drv->poll(drive);

    if (fdd_notfound) {
	fdd_notfound--;
	if (!fdd_notfound)
		fdc_noidam(fdd_fdc);
    }
}


int
fdd_get_bitcell_period(int rate)
{
    int bit_rate = 250;

    switch (rate) {
	case 0:	/*High density*/
		bit_rate = 500;
		break;
	case 1:	/*Double density (360 rpm)*/
		bit_rate = 300;
		break;
	case 2:	/*Double density*/
		bit_rate = 250;
		break;
	case 3:	/*Extended density*/
		bit_rate = 1000;
		break;
    }

    return 1000000 / bit_rate*2; /*Bitcell period in ns*/
}


void
fdd_reset(void)
{
    int i;

    for (i = 0; i < 4; i++) {
	drives[i].id = i;
	timer_add(&(fdd_poll_time[i]), fdd_poll, &drives[i], 0);
    }
}


void
fdd_readsector(int drive, int sector, int track, int side, int density, int sector_size)
{
    if (drives[drive].readsector)
	drives[drive].readsector(drive, sector, track, side, density, sector_size);
    else
	fdd_notfound = 1000;
}


void
fdd_writesector(int drive, int sector, int track, int side, int density, int sector_size)
{
    if (drives[drive].writesector)
	drives[drive].writesector(drive, sector, track, side, density, sector_size);
    else
	fdd_notfound = 1000;
}


void
fdd_comparesector(int drive, int sector, int track, int side, int density, int sector_size)
{
    if (drives[drive].comparesector)
	drives[drive].comparesector(drive, sector, track, side, density, sector_size);
    else
	fdd_notfound = 1000;
}


void
fdd_readaddress(int drive, int side, int density)
{
    if (drives[drive].readaddress)
	drives[drive].readaddress(drive, side, density);
}


void
fdd_format(int drive, int side, int density, uint8_t fill)
{
    if (drives[drive].format)
	drives[drive].format(drive, side, density, fill);
    else
	fdd_notfound = 1000;
}


void
fdd_stop(int drive)
{
    if (drives[drive].stop)
	drives[drive].stop(drive);
}


void
fdd_set_fdc(void *fdc)
{
    fdd_fdc = (fdc_t *) fdc;
}


void
fdd_init(void)
{
    int i;

    for (i = 0; i < 4; i++) {
	drives[i].poll = 0;
	drives[i].seek = 0;
	drives[i].readsector = 0;
    }

    img_init();
    d86f_init();
    td0_init();
    imd_init();
    json_init();

    fdd_load(0, floppyfns[0]);
    fdd_load(1, floppyfns[1]);
    fdd_load(2, floppyfns[2]);
    fdd_load(3, floppyfns[3]);
}
