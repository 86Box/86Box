/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the Philips XT-compatible machines.
 *
 *
 *
 * Authors:	EngiNerd <webmaster.crrc@yahoo.it>
 *
 *		Copyright 2020-2021 EngiNerd.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/nmi.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/hdc.h>
#include <86box/gameport.h>
#include <86box/ibm_5161.h>
#include <86box/keyboard.h>
#include <86box/rom.h>
#include <86box/machine.h>
#include <86box/chipset.h>
#include <86box/io.h>
#include <86box/video.h>


typedef struct
{
    uint8_t	reg;
} philips_t;


#ifdef ENABLE_PHILIPS_LOG
int philips_do_log = ENABLE_PHILIPS_LOG;
static void
philips_log(const char *fmt, ...)
{
    va_list ap;

    if (philips_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
    	va_end(ap);
    }
}
#else
#define philips_log(fmt, ...)
#endif

static void
philips_write(uint16_t port, uint8_t val, void *priv)
{
    philips_t *dev = (philips_t *) priv;

    switch (port) {
	/* port 0xc0
	 * bit 7: turbo
	 * bits 4-5: rtc read/set (I2C Bus SDA/SCL?)
	 * bit 2: parity disabled
	 */
	case 0xc0:
		dev->reg = val;
		if (val & 0x80)
			cpu_dynamic_switch(cpu);
		else
			cpu_dynamic_switch(0);
		break;
    }

    philips_log("Philips XT Mainboard: Write %02x at %02x\n", val, port);

}

static uint8_t
philips_read(uint16_t port, void *priv)
{
    philips_t *dev = (philips_t *) priv;
    uint8_t ret = 0xff;

    switch (port) {
	/* port 0xc0
	 * bit 7: turbo
	 * bits 4-5: rtc read/set
	 * bit 2: parity disabled
	 */
	case 0xc0:
		ret = dev->reg;
		break;
    }

    philips_log("Philips XT Mainboard: Read %02x at %02x\n", ret, port);

    return ret;
}


static void
philips_close(void *priv)
{
    philips_t *dev = (philips_t *) priv;

    free(dev);
}

static void *
philips_init(const device_t *info)
{
    philips_t *dev = (philips_t *) malloc(sizeof(philips_t));
    memset(dev, 0, sizeof(philips_t));

    dev->reg = 0x40;

    io_sethandler(0x0c0, 0x01, philips_read, NULL, NULL, philips_write, NULL, NULL, dev);

    return dev;
}

const device_t philips_device = {
    .name = "Philips XT Mainboard",
    .internal_name = "philips",
    .flags = 0,
    .local = 0,
    .init = philips_init,
    .close = philips_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};

void
machine_xt_philips_common_init(const machine_t *model)
{
    machine_common_init(model);

    pit_ctr_set_out_func(&pit->counters[1], pit_refresh_timer_xt);

    nmi_init();

    standalone_gameport_type = &gameport_device;

    device_add(&keyboard_pc_device);

    device_add(&philips_device);

    device_add(&xta_hd20_device);

}

int
machine_xt_p3105_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p3105/philipsnms9100.bin",
			   0x000fc000, 16384, 0);

    if (bios_only || !ret)
	return ret;

    machine_xt_philips_common_init(model);

    /* On-board FDC cannot be disabled */
	device_add(&fdc_xt_device);

    return ret;
}

int
machine_xt_p3120_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/p3120/philips_p3120.bin",
			   0x000f8000, 32768, 0);

    if (bios_only || !ret)
	return ret;

    machine_xt_philips_common_init(model);

    device_add(&gc100a_device);

    device_add(&fdc_at_device);

    return ret;
}
