#include <stdio.h>
#include "ibm.h"
#include "io.h"
#include "mem.h"
#include "nvr.h"
#include "pic.h"
#include "rom.h"
#include "timer.h"
#include "rtc.h"

int oldromset;
int nvrmask=63;
char nvrram[128];
int nvraddr;

int nvr_dosave = 0;

static int nvr_onesec_time = 0, nvr_onesec_cnt = 0;

static int rtctime;

void getnvrtime()
{
	time_get(nvrram);
}

void nvr_recalc()
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

void loadnvr()
{
        FILE *f;
        int c;
        nvrmask=63;
        oldromset=romset;
        switch (romset)
        {
                case ROM_PC1512:		f = nvrfopen(L"pc1512.nvr",		L"rb"); break;
                case ROM_PC1640:		f = nvrfopen(L"pc1640.nvr",		L"rb"); break;
                case ROM_PC200:			f = nvrfopen(L"pc200.nvr",		L"rb"); break;
                case ROM_PC2086:		f = nvrfopen(L"pc2086.nvr",		L"rb"); break;
                case ROM_PC3086:		f = nvrfopen(L"pc3086.nvr",		L"rb"); break;                
                case ROM_IBMAT:			f = nvrfopen(L"at.nvr",			L"rb"); break;
                case ROM_IBMPS1_2011:		f = nvrfopen(L"ibmps1_2011.nvr",	L"rb"); nvrmask = 127; break;
                case ROM_IBMPS1_2121:		f = nvrfopen(L"ibmps1_2121.nvr",	L"rb"); nvrmask = 127; break;
                case ROM_IBMPS1_2121_ISA:	f = nvrfopen(L"ibmps1_2121_isa.nvr",	L"rb"); nvrmask = 127; break;
                case ROM_IBMPS2_M30_286:	f = nvrfopen(L"ibmps2_m30_286.nvr",	L"rb"); nvrmask = 127; break;
                case ROM_IBMPS2_M50:		f = nvrfopen(L"ibmps2_m50.nvr",		L"rb"); break;
                case ROM_IBMPS2_M55SX:		f = nvrfopen(L"ibmps2_m55sx.nvr",	L"rb"); break;
                case ROM_IBMPS2_M80:		f = nvrfopen(L"ibmps2_m80.nvr",		L"rb"); break;
                case ROM_CMDPC30:		f = nvrfopen(L"cmdpc30.nvr",		L"rb"); nvrmask = 127; break;
		case ROM_PORTABLEII:		f = nvrfopen(L"portableii.nvr",		L"rb"); break;
		case ROM_PORTABLEIII:		f = nvrfopen(L"portableiii.nvr",	L"rb"); break;
                case ROM_AMI286:		f = nvrfopen(L"ami286.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_AWARD286:		f = nvrfopen(L"award286.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_DELL200:		f = nvrfopen(L"dell200.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_SUPER286TR:		f = nvrfopen(L"super286tr.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_SPC4200P:		f = nvrfopen(L"spc4200p.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_IBMAT386:		f = nvrfopen(L"at386.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_DESKPRO_386:		f = nvrfopen(L"deskpro386.nvr",		L"rb"); break;
		case ROM_PORTABLEIII386:	f = nvrfopen(L"portableiii386.nvr",	L"rb"); break;
                case ROM_MEGAPC:		f = nvrfopen(L"megapc.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_MEGAPCDX:		f = nvrfopen(L"megapcdx.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_AMI386SX:		f = nvrfopen(L"ami386.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_AMI486:		f = nvrfopen(L"ami486.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_WIN486:		f = nvrfopen(L"win486.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_SIS496:		f = nvrfopen(L"sis496.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_430VX:			f = nvrfopen(L"430vx.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_REVENGE:		f = nvrfopen(L"revenge.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_ENDEAVOR:		f = nvrfopen(L"endeavor.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_DTK386:		f = nvrfopen(L"dtk386.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_MR386DX_OPTI495:	f = nvrfopen(L"mr386dx_opti495.nvr",	L"rb"); nvrmask = 127; break;
                case ROM_AMI386DX_OPTI495:	f = nvrfopen(L"ami386dx_opti495.nvr",	L"rb"); nvrmask = 127; break;
                case ROM_DTK486:		f = nvrfopen(L"dtk486.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_R418:			f = nvrfopen(L"r418.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_586MC1:		f = nvrfopen(L"586mc1.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_PLATO:			f = nvrfopen(L"plato.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_MB500N:		f = nvrfopen(L"mb500n.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_P54TP4XE:		f = nvrfopen(L"p54tp4xe.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_AP53:			f = nvrfopen(L"ap53.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_P55T2S:		f = nvrfopen(L"p55t2s.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_ACERM3A:		f = nvrfopen(L"acerm3a.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_ACERV35N:		f = nvrfopen(L"acerv35n.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_P55VA:			f = nvrfopen(L"p55va.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_P55T2P4:		f = nvrfopen(L"p55t2p4.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_P55TVP4:		f = nvrfopen(L"p55tvp4.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_440FX:			f = nvrfopen(L"440fx.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_THOR:			f = nvrfopen(L"thor.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_MRTHOR:		f = nvrfopen(L"mrthor.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_ZAPPA:			f = nvrfopen(L"zappa.nvr",		L"rb"); nvrmask = 127; break;
                case ROM_S1668:			f = nvrfopen(L"tpatx.nvr",		L"rb"); nvrmask = 127; break;
                default: return;
        }
        if (!f)
        {
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
void savenvr()
{
        FILE *f;
        switch (oldromset)
        {
                case ROM_PC1512:		f = nvrfopen(L"pc1512.nvr",		L"wb"); break;
                case ROM_PC1640:		f = nvrfopen(L"pc1640.nvr",		L"wb"); break;
                case ROM_PC200:			f = nvrfopen(L"pc200.nvr",		L"wb"); break;
                case ROM_PC2086:		f = nvrfopen(L"pc2086.nvr",		L"wb"); break;
                case ROM_PC3086:		f = nvrfopen(L"pc3086.nvr",		L"wb"); break;
                case ROM_IBMAT:			f = nvrfopen(L"at.nvr",			L"wb"); break;
                case ROM_IBMPS1_2011:		f = nvrfopen(L"ibmps1_2011.nvr",	L"wb"); break;
                case ROM_IBMPS1_2121:		f = nvrfopen(L"ibmps1_2121.nvr",	L"wb"); break;
                case ROM_IBMPS1_2121_ISA:	f = nvrfopen(L"ibmps1_2121_isa.nvr",	L"wb"); break;
                case ROM_IBMPS2_M30_286:	f = nvrfopen(L"ibmps2_m30_286.nvr",	L"wb"); break;
                case ROM_IBMPS2_M50:		f = nvrfopen(L"ibmps2_m50.nvr",		L"wb"); break;
                case ROM_IBMPS2_M55SX:		f = nvrfopen(L"ibmps2_m55sx.nvr",	L"wb"); break;
                case ROM_IBMPS2_M80:		f = nvrfopen(L"ibmps2_m80.nvr",		L"wb"); break;
                case ROM_CMDPC30:		f = nvrfopen(L"cmdpc30.nvr",		L"wb"); break;
		case ROM_PORTABLEII:		f = nvrfopen(L"portableii.nvr",		L"wb"); break;
		case ROM_PORTABLEIII:		f = nvrfopen(L"portableiii.nvr",	L"wb"); break;
                case ROM_AMI286:		f = nvrfopen(L"ami286.nvr",		L"wb"); break;
                case ROM_AWARD286:		f = nvrfopen(L"award286.nvr",		L"wb"); break;
                case ROM_DELL200:		f = nvrfopen(L"dell200.nvr",		L"wb"); break;
                case ROM_SUPER286TR:		f = nvrfopen(L"super286tr.nvr",		L"wb"); break;
                case ROM_SPC4200P:		f = nvrfopen(L"spc4200p.nvr",		L"wb"); break;
                case ROM_IBMAT386:		f = nvrfopen(L"at386.nvr",		L"wb"); break;
                case ROM_DESKPRO_386:		f = nvrfopen(L"deskpro386.nvr",		L"wb"); break;
		case ROM_PORTABLEIII386:	f = nvrfopen(L"portableiii386.nvr",	L"wb"); break;
                case ROM_MEGAPC:		f = nvrfopen(L"megapc.nvr",		L"wb"); break;
                case ROM_MEGAPCDX:		f = nvrfopen(L"megapcdx.nvr",		L"wb"); break;
                case ROM_AMI386SX:		f = nvrfopen(L"ami386.nvr",		L"wb"); break;
                case ROM_AMI486:		f = nvrfopen(L"ami486.nvr",		L"wb"); break;
                case ROM_WIN486:		f = nvrfopen(L"win486.nvr",		L"wb"); break;
                case ROM_SIS496:		f = nvrfopen(L"sis496.nvr",		L"wb"); break;
                case ROM_430VX:			f = nvrfopen(L"430vx.nvr",		L"wb"); break;
                case ROM_REVENGE:		f = nvrfopen(L"revenge.nvr",		L"wb"); break;
                case ROM_ENDEAVOR:		f = nvrfopen(L"endeavor.nvr",		L"wb"); break;
                case ROM_DTK386:		f = nvrfopen(L"dtk386.nvr",		L"wb"); break;
                case ROM_MR386DX_OPTI495:	f = nvrfopen(L"mr386dx_opti495.nvr",	L"wb"); break;
                case ROM_AMI386DX_OPTI495:	f = nvrfopen(L"ami386dx_opti495.nvr",	L"wb"); break;
                case ROM_DTK486:		f = nvrfopen(L"dtk486.nvr",		L"wb"); break;
                case ROM_R418:			f = nvrfopen(L"r418.nvr",		L"wb"); break;
                case ROM_586MC1:		f = nvrfopen(L"586mc1.nvr",		L"wb"); break;
                case ROM_PLATO:			f = nvrfopen(L"plato.nvr",		L"wb"); break;
                case ROM_MB500N:		f = nvrfopen(L"mb500n.nvr",		L"wb"); break;
                case ROM_P54TP4XE:		f = nvrfopen(L"p54tp4xe.nvr",		L"wb"); break;
                case ROM_AP53:			f = nvrfopen(L"ap53.nvr",		L"wb"); break;
                case ROM_P55T2S:		f = nvrfopen(L"p55t2s.nvr",		L"wb"); break;
                case ROM_ACERM3A:		f = nvrfopen(L"acerm3a.nvr",		L"wb"); break;
                case ROM_ACERV35N:		f = nvrfopen(L"acerv35n.nvr",		L"wb"); break;
                case ROM_P55VA:			f = nvrfopen(L"p55va.nvr",		L"wb"); break;
                case ROM_P55T2P4:		f = nvrfopen(L"p55t2p4.nvr",		L"wb"); break;
                case ROM_P55TVP4:		f = nvrfopen(L"p55tvp4.nvr",		L"wb"); break;
                case ROM_440FX:			f = nvrfopen(L"440fx.nvr",		L"wb"); break;
                case ROM_THOR:			f = nvrfopen(L"thor.nvr",		L"wb"); break;
                case ROM_MRTHOR:		f = nvrfopen(L"mrthor.nvr",		L"wb"); break;
                case ROM_ZAPPA:			f = nvrfopen(L"zappa.nvr",		L"wb"); break;
                case ROM_S1668:			f = nvrfopen(L"tpatx.nvr",		L"wb"); break;
                default: return;
        }
        fwrite(nvrram,128,1,f);
        fclose(f);
}

void nvr_init()
{
        io_sethandler(0x0070, 0x0002, readnvr, NULL, NULL, writenvr, NULL, NULL,  NULL);
        timer_add(nvr_rtc, &rtctime, TIMER_ALWAYS_ENABLED, NULL);
        timer_add(nvr_onesec, &nvr_onesec_time, TIMER_ALWAYS_ENABLED, NULL);
        timer_add(nvr_update_end, &nvr_update_end_count, &nvr_update_end_count, NULL);

}
