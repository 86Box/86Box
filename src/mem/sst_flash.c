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
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2016-2020 Miran Grca.
 *          Copyright 2022-2023 Jasmine Iwanek.
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
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/plat.h>
#include <86box/m_xt_xi8088.h>

typedef struct sst_t {
    uint8_t manufacturer;
    uint8_t id;
    uint8_t has_bbp;
    uint8_t is_39;
    uint8_t page_bytes;
    uint8_t sdp;
    uint8_t bbp_first_8k;
    uint8_t bbp_last_8k;

    int command_state;
    int id_mode;
    int dirty;

    uint32_t size;
    uint32_t mask;
    uint32_t page_mask;
    uint32_t page_base;
    uint32_t last_addr;

    uint8_t  page_buffer[128];
    uint8_t  page_dirty[128];
    uint8_t *array;

    mem_mapping_t mapping[8];
    mem_mapping_t mapping_h[8];

    pc_timer_t page_write_timer;
} sst_t;

static char flash_path[1024];

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
#define W29C020     0x4500
#define W29C040     0x4600

#define AMD         0x01 /* AMD Manufacturer's ID */
#define AMD29F020A  0xb000

#define SIZE_512K   0x010000
#define SIZE_1M     0x020000
#define SIZE_2M     0x040000
#define SIZE_3M     0x060000
#define SIZE_4M     0x080000
#define SIZE_8M     0x100000
#define SIZE_16M    0x200000

static void
sst_sector_erase(sst_t *dev, uint32_t addr)
{
    uint32_t base = addr & (dev->mask & ~0xfff);

    if (dev->manufacturer == AMD) {
        base = addr & biosmask;

        if ((base >= 0x00000) && (base <= 0x0ffff))
            memset(&dev->array[0x00000], 0xff, 65536);
        else if ((base >= 0x10000) && (base <= 0x1ffff))
            memset(&dev->array[0x10000], 0xff, 65536);
        else if ((base >= 0x20000) && (base <= 0x2ffff))
            memset(&dev->array[0x20000], 0xff, 65536);
        else if ((base >= 0x30000) && (base <= 0x37fff))
            memset(&dev->array[0x30000], 0xff, 32768);
        else if ((base >= 0x38000) && (base <= 0x39fff))
            memset(&dev->array[0x38000], 0xff, 8192);
        else if ((base >= 0x3a000) && (base <= 0x3bfff))
            memset(&dev->array[0x3a000], 0xff, 8192);
        else if ((base >= 0x3c000) && (base <= 0x3ffff))
            memset(&dev->array[0x3c000], 0xff, 16384);
    } else {
        if ((base < 0x2000) && (dev->bbp_first_8k & 0x01))
            return;
        else if ((base >= (dev->size - 0x2000)) && (dev->bbp_last_8k & 0x01))
            return;

        memset(&dev->array[base], 0xff, 4096);
    }

    dev->dirty = 1;
}

static void
sst_new_command(sst_t *dev, uint32_t addr, uint8_t val)
{
    uint32_t base = 0x00000;
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
                else if (((dev->page_base + i) >= (dev->size - 0x2000)) && (dev->bbp_last_8k & 0x01))
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
    const sst_t  *dev = (sst_t *) priv;
    uint8_t       ret = 0xff;

    addr &= 0x000fffff;

    if (dev->id_mode)
        ret = sst_read_id(addr, priv);
    else {
        if ((addr >= biosaddr) && (addr <= (biosaddr + biosmask)))
            ret = dev->array[addr - biosaddr];
    }

    return ret;
}

static uint16_t
sst_readw(uint32_t addr, void *priv)
{
    sst_t   *dev = (sst_t *) priv;
    uint16_t ret = 0xffff;

    addr &= 0x000fffff;

    if (dev->id_mode)
        ret = sst_read(addr, priv) | (sst_read(addr + 1, priv) << 8);
    else {
        if ((addr >= biosaddr) && (addr <= (biosaddr + biosmask)))
            ret = *(uint16_t *) &dev->array[addr - biosaddr];
    }

    return ret;
}

static uint32_t
sst_readl(uint32_t addr, void *priv)
{
    sst_t   *dev = (sst_t *) priv;
    uint32_t ret = 0xffffffff;

    addr &= 0x000fffff;

    if (dev->id_mode)
        ret = sst_readw(addr, priv) | (sst_readw(addr + 2, priv) << 16);
    else {
        if ((addr >= biosaddr) && (addr <= (biosaddr + biosmask)))
            ret = *(uint32_t *) &dev->array[addr - biosaddr];
    }

    return ret;
}

static void
sst_add_mappings(sst_t *dev)
{
    int      count;
    uint32_t base;
    uint32_t fbase;
    uint32_t root_base;

    count     = dev->size >> 16;
    root_base = 0x100000 - dev->size;

    for (int i = 0; i < count; i++) {
        base  = root_base + (i << 16);
        fbase = base & biosmask;

        memcpy(&dev->array[fbase], &rom[base & biosmask], 0x10000);

        if (base >= 0xe0000) {
            mem_mapping_add(&(dev->mapping[i]), base, 0x10000,
                            sst_read, sst_readw, sst_readl,
                            sst_write, NULL, NULL,
                            dev->array + fbase, MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM | MEM_MAPPING_ROMCS, (void *) dev);
        }
        if (is6117) {
            mem_mapping_add(&(dev->mapping_h[i]), (base | 0x3f00000), 0x10000,
                            sst_read, sst_readw, sst_readl,
                            sst_write, NULL, NULL,
                            dev->array + fbase, MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM | MEM_MAPPING_ROMCS, (void *) dev);
        } else {
            mem_mapping_add(&(dev->mapping_h[i]), (base | (cpu_16bitbus ? 0xf00000 : 0xfff00000)), 0x10000,
                            sst_read, sst_readw, sst_readl,
                            sst_write, NULL, NULL,
                            dev->array + fbase, MEM_MAPPING_EXTERNAL | MEM_MAPPING_ROM | MEM_MAPPING_ROMCS, (void *) dev);
        }
    }
}

static void *
sst_init(const device_t *info)
{
    FILE  *fp;
    sst_t *dev = calloc(1, sizeof(sst_t));

    sprintf(flash_path, "%s.bin", machine_get_nvr_name_ex(machine));

    mem_mapping_disable(&bios_mapping);
    mem_mapping_disable(&bios_high_mapping);

    dev->array = (uint8_t *) malloc(biosmask + 1);
    memset(dev->array, 0xff, biosmask + 1);

    dev->manufacturer = info->local & 0xff;
    dev->id           = (info->local >> 8) & 0xff;
    dev->has_bbp      = (dev->manufacturer == WINBOND) && ((info->local & 0xff00) >= W29C020);
    dev->is_39        = (dev->manufacturer == SST) && ((info->local & 0xff00) >= SST39SF512);
    if (dev->manufacturer == AMD)
        dev->is_39    = 1;

    dev->size = info->local & 0xffff0000;
    if ((dev->size == 0x20000) && (strstr(machine_get_internal_name_ex(machine), "xi8088")) && !xi8088_bios_128kb())
        dev->size = 0x10000;

    dev->mask         = dev->size - 1;
    dev->page_mask    = dev->mask & 0xffffff80; /* Filter out A0-A6. */
    dev->sdp          = 1;
    dev->bbp_first_8k = dev->bbp_last_8k = 0xfe;

    sst_add_mappings(dev);

    fp = nvr_fopen(flash_path, "rb");
    if (fp) {
        if (fread(&(dev->array[0x00000]), 1, dev->size, fp) != dev->size)
            pclog("Less than %i bytes read from the SST Flash ROM file\n", dev->size);
        fclose(fp);
    } else
        dev->dirty = 1; /* It is by definition dirty on creation. */

    if (!dev->is_39)
        timer_add(&dev->page_write_timer, sst_page_write, dev, 0);

    return dev;
}

static void
sst_close(void *priv)
{
    FILE  *fp;
    sst_t *dev = (sst_t *) priv;

    if (dev->dirty) {
        fp = nvr_fopen(flash_path, "wb");
        if (fp != NULL) {
            fwrite(&(dev->array[0x00000]), dev->size, 1, fp);
            fclose(fp);
        }
    }

    free(dev->array);
    dev->array = NULL;

    free(dev);
}

const device_t sst_flash_29ee010_device = {
    .name          = "SST 29EE010 Flash BIOS",
    .internal_name = "sst_flash_29ee010",
    .flags         = 0,
    .local         = SST | SST29EE010 | SIZE_1M,
    .init          = sst_init,
    .close         = sst_close,
    .reset         = NULL,
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
    .reset         = NULL,
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
    .reset         = NULL,
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
    .reset         = NULL,
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
    .reset         = NULL,
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
    .reset         = NULL,
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
    .reset         = NULL,
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
    .reset         = NULL,
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
    .reset         = NULL,
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
    .reset         = NULL,
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
    .reset         = NULL,
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
    .reset         = NULL,
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
    .reset         = NULL,
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
    .reset         = NULL,
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
    .reset         = NULL,
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
    .reset         = NULL,
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
    .reset         = NULL,
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
    .reset         = NULL,
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
    .reset         = NULL,
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
    .reset         = NULL,
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
    .reset         = NULL,
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
    .reset         = NULL,
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
    .reset         = NULL,
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
    .reset         = NULL,
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
    .reset         = NULL,
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
    .reset         = NULL,
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
    .reset         = NULL,
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
    .reset         = NULL,
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
    .reset         = NULL,
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
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
