/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of National Semiconductor PC87366(NSC366) Hardware Monitor
 *
 *
 *
 * Authors: Tiseno100,
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2022      Tiseno100.
 *          Copyright 2022-2023 Jasmine Iwanek.
 */

/* The conversion algorithms were taken by the pc87360.c driver of the Linux kernel.
   Respective credits goes to the authors.                                          */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/plat.h> // Replace with plat_unused.h when upstreamed

#include <86box/hwm.h>
#include <86box/nsc366.h>

/* Fan Algorithms */
#define FAN_TO_REG(val, div)   ((val) <= 100 ? 0 : 480000 / ((val) * (div)))
#define FAN_DIV_FROM_REG(val)  (1 << (((val) >> 5) & 0x03))
#define FAN_FROM_REG(val, div) ((val) == 0 ? 0 : 480000 / ((val) * (div)))

/* Voltage Algorithms */
#define IN_TO_REG(val, ref) ((val) < 0 ? 0 : (val) *256 >= (ref) *255 ? 255 \
                                                                      : ((val) *256 + (ref) / 2) / (ref))
#define IN_FROM_REG(val, ref) (((val) * (ref) + 128) / 256)
#define VREF                  (dev->vlm_config_global[0x08] & 2) ? 3025 : 2966 // VREF taken from pc87360.c
#define VLM_BANK              dev->vlm_config_global[0x09]

/* Temperature Algorithms */
#define TEMP_TO_REG(val) ((val) < -55000 ? -55 : (val) > 127000 ? 127                 \
                              : (val) < 0                       ? ((val) -500) / 1000 \
                                                                : ((val) + 500) / 1000)
#define TEMP_FROM_REG(val) ((val) *1000)
#define TMS_BANK           dev->tms_config_global[0x09]

#ifdef ENABLE_NSC366_HWM_LOG
int nsc366_hwm_do_log = ENABLE_NSC366_HWM_LOG;
void
nsc366_hwm_log(const char *fmt, ...)
{
    va_list ap;

    if (nsc366_hwm_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define nsc366_hwm_log(fmt, ...)
#endif

/* Fans */
static void
nsc366_fscm_write(uint16_t addr, uint8_t val, void *priv)
{
    nsc366_hwm_t *dev = (nsc366_hwm_t *) priv;

    addr &= 0x000f;

    nsc366_hwm_log("NSC366 Fan Control: Write 0x%02x to register 0x%02x\n", val, addr);

    switch (addr) {
        case 0x00:
        case 0x02:
        case 0x04:
            dev->fscm_config[addr] = val;
            break;

        case 0x01:
        case 0x03:
        case 0x05:
            dev->fscm_config[addr] = val;
            break;

        case 0x06:
        case 0x09:
        case 0x0c:
            dev->fscm_config[addr] = val;
            break;

        case 0x08:
        case 0x0b:
        case 0x0d:
            dev->fscm_config[addr] = (val & 0x78) | 1;
            break;

        default:
            break;
    }
}

static uint8_t
nsc366_fscm_read(uint16_t addr, void *priv)
{
    nsc366_hwm_t *dev = (nsc366_hwm_t *) priv;

    addr &= 0x000f;

    switch (addr) {
        case 0x00 ... 0x06:
        case 0x08 ... 0x09:
        case 0x0b ... 0x0c:
            return dev->fscm_config[addr];

        case 0x07:
        case 0x0a:
        case 0x0d:
            if (((addr == 0x07) && !!(dev->fscm_enable & 1)) || ((addr == 0x0a) && !!(dev->fscm_enable & 2)) || ((addr == 0x0d) && !!(dev->fscm_enable & 4))) {
                nsc366_hwm_log("NSC366 Fan Control: Reading %d RPM's from Bank %d\n", FAN_FROM_REG(dev->fscm_config[addr], FAN_DIV_FROM_REG(dev->fscm_config[0x06])), (addr - 7) / 3);
                return dev->fscm_config[addr];
            } else
                return 0;

        default:
            return 0;
    }
}

void
nsc366_update_fscm_io(int enable, uint16_t addr, nsc366_hwm_t *dev)
{
    if (dev->fscm_addr != 0)
        io_removehandler(dev->fscm_addr, 15, nsc366_fscm_read, NULL, NULL, nsc366_fscm_write, NULL, NULL, dev);

    dev->fscm_addr = addr;

    if ((addr != 0) && enable)
        io_sethandler(addr, 15, nsc366_fscm_read, NULL, NULL, nsc366_fscm_write, NULL, NULL, dev);
}

/* Voltage */
static void
nsc366_vlm_write(uint16_t addr, uint8_t val, void *priv)
{
    nsc366_hwm_t *dev = (nsc366_hwm_t *) priv;

    addr &= 0x000f;

    if (addr <= 9)
        nsc366_hwm_log("NSC366 Voltage Monitor: Write 0x%02x to register 0x%02x\n", val, addr);
    else
        nsc366_hwm_log("NSC366 Voltage Monitor: Write 0x%02x to register 0x%02x of bank %d\n", val, addr, VLM_BANK);

    switch (addr) {
        case 0x02 ... 0x04:
            dev->vlm_config_global[addr] = val;
            break;

        case 0x05:
            dev->vlm_config_global[addr] = val & 0x3f;
            break;

        case 0x06:
            dev->vlm_config_global[addr] = val & 0xc0;
            break;

        case 0x07:
            dev->vlm_config_global[addr] = val & 0x3f;
            break;

        case 0x08:
            dev->vlm_config_global[addr] = val & 3;
            break;

        case 0x09:
            dev->vlm_config_global[addr] = val & 0x1f;
            break;

        case 0x0a:
            if (VLM_BANK < 13)
                dev->vlm_config_bank[VLM_BANK][addr - 0x0a] = val & 1;
            break;

        case 0x0c ... 0x0e:
            if (VLM_BANK < 13)
                dev->vlm_config_bank[VLM_BANK][addr - 0x0a] = val;
            break;

        default:
            break;
    }
}

static uint8_t
nsc366_vlm_read(uint16_t addr, void *priv)
{
    nsc366_hwm_t *dev = (nsc366_hwm_t *) priv;

    addr &= 0x000f;

    switch (addr) {
        case 0x00 ... 0x09:
            return dev->vlm_config_global[addr];

        case 0x0a:
        case 0x0c ... 0x0e:
            if (VLM_BANK < 13)
                return dev->vlm_config_bank[VLM_BANK][addr - 0x0a];
            else
                return 0;

        case 0x0b:
            if (VLM_BANK < 13) {
                if (dev->vlm_config_bank[VLM_BANK][0] & 1) {
                    nsc366_hwm_log("NSC366 Voltage Monitor: Reading %d Volts from Bank %d\n", IN_FROM_REG(dev->vlm_config_bank[VLM_BANK][1], VREF), VLM_BANK);
                    return dev->vlm_config_bank[VLM_BANK][1];
                } else
                    return 0;
            } else
                return 0;

        default:
            return 0;
    }
}

void
nsc366_update_vlm_io(int enable, uint16_t addr, nsc366_hwm_t *dev)
{
    if (dev->vlm_addr != 0)
        io_removehandler(dev->vlm_addr, 15, nsc366_vlm_read, NULL, NULL, nsc366_vlm_write, NULL, NULL, dev);

    dev->vlm_addr = addr;

    if ((addr != 0) && enable)
        io_sethandler(addr, 15, nsc366_vlm_read, NULL, NULL, nsc366_vlm_write, NULL, NULL, dev);
}

/* Temperature */
static void
nsc366_tms_write(uint16_t addr, uint8_t val, void *priv)
{
    nsc366_hwm_t *dev = (nsc366_hwm_t *) priv;

    addr &= 0x000f;

    if (addr <= 9)
        nsc366_hwm_log("NSC366 Temperature Monitor: Write 0x%02x to register 0x%02x\n", val, addr);
    else
        nsc366_hwm_log("NSC366 Temperature Monitor: Write 0x%02x to register 0x%02x of bank %d\n", val, addr, TMS_BANK);

    switch (addr) {
        case 0x02:
            dev->tms_config_global[addr] = val & 0x3f;
            break;

        case 0x04:
            dev->tms_config_global[addr] = val & 0x3f;
            break;

        case 0x08:
            dev->tms_config_global[addr] = val & 3;
            break;

        case 0x09:
            dev->tms_config_global[addr] = val & 3;
            break;

        case 0x0a:
            if (TMS_BANK < 3)
                dev->tms_config_bank[TMS_BANK][addr - 0x0a] = val & 1;
            break;

        case 0x0c ... 0x0e:
            if (TMS_BANK < 3)
                dev->tms_config_bank[TMS_BANK][addr - 0x0a] = val;
            break;

        default:
            break;
    }
}

static uint8_t
nsc366_tms_read(uint16_t addr, void *priv)
{
    nsc366_hwm_t *dev = (nsc366_hwm_t *) priv;

    addr &= 0x000f;

    switch (addr) {
        case 0x00 ... 0x09:
            return dev->tms_config_global[addr];

        case 0x0a:
        case 0x0c ... 0x0e:
            if (TMS_BANK < 4)
                return dev->tms_config_bank[TMS_BANK][addr - 0x0a];
            else
                return 0;

        case 0x0b:
            if (TMS_BANK < 4) {
                if (dev->vlm_config_bank[VLM_BANK][0] & 1) {
                    nsc366_hwm_log("NSC366 Temperature Monitor: Reading %d Degrees Celsius from Bank %d\n", TEMP_FROM_REG(dev->tms_config_bank[TMS_BANK][1]), TMS_BANK);
                    return dev->tms_config_bank[TMS_BANK][1];
                } else
                    return 0;
            }

        default:
            return 0;
    }
}

void
nsc366_update_tms_io(int enable, uint16_t addr, nsc366_hwm_t *dev)
{
    if (dev->vlm_addr != 0)
        io_removehandler(dev->tms_addr, 15, nsc366_tms_read, NULL, NULL, nsc366_tms_write, NULL, NULL, dev);

    dev->tms_addr = addr;

    if ((addr != 0) && enable)
        io_sethandler(addr, 15, nsc366_tms_read, NULL, NULL, nsc366_tms_write, NULL, NULL, dev);
}

#define TEMP_FROM_REG(val) ((val) *1000)

static void
nsc366_hwm_reset(void *priv)
{
    nsc366_hwm_t *dev = (nsc366_hwm_t *) priv;
    memset(dev->fscm_config, 0, sizeof(dev->fscm_config));
    dev->fscm_enable = 0;
    dev->fscm_addr   = 0;

    /* Get fan reports from defaults */
    dev->fscm_config[0x07] = FAN_TO_REG(dev->values->fans[0], FAN_DIV_FROM_REG(dev->fscm_config[0x06]));
    dev->fscm_config[0x0a] = FAN_TO_REG(dev->values->fans[1], FAN_DIV_FROM_REG(dev->fscm_config[0x06]));
    dev->fscm_config[0x0d] = FAN_TO_REG(dev->values->fans[2], FAN_DIV_FROM_REG(dev->fscm_config[0x06]));

    memset(dev->vlm_config_global, 0, sizeof(dev->vlm_config_global));
    memset(dev->vlm_config_bank, 0, sizeof(dev->vlm_config_bank));
    dev->vlm_addr                = 0;
    dev->vlm_config_global[0x08] = 3;

    /* Get voltage reports from defaults */
    for (int i = 0; i < 13; i++) {
        dev->vlm_config_bank[i][1] = IN_TO_REG(dev->values->voltages[i], VREF);
    }

    memset(dev->tms_config_global, 0, sizeof(dev->tms_config_global));
    memset(dev->tms_config_bank, 0, sizeof(dev->tms_config_bank));
    dev->tms_addr                = 0;
    dev->tms_config_global[0x08] = 3;

    /* Get temperature reports from defaults */
    for (int i = 0; i < 4; i++)
        dev->tms_config_bank[i][1] = TEMP_TO_REG(dev->values->temperatures[i]);
}

static void
nsc366_hwm_close(void *priv)
{
    nsc366_hwm_t *dev = (nsc366_hwm_t *) priv;

    free(dev);
}

static void *
nsc366_hwm_init(UNUSED(const device_t *info))
{
    nsc366_hwm_t *dev = (nsc366_hwm_t *) malloc(sizeof(nsc366_hwm_t));
    memset(dev, 0, sizeof(nsc366_hwm_t));

    /* Initialize the default values (HWM is incomplete still) */
    hwm_values_t defaults = {
        {
            3000, /* FAN 0 */
            3000, /* FAN 1 */
            3000  /* FAN 2 */
        },
        {
            30, /* Temperatures which are broken */
            30,
            30,
            30
        },
        {
            0, /* Voltages which are broken */
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0
        }
    };
    hwm_values  = defaults;
    dev->values = &hwm_values;

    return dev;
}

const device_t nsc366_hwm_device = {
    .name          = "National Semiconductor NSC366 Hardware Monitor",
    .internal_name = "nsc366_hwm",
    .flags         = 0,
    .local         = 0,
    .init          = nsc366_hwm_init,
    .close         = nsc366_hwm_close,
    .reset         = nsc366_hwm_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
