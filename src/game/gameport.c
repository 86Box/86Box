/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of a generic Game Port.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Sarah Walker, <https://pcem-emulator.co.uk/>
 *          RichardG, <richardg867@gmail.com>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2016-2022 Miran Grca.
 *          Copyright 2008-2018 Sarah Walker.
 *          Copyright 2021 RichardG.
 *          Copyright 2021-2025 Jasmine Iwanek.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/machine.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/isapnp.h>
#include <86box/gameport.h>
#include <86box/plat_unused.h>

device_t game_ports[GAMEPORT_MAX];

typedef struct {
    const device_t *device;
} GAMEPORT;

typedef struct g_axis_t {
    pc_timer_t                  timer;
    int                         axis_nr;
    struct _joystick_instance_ *joystick;
} g_axis_t;

typedef struct _gameport_ {
    uint16_t                    addr;
    uint8_t                     len;
    struct _joystick_instance_ *joystick;
    struct _gameport_          *next;
} gameport_t;

typedef struct _tmacm_ {
    struct gameport_t *port1;
    struct gameport_t *port2;
} tmacm_t;

typedef struct _joystick_instance_ {
    uint8_t  state;
    g_axis_t axis[4];

    const joystick_if_t *intf;
    void                *dat;
} joystick_instance_t;

int joystick_type = JS_TYPE_NONE;

static const joystick_if_t joystick_none = {
    .name          = "None",
    .internal_name = "none",
    .init          = NULL,
    .close         = NULL,
    .read          = NULL,
    .write         = NULL,
    .read_axis     = NULL,
    .a0_over       = NULL,
    .axis_count    = 0,
    .button_count  = 0,
    .pov_count     = 0,
    .max_joysticks = 0,
    .axis_names    = { NULL },
    .button_names  = { NULL },
    .pov_names     = { NULL }
};

static const struct {
    const joystick_if_t *joystick;
} joysticks[] = {
    { &joystick_none               },
    { &joystick_2axis_2button      },
    { &joystick_2axis_4button      },
    { &joystick_2axis_6button      },
    { &joystick_2axis_8button      },
    { &joystick_3axis_2button      },
    { &joystick_3axis_4button      },
    { &joystick_4axis_4button      },
    { &joystick_ch_flightstick_pro },
    { &joystick_sw_pad             },
    { &joystick_tm_fcs             },
    { NULL                         }
};

static joystick_instance_t *joystick_instance[GAMEPORT_MAX] = { NULL, NULL };

static uint8_t gameport_pnp_rom[] = {
    0x09, 0xf8, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,          /* BOX0002, dummy checksum (filled in by isapnp_add_card) */
    0x0a, 0x10, 0x10,                                              /* PnP version 1.0, vendor version 1.0 */
    0x82, 0x09, 0x00, 'G', 'a', 'm', 'e', ' ', 'P', 'o', 'r', 't', /* ANSI identifier */

    0x15, 0x09, 0xf8, 0x00, 0x02, 0x01,             /* logical device BOX0002, can participate in boot */
    0x1c, 0x41, 0xd0, 0xb0, 0x2f,                   /* compatible device PNPB02F */
    0x31, 0x00,                                     /* start dependent functions, preferred */
    0x47, 0x01, 0x00, 0x02, 0x00, 0x02, 0x08, 0x08, /* I/O 0x200, decodes 16-bit, 8-byte alignment, 8 addresses */
    0x30,                                           /* start dependent functions, acceptable */
    0x47, 0x01, 0x08, 0x02, 0x08, 0x02, 0x08, 0x08, /* I/O 0x208, decodes 16-bit, 8-byte alignment, 8 addresses */
    0x31, 0x02,                                     /* start dependent functions, sub-optimal */
    0x47, 0x01, 0x00, 0x01, 0xf8, 0xff, 0x08, 0x08, /* I/O 0x100-0xFFF8, decodes 16-bit, 8-byte alignment, 8 addresses */
    0x38,                                           /* end dependent functions */

    0x79, 0x00 /* end tag, dummy checksum (filled in by isapnp_add_card) */
};
static const isapnp_device_config_t gameport_pnp_defaults[] = {
    {.activate = 1,
        .io       = {
            { .base = 0x200 },
        }
    }
};

const device_t *standalone_gameport_type;
int             gameport_instance_id = 0;
/* Linked list of active game ports. Only the top port responds to reads
   or writes, and ports at the standard 200h location are prioritized. */
static gameport_t *active_gameports = NULL;

const char *
joystick_get_name(int js)
{
    if (!joysticks[js].joystick)
        return NULL;
    return joysticks[js].joystick->name;
}

const char *
joystick_get_internal_name(int js)
{
    if (joysticks[js].joystick == NULL)
        return "";

    return joysticks[js].joystick->internal_name;
}

int
joystick_get_from_internal_name(char *s)
{
    int c = 0;

    while (joysticks[c].joystick != NULL) {
        if (!strcmp(joysticks[c].joystick->internal_name, s))
            return c;
        c++;
    }

    return 0;
}

int
joystick_get_max_joysticks(int js)
{
    return joysticks[js].joystick->max_joysticks;
}

int
joystick_get_axis_count(int js)
{
    return joysticks[js].joystick->axis_count;
}

int
joystick_get_button_count(int js)
{
    return joysticks[js].joystick->button_count;
}

int
joystick_get_pov_count(int js)
{
    return joysticks[js].joystick->pov_count;
}

const char *
joystick_get_axis_name(int js, int id)
{
    return joysticks[js].joystick->axis_names[id];
}

const char *
joystick_get_button_name(int js, int id)
{
    return joysticks[js].joystick->button_names[id];
}

const char *
joystick_get_pov_name(int js, int id)
{
    return joysticks[js].joystick->pov_names[id];
}

static void
gameport_time(joystick_instance_t *joystick, int nr, int axis)
{
    if (axis == AXIS_NOT_PRESENT)
        timer_disable(&joystick->axis[nr].timer);
    else {
        /* Convert axis value to 555 timing. */
        axis += 32768;
        axis = (axis * 100) / 65; /* axis now in ohms */
        axis = (axis * 11) / 1000;
        timer_set_delay_u64(&joystick->axis[nr].timer, TIMER_USEC * (axis + 24)); /* max = 11.115 ms */
    }
}

static void
gameport_write(UNUSED(uint16_t addr), UNUSED(uint8_t val), void *priv)
{
    gameport_t          *dev      = (gameport_t *) priv;
    joystick_instance_t *joystick = dev->joystick;

    /* Respond only if a joystick is present and this port is at the top of the active ports list. */
    if (!joystick || (active_gameports != dev))
        return;

    /* Read all axes. */
    joystick->state |= 0x0f;

    gameport_time(joystick, 0, joystick->intf->read_axis(joystick->dat, 0));
    gameport_time(joystick, 1, joystick->intf->read_axis(joystick->dat, 1));
    gameport_time(joystick, 2, joystick->intf->read_axis(joystick->dat, 2));
    gameport_time(joystick, 3, joystick->intf->read_axis(joystick->dat, 3));

    /* Notify the interface. */
    joystick->intf->write(joystick->dat);

    cycles -= ISA_CYCLES(8);
}

static uint8_t
gameport_read(UNUSED(uint16_t addr), void *priv)
{
    gameport_t          *dev      = (gameport_t *) priv;
    joystick_instance_t *joystick = dev->joystick;

    /* Respond only if a joystick is present and this port is at the top of the active ports list. */
    if (!joystick || (active_gameports != dev))
        return 0xff;

    /* Merge axis state with button state. */
    uint8_t ret = joystick->state | joystick->intf->read(joystick->dat);

    cycles -= ISA_CYCLES(8);

    return ret;
}

static void
timer_over(void *priv)
{
    g_axis_t *axis = (g_axis_t *) priv;

    axis->joystick->state &= ~(1 << axis->axis_nr);

    /* Notify the joystick when the first axis' period is finished. */
    if (axis == &axis->joystick->axis[0])
        axis->joystick->intf->a0_over(axis->joystick->dat);
}

void
gameport_update_joystick_type(void)
{
    /* Add a standalone game port if a joystick is enabled but no other game ports exist. */
    if (standalone_gameport_type)
        gameport_add(standalone_gameport_type);

    /* Reset the joystick interface. */
    if (joystick_instance[0]) {
        joystick_instance[0]->intf->close(joystick_instance[0]->dat);
        joystick_instance[0]->intf = joysticks[joystick_type].joystick;
        joystick_instance[0]->dat  = joystick_instance[0]->intf->init();
    }
}

void
gameport_remap(void *priv, uint16_t address)
{
    gameport_t *dev = (gameport_t *) priv;
    gameport_t *other_dev;

    if (dev->addr) {
        /* Remove this port from the active ports list. */
        if (active_gameports == dev) {
            active_gameports = dev->next;
            dev->next        = NULL;
        } else {
            other_dev = active_gameports;
            while (other_dev) {
                if (other_dev->next == dev) {
                    other_dev->next = dev->next;
                    dev->next       = NULL;
                    break;
                }
                other_dev = other_dev->next;
            }
        }

        io_removehandler(dev->addr, dev->len,
                         gameport_read, NULL, NULL, gameport_write, NULL, NULL, dev);
    }

    dev->addr = address;

    if (dev->addr) {
        /* Add this port to the active ports list. */
        if (!active_gameports || ((dev->addr & 0xfff8) == 0x200)) {
            /* No ports have been added yet, or port within 200-207h: add to top. */
            dev->next        = active_gameports;
            active_gameports = dev;
        } else {
            /* Port at other addresses: add to bottom. */
            other_dev = active_gameports;
            while (other_dev->next)
                other_dev = other_dev->next;
            other_dev->next = dev;
        }

        io_sethandler(dev->addr, dev->len,
                      gameport_read, NULL, NULL, gameport_write, NULL, NULL, dev);
    }
}

static void
gameport_pnp_config_changed(uint8_t ld, isapnp_device_config_t *config, void *priv)
{
    if (ld > 0)
        return;

    gameport_t *dev = (gameport_t *) priv;

    /* Remap the game port to the specified address, or disable it. */
    gameport_remap(dev, (config->activate && (config->io[0].base != ISAPNP_IO_DISABLED)) ? config->io[0].base : 0);
}

void *
gameport_add(const device_t *gameport_type)
{
    /* Prevent a standalone game port from being added later on, unless this
       is an unused Super I/O game port (no MACHINE_GAMEPORT machine flag). */
    if (!(gameport_type->local & GAMEPORT_SIO) || machine_has_flags(machine, MACHINE_GAMEPORT))
        standalone_gameport_type = NULL;

    /* Add game port device. */
    return device_add_inst(gameport_type, gameport_instance_id++);
}

static void *
gameport_init(const device_t *info)
{
    gameport_t *dev = calloc(1, sizeof(gameport_t));

    /* Allocate global instance. */
    if (!joystick_instance[0] && joystick_type) {
        joystick_instance[0] = calloc(1, sizeof(joystick_instance_t));

        joystick_instance[0]->axis[0].joystick = joystick_instance[0];
        joystick_instance[0]->axis[1].joystick = joystick_instance[0];
        joystick_instance[0]->axis[2].joystick = joystick_instance[0];
        joystick_instance[0]->axis[3].joystick = joystick_instance[0];

        joystick_instance[0]->axis[0].axis_nr = 0;
        joystick_instance[0]->axis[1].axis_nr = 1;
        joystick_instance[0]->axis[2].axis_nr = 2;
        joystick_instance[0]->axis[3].axis_nr = 3;

        timer_add(&joystick_instance[0]->axis[0].timer, timer_over, &joystick_instance[0]->axis[0], 0);
        timer_add(&joystick_instance[0]->axis[1].timer, timer_over, &joystick_instance[0]->axis[1], 0);
        timer_add(&joystick_instance[0]->axis[2].timer, timer_over, &joystick_instance[0]->axis[2], 0);
        timer_add(&joystick_instance[0]->axis[3].timer, timer_over, &joystick_instance[0]->axis[3], 0);

        joystick_instance[0]->intf = joysticks[joystick_type].joystick;
        joystick_instance[0]->dat  = joystick_instance[0]->intf->init();
    }

    dev->joystick = joystick_instance[0];

    /* Map game port to the default address. Not applicable on PnP-only ports. */
    dev->len = (info->local >> 16) & 0xff;
    gameport_remap(dev, info->local & 0xffff);

    /* Register ISAPnP if this is a standard game port card. */
    if ((info->local & 0xffff) == 0x200)
        isapnp_set_device_defaults(isapnp_add_card(gameport_pnp_rom, sizeof(gameport_pnp_rom), gameport_pnp_config_changed, NULL, NULL, NULL, dev), 0, gameport_pnp_defaults);

    return dev;
}

static void *
tmacm_init(UNUSED(const device_t *info))
{
    uint16_t  port = 0x0000;
    tmacm_t  *dev  = NULL;

    dev = calloc(1, sizeof(tmacm_t));

    port = (uint16_t) device_get_config_hex16("port1_addr");
    switch (port) {
        case 0x201:
            dev->port1 = gameport_add(&gameport_201_device);
            break;
        case 0x203:
            dev->port1 = gameport_add(&gameport_203_device);
            break;
        case 0x205:
            dev->port1 = gameport_add(&gameport_205_device);
            break;
        case 0x207:
            dev->port1 = gameport_add(&gameport_207_device);
            break;
        default:
            break;
    }

    port = (uint16_t) device_get_config_hex16("port2_addr");
    switch (port) {
        case 0x209:
            dev->port2 = gameport_add(&gameport_209_device);
            break;
        case 0x20b:
            dev->port2 = gameport_add(&gameport_20b_device);
            break;
        case 0x20d:
            dev->port2 = gameport_add(&gameport_20d_device);
            break;
        case 0x20f:
            dev->port2 = gameport_add(&gameport_20f_device);
            break;
        default:
            break;
    }

    return dev;
}

static void
gameport_close(void *priv)
{
    gameport_t *dev = (gameport_t *) priv;

    /* If this port was active, remove it from the active ports list. */
    gameport_remap(dev, 0);

    /* Free the global instance here, if it wasn't already freed. */
    if (joystick_instance[0]) {
        joystick_instance[0]->intf->close(joystick_instance[0]->dat);

        free(joystick_instance[0]);
        joystick_instance[0] = NULL;
    }

    free(dev);
}

const device_t gameport_device = {
    .name          = "Game port",
    .internal_name = "gameport",
    .flags         = 0,
    .local         = GAMEPORT_8ADDR | 0x0200,
    .init          = gameport_init,
    .close         = gameport_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t gameport_201_device = {
    .name          = "Game port (Port 201h only)",
    .internal_name = "gameport_201",
    .flags         = 0,
    .local         = GAMEPORT_1ADDR | 0x0201,
    .init          = gameport_init,
    .close         = gameport_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t gameport_203_device = {
    .name          = "Game port (Port 203h only)",
    .internal_name = "gameport_203",
    .flags         = 0,
    .local         = GAMEPORT_1ADDR | 0x0203,
    .init          = gameport_init,
    .close         = gameport_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t gameport_205_device = {
    .name          = "Game port (Port 205h only)",
    .internal_name = "gameport_205",
    .flags         = 0,
    .local         = GAMEPORT_1ADDR | 0x0205,
    .init          = gameport_init,
    .close         = gameport_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t gameport_207_device = {
    .name          = "Game port (Port 207h only)",
    .internal_name = "gameport_207",
    .flags         = 0,
    .local         = GAMEPORT_1ADDR | 0x0207,
    .init          = gameport_init,
    .close         = gameport_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t gameport_208_device = {
    .name          = "Game port (Port 208h-20fh)",
    .internal_name = "gameport_208",
    .flags         = 0,
    .local         = GAMEPORT_8ADDR | 0x0208,
    .init          = gameport_init,
    .close         = gameport_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t gameport_209_device = {
    .name          = "Game port (Port 209h only)",
    .internal_name = "gameport_209",
    .flags         = 0,
    .local         = GAMEPORT_1ADDR | 0x0209,
    .init          = gameport_init,
    .close         = gameport_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t gameport_20b_device = {
    .name          = "Game port (Port 20Bh only)",
    .internal_name = "gameport_20b",
    .flags         = 0,
    .local         = GAMEPORT_1ADDR | 0x020B,
    .init          = gameport_init,
    .close         = gameport_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t gameport_20d_device = {
    .name          = "Game port (Port 20Dh only)",
    .internal_name = "gameport_20d",
    .flags         = 0,
    .local         = GAMEPORT_1ADDR | 0x020D,
    .init          = gameport_init,
    .close         = gameport_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t gameport_20f_device = {
    .name          = "Game port (Port 20Fh only)",
    .internal_name = "gameport_20f",
    .flags         = 0,
    .local         = GAMEPORT_1ADDR | 0x020F,
    .init          = gameport_init,
    .close         = gameport_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

static const device_config_t tmacm_config[] = {
  // clang-format off
    {
        .name           = "port1_addr",
        .description    = "Port 1 Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x0201,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "201h",     .value = 0x0201 },
            { .description = "203h",     .value = 0x0203 },
            { .description = "205h",     .value = 0x0205 },
            { .description = "207h",     .value = 0x0207 },
            { .description = "Disabled", .value = 0x0000 },
            { .description = ""                          }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "port2_addr",
        .description    = "Port 2 Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x0209,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "209h",     .value = 0x0209 },
            { .description = "20Bh",     .value = 0x020B },
            { .description = "20Dh",     .value = 0x020D },
            { .description = "20Fh",     .value = 0x020F },
            { .description = "Disabled", .value = 0x0000 },
            { .description = ""                          }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t gameport_tm_acm_device = {
    .name          = "Game port (ThrustMaster ACM)",
    .internal_name = "gameport_tmacm",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = tmacm_init,
    .close         = NULL,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = tmacm_config
};

const device_t gameport_pnp_device = {
    .name          = "Game port (Plug and Play only)",
    .internal_name = "gameport_pnp",
    .flags         = 0,
    .local         = GAMEPORT_8ADDR,
    .init          = gameport_init,
    .close         = gameport_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t gameport_pnp_1io_device = {
    .name          = "Game port (Plug and Play only, 1 I/O port)",
    .internal_name = "gameport_pnp_1io",
    .flags         = 0,
    .local         = GAMEPORT_1ADDR,
    .init          = gameport_init,
    .close         = gameport_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t gameport_pnp_6io_device = {
    .name          = "Game port (Plug and Play only, 6 I/O ports)",
    .internal_name = "gameport_pnp_6io",
    .flags         = 0,
    .local         = GAMEPORT_6ADDR,
    .init          = gameport_init,
    .close         = gameport_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t gameport_sio_device = {
    .name          = "Game port (Super I/O)",
    .internal_name = "gameport_sio",
    .flags         = 0,
    .local         = GAMEPORT_SIO | GAMEPORT_8ADDR,
    .init          = gameport_init,
    .close         = gameport_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t gameport_sio_1io_device = {
    .name          = "Game port (Super I/O, 1 I/O port)",
    .internal_name = "gameport_sio",
    .flags         = 0,
    .local         = GAMEPORT_SIO | GAMEPORT_1ADDR,
    .init          = gameport_init,
    .close         = gameport_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

static const GAMEPORT gameports[] = {
    { &device_none            },
    { &device_internal        },
    { &gameport_device        },
    { &gameport_208_device    },
    { &gameport_pnp_device    },
    { &gameport_tm_acm_device },
    { NULL                    }
    // clang-format on
};

/* UI */
int
gameport_available(int port)
{
    if (gameports[port].device)
        return (device_available(gameports[port].device));

    return 1;
}

/* UI */
const device_t *
gameports_getdevice(int port)
{
    return (gameports[port].device);
}

/* UI */
int
gameport_has_config(int port)
{
    if (!gameports[port].device)
        return 0;

    return (device_has_config(gameports[port].device) ? 1 : 0);
}

/* UI */
const char *
gameport_get_internal_name(int port)
{
    return device_get_internal_name(gameports[port].device);
}

/* UI */
int
gameport_get_from_internal_name(const char *str)
{
    int c = 0;

    while (gameports[c].device != NULL) {
        if (!strcmp(gameports[c].device->internal_name, str))
            return c;
        c++;
    }

    return 0;
}
