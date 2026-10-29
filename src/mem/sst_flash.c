/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of an SST flash chip.
 *
 *          The chip itself (sst_flash_create() and friends) knows
 *          nothing about where it is mapped: it decodes its own
 *          address lines (addr & mask), so a machine can map one or
 *          more chips wherever its chip selects put them. The
 *          *_device descriptors below wrap a single chip as the boot
 *          BIOS at the top of the first megabyte.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *          Nathan Lewis, <git@nrlewis.dev>
 *
 *          Copyright 2016-2020 Miran Grca.
 *          Copyright 2022-2023 Jasmine Iwanek.
 *          Copyright 2026      Nathan Lewis.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/machine.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/plat.h>
#include <86box/m_xt_xi8088.h>
#include <86box/flash.h>

typedef struct sst_sector_t {
    uint32_t base;
    uint32_t size;
} sst_sector_t;

struct sst_t {
    uint8_t manufacturer;
    uint8_t id;
    uint8_t has_bbp;
    uint8_t is_39;
    uint8_t page_bytes;
    uint8_t sdp;
    uint8_t bbp_first_8k;
    uint8_t bbp_last_8k;
    uint8_t write_protect;

    int command_state;
    int id_mode;
    int dirty;

    uint32_t size;
    uint32_t mask;
    uint32_t page_mask;
    uint32_t page_base;
    uint32_t last_addr;
    uint32_t sector_size;
    uint32_t data_base; /* Where the storage starts, the top of the address space. */

    const sst_sector_t *sectors; /* Non-uniform sector layout, NULL if uniform. */

    uint8_t  page_buffer[128];
    uint8_t  page_dirty[128];
    uint8_t *array;

    char nvr_path[1024];

    pc_timer_t page_write_timer;
};

/*
 * A single chip mapped as the boot BIOS, behind the chipset's BIOS chip
 * select (flash_bios_*_selected()) and, with SST_FORCE_128, an address
 * inversion of A17.
 */
typedef struct sst_bios_t {
    sst_t *chip;

    int      force_128;
    uint32_t xor;

    mem_mapping_t mapping[16];
    mem_mapping_t mapping_h[16];
    uint8_t      *exec[16];
    uint8_t      *exec_h[16];
    int           enabled[16];
    int           enabled_h[16];
} sst_bios_t;

#define SST_CHIP_ERASE      0x10 /* Both 29 and 39, 6th cycle */
#define SST_SDP_DISABLE     0x20 /* Only 29, Software data protect disable and write - treat as write */
#define SST_SECTOR_ERASE    0x30 /* Only 39, 6th cycle */
#define W_BOOT_BLOCK_PROT   0x40 /* Only W29C020 */
#define SST_SET_ID_MODE_ALT 0x60 /* Only 29, 6th cycle */
#define SST_ERASE           0x80 /* Both 29 and 39 */
                                 /* With data 60h on 6th cycle, it's alt. ID */
#define SST_SET_ID_MODE   0x90   /* Both 29 and 39 */
#define SST_BYTE_PROGRAM  0xa0   /* Both 29 and 39 */
#define SST_CLEAR_ID_MODE 0xf0   /* Both 29 and 39 */
                                 /* 1st cycle variant only on 39 */

#define SST           0xbf /* SST Manufacturer's ID */

#define SST29EE512    0x5d00
#define SST29LE_VE512 0x3d00
#define SST29EE010    0x0700
#define SST29LE_VE010 0x0800
#define SST29EE020    0x1000
#define SST29LE_VE020 0x1200

#define SST39SF512    0xb400
#define SST39SF010    0xb500
#define SST39SF020    0xb600
#define SST39SF040    0xb700

#define SST39LF512    0xd400
#define SST39LF010    0xd500
#define SST39LF020    0xd600
#define SST39LF040    0xd700
#define SST39LF080    0xd800
#define SST39LF016    0xd900

#if 0
// 16 wide
#define SST39WF400      0x272f
#define SST39WF400B     0x272e
#define SST39WF800      0x273f
#define SST39WF800B     0x273e
#define SST39WF1601     0xbf274b
#define SST39WF1602     0xbf274a

#define SST39LF100      0x2788
#define SST39LF200      0x2789
#define SST39LF400      0x2780
#define SST39LF800      0x2781
#define SST39LF160      0x2782
#endif

#define SST49LF002  0x5700
#define SST49LF020  0x6100
#define SST49LF020A 0x5200
#define SST49LF003  0x1b00
#define SST49LF004  0x6000
#define SST49LF004C 0x5400
#define SST49LF040  0x5100
#define SST49LF008  0x5a00
#define SST49LF008C 0x5900
#define SST49LF080  0x5b00
#define SST49LF030  0x1c00
#define SST49LF160  0x4c00
#define SST49LF016  0x5c00

#define WINBOND     0xda /* Winbond Manufacturer's ID */
#define W29C512     0xc800
#define W29C010     0xc100
#define W29C011A    0xc100
#define W29C020     0x4500
#define W29C040     0x4600

#define AMD         0x01 /* AMD Manufacturer's ID */
#define AMD29F010A  0x2000
#define AMD29F020A    0xb000
#define AMD29F002NBT  0xb000

#define SIZE_512K   0x010000
#define SIZE_1M     0x020000
#define SIZE_2M     0x040000
#define SIZE_3M     0x060000
#define SIZE_4M     0x080000
#define SIZE_8M     0x100000
#define SIZE_16M    0x200000

static const sst_sector_t amd29f020a_sectors[] = {
    { 0x00000, 0x10000 },
    { 0x10000, 0x10000 },
    { 0x20000, 0x10000 },
    { 0x30000, 0x08000 },
    { 0x38000, 0x02000 },
    { 0x3a000, 0x02000 },
    { 0x3c000, 0x04000 },
    { 0x00000, 0x00000 }
};

static void
sst_sector_erase(sst_t *dev, uint32_t addr)
{
    uint32_t base = addr & dev->mask;
    uint32_t size = dev->sector_size;

    if (dev->sectors != NULL) {
        const sst_sector_t *sector = dev->sectors;

        while ((sector->size != 0) && ((base < sector->base) || (base >= (sector->base + sector->size))))
            sector++;

        if (sector->size == 0)
            return;

        base = sector->base;
        size = sector->size;
    } else {
        base &= ~(size - 1);

        if ((base < 0x2000) && (dev->bbp_first_8k & 0x01))
            return;
        else if ((base >= (dev->mask - 0x1fff)) && (dev->bbp_last_8k & 0x01))
            return;
    }

    memset(&dev->array[base], 0xff, size);

    dev->dirty = 1;
}

static void
sst_new_command(sst_t *dev, uint32_t addr, uint8_t val)
{
    uint32_t base = dev->data_base;
    uint32_t size = dev->size;

    if (dev->command_state == 5)
        switch (val) {
            case SST_CHIP_ERASE:
                if (dev->bbp_first_8k & 0x01) {
                    base += 0x2000;
                    size -= 0x2000;
                }

                if (dev->bbp_last_8k & 0x01)
                    size -= 0x2000;

                memset(&(dev->array[base]), 0xff, size);
                dev->command_state = 0;
                break;

            case SST_SDP_DISABLE:
                if (!dev->is_39)
                    dev->sdp = 0;
                dev->command_state = 0;
                break;

            case SST_SECTOR_ERASE:
                if (dev->is_39)
                    sst_sector_erase(dev, addr);
                dev->command_state = 0;
                break;

            case SST_SET_ID_MODE_ALT:
                dev->id_mode       = 1;
                dev->command_state = 0;
                break;

            default:
                dev->command_state = 0;
                break;
        }
    else
        switch (val) {
            case SST_ERASE:
                dev->command_state = 3;
                break;

            case SST_SET_ID_MODE:
                dev->id_mode       = 1;
                dev->command_state = 0;
                break;

            case SST_BYTE_PROGRAM:
                if (!dev->is_39) {
                    dev->sdp = 1;
                    memset(dev->page_buffer, 0xff, 128);
                    memset(dev->page_dirty, 0x00, 128);
                    dev->page_bytes = 0;
                    dev->last_addr  = 0xffffffff;
                    timer_on_auto(&dev->page_write_timer, 210.0);
                }
                dev->command_state = 6;
                break;

            case W_BOOT_BLOCK_PROT:
                dev->command_state = dev->has_bbp ? 8 : 0;
                break;

            case SST_CLEAR_ID_MODE:
                dev->id_mode       = 0;
                dev->command_state = 0;
                break;

            default:
                dev->command_state = 0;
                break;
        }
}

static void
sst_page_write(void *priv)
{
    sst_t *dev = (sst_t *) priv;

    if (dev->last_addr != 0xffffffff) {
        dev->page_base = dev->last_addr & dev->page_mask;
        for (uint8_t i = 0; i < 128; i++) {
            if (dev->page_dirty[i]) {
                if (((dev->page_base + i) < 0x2000) && (dev->bbp_first_8k & 0x01))
                    continue;
                else if (((dev->page_base + i) >= (dev->mask - 0x1fff)) && (dev->bbp_last_8k & 0x01))
                    continue;

                dev->array[dev->page_base + i] = dev->page_buffer[i];
                dev->dirty |= 1;
            }
        }
    }
    dev->page_bytes    = 0;
    dev->command_state = 0;
    timer_disable(&dev->page_write_timer);
}

static uint8_t
sst_read_id(uint32_t addr, void *priv)
{
    const sst_t  *dev = (sst_t *) priv;
    uint8_t       ret = 0x00;
    uint32_t      mask = 0xffff;

    if (dev->manufacturer == AMD)
        mask >>= 8;

    if ((addr & mask) == 0)
        ret = dev->manufacturer;
    else if ((addr & mask) == 1)
        ret = dev->id;
#ifdef UNKNOWN_FLASH
    else if ((addr & 0xffff) == 0x100)
        ret = 0x1c;
    else if ((addr & 0xffff) == 0x101)
        ret = 0x92;
#endif
    else if (dev->has_bbp) {
        if (addr == 0x00002)
            ret = dev->bbp_first_8k;
        else if (addr == 0x3fff2)
            ret = dev->bbp_last_8k;
    } else if (dev->manufacturer == AMD) {
        if ((addr & mask) == 2)
            ret = 0x00;
    }

    return ret;
}

static void
sst_buf_write(sst_t *dev, uint32_t addr, uint8_t val)
{
    dev->page_buffer[addr & 0x0000007f] = val;
    dev->page_dirty[addr & 0x0000007f]  = 1;
    dev->page_bytes++;
    dev->last_addr = addr;
    if (dev->page_bytes >= 128) {
        sst_page_write(dev);
    } else
        timer_on_auto(&dev->page_write_timer, 210.0);
}

static void
sst_write(uint32_t addr, uint8_t val, void *priv)
{
    sst_t *dev = (sst_t *) priv;
    uint32_t mask = 0x7fff;
    uint32_t addr0 = 0x5555;
    uint32_t addr1 = 0x2aaa;

    if (dev->write_protect)
        return;

    addr &= dev->mask;

    if (dev->manufacturer == AMD) {
        mask >>= 4;
        addr0 >>= 4;
        addr1 >>= 4;
    }

    switch (dev->command_state) {
        case 0:
        case 3:
            /* 1st and 4th Bus Write Cycle */
            if ((val == 0xf0) && dev->is_39 && (dev->command_state == 0)) {
                if (dev->id_mode)
                    dev->id_mode = 0;
                dev->command_state = 0;
            } else if (((addr & mask) == addr0) && (val == 0xaa))
                dev->command_state++;
            else {
                if (!dev->is_39 && !dev->sdp && (dev->command_state == 0)) {
                    /* 29 series, software data protection off, start loading the page. */
                    memset(dev->page_buffer, 0xff, 128);
                    memset(dev->page_dirty, 0x00, 128);
                    dev->page_bytes    = 0;
                    dev->command_state = 7;
                    sst_buf_write(dev, addr, val);
                } else
                    dev->command_state = 0;
            }
            break;
        case 1:
        case 4:
            /* 2nd and 5th Bus Write Cycle */
            if (((addr & mask) == addr1) && (val == 0x55))
                dev->command_state++;
            else
                dev->command_state = 0;
            break;
        case 2:
        case 5:
            /* 3rd and 6th Bus Write Cycle */
            if ((dev->command_state == 5) && (val == SST_SECTOR_ERASE)) {
                /* Sector erase - can be on any address. */
                sst_new_command(dev, addr, val);
            } else if ((addr & mask) == addr0)
                sst_new_command(dev, addr, val);
            else
                dev->command_state = 0;
            break;
        case 6:
            /* Page Load Cycle (29) / Data Write Cycle (39SF) */
            if (dev->is_39) {
                dev->command_state = 0;

                dev->array[addr & dev->mask] = val;
                dev->dirty                   = 1;
            } else {
                dev->command_state++;
                sst_buf_write(dev, addr, val);
            }
            break;
        case 7:
            if (!dev->is_39)
                sst_buf_write(dev, addr, val);
            break;
        case 8:
            if ((addr == 0x00000) && (val == 0x00))
                dev->bbp_first_8k = 0xff;
            else if ((addr == 0x3ffff) && (val == 0xff))
                dev->bbp_last_8k = 0xff;
            dev->command_state = 0;
            break;

        default:
            break;
    }
}

static uint8_t
sst_read(uint32_t addr, void *priv)
{
    const sst_t *dev = (sst_t *) priv;
    uint8_t      ret;

    addr &= dev->mask;

    if (dev->id_mode)
        ret = sst_read_id(addr, priv);
    else
        ret = dev->array[addr];

    return ret;
}

static uint16_t
sst_readw(uint32_t addr, void *priv)
{
    sst_t   *dev = (sst_t *) priv;
    uint16_t ret;

    addr &= dev->mask;

    if (dev->id_mode)
        ret = sst_read(addr, priv) | (sst_read(addr + 1, priv) << 8);
    else
        ret = *(uint16_t *) &dev->array[addr];

    return ret;
}

static uint32_t
sst_readl(uint32_t addr, void *priv)
{
    sst_t   *dev = (sst_t *) priv;
    uint32_t ret;

    addr &= dev->mask;

    if (dev->id_mode)
        ret = sst_readw(addr, priv) | (sst_readw(addr + 2, priv) << 16);
    else
        ret = *(uint32_t *) &dev->array[addr];

    return ret;
}

/*
 * Where a window of the chip at base starts in the chip, and how the window
 * mirrors it. Like the real part, the chip only sees the address lines it
 * has, so a window shows the chip contents at (base & mask) onwards. A
 * window no bigger than the chip must not run past its end, and a bigger
 * one repeats the chip, so it has to start where the chip does.
 */
static void
sst_flash_window(const sst_t *dev, uint32_t base, uint32_t size, uint8_t **exec, uint32_t *mask)
{
    uint32_t offset = base & dev->mask;

    if (size > (dev->mask + 1)) {
        if (offset != 0)
            fatal("SST Flash: %08X bytes at %08X do not start on a %08X byte boundary\n",
                  size, base, dev->mask + 1);

        *exec = dev->array;
        *mask = dev->mask;
    } else {
        if ((offset + size) > (dev->mask + 1))
            fatal("SST Flash: %08X bytes at %08X are past the end of the %08X byte chip\n",
                  size, base, dev->mask + 1);

        *exec = dev->array + offset;
        *mask = 0xffffffff;
    }
}

/* Map a window of the chip at base, which may be bigger than the chip. */
void
sst_flash_map(sst_t *dev, mem_mapping_t *mapping, uint32_t base, uint32_t size)
{
    uint8_t *exec;
    uint32_t mask;

    sst_flash_window(dev, base, size, &exec, &mask);

    mem_mapping_add(mapping, base, size,
                    sst_read, sst_readw, sst_readl,
                    sst_write, NULL, NULL,
                    exec, MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM | MEM_MAPPING_ROMCS | MEM_MAPPING_ROM_WS, (void *) dev);
    if (mask != 0xffffffff)
        mem_mapping_set_mask(mapping, mask);
}

/* Move a window added by sst_flash_map() elsewhere, under the same rules. */
void
sst_flash_remap(sst_t *dev, mem_mapping_t *mapping, uint32_t base, uint32_t size)
{
    uint8_t *exec;
    uint32_t mask;

    sst_flash_window(dev, base, size, &exec, &mask);

    mem_mapping_set_addr(mapping, base, size);
    mem_mapping_set_exec(mapping, exec);
    mem_mapping_set_mask(mapping, mask);
}

/* A write cycle to the chip, for boards that route writes to it themselves. */
void
sst_flash_write(sst_t *dev, uint32_t addr, uint8_t val)
{
    sst_write(addr, val, dev);
}

void
sst_flash_set_write_protect(sst_t *dev, int protect)
{
    dev->write_protect = !!protect;
}

uint32_t
sst_flash_get_size(const sst_t *dev)
{
    return dev->size;
}

void
sst_flash_reset(sst_t *dev)
{
    dev->command_state = 0;
    dev->id_mode       = 0;
}

sst_t *
sst_flash_create(const device_t *model, const sst_flash_cfg_t *cfg)
{
    FILE  *fp;
    sst_t *dev = calloc(1, sizeof(sst_t));

    dev->manufacturer = model->local & 0xff;
    dev->id           = (model->local >> 8) & 0xff;
    dev->has_bbp      = (dev->manufacturer == WINBOND) && ((model->local & 0xff00) >= W29C020);
    dev->is_39        = (dev->manufacturer == SST) && ((model->local & 0xff00) >= SST39SF512);
    if (dev->manufacturer == AMD)
        dev->is_39 = 1;

    dev->sector_size = 0x1000;
    if ((dev->manufacturer == AMD) && ((model->local & 0xff00) == AMD29F020A))
        dev->sectors = amd29f020a_sectors;
    else if ((dev->manufacturer == AMD) && ((model->local & 0xff00) == AMD29F010A))
        dev->sector_size = 0x4000;

    dev->size = cfg->size ? cfg->size : (model->local & 0x7fff0000);

    /*
       The address space is a power of two; a chip with less storage
       than that (such as the 384k 49LF003) sits at the top of it.
     */
    dev->mask = 0x0000ffff;
    while (dev->mask < (dev->size - 1))
        dev->mask = (dev->mask << 1) | 1;
    dev->data_base    = (dev->mask + 1) - dev->size;
    dev->page_mask    = dev->mask & 0xffffff80; /* Filter out A0-A6. */
    dev->sdp          = 1;
    dev->bbp_first_8k = dev->bbp_last_8k = 0xfe;

    dev->array = (uint8_t *) malloc(dev->mask + 1);
    memset(dev->array, 0xff, dev->mask + 1);

    /* The image covers the address space from 0, like the BIOS image in rom[]. */
    if (cfg->image != NULL)
        memcpy(dev->array, cfg->image, MIN(cfg->image_len, dev->mask + 1));

    if (cfg->nvr_name != NULL) {
        if (strlen(cfg->nvr_name) == 0)
            fatal("Attempting to open the Flash file for reading with an empty invalid name\n");

        snprintf(dev->nvr_path, sizeof(dev->nvr_path), "%s.bin", cfg->nvr_name);

        fp = nvr_fopen(dev->nvr_path, "rb");
        if (fp != NULL) {
            if (!dump_missing)
                (void) !fread(&(dev->array[dev->data_base]), 1, dev->size, fp);
            fclose(fp);
        } else if (!dump_missing)
            dev->dirty = 1; /* It is by definition dirty on creation. */
    }

    if (!dev->is_39)
        timer_add(&dev->page_write_timer, sst_page_write, dev, 0);

    return dev;
}

void
sst_flash_destroy(sst_t *dev)
{
    FILE *fp;

    if (!dev->is_39)
        timer_disable(&dev->page_write_timer);

    if (dev->dirty && (dev->nvr_path[0] != '\0')) {
        fp = nvr_fopen(dev->nvr_path, "wb");
        if (fp != NULL) {
            if (!dump_missing)
                fwrite(&(dev->array[dev->data_base]), dev->size, 1, fp);
            fclose(fp);
        } else if (!dump_missing)
            warning("Unable to open %s for writing, please make sure your NVR folder is writable\n", dev->nvr_path);
    }

    free(dev->array);
    dev->array = NULL;

    free(dev);
}

/* The boot BIOS. */
static sst_bios_t *sst = NULL;

static uint8_t
sst_bios_read(uint32_t addr, void *priv)
{
    const sst_bios_t *dev = (sst_bios_t *) priv;

    if (!flash_bios_read_selected(addr))
        return 0xff;

    return sst_read(addr ^ dev->xor, dev->chip);
}

static uint16_t
sst_bios_readw(uint32_t addr, void *priv)
{
    const sst_bios_t *dev = (sst_bios_t *) priv;

    if (!flash_bios_read_selected(addr))
        return 0xffff;

    return sst_readw(addr ^ dev->xor, dev->chip);
}

static uint32_t
sst_bios_readl(uint32_t addr, void *priv)
{
    const sst_bios_t *dev = (sst_bios_t *) priv;

    if (!flash_bios_read_selected(addr))
        return 0xffffffff;

    return sst_readl(addr ^ dev->xor, dev->chip);
}

static void
sst_bios_write(uint32_t addr, uint8_t val, void *priv)
{
    const sst_bios_t *dev = (sst_bios_t *) priv;

    if (!flash_bios_write_selected(addr))
        return;

    sst_write(addr ^ dev->xor, val, dev->chip);
}

static void
sst_bios_map(sst_bios_t *dev, mem_mapping_t *mapping, uint8_t **exec, uint32_t base)
{
    *exec = dev->chip->array + (base & dev->chip->mask);

    mem_mapping_add(mapping, base, 0x10000,
                    sst_bios_read, sst_bios_readw, sst_bios_readl,
                    sst_bios_write, NULL, NULL,
                    *exec, MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM | MEM_MAPPING_ROMCS | MEM_MAPPING_ROM_WS, (void *) dev);
}

/* The chip select changed: keep an exec pointer only on a mapping the
   chipset decodes whole (flash_bios_mapping_update). */
static void
sst_decode_hook(void *priv)
{
    sst_bios_t *dev = (sst_bios_t *) priv;

    for (uint8_t i = 0; i < 16; i++) {
        if (dev->mapping[i].size)
            flash_bios_mapping_update(&dev->mapping[i], dev->exec[i]);
        if (dev->mapping_h[i].size)
            flash_bios_mapping_update(&dev->mapping_h[i], dev->exec_h[i]);
    }
}

void
sst_set_xor(int enable)
{
    if (sst != NULL) {
        sst->xor = (!!enable) << 17;

        for (uint8_t i = 0; i < 16; i++) {
            if (sst->enabled[i])
                mem_mapping_set_exec(&(sst->mapping[i]), sst->chip->array + ((sst->mapping[i].base ^ sst->xor) & sst->chip->mask));

            if (sst->enabled_h[i])
                mem_mapping_set_exec(&(sst->mapping_h[i]), sst->chip->array + ((sst->mapping_h[i].base ^ sst->xor) & sst->chip->mask));
        }

        flushmmucache();
    }
}

static void
sst_reset(void *priv)
{
    sst_bios_t *dev = (sst_bios_t *) priv;

    sst_flash_reset(dev->chip);

    if (dev->force_128)
        sst_set_xor(0);
}

static void *
sst_init(const device_t *info)
{
    sst_bios_t     *dev = calloc(1, sizeof(sst_bios_t));
    sst_flash_cfg_t cfg = { 0 };
    uint32_t        size;
    uint32_t        base;
    uint32_t        root_base;
    int             count;

    dev->force_128 = !!(info->local & SST_FORCE_128);

    cfg.nvr_name  = machine_get_nvr_name_ex(machine);
    cfg.image     = rom;
    cfg.image_len = biosmask + 1;

    /* The Xi 8088 only decodes 64k of its 128k chip unless the 128k BIOS is selected. */
    if (((info->local & 0x7fff0000) == 0x20000) && ((machines[machine].init == machine_xt_xi8088_init) && !xi8088_bios_128kb()))
        cfg.size = 0x10000;

    mem_mapping_disable(&bios_mapping);
    mem_mapping_disable(&bios_high_mapping);

    dev->chip = sst_flash_create(info, &cfg);

    size      = MIN(sst_flash_get_size(dev->chip), 0x100000);
    count     = size >> 16;
    root_base = 0x100000 - size;

    for (int i = 0; i < count; i++) {
        base = root_base + (i << 16);

        if (base >= 0xe0000) {
            sst_bios_map(dev, &(dev->mapping[i]), &(dev->exec[i]), base);
            dev->enabled[i] = 1;
        }

        if ((base >= 0xe0000) || !(dev->force_128)) {
            sst_bios_map(dev, &(dev->mapping_h[i]), &(dev->exec_h[i]), base | ((is6117 || cpu_16bitbus) ? 0xf00000 : 0xfff00000));
            dev->enabled_h[i] = 1;
        }
    }

    /* Set the requested logo display */
    if ((machines[machine].init == machine_at_in530_init) &&
        (info->local == (AMD | AMD29F002NBT | SIZE_2M))) {
        uint8_t      *logo = &dev->chip->array[dev->chip->data_base + 0x3af70];
        const uint8_t old  = *logo;

        *logo &= 0xf0;
        *logo |= machine_in530_boot_logo() & 0x0f;

        if (*logo != old)
            dev->chip->dirty = 1;
    }

    sst = dev;

    if (dev->force_128)
        sst_set_xor(0);

    flash_bios_set_decode_hook(sst_decode_hook, dev);
    sst_decode_hook(dev);

    return dev;
}

static void
sst_close(void *priv)
{
    sst_bios_t *dev = (sst_bios_t *) priv;

    flash_bios_set_decode_hook(NULL, NULL);

    sst_flash_destroy(dev->chip);

    free(dev);

    sst = NULL;
}

const device_t sst_flash_29ee010_device = {
    .name          = "SST 29EE010 Flash BIOS",
    .internal_name = "sst_flash_29ee010",
    .flags         = 0,
    .local         = SST | SST29EE010 | SIZE_1M,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sst_flash_29ee020_device = {
    .name          = "SST 29EE020 Flash BIOS",
    .internal_name = "sst_flash_29ee020",
    .flags         = 0,
    .local         = SST | SST29EE020 | SIZE_2M,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t winbond_flash_w29c512_device = {
    .name          = "Winbond W29C512 Flash BIOS",
    .internal_name = "winbond_flash_w29c512",
    .flags         = 0,
    .local         = WINBOND | W29C010 | SIZE_512K,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t winbond_flash_w29c010_device = {
    .name          = "Winbond W29C010 Flash BIOS",
    .internal_name = "winbond_flash_w29c010",
    .flags         = 0,
    .local         = WINBOND | W29C010 | SIZE_1M,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t winbond_flash_w29c011a_device = {
    .name          = "Winbond W29C011A Flash BIOS",
    .internal_name = "winbond_flash_w29c011a",
    .flags         = 0,
    .local         = WINBOND | W29C011A | SIZE_1M,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t winbond_flash_w29c020_device = {
    .name          = "Winbond W29C020 Flash BIOS",
    .internal_name = "winbond_flash_w29c020",
    .flags         = 0,
    .local         = WINBOND | W29C020 | SIZE_2M,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t winbond_flash_w29c040_device = {
    .name          = "Winbond W29C040 Flash BIOS",
    .internal_name = "winbond_flash_w29c040",
    .flags         = 0,
    .local         = WINBOND | W29C040 | SIZE_4M,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sst_flash_39sf512_device = {
    .name          = "SST 39SF512 Flash BIOS",
    .internal_name = "sst_flash_39sf512",
    .flags         = 0,
    .local         = SST | SST39SF512 | SIZE_512K,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sst_flash_39sf010_device = {
    .name          = "SST 39SF010 Flash BIOS",
    .internal_name = "sst_flash_39sf010",
    .flags         = 0,
    .local         = SST | SST39SF010 | SIZE_1M,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sst_flash_39sf020_device = {
    .name          = "SST 39SF020 Flash BIOS",
    .internal_name = "sst_flash_39sf020",
    .flags         = 0,
    .local         = SST | SST39SF020 | SIZE_2M,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sst_flash_39sf040_device = {
    .name          = "SST 39SF040 Flash BIOS",
    .internal_name = "sst_flash_39sf040",
    .flags         = 0,
    .local         = SST | SST39SF040 | SIZE_4M,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sst_flash_39lf512_device = {
    .name          = "SST 39LF512 Flash BIOS",
    .internal_name = "sst_flash_39lf512",
    .flags         = 0,
    .local         = SST | SST39LF512 | SIZE_512K,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sst_flash_39lf010_device = {
    .name          = "SST 39LF010 Flash BIOS",
    .internal_name = "sst_flash_39lf010",
    .flags         = 0,
    .local         = SST | SST39LF010 | SIZE_1M,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sst_flash_39lf020_device = {
    .name          = "SST 39LF020 Flash BIOS",
    .internal_name = "sst_flash_39lf020",
    .flags         = 0,
    .local         = SST | SST39LF020 | SIZE_2M,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sst_flash_39lf040_device = {
    .name          = "SST 39LF040 Flash BIOS",
    .internal_name = "sst_flash_39lf040",
    .flags         = 0,
    .local         = SST | SST39LF040 | SIZE_4M,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sst_flash_39lf080_device = {
    .name          = "SST 39LF080 Flash BIOS",
    .internal_name = "sst_flash_39lf080",
    .flags         = 0,
    .local         = SST | SST39LF080 | SIZE_8M,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sst_flash_39lf016_device = {
    .name          = "SST 39LF016 Flash BIOS",
    .internal_name = "sst_flash_39lf016",
    .flags         = 0,
    .local         = SST | SST39LF016 | SIZE_16M,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

/*
 * Firmware Hubs. The FWH signals are not implemented yet. Firmware Hubs do write cycles
 * to read/write on the flash. SST Flashes still do traditional flashing via PP Mode. Our
 * BIOS firmwares don't seem to utilize FWH R/W thus the FWH ports remain unknown for an
 * implementation. We just contain the ID's so the BIOS can do ESCD & DMI writes with no
 * worries.
 */

const device_t sst_flash_49lf002_device = {
    .name          = "SST 49LF002 Firmware Hub",
    .internal_name = "sst_flash_49lf002",
    .flags         = 0,
    .local         = SST | SST49LF002 | SIZE_2M,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sst_flash_49lf020_device = {
    .name          = "SST 49LF020 Firmware Hub",
    .internal_name = "sst_flash_49lf0020",
    .flags         = 0,
    .local         = SST | SST49LF020 | SIZE_2M,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sst_flash_49lf020a_device = {
    .name          = "SST 49LF020A Firmware Hub",
    .internal_name = "sst_flash_49lf0020a",
    .flags         = 0,
    .local         = SST | SST49LF020A | SIZE_2M,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sst_flash_49lf003_device = {
    .name          = "SST 49LF003 Firmware Hub",
    .internal_name = "sst_flash_49lf003",
    .flags         = 0,
    .local         = SST | SST49LF003 | SIZE_3M,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sst_flash_49lf030_device = {
    .name          = "SST 49LF030 Firmware Hub",
    .internal_name = "sst_flash_49lf030",
    .flags         = 0,
    .local         = SST | SST49LF030 | SIZE_3M,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sst_flash_49lf004_device = {
    .name          = "SST 49LF004 Firmware Hub",
    .internal_name = "sst_flash_49lf004",
    .flags         = 0,
    .local         = SST | SST49LF004 | SIZE_4M,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sst_flash_49lf004c_device = {
    .name          = "SST 49LF004C Firmware Hub",
    .internal_name = "sst_flash_49lf004c",
    .flags         = 0,
    .local         = SST | SST49LF004C | SIZE_4M,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sst_flash_49lf040_device = {
    .name          = "SST 49LF040 Firmware Hub",
    .internal_name = "sst_flash_49lf040",
    .flags         = 0,
    .local         = SST | SST49LF040 | SIZE_4M,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sst_flash_49lf008_device = {
    .name          = "SST 49LF008 Firmware Hub",
    .internal_name = "sst_flash_49lf008",
    .flags         = 0,
    .local         = SST | SST49LF008 | SIZE_8M,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sst_flash_49lf008c_device = {
    .name          = "SST 49LF008C Firmware Hub",
    .internal_name = "sst_flash_49lf008c",
    .flags         = 0,
    .local         = SST | SST49LF008C | SIZE_8M,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sst_flash_49lf080_device = {
    .name          = "SST 49LF080 Firmware Hub",
    .internal_name = "sst_flash_49lf080",
    .flags         = 0,
    .local         = SST | SST49LF080 | SIZE_8M,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sst_flash_49lf016_device = {
    .name          = "SST 49LF016 Firmware Hub",
    .internal_name = "sst_flash_49lf016",
    .flags         = 0,
    .local         = SST | SST49LF016 | SIZE_16M,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t sst_flash_49lf160_device = {

    .name          = "SST 49LF160 Firmware Hub",
    .internal_name = "sst_flash_49lf160",
    .flags         = 0,
    .local         = SST | SST49LF160 | SIZE_16M,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t amd_flash_29f010a_device = {
    .name          = "AMD 29F010a Flash BIOS",
    .internal_name = "amd_flash_29f010a",
    .flags         = 0,
    .local         = AMD | AMD29F010A | SIZE_1M,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t amd_flash_29f020a_device = {
    .name          = "AMD 29F020a Flash BIOS",
    .internal_name = "amd_flash_29f020a",
    .flags         = 0,
    .local         = AMD | AMD29F020A | SIZE_2M,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
const device_t amd_flash_29f002nbt_device = {
    .name          = "AMD Am29F002NBT Flash BIOS",
    .internal_name = "amd_flash_29f002nbt",
    .flags         = 0,
    .local         = AMD | AMD29F002NBT | SIZE_2M,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = sst_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
