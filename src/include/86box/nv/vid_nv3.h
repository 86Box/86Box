/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          JENSEN HUANG APPROVED !!!!
 *
 *
 *
 * Authors: Connor Hyde <mario64crashed@gmail.com>
 *
 *          Copyright 2024 Connor Hyde
 */

// vid_nv3.h: NV3 Architecture Hardware Reference (open-source)
// Last updated     2 December 2024

// The GPU base structure
extern nv3_t* nv3;
extern const device_config_t nv3_config[];

#define NV3_MMIO_SIZE                                   0x1000000       // Max MMIO size

// various vbioses for testing
// Coming soon: MIROmagic Premium BIOS (when I get mine dumped)
//todo: move to hash system

// Oldest one of these - September 6, 1997
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

// The default VBIOS to use
#define NV3_VBIOS_DEFAULT                               NV3_VBIOS_ERAZOR_V15403

// Temporary, will be loaded from settings
#define VRAM_SIZE_2MB                                   0x200000 // 2MB
#define VRAM_SIZE_4MB                                   0x400000 // 4MB
#define VRAM_SIZE_8MB                                   0x800000 // NV3T only


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

#define NV3_PCI_CFG_INT_LINE                            0x3C
#define NV3_PCI_CFG_INT_PIN                             0x3D

#define NV3_PCI_CFG_SUBSYSTEM_ID_MIRROR_START           0x40
#define NV3_PCI_CFG_SUBSYSTEM_ID_MIRROR_END             0x43

#define NV3_PCI_CFG_MIN_GRANT                           0x3E
#define NV3_PCI_CFG_MIN_GRANT_DEFAULT                   0x03
#define NV3_PCI_CFG_MAX_LATENCY                         0x3F
#define NV3_PCI_CFG_MAX_LATENCY_DEFAULT                 0x01

// GPU Subsystems
// These most likely correspond to functional blocks in the original design

#define NV3_PMC_START                                   0x0         // Chip Master Control Subsystem

#define NV3_PMC_BOOT                                    0x0         // Boot Configuration

#define NV3_PMC_INTERRUPT_STATUS                        0x100       // Interrupt Control
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
#define NV3_PMC_INTERRUPT_ENABLE                        0x140       // Controls global interrupt enable state
#define NV3_PMC_INTERRUPT_ENABLE_HARDWARE               0x1         // Determines if hardware interrupts are enabled
#define NV3_PMC_INTERRUPT_ENABLE_SOFTWARE               0x2         // Determinse if software interrupts were enabled
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
#define NV3_PBUS_INTR                                   0x1100      // Bus Control - Interrupt Status
#define NV3_PBUS_INTR_EN                                0x1140      // Bus Control - Interrupt Enable
#define NV3_PBUS_PCI_START                              0x1800      // PCI mirror start
#define NV3_PBUS_PCI_END                                0x18FF      // PCI mirror end
#define NV3_PBUS_END                                    0x1FFF
#define NV3_PFIFO_START                                 0x2000      // FIFO for DMA Object Submission (uses hashtable to store the objects)
#define NV3_PFIFO_INTR                                  0x2100      // FIFO - Interrupt Status
#define NV3_PFIFO_INTR_EN                               0x2140      // FIFO - Interrupt Enable
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
#define NV3_PTIMER_INTR_EN                              0x9140
#define NV3_PTIMER_END                                  0x9FFF
#define NV3_VGA_VRAM_START                              0xA0000     // VGA Emulation VRAM
#define NV3_VGA_VRAM_END                                0xBFFFF
#define NV3_VGA_START                                   0xC0000     // VGA Emulation Registers
#define NV3_VGA_END                                     0xC7FFF
#define NV3_PRMVIO_START                                NV3_VGA_START
#define NV3_PRMVIO_END                                  NV3_VGA_END
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
#define NV3_PFB_CONFIG_0                                0x100200    // Framebuffer interface config register 0
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
#define NV3_PEXTDEV_START                               0x101000    // External Devices
#define NV3_PSTRAPS                                     0x101000    // Straps Bits
#define NV3_PSTRAPS_BUS_SPEED                           0           // Configured bus speed
#define NV3_PSTRAPS_BUS_SPEED_33MHZ                     0x0
#define NV3_PSTRAPS_BUS_SPEED_66MHZ                     0x1
#define NV3_PSTRAPS_BIOS                                1           // Is a VBIOS present?
#define NV3_PSTRAPS_BIOS_NOT_PRESENT                    0x0
#define NV3_PSTRAPS_BIOS_PRESENT                        1
#define NV3_PSTRAPS_RAM_TYPE                            2           // Type of RAM module
#define NV3_PSTRAPS_RAM_TYPE_16MBIT                     0x0
#define NV3_PSTRAPS_RAM_TYPE_8MBIT                      0x1
#define NV3_PSTRAPS_NEC_MODE                            3           // PC98?
#define NV3_PSTRAPS_NEC_MODE_DISABLED                   0x0
#define NV3_PSTRAPS_NEC_MODE_ENABLED                    0x1
#define NV3_PSTRAPS_BUS_WIDTH                           4           // Bus width
#define NV3_PSTRAPS_BUS_WIDTH_64BIT                     0x0
#define NV3_PSTRAPS_BUS_WIDTH_128BIT                    0x0
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
#define NV3_PSTRAPS_AGP2X                               9
#define NV3_PSTRAPS_AGP2X_ENABLED                       0x0
#define NV3_PSTRAPS_AGP2X_DISABLED                      0x1
#define NV3_PSTRAPS_UNUSED                              10
#define NV3_PSTRAPS_OVERWRITE                           11
#define NV3_PSTRAPS_OVERWRITE_DISABLED                  0x0
#define NV3_PSTRAPS_OVERWRITE_ENABLED                   0x1
#define NV3_PEXTDEV_END                                 0x101FFF
#define NV3_PROM_START                                  0x110000    // VBIOS?
#define NV3_PROM_END                                    0x110FFF
#define NV3_PALT_START                                  0x120000    // ??? but it exists
#define NV3_PALT_END                                    0x120FFF
#define NV3_PME_START                                   0x200000    // Mediaport 
#define NV3_PME_INTR                                    0x200100    // Mediaport: Interrupt Pending?
#define NV3_PME_INTR_EN                                 0x200140    // Mediaport: Interrupt Enable
#define NV3_PME_END                                     0x200FFF
#define NV3_PGRAPH_START                                0x400000    // Scene graph for 2d/3d rendering...the most important part
#define NV3_PGRAPH_INTR_0                               0x400100
#define NV3_PGRAPH_INTR_1                               0x400104
#define NV3_PGRAPH_INTR_EN_0                            0x400140    // Interrupt Control for PGRAPH #1
#define NV3_PGRAPH_INTR_EN_0_VBLANK                     8           // Fired every frame
//todo: add what this does
#define NV3_PGRAPH_INTR_EN_1                            0x400180    // Interrupt Control for PGRAPH #2 (it can receive two at onc)

// not sure about the class ids
// these are NOT what each class is, just uSed to manipulate it (there isn't a one to one class->reg mapping anyway)
#define NV3_PGRAPH_CLASS18_BETA_START                   0x410000    // Beta blending factor
#define NV3_PGRAPH_CLASS18_BETA_END                     0x411FFF  
#define NV3_PGRAPH_CLASS20_ROP_START                    0x420000    // Blending render operation used at final pixel/fragment generation stage
#define NV3_PGRAPH_CLASS20_ROP_END                      0x421FFF
#define NV3_PGRAPH_CLASS21_COLORKEY_START               0x430000    // Color key for image
#define NV3_PGRAPH_CLASS21_COLORKEY_END                 0x431FFF      
#define NV3_PGRAPH_CLASS22_PLANEMASK_START              0x440000    // Plane mask (for clipping?)
#define NV3_PGRAPH_CLASS22_PLANEMASK_END                0x441FFF
#define NV3_PGRAPH_CLASSXX_CLIP_START                   0x450000    // clipping, probably class 23
#define NV3_PGRAPH_CLASSXX_CLIP_END                     0x451FFF
#define NV3_PGRAPH_CLASS24_PATTERN_START                0x460000    // presumably a blend pattern 
#define NV3_PGRAPH_CLASS24_PATTERN_END                  0x461FFF
#define NV3_PGRAPH_CLASS30_RECTANGLE_START              0x470000    // also class 25 - that's black [NV1]
#define NV3_PGRAPH_CLASS30_RECTANGLE_END                0x471FFF    // also class 25 - that's black [NV1]
#define NV3_PGRAPH_CLASS26_POINT_START                  0x480000    // A single point
#define NV3_PGRAPH_CLASS26_POINT_END                    0x481FFF
#define NV3_PGRAPH_CLASS27_LINE_START                   0x490000    // A line
#define NV3_PGRAPH_CLASS27_LINE_END                     0x491FFF       
#define NV3_PGRAPH_CLASS28_LIN_START                    0x4A0000    // A lin - a line without its starting or ending pixels
#define NV3_PGRAPH_CLASS28_LIN_END                      0x4A1FFF
#define NV3_PGRAPH_CLASS29_TRIANGLE_START               0x4B0000    // A triangle [NV1 variant] - in NV1 this was converted to a quad patch
#define NV3_PGRAPH_CLASS29_TRIANGLE_END                 0x4B1FFF   
#define NV3_PGRAPH_CLASS75_GDITEXT_START                0x4C0000    // Windows 95/NT GDI text acceleration
#define NV3_PGRAPH_CLASS75_GDITEXT_END                  0x4C1FFF    

#define NV3_PGRAPH_CLASS61_MEM2MEM_XFER_START           0x4D0000    // memory to memory transfer (not sure about which class this is)
#define NV3_PGRAPH_CLASS61_MEM2MEM_XFER_END             0x4D1FFF    
#define NV3_PGRAPH_CLASSXX_IMAGE2MEM_XFER_SCALED_START  0x4E0000    // class 55, 56
#define NV3_PGRAPH_CLASSXX_IMAGE2MEM_XFER_SCALED_END    0x4E1FFF

#define NV3_PGRAPH_CLASS31_BLIT_START                   0x500000    // Blit 2d image from memory
#define NV3_PGRAPH_CLASS31_BLIT_END                     0x501FFF      

#define NV3_PGRAPH_CLASSXX_CPU2MEM_IMAGE_START          0x510000    // Used for class 33, 34, 54
#define NV3_PGRAPH_CLASSXX_CPU2MEM_IMAGE_END            0x511FFF    
#define NV3_PGRAPH_CLASSXX_CPU2MEM_BITMAP_START         0x520000    // not sure, might depend on format
#define NV3_PGRAPH_CLASSXX_CPU2MEM_BITMAP_END           0x521FFF    

#define NV3_PGRAPH_CLASSXX_IMAGE2MEM_XFER_START         0x540000    // send image to vram, not sure what class 
#define NV3_PGRAPH_CLASSXX_IMAGE2MEM_XFER_END           0x541FFF    
#define NV3_PGRAPH_CLASS54_CPU2MEM_STRETCHED_START      0x550000    // stretched cpu->vram transfer, 54
#define NV3_PGRAPH_CLASS54_CPU2MEM_STRETCHED_END        0x551FFF  

#define NV3_PGRAPH_CLASS72_D3D5TRI_ZETA_START           0x570000    // [NV3] Copy a direct3d 5.0 accelerated triangle to the zeta buffer
#define NV3_PGRAPH_CLASS72_D3D5TRI_ZETA_END             0x571FFF    
#define NV3_PGRAPH_CLASSXX_POINTZETA_START              0x580000    // possibly class 69
#define NV3_PGRAPH_CLASSXX_POINTZETA_END                0x581FFF

#define NV3_PGRAPH_CLASS62_MEM2IMAGE_START              0x5C0000    // class 55, 56, 62, 63?
#define NV3_PGRAPH_CLASS62_MEM2IMAGE_END                0x5C1FFF    

#define NV3_PGRAPH_REGISTER_END                         0x401FFF    // end of pgraph registers
#define NV3_PGRAPH_REAL_END                             0x5C1FFF

#define NV3_PRMCIO_START                                0x601000
#define NV3_PRMCIO_END                                  0x601FFF

#define NV3_PDAC_START                                  0x680000    // OPTIONAL external DAC
#define NV3_PVIDEO_START                                0x680000    // Video Generation / overlay configuration
#define NV3_PVIDEO_INTR                                 0x680100
#define NV3_PVIDEO_INTR_EN                              0x680140
#define NV3_PVIDEO_END                                  0x6802FF
#define NV3_PRAMDAC_START                               0x680300

#define NV3_PRAMDAC_CLOCK_MEMORY                        0x680504
#define NV3_PRAMDAC_CLOCK_MEMORY_VDIV                   7:0
#define NV3_PRAMDAC_CLOCK_MEMORY_NDIV                   15:8
#define NV3_PRAMDAC_CLOCK_MEMORY_PDIV                   18:16
#define NV3_PRAMDAC_CLOCK_PIXEL                         0x680508
#define NV3_PRAMDAC_COEFF_SELECT                        0x68050C

#define NV3_PRAMDAC_GENERAL_CONTROL                     0x680600

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


#define NV3_USER_START                                  0x800000    // Mapping for the area where objects are submitted into the FIFO
#define NV3_USER_END                                    0xFFFFFF

// easier name
#define NV3_OBJECT_SUBMIT_START                         NV3_USER_START
#define NV3_OBJECT_SUBMIT_END                           NV3_USER_END

// also PDFB (Debug Framebuffer?)
#define NV3_PNVM_START                                  0x1000000   // VRAM access (max 8MB)
#define NV3_PNVM_END                                    0x17FFFFF   

// to be less confusing - NVM = "NV Memory"
#define NV3_VRAM_START                                  NV3_PNVM_START
#define NV3_VRAM_END                                    NV3_PNVM_END

// control structures for dma'd in graphics objects from pfifo
// these all have configurable sizes, define them here
#define NV3_PRAMIN_START                                0x1C00000

#define NV3_PRAMIN_RAMHT_START                          0x1C00000   // Hashtable for storing submitted objects
#define NV3_PRAMIN_RAMHT_END                            0x1C00FFF
#define NV3_PRAMIN_RAMHT_SIZE_0                         0xFFF
#define NV3_PRAMIN_RAMHT_SIZE_1                         0x1FFF
#define NV3_PRAMIN_RAMHT_SIZE_2                         0x3FFF
#define NV3_PRAMIN_RAMHT_SIZE_3                         0x7FFF

#define NV3_PRAMIN_RAMAU_START                          0x1C01000   // Auxillary area
#define NV3_PRAMIN_RAMAU_END                            0x1C01BFF
#define NV3_PRAMIN_RAMFC_START                          0x1C01C00   // context for unused PFIFO DMA channels
#define NV3_PRAMIN_RAMFC_END                            0x1C01DFF
#define NV3_PRAMIN_RAMFC_SIZE_0                         0x1FF
#define NV3_PRAMIN_RAMFC_SIZE_1                         0xFFF
#define NV3_PRAMIN_RAMRO_START                          0x1C01E00   // Runout area for invalid submissions
#define NV3_PRAMIN_RAMRO_SIZE_0                         0x1FF
#define NV3_PRAMIN_RAMRO_SIZE_1                         0x1FFF
#define NV3_PRAMIN_RAMRO_END                            0x1C01FFF
#define NV3_PRAMIN_RAMRM_START                          0x1C02000
#define NV3_PRAMIN_RAMRM_END                            0x1C02FFF

#define NV3_PRAMIN_END                                  0x1FFFFFF

// not done

// Master Control


// CRTC/CIO (0x3b0-0x3df)

#define NV3_CRTC_DATA_OUT                               0x3C0
#define NV3_CRTC_MISCOUT                                0x3C2

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


// These are nvidia (25-63)
#define NV3_CRTC_REGISTER_RPC0                          0x19        // What does this mean?
#define NV3_CRTC_REGISTER_RPC1                          0x1A        // What does this mean?
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

#define NV3_CRTC_REGISTER_PIXELMODE_VGA                 0x00        // vga textmode
#define NV3_CRTC_REGISTER_PIXELMODE_8BPP                0x01
#define NV3_CRTC_REGISTER_PIXELMODE_16BPP               0x02
#define NV3_CRTC_REGISTER_PIXELMODE_32BPP               0x03 

#define NV3_CRTC_REGISTER_RMA                           0x38 // REAL MODE ACCESS!

// where the fuck is GDC?
#define NV3_CRTC_BANKED_128K_A0000                      0x00
#define NV3_CRTC_BANKED_64K_A0000                       0x04
#define NV3_CRTC_BANKED_32K_B0000                       0x08
#define NV3_CRTC_BANKED_32K_B8000                       0x0C


#define NV3_RMA_REGISTER_START                          0x3D0
#define NV3_RMA_REGISTER_END                            0x3D3

#define NV3_CRTC_REGISTER_NVIDIA_END                    0x3F
// for 86box 8bit addressing
// get rid of this asap, replace with 32->8 macros
#define NV3_RMA_SIGNATURE_MSB                           0x65
#define NV3_RMA_SIGNATURE_BYTE2                         0xD0
#define NV3_RMA_SIGNATURE_BYTE1                         0x16
#define NV3_RMA_SIGNATURE_LSB                           0x2B

#define NV3_CRTC_REGISTER_RMA_MODE_MAX                  0x0F


//todo: pixel format

