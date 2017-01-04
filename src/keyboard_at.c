/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#include <stdint.h>

#include "ibm.h"
#include "io.h"
#include "mem.h"
#include "pic.h"
#include "pit.h"
#include "sound.h"
#include "sound_speaker.h"
#include "timer.h"

#include "keyboard.h"
#include "keyboard_at.h"

#define STAT_PARITY     0x80
#define STAT_RTIMEOUT   0x40
#define STAT_TTIMEOUT   0x20
#define STAT_MFULL      0x20
#define STAT_LOCK       0x10
#define STAT_CD         0x08
#define STAT_SYSFLAG    0x04
#define STAT_IFULL      0x02
#define STAT_OFULL      0x01

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
        uint8_t mem[0x20];
        uint8_t out;
        int out_new;
        
        uint8_t input_port;
        uint8_t output_port;
        
        uint8_t key_command;
        int key_wantdata;
        
        int last_irq;
        
        void (*mouse_write)(uint8_t val, void *p);
        void *mouse_p;
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

#if 0
/* Translated to non-translated scan codes. */
					/*	Assuming we get XSET1, SET1/XSET2/XSET3 = T_TO_NONT(XSET1), and then we go through
						T_TO_NONT again to get SET2/SET3.							*/
/* static uint8_t t_to_nont[256] = {	0xFF, 0x76, 0x16, 0x1E, 0x26, 0x25, 0x2E, 0x36, 0x3D, 0x3E, 0x46, 0x45, 0x4E, 0x55, 0x66, 0x0D,
					0x15, 0x1D, 0x24, 0x2D, 0x2C, 0x35, 0x3C, 0x43, 0x44, 0x4D, 0x54, 0x5B, 0x5A, 0x14, 0x1C, 0x1B,
					0x23, 0x2B, 0x34, 0x33, 0x3B, 0x42, 0x4B, 0x4C, 0x52, 0x0E, 0x12, 0x5D, 0x1A, 0x22, 0x21, 0x2A,
					0x32, 0x31, 0x3A, 0x41, 0x49, 0x4A, 0x59, 0x7C, 0x11, 0x29, 0x58, 0x05, 0x06, 0x04, 0x0C, 0x03,
					0x0B, 0x02, 0x0A, 0x01, 0x09, 0x77, 0x7E, 0x6C, 0x75, 0x7D, 0x7B, 0x6B, 0x73, 0x74, 0x79, 0x69,
					0x72, 0x7A, 0x70, 0x71, 0x7F, 0x60, 0x61, 0x78, 0x07, 0x0F, 0x17, 0x1F, 0x27, 0x2F, 0x37, 0x3F,
					0x47, 0x4F, 0x56, 0x5E, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x48, 0x50, 0x57, 0x6F,
					0x13, 0x19, 0x39, 0x51, 0x53, 0x5C, 0x5F, 0x62, 0x63, 0x64, 0x65, 0x67, 0x68, 0x6A, 0x6D, 0x6E,
					0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F,
					0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
					0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
					0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
					0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF,
					0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
					0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
					0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF	}; */
#endif

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

void keyboard_at_poll()
{
	keybsenddelay += (1000 * TIMER_USEC);

        if (keyboard_at.out_new != -1 && !keyboard_at.last_irq)
        {
                keyboard_at.wantirq = 0;
                if (keyboard_at.out_new & 0x100)
                {
                        if (keyboard_at.mem[0] & 0x02)
                                picint(0x1000);
                        keyboard_at.out = keyboard_at.out_new & 0xff;
                        keyboard_at.out_new = -1;
                        keyboard_at.status |=  STAT_OFULL;
                        keyboard_at.status &= ~STAT_IFULL;
                        keyboard_at.status |=  STAT_MFULL;
//                        pclog("keyboard_at : take IRQ12\n");
                        keyboard_at.last_irq = 0x1000;
                }
                else
                {
                        if (keyboard_at.mem[0] & 0x01)
                                picint(2);
                        keyboard_at.out = keyboard_at.out_new;
                        keyboard_at.out_new = -1;
                        keyboard_at.status |=  STAT_OFULL;
                        keyboard_at.status &= ~STAT_IFULL;
                        keyboard_at.status &= ~STAT_MFULL;
//                        pclog("keyboard_at : take IRQ1\n");
                        keyboard_at.last_irq = 2;
                }
        }

        if (!(keyboard_at.status & STAT_OFULL) && keyboard_at.out_new == -1 && /*!(keyboard_at.mem[0] & 0x20) &&*/
            mouse_queue_start != mouse_queue_end)
        {
                keyboard_at.out_new = mouse_queue[mouse_queue_start] | 0x100;
                mouse_queue_start = (mouse_queue_start + 1) & 0xf;
        }                
        else if (!(keyboard_at.status & STAT_OFULL) && keyboard_at.out_new == -1 &&
                 !(keyboard_at.mem[0] & 0x10) && key_queue_start != key_queue_end)
        {
                keyboard_at.out_new = key_queue[key_queue_start];
                key_queue_start = (key_queue_start + 1) & 0xf;
        }                
        else if (keyboard_at.out_new == -1 && !(keyboard_at.status & STAT_OFULL) && 
            key_ctrl_queue_start != key_ctrl_queue_end)
        {
                keyboard_at.out_new = key_ctrl_queue[key_ctrl_queue_start];
                key_ctrl_queue_start = (key_ctrl_queue_start + 1) & 0xf;
        }                
}

void keyboard_at_adddata(uint8_t val)
{
//        if (keyboard_at.status & STAT_OFULL)
//        {
                key_ctrl_queue[key_ctrl_queue_end] = val;
                key_ctrl_queue_end = (key_ctrl_queue_end + 1) & 0xf;
//                pclog("keyboard_at : %02X added to queue\n", val);                
/*                return;
        }
        keyboard_at.out = val;
        keyboard_at.status |=  STAT_OFULL;
        keyboard_at.status &= ~STAT_IFULL;
        if (keyboard_at.mem[0] & 0x01)
           keyboard_at.wantirq = 1;        
        pclog("keyboard_at : output %02X (IRQ %i)\n", val, keyboard_at.wantirq);*/
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
//        pclog("keyboard_at : %02X added to mouse queue\n", val);
        return;
}

void keyboard_at_write(uint16_t port, uint8_t val, void *priv)
{
	int i = 0;
//        pclog("keyboard_at : write %04X %02X %i  %02X\n", port, val, keyboard_at.key_wantdata, ram[8]);
/*        if (ram[8] == 0xc3) 
        {
                output = 3;
        }*/
        switch (port)
        {
                case 0x60:
                if (keyboard_at.want60)
                {
                        /*Write to controller*/
                        keyboard_at.want60 = 0;
                        switch (keyboard_at.command)
                        {
				case 0x40 ... 0x5f:				/* 0x40 - 0x5F are aliases for 0x60-0x7F */
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

					/* Addition by OBattler: Scan code translate ON/OFF. */
					// pclog("KEYBOARD_AT: Writing %02X to system register\n", val);
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
						first_write = 0;
						// pclog("Keyboard set to scan code set %i, mode & 0x60 = 0x%02X\n", mode & 3, mode & 0x60);
						/* No else because in all other cases, translation is off, so we need to keep it
						   set to set 0 which the mode &= 0xFC above will set it. */
					}
                                }                                           
                                break;

                                case 0xcb: /*AMI - set keyboard mode*/
                                break;
                                
                                case 0xcf: /*??? - sent by MegaPC BIOS*/
				/* To make sure the keyboard works correctly on the MegaPC. */
				mode &= 0xFC;
				mode |= 2;
                                break;
                                
                                case 0xd1: /*Write output port*/
//                                pclog("Write output port - %02X %02X %04X:%04X\n", keyboard_at.output_port, val, CS, pc);
                                if ((keyboard_at.output_port ^ val) & 0x02) /*A20 enable change*/
                                {
                                        mem_a20_key = val & 0x02;
                                        mem_a20_recalc();
//                                        pclog("Rammask change to %08X %02X\n", rammask, val & 0x02);
                                        flushmmucache();
                                }
                                keyboard_at.output_port = val;
                                break;
                                
                                case 0xd2: /*Write to keyboard output buffer*/
                                keyboard_at_adddata_keyboard(val);
                                break;
                                
                                case 0xd3: /*Write to mouse output buffer*/
                                keyboard_at_adddata_mouse(val);
                                break;
                                
                                case 0xd4: /*Write to mouse*/
                                if (keyboard_at.mouse_write)
                                        keyboard_at.mouse_write(val, keyboard_at.mouse_p);
                                break;     
                                
                                default:
                                pclog("Bad AT keyboard controller 0060 write %02X command %02X\n", val, keyboard_at.command);
//                                dumpregs();
//                                exit(-1);
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
					// pclog("KEYBOARD_AT: Get/set scan code set: %i\n", val);
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
                                        pclog("Bad AT keyboard 0060 write %02X command %02X\n", val, keyboard_at.key_command);
//                                        dumpregs();
//                                        exit(-1);
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

                                        case 0xed: /*Set/reset LEDs*/
                                        keyboard_at.key_wantdata = 1;
                                        keyboard_at_adddata_keyboard(0xfa);
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
					// pclog("KEYBOARD_AT: Set defaults\n");
					set3_all_break = 0;
					set3_all_repeat = 0;
					memset(set3_flags, 0, 272);
					mode = (mode & 0xFC) | 2;
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
                                        
                                        case 0xff: /*Reset*/
					// pclog("KEYBOARD_AT: Set defaults\n");
                                        key_queue_start = key_queue_end = 0; /*Clear key queue*/
                                        keyboard_at_adddata_keyboard(0xfa);
                                        keyboard_at_adddata_keyboard(0xaa);
					/* Set system flag to 1 and scan code set to 2. */
					mode &= 0xFC;
					mode |= 2;
                                        break;
                                        
                                        default:
                                        pclog("Bad AT keyboard command %02X\n", val);
                                        keyboard_at_adddata_keyboard(0xfe);
//                                        dumpregs();
//                                        exit(-1);
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
                pit_set_gate(2, val & 1);
                break;
                
                case 0x64:
                keyboard_at.want60 = 0;
                keyboard_at.command = val;
                /*New controller command*/
                switch (val)
                {
			case 0x00 ... 0x1f:
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
                        
                        case 0xa1: /*AMI - get controlled version*/
                        break;
                                
                        case 0xa7: /*Disable mouse port*/
                        mouse_scan = 0;
                        break;

                        case 0xa8: /*Enable mouse port*/
                        mouse_scan = 1;
                        break;
                        
                        case 0xa9: /*Test mouse port*/
                        keyboard_at_adddata(0x00); /*no error*/
                        break;
                        
                        case 0xaa: /*Self-test*/
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
//                                pclog("Rammask change to %08X %02X\n", rammask, val & 0x02);
                                flushmmucache();
                        }
                        keyboard_at.output_port = 0xcf;
                        break;
                        
                        case 0xab: /*Interface test*/
                        keyboard_at_adddata(0x00); /*no error*/
                        break;
                        
                        case 0xac: /*Diagnostic dump*/
			for (i = 0; i < 16; i++)
			{
	                        keyboard_at_adddata(keyboard_at.mem[i]);
			}
                        keyboard_at_adddata((keyboard_at.input_port & 0xf0) | 0x80);
                        keyboard_at_adddata(keyboard_at.output_port);
                        keyboard_at_adddata(keyboard_at.status);
                        break;
                        
                        case 0xad: /*Disable keyboard*/
                        keyboard_at.mem[0] |=  0x10;
                        break;

                        case 0xae: /*Enable keyboard*/
                        keyboard_at.mem[0] &= ~0x10;
                        break;
                        
                        case 0xc0: /*Read input port*/
                        keyboard_at_adddata((keyboard_at.input_port & 0xf0) | 0x80);
                        // keyboard_at_adddata(keyboard_at.input_port | 4);
                        // keyboard_at.input_port = ((keyboard_at.input_port + 1) & 3) | (keyboard_at.input_port & 0xfc);
                        break;

			case 0xc1: /*Copy bits 0 to 3 of input port to status bits 4 to 7*/
			keyboard_at.status &= 0xf;
			keyboard_at.status |= ((keyboard_at.input_port & 0xf) << 4);
			break;
                        
			case 0xc2: /*Copy bits 4 to 7 of input port to status bits 4 to 7*/
			keyboard_at.status &= 0xf;
			keyboard_at.status |= (keyboard_at.input_port & 0xf0);
			break;
                        
                        case 0xc9: /*AMI - block P22 and P23 ??? */
                        break;
                        
                        case 0xca: /*AMI - read keyboard mode*/
                        keyboard_at_adddata(0x00); /*ISA mode*/
                        break;
                        
                        case 0xcb: /*AMI - set keyboard mode*/
                        keyboard_at.want60 = 1;
                        break;
                        
                        case 0xcf: /*??? - sent by MegaPC BIOS*/
                        keyboard_at.want60 = 1;
                        break;
                        
                        case 0xd0: /*Read output port*/
                        keyboard_at_adddata(keyboard_at.output_port);
                        break;
                        
                        case 0xd1: /*Write output port*/
                        keyboard_at.want60 = 1;
                        break;

                        case 0xd2: /*Write keyboard output buffer*/
                        keyboard_at.want60 = 1;
                        break;
                        
                        case 0xd3: /*Write mouse output buffer*/
                        keyboard_at.want60 = 1;
                        break;
                        
                        case 0xd4: /*Write to mouse*/
                        keyboard_at.want60 = 1;
                        break;
                        
                        case 0xe0: /*Read test inputs*/
                        keyboard_at_adddata(0x00);
                        break;
                        
                        case 0xef: /*??? - sent by AMI486*/
                        break;

			case 0xf0 ... 0xff:
			if (!(val & 1))
			{
				/* Pin 0 selected. */
	                        softresetx86(); /*Pulse reset!*/
				cpu_set_edx();
			}
			break;

#if 0
                        case 0xfe: /*Pulse output port - pin 0 selected - x86 reset*/
                        softresetx86(); /*Pulse reset!*/
			cpu_set_edx();
                        break;
                                                
                        case 0xff: /*Pulse output port - but no pins selected - sent by MegaPC BIOS*/
                        break;
#endif
                                
                        default:
                        pclog("Bad AT keyboard controller command %02X\n", val);
//                        dumpregs();
//                        exit(-1);
                }
        }
}

uint8_t keyboard_at_read(uint16_t port, void *priv)
{
        uint8_t temp = 0xff;
        cycles -= 4;
//        if (port != 0x61) pclog("keyboard_at : read %04X ", port);
        switch (port)
        {
                case 0x60:
                temp = keyboard_at.out;
                keyboard_at.status &= ~(STAT_OFULL/* | STAT_MFULL*/);
                picintc(keyboard_at.last_irq);
		if (PCI)
		{
			/* The PIIX/PIIX3 datasheet mandates that both of these interrupts are cleared on any read of port 0x60. */
	                picintc(1 << 1);
	                picintc(1 << 12);
		}
                keyboard_at.last_irq = 0;
                break;

                case 0x61:                
                if (ppispeakon) return (ppi.pb&~0xC0)|0x20;
                return ppi.pb&~0xe0;
                
                case 0x64:
                temp = (keyboard_at.status & 0xFB) | (mode & CCB_SYSTEM);
		/* if (mode & CCB_IGNORELOCK) */  temp |= STAT_LOCK;
                keyboard_at.status &= ~(STAT_RTIMEOUT/* | STAT_TTIMEOUT*/);
                break;
        }
//        if (port != 0x61) pclog("%02X  %08X\n", temp, rammask);
        return temp;
}

void keyboard_at_reset()
{
        keyboard_at.initialised = 0;
        keyboard_at.status = STAT_LOCK | STAT_CD;
        keyboard_at.mem[0] = 0x11;
	mode = 0x02 | dtrans;
	first_write = 1;
        keyboard_at.wantirq = 0;
        keyboard_at.output_port = 0xcf;
        keyboard_at.input_port = 0xb0;
        keyboard_at.out_new = -1;
        keyboard_at.last_irq = 0;
        
        keyboard_at.key_wantdata = 0;
        
        keyboard_scan = 1;

	sc_or = 0;

	memset(set3_flags, 0, 272);
}

void keyboard_at_init()
{
        //return;
        io_sethandler(0x0060, 0x0005, keyboard_at_read, NULL, NULL, keyboard_at_write, NULL, NULL,  NULL);
        keyboard_at_reset();
        keyboard_send = keyboard_at_adddata_keyboard;
        keyboard_poll = keyboard_at_poll;
        keyboard_at.mouse_write = NULL;
        keyboard_at.mouse_p = NULL;
	dtrans = 0;
        
        timer_add(keyboard_at_poll, &keybsenddelay, TIMER_ALWAYS_ENABLED,  NULL);
}

void keyboard_at_set_mouse(void (*mouse_write)(uint8_t val, void *p), void *p)
{
        keyboard_at.mouse_write = mouse_write;
        keyboard_at.mouse_p = p;
}
