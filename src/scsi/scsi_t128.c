/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Trantor 128/228 series of SCSI Host Adapters
 *          made by Trantor. These controllers were designed for the ISA and MCA bus.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          TheCollector1995, <mariogplayer@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2017-2019 Sarah Walker.
 *          Copyright 2017-2019 Fred N. van Kempen.
 *          Copyright 2017-2024 TheCollector1995.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/dma.h>
#include <86box/pic.h>
#include <86box/mca.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/nvr.h>
#include <86box/plat.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/scsi_ncr5380.h>
#include <86box/scsi_t128.h>

#define T128_ROM                "roms/scsi/ncr5380/trantor_t128_bios_v1.12.bin"

#ifdef ENABLE_T128_LOG
int t128_do_log = ENABLE_T128_LOG;

static void
t128_log(const char *fmt, ...)
{
    va_list ap;

    if (t128_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define t128_log(fmt, ...)
#endif

/* Memory-mapped I/O WRITE handler. */
void
t128_write(uint32_t addr, uint8_t val, void *priv)
{
    t128_t              *t128    = (t128_t *) priv;
    ncr_t               *ncr     = &t128->ncr;
    scsi_bus_t          *scsi_bus = &ncr->scsibus;
    const scsi_device_t *dev     = &scsi_devices[ncr->bus][scsi_bus->target_id];

    addr &= 0x3fff;
    if ((addr >= 0x1800) && (addr < 0x1880))
        t128->ext_ram[addr & 0x7f] = val;
    else if ((addr >= 0x1c00) && (addr < 0x1c20)) {
        if ((val & 0x02) && !(t128->ctrl & 0x02))
            t128->status |= 0x02;

        t128->ctrl = val;
        t128_log("T128 ctrl write=%02x\n", val);
    } else if ((addr >= 0x1d00) && (addr < 0x1e00))
        ncr5380_write((addr - 0x1d00) >> 5, val, ncr);
    else if ((addr >= 0x1e00) && (addr < 0x2000)) {
        if ((t128->host_pos < MIN(512, dev->buffer_length)) &&
            (scsi_bus->tx_mode == DMA_OUT_TX_BUS)) {
            t128->buffer[t128->host_pos] = val;
            t128->host_pos++;

            t128_log("T128 Write transfer, addr = %i, pos = %i, val = %02x\n",
                    addr & 0x1ff, t128->host_pos, val);

            if (t128->host_pos == MIN(512, dev->buffer_length)) {
                t128->status &= ~0x04;
                t128_log("Transfer busy write, status = %02x\n", t128->status);
                timer_on_auto(&t128->timer, 0.02);
            }
        }
    }
}

/* Memory-mapped I/O READ handler. */
uint8_t
t128_read(uint32_t addr, void *priv)
{
    t128_t        *t128    = (t128_t *) priv;
    ncr_t         *ncr     = &t128->ncr;
    scsi_bus_t    *scsi_bus = &ncr->scsibus;
    scsi_device_t *dev     = &scsi_devices[ncr->bus][scsi_bus->target_id];
    uint8_t        ret     = 0xff;

    addr &= 0x3fff;
    if (t128->bios_enabled && (addr >= 0) && (addr < 0x1800))
        ret = t128->bios_rom.rom[addr & 0x1fff];
    else if ((addr >= 0x1800) && (addr < 0x1880))
        ret = t128->ext_ram[addr & 0x7f];
    else if ((addr >= 0x1c00) && (addr < 0x1c20)) {
        ret = t128->ctrl;
        t128_log("T128 ctrl read=%02x, dma=%02x\n", ret, ncr->mode & MODE_DMA);
    } else if ((addr >= 0x1c20) && (addr < 0x1c40)) {
        ret = t128->status;
        t128_log("T128 status read=%02x, dma=%02x\n", ret, ncr->mode & MODE_DMA);
    } else if ((addr >= 0x1d00) && (addr < 0x1e00))
        ret = ncr5380_read((addr - 0x1d00) >> 5, ncr);
    else if (addr >= 0x1e00 && addr < 0x2000) {
        if ((t128->host_pos >= MIN(512, dev->buffer_length)) ||
            (scsi_bus->tx_mode != DMA_IN_TX_BUS))
            ret = 0xff;
        else {
            ret = t128->buffer[t128->host_pos++];

            t128_log("T128 Read transfer, addr = %i, pos = %i\n", addr & 0x1ff,
                    t128->host_pos);

            if (t128->host_pos == MIN(512, dev->buffer_length)) {
                t128->status &= ~0x04;
                t128_log("T128 Transfer busy read, status = %02x, period = %lf\n",
                        t128->status, scsi_bus->period);
                if ((scsi_bus->period == 0.2) || (scsi_bus->period == 0.02))
                    timer_on_auto(&t128->timer, 40.2);
            } else if ((t128->host_pos < MIN(512, dev->buffer_length)) &&
                       (scsi_device_get_callback(dev) > 100.0))
                cycles += 100; /*Needed to avoid timer de-syncing with transfers.*/
        }
    }

    return ret;
}

static void
t128_dma_mode_ext(void *priv, void *ext_priv, UNUSED(uint8_t val))
{
    t128_t      *t128   = (t128_t *) ext_priv;
    ncr_t       *ncr    = (ncr_t *) priv;
    scsi_bus_t  *scsi_bus = &ncr->scsibus;

    /*Don't stop the timer until it finishes the transfer*/
    if (t128->block_loaded && (ncr->mode & MODE_DMA)) {
        t128_log("Continuing DMA mode\n");
        timer_on_auto(&t128->timer, scsi_bus->period + 1.0);
    }

    /*When a pseudo-DMA transfer has completed (Send or Initiator Receive), mark it as complete and idle the status*/
    if (!t128->block_loaded && !(ncr->mode & MODE_DMA)) {
        t128_log("No DMA mode\n");
        ncr->tcr &= ~TCR_LAST_BYTE_SENT;
        ncr->isr &= ~STATUS_END_OF_DMA;
        scsi_bus->tx_mode = PIO_TX_BUS;
    }
}

static int
t128_dma_send_ext(void *priv, void *ext_priv)
{
    t128_t          *t128   = (t128_t *) ext_priv;
    ncr_t           *ncr    = (ncr_t *) priv;
    scsi_bus_t      *scsi_bus = &ncr->scsibus;
    scsi_device_t   *dev    = &scsi_devices[ncr->bus][scsi_bus->target_id];

    if ((ncr->mode & MODE_DMA) && !timer_is_on(&t128->timer) && (dev->buffer_length > 0)) {
        memset(t128->buffer, 0, MIN(512, dev->buffer_length));
        t128->status |= 0x04;
        t128->host_pos = 0;
        t128->block_count = dev->buffer_length >> 9;

        if (dev->buffer_length < 512)
            t128->block_count = 1;

        t128->block_loaded = 1;
    }
    return 1;
}

static int
t128_dma_initiator_receive_ext(void *priv, void *ext_priv)
{
    t128_t          *t128   = (t128_t *) ext_priv;
    ncr_t           *ncr    = (ncr_t *) priv;
    scsi_bus_t      *scsi_bus = &ncr->scsibus;
    scsi_device_t   *dev    = &scsi_devices[ncr->bus][scsi_bus->target_id];

    if ((ncr->mode & MODE_DMA) && !timer_is_on(&t128->timer) && (dev->buffer_length > 0)) {
        memset(t128->buffer, 0, MIN(512, dev->buffer_length));
        t128->status |= 0x04;
        t128->host_pos = MIN(512, dev->buffer_length);
        t128->block_count = dev->buffer_length >> 9;

        if (dev->buffer_length < 512)
            t128->block_count = 1;

        t128->block_loaded = 1;
        timer_on_auto(&t128->timer, 0.02);
    }
    return 1;
}

static void
t128_timer_on_auto(void *ext_priv, double period)
{
    t128_t          *t128   = (t128_t *) ext_priv;

    if (period == 0.0)
        timer_stop(&t128->timer);
    else
        timer_on_auto(&t128->timer, period);
}

void
t128_callback(void *priv)
{
    t128_t         *t128       = (void *) priv;
    ncr_t          *ncr        = &t128->ncr;
    scsi_bus_t     *scsi_bus   = &ncr->scsibus;
    scsi_device_t  *dev        = &scsi_devices[ncr->bus][scsi_bus->target_id];
    int             bus;
    uint8_t         c;
    uint8_t         temp;
    uint8_t         status;

    if ((scsi_bus->tx_mode != PIO_TX_BUS) && (ncr->mode & MODE_DMA) && t128->block_loaded) {
        if ((t128->host_pos == MIN(512, dev->buffer_length)) && t128->block_count)
            t128->status |= 0x04;

        timer_on_auto(&t128->timer, scsi_bus->period / 55.0);
    }

    if (scsi_bus->data_wait & 1) {
        scsi_bus->clear_req = 3;
        scsi_bus->data_wait &= ~1;
        if (scsi_bus->tx_mode == PIO_TX_BUS)
            return;
    }

    switch (scsi_bus->tx_mode) {
        case DMA_OUT_TX_BUS:
            if (!(t128->status & 0x04)) {
                t128_log("Write status busy, block count = %i, host pos = %i\n", t128->block_count, t128->host_pos);
                break;
            }

            if (!t128->block_loaded) {
                t128_log("Write block not loaded\n");
                break;
            }

            if (t128->host_pos < MIN(512, dev->buffer_length))
                break;

write_again:
            for (c = 0; c < 10; c++) {
                status = scsi_bus_read(scsi_bus);
                if (status & BUS_REQ)
                    break;
            }

            /* Data ready. */
            temp = t128->buffer[t128->pos];

            bus = ncr5380_get_bus_host(ncr) & ~BUS_DATAMASK;
            bus |= BUS_SETDATA(temp);

            scsi_bus_update(scsi_bus, bus | BUS_ACK);
            scsi_bus_update(scsi_bus, bus & ~BUS_ACK);

            t128->pos++;
            t128_log("T128 Buffer pos for writing = %d\n", t128->pos);

            if (t128->pos == MIN(512, dev->buffer_length)) {
                t128->pos = 0;
                t128->host_pos = 0;
                t128->status &= ~0x02;
                t128->block_count = (t128->block_count - 1) & 0xff;
                t128_log("T128 Remaining blocks to be written=%d\n", t128->block_count);
                if (!t128->block_count) {
                    t128->block_loaded = 0;
                    t128_log("IO End of write transfer\n");
                    ncr->tcr |= TCR_LAST_BYTE_SENT;
                    ncr->isr |= STATUS_END_OF_DMA;
                    timer_stop(&t128->timer);
                    if (ncr->mode & MODE_ENA_EOP_INT) {
                        t128_log("T128 write irq\n");
                        ncr5380_irq(ncr, 1);
                    }
                }
                break;
            } else
                goto write_again;
            break;

        case DMA_IN_TX_BUS:
            if (!(t128->status & 0x04)) {
                t128_log("Read status busy, block count = %i, host pos = %i\n", t128->block_count, t128->host_pos);
                break;
            }

            if (!t128->block_loaded) {
                t128_log("Read block not loaded\n");
                break;
            }

            if (t128->host_pos < MIN(512, dev->buffer_length))
                break;

read_again:
            for (c = 0; c < 10; c++) {
                status = scsi_bus_read(scsi_bus);
                if (status & BUS_REQ)
                    break;
            }

            /* Data ready. */
            bus = scsi_bus_read(scsi_bus);
            temp = BUS_GETDATA(bus);

            bus = ncr5380_get_bus_host(ncr);

            scsi_bus_update(scsi_bus, bus | BUS_ACK);
            scsi_bus_update(scsi_bus, bus & ~BUS_ACK);

            t128->buffer[t128->pos++] = temp;
            t128_log("T128 Buffer pos for reading=%d, temp=%02x, len=%d.\n", t128->pos, temp, dev->buffer_length);

            if (t128->pos == MIN(512, dev->buffer_length)) {
                t128->pos      = 0;
                t128->host_pos = 0;
                t128->status &= ~0x02;
                t128->block_count = (t128->block_count - 1) & 0xff;
                t128_log("T128 Remaining blocks to be read=%d, status=%02x, len=%i, cdb[0] = %02x\n", t128->block_count, t128->status, dev->buffer_length, ncr->command[0]);
                if (!t128->block_count) {
                    t128->block_loaded = 0;
                    t128_log("IO End of read transfer\n");
                    ncr->isr |= STATUS_END_OF_DMA;
                    timer_stop(&t128->timer);
                    if (ncr->mode & MODE_ENA_EOP_INT) {
                        t128_log("NCR read irq\n");
                        ncr5380_irq(ncr, 1);
                    }
                }
                break;
            } else
                goto read_again;
            break;

        default:
            break;
    }

    status = scsi_bus_read(scsi_bus);

    if (!(status & BUS_BSY) && (ncr->mode & MODE_MONITOR_BUSY)) {
        t128_log("Updating DMA\n");
        ncr->mode &= ~MODE_DMA;
        scsi_bus->tx_mode = PIO_TX_BUS;
        timer_on_auto(&t128->timer, 10.0);
    }
}

static uint8_t
t228_read(int port, void *priv)
{
    const t128_t *t128 = (t128_t *) priv;

    return (t128->pos_regs[port & 7]);
}

static void
t228_write(int port, uint8_t val, void *priv)
{
    t128_t *t128 = (t128_t *) priv;
    ncr_t  *ncr  = &t128->ncr;

    /* MCA does not write registers below 0x0100. */
    if (port < 0x0102)
        return;

    mem_mapping_disable(&t128->bios_rom.mapping);
    mem_mapping_disable(&t128->mapping);

    /* Save the MCA register value. */
    t128->pos_regs[port & 7] = val;

    if (t128->pos_regs[2] & 1) {
        switch (t128->pos_regs[2] & 6) {
            case 0:
                t128->rom_addr = 0xcc000;
                break;
            case 2:
                t128->rom_addr = 0xc8000;
                break;
            case 4:
                t128->rom_addr = 0xdc000;
                break;
            case 6:
                t128->rom_addr = 0xd8000;
                break;

            default:
                break;
        }

        t128->bios_enabled = !(t128->pos_regs[2] & 8);

        switch (t128->pos_regs[2] & 0x70) {
            case 0:
                ncr->irq = -1;
                break;
            case 0x10:
                ncr->irq = 3;
                break;
            case 0x20:
                ncr->irq = 5;
                break;
            case 0x30:
                ncr->irq = 7;
                break;
            case 0x40:
                ncr->irq = 10;
                break;
            case 0x50:
                ncr->irq = 12;
                break;
            case 0x60:
                ncr->irq = 14;
                break;
            case 0x70:
                ncr->irq = 15;
                break;

            default:
                break;
        }

        if (t128->bios_enabled) {
            t128->status &= ~0x80;
            mem_mapping_set_addr(&t128->bios_rom.mapping, t128->rom_addr, 0x4000);
            mem_mapping_set_addr(&t128->mapping, t128->rom_addr, 0x4000);
        } else
            t128->status |= 0x80;
    }
}

static uint8_t
t228_feedb(void *priv)
{
    const t128_t *t128 = (t128_t *) priv;

    return t128->pos_regs[2] & 1;
}

static void *
t128_init(const device_t *info)
{
    t128_t      *t128 = calloc(1, sizeof(t128_t));
    ncr_t       *ncr  = &t128->ncr;
    scsi_bus_t  *scsi_bus;

    ncr->bus = scsi_get_bus();
    scsi_bus = &ncr->scsibus;

    if (info->flags & DEVICE_MCA) {
        rom_init(&t128->bios_rom, T128_ROM,
                 0xd8000, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);
        mem_mapping_add(&t128->mapping, 0xd8000, 0x4000,
                        t128_read, NULL, NULL,
                        t128_write, NULL, NULL,
                        t128->bios_rom.rom, MEM_MAPPING_EXTERNAL, t128);
        mem_mapping_disable(&t128->bios_rom.mapping);
        t128->pos_regs[0]   = 0x8c;
        t128->pos_regs[1]   = 0x50;
        mca_add(t228_read, t228_write, t228_feedb, NULL, t128);
    } else if (info->local == 0) {
        ncr->irq            = device_get_config_int("irq");
        t128->rom_addr      = device_get_config_hex20("bios_addr");
        t128->bios_enabled  = device_get_config_int("boot");
        if (t128->bios_enabled)
            rom_init(&t128->bios_rom, T128_ROM,
                     t128->rom_addr, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);

        mem_mapping_add(&t128->mapping, t128->rom_addr, 0x4000,
                        t128_read, NULL, NULL,
                        t128_write, NULL, NULL,
                        t128->bios_rom.rom, MEM_MAPPING_EXTERNAL, t128);
    }

    ncr->priv                       = t128;
    ncr->dma_mode_ext               = t128_dma_mode_ext;
    ncr->dma_send_ext               = t128_dma_send_ext;
    ncr->dma_initiator_receive_ext  = t128_dma_initiator_receive_ext;
    ncr->timer                      = t128_timer_on_auto;
    scsi_bus->bus_device            = ncr->bus;
    scsi_bus->timer                 = ncr->timer;
    scsi_bus->priv                  = ncr->priv;
    t128->status                    = 0x00 /*0x04*/;
    t128->host_pos                  = 512;
    if (!t128->bios_enabled && !(info->flags & DEVICE_MCA))
        t128->status                |= 0x80;

    if (info->flags & DEVICE_MCA)
        t128->status                |= 0x08;

    if (info->local == 0)
        timer_add(&t128->timer, t128_callback, t128, 0);

    scsi_bus_set_speed(ncr->bus, 5000000.0);
    scsi_bus->speed = 0.2;
    scsi_bus->divider = 1.0;
    scsi_bus->multi = 1.0;

    return t128;
}

static void
t128_close(void *priv)
{
    t128_t *t128 = (t128_t *) priv;

    if (t128) {
        /* Tell the timer to terminate. */
        timer_stop(&t128->timer);

        free(t128);
        t128 = NULL;
    }
}

static int
t128_available(void)
{
    return (rom_present(T128_ROM));
}

// clang-format off
static const device_config_t t128_config[] = {
    {
        .name           = "bios_addr",
        .description    = "BIOS Address",
        .type           = CONFIG_HEX20,
        .default_string = NULL,
        .default_int    = 0xD8000,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "C800H", .value = 0xc8000 },
            { .description = "CC00H", .value = 0xcc000 },
            { .description = "D800H", .value = 0xd8000 },
            { .description = "DC00H", .value = 0xdc000 },
            { .description = ""                        }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "irq",
        .description    = "IRQ",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 5,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "None",  .value = -1 },
            { .description = "IRQ 3", .value = 3 },
            { .description = "IRQ 5", .value = 5 },
            { .description = "IRQ 7", .value = 7 },
            { .description = ""                  }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "boot",
        .description    = "Enable BIOS",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t scsi_t128_device = {
    .name          = "Trantor T128",
    .internal_name = "t128",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = t128_init,
    .close         = t128_close,
    .reset         = NULL,
    .available     = t128_available,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = t128_config
};

const device_t scsi_t228_device = {
    .name          = "Trantor T228",
    .internal_name = "t228",
    .flags         = DEVICE_MCA,
    .local         = 0,
    .init          = t128_init,
    .close         = t128_close,
    .reset         = NULL,
    .available     = t128_available,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t scsi_pas_device = {
    .name          = "Pro Audio Spectrum Plus/16 SCSI",
    .internal_name = "scsi_pas",
    .flags         = DEVICE_ISA,
    .local         = 1,
    .init          = t128_init,
    .close         = t128_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
