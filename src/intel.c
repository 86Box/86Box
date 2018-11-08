/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "cpu/cpu.h"
#include "device.h"
#include "machine/machine.h"
#include "io.h"
#include "mem.h"
#include "pit.h"
#include "timer.h"
#include "intel.h"


typedef struct {
    uint16_t timer_latch;

    int64_t timer;
} batman_t;


static uint8_t
batman_config_read(uint16_t port, void *priv)
{
    uint8_t ret = 0x00;

    switch (port & 0x000f) {
	case 3:
		ret = 0xff;
		break;
	case 5:
		ret = 0xdf;
		break;
    }

    return ret;
}


static void
batman_timer_over(void *priv)
{
    batman_t *dev = (batman_t *) priv;

    dev->timer = 0;
}


static void
batman_timer_write(uint16_t addr, uint8_t val, void *priv)
{
    batman_t *dev = (batman_t *) priv;

    if (addr & 1)
	dev->timer_latch = (dev->timer_latch & 0xff) | (val << 8);
    else
	dev->timer_latch = (dev->timer_latch & 0xff00) | val;

    dev->timer = dev->timer_latch * TIMER_USEC;
}


static uint8_t
batman_timer_read(uint16_t addr, void *priv)
{
    batman_t *dev = (batman_t *) priv;
    uint16_t batman_timer_latch;
    uint8_t ret;

    cycles -= (int)PITCONST;

    timer_clock();

    if (dev->timer < 0)
	return 0;

    batman_timer_latch = dev->timer / TIMER_USEC;

    if (addr & 1)
	ret = batman_timer_latch >> 8;
    else
	ret = batman_timer_latch & 0xff;

    return ret;
}


static void
intel_batman_close(void *priv)
{
    batman_t *dev = (batman_t *) priv;

    free(dev);
}


static void *
intel_batman_init(const device_t *info)
{
    batman_t *dev = (batman_t *) malloc(sizeof(batman_t));
    memset(dev, 0, sizeof(batman_t));

    io_sethandler(0x0073, 0x0001,
		  batman_config_read, NULL, NULL, NULL, NULL, NULL, dev);
    io_sethandler(0x0075, 0x0001,
		  batman_config_read, NULL, NULL, NULL, NULL, NULL, dev);

    io_sethandler(0x0078, 0x0002,
		  batman_timer_read, NULL, NULL, batman_timer_write, NULL, NULL, dev);

    timer_add(batman_timer_over, &dev->timer, &dev->timer, dev);

    return dev;
}


const device_t intel_batman_device = {
    "Intel Batman board chip",
    0,
    0,
    intel_batman_init, intel_batman_close, NULL,
    NULL, NULL, NULL,
    NULL
};
