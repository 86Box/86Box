// SPDX-License-Identifier: GPL-2.0-or-later
/* HP Vectra 386N (D24xx) system board. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/chipset.h>
#include <86box/sio.h>
#include <86box/video.h>
#include <86box/machine.h>
#include <86box/nmc93cxx.h>
#include <86box/plat_unused.h>

/* HP's board-control latch and upper ROM decode are private to the D24xx. */
typedef struct {
    uint8_t            control;
    uint16_t           indexed_index, indexed_regs[16];
    uint16_t           indexed_tags[8192];
    uint8_t            indexed_map[1024];
    nmc93cxx_eeprom_t *eeprom;
    mem_mapping_t      setup_rom;
} vect386n_board_t;

/* L.03.05 identifies the 20 MHz board through register 1 at 2Ch/2Eh.
 * Besides selecting the model string, this enables its indexed-controller
 * diagnostics. Implement the firmware-used cacheability map and tag RAM
 * accesses, including the inverted map readback. No external cache SRAM or
 * cache timing is modeled; POST selects the unpopulated SRAM configuration.
 * Register behavior is derived from the supplied HP firmware, not a complete
 * programming specification for this controller.
 */
static uint16_t
vect386n_indexed_read(uint16_t port, void *priv)
{
    vect386n_board_t *d = priv;
    if (port == 0x2c)
        return d->indexed_index;
    if (d->indexed_index == 1)
        return 0xcc00;
    /* There is no buffered cache data, so a flush is already complete. */
    if (d->indexed_index == 6)
        return 1;
    if (d->indexed_index == 4)
        return (~d->indexed_map[d->indexed_regs[3] & 0x3ff]) & 3;
    if (d->indexed_index == 0xc)
        return d->indexed_tags[d->indexed_regs[0xa] & 0x1fff];
    return d->indexed_index < 16 ? d->indexed_regs[d->indexed_index] : 0xffff;
}
static void
vect386n_indexed_write(uint16_t port, uint16_t val, void *priv)
{
    vect386n_board_t *d = priv;
    if (port == 0x2c)
        d->indexed_index = val;
    else if (d->indexed_index < 16) {
        if (d->indexed_index == 4)
            d->indexed_map[d->indexed_regs[3] & 0x3ff] = val & 3;
        else if (d->indexed_index == 0xc)
            d->indexed_tags[d->indexed_regs[0xa] & 0x1fff] = val & 0xffe3;
        else if (d->indexed_index != 1 && d->indexed_index != 6)
            d->indexed_regs[d->indexed_index] = val;
    }
}

static uint8_t
vect386n_control_read(UNUSED(uint16_t port), void *priv)
{
    vect386n_board_t *dev = priv;
    /* Read bit 2 is the inactive (high) configuration-clear switch;
       the corresponding output drives the EEPROM clock. */
    return (dev->control & ~0x10) | 0x04 | (nmc93cxx_eeprom_read(dev->eeprom) ? 0x10 : 0);
}

static void
vect386n_control_write(UNUSED(uint16_t port), uint8_t val, void *priv)
{
    vect386n_board_t *dev = priv;
    dev->control          = val;
    /* CS, clock and the shared serial data line. */
    nmc93cxx_eeprom_write(dev->eeprom, !!(val & 0x80), !!(val & 0x04), !!(val & 0x10));
}

static uint8_t
vect386n_setup_read(uint32_t addr, UNUSED(void *priv))
{
    return rom[addr & 0xffff];
}

static void *
vect386n_board_init(UNUSED(const device_t *info))
{
    vect386n_board_t        *dev    = calloc(1, sizeof(*dev));
    nmc93cxx_eeprom_params_t params = {
        .type     = NMC_93C46_x16_64,
        .filename = "vect386n_93c46.nvr"
    };
    dev->control = 0x20;
    /* Leave this interface absent in the 16 MHz profile; the BIOS uses
       its absence to select that model. */
    if (cpu_s->rspeed == 20000000)
        io_sethandler(0x2c, 4, NULL, vect386n_indexed_read, NULL, NULL, vect386n_indexed_write, NULL, dev);
    dev->eeprom = device_add_params(&nmc93cxx_device, &params);
    /* The upper bank's reset vector jumps to F000:FFF0 in the system bank.
       Its final 32 KB contain the resident F2 setup program. */
    mem_mapping_add(&dev->setup_rom, 0xff0000, 0x10000,
                    vect386n_setup_read, NULL, NULL, NULL, NULL, NULL,
                    rom, MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM | MEM_MAPPING_ROMCS, dev);
    io_sethandler(0x009b, 1, vect386n_control_read, NULL, NULL,
                  vect386n_control_write, NULL, NULL, dev);
    return dev;
}

static void
vect386n_board_close(void *priv)
{
    free(priv);
}

static const device_t vect386n_board_device = {
    .name          = "HP Vectra 386N board control",
    .internal_name = "vect386n_board",
    .init          = vect386n_board_init,
    .close         = vect386n_board_close
};

/* TI TACT83000 */
int
machine_at_vect386n_init(const machine_t *model)
{
    int ret = bios_load_linear("roms/machines/vect386n/HP_Vectra_386-20_D2480_BIOS.BIN",
                               0x000e0000, 131072, 0);
    if (bios_only || !ret)
        return ret;

    machine_at_ps2_init(model);
    device_add(&tact83000_device);
    device_add(&vect386n_board_device);
    device_add_params(&pc87310_device, (void *) PCX73XX_IDE);
    if (gfxcard[0] == VID_INTERNAL)
        device_add(&v7_vga_vect386n_device);
    return ret;
}
