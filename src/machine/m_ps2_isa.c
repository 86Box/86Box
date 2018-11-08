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
#include "../keyboard.h"
#include "../lpt.h"
#include "../serial.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"
#include "../video/vid_vga.h"
#include "machine.h"


static uint8_t ps2_94, ps2_102, ps2_103, ps2_104, ps2_105, ps2_190;


static struct
{
        uint8_t status, int_status;
        uint8_t attention, ctrl;
} ps2_hd;


static uint8_t ps2_read(uint16_t port, void *p)
{
        uint8_t temp;

        switch (port)
        {
                case 0x91:
                return 0;
                case 0x94:
                return ps2_94;
                case 0x102:
                return ps2_102 | 8;
                case 0x103:
                return ps2_103;
                case 0x104:
                return ps2_104;
                case 0x105:
                return ps2_105;
                case 0x190:
                return ps2_190;
                
                case 0x322:
                temp = ps2_hd.status;
                break;
                case 0x324:
                temp = ps2_hd.int_status;
                ps2_hd.int_status &= ~0x02;
                break;
                
                default:
                temp = 0xff;
                break;
        }
        
        return temp;
}

static void ps2_write(uint16_t port, uint8_t val, void *p)
{
	serial_t *uart = machine_get_serial(0);

        switch (port)
        {
                case 0x94:
                ps2_94 = val;
                break;
                case 0x102:
                lpt1_remove();
                if (val & 0x04)
                        serial_setup(uart, SERIAL1_ADDR, SERIAL1_IRQ);
                else
                        serial_remove(uart);
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
                ps2_102 = val;
                break;
                case 0x103:
                ps2_103 = val;
                break;
                case 0x104:
                ps2_104 = val;
                break;
                case 0x105:
                ps2_105 = val;
                break;
                case 0x190:
                ps2_190 = val;
                break;
                
                case 0x322:
                ps2_hd.ctrl = val;
                if (val & 0x80)
                        ps2_hd.status |= 0x02;
                break;
                case 0x324:
                ps2_hd.attention = val & 0xf0;
                if (ps2_hd.attention)
                        ps2_hd.status = 0x14;
                break;
        }
}


static void ps2board_init(void)
{
        io_sethandler(0x0091, 0x0001, ps2_read, NULL, NULL, ps2_write, NULL, NULL, NULL);
        io_sethandler(0x0094, 0x0001, ps2_read, NULL, NULL, ps2_write, NULL, NULL, NULL);
        io_sethandler(0x0102, 0x0004, ps2_read, NULL, NULL, ps2_write, NULL, NULL, NULL);
        io_sethandler(0x0190, 0x0001, ps2_read, NULL, NULL, ps2_write, NULL, NULL, NULL);
        io_sethandler(0x0320, 0x0001, ps2_read, NULL, NULL, ps2_write, NULL, NULL, NULL);
        io_sethandler(0x0322, 0x0001, ps2_read, NULL, NULL, ps2_write, NULL, NULL, NULL);
        io_sethandler(0x0324, 0x0001, ps2_read, NULL, NULL, ps2_write, NULL, NULL, NULL);
        
	port_92_reset();

        port_92_add();

        ps2_190 = 0;

        lpt1_init(0x3bc);
        
        memset(&ps2_hd, 0, sizeof(ps2_hd));
}


void
machine_ps2_m30_286_init(const machine_t *model)
{
        machine_common_init(model);
	device_add(&fdc_at_ps1_device);

        pit_set_out_func(&pit, 1, pit_refresh_timer_at);
        dma16_init();
	device_add(&keyboard_ps2_device);
	device_add(&ps_nvr_device);
        pic2_init();
        ps2board_init();
	device_add(&ps1vga_device);
}
