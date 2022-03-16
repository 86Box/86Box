#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/dma.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/nvr.h>
#include <86box/keyboard.h>
#include <86box/lpt.h>
#include <86box/port_6x.h>
#include <86box/port_92.h>
#include <86box/serial.h>
#include <86box/hdc.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/video.h>
#include <86box/machine.h>


static uint8_t ps2_91, ps2_94, ps2_102, ps2_103, ps2_104, ps2_105, ps2_190;
static serial_t *ps2_uart;


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
		temp = ps2_91;
		ps2_91 = 0;
                return temp;
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

#ifdef FIXME
                case 0x322:
                temp = ps2_hd.status;
                break;
                case 0x324:
                temp = ps2_hd.int_status;
                ps2_hd.int_status &= ~0x02;
                break;
#endif

                default:
                temp = 0xff;
                break;
        }

        return temp;
}

static void ps2_write(uint16_t port, uint8_t val, void *p)
{
        switch (port)
        {
                case 0x94:
                ps2_94 = val;
                break;
                case 0x102:
				if (!(ps2_94 & 0x80)) {
					lpt1_remove();
					serial_remove(ps2_uart);
					if (val & 0x04) {
						if (val & 0x08)
							serial_setup(ps2_uart, COM1_ADDR, COM1_IRQ);
						else
							serial_setup(ps2_uart, COM2_ADDR, COM2_IRQ);
					}
					if (val & 0x10) {
						switch ((val >> 5) & 3)
						{
							case 0:
							lpt1_init(LPT_MDA_ADDR);
							break;
							case 1:
							lpt1_init(LPT1_ADDR);
							break;
							case 2:
							lpt1_init(LPT2_ADDR);
							break;
						}
					}
					ps2_102 = val;
				}
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

#ifdef FIXME
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
#endif
        }
}


static void ps2board_init(void)
{
        io_sethandler(0x0091, 0x0001, ps2_read, NULL, NULL, ps2_write, NULL, NULL, NULL);
        io_sethandler(0x0094, 0x0001, ps2_read, NULL, NULL, ps2_write, NULL, NULL, NULL);
        io_sethandler(0x0102, 0x0004, ps2_read, NULL, NULL, ps2_write, NULL, NULL, NULL);
        io_sethandler(0x0190, 0x0001, ps2_read, NULL, NULL, ps2_write, NULL, NULL, NULL);
#ifdef FIXME
        io_sethandler(0x0320, 0x0001, ps2_read, NULL, NULL, ps2_write, NULL, NULL, NULL);
        io_sethandler(0x0322, 0x0001, ps2_read, NULL, NULL, ps2_write, NULL, NULL, NULL);
        io_sethandler(0x0324, 0x0001, ps2_read, NULL, NULL, ps2_write, NULL, NULL, NULL);
#endif

	device_add(&port_92_device);

        ps2_190 = 0;

	ps2_uart = device_add_inst(&ns16450_device, 1);

        lpt1_init(LPT_MDA_ADDR);

        memset(&ps2_hd, 0, sizeof(ps2_hd));
}


int
machine_ps2_m30_286_init(const machine_t *model)
{
	void *priv;

	int ret;

	ret = bios_load_linear("roms/machines/ibmps2_m30_286/33f5381a.bin",
			       0x000e0000, 131072, 0);

	if (bios_only || !ret)
		return ret;

        machine_common_init(model);

	mem_remap_top(384);

	device_add(&fdc_at_ps1_device);

	refresh_at_enable = 1;
        pit_ctr_set_out_func(&pit->counters[1], pit_refresh_timer_at);
        dma16_init();
	device_add(&keyboard_ps2_device);
	device_add(&port_6x_ps2_device);
	device_add(&ps_nvr_device);
        pic2_init();
        ps2board_init();
	device_add(&ps1vga_device);

 	/* Enable the builtin HDC. */
	if (hdc_current == 1) {
		priv = device_add(&ps1_hdc_device);

		ps1_hdc_inform(priv, &ps2_91);
	}

	return ret;
}
