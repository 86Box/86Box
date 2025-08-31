/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 * 
 *          Riva TNT hardware defines
 * 
 * Authors: Connor Hyde <mario64crashed@gmail.com>
 *
 *          Copyright 2024-2025 Connor Hyde
 */


#pragma once

extern const device_config_t nv4_config[];                              // Config for RIVA 128 (revision A/B)

//
// General
//
#define NV4_VRAM_SIZE_2MB                               0x200000 // 2MB (never used; NV4 only)
#define NV4_VRAM_SIZE_4MB                               0x400000 // 4MB (never used)
#define NV4_VRAM_SIZE_8MB                               0x800000 // 8MB
#define NV4_VRAM_SIZE_16MB                              0x1000000 // 16MB
#define NV5_VRAM_SIZE_32MB                              0x2000000 // NV5 only

#define NV4_MMIO_SIZE                                   0x1000000 // not sure. May be larger!!!!

//
// VBIOS
//
#define NV4_VBIOS_STB_REVA                              "roms/video/nvidia/nv4/NV4_STB_velocity.rom" 

//
// PMC
//

#define NV4_PMC_START                                   0x0

#define NV4_PMC_INTR                                    0x100
#define NV4_PMC_INTR_PMEDIA_PENDING                     4
#define NV4_PMC_INTR_PFIFO_PENDING                      8
#define NV4_PMC_INTR_PGRAPH_PENDING                     12
#define NV4_PMC_INTR_PVIDEO_PENDING                     16
#define NV4_PMC_INTR_PTIMER_PENDING                     20
#define NV4_PMC_INTR_PCRTC_PENDING                      24
#define NV4_PMC_INTR_PBUS_PENDING                       28
#define NV4_PMC_INTR_SOFTWARE_PENDING                   31

#define NV4_PMC_INTR_EN                                 0x140
#define NV4_PMC_INTR_EN_DISABLED                        0x0
#define NV4_PMC_INTR_EN_SOFTWARE                        0x1
#define NV4_PMC_INTR_EN_HARDWARE                        0x2
#define NV4_PMC_INTR_EN_ALL                             0x3

#define NV4_PMC_BOOT                                    0x0
#define NV4_PMC_ENABLE                                  0x200
#define NV4_PMC_ENABLE_PMEDIA                           4               // Enable mediaport external MPEG decoder engine
#define NV4_PMC_ENABLE_PFIFO                            8               // Enable FIFO
#define NV4_PMC_ENABLE_PGRAPH                           12
#define NV4_PMC_ENABLE_PPMI                             16
#define NV4_PMC_ENABLE_PFB                              20
#define NV4_PMC_ENABLE_PCRTC                            24
#define NV4_PMC_ENABLE_PVIDEO                           28

//
// PFB
//

#define NV4_PFB_START                                   0x100000
#define NV4_PFB_BOOT                                    0x100000
#define NV4_PFB_BOOT_RAM_AMOUNT                         0
#define NV4_PFB_BOOT_RAM_AMOUNT_2MB                     0x0
#define NV4_PFB_BOOT_RAM_AMOUNT_4MB                     0x1
#define NV4_PFB_BOOT_RAM_AMOUNT_8MB                     0x2
#define NV4_PFB_BOOT_RAM_AMOUNT_16MB                    0x3
#define NV5_PFB_BOOT_RAM_AMOUNT_32MB                    0x0

#define NV4_PSTRAPS                                     0x101000

#define NV4_PSTRAPS_CRYSTAL                             6
#define NV4_PSTRAPS_CRYSTAL_13500K                      0x0
#define NV4_PSTRAPS_CRYSTAL_14318180                    0x1

//
// PRAMDAC
//

#define NV4_PRAMDAC_START                               0x680300
#define NV4_PRAMDAC_CURSOR_START_POSITION               0x680300

#define NV4_PRAMDAC_CURSOR_SIZE_X                       32
#define NV4_PRAMDAC_CURSOR_SIZE_Y                       32

// Same for all 3 clocks
#define NV4_PRAMDAC_CLOCK_VDIV                          0
#define NV4_PRAMDAC_CLOCK_NDIV                          8
#define NV4_PRAMDAC_CLOCK_PDIV                          16

#define NV4_PRAMDAC_CLOCK_CORE                          0x680500        // NVPLL
#define NV4_PRAMDAC_CLOCK_MEMORY                        0x680504
#define NV4_PRAMDAC_CLOCK_PIXEL                         0x680508
#define NV4_PRAMDAC_COEFF_SELECT                        0x68050C
#define NV4_PRAMDAC_COEFF_SELECT_VPLL_SOURCE            0
#define NV4_PRAMDAC_COEFF_SELECT_VPLL_SOURCE_XTAL       0x0
#define NV4_PRAMDAC_COEFF_SELECT_VPLL_SOURCE_VIP        0x1
#define NV4_PRAMDAC_COEFF_SELECT_SOURCE                 8               // Bit not set = hardware, otherwise software
#define NV4_PRAMDAC_COEFF_SELECT_MPLL_IS_SOFTWARE       0x1
#define NV4_PRAMDAC_COEFF_SELECT_VPLL_IS_SOFTWARE       0x2
#define NV4_PRAMDAC_COEFF_SELECT_NVPLL_IS_SOFTWARE      0x4
#define NV4_PRAMDAC_COEFF_SELECT_ALL_SOFTWARE           0x7
#define NV4_PRAMDAC_COEFF_SELECT_VS_PCLK_TV             16
#define NV4_PRAMDAC_COEFF_SELECT_VS_PCLK_TV_NONE        0x0 
#define NV4_PRAMDAC_COEFF_SELECT_VS_PCLK_TV_VSCLK       0x1 
#define NV4_PRAMDAC_COEFF_SELECT_VS_PCLK_TV_PCLK        0x2 
#define NV4_PRAMDAC_COEFF_SELECT_VS_PCLK_TV_BOTH        0x3 
#define NV4_PRAMDAC_COEFF_SELECT_TVCLK_SOURCE           20  
#define NV4_PRAMDAC_COEFF_SELECT_TVCLK_SOURCE_EXT       0x0
#define NV4_PRAMDAC_COEFF_SELECT_TVCLK_SOURCE_VIP       0x1             // VIP = Video Interface Port / Mediaport
#define NV4_PRAMDAC_COEFF_SELECT_TVCLK_RATIO            24 
#define NV4_PRAMDAC_COEFF_SELECT_TVCLK_RATIO_DB1        0x0
#define NV4_PRAMDAC_COEFF_SELECT_TVCLK_RATIO_DB2        0x1 
#define NV4_PRAMDAC_COEFF_SELECT_VCLK_RATIO             28 
#define NV4_PRAMDAC_COEFF_SELECT_VCLK_RATIO_DB1         0x0 
#define NV4_PRAMDAC_COEFF_SELECT_VCLK_RATIO_DB2         0x1 

#define NV4_PRAMDAC_GENERAL_CONTROL                     0x680600
#define NV4_PRAMDAC_GENERAL_CONTROL_ALT_MODE            12

#define NV4_RAMIN_START                                 0x700000        // Nominal. In reality PROM is here on real NV4

// Device Core
void        nv4_init();
void        nv4_close(void* priv);
void        nv4_speed_changed(void *priv);
void        nv4_draw_cursor(svga_t* svga, int32_t drawline);
void        nv4_recalc_timings(svga_t* svga);
void        nv4_force_redraw(void* priv);