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
 * Version:	@(#)device.h	1.0.0	2017/05/30
 *
 * Author:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2008-2016 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 */

#define CONFIG_STRING 0
#define CONFIG_INT 1
#define CONFIG_BINARY 2
#define CONFIG_SELECTION 3
#define CONFIG_HEX16 4
#define CONFIG_HEX20 5
#define CONFIG_MAC 6

typedef struct device_config_selection_t
{
        char description[256];
        int value;
} device_config_selection_t;

typedef struct device_config_t
{
        char name[256];
        char description[256];
        int type;
        char default_string[256];
        int default_int;
        device_config_selection_t selection[16];
} device_config_t;

typedef struct device_t
{
        char name[50];
        uint32_t flags;
        void *(*init)();
        void (*close)(void *p);
        int  (*available)();
        void (*speed_changed)(void *p);
        void (*force_redraw)(void *p);
        void (*add_status_info)(char *s, int max_len, void *p);
        device_config_t *config;
} device_t;

void device_init();
void device_add(device_t *d);
void device_close_all();
int device_available(device_t *d);
void device_speed_changed();
void device_force_redraw();
char *device_add_status_info(char *s, int max_len);

int device_get_config_int(char *name);
int device_get_config_int_ex(char *s, int default_int);
int device_get_config_hex16(char *name);
int device_get_config_hex20(char *name);
int device_get_config_mac(char *name, int default_int);
void device_set_config_int(char *s, int val);
void device_set_config_hex16(char *s, int val);
void device_set_config_hex20(char *s, int val);
void device_set_config_mac(char *s, int val);
char *device_get_config_string(char *name);

enum
{
        DEVICE_NOT_WORKING = 1, /*Device does not currently work correctly and will be disabled in a release build*/
        DEVICE_AT = 2,          /*Device requires an AT-compatible system*/
	DEVICE_PS2 = 4,		/*Device requires a PS/1 or PS/2 system*/
	DEVICE_MCA = 0x20,      /*Device requires the MCA bus*/
	DEVICE_PCI = 0x40       /*Device requires the PCI bus*/
};

int model_get_config_int(char *s);
char *model_get_config_string(char *s);
