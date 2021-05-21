/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of a generic Game Port.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Sarah Walker, <tommowalker@tommowalker.co.uk>
 *		RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2021 RichardG.
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
#include <86box/joystick_ch_flightstick_pro.h>
#include <86box/joystick_standard.h>
#include <86box/joystick_sw_pad.h>
#include <86box/joystick_tm_fcs.h>


typedef struct {
    pc_timer_t	timer;
    int		axis_nr;
    struct _joystick_instance_ *joystick;
} g_axis_t;

typedef struct _gameport_ {
    uint16_t	addr;
    struct _joystick_instance_ *joystick;
    struct _gameport_ *next;
} gameport_t;

typedef struct _joystick_instance_ {
    uint8_t	state;
    g_axis_t	axis[4];

    const joystick_if_t *intf;
    void	*dat;
} joystick_instance_t;


int		joystick_type = 1;


static const joystick_if_t joystick_none = {
    "None",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    0,
    0,
    0
};


static const struct {
    const char		*internal_name;
    const joystick_if_t	*joystick;
} joysticks[] = {
    { "none",			&joystick_none			},
    { "standard_2button",	&joystick_standard		},
    { "standard_4button",	&joystick_standard_4button	},
    { "standard_6button",	&joystick_standard_6button	},
    { "standard_8button",	&joystick_standard_8button	},
    { "4axis_4button",		&joystick_4axis_4button		},
    { "ch_flighstick_pro",	&joystick_ch_flightstick_pro	},
    { "sidewinder_pad",		&joystick_sw_pad		},
    { "thrustmaster_fcs",	&joystick_tm_fcs		},
    { "",			NULL				}
};
static joystick_instance_t *joystick_instance = NULL;


static uint8_t gameport_pnp_rom[] = {
    0x09, 0xf8, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, /* BOX0002, dummy checksum (filled in by isapnp_add_card) */
    0x0a, 0x10, 0x10, /* PnP version 1.0, vendor version 1.0 */
    0x82, 0x09, 0x00, 'G', 'a', 'm', 'e', ' ', 'P', 'o', 'r', 't', /* ANSI identifier */

    0x15, 0x09, 0xf8, 0x00, 0x02, 0x01, /* logical device BOX0002, can participate in boot */
	0x1c, 0x41, 0xd0, 0xb0, 0x2f, /* compatible device PNPB02F */
	0x31, 0x00, /* start dependent functions, preferred */
		0x47, 0x01, 0x00, 0x02, 0x00, 0x02, 0x08, 0x08, /* I/O 0x200, decodes 16-bit, 8-byte alignment, 8 addresses */
	0x30, /* start dependent functions, acceptable */
		0x47, 0x01, 0x08, 0x02, 0x08, 0x02, 0x08, 0x08, /* I/O 0x208, decodes 16-bit, 8-byte alignment, 8 addresses */
	0x31, 0x02, /* start dependent functions, sub-optimal */
		0x47, 0x01, 0x00, 0x01, 0xf8, 0xff, 0x08, 0x08, /* I/O 0x100-0xFFF8, decodes 16-bit, 8-byte alignment, 8 addresses */
	0x38, /* end dependent functions */

    0x79, 0x00 /* end tag, dummy checksum (filled in by isapnp_add_card) */
};
static const isapnp_device_config_t gameport_pnp_defaults[] = {
    {
	.activate = 1,
	.io = { { .base = 0x200 }, }
    }
};


const device_t		*standalone_gameport_type;
static int		gameport_instance_id = 0;
/* Linked list of active game ports. Only the top port responds to reads
   or writes, and ports at the standard 200h location are prioritized. */
static gameport_t	*active_gameports = NULL;


char *
joystick_get_name(int js)
{
    if (!joysticks[js].joystick)
	return NULL;
    return (char *) joysticks[js].joystick->name;
}


char *
joystick_get_internal_name(int js)
{
    return (char *) joysticks[js].internal_name;
}


int
joystick_get_from_internal_name(char *s)
{
    int c = 0;

    while (strlen((char *) joysticks[c].internal_name)) {
	if (!strcmp((char *) joysticks[c].internal_name, s))
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


char *
joystick_get_axis_name(int js, int id)
{
    return (char *) joysticks[js].joystick->axis_names[id];
}


char *
joystick_get_button_name(int js, int id)
{
    return (char *) joysticks[js].joystick->button_names[id];
}


char *
joystick_get_pov_name(int js, int id)
{
    return (char *) joysticks[js].joystick->pov_names[id];
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
gameport_write(uint16_t addr, uint8_t val, void *priv)
{
    gameport_t *dev = (gameport_t *) priv;
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
gameport_read(uint16_t addr, void *priv)
{
    gameport_t *dev = (gameport_t *) priv;
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
    if (joystick_instance) {
	joystick_instance->intf->close(joystick_instance->dat);
	joystick_instance->intf = joysticks[joystick_type].joystick;
	joystick_instance->dat = joystick_instance->intf->init();
    }
}


void
gameport_remap(void *priv, uint16_t address)
{
    gameport_t *dev = (gameport_t *) priv, *other_dev;

    if (dev->addr) {
	/* Remove this port from the active ports list. */
	if (active_gameports == dev) {
		active_gameports = dev->next;
		dev->next = NULL;
	} else {
		other_dev = active_gameports;
		while (other_dev) {
			if (other_dev->next == dev) {
				other_dev->next = dev->next;
				dev->next = NULL;
				break;
			}
			other_dev = other_dev->next;
		}
	}

	io_removehandler(dev->addr, (dev->addr & 1) ? 1 : 8,
			 gameport_read, NULL, NULL, gameport_write, NULL, NULL, dev);
    }

    dev->addr = address;

    if (dev->addr) {
	/* Add this port to the active ports list. */
	if ((dev->addr & 0xfff8) == 0x200) {
		/* Port within 200-207h: add to top. */
		dev->next = active_gameports;
		active_gameports = dev;
	} else {
		/* Port at other addresses: add to bottom. */
		other_dev = active_gameports;
		while (other_dev->next)
			other_dev = other_dev->next;
		other_dev->next = dev;
	}

	io_sethandler(dev->addr, (dev->addr & 1) ? 1 : 8,
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
    /* Prevent a standalone game port from being added later on. */
    standalone_gameport_type = NULL;

    /* Add game port device. */
    return device_add_inst(gameport_type, gameport_instance_id++);
}


static void *
gameport_init(const device_t *info)
{
    gameport_t *dev = NULL;

    dev = malloc(sizeof(gameport_t));
    memset(dev, 0x00, sizeof(gameport_t));

    /* Allocate global instance. */
    if (!joystick_instance && joystick_type) {
	joystick_instance = malloc(sizeof(joystick_instance_t));
	memset(joystick_instance, 0x00, sizeof(joystick_instance_t));

	joystick_instance->axis[0].joystick = joystick_instance;
	joystick_instance->axis[1].joystick = joystick_instance;
	joystick_instance->axis[2].joystick = joystick_instance;
	joystick_instance->axis[3].joystick = joystick_instance;

	joystick_instance->axis[0].axis_nr = 0;
	joystick_instance->axis[1].axis_nr = 1;
	joystick_instance->axis[2].axis_nr = 2;
	joystick_instance->axis[3].axis_nr = 3;

	timer_add(&joystick_instance->axis[0].timer, timer_over, &joystick_instance->axis[0], 0);
	timer_add(&joystick_instance->axis[1].timer, timer_over, &joystick_instance->axis[1], 0);
	timer_add(&joystick_instance->axis[2].timer, timer_over, &joystick_instance->axis[2], 0);
	timer_add(&joystick_instance->axis[3].timer, timer_over, &joystick_instance->axis[3], 0);

	joystick_instance->intf = joysticks[joystick_type].joystick;
	joystick_instance->dat = joystick_instance->intf->init();
    }

    dev->joystick = joystick_instance;

    /* Map game port to the default address. Not applicable on PnP-only ports. */
    gameport_remap(dev, info->local);

    /* Register ISAPnP if this is a standard game port card. */
    if (info->local == 0x200)
	isapnp_set_device_defaults(isapnp_add_card(gameport_pnp_rom, sizeof(gameport_pnp_rom), gameport_pnp_config_changed, NULL, NULL, NULL, dev), 0, gameport_pnp_defaults);

    return dev;
}


static void
gameport_close(void *priv)
{
    gameport_t *dev = (gameport_t *) priv;

    /* If this port was active, remove it from the active ports list. */
    gameport_remap(dev, 0);

    /* Free the global instance here, if it wasn't already freed. */
    if (joystick_instance) {
	joystick_instance->intf->close(joystick_instance->dat);

	free(joystick_instance);
	joystick_instance = NULL;
    }

    free(dev);
}


const device_t gameport_device = {
    "Game port",
    0, 0x200,
    gameport_init,
    gameport_close,
    NULL, { NULL }, NULL,
    NULL
};

const device_t gameport_201_device = {
    "Game port (port 201h only)",
    0, 0x201,
    gameport_init,
    gameport_close,
    NULL, { NULL }, NULL,
    NULL
};

const device_t gameport_pnp_device = {
    "Game port (Plug and Play only)",
    0, 0,
    gameport_init,
    gameport_close,
    NULL, { NULL }, NULL,
    NULL
};
