/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Common driver module for MOUSE devices.
 *
 * Version:	@(#)mouse.c	1.0.15	2017/11/03
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		TheCollector1995,
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "machine/machine.h"
#include "86box.h"
#include "device.h"
#include "mouse.h"
#include "plat_mouse.h"


int	mouse_type = 0;
int	mouse_x,
	mouse_y,
	mouse_z,
	mouse_buttons;


static mouse_t mouse_none = {
    "Disabled", "none",
    MOUSE_TYPE_NONE,
    NULL, NULL, NULL
};

static mouse_t mouse_internal = {
    "Internal", "internal",
    MOUSE_TYPE_INTERNAL,
    NULL, NULL, NULL
};


static mouse_t *mouse_list[] = {
    &mouse_none,
    &mouse_internal,
    &mouse_bus_logitech,	/*  2 Logitech Bus Mouse 2-button */
    &mouse_bus_msinport,	/*  3 Microsoft InPort Mouse */
    &mouse_serial_msystems,	/*  4 Mouse Systems Serial Mouse */
    &mouse_serial_microsoft,	/*  5 Microsoft Serial Mouse */
    &mouse_serial_logitech,	/*  6 Logitech 3-button Serial Mouse */
    &mouse_serial_mswheel,	/*  7 Microsoft Serial Wheel Mouse */
    &mouse_ps2_2button,		/*  8 PS/2 Mouse 2-button */
    &mouse_ps2_intellimouse,	/*  9 PS/2 Intellimouse 3-button */
#if 0
    &mouse_bus_genius,		/* 10 Genius Bus Mouse */
#endif
    NULL
};
static mouse_t	*mouse_curr;
static void	*mouse_priv;


void
mouse_emu_init(void)
{
    /* Initialize local data. */
    mouse_x = mouse_y = mouse_z = 0;
    mouse_buttons = 0x00;

    mouse_curr = mouse_list[mouse_type];

    if (mouse_curr == NULL || mouse_curr->init == NULL) return;

    mouse_priv = mouse_curr->init(mouse_curr);
}


void
mouse_emu_close(void)
{
    if (mouse_curr == NULL || mouse_curr->close == NULL) return;

    mouse_curr->close(mouse_priv);

    mouse_curr = NULL;
    mouse_priv = NULL;
}


void
mouse_process(void)
{
    static int poll_delay = 2;

    if (--poll_delay) return;

    mouse_poll_host();

    if (mouse_curr->poll != NULL)
    	mouse_curr->poll(mouse_x,mouse_y,mouse_z,mouse_buttons, mouse_priv);

    /* Reset mouse deltas. */
    mouse_x = mouse_y = mouse_z = 0;

    poll_delay = 2;
}


void
mouse_setpoll(uint8_t (*func)(int,int,int,int,void *), void *arg)
{
    if (mouse_type != MOUSE_TYPE_INTERNAL) return;

    mouse_curr->poll = func;
    mouse_priv = arg;
}


char *
mouse_get_name(int mouse)
{
    if (!mouse_list[mouse])
	return(NULL);

    return((char *)mouse_list[mouse]->name);
}


char *
mouse_get_internal_name(int mouse)
{
    return((char *)mouse_list[mouse]->internal_name);
}


int
mouse_get_from_internal_name(char *s)
{
    int c = 0;

    while (mouse_list[c] != NULL) {
	if (!strcmp((char *)mouse_list[c]->internal_name, s))
		return(c);
	c++;
    }

    return(0);
}


int
mouse_get_type(int mouse)
{
    return(mouse_list[mouse]->type);
}


/* Return number of MOUSE types we know about. */
int
mouse_get_ndev(void)
{
    return(sizeof(mouse_list)/sizeof(mouse_t *) - 1);
}
