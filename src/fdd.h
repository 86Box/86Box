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
 * Version:	@(#)fdd.h	1.0.0	2017/05/30
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */

#define SEEK_RECALIBRATE -999
void fdd_forced_seek(int drive, int track_diff);
void fdd_seek(int drive, int track_diff);
int fdd_track0(int drive);
int fdd_getrpm(int drive);
void fdd_set_densel(int densel);
int fdd_can_read_medium(int drive);
int fdd_doublestep_40(int drive);
int fdd_is_525(int drive);
int fdd_is_dd(int drive);
int fdd_is_ed(int drive);
int fdd_is_double_sided(int drive);
void fdd_set_head(int drive, int head);
int fdd_get_head(int drive);
void fdd_set_turbo(int drive, int turbo);
int fdd_get_turbo(int drive);

void fdd_set_type(int drive, int type);
int fdd_get_type(int drive);

int fdd_get_flags(int drive);

extern int fdd_swap;

void fdd_init();
int fdd_get_densel(int drive);

void fdd_setswap(int swap);

char *fdd_getname(int type);

char *fdd_get_internal_name(int type);
int fdd_get_from_internal_name(char *s);
