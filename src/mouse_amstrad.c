/* Copyright holders: SA1988, Curt Coder
   see COPYING for more details
*/
#include "ibm.h"
#include "io.h"
#include "mouse.h"
#include "mouse_amstrad.h"

static int amstrad_x = 0, amstrad_y = 0, amstrad_b = 0;

uint8_t mouse_amstrad_read(uint16_t port, void *priv)
{
	switch (port)
	{
		case 0x78:
		return amstrad_x;
		
		case 0x7a:
		return amstrad_y;
	}
	return 0xff;
}

void mouse_amstrad_write(uint16_t port, uint8_t val, void *priv)
{
	switch (port)
	{
		case 0x78:
		amstrad_x = 0;
		break;
		
		case 0x7a:
		amstrad_y = 0;
		break;
	}
}

void mouse_amstrad_poll(int x, int y, int b)
{
        if (!x && !y && b==amstrad_b) return;

        amstrad_b=b;
		if (x > amstrad_x)
			amstrad_x+=3;
		else
			amstrad_x-=3;
		
		amstrad_x = x;
		
		if (y > amstrad_y)
			amstrad_y+=3;
		else
			amstrad_y-=3;		
		
        amstrad_y = y;
}

void mouse_amstrad_init()
{
        mouse_poll = mouse_amstrad_poll;
		io_sethandler(0x0078, 0x0001, mouse_amstrad_read, NULL, NULL, mouse_amstrad_write, NULL, NULL, NULL);
		io_sethandler(0x007a, 0x0001, mouse_amstrad_read, NULL, NULL, mouse_amstrad_write, NULL, NULL, NULL);
}
