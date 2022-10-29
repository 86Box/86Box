/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Toshiba T3100e.
 *
 *          The Toshiba 3100e is a 286-based portable.
 *
 *          To bring up the BIOS setup screen hold down the 'Fn' key
 *          on booting.
 *
 *          Memory management
 *          ~~~~~~~~~~~~~~~~~
 *
 *          Motherboard memory is divided into:
 *          - Conventional memory: Either 512k or 640k
 *          - Upper memory:        Either 512k or 384k, depending on
 *                         amount of conventional memory.
 *                         Upper memory can be used as EMS or XMS.
 *          - High memory:         0-4Mb, depending on RAM installed.
 *                         The BIOS setup screen allows some or
 *                         all of this to be used as EMS; the
 *                         remainder is XMS.
 *
 *          Additional memory (either EMS or XMS) can also be provided
 *          by ISA expansion cards.
 *
 *          Under test in PCem, the BIOS will boot with up to 65368Kb
 *          of memory in total (16Mb less 16k). However it will give
 *          an error with RAM sizes above 8Mb, if any of the high
 *          memory is allocated as EMS, because the builtin EMS page
 *          registers can only access up to 8Mb.
 *
 *          Memory is controlled by writes to I/O port 8084h:
 *            Bit 7: Always 0  }
 *            Bit 6: Always 1  } These bits select which motherboard
 *            Bit 5: Always 0  } function to access.
 *            Bit 4: Set to treat upper RAM as XMS
 *            Bit 3: Enable external RAM boards?
 *            Bit 2: Set for 640k conventional memory, clear for 512k
 *            Bit 1: Enable RAM beyond 1Mb.
 *            Bit 0: Enable EMS.
 *
 *          The last value written to this port is saved at 0040:0093h,
 *          and in CMOS memory at offset 0x37. If the top bit of the
 *          CMOS byte is set, then high memory is being provided by
 *          an add-on card rather than the mainboard; accordingly,
 *          the BIOS will not allow high memory to be used as EMS.
 *
 *          EMS is controlled by 16 page registers:
 *
 *          Page mapped at        0xD000    0xD400    0xD800    0xDC00
 *          ------------------------------------------------------
 *          Pages 0x00-0x7F         0x208    0x4208    0x8208    0xc208
 *          Pages 0x80-0xFF         0x218    0x4218    0x8218    0xc218
 *          Pages 0x100-0x17F     0x258    0x4258    0x8258    0xc258
 *          Pages 0x180-0x1FF     0x268    0x4268    0x8268    0xc268
 *
 *          The value written has bit 7 set to enable EMS, reset to
 *          disable it.
 *
 *          So:
 *          OUT 0x208,  0x80  will page in the first 16k page at 0xD0000.
 *          OUT 0x208,  0x00  will page out EMS, leaving nothing at 0xD0000.
 *          OUT 0x4208, 0x80  will page in the first 16k page at 0xD4000.
 *          OUT 0x218,  0x80  will page in the 129th 16k page at 0xD0000.
 *          etc.
 *
 *          To use EMS from DOS, you will need the Toshiba EMS driver
 *          (TOSHEMM.ZIP). This supports the above system, plus further
 *          ranges of ports at 0x_2A8, 0x_2B8, 0x_2C8.
 *
 *          Features not implemented:
 *          > Four video fonts.
 *          > BIOS-controlled mapping of serial ports to IRQs.
 *          > Custom keyboard controller. This has a number of extra
 *            commands in the 0xB0-0xBC range, for such things as turbo
 *            on/off, and switching the keyboard between AT and PS/2
 *            modes. Currently I have only implemented command 0xBB,
 *            so that self-test completes successfully. Commands include:
 *
 *            0xB0:   Turbo on
 *            0xB1:   Turbo off
 *            0xB2:   Internal display on?
 *            0xB3:   Internal display off?
 *            0xB5:   Get settings byte (bottom bit is color/mono setting)
 *            0xB6:   Set settings byte
 *            0xB7:   Behave as 101-key PS/2 keyboard
 *            0xB8:   Behave as 84-key AT keyboard
 *            0xBB:   Return a byte, bit 2 is Fn key state, other bits unknown.
 *
 *          The other main I/O port needed to POST is:
 *            0x8084: System control.
 *            Top 3 bits give command, bottom 5 bits give parameters.
 *            000 => set serial port IRQ / addresses
 *            bit 4:    IRQ5 serial port base: 1 => 0x338, 0 => 0x3E8
 *            bits 3, 2, 0 specify serial IRQs for COM1, COM2, COM3:
 *                          00 0 => 4, 3, 5
 *                          00 1 => 4, 5, 3
 *                          01 0 => 3, 4, 5
 *                          01 1 => 3, 5, 4
 *                          10 0 => 4, -, 3
 *                          10 1 => 3, -, 4
 *            010 => set memory mappings
 *               bit 4 set if upper RAM is XMS
 *               bit 3 enable add-on memory boards beyond 5Mb?
 *                         bit 2 set for 640k sysram, clear for 512k sysram
 *                         bit 1 enable mainboard XMS
 *                         bit 0 enable mainboard EMS
 *            100 => set parallel mode / LCD settings
 *                         bit 4 set for bidirectional parallel port
 *                         bit 3 set to disable internal CGA
 *                         bit 2 set for single-pixel LCD font
 *                         bits 0,1 for display font
 *
 *
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Sarah Walker, <tommowalker@tommowalker.co.uk>
 *
 *          Copyright 2017,2018 Fred N. van Kempen.
 *          Copyright 2016-2018 Miran Grca.
 *          Copyright 2008-2018 Sarah Walker.
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
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/mouse.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/keyboard.h>
#include <86box/rom.h>
#include "cpu.h"
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/machine.h>
#include <86box/m_at_t3100e.h>

extern uint8_t *ram; /* Physical RAM */

void at_init();

/* The T3100e motherboard can (and does) dynamically reassign RAM between
 * conventional, XMS and EMS. This translates to monkeying with the mappings.
 */

extern mem_mapping_t base_mapping;

extern mem_mapping_t ram_low_mapping; /* This is to switch conventional RAM
                                       * between 512k and 640k */

extern mem_mapping_t ram_mid_mapping; /* This will not be used */

extern mem_mapping_t ram_high_mapping; /* This is RAM beyond 1Mb if any */

extern uint8_t *ram;

static unsigned t3100e_ems_page_reg[] = {
    0x208,
    0x4208,
    0x8208,
    0xc208, /* The first four map the first 2Mb */
    /* of RAM into the page frame */
    0x218,
    0x4218,
    0x8218,
    0xc218, /* The next four map the next 2Mb */
    /* of RAM */
    0x258,
    0x4258,
    0x8258,
    0xc258, /* and so on. */
    0x268,
    0x4268,
    0x8268,
    0xc268,
};

struct t3100e_ems_regs {
    uint8_t       page[16];
    mem_mapping_t mapping[4];
    uint32_t      page_exec[4]; /* Physical location of memory pages */
    uint32_t      upper_base;   /* Start of upper RAM */
    uint8_t       upper_pages;  /* Pages of EMS available from upper RAM */
    uint8_t       upper_is_ems; /* Upper RAM is EMS? */
    mem_mapping_t upper_mapping;
    uint8_t       notify; /* Notification from keyboard controller */
    uint8_t       turbo;  /* 0 for 6MHz, else full speed */
    uint8_t       mono;   /* Emulates PC/AT 'mono' motherboard switch */
    /* Bit 0 is 0 for colour, 1 for mono */
} t3100e_ems;

void t3100e_ems_out(uint16_t addr, uint8_t val, void *p);

#ifdef ENABLE_T3100E_LOG
int t3100e_do_log = ENABLE_T3100E_LOG;

static void
t3100e_log(const char *fmt, ...)
{
    va_list ap;

    if (t3100e_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define t3100e_log(fmt, ...)
#endif

/* Given a memory address (which ought to be in the page frame at 0xD0000),
 * which page does it relate to? */
static int
addr_to_page(uint32_t addr)
{
    if ((addr & 0xF0000) == 0xD0000) {
        return ((addr >> 14) & 3);
    }
    return -1;
}

/* And vice versa: Given a page slot, which memory address does it
 * correspond to? */
static uint32_t
page_to_addr(int pg)
{
    return 0xD0000 + ((pg & 3) * 16384);
}

/* Given an EMS page ID, return its physical address in RAM. */
uint32_t
t3100e_ems_execaddr(struct t3100e_ems_regs *regs,
                    int pg, uint16_t val)
{
    uint32_t addr;

    if (!(val & 0x80))
        return 0; /* Bit 7 reset => not mapped */

    val &= 0x7F;
    val += (0x80 * (pg >> 2)); /* The high bits of the register bank */
    /* are used to extend val to allow up */
    /* to 8Mb of EMS to be accessed */

    /* Is it in the upper memory range? */
    if (regs->upper_is_ems) {
        if (val < regs->upper_pages) {
            addr = regs->upper_base + 0x4000 * val;
            return addr;
        }
        val -= regs->upper_pages;
    }
    /* Otherwise work down from the top of high RAM (so, the more EMS,
     * the less XMS) */
    if ((val * 0x4000) + 0x100000 >= (mem_size * 1024)) {
        return 0; /* Not enough high RAM for this page */
    }
    /* High RAM found */
    addr = (mem_size * 1024) - 0x4000 * (val + 1);

    return addr;
}

/* The registers governing the EMS ports are in rather a nonintuitive order */
static int
port_to_page(uint16_t addr)
{
    switch (addr) {
        case 0x208:
            return 0;
        case 0x4208:
            return 1;
        case 0x8208:
            return 2;
        case 0xC208:
            return 3;
        case 0x218:
            return 4;
        case 0x4218:
            return 5;
        case 0x8218:
            return 6;
        case 0xC218:
            return 7;
        case 0x258:
            return 8;
        case 0x4258:
            return 9;
        case 0x8258:
            return 10;
        case 0xC258:
            return 11;
        case 0x268:
            return 12;
        case 0x4268:
            return 13;
        case 0x8268:
            return 14;
        case 0xC268:
            return 15;
    }
    return -1;
}

/* Used to dump the memory mapping table, for debugging
void dump_mappings()
{
        mem_mapping_t *mm = base_mapping.next;

        if (!t3100e_log) return;
        while (mm)
        {
                const char *name = "";
                uint32_t offset = (uint32_t)(mm->exec - ram);

                if (mm == &ram_low_mapping ) name = "LOW ";
                if (mm == &ram_mid_mapping ) name = "MID ";
                if (mm == &ram_high_mapping) name = "HIGH";
                if (mm == &t3100e_ems.upper_mapping) name = "UPPR";
                if (mm == &t3100e_ems.mapping[0])
                {
                        name = "EMS0";
                        offset = t3100e_ems.page_exec[0];
                }
                if (mm == &t3100e_ems.mapping[1])
                {
                        name = "EMS1";
                        offset = t3100e_ems.page_exec[1];
                }
                if (mm == &t3100e_ems.mapping[2])
                {
                        name = "EMS2";
                        offset = t3100e_ems.page_exec[2];
                }
                if (mm == &t3100e_ems.mapping[3])
                {
                        name = "EMS3";
                        offset = t3100e_ems.page_exec[3];
                }

                t3100e_log("  %p | base=%05x size=%05x %c @ %06x %s\n", mm,
                        mm->base, mm->size, mm->enable ? 'Y' : 'N',
                        offset, name);

                mm = mm->next;
        }
}*/

void
t3100e_map_ram(uint8_t val)
{
    int     n;
    int32_t upper_len;

#ifdef ENABLE_T3100E_LOG
    t3100e_log("OUT 0x8084, %02x [ set memory mapping :", val | 0x40);
    if (val & 1)
        t3100e_log("ENABLE_EMS ");
    if (val & 2)
        t3100e_log("ENABLE_XMS ");
    if (val & 4)
        t3100e_log("640K ");
    if (val & 8)
        t3100e_log("X8X ");
    if (val & 16)
        t3100e_log("UPPER_IS_XMS ");
    t3100e_log("\n");
#endif

    /* Bit 2 controls size of conventional memory */
    if (val & 4) {
        t3100e_ems.upper_base  = 0xA0000;
        t3100e_ems.upper_pages = 24;
    } else {
        t3100e_ems.upper_base  = 0x80000;
        t3100e_ems.upper_pages = 32;
    }
    upper_len = t3100e_ems.upper_pages * 16384;

    mem_mapping_set_addr(&ram_low_mapping, 0, t3100e_ems.upper_base);
    /* Bit 0 set if upper RAM is EMS */
    t3100e_ems.upper_is_ems = (val & 1);

    /* Bit 1 set if high RAM is enabled */
    if (val & 2) {
        mem_mapping_enable(&ram_high_mapping);
    } else {
        mem_mapping_disable(&ram_high_mapping);
    }

    /* Bit 4 set if upper RAM is mapped to high memory
     * (and bit 1 set if XMS enabled) */
    if ((val & 0x12) == 0x12) {
        mem_mapping_set_addr(&t3100e_ems.upper_mapping,
                             mem_size * 1024,
                             upper_len);
        mem_mapping_enable(&t3100e_ems.upper_mapping);
        mem_mapping_set_exec(&t3100e_ems.upper_mapping, ram + t3100e_ems.upper_base);
    } else {
        mem_mapping_disable(&t3100e_ems.upper_mapping);
    }
    /* Recalculate EMS mappings */
    for (n = 0; n < 4; n++) {
        t3100e_ems_out(t3100e_ems_page_reg[n], t3100e_ems.page[n],
                       &t3100e_ems);
    }

    // dump_mappings();
}

void
t3100e_notify_set(uint8_t value)
{
    t3100e_ems.notify = value;
}

void
t3100e_mono_set(uint8_t value)
{
    t3100e_ems.mono = value;
}

uint8_t
t3100e_mono_get(void)
{
    return t3100e_ems.mono;
}

void
t3100e_turbo_set(uint8_t value)
{
    t3100e_ems.turbo = value;
    if (!value) {
        cpu_dynamic_switch(0); /* 286/6 */
    } else {
        cpu_dynamic_switch(cpu);
    }
}

uint8_t
t3100e_sys_in(uint16_t addr, void *p)
{
    struct t3100e_ems_regs *regs = (struct t3100e_ems_regs *) p;

    /* The low 4 bits always seem to be 0x0C. The high 4 are a
     * notification sent by the keyboard controller when it detects
     * an [Fn] key combination */
    t3100e_log("IN 0x8084\n");
    return 0x0C | (regs->notify << 4);
}

/* Handle writes to the T3100e system control port at 0x8084 */
void
t3100e_sys_out(uint16_t addr, uint8_t val, void *p)
{
    //    struct t3100e_ems_regs *regs = (struct t3100e_ems_regs *)p;

    switch (val & 0xE0) {
        case 0x00: /* Set serial port IRQs. Not implemented */
            t3100e_log("OUT 0x8084, %02x [ set serial port IRQs]\n", val);
            break;
        case 0x40: /* Set RAM mappings. */
            t3100e_map_ram(val & 0x1F);
            break;

        case 0x80: /* Set video options. */
            t3100e_video_options_set(val & 0x1F);
            break;

            /* Other options not implemented. */
        default:
            t3100e_log("OUT 0x8084, %02x\n", val);
            break;
    }
}

uint8_t
t3100e_config_get(void)
{
    /* The byte returned:
            Bit 7: Set if internal plasma display enabled
            Bit 6: Set if running at 6MHz, clear at full speed
            Bit 5: Always 1?
            Bit 4: Set if the FD2MB jumper is present (internal floppy is ?tri-mode)
            Bit 3: Clear if the FD2 jumper is present (two internal floppies)
            Bit 2: Set if the internal drive is A:, clear if B:
            Bit 1: Set if the parallel port is configured as a floppy connector
                   for the second drive.
            Bit 0: Set if the F2HD jumper is present (internal floppy is 720k)
     */
    uint8_t value = 0x28; /* Start with bits 5 and 3 set. */

    int type_a = fdd_get_type(0);
    int type_b = fdd_get_type(1);
    int prt_switch; /* External drive type: 0=> none, 1=>A, 2=>B */

    /* Get display setting */
    if (t3100e_display_get())
        value |= 0x80;
    if (!t3100e_ems.turbo)
        value |= 0x40;

    /* Try to determine the floppy types.*/

    prt_switch = (type_b ? 2 : 0);
    switch (type_a) {
            /* Since a T3100e cannot have an internal 5.25" drive, mark 5.25" A: drive as
             * being external, and set the internal type based on type_b. */
        case 1:             /* 360k */
        case 2:             /* 1.2Mb */
        case 3:             /* 1.2Mb RPMx2*/
            prt_switch = 1; /* External drive is A: */
            switch (type_b) {
                case 1: /* 360k */
                case 4:
                    value |= 1;
                    break; /* 720k */
                case 6:
                    value |= 0x10;
                    break; /* Tri-mode */
                           /* All others will be treated as 1.4M */
            }
            break;
        case 4:
            value |= 0x01; /* 720k */
            if (type_a == type_b) {
                value &= (~8);  /* Two internal drives */
                prt_switch = 0; /* No external drive */
            }
            break;
        case 5: /* 1.4M */
        case 7: /* 2.8M */
            if (type_a == type_b) {
                value &= (~8);  /* Two internal drives */
                prt_switch = 0; /* No external drive */
            }
            break;
        case 6: /* 3-mode */
            value |= 0x10;
            if (type_a == type_b) {
                value &= (~8);  /* Two internal drives */
                prt_switch = 0; /* No external drive */
            }
            break;
    } /* End switch */
    switch (prt_switch) {
        case 0:
            value |= 4;
            break; /* No external floppy */
        case 1:
            value |= 2;
            break; /* External floppy is A: */
        case 2:
            value |= 6;
            break; /* External floppy is B: */
    }
    return value;
}

/* Read EMS page register */
uint8_t
t3100e_ems_in(uint16_t addr, void *p)
{
    struct t3100e_ems_regs *regs = (struct t3100e_ems_regs *) p;

    int page = port_to_page(addr);
    if (page >= 0)
        return regs->page[page];
    else {
        fatal("t3100e_ems_in(): invalid address");
        return 0xff;
    }
}

/* Write EMS page register */
void
t3100e_ems_out(uint16_t addr, uint8_t val, void *p)
{
    struct t3100e_ems_regs *regs = (struct t3100e_ems_regs *) p;
    int                     pg   = port_to_page(addr);

    if (pg == -1)
        return;

    regs->page_exec[pg & 3] = t3100e_ems_execaddr(regs, pg, val);
    t3100e_log("EMS: page %d %02x -> %02x [%06x]\n",
               pg, regs->page[pg], val, regs->page_exec[pg & 3]);
    regs->page[pg] = val;

    pg &= 3;
    /* Bit 7 set if page is enabled, reset if page is disabled */
    if (regs->page_exec[pg]) {
        t3100e_log("Enabling EMS RAM at %05x\n",
                   page_to_addr(pg));
        mem_mapping_enable(&regs->mapping[pg]);
        mem_mapping_set_exec(&regs->mapping[pg], ram + regs->page_exec[pg]);
    } else {
        t3100e_log("Disabling EMS RAM at %05x\n",
                   page_to_addr(pg));
        mem_mapping_disable(&regs->mapping[pg]);
    }
}

/* Read RAM in the EMS page frame */
static uint8_t
ems_read_ram(uint32_t addr, void *priv)
{
    struct t3100e_ems_regs *regs = (struct t3100e_ems_regs *) priv;
    int                     pg   = addr_to_page(addr);

    if (pg < 0)
        return 0xFF;
    addr = regs->page_exec[pg] + (addr & 0x3FFF);
    return ram[addr];
}

static uint16_t
ems_read_ramw(uint32_t addr, void *priv)
{
    struct t3100e_ems_regs *regs = (struct t3100e_ems_regs *) priv;
    int                     pg   = addr_to_page(addr);

    if (pg < 0)
        return 0xFFFF;
    // t3100e_log("ems_read_ramw addr=%05x ", addr);
    addr = regs->page_exec[pg] + (addr & 0x3FFF);
    // t3100e_log("-> %06x val=%04x\n", addr, *(uint16_t *)&ram[addr]);
    return *(uint16_t *) &ram[addr];
}

static uint32_t
ems_read_raml(uint32_t addr, void *priv)
{
    struct t3100e_ems_regs *regs = (struct t3100e_ems_regs *) priv;
    int                     pg   = addr_to_page(addr);

    if (pg < 0)
        return 0xFFFFFFFF;
    addr = regs->page_exec[pg] + (addr & 0x3FFF);
    return *(uint32_t *) &ram[addr];
}

/* Write RAM in the EMS page frame */
static void
ems_write_ram(uint32_t addr, uint8_t val, void *priv)
{
    struct t3100e_ems_regs *regs = (struct t3100e_ems_regs *) priv;
    int                     pg   = addr_to_page(addr);

    if (pg < 0)
        return;
    addr      = regs->page_exec[pg] + (addr & 0x3FFF);
    ram[addr] = val;
}

static void
ems_write_ramw(uint32_t addr, uint16_t val, void *priv)
{
    struct t3100e_ems_regs *regs = (struct t3100e_ems_regs *) priv;
    int                     pg   = addr_to_page(addr);

    if (pg < 0)
        return;
    // t3100e_log("ems_write_ramw addr=%05x ", addr);
    addr = regs->page_exec[pg] + (addr & 0x3FFF);
    // t3100e_log("-> %06x val=%04x\n", addr, val);

    *(uint16_t *) &ram[addr] = val;
}

static void
ems_write_raml(uint32_t addr, uint32_t val, void *priv)
{
    struct t3100e_ems_regs *regs = (struct t3100e_ems_regs *) priv;
    int                     pg   = addr_to_page(addr);

    if (pg < 0)
        return;
    addr                     = regs->page_exec[pg] + (addr & 0x3FFF);
    *(uint32_t *) &ram[addr] = val;
}

/* Read RAM in the upper area. This is basically what the 'remapped'
 * mapping in mem.c does, except that the upper area can move around */
static uint8_t
upper_read_ram(uint32_t addr, void *priv)
{
    struct t3100e_ems_regs *regs = (struct t3100e_ems_regs *) priv;

    addr = (addr - (1024 * mem_size)) + regs->upper_base;
    return ram[addr];
}

static uint16_t
upper_read_ramw(uint32_t addr, void *priv)
{
    struct t3100e_ems_regs *regs = (struct t3100e_ems_regs *) priv;

    addr = (addr - (1024 * mem_size)) + regs->upper_base;
    return *(uint16_t *) &ram[addr];
}

static uint32_t
upper_read_raml(uint32_t addr, void *priv)
{
    struct t3100e_ems_regs *regs = (struct t3100e_ems_regs *) priv;

    addr = (addr - (1024 * mem_size)) + regs->upper_base;
    return *(uint32_t *) &ram[addr];
}

static void
upper_write_ram(uint32_t addr, uint8_t val, void *priv)
{
    struct t3100e_ems_regs *regs = (struct t3100e_ems_regs *) priv;

    addr      = (addr - (1024 * mem_size)) + regs->upper_base;
    ram[addr] = val;
}

static void
upper_write_ramw(uint32_t addr, uint16_t val, void *priv)
{
    struct t3100e_ems_regs *regs = (struct t3100e_ems_regs *) priv;

    addr                     = (addr - (1024 * mem_size)) + regs->upper_base;
    *(uint16_t *) &ram[addr] = val;
}

static void
upper_write_raml(uint32_t addr, uint32_t val, void *priv)
{
    struct t3100e_ems_regs *regs = (struct t3100e_ems_regs *) priv;

    addr                     = (addr - (1024 * mem_size)) + regs->upper_base;
    *(uint32_t *) &ram[addr] = val;
}

int
machine_at_t3100e_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/t3100e/t3100e.rom",
                           0x000f0000, 65536, 0);

    if (bios_only || !ret)
        return ret;

    int pg;

    memset(&t3100e_ems, 0, sizeof(t3100e_ems));

    machine_at_common_ide_init(model);

    device_add(&keyboard_at_toshiba_device);

    if (fdc_type == FDC_INTERNAL) {
        device_add(&fdc_at_device);
    }

    /* Hook up system control port */
    io_sethandler(0x8084, 0x0001,
                  t3100e_sys_in, NULL, NULL,
                  t3100e_sys_out, NULL, NULL, &t3100e_ems);

    /* Start monitoring all 16 EMS registers */
    for (pg = 0; pg < 16; pg++) {
        io_sethandler(t3100e_ems_page_reg[pg], 0x0001,
                      t3100e_ems_in, NULL, NULL,
                      t3100e_ems_out, NULL, NULL, &t3100e_ems);
    }

    /* Map the EMS page frame */
    for (pg = 0; pg < 4; pg++) {
        t3100e_log("Adding memory map at %x for page %d\n", page_to_addr(pg), pg);
        mem_mapping_add(&t3100e_ems.mapping[pg],
                        page_to_addr(pg), 16384,
                        ems_read_ram, ems_read_ramw, ems_read_raml,
                        ems_write_ram, ems_write_ramw, ems_write_raml,
                        NULL, MEM_MAPPING_EXTERNAL,
                        &t3100e_ems);
        /* Start them all off disabled */
        mem_mapping_disable(&t3100e_ems.mapping[pg]);
    }
    /* Mapping for upper RAM when in use as XMS*/
    mem_mapping_add(&t3100e_ems.upper_mapping, mem_size * 1024, 384 * 1024,
                    upper_read_ram, upper_read_ramw, upper_read_raml,
                    upper_write_ram, upper_write_ramw, upper_write_raml,
                    NULL, MEM_MAPPING_INTERNAL, &t3100e_ems);
    mem_mapping_disable(&t3100e_ems.upper_mapping);

    device_add(&t3100e_device);

    return ret;
}
