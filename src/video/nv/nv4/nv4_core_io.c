/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV4 I/O, including Real-Mode Access (RMA), memory mapping, PCI, AGP, SVGA and MMIO via PCI BARs
 *         
 *          MMIO dumps are available at: https://nvwiki.org/misc/NVDumps/
 *
 *
 * Authors: Connor Hyde, <mario64crashed@gmail.com> I need a better email address ;^)
 *
 *          Copyright 2024-2025 starfrost
 */


// Prototypes for functions only used in this translation unit

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/io.h>
#include <86box/pci.h>
#include <86box/rom.h> 
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv4.h>


void nv4_init_mappings_mmio(void);
void nv4_init_mappings_svga(void);
bool nv4_is_svga_redirect_address(uint32_t addr);

uint8_t nv4_svga_read(uint16_t addr, void* priv);
void nv4_svga_write(uint16_t addr, uint8_t val, void* priv);


uint32_t nv4_mmio_arbitrate_read(uint32_t addr)
{
    nv_log_verbose_only("MMIO read from address=0x%08x\n", addr);
}

void nv4_mmio_arbitrate_write(uint32_t addr, uint32_t val)
{
    nv_log_verbose_only("MMIO write to address=0x%08x value %08x\n", addr, val);
}

// Determine if this address needs to be redirected to the SVGA subsystem.

bool nv4_is_svga_redirect_address(uint32_t addr)
{
    return (addr >= NV4_PRMVIO_START && addr <= NV4_PRMVIO_END)                     // VGA
    || (addr >= NV4_PRMCIO_START && addr <= NV4_PRMCIO_END)                         // CRTC
    || (addr >= NV4_USER_DAC_START && addr <= NV4_USER_DAC_END);                  // Note: 6813c6-6813c9 are ignored somewhere else
}

uint8_t nv4_rma_read(uint16_t addr)
{
    addr &= 0xFF;
    uint32_t real_final_address = 0x0;
    uint8_t ret = 0x0;

    switch (addr)
    {
        // signature so you know reads work
        case 0x00:
            ret = NV4_PRMIO_RMA_ID_CODE_VALID & 0xFF;
            break;
        case 0x01:
            ret = (NV4_PRMIO_RMA_ID_CODE_VALID >> 8) & 0xFF;
            break;
        case 0x02:
            ret = (NV4_PRMIO_RMA_ID_CODE_VALID >> 16) & 0xFF;
            break;       
        case 0x03:
            ret = (NV4_PRMIO_RMA_ID_CODE_VALID >> 24) & 0xFF;
            break;
        case 0x08 ... 0x0B:
            // reads must be dword aligned
            real_final_address = (nv4->pbus.rma.addr + (addr & 0x03));

            if (nv4->pbus.rma.addr < NV4_MMIO_SIZE) 
                ret = nv4_mmio_read8(real_final_address, NULL);
            else 
            {
                /* Do we need to read RAMIN here? */
                ret = nv4->nvbase.svga.vram[real_final_address - NV4_MMIO_SIZE] & (nv4->nvbase.svga.vram_max - 1);
            }

            // log current location for vbios RE
            nv_log_verbose_only("MMIO Real Mode Access read, initial address=0x%08x final RMA MMIO address=0x%08x data=0x%08x\n",
                addr, real_final_address, ret);

            break;
    }

    return ret; 
}

// Implements a 32-bit write using 16 bit port number
void nv4_rma_write(uint16_t addr, uint8_t val)
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
                nv4->pbus.rma.addr &= ~0xff;
                nv4->pbus.rma.addr |= val;
                break;
            case 0x01: // 2nd highest byte
                nv4->pbus.rma.addr &= ~0xff00;
                nv4->pbus.rma.addr |= (val << 8);
                break;
            case 0x02: // 3rd highest byte
                nv4->pbus.rma.addr &= ~0xff0000;
                nv4->pbus.rma.addr |= (val << 16);
                break;
            case 0x03: // 4th highest byte 
                nv4->pbus.rma.addr &= ~0xff000000;
                nv4->pbus.rma.addr |= (val << 24);
                break;
        }
    }
    // Data to send to MMIO
    else
    {
        switch (addr % 0x04)
        {
            case 0x00: // lowest byte
                nv4->pbus.rma.data &= ~0xff;
                nv4->pbus.rma.data |= val;
                break;
            case 0x01: // 2nd highest byte
                nv4->pbus.rma.data &= ~0xff00;
                nv4->pbus.rma.data |= (val << 8);
                break;
            case 0x02: // 3rd highest byte
                nv4->pbus.rma.data &= ~0xff0000;
                nv4->pbus.rma.data |= (val << 16);
                break;
            case 0x03: // 4th highest byte 
                nv4->pbus.rma.data &= ~0xff000000;
                nv4->pbus.rma.data |= (val << 24);

                nv_log_verbose_only("MMIO Real Mode Access write transaction complete, initial address=0x%04x final RMA MMIO address=0x%08x data=0x%08x\n",
                addr, nv4->pbus.rma.addr, nv4->pbus.rma.data);

                if (nv4->pbus.rma.addr < NV4_MMIO_SIZE) 
                    nv4_mmio_write32(nv4->pbus.rma.addr, nv4->pbus.rma.data, NULL);
                else // failsafe code, i don't think you will ever write outside of VRAM?
                {
                    uint32_t* vram_32 = (uint32_t*)nv4->nvbase.svga.vram;
                    vram_32[(nv4->pbus.rma.addr - NV4_MMIO_SIZE) >> 2] = nv4->pbus.rma.data;
                }
                    
                    
                break;
        }
    }

    if (addr & 0x10)
        nv4->pbus.rma.addr += 0x04; // Alignment
}



// All MMIO regs are 32-bit i believe internally
// so we have to do some munging to get this to read

// Read 8-bit MMIO
uint8_t nv4_mmio_read8(uint32_t addr, void* priv)
{
    uint32_t ret = 0x00;

    // Some of these addresses are Weitek VGA stuff and we need to mask it to this first because the weitek addresses are 8-bit aligned.
    addr &= 0xFFFFFF;

    // We need to specifically exclude this particular set of registers
    // so we can write the 4/8bpp CLUT
    if (addr >= NV4_USER_DAC_PALETTE_START && addr <= NV4_USER_DAC_PALETTE_END) 
    {
        // Throw directly into PRAMDAC
        return nv4_mmio_arbitrate_read(addr);
    }
        
    if (nv4_is_svga_redirect_address(addr))
    {
        // svga writes are not logged anyway rn
        uint32_t real_address = addr & 0x3FF;

        ret = nv4_svga_read(real_address, nv4);

        nv_log_verbose_only("Redirected MMIO read8 to SVGA: addr=0x%04x returned 0x%04x\n", addr, ret);

        return ret; 
    }

    // see if unaligned reads are a problem
    ret = nv4_mmio_read32(addr, priv);
    return (uint8_t)(ret >> ((addr & 3) << 3) & 0xFF);
}

// Read 16-bit MMIO
uint16_t nv4_mmio_read16(uint32_t addr, void* priv)
{
    uint32_t ret = 0x00;

    // Some of these addresses are Weitek VGA stuff and we need to mask it to this first because the weitek addresses are 8-bit aligned.
    addr &= 0xFFFFFF;

    if (nv4_is_svga_redirect_address(addr))
    {
        // svga writes are not logged anyway rn
        uint32_t real_address = addr & 0x3FF;

        ret = nv4_svga_read(real_address, nv4)
        | (nv4_svga_read(real_address + 1, nv4) << 8);
        
        nv_log_verbose_only("Redirected MMIO read16 to SVGA: addr=0x%04x returned 0x%04x\n", addr, ret);

        return ret; 
    }

    ret = nv4_mmio_read32(addr, priv);
    return (uint8_t)(ret >> ((addr & 3) << 3) & 0xFFFF);
}

// Read 32-bit MMIO
uint32_t nv4_mmio_read32(uint32_t addr, void* priv)
{
    uint32_t ret = 0x00;

    // Some of these addresses are Weitek VGA stuff and we need to mask it to this first because the weitek addresses are 8-bit aligned.
    addr &= 0xFFFFFF;

    if (nv4_is_svga_redirect_address(addr))
    {
        // svga writes are not logged anyway rn
        uint32_t real_address = addr & 0x3FF;

        ret = nv4_svga_read(real_address, nv4)
        | (nv4_svga_read(real_address + 1, nv4) << 8)
        | (nv4_svga_read(real_address + 2, nv4) << 16)
        | (nv4_svga_read(real_address + 3, nv4) << 24);

        nv_log_verbose_only("Redirected MMIO read32 to SVGA: addr=0x%04x returned 0x%04x\n", addr, ret);

        return ret; 
    }

    ret = nv4_mmio_arbitrate_read(addr);

    return ret; 
}

// Write 8-bit MMIO
void nv4_mmio_write8(uint32_t addr, uint8_t val, void* priv)
{
    addr &= 0xFFFFFF;

    // We need to specifically exclude this particular set of registers
    // so we can write the 4/8bpp CLUT
    if (addr >= NV4_USER_DAC_PALETTE_START && addr <= NV4_USER_DAC_PALETTE_END) 
    {
        // Throw directly into PRAMDAC
        nv4_mmio_arbitrate_write(addr, val);
        return; 
    }

    // This is weitek vga stuff
    // If we need to add more of these we can convert these to a switch statement
    if (nv4_is_svga_redirect_address(addr))
    {
        // svga writes are not logged anyway rn
        uint32_t real_address = addr & 0x3FF;

        nv_log_verbose_only("Redirected MMIO write8 to SVGA: addr=0x%04x val=0x%02x\n", addr, val);

        nv4_svga_write(real_address, val & 0xFF, nv4);

        return; 
    }
    
    // overwrite first 8bits of a 32 bit value
    uint32_t new_val = nv4_mmio_read32(addr, NULL);

    new_val &= (~0xFF << (addr & 3) << 3);
    new_val |= (val << ((addr & 3) << 3));

    nv4_mmio_write32(addr, new_val, priv);
}

// Write 16-bit MMIO
void nv4_mmio_write16(uint32_t addr, uint16_t val, void* priv)
{
    addr &= 0xFFFFFF;

    // This is weitek vga stuff
    if (nv4_is_svga_redirect_address(addr))
    {
        // svga writes are not logged anyway rn
        uint32_t real_address = addr & 0x3FF;

        nv_log_verbose_only("Redirected MMIO write16 to SVGA: addr=0x%04x val=0x%02x\n", addr, val);

        nv4_svga_write(real_address, val & 0xFF, nv4);
        nv4_svga_write(real_address + 1, (val >> 8) & 0xFF, nv4);
        
        return; 
    }

    // overwrite first 16bits of a 32 bit value
    uint32_t new_val = nv4_mmio_read32(addr, NULL);

    new_val &= (~0xFFFF << (addr & 3) << 3);
    new_val |= (val << ((addr & 3) << 3));

    nv4_mmio_write32(addr, new_val, priv);
}

// Write 32-bit MMIO
void nv4_mmio_write32(uint32_t addr, uint32_t val, void* priv)
{
    addr &= 0xFFFFFF;

    // This is weitek vga stuff
    if (nv4_is_svga_redirect_address(addr))
    {
        // svga writes are not logged anyway rn
        uint32_t real_address = addr & 0x3FF;

        nv_log_verbose_only("Redirected MMIO write32 to SVGA: addr=0x%04x val=0x%02x\n", addr, val);

        nv4_svga_write(real_address, val & 0xFF, nv4);
        nv4_svga_write(real_address + 1, (val >> 8) & 0xFF, nv4);
        nv4_svga_write(real_address + 2, (val >> 16) & 0xFF, nv4);
        nv4_svga_write(real_address + 3, (val >> 24) & 0xFF, nv4);
        
        return; 
    }

    nv4_mmio_arbitrate_write(addr, val);
}

// PCI stuff
// BAR0         Pointer to MMIO space & RAMIN
// BAR1         Pointer to Linear Framebuffer 
uint8_t nv4_pci_read(int32_t func, int32_t addr, void* priv)
{
    uint8_t ret = 0x00;

    // sanity check
    if (!nv4)
        return ret; 

    // Convert to the MMIO addresses
    if (addr <= 0xFF)
        addr += 0x1800;

    // Anything not listed is 0x00
    // PCI values extracted from https://nvwiki.org/misc/NVDumps/RivaMobileNV4/Nv4win_HL/nv4bar0.bin
    // STB V4400 (running Half-Life)
    switch (addr) 
    {
        // Get the pci vendor id..

        case NV4_PBUS_PCI_VENDOR_ID:
            ret = (NV4_PBUS_PCI_DEVICE_VENDOR_NVIDIA & 0xFF);
            break;
        
        case NV4_PBUS_PCI_VENDOR_ID + 1: // all access 8bit
            ret = (NV4_PBUS_PCI_DEVICE_VENDOR_NVIDIA >> 8);
            break;

        // device id

        case NV4_PBUS_PCI_DEVICE_ID:
            ret = (NV_PCI_DEVICE_NV4 & 0xFF);
            break;
        
        case NV4_PBUS_PCI_DEVICE_ID + 1:
            ret = (NV_PCI_DEVICE_NV4 >> 8);
            break;
        
        // various capabilities enabled by default 
        // IO space         enabled
        // Memory space     enabled
        // Bus master       enabled
        // Write/inval      enabled
        // Pal snoop        enabled
        // Capabiliies list enabled
        // 66Mhz FSB        capable

        case NV4_PBUS_PCI_COMMAND:
            ret = nv4->nvbase.pci_config.pci_regs[PCI_REG_COMMAND_L]; 
            break;
        // COMMAND_L not used 

        // pci status register
        case NV4_PBUS_PCI_STATUS:
            if (nv4->straps 
            & NV4_STRAP_BUS_SPEED_66MHZ)
                ret = (nv4->nvbase.pci_config.pci_regs[PCI_REG_STATUS_L] | NV4_PBUS_PCI_STATUS_66MHZ_CAPABLE);
            else
                ret = nv4->nvbase.pci_config.pci_regs[PCI_REG_STATUS_L];

            break;

        case NV4_PBUS_PCI_STATUS_2:
            ret = (nv4->nvbase.pci_config.pci_regs[PCI_REG_STATUS_H]) & (NV4_PBUS_PCI_STATUS_2_DEVSEL_TIMING_FAST << NV4_PBUS_PCI_STATUS_2_DEVSEL_TIMING);
            break;
        
        case NV4_PBUS_PCI_REVISION_ID:
            ret = nv4->nvbase.gpu_revision; // Commercial release
            break;
       
        case PCI_REG_PROG_IF:
            ret = 0x00;
            break;

        // We only need to return 0x30 since the VGA class code is 0x30000
        case NV4_PBUS_PCI_CLASS_CODE:
            ret = (NV4_PBUS_PCI_CLASS_CODE_VGA) >> 16; // CLASS_CODE_VGA 
            break;

        
        case NV4_PBUS_PCI_LATENCY_TIMER:
        case NV4_PBUS_PCI_HEADER_TYPE:
            ret = 0x00;
            break;

        // BARs are marked as prefetchable per the datasheet
        case NV4_PBUS_PCI_BAR0_INFO:
        case NV4_PBUS_PCI_BAR1_INFO:
            // only bit that matters is bit 3 (prefetch bit)
            ret = (NV4_PBUS_PCI_BAR_PREFETCHABLE_MERGABLE << NV4_PBUS_PCI_BAR_PREFETCHABLE);
            break;

        // MMIO base address
        case NV4_PBUS_PCI_BAR0_BASE_31_TO_24:
            ret = nv4->nvbase.bar0_mmio_base >> 24;//8bit value
            break; 

        case NV4_PBUS_PCI_BAR1_BASE_31_TO_24:
            ret = nv4->nvbase.bar1_lfb_base >> 24; //8bit value
            break;
        
        case NV4_PBUS_PCI_BAR_RESERVED_START ... NV4_PBUS_PCI_BAR_RESERVED_END:
        case NV4_PBUS_PCI_BAR0_UNUSED1:
        case NV4_PBUS_PCI_BAR0_UNUSED2:
        case NV4_PBUS_PCI_BAR1_UNUSED1:
        case NV4_PBUS_PCI_BAR1_UNUSED2:
        
            ret = 0x00; // hard lock
            break;

        case NV4_PBUS_PCI_ROM:
            ret = nv4->nvbase.pci_config.vbios_enabled;
            break;

        case NV4_PBUS_PCI_INTR_LINE:
            ret = nv4->nvbase.pci_config.int_line;
            break;
        
        case NV4_PBUS_PCI_INTR_PIN:
            ret = PCI_INTA;
            break;

        //
        // Capabilities pointers
        //

        case NV4_PBUS_PCI_NEXT_PTR:
            ret = NV4_PBUS_PCI_CAP_PTR_POWER_MGMT;
            break;

        case NV4_PBUS_PCIPOWER_NEXT_PTR:
            ret = NV4_PBUS_PCI_CAP_PTR_AGP;
            break;

        // AGP is the end of the chain
        case NV4_PBUS_AGP_NEXT_PTR:
            ret = 0x00;
            break;

        case NV4_PBUS_PCI_MAX_LAT:
            ret = NV4_PBUS_PCI_MAX_LAT_250NS;
            break;
            
        // these map to the subsystem 
        // todo: port this bugfix to NV4
        case NV4_PBUS_PCI_SUBSYSTEM_VENDOR_ID_WRITABLE:
        case NV4_PBUS_PCI_SUBSYSTEM_VENDOR_ID_WRITABLE + 1:
        case NV4_PBUS_PCI_SUBSYSTEM_ID_WRITABLE:
        case NV4_PBUS_PCI_SUBSYSTEM_ID_WRITABLE + 1:
            ret = nv4->nvbase.pci_config.pci_regs[NV4_PBUS_PCI_SUBSYSTEM_ID + (addr & 0x03)];
            break;

        case NV4_PBUS_AGP_CAPABILITIES:
            ret = NV4_PBUS_AGP_CAPABILITY_AGP;               // AGP capable device
            break;
        case NV4_PBUS_AGP_REV:
            ret = (NV4_PBUS_AGP_REV_MAJOR_1 << NV4_PBUS_AGP_REV_MAJOR) | NV4_PBUS_AGP_REV_MINOR;
            break;
        case NV4_PBUS_AGP_STATUS_RATE:
            ret = NV4_PBUS_AGP_STATUS_RATE_1X_AND_2X;
            break;
        case NV4_PBUS_AGP_STATUS_SBA:
            ret = (NV4_PBUS_AGP_STATUS_SBA_STATUS_CAPABLE) << NV4_PBUS_AGP_STATUS_SBA_STATUS;     // Sideband is supported on NV4
            break;
        case NV4_PBUS_AGP_STATUS_RQ:
            ret = NV4_PBUS_AGP_STATUS_RQ_16;
            break;
        case NV4_PBUS_AGP_COMMAND_2:
            ret = (nv4->nvbase.agp_enabled) << NV4_PBUS_AGP_COMMAND_2_AGP_ENABLED
            | (nv4->nvbase.agp_sba_enabled) << NV4_PBUS_AGP_COMMAND_2_SBA_ENABLED;
            break;
        default: // by default just return pci_config.pci_regs (default value for nonwritten registers is 0x00)
            ret = nv4->nvbase.pci_config.pci_regs[addr];
            break;
        
    }

    nv_log("nv4_pci_read func=0x%04x addr=0x%04x ret=0x%04x\n", func, addr & 0xFF, ret);
    return ret; 
}


// nv4 pci/agp write
void nv4_pci_write(int32_t func, int32_t addr, uint8_t val, void* priv)
{
    // sanity check
    if (!nv4)
        return; 

    // Convert to the MMIO addresses
    if (addr <= 0xFF)
        addr += 0x1800;
    // some addresses are not writable so can't have any effect and can't be allowed to be modified using this code
    // as an example, only the most significant byte of the PCI BARs can be modified
    if (addr == NV4_PBUS_PCI_BAR0_UNUSED1 || addr == NV4_PBUS_PCI_BAR0_UNUSED2
    && addr == NV4_PBUS_PCI_BAR1_UNUSED1 || addr == NV4_PBUS_PCI_BAR1_UNUSED2)
        return;

    nv_log("nv4_pci_write func=0x%04x addr=0x%04x val=0x%04x\n", func, addr & 0xFF, val);

    nv4->nvbase.pci_config.pci_regs[addr] = val;

    switch (addr)
    {
        // standard pci command stuff
        case NV4_PBUS_PCI_COMMAND:
            nv4->nvbase.pci_config.pci_regs[PCI_REG_COMMAND_L] = val;
            // actually update the mappings
            nv4_update_mappings();
            break;
        case NV4_PBUS_PCI_COMMAND_H:
            nv4->nvbase.pci_config.pci_regs[PCI_REG_COMMAND_H] = val;
            // actually update the mappings
            nv4_update_mappings();          
            break;
        // pci status register
        case NV4_PBUS_PCI_STATUS:
            nv4->nvbase.pci_config.pci_regs[PCI_REG_STATUS_L] = val | (NV4_PBUS_PCI_STATUS_66MHZ_CAPABLE << NV4_PBUS_PCI_STATUS_66MHZ);
            break;
        case NV4_PBUS_PCI_STATUS_2:
            nv4->nvbase.pci_config.pci_regs[PCI_REG_STATUS_H] = val | (NV4_PBUS_PCI_STATUS_2_DEVSEL_TIMING_FAST << NV4_PBUS_PCI_STATUS_2_DEVSEL_TIMING);
            break;
        //TODO: ACTUALLY REMAP THE MMIO AND NV_USER
        case NV4_PBUS_PCI_BAR0_BASE_31_TO_24:
            nv4->nvbase.bar0_mmio_base = val << 24;
            nv4_update_mappings();
            break; 
        case NV4_PBUS_PCI_BAR1_BASE_31_TO_24:
            nv4->nvbase.bar1_lfb_base = val << 24;
            nv4_update_mappings();
            break;
        
        case NV4_PBUS_PCI_ROM:
        case NV4_PBUS_PCI_ROM_BASE:
            
            // make sure we are actually toggling the vbios, not the rom base
            if (addr == NV4_PBUS_PCI_ROM)
                nv4->nvbase.pci_config.vbios_enabled = (val & 0x01);

            if (nv4->nvbase.pci_config.vbios_enabled)
            {
                // First see if we simply wanted to change the VBIOS location

                // Enable it in case it was disabled before
                mem_mapping_enable(&nv4->nvbase.vbios.mapping);

                if (addr != NV4_PBUS_PCI_ROM)
                {
                    uint32_t old_addr = nv4->nvbase.vbios.mapping.base;
                    // 9bit register
                    uint32_t new_addr = nv4->nvbase.pci_config.pci_regs[NV4_PBUS_PCI_ROM + 3] << 24 |
                    nv4->nvbase.pci_config.pci_regs[NV4_PBUS_PCI_ROM + 2] << 16;

                    // only bits 31;22 matter
                    //new_addr &= 0xFFC00000;

                    // move it
                    mem_mapping_set_addr(&nv4->nvbase.vbios.mapping, new_addr, 0x8000);

                    nv_log("...i like to move it move it (VBIOS Relocation) 0x%x -> 0x%x\n", old_addr, new_addr);

                }
                else
                {
                    nv_log("...VBIOS Enable\n");
                }
            }
            else
            {
                nv_log("...VBIOS Disable\n");
                mem_mapping_disable(&nv4->nvbase.vbios.mapping);

            }
            break;
        case NV4_PBUS_PCI_INTR_LINE:
            nv4->nvbase.pci_config.int_line = val;
            break;
        // these are mirrored to the subsystem id and also stored in the ROMBIOS
        //todo: port to pci
        case NV4_PBUS_PCI_SUBSYSTEM_ID_WRITABLE:
        case NV4_PBUS_PCI_SUBSYSTEM_ID_WRITABLE + 1:
        case NV4_PBUS_PCI_SUBSYSTEM_VENDOR_ID_WRITABLE:
        case NV4_PBUS_PCI_SUBSYSTEM_VENDOR_ID_WRITABLE + 1:
            nv4->nvbase.pci_config.pci_regs[NV4_PBUS_PCI_SUBSYSTEM_ID + (addr & 0x03)] = val;
            break;
        case NV4_PBUS_AGP_COMMAND_2:
            nv4->nvbase.agp_enabled = (val >> NV4_PBUS_AGP_COMMAND_2_AGP_ENABLED) & 0x01;
            nv4->nvbase.agp_sba_enabled = (val >> NV4_PBUS_AGP_COMMAND_2_SBA_ENABLED) & 0x01;
            break;
        default:
            break;
    }
}


void nv4_speed_changed(void* priv)
{
    // sanity check
    if (!nv4)
        return; 
        
    nv4_recalc_timings(&nv4->nvbase.svga);
}

// Force Redraw
// Reset etc.
void nv4_force_redraw(void* priv)
{
    // sanity check
    if (!nv4)
        return; 

    nv4->nvbase.svga.fullchange = changeframecount; 
}

// CHECK that ramin is the smae as nv4

// Read 8-bit ramin
uint8_t nv4_ramin_read8(uint32_t addr, void* priv)
{
    if (!nv4) return 0x00;

    addr &= (nv4->nvbase.svga.vram_max - 1);
    uint32_t raw_addr = addr; // saved after and

    addr ^= (nv4->nvbase.svga.vram_max - 0x10);

    uint32_t val = 0x00;

    //if (!nv4_ramin_arbitrate_read(addr, &val)) // Oh well
    //{
        val = (uint8_t)nv4->nvbase.svga.vram[addr];
        nv_log_verbose_only("Read byte from PRAMIN addr=0x%08x (raw address=0x%08x)\n", addr, raw_addr);
    //}

    return (uint8_t)val;
}

// Read 16-bit ramin
uint16_t nv4_ramin_read16(uint32_t addr, void* priv)
{
    if (!nv4) return 0x00;

    addr &= (nv4->nvbase.svga.vram_max - 1);

    // why does this not work in one line
    svga_t* svga = &nv4->nvbase.svga;
    uint16_t* vram_16bit = (uint16_t*)svga->vram;
    uint32_t raw_addr = addr; // saved after and

    addr ^= (nv4->nvbase.svga.vram_max - 0x10);
    addr >>= 1; // what

    uint32_t val = 0x00;

    //if (!nv4_ramin_arbitrate_read(addr, &val))
    //{
        val = (uint16_t)vram_16bit[addr];
        nv_log_verbose_only("Read word from PRAMIN addr=0x%08x (raw address=0x%08x)\n", addr, raw_addr);
    //}

    return val;
}

// Read 32-bit ramin
uint32_t nv4_ramin_read32(uint32_t addr, void* priv)
{
    if (!nv4) 
        return 0x00;

    addr &= (nv4->nvbase.svga.vram_max - 1);

    // why does this not work in one line
    uint32_t* vram_32bit = (uint32_t*)nv4->nvbase.svga.vram;
    uint32_t raw_addr = addr; // saved after and logged

    addr ^= (nv4->nvbase.svga.vram_max - 0x10);
    addr >>= 2; // what

    uint32_t val = 0x00;

    //if (!nv4_ramin_arbitrate_read(addr, &val))
    //{
        val = vram_32bit[addr];
        nv_log_verbose_only("Read dword from PRAMIN 0x%08x <- 0x%08x (raw address=0x%08x)\n", val, addr, raw_addr);
    //}

    return val;
}

// Write 8-bit ramin
void nv4_ramin_write8(uint32_t addr, uint8_t val, void* priv)
{
    if (!nv4) return;

    addr &= (nv4->nvbase.svga.vram_max - 1);
    uint32_t raw_addr = addr; // saved after and

    // Structures in RAMIN are stored from the bottom of vram up in reverse order
    // this can be explained without bitwise math like so:
    // real VRAM address = VRAM_size - (ramin_address - (ramin_address % reversal_unit_size)) - reversal_unit_size + (ramin_address % reversal_unit_size) 
    // reversal unit size in this case is 16 bytes, vram size is 2-8mb (but 8mb is zx/nv4t only and 2mb...i haven't found a 22mb card)
    addr ^= (nv4->nvbase.svga.vram_max - 0x10);

    uint32_t val32 = (uint32_t)val;

    //if (!nv4_ramin_arbitrate_write(addr, val32))
    //{
        nv4->nvbase.svga.vram[addr] = val;
        nv_log_verbose_only("Write byte to PRAMIN addr=0x%08x val=0x%02x (raw address=0x%08x)\n", addr, val, raw_addr);
    //}
}

// Write 16-bit ramin
void nv4_ramin_write16(uint32_t addr, uint16_t val, void* priv)
{
    if (!nv4) return;

    addr &= (nv4->nvbase.svga.vram_max - 1);

    // why does this not work in one line
    svga_t* svga = &nv4->nvbase.svga;
    uint16_t* vram_16bit = (uint16_t*)svga->vram;
    uint32_t raw_addr = addr; // saved after and

    addr ^= (nv4->nvbase.svga.vram_max - 0x10);
    addr >>= 1; // what

    uint32_t val32 = (uint32_t)val;

    //if (!nv4_ramin_arbitrate_write(addr, val32))
    //{
        vram_16bit[addr] = val;
        nv_log_verbose_only("Write word to PRAMIN addr=0x%08x val=0x%04x (raw address=0x%08x)\n", addr, val, raw_addr);
    //}


}

// Write 32-bit ramin
void nv4_ramin_write32(uint32_t addr, uint32_t val, void* priv)
{
    if (!nv4) return;

    addr &= (nv4->nvbase.svga.vram_max - 1);

    // why does this not work in one line
    svga_t* svga = &nv4->nvbase.svga;
    uint32_t* vram_32bit = (uint32_t*)svga->vram;
    uint32_t raw_addr = addr; // saved after and

    addr ^= (nv4->nvbase.svga.vram_max - 0x10);
    addr >>= 2; // what

    //if (!nv4_ramin_arbitrate_write(addr, val))
    //{
        vram_32bit[addr] = val;
        nv_log_verbose_only("Write dword to PRAMIN addr=0x%08x val=0x%08x (raw address=0x%08x)\n", addr, val, raw_addr);
    //}
}

// Read from SVGA core memory
uint8_t nv4_svga_read(uint16_t addr, void* priv)
{
    // CR = CRTC Controller
    // CRE = CRTC Controller Extended (weitek)
    uint8_t ret = 0x00;

    // sanity check
    if (!nv4)
        return ret; 

    // If we need to RMA from GPU MMIO, go do that
    if (addr >= NV4_RMA_REGISTER_START
    && addr <= NV4_RMA_REGISTER_END)
    {
        if (!(nv4->pbus.rma.mode & 0x01))
            return ret;

        // must be dword aligned
        uint32_t real_rma_read_addr = ((nv4->pbus.rma.mode & 0x0E) << 1) + (addr & 0x03); 
        ret = nv4_rma_read(real_rma_read_addr);
        return ret;
    }

    // mask off b0/d0 registers 
    if ((((addr & 0xFFF0) == 0x3D0 
    || (addr & 0xFFF0) == 0x3B0) && addr < 0x3de) 
    && !(nv4->nvbase.svga.miscout & 1))
        addr ^= 0x60;

    switch (addr)
    {
        // Alias for "get current SVGA CRTC register ID"
        case NV4_CIO_CRX_COLOR:
            ret = nv4->nvbase.svga.crtcreg;
            break;
        case NV4_CIO_CR_COLOR:
            // Support the extended NVIDIA CRTC register range
            switch (nv4->nvbase.svga.crtcreg)
            {
                case NV4_CIO_CRE_RL0_INDEX:
                    ret = nv4->nvbase.svga.displine & 0xFF; 
                    break;
                    /* Is rl1?*/
                case NV4_CIO_CRE_RL1_INDEX:
                    ret = (nv4->nvbase.svga.displine >> 8) & 7;
                    break;
                case NV4_CIO_CRE_DDC_STATUS_INDEX:
                    ret = i2c_gpio_get_sda(nv4->nvbase.i2c) << 3
                    | i2c_gpio_get_scl(nv4->nvbase.i2c) << 2;

                    break;
                default:
                    ret = nv4->nvbase.svga.crtc[nv4->nvbase.svga.crtcreg];
            }
            break;
        default:
            ret = svga_in(addr, &nv4->nvbase.svga);
            break;
    }


    return ret; //TEMP
}

// Write to SVGA core memory
void nv4_svga_write(uint16_t addr, uint8_t val, void* priv)
{
    // sanity check
    if (!nv4)
        return; 

    // If we need to RMA to GPU MMIO, go do that
    if (addr >= NV4_RMA_REGISTER_START
    && addr <= NV4_RMA_REGISTER_END)
    {
        // we don't need to store these registers...
        nv4->pbus.rma.rma_regs[addr & 3] = val;

        if (!(nv4->pbus.rma.mode & 0x01)) // we are halfway through sending something
            return;

        uint32_t real_rma_write_addr = ((nv4->pbus.rma.mode & (0x0E)) << 1) + (addr & 0x03); 

        nv4_rma_write(real_rma_write_addr, nv4->pbus.rma.rma_regs[addr & 0x03]);
        return;
    }

    // mask off b0/d0 registers 
    if ((((addr & 0xFFF0) == 0x3D0 || (addr & 0xFFF0) == 0x3B0) 
    && addr < 0x3de) 
    && !(nv4->nvbase.svga.miscout & 1))//miscout bit 7 controls mappping
        addr ^= 0x60;

    uint8_t crtcreg = nv4->nvbase.svga.crtcreg;
    uint8_t old_val = 0x00;

    // todo:
    // Pixel formats (8bit vs 555 vs 565)
    // VBE 3.0?
    
    switch (addr)
    {
        case NV4_CIO_CRX_COLOR:
            // real mode access to GPU MMIO space...
            nv4->nvbase.svga.crtcreg = val;
            break;
        // support the extended crtc regs and debug this out
        case NV4_CIO_CR_COLOR:

            // Implements the VGA Protect register
            if ((nv4->nvbase.svga.crtcreg < NV4_CIO_CR_OVL_INDEX) && (nv4->nvbase.svga.crtc[NV4_CIO_CR_VRE_INDEX] & 0x80))
                return;

            // Ignore certain bits when VGA Protect register set and we are writing to CRTC register=07h
            if ((nv4->nvbase.svga.crtcreg == NV4_CIO_CR_OVL_INDEX) && (nv4->nvbase.svga.crtc[NV4_CIO_CR_VRE_INDEX] & 0x80))
                val = (nv4->nvbase.svga.crtc[NV4_CIO_CR_OVL_INDEX] & ~0x10) | (val & 0x10);

            // set the register value...
            old_val = nv4->nvbase.svga.crtc[crtcreg];

            nv4->nvbase.svga.crtc[crtcreg] = val;
            // ...now act on it

            // Handle nvidia extended Bank0/Bank1 IDs
            switch (crtcreg)
            {
                case NV4_CIO_CRE_PAGE0_INDEX:
                    nv4->nvbase.cio_read_bank = val;
                    if (nv4->nvbase.svga.chain4) // chain4 addressing (planar?)
                        nv4->nvbase.svga.read_bank = nv4->nvbase.cio_read_bank << 15;
                    else
                        nv4->nvbase.svga.read_bank = nv4->nvbase.cio_read_bank << 13; // extended bank numbers
                    break;
                case NV4_CIO_CRE_PAGE1_INDEX:
                    nv4->nvbase.cio_write_bank = val;
                    if (nv4->nvbase.svga.chain4)
                        nv4->nvbase.svga.write_bank = nv4->nvbase.cio_write_bank << 15;
                    else
                        nv4->nvbase.svga.write_bank = nv4->nvbase.cio_write_bank << 13;
                    break;
                case NV4_CIO_CRE_RMA_INDEX:
                    nv4->pbus.rma.mode = val & NV4_PRMIO_RMA_MODE_MAX;
                    break;
                /* Handle some large screen stuff */
                case NV4_CIO_CRE_PIXEL_INDEX:
                    if (val & 1 << (NV4_CIO_CRE_LSR_VDT_10)) 
                        nv4->nvbase.svga.vtotal += 0x400;
                    if (val & 1 << (NV4_CIO_CRE_LSR_VRS_10))  
                        nv4->nvbase.svga.vblankstart += 0x400;
                    if (val & 1 << (NV4_CIO_CRE_LSR_VBS_10)) 
                        nv4->nvbase.svga.vsyncstart += 0x400;
                    if (val & 1 << (NV4_CIO_CRE_LSR_HBE_6)) 
                        nv4->nvbase.svga.hdisp += 0x400; 
                
                    /* Make sure dispend and vblankstart are right if we are displaying above 1024 vert */
                    if (nv4->nvbase.svga.crtc[NV4_CIO_CRE_PIXEL_INDEX] & 1 << (NV4_CIO_CRE_LSR_VDE_10)) 
                        nv4->nvbase.svga.dispend += 0x400;

                    break;
                case NV4_CIO_CRE_HEB_INDEX: // large screen bit
                    if (val & 0x01)
                        nv4->nvbase.svga.hdisp += 0x100;
                    break;
                case NV4_CIO_CRE_DDC_WR_INDEX:
                {
                    uint8_t scl = !!(val & 0x20);
                    uint8_t sda = !!(val & 0x10);
                    // Set an I2C GPIO register
                    i2c_gpio_set(nv4->nvbase.i2c, scl, sda);
                    break;
                }
                /* [6:0] contains cursorAddr [23:17] */
                case NV4_CIO_CRE_HCUR_ADDR0_INDEX:
                    nv4->pramdac.cursor_address |= (val & 0x7F) << 13; //bit7 technically ignored, but nv don't care, so neither do we
                    break;
                /* [7:2] contains cursorAddr [16:11] */
                case NV4_CIO_CRE_HCUR_ADDR1_INDEX:
                    nv4->pramdac.cursor_address |= (val & 0xFC) << 5; // bit0 and 1 aren't part of the address 
                    break;


            }

            /* Recalculate the timings if we actually changed them 
            Additionally only do it if the value actually changed*/
            if (old_val != val)
            {
                // Thx to Fuel who basically wrote most of the SVGA compatibility code already (although I fixed some issues), because VGA is boring 
                // and in the words of an ex-Rendition/3dfx/NVIDIA engineer, "VGA was basically an undocumented bundle of steaming you-know-what.   
                // And it was essential that any cores the PC 3D startups acquired had to work with all the undocumented modes and timing tweaks (mode X, etc.)"
                if (nv4->nvbase.svga.crtcreg < 0xE
                || nv4->nvbase.svga.crtcreg > 0x10)
                {
                    nv4->nvbase.svga.fullchange = changeframecount;
                    nv4_recalc_timings(&nv4->nvbase.svga);
                }
            }

            break;
        default:
            svga_out(addr, val, &nv4->nvbase.svga);
            break;
    }

}

/* DFB, sets up a dumb framebuffer */
uint8_t nv4_dfb_read8(uint32_t addr, void* priv)
{
    addr &= (nv4->nvbase.svga.vram_mask);
    return nv4->nvbase.svga.vram[addr];
}

uint16_t nv4_dfb_read16(uint32_t addr, void* priv)
{
    addr &= (nv4->nvbase.svga.vram_mask);
    return (nv4->nvbase.svga.vram[addr + 1] << 8) | nv4->nvbase.svga.vram[addr];
}

uint32_t nv4_dfb_read32(uint32_t addr, void* priv)
{
    addr &= (nv4->nvbase.svga.vram_mask);
    return (nv4->nvbase.svga.vram[addr + 3] << 24) | (nv4->nvbase.svga.vram[addr + 2] << 16) +
    (nv4->nvbase.svga.vram[addr + 1] << 8) | nv4->nvbase.svga.vram[addr];
}

void nv4_dfb_write8(uint32_t addr, uint8_t val, void* priv)
{
    addr &= (nv4->nvbase.svga.vram_mask);
    nv4->nvbase.svga.vram[addr] = val;
    nv4->nvbase.svga.changedvram[addr >> 12] = val;
    //nv4_render_current_bpp_dfb_8(addr);
}

void nv4_dfb_write16(uint32_t addr, uint16_t val, void* priv)
{
    addr &= (nv4->nvbase.svga.vram_mask);
    nv4->nvbase.svga.vram[addr + 1] = (val >> 8) & 0xFF;
    nv4->nvbase.svga.vram[addr] = (val) & 0xFF;
    nv4->nvbase.svga.changedvram[addr >> 12] = val;

    //nv4_render_current_bpp_dfb_16(addr);

}

void nv4_dfb_write32(uint32_t addr, uint32_t val, void* priv)
{
    addr &= (nv4->nvbase.svga.vram_mask);
    nv4->nvbase.svga.vram[addr + 3] = (val >> 24) & 0xFF;
    nv4->nvbase.svga.vram[addr + 2] = (val >> 16) & 0xFF;
    nv4->nvbase.svga.vram[addr + 1] = (val >> 8) & 0xFF;
    nv4->nvbase.svga.vram[addr] = (val) & 0xFF;
    nv4->nvbase.svga.changedvram[addr >> 12] = val;

    //removed until there is a render pipeline
    //nv4_render_current_bpp_dfb_32(addr);

}

/* Cursor shit */
void nv4_draw_cursor(svga_t* svga, int32_t drawline)
{
    // sanity check
    if (!nv4)
        return; 

    /*
    Commented out until we have a real graphics pipeline going here

    // if cursor disabled is set, return
    if ((nv4->nvbase.svga.crtc[NV4_CRTC_REGISTER_CURSOR_START] >> NV4_CRTC_REGISTER_CURSOR_START_DISABLED) & 0x01)
        return; 
    
    // NT GDI drivers: Load cursor using NV_IMAGE_FROM_MEMORY ("NV4LCD")
    // 9x GDI drivers: Use H/W cursor in RAMIN

    // Do we need to emulate it?

    // THIS IS CORRECT. BUT HOW DO WE FIND IT?
    uint32_t ramin_cursor_position = NV4_RAMIN_OFFSET_CURSOR;

    /* let's just assume buffer 0 here...that code needs to be totally rewritten
    nv4_coord_16_t start_position = nv4->pramdac.cursor_start;

    /* refuse to draw if thge cursor is offscreen  
    if (start_position.x >= nv4->nvbase.svga.hdisp
        || start_position.y >= nv4->nvbase.svga.dispend)
        {
            return;
        }

    nv_log("nv4_draw_cursor start=0x%04x,0x%04x", start_position.x, start_position.y);

    uint32_t final_position = nv4_render_get_vram_address_for_buffer(start_position, 0);
    
    uint16_t* vram_16 = (uint16_t*)nv4->nvbase.svga.vram;
    uint32_t* vram_32 = (uint32_t*)nv4->nvbase.svga.vram;
    
    /* 
        We have to get a 32x32, "A"1R5G5B5-format cursor 
        out of video memory. The alpha bit actually means - XOR with display pixel if 0, replace if 1

        These are expanded to RGB10 only if they are XORed. We don't do this (we don't really need to + there is no grobj specified here so special casing
        would be needed) so we just xor it with the current pixel format
    
    for (int32_t y = 0; y < NV4_PRAMDAC_CURSOR_SIZE_Y; y++)
    {
        for (int32_t x = 0; x < NV4_PRAMDAC_CURSOR_SIZE_X; x++)
        {
            uint16_t current_pixel = nv4_ramin_read16(ramin_cursor_position, nv4);

            // 0000 = transparent, so skip drawing
            if (current_pixel)
            {
                bool replace_bit = (current_pixel & 0x8000);
        
                // use buffer 0 BPIXEL
                uint32_t bpixel_format = (nv4->pgraph.bpixel[0]) & 0x03;

                switch (bpixel_format)
                {
                    case bpixel_fmt_8bit: 
                        if (replace_bit)
                            nv4->nvbase.svga.vram[final_position] = current_pixel;
                        else //xor
                        {
                            // not sure what to do here. we'd have to search through the palette to find the closest possible colour.
                            uint8_t final = current_pixel ^ nv4->nvbase.svga.vram[final_position];
                            nv4->nvbase.svga.vram[final_position] = final;
                        }
                    case bpixel_fmt_16bit:             // easy case (our cursor is 15bpp format)
                        uint32_t index_16 = final_position >> 1; 

                        if (replace_bit) // just replace
                            vram_16[index_16] = current_pixel;
                        else // xor
                        {
                            current_pixel &= ~0x8000;  // mask off the xor bit
                            uint16_t final = current_pixel ^ vram_16[index_16];
                            vram_16[index_16] = final;
                        }
                    case bpixel_fmt_32bit: 
                        uint32_t index_32 = final_position >> 2; 

                        if (replace_bit) // just replace    
                            vram_32[index_32] = nv4->nvbase.svga.conv_16to32(&nv4->nvbase.svga, current_pixel, 15); // 565_MODE doesn't seem to matter here
                        else //xor
                        {
                            current_pixel &= ~0x8000;  // mask off the xor bit
                            uint32_t current_pixel_32 = nv4->nvbase.svga.conv_16to32(&nv4->nvbase.svga, current_pixel, 15); // 565_MODE doesn't seem to matter here
                        
                            uint32_t final = current_pixel_32 ^ vram_32[index_32];
                            vram_32[index_32] = final;
                        }
                        break;  
                }
            }

            // increment vram position 
            ramin_cursor_position += 2; 

            // go
            switch (nv4->nvbase.svga.bpp)
            {
                case 8:
                    final_position++; 
                case 15 ... 16:
                    final_position += 2;
                    break;  
                case 32: 
                    final_position += 4; 
                    break;
            }

            start_position.x++; 
        }


        start_position.y++; 
        start_position.x = nv4->pramdac.cursor_start.x; 

        // reset at the end of each line so we "jump" to the start x
        final_position = nv4_render_get_vram_address_for_buffer(start_position, 0);
    }*/
}

// MMIO 0x110000->0x111FFF is mapped to a mirror of the VBIOS.
// Note this area is 64kb and the vbios is only 32kb. See below..

uint8_t nv4_prom_read(uint32_t address)
{
    // prom area is 64k, so...
    // first see if we even have a rom of 64kb in size
    uint32_t max_rom_size = NV4_PROM_END - NV4_PROM_START;
    uint32_t real_rom_size = max_rom_size;

    // set it
    if (nv4->nvbase.vbios.sz < max_rom_size)
        real_rom_size = nv4->nvbase.vbios.sz;

    //get our real address
    uint8_t rom_address = address & max_rom_size;

    // Does this mirror on real hardware?
    if (rom_address >= real_rom_size)
    {
        nv_log("PROM VBIOS Read to INVALID address 0x%05x, returning 0xFF", rom_address);
        return 0xFF;
    }
    else
    {
        uint8_t val = nv4->nvbase.vbios.rom[rom_address];
        nv_log("PROM VBIOS Read 0x%05x <- 0x%05x", val, rom_address);
        return val;
    }
}

void nv4_prom_write(uint32_t address, uint32_t value)
{
    uint32_t real_addr = address & 0x1FFFF;
    nv_log("What's going on here? Tried to write to the Video BIOS ROM? (Address=0x%05x, value=0x%02x)", real_addr, value);
}
