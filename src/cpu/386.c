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
#include "x86.h"
#include "x87.h"
#include "../nmi.h"
#include "../mem.h"
#include "../pic.h"
#include "../pit.h"
#include "../timer.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"
#include "386_common.h"


#define CPU_BLOCK_END()

extern int codegen_flags_changed;

extern int nmi_enable;

int inscounts[256];
uint32_t oldpc2;

int trap;

uint16_t flags,eflags;
uint32_t oldds,oldss,olddslimit,oldsslimit,olddslimitw,oldsslimitw;

x86seg gdt,ldt,idt,tr;
x86seg _cs,_ds,_es,_ss,_fs,_gs;
x86seg _oldds;



extern int cpl_override;

extern int fpucount;
uint16_t rds;
uint16_t ea_rseg;

int cgate32;

uint32_t cr2, cr3, cr4;
uint32_t dr[8];

uint32_t rmdat32;
#define rmdat rmdat32
#define fetchdat rmdat32
uint32_t backupregs[16];
extern int oddeven;
int inttype;


uint32_t oldcs2;
uint32_t oldecx;

uint32_t *eal_r, *eal_w;

uint16_t *mod1add[2][8];
uint32_t *mod1seg[8];


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
#define NOTRM   if (!(msw & 1) || (eflags & VM_FLAG))\
                { \
                        x86_int(6); \
                        return 0; \
                }

#define OP_TABLE(name) ops_ ## name

#define CLOCK_CYCLES(c) cycles -= (c)
#define CLOCK_CYCLES_ALWAYS(c) cycles -= (c)

#include "x86_ops.h"

#undef NOTRM
#define NOTRM   if (!(msw & 1) || (eflags & VM_FLAG))\
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
        uint8_t temp;
        uint32_t addr;
        int tempi;
        int cycdiff;
        int oldcyc;

        cycles+=cycs;
        /* output=3; */
        while (cycles>0)
        {
                int cycle_period = (timer_count >> TIMER_SHIFT) + 1;
                
		x86_was_reset = 0;
                cycdiff=0;
                oldcyc=cycles;
                timer_start_period(cycles << TIMER_SHIFT);
                while (cycdiff < cycle_period)
                {
            /*            testr[0]=EAX; testr[1]=EBX; testr[2]=ECX; testr[3]=EDX;
                        testr[4]=ESI; testr[5]=EDI; testr[6]=EBP; testr[7]=ESP;*/
/*                        testr[8]=flags;*/
                /* oldcs2=oldcs; */
                /* oldpc2=oldpc; */
                oldcs=CS;
                cpu_state.oldpc = cpu_state.pc;
                oldcpl=CPL;
                cpu_state.op32 = use32;

		x86_was_reset = 0;
                
dontprint=0;

                cpu_state.ea_seg = &_ds;
                cpu_state.ssegs = 0;
                
                fetchdat = fastreadl(cs + cpu_state.pc);

                if (!cpu_state.abrt)
                {               
                        trap = flags & T_FLAG;
                        opcode = fetchdat & 0xFF;
                        fetchdat >>= 8;

                        cpu_state.pc++;
                        x86_opcodes[(opcode | cpu_state.op32) & 0x3ff](fetchdat);
			if (x86_was_reset)
				break;
			if(x86_was_reset) break;
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
                                writememw(ss,(SP-2)&0xFFFF,flags);
                                writememw(ss,(SP-4)&0xFFFF,CS);
                                writememw(ss,(SP-6)&0xFFFF,cpu_state.pc);
                                SP-=6;
                                addr = (1 << 2) + idt.base;
                                flags&=~I_FLAG;
                                flags&=~T_FLAG;
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
                else if ((flags&I_FLAG) && pic_intpending)
                {
                        temp=picinterrupt();
                        if (temp!=0xFF)
                        {
                                flags_rebuild();
                                if (msw&1)
                                {
                                        pmodeint(temp,0);
                                }
                                else
                                {
                                        writememw(ss,(SP-2)&0xFFFF,flags);
                                        writememw(ss,(SP-4)&0xFFFF,CS);
                                        writememw(ss,(SP-6)&0xFFFF,cpu_state.pc);
                                        SP-=6;
                                        addr = (temp << 2) + idt.base;
                                        flags&=~I_FLAG;
                                        flags&=~T_FLAG;
                                        oxpc=cpu_state.pc;
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
                
                tsc += cycdiff;
                
                timer_end_period(cycles << TIMER_SHIFT);
        }
}
