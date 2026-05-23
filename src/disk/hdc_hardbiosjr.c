/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          HARDBIOSjr controller emulation.
 *
 *          This is a first-pass transport implementation for the
 *          MSC/RIM PCjr fixed-disk BIOS found in temporary/hardbios.bin.
 *          The ROM speaks to a small sidecar controller through 0x3f8-0x3fb
 *          rather than an XT ST506 register block.
 */

//#define ENABLE_HARDBIOSJR_LOG 1
#ifdef ENABLE_HARDBIOSJR_LOG
#include <stdarg.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/hdc.h>
#include <86box/hdd.h>
#include <86box/io.h>
#ifdef ENABLE_HARDBIOSJR_LOG
#include <86box/log.h>
#endif
#include <86box/mem.h>
#include <86box/plat_unused.h>
#include <86box/rom.h>

#define HARDBIOSJR_ROM_PATH        "temporary/hardbios.bin"
#define HARDBIOSJR_ROM_ADDR        0xc8000
#define HARDBIOSJR_ROM_SIZE        0x2000
#define HARDBIOSJR_ROM_MASK        0x1fff

#define HARDBIOSJR_IO_BASE         0x03f8
#define HARDBIOSJR_IO_LEN          0x0004

#define HARDBIOSJR_MAX_DRIVES      2
#define HARDBIOSJR_COMMAND_LEN     6
#define HARDBIOSJR_PARAM_LEN       4
#define HARDBIOSJR_SECTOR_SIZE     512
#define HARDBIOSJR_MAX_SECTORS     0x7f
#define HARDBIOSJR_MAX_TRANSFER    (HARDBIOSJR_MAX_SECTORS * HARDBIOSJR_SECTOR_SIZE)

#define HARDBIOSJR_CMD_STATUS      0x03
#define HARDBIOSJR_CMD_READ        0x08
#define HARDBIOSJR_CMD_WRITE       0x0a
#define HARDBIOSJR_CMD_FORMAT      0x15
#define HARDBIOSJR_CMD_GET_PARAMS  0x25
#define HARDBIOSJR_CMD_FORMAT_EXT  0x2e

#define HARDBIOSJR_ERR_SUCCESS     0x00
#define HARDBIOSJR_ERR_FAILURE     0x02

#ifdef ENABLE_HARDBIOSJR_LOG
uint8_t hardbiosjr_do_log = ENABLE_HARDBIOSJR_LOG;

static void
hardbiosjr_log(void *priv, const char *fmt, ...)
{
    if (hardbiosjr_do_log) {
        va_list ap;

        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define hardbiosjr_log(fmt, ...)
#endif

typedef enum hardbiosjr_phase_t {
    HARDBIOSJR_PHASE_IDLE = 0,
    HARDBIOSJR_PHASE_START_WAIT,
    HARDBIOSJR_PHASE_COMMAND,
    HARDBIOSJR_PHASE_HOST_PARAM,
    HARDBIOSJR_PHASE_HOST_DATA,
    HARDBIOSJR_PHASE_DEVICE_DATA,
    HARDBIOSJR_PHASE_FINALIZE
} hardbiosjr_phase_t;

typedef struct hardbiosjr_drive_t {
    uint8_t present;
    uint8_t hdd_num;
} hardbiosjr_drive_t;

typedef struct hardbiosjr_t {
    rom_t               bios_rom;
    hardbiosjr_drive_t  drives[HARDBIOSJR_MAX_DRIVES];

    hardbiosjr_phase_t  phase;

    uint8_t             mode;
    uint8_t             ctrl;
    uint8_t             data;
    uint8_t             command[HARDBIOSJR_COMMAND_LEN];
    uint8_t             command_index;
    uint8_t             ready;
    uint8_t             device_byte_latched;

    uint32_t            transfer_index;
    uint32_t            transfer_len;
    uint32_t            transfer_lba;

    uint8_t             transfer_buffer[HARDBIOSJR_MAX_TRANSFER];

    void               *log;
} hardbiosjr_t;

static void
hardbiosjr_prepare_finalize(hardbiosjr_t *dev, uint8_t status_byte)
{
    hardbiosjr_log(dev->log, "Finalize phase=%u status=%02x\n", dev->phase, status_byte);

    dev->phase               = HARDBIOSJR_PHASE_FINALIZE;
    dev->data                = status_byte;
    dev->ready               = 1;
    dev->device_byte_latched = 0;
    dev->transfer_index      = 0;
    dev->transfer_len        = 0;
}

static int
hardbiosjr_get_drive_index(const hardbiosjr_t *dev)
{
    int drive = (dev->command[1] >> 5) & 0x01;

    if ((drive < 0) || (drive >= HARDBIOSJR_MAX_DRIVES) || !dev->drives[drive].present)
        return -1;

    return drive;
}

static uint32_t
hardbiosjr_get_lba(const hardbiosjr_t *dev)
{
    return ((uint32_t) dev->command[2] << 8) | (uint32_t) dev->command[3];
}

static uint32_t
hardbiosjr_get_count(const hardbiosjr_t *dev)
{
    return (uint32_t) dev->command[4];
}

static void
hardbiosjr_start_device_data(hardbiosjr_t *dev, uint32_t len)
{
    hardbiosjr_log(dev->log, "Start device data len=%lu\n", len);

    dev->phase               = HARDBIOSJR_PHASE_DEVICE_DATA;
    dev->transfer_index      = 0;
    dev->transfer_len        = len;
    dev->ready               = 1;
    dev->device_byte_latched = 0;

    if (len == 0)
        hardbiosjr_prepare_finalize(dev, HARDBIOSJR_ERR_SUCCESS);
}

static void
hardbiosjr_start_host_param(hardbiosjr_t *dev)
{
    hardbiosjr_log(dev->log, "Start host param len=%u cmd=%02x\n", HARDBIOSJR_PARAM_LEN, dev->command[0]);

    dev->phase               = HARDBIOSJR_PHASE_HOST_PARAM;
    dev->transfer_index      = 0;
    dev->transfer_len        = HARDBIOSJR_PARAM_LEN;
    dev->ready               = 1;
    dev->device_byte_latched = 0;
}

static void
hardbiosjr_start_host_data(hardbiosjr_t *dev, uint32_t lba, uint32_t count)
{
    if ((count == 0) || (count > HARDBIOSJR_MAX_SECTORS)) {
        hardbiosjr_log(dev->log, "Reject host data lba=%lu count=%lu\n", lba, count);
        hardbiosjr_prepare_finalize(dev, HARDBIOSJR_ERR_FAILURE);
        return;
    }

    hardbiosjr_log(dev->log, "Start host data lba=%lu count=%lu len=%lu\n",
                   lba, count, count * HARDBIOSJR_SECTOR_SIZE);

    dev->phase               = HARDBIOSJR_PHASE_HOST_DATA;
    dev->transfer_index      = 0;
    dev->transfer_len        = count * HARDBIOSJR_SECTOR_SIZE;
    dev->transfer_lba        = lba;
    dev->ready               = 1;
    dev->device_byte_latched = 0;
}

static void
hardbiosjr_finish_host_param(hardbiosjr_t *dev)
{
    const uint32_t command = dev->command[0];

    hardbiosjr_log(dev->log, "Finish host param cmd=%02lx\n", command);

    switch (command) {
        case HARDBIOSJR_CMD_GET_PARAMS: {
            const int drive = hardbiosjr_get_drive_index(dev);
            uint32_t  last_sector = 0;

            if (drive < 0) {
                hardbiosjr_log(dev->log, "Get params missing drive\n");
                hardbiosjr_prepare_finalize(dev, HARDBIOSJR_ERR_FAILURE);
                return;
            }

            last_sector = hdd_image_get_last_sector(dev->drives[drive].hdd_num);
            hardbiosjr_log(dev->log, "Get params drive=%d hdd=%u last=%lu\n",
                           drive, dev->drives[drive].hdd_num, last_sector);
            dev->transfer_buffer[0] = (uint8_t) (last_sector >> 24);
            dev->transfer_buffer[1] = (uint8_t) (last_sector >> 16);
            dev->transfer_buffer[2] = (uint8_t) (last_sector >> 8);
            dev->transfer_buffer[3] = (uint8_t) (last_sector & 0xff);
            memset(&dev->transfer_buffer[4], 0x00, 4);
            hardbiosjr_start_device_data(dev, 8);
            return;
        }

        case HARDBIOSJR_CMD_FORMAT_EXT:
        default:
            hardbiosjr_prepare_finalize(dev, HARDBIOSJR_ERR_SUCCESS);
            return;
    }
}

static void
hardbiosjr_finish_host_data(hardbiosjr_t *dev)
{
    const int      drive = hardbiosjr_get_drive_index(dev);
    const uint32_t count = hardbiosjr_get_count(dev);
    const uint32_t last  = (count > 0) ? (dev->transfer_lba + count - 1) : dev->transfer_lba;

    hardbiosjr_log(dev->log, "Finish host data drive=%d lba=%lu count=%lu last=%lu\n",
                   drive, dev->transfer_lba, count, last);

    if ((drive < 0) || (count == 0)) {
        hardbiosjr_log(dev->log, "Finish host data invalid drive/count\n");
        hardbiosjr_prepare_finalize(dev, HARDBIOSJR_ERR_FAILURE);
        return;
    }

    if (last > hdd_image_get_last_sector(dev->drives[drive].hdd_num)) {
        hardbiosjr_log(dev->log, "Finish host data beyond media last=%lu media_last=%u\n",
                       last, hdd_image_get_last_sector(dev->drives[drive].hdd_num));
        hardbiosjr_prepare_finalize(dev, HARDBIOSJR_ERR_FAILURE);
        return;
    }

    if (hdd_image_write(dev->drives[drive].hdd_num, dev->transfer_lba, count, dev->transfer_buffer) < 0) {
        hardbiosjr_log(dev->log, "Write failed hdd=%u lba=%lu count=%lu\n",
                       dev->drives[drive].hdd_num, dev->transfer_lba, count);
        hardbiosjr_prepare_finalize(dev, HARDBIOSJR_ERR_FAILURE);
        return;
    }

    hardbiosjr_log(dev->log, "Write complete hdd=%u lba=%lu count=%lu\n",
                   dev->drives[drive].hdd_num, dev->transfer_lba, count);

    hardbiosjr_prepare_finalize(dev, HARDBIOSJR_ERR_SUCCESS);
}

static void
hardbiosjr_process_command(hardbiosjr_t *dev)
{
    const uint32_t command = dev->command[0];
    const int      drive   = hardbiosjr_get_drive_index(dev);
    const uint32_t lba     = hardbiosjr_get_lba(dev);
    const uint32_t count   = hardbiosjr_get_count(dev);
    const uint32_t last    = (count > 0) ? (lba + count - 1) : lba;

    hardbiosjr_log(dev->log,
                   "Process cmd=%02lx drive=%d lba=%lu count=%lu last=%lu raw=[%02x %02x %02x %02x %02x %02x]\n",
                   command, drive, lba, count, last,
                   dev->command[0], dev->command[1], dev->command[2],
                   dev->command[3], dev->command[4], dev->command[5]);

    switch (command) {
        case HARDBIOSJR_CMD_STATUS:
            memset(dev->transfer_buffer, 0x00, 4);
            hardbiosjr_start_device_data(dev, 4);
            return;

        case HARDBIOSJR_CMD_GET_PARAMS:
            hardbiosjr_start_host_param(dev);
            return;

        case HARDBIOSJR_CMD_READ:
            if ((drive < 0) || (count == 0) || (count > HARDBIOSJR_MAX_SECTORS) ||
                (last > hdd_image_get_last_sector(dev->drives[drive].hdd_num))) {
                hardbiosjr_log(dev->log, "Reject read drive=%d lba=%lu count=%lu\n", drive, lba, count);
                hardbiosjr_prepare_finalize(dev, HARDBIOSJR_ERR_FAILURE);
                return;
            }

            if (hdd_image_read(dev->drives[drive].hdd_num, lba, count, dev->transfer_buffer) < 0) {
                hardbiosjr_log(dev->log, "Read failed hdd=%u lba=%lu count=%lu\n",
                               dev->drives[drive].hdd_num, lba, count);
                hardbiosjr_prepare_finalize(dev, HARDBIOSJR_ERR_FAILURE);
                return;
            }

            hardbiosjr_log(dev->log, "Read complete hdd=%u lba=%lu count=%lu\n",
                           dev->drives[drive].hdd_num, lba, count);

            hardbiosjr_start_device_data(dev, count * HARDBIOSJR_SECTOR_SIZE);
            return;

        case HARDBIOSJR_CMD_WRITE:
            if ((drive < 0) || (count == 0) || (count > HARDBIOSJR_MAX_SECTORS) ||
                (last > hdd_image_get_last_sector(dev->drives[drive].hdd_num))) {
                hardbiosjr_log(dev->log, "Reject write drive=%d lba=%lu count=%lu\n", drive, lba, count);
                hardbiosjr_prepare_finalize(dev, HARDBIOSJR_ERR_FAILURE);
                return;
            }

            hardbiosjr_start_host_data(dev, lba, count);
            return;

        case HARDBIOSJR_CMD_FORMAT_EXT:
            hardbiosjr_start_host_param(dev);
            return;

        case HARDBIOSJR_CMD_FORMAT:
            hardbiosjr_prepare_finalize(dev, HARDBIOSJR_ERR_SUCCESS);
            return;

        default:
            hardbiosjr_prepare_finalize(dev, drive >= 0 ? HARDBIOSJR_ERR_SUCCESS : HARDBIOSJR_ERR_FAILURE);
            return;
    }
}

static uint8_t
hardbiosjr_status(const hardbiosjr_t *dev)
{
    uint8_t status = 0x00;

    switch (dev->phase) {
        case HARDBIOSJR_PHASE_START_WAIT:
            return 0x08;

        case HARDBIOSJR_PHASE_FINALIZE:
            status = 0x11;
            break;

        default:
            status = 0x00;
            break;
    }

    if (dev->phase == HARDBIOSJR_PHASE_IDLE)
        return 0x04;

    if (dev->ready)
        status |= 0x04;

    if ((dev->phase == HARDBIOSJR_PHASE_FINALIZE) ||
        ((dev->phase == HARDBIOSJR_PHASE_HOST_PARAM) && (dev->transfer_index >= dev->transfer_len)) ||
        ((dev->phase == HARDBIOSJR_PHASE_HOST_DATA) && (dev->transfer_index >= dev->transfer_len)) ||
        ((dev->phase == HARDBIOSJR_PHASE_DEVICE_DATA) && (dev->transfer_index >= dev->transfer_len)))
        status |= 0x01;

    return status;
}

static void
hardbiosjr_write_ctrl(hardbiosjr_t *dev, uint8_t val)
{
    const uint8_t old = dev->ctrl;

    hardbiosjr_log(dev->log, "[%04X:%08X] [W] CTRL %02X -> %02X phase=%u ready=%u idx=%lu/%lu\n",
                   CS, cpu_state.pc, old, val, dev->phase, dev->ready,
                   dev->transfer_index, dev->transfer_len);

    dev->ctrl = val;

    if ((dev->phase == HARDBIOSJR_PHASE_START_WAIT) && (val == 0x0c)) {
        dev->phase          = HARDBIOSJR_PHASE_COMMAND;
        dev->command_index  = 0;
        dev->ready          = 1;
        return;
    }

    if (val & 0x40) {
        dev->ready = 0;
        return;
    }

    if ((old & 0x40) && !(val & 0x40)) {
        dev->ready = 1;

        switch (dev->phase) {
            case HARDBIOSJR_PHASE_COMMAND:
                if (dev->command_index >= HARDBIOSJR_COMMAND_LEN)
                    hardbiosjr_process_command(dev);
                break;

            case HARDBIOSJR_PHASE_HOST_PARAM:
                if (dev->transfer_index >= dev->transfer_len)
                    hardbiosjr_finish_host_param(dev);
                break;

            case HARDBIOSJR_PHASE_HOST_DATA:
                if (dev->transfer_index >= dev->transfer_len)
                    hardbiosjr_finish_host_data(dev);
                break;

            case HARDBIOSJR_PHASE_DEVICE_DATA:
                if (dev->device_byte_latched) {
                    dev->device_byte_latched = 0;
                    dev->transfer_index++;
                    if (dev->transfer_index >= dev->transfer_len)
                        hardbiosjr_prepare_finalize(dev, HARDBIOSJR_ERR_SUCCESS);
                }
                break;

            default:
                break;
        }
    }
}

static void
hardbiosjr_write(uint16_t port, uint8_t val, void *priv)
{
    hardbiosjr_t *dev = (hardbiosjr_t *) priv;

    hardbiosjr_log(dev->log, "[%04X:%08X] [W] %04X = %02X phase=%u cmd_idx=%u xfer=%lu/%lu\n",
                   CS, cpu_state.pc, port, val, dev->phase, dev->command_index,
                   dev->transfer_index, dev->transfer_len);

    switch (port - HARDBIOSJR_IO_BASE) {
        case 0:
            dev->data = val;

            switch (dev->phase) {
                case HARDBIOSJR_PHASE_COMMAND:
                    if (dev->command_index < HARDBIOSJR_COMMAND_LEN)
                        dev->command[dev->command_index++] = val;
                    break;

                case HARDBIOSJR_PHASE_HOST_PARAM:
                case HARDBIOSJR_PHASE_HOST_DATA:
                    if (dev->transfer_index < dev->transfer_len)
                        dev->transfer_buffer[dev->transfer_index++] = val;
                    break;

                default:
                    break;
            }
            break;

        case 1:
            hardbiosjr_write_ctrl(dev, val);
            break;

        case 2:
            break;

        case 3:
            dev->mode = val;
            break;

        default:
            break;
    }
}

static uint8_t
hardbiosjr_read(uint16_t port, void *priv)
{
    hardbiosjr_t *dev = (hardbiosjr_t *) priv;
    uint8_t       ret = 0xff;

    switch (port - HARDBIOSJR_IO_BASE) {
        case 0:
            if ((dev->phase == HARDBIOSJR_PHASE_DEVICE_DATA) && (dev->transfer_index < dev->transfer_len)) {
                dev->data                = dev->transfer_buffer[dev->transfer_index];
                dev->device_byte_latched = 1;
            }
            ret = dev->data;
            break;

        case 1:
            ret = dev->ctrl;
            break;

        case 2:
            ret = hardbiosjr_status(dev);
            break;

        case 3:
            ret = dev->mode;
            break;

        default:
            ret = 0xff;
            break;
    }

    hardbiosjr_log(dev->log, "[%04X:%08X] [R] %04X = %02X phase=%u cmd_idx=%u xfer=%lu/%lu\n",
                   CS, cpu_state.pc, port, ret, dev->phase, dev->command_index,
                   dev->transfer_index, dev->transfer_len);

    return ret;
}

static void *
hardbiosjr_init(UNUSED(const device_t *info))
{
    hardbiosjr_t *dev = calloc(1, sizeof(hardbiosjr_t));

#ifdef ENABLE_HARDBIOSJR_LOG
    dev->log = log_open("HARDBIOSjr");
#endif

    rom_init(&dev->bios_rom, HARDBIOSJR_ROM_PATH,
             HARDBIOSJR_ROM_ADDR, HARDBIOSJR_ROM_SIZE, HARDBIOSJR_ROM_MASK, 0, MEM_MAPPING_EXTERNAL);

    dev->phase = HARDBIOSJR_PHASE_IDLE;
    dev->ready = 1;

    hardbiosjr_log(dev->log, "Init ROM=%s addr=%06X io=%04X len=%u\n",
                   HARDBIOSJR_ROM_PATH, HARDBIOSJR_ROM_ADDR, HARDBIOSJR_IO_BASE, HARDBIOSJR_IO_LEN);

    for (uint8_t i = 0; i < HDD_NUM; i++) {
        if ((hdd[i].bus_type == HDD_BUS_MFM) && (hdd[i].mfm_channel < HARDBIOSJR_MAX_DRIVES)) {
            if (!hdd_image_load(i))
                continue;

            dev->drives[hdd[i].mfm_channel].present = 1;
            dev->drives[hdd[i].mfm_channel].hdd_num = i;
            hardbiosjr_log(dev->log, "Attach drive slot=%u hdd=%u chs=%lu/%lu/%lu\n",
                           hdd[i].mfm_channel, i, hdd[i].tracks, hdd[i].hpc, hdd[i].spt);
        }
    }

    io_sethandler(HARDBIOSJR_IO_BASE, HARDBIOSJR_IO_LEN,
                  hardbiosjr_read, NULL, NULL,
                  hardbiosjr_write, NULL, NULL,
                  dev);

    return dev;
}

static void
hardbiosjr_close(void *priv)
{
    hardbiosjr_t *dev = (hardbiosjr_t *) priv;

    hardbiosjr_log(dev->log, "Close\n");

#ifdef ENABLE_HARDBIOSJR_LOG
    if (dev->log)
        log_close(dev->log);
#endif

    free(dev);
}

static int
hardbiosjr_available(void)
{
    return rom_present(HARDBIOSJR_ROM_PATH);
}

const device_t hardbiosjr_device = {
    .name          = "HARDBIOSjr Fixed Disk",
    .internal_name = "hardbiosjr",
    .flags         = DEVICE_SIDECAR,
    .local         = 0,
    .init          = hardbiosjr_init,
    .close         = hardbiosjr_close,
    .reset         = NULL,
    .available     = hardbiosjr_available,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};