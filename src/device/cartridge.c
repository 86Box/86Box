/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the PCjr cartridge emulation.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2021 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/mem.h>
#include <86box/machine.h>
#include <86box/cartridge.h>

typedef struct
{
    uint8_t *buf;
    uint32_t base;
} cart_t;

char cart_fns[2][512];

static cart_t carts[2];

static mem_mapping_t cart_mappings[2];

#ifdef ENABLE_CARTRIDGE_LOG
int cartridge_do_log = ENABLE_CARTRIDGE_LOG;

static void
cartridge_log(const char *fmt, ...)
{
    va_list ap;

    if (cartridge_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define cartridge_log(fmt, ...)
#endif

static uint8_t
cart_read(uint32_t addr, void *priv)
{
    cart_t *dev = (cart_t *) priv;

    return dev->buf[addr - dev->base];
}

static void
cart_load_error(int drive, char *fn)
{
    cartridge_log("Cartridge: could not load '%s'\n", fn);
    memset(cart_fns[drive], 0, sizeof(cart_fns[drive]));
    ui_sb_update_icon_state(SB_CARTRIDGE | drive, 1);
}

static void
cart_image_close(int drive)
{
    if (carts[drive].buf != NULL) {
        free(carts[drive].buf);
        carts[drive].buf = NULL;
    }

    carts[drive].base = 0x00000000;

    mem_mapping_disable(&cart_mappings[drive]);
}

static void
cart_image_load(int drive, char *fn)
{
    FILE    *f;
    uint32_t size;
    uint32_t base = 0x00000000;

    cart_image_close(drive);

    f = fopen(fn, "rb");
    if (fseek(f, 0, SEEK_END) == -1)
        fatal("cart_image_load(): Error seeking to the end of the file\n");
    size = ftell(f);
    if (size < 0x1200) {
        cartridge_log("cart_image_load(): File size %i is too small\n", size);
        cart_load_error(drive, fn);
        return;
    }
    if (size & 0x00000fff) {
        size -= 0x00000200;
        fseek(f, 0x000001ce, SEEK_SET);
        (void) !fread(&base, 1, 2, f);
        base <<= 4;
        fseek(f, 0x00000200, SEEK_SET);
        carts[drive].buf = (uint8_t *) malloc(size);
        memset(carts[drive].buf, 0x00, size);
        (void) !fread(carts[drive].buf, 1, size, f);
        fclose(f);
    } else {
        base = drive ? 0xe0000 : 0xd0000;
        if (size == 32768)
            base += 0x8000;
        fseek(f, 0x00000000, SEEK_SET);
        carts[drive].buf = (uint8_t *) malloc(size);
        memset(carts[drive].buf, 0x00, size);
        (void) !fread(carts[drive].buf, 1, size, f);
        fclose(f);
    }

    cartridge_log("cart_image_load(): %s at %08X-%08X\n", fn, base, base + size - 1);
    carts[drive].base = base;
    mem_mapping_set_addr(&cart_mappings[drive], base, size);
    mem_mapping_set_exec(&cart_mappings[drive], carts[drive].buf);
    mem_mapping_set_p(&cart_mappings[drive], &(carts[drive]));
}

static void
cart_load_common(int drive, char *fn, uint8_t hard_reset)
{
    FILE *f;

    cartridge_log("Cartridge: loading drive %d with '%s'\n", drive, fn);

    if (!fn)
        return;
    f = plat_fopen(fn, "rb");
    if (f) {
        fclose(f);
        strcpy(cart_fns[drive], fn);
        cart_image_load(drive, cart_fns[drive]);
        /* On the real PCjr, inserting a cartridge causes a reset
           in order to boot from the cartridge. */
        if (!hard_reset)
            resetx86();
    } else
        cart_load_error(drive, fn);
}

void
cart_load(int drive, char *fn)
{
    cart_load_common(drive, fn, 0);
}

void
cart_close(int drive)
{
    cartridge_log("Cartridge: closing drive %d\n", drive);

    cart_image_close(drive);
    cart_fns[drive][0] = 0;
    ui_sb_update_icon_state(SB_CARTRIDGE | drive, 1);
}

void
cart_reset(void)
{
    int i;

    cart_image_close(1);
    cart_image_close(0);

    if (!machine_has_cartridge(machine))
        return;

    for (i = 0; i < 2; i++) {
        mem_mapping_add(&cart_mappings[i], 0x000d0000, 0x00002000,
                        cart_read, NULL, NULL,
                        NULL, NULL, NULL,
                        NULL, MEM_MAPPING_EXTERNAL, NULL);
        mem_mapping_disable(&cart_mappings[i]);
    }

    cart_load_common(0, cart_fns[0], 1);
    cart_load_common(1, cart_fns[1], 1);
}
