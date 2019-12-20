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
 * Version:	@(#)machine.h	1.0.34	2019/03/08
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2017-2019 Fred N. van Kempen.
 */
#ifndef EMU_MACHINE_H
# define EMU_MACHINE_H


/* Machine feature flags. */
#ifdef NEW_FLAGS
#define MACHINE_PC		0x000000	/* PC architecture */
#define MACHINE_AT		0x000001	/* PC/AT architecture */
#define MACHINE_PS2		0x000002	/* PS/2 architecture */
#define MACHINE_ISA		0x000010	/* sys has ISA bus */
#define MACHINE_CBUS		0x000020	/* sys has C-BUS bus */
#define MACHINE_EISA		0x000040	/* sys has EISA bus */
#define MACHINE_VLB		0x000080	/* sys has VL bus */
#define MACHINE_MCA		0x000100	/* sys has MCA bus */
#define MACHINE_PCI		0x000200	/* sys has PCI bus */
#define MACHINE_AGP		0x000400	/* sys has AGP bus */
#define MACHINE_HDC		0x001000	/* sys has int HDC */
#define MACHINE_VIDEO		0x002000	/* sys has int video */
#define MACHINE_VIDEO_FIXED	0x004000	/* sys has ONLY int video */
#define MACHINE_MOUSE		0x008000	/* sys has int mouse */
#define MACHINE_NONMI		0x010000	/* sys does not have NMI's */
#else
#define MACHINE_PC		0x000000	/* PC architecture */
#define MACHINE_AT		0x000001	/* PC/AT architecture */
#define MACHINE_PS2		0x000002	/* PS/2 architecture */
#define MACHINE_ISA		0x000010	/* sys has ISA bus */
#define MACHINE_CBUS		0x000020	/* sys has C-BUS bus */
#define MACHINE_EISA		0x000040	/* sys has EISA bus */
#define MACHINE_VLB		0x000080	/* sys has VL bus */
#define MACHINE_MCA		0x000100	/* sys has MCA bus */
#define MACHINE_PCI		0x000200	/* sys has PCI bus */
#define MACHINE_AGP		0x000400	/* sys has AGP bus */
#define MACHINE_HDC		0x001000	/* sys has int HDC */
#define MACHINE_VIDEO		0x002000	/* sys has int video */
#define MACHINE_VIDEO_FIXED	0x004000	/* sys has ONLY int video */
#define MACHINE_MOUSE		0x008000	/* sys has int mouse */
#define MACHINE_NONMI		0x010000	/* sys does not have NMI's */
#endif

#define IS_ARCH(m, a)		(machines[(m)].flags & (a)) ? 1 : 0;


#ifdef NEW_STRUCT
typedef struct _machine_ {
    const char	*name;
    const char	*internal_name;
#ifdef EMU_DEVICE_H
    const device_t	*device;
#else
    void	*device;
#endif
    struct {
	const char *name;
#ifdef EMU_CPU_H
	CPU *cpus;
#else
	void *cpus;
#endif
    }		cpu[5];
    int		flags;
    uint32_t	min_ram, max_ram;
    int		ram_granularity;
    int		nvrmask;
} machine_t;
#else
typedef struct _machine_ {
    const char	*name;
    const char	*internal_name;
    struct {
	const char *name;
#ifdef EMU_CPU_H
	CPU *cpus;
#else
	void *cpus;
#endif
    }		cpu[5];
    int		flags;
    uint32_t	min_ram, max_ram;
    int		ram_granularity;
    int		nvrmask;
    int		(*init)(const struct _machine_ *);
#ifdef EMU_DEVICE_H
    const device_t	*(*get_device)(void);
#else
    void	*get_device;
#endif
} machine_t;
#endif


/* Global variables. */
extern const machine_t	machines[];
extern int		bios_only;
extern int		machine;
extern int		AT, PCI;


/* Core functions. */
extern int	machine_count(void);
extern int	machine_available(int m);
extern char	*machine_getname(void);
extern char	*machine_get_internal_name(void);
extern int	machine_get_machine_from_internal_name(char *s);
extern void	machine_init(void);
#ifdef EMU_DEVICE_H
extern const device_t	*machine_getdevice(int m);
#endif
extern char	*machine_get_internal_name_ex(int m);
extern int	machine_get_nvrmask(int m);
extern void	machine_close(void);


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
extern void	machine_at_common_init_ex(const machine_t *, int is_ibm);
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

#if defined(DEV_BRANCH) && defined(USE_OPEN_AT)
extern int	machine_at_open_at_init(const machine_t *);
#endif

/* m_at_286_386sx.c */
#if defined(DEV_BRANCH) && defined(USE_AMI386SX)
extern int	machine_at_headland_init(const machine_t *);
#endif
extern int	machine_at_tg286m_init(const machine_t *);
extern int	machine_at_ama932j_init(const machine_t *);
extern int	machine_at_headlandpho_init(const machine_t *);
extern int	machine_at_headlandquadtel_init(const machine_t *);

extern int	machine_at_neat_init(const machine_t *);
extern int	machine_at_neat_ami_init(const machine_t *);
#if defined(DEV_BRANCH) && defined(USE_GOLDSTAR386)
extern int	machine_at_goldstar386_init(const machine_t *); //Neat based Phoenix 80386 board. It has memory related issues.
#endif

extern int	machine_at_award286_init(const machine_t *);
extern int	machine_at_gw286ct_init(const machine_t *);
extern int	machine_at_super286tr_init(const machine_t *);
extern int	machine_at_spc4200p_init(const machine_t *);
extern int	machine_at_spc4216p_init(const machine_t *);
extern int	machine_at_kmxc02_init(const machine_t *);
extern int	machine_at_deskmaster286_init(const machine_t *);

extern int	machine_at_wd76c10_init(const machine_t *);

#ifdef EMU_DEVICE_H
extern const device_t 	*at_ama932j_get_device(void);
#endif

/* m_at_386dx_486.c */
extern int	machine_at_pb410a_init(const machine_t *);

extern int	machine_at_ali1429_init(const machine_t *);
extern int	machine_at_winbios1429_init(const machine_t *);

extern int	machine_at_opti495_init(const machine_t *);
extern int	machine_at_opti495_ami_init(const machine_t *);
#if defined(DEV_BRANCH) && defined(USE_MR495)
extern int	machine_at_opti495_mr_init(const machine_t *);
#endif

extern int	machine_at_ami471_init(const machine_t *);
extern int	machine_at_dtk486_init(const machine_t *);
extern int	machine_at_px471_init(const machine_t *);
extern int	machine_at_win471_init(const machine_t *);

extern int	machine_at_r418_init(const machine_t *);
extern int	machine_at_alfredo_init(const machine_t *);

/* m_at_commodore.c */
extern int	machine_at_cmdpc_init(const machine_t *);

/* m_at_compaq.c */
extern int	machine_at_portableii_init(const machine_t *);
#if defined(DEV_BRANCH) && defined(USE_PORTABLE3)
extern int	machine_at_portableiii_init(const machine_t *);
extern int	machine_at_portableiii386_init(const machine_t *);
#endif

/* m_at_socket4_5.c */
extern int	machine_at_batman_init(const machine_t *);
extern int	machine_at_ambradp60_init(const machine_t *);
extern int	machine_at_valuepointp60_init(const machine_t *);
extern int	machine_at_586mc1_init(const machine_t *);

extern int	machine_at_plato_init(const machine_t *);
extern int	machine_at_ambradp90_init(const machine_t *);
extern int	machine_at_430nx_init(const machine_t *);

extern int	machine_at_p54tp4xe_init(const machine_t *);
extern int	machine_at_endeavor_init(const machine_t *);
extern int	machine_at_zappa_init(const machine_t *);
extern int	machine_at_mb500n_init(const machine_t *);
extern int	machine_at_president_init(const machine_t *);
#if defined(DEV_BRANCH) && defined(USE_VECTRA54)
extern int	machine_at_vectra54_init(const machine_t *);
#endif

#ifdef EMU_DEVICE_H
extern const device_t	*at_endeavor_get_device(void);
#endif

/* m_at_socket7_s7.c */
extern int	machine_at_thor_init(const machine_t *);
#if defined(DEV_BRANCH) && defined(USE_MRTHOR)
extern int	machine_at_mrthor_init(const machine_t *);
#endif
extern int	machine_at_pb640_init(const machine_t *);

extern int	machine_at_acerm3a_init(const machine_t *);
extern int	machine_at_acerv35n_init(const machine_t *);
extern int	machine_at_ap53_init(const machine_t *);
extern int	machine_at_p55t2p4_init(const machine_t *);
extern int	machine_at_p55t2s_init(const machine_t *);
#if defined(DEV_BRANCH) && defined(USE_TC430HX)
extern int	machine_at_tc430hx_init(const machine_t *);
#endif

extern int	machine_at_p55tvp4_init(const machine_t *);
extern int	machine_at_i430vx_init(const machine_t *);
extern int	machine_at_p55va_init(const machine_t *);
extern int	machine_at_j656vxd_init(const machine_t *);

#ifdef EMU_DEVICE_H
extern const device_t	*at_pb640_get_device(void);
#endif

/* m_at_socket8.c */
#if defined(DEV_BRANCH) && defined(USE_I686)
extern int	machine_at_i440fx_init(const machine_t *);
extern int	machine_at_s1668_init(const machine_t *);
#endif

/* m_at_t3100e.c */
extern int	machine_at_t3100e_init(const machine_t *);

/* m_europc.c */
extern int	machine_europc_init(const machine_t *);
#ifdef EMU_DEVICE_H
extern const device_t europc_device;
#endif

/* m_oivetti_m24.c */
extern int	machine_olim24_init(const machine_t *);

/* m_pcjr.c */
extern int	machine_pcjr_init(const machine_t *);

#ifdef EMU_DEVICE_H
extern const device_t	*pcjr_get_device(void);
#endif

/* m_ps1.c */
extern int	machine_ps1_m2011_init(const machine_t *);
extern int	machine_ps1_m2121_init(const machine_t *);
#if defined(DEV_BRANCH) && defined(USE_PS1M2133)
extern int	machine_ps1_m2133_init(const machine_t *);
#endif

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
#if defined(DEV_BRANCH) && defined(USE_PS2M70T4)
extern int	machine_ps2_model_70_type4_init(const machine_t *);
#endif
extern int	machine_ps2_model_80_init(const machine_t *);

/* m_tandy.c */
extern int	tandy1k_eeprom_read(void);
extern int	machine_tandy_init(const machine_t *);
extern int	machine_tandy1000hx_init(const machine_t *);
extern int	machine_tandy1000sl2_init(const machine_t *);

#ifdef EMU_DEVICE_H
extern const device_t	*tandy1k_get_device(void);
extern const device_t	*tandy1k_hx_get_device(void);
#endif

/* m_xt.c */
extern int	machine_pc_init(const machine_t *);
extern int	machine_pc82_init(const machine_t *);

extern int	machine_xt_init(const machine_t *);
extern int	machine_genxt_init(const machine_t *);

extern int	machine_xt86_init(const machine_t *);

extern int	machine_xt_amixt_init(const machine_t *);
extern int	machine_xt_dtk_init(const machine_t *);
extern int	machine_xt_jukopc_init(const machine_t *);
extern int	machine_xt_open_xt_init(const machine_t *);
extern int	machine_xt_pxxt_init(const machine_t *);

/* m_xt_compaq.c */
extern int	machine_xt_compaq_init(const machine_t *);

/* m_xt_laserxt.c */
#if defined(DEV_BRANCH) && defined(USE_LASERXT)
extern int	machine_xt_laserxt_init(const machine_t *);
extern int	machine_xt_lxt3_init(const machine_t *);
#endif

/* m_xt_t1000.c */
extern int	machine_xt_t1000_init(const machine_t *);
extern int	machine_xt_t1200_init(const machine_t *);

#ifdef EMU_DEVICE_H
extern const device_t	*t1000_get_device(void);
extern const device_t	*t1200_get_device(void);
#endif

/* m_xt_zenith.c */
extern int	machine_xt_zenith_init(const machine_t *);

/* m_xt_xi8088.c */
extern int	machine_xt_xi8088_init(const machine_t *);

#ifdef EMU_DEVICE_H
extern const device_t	*xi8088_get_device(void);
#endif


#endif	/*EMU_MACHINE_H*/
