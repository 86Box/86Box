/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#include "ibm.h"

#include "disc.h"
#include "fdc.h"
#include "fdd.h"
#include "ide.h"
#include "io.h"
#include "lpt.h"
#include "serial.h"
#include "fdc37c665.h"

static uint8_t fdc37c665_lock[2];
static int fdc37c665_curreg;
static uint8_t fdc37c665_regs[16];
static int com3_addr, com4_addr;

static void write_lock(uint8_t val)
{
        if (val == 0x55 && fdc37c665_lock[1] == 0x55)
                fdc_3f1_enable(0);
        if (fdc37c665_lock[0] == 0x55 && fdc37c665_lock[1] == 0x55 && val != 0x55)
                fdc_3f1_enable(1);

        fdc37c665_lock[0] = fdc37c665_lock[1];
        fdc37c665_lock[1] = val;
}

static void ide_handler()
{
	uint16_t or_value = 0;
	if (romset == ROM_440FX)
	{
		return;
	}
	ide_pri_disable();
	if (fdc37c665_regs[0] & 1)
	{
		if (fdc37c665_regs[5] & 2)
		{
			or_value = 0;
		}
		else
		{
			or_value = 0x800;
		}
		ide_set_base(0, 0x170 | or_value);
		ide_set_side(0, 0x376 | or_value);
		ide_pri_enable_ex();
	}
	else
	{
		pclog("Disabling IDE...\n");
	}
}

static void set_com34_addr()
{
	switch (fdc37c665_regs[1] & 0x60)
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

void set_serial1_addr()
{
	if (fdc37c665_regs[2] & 4)
	{
		switch (fdc37c665_regs[2] & 3)
		{
			case 0:
				serial1_set(0x3f8, 4);
				break;

			case 1:
				serial1_set(0x2f8, 3);
				break;

			case 2:
				serial1_set(com3_addr, 4);
				break;

			case 3:
				serial1_set(com4_addr, 3);
				break;
		}
	}
}

void set_serial2_addr()
{
	if (fdc37c665_regs[2] & 0x40)
	{
		switch (fdc37c665_regs[2] & 0x30)
		{
			case 0:
				serial2_set(0x3f8, 4);
				break;

			case 1:
				serial2_set(0x2f8, 3);
				break;

			case 2:
				serial2_set(com3_addr, 4);
				break;

			case 3:
				serial2_set(com4_addr, 3);
				break;
		}
	}
}

static void lpt1_handler()
{
	lpt1_remove();
	switch (fdc37c665_regs[1] & 3)
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

void fdc37c665_write(uint16_t port, uint8_t val, void *priv)
{
	uint8_t valxor = 0;
        if (fdc37c665_lock[0] == 0x55 && fdc37c665_lock[1] == 0x55)
        {
                if (port == 0x3f0)
                {
                        if (val == 0xaa)
                                write_lock(val);
                        else
				if (fdc37c665_curreg != 0)
				{
	                                fdc37c665_curreg = val & 0xf;
				}
				else
				{
					/* Hardcode the IDE to AT type. */
	                                fdc37c665_curreg = (val & 0xf) | 2;
				}
                }
                else
                {
			valxor = val ^ fdc37c665_regs[fdc37c665_curreg];
                        fdc37c665_regs[fdc37c665_curreg] = val;
                        
			switch(fdc37c665_curreg)
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
		                                serial1_remove();
						set_com34_addr();
						set_serial1_addr();
						set_serial2_addr();
					}
					break;
				case 2:
					if (valxor & 7)
					{
		                                serial1_remove();
						set_serial1_addr();
					}
					if (valxor & 0x70)
					{
		                                serial2_remove();
						set_serial2_addr();
					}
					break;
				case 3:
					if (valxor & 2)
					{
						fdc_update_enh_mode((fdc37c665_regs[3] & 2) ? 1 : 0);
					}
					break;
				case 5:
					if (valxor & 2)
					{
						ide_handler();
					}
					if (valxor & 0x18)
					{
						fdc_update_densel_force((fdc37c665_regs[5] & 0x18) >> 3);
					}
					if (valxor & 0x20)
					{
						fdd_swap = ((fdc37c665_regs[5] & 0x20) >> 5);
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

uint8_t fdc37c665_read(uint16_t port, void *priv)
{
        if (fdc37c665_lock[0] == 0x55 && fdc37c665_lock[1] == 0x55)
        {
                if (port == 0x3f1)
                        return fdc37c665_regs[fdc37c665_curreg];
        }
        return 0xff;
}

void fdc37c665_reset(void)
{
	com3_addr = 0x338;
	com4_addr = 0x238;

	fdc_remove();
	fdc_add_for_superio();

        fdc_update_is_nsc(0);
        
	serial1_remove();
	serial1_set(0x3f8, 4);

	serial2_remove();
	serial2_set(0x2f8, 3);

	lpt2_remove();

	lpt1_remove();
	lpt1_init(0x378);
        
	memset(fdc37c665_lock, 0, 2);
	memset(fdc37c665_regs, 0, 16);
        fdc37c665_regs[0x0] = 0x3b;
        fdc37c665_regs[0x1] = 0x9f;
        fdc37c665_regs[0x2] = 0xdc;
        fdc37c665_regs[0x3] = 0x78;
        fdc37c665_regs[0x6] = 0xff;
        fdc37c665_regs[0xd] = 0x65;
        fdc37c665_regs[0xe] = 0x01;

	fdc_update_densel_polarity(1);
	fdc_update_densel_force(0);
	fdd_swap = 0;
}

void fdc37c665_init()
{
        io_sethandler(0x03f0, 0x0002, fdc37c665_read, NULL, NULL, fdc37c665_write, NULL, NULL,  NULL);

	fdc37c665_reset();

	pci_reset_handler.super_io_reset = fdc37c665_reset;
}
