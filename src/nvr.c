/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		CMOS NVRAM emulation.
 *
 * Version:	@(#)nvr.c	1.0.1	2017/06/03
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Mahod,
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 *		Copyright 2016-2017 Mahod.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "ibm.h"
#include "CPU/cpu.h"
#include "device.h"
#include "io.h"
#include "mem.h"
#include "model.h"
#include "nvr.h"
#include "pic.h"
#include "rom.h"
#include "timer.h"
#include "rtc.h"

int oldmodel;
int nvrmask=63;
char nvrram[128];
int nvraddr;

int nvr_dosave = 0;

static int nvr_onesec_time = 0, nvr_onesec_cnt = 0;

static int rtctime;

void getnvrtime(void)
{
	time_get(nvrram);
}

void nvr_recalc(void)
{
        int c;
        int newrtctime;
        c = 1 << ((nvrram[RTC_REGA] & RTC_RS) - 1);
        newrtctime=(int)(RTCCONST * c * (1 << TIMER_SHIFT));
        if (rtctime>newrtctime) rtctime=newrtctime;
}

void nvr_rtc(void *p)
{
        int c;
        if (!(nvrram[RTC_REGA] & RTC_RS))
        {
                rtctime=0x7fffffff;
                return;
        }
        c = 1 << ((nvrram[RTC_REGA] & RTC_RS) - 1);
        rtctime += (int)(RTCCONST * c * (1 << TIMER_SHIFT));
        nvrram[RTC_REGC] |= RTC_PF;
        if (nvrram[RTC_REGB] & RTC_PIE)
        {
                nvrram[RTC_REGC] |= RTC_IRQF;
                if (AMSTRAD) picint(2);
                else         picint(0x100);
        }
}

int nvr_update_status = 0;

#define ALARM_DONTCARE	0xc0

int nvr_check_alarm(int nvraddr)
{
        return (nvrram[nvraddr + 1] == nvrram[nvraddr] || (nvrram[nvraddr + 1] & ALARM_DONTCARE) == ALARM_DONTCARE);
}

int nvr_update_end_count = 0;

void nvr_update_end(void *p)
{
        if (!(nvrram[RTC_REGB] & RTC_SET))
        {
                getnvrtime();
                /* Clear update status. */
                nvr_update_status = 0;

                if (nvr_check_alarm(RTC_SECONDS) && nvr_check_alarm(RTC_MINUTES) && nvr_check_alarm(RTC_HOURS))
                {
                        nvrram[RTC_REGC] |= RTC_AF;
                        if (nvrram[RTC_REGB] & RTC_AIE)
                        {
                                nvrram[RTC_REGC] |= RTC_IRQF;
                                if (AMSTRAD) picint(2);
                                else         picint(0x100);
                        }
                }

                /* The flag and interrupt should be issued on update ended, not started. */
                nvrram[RTC_REGC] |= RTC_UF;
                if (nvrram[RTC_REGB] & RTC_UIE)
                {
                        nvrram[RTC_REGC] |= RTC_IRQF;
                        if (AMSTRAD) picint(2);
                        else         picint(0x100);
                }
        }

        nvr_update_end_count = 0;
}

void nvr_onesec(void *p)
{
        nvr_onesec_cnt++;
        if (nvr_onesec_cnt >= 100)
        {
                if (!(nvrram[RTC_REGB] & RTC_SET))
                {
                        nvr_update_status = RTC_UIP;
                        rtc_tick();

                        nvr_update_end_count = (int)((244.0 + 1984.0) * TIMER_USEC);
                }
                nvr_onesec_cnt = 0;
        }
        nvr_onesec_time += (int)(10000 * TIMER_USEC);
}

void writenvr(uint16_t addr, uint8_t val, void *priv)
{
        int c, old;
        if (addr&1)
        {
                if (nvraddr==RTC_REGC || nvraddr==RTC_REGD)
                        return; /* Registers C and D are read-only. There's no reason to continue. */
                if (nvraddr > RTC_REGD && nvrram[nvraddr] != val)
                   nvr_dosave = 1;
                
		old = nvrram[nvraddr];
                nvrram[nvraddr]=val;

                if (nvraddr == RTC_REGA)
                {
                        if (val & RTC_RS)
                        {
                                c = 1 << ((val & RTC_RS) - 1);
                                rtctime += (int)(RTCCONST * c * (1 << TIMER_SHIFT));
                        }
                        else
                           rtctime = 0x7fffffff;
                }
		else
		{
                        if (nvraddr == RTC_REGB)
                        {
                                if (((old ^ val) & RTC_SET) && (val & RTC_SET))
                                {
                                        nvrram[RTC_REGA] &= ~RTC_UIP;             /* This has to be done according to the datasheet. */
                                        nvrram[RTC_REGB] &= ~RTC_UIE;             /* This also has to happen per the specification. */
                                }
                        }

                        if ((nvraddr < RTC_REGA) || (nvraddr == RTC_CENTURY))
                        {
                                if ((nvraddr != 1) && (nvraddr != 3) && (nvraddr != 5))
                                {
                                        if ((old != val) && !enable_sync)
                                        {
                                                time_update(nvrram, nvraddr);
                                                nvr_dosave = 1;
                                        }
                                }
                        }
                }
        }
        else        nvraddr=val&nvrmask;
}

uint8_t readnvr(uint16_t addr, void *priv)
{
        uint8_t temp;
        if (addr&1)
        {
                if (nvraddr == RTC_REGA)
                        return ((nvrram[RTC_REGA] & 0x7F) | nvr_update_status);
                if (nvraddr == RTC_REGD)
                        nvrram[RTC_REGD] |= RTC_VRT;
                if (nvraddr == RTC_REGC)
                {
                        if (AMSTRAD) picintc(2);
                        else         picintc(0x100);
                        temp = nvrram[RTC_REGC];
                        nvrram[RTC_REGC] = 0;
                        return temp;
                }
                return nvrram[nvraddr];
        }
        return nvraddr;
}

void loadnvr(void)
{
        FILE *f = NULL;
        int c;
        nvrmask=63;
        oldmodel = model;

	wchar_t *model_name;
	wchar_t *nvr_name;

	model_name = (wchar_t *) malloc((strlen(model_get_internal_name_ex(model)) << 1) + 2);
	mbstowcs(model_name, model_get_internal_name_ex(model), strlen(model_get_internal_name_ex(model)) + 1);
	nvr_name = (wchar_t *) malloc((wcslen(model_name) << 1) + 2 + 8);
	_swprintf(nvr_name, L"%s.nvr", model_name);

	pclog_w(L"Opening NVR file: %s...\n", nvr_name);

	if (model_get_nvrmask(model) != 0)
	{
		f = nvrfopen(nvr_name, L"rb");
		nvrmask = model_get_nvrmask(model);
	}

	free(nvr_name);
	free(model_name);

        if (!f || (model_get_nvrmask(model) == 0))
        {
		if (f)
		{
			fclose(f);
		}
                memset(nvrram,0xFF,128);
                if (!enable_sync)
                {
                        nvrram[RTC_SECONDS] = nvrram[RTC_MINUTES] = nvrram[RTC_HOURS] = 0;
                        nvrram[RTC_DOM] = nvrram[RTC_MONTH] = 1;
                        nvrram[RTC_YEAR] = BCD(80);
                        nvrram[RTC_CENTURY] = BCD(19);
                        nvrram[RTC_REGB] = RTC_2412;
                }
                return;
        }
        fread(nvrram,128,1,f);
        if (enable_sync)
                time_internal_sync(nvrram);
        else
                time_internal_set_nvrram(nvrram); /* Update the internal clock state based on the NVR registers. */
        fclose(f);
        nvrram[RTC_REGA] = 6;
        nvrram[RTC_REGB] = RTC_2412;
        c = 1 << ((nvrram[RTC_REGA] & RTC_RS) - 1);
        rtctime += (int)(RTCCONST * c * (1 << TIMER_SHIFT));
}

void savenvr(void)
{
        FILE *f = NULL;

	wchar_t *model_name;
	wchar_t *nvr_name;

	model_name = (wchar_t *) malloc((strlen(model_get_internal_name_ex(oldmodel)) << 1) + 2);
	mbstowcs(model_name, model_get_internal_name_ex(oldmodel), strlen(model_get_internal_name_ex(oldmodel)) + 1);
	nvr_name = (wchar_t *) malloc((wcslen(model_name) << 1) + 2 + 8);
	_swprintf(nvr_name, L"%s.nvr", model_name);

	pclog_w(L"Saving NVR file: %s...\n", nvr_name);

	if (model_get_nvrmask(oldmodel) != 0)
	{
		f = nvrfopen(nvr_name, L"wb");
	}

	free(nvr_name);
	free(model_name);

	if (!f || (model_get_nvrmask(oldmodel) == 0))
	{
		if (f)
		{
			fclose(f);
		}
		return;
	}

        fwrite(nvrram,128,1,f);
        fclose(f);
}

void nvr_init(void)
{
        io_sethandler(0x0070, 0x0002, readnvr, NULL, NULL, writenvr, NULL, NULL,  NULL);
        timer_add(nvr_rtc, &rtctime, TIMER_ALWAYS_ENABLED, NULL);
        timer_add(nvr_onesec, &nvr_onesec_time, TIMER_ALWAYS_ENABLED, NULL);
        timer_add(nvr_update_end, &nvr_update_end_count, &nvr_update_end_count, NULL);

}
