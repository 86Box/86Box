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
 * Version:	@(#)machine.h	1.0.3	2017/09/02
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */
#ifndef EMU_MACHINE_H
# define EMU_MACHINE_H


#define MACHINE_AT	   1
#define MACHINE_PS2	   2
#define MACHINE_AMSTRAD	   4
#define MACHINE_OLIM24	   8
#define MACHINE_HAS_IDE	  16
#define MACHINE_MCA	  32
#define MACHINE_PCI	  64
#define MACHINE_PS2_HDD	 128
#define MACHINE_NEC	 256
#define MACHINE_FUJITSU	 512
#define MACHINE_RM	1024


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
extern device_t	*machine_getdevice(int machine);
extern int	machine_getromset_ex(int m);
extern char	*machine_get_internal_name_ex(int m);
extern int	machine_get_nvrmask(int m);


/* Global variables for boards and systems. */
#ifdef EMU_MOUSE_H
extern mouse_t	mouse_amstrad;
extern mouse_t	mouse_olim24;
#endif


#endif	/*EMU_MACHINE_H*/
