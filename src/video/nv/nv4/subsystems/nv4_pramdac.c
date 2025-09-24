/*
* 86Box    A hypervisor and IBM PC system emulator that specializes in
*          running old operating systems and software designed for IBM
*          PC systems and compatibles from 1981 through fairly recent
*          system designs based on the PCI bus.
*
*          This file is part of the 86Box distribution.
*
*          NV4/Riva TNT - RAMDAC
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
#include <86box/nv/vid_nv4.h>


uint64_t nv4_pramdac_get_hz(uint32_t coeff, bool apply_divider)
{
    uint32_t m = coeff & 0xFF;
    uint32_t n = (coeff >> 8) & 0xFF;
    uint32_t p = (coeff >> 16) & 0x07;

    // Check clock base
    uint32_t hz_base = (nv4->straps & (1 << NV4_STRAP_CRYSTAL)) ? 14318180 : 13500000;
    uint32_t final_hz = (hz_base * n) / (m << p);

    // Check VCLK divider

    if (apply_divider)
    {
        if (nv4->pramdac.clk_coeff_select & (1 << NV4_PRAMDAC_PLL_COEFF_SELECT_VCLK_RATIO))
            final_hz >>= 1;
    }

    return final_hz;
}

void nv4_pramdac_set_vclk()
{
    uint64_t final_hz = nv4_pramdac_get_hz(nv4->pramdac.nvclk, false);

    //TODO: Everything
    if (nv4->nvbase.nv4_vclk_timer)
        timer_set_delay_u64(nv4->nvbase.nv4_vclk_timer, final_hz / TIMER_USEC);

}

uint32_t nv4_pramdac_read(uint32_t address)
{
    uint32_t ret = 0x00;

    switch (address)
    {
        case NV4_PRAMDAC_VPLL_COEFF:    // Pixel clock
            ret = nv4->pramdac.vclk;   
            break;
        case NV4_PRAMDAC_NVPLL_COEFF:   // System clock
            ret = nv4->pramdac.nvclk;  
            break;
        case NV4_PRAMDAC_MPLL_COEFF:    // Memory clock
            ret = nv4->pramdac.mclk;
            break;
        
    }
}

void nv4_pramdac_write(uint32_t address, uint32_t data)
{

}
