#include <stdlib.h>
#include "ibm.h"
#include "io.h"
#include "mem.h"
#include "pic.h"
#include "pit.h"
#include "timer.h"
#include "mouse.h"
#include "SOUND/sound.h"
#include "SOUND/snd_speaker.h"
#include "keyboard.h"
#include "keyboard_olim24.h"


#define STAT_PARITY     0x80
#define STAT_RTIMEOUT   0x40
#define STAT_TTIMEOUT   0x20
#define STAT_LOCK       0x10
#define STAT_CD         0x08
#define STAT_SYSFLAG    0x04
#define STAT_IFULL      0x02
#define STAT_OFULL      0x01


struct
{
        int wantirq;
        uint8_t command;
        uint8_t status;
        uint8_t out;
        
        uint8_t output_port;

        int param, param_total;
        uint8_t params[16];
        
        int mouse_mode;
} keyboard_olim24;

static uint8_t key_queue[16];
static int key_queue_start = 0, key_queue_end = 0;

static uint8_t mouse_scancodes[7];


static void keyboard_olim24_poll(void)
{
        keybsenddelay += (1000 * TIMER_USEC);
        if (keyboard_olim24.wantirq)
        {
                keyboard_olim24.wantirq = 0;
                picint(2);
                pclog("keyboard_olim24 : take IRQ\n");
        }
        if (!(keyboard_olim24.status & STAT_OFULL) && key_queue_start != key_queue_end)
        {
                pclog("Reading %02X from the key queue at %i\n", keyboard_olim24.out, key_queue_start);
                keyboard_olim24.out = key_queue[key_queue_start];
                key_queue_start = (key_queue_start + 1) & 0xf;
                keyboard_olim24.status |=  STAT_OFULL;
                keyboard_olim24.status &= ~STAT_IFULL;
                keyboard_olim24.wantirq = 1;        
        }                
}


void keyboard_olim24_adddata(uint8_t val)
{
        key_queue[key_queue_end] = val;
        key_queue_end = (key_queue_end + 1) & 0xf;
        pclog("keyboard_olim24 : %02X added to key queue %02X\n", val, keyboard_olim24.status);
        return;
}


static void keyboard_olim24_write(uint16_t port, uint8_t val, void *priv)
{
        pclog("keyboard_olim24 : write %04X %02X\n", port, val);
/*        if (ram[8] == 0xc3) 
        {
                output = 3;
        }*/
        switch (port)
        {
                case 0x60:
                if (keyboard_olim24.param != keyboard_olim24.param_total)
                {
                        keyboard_olim24.params[keyboard_olim24.param++] = val;
                        if (keyboard_olim24.param == keyboard_olim24.param_total)
                        {
                                switch (keyboard_olim24.command)
                                {
                                        case 0x11:
                                        keyboard_olim24.mouse_mode = 0;
                                        mouse_scancodes[0] = keyboard_olim24.params[0];
                                        mouse_scancodes[1] = keyboard_olim24.params[1];
                                        mouse_scancodes[2] = keyboard_olim24.params[2];
                                        mouse_scancodes[3] = keyboard_olim24.params[3];
                                        mouse_scancodes[4] = keyboard_olim24.params[4];
                                        mouse_scancodes[5] = keyboard_olim24.params[5];
                                        mouse_scancodes[6] = keyboard_olim24.params[6];
                                        break;

                                        case 0x12:
                                        keyboard_olim24.mouse_mode = 1;
                                        mouse_scancodes[0] = keyboard_olim24.params[0];
                                        mouse_scancodes[1] = keyboard_olim24.params[1];
                                        mouse_scancodes[2] = keyboard_olim24.params[2];
                                        break;
                                        
                                        default:
                                        pclog("Bad keyboard command complete %02X\n", keyboard_olim24.command);
                                }
                        }
                }
                else
                {
                        keyboard_olim24.command = val;
                        switch (val)
                        {
                                case 0x01: /*Self-test*/
                                break;
                                
                                case 0x05: /*Read ID*/
                                keyboard_olim24_adddata(0x00);
                                break;
                                
                                case 0x11:
                                keyboard_olim24.param = 0;
                                keyboard_olim24.param_total = 9;
                                break;
                                
                                case 0x12:
                                keyboard_olim24.param = 0;
                                keyboard_olim24.param_total = 4;
                                break;
                                
                                default:
                                pclog("Bad keyboard command %02X\n", val);
                        }
                }
                        
                break;
                
                case 0x61:
                ppi.pb = val;
                
                timer_process();
                timer_update_outstanding();

                speaker_update();
                speaker_gated = val & 1;
                speaker_enable = val & 2;
                if (speaker_enable) 
                        was_speaker_enable = 1;
                pit_set_gate(&pit, 2, val & 1);
                break;
        }
}


static uint8_t keyboard_olim24_read(uint16_t port, void *priv)
{
        uint8_t temp = 0xff;
        switch (port)
        {
                case 0x60:
                temp = keyboard_olim24.out;
                if (key_queue_start == key_queue_end)
                {
                        keyboard_olim24.status &= ~STAT_OFULL;
                        keyboard_olim24.wantirq = 0;
                }
                else
                {
                        keyboard_olim24.out = key_queue[key_queue_start];
                        key_queue_start = (key_queue_start + 1) & 0xf;
                        keyboard_olim24.status |=  STAT_OFULL;
                        keyboard_olim24.status &= ~STAT_IFULL;
                        keyboard_olim24.wantirq = 1;        
                }        
                break;
                
                case 0x61:
                return ppi.pb;
                
                case 0x64:
                temp = keyboard_olim24.status;
                keyboard_olim24.status &= ~(STAT_RTIMEOUT | STAT_TTIMEOUT);
                break;
                
                default:
                pclog("\nBad olim24 keyboard read %04X\n", port);
        }
        return temp;
}


void keyboard_olim24_reset(void)
{
        keyboard_olim24.status = STAT_LOCK | STAT_CD;
        keyboard_olim24.wantirq = 0;
        
        keyboard_scan = 1;
        
        keyboard_olim24.param = keyboard_olim24.param_total = 0;
        
        keyboard_olim24.mouse_mode = 0;
        mouse_scancodes[0] = 0x1c;
        mouse_scancodes[1] = 0x53;
        mouse_scancodes[2] = 0x01;
        mouse_scancodes[3] = 0x4b;
        mouse_scancodes[4] = 0x4d;
        mouse_scancodes[5] = 0x48;
        mouse_scancodes[6] = 0x50;
}

typedef struct mouse_olim24_t
{
        int x, y, b;
} mouse_olim24_t;

uint8_t mouse_olim24_poll(int x, int y, int z, int b, void *p)
{
        mouse_olim24_t *mouse = (mouse_olim24_t *)p;
        
        mouse->x += x;
        mouse->y += y;

        if (((key_queue_end - key_queue_start) & 0xf) > 14)
                return(0xff);
        if ((b & 1) && !(mouse->b & 1))
                keyboard_olim24_adddata(mouse_scancodes[0]);
        if (!(b & 1) && (mouse->b & 1))
                keyboard_olim24_adddata(mouse_scancodes[0] | 0x80);
        mouse->b = (mouse->b & ~1) | (b & 1);
        
        if (((key_queue_end - key_queue_start) & 0xf) > 14)
                return(0xff);
        if ((b & 2) && !(mouse->b & 2))
                keyboard_olim24_adddata(mouse_scancodes[2]);
        if (!(b & 2) && (mouse->b & 2))
                keyboard_olim24_adddata(mouse_scancodes[2] | 0x80);
        mouse->b = (mouse->b & ~2) | (b & 2);
        
        if (((key_queue_end - key_queue_start) & 0xf) > 14)
                return(0xff);
        if ((b & 4) && !(mouse->b & 4))
                keyboard_olim24_adddata(mouse_scancodes[1]);
        if (!(b & 4) && (mouse->b & 4))
                keyboard_olim24_adddata(mouse_scancodes[1] | 0x80);
        mouse->b = (mouse->b & ~4) | (b & 4);
        
        if (keyboard_olim24.mouse_mode)
        {
                if (((key_queue_end - key_queue_start) & 0xf) > 12)
                        return(0xff);
                if (!mouse->x && !mouse->y)
                        return(0xff);
                
                mouse->y = -mouse->y;
                
                if (mouse->x < -127) mouse->x = -127;
                if (mouse->x >  127) mouse->x =  127;
                if (mouse->x < -127) mouse->x = 0x80 | ((-mouse->x) & 0x7f);

                if (mouse->y < -127) mouse->y = -127;
                if (mouse->y >  127) mouse->y =  127;
                if (mouse->y < -127) mouse->y = 0x80 | ((-mouse->y) & 0x7f);
                
                keyboard_olim24_adddata(0xfe);
                keyboard_olim24_adddata(mouse->x);
                keyboard_olim24_adddata(mouse->y);
                
                mouse->x = mouse->y = 0;
        }
        else
        {   
                while (mouse->x < -4)
                {
                        if (((key_queue_end - key_queue_start) & 0xf) > 14)
                                return(0xff);
                        mouse->x += 4;
                        keyboard_olim24_adddata(mouse_scancodes[3]);
                }
                while (mouse->x > 4)
                {
                        if (((key_queue_end - key_queue_start) & 0xf) > 14)
                                return(0xff);
                        mouse->x -= 4;
                        keyboard_olim24_adddata(mouse_scancodes[4]);
                }
                while (mouse->y < -4)
                {
                        if (((key_queue_end - key_queue_start) & 0xf) > 14)
                                return(0xff);
                        mouse->y += 4;
                        keyboard_olim24_adddata(mouse_scancodes[5]);
                }
                while (mouse->y > 4)
                {
                        if (((key_queue_end - key_queue_start) & 0xf) > 14)
                                return(0xff);
                        mouse->y -= 4;
                        keyboard_olim24_adddata(mouse_scancodes[6]);
                }
        }

	return(0);
}


static void *mouse_olim24_init(void)
{
        mouse_olim24_t *mouse = (mouse_olim24_t *)malloc(sizeof(mouse_olim24_t));
        memset(mouse, 0, sizeof(mouse_olim24_t));
                
        return mouse;
}


static void mouse_olim24_close(void *p)
{
        mouse_olim24_t *mouse = (mouse_olim24_t *)p;
        
        free(mouse);
}


mouse_t mouse_olim24 =
{
        "Olivetti M24 mouse",
        "olim24",
        MOUSE_TYPE_OLIM24,
        mouse_olim24_init,
        mouse_olim24_close,
        mouse_olim24_poll
};

void keyboard_olim24_init(void)
{
        io_sethandler(0x0060, 0x0002, keyboard_olim24_read, NULL, NULL, keyboard_olim24_write, NULL, NULL,  NULL);
        io_sethandler(0x0064, 0x0001, keyboard_olim24_read, NULL, NULL, keyboard_olim24_write, NULL, NULL,  NULL);
        keyboard_olim24_reset();
        keyboard_send = keyboard_olim24_adddata;
        keyboard_poll = keyboard_olim24_poll;
        
        timer_add((void(*)(void *))keyboard_olim24_poll, &keybsenddelay, TIMER_ALWAYS_ENABLED,  NULL);
}
