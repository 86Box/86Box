/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the IBM PC 730 (type 6877) and PC 750
 *          (type 6887) system board.
 *
 *          The I/O map is from the PC 730/750 Technical Information
 *          Manual (52H3381, page 9); bit layouts it leaves out are taken
 *          from the LQKT47A BIOS.
 *
 * Authors: Avastrap2, <110710503+Avastrap2@users.noreply.github.com>
 *
 *          Copyright 2026 Avastrap2.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/io.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/pci.h>
#include <86box/chipset.h>
#include <86box/sio.h>
#include <86box/video.h>
#include <86box/crc.h>
#include <86box/nmc93cxx.h>
#include <86box/nvr_ps2.h>
#include <86box/plat_unused.h>
#include <86box/machine.h>

typedef struct ibm_pc700_t {
    uint8_t bank;
    uint8_t regs[4];
    uint8_t setup;
    uint8_t pos[4];

    nmc93cxx_eeprom_t *eeprom;
    uint16_t           eeprom_default[64];
} ibm_pc700_t;

static ibm_pc700_t *ibm_pc700;

/*
 * The 256 KB flash is seen through a 128 KB window, with A17 driven by GPIO
 * 78h bit 4. The code that switches banks is the same in both halves.
 */
static uint8_t
ibm_pc700_bios_read(uint32_t addr, void *priv)
{
    const ibm_pc700_t *dev = (ibm_pc700_t *) priv;

    return rom[(dev->bank << 17) | (addr & 0x1ffff)];
}

static uint16_t
ibm_pc700_bios_readw(uint32_t addr, void *priv)
{
    return ibm_pc700_bios_read(addr, priv) | (ibm_pc700_bios_read(addr + 1, priv) << 8);
}

static uint32_t
ibm_pc700_bios_readl(uint32_t addr, void *priv)
{
    return ibm_pc700_bios_readw(addr, priv) | ((uint32_t) ibm_pc700_bios_readw(addr + 2, priv) << 16);
}

static void
ibm_pc700_set_bank(ibm_pc700_t *dev, uint8_t bank)
{
    dev->bank = bank;
    mem_mapping_set_exec(&bios_mapping, &rom[bank << 17]);
    mem_mapping_set_exec(&bios_high_mapping, &rom[bank << 17]);
    flushmmucache();
}

/*
 * GPIO 78h bits 3-0 are SW1, read as the index into the BIOS's speed table:
 * bits 1-0 select 50, 66 or 60 MHz, bits 3-2 select 2.5x, 3x, 2x or 1.5x.
 */
static void
machine_at_ibm_pc700_gpio_init(void)
{
    uint32_t gpio = 0xfffffff0;

    if (cpu_busspeed > 60000000)
        gpio |= 0x01;
    else if (cpu_busspeed > 50000000)
        gpio |= 0x02;

    if (cpu_dmulti <= 1.5)
        gpio |= 0x0c;
    else if (cpu_dmulti <= 2.0)
        gpio |= 0x08;
    else if (cpu_dmulti > 2.5)
        gpio |= 0x04;

    machine_set_gpio_default(gpio);
}

uint32_t
machine_at_ibm_pc700_gpio_handler(uint8_t write, uint32_t val)
{
    ibm_pc700_t *dev = ibm_pc700;
    uint32_t     ret = machine_get_gpio_default();

    if (write) {
        ret &= val;
        machine_set_gpio(ret);

        if (dev != NULL) {
            /* GPIO 79h bits 4-2 are the security EEPROM's DI, SK and CS. */
            nmc93cxx_eeprom_write(dev->eeprom, !!(ret & 0x0400), !!(ret & 0x0800), !!(ret & 0x1000));

            if (!!(ret & 0x0010) != dev->bank)
                ibm_pc700_set_bank(dev, !!(ret & 0x0010));
        }
    } else {
        ret = machine_get_gpio();

        /* GPIO 79h bit 1 is the security EEPROM's DO. */
        if ((dev != NULL) && !nmc93cxx_eeprom_read(dev->eeprom))
            ret &= ~0x0200;
    }

    return ret;
}

/*
 * 7Ch-7Fh are the "L2 Cache ID, tamper EEPROM, SMI/PCI IRQ enable" ports.
 * The BIOS sizes the L2 cache from bits 3-0 of 7Ch, inverted: 0Bh is the
 * standard 256 KB of pipelined burst SRAM. It pulses bit 0 around writes to
 * 3F7h. 94h and 100h-105h are reserved for the riser.
 */
static uint8_t
ibm_pc700_read(uint16_t port, void *priv)
{
    const ibm_pc700_t *dev = (ibm_pc700_t *) priv;
    uint8_t            ret = 0xff;

    switch (port) {
        case 0x7c:
        case 0x7d:
        case 0x7e:
        case 0x7f:
            ret = dev->regs[port & 0x03];
            break;
        case 0x94:
            ret = dev->setup;
            break;
        case 0x102:
        case 0x103:
        case 0x104:
        case 0x105:
            ret = dev->pos[port - 0x102];
            break;

        default:
            break;
    }

    return ret;
}

static void
ibm_pc700_write(uint16_t port, uint8_t val, void *priv)
{
    ibm_pc700_t *dev = (ibm_pc700_t *) priv;

    switch (port) {
        case 0x7c:
            dev->regs[0] = (dev->regs[0] & 0xfe) | (val & 0x01);
            break;
        case 0x7d:
        case 0x7e:
        case 0x7f:
            dev->regs[port & 0x03] = val;
            break;
        case 0x94:
            dev->setup = val;
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
ibm_pc700_reset(void *priv)
{
    ibm_pc700_t *dev = (ibm_pc700_t *) priv;

    memset(dev->regs, 0x00, sizeof(dev->regs));
    memset(dev->pos, 0x00, sizeof(dev->pos));
    dev->regs[0] = 0x0b;
    dev->setup   = 0xff;
}

static void
ibm_pc700_close(void *priv)
{
    ibm_pc700_t *dev = (ibm_pc700_t *) priv;

    ibm_pc700 = NULL;
    free(dev);
}

/*
 * A new board's security EEPROM: no passwords, an empty VPD record in words
 * 08h-28h with its CRC in word 07h, and in word 24h the CRC of the empty CMOS
 * record at 42h, which the BIOS compares it with. Both are CRC-16/CCITT over
 * the low then the high byte of each word.
 */
static void
ibm_pc700_eeprom_default(ibm_pc700_t *dev)
{
    uint16_t crc_table[256];
    crc_t    crc = { .word = 0xffff };

    memset(dev->eeprom_default, 0xff, sizeof(dev->eeprom_default));
    memset(&dev->eeprom_default[0x08], 0x00, 0x21 * sizeof(uint16_t));

    crc16_setup(crc_table, 0x1021);
    crc16_calc(crc_table, 0x00, &crc);
    dev->eeprom_default[0x24] = crc.word;

    crc.word = 0xffff;
    for (uint8_t i = 0x08; i <= 0x28; i++) {
        crc16_calc(crc_table, dev->eeprom_default[i] & 0xff, &crc);
        crc16_calc(crc_table, dev->eeprom_default[i] >> 8, &crc);
    }
    dev->eeprom_default[0x07] = crc.word;
}

static void *
ibm_pc700_init(UNUSED(const device_t *info))
{
    ibm_pc700_t             *dev    = (ibm_pc700_t *) calloc(1, sizeof(ibm_pc700_t));
    nmc93cxx_eeprom_params_t params = { 0 };

    ibm_pc700_eeprom_default(dev);
    params.type            = NMC_93C46_x16_64;
    params.filename        = "ibm_pc700_93c46.nvr";
    params.default_content = dev->eeprom_default;
    dev->eeprom            = device_add_params(&nmc93cxx_device, &params);

    /* The riser's battery-backed 8 KB NVRAM at 74h-76h. */
    device_add(&ps2_nvr_8kb_device);

    mem_mapping_set_addr(&bios_high_mapping, 0xfffe0000, 0x20000);
    mem_mapping_set_handler(&bios_mapping,
                            ibm_pc700_bios_read, ibm_pc700_bios_readw, ibm_pc700_bios_readl,
                            NULL, NULL, NULL);
    mem_mapping_set_handler(&bios_high_mapping,
                            ibm_pc700_bios_read, ibm_pc700_bios_readw, ibm_pc700_bios_readl,
                            NULL, NULL, NULL);
    mem_mapping_set_p(&bios_mapping, dev);
    mem_mapping_set_p(&bios_high_mapping, dev);
    ibm_pc700_set_bank(dev, 1);

    io_sethandler(0x007c, 0x0004, ibm_pc700_read, NULL, NULL, ibm_pc700_write, NULL, NULL, dev);
    io_sethandler(0x0094, 0x0001, ibm_pc700_read, NULL, NULL, ibm_pc700_write, NULL, NULL, dev);
    io_sethandler(0x0102, 0x0004, ibm_pc700_read, NULL, NULL, ibm_pc700_write, NULL, NULL, dev);

    ibm_pc700_reset(dev);
    ibm_pc700 = dev;

    return dev;
}

static const device_t ibm_pc700_device = {
    .name          = "IBM PC 730/750 System Board",
    .internal_name = "ibm_pc700",
    .flags         = 0,
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
    machine_at_ibm_pc700_gpio_init();

    /*
     * The BIOS routes INTA of device 06h to PIRQ C, of 07h to PIRQ B and of
     * any other device to PIRQ A, and a card at 0Ch makes it crash. The riser
     * has three slots; the third one's device number is not documented.
     */
    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x01, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x06, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_slot(0x07, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_slot(0x09, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_slot(0x08, PCI_CARD_VIDEO,       1, 0, 0, 0);

    if (gfxcard[0] == VID_INTERNAL)
        device_add(machine_get_vid_device(machine));

    device_add(&i430fx_device);
    device_add(&piix_rev02_device);
    device_add_params(&pc87306_device, (void *) PCX730X_AMI);
    device_add(&ibm_pc700_device);
    device_add(&ibm_pc700_power_device);

    return ret;
}
