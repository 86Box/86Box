/*IBM AT -
  Write B0
  Write aa55
  Expects aa55 back*/

#include <string.h>
#include "ibm.h"

#include "cpu.h"
#include "dma.h"
#include "io.h"
#include "pic.h"
#include "pit.h"
#include "timer.h"
#include "video.h"
#include "model.h"
/*B0 to 40, two writes to 43, then two reads - value does not change!*/
/*B4 to 40, two writes to 43, then two reads - value _does_ change!*/
//Tyrian writes 4300 or 17512
int displine;

double PITCONST;
float cpuclock;
float isa_timing, bus_timing;

int firsttime=1;
void setpitclock(float clock)
{
//        printf("PIT clock %f\n",clock);
        cpuclock=clock;
        PITCONST=clock/1193182.0;
        CGACONST=(clock/(19687503.0/11.0));
        MDACONST=(clock/2032125.0);
        VGACONST1=(clock/25175000.0);
        VGACONST2=(clock/28322000.0);
        isa_timing = clock/8000000.0;
        bus_timing = clock/(double)cpu_busspeed;
        video_updatetiming();
//        pclog("PITCONST=%f CGACONST=%f\n", PITCONST, CGACONST);
//        pclog("CPUMULTI=%g\n", ((14318184.0*(double)(1 << TIMER_SHIFT)) / (double)models[model].cpu[cpu_manufacturer].cpus[cpu].rspeed));
        
        xt_cpu_multi = (int)((14318184.0*(double)(1 << TIMER_SHIFT)) / (double)models[model].cpu[cpu_manufacturer].cpus[cpu].rspeed);
//        pclog("egacycles %i egacycles2 %i temp %f clock %f\n",egacycles,egacycles2,temp,clock);
/*        if (video_recalctimings)
                video_recalctimings();*/
        RTCCONST=clock/32768.0;
        TIMER_USEC = (int)((clock / 1000000.0f) * (float)(1 << TIMER_SHIFT));
        device_speed_changed();
}

//#define PITCONST (8000000.0/1193000.0)
//#define PITCONST (cpuclock/1193000.0)
void pit_reset()
{
        memset(&pit,0,sizeof(PIT));
        pit.l[0]=0xFFFF; pit.c[0]=0xFFFF*PITCONST;
        pit.l[1]=0xFFFF; pit.c[1]=0xFFFF*PITCONST;
        pit.l[2]=0xFFFF; pit.c[2]=0xFFFF*PITCONST;
        pit.m[0]=pit.m[1]=pit.m[2]=0;
        pit.ctrls[0]=pit.ctrls[1]=pit.ctrls[2]=0;
        pit.thit[0]=1;
        spkstat=0;
        pit.gate[0] = pit.gate[1] = 1;
        pit.gate[2] = 0;
        pit.using_timer[0] = pit.using_timer[1] = pit.using_timer[2] = 1;
}

void clearpit()
{
        pit.c[0]=(pit.l[0]<<2);
}

float pit_timer0_freq()
{
        if (pit.l[0])
                return 1193182.0f/(float)pit.l[0];
        else
                return 1193182.0f/(float)0x10000;
}

static void (*pit_set_out_funcs[3])(int new_out, int old_out);

static void pit_set_out(int t, int out)
{
        pit_set_out_funcs[t](out, pit.out[t]);
        pit.out[t] = out;
}

static void pit_load(int t)
{
        int l = pit.l[t] ? pit.l[t] : 0x10000;
        timer_process();
        pit.newcount[t] = 0;
        pit.disabled[t] = 0;
//        pclog("pit_load: t=%i l=%x\n", t, l);
        switch (pit.m[t])
        {
                case 0: /*Interrupt on terminal count*/
                pit.count[t] = l;
                pit.c[t] = (int)((l << TIMER_SHIFT) * PITCONST);
                pit_set_out(t, 0);
                pit.thit[t] = 0;
                pit.enabled[t] = pit.gate[t];
                break;
                case 1: /*Hardware retriggerable one-shot*/
                pit.enabled[t] = 1;
                break;
                case 2: /*Rate generator*/
                if (pit.initial[t])
                {
                        pit.count[t] = l - 1;
                        pit.c[t] = (int)(((l - 1) << TIMER_SHIFT) * PITCONST);
                        pit_set_out(t, 1);
                        pit.thit[t] = 0;
                }
                pit.enabled[t] = pit.gate[t];
                break;
                case 3: /*Square wave mode*/
                if (pit.initial[t])
                {
                        pit.count[t] = l;
                        pit.c[t] = (int)((((l + 1) >> 1) << TIMER_SHIFT) * PITCONST);
                        pit_set_out(t, 1);
                        pit.thit[t] = 0;
                }
                pit.enabled[t] = pit.gate[t];
//                pclog("pit_load: square wave mode c=%x\n", pit.c[t]);
                break;
                case 4: /*Software triggered stobe*/
                if (!pit.thit[t] && !pit.initial[t])
                        pit.newcount[t] = 1;
                else
                {
                        pit.count[t] = l;
                        pit.c[t] = (int)((l << TIMER_SHIFT) * PITCONST);
                        pit_set_out(t, 0);
                        pit.thit[t] = 0;
                }
                pit.enabled[t] = pit.gate[t];
                break;
                case 5: /*Hardware triggered stobe*/
                pit.enabled[t] = 1;
                break;
        }
        pit.initial[t] = 0;
        pit.running[t] = pit.enabled[t] && pit.using_timer[t] && !pit.disabled[t];
        timer_update_outstanding();
//        pclog("pit_load: t=%i running=%i thit=%i enabled=%i m=%i l=%x c=%g gate=%i\n", t, pit.running[t], pit.thit[t], pit.enabled[t], pit.m[t], pit.l[t], pit.c[t], pit.gate[t]);
}

void pit_set_gate(int t, int gate)
{
        int l = pit.l[t] ? pit.l[t] : 0x10000;

        if (pit.disabled[t])
        {
                pit.gate[t] = gate;
                return;
        }
                
        timer_process();
        switch (pit.m[t])
        {
                case 0: /*Interrupt on terminal count*/
                case 4: /*Software triggered stobe*/
                pit.enabled[t] = gate;
                break;
                case 1: /*Hardware retriggerable one-shot*/
                case 5: /*Hardware triggered stobe*/
                if (gate && !pit.gate[t])
                {
                        pit.count[t] = l;
                        pit.c[t] = (int)((l << TIMER_SHIFT) * PITCONST);
                        pit_set_out(t, 0);
                        pit.thit[t] = 0;
                        pit.enabled[t] = 1;
                }
                break;
                case 2: /*Rate generator*/
                if (gate && !pit.gate[t])
                {
                        pit.count[t] = l - 1;
                        pit.c[t] = (int)(((l - 1) << TIMER_SHIFT) * PITCONST);
                        pit_set_out(t, 1);
                        pit.thit[t] = 0;
                }                
                pit.enabled[t] = gate;
                break;
                case 3: /*Square wave mode*/
                if (gate && !pit.gate[t])
                {
                        pit.count[t] = l;
                        pit.c[t] = (int)((((l + 1) >> 1) << TIMER_SHIFT) * PITCONST);
                        pit_set_out(t, 1);
                        pit.thit[t] = 0;
                }
                pit.enabled[t] = gate;
                break;
        }
        pit.gate[t] = gate;
        pit.running[t] = pit.enabled[t] && pit.using_timer[t] && !pit.disabled[t];
        timer_update_outstanding();
//        pclog("pit_set_gate: t=%i gate=%i\n", t, gate);
}

static void pit_over(int t)
{
        int l = pit.l[t] ? pit.l[t] : 0x10000;
        if (pit.disabled[t])
        {
                pit.count[t] += 0xffff;
                pit.c[t] += (int)((0xffff << TIMER_SHIFT) * PITCONST);
                return;
        }
                
//        if (!t) pclog("pit_over: t=%i l=%x c=%x %i hit=%i\n", t, pit.l[t], pit.c[t], pit.c[t] >> TIMER_SHIFT, pit.thit[t]);
        switch (pit.m[t])
        {
                case 0: /*Interrupt on terminal count*/
                case 1: /*Hardware retriggerable one-shot*/
                if (!pit.thit[t])
                        pit_set_out(t, 1);
                pit.thit[t] = 1;
                pit.count[t] += 0xffff;
                pit.c[t] += (int)((0xffff << TIMER_SHIFT) * PITCONST);
                break;
                case 2: /*Rate generator*/
                pit.count[t] += l;
                pit.c[t] += (int)((l << TIMER_SHIFT) * PITCONST);
                pit_set_out(t, 0);
                pit_set_out(t, 1);
                break;
                case 3: /*Square wave mode*/
                if (pit.out[t])
                {
                        pit_set_out(t, 0);
                        pit.count[t] += (l >> 1);
                        pit.c[t] += (int)(((l >> 1) << TIMER_SHIFT) * PITCONST);
                }
                else
                {
                        pit_set_out(t, 1);
                        pit.count[t] += ((l + 1) >> 1);
                        pit.c[t] = (int)((((l + 1) >> 1) << TIMER_SHIFT) * PITCONST);
                }
//                if (!t) pclog("pit_over: square wave mode c=%x  %lli  %f\n", pit.c[t], tsc, PITCONST);
                break;
                case 4: /*Software triggered strove*/
                if (!pit.thit[t])
                {
                        pit_set_out(t, 0);
                        pit_set_out(t, 1);
                }
                if (pit.newcount[t])
                {
                        pit.newcount[t] = 0;
                        pit.count[t] += l;
                        pit.c[t] += (int)((l << TIMER_SHIFT) * PITCONST);
                }
                else
                {
                        pit.thit[t] = 1;
                        pit.count[t] += 0xffff;
                        pit.c[t] += (int)((0xffff << TIMER_SHIFT) * PITCONST);
                }
                break;
                case 5: /*Hardware triggered strove*/
                if (!pit.thit[t])
                {
                        pit_set_out(t, 0);
                        pit_set_out(t, 1);
                }
                pit.thit[t] = 1;
                pit.count[t] += 0xffff;
                pit.c[t] += (int)((0xffff << TIMER_SHIFT) * PITCONST);
                break;
        }
        pit.running[t] = pit.enabled[t] && pit.using_timer[t] && !pit.disabled[t];
}

int pit_get_timer_0()
{
        int read = (int)((pit.c[0] + ((1 << TIMER_SHIFT) - 1)) / PITCONST) >> TIMER_SHIFT;
//pclog("pit_get_timer_0: t=%i using_timer=%i m=%i\n", 0, pit.using_timer[0], pit.m[0]);
        if (pit.m[0] == 2)
                read++;
        if (read < 0)
                read = 0;
        if (read > 0x10000)
                read = 0x10000;
        if (pit.m[0] == 3)
                read <<= 1;
        return read;
}
        
static int pit_read_timer(int t)
{
        timer_clock();
//        pclog("pit_read_timer: t=%i using_timer=%i m=%i\n", t, pit.using_timer[t], pit.m[t]);
        if (pit.using_timer[t])
        {
                int read = (int)((pit.c[t] + ((1 << TIMER_SHIFT) - 1)) / PITCONST) >> TIMER_SHIFT;
                if (pit.m[t] == 2)
                        read++;
                if (read < 0)
                        read = 0;
                if (read > 0x10000)
                        read = 0x10000;
                if (pit.m[t] == 3)
                        read <<= 1;
                return read;
        }
        if (pit.m[t] == 2)
                return pit.count[t] + 1;
        return pit.count[t];
}
        
void pit_write(uint16_t addr, uint8_t val, void *priv)
{
        int t;
        cycles -= (int)PITCONST;
//        /*if (val != 0x40) */pclog("Write PIT %04X %02X %04X:%08X %i %i\n",addr,val,CS,pc,ins, pit.gate[0]);
        
        switch (addr&3)
        {
                case 3: /*CTRL*/
                if ((val&0xC0)==0xC0)
                {
                        if (!(val&0x20))
                        {
                                if (val & 2)
                                        pit.rl[0] = pit.using_timer[0] ? ((int)(pit.c[0] / PITCONST) >> TIMER_SHIFT) : pit.count[0];
                                if (val & 4)
                                        pit.rl[1] = pit.using_timer[1] ? ((int)(pit.c[1] / PITCONST) >> TIMER_SHIFT) : pit.count[1];
                                if (val & 8)
                                        pit.rl[2] = pit.using_timer[2] ? ((int)(pit.c[2] / PITCONST) >> TIMER_SHIFT) : pit.count[2];
                        }
                        if (!(val & 0x10))
                        {
                                if (val & 2)
                                {
                                        pit.read_status[0] = (pit.ctrls[0] & 0x3f) | 0x40 | (pit.out[0] ? 0x80 : 0);
                                        pit.do_read_status[0] = 1;
                                }
                                if (val & 4)
                                {
                                        pit.read_status[1] = (pit.ctrls[1] & 0x3f) | 0x40 | (pit.out[1] ? 0x80 : 0);
                                        pit.do_read_status[1] = 1;
                                }
                                if (val & 8)
                                {
                                        pit.read_status[2] = (pit.ctrls[2] & 0x3f) | 0x40 | (pit.out[2] ? 0x80 : 0);
                                        pit.do_read_status[2] = 1;
                                }
                        }
                        return;
                }
                t = val >> 6;
                pit.ctrl=val;
                if ((val>>7)==3)
                {
                        printf("Bad PIT reg select\n");
                        return;
//                        dumpregs();
//                        exit(-1);
                }
//                printf("CTRL write %02X\n",val);
                if (!(pit.ctrl&0x30))
                {
                        pit.rl[t] = pit_read_timer(t);
//                        pclog("Timer latch %f %04X %04X\n",pit.c[0],pit.rl[0],pit.l[0]);
                        pit.ctrl |= 0x30;
                        pit.rereadlatch[t] = 0;
                        pit.rm[t] = 3;
                        pit.latched[t] = 1;
                }
                else
                {
                        pit.ctrls[val>>6] = val;
                        pit.rm[val>>6]=pit.wm[val>>6]=(pit.ctrl>>4)&3;
                        pit.m[val>>6]=(val>>1)&7;
                        if (pit.m[val>>6]>5)
                                pit.m[val>>6]&=3;
                        if (!(pit.rm[val>>6]))
                        {
                                pit.rm[val>>6]=3;
                                pit.rl[t] = pit_read_timer(t);
                        }
                        pit.rereadlatch[val>>6]=1;
                        if ((val>>6)==2) ppispeakon=speakon=(pit.m[2]==0)?0:1;
                        pit.initial[t] = 1;
                        if (!pit.m[val >> 6])
                                pit_set_out(val >> 6, 0);
                        else
                                pit_set_out(val >> 6, 1);
                        pit.disabled[val >> 6] = 1;
//                                pclog("ppispeakon %i\n",ppispeakon);
                }
                pit.wp=0;
                pit.thit[pit.ctrl>>6]=0;
                break;
                case 0: case 1: case 2: /*Timers*/
                t=addr&3;
//                if (t==2) ppispeakon=speakon=0;
//                pclog("Write timer %02X %i\n",pit.ctrls[t],pit.wm[t]);
                switch (pit.wm[t])
                {
                        case 1:
                        pit.l[t]=val;
//                        pit.thit[t]=0;
                        pit_load(t);
//                        pit.c[t]=pit.l[t]*PITCONST;
//                        if (!t)
//                                picintc(1);
                        break;
                        case 2:
                        pit.l[t]=(val<<8);
//                        pit.thit[t]=0;
                        pit_load(t);
//                        pit.c[t]=pit.l[t]*PITCONST;
//                        if (!t)
//                                picintc(1);
                        break;
                        case 0:
                        pit.l[t]&=0xFF;
                        pit.l[t]|=(val<<8);
                        pit_load(t);
//                        pit.c[t]=pit.l[t]*PITCONST;
//                        pclog("%04X %f\n",pit.l[t],pit.c[t]);                        
//                        pit.thit[t]=0;
                        pit.wm[t]=3;
//                        if (!t)
//                                picintc(1);
                        break;
                        case 3:
                        pit.l[t]&=0xFF00;
                        pit.l[t]|=val;
                        pit.wm[t]=0;
                        break;
                }
                speakval=(((float)pit.l[2]/(float)pit.l[0])*0x4000)-0x2000;
//                printf("Speakval now %i\n",speakval);
//                if (speakval>0x2000)
//                   printf("Speaker overflow - %i %i %04X %04X\n",pit.l[0],pit.l[2],pit.l[0],pit.l[2]);
                if (speakval>0x2000) speakval=0x2000;
/*                if (!pit.l[t])
                {
                        pit.l[t]|=0x10000;
                        pit.c[t]=pit.l[t]*PITCONST;
                }*/
                break;
        }
}

uint8_t pit_read(uint16_t addr, void *priv)
{
        int t;
        uint8_t temp;
        cycles -= (int)PITCONST;        
//        printf("Read PIT %04X ",addr);
        switch (addr&3)
        {
                case 0: case 1: case 2: /*Timers*/
                t = addr & 3;
                if (pit.do_read_status[t])
                {
                        pit.do_read_status[t] = 0;
                        temp = pit.read_status[t];
                        break;
                }
                if (pit.rereadlatch[addr & 3] && !pit.latched[addr & 3])
                {
                        pit.rereadlatch[addr & 3] = 0;
                        pit.rl[t] = pit_read_timer(t);
                }
                switch (pit.rm[addr & 3])
                {
                        case 0:
                        temp = pit.rl[addr & 3] >> 8;
                        pit.rm[addr & 3] = 3;
                        pit.latched[addr & 3] = 0;
                        pit.rereadlatch[addr & 3] = 1;
                        break;
                        case 1:
                        temp = (pit.rl[addr & 3]) & 0xFF;
                        pit.latched[addr & 3] = 0;
                        pit.rereadlatch[addr & 3] = 1;
                        break;
                        case 2:
                        temp = (pit.rl[addr & 3]) >> 8;
                        pit.latched[addr & 3] = 0;
                        pit.rereadlatch[addr & 3] = 1;
                        break;
                        case 3:
                        temp = (pit.rl[addr & 3]) & 0xFF;
                        if (pit.m[addr & 3] & 0x80)
                                pit.m[addr & 3] &= 7;
                        else
                                pit.rm[addr & 3] = 0;
                        break;
                }
                break;
                case 3: /*Control*/
                temp = pit.ctrl;
                break;
        }
//        pclog("%02X\n", temp);
//        printf("%02X %i %i %04X:%04X %i\n",temp,pit.rm[addr&3],pit.wp,cs>>4,pc, ins);
        return temp;
}

void pit_poll()
{
//                printf("Poll pit %f %f %f\n",pit.c[0],pit.c[1],pit.c[2]);
        if (pit.c[0] < 1 && pit.running[0])
                pit_over(0);
        if (pit.c[1] < 1 && pit.running[1])
                pit_over(1);
        if (pit.c[2] < 1 && pit.running[2])
                pit_over(2);
}

void pit_timer_over(void *p)
{
        int timer = (int) p;
//        pclog("pit_timer_over %i\n", timer);
        
        pit_over(timer);
}

void pit_clock(int t)
{
        if (pit.thit[t] || !pit.enabled[t])
                return;
        
        if (pit.using_timer[t])
                return;
                
        pit.count[t] -= (pit.m[t] == 3) ? 2 : 1;
        if (!pit.count[t])
                pit_over(t);
}

void pit_set_using_timer(int t, int using_timer)
{
//        pclog("pit_set_using_timer: t=%i using_timer=%i\n", t, using_timer);        
        timer_process();
        if (pit.using_timer[t] && !using_timer)
                pit.count[t] = pit_read_timer(t);
        if (!pit.using_timer[t] && using_timer)
                pit.c[t] = (int)((pit.count[t] << TIMER_SHIFT) * PITCONST);
        pit.using_timer[t] = using_timer;
        pit.running[t] = pit.enabled[t] && pit.using_timer[t] && !pit.disabled[t];
        timer_update_outstanding();
}

void pit_set_out_func(int t, void (*func)(int new_out, int old_out))
{
        pit_set_out_funcs[t] = func;
}

void pit_null_timer(int new_out, int old_out)
{
}

void pit_irq0_timer(int new_out, int old_out)
{
        if (new_out && !old_out)
                picint(1);
        if (!new_out)
                picintc(1);
}

void pit_irq0_timer_pcjr(int new_out, int old_out)
{
        if (new_out && !old_out)
        {
                picint(1);
                pit_clock(1);
        }
        if (!new_out)
                picintc(1);
}

void pit_refresh_timer_xt(int new_out, int old_out)
{
        if (new_out && !old_out)
                dma_channel_read(0);
}

void pit_refresh_timer_at(int new_out, int old_out)
{
        if (new_out && !old_out)
                ppi.pb ^= 0x10;
}

void pit_speaker_timer(int new_out, int old_out)
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


void pit_init()
{
        io_sethandler(0x0040, 0x0004, pit_read, NULL, NULL, pit_write, NULL, NULL, NULL);
        pit.gate[0] = pit.gate[1] = 1;
        pit.gate[2] = 0;
        pit.using_timer[0] = pit.using_timer[1] = pit.using_timer[2] = 1;

        timer_add(pit_timer_over, &pit.c[0], &pit.running[0], (void *)0);
        timer_add(pit_timer_over, &pit.c[1], &pit.running[1], (void *)1);
        timer_add(pit_timer_over, &pit.c[2], &pit.running[2], (void *)2);
                
        pit_set_out_func(0, pit_irq0_timer);
        pit_set_out_func(1, pit_null_timer);
        pit_set_out_func(2, pit_speaker_timer);
}
