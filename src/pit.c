/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Intel 8253/8254 Programmable Interval
 *		Timer.
 *
 *
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2019 Miran Grca.
 */
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/cassette.h>
#include <86box/dma.h>
#include <86box/io.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/ppi.h>
#include <86box/machine.h>
#include <86box/sound.h>
#include <86box/snd_speaker.h>
#include <86box/video.h>


pit_t		*pit, *pit2;
double		cpuclock, PITCONSTD,
		SYSCLK,
		isa_timing,
		bus_timing, pci_timing, agp_timing,
		PCICLK, AGPCLK;

uint64_t	PITCONST, ISACONST,
		CGACONST,
		MDACONST, HERCCONST,
		VGACONST1, VGACONST2,
		RTCCONST, ACPICONST;

int		refresh_at_enable = 1,
		io_delay = 5;


int64_t		firsttime = 1;


#define PIT_PS2			16	/* The PIT is the PS/2's second PIT. */
#define PIT_EXT_IO		32	/* The PIT has externally specified port I/O. */
#define PIT_CUSTOM_CLOCK	64	/* The PIT uses custom clock inputs provided by another provider. */
#define PIT_SECONDARY		128	/* The PIT is secondary (ports 0048-004B). */


enum {
    PIT_8253 = 0,
    PIT_8254
};


#ifdef ENABLE_PIT_LOG
int pit_do_log = ENABLE_PIT_LOG;


static void
pit_log(const char *fmt, ...)
{
    va_list ap;

    if (pit_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define pit_log(fmt, ...)
#endif


static void
ctr_set_out(ctr_t *ctr, int out)
{
    if (ctr == NULL)
	return;

    if (ctr->out_func != NULL)
	ctr->out_func(out, ctr->out);
    ctr->out = out;
}


static void
ctr_decrease_count(ctr_t *ctr)
{
    if (ctr->bcd) {
	ctr->units--;
	if (ctr->units == 0xff) {
		ctr->units = 9;
		ctr->tens--;
		if (ctr->tens == 0xff) {
			ctr->tens = 9;
			ctr->hundreds--;
			if (ctr->hundreds == 0xff) {
				ctr->hundreds = 9;
				ctr->thousands--;
				if (ctr->thousands == 0xff) {
					ctr->thousands = 9;
					ctr->myriads--;
					if (ctr->myriads == 0xff)
						ctr->myriads = 0;	/* 0 - 1 should wrap around to 9999. */
				}
			}
		}
	}
    } else
	ctr->count = (ctr->count - 1) & 0xffff;
}


static void
ctr_load_count(ctr_t *ctr)
{
    int l = ctr->l ? ctr->l : 0x10000;

    ctr->count = l;
    pit_log("ctr->count = %i\n", l);
    ctr->null_count = 0;
    ctr->newcount = !!(l & 1);
}


static void
ctr_tick(ctr_t *ctr)
{
    uint8_t state = ctr->state;

    if (state == 1) {
	/* This is true for all modes */
	ctr_load_count(ctr);
	ctr->state = 2;
	return;
    }

    switch(ctr->m & 0x07) {
	case 0:
		/* Interrupt on terminal count */
		switch (state) {
			case 2:
				if (ctr->gate && (ctr->count >= 1)) {
					ctr_decrease_count(ctr);
					if (ctr->count < 1) {
						ctr->state = 3;
						ctr_set_out(ctr, 1);
					}
				}
				break;
			case 3:
				ctr_decrease_count(ctr);
				break;
		}
		break;
	case 1:
		/* Hardware retriggerable one-shot */
		switch (state) {
			case 1:
				ctr_load_count(ctr);
				ctr->state = 2;
				ctr_set_out(ctr, 0);
				break;
			case 2:
				if (ctr->count >= 1) {
					ctr_decrease_count(ctr);
					if (ctr->count < 1) {
						ctr->state = 3;
						ctr_set_out(ctr, 1);
					}
				}
				break;
			case 3:
				ctr_decrease_count(ctr);
				break;
		}
		break;
	case 2: case 6:
		/* Rate generator */
		switch (state) {
			case 3:
				ctr_load_count(ctr);
				ctr->state = 2;
				ctr_set_out(ctr, 1);
				break;
			case 2:
				if (ctr->gate == 0)
					break;
				else if (ctr->count >= 2) {
					ctr_decrease_count(ctr);
					if (ctr->count < 2) {
						ctr->state = 3;
						ctr_set_out(ctr, 0);
					}
				}
				break;
		}
		break;
	case 3: case 7:
		/* Square wave mode */
		switch (state) {
			case 2:
				if (ctr->gate == 0)
					break;
				else if (ctr->count >= 0) {
					if (ctr->bcd) {
						ctr_decrease_count(ctr);
						if (!ctr->newcount)
							ctr_decrease_count(ctr);
					} else
						ctr->count -= (ctr->newcount ? 1 : 2);
					if (ctr->count < 0) {
						ctr_load_count(ctr);
						ctr->state = 3;
						ctr_set_out(ctr, 0);
					} else if (ctr->newcount)
						ctr->newcount = 0;
				}
				break;
			case 3:
				if (ctr->gate == 0)
					break;
				else if (ctr->count >= 0) {
					if (ctr->bcd) {
						ctr_decrease_count(ctr);
						ctr_decrease_count(ctr);
						if (ctr->newcount)
							ctr_decrease_count(ctr);
					} else
						ctr->count -= (ctr->newcount ? 3 : 2);
					if (ctr->count < 0) {
						ctr_load_count(ctr);
						ctr->state = 2;
						ctr_set_out(ctr, 1);
					} else if (ctr->newcount)
						ctr->newcount = 0;
				}
				break;
		}
		break;
	case 4: case 5:
		/* Software triggered strobe */
		/* Hardware triggered strobe */
		if ((ctr->gate != 0) || (ctr->m != 4)) {
			switch(state) {
				case 0:
					ctr_decrease_count(ctr);
					break;
				case 2:
					if (ctr->count >= 1) {
						ctr_decrease_count(ctr);
						if (ctr->count < 1) {
							ctr->state = 3;
							ctr_set_out(ctr, 0);
						}
					}
					break;
				case 3:
					ctr->state = 0;
					ctr_set_out(ctr, 1);
					break;
			}
		}
		break;
	default:
		break;
    }
}


static void
ctr_clock(ctr_t *ctr)
{
   /* FIXME: Is this even needed? */
    if ((ctr->state == 3) && (ctr->m != 2) && (ctr->m != 3))
	return;

    if (ctr->using_timer)
	return;

    ctr_tick(ctr);
}


static void
ctr_set_state_1(ctr_t *ctr)
{
    uint8_t mode = (ctr->m & 0x03);

    if ((mode == 0) || ((mode > 1) && (ctr->state == 0)))
	ctr->state = 1;
}


static void
ctr_load(ctr_t *ctr)
{
    if (ctr->l == 1) {
	/* Count of 1 is illegal in modes 2 and 3. What happens here was
	   determined experimentally. */
	if (ctr->m == 2)
		ctr->l = 2;
	else if (ctr->m == 3)
		ctr->l = 0;
    }

    if (ctr->using_timer)
	ctr->latch = 1;
    else
	ctr_set_state_1(ctr);

    if (ctr->load_func != NULL)
	ctr->load_func(ctr->m, ctr->l ? ctr->l : 0x10000);

    pit_log("Counter loaded, state = %i, gate = %i\n", ctr->state, ctr->gate);
}


static __inline void
ctr_latch_status(ctr_t *ctr)
{
    ctr->read_status = (ctr->ctrl & 0x3f) | (ctr->out ? 0x80 : 0) | (ctr->null_count ? 0x40 : 0);
    ctr->do_read_status = 1;
}


static __inline void
ctr_latch_count(ctr_t *ctr)
{
    int count = (ctr->latch || (ctr->state == 1)) ? ctr->l : ctr->count;

    switch (ctr->rm & 0x03) {
	case 0x00:
		/* This should never happen. */
		break;
	case 0x01:
		/* Latch bits 0-7 only. */
		ctr->rl = ((count << 8) & 0xff00) | (count & 0xff);
		ctr->latched = 1;
		break;
	case 0x02:
		/* Latch bit 8-15 only. */
		ctr->rl = (count & 0xff00) | ((count >> 8) & 0xff);
		ctr->latched = 1;
		break;
	case 0x03:
		/* Latch all 16 bits. */
		ctr->rl = count;
		ctr->latched = 2;
		break;
    }

    pit_log("latched counter = %04X\n", ctr->rl & 0xffff);
}


uint16_t
pit_ctr_get_count(ctr_t *ctr)
{
    return (uint16_t) ctr->l;
}


void
pit_ctr_set_load_func(ctr_t *ctr, void (*func)(uint8_t new_m, int new_count))
{
    if (ctr == NULL)
	return;

    ctr->load_func = func;
}


void
pit_ctr_set_out_func(ctr_t *ctr, void (*func)(int new_out, int old_out))
{
    if (ctr == NULL)
	return;

    ctr->out_func = func;
}


void
pit_ctr_set_gate(ctr_t *ctr, int gate)
{
    int old = ctr->gate;
    uint8_t mode = ctr->m & 3;

    ctr->gate = gate;

    switch (mode) {
	case 1: case 2: case 3: case 5: case 6: case 7:
		if (!old && gate) {
			/* Here we handle the rising edges. */
			if (mode & 1) {
				if (mode != 1)
					ctr_set_out(ctr, 1);
				ctr->state = 1;
			} else if (mode == 2)
				ctr->state = 3;
		} else if (old && !gate) {
			/* Here we handle the lowering edges. */
			if (mode & 2)
				ctr_set_out(ctr, 1);
		}
		break;
   }
}


static __inline void
pit_ctr_set_clock_common(ctr_t *ctr, int clock)
{
    int old = ctr->clock;

    ctr->clock = clock;

    if (ctr->using_timer && ctr->latch) {
	if (old && !ctr->clock) {
		ctr_set_state_1(ctr);
		ctr->latch = 0;
	}
    } else if (ctr->using_timer && !ctr->latch) {
	if (ctr->state == 1) {
		if (!old && ctr->clock)
			ctr->s1_det = 1;	/* Rising edge. */
		else if (old && !ctr->clock) {
			ctr->s1_det++;		/* Falling edge. */
			if (ctr->s1_det >= 2) {
				ctr->s1_det = 0;
				ctr_tick(ctr);
			}
		}
	} else if (old && !ctr->clock)
		ctr_tick(ctr);
    }
}


void
pit_ctr_set_clock(ctr_t *ctr, int clock)
{
    pit_ctr_set_clock_common(ctr, clock);
}


void
pit_ctr_set_using_timer(ctr_t *ctr, int using_timer)
{
    timer_process();

    ctr->using_timer = using_timer;
}


static void
pit_timer_over(void *p)
{
    pit_t *dev = (pit_t *) p;
    int i;

    dev->clock ^= 1;

    for (i = 0; i < 3; i++)
	pit_ctr_set_clock_common(&dev->counters[i], dev->clock);

    timer_advance_u64(&dev->callback_timer, PITCONST >> 1ULL);
}


static void
pit_write(uint16_t addr, uint8_t val, void *priv)
{
    pit_t *dev = (pit_t *)priv;
    int t = (addr & 3);
    ctr_t *ctr;

    pit_log("[%04X:%08X] pit_write(%04X, %02X, %08X)\n", CS, cpu_state.pc, addr, val, priv);

    switch (addr & 3) {
	case 3:		/* control */
		t = val >> 6;

		if (t == 3) {
			if (dev->flags & PIT_8254) {
				/* This is 8254-only. */
				if (!(val & 0x20)) {
					if (val & 2)
						ctr_latch_count(&dev->counters[0]);
					if (val & 4)
						ctr_latch_count(&dev->counters[1]);
					if (val & 8)
						ctr_latch_count(&dev->counters[2]);
					pit_log("PIT %i: Initiated readback command\n", t);
				}
				if (!(val & 0x10)) {
					if (val & 2)
						ctr_latch_status(&dev->counters[0]);
					if (val & 4)
						ctr_latch_status(&dev->counters[1]);
					if (val & 8)
						ctr_latch_status(&dev->counters[2]);
				}
			}
		} else {
			dev->ctrl = val;
			ctr = &dev->counters[t];

			if (!(dev->ctrl & 0x30)) {
				ctr_latch_count(ctr);
				pit_log("PIT %i: Initiated latched read, %i bytes latched\n",
					t, ctr->latched);
			} else {
				ctr->ctrl = val;
				ctr->rm = ctr->wm = (ctr->ctrl >> 4) & 3;
				ctr->m = (val >> 1) & 7;
				if (ctr->m > 5)
					ctr->m &= 3;
				ctr->null_count = 1;
				ctr->bcd = (ctr->ctrl & 0x01);
				ctr_set_out(ctr, !!ctr->m);
				ctr->state = 0;
				if (ctr->latched) {
					pit_log("PIT %i: Reload while counter is latched\n", t);
					ctr->rl--;
				}

				pit_log("PIT %i: M = %i, RM/WM = %i, State = %i, Out = %i\n", t, ctr->m, ctr->rm, ctr->state, ctr->out);
			}
		}
		break;

	case 0:
	case 1:
	case 2:		/* the actual timers */
		ctr = &dev->counters[t];

		switch (ctr->wm) {
			case 0:
				/* This should never happen. */
				break;
			case 1:
				ctr->l = val;
				if (ctr->m == 0)
					ctr_set_out(ctr, 0);
				ctr_load(ctr);
				break;
			case 2:
				ctr->l = (val << 8);
				if (ctr->m == 0)
					ctr_set_out(ctr, 0);
				ctr_load(ctr);
				break;
			case 3: case 0x83:
				if (ctr->wm & 0x80) {
					ctr->l = (ctr->l & 0x00ff) | (val << 8);
					pit_log("PIT %i: Written high byte %02X, latch now %04X\n", t, val, ctr->l);
					ctr_load(ctr);
				} else {
					ctr->l = (ctr->l & 0xff00) | val;
					pit_log("PIT %i: Written low byte %02X, latch now %04X\n", t, val, ctr->l);
					if (ctr->m == 0) {
						ctr->state = 0;
						ctr_set_out(ctr, 0);
					}
				}

				if (ctr->wm & 0x80)
					ctr->wm &= ~0x80;
				else
					ctr->wm |= 0x80;
				break;
		}
		break;
    }
}


static uint8_t
pit_read(uint16_t addr, void *priv)
{
    pit_t *dev = (pit_t *)priv;
    uint8_t ret = 0xff;
    int count, t = (addr & 3);
    ctr_t *ctr;

    switch (addr & 3) {
	case 3:		/* Control. */
		/* This is 8254-only, 8253 returns 0x00. */
		ret = (dev->flags & PIT_8254) ? dev->ctrl : 0x00;
		break;

	case 0:
	case 1:
	case 2:		/* The actual timers. */
		ctr = &dev->counters[t];

		if (ctr->do_read_status) {
			ctr->do_read_status = 0;
			ret = ctr->read_status;
			break;
		}

		count = (ctr->state == 1) ? ctr->l : ctr->count;

		if (ctr->latched) {
			ret = (ctr->rl) >> ((ctr->rm & 0x80) ? 8 : 0);

			if (ctr->rm & 0x80)
				ctr->rm &= ~0x80;
			else
				ctr->rm |= 0x80;

			ctr->latched--;
		} else switch (ctr->rm) {
			case 0: case 0x80:
				ret = 0x00;
				break;

			case 1:
				ret = count & 0xff;
				break;

			case 2:
				ret = count >> 8;
				break;

			case 3: case 0x83:
				/* Yes, wm is correct here - this is to ensure correct readout while the
				   count is being written. */
				if (ctr->wm & 0x80)
					ret = ~(ctr->l & 0xff);
				else
					ret = count >> ((ctr->rm & 0x80) ? 8 : 0);

				if (ctr->rm & 0x80)
					ctr->rm &= ~0x80;
				else
					ctr->rm |= 0x80;
				break;
		}
		break;
    }

    pit_log("[%04X:%08X] pit_read(%04X, %08X) = %02X\n", CS, cpu_state.pc, addr, priv, ret);

    return ret;
}


/* FIXME: Should be moved to machine.c (default for most machine). */
void
pit_irq0_timer(int new_out, int old_out)
{
    if (new_out && !old_out)
	picint(1);

    if (!new_out)
	picintc(1);
}


void
pit_irq0_timer_pcjr(int new_out, int old_out)
{
    if (new_out && !old_out) {
	picint(1);
	ctr_clock(&pit->counters[1]);
    }

    if (!new_out)
	picintc(1);
}


void
pit_irq0_timer_ps2(int new_out, int old_out)
{
    ctr_t *ctr = &pit2->counters[0];

    if (new_out && !old_out) {
	picint(1);
	pit_ctr_set_gate(ctr, 1);
    }

    if (!new_out)
	picintc(1);

    if (!new_out && old_out)
	ctr_clock(ctr);
}


void
pit_refresh_timer_xt(int new_out, int old_out)
{
    if (new_out && !old_out)
	dma_channel_read(0);
}


void
pit_refresh_timer_at(int new_out, int old_out)
{
    if (refresh_at_enable && new_out && !old_out)
	ppi.pb ^= 0x10;
}


void
pit_speaker_timer(int new_out, int old_out)
{
    int l;

    if (cassette != NULL)
	pc_cas_set_out(cassette, new_out);

    speaker_update();

    l = pit->counters[2].l ? pit->counters[2].l : 0x10000;
    if (l < 25)
	speakon = 0;
    else
	speakon = new_out;

    ppispeakon = new_out;
}


void
pit_nmi_timer_ps2(int new_out, int old_out)
{
    nmi = new_out;

    if (nmi)
	nmi_auto_clear = 1;
}


static void
ctr_reset(ctr_t *ctr)
{
    ctr->ctrl = 0;
    ctr->m = 0;
    ctr->gate = 0;
    ctr->l = 0xffff;
    ctr->using_timer = 1;
    ctr->state = 0;
    ctr->null_count = 1;

    ctr->latch = 0;

    ctr->s1_det = 0;
    ctr->l_det = 0;
}


void
pit_reset(pit_t *dev)
{
    int i;

    memset(dev, 0, sizeof(pit_t));

    dev->clock = 0;

    for (i = 0; i < 3; i++)
	ctr_reset(&dev->counters[i]);

    /* Disable speaker gate. */
    dev->counters[2].gate = 0;
}


void
pit_handler(int set, uint16_t base, int size, void *priv)
{
    io_handler(set, base, size, pit_read, NULL, NULL, pit_write, NULL, NULL, priv);
}


static void
pit_close(void *priv)
{
    pit_t *dev = (pit_t *) priv;

    if (dev == pit)
	pit = NULL;

    if (dev == pit2)
	pit2 = NULL;

    if (dev != NULL)
	free(dev);
}


static void *
pit_init(const device_t *info)
{
    pit_t *dev = (pit_t *) malloc(sizeof(pit_t));
    pit_reset(dev);

    if (!(dev->flags & PIT_PS2) && !(dev->flags & PIT_CUSTOM_CLOCK)) {
	timer_add(&dev->callback_timer, pit_timer_over, (void *) dev, 0);
	timer_set_delay_u64(&dev->callback_timer, PITCONST >> 1ULL);
    }

    dev->flags = info->local;

    if (!(dev->flags & PIT_EXT_IO)) {
	io_sethandler((dev->flags & PIT_SECONDARY) ? 0x0048 : 0x0040, 0x0004,
		      pit_read, NULL, NULL, pit_write, NULL, NULL, dev);
    }

    return dev;
}


const device_t i8253_device =
{
        "Intel 8253/8253-5 Programmable Interval Timer",
        DEVICE_ISA,
	PIT_8253,
        pit_init, pit_close, NULL,
        { NULL }, NULL, NULL,
	NULL
};


const device_t i8254_device =
{
        "Intel 8254 Programmable Interval Timer",
        DEVICE_ISA,
	PIT_8254,
        pit_init, pit_close, NULL,
        { NULL }, NULL, NULL,
	NULL
};


const device_t i8254_sec_device =
{
        "Intel 8254 Programmable Interval Timer (Secondary)",
        DEVICE_ISA,
	PIT_8254 | PIT_SECONDARY,
        pit_init, pit_close, NULL,
        { NULL }, NULL, NULL,
	NULL
};


const device_t i8254_ext_io_device =
{
        "Intel 8254 Programmable Interval Timer (External I/O)",
        DEVICE_ISA,
	PIT_8254 | PIT_EXT_IO,
        pit_init, pit_close, NULL,
        { NULL }, NULL, NULL,
	NULL
};


const device_t i8254_ps2_device =
{
        "Intel 8254 Programmable Interval Timer (PS/2)",
        DEVICE_ISA,
	PIT_8254 | PIT_PS2 | PIT_EXT_IO,
        pit_init, pit_close, NULL,
        { NULL }, NULL, NULL,
	NULL
};


pit_t *
pit_common_init(int type, void (*out0)(int new_out, int old_out), void (*out1)(int new_out, int old_out))
{
    int i;

    switch (type) {
	case PIT_8253:
	default:
		pit = device_add(&i8253_device);
		break;
	case PIT_8254:
		pit = device_add(&i8254_device);
		break;
    }

    for (i = 0; i < 3; i++) {
	pit->counters[i].gate = 1;
	pit->counters[i].using_timer = 1;
    }

    pit_ctr_set_out_func(&pit->counters[0], out0);
    pit_ctr_set_out_func(&pit->counters[1], out1);
    pit_ctr_set_out_func(&pit->counters[2], pit_speaker_timer);
    pit_ctr_set_load_func(&pit->counters[2], speaker_set_count);
    pit->counters[2].gate = 0;

    return pit;
}


pit_t *
pit_ps2_init(void)
{
    pit2 = device_add(&i8254_ps2_device);

    pit_handler(1, 0x0044, 0x0001, pit2);
    pit_handler(1, 0x0047, 0x0001, pit2);

    pit2->counters[0].gate = 0;
    pit2->counters[0].using_timer = pit2->counters[1].using_timer = pit2->counters[2].using_timer = 0;

    pit_ctr_set_out_func(&pit->counters[0], pit_irq0_timer_ps2);
    pit_ctr_set_out_func(&pit2->counters[0], pit_nmi_timer_ps2);

    return pit2;
}


void
pit_set_clock(int clock)
{
    /* Set default CPU/crystal clock and xt_cpu_multi. */
    if (cpu_s->cpu_type >= CPU_286) {
	int remainder = (clock % 100000000);
	if (remainder == 66666666)
		cpuclock = (double) (clock - remainder) + (200000000.0 / 3.0);
	else if (remainder == 33333333)
		cpuclock = (double) (clock - remainder) + (100000000.0 / 3.0);
	else
		cpuclock = (double) clock;

	PITCONSTD = (cpuclock / 1193182.0);
	PITCONST = (uint64_t) (PITCONSTD * (double)(1ull << 32));
	CGACONST = (uint64_t) ((cpuclock / (19687503.0/11.0)) * (double)(1ull << 32));
	ISACONST = (uint64_t) ((cpuclock / (double)cpu_isa_speed) * (double)(1ull << 32));
	xt_cpu_multi = 1ULL;
    } else {
	cpuclock = 14318184.0;
	PITCONSTD = 12.0;
       	PITCONST = (12ULL << 32ULL);
        CGACONST = (8ULL << 32ULL);
	xt_cpu_multi = 3ULL;

	switch (cpu_s->rspeed) {
		case 7159092:
			if (cpu_s->cpu_flags & CPU_ALTERNATE_XTAL) {
				cpuclock = 28636368.0;
				xt_cpu_multi = 4ULL;
			} else
				xt_cpu_multi = 2ULL;
			break;

		case 8000000:
			cpuclock = 24000000.0;
			break;
		case 9545456:
			cpuclock = 28636368.0;
			break;
		case 10000000:
			cpuclock = 30000000.0;
			break;
		case 12000000:
			cpuclock = 36000000.0;
			break;
		case 16000000:
			cpuclock = 48000000.0;
			break;

		default:
			if (cpu_s->cpu_flags & CPU_ALTERNATE_XTAL) {
				cpuclock = 28636368.0;
				xt_cpu_multi = 6ULL;
			}
			break;
	}

	if (cpuclock == 28636368.0) {
		PITCONSTD = 24.0;
        	PITCONST = (24ULL << 32LL);
	        CGACONST = (16ULL << 32LL);
	} else if (cpuclock != 14318184.0) {
		PITCONSTD = (cpuclock / 1193182.0);
		PITCONST = (uint64_t) (PITCONSTD * (double)(1ull << 32));
		CGACONST = (uint64_t) (((cpuclock/(19687503.0/11.0)) * (double)(1ull << 32)));
	}

	ISACONST = (1ULL << 32ULL);
    }
    xt_cpu_multi <<= 32ULL;

    /* Delay for empty I/O ports. */
    io_delay = (int) round(((double) cpu_s->rspeed) / 3000000.0);

    MDACONST = (uint64_t) (cpuclock / 2032125.0 * (double)(1ull << 32));
    HERCCONST = MDACONST;
    VGACONST1 = (uint64_t) (cpuclock / 25175000.0 * (double)(1ull << 32));
    VGACONST2 = (uint64_t) (cpuclock / 28322000.0 * (double)(1ull << 32));
    RTCCONST = (uint64_t) (cpuclock / 32768.0 * (double)(1ull << 32));
    ACPICONST = (uint64_t) (cpuclock / 3579545.0 * (double)(1ull << 32));

    TIMER_USEC = (uint64_t)((cpuclock / 1000000.0) * (double)(1ull << 32));

    isa_timing = (cpuclock / (double)cpu_isa_speed);
    if (cpu_64bitbus)
	bus_timing = (cpuclock / ((double)cpu_busspeed / 2));
    else
	bus_timing = (cpuclock / (double)cpu_busspeed);	    
    pci_timing = (cpuclock / (double)cpu_pci_speed);
    agp_timing = (cpuclock / (double)cpu_agp_speed);

    /* PCICLK in us for use with timer_on_auto(). */
    PCICLK = pci_timing / (cpuclock / 1000000.0);
    AGPCLK = agp_timing / (cpuclock / 1000000.0);

    if (cpu_busspeed >= 30000000)
	SYSCLK = bus_timing * 4.0;
    else
	SYSCLK = bus_timing * 3.0;

    video_update_timing();

    device_speed_changed();
}
