/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          NVIDIA RIVA TNT / TNT2 -- PRAMDAC.
 *
 *          The three PLLs (NVCLK for the engine, MCLK for memory, VCLK
 *          for the pixels), the hardware cursor position, the general
 *          control register (palette width, 555/565), the test and TV
 *          timing registers (stored).
 *
 *          A PLL runs at Fin * N / M / 2^P, with Fin the strapped
 *          crystal. Until PLL_COEFF_SELECT picks the programmed
 *          coefficients a PLL runs at its built-in default; for the VPLL
 *          that is the VGA clock MISC bits 3:2 select.
 *
 * Authors: Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2026 Marcos Alves
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/nv/vid_nv4.h>

#define NV4_PLL_SELECT_PROG_MPLL  (1u << 8)
#define NV4_PLL_SELECT_PROG_VPLL  (1u << 9)
#define NV4_PLL_SELECT_PROG_NVPLL (1u << 10)

/* The engine and memory clocks the boards run before a driver touches
   the PLLs: the video BIOS's init script programs these (90 and
   110 MHz on the TNT; the TNT2 boards run faster, which matters to no
   guest-visible behavior here beyond PTIMER, whose rate drivers derive
   from the same register). */
#define NV4_DEFAULT_NVCLK 90000000.0
#define NV4_DEFAULT_MCLK  110000000.0

double
nv4_pll_hz(const nv4_t *nv4, uint32_t coeff)
{
    uint32_t m = coeff & 0xff;
    uint32_t n = (coeff >> 8) & 0xff;
    uint32_t p = (coeff >> 16) & 0x07;

    if (!m || !n)
        return 0.0;
    return nv4_crystal_hz(nv4) * (double) n / (double) m / (double) (1u << p);
}

static void
nv4_pramdac_update_clocks(nv4_t *nv4)
{
    double f;

    f             = (nv4->pll_select & NV4_PLL_SELECT_PROG_NVPLL) ? nv4_pll_hz(nv4, nv4->nvpll) : 0.0;
    nv4->nvclk_hz = (f > 0.0) ? f : NV4_DEFAULT_NVCLK;
    f             = (nv4->pll_select & NV4_PLL_SELECT_PROG_MPLL) ? nv4_pll_hz(nv4, nv4->mpll) : 0.0;
    nv4->mclk_hz  = (f > 0.0) ? f : NV4_DEFAULT_MCLK;
    nv4->vclk_hz  = (nv4->pll_select & NV4_PLL_SELECT_PROG_VPLL) ? nv4_pll_hz(nv4, nv4->vpll) : 0.0;
}

/* The palette is 6 or 8 bits per component (GENERAL_CONTROL BPC). */
static void
nv4_pramdac_update_dac(nv4_t *nv4)
{
    svga_set_ramdac_type(&nv4->svga, (nv4->general_control & NV4_PRAMDAC_GC_BPC_8BITS) ? RAMDAC_8BIT : RAMDAC_6BIT);
}

void
nv4_pramdac_reset(nv4_t *nv4)
{
    nv4->cursor_pos      = 0;
    nv4->nvpll           = 0;
    nv4->mpll            = 0;
    nv4->vpll            = 0;
    nv4->pll_select      = 0;
    nv4->pll_setup       = 0x0000044e;
    nv4->pll_test        = 0;
    nv4->palette_test    = 0;
    nv4->general_control = 0x00000001; /* PIXMIX32_BIT 24 */
    nv4->test_control    = 0;
    memset(nv4->tv_regs, 0, sizeof(nv4->tv_regs));
    nv4_pramdac_update_clocks(nv4);
    nv4_pramdac_update_dac(nv4);
}

uint32_t
nv4_pramdac_read(nv4_t *nv4, uint32_t addr)
{
    switch (addr) {
        case NV4_PRAMDAC_CU_START_POS:
            return nv4->cursor_pos;
        case NV4_PRAMDAC_NVPLL_COEFF:
            return nv4->nvpll;
        case NV4_PRAMDAC_MPLL_COEFF:
            return nv4->mpll;
        case NV4_PRAMDAC_VPLL_COEFF:
            return nv4->vpll;
        case NV4_PRAMDAC_PLL_COEFF_SELECT:
            return nv4->pll_select;
        case NV4_PRAMDAC_PLL_SETUP_CTRL:
            return nv4->pll_setup;
        case NV4_PRAMDAC_PLL_TEST_COUNTER:
            /* the three PLLs report lock */
            return (nv4->pll_test & 0x1131ffff) | 0xe0000000;
        case NV4_PRAMDAC_PALETTE_TEST:
            return nv4->palette_test;
        case NV4_PRAMDAC_GENERAL_CONTROL:
            return nv4->general_control;
        case NV4_PRAMDAC_TEST_CONTROL:
            /* SENSEB: some DAC output loaded, a monitor is attached */
            return nv4->test_control & ~0x10000000;
        case NV4_PRAMDAC_CHECKSUM:
            return 0;
        default:
            if ((addr >= 0x680700) && (addr <= 0x68071c))
                return nv4->tv_regs[(addr - 0x680700) >> 2];
            nv4_log("NV4 PRAMDAC: read %06x\n", addr);
            return 0;
    }
}

void
nv4_pramdac_write(nv4_t *nv4, uint32_t addr, uint32_t val)
{
    switch (addr) {
        case NV4_PRAMDAC_CU_START_POS:
            nv4->cursor_pos = val & 0x0fff0fff;
            nv4->svga.hwcursor.x = ((int) ((val & 0xfff) ^ 0x800)) - 0x800;
            nv4->svga.hwcursor.y = ((int) (((val >> 16) & 0xfff) ^ 0x800)) - 0x800;
            break;
        case NV4_PRAMDAC_NVPLL_COEFF:
            nv4->nvpll = val & 0x7ffff;
            nv4_pramdac_update_clocks(nv4);
            break;
        case NV4_PRAMDAC_MPLL_COEFF:
            nv4->mpll = val & 0x7ffff;
            nv4_pramdac_update_clocks(nv4);
            break;
        case NV4_PRAMDAC_VPLL_COEFF:
            nv4->vpll = val & 0x7ffff;
            nv4_pramdac_update_clocks(nv4);
            svga_recalctimings(&nv4->svga);
            break;
        case NV4_PRAMDAC_PLL_COEFF_SELECT:
            nv4->pll_select = val & 0x11130701;
            nv4_pramdac_update_clocks(nv4);
            svga_recalctimings(&nv4->svga);
            break;
        case NV4_PRAMDAC_PLL_SETUP_CTRL:
            nv4->pll_setup = val & 0x77ff;
            break;
        case NV4_PRAMDAC_PLL_TEST_COUNTER:
            nv4->pll_test = val;
            break;
        case NV4_PRAMDAC_PALETTE_TEST:
            nv4->palette_test = val & 0x11000000;
            break;
        case NV4_PRAMDAC_GENERAL_CONTROL:
            nv4->general_control = val & 0x11131131;
            nv4_pramdac_update_dac(nv4);
            svga_recalctimings(&nv4->svga);
            break;
        case NV4_PRAMDAC_TEST_CONTROL:
            nv4->test_control = val & 0x03111311;
            break;
        default:
            if ((addr >= 0x680700) && (addr <= 0x68071c))
                nv4->tv_regs[(addr - 0x680700) >> 2] = val;
            else
                nv4_log("NV4 PRAMDAC: write %06x = %08x\n", addr, val);
            break;
    }
}
