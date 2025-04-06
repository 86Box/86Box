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
 *
 *          Copyright 2024-2025 starfrost
 */
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/io.h>
#include <86box/pci.h>
#include <86box/rom.h> // DEPENDENT!!!
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>

/* Main device object pointer */
nv3_t* nv3;

/* These are a ****PLACEHOLDER**** and are copied from 3dfx VoodooBanshee/Voodoo3*/
static video_timings_t timing_nv3_pci = { .type = VIDEO_PCI, .write_b = 2, .write_w = 2, .write_l = 1, .read_b = 20, .read_w = 20, .read_l = 21 };
static video_timings_t timing_nv3_agp = { .type = VIDEO_AGP, .write_b = 2, .write_w = 2, .write_l = 1, .read_b = 20, .read_w = 20, .read_l = 21 };
// Revision C
static video_timings_t timing_nv3t_pci = { .type = VIDEO_PCI, .write_b = 2, .write_w = 2, .write_l = 1, .read_b = 20, .read_w = 20, .read_l = 21 };
static video_timings_t timing_nv3t_agp = { .type = VIDEO_AGP, .write_b = 2, .write_w = 2, .write_l = 1, .read_b = 20, .read_w = 20, .read_l = 21 };

// Prototypes for functions only used in this translation unit
void nv3_init_mappings_mmio();
void nv3_init_mappings_svga();
bool nv3_is_svga_redirect_address(uint32_t addr);

uint8_t nv3_svga_in(uint16_t addr, void* priv);
void nv3_svga_out(uint16_t addr, uint8_t val, void* priv);

// Determine if this address needs to be redirected to the SVGA subsystem.

bool nv3_is_svga_redirect_address(uint32_t addr)
{
    return (addr >= NV3_PRMVIO_START && addr <= NV3_PRMVIO_END)     // VGA
    || (addr >= NV3_PRMCIO_START && addr <= NV3_PRMCIO_END)         // CRTC
    || (addr >= NV3_VGA_DAC_START && addr <= NV3_VGA_DAC_END);      // Legacy RAMDAC support(?)
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
        uint32_t real_address = addr & 0x3FF;

        ret = nv3_svga_in(real_address, nv3);

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
        uint32_t real_address = addr & 0x3FF;

        ret = nv3_svga_in(real_address, nv3)
        | (nv3_svga_in(real_address + 1, nv3) << 8);
        
        nv_log_verbose_only("Redirected MMIO read16 to SVGA: addr=0x%04x returned 0x%04x\n", addr, ret);

        return ret; 
    }

    ret = nv3_mmio_read32(addr, priv);
    return (uint8_t)(ret >> ((addr & 3) << 3) & 0xFFFF);
}

// Read 32-bit MMIO
uint32_t nv3_mmio_read32(uint32_t addr, void* priv)
{
    uint32_t ret = 0x00;

    // Some of these addresses are Weitek VGA stuff and we need to mask it to this first because the weitek addresses are 8-bit aligned.
    addr &= 0xFFFFFF;

    if (nv3_is_svga_redirect_address(addr))
    {
        // svga writes are not logged anyway rn
        uint32_t real_address = addr & 0x3FF;

        ret = nv3_svga_in(real_address, nv3)
        | (nv3_svga_in(real_address + 1, nv3) << 8)
        | (nv3_svga_in(real_address + 2, nv3) << 16)
        | (nv3_svga_in(real_address + 3, nv3) << 24);

        nv_log_verbose_only("Redirected MMIO read32 to SVGA: addr=0x%04x returned 0x%04x\n", addr, ret);

        return ret; 
    }


    ret = nv3_mmio_arbitrate_read(addr);


    return ret; 

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
        uint32_t real_address = addr & 0x3FF;

        nv_log_verbose_only("Redirected MMIO write8 to SVGA: addr=0x%04x val=0x%02x\n", addr, val);

        nv3_svga_out(real_address, val & 0xFF, nv3);

        return; 
    }
    
    // overwrite first 8bits of a 32 bit value
    uint32_t new_val = nv3_mmio_read32(addr, NULL);

    new_val &= (~0xFF << (addr & 3) << 3);
    new_val |= (val << ((addr & 3) << 3));

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
        uint32_t real_address = addr & 0x3FF;

        nv_log_verbose_only("Redirected MMIO write16 to SVGA: addr=0x%04x val=0x%02x\n", addr, val);


        nv3_svga_out(real_address, val & 0xFF, nv3);
        nv3_svga_out(real_address + 1, (val >> 8) & 0xFF, nv3);
        
        return; 
    }

    // overwrite first 16bits of a 32 bit value
    uint32_t new_val = nv3_mmio_read32(addr, NULL);

    new_val &= (~0xFFFF << (addr & 3) << 3);
    new_val |= (val << ((addr & 3) << 3));

    nv3_mmio_write32(addr, new_val, priv);
}

// Write 32-bit MMIO
void nv3_mmio_write32(uint32_t addr, uint32_t val, void* priv)
{
    addr &= 0xFFFFFF;

    // This is weitek vga stuff
    if (nv3_is_svga_redirect_address(addr))
    {
        // svga writes are not logged anyway rn
        uint32_t real_address = addr & 0x3FF;

        nv_log_verbose_only("Redirected MMIO write32 to SVGA: addr=0x%04x val=0x%02x\n", addr, val);

        nv3_svga_out(real_address, val & 0xFF, nv3);
        nv3_svga_out(real_address + 1, (val >> 8) & 0xFF, nv3);
        nv3_svga_out(real_address + 2, (val >> 16) & 0xFF, nv3);
        nv3_svga_out(real_address + 3, (val >> 24) & 0xFF, nv3);
        
        return; 
    }

    nv3_mmio_arbitrate_write(addr, val);
}

// PCI stuff
// BAR0         Pointer to MMIO space
// BAR1         Pointer to Linear Framebuffer (NV_USER)

uint8_t nv3_pci_read(int32_t func, int32_t addr, void* priv)
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

        case NV3_PCI_CFG_DEVICE_ID:
            ret = (NV_PCI_DEVICE_NV3 & 0xFF);
            break;
        
        case NV3_PCI_CFG_DEVICE_ID+1:
            ret = (NV_PCI_DEVICE_NV3 >> 8);
            break;
        
        // various capabilities
        // IO space         enabled
        // Memory space     enabled
        // Bus master       enabled
        // Write/inval      enabled
        // Pal snoop        enabled
        // Capabiliies list enabled
        // 66Mhz FSB        capable

        case PCI_REG_COMMAND_L:
            ret = nv3->pci_config.pci_regs[PCI_REG_COMMAND_L] ; // we actually respond to the fucking 
            break;
        
        case PCI_REG_COMMAND_H:
            ret = nv3->pci_config.pci_regs[PCI_REG_COMMAND_H] & NV3_PCI_COMMAND_H_FAST_BACK2BACK; // always enable fast back2back
            break;

        // pci status register
        case PCI_REG_STATUS_L:
            if (nv3->pextdev.straps 
            & NV3_PSTRAPS_BUS_SPEED_66MHZ)
                ret = (nv3->pci_config.pci_regs[PCI_REG_STATUS_L] | NV3_PCI_STATUS_L_66MHZ_CAPABLE);
            else
                ret = nv3->pci_config.pci_regs[PCI_REG_STATUS_L];

            break;

        case PCI_REG_STATUS_H:
            ret = (nv3->pci_config.pci_regs[PCI_REG_STATUS_H]) & (NV3_PCI_STATUS_H_FAST_DEVSEL_TIMING << NV3_PCI_STATUS_H_DEVSEL_TIMING);
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

        // BARs are marked as prefetchable per the datasheet
        case NV3_PCI_CFG_BAR0_L:
        case NV3_PCI_CFG_BAR1_L:
            // only bit that matters is bit 3 (prefetch bit)
            ret =(NV3_PCI_CFG_BAR_PREFETCHABLE_ENABLED << NV3_PCI_CFG_BAR_PREFETCHABLE);
            break;

        // These registers are hardwired to zero per the datasheet
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

        case NV3_PCI_CFG_ENABLE_VBIOS:
            ret = nv3->pci_config.vbios_enabled;
            break;
        
        case NV3_PCI_CFG_INT_LINE:
            ret = nv3->pci_config.int_line;
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
            
        case NV3_PCI_CFG_SUBSYSTEM_ID_MIRROR_START:
        case NV3_PCI_CFG_SUBSYSTEM_ID_MIRROR_END:
            ret = nv3->pci_config.pci_regs[NV3_PCI_CFG_SUBSYSTEM_ID + (addr & 0x03)];
            break;

        default: // by default just return pci_config.pci_regs
            ret = nv3->pci_config.pci_regs[addr];
            break;
        
    }

    nv_log("nv3_pci_read func=0x%04x addr=0x%04x ret=0x%04x\n", func, addr, ret);
    return ret; 
}

void nv3_pci_write(int32_t func, int32_t addr, uint8_t val, void* priv)
{

    // sanity check
    if (!nv3)
        return; 

    // some addresses are not writable so can't have any effect and can't be allowed to be modified using this code
    // as an example, only the most significant byte of the PCI BARs can be modified
    if (addr >= NV3_PCI_CFG_BAR0_L && addr <= NV3_PCI_CFG_BAR0_BYTE2
    && addr >= NV3_PCI_CFG_BAR1_L && addr <= NV3_PCI_CFG_BAR1_BYTE2)
        return;

    nv_log("nv3_pci_write func=0x%04x addr=0x%04x val=0x%04x\n", func, addr, val);

    nv3->pci_config.pci_regs[addr] = val;

    switch (addr)
    {
        // standard pci command stuff
        case PCI_REG_COMMAND_L:
            nv3->pci_config.pci_regs[PCI_REG_COMMAND_L] = val;
            // actually update the mappings
            nv3_update_mappings();
            break;
        case PCI_REG_COMMAND_H:
            nv3->pci_config.pci_regs[PCI_REG_COMMAND_H] = val;
            // actually update the mappings
            nv3_update_mappings();          
            break;
        // pci status register
        case PCI_REG_STATUS_L:
            nv3->pci_config.pci_regs[PCI_REG_STATUS_L] = val | (NV3_PCI_STATUS_L_66MHZ_CAPABLE);
            break;
        case PCI_REG_STATUS_H:
            nv3->pci_config.pci_regs[PCI_REG_STATUS_H] = val | (NV3_PCI_STATUS_H_FAST_DEVSEL_TIMING << NV3_PCI_STATUS_H_DEVSEL_TIMING);
            break;
        //TODO: ACTUALLY REMAP THE MMIO AND NV_USER
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
                nv3->pci_config.vbios_enabled = (val & 0x01);

            if (nv3->pci_config.vbios_enabled)
            {
                // First see if we simply wanted to change the VBIOS location

                // Enable it in case it was disabled before
                mem_mapping_enable(&nv3->nvbase.vbios.mapping);

                if (addr != NV3_PCI_CFG_ENABLE_VBIOS)
                {
                    uint32_t old_addr = nv3->nvbase.vbios.mapping.base;
                    // 9bit register
                    uint32_t new_addr = nv3->pci_config.pci_regs[NV3_PCI_CFG_VBIOS_BASE_H] << 24 |
                    nv3->pci_config.pci_regs[NV3_PCI_CFG_VBIOS_BASE_L] << 16;

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
            nv3->pci_config.int_line = val;
            break;
        //bar2-5 are not used and can't be written to
        case NV3_PCI_CFG_BAR_INVALID_START ... NV3_PCI_CFG_BAR_INVALID_END:
            break;

        // these are mirrored to the subsystem id and also stored in the ROMBIOS
        case NV3_PCI_CFG_SUBSYSTEM_ID_MIRROR_START:
        case NV3_PCI_CFG_SUBSYSTEM_ID_MIRROR_END:
            nv3->pci_config.pci_regs[NV3_PCI_CFG_SUBSYSTEM_ID + (addr & 0x03)] = val;
            break;

        default:

    }
}


//
// SVGA functions
//
void nv3_recalc_timings(svga_t* svga)
{    
    // sanity check
    if (!nv3)
        return; 

    nv3_t* nv3 = (nv3_t*)svga->priv;
    uint32_t pixel_mode = svga->crtc[NV3_CRTC_REGISTER_PIXELMODE] & 0x03;

    svga->ma_latch += (svga->crtc[NV3_CRTC_REGISTER_RPC0] & 0x1F) << 16;

    // should these actually use separate values?
    // i don't we should force the top 2 bits to 1...

    // required for VESA resolutions, force parameters higher
    // only fuck around with any of this in VGAmode?

    if (pixel_mode == NV3_CRTC_REGISTER_PIXELMODE_VGA)
    {
        if (svga->crtc[NV3_CRTC_REGISTER_PIXELMODE] & 1 << (NV3_CRTC_REGISTER_FORMAT_VDT10)) svga->vtotal += 0x400;
        if (svga->crtc[NV3_CRTC_REGISTER_PIXELMODE] & 1 << (NV3_CRTC_REGISTER_FORMAT_VDE10)) svga->dispend += 0x400;
        if (svga->crtc[NV3_CRTC_REGISTER_PIXELMODE] & 1 << (NV3_CRTC_REGISTER_FORMAT_VRS10)) svga->vblankstart += 0x400;
        if (svga->crtc[NV3_CRTC_REGISTER_PIXELMODE] & 1 << (NV3_CRTC_REGISTER_FORMAT_VBS10)) svga->vsyncstart += 0x400;
        if (svga->crtc[NV3_CRTC_REGISTER_PIXELMODE] & 1 << (NV3_CRTC_REGISTER_FORMAT_HBE6)) svga->hdisp += 0x400; 

        if (svga->crtc[NV3_CRTC_REGISTER_HEB] & 0x01)
            svga->hdisp += 0x100; // large screen bit
    }
 
    // Set the pixel mode
    switch (pixel_mode)
    {
        case NV3_CRTC_REGISTER_PIXELMODE_8BPP:
            svga->rowoffset += (svga->crtc[NV3_CRTC_REGISTER_RPC0] & 0xE0) << 1; // ?????
            svga->bpp = 8;
            svga->lowres = 0;
            svga->map8 = svga->pallook;
            svga->render = svga_render_8bpp_highres;
            break;
        case NV3_CRTC_REGISTER_PIXELMODE_16BPP:
            /* This is some sketchy shit that is an attempt at an educated guess
            at pixel clock differences between 9x and NT only in 16bpp. If there is ever an error on 9x with "interlaced" looking graphics,
            this is what's causing it. Possibly fucking *ReactOS* of all things */
            if ((svga->crtc[NV3_CRTC_REGISTER_VRETRACESTART] >> 1) & 0x01)
                svga->rowoffset += (svga->crtc[NV3_CRTC_REGISTER_RPC0] & 0xE0) << 2;
            else 
                svga->rowoffset += (svga->crtc[NV3_CRTC_REGISTER_RPC0] & 0xE0) << 3;


            /* sometimes it really renders in 15bpp, so you need to do this */
            if ((nv3->pramdac.general_control >> NV3_PRAMDAC_GENERAL_CONTROL_565_MODE) & 0x01)
            {
                svga->bpp = 16;
                svga->lowres = 0;
                svga->render = nv3_render_16bpp;
            }
            else
            {
                svga->bpp = 15;
                svga->lowres = 0;
                svga->render = nv3_render_15bpp;
                
            }
        
            break;
        case NV3_CRTC_REGISTER_PIXELMODE_32BPP:
            svga->rowoffset += (svga->crtc[NV3_CRTC_REGISTER_RPC0] & 0xE0) << 3;
            
            svga->bpp = 32;
            svga->lowres = 0;
            svga->render = nv3_render_32bpp;
            break;
    }

    // from nv_riva128
    if (((svga->miscout >> 2) & 2) == 2)
    {
        // set clocks
        nv3_pramdac_set_pixel_clock();
        nv3_pramdac_set_vram_clock();
    }
}

void nv3_speed_changed(void* priv)
{
    // sanity check
    if (!nv3)
        return; 
        
    nv3_recalc_timings(&nv3->nvbase.svga);
}

// Force Redraw
// Reset etc.
void nv3_force_redraw(void* priv)
{
    // sanity check
    if (!nv3)
        return; 

    nv3->nvbase.svga.fullchange = changeframecount; 
}

// Read from SVGA core memory
uint8_t nv3_svga_in(uint16_t addr, void* priv)
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
        uint32_t real_rma_read_addr = ((nv3->pbus.rma.mode & NV3_CRTC_REGISTER_RMA_MODE_MAX - 1) << 1) + (addr & 0x03); 
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
void nv3_svga_out(uint16_t addr, uint8_t val, void* priv)
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
    uint8_t old_value;

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

            // Handle nvidia extended Bank0/Bank1 IDs
            switch (crtcreg)
            {
                case NV3_CRTC_REGISTER_READ_BANK:
                        nv3->nvbase.cio_read_bank = val;
                        if (nv3->nvbase.svga.chain4) // chain4 addressing (planar?)
                            nv3->nvbase.svga.read_bank = nv3->nvbase.cio_read_bank << 15;
                        else
                            nv3->nvbase.svga.read_bank = nv3->nvbase.cio_read_bank << 13; // extended bank numbers
                    break;
                case NV3_CRTC_REGISTER_WRITE_BANK:
                    nv3->nvbase.cio_write_bank = val;
                        if (nv3->nvbase.svga.chain4)
                            nv3->nvbase.svga.write_bank = nv3->nvbase.cio_write_bank << 15;
                        else
                            nv3->nvbase.svga.write_bank = nv3->nvbase.cio_write_bank << 13;
                    break;
                case NV3_CRTC_REGISTER_RMA:
                    nv3->pbus.rma.mode = val & NV3_CRTC_REGISTER_RMA_MODE_MAX;
                    break;
                case NV3_CRTC_REGISTER_I2C_GPIO:
                    uint8_t scl = !!(val & 0x20);
                    uint8_t sda = !!(val & 0x10);
                    // Set an I2C GPIO register
                    i2c_gpio_set(nv3->nvbase.i2c, scl, sda);
                    break;
            }

            /* Recalculate the timings if we actually changed them 
            Additionally only do it if the value actually changed*/
            if (old_value != val)
            {
                // Thx to Fuel who basically wrote most of the SVGA compatibility code already (although I fixed some issues), because VGA is boring 
                // and in the words of an ex-Rendition/3dfx/NVIDIA engineer, "VGA was basically an undocumented bundle of steaming you-know-what.   
                // And it was essential that any cores the PC 3D startups acquired had to work with all the undocumented modes and timing tweaks (mode X, etc.)"
                if (nv3->nvbase.svga.crtcreg < 0xE
                && nv3->nvbase.svga.crtcreg > 0x10)
                {
                    nv3->nvbase.svga.fullchange = changeframecount;
                    nv3_recalc_timings(&nv3->nvbase.svga);
                }
            }

            break;
        default:
            svga_out(addr, val, &nv3->nvbase.svga);
            break;
    }

}

/* DFB, sets up a dumb framebuffer */
uint8_t nv3_dfb_read8(uint32_t addr, void* priv)
{
    addr &= (nv3->nvbase.svga.vram_mask);
    return nv3->nvbase.svga.vram[addr];
}

uint16_t nv3_dfb_read16(uint32_t addr, void* priv)
{
    addr &= (nv3->nvbase.svga.vram_mask);
    return (nv3->nvbase.svga.vram[addr + 1] << 8) | nv3->nvbase.svga.vram[addr];
}

uint32_t nv3_dfb_read32(uint32_t addr, void* priv)
{
    addr &= (nv3->nvbase.svga.vram_mask);
    return (nv3->nvbase.svga.vram[addr + 3] << 24) | (nv3->nvbase.svga.vram[addr + 2] << 16) +
    (nv3->nvbase.svga.vram[addr + 1] << 8) | nv3->nvbase.svga.vram[addr];
}

void nv3_dfb_write8(uint32_t addr, uint8_t val, void* priv)
{
    addr &= (nv3->nvbase.svga.vram_mask);
    nv3->nvbase.svga.vram[addr] = val;
}

void nv3_dfb_write16(uint32_t addr, uint16_t val, void* priv)
{
    addr &= (nv3->nvbase.svga.vram_mask);
    nv3->nvbase.svga.vram[addr + 1] = (val >> 8) & 0xFF;
    nv3->nvbase.svga.vram[addr] = (val) & 0xFF;
}

void nv3_dfb_write32(uint32_t addr, uint32_t val, void* priv)
{
    addr &= (nv3->nvbase.svga.vram_mask);
    nv3->nvbase.svga.vram[addr + 3] = (val >> 24) & 0xFF;
    nv3->nvbase.svga.vram[addr + 2] = (val >> 16) & 0xFF;
    nv3->nvbase.svga.vram[addr + 1] = (val >> 8) & 0xFF;
    nv3->nvbase.svga.vram[addr] = (val) & 0xFF;
}

/* Cursor shit */
void nv3_draw_cursor(svga_t* svga, int32_t drawline)
{
    // sanity check
    if (!nv3)
        return; 
    
    // On windows, this shows up using NV_IMAGE_IN_MEMORY.
    // Do we need to emulate it?

    nv_log("nv3_draw_cursor drawline=0x%04x", drawline);
}

// MMIO 0x110000->0x111FFF is mapped to a mirror of the VBIOS.
// Note this area is 64kb and the vbios is only 32kb. See below..

uint8_t nv3_prom_read(uint32_t address)
{
    // prom area is 64k, so...
    // first see if we even have a rom of 64kb in size
    uint32_t max_rom_size = NV3_PROM_END - NV3_PROM_START;
    uint32_t real_rom_size = max_rom_size;

    // set it
    if (nv3->nvbase.vbios.sz < max_rom_size)
        real_rom_size = nv3->nvbase.vbios.sz;

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
        uint8_t val = nv3->nvbase.vbios.rom[rom_address];
        nv_log("PROM VBIOS Read 0x%05x <- 0x%05x", val, rom_address);
        return val;
    }
}

void nv3_prom_write(uint32_t address, uint32_t value)
{
    uint32_t real_addr = address & 0x1FFFF;
    nv_log("What's going on here? Tried to write to the Video BIOS ROM? (Address=0x%05x, value=0x%02x)", address, value);
}

// Initialise the MMIO mappings
void nv3_init_mappings_mmio()
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

void nv3_init_mappings_svga()
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
    nv3_svga_in, NULL, NULL, 
    nv3_svga_out, NULL, NULL, 
    nv3);
}

void nv3_init_mappings()
{
    nv3_init_mappings_mmio();
    nv3_init_mappings_svga();
}

// Updates the mappings after initialisation. 
void nv3_update_mappings()
{
    // sanity check
    if (!nv3)
        return; 

    // setting this to 0 doesn't seem to disable it, based on the datasheet

    nv_log("\nMemory Mapping Config Change:\n");

    (nv3->pci_config.pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO) ? nv_log("Enable I/O\n") : nv_log("Disable I/O\n");

    io_removehandler(0x03c0, 0x0020, 
        nv3_svga_in, NULL, NULL, 
        nv3_svga_out, NULL, NULL, 
        nv3);

    if (nv3->pci_config.pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO)
        io_sethandler(0x03c0, 0x0020, 
        nv3_svga_in, NULL, NULL, 
        nv3_svga_out, NULL, NULL, 
        nv3);   
    
    if (!(nv3->pci_config.pci_regs[PCI_REG_COMMAND]) & PCI_COMMAND_MEM)
    {
        nv_log("The memory was turned off, not much is going to happen.\n");
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

    // this is likely mirrored 
    // 4x on 2mb cards
    // 2x on 4mb cards
    // and not at all on 8mb

    /* TODO: 2MB */

    // 4MB VRAM memory map:
    // LFB_BASE+VRAM_SIZE=RAMIN Mirror(?)                                                   0x1400000 (VERIFY PCBOX)
    // LFB_BASE+VRAM_SIZE*2=LFB Mirror(?)                                                   0x1800000            
    // LFB_BASE+VRAM_SIZE*3=Definitely RAMIN (then it ends, the total ram space is 16mb)    0x1C00000

    // 8MB VRAM memory map:
    // LFB_BASE->LFB_BASE+VRAM_SIZE=LFB
    // What is in 800000-c00000?
    // LFB_BASE+0xC00000 = RAMIN

    if (nv3->nvbase.bar1_lfb_base)
    {
        if (nv3->nvbase.vram_amount == NV3_VRAM_SIZE_4MB)
        {    
            mem_mapping_set_addr(&nv3->nvbase.framebuffer_mapping, nv3->nvbase.bar1_lfb_base, NV3_VRAM_SIZE_4MB);
            mem_mapping_set_addr(&nv3->nvbase.ramin_mapping_mirror, nv3->nvbase.bar1_lfb_base + NV3_LFB_RAMIN_MIRROR_START, NV3_LFB_MAPPING_SIZE);
            mem_mapping_set_addr(&nv3->nvbase.framebuffer_mapping_mirror, nv3->nvbase.bar1_lfb_base + NV3_LFB_MIRROR_START, NV3_VRAM_SIZE_4MB);
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
        else
        {
            fatal("NV3 2MB not implemented yet"); 
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
void* nv3_init(const device_t *info)
{
    if (device_get_config_int("nv_debug_fulllog"))
        nv3->nvbase.log = log_open("NV3");
    else
        nv3->nvbase.log = log_open_cyclic("NV3");

#ifdef ENABLE_NV_LOG
    // Allows nv_log to be used for multiple nvidia devices
    nv_log_set_device(nv3->nvbase.log); 
#endif   
    nv_log("Initialising core\n");

    // this will only be logged if ENABLE_NV_LOG_ULTRA is defined
    nv_log_verbose_only("ULTRA LOGGING enabled");


    // Figure out which vbios the user selected
    const char* vbios_id = device_get_config_bios("vbios");
    const char* vbios_file = "";

    // depends on the bus we are using
    if (nv3->nvbase.bus_generation == nv_bus_pci)
        vbios_file = device_get_bios_file(&nv3_device_pci, vbios_id, 0);
    else   
        vbios_file = device_get_bios_file(&nv3_device_agp, vbios_id, 0);

    int32_t err = rom_init(&nv3->nvbase.vbios, vbios_file, 0xC0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
    
    if (err)
    {
        nv_log("NV3 FATAL: failed to load VBIOS err=%d\n", err);
        fatal("Nvidia NV3 init failed: Somehow selected a nonexistent VBIOS? err=%d\n", err);
        return NULL;
    }
    else    
        nv_log("Successfully loaded VBIOS %s located at %s\n", vbios_id, vbios_file);

    // set the vram amount and gpu revision
    nv3->nvbase.vram_amount = device_get_config_int("vram_size");
    nv3->nvbase.gpu_revision = device_get_config_int("chip_revision");
    
    // set up the bus and start setting up SVGA core
    if (nv3->nvbase.bus_generation == nv_bus_pci)
    {
        nv_log("using PCI bus\n");

        pci_add_card(PCI_ADD_NORMAL, nv3_pci_read, nv3_pci_write, NULL, &nv3->nvbase.pci_slot);

        svga_init(&nv3_device_pci, &nv3->nvbase.svga, nv3, nv3->nvbase.vram_amount, 
        nv3_recalc_timings, nv3_svga_in, nv3_svga_out, nv3_draw_cursor, NULL);

        if (nv3->nvbase.gpu_revision == NV3_PCI_CFG_REVISION_C00)
            video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_nv3t_pci);
        else 
            video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_nv3_pci);

    }
    else if (nv3->nvbase.bus_generation == nv_bus_agp_1x)
    {
        nv_log("using AGP 1X bus\n");

        pci_add_card(PCI_ADD_AGP, nv3_pci_read, nv3_pci_write, NULL, &nv3->nvbase.pci_slot);

        svga_init(&nv3_device_agp, &nv3->nvbase.svga, nv3, nv3->nvbase.vram_amount, 
        nv3_recalc_timings, nv3_svga_in, nv3_svga_out, nv3_draw_cursor, NULL);

        if (nv3->nvbase.gpu_revision == NV3_PCI_CFG_REVISION_C00)
            video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_nv3t_agp);
        else 
            video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_nv3_agp);
    }

    // set vram
    nv_log("VRAM=%d bytes\n", nv3->nvbase.svga.vram_max);

    // init memory mappings
    nv3_init_mappings();

    // make us actually exist
    nv3->pci_config.int_line = 0xFF; // per datasheet
    nv3->pci_config.pci_regs[PCI_REG_COMMAND] = PCI_COMMAND_IO | PCI_COMMAND_MEM;

    // svga is done, so now initialise the real gpu

    nv_log("Initialising GPU core...\n");

    nv3_pextdev_init();             // Initialise Straps
    nv3_pmc_init();                 // Initialise Master Control
    nv3_pbus_init();                // Initialise Bus (the 128 part of riva)
    nv3_pfb_init();                 // Initialise Framebuffer Interface
    nv3_pramdac_init();             // Initialise RAMDAC (CLUT, final pixel presentation etc)
    nv3_pfifo_init();               // Initialise FIFO for graphics object submission
    nv3_pgraph_init();              // Initialise accelerated graphics engine
    nv3_ptimer_init();              // Initialise programmable interval timer
    nv3_pvideo_init();              // Initialise video overlay engine

    nv_log("Initialising I2C...");
    nv3->nvbase.i2c = i2c_gpio_init("nv3_i2c");
    nv3->nvbase.ddc = ddc_init(i2c_gpio_get_bus(nv3->nvbase.i2c));

    return nv3;
}

// This function simply allocates ram and sets the bus to pci before initialising.
void* nv3_init_pci(const device_t* info)
{
    nv3 = (nv3_t*)calloc(1, sizeof(nv3_t));
    nv3->nvbase.bus_generation = nv_bus_pci;
    nv3_init(info);
}

// This function simply allocates ram and sets the bus to agp before initialising.
void* nv3_init_agp(const device_t* info)
{
    nv3 = (nv3_t*)calloc(1, sizeof(nv3_t));
    nv3->nvbase.bus_generation = nv_bus_agp_1x;
    nv3_init(info);
}

void nv3_close(void* priv)
{
    // Shut down logging
    log_close(nv3->nvbase.log);
#ifdef ENABLE_NV_LOG
    nv_log_set_device(NULL);
#endif

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
int32_t nv3_available()
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
    || rom_present(NV3T_VBIOS_REFERENCE_CEK_V172);
}

// NV3 (RIVA 128)
// PCI
// 2MB or 4MB VRAM
const device_t nv3_device_pci = 
{
    .name = "NVidia RIVA 128 (NV3) PCI",
    .internal_name = "nv3",
    .flags = DEVICE_PCI,
    .local = 0,
    .init = nv3_init_pci,
    .close = nv3_close,
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
    .name = "NVidia RIVA 128 (NV3) AGP",
    .internal_name = "nv3_agp",
    .flags = DEVICE_AGP,
    .local = 0,
    .init = nv3_init_agp,
    .close = nv3_close,
    .speed_changed = nv3_speed_changed,
    .force_redraw = nv3_force_redraw,
    .available = nv3_available,
    .config = nv3_config,
};