/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Intel 8042 (AT keyboard controller) emulation.
 *
 * Version:	@(#)keyboard_at.c	1.0.4	2017/10/16
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "86box.h"
#include "ibm.h"
#include "io.h"
#include "pic.h"
#include "pit.h"
#include "mem.h"
#include "rom.h"
#include "timer.h"
#include "floppy/floppy.h"
#include "floppy/fdc.h"
#include "sound/sound.h"
#include "sound/snd_speaker.h"
#include "keyboard.h"
#include "keyboard_at.h"
#include "cpu/cpu.h"
#include "device.h"
#include "machine/machine.h"


#define STAT_PARITY     0x80
#define STAT_RTIMEOUT   0x40
#define STAT_TTIMEOUT   0x20
#define STAT_MFULL      0x20
#define STAT_LOCK       0x10
#define STAT_CD         0x08
#define STAT_SYSFLAG    0x04
#define STAT_IFULL      0x02
#define STAT_OFULL      0x01

#define PS2_REFRESH_TIME (16LL * TIMER_USEC)

#define CCB_UNUSED      0x80
#define CCB_TRANSLATE   0x40
#define CCB_PCMODE      0x20
#define CCB_ENABLEKBD   0x10
#define CCB_IGNORELOCK  0x08
#define CCB_SYSTEM      0x04
#define CCB_ENABLEMINT  0x02
#define CCB_ENABLEKINT  0x01

#define CCB_MASK        0x68
#define MODE_MASK       0x6C

struct
{
        int initialised;
        int want60;
        int wantirq, wantirq12;
        uint8_t command;
        uint8_t status;
        uint8_t mem[0x100];
        uint8_t out;
        int out_new;
	uint8_t secr_phase;
	uint8_t mem_addr;
        
        uint8_t input_port;
        uint8_t output_port;
        
        uint8_t key_command;
        int key_wantdata;
        
        int last_irq;

	uint8_t last_scan_code;
	uint8_t default_mode;
        
        void (*mouse_write)(uint8_t val, void *p);
        void *mouse_p;
        
        int64_t refresh_time;
        int refresh;
        
        int is_ps2;
} keyboard_at;

static uint8_t key_ctrl_queue[16];
static int key_ctrl_queue_start = 0, key_ctrl_queue_end = 0;

static uint8_t key_queue[16];
static int key_queue_start = 0, key_queue_end = 0;

static uint8_t mouse_queue[16];
int mouse_queue_start = 0, mouse_queue_end = 0;

int first_write = 1;
int dtrans = 0;

/* Bits 0 - 1 = scan code set, bit 6 = translate or not. */
uint8_t mode = 0x42;

/* Non-translated to translated scan codes. */
static uint8_t nont_to_t[256] = {	0xFF, 0x43, 0x41, 0x3F, 0x3D, 0x3B, 0x3C, 0x58, 0x64, 0x44, 0x42, 0x40, 0x3E, 0x0F, 0x29, 0x59,
					0x65, 0x38, 0x2A, 0x70, 0x1D, 0x10, 0x02, 0x5A, 0x66, 0x71, 0x2C, 0x1F, 0x1E, 0x11, 0x03, 0x5B,
					0x67, 0x2E, 0x2D, 0x20, 0x12, 0x05, 0x04, 0x5C, 0x68, 0x39, 0x2F, 0x21, 0x14, 0x13, 0x06, 0x5D,
					0x69, 0x31, 0x30, 0x23, 0x22, 0x15, 0x07, 0x5E, 0x6A, 0x72, 0x32, 0x24, 0x16, 0x08, 0x09, 0x5F,
					0x6B, 0x33, 0x25, 0x17, 0x18, 0x0B, 0x0A, 0x60, 0x6C, 0x34, 0x35, 0x26, 0x27, 0x19, 0x0C, 0x61,
					0x6D, 0x73, 0x28, 0x74, 0x1A, 0x0D, 0x62, 0x6E, 0x3A, 0x36, 0x1C, 0x1B, 0x75, 0x2B, 0x63, 0x76,
					0x55, 0x56, 0x77, 0x78, 0x79, 0x7A, 0x0E, 0x7B, 0x7C, 0x4F, 0x7D, 0x4B, 0x47, 0x7E, 0x7F, 0x6F,
					0x52, 0x53, 0x50, 0x4C, 0x4D, 0x48, 0x01, 0x45, 0x57, 0x4E, 0x51, 0x4A, 0x37, 0x49, 0x46, 0x54,
					0x80, 0x81, 0x82, 0x41, 0x54, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F,
					0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
					0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
					0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
					0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF,
					0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
					0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
					0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF	};

int keyboard_at_do_log = 0;

void keyboard_at_log(const char *format, ...)
{
#ifdef ENABLE_KEYBOARD_AT_LOG
	if (keyboard_at_do_log)
	{
		va_list ap;
		va_start(ap, format);
		vprintf(format, ap);
		va_end(ap);
		fflush(stdout);
	}
#endif
}

static void keyboard_at_poll(void)
{
	keybsenddelay += (1000LL * TIMER_USEC);

        if ((keyboard_at.out_new != -1) && !keyboard_at.last_irq)
        {
                keyboard_at.wantirq = 0;
                if (keyboard_at.out_new & 0x100)
                {
			keyboard_at_log("Want mouse data\n");
                        if (keyboard_at.mem[0] & 0x02)
                                picint(0x1000);
                        keyboard_at.out = keyboard_at.out_new & 0xff;
                        keyboard_at.out_new = -1;
                        keyboard_at.status |=  STAT_OFULL;
                        keyboard_at.status &= ~STAT_IFULL;
                        keyboard_at.status |=  STAT_MFULL;
                        keyboard_at.last_irq = 0x1000;
                }
                else
                {
			keyboard_at_log("Want keyboard data\n");
                        if (keyboard_at.mem[0] & 0x01)
                                picint(2);
                        keyboard_at.out = keyboard_at.out_new;
                        keyboard_at.out_new = -1;
                        keyboard_at.status |=  STAT_OFULL;
                        keyboard_at.status &= ~STAT_IFULL;
                        keyboard_at.status &= ~STAT_MFULL;
                        keyboard_at.last_irq = 2;
                }
        }

        if (keyboard_at.out_new == -1 && !(keyboard_at.status & STAT_OFULL) && 
            key_ctrl_queue_start != key_ctrl_queue_end)
        {
                keyboard_at.out_new = key_ctrl_queue[key_ctrl_queue_start];
                key_ctrl_queue_start = (key_ctrl_queue_start + 1) & 0xf;
        }                
        else if (!(keyboard_at.status & STAT_OFULL) && keyboard_at.out_new == -1/* && !(keyboard_at.mem[0] & 0x20)*/ &&
            (mouse_queue_start != mouse_queue_end))
        {
                keyboard_at.out_new = mouse_queue[mouse_queue_start] | 0x100;
                mouse_queue_start = (mouse_queue_start + 1) & 0xf;
        }                
        else if (!(keyboard_at.status & STAT_OFULL) && keyboard_at.out_new == -1 &&
                 !(keyboard_at.mem[0] & 0x10) && (key_queue_start != key_queue_end))
        {
                keyboard_at.out_new = key_queue[key_queue_start];
                key_queue_start = (key_queue_start + 1) & 0xf;
        }                
}

void keyboard_at_adddata(uint8_t val)
{
                key_ctrl_queue[key_ctrl_queue_end] = val;
                key_ctrl_queue_end = (key_ctrl_queue_end + 1) & 0xf;
}

uint8_t sc_or = 0;

void keyboard_at_adddata_keyboard(uint8_t val)
{
	/* Modification by OBattler: Allow for scan code translation. */
	if ((mode & 0x40) && (val == 0xf0) && !(mode & 0x20))
	{
		sc_or = 0x80;
		return;
	}
	/* Skip break code if translated make code has bit 7 set. */
	if ((mode & 0x40) && (sc_or == 0x80) && (nont_to_t[val] & 0x80) && !(mode & 0x20))
	{
		sc_or = 0;
		return;
	}
        key_queue[key_queue_end] = (((mode & 0x40) && !(mode & 0x20)) ? (nont_to_t[val] | sc_or) : val);
        key_queue_end = (key_queue_end + 1) & 0xf;
	if (sc_or == 0x80)  sc_or = 0;
        return;
}

void keyboard_at_adddata_keyboard_raw(uint8_t val)
{
        key_queue[key_queue_end] = val;
        key_queue_end = (key_queue_end + 1) & 0xf;
        return;
}

void keyboard_at_adddata_mouse(uint8_t val)
{
        mouse_queue[mouse_queue_end] = val;
        mouse_queue_end = (mouse_queue_end + 1) & 0xf;
        return;
}

void keyboard_at_write(uint16_t port, uint8_t val, void *priv)
{
	int i = 0;
        switch (port)
        {
                case 0x60:
                if (keyboard_at.want60)
                {
                        /*Write to controller*/
                        keyboard_at.want60 = 0;
                        switch (keyboard_at.command)
                        {
				/* 0x40 - 0x5F are aliases for 0x60-0x7F */
                                case 0x40: case 0x41: case 0x42: case 0x43:
                                case 0x44: case 0x45: case 0x46: case 0x47:
                                case 0x48: case 0x49: case 0x4a: case 0x4b:
                                case 0x4c: case 0x4d: case 0x4e: case 0x4f:
                                case 0x50: case 0x51: case 0x52: case 0x53:
                                case 0x54: case 0x55: case 0x56: case 0x57:
                                case 0x58: case 0x59: case 0x5a: case 0x5b:
                                case 0x5c: case 0x5d: case 0x5e: case 0x5f:
				keyboard_at.command |= 0x20;
				goto write_register;

                                case 0x60: case 0x61: case 0x62: case 0x63:
                                case 0x64: case 0x65: case 0x66: case 0x67:
                                case 0x68: case 0x69: case 0x6a: case 0x6b:
                                case 0x6c: case 0x6d: case 0x6e: case 0x6f:
                                case 0x70: case 0x71: case 0x72: case 0x73:
                                case 0x74: case 0x75: case 0x76: case 0x77:
                                case 0x78: case 0x79: case 0x7a: case 0x7b:
                                case 0x7c: case 0x7d: case 0x7e: case 0x7f:

write_register:
                                keyboard_at.mem[keyboard_at.command & 0x1f] = val;
                                if (keyboard_at.command == 0x60)
                                {
                                        if ((val & 1) && (keyboard_at.status & STAT_OFULL))
                                           keyboard_at.wantirq = 1;
                                        if (!(val & 1) && keyboard_at.wantirq)
                                           keyboard_at.wantirq = 0;
                                        mouse_scan = !(val & 0x20);
					keyboard_at_log("Mouse is now %s\n",  mouse_scan ? "enabled" : "disabled");
					keyboard_at_log("Mouse interrupt is now %s\n",  (val & 0x02) ? "enabled" : "disabled");

					/* Addition by OBattler: Scan code translate ON/OFF. */
					mode &= 0x93;
					mode |= (val & MODE_MASK);
					if (first_write)
					{
						/* A bit of a hack, but it will make the keyboard behave correctly, regardless
						   of what the BIOS sets here. */
						mode &= 0xFC;
						dtrans = mode & (CCB_TRANSLATE | CCB_PCMODE);
						if ((mode & (CCB_TRANSLATE | CCB_PCMODE)) == CCB_TRANSLATE)
						{
							/* Bit 6 on, bit 5 off, the only case in which translation is on,
							   therefore, set to set 2. */
							mode |= 2;
						}
						keyboard_at.default_mode = (mode & 3);
						first_write = 0;
						/* No else because in all other cases, translation is off, so we need to keep it
						   set to set 0 which the mode &= 0xFC above will set it. */
					}
                                }                                           
                                break;

				case 0xaf: /*AMI - set extended controller RAM*/
				keyboard_at_log("AMI - set extended controller RAM\n");
				if (keyboard_at.secr_phase == 0)
				{
					goto bad_command;
				}
				else if (keyboard_at.secr_phase == 1)
				{
					keyboard_at.mem_addr = val;
					keyboard_at.want60 = 1;
					keyboard_at.secr_phase = 2;
				}
				else if (keyboard_at.secr_phase == 2)
				{
					keyboard_at.mem[keyboard_at.mem_addr] = val;
					keyboard_at.secr_phase = 0;
				}
				break;

                                case 0xcb: /*AMI - set keyboard mode*/
				keyboard_at_log("AMI - set keyboard mode\n");
                                break;
                                
                                case 0xcf: /*??? - sent by MegaPC BIOS*/
				keyboard_at_log("??? - sent by MegaPC BIOS\n");
				/* To make sure the keyboard works correctly on the MegaPC. */
				mode &= 0xFC;
				mode |= 2;
                                break;
                                
                                case 0xd1: /*Write output port*/
				keyboard_at_log("Write output port\n");
                                if ((keyboard_at.output_port ^ val) & 0x02) /*A20 enable change*/
                                {
                                        mem_a20_key = val & 0x02;
                                        mem_a20_recalc();
                                        flushmmucache();
                                }
                                keyboard_at.output_port = val;
                                break;
                                
                                case 0xd2: /*Write to keyboard output buffer*/
				keyboard_at_log("Write to keyboard output buffer\n");
                                keyboard_at_adddata_keyboard(val);
                                break;
                                
                                case 0xd3: /*Write to mouse output buffer*/
				keyboard_at_log("Write to mouse output buffer\n");
                                keyboard_at_adddata_mouse(val);
                                break;
                                
                                case 0xd4: /*Write to mouse*/
				keyboard_at_log("Write to mouse (%02X)\n", val);
                                if (keyboard_at.mouse_write && (machines[machine].flags & MACHINE_PS2))
                                        keyboard_at.mouse_write(val, keyboard_at.mouse_p);
				else
					keyboard_at_adddata_mouse(0xff);
                                break;     
                                
                                default:
bad_command:
                                keyboard_at_log("Bad AT keyboard controller 0060 write %02X command %02X\n", val, keyboard_at.command);
                        }
                }
                else
                {
                        /*Write to keyboard*/                        
                        keyboard_at.mem[0] &= ~0x10;
                        if (keyboard_at.key_wantdata)
                        {
                                keyboard_at.key_wantdata = 0;
                                switch (keyboard_at.key_command)
                                {
                                        case 0xed: /*Set/reset LEDs*/
                                        keyboard_at_adddata_keyboard(0xfa);
                                        break;

					case 0xf0: /*Get/set scancode set*/
					if (val == 0)
					{
						keyboard_at_adddata_keyboard(mode & 3);
					}
					else
					{
						if (val <= 3)
						{
							mode &= 0xFC;
							mode |= (val & 3);
						}
						keyboard_at_adddata_keyboard(0xfa);
					}
					break;

                                        case 0xf3: /*Set typematic rate/delay*/
                                        keyboard_at_adddata_keyboard(0xfa);
                                        break;
                                        
                                        default:
                                        keyboard_at_log("Bad AT keyboard 0060 write %02X command %02X\n", val, keyboard_at.key_command);
                                }
                        }
                        else
                        {
                                keyboard_at.key_command = val;
                                switch (val)
                                {
					case 0x00:
					keyboard_at_adddata_keyboard(0xfa);
					break;

                                        case 0x05: /*??? - sent by NT 4.0*/
                                        keyboard_at_adddata_keyboard(0xfe);
                                        break;

					case 0x71: /*These two commands are sent by Pentium-era AMI BIOS'es.*/
					case 0x82:
					break;

                                        case 0xed: /*Set/reset LEDs*/
                                        keyboard_at.key_wantdata = 1;
                                        keyboard_at_adddata_keyboard(0xfa);
                                        break;

					case 0xee: /*Diagnostic echo*/
                                        keyboard_at_adddata_keyboard(0xee);
					break;

					case 0xef: /*NOP (No OPeration). Reserved for future use.*/
					break;
                                        
					case 0xf0: /*Get/set scan code set*/
					keyboard_at.key_wantdata = 1;
					keyboard_at_adddata_keyboard(0xfa);
					break;
                                        
                                        case 0xf2: /*Read ID*/
					/* Fixed as translation will be done in keyboard_at_adddata_keyboard(). */
                                        keyboard_at_adddata_keyboard(0xfa);
                                        keyboard_at_adddata_keyboard(0xab);
                                        keyboard_at_adddata_keyboard(0x83);
                                        break;
                                        
                                        case 0xf3: /*Set typematic rate/delay*/
                                        keyboard_at.key_wantdata = 1;
                                        keyboard_at_adddata_keyboard(0xfa);
                                        break;
                                        
                                        case 0xf4: /*Enable keyboard*/
                                        keyboard_scan = 1;
                                        keyboard_at_adddata_keyboard(0xfa);
                                        break;
                                        case 0xf5: /*Disable keyboard*/
                                        keyboard_scan = 0;
                                        keyboard_at_adddata_keyboard(0xfa);
                                        break;
                                        
                                        case 0xf6: /*Set defaults*/
					set3_all_break = 0;
					set3_all_repeat = 0;
					memset(set3_flags, 0, 272);
					mode = (mode & 0xFC) | keyboard_at.default_mode;
                                        keyboard_at_adddata_keyboard(0xfa);
                                        break;
                                        
                                        case 0xf7: /*Set all keys to repeat*/
					set3_all_break = 1;
                                        keyboard_at_adddata_keyboard(0xfa);
                                        break;
                                        
                                        case 0xf8: /*Set all keys to give make/break codes*/
					set3_all_break = 1;
                                        keyboard_at_adddata_keyboard(0xfa);
                                        break;
                                        
                                        case 0xf9: /*Set all keys to give make codes only*/
					set3_all_break = 0;
                                        keyboard_at_adddata_keyboard(0xfa);
                                        break;
                                        
                                        case 0xfa: /*Set all keys to repeat and give make/break codes*/
                                        set3_all_repeat = 1;
					set3_all_break = 1;
                                        keyboard_at_adddata_keyboard(0xfa);
                                        break;
                                        
                                        case 0xfe: /*Resend last scan code*/
                                        keyboard_at_adddata_keyboard(keyboard_at.last_scan_code);
					break;
                                        
                                        case 0xff: /*Reset*/
                                        key_queue_start = key_queue_end = 0; /*Clear key queue*/
                                        keyboard_at_adddata_keyboard(0xfa);
                                        keyboard_at_adddata_keyboard(0xaa);
					/* Set system flag to 1 and scan code set to 2. */
					mode &= 0xFC;
					mode |= 2;
                                        break;
                                        
                                        default:
                                        keyboard_at_log("Bad AT keyboard command %02X\n", val);
                                        keyboard_at_adddata_keyboard(0xfe);
                                }
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
                
                case 0x64:
                keyboard_at.want60 = 0;
                keyboard_at.command = val;
                /*New controller command*/
                switch (val)
                {
                        case 0x00: case 0x01: case 0x02: case 0x03:
                        case 0x04: case 0x05: case 0x06: case 0x07:
                        case 0x08: case 0x09: case 0x0a: case 0x0b:
                        case 0x0c: case 0x0d: case 0x0e: case 0x0f:
                        case 0x10: case 0x11: case 0x12: case 0x13:
                        case 0x14: case 0x15: case 0x16: case 0x17:
                        case 0x18: case 0x19: case 0x1a: case 0x1b:
                        case 0x1c: case 0x1d: case 0x1e: case 0x1f:
			val |= 0x20;				/* 0x00-0x1f are aliases for 0x20-0x3f */
                        keyboard_at_adddata(keyboard_at.mem[val & 0x1f]);
                        break;

                        case 0x20: case 0x21: case 0x22: case 0x23:
                        case 0x24: case 0x25: case 0x26: case 0x27:
                        case 0x28: case 0x29: case 0x2a: case 0x2b:
                        case 0x2c: case 0x2d: case 0x2e: case 0x2f:
                        case 0x30: case 0x31: case 0x32: case 0x33:
                        case 0x34: case 0x35: case 0x36: case 0x37:
                        case 0x38: case 0x39: case 0x3a: case 0x3b:
                        case 0x3c: case 0x3d: case 0x3e: case 0x3f:
                        keyboard_at_adddata(keyboard_at.mem[val & 0x1f]);
                        break;

                        case 0x60: case 0x61: case 0x62: case 0x63:
                        case 0x64: case 0x65: case 0x66: case 0x67:
                        case 0x68: case 0x69: case 0x6a: case 0x6b:
                        case 0x6c: case 0x6d: case 0x6e: case 0x6f:
                        case 0x70: case 0x71: case 0x72: case 0x73:
                        case 0x74: case 0x75: case 0x76: case 0x77:
                        case 0x78: case 0x79: case 0x7a: case 0x7b:
                        case 0x7c: case 0x7d: case 0x7e: case 0x7f:
                        keyboard_at.want60 = 1;
                        break;
                        
                        case 0xa1: /*AMI - get controller version*/
			keyboard_at_log("AMI - get controller version\n");
                        break;
                                
                        case 0xa7: /*Disable mouse port*/
			if (machines[machine].flags & MACHINE_PS2)
			{
				keyboard_at_log("Disable mouse port\n");
        	                mouse_scan = 0;
				keyboard_at.mem[0] |= 0x20;
			}
			else
			{
				keyboard_at_log("Write Cache Bad\n");
			}
                        break;

                        case 0xa8: /*Enable mouse port*/
			if (machines[machine].flags & MACHINE_PS2)
			{
				keyboard_at_log("Enable mouse port\n");
        	                mouse_scan = 1;
				keyboard_at.mem[0] &= 0xDF;
			}
			else
			{
				keyboard_at_log("Write Cache Good\n");
			}
                        break;
                        
                        case 0xa9: /*Test mouse port*/
			keyboard_at_log("Test mouse port\n");
			if (machines[machine].flags & MACHINE_PS2)
			{
	                        keyboard_at_adddata(0x00); /*no error*/
			}
			else
			{
	                        keyboard_at_adddata(0xff); /*no mouse*/
			}
                        break;
                        
                        case 0xaa: /*Self-test*/
			keyboard_at_log("Self-test\n");
                        if (!keyboard_at.initialised)
                        {
                                keyboard_at.initialised = 1;
                                key_ctrl_queue_start = key_ctrl_queue_end = 0;
                                keyboard_at.status &= ~STAT_OFULL;
                        }
                        keyboard_at.status |= STAT_SYSFLAG;
                        keyboard_at.mem[0] |= 0x04;
                        keyboard_at_adddata(0x55);
                        /*Self-test also resets the output port, enabling A20*/
                        if (!(keyboard_at.output_port & 0x02))
                        {
                                mem_a20_key = 2;
                                mem_a20_recalc();
                                flushmmucache();
                        }
                        keyboard_at.output_port = 0xcf;
                        break;
                        
                        case 0xab: /*Interface test*/
			keyboard_at_log("Interface test\n");
                        keyboard_at_adddata(0x00); /*no error*/
                        break;
                        
                        case 0xac: /*Diagnostic dump*/
			keyboard_at_log("Diagnostic dump\n");
			for (i = 0; i < 16; i++)
			{
	                        keyboard_at_adddata(keyboard_at.mem[i]);
			}
                        keyboard_at_adddata((keyboard_at.input_port & 0xf0) | 0x80);
                        keyboard_at_adddata(keyboard_at.output_port);
                        keyboard_at_adddata(keyboard_at.status);
                        break;
                        
                        case 0xad: /*Disable keyboard*/
			keyboard_at_log("Disable keyboard\n");
                        keyboard_at.mem[0] |=  0x10;
                        break;

                        case 0xae: /*Enable keyboard*/
			keyboard_at_log("Enable keyboard\n");
                        keyboard_at.mem[0] &= ~0x10;
                        break;
                        
                        case 0xaf:
			switch(romset)
			{
				case ROM_AMI286:
				case ROM_AMI386SX:
				case ROM_AMI386DX_OPTI495:
				case ROM_MR386DX_OPTI495:
				case ROM_AMI486:
				case ROM_WIN486:
				case ROM_REVENGE:
				case ROM_PLATO:
				case ROM_ENDEAVOR:
				case ROM_THOR:
				case ROM_MRTHOR:
				case ROM_AP53:
				case ROM_P55T2S:
				case ROM_S1668:
					/*Set extended controller RAM*/
					keyboard_at_log("Set extended controller RAM\n");
		                        keyboard_at.want60 = 1;
					keyboard_at.secr_phase = 1;
					break;
				default:
					/*Read keyboard version*/
					keyboard_at_log("Read keyboard version\n");
		                        keyboard_at_adddata(0x00);
					break;
			}
			break;

			case 0xb0: case 0xb1: case 0xb2: case 0xb3: case 0xb4: case 0xb5: case 0xb6: case 0xb7:
			case 0xb8: case 0xb9: case 0xba: case 0xbb: case 0xbc: case 0xbd: case 0xbe: case 0xbf:
			/*Set keyboard lines low (B0-B7) or high (B8-BF)*/
			keyboard_at_log("Set keyboard lines low (B0-B7) or high (B8-BF)\n");
			keyboard_at_adddata(0x00);
			break;

                        case 0xc0: /*Read input port*/
			keyboard_at_log("Read input port\n");
                        keyboard_at_adddata(keyboard_at.input_port | 4 | fdc_ps1_525());
                        keyboard_at.input_port = ((keyboard_at.input_port + 1) & 3) | (keyboard_at.input_port & 0xfc) | fdc_ps1_525();
                        break;

			case 0xc1: /*Copy bits 0 to 3 of input port to status bits 4 to 7*/
			keyboard_at_log("Copy bits 0 to 3 of input port to status bits 4 to 7\n");
			keyboard_at.status &= 0xf;
			keyboard_at.status |= ((((keyboard_at.input_port & 0xfc) | 0x84 | fdc_ps1_525()) & 0xf) << 4);
			break;
                        
			case 0xc2: /*Copy bits 4 to 7 of input port to status bits 4 to 7*/
			keyboard_at_log("Copy bits 4 to 7 of input port to status bits 4 to 7\n");
			keyboard_at.status &= 0xf;
			keyboard_at.status |= (((keyboard_at.input_port & 0xfc) | 0x84 | fdc_ps1_525()) & 0xf0);
			break;
                        
                        case 0xc9: /*AMI - block P22 and P23 ???*/
			keyboard_at_log("AMI - block P22 and P23 ???\n");
                        break;
                        
                        case 0xca: /*AMI - read keyboard mode*/
			keyboard_at_log("AMI - read keyboard mode\n");
                        keyboard_at_adddata(0x00); /*ISA mode*/
                        break;
                        
                        case 0xcb: /*AMI - set keyboard mode*/
			keyboard_at_log("AMI - set keyboard mode\n");
                        keyboard_at.want60 = 1;
                        break;
                        
                        case 0xcf: /*??? - sent by MegaPC BIOS*/
			keyboard_at_log("??? - sent by MegaPC BIOS\n");
                        keyboard_at.want60 = 1;
                        break;
                        
                        case 0xd0: /*Read output port*/
			keyboard_at_log("Read output port\n");
                        keyboard_at_adddata(keyboard_at.output_port);
                        break;
                        
                        case 0xd1: /*Write output port*/
			keyboard_at_log("Write output port\n");
                        keyboard_at.want60 = 1;
                        break;

                        case 0xd2: /*Write keyboard output buffer*/
			keyboard_at_log("Write keyboard output buffer\n");
                        keyboard_at.want60 = 1;
                        break;
                        
                        case 0xd3: /*Write mouse output buffer*/
			keyboard_at_log("Write mouse output buffer\n");
                        keyboard_at.want60 = 1;
                        break;
                        
                        case 0xd4: /*Write to mouse*/
			keyboard_at_log("Write to mouse\n");
                        keyboard_at.want60 = 1;
                        break;

			case 0xdd: /* Disable A20 Address Line */
			keyboard_at_log("Disable A20 Address Line\n");
			keyboard_at.output_port &= ~0x02;
			mem_a20_key = 0;
			mem_a20_recalc();
			flushmmucache();
			break;

			case 0xdf: /* Enable A20 Address Line */
			keyboard_at_log("Enable A20 Address Line\n");
			keyboard_at.output_port |= 0x02;
			mem_a20_key = 2;
			mem_a20_recalc();
			flushmmucache();
			break;

                        case 0xe0: /*Read test inputs*/
			keyboard_at_log("Read test inputs\n");
                        keyboard_at_adddata(0x00);
                        break;
                        
                        case 0xef: /*??? - sent by AMI486*/
			keyboard_at_log("??? - sent by AMI486\n");
                        break;

			case 0xf0: case 0xf1: case 0xf2: case 0xf3: case 0xf4: case 0xf5: case 0xf6: case 0xf7:
			case 0xf8: case 0xf9: case 0xfa: case 0xfb: case 0xfc: case 0xfd: case 0xfe: case 0xff:
			keyboard_at_log("Pulse\n");
			if (!(val & 1))
			{
				/* Pin 0 selected. */
				/* trc_reset(2); */
        	                softresetx86(); /*Pulse reset!*/
	                        cpu_set_edx();
			}
			break;
                                
                        default:
                        keyboard_at_log("Bad AT keyboard controller command %02X\n", val);
                }
        }
}

uint8_t keyboard_at_get_mouse_scan(void)
{
	return mouse_scan ? 0x10 : 0x00;
}

void keyboard_at_set_mouse_scan(uint8_t val)
{
	uint8_t temp_mouse_scan = val ? 1 : 0;

	if (temp_mouse_scan == mouse_scan)
	{
		return;
	}

	mouse_scan = val ? 1 : 0;

	keyboard_at.mem[0] &= 0xDF;
	keyboard_at.mem[0] |= (val ? 0x00 : 0x20);

	keyboard_at_log("Mouse scan %sabled via PCI\n", mouse_scan ? "en" : "dis");
}

uint8_t keyboard_at_read(uint16_t port, void *priv)
{
        uint8_t temp = 0xff;
        switch (port)
        {
                case 0x60:
                temp = keyboard_at.out;
                keyboard_at.status &= ~(STAT_OFULL/* | STAT_MFULL*/);
                picintc(keyboard_at.last_irq);
                keyboard_at.last_irq = 0;
                break;

                case 0x61:
                temp = ppi.pb & ~0xe0;
                if (ppispeakon)
                        temp |= 0x20;
                if (keyboard_at.is_ps2)
                {
                        if (keyboard_at.refresh)
                                temp |= 0x10;
                        else
                                temp &= ~0x10;
                }
                break;
                
                case 0x64:
                temp = (keyboard_at.status & 0xFB) | (mode & CCB_SYSTEM);
		/* if (mode & CCB_IGNORELOCK) */  temp |= STAT_LOCK;
                keyboard_at.status &= ~(STAT_RTIMEOUT/* | STAT_TTIMEOUT*/);
                break;
        }
        return temp;
}

void keyboard_at_reset(void)
{
        keyboard_at.initialised = 0;
        keyboard_at.status = STAT_LOCK | STAT_CD;
        keyboard_at.mem[0] = 0x31;
	mode = 0x02 | dtrans;
	keyboard_at.default_mode = 2;
	first_write = 1;
        keyboard_at.wantirq = 0;
        keyboard_at.output_port = 0xcf;
        keyboard_at.input_port = (MDA) ? 0xf0 : 0xb0;
        keyboard_at.out_new = -1;
        keyboard_at.last_irq = 0;
	keyboard_at.secr_phase = 0;
        
        keyboard_at.key_wantdata = 0;
        
        keyboard_scan = 1;

	mouse_scan = 0;

	sc_or = 0;

	memset(set3_flags, 0, 272);
}

static void at_refresh(void *p)
{
        keyboard_at.refresh = !keyboard_at.refresh;
        keyboard_at.refresh_time += PS2_REFRESH_TIME;
}

void keyboard_at_init(void)
{
        io_sethandler(0x0060, 0x0005, keyboard_at_read, NULL, NULL, keyboard_at_write, NULL, NULL,  NULL);
        keyboard_at_reset();
        keyboard_send = keyboard_at_adddata_keyboard;
        keyboard_poll = keyboard_at_poll;
        keyboard_at.mouse_write = NULL;
        keyboard_at.mouse_p = NULL;
        keyboard_at.is_ps2 = 0;
	dtrans = 0;
        
        timer_add((void (*)(void *))keyboard_at_poll, &keybsenddelay, TIMER_ALWAYS_ENABLED,  NULL);
}

void keyboard_at_set_mouse(void (*mouse_write)(uint8_t val, void *p), void *p)
{
        keyboard_at.mouse_write = mouse_write;
        keyboard_at.mouse_p = p;
}

void keyboard_at_init_ps2(void)
{
        timer_add(at_refresh, &keyboard_at.refresh_time, TIMER_ALWAYS_ENABLED,  NULL);
        keyboard_at.is_ps2 = 1;
}
