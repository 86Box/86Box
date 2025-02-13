/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the device handler.
 *
 *
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Sarah Walker, <https://pcem-emulator.co.uk/>
 *
 *          Copyright 2017-2019 Fred N. van Kempen.
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2021      Andreas J. Reichel.
 *          Copyright 2021-2025 Jasmine Iwanek.
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
#define EMU_DEVICE_H

#define CONFIG_END         -1                          /* N/A */

#define CONFIG_SHIFT        4

#define CONFIG_TYPE_INT     (0 << CONFIG_SHIFT)
#define CONFIG_TYPE_STRING  (1 << CONFIG_SHIFT)
#define CONFIG_TYPE_HEX16   (2 << CONFIG_SHIFT)
#define CONFIG_TYPE_HEX20   (3 << CONFIG_SHIFT)
#define CONFIG_TYPE_MAC     (4 << CONFIG_SHIFT)

#define CONFIG_INT          (0 | CONFIG_TYPE_INT)      /* config_get_int() */
#define CONFIG_BINARY       (1 | CONFIG_TYPE_INT)      /* config_get_int() */
#define CONFIG_SELECTION    (2 | CONFIG_TYPE_INT)      /* config_get_int() */
#define CONFIG_MIDI_OUT     (3 | CONFIG_TYPE_INT)      /* config_get_int() */
#define CONFIG_SPINNER      (4 | CONFIG_TYPE_INT)      /* config_get_int() */
#define CONFIG_MIDI_IN      (5 | CONFIG_TYPE_INT)      /* config_get_int() */
#define CONFIG_MEMORY       (6 | CONFIG_TYPE_INT)      /* config_get_int() */

#define CONFIG_STRING       (0 | CONFIG_TYPE_STRING)     /* config_get_string() */
#define CONFIG_FNAME        (1 | CONFIG_TYPE_STRING)     /* config_get_string() */
#define CONFIG_SERPORT      (2 | CONFIG_TYPE_STRING)     /* config_get_string() */
#define CONFIG_BIOS         (3 | CONFIG_TYPE_STRING)     /* config_get_string() */

#define CONFIG_HEX16        (0 | CONFIG_TYPE_HEX16)      /* config_get_hex16() */

#define CONFIG_HEX20        (0 | CONFIG_TYPE_HEX20)      /* config_get_hex20() */

#define CONFIG_MAC          (0 | CONFIG_TYPE_MAC)        /* N/A */

#define CONFIG_SUBTYPE_MASK (CONFIG_IS_STRING - 1)

#define CONFIG_DEP          (16 << CONFIG_SHIFT)
#define CONFIG_TYPE_MASK    (CONFIG_DEP - 1)

// #define CONFIG_ONBOARD    256      /* only avaialble on the on-board variant */
// #define CONFIG_STANDALONE 257      /* not available on the on-board variant */

enum {
    DEVICE_PCJR      = 2,          /* requires an IBM PCjr */
    DEVICE_XTKBC     = 4,          /* requires an XT-compatible keyboard controller */
    DEVICE_AT        = 8,          /* requires an AT-compatible system */
    DEVICE_ATKBC     = 0x10,       /* requires an AT-compatible keyboard controller */
    DEVICE_PS2       = 0x20,       /* requires a PS/1 or PS/2 system */
    DEVICE_ISA       = 0x40,       /* requires the ISA bus */
    DEVICE_CBUS      = 0x80,       /* requires the C-BUS bus */
    DEVICE_PCMCIA    = 0x100,      /* requires the PCMCIA bus */
    DEVICE_MCA       = 0x200,      /* requires the MCA bus */
    DEVICE_HIL       = 0x400,      /* requires the HP HIL bus */
    DEVICE_EISA      = 0x800,      /* requires the EISA bus */
    DEVICE_AT32      = 0x1000,     /* requires the Mylex AT/32 local bus */
    DEVICE_OLB       = 0x2000,     /* requires the OPTi local bus */
    DEVICE_VLB       = 0x4000,     /* requires the VLB bus */
    DEVICE_PCI       = 0x8000,     /* requires the PCI bus */
    DEVICE_CARDBUS   = 0x10000,    /* requires the CardBus bus */
    DEVICE_USB       = 0x20000,    /* requires the USB bus */
    DEVICE_AGP       = 0x40000,    /* requires the AGP bus */
    DEVICE_AC97      = 0x80000,    /* requires the AC'97 bus */
    DEVICE_COM       = 0x100000,   /* requires a serial port */
    DEVICE_LPT       = 0x200000,   /* requires a parallel port */
    DEVICE_KBC       = 0x400000,   /* is a keyboard controller */
    DEVICE_SOFTRESET = 0x800000,   /* requires to be reset on soft reset */

    DEVICE_ONBOARD   = 0x40000000, /* is on-board */
    DEVICE_PIT       = 0x80000000, /* device is a PIT */

    DEVICE_ALL       = 0xffffffff  /* match all devices */
};

#define BIOS_NORMAL                      0
#define BIOS_INTERLEAVED                 1
#define BIOS_INTERLEAVED_SINGLEFILE      2
#define BIOS_INTERLEAVED_QUAD            3
#define BIOS_INTERLEAVED_QUAD_SINGLEFILE 4
#define BIOS_INTEL_AMI                   5
#define BIOS_INTERLEAVED_INVERT          8
#define BIOS_HIGH_BIT_INVERT             16

typedef struct device_config_selection_t {
    const char *description;
    int         value;
} device_config_selection_t;

typedef struct device_config_spinner_t {
    int16_t min;
    int16_t max;
    int16_t step;
} device_config_spinner_t;

typedef struct device_config_bios_t {
    const char *name;
    const char *internal_name;
    int         bios_type;
    int         files_no;
    uint32_t    local;
    uint32_t    size;
    void       *dev1;
    void       *dev2;
    const char *files[9];
} device_config_bios_t;

typedef struct _device_config_ {
    const char                      *name;
    const char                      *description;
    int                              type;
    const char                      *default_string;
    int                              default_int;
    const char                      *file_filter;
    const device_config_spinner_t    spinner;
    const device_config_selection_t  selection[32];
    const device_config_bios_t       bios[32];
} device_config_t;

typedef struct _device_ {
    const char *name;
    const char *internal_name;
    uint32_t    flags; /* system flags */
    uintptr_t   local; /* flags local to device */

    union {
        void *(*init)(const struct _device_ *);
        void *(*init_ext)(const struct _device_ *, void*);
    };
    void (*close)(void *priv);
    void (*reset)(void *priv);
    int  (*available)(void);
    void (*speed_changed)(void *priv);
    void (*force_redraw)(void *priv);

    const device_config_t *config;
} device_t;

typedef struct device_context_t {
    const device_t *dev;
    char            name[2048];
    int             instance;
} device_context_t;

#ifdef __cplusplus
extern "C" {
#endif

extern void  device_init(void);
extern void  device_set_context(device_context_t *ctx, const device_t *dev, int inst);
extern void  device_context(const device_t *dev);
extern void  device_context_inst(const device_t *dev, int inst);
extern void  device_context_restore(void);
extern void *device_add(const device_t *dev);
extern void *device_add_linked(const device_t *dev, void *priv);
extern void *device_add_params(const device_t *dev, void *params);
extern void  device_add_ex(const device_t *dev, void *priv);
extern void  device_add_ex_params(const device_t *dev, void *priv, void *params);
extern void *device_add_inst(const device_t *dev, int inst);
extern void *device_add_inst_params(const device_t *dev, int inst, void *params);
extern void  device_add_inst_ex(const device_t *dev, void *priv, int inst);
extern void  device_add_inst_ex_params(const device_t *dev, void *priv, int inst, void *params);
extern void *device_get_common_priv(void);
extern void  device_close_all(void);
extern void  device_reset_all(uint32_t match_flags);
extern void *device_find_first_priv(uint32_t match_flags);
extern void *device_get_priv(const device_t *dev);
extern int   device_available(const device_t *dev);
extern void  device_speed_changed(void);
extern void  device_force_redraw(void);
extern void  device_get_name(const device_t *dev, int bus, char *name);
extern int   device_has_config(const device_t *dev);
extern const char *device_get_bios_file(const device_t *dev, const char *internal_name, int file_no);

extern int device_is_valid(const device_t *, int mch);

extern const device_t* device_context_get_device(void);

extern int         device_get_config_int(const char *name);
extern int         device_get_config_int_ex(const char *str, int def);
extern int         device_get_config_hex16(const char *name);
extern int         device_get_config_hex20(const char *name);
extern int         device_get_config_mac(const char *name, int def);
extern void        device_set_config_int(const char *str, int val);
extern void        device_set_config_hex16(const char *str, int val);
extern void        device_set_config_hex20(const char *str, int val);
extern void        device_set_config_mac(const char *str, int val);
extern const char *device_get_config_string(const char *name);
extern int         device_get_instance(void);
#define device_get_config_bios device_get_config_string

extern const char *device_get_internal_name(const device_t *dev);

extern int         machine_get_config_int(char *str);
extern const char *machine_get_config_string(char *str);

extern const device_t device_none;
extern const device_t device_internal;

#ifdef __cplusplus
}
#endif

#endif /*EMU_DEVICE_H*/
