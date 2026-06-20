#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/pci.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/dma.h>
#include <86box/device.h>
#include <86box/thread.h>
#include <86box/network.h>

uint16_t
l80225_mii_readw(uint16_t *regs, uint16_t addr)
{
    switch (addr) {
        case 0x1:
            return 0x782D;
        case 0x2:
            return 0b10110;
        case 0x3:
            return 0xF830;
        case 0x5:
            return 0x41E1;
        case 0x18:
            return 0xC0;
        default:
            return regs[addr];
    }
    return 0;
}

void
l80225_mii_writew(uint16_t *regs, uint16_t addr, uint16_t val)
{
    regs[addr] = val;
}
