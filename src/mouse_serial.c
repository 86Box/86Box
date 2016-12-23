#include "ibm.h"
#include "mouse.h"
#include "pic.h"
#include "serial.h"
#include "timer.h"

typedef struct mouse_serial_t
{
        int mousepos, mousedelay;
        int oldb;
        SERIAL *serial;
} mouse_serial_t;

void mouse_serial_poll(int x, int y, int z, int b, void *p)
{
        mouse_serial_t *mouse = (mouse_serial_t *)p;
        SERIAL *serial = mouse->serial;        
        uint8_t mousedat[3];

        if (!(serial->ier & 1))
                return;
        if (!x && !y && b == mouse->oldb)
                return;

        mouse->oldb = b;
        if (x>127) x=127;
        if (y>127) y=127;
        if (x<-128) x=-128;
        if (y<-128) y=-128;

        /*Use Microsoft format*/
        mousedat[0]=0x40;
        mousedat[0]|=(((y>>6)&3)<<2);
        mousedat[0]|=((x>>6)&3);
        if (b&1) mousedat[0]|=0x20;
        if (b&2) mousedat[0]|=0x10;
        mousedat[1]=x&0x3F;
        mousedat[2]=y&0x3F;
        
        if (!(serial->mctrl & 0x10))
        {
//                pclog("Serial data %02X %02X %02X\n", mousedat[0], mousedat[1], mousedat[2]);
                serial_write_fifo(mouse->serial, mousedat[0]);
                serial_write_fifo(mouse->serial, mousedat[1]);
                serial_write_fifo(mouse->serial, mousedat[2]);
        }
}

void mouse_serial_rcr(SERIAL *serial, void *p)
{
        mouse_serial_t *mouse = (mouse_serial_t *)p;
        
        mouse->mousepos = -1;
        mouse->mousedelay = 5000 * (1 << TIMER_SHIFT);
}
        
void mousecallback(void *p)
{
        mouse_serial_t *mouse = (mouse_serial_t *)p;

	mouse->mousedelay = 0;
        if (mouse->mousepos == -1)
        {
                mouse->mousepos = 0;
                serial_write_fifo(mouse->serial, 'M');
        }
}

void *mouse_serial_init()
{
        mouse_serial_t *mouse = (mouse_t *)malloc(sizeof(mouse_serial_t));
        memset(mouse, 0, sizeof(mouse_serial_t));

        mouse->serial = &serial1;
        serial1.rcr_callback = mouse_serial_rcr;
        serial1.rcr_callback_p = mouse;
        timer_add(mousecallback, &mouse->mousedelay, &mouse->mousedelay, mouse);
        
        return mouse;
}

void mouse_serial_close(void *p)
{
        mouse_serial_t *mouse = (mouse_serial_t *)p;
        
        free(mouse);
        
        serial1.rcr_callback = NULL;
}

mouse_t mouse_serial_microsoft =
{
        "Microsoft 2-button mouse (serial)",
        mouse_serial_init,
        mouse_serial_close,
        mouse_serial_poll,
        MOUSE_TYPE_SERIAL
};
