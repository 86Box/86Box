#include <stdint.h>
#include <stdlib.h>
#include "ibm.h"
#include "io.h"
#include "sound.h"
#include "mame/fmopl.h"
#include "mame/ymf262.h"
#include "sound_opl.h"
#include "sound_dbopl.h"

/*Interfaces between PCem and the actual OPL emulator*/


uint8_t opl2_read(uint16_t a, void *priv)
{
        opl_t *opl = (opl_t *)priv;

        cycles -= (int)(isa_timing * 8);
        opl2_update2(opl);
        return ym3812_read(opl->YM3812[0], a);
}
void opl2_write(uint16_t a, uint8_t v, void *priv)
{
        opl_t *opl = (opl_t *)priv;

        opl2_update2(opl);
        ym3812_write(opl->YM3812[0],a,v);
        ym3812_write(opl->YM3812[1],a,v);
}

uint8_t opl2_l_read(uint16_t a, void *priv)
{
        opl_t *opl = (opl_t *)priv;

        cycles -= (int)(isa_timing * 8);
        opl2_update2(opl);
        return ym3812_read(opl->YM3812[0], a);
}
void opl2_l_write(uint16_t a, uint8_t v, void *priv)
{
        opl_t *opl = (opl_t *)priv;

        opl2_update2(opl);
        ym3812_write(opl->YM3812[0],a,v);
}

uint8_t opl2_r_read(uint16_t a, void *priv)
{
        opl_t *opl = (opl_t *)priv;

        cycles -= (int)(isa_timing * 8);
        opl2_update2(opl);
        return ym3812_read(opl->YM3812[1], a);
}
void opl2_r_write(uint16_t a, uint8_t v, void *priv)
{
        opl_t *opl = (opl_t *)priv;

        opl2_update2(opl);
        ym3812_write(opl->YM3812[1],a,v);
}

uint8_t opl3_read(uint16_t a, void *priv)
{
        opl_t *opl = (opl_t *)priv;

        cycles -= (int)(isa_timing * 8);
        opl3_update2(opl);
        return ymf262_read(opl->YMF262, a);
}
void opl3_write(uint16_t a, uint8_t v, void *priv)
{
        opl_t *opl = (opl_t *)priv;
        
        opl3_update2(opl);
        ymf262_write(opl->YMF262, a, v);
}


void opl2_update2(opl_t *opl)
{
        if (opl->pos < sound_pos_global)
        {
                ym3812_update_one(opl->YM3812[0], &opl->buffer[opl->pos*2], sound_pos_global - opl->pos);
	        ym3812_update_one(opl->YM3812[1], &opl->buffer[opl->pos*2 + 1], sound_pos_global - opl->pos);
                for (; opl->pos < sound_pos_global; opl->pos++)
                {
                        opl->filtbuf[0] = opl->buffer[opl->pos*2]   = (opl->buffer[opl->pos*2]   / 4) + ((opl->filtbuf[0] * 11) / 16);
                        opl->filtbuf[1] = opl->buffer[opl->pos*2+1] = (opl->buffer[opl->pos*2+1] / 4) + ((opl->filtbuf[1] * 11) / 16);
                }
        }
}

void opl3_update2(opl_t *opl)
{
        if (opl->pos < sound_pos_global)
        {
                ymf262_update_one(opl->YMF262, &opl->buffer[opl->pos*2], sound_pos_global - opl->pos);
                for (; opl->pos < sound_pos_global; opl->pos++)
                {
                        opl->filtbuf[0] = opl->buffer[opl->pos*2]   = (opl->buffer[opl->pos*2]   / 4) + ((opl->filtbuf[0] * 11) / 16);
                        opl->filtbuf[1] = opl->buffer[opl->pos*2+1] = (opl->buffer[opl->pos*2+1] / 4) + ((opl->filtbuf[1] * 11) / 16);
                }
        }
}

void ym3812_timer_set_0(void *param, int timer, int64_t period)
{
        opl_t *opl = (opl_t *)param;
        
        opl->timers[0][timer] = period * TIMER_USEC * 20;
        if (!opl->timers[0][timer]) opl->timers[0][timer] = 1;
        opl->timers_enable[0][timer] = period ? 1 : 0;
}
void ym3812_timer_set_1(void *param, int timer, int64_t period)
{
        opl_t *opl = (opl_t *)param;

        opl->timers[1][timer] = period * TIMER_USEC * 20;
        if (!opl->timers[1][timer]) opl->timers[1][timer] = 1;
        opl->timers_enable[1][timer] = period ? 1 : 0;
}

void ymf262_timer_set(void *param, int timer, int64_t period)
{
        opl_t *opl = (opl_t *)param;

        opl->timers[0][timer] = period * TIMER_USEC * 20;
        if (!opl->timers[0][timer]) opl->timers[0][timer] = 1;
        opl->timers_enable[0][timer] = period ? 1 : 0;
}

static void opl_timer_callback00(void *p)
{
        opl_t *opl = (opl_t *)p;
        
        opl->timers_enable[0][0] = 0;
        ym3812_timer_over(opl->YM3812[0], 0);
}
static void opl_timer_callback01(void *p)
{
        opl_t *opl = (opl_t *)p;
        
        opl->timers_enable[0][1] = 0;
        ym3812_timer_over(opl->YM3812[0], 1);
}
static void opl_timer_callback10(void *p)
{
        opl_t *opl = (opl_t *)p;
        
        opl->timers_enable[1][0] = 0;
        ym3812_timer_over(opl->YM3812[1], 0);
}
static void opl_timer_callback11(void *p)
{
        opl_t *opl = (opl_t *)p;
        
        opl->timers_enable[1][1] = 0;
        ym3812_timer_over(opl->YM3812[1], 1);
}
static void opl3_timer_callback00(void *p)
{
        opl_t *opl = (opl_t *)p;
        
        opl->timers_enable[0][0] = 0;
        ymf262_timer_over(opl->YMF262, 0);
}
static void opl3_timer_callback01(void *p)
{
        opl_t *opl = (opl_t *)p;
        
        opl->timers_enable[0][1] = 0;
        ymf262_timer_over(opl->YMF262, 1);
}
        
void opl2_init(opl_t *opl)
{
        opl->YM3812[0] = ym3812_init(NULL, 3579545, 48000);
        ym3812_reset_chip(opl->YM3812[0]);
        ym3812_set_timer_handler(opl->YM3812[0], ym3812_timer_set_0, opl);

        opl->YM3812[1] = ym3812_init(NULL, 3579545, 48000);
        ym3812_reset_chip(opl->YM3812[1]);
        ym3812_set_timer_handler(opl->YM3812[1], ym3812_timer_set_1, opl);

        timer_add(opl_timer_callback00, &opl->timers[0][0], &opl->timers_enable[0][0], (void *)opl);
        timer_add(opl_timer_callback01, &opl->timers[0][1], &opl->timers_enable[0][1], (void *)opl);
        timer_add(opl_timer_callback10, &opl->timers[1][0], &opl->timers_enable[1][0], (void *)opl);
        timer_add(opl_timer_callback11, &opl->timers[1][1], &opl->timers_enable[1][1], (void *)opl);
}

void opl3_init(opl_t *opl)
{
        opl->YMF262 = ymf262_init(NULL, 3579545 * 4, 48000);
        ymf262_reset_chip(opl->YMF262);
        ymf262_set_timer_handler(opl->YMF262, ymf262_timer_set, opl);
        timer_add(opl3_timer_callback00, &opl->timers[0][0], &opl->timers_enable[0][0], (void *)opl);
        timer_add(opl3_timer_callback01, &opl->timers[0][1], &opl->timers_enable[0][1], (void *)opl);
}

