/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 * 
 *          Quadratic Heaven
 * 
 * Authors: Connor Hyde <mario64crashed@gmail.com>
 *
 *          Copyright 2024-2025 Connor Hyde
 */

#pragma once 

#include <stdbool.h>
#include <stdint.h>

extern const device_config_t nv1_config[];                              // Config for RIVA 128 (revision A/B)

//
// PCI Bus Information
//

#define NV1_PCI_BAR0_SIZE                           0xFFFFFF

// 
// VRAM
//
#define NV1_VRAM_SIZE_1MB                           0x100000
#define NV1_VRAM_SIZE_2MB                           0x200000
#define NV1_VRAM_SIZE_4MB                           0x400000

// Video BIOS
#define NV1_VBIOS_E3D_2X00                          "roms/video/nvidia/nv1/Diamond_Edge_3D_2x00.BIN" 
#define NV1_VBIOS_E3D_3X00                          "roms/video/nvidia/nv1/Diamond_Edge_3D_3400_BIOS_M27C256.BIN" 

//
// PMC
// 
#define NV1_PMC_BOOT_0                              0x0
#define NV1_PMC_BOOT_0_REVISION                     0

#define NV1_PMC_BOOT_0_REVISION_A                   0x0             // Prototype (1994)
#define NV1_PMC_BOOT_0_REVISION_B1                  0x0             // Prototype (early 1995)
#define NV1_PMC_BOOT_0_REVISION_B2                  0x0             // Prototype (mid 1995)
#define NV1_PMC_BOOT_0_REVISION_B3                  0x0             // Final?
#define NV1_PMC_BOOT_0_REVISION_C                   0x0             // Final?

#define NV1_PMC_BOOT_0_IMPLEMENTATION               8  
#define NV1_PMC_BOOT_0_IMPLEMENTATION_NV0           0x1             // Nvidia Hardware Simulator (1993-1994)
#define NV1_PMC_BOOT_0_IMPLEMENTATION_NV1_D32       0x2             // NV1 + DRAM + SGS-Thomson STG-1732/1764 DAC
#define NV1_PMC_BOOT_0_IMPLEMENTATION_NV1_V32       0x3             // NV1 + VRAM + SGS-Thomson STG-1732/1764 DAC
#define NV1_PMC_BOOT_0_IMPLEMENTATION_PICASSO       0x4             // NV1 + VRAM + NV 128-bit DAC


// Defines the NV architecture version (NV1/NV2/...)
#define NV1_PMC_BOOT_0_ARCHITECTURE                 16
#define NV1_PMC_BOOT_0_ARCHITECTURE_NV0             0x0             // Nvidia Hardware Simulator (1993-1994)   
#define NV1_PMC_BOOT_0_ARCHITECTURE_NV1             0x1             // NV1 (1995)
#define NV1_PMC_BOOT_0_ARCHITECTURE_NV2             0x2             // Mutara (1996, cancelled)

#define NV1_PMC_DEBUG_0                             0x80

#define NV1_PMC_INTR_0                              0x100
#define NV1_PMC_INTR_EN_0                           0x140

#define NV1_PMC_INTR_EN_0_INTA                      0
#define NV1_PMC_INTR_EN_0_INTB                      4
#define NV1_PMC_INTR_EN_0_INTC                      8
#define NV1_PMC_INTR_EN_0_INTD                      12

#define NV1_PMC_INTR_EN_0_DISABLED                  0x0
#define NV1_PMC_INTR_EN_0_HARDWARE                  0x1
#define NV1_PMC_INTR_EN_0_SOFTWARE                  0x2
#define NV1_PMC_INTR_EN_0_ALL                       0x3             // (HARDWARE | SOFTWARE)

#define NV1_PMC_INTR_READ                           0x160

//TODO: DEFINE bits
#define NV1_PMC_ENABLE                              0x200

//
// PRAMIN
//

#define NV1_RAMIN_START                            0x100000

//
// PAUTH
// Scary nvidia mode
//

// Read only
#define NV1_PAUTH_DEBUG_0                           0x605080
#define NV1_PAUTH_DEBUG_0_BREACH_DETECTED           0
#define NV1_PAUTH_DEBUG_0_EEPROM_INVALID            4

#define NV1_PAUTH_CHIP_TOKEN_0                      0x605400
#define NV1_PAUTH_CHIP_TOKEN_1                      0x605404
#define NV1_PAUTH_PASSWORD_0(i)                     0x605800+(i*16)
#define NV1_PAUTH_PASSWORD_1(i)                     0x605804+(i*16)
#define NV1_PAUTH_PASSWORD_2(i)                     0x605808+(i*16)
#define NV1_PAUTH_PASSWORD_3(i)                     0x60580C+(i*16)

#define NV1_PAUTH_PASSWORD_SIZE                     128

// 
// PFB
//

#define NV1_PFB_BOOT_0                              0x600000

#define NV1_PFB_BOOT_0_RAM_AMOUNT                   0
#define NV1_PFB_BOOT_0_RAM_AMOUNT_1MB               0x0
#define NV1_PFB_BOOT_0_RAM_AMOUNT_2MB               0x1
#define NV1_PFB_BOOT_0_RAM_AMOUNT_4MB               0x2

//
// PEXTDEV
//

#define NV1_STRAPS                                  0x608000
#define NV1_STRAPS_STRAP_VENDOR                     0

//
// PRAM+RAMIN
//

#define NV1_PRAM_CONFIG                             0x602200
#define NV1_PRAM_CONFIG_SIZE                        0
#define NV1_PRAM_CONFIG_12KB                        0
#define NV1_PRAM_CONFIG_20KB                        1
#define NV1_PRAM_CONFIG_36KB                        2
#define NV1_PRAM_CONFIG_68KB                        3

// Position of RAMPW in RAMIN
#define NV1_RAMPW_POSITION_CONFIG0                  0x2c00
#define NV1_RAMPW_POSITION_CONFIG1                  0x4c00
#define NV1_RAMPW_POSITION_CONFIG2                  0x8c00
#define NV1_RAMPW_POSITION_CONFIG3                  0x10c00

// Static RAMPW mirror
#define NV1_PRAMPW                                  0x606000
#define NV1_RAMPW_SIZE                              0x400

//
// PROM
//
#define NV1_PROM                                    0x601000
#define NV1_PROM_SIZE                               32768

// Structures
typedef struct nv1_s
{
    nv_base_t nvbase;   // Base Nvidia structure
} nv1_t;

// Device Core
void        nv1_init();
void        nv1_close(void* priv);
void        nv1_speed_changed(void *priv);
void        nv1_draw_cursor(svga_t* svga, int32_t drawline);
void        nv1_recalc_timings(svga_t* svga);
void        nv1_force_redraw(void* priv);