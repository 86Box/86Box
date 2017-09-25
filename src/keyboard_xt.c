/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "ibm.h"
#include "io.h"
#include "mem.h"
#include "rom.h"
#include "pic.h"
#include "pit.h"
#include "timer.h"
#include "device.h"
#include "tandy_eeprom.h"
#include "sound/sound.h"
#include "sound/snd_speaker.h"
#include "keyboard.h"
#include "keyboard_xt.h"


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
        int blocked;
        
        uint8_t pa;        
        uint8_t pb;
        
        int tandy;
} keyboard_xt;

static uint8_t key_queue[16];
static int key_queue_start = 0, key_queue_end = 0;


static void keyboard_xt_poll(void)
{
        keybsenddelay += (1000 * TIMER_USEC);
        if (key_queue_start != key_queue_end && !keyboard_xt.blocked)
        {
                keyboard_xt.pa = key_queue[key_queue_start];
		picint(2);
                pclog("Reading %02X from the key queue at %i\n", keyboard_xt.pa, key_queue_start);
                key_queue_start = (key_queue_start + 1) & 0xf;
                keyboard_xt.blocked = 1;
        }                
}

void keyboard_xt_adddata(uint8_t val)
{
        key_queue[key_queue_end] = val;
        pclog("keyboard_xt : %02X added to key queue at %i\n", val, key_queue_end);
        key_queue_end = (key_queue_end + 1) & 0xf;
        return;
}

static void keyboard_xt_write(uint16_t port, uint8_t val, void *priv)
{
        switch (port)
        {
                case 0x61:
                if (!(keyboard_xt.pb & 0x40) && (val & 0x40)) /*Reset keyboard*/
                {
                        pclog("keyboard_xt : reset keyboard\n");
			key_queue_end = key_queue_start;
                        keyboard_xt_adddata(0xaa);
                }
		if ((keyboard_xt.pb & 0x80)==0 && (val & 0x80)!=0)
		{
			keyboard_xt.pa = 0;
			keyboard_xt.blocked = 0;
			picintc(2);
		}
                keyboard_xt.pb = val;
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

static uint8_t keyboard_xt_read(uint16_t port, void *priv)
{
        uint8_t temp;
        switch (port)
        {
                case 0x60:
		if ((romset == ROM_IBMPC) && (keyboard_xt.pb & 0x80))
		{
			if (VGA || gfxcard == GFX_EGA)
				temp = 0x4D;
			else if (MDA)
				temp = 0x7D;
			else
				temp = 0x6D;
		}
		else
			temp = keyboard_xt.pa;
                break;
                
                case 0x61:
                temp = keyboard_xt.pb;
                break;
                
                case 0x62:
                if (romset == ROM_IBMPC)
                {
                        if (keyboard_xt.pb & 0x04)
                                temp = ((mem_size-64) / 32) & 0xf;
                        else
                                temp = ((mem_size-64) / 32) >> 4;
                }
                else
                {
                        if (keyboard_xt.pb & 0x08)
                        {
                                if (VGA || gfxcard == GFX_EGA)
                                        temp = 4;
                                else if (MDA)
                                        temp = 7;
                                else
                                        temp = 6;
                        }
                        else
                                temp = 0xD;
                }
                temp |= (ppispeakon ? 0x20 : 0);
                if (keyboard_xt.tandy)
                        temp |= (tandy_eeprom_read() ? 0x10 : 0);
                break;
                
                default:
                pclog("\nBad XT keyboard read %04X\n", port);
		temp = 0xff;
        }
        return temp;
}

void keyboard_xt_reset(void)
{
        keyboard_xt.blocked = 0;
        
        keyboard_scan = 1;
}

void keyboard_xt_init(void)
{
        io_sethandler(0x0060, 0x0004, keyboard_xt_read, NULL, NULL, keyboard_xt_write, NULL, NULL,  NULL);
        keyboard_xt_reset();
        keyboard_send = keyboard_xt_adddata;
        keyboard_poll = keyboard_xt_poll;
        keyboard_xt.tandy = 0;

        timer_add((void (*)(void *))keyboard_xt_poll, &keybsenddelay, TIMER_ALWAYS_ENABLED,  NULL);
}

void keyboard_tandy_init(void)
{
        io_sethandler(0x0060, 0x0004, keyboard_xt_read, NULL, NULL, keyboard_xt_write, NULL, NULL,  NULL);
        keyboard_xt_reset();
        keyboard_send = keyboard_xt_adddata;
        keyboard_poll = keyboard_xt_poll;
        keyboard_xt.tandy = (romset != ROM_TANDY) ? 1 : 0;
        
        timer_add((void (*)(void *))keyboard_xt_poll, &keybsenddelay, TIMER_ALWAYS_ENABLED,  NULL);
}
