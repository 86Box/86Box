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
    uint32_t	local;
    int		configuration_state;	/* state of algorithm to enter configuration mode */
    int		configuration_mode;
    uint16_t	cri_addr;		/* cri = configuration index register, addr is even */
    uint16_t	cap_addr;		/* cap = configuration access port, addr is odd and is cri_addr + 1 */
    uint8_t	cri;			/* currently indexed register */
    uint8_t	last_write;

    /* these regs are not affected by reset */
    uint8_t	regs[15];		/* there are 16 indexes, but there is no need to store the last one which is: R = cri_addr / 4, W = exit config mode */
    fdc_t *	fdc;
    nvr_t *	nvr;
    void *	gameport;
    serial_t *	uart[2];
} upc_t;


static void 
f82c710_update_ports(upc_t *dev, int set)
{
    uint16_t com_addr = 0;
    uint16_t lpt_addr = 0;

    serial_remove(dev->uart[0]);
    serial_remove(dev->uart[1]);
    lpt1_remove();
    lpt2_remove();
    fdc_remove(dev->fdc);
    ide_pri_disable();

    if (!set)
	return;

    if (dev->regs[0] & 4) {
	com_addr = dev->regs[4] * 4;
	if (com_addr == SERIAL1_ADDR)
		serial_setup(dev->uart[0], com_addr, 4);
	else if (com_addr == SERIAL2_ADDR)
		serial_setup(dev->uart[1], com_addr, 3);
    }

    if (dev->regs[0] & 8) {
	lpt_addr = dev->regs[6] * 4;
	lpt1_init(lpt_addr);
	if ((lpt_addr == 0x378) || (lpt_addr == 0x3bc))
		lpt1_irq(7);
	else if (lpt_addr == 0x278)
		lpt1_irq(5);
    }

    if (dev->regs[12] & 0x80)
	ide_pri_enable();

    if (dev->regs[12] & 0x20)
	fdc_set_base(dev->fdc, 0x03f0);
}


static void
f82c606_update_ports(upc_t *dev, int set)
{
    uint8_t uart1_int = 0xff;
    uint8_t uart2_int = 0xff;
    uint8_t lpt1_int = 0xff;
    int nvr_int = -1;

    serial_remove(dev->uart[0]);
    serial_remove(dev->uart[1]);
    lpt1_remove();
    lpt2_remove();

    nvr_at_handler(0, ((uint16_t) dev->regs[3]) << 2, dev->nvr);
    nvr_at_handler(0, 0x70, dev->nvr);

    gameport_remap(dev->gameport, 0);

    if (!set)
	return;

    switch (dev->regs[8] & 0xc0) {
	case 0x40: nvr_int = 3; break;
	case 0x80: uart1_int = 3; break;
	case 0xc0: uart2_int = 3; break;
    }

    switch (dev->regs[8] & 0x30) {
	case 0x10: nvr_int = 4; break;
	case 0x20: uart1_int = 4; break;
	case 0x30: uart2_int = 4; break;
    }

    switch (dev->regs[8] & 0x0c) {
	case 0x04: nvr_int = 5; break;
	case 0x08: uart1_int = 5; break;
	case 0x0c: lpt1_int = 5; break;
    }

    switch (dev->regs[8] & 0x03) {
	case 0x01: nvr_int = 7; break;
	case 0x02: uart2_int = 7; break;
	case 0x03: lpt1_int = 7; break;
    }

    if (dev->regs[0] & 1) {
	gameport_remap(dev->gameport, ((uint16_t) dev->regs[7]) << 2);
	pclog("Game port at %04X\n", ((uint16_t) dev->regs[7]) << 2);
    }

    if (dev->regs[0] & 2) {
	serial_setup(dev->uart[0], ((uint16_t) dev->regs[4]) << 2, uart1_int);
	pclog("UART 1 at %04X, IRQ %i\n", ((uint16_t) dev->regs[4]) << 2, uart1_int);
    }

    if (dev->regs[0] & 4) {
	serial_setup(dev->uart[1], ((uint16_t) dev->regs[5]) << 2, uart2_int);
	pclog("UART 2 at %04X, IRQ %i\n", ((uint16_t) dev->regs[5]) << 2, uart2_int);
    }

    if (dev->regs[0] & 8) {
	lpt1_init(((uint16_t) dev->regs[6]) << 2);
	lpt1_irq(lpt1_int);
	pclog("LPT1 at %04X, IRQ %i\n", ((uint16_t) dev->regs[6]) << 2, lpt1_int);
    }

    nvr_at_handler(1, ((uint16_t) dev->regs[3]) << 2, dev->nvr);
    nvr_irq_set(nvr_int, dev->nvr);
    pclog("RTC at %04X, IRQ %i\n", ((uint16_t) dev->regs[3]) << 2, nvr_int);
}


static uint8_t 
f82c710_config_read(uint16_t port, void *priv)
{
    upc_t *dev = (upc_t *) priv;
    uint8_t temp = 0xff;

    if (dev->configuration_mode) {
	if (port == dev->cri_addr) {
		temp = dev->cri;
	} else if (port == dev->cap_addr) {
		if (dev->cri == 0xf)
			temp = dev->cri_addr / 4;
		else
			temp = dev->regs[dev->cri];
	}
    }

    return temp;
}


static void 
f82c710_config_write(uint16_t port, uint8_t val, void *priv)
{
    upc_t *dev = (upc_t *) priv;
    int configuration_state_event = 0;

    switch (port) {
	case 0x2fa:
		if ((dev->configuration_state == 0) && (val != 0x00) && (val != 0xff) && (dev->local == 606)) {
			configuration_state_event = 1;
			dev->last_write = val;
		} else if ((dev->configuration_state == 0) && (val == 0x55) && (dev->local == 710))
			configuration_state_event = 1;
		else if (dev->configuration_state == 4) {
			if ((val | dev->last_write) == 0xff) {
				dev->cri_addr = ((uint16_t) dev->last_write) << 2;
				dev->cap_addr = dev->cri_addr + 1;
				dev->configuration_mode = 1;
				if (dev->local == 606)
					f82c606_update_ports(dev, 0);
				else if (dev->local == 710)
					f82c710_update_ports(dev, 0);
				/* TODO: is the value of cri reset here or when exiting configuration mode? */
				io_sethandler(dev->cri_addr, 0x0002, f82c710_config_read, NULL, NULL, f82c710_config_write, NULL, NULL, dev);
			} else
				dev->configuration_mode = 0;
		}
		break;
	case 0x3fa:
		if ((dev->configuration_state == 1) && ((val | dev->last_write) == 0xff) && (dev->local == 606))
			configuration_state_event = 1;
		else if ((dev->configuration_state == 1) && (val == 0xaa) && (dev->local == 710))
			configuration_state_event = 1;
		else if ((dev->configuration_state == 2) && (val == 0x36))
			configuration_state_event = 1;
		else if (dev->configuration_state == 3) {
			dev->last_write = val;
			configuration_state_event = 1;
		}
		break;
	default:
		break;
    }

    if (dev->configuration_mode) {
	if (port == dev->cri_addr) {
		dev->cri = val & 0xf;
	} else if (port == dev->cap_addr) {
		if (dev->cri == 0xf) {
			dev->configuration_mode = 0;
			io_removehandler(dev->cri_addr, 0x0002, f82c710_config_read, NULL, NULL, f82c710_config_write, NULL, NULL, dev);
			/* TODO: any benefit in updating at each register write instead of when exiting config mode? */
			if (dev->local == 606)
				f82c606_update_ports(dev, 1);
			else if (dev->local == 710)
				f82c710_update_ports(dev, 1);
		} else
			dev->regs[dev->cri] = val;
	}
    }

    /* TODO: is the state only reset when accessing 0x2fa and 0x3fa wrongly? */
    if ((port == 0x2fa || port == 0x3fa) && configuration_state_event)
	dev->configuration_state++;
    else
	dev->configuration_state = 0;
}


static void
f82c710_reset(void *priv)
{
    upc_t *dev = (upc_t *) priv;

    /* Set power-on defaults. */
    if (dev->local == 606) {
	dev->regs[0] = 0x00;	/* Enable */
	dev->regs[1] = 0x00;	/* Configuration Register */
	dev->regs[2] = 0x00;	/* Ext Baud Rate Select */
	dev->regs[3] = 0xb0;	/* RTC Base */
	dev->regs[4] = 0xfe;	/* UART1 Base */
	dev->regs[5] = 0xbe;	/* UART2 Base */
	dev->regs[6] = 0x9e;	/* Parallel Base */
	dev->regs[7] = 0x80;	/* Game Base */
	dev->regs[8] = 0xec;	/* Interrupt Select */
    } else if (dev->local == 710) {
	dev->regs[0] = 0x0c;
	dev->regs[1] = 0x00;
	dev->regs[2] = 0x00;
	dev->regs[3] = 0x00;
	dev->regs[4] = 0xfe;
	dev->regs[5] = 0x00;
	dev->regs[6] = 0x9e;
	dev->regs[7] = 0x00;
	dev->regs[8] = 0x00;
	dev->regs[9] = 0xb0;
	dev->regs[10] = 0x00;
	dev->regs[11] = 0x00;
	dev->regs[12] = 0xa0;
	dev->regs[13] = 0x00;
	dev->regs[14] = 0x00;
    }

    if (dev->local == 606)
	f82c606_update_ports(dev, 1);
    else if (dev->local == 710)
	f82c710_update_ports(dev, 1);
}


static void
f82c710_close(void *priv)
{
    upc_t *dev = (upc_t *) priv;

    free(dev);
}


static void *
f82c710_init(const device_t *info)
{
    upc_t *dev = (upc_t *) malloc(sizeof(upc_t));
    memset(dev, 0, sizeof(upc_t));
    dev->local = info->local;

    if (dev->local == 606) {
	dev->nvr = device_add(&at_nvr_old_device);
	dev->gameport = gameport_add(&gameport_sio_device);
    } else if (dev->local == 710)
	dev->fdc = device_add(&fdc_at_device);

    dev->uart[0] = device_add_inst(&ns16450_device, 1);
    dev->uart[1] = device_add_inst(&ns16450_device, 2);	

    io_sethandler(0x02fa, 0x0001, NULL, NULL, NULL, f82c710_config_write, NULL, NULL, dev);
    io_sethandler(0x03fa, 0x0001, NULL, NULL, NULL, f82c710_config_write, NULL, NULL, dev);

    f82c710_reset(dev);

    return dev;
}


const device_t f82c606_device = {
    "82C606 CHIPSpak Multifunction Controller",
    0,
    606,
    f82c710_init, f82c710_close, f82c710_reset,
    { NULL }, NULL, NULL,
    NULL
};

const device_t f82c710_device = {
    "F82C710 UPC Super I/O",
    0,
    710,
    f82c710_init, f82c710_close, f82c710_reset,
    { NULL }, NULL, NULL,
    NULL
};
