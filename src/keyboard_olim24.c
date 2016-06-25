#include "ibm.h"
#include "io.h"
#include "mem.h"
#include "mouse.h"
#include "pic.h"
#include "sound.h"
#include "sound_speaker.h"
#include "timer.h"

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

void keyboard_olim24_poll()
{
        keybsenddelay += (1000 * TIMER_USEC);
        //pclog("poll %i\n", keyboard_olim24.wantirq);
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

void keyboard_olim24_write(uint16_t port, uint8_t val, void *priv)
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
//                                        dumpregs();
//                                        exit(-1);
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
//                                dumpregs();
//                                exit(-1);
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
                pit_set_gate(2, val & 1);
                break;
        }
}

uint8_t keyboard_olim24_read(uint16_t port, void *priv)
{
        uint8_t temp;
//        pclog("keyboard_olim24 : read %04X ", port);
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
//                dumpregs();
//                exit(-1);
        }
//        pclog("%02X\n", temp);
        return temp;
}

void keyboard_olim24_reset()
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

static int mouse_x = 0, mouse_y = 0, mouse_b = 0;
void mouse_olim24_poll(int x, int y, int b)
{
        mouse_x += x;
        mouse_y += y;

        pclog("mouse_poll - %i, %i  %i, %i\n", x, y, mouse_x, mouse_y);
        
        if (((key_queue_end - key_queue_start) & 0xf) > 14) return;
        if ((b & 1) && !(mouse_b & 1))
           keyboard_olim24_adddata(mouse_scancodes[0]);
        if (!(b & 1) && (mouse_b & 1))
           keyboard_olim24_adddata(mouse_scancodes[0] | 0x80);
        mouse_b = (mouse_b & ~1) | (b & 1);
        
        if (((key_queue_end - key_queue_start) & 0xf) > 14) return;
        if ((b & 2) && !(mouse_b & 2))
           keyboard_olim24_adddata(mouse_scancodes[2]);
        if (!(b & 2) && (mouse_b & 2))
           keyboard_olim24_adddata(mouse_scancodes[2] | 0x80);
        mouse_b = (mouse_b & ~2) | (b & 2);
        
        if (((key_queue_end - key_queue_start) & 0xf) > 14) return;
        if ((b & 4) && !(mouse_b & 4))
           keyboard_olim24_adddata(mouse_scancodes[1]);
        if (!(b & 4) && (mouse_b & 4))
           keyboard_olim24_adddata(mouse_scancodes[1] | 0x80);
        mouse_b = (mouse_b & ~4) | (b & 4);
        
        if (keyboard_olim24.mouse_mode)
        {
                if (((key_queue_end - key_queue_start) & 0xf) > 12) return;
                if (!mouse_x && !mouse_y) return;
                
                mouse_y = -mouse_y;
                
                if (mouse_x < -127) mouse_x = -127;
                if (mouse_x >  127) mouse_x =  127;
                if (mouse_x < -127) mouse_x = 0x80 | ((-mouse_x) & 0x7f);

                if (mouse_y < -127) mouse_y = -127;
                if (mouse_y >  127) mouse_y =  127;
                if (mouse_y < -127) mouse_y = 0x80 | ((-mouse_y) & 0x7f);
                
                keyboard_olim24_adddata(0xfe);
                keyboard_olim24_adddata(mouse_x);
                keyboard_olim24_adddata(mouse_y);
                
                mouse_x = mouse_y = 0;
        }
        else
        {   
                while (mouse_x < -4)
                {
                        if (((key_queue_end - key_queue_start) & 0xf) > 14) return;
                        mouse_x+=4;
                        keyboard_olim24_adddata(mouse_scancodes[3]);
                }
                while (mouse_x > 4)
                {
                        if (((key_queue_end - key_queue_start) & 0xf) > 14) return;
                        mouse_x-=4;
                        keyboard_olim24_adddata(mouse_scancodes[4]);
                }
                while (mouse_y < -4)
                {
                        if (((key_queue_end - key_queue_start) & 0xf) > 14) return;
                        mouse_y+=4;
                        keyboard_olim24_adddata(mouse_scancodes[5]);
                }
                while (mouse_y > 4)
                {
                        if (((key_queue_end - key_queue_start) & 0xf) > 14) return;
                        mouse_y-=4;
                        keyboard_olim24_adddata(mouse_scancodes[6]);
                }
        }
}

void keyboard_olim24_init()
{
        //return;
        io_sethandler(0x0060, 0x0002, keyboard_olim24_read, NULL, NULL, keyboard_olim24_write, NULL, NULL,  NULL);
        io_sethandler(0x0064, 0x0001, keyboard_olim24_read, NULL, NULL, keyboard_olim24_write, NULL, NULL,  NULL);
        keyboard_olim24_reset();
        keyboard_send = keyboard_olim24_adddata;
        keyboard_poll = keyboard_olim24_poll;
        mouse_poll    = mouse_olim24_poll;
        
        timer_add(keyboard_olim24_poll, &keybsenddelay, TIMER_ALWAYS_ENABLED,  NULL);
}
