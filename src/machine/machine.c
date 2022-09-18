/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Handling of the emulated machines.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2020 Sarah Walker.
 *		Copyright 2016-2020 Miran Grca.
 *		Copyright 2017-2020 Fred N. van Kempen.
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
        machine_log("Initializing as \"%s\"\n", machine_getname());

        is_vpc                   = 0;
        standalone_gameport_type = NULL;
        gameport_instance_id     = 0;

        /* Set up the architecture flags. */
        // AT = IS_AT(machine);
        // PCI = IS_ARCH(machine, MACHINE_BUS_PCI);

        cpu_set();
        pc_speed_changed();

        /* Reset the memory state. */
        mem_reset();
        smbase = is_am486dxl ? 0x00060000 : 0x00030000;

        lpt_init();

        if (cassette_enable)
            device_add(&cassette_device);

        cart_reset();

        /* Prepare some video-related things if we're using internal
           or no video. */
        video_pre_reset(gfxcard);

        /* Reset any ISA memory cards. */
        isamem_reset();

        /* Reset the fast off stuff. */
        cpu_fast_off_reset();
    }

    /* All good, boot the machine! */
    if (machines[m].init)
        ret = machines[m].init(&machines[m]);

    if (bios_only || !ret)
        return ret;

    if (gfxcard != VID_NONE) {
        if (ibm8514_enabled) {
            ibm8514_device_add();
        }
        if (xga_enabled)
            xga_device_add();
    }

    /* Reset the graphics card (or do nothing if it was already done
       by the machine's init function). */
    video_reset(gfxcard);

    return ret;
}

void
machine_init(void)
{
    bios_only = 0;
    (void) machine_init_ex(machine);
}

int
machine_available(int m)
{
    int       ret;
    device_t *d = (device_t *) machine_getdevice(m);

    bios_only = 1;

    ret = device_available(d);
    /* Do not check via machine_init_ex() if the device is not NULL and
       it has a CONFIG_BIOS field. */
    if ((d == NULL) || (ret != -1))
        ret = machine_init_ex(m);

    bios_only = 0;

    return !!ret;
}

void
pit_irq0_timer(int new_out, int old_out)
{
    if (new_out && !old_out)
        picint(1);

    if (!new_out)
        picintc(1);
}

void
machine_common_init(const machine_t *model)
{
    /* System devices first. */
    pic_init();
    dma_init();

    int pit_type = IS_AT(machine) ? PIT_8254 : PIT_8253;
    /* Select fast PIT if needed */
    if ((pit_mode == -1 && is486) || pit_mode == 1)
        pit_type += 2;

    pit_common_init(pit_type, pit_irq0_timer, NULL);
}
