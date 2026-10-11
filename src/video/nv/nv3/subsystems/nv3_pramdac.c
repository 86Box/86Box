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
 *
 * Authors: Connor Hyde, <mario64crashed@gmail.com> I need a better email address ;^)
 *          Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2024-2025 starfrost
 *          Copyright 2026 Marcos Alves
 */

// nv3_pramdac.c: NV3 RAMDAC
// Todo: Allow overridability using 68050C register...

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <86box/86box.h>
#include <86box/plat_unused.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h> // DEPENDENT!!!
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>

void nv3_pramdac_init(void)
{
    nv_log("Initialising PRAMDAC\n");

    /* Until the BIOS programs them: MPLL at the 100 MHz the BIOS POST sets (0x1A30B with the
       13.5 MHz crystal, 0x1C40E with 14.318 MHz), VPLL near
       the VGA 25.175 MHz. (The old default had P = 12: a 94 kHz memory clock.) */
    if (nv3_pstraps_crystal_hz() > 14000000.0) {
        nv3_pramdac_set_core_clock_register(0x1C40E);
        nv3_pramdac_set_pixel_clock_register(0x0331D);   /* 14.318 x 51 / 29 = 25.18 MHz */
    } else {
        nv3_pramdac_set_core_clock_register(0x1A30B);    /* 13.5 x 163 / (11 x 2) = 100.0 MHz */
        nv3_pramdac_set_pixel_clock_register(0x1290B);   /* 13.5 x 41 / (11 x 2) = 25.16 MHz */
    }

    nv3_pramdac_set_pixel_clock();
    nv3_pramdac_set_core_clock();

    nv_log("Initialising PRAMDAC: Done\n");
}

// Polls the pixel clock.
void nv3_pramdac_pixel_clock_poll(UNUSED(double real_time))
{
    /* Scan-out is the SVGA core's (nv3_recalc_timings picks the renderer) */
}

// Polls the memory clock.
// This updates the 2D/3D engine PGRAPH, PTIMER and more 
void nv3_pramdac_core_clock_poll(double real_time)
{
    nv3_ptimer_tick(real_time);

    nv3_pfifo_cache0_pull();
    nv3_pfifo_cache1_pull();
    // TODO: UPDATE PGRAPH!
}

// Gets the vram clock register.
uint32_t nv3_pramdac_get_core_clock_register(void)
{
    // the clock format is packed into 19 bits
    // M divisor            [7-0]
    // N divisor            [16-8]
    // P divisor            [18-16]
    return (nv3->pramdac.memory_clock_m)
    + (nv3->pramdac.memory_clock_n << 8)
    + (nv3->pramdac.memory_clock_p << 16); // 0-3
}

uint32_t nv3_pramdac_get_pixel_clock_register(void)
{
    return (nv3->pramdac.pixel_clock_m)
    + (nv3->pramdac.pixel_clock_n << 8)
    + (nv3->pramdac.pixel_clock_p << 16); // 0-3
}

void nv3_pramdac_set_core_clock_register(uint32_t value)
{
    nv3->pramdac.memory_clock_m = value & 0xFF;
    nv3->pramdac.memory_clock_n = (value >> 8) & 0xFF;
    nv3->pramdac.memory_clock_p = (value >> 16) & 0x07;
    
    nv3_pramdac_set_core_clock();
}

void nv3_pramdac_set_pixel_clock_register(uint32_t value)
{
    nv3->pramdac.pixel_clock_m = value & 0xFF;
    nv3->pramdac.pixel_clock_n = (value >> 8) & 0xFF;
    nv3->pramdac.pixel_clock_p = (value >> 16) & 0x07;

    nv3_pramdac_set_pixel_clock();
}

void nv3_pramdac_set_core_clock(void)
{
    // from driver and vbios source
    float frequency = (float) nv3_pstraps_crystal_hz(); /* the strapped reference clock */

    // prevent division by 0
    if (nv3->pramdac.memory_clock_m == 0)
        nv3->pramdac.memory_clock_m = 1;
    
    if (nv3->pramdac.memory_clock_n == 0)
        nv3->pramdac.memory_clock_n = 1;
    
    // Convert to microseconds
    frequency = (frequency * nv3->pramdac.memory_clock_n) / (nv3->pramdac.memory_clock_m << nv3->pramdac.memory_clock_p); 

    double time = 1000000.0  / (double)frequency; // needs to be a double for 86box

    nv_log("Memory clock = %.2f MHz\n", frequency / 1000000.0f);    

    nv3->nvbase.memory_clock_frequency = frequency;
    
    // Create and start if it it's not running.
    if (!nv3->nvbase.memory_clock_timer)
    {
        nv3->nvbase.memory_clock_timer = rivatimer_create(time, nv3_pramdac_core_clock_poll);
        rivatimer_start(nv3->nvbase.memory_clock_timer);
    }

    rivatimer_set_period(nv3->nvbase.memory_clock_timer, time);
}

void nv3_pramdac_set_pixel_clock(void)
{
    // frequency divider algorithm from old varcem/86box/pcbox riva driver,
    // verified by reversing NT drivers v1.50e CalcMNP [symbols] function

    // Technically some Rivas have 14.318mhz crystals so you should check the clock_crystal bits of pfb_boot:
    // if (nv3->pfb.boot.clock_crystal == CLOCK_CRYSTAL_13500)
    //      freq = 13500000.0f;
    // else
    //      freq = 14318000.0f
    // but we really don't need to

    float frequency = (float) nv3_pstraps_crystal_hz(); /* the strapped reference clock */

    // prevent division by 0
    if (nv3->pramdac.pixel_clock_m == 0)
        nv3->pramdac.pixel_clock_m = 1;
    
    if (nv3->pramdac.pixel_clock_n == 0)
        nv3->pramdac.pixel_clock_n = 1;

    frequency = (frequency * nv3->pramdac.pixel_clock_n) / (nv3->pramdac.pixel_clock_m << nv3->pramdac.pixel_clock_p); 

    nv3->nvbase.svga.clock = (cpuclock * (double) (1ULL << 32)) / frequency;

    double time = 1000000.0 / (double)frequency; // needs to be a double for 86box

    nv_log("Pixel clock = %.2f MHz\n", frequency / 1000000.0f);

    nv3->nvbase.pixel_clock_frequency = frequency;

    // Create and start if it it's not running.
    if (!nv3->nvbase.pixel_clock_timer)
    {
        nv3->nvbase.pixel_clock_timer = rivatimer_create(time, nv3_pramdac_pixel_clock_poll);
        rivatimer_start(nv3->nvbase.pixel_clock_timer);
    }

    rivatimer_set_period(nv3->nvbase.pixel_clock_timer, time);
}

//
// ****** Read/Write functions start ******
//

uint32_t nv3_pramdac_read(uint32_t address) 
{ 
    uint32_t ret = 0x00;

    // most of these just need to be read/written
    switch (address)
    {
        case NV3_PRAMDAC_COEFF_SELECT:
            ret = nv3->pramdac.coeff_select;
            break;
        case NV3_PRAMDAC_GENERAL_CONTROL:
            ret = nv3->pramdac.general_control;
            break;
        case NV3_PRAMDAC_VSERR_WIDTH:
            ret = nv3->pramdac.vserr_width;
            break;
        case NV3_PRAMDAC_VBBLANK_END:
            ret = nv3->pramdac.vbblank_end;
            break;
        case NV3_PRAMDAC_VBLANK_END:
            ret = nv3->pramdac.vblank_end;
            break;
        case NV3_PRAMDAC_VBLANK_START:
            ret = nv3->pramdac.vblank_start;
            break;
        case NV3_PRAMDAC_VEQU_START:
            ret = nv3->pramdac.vequ_start;
            break;
        case NV3_PRAMDAC_VTOTAL:
            ret = nv3->pramdac.vtotal;
            break;
        case NV3_PRAMDAC_HSYNC_WIDTH:
            ret = nv3->pramdac.hsync_width;
            break;
        case NV3_PRAMDAC_HBURST_START:
            ret = nv3->pramdac.hburst_start;
            break;
        case NV3_PRAMDAC_HBURST_END:
            ret = nv3->pramdac.hburst_end;
            break;
        case NV3_PRAMDAC_HBLANK_START:
            ret = nv3->pramdac.hblank_start;
            break;
        case NV3_PRAMDAC_HBLANK_END:
            ret =  nv3->pramdac.hblank_end;
            break;
        case NV3_PRAMDAC_HTOTAL:
            ret = nv3->pramdac.htotal;
            break;
        case NV3_PRAMDAC_HEQU_WIDTH:
            ret = nv3->pramdac.hequ_width;
            break;
        case NV3_PRAMDAC_HSERR_WIDTH:
            ret = nv3->pramdac.hserr_width;
            break;
        case NV3_USER_DAC_PIXEL_MASK:
            ret = nv3->pramdac.user_pixel_mask;
            break;
        case NV3_USER_DAC_READ_MODE_ADDRESS:
            ret = nv3->pramdac.user_read_mode_address;
            break; 
        case NV3_USER_DAC_WRITE_MODE_ADDRESS:
            ret = nv3->pramdac.user_write_mode_address;
            break; 
        case NV3_USER_DAC_PALETTE_DATA:  
            /* I doubt NV actually read this in their drivers, but it's worth doing anyway */
            /* Bit 1 is listed as "read or write mode" and 7:0 as "Write-only address", but NV only ever set this to 0 too, so i think this should be fine for now */
            ret = nv3->pramdac.palette[nv3->pramdac.user_read_mode_address];
            nv3->pramdac.user_read_mode_address++; 
            break;
        case NV3_PRAMDAC_CURSOR_START:
            ret = (nv3->pramdac.cursor_start.y << 16) | nv3->pramdac.cursor_start.x;
            break; 
    }

    return ret; 
}

void nv3_pramdac_write(uint32_t address, uint32_t value) 
{

    //s hould be pretty easy to understand
    // we also update the SVGA state here
    switch (address)
    {
        case NV3_PRAMDAC_COEFF_SELECT:
            nv3->pramdac.coeff_select = value;
            break;
        case NV3_PRAMDAC_GENERAL_CONTROL:
            nv3->pramdac.general_control = value;
            /* BPC_8BITS: the CLUT takes 8-bit entries (the driver sets it for its modes) */
            svga_set_ramdac_type(&nv3->nvbase.svga, ((value >> NV3_PRAMDAC_GENERAL_CONTROL_BPC_8BITS) & 1) ? RAMDAC_8BIT : RAMDAC_6BIT);
            svga_recalctimings(&nv3->nvbase.svga);
            break;
        case NV3_PRAMDAC_VSERR_WIDTH:
            //vslines?
            nv3->pramdac.vserr_width = value;
            break;
        case NV3_PRAMDAC_VBBLANK_END:
            nv3->pramdac.vbblank_end = value;
            break;
        case NV3_PRAMDAC_VBLANK_END:
            nv3->pramdac.vblank_end = value;
            break;
        case NV3_PRAMDAC_VBLANK_START:
            //nv3->nvbase.svga.vblankstart = value;
            nv3->pramdac.vblank_start = value;
            break;
        case NV3_PRAMDAC_VEQU_START:
            nv3->pramdac.vequ_start = value;
            break;
        case NV3_PRAMDAC_VTOTAL:
            /* the TV timing generator's (0x680700-0x68073C, used with CR28 TV mode), not the CRTC's */
            nv3->pramdac.vtotal = value & 0x7FF;
            break;
        case NV3_PRAMDAC_HSYNC_WIDTH:
            nv3->pramdac.hsync_width = value;
            break;
        case NV3_PRAMDAC_HBURST_START:
            nv3->pramdac.hburst_start = value;
            break;
        case NV3_PRAMDAC_HBURST_END:
            nv3->pramdac.hburst_end = value;
            break;
        case NV3_PRAMDAC_HBLANK_START:
            //nv3->nvbase.svga.hblankstart = value;
            nv3->pramdac.hblank_start = value;
            break;
        case NV3_PRAMDAC_HBLANK_END:
            //nv3->nvbase.svga.hblank_end_val = value;
            nv3->pramdac.hblank_end = value;
            break;
        case NV3_PRAMDAC_HTOTAL:
            nv3->pramdac.htotal = value;
            //nv3->nvbase.svga.htotal = value;
            break;
        case NV3_PRAMDAC_HEQU_WIDTH:
            nv3->pramdac.hequ_width = value;
            break;
        case NV3_PRAMDAC_HSERR_WIDTH:
            nv3->pramdac.hserr_width = value;
            break;
        case NV3_USER_DAC_PIXEL_MASK:
            nv3->pramdac.user_pixel_mask = value;
            break;
        case NV3_USER_DAC_READ_MODE_ADDRESS:
            nv3->pramdac.user_read_mode_address = value;
            break; 
        case NV3_USER_DAC_WRITE_MODE_ADDRESS:
            /* 
                This seems to get reset to 0 after 256 writes, but, the palette is 768 bytes in size. 
                Clearly there's some mechanism here, but I'm not sure what it is. So let's just reset if we reach 768.
            */
            if (nv3->pramdac.user_write_mode_address >= NV3_USER_DAC_PALETTE_SIZE)
                nv3->pramdac.user_write_mode_address = value;

            break; 
        case NV3_USER_DAC_PALETTE_DATA:  
            /* I doubt NV actually read this in their drivers, but it's worth doing anyway */
            /* Bit 1 is listed as "read or write mode" and 7:0 as "Write-only address", but NV only ever set this to 0 too, so i think this should be fine for now */
            nv3->pramdac.palette[nv3->pramdac.user_write_mode_address] = value;
            nv3->pramdac.user_write_mode_address++; 
            
            break;
        /* cursor start location */
        case NV3_PRAMDAC_CURSOR_START: 
        {
            /* 12-bit signed coordinates: the cursor can hang off the top/left edge */
            svga_t *svga = &nv3->nvbase.svga;

            nv3->pramdac.cursor_start.y = (value >> 16) & 0xFFF;
            nv3->pramdac.cursor_start.x = (value) & 0xFFF;
            svga->hwcursor.x    = ((int32_t) (value & 0xFFF) ^ 0x800) - 0x800;
            svga->hwcursor.y    = ((int32_t) ((value >> 16) & 0xFFF) ^ 0x800) - 0x800;
            svga->hwcursor.xoff = (svga->hwcursor.x < 0) ? -svga->hwcursor.x : 0;
            svga->hwcursor.yoff = (svga->hwcursor.y < 0) ? -svga->hwcursor.y : 0;
            svga->hwcursor.addr = ((svga->crtc[NV3_CRTC_REGISTER_CURSOR_ADDR0] & 0x7F) << 16)
                                | ((svga->crtc[NV3_CRTC_REGISTER_CURSOR_ADDR1] & 0xF8) << 8);
            svga->hwcursor.addr += svga->hwcursor.yoff * NV3_PRAMDAC_CURSOR_SIZE_X * 2;
            break;
        }
    }
}

