/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Tandy 2000 emulation ported from MAME.
 *
 * Authors: Curt Coder
 *          WNT50
 *
 *          Copyright 2010-2026 Curt Coder.
 *          Copyright 2026 WNT50.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include "cpu.h"
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/device.h>
#include <86box/i8018x.h>
#include <86box/machine.h>
#include <86box/rom.h>
#include <86box/pic.h>
#include <86box/pit.h>

/* Character RAM sits in the low byte lane of the upper memory window,
   the BIOS writes it with one byte every two addresses. */
typedef struct tandy2k_t {
    mem_mapping_t ram_map;
    uint8_t       port0;  /* speaker, clock gates, FDC reset */
    uint8_t       dmamux; /* DMA request multiplexer */
    uint8_t       ppi_ctrl;
    uint8_t       ppi_pa;
    uint8_t       ppi_pc;
} tandy2k_t;

static tandy2k_t tandy2k;

static uint8_t
tandy2k_read(uint16_t addr, void *priv)
{
    switch (addr & 0x000f) {
        case 0x00:
            return tandy2k.port0;
        case 0x02:
            return tandy2k.dmamux;
        case 0x04:
            /* Reading the floppy terminal count acknowledges it. */
            return 0x00;
        default:
            break;
    }

    return 0xff;
}

static void
tandy2k_write(uint16_t addr, uint8_t val, void *priv)
{
    switch (addr & 0x000f) {
        case 0x00:
            tandy2k.port0 = val;
            break;
        case 0x02:
            tandy2k.dmamux = val;
            break;
        default:
            break;
    }
}

static uint8_t
ppi_read(uint16_t addr, void *priv)
{
    switch (addr & 0x0006) {
        case 0x00:
            return (tandy2k.ppi_ctrl & 0x10) ? 0xff : tandy2k.ppi_pa;
        case 0x02:
            /* Port C bits 2-1 select what port B reads back. */
            return (((tandy2k.ppi_pc >> 1) & 0x03) == 0x02) ? 0x03 : 0xff;
        case 0x04:
            return tandy2k.ppi_pc;
        case 0x06:
            return tandy2k.ppi_ctrl;
        default:
            break;
    }

    return 0xff;
}

static void
ppi_write(uint16_t addr, uint8_t val, void *priv)
{
    uint8_t bit;

    switch (addr & 0x0006) {
        case 0x00:
            tandy2k.ppi_pa = val;
            break;
        case 0x04:
            tandy2k.ppi_pc = val;
            break;
        case 0x06:
            if (val & 0x80)
                tandy2k.ppi_ctrl = val;
            else {
                bit = (uint8_t) (1u << ((val >> 1) & 0x07));
                if (val & 0x01)
                    tandy2k.ppi_pc |= bit;
                else
                    tandy2k.ppi_pc &= (uint8_t) ~bit;
            }
            break;
        default:
            break;
    }
}

/* The chip's cascade ack returns the vector the external 8259 puts on the bus. */
static int
tandy2k_8259(void *priv)
{
    return pic_irq_ack_instance((pic_t *) priv);
}

/* The 8259 output callback carries only the level, so each line needs its own
   adapter to name the 80186 interrupt input it drives. */
static void
tandy2k_icu_int0(void *priv, int level)
{
    i8018x_pcb_icu_int((struct i8018x_pcb_t *) priv, 0, level);
}

static void
tandy2k_icu_int1(void *priv, int level)
{
    i8018x_pcb_icu_int((struct i8018x_pcb_t *) priv, 1, level);
}

int
machine_tandy2000_init(const machine_t *model)
{
    int ret;

    /* UCS covers F8000-FFFFF and is split by A14: the lower 16 KB
       is the character generator, the upper one is the boot ROM. */
    ret = bios_load_interleaved("roms/machines/tandy2000/484a00.u48",
                                "roms/machines/tandy2000/474600.u47",
                                0x000fe000, 8192, 0);

    if (bios_only || !ret)
        return ret;

    /* RAM is contiguous up to the top, unlike the PC layout that stops at
       A0000h; without this the BIOS sizes anything above 640 KB as absent. */
    if (mem_size > 640) {
        mem_set_mem_state_both(0xa0000, (uint32_t) (mem_size - 640) << 10,
                               MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
        mem_mapping_add(&tandy2k.ram_map, 0xa0000,
                        (uint32_t) (mem_size - 640) << 10,
                        mem_read_ram, mem_read_ramw, mem_read_raml,
                        mem_write_ram, mem_write_ramw, mem_write_raml,
                        NULL, MEM_MAPPING_INTERNAL, NULL);
    }

    pic_init_tandy2k();

    device_add(&tandy_2000_video_device);

    /* The board clocks the 8253 at 2 MHz and decodes its registers on
       even addresses, so replace the default 40h-43h mapping. */
    pit_t *pit = pit_common_init(PIT_8253, NULL, NULL);
    pit_set_clock(2000000);
    pit_handler(0, 0x0040, 0x0004, pit);
    io_sethandler(0x0040, 0x0008, pit_read_even, NULL, NULL,
                  pit_write_even, NULL, NULL, pit);

    tandy2k.ppi_ctrl = 0x9b;
    io_sethandler(0x0050, 0x0008, ppi_read, NULL, NULL,
                  ppi_write, NULL, NULL, NULL);

    /* Both 8259As are slaves of the 80186's internal controller:
       the first drives INT0 and the second INT1, and each puts
       own vector on the bus during the acknowledge cycles. */
    struct i8018x_pcb_t *pcb = device_add(&i8018x_pcb_device);
    i8018x_pcb_set_ext_ack(pcb, 0, tandy2k_8259, &pic);
    i8018x_pcb_set_ext_ack(pcb, 1, tandy2k_8259, &pic2);
    pic_set_output_callback(&pic, tandy2k_icu_int0, pcb);
    pic_set_output_callback(&pic2, tandy2k_icu_int1, pcb);

    io_sethandler(0x0000, 0x0010, tandy2k_read, NULL, NULL,
                  tandy2k_write, NULL, NULL, NULL);

    return ret;
}
