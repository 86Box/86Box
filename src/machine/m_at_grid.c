/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the GRiD GRiDcase 1520
 *
 *          The GRiDcase 1520 is a 286-based portable.
 * These are HDDs supported by GRiD1520 (and probably other 15XX) BIOS
 * "CP3022",5
 * "CP3024",5, 615,4,17 BIOS table type 2
 * "CP344",6,
 * "CP3044",9, 980,5,17 BIOS table type 17
 * "CP3042",9
 * "CP3104",7, 776,8,33 extended type 224 (separate entry outside BIOS table)
 * The only way to run unpatched BIOS is to run exactly that (or larger)
 * geometry and report model name correctly in response to IDENTYIFY command.
 * Alternatively you can use RomBuster to patch the BIOS.
 * https://classicbits.net/coding-and-software/my-software/rombuster/
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/io.h>
#include <86box/keyboard.h>
#include <86box/machine.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/vid_cga.h>
#include <86box/plat_unused.h>

#define GRID_APPROM_SELECT 0x440
#define GRID_APPROM_ENABLE 0x405

/*
approm mapping regs?
XXX_7FA         equ     7FAh
XXX_7F8         equ     7F8h
XXX_7F9         equ     7F9h
XXX_BD0         equ     0BD0h
XXX_BD1         equ     0BD1h
*/

#define GRID_EMS_PAGE_0 0x0258
#define GRID_EMS_PAGE_1 0x4258
#define GRID_EMS_PAGE_2 0x8258
#define GRID_EMS_PAGE_3 0xC258
#define GRID_TURBO 0x416
#define GRID_UNUSED_424 0x424
#define GRID_426 0x426
#define GRID_HIGH_ENABLE 0xFFF
#define GRID_ROM_SUBSYSTEM 0x6F8

// EMS window
#define GRID_EMS_BASE 0xE0000
#define GRID_EMS_PAGE_SIZE 0x4000
#define GRID_EMS_PAGE_MASK 0x3FFF
#define GRID_EMS_PAGE_SHIFT 14
// physical base of extended memory
#define GRID_EXTENDED_BASE 0xA0000
#define GRID_1M 0x100000

typedef struct {
    uint8_t grid_unknown;
    uint8_t grid_unused_424;
    uint8_t grid_426;
    uint8_t grid_high_enable;
    uint8_t grid_ems_page[4];
    mem_mapping_t grid_ems_mapping[4];
    uint8_t grid_turbo;
    uint8_t grid_rom_enable;
    uint8_t grid_rom_select;
} grid_t;


static uint32_t get_grid_ems_paddr(grid_t *dev, uint32_t addr) {
    uint32_t slot  = (addr >> GRID_EMS_PAGE_SHIFT) & 0x3;
    uint32_t paddr = addr;

    if (dev->grid_ems_page[slot] & 0x80)
        paddr = GRID_EXTENDED_BASE + ((uint32_t)(dev->grid_ems_page[slot] & 0x7F) << GRID_EMS_PAGE_SHIFT) + (addr & GRID_EMS_PAGE_MASK);

    return paddr;
}

static void grid_ems_mem_write8(uint32_t addr, uint8_t val, void *priv) {
    grid_t *dev = (grid_t *) priv;

    addr = get_grid_ems_paddr(dev, addr);

    if (addr < (mem_size << 10))
        ram[addr] = val;                
}

static uint8_t grid_ems_mem_read8(uint32_t addr, void *priv) {
    grid_t *dev = (grid_t *) priv;
    uint8_t val = 0xFF;

    addr = get_grid_ems_paddr(dev, addr);

    if (addr < (mem_size << 10))
        val = ram[addr];                

    return val;
}

static void grid_ems_mem_write16(uint32_t addr, uint16_t val, void *priv) {
    grid_t *dev = (grid_t *) priv;

    addr = get_grid_ems_paddr(dev, addr);

    if (addr < (mem_size << 10))
        *(uint16_t *)&(ram[addr]) = val;                
}

static uint16_t grid_ems_mem_read16(uint32_t addr, void *priv) {
    grid_t  *dev = (grid_t *) priv;
    uint16_t val = 0xFFFF;

    addr = get_grid_ems_paddr(dev, addr);

    if (addr < (mem_size << 10))
        val = *(uint16_t *)&(ram[addr]);

    return val;
}

static void grid_ems_update_mapping(grid_t *dev, uint32_t slot) {
    uint32_t vaddr = GRID_EMS_BASE + (slot << GRID_EMS_PAGE_SHIFT);
    if (dev->grid_ems_page[slot] & 0x80) {
        uint32_t paddr;
        mem_mapping_enable(&dev->grid_ems_mapping[slot]);
        paddr = get_grid_ems_paddr(dev, vaddr);
        mem_mapping_set_exec(&dev->grid_ems_mapping[slot], ram + paddr);
    } else {
        mem_mapping_disable(&dev->grid_ems_mapping[slot]);
    }
}

static void grid_io_write(uint16_t port, uint8_t val, void *priv) {
    grid_t *dev = (grid_t *) priv;

    switch (port) {
        case GRID_426:
            dev->grid_426 = val;
            break;
        case GRID_UNUSED_424:
            dev->grid_unused_424 = val;
            break;
        case GRID_ROM_SUBSYSTEM:
        case GRID_ROM_SUBSYSTEM+1:
        case GRID_ROM_SUBSYSTEM+2:
        case GRID_ROM_SUBSYSTEM+3:
        case GRID_ROM_SUBSYSTEM+4:
        case GRID_ROM_SUBSYSTEM+5:
        case GRID_ROM_SUBSYSTEM+6:
        case GRID_ROM_SUBSYSTEM+7:
            break;
        case GRID_APPROM_SELECT:
            dev->grid_rom_select = val;
            break;
        case GRID_APPROM_ENABLE:
            dev->grid_rom_enable = val;
            break;
        case GRID_TURBO:   
            if ((dev->grid_turbo ^ val) & 1) {
                dev->grid_turbo = val;
                if (dev->grid_turbo)
                    cpu_dynamic_switch(cpu);
                else
                    cpu_dynamic_switch(0); /* 286/6 */        
            }
            break;
        case GRID_EMS_PAGE_0:
        case GRID_EMS_PAGE_1:
        case GRID_EMS_PAGE_2:
        case GRID_EMS_PAGE_3: {
            uint32_t slot = (port >> 14) & 0x3;
            if (dev->grid_ems_page[slot] == val)
                break; // no change

            dev->grid_ems_page[slot] = val;
            if (dev->grid_high_enable & 0x1)
                break; // XMS is enabled
            grid_ems_update_mapping(dev, slot);

            flushmmucache();
            break;
        }
        case GRID_HIGH_ENABLE: {
            if (((val ^ dev->grid_high_enable) & 0x1) == 0)
                break; // no change
            dev->grid_high_enable = val;
            if (dev->grid_high_enable & 0x1) {
                for (uint8_t i = 0; i < 4; i++)
                    mem_mapping_disable(&dev->grid_ems_mapping[i]);
                mem_mapping_enable(&ram_high_mapping);                
            } else {
                mem_mapping_disable(&ram_high_mapping);
                for (uint8_t i = 0; i < 4; i++)
                    grid_ems_update_mapping(dev, i);
            }
            flushmmucache();
            break;
        }
        default:
            break;
    }
}

static uint8_t grid_io_read(uint16_t port, void *priv) {
    grid_t *dev = (grid_t *) priv;

    switch (port) {
        case GRID_426:
            return dev->grid_426;
            break;
        case GRID_UNUSED_424:
            return dev->grid_unused_424;
            break;
        case GRID_ROM_SUBSYSTEM:
            return 0x99;
            break;
        case GRID_ROM_SUBSYSTEM+1:
        case GRID_ROM_SUBSYSTEM+2:
        case GRID_ROM_SUBSYSTEM+3:
        case GRID_ROM_SUBSYSTEM+4:
        case GRID_ROM_SUBSYSTEM+5:
        case GRID_ROM_SUBSYSTEM+6:
        case GRID_ROM_SUBSYSTEM+7:
            break;
        case GRID_APPROM_SELECT:
            return dev->grid_rom_select;
        case GRID_APPROM_ENABLE:
            return dev->grid_rom_enable;
        case GRID_TURBO:   
            return dev->grid_turbo;
        case GRID_HIGH_ENABLE:
            return dev->grid_high_enable;
        case GRID_EMS_PAGE_0:
        case GRID_EMS_PAGE_1:
        case GRID_EMS_PAGE_2:
        case GRID_EMS_PAGE_3: {
            uint32_t slot = (port >> 14) & 0x3;

            return dev->grid_ems_page[slot];
            }
        default:
            break;
    }

    return 0xff;
}

static void *
grid_init(UNUSED(const device_t *info))
{
    grid_t *dev = calloc(1, sizeof(grid_t));

    io_sethandler(GRID_ROM_SUBSYSTEM, 0x0008, grid_io_read, NULL, NULL, grid_io_write, NULL, NULL, dev);
    io_sethandler(GRID_UNUSED_424, 0x0001, grid_io_read, NULL, NULL, grid_io_write, NULL, NULL, dev);
    io_sethandler(GRID_426, 0x0001, grid_io_read, NULL, NULL, grid_io_write, NULL, NULL, dev);
    io_sethandler(GRID_APPROM_SELECT, 0x0001, grid_io_read, NULL, NULL, grid_io_write, NULL, NULL, dev);
    io_sethandler(GRID_APPROM_ENABLE, 0x0001, grid_io_read, NULL, NULL, grid_io_write, NULL, NULL, dev);
    io_sethandler(GRID_TURBO, 0x0001, grid_io_read, NULL, NULL, grid_io_write, NULL, NULL, dev);
    dev->grid_turbo = 0x1;

    io_sethandler(GRID_HIGH_ENABLE, 0x0001, grid_io_read, NULL, NULL, grid_io_write, NULL, NULL, dev);
    io_sethandler(GRID_EMS_PAGE_0, 0x0001, grid_io_read, NULL, NULL, grid_io_write, NULL, NULL, dev);
    io_sethandler(GRID_EMS_PAGE_1, 0x0001, grid_io_read, NULL, NULL, grid_io_write, NULL, NULL, dev);
    io_sethandler(GRID_EMS_PAGE_2, 0x0001, grid_io_read, NULL, NULL, grid_io_write, NULL, NULL, dev);
    io_sethandler(GRID_EMS_PAGE_3, 0x0001, grid_io_read, NULL, NULL, grid_io_write, NULL, NULL, dev);

    dev->grid_high_enable = 1;
    for (uint8_t slot = 0; slot < 4; slot++) {
        dev->grid_ems_page[slot] = 0;
        mem_mapping_add(&dev->grid_ems_mapping[slot], GRID_EMS_BASE + (slot << GRID_EMS_PAGE_SHIFT), GRID_EMS_PAGE_SIZE, grid_ems_mem_read8, grid_ems_mem_read16, NULL,
                        grid_ems_mem_write8, grid_ems_mem_write16, NULL, ram + GRID_EXTENDED_BASE + (slot << GRID_EMS_PAGE_SHIFT), MEM_MAPPING_EXTERNAL, dev);
        mem_mapping_disable(&dev->grid_ems_mapping[slot]);
    }
    flushmmucache();
    return dev;
}

static void grid_close(void *priv) {
    grid_t *dev = (grid_t *) priv;

    free(dev);
}

static void grid_reset(void *priv) {
    grid_t  *dev = (grid_t *) priv;

    dev->grid_high_enable = 1;
    mem_mapping_enable(&ram_high_mapping);
    dev->grid_turbo = 0x1;
    for (uint8_t slot = 0; slot < 4; slot++) {
        dev->grid_ems_page[slot] = 0;
        mem_mapping_disable(&dev->grid_ems_mapping[slot]);
    }
    flushmmucache();
    dev->grid_unknown = 0;
    dev->grid_unused_424 = 0;
    dev->grid_426 = 0;
    dev->grid_rom_enable = 0;
    dev->grid_rom_select = 0;
}

const device_t grid_device = {
    .name          = "GRiDcase 1520 chipset",
    .internal_name = "grid1520",
    .flags         = 0,
    .local         = 0,
    .init          = grid_init,
    .close         = grid_close,
    .reset         = grid_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

int machine_at_grid1520_init(const machine_t *model) {
    int ret = 0;

    ret = bios_load_linear("roms/machines/grid1520/grid1520_891025.rom",
                           0x000f8000, 0x8000, 0);
    if (bios_only || !ret)
        return ret;

    machine_at_common_ide_init(model);
    mem_remap_top(384);

    device_add(&keyboard_at_device);
    // for now just select CGA with amber monitor 
    //device_add(&cga_device);

    if (fdc_current[0] == FDC_INTERNAL)
        device_add(&fdc_at_device);

    device_add(&grid_device);

    return ret;
}
