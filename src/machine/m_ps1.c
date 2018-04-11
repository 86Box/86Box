/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the IBM PS/1 models 2011, 2121 and 2133.
 *
 * Model 2011:	The initial model, using a 10MHz 80286.
 *
 * Model 2121:	This is similar to model 2011 but some of the functionality
 *		has moved to a chip at ports 0xe0 (index)/0xe1 (data). The
 *		only functions I have identified are enables for the first
 *		512K and next 128K of RAM, in bits 0 of registers 0 and 1
 *		respectively.
 *
 *		Port 0x105 has bit 7 forced high. Without this 128K of
 *		memory will be missed by the BIOS on cold boots.
 *
 *		The reserved 384K is remapped to the top of extended memory.
 *		If this is not done then you get an error on startup.
 *
 * NOTES:	Floppy does not seem to work.  --FvK
 *		The "ROM DOS" shell does not seem to work. We do have the
 *		correct BIOS images now, and they do load, but they do not
 *		boot. Sometimes, they do, and then it shows an "Incorrect
 *		DOS" error message??  --FvK
 *
 * Version:	@(#)m_ps1.c	1.0.7	2018/03/18
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../cpu/cpu.h"
#include "../io.h"
#include "../dma.h"
#include "../pic.h"
#include "../pit.h"
#include "../mem.h"
#include "../nmi.h"
#include "../rom.h"
#include "../timer.h"
#include "../device.h"
#include "../nvr.h"
#include "../game/gameport.h"
#include "../lpt.h"
#include "../serial.h"
#include "../keyboard.h"
#include "../disk/hdc.h"
#include "../disk/hdc_ide.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"
#include "../sound/sound.h"
#include "../sound/snd_sn76489.h"
#include "../video/video.h"
#include "../video/vid_vga.h"
#include "../video/vid_ti_cf62011.h"
#include "machine.h"


typedef struct {
	sn76489_t	sn76489;
	uint8_t		status, ctrl;
	int64_t		timer_latch, timer_count, timer_enable;
	uint8_t		fifo[2048];
	int		fifo_read_idx, fifo_write_idx;
	int		fifo_threshold;
	uint8_t		dac_val;
	int16_t		buffer[SOUNDBUFLEN];
	int		pos;
} ps1snd_t;

typedef struct {
    int		model;

    rom_t	high_rom;

    uint8_t	ps1_92,
		ps1_94,
		ps1_102,
		ps1_103,
		ps1_104,
		ps1_105,
		ps1_190;
    int		ps1_e0_addr;
    uint8_t	ps1_e0_regs[256];

    struct {
	uint8_t status, int_status;
	uint8_t attention, ctrl;
    }		hd;
} ps1_t;


static void
update_irq_status(ps1snd_t *snd)
{
    if (((snd->status & snd->ctrl) & 0x12) && (snd->ctrl & 0x01))
	picint(1 << 7);
      else
	picintc(1 << 7);
}


static uint8_t
snd_read(uint16_t port, void *priv)
{
    ps1snd_t *snd = (ps1snd_t *)priv;
    uint8_t ret = 0xff;

    switch (port & 7) {
	case 0:		/* ADC data */
		snd->status &= ~0x10;
		update_irq_status(snd);
		ret = 0;
		break;

	case 2:		/* status */
		ret = snd->status;
		ret |= (snd->ctrl & 0x01);
		if ((snd->fifo_write_idx - snd->fifo_read_idx) >= 2048)
			ret |= 0x08; /* FIFO full */
		if (snd->fifo_read_idx == snd->fifo_write_idx)
			ret |= 0x04; /* FIFO empty */
		break;

	case 3:		/* FIFO timer */
		/*
		 * The PS/1 Technical Reference says this should return
		 * thecurrent value, but the PS/1 BIOS and Stunt Island
		 * expect it not to change.
		 */
		ret = snd->timer_latch;
		break;

	case 4:
	case 5:
	case 6:
	case 7:
		ret = 0;
    }

    return(ret);
}


static void
snd_write(uint16_t port, uint8_t val, void *priv)
{
    ps1snd_t *snd = (ps1snd_t *)priv;

    switch (port & 7) {
	case 0:		/* DAC output */
		if ((snd->fifo_write_idx - snd->fifo_read_idx) < 2048) {
			snd->fifo[snd->fifo_write_idx & 2047] = val;
			snd->fifo_write_idx++;
		}
		break;

	case 2:		/* control */
		snd->ctrl = val;
		if (! (val & 0x02))
			snd->status &= ~0x02;
		update_irq_status(snd);
		break;

	case 3:		/* timer reload value */
		snd->timer_latch = val;
		snd->timer_count = (int64_t) ((0xff-val) * TIMER_USEC);
		snd->timer_enable = (val != 0);
		break;

	case 4:		/* almost empty */
		snd->fifo_threshold = val * 4;
		break;
    }
}


static void
snd_update(ps1snd_t *snd)
{
    for (; snd->pos < sound_pos_global; snd->pos++)        
	snd->buffer[snd->pos] = (int8_t)(snd->dac_val ^ 0x80) * 0x20;
}


static void
snd_callback(void *priv)
{
    ps1snd_t *snd = (ps1snd_t *)priv;

    snd_update(snd);

    if (snd->fifo_read_idx != snd->fifo_write_idx) {
	snd->dac_val = snd->fifo[snd->fifo_read_idx & 2047];
	snd->fifo_read_idx++;
    }

    if ((snd->fifo_write_idx - snd->fifo_read_idx) == snd->fifo_threshold)
	snd->status |= 0x02; /*FIFO almost empty*/

    snd->status |= 0x10; /*ADC data ready*/
    update_irq_status(snd);

    snd->timer_count += snd->timer_latch * TIMER_USEC;
}


static void
snd_get_buffer(int32_t *buffer, int len, void *priv)
{
    ps1snd_t *snd = (ps1snd_t *)priv;
    int c;

    snd_update(snd);

    for (c = 0; c < len * 2; c++)
	buffer[c] += snd->buffer[c >> 1];

    snd->pos = 0;
}


static void *
snd_init(const device_t *info)
{
    ps1snd_t *snd;

    snd = malloc(sizeof(ps1snd_t));
    memset(snd, 0x00, sizeof(ps1snd_t));

    sn76489_init(&snd->sn76489, 0x0205, 0x0001, SN76496, 4000000);

    io_sethandler(0x0200, 1, snd_read,NULL,NULL, snd_write,NULL,NULL, snd);
    io_sethandler(0x0202, 6, snd_read,NULL,NULL, snd_write,NULL,NULL, snd);

    timer_add(snd_callback, &snd->timer_count, &snd->timer_enable, snd);

    sound_add_handler(snd_get_buffer, snd);

    return(snd);
}


static void
snd_close(void *priv)
{
    ps1snd_t *snd = (ps1snd_t *)priv;

    free(snd);
}


static const device_t snd_device = {
    "PS/1 Audio Card",
    0, 0,
    snd_init, snd_close, NULL,
    NULL,
    NULL,
    NULL,
    NULL
};


static void
recalc_memory(ps1_t *ps)
{
    /* Enable first 512K */
    mem_set_mem_state(0x00000, 0x80000,
		      (ps->ps1_e0_regs[0] & 0x01) ?
			(MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) :
			(MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL));

    /* Enable 512-640K */
    mem_set_mem_state(0x80000, 0x20000,
		      (ps->ps1_e0_regs[1] & 0x01) ?
			(MEM_READ_INTERNAL | MEM_WRITE_INTERNAL) :
			(MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL));
}


static void
ps1_write(uint16_t port, uint8_t val, void *priv)
{
    ps1_t *ps = (ps1_t *)priv;

    switch (port) {
	case 0x0092:
		if (ps->model != 2011) {
			if (val & 1) {
				softresetx86();
				cpu_set_edx();
			}
			ps->ps1_92 = val & ~1;
		} else {
			ps->ps1_92 = val;    
		}
		mem_a20_alt = val & 2;
		mem_a20_recalc();
		break;

	case 0x0094:
		ps->ps1_94 = val;
		break;

	case 0x00e0:
		if (ps->model != 2011) {
			ps->ps1_e0_addr = val;
		}
		break;

	case 0x00e1:
		if (ps->model != 2011) {
			ps->ps1_e0_regs[ps->ps1_e0_addr] = val;
			recalc_memory(ps);
		}
		break;

	case 0x0102:
		lpt1_remove();
		if (val & 0x04)
			serial_setup(1, SERIAL1_ADDR, SERIAL1_IRQ);
		  else
			serial_remove(1);
		if (val & 0x10) {
			switch ((val >> 5) & 3) {
				case 0:
					lpt1_init(0x03bc);
					break;
				case 1:
					lpt1_init(0x0378);
					break;
				case 2:
					lpt1_init(0x0278);
					break;
			}
		}
		ps->ps1_102 = val;
		break;

	case 0x0103:
		ps->ps1_103 = val;
		break;

	case 0x0104:
		ps->ps1_104 = val;
		break;

	case 0x0105:
		ps->ps1_105 = val;
		break;

	case 0x0190:
		ps->ps1_190 = val;
		break;

	case 0x0322:
		if (ps->model == 2011) {
			ps->hd.ctrl = val;
			if (val & 0x80)
				ps->hd.status |= 0x02;
		}
		break;

	case 0x0324:
		if (ps->model == 2011) {
			ps->hd.attention = val & 0xf0;
			if (ps->hd.attention)
				ps->hd.status = 0x14;
		}
		break;
    }
}


static uint8_t
ps1_read(uint16_t port, void *priv)
{
    ps1_t *ps = (ps1_t *)priv;
    uint8_t ret = 0xff;

    switch (port) {
	case 0x0091:
		ret = 0;
		break;

	case 0x0092:
		ret = ps->ps1_92;
		break;

	case 0x0094:
		ret = ps->ps1_94;
		break;

	case 0x00e1:
		if (ps->model != 2011) {
			ret = ps->ps1_e0_regs[ps->ps1_e0_addr];
		}
		break;

	case 0x0102:
		if (ps->model == 2011)
			ret = ps->ps1_102 | 0x08;
		  else
			ret = ps->ps1_102;
		break;

	case 0x0103:
		ret = ps->ps1_103;
		break;

	case 0x0104:
		ret = ps->ps1_104;
		break;

	case 0x0105:
		if (ps->model == 2011)
			ret = ps->ps1_105;
		  else
			ret = ps->ps1_105 | 0x80;
		break;

	case 0x0190:
		ret = ps->ps1_190;
		break;

	case 0x0322:
		if (ps->model == 2011) {
			ret = ps->hd.status;
		}
		break;

	case 0x0324:
		if (ps->model == 2011) {
			ret = ps->hd.int_status;
			ps->hd.int_status &= ~0x02;
		}
		break;

	default:
		break;
    }

    return(ret);
}


static void
ps1_setup(int model)
{
    ps1_t *ps;

    ps = (ps1_t *)malloc(sizeof(ps1_t));
    memset(ps, 0x00, sizeof(ps1_t));
    ps->model = model;

    io_sethandler(0x0091, 1,
		  ps1_read, NULL, NULL, ps1_write, NULL, NULL, ps);
    io_sethandler(0x0092, 1,
		  ps1_read, NULL, NULL, ps1_write, NULL, NULL, ps);
    io_sethandler(0x0094, 1,
		  ps1_read, NULL, NULL, ps1_write, NULL, NULL, ps);
    io_sethandler(0x0102, 4,
		  ps1_read, NULL, NULL, ps1_write, NULL, NULL, ps);
    io_sethandler(0x0190, 1,
		  ps1_read, NULL, NULL, ps1_write, NULL, NULL, ps);

    if (model == 2011) {
	io_sethandler(0x0320, 1,
		      ps1_read, NULL, NULL, ps1_write, NULL, NULL, ps);
	io_sethandler(0x0322, 1,
		      ps1_read, NULL, NULL, ps1_write, NULL, NULL, ps);
	io_sethandler(0x0324, 1,
		      ps1_read, NULL, NULL, ps1_write, NULL, NULL, ps);

#if 0
	rom_init(&ps->high_rom,
		 L"roms/machines/ibmps1es/f80000_shell.bin",
		 0xf80000, 0x80000, 0x7ffff, 0, MEM_MAPPING_EXTERNAL);
#endif

	lpt1_remove();
	lpt2_remove();
	lpt1_init(0x03bc);

	serial_remove(1);
	serial_remove(2);

	/* Enable the PS/1 VGA controller. */
	if (model == 2011)
		device_add(&ps1vga_device);
	else
		device_add(&ibm_ps1_2121_device);
    }

    if (model == 2121) {
	io_sethandler(0x00e0, 2,
		      ps1_read, NULL, NULL, ps1_write, NULL, NULL, ps);

#if 1
	rom_init(&ps->high_rom,
		 L"roms/machines/ibmps1_2121/fc0000.bin",
		 0xfc0000, 0x20000, 0x1ffff, 0, MEM_MAPPING_EXTERNAL);
#else
	rom_init(&ps->high_rom,
		 L"roms/machines/ibmps1_2121/fc0000_shell.bin",
		 0xfc0000, 0x40000, 0x3ffff, 0, MEM_MAPPING_EXTERNAL);
#endif

	lpt1_init(0x03bc);

	/* Initialize the video controller. */
	if (gfxcard == GFX_INTERNAL)
		device_add(&ibm_ps1_2121_device);
    }

    if (model == 2133) {
	lpt1_init(0x03bc);
    }
}


static void
ps1_common_init(const machine_t *model)
{
    machine_common_init(model);

    mem_remap_top_384k();

    pit_set_out_func(&pit, 1, pit_refresh_timer_at);

    dma16_init();
    pic2_init();

    nvr_at_init(8);

    if (romset != ROM_IBMPS1_2011)
	device_add(&ide_isa_device);

    device_add(&keyboard_ps2_device);

    if (romset == ROM_IBMPS1_2133)
	device_add(&fdc_at_device);
    else {
	if ((romset == ROM_IBMPS1_2121) || (romset == ROM_IBMPS1_2121_ISA))
		device_add(&fdc_at_ps1_device);
	else
		device_add(&fdc_at_actlow_device);
	device_add(&snd_device);
    }

    /* Audio uses ports 200h and 202-207h, so only initialize gameport on 201h. */
    if (joystick_type != 7)
	device_add(&gameport_201_device);
}


void
machine_ps1_m2011_init(const machine_t *model)
{
    ps1_common_init(model);

    ps1_setup(2011);
}


void
machine_ps1_m2121_init(const machine_t *model)
{
    ps1_common_init(model);

    ps1_setup(2121);
}


void
machine_ps1_m2133_init(const machine_t *model)
{
    ps1_common_init(model);

    ps1_setup(2133);

    nmi_mask = 0x80;
}
