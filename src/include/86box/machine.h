/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Handling of the emulated machines.
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2016-2025 Miran Grca.
 *          Copyright 2017-2020 Fred N. van Kempen.
 *          Copyright 2025      Jasmine Iwanek.
 */
#ifndef EMU_MACHINE_H
#define EMU_MACHINE_H

/* Machine feature flags. */
#define MACHINE_BUS_NONE      0x00000000 /* sys has no bus */
/* Feature flags for BUS'es. */
#define MACHINE_BUS_CASSETTE  0x00000001 /* sys has cassette port */
#define MACHINE_BUS_SIDECAR   0x00000002 /* sys has PCjr sidecar bus */
#define MACHINE_BUS_ISA       0x00000004 /* sys has ISA bus */
#define MACHINE_BUS_XT_KBD    0x00000008 /* sys has an XT keyboard port */
#define MACHINE_BUS_CBUS      0x00000010 /* sys has C-BUS bus */
#define MACHINE_BUS_ISA16     0x00000020 /* sys has ISA16 bus - PC/AT architecture */
#define MACHINE_BUS_AT_KBD    0x00000040 /* sys has an AT keyboard port */
#define MACHINE_BUS_MCA       0x00000080 /* sys has MCA bus */
#define MACHINE_BUS_MCA32     0x00000100 /* sys has MCA32 bus */
#define MACHINE_BUS_PS2_PORTS 0x00000200 /* system has PS/2 keyboard and mouse ports */
#define MACHINE_BUS_PS2       MACHINE_BUS_PS2_PORTS
#define MACHINE_BUS_PCMCIA    0x00000400 /* sys has PCMCIA bus */
#define MACHINE_BUS_HIL       0x00000800 /* system has HP HIL keyboard and mouse ports */
#define MACHINE_BUS_EISA      0x00001000 /* sys has EISA bus */
#define MACHINE_BUS_AT32      0x00002000 /* sys has Mylex AT/32 local bus */
#define MACHINE_BUS_OLB       0x00004000 /* sys has OPTi local bus */
#define MACHINE_BUS_VLB       0x00008000 /* sys has VL bus */
#define MACHINE_BUS_PCI       0x00010000 /* sys has PCI bus */
#define MACHINE_BUS_CARDBUS   0x00020000 /* sys has CardBus bus */
#define MACHINE_BUS_USB       0x00040000 /* sys has USB bus */
#define MACHINE_BUS_AGP       0x00080000 /* sys has AGP bus */
#define MACHINE_BUS_AC97      0x00100000 /* sys has AC97 bus (ACR/AMR/CNR slot) */
/* Aliases. */
#define MACHINE_CASSETTE    (MACHINE_BUS_CASSETTE) /* sys has cassette port */
/* Combined flags. */
#define MACHINE_PC          (MACHINE_BUS_ISA)                     /* sys is PC/XT-compatible (ISA) */
#define MACHINE_AT          (MACHINE_BUS_ISA | MACHINE_BUS_ISA16) /* sys is AT-compatible (ISA + ISA16) */
#define MACHINE_PC98        (MACHINE_BUS_CBUS)                    /* sys is NEC PC-98x1 series */
#define MACHINE_EISA        (MACHINE_BUS_EISA | MACHINE_AT)       /* sys is AT-compatible with EISA */
#define MACHINE_VLB         (MACHINE_BUS_VLB | MACHINE_AT)        /* sys is AT-compatible with VLB */
#define MACHINE_VLB98       (MACHINE_BUS_VLB | MACHINE_PC98)      /* sys is NEC PC-98x1 series with VLB (did that even exist?) */
#define MACHINE_VLBE        (MACHINE_BUS_VLB | MACHINE_EISA)      /* sys is AT-compatible with EISA and VLB */
#define MACHINE_MCA         (MACHINE_BUS_MCA)                     /* sys is MCA */
#define MACHINE_PCI         (MACHINE_BUS_PCI | MACHINE_AT)        /* sys is AT-compatible with PCI */
#define MACHINE_PCI98       (MACHINE_BUS_PCI | MACHINE_PC98)      /* sys is NEC PC-98x1 series with PCI */
#define MACHINE_PCIE        (MACHINE_BUS_PCI | MACHINE_EISA)      /* sys is AT-compatible with PCI, and EISA */
#define MACHINE_PCIV        (MACHINE_BUS_PCI | MACHINE_VLB)       /* sys is AT-compatible with PCI and VLB */
#define MACHINE_PCIVE       (MACHINE_BUS_PCI | MACHINE_VLBE)      /* sys is AT-compatible with PCI, VLB, and EISA */
#define MACHINE_PCMCIA      (MACHINE_BUS_PCMCIA | MACHINE_AT)     /* sys is AT-compatible laptop with PCMCIA */
#define MACHINE_AGP         (MACHINE_BUS_AGP | MACHINE_PCI)       /* sys is AT-compatible with AGP  */
#define MACHINE_AGP98       (MACHINE_BUS_AGP | MACHINE_PCI98)     /* sys is NEC PC-98x1 series with AGP (did that even exist?) */

#define MACHINE_PC5150      (MACHINE_CASSETTE | MACHINE_PC)          /* sys is IBM PC 5150 */
#define MACHINE_PCJR        (MACHINE_CASSETTE | MACHINE_BUS_SIDECAR) /* sys is PCjr */
#define MACHINE_PS2         (MACHINE_AT | MACHINE_BUS_PS2)           /* sys is PS/2 */
#define MACHINE_PS2_MCA     (MACHINE_MCA | MACHINE_BUS_PS2)          /* sys is MCA PS/2 */
#define MACHINE_PS2_VLB     (MACHINE_VLB | MACHINE_BUS_PS2)          /* sys is VLB PS/2 */
#define MACHINE_PS2_PCI     (MACHINE_PCI | MACHINE_BUS_PS2)          /* sys is PCI PS/2 */
#define MACHINE_PS2_PCIV    (MACHINE_PCIV | MACHINE_BUS_PS2)         /* sys is VLB/PCI PS/2 */
#define MACHINE_PS2_AGP     (MACHINE_AGP | MACHINE_BUS_PS2)          /* sys is AGP PS/2 */
#define MACHINE_PS2_A97     (MACHINE_PS2_AGP | MACHINE_BUS_AC97)     /* sys is AGP/AC97 PS/2 */
#define MACHINE_PS2_NOISA   (MACHINE_PS2_AGP & ~MACHINE_AT)          /* sys is AGP PS/2 without ISA */
#define MACHINE_PS2_PCIONLY (MACHINE_PS2_NOISA & ~MACHINE_BUS_AGP)   /* sys is PCI PS/2 without ISA */
#define MACHINE_PS2_NOI97   (MACHINE_PS2_A97 & ~MACHINE_AT)          /* sys is AGP/AC97 PS/2 without ISA */

/* Feature flags for miscellaneous internal devices. */
#define MACHINE_FLAGS_NONE        0x00000000 /* sys has no int devices */
#define MACHINE_SOFTFLOAT_ONLY    0x00000001 /* sys requires SoftFloat FPU */
#define MACHINE_VIDEO             0x00000002 /* sys has int video */
#define MACHINE_VIDEO_8514A       0x00000004 /* sys has int video */
#define MACHINE_VIDEO_ONLY        0x00000008 /* sys has fixed video */
#define MACHINE_KEYBOARD          0x00000010 /* sys has int keyboard */
#define MACHINE_AX                0x00000020 /* sys adheres to Japanese AX standard */
#define MACHINE_KEYBOARD_JIS      0x00000020 /* sys has int keyboard which is Japanese (AX or PS/55) */
#define MACHINE_MOUSE             0x00000040 /* sys has int mouse */
#define MACHINE_FDC               0x00000080 /* sys has int FDC */
#define MACHINE_LPT_PRI           0x00000100 /* sys has int pri LPT */
#define MACHINE_LPT_SEC           0x00000200 /* sys has int sec LPT */
#define MACHINE_LPT_TER           0x00000400 /* sys has int ter LPT */
#define MACHINE_PS2_KBC           0x00000800 /* sys has a PS/2 keyboard controller */
                                             /* this is separate from having PS/2 ports */
#define MACHINE_UART_PRI          0x00010800 /* sys has int pri UART */
#define MACHINE_UART_SEC          0x00002000 /* sys has int sec UART */
#define MACHINE_UART_TER          0x00004000 /* sys has int ter UART */
#define MACHINE_UART_QUA          0x00008000 /* sys has int qua UART */
#define MACHINE_GAMEPORT          0x00010000 /* sys has int game port */
#define MACHINE_SOUND             0x00020000 /* sys has int sound */
#define MACHINE_NIC               0x00040000 /* sys has int NIC */
/* Feature flags for advanced devices. */
#define MACHINE_APM               0x00080000 /* sys has APM */
#define MACHINE_ACPI              0x00100000 /* sys has ACPI */
#define MACHINE_PCI_INTERNAL      0x00200000 /* sys has only internal PCI */
#define MACHINE_CARTRIDGE         0x00400000 /* sys has cartridge bays */
/* Feature flags for internal storage controllers. */
#define MACHINE_MFM               0x00800000 /* sys has int MFM/RLL */
#define MACHINE_XTA               0x01000000 /* sys has int XTA */
#define MACHINE_ESDI              0x02000000 /* sys has int ESDI */
#define MACHINE_IDE_PRI           0x04000000 /* sys has int pri IDE/ATAPI */
#define MACHINE_IDE_SEC           0x08000000 /* sys has int sec IDE/ATAPI */
#define MACHINE_IDE_TER           0x10000000 /* sys has int ter IDE/ATAPI */
#define MACHINE_IDE_QUA           0x20000000 /* sys has int qua IDE/ATAPI */
#define MACHINE_SCSI              0x40000000 /* sys has int SCSI */
#define MACHINE_USB               0x80000000 /* sys has int USB */
/* Combined flags. */
#define MACHINE_LPT               (MACHINE_LPT-PRI | MACHINE_LPT_SEC | \
                                   MACHINE_LPT_TER | MACHINE_LPT_QUA)
#define MACHINE_UART              (MACHINE_UART_PRI | MACHINE_UART_SEC | \
                                   MACHINE_UART_TER | MACHINE_UART_QUA)
#define MACHINE_VIDEO_FIXED       (MACHINE_VIDEO | MACHINE_VIDEO_ONLY) /* sys has fixed int video */
#define MACHINE_SUPER_IO          (MACHINE_FDC | MACHINE_LPT_PRI | MACHINE_UART_PRI | MACHINE_UART_SEC)
#define MACHINE_SUPER_IO_GAME     (MACHINE_SUPER_IO | MACHINE_GAMEPORT)
#define MACHINE_SUPER_IO_DUAL     (MACHINE_SUPER_IO | MACHINE_LPT_SEC | \
                                   MACHINE_UART_TER | MACHINE_UART_QUA)
#define MACHINE_AV                (MACHINE_VIDEO | MACHINE_SOUND)    /* sys has video and sound */
#define MACHINE_AG                (MACHINE_SOUND | MACHINE_GAMEPORT) /* sys has sound and game port */
/* Combined flag for internal storage controllerss. */
#define MACHINE_IDE               (MACHINE_IDE_PRI) /* sys has int single IDE/ATAPI - mark as pri IDE/ATAPI */
#define MACHINE_IDE_DUAL          (MACHINE_IDE_PRI | MACHINE_IDE_SEC) /* sys has int dual IDE/ATAPI - mark as both pri and sec IDE/ATAPI */
#define MACHINE_IDE_DUALTQ        (MACHINE_IDE_TER | MACHINE_IDE_QUA)
#define MACHINE_IDE_QUAD          (MACHINE_IDE_DUAL | MACHINE_IDE_DUALTQ) /* sys has int quad IDE/ATAPI - mark as dual + both ter and and qua IDE/ATAPI */
#define MACHINE_HDC               (MACHINE_MFM | MACHINE_XTA | \
                                   MACHINE_ESDI | MACHINE_IDE_QUAD | \
                                   MACHINE_SCSI | MACHINE_USB)
/* Special combined flags. */
#define MACHINE_PIIX              (MACHINE_IDE_DUAL)
#define MACHINE_PIIX3             (MACHINE_PIIX | MACHINE_USB)
#define MACHINE_PIIX4             (MACHINE_PIIX3 | MACHINE_ACPI)

#define MACHINE_DMA_0             0x00000001
#define MACHINE_DMA_1             0x00000002
#define MACHINE_DMA_2             0x00000004
#define MACHINE_DMA_3             0x00000008
#define MACHINE_DMA_DISABLED      0x00000010
#define MACHINE_DMA_5             0x00000020
#define MACHINE_DMA_6             0x00000040
#define MACHINE_DMA_7             0x00000080
#define MACHINE_DMA_JUMPERS_MASK  (MACHINE_DMA_0 | MACHINE_DMA_1 | MACHINE_DMA_2 | MACHINE_DMA_3 | \
                                   MACHINE_DMA_DISABLED | MACHINE_DMA_5 | MACHINE_DMA_6 | MACHINE_DMA_7)
#define MACHINE_DMA_USE_MBDMA     0x00000100
#define MACHINE_DMA_USE_CONFIG    0x00000200
#define MACHINE_DMA_EXT_CONFIG    (MACHINE_DMA_USE_MBDMA | MACHINE_DMA_USE_CONFIG)

#define DMA_DISABLED                       4
#define DMA_NONE                          -1
#define DMA_ANY                           -1

#define IS_ARCH(m, a) ((machines[m].bus_flags & (a)) ? 1 : 0)
#define IS_AT(m)      (((machines[m].bus_flags & (MACHINE_BUS_ISA16 | MACHINE_BUS_EISA | MACHINE_BUS_VLB | MACHINE_BUS_MCA | MACHINE_BUS_PCI | MACHINE_BUS_PCMCIA | MACHINE_BUS_AGP | MACHINE_BUS_AC97)) && !(machines[m].bus_flags & MACHINE_PC98)) ? 1 : 0)

#define CPU_BLOCK(...) \
    (const uint8_t[])  \
    {                  \
        __VA_ARGS__, 0 \
    }
#define MACHINE_MULTIPLIER_FIXED -1

#define CPU_BLOCK_NONE           0

/* Make sure it's always an invalid value to avoid misdetections. */
#define MACHINE_AVAILABLE 0xffffffffffffffffULL

enum {
    MACHINE_TYPE_NONE       = 0,
    MACHINE_TYPE_8088,
    MACHINE_TYPE_8086,
    MACHINE_TYPE_286,
    MACHINE_TYPE_386SX,
    MACHINE_TYPE_M6117,
    MACHINE_TYPE_486SLC,
    MACHINE_TYPE_386DX,
    MACHINE_TYPE_386DX_486,
    MACHINE_TYPE_486,
    MACHINE_TYPE_486_S2,
    MACHINE_TYPE_486_S3,
    MACHINE_TYPE_486_S3_PCI,
    MACHINE_TYPE_486_MISC,
    MACHINE_TYPE_SOCKET4,
    MACHINE_TYPE_SOCKET4_5,
    MACHINE_TYPE_SOCKET5,
    MACHINE_TYPE_SOCKET7_3V,
    MACHINE_TYPE_SOCKET7,
    MACHINE_TYPE_SOCKETS7,
    MACHINE_TYPE_SOCKET8,
    MACHINE_TYPE_SLOT1,
    MACHINE_TYPE_SLOT1_2,
    MACHINE_TYPE_SLOT1_370,
    MACHINE_TYPE_SLOT2,
    MACHINE_TYPE_SOCKET370,
    MACHINE_TYPE_MISC,
    MACHINE_TYPE_MAX
};

enum {
    MACHINE_CHIPSET_NONE = 0,
    MACHINE_CHIPSET_DISCRETE,
    MACHINE_CHIPSET_PROPRIETARY,
    MACHINE_CHIPSET_GC100A,
    MACHINE_CHIPSET_GC103,
    MACHINE_CHIPSET_HT18,
    MACHINE_CHIPSET_ACC_2036,
    MACHINE_CHIPSET_ACC_2168,
    MACHINE_CHIPSET_ALI_M1217,
    MACHINE_CHIPSET_ALI_M6117,
    MACHINE_CHIPSET_ALI_M1409,
    MACHINE_CHIPSET_ALI_M1429,
    MACHINE_CHIPSET_ALI_M1429G,
    MACHINE_CHIPSET_ALI_M1489,
    MACHINE_CHIPSET_ALI_ALADDIN_IV_PLUS,
    MACHINE_CHIPSET_ALI_ALADDIN_V,
    MACHINE_CHIPSET_ALI_ALADDIN_PRO_II,
    MACHINE_CHIPSET_SCAT,
    MACHINE_CHIPSET_SCAT_SX,
    MACHINE_CHIPSET_NEAT,
    MACHINE_CHIPSET_NEAT_SX,
    MACHINE_CHIPSET_CT_AT,
    MACHINE_CHIPSET_CT_386,
    MACHINE_CHIPSET_CT_CS4031,
    MACHINE_CHIPSET_CONTAQ_82C596,
    MACHINE_CHIPSET_CONTAQ_82C597,
    MACHINE_CHIPSET_IMS_8848,
    MACHINE_CHIPSET_INTEL_82335,
    MACHINE_CHIPSET_INTEL_420TX,
    MACHINE_CHIPSET_INTEL_420ZX,
    MACHINE_CHIPSET_INTEL_420EX,
    MACHINE_CHIPSET_INTEL_430LX,
    MACHINE_CHIPSET_INTEL_430NX,
    MACHINE_CHIPSET_INTEL_430FX,
    MACHINE_CHIPSET_INTEL_430HX,
    MACHINE_CHIPSET_INTEL_430VX,
    MACHINE_CHIPSET_INTEL_430TX,
    MACHINE_CHIPSET_INTEL_450KX,
    MACHINE_CHIPSET_INTEL_440FX,
    MACHINE_CHIPSET_INTEL_440EX,
    MACHINE_CHIPSET_INTEL_440LX,
    MACHINE_CHIPSET_INTEL_440BX,
    MACHINE_CHIPSET_INTEL_440ZX,
    MACHINE_CHIPSET_INTEL_440GX,
    MACHINE_CHIPSET_OPTI_283,
    MACHINE_CHIPSET_OPTI_291,
    MACHINE_CHIPSET_OPTI_381,
    MACHINE_CHIPSET_OPTI_391,
    MACHINE_CHIPSET_OPTI_481,
    MACHINE_CHIPSET_OPTI_493,
    MACHINE_CHIPSET_OPTI_495SLC,
    MACHINE_CHIPSET_OPTI_495SX,
    MACHINE_CHIPSET_OPTI_496,
    MACHINE_CHIPSET_OPTI_498,
    MACHINE_CHIPSET_OPTI_499,
    MACHINE_CHIPSET_OPTI_895_802G,
    MACHINE_CHIPSET_OPTI_547_597,
    MACHINE_CHIPSET_SARC_RC2016A,
    MACHINE_CHIPSET_SIS_310,
    MACHINE_CHIPSET_SIS_401,
    MACHINE_CHIPSET_SIS_460,
    MACHINE_CHIPSET_SIS_461,
    MACHINE_CHIPSET_SIS_471,
    MACHINE_CHIPSET_SIS_496,
    MACHINE_CHIPSET_SIS_501,
    MACHINE_CHIPSET_SIS_5501,
    MACHINE_CHIPSET_SIS_5511,
    MACHINE_CHIPSET_SIS_5571,
    MACHINE_CHIPSET_SIS_5581,
    MACHINE_CHIPSET_SIS_5591,
    MACHINE_CHIPSET_SIS_5600,
    MACHINE_CHIPSET_SMSC_VICTORYBX_66,
    MACHINE_CHIPSET_STPC_CLIENT,
    MACHINE_CHIPSET_STPC_CONSUMER_II,
    MACHINE_CHIPSET_STPC_ELITE,
    MACHINE_CHIPSET_STPC_ATLAS,
    MACHINE_CHIPSET_SYMPHONY_SL82C460,
    MACHINE_CHIPSET_UMC_UM82C480,
    MACHINE_CHIPSET_UMC_UM82C491,
    MACHINE_CHIPSET_UMC_UM8881,
    MACHINE_CHIPSET_UMC_UM8890BF,
    MACHINE_CHIPSET_VIA_VT82C495,
    MACHINE_CHIPSET_VIA_VT82C496G,
    MACHINE_CHIPSET_VIA_APOLLO_VPX,
    MACHINE_CHIPSET_VIA_APOLLO_VP3,
    MACHINE_CHIPSET_VIA_APOLLO_MVP3,
    MACHINE_CHIPSET_VIA_APOLLO_PRO,
    MACHINE_CHIPSET_VIA_APOLLO_PRO_133,
    MACHINE_CHIPSET_VIA_APOLLO_PRO_133A,
    MACHINE_CHIPSET_VLSI_SCAMP,
    MACHINE_CHIPSET_VLSI_VL82C480,
    MACHINE_CHIPSET_VLSI_VL82C481,
    MACHINE_CHIPSET_VLSI_VL82C486,
    MACHINE_CHIPSET_VLSI_SUPERCORE,
    MACHINE_CHIPSET_VLSI_WILDCAT,
    MACHINE_CHIPSET_WD76C10,
    MACHINE_CHIPSET_ZYMOS_POACH,
    MACHINE_CHIPSET_MAX
};

typedef struct _machine_filter_ {
    const char *name;
    const char  id;
} machine_filter_t;

typedef struct _machine_cpu_ {
    uint32_t       package;
    const uint8_t *block;
    uint32_t       min_bus;
    uint32_t       max_bus;
    uint16_t       min_voltage;
    uint16_t       max_voltage;
    float          min_multi;
    float          max_multi;
} machine_cpu_t;

typedef struct _machine_memory_ {
    uint32_t min;
    uint32_t max;
    int      step;
} machine_memory_t;

typedef struct _machine_ {
    const char            *name;
    const char            *internal_name;
    uint32_t               type;
    uintptr_t              chipset;
    int                  (*init)(const struct _machine_ *);
    uint8_t              (*p1_handler)(void);
    uint32_t             (*gpio_handler)(uint8_t write, uint32_t val);
    uintptr_t              available_flag;
    uint32_t             (*gpio_acpi_handler)(uint8_t write, uint32_t val);
    const machine_cpu_t    cpu;
    uintptr_t              bus_flags;
    uintptr_t              flags;
    const machine_memory_t ram;
    int                    ram_granularity;
    int                    nvrmask;
    int                    jumpered_ecp_dma;
    int                    default_jumpered_ecp_dma;
#ifdef EMU_DEVICE_H
    const device_t        *kbc_device;
#else
    void                  *kbc_device;
#endif /* EMU_DEVICE_H */
    uintptr_t              kbc_params;
    /* Bits 23-16: XOR mask, bits 15-8: OR mask, bits 7-0: AND mask. */
    uint32_t               kbc_p1;
    uint32_t               gpio;
    uint32_t               gpio_acpi;
#ifdef EMU_DEVICE_H
    const device_t        *device;
    const device_t        *kbd_device;
    const device_t        *fdc_device;
    const device_t        *sio_device;
    const device_t        *vid_device;
    const device_t        *snd_device;
    const device_t        *net_device;
#else
    void                  *device;
    void                  *kbd_device;
    void                  *fdc_device;
    void                  *sio_device;
    void                  *vid_device;
    void                  *snd_device;
    void                  *net_device;
#endif
} machine_t;

/* Global variables. */
extern const machine_filter_t machine_types[];
extern const machine_filter_t machine_chipsets[];
extern const machine_t        machines[];
extern int                    bios_only;
extern int                    machine;
extern void *                 machine_snd;

/* Core functions. */
extern int             machine_count(void);
extern int             machine_available(int m);
extern const char *    machine_getname(int m);
extern const char *    machine_get_internal_name(void);
extern const char *    machine_get_nvr_name(void);
extern int             machine_get_machine_from_internal_name(const char *s);
extern void            machine_init(void);
#ifdef EMU_DEVICE_H
extern const device_t *machine_get_kbc_device(int m);
extern const device_t *machine_get_device(int m);
extern const device_t *machine_get_fdc_device(int m);
extern const device_t *machine_get_sio_device(int m);
extern const device_t *machine_get_vid_device(int m);
extern const device_t *machine_get_snd_device(int m);
extern const device_t *machine_get_net_device(int m);
#endif
extern const char *    machine_get_internal_name_ex(int m);
extern const char *    machine_get_nvr_name_ex(int m);
extern int             machine_get_nvrmask(int m);
extern int             machine_has_flags(int m, int flags);
extern void            machine_set_ps2(void);
extern void            machine_force_ps2(int is_ps2);
extern int             machine_has_flags_ex(int flags);
extern int             machine_has_bus(int m, int bus_flags);
extern int             machine_has_cartridge(int m);
extern int             machine_has_jumpered_ecp_dma(int m, int dma);
extern int             machine_get_default_jumpered_ecp_dma(int m);
extern int             machine_map_jumpered_ecp_dma(int dma);
extern const char *    machine_get_jumpered_ecp_dma_name(int dma);
extern int             machine_get_min_ram(int m);
extern int             machine_get_max_ram(int m);
extern int             machine_get_ram_granularity(int m);
extern int             machine_get_type(int m);
extern int             machine_get_chipset(int m);
extern void            machine_close(void);
extern int             machine_has_mouse(void);

extern uint8_t         machine_compaq_p1_handler(void);
extern uint8_t         machine_generic_p1_handler(void);
extern uint8_t         machine_ncr_p1_handler(void);
extern uint8_t         machine_ps1_p1_handler(void);
extern uint8_t         machine_ps2_isa_p1_handler(void);
extern uint8_t         machine_t3100e_p1_handler(void);

extern uint8_t         machine_get_p1_default(void);
extern void            machine_set_p1_default(uint8_t val);
extern void            machine_set_p1(uint8_t val);
extern void            machine_and_p1(uint8_t val);
extern void            machine_init_p1(void);
extern uint8_t         machine_handle_p1(uint8_t write, uint8_t val);
extern uint8_t         machine_get_p1(uint8_t kbc_p1);
extern uint32_t        machine_get_gpio_default(void);
extern uint32_t        machine_get_gpio(void);
extern void            machine_set_gpio_default(uint32_t val);
extern void            machine_set_gpio(uint32_t val);
extern void            machine_and_gpio(uint32_t val);
extern void            machine_init_gpio(void);
extern uint32_t        machine_handle_gpio(uint8_t write, uint32_t val);
extern uint32_t        machine_get_gpio_acpi_default(void);
extern uint32_t        machine_get_gpio_acpi(void);
extern void            machine_set_gpio_acpi_default(uint32_t val);
extern void            machine_set_gpio_acpi(uint32_t val);
extern void            machine_and_gpio_acpi(uint32_t val);
extern void            machine_init_gpio_acpi(void);
extern uint32_t        machine_handle_gpio_acpi(uint8_t write, uint32_t val);

/* Initialization functions for boards and systems. */
extern void            machine_common_init(const machine_t *);

/* m_amstrad.c */
#ifdef EMU_DEVICE_H
extern const device_t  vid_1512_device;
#endif
extern int             machine_pc1512_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  vid_1640_device;
#endif
extern int             machine_pc1640_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  vid_200_device;
#endif
extern int             machine_pc200_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  vid_ppc512_device;
#endif
extern int             machine_ppc512_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  vid_pc2086_device;
#endif
extern int             machine_pc2086_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  vid_pc3086_device;
#endif
extern int             machine_pc3086_init(const machine_t *);

/* m_at_286.c */
/* ISA */
#ifdef EMU_DEVICE_H
extern const device_t  ibmat_device;
#endif
extern int             machine_at_ibmat_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  ibmxt286_device;
#endif
extern int             machine_at_ibmxt286_init(const machine_t *);
extern int             machine_at_cmdpc_init(const machine_t *);
extern int             machine_at_portableii_init(const machine_t *);
extern int             machine_at_portableiii_init(const machine_t *);
extern int             machine_at_grid1520_init(const machine_t *);
extern int             machine_at_pc900_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  pc900_device;
#endif
extern int             machine_at_mr286_init(const machine_t *);
extern int             machine_at_pc8_init(const machine_t *);
extern int             machine_at_m290_init(const machine_t *);
extern int             machine_at_pxat_init(const machine_t *);
extern int             machine_at_quadtat_init(const machine_t *);
extern int             machine_at_pb286_init(const machine_t *);
extern int             machine_at_mbc17_init(const machine_t *);
extern int             machine_at_ax286_init(const machine_t *);
/* Siemens PCD-2L. N82330 discrete machine. It segfaults in some places */
extern int             machine_at_siemens_init(const machine_t *);
extern int             machine_at_tbunk286_init(const machine_t *);

/* C&T PC/AT */
extern int             machine_at_dells200_init(const machine_t *);
extern int             machine_at_super286c_init(const machine_t *);
extern int             machine_at_at122_init(const machine_t *);
extern int             machine_at_tuliptc7_init(const machine_t *);
/* Wells American A*Star with custom award BIOS. */
extern int             machine_at_wellamerastar_init(const machine_t *);

/* GC103 */
extern int             machine_at_quadt286_init(const machine_t *);
extern void            machine_at_headland_common_init(const machine_t *model, int type);
extern int             machine_at_tg286m_init(const machine_t *);

/* NEAT */
extern int             machine_at_px286_init(const machine_t *);
extern int             machine_at_ataripc4_init(const machine_t *);
extern int             machine_at_neat_ami_init(const machine_t *);
extern int             machine_at_3302_init(const machine_t *);

/* SCAMP */
extern int             machine_at_pc7286_init(const machine_t *);

/* SCAT */
extern int             machine_at_pc5286_init(const machine_t *);
extern int             machine_at_gw286ct_init(const machine_t *);
extern int             machine_at_gdc212m_init(const machine_t *);
extern int             machine_at_award286_init(const machine_t *);
extern int             machine_at_super286tr_init(const machine_t *);
extern int             machine_at_drsm35286_init(const machine_t *);
extern int             machine_at_deskmaster286_init(const machine_t *);
extern int             machine_at_spc4200p_init(const machine_t *);
extern int             machine_at_spc4216p_init(const machine_t *);
extern int             machine_at_spc4620p_init(const machine_t *);
extern int             machine_at_senor_scat286_init(const machine_t *);

/* m_at_386sx.c */
/* ISA */
extern int             machine_at_pc916sx_init(const machine_t *);
extern int             machine_at_quadt386sx_init(const machine_t *);

/* ACC 2036 */
#ifdef EMU_DEVICE_H
extern const device_t  pbl300sx_device;
#endif
extern int             machine_at_pbl300sx_init(const machine_t *);

/* ALi M1217 */
extern int             machine_at_sbc350a_init(const machine_t *);
extern int             machine_at_arb1374_init(const machine_t *);
extern int             machine_at_flytech386_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  c325ax_device;
#endif
extern int             machine_at_325ax_init(const machine_t *);

/* ALi M1409 */
extern int             machine_at_acer100t_init(const machine_t *);

/* HT18 */
extern int             machine_at_ama932j_init(const machine_t *);
extern int             machine_at_tandy1000rsx_init(const machine_t *);

/* Intel 82335 */
extern int             machine_at_adi386sx_init(const machine_t *);
extern int             machine_at_shuttle386sx_init(const machine_t *);

/* NEAT */
extern int             machine_at_cmdsl386sx16_init(const machine_t *);
extern int             machine_at_neat_init(const machine_t *);

/* NEATsx */
extern int             machine_at_if386sx_init(const machine_t *);

/* OPTi 283 */
extern int             machine_at_svc386sxp1_init(const machine_t *);

/* OPTi 291 */
extern int             machine_at_awardsx_init(const machine_t *);

/* SCAMP */
extern int             machine_at_cmdsl386sx25_init(const machine_t *);
extern int             machine_at_dataexpert386sx_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  dells333sl_device;
#endif
extern int             machine_at_dells333sl_init(const machine_t *);
extern int             machine_at_spc6033p_init(const machine_t *);

/* SCATsx */
extern int             machine_at_kmxc02_init(const machine_t *);

/* WD76C10 */
extern int             machine_at_wd76c10_init(const machine_t *);

/* m_at_m6117.c */
/* ALi M6117D */
extern int             machine_at_pja511m_init(const machine_t *);
extern int             machine_at_prox1332_init(const machine_t *);

/* m_at_486slc.c */
/* OPTi 283 */
extern int             machine_at_rycleopardlx_init(const machine_t *);

/* m_at_386dx.c */
/* ISA */
#ifdef EMU_DEVICE_H
extern const device_t  deskpro386_device;
#endif
extern int             machine_at_deskpro386_init(const machine_t *);
extern int             machine_at_portableiii386_init(const machine_t *);
extern int             machine_at_micronics386_init(const machine_t *);
extern int             machine_at_micronics386px_init(const machine_t *);

/* ACC 2168 */
extern int             machine_at_acc386_init(const machine_t *);

/* C&T 386/AT */
extern int             machine_at_ecs386_init(const machine_t *);
extern int             machine_at_spc6000a_init(const machine_t *);
extern int             machine_at_tandy4000_init(const machine_t *);

/* ALi M1429 */
extern int             machine_at_ecs386v_init(const machine_t *);

/* OPTi 391 */
#ifdef EMU_DEVICE_H
extern const device_t  dataexpert386wb_device;
#endif
extern int             machine_at_dataexpert386wb_init(const machine_t *);

/* OPTi 495SLC */
extern int             machine_at_opti495_init(const machine_t *);

/* SiS 310 */
extern int             machine_at_asus3863364k_init(const machine_t *);
extern int             machine_at_asus386_init(const machine_t *);

/* m_at_386dx_486.c */
/* ALi M1429G */
extern int             machine_at_exp4349_init(const machine_t *);

/* OPTi 495SX */
extern int             machine_at_c747_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  opti495_ami_device;
#endif
extern int             machine_at_opti495_ami_init(const machine_t *);

/* m_at_common.c */
extern void            machine_at_common_init_ex(const machine_t *, int type);
extern void            machine_at_common_init(const machine_t *);
extern void            machine_at_init(const machine_t *);
extern void            machine_at_ps2_init(const machine_t *);
extern void            machine_at_common_ide_init(const machine_t *);
extern void            machine_at_ibm_common_ide_init(const machine_t *);
extern void            machine_at_ide_init(const machine_t *);
extern void            machine_at_ps2_ide_init(const machine_t *);

/* m_at_socket1.c */
/* CS4031 */
extern int             machine_at_cs4031_init(const machine_t *);

/* OPTi 381 */
extern int             machine_at_ga486l_init(const machine_t *);

/* OPTi 493 */
extern int             machine_at_svc486wb_init(const machine_t *);

/* OPTi 498 */
extern int             machine_at_mvi486_init(const machine_t *);

/* SiS 401 */
extern int             machine_at_isa486_init(const machine_t *);
extern int             machine_at_sis401_init(const machine_t *);

/* SiS 460 */
extern int             machine_at_av4_init(const machine_t *);

/* SiS 471 */
extern int             machine_at_advantage40xxd_init(const machine_t *);

/* Symphony SL42C460 */
extern int             machine_at_dtk461_init(const machine_t *);

/* VIA VT82C495 */
extern int             machine_at_486vchd_init(const machine_t *);

/* VLSI 82C480 */
extern int             machine_at_vect486vl_init(const machine_t *);

/* VLSI 82C481 */
extern int             machine_at_d824_init(const machine_t *);

/* VLSI 82C486 */
extern int             machine_at_pcs44c_init(const machine_t *);
extern int             machine_at_sensation1_init(const machine_t *);
extern int             machine_at_tuliptc38_init(const machine_t *);

/* ZyMOS Poach */
extern int             machine_at_isa486c_init(const machine_t *);
extern int             machine_at_genoa486_init(const machine_t *);

/* m_at_socket2.c */
/* ACC 2168 */
extern int             machine_at_pb410a_init(const machine_t *);

/* ALi M1429G */
extern int             machine_at_acera1g_init(const machine_t *);
extern int             machine_at_winbios1429_init(const machine_t *);
extern int             machine_at_ali1429_init(const machine_t *);

/* i420TX */
extern int             machine_at_pci400ca_init(const machine_t *);

/* IMS 8848 */
extern int             machine_at_g486ip_init(const machine_t *);

/* OPTi 499 */
extern int             machine_at_cobalt_init(const machine_t *);
extern int             machine_at_cougar_init(const machine_t *);

/* SiS 461 */
extern int             machine_at_decpclpv_init(const machine_t *);
extern int             machine_at_dell466np_init(const machine_t *);
extern int             machine_at_valuepoint433_init(const machine_t *);

/* VLSI 82C480 */
extern int             machine_at_monsoon_init(const machine_t *);
extern int             machine_at_martin_init(const machine_t *);

/* VLSI 82C486 */
extern int             machine_at_sensation2_init(const machine_t *);

/* m_at_socket3.c */
/* ALi M1429G */
extern int             machine_at_atc1762_init(const machine_t *);
extern int             machine_at_ecsal486_init(const machine_t *);
extern int             machine_at_ap4100aa_init(const machine_t *);

/* Contaq 82C596A */
extern int             machine_at_4gpv5_init(const machine_t *);

/* Contaq 82C597 */
extern int             machine_at_greenb_init(const machine_t *);

/* OPTi 499 */
extern int             machine_at_xenon_init(const machine_t *);

/* OPTi 895 */
#ifdef EMU_DEVICE_H
extern const device_t  j403tg_device;
#endif
extern int             machine_at_403tg_init(const machine_t *);
extern int             machine_at_403tg_d_init(const machine_t *);
extern int             machine_at_403tg_d_mr_init(const machine_t *);

/* SiS 461 */
extern int             machine_at_acerv10_init(const machine_t *);

/* SiS 471 */
extern int             machine_at_win471_init(const machine_t *);
extern int             machine_at_win471t_init(const machine_t *);
extern int             machine_at_vi15g_init(const machine_t *);
extern int             machine_at_vli486sv2g_init(const machine_t *);
extern int             machine_at_dvent4xx_init(const machine_t *);
extern int             machine_at_dtk486_init(const machine_t *);
extern int             machine_at_ami471_init(const machine_t *);
extern int             machine_at_px471_init(const machine_t *);
extern int             machine_at_tg486g_init(const machine_t *);

/* m_at_socket3_pci.c */
/* ALi M1429G */
extern int             machine_at_ms4134_init(const machine_t *);
extern int             machine_at_tg486gp_init(const machine_t *);

/* ALi M1489 */
extern int             machine_at_sbc490_init(const machine_t *);
extern int             machine_at_abpb4_init(const machine_t *);
extern int             machine_at_arb1476_init(const machine_t *);
extern int             machine_at_win486pci_init(const machine_t *);
extern int             machine_at_tf486_init(const machine_t *);
extern int             machine_at_ms4145_init(const machine_t *);

/* OPTi 802G */
#ifdef EMU_DEVICE_H
extern const device_t  pc330_6573_device;
#endif
extern int             machine_at_pc330_6573_init(const machine_t *);

/* OPTi 895 */
#ifdef EMU_DEVICE_H
extern const device_t  pb450_device;
#endif
extern int             machine_at_pb450_init(const machine_t *);

/* i420EX */
extern int             machine_at_486pi_init(const machine_t *);
extern int             machine_at_bat4ip3e_init(const machine_t *);
extern int             machine_at_486ap4_init(const machine_t *);
extern int             machine_at_ninja_init(const machine_t *);
extern int             machine_at_sb486p_init(const machine_t *);

/* i420TX */
extern int             machine_at_amis76_init(const machine_t *);
extern int             machine_at_486sp3_init(const machine_t *);
extern int             machine_at_alfredo_init(const machine_t *);

/* i420ZX */
extern int             machine_at_486sp3g_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  sb486pv_device;
#endif
extern int             machine_at_sb486pv_init(const machine_t *);

/* IMS 8848 */
extern int             machine_at_pci400cb_init(const machine_t *);

/* SiS 496 */
extern int             machine_at_acerp3_init(const machine_t *);
extern int             machine_at_486sp3c_init(const machine_t *);
extern int             machine_at_ls486e_init(const machine_t *);
extern int             machine_at_m4li_init(const machine_t *);
extern int             machine_at_ms4144_init(const machine_t *);
extern int             machine_at_r418_init(const machine_t *);
extern int             machine_at_4saw2_init(const machine_t *);
extern int             machine_at_4dps_init(const machine_t *);

/* UMC 8881 */
extern int             machine_at_atc1415_init(const machine_t *);
extern int             machine_at_84xxuuda_init(const machine_t *);
extern int             machine_at_pl4600c_init(const machine_t *);
extern int             machine_at_ecs486_init(const machine_t *);
extern int             machine_at_actionpc2600_init(const machine_t *);
extern int             machine_at_actiontower8400_init(const machine_t *);
extern int             machine_at_m919_init(const machine_t *);
extern int             machine_at_spc7700plw_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  hot433a_device;
#endif
extern int             machine_at_hot433a_init(const machine_t *);

/* VIA VT82C496G */
extern int             machine_at_g486vpa_init(const machine_t *);
extern int             machine_at_486vipio2_init(const machine_t *);

/* m_at_486_misc.c */
/* STPC Client */
extern int             machine_at_itoxstar_init(const machine_t *);

/* STPC Consumer-II */
extern int             machine_at_arb1423c_init(const machine_t *);
extern int             machine_at_arb1479_init(const machine_t *);
extern int             machine_at_iach488_init(const machine_t *);

/* STPC Elite */
extern int             machine_at_pcm9340_init(const machine_t *);

/* STPC Atlas */
extern int             machine_at_pcm5330_init(const machine_t *);

/* m_at_socket4.c */
/* i430LX */
#ifdef EMU_DEVICE_H
extern const device_t  v12p_device;
#endif
extern int             machine_at_v12p_init(const machine_t *);
extern int             machine_at_excaliburpci_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  p5mp3_device;
#endif
extern int             machine_at_p5mp3_init(const machine_t *);
extern int             machine_at_opti560l_init(const machine_t *);
extern void            machine_at_award_common_init(const machine_t *);
extern int             machine_at_586is_init(const machine_t *);
extern int             machine_at_valuepointp60_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  batman_device;
#endif
extern int             machine_at_batman_init(const machine_t *);
extern void            machine_at_premiere_common_init(const machine_t *, int);
extern int             machine_at_revenge_init(const machine_t *);
extern int             machine_at_m5pi_init(const machine_t *);
extern int             machine_at_pb520r_init(const machine_t *);

/* OPTi 597 */
extern int             machine_at_excalibur_init(const machine_t *);
extern int             machine_at_globalyst330_p5_init(const machine_t *);
extern int             machine_at_p5vl_init(const machine_t *);

/* SiS 501 */
extern int             machine_at_excaliburpci2_init(const machine_t *);
extern void            machine_at_sp4_common_init(const machine_t *model);
extern int             machine_at_p5sp4_init(const machine_t *);
extern int             machine_at_ecs50x_init(const machine_t *);

/* m_at_socket4_5.c */
/* OPTi 597 */
extern int             machine_at_pci56001_init(const machine_t *);

/* VLSI SuperCore */
extern int             machine_at_celebris5xx_init(const machine_t *);

/* m_at_socket5.c */
/* i430NX */
extern int             machine_at_p54np4_init(const machine_t *);
extern int             machine_at_586ip_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  plato_device;
#endif
extern int             machine_at_plato_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  d842_device;
#endif
extern int             machine_at_d842_init(const machine_t *);
extern int             machine_at_tek932_init(const machine_t *);

/* i430FX */
extern int             machine_at_acerv30_init(const machine_t *);
extern int             machine_at_apollo_init(const machine_t *);
extern int             machine_at_optiplexgxl_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  pt2000_device;
#endif
extern int             machine_at_pt2000_init(const machine_t *);
extern int             machine_at_morrison32_init(const machine_t *);
extern int             machine_at_pc330_65x6_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  zappa_device;
#endif
extern int             machine_at_zappa_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  powermatev_device;
#endif
extern int             machine_at_powermatev_init(const machine_t *);
extern int             machine_at_hawk_init(const machine_t *);

/* OPTi 597 */
extern int             machine_at_ncselp90_init(const machine_t *);
extern int             machine_at_hot543_init(const machine_t *);
extern int             machine_at_pat54pv_init(const machine_t *);

/* SiS 501 */
extern int             machine_at_p54sp4_init(const machine_t *);
extern int             machine_at_sq588_init(const machine_t *);
extern int             machine_at_p54sps_init(const machine_t *);
extern int             machine_at_ms5109_init(const machine_t *);

/* SiS 5501 */
extern int             machine_at_torino_init(const machine_t *);

/* UMC 889x */
extern int             machine_at_hot539_init(const machine_t *);

/* VLSI SuperCore */
extern int             machine_at_bravoms586_init(const machine_t *);
extern int             machine_at_g586vpmc_init(const machine_t *);
extern int             machine_at_m54si_init(const machine_t *);
extern int             machine_at_pb600_init(const machine_t *);

/* VLSI Wildcat */
extern int             machine_at_globalyst620_init(const machine_t *);

/* m_at_socket7_3v.c */
/* i430FX */
#ifdef EMU_DEVICE_H
extern const device_t  p54tp4xe_device;
#endif
extern int             machine_at_p54tp4xe_init(const machine_t *);
extern int             machine_at_exp8551_init(const machine_t *);
extern int             machine_at_holly_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t vectra52_device;
#endif
extern int             machine_at_vectra52_init(const machine_t *);
extern int             machine_at_vectra500mt_init(const machine_t *);
extern int             machine_at_vectra54_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  thor_device;
#endif
extern int             machine_at_thor_init(const machine_t *);
extern uint32_t        machine_at_monaco_gpio_handler(uint8_t write, uint32_t val);
extern int             machine_at_monaco_init(const machine_t *);
extern uint32_t        machine_at_endeavor_gpio_handler(uint8_t write, uint32_t val);
extern int             machine_at_endeavor_init(const machine_t *);
extern int             machine_at_atlantis_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  monaco_device;
#endif
extern int             machine_at_monaco_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  ms5119_device;
#endif
extern int             machine_at_ms5119_init(const machine_t *);
extern int             machine_at_pb640_init(const machine_t *);
extern int             machine_at_mb500n_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  fmb_device;
#endif
extern int             machine_at_fmb_init(const machine_t *);

/* i430HX */
extern int             machine_at_acerv35n_init(const machine_t *);
extern int             machine_at_ap53_init(const machine_t *);
extern int             machine_at_8500tuc_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  d943_device;
#endif
extern int             machine_at_d943_init(const machine_t *);

/* i430VX */
extern int             machine_at_gw2kma_init(const machine_t *);

/* SiS 5501 */
#ifdef EMU_DEVICE_H
extern const device_t  c5sbm2_device;
#endif
extern int             machine_at_5sbm2_init(const machine_t *);

/* SiS 5511 */
extern int             machine_at_amis727_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  ap5s_device;
#endif
extern int             machine_at_ap5s_init(const machine_t *);
extern int             machine_at_fm562_init(const machine_t *);
extern int             machine_at_pc140_6260_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  ms5124_device;
#endif
extern int             machine_at_ms5124_init(const machine_t *);

/* VLSI Wildcat */
extern int             machine_at_zeoswildcat_init(const machine_t *);

/* m_at_socket7.c */
/* i430HX */
extern int             machine_at_acerm3a_init(const machine_t *);
extern int             machine_at_p55t2p4_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern void            machine_at_p65up5_common_init(const machine_t *, const device_t *northbridge);
#endif
extern int             machine_at_p65up5_cp55t2d_init(const machine_t *);
extern int             machine_at_rubyusb_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  cu430hx_device;
#endif
extern int             machine_at_cu430hx_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  tc430hx_device;
#endif
extern int             machine_at_tc430hx_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  m7shi_device;
#endif
extern int             machine_at_m7shi_init(const machine_t *);
extern int             machine_at_epc2102_init(const machine_t *);
extern int             machine_at_pcv90_init(const machine_t *);
extern int             machine_at_p55t2s_init(const machine_t *);

/* i430VX */
extern int             machine_at_ap5vm_init(const machine_t *);
extern int             machine_at_p55tvp4_init(const machine_t *);
extern int             machine_at_5ivg_init(const machine_t *);
extern int             machine_at_8500tvxa_init(const machine_t *);
extern int             machine_at_presario2240_init(const machine_t *);
extern int             machine_at_presario4500_init(const machine_t *);
extern int             machine_at_dellhannibalp_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  p5vxb_device;
#endif
extern int             machine_at_p5vxb_init(const machine_t *);
extern int             machine_at_p55va_init(const machine_t *);
extern int             machine_at_gw2kte_init(const machine_t *);
extern int             machine_at_brio80xx_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  lgibmx52_device;
#endif
extern int             machine_at_lgibmx52_init(const machine_t *);
extern int             machine_at_pb680_init(const machine_t *);
extern int             machine_at_pb810_init(const machine_t *);
extern int             machine_at_mb520n_init(const machine_t *);
extern int             machine_at_i430vx_init(const machine_t *);

/* i430TX */
extern int             machine_at_nupro592_init(const machine_t *);
extern int             machine_at_tx97_init(const machine_t *);
extern int             machine_at_tx97xv_init(const machine_t *);
extern void            machine_at_optiplex_21152_init(void);
extern int             machine_at_optiplexgn_init(const machine_t *);
extern int             machine_at_tomahawk_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  an430tx_device;
#endif
extern int             machine_at_an430tx_init(const machine_t *);
extern int             machine_at_ym430tx_init(const machine_t *);
extern int             machine_at_thunderbolt_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  ms5156_device;
#endif
extern int             machine_at_ms5156_init(const machine_t *);
extern int             machine_at_ma23c_init(const machine_t *);
extern int             machine_at_mb540n_init(const machine_t *);
extern int             machine_at_56a5_init(const machine_t *);
extern int             machine_at_p5mms98_init(const machine_t *);
extern int             machine_at_richmond_init(const machine_t *);

/* VIA VPX */
extern int             machine_at_ficva502_init(const machine_t *);

/* VIA VP3 */
extern int             machine_at_ficpa2012_init(const machine_t *);
extern int             machine_at_via809ds_init(const machine_t *);

/* SiS 5571 */
extern int             machine_at_cb52xsi_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  ms5146_device;
#endif
extern int             machine_at_ms5146_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  r534f_device;
#endif
extern int             machine_at_r534f_init(const machine_t *);

/* SiS 5581 */
extern int             machine_at_sp97xv_init(const machine_t *);
extern int             machine_at_sq578_init(const machine_t *);

/* SiS 5591 */
extern int             machine_at_ms5172_init(const machine_t *);

/* ALi ALADDiN IV+ */
#ifdef EMU_DEVICE_H
extern const device_t  m5ata_device;
#endif
extern int             machine_at_m5ata_init(const machine_t *);
extern int             machine_at_ms5164_init(const machine_t *);
extern int             machine_at_m560_init(const machine_t *);

/* m_at_sockets7.c */
/* ALi ALADDiN V */
extern int             machine_at_p5a_init(const machine_t *);
extern int             machine_at_m579_init(const machine_t *);
extern int             machine_at_gwlucas_init(const machine_t *);
extern int             machine_at_5aa_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  g5x_device;
#endif
extern int             machine_at_g5x_init(const machine_t *);

/* VIA MVP3 */
extern int             machine_at_ax59pro_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  delhi3_device;
#endif
extern int             machine_at_delhi3_init(const machine_t *);
extern int             machine_at_mvp3_init(const machine_t *);
extern int             machine_at_ficva503a_init(const machine_t *);
extern int             machine_at_5emapro_init(const machine_t *);
extern int             machine_at_k6bv3p_a_init(const machine_t *);

/* SiS 5591 */
extern int             machine_at_5sg100_init(const machine_t *);

/* m_at_socket8.c */
/* i450KX */
extern int             machine_at_ap61_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  ficpo6000_device;
#endif
extern int             machine_at_p6rp4_init(const machine_t *);
extern int             machine_at_ficpo6000_init(const machine_t *);

/* i440FX */
extern int             machine_at_acerv60n_init(const machine_t *);
extern int             machine_at_p65up5_cp6nd_init(const machine_t *);
extern int             machine_at_8600ttc_init(const machine_t *);
extern int             machine_at_686nx_init(const machine_t *);
extern uint32_t        machine_ap440fx_vs440fx_gpio_handler(uint8_t write, uint32_t val);
extern int             machine_at_ap440fx_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  vs440fx_device;
#endif
extern int             machine_at_vs440fx_init(const machine_t *);
extern int             machine_at_lgibmx61_init(const machine_t *);
extern int             machine_at_m6mi_init(const machine_t *);
extern int             machine_at_mb600n_init(const machine_t *);

/* m_at_slot1.c */
/* ALi ALADDiN-PRO II */
extern int             machine_at_m729_init(const machine_t *);

/* i440FX */
extern int             machine_at_acerv62x_init(const machine_t *);
extern int             machine_at_p65up5_cpknd_init(const machine_t *);
extern int             machine_at_kn97_init(const machine_t *);

/* i440LX */
#ifdef EMU_DEVICE_H
extern const device_t  lx6_device;
#endif
extern int             machine_at_lx6_init(const machine_t *);
extern int             machine_at_optiplexgxa_init(const machine_t *);
extern int             machine_at_spitfire_init(const machine_t *);
extern int             machine_at_ma30d_init(const machine_t *);

/* i440EX */
extern int             machine_at_brio83xx_init(const machine_t *);
extern int             machine_at_p6i440e2_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  como_device;
#endif
extern int             machine_at_como_init(const machine_t *);

/* i440BX */
extern int             machine_at_bf6_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  bx6_device;
#endif
extern int             machine_at_bx6_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  ax6bc_device;
#endif
extern int             machine_at_ax6bc_init(const machine_t *);
extern int             machine_at_p2bls_init(const machine_t *);
extern int             machine_at_p3bf_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  ga686_device;
#endif
extern int             machine_at_ga686_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  ms6117_device;
#endif
extern int             machine_at_ms6117_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  ms6119_device;
#endif
extern int             machine_at_ms6119_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  ms6147_device;
#endif
extern int             machine_at_ms6147_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  p6sba_device;
#endif
extern int             machine_at_p6sba_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  s1846_device;
#endif
extern int             machine_at_s1846_init(const machine_t *);

/* i440ZX */
#ifdef EMU_DEVICE_H
extern const device_t  vei8_device;
#endif
extern int             machine_at_vei8_init(const machine_t *);
extern int             machine_at_ms6168_init(const machine_t *);
extern int             machine_at_borapro_init(const machine_t *);

/* SMSC VictoryBX-66 */
extern int             machine_at_atc6310bxii_init(const machine_t *);

/* VIA Apollo Pro */
extern int             machine_at_ficka6130_init(const machine_t *);

/* VIA Apollo Pro 133 */
extern int             machine_at_p3v133_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  ms6199va_device;
#endif
extern int             machine_at_ms6199va_init(const machine_t *);

/* VIA Apollo Pro 133A */
extern int             machine_at_p3v4x_init(const machine_t *);
extern int             machine_at_gt694va_init(const machine_t *);

/* SiS 5600 */
extern int             machine_at_p6f99_init(const machine_t *);
extern int             machine_at_m747_init(const machine_t *);

/* m_at_slot1_2.c */
/* i440GX */
extern int             machine_at_fw6400gx_init(const machine_t *);

/* m_at_slot1_socket370.c */
/* i440BX */
#ifdef EMU_DEVICE_H
extern const device_t  prosignias31x_device;
#endif
extern int             machine_at_prosignias31x_bx_init(const machine_t *);
extern int             machine_at_s1857_init(const machine_t *);

/* VIA Apollo Pro 133 */
extern int             machine_at_p6bat_init(const machine_t *);

/* m_at_slot2.c */
/* i440GX */
extern int             machine_at_6gxu_init(const machine_t *);
extern int             machine_at_s2dge_init(const machine_t *);

/* m_at_socket370.c */
/* i440LX */
extern int             machine_at_s370slm_init(const machine_t *);

/* i440BX */
extern int             machine_at_awo671r_init(const machine_t *);
extern int             machine_at_ambx133_init(const machine_t *);
extern int             machine_at_cubx_init(const machine_t *);

/* i440ZX */
extern int             machine_at_63a1_init(const machine_t *);

/* SiS 600 */
extern int             machine_at_7sbb_init(const machine_t *);

/* SMSC VictoryBX-66 */
extern int             machine_at_atc7020bxii_init(const machine_t *);
extern int             machine_at_m773_init(const machine_t *);

/* VIA Apollo Pro */
extern int             machine_at_apas3_init(const machine_t *);

/* VIA Apollo Pro 133 */
extern int             machine_at_p6bap_init(const machine_t *);

/* VIA Apollo Pro 133A */
extern int             machine_at_6via90ap_init(const machine_t *);
extern int             machine_at_cuv4xls_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  ms6318_device;
#endif
extern int             machine_at_ms6318_init(const machine_t *);
extern int             machine_at_cairo5_init(const machine_t *);

/* m_at_misc.c */
extern int             machine_at_vpc2007_init(const machine_t *);

/* m_at_t3100e.c */
extern int             machine_at_t3100e_init(const machine_t *);

/* m_elt.c */
extern int machine_elt_init(const machine_t *);

/* m_europc.c */
#ifdef EMU_DEVICE_H
extern const device_t  europc_device;
#endif
extern int             machine_europc_init(const machine_t *);

/* m_xt_olivetti.c */
#ifdef EMU_DEVICE_H
extern const device_t  m19_vid_device;
#endif
extern int             machine_xt_m19_init(const machine_t *);
extern int             machine_xt_m24_init(const machine_t *);
extern int             machine_xt_m240_init(const machine_t *);

/* m_pcjr.c */
#ifdef EMU_DEVICE_H
extern const device_t  pcjr_device;
#endif
extern int             machine_pcjr_init(const machine_t *);

/* m_ps1.c */
#ifdef EMU_DEVICE_H
extern const device_t  ps1_2011_device;
#endif
extern int             machine_ps1_m2011_init(const machine_t *);
extern int             machine_ps1_m2121_init(const machine_t *);

/* m_ps1_hdc.c */
#ifdef EMU_DEVICE_H
extern void            ps1_hdc_inform(void *, uint8_t *);
extern const device_t  ps1_hdc_device;
#endif

/* m_ps2_isa.c */
#ifdef EMU_DEVICE_H
extern const device_t  ps2_m30_286_device;
#endif
extern int             machine_ps2_m30_286_init(const machine_t *);

/* m_ps2_mca.c */
#ifdef EMU_DEVICE_H
extern const device_t  ps2_model_50_device;
#endif
extern int             machine_ps2_model_50_init(const machine_t *);
extern int             machine_ps2_model_60_init(const machine_t *);
extern int             machine_ps2_model_55sx_init(const machine_t *);
extern int             machine_ps2_model_65sx_init(const machine_t *);
extern int             machine_ps2_model_70_type3_init(const machine_t *);
extern int             machine_ps2_model_80_init(const machine_t *);
extern int             machine_ps2_model_80_axx_init(const machine_t *);
extern int             machine_ps2_model_70_type4_init(const machine_t *);
extern int             machine_ps55_model_50t_init(const machine_t*);
extern int             machine_ps55_model_50v_init(const machine_t*);

/* m_tandy.c */
extern int tandy1k_eeprom_read(void);
#ifdef EMU_DEVICE_H
extern const device_t  tandy_1000sx_video_device;
#endif
extern int             machine_tandy1000sx_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  tandy_1000hx_video_device;
#endif
extern int             machine_tandy1000hx_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  tandy_1000sl_video_device;
#endif
extern int             machine_tandy1000sl2_init(const machine_t *);

/* m_v86p.c */
#ifdef EMU_DEVICE_H
extern const device_t  v86p_device;
#endif
extern int             machine_v86p_init(const machine_t *);

/* m_xt.c */
/* 8088 */
#ifdef EMU_DEVICE_H
extern const device_t  ibmpc_device;
#endif
extern int             machine_ibmpc_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  ibmpc82_device;
#endif
extern int             machine_ibmpc82_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  ibmxt_device;
#endif
extern int             machine_ibmxt_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  ibmxt86_device;
#endif
extern int             machine_ibmxt86_init(const machine_t *);
extern int             machine_xt_americxt_init(const machine_t *);
extern int             machine_xt_amixt_init(const machine_t *);
extern int             machine_xt_ataripc3_init(const machine_t *);
extern int             machine_xt_bw230_init(const machine_t *);
extern int             machine_xt_mpc1600_init(const machine_t *);
extern int             machine_xt_compaq_portable_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t dtk_device;
#endif
extern int             machine_xt_dtk_init(const machine_t *);
extern int             machine_xt_pcspirit_init(const machine_t *);
extern int             machine_genxt_init(const machine_t *);
extern int             machine_xt_glabios_init(const machine_t *);
extern int             machine_xt_top88_init(const machine_t *);
extern int             machine_xt_super16t_init(const machine_t *);
extern int             machine_xt_super16te_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  jukopc_device;
#endif
extern int             machine_xt_jukopc_init(const machine_t *);
extern int             machine_xt_kaypropc_init(const machine_t *);
extern int             machine_xt_micoms_xl7turbo_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  pc500_device;
#endif
extern int             machine_xt_pc500_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  pc500plus_device;
#endif
extern int             machine_xt_pc500plus_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  pc700_device;
#endif
extern int             machine_xt_pc700_init(const machine_t *);
extern int             machine_xt_pc4i_init(const machine_t *);
extern int             machine_xt_openxt_init(const machine_t *);
extern int             machine_xt_p3105_init(const machine_t *);
extern int             machine_xt_pxxt_init(const machine_t *);
extern int             machine_xt_pravetz16_imko4_init(const machine_t *);
extern int             machine_xt_pravetz16s_cpu12p_init(const machine_t *);
extern int             machine_xt_pb8810_init(const machine_t *);
extern int             machine_xt_sansx16_init(const machine_t *);
extern int             machine_xt_pcxt_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  to16_device;
#endif
extern int             machine_xt_to16_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  vendex_device;
#endif
extern int             machine_xt_vendex_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t  laserxt_device;
#endif
extern int             machine_xt_laserxt_init(const machine_t *);
extern int             machine_xt_znic_init(const machine_t *);
extern int             machine_xt_z151_init(const machine_t *);
extern int             machine_xt_z159_init(const machine_t *);
extern int             machine_xt_z184_init(const machine_t *);

/* GC100A */
extern int             machine_xt_p3120_init(const machine_t *);

/* V20 */
extern int             machine_xt_v20xt_init(const machine_t *);
extern int             machine_xt_tuliptc8_init(const machine_t *);

/* 8086 */
extern int             machine_xt_pc5086_init(const machine_t *);
extern int             machine_xt_maz1016_init(const machine_t *);
extern int             machine_xt_iskra3104_init(const machine_t *);
extern int             machine_xt_lxt3_init(const machine_t *);
extern int             machine_xt_compaq_deskpro_init(const machine_t *);

/* m_xt_ibm5550.c */
#ifdef EMU_DEVICE_H
extern const device_t  ibm5550_vid_device;
#endif
extern int             machine_xt_ibm5550_init(const machine_t *);

/* m_xt_t1000.c */
#ifdef EMU_DEVICE_H
extern const device_t  t1000_video_device;
extern const device_t  t1200_video_device;
#endif
extern int             machine_xt_t1000_init(const machine_t *);
extern int             machine_xt_t1200_init(const machine_t *);

/* m_xt_xi8088.c */
#ifdef EMU_DEVICE_H
extern const device_t  xi8088_device;
#endif
extern int             machine_xt_xi8088_init(const machine_t *);

#endif /*EMU_MACHINE_H*/
