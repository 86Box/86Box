#include "ibm.h"
#include "cpu/cpu.h"
#include "device.h"
#include "mouse.h"
#include "mouse_serial.h"
#include "mouse_ps2.h"
#include "mouse_bus.h"
#include "model.h"
//#include "keyboard_olim24.h"


static mouse_t *mouse_list[] = {
    &mouse_serial_microsoft,	/* 0 Microsoft Serial Mouse */
    &mouse_ps2_2_button,	/* 1 PS/2 Mouse 2-button */
    &mouse_intellimouse,	/* 2 PS/2 Intellimouse 3-button */
    &mouse_bus,			/* 3 Logitech Bus Mouse 2-button */
    &mouse_amstrad,		/* 4 Amstrad PC System Mouse */
    &mouse_olim24,		/* 5 Olivetti M24 System Mouse */
    &mouse_msystems,		/* 6 Mouse Systems */
    &mouse_serial_logitech,	/* 0 Logitech 3-button Serial Mouse */
#if 0
    &mouse_genius,		/* 7 Genius Bus Mouse */
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
    mouse_p = cur_mouse->init();
}


void
mouse_emu_close(void)
{
    if (cur_mouse)
	cur_mouse->close(mouse_p);
    cur_mouse = NULL;
}


void
mouse_poll(int x, int y, int z, int b)
{
    if (cur_mouse)
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
