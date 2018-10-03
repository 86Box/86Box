extern "C"
{
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

#define CPU_BLOCK_END()

uint16_t flags,eflags;
uint32_t oldds,oldss,olddslimit,oldsslimit,olddslimitw,oldsslimitw;

x86seg gdt,ldt,idt,tr;
x86seg _cs,_ds,_es,_ss,_fs,_gs;
x86seg _oldds;

uint32_t cr2, cr3, cr4;
uint32_t dr[8];

#include "x86_flags.h"

#ifdef ENABLE_386_REF_LOG
int x386_ref_do_log = ENABLE_386_REF_LOG;
#endif


static void
x386_ref_log(const char *fmt, ...)
{
#ifdef ENABLE_386_REF_LOG
    va_list ap;

    if (x386_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
#endif
}


enum class translate_kind
{
    TRANSLATE_READ, TRANSLATE_WRITE, TRANSLATE_EXEC
};


enum class exception_type
{
    FAULT, TRAP, ABORT
};


struct cpu_exception
{
    exception_type type;
    uint8_t fault_type;
    uint32_t error_code;
    bool error_code_valid;
    cpu_exception(exception_type _type, uint8_t _fault_type, uint32_t errcode, bool errcodevalid)
    : type(_type)
    , fault_type(_fault_type)
    , error_code(errcode)
    , error_code_valid(errcodevalid) {}
};


void
type_check_ref(x86seg* segment, uint32_t offset, translate_kind kind)
{
    bool system_seg = !((segment->flags_ref >> 4) & 1);
    bool executable = (segment->flags_ref >> 3) & 1;

    if(!system_seg)
    {
	if(executable) {
		bool readable = (segment->flags_ref >> 1) & 1;
		switch(kind) {
			case TRANSLATE_READ:
				if (!readable)
					throw cpu_exception(FAULT, ABRT_GPF, 0, true);
				break;
			case TRANSLATE_WRITE:
				throw cpu_exception(FAULT, ABRT_GPF, 0, true);
				break;
			default:
				break;
		}
	} else {
		bool writeable = (segment->flags_ref >> 1) & 1;
		switch(kind) {
			case TRANSLATE_WRITE:
				if (!writeable)
					throw cpu_exception(FAULT, ABRT_GPF, 0, true);
				break;
			case TRANSLATE_EXEC:
				throw cpu_exception(FAULT, ABRT_GPF, 0, true);
				break;
			default:
				break;
		}
	}
    } else {
	// TODO
	x386_ref_log("type_check_ref called with a system-type segment! Execution correctness is not guaranteed past this point!\n");
    }
}


bool
limit_check_ref(x86seg* segment, uint32_t offset, translate_kind kind)
{
    uint8_t fault_type = ABRT_GPF;
    uint32_t addr = offset & ((1 << (32 - 12)) - 1);

    if (segment == &_ss)
	fault_type = ABRT_SS;

    switch(kind) {
	case translate_kind::READ:
	case translate_kind::WRITE:
		// Data segment.
		bool expand_down = (segment->flags_ref >> 2) & 1;
		bool big_seg = (segment->flags_ref >> 14) & 1;		// TODO: Not sure if this is ever used. Test this!
		bool granularity = (segment->flags_ref >> 15) & 1;
		uint32_t lower_bound;
		uint32_t upper_bound;
		if (big_seg != granularity)
			x386_ref_log("B bit doesn't equal granularity bit! Execution correctness is not guaranteed past this point!\n");
		if (expand_down) {
			if (granularity) {
				lower_bound = ((addr << 12) | 0xfff) + 1;
				upper_bound = 0xffffffff; //4G - 1
			} else {
				lower_bound = addr + 1;
				upper_bound = 0xffff; //64K - 1
			}
		} else {
			lower_bound = 0;
			if (granularity)
				upper_bound = (addr << 12) | 0xfff;
			else
				upper_bound = addr;
		}
		if ((addr < lower_bound) || (addr > upper_bound))
			throw cpu_exception(FAULT, fault_type, 0, true);
		break;

	default:
		bool granularity = (segment->flags_ref >> 15) & 1;
		uint32_t limit;

		if (granularity)
			limit = (addr << 12) | 0xfff;
		else
			limit = addr;

		if (addr > limit)
			throw cpu_exception(FAULT, fault_type, 0, true);
		break;
    }
}


void
privilege_check_ref(x86seg* segment, uint32_t offset, translate_kind kind)
{
    bool system_seg = !((segment->flags_ref >> 4) & 1);
    bool executable = (segment->flags_ref >> 3) & 1;

    if (!system_seg) {
	if(executable) {
		bool conforming = (segment->flags_ref >> 2) & 1;
		if (conforming)
			return;
		else {
			int seg_rpl = segment->seg & 3;
			int dpl = (segment->flags_ref >> 5) & 3;
			if (dpl < CPL)
				throw cpu_exception(FAULT, ABRT_GPF, 0, true);
			if (dpl < seg_rpl)
				throw cpu_exception(FAULT, ABRT_GPF, 0, true);
		}
	} else {
		int seg_rpl = segment->seg & 3;
		int dpl = (segment->flags_ref >> 5) & 3;
		if (dpl < CPL)
			throw cpu_exception(FAULT, ABRT_GPF, 0, true);
		if (dpl < seg_rpl)
			throw cpu_exception(FAULT, ABRT_GPF, 0, true);
	}
    } else {
	// TODO
	x386_ref_log("privilege_check_ref called with a system-type segment! Execution correctness is not guaranteed past this point!\n");
    }
}


#define rammap(x) ((uint32_t *)(_mem_exec[(x) >> 14]))[((x) >> 2) & 0xfff]


uint32_t
translate_addr_ref(x86seg* segment, uint32_t offset, translate_kind kind)
{
    // Segment-level checks.
    type_check_ref(segment, offset, kind);
    limit_check_ref(segment, offset, kind);
    privilege_check_ref(segment, offset, kind);

    uint32_t addr = segment->base + offset;

    if (!(cr0 >> 31))
	return addr;
}


void
readmemb_ref(x86seg* segment, uint32_t offset)
{
    uint32_t addr = translate_addr_ref(segment, offset, TRANSLATE_READ);
    return mem_readb_phys_dma(addr);
}


void
writememb_ref(x86seg* segment, uint32_t offset, uint8_t data)
{
    uint32_t addr = translate_addr_ref(segment, offset, TRANSLATE_READ);
    mem_writeb_phys_dma(addr, data);
}


void
exec386_ref(int cycs)
{
    uint8_t temp;
    uint32_t addr;
    int tempi;
    int cycdiff;
    int oldcyc;

    cycles+=cycs;

    while (cycles>0) {
	timer_start_period(cycles << TIMER_SHIFT);

	oldcs = CS;
	cpu_state.oldpc = cpu_state.pc;
	oldcpl = CPL;
	cpu_state.op32 = use32;

	x86_was_reset = 0;

	dontprint = 0;

	cpu_state.ea_seg = &_ds;
	cpu_state.ssegs = 0;

	try {
	} catch(cpu_exception) {
	}

	timer_end_period(cycles << TIMER_SHIFT);
    }
}
}