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
 * Version:	@(#)model.h	1.0.2	2017/06/17
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */
#ifndef EMU_MODEL_H
# define EMU_MODEL_H


#define MODEL_AT	   1
#define MODEL_PS2	   2
#define MODEL_AMSTRAD	   4
#define MODEL_OLIM24	   8
#define MODEL_HAS_IDE	  16
#define MODEL_MCA	  32
#define MODEL_PCI	  64
#define MODEL_PS2_HDD	 128
#define MODEL_NEC	 256
#define MODEL_FUJITSU	 512
#define MODEL_RM	1024


typedef struct {
    char	name[64];
    int		id;
    char	internal_name[24];
    struct {
	char name[16];
	CPU *cpus;
    }		cpu[5];
    int		fixed_gfxcard;
    int		flags;
    int		min_ram, max_ram;
    int		ram_granularity;
    int		nvrmask;
    void	(*init)(void);
    device_t	*(*get_device)(void);
} MODEL;


/* Global variables. */
extern MODEL	models[];
extern int	model;


/* Core functions. */
extern int	model_count(void);
extern int	model_getromset(void);
extern int	model_getmodel(int romset);
extern char	*model_getname(void);
extern char	*model_get_internal_name(void);
extern int	model_get_model_from_internal_name(char *s);
extern void	model_init(void);
extern device_t	*model_getdevice(int model);
extern int	model_getromset_ex(int m);
extern char	*model_get_internal_name_ex(int m);
extern int	model_get_nvrmask(int m);


/* Global variables for boards and systems. */
#ifdef EMU_MOUSE_H
extern mouse_t	mouse_amstrad;
extern mouse_t	mouse_olim24;
#endif


/* Initialization functions for boards and systems. */
extern void	acer386sx_init(void);
extern void	acerm3a_io_init(void);
extern void	ali1429_init(void);
extern void	ali1429_reset(void);
extern void	amstrad_init(void);
extern void	compaq_init(void);
extern void	headland_init(void);
extern void	i430fx_init(void);
extern void	i430hx_init(void);
extern void	i430lx_init(void);
extern void	i430nx_init(void);
extern void	i430vx_init(void);
extern void	i440fx_init(void);
extern void	jim_init(void);
extern void	laserxt_init(void);
extern void	neat_init(void);
extern void	olivetti_m24_init(void);
extern void	opti495_init(void);

extern void	ps1mb_init(void);
extern void	ps1mb_m2121_init(void);
extern void	ps1mb_m2133_init(void);

extern void	ps2board_init(void);

extern void	scat_init(void);
extern void	sis496_init(void);


#endif	/*EMU_MODEL_H*/
