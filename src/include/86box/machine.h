/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Handling of the emulated machines.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2016-2020 Miran Grca.
 *		Copyright 2017-2020 Fred N. van Kempen.
 */

#ifndef EMU_MACHINE_H
# define EMU_MACHINE_H

/* Machine feature flags. */
#define MACHINE_BUS_NONE      0x00000000    /* sys has no bus */
/* Feature flags for BUS'es. */
#define MACHINE_BUS_ISA       0x00000001    /* sys has ISA bus */
#define MACHINE_BUS_CARTRIDGE 0x00000002    /* sys has two cartridge bays */
#define MACHINE_BUS_ISA16     0x00000004    /* sys has ISA16 bus - PC/AT architecture */
#define MACHINE_BUS_CBUS      0x00000008    /* sys has C-BUS bus */
#define MACHINE_BUS_PS2       0x00000010    /* system has PS/2 keyboard and mouse ports */
#define MACHINE_BUS_EISA      0x00000020    /* sys has EISA bus */
#define MACHINE_BUS_VLB       0x00000040    /* sys has VL bus */
#define MACHINE_BUS_MCA       0x00000080    /* sys has MCA bus */
#define MACHINE_BUS_PCI       0x00000100    /* sys has PCI bus */
#define MACHINE_BUS_PCMCIA    0x00000200    /* sys has PCMCIA bus */
#define MACHINE_BUS_AGP       0x00000400    /* sys has AGP bus */
#define MACHINE_BUS_AC97      0x00000800    /* sys has AC97 bus (ACR/AMR/CNR slot) */
/* Aliases. */
#define MACHINE_CARTRIDGE     (MACHINE_BUS_CARTRIDGE)    /* sys has two cartridge bays */
/* Combined flags. */
#define MACHINE_PC            (MACHINE_BUS_ISA)                     /* sys is PC/XT-compatible (ISA) */
#define MACHINE_AT            (MACHINE_BUS_ISA | MACHINE_BUS_ISA16) /* sys is AT-compatible (ISA + ISA16) */
#define MACHINE_PC98          (MACHINE_BUS_CBUS)                    /* sys is NEC PC-98x1 series */
#define MACHINE_EISA          (MACHINE_BUS_EISA | MACHINE_AT)       /* sys is AT-compatible with EISA */
#define MACHINE_VLB           (MACHINE_BUS_VLB | MACHINE_AT)        /* sys is AT-compatible with VLB */
#define MACHINE_VLB98         (MACHINE_BUS_VLB | MACHINE_PC98)      /* sys is NEC PC-98x1 series with VLB (did that even exist?) */
#define MACHINE_VLBE          (MACHINE_BUS_VLB | MACHINE_EISA)      /* sys is AT-compatible with EISA and VLB */
#define MACHINE_MCA           (MACHINE_BUS_MCA)                     /* sys is MCA */
#define MACHINE_PCI           (MACHINE_BUS_PCI | MACHINE_AT)        /* sys is AT-compatible with PCI */
#define MACHINE_PCI98         (MACHINE_BUS_PCI | MACHINE_PC98)      /* sys is NEC PC-98x1 series with PCI */
#define MACHINE_PCIE          (MACHINE_BUS_PCI | MACHINE_EISA)      /* sys is AT-compatible with PCI, and EISA */
#define MACHINE_PCIV          (MACHINE_BUS_PCI | MACHINE_VLB)       /* sys is AT-compatible with PCI and VLB */
#define MACHINE_PCIVE         (MACHINE_BUS_PCI | MACHINE_VLBE)      /* sys is AT-compatible with PCI, VLB, and EISA */
#define MACHINE_PCMCIA        (MACHINE_BUS_PCMCIA | MACHINE_AT)     /* sys is AT-compatible laptop with PCMCIA */
#define MACHINE_AGP           (MACHINE_BUS_AGP | MACHINE_PCI)       /* sys is AT-compatible with AGP  */
#define MACHINE_AGP98         (MACHINE_BUS_AGP | MACHINE_PCI98)     /* sys is NEC PC-98x1 series with AGP (did that even exist?) */

#define MACHINE_PCJR          (MACHINE_PC | MACHINE_CARTRIDGE)      /* sys is PCjr */
#define MACHINE_PS2           (MACHINE_AT | MACHINE_BUS_PS2)        /* sys is PS/2 */
#define MACHINE_PS2_MCA       (MACHINE_MCA | MACHINE_BUS_PS2)       /* sys is MCA PS/2 */
#define MACHINE_PS2_VLB       (MACHINE_VLB | MACHINE_BUS_PS2)       /* sys is VLB PS/2 */
#define MACHINE_PS2_PCI       (MACHINE_PCI | MACHINE_BUS_PS2)       /* sys is PCI PS/2 */
#define MACHINE_PS2_PCIV      (MACHINE_PCIV | MACHINE_BUS_PS2)      /* sys is VLB/PCI PS/2 */
#define MACHINE_PS2_AGP       (MACHINE_AGP | MACHINE_BUS_PS2)       /* sys is AGP PS/2 */
#define MACHINE_PS2_A97       (MACHINE_PS2_AGP | MACHINE_BUS_AC97)  /* sys is AGP/AC97 PS/2 */
#define MACHINE_PS2_NOISA     (MACHINE_PS2_AGP & ~MACHINE_AT)       /* sys is AGP PS/2 without ISA */
#define MACHINE_PS2_NOI97     (MACHINE_PS2_A97 & ~MACHINE_AT)       /* sys is AGP/AC97 PS/2 without ISA */
/* Feature flags for miscellaneous internal devices. */
#define MACHINE_FLAGS_NONE    0x00000000    /* sys has no int devices */
#define MACHINE_VIDEO         0x00000001    /* sys has int video */
#define MACHINE_VIDEO_ONLY    0x00000002    /* sys has fixed video */
#define MACHINE_MOUSE         0x00000004    /* sys has int mouse */
#define MACHINE_FDC           0x00000008    /* sys has int FDC */
#define MACHINE_LPT_PRI       0x00000010    /* sys has int pri LPT */
#define MACHINE_LPT_SEC       0x00000020    /* sys has int sec LPT */
#define MACHINE_UART_PRI      0x00000040    /* sys has int pri UART */
#define MACHINE_UART_SEC      0x00000080    /* sys has int sec UART */
#define MACHINE_UART_TER      0x00000100    /* sys has int ter UART */
#define MACHINE_UART_QUA      0x00000200    /* sys has int qua UART */
#define MACHINE_GAMEPORT      0x00000400    /* sys has int game port */
#define MACHINE_SOUND         0x00000800    /* sys has int sound */
#define MACHINE_NIC           0x00001000    /* sys has int NIC */
#define MACHINE_MODEM         0x00002000    /* sys has int modem */
/* Feature flags for advanced devices. */
#define MACHINE_APM           0x00004000    /* sys has APM */
#define MACHINE_ACPI          0x00008000    /* sys has ACPI */
#define MACHINE_HWM           0x00010000    /* sys has hw monitor */
/* Combined flags. */
#define MACHINE_VIDEO_FIXED   (MACHINE_VIDEO | MACHINE_VIDEO_ONLY)  /* sys has fixed int video */
#define MACHINE_SUPER_IO      (MACHINE_FDC | MACHINE_LPT_PRI | MACHINE_UART_PRI | MACHINE_UART_SEC)
#define MACHINE_SUPER_IO_GAME (MACHINE_SUPER_IO | MACHINE_GAMEPORT)
#define MACHINE_SUPER_IO_DUAL (MACHINE_SUPER_IO | MACHINE_LPT_SEC | MACHINE_UART_TER | MACHINE_UART_QUA)
#define MACHINE_AV            (MACHINE_VIDEO | MACHINE_SOUND)       /* sys has video and sound */
#define MACHINE_AG            (MACHINE_SOUND | MACHINE_GAMEPORT)    /* sys has sound and game port */
/* Feature flags for internal storage controllers. */
#define MACHINE_HDC           0x03FE0000    /* sys has int HDC */
#define MACHINE_MFM           0x00020000    /* sys has int MFM/RLL */
#define MACHINE_XTA           0x00040000    /* sys has int XTA */
#define MACHINE_ESDI          0x00080000    /* sys has int ESDI */
#define MACHINE_IDE_PRI       0x00100000    /* sys has int pri IDE/ATAPI */
#define MACHINE_IDE_SEC       0x00200000    /* sys has int sec IDE/ATAPI */
#define MACHINE_IDE_TER       0x00400000    /* sys has int ter IDE/ATAPI */
#define MACHINE_IDE_QUA       0x00800000    /* sys has int qua IDE/ATAPI */
#define MACHINE_SCSI_PRI      0x01000000    /* sys has int pri SCSI */
#define MACHINE_SCSI_SEC      0x02000000    /* sys has int sec SCSI */
#define MACHINE_USB_PRI       0x04000000    /* sys has int pri USB */
#define MACHINE_USB_SEC       0x08000000    /* sys has int sec USB */
/* Combined flags. */
#define MACHINE_IDE           (MACHINE_IDE_PRI)                       /* sys has int single IDE/ATAPI - mark as pri IDE/ATAPI */
#define MACHINE_IDE_DUAL      (MACHINE_IDE_PRI | MACHINE_IDE_SEC)     /* sys has int dual IDE/ATAPI - mark as both pri and sec IDE/ATAPI */
#define MACHINE_IDE_DUALTQ    (MACHINE_IDE_TER | MACHINE_IDE_QUA)
#define MACHINE_IDE_QUAD      (MACHINE_IDE_DUAL | MACHINE_IDE_DUALTQ) /* sys has int quad IDE/ATAPI - mark as dual + both ter and and qua IDE/ATAPI */
#define MACHINE_SCSI          (MACHINE_SCSI_PRI)                      /* sys has int single SCSI - mark as pri SCSI */
#define MACHINE_SCSI_DUAL     (MACHINE_SCSI_PRI | MACHINE_SCSI_SEC)   /* sys has int dual SCSI - mark as both pri and sec SCSI */
#define MACHINE_USB           (MACHINE_USB_PRI)
#define MACHINE_USB_DUAL      (MACHINE_USB_PRI | MACHINE_USB_SEC)
/* Special combined flags. */
#define MACHINE_PIIX		(MACHINE_IDE_DUAL)
#define MACHINE_PIIX3		(MACHINE_PIIX | MACHINE_USB)
/* TODO: ACPI flag. */
#define MACHINE_PIIX4		(MACHINE_PIIX3 | MACHINE_ACPI)

#define IS_ARCH(m, a)		((machines[m].bus_flags & (a)) ? 1 : 0)
#define IS_AT(m)		(((machines[m].bus_flags & (MACHINE_BUS_ISA16 | MACHINE_BUS_EISA | MACHINE_BUS_VLB | MACHINE_BUS_MCA | MACHINE_BUS_PCI | MACHINE_BUS_PCMCIA | MACHINE_BUS_AGP | MACHINE_BUS_AC97)) && !(machines[m].bus_flags & MACHINE_PC98)) ? 1 : 0)

#define CPU_BLOCK(...)		(const uint8_t[]) {__VA_ARGS__, 0}
#define MACHINE_MULTIPLIER_FIXED -1, -1

#define CPU_BLOCK_NONE       0
#define CPU_BLOCK_QDI_FMB	 CPU_BLOCK(CPU_WINCHIP, CPU_WINCHIP2, CPU_Cx6x86, CPU_Cx6x86L, CPU_Cx6x86MX)
#define CPU_BLOCK_SOYO_4SAW2 CPU_BLOCK(CPU_i486SX, CPU_i486DX, CPU_Am486SX, CPU_Am486DX)

/* Make sure it's always an invalid value to avoid misdetections. */
#if (defined __amd64__ || defined _M_X64 || defined __aarch64__ || defined _M_ARM64)
#define MACHINE_AVAILABLE	0xffffffffffffffffULL
#else
#define MACHINE_AVAILABLE	0xffffffff
#endif

enum {
    MACHINE_TYPE_NONE = 0,
    MACHINE_TYPE_8088,
    MACHINE_TYPE_8086,
    MACHINE_TYPE_286,
    MACHINE_TYPE_386SX,
    MACHINE_TYPE_486SLC,
    MACHINE_TYPE_386DX,
    MACHINE_TYPE_386DX_486,
    MACHINE_TYPE_486,
    MACHINE_TYPE_486_S2,
    MACHINE_TYPE_486_S3,
    MACHINE_TYPE_486_MISC,
    MACHINE_TYPE_SOCKET4,
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
    MACHINE_CHIPSET_NEAT,
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
    MACHINE_CHIPSET_OPTI_493,
    MACHINE_CHIPSET_OPTI_495,
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
    MACHINE_CHIPSET_SIS_5511,
    MACHINE_CHIPSET_SIS_5571,
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
    MACHINE_CHIPSET_WD76C10,
    MACHINE_CHIPSET_MAX
};

typedef struct _machine_filter_ {
    const char	*name;
    const char  id;
} machine_filter_t;

typedef struct _machine_ {
    const char     *name;
    const char     *internal_name;
    uint32_t        type;
    uint32_t        chipset;
    int             (*init)(const struct _machine_ *);
    uintptr_t       pad, pad0, pad1, pad2;
    uint32_t        cpu_package;
    const uint8_t  *cpu_block;
    uint32_t        cpu_min_bus;
    uint32_t        cpu_max_bus;
    uint16_t        cpu_min_voltage;
    uint16_t        cpu_max_voltage;
    float           cpu_min_multi;
    float           cpu_max_multi;
    uintptr_t       bus_flags;
    uintptr_t       flags;
    uint32_t        min_ram, max_ram;
    int             ram_granularity;
    int             nvrmask;
#ifdef EMU_DEVICE_H
    const device_t *(*get_device)(void);
    const device_t *(*get_vid_device)(void);
#else
    void           *get_device;
    void           *get_vid_device;
#endif
} machine_t;

/* Global variables. */
extern const machine_filter_t machine_types[],
                              machine_chipsets[];
extern const machine_t        machines[];
extern int                    bios_only;
extern int                    machine;

/* Core functions. */
extern int	machine_count(void);
extern int	machine_available(int m);
extern char	*machine_getname(void);
extern char	*machine_getname_ex(int m);
extern char	*machine_get_internal_name(void);
extern int	machine_get_machine_from_internal_name(char *s);
extern void	machine_init(void);
#ifdef EMU_DEVICE_H
extern const device_t	*machine_getdevice(int m);
#endif
extern char	*machine_get_internal_name_ex(int m);
extern int   machine_get_nvrmask(int m);
extern int   machine_has_flags(int m, int flags);
extern int   machine_has_bus(int m, int bus_flags);
extern int   machine_has_cartridge(int m);
extern int   machine_get_min_ram(int m);
extern int   machine_get_max_ram(int m);
extern int   machine_get_ram_granularity(int m);
extern int   machine_get_type(int m);
extern void  machine_close(void);


/* Initialization functions for boards and systems. */
extern void	machine_common_init(const machine_t *);

/* m_amstrad.c */
extern int	machine_pc1512_init(const machine_t *);
extern int	machine_pc1640_init(const machine_t *);
extern int	machine_pc200_init(const machine_t *);
extern int	machine_ppc512_init(const machine_t *);
extern int	machine_pc2086_init(const machine_t *);
extern int	machine_pc3086_init(const machine_t *);

#ifdef EMU_DEVICE_H
extern const device_t  	*pc1512_get_device(void);
extern const device_t 	*pc1640_get_device(void);
extern const device_t 	*pc200_get_device(void);
extern const device_t 	*ppc512_get_device(void);
extern const device_t 	*pc2086_get_device(void);
extern const device_t 	*pc3086_get_device(void);
#endif

/* m_at.c */
extern void	machine_at_common_init_ex(const machine_t *, int type);
extern void	machine_at_common_init(const machine_t *);
extern void	machine_at_init(const machine_t *);
extern void	machine_at_ps2_init(const machine_t *);
extern void	machine_at_common_ide_init(const machine_t *);
extern void	machine_at_ibm_common_ide_init(const machine_t *);
extern void	machine_at_ide_init(const machine_t *);
extern void	machine_at_ps2_ide_init(const machine_t *);

extern int	machine_at_ibm_init(const machine_t *);

//IBM AT with custom BIOS
extern int	machine_at_ibmatami_init(const machine_t *); // IBM AT with AMI BIOS
extern int	machine_at_ibmatpx_init(const machine_t *); //IBM AT with Phoenix BIOS
extern int	machine_at_ibmatquadtel_init(const machine_t *); // IBM AT with Quadtel BIOS

extern int	machine_at_ibmxt286_init(const machine_t *);

extern int	machine_at_siemens_init(const machine_t *); //Siemens PCD-2L. N82330 discrete machine. It segfaults in some places

#if defined(DEV_BRANCH) && defined(USE_OPEN_AT)
extern int	machine_at_openat_init(const machine_t *);
#endif

/* m_at_286_386sx.c */
extern int	machine_at_tg286m_init(const machine_t *);
extern int	machine_at_ama932j_init(const machine_t *);
extern int	machine_at_px286_init(const machine_t *);
extern int	machine_at_quadt286_init(const machine_t *);
extern int	machine_at_mr286_init(const machine_t *);

extern int	machine_at_neat_init(const machine_t *);
extern int	machine_at_neat_ami_init(const machine_t *);

extern int	machine_at_quadt386sx_init(const machine_t *);

extern int	machine_at_award286_init(const machine_t *);
extern int	machine_at_gdc212m_init(const machine_t *);
extern int	machine_at_gw286ct_init(const machine_t *);
extern int	machine_at_super286tr_init(const machine_t *);
extern int	machine_at_spc4200p_init(const machine_t *);
extern int	machine_at_spc4216p_init(const machine_t *);
extern int	machine_at_spc4620p_init(const machine_t *);
extern int	machine_at_kmxc02_init(const machine_t *);
extern int	machine_at_deskmaster286_init(const machine_t *);

extern int	machine_at_pc8_init(const machine_t *);
extern int	machine_at_3302_init(const machine_t *);

#if defined(DEV_BRANCH) && defined(USE_OLIVETTI)
extern int	machine_at_m290_init(const machine_t *);
#endif

extern int	machine_at_shuttle386sx_init(const machine_t *);
extern int	machine_at_adi386sx_init(const machine_t *);
extern int	machine_at_cmdsl386sx16_init(const machine_t *);
extern int	machine_at_cmdsl386sx25_init(const machine_t *);
extern int	machine_at_dataexpert386sx_init(const machine_t *);
extern int	machine_at_spc6033p_init(const machine_t *);
extern int	machine_at_wd76c10_init(const machine_t *);
extern int	machine_at_arb1374_init(const machine_t *);
extern int	machine_at_sbc350a_init(const machine_t *);
extern int	machine_at_flytech386_init(const machine_t *);
extern int	machine_at_mr1217_init(const machine_t *);
extern int	machine_at_pja511m_init(const machine_t *);
extern int	machine_at_prox1332_init(const machine_t *);

extern int	machine_at_awardsx_init(const machine_t *);

extern int 	machine_at_pc916sx_init(const machine_t *);

#ifdef EMU_DEVICE_H
extern const device_t	*at_ama932j_get_device(void);
extern const device_t	*at_flytech386_get_device(void);
extern const device_t	*at_cmdsl386sx25_get_device(void);
extern const device_t	*at_spc4620p_get_device(void);
extern const device_t	*at_spc6033p_get_device(void);
#endif

/* m_at_386dx_486.c */
extern int	machine_at_acc386_init(const machine_t *);
extern int	machine_at_asus386_init(const machine_t *);
extern int	machine_at_ecs386_init(const machine_t *);
extern int	machine_at_spc6000a_init(const machine_t *);
extern int	machine_at_micronics386_init(const machine_t *);

extern int	machine_at_rycleopardlx_init(const machine_t *);

extern int	machine_at_486vchd_init(const machine_t *);

extern int	machine_at_cs4031_init(const machine_t *);

extern int	machine_at_pb410a_init(const machine_t *);

extern int	machine_at_decpclpv_init(const machine_t *);
extern int	machine_at_acerv10_init(const machine_t *);

extern int	machine_at_acera1g_init(const machine_t *);
extern int	machine_at_ali1429_init(const machine_t *);
extern int	machine_at_winbios1429_init(const machine_t *);

extern int	machine_at_opti495_init(const machine_t *);
extern int	machine_at_opti495_ami_init(const machine_t *);
extern int	machine_at_opti495_mr_init(const machine_t *);

extern int	machine_at_vect486vl_init(const machine_t *);
extern int	machine_at_d824_init(const machine_t *);

extern int	machine_at_403tg_init(const machine_t *);
extern int	machine_at_403tg_d_init(const machine_t *);
extern int	machine_at_403tg_d_mr_init(const machine_t *);
extern int	machine_at_pc330_6573_init(const machine_t *);
extern int	machine_at_mvi486_init(const machine_t *);

extern int	machine_at_sis401_init(const machine_t *);
extern int	machine_at_isa486_init(const machine_t *);
extern int	machine_at_av4_init(const machine_t *);
extern int	machine_at_valuepoint433_init(const machine_t *);

extern int	machine_at_vli486sv2g_init(const machine_t *);
extern int	machine_at_ami471_init(const machine_t *);
extern int	machine_at_dtk486_init(const machine_t *);
extern int	machine_at_px471_init(const machine_t *);
extern int	machine_at_win471_init(const machine_t *);
extern int	machine_at_vi15g_init(const machine_t *);
extern int	machine_at_greenb_init(const machine_t *);

extern int	machine_at_r418_init(const machine_t *);
extern int	machine_at_ls486e_init(const machine_t *);
extern int	machine_at_4dps_init(const machine_t *);
extern int	machine_at_4saw2_init(const machine_t *);
extern int	machine_at_m4li_init(const machine_t *);
extern int	machine_at_alfredo_init(const machine_t *);
extern int	machine_at_ninja_init(const machine_t *);
extern int	machine_at_486sp3_init(const machine_t *);
extern int	machine_at_486sp3c_init(const machine_t *);
extern int	machine_at_486sp3g_init(const machine_t *);
extern int	machine_at_486ap4_init(const machine_t *);
extern int	machine_at_g486vpa_init(const machine_t *);
extern int	machine_at_486vipio2_init(const machine_t *);
extern int	machine_at_abpb4_init(const machine_t *);
extern int	machine_at_win486pci_init(const machine_t *);
extern int	machine_at_ms4145_init(const machine_t *);
extern int	machine_at_sbc490_init(const machine_t *);
extern int	machine_at_tf486_init(const machine_t *);

extern int	machine_at_pci400cb_init(const machine_t *);
extern int	machine_at_g486ip_init(const machine_t *);

extern int	machine_at_itoxstar_init(const machine_t *);
extern int	machine_at_arb1423c_init(const machine_t *);
extern int	machine_at_arb1479_init(const machine_t *);
extern int	machine_at_pcm9340_init(const machine_t *);
extern int	machine_at_pcm5330_init(const machine_t *);

extern int	machine_at_ecs486_init(const machine_t *);
extern int	machine_at_hot433_init(const machine_t *);
extern int	machine_at_atc1415_init(const machine_t *);
extern int	machine_at_actionpc2600_init(const machine_t *);
extern int	machine_at_m919_init(const machine_t *);
extern int	machine_at_spc7700plw_init(const machine_t *);

#ifdef EMU_DEVICE_H
extern const device_t 	*at_acera1g_get_device(void);
extern const device_t 	*at_vect486vl_get_device(void);
extern const device_t 	*at_d824_get_device(void);
extern const device_t 	*at_pcs46c_get_device(void);
extern const device_t 	*at_valuepoint433_get_device(void);
extern const device_t  	*at_sbc490_get_device(void);
#endif

/* m_at_commodore.c */
extern int	machine_at_cmdpc_init(const machine_t *);

/* m_at_compaq.c */
extern int	machine_at_portableii_init(const machine_t *);
extern int	machine_at_portableiii_init(const machine_t *);
extern int	machine_at_portableiii386_init(const machine_t *);
#if defined(DEV_BRANCH) && defined(USE_DESKPRO386)
extern int	machine_at_deskpro386_init(const machine_t *);
#endif
#ifdef EMU_DEVICE_H
extern const device_t 	*at_cpqiii_get_device(void);
#endif

/* m_at_socket4.c */
extern void	machine_at_premiere_common_init(const machine_t *, int);
extern void	machine_at_award_common_init(const machine_t *);

extern void	machine_at_sp4_common_init(const machine_t *model);

extern int	machine_at_excaliburpci_init(const machine_t *);
extern int	machine_at_p5mp3_init(const machine_t *);
extern int	machine_at_dellxp60_init(const machine_t *);
extern int	machine_at_opti560l_init(const machine_t *);
extern int	machine_at_ambradp60_init(const machine_t *);
extern int	machine_at_valuepointp60_init(const machine_t *);
extern int	machine_at_revenge_init(const machine_t *);
extern int	machine_at_586mc1_init(const machine_t *);
extern int	machine_at_pb520r_init(const machine_t *);

extern int	machine_at_excalibur_init(const machine_t *);

extern int	machine_at_p5vl_init(const machine_t *);

extern int	machine_at_excaliburpci2_init(const machine_t *);
extern int	machine_at_p5sp4_init(const machine_t *);

#ifdef EMU_DEVICE_H
extern const device_t	*at_pb520r_get_device(void);
#endif

/* m_at_socket5.c */
extern int	machine_at_plato_init(const machine_t *);
extern int	machine_at_ambradp90_init(const machine_t *);
extern int	machine_at_430nx_init(const machine_t *);

extern int	machine_at_acerv30_init(const machine_t *);
extern int	machine_at_apollo_init(const machine_t *);
extern int	machine_at_exp8551_init(const machine_t *);
extern int	machine_at_zappa_init(const machine_t *);
extern int	machine_at_powermatev_init(const machine_t *);
extern int	machine_at_mb500n_init(const machine_t *);
extern int	machine_at_hawk_init(const machine_t *);

extern int	machine_at_pat54pv_init(const machine_t *);

extern int	machine_at_hot543_init(const machine_t *);

extern int	machine_at_p54sp4_init(const machine_t *);
extern int	machine_at_sq588_init(const machine_t *);


/* m_at_socket7_3v.c */
extern int	machine_at_p54tp4xe_init(const machine_t *);
extern int	machine_at_p54tp4xe_mr_init(const machine_t *);
extern int	machine_at_gw2katx_init(const machine_t *);
extern int	machine_at_thor_init(const machine_t *);
extern int	machine_at_mrthor_init(const machine_t *);
extern int	machine_at_endeavor_init(const machine_t *);
extern int	machine_at_ms5119_init(const machine_t *);
extern int	machine_at_pb640_init(const machine_t *);
extern int	machine_at_fmb_init(const machine_t *);

extern int	machine_at_acerm3a_init(const machine_t *);
extern int	machine_at_ap53_init(const machine_t *);
extern int	machine_at_8500tuc_init(const machine_t *);
extern int	machine_at_p55t2s_init(const machine_t *);

extern int	machine_at_p5vxb_init(const machine_t *);
extern int	machine_at_gw2kte_init(const machine_t *);

extern int	machine_at_ap5s_init(const machine_t *);
extern int	machine_at_vectra54_init(const machine_t *);

#ifdef EMU_DEVICE_H
extern const device_t	*at_endeavor_get_device(void);
#define at_vectra54_get_device	at_endeavor_get_device
extern const device_t	*at_thor_get_device(void);
#define at_mrthor_get_device	at_thor_get_device
extern const device_t	*at_pb640_get_device(void);
#endif

/* m_at_socket7.c */
extern int	machine_at_acerv35n_init(const machine_t *);
extern int	machine_at_p55t2p4_init(const machine_t *);
extern int	machine_at_m7shi_init(const machine_t *);
extern int	machine_at_tc430hx_init(const machine_t *);
extern int	machine_at_equium5200_init(const machine_t *);
extern int	machine_at_pcv90_init(const machine_t *);
extern int	machine_at_p65up5_cp55t2d_init(const machine_t *);

extern int	machine_at_ap5vm_init(const machine_t *);
extern int	machine_at_p55tvp4_init(const machine_t *);
extern int	machine_at_5ivg_init(const machine_t *);
extern int	machine_at_8500tvxa_init(const machine_t *);
extern int	machine_at_presario2240_init(const machine_t *);
extern int	machine_at_presario4500_init(const machine_t *);
extern int	machine_at_p55va_init(const machine_t *);
extern int	machine_at_brio80xx_init(const machine_t *);
extern int	machine_at_pb680_init(const machine_t *);
extern int	machine_at_mb520n_init(const machine_t *);
extern int	machine_at_i430vx_init(const machine_t *);

extern int	machine_at_nupro592_init(const machine_t *);
extern int	machine_at_tx97_init(const machine_t *);
#if defined(DEV_BRANCH) && defined(USE_AN430TX)
extern int	machine_at_an430tx_init(const machine_t *);
#endif
extern int	machine_at_ym430tx_init(const machine_t *);
extern int	machine_at_mb540n_init(const machine_t *);
extern int	machine_at_p5mms98_init(const machine_t *);

extern int	machine_at_ficva502_init(const machine_t *);

extern int	machine_at_ficpa2012_init(const machine_t *);

extern int	machine_at_r534f_init(const machine_t *);
extern int	machine_at_ms5146_init(const machine_t *);

extern int	machine_at_m560_init(const machine_t *);
extern int	machine_at_ms5164_init(const machine_t *);

#ifdef EMU_DEVICE_H
extern const device_t	*at_presario2240_get_device(void);
#define at_presario4500_get_device	at_presario2240_get_device
#endif

/* m_at_sockets7.c */
extern int	machine_at_p5a_init(const machine_t *);
extern int	machine_at_m579_init(const machine_t *);
extern int	machine_at_5aa_init(const machine_t *);
extern int	machine_at_5ax_init(const machine_t *);

extern int	machine_at_ax59pro_init(const machine_t *);
extern int	machine_at_mvp3_init(const machine_t *);
extern int	machine_at_ficva503a_init(const machine_t *);
extern int	machine_at_5emapro_init(const machine_t *);

/* m_at_socket8.c */
extern int	machine_at_p6rp4_init(const machine_t *);

extern int	machine_at_686nx_init(const machine_t *);
extern int	machine_at_acerv60n_init(const machine_t *);
extern int	machine_at_vs440fx_init(const machine_t *);
extern int	machine_at_ap440fx_init(const machine_t *);
extern int	machine_at_mb600n_init(const machine_t *);
extern int	machine_at_8600ttc_init(const machine_t *);
extern int	machine_at_m6mi_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern void	machine_at_p65up5_common_init(const machine_t *, const device_t *northbridge);
#endif
extern int	machine_at_p65up5_cp6nd_init(const machine_t *);

/* m_at_slot1.c */
extern int	machine_at_m729_init(const machine_t *);

extern int	machine_at_p65up5_cpknd_init(const machine_t *);
extern int	machine_at_kn97_init(const machine_t *);

extern int	machine_at_lx6_init(const machine_t *);
extern int	machine_at_spitfire_init(const machine_t *);

extern int	machine_at_p6i440e2_init(const machine_t *);

extern int	machine_at_p2bls_init(const machine_t *);
extern int	machine_at_p3bf_init(const machine_t *);
extern int	machine_at_bf6_init(const machine_t *);
extern int	machine_at_ax6bc_init(const machine_t *);
extern int	machine_at_atc6310bxii_init(const machine_t *);
extern int	machine_at_686bx_init(const machine_t *);
extern int	machine_at_s1846_init(const machine_t *);
extern int	machine_at_p6sba_init(const machine_t *);
extern int	machine_at_ficka6130_init(const machine_t *);
extern int	machine_at_p3v133_init(const machine_t *);
extern int	machine_at_p3v4x_init(const machine_t *);

extern int	machine_at_vei8_init(const machine_t *);

extern int	machine_at_borapro_init(const machine_t *);
extern int	machine_at_ms6168_init(const machine_t *);

#ifdef EMU_DEVICE_H
extern const device_t 	*at_s1846_get_device(void);
#define at_s1857_get_device	at_s1846_get_device
#define at_gt694va_get_device	at_s1846_get_device
extern const device_t 	*at_ms6168_get_device(void);
#define at_borapro_get_device	at_ms6168_get_device
#endif

/* m_at_slot2.c */
extern int	machine_at_6gxu_init(const machine_t *);
extern int	machine_at_s2dge_init(const machine_t *);
extern int	machine_at_fw6400gx_init(const machine_t *);

/* m_at_socket370.c */
extern int	machine_at_s370slm_init(const machine_t *);

extern int	machine_at_cubx_init(const machine_t *);
extern int	machine_at_atc7020bxii_init(const machine_t *);
extern int	machine_at_ambx133_init(const machine_t *);
extern int	machine_at_awo671r_init(const machine_t *);
extern int	machine_at_63a1_init(const machine_t *);
extern int	machine_at_s370sba_init(const machine_t *);
extern int	machine_at_apas3_init(const machine_t *);
extern int	machine_at_gt694va_init(const machine_t *);
extern int	machine_at_cuv4xls_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t 	*at_cuv4xls_get_device(void);
#endif
extern int	machine_at_6via90ap_init(const machine_t *);
extern int	machine_at_s1857_init(const machine_t *);
extern int	machine_at_p6bap_init(const machine_t *);

/* m_at_misc.c */
extern int	machine_at_vpc2007_init(const machine_t *);

/* m_at_t3100e.c */
extern int	machine_at_t3100e_init(const machine_t *);

/* m_europc.c */
extern int	machine_europc_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t europc_device;
#endif

/* m_xt_olivetti.c */
extern int	machine_xt_m24_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const 	device_t *m24_get_device(void);
#endif
extern int	machine_xt_m240_init(const machine_t *);
extern int	machine_xt_m19_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const 	device_t *m19_get_device(void);
#endif

/* m_pcjr.c */
extern int	machine_pcjr_init(const machine_t *);

#ifdef EMU_DEVICE_H
extern const device_t	*pcjr_get_device(void);
#endif

/* m_ps1.c */
extern int	machine_ps1_m2011_init(const machine_t *);
extern int	machine_ps1_m2121_init(const machine_t *);

/* m_ps1_hdc.c */
#ifdef EMU_DEVICE_H
extern void	ps1_hdc_inform(void *, uint8_t *);
extern const device_t ps1_hdc_device;
#endif

/* m_ps2_isa.c */
extern int	machine_ps2_m30_286_init(const machine_t *);

/* m_ps2_mca.c */
extern int	machine_ps2_model_50_init(const machine_t *);
extern int	machine_ps2_model_55sx_init(const machine_t *);
extern int	machine_ps2_model_70_type3_init(const machine_t *);
extern int	machine_ps2_model_80_init(const machine_t *);
extern int	machine_ps2_model_80_axx_init(const machine_t *);

/* m_tandy.c */
extern int	tandy1k_eeprom_read(void);
extern int	machine_tandy_init(const machine_t *);
extern int	machine_tandy1000hx_init(const machine_t *);
extern int	machine_tandy1000sl2_init(const machine_t *);

/* m_v86p.c */
extern int	machine_v86p_init(const machine_t *);

#ifdef EMU_DEVICE_H
extern const device_t	*tandy1k_get_device(void);
extern const device_t	*tandy1k_hx_get_device(void);
extern const device_t   *tandy1k_sl_get_device(void);
#endif

/* m_xt.c */
extern int	machine_pc_init(const machine_t *);
extern int	machine_pc82_init(const machine_t *);

extern int	machine_xt_init(const machine_t *);
extern int	machine_genxt_init(const machine_t *);

extern int	machine_xt86_init(const machine_t *);

extern int	machine_xt_americxt_init(const machine_t *);
extern int	machine_xt_amixt_init(const machine_t *);
extern int	machine_xt_dtk_init(const machine_t *);
extern int	machine_xt_jukopc_init(const machine_t *);
extern int	machine_xt_openxt_init(const machine_t *);
extern int	machine_xt_pcxt_init(const machine_t *);
extern int	machine_xt_pxxt_init(const machine_t *);
extern int	machine_xt_pc4i_init(const machine_t *);
extern int	machine_xt_mpc1600_init(const machine_t *);
extern int	machine_xt_pcspirit_init(const machine_t *);
extern int	machine_xt_pc700_init(const machine_t *);
extern int	machine_xt_pc500_init(const machine_t *);
extern int	machine_xt_vendex_init(const machine_t *);
extern int	machine_xt_znic_init(const machine_t *);

extern int	machine_xt_iskra3104_init(const machine_t *);

/* m_xt_compaq.c */
extern int	machine_xt_compaq_deskpro_init(const machine_t *);
extern int	machine_xt_compaq_portable_init(const machine_t *);

/* m_xt_laserxt.c */
#if defined(DEV_BRANCH) && defined(USE_LASERXT)
extern int	machine_xt_laserxt_init(const machine_t *);
extern int	machine_xt_lxt3_init(const machine_t *);
#endif

/* m_xt_philips.c */
extern int	machine_xt_p3105_init(const machine_t *);
extern int	machine_xt_p3120_init(const machine_t *);
/* m_xt_t1000.c */
extern int	machine_xt_t1000_init(const machine_t *);
extern int	machine_xt_t1200_init(const machine_t *);

#ifdef EMU_DEVICE_H
extern const device_t	*t1000_get_device(void);
extern const device_t	*t1200_get_device(void);
#endif

/* m_xt_zenith.c */
extern int	machine_xt_z184_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const 	device_t *z184_get_device(void);
#endif
extern int	machine_xt_z151_init(const machine_t *);
extern int	machine_xt_z159_init(const machine_t *);

/* m_xt_xi8088.c */
extern int	machine_xt_xi8088_init(const machine_t *);

#ifdef EMU_DEVICE_H
extern const device_t	*xi8088_get_device(void);
#endif

#endif	/*EMU_MACHINE_H*/
