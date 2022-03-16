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
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include "x86.h"
#include "x87.h"
#include <86box/nmi.h>
#include <86box/mem.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/machine.h>
#include <86box/gdbstub.h>
#include "386_common.h"
#ifdef USE_NEW_DYNAREC
#include "codegen.h"
#endif


#undef CPU_BLOCK_END
#define CPU_BLOCK_END()


extern int codegen_flags_changed;

int tempc, oldcpl, optype, inttype, oddeven = 0;
int timetolive;

uint16_t oldcs;

uint32_t oldds, oldss, olddslimit, oldsslimit,
	 olddslimitw, oldsslimitw;
uint32_t oxpc;
uint32_t rmdat32;
uint32_t backupregs[16];

x86seg _oldds;


#ifdef ENABLE_386_LOG
int x386_do_log = ENABLE_386_LOG;


void
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


#undef CPU_BLOCK_END
#define CPU_BLOCK_END()

#include "x86_flags.h"

#define getbytef() ((uint8_t)(fetchdat)); cpu_state.pc++
#define getwordf() ((uint16_t)(fetchdat)); cpu_state.pc+=2
#define getbyte2f() ((uint8_t)(fetchdat>>8)); cpu_state.pc++
#define getword2f() ((uint16_t)(fetchdat>>8)); cpu_state.pc+=2


#define OP_TABLE(name) ops_ ## name

#if 0
#define CLOCK_CYCLES(c) \
	{\
		if (fpu_cycles > 0) {\
			fpu_cycles -= (c);\
			if (fpu_cycles < 0) {\
				cycles += fpu_cycles;\
			}\
		} else {\
			cycles -= (c);\
		}\
	}

#define CLOCK_CYCLES_FPU(c) cycles -= (c)
#define CONCURRENCY_CYCLES(c) fpu_cycles = (c)
#else
#define CLOCK_CYCLES(c) cycles -= (c)
#define CLOCK_CYCLES_FPU(c) cycles -= (c)
#define CONCURRENCY_CYCLES(c)
#endif

#define CLOCK_CYCLES_ALWAYS(c) cycles -= (c)

#include "x86_ops.h"


void
exec386(int cycs)
{
    int vector, tempi, cycdiff, oldcyc;
    int cycle_period, ins_cycles;
    uint32_t addr;

    cycles += cycs;

    while (cycles > 0) {
	cycle_period = (timer_target - (uint32_t)tsc) + 1;

	x86_was_reset = 0;
	cycdiff = 0;
	oldcyc = cycles;
	while (cycdiff < cycle_period) {
		ins_cycles = cycles;

#ifndef USE_NEW_DYNAREC
                oldcs=CS;
		oldcpl=CPL;
#endif
		cpu_state.oldpc = cpu_state.pc;
		cpu_state.op32 = use32;

#ifndef USE_NEW_DYNAREC
		x86_was_reset = 0;
#endif

		cpu_state.ea_seg = &cpu_state.seg_ds;
		cpu_state.ssegs = 0;

		fetchdat = fastreadl(cs + cpu_state.pc);

		if (!cpu_state.abrt) {
#ifdef ENABLE_386_LOG
			if (in_smm)
				x386_log("[%04X:%08X] %08X\n", CS, cpu_state.pc, fetchdat);
#endif
			opcode = fetchdat & 0xFF;
			fetchdat >>= 8;
			trap = cpu_state.flags & T_FLAG;

			cpu_state.pc++;
			x86_opcodes[(opcode | cpu_state.op32) & 0x3ff](fetchdat);
			if (x86_was_reset)
				break;
		}
#ifdef ENABLE_386_LOG
		else if (in_smm)
			x386_log("[%04X:%08X] ABRT\n", CS, cpu_state.pc);
#endif

#ifndef USE_NEW_DYNAREC
		if (!use32) cpu_state.pc &= 0xffff;
#endif

		if (cpu_end_block_after_ins)
			cpu_end_block_after_ins--;

		if (cpu_state.abrt) {
			flags_rebuild();
			tempi = cpu_state.abrt & ABRT_MASK;
			cpu_state.abrt = 0;
			x86_doabrt(tempi);
			if (cpu_state.abrt) {
				cpu_state.abrt = 0;
#ifndef USE_NEW_DYNAREC
				CS = oldcs;
#endif
				cpu_state.pc = cpu_state.oldpc;
				x386_log("Double fault\n");
				pmodeint(8, 0);
				if (cpu_state.abrt) {
					cpu_state.abrt = 0;
					softresetx86();
					cpu_set_edx();
#ifdef ENABLE_386_LOG
					x386_log("Triple fault - reset\n");
#endif
				}
			}
		}

		if (smi_line)
			enter_smm_check(0);
		else if (trap) {
			flags_rebuild();
			if (msw&1)
				pmodeint(1,0);
			else {
				writememw(ss, (SP - 2) & 0xFFFF, cpu_state.flags);
				writememw(ss, (SP - 4) & 0xFFFF, CS);
				writememw(ss, (SP - 6) & 0xFFFF, cpu_state.pc);
				SP -= 6;
				addr = (1 << 2) + idt.base;
				cpu_state.flags &= ~I_FLAG;
				cpu_state.flags &= ~T_FLAG;
				cpu_state.pc = readmemw(0, addr);
				loadcs(readmemw(0, addr + 2));
			}
		} else if (nmi && nmi_enable && nmi_mask) {
			if (is486 && (cpu_fast_off_flags & 0x20000000))
				cpu_fast_off_count = cpu_fast_off_val + 1;

			cpu_state.oldpc = cpu_state.pc;
			x86_int(2);
			nmi_enable = 0;
#ifdef OLD_NMI_BEHAVIOR
			if (nmi_auto_clear) {
				nmi_auto_clear = 0;
				nmi = 0;
			}
#else
			nmi = 0;
#endif
		} else if ((cpu_state.flags & I_FLAG) && pic.int_pending && !cpu_end_block_after_ins) {
			vector = picinterrupt();
			if (vector != -1) {
				flags_rebuild();
				if (msw & 1)
					pmodeint(vector, 0);
				else {
					writememw(ss, (SP - 2) & 0xFFFF, cpu_state.flags);
					writememw(ss, (SP - 4) & 0xFFFF, CS);
					writememw(ss, (SP - 6) & 0xFFFF, cpu_state.pc);
					SP -= 6;
					addr = (vector << 2) + idt.base;
					cpu_state.flags &= ~I_FLAG;
					cpu_state.flags &= ~T_FLAG;
					cpu_state.pc = readmemw(0, addr);
					loadcs(readmemw(0, addr + 2));
				}
			}
		}

		ins_cycles -= cycles;
		tsc += ins_cycles;

		cycdiff = oldcyc - cycles;

		if (timetolive) {
			timetolive--;
			if (!timetolive)
				fatal("Life expired\n");
		}

		if (TIMER_VAL_LESS_THAN_VAL(timer_target, (uint32_t) tsc))
			timer_process_inline();

#ifdef USE_GDBSTUB
		if (gdbstub_instruction())
			return;
#endif
	}
    }
}
