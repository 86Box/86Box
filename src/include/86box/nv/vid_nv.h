/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Shared implementation file for all NVIDIA GPUs (hopefully to be) emulated by 86box.
 * 
 *          Credit to:
 * 
 *          - Marcelina Kościelnicka (envytools)                            https://envytools.readthedocs.io/en/latest/ 
 *          - fuel (PCBox developer)                                        https://github.com/PCBox/PCBox
 *          - nouveau developers                                            https://nouveau.freedesktop.org/
 *          - Utah GLX developers                                           https://utah-glx.sourceforge.net/
 *          - XFree86 developers                                            https://www.xfree86.org/
 *          - xemu developers                                               https://github.com/xemu-project/xemu
 *          - RivaTV developers                                             https://rivatv.sourceforge.net (esp. https://rivatv.sourceforge.net/stuff/riva128.txt) 
 *          - Nvidia for leaking their driver symbols numerous times ;^)    https://nvidia.com 
 *          - People who prevented me from giving up (various)              
 * 
 * Authors: Connor Hyde / starfrost <mario64crashed@gmail.com>
 *      
 *          Copyright 2024-2025 Connor Hyde
 */
#ifdef EMU_DEVICE_H // what

//TODO: split this all into nv1, nv3, nv4...
#include <86box/log.h>
#include <86box/i2c.h>
#include <86box/vid_ddc.h>
#include <86box/timer.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>
#include <86box/nv/vid_nv_rivatimer.h>

void nv_log_set_device(void* device);
void nv_log(const char *fmt, ...);

// Defines common to all NV chip architectural generations

// PCI IDs
#define PCI_VENDOR_NV           0x10DE  // NVidia PCI ID
#define PCI_VENDOR_SGS          0x104A  // SGS-Thompson
#define PCI_VENDOR_SGS_NV       0x12D2  // SGS-Thompson/NVidia joint venture

#define NV_PCI_NUM_CFG_REGS     256     // number of pci config registers

// 0x0000 was probably the NV0 'Nvidia Hardware Simulator'
#define PCI_DEVICE_NV1          0x0008  // Nvidia NV1
#define PCI_DEVICE_NV1_VGA      0x0009  // Nvidia NV1 VGA core
#define PCI_DEVICE_NV2          0x0010  // Nvidia NV2 / Mutara V08 (cancelled)
#define PCI_DEVICE_NV3          0x0018  // Nvidia NV3 (Riva 128)
#define PCI_DEVICE_NV3T         0x0019  // Nvidia NV3T (Riva 128 ZX)
#define PCI_DEVICE_NV4          0x0020  // Nvidia NV4 (RIVA TNT)

#define CHIP_REVISION_NV1_A0    0x0000  // 1994
#define CHIP_REVISION_NV1_B0    0x0010  // 1995
#define CHIP_REVISION_NV1_C0    0x0020  // 

#define CHIP_REVISION_NV3_A0    0x0000  // January 1997
#define CHIP_REVISION_NV3_B0    0x0010  // October 1997
#define CHIP_REVISION_NV3_C0    0x0020  // 1998

// Architecture IDs
#define NV_ARCHITECTURE_NV1     1       // NV1/STG2000
#define NV_ARCHITECTURE_NV2     2       // Nvidia 'Mutara V08' 
#define NV_ARCHITECTURE_NV3     3       // Riva 128
#define NV_ARCHITECTURE_NV4     4       // Riva TNT and later

typedef enum nv_bus_generation_e
{
    // NV1 - Prototype version
    nv_bus_vlb = 0,

    // NV1
    // NV3
    nv_bus_pci = 1,

    // NV3
    nv_bus_agp_1x = 2,

    // NV3T
    // NV4
    nv_bus_agp_2x = 3,

} nv_bus_generation;

// NV Base
typedef struct nv_base_s
{
    rom_t vbios;                                // NVIDIA/OEm VBIOS
    // move to nv3_cio_t?
    svga_t svga;                                // SVGA core (separate to nv3) - Weitek licensed
    void* log;                                  // new logging engine
    // stuff that doesn't fit in the svga structure
    uint32_t cio_read_bank;                     // SVGA read bank
    uint32_t cio_write_bank;                    // SVGA write bank

    mem_mapping_t framebuffer_mapping;          // Linear Framebuffer / NV_USER memory mapping
    mem_mapping_t mmio_mapping;                 // mmio mapping (32MB unified MMIO) 
    mem_mapping_t framebuffer_mapping_mirror;   // Mirror of LFB mapping
    mem_mapping_t ramin_mapping;                // RAM INput area mapping
    mem_mapping_t ramin_mapping_mirror;         // RAM INput area mapping (mirrored)
    uint8_t pci_slot;                           // pci slot number
    uint8_t pci_irq_state;                      // current PCI irq state
    uint32_t bar0_mmio_base;                    // PCI Base Address Register 0 - MMIO Base
    uint32_t bar1_lfb_base;                     // PCI Base Address Register 1 - Linear Framebuffer (NV_BASE)
    nv_bus_generation bus_generation;           // current bus (see nv_bus_generation documentation)
    uint32_t gpu_revision;                      // GPU Stepping
    double pixel_clock_frequency;               // Frequency used for pixel clock
    rivatimer_t* pixel_clock_timer;             // Timer for measuring pixel clock
    bool pixel_clock_enabled;                   // Pixel Clock Enabled - stupid crap used to prevent us enabling the timer multiple times
    double memory_clock_frequency;              // Source Frequency for PTIMER
    rivatimer_t* memory_clock_timer;            // Timer for measuring memory/gpu clock
    bool memory_clock_enabled;                  // Memory Clock Enabled - stupid crap used to prevent us eanbling the timer multiple times
    void* i2c;                                  // I2C for monitor EDID
    void* ddc;                                  // Display Data Channel for EDID
} nv_base_t;

#define NV_REG_LIST_END                         0xD15EA5E

// The NV architectures are very complex.
// There are hundreds of registers at minimum, and implementing these in a standard way would lead to 
// unbelievably large switch statements and horrifically unreadable code.
// So this is used to abstract it and allow for more readable code.
// This is mostly just used for logging and stuff.
// Optionally, you can provide a function that is run when you read to and write from the register. 
// You can also implement this functionality in a traditional way such as a switch statement, for simpler registers. To do this, simply set both read and write functions to NULL.
// Typically, unless they are for a special purpose (and handled specially) e.g. vga all register reads and writes are also 32-bit aligned
typedef struct nv_register_s
{
    int32_t     address;                        // MMIO Address
    char*       friendly_name;                  // Friendly name
    // reg_ptr not needed as a parameter, because we implicitly know which register si being tiwddled
    uint32_t    (*on_read)();                   // Optional on-read function
    void        (*on_write)(uint32_t value);    // Optional on-write fucntion
} nv_register_t; 

nv_register_t* nv_get_register(uint32_t address, nv_register_t* register_list, uint32_t num_regs);


#endif