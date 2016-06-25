#include "allegro-main.h"
#include "plat-mouse.h"

int mouse_buttons;

void mouse_init()
{
        install_mouse();
}

void mouse_close()
{
}

void mouse_poll_host()
{
        //poll_mouse();
	mouse_buttons = mouse_b;
}

void mouse_get_mickeys(int *x, int *y)
{
	if (mousecapture)
	{
	        get_mouse_mickeys(x, y);
//        	position_mouse(64, 64);
	}
	else
	        *x = *y = 0;
}

