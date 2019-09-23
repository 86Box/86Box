/*IBM AT -
  Write B0
  Write aa55
  Expects aa55 back*/
#include <inttypes.h>
#if 1
#include <math.h>
#endif
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "86box.h"
#include "cpu/cpu.h"
#include "dma.h"
#include "io.h"
#include "nmi.h"
#include "pic.h"
#include "timer.h"
#include "pit.h"
#include "ppi.h"
#include "device.h"
#include "timer.h"
#include "machine/machine.h"
#include "sound/sound.h"
#include "sound/snd_speaker.h"
#include "video/video.h"


PIT		pit, pit2;
double		cpuclock, PITCONSTD,
		SYSCLK,
		isa_timing, bus_timing;

uint64_t	PITCONST, ISACONST,
		CGACONST,
		MDACONST, HERCCONST,
		VGACONST1, VGACONST2,
		RTCCONST;

int		io_delay = 5;


int64_t		firsttime = 1;


static void	pit_dump_and_disable_timer(PIT *pit, int t);


static void
pit_set_out(PIT *pit, int t, int out)
{
    pit->set_out_funcs[t](out, pit->out[t]);
    pit->out[t] = out;
}


static void
pit_set_gate_no_timer(PIT *pit, int t, int gate)
{
    int64_t l = pit->l[t] ? pit->l[t] : 0x10000LL;

    if (pit->disabled[t]) {
	pit->gate[t] = gate;
	return;
    }

     switch (pit->m[t]) {
	case 0: /*Interrupt on terminal count*/
	case 4: /*Software triggered strobe*/
		if (pit->using_timer[t] && !pit->running[t])
			timer_set_delay_u64(&pit->timer[t], (uint64_t)(l * PITCONST));
		pit->enabled[t] = gate;
		break;
	case 1: /*Hardware retriggerable one-shot*/
	case 5: /*Hardware triggered stobe*/
		if (gate && !pit->gate[t]) {
			pit->count[t] = l;
			if (pit->using_timer[t])
				timer_set_delay_u64(&pit->timer[t], (uint64_t)(l * PITCONST));
			pit_set_out(pit, t, 0);
			pit->thit[t] = 0;
			pit->enabled[t] = 1;
		}
		break;
	case 2: /*Rate generator*/
		if (gate && !pit->gate[t]) {
			pit->count[t] = l - 1;
			if (pit->using_timer[t])
				timer_set_delay_u64(&pit->timer[t], (uint64_t)(l * PITCONST));
			pit_set_out(pit, t, 1);
			pit->thit[t] = 0;
		}
		pit->enabled[t] = gate;
		break;
	case 3: /*Square wave mode*/
		if (gate && !pit->gate[t]) {
			pit->count[t] = l;
			if (pit->using_timer[t])
				timer_set_delay_u64(&pit->timer[t], (uint64_t)(((l + 1) >> 1) * PITCONST));
			pit_set_out(pit, t, 1);
			pit->thit[t] = 0;
		}
		pit->enabled[t] = gate;
		break;
    }

    pit->gate[t] = gate;
    pit->running[t] = pit->enabled[t] && pit->using_timer[t] && !pit->disabled[t];
    if (pit->using_timer[t] && !pit->running[t])
	pit_dump_and_disable_timer(pit, t);
}


static void
pit_over(PIT *pit, int t)
{
    int64_t l = pit->l[t] ? pit->l[t] : 0x10000;

    if (pit->disabled[t]) {
	pit->count[t] += 0xffff;
	if (pit->using_timer[t])
		timer_advance_u64(&pit->timer[t], (uint64_t)(0xffff * PITCONST));
	return;
    }

    switch (pit->m[t]) {
	case 0: /*Interrupt on terminal count*/
	case 1: /*Hardware retriggerable one-shot*/
		if (!pit->thit[t])
			pit_set_out(pit, t, 1);
		pit->thit[t] = 1;
		pit->count[t] += 0xffff;
		if (pit->using_timer[t])
	                timer_advance_u64(&pit->timer[t], (uint64_t)(0xffff * PITCONST));
		break;
	case 2: /*Rate generator*/
		pit->count[t] += l;
		if (pit->using_timer[t])
			timer_advance_u64(&pit->timer[t], (uint64_t)(l * PITCONST));
		pit_set_out(pit, t, 0);
		pit_set_out(pit, t, 1);
		break;
	case 3: /*Square wave mode*/
		if (pit->out[t]) {
			pit_set_out(pit, t, 0);
			pit->count[t] += (l >> 1);
			if (pit->using_timer[t])
				timer_advance_u64(&pit->timer[t], (uint64_t)((l >> 1) * PITCONST));
		} else {
			pit_set_out(pit, t, 1);
			pit->count[t] += ((l + 1) >> 1);
			if (pit->using_timer[t])
				timer_advance_u64(&pit->timer[t], (uint64_t)(((l + 1) >> 1) * PITCONST));
		}
		break;
	case 4: /*Software triggered strobe*/
		if (!pit->thit[t]) {
			pit_set_out(pit, t, 0);
			pit_set_out(pit, t, 1);
		}
		if (pit->newcount[t]) {
			pit->newcount[t] = 0;
			pit->count[t] += l;
			if (pit->using_timer[t])
				timer_advance_u64(&pit->timer[t], (uint64_t)(l * PITCONST));
		} else {
			pit->thit[t] = 1;
			pit->count[t] += 0xffff;
			if (pit->using_timer[t])
				timer_advance_u64(&pit->timer[t], (uint64_t)(0xffff * PITCONST));
		}
		break;
	case 5: /*Hardware triggered strobe*/
		if (!pit->thit[t]) {
			pit_set_out(pit, t, 0);
			pit_set_out(pit, t, 1);
		}
		pit->thit[t] = 1;
		pit->count[t] += 0xffff;
		if (pit->using_timer[t])
			timer_advance_u64(&pit->timer[t], (uint64_t)(0xffff * PITCONST));
		break;
    }

    pit->running[t] = pit->enabled[t] && pit->using_timer[t] && !pit->disabled[t];
    if (pit->using_timer[t] && !pit->running[t])
	pit_dump_and_disable_timer(pit, t);
}


static void
pit_clock(PIT *pit, int t)
{
    if (pit->thit[t] || !pit->enabled[t])  return;

    if (pit->using_timer[t])  return;

    pit->count[t] -= (pit->m[t] == 3) ? 2 : 1;
    if (pit->count[t] == 0)
	pit_over(pit, t);
}


static void
pit_timer_over(void *p)
{
    PIT_nr *pit_nr = (PIT_nr *)p;
    PIT *pit = pit_nr->pit;
    int t = pit_nr->nr;

    pit_over(pit, t);
}


static int 
pit_read_timer(PIT *pit, int t)
{
    int read;

    if (pit->using_timer[t] && !(pit->m[t] == 3 && !pit->gate[t]) && timer_is_enabled(&pit->timer[t])) {
	// read = (int)((timer_get_remaining_u64(&pit->timer[t])) / PITCONST);
	read = (int) round(((double) timer_get_remaining_u64(&pit->timer[t])) / (PITCONSTD * 4294967296.0));
	if (pit->m[t] == 2)
		read++;
	if (read < 0)
		read = 0;
	if (read > 0x10000)
		read = 0x10000;
	if (pit->m[t] == 3)
		read <<= 1;
	return read;
    }
    if (pit->m[t] == 2)
	return pit->count[t] + 1;
    return pit->count[t];
}


/*Dump timer count back to pit->count[], and disable timer. This should be used
  when stopping a PIT timer, to ensure the correct value can be read back.*/
static void
pit_dump_and_disable_timer(PIT *pit, int t)
{
    if (pit->using_timer[t] && timer_is_enabled(&pit->timer[t])) {
	pit->count[t] = pit_read_timer(pit, t);
	timer_disable(&pit->timer[t]);
    }
}


static void
pit_load(PIT *pit, int t)
{
    int l = pit->l[t] ? pit->l[t] : 0x10000;

    pit->newcount[t] = 0;
    pit->disabled[t] = 0;

    switch (pit->m[t]) {
	case 0: /*Interrupt on terminal count*/
		pit->count[t] = l;
		if (pit->using_timer[t])
			timer_set_delay_u64(&pit->timer[t], (uint64_t)(l * PITCONST));
		pit_set_out(pit, t, 0);
		pit->thit[t] = 0;
		pit->enabled[t] = pit->gate[t];
		break;
	case 1: /*Hardware retriggerable one-shot*/
		pit->enabled[t] = 1;
		break;
	case 2: /*Rate generator*/
		if (pit->initial[t]) {
			pit->count[t] = l - 1;
			if (pit->using_timer[t])
				timer_set_delay_u64(&pit->timer[t], (uint64_t)((l - 1) * PITCONST));
			pit_set_out(pit, t, 1);
			pit->thit[t] = 0;
		}
		pit->enabled[t] = pit->gate[t];
		break;
	case 3: /*Square wave mode*/
		if (pit->initial[t]) {
			pit->count[t] = l;
			if (pit->using_timer[t])
				timer_set_delay_u64(&pit->timer[t], (uint64_t)(((l + 1) >> 1) * PITCONST));
			pit_set_out(pit, t, 1);
			pit->thit[t] = 0;
		}
		pit->enabled[t] = pit->gate[t];
		break;
	case 4: /*Software triggered strobe*/
		if (!pit->thit[t] && !pit->initial[t])
			pit->newcount[t] = 1;
		else {
			pit->count[t] = l;
			if (pit->using_timer[t])
				timer_set_delay_u64(&pit->timer[t], (uint64_t)(l * PITCONST));
			pit_set_out(pit, t, 0);
			pit->thit[t] = 0;
		}
		pit->enabled[t] = pit->gate[t];
		break;
	case 5: /*Hardware triggered strobe*/
		pit->enabled[t] = 1;
		break;
    }

    pit->initial[t] = 0;
    pit->running[t] = pit->enabled[t] && pit->using_timer[t] && !pit->disabled[t];
    if (pit->using_timer[t] && !pit->running[t])
	pit_dump_and_disable_timer(pit, t);
}


static void
pit_write(uint16_t addr, uint8_t val, void *priv)
{
    PIT *pit = (PIT *)priv;
    double sv = 0.0;
    int t;

    switch (addr & 3) {
	case 3:		/* control */
		if ((val & 0xC0) == 0xC0) {
			if (!(val & 0x20)) {
				if (val & 2)
					pit->rl[0] = (uint32_t)pit_read_timer(pit, 0);
				if (val & 4)
					pit->rl[1] = (uint32_t)pit_read_timer(pit, 1);
				if (val & 8)
					pit->rl[2] = (uint32_t)pit_read_timer(pit, 2);
			}
			if (!(val & 0x10)) {
				if (val & 2) {
					pit->read_status[0] = (pit->ctrls[0] & 0x3f) | (pit->out[0] ? 0x80 : 0);
					pit->do_read_status[0] = 1;
				}
				if (val & 4) {
					pit->read_status[1] = (pit->ctrls[1] & 0x3f) | (pit->out[1] ? 0x80 : 0);
					pit->do_read_status[1] = 1;
				}
				if (val & 8) {
					pit->read_status[2] = (pit->ctrls[2] & 0x3f) | (pit->out[2] ? 0x80 : 0);
					pit->do_read_status[2] = 1;
				}
			}
			return;
		}
		t = val >> 6;
		pit->ctrl = val;
		if ((val >> 7) == 3)
			return;

		if (!(pit->ctrl & 0x30)) {
			pit->rl[t] = (uint32_t)pit_read_timer(pit, t);
			pit->ctrl |= 0x30;
			pit->rereadlatch[t] = 0;
			pit->rm[t] = 3;
			pit->latched[t] = 1;
		} else {
			pit->ctrls[t] = val;
			pit->rm[t] = pit->wm[t] = (pit->ctrl >> 4) & 3;
			pit->m[t] = (val >> 1) & 7;
			if (pit->m[t] > 5)
				pit->m[t] &= 3;
			if (!(pit->rm[t])) {
				pit->rm[t] = 3;
				pit->rl[t] = (uint32_t)pit_read_timer(pit, t);
			}
			pit->rereadlatch[t] = 1;
			pit->initial[t] = 1;
			if (!pit->m[t])
				pit_set_out(pit, t, 0);
			else
				pit_set_out(pit, t, 1);
			pit->disabled[t] = 1;
		}
		pit->wp = 0;
		pit->thit[t] = 0;
		break;

	case 0:
	case 1:
	case 2:		/* the actual timers */
		t = addr & 3;
		switch (pit->wm[t]) {
			case 0:
				pit->l[t] &= 0xff;
				pit->l[t] |= (val << 8);
				pit_load(pit, t);
				pit->wm[t] = 3;
				break;
			case 1:
				pit->l[t] = val;
				pit_load(pit, t);
				break;
			case 2:
				pit->l[t] = (val << 8);
				pit_load(pit, t);
				break;
			case 3:
				pit->l[t] &= 0xFF00;
				pit->l[t] |= val;
				pit->wm[t] = 0;
				break;
		}

		/* PIT latches are in fractions of 60 ms, so convert to sample using the formula below. */
		sv = (((double) pit->l[2]) / 60.0) * 16384.0;
		speakval = ((int) sv) - 0x2000;
		if (speakval > 0x2000)
			speakval = 0x2000;
		break;
    }
}


static uint8_t
pit_read(uint16_t addr, void *priv)
{
    PIT *pit = (PIT *)priv;
    uint8_t temp = 0xff;
    int t;

    switch (addr & 3) {
	case 3:		/* control */
		temp = pit->ctrl;
		break;

	case 0:
	case 1:
	case 2:		/*the actual timers */
		t = addr & 3;
		if (pit->do_read_status[t]) {
			pit->do_read_status[t] = 0;
			temp = pit->read_status[t];
			break;
		}
		if (pit->rereadlatch[addr & 3] && !pit->latched[addr & 3]) {
			pit->rereadlatch[addr & 3] = 0;
			pit->rl[t] = pit_read_timer(pit, t);
		}
		switch (pit->rm[addr & 3]) {
			case 0:
				temp = pit->rl[addr & 3] >> 8;
				pit->rm[addr & 3] = 3;
				pit->latched[addr & 3] = 0;
				pit->rereadlatch[addr & 3] = 1;
				break;

			case 1:
				temp = (pit->rl[addr & 3]) & 0xff;
				pit->latched[addr & 3] = 0;
				pit->rereadlatch[addr & 3] = 1;
				break;

			case 2:
				temp = (pit->rl[addr & 3]) >> 8;
				pit->latched[addr & 3] = 0;
				pit->rereadlatch[addr & 3] = 1;
				break;

			case 3:
				temp = (pit->rl[addr & 3]) & 0xff;
				if (pit->m[addr & 3] & 0x80)
					pit->m[addr & 3] &= 7;
				else
					pit->rm[addr & 3] = 0;
				break;
		}
		break;
    }

    return temp;
}


/* FIXME: Should be removed. */
static void
pit_null_timer(int new_out, int old_out)
{
}


/* FIXME: Should be moved to machine.c (default for most machine). */
static void
pit_irq0_timer(int new_out, int old_out)
{
    if (new_out && !old_out)
	picint(1);

    if (!new_out)
	picintc(1);
}


static void
pit_speaker_timer(int new_out, int old_out)
{
    int l;

    speaker_update();

    l = pit.l[2] ? pit.l[2] : 0x10000;
    if (l < 25)
	speakon = 0;
    else
	speakon = new_out;

    ppispeakon = new_out;
}


void
pit_init(void)
{
    int i;

    pit_reset(&pit);

    for (i = 0; i < 3; i++) {
	pit.pit_nr[i].nr = i;
	pit.pit_nr[i].pit = &pit;

	pit.gate[i] = 1;
	pit.using_timer[i] = 1;

	timer_add(&pit.timer[i], pit_timer_over, (void *)&pit.pit_nr[i], 0);
    }

    io_sethandler(0x0040, 0x0004, pit_read, NULL, NULL, pit_write, NULL, NULL, &pit);

    /* Timer 0: The TOD clock. */
    pit_set_out_func(&pit, 0, pit_irq0_timer);

    /* Timer 1: Unused. */
    pit_set_out_func(&pit, 1, pit_null_timer);

    /* Timer 2: speaker and cassette. */
    pit_set_out_func(&pit, 2, pit_speaker_timer);
    pit.gate[2] = 0;
}


static void
pit_irq0_ps2(int new_out, int old_out)
{
    if (new_out && !old_out) {
	picint(1);
	pit_set_gate_no_timer(&pit2, 0, 1);
    }

    if (!new_out)
	picintc(1);

    if (!new_out && old_out)
	pit_clock(&pit2, 0);
}


static void
pit_nmi_ps2(int new_out, int old_out)
{
    nmi = new_out;

    if (nmi)
	nmi_auto_clear = 1;
}


void
pit_ps2_init()
{
    pit_reset(&pit2);

    pit2.gate[0] = 0;
    pit2.using_timer[0] = 0;
    pit2.disabled[0] = 1;

    pit2.pit_nr[0].nr = 0;
    pit2.pit_nr[0].pit = &pit2;

    timer_add(&pit2.timer[0], pit_timer_over, (void *)&pit2.pit_nr[0], 0);

    io_sethandler(0x0044, 0x0001, pit_read, NULL, NULL, pit_write, NULL, NULL, &pit2);
    io_sethandler(0x0047, 0x0001, pit_read, NULL, NULL, pit_write, NULL, NULL, &pit2);

    pit_set_out_func(&pit, 0, pit_irq0_ps2);
    pit_set_out_func(&pit2, 0, pit_nmi_ps2);
}


void
pit_reset(PIT *pit)
{
    int i;

    memset(pit, 0, sizeof(PIT));

    for (i = 0; i < 3; i++) {
	pit->ctrls[i] = 0;
	pit->thit[i] = 0;	/* Should be only thit[0]? */

	pit->m[i] = 0;
	pit->gate[i] = 1;
	pit->l[i] = 0xffff;
	pit->using_timer[i] = 1;
    }

    pit->thit[0] = 1;

    /* Disable speaker gate. */
    pit->gate[2] = 0;
}


void
pit_set_clock(int clock)
{
    /* Set default CPU/crystal clock and xt_cpu_multi. */
    if (machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].cpu_type >= CPU_286) {
	if (clock == 66666666)
		cpuclock = 200000000.0 / 3.0;
	else if (clock == 33333333)
		cpuclock = 100000000.0 / 3.0;
	else
		cpuclock = (double) clock;

	PITCONSTD = (cpuclock / 1193182.0);
	PITCONST = (uint64_t) (PITCONSTD * (double)(1ull << 32));
	CGACONST = (uint64_t) ((cpuclock / (19687503.0/11.0)) * (double)(1ull << 32));
	ISACONST = (uint64_t) ((cpuclock / 8000000.0) * (double)(1ull << 32));
	xt_cpu_multi = 1ULL;
    } else {
	cpuclock = 14318184.0;
	PITCONSTD = 12.0;
       	PITCONST = (12ULL << 32ULL);
        CGACONST = (8ULL << 32ULL);
	xt_cpu_multi = 3ULL;

	switch (machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].rspeed) {
		case 7159092:
			if (machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].cpu_flags & CPU_ALTERNATE_XTAL) {
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
			if (machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].cpu_flags & CPU_ALTERNATE_XTAL) {
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
    io_delay = (int) round(((double) machines[machine].cpu[cpu_manufacturer].cpus[cpu_effective].rspeed) / 1000000.0);

    MDACONST = (uint64_t) (cpuclock / 2032125.0 * (double)(1ull << 32));
    HERCCONST = MDACONST;
    VGACONST1 = (uint64_t) (cpuclock / 25175000.0 * (double)(1ull << 32));
    VGACONST2 = (uint64_t) (cpuclock / 28322000.0 * (double)(1ull << 32));
    RTCCONST = (uint64_t) (cpuclock / 32768.0 * (double)(1ull << 32));

    TIMER_USEC = (uint64_t)((cpuclock / 1000000.0) * (double)(1ull << 32));

    isa_timing = (cpuclock / (double)8000000.0);
    bus_timing = (cpuclock / (double)cpu_busspeed);

    if (cpu_busspeed >= 30000000)
	SYSCLK = bus_timing * 4.0;
    else
	SYSCLK = bus_timing * 3.0;

    video_update_timing();

    device_speed_changed();
}


void
clearpit(void)
{
    // pit.c[0] = (pit.l[0] << 2);
}


void
pit_set_gate(PIT *pit, int t, int gate)
{
    if (pit->disabled[t]) {
	pit->gate[t] = gate;
	return;
    }

    pit_set_gate_no_timer(pit, t, gate);
}


void
pit_set_using_timer(PIT *pit, int t, int using_timer)
{
    timer_process();

    if (pit->using_timer[t] && !using_timer)
	pit->count[t] = pit_read_timer(pit, t);

    pit->running[t] = pit->enabled[t] && using_timer && !pit->disabled[t];

    if (!pit->using_timer[t] && using_timer && pit->running[t])
	timer_set_delay_u64(&pit->timer[t], (uint64_t)(pit->count[t] * PITCONST));
    else if (!pit->running[t])
	timer_disable(&pit->timer[t]);

    pit->using_timer[t] = using_timer;
}


void
pit_set_out_func(PIT *pit, int t, void (*func)(int new_out, int old_out))
{
    pit->set_out_funcs[t] = func;
}


void
pit_irq0_timer_pcjr(int new_out, int old_out)
{
    if (new_out && !old_out) {
	picint(1);
	pit_clock(&pit, 1);
    }

    if (!new_out)
	picintc(1);
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
    if (new_out && !old_out)
	ppi.pb ^= 0x10;
}
