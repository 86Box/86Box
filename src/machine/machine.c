/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Handling of the emulated machines.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2016-2020 Miran Grca.
 *          Copyright 2017-2020 Fred N. van Kempen.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/cassette.h>
#include <86box/cartridge.h>
#include <86box/dma.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/gameport.h>
#include "cpu.h"
#include <86box/video.h>
#include <86box/machine.h>
#include <86box/isamem.h>
#include <86box/isarom.h>
#include <86box/pci.h>
#include <86box/plat_unused.h>

int bios_only = 0;
int machine;
// int AT, PCI;

#ifdef ENABLE_MACHINE_LOG
int machine_do_log = ENABLE_MACHINE_LOG;

static void
machine_log(const char *fmt, ...)
{
    va_list ap;

    if (machine_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define machine_log(fmt, ...)
#endif

static int
machine_init_ex(int m)
{
    int ret = 0;

    if (!bios_only) {
        machine_log("Initializing as \"%s\"\n", machine_getname(machine));

        machine_init_p1();

        machine_init_gpio();
        machine_init_gpio_acpi();

        machine_snd = NULL;

        is_vpc = 0;

        standalone_gameport_type = NULL;
        gameport_instance_id     = 0;

        /* Set up the architecture flags. */
#if 0
        AT = IS_AT(machine);
        PCI = IS_ARCH(machine, MACHINE_BUS_PCI);
#endif

        cpu_set();
        pc_speed_changed();

        /* Reset the memory state. */
        mem_reset();
        smbase = is_am486dxl ? 0x00060000 : 0x00030000;

        if (cassette_enable)
            device_add(&cassette_device);

        cart_reset();

        /* Prepare some video-related things if we're using internal
           or no video. */
        video_pre_reset(gfxcard[0]);

        /* Reset any ISA memory cards. */
        isamem_reset();

#if 0
        /* Reset any ISA ROM cards. */
        isarom_reset();
#endif

        /* Reset the fast off stuff. */
        cpu_fast_off_reset();

        pci_flags = 0x00000000;

        if (machines[m].nvr_device)
            device_add_params(machines[m].nvr_device, (void *) (uintptr_t) machines[m].nvr_params);
    }

    is_pcjr = 0;

    /* All good, boot the machine! */
    if (machines[m].init)
        ret = machines[m].init(&machines[m]);

    if (bios_only || !ret)
        return ret;

    video_post_reset();

    return ret;
}

void
machine_init(void)
{
    bios_only = 0;

    machine_set_p1_default(machines[machine].kbc_p1);
    machine_set_ps2();

    (void) machine_init_ex(machine);
}

int
machine_available(int m)
{
    int             ret = 0;
    const device_t *dev = machine_get_device(m);

    if (dev != NULL)
        ret = machine_device_available(dev);
    /*
       Only via machine_init_ex() if the device is NULL or
       it lacks a CONFIG_BIOS field (or the CONFIG_BIOS field
       is not the first in list.
     */
    if (ret == 0) {
        bios_only = 1;

        ret = machine_init_ex(m);

        bios_only = 0;
    } else if (ret == -2)
        ret = 0;

    return !!ret;
}

void
pit_irq0_timer(int new_out, int old_out, UNUSED(void *priv))
{
    if (new_out && !old_out)
        picint(1);

    if (!new_out)
        picintc(1);
}

void
machine_common_init(UNUSED(const machine_t *model))
{
    uint8_t cpu_requires_fast_pit = is486 || (!is286 && is8086 && (cpu_s->rspeed >= 8000000));
    cpu_requires_fast_pit         = cpu_requires_fast_pit && !cpu_16bitbus;

    /* System devices first. */
    pic_init();
    dma_init();

    int pit_type = IS_AT(machine) ? PIT_8254 : PIT_8253;
    /* Select fast PIT if needed */
    if (((pit_mode == -1) && cpu_requires_fast_pit) || (pit_mode == 1))
        pit_type += 2;

    pit_common_init(pit_type, pit_irq0_timer, NULL);
}
