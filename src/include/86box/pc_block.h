/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          The length of pc_run()'s blocks of emulated time.
 *
 *          A block is a millisecond (or ten) of the CPU's cycles. Clocks
 *          that aren't a multiple of the block rate leave a remainder
 *          (66666666 Hz: 66666.666 cycles a millisecond), which carries
 *          into the next block, so blocks average exactly the right length
 *          instead of running short.
 */
#ifndef EMU_PC_BLOCK_H
#define EMU_PC_BLOCK_H

#include <stdint.h>

typedef struct pc_block_t {
    uint32_t remainder; /* cycles left over from blocks so far */
    uint32_t per_s;     /* blocks per second they were counted for */
} pc_block_t;

/* Start afresh, as at power-on: after a hard reset, blocks fall exactly as
   after a fresh start. */
static inline void
pc_block_reset(pc_block_t *b)
{
    b->remainder = 0;
}

/* The cycles to run in the next block, for a CPU of `speed` Hz and `per_s`
   blocks per second. */
static inline int32_t
pc_block_cycles(pc_block_t *b, uint32_t speed, uint32_t per_s)
{
    int32_t n = (int32_t) (speed / per_s);

    if (per_s != b->per_s) {
        /* The block length changed: start the carry afresh. */
        b->per_s     = per_s;
        b->remainder = 0;
    }
    b->remainder += speed % per_s;
    if (b->remainder >= per_s) {
        b->remainder -= per_s;
        n++;
    }
    return n;
}

#endif /*EMU_PC_BLOCK_H*/
