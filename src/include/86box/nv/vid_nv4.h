/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NVIDIA RIVA TNT (NV4) and RIVA TNT2 (NV5) -- shared state.
 *
 *          One nv4_t per chip; every block takes the chip it acts on, so
 *          two boards can coexist. The blocks live in src/video/nv/nv4/:
 *          the device shell (nv4_core.c), the legacy VGA path (nv4_vga.c)
 *          and one file per chip unit under subsystems/.
 *
 * Authors: Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2026 Marcos Alves
 */
#ifndef VIDEO_NV4_H
#define VIDEO_NV4_H

#include <stdatomic.h>

/* ------------------------------------------------------------------ */
/* Chips and boards.                                                   */
/* ------------------------------------------------------------------ */
#define NV4_PCI_VENDOR           0x10de
#define NV4_PCI_DEVICE_TNT       0x0020 /* RIVA TNT (NV4) */
#define NV4_PCI_DEVICE_TNT2      0x0028 /* RIVA TNT2 / TNT2 Pro (NV5) */
#define NV4_PCI_DEVICE_TNT2_U    0x0029 /* RIVA TNT2 Ultra (NV5) */
#define NV4_PCI_DEVICE_VANTA     0x002c /* Vanta (NV5, 64-bit memory) */
#define NV4_PCI_DEVICE_TNT2_M64  0x002d /* RIVA TNT2 M64 (NV5, 64-bit memory) */

typedef enum nv4_chip_e {
    NV4_CHIP_TNT  = 4, /* NV4 */
    NV4_CHIP_TNT2 = 5  /* NV5 and its 64-bit variants */
} nv4_chip_t;

/* ------------------------------------------------------------------ */
/* BAR0: the 16 MB register space.                                     */
/* ------------------------------------------------------------------ */
#define NV4_MMIO_SIZE            0x01000000

#define NV4_PMC_START            0x000000
#define NV4_PBUS_START           0x001000
#define NV4_PFIFO_START          0x002000
#define NV4_PFIFO_END            0x003fff
#define NV4_PRM_START            0x004000 /* real-mode trace */
#define NV4_PRM_END              0x005fff
#define NV4_PRMIO_START          0x007000 /* real-mode access registers */
#define NV4_PRMIO_END            0x007fff
#define NV4_PTIMER_START         0x009000
#define NV4_PTIMER_END           0x009fff
#define NV4_PRMVGA_START         0x0a0000 /* the VGA memory window */
#define NV4_PRMVGA_END           0x0bffff
#define NV4_PRMVIO_START         0x0c0000 /* VGA sequencer and graphics controller ports */
#define NV4_PRMVIO_END           0x0c7fff
#define NV4_PFB_START            0x100000
#define NV4_PFB_END              0x100fff
#define NV4_PEXTDEV_START        0x101000
#define NV4_PEXTDEV_END          0x101fff
#define NV4_PME_START            0x200000
#define NV4_PME_END              0x200fff
#define NV4_PROM_START           0x300000 /* the video BIOS, 64 KB */
#define NV4_PROM_END             0x30ffff
#define NV4_PGRAPH_START         0x400000
#define NV4_PGRAPH_END           0x401fff
#define NV4_PCRTC_START          0x600000
#define NV4_PCRTC_END            0x600fff
#define NV4_PRMCIO_START         0x601000 /* VGA CRTC and attribute ports */
#define NV4_PRMCIO_END           0x601fff
#define NV4_PVIDEO_START         0x680000
#define NV4_PVIDEO_END           0x6802ff
#define NV4_PRAMDAC_START        0x680300
#define NV4_PRAMDAC_END          0x680fff
#define NV4_PRMDIO_START         0x681000 /* VGA DAC ports */
#define NV4_PRMDIO_END           0x681fff
#define NV4_PRAMIN_START         0x700000 /* instance memory window, 1 MB */
#define NV4_PRAMIN_END           0x7fffff
#define NV4_USER_START           0x800000 /* FIFO channels */
#define NV4_USER_END             0xffffff

/* PMC */
#define NV4_PMC_BOOT_0           0x000000
#define NV4_PMC_INTR_0           0x000100
#define NV4_PMC_INTR_EN_0        0x000140
#define NV4_PMC_INTR_READ_0      0x000160
#define NV4_PMC_ENABLE           0x000200

#define NV4_PMC_INTR_PMEDIA      (1u << 4)
#define NV4_PMC_INTR_PFIFO       (1u << 8)
#define NV4_PMC_INTR_PGRAPH      (1u << 12)
#define NV4_PMC_INTR_PVIDEO      (1u << 16)
#define NV4_PMC_INTR_PTIMER      (1u << 20)
#define NV4_PMC_INTR_PCRTC       (1u << 24)
#define NV4_PMC_INTR_PBUS        (1u << 28)
#define NV4_PMC_INTR_SOFTWARE    (1u << 31)

/* PBUS */
#define NV4_PBUS_DEBUG_0         0x001080
#define NV4_PBUS_DEBUG_1         0x001084
#define NV4_PBUS_DEBUG_2         0x001088
#define NV4_PBUS_DEBUG_3         0x00108c
#define NV4_PBUS_INTR_0          0x001100
#define NV4_PBUS_INTR_EN_0       0x001140
#define NV4_PBUS_ROM_CONFIG      0x001200
#define NV4_PBUS_PCI_START       0x001800 /* PCI configuration space, mirrored */
#define NV4_PBUS_PCI_END         0x0018ff

/* PTIMER */
#define NV4_PTIMER_INTR_0        0x009100
#define NV4_PTIMER_INTR_EN_0     0x009140
#define NV4_PTIMER_NUMERATOR     0x009200
#define NV4_PTIMER_DENOMINATOR   0x009210
#define NV4_PTIMER_TIME_0        0x009400
#define NV4_PTIMER_TIME_1        0x009410
#define NV4_PTIMER_ALARM_0       0x009420

/* PFB */
#define NV4_PFB_BOOT_0           0x100000
#define NV4_PFB_DEBUG_0          0x100080
#define NV4_PFB_GREEN_0          0x1000c0
#define NV4_PFB_CONFIG_0         0x100200
#define NV4_PFB_CONFIG_1         0x100204
#define NV4_PFB_RTL              0x100300

/* PFB_BOOT_0 RAM_AMOUNT (bits 1:0) */
#define NV4_PFB_RAM_32MB         0 /* NV5; the NV4 resman calls this code 2MB */
#define NV4_PFB_RAM_4MB          1
#define NV4_PFB_RAM_8MB          2
#define NV4_PFB_RAM_16MB         3

/* PEXTDEV */
#define NV4_PEXTDEV_BOOT_0       0x101000

/* PCRTC */
#define NV4_PCRTC_INTR_0         0x600100
#define NV4_PCRTC_INTR_EN_0      0x600140
#define NV4_PCRTC_START_ADDR     0x600800
#define NV4_PCRTC_CONFIG         0x600804
#define NV4_PCRTC_RASTER         0x600808

/* PRAMDAC */
#define NV4_PRAMDAC_CU_START_POS     0x680300
#define NV4_PRAMDAC_NVPLL_COEFF      0x680500
#define NV4_PRAMDAC_MPLL_COEFF       0x680504
#define NV4_PRAMDAC_VPLL_COEFF       0x680508
#define NV4_PRAMDAC_PLL_COEFF_SELECT 0x68050c
#define NV4_PRAMDAC_PLL_SETUP_CTRL   0x680510
#define NV4_PRAMDAC_PLL_TEST_COUNTER 0x680514
#define NV4_PRAMDAC_PALETTE_TEST     0x680518
#define NV4_PRAMDAC_GENERAL_CONTROL  0x680600
#define NV4_PRAMDAC_TEST_CONTROL     0x680608
#define NV4_PRAMDAC_CHECKSUM         0x68060c

#define NV4_PRAMDAC_GC_ALT_MODE      (1u << 12) /* 565 at 16 bpp */
#define NV4_PRAMDAC_GC_BPC_8BITS     (1u << 20) /* 8 bits per palette component */

/* PRMIO: real-mode access through I/O 3D0-3D3 */
#define NV4_PRMIO_RMA_ID         0x007100
#define NV4_PRMIO_RMA_PTR        0x007104
#define NV4_PRMIO_RMA_DATA       0x007108
#define NV4_PRMIO_RMA_DATA32     0x00710c
#define NV4_RMA_ID_CODE          0x2b16d065

/* Extended CRTC indexes (unlocked by writing 0x57 to CR1F) */
#define NV4_CR_RPC0              0x19 /* start address 20:16, row offset 10:8 */
#define NV4_CR_RPC1              0x1a
#define NV4_CR_FF                0x1b
#define NV4_CR_ENH               0x1c
#define NV4_CR_PAGE0             0x1d /* write page */
#define NV4_CR_PAGE1             0x1e /* read page */
#define NV4_CR_LOCK              0x1f
#define NV4_CR_FFLWM             0x20
#define NV4_CR_SHADOW_LOCK       0x21
#define NV4_CR_FAB_ID            0x23
#define NV4_CR_LSR               0x25
#define NV4_CR_CHIP_ID           0x27
#define NV4_CR_PIXEL             0x28
#define NV4_CR_PAGE_OVFL         0x29
#define NV4_CR_HEB               0x2d
#define NV4_CR_HCUR_ADDR2        0x2f
#define NV4_CR_HCUR_ADDR0        0x30
#define NV4_CR_HCUR_ADDR1        0x31
#define NV4_CR_RL0               0x34
#define NV4_CR_RL1               0x35
#define NV4_CR_RMA               0x38
#define NV4_CR_ILACE             0x39
#define NV4_CR_DDC_STATUS        0x3e
#define NV4_CR_DDC_WR            0x3f
#define NV4_CR_PCI_TO            0x40

#define NV4_CR_LOCK_RW           0x57
#define NV4_CR_LOCK_RO           0x75
#define NV4_CR_LOCK_LOCK         0x99

/* ------------------------------------------------------------------ */
/* PFIFO.                                                              */
/* ------------------------------------------------------------------ */
#define NV4_CHANNELS    16
#define NV4_SUBCHANNELS 8
#define NV4_CACHE1_SIZE 32 /* entries; PIO channels see 124 bytes free */

/* RAMHT engines */
#define NV4_ENGINE_SW       0
#define NV4_ENGINE_GRAPHICS 1
#define NV4_ENGINE_DVD      2

/* What an engine did with a method the puller handed it. */
typedef enum nv4_mthd_result_e {
    NV4_MTHD_DONE,  /* taken (executed, or trapped to the driver) */
    NV4_MTHD_STALL  /* not taken: the engine is held; retry later */
} nv4_mthd_result_t;

typedef struct nv4_pfifo_s {
    uint32_t delay_0;
    uint32_t dma_timeslice;
    uint32_t pio_timeslice;
    uint32_t timeslice;
    uint32_t next_channel;
    uint32_t intr;
    uint32_t intr_en;
    uint32_t ramht;
    uint32_t ramfc;
    uint32_t ramro;
    uint32_t caches;       /* REASSIGN */
    uint32_t mode;         /* bit per channel: DMA */
    uint32_t dma;          /* bit per channel: DMA work pending */
    uint32_t size;
    uint32_t runout_put;
    uint32_t runout_get;

    /* CACHE0: one entry, used by the driver to resend a method */
    uint32_t c0_push0;
    uint32_t c0_push1;
    uint32_t c0_pull0;
    uint32_t c0_pull1;
    uint32_t c0_hash;
    uint32_t c0_put;
    uint32_t c0_get;
    uint32_t c0_engine;
    uint32_t c0_method;
    uint32_t c0_data;

    /* CACHE1 and its channel's DMA pusher */
    uint32_t c1_push0;
    uint32_t c1_push1;     /* CHID 3:0, MODE 8 */
    uint32_t c1_dma_push;  /* ACCESS 0, STATE 4, BUFFER 8, STATUS 12 */
    uint32_t c1_dma_fetch;
    uint32_t c1_dma_put;
    uint32_t c1_dma_get;
    uint32_t c1_dma_dcount;
    uint32_t c1_dma_jmp_shadow;
    uint32_t c1_dma_rsvd_shadow;
    uint32_t c1_dma_data_shadow;
    uint32_t c1_dma_state; /* METHOD 12:2, SUBCH 15:13, COUNT 28:18, ERROR 31:30 */
    uint32_t c1_dma_instance;
    uint32_t c1_dma_ctl;
    uint32_t c1_dma_limit;
    uint32_t c1_dma_tlb_tag;
    uint32_t c1_dma_tlb_pte;
    uint32_t c1_pull0;     /* ACCESS 0, HASH failed 4, DEVICE software 8 */
    uint32_t c1_pull1;     /* ENGINE 1:0 */
    uint32_t c1_hash;
    uint32_t c1_put;       /* entry index */
    uint32_t c1_get;
    uint32_t c1_engine;    /* 2 bits per subchannel, 4 apart */
    uint32_t c1_method[NV4_CACHE1_SIZE];
    uint32_t c1_data[NV4_CACHE1_SIZE];

    int busy;              /* the run loop is active (no reentry) */
} nv4_pfifo_t;

/* ------------------------------------------------------------------ */
/* PGRAPH.                                                             */
/* ------------------------------------------------------------------ */
#define NV4_PGRAPH_DEBUG_0         0x400080
#define NV4_PGRAPH_DEBUG_1         0x400084
#define NV4_PGRAPH_DEBUG_2         0x400088
#define NV4_PGRAPH_DEBUG_3         0x40008c
#define NV4_PGRAPH_INTR            0x400100
#define NV4_PGRAPH_NSTATUS         0x400104
#define NV4_PGRAPH_NSOURCE         0x400108
#define NV4_PGRAPH_INTR_EN         0x400140
#define NV4_PGRAPH_CTX_SWITCH1     0x400160 /* class, flags, operation */
#define NV4_PGRAPH_CTX_SWITCH2     0x400164 /* mono/color format, notify DMA */
#define NV4_PGRAPH_CTX_SWITCH3     0x400168 /* DMA objects A and B */
#define NV4_PGRAPH_CTX_SWITCH4     0x40016c /* object instance */
#define NV4_PGRAPH_CTX_CONTROL     0x400170
#define NV4_PGRAPH_CTX_USER        0x400174 /* subchannel 15:13, channel 27:24 */
#define NV4_PGRAPH_CTX_CACHE1      0x400180 /* 8 each, one per subchannel */
#define NV4_PGRAPH_CTX_CACHE2      0x4001a0
#define NV4_PGRAPH_CTX_CACHE3      0x4001c0
#define NV4_PGRAPH_CTX_CACHE4      0x4001e0
#define NV4_PGRAPH_SOURCE_COLOR    0x40050c
#define NV4_PGRAPH_UCLIP_MIN       0x40053c /* x, y */
#define NV4_PGRAPH_UCLIP_MAX       0x400544 /* x, y */
#define NV4_PGRAPH_UCLIPA_MIN      0x400560 /* x, y: the image classes' own clip */
#define NV4_PGRAPH_UCLIPA_MAX      0x400568 /* x, y */
#define NV4_PGRAPH_MONO_COLOR0     0x400600
#define NV4_PGRAPH_ROP3            0x400604
#define NV4_PGRAPH_BETA_AND        0x400608
#define NV4_PGRAPH_BETA_PREMULT    0x40060c
#define NV4_PGRAPH_BOFFSET         0x400640 /* 6 surfaces */
#define NV4_PGRAPH_BBASE           0x400658
#define NV4_PGRAPH_BPITCH          0x400670 /* 5 */
#define NV4_PGRAPH_BLIMIT          0x400684 /* 6; bit 31: no surface */
#define NV4_PGRAPH_BSWIZZLE2       0x40069c
#define NV4_PGRAPH_BSWIZZLE5       0x4006a0
#define NV4_PGRAPH_STATUS          0x400700
#define NV4_PGRAPH_TRAPPED_ADDR    0x400704
#define NV4_PGRAPH_TRAPPED_DATA    0x400708
#define NV4_PGRAPH_SURFACE         0x40070c
#define NV4_PGRAPH_STATE           0x400710
#define NV4_PGRAPH_NOTIFY          0x400714
#define NV4_PGRAPH_FIFO            0x400720
#define NV4_PGRAPH_BPIXEL          0x400724 /* 4 bits per surface */
#define NV4_PGRAPH_FFINTFC_ST2     0x400754
#define NV4_PGRAPH_FFINTFC_ST2_D   0x400758
#define NV4_PGRAPH_PATT_COLOR0     0x400800
#define NV4_PGRAPH_PATT_COLOR1     0x400804
#define NV4_PGRAPH_PATTERN         0x400808 /* 2: the 8x8 mono pattern */
#define NV4_PGRAPH_PATTERN_SHAPE   0x400810 /* shape 1:0, color select 4 */
#define NV4_PGRAPH_CHROMA          0x400814
#define NV4_PGRAPH_STORED_FMT      0x400830 /* formats of mono, pattern, chroma */
#define NV4_PGRAPH_PATT_COLORRAM   0x400900 /* 64: the color pattern, X8R8G8B8 */

#define NV4_PGRAPH_INTR_NOTIFY         (1u << 0)
#define NV4_PGRAPH_INTR_MISSING_HW     (1u << 4)
#define NV4_PGRAPH_INTR_CONTEXT_SWITCH (1u << 12)
#define NV4_PGRAPH_INTR_BUFFER_NOTIFY  (1u << 16)

/* NSOURCE: why a NOTIFY interrupt was raised */
#define NV4_NSOURCE_NOTIFICATION   (1u << 0)
#define NV4_NSOURCE_DATA_ERROR     (1u << 1)
#define NV4_NSOURCE_PROTECTION     (1u << 2)
#define NV4_NSOURCE_ILLEGAL_MTHD   (1u << 6)
#define NV4_NSOURCE_STATE_INVALID  (1u << 11)
#define NV4_NSOURCE_DOUBLE_NOTIFY  (1u << 12)

/* Surfaces, as PGRAPH numbers them */
#define NV4_SURF_DST   0
#define NV4_SURF_SRC   1
#define NV4_SURF_COLOR 2
#define NV4_SURF_ZETA  3
#define NV4_SURF_SWZ   5

/* What a class method did. */
#define NV4_GR_OK      0
#define NV4_GR_ILLEGAL 1 /* not a method of the class: the driver emulates it */
#define NV4_GR_MISSING 2 /* a software method */

/* Per-channel progress of primitives and transfers: what a method stream
   leaves half done (vertices, image data). Object state proper lives in
   the PGRAPH registers, which the driver saves and restores on a channel
   switch. */
typedef struct nv4_gr_chan_s {
    uint32_t color;            /* solid primitives, GDI color 1 */
    uint32_t color0;           /* GDI color 0 */
    int32_t  vx[3], vy[3];     /* lines and triangles */
    int      nvtx;

    /* image transfers: IFC, SIFC, IIFC, GDI blocks C/E */
    int32_t  img_x, img_y;     /* top left */
    int32_t  img_dx, img_dy;   /* SIFC: 12.20 output step per input pixel */
    int32_t  img_fx, img_fy;   /* SIFC: 12.4 output origin */
    uint32_t in_w, in_h;
    uint32_t out_w, out_h;
    uint32_t pos;              /* pixels received */
    uint32_t sifc_size;        /* SIFC input size, latched until the point */
    int      img_kind;         /* which GDI block or class is streaming */
    uint32_t iifc_pal;         /* IIFC palette offset */
    uint32_t iifc_fmt;         /* IIFC index format */
    uint32_t font;             /* GDI blocks F/G: pitch 31:28, offset 27:0 */

    /* blit */
    int32_t  blit_sx, blit_sy, blit_dx, blit_dy;

    /* M2MF */
    uint32_t m2mf_in, m2mf_out;
    int32_t  m2mf_pitch_in, m2mf_pitch_out;
    uint32_t m2mf_len, m2mf_count, m2mf_fmt;

    /* SIFM */
    int32_t  sifm_x, sifm_y;
    uint32_t sifm_w, sifm_h;
    int32_t  sifm_dudx, sifm_dvdy;
    uint32_t sifm_in_size, sifm_in_fmt, sifm_in_off;

    /* DVD subpicture: the output rectangle, then the image and the
       overlay, each a scaled source in its own DMA object */
    int32_t  dvd_x, dvd_y;
    uint32_t dvd_w, dvd_h;
    struct {
        int32_t  dudx, dvdy;
        uint32_t size, fmt, off, point;
    } dvd[2];
} nv4_gr_chan_t;

/* The 3D classes' state, as the methods gave it, per channel: the
   driver never touches it, so a channel switch needs no save. */
typedef struct nv4_d3d_vtx_s {
    float    sx, sy, sz, rhw;
    uint32_t color;    /* A8R8G8B8 */
    uint32_t specular; /* fog 31:24, R8G8B8 */
    float    tu[2], tv[2];
} nv4_d3d_vtx_t;

typedef struct nv4_d3d_s {
    uint32_t      color_key;
    uint32_t      offset[2];
    uint32_t      format[2];
    uint32_t      filter[2];
    uint32_t      blend;
    uint32_t      control[3];     /* DX5: [0] only */
    uint32_t      fog_color;
    uint32_t      comb_alpha[2];  /* DX6 */
    uint32_t      comb_color[2];
    uint32_t      comb_factor;
    nv4_d3d_vtx_t vtx[16];
    int           state;          /* index of the derived draw state in the batch, -1: none */
} nv4_d3d_t;

struct nv4_3d_batch_s;

/* ------------------------------------------------------------------ */
/* Board options.                                                      */
/* ------------------------------------------------------------------ */
typedef struct nv4_board_s {
    const char *name;         /* its "bios" option value */
    const char *rom_path;
    uint16_t    device_id;
    nv4_chip_t  chip;
    uint8_t     revision;     /* PCI revision and PMC_BOOT_0 revision */
    uint8_t     mem_64bit;    /* 64-bit memory bus (M64, Vanta) */
    uint8_t     sgram;        /* SGRAM rather than SDRAM */
    uint8_t     crystal_14m;  /* 14.318 MHz crystal rather than 13.5 MHz */
} nv4_board_t;

/* ------------------------------------------------------------------ */
/* Chip state.                                                         */
/* ------------------------------------------------------------------ */
typedef struct nv4_s {
    svga_t             svga;
    const nv4_board_t *board;
    nv4_chip_t         chip;
    int                is_agp;

    /* PCI */
    uint8_t  pci_regs[256];
    uint8_t  pci_slot;
    uint8_t  irq_state;
    uint32_t mmio_base;     /* BAR0 */
    uint32_t lfb_base;      /* BAR1 */
    uint32_t lfb_size;      /* 16 MB (NV4) or 32 MB (NV5) */
    uint16_t subsys_vendor;
    uint16_t subsys_id;

    /* Memory */
    uint32_t vram_size;
    uint32_t vram_mask;
    uint32_t ramin_flip;    /* PRAMIN offset ^ ramin_flip = VRAM offset */

    rom_t         bios_rom;
    mem_mapping_t mmio_mapping;
    mem_mapping_t lfb_mapping;

    /* PMC */
    uint32_t pmc_intr;      /* software interrupt bit only; the rest is gathered */
    uint32_t pmc_intr_en;
    uint32_t pmc_enable;

    /* PBUS */
    uint32_t pbus_debug[4];
    uint32_t pbus_intr;
    uint32_t pbus_intr_en;
    uint32_t pbus_rom_config;
    uint32_t pbus_pci_nv20; /* ROM shadow */
    uint32_t pbus_pci_nv22; /* scratch */
    uint32_t pbus_pci_nv23;

    /* PTIMER (counts nanoseconds of guest time) */
    uint32_t   ptimer_intr;
    uint32_t   ptimer_intr_en;
    uint32_t   ptimer_num;
    uint32_t   ptimer_den;
    uint64_t   ptimer_time;
    uint32_t   ptimer_alarm;
    uint64_t   ptimer_last_tsc;
    double     ptimer_frac;
    pc_timer_t ptimer_timer;

    /* Engine time: the puller hands PGRAPH a method only once the
       work before it is done in guest time (nv4_pfifo.c) */
    uint64_t   eng_tsc;   /* when PGRAPH finishes what it was given */
    uint64_t   eng_bytes; /* framebuffer bytes moved by the current method */
    uint64_t   eng_px3d;  /* 3D pixels drawn by the current method */
    pc_timer_t eng_timer;

    /* PFB and PEXTDEV */
    uint32_t pfb_boot_0;
    uint32_t pfb_debug_0;
    uint32_t pfb_green_0;
    uint32_t pfb_config_0;
    uint32_t pfb_config_1;
    uint32_t pfb_rtl;
    uint32_t straps;        /* PEXTDEV_BOOT_0 as strapped */
    uint32_t straps_ovr;    /* as overwritten by software (bit 11 set) */

    /* PCRTC */
    uint32_t pcrtc_intr;
    uint32_t pcrtc_intr_en;
    uint32_t pcrtc_start;
    uint32_t pcrtc_config;

    /* PRAMDAC */
    uint32_t cursor_pos;
    uint32_t nvpll;
    uint32_t mpll;
    uint32_t vpll;
    uint32_t pll_select;
    uint32_t pll_setup;
    uint32_t pll_test;
    uint32_t palette_test;
    uint32_t general_control;
    uint32_t test_control;
    uint32_t tv_regs[8];    /* TV_SETUP .. TV_TEST_CONTROL */
    double   nvclk_hz;
    double   mclk_hz;
    double   vclk_hz;

    /* Legacy VGA extensions */
    uint8_t  cr_lock;       /* 0 locked, 1 read-only, 3 read/write */
    uint8_t  rma_mode;      /* CR38 */
    uint32_t rma_ptr;
    uint32_t rma_data32;    /* coalescing buffer of RMA DATA32 */
    uint8_t  pci_to;        /* CR40 */

    /* DDC */
    void *i2c;
    void *ddc;

    nv4_pfifo_t pfifo;

    /* PGRAPH: the register file is the engine's state */
    uint32_t      pgraph_regs[0x2000 / 4];
    nv4_gr_chan_t gr_chan[NV4_CHANNELS];
    nv4_d3d_t     d3d[NV4_CHANNELS];
    struct nv4_3d_batch_s *batch3d; /* triangles set up, not yet drawn */
    int           render_threads;     /* 3D span shading threads, 1 = inline */
    int           gpu_raster;         /* 3D span shading on the host GPU */
    uint32_t      dbg_cls_count[256]; /* methods per class, for the debug hook */
    /* what the guest asked for that is not modeled (nv4_gap) */
    struct {
        uint8_t     cls;
        uint16_t    mthd;
        const char *what;
        uint32_t    count;
    } dbg_gap[64];
    int           dbg_gaps;
    /* channel control area accesses, for the debug hook */
    struct {
        uint32_t reads, writes, last_off, last_val;
    } dbg_user[16];
    uint32_t      dbg_trace;          /* methods left to log */
    int           dbg_trace_ch;       /* only this channel's, or -1 */
    int           dbg_trace_novtx;    /* methods and binds only, no 3D vertices or pushes */
    int           dbg_log_cls;        /* log every method of this class ("dev logclass", NV4_LOGCLASS), or -1 */
    uint32_t      dbg_regtrace;       /* register writes left to log */
    /* "dev reads N": the registers read most over the next N reads */
    uint32_t      dbg_reads_left;
    uint32_t      dbg_read_addr[64], dbg_read_count[64];
    uint32_t      dbg_vblanks;        /* vertical blanks, for "dev 3d" */
    uint32_t      vblank_count;       /* vertical blanks since power-on */
    /* DMA into system memory, for the debug hook: the last writes and
       the translations through a page table entry not marked present */
    struct {
        uint32_t inst, offset, addr, len;
    } dbg_sysw[8];
    uint32_t      dbg_sysw_n;
    uint32_t      dbg_dma_np, dbg_np_inst, dbg_np_offset, dbg_np_pte;
    /* 2D pixels with a plain store gathered into a run on one row, to
       join the 3D batch as a fill (nv4_pgraph_2d.c) */
    struct {
        int      n;
        uint32_t base, pitch, lim, v;
        int      bpp;
        int32_t  y, x0, x1;
    } run2d;

    /* PVIDEO (nv4_pvideo.c): the overlay's registers, its interrupt
       and the buffer being scanned out */
    uint32_t pvideo_regs[0x300 / 4];
    uint32_t pvideo_intr, pvideo_intr_en;
    uint32_t pvideo_shown;

    /* Register file of the unit not yet modeled: what the driver wrote
       reads back. */
    uint32_t pme_regs[0x1000 / 4];
} nv4_t;

/* nv4_core.c */
#ifdef ENABLE_NV4_LOG
extern void nv4_log(const char *fmt, ...);
#else
#    define nv4_log(fmt, ...)
#endif
extern uint32_t nv4_mmio_read(nv4_t *nv4, uint32_t addr, int len);
extern void     nv4_mmio_write(nv4_t *nv4, uint32_t addr, uint32_t val, int len);
extern uint8_t  nv4_pci_cfg_read(nv4_t *nv4, int addr);
extern void     nv4_pci_cfg_write(nv4_t *nv4, int addr, uint8_t val);
extern uint8_t  nv4_rom_read(nv4_t *nv4, uint32_t off);
extern uint32_t nv4_vram_size_code(const nv4_t *nv4);

/* nv4_vga.c */
extern uint8_t nv4_vga_in(uint16_t addr, void *priv);
extern void    nv4_vga_out(uint16_t addr, uint8_t val, void *priv);
extern void    nv4_vga_init(nv4_t *nv4);
extern void    nv4_vga_close(nv4_t *nv4);
extern void    nv4_vga_reset(nv4_t *nv4);
extern void    nv4_vga_update_mapping(nv4_t *nv4);
extern void    nv4_vga_update_banks(nv4_t *nv4);
extern void    nv4_vga_mem_access(nv4_t *nv4, uint32_t addr, int n, int write);

/* subsystems/nv4_pmc.c */
extern void     nv4_pmc_reset(nv4_t *nv4);
extern uint32_t nv4_pmc_read(nv4_t *nv4, uint32_t addr);
extern void     nv4_pmc_write(nv4_t *nv4, uint32_t addr, uint32_t val);
extern uint32_t nv4_pmc_pending(nv4_t *nv4);
extern void     nv4_pmc_update_irq(nv4_t *nv4);

/* subsystems/nv4_pbus.c */
extern void     nv4_pbus_reset(nv4_t *nv4);
extern uint32_t nv4_pbus_read(nv4_t *nv4, uint32_t addr);
extern void     nv4_pbus_write(nv4_t *nv4, uint32_t addr, uint32_t val);
extern uint32_t nv4_prmio_read(nv4_t *nv4, uint32_t addr);
extern void     nv4_prmio_write(nv4_t *nv4, uint32_t addr, uint32_t val);

/* subsystems/nv4_ptimer.c */
extern void     nv4_ptimer_init(nv4_t *nv4);
extern void     nv4_ptimer_reset(nv4_t *nv4);
extern uint32_t nv4_ptimer_read(nv4_t *nv4, uint32_t addr);
extern void     nv4_ptimer_write(nv4_t *nv4, uint32_t addr, uint32_t val);

/* subsystems/nv4_pfb.c */
extern void     nv4_pfb_reset(nv4_t *nv4);
extern uint32_t nv4_pfb_read(nv4_t *nv4, uint32_t addr);
extern void     nv4_pfb_write(nv4_t *nv4, uint32_t addr, uint32_t val);
extern uint32_t nv4_pextdev_read(nv4_t *nv4, uint32_t addr);
extern void     nv4_pextdev_write(nv4_t *nv4, uint32_t addr, uint32_t val);
/* "dev trace N [CH]": log this event of channel <ch>? (counts it) */
#define NV4_TRACE(nv4, ch) \
    ((nv4)->dbg_trace && (((nv4)->dbg_trace_ch < 0) || ((nv4)->dbg_trace_ch == (int) (ch))) && (nv4)->dbg_trace--)

extern uint32_t nv4_straps(const nv4_t *nv4);
extern void     nv4_pfifo_ramht_dump(nv4_t *nv4, uint32_t handle);
extern const nv4_board_t *nv4_board_find(const char *name);
extern double   nv4_crystal_hz(const nv4_t *nv4);

/* subsystems/nv4_pramdac.c */
extern void     nv4_pramdac_reset(nv4_t *nv4);
extern uint32_t nv4_pramdac_read(nv4_t *nv4, uint32_t addr);
extern void     nv4_pramdac_write(nv4_t *nv4, uint32_t addr, uint32_t val);
extern double   nv4_pll_hz(const nv4_t *nv4, uint32_t coeff);

/* subsystems/nv4_pcrtc.c */
extern void     nv4_pcrtc_reset(nv4_t *nv4);
extern uint32_t nv4_pcrtc_read(nv4_t *nv4, uint32_t addr);
extern void     nv4_pcrtc_write(nv4_t *nv4, uint32_t addr, uint32_t val);
extern void     nv4_recalctimings(svga_t *svga);
extern void     nv4_hwcursor_draw(svga_t *svga, int displine);
extern void     nv4_vblank_start(svga_t *svga);
extern void     nv4_pcrtc_crtc_start_write(nv4_t *nv4, uint8_t idx, uint8_t val);

/* subsystems/nv4_pramin.c */
extern uint8_t  nv4_ramin_read8(nv4_t *nv4, uint32_t off);
extern uint16_t nv4_ramin_read16(nv4_t *nv4, uint32_t off);
extern uint32_t nv4_ramin_read32(nv4_t *nv4, uint32_t off);
extern void     nv4_ramin_write8(nv4_t *nv4, uint32_t off, uint8_t val);
extern void     nv4_ramin_write16(nv4_t *nv4, uint32_t off, uint16_t val);
extern void     nv4_ramin_write32(nv4_t *nv4, uint32_t off, uint32_t val);

/* subsystems/nv4_dma.c: DMA objects */
#define NV4_DMA_OK    0
#define NV4_DMA_FAULT 1 /* bad class, past the limit, or no page */
extern int      nv4_dma_translate(nv4_t *nv4, uint32_t inst, uint32_t offset, int write, uint32_t *addr, int *target);
extern int      nv4_dma_read32(nv4_t *nv4, uint32_t inst, uint32_t offset, uint32_t *val);
extern int      nv4_dma_write32(nv4_t *nv4, uint32_t inst, uint32_t offset, uint32_t val);
extern int      nv4_dma_read(nv4_t *nv4, uint32_t inst, uint32_t offset, uint8_t *buf, uint32_t len);
extern int      nv4_dma_write(nv4_t *nv4, uint32_t inst, uint32_t offset, const uint8_t *buf, uint32_t len);
extern uint32_t nv4_dma_class(nv4_t *nv4, uint32_t inst);
extern uint32_t nv4_dma_limit(nv4_t *nv4, uint32_t inst);
extern uint32_t nv4_dma_page_left(nv4_t *nv4, uint32_t inst, uint32_t offset);

/* subsystems/nv4_pfifo.c */
extern void     nv4_pfifo_reset(nv4_t *nv4);
extern uint32_t nv4_pfifo_read(nv4_t *nv4, uint32_t addr);
extern void     nv4_pfifo_write(nv4_t *nv4, uint32_t addr, uint32_t val);
extern uint32_t nv4_pfifo_pending(nv4_t *nv4);
extern uint32_t nv4_user_read(nv4_t *nv4, uint32_t addr);
extern void     nv4_user_write(nv4_t *nv4, uint32_t addr, uint32_t val);
extern void     nv4_pfifo_run(nv4_t *nv4);
extern void     nv4_pfifo_init(nv4_t *nv4);
extern void     nv4_pfifo_close(nv4_t *nv4);
extern int      nv4_pfifo_engine_busy(nv4_t *nv4);

/* subsystems/nv4_pgraph.c: the puller's way in */
extern nv4_mthd_result_t nv4_pgraph_method(nv4_t *nv4, int chid, int subc, uint32_t mthd, uint32_t data);
extern void nv4_gap(nv4_t *nv4, int cls, uint32_t mthd, const char *what);

#define NV4_GR(nv4, addr) ((nv4)->pgraph_regs[((addr) - NV4_PGRAPH_START) >> 2])
extern void     nv4_pgraph_notify_write(nv4_t *nv4, uint32_t index, uint32_t info32);
extern void     nv4_pgraph_blowup(nv4_t *nv4, uint32_t nsource);
extern void     nv4_pgraph_set_grobj(nv4_t *nv4, int word, int shift, int bits, uint32_t val);

/* subsystems/nv4_pgraph_2d.c: the 2D classes */
extern int  nv4_pgraph_2d_method(nv4_t *nv4, nv4_gr_chan_t *ch, int cls, uint32_t mthd, uint32_t data);
extern void nv4_pgraph_2d_reset_chan(nv4_gr_chan_t *ch);

/* subsystems/nv4_pgraph_3d.c: the Direct3D classes */
extern int  nv4_pgraph_3d_method(nv4_t *nv4, int chid, int cls, uint32_t mthd, uint32_t data);
extern void nv4_3d_flush(nv4_t *nv4);
/* Who asks for the 3D pixels (for "dev 3d"): a PGRAPH class 0-255, or */
#define NV4_SYNC_LFB    256
#define NV4_SYNC_VGA    257
#define NV4_SYNC_VBLANK 258
#define NV4_SYNC_CAUSES 259
extern void nv4_3d_sync(nv4_t *nv4, int cause);
extern void nv4_3d_touch(nv4_t *nv4, uint32_t lo, uint32_t hi, int cause);
extern void nv4_3d_access(nv4_t *nv4, uint32_t lo, uint32_t hi, int cause);
extern int  nv4_3d_joins(nv4_t *nv4, uint32_t base, uint32_t pitch, uint32_t lo, uint32_t hi);
extern void nv4_pgraph_2d_flush_run(nv4_t *nv4);
extern int  nv4_3d_fill(nv4_t *nv4, uint32_t base, uint32_t pitch, uint32_t lim, int bpp, int32_t x0, int32_t y0,
                        int32_t x1, int32_t y1, uint32_t val);
/* A ROP fill's words (nv4_3d_fill_rop): A and B for pattern color 0,
   then for color 1, the mono pattern's two words, then the flags: the
   pattern shape (0 8x8, 1 a row of 64, 2 a column of 64) and these */
#define NV4_ROPFILL_SKIP0 0x04 /* pattern color 0 leaves its pixels alone */
#define NV4_ROPFILL_SKIP1 0x08
#define NV4_ROPFILL_PAT   0x10 /* the pattern picks A and B (else color 0's) */
extern int  nv4_3d_fill_rop(nv4_t *nv4, uint32_t base, uint32_t pitch, uint32_t lim, int bpp, int32_t x0, int32_t y0,
                            int32_t x1, int32_t y1, const uint32_t rop[7]);
extern void nv4_3d_vblank(nv4_t *nv4, uint32_t lo, uint32_t hi);
extern void nv4_3d_init(nv4_t *nv4);
extern void nv4_3d_close(nv4_t *nv4);
extern void nv4_3d_reset(nv4_t *nv4);
extern void nv4_3d_stats(nv4_t *nv4);

/* subsystems/nv4_pgraph.c, nv4_pvideo.c, nv4_pme.c */
extern void     nv4_pgraph_reset(nv4_t *nv4);
extern uint32_t nv4_pgraph_read(nv4_t *nv4, uint32_t addr);
extern void     nv4_pgraph_write(nv4_t *nv4, uint32_t addr, uint32_t val);
extern uint32_t nv4_pgraph_pending(nv4_t *nv4);
extern void     nv4_pvideo_reset(nv4_t *nv4);
extern uint32_t nv4_pvideo_read(nv4_t *nv4, uint32_t addr);
extern void     nv4_pvideo_write(nv4_t *nv4, uint32_t addr, uint32_t val);
extern uint32_t nv4_pvideo_pending(nv4_t *nv4);
extern void     nv4_pvideo_vblank(nv4_t *nv4);
extern void     nv4_pvideo_draw(svga_t *svga, int displine);
extern void     nv4_pme_reset(nv4_t *nv4);
extern uint32_t nv4_pme_read(nv4_t *nv4, uint32_t addr);
extern void     nv4_pme_write(nv4_t *nv4, uint32_t addr, uint32_t val);
extern uint32_t nv4_pme_pending(nv4_t *nv4);

#endif /* VIDEO_NV4_H */
