/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../cpu/cpu.h"
#include "../io.h"
#include "../dma.h"
#include "../pic.h"
#include "../pit.h"
#include "../mem.h"
#include "../rom.h"
#include "../device.h"
#include "../nvr.h"
#include "../game/gameport.h"
#include "../lpt.h"
#include "../serial.h"
#include "../keyboard.h"
#include "../disk/hdc.h"
#include "../disk/hdc_ide.h"
#include "../floppy/floppy.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"
#include "../sound/snd_ps1.h"
#include "machine.h"


static rom_t ps1_high_rom;
static uint8_t ps1_92, ps1_94, ps1_102, ps1_103, ps1_104, ps1_105, ps1_190;
static int ps1_e0_addr;
static uint8_t ps1_e0_regs[256];


static struct
{
        uint8_t status, int_status;
        uint8_t attention, ctrl;
} ps1_hd;


static uint8_t ps1_read(uint16_t port, void *p)
{
        uint8_t temp;

        switch (port)
        {
                case 0x91:
                return 0;
                case 0x92:
                return ps1_92;
                case 0x94:
                return ps1_94;
                case 0x102:
                return ps1_102 | 8;
                case 0x103:
                return ps1_103;
                case 0x104:
                return ps1_104;
                case 0x105:
                return ps1_105;
                case 0x190:
                return ps1_190;
                
                case 0x322:
                temp = ps1_hd.status;
                break;
                case 0x324:
                temp = ps1_hd.int_status;
                ps1_hd.int_status &= ~0x02;
                break;
                
                default:
                temp = 0xff;
                break;
        }
        
        return temp;
}


static void ps1_write(uint16_t port, uint8_t val, void *p)
{
        switch (port)
        {
                case 0x0092:
                ps1_92 = val;    
                mem_a20_alt = val & 2;
                mem_a20_recalc();
                break;
                case 0x94:
                ps1_94 = val;
                break;
                case 0x102:
                lpt1_remove();
                if (val & 0x04)
                        serial_setup(1, SERIAL1_ADDR, SERIAL1_IRQ);
                else
                        serial_remove(1);
                if (val & 0x10)
                {
                        switch ((val >> 5) & 3)
                        {
                                case 0:
                                lpt1_init(0x3bc);
                                break;
                                case 1:
                                lpt1_init(0x378);
                                break;
                                case 2:
                                lpt1_init(0x278);
                                break;
                        }
                }
                ps1_102 = val;
                break;
                case 0x103:
                ps1_103 = val;
                break;
                case 0x104:
                ps1_104 = val;
                break;
                case 0x105:
                ps1_105 = val;
                break;
                case 0x190:
                ps1_190 = val;
                break;
                
                case 0x322:
                ps1_hd.ctrl = val;
                if (val & 0x80)
                        ps1_hd.status |= 0x02;
                break;
                case 0x324:
                ps1_hd.attention = val & 0xf0;
                if (ps1_hd.attention)
                        ps1_hd.status = 0x14;
                break;
        }
}


void ps1mb_init(void)
{
        io_sethandler(0x0091, 0x0001, ps1_read, NULL, NULL, ps1_write, NULL, NULL, NULL);
        io_sethandler(0x0092, 0x0001, ps1_read, NULL, NULL, ps1_write, NULL, NULL, NULL);
        io_sethandler(0x0094, 0x0001, ps1_read, NULL, NULL, ps1_write, NULL, NULL, NULL);
        io_sethandler(0x0102, 0x0004, ps1_read, NULL, NULL, ps1_write, NULL, NULL, NULL);
        io_sethandler(0x0190, 0x0001, ps1_read, NULL, NULL, ps1_write, NULL, NULL, NULL);
        io_sethandler(0x0320, 0x0001, ps1_read, NULL, NULL, ps1_write, NULL, NULL, NULL);
        io_sethandler(0x0322, 0x0001, ps1_read, NULL, NULL, ps1_write, NULL, NULL, NULL);
        io_sethandler(0x0324, 0x0001, ps1_read, NULL, NULL, ps1_write, NULL, NULL, NULL);

#if 0
	if (!enable_xtide)
	{
	        rom_init(&ps1_high_rom,
                                L"roms/machines/ibmps1es/f80000_shell.bin",
                                0xf80000,
                                0x80000,
                                0x7ffff,
                                0,
                                MEM_MAPPING_EXTERNAL);
	}
#endif
        ps1_190 = 0;
        
        lpt1_remove();
        lpt2_remove();
        lpt1_init(0x3bc);
        
        serial_remove(1);
        serial_remove(2);
        
        memset(&ps1_hd, 0, sizeof(ps1_hd));
}

/*PS/1 Model 2121.

  This is similar to the model 2011 but some of the functionality has moved to a
  chip at ports 0xe0 (index)/0xe1 (data). The only functions I have identified
  are enables for the first 512kb and next 128kb of RAM, in bits 0 of registers
  0 and 1 respectively.
  
  Port 0x105 has bit 7 forced high. Without this 128kb of memory will be missed
  by the BIOS on cold boots.
  
  The reserved 384kb is remapped to the top of extended memory. If this is not
  done then you get an error on startup.
*/
static uint8_t ps1_m2121_read(uint16_t port, void *p)
{
        uint8_t temp;

        switch (port)
        {
                case 0x91:
                return 0;
                case 0x92:
                return ps1_92;
                case 0x94:
                return ps1_94;
                case 0xe1:
                return ps1_e0_regs[ps1_e0_addr];
                case 0x102:
                return ps1_102;
                case 0x103:
                return ps1_103;
                case 0x104:
                return ps1_104;
                case 0x105:
                return ps1_105 | 0x80;
                case 0x190:
                return ps1_190;
                
                default:
                temp = 0xff;
                break;
        }
        
        return temp;
}

static void ps1_m2121_recalc_memory(void)
{
        /*Enable first 512kb*/
        mem_set_mem_state(0x00000, 0x80000, (ps1_e0_regs[0] & 0x01) ? (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) : (MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL));
        /*Enable 512-640kb*/
        mem_set_mem_state(0x80000, 0x20000, (ps1_e0_regs[1] & 0x01) ? (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) : (MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL));
}

void ps1_m2121_write(uint16_t port, uint8_t val, void *p)
{
        switch (port)
        {
                case 0x0092:
                if (val & 1)
		{
                        softresetx86();
			cpu_set_edx();
		}
                ps1_92 = val & ~1;
                mem_a20_alt = val & 2;
                mem_a20_recalc();
                break;
                case 0x94:
                ps1_94 = val;
                break;
                case 0xe0:
                ps1_e0_addr = val;
                break;
                case 0xe1:
                ps1_e0_regs[ps1_e0_addr] = val;
                ps1_m2121_recalc_memory();
                break;
                case 0x102:
                lpt1_remove();
                if (val & 0x04)
                        serial_setup(1, SERIAL1_ADDR, SERIAL1_IRQ);
                else
                        serial_remove(1);
                if (val & 0x10)
                {
                        switch ((val >> 5) & 3)
                        {
                                case 0:
                                lpt1_init(0x3bc);
                                break;
                                case 1:
                                lpt1_init(0x378);
                                break;
                                case 2:
                                lpt1_init(0x278);
                                break;
                        }
                }
                ps1_102 = val;
                break;
                case 0x103:
                ps1_103 = val;
                break;
                case 0x104:
                ps1_104 = val;
                break;
                case 0x105:
                ps1_105 = val;
                break;
                case 0x190:
                ps1_190 = val;
                break;
        }
}

static void ps1mb_m2121_init(void)
{
        io_sethandler(0x0091, 0x0001, ps1_m2121_read, NULL, NULL, ps1_m2121_write, NULL, NULL, NULL);
        io_sethandler(0x0092, 0x0001, ps1_m2121_read, NULL, NULL, ps1_m2121_write, NULL, NULL, NULL);
        io_sethandler(0x0094, 0x0001, ps1_m2121_read, NULL, NULL, ps1_m2121_write, NULL, NULL, NULL);
        io_sethandler(0x00e0, 0x0002, ps1_m2121_read, NULL, NULL, ps1_m2121_write, NULL, NULL, NULL);
        io_sethandler(0x0102, 0x0004, ps1_m2121_read, NULL, NULL, ps1_m2121_write, NULL, NULL, NULL);
        io_sethandler(0x0190, 0x0001, ps1_m2121_read, NULL, NULL, ps1_m2121_write, NULL, NULL, NULL);

        rom_init(&ps1_high_rom,
                                L"roms/machines/ibmps1_2121/fc0000_shell.bin",
                                0xfc0000,
                                0x40000,
                                0x3ffff,
                                0,
                                MEM_MAPPING_EXTERNAL);
	ps1_92 = 0;
        ps1_190 = 0;
        
        lpt1_init(0x3bc);

        mem_remap_top_384k();
}

static void ps1mb_m2133_init(void)
{
        io_sethandler(0x0091, 0x0001, ps1_m2121_read, NULL, NULL, ps1_m2121_write, NULL, NULL, NULL);
        io_sethandler(0x0092, 0x0001, ps1_m2121_read, NULL, NULL, ps1_m2121_write, NULL, NULL, NULL);
        io_sethandler(0x0094, 0x0001, ps1_m2121_read, NULL, NULL, ps1_m2121_write, NULL, NULL, NULL);
        io_sethandler(0x0102, 0x0004, ps1_m2121_read, NULL, NULL, ps1_m2121_write, NULL, NULL, NULL);
        io_sethandler(0x0190, 0x0001, ps1_m2121_read, NULL, NULL, ps1_m2121_write, NULL, NULL, NULL);
 
	ps1_92 = 0;
        ps1_190 = 0;
       
        lpt1_init(0x3bc);

        mem_remap_top_384k();
}


static void
machine_ps1_common_init(machine_t *model)
{
        machine_common_init(model);

        pit_set_out_func(&pit, 1, pit_refresh_timer_at);
        dma16_init();
        if (romset != ROM_IBMPS1_2011)
	{
		ide_init();
	}
	device_add(&keyboard_at_device);
        nvr_at_init(8);
        pic2_init();
	if (romset != ROM_IBMPS1_2133)
	{			
		fdc_set_dskchg_activelow();
		device_add(&ps1_audio_device);
	}

        /*PS/1 audio uses ports 200h and 202-207h, so only initialise gameport on 201h*/
        if (joystick_type != 7)
		device_add(&gameport_201_device);
}


void
machine_ps1_m2011_init(machine_t *model)
{
        machine_ps1_common_init(model);

        ps1mb_init();
        mem_remap_top_384k();
}


void
machine_ps1_m2121_init(machine_t *model)
{
        machine_ps1_common_init(model);

        ps1mb_m2121_init();
        fdc_set_ps1();
}


void
machine_ps1_m2133_init(machine_t *model)
{
        machine_ps1_common_init(model);

        ps1mb_m2133_init();
}
