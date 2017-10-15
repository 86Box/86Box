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
#include "pic.h"
#include "pit.h"
#include "timer.h"
#include "sound/sound.h"
#include "sound/snd_speaker.h"
#include "keyboard.h"
#include "keyboard_amstrad.h"


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
        
        uint8_t key_waiting;
        uint8_t pa;        
        uint8_t pb;
} keyboard_amstrad;

static uint8_t key_queue[16];
static int key_queue_start = 0, key_queue_end = 0;

static uint8_t amstrad_systemstat_1, amstrad_systemstat_2;

void keyboard_amstrad_poll(void)
{
        keybsenddelay += (1000 * TIMER_USEC);
        if (keyboard_amstrad.wantirq)
        {
                keyboard_amstrad.wantirq = 0;
                keyboard_amstrad.pa = keyboard_amstrad.key_waiting;
                picint(2);
#if ENABLE_KEYBOARD_LOG
                pclog("keyboard_amstrad : take IRQ\n");
#endif
        }
        if (key_queue_start != key_queue_end && !keyboard_amstrad.pa)
        {
                keyboard_amstrad.key_waiting = key_queue[key_queue_start];
#if ENABLE_KEYBOARD_LOG
                pclog("Reading %02X from the key queue at %i\n", keyboard_amstrad.key_waiting, key_queue_start);
#endif
                key_queue_start = (key_queue_start + 1) & 0xf;
                keyboard_amstrad.wantirq = 1;        
        }                
}

void keyboard_amstrad_adddata(uint8_t val)
{
        key_queue[key_queue_end] = val;
#if ENABLE_KEYBOARD_LOG
        pclog("keyboard_amstrad : %02X added to key queue at %i\n", val, key_queue_end);
#endif
        key_queue_end = (key_queue_end + 1) & 0xf;
        return;
}

void keyboard_amstrad_write(uint16_t port, uint8_t val, void *priv)
{
#if ENABLE_KEYBOARD_LOG
        pclog("keyboard_amstrad : write %04X %02X %02X\n", port, val, keyboard_amstrad.pb);
#endif

        switch (port)
        {
                case 0x61:
#if ENABLE_KEYBOARD_LOG
                pclog("keyboard_amstrad : pb write %02X %02X  %i %02X %i\n", val, keyboard_amstrad.pb, !(keyboard_amstrad.pb & 0x40), keyboard_amstrad.pb & 0x40, (val & 0x40));
#endif
                if (!(keyboard_amstrad.pb & 0x40) && (val & 0x40)) /*Reset keyboard*/
                {
#if ENABLE_KEYBOARD_LOG
                        pclog("keyboard_amstrad : reset keyboard\n");
#endif
                        keyboard_amstrad_adddata(0xaa);
                }
                keyboard_amstrad.pb = val;
                ppi.pb = val;

                timer_process();
                timer_update_outstanding();

		speaker_update();
                speaker_gated = val & 1;
                speaker_enable = val & 2;
                if (speaker_enable) 
                        was_speaker_enable = 1;
                pit_set_gate(&pit, 2, val & 1);
                
                if (val & 0x80)
                        keyboard_amstrad.pa = 0;
                break;
                
                case 0x63:
                break;
                
                case 0x64:
                amstrad_systemstat_1 = val;
                break;
                
                case 0x65:
                amstrad_systemstat_2 = val;
                break;

                default:
                pclog("\nBad XT keyboard write %04X %02X\n", port, val);
        }
}

uint8_t keyboard_amstrad_read(uint16_t port, void *priv)
{
        uint8_t temp = 0xff;
        switch (port)
        {
                case 0x60:
                if (keyboard_amstrad.pb & 0x80)
                {
                        temp = (amstrad_systemstat_1 | 0xd) & 0x7f;
                }
                else
                {
                        temp = keyboard_amstrad.pa;
                        if (key_queue_start == key_queue_end)
                        {
                                keyboard_amstrad.wantirq = 0;
                        }
                        else
                        {
                                keyboard_amstrad.key_waiting = key_queue[key_queue_start];
                                key_queue_start = (key_queue_start + 1) & 0xf;
                                keyboard_amstrad.wantirq = 1;        
                        }
                }        
                break;
                
                case 0x61:
                temp = keyboard_amstrad.pb;
                break;
                
                case 0x62:
                if (keyboard_amstrad.pb & 0x04)
                   temp = amstrad_systemstat_2 & 0xf;
                else
                   temp = amstrad_systemstat_2 >> 4;
                temp |= (ppispeakon ? 0x20 : 0);
                if (nmi)
                        temp |= 0x40;
                break;
                
                default:
                pclog("\nBad XT keyboard read %04X\n", port);
        }
        return temp;
}

void keyboard_amstrad_reset(void)
{
        keyboard_amstrad.wantirq = 0;
        
        keyboard_scan = 1;
}

void keyboard_amstrad_init(void)
{
#if ENABLE_KEYBOARD_LOG
        pclog("keyboard_amstrad_init\n");
#endif
        io_sethandler(0x0060, 0x0006, keyboard_amstrad_read, NULL, NULL, keyboard_amstrad_write, NULL, NULL,  NULL);
        keyboard_amstrad_reset();
        keyboard_send = keyboard_amstrad_adddata;
        keyboard_poll = keyboard_amstrad_poll;

        timer_add((void (*)(void *))keyboard_amstrad_poll, &keybsenddelay, TIMER_ALWAYS_ENABLED,  NULL);
}
