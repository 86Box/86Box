#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <math.h>
#ifndef INFINITY
# define INFINITY   (__builtin_inff())
#endif
#define HAVE_STDARG_H
#include "../86box.h"
#include "cpu.h"
#include "../timer.h"
#include "x86.h"
#include "x87.h"
#include "../nmi.h"
#include "../mem.h"
#include "../pic.h"
#include "../pit.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"
#include "386_common.h"


#define CPU_BLOCK_END()

extern int codegen_flags_changed;

int cpl_override = 0, fpucount = 0;
int tempc, oldcpl, optype, inttype, oddeven = 0;
int stack32, timetolive;

uint16_t oldcs;

uint32_t use32;
uint32_t oldds, oldss, olddslimit, oldsslimit,
	 olddslimitw, oldsslimitw;
uint32_t *eal_r, *eal_w;
uint32_t oxpc, cr2, cr3, cr4;
uint32_t dr[8];
uint32_t rmdat32;
uint32_t backupregs[16];

x86seg gdt,ldt,idt,tr;
x86seg _oldds;

uint32_t rmdat;

#define fetch_ea_16(rmdat)              cpu_state.pc++; cpu_mod=(rmdat >> 6) & 3; cpu_reg=(rmdat >> 3) & 7; cpu_rm = rmdat & 7; if (cpu_mod != 3) { fetch_ea_16_long(rmdat); if (cpu_state.abrt) return 0; } 
#define fetch_ea_32(rmdat)              cpu_state.pc++; cpu_mod=(rmdat >> 6) & 3; cpu_reg=(rmdat >> 3) & 7; cpu_rm = rmdat & 7; if (cpu_mod != 3) { fetch_ea_32_long(rmdat); } if (cpu_state.abrt) return 0


#include "x86_flags.h"

#define getbytef() ((uint8_t)(fetchdat)); cpu_state.pc++
#define getwordf() ((uint16_t)(fetchdat)); cpu_state.pc+=2
#define getbyte2f() ((uint8_t)(fetchdat>>8)); cpu_state.pc++
#define getword2f() ((uint16_t)(fetchdat>>8)); cpu_state.pc+=2
extern int xout;

int oldi;

uint32_t testr[9];
extern int dontprint;

#undef NOTRM
#define NOTRM   if (!(msw & 1) || (cpu_state.eflags & VM_FLAG))\
                { \
                        x86_int(6); \
                        return 0; \
                }

#define OP_TABLE(name) ops_ ## name

#define CLOCK_CYCLES(c) do { cycles -= (c);	\
			if (TIMER_VAL_LESS_THAN_VAL(timer_target, (uint32_t)tsc))	\
				timer_process(); } while(0)
#define CLOCK_CYCLES_ALWAYS(c) do { cycles -= (c);	\
			if (TIMER_VAL_LESS_THAN_VAL(timer_target, (uint32_t)tsc))	\
				timer_process(); } while(0)

#include "x86_ops.h"

#undef NOTRM
#define NOTRM   if (!(msw & 1) || (cpu_state.eflags & VM_FLAG))\
                { \
                        x86_int(6); \
                        break; \
                }


#ifdef ENABLE_386_LOG
int x386_do_log = ENABLE_386_LOG;


static void
x386_log(const char *fmt, ...)
{
    va_list ap;

    if (x386_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define x386_log(fmt, ...)
#endif


void exec386(int cycs)
{
	int vector, tempi, cycdiff, oldcyc;
	int ins_cycles;
        uint32_t addr;

        cycles+=cycs;
        while (cycles>0)
        {
                int cycle_period = (timer_target - (uint32_t)tsc) + 1;
                
		x86_was_reset = 0;
                cycdiff=0;
                oldcyc=cycles;
                while (cycdiff < cycle_period)
                {
		ins_cycles = cycles;

                oldcs=CS;
                cpu_state.oldpc = cpu_state.pc;
				oldcpl=CPL;
                cpu_state.op32 = use32;

		x86_was_reset = 0;
                
dontprint=0;

                cpu_state.ea_seg = &cpu_state.seg_ds;
                cpu_state.ssegs = 0;
                
                fetchdat = fastreadl(cs + cpu_state.pc);

                if (!cpu_state.abrt)
                {         
                        opcode = fetchdat & 0xFF;
                        fetchdat >>= 8;			
                        trap = cpu_state.flags & T_FLAG;

                        cpu_state.pc++;
                        x86_opcodes[(opcode | cpu_state.op32) & 0x3ff](fetchdat);
						if(x86_was_reset) 
							break;
                }

                if (!use32) cpu_state.pc &= 0xffff;

                if (cpu_state.abrt)
                {
                        flags_rebuild();
                        tempi = cpu_state.abrt;
                        cpu_state.abrt = 0;
                        x86_doabrt(tempi);
                        if (cpu_state.abrt)
                        {
                                cpu_state.abrt = 0;
                                CS = oldcs;
                                cpu_state.pc = cpu_state.oldpc;
                                x386_log("Double fault %i\n", ins);
                                pmodeint(8, 0);
                                if (cpu_state.abrt)
                                {
                                        cpu_state.abrt = 0;
                                        softresetx86();
					cpu_set_edx();
                                        x386_log("Triple fault - reset\n");
                                }
                        }
                }

		ins_cycles -= cycles;
		tsc += ins_cycles;

                cycdiff=oldcyc-cycles;

                if (trap)
                {
                        flags_rebuild();
                        /* oldpc=pc; */
                        /* oldcs=CS; */
                        if (msw&1)
                        {
                                pmodeint(1,0);
                        }
                        else
                        {
                                writememw(ss,(SP-2)&0xFFFF,cpu_state.flags);
                                writememw(ss,(SP-4)&0xFFFF,CS);
                                writememw(ss,(SP-6)&0xFFFF,cpu_state.pc);
                                SP-=6;
                                addr = (1 << 2) + idt.base;
                                cpu_state.flags&=~I_FLAG;
                                cpu_state.flags&=~T_FLAG;
                                cpu_state.pc=readmemw(0,addr);
                                loadcs(readmemw(0,addr+2));
                        }
                }
                else if (nmi && nmi_enable)
                {
                        cpu_state.oldpc = cpu_state.pc;
                        oldcs = CS;
                        x86_int(2);
                        nmi_enable = 0;
                        if (nmi_auto_clear)
                        {
                                nmi_auto_clear = 0;
                                nmi = 0;
                        }
                }
                else if ((cpu_state.flags & I_FLAG) && pic_intpending)
                {
                        vector = picinterrupt();
                        if (vector != -1)
                        {
                                flags_rebuild();
                                if (msw&1)
                                {
                                        pmodeint(vector,0);
                                }
                                else
                                {
                                        writememw(ss,(SP-2)&0xFFFF,cpu_state.flags);
                                        writememw(ss,(SP-4)&0xFFFF,CS);
                                        writememw(ss,(SP-6)&0xFFFF,cpu_state.pc);
                                        SP-=6;
                                        addr = (vector << 2) + idt.base;
                                        cpu_state.flags&=~I_FLAG;
                                        cpu_state.flags&=~T_FLAG;
										oxpc = cpu_state.pc;
                                        cpu_state.pc=readmemw(0,addr);
                                        loadcs(readmemw(0,addr+2));
                                }
                        }
                }

                ins++;

                if (timetolive)
                {
                        timetolive--;
                        if (!timetolive)
                                fatal("Life expired\n");
                }
                }
                
		if (TIMER_VAL_LESS_THAN_VAL(timer_target, (uint32_t)tsc))
			timer_process();
        }
}
