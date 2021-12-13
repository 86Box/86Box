/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Definitions for the device handler.
 *
 *
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Sarah Walker, <tommowalker@tommowalker.co.uk>
 *
 *		Copyright 2017-2019 Fred N. van Kempen.
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2008-2019 Sarah Walker.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free  Software  Foundation; either  version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is  distributed in the hope that it will be useful, but
 * WITHOUT   ANY  WARRANTY;  without  even   the  implied  warranty  of
 * MERCHANTABILITY  or FITNESS  FOR A PARTICULAR  PURPOSE. See  the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the:
 *
 *   Free Software Foundation, Inc.
 *   59 Temple Place - Suite 330
 *   Boston, MA 02111-1307
 *   USA.
 */
#ifndef EMU_DEVICE_H
# define EMU_DEVICE_H


#define CONFIG_STRING		0
#define CONFIG_INT		1
#define CONFIG_BINARY		2
#define CONFIG_SELECTION	3
#define CONFIG_MIDI		4
#define CONFIG_FNAME		5
#define CONFIG_SPINNER		6
#define CONFIG_HEX16		7
#define CONFIG_HEX20		8
#define CONFIG_MAC		9
#define CONFIG_MIDI_IN  10


enum {
    DEVICE_NOT_WORKING = 1,	/* does not currently work correctly and will be disabled in a release build */
    DEVICE_LPT = 2,		/* requires a parallel port */
    DEVICE_PCJR = 4,		/* requires an IBM PCjr */
    DEVICE_AT = 8,		/* requires an AT-compatible system */
    DEVICE_PS2 = 0x10,		/* requires a PS/1 or PS/2 system */
    DEVICE_ISA = 0x20,		/* requires the ISA bus */
    DEVICE_CBUS = 0x40,		/* requires the C-BUS bus */
    DEVICE_MCA = 0x80,		/* requires the MCA bus */
    DEVICE_EISA = 0x100,	/* requires the EISA bus */
    DEVICE_VLB = 0x200,		/* requires the PCI bus */
    DEVICE_PCI = 0x400,		/* requires the VLB bus */
    DEVICE_AGP = 0x800,		/* requires the AGP bus */
    DEVICE_AC97 = 0x1000	/* requires the AC'97 bus */
};


typedef struct {
    const char *description;
    int  value;
} device_config_selection_t;

typedef struct {
    int16_t min;
    int16_t max;
    int16_t step;
} device_config_spinner_t;

typedef struct {
    const char *name;
    const char *description;
    int type;
    const char *default_string;
    int default_int;
    const char *file_filter;
    device_config_spinner_t spinner;
    const device_config_selection_t selection[16];
} device_config_t;

typedef struct _device_ {
    const char	*name;
    uint32_t	flags;		/* system flags */
    uint32_t	local;		/* flags local to device */

    void	*(*init)(const struct _device_ *);
    void	(*close)(void *priv);
    void	(*reset)(void *priv);
    union {
	int		(*available)(void);
	int		(*poll)(int x, int y, int z, int b, void *priv);
	void		(*register_pci_slot)(int device, int type, int inta, int intb, int intc, int intd, void *priv);
    };
    void	(*speed_changed)(void *priv);
    void	(*force_redraw)(void *priv);

    const device_config_t *config;
} device_t;

typedef struct {
    const device_t	*dev;
    char		name[2048];
} device_context_t;


#ifdef __cplusplus
extern "C" {
#endif

extern void		device_init(void);
extern void		device_set_context(device_context_t *c, const device_t *d, int inst);
extern void		device_context(const device_t *d);
extern void		device_context_inst(const device_t *d, int inst);
extern void		device_context_restore(void);
extern void		*device_add(const device_t *d);
extern void		device_add_ex(const device_t *d, void *priv);
extern void		*device_add_inst(const device_t *d, int inst);
extern void		device_add_inst_ex(const device_t *d, void *priv, int inst);
extern void		*device_cadd(const device_t *d, const device_t *cd);
extern void		device_cadd_ex(const device_t *d, const device_t *cd, void *priv);
extern void		*device_cadd_inst(const device_t *d, const device_t *cd, int inst);
extern void		device_cadd_inst_ex(const device_t *d, const device_t *cd, void *priv, int inst);
extern void		device_close_all(void);
extern void		device_reset_all(void);
extern void		device_reset_all_pci(void);
extern void		*device_get_priv(const device_t *d);
extern int		device_available(const device_t *d);
extern int		device_poll(const device_t *d, int x, int y, int z, int b);
extern void		device_register_pci_slot(const device_t *d, int device, int type, int inta, int intb, int intc, int intd);
extern void		device_speed_changed(void);
extern void		device_force_redraw(void);
extern void		device_get_name(const device_t *d, int bus, char *name);

extern int		device_is_valid(const device_t *, int m);

extern int		device_get_config_int(const char *name);
extern int		device_get_config_int_ex(const char *s, int dflt_int);
extern int		device_get_config_hex16(const char *name);
extern int		device_get_config_hex20(const char *name);
extern int		device_get_config_mac(const char *name, int dflt_int);
extern void		device_set_config_int(const char *s, int val);
extern void		device_set_config_hex16(const char *s, int val);
extern void		device_set_config_hex20(const char *s, int val);
extern void		device_set_config_mac(const char *s, int val);
extern const char	*device_get_config_string(const char *name);

extern int	machine_get_config_int(char *s);
extern char	*machine_get_config_string(char *s);

#ifdef __cplusplus
}
#endif


#endif	/*EMU_DEVICE_H*/
