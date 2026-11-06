/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          The flags the CPU loop tests between two blocks.
 */
#ifndef EMU_CPU_EVENTS_H
#define EMU_CPU_EVENTS_H

#include <stdint.h>

/* The flags the CPU loop tests between two blocks, in one place so that a
   block-to-block transfer reads them as two words. Each keeps its old name
   through a macro, so a writer anywhere lands here without change. The
   loop reads: x86_was_reset (resetx86), cpu_flush_pending (a CR0 paging
   toggle), cpu_init (a reset asked for), new_ne (a new numeric exception),
   cpu_force_interpreter and cpu_override_dynarec (the settings), and
   cpu_dr_armed (an enabled breakpoint in DR7, kept by cpu_dr7_update(),
   which must follow every write to DR7). cpu_halted says the processor
   stopped in HLT, its PC on the HLT (opHLT): an interrupt taken from there
   returns past it; the loop clears it before every block. */
typedef struct cpu_events_t {
    int32_t was_reset;
    int32_t flush_pending;
    int32_t init;
    int32_t ne;
    int32_t force_interpreter;
    int32_t override_dynarec;
    int32_t dr_armed;
    int32_t halted;
} cpu_events_t;

extern cpu_events_t cpu_events;

#define x86_was_reset         cpu_events.was_reset
#define cpu_flush_pending     cpu_events.flush_pending
#define cpu_init              cpu_events.init
#define new_ne                cpu_events.ne
#define cpu_force_interpreter cpu_events.force_interpreter
#define cpu_override_dynarec  cpu_events.override_dynarec
#define cpu_dr_armed          cpu_events.dr_armed
#define cpu_halted            cpu_events.halted

#endif /*EMU_CPU_EVENTS_H*/
