// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VL82C486_CACHE_H
#define VL82C486_CACHE_H

/* VL82C486 data manual (June 1992), pp. 20, 27-28. KEN# controls line
   allocation; changing these registers must not discard resident CPU lines. */
static inline int
vl82c486_cache_flags(const uint8_t *regs, uint32_t addr, int cacheable)
{
    int protect = 0;
    int deny    = 0;

    if (addr >= 0x08000000)
        return 0;
    if (addr >= 0xa0000 && addr < 0x100000) {
        const uint8_t access = (regs[0x13 + ((addr - 0xa0000) >> 16)] >> (((addr >> 14) & 3) * 2)) & 3;
        cacheable            = !(access & 1);
        deny                 = access & 1;
        protect              = (access & 2) ? CPU_CACHE_WRITE_PROTECT : 0;
    }
    for (int i = 0x20; i <= 0x22; i += 2) {
        const uint8_t attr = regs[i];
        uint32_t      base;
        unsigned      shift;

        if (!(attr & 0x20)) {
            base  = (attr & 0x1f) << 19;
            shift = 16;
        } else if (!(attr & 0x10)) {
            base  = (attr & 0x0f) << 23;
            shift = 20;
        } else {
            base  = 0xc0000 | ((attr & 0x0f) << 14);
            shift = 11;
        }
        if (addr >= base && ((addr - base) >> shift) < 8 && (regs[i + 1] & (1 << ((addr - base) >> shift)))) {
            cacheable = 1;
            deny |= attr & 0x40;
        }
    }
    if (cacheable && !deny && (regs[0x07] & 8))
        return CPU_CACHE_FILL | protect;
    return protect;
}

#endif
