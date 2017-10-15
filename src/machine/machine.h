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
 * Version:	@(#)machine.h	1.0.7	2017/10/12
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#ifndef EMU_MACHINE_H
# define EMU_MACHINE_H


/* Machine feature flags. */
#define MACHINE_PC		0x000000	/* PC architecture */
#define MACHINE_AT		0x000001	/* PC/AT architecture */
#define MACHINE_PS2		0x000002	/* PS/2 architecture */
#define MACHINE_ISA		0x000010	/* machine has ISA bus */
#define MACHINE_CBUS		0x000020	/* machine has C-BUS bus */
#define MACHINE_EISA		0x000040	/* machine has EISA bus */
#define MACHINE_VLB		0x000080	/* machine has VL bus */
#define MACHINE_MCA		0x000100	/* machine has MCA bus */
#define MACHINE_PCI		0x000200	/* machine has PCI */
#define MACHINE_AGP		0x000400	/* machine has AGP */
#define MACHINE_HAS_HDC		0x001000	/* machine has internal HDC */
#define MACHINE_PS2_HDD		0x002000	// can now remove? --FvK
#define MACHINE_NEC		0x010000
#define MACHINE_FUJITSU		0x020000
#define MACHINE_AMSTRAD		0x040000
#define MACHINE_OLIM24		0x080000
#define MACHINE_RM		0x100000

#define IS_ARCH(m, a)		(machines[(m)].flags & (a)) ? 1 : 0;


typedef struct _machine_ {
    char	name[64];
    int		id;
    char	internal_name[24];
    struct {
	char name[16];
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
} machine_t;


/* Global variables. */
extern machine_t	machines[];
extern int		machine;


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


/* Global variables for boards and systems. */
#ifdef EMU_MOUSE_H
extern mouse_t	mouse_amstrad;
extern mouse_t	mouse_olim24;
#endif


/* Initialization functions for boards and systems. */
extern void	machine_common_init(machine_t *);

extern void	machine_at_init(machine_t *);
extern void	machine_at_ide_init(machine_t *);
extern void	machine_at_top_remap_init(machine_t *);
extern void	machine_at_ide_top_remap_init(machine_t *);

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

extern void	machine_at_i440fx_init(machine_t *);
extern void	machine_at_s1668_init(machine_t *);
extern void	machine_at_ali1429_init(machine_t *);
extern void	machine_at_cmdpc_init(machine_t *);

extern void	machine_at_headland_init(machine_t *);
extern void	machine_at_neat_init(machine_t *);
extern void	machine_at_opti495_init(machine_t *);
extern void	machine_at_scat_init(machine_t *);

extern void	machine_at_dtk486_init(machine_t *);
extern void	machine_at_r418_init(machine_t *);

extern void	machine_at_wd76c10_init(machine_t *);

extern void	machine_ps1_m2011_init(machine_t *);
extern void	machine_ps1_m2121_init(machine_t *);
extern void	machine_ps1_m2133_init(machine_t *);
extern void	machine_ps2_m30_286_init(machine_t *);

extern void	machine_ps2_model_50_init(machine_t *);
extern void	machine_ps2_model_55sx_init(machine_t *);
extern void	machine_ps2_model_80_init(machine_t *);
extern void	machine_ps2_model_80_486_init(machine_t *);

extern void	machine_amstrad_init(machine_t *);

extern void	machine_europc_init(machine_t *);

extern void	machine_olim24_init(machine_t *);

extern void	machine_pcjr_init(machine_t *);

extern void	machine_tandy1k_init(machine_t *);
extern void	machine_tandy1ksl2_init(machine_t *);

extern void	machine_xt_init(machine_t *);
extern void	machine_xt_laserxt_init(machine_t *);


#endif	/*EMU_MACHINE_H*/
