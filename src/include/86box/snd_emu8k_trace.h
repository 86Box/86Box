/*
 * 86Box    A hypervisor and IBM PC system emulator.
 *
 *          EMU8000 port-write trace.
 *
 *          NOT PART OF UPSTREAM 86Box. Added locally for the AWE32Emu project
 *          so the register writes produced by the real DOS driver can be
 *          compared against the ones our own player produces. See
 *          AWE32Emu/ref86box/README.md.
 *
 *          Inactive unless the EMU8K_TRACE environment variable names an
 *          output file.
 */
#ifndef SOUND_EMU8K_TRACE_H
#define SOUND_EMU8K_TRACE_H

#include <stdint.h>

/* Called from emu8k_update() with the number of samples just generated;
   keeps the trace on the chip's own 44100 Hz timebase. */
extern void emu8k_trace_advance(int samples);

/* Called from emu8k_outw() after emu8k_update(), so the frame counter is
   already caught up to the moment of the write. */
extern void emu8k_trace_write(uint16_t addr, uint16_t val);

/* Called from emu8k_inw() with the value being returned. Reads are logged
   with a leading "R" so tools that only care about writes skip them. */
extern void emu8k_trace_read(uint16_t addr, uint16_t val);


/* Called from the end of emu8k_update() with the stereo block just produced.
   Writes it to the file named by AWE32_WAV, if that variable is set. */
extern void emu8k_trace_wav(const int32_t *buf, int samples);

#endif /*SOUND_EMU8K_TRACE_H*/
