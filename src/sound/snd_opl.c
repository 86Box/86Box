/* Copyright holders: Sarah Walker, SA1988
   see COPYING for more details
*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/sound.h>
#include <86box/snd_opl.h>
#include <86box/snd_opl_backend.h>


/*Interfaces between 86Box and the actual OPL emulator*/


uint8_t
opl2_read(uint16_t a, void *priv)
{
    opl_t *opl = (opl_t *)priv;

    sub_cycles((int) (isa_timing * 8));
    opl2_update2(opl);

    return opl_read(0, a);
}


void
opl2_write(uint16_t a, uint8_t v, void *priv)
{
    opl_t *opl = (opl_t *)priv;

    opl2_update2(opl);
    opl_write(0, a, v);
    opl_write(1, a, v);
}


uint8_t
opl2_l_read(uint16_t a, void *priv)
{
    opl_t *opl = (opl_t *)priv;

    sub_cycles((int)(isa_timing * 8));
    opl2_update2(opl);

    return opl_read(0, a);
}


void
opl2_l_write(uint16_t a, uint8_t v, void *priv)
{
    opl_t *opl = (opl_t *)priv;

    opl2_update2(opl);
    opl_write(0, a, v);
}


uint8_t
opl2_r_read(uint16_t a, void *priv)
{
    opl_t *opl = (opl_t *)priv;

    sub_cycles((int)(isa_timing * 8));
    opl2_update2(opl);

    return opl_read(1, a);
}


void
opl2_r_write(uint16_t a, uint8_t v, void *priv)
{
    opl_t *opl = (opl_t *)priv;

    opl2_update2(opl);
    opl_write(1, a, v);
}


uint8_t
opl3_read(uint16_t a, void *priv)
{
    opl_t *opl = (opl_t *)priv;

    sub_cycles((int)(isa_timing * 8));
    opl3_update2(opl);

    return opl_read(0, a);
}


void
opl3_write(uint16_t a, uint8_t v, void *priv)
{
        opl_t *opl = (opl_t *)priv;
        
        opl3_update2(opl);
        opl_write(0, a, v);
}


void
opl2_update2(opl_t *opl)
{
    if (opl->pos < sound_pos_global) {
	opl2_update(0, &opl->buffer[opl->pos << 1], sound_pos_global - opl->pos);
	opl2_update(1, &opl->buffer2[opl->pos << 1], sound_pos_global - opl->pos);
	for (; opl->pos < sound_pos_global; opl->pos++) {
		opl->buffer[(opl->pos << 1) + 1] = opl->buffer2[(opl->pos << 1) + 1];
		opl->buffer[opl->pos << 1]   = (opl->buffer[opl->pos << 1] / 2);
		opl->buffer[(opl->pos << 1) + 1] = (opl->buffer[(opl->pos << 1) + 1] / 2);
	}
    }
}


void
opl3_update2(opl_t *opl)
{
    if (opl->pos < sound_pos_global) {
	opl3_update(0, &opl->buffer[(opl->pos << 1)], sound_pos_global - opl->pos);
	for (; opl->pos < sound_pos_global; opl->pos++) {
		opl->buffer[opl->pos << 1]   = (opl->buffer[opl->pos << 1]   / 2);
		opl->buffer[(opl->pos << 1) + 1] = (opl->buffer[(opl->pos << 1) + 1] / 2);
	}
    }
}


void
ym3812_timer_set_0(void *param, int timer, uint64_t period)
{
    opl_t *opl = (opl_t *)param;

    if (period)
	timer_set_delay_u64(&opl->timers[0][timer], period * TIMER_USEC * 20);
    else
	timer_disable(&opl->timers[0][timer]);
}


void
ym3812_timer_set_1(void *param, int timer, uint64_t period)
{
    opl_t *opl = (opl_t *)param;

    if (period)
	timer_set_delay_u64(&opl->timers[1][timer], period * TIMER_USEC * 20);
    else
	timer_disable(&opl->timers[1][timer]);
}


void 
ymf262_timer_set(void *param, int timer, uint64_t period)
{
    opl_t *opl = (opl_t *)param;

    if (period)
	timer_set_delay_u64(&opl->timers[0][timer], period * TIMER_USEC * 20);
    else
	timer_disable(&opl->timers[0][timer]);
}


static void
opl_timer_callback00(void *p)
{
    opl_timer_over(0, 0);
}


static void
opl_timer_callback01(void *p)
{
    opl_timer_over(0, 1);
}


static void
opl_timer_callback10(void *p)
{
    opl_timer_over(1, 0);
}


static void
opl_timer_callback11(void *p)
{
    opl_timer_over(1, 1);
}


void
opl2_init(opl_t *opl)
{
    opl_init(ym3812_timer_set_0, opl, 0, 0);
    opl_init(ym3812_timer_set_1, opl, 1, 0);

    timer_add(&opl->timers[0][0], opl_timer_callback00, (void *)opl, 0);
    timer_add(&opl->timers[0][1], opl_timer_callback01, (void *)opl, 0);
    timer_add(&opl->timers[1][0], opl_timer_callback10, (void *)opl, 0);
    timer_add(&opl->timers[1][1], opl_timer_callback11, (void *)opl, 0);
}


void
opl3_init(opl_t *opl)
{
    opl_init(ymf262_timer_set, opl, 0, 1);

    timer_add(&opl->timers[0][0], opl_timer_callback00, (void *)opl, 0);
    timer_add(&opl->timers[0][1], opl_timer_callback01, (void *)opl, 0);
}
