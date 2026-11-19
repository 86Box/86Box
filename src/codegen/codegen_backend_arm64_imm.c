#include <stdint.h>

/*ARM64 logical instructions have an 'interesting' immediate encoding: an
  element of 2, 4, 8, 16 or 32 bits holding one run of ones, rotated right
  by immr and repeated to fill the register (ARM DDI 0487 M.d, AND
  (immediate) p. C6-1830, DecodeBitMasks p. J1-16031). Returns N:immr:imms,
  or 0 when data has no such encoding. The processor ignores the bits of
  immr above the element size; they are set here*/
uint32_t
host_arm64_find_imm(uint32_t data)
{
    int      len = 5;
    uint32_t starts;
    uint32_t size;
    uint32_t pos;
    uint32_t ones;

    if (!data || (data == 0xffffffff))
        return 0;

    /*The smallest element data repeats*/
    while (len > 1) {
        const uint32_t half = 1u << (len - 1);
        const uint32_t mask = (1u << half) - 1;

        if ((data & mask) != ((data >> half) & mask))
            break;
        len--;
    }
    size = 1u << len;

    /*A bit set where a run of ones starts, one per element*/
    starts = data & ~((data << 1) | (data >> 31));
    if ((uint32_t) __builtin_popcount(starts) != (32u >> len))
        return 0;
    pos  = __builtin_ctz(starts);
    ones = (uint32_t) __builtin_popcount(data) >> (5 - len);

    return ((((size - pos) & (size - 1)) | (0x3f & ~(size - 1))) << 6) | (0x3f & ~((size << 1) - 1)) | (ones - 1);
}
