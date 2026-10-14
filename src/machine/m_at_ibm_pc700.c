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
 *          Sources: IBM PC 730 and PC 750 Technical Information Manual
 *          (52H3381), IBM US patent 5,530,879 and the LQKT47A BIOS, whose
 *          code locations are given as offsets into LQKT47A.BIN.
 *
 * Authors: The 86Box Project
 *
 *          Copyright 2026 The 86Box Project
 */

#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HAVE_STDARG_H
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
#include "m_at_ibm_pc700.h"

#define IBM_PC700_FLASH_BANK_SIZE 0x20000

/*
 * Patent 5,530,879: the status port reads the AND of PROC_RDY, low while the
 * controller takes a nibble, and CMD_STATE#, a heartbeat pulsed low for about
 * 1.5 us every 50-60 us. The BIOS finds the controller by that pulse
 * (217C3h) and waits for PROC_RDY before each nibble (2186Ah). The
 * patent gives no time for PROC_RDY.
 */
#define IBM_PC700_RAPID_BEAT_US  55.0
#define IBM_PC700_RAPID_PULSE_US 1.5
#define IBM_PC700_RAPID_BUSY_US  10.0

typedef struct ibm_pc700_t {
    nmc93cxx_eeprom_t *eeprom;
    void              *riser_nvr;

    uint8_t flash_bank;
    uint8_t gpio[2];

    ibm_pc700_rapid_t rapid;
    double            rapid_busy_until;

    uint8_t board_7c;
    uint8_t board_7d;
    uint8_t board_7e;
    uint8_t board_7f;

    uint8_t pos_94;
    uint8_t pos[4];

    uint16_t eeprom_default[64];
} ibm_pc700_t;

static ibm_pc700_t *ibm_pc700;

#ifdef ENABLE_IBM_PC700_LOG
int ibm_pc700_do_log = ENABLE_IBM_PC700_LOG;

static void
ibm_pc700_log(const char *fmt, ...)
{
    va_list ap;

    if (ibm_pc700_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ibm_pc700_log(fmt, ...)
#endif

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
    ibm_pc700_log("IBM PC 700: Flash bank %u selected\n", bank);
}

/*
 * SW1 as the BIOS reads it on GPIO 78h bits 3:0 (21C5Ch). Its speed table
 * at 21C3Ah has entries for 0-2 (2.5x at 50/66/60 MHz), 5 (3x at 66 MHz),
 * 9-Ah (2x at 66/60 MHz) and C-Eh (1.5x at 50/66/60 MHz), and none for 8,
 * which is 2x at 50 MHz.
 */
static uint8_t
ibm_pc700_cpu_straps(void)
{
    uint8_t ret;

    if (cpu_busspeed <= 50000000)
        ret = 0x00;
    else if (cpu_busspeed <= 60000000)
        ret = 0x02;
    else
        ret = 0x01;

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

        /*
         * GPIO 78h bit 4 drives flash A17: the bank-switch code, the same in
         * both banks (10545h and 10551h, 30545h and 30551h), clears and sets
         * it. 52H3381 puts A17 on 79h instead.
         * GPIO 79h bits 4:2 drive the security EEPROM.
         */
        ibm_pc700_flash_map(dev, !!(dev->gpio[0] & 0x10), false);
    }

    const uint8_t gpio_78 = (dev->gpio[0] & 0xf0) | ibm_pc700_cpu_straps();

    /* GPIO 79h bits 7, 1 and 0 are inputs, bit 1 being the EEPROM's data out. */
    uint8_t gpio_79 = (dev->gpio[1] & 0x7c) | 0x81;
    if (nmc93cxx_eeprom_read(dev->eeprom))
        gpio_79 |= 0x02;

    return 0xffff0000 | ((uint32_t) gpio_79 << 8) | gpio_78;
}

static double
ibm_pc700_rapid_now_us(void)
{
    return (double) tsc / (cpuclock / 1000000.0);
}

static uint8_t
ibm_pc700_rapid_read(uint16_t port, void *priv)
{
    const ibm_pc700_t *dev = (const ibm_pc700_t *) priv;
    const double       now = ibm_pc700_rapid_now_us();
    uint8_t            ret;

    if (port == 0x48)
        ret = dev->rapid.reply;
    else
        ret = (now >= dev->rapid_busy_until) &&
              (fmod(now, IBM_PC700_RAPID_BEAT_US) >= IBM_PC700_RAPID_PULSE_US);

    /* Only SD0 is driven. */
    return 0xfe | ret;
}

static void
ibm_pc700_rapid_write(uint16_t port, uint8_t val, void *priv)
{
    ibm_pc700_t *dev = (ibm_pc700_t *) priv;
    int          cmd;

    if (port == 0x48) {
        if (val & 0x10)
            dev->rapid_busy_until = ibm_pc700_rapid_now_us() + IBM_PC700_RAPID_BUSY_US;

        cmd = ibm_pc700_rapid_decode(&dev->rapid, val);
        if ((cmd >= 0) && ibm_pc700_rapid_execute(&dev->rapid, cmd)) {
            ibm_pc700_log("IBM PC 700: Power controller switched the supply off\n");
            plat_power_off();
        }
    } else if (val & 0x01) {
        /* Bit 0 holds the controller in reset (patent); the BIOS pulses it
           when the heartbeat is missing (217FBh). */
        dev->rapid.nibbles = 0;
    }
}

static uint8_t
ibm_pc700_board_read(uint16_t port, void *priv)
{
    const ibm_pc700_t *dev = (const ibm_pc700_t *) priv;
    uint8_t            ret;

    switch (port) {
        case 0x77:
            /* DCC (data collaboration card) setup/presence detect (52H3381).
               The BIOS treats FFh as no card (2F66Ah). */
            ret = 0xff;
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

/*
 * 52H3381 lists 7Ch-7Fh as "L2 Cache ID, tamper EEPROM, SMI/PCI IRQ enable",
 * 94h as reserved and 100h-105h as the reserved riser, without bit layouts,
 * so these are kept as plain registers. The BIOS tests 7Ch bit 6 (20268h,
 * 20309h, 204DDh, 204FEh), pulses 7Ch bit 0 around writes to 3F7h
 * (27753h) and clears the high nibble of 7Dh (30640h).
 */
static void
ibm_pc700_board_write(uint16_t port, uint8_t val, void *priv)
{
    ibm_pc700_t *dev = (ibm_pc700_t *) priv;

    switch (port) {
        case 0x7c:
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

/*
 * Give a new CMOS valid checksums: the AT sum of 10h-2Dh at 2Eh, the sum of
 * 35h-3Dh at 3Eh and an empty variable-length record at 42h, whose CRC at
 * 40h is what the BIOS's own routine (2C7A1h) computes for it.
 */
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

    nvr->regs[0x40] = 0xe1;
    nvr->regs[0x41] = 0xf0;
    nvr->regs[0x42] = 0x00;
}

/*
 * The riser's 8 KiB NVRAM behind ports 74h-76h, which the BIOS reads (byte
 * 186h at 27BA3h). These starting values come from the original
 * contribution; their meaning has not been traced.
 */
static void
ibm_pc700_seed_riser_nvr(ibm_pc700_t *dev)
{
    if (!ps2_nvr_is_new(dev->riser_nvr))
        return;

    ps2_nvr_set_byte(dev->riser_nvr, 0x0186, 0x00);
    ps2_nvr_set_byte(dev->riser_nvr, 0x018a, 0x6d);
    ps2_nvr_set_byte(dev->riser_nvr, 0x018b, 0xb6);

    ps2_nvr_set_byte(dev->riser_nvr, 0x0500, 0x01);
    ps2_nvr_set_byte(dev->riser_nvr, 0x0501, 0x00);
    ps2_nvr_set_byte(dev->riser_nvr, 0x0502, 0x00);
}

/* The power controller runs from standby power, so a reset leaves it alone. */
static void
ibm_pc700_reset(void *priv)
{
    ibm_pc700_t *dev = (ibm_pc700_t *) priv;

    dev->gpio[0]  = 0xff;
    dev->gpio[1]  = 0xff;
    dev->board_7c = 0x0b;
    dev->board_7d = 0x00;
    dev->board_7e = 0x00;
    dev->board_7f = 0x00;

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

    /*
     * A new security EEPROM starts with the token at word 07h and the CRC at
     * word 24h that the BIOS generated for the original contribution, rather
     * than blank. The CRC algorithm has not been traced.
     */
    memset(dev->eeprom_default, 0xff, sizeof(dev->eeprom_default));
    for (uint8_t i = 0x08; i <= 0x28; i++)
        dev->eeprom_default[i] = 0x0000;
    dev->eeprom_default[0x07] = 0x3cd1;
    dev->eeprom_default[0x24] = 0x870c;
    params.type            = NMC_93C46_x16_64;
    params.filename        = "ibm_pc700_93c46.nvr";
    params.default_content = dev->eeprom_default;

    dev->eeprom    = device_add_params(&nmc93cxx_device, &params);
    dev->riser_nvr = device_add(&ps2_nvr_8kb_device);
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

    ibm_pc700_rapid_power_on(&dev->rapid);

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

    /* 52H3381 does not document the slot numbers or interrupt routing. */
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
