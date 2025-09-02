/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of ISA ROM card Expansions.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025 Jasmine Iwanek.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/nvr.h>
#include <86box/isarom.h>

enum {
    ISAROM_CARD = 0,
    ISAROM_CARD_DUAL,
    ISAROM_CARD_QUAD,
    ISAROM_CARD_LBA_ENHANCER
};

#define BIOS_LBA_ENHANCER  "roms/hdd/misc/lbaenhancer.bin"

#ifdef ENABLE_ISAROM_LOG
int isarom_do_log = ENABLE_ISAROM_LOG;

static void
isarom_log(const char *fmt, ...)
{
    va_list ap;

    if (isarom_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define isarom_log(fmt, ...)
#endif

typedef struct isarom_t {
    struct {
        rom_t       rom;
        uint32_t    addr;
        const char *fn;
        uint32_t    size;
        uint32_t    len;
        char        nvr_path[64];
        uint8_t     writable;
    } socket[4];
    uint8_t inst;
    uint8_t type;
} isarom_t;

static inline uint8_t
get_limit(uint8_t type)
{
    switch (type) {
        case ISAROM_CARD_DUAL:
            return 2;
        case ISAROM_CARD_QUAD:
            return 4;
        default:
            return 1;
    }
}

static inline void
isarom_save_nvr(char *path, uint8_t *data, size_t size)
{
    if (path[0] == 0x00)
        return;

    FILE *fp = nvr_fopen(path, "wb");
    if (fp) {
        fwrite(data, 1, size, fp);
        fclose(fp);
    }
}

void
isarom_close(void *priv)
{
    isarom_t *dev = (isarom_t *) priv;
    if (!priv)
        return;

    for (uint8_t i = 0; i < get_limit(dev->type); i++) {
        if (dev->socket[i].writable) {
            isarom_log("isarom[%u]: saving NVR for socket %u -> %s (%u bytes)\n",
                       dev->inst, i, dev->socket[i].nvr_path, dev->socket[i].size);
            isarom_save_nvr(dev->socket[i].nvr_path, dev->socket[i].rom.rom, dev->socket[i].size);
        }
    }

    free(dev);
}

static void *
isarom_init(const device_t *info)
{
    isarom_t *dev = (isarom_t *) calloc(1, sizeof(isarom_t));
    if (!dev)
        return NULL;

    dev->inst = device_get_instance();
    dev->type = (uint8_t) info->local;

    isarom_log("isarom[%u]: initializing device (type=%u)\n", dev->inst, dev->type);

    for (uint8_t i = 0; i < get_limit(dev->type); i++) {
        char str[22];
        char suffix[4] = "";
        if (i > 0)
            snprintf(suffix, sizeof(suffix), "%d", i + 1);

        snprintf(str, sizeof(str), "bios_addr%s", suffix);
        dev->socket[i].addr = device_get_config_hex20(str);

        switch (dev->type) {
            case ISAROM_CARD_LBA_ENHANCER:
                dev->socket[i].fn   = BIOS_LBA_ENHANCER;
                dev->socket[i].size = 0x4000;
                break;

            default:
                snprintf(str, sizeof(str), "bios_fn%s", suffix);
                dev->socket[i].fn = device_get_config_string(str);

                snprintf(str, sizeof(str), "bios_size%s", suffix);
                dev->socket[i].size = device_get_config_int(str);

                snprintf(str, sizeof(str), "rom_writes_enabled%s", suffix);
                if (device_get_config_int(str))
                    dev->socket[i].writable = 1;
                break;
        }

        /* Note: 2K is the smallest ROM I've found, but 86Box's memory granularity is 4K, the number
           below is fine as we'll end up allocating no less than 4K due to the device config limits. */
        dev->socket[i].len = (dev->socket[i].size > 0) ? ((dev->socket[i].size - 1) | MEM_GRANULARITY_MASK) : 0;

        isarom_log("isarom[%u]: socket %u: addr=0x%05X size=%u writable=%u fn=%s\n",
                   dev->inst, i, dev->socket[i].addr, dev->socket[i].size,
                   dev->socket[i].writable, dev->socket[i].fn ? dev->socket[i].fn : "(null)");

        if ((dev->socket[i].addr != 0) && (dev->socket[i].fn != NULL)) {
            rom_init(&dev->socket[i].rom,
                     dev->socket[i].fn,
                     dev->socket[i].addr,
                     dev->socket[i].size,
                     dev->socket[i].len,
                     0,
                     MEM_MAPPING_EXTERNAL);

            isarom_log("isarom[%u]: ROM initialized for socket %u\n", dev->inst, i);

            if (dev->socket[i].writable) {
                mem_mapping_set_write_handler(&dev->socket[i].rom.mapping, rom_write, rom_writew, rom_writel);
                snprintf(dev->socket[i].nvr_path, sizeof(dev->socket[i].nvr_path), "isarom_%i_%i.nvr", dev->inst, i + 1);
                FILE *fp = nvr_fopen(dev->socket[i].nvr_path, "rb");
                if (fp != NULL) {
                    (void) !fread(dev->socket[i].rom.rom, 1, dev->socket[i].size, fp);
                    fclose(fp);
                    isarom_log("isarom[%u]: loaded %zu bytes from %s\n", dev->inst, read_bytes, dev->socket[i].nvr_path);
                } else
                    isarom_log("isarom[%u]: NVR not found, skipping load (%s)\n", dev->inst, dev->socket[i].nvr_path);
            }
        }
    }

    return dev;
}

static int
isarom_lba_enhancer_available(void)
{
    return rom_present(BIOS_LBA_ENHANCER);
}

#define BIOS_FILE_FILTER "ROM files (*.bin *.rom)|*.bin,*.rom"

#define BIOS_ADDR_SELECTION { \
    { "Disabled", 0x00000 },  \
    { "C000H",    0xc0000 },  \
    { "C200H",    0xc2000 },  \
    { "C400H",    0xc4000 },  \
    { "C600H",    0xc6000 },  \
    { "C800H",    0xc8000 },  \
    { "CA00H",    0xca000 },  \
    { "CC00H",    0xcc000 },  \
    { "CE00H",    0xce000 },  \
    { "D000H",    0xd0000 },  \
    { "D200H",    0xd2000 },  \
    { "D400H",    0xd4000 },  \
    { "D600H",    0xd6000 },  \
    { "D800H",    0xd8000 },  \
    { "DA00H",    0xda000 },  \
    { "DC00H",    0xdc000 },  \
    { "DE00H",    0xde000 },  \
    { "E000H",    0xe0000 },  \
    { "E200H",    0xe2000 },  \
    { "E400H",    0xe4000 },  \
    { "E600H",    0xe6000 },  \
    { "E800H",    0xe8000 },  \
    { "EA00H",    0xea000 },  \
    { "EC00H",    0xec000 },  \
    { "EE00H",    0xee000 },  \
    { "",         0       }   \
}

#define BIOS_SIZE_SELECTION { \
    { "4K",   4096 },         \
    { "8K",   8192 },         \
    { "16K", 16384 },         \
    { "32K", 32768 },         \
    { "64K", 65536 },         \
    { "",        0 }          \
}

// clang-format off
static const device_config_t isarom_config[] = {
    {
        .name           = "bios_fn",
        .description    = "BIOS file",
        .type           = CONFIG_FNAME,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = BIOS_FILE_FILTER,
        .spinner        = { 0 },
        .selection      = { },
        .bios           = { { 0 } }
    },
    {
        .name           = "bios_addr",
        .description    = "BIOS address",
        .type           = CONFIG_HEX20,
        .default_string = NULL,
        .default_int    = 0x00000,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = BIOS_ADDR_SELECTION,
        .bios           = { { 0 } }
    },
    {
        .name           = "bios_size",
        .description    = "BIOS size",
        .type           = CONFIG_INT,
        .default_string = NULL,
        .default_int    = 8192,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = BIOS_SIZE_SELECTION,
        .bios           = { { 0 } }
    },
    {
        .name           = "rom_writes_enabled",
        .description    = "Enable BIOS extension ROM Writes",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t isarom_dual_config[] = {
    {
        .name           = "bios_fn",
        .description    = "BIOS file (ROM #1)",
        .type           = CONFIG_FNAME,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = BIOS_FILE_FILTER,
        .spinner        = { 0 },
        .selection      = { },
        .bios           = { { 0 } }
    },
    {
        .name           = "bios_addr",
        .description    = "BIOS address (ROM #1)",
        .type           = CONFIG_HEX20,
        .default_string = NULL,
        .default_int    = 0x00000,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = BIOS_ADDR_SELECTION,
        .bios           = { { 0 } }
    },
    {
        .name           = "bios_size",
        .description    = "BIOS size (ROM #1)",
        .type           = CONFIG_INT,
        .default_string = NULL,
        .default_int    = 8192,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = BIOS_SIZE_SELECTION,
        .bios           = { { 0 } }
    },
    {
        .name           = "rom_writes_enabled",
        .description    = "Enable BIOS extension ROM Writes (ROM #1)",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "bios_fn2",
        .description    = "BIOS file (ROM #2)",
        .type           = CONFIG_FNAME,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = BIOS_FILE_FILTER,
        .spinner        = { 0 },
        .selection      = { },
        .bios           = { { 0 } }
    },
    {
        .name           = "bios_addr2",
        .description    = "BIOS address (ROM #2)",
        .type           = CONFIG_HEX20,
        .default_string = NULL,
        .default_int    = 0x00000,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = BIOS_ADDR_SELECTION,
        .bios           = { { 0 } }
    },
    {
        .name           = "bios_size2",
        .description    = "BIOS size (ROM #2)",
        .type           = CONFIG_INT,
        .default_string = NULL,
        .default_int    = 8192,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = BIOS_SIZE_SELECTION,
        .bios           = { { 0 } }
    },
    {
        .name           = "rom_writes_enabled2",
        .description    = "Enable BIOS extension ROM Writes (ROM #2)",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t isarom_quad_config[] = {
    {
        .name           = "bios_fn",
        .description    = "BIOS file (ROM #1)",
        .type           = CONFIG_FNAME,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = BIOS_FILE_FILTER,
        .spinner        = { 0 },
        .selection      = { },
        .bios           = { { 0 } }
    },
    {
        .name           = "bios_addr",
        .description    = "BIOS address (ROM #1)",
        .type           = CONFIG_HEX20,
        .default_string = NULL,
        .default_int    = 0x00000,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = BIOS_ADDR_SELECTION,
        .bios           = { { 0 } }
    },
    {
        .name           = "bios_size",
        .description    = "BIOS size (ROM #1)",
        .type           = CONFIG_INT,
        .default_string = NULL,
        .default_int    = 8192,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = BIOS_SIZE_SELECTION,
        .bios           = { { 0 } }
    },
    {
        .name           = "rom_writes_enabled",
        .description    = "Enable BIOS extension ROM Writes (ROM #1)",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "bios_fn2",
        .description    = "BIOS file (ROM #2)",
        .type           = CONFIG_FNAME,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = BIOS_FILE_FILTER,
        .spinner        = { 0 },
        .selection      = { },
        .bios           = { { 0 } }
    },
    {
        .name           = "bios_addr2",
        .description    = "BIOS address (ROM #2)",
        .type           = CONFIG_HEX20,
        .default_string = NULL,
        .default_int    = 0x00000,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = BIOS_ADDR_SELECTION,
        .bios           = { { 0 } }
    },
    {
        .name           = "bios_size2",
        .description    = "BIOS size (ROM #2)",
        .type           = CONFIG_INT,
        .default_string = NULL,
        .default_int    = 8192,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = BIOS_SIZE_SELECTION,
        .bios           = { { 0 } }
    },
    {
        .name           = "rom_writes_enabled2",
        .description    = "Enable BIOS extension ROM Writes (ROM #2)",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "bios_fn3",
        .description    = "BIOS file (ROM #3)",
        .type           = CONFIG_FNAME,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = BIOS_FILE_FILTER,
        .spinner        = { 0 },
        .selection      = { },
        .bios           = { { 0 } }
    },
    {
        .name           = "bios_addr3",
        .description    = "BIOS address (ROM #3)",
        .type           = CONFIG_HEX20,
        .default_string = NULL,
        .default_int    = 0x00000,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = BIOS_ADDR_SELECTION,
        .bios           = { { 0 } }
    },
    {
        .name           = "bios_size3",
        .description    = "BIOS size (ROM #3)",
        .type           = CONFIG_INT,
        .default_string = NULL,
        .default_int    = 8192,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = BIOS_SIZE_SELECTION,
        .bios           = { { 0 } }
    },
    {
        .name           = "rom_writes_enabled3",
        .description    = "Enable BIOS extension ROM Writes (ROM #3)",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    {
        .name           = "bios_fn4",
        .description    = "BIOS file (ROM #4)",
        .type           = CONFIG_FNAME,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = BIOS_FILE_FILTER,
        .spinner        = { 0 },
        .selection      = { },
        .bios           = { { 0 } }
    },
    {
        .name           = "bios_addr4",
        .description    = "BIOS address (ROM #4)",
        .type           = CONFIG_HEX20,
        .default_string = NULL,
        .default_int    = 0x00000,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = BIOS_ADDR_SELECTION,
        .bios           = { { 0 } }
    },
    {
        .name           = "bios_size4",
        .description    = "BIOS size (ROM #4)",
        .type           = CONFIG_INT,
        .default_string = NULL,
        .default_int    = 8192,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = BIOS_SIZE_SELECTION,
        .bios           = { { 0 } }
    },
    {
        .name           = "rom_writes_enabled4",
        .description    = "Enable BIOS extension ROM Writes (ROM #4)",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t lba_enhancer_config[] = {
    {
        .name           = "bios_addr",
        .description    = "BIOS address",
        .type           = CONFIG_HEX20,
        .default_string = NULL,
        .default_int    = 0xc8000,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "C800H", .value = 0xc8000 },
            { .description = "CC00H", .value = 0xcc000 },
            { .description = "D000H", .value = 0xd0000 },
            { .description = "D400H", .value = 0xd4000 },
            { .description = "D800H", .value = 0xd8000 },
            { .description = "DC00H", .value = 0xdc000 },
            { .description = ""                        }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

static const device_t isarom_device = {
    .name          = "Generic ISA ROM Board",
    .internal_name = "isarom",
    .flags         = DEVICE_ISA,
    .local         = ISAROM_CARD,
    .init          = isarom_init,
    .close         = isarom_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = isarom_config
};

static const device_t isarom_dual_device = {
    .name          = "Generic Dual ISA ROM Board",
    .internal_name = "isarom_dual",
    .flags         = DEVICE_ISA,
    .local         = ISAROM_CARD_DUAL,
    .init          = isarom_init,
    .close         = isarom_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = isarom_dual_config
};

static const device_t isarom_quad_device = {
    .name          = "Generic Quad ISA ROM Board",
    .internal_name = "isarom_quad",
    .flags         = DEVICE_ISA,
    .local         = ISAROM_CARD_QUAD,
    .init          = isarom_init,
    .close         = isarom_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = isarom_quad_config
};

static const device_t lba_enhancer_device = {
    .name          = "Vision Systems LBA Enhancer",
    .internal_name = "lba_enhancer",
    .flags         = DEVICE_ISA,
    .local         = ISAROM_CARD_LBA_ENHANCER,
    .init          = isarom_init,
    .close         = isarom_close,
    .reset         = NULL,
    .available     = isarom_lba_enhancer_available,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = lba_enhancer_config
};

static const struct {
    const device_t *dev;
} boards[] = {
    // clang-format off
    { &device_none         },
    { &isarom_device       },
    { &isarom_dual_device  },
    { &isarom_quad_device  },
    { &lba_enhancer_device },
    { NULL                 }
    // clang-format on
};

void
isarom_reset(void)
{
    for (uint8_t i = 0; i < ISAROM_MAX; i++) {
        if (isarom_type[i] == 0)
            continue;

        /* Add the device instance to the system. */
        device_add_inst(boards[isarom_type[i]].dev, i + 1);
    }
}

const char *
isarom_get_name(int board)
{
    if (boards[board].dev == NULL)
        return NULL;

    return (boards[board].dev->name);
}

const char *
isarom_get_internal_name(int board)
{
    return device_get_internal_name(boards[board].dev);
}

int
isarom_get_from_internal_name(const char *str)
{
    int c = 0;

    while (boards[c].dev != NULL) {
        if (!strcmp(boards[c].dev->internal_name, str))
            return c;
        c++;
    }

    /* Not found. */
    return 0;
}

const device_t *
isarom_get_device(int board)
{
    /* Add the device instance to the system. */
    return boards[board].dev;
}

int
isarom_has_config(int board)
{
    if (boards[board].dev == NULL)
        return 0;

    return (boards[board].dev->config ? 1 : 0);
}
