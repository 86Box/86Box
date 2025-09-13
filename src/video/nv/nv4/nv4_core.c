/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV4 bringup and device emulation.
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
#include <86box/nv/vid_nv4.h>

nv4_t* nv4;

// Stolen from Voodoo 3
static video_timings_t timing_nv4_agp = { .type = VIDEO_AGP, .write_b = 2, .write_w = 2, .write_l = 1, .read_b = 20, .read_w = 20, .read_l = 21 };

// Initialise the MMIO mappings
void nv4_init_mappings_mmio(void)
{
    nv_log("Initialising MMIO mapping\n");

    // 0x0 - 1000000: regs
    // 0x1000000-2000000

    // initialize the mmio mapping
    mem_mapping_add(&nv4->nvbase.mmio_mapping, 0, 0, 
        nv4_mmio_read8,
        nv4_mmio_read16,
        nv4_mmio_read32,
        nv4_mmio_write8,
        nv4_mmio_write16,
        nv4_mmio_write32,
        NULL, MEM_MAPPING_EXTERNAL, nv4);
    
    // initialize the mmio mapping
    mem_mapping_add(&nv4->nvbase.ramin_mapping, 0, 0, 
        nv4_ramin_read8,
        nv4_ramin_read16,
        nv4_ramin_read32,
        nv4_ramin_write8,
        nv4_ramin_write16,
        nv4_ramin_write32,
        NULL, MEM_MAPPING_EXTERNAL, nv4);

}

void nv4_init_mappings_svga(void)
{
    nv_log("Initialising SVGA core memory mapping\n");
    // setup the svga mappings

    mem_mapping_add(&nv4->nvbase.framebuffer_mapping, 0, 0,
        nv4_dfb_read8,
        nv4_dfb_read16,
        nv4_dfb_read32,
        nv4_dfb_write8,
        nv4_dfb_write16,
        nv4_dfb_write32,
        nv4->nvbase.svga.vram, 0, &nv4->nvbase.svga);

    // the SVGA/LFB mapping is also mirrored
    mem_mapping_add(&nv4->nvbase.framebuffer_mapping_mirror, 0, 0, 
        nv4_dfb_read8,
        nv4_dfb_read16,
        nv4_dfb_read32,
        nv4_dfb_write8,
        nv4_dfb_write16,
        nv4_dfb_write32,
        nv4->nvbase.svga.vram, 0, &nv4->nvbase.svga);

    io_sethandler(NV4_CIO_START, NV4_CIO_SIZE, 
    nv4_svga_read, NULL, NULL, 
    nv4_svga_write, NULL, NULL, 
    nv4);
}

void nv4_init_mappings(void)
{
    nv4_init_mappings_mmio();
    nv4_init_mappings_svga();
}

// Updates the mappings after initialisation. 
void nv4_update_mappings(void)
{
    // sanity check
    if (!nv4)
        return; 

    // setting this to 0 doesn't seem to disable it, based on the datasheet

    nv_log("\nMemory Mapping Config Change:\n");

    (nv4->nvbase.pci_config.pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO) ? nv_log("Enable I/O\n") : nv_log("Disable I/O\n");

    io_removehandler(NV4_CIO_START, NV4_CIO_SIZE, 
        nv4_svga_read, NULL, NULL, 
        nv4_svga_write, NULL, NULL, 
        nv4);

    if (nv4->nvbase.pci_config.pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_IO)
        io_sethandler(NV4_CIO_START, NV4_CIO_SIZE, 
        nv4_svga_read, NULL, NULL, 
        nv4_svga_write, NULL, NULL, 
        nv4);   
    
    if (!(nv4->nvbase.pci_config.pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_MEM))
    {
        nv_log("The memory was turned off, not much is going to happen.\n");
        return;
    }

    // turn off bar0 and bar1 by defualt
    mem_mapping_disable(&nv4->nvbase.mmio_mapping);
    mem_mapping_disable(&nv4->nvbase.framebuffer_mapping);
    mem_mapping_disable(&nv4->nvbase.framebuffer_mapping_mirror);
    mem_mapping_disable(&nv4->nvbase.ramin_mapping);

    // Setup BAR0 (MMIO)

    nv_log("BAR0 (MMIO Base) = 0x%08x\n", nv4->nvbase.bar0_mmio_base);

    if (nv4->nvbase.bar0_mmio_base)
    {
        mem_mapping_set_addr(&nv4->nvbase.mmio_mapping, nv4->nvbase.bar0_mmio_base, NV4_MMIO_SIZE);
        mem_mapping_set_addr(&nv4->nvbase.ramin_mapping, nv4->nvbase.bar0_mmio_base + NV4_PRAMIN_START, NV4_PRAMIN_SIZE);
    }

    // if this breaks anything, remove it
    nv_log("BAR1 (Linear Framebuffer & VRAM) = 0x%08x\n", nv4->nvbase.bar1_lfb_base);

    if (nv4->nvbase.bar1_lfb_base)
    {
        if (nv4->nvbase.vram_amount == NV4_VRAM_SIZE_16MB)
        {    
            // we don't need this one in the case of 16mb, 
            mem_mapping_disable(&nv4->nvbase.framebuffer_mapping_mirror);
            mem_mapping_set_addr(&nv4->nvbase.framebuffer_mapping, nv4->nvbase.bar1_lfb_base, NV4_VRAM_SIZE_16MB);
        }
        else if (nv4->nvbase.vram_amount == NV4_VRAM_SIZE_8MB)
        {
            mem_mapping_set_addr(&nv4->nvbase.framebuffer_mapping, nv4->nvbase.bar1_lfb_base, NV4_VRAM_SIZE_8MB);
            mem_mapping_set_addr(&nv4->nvbase.framebuffer_mapping_mirror, nv4->nvbase.bar1_lfb_base + NV4_VRAM_SIZE_8MB, NV4_VRAM_SIZE_8MB);
        }
    }

    // Did we change the banked SVGA mode?
    switch (nv4->nvbase.svga.gdcreg[NV4_PRMVIO_GX_MISC_INDEX] & 0x0c)
    {
        case NV4_PRMVIO_GX_MISC_BANKED_128K_A0000:
            nv_log("SVGA Banked Mode = 128K @ A0000h\n");
            mem_mapping_set_addr(&nv4->nvbase.svga.mapping, 0xA0000, 0x20000); // 128kb @ 0xA0000
            nv4->nvbase.svga.banked_mask = 0x1FFFF;
            break;
        case NV4_PRMVIO_GX_MISC_BANKED_64K_A0000:
            nv_log("SVGA Banked Mode = 64K @ A0000h\n");
            mem_mapping_set_addr(&nv4->nvbase.svga.mapping, 0xA0000, 0x10000); // 64kb @ 0xA0000
            nv4->nvbase.svga.banked_mask = 0xFFFF;
            break;
        case NV4_PRMVIO_GX_MISC_BANKED_32K_B0000:
            nv_log("SVGA Banked Mode = 32K @ B0000h\n");
            mem_mapping_set_addr(&nv4->nvbase.svga.mapping, 0xB0000, 0x8000); // 32kb @ 0xB0000
            nv4->nvbase.svga.banked_mask = 0x7FFF;
            break;
        case NV4_PRMVIO_GX_MISC_BANKED_32K_B8000:
            nv_log("SVGA Banked Mode = 32K @ B8000h\n");
            mem_mapping_set_addr(&nv4->nvbase.svga.mapping, 0xB8000, 0x8000); // 32kb @ 0xB8000
            nv4->nvbase.svga.banked_mask = 0x7FFF;
            break;
    }
}


bool nv4_init()
{
    nv4 = calloc(1, sizeof(nv4_t));
 
     if (!nv4->nvbase.vram_amount)
        nv4->nvbase.vram_amount = device_get_config_int("vram_size");

    /* Set log device name based on card model */
    const char* log_device_name = "NV4";


    /* Just hardcode full logging */

    if (device_get_config_int("nv_debug_fulllog"))
        nv4->nvbase.log = log_open(log_device_name);
    else
        nv4->nvbase.log = log_open_cyclic(log_device_name);

    nv_log_set_device(nv4->nvbase.log);

    nv4->nvbase.bus_generation = nv_bus_agp_2x;

    // Figure out which vbios the user selected
    // This depends on the bus we are using and if the gpu is rev a/b or rev c

    const char* vbios_file = NV4_VBIOS_STB_REVA;

    int32_t err = rom_init(&nv4->nvbase.vbios, vbios_file, 0xC0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
    
    if (err)
    {
        nv_log("NV4 FATAL: failed to load VBIOS err=%d\n", err);
        fatal("Nvidia NV4 init failed: Somehow selected a nonexistent VBIOS? err=%d\n", err);
        return false;
    }
    else    
        nv_log("Successfully loaded VBIOS located at %s\n", vbios_file);

    pci_add_card(PCI_ADD_AGP, nv4_pci_read, nv4_pci_write, NULL, &nv4->nvbase.pci_slot);

    svga_init(&nv4_device_agp, &nv4->nvbase.svga, nv4, nv4->nvbase.vram_amount, 
            nv4_recalc_timings, nv4_svga_read, nv4_svga_write, nv4_draw_cursor, NULL);

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_nv4_agp);

    nv4_init_mappings();
    //nv4_update_mappings();

    return true; 
}

void* nv4_init_stb4400(const device_t *info)
{
    bool successful = nv4_init();

    if (successful)
        return nv4;
    else
        return NULL;
}

void nv4_close(void* priv)
{
    free(nv4);
}

void nv4_speed_changed(void *priv)
{

}

void nv4_draw_cursor(svga_t* svga, int32_t drawline)
{

}


//
// SVGA functions
//
void nv4_recalc_timings(svga_t* svga)
{    
    // sanity check
    if (!nv4)
        return; 

 
    nv4_t* nv4 = (nv4_t*)svga->priv;
 
    // TODO: Everything, this code sucks, incl. NV4_PRAMDAC_GENERAL_CONTROL_BPC and the OFFSET register 
    uint32_t pixel_mode = svga->crtc[NV4_CIO_CRE_PIXEL_INDEX] & 0x03;

    svga->memaddr_latch += (svga->crtc[NV4_CIO_CRE_RPC0_INDEX] & 0x1F) << 16;

    /* Turn off override if we are in VGA mode */
    svga->override = !(pixel_mode == NV4_CIO_CRE_PIXEL_FORMAT_VGA);

    /* NOTE: The RIVA 128 draws in a way almost completely separate to any other 86Box GPU.
    
    Basically, we only blit to buffer32 when something changes and we don't even bother using a timer. We only render when there is something to actually render.

    This is because there is no linear relationship between the contents of VRAM and the contents of the display which 86box's SVGA subsystem cannot tolerate.
    In fact, the position in VRAM and pitch can be changed at any time via an NV_IMAGE_IN_MEMORY object.

    Therefore, we need to completely bypass it using svga->override and draw our own rendering functions. This allows us to use a neat optimisation trick
    to only ever actually draw when we need to do something. This shouldn't be a problem in games, because the drivers will read the current refresh rate from 
    the Windows settings, and then, just submit objects at that pace for anything that changes on the screen.
    */

    // Set the pixel mode
    switch (pixel_mode)
    {
        case NV4_CIO_CRE_PIXEL_FORMAT_8BPP:
            svga->rowoffset += (svga->crtc[NV4_CIO_CRE_RPC0_INDEX] & 0xE0) << 1; // ?????
            svga->bpp = 8;
            svga->lowres = 0;
            svga->map8 = svga->pallook;
            break;
        case NV4_CIO_CRE_PIXEL_FORMAT_16BPP:
            /* This is some sketchy shit that is an attempt at an educated guess
            at pixel clock differences between 9x and NT only in 16bpp. If there is ever an error on 9x with "interlaced" looking graphics,
            this is what's causing it. Possibly fucking up the drivers under *ReactOS* of all things */
            if ((svga->crtc[NV4_CIO_CR_VRS_INDEX] >> 1) & 0x01)
                svga->rowoffset += (svga->crtc[NV4_CIO_CRE_RPC0_INDEX] & 0xE0) << 2;
            else 
                svga->rowoffset += (svga->crtc[NV4_CIO_CRE_RPC0_INDEX] & 0xE0) << 3;

            // 15bpp mode is removed on NV4
            // TODO: Not svga
            svga->bpp = 16;
            svga->lowres = 0;
        
            break;
        case NV4_CIO_CRE_PIXEL_FORMAT_32BPP:
            svga->rowoffset += (svga->crtc[NV4_CIO_CRE_RPC0_INDEX] & 0xE0) << 3;
            
            svga->bpp = 32;
            svga->lowres = 0;
            //svga->render = nv4_render_32bpp;
            break;
    }


    if (((svga->miscout >> 2) & 2) == 2)
    {
        // set clocks
        //nv4_pramdac_set_pixel_clock();
        //nv4_pramdac_set_vram_clock();
    }
}


// See if the bios rom is available.
int32_t nv4_available(void)
{
    return (rom_present(NV4_VBIOS_STB_REVA));
}

// NV4 (RIVA 128)
// AGP
// 8MB or 16MB VRAM
const device_t nv4_device_agp = 
{
    .name = "nVIDIA RIVA TNT (STB Velocity 4400)",
    .internal_name = "nv4_stb4400",
    .flags = DEVICE_AGP,
    .local = 0,
    .init = nv4_init_stb4400,
    .close = nv4_close,
    .speed_changed = nv4_speed_changed,
    .force_redraw = nv4_force_redraw,
    .available = nv4_available,
    .config = nv4_config,
};
