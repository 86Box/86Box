/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          IBM PC 730/750 (types 6877/6887) system board glue.
 *
 * Authors: The 86Box Project
 *
 *          Copyright 2026 The 86Box Project
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/io.h>
#include <86box/machine.h>
#include <86box/mem.h>
#include <86box/nmc93cxx.h>
#include <86box/nvr_ps2.h>
#include <86box/pci.h>
#include <86box/plat.h>
#include <86box/plat_unused.h>
#include <86box/rom.h>
#include <86box/sio.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/video.h>

#include "cpu.h"

#define IBM_PC700_FLASH_BANK_SIZE 0x20000

typedef struct ibm_pc700_t {
    nmc93cxx_eeprom_t *eeprom;
    void            *riser_nvr;

    uint8_t flash_bank;
    uint8_t gpio[2];

    uint8_t rapid_data;
    uint8_t rapid_status_phase;
    uint8_t rapid_command;
    uint8_t rapid_command_phase;

    uint8_t board_7c;
    uint8_t board_7d;
    uint8_t board_7e;
    uint8_t board_7f;

    uint8_t pos_94;
    uint8_t pos[4];

    uint16_t eeprom_default[64];
} ibm_pc700_t;

static ibm_pc700_t *ibm_pc700;

static uint8_t
ibm_pc700_flash_read(uint32_t addr, void *priv)
{
    const ibm_pc700_t *dev = (const ibm_pc700_t *) priv;
    const uint32_t     off = ((uint32_t) dev->flash_bank << 17) | (addr & 0x1ffff);

    return rom[off];
}

static uint16_t
ibm_pc700_flash_readw(uint32_t addr, void *priv)
{
    uint16_t ret = ibm_pc700_flash_read(addr, priv);

    ret |= ((uint16_t) ibm_pc700_flash_read(addr + 1, priv)) << 8;
    return ret;
}

static uint32_t
ibm_pc700_flash_readl(uint32_t addr, void *priv)
{
    uint32_t ret = ibm_pc700_flash_readw(addr, priv);

    ret |= ((uint32_t) ibm_pc700_flash_readw(addr + 2, priv)) << 16;
    return ret;
}

static void
ibm_pc700_flash_map(ibm_pc700_t *dev, uint8_t bank, bool force)
{
    bank = !!bank;
    if (!force && dev->flash_bank == bank)
        return;

    dev->flash_bank = bank;
    mem_mapping_set_exec(&bios_mapping, &rom[bank * IBM_PC700_FLASH_BANK_SIZE]);
    mem_mapping_set_exec(&bios_high_mapping, &rom[bank * IBM_PC700_FLASH_BANK_SIZE]);
    flushmmucache();
    pclog("IBM PC 700: Flash bank %u selected\n", bank);
}

static uint8_t
ibm_pc700_cpu_straps(void)
{
    uint8_t ret;

    /* SW1/4 and SW1/3 select the bus clock; an open switch reads as one. */
    if (cpu_busspeed <= 50000000)
        ret = 0x00;
    else if (cpu_busspeed <= 60000000)
        ret = 0x02;
    else
        ret = 0x01;

    /* SW1/2 and SW1/1 select the multiplier. */
    if (cpu_dmulti <= 1.5)
        ret |= 0x0c;
    else if (cpu_dmulti <= 2.0)
        ret |= 0x08;
    else if (cpu_dmulti > 2.5)
        ret |= 0x04;

    return ret;
}

uint32_t
machine_at_ibm_pc700_gpio_handler(uint8_t write, uint32_t val)
{
    ibm_pc700_t *dev = ibm_pc700;

    if (dev == NULL)
        return 0xffffffff;

    if (write) {
        dev->gpio[0] = val & 0xff;
        dev->gpio[1] = (val >> 8) & 0xff;

        nmc93cxx_eeprom_write(dev->eeprom,
                             !!(dev->gpio[1] & 0x04),
                             !!(dev->gpio[1] & 0x08),
                             !!(dev->gpio[1] & 0x10));

        /* GPIO 78 bit 4 selects Flash A17; GPIO 79 bit 2 is EEPROM CS. */
        ibm_pc700_flash_map(dev, !!(dev->gpio[0] & 0x10), false);
    }

    /* GPIO 78 bits 3:0 are SW1/2, SW1/1, SW1/4 and SW1/3. */
    const uint8_t gpio_78 = (dev->gpio[0] & 0xf0) | ibm_pc700_cpu_straps();

    /* GPIO 79 bits 7, 1 and 0 are inputs; the remaining bits are outputs. */
    uint8_t gpio_79 = (dev->gpio[1] & 0x7c) | 0x81;
    if (nmc93cxx_eeprom_read(dev->eeprom))
        gpio_79 |= 0x02;

    return 0xffff0000 | ((uint32_t) gpio_79 << 8) | gpio_78;
}

static uint8_t
ibm_pc700_rapid_read(uint16_t port, void *priv)
{
    ibm_pc700_t *dev = (ibm_pc700_t *) priv;

    if (port == 0x48)
        return dev->rapid_data;

    /*
     * The controller's ready line pulses low after presence detection and
     * after each nibble transfer. Alternating the sampled state models the
     * observed transport without inventing command-level power semantics.
     */
    return (dev->rapid_status_phase++ & 1) ? 0x00 : 0x01;
}

static void
ibm_pc700_rapid_write(uint16_t port, uint8_t val, void *priv)
{
    ibm_pc700_t *dev = (ibm_pc700_t *) priv;

    if (port == 0x48) {
        dev->rapid_data = val;

        if (val & 0x80) {
            dev->rapid_command_phase = 0;
        } else if ((val & 0xf0) == 0x10) {
            if (dev->rapid_command_phase == 0) {
                dev->rapid_command = (val & 0x0f) << 4;
                dev->rapid_command_phase = 1;
            } else {
                dev->rapid_command |= val & 0x0f;
                dev->rapid_command_phase = 0;

                /* Command 0Eh asserts the PC 700 power-supply Off signal. */
                if (dev->rapid_command == 0x0e)
                    plat_power_off();
            }
        } else {
            dev->rapid_command_phase = 0;
        }
    } else {
        dev->rapid_status_phase = 0;
        dev->rapid_command_phase = 0;
    }
}

static uint8_t
ibm_pc700_board_read(uint16_t port, void *priv)
{
    ibm_pc700_t *dev = (ibm_pc700_t *) priv;

    uint8_t ret;

    switch (port) {
        case 0x77:
            ret = 0xff; /* Optional Data Collaboration Card absent. */
            break;
        case 0x7c:
            ret = dev->board_7c;
            break;
        case 0x7d:
            ret = dev->board_7d;
            break;
        case 0x7e:
            ret = dev->board_7e;
            break;
        case 0x7f:
            ret = dev->board_7f;
            break;
        case 0x94:
            ret = dev->pos_94;
            break;
        case 0x102:
        case 0x103:
        case 0x104:
        case 0x105:
            ret = dev->pos[port - 0x102];
            break;
        default:
            ret = 0xff;
            break;
    }

    return ret;
}

static void
ibm_pc700_board_write(uint16_t port, uint8_t val, void *priv)
{
    ibm_pc700_t *dev = (ibm_pc700_t *) priv;

    switch (port) {
        case 0x7c:
            /* Bits 3:1 identify the standard 256 KiB L2 cache. Bit 0 is a latch. */
            dev->board_7c = (dev->board_7c & 0xfe) | (val & 0x01);
            break;
        case 0x7d:
            dev->board_7d = val;
            break;
        case 0x7e:
            dev->board_7e = val;
            break;
        case 0x7f:
            dev->board_7f = val;
            break;
        case 0x94:
            dev->pos_94 = val;
            break;
        case 0x102:
        case 0x103:
        case 0x104:
        case 0x105:
            dev->pos[port - 0x102] = val;
            break;
        default:
            break;
    }
}

static void
ibm_pc700_seed_nvr(nvr_t *nvr)
{
    uint16_t checksum = 0;

    if (!nvr->is_new)
        return;

    nvr->regs[0x0e] &= ~0x60;
    nvr->regs[0x2d] = 0x80;
    for (uint8_t i = 0x10; i <= 0x2d; i++)
        checksum += nvr->regs[i];
    nvr->regs[0x2e] = checksum >> 8;
    nvr->regs[0x2f] = checksum & 0xff;

    checksum = 0;
    for (uint8_t i = 0x35; i <= 0x3d; i++)
        checksum += nvr->regs[i];
    nvr->regs[0x3e] = checksum >> 8;
    nvr->regs[0x3f] = checksum & 0xff;

    /* Empty IBM variable-length CMOS record and its CRC-16. */
    nvr->regs[0x40] = 0xe1;
    nvr->regs[0x41] = 0xf0;
    nvr->regs[0x42] = 0x00;
}

static void
ibm_pc700_seed_riser_nvr(ibm_pc700_t *dev)
{
    if (!ps2_nvr_is_new(dev->riser_nvr))
        return;

    /* Known-good markers used by the IBM firmware. */
    ps2_nvr_set_byte(dev->riser_nvr, 0x0186, 0x00);
    ps2_nvr_set_byte(dev->riser_nvr, 0x018a, 0x6d);
    ps2_nvr_set_byte(dev->riser_nvr, 0x018b, 0xb6);

    /* Minimal empty additive-checksum record at 0500h. */
    ps2_nvr_set_byte(dev->riser_nvr, 0x0500, 0x01);
    ps2_nvr_set_byte(dev->riser_nvr, 0x0501, 0x00);
    ps2_nvr_set_byte(dev->riser_nvr, 0x0502, 0x00);
}

static void
ibm_pc700_reset(void *priv)
{
    ibm_pc700_t *dev = (ibm_pc700_t *) priv;

    dev->gpio[0]             = 0xff;
    dev->gpio[1]             = 0xff;
    dev->rapid_data          = 0x00;
    dev->rapid_status_phase  = 0;
    dev->rapid_command       = 0x00;
    dev->rapid_command_phase = 0;
    dev->board_7c            = 0x0b; /* 256 KiB L2 cache, no tamper. */
    dev->board_7d            = 0x00;
    dev->board_7e            = 0x00;
    dev->board_7f            = 0x00;

    dev->pos_94 = 0xff;
    memset(dev->pos, 0x00, sizeof(dev->pos));

    nmc93cxx_eeprom_write(dev->eeprom, false, false, false);
    mem_set_mem_state_both(0xfffe0000, IBM_PC700_FLASH_BANK_SIZE,
                           MEM_READ_EXTANY | MEM_WRITE_EXTANY);
    mem_mapping_enable(&bios_high_mapping);
    ibm_pc700_flash_map(dev, 1, true);
}

static void
ibm_pc700_close(void *priv)
{
    ibm_pc700_t *dev = (ibm_pc700_t *) priv;

    if (ibm_pc700 == dev)
        ibm_pc700 = NULL;
    free(dev);
}

static void *
ibm_pc700_init(UNUSED(const device_t *info))
{
    ibm_pc700_t             *dev = (ibm_pc700_t *) calloc(1, sizeof(ibm_pc700_t));
    nmc93cxx_eeprom_params_t params;

    memset(dev->eeprom_default, 0xff, sizeof(dev->eeprom_default));
    for (uint8_t i = 0x08; i <= 0x28; i++)
        dev->eeprom_default[i] = 0x0000;
    /* BIOS-generated default security token and CRC for words 08h-28h. */
    dev->eeprom_default[0x07] = 0x3cd1;
    dev->eeprom_default[0x24] = 0x870c;
    params.type            = NMC_93C46_x16_64;
    params.filename        = "ibm_pc700_93c46.nvr";
    params.default_content = dev->eeprom_default;

    dev->eeprom    = device_add_params(&nmc93cxx_device, &params);
    dev->riser_nvr = device_add(&ps2_nvr_device);
    ibm_pc700_seed_riser_nvr(dev);

    ibm_pc700 = dev;

    mem_mapping_set_addr(&bios_high_mapping, 0xfffe0000, IBM_PC700_FLASH_BANK_SIZE);
    mem_mapping_set_handler(&bios_mapping,
                            ibm_pc700_flash_read, ibm_pc700_flash_readw, ibm_pc700_flash_readl,
                            NULL, NULL, NULL);
    mem_mapping_set_handler(&bios_high_mapping,
                            ibm_pc700_flash_read, ibm_pc700_flash_readw, ibm_pc700_flash_readl,
                            NULL, NULL, NULL);
    mem_mapping_set_p(&bios_mapping, dev);
    mem_mapping_set_p(&bios_high_mapping, dev);

    io_sethandler(0x0048, 2,
                  ibm_pc700_rapid_read, NULL, NULL, ibm_pc700_rapid_write, NULL, NULL, dev);
    io_sethandler(0x0077, 1,
                  ibm_pc700_board_read, NULL, NULL, ibm_pc700_board_write, NULL, NULL, dev);
    io_sethandler(0x007c, 4,
                  ibm_pc700_board_read, NULL, NULL, ibm_pc700_board_write, NULL, NULL, dev);
    io_sethandler(0x0094, 1,
                  ibm_pc700_board_read, NULL, NULL, ibm_pc700_board_write, NULL, NULL, dev);
    io_sethandler(0x0102, 4,
                  ibm_pc700_board_read, NULL, NULL, ibm_pc700_board_write, NULL, NULL, dev);

    dev->flash_bank = 0;
    ibm_pc700_reset(dev);

    return dev;
}

static const device_t ibm_pc700_device = {
    .name          = "IBM PC 730/750 (6877/6887) system board",
    .internal_name = "ibm_pc700",
    .flags         = DEVICE_SOFTRESET,
    .local         = 0,
    .init          = ibm_pc700_init,
    .close         = ibm_pc700_close,
    .reset         = ibm_pc700_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

int
machine_at_ibm_pc700_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ibm_pc700/LQKT47A.BIN",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x06, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x07, PCI_CARD_NORMAL,      2, 1, 4, 3);
    pci_register_slot(0x08, PCI_CARD_VIDEO,       4, 0, 0, 0);
    pci_register_slot(0x0c, PCI_CARD_NORMAL,      2, 1, 4, 3);

    device_add(&ibm_pc700_device);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    device_add(&i430fx_device);
    /* The photographed planar uses an SZ997 82371FB (PIIX A1, PCI revision 02h). */
    device_add(&piix_rev02_device);
    device_add_params(&pc87306_device, (void *) PCX730X_AMI);
    ibm_pc700_seed_nvr(device_get_priv(&nvr_at_device));

    return ret;
}
