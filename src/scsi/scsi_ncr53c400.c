/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the NCR 53c400 series of SCSI Host Adapters
 *          made by NCR. These controllers were designed for the ISA and MCA bus.
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

#define LCS6821N_ROM            "roms/scsi/ncr5380/Longshine LCS-6821N - BIOS version 1.04.bin"
#define COREL_LS2000_ROM        "roms/scsi/ncr5380/Corel LS2000 - BIOS ROM - Ver 1.65.bin"
#define RT1000B_810R_ROM        "roms/scsi/ncr5380/Rancho_RT1000_RTBios_version_8.10R.bin"
#define RT1000B_820R_ROM        "roms/scsi/ncr5380/RTBIOS82.ROM"
#define T130B_ROM               "roms/scsi/ncr5380/trantor_t130b_bios_v2.14.bin"

#define CTRL_DATA_DIR           0x40
#define STATUS_BUFFER_NOT_READY 0x04
#define STATUS_5380_ACCESSIBLE  0x80

enum {
    ROM_LCS6821N = 0,
    ROM_LS2000,
    ROM_RT1000B,
    ROM_T130B
};

typedef struct ncr53c400_t {
    rom_t         bios_rom;
    mem_mapping_t mapping;
    ncr_t   ncr;
    uint8_t buffer[128];
    uint8_t int_ram[0x40];
    uint8_t ext_ram[0x600];

    uint32_t rom_addr;
    uint16_t base;

    int8_t  type;
    uint8_t block_count;
    uint8_t status_ctrl;

    int block_count_loaded;

    int buffer_pos;
    int buffer_host_pos;

    int     busy;
    uint8_t pos_regs[8];

    pc_timer_t timer;
} ncr53c400_t;

#ifdef ENABLE_NCR53C400_LOG
int ncr53c400_do_log = ENABLE_NCR53C400_LOG;

static void
ncr53c400_log(const char *fmt, ...)
{
    va_list ap;

    if (ncr53c400_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ncr53c400_log(fmt, ...)
#endif

static void
ncr53c400_timer_on_auto(void *ext_priv, double period)
{
    ncr53c400_t *ncr400 = (ncr53c400_t *) ext_priv;

    ncr53c400_log("53c400: PERIOD=%lf, timer=%x.\n", period, timer_is_enabled(&ncr400->timer));
    if (period <= 0.0)
        timer_stop(&ncr400->timer);
    else if ((period > 0.0) && !timer_is_enabled(&ncr400->timer))
        timer_on_auto(&ncr400->timer, period);
}

/* Memory-mapped I/O WRITE handler. */
static void
ncr53c400_write(uint32_t addr, uint8_t val, void *priv)
{
    ncr53c400_t         *ncr400     = (ncr53c400_t *) priv;
    ncr_t               *ncr        = &ncr400->ncr;
    scsi_bus_t          *scsi_bus   = &ncr->scsibus;
    scsi_device_t       *dev        = &scsi_devices[ncr->bus][scsi_bus->target_id];

    addr &= 0x3fff;

    if (addr >= 0x3a00)
        ncr400->ext_ram[addr - 0x3a00] = val;
    else {
        switch (addr & 0x3f80) {
            case 0x3800:
                ncr400->int_ram[addr & 0x3f] = val;
                break;

            case 0x3880:
                ncr5380_write(addr, val, ncr);
                break;

            case 0x3900:
                if (!(ncr400->status_ctrl & CTRL_DATA_DIR) && (ncr400->buffer_host_pos < MIN(128, dev->buffer_length))) {
                    ncr400->buffer[ncr400->buffer_host_pos++] = val;

                    ncr53c400_log("Write host pos=%i, val=%02x.\n", ncr400->buffer_host_pos, val);

                    if (ncr400->buffer_host_pos == MIN(128, dev->buffer_length)) {
                        ncr400->status_ctrl |= STATUS_BUFFER_NOT_READY;
                        ncr400->busy = 1;
                    }
                } else
                    ncr53c400_log("No Write.\n");
                break;

            case 0x3980:
                switch (addr) {
                    case 0x3980: /* Control */
                        ncr53c400_log("NCR 53c400 control=%02x, mode=%02x.\n", val, ncr->mode);
                        if ((val & CTRL_DATA_DIR) && !(ncr400->status_ctrl & CTRL_DATA_DIR)) {
                            ncr400->buffer_host_pos = MIN(128, dev->buffer_length);
                            ncr400->status_ctrl |= STATUS_BUFFER_NOT_READY;
                        } else if (!(val & CTRL_DATA_DIR) && (ncr400->status_ctrl & CTRL_DATA_DIR)) {
                            ncr400->buffer_host_pos = 0;
                            ncr400->status_ctrl &= ~STATUS_BUFFER_NOT_READY;
                        }
                        ncr400->status_ctrl = (ncr400->status_ctrl & 0x87) | (val & 0x78);
                        break;

                    case 0x3981: /* block counter register */
                        ncr53c400_log("Write block counter register: val=%d, dma mode=%x, period=%lf.\n", val, scsi_bus->tx_mode, scsi_bus->period);
                        ncr400->block_count        = val;
                        ncr400->block_count_loaded = 1;

                        if (ncr400->status_ctrl & CTRL_DATA_DIR) {
                            ncr400->buffer_host_pos = MIN(128, dev->buffer_length);
                            ncr400->status_ctrl |= STATUS_BUFFER_NOT_READY;
                        } else {
                            ncr400->buffer_host_pos = 0;
                            ncr400->status_ctrl &= ~STATUS_BUFFER_NOT_READY;
                        }
                        if ((ncr->mode & MODE_DMA) && (dev->buffer_length > 0)) {
                            memset(ncr400->buffer, 0, MIN(128, dev->buffer_length));
                            if (ncr400->type == ROM_T130B)
                                timer_on_auto(&ncr400->timer, 10.0);
                            else
                                timer_on_auto(&ncr400->timer, scsi_bus->period);
                            ncr53c400_log("DMA timer on=%02x, callback=%lf, scsi buflen=%d, waitdata=%d, waitcomplete=%d, clearreq=%d, p=%lf enabled=%d.\n",
                                  ncr->mode & MODE_MONITOR_BUSY, scsi_device_get_callback(dev), dev->buffer_length, scsi_bus->wait_data, scsi_bus->wait_complete, scsi_bus->clear_req, scsi_bus->period, timer_is_enabled(&ncr400->timer));
                        } else
                            ncr53c400_log("No Timer.\n");
                        break;

                    default:
                        break;
                }
                break;

            default:
                break;
        }
    }
}

/* Memory-mapped I/O READ handler. */
static uint8_t
ncr53c400_read(uint32_t addr, void *priv)
{
    ncr53c400_t         *ncr400     = (ncr53c400_t *) priv;
    ncr_t               *ncr        = &ncr400->ncr;
    scsi_bus_t          *scsi_bus   = &ncr->scsibus;
    scsi_device_t       *dev        = &scsi_devices[ncr->bus][scsi_bus->target_id];
    uint8_t              ret        = 0xff;

    addr &= 0x3fff;

    if (addr < 0x2000)
        ret = ncr400->bios_rom.rom[addr & 0x1fff];
    else if (addr < 0x3800)
        ret = 0xff;
    else if (addr >= 0x3a00)
        ret = ncr400->ext_ram[addr - 0x3a00];
    else {
        switch (addr & 0x3f80) {
            case 0x3800:
                ncr53c400_log("Read intRAM %02x %02x.\n", addr & 0x3f, ncr400->int_ram[addr & 0x3f]);
                ret = ncr400->int_ram[addr & 0x3f];
                break;

            case 0x3880:
                ncr53c400_log("Read 5380 %04x.\n", addr);
                ret = ncr5380_read(addr, ncr);
                break;

            case 0x3900:
                if ((ncr400->buffer_host_pos >= MIN(128, dev->buffer_length)) || (!(ncr400->status_ctrl & CTRL_DATA_DIR))) {
                    ret = 0xff;
                    ncr53c400_log("No Read.\n");
                } else {
                    ret = ncr400->buffer[ncr400->buffer_host_pos++];
                    ncr53c400_log("Read host pos=%i, ret=%02x.\n", ncr400->buffer_host_pos, ret);

                    if (ncr400->buffer_host_pos == MIN(128, dev->buffer_length)) {
                        ncr400->status_ctrl |= STATUS_BUFFER_NOT_READY;
                    }
                }
                break;

            case 0x3980:
                switch (addr) {
                    case 0x3980: /* status */
                        ret = ncr400->status_ctrl;
                        ncr53c400_log("NCR status ctrl read=%02x.\n", ncr400->status_ctrl & STATUS_BUFFER_NOT_READY);
                        if (!ncr400->busy)
                            ret |= STATUS_5380_ACCESSIBLE;
                        if (ncr->mode & 0x30) {            /*Parity bits*/
                            if (!(ncr->mode & MODE_DMA)) { /*This is to avoid RTBios 8.10R BIOS problems with the hard disk and detection.*/
                                ret |= 0x01;               /*If the parity bits are set, bit 0 of the 53c400 status port should be set as well.*/
                                ncr->mode = 0x00;          /*Required by RTASPI10.SYS otherwise it won't initialize.*/
                            }
                        }
                        ncr53c400_log("NCR 53c400 status=%02x.\n", ret);
                        break;

                    case 0x3981: /* block counter register*/
                        ret = ncr400->block_count;
                        ncr53c400_log("NCR 53c400 block count read=%02x.\n", ret);
                        break;

                    case 0x3982: /* switch register read */
                        ret = 0xff;
                        ncr53c400_log("Switches read=%02x.\n", ret);
                        break;

                    default:
                        break;
                }
                break;

            default:
                break;
        }
    }

    if (addr >= 0x3880)
        ncr53c400_log("memio_read(%08x)=%02x\n", addr, ret);

    return ret;
}


/* Memory-mapped I/O WRITE handler for the Trantor T130B. */
static void
t130b_write(uint32_t addr, uint8_t val, void *priv)
{
    ncr53c400_t *ncr400 = (ncr53c400_t *) priv;

    addr &= 0x3fff;
    ncr53c400_log("MEM: Writing %02X to %08X\n", val, addr);
    if (addr >= 0x1800 && addr < 0x1880)
        ncr400->ext_ram[addr & 0x7f] = val;
}

/* Memory-mapped I/O READ handler for the Trantor T130B. */
static uint8_t
t130b_read(uint32_t addr, void *priv)
{
    const ncr53c400_t *ncr400  = (ncr53c400_t *) priv;
    uint8_t            ret     = 0xff;

    addr &= 0x3fff;
    if (addr < 0x1800)
        ret = ncr400->bios_rom.rom[addr & 0x1fff];
    else if (addr >= 0x1800 && addr < 0x1880)
        ret = ncr400->ext_ram[addr & 0x7f];

    ncr53c400_log("MEM: Reading %02X from %08X\n", ret, addr);
    return ret;
}

static void
t130b_out(uint16_t port, uint8_t val, void *priv)
{
    ncr53c400_t *ncr400 = (ncr53c400_t *) priv;
    ncr_t       *ncr    = &ncr400->ncr;

    ncr53c400_log("I/O: Writing %02X to %04X\n", val, port);

    switch (port & 0x0f) {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
            ncr53c400_write((port & 7) | 0x3980, val, ncr400);
            break;

        case 0x04:
        case 0x05:
            ncr53c400_write(0x3900, val, ncr400);
            break;

        case 0x08:
        case 0x09:
        case 0x0a:
        case 0x0b:
        case 0x0c:
        case 0x0d:
        case 0x0e:
        case 0x0f:
            ncr5380_write(port, val, ncr);
            break;

        default:
            break;
    }
}

static uint8_t
t130b_in(uint16_t port, void *priv)
{
    ncr53c400_t *ncr400 = (ncr53c400_t *) priv;
    ncr_t       *ncr    = &ncr400->ncr;
    uint8_t      ret     = 0xff;

    switch (port & 0x0f) {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
            ret = ncr53c400_read((port & 7) | 0x3980, ncr400);
            break;

        case 0x04:
        case 0x05:
            ret = ncr53c400_read(0x3900, ncr400);
            break;

        case 0x08:
        case 0x09:
        case 0x0a:
        case 0x0b:
        case 0x0c:
        case 0x0d:
        case 0x0e:
        case 0x0f:
            ret = ncr5380_read(port, ncr);
            break;

        default:
            break;
    }

    ncr53c400_log("I/O: Reading %02X from %04X\n", ret, port);
    return ret;
}

static void
ncr53c400_dma_mode_ext(void *priv, void *ext_priv, uint8_t val)
{
    ncr53c400_t    *ncr400     = (ncr53c400_t *) ext_priv;
    ncr_t          *ncr        = (ncr_t *) priv;
    scsi_bus_t     *scsi_bus   = &ncr->scsibus;

    /*When a pseudo-DMA transfer has completed (Send or Initiator Receive), mark it as complete and idle the status*/
    ncr53c400_log("NCR 53c400: Loaded?=%d, DMA mode enabled=%02x, valDMA=%02x.\n", ncr400->block_count_loaded, ncr->mode & MODE_DMA, val & MODE_DMA);
    if (!ncr400->block_count_loaded) {
        if (!(val & MODE_DMA)) {
            ncr->tcr &= ~TCR_LAST_BYTE_SENT;
            ncr->isr &= ~STATUS_END_OF_DMA;
            scsi_bus->tx_mode = PIO_TX_BUS;
        }
    }
}

static void
ncr53c400_callback(void *priv)
{
    ncr53c400_t    *ncr400     = (ncr53c400_t *) priv;
    ncr_t          *ncr        = &ncr400->ncr;
    scsi_bus_t     *scsi_bus   = &ncr->scsibus;
    scsi_device_t  *dev        = &scsi_devices[ncr->bus][scsi_bus->target_id];
    int            bus;
    uint8_t        c;
    uint8_t        temp;
    uint8_t        status;

    if (scsi_bus->tx_mode != PIO_TX_BUS) {
        if (ncr400->type == ROM_T130B) {
            ncr53c400_log("PERIOD T130B DMA=%lf.\n", scsi_bus->period / 200.0);
            timer_on_auto(&ncr400->timer, scsi_bus->period / 200.0);
        } else
            timer_on_auto(&ncr400->timer, 1.0);
    }

    if (scsi_bus->data_wait & 1) {
        scsi_bus->clear_req = 3;
        scsi_bus->data_wait &= ~1;
    }

    if (scsi_bus->tx_mode == PIO_TX_BUS) {
        ncr53c400_log("Timer CMD=%02x.\n", scsi_bus->command[0]);
        return;
    }

    switch (scsi_bus->tx_mode) {
        case DMA_OUT_TX_BUS:
            if (ncr400->status_ctrl & CTRL_DATA_DIR) {
                ncr53c400_log("DMA_SEND with DMA direction set wrong\n");
                break;
            }

            if (!(ncr400->status_ctrl & STATUS_BUFFER_NOT_READY)) {
                ncr53c400_log("Write buffer status ready\n");
                break;
            }

            if (!ncr400->block_count_loaded) {
                ncr53c400_log("Write block count not loaded.\n");
                break;
            }

            while (1) {
                for (c = 0; c < 10; c++) {
                    status = scsi_bus_read(scsi_bus);
                    if (status & BUS_REQ)
                        break;
                }
                /* Data ready. */
                temp = ncr400->buffer[ncr400->buffer_pos];

                bus = ncr5380_get_bus_host(ncr) & ~BUS_DATAMASK;
                bus |= BUS_SETDATA(temp);

                scsi_bus_update(scsi_bus, bus | BUS_ACK);
                scsi_bus_update(scsi_bus, bus & ~BUS_ACK);

                ncr400->buffer_pos++;
                ncr53c400_log("NCR 53c400 Buffer pos for writing = %d\n", ncr400->buffer_pos);

                if (ncr400->buffer_pos == MIN(128, dev->buffer_length)) {
                    ncr400->status_ctrl &= ~STATUS_BUFFER_NOT_READY;
                    ncr400->buffer_pos      = 0;
                    ncr400->buffer_host_pos = 0;
                    ncr400->busy    = 0;
                    ncr400->block_count = (ncr400->block_count - 1) & 0xff;
                    ncr53c400_log("NCR 53c400 Remaining blocks to be written=%d\n", ncr400->block_count);
                    if (!ncr400->block_count) {
                        scsi_bus->tx_mode = PIO_TX_BUS;
                        ncr400->block_count_loaded = 0;
                        ncr53c400_log("IO End of write transfer\n");
                        ncr->tcr |= TCR_LAST_BYTE_SENT;
                        ncr->isr |= STATUS_END_OF_DMA;
                        if (ncr->mode & MODE_ENA_EOP_INT) {
                            ncr53c400_log("NCR 53c400 write irq\n");
                            ncr5380_irq(ncr, 1);
                        }
                    }
                    break;
                }
            }
            break;

        case DMA_IN_TX_BUS:
            if (!(ncr400->status_ctrl & CTRL_DATA_DIR)) {
                ncr53c400_log("DMA_INITIATOR_RECEIVE with DMA direction set wrong\n");
                break;
            }

            if (!(ncr400->status_ctrl & STATUS_BUFFER_NOT_READY)) {
                ncr53c400_log("Read buffer status ready\n");
                break;
            }

            if (!ncr400->block_count_loaded)
                break;

            while (1) {
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

                ncr400->buffer[ncr400->buffer_pos++] = temp;
                ncr53c400_log("NCR 53c400 Buffer pos for reading = %d\n", ncr400->buffer_pos);

                if (ncr400->buffer_pos == MIN(128, dev->buffer_length)) {
                    ncr400->status_ctrl &= ~STATUS_BUFFER_NOT_READY;
                    ncr400->buffer_pos      = 0;
                    ncr400->buffer_host_pos = 0;
                    ncr400->block_count = (ncr400->block_count - 1) & 0xff;
                    ncr53c400_log("NCR 53c400 Remaining blocks to be read=%d\n", ncr400->block_count);
                    if (!ncr400->block_count) {
                        scsi_bus->tx_mode = PIO_TX_BUS;
                        ncr400->block_count_loaded = 0;
                        ncr53c400_log("IO End of read transfer\n");
                        ncr->isr |= STATUS_END_OF_DMA;
                        if (ncr->mode & MODE_ENA_EOP_INT) {
                            ncr53c400_log("NCR read irq\n");
                            ncr5380_irq(ncr, 1);
                        }
                    }
                    break;
                }
            }
            break;

        default:
            break;
    }

    status = scsi_bus_read(scsi_bus);

    if (!(status & BUS_BSY) && (ncr->mode & MODE_MONITOR_BUSY)) {
        ncr53c400_log("Updating DMA\n");
        ncr->mode &= ~MODE_DMA;
        scsi_bus->tx_mode = PIO_TX_BUS;
        ncr400->block_count_loaded = 0;
    }
}

static uint8_t
rt1000b_mc_read(int port, void *priv)
{
    const ncr53c400_t *ncr400 = (ncr53c400_t *) priv;

    return (ncr400->pos_regs[port & 7]);
}

static void
rt1000b_mc_write(int port, uint8_t val, void *priv)
{
    ncr53c400_t *ncr400 = (ncr53c400_t *) priv;

    /* MCA does not write registers below 0x0100. */
    if (port < 0x0102)
        return;

    mem_mapping_disable(&ncr400->bios_rom.mapping);
    mem_mapping_disable(&ncr400->mapping);

    /* Save the MCA register value. */
    ncr400->pos_regs[port & 7] = val;

    if (ncr400->pos_regs[2] & 1) {
        switch (ncr400->pos_regs[2] & 0xe0) {
            case 0:
                ncr400->rom_addr = 0xd4000;
                break;
            case 0x20:
                ncr400->rom_addr = 0xd0000;
                break;
            case 0x40:
                ncr400->rom_addr = 0xcc000;
                break;
            case 0x60:
                ncr400->rom_addr = 0xc8000;
                break;
            case 0xc0:
                ncr400->rom_addr = 0xdc000;
                break;
            case 0xe0:
                ncr400->rom_addr = 0xd8000;
                break;

            default:
                break;
        }

        mem_mapping_set_addr(&ncr400->bios_rom.mapping, ncr400->rom_addr, 0x4000);
        mem_mapping_set_addr(&ncr400->mapping, ncr400->rom_addr, 0x4000);
    }
}

static uint8_t
rt1000b_mc_feedb(void *priv)
{
    const ncr53c400_t *ncr400 = (ncr53c400_t *) priv;

    return ncr400->pos_regs[2] & 1;
}

static void *
ncr53c400_init(const device_t *info)
{
    const char  *bios_ver = NULL;
    const char  *fn;
    ncr53c400_t *ncr400 = calloc(1, sizeof(ncr53c400_t));
    ncr_t       *ncr    = &ncr400->ncr;
    scsi_bus_t  *scsi_bus;

    ncr400->type = info->local;

    ncr->bus = scsi_get_bus();
    scsi_bus = &ncr->scsibus;

    switch (ncr400->type) {
        case ROM_LCS6821N: /* Longshine LCS6821N */
            ncr400->rom_addr = device_get_config_hex20("bios_addr");
            ncr->irq         = device_get_config_int("irq");

            rom_init(&ncr400->bios_rom, LCS6821N_ROM,
                     ncr400->rom_addr, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);

            mem_mapping_add(&ncr400->mapping, ncr400->rom_addr, 0x4000,
                            ncr53c400_read, NULL, NULL,
                            ncr53c400_write, NULL, NULL,
                            ncr400->bios_rom.rom, MEM_MAPPING_EXTERNAL, ncr400);
            break;


        case ROM_LS2000: /* Corel LS2000 */
            ncr400->rom_addr = device_get_config_hex20("bios_addr");
            ncr->irq         = device_get_config_int("irq");

            rom_init(&ncr400->bios_rom, COREL_LS2000_ROM,
                     ncr400->rom_addr, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);

            mem_mapping_add(&ncr400->mapping, ncr400->rom_addr, 0x4000,
                            ncr53c400_read, NULL, NULL,
                            ncr53c400_write, NULL, NULL,
                            ncr400->bios_rom.rom, MEM_MAPPING_EXTERNAL, ncr400);
            break;

        case ROM_RT1000B: /* Rancho RT1000B/MC */
            ncr400->rom_addr = device_get_config_hex20("bios_addr");
            ncr->irq         = device_get_config_int("irq");
            if (info->flags & DEVICE_MCA) {
                rom_init(&ncr400->bios_rom, RT1000B_820R_ROM,
                         0xd8000, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);
                mem_mapping_add(&ncr400->mapping, 0xd8000, 0x4000,
                                ncr53c400_read, NULL, NULL,
                                ncr53c400_write, NULL, NULL,
                                ncr400->bios_rom.rom, MEM_MAPPING_EXTERNAL, ncr400);
                mem_mapping_disable(&ncr400->bios_rom.mapping);
                ncr400->pos_regs[0] = 0x8d;
                ncr400->pos_regs[1] = 0x70;
                mca_add(rt1000b_mc_read, rt1000b_mc_write, rt1000b_mc_feedb, NULL, ncr400);
            } else {
                bios_ver     = (char *) device_get_config_bios("bios_ver");
                fn           = (char *) device_get_bios_file(info, bios_ver, 0);
                rom_init(&ncr400->bios_rom, fn,
                         ncr400->rom_addr, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);
                mem_mapping_add(&ncr400->mapping, ncr400->rom_addr, 0x4000,
                                ncr53c400_read, NULL, NULL,
                                ncr53c400_write, NULL, NULL,
                                ncr400->bios_rom.rom, MEM_MAPPING_EXTERNAL, ncr400);
            }
            break;

        case ROM_T130B: /* Trantor T130B */
            ncr400->rom_addr = device_get_config_hex20("bios_addr");
            ncr400->base     = device_get_config_hex16("base");
            ncr->irq     = device_get_config_int("irq");

            if (ncr400->rom_addr > 0x00000) {
                rom_init(&ncr400->bios_rom, T130B_ROM,
                         ncr400->rom_addr, 0x4000, 0x3fff, 0, MEM_MAPPING_EXTERNAL);

                mem_mapping_add(&ncr400->mapping, ncr400->rom_addr, 0x4000,
                                t130b_read, NULL, NULL,
                                t130b_write, NULL, NULL,
                                ncr400->bios_rom.rom, MEM_MAPPING_EXTERNAL, ncr400);
            }

            io_sethandler(ncr400->base, 16,
                          t130b_in, NULL, NULL, t130b_out, NULL, NULL, ncr400);
            break;

        default:
            break;
    }

    ncr->priv                       = ncr400;
    ncr->dma_mode_ext               = ncr53c400_dma_mode_ext;
    ncr->dma_send_ext               = NULL;
    ncr->dma_initiator_receive_ext  = NULL;
    ncr->timer                      = ncr53c400_timer_on_auto;
    scsi_bus->bus_device            = ncr->bus;
    scsi_bus->timer                 = ncr->timer;
    scsi_bus->priv                  = ncr->priv;
    ncr400->status_ctrl             = STATUS_BUFFER_NOT_READY;
    ncr400->buffer_host_pos         = 128;
    timer_add(&ncr400->timer, ncr53c400_callback, ncr400, 0);

    scsi_bus_set_speed(ncr->bus, 5000000.0);
    scsi_bus->speed = 0.2;
    scsi_bus->divider = 2.0;
    scsi_bus->multi = 1.750;
    return ncr400;
}

static void
ncr53c400_close(void *priv)
{
    ncr53c400_t *ncr400 = (ncr53c400_t *) priv;

    if (ncr400) {
        /* Tell the timer to terminate. */
        timer_stop(&ncr400->timer);

        free(ncr400);
        ncr400 = NULL;
    }
}

static int
lcs6821n_available(void)
{
    return (rom_present(LCS6821N_ROM));
}

static int
rt1000b_mc_available(void)
{
    return (rom_present(RT1000B_820R_ROM));
}

static int
t130b_available(void)
{
    return (rom_present(T130B_ROM));
}

static int
corel_ls2000_available(void)
{
    return (rom_present(COREL_LS2000_ROM));
}

// clang-format off
static const device_config_t ncr53c400_mmio_config[] = {
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
            { .description = "D000H", .value = 0xd0000 },
            { .description = "D400H", .value = 0xd4000 },
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
            { .description = "IRQ 3", .value =  3 },
            { .description = "IRQ 5", .value =  5 },
            { .description = "IRQ 7", .value =  7 },
            { .description = ""                   }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t rt1000b_config[] = {
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
            { .description = "D000H", .value = 0xd0000 },
            { .description = "D400H", .value = 0xd4000 },
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
            { .description = "IRQ 3", .value =  3 },
            { .description = "IRQ 5", .value =  5 },
            { .description = "IRQ 7", .value =  7 },
            { .description = ""                   }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "bios_ver",
        .description    = "BIOS Revision",
        .type           = CONFIG_BIOS,
        .default_string = "v8_10r",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios = {
            {
                .name          = "Version 8.10R",
                .internal_name = "v8_10r",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 8192,
                .files         = { RT1000B_810R_ROM, "" }
            },
            {
                .name          = "Version 8.20R",
                .internal_name = "v8_20r",
                .bios_type     = BIOS_NORMAL,
                .files_no      = 1,
                .local         = 0,
                .size          = 8192,
                .files         = { RT1000B_820R_ROM, "" }
            },
            { .files_no = 0 }
        },
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t rt1000b_mc_config[] = {
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
            { .description = "IRQ 3", .value =  3 },
            { .description = "IRQ 5", .value =  5 },
            { .description = "IRQ 7", .value =  7 },
            { .description = ""                   }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t t130b_config[] = {
    {
        .name           = "bios_addr",
        .description    = "BIOS Address",
        .type           = CONFIG_HEX20,
        .default_string = NULL,
        .default_int    = 0xD8000,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "Disabled", .value =       0 },
            { .description = "C800H",    .value = 0xc8000 },
            { .description = "CC00H",    .value = 0xcc000 },
            { .description = "D800H",    .value = 0xd8000 },
            { .description = "DC00H",    .value = 0xdc000 },
            { .description = ""                           }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x0350,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "240H", .value = 0x0240 },
            { .description = "250H", .value = 0x0250 },
            { .description = "340H", .value = 0x0340 },
            { .description = "350H", .value = 0x0350 },
            { .description = ""                      }
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
            { .description = "IRQ 3", .value =  3 },
            { .description = "IRQ 5", .value =  5 },
            { .description = "IRQ 7", .value =  7 },
            { .description = ""                   }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t scsi_lcs6821n_device = {
    .name          = "Longshine LCS-6821N",
    .internal_name = "lcs6821n",
    .flags         = DEVICE_ISA,
    .local         = ROM_LCS6821N,
    .init          = ncr53c400_init,
    .close         = ncr53c400_close,
    .reset         = NULL,
    .available     = lcs6821n_available,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ncr53c400_mmio_config
};

const device_t scsi_rt1000b_device = {
    .name          = "Rancho RT1000B",
    .internal_name = "rt1000b",
    .flags         = DEVICE_ISA,
    .local         = ROM_RT1000B,
    .init          = ncr53c400_init,
    .close         = ncr53c400_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = rt1000b_config
};

const device_t scsi_rt1000mc_device = {
    .name          = "Rancho RT1000B-MC",
    .internal_name = "rt1000mc",
    .flags         = DEVICE_MCA,
    .local         = ROM_RT1000B,
    .init          = ncr53c400_init,
    .close         = ncr53c400_close,
    .reset         = NULL,
    .available     = rt1000b_mc_available,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = rt1000b_mc_config
};

const device_t scsi_t130b_device = {
    .name          = "Trantor T130B",
    .internal_name = "t130b",
    .flags         = DEVICE_ISA,
    .local         = ROM_T130B,
    .init          = ncr53c400_init,
    .close         = ncr53c400_close,
    .reset         = NULL,
    .available     = t130b_available,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = t130b_config
};

const device_t scsi_ls2000_device = {
    .name          = "Corel LS2000",
    .internal_name = "ls2000",
    .flags         = DEVICE_ISA,
    .local         = ROM_LS2000,
    .init          = ncr53c400_init,
    .close         = ncr53c400_close,
    .reset         = NULL,
    .available     = corel_ls2000_available,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = ncr53c400_mmio_config
};
