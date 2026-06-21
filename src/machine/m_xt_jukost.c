/*
 * 86Box
 *
 * Juko ST motherboard implementation
 *
 * The Juko ST board has a proprietary memory switch controlled through
 * I/O port E0h. Software such as JUKO CDISK/EMS drivers uses:
 *
 *   OUT E0h, 0  -> normal conventional RAM mapping
 *   OUT E0h, 1  -> map hidden onboard RAM 640 KiB..1024 KiB
 *                  into the CPU address window 128 KiB..512 KiB
 *
 * Additionaly implemented turbo support (based on TURBO.COM and BIOS
 * disassembly, uses port 0x90) and latch for the wait states port,
 * documented in the user manual.
 *
 * Author: Oleg Farenyuk, <indrekis@gmail.com>, 2026
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

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/nmi.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/nvr.h>
#include <86box/keyboard.h>
#include <86box/lpt.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/gameport.h>
#include <86box/hdc.h>
#include <86box/video.h>
#include <86box/plat.h>
#include <86box/machine.h>
#include <86box/m_xt_jukost.h>

#define JUKO_ST_WINDOW_BASE      0x20000u
#define JUKO_ST_WINDOW_SIZE      0x60000u

#define JUKO_ST_HIDDEN_BASE      0xa0000u
#define JUKO_ST_HIDDEN_END       0x100000u

#define JUKO_CPU_477_SPEED       4772728
#define JUKO_CPU_120_SPEED       12000000

typedef struct juko_st_t {
    /* 0xE0 port: 0 -- default mapping, 1 -- alternate mapping. Reads as 0xFF */
    uint8_t         e0_latch;
    /* 0x90 port. BIOS reads it before writing the new value, but do not use the result:
     * 0 -- use jumper position
     * 2 -- Turbo on
     * 3 -- Turbo off
     */
    uint8_t         turbo_latch;
    /* 0x70 port.
     * 0 -- Board ROM wait state (0 -- 0, 1 -- 1)
     * 1 -- Board RAM wait state (0 -- 0, 1 -- 1)
     * 2-3 -- Board I/O wait states, (0-3 -- 00-11)
     * 4-5 -- Slot RAM wait states, (0-3 -- 00-11)
     * 6-7 -- Slot I/O wait states, (0-3 -- 00-11)
     * Note: wait states not yet implemented -- only reading-writing the port.
     */
    uint8_t         ws_latch;

    int cpu_477_index;
    int cpu_120_index;

    mem_mapping_t   window_mapping;
} juko_st_t;

#ifdef ENABLE_JUKO_ST_LOG
int juko_st_do_log = ENABLE_JUKO_ST_LOG;

static void
juko_st_log(const char *fmt, ...)
{
    va_list ap;

    if (juko_st_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
# define juko_st_log(fmt, ...)
#endif

static int
juko_st_find_cpu_by_speed(int speed)
{
    int i = 0;

    while (cpu_f->cpus[i].cpu_type) {
        if (cpu_is_eligible(cpu_f, i, machine) &&
            cpu_f->cpus[i].rspeed == speed)
            return i;

        i++;
    }

    return -1;
}

static inline int
juko_st_alt_mapping_enabled(const juko_st_t *dev)
{
    return (dev->e0_latch & 0x01) != 0;
}

static inline uint32_t
juko_st_translate(uint32_t addr, const juko_st_t *dev)
{
    return JUKO_ST_HIDDEN_BASE + (addr - JUKO_ST_WINDOW_BASE);
}

static inline int
juko_st_ram_present(uint32_t phys)
{
    return phys < (mem_size << 10);
}

static uint8_t
juko_st_readb(uint32_t addr, void *priv)
{
    const juko_st_t *dev  = (const juko_st_t *) priv;
    const uint32_t   phys = juko_st_translate(addr, dev);

    if (!juko_st_ram_present(phys))
        return 0xFF;

    return ram[phys];
}

static uint16_t
juko_st_readw(uint32_t addr, void *priv)
{
    uint16_t ret;

    ret  = (uint16_t) juko_st_readb(addr,     priv);
    ret |= (uint16_t) juko_st_readb(addr + 1, priv) << 8;

    return ret;
}

static uint32_t
juko_st_readl(uint32_t addr, void *priv)
{
    uint32_t ret;

    ret  = (uint32_t) juko_st_readb(addr,     priv);
    ret |= (uint32_t) juko_st_readb(addr + 1, priv) << 8;
    ret |= (uint32_t) juko_st_readb(addr + 2, priv) << 16;
    ret |= (uint32_t) juko_st_readb(addr + 3, priv) << 24;

    return ret;
}

static void
juko_st_writeb(uint32_t addr, uint8_t val, void *priv)
{
    const juko_st_t *dev  = (const juko_st_t *) priv;
    const uint32_t   phys = juko_st_translate(addr, dev);

    if (!juko_st_ram_present(phys))
        return;

    ram[phys] = val;
}

static void
juko_st_writew(uint32_t addr, uint16_t val, void *priv)
{
    juko_st_writeb(addr,     val & 0xff, priv);
    juko_st_writeb(addr + 1, val >> 8,   priv);
}

static void
juko_st_writel(uint32_t addr, uint32_t val, void *priv)
{
    juko_st_writeb(addr,     val & 0xff,         priv);
    juko_st_writeb(addr + 1, (val >> 8)  & 0xff, priv);
    juko_st_writeb(addr + 2, (val >> 16) & 0xff, priv);
    juko_st_writeb(addr + 3, (val >> 24) & 0xff, priv);
}

static void
juko_st_update_mapping(juko_st_t *dev)
{
    juko_st_log("Juko ST: E0h=%02x, %s mapping\n",
                dev->e0_latch,
                juko_st_alt_mapping_enabled(dev) ? "alternate" : "normal");
    if (juko_st_alt_mapping_enabled(dev)) {
        mem_mapping_set_exec(&dev->window_mapping, ram + JUKO_ST_HIDDEN_BASE);
        mem_mapping_enable(&dev->window_mapping);
    } else {
        mem_mapping_disable(&dev->window_mapping);
    }

    flushmmucache();
}

static uint8_t
juko_st_read(uint16_t port, void *priv)
{
    const juko_st_t *dev = (const juko_st_t *) priv;
    switch (port) {
        case 0x70:
            return dev->ws_latch;

        case 0x90:
            return dev->turbo_latch;

        case 0xE0:
            return 0xFF; /* Not the dev->e0_latch -- hardware always returns 0xFF */

        default:
            return 0xFF;
    }
}

static void
juko_st_set_turbo(juko_st_t *dev, uint8_t val)
{
    switch (val) {
        case 0x00:
            /* Follow hardware switch. Set as always on for simplicity */
            if (dev->cpu_120_index >= 0)
                cpu_dynamic_switch(dev->cpu_120_index);
            break;

        case 0x02:
            /* Turbo ON: 12 MHz */
            if (dev->cpu_120_index >= 0)
                cpu_dynamic_switch(dev->cpu_120_index);
            else
                cpu_dynamic_switch(cpu);  /* fallback */
            break;

        case 0x03:
            /* Turbo OFF / normal: 4.77 MHz */
            if (dev->cpu_477_index >= 0)
                cpu_dynamic_switch(dev->cpu_477_index);
            else
                cpu_dynamic_switch(0);    /* fallback */
            break;

        default:
            juko_st_log("Juko ST: unknown OUT 90h value %02X\n", val);
            break;
    }
}

static void
juko_st_write(uint16_t port, uint8_t val, void *priv)
{
    juko_st_t *dev = (juko_st_t *) priv;
    uint16_t bda_mem_kb;

    bda_mem_kb  = (uint16_t) ram[0x413];
    bda_mem_kb |= (uint16_t) ram[0x414] << 8;

    juko_st_log("Juko ST: [413h] %04X (%u KB)\n",
                (unsigned) bda_mem_kb,
                (unsigned) bda_mem_kb);

    switch (port) {
        case 0x70:
            dev->ws_latch = val;

            break;

        case 0x90:
            if (dev->turbo_latch == val)
                return;

            dev->turbo_latch = val;

            juko_st_set_turbo(dev, val);

            break;

        case 0xE0:
            if (dev->e0_latch == val)
                return;

            dev->e0_latch = val;
            juko_st_update_mapping(dev);
            break;

        default:
            juko_st_log("Juko ST: unexpected OUT %04Xh,%02X at CS:IP=%04X:%04X\n",
                         port, val, CS, cpu_state.pc);
            break;
    }
}

static void
juko_st_reset(void *priv)
{
    juko_st_t *dev = (juko_st_t *) priv;

    dev->e0_latch = 0xFE;
    juko_st_update_mapping(dev);
    dev->turbo_latch = 0x00;
    juko_st_set_turbo(dev, dev->turbo_latch);
    dev->ws_latch = 0xF5; /* Default value returned by hardware */
}

static void
juko_st_close(void *priv)
{
    juko_st_t *dev = (juko_st_t *) priv;

    free(dev);
}


int
machine_xt_jukopc_init(const machine_t *model)
{
    int         ret = 0;
    const char *fn;

    if (!device_available(model->device))
        return ret;

    device_context(model->device);

    fn  = device_get_bios_file(model->device, device_get_config_bios("bios"), 0);
    ret = bios_load_linear(fn, 0x000fe000, 8192, 0);

    device_context_restore();

    if (bios_only || !ret)
        return ret;

    device_add(&kbc_jukost_device);

    if ((fdc_current[0] == FDC_INTERNAL) || 0)
        device_add(&fdc_xt_device);

    machine_common_init(model);

    pit_devs[0].set_out_func(pit_devs[0].data, 1, pit_refresh_timer_xt);

    nmi_init();

    device_add(&jukopc_device);

    return ret;
}

static void *
juko_st_init(UNUSED(const device_t *info))
{
    juko_st_t *dev;

    dev = (juko_st_t *) calloc(1, sizeof(juko_st_t));
    if (dev == NULL)
        return NULL;

    dev->cpu_477_index = juko_st_find_cpu_by_speed(JUKO_CPU_477_SPEED);
    dev->cpu_120_index  = juko_st_find_cpu_by_speed(JUKO_CPU_120_SPEED);

    if (dev->cpu_477_index < 0)
        juko_st_log("Juko ST: 4.77 MHz CPU speed not available\n");

    if (dev->cpu_120_index < 0)
        juko_st_log("Juko ST: 12 MHz CPU speed not available\n");

    /*
     * One always-enabled overlay mapping.
     * Its callbacks implement both states:
     *   E0h bit 0 clear -> identity access to ram[addr]
     *   E0h bit 0 set   -> translated access to ram[A0000h + addr - 20000h]
     */
    mem_mapping_add(&dev->window_mapping,
                    JUKO_ST_WINDOW_BASE,
                    JUKO_ST_WINDOW_SIZE,
                    juko_st_readb,
                    juko_st_readw,
                    juko_st_readl,
                    juko_st_writeb,
                    juko_st_writew,
                    juko_st_writel,
                    ram + JUKO_ST_HIDDEN_BASE, /* Exec */
                    MEM_MAPPING_INTERNAL,
                    dev);
    mem_mapping_disable(&dev->window_mapping);

    io_sethandler(0xE0, 1,
                  juko_st_read, NULL, NULL,
                  juko_st_write, NULL, NULL,
                  dev);

    io_sethandler(0x70, 1,
              juko_st_read, NULL, NULL,
              juko_st_write, NULL, NULL,
              dev);

    io_sethandler(0x90, 1,
                  juko_st_read, NULL, NULL,
                  juko_st_write, NULL, NULL,
                  dev);

    juko_st_reset(dev);

    return dev;
}

static const device_config_t jukopc_config[] = {
    // clang-format off
    {
        .name           = "bios",
        .description    = "BIOS Version",
        .type           = CONFIG_BIOS,
        .default_string = "jukost",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            {
                .name          = "Bios 2.30",
                .internal_name = "jukost",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 8192,
                .files         = { "roms/machines/jukopc/000o001.bin", "" }
            },

            {
                .name          = "GLaBIOS 0.4.0 (8088)",
                .internal_name = "glabios_040_8088",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 8192,
                .files         = { "roms/machines/glabios/GLABIOS_0.4.0_8S.ROM", "" }
            },
            {
                .name          = "GLaBIOS 0.4.0 (V20)",
                .internal_name = "glabios_040_v20",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 8192,
                .files         = { "roms/machines/glabios/GLABIOS_0.4.0_VS.ROM", "" }
            },

            { .files_no = 0 }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t jukopc_device = {
    .name          = "Juko ST",
    .internal_name = "jukopc",
    .flags         = 0,
    .local         = 0,
    .init          = juko_st_init,
    .close         = juko_st_close,
    .reset         = juko_st_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = jukopc_config
};
