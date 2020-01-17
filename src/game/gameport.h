/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Definitions for the generic game port handlers.
 *
 * NOTE:	This module needs a good cleanup someday.
 *
 * Version:	@(#)gameport.h	1.0.4	2018/11/11
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Sarah Walker, <tommowalker@tommowalker.co.uk>
 *
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2008-2017 Sarah Walker.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free  Software  Foundation; either  version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is  distributed in the hope that it will be useful, but
 * WITHOUT   ANY  WARRANTY;  without  even   the  implied  warranty  of
 * MERCHANTABILITY  or FITNESS  FOR A PARTICULAR  PURPOSE. See  the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *   Free Software Foundation, Inc.
 *   59 Temple Place - Suite 330
 *   Boston, MA 02111-1307
 *   USA.
 */
#ifndef EMU_GAMEPORT_H
# define EMU_GAMEPORT_H


#define MAX_PLAT_JOYSTICKS	8
#define MAX_JOYSTICKS		4

#define POV_X			0x80000000
#define POV_Y			0x40000000
#define SLIDER 			0x20000000

#define AXIS_NOT_PRESENT	-99999
#define JOYSTICK_TYPE_NONE 	8

#define JOYSTICK_PRESENT(n)	(joystick_state[n].plat_joystick_nr != 0)


typedef struct {
    char	name[260];

    int		a[8];
    int		b[32];
    int		p[4];
	int 	s[2];

    struct {
	char	name[260];
	int	id;
    }		axis[8];

    struct {
	char	name[260];
	int	id;
    }		button[32];

    struct {
	char	name[260];
	int	id;
    }		pov[4];

	struct
    {
    char 	name[260];
    int id;
    } 		slider[2];

    int		nr_axes;
    int		nr_buttons;
    int		nr_povs;
	int 	nr_sliders;
} plat_joystick_t;

typedef struct {
    int		axis[8];
    int		button[32];
    int		pov[4];

    int		plat_joystick_nr;
    int		axis_mapping[8];
    int		button_mapping[32];
    int		pov_mapping[4][2];
} joystick_t;

typedef struct {
    const char *name;

    void	*(*init)(void);
    void	(*close)(void *p);
    uint8_t	(*read)(void *p);
    void	(*write)(void *p);
    int		(*read_axis)(void *p, int axis);
    void	(*a0_over)(void *p);

    int		axis_count,
		button_count,
		pov_count;
    int		max_joysticks;
    const char	*axis_names[8];
    const char	*button_names[32];
    const char	*pov_names[4];
} joystick_if_t;


#ifdef __cplusplus
extern "C" {
#endif

#ifdef EMU_DEVICE_H
extern const device_t	gameport_device;
extern const device_t	gameport_201_device;
#endif

extern plat_joystick_t	plat_joystick_state[MAX_PLAT_JOYSTICKS];
extern joystick_t	joystick_state[MAX_JOYSTICKS];
extern int		joysticks_present;

extern int	joystick_type;


extern void	joystick_init(void);
extern void	joystick_close(void);
extern void	joystick_process(void);

extern char	*joystick_get_name(int js);
extern int	joystick_get_max_joysticks(int js);
extern int	joystick_get_axis_count(int js);
extern int	joystick_get_button_count(int js);
extern int	joystick_get_pov_count(int js);
extern char	*joystick_get_axis_name(int js, int id);
extern char	*joystick_get_button_name(int js, int id);
extern char	*joystick_get_pov_name(int js, int id);

extern void	gameport_update_joystick_type(void);

#ifdef __cplusplus
}
#endif


#endif	/*EMU_GAMEPORT_H*/
