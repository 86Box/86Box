#include "ibm.h"
#include "device.h"
#include "io.h"
#include "mem.h"
#include "nmi.h"
#include "pic.h"
#include "sound.h"
#include "sound_sn76489.h"
#include "sound_speaker.h"
#include "timer.h"

#include "keyboard.h"
#include "keyboard_pcjr.h"

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
        int latched;
        int data;
        
        int serial_data[44];
        int serial_pos;

        uint8_t pa;
        uint8_t pb;
} keyboard_pcjr;

static uint8_t key_queue[16];
static int key_queue_start = 0, key_queue_end = 0;

void keyboard_pcjr_poll()
{
        keybsenddelay += (220 * TIMER_USEC);


        if (key_queue_start != key_queue_end && !keyboard_pcjr.serial_pos && !keyboard_pcjr.latched)
        {
                int c;
                int p = 0;
                uint8_t key = key_queue[key_queue_start];
                
//                pclog("Reading %02X from the key queue at %i\n", key, key_queue_start);
                key_queue_start = (key_queue_start + 1) & 0xf;

                keyboard_pcjr.latched = 1;

                keyboard_pcjr.serial_data[0] = 1; /*Start bit*/
                keyboard_pcjr.serial_data[1] = 0;
                
                for (c = 0; c < 8; c++)
                {
                        if (key & (1 << c))
                        {
                                keyboard_pcjr.serial_data[(c + 1) * 2]     = 1;
                                keyboard_pcjr.serial_data[(c + 1) * 2 + 1] = 0;
                                p++;
                        }
                        else
                        {
                                keyboard_pcjr.serial_data[(c + 1) * 2]     = 0;
                                keyboard_pcjr.serial_data[(c + 1) * 2 + 1] = 1;
                        }
                }
                
                if (p & 1) /*Parity*/
                {
                        keyboard_pcjr.serial_data[9 * 2]     = 1;
                        keyboard_pcjr.serial_data[9 * 2 + 1] = 0;
                }
                else
                {
                        keyboard_pcjr.serial_data[9 * 2]     = 0;
                        keyboard_pcjr.serial_data[9 * 2 + 1] = 1;
                }
                
                for (c = 0; c < 11; c++) /*11 stop bits*/
                {                       
                        keyboard_pcjr.serial_data[(c + 10) * 2]     = 0;
                        keyboard_pcjr.serial_data[(c + 10) * 2 + 1] = 0;
                }
                
                keyboard_pcjr.serial_pos++;
        }

        if (keyboard_pcjr.serial_pos)
        {
                keyboard_pcjr.data = keyboard_pcjr.serial_data[keyboard_pcjr.serial_pos - 1];       
                nmi = keyboard_pcjr.data;
                keyboard_pcjr.serial_pos++;
                if (keyboard_pcjr.serial_pos == 42+1)
                        keyboard_pcjr.serial_pos = 0;
//                pclog("Keyboard poll %i %i\n", keyboard_pcjr.data, keyboard_pcjr.serial_pos);
        }
}

void keyboard_pcjr_adddata(uint8_t val)
{
        key_queue[key_queue_end] = val;
//        pclog("keyboard_pcjr : %02X added to key queue at %i\n", val, key_queue_end);
        key_queue_end = (key_queue_end + 1) & 0xf;
        return;
}

void keyboard_pcjr_write(uint16_t port, uint8_t val, void *priv)
{
//        pclog("keyboard_pcjr : write %04X %02X %02X\n", port, val, keyboard_pcjr.pb);
/*        if (ram[8] == 0xc3) 
        {
                output = 3;
        }*/
        switch (port)
        {
                case 0x60:
                keyboard_pcjr.pa = val;
                break;
                
                case 0x61:
                keyboard_pcjr.pb = val;

                timer_process();
                timer_update_outstanding();

		speaker_update();
                speaker_gated = val & 1;
                speaker_enable = val & 2;
                if (speaker_enable) 
                        was_speaker_enable = 1;
                pit_set_gate(2, val & 1);
                sn76489_mute = speaker_mute = 1;
                switch (val & 0x60)
                {
                        case 0x00:
                        speaker_mute = 0;
                        break;
                        case 0x60:
                        sn76489_mute = 0;
                        break;
                }
                break;
                
                case 0xa0:
                nmi_mask = val & 0x80;
                pit_set_using_timer(1, !(val & 0x20));
                break;
        }
}

uint8_t keyboard_pcjr_read(uint16_t port, void *priv)
{
        uint8_t temp;
//        pclog("keyboard_pcjr : read %04X ", port);
        switch (port)
        {
                case 0x60:
                temp = keyboard_pcjr.pa;
                break;
                
                case 0x61:
                temp = keyboard_pcjr.pb;
                break;
                
                case 0x62:
                temp = (keyboard_pcjr.latched ? 1 : 0);
                temp |= 0x02; /*Modem card not installed*/
                temp |= (ppispeakon ? 0x10 : 0);
                temp |= (ppispeakon ? 0x20 : 0);
                temp |= (keyboard_pcjr.data ? 0x40: 0);
//                temp |= 0x04;
                if (keyboard_pcjr.data)
                        temp |= 0x40;
                break;
                
                case 0xa0:
                keyboard_pcjr.latched = 0;
                break;
                
                default:
                pclog("\nBad XT keyboard read %04X\n", port);
                //dumpregs();
                //exit(-1);
        }
//        pclog("%02X\n", temp);
        return temp;
}

void keyboard_pcjr_reset()
{
}

void keyboard_pcjr_init()
{
        //return;
        io_sethandler(0x0060, 0x0004, keyboard_pcjr_read, NULL, NULL, keyboard_pcjr_write, NULL, NULL,  NULL);
        io_sethandler(0x00a0, 0x0008, keyboard_pcjr_read, NULL, NULL, keyboard_pcjr_write, NULL, NULL,  NULL);
        keyboard_pcjr_reset();
        keyboard_send = keyboard_pcjr_adddata;
        keyboard_poll = keyboard_pcjr_poll;

        timer_add(keyboard_pcjr_poll, &keybsenddelay, TIMER_ALWAYS_ENABLED,  NULL);
}
