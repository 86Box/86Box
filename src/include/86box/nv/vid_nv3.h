/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 * 
 *          Welcome to what happens when a single person demands that their overly complicated "vision" of a design be implemented 
 *          with absolutely no compromise. This is true lunacy.
 * 
 *          Explanation of what is being done here, and how this all works, hopefully posted on the 86Box blog.
 *          Notes specific to a subsystem in the header or c file for that subsystem
 *          Also check the doc folder for some more notres
 * 
 *          vid_nv3.h:      Defines a lot of shit
 *  
 * Authors: Connor Hyde <mario64crashed@gmail.com>
 *          Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2024-2026 Connor Hyde
 *          Copyright 2026 Marcos Alves
 */

#pragma once
#include <86box/nv/classes/vid_nv3_classes.h>
#include <86box/nv/render/vid_nv3_render.h>

// The GPU base structure
extern const device_config_t nv3_config[];                              // Config for RIVA 128 (revision A/B)
extern const device_config_t nv3t_config[];                             // Config for RIVA 128 ZX (revision C)

#define NV3_MMIO_SIZE                                   0x1000000       // Max MMIO size

#define NV3_LFB_RAMIN_MIRROR_START                      0x400000        // Mirror of ramin (VERIFY ON HARDWARE)
#define NV3_LFB_MIRROR_START                            0x800000        // The second half of LFB(?)
#define NV3_LFB_RAMIN_START                             0xC00000        // RAMIN mapping start
#define NV3_LFB_MAPPING_SIZE                            0x400000        // Size of RAMIN

// THere are 64 DMA channels grouped into 8 "channels" with 8 "subchannels" each. You can only use one channel at a time. An arbitrary number of 8 objects can be submitted.
// Channel 0 is always taken up by NV drivers.

#define NV3_DMA_CHANNELS                                8
#define NV3_DMA_SUBCHANNELS_PER_CHANNEL                 8

#define NV3_DMA_CHANNELS_TOTAL                          0x7F            // This is also used somewhere despite there only being 8*8 = 64 channels

#define NV3_LAST_VALID_GRAPHICS_OBJECT_ID               0x1F

// The class ids are represented with 5 bits in PGRAPH, but 7 bits in PFIFO!
// What...
#define NV3_PFIFO_FIRST_VALID_GRAPHICS_OBJECT_ID        0x40
#define NV3_PFIFO_LAST_VALID_GRAPHICS_OBJECT_ID         0x5F

// Default value for the boot information register.
// Depends on the chip
#define NV3_BOOT_REG_REV_A00                            0x00030100      // todo: format is wrong(?) for nv3a, fix it later
#define NV3_BOOT_REG_REV_B00                            0x00030110
#define NV3_BOOT_REG_REV_C00                            0x00030120

// various vbioses for testing
// Coming soon: MIROmagic Premium BIOS (when I get mine dumped)
//todo: move to hash system

// Oldest one of these - August 8, 1997

// This particular VBIOS is very early and has certain bugs
#define NV3_VBIOS_STB_V128_V160                         "roms/video/nvidia/nv3/stb-velocity128-1.6.bin" // STB Velocity 128 3D (Riva 128) Ver.1.60
#define NV3_VBIOS_ERAZOR_V14700                         "roms/video/nvidia/nv3/OLD_0000.BIN"            // ELSA VICTORY Erazor VBE 3.0 DDC2B DPMS Video BIOS  Ver. 1.47.01 (ZZ/ A/00)
#define NV3_VBIOS_ERAZOR_V15403                         "roms/video/nvidia/nv3/VCERAZOR.BIN"            // ELSA VICTORY Erazor Ver. 1.54.03    [WD/VBE30/DDC2B/DPMS]
#define NV3_VBIOS_ERAZOR_V15500                         "roms/video/nvidia/nv3/Ver15500.rv_"            // ELSA VICTORY Erazor Ver. 1.55.00    [WD/VBE30/DDC2B/DPMS]
#define NV3_VBIOS_DIAMOND_V330_V162                     "roms/video/nvidia/nv3/diamond_v330_rev-e.vbi"  // Diamond Multimedia Systems, Inc. Viper V330 Version 1.62-CO
#define NV3_VBIOS_ASUS_V3000_V151                       "roms/video/nvidia/nv3/riva128_asus.vbi"        // ASUS AGP/3DP-V3000 BIOS 1.51B
#define NV3_VBIOS_STB_V128_V182                         "roms/video/nvidia/nv3/riva128_stb.vbi"         // STB Velocity 128 (RIVA 128) Ver.1.82
#define NV3T_VBIOS_DIAMOND_V330_V182B                   "roms/video/nvidia/nv3/nv3t182b.rom"            // Diamond Multimedia Viper V330 8M BIOS - Version 1.82B
#define NV3T_VBIOS_ASUS_V170                            "roms/video/nvidia/nv3/A170D03T.rom"            // ASUS AGP-V3000 ZXTV BIOS - V1.70D.03 (C) 1996-98 Nvidia Corporation
#define NV3T_VBIOS_REFERENCE_CEK_V171                   "roms/video/nvidia/nv3/BIOS_49_Riva 128"        // Reference BIOS: RIVA 128 ZX BIOS - V1.71B-N (C) 1996-98 NVidia Corporation
#define NV3T_VBIOS_REFERENCE_CEK_V172                   "roms/video/nvidia/nv3/vgasgram.rom"            // Reference(?) BIOS: RIVA 128 ZX BIOS - V1.72B (C) 1996-98 NVidia Corporation
// RIVA 128 ZX dumps (all report PCI 12D2:0018; the 64K ones are a 32K ROM and padding)
#define NV3T_VBIOS_STB_V128ZX_V120                      "roms/video/nvidia/nv3/stb_velocity_128zx_v1_2.bin"        // STB Velocity 128 (NV3T) Ver.1.20
#define NV3T_VBIOS_ASUS_V3000ZX_V170                    "roms/video/nvidia/nv3/asus-agp-v3000zx.vbi"               // ASUS AGP-V3000 ZX: RIVA 128 BIOS - V1.70D.03
#define NV3T_VBIOS_AGP300S_V170Y                        "roms/video/nvidia/nv3/AGP-300S_riva128zx.BIN"             // AGP-300S: RIVA 128 BIOS - V1.70Y
#define NV3T_VBIOS_CHAINTECH_RI20_V172B                 "roms/video/nvidia/nv3/Chaintech-AGP-RI20_riva128zx.BIN"   // Chaintech AGP-RI20: RIVA 128 BIOS - V1.72B
#define NV3T_VBIOS_CREATIVE_CT6730_V1723D               "roms/video/nvidia/nv3/creative-ct6730.vbi"                // Creative CT6730 (Graphics Blaster RIVA 128 ZX): RIVA 128 BIOS V1.72.3D
#define NV3T_VBIOS_REFERENCE_V172D                      "roms/video/nvidia/nv3/r128zx.VBI"                         // RIVA 128 BIOS - V1.72D
#define NV3T_VBIOS_ELSA_ERAZOR_LT_V15800                "roms/video/nvidia/nv3/elsa.vbi"                           // ELSA VICTORY Erazor/LT Ver. 1.58.00 [WD/VBE30/DDC2B/DPMS/ZX/SD]

// The default VBIOS to use
#define NV3_VBIOS_DEFAULT                               NV3_VBIOS_DIAMOND_V330_V162

// Temporary, will be loaded from settings
#define NV3_VRAM_SIZE_2MB                               0x200000 // 2MB
#define NV3_VRAM_SIZE_4MB                               0x400000 // 4MB
#define NV3_VRAM_SIZE_8MB                               0x800000 // NV3T only
// There is also 1mb supported by the card but it was never used

// PCI config
#define NV3_PCI_CFG_VENDOR_ID                           0x0
#define NV3_PCI_CFG_DEVICE_ID                           0x2
#define NV3_PCI_CFG_CAPABILITIES                        0x4

#define NV3_PCI_COMMAND_L_IO                            1
#define NV3_PCI_COMMAND_L_IO_ENABLED                    0x1
#define NV3_PCI_COMMAND_L_MEMORY                        2
#define NV3_PCI_COMMAND_L_MEMORY_ENABLED                0x1

#define NV3_PCI_COMMAND_H_FAST_BACK2BACK                0x01

#define NV3_PCI_STATUS_L_66MHZ_CAPABLE                  0x20
#define NV3_PCI_STATUS_H_DEVSEL_TIMING                  5
#define NV3_PCI_STATUS_H_FAST_DEVSEL_TIMING             0x00

#define NV3_PCI_CFG_REVISION                            0x8

#define NV3_PCI_CFG_REVISION_A00                        0x00 // nv3a January 1997 - engineering sample, had NV1 PAUDIO and other minor incompatibilities
#define NV3_PCI_CFG_REVISION_B00                        0x10 // nv3b September 1997
#define NV3_PCI_CFG_REVISION_C00                        0x20 // todo: verify this - nv3c (nv3t?) / RIVA 128 ZX

#define NV3_PCI_CFG_PROGRAMMING_INTERFACE               0x9
#define NV3_PCI_CFG_SUBCLASS_CODE                       0x0A
#define NV3_PCI_CFG_CLASS_CODE                          0x0B
#define NV3_PCI_CFG_CLASS_CODE_VGA                      0x03

#define NV3_PCI_CFG_CACHE_LINE_SIZE                     0x0C
#define NV3_PCI_CFG_CACHE_LINE_SIZE_DEFAULT_FROM_VBIOS  0x40

#define NV3_PCI_CFG_LATENCY_TIMER                       0x0D
#define NV3_PCI_CFG_HEADER_TYPE                         0x0E
#define NV3_PCI_CFG_BIST                                0x0F

// PCI Bars
#define NV3_PCI_CFG_BAR_PREFETCHABLE                    3
#define NV3_PCI_CFG_BAR_PREFETCHABLE_ENABLED            0x1

#define NV3_PCI_CFG_BAR0_L                              0x10
#define NV3_PCI_CFG_BAR0_BYTE1                          0x11
#define NV3_PCI_CFG_BAR0_BYTE2                          0x12
#define NV3_PCI_CFG_BAR0_BASE_ADDRESS                   0x13
#define NV3_PCI_CFG_BAR1_L                              0x14
#define NV3_PCI_CFG_BAR1_BYTE1                          0x15
#define NV3_PCI_CFG_BAR1_BYTE2                          0x16
#define NV3_PCI_CFG_BAR1_BASE_ADDRESS                   0x17
#define NV3_PCI_CFG_BAR_INVALID_START                   0x18
#define NV3_PCI_CFG_BAR_INVALID_END                     0x27
#define NV3_PCI_CFG_SUBSYSTEM_ID                        0x2C

#define NV3_PCI_CFG_ENABLE_VBIOS                        0x30
#define NV3_PCI_CFG_VBIOS_BASE                          0x32 ... 0x33
#define NV3_PCI_CFG_VBIOS_BASE_L                        0x32
#define NV3_PCI_CFG_VBIOS_BASE_H                        0x33

#define NV3_AGP_CAPABILITIES_POINTER                    0x34

#define NV3_PCI_CFG_INT_LINE                            0x3C
#define NV3_PCI_CFG_INT_PIN                             0x3D

#define NV3_PCI_CFG_SUBSYSTEM_ID_MIRROR_START           0x40
#define NV3_PCI_CFG_SUBSYSTEM_ID_MIRROR_END             0x43

#define NV3_PCI_CFG_MIN_GRANT                           0x3E
#define NV3_PCI_CFG_MIN_GRANT_DEFAULT                   0x03
#define NV3_PCI_CFG_MAX_LATENCY                         0x3F
#define NV3_PCI_CFG_MAX_LATENCY_DEFAULT                 0x01

//
// AGP configuration
//

#define NV3_AGP_START                                   0x44

// stupid stupid pci system
#define NV3_AGP_CAPABILITIES_START                      0x44
#define NV3_AGP_CAPABILITIES_CAP_ID                     0x44
#define NV3_AGP_CAPABILITIES_CAP_ID_AGP                 0x02        // "AGP"
#define NV3_AGP_CAPABILITIES_NEXT_PTR                   0x45
#define NV3_AGP_CAPABILITIES_AGP_VERSION                0x46
#define NV3_AGP_CAPABILITIES_AGP_VERSION_MINOR          0
#define NV3_AGP_CAPABILITIES_AGP_VERSION_MAJOR          4
#define NV3_AGP_CAPABILITIES_H                          0x47

#define NV3_AGP_STATUS_RATE                             0x48
#define NV3_AGP_STATUS_RATE_1X_SUPPORTED                0x1
#define NV3_AGP_STATUS_RATE_2X_SUPPORTED                0x2

#define NV3_AGP_STATUS_BYTE1                            0x49
#define NV3_AGP_STATUS_BYTE1_SBA                        1
#define NV3_AGP_STATUS_SBA_SUPPORTED                    0x0
#define NV3_AGP_STATUS_SBA_UNSUPPORTED                  0x1
#define NV3_AGP_STATUS_MAX_REQUESTS                     0x4B
#define NV3_AGP_STATUS_MAX_REQUESTS_AMOUNT              4

#define NV3_AGP_COMMAND                                 0x4C
#define NV3_AGP_COMMAND_DATA_RATE                       0
#define NV3_AGP_COMMAND_DATA_RATE_1X                    0x1
#define NV3_AGP_COMMAND_DATA_RATE_2X                    0x2
#define NV3_AGP_COMMAND_BYTE1                           0x4D
#define NV3_AGP_COMMAND_BYTE1_ENABLE                    0
#define NV3_AGP_COMMAND_BYTE1_ENABLE_DISABLED           0x0
#define NV3_AGP_COMMAND_BYTE1_ENABLE_ENABLED            0x1
#define NV3_AGP_COMMAND_SBA_ENABLE                      1
#define NV3_AGP_COMMAND_SBA_ENABLE_DISABLED             0x0
#define NV3_AGP_COMMAND_SBA_ENABLE_ENABLED              0x1
#define NV3_AGP_COMMAND_REQUEST_DEPTH                   0x4F

#define NV3_AGP_END                                     0x4F

//
// ACPI (NV3T only)
// TODO: IMPLEMENT THIS!!!!!!
//
#define NV3_POWER_CAP_ID                                0x60
#define NV3_POWER_NEXT_PTR                              0x61
#define NV3_POWER_VERSION                               0x62

// "The RIVA128ZX does not physically change its power consumption when
// POWER_STATE is modified."
#define NV3_POWER_STATE                                 0x64

#define NV3_POWER_STATE_D0                              0x0
#define NV3_POWER_STATE_D3HOT                           0x3

// GPU Subsystems
// These most likely correspond to functional blocks in the original design

#define NV3_PMC_START                                   0x0         // Chip Master Control Subsystem

#define NV3_PMC_BOOT                                    0x0         // Boot Configuration

#define NV3_PMC_INTR                        0x100       // Interrupt Control
#define NV3_PMC_INTERRUPT_PAUDIO                        0           // Unused, NV3A only
#define NV3_PMC_INTERRUPT_PAUDIO_PENDING                0x1         // Unused, NV3A only
#define NV3_PMC_INTERRUPT_PMEDIA                        4
#define NV3_PMC_INTERRUPT_PMEDIA_PENDING                0x1
#define NV3_PMC_INTERRUPT_PFIFO                         8
#define NV3_PMC_INTERRUPT_PFIFO_PENDING                 0x1
#define NV3_PMC_INTERRUPT_PGRAPH0                       12
#define NV3_PMC_INTERRUPT_PGRAPH0_PENDING               0x1
#define NV3_PMC_INTERRUPT_PGRAPH1                       13
#define NV3_PMC_INTERRUPT_PGRAPH1_PENDING               0x1
#define NV3_PMC_INTERRUPT_PVIDEO                        16
#define NV3_PMC_INTERRUPT_PVIDEO_PENDING                0x1
#define NV3_PMC_INTERRUPT_PTIMER                        20
#define NV3_PMC_INTERRUPT_PTIMER_PENDING                0x1
#define NV3_PMC_INTERRUPT_PFB                           24
#define NV3_PMC_INTERRUPT_PFB_PENDING                   0x1
#define NV3_PMC_INTERRUPT_PBUS                          28  
#define NV3_PMC_INTERRUPT_PBUS_PENDING                  0x1
#define NV3_PMC_INTERRUPT_SOFTWARE                      31
#define NV3_PMC_INTERRUPT_SOFTWARE_PENDING              0x1
#define NV3_PMC_INTR_EN                        0x140       // Controls global interrupt enable state
#define NV3_PMC_INTR_EN_HARDWARE               0x1         // Determines if hardware interrupts are enabled
#define NV3_PMC_INTR_EN_SOFTWARE               0x2         // Determinse if software interrupts were enabled
#define NV3_PMC_ENABLE                                  0x200       // Determines which gpu subsystems were enabled
#define NV3_PMC_ENABLE_PAUDIO                           0           // UNUSED - PAudio removed in NV3 Stepping B0 
#define NV3_PMC_ENABLE_PAUDIO_ENABLED                   0x1         // UNUSED - PAudio removed in NV3 Stepping B0
#define NV3_PMC_ENABLE_PMEDIA                           4
#define NV3_PMC_ENABLE_PMEDIA_ENABLED                   0x1
#define NV3_PMC_ENABLE_PFIFO                            8
#define NV3_PMC_ENABLE_PFIFO_ENABLED                    0x1
#define NV3_PMC_ENABLE_PGRAPH                           12          // Determines if PGRAPH is enabled.
#define NV3_PMC_ENABLE_PGRAPH_ENABLED                   0x1
#define NV3_PMC_ENABLE_PPMI                             16
#define NV3_PMC_ENABLE_PPMI_ENABLED                     0x1
#define NV3_PMC_ENABLE_PFB                              20
#define NV3_PMC_ENABLE_PFB_ENABLED                      0x1
#define NV3_PMC_ENABLE_PCRTC                            24
#define NV3_PMC_ENABLE_PCRTC_ENABLED                    0x1
#define NV3_PMC_ENABLE_PVIDEO                           28
#define NV3_PMC_ENABLE_PVIDEO_ENABLED                   0x1

#define NV3_PMC_END                                     0xfff       // overlaps with CIO
#define NV3_CIO_START                                   0x3b0       // Legacy SVGA Emulation Subsystem
#define NV3_CIO_END                                     0x3df
#define NV3_PBUS_START                                  0x1000      // Bus Control Subsystem
#define NV3_PBUS_DEBUG_0                                0x1084      // Bus Control Debug
#define NV3_PBUS_INTR                                   0x1100      // Bus Control - Interrupt Status

#define NV3_PBUS_INTR_EN                                0x1140      // Bus Control - Interrupt Enable
#define NV3_PBUS_PCI_START                              0x1800      // PCI mirror start
#define NV3_PBUS_PCI_END                                0x18FF      // PCI mirror end
#define NV3_PBUS_END                                    0x1FFF
#define NV3_PFIFO_START                                 0x2000      // FIFO for DMA Object Submission (uses hashtable to store the objects)

#define NV3_PFIFO_MINIMUM_GUARANTEED_DEPTH                             0x7C

#define NV3_PFIFO_DELAY_0                               0x2040      // PFIFO Config Register
#define NV3_PFIFO_DEBUG_0                               0x2080      // PFIFO Debug Register
#define NV3_PFIFO_CACHE0_ERROR_PENDING                  0
#define NV3_PFIFO_CACHE1_ERROR_PENDING                  4

#define NV3_PFIFO_CACHE1_ACCEL_INDEX_COUNT              NV3_PFIFO_FIRST_ACCEL_OBJECT
#define NV3_PFIFO_CACHE1_PGRAPH_CACHE_FETCH_SIZE        717

#define NV3_PFIFO_INTR                                  0x2100      // FIFO - Interrupt Status
#define NV3_PFIFO_INTR_EN                               0x2140      // FIFO - Interrupt Enable

// PFIFO interrupts
#define NV3_PFIFO_INTR_CACHE_ERROR                      0
#define NV3_PFIFO_INTR_RUNOUT                           4
#define NV3_PFIFO_INTR_RUNOUT_OVERFLOW                  8
#define NV3_PFIFO_INTR_DMA_PUSHER                       12
#define NV3_PFIFO_INTR_DMA_PTE                          16

#define NV3_PFIFO_CONFIG_0                              0x2200
#define NV3_PFIFO_CONFIG_0_DMA_FETCH                    8

#define NV3_PFIFO_CONFIG_RAMHT                          0x2210      // Hashtable for graphics objects config
#define NV3_PFIFO_CONFIG_RAMHT_BASE_ADDRESS             12          // 15:12
#define NV3_PFIFO_CONFIG_RAMHT_BASE_ADDRESS_DEFAULT     0x0
#define NV3_PFIFO_CONFIG_RAMHT_SIZE                     16          // 17:16
#define NV3_PFIFO_CONFIG_RAMHT_SIZE_4K                  0x0
#define NV3_PFIFO_CONFIG_RAMHT_SIZE_8K                  0x1
#define NV3_PFIFO_CONFIG_RAMHT_SIZE_16K                 0x2
#define NV3_PFIFO_CONFIG_RAMHT_SIZE_32K                 0x3

#define NV3_PFIFO_CONFIG_RAMFC                          0x2214
#define NV3_PFIFO_CONFIG_RAMFC_BASE_ADDRESS             9
#define NV3_PFIFO_CONFIG_RAMFC_BASE_ADDRESS_DEFAULT     0x1C00      // Hardcoded in silicon?
#define NV3_PFIFO_CONFIG_RAMRO                          0x2218
#define NV3_PFIFO_CONFIG_RAMRO_BASE_ADDRESS             9
#define NV3_PFIFO_CONFIG_RAMRO_BASE_ADDRESS_DEFAULT     0x1E00      // Hardcoded in silicon?
#define NV3_PFIFO_CONFIG_RAMRO_SIZE                     16
#define NV3_PFIFO_CONFIG_RAMRO_SIZE_512B                0x0
#define NV3_PFIFO_CONFIG_RAMRO_SIZE_8K                  0x1 

#define NV3_PFIFO_RUNOUT_STATUS                         0x2400
#define NV3_PFIFO_RUNOUT_STATUS_RANOUT                  0           // 1 if we fucked up
#define NV3_PFIFO_RUNOUT_STATUS_EMPTY                   4           // 1 if ramro is empty
#define NV3_PFIFO_RUNOUT_STATUS_FULL                    8
#define NV3_PFIFO_RUNOUT_PUT                            0x2410
#define NV3_PFIFO_RUNOUT_PUT_ADDRESS                    3           // 9:3 if small ramfc(?) otherwise 12:3
#define NV3_PFIFO_RUNOUT_GET                            0x2420
#define NV3_PFIFO_RUNOUT_GET_ADDRESS                    3           // 13:3

#define NV3_PFIFO_RUNOUT_RAMIN_ERR                      28          // bit to or with

#define NV3_PFIFO_CACHE0_SIZE                           1           // This is for software-injected notified only!
#define NV3_PFIFO_CACHE1_SIZE_REV_AB                    32
#define NV3_PFIFO_CACHE1_SIZE_REV_C                     64
#define NV3_PFIFO_CACHE1_SIZE_MAX                       NV3_PFIFO_CACHE1_SIZE_REV_C
#define NV3_PFIFO_CACHE_REASSIGNMENT                    0x2500        

#define NV3_PFIFO_CACHE0_PUSH_ENABLED                   0x3000
#define NV3_PFIFO_CACHE0_PUSH_CHANNEL_ID                0x3004
#define NV3_PFIFO_CACHE0_PUT                            0x3010
#define NV3_PFIFO_CACHE0_STATUS                         0x3014
#define NV3_PFIFO_CACHE0_STATUS_EMPTY                   4           // 1 if ramro is empty
#define NV3_PFIFO_CACHE0_STATUS_FULL                    8
#define NV3_PFIFO_CACHE0_PUT_ADDRESS                    2           // 1 bit
#define NV3_PFIFO_CACHE0_PULL0                          0x3040
#define NV3_PFIFO_CACHE0_PULL0_ENABLED                  0
#define NV3_PFIFO_CACHE0_PULL0_HASH_FAILURE             4
#define NV3_PFIFO_CACHE0_PULL0_SOFTWARE_METHOD          8
#define NV3_PFIFO_CACHE0_PULLER_CTX_STATE               0x3050
#define NV3_PFIFO_CACHE0_PULLER_CTX_STATE_DIRTY         4           // 1=dirty 0=clean
#define NV3_PFIFO_CACHE0_GET                            0x3070
#define NV3_PFIFO_CACHE0_GET_ADDRESS                    2           // 1 bit
// Current channel context - cache1
#define NV3_PFIFO_CACHE0_CTX                            0x3080      

#define NV3_PFIFO_CACHE0_METHOD_START                   0x3100
#define NV3_PFIFO_CACHE0_METHOD_END                     0x3200
#define NV3_PFIFO_CACHE0_METHOD_ADDRESS                 2           // 12:2
#define NV3_PFIFO_CACHE0_METHOD_SUBCHANNEL              13          // 15:13
#define NV3_PFIFO_CACHE1_PUSH_ENABLED                   0x3200
#define NV3_PFIFO_CACHE1_PUSH_CHANNEL_ID                0x3204
#define NV3_PFIFO_CACHE1_PUT                            0x3210
#define NV3_PFIFO_CACHE1_PUT_ADDRESS                    2           // 6:2
#define NV3_PFIFO_CACHE1_STATUS                         0x3214
#define NV3_PFIFO_CACHE1_STATUS_RANOUT                  0           // 1 if we fucked up
#define NV3_PFIFO_CACHE1_STATUS_EMPTY                   4           // 1 if ramro is empty
#define NV3_PFIFO_CACHE1_STATUS_FULL                    8
#define NV3_PFIFO_CACHE1_DMA_STATUS                     0x3218
#define NV3_PFIFO_CACHE1_DMA_STATUS_STATE               0
#define NV3_PFIFO_CACHE1_DMA_STATUS_STATE_IDLE          0x00
#define NV3_PFIFO_CACHE1_DMA_STATUS_STATE_RUNNING       0x01
#define NV3_PFIFO_CACHE1_DMA_CONFIG_0                   0x3220
#define NV3_PFIFO_CACHE1_DMA_CONFIG_1                   0x3224
#define NV3_PFIFO_CACHE1_DMA_CONFIG_2                   0x3228
#define NV3_PFIFO_CACHE1_DMA_CONFIG_3                   0x322C
#define NV3_PFIFO_CACHE1_DMA_CONFIG_3_TARGET_NODE       0           // The type of bus we are sending over
#define NV3_PFIFO_CACHE1_DMA_CONFIG_3_TARGET_NODE_PCI   0x02        // The type of bus we are sending over
#define NV3_PFIFO_CACHE1_DMA_CONFIG_3_TARGET_NODE_AGP   0x03        // The type of bus we are sending over

// Why does a gpu need its own translation lookaside buffer and pagetable format. Are they crazy
// Seems to be the same format as the notifier engine
#define NV3_PFIFO_CACHE1_DMA_TLB_TAG                    0x3230
#define NV3_PFIFO_CACHE1_DMA_TLB_PTE                    0x3234      // pagetable entry for dma
#define NV3_PFIFO_CACHE1_DMA_TLB_PTE_IS_PRESENT         1
#define NV3_PFIFO_CACHE1_DMA_TLB_FRAME_ADDRESS          12          // 31:12 
#define NV3_PFIFO_CACHE1_DMA_TLB_PT_BASE                0x3238      // Base of pagetable for DMA
#define NV3_PFIFO_CACHE1_PULL0                          0x3240
//todo: merge stuff
#define NV3_PFIFO_CACHE1_PULL0_ENABLED                  0
#define NV3_PFIFO_CACHE1_PULL0_HASH_FAILURE             4   
#define NV3_PFIFO_CACHE1_PULL0_SOFTWARE_METHOD          8           // 0=software, 1=hardware
#define NV3_PFIFO_CACHE1_PULLER_CTX_STATE               0x3250
#define NV3_PFIFO_CACHE1_PULLER_CTX_STATE_DIRTY         4
#define NV3_PFIFO_CACHE1_GET                            0x3270
#define NV3_PFIFO_CACHE1_GET_ADDRESS                    2           // 6:2

// Current channel context - cache1
#define NV3_PFIFO_CACHE1_CTX_START                      0x3280      
#define NV3_PFIFO_CACHE1_CTX_END                        0x3300      // exclusive: 8 entries, 0x10 apart

#define NV3_PFIFO_CACHE1_METHOD_START                   0x3300      // 32 entries (rev A/B)
#define NV3_PFIFO_CACHE1_METHOD_END                     0x3400
#define NV3_PFIFO_CACHE1_METHOD_START_REV_C             0x3400      // 64 entries (rev C, RIVA 128 ZX)
#define NV3_PFIFO_CACHE1_METHOD_END_REV_C               0x3600
#define NV3_PFIFO_CACHE1_METHOD_ADDRESS                 2           // 12:2
#define NV3_PFIFO_CACHE1_METHOD_SUBCHANNEL              13          // 15:13

#define NV3_PFIFO_FIRST_ACCEL_OBJECT                    VMM_PATH_WINDOWS

#define NV3_PFIFO_END                                   0x3FFF
#define NV3_PRM_START                                   0x4000      // Real-Mode Device Support Subsystem
#define NV3_PRM_INTR                                    0x4100
#define NV3_PRM_INTR_EN                                 0x4140
#define NV3_PRM_END                                     0x4FFF
#define NV3_PRAM_START                                  0x6000      // Local ram/cache?
#define NV3_PRAM_END                                    0x6FFF
#define NV3_PRMIO_START                                 0x7000      // Real-Mode I/O Subsystem
#define NV3_PRMIO_END                                   0x7FFF
#define NV3_PTIMER_START                                0x9000      // Programmable Interval Timer
#define NV3_PTIMER_INTR                                 0x9100
#define NV3_PTIMER_INTR_ALARM                           0           // Alarm interrupt
#define NV3_PTIMER_INTR_EN                              0x9140
#define NV3_PTIMER_NUMERATOR                            0x9200
#define NV3_PTIMER_DENOMINATOR                          0x9210
#define NV3_PTIMER_TIME_0_NSEC                          0x9400      // nanoseconds [31:5]
#define NV3_PTIMER_TIME_1_NSEC                          0x9410      // nanoseconds [28:0]
#define NV3_PTIMER_ALARM_NSEC                           0x9420      // nanoseconds [31:5]
#define NV3_PTIMER_END                                  0x9FFF
#define NV3_VGA_VRAM_START                              0xA0000     // VGA Emulation VRAM
#define NV3_VGA_VRAM_END                                0xBFFFF
#define NV3_VGA_START                                   0xC0000     // VGA Emulation Registers
#define NV3_VGA_END                                     0xC7FFF
#define NV3_PRMVIO_START                                NV3_VGA_START // VGA stuff written from main GPU
#define NV3_PRMVIO_END                                  0xC0400
#define NV3_PFB_START                                   0x100000    // GPU Interface to VRAM
#define NV3_PFB_BOOT                                    0x100000    // Boot registration 
#define NV3_PFB_BOOT_RAM_AMOUNT                         0           // The amount of ram
#define NV3_PFB_BOOT_RAM_AMOUNT_8MB                     0x0         // 1mb in NV3A
#define NV3_PFB_BOOT_RAM_AMOUNT_2MB                     0x1
#define NV3_PFB_BOOT_RAM_AMOUNT_4MB                     0x2
#define NV3_PFB_BOOT_RAM_AMOUNT_UNDEFINED               0x3         // i assume this is used for debug
#define NV3_PFB_BOOT_RAM_WIDTH                          2           // the bus width of the gpu's vram
#define NV3_PFB_BOOT_RAM_WIDTH_64                       0x0         // 64bit
#define NV3_PFB_BOOT_RAM_WIDTH_128                      0x1         // 128bit
#define NV3_PFB_BOOT_RAM_BANKS                          3           // the number of banks
#define NV3_PFB_BOOT_RAM_BANKS_2                        0x0         // 2 banks (seems to be used for 2mb)
#define NV3_PFB_BOOT_RAM_BANKS_4                        0x1         // 4 banks (seems to be used for 4mb)
#define NV3_PFB_BOOT_RAM_DATA_TWIDDLE                   4
#define NV3_PFB_BOOT_RAM_DATA_TWIDDLE_OFF               0x0
#define NV3_PFB_BOOT_RAM_DATA_TWIDDLE_ON                0x1
#define NV3_PFB_BOOT_RAM_EXTENSION                      5
#define NV3_PFB_BOOT_RAM_EXTENSION_NONE                 0x0
#define NV3_PFB_BOOT_RAM_EXTENSION_8MB                  0x1

#define NV3_PFB_DELAY                                   0x100044
#define NV3_PFB_DEBUG_0                                 0x100080    // Debug register for pfb
#define NV3_PFB_GREEN_0                                 0x1000C0

#define NV3_PFB_CONFIG_0                                0x100200    // Framebuffer interface config register 0

// What is this lol
// ??? Part of the memory timings
#define NV3_PFB_RTL                                     0x100300
#define NV3_PFB_CONFIG_0_RESOLUTION                     0
// 1=40 horiz. resolution
// i assume it can be divided by some kind of divisor to produce the vertical resolution (e.g. 3/2 or multiply by 2/3) to get the final 
// horiz is 32*value
// theoretically it should support resolutions from 40-2560 horiz

// WHAT ARE THE TIMINGS: ARE THEY IN THE VBIOS?
#define NV3_PFB_CONFIG_0_HORIZ_RESOLUTION_320           0xA
#define NV3_PFB_CONFIG_0_HORIZ_RESOLUTION_400           0xD
#define NV3_PFB_CONFIG_0_HORIZ_RESOLUTION_480           0xF
#define NV3_PFB_CONFIG_0_HORIZ_RESOLUTION_512           0x10
#define NV3_PFB_CONFIG_0_HORIZ_RESOLUTION_640           0x14
#define NV3_PFB_CONFIG_0_HORIZ_RESOLUTION_800           0x19
#define NV3_PFB_CONFIG_0_HORIZ_RESOLUTION_960           0x1E
#define NV3_PFB_CONFIG_0_HORIZ_RESOLUTION_1024          0x20
#define NV3_PFB_CONFIG_0_HORIZ_RESOLUTION_1152          0x24
#define NV3_PFB_CONFIG_0_HORIZ_RESOLUTION_1280          0x28
#define NV3_PFB_CONFIG_0_HORIZ_RESOLUTION_1600          0x32

#define NV3_PFB_CONFIG_0_PIXEL_DEPTH                    8
#define NV3_PFB_CONFIG_0_DEPTH_8BPP                     0x1
#define NV3_PFB_CONFIG_0_DEPTH_16BPP                    0x2
#define NV3_PFB_CONFIG_0_DEPTH_32BPP                    0x3

#define NV3_PFB_CONFIG_1                                0x100204    // Framebuffer interface config register 1
#define NV3_PFB_END                                     0x100FFF
#define NV3_PSTRAPS_START                               0x101000    // External Devices
#define NV3_PSTRAPS                                     0x101000    // Straps Bits
#define NV3_PSTRAPS_BUS_SPEED                           0           // Configured bus speed
#define NV3_PSTRAPS_BUS_SPEED_33MHZ                     0x0
#define NV3_PSTRAPS_BUS_SPEED_66MHZ                     0x1
#define NV3_PSTRAPS_BIOS                                1           // Is a VBIOS present?
#define NV3_PSTRAPS_BIOS_NOT_PRESENT                    0x0
#define NV3_PSTRAPS_BIOS_PRESENT                        1
#define NV3_PSTRAPS_RAM_TYPE                            2           // Type of RAM module
#define NV3_PSTRAPS_RAM_TYPE_16MBIT                     0x0
#define NV3_PSTRAPS_RAM_TYPE_8MBIT                      0x1         // datasheet FBA[3:2] = 01: 8Mbit SGRAM, 128K x 2 banks x 32
#define NV3_PSTRAPS_NEC_MODE                            3           // PC98?
#define NV3_PSTRAPS_NEC_MODE_DISABLED                   0x0
#define NV3_PSTRAPS_NEC_MODE_ENABLED                    0x1
#define NV3_PSTRAPS_BUS_WIDTH                           4           // Bus width
#define NV3_PSTRAPS_BUS_WIDTH_64BIT                     0x0
#define NV3_PSTRAPS_BUS_WIDTH_128BIT                    0x1
#define NV3_PSTRAPS_BUS_TYPE                            5           // Determines if this is a PCI or AGP card
#define NV3_PSTRAPS_BUS_TYPE_PCI                        0x0
#define NV3_PSTRAPS_BUS_TYPE_AGP                        0x1
#define NV3_PSTRAPS_CRYSTAL                             6           // type of clock crystal
#define NV3_PSTRAPS_CRYSTAL_13500K                      0x0         // 13.5 Mhz
#define NV3_PSTRAPS_CRYSTAL_14318180                    0x1         // 14.318180 Mhz clock crystal
#define NV3_PSTRAPS_TVMODE                              7           // Type of TV signal to put out
#define NV3_PSTRAPS_TVMODE_SECAM                        0x0  
#define NV3_PSTRAPS_TVMODE_NTSC                         0x1
#define NV3_PSTRAPS_TVMODE_PAL                          0x2
#define NV3_PSTRAPS_TVMODE_NONE                         0x3
#define NV3_PSTRAPS_PCI21                               9           // PCI 2.1 (delayed transactions) on NV3, AGP 2X on NV3T
#define NV3_PSTRAPS_AGP2X                               9
#define NV3_PSTRAPS_AGP2X_ENABLED                       0x0
#define NV3_PSTRAPS_AGP2X_DISABLED                      0x1
#define NV3_PSTRAPS_UNUSED                              10
#define NV3_PSTRAPS_OVERWRITE                           11
#define NV3_PSTRAPS_OVERWRITE_DISABLED                  0x0
#define NV3_PSTRAPS_OVERWRITE_ENABLED                   0x1
#define NV3_PSTRAPS_END                                 0x101FFF
#define NV3_PROM_START                                  0x110000    // VBIOS?
#define NV3_PROM_END                                    0x11FFFF
#define NV3_PALT_START                                  0x120000    // ??? but it exists
#define NV3_PALT_END                                    0x12FFFF
#define NV3_PME_START                                   0x200000    // Mediaport 
#define NV3_PME_INTR                                    0x200100    // Mediaport: Interrupt Pending?
#define NV3_PME_INTR_EN                                 0x200140    // Mediaport: Interrupt Enable
#define NV3_PME_END                                     0x200FFF

#define NV3_PGRAPH_START                                0x400000    // Scene graph for 2d/3d rendering...the most important part
// PGRAPH Core

#define NV3_PGRAPH_MAX_BUFFERS                          4

// For these debug registers, 0=Disabled, 1=Enabled

// Debug 0: General
#define NV3_PGRAPH_DEBUG_0                              0x400080
#define NV3_PGRAPH_DEBUG_0_STATE_IN_RESET               0
#define NV3_PGRAPH_DEBUG_0_AP_PIPE_IN_RESET             1
#define NV3_PGRAPH_DEBUG_0_CACHE_IN_RESET               2
#define NV3_PGRAPH_DEBUG_0_3D_PIPE_IN_RESET             3
#define NV3_PGRAPH_DEBUG_0_BULK_READS                   4
#define NV3_PGRAPH_DEBUG_0_TILING                       16
#define NV3_PGRAPH_DEBUG_0_WRITE_ONLY_ROPS_2D           20
#define NV3_PGRAPH_DEBUG_0_WRITE_ONLY_ROPS_3D           21
#define NV3_PGRAPH_DEBUG_0_DRAWDIR_AUT                  24
#define NV3_PGRAPH_DEBUG_0_DRAWDIR_Y                    25
#define NV3_PGRAPH_DEBUG_0_ALPHA_ABORT                  28

// Debug 1: Registers
#define NV3_PGRAPH_DEBUG_1                              0x400084
#define NV3_PGRAPH_DEBUG_1_VOLATILE_RESET_LAST          0
#define NV3_PGRAPH_DEBUG_1_DMA_ACTIVITY_CANCEL          4
#define NV3_PGRAPH_DEBUG_1_TURBO3D_2X                   8
#define NV3_PGRAPH_DEBUG_1_TURBO3D_4X                   9
#define NV3_PGRAPH_DEBUG_1_TRIANGLE_OPS                 12
#define NV3_PGRAPH_DEBUG_1_TRIANGLE_CLIP_OPS            13
#define NV3_PGRAPH_DEBUG_1_INSTANCE                     16
#define NV3_PGRAPH_DEBUG_1_CONTEXT                      20
#define NV3_PGRAPH_DEBUG_1_CACHE_FLUSH                  24
#define NV3_PGRAPH_DEBUG_1_ZCLAMP                       28

// Debug 2: 3D Pipeline
#define NV3_PGRAPH_DEBUG_2                              0x400088
#define NV3_PGRAPH_DEBUG_2_AVOID_READMODIFYWRITE_BLEND  0
#define NV3_PGRAPH_DEBUG_2_DPWR_FIFO                    8
#define NV3_PGRAPH_DEBUG_2_BILINEAR_FILTERING_3D        12
#define NV3_PGRAPH_DEBUG_2_ANISOTROPIC_FILTERING_3D     13
#define NV3_PGRAPH_DEBUG_2_FOG                          14
#define NV3_PGRAPH_DEBUG_2_LIGHTING                     15 // Not sure what this does, maybe hardware t&l was planned
#define NV3_PGRAPH_DEBUG_2_BILINEAR_FILTERING_2D        16
#define NV3_PGRAPH_DEBUG_2_ANISOTROPIC_FILTERING_2D     17
#define NV3_PGRAPH_DEBUG_2_D3D_COALESCE                 20 // coalesce reads/writes for d3d class 0x17
#define NV3_PGRAPH_DEBUG_2_D3D_COALESCE_POINT_ZETA      22 // class 0x18 coalesce
#define NV3_PGRAPH_DEBUG_2_PREFETCH                     24
#define NV3_PGRAPH_DEBUG_2_VOLATILE_RESET               28

// Debug 3: Zeta & Alpha Buffer 
#define NV3_PGRAPH_DEBUG_3                              0x40008C
#define NV3_PGRAPH_DEBUG_3_CULLING                      0
#define NV3_PGRAPH_DEBUG_3_FAST_DATA_D3D                4
#define NV3_PGRAPH_DEBUG_3_FAST_DATA_STRETCH            5
#define NV3_PGRAPH_DEBUG_3_ZFLUSH                       7
#define NV3_PGRAPH_DEBUG_3_AUTOZFLUSH_POINT_ZETA        8
#define NV3_PGRAPH_DEBUG_3_AUTOZFLUSH_D3D               9
#define NV3_PGRAPH_DEBUG_3_SLOT_CONFLICT_POINT_ZETA     10          // Slot conflict handling for POINT_ZETA (class 0x18)
#define NV3_PGRAPH_DEBUG_3_SLOT_CONFLICT_D3D            11          // Slot conflict handling for D3D5_TRI (class 0x17)
#define NV3_PGRAPH_DEBUG_3_EARLY_ZABORT                 12
#define NV3_PGRAPH_DEBUG_3_TRIANGLE_END_FLUSH           13
#define NV3_PGRAPH_DEBUG_3_ZFIFO_NOOP                   14          // ???
#define NV3_PGRAPH_DEBUG_3_DITHER                       15
#define NV3_PGRAPH_DEBUG_3_FORCE_COLOR_BUFFER_READ      16
#define NV3_PGRAPH_DEBUG_3_FORCE_ZETA_BUFFER_READ       17
#define NV3_PGRAPH_DEBUG_3_DATA_CHECK                   20
#define NV3_PGRAPH_DEBUG_3_DATA_CHECK_FAIL              21
#define NV3_PGRAPH_DEBUG_3_FORMAT_CHECK                 22
#define NV3_PGRAPH_DEBUG_3_ALPHA_CHECK                  24

// Interrupt stuff
#define NV3_PGRAPH_INTR_0                               0x400100
#define NV3_PGRAPH_INTR_0_CONTEXT_SWITCH                4           // Channel switch ocured
#define NV3_PGRAPH_INTR_0_VBLANK                        8           // Fired every frame
#define NV3_PGRAPH_INTR_0_VBLANK_ENABLED                0x1         // Is the vblank interrupt enabled?
#define NV3_PGRAPH_INTR_0_SOFTWARE_NOTIFY               28          // Fired on software notification

#define NV3_PGRAPH_INTR_1                               0x400104
#define NV3_PGRAPH_INTR_1_SOFTWARE_METHOD_PENDING       0           // Software or invalid method
#define NV3_PGRAPH_INTR_1_INVALID_DATA                  4           // Invalid data. Not sure when this would be triggered.
#define NV3_PGRAPH_INTR_1_DOUBLE_NOTIFY                 12          // Tried to notify while a notify was pending.
#define NV3_PGRAPH_INTR_1_CTXSW_NOTIFY                  16          // Notify fired for software context

#define NV3_PGRAPH_INTR_EN_0                            0x400140    // Interrupt Control for PGRAPH #1
//todo: add what this does
#define NV3_PGRAPH_INTR_EN_1                            0x400144    // Interrupt Control for PGRAPH #2 (it can receive two at onc)
#define NV3_PGRAPH_CTX_SWITCH                           0x400180    // Holds the current PGRAPH context, switched by context switching

/* Contextual information for pgraph */
#define NV3_PGRAPH_CTX_SWITCH_COLOR_FORMAT              0           // Holds the current color format used for drawing operations.
#define NV3_PGRAPH_CTX_SWITCH_ALPHA                     3           // Holds a boolean indicating in if alpha transparency is currently enabled in drawing operations.
#define NV3_PGRAPH_CTX_SWITCH_MONO_FORMAT               8           // Holds the current color format used for monochome drawing operations.  
#define NV3_PGRAPH_CTX_SWITCH_DAC_BYPASS                9           // Holds if PRAMDAC should be bypassed, and an external DAC drawn.
#define NV3_PGRAPH_CTX_SWITCH_Z_WRITE                   12          // Holds if we should write back to the zbuffer.
#define NV3_PGRAPH_CTX_SWITCH_CHROMA_KEY                13          // Holds the current chroma mask used for drawing operations.
#define NV3_PGRAPH_CTX_SWITCH_PLANE_MASK                14          // Holds the current plane mask used for drawing operations.
#define NV3_PGRAPH_CTX_SWITCH_USER_CLIP                 15          // Holds the user-specified clipping information used for drawing operations.
#define NV3_PGRAPH_CTX_SWITCH_SRC_BUFFER                16          // Holds the buffer ID used for drawing operation (i.e. which bpixel/bpitch/boffset index to use)
#define NV3_PGRAPH_CTX_SWITCH_DST_BUFFER0_ENABLED       20          // Holds a boolean indicating if buffer 0 can be used as the destination for a drawing operation.
#define NV3_PGRAPH_CTX_SWITCH_DST_BUFFER1_ENABLED       21          // Holds a boolean indicating if buffer 1 can be used as the destination for a drawing operation.
#define NV3_PGRAPH_CTX_SWITCH_DST_BUFFER2_ENABLED       22          // Holds a boolean indicating if buffer 2 can be used as the destination for a drawing operation.
#define NV3_PGRAPH_CTX_SWITCH_DST_BUFFER3_ENABLED       23          // Holds a boolean indicating if buffer 3 can be used as the destination for a drawing operation.
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG              24          // ROP type

#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_RSVD0        0x0         // Reserved
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_DST_DST_SRC  0x1 
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_DST_SRC_DST  0x2 
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_DST_SRC_SRC  0x3 
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_SRC_DST_DST  0x4 
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_SRC_DST_SRC  0x5 
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_SRC_SRC_DST  0x6 
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_SRC_SRC_SRC0 0x7 
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_SRC_SRC_SRC1 0x8 
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_SRC_SRC_PAT  0x9 
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_SRC_PAT_SRC  0xA 
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_SRC_PAT_PAT  0xB 
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_PAT_SRC_SRC  0xC 
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_PAT_SRC_PAT  0xD 
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_PAT_PAT_SRC  0xE 
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_RSVD1        0xF 
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_PAT_SRC_DST  0x10 
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_PAT_DST_SRC  0x11 
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_SRC_PAT_DST  0x12 
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_SRC_DST_PAT  0x13 
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_DST_PAT_SRC  0x14 
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_DST_SRC_PAT  0x15 
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_RSVD2        0x16 
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_SRC_BYPASS   0x17        // Ignore
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_BLEND_RSVD0  0x18 
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_BLEND_SRC_DST 0x19 
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_BLEND_DST_SRC 0x1A 
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_BLEND_RSVD1  0x1B 
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_BLEND_RSVD2  0x1C 
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_BLEND_SRC    0x1D 
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_BLEND_RSVD3  0x1E 
#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_BLEND_RSVD4  0x1F       

#define NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG_LAST         0x1F

#define NV3_PGRAPH_CTX_SWITCH_VOLATILE                  31          // HUH

#define NV3_PGRAPH_CONTEXT_CONTROL                      0x400190    // DMA context control
#define NV3_PGRAPH_CONTEXT_CONTROL_CHID_VALID           16          // CTX_USER holds a loaded channel
#define NV3_PGRAPH_CONTEXT_USER                         0x400194    // Current DMA context state, may rename

#define NV3_PGRAPH_CONTEXT_USER_SUBCHANNEL              13
#define NV3_PGRAPH_CONTEXT_USER_CLASS                   16
#define NV3_PGRAPH_CONTEXT_USER_CHANNEL                 24

#define NV3_PGRAPH_CONTEXT_CACHE(i)                     0x4001A0+(i*4)  // Context Cache
#define NV3_PGRAPH_CONTEXT_CACHE_SIZE                   8
// TODO: CLIP0/CLIP1 (8 clips min/max in 32bits)
#define NV3_PGRAPH_ABS_UCLIP_XMIN                       0x40053C    // Clip X minimum
#define NV3_PGRAPH_ABS_UCLIP_YMIN                       0x400540    // Clip Y minimum
#define NV3_PGRAPH_ABS_UCLIP_XMAX                       0x400544    // Clip X maximum
#define NV3_PGRAPH_ABS_UCLIP_YMAX                       0x400548    // Clip Y maximum
#define NV3_PGRAPH_SRC_CANVAS_MIN                       0x400550    // Minimum Source Canvas for Blit, Y=30:16, X=10:0
#define NV3_PGRAPH_SRC_CANVAS_MAX                       0x400554    // Maximum Source Canvas for Blit, Y=30:16, X=10:0
#define NV3_PGRAPH_DST_CANVAS_MIN                       0x400558    // Minimum Destination Canvas for Blit, Y=30:16, X=10:0
#define NV3_PGRAPH_DST_CANVAS_MAX                       0x40055C    // Maximum Destination Canvas for Blit, Y=30:16, X=10:0
#define NV3_PGRAPH_PATTERN_COLOR_0_RGB                  0x400600    
#define NV3_PGRAPH_PATTERN_COLOR_0_ALPHA                0x400604
#define NV3_PGRAPH_PATTERN_COLOR_1_RGB                  0x400608    
#define NV3_PGRAPH_PATTERN_COLOR_1_ALPHA                0x40060C    // pattern color 
#define NV3_PGRAPH_PATTERN_BITMAP_0                     0x400610    // pattern bitmap [31:0]
#define NV3_PGRAPH_PATTERN_BITMAP_1                     0x400614    // pattern bitmap [63:32]
#define NV3_PGRAPH_PATTERN_SHAPE                        0x400618
#define NV3_PGRAPH_ROP3                                 0x400624    // ROP3      
#define NV3_PGRAPH_PLANE_MASK                           0x400628
#define NV3_PGRAPH_CHROMA_KEY                           0x40062C
#define NV3_PGRAPH_SURF_OFFSET(i)                       (0x400630 + ((i) << 2))
#define NV3_PGRAPH_SURF_PITCH(i)                        (0x400650 + ((i) << 2))
#define NV3_PGRAPH_SURF_FORMAT                          0x4006A8    // BPIXEL: a nibble per surface
#define NV3_PGRAPH_D3D_CONFIG                           0x400644
#define NV3_PGRAPH_ALPHACNTRL                           0x4006C8    // D3D CONTROL1: alpha key 7:0, compare 11:8
#define NV3_PGRAPH_BETA                                 0x400640    // Beta factor (30:23 fractional, 22:0 before fraction)
#define NV3_PGRAPH_DMA                                  0x400680
#define NV3_PGRAPH_INSTANCE                             0x400688    // CTX_SWITCH_I: instance whose DMA pointers are loaded
#define NV3_PGRAPH_CTX_SWITCH_C                         0x40068C    // M2MF output DMA instance

// Current notification object for pgraph
#define NV3_PGRAPH_NOTIFY                               0x400684    // Notifier for PGRAPH    
#define NV3_PGRAPH_NOTIFY_INSTANCE                      0
#define NV3_PGRAPH_NOTIFY_REQUEST_PENDING               16
#define NV3_PGRAPH_NOTIFY_REQUEST_TYPE                  20
#define NV3_PGRAPH_NOTIFY_REQUEST_TYPE_HARDWARE         0x0         // anything else is software

#define NV3_PGRAPH_CLIP0_MIN                            0x400690    // Clip for Blitting 0 Min
#define NV3_PGRAPH_CLIP0_MAX                            0x400694    // Clip for Blitting 0 Max
#define NV3_PGRAPH_CLIP1_MIN                            0x400698    // Clip for Blitting 1 Min
#define NV3_PGRAPH_CLIP1_MAX                            0x40069C    // Clip for Blitting 1 Max
#define NV3_PGRAPH_CLIP_MISC                            0x4006A0    // Regions/Render/Complex mode
#define NV3_PGRAPH_FIFO_ACCESS                          0x4006A4    // Is PGRAPH enabled?
#define NV3_PGRAPH_FIFO_ACCESS_DISABLED                 0x0
#define NV3_PGRAPH_FIFO_ACCESS_ENABLED                  0x1
#define NV3_PGRAPH_CLIP_MISC                            0x4006A0    // Miscellaneous clipping information
#define NV3_PGRAPH_STATUS                               0x4006B0    // Current PGRAPH status
#define NV3_PGRAPH_TRAPPED_ADDRESS                      0x4006B4
#define NV3_PGRAPH_TRAPPED_DATA                         0x4006B8
#define NV3_PGRAPH_TRAPPED_INSTANCE                     0x4006BC

#define NV3_PGRAPH_DMA_INTR_0                           0x401100    // PGRAPH DMA Interrupt Status
#define NV3_PGRAPH_DMA_INTR_INSTANCE                    0
#define NV3_PGRAPH_DMA_INTR_PRESENT                     4
#define NV3_PGRAPH_DMA_INTR_PROTECTION                  8
#define NV3_PGRAPH_DMA_INTR_LINEAR                      12
#define NV3_PGRAPH_DMA_INTR_NOTIFY                      16
#define NV3_PGRAPH_DMA_INTR_EN_0                        0x401140    // PGRAPH DMA Interrupt Enable 0

#define NV3_PGRAPH_CACHE_SIZE                           12288       // Size of the internal texture cache

// U* registers are not needed

#define NV3_PGRAPH_REGISTER_END                         0x401FFF    // end of pgraph registers
#define NV3_PGRAPH_REAL_END                             0x5C1FFF

// PRMCIO is redirected to SVGA subsystem
#define NV3_PRMCIO_START                                0x601000
#define NV3_PRMCIO_END                                  0x601FFF

#define NV3_PDAC_START                                  0x680000    // OPTIONAL external DAC
#define NV3_PVIDEO_START                                0x680000    // Video Generation / overlay configuration
#define NV3_PVIDEO_INTR                                 0x680100
#define NV3_PVIDEO_INTR_EN                              0x680140
#define NV3_PVIDEO_FIFO_THRESHOLD                       0x680238
#define NV3_PVIDEO_FIFO_BURST_LENGTH                    0x68023C
#define NV3_PVIDEO_OVERLAY                              0x680244
#define NV3_PVIDEO_OVERLAY_VIDEO_IS_ON                  0
#define NV3_PVIDEO_OVERLAY_KEY_ENABLED                  4
#define NV3_PVIDEO_OVERLAY_FORMAT                       8           // 0 = CCIR, 1 = YUY2

#define NV3_PVIDEO_END                                  0x6802FF
#define NV3_PRAMDAC_START                               0x680300

#define NV3_PRAMDAC_CURSOR_START                        0x680300

#define NV3_PRAMDAC_CURSOR_SIZE_X                       32
#define NV3_PRAMDAC_CURSOR_SIZE_Y                       32

#define NV3_PRAMDAC_CLOCK_MEMORY                        0x680504
#define NV3_PRAMDAC_CLOCK_MEMORY_VDIV                   7:0
#define NV3_PRAMDAC_CLOCK_MEMORY_NDIV                   15:8
#define NV3_PRAMDAC_CLOCK_MEMORY_PDIV                   18:16
#define NV3_PRAMDAC_CLOCK_PIXEL                         0x680508
#define NV3_PRAMDAC_COEFF_SELECT                        0x68050C

#define NV3_PRAMDAC_GENERAL_CONTROL                     0x680600
#define NV3_PRAMDAC_GENERAL_CONTROL_IDC_MODE            4           // 0 = gamma (direct colour through the CLUT), 1 = index
#define NV3_PRAMDAC_GENERAL_CONTROL_565_MODE            12
#define NV3_PRAMDAC_GENERAL_CONTROL_BPC_8BITS           20

// These are all 10-bit values, but aligned to 32bits
// so treating them as 32bit should be fine
#define NV3_PRAMDAC_VSERR_WIDTH                         0x680700
#define NV3_PRAMDAC_VEQU_END                            0x680704
#define NV3_PRAMDAC_VBBLANK_END                         0x680708
#define NV3_PRAMDAC_VBLANK_END                          0x68070C
#define NV3_PRAMDAC_VBLANK_START                        0x680710
#define NV3_PRAMDAC_VBBLANK_START                       0x680714
#define NV3_PRAMDAC_VEQU_START                          0x680718
#define NV3_PRAMDAC_VTOTAL                              0x68071C
#define NV3_PRAMDAC_HSYNC_WIDTH                         0x680720
#define NV3_PRAMDAC_HBURST_START                        0x680724
#define NV3_PRAMDAC_HBURST_END                          0x680728
#define NV3_PRAMDAC_HBLANK_START                        0x68072C
#define NV3_PRAMDAC_HBLANK_END                          0x680730
#define NV3_PRAMDAC_HTOTAL                              0x680734
#define NV3_PRAMDAC_HEQU_WIDTH                          0x680738
#define NV3_PRAMDAC_HSERR_WIDTH                         0x68073C

#define NV3_PRAMDAC_END                                 0x680FFF
#define NV3_PDAC_END                                    0x680FFF    // OPTIONAL external DAC


#define NV3_USER_DAC_START                              0x681200
#define NV3_USER_DAC_PALETTE_START                      0x6813C6
#define NV3_USER_DAC_PIXEL_MASK                         0x6813C6
#define NV3_USER_DAC_READ_MODE_ADDRESS                  0x6813C7    //bit0=read/write mode?
#define NV3_USER_DAC_WRITE_MODE_ADDRESS                 0x6813C8
#define NV3_USER_DAC_PALETTE_DATA                       0x6813C9
#define NV3_USER_DAC_PALETTE_SIZE                       768
#define NV3_USER_DAC_PALETTE_END                        0x6813C9
#define NV3_USER_DAC_END                                0x681FFF

#define NV3_USER_START                                  0x800000    // Mapping for the area where objects are submitted into the FIFO (up to 0x880000?)
#define NV3_USER_END                                    0xFFFFFF

// easier name
#define NV3_OBJECT_SUBMIT_START                         NV3_USER_START
#define NV3_OBJECT_SUBMIT_SUBCHANNEL                    13
#define NV3_OBJECT_SUBMIT_CHANNEL                       16
#define NV3_OBJECT_SUBMIT_END                           NV3_USER_END

// also PDFB (Debug Framebuffer?)
#define NV3_PNVM_START                                  0x1000000   // VRAM access (max 8MB)
#define NV3_PNVM_END                                    0x17FFFFF   

// to be less confusing - NVM = "NV Memory"
#define NV3_VRAM_START                                  NV3_PNVM_START
#define NV3_VRAM_END                                    NV3_PNVM_END

// control structures for dma'd in graphics objects from pfifo
// these all have configurable sizes, define them here
#define NV3_RAMIN_START                                 0x1C00000

#define NV3_RAMIN_RAMHT_START                           0x1C00000   // Hashtable for storing submitted objects
#define NV3_RAMIN_RAMHT_END                             0x1C00FFF
#define NV3_RAMIN_RAMHT_SIZE_0                          0xFFF
#define NV3_RAMIN_RAMHT_SIZE_1                          0x1FFF
#define NV3_RAMIN_RAMHT_SIZE_2                          0x3FFF
#define NV3_RAMIN_RAMHT_SIZE_3                          0x7FFF

/* OBSOLETE AREA for AUDIO probably. DO NOT USE! */
#define NV3_RAMIN_RAMAU_START                           0x1C01000   
#define NV3_RAMIN_RAMAU_END                             0x1C01BFF
#define NV3_RAMIN_RAMFC_START                           0x1C01C00   // context for unused PFIFO DMA channels
#define NV3_RAMIN_RAMFC_END                             0x1C01DFF
#define NV3_RAMIN_RAMFC_SIZE_0                          0x1FF
#define NV3_RAMIN_RAMFC_SIZE_1                          0xFFF
#define NV3_RAMIN_RAMRO_START                           0x1C01E00   // Runout area for invalid submissions
#define NV3_RAMIN_RAMRO_SIZE_0                          0x1FF
#define NV3_RAMIN_RAMRO_SIZE_1                          0x1FFF
#define NV3_RAMIN_RAMRO_END                             0x1C01FFF
#define NV3_RAMIN_RAMRM_START                           0x1C02000
#define NV3_RAMIN_RAMRM_END                             0x1C02FFF

// I'm not sure if this can be moved.
// 4K of "PRAM" is listed at 0x6000.
#define NV3_RAMIN_OFFSET_CURSOR                         0x6000

#define NV3_RAMIN_END                                   0x1FFFFFF

// not done

// CRTC/CIO (0x3b0-0x3df)


#define NV3_CRTC_REGISTER_INDEX_MONO                    0x3B4    
#define NV3_CRTC_REGISTER_MONO                          0x3B5    // Currently Selected CRTC Register - Monochrome 

#define NV3_CRTC_DATA_OUT                               0x3C0
#define NV3_CRTC_MISCOUT                                0x3C2

#define NV3_RMA_REGISTER_START                          0x3D0
#define NV3_RMA_REGISTER_END                            0x3D3

#define NV3_CRTC_REGISTER_INDEX                         0x3D4 
#define NV3_CRTC_REGISTER_CURRENT                       0x3D5

#define NV3_CRTC_REGISTER_WTF                           0x3D8

// These are standard (0-18h)
#define NV3_CRTC_REGISTER_HTOTAL                        0x00
#define NV3_CRTC_REGISTER_HDISPEND                      0x01
#define NV3_CRTC_REGISTER_HBLANKSTART                   0x02
#define NV3_CRTC_REGISTER_HBLANKEND                     0x03
#define NV3_CRTC_REGISTER_HRETRACESTART                 0x04
#define NV3_CRTC_REGISTER_HRETRACEEND                   0x05
#define NV3_CRTC_REGISTER_VTOTAL                        0x06
#define NV3_CRTC_REGISTER_OVERFLOW                      0x07
#define NV3_CRTC_REGISTER_PRESETROWSCAN                 0x08
#define NV3_CRTC_REGISTER_MAXSCAN                       0x09
#define NV3_CRTC_REGISTER_CURSOR_START                  0x0A
#define NV3_CRTC_REGISTER_CURSOR_START_DISABLED         5
#define NV3_CRTC_REGISTER_CURSOR_END                    0x0B
#define NV3_CRTC_REGISTER_STARTADDR_HIGH                0x0C
#define NV3_CRTC_REGISTER_STARTADDR_LOW                 0x0D
#define NV3_CRTC_REGISTER_CURSORLOCATION_HIGH           0x0E
#define NV3_CRTC_REGISTER_CURSORLOCATION_LOW            0x0F
#define NV3_CRTC_REGISTER_VRETRACESTART                 0x10
#define NV3_CRTC_REGISTER_VRETRACEEND                   0x11
#define NV3_CRTC_REGISTER_VDISPEND                      0x12
#define NV3_CRTC_REGISTER_OFFSET                        0x13
#define NV3_CRTC_REGISTER_UNDERLINELOCATION             0x14
#define NV3_CRTC_REGISTER_STARTVBLANK                   0x15
#define NV3_CRTC_REGISTER_ENDVBLANK                     0x16
#define NV3_CRTC_REGISTER_CRTCCONTROL                   0x17
#define NV3_CRTC_REGISTER_LINECOMP                      0x18
#define NV3_CRTC_REGISTER_STANDARDVGA_END               0x18


// These are nvidia, licensed from weitek (25-63)
#define NV3_CRTC_REGISTER_RPC0                          0x19        // 7:5 - [10:8] of CRTC. 4:0 - [20:16] of 21-bit display buffer address
#define NV3_CRTC_REGISTER_RPC1                          0x1A        // bit7=hsync enabled, bit6=vsync enabled, bit4="compatible text", bit2=large screen, bit1=6bit palette width (>1280)
#define NV3_CRTC_REGISTER_READ_BANK                     0x1D
#define NV3_CRTC_REGISTER_WRITE_BANK                    0x1E
#define NV3_CRTC_REGISTER_FORMAT                        0x25
#define NV3_CRTC_REGISTER_FORMAT_VDT10                  0           // Use 10 bit vtotal value instead of 8 bit
#define NV3_CRTC_REGISTER_FORMAT_VDE10                  1           // Use 10 bit dispend value instead of 8 bit
#define NV3_CRTC_REGISTER_FORMAT_VRS10                  2           // Use 10 bit vblank start value instead of 8 bit
#define NV3_CRTC_REGISTER_FORMAT_VBS10                  3           // Use 10 bit vsync start value instead of 8 bit
#define NV3_CRTC_REGISTER_FORMAT_HBE6                   4           // Use 6 bit hsync start value
#define NV3_CRTC_REGISTER_PIXELMODE                     0x28

#define NV3_CRTC_REGISTER_HEB                           0x2D        // HRS most significant bit

#define NV3_CRTC_REGISTER_CURSOR_ADDR0                  0x30        // Cursor high                  21:16
#define NV3_CRTC_REGISTER_CURSOR_ADDR1                  0x31        // Cursor low (1:0 = enable)    15:11

#define NV3_CRTC_REGISTER_PIXELMODE_VGA                 0x00        // vga textmode
#define NV3_CRTC_REGISTER_PIXELMODE_8BPP                0x01
#define NV3_CRTC_REGISTER_PIXELMODE_16BPP               0x02
#define NV3_CRTC_REGISTER_PIXELMODE_32BPP               0x03 

#define NV3_CRTC_REGISTER_RL0                           0x34
#define NV3_CRTC_REGISTER_RL1                           0x35
#define NV3_CRTC_REGISTER_RMA                           0x38        // REAL MODE ACCESS!
#define NV3_CRTC_REGISTER_I2C                           0x3E    
#define NV3_CRTC_REGISTER_I2C_GPIO                      0x3F

#define NV3_CRTC_BANKED_128K_A0000                      0x00
#define NV3_CRTC_BANKED_64K_A0000                       0x04
#define NV3_CRTC_BANKED_32K_B0000                       0x08
#define NV3_CRTC_BANKED_32K_B8000                       0x0C

#define NV3_CRTC_REGISTER_NVIDIA_END                    0x3F

// for 86box 8bit addressing
// get rid of this asap, replace with 32->8 macros
#define NV3_RMA_SIGNATURE_MSB                           0x65
#define NV3_RMA_SIGNATURE_BYTE2                         0xD0
#define NV3_RMA_SIGNATURE_BYTE1                         0x16
#define NV3_RMA_SIGNATURE_LSB                           0x2B

#define NV3_CRTC_REGISTER_RMA_MODE_MAX                  0x0F

/* 
    STRUCTURES FOR THE GPU START HERE
    OBJECT CLASS & RENDERING RELATED STUFF IS IN VID_NV3_CLASSES.H
*/

//todo: pixel format

// Master Control
typedef struct nv3_pmc_s
{
    /* 
    Holds chip manufacturing information at bootup.
    Current specification (may change later): pre-packed for convenience

    Revision A: 
        FIB Revision 1, Mask Revision B0, Implementation 1 [NV3], Architecture 3 [NV3], Manufacturer Nvidia, Foundry SGS (seems to have been the most common?)
        31:28=0000, 27:24=0000, 23:16=0003, 15:8=0001, 7:4=0001, 3:0=0001
        little endian 00000000 00000011 00000001 00010001   = 0x00300111#
    Revision B/c:
        write this later
    */
    int32_t boot;   
    int32_t intr;   // Determines if interrupts are pending for specific subsystems.
    int32_t intr_en;   // Determines if interrupts are actually enabled.
    int32_t enable;             // Determines which subsystems are enabled.

} nv3_pmc_t;

// add enums for eac    
// Chip configuration
typedef struct nv3_straps_s
{
    uint32_t straps;
} nv3_straps_t;

// Framebuffer
typedef struct nv3_pfb_s
{
    uint32_t boot;
    uint32_t debug_0;                   // debug stuff
    uint32_t config_0;                  // Framebuffer width, etc.
    uint32_t config_1;
    uint32_t green;                     // does not seem to do anything
    uint32_t delay;
    uint32_t rtl;                       // Part of the memory timings
} nv3_pfb_t;

//
// DMA & Notifier Engine
// All singing all dancing all happy happy bullshit to send dma transfers literally anywhere in your computer
//

// Not a notification status, because it's a 16-bit enum
// C23 fixes this
#define NV3_NOTIFICATION_STATUS_DONE_OK         0x0
#define NV3_NOTIFICATION_STATUS_IN_PROGRESS     0xFF
#define NV3_NOTIFICATION_STATUS_ERROR           0x100

#define NV3_NOTIFICATION_INFO_ADJUST            0       // Wut
#define NV3_NOTIFICATION_PT_PRESENT             16      // Determines if the pagetable exists.
#define NV3_NOTIFICATION_TARGET                 24      // Determines where this notification goes
#define NV3_DMA_TARGET_NODE_VRAM             0       // VRAM target for DMA
#define NV3_DMA_TARGET_NODE_CART            1       // "Cartridge" target for dma, only mentioned in a few places, !!! NV2 LEFTOVER !!!
#define NV3_DMA_TARGET_NODE_PCI             2       // Send the data to the host system over PCI
#define NV3_DMA_TARGET_NODE_AGP             3       // Send the data to the host system over AGP

#define NV3_NOTIFICATION_PAGE_IS_PRESENT        0       // Determines if the page really exists
#define NV3_NOTIFICATION_PAGE_ACCESS            1       // Determines the page access type
#define NV3_NOTIFICATION_PAGE_ACCESS_READ       0x0     // If this is set, the page is read only
#define NV3_NOTIFICATION_PAGE_ACCESS_READ_WRITE 0x1     // If this is set, the page is read/write
#define NV3_NOTIFICATION_PAGE_FRAME_ADDRESS     12      // The pageframe to use 

// Core notification structure
typedef struct nv3_notification_s
{
    uint64_t nanoseconds;
    uint32_t info32;
    uint16_t info16;
    uint16_t status;
} nv3_notification_t;

#define NV3_RMA_NUM_REGS        4
// Access the GPU from real-mode
typedef struct nv3_pbus_rma_s
{
    uint32_t addr;                      // Address to RMA to
    uint32_t data;                      // Data to send to MMIO
    uint8_t mode;                       // the current state of the rma shifting engin
    uint8_t rma_regs[NV3_RMA_NUM_REGS]; // The rma registers (saved)
} nv3_pbus_rma_t;

// Bus Configuration
typedef struct nv3_pbus_s
{
    uint8_t agp_rq;         // AGP status RQ (config 0x4B): read-only in config space, set through the PBUS alias

    uint32_t debug_0;
    uint32_t intr;          // Interrupt status
    uint32_t intr_en;          // Interrupt enable
    nv3_pbus_rma_t rma;
} nv3_pbus_t;

typedef struct nv3_pfifo_cache_s
{
    bool push0;                         // Can we even access this cache?
    uint32_t put_address;               // Trigger a DMA into the value you put here.
    uint32_t get_address;               // Trigger a DMA from the value you put here into where you were going.
    uint8_t channel;                    // The DMA channel ID of this cache.
    uint32_t status;
    uint32_t pull0;
    uint32_t context[NV3_DMA_SUBCHANNELS_PER_CHANNEL];  // Only one of these exists for cache0

    /* cache1 only 
        do we even need to emulate this?
    */
    /* The DMA pusher (rnndb nv1_pfifo CACHE1): it fetches a push buffer through a page table
       and feeds the commands into CACHE1 like PIO writes to USER would */
    uint32_t dma_status;                // 0x3218 DMA_STATE: method (12:2), subchannel (15:13), methods left (28:18), error (30)
    bool dma_enabled;                   // 0x3220 bit0 (unused: dma_state holds DMA_CTRL)
    bool dma_is_busy;                   // 0x3220 bit4

    uint32_t dma_state;                 // 0x3220 DMA_CTRL: bit 0 = the pusher may fetch
    uint32_t dma_length;                // 0x3224 DMA_COUNT: bytes left to fetch
    uint32_t dma_address;               // 0x3228 DMA_GET: linear offset of the next fetch
    uint32_t dma_target_node;           // 0x322C DMA_TARGET: 2 PCI, 3 AGP
    uint32_t dma_tlb_tag;               // 0x3230 page (linear 31:12) whose PTE is in dma_tlb_pte, ~0 = none
    uint32_t dma_tlb_pte;               // 0x3234
    uint32_t dma_tlb_pt_base;           // 0x3238 DMA_PT_INST: RAMIN byte address of the push buffer's page table
    uint16_t method_address;            // address of the method (i.e. what method it is)
    uint16_t method_subchannel;         // subchannel
    bool context_is_dirty;

    /* TODO */
} nv3_pfifo_cache_t;

extern uint8_t nv3_pfifo_cache1_gray_code_table[];

typedef struct nv3_pfifo_cache_entry_s
{
    uint16_t method;                    // method id depending on class (offset from entry channel start in ramin)
    uint8_t subchannel;                 // subchannel
    uint32_t data;                      // is this the context
} nv3_pfifo_cache_entry_t; 

// Command submission to PGRAPH
typedef struct nv3_pfifo_s
{
    uint32_t intr;          // Interrupt status
    uint32_t intr_en;          // Interrupt enable
    uint32_t dma_delay_retry;           // DMA Delay/Retry
    uint32_t debug_0;                   // Cache Debug register
    uint32_t config_0;
    uint32_t ramht_config;              // RAMHT config
    uint32_t ramht_size;                // not a real register, used to store the size of ramht to reduce code duplication
    uint32_t ramht_location;            // not a real register, used to store the location of ramht to reduce code duplication
    uint32_t ramfc_config;              // RAMFC config
    uint32_t ramro_config;              // RAMRO config
    uint32_t ramro_location;            // not a real register, used to store the location of ramro to reduce code duplication
    uint32_t ramro_size;                // not a real register, used to store the size to of ramro reduce code duplication

    // Runout stuff
    uint32_t runout_put;                // 8:3 if RAMRO=512b, otherwise 12:3
    uint32_t runout_get;                // 8:3 if RAMRO=512b, otherwise 12:3

    // Cache stuff
    uint32_t cache_reassignment;        // Allow context switching
    nv3_pfifo_cache_t cache0_settings;
    nv3_pfifo_cache_t cache1_settings;

    nv3_pfifo_cache_entry_t cache0_entry;                              // It only has 1 entry
    nv3_pfifo_cache_entry_t cache1_entries[NV3_PFIFO_CACHE1_SIZE_MAX]; // ONLY 32 USED ON REVISION A/B CARDS
} nv3_pfifo_t;

// RAMDAC
typedef struct nv3_pramdac_s
{
    // these should be uint8_t but C math is a lot better with this
    uint32_t memory_clock_m;    // memory clock M-divider
    uint32_t memory_clock_n;    // memory clock N-divider
    uint32_t memory_clock_p;    // memory clock P-divider
    uint32_t pixel_clock_m;     // pixel clock M-divider
    uint32_t pixel_clock_n;     // pixel clock N-divider
    uint32_t pixel_clock_p;     // pixel clock P-divider
    uint32_t coeff_select;      // coefficient select

    uint32_t general_control;   // general control register

    // this could duplicate SVGA state but I tihnk it's more readable,
    // we'll just modify both
    uint32_t vserr_width;       // vertical sync error width
    uint32_t vequ_end;          // vequ end (not sure what this is)
    uint32_t vbblank_end;       // vbblank end (not sure what this is)
    uint32_t vblank_end;        // vblank end
    uint32_t vblank_start;      // vblank start
    uint32_t vequ_start;        // vequ start (not sure what this is)
    uint32_t vtotal;            // vertical total lines
    uint32_t hsync_width;       // horizontal sync width
    uint32_t hburst_start;      // horizontal burst signal start (in lines)
    uint32_t hburst_end;        // horizontal burst signal end (in lines)
    uint32_t hblank_start;      // horizontal blank start (in lines)
    uint32_t hblank_end;        // horizontal blank end (in lines)
    uint32_t htotal;            // horizontal total lines
    uint32_t hequ_width;        // horizontal equ width (not sure what this is)
    uint32_t hserr_width;       // horizontal sync error width

    uint8_t user_pixel_mask;                        // pixel mask for DAC lookup
    uint32_t user_read_mode_address;                // user read mode address
    uint32_t user_write_mode_address;               // user write mode address
    uint8_t palette[NV3_USER_DAC_PALETTE_SIZE];     // Palette Info/CLUT - 256 entries, 1 byte for r,g,b = 768 bytes

    uint32_t cursor_address;    // cursor address start
    nv3_coord_16_t cursor_start; 
} nv3_pramdac_t;

typedef struct nv3_pgraph_status_s
{
    bool overall_busy : 1;          // Is anything busy?
    uint8_t reserved : 3;
    bool xy_logic_busy : 1;         // Determines if the line drawing/xy/vector stuff is busy.
    uint8_t reserved2 : 3;
    bool port_notify_busy : 1;      // Mediaport?/PIO? notifier engine busy
    uint8_t reserved3 : 3;
    bool port_register_busy : 1;
    uint8_t reserved4 : 3;
    bool port_dma_busy : 1;         // Mediaport?/PIO? DMA engine busy
    bool dma_engine_busy : 1;       // DMA engine busy
    uint8_t reserved5 : 2;
    bool dma_notify_busy : 1;       // Are the notifiers busy?
    uint8_t reserved6 : 3;
    bool engine_3d_busy : 1;        // 3d engine busy?  
    bool engine_cache_busy : 1;     // PFIFO CACHE0/CACHE1 busy?
    bool engine_zfifo_busy : 1;     // ZFIFO (zeta buffer? z buffer?) busy?
    bool port_user_busy : 1;        // User context switch?
    uint8_t reserved7 : 3;

} nv3_pgraph_status_t;

/* All of this B* stuff is registers at 400630..40065c and 4006a8 in reality, easier to implement it like this 
   BPixel = Buffer Pixel Format
*/
#define NV3_BPIXEL_FORMAT               0
#define NV3_BPIXEL_FORMAT_IS_VALID      2
#define NV3_BPIXEL_DEPTH                4

typedef enum nv3_pgraph_bpixel_format_e
{
    // Y16
    bpixel_fmt_y16 = 0,
    // 8-bit colour
    bpixel_fmt_8bit = 1,
    // 16-bit colour
    bpixel_fmt_16bit = 2,
    // 32-bit colour (ARGB)
    bpixel_fmt_32bit = 3,
} nv3_pgraph_bpixel_format;

/* Primitive state the classes build up over several methods (vertices, colours, the D3D vertex
   file and texture). On the chip it is PGRAPH register state (ABS_X/Y_RAM, ZFOG_RAM, D3D_*), which
   the resman saves and reloads around every channel switch, so each channel keeps
   its own: the emulator does the same with a copy per channel, swapped when CTX_USER's CHID
   changes (nv3_pgraph_write). */
typedef struct nv3_d3d_vertex_s {
    float    x, y, z, rhw, u, v;
    uint32_t color; /* A8R8G8B8 */
    uint8_t  fog;
} nv3_d3d_vertex_t;

typedef struct nv3_line_state_s {
    uint32_t color;
    uint32_t cpoly_color;
    int32_t  x0, y0; /* LINE: first point of the pair */
    int32_t  lx, ly; /* last vertex, where a polyline continues from */
    int32_t  x32[3]; /* LINE32/POLYLINE32 coordinates gathered so far */
} nv3_line_state_t;

typedef struct nv3_d3d_state_s {
    uint32_t tex_offset, tex_format, filter, fog_color;

    /* the vertex being sent */
    uint32_t         fog_tri;
    nv3_d3d_vertex_t cur;
    nv3_d3d_vertex_t vtx[16];

    /* texture pages: valid while the texture state is unchanged */
    bool     tex_mapped;
    uint32_t tex_target;
    uint32_t tex_lin;
    uint32_t tex_pages[64];

    /* class 0x18 */
    int32_t  zpoint_x, zpoint_y;
    uint32_t zpoint_color;
} nv3_d3d_state_t;

typedef struct nv3_prim_state_s {
    /* class 0x08 point */
    uint32_t point_color;
    int32_t  point32_x;
    uint32_t cpoint_color;
    /* classes 0x09 line, 0x0A lin */
    nv3_line_state_t line[2];
    /* class 0x0B triangle */
    uint32_t tri_color;
    uint32_t tri_ccolor;
    int32_t  tri_x[3], tri_y[3];   /* TRIANGLE / CTRIANGLE vertices */
    int32_t  mesh_x[2], mesh_y[2]; /* the last two mesh vertices */
    uint32_t mesh_count;
    int32_t  tri32[5];
    /* classes 0x17, 0x18 */
    nv3_d3d_state_t d3d;
} nv3_prim_state_t;

#define NV3_PGRAPH_CHANNELS 128 /* CTX_USER CHID is 7 bits */

// Graphics Subsystem
typedef struct nv3_pgraph_s
{
    uint32_t debug_0;
    nv3_prim_state_t prim;  // the current channel's primitive state
    uint32_t debug_1;
    uint32_t debug_2;
    uint32_t debug_3;
    uint32_t intr_0;          // Interrupt status 0
    uint32_t intr_en_0;          // Interrupt enable 0 
    uint32_t intr_1;          // Interrupt status 1
    uint32_t intr_en_1;          // Interrupt enable 1
    uint32_t intr_dma;        // Interrupt status for DMA
    uint32_t intr_en_dma;        // Interrupt enable for DMA

    uint32_t context_switch;              // TODO: Make this a struct, it's just going to be enormous lol.
    uint32_t context_control;
    uint32_t context_user;

    uint32_t context_cache[NV3_PGRAPH_CONTEXT_CACHE_SIZE];  // DMA context cache (nv3_pgraph_context_user_t array?)

    // Clipping. The user clip (class 0x05) applies to objects whose context has CLIP set; the canvas always does.
    int32_t uclip_min[2];                                   // x, y (inclusive)
    int32_t uclip_max[2];                                   // x, y (exclusive)
    uint32_t src_canvas_min;                                // Y=29:16, X=10:0
    uint32_t src_canvas_max;
    uint32_t dst_canvas_min;
    uint32_t dst_canvas_max;
    uint32_t cliprect_min[2];                               // WinNT-style clip rectangles, Y=31:16, X=15:0
    uint32_t cliprect_max[2];
    uint32_t cliprect_ctrl;                                 // 1:0 count, 4 inverted
    // Pattern (class 0x06): two mono colours as R10G10B10 plus alpha, and a 64-bit mono bitmap
    uint32_t pattern_mono_rgb[2];
    uint8_t pattern_mono_a[2];
    uint32_t pattern_mono_bitmap[2];
    uint32_t pattern_shape;                                 // 0=8x8, 1=64x1, 2=1x64
    uint32_t plane_mask;                                    // does nothing on NV3
    uint32_t chroma_key;                                    // A1R10G10B10, alpha = bit 30
    uint32_t beta_factor;                                   // 30:23
    uint32_t d3d_config;                                    // CONTROL_OUT (0x400644): the D3D CONTROL0 of classes 0x17/0x18
    uint32_t d3d_alpha;                                     // ALPHACNTRL (0x4006C8): their CONTROL1 (alpha key and compare)
    uint32_t regs[0x2000 >> 2];                             // the registers below with no state of their own: their storage
    uint32_t dma_settings;                                  // CTX_SWITCH_B (0x400680): DMA object instance (15:0), M2MF input
    uint32_t ctx_switch_c;                                  // CTX_SWITCH_C (0x40068C): M2MF output DMA instance (15:0), bit 16
    uint8_t rop;                                            // Current GDI Ternary Render Operation
    // SURFACE STUFF - PGRAPH CAN OPERATE ON 4 SURFACES/BUFFERS AT A TIME
    uint32_t boffset[NV3_PGRAPH_MAX_BUFFERS];               // 22-bit linear VRAM offset for the start of a buffer.
    uint16_t bpitch[NV3_PGRAPH_MAX_BUFFERS];                // 12-bit linear VRAM offset for the pitch of a buffer
    uint32_t bpixel[NV3_PGRAPH_MAX_BUFFERS];                // Surface format per buffer (2:0, bit 2 = valid; BPIXEL nibbles)
    uint32_t notifier;
    bool notify_pending;                                    // Determines if a notification is pending.
    uint32_t notify_index;                                  // Which of the object's notifiers it writes (M2MF buffer notify = 1)
    bool fifo_access;                                       // Determines if PGRAPH can access PFIFO.
    nv3_pgraph_status_t status;                             // Current status of the 3D engine.
    uint32_t trapped_address;
    uint32_t trapped_data;
    uint32_t instance;                                      // no idea what this is but possibly an object context
    uint32_t trapped_instance;
    uint8_t cache[NV3_PGRAPH_CACHE_SIZE];                   // Internal vertex/texturea cache.

    /*  This area is used for holding universal representations of the U* registers, which are actually mapped into MMIO */
    struct nv3_object_class_001 beta_factor_class;
    struct nv3_object_class_002 rop_class;
    struct nv3_object_class_003 chroma_key_class;
    struct nv3_object_class_004 plane_mask_class;
    struct nv3_object_class_005 clipping_rectangle;
    struct nv3_object_class_006 pattern;
    struct nv3_object_class_007 rectangle;
    struct nv3_object_class_008 point;
    struct nv3_object_class_009 line;
    struct nv3_object_class_00A lin;
    struct nv3_object_class_00B triangle;
    struct nv3_object_class_00C win95_gdi_text;
    /* These are here so we can hold the current state of the image draw */
    uint32_t win95_gdi_text_bit_count;
    nv3_coord_16_t win95_gdi_text_current_position;     
    struct nv3_object_class_00D m2mf;
    struct nv3_object_class_00E scaled_image_from_memory;
    struct nv3_object_class_010 blit;
    struct nv3_object_class_011 image;
    nv3_coord_16_t image_current_position;               /* This is here so we can hold the current state of the image */
    uint32_t image_pixel_count;                          /* Pixels of the current image/bitmap/SIFC transfer consumed so far */
    struct nv3_object_class_012 bitmap;
    struct nv3_object_class_014 transfer2memory;
    struct nv3_object_class_015 stretched_image_from_cpu;
    struct nv3_object_class_017 d3d5_tri;
    struct nv3_object_class_018 point_zeta_buffer;
    struct nv3_object_class_01C image_in_memory;
} nv3_pgraph_t;

typedef struct nv3_ptimer_s
{
    uint32_t intr;          // PTIMER Interrupt status
    uint32_t intr_en;          // PTIMER Interrupt enable
    uint32_t clock_numerator;           // PTIMER (tick?) numerator
    uint32_t clock_denominator;         // PTIMER (tick?) denominator
    uint64_t time;                      // time
    uint32_t alarm;                     // The value of time when there should be an alarm
    uint64_t last_tsc;                  // guest time (tsc) TIME was last brought up to date at
    double   frac_ns;                   // the part of a nanosecond not yet added to TIME
} nv3_ptimer_t;

// Object name is just a uint32_t identifier it doesn't need a struct
// This is how the context is represented in ramin
// IN PGRAPH IT IS DIFFERENT! ONLY 5 BITS FOR THE CLASS ID! WHY?
typedef struct nv3_ramin_context_s
{
    union 
    {
        uint32_t context; 

        struct
        {
            uint16_t ramin_offset; 
            uint8_t class_id : 7;
            bool is_rendering : 1;
            uint8_t channel : 7;
            bool reserved : 1;
        };
    };
} nv3_ramin_context_t;

typedef enum nv3_ramin_ramro_reason_e
{
    nv3_runout_reason_illegal_access = 0,

    // PFIFO CACHE0/CACHE1 were turned off, so the graphics object could not be processed.
    nv3_runout_reason_no_cache_available = 1,

    // Ran out of CACHE0 & CACHE1 space.
    nv3_runout_reason_cache_ran_out = 2,

    nv3_runout_reason_free_count_overrun = 3,

    nv3_runout_reason_caught_lying = 4,

    // Access reserved by pagetable
    nv3_runout_reason_reserved_access = 5,

} nv3_ramin_ramro_reason;

// context for unused channels
typedef struct nv3_ramin_ramfc_s
{

} nv3_ramin_ramfc_t;

typedef struct nv3_pvideo_s
{
    uint32_t intr;          // Interrupt status
    uint32_t intr_en;          // Interrupt enable
    uint32_t fifo_threshold;            // FIFO threshold
    uint32_t fifo_burst_size;           // FIFO burst size
    uint32_t overlay_settings;          // Overlay settings
    uint32_t regs[0x40];                // 0x680200-0x6802FC as written (scaler, buffers, key, control)
    uint32_t shown;                     // buffer being scanned out (the last one taken)
} nv3_pvideo_t;

typedef struct nv3_pme_s                // Mediaport
{
    uint32_t intr;
    uint32_t intr_en;
} nv3_pme_t;

typedef struct nv3_s
{
    nv_base_t nvbase;   // Base Nvidia structure
    
    // Config
    /*
    // PCI bus speed - disabled is 33, enabled is 66Mhz. Irrelevant in AGP
    bool bus_speed;
    // Disabled     No BIOS
    // Enabled      BIOS
    bool bios;
    // RAM Type #1 - Disabled is 16Mbit (2MB) module size, enabled is 8Mbit (1MB) module ize
    bool ram_type;
    // NEC PC98 Mode
    bool nec_mode;
    // Disabled     64-bit
    // Enabled      128-bit
    bool bus_width;
    // Bus type: Disabled - PCI; Enabled - AGP
    bool bus_type;
    // Crystal type: Disabled - 13.5Mhz, enabled = 14.31818Mhz
    bool crystal;
    // TV Mode:  0 - SECAM, 1 - NTSC, 2 - PAL, 3 - none
    uint8_t tv_mode;
    // AGP 2X mode:  Disabled - AGP 1X, enabled - AGP 2X
    bool agp_is_2x; 
    bool unused;
    // Strap overwrite by Video BIOS enable
    bool overwrite;

    See defines in vid_nv3.h
    */
    uint32_t straps;

    // Subsystems
    nv3_pmc_t pmc;                  // Master Control
    nv3_pfb_t pfb;                  // Framebuffer/VRAM
    nv3_pbus_t pbus;                // Bus Control
    nv3_pfifo_t pfifo;              // FIFO for command submission

    nv3_pramdac_t pramdac;          // RAMDAC (CLUT etc)
    nv3_pgraph_t pgraph;            // 2D/3D Graphics
    nv3_ptimer_t ptimer;            // programmable interval timer
    // (ramro does not need a struct)
    nv3_ramin_ramfc_t ramfc;        // context for unused channels
    nv3_pvideo_t pvideo;            // Video overlay
    nv3_pme_t pme;                  // Mediaport - external MPEG decoder and video interface
    pc_timer_t dma_pusher_timer;    // the DMA pusher fetches asynchronously, a moment after it is started
    nv3_prim_state_t prim_saved[NV3_PGRAPH_CHANNELS]; // the other channels' primitive state (see nv3_prim_state_t)
    //more here

} nv3_t;

// device object
extern nv3_t* nv3;

//#ifdef ENABLE_NV_LOG
extern nv_register_t nv3_registers[];
//#endif

/*
    *FUNCTIONS* for the GPU core start here
    Functions for PGRAPH objects are in vid_nv3_classes.h
*/

// Device Core
void*       nv3_init(const device_t *info);
void        nv3_close(void* priv);
void        nv3_speed_changed(void *priv);
void        nv3_draw_cursor(svga_t* svga, int32_t drawline);
void        nv3_recalc_timings(svga_t* svga);
void        nv3_force_redraw(void* priv);

/* BAR0 GPU MMIO read */
void        nv3_update_mappings(void);                                          // Update memory mappings
uint8_t     nv3_mmio_read8(uint32_t addr, void* priv);                          // Read 8-bit MMIO
uint16_t    nv3_mmio_read16(uint32_t addr, void* priv);                         // Read 16-bit MMIO
uint32_t    nv3_mmio_read32(uint32_t addr, void* priv);                         // Read 32-bit MMIO
void        nv3_mmio_write8(uint32_t addr, uint8_t val, void* priv);            // Write 8-bit MMIO
void        nv3_mmio_write16(uint32_t addr, uint16_t val, void* priv);          // Write 16-bit MMIO
void        nv3_mmio_write32(uint32_t addr, uint32_t val, void* priv);          // Write 32-bit MMIO

/* BAR1 Dumb Framebuffer Read */
uint8_t     nv3_dfb_read8(uint32_t addr, void* priv);                           // Write 8-bit DFB
uint16_t    nv3_dfb_read16(uint32_t addr, void* priv);                          // Write 16-bit DFB
uint32_t    nv3_dfb_read32(uint32_t addr, void* priv);                          // Write 32-bit DFB
void        nv3_dfb_write8(uint32_t addr, uint8_t val, void* priv);             // Write 8-bit DFB
void        nv3_dfb_write16(uint32_t addr, uint16_t val, void* priv);           // Write 16-bit DFB
void        nv3_dfb_write32(uint32_t addr, uint32_t val, void* priv);           // Write 32-bit DFB

uint8_t     nv3_svga_read(uint16_t addr, void* priv);                           // Read SVGA compatibility register
void        nv3_svga_write(uint16_t addr, uint8_t val, void* priv);             // Write SVGA register
uint8_t     nv3_pci_read(int32_t func, int32_t addr, int32_t len, void* priv);             // Read PCI configuration register
void        nv3_pci_write(int32_t func, int32_t addr, int32_t len, uint8_t val, void* priv); // Write PCI configuration register

uint8_t     nv3_ramin_read8(uint32_t addr, void* priv);                         // Read 8-bit RAMIN
uint16_t    nv3_ramin_read16(uint32_t addr, void* priv);                        // Read 16-bit RAMIN
uint32_t    nv3_ramin_read32(uint32_t addr, void* priv);                        // Read 32-bit RAMIN
void        nv3_ramin_write8(uint32_t addr, uint8_t val, void* priv);           // Write 8-bit RAMIN
void        nv3_ramin_write16(uint32_t addr, uint16_t val, void* priv);         // Write 16-bit RAMIN
void        nv3_ramin_write32(uint32_t addr, uint32_t val, void* priv);         // Write 32-bit RAMIN
void        nv3_watch(uint32_t addr, uint32_t val, int size);                   // Debug: VRAM write watch
// RAMIN functions
uint32_t    nv3_ramht_hash(uint32_t name, uint32_t channel);
bool        nv3_ramin_find_object(uint32_t name, uint32_t cache_num, uint8_t channel_id, uint8_t subchannel_id);
#ifndef RELEASE_BUILD
void        nv3_debug_ramin_print_context_info(uint32_t name, nv3_ramin_context_t context);
#endif

//RAMHT, RAMRO and RAMFC just uses the RAMIN I/O functions

// MMIO Arbitration
// Determine where our reads or writes are going
uint32_t    nv3_mmio_arbitrate_read(uint32_t address);
void        nv3_mmio_arbitrate_write(uint32_t address, uint32_t value);

// Read and Write functions for GPU subsystems
// Remove the ones that aren't used here eventually, have all of htem for now
uint32_t    nv3_pmc_read(uint32_t address);
void        nv3_pmc_write(uint32_t address, uint32_t value);
uint32_t    nv3_pbus_read(uint32_t address);
void        nv3_pbus_write(uint32_t address, uint32_t value);
uint32_t    nv3_ptimer_read(uint32_t address);
void        nv3_ptimer_write(uint32_t address, uint32_t value);
uint32_t    nv3_pfb_read(uint32_t address);
void        nv3_pfb_write(uint32_t address, uint32_t value);
void        nv3_pfb_config0_write(uint32_t val);
uint32_t    nv3_pstraps_read(uint32_t address);
void        nv3_pstraps_write(uint32_t address, uint32_t value);

// Reads from vbios are 8bit
uint32_t    nv3_prom_read(uint32_t address);
void        nv3_prom_write(uint32_t address, uint32_t value);
uint32_t    nv3_pme_read(uint32_t address);
void        nv3_pme_write(uint32_t address, uint32_t value);

// TODO: PGRAPH class registers

uint32_t    nv3_pramdac_read(uint32_t address);
void        nv3_pramdac_write(uint32_t address, uint32_t value);
uint32_t    nv3_pramdac_get_pixel_clock_register(void);
uint32_t    nv3_pramdac_get_core_clock_register(void);
void        nv3_pramdac_set_pixel_clock_register(uint32_t value);
void        nv3_pramdac_set_core_clock_register(uint32_t value);

uint32_t    nv3_user_read(uint32_t address);
void        nv3_user_write(uint32_t address, uint32_t value);

// GPU subsystems

// NV3 PMC
void        nv3_pmc_init(void);
void        nv3_pmc_clear_interrupts(void);
uint32_t    nv3_pmc_handle_interrupts(bool send_now);

// NV3 PGRAPH
void        nv3_pgraph_init(void);
uint32_t    nv3_pgraph_read(uint32_t address);
void        nv3_pgraph_write(uint32_t address, uint32_t value);
void        nv3_pgraph_vblank_start(svga_t* svga);
void        nv3_pgraph_interrupt_context_switch(void);
void        nv3_pgraph_interrupt_valid(uint32_t num);
void        nv3_pgraph_interrupt_invalid(uint32_t num);
void        nv3_pgraph_submit(uint32_t param, uint16_t method, uint8_t channel, uint8_t subchannel, uint8_t class_id, nv3_ramin_context_t context);

// PGRAPH class methods
// this should be in "vid_nv3_classes.h", but before that can happen, some things need to be rejigged
void        nv3_generic_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj);
void        nv3_class_001_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj);
void        nv3_class_002_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj);
void        nv3_class_003_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj);
void        nv3_class_004_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj);
void        nv3_class_005_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj);
void        nv3_class_006_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj);
void        nv3_class_007_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj);
void        nv3_class_008_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj);
void        nv3_class_009_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj);
void        nv3_class_00a_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj);
void        nv3_class_00b_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj);
void        nv3_class_00c_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj);
void        nv3_class_00d_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj);
void        nv3_class_00e_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj);
void        nv3_class_010_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj);
void        nv3_class_011_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj);
void        nv3_class_012_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj);
void        nv3_class_014_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj);
void        nv3_class_015_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj);
void        nv3_class_017_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj);
void        nv3_class_018_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj);
void        nv3_class_01c_method(uint32_t param, uint32_t method_id, nv3_ramin_context_t context, nv3_grobj_t grobj);

// Notification Engine
void        nv3_notify_if_needed(uint32_t name, uint32_t method_id, nv3_ramin_context_t context,nv3_grobj_t grobj);
void        nv3_write_notifier(nv3_grobj_t grobj, uint32_t index, uint16_t status, uint32_t info32, uint16_t info16);
uint32_t    nv3_dma_read8(uint32_t inst, uint32_t offset);                              // a byte at offset in a DMA object
void        nv3_dma_write8(uint32_t inst, uint32_t offset, uint8_t val);

// NV3 PFIFO
uint32_t    nv3_pfifo_read(uint32_t address);
void        nv3_pfifo_write(uint32_t address, uint32_t value);
void        nv3_pfifo_interrupt(uint32_t id, bool fire_now);

// NV3 PFIFO - Caches
//cache0_push not a thing
void        nv3_pfifo_cache0_pull(void);
void        nv3_pfifo_cache1_push(uint32_t addr, uint32_t val);
void        nv3_pfifo_cache1_pull(void);
void        nv3_pfifo_cache1_drain(void);
void        nv3_pfifo_trigger_dma_if_required(void);
void        nv3_pfifo_dma_pusher_kick(void);
void        nv3_pfifo_dma_pusher_timer(void *priv);
uint32_t    nv3_pfifo_cache1_normal2gray(uint32_t val);
uint32_t    nv3_pfifo_cache1_gray2normal(uint32_t val);
uint32_t    nv3_pfifo_cache1_num_free_spaces(void);

// NV3 PFB
void        nv3_pfb_init(void);

// NV3 pstraps/PSTRAPS
void        nv3_pstraps_init(void);
double      nv3_pstraps_crystal_hz(void);

// NV3 PBUS/RMA
uint8_t     nv3_pbus_rma_read(uint16_t addr);
void        nv3_pbus_rma_write(uint16_t addr, uint8_t val);

// NV3 PRAMDAC (Final presentation)
void        nv3_pramdac_init(void);
void        nv3_pramdac_set_core_clock(void);
void        nv3_pramdac_set_pixel_clock(void);
void        nv3_pramdac_pixel_clock_poll(double real_time);
void        nv3_pramdac_core_clock_poll(double real_time);

// NV3 PTIMER
void        nv3_ptimer_tick(double real_time);
void        nv3_ptimer_update(void);       // bring TIME up to date with guest time

// NV3 PVIDEO
uint32_t    nv3_pvideo_read(uint32_t address);
void        nv3_pvideo_write(uint32_t address, uint32_t value);
void        nv3_pvideo_draw(svga_t *svga, int displine);
void        nv3_pvideo_vblank(void);
void        nv3_pvideo_init(void);
