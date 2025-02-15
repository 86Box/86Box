#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/dma.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/nvr.h>
#include <86box/keyboard.h>
#include <86box/lpt.h>
#include <86box/port_6x.h>
#include <86box/port_92.h>
#include <86box/serial.h>
#include <86box/hdc.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/video.h>
#include <86box/machine.h>

typedef struct {
    int model;
    int cpu_type;

    uint8_t ps2_91,
        ps2_92,
        ps2_94,
        ps2_102,
        ps2_103,
        ps2_104,
        ps2_105,
        ps2_190;

    serial_t *uart;
} ps2_isa_t;

static void
ps2_write(uint16_t port, uint8_t val, void *priv)
{
    ps2_isa_t *ps2 = (ps2_isa_t *) priv;

    switch (port) {
        case 0x0094:
            ps2->ps2_94 = val;
            break;

        case 0x0102:
            if (!(ps2->ps2_94 & 0x80)) {
                lpt1_remove();
                serial_remove(ps2->uart);
                if (val & 0x04) {
                    if (val & 0x08)
                        serial_setup(ps2->uart, COM1_ADDR, COM1_IRQ);
                    else
                        serial_setup(ps2->uart, COM2_ADDR, COM2_IRQ);
                }
                if (val & 0x10) {
                    switch ((val >> 5) & 3) {
                        case 0:
                            lpt1_setup(LPT_MDA_ADDR);
                            break;
                        case 1:
                            lpt1_setup(LPT1_ADDR);
                            break;
                        case 2:
                            lpt1_setup(LPT2_ADDR);
                            break;

                        default:
                            break;
                    }
                }
                ps2->ps2_102 = val;
            }
            break;

        case 0x0103:
            ps2->ps2_103 = val;
            break;

        case 0x0104:
            ps2->ps2_104 = val;
            break;

        case 0x0105:
            ps2->ps2_105 = val;
            break;

        case 0x0190:
            ps2->ps2_190 = val;
            break;

        default:
            break;
    }
}

static uint8_t
ps2_read(uint16_t port, void *priv)
{
    ps2_isa_t *ps2  = (ps2_isa_t *) priv;
    uint8_t    temp = 0xff;

    switch (port) {
        case 0x0091:
            temp        = ps2->ps2_91;
            ps2->ps2_91 = 0;
            break;

        case 0x0094:
            temp = ps2->ps2_94;
            break;

        case 0x0102:
            temp = ps2->ps2_102 | 0x08;
            break;

        case 0x0103:
            temp = ps2->ps2_103;
            break;

        case 0x0104:
            temp = ps2->ps2_104;
            break;

        case 0x0105:
            temp = ps2->ps2_105;
            break;

        case 0x0190:
            temp = ps2->ps2_190;
            break;

        default:
            break;
    }

    return temp;
}

static void
ps2_isa_setup(int model, int cpu_type)
{
    ps2_isa_t *ps2;
    void      *priv;

    ps2 = (ps2_isa_t *) calloc(1, sizeof(ps2_isa_t));
    ps2->model    = model;
    ps2->cpu_type = cpu_type;

    io_sethandler(0x0091, 1,
                  ps2_read, NULL, NULL, ps2_write, NULL, NULL, ps2);
    io_sethandler(0x0094, 1,
                  ps2_read, NULL, NULL, ps2_write, NULL, NULL, ps2);
    io_sethandler(0x0102, 4,
                  ps2_read, NULL, NULL, ps2_write, NULL, NULL, ps2);
    io_sethandler(0x0190, 1,
                  ps2_read, NULL, NULL, ps2_write, NULL, NULL, ps2);

    ps2->uart = device_add_inst(&ns16450_device, 1);

    lpt1_remove();
    lpt1_setup(LPT_MDA_ADDR);

    device_add(&port_92_device);

    mem_remap_top(384);

    device_add(&ps_nvr_device);

    device_add(&fdc_ps2_device);

    /* Enable the builtin HDC. */
    if (hdc_current[0] == HDC_INTERNAL) {
        priv = device_add(&ps1_hdc_device);
        ps1_hdc_inform(priv, &ps2->ps2_91);
    }

    device_add(&ps1vga_device);
}

static void
ps2_isa_common_init(const machine_t *model)
{
    machine_common_init(model);

    refresh_at_enable = 1;
    pit_devs[0].set_out_func(pit_devs[0].data, 1, pit_refresh_timer_at);

    dma16_init();
    pic2_init();

    device_add(&keyboard_ps2_device);
    device_add(&port_6x_ps2_device);
}

int
machine_ps2_m30_286_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ibmps2_m30_286/33f5381a.bin",
                           0x000e0000, 131072, 0);

    if (bios_only || !ret)
        return ret;

    ps2_isa_common_init(model);

    ps2_isa_setup(30, 286);

    return ret;
}
