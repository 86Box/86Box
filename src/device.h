/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the generic device interface to handle
 *		all devices attached to the emulator.
 *
 * Version:	@(#)device.h	1.0.1	2017/06/03
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2016 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */
#ifndef EMU_DEVICE_H
# define EMU_DEVICE_H


#define CONFIG_STRING	0
#define CONFIG_INT	1
#define CONFIG_BINARY	2
#define CONFIG_SELECTION 3
#define CONFIG_HEX16	4
#define CONFIG_HEX20	5
#define CONFIG_MAC	6


enum
{
        DEVICE_NOT_WORKING = 1, /*Device does not currently work correctly and will be disabled in a release build*/
        DEVICE_AT = 2,          /*Device requires an AT-compatible system*/
	DEVICE_PS2 = 4,		/*Device requires a PS/1 or PS/2 system*/
	DEVICE_MCA = 0x20,      /*Device requires the MCA bus*/
	DEVICE_PCI = 0x40       /*Device requires the PCI bus*/
};


typedef struct device_config_selection_t
{
        const char *description;
        int value;
} device_config_selection_t;

typedef struct device_config_t
{
        const char *name;
        const char *description;
        int type;
        const char *default_string;
        int default_int;
        device_config_selection_t selection[16];
} device_config_t;

typedef struct device_t
{
        const char *name;
        uint32_t flags;
        void *(*init)();
        void (*close)(void *p);
        int  (*available)();
        void (*speed_changed)(void *p);
        void (*force_redraw)(void *p);
        void (*add_status_info)(char *s, int max_len, void *p);
        device_config_t *config;
} device_t;


extern void	device_init(void);
extern void	device_add(device_t *d);
extern void	device_close_all(void);
extern void	*device_get_priv(device_t *d);
extern int	device_available(device_t *d);
extern void	device_speed_changed(void);
extern void	device_force_redraw(void);
extern char	*device_add_status_info(char *s, int max_len);

extern int	device_get_config_int(char *name);
extern int	device_get_config_int_ex(char *s, int default_int);
extern int	device_get_config_hex16(char *name);
extern int	device_get_config_hex20(char *name);
extern int	device_get_config_mac(char *name, int default_int);
extern void	device_set_config_int(char *s, int val);
extern void	device_set_config_hex16(char *s, int val);
extern void	device_set_config_hex20(char *s, int val);
extern void	device_set_config_mac(char *s, int val);
extern char	*device_get_config_string(char *name);

extern int	model_get_config_int(char *s);
extern char	*model_get_config_string(char *s);


#endif	/*EMU_DEVICE_H*/
