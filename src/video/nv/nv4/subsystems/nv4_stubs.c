/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NVIDIA RIVA TNT / TNT2 -- units not yet modeled.
 *
 *          PME as a register file: what is written reads back and
 *          nothing raises an interrupt. It moves to its own file when
 *          it is modeled.
 *
 * Authors: Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2026 Marcos Alves
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/plat_unused.h>
#include <86box/nv/vid_nv4.h>

/* PME */
void
nv4_pme_reset(nv4_t *nv4)
{
    memset(nv4->pme_regs, 0, sizeof(nv4->pme_regs));
}

uint32_t
nv4_pme_read(nv4_t *nv4, uint32_t addr)
{
    return nv4->pme_regs[((addr - NV4_PME_START) >> 2) & 0x3ff];
}

void
nv4_pme_write(nv4_t *nv4, uint32_t addr, uint32_t val)
{
    nv4->pme_regs[((addr - NV4_PME_START) >> 2) & 0x3ff] = val;
}

uint32_t
nv4_pme_pending(UNUSED(nv4_t *nv4))
{
    return 0;
}
