/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NVIDIA RIVA TNT / TNT2 -- PMC, the master control.
 *
 *          The chip id, the summary interrupt register gathered from the
 *          units, the interrupt enable that drives INTA, and the engine
 *          enables, whose clearing resets an engine.
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
#include <86box/nv/vid_nv4.h>

#define NV4_PMC_ENABLE_PMEDIA (1u << 4)
#define NV4_PMC_ENABLE_PFIFO  (1u << 8)
#define NV4_PMC_ENABLE_PGRAPH (1u << 12)
#define NV4_PMC_ENABLE_PPMI   (1u << 16)
#define NV4_PMC_ENABLE_PFB    (1u << 20)
#define NV4_PMC_ENABLE_PCRTC  (1u << 24)
#define NV4_PMC_ENABLE_PVIDEO (1u << 28)

/* PMC_BOOT_0: foundry TSMC (31:28 = 2), architecture 4 (15:12),
   implementation 0. The TNT2 is the same architecture with mask
   revision B (23:20 = 1); the resman's IsNV5() and nouveau's chipset
   probe both test that field. */
static uint32_t
nv4_pmc_boot_0(const nv4_t *nv4)
{
    uint32_t boot = 0x20004000 | (nv4->board->revision & 0x0f);

    if (nv4->chip == NV4_CHIP_TNT2)
        boot |= 0x00100000;
    return boot;
}

/* PMC_INTR_0: a unit's bit is set while that unit has an enabled
   interrupt pending. */
uint32_t
nv4_pmc_pending(nv4_t *nv4)
{
    uint32_t pending = nv4->pmc_intr & NV4_PMC_INTR_SOFTWARE;

    if (nv4_pme_pending(nv4))
        pending |= NV4_PMC_INTR_PMEDIA;
    if (nv4_pfifo_pending(nv4))
        pending |= NV4_PMC_INTR_PFIFO;
    if (nv4_pgraph_pending(nv4))
        pending |= NV4_PMC_INTR_PGRAPH;
    if (nv4_pvideo_pending(nv4))
        pending |= NV4_PMC_INTR_PVIDEO;
    if (nv4->ptimer_intr & nv4->ptimer_intr_en)
        pending |= NV4_PMC_INTR_PTIMER;
    if (nv4->pcrtc_intr & nv4->pcrtc_intr_en)
        pending |= NV4_PMC_INTR_PCRTC;
    if (nv4->pbus_intr & nv4->pbus_intr_en)
        pending |= NV4_PMC_INTR_PBUS;

    return pending;
}

void
nv4_pmc_reset(nv4_t *nv4)
{
    nv4->pmc_intr    = 0;
    nv4->pmc_intr_en = 0;
    nv4->pmc_enable  = NV4_PMC_ENABLE_PFB | NV4_PMC_ENABLE_PCRTC;
}

uint32_t
nv4_pmc_read(nv4_t *nv4, uint32_t addr)
{
    switch (addr) {
        case NV4_PMC_BOOT_0:
            return nv4_pmc_boot_0(nv4);
        case NV4_PMC_INTR_0:
            return nv4_pmc_pending(nv4);
        case NV4_PMC_INTR_EN_0:
            return nv4->pmc_intr_en;
        case NV4_PMC_INTR_READ_0:
            return nv4->irq_state ? 1 : 0;
        case NV4_PMC_ENABLE:
            return nv4->pmc_enable;
        default:
            nv4_log("NV4 PMC: read %06x\n", addr);
            return 0;
    }
}

void
nv4_pmc_write(nv4_t *nv4, uint32_t addr, uint32_t val)
{
    uint32_t off;

    switch (addr) {
        case NV4_PMC_INTR_0:
            /* only the software interrupt is set and cleared here */
            nv4->pmc_intr = val & NV4_PMC_INTR_SOFTWARE;
            nv4_pmc_update_irq(nv4);
            break;
        case NV4_PMC_INTR_EN_0:
            nv4->pmc_intr_en = val & 0x03;
            nv4_pmc_update_irq(nv4);
            break;
        case NV4_PMC_ENABLE:
            /* An engine whose enable goes from 1 to 0 is held in reset;
               the video BIOS's init script resets the engines this way. */
            off = nv4->pmc_enable & ~val;
            nv4->pmc_enable = val & 0x11111110;
            if (off & NV4_PMC_ENABLE_PFIFO)
                nv4_pfifo_reset(nv4);
            if (off & NV4_PMC_ENABLE_PGRAPH)
                nv4_pgraph_reset(nv4);
            if (off & NV4_PMC_ENABLE_PVIDEO)
                nv4_pvideo_reset(nv4);
            if (off & NV4_PMC_ENABLE_PMEDIA)
                nv4_pme_reset(nv4);
            nv4_pmc_update_irq(nv4);
            break;
        default:
            nv4_log("NV4 PMC: write %06x = %08x\n", addr, val);
            break;
    }
}
