/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed based on the
 *          IBM PC architecture.
 *
 *          This file is part of the 86Box distribution.
 */
#ifndef EMU_AWARD_LH5_H
#define EMU_AWARD_LH5_H
#include <stddef.h>
#include <stdint.h>
int award_lh5_decode(const uint8_t *src, size_t src_len, uint8_t *dest, size_t dest_len);
#endif
