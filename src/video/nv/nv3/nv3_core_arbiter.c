/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          The insane NV3 MMIO arbiter.
 *          Writes to ALL sections of the GPU based on the write position
 *          All writes are internally considered to be 32-bit! Be careful...
 * 
 *          Also handles interrupt dispatch
 *
 *          
 *
 * Authors: Connor Hyde, <mario64crashed@gmail.com> I need a better email addr ;^)
 *          Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2024-2025 starfrost
 *          Copyright 2026 Marcos Alves
 */

// STANDARD NV3 includes
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h> // DEPENDENT!!!
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>

// Gets a register...
// move this somewhere else when we have more models
nv_register_t* nv_get_register(uint32_t addr, nv_register_t* register_list)
{
    uint32_t reg_num = 0;
    nv_register_t reg_current = register_list[0];

    // MAKE SURE THE NV_REGISTER LIST IS TERMINATED!!! OTHERWISE, VERY BAD THINGS WILL HAPPEN!!
    // num_regs was removed due to the move to a unified register file...
    while (reg_current.addr != NV_REG_LIST_END)
    {
        if (register_list[reg_num].addr == addr)
            return &register_list[reg_num];

        reg_num++;
        reg_current = register_list[reg_num];
    }

    return NULL;
}

/* Debug: the next N register accesses (not PTIMER's clock) go to the log ("dev nv3 mmio N") */
uint32_t nv3_mmio_trace_left;
uint32_t nv3_mmio_trace_lo, nv3_mmio_trace_hi = 0xFFFFFFFF;

static void nv3_mmio_trace(char rw, uint32_t addr, uint32_t val)
{
    if (!nv3_mmio_trace_left || addr == NV3_PTIMER_TIME_0_NSEC || addr == NV3_PTIMER_TIME_1_NSEC)
        return;
    if ((addr < nv3_mmio_trace_lo) || (addr > nv3_mmio_trace_hi))
        return;
    nv3_mmio_trace_left--;
    always_log("nv3: mmio %c %06x %08x\n", rw, addr, val);
}

// Arbitrates an MMIO read
uint32_t nv3_mmio_arbitrate_read(uint32_t addr)
{
    // sanity check
    if (!nv3)
        return 0x00; 

    uint32_t ret = 0x00;

    // Ensure the addresses are dword aligned.
    // I don't know why this is needed because writepriv32 is Nvidia's drivers is always dword aligned, but it crashes if you don't do this.
    // Exclude the 4bpp/8bpp CLUT for this purpose
    if (!(addr >= NV3_USER_DAC_PALETTE_START && addr <= NV3_USER_DAC_PALETTE_END))
        addr &= 0xFFFFFC;

    // gigantic set of if statements to send the write to the right subsystem
    if (addr <= NV3_PMC_END) /* PMC starts at 0 */
        ret = nv3_pmc_read(addr);
    else if (addr >= NV3_PBUS_START && addr <= NV3_PBUS_END)
        ret = nv3_pbus_read(addr);
    else if (addr >= NV3_PFIFO_START && addr <= NV3_PFIFO_END)
        ret = nv3_pfifo_read(addr);
    else if (addr >= NV3_PFB_START && addr <= NV3_PFB_END)
        ret = nv3_pfb_read(addr);
    else if (addr >= NV3_PTIMER_START && addr <= NV3_PTIMER_END)
        ret = nv3_ptimer_read(addr);
    else if (addr >= NV3_PFB_START && addr <= NV3_PFB_END)
        ret = nv3_pfb_read(addr);
    else if (addr >= NV3_PSTRAPS_START && addr <= NV3_PSTRAPS_END)
        ret = nv3_pstraps_read(addr);
    else if (addr >= NV3_PROM_START && addr <= NV3_PROM_END)
        ret = nv3_prom_read(addr);
    else if (addr >= NV3_PME_START && addr <= NV3_PME_END)
        ret = nv3_pme_read(addr);
    else if (addr >= NV3_PGRAPH_START && addr <= NV3_PGRAPH_REAL_END) // what we're actually doing here determined by nv3_pgraph_* func
        ret = nv3_pgraph_read(addr);
    else if (addr >= NV3_PVIDEO_START && addr <= NV3_PVIDEO_END)
        ret = nv3_pvideo_read(addr);
    else if ((addr >= NV3_PRAMDAC_START && addr <= NV3_PRAMDAC_END)
    || (addr >= NV3_USER_DAC_PALETTE_START && addr <= NV3_USER_DAC_PALETTE_END)) //clut
        ret = nv3_pramdac_read(addr);
    else if (addr >= NV3_VRAM_START && addr <= NV3_VRAM_END)
        ret = nv3_dfb_read32(addr & nv3->nvbase.svga.vram_mask, &nv3->nvbase.svga);
    else if (addr >= NV3_USER_START && addr <= NV3_USER_END)
        ret = nv3_user_read(addr);
    else 
    {
        nv_log("MMIO read arbitration failed, INVALID addr NOT mapped to any GPU subsystem 0x%08x [returning unmapped pattern]\n", addr);

        // The real hardware returns a garbage pattern
        ret = 0x00;
    }

    //#ifdef ENABLE_NV_LOG

    // Don't log some very very highly used registers.
    if (addr <= NV3_USER_START
    && addr != NV3_PTIMER_TIME_0_NSEC
    && addr != NV3_PTIMER_TIME_1_NSEC)
    {
        nv_register_t* reg = nv_get_register(addr, nv3_registers);

        if (reg)
        {
            if (reg->on_read)
                ret = reg->on_read();
            
            nv_log_verbose_only("Register read 0x%08x from 0x%08x (%s)\n", ret, addr, reg->friendly_name);
        }
        else
            nv_log_verbose_only("Unknown register read 0x%08x\n", addr);
    }

    nv3_mmio_trace('r', addr, ret);
    return ret;
}

void nv3_mmio_arbitrate_write(uint32_t addr, uint32_t val)
{
    // sanity check
    if (!nv3)
        return; 

    // Some of these addresess are Weitek VGA stuff and we need to mask it to this first because the weitek addresses are 8-bit aligned.
    addr &= 0xFFFFFF;

    // Ensure the addresses are dword aligned.
    // I don't know why this is needed because writepriv32 is Nvidia's drivers is always dword aligned, but it crashes if you don't do this.
    // Exclude the 4bpp/8bpp CLUT for this purpose
    if (!(addr >= NV3_USER_DAC_PALETTE_START && addr <= NV3_USER_DAC_PALETTE_END))
        addr &= 0xFFFFFC;

    nv3_mmio_trace('w', addr, val);

    // gigantic set of if statements to send the write to the right subsystem
    if (addr <= NV3_PMC_END) /* PMC starts at 0 */
        nv3_pmc_write(addr, val);
    else if (addr >= NV3_PBUS_START && addr <= NV3_PBUS_END)
        nv3_pbus_write(addr, val);
    else if (addr >= NV3_PFIFO_START && addr <= NV3_PFIFO_END)
        nv3_pfifo_write(addr, val);
    else if (addr >= NV3_PTIMER_START && addr <= NV3_PTIMER_END)
        nv3_ptimer_write(addr, val);
    else if (addr >= NV3_PFB_START && addr <= NV3_PFB_END)
        nv3_pfb_write(addr, val);
    else if (addr >= NV3_PSTRAPS_START && addr <= NV3_PSTRAPS_END)
        nv3_pstraps_write(addr, val);
    else if (addr >= NV3_PROM_START && addr <= NV3_PROM_END)
        nv3_prom_write(addr, val);
    else if (addr >= NV3_PME_START && addr <= NV3_PME_END)
        nv3_pme_write(addr, val);
    else if (addr >= NV3_PGRAPH_START && addr <= NV3_PGRAPH_REAL_END) // what we're actually doing here is determined by the nv3_pgraph_* functions
        nv3_pgraph_write(addr, val);
    else if (addr >= NV3_PVIDEO_START && addr <= NV3_PVIDEO_END)
        nv3_pvideo_write(addr, val);
    else if ((addr >= NV3_PRAMDAC_START && addr <= NV3_PRAMDAC_END)
        || (addr >= NV3_USER_DAC_PALETTE_START && addr <= NV3_USER_DAC_PALETTE_END)) //clut
        nv3_pramdac_write(addr, val);
    else if (addr >= NV3_VRAM_START && addr <= NV3_VRAM_END)
        nv3_dfb_write32(addr, val, &nv3->nvbase.svga);
    else if (addr >= NV3_USER_START && addr <= NV3_USER_END)
        nv3_user_write(addr, val);
    //RAMIN is its own thing
    else 
    {
        nv_log("MMIO write arbitration failed, INVALID addr NOT mapped to any GPU subsystem 0x%08x [returning 0x00]\n", addr);
        return;
    }

    //#ifdef ENABLE_NV_LOG


    // Don't log some very very highly used registers.
    if (addr <= NV3_USER_START
    && addr != NV3_PTIMER_TIME_0_NSEC
    && addr != NV3_PTIMER_TIME_1_NSEC)
    {
        nv_register_t* reg = nv_get_register(addr, nv3_registers);

        if (reg)
        {
            if (reg->on_write)
                reg->on_write(val);
            
            nv_log_verbose_only("Register write 0x%08x to 0x%08x (%s)\n", addr, val, reg->friendly_name);
        }
        else
            nv_log_verbose_only("Unknown register write 0x%08x to 0x%08x\n", addr, val);
    }
    //#endif 
}