/*
 * 86Box    A hypervisor and IBM PC system emulator.
 *
 *          Empty variant of the AWE32Emu CPU-context probe.
 *
 *          snd_emu8k.c is shared byte for byte with AWE32Emu and DOSBox-X AWE32.
 *          In the AWE32Emu measuring build of 86Box, awe32_trace.c annotates the
 *          EMU8000 port accesses with the CPU state of the driver (a research
 *          tool that hooks the CPU core). This build does not include it; the
 *          call is an empty function the compiler drops.
 */
#ifndef AWE32_TRACE_H
#define AWE32_TRACE_H

#include <stdint.h>

static inline void
awe32_trace_ioctx(const char *dir, uint16_t addr, uint16_t val)
{
    (void) dir;
    (void) addr;
    (void) val;
}

#endif /* AWE32_TRACE_H */
