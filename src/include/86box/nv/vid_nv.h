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
 *          Credit to:
 * 
 *          - fuel (PCBox developer)
 *          - Marcelina Kościelnicka (envytools)
 *          - nouveau developers
 *          - Utah GLX developers
 *          - XFree86 developers
 *          - xemu developers
 *
 * Authors: Connor Hyde / starfrost <mario64crashed@gmail.com>
 *
 *          Copyright 2024 Connor Hyde
 */
#ifdef EMU_DEVICE_H // what

//TODO: split this all into nv1, nv3, nv4...

#include <86box/timer.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>

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

#define CHIP_REVISION_NV1_A0    0x0000
#define CHIP_REVISION_NV1_B0    0x0010
#define CHIP_REVISION_NV1_C0    0x0020

#define CHIP_REVISION_NV3_A0    0x0000  // January 1997
#define CHIP_REVISION_NV3_B0    0x0010  // October 1997
#define CHIP_REVISION_NV3_C0    0x0020  // 1998

// Architecture IDs
#define NV_ARCHITECTURE_NV1     1
#define NV_ARCHITECTURE_NV2     2
#define NV_ARCHITECTURE_NV3     3


typedef enum nv_bus_generation_e
{
    // NV1
    // NV3
    nv_bus_pci = 0,

    // NV3
    nv_bus_agp_1x = 1,

    // NV3T
    // NV4
    nv_bus_agp_2x = 2,

} nv_bus_generation;

// NV Base
typedef struct nv_base_s
{
    rom_t vbios;                                // NVIDIA/OEm VBIOS
    // move to nv3_cio_t?
    svga_t svga;                                // SVGA core (separate to nv3)
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
} nv_base_t;

#define NV_REG_LIST_END                 0xD15EA5E

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
    int32_t     address;                    // MMIO Address
    char*       friendly_name;              // Friendly name
    // reg_ptr not needed as a parameter, because we implicitly know which register si being tiwddled
    uint32_t    (*on_read)();               // Optional on-read function
    void        (*on_write)(uint32_t value);// Optional on-write fucntion
} nv_register_t; 

nv_register_t* nv_get_register(uint32_t address, nv_register_t* register_list, uint32_t num_regs);

#define NV3_BOOT_REG_DEFAULT    0x00300111

// Master Control
typedef struct nv3_pmc_s
{
    /* 
    Holds chip manufacturing information at bootup.
    Current specification (may change later): pre-packed for convenience

    FIB Revision 1, Mask Revision B0, Implementation 1 [NV3], Architecture 3 [NV3], Manufacturer Nvidia, Foundry SGS (seems to have been the most common?)
    31:28=0000, 27:24=0000, 23:16=0003, 15:8=0001, 7:4=0001, 3:0=0001
    little endian 00000000 00000011 00000001 00010001   = 0x00300111
    */
    int32_t boot;   
    int32_t interrupt_status;          // Determines if interrupts are pending for specific subsystems.
    int32_t interrupt_enable;   // Determines if interrupts are actually enabled.
    int32_t enable;             // Determines which subsystems are enabled.

} nv3_pmc_t;

typedef struct nv3_pci_config_s
{
    uint8_t pci_regs[NV_PCI_NUM_CFG_REGS];  // The actual pci register values (not really used, just so they can be stored - they aren't very good for code readability)
    bool    vbios_enabled;                  // is the vbios enabled?
    uint8_t int_line;
} nv3_pci_config_t;

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
} nv3_pfb_t;

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
    uint32_t interrupt_status;          // Interrupt status
    uint32_t interrupt_enable;          // Interrupt enable
    nv3_pbus_rma_t rma;
} nv3_pbus_t;

// Command submission to PGRAPH
typedef struct nv_pfifo_s
{
    uint32_t interrupt_status;          // Interrupt status
    uint32_t interrupt_enable;          // Interrupt enable
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
} nv3_pramdac_t;

// Graphics Subsystem
typedef struct nv3_pgraph_s
{
    uint32_t interrupt_status_0;          // Interrupt status 0
    uint32_t interrupt_enable_0;          // Interrupt enable 0 
    uint32_t interrupt_status_1;          // Interrupt status 1
    uint32_t interrupt_enable_1;          // Interrupt enable 1
} nv3_pgraph_t;

// GPU Manufacturing Configuration (again)
// In the future this will be configurable
typedef struct nv3_pextdev_s
{
    /*
    // Disabled     33Mhz
    // Enabled      66Mhz
    bool bus_speed;
    
    // Disabled     No BIOS
    // Enabled      BIOS
    bool bios;

    // RAM Type #1
    // Disabled     16Mbit (2MB) module size
    // Enabled      8Mbit (1MB) module size
    bool ram_type_1;

    // NEC Mode
    bool nec_mode;

    // Disabled     64-bit
    // Enabled      128-bit
    bool bus_width;

    // Disabled     PCI
    // Enabled      AGP
    bool bus_type;

    // Disabled     13500
    // Enabled      14318180
    bool crystal;

    // TV Mode
    // 0 - SECAM, 1 - NTSC, 2 - PAL, 3 - none
    uint8_t tv_mode;

    // AGP 2X mode
    // Disabled     AGP 1X
    // Enabled      AGP 2X
    bool agp_is_2x; 
    
    bool unused;

    // Overwrite enable
    bool overwrite;

    See defines in vid_nv3.h
    */
    uint32_t straps;
    
    // more ram type stuff here but not used?
} nv3_pextdev_t;

typedef struct nv3_ptimer_s
{
    uint32_t interrupt_status;          // Interrupt status
    uint32_t interrupt_enable;          // Interrupt enable
} nv3_ptimer_t;

// Graphics object hashtable
typedef struct nv3_pramin_ramht_s
{

} nv3_pramin_ramht_t;

// Anti-fuckup device
typedef struct nv3_pramin_ramro_s
{

} nv3_pramin_ramro_t;

// context for unused channels
typedef struct nv3_pramin_ramfc_s
{

} nv3_pramin_ramfc_t;

// ????? ram auxillary
typedef struct nv_pramin_ramau_s
{

} nv3_pramin_ramau_t;

typedef struct nv3_pramin_s
{

} nv3_pramin_t;

typedef struct nv3_pvideo_s
{
    uint32_t interrupt_status;          // Interrupt status
    uint32_t interrupt_enable;          // Interrupt enable
} nv3_pvideo_t;

typedef struct nv3_pme_s                // Mediaport
{
    uint32_t interrupt_status;
    uint32_t interrupt_enable;
} nv3_pme_t;

typedef struct nv3_s
{
    nv_base_t nvbase;   // Base Nvidia structure
    
    // Config
    nv3_straps_t straps;
    nv3_pci_config_t pci_config;

    // Subsystems
    nv3_pmc_t pmc;              // Master Control
    nv3_pfb_t pfb;              // Framebuffer/VRAM
    nv3_pbus_t pbus;            // Bus Control
    nv3_pfifo_t pfifo;          // FIFO for command submisison

    nv3_pramdac_t pramdac;      // RAMDAC (CLUT etc)
    nv3_pgraph_t pgraph;        // 2D/3D Graphics
    nv3_pextdev_t pextdev;      // Chip configuration
    nv3_ptimer_t ptimer;        // programmable interval timer
    nv3_pramin_ramht_t ramht;   // hashtable for PGRAPH objects
    nv3_pramin_ramro_t ramro;   // anti-fuckup mechanism for idiots who fucked up the FIFO submission
    nv3_pramin_ramfc_t ramfc;   // context for unused channels
    nv3_pramin_ramau_t ramau;   // auxillary weirdnes
    nv3_pramin_t pramin;        // Ram for INput of DMA objects. Very important!
    nv3_pvideo_t pvideo;        // Video overlay
    nv3_pme_t pme;              // Mediaport - external MPEG decoder and video interface
    //more here

} nv3_t;

// device objects
extern nv3_t* nv3;

// Address of this returned by unimplemented registers to prevent a crash
extern uint32_t    unimplemented_dummy;

// NV3 stuff

// Device Core
void*       nv3_init(const device_t *info);
void        nv3_close(void* priv);
void        nv3_speed_changed(void *priv);
void        nv3_force_redraw(void* priv);

// Memory Mapping
void        nv3_update_mappings();
uint8_t     nv3_mmio_read8(uint32_t addr, void* priv);                          // Read 8-bit MMIO
uint16_t    nv3_mmio_read16(uint32_t addr, void* priv);                         // Read 16-bit MMIO
uint32_t    nv3_mmio_read32(uint32_t addr, void* priv);                         // Read 32-bit MMIO
void        nv3_mmio_write8(uint32_t addr, uint8_t val, void* priv);            // Write 8-bit MMIO
void        nv3_mmio_write16(uint32_t addr, uint16_t val, void* priv);          // Write 16-bit MMIO
void        nv3_mmio_write32(uint32_t addr, uint32_t val, void* priv);          // Write 32-bit MMIO

uint8_t     nv3_svga_in(uint16_t addr, void* priv);                             // Read SVGA compatibility registers
void        nv3_svga_out(uint16_t addr, uint8_t val, void* priv);               // Write SVGA registers
uint8_t     nv3_pci_read(int32_t func, int32_t addr, void* priv);               // Read PCI configuration registers
void        nv3_pci_write(int32_t func, int32_t addr, uint8_t val, void* priv); // Write PCI configuration registers

uint8_t     nv3_ramin_read8(uint32_t addr, void* priv);                          // Read 8-bit RAMIN
uint16_t    nv3_ramin_read16(uint32_t addr, void* priv);                         // Read 16-bit RAMIN
uint32_t    nv3_ramin_read32(uint32_t addr, void* priv);                         // Read 32-bit RAMIN
void        nv3_ramin_write8(uint32_t addr, uint8_t val, void* priv);            // Write 8-bit RAMIN
void        nv3_ramin_write16(uint32_t addr, uint16_t val, void* priv);          // Write 16-bit RAMIN
void        nv3_ramin_write32(uint32_t addr, uint32_t val, void* priv);          // Write 32-bit RAMIN

// MMIO Arbitration
// Determine where the hell in this mess our reads or writes are going
uint32_t    nv3_mmio_arbitrate_read(uint32_t address);
void        nv3_mmio_arbitrate_write(uint32_t address, uint32_t value);

// Read and Write functions for GPU subsystems
// Remove the ones that aren't used here eventually, have all of htem for now
uint32_t    nv3_pmc_read(uint32_t address);
void        nv3_pmc_write(uint32_t address, uint32_t value);
uint32_t    nv3_cio_read(uint32_t address);
void        nv3_cio_write(uint32_t address, uint32_t value);
uint32_t    nv3_pbus_read(uint32_t address);
void        nv3_pbus_write(uint32_t address, uint32_t value);
uint32_t    nv3_pfifo_read(uint32_t address);
void        nv3_pfifo_write(uint32_t address, uint32_t value);
uint32_t    nv3_prm_read(uint32_t address);
void        nv3_prm_write(uint32_t address, uint32_t value);
uint32_t    nv3_prmio_read(uint32_t address);
void        nv3_prmio_write(uint32_t address, uint32_t value);
uint32_t    nv3_ptimer_read(uint32_t address);
void        nv3_ptimer_write(uint32_t address, uint32_t value);
uint32_t    nv3_pfb_read(uint32_t address);
void        nv3_pfb_write(uint32_t address, uint32_t value);
uint32_t    nv3_pextdev_read(uint32_t address);
void        nv3_pextdev_write(uint32_t address, uint32_t value);

// Special consideration for straps
#define nv3_pstraps_read nv3_pextdev_read(NV3_PSTRAPS)
#define nv3_pstraps_write(x) nv3_pextdev_write(NV3_PSTRAPS, x)

uint32_t    nv3_prom_read(uint32_t address);
void        nv3_prom_write(uint32_t address, uint32_t value);
uint32_t    nv3_palt_read(uint32_t address);
void        nv3_palt_write(uint32_t address, uint32_t value);
uint32_t    nv3_pme_read(uint32_t address);
void        nv3_pme_write(uint32_t address, uint32_t value);
uint32_t    nv3_pgraph_read(uint32_t address);
void        nv3_pgraph_write(uint32_t address, uint32_t value);

// TODO: PGRAPH class registers

uint32_t    nv3_prmcio_read(uint32_t address);
void        nv3_prmcio_write(uint32_t address, uint32_t value);
uint32_t    nv3_pvideo_read(uint32_t address);
void        nv3_pvideo_write(uint32_t address, uint32_t value);
uint32_t    nv3_pramdac_read(uint32_t address);
void        nv3_pramdac_write(uint32_t address, uint32_t value);
uint32_t    nv3_vram_read(uint32_t address);
void        nv3_vram_write(uint32_t address, uint32_t value);
#define nv3_nvm_read nv3_vram_read
#define nv3_nvm_write nv3_vram_write
uint32_t    nv3_user_read(uint32_t address);
void        nv3_user_write(uint32_t address, uint32_t value);
#define nv3_object_submit_start nv3_user_read
#define nv3_object_submit_end nv3_user_write
uint32_t    nv3_pramin_read(uint32_t address);
void        nv3_pramin_write(uint32_t address, uint32_t value);
// TODO: RAMHT, RAMFC...or maybe handle it inside of nv3_pramin_*

// GPU subsystems

// NV3 PMC
void        nv3_pmc_init();
uint32_t    nv3_pmc_clear_interrupts();
uint32_t    nv3_pmc_handle_interrupts(bool send_now);

// NV3 PGRAPH
void        nv3_pgraph_init();

// NV3 PFIFO
void        nv3_pfifo_init();


// NV3 PFB
void        nv3_pfb_init();

// NV3 PEXTDEV/PSTRAPS
void        nv3_pextdev_init();

// NV3 PBUS
void        nv3_pbus_init();

// NV3 PBUS RMA - Real Mode Access for VBIOS
uint8_t     nv3_pbus_rma_read(uint16_t addr);
void        nv3_pbus_rma_write(uint16_t addr, uint8_t val);

// NV3 PRAMDAC
void        nv3_pramdac_init();
void        nv3_pramdac_set_vram_clock();
void        nv3_pramdac_set_pixel_clock();

// NV3 PTIMER
void        nv3_ptimer_init();

// NV3 PVIDEO
void        nv3_pvideo_init();

// NV3 PMEDIA
void        nv3_pmedia_init();
#endif