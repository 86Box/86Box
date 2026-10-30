/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3 PFB: Framebuffer
 *
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
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h> // DEPENDENT!!!
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>

void nv3_pfb_init(void)
{  
    nv_log("Initialising PFB...");

    // initial configuration: 4mb ram, 128 bit bus width, no extension ram (nv3t only), 4 banks (from my riva 128), twiddle off
    nv3->pfb.boot = (NV3_PFB_BOOT_RAM_EXTENSION_NONE << (NV3_PFB_BOOT_RAM_EXTENSION)
    | (NV3_PFB_BOOT_RAM_DATA_TWIDDLE_OFF << NV3_PFB_BOOT_RAM_DATA_TWIDDLE)
    | (NV3_PFB_BOOT_RAM_BANKS_4 << NV3_PFB_BOOT_RAM_BANKS)
    | (NV3_PFB_BOOT_RAM_WIDTH_128 << NV3_PFB_BOOT_RAM_WIDTH)
    );

    if (nv3->nvbase.vram_amount == NV3_VRAM_SIZE_2MB)
        nv3->pfb.boot |= (NV3_PFB_BOOT_RAM_AMOUNT_2MB << NV3_PFB_BOOT_RAM_AMOUNT);
    else if (nv3->nvbase.vram_amount == NV3_VRAM_SIZE_4MB)
        nv3->pfb.boot |= (NV3_PFB_BOOT_RAM_AMOUNT_4MB << NV3_PFB_BOOT_RAM_AMOUNT);
    else    
        nv3->pfb.boot |= (NV3_PFB_BOOT_RAM_AMOUNT_8MB << NV3_PFB_BOOT_RAM_AMOUNT);

    nv_log("Done\n");
}

uint32_t nv3_pfb_read(uint32_t address) 
{ 
    uint32_t ret = 0x00;

    // todo: friendly logging

    switch (address)
    {
        case NV3_PFB_BOOT:
            ret = nv3->pfb.boot;
            break;
        case NV3_PFB_DEBUG_0:
            ret = nv3->pfb.debug_0;
            break;
        case NV3_PFB_CONFIG_0:
            ret = nv3->pfb.config_0;
            break;
        // Config 0 has a read/write function
        case NV3_PFB_CONFIG_1:
            ret = nv3->pfb.config_1;
            break;
        case NV3_PFB_GREEN_0:
            ret = nv3->pfb.green;
            break;
        case NV3_PFB_DELAY:
            ret = nv3->pfb.delay;
            break;
        case NV3_PFB_RTL:
            ret = nv3->pfb.rtl;
            break;
    }

    return ret; 
}

void nv3_pfb_write(uint32_t address, uint32_t value) 
{
    switch (address)
    {
        // Boot is read only
        case NV3_PFB_DEBUG_0:
            nv3->pfb.debug_0 = value;
            break;
        // Config 0 has a read/write function
        case NV3_PFB_CONFIG_1:              // Config Register 1
            nv3->pfb.config_1 = value;
            break;
        case NV3_PFB_GREEN_0:
            nv3->pfb.green = value;
            break;
        case NV3_PFB_DELAY:
            nv3->pfb.delay = value;
            break;
        case NV3_PFB_RTL:
            nv3->pfb.rtl = value;
            break;
    }
}

// the actual size and pixel depth are set in PRAMDAC using the CRTC so we don't update things here for now
void nv3_pfb_config0_write(uint32_t val)
{
    nv3->pfb.config_0 = val;

    uint32_t new_pfb_htotal = (nv3->pfb.config_0 & 0x3F) << 5;
    // i don't think 16:9 is supported
    uint32_t new_pfb_vtotal = new_pfb_htotal * (3.0/4.0); 
    uint32_t new_bit_depth = (nv3->pfb.config_0 >> 8) & 0x03;

    nv_log_verbose_only("Framebuffer Config Change\n");
    nv_log_verbose_only("Horizontal Size=%d pixels\n", new_pfb_htotal); 
    nv_log_verbose_only("Vertical Size @ 4:3=%d pixels\n", new_pfb_vtotal); 
    
    if (new_bit_depth == NV3_PFB_CONFIG_0_DEPTH_8BPP)
        nv_log_verbose_only("Bit Depth=8bpp\n");
    else if (new_bit_depth == NV3_PFB_CONFIG_0_DEPTH_16BPP)
        nv_log_verbose_only("Bit Depth=16bpp\n");
    else if (new_bit_depth == NV3_PFB_CONFIG_0_DEPTH_32BPP)
        nv_log_verbose_only("Bit Depth=32bpp\n");
}