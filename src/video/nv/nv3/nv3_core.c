/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3 bringup and device emulation.
 *
 *
 * Authors: Connor Hyde, <mario64crashed@gmail.com> I need a better email address ;^)
 *          Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2024-2025 starfrost
 *          Copyright 2026 Marcos Alves
 */
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/io.h>
#include <86box/pci.h>
#include <86box/rom.h> // DEPENDENT!!!
#include <86box/plat_unused.h>
#include <86box/dma.h>
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>
#include "cpu.h"

/* Main device object pointer */
nv3_t* nv3;

/* These are a ****PLACEHOLDER**** and are copied from 3dfx VoodooBanshee/Voodoo3*/
static video_timings_t timing_nv3_pci = { .type = VIDEO_PCI, .write_b = 2, .write_w = 2, .write_l = 1, .read_b = 20, .read_w = 20, .read_l = 21 };
static video_timings_t timing_nv3_agp = { .type = VIDEO_AGP, .write_b = 2, .write_w = 2, .write_l = 1, .read_b = 20, .read_w = 20, .read_l = 21 };
// Revision C
static video_timings_t timing_nv3t_pci = { .type = VIDEO_PCI, .write_b = 2, .write_w = 2, .write_l = 1, .read_b = 20, .read_w = 20, .read_l = 21 };
static video_timings_t timing_nv3t_agp = { .type = VIDEO_AGP, .write_b = 2, .write_w = 2, .write_l = 1, .read_b = 20, .read_w = 20, .read_l = 21 };

// Prototypes for functions only used in this translation unit
void nv3_init_mappings_mmio(void);
void nv3_init_mappings_svga(void);
bool nv3_is_svga_redirect_address(uint32_t addr);

uint8_t nv3_svga_read(uint16_t addr, void* priv);
void nv3_svga_write(uint16_t addr, uint8_t val, void* priv);

// Determine if this address needs to be redirected to the SVGA subsystem.

bool nv3_is_svga_redirect_address(uint32_t addr)
{
    return (addr >= NV3_PRMVIO_START && addr <= NV3_PRMVIO_END)                    // VGA
    || (addr >= NV3_PRMCIO_START && addr <= NV3_PRMCIO_END)                       // CRTC
    || (addr >= NV3_USER_DAC_START && addr <= NV3_USER_DAC_END);                  // 6813c6-6813c9: the VGA DAC (CLUT) itself
}

/* Each of the three VGA windows decodes only its own registers (envytools nv_vga.xml:
   nv_vga_vio, nv_vga_cio, nv_vga_dio). The Windows 2000 miniport writes the sequencer
   index with a 32-bit store to PRMVIO 0x3C4; bytes 2-3 of it must not reach the DAC's
   pixel mask at 0x3C6, or every pixel of the desktop comes out black. */
static bool nv3_redirect_port_decoded(uint32_t addr)
{
    uint16_t port = addr & 0x3FF;

    if (addr >= NV3_PRMVIO_START && addr <= NV3_PRMVIO_END)
        return (port >= 0x3C2 && port <= 0x3C5) || port == 0x3CC || port == 0x3CE || port == 0x3CF;

    if (addr >= NV3_PRMCIO_START && addr <= NV3_PRMCIO_END)
        return port == 0x3B4 || port == 0x3B5 || port == 0x3BA || port == 0x3C0 || port == 0x3C1
            || port == 0x3C2 || port == 0x3CA || port == 0x3D4 || port == 0x3D5 || port == 0x3DA;

    return port >= 0x3C6 && port <= 0x3C9; // PRMDIO: the DAC
}

static uint8_t nv3_redirect_read(uint32_t addr)
{
    return nv3_redirect_port_decoded(addr) ? nv3_svga_read(addr & 0x3FF, nv3) : 0x00;
}

static void nv3_redirect_write(uint32_t addr, uint8_t val)
{
    if (nv3_redirect_port_decoded(addr))
        nv3_svga_write(addr & 0x3FF, val, nv3);
}

// All MMIO regs are 32-bit i believe internally
// so we have to do some munging to get this to read

// Read 8-bit MMIO
uint8_t nv3_mmio_read8(uint32_t addr, void* priv)
{
    uint32_t ret = 0x00;

    // Some of these addresses are Weitek VGA stuff and we need to mask it to this first because the weitek addresses are 8-bit aligned.
    addr &= 0xFFFFFF;

    if (nv3_is_svga_redirect_address(addr))
    {
        // svga writes are not logged anyway rn

        ret = nv3_redirect_read(addr);

        nv_log_verbose_only("Redirected MMIO read8 to SVGA: addr=0x%04x returned 0x%04x\n", addr, ret);

        return ret; 
    }

    // see if unaligned reads are a problem
    ret = nv3_mmio_read32(addr, priv);
    return (uint8_t)(ret >> ((addr & 3) << 3) & 0xFF);
}

// Read 16-bit MMIO
uint16_t nv3_mmio_read16(uint32_t addr, void* priv)
{
    uint32_t ret = 0x00;

    // Some of these addresses are Weitek VGA stuff and we need to mask it to this first because the weitek addresses are 8-bit aligned.
    addr &= 0xFFFFFF;

    if (nv3_is_svga_redirect_address(addr))
    {
        // svga writes are not logged anyway rn

        ret = nv3_redirect_read(addr)
        | (nv3_redirect_read(addr + 1) << 8);
        
        nv_log_verbose_only("Redirected MMIO read16 to SVGA: addr=0x%04x returned 0x%04x\n", addr, ret);

        return ret; 
    }

    ret = nv3_mmio_read32(addr, priv);
    return (uint16_t) (ret >> ((addr & 3) << 3));
}

// Read 32-bit MMIO
uint32_t nv3_mmio_read32(uint32_t addr, UNUSED(void* priv))
{
    uint32_t ret = 0x00;

    // Some of these addresses are Weitek VGA stuff and we need to mask it to this first because the weitek addresses are 8-bit aligned.
    addr &= 0xFFFFFF;

    if (nv3_is_svga_redirect_address(addr))
    {
        // svga writes are not logged anyway rn

        ret = nv3_redirect_read(addr)
        | (nv3_redirect_read(addr + 1) << 8)
        | (nv3_redirect_read(addr + 2) << 16)
        | (nv3_redirect_read(addr + 3) << 24);

        nv_log_verbose_only("Redirected MMIO read32 to SVGA: addr=0x%04x returned 0x%04x\n", addr, ret);

        return ret; 
    }

    ret = nv3_mmio_arbitrate_read(addr);
    return ret; 

}

/* A byte or word write goes out as a dword: the other bytes are the register's current value,
   except in the write-1-to-clear interrupt status registers, where writing back what reads as
   pending would acknowledge interrupts the driver never touched -- there they are 0 */
static uint32_t nv3_mmio_partial_base(uint32_t addr)
{
    switch (addr & 0xFFFFFC) {
        case NV3_PMC_INTR:
        case NV3_PBUS_INTR:
        case NV3_PFIFO_INTR:
        case NV3_PRM_INTR:
        case NV3_PTIMER_INTR:
        case NV3_PME_INTR:
        case NV3_PGRAPH_INTR_0:
        case NV3_PGRAPH_INTR_1:
        case NV3_PGRAPH_DMA_INTR_0:
        case NV3_PVIDEO_INTR:
            return 0;
        default:
            return nv3_mmio_read32(addr, NULL);
    }
}

// Write 8-bit MMIO
void nv3_mmio_write8(uint32_t addr, uint8_t val, void* priv)
{
    addr &= 0xFFFFFF;

    // This is weitek vga stuff
    // If we need to add more of these we can convert these to a switch statement
    if (nv3_is_svga_redirect_address(addr))
    {
        // svga writes are not logged anyway rn

        nv_log_verbose_only("Redirected MMIO write8 to SVGA: addr=0x%04x val=0x%02x\n", addr, val);

        nv3_redirect_write(addr, val & 0xFF);

        return; 
    }
    
    // overwrite first 8bits of a 32 bit value
    uint32_t new_val = nv3_mmio_partial_base(addr);

    new_val &= ~(0xFFu << ((addr & 3) << 3));
    new_val |= ((uint32_t) val << ((addr & 3) << 3));

    nv3_mmio_write32(addr, new_val, priv);
}

// Write 16-bit MMIO
void nv3_mmio_write16(uint32_t addr, uint16_t val, void* priv)
{
    addr &= 0xFFFFFF;

    // This is weitek vga stuff
    if (nv3_is_svga_redirect_address(addr))
    {
        // svga writes are not logged anyway rn

        nv_log_verbose_only("Redirected MMIO write16 to SVGA: addr=0x%04x val=0x%02x\n", addr, val);

        nv3_redirect_write(addr, val & 0xFF);
        nv3_redirect_write(addr + 1, (val >> 8) & 0xFF);
        
        return; 
    }

    // overwrite first 16bits of a 32 bit value
    uint32_t new_val = nv3_mmio_partial_base(addr);

    new_val &= ~(0xFFFFu << ((addr & 3) << 3));
    new_val |= ((uint32_t) val << ((addr & 3) << 3));

    nv3_mmio_write32(addr, new_val, priv);
}

// Write 32-bit MMIO
void nv3_mmio_write32(uint32_t addr, uint32_t val, UNUSED(void* priv))
{
    addr &= 0xFFFFFF;

    // This is weitek vga stuff
    if (nv3_is_svga_redirect_address(addr))
    {
        // svga writes are not logged anyway rn

        nv_log_verbose_only("Redirected MMIO write32 to SVGA: addr=0x%04x val=0x%02x\n", addr, val);

        nv3_redirect_write(addr, val & 0xFF);
        nv3_redirect_write(addr + 1, (val >> 8) & 0xFF);
        nv3_redirect_write(addr + 2, (val >> 16) & 0xFF);
        nv3_redirect_write(addr + 3, (val >> 24) & 0xFF);
        
        return; 
    }

    nv3_mmio_arbitrate_write(addr, val);
}

// AGP read function
uint8_t nv3_agp_read(UNUSED(int32_t func), int32_t addr)
{
    uint8_t ret = 0x00;

    switch (addr)
    {
        case NV3_AGP_CAPABILITIES_CAP_ID:
            ret = NV3_AGP_CAPABILITIES_CAP_ID_AGP;     // AGP capable device
            break;
        case NV3_AGP_CAPABILITIES_NEXT_PTR:             // the last capability
            ret = 0x00;
            break;
        case NV3_AGP_CAPABILITIES_AGP_VERSION:
            ret = (0x1 << NV3_AGP_CAPABILITIES_AGP_VERSION_MAJOR) | NV3_AGP_CAPABILITIES_AGP_VERSION_MINOR;
            break;
        case NV3_AGP_STATUS_RATE:
            // NV3T = AGP 2X, NV3 = AGP 1X
            if (nv3->nvbase.gpu_revision == NV3_PCI_CFG_REVISION_C00)
                ret = NV3_AGP_STATUS_RATE_1X_SUPPORTED | NV3_AGP_STATUS_RATE_2X_SUPPORTED;
            else
                ret = NV3_AGP_STATUS_RATE_1X_SUPPORTED;
            break;
        case NV3_AGP_STATUS_BYTE1:
            ret = 0x00;             // SBA not supported
            break;
        case NV3_AGP_STATUS_MAX_REQUESTS:
            ret = nv3->pbus.agp_rq;
            break;
        // This is also used for SBA but SBA is always off so we can use a bool
        case NV3_AGP_COMMAND_BYTE1:
            ret = nv3->nvbase.agp_enabled;
            break;
        default:
            ret = nv3->nvbase.pci_config.pci_regs[addr];
            break; 
    }

    return ret; 
}

// PCI stuff
// BAR0         Pointer to MMIO space
// BAR1         Pointer to Linear Framebuffer (NV_USER)

uint8_t nv3_pci_read(int32_t func, int32_t addr, UNUSED(int32_t len), UNUSED(void* priv))
{
    uint8_t ret = 0x00;

    // sanity check
    if (!nv3)
        return ret; 

    // figure out what size this gets read as first
    // seems func does not matter at least here?
    switch (addr) 
    {
        // Get the pci vendor id..

        case NV3_PCI_CFG_VENDOR_ID:
            ret = (PCI_VENDOR_SGS_NV & 0xFF);
            break;
        
        case NV3_PCI_CFG_VENDOR_ID + 1: // all access 8bit
            ret = (PCI_VENDOR_SGS_NV >> 8);
            break;

        // device id

        /* device id: the RIVA 128 ZX (revision C) is 0x0019 */
        case NV3_PCI_CFG_DEVICE_ID:
        case NV3_PCI_CFG_DEVICE_ID + 1:
        {
            uint16_t id = (nv3->nvbase.gpu_revision == NV3_PCI_CFG_REVISION_C00) ? NV_PCI_DEVICE_NV3T : NV_PCI_DEVICE_NV3;
            ret = (addr == NV3_PCI_CFG_DEVICE_ID) ? (id & 0xFF) : (id >> 8);
            break;
        }
        
        // various capabilities enabled by default 
        // IO space         enabled
        // Memory space     enabled
        // Bus master       enabled
        // Write/inval      enabled
        // Pal snoop        enabled
        // Capabiliies list enabled
        // 66Mhz FSB        capable

        case PCI_REG_COMMAND_L:
            ret = nv3->nvbase.pci_config.pci_regs[PCI_REG_COMMAND_L]; 
            break;
        
        case PCI_REG_COMMAND_H:
            ret = nv3->nvbase.pci_config.pci_regs[PCI_REG_COMMAND_H] & NV3_PCI_COMMAND_H_FAST_BACK2BACK; // always enable fast back2back
            break;

        // pci status register
        /* Status: 66MHZ follows the bus speed strap, CAP_LIST is set when
           strapped for AGP, DEVSEL timing is medium; the abort bits are clear (no aborts here) */
        case PCI_REG_STATUS_L:
            ret = 0x00;
            if ((nv3->straps >> NV3_PSTRAPS_BUS_SPEED) & 1)
                ret |= NV3_PCI_STATUS_L_66MHZ_CAPABLE;
            if (nv3->nvbase.bus_generation >= nv_bus_agp_1x)
                ret |= 0x10;
            break;

        case PCI_REG_STATUS_H:
            ret = 0x01 << 1; /* DEVSEL medium */
            break;
        
        case NV3_PCI_CFG_REVISION:
            ret = nv3->nvbase.gpu_revision; // Commercial release
            break;
       
        case PCI_REG_PROG_IF:
            ret = 0x00;
            break;
            
        case NV3_PCI_CFG_SUBCLASS_CODE:
            ret = 0x00; // nothing
            break;
        
        case NV3_PCI_CFG_CLASS_CODE:
            ret = NV3_PCI_CFG_CLASS_CODE_VGA; // CLASS_CODE_VGA 
            break;
        
        case NV3_PCI_CFG_CACHE_LINE_SIZE:
            ret = NV3_PCI_CFG_CACHE_LINE_SIZE_DEFAULT_FROM_VBIOS;
            break;
        
        case NV3_PCI_CFG_LATENCY_TIMER:
        case NV3_PCI_CFG_HEADER_TYPE:
        case NV3_PCI_CFG_BIST:
            ret = 0x00;
            break;

        // Both BARs are marked prefetchable
        case NV3_PCI_CFG_BAR0_L:
        case NV3_PCI_CFG_BAR1_L:
            // only bit that matters is bit 3 (prefetch bit)
            ret = (NV3_PCI_CFG_BAR_PREFETCHABLE_ENABLED << NV3_PCI_CFG_BAR_PREFETCHABLE);
            break;

        // These bits are hardwired to zero
        // Writes have no effect, we can just handle it here though
        case NV3_PCI_CFG_BAR0_BYTE1 ... NV3_PCI_CFG_BAR0_BYTE2:
        case NV3_PCI_CFG_BAR1_BYTE1 ... NV3_PCI_CFG_BAR1_BYTE2:
            ret = 0x00;
            break;

        // MMIO base address
        case NV3_PCI_CFG_BAR0_BASE_ADDRESS:
            ret = nv3->nvbase.bar0_mmio_base >> 24;//8bit value
            break; 

        case NV3_PCI_CFG_BAR1_BASE_ADDRESS:
            ret = nv3->nvbase.bar1_lfb_base >> 24; //8bit value
            break;

        /* Expansion ROM BAR: decode enable bit 0, ROM_BASE 31:22 -- a 4 MB
           window, so a BIOS sizing it with all ones reads back FFC00001. Bits 21:1 read 0: AMI
           BIOSes size the ROM from this and found none when 0x31/0x32 read back FF (1 short,
           3 long beeps); Award BIOSes don't size it */
        case NV3_PCI_CFG_ENABLE_VBIOS:
            ret = nv3->nvbase.pci_config.vbios_enabled;
            break;
        case NV3_PCI_CFG_ENABLE_VBIOS + 1:
            ret = 0x00;
            break;
        case NV3_PCI_CFG_VBIOS_BASE_L:
            ret = nv3->nvbase.pci_config.pci_regs[NV3_PCI_CFG_VBIOS_BASE_L] & 0xC0;
            break;
        
        case NV3_AGP_CAPABILITIES_POINTER:
            if (nv3->nvbase.bus_generation >= nv_bus_agp_1x)
                ret = NV3_AGP_CAPABILITIES_START;
            else 
                ret = 0x00;
            break; 

        case NV3_PCI_CFG_INT_LINE:
            ret = nv3->nvbase.pci_config.int_line;
            break;
        
        case NV3_PCI_CFG_INT_PIN:
            ret = PCI_INTA;
            break;

        case NV3_PCI_CFG_MIN_GRANT:
            ret = NV3_PCI_CFG_MIN_GRANT_DEFAULT;
            break;

        case NV3_PCI_CFG_MAX_LATENCY:
            ret = NV3_PCI_CFG_MAX_LATENCY_DEFAULT;
            break;

        //bar2-5 are not used and hardwired to 0
        case NV3_PCI_CFG_BAR_INVALID_START ... NV3_PCI_CFG_BAR_INVALID_END:
            ret = 0x00;
            break;
            
        case NV3_PCI_CFG_SUBSYSTEM_ID_MIRROR_START ... NV3_PCI_CFG_SUBSYSTEM_ID_MIRROR_END:
            ret = nv3->nvbase.pci_config.pci_regs[NV3_PCI_CFG_SUBSYSTEM_ID + (addr & 0x03)];
            break;

        case NV3_AGP_START ... NV3_AGP_END:
            if (nv3->nvbase.bus_generation < nv_bus_agp_1x)
                break;

            ret = nv3_agp_read(func, addr);

            break; 
        

        default: // by default just return pci_config.pci_regs
            ret = nv3->nvbase.pci_config.pci_regs[addr];
            break;
        
    }

    nv_log("nv3_pci_read func=0x%04x addr=0x%04x ret=0x%04x\n", func, addr, ret);
    return ret; 
}

void nv3_agp_write(UNUSED(int32_t func), int32_t addr, uint8_t val)
{
    nv3->nvbase.pci_config.pci_regs[addr] = val;

    switch (addr)
    {
        case NV3_AGP_COMMAND_BYTE1:
            nv3->nvbase.agp_enabled = val;
            break;
        default:  
            break;
    }
}

void nv3_pci_write(int32_t func, int32_t addr, UNUSED(int32_t len), uint8_t val, UNUSED(void* priv))
{
    // sanity check
    if (!nv3)
        return; 

    // some addresses are not writable so can't have any effect and can't be allowed to be modified using this code
    // as an example, only the most significant byte of the PCI BARs can be modified
    if ((addr >= NV3_PCI_CFG_BAR0_L && addr <= NV3_PCI_CFG_BAR0_BYTE2)
    || (addr >= NV3_PCI_CFG_BAR1_L && addr <= NV3_PCI_CFG_BAR1_BYTE2))
        return;

    /* The subsystem IDs at 0x2C-0x2F are read-only; they are written through 0x40-0x43 */
    if (addr >= NV3_PCI_CFG_SUBSYSTEM_ID && addr <= NV3_PCI_CFG_SUBSYSTEM_ID + 3)
        return;

    nv_log("nv3_pci_write func=0x%04x addr=0x%04x val=0x%04x\n", func, addr, val);

    nv3->nvbase.pci_config.pci_regs[addr] = val;

    switch (addr)
    {
        // standard pci command stuff
        case PCI_REG_COMMAND_L:
            nv3->nvbase.pci_config.pci_regs[PCI_REG_COMMAND_L] = val;
            // actually update the mappings
            nv3_update_mappings();
            break;
        case PCI_REG_COMMAND_H:
            nv3->nvbase.pci_config.pci_regs[PCI_REG_COMMAND_H] = val;
            // actually update the mappings
            nv3_update_mappings();          
            break;
        // pci status register
        /* Status is read-only here (the abort bits never get set) */
        case PCI_REG_STATUS_L:
        case PCI_REG_STATUS_H:
            break;
        case NV3_PCI_CFG_BAR0_BASE_ADDRESS:
            nv3->nvbase.bar0_mmio_base = val << 24;
            nv3_update_mappings();
            break; 
        case NV3_PCI_CFG_BAR1_BASE_ADDRESS:
            nv3->nvbase.bar1_lfb_base = val << 24;
            nv3_update_mappings();
            break;
        case NV3_PCI_CFG_ENABLE_VBIOS:
        case NV3_PCI_CFG_VBIOS_BASE:
            
            // make sure we are actually toggling the vbios, not the rom base
            if (addr == NV3_PCI_CFG_ENABLE_VBIOS)
                nv3->nvbase.pci_config.vbios_enabled = (val & 0x01);

            if (nv3->nvbase.pci_config.vbios_enabled)
            {
                // First see if we simply wanted to change the VBIOS location

                // Enable it in case it was disabled before
                mem_mapping_enable(&nv3->nvbase.vbios.mapping);

                if (addr != NV3_PCI_CFG_ENABLE_VBIOS)
                {
                    uint32_t old_addr = nv3->nvbase.vbios.mapping.base;
                    // 9bit register
                    uint32_t new_addr = nv3->nvbase.pci_config.pci_regs[NV3_PCI_CFG_VBIOS_BASE_H] << 24 |
                    (nv3->nvbase.pci_config.pci_regs[NV3_PCI_CFG_VBIOS_BASE_L] & 0xC0) << 16;

                    // move it
                    mem_mapping_set_addr(&nv3->nvbase.vbios.mapping, new_addr, 0x8000);

                    nv_log("...i like to move it move it (VBIOS Relocation) 0x%04x -> 0x%04x\n", old_addr, new_addr);

                }
                else
                {
                    nv_log("...VBIOS Enable\n");
                }
            }
            else
            {
                nv_log("...VBIOS Disable\n");
                mem_mapping_disable(&nv3->nvbase.vbios.mapping);

            }
            break;
        case NV3_PCI_CFG_INT_LINE:
            nv3->nvbase.pci_config.int_line = val;
            break;
        //bar2-5 are not used and can't be written to
        case NV3_PCI_CFG_BAR_INVALID_START ... NV3_PCI_CFG_BAR_INVALID_END:
            break;

        // these are mirrored to the subsystem id and also stored in the ROMBIOS
        case NV3_PCI_CFG_SUBSYSTEM_ID_MIRROR_START ... NV3_PCI_CFG_SUBSYSTEM_ID_MIRROR_END:
            nv3->nvbase.pci_config.pci_regs[NV3_PCI_CFG_SUBSYSTEM_ID + (addr & 0x03)] = val;
            break;

        case NV3_AGP_START ... NV3_AGP_END:
            if (nv3->nvbase.bus_generation < nv_bus_agp_1x)
                break;

            nv3_agp_write(func, addr, val);

            break; 
        
        default:
            break;
    }
}


//
// SVGA functions
//
extern uint32_t svga_conv_16to32(svga_t *svga, uint16_t color, uint8_t bpp); /* vid_svga.c */

/* 15/16 bpp through the CLUT as a gamma ramp (PRAMDAC_GENERAL_CONTROL IDC_MODE = gamma) */
static uint32_t
nv3_conv_16to32(svga_t *svga, uint16_t color, uint8_t bpp)
{
    return svga_lookup_lut_ram(svga, svga_conv_16to32(svga, color, bpp));
}

void nv3_recalc_timings(svga_t* svga)
{    
    // sanity check
    if (!nv3)
        return; 

    uint32_t pixel_mode = svga->crtc[NV3_CRTC_REGISTER_PIXELMODE] & 0x03;
    uint8_t  ext_vert   = svga->crtc[NV3_CRTC_REGISTER_FORMAT];     /* CR25 */
    uint8_t  ext_horz   = svga->crtc[NV3_CRTC_REGISTER_HEB];        /* CR2D */

    /* The extended CRTC bits, as Linux's rivafb (riva_hw.c) programs them:
       CR19 4:0 = start address 20:16 (in dwords), 7:5 = row offset 10:8 (in 8 bytes);
       CR25 = bit 10 of vtotal (0), display end (1), sync start (2), blank start (3);
       CR2D = bit 8 of htotal (0), display end (1), blank start (2), sync start (3). */
    svga->memaddr_latch += (svga->crtc[NV3_CRTC_REGISTER_RPC0] & 0x1F) << 16;
    svga->rowoffset     += (svga->crtc[NV3_CRTC_REGISTER_RPC0] & 0xE0) << 3;
    /* CR25 and CR1A: CR25 bit 5 = row offset bit 11;
       CR1A bit 7/6 = hsync/vsync disabled (DPMS standby/suspend/off), bit 0 = VGA address wrap
       (the CRTC fetches from the first 256 KB only; set by the BIOS for the VGA modes) */
    if (ext_vert & 0x20)
        svga->rowoffset += 0x800;
    /* CR1A bit 2 clear (screens of 1280x1024 and over) enables bit 6 of the horizontal blank end,
       CR25 bit 4 */
    if (!(svga->crtc[NV3_CRTC_REGISTER_RPC1] & 0x04)) {
        svga->hblank_end_val |= (ext_vert & 0x10) << 2;
        svga->hblank_end_mask = 0x7F;
    }
    svga->dpms              = !!(svga->crtc[NV3_CRTC_REGISTER_RPC1] & 0xC0);
    svga->vram_display_mask = ((pixel_mode == NV3_CRTC_REGISTER_PIXELMODE_VGA) && (svga->crtc[NV3_CRTC_REGISTER_RPC1] & 0x01))
                            ? (svga->vram_mask & 0x3FFFF) : svga->vram_mask;

    if (ext_vert & 0x01)
        svga->vtotal += 0x400;
    if (ext_vert & 0x02)
        svga->dispend += 0x400;
    if (ext_vert & 0x04)
        svga->vsyncstart += 0x400;
    if (ext_vert & 0x08)
        svga->vblankstart += 0x400;
    if (ext_horz & 0x01)
        svga->htotal += 0x100;
    if (ext_horz & 0x02) {
        svga->hdisp += 0x100 * svga->dots_per_clock;
        svga->hdisp_time += 0x100;
    }
    if (ext_horz & 0x04)
        svga->hblankstart += 0x100;

    /* Line compare has no bit 10 here (CR25/CR2D extend everything else), so the drivers' "no
       split" value 0x3FF would restart the scan-out at line 1024 and repeat the top of a
       1600x1200 screen; at its maximum it never cuts a taller screen */
    if ((svga->split == 0x400) && (svga->dispend > 0x400))
        svga->split = 0x7FFFFFFF;

    /* Horizontal blank end is compared from the character after blank start, so an end equal to
       the start in the compared bits blanks a whole 64 (or 128) characters -- 1600x1200 sets
       start 0xC7, end 0x07. The SVGA core would match at once and make a 512-pixel overscan:
       give it the end with the 7-bit compare (bit 7 makes the core use the mask). */
    {
        uint32_t mask = (svga->hblank_end_val & ~0x3F) ? svga->hblank_end_mask : 0x3F;

        if (((svga->hblankstart ^ svga->hblank_end_val) & mask) == 0) {
            svga->hblank_end_val  = 0x80 | ((svga->hblankstart + mask + 1) & 0x7F);
            svga->hblank_end_mask = 0x7F;
        }
    }

    /* The CRTC scans VRAM out linearly from the start address with the CRTC pitch,
       so the SVGA core draws the screen; PGRAPH and the linear framebuffer only mark
       what they change in changedvram. */
    svga->override = 0;

    /* The extended modes scan VRAM out linearly, so CPU accesses through the banked A0000 window
       in chain-4 must be linear too; only VGA mode 13h keeps the planar chain-4 layout its
       doubleword scan-out reads. (Packed modes drawn through the window by DOS VESA programs came
       out as 4-pixel slivers every 16 pixels.) */
    svga->packed_chain4 = (pixel_mode != NV3_CRTC_REGISTER_PIXELMODE_VGA);

    switch (pixel_mode)
    {
        case NV3_CRTC_REGISTER_PIXELMODE_8BPP:
            svga->bpp    = 8;
            svga->lowres = 0;
            svga->map8   = svga->pallook;
            svga->render = svga_render_8bpp_highres;
            break;
        case NV3_CRTC_REGISTER_PIXELMODE_16BPP:
            /* The RIVA 128 scans 16-bit modes out as X1R5G5B5; the PRAMDAC's
               alternate-mode bit selects R5G6B5. */
            svga->lowres = 0;
            if ((nv3->pramdac.general_control >> NV3_PRAMDAC_GENERAL_CONTROL_565_MODE) & 0x01) {
                svga->bpp    = 16;
                svga->render = svga_render_16bpp_highres;
            } else {
                svga->bpp    = 15;
                svga->render = svga_render_15bpp_highres;
            }
            break;
        case NV3_CRTC_REGISTER_PIXELMODE_32BPP:
            svga->bpp    = 32;
            svga->lowres = 0;
            svga->render = svga_render_32bpp_highres;
            break;
        default:
            /* VGA: everything above is the SVGA core's, but it chose the renderer with the
               bpp of the last extended mode (Windows' 256-colour shutdown screen came out
               as 32 bpp after a 32-bit desktop) -- VGA has 8 bits per pixel at most */
            if (svga->bpp > 8) {
                svga->bpp = 8;
                if (!svga->attr_palette_enable || !((svga->gdcreg[6] & 1) || (svga->attrregs[0x10] & 1))
                    || ((svga->gdcreg[5] & 0x60) < 0x40))
                    break; /* blanked, text, or 16/4 colours: the core got those right */
                svga->map8   = svga->pallook;
                svga->render = svga->lowres ? svga_render_8bpp_lowres : svga_render_8bpp_highres;
            }
            break;
    }

    /* Direct colour modes go through the CLUT as a per-channel gamma ramp unless
       PRAMDAC_GENERAL_CONTROL IDC_MODE selects index (it resets to gamma). The resman sets
       index at 8 bpp and gamma at 16/32 bpp after loading the CLUT with an identity ramp, so
       apps' gamma ramps take effect. */
    {
        bool gamma = (pixel_mode >= NV3_CRTC_REGISTER_PIXELMODE_16BPP)
                  && !((nv3->pramdac.general_control >> NV3_PRAMDAC_GENERAL_CONTROL_IDC_MODE) & 1);
        svga->lut_map     = gamma;
        svga->conv_16to32 = gamma ? nv3_conv_16to32 : svga_conv_16to32;
    }

    /* Hardware cursor: CR31 bit 0 shows it; the image is 32x32 A1R5G5B5 in instance memory */
    svga->hwcursor.ena       = (svga->crtc[NV3_CRTC_REGISTER_CURSOR_ADDR1] & 0x01) && (pixel_mode != NV3_CRTC_REGISTER_PIXELMODE_VGA);
    svga->hwcursor.cur_xsize = NV3_PRAMDAC_CURSOR_SIZE_X;
    svga->hwcursor.cur_ysize = NV3_PRAMDAC_CURSOR_SIZE_Y;
    svga->hwcursor.x         = ((int32_t) nv3->pramdac.cursor_start.x ^ 0x800) - 0x800;
    svga->hwcursor.y         = ((int32_t) nv3->pramdac.cursor_start.y ^ 0x800) - 0x800;
    svga->hwcursor.xoff      = (svga->hwcursor.x < 0) ? -svga->hwcursor.x : 0;
    svga->hwcursor.yoff      = (svga->hwcursor.y < 0) ? -svga->hwcursor.y : 0;
    svga->hwcursor.addr      = ((svga->crtc[NV3_CRTC_REGISTER_CURSOR_ADDR0] & 0x7F) << 16)
                             | ((svga->crtc[NV3_CRTC_REGISTER_CURSOR_ADDR1] & 0xF8) << 8);
    svga->hwcursor.addr     += svga->hwcursor.yoff * NV3_PRAMDAC_CURSOR_SIZE_X * 2;

    // from nv_riva128
    if (((svga->miscout >> 2) & 2) == 2)
    {
        // set clocks
        nv3_pramdac_set_pixel_clock();
        nv3_pramdac_set_core_clock();
    }
}

void nv3_speed_changed(UNUSED(void* priv))
{
    // sanity check
    if (!nv3)
        return; 
        
    svga_recalctimings(&nv3->nvbase.svga);
}

// Force Redraw
// Reset etc.
void nv3_force_redraw(UNUSED(void* priv))
{
    // sanity check
    if (!nv3)
        return; 

    nv3->nvbase.svga.fullchange = changeframecount; 
}

// Read from SVGA core memory
uint8_t nv3_svga_read(uint16_t addr, void* priv)
{
    nv3_t* nv3 = (nv3_t*)priv;

    uint8_t ret = 0x00;

    // sanity check
    if (!nv3)
        return ret; 

    // If we need to RMA from GPU MMIO, go do that
    if (addr >= NV3_RMA_REGISTER_START
    && addr <= NV3_RMA_REGISTER_END)
    {
        if (!(nv3->pbus.rma.mode & 0x01))
            return ret;

        // must be dword aligned
        uint32_t real_rma_read_addr = (((nv3->pbus.rma.mode & NV3_CRTC_REGISTER_RMA_MODE_MAX) - 1) << 1) + (addr & 0x03); 
        ret = nv3_pbus_rma_read(real_rma_read_addr);
        return ret;
    }

    // mask off b0/d0 registers 
    if ((((addr & 0xFFF0) == 0x3D0 
    || (addr & 0xFFF0) == 0x3B0) && addr < 0x3de) 
    && !(nv3->nvbase.svga.miscout & 1))
        addr ^= 0x60;

    switch (addr)
    {
        // Alias for "get current SVGA CRTC register ID"
        case NV3_CRTC_REGISTER_INDEX:
            ret = nv3->nvbase.svga.crtcreg;
            break;
        case NV3_CRTC_REGISTER_WTF:
            ret = 0x08; // Required to not freeze in certain situations on v3.xx drivers. Even though this register doesn't actually exist lol
            break; 
        case NV3_CRTC_REGISTER_CURRENT:
            // Support the extended NVIDIA CRTC register range
            switch (nv3->nvbase.svga.crtcreg)
            {
                case NV3_CRTC_REGISTER_RL0:
                    ret = nv3->nvbase.svga.displine & 0xFF; 
                    break;
                    /* Is rl1?*/
                case NV3_CRTC_REGISTER_RL1:
                    ret = (nv3->nvbase.svga.displine >> 8) & 7;
                    break;
                case NV3_CRTC_REGISTER_I2C:
                    ret = i2c_gpio_get_sda(nv3->nvbase.i2c) << 3
                    | i2c_gpio_get_scl(nv3->nvbase.i2c) << 2;

                    break;
                default:
                    ret = nv3->nvbase.svga.crtc[nv3->nvbase.svga.crtcreg];
            }
            break;
        default:
            ret = svga_in(addr, &nv3->nvbase.svga);
            break;
    }

    return ret; //TEMP
}

// Write to SVGA core memory
void nv3_svga_write(uint16_t addr, uint8_t val, UNUSED(void* priv))
{
    // sanity check
    if (!nv3)
        return; 

    // If we need to RMA to GPU MMIO, go do that
    if (addr >= NV3_RMA_REGISTER_START
    && addr <= NV3_RMA_REGISTER_END)
    {
        // we don't need to store these registers...
        nv3->pbus.rma.rma_regs[addr & 3] = val;

        if (!(nv3->pbus.rma.mode & 0x01)) // we are halfway through sending something
            return;

        uint32_t real_rma_write_addr = ((nv3->pbus.rma.mode & (NV3_CRTC_REGISTER_RMA_MODE_MAX - 1)) << 1) + (addr & 0x03); 

        nv3_pbus_rma_write(real_rma_write_addr, nv3->pbus.rma.rma_regs[addr & 3]);
        return;
    }

    // mask off b0/d0 registers 
    if ((((addr & 0xFFF0) == 0x3D0 || (addr & 0xFFF0) == 0x3B0) 
    && addr < 0x3de) 
    && !(nv3->nvbase.svga.miscout & 1))//miscout bit 7 controls mappping
        addr ^= 0x60;

    uint8_t crtcreg = nv3->nvbase.svga.crtcreg;
    uint8_t old_value = 0x00;

    /* A synchronous sequencer reset (every mode set) restarts the cursor/attribute blink count, so
       a text screen's blink phase follows its mode set: NVIDIA's manufacturing test checksums its
       text modes with the cursor in its off half and fails at random otherwise */
    if ((addr == 0x3C5) && ((nv3->nvbase.svga.seqaddr & 0x0F) == 0) && !(val & 0x02))
        nv3->nvbase.svga.blink = 0;

    // todo:
    // Pixel formats (8bit vs 555 vs 565)
    // VBE 3.0?
    
    switch (addr)
    {
        case NV3_CRTC_REGISTER_INDEX:
            // real mode access to GPU MMIO space...
            nv3->nvbase.svga.crtcreg = val;
            break;
        // support the extended crtc regs and debug this out
        case NV3_CRTC_REGISTER_CURRENT:

            // Implements the VGA Protect register
            if ((nv3->nvbase.svga.crtcreg < NV3_CRTC_REGISTER_OVERFLOW) && (nv3->nvbase.svga.crtc[0x11] & 0x80))
                return;

            // Ignore certain bits when VGA Protect register set and we are writing to CRTC register=07h
            if ((nv3->nvbase.svga.crtcreg == NV3_CRTC_REGISTER_OVERFLOW) && (nv3->nvbase.svga.crtc[0x11] & 0x80))
                val = (nv3->nvbase.svga.crtc[NV3_CRTC_REGISTER_OVERFLOW] & ~0x10) | (val & 0x10);

            // set the register value...
            old_value = nv3->nvbase.svga.crtc[crtcreg];

            nv3->nvbase.svga.crtc[crtcreg] = val;
            // ...now act on it

            /* CR1D/CR1E, the page registers, count 32K of CPU address in every mode. In planar
               mode a CPU byte spans all four planes, so that is 32K of plane address -- the SVGA
               core adds the bank before it spreads the address over the planes. (It used to be 8K
               in planar modes, so VESA planar modes past 64K per plane -- 1024x768 and 1280x1024
               in 16 colours -- drew over their own top.) */
            switch (crtcreg)
            {
                case NV3_CRTC_REGISTER_READ_BANK:
                    nv3->nvbase.cio_read_bank = val;
                    nv3->nvbase.svga.read_bank = nv3->nvbase.cio_read_bank << 15;
                    break;
                case NV3_CRTC_REGISTER_WRITE_BANK:
                    nv3->nvbase.cio_write_bank = val;
                    nv3->nvbase.svga.write_bank = nv3->nvbase.cio_write_bank << 15;
                    break;
                case NV3_CRTC_REGISTER_RMA:
                    nv3->pbus.rma.mode = val & NV3_CRTC_REGISTER_RMA_MODE_MAX;
                    break;
                case NV3_CRTC_REGISTER_I2C_GPIO:
                {
                    uint8_t scl = !!(val & 0x20);
                    uint8_t sda = !!(val & 0x10);
                    // Set an I2C GPIO register
                    i2c_gpio_set(nv3->nvbase.i2c, scl, sda);
                    break;
                }
            }

            /* Recalculate the timings if we actually changed them 
            Additionally only do it if the value actually changed*/
            if (old_value != val)
            {
                // Thx to Fuel who basically wrote most of the SVGA compatibility code already (although I fixed some issues), because VGA is boring 
                // and in the words of an ex-Rendition/3dfx/NVIDIA engineer, "VGA was basically an undocumented bundle of steaming you-know-what.   
                // And it was essential that any cores the PC 3D startups acquired had to work with all the undocumented modes and timing tweaks (mode X, etc.)"
                if (nv3->nvbase.svga.crtcreg < 0xE
                || nv3->nvbase.svga.crtcreg > 0x10)
                {
                    /* the whole recalculation: nv3_recalc_timings() adds the extended bits to the
                       core's start address and row offset, so it must not run on its own */
                    nv3->nvbase.svga.fullchange = changeframecount;
                    svga_recalctimings(&nv3->nvbase.svga);
                }
            }

            break;
        default:
            svga_out(addr, val, &nv3->nvbase.svga);
            break;
    }

}

/* DFB, sets up a dumb framebuffer */
uint8_t nv3_dfb_read8(uint32_t addr, UNUSED(void* priv))
{
    addr &= (nv3->nvbase.svga.vram_mask);
    return nv3->nvbase.svga.vram[addr];
}

uint16_t nv3_dfb_read16(uint32_t addr, UNUSED(void* priv))
{
    addr &= (nv3->nvbase.svga.vram_mask);
    return (nv3->nvbase.svga.vram[addr + 1] << 8) | nv3->nvbase.svga.vram[addr];
}

uint32_t nv3_dfb_read32(uint32_t addr, UNUSED(void* priv))
{
    addr &= (nv3->nvbase.svga.vram_mask);
    return (nv3->nvbase.svga.vram[addr + 3] << 24) | (nv3->nvbase.svga.vram[addr + 2] << 16) |
    (nv3->nvbase.svga.vram[addr + 1] << 8) | nv3->nvbase.svga.vram[addr];
}

void nv3_dfb_write8(uint32_t addr, uint8_t val, UNUSED(void* priv))
{
    addr &= (nv3->nvbase.svga.vram_mask);
    nv3->nvbase.svga.vram[addr] = val;
    nv3->nvbase.svga.changedvram[addr >> 12] = changeframecount;
}

void nv3_dfb_write16(uint32_t addr, uint16_t val, UNUSED(void* priv))
{
    addr &= (nv3->nvbase.svga.vram_mask);
    nv3->nvbase.svga.vram[addr + 1] = (val >> 8) & 0xFF;
    nv3->nvbase.svga.vram[addr] = (val) & 0xFF;
    nv3->nvbase.svga.changedvram[addr >> 12] = changeframecount;
}

void nv3_dfb_write32(uint32_t addr, uint32_t val, UNUSED(void* priv))
{
    addr &= (nv3->nvbase.svga.vram_mask);
    nv3->nvbase.svga.vram[addr + 3] = (val >> 24) & 0xFF;
    nv3->nvbase.svga.vram[addr + 2] = (val >> 16) & 0xFF;
    nv3->nvbase.svga.vram[addr + 1] = (val >> 8) & 0xFF;
    nv3->nvbase.svga.vram[addr] = (val) & 0xFF;
    nv3->nvbase.svga.changedvram[addr >> 12] = changeframecount;
}

/* Hardware cursor, called by the SVGA core for each of its lines.
   32x32 A1R5G5B5 in instance memory at CR30 6:0 (address 22:16) and CR31 7:3
   (address 15:11); Linux's rivafb puts it at PRAMIN 0x7800 (CR31 = 0x78 | show).
   A set alpha bit replaces the screen pixel, a clear one XORs it (0 = transparent). */
void nv3_draw_cursor(svga_t* svga, int32_t drawline)
{
    if (!nv3)
        return;

    uint32_t *line  = svga->monitor->target_buffer->line[drawline];
    uint32_t  addr  = svga->hwcursor_latch.addr;
    int32_t   x_pos = svga->hwcursor_latch.x + svga->x_add;

    for (int32_t x = 0; x < NV3_PRAMDAC_CURSOR_SIZE_X; x++, addr += 2)
    {
        uint16_t pixel = nv3_ramin_read16(addr, nv3);
        int32_t  px    = x_pos + x;

        if ((px < 0) || (px > 2047))
            continue;

        uint32_t rgb = ((pixel & 0x7C00) << 9) | ((pixel & 0x7000) << 4)
                     | ((pixel & 0x03E0) << 6) | ((pixel & 0x0380) << 1)
                     | ((pixel & 0x001F) << 3) | ((pixel & 0x001C) >> 2);

        if (pixel & 0x8000)
            line[px] = rgb;
        else
            line[px] ^= rgb;
    }

    svga->hwcursor_latch.addr += NV3_PRAMDAC_CURSOR_SIZE_X * 2;
}

/* A dword of the VBIOS through the PROM window (0x110000-0x11FFFF, 64 KB; the image is 32 KB):
   the arbiter hands over the dword-aligned address and nv3_mmio_read8/16 pick the bytes out, so
   all four come back (only one used to, and the offset was kept in a uint8_t: the first 256 bytes,
   repeated). Past the end of the image the window reads 0xFF. */
uint32_t nv3_prom_read(uint32_t address)
{
    uint32_t offset = (address - NV3_PROM_START) & ~3u;
    uint32_t size   = (uint32_t) nv3->nvbase.vbios.sz;
    uint32_t ret    = 0;

    for (uint32_t i = 0; i < 4; i++) {
        uint8_t val = 0xFF;
        if (nv3->nvbase.vbios.rom && (offset + i < size))
            val = nv3->nvbase.vbios.rom[offset + i];
        ret |= (uint32_t) val << (i * 8);
    }
    return ret;
}

void nv3_prom_write(uint32_t address, uint32_t value)
{
    uint32_t real_addr = address & 0x1FFFF;
    nv_log("What's going on here? Tried to write to the Video BIOS ROM? (Address=0x%05x, value=0x%02x)", real_addr, value);
}

// Initialise the MMIO mappings
void nv3_init_mappings_mmio(void)
{
    nv_log("Initialising MMIO mapping\n");

    // 0x0 - 1000000: regs
    // 0x1000000-2000000

    // initialize the mmio mapping
    mem_mapping_add(&nv3->nvbase.mmio_mapping, 0, 0, 
        nv3_mmio_read8,
        nv3_mmio_read16,
        nv3_mmio_read32,
        nv3_mmio_write8,
        nv3_mmio_write16,
        nv3_mmio_write32,
        NULL, MEM_MAPPING_EXTERNAL, nv3);
    
    // initialize the mmio mapping
    mem_mapping_add(&nv3->nvbase.ramin_mapping, 0, 0, 
        nv3_ramin_read8,
        nv3_ramin_read16,
        nv3_ramin_read32,
        nv3_ramin_write8,
        nv3_ramin_write16,
        nv3_ramin_write32,
        NULL, MEM_MAPPING_EXTERNAL, nv3);

    mem_mapping_add(&nv3->nvbase.ramin_mapping_mirror, 0, 0,
        nv3_ramin_read8,
        nv3_ramin_read16,
        nv3_ramin_read32,
        nv3_ramin_write8,
        nv3_ramin_write16,
        nv3_ramin_write32,
        NULL, MEM_MAPPING_EXTERNAL, nv3);

}

void nv3_init_mappings_svga(void)
{
    nv_log("Initialising SVGA core memory mapping\n");
    // setup the svga mappings
    mem_mapping_add(&nv3->nvbase.framebuffer_mapping, 0, 0,
        nv3_dfb_read8,
        nv3_dfb_read16,
        nv3_dfb_read32,
        nv3_dfb_write8,
        nv3_dfb_write16,
        nv3_dfb_write32,
        nv3->nvbase.svga.vram, 0, &nv3->nvbase.svga);

    // the SVGA/LFB mapping is also mirrored
    mem_mapping_add(&nv3->nvbase.framebuffer_mapping_mirror, 0, 0, 
        nv3_dfb_read8,
        nv3_dfb_read16,
        nv3_dfb_read32,
        nv3_dfb_write8,
        nv3_dfb_write16,
        nv3_dfb_write32,
        nv3->nvbase.svga.vram, 0, &nv3->nvbase.svga);

    io_sethandler(0x03c0, 0x0020, 
    nv3_svga_read, NULL, NULL, 
    nv3_svga_write, NULL, NULL, 
    nv3);
}

void nv3_init_mappings(void)
{
    nv3_init_mappings_mmio();
    nv3_init_mappings_svga();
}

// Updates the mappings after initialisation. 
void nv3_update_mappings(void)
{
    // sanity check
    if (!nv3)
        return; 


    nv_log("\nMemory Mapping Config Change:\n");

    (nv3->nvbase.pci_config.pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO) ? nv_log("Enable I/O\n") : nv_log("Disable I/O\n");

    io_removehandler(0x03c0, 0x0020, 
        nv3_svga_read, NULL, NULL, 
        nv3_svga_write, NULL, NULL, 
        nv3);

    if (nv3->nvbase.pci_config.pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO)
        io_sethandler(0x03c0, 0x0020, 
        nv3_svga_read, NULL, NULL, 
        nv3_svga_write, NULL, NULL, 
        nv3);   
    
    /* With memory decode off the card answers no memory cycles, the VGA window included (Windows
       turns both decodes off on a display adapter it has no driver for; drawing on through the
       window with the VGA registers gone garbled the screen) */
    if (!(nv3->nvbase.pci_config.pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_MEM))
    {
        nv_log("The memory was turned off, not much is going to happen.\n");
        mem_mapping_disable(&nv3->nvbase.svga.mapping);
        mem_mapping_disable(&nv3->nvbase.mmio_mapping);
        mem_mapping_disable(&nv3->nvbase.framebuffer_mapping);
        mem_mapping_disable(&nv3->nvbase.framebuffer_mapping_mirror);
        mem_mapping_disable(&nv3->nvbase.ramin_mapping);
        mem_mapping_disable(&nv3->nvbase.ramin_mapping_mirror);
        return;
    }

    // turn off bar0 and bar1 by defualt
    mem_mapping_disable(&nv3->nvbase.mmio_mapping);
    mem_mapping_disable(&nv3->nvbase.framebuffer_mapping);
    mem_mapping_disable(&nv3->nvbase.framebuffer_mapping_mirror);
    mem_mapping_disable(&nv3->nvbase.ramin_mapping);
    mem_mapping_disable(&nv3->nvbase.ramin_mapping_mirror);

    // Setup BAR0 (MMIO)

    nv_log("BAR0 (MMIO Base) = 0x%08x\n", nv3->nvbase.bar0_mmio_base);
    
    if (nv3->nvbase.bar0_mmio_base)
        mem_mapping_set_addr(&nv3->nvbase.mmio_mapping, nv3->nvbase.bar0_mmio_base, NV3_MMIO_SIZE);

    // if this breaks anything, remove it
    nv_log("BAR1 (Linear Framebuffer / NV_USER Base & RAMIN) = 0x%08x\n", nv3->nvbase.bar1_lfb_base);

    // this is likely mirrored 4x on 2mb cards, 2x on 4mb cards, and not at all on 8mb cards

    // 1MB was never used, 2MB only used once. Do we really need it?


    // 4MB VRAM memory map:
    // LFB_BASE+VRAM_SIZE=RAMIN Mirror(?)                                                   0x1400000 (VERIFY PCBOX)
    // LFB_BASE+VRAM_SIZE*2=LFB Mirror(?)                                                   0x1800000            
    // LFB_BASE+VRAM_SIZE*3=Definitely RAMIN (then it ends, the total ram space is 16mb)    0x1C00000

    // 8MB VRAM memory map:
    // LFB_BASE->LFB_BASE+VRAM_SIZE=LFB
    // What is in 800000-c00000? (Partial mirror )
    // LFB_BASE+0xC00000 = RAMIN

    if (nv3->nvbase.bar1_lfb_base)
    {
        if (nv3->nvbase.vram_amount == NV3_VRAM_SIZE_4MB || nv3->nvbase.vram_amount == NV3_VRAM_SIZE_2MB)
        {    
            /* 2MB (NEC G7AGK): the 4MB layout with the framebuffer and its mirror 2MB long */
            mem_mapping_set_addr(&nv3->nvbase.framebuffer_mapping, nv3->nvbase.bar1_lfb_base, nv3->nvbase.vram_amount);
            mem_mapping_set_addr(&nv3->nvbase.ramin_mapping_mirror, nv3->nvbase.bar1_lfb_base + NV3_LFB_RAMIN_MIRROR_START, NV3_LFB_MAPPING_SIZE);
            mem_mapping_set_addr(&nv3->nvbase.framebuffer_mapping_mirror, nv3->nvbase.bar1_lfb_base + NV3_LFB_MIRROR_START, nv3->nvbase.vram_amount);
            mem_mapping_set_addr(&nv3->nvbase.ramin_mapping, nv3->nvbase.bar1_lfb_base + NV3_LFB_RAMIN_START, NV3_LFB_MAPPING_SIZE);
        }
        else if (nv3->nvbase.vram_amount == NV3_VRAM_SIZE_8MB)
        {
            // we don't need this one in the case of 8mb, because regular mapping is 8mb
            mem_mapping_disable(&nv3->nvbase.ramin_mapping_mirror);
            mem_mapping_set_addr(&nv3->nvbase.framebuffer_mapping, nv3->nvbase.bar1_lfb_base, NV3_VRAM_SIZE_8MB);
            mem_mapping_set_addr(&nv3->nvbase.framebuffer_mapping_mirror, nv3->nvbase.bar1_lfb_base + NV3_LFB_MIRROR_START, NV3_LFB_MAPPING_SIZE);
            mem_mapping_set_addr(&nv3->nvbase.ramin_mapping, nv3->nvbase.bar1_lfb_base + NV3_LFB_RAMIN_START, NV3_LFB_MAPPING_SIZE);
        }
    }

    // Did we change the banked SVGA mode?
    switch (nv3->nvbase.svga.gdcreg[0x06] & 0x0c)
    {
        case NV3_CRTC_BANKED_128K_A0000:
            nv_log("SVGA Banked Mode = 128K @ A0000h\n");
            mem_mapping_set_addr(&nv3->nvbase.svga.mapping, 0xA0000, 0x20000); // 128kb @ 0xA0000
            nv3->nvbase.svga.banked_mask = 0x1FFFF;
            break;
        case NV3_CRTC_BANKED_64K_A0000:
            nv_log("SVGA Banked Mode = 64K @ A0000h\n");
            mem_mapping_set_addr(&nv3->nvbase.svga.mapping, 0xA0000, 0x10000); // 64kb @ 0xA0000
            nv3->nvbase.svga.banked_mask = 0xFFFF;
            break;
        case NV3_CRTC_BANKED_32K_B0000:
            nv_log("SVGA Banked Mode = 32K @ B0000h\n");
            mem_mapping_set_addr(&nv3->nvbase.svga.mapping, 0xB0000, 0x8000); // 32kb @ 0xB0000
            nv3->nvbase.svga.banked_mask = 0x7FFF;
            break;
        case NV3_CRTC_BANKED_32K_B8000:
            nv_log("SVGA Banked Mode = 32K @ B8000h\n");
            mem_mapping_set_addr(&nv3->nvbase.svga.mapping, 0xB8000, 0x8000); // 32kb @ 0xB8000
            nv3->nvbase.svga.banked_mask = 0x7FFF;
            break;
    }
}

// 
// Init code
//
void* nv3_init(UNUSED(const device_t *info))
{
    // set the vram amount and gpu revision
    
    /* We don't bother looking these up if they are nonzero. On Riva 128 ZX, they are already set by the init function (always 8 MB VRAM + Revision C0) */
    if (!nv3->nvbase.vram_amount)
        nv3->nvbase.vram_amount = device_get_config_int("vram_size");

    if (!nv3->nvbase.gpu_revision)
        nv3->nvbase.gpu_revision = device_get_config_int("chip_revision");

    /* Set log device name based on card model */
    const char* log_device_name = "NV3";

    if (device_get_config_int("nv_debug_fulllog"))
        nv3->nvbase.log = log_open(log_device_name);
    else
        nv3->nvbase.log = log_open_cyclic(log_device_name);

//#ifdef ENABLE_NV_LOG
    // Allows nv_log to be used for multiple nvidia devices
    nv_log_set_device(nv3->nvbase.log); 
//#endif   
    nv_log("Initialising core\n");

    // this will only be logged if ENABLE_NV_LOG_ULTRA is defined
    nv_log_verbose_only("ULTRA LOGGING enabled\n");

    const device_t* device_id = &nv3_device_pci;
    static video_timings_t* timing_id = &timing_nv3_pci;

    // RIVA 128ZX
    if (nv3->nvbase.gpu_revision == NV3_PCI_CFG_REVISION_C00)
    {
        nv_log("Submodel: RIVA 128 ZX (NV3T)\n");

        if (nv3->nvbase.bus_generation >= nv_bus_agp_1x)
        {
            device_id = &nv3t_device_agp; 
            timing_id = &timing_nv3t_agp;
        }
        else
        {
            device_id = &nv3t_device_pci;
            timing_id = &timing_nv3t_pci;
        }
    }
    else
    {
        nv_log("Submodel: RIVA 128 (NV3)\n");

        // we don't need to check for pci as its the default value above
        if (nv3->nvbase.bus_generation >= nv_bus_agp_1x)
        {
            device_id = &nv3_device_agp;   
            timing_id = &timing_nv3_agp;
        }
    }

    (nv3->nvbase.bus_generation >= nv_bus_agp_1x) ? nv_log("AGP bus\n") : nv_log("PCI bus\n");

    // Figure out which vbios the user selected
    // This depends on the bus we are using and if the gpu is rev a/b or rev c
    const char* vbios_id = device_get_config_bios("vbios");
    const char* vbios_file = device_get_bios_file(device_id, vbios_id, 0);

    int32_t err = rom_init(&nv3->nvbase.vbios, vbios_file, 0xC0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
    
    if (err)
    {
        nv_log("NV3 FATAL: failed to load VBIOS err=%d\n", err);
        fatal("Nvidia NV3 init failed: Somehow selected a nonexistent VBIOS? err=%d\n", err);
        return NULL;
    }
    else    
        nv_log("Successfully loaded VBIOS %s located at %s\n", vbios_id, vbios_file);

    // set up the bus and start setting up SVGA core
    if (nv3->nvbase.bus_generation == nv_bus_pci)
        pci_add_card(PCI_ADD_NORMAL, nv3_pci_read, nv3_pci_write, NULL, &nv3->nvbase.pci_slot);
    else
        pci_add_card(PCI_ADD_AGP, nv3_pci_read, nv3_pci_write, NULL, &nv3->nvbase.pci_slot);

    svga_init(device_id, &nv3->nvbase.svga, nv3, nv3->nvbase.vram_amount, 
        nv3_recalc_timings, nv3_svga_read, nv3_svga_write, nv3_draw_cursor, nv3_pvideo_draw);

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, timing_id);
    
    // set vram
    nv_log("VRAM=%d bytes\n", nv3->nvbase.svga.vram_max);

    // init memory mappings
    nv3_init_mappings();

    // make us actually exist
    nv3->nvbase.pci_config.int_line = 0xFF; // no interrupt line routed yet
    nv3->nvbase.pci_config.pci_regs[PCI_REG_COMMAND] = PCI_COMMAND_IO | PCI_COMMAND_MEM;

    // svga is done, so now initialise the real gpu

    nv_log("Initialising GPU core...\n");

    nv3_pstraps_init();             // Initialise Straps
    nv3_pmc_init();                 // Initialise Master Control
    nv3_pfb_init();                 // Initialise Framebuffer Interface
    nv3_pramdac_init();             // Initialise RAMDAC (CLUT, final pixel presentation etc)
    nv3_pgraph_init();              // Initialise accelerated graphics engine
    nv3_pvideo_init();              // Initialise video overlay
    timer_add(&nv3->dma_pusher_timer, nv3_pfifo_dma_pusher_timer, NULL, 0);

    nv_log("Initialising I2C...");
    nv3->nvbase.i2c = i2c_gpio_init("nv3_i2c");
    nv3->nvbase.ddc = ddc_init(i2c_gpio_get_bus(nv3->nvbase.i2c));


    return nv3;
}

// RIVA 128 PCI initialisation function: This function simply allocates the device struct, and sets the bus to PCI before initialising.
void* nv3_init_pci(const device_t* info)
{
    nv3 = (nv3_t*)calloc(1, sizeof(nv3_t));
    nv3->nvbase.bus_generation = nv_bus_pci;
    nv3_init(info);
    return nv3;
}

// RIVA 128 AGP initialisation function: This function simply allocates the device struct, and sets the bus to AGP before initialising.
void* nv3_init_agp(const device_t* info)
{
    nv3 = (nv3_t*)calloc(1, sizeof(nv3_t));
    nv3->nvbase.bus_generation = nv_bus_agp_1x;
    nv3_init(info);
    return nv3;
}

// RIVA 128 ZX PCI initialisation function: This function simply allocates the device struct, and sets the bus to PCI before initialising.
// It also sets the GPU revision to C0 because NV3T config doesn't let you configure the rev (there were multiple steppings, but it's basically irrelevant),
// and sets RAM to 8 MB (the only supported config on ZX cards)
void* nv3t_init_pci(const device_t* info)
{
    nv3 = (nv3_t*)calloc(1, sizeof(nv3_t));
    nv3->nvbase.bus_generation = nv_bus_pci;
    nv3->nvbase.gpu_revision = NV3_PCI_CFG_REVISION_C00;
    nv3->nvbase.vram_amount = NV3_VRAM_SIZE_8MB;
    nv3_init(info);
    return nv3;
}

// RIVA 128 ZX AGP initialisation function: This function simply allocates the device struct, and sets the bus to AGP before initialising.
// It also sets the GPU revision to C0 because NV3T config doesn't let you configure the rev (there were multiple steppings, but it's basically irrelevant)
// and sets RAM to 8 MB (the only supported config on ZX cards)
void* nv3t_init_agp(const device_t* info)
{
    nv3 = (nv3_t*)calloc(1, sizeof(nv3_t));
    nv3->nvbase.bus_generation = nv_bus_agp_2x; //  Riva 128 ZX is AGP2X 
    nv3->nvbase.gpu_revision = NV3_PCI_CFG_REVISION_C00;
    nv3->nvbase.vram_amount = NV3_VRAM_SIZE_8MB;
    nv3_init(info);
    return nv3;
}

/* PCI RST#: the GPU's engines go back to their power-on state, as nv3_init()
   left them. Without this a warm reboot (Windows' restart pulses the KBC reset,
   then the BIOS the PCI reset) kept the old driver's state: PMC interrupts
   enabled with PTIMER/vblank pending held INTA asserted before any NVIDIA
   driver was loaded, and Windows 2000 stalled at its boot screen. VRAM, the
   VBIOS, the SVGA core and the PCI configuration are left alone -- the BIOS
   enumerates the card and runs its VBIOS again after the reset. */
static void nv3_reset(UNUSED(void* priv))
{
    timer_disable(&nv3->dma_pusher_timer);

    memset(&nv3->pmc, 0, sizeof(nv3->pmc));
    memset(&nv3->pfb, 0, sizeof(nv3->pfb));
    memset(&nv3->pbus, 0, sizeof(nv3->pbus));
    memset(&nv3->pfifo, 0, sizeof(nv3->pfifo));
    memset(&nv3->pramdac, 0, sizeof(nv3->pramdac));
    memset(&nv3->pgraph, 0, sizeof(nv3->pgraph));
    memset(&nv3->ptimer, 0, sizeof(nv3->ptimer));
    memset(&nv3->ramfc, 0, sizeof(nv3->ramfc));
    memset(&nv3->pvideo, 0, sizeof(nv3->pvideo));
    memset(&nv3->pme, 0, sizeof(nv3->pme));
    memset(nv3->prim_saved, 0, sizeof(nv3->prim_saved));
    nv3->nvbase.cio_read_bank = nv3->nvbase.cio_write_bank = 0;

    nv3_pstraps_init();
    nv3_pmc_init();
    nv3_pfb_init();
    nv3_pramdac_init();
    nv3_pgraph_init();
    nv3_pvideo_init();

    nv3_pmc_clear_interrupts();
}

void nv3_close(UNUSED(void* priv))
{
    // Shut down logging
    log_close(nv3->nvbase.log);
//#ifdef ENABLE_NV_LOG
    nv_log_set_device(NULL);
//#endif

    // Shut down I2C and the DDC
    ddc_close(nv3->nvbase.ddc);
    i2c_gpio_close(nv3->nvbase.i2c);

    // Destroy the Rivatimers. (It doesn't matter if they are running.)
    rivatimer_destroy(nv3->nvbase.pixel_clock_timer);
    rivatimer_destroy(nv3->nvbase.memory_clock_timer);
    
    // Shut down SVGA
    svga_close(&nv3->nvbase.svga);
    free(nv3);
    nv3 = NULL;
}

// See if the bios rom is available.
int32_t nv3_available(void)
{
    return rom_present(NV3_VBIOS_ASUS_V3000_V151)
    || rom_present(NV3_VBIOS_DIAMOND_V330_V162)
    || rom_present(NV3_VBIOS_ERAZOR_V14700)
    || rom_present(NV3_VBIOS_ERAZOR_V15403)
    || rom_present(NV3_VBIOS_ERAZOR_V15500)
    || rom_present(NV3_VBIOS_STB_V128_V182)
    || rom_present(NV3_VBIOS_STB_V128_V182)
    || rom_present(NV3T_VBIOS_ASUS_V170)
    || rom_present(NV3T_VBIOS_DIAMOND_V330_V182B)
    || rom_present(NV3T_VBIOS_REFERENCE_CEK_V171)
    || rom_present(NV3T_VBIOS_REFERENCE_CEK_V172)
    || rom_present(NV3T_VBIOS_STB_V128ZX_V120)
    || rom_present(NV3T_VBIOS_ASUS_V3000ZX_V170)
    || rom_present(NV3T_VBIOS_AGP300S_V170Y)
    || rom_present(NV3T_VBIOS_CHAINTECH_RI20_V172B)
    || rom_present(NV3T_VBIOS_CREATIVE_CT6730_V1723D)
    || rom_present(NV3T_VBIOS_REFERENCE_V172D)
    || rom_present(NV3T_VBIOS_ELSA_ERAZOR_LT_V15800);
}

// NV3 (RIVA 128)
// PCI
// 2MB or 4MB VRAM
const device_t nv3_device_pci = 
{
    .name = "nVIDIA RIVA 128 (NV3) PCI",
    .internal_name = "nv3_pci",
    .flags = DEVICE_PCI,
    .local = 0,
    .init = nv3_init_pci,
    .close = nv3_close,
    .reset = nv3_reset,
    .speed_changed = nv3_speed_changed,
    .force_redraw = nv3_force_redraw,
    .available = nv3_available,
    .config = nv3_config,
};

// NV3 (RIVA 128)
// AGP
// 2MB or 4MB VRAM
const device_t nv3_device_agp = 
{
    .name = "nVIDIA RIVA 128 (NV3) AGP",
    .internal_name = "nv3_agp",
    .flags = DEVICE_AGP,
    .local = 0,
    .init = nv3_init_agp,
    .close = nv3_close,
    .reset = nv3_reset,
    .speed_changed = nv3_speed_changed,
    .force_redraw = nv3_force_redraw,
    .available = nv3_available,
    .config = nv3_config,
};

// NV3T (RIVA 128 ZX)
// PCI
// 8MB VRAM
const device_t nv3t_device_pci = 
{
    .name = "nVIDIA RIVA 128 ZX (NV3T) PCI",
    .internal_name = "nv3t_pci",
    .flags = DEVICE_PCI,
    .local = 0,
    .init = nv3t_init_pci,
    .close = nv3_close,
    .reset = nv3_reset,
    .speed_changed = nv3_speed_changed,
    .force_redraw = nv3_force_redraw,
    .available = nv3_available,
    .config = nv3t_config,
};

// NV3T (RIVA 128)
// AGP
// 2MB or 4MB VRAM
const device_t nv3t_device_agp = 
{
    .name = "nVIDIA RIVA 128 ZX (NV3T) AGP",
    .internal_name = "nv3t_agp",
    .flags = DEVICE_AGP,
    .local = 0,
    .init = nv3t_init_agp,
    .close = nv3_close,
    .reset = nv3_reset,
    .speed_changed = nv3_speed_changed,
    .force_redraw = nv3_force_redraw,
    .available = nv3_available,
    .config = nv3t_config,
};