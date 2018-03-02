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
 * Version:	@(#)machine.h	1.0.21	2018/03/02
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#ifndef EMU_MACHINE_H
# define EMU_MACHINE_H


/* Machine feature flags. */
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
#define MACHINE_HDC_PS2		0x002000	/* sys has int PS/2 HDC */
#define MACHINE_MOUSE		0x004000	/* sys has int mouse */
#define MACHINE_VIDEO		0x008000	/* sys has int video */

#define IS_ARCH(m, a)		(machines[(m)].flags & (a)) ? 1 : 0;


typedef struct _machine_ {
    const char	*name;
    int		id;
    const char	*internal_name;
    struct {
	const char *name;
#ifdef EMU_CPU_H
	CPU *cpus;
#else
	void *cpus;
#endif
    }		cpu[5];
    int		fixed_gfxcard;
    int		flags;
    int		min_ram, max_ram;
    int		ram_granularity;
    int		nvrmask;
    void	(*init)(struct _machine_ *);
#ifdef EMU_DEVICE_H
    device_t	*(*get_device)(void);
#else
    void	*get_device;
#endif
    void	(*nvr_close)(void);
} machine_t;


/* Global variables. */
extern machine_t	machines[];
extern int		machine;
extern int		romset;
extern int		AT, PCI;


/* Core functions. */
extern int	machine_count(void);
extern int	machine_getromset(void);
extern int	machine_getmachine(int romset);
extern char	*machine_getname(void);
extern char	*machine_get_internal_name(void);
extern int	machine_get_machine_from_internal_name(char *s);
extern void	machine_init(void);
#ifdef EMU_DEVICE_H
extern device_t	*machine_getdevice(int machine);
#endif
extern int	machine_getromset_ex(int m);
extern char	*machine_get_internal_name_ex(int m);
extern int	machine_get_nvrmask(int m);
extern void	machine_close(void);


/* Initialization functions for boards and systems. */
extern void	machine_common_init(machine_t *);

extern void	machine_at_common_init(machine_t *);
extern void	machine_at_init(machine_t *);
extern void	machine_at_ps2_init(machine_t *);
extern void	machine_at_common_ide_init(machine_t *);
extern void	machine_at_ide_init(machine_t *);
extern void	machine_at_ps2_ide_init(machine_t *);
extern void	machine_at_top_remap_init(machine_t *);
extern void	machine_at_ide_top_remap_init(machine_t *);

extern void	machine_at_ibm_init(machine_t *);

extern void	machine_at_t3100e_init(machine_t *);

extern void	machine_at_p54tp4xe_init(machine_t *);
extern void	machine_at_endeavor_init(machine_t *);
extern void	machine_at_zappa_init(machine_t *);
extern void	machine_at_mb500n_init(machine_t *);
extern void	machine_at_president_init(machine_t *);
extern void	machine_at_thor_init(machine_t *);

extern void	machine_at_acerm3a_init(machine_t *);
extern void	machine_at_acerv35n_init(machine_t *);
extern void	machine_at_ap53_init(machine_t *);
extern void	machine_at_p55t2p4_init(machine_t *);
extern void	machine_at_p55t2s_init(machine_t *);

extern void	machine_at_batman_init(machine_t *);
extern void	machine_at_plato_init(machine_t *);

extern void	machine_at_p55tvp4_init(machine_t *);
extern void	machine_at_i430vx_init(machine_t *);
extern void	machine_at_p55va_init(machine_t *);

#if defined(DEV_BRANCH) && defined(USE_I686)
extern void	machine_at_i440fx_init(machine_t *);
extern void	machine_at_s1668_init(machine_t *);
#endif
extern void	machine_at_ali1429_init(machine_t *);
extern void	machine_at_cmdpc_init(machine_t *);

extern void	machine_at_headland_init(machine_t *);
extern void	machine_at_neat_init(machine_t *);
extern void	machine_at_neat_ami_init(machine_t *);
extern void	machine_at_opti495_init(machine_t *);
extern void	machine_at_opti495_ami_init(machine_t *);
extern void	machine_at_scat_init(machine_t *);
extern void	machine_at_scatsx_init(machine_t *);
extern void	machine_at_compaq_init(machine_t *);

extern void	machine_at_dtk486_init(machine_t *);
extern void	machine_at_r418_init(machine_t *);

extern void	machine_at_wd76c10_init(machine_t *);

#if defined(DEV_BRANCH) && defined(USE_GREENB)
extern void	machine_at_4gpv31_init(machine_t *);
#endif

extern void	machine_pcjr_init(machine_t *);

extern void	machine_ps1_m2011_init(machine_t *);
extern void	machine_ps1_m2121_init(machine_t *);
extern void	machine_ps1_m2133_init(machine_t *);

extern void	machine_ps2_m30_286_init(machine_t *);
extern void	machine_ps2_model_50_init(machine_t *);
extern void	machine_ps2_model_55sx_init(machine_t *);
extern void	machine_ps2_model_80_init(machine_t *);
#ifdef WALTJE
extern void	machine_ps2_model_80_486_init(machine_t *);
#endif

extern void	machine_amstrad_init(machine_t *);

extern void	machine_europc_init(machine_t *);
#ifdef EMU_DEVICE_H
extern device_t europc_device,
                europc_hdc_device;
#endif

extern void	machine_olim24_init(machine_t *);
extern void	machine_olim24_video_init(void);

extern void	machine_tandy1k_init(machine_t *);
extern int	tandy1k_eeprom_read(void);

extern void	machine_xt_init(machine_t *);
extern void	machine_xt_compaq_init(machine_t *);
#if defined(DEV_BRANCH) && defined(USE_LASERXT)
extern void	machine_xt_laserxt_init(machine_t *);
#endif

extern void	machine_xt_t1000_init(machine_t *);
extern void	machine_xt_t1200_init(machine_t *);

extern void	machine_xt_xi8088_init(machine_t *);

#ifdef EMU_DEVICE_H
extern device_t	*xi8088_get_device(void);

extern device_t	*pcjr_get_device(void);

extern device_t	*tandy1k_get_device(void);
extern device_t	*tandy1k_hx_get_device(void);

extern device_t	*t1000_get_device(void);
extern device_t	*t1200_get_device(void);

extern device_t	*at_endeavor_get_device(void);
#endif


#endif	/*EMU_MACHINE_H*/
