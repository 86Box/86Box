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
#include <86box/pci.h>
#include <86box/rom.h> // DEPENDENT!!!
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>

// Functions only used in this translation unit
uint32_t nv3_pfb_config0_read();
void nv3_pfb_config0_write(uint32_t val);

nv_register_t pfb_registers[] = {
    { NV3_PFB_BOOT, "PFB Boot Config", NULL, NULL},
    { NV3_PFB_DELAY, "PFB Delay", NULL, NULL},
    { NV3_PFB_GREEN_0, "PFB Green / Power Saving", NULL, NULL,},
    { NV3_PFB_CONFIG_0, "PFB Framebuffer Config 0", nv3_pfb_config0_read, nv3_pfb_config0_write },
    { NV3_PFB_CONFIG_1, "PFB Framebuffer Config 1", NULL, NULL },
    { NV3_PFB_RTL, "PFB RTL (Part of memory timings?)", NULL, NULL },
    { NV_REG_LIST_END, NULL, NULL, NULL}, // sentinel value 
};

void nv3_pfb_init()
{  
    nv_log("Initialising PFB...");

    // initial configuration:
    // ram              4mb
    // bus width        128bit
    // extension ram    none (was this ever used?)
    // ram banks        4 (based on observation of physical RIVA 128 with 4mb, does 1 bank = 1chip?)
    // twiddle          off (check this on a real card once it's actually installed)
    nv3->pfb.boot = (NV3_PFB_BOOT_RAM_EXTENSION_NONE << (NV3_PFB_BOOT_RAM_EXTENSION)
    | (NV3_PFB_BOOT_RAM_DATA_TWIDDLE_OFF << NV3_PFB_BOOT_RAM_DATA_TWIDDLE)
    | (NV3_PFB_BOOT_RAM_BANKS_4 << NV3_PFB_BOOT_RAM_BANKS)
    | (NV3_PFB_BOOT_RAM_WIDTH_128 << NV3_PFB_BOOT_RAM_WIDTH)
    | (NV3_PFB_BOOT_RAM_AMOUNT_4MB << NV3_PFB_BOOT_RAM_AMOUNT)
    );

    nv_log("Done\n");
}

uint32_t nv3_pfb_read(uint32_t address) 
{ 
    nv_register_t* reg = nv_get_register(address, pfb_registers, sizeof(pfb_registers)/sizeof(pfb_registers[0]));

    uint32_t ret = 0x00;

    // todo: friendly logging

    nv_log("PFB Read from 0x%08x", address);

    // if the register actually exists
    if (reg)
    {
        // on-read function
        if (reg->on_read)
            ret = reg->on_read();
        else
        {
            switch (reg->address)
            {
                case NV3_PFB_BOOT:
                    ret = nv3->pfb.boot;
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
        }

        if (reg->friendly_name)
            nv_log(": 0x%08x <- %s\n", ret, reg->friendly_name);
        else   
            nv_log("\n");
    }
    else
    {
        nv_log(": Unknown register read (address=0x%08x), returning 0x00\n", address);
    }

    return ret; 
}

void nv3_pfb_write(uint32_t address, uint32_t value) 
{
    nv_register_t* reg = nv_get_register(address, pfb_registers, sizeof(pfb_registers)/sizeof(pfb_registers[0]));

    nv_log("PFB Write 0x%08x -> 0x%08x", value, address);

    // if the register actually exists
    if (reg)
    {
        if (reg->friendly_name)
            nv_log(": %s\n", reg->friendly_name);
        else   
            nv_log("\n");

        // on-read function
        if (reg->on_write)
            reg->on_write(value);   
        else
        {
            switch (reg->address)
            {
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
    }
    else /* Completely unknown */
    {
        nv_log(": Unknown register write (address=0x%08x)\n", address);
    }
}

uint32_t nv3_pfb_config0_read()
{
    return nv3->pfb.config_0;
}

void nv3_pfb_config0_write(uint32_t val)
{
    nv3->pfb.config_0 = val;

    // i think the actual size and pixel depth are set in PRAMDAC
    // so we don't update things here for now

    uint32_t new_pfb_htotal = (nv3->pfb.config_0 & 0x3F) << 5;
    // i don't think 16:9 is supported
    uint32_t new_pfb_vtotal = new_pfb_htotal * (4.0/3.0); 
    uint32_t new_bit_depth = (nv3->pfb.config_0 >> 8) & 0x03;

    nv_log("Framebuffer Config Change\n");
    nv_log("Horizontal Size=%d pixels\n", new_pfb_htotal); 
    nv_log("Vertical Size @ 4:3=%d pixels\n", new_pfb_vtotal); 
    
    if (new_bit_depth == NV3_PFB_CONFIG_0_DEPTH_8BPP)
        nv_log("Bit Depth=8bpp\n");
    else if (new_bit_depth == NV3_PFB_CONFIG_0_DEPTH_16BPP)
        nv_log("Bit Depth=16bpp\n");
    else if (new_bit_depth == NV3_PFB_CONFIG_0_DEPTH_32BPP)
        nv_log("Bit Depth=32bpp\n");
        
}