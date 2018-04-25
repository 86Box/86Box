/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the Winbond W83877F Super I/O Chip.
 *
 *		Winbond W83877F Super I/O Chip
 *		Used by the Award 430HX
 *
 * Version:	@(#)sio_w83877f.c	1.0.10	2018/04/04
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2016-2018 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "86box.h"
#include "machine/machine.h"
#include "device.h"
#include "io.h"
#include "pci.h"
#include "mem.h"
#include "rom.h"
#include "lpt.h"
#include "serial.h"
#include "floppy/fdd.h"
#include "floppy/fdc.h"
#include "sio.h"


static int w83877f_locked;
static int w83877f_rw_locked = 0;
static int w83877f_curreg = 0;
static uint8_t w83877f_regs[0x2A];
static uint8_t tries;
static fdc_t *w83877f_fdc;

static int winbond_port = 0x3f0;
static int winbond_key = 0x89;
static int winbond_key_times = 1;


void w83877f_write(uint16_t port, uint8_t val, void *priv);
uint8_t w83877f_read(uint16_t port, void *priv);

#define OCSS0		(w83877f_regs[0] & 1)
#define OCSS1		((w83877f_regs[0] >> 1) & 1)
#define PRTMODS0	((w83877f_regs[0] >> 2) & 1)
#define PRTMODS1	((w83877f_regs[0] >> 3) & 1)

#define ABCHG		(w83877f_regs[1] >> 7)

#define CEA		(w83877f_regs[2] & 1)
#define EA3		((w83877f_regs[2] >> 1) & 1)
#define EA4		((w83877f_regs[2] >> 2) & 1)
#define EA5		((w83877f_regs[2] >> 3) & 1)
#define EA6		((w83877f_regs[2] >> 4) & 1)
#define EA7		((w83877f_regs[2] >> 5) & 1)
#define EA8		((w83877f_regs[2] >> 6) & 1)
#define EA9		(w83877f_regs[2] >> 7)

#define SUBMIDI		(w83877f_regs[3] & 1)
#define SUAMIDI		((w83877f_regs[3] >> 1) & 1)
#define GMODS		((w83877f_regs[3] >> 4) & 1)
#define EPPVER		((w83877f_regs[3] >> 5) & 1)
#define GMENL		((w83877f_regs[3] >> 6) & 1)

#define URBTRI		(w83877f_regs[4] & 1)
#define URATRI		((w83877f_regs[4] >> 1) & 1)
#define GMTRI		((w83877f_regs[4] >> 2) & 1)
#define PRTTRI		((w83877f_regs[4] >> 3) & 1)
#define URBPWD		((w83877f_regs[4] >> 4) & 1)
#define URAPWD		((w83877f_regs[4] >> 5) & 1)
#define GMPWD		((w83877f_regs[4] >> 6) & 1)
#define PRTPWD		(w83877f_regs[4] >> 7)

#define ECPFTHR0	(w83877f_regs[5] & 1)
#define ECPFTHR1	((w83877f_regs[5] >> 1) & 1)
#define ECPFTHR2	((w83877f_regs[5] >> 2) & 1)
#define ECPFTHR3	((w83877f_regs[5] >> 3) & 1)

#define IDETRI		(w83877f_regs[6] & 1)
#define FDCTRI		((w83877f_regs[6] >> 1) & 1)
#define IDEPWD		((w83877f_regs[6] >> 2) & 1)
#define FDCPWD		((w83877f_regs[6] >> 3) & 1)
#define FIPURDWM	((w83877f_regs[6] >> 4) & 1)
#define SEL4FDD		((w83877f_regs[6] >> 5) & 1)
#define OSCS2		((w83877f_regs[6] >> 6) & 1)

#define FDDA_TYPE	(w83877f_regs[7] & 3)
#define FDDB_TYPE	((w83877f_regs[7] >> 2) & 3)
#define FDDC_TYPE	((w83877f_regs[7] >> 4) & 3)
#define FDDD_TYPE	((w83877f_regs[7] >> 6) & 3)

#define FD_BOOT		(w83877f_regs[8] & 3)
#define MEDIA_ID	((w83877f_regs[8] >> 2) & 3)
#define SWWP		((w83877f_regs[8] >> 4) & 1)
#define DISFDDWR	((w83877f_regs[8] >> 5) & 1)
#define APDTMS2		((w83877f_regs[8] >> 6) & 1)
#define APDTMS1		(w83877f_regs[8] >> 7)

#define CHIP_ID		(w83877f_regs[9] & 0xF)
#define EN3MODE		((w83877f_regs[9] >> 5) & 1)
#define LOCKREG		((w83877f_regs[9] >> 6) & 1)
#define PRTMODS2	((w83877f_regs[9] >> 7) & 1)

#define PEXTECPP	(w83877f_regs[0xA] & 1)
#define PEXT_ECP	((w83877f_regs[0xA] >> 1) & 1)
#define PEXT_EPP	((w83877f_regs[0xA] >> 2) & 1)
#define PEXT_ADP	((w83877f_regs[0xA] >> 3) & 1)
#define PDCACT		((w83877f_regs[0xA] >> 4) & 1)
#define PDIRHOP		((w83877f_regs[0xA] >> 5) & 1)
#define PEXT_ACT	((w83877f_regs[0xA] >> 6) & 1)
#define PFDCACT		(w83877f_regs[0xA] >> 7)

#define DRV2EN_NEG	(w83877f_regs[0xB] & 1)		/* 0 = drive 2 installed */
#define INVERTZ		((w83877f_regs[0xB] >> 1) & 1)	/* 0 = invert DENSEL polarity */
#define MFM		((w83877f_regs[0xB] >> 2) & 1)
#define IDENT		((w83877f_regs[0xB] >> 3) & 1)
#define ENIFCHG		((w83877f_regs[0xB] >> 4) & 1)

#define TX2INV		(w83877f_regs[0xC] & 1)
#define RX2INV		((w83877f_regs[0xC] >> 1) & 1)
#define URIRSEL		((w83877f_regs[0xC] >> 3) & 1)
#define HEFERE		((w83877f_regs[0xC] >> 5) & 1)
#define TURB		((w83877f_regs[0xC] >> 6) & 1)
#define TURA		(w83877f_regs[0xC] >> 7)

#define IRMODE0		(w83877f_regs[0xD] & 1)
#define IRMODE1		((w83877f_regs[0xD] >> 1) & 1)
#define IRMODE2		((w83877f_regs[0xD] >> 2) & 1)
#define HDUPLX		((w83877f_regs[0xD] >> 3) & 1)
#define SIRRX0		((w83877f_regs[0xD] >> 4) & 1)
#define SIRRX1		((w83877f_regs[0xD] >> 5) & 1)
#define SIRTX0		((w83877f_regs[0xD] >> 6) & 1)
#define SIRTX1		(w83877f_regs[0xD] >> 7)

#define GIO0AD		(w83877f_regs[0x10] | (((uint16_t) w83877f_regs[0x11] & 7) << 8))
#define GIO0CADM	(w83877f_regs[0x11] >> 6)
#define GIO1AD		(w83877f_regs[0x12] | (((uint16_t) w83877f_regs[0x13] & 7) << 8))
#define GIO1CADM	(w83877f_regs[0x13] >> 6)

#define GDA0IPI		(w83877f_regs[0x14] & 1)
#define GDA0OPI		((w83877f_regs[0x14] >> 1) & 1)
#define GCS0IOW		((w83877f_regs[0x14] >> 2) & 1)
#define GCS0IOR		((w83877f_regs[0x14] >> 3) & 1)
#define GIO0CSH		((w83877f_regs[0x14] >> 4) & 1)
#define GIOP0MD		((w83877f_regs[0x14] >> 5) & 7)

#define GDA1IPI		(w83877f_regs[0x15] & 1)
#define GDA1OPI		((w83877f_regs[0x15] >> 1) & 1)
#define GCS1IOW		((w83877f_regs[0x15] >> 2) & 1)
#define GCS1IOR		((w83877f_regs[0x15] >> 3) & 1)
#define GIO1CSH		((w83877f_regs[0x15] >> 4) & 1)
#define GIOP1MD		((w83877f_regs[0x15] >> 5) & 7)

#define HEFRAS		(w83877f_regs[0x16] & 1)
#define IRIDE		((w83877f_regs[0x16] >> 1) & 1)
#define PNPCVS		((w83877f_regs[0x16] >> 2) & 1)
#define GMDRQ		((w83877f_regs[0x16] >> 3) & 1)
#define GOIQSEL		((w83877f_regs[0x16] >> 4) & 1)

#define DSUBLGRQ	(w83877f_regs[0x17] & 1)
#define DSUALGRQ	((w83877f_regs[0x17] >> 1) & 1)
#define DSPRLGRQ	((w83877f_regs[0x17] >> 2) & 1)
#define DSFDLGRQ	((w83877f_regs[0x17] >> 3) & 1)
#define PRIRQOD		((w83877f_regs[0x17] >> 4) & 1)

#define GMAS		(w83877f_regs[0x1E] & 3)
#define GMAD		(w83877f_regs[0x1E] & 0xFC)

#define FDCAD		((w83877f_regs[0x20] & 0xFC) << 2)

/* Main IDE base address. */
#define IDE0AD		((w83877f_regs[0x21] & 0xFC) << 2)
/* IDE Alternate status base address. */
#define IDE1AD		((w83877f_regs[0x22] & 0xFC) << 2)

#define PRTAD		(((uint16_t) w83877f_regs[0x23]) << 2)

#define URAAD		(((uint16_t) w83877f_regs[0x24] & 0xFE) << 2)
#define URBAD		(((uint16_t) w83877f_regs[0x25] & 0xFE) << 2)

#define PRTDQS		(w83877f[regs[0x26] & 0xF)
#define FDCDQS		(w83877f[regs[0x26] >> 4)

#define PRTIQS		(w83877f[regs[0x27] & 0xF)
#define ECPIRQx		(w83877f[regs[0x27] >> 5)

#define URBIQS		(w83877f[regs[0x28] & 0xF)
#define URAIQS		(w83877f[regs[0x28] >> 4)

#define IQNIQS		(w83877f[regs[0x29] & 0xF)
#define FDCIQS		(w83877f[regs[0x29] >> 4)

#define W83757		(!PRTMODS2 && !PRTMODS1 && !PRTMODS0)
#define EXTFDC		(!PRTMODS2 && !PRTMODS1 && PRTMODS0)
#define EXTADP		(!PRTMODS2 && PRTMODS1 && !PRTMODS0)
#define EXT2FDD		(!PRTMODS2 && PRTMODS1 && PRTMODS0)
#define JOYSTICK	(PRTMODS2 && !PRTMODS1 && !PRTMODS0)
#define EPP_SPP		(PRTMODS2 && !PRTMODS1 && PRTMODS0)
#define ECP		(PRTMODS2 && PRTMODS1 && !PRTMODS0)
#define ECP_EPP		(PRTMODS2 && PRTMODS1 && PRTMODS0)

static uint16_t fdc_valid_ports[2] = {0x3F0, 0x370};
static uint16_t ide_valid_ports[2] = {0x1F0, 0x170};
static uint16_t ide_as_valid_ports[2] = {0x3F6, 0x376};
static uint16_t lpt1_valid_ports[3] = {0x3BC, 0x378, 0x278};
static uint16_t com1_valid_ports[9] = {0x3F8, 0x2F8, 0x3E8, 0x2E8};
static uint16_t com2_valid_ports[9] = {0x3F8, 0x2F8, 0x3E8, 0x2E8};


static void w83877f_remap(void)
{
        io_removehandler(0x250, 0x0002, w83877f_read, NULL, NULL, w83877f_write, NULL, NULL,  NULL);
        io_removehandler(0x3f0, 0x0002, w83877f_read, NULL, NULL, w83877f_write, NULL, NULL,  NULL);
        io_sethandler(HEFRAS ? 0x3f0 : 0x250, 0x0002, w83877f_read, NULL, NULL, w83877f_write, NULL, NULL,  NULL);
	winbond_port = (HEFRAS ? 0x3f0 : 0x250);
	winbond_key_times = HEFRAS + 1;
	winbond_key = (HEFRAS ? 0x86 : 0x88) | HEFERE;
	/* pclog("W83877F: Remapped to port %04X, key %02X\n", winbond_port, winbond_key); */
}


static uint8_t is_in_array(uint16_t *port_array, uint8_t max, uint16_t port)
{
	uint8_t i = 0;

	for (i = 0; i < max; i++)
	{
		if (port_array[i] == port)  return 1;
	}
	return 0;
}


static uint16_t make_port(uint8_t reg)
{
	uint16_t p = 0;

	switch(reg)
	{
		case 0x20:
			p = ((uint16_t) (w83877f_regs[reg] & 0xfc)) << 2;
			p &= 0xFF0;
			if ((p < 0x100) || (p > 0x3F0))  p = 0x3F0;
			if (!(is_in_array(fdc_valid_ports, 2, p)))  p = 0x3F0;
			w83877f_regs[reg] = ((p >> 2) & 0xfc) | (w83877f_regs[reg] & 3);
			break;
		case 0x21:
			p = ((uint16_t) (w83877f_regs[reg] & 0xfc)) << 2;
			p &= 0xFF0;
			if ((p < 0x100) || (p > 0x3F0))  p = 0x1F0;
			if (!(is_in_array(ide_valid_ports, 2, p)))  p = 0x1F0;
			w83877f_regs[reg] = ((p >> 2) & 0xfc) | (w83877f_regs[reg] & 3);
			break;
		case 0x22:
			p = ((uint16_t) (w83877f_regs[reg] & 0xfc)) << 2;
			p &= 0xFF0;
			if ((p < 0x106) || (p > 0x3F6))  p = 0x3F6;
			if (!(is_in_array(ide_as_valid_ports, 2, p)))  p = 0x3F6;
			w83877f_regs[reg] = ((p >> 2) & 0xfc) | (w83877f_regs[reg] & 3);
			break;
		case 0x23:
			p = ((uint16_t) (w83877f_regs[reg] & 0xff)) << 2;
			p &= 0xFFC;
			if ((p < 0x100) || (p > 0x3F8))  p = 0x378;
			if (!(is_in_array(lpt1_valid_ports, 3, p)))  p = 0x378;
			w83877f_regs[reg] = (p >> 2);
			break;
		case 0x24:
			p = ((uint16_t) (w83877f_regs[reg] & 0xfe)) << 2;
			p &= 0xFF8;
			if ((p < 0x100) || (p > 0x3F8))  p = 0x3F8;
			if (!(is_in_array(com1_valid_ports, 9, p)))  p = 0x3F8;
			w83877f_regs[reg] = ((p >> 2) & 0xfe) | (w83877f_regs[reg] & 1);
			break;
		case 0x25:
			p = ((uint16_t) (w83877f_regs[reg] & 0xfe)) << 2;
			p &= 0xFF8;
			if ((p < 0x100) || (p > 0x3F8))  p = 0x2F8;
			if (!(is_in_array(com2_valid_ports, 9, p)))  p = 0x2F8;
			w83877f_regs[reg] = ((p >> 2) & 0xfe) | (w83877f_regs[reg] & 1);
			break;
	}

	return p;
}


void w83877f_serial_handler(int id)
{
	int reg_mask = (id - 1) ? 0x10 : 0x20;
	int reg_id = (id - 1) ? 0x24 : 0x25;
	int irq_mask = (id - 1) ? 0xF : 0xF0;

	/* pclog("Registers (%i): %02X %02X %02X\n", id, w83877f_regs[4], w83877f_regs[reg_id], w83877f_regs[0x28]); */

	if ((w83877f_regs[4] & reg_mask) || !(w83877f_regs[reg_id] & 0xc0))
	{
		serial_remove(id);
	}
	else
	{
		serial_setup(id, make_port(reg_id), w83877f_regs[0x28] & irq_mask);
	}
}


void w83877f_write(uint16_t port, uint8_t val, void *priv)
{
	uint8_t index = (port & 1) ? 0 : 1;
	uint8_t valxor = 0;
	uint8_t max = 0x2A;

	if (index)
	{
		if ((val == winbond_key) && !w83877f_locked)
		{
			if (winbond_key_times == 2)
			{
				if (tries)
				{
					w83877f_locked = 1;
					tries = 0;
				}
				else
				{
					tries++;
				}
			}
			else
			{
				w83877f_locked = 1;
				tries = 0;
			}
		}
		else
		{
			if (w83877f_locked)
			{
				if (val < max)  w83877f_curreg = val;
				if (val == 0xaa)
				{
					w83877f_locked = 0;
				}
			}
			else
			{
				if (tries)
					tries = 0;
			}
		}
	}
	else
	{
		if (w83877f_locked)
		{
			if (w83877f_rw_locked)  return;
			if ((w83877f_curreg >= 0x26) && (w83877f_curreg <= 0x27))  return;
			if (w83877f_curreg == 0x29)  return;
			if (w83877f_curreg == 6)  val &= 0xF3;
			valxor = val ^ w83877f_regs[w83877f_curreg];
			w83877f_regs[w83877f_curreg] = val;
			goto process_value;
		}
	}
	return;

process_value:
	switch(w83877f_curreg)
	{
		case 1:
			if (valxor & 0x80)
			{
				fdc_set_swap(w83877f_fdc, (w83877f_regs[1] & 0x80) ? 1 : 0);
			}
			break;
		case 4:
			if (valxor & 0x10)
			{
				w83877f_serial_handler(2);
			}
			if (valxor & 0x20)
			{
				w83877f_serial_handler(1);
			}
			if (valxor & 0x80)
			{
				lpt1_remove();
				if (!(w83877f_regs[4] & 0x80))  lpt1_init(make_port(0x23));
			}
			break;
		case 6:
			if (valxor & 0x08)
			{
				fdc_remove(w83877f_fdc);
				if (!(w83877f_regs[6] & 0x08))  fdc_set_base(w83877f_fdc, 0x03f0);
			}
			break;
		case 7:
			if (valxor & 3)  fdc_update_rwc(w83877f_fdc, 0, FDDA_TYPE);
			if (valxor & 0xC)  fdc_update_rwc(w83877f_fdc, 1, FDDB_TYPE);
			if (valxor & 0x30)  fdc_update_rwc(w83877f_fdc, 2, FDDC_TYPE);
			if (valxor & 0xC0)  fdc_update_rwc(w83877f_fdc, 3, FDDD_TYPE);
			break;
		case 8:
			if (valxor & 3)  fdc_update_boot_drive(w83877f_fdc, FD_BOOT);
			if (valxor & 0x10)  fdc_set_swwp(w83877f_fdc, SWWP ? 1 : 0);
			if (valxor & 0x20)  fdc_set_diswr(w83877f_fdc, DISFDDWR ? 1 : 0);
			break;
		case 9:
			if (valxor & 0x20)
			{
				fdc_update_enh_mode(w83877f_fdc, EN3MODE ? 1 : 0);
			}
			if (valxor & 0x40)
			{
				w83877f_rw_locked = (val & 0x40) ? 1 : 0;
			}
			break;
		case 0xB:
			if (valxor & 1)  fdc_update_drv2en(w83877f_fdc, DRV2EN_NEG ? 0 : 1);
			if (valxor & 2)  fdc_update_densel_polarity(w83877f_fdc, INVERTZ ? 1 : 0);
			break;
		case 0xC:
			if (valxor & 0x20)  w83877f_remap();
			break;
		case 0x16:
			if (valxor & 1)  w83877f_remap();
			break;
		case 0x23:
			if (valxor)
			{
				lpt1_remove();
				if (!(w83877f_regs[4] & 0x80))  lpt1_init(make_port(0x23));
			}
			break;
		case 0x24:
			if (valxor & 0xfe)
			{
				w83877f_serial_handler(1);
			}
			break;
		case 0x25:
			if (valxor & 0xfe)
			{
				w83877f_serial_handler(2);
			}
			break;
		case 0x28:
			if (valxor & 0xf)
			{
				if ((w83877f_regs[0x28] & 0xf) == 0)  w83877f_regs[0x28] |= 0x3;
				if (!(w83877f_regs[2] & 0x10))  serial_setup(2, make_port(0x25), w83877f_regs[0x28] & 0xF);
			}
			if (valxor & 0xf0)
			{
				if ((w83877f_regs[0x28] & 0xf0) == 0)  w83877f_regs[0x28] |= 0x40;
				if (!(w83877f_regs[4] & 0x20))
				{
					serial_setup(1, make_port(0x24), (w83877f_regs[0x28] & 0xF0) >> 8);
				}
			}
			break;
	}
}


uint8_t w83877f_read(uint16_t port, void *priv)
{
	uint8_t index = (port & 1) ? 0 : 1;

	if (!w83877f_locked)
	{
		return 0xff;
	}

	if (index)
	{
		return w83877f_curreg;
	}
	else
	{
		if ((w83877f_curreg < 0x18) && w83877f_rw_locked)  return 0xff;
		if (w83877f_curreg == 7)
			return (fdc_get_rwc(w83877f_fdc, 0) | (fdc_get_rwc(w83877f_fdc, 1) << 2));
		return w83877f_regs[w83877f_curreg];
	}
}


void w83877f_reset(void)
{
	lpt1_remove();
	lpt1_init(0x378);

	fdc_reset(w83877f_fdc);

	memset(w83877f_regs, 0, 0x2A);
	w83877f_regs[3] = 0x30;
	w83877f_regs[7] = 0xF5;
	w83877f_regs[9] = 0x0A;
	w83877f_regs[0xA] = 0x1F;
	w83877f_regs[0xC] = 0x28;
	w83877f_regs[0xD] = 0xA3;
	w83877f_regs[0x16] = (romset == ROM_PRESIDENT) ? 4 : 5;
	w83877f_regs[0x1E] = 0x81;
	w83877f_regs[0x20] = (0x3f0 >> 2) & 0xfc;
	w83877f_regs[0x21] = (0x1f0 >> 2) & 0xfc;
	w83877f_regs[0x22] = ((0x3f6 >> 2) & 0xfc) | 1;
	w83877f_regs[0x23] = (0x378 >> 2);
	w83877f_regs[0x24] = (0x3f8 >> 2) & 0xfe;
	w83877f_regs[0x25] = (0x2f8 >> 2) & 0xfe;
	w83877f_regs[0x26] = (2 << 4) | 4;
	w83877f_regs[0x27] = (6 << 4) | 7;
	w83877f_regs[0x28] = (4 << 4) | 3;
	w83877f_regs[0x29] = 0x62;

	serial_setup(1, SERIAL1_ADDR, SERIAL1_IRQ);
	serial_setup(2, SERIAL2_ADDR, SERIAL2_IRQ);
	w83877f_remap();
        w83877f_locked = 0;
        w83877f_rw_locked = 0;
}


void w83877f_init(void)
{
	w83877f_fdc = device_add(&fdc_at_winbond_device);

	lpt2_remove();

	w83877f_reset();
}
