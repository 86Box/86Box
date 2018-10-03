/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the SMC FDC37C663 and FDC37C665 Super
 *		I/O Chips.
 *
 * Version:	@(#)sio_fdc37c66x.c	1.0.11	2018/04/04
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "86box.h"
#include "io.h"
#include "device.h"
#include "pci.h"
#include "lpt.h"
#include "serial.h"
#include "disk/hdc.h"
#include "disk/hdc_ide.h"
#include "floppy/fdd.h"
#include "floppy/fdc.h"
#include "sio.h"


static uint8_t fdc37c66x_lock[2];
static int fdc37c66x_curreg;
static uint8_t fdc37c66x_regs[16];
static int com3_addr, com4_addr;
static fdc_t *fdc37c66x_fdc;


static void write_lock(uint8_t val)
{
        if (val == 0x55 && fdc37c66x_lock[1] == 0x55)
                fdc_3f1_enable(fdc37c66x_fdc, 0);
        if (fdc37c66x_lock[0] == 0x55 && fdc37c66x_lock[1] == 0x55 && val != 0x55)
                fdc_3f1_enable(fdc37c66x_fdc, 1);

        fdc37c66x_lock[0] = fdc37c66x_lock[1];
        fdc37c66x_lock[1] = val;
}

static void ide_handler()
{
#if 0
	uint16_t or_value = 0;
	if ((romset == ROM_440FX) || (romset == ROM_R418) || (romset == ROM_MB500N))
	{
		return;
	}
	ide_pri_disable();
	if (fdc37c66x_regs[0] & 1)
	{
		if (fdc37c66x_regs[5] & 2)
		{
			or_value = 0;
		}
		else
		{
			or_value = 0x800;
		}
		ide_set_base(0, 0x170 | or_value);
		ide_set_side(0, 0x376 | or_value);
		ide_pri_enable();
	}
#endif
}

static void set_com34_addr()
{
	switch (fdc37c66x_regs[1] & 0x60)
	{
		case 0x00:
			com3_addr = 0x338;
			com4_addr = 0x238;
			break;
		case 0x20:
			com3_addr = 0x3e8;
			com4_addr = 0x2e8;
			break;
		case 0x40:
			com3_addr = 0x3e8;
			com4_addr = 0x2e0;
			break;
		case 0x60:
			com3_addr = 0x220;
			com4_addr = 0x228;
			break;
	}
}

static void set_serial1_addr()
{
	if (fdc37c66x_regs[2] & 4)
	{
		switch (fdc37c66x_regs[2] & 3)
		{
			case 0:
				serial_setup(1, SERIAL1_ADDR, SERIAL1_IRQ);
				break;

			case 1:
				serial_setup(1, SERIAL2_ADDR, SERIAL2_IRQ);
				break;

			case 2:
				serial_setup(1, com3_addr, 4);
				break;

			case 3:
				serial_setup(1, com4_addr, 3);
				break;
		}
	}
}

static void set_serial2_addr()
{
	if (fdc37c66x_regs[2] & 0x40)
	{
		switch (fdc37c66x_regs[2] & 0x30)
		{
			case 0:
				serial_setup(2, SERIAL1_ADDR, SERIAL1_IRQ);
				break;

			case 1:
				serial_setup(2, SERIAL2_ADDR, SERIAL2_IRQ);
				break;

			case 2:
				serial_setup(2, com3_addr, 4);
				break;

			case 3:
				serial_setup(2, com4_addr, 3);
				break;
		}
	}
}

static void lpt1_handler()
{
	lpt1_remove();
	switch (fdc37c66x_regs[1] & 3)
	{
		case 1:
			lpt1_init(0x3bc);
			break;
		case 2:
			lpt1_init(0x378);
			break;
		case 3:
			lpt1_init(0x278);
			break;
	}
}

static void fdc37c66x_write(uint16_t port, uint8_t val, void *priv)
{
	uint8_t valxor = 0;
        if (fdc37c66x_lock[0] == 0x55 && fdc37c66x_lock[1] == 0x55)
        {
                if (port == 0x3f0)
                {
                        if (val == 0xaa)
                                write_lock(val);
                        else
				fdc37c66x_curreg = val;
#if 0
				if (fdc37c66x_curreg != 0)
				{
	                                fdc37c66x_curreg = val & 0xf;
				}
				else
				{
					/* Hardcode the IDE to AT type. */
	                                fdc37c66x_curreg = (val & 0xf) | 2;
				}
#endif
                }
                else
                {
			if (fdc37c66x_curreg > 15)
				return;

			valxor = val ^ fdc37c66x_regs[fdc37c66x_curreg];
                        fdc37c66x_regs[fdc37c66x_curreg] = val;
                        
			switch(fdc37c66x_curreg)
			{
				case 0:
					if (valxor & 1)
					{
						ide_handler();
					}
					break;
				case 1:
					if (valxor & 3)
					{
						lpt1_handler();
					}
					if (valxor & 0x60)
					{
		                                serial_remove(1);
						set_com34_addr();
						set_serial1_addr();
						set_serial2_addr();
					}
					break;
				case 2:
					if (valxor & 7)
					{
		                                serial_remove(1);
						set_serial1_addr();
					}
					if (valxor & 0x70)
					{
		                                serial_remove(2);
						set_serial2_addr();
					}
					break;
				case 3:
					if (valxor & 2)
					{
						fdc_update_enh_mode(fdc37c66x_fdc, (fdc37c66x_regs[3] & 2) ? 1 : 0);
					}
					break;
				case 5:
					if (valxor & 2)
					{
						ide_handler();
					}
					if (valxor & 0x18)
					{
						fdc_update_densel_force(fdc37c66x_fdc, (fdc37c66x_regs[5] & 0x18) >> 3);
					}
					if (valxor & 0x20)
					{
						fdc_set_swap(fdc37c66x_fdc, (fdc37c66x_regs[5] & 0x20) >> 5);
					}
					break;
                        }
                }
        }
        else
        {
                if (port == 0x3f0)
                        write_lock(val);
        }
}

static uint8_t fdc37c66x_read(uint16_t port, void *priv)
{
        if (fdc37c66x_lock[0] == 0x55 && fdc37c66x_lock[1] == 0x55)
        {
                if (port == 0x3f1)
                        return fdc37c66x_regs[fdc37c66x_curreg];
        }
        return 0xff;
}

static void fdc37c66x_reset(void)
{
	com3_addr = 0x338;
	com4_addr = 0x238;

	serial_remove(1);
	serial_setup(1, SERIAL1_ADDR, SERIAL1_IRQ);

	serial_remove(2);
	serial_setup(2, SERIAL2_ADDR, SERIAL2_IRQ);

	lpt2_remove();

	lpt1_remove();
	lpt1_init(0x378);

	fdc_reset(fdc37c66x_fdc);
        
	memset(fdc37c66x_lock, 0, 2);
	memset(fdc37c66x_regs, 0, 16);
        fdc37c66x_regs[0x0] = 0x3a;
        fdc37c66x_regs[0x1] = 0x9f;
        fdc37c66x_regs[0x2] = 0xdc;
        fdc37c66x_regs[0x3] = 0x78;
        fdc37c66x_regs[0x6] = 0xff;
        fdc37c66x_regs[0xe] = 0x01;
}

static void fdc37c663_reset(void)
{
	fdc37c66x_reset();
        fdc37c66x_regs[0xd] = 0x63;
}

static void fdc37c665_reset(void)
{
	fdc37c66x_reset();
        fdc37c66x_regs[0xd] = 0x65;
}

void fdc37c663_init()
{
	fdc37c66x_fdc = device_add(&fdc_at_smc_device);

        io_sethandler(0x03f0, 0x0002, fdc37c66x_read, NULL, NULL, fdc37c66x_write, NULL, NULL,  NULL);

	fdc37c663_reset();
}

void fdc37c665_init()
{
	fdc37c66x_fdc = device_add(&fdc_at_smc_device);

        io_sethandler(0x03f0, 0x0002, fdc37c66x_read, NULL, NULL, fdc37c66x_write, NULL, NULL,  NULL);

	fdc37c665_reset();
}
