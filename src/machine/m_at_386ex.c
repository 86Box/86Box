/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of Intel 386EX-based embedded machines.
 *
 *          The 386EX shares the 386SX instruction core but extends the
 *          external address bus to 26 bits (64 MB) and integrates a
 *          number of peripherals (DMA, PIC, timers, UARTs, watchdog,
 *          chip selects, SMM). The address bus extension is part of the
 *          CPU (CPU_PKG_386EX) and the peripherals are i386ex_device;
 *          this file is the board logic around them.
 *
 * Authors: Nathan Lewis, <git@nrlewis.dev>
 *
 *          Copyright 2026 Nathan Lewis.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/flash.h>
#include <86box/i386ex.h>
#include <86box/rom.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/nvr.h>
#include <86box/port_92.h>
#include <86box/serial.h>
#include <86box/board_led.h>
#include <86box/machine.h>
#include <86box/plat_unused.h>

/*
 * TS-3x00 board logic: the CPLD that sits around the 386EX.
 *
 * Its registers are a 16-byte I/O block on CS5#, which the BIOS puts at
 * 0x70, decoded from A3:0:
 *
 *   0x0-0x1  RTC (DS12887) index and data
 *   0x2-0x3  LCD port, RS following A0 (instruction at 0x2, data at 0x3)
 *   0x4      Product code: 1 = TS-3100, 2 = TS-3200, 4 = TS-3400
 *   0x5      Options: bit 0 battery-backed SRAM, bit 1 RS-485 on COM1
 *   0x6      Options: bit 0 external reset, bit 1 industrial temperature
 *   0x7      Jumpers: bit 1 JP3, bit 2 JP4
 *   0xD      DIO2 and LCD control
 *   0xE      DIO2
 *   0xF      Written by the BIOS before it resets the board through the
 *            watchdog; what it does is not known
 *
 * None of the options are fitted. The rest of the block reads 0xFF.
 *
 * The LCD port is a bus to an HD44780-style LCD in LCD mode (control bit 4),
 * on which each access is a bus cycle with RS from A0, and the LCD header's
 * pins are I/O in DIO mode: 0x2 is pins 7-14, whose nibbles control bits 2
 * and 3 make outputs, and 0x3 has the RS (bit 7) and WR# (bit 6) pins to
 * read and the EN pin (bit 0) to write. DIO2 is pins 1-15 of the DIO2
 * header, whose low and high nibbles control bits 0 and 1 make outputs.
 * Nothing is attached to either header, so their inputs read high, as does
 * the LCD bus.
 *
 * Each "checked" jumper config value represents an installed jumper. The
 * board pulls the jumper pins up and an installed jumper grounds its pin,
 * so the jumpers on the 386EX's pins read 0 when installed; those in the
 * jumper register read 1.
 *
 *   JP1 - Manufacturing Mode        - 386EX P1.7  - I/O 0xF860 bit 7
 *   JP2 - Redirect Console to COM2  - 386EX P1.4  - I/O 0xF860 bit 4
 *   JP3 - Enable Flash Write        -             - I/O 0x0077 bit 1
 *   JP4 - Reduced Clock Rate        -             - I/O 0x0077 bit 2
 *   JP5 - User Jumper               - 386EX P3.2  - I/O 0xF870 bit 2
 *
 * The jumpers on the 386EX's pins are read through the machine's GPIO pins,
 * which the 386EX's I/O ports are, as is the board's LED: P1.6 drives it
 * through an inverter, so it lights while P1.6 is low, as it is from reset
 * on its weak pull-down.
 */
#define TS3X00_REG_RTC       0x0
#define TS3X00_REG_LCD_INST  0x2
#define TS3X00_REG_LCD_DATA  0x3
#define TS3X00_REG_PRODUCT   0x4
#define TS3X00_REG_OPTIONS1  0x5
#define TS3X00_REG_OPTIONS2  0x6
#define TS3X00_REG_JUMPERS   0x7
#define TS3X00_REG_DIO_CTRL  0xd
#define TS3X00_REG_DIO2      0xe
#define TS3X00_REG_RESET     0xf

#define TS3X00_REG_BLOCK     16

#define TS3100_PRODUCT       0x01
#define TS3200_PRODUCT       0x02
#define TS3400_PRODUCT       0x04

#define TS3X00_JUMPERS_JP3   0x02
#define TS3X00_JUMPERS_JP4   0x04

#define TS3X00_DIO2_LOW_OUT  0x01 /* DIO2_0-3 (pins 1, 3, 5 and 7) are outputs. */
#define TS3X00_DIO2_HIGH_OUT 0x02 /* DIO2_4-7 (pins 9, 11, 13 and 15) are outputs. */
#define TS3X00_LCD_LOW_OUT   0x04 /* LCD pins 7-10 are outputs. */
#define TS3X00_LCD_HIGH_OUT  0x08 /* LCD pins 11-14 are outputs. */
#define TS3X00_LCD_MODE      0x10 /* The LCD port is an LCD bus. */

#define TS3X00_LCD_EN        0x01 /* In DIO mode, at 0x3. */

#define TS3X00_ADDR_SPACE    0x04000000 /* The 386EX's 26-bit address space. */
#define TS3X00_SEL_SHIFT     MEM_GRANULARITY_BITS
#define TS3X00_SEL_MASK      MEM_GRANULARITY_MASK
#define TS3X00_SEL_UCS       0x01
#define TS3X00_SEL_CS4       0x02

#define TS3100_WRAP          0x00200000 /* The TS-3100 decodes A20:0, so its memory repeats every 2 MB. */
#define TS3100_RAM_MASK      0x0007ffff /* Where in its 512k of DRAM the TS-3100 keeps a 1 MB address. */

typedef struct ts3x00_t {
    uint8_t jp1;
    uint8_t jp2;
    uint8_t jp3;
    uint8_t jp4;
    uint8_t jp5;

    uint8_t product;
    uint8_t dio_ctrl;
    uint8_t dio2;    /* The DIO2 latch. */
    uint8_t lcd;     /* The LCD pin 7-14 latch. */
    uint8_t lcd_ctl; /* The LCD EN pin latch. */

    nvr_t *nvr;

    sst_t *flash;

    uint32_t      shadow_base;
    uint32_t      shadow_size;
    uint8_t      *shadow;
    mem_mapping_t shadow_mapping;
    uint32_t      low_top;  /* End of the DRAM at 0. */
    uint32_t      high_top; /* End of the DRAM above 1 MB. */

    uint32_t      wrap;    /* How often the board's own memory repeats, 0 if it does not. */
    uint8_t       dos_rom; /* Reads at 0xE0000-0xEFFFF of every megabyte select the flash. */
    mem_mapping_t ram_mapping;
    mem_mapping_t dos_rom_mapping;

    uint8_t sel[TS3X00_ADDR_SPACE >> TS3X00_SEL_SHIFT]; /* The chip selects claiming each granule. */
} ts3x00_t;

#define TS3X00_GPIO_JP1 (1 << 7)        /* P1.7 */
#define TS3X00_GPIO_JP2 (1 << 4)        /* P1.4 */
#define TS3X00_GPIO_LED (1 << 6)        /* P1.6 */
#define TS3X00_GPIO_JP5 (1 << (16 + 2)) /* P3.2 */

/* For the GPIO handler, which is the machine's and so has no device to go with it. */
static ts3x00_t *ts3x00_board;

/*
   The 386EX hands over what it does to its pins, and reads back their
   levels, which the jumpers pull to ground where installed and pull-ups
   hold high otherwise.
 */
uint32_t
machine_at_ts3x00_gpio_handler(uint8_t write, uint32_t val)
{
    const ts3x00_t *dev = ts3x00_board;
    uint32_t        ret = machine_get_gpio();

    if (write) {
        machine_set_gpio(val);
        board_led_set(0, !(val & TS3X00_GPIO_LED));
        return val;
    }

    if (dev != NULL) {
        ret |= TS3X00_GPIO_JP1 | TS3X00_GPIO_JP2 | TS3X00_GPIO_JP5;
        if (dev->jp1)
            ret &= ~TS3X00_GPIO_JP1;
        if (dev->jp2)
            ret &= ~TS3X00_GPIO_JP2;
        if (dev->jp5)
            ret &= ~TS3X00_GPIO_JP5;
    }

    return ret;
}

/* A port's pins: the latch where they are outputs, and where they are inputs, what drives them. */
static uint8_t
ts3x00_port_pins(uint8_t latch, uint8_t outputs, uint8_t inputs)
{
    return (latch & outputs) | (inputs & ~outputs);
}

static uint8_t
ts3x00_dio2_outputs(const ts3x00_t *dev)
{
    return ((dev->dio_ctrl & TS3X00_DIO2_LOW_OUT) ? 0x0f : 0x00) | ((dev->dio_ctrl & TS3X00_DIO2_HIGH_OUT) ? 0xf0 : 0x00);
}

static uint8_t
ts3x00_lcd_outputs(const ts3x00_t *dev)
{
    return ((dev->dio_ctrl & TS3X00_LCD_LOW_OUT) ? 0x0f : 0x00) | ((dev->dio_ctrl & TS3X00_LCD_HIGH_OUT) ? 0xf0 : 0x00);
}

/* An LCD bus cycle, with RS from A0. No LCD is attached, so the bus reads high. */
static uint8_t
ts3x00_lcd_bus_read(UNUSED(const ts3x00_t *dev), UNUSED(int rs))
{
    return 0xff;
}

static void
ts3x00_lcd_bus_write(UNUSED(ts3x00_t *dev), UNUSED(int rs), UNUSED(uint8_t val))
{
    /* No LCD is attached. */
}

static uint8_t
ts3x00_read(uint16_t port, void *priv)
{
    const ts3x00_t *dev = (ts3x00_t *) priv;
    uint8_t         ret = 0xff;

    switch (port & (TS3X00_REG_BLOCK - 1)) {
        case TS3X00_REG_LCD_INST:
        case TS3X00_REG_LCD_DATA:
            if (dev->dio_ctrl & TS3X00_LCD_MODE)
                ret = ts3x00_lcd_bus_read(dev, port & 1);
            else if (!(port & 1))
                ret = ts3x00_port_pins(dev->lcd, ts3x00_lcd_outputs(dev), 0xff);
            /* The RS and WR# pins, which nothing drives, read high, as does the rest. */
            break;
        case TS3X00_REG_PRODUCT:
            ret = dev->product;
            break;
        case TS3X00_REG_OPTIONS1:
        case TS3X00_REG_OPTIONS2:
            ret = 0x00;
            break;
        case TS3X00_REG_JUMPERS:
            ret = (dev->jp3 ? TS3X00_JUMPERS_JP3 : 0x00) | (dev->jp4 ? TS3X00_JUMPERS_JP4 : 0x00);
            break;
        case TS3X00_REG_DIO_CTRL:
            ret = dev->dio_ctrl;
            break;
        case TS3X00_REG_DIO2:
            ret = ts3x00_port_pins(dev->dio2, ts3x00_dio2_outputs(dev), 0xff);
            break;
        default:
            break;
    }

    return ret;
}

static void
ts3x00_write(uint16_t port, uint8_t val, void *priv)
{
    ts3x00_t *dev = (ts3x00_t *) priv;

    switch (port & (TS3X00_REG_BLOCK - 1)) {
        case TS3X00_REG_LCD_INST:
        case TS3X00_REG_LCD_DATA:
            if (dev->dio_ctrl & TS3X00_LCD_MODE)
                ts3x00_lcd_bus_write(dev, port & 1, val);
            else if (port & 1)
                dev->lcd_ctl = val & TS3X00_LCD_EN;
            else
                dev->lcd = val;
            break;
        case TS3X00_REG_DIO_CTRL:
            dev->dio_ctrl = val;
            break;
        case TS3X00_REG_DIO2:
            dev->dio2 = val;
            break;
        default:
            break;
    }
}

/*
   CS5#: the board's register block, which repeats every 16 bytes of the
   block CS5# selects. The RTC is the machine's, so it is mapped on its own.
 */
static void
ts3x00_cs5(void *priv, int set, int is_mem, uint32_t base, uint32_t size)
{
    ts3x00_t *dev = (ts3x00_t *) priv;

    if (is_mem)
        return;

    for (uint32_t blk = base; blk < (base + size); blk += TS3X00_REG_BLOCK) {
        uint32_t len = MIN(TS3X00_REG_BLOCK, base + size - blk);
        uint32_t reg = blk;

        /* The RTC's two registers, when the block has them. */
        if (!(blk & (TS3X00_REG_BLOCK - 1)) && (len >= 2)) {
            nvr_at_handler(set, blk + TS3X00_REG_RTC, dev->nvr);
            reg += 2;
        }

        if (reg < (blk + len))
            io_handler(set, reg, blk + len - reg, ts3x00_read, NULL, NULL, ts3x00_write, NULL, NULL, dev);
    }
}

static void *
ts3x00_dev_init(UNUSED(const device_t *info))
{
    ts3x00_t *dev = (ts3x00_t *) calloc(1, sizeof(ts3x00_t));

    dev->jp1 = device_get_config_int("jp1");
    dev->jp2 = device_get_config_int("jp2");
    dev->jp3 = device_get_config_int("jp3");
    dev->jp4 = device_get_config_int("jp4");
    dev->jp5 = device_get_config_int("jp5");

    return dev;
}

static void
ts3x00_dev_close(void *priv)
{
    ts3x00_t *dev = (ts3x00_t *) priv;

    if (dev->flash != NULL)
        sst_flash_destroy(dev->flash);

    ts3x00_board = NULL;
    free(dev);
}

static const device_config_t ts3x00_config[] = {
    // clang-format off
    {
        .name        = "jp1",
        .description = "JP1: Manufacturing Mode",
        .type        = CONFIG_BINARY,
        .default_int = 0
    },
    {
        .name        = "jp2",
        .description = "JP2: Redirect Console to COM2",
        .type        = CONFIG_BINARY,
        .default_int = 1
    },
    {
        .name        = "jp3",
        .description = "JP3: Enable Flash Write",
        .type        = CONFIG_BINARY,
        .default_int = 1
    },
    {
        .name        = "jp4",
        .description = "JP4: Reduced Clock Rate",
        .type        = CONFIG_BINARY,
        .default_int = 0
    },
    {
        .name        = "jp5",
        .description = "JP5: User Jumper",
        .type        = CONFIG_BINARY,
        .default_int = 0
    },
    { .name = "", .description = "", .type = CONFIG_END }
    // clang-format on
};

const device_t ts3x00_device = {
    .name          = "Technologic Systems TS-3x00",
    .internal_name = "ts3x00",
    .flags         = 0,
    .local         = 0,
    .init          = ts3x00_dev_init,
    .close         = ts3x00_dev_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ts3x00_config
};

/*
 * TS-9500 expansion board: a PS/2 keyboard and mouse controller, VGA, a
 * Compact Flash socket and a flash chip on the PC/104 bus, with registers
 * in I/O 0x198-0x19F. The boards have no keyboard controller of their own,
 * so one is always fitted, with only its keyboard controller and registers
 * emulated:
 *
 *   0x19A  Bit 0 LED (1 = on), bit 1 KB IRQ, bit 2 mouse IRQ, bit 6 JP1
 *          (write enable for the top 512k of the flash), bit 7 JP2 (user),
 *          jumpers reading 1 when installed
 *   0x19B  DIP switches 1-8, 1 = on
 *   0x19D  Board ID, 0x5F
 *
 * What the IRQ bits report is not documented, so they read 0. With the
 * rest of the board not emulated, it is set up with as little enabled as
 * will do: neither jumper installed, and of the DIP switches only 8 (VGA
 * BIOS and keyboard controller) on. The rest of the block is not
 * documented and reads 0xFF.
 *
 * The BIOS only uses the board ID: when 0x19D reads 0x5F, it connects the
 * 386EX's INT0 pin, which carries the keyboard's IRQ1, to the master's IR1
 * (P3CFG bit 2), and writes 0x74 to 0x199. Besides that, it drives the
 * LED with the board's own, and never reads the IRQ bits or DIP switches.
 */
#define TS9500_REG_BASE    0x0198
#define TS9500_REG_SIZE    8
#define TS9500_REG_CTRL    0x019a
#define TS9500_REG_DIP     0x019b
#define TS9500_REG_ID      0x019d

#define TS9500_CTRL_LED    0x01

#define TS9500_DIP_VGA_KBC 0x80 /* DIP 8. */

#define TS9500_ID          0x5f

typedef struct ts9500_t {
    uint8_t ctrl;
} ts9500_t;

static uint8_t
ts9500_read(uint16_t port, void *priv)
{
    const ts9500_t *dev = (ts9500_t *) priv;

    switch (port) {
        case TS9500_REG_CTRL:
            return dev->ctrl;
        case TS9500_REG_DIP:
            return TS9500_DIP_VGA_KBC;
        case TS9500_REG_ID:
            return TS9500_ID;
        default:
            return 0xff;
    }
}

static void
ts9500_write(uint16_t port, uint8_t val, void *priv)
{
    ts9500_t *dev = (ts9500_t *) priv;

    if (port == TS9500_REG_CTRL)
        dev->ctrl = val & TS9500_CTRL_LED;
}

static void *
ts9500_init(UNUSED(const device_t *info))
{
    ts9500_t *dev = (ts9500_t *) calloc(1, sizeof(ts9500_t));

    io_sethandler(TS9500_REG_BASE, TS9500_REG_SIZE, ts9500_read, NULL, NULL, ts9500_write, NULL, NULL, dev);

    /* The keyboard controller is the one the machine table names. */
    device_add_params(machine_get_kbc_device(machine), (void *) machines[machine].kbc_params);

    return dev;
}

static void
ts9500_close(void *priv)
{
    free(priv);
}

static const device_t ts9500_device = {
    .name          = "TS-9500 Keyboard/Mouse Expansion Board",
    .internal_name = "ts9500",
    .flags         = 0,
    .local         = 0,
    .init          = ts9500_init,
    .close         = ts9500_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

/*
 * TS-3x00 memory map. The DRAM sits at the bottom of memory (448k on the
 * TS-3100, 640k on the TS-3200), in a shadow area at the top of the first
 * megabyte (0xF0000-0xFFFFF on the TS-3100, 0xE0000-0xFFFFF on the
 * TS-3200) and, on the TS-3200, above 1 MB. The BIOS
 * flash is on UCS#, which selects all of memory after reset, and wins over
 * whatever else is at an address while UCS# selects it. The shadow DRAM
 * only takes writes while UCS# is active, so the BIOS shadows itself by
 * copying the flash onto itself before moving UCS# away; otherwise the
 * shadow area is read-only.
 *
 * The TS-3100's board logic only decodes A20:0, so its DRAM and shadow
 * area repeat every 2 MB. Its DOS-ROM is not shadowed: a read at
 * 0xE0000-0xEFFFF of any megabyte asserts the flash's chip select, as UCS#
 * does, and the flash only decodes its own address lines, A18:0 on the
 * 512k part, so it returns what it has at 0x60000-0x6FFFF, where the
 * DOS-ROM is. Anything on UCS# or CS4# wins over all of this, and as
 * nothing is emulated on CS4#, what it selects is the external bus.
 *
 * Which of these an address is, and so its memory state, is worked out for
 * each 4k granule from what the board decodes there and which chip selects
 * claim it.
 */
enum {
    TS3X00_REGION_NONE = 0,
    TS3X00_REGION_DRAM,
    TS3X00_REGION_SHADOW,
    TS3X00_REGION_DOS_ROM
};

static int
ts3x00_region(const ts3x00_t *j, uint32_t addr)
{
    if (j->dos_rom && ((addr & 0x000f0000) == 0x000e0000))
        return TS3X00_REGION_DOS_ROM;
    if (j->wrap)
        addr &= j->wrap - 1;

    if (addr < j->low_top)
        return TS3X00_REGION_DRAM;
    if ((addr >= j->shadow_base) && (addr < (j->shadow_base + j->shadow_size)))
        return TS3X00_REGION_SHADOW;
    if ((addr >= 0x00100000) && (addr < j->high_top))
        return TS3X00_REGION_DRAM;

    return TS3X00_REGION_NONE;
}

static int
ts3x00_mem_state(const ts3x00_t *j, uint32_t addr)
{
    int region = ts3x00_region(j, addr);

    if (j->sel[addr >> TS3X00_SEL_SHIFT] & TS3X00_SEL_UCS)
        return (region == TS3X00_REGION_SHADOW) ? (MEM_READ_ROMCS | MEM_WRITE_INTERNAL) : (MEM_READ_ROMCS | MEM_WRITE_ROMCS);
    if (j->sel[addr >> TS3X00_SEL_SHIFT] & TS3X00_SEL_CS4)
        return MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL;

    switch (region) {
        case TS3X00_REGION_DRAM:
            return MEM_READ_INTERNAL | MEM_WRITE_INTERNAL;
        case TS3X00_REGION_SHADOW:
            return MEM_READ_INTERNAL | MEM_WRITE_DISABLED;
        case TS3X00_REGION_DOS_ROM:
            return MEM_READ_ROMCS | MEM_WRITE_EXTERNAL;
        default:
            return MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL;
    }
}

/* Set the memory state of the granules from base to base + size, in runs of the same state. */
static void
ts3x00_update_mem(const ts3x00_t *j, uint32_t base, uint32_t size)
{
    uint32_t start = base & ~TS3X00_SEL_MASK;
    uint32_t end   = MIN((uint64_t) base + size + TS3X00_SEL_MASK, TS3X00_ADDR_SPACE) & ~TS3X00_SEL_MASK;
    uint32_t run   = start;
    int      state = ts3x00_mem_state(j, start);

    for (uint32_t addr = start + TS3X00_SEL_MASK + 1; addr <= end; addr += TS3X00_SEL_MASK + 1) {
        int next = (addr < end) ? ts3x00_mem_state(j, addr) : -1;

        if (next != state) {
            mem_set_mem_state_both(run, addr - run, state);
            run   = addr;
            state = next;
        }
    }
}

/* Mark the granules a chip select claims, and work their memory state out again. */
static void
ts3x00_select(ts3x00_t *j, uint8_t sel, int set, uint32_t base, uint32_t size)
{
    uint32_t end = MIN((uint64_t) base + size, TS3X00_ADDR_SPACE);

    for (uint32_t addr = base & ~TS3X00_SEL_MASK; addr < end; addr += TS3X00_SEL_MASK + 1) {
        if (set)
            j->sel[addr >> TS3X00_SEL_SHIFT] |= sel;
        else
            j->sel[addr >> TS3X00_SEL_SHIFT] &= ~sel;
    }

    ts3x00_update_mem(j, base, size);
}

/* UCS#: the CSU maps the flash, which leaves which mapping wins where to the board. */
static void
ts3x00_ucs(void *priv, int set, int is_mem, uint32_t base, uint32_t size)
{
    ts3x00_t *j = (ts3x00_t *) priv;

    if (is_mem)
        ts3x00_select(j, TS3X00_SEL_UCS, set, base, size);
}

/* CS4#: nothing is emulated on it, but it takes what it selects away from the TS-3100's board logic. */
static void
ts3x00_cs4(void *priv, int set, int is_mem, uint32_t base, uint32_t size)
{
    ts3x00_t *j = (ts3x00_t *) priv;

    if (is_mem)
        ts3x00_select(j, TS3X00_SEL_CS4, set, base, size);
}

static void
ts3x00_flash_map(void *priv, mem_mapping_t *mapping, uint32_t base, uint32_t size)
{
    sst_flash_map((sst_t *) priv, mapping, base, size);
}

static void
ts3x00_flash_remap(void *priv, mem_mapping_t *mapping, uint32_t base, uint32_t size)
{
    sst_flash_remap((sst_t *) priv, mapping, base, size);
}

static uint8_t
ts3x00_shadow_read(uint32_t addr, void *priv)
{
    const ts3x00_t *j = (ts3x00_t *) priv;

    return j->shadow[addr - j->shadow_base];
}

static uint16_t
ts3x00_shadow_readw(uint32_t addr, void *priv)
{
    return ts3x00_shadow_read(addr, priv) | (ts3x00_shadow_read(addr + 1, priv) << 8);
}

static uint32_t
ts3x00_shadow_readl(uint32_t addr, void *priv)
{
    return ts3x00_shadow_readw(addr, priv) | (ts3x00_shadow_readw(addr + 2, priv) << 16);
}

/* Only reached while UCS# selects the shadow area, so the flash sees the write too. */
static void
ts3x00_shadow_write(uint32_t addr, uint8_t val, void *priv)
{
    ts3x00_t *j = (ts3x00_t *) priv;

    j->shadow[addr - j->shadow_base] = val;
    sst_flash_write(j->flash, addr, val);
}

static void
ts3x00_shadow_writew(uint32_t addr, uint16_t val, void *priv)
{
    ts3x00_shadow_write(addr, val & 0xff, priv);
    ts3x00_shadow_write(addr + 1, val >> 8, priv);
}

static void
ts3x00_shadow_writel(uint32_t addr, uint32_t val, void *priv)
{
    ts3x00_shadow_writew(addr, val & 0xffff, priv);
    ts3x00_shadow_writew(addr + 2, val >> 16, priv);
}

/*
   The TS-3100's DRAM and shadow area at every 2 MB. Both are in its 512k of
   DRAM at the same offsets as in the first megabyte, after taking A19 away:
   the shadow area at 0xF0000-0xFFFFF is the DRAM after the low 448k. The
   memory states only let the mapping through where one of them is.
 */
static uint8_t
ts3x00_wrap_read(uint32_t addr, UNUSED(void *priv))
{
    uint32_t offset = addr & TS3100_RAM_MASK;

    addreadlookup(mem_logical_addr, offset);

    return ram[offset];
}

static uint16_t
ts3x00_wrap_readw(uint32_t addr, UNUSED(void *priv))
{
    uint32_t offset = addr & TS3100_RAM_MASK;

    addreadlookup(mem_logical_addr, offset);

    return *(uint16_t *) &ram[offset];
}

static uint32_t
ts3x00_wrap_readl(uint32_t addr, UNUSED(void *priv))
{
    uint32_t offset = addr & TS3100_RAM_MASK;

    addreadlookup(mem_logical_addr, offset);

    return *(uint32_t *) &ram[offset];
}

/* Writes to the shadow area only get here while UCS# selects it, and go to the flash too. */
static void
ts3x00_wrap_write(uint32_t addr, uint8_t val, void *priv)
{
    const ts3x00_t *j      = (ts3x00_t *) priv;
    uint32_t        offset = addr & TS3100_RAM_MASK;

    if (offset >= j->low_top) {
        ram[offset] = val;
        sst_flash_write(j->flash, addr, val);
        return;
    }

    addwritelookup_remapped(mem_logical_addr, addr, offset);
    mem_write_ramb_page(offset, val, &pages[addr >> 12]);
}

static void
ts3x00_wrap_writew(uint32_t addr, uint16_t val, void *priv)
{
    const ts3x00_t *j      = (ts3x00_t *) priv;
    uint32_t        offset = addr & TS3100_RAM_MASK;

    if (offset >= j->low_top) {
        ts3x00_wrap_write(addr, val & 0xff, priv);
        ts3x00_wrap_write(addr + 1, val >> 8, priv);
        return;
    }

    addwritelookup_remapped(mem_logical_addr, addr, offset);
    mem_write_ramw_page(offset, val, &pages[addr >> 12]);
}

static void
ts3x00_wrap_writel(uint32_t addr, uint32_t val, void *priv)
{
    const ts3x00_t *j      = (ts3x00_t *) priv;
    uint32_t        offset = addr & TS3100_RAM_MASK;

    if (offset >= j->low_top) {
        ts3x00_wrap_writew(addr, val & 0xffff, priv);
        ts3x00_wrap_writew(addr + 2, val >> 16, priv);
        return;
    }

    addwritelookup_remapped(mem_logical_addr, addr, offset);
    mem_write_raml_page(offset, val, &pages[addr >> 12]);
}

/* Point the page table at the DRAM wherever it repeats, as the RAM write paths need. */
static void
ts3x00_wrap_pages(const ts3x00_t *j)
{
    for (uint32_t base = j->wrap; base < TS3X00_ADDR_SPACE; base += j->wrap) {
        for (uint32_t c = base >> 12; c < ((base + j->low_top) >> 12); c++) {
            uint32_t offset = (c << 12) & TS3100_RAM_MASK;

            pages[c].mem                    = &ram[offset];
            pages[c].write_b                = mem_write_ramb_page;
            pages[c].write_w                = mem_write_ramw_page;
            pages[c].write_l                = mem_write_raml_page;
            pages[c].evict_prev             = EVICT_NOT_IN_LIST;
            pages[c].byte_dirty_mask        = &byte_dirty_mask[(offset >> 12) * 64];
            pages[c].byte_code_present_mask = &byte_code_present_mask[(offset >> 12) * 64];
        }
    }
}

typedef struct ts3x00_model_t {
    const char     *rom;
    const char     *nvr; /* The CMOS as the board ships, with the flash disk set up. */
    uint32_t        rom_addr;
    int             rom_size;
    uint8_t         product;
    const device_t *flash;
    uint32_t        low_ram_top;
    uint32_t        shadow_base;
    uint32_t        wrap;
    uint8_t         dos_rom;
} ts3x00_model_t;

static const ts3x00_model_t ts3100_model = {
    .rom         = "roms/machines/ts3100/ts3100.bin",
    .nvr         = "roms/machines/ts3100/ts3100.nvr",
    .rom_addr    = 0x00080000,
    .rom_size    = 524288,
    .product     = TS3100_PRODUCT,
    .flash       = &amd_flash_29f040a_device,
    .low_ram_top = 0x00070000,
    .shadow_base = 0x000f0000,
    .wrap        = TS3100_WRAP,
    .dos_rom     = 1
};

static const ts3x00_model_t ts3200_model = {
    .rom         = "roms/machines/ts3200/ts3200.bin",
    .nvr         = "roms/machines/ts3200/ts3200.nvr",
    .rom_addr    = 0x00000000,
    .rom_size    = 1048576,
    .product     = TS3200_PRODUCT,
    .flash       = &amd_flash_29f080a_device,
    .low_ram_top = 0x000a0000,
    .shadow_base = 0x000e0000
};

/* A faster TS-3200 with more DRAM, and otherwise the same board. */
static const ts3x00_model_t ts3400_model = {
    .rom         = "roms/machines/ts3400/ts3400.bin",
    .nvr         = "roms/machines/ts3400/ts3400.nvr",
    .rom_addr    = 0x00000000,
    .rom_size    = 1048576,
    .product     = TS3400_PRODUCT,
    .flash       = &amd_flash_29f080a_device,
    .low_ram_top = 0x000a0000,
    .shadow_base = 0x000e0000
};

static void
ts3x00_board_init(ts3x00_t *j, const ts3x00_model_t *ts, i386ex_t *ex)
{
    sst_flash_cfg_t cfg          = { 0 };
    i386ex_cs_mem_t flash_target = { .map = ts3x00_flash_map, .remap = ts3x00_flash_remap };
    uint32_t        shadow_base  = ts->shadow_base;

    /* The flash replaces the BIOS ROM. */
    mem_mapping_disable(&bios_mapping);
    mem_mapping_disable(&bios_high_mapping);

    cfg.nvr_name  = machine_get_nvr_name_ex(machine);
    cfg.image     = rom;
    cfg.image_len = biosmask + 1;

    j->product = ts->product;

    j->flash = sst_flash_create(ts->flash, &cfg);
    sst_flash_set_write_protect(j->flash, !j->jp3);

    /*
       Lay the DRAM out as on the board. The shadow area uses the RAM left
       over below 1 MB: on the TS-3100 that is the top 64k of its 512k, on
       the TS-3200 the RAM at the shadow area's own address. With less RAM
       than the board has, the layout is cut short and, if there is no RAM
       left for it, there is no shadow area.
     */
    j->low_top     = MIN(ts->low_ram_top, mem_size * 1024);
    j->high_top    = (mem_size > 1024) ? (mem_size * 1024) : 0x00100000;
    j->shadow_base = shadow_base;
    j->shadow_size = 0x00100000 - shadow_base;
    if ((mem_size * 1024) >= 0x00100000)
        j->shadow = ram + shadow_base;
    else if ((mem_size * 1024) >= (ts->low_ram_top + j->shadow_size))
        j->shadow = ram + ts->low_ram_top;
    else
        j->shadow_size = 0;

    j->wrap    = ts->wrap;
    j->dos_rom = ts->dos_rom;

    if (j->wrap) {
        /* One mapping for the DRAM and shadow area at every 2 MB; the memory states pick them out. */
        mem_mapping_disable(&ram_low_mapping);
        mem_mapping_add(&j->ram_mapping, 0x00000000, TS3X00_ADDR_SPACE,
                        ts3x00_wrap_read, ts3x00_wrap_readw, ts3x00_wrap_readl,
                        ts3x00_wrap_write, ts3x00_wrap_writew, ts3x00_wrap_writel,
                        ram, MEM_MAPPING_INTERNAL, j);
        mem_mapping_set_mask(&j->ram_mapping, TS3100_RAM_MASK);
        ts3x00_wrap_pages(j);
    } else {
        mem_mapping_set_addr(&ram_low_mapping, 0x00000000, j->low_top);
        if (mem_size > 768)
            mem_mapping_disable(&ram_mid_mapping);
        if (j->shadow_size != 0)
            mem_mapping_add(&j->shadow_mapping, j->shadow_base, j->shadow_size,
                            ts3x00_shadow_read, ts3x00_shadow_readw, ts3x00_shadow_readl,
                            ts3x00_shadow_write, ts3x00_shadow_writew, ts3x00_shadow_writel,
                            j->shadow, MEM_MAPPING_INTERNAL, j);
    }

    /* The board asserts the flash's chip select there itself; the flash decodes its own address lines. */
    if (j->dos_rom)
        sst_flash_map(j->flash, &j->dos_rom_mapping, 0x00000000, TS3X00_ADDR_SPACE);

    ts3x00_update_mem(j, 0x00000000, TS3X00_ADDR_SPACE);

    /* The RTC and the board's registers only answer through CS5#, which is off after reset. */
    j->nvr = device_get_priv(&nvr_at_device);
    /* With a blank CMOS, the BIOS leaves the flash disk unconfigured. */
    nvr_load_default(j->nvr, ts->nvr);
    nvr_at_handler(0, 0x0070, j->nvr);

    flash_target.priv = j->flash;
    i386ex_csu_attach_mem(i386ex_get_csu(ex), I386EX_CSU_UCS, &flash_target);
    i386ex_csu_attach(i386ex_get_csu(ex), I386EX_CSU_UCS, ts3x00_ucs, j);
    if (j->wrap)
        i386ex_csu_attach(i386ex_get_csu(ex), 4, ts3x00_cs4, j);
    i386ex_csu_attach(i386ex_get_csu(ex), 5, ts3x00_cs5, j);

    ts3x00_board = j;
}

static int
ts3x00_init(const machine_t *model, const ts3x00_model_t *ts)
{
    ts3x00_t *j;
    i386ex_t *ex;
    int       ret;

    ret = bios_load_linear(ts->rom, ts->rom_addr, ts->rom_size, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    device_add(&port_92_device);

    /* Before the 386EX, so that the LED shows its pin's state from reset. */
    device_add_params(&board_led_device, (void *) 1);

    ex = device_add_params(&i386ex_device, (void *) (I386EX_SIO0 | I386EX_SIO1));
    /* The board logic resets the processor when the watchdog times out. */
    i386ex_set_wdtout(ex, i386ex_wdtout_reset, NULL);
    /* The board logic drives COMCLK with the usual UART clock. */
    i386ex_set_comclk(ex, 1843200.0);
    /* The 386EX's UARTs are the only serial ports on the board. */
    serial_set_next_inst(SERIAL_MAX - 1);

    j = device_add(&ts3x00_device);
    ts3x00_board_init(j, ts, ex);

    /* The keyboard controller is on a TS-9500, which the BIOS has to find to connect IRQ1. */
    device_add(&ts9500_device);

    return ret;
}

int
machine_at_ts3100_init(const machine_t *model)
{
    return ts3x00_init(model, &ts3100_model);
}

int
machine_at_ts3200_init(const machine_t *model)
{
    return ts3x00_init(model, &ts3200_model);
}

int
machine_at_ts3400_init(const machine_t *model)
{
    return ts3x00_init(model, &ts3400_model);
}
