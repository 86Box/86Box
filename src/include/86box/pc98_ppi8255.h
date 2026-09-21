/*
 * Intel 8255A PPI core used by the experimental PC-98 motherboard.
 *
 * Clean implementation for 86Box.  The callback-oriented shape follows the
 * author's earlier neetburst/c8255 experiments, but this file is a fresh
 * implementation suitable for 86Box's GPL-2.0-or-later tree.
 */
#ifndef EMU_86BOX_PC98_PPI8255_H
#define EMU_86BOX_PC98_PPI8255_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t (*pc98_ppi_in_cb)(void *opaque, unsigned port);
typedef void (*pc98_ppi_out_cb)(void *opaque, unsigned port, uint8_t value);
typedef void (*pc98_ppi_intr_cb)(void *opaque, unsigned group, int level);

typedef struct pc98_ppi8255_t {
    void *opaque;
    pc98_ppi_in_cb in_cb;
    pc98_ppi_out_cb out_cb;
    pc98_ppi_intr_cb intr_cb;

    uint8_t latch[3];
    uint8_t input[3];
    uint8_t control;

    uint8_t mode_a;
    uint8_t mode_b;
    uint8_t dir_a;
    uint8_t dir_b;
    uint8_t dir_c_upper;
    uint8_t dir_c_lower;

    /* Mode 1/2 handshake state. */
    uint8_t ibf_a, obf_a, intr_a, inte_a_in, inte_a_out;
    uint8_t ibf_b, obf_b, intr_b, inte_b;
    uint8_t stb_a, ack_a, stb_b, ack_b;
} pc98_ppi8255_t;

/* Apply an 8255 bit set/reset command to a board-level latch that is wired to
 * port C pins but lives outside the chip.  On PC-9801 286 machines SHUT0 (PC7)
 * and SHUT1 (PC5) work exactly this way: BSR writes to 37h drive them, they
 * are read back through port C at 35h, and neither a mode-set command nor an
 * 80286 software reset clears them.  `mask` selects the pins that are latched
 * externally; everything else is left to the chip. */
static inline uint8_t pc98_ppi8255_bsr_latch(uint8_t latch, uint8_t bsr,
                                             uint8_t mask)
{
    uint8_t bit;

    if (bsr & 0x80u) /* mode-set command, not a BSR command */
        return latch;

    bit = (uint8_t)(1u << ((bsr >> 1) & 7u));
    if (!(bit & mask))
        return latch;

    return (uint8_t)((bsr & 1u) ? (latch | bit) : (latch & (uint8_t)~bit));
}

void pc98_ppi8255_init(pc98_ppi8255_t *ppi, void *opaque,
                       pc98_ppi_in_cb in_cb, pc98_ppi_out_cb out_cb,
                       pc98_ppi_intr_cb intr_cb);
void pc98_ppi8255_reset(pc98_ppi8255_t *ppi);
uint8_t pc98_ppi8255_read(pc98_ppi8255_t *ppi, unsigned reg);
void pc98_ppi8255_write(pc98_ppi8255_t *ppi, unsigned reg, uint8_t value);
void pc98_ppi8255_set_input(pc98_ppi8255_t *ppi, unsigned port, uint8_t value);
void pc98_ppi8255_strobe_a(pc98_ppi8255_t *ppi, int level);
void pc98_ppi8255_ack_a(pc98_ppi8255_t *ppi, int level);
void pc98_ppi8255_strobe_b(pc98_ppi8255_t *ppi, int level);
void pc98_ppi8255_ack_b(pc98_ppi8255_t *ppi, int level);

#ifdef __cplusplus
}
#endif
#endif
