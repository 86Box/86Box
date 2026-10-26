/* Test the production register decoder independently of device initialization. */
#include <stdint.h>
#include <stdio.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include "../../src/chipset/vl82c486_cache.h"

int
test_vl82c486_cache_policy(uint32_t addr, const uint8_t *regs)
{
    return vl82c486_cache_flags(regs, addr, mem_addr_is_ram(addr));
}
