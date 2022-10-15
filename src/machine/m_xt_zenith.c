/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of various Zenith PC compatible machines.
 *		Currently only the Zenith Data Systems Supersport is emulated.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		TheCollector1995, <mariogplayer@gmail.com>
 *      EngiNerd <webmaster.crrc@yahoo.it>
 *
 *		Copyright 2008-2019 Sarah Walker.
 *		Copyright 2016-2019 Miran Grca.
 *      Copyright 2020 EngiNerd.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/dma.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/gameport.h>
#include <86box/keyboard.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/machine.h>
#include <86box/io.h>
#include <86box/vid_cga.h>

typedef struct {
    mem_mapping_t scratchpad_mapping;
    uint8_t      *scratchpad_ram;
} zenith_t;

static uint8_t
zenith_scratchpad_read(uint32_t addr, void *p)
{
    zenith_t *dev = (zenith_t *) p;
    return dev->scratchpad_ram[addr & 0x3fff];
}

static void
zenith_scratchpad_write(uint32_t addr, uint8_t val, void *p)
{
    zenith_t *dev                      = (zenith_t *) p;
    dev->scratchpad_ram[addr & 0x3fff] = val;
}

static void *
zenith_scratchpad_init(const device_t *info)
{
    zenith_t *dev;

    dev = (zenith_t *) malloc(sizeof(zenith_t));
    memset(dev, 0x00, sizeof(zenith_t));

    dev->scratchpad_ram = malloc(0x4000);

    mem_mapping_add(&dev->scratchpad_mapping, 0xf0000, 0x4000,
                    zenith_scratchpad_read, NULL, NULL,
                    zenith_scratchpad_write, NULL, NULL,
                    dev->scratchpad_ram, MEM_MAPPING_EXTERNAL, dev);

    return dev;
}

static void
zenith_scratchpad_close(void *p)
{
    zenith_t *dev = (zenith_t *) p;

    free(dev->scratchpad_ram);
    free(dev);
}

static const device_t zenith_scratchpad_device = {
    .name          = "Zenith scratchpad RAM",
    .internal_name = "zenith_scratchpad",
    .flags         = 0,
    .local         = 0,
    .init          = zenith_scratchpad_init,
    .close         = zenith_scratchpad_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

void
machine_zenith_init(const machine_t *model)
{

    machine_common_init(model);

    if (fdc_type == FDC_INTERNAL)
        device_add(&fdc_xt_device);

    device_add(&zenith_scratchpad_device);

    pit_devs[0].set_out_func(pit_devs[0].data, 1, pit_refresh_timer_xt);

    device_add(&keyboard_xt_zenith_device);

    nmi_init();
}

/*
 * Current bugs and limitations:
 * - missing NVRAM implementation
 */
int
machine_xt_z184_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/zdsupers/z184m v3.1d.10d",
                           0x000f8000, 32768, 0);

    if (bios_only || !ret)
        return ret;

    machine_zenith_init(model);

    lpt1_remove(); /* only one parallel port */
    lpt2_remove();
    lpt1_init(0x278);
    device_add(&ns8250_device);
    serial_set_next_inst(SERIAL_MAX); /* So that serial_standalone_init() won't do anything. */

    device_add(&cga_device);

    return ret;
}

int
machine_xt_z151_init(const machine_t *model)
{
    int ret;
    ret = bios_load_linear("roms/machines/zdsz151/444-229-18.bin",
                           0x000fc000, 32768, 0);
    if (ret) {
        bios_load_aux_linear("roms/machines/zdsz151/444-260-18.bin",
                             0x000f8000, 16384, 0);
    }

    if (bios_only || !ret)
        return ret;

    machine_zenith_init(model);

    return ret;
}

/*
 * Current bugs and limitations:
 * - Memory board support for EMS currently missing
 */
int
machine_xt_z159_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/zdsz159/z159m v2.9e.10d",
                           0x000f8000, 32768, 0);

    if (bios_only || !ret)
        return ret;

    machine_zenith_init(model);

    /* parallel port is on the memory board */
    lpt1_remove(); /* only one parallel port */
    lpt2_remove();
    lpt1_init(0x278);

    return ret;
}
