/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Implementation of a generic Game Port.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Sarah Walker, <tommowalker@tommowalker.co.uk>
 *
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2008-2018 Sarah Walker.
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
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/machine.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/gameport.h>
#include <86box/joystick_ch_flightstick_pro.h>
#include <86box/joystick_standard.h>
#include <86box/joystick_sw_pad.h>
#include <86box/joystick_tm_fcs.h>


typedef struct {
    pc_timer_t	timer;
    int		axis_nr;
    struct _gameport_ *gameport;
} g_axis_t;

typedef struct _gameport_ {
    uint8_t	state;

    g_axis_t	axis[4];

    const joystick_if_t *joystick;
    void	*joystick_dat;
} gameport_t;


int	joystick_type = 0;


static const joystick_if_t joystick_none = {
    "None",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    0,
    0,
    0
};


static const struct {
    const char		*internal_name;
    const joystick_if_t	*joystick;
} joysticks[] = {
    { "none",			&joystick_none			},
    { "standard_2button",	&joystick_standard		},
    { "standard_4button",	&joystick_standard_4button	},
    { "standard_6button",	&joystick_standard_6button	},
    { "standard_8button",	&joystick_standard_8button	},
    { "4axis_4button",		&joystick_4axis_4button		},
    { "ch_flighstick_pro",	&joystick_ch_flightstick_pro	},
    { "sidewinder_pad",		&joystick_sw_pad		},
    { "thrustmaster_fcs",	&joystick_tm_fcs		},
    { "",			NULL				}
};
static gameport_t *gameport_global = NULL;


char *
joystick_get_name(int js)
{
    if (! joysticks[js].joystick)
	return(NULL);
    return((char *)joysticks[js].joystick->name);
}


char *
joystick_get_internal_name(int js)
{
    return((char *) joysticks[js].internal_name);
}


int
joystick_get_from_internal_name(char *s)
{
    int c = 0;

    while (strlen((char *) joysticks[c].internal_name))
    {
	if (!strcmp((char *) joysticks[c].internal_name, s))
		return c;
	c++;
    }

    return 0;
}


int
joystick_get_max_joysticks(int js)
{
    return(joysticks[js].joystick->max_joysticks);
}


int
joystick_get_axis_count(int js)
{
    return(joysticks[js].joystick->axis_count);
}


int
joystick_get_button_count(int js)
{
    return(joysticks[js].joystick->button_count);
}


int
joystick_get_pov_count(int js)
{
    return(joysticks[js].joystick->pov_count);
}


char *
joystick_get_axis_name(int js, int id)
{
    return((char *)joysticks[js].joystick->axis_names[id]);
}


char *
joystick_get_button_name(int js, int id)
{
    return((char *)joysticks[js].joystick->button_names[id]);
}


char *
joystick_get_pov_name(int js, int id)
{
    return (char *)joysticks[js].joystick->pov_names[id];
}


static void
gameport_time(gameport_t *gameport, int nr, int axis)
{
    if (axis == AXIS_NOT_PRESENT)
	timer_disable(&gameport->axis[nr].timer);
    else {
	axis += 32768;
	axis = (axis * 100) / 65; /*Axis now in ohms*/
	axis = (axis * 11) / 1000;
	timer_set_delay_u64(&gameport->axis[nr].timer, TIMER_USEC * (axis + 24)); /*max = 11.115 ms*/
    }
}


static void
gameport_write(uint16_t addr, uint8_t val, void *priv)
{
    gameport_t *p = (gameport_t *)priv;

    p->state |= 0x0f;

    gameport_time(p, 0, p->joystick->read_axis(p->joystick_dat, 0));
    gameport_time(p, 1, p->joystick->read_axis(p->joystick_dat, 1));
    gameport_time(p, 2, p->joystick->read_axis(p->joystick_dat, 2));
    gameport_time(p, 3, p->joystick->read_axis(p->joystick_dat, 3));
	
    p->joystick->write(p->joystick_dat);

    cycles -= ISA_CYCLES(8);
}


static uint8_t
gameport_read(uint16_t addr, void *priv)
{
    gameport_t *p = (gameport_t *)priv;
    uint8_t ret;

    ret = p->state | p->joystick->read(p->joystick_dat);

    cycles -= ISA_CYCLES(8);

    return(ret);
}


static void
timer_over(void *priv)
{
    g_axis_t *axis = (g_axis_t *)priv;
    gameport_t *p = axis->gameport;

    p->state &= ~(1 << axis->axis_nr);

    if (axis == &p->axis[0])
	p->joystick->a0_over(p->joystick_dat);
}


static void *
init_common(void)
{
    gameport_t *p = malloc(sizeof(gameport_t));

    memset(p, 0x00, sizeof(gameport_t));

    p->axis[0].gameport = p;
    p->axis[1].gameport = p;
    p->axis[2].gameport = p;
    p->axis[3].gameport = p;

    p->axis[0].axis_nr = 0;
    p->axis[1].axis_nr = 1;
    p->axis[2].axis_nr = 2;
    p->axis[3].axis_nr = 3;

    timer_add(&p->axis[0].timer, timer_over, &p->axis[0], 0);
    timer_add(&p->axis[1].timer, timer_over, &p->axis[1], 0);
    timer_add(&p->axis[2].timer, timer_over, &p->axis[2], 0);
    timer_add(&p->axis[3].timer, timer_over, &p->axis[3], 0);

    p->joystick = joysticks[joystick_type].joystick;
    p->joystick_dat = p->joystick->init();

    gameport_global = p;

    return(p);
}


void
gameport_update_joystick_type(void)
{
    gameport_t *p = gameport_global;

    if (p != NULL) {
	p->joystick->close(p->joystick_dat);
	p->joystick = joysticks[joystick_type].joystick;
	p->joystick_dat = p->joystick->init();
    }
}


static void *
gameport_init(const device_t *info)
{
    gameport_t *p = NULL;

    if (!joystick_type) {
	p = NULL;
	return(p);
    }

    p = init_common();

    io_sethandler(0x0200, 8,
		  gameport_read,NULL,NULL, gameport_write,NULL,NULL, p);

    return(p);
}


static void *
gameport_201_init(const device_t *info)
{
    gameport_t *p;

    if (!joystick_type) {
	p = NULL;
	return(p);
    }

    p = init_common();

    io_sethandler(0x0201, 1,
		  gameport_read,NULL,NULL, gameport_write,NULL,NULL, p);

    return(p);
}


static void
gameport_close(void *priv)
{
    gameport_t *p = (gameport_t *)priv;

    if (p == NULL) return;

    p->joystick->close(p->joystick_dat);

    gameport_global = NULL;

    free(p);
}


const device_t gameport_device = {
    "Game port",
    0, 0,
    gameport_init,
    gameport_close,
    NULL, { NULL }, NULL,
    NULL
};

const device_t gameport_201_device = {
    "Game port (port 201h only)",
    0, 0,
    gameport_201_init,
    gameport_close,
    NULL, { NULL }, NULL,
    NULL
};
