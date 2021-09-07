/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Chips & Technologies F82C710 Universal Peripheral
 *		Controller (UPC) and 82C606 CHIPSpak Multifunction Controller.
 *
 *		Relevant literature:
 *
 *		[1] Chips and Technologies, Inc.,
 *		    82C605/82C606 CHIPSpak/CHIPSport MULTIFUNCTION CONTROLLERS,
 *		    PRELIMINARY Data Sheet, Revision 1, May 1987.
 *		    <https://archive.org/download/82C606/82C606.pdf>
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Eluan Costa Miranda <eluancm@gmail.com>
 *		Lubomir Rintel <lkundrak@v3.sk>
 *
 *		Copyright 2020 Sarah Walker.
 *		Copyright 2020 Eluan Costa Miranda.
 *		Copyright 2021 Lubomir Rintel.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/gameport.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/nvr.h>
#include <86box/sio.h>

typedef struct upc_t
{
	uint32_t local;
	int configuration_state; /* state of algorithm to enter configuration mode */
	int configuration_mode;
	uint16_t cri_addr; /* cri = configuration index register, addr is even */
	uint16_t cap_addr; /* cap = configuration access port, addr is odd and is cri_addr + 1 */
	uint8_t cri; /* currently indexed register */

        /* these regs are not affected by reset */
        uint8_t regs[15]; /* there are 16 indexes, but there is no need to store the last one which is: R = cri_addr / 4, W = exit config mode */
	fdc_t *fdc;
	nvr_t *nvr;
	void *gameport;
	serial_t *uart[2];
} upc_t;

static void 
f82c710_update_ports(upc_t *upc)
{
	uint16_t com_addr = 0;
	uint16_t lpt_addr = 0;
	
        serial_remove(upc->uart[0]);
        serial_remove(upc->uart[1]);
        lpt1_remove();
        lpt2_remove();
        fdc_remove(upc->fdc);
        ide_pri_disable();

        if (upc->regs[0] & 4) {
                com_addr = upc->regs[4] * 4;
		if (com_addr == SERIAL1_ADDR) {
			serial_setup(upc->uart[0], com_addr, 4);
		} else if (com_addr == SERIAL2_ADDR) {
			serial_setup(upc->uart[1], com_addr, 3);
		}
        }
                

        if (upc->regs[0] & 8) {
		lpt_addr = upc->regs[6] * 4;
                lpt1_init(lpt_addr);
		if ((lpt_addr == 0x378) || (lpt_addr == 0x3bc)) {
			lpt1_irq(7);
		} else if (lpt_addr == 0x278) {
			lpt1_irq(5);
		}
        }

        if (upc->regs[12] & 0x80) {
                ide_pri_enable();
        }

        if (upc->regs[12] & 0x20) {
                fdc_set_base(upc->fdc, 0x03f0);
        }
}

static void
f82c606_update_ports(upc_t *upc)
{
	uint8_t uart1_int = 0xff;
	uint8_t uart2_int = 0xff;
	uint8_t lpt1_int = 0xff;
	int nvr_int = -1;

	serial_remove(upc->uart[0]);
	serial_remove(upc->uart[1]);
	lpt1_remove();
	lpt2_remove();

	nvr_at_handler(0, upc->regs[3] * 4, upc->nvr);
	nvr_at_handler(0, 0x70, upc->nvr);

	switch (upc->regs[8] & 0xc0) {
	case 0x40: nvr_int = 4; break;
	case 0x80: uart1_int = 4; break;
	case 0xc0: uart2_int = 4; break;
	}

	switch (upc->regs[8] & 0x30) {
	case 0x10: nvr_int = 3; break;
	case 0x20: uart1_int = 3; break;
	case 0x30: uart2_int = 3; break;
	}

	switch (upc->regs[8] & 0x0c) {
	case 0x04: nvr_int = 5; break;
	case 0x08: uart1_int = 5; break;
	case 0x0c: lpt1_int = 5; break;
	}

	switch (upc->regs[8] & 0x03) {
	case 0x01: nvr_int = 7; break;
	case 0x02: uart2_int = 7; break;
	case 0x03: lpt1_int = 7; break;
	}

	if (upc->regs[0] & 1)
		gameport_remap(upc->gameport, upc->regs[7] * 4);
	else
		gameport_remap(upc->gameport, 0);

	if (upc->regs[0] & 2)
		serial_setup(upc->uart[0], upc->regs[4] * 4, uart1_int);

	if (upc->regs[0] & 4)
		serial_setup(upc->uart[1], upc->regs[5] * 4, uart2_int);

	if (upc->regs[0] & 8) {
		lpt1_init(upc->regs[6] * 4);
		lpt1_irq(lpt1_int);
	}

	nvr_at_handler(1, upc->regs[3] * 4, upc->nvr);
	nvr_irq_set(nvr_int, upc->nvr);
}

static uint8_t 
f82c710_config_read(uint16_t port, void *priv)
{
	upc_t *upc = (upc_t *)priv;
	uint8_t temp = 0xff;

        if (upc->configuration_mode) {
		if (port == upc->cri_addr) {
			temp = upc->cri;
                } else if (port == upc->cap_addr) {
			if (upc->cri == 0xf)
				temp = upc->cri_addr / 4;
			else
				temp = upc->regs[upc->cri];
                }
        }

        return temp;
}

static void 
f82c710_config_write(uint16_t port, uint8_t val, void *priv)
{
	upc_t *upc = (upc_t *)priv;
	int configuration_state_event = 0;

        switch(port) {
                case 0x2fa:
			if (upc->configuration_state == 0 && val == 0x55)
				configuration_state_event = 1;
                        else if (upc->configuration_state == 4) {
                                uint8_t addr_verify = upc->cri_addr / 4;
				addr_verify += val;
				if (addr_verify == 0xff) {
                                        upc->configuration_mode = 1;
                                        /* TODO: is the value of cri reset here or when exiting configuration mode? */
                                        io_sethandler(upc->cri_addr, 0x0002, f82c710_config_read, NULL, NULL, f82c710_config_write, NULL, NULL, upc);
                                } else {
					upc->configuration_mode = 0;
				}
			}
			break;
                case 0x3fa:
			if (upc->configuration_state == 1 && val == 0xaa)
				configuration_state_event = 1;
                        else if (upc->configuration_state == 2 && val == 0x36)
				configuration_state_event = 1;
                        else if (upc->configuration_state == 3) {
				upc->cri_addr = val * 4;
				upc->cap_addr = upc->cri_addr + 1;
				configuration_state_event = 1;
			}
			break;
                default:
                        break;
        }
        
	if (upc->configuration_mode) {
                if (port == upc->cri_addr) {
                        upc->cri = val & 0xf;
                } else if (port == upc->cap_addr) {
                        if (upc->cri == 0xf) {
				upc->configuration_mode = 0;
				io_removehandler(upc->cri_addr, 0x0002, f82c710_config_read, NULL, NULL, f82c710_config_write, NULL, NULL, upc);
				/* TODO: any benefit in updating at each register write instead of when exiting config mode? */
				if (upc->local == 710)
					f82c710_update_ports(upc);
				if (upc->local == 606)
					f82c606_update_ports(upc);
			} else {
                                upc->regs[upc->cri] = val;
                        }
                }
        }

        /* TODO: is the state only reset when accessing 0x2fa and 0x3fa wrongly? */
	if ((port == 0x2fa || port == 0x3fa) && configuration_state_event)
		upc->configuration_state++;
	else
		upc->configuration_state = 0;
}


static void
f82c710_reset(upc_t *upc)
{
	serial_remove(upc->uart[0]);
	serial_setup(upc->uart[0], SERIAL1_ADDR, SERIAL1_IRQ);

	serial_remove(upc->uart[1]);
	serial_setup(upc->uart[1], SERIAL2_ADDR, SERIAL2_IRQ);
	
	lpt1_remove();
	lpt1_init(0x378);
	lpt1_irq(7);

	if (upc->local == 710)
		fdc_reset(upc->fdc);
}

static void *
f82c710_init(const device_t *info)
{
	upc_t *upc = (upc_t *) malloc(sizeof(upc_t));
	memset(upc, 0, sizeof(upc_t));
	upc->local = info->local;

	if (upc->local == 710) {
		upc->regs[0] = 0x0c;
		upc->regs[1] = 0x00;
		upc->regs[2] = 0x00;
		upc->regs[3] = 0x00;
		upc->regs[4] = 0xfe;
		upc->regs[5] = 0x00;
		upc->regs[6] = 0x9e;
		upc->regs[7] = 0x00;
		upc->regs[8] = 0x00;
		upc->regs[9] = 0xb0;
		upc->regs[10] = 0x00;
		upc->regs[11] = 0x00;
		upc->regs[12] = 0xa0;
		upc->regs[13] = 0x00;
		upc->regs[14] = 0x00;

		upc->fdc = device_add(&fdc_at_device);
	}

	if (upc->local == 606) {
		/* Set power-on defaults. */
		upc->regs[0] = 0x00; /* Enable */
		upc->regs[1] = 0x00; /* Configuration Register */
		upc->regs[2] = 0x00; /* Ext Baud Rate Select */
		upc->regs[3] = 0xb0; /* RTC Base */
		upc->regs[4] = 0xfe; /* UART1 Base */
		upc->regs[5] = 0xbe; /* UART2 Base */
		upc->regs[6] = 0x9e; /* Parallel Base */
		upc->regs[7] = 0x80; /* Game Base */
		upc->regs[8] = 0xec; /* Interrupt Select */

		upc->nvr = device_add(&at_nvr_old_device);
		upc->gameport = gameport_add(&gameport_sio_device);
	}

	upc->uart[0] = device_add_inst(&ns16450_device, 1);
	upc->uart[1] = device_add_inst(&ns16450_device, 2);	

        io_sethandler(0x02fa, 0x0001, NULL, NULL, NULL, f82c710_config_write, NULL, NULL, upc);
        io_sethandler(0x03fa, 0x0001, NULL, NULL, NULL, f82c710_config_write, NULL, NULL, upc);

	f82c710_reset(upc);

	if (upc->local == 710)
		f82c710_update_ports(upc);
	if (upc->local == 606)
		f82c606_update_ports(upc);

	return upc;
}

static void
f82c710_close(void *priv)
{
	upc_t *upc = (upc_t *)priv;

	free(upc);
}

const device_t f82c606_device = {
	"82C606 CHIPSpak Multifunction Controller",
	0,
	606,
	f82c710_init, f82c710_close, NULL,
	{ NULL }, NULL, NULL,
	NULL
};

const device_t f82c710_device = {
	"F82C710 UPC Super I/O",
	0,
	710,
	f82c710_init, f82c710_close, NULL,
	{ NULL }, NULL, NULL,
	NULL
};
