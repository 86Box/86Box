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
   returns past it; the loop clears it before every block.

   The loop takes no event after a block unless one of three words says an
   event may be due: init and ne, cpu_state.abrt and smi_line, and nmi
   with irq. nmi is the NMI request, debugger_attached the memory viewer.
   irq is set while the 8259 or local APIC 0 may hold an interrupt: every
   writer that can raise one stores it (release), and only the loop clears
   it, once all those sources read empty. irq_pad is never written: it is
   read as part of the word that holds nmi and irq.

   stpclk and throttle_run are the chipset's clock control (cpu.h): the
   loop reads them as a fourth word, so that one test after a block covers
   every input of the work it does there. */
typedef struct cpu_events_t {
    int32_t          was_reset;
    int32_t          flush_pending;
    int32_t          init;
    int32_t          ne;
    int32_t          force_interpreter;
    int32_t          override_dynarec;
    int32_t          dr_armed;
    int32_t          halted;
    int32_t          nmi_req;
    uint8_t          irq;
    uint8_t          irq_pad[3];
    volatile int32_t debugger;
    int32_t          pad;
    int32_t          stpclk;
    int32_t          throttle_run;
} cpu_events_t;

#ifdef __cplusplus
extern "C" {
#endif
extern cpu_events_t cpu_events;
#ifdef __cplusplus
}
#endif

#define x86_was_reset         cpu_events.was_reset
#define cpu_flush_pending     cpu_events.flush_pending
#define cpu_init              cpu_events.init
#define new_ne                cpu_events.ne
#define cpu_force_interpreter cpu_events.force_interpreter
#define cpu_override_dynarec  cpu_events.override_dynarec
#define cpu_dr_armed          cpu_events.dr_armed
#define cpu_halted            cpu_events.halted
#define nmi                   cpu_events.nmi_req
#define debugger_attached     cpu_events.debugger
#define cpu_stpclk            cpu_events.stpclk
#define cpu_throttle_run      cpu_events.throttle_run

/* cpu_override_dynarec holds a bit per writer, so that one clearing its own
   does not end another's: the board's (the PS/2 cache disable, the PIIX4
   SMBus transfer) and the 486's first fetch after RESET while its line is
   read past the A20 gate (mem_a20_reset_line). */
#define CPU_OVERRIDE_BOARD      1
#define CPU_OVERRIDE_RESET_LINE 2

#endif /*EMU_CPU_EVENTS_H*/
