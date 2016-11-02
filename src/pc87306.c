/* Copyright holders: Tenshi
   see COPYING for more details
*/
/*
	National Semiconductors PC87306 Super I/O Chip
	Used by Intel Advanced/EV
*/

#include "ibm.h"

#include "fdc.h"
#include "fdd.h"
#include "io.h"
#include "lpt.h"
#include "mouse_serial.h"
#include "serial.h"
#include "pc87306.h"

static int pc87306_locked;
static int pc87306_curreg;
static uint8_t pc87306_regs[29];
static uint8_t pc87306_gpio[2] = {0xFF, 0xFF};
static uint8_t tries;
static uint16_t lpt_port;

void pc87306_gpio_remove();
void pc87306_gpio_init();

void pc87306_gpio_write(uint16_t port, uint8_t val, void *priv)
{
	pc87306_gpio[port & 1] = val;
}

uint8_t uart_int1()
{
	return ((pc87306_regs[0x1C] >> 2) & 1) ? 4 : 3;
}

uint8_t uart_int2()
{
	return ((pc87306_regs[0x1C] >> 6) & 1) ? 4 : 3;
}

uint8_t uart1_int()
{
	uint8_t temp;
	temp = ((pc87306_regs[1] >> 2) & 1) ? 3 : 4;	/* 0 = IRQ 4, 1 = IRQ 3 */
	return (pc87306_regs[0x1C] & 1) ? uart_int1() : temp;
}

uint8_t uart2_int()
{
	uint8_t temp;
	temp = ((pc87306_regs[1] >> 4) & 1) ? 3 : 4;	/* 0 = IRQ 4, 1 = IRQ 3 */
	return (pc87306_regs[0x1C] & 1) ? uart_int2() : temp;
}

void serial1_handler()
{
        int temp;
	temp = (pc87306_regs[1] >> 2) & 3;
	switch (temp)
	{
		case 0: serial1_set(0x3f8, uart1_int()); break;
		case 1: serial1_set(0x2f8, uart1_int()); break;
		case 2:
			switch ((pc87306_regs[1] >> 6) & 3)
			{
				case 0: serial1_set(0x3e8, uart1_int()); break;
				case 1: serial1_set(0x338, uart1_int()); break;
				case 2: serial1_set(0x2e8, uart1_int()); break;
				case 3: serial1_set(0x220, uart1_int()); break;
			}
			break;
		case 3:
			switch ((pc87306_regs[1] >> 6) & 3)
			{
				case 0: serial1_set(0x2e8, uart1_int()); break;
				case 1: serial1_set(0x238, uart1_int()); break;
				case 2: serial1_set(0x2e0, uart1_int()); break;
				case 3: serial1_set(0x228, uart1_int()); break;
			}
			break;
	}
}

void serial2_handler()
{
        int temp;
	temp = (pc87306_regs[1] >> 4) & 3;
	switch (temp)
	{
		case 0: serial2_set(0x3f8, uart2_int()); break;
		case 1: serial2_set(0x2f8, uart2_int()); break;
		case 2:
			switch ((pc87306_regs[1] >> 6) & 3)
			{
				case 0: serial2_set(0x3e8, uart2_int()); break;
				case 1: serial2_set(0x338, uart2_int()); break;
				case 2: serial2_set(0x2e8, uart2_int()); break;
				case 3: serial2_set(0x220, uart2_int()); break;
			}
			break;
		case 3:
			switch ((pc87306_regs[1] >> 6) & 3)
			{
				case 0: serial2_set(0x2e8, uart2_int()); break;
				case 1: serial2_set(0x238, uart2_int()); break;
				case 2: serial2_set(0x2e0, uart2_int()); break;
				case 3: serial2_set(0x228, uart2_int()); break;
			}
			break;
	}
}

void pc87306_write(uint16_t port, uint8_t val, void *priv)
{
	uint8_t index;
	index = (port & 1) ? 0 : 1;
        int temp;
	uint8_t valxor;
        // pclog("pc87306_write : port=%04x reg %02X = %02X locked=%i\n", port, pc87306_curreg, val, pc87306_locked);

	if (index)
	{
		pc87306_curreg = val;
		// pclog("Register set to: %02X\n", val);
		tries = 0;
		return;
	}
	else
	{
		if (tries)
		{
			if (pc87306_curreg <= 28)  valxor = val ^ pc87306_regs[pc87306_curreg];
			if (pc87306_curreg == 0xF)  pc87306_gpio_remove();
			if (pc87306_curreg <= 28)  pc87306_regs[pc87306_curreg] = val;
			// pclog("Register %02X set to: %02X\n", pc87306_regs[pc87306_curreg], val);
			tries = 0;
			if (pc87306_curreg <= 28)  goto process_value;
		}
		else
		{
			tries++;
			return;
		}
	}
	return;

process_value:
	switch(pc87306_curreg)
	{
		case 0:
			if (valxor & 1)
			{
				lpt1_remove();
				lpt2_remove();
			}
			if ((valxor & 1) && (val & 1))
			{
				if (pc87306_regs[0x1B] & 0x10)
				{
					temp = (pc87306_regs[0x1B] & 0x20) >> 5;
					if (temp)
					{
						lpt1_init(0x378); break;
					}
					else
					{
						lpt1_init(0x278); break;
					}
				}
				else
				{
	        	                temp = pc87306_regs[1] & 3;
		                        switch (temp)
        		                {
                		                case 0: lpt1_init(0x378); break;
                        		        case 1: lpt1_init(0x3bc); break;
                                		case 2: lpt1_init(0x278); break;
		                        }
				}
			}

			if (valxor & 2)
			{
				serial1_remove();
				if (val & 2)
				{
					serial1_handler();
					// if (mouse_always_serial)  mouse_serial_init();
				}
			}
			if (valxor & 4)
			{
				serial2_remove();
				if (val & 4)  serial2_handler();
			}
			
			break;
		case 1:
			if (valxor & 1)
			{
				lpt1_remove();
				if (pc87306_regs[0] & 1)
				{
					if (pc87306_regs[0x1B] & 0x10)
					{
						temp = (pc87306_regs[0x1B] & 0x20) >> 5;
						if (temp)
						{
							lpt_port = 0x378; break;
						}
						else
						{
							lpt_port = 0x278; break;
						}
					}
					else
					{
						temp = val & 3;
			                        switch (temp)
        			                {
                			                case 0: lpt_port = 0x378; break;
                        			        case 1: lpt_port = 0x3bc; break;
                                			case 2: lpt_port = 0x278; break;
			                        }
					}
					lpt1_init(lpt_port);
					pc87306_regs[0x19] = lpt_port >> 2;
				}
			}

			if (valxor & 0xcc)
			{
				serial1_remove();

				if (pc87306_regs[0] & 2)
				{
					serial1_handler();
	                	        // if (mouse_always_serial)  mouse_serial_init();
				}
			}

			if (valxor & 0xf0)
			{
				serial2_remove();

				if (pc87306_regs[0] & 4)  serial2_handler();
			}
			break;
		case 9:
			// pclog("Setting DENSEL polarity to: %i (before: %i)\n", (val & 0x40 ? 1 : 0), fdc_get_densel_polarity());
			fdc_update_densel_polarity((val & 0x40) ? 1 : 0);
			break;
		case 0xF:
			pc87306_gpio_init();
			break;
		case 0x1C:
			// if (valxor & 0x25)
			if (valxor)
			{
				serial1_remove();
				serial2_remove();
				if (pc87306_regs[0] & 2)
				{
					serial1_handler();
		                        // if (mouse_always_serial)  mouse_serial_init();
				}
				if (pc87306_regs[0] & 4)  serial2_handler();
			}
			break;
	}
}

uint8_t pc87306_gpio_read(uint16_t port, void *priv)
{
	return pc87306_gpio[port & 1];
}

uint8_t pc87306_read(uint16_t port, void *priv)
{
        // pclog("pc87306_read : port=%04x reg %02X locked=%i\n", port, pc87306_curreg, pc87306_locked);
	uint8_t index;
	index = (port & 1) ? 0 : 1;

	if (index)
		return pc87306_curreg;
	else
	{
	        if (pc87306_curreg >= 28)
			return 0xff;
		else
			return pc87306_regs[pc87306_curreg];
	}
}

void pc87306_gpio_remove()
{
        io_removehandler(pc87306_regs[0xF] << 2, 0x0002, pc87306_gpio_read, NULL, NULL, pc87306_gpio_write, NULL, NULL,  NULL);
}

void pc87306_gpio_init()
{
        io_sethandler(pc87306_regs[0xF] << 2, 0x0002, pc87306_gpio_read, NULL, NULL, pc87306_gpio_write, NULL, NULL,  NULL);
}

void pc87306_init()
{
	pc87306_regs[0] = 0xF;
	pc87306_regs[1] = 0x11;
	pc87306_regs[5] = 0xD;
	pc87306_regs[8] = 0x70;
	pc87306_regs[9] = 0xFF;
	pc87306_regs[0xF] = 0x1E;
	pc87306_regs[0x19] = 0xDE;
	pc87306_regs[0x1B] = 0x10;
	pc87306_regs[0x1C] = 0;
	/*
		0 = 360 rpm @ 500 kbps for 3.5"
		1 = Default, 300 rpm @ 500,300,250,1000 kbps for 3.5"
	*/
	fdc_update_is_nsc(1);
	fdc_update_densel_polarity(1);
	fdc_update_max_track(85);
	fdd_swap = 0;
        io_sethandler(0x02e, 0x0002, pc87306_read, NULL, NULL, pc87306_write, NULL, NULL,  NULL);
}
