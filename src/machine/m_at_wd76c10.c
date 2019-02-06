/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../device.h"
#include "../io.h"
#include "../keyboard.h"
#include "../mem.h"
#include "../serial.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"
#include "../video/vid_paradise.h"
#include "machine.h"


static uint16_t wd76c10_0092;
static uint16_t wd76c10_2072;
static uint16_t wd76c10_2872;
static uint16_t wd76c10_5872;

static serial_t *wd76c10_uart[2];


static fdc_t *wd76c10_fdc;


static uint16_t
wd76c10_read(uint16_t port, void *priv)
{
        switch (port)
        {
                case 0x0092:
                return wd76c10_0092;
                
                case 0x2072:
                return wd76c10_2072;

                case 0x2872:
                return wd76c10_2872;
                
                case 0x5872:
                return wd76c10_5872;
        }
        return 0;
}


static void
wd76c10_write(uint16_t port, uint16_t val, void *priv)
{
        switch (port)
        {
                case 0x0092:
                wd76c10_0092 = val;
                        
                mem_a20_alt = val & 2;
                mem_a20_recalc();
                break;
                
                case 0x2072:
                wd76c10_2072 = val;
                
                serial_remove(wd76c10_uart[0]);
                if (!(val & 0x10))
                {
                        switch ((val >> 5) & 7)
                        {
                                case 1: serial_setup(wd76c10_uart[0], 0x3f8, 4); break;
                                case 2: serial_setup(wd76c10_uart[0], 0x2f8, 4); break;
                                case 3: serial_setup(wd76c10_uart[0], 0x3e8, 4); break;
                                case 4: serial_setup(wd76c10_uart[0], 0x2e8, 4); break;
                                default: break;
                        }
                }
                serial_remove(wd76c10_uart[1]);
                if (!(val & 0x01))
                {
                        switch ((val >> 1) & 7)
                        {
                                case 1: serial_setup(wd76c10_uart[1], 0x3f8, 3); break;
                                case 2: serial_setup(wd76c10_uart[1], 0x2f8, 3); break;
                                case 3: serial_setup(wd76c10_uart[1], 0x3e8, 3); break;
                                case 4: serial_setup(wd76c10_uart[1], 0x2e8, 3); break;
                                default: break;
                        }
                }
                break;

                case 0x2872:
                wd76c10_2872 = val;
                
                fdc_remove(wd76c10_fdc);
                if (!(val & 1))
                   fdc_set_base(wd76c10_fdc, 0x03f0);
                break;
                
                case 0x5872:
                wd76c10_5872 = val;
                break;
        }
}


static uint8_t
wd76c10_readb(uint16_t port, void *priv)
{
        if (port & 1)
           return wd76c10_read(port & ~1, priv) >> 8;
        return wd76c10_read(port, priv) & 0xff;
}


static void
wd76c10_writeb(uint16_t port, uint8_t val, void *priv)
{
        uint16_t temp = wd76c10_read(port, priv);
        if (port & 1)
           wd76c10_write(port & ~1, (temp & 0x00ff) | (val << 8), priv);
        else
           wd76c10_write(port     , (temp & 0xff00) | val, priv);
}


static void wd76c10_init(void)
{
        io_sethandler(0x0092, 2,
		      wd76c10_readb, wd76c10_read, NULL,
		      wd76c10_writeb, wd76c10_write, NULL, NULL);
        io_sethandler(0x2072, 2,
		      wd76c10_readb, wd76c10_read, NULL,
		      wd76c10_writeb, wd76c10_write, NULL, NULL);
        io_sethandler(0x2872, 2,
		      wd76c10_readb, wd76c10_read, NULL,
		      wd76c10_writeb, wd76c10_write, NULL, NULL);
        io_sethandler(0x5872, 2,
		      wd76c10_readb, wd76c10_read, NULL,
		      wd76c10_writeb, wd76c10_write, NULL, NULL);
}


void
machine_at_wd76c10_init(const machine_t *model)
{
        machine_at_common_ide_init(model);

	device_add(&keyboard_ps2_quadtel_device);
	wd76c10_fdc = device_add(&fdc_at_device);
	wd76c10_uart[0] = device_add_inst(&i8250_device, 1);
	wd76c10_uart[1] = device_add_inst(&i8250_device, 2);

        wd76c10_init();

	device_add(&paradise_wd90c11_megapc_device);
}
