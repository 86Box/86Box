#include <stdio.h>
#include "ibm.h"
#include "io.h"
#include "nvr.h"
#include "pic.h"
#include "timer.h"
#include "rtc.h"

int oldromset;
int nvrmask=63;
uint8_t nvrram[128];
int nvraddr;

int nvr_dosave = 0;

static int64_t nvr_onesec_time = 0, nvr_onesec_cnt = 0;

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
//        pclog("RTCtime now %f\n",rtctime);
        nvrram[RTC_REGC] |= RTC_PF;
        if (nvrram[RTC_REGB] & RTC_PIE)
        {
                nvrram[RTC_REGC] |= RTC_IRQF;
                if (AMSTRAD) picint(2);
                else         picint(0x100);
//                pclog("RTC int\n");
        }
}

int nvr_update_status = 0;

#define ALARM_DONTCARE	0xc0

int nvr_check_alarm(int nvraddr)
{
        return (nvrram[nvraddr + 1] == nvrram[nvraddr] || (nvrram[nvraddr + 1] & ALARM_DONTCARE) == ALARM_DONTCARE);
}

int64_t nvr_update_end_count = 0;

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
        
//                pclog("RTC onesec\n");

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
//        printf("Write NVR %03X %02X %02X %04X:%04X %i\n",addr,nvraddr,val,cs>>4,pc,ins);
        if (addr&1)
        {
                if (nvraddr==RTC_REGC || nvraddr==RTC_REGD)
                        return; /* Registers C and D are read-only. There's no reason to continue. */
//                if (nvraddr == 0x33) pclog("NVRWRITE33 %02X %04X:%04X %i\n",val,CS,pc,ins);
                if (nvraddr > RTC_REGD && nvrram[nvraddr] != val)
                   nvr_dosave = 1;
                
		old = nvrram[nvraddr];
                nvrram[nvraddr]=val;

                if (nvraddr == RTC_REGA)
                {
//                        pclog("NVR rate %i\n",val&0xF);
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
//        printf("Read NVR %03X %02X %02X %04X:%04X\n",addr,nvraddr,nvrram[nvraddr],cs>>4,pc);
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
//                if (AMIBIOS && nvraddr==0x36) return 0;
//                if (nvraddr==0xA) nvrram[0xA]^=0x80;
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
                case ROM_PC1512:      f = romfopen("nvr/pc1512.nvr",      "rb"); break;
                case ROM_PC1640:      f = romfopen("nvr/pc1640.nvr",      "rb"); break;
                case ROM_PC200:       f = romfopen("nvr/pc200.nvr",       "rb"); break;
                case ROM_PC2086:      f = romfopen("nvr/pc2086.nvr",      "rb"); break;
                case ROM_PC3086:      f = romfopen("nvr/pc3086.nvr",      "rb"); break;                
                case ROM_IBMAT:       f = romfopen("nvr/at.nvr",          "rb"); break;
                case ROM_IBMPS1_2011: f = romfopen("nvr/ibmps1_2011.nvr", "rb"); /*nvrmask = 127; */break;
                case ROM_IBMPS1_2121: f = romfopen("nvr/ibmps1_2121.nvr", "rb"); nvrmask = 127; break;
                case ROM_CMDPC30:     f = romfopen("nvr/cmdpc30.nvr",     "rb"); nvrmask = 127; break;
                case ROM_AMI286:      f = romfopen("nvr/ami286.nvr",      "rb"); nvrmask = 127; break;
                case ROM_AWARD286:    f = romfopen("nvr/award286.nvr",    "rb"); nvrmask = 127; break;
                case ROM_DELL200:     f = romfopen("nvr/dell200.nvr",     "rb"); nvrmask = 127; break;
                case ROM_IBMAT386:    f = romfopen("nvr/at386.nvr",       "rb"); nvrmask = 127; break;
                case ROM_DESKPRO_386: f = romfopen("nvr/deskpro386.nvr",  "rb"); break;
                case ROM_ACER386:     f = romfopen("nvr/acer386.nvr",     "rb"); nvrmask = 127; break;
                case ROM_MEGAPC:      f = romfopen("nvr/megapc.nvr",      "rb"); nvrmask = 127; break;
                case ROM_AMI386:      f = romfopen("nvr/ami386.nvr",      "rb"); nvrmask = 127; break;
                case ROM_AMI486:      f = romfopen("nvr/ami486.nvr",      "rb"); nvrmask = 127; break;
                case ROM_WIN486:      f = romfopen("nvr/win486.nvr",      "rb"); nvrmask = 127; break;
                case ROM_PCI486:      f = romfopen("nvr/hot-433.nvr",     "rb"); nvrmask = 127; break;
                case ROM_SIS496:      f = romfopen("nvr/sis496.nvr",      "rb"); nvrmask = 127; break;
                case ROM_430VX:       f = romfopen("nvr/430vx.nvr",       "rb"); nvrmask = 127; break;
                case ROM_REVENGE:     f = romfopen("nvr/revenge.nvr",     "rb"); nvrmask = 127; break;
                case ROM_ENDEAVOR:    f = romfopen("nvr/endeavor.nvr",    "rb"); nvrmask = 127; break;
                case ROM_PX386:       f = romfopen("nvr/px386.nvr",       "rb"); nvrmask = 127; break;
                case ROM_DTK386:      f = romfopen("nvr/dtk386.nvr",      "rb"); nvrmask = 127; break;
                case ROM_DTK486:      f = romfopen("nvr/dtk486.nvr",      "rb"); nvrmask = 127; break;
                case ROM_R418:        f = romfopen("nvr/r418.nvr",        "rb"); nvrmask = 127; break;
                case ROM_586MC1:      f = romfopen("nvr/586mc1.nvr",      "rb"); nvrmask = 127; break;
                case ROM_PLATO:       f = romfopen("nvr/plato.nvr",       "rb"); nvrmask = 127; break;
                case ROM_MB500N:      f = romfopen("nvr/mb500n.nvr",      "rb"); nvrmask = 127; break;
#if 0
                case ROM_P54TP4XE:    f = romfopen("nvr/p54tp4xe.nvr",    "rb"); nvrmask = 127; break;
#endif
                case ROM_ACERM3A:     f = romfopen("nvr/acerm3a.nvr",     "rb"); nvrmask = 127; break;
                case ROM_ACERV35N:    f = romfopen("nvr/acerv35n.nvr",    "rb"); nvrmask = 127; break;
#if 0
                case ROM_P55T2P4:     f = romfopen("nvr/p55t2p4.nvr",     "rb"); nvrmask = 127; break;
#endif
                case ROM_P55TVP4:     f = romfopen("nvr/p55tvp4.nvr",     "rb"); nvrmask = 127; break;
                case ROM_P55VA:       f = romfopen("nvr/p55va.nvr",       "rb"); nvrmask = 127; break;
                case ROM_440FX:       f = romfopen("nvr/440fx.nvr",       "rb"); nvrmask = 127; break;
                case ROM_KN97:        f = romfopen("nvr/kn97.nvr",        "rb"); nvrmask = 127; break;
                case ROM_MARL:        f = romfopen("nvr/marl.nvr",        "rb"); nvrmask = 127; break;
                case ROM_THOR:        f = romfopen("nvr/thor.nvr",        "rb"); nvrmask = 127; break;
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
                case ROM_PC1512:      f = romfopen("nvr/pc1512.nvr",      "wb"); break;
                case ROM_PC1640:      f = romfopen("nvr/pc1640.nvr",      "wb"); break;
                case ROM_PC200:       f = romfopen("nvr/pc200.nvr",       "wb"); break;
                case ROM_PC2086:      f = romfopen("nvr/pc2086.nvr",      "wb"); break;
                case ROM_PC3086:      f = romfopen("nvr/pc3086.nvr",      "wb"); break;
                case ROM_IBMAT:       f = romfopen("nvr/at.nvr",          "wb"); break;
                case ROM_IBMPS1_2011: f = romfopen("nvr/ibmps1_2011.nvr", "wb"); break;
                case ROM_IBMPS1_2121: f = romfopen("nvr/ibmps1_2121.nvr", "wb"); break;
                case ROM_CMDPC30:     f = romfopen("nvr/cmdpc30.nvr",     "wb"); break;                
                case ROM_AMI286:      f = romfopen("nvr/ami286.nvr",      "wb"); break;
                case ROM_AWARD286:    f = romfopen("nvr/award286.nvr",    "wb"); break;
                case ROM_DELL200:     f = romfopen("nvr/dell200.nvr",     "wb"); break;
                case ROM_IBMAT386:    f = romfopen("nvr/at386.nvr",       "wb"); break;
                case ROM_DESKPRO_386: f = romfopen("nvr/deskpro386.nvr",  "wb"); break;
                case ROM_ACER386:     f = romfopen("nvr/acer386.nvr",     "wb"); break;
                case ROM_MEGAPC:      f = romfopen("nvr/megapc.nvr",      "wb"); break;
                case ROM_AMI386:      f = romfopen("nvr/ami386.nvr",      "wb"); break;
                case ROM_AMI486:      f = romfopen("nvr/ami486.nvr",      "wb"); break;
                case ROM_WIN486:      f = romfopen("nvr/win486.nvr",      "wb"); break;
                case ROM_PCI486:      f = romfopen("nvr/hot-433.nvr",     "wb"); break;
                case ROM_SIS496:      f = romfopen("nvr/sis496.nvr",      "wb"); break;
                case ROM_430VX:       f = romfopen("nvr/430vx.nvr",       "wb"); break;
                case ROM_REVENGE:     f = romfopen("nvr/revenge.nvr",     "wb"); break;
                case ROM_ENDEAVOR:    f = romfopen("nvr/endeavor.nvr",    "wb"); break;
                case ROM_PX386:       f = romfopen("nvr/px386.nvr",       "wb"); break;
                case ROM_DTK386:      f = romfopen("nvr/dtk386.nvr",      "wb"); break;
                case ROM_DTK486:      f = romfopen("nvr/dtk486.nvr",      "wb"); break;
                case ROM_R418:        f = romfopen("nvr/r418.nvr",        "wb"); break;
                case ROM_586MC1:      f = romfopen("nvr/586mc1.nvr",      "wb"); break;
                case ROM_PLATO:       f = romfopen("nvr/plato.nvr",       "wb"); break;
                case ROM_MB500N:      f = romfopen("nvr/mb500n.nvr",      "wb"); break;
#if 0
                case ROM_P54TP4XE:    f = romfopen("nvr/p54tp4xe.nvr",    "wb"); break;
#endif
                case ROM_ACERM3A:     f = romfopen("nvr/acerm3a.nvr",     "wb"); break;
                case ROM_ACERV35N:    f = romfopen("nvr/acerv35n.nvr",    "wb"); break;
#if 0
                case ROM_P55T2P4:     f = romfopen("nvr/p55t2p4.nvr",     "wb"); break;
#endif
                case ROM_P55TVP4:     f = romfopen("nvr/p55tvp4.nvr",     "wb"); break;
                case ROM_P55VA:       f = romfopen("nvr/p55va.nvr",       "wb"); break;
                case ROM_440FX:       f = romfopen("nvr/440fx.nvr",       "wb"); break;
                case ROM_KN97:        f = romfopen("nvr/kn97.nvr",        "wb"); break;
                case ROM_MARL:        f = romfopen("nvr/marl.nvr",        "wb"); break;
                case ROM_THOR:        f = romfopen("nvr/thor.nvr",        "wb"); break;
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
