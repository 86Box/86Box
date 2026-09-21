/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the PCjr cartridge emulation.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2021 Miran Grca.
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
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/mem.h>
#include <86box/machine.h>
#include <86box/cartridge.h>

typedef struct cart_t {
    uint8_t *buf;
    uint32_t base;
    uint32_t size;
} cart_t;

char  cart_fns[2][MAX_IMAGE_PATH_LEN];
char *cart_image_history[2][CART_IMAGE_HISTORY];

static cart_t carts[2];

static mem_mapping_t cart_mappings[2];
static uint8_t       cart_mapping_registered[2];

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
    const cart_t *dev = (cart_t *) priv;

    return (addr >= dev->base && addr - dev->base < dev->size) ? dev->buf[addr - dev->base] : 0xff;
}

int
cart_read_resource(uint32_t address, uint8_t *value)
{
    for (unsigned int i = 0; i < 2; i++) {
        const cart_t *dev = &carts[i];

        if (dev->buf && address >= dev->base && address - dev->base < dev->size) {
            *value = dev->buf[address - dev->base];
            return 1;
        }
    }

    *value = 0xff;
    return 0;
}

static void
cart_load_error(int drive, UNUSED(char *fn))
{
    cartridge_log("Cartridge: could not load '%s'\n", fn);
    memset(cart_fns[drive], 0, sizeof(cart_fns[drive]));
    ui_sb_update_icon_state(SB_CARTRIDGE | drive, 1);
}

static void
cart_image_close(int drive)
{
    if (cart_mapping_registered[drive])
        mem_mapping_disable(&cart_mappings[drive]);

    free(carts[drive].buf);
    memset(&carts[drive], 0, sizeof(carts[drive]));
}

static int
cart_image_load(int drive, char *fn)
{
    FILE         *fp;
    uint8_t      *buf = NULL;
    uint8_t       header[512];
    uint32_t      size;
    uint32_t      base;
    long          length;
    const cart_t *other = &carts[drive ^ 1];

    cart_image_close(drive);

    fp = plat_fopen(fn, "rb");
    if (!fp)
        goto fail;
    if (fseek(fp, 0, SEEK_END) != 0)
        goto fail;
    length = ftell(fp);
    if (length < 0x1200 || length > 0x100200 || fseek(fp, 0, SEEK_SET) != 0)
        goto fail;

    size = (uint32_t) length;
    if ((size & 0xfff) == 0x200) {
        if (fread(header, 1, sizeof(header), fp) != sizeof(header))
            goto fail;
        size -= sizeof(header);
        base = ((uint32_t) header[0x1ce] | ((uint32_t) header[0x1cf] << 8)) << 4;
    } else if (!(size & 0xfff)) {
        base = drive ? 0xe0000 : 0xd0000;
        if (size == 0x8000)
            base += 0x8000;
    } else
        goto fail;

    if (base >= 0x100000 || size > 0x100000 - base)
        goto fail;
    if (other->buf && base < other->base + other->size && other->base < base + size)
        goto fail;

    buf = malloc(size);
    if (!buf || fread(buf, 1, size, fp) != size || fgetc(fp) != EOF || ferror(fp))
        goto fail;
    if (fclose(fp) != 0) {
        fp = NULL;
        goto fail;
    }

    cartridge_log("cart_image_load(): %s at %08X-%08X\n", fn, base, base + size - 1);
    carts[drive].buf  = buf;
    carts[drive].base = base;
    carts[drive].size = size;
    if (cart_mapping_registered[drive]) {
        mem_mapping_set_addr(&cart_mappings[drive], base, size);
        mem_mapping_set_exec(&cart_mappings[drive], buf);
        mem_mapping_set_p(&cart_mappings[drive], &carts[drive]);
    }
    return 1;

fail:
    if (fp)
        fclose(fp);
    free(buf);
    cart_load_error(drive, fn);
    return 0;
}

static void
cart_load_common(int drive, char *fn, uint8_t hard_reset)
{
    if (drive < 0 || drive >= 2 || !fn || !fn[0])
        return;

    cartridge_log("Cartridge: loading drive %d with '%s'\n", drive, fn);

    if (strlen(fn) >= sizeof(cart_fns[drive])) {
        cart_image_close(drive);
        cart_load_error(drive, fn);
    } else if (cart_image_load(drive, fn))
        memmove(cart_fns[drive], fn, strlen(fn) + 1);

    /* Inserting or removing a cartridge resets both PCjr and JX. */
    if (!hard_reset)
        resetx86();
}

void
cart_load(int drive, char *fn)
{
    cart_load_common(drive, fn, 0);
}

void
cart_close(int drive)
{
    if (drive < 0 || drive >= 2)
        return;

    cartridge_log("Cartridge: closing drive %d\n", drive);

    cart_image_close(drive);
    cart_fns[drive][0] = 0;
    ui_sb_update_icon_state(SB_CARTRIDGE | drive, 1);
    resetx86();
}

void
cart_reset(void)
{
    /* mem_reset has already discarded the previous machine's mappings. */
    memset(cart_mapping_registered, 0, sizeof(cart_mapping_registered));
    cart_image_close(1);
    cart_image_close(0);

    if (!machine_has_cartridge(machine))
        return;

    if (!machine_is_pcjx(machine)) {
        for (unsigned int i = 0; i < 2; i++) {
            mem_mapping_add(&cart_mappings[i], 0xd0000, 0x2000, cart_read, NULL, NULL,
                            NULL, NULL, NULL, NULL, MEM_MAPPING_EXTERNAL, &carts[i]);
            mem_mapping_disable(&cart_mappings[i]);
            cart_mapping_registered[i] = 1;
        }
    }

    cart_load_common(0, cart_fns[0], 1);
    cart_load_common(1, cart_fns[1], 1);
}
