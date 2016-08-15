/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include "ibm.h"
#include "keyboard_at.h"
#include "mouse.h"
#include "mouse_ps2.h"
#include "plat-mouse.h"

int mouse_scan = 0;

enum
{
        MOUSE_STREAM,
        MOUSE_REMOTE,
        MOUSE_ECHO
};

#define MOUSE_ENABLE 0x20
#define MOUSE_SCALE  0x10

static struct
{
        int mode;
        
        uint8_t flags;
        uint8_t resolution;
        uint8_t sample_rate;
	uint8_t type;
        
        uint8_t command;
        
        int cd;
} mouse_ps2;

void mouse_ps2_write(uint8_t val)
{
	// pclog("PS/2 Mouse: Write %02X\n", val);

        if (mouse_ps2.cd)
        {
                mouse_ps2.cd = 0;
                switch (mouse_ps2.command)
                {
                        case 0xe8: /*Set mouse resolution*/
                        mouse_ps2.resolution = val;
                        keyboard_at_adddata_mouse(0xfa);
                        break;
                        
                        case 0xf3: /*Set sample rate*/
                        mouse_ps2.sample_rate = val;
                        keyboard_at_adddata_mouse(0xfa);
                        break;
                        
//                        default:
//                        fatal("mouse_ps2 : Bad data write %02X for command %02X\n", val, mouse_ps2.command);
                }
        }
        else
        {
                uint8_t temp;
                
                mouse_ps2.command = val;
                switch (mouse_ps2.command)
                {
                        case 0xe6: /*Set scaling to 1:1*/
                        mouse_ps2.flags &= ~MOUSE_SCALE;
                        keyboard_at_adddata_mouse(0xfa);
                        break;

                        case 0xe7: /*Set scaling to 2:1*/
                        mouse_ps2.flags |= MOUSE_SCALE;
                        keyboard_at_adddata_mouse(0xfa);
                        break;
                        
                        case 0xe8: /*Set mouse resolution*/
                        mouse_ps2.cd = 1;
                        keyboard_at_adddata_mouse(0xfa);
                        break;
                        
                        case 0xe9: /*Status request*/
                        keyboard_at_adddata_mouse(0xfa);
                        temp = mouse_ps2.flags;
                        if (mouse_buttons & 1)
                                temp |= 1;
                        if (mouse_buttons & 2)
                                temp |= 2;
                        if (mouse_buttons & 4)
                                temp |= 3;
                        keyboard_at_adddata_mouse(temp);
                        keyboard_at_adddata_mouse(mouse_ps2.resolution);
                        keyboard_at_adddata_mouse(mouse_ps2.sample_rate);
                        break;
                        
                        case 0xf2: /*Read ID*/
                        keyboard_at_adddata_mouse(0xfa);
                        keyboard_at_adddata_mouse(0x00);
                        break;
                        
                        case 0xf3: /*Set sample rate*/
                        mouse_ps2.cd = 1;
                        keyboard_at_adddata_mouse(0xfa);
                        break;

                        case 0xf4: /*Enable*/
                        mouse_ps2.flags |= MOUSE_ENABLE;
                        keyboard_at_adddata_mouse(0xfa);
                        break;
                                                
                        case 0xf5: /*Disable*/
                        mouse_ps2.flags &= ~MOUSE_ENABLE;
                        keyboard_at_adddata_mouse(0xfa);
                        break;
                        
                        case 0xff: /*Reset*/
                        mouse_ps2.mode  = MOUSE_STREAM;
                        mouse_ps2.flags = 0;
                        keyboard_at_adddata_mouse(0xfa);
                        keyboard_at_adddata_mouse(0xaa);
                        keyboard_at_adddata_mouse(0x00);
                        break;
                        
//                        default:
//                        fatal("mouse_ps2 : Bad command %02X\n", val, mouse_ps2.command);
                }
        }
}

static int ps2_x = 0, ps2_y = 0, ps2_b = 0;
int first_time = 1;
void mouse_ps2_poll(int x, int y, int b)
{
        uint8_t packet[3] = {0x08, 0, 0};
        if (!x && !y && b == ps2_b && !first_time){
		// pclog("PS/2 Mouse: X is 0, Y is 0, and B is PS2_B\n");
		return;        
	}

	if (first_time)  first_time = 0;

        if (!mouse_scan)
	{
		// pclog("PS/2 Mouse: Not scanning\n");
                return;
	}
        ps2_x += x;
        ps2_y -= y;        
        if (mouse_ps2.mode == MOUSE_STREAM && (mouse_ps2.flags & MOUSE_ENABLE) &&
            ((mouse_queue_end - mouse_queue_start) & 0xf) < 13)
        {
                ps2_b  = b;
               // pclog("Send packet : %i %i\n", ps2_x, ps2_y);
                if (ps2_x > 255)
                   ps2_x = 255;
                if (ps2_x < -256)
                   ps2_x = -256;
                if (ps2_y > 255)
                   ps2_y = 255;
                if (ps2_y < -256)
                   ps2_y = -256;
                if (ps2_x < 0)
                   packet[0] |= 0x10;
                if (ps2_y < 0)
                   packet[0] |= 0x20;
                if (mouse_buttons & 1)
                   packet[0] |= 1;
                if (mouse_buttons & 2)
                   packet[0] |= 2;
                if (mouse_buttons & 4)
                   packet[0] |= 4;
                packet[1] = ps2_x & 0xff;
                packet[2] = ps2_y & 0xff;
                
                ps2_x = ps2_y = 0;
                
                keyboard_at_adddata_mouse(packet[0]);
                keyboard_at_adddata_mouse(packet[1]);
                keyboard_at_adddata_mouse(packet[2]);
        }

	// pclog("PS/2 Mouse: Poll\n");
}


void mouse_ps2_init()
{
        mouse_poll  = mouse_ps2_poll;
        mouse_write = mouse_ps2_write;
        mouse_ps2.cd = 0;
        mouse_ps2.flags = 0;
        mouse_ps2.mode = MOUSE_STREAM;
}
