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
 * Version:	@(#)mouse.c	1.0.6	2017/08/23
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		TheCollector1995,
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */
#include "ibm.h"
#include "cpu/cpu.h"
#include "device.h"
#include "mouse.h"
#include "model.h"


static mouse_t mouse_none = {
    "Disabled", "none",
    MOUSE_TYPE_NONE,
    NULL, NULL, NULL
};


static mouse_t *mouse_list[] = {
    &mouse_none,
    &mouse_bus_logitech,	/*  1 Logitech Bus Mouse 2-button */
    &mouse_bus_msinport,	/*  2 Microsoft InPort Mouse */
    &mouse_serial_msystems,	/*  3 Mouse Systems Serial Mouse */
    &mouse_serial_microsoft,	/*  4 Microsoft Serial Mouse */
    &mouse_serial_logitech,	/*  5 Logitech 3-button Serial Mouse */
    &mouse_serial_mswheel,	/*  6 Microsoft Serial Wheel Mouse */
    &mouse_ps2_2button,		/*  7 PS/2 Mouse 2-button */
    &mouse_ps2_intellimouse,	/*  8 PS/2 Intellimouse 3-button */
    &mouse_amstrad,		/*  9 Amstrad PC System Mouse */
    &mouse_olim24,		/* 10 Olivetti M24 System Mouse */
#if 0
    &mouse_bus_genius,		/* 11 Genius Bus Mouse */
#endif
    NULL
};

static mouse_t *cur_mouse;
static void *mouse_p;
int mouse_type = 0;


void
mouse_emu_init(void)
{
    cur_mouse = mouse_list[mouse_type];

    if (cur_mouse == NULL || cur_mouse->init == NULL) return;

    mouse_p = cur_mouse->init();
}


void
mouse_emu_close(void)
{
    if (cur_mouse == NULL || cur_mouse->close == NULL) return;

    cur_mouse->close(mouse_p);

    cur_mouse = NULL;
    mouse_p = NULL;
}


void
mouse_poll(int x, int y, int z, int b)
{
    if (cur_mouse == NULL || cur_mouse->init == NULL) return;

    cur_mouse->poll(x, y, z, b, mouse_p);
}


char *
mouse_get_name(int mouse)
{
    if (!mouse_list[mouse])
	return(NULL);

    return(mouse_list[mouse]->name);
}


char *
mouse_get_internal_name(int mouse)
{
    return(mouse_list[mouse]->internal_name);
}


int
mouse_get_from_internal_name(char *s)
{
    int c = 0;

    while (mouse_list[c] != NULL) {
	if (!strcmp(mouse_list[c]->internal_name, s))
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
