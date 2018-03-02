#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <wchar.h>
#include "86box.h"
#include "mem.h"
#include "io.h"
#include "nvr.h"
#include "nvr_tc8521.h"
#include "rtc_tc8521.h"
#include "pic.h"
#include "pit.h"
#include "plat.h"
#include "rom.h"
#include "timer.h"
#include "config.h"
#include "nmi.h"
#include "machine/machine.h"

int oldromset;
int nvrmask=63;
uint8_t nvrram[128];
int nvraddr;

int nvr_dosave = 0;

static int64_t nvr_onesec_time = 0, nvr_onesec_cnt = 0;

static void tc8521_onesec(void *p)
{
        nvr_onesec_cnt++;
        if (nvr_onesec_cnt >= 100)
        {
		tc8521_tick();
                nvr_onesec_cnt = 0;
        }
        nvr_onesec_time += (int)(10000 * TIMER_USEC);
}

void write_tc8521(uint16_t addr, uint8_t val, void *priv)
{
	uint8_t page = nvrram[0x0D] & 3;

	addr &= 0x0F;
	if (addr < 0x0D) addr += 16 * page;

        if (addr >= 0x10 && nvrram[addr] != val)
        	nvr_dosave = 1;
               
	nvrram[addr] = val;
}


uint8_t read_tc8521(uint16_t addr, void *priv)
{
	uint8_t page = nvrram[0x0D] & 3;

	addr &= 0x0F;
	if (addr < 0x0D) addr += 16 * page;

	return nvrram[addr];
}


void tc8521_loadnvr()
{
        FILE *f;

        nvrmask=63;
        oldromset=romset;
        switch (romset)
        {
                case ROM_T1000: f = plat_fopen(nvr_path(L"t1000.nvr"), L"rb"); break;
                case ROM_T1200: f = plat_fopen(nvr_path(L"t1200.nvr"), L"rb"); break;
                default: return;
        }
        if (!f)
        {
                memset(nvrram,0xFF,64);
		nvrram[0x0E] = 0;	/* Test register */
                if (!enable_sync)
                {
			memset(nvrram, 0, 16);
                }
                return;
        }
        fread(nvrram,64,1,f);
        if (enable_sync)
                tc8521_internal_sync(nvrram);
        else
                tc8521_internal_set_nvrram(nvrram); /* Update the internal clock state based on the NVR registers. */
        fclose(f);
}


void tc8521_savenvr()
{
        FILE *f;
        switch (oldromset)
        {
                case ROM_T1000: f = plat_fopen(nvr_path(L"t1000.nvr"), L"wb"); break;
                case ROM_T1200: f = plat_fopen(nvr_path(L"t1200.nvr"), L"wb"); break;
                default: return;
        }
        fwrite(nvrram,64,1,f);
        fclose(f);
}

void nvr_tc8521_init()
{
        io_sethandler(0x2C0, 0x10, read_tc8521, NULL, NULL, write_tc8521, NULL, NULL,  NULL);
        timer_add(tc8521_onesec, &nvr_onesec_time, TIMER_ALWAYS_ENABLED, NULL);
}
