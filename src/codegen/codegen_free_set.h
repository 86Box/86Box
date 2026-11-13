#ifndef _CODEGEN_FREE_SET_H_
#define _CODEGEN_FREE_SET_H_

#include <stdint.h>

/*A set of free block numbers: one bit per block, and one summary bit per
  64-bit word that has any bit set, so the lowest free block is found with two
  bit scans after skipping empty summary words. bits must have room for
  (nr + 63) / 64 words and summary for (words + 63) / 64.*/

static inline void
codegen_free_set_add(uint64_t *bits, uint64_t *summary, uint32_t *count, uint32_t c)
{
    bits[c >> 6] |= 1ull << (c & 63);
    summary[c >> 12] |= 1ull << ((c >> 6) & 63);
    (*count)++;
}

/*Lowest free block, taken off the set. Only called while *count is not 0.*/
static inline uint32_t
codegen_free_set_take_lowest(uint64_t *bits, uint64_t *summary, uint32_t *count)
{
    uint32_t s = 0;

    while (!summary[s])
        s++;

    const uint32_t w = (s << 6) | __builtin_ctzll(summary[s]);
    const uint32_t c = (w << 6) | __builtin_ctzll(bits[w]);

    bits[w] &= bits[w] - 1;
    if (!bits[w])
        summary[s] &= ~(1ull << (w & 63));
    (*count)--;
    return c;
}

#endif
