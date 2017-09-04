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
 * Version:	@(#)fdd.h	1.0.2	2017/09/03
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */
#ifndef EMU_FDD_H
# define EMU_FDD_H


#define SEEK_RECALIBRATE -999


extern int fdd_swap;


extern void	fdd_forced_seek(int drive, int track_diff);
extern void	fdd_seek(int drive, int track_diff);
extern int	fdd_track0(int drive);
extern int	fdd_getrpm(int drive);
extern void	fdd_set_densel(int densel);
extern int	fdd_can_read_medium(int drive);
extern int	fdd_doublestep_40(int drive);
extern int	fdd_is_525(int drive);
extern int	fdd_is_dd(int drive);
extern int	fdd_is_ed(int drive);
extern int	fdd_is_double_sided(int drive);
extern void	fdd_set_head(int drive, int head);
extern int	fdd_get_head(int drive);
extern void	fdd_set_turbo(int drive, int turbo);
extern int	fdd_get_turbo(int drive);
extern void	fdd_set_check_bpb(int drive, int check_bpb);
extern int	fdd_get_check_bpb(int drive);

extern void	fdd_set_type(int drive, int type);
extern int	fdd_get_type(int drive);

extern int	fdd_get_flags(int drive);

extern void	fdd_init(void);
extern int	fdd_get_densel(int drive);

extern void	fdd_setswap(int swap);

extern char	*fdd_getname(int type);

extern char	*fdd_get_internal_name(int type);
extern int	fdd_get_from_internal_name(char *s);

extern int	fdd_track(int drive);


#endif	/*EMU_FDD_H*/
