/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3 PBUS: 128-bit unified bus
 *
 *
 *
 * Authors: Connor Hyde, <mario64crashed@gmail.com> I need a better email dataess ;^)
 *          Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2024-2025 starfrost
 *          Copyright 2026 Marcos Alves
 */

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

// NV3 PBUS RMA - Real Mode Access for VBIOS
// This basically works like a shifter, you write one byte at a time from [0x3d0...0x3d3] and it shifts it in to build a 32-bit MMIO address...
// Putting this in pbus because imo it makes the most sense (related to memory access/memory interface)

/* PBUS 0x1800-0x18FF aliases the PCI configuration space (NV_PBUS_PCI_NV_*), a dword per
   access (the arbiter used to hand over one byte: the VBE code of every NV3 BIOS reads BAR1
   there for the mode's PhysBasePtr, and got 0). Through it the
   resman also raises the AGP request depth, read-only in config space: it bumps
   NV_PBUS_PCI_NV_18 AGP_STATUS_RQ while it is <= 3 */
#define NV3_PBUS_PCI_ALIAS 0x1800

uint32_t nv3_pbus_read(uint32_t address) 
{ 
    uint32_t ret = 0x00; 

    if (address >= NV3_PBUS_PCI_ALIAS && address < NV3_PBUS_PCI_ALIAS + 0x100) {
        for (int i = 0; i < 4; i++)
            ret |= (uint32_t) nv3_pci_read(0, (address & 0xFC) + i, 1, NULL) << (i * 8);
        return ret;
    }

    switch (address)
    {
        case NV3_PBUS_DEBUG_0:
            ret = nv3->pbus.debug_0;
            break;
        case NV3_PBUS_INTR:
            ret = nv3->pbus.intr;
            break;
        case NV3_PBUS_INTR_EN:
            ret = nv3->pbus.intr_en;
            break;
    }


    return ret; 
}

void nv3_pbus_write(uint32_t address, uint32_t val) 
{
    if (address >= NV3_PBUS_PCI_ALIAS && address < NV3_PBUS_PCI_ALIAS + 0x100) {
        if ((address & 0xFC) == NV3_AGP_STATUS_RATE)
            nv3->pbus.agp_rq = val >> 24;
        for (int i = 0; i < 4; i++)
            nv3_pci_write(0, (address & 0xFC) + i, 1, (val >> (i * 8)) & 0xFF, NULL);
        return;
    }
    switch (address)
    {
        case NV3_PBUS_DEBUG_0:
            nv3->pbus.debug_0 = val;
            break;
        // Interrupt registers
        // Interrupt state:
        // Bit 0 - PCI Bus Error
        case NV3_PBUS_INTR:
            nv3->pbus.intr &= ~val;
            nv3_pmc_handle_interrupts(true);
            break;
        case NV3_PBUS_INTR_EN:
            nv3->pbus.intr_en = val & 0x00000001;
            break;
    }

}

uint8_t nv3_pbus_rma_read(uint16_t addr)
{
    addr &= 0xFF;
    uint32_t real_final_address = 0x0;
    uint8_t ret = 0x0;

    switch (addr)
    {
        // signature so you know reads work
        case 0x00:
            ret = NV3_RMA_SIGNATURE_MSB;
            break;
        case 0x01:
            ret = NV3_RMA_SIGNATURE_BYTE2;
            break;
        case 0x02:
            ret = NV3_RMA_SIGNATURE_BYTE1;
            break;       
        case 0x03:
            ret = NV3_RMA_SIGNATURE_LSB;
            break;
        /* CR38 = 3: the address reads back too -- the BIOS saves it (and the data) around
           each of its own accesses and restores them (its RMA helpers at C000:34D5/34FC) */
        case 0x04 ... 0x07:
            ret = (nv3->pbus.rma.addr >> ((addr & 0x03) << 3)) & 0xFF;
            break;
        case 0x08 ... 0x0B:
            // reads must be dword aligned
            real_final_address = (nv3->pbus.rma.addr + (addr & 0x03));

            if (nv3->pbus.rma.addr < NV3_MMIO_SIZE) 
                ret = nv3_mmio_read8(real_final_address, NULL);
            else 
            {
                /* Do we need to read RAMIN here? */
                ret = nv3->nvbase.svga.vram[(real_final_address - NV3_MMIO_SIZE) & nv3->nvbase.svga.vram_mask];
            }

            // log current location for vbios RE
            nv_log_verbose_only("MMIO Real Mode Access read, initial address=0x%04x final RMA MMIO address=0x%08x data=0x%08x\n",
                addr, real_final_address, ret);

            break;
    }

    return ret; 
}

// Implements a 32-bit write using 16 bit port number
void nv3_pbus_rma_write(uint16_t addr, uint8_t val)
{
    // addresses are in reality 8bit so just mask it to be safe
    addr &= 0xFF;

    // format:
    // 0x00     ID
    // 0x04     Pointer to data
    // 0x08     Data port(?) 
    // 0x0B     Data - 32bit. SENT IN THE RIGHT ORDER FOR ONCE WAHOO!
    // 0x10     Increment (?) data - implemented the same as data for now 

    if (addr < 0x08)
    {
        switch (addr % 0x04)
        {
            case 0x00: // lowest byte
                nv3->pbus.rma.addr &= ~0xff;
                nv3->pbus.rma.addr |= val;
                break;
            case 0x01: // 2nd highest byte
                nv3->pbus.rma.addr &= ~0xff00;
                nv3->pbus.rma.addr |= (val << 8);
                break;
            case 0x02: // 3rd highest byte
                nv3->pbus.rma.addr &= ~0xff0000;
                nv3->pbus.rma.addr |= (val << 16);
                break;
            case 0x03: // 4th highest byte 
                nv3->pbus.rma.addr &= ~0xff000000;
                nv3->pbus.rma.addr |= (val << 24);
                break;
        }
    }
    // Data to send to MMIO
    else
    {
        switch (addr % 0x04)
        {
            case 0x00: // lowest byte
                nv3->pbus.rma.data &= ~0xff;
                nv3->pbus.rma.data |= val;
                break;
            case 0x01: // 2nd highest byte
                nv3->pbus.rma.data &= ~0xff00;
                nv3->pbus.rma.data |= (val << 8);
                break;
            case 0x02: // 3rd highest byte
                nv3->pbus.rma.data &= ~0xff0000;
                nv3->pbus.rma.data |= (val << 16);
                break;
            case 0x03: // 4th highest byte 
                nv3->pbus.rma.data &= ~0xff000000;
                nv3->pbus.rma.data |= (val << 24);

                nv_log_verbose_only("MMIO Real Mode Access write transaction complete, initial address=0x%04x final RMA MMIO address=0x%08x data=0x%08x\n",
                addr, nv3->pbus.rma.addr, nv3->pbus.rma.data);

                if (nv3->pbus.rma.addr < NV3_MMIO_SIZE) 
                    nv3_mmio_write32(nv3->pbus.rma.addr, nv3->pbus.rma.data, NULL);
                else // failsafe code, i don't think you will ever write outside of VRAM?
                {
                    uint32_t* vram_32 = (uint32_t*)nv3->nvbase.svga.vram;
                    vram_32[((nv3->pbus.rma.addr - NV3_MMIO_SIZE) & nv3->nvbase.svga.vram_mask) >> 2] = nv3->pbus.rma.data;
                }
                    
                    
                break;
        }
    }

    if (addr & 0x10)
        nv3->pbus.rma.addr += 0x04; // Alignment
}