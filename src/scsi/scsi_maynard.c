/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Maynard 16-bit SCSI Adapter for the
 *          Western Digital WD33C93 SCSI controller: port decoding, the
 *          CardControl register, DMA byte/word assembly and the DMA pump.
 *
 * Authors: WNT50, <wnt2195@gmail.com>
 *
 *          Copyright 2026 WNT50.
 */
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/dma.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/scsi.h>
#include <86box/scsi_maynard.h>
#include <86box/scsi_wd33c93.h>

#ifdef ENABLE_MAYNARD_LOG
static int maynard_do_log = ENABLE_MAYNARD_LOG;

static void
maynard_log(const char *fmt, ...)
{
    va_list ap;

    if (maynard_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define maynard_log(fmt, ...)
#endif

static void maynard_callback(void *priv);

/* The bus engine asks the adapter to pace itself (used for PIO phases). */
static void
maynard_timer(void *priv, double period)
{
    maynard_t *dev = priv;

    /* The bus engine asks for pacing, and reports bus free with a zero
       period. That must not stop the DMA pump: bytes may still be sitting
       in the FIFO waiting for the host to program the channel. */
    if ((period > 0.0) && (dev->wd.state == WD_STATE_TRANSFER) &&
        !timer_is_enabled(&dev->timer))
        timer_on_auto(&dev->timer, 10.0);
}

/* The DMA channel is programmed by the host after it starts the transfer,
   so the move cannot be done inside the port write that kicks it off: the
   8237 would still hold the previous request's state. Run it on a timer
   instead, the way the real chip dribbles bytes at bus speed. */
static void
maynard_callback(void *priv)
{
    maynard_t *dev = priv;

    wd33c93_service_dma(&dev->wd);

    /* A one-shot timer: keep rearming while the data phase is running and
       stop as soon as it ends, so the completion interrupt is raised from
       here rather than from inside a port access. */
    if (dev->wd.state == WD_STATE_TRANSFER)
        timer_on_auto(&dev->timer, 10.0);
    else
        timer_stop(&dev->timer);
}

static void
maynard_irq(void *priv, int state)
{
    maynard_t *dev = priv;

    if (state == dev->irq_state)
        return;

    dev->irq_state = state;

    if (state)
        picint(1 << dev->irq);
    else
        picintc(1 << dev->irq);
}

/* A 16-bit DMA channel moves words, so two SCSI bytes have to be assembled
   into one before handing them to the controllers; an odd trailing byte is
   paired with zero, as the board's bus interface does. On an 8-bit channel
   every byte goes straight through. */
static int
maynard_dma_read(void *priv, uint8_t *val)
{
    maynard_t *dev = priv;
    int        ret;

    /* The host programs the channel after it starts the transfer, so a
       channel that is still set up for the previous request has to be
       ignored: report "not ready" and leave the byte on the bus. */
    if ((dma[dev->dma].mode & 0x0c) != 0x08)
        return -1;

    if (!dma[dev->dma].size) {
        ret = dma_channel_read(dev->dma);
        if (ret == DMA_NODATA) {
            maynard_log("[MAYNARD] DMA %d read failed\n", dev->dma);
            return -1;
        }

        *val = (uint8_t) ret;

        return 0;
    }

    if (dev->dma_half) {
        dev->dma_half = 0;
        *val          = (uint8_t) (dev->dma_word >> 8);

        return 0;
    }

    ret = dma_channel_read(dev->dma);
    if (ret == DMA_NODATA) {
        maynard_log("[MAYNARD] DMA %d read failed\n", dev->dma);
        return -1;
    }

    dev->dma_word = (uint16_t) ret;
    dev->dma_half = 1;
    *val          = (uint8_t) dev->dma_word;

    return 0;
}

static int
maynard_dma_write(void *priv, uint8_t val)
{
    maynard_t *dev = priv;
    int        ret;

    /* See maynard_dma_read: wait for the host to program the channel. */
    if ((dma[dev->dma].mode & 0x0c) != 0x04)
        return -1;

    if (!dma[dev->dma].size) {
        ret = dma_channel_write(dev->dma, val);
        if (ret == DMA_NODATA) {
            maynard_log("[MAYNARD] DMA %d write %02X failed (ena %02X, "
                        "msk %02X, mode %02X, count %04X)\n",
                        dev->dma, val, dma_e, dma_m, dma[dev->dma].mode,
                        (unsigned) dma[dev->dma].cb);
            return -1;
        }

        return 0;
    }

    if (!dev->dma_half) {
        dev->dma_word = val;
        dev->dma_half = 1;

        return 0;
    }

    dev->dma_half = 0;

    ret = dma_channel_write(dev->dma, dev->dma_word | (val << 8));
    if (ret == DMA_NODATA) {
        /* Keep the low byte staged so a retry sends the same word. */
        dev->dma_half = 1;
        return -1;
    }

    return 0;
}

/* Board control register (base+2):
     bit0    ResetScsiBus, 1 = hold the SCSI bus in reset
     bit1-2  EnableDmaWrite / BufferedIo, not modelled
     bit3-5  IRQ code (see maynard_irq_map)
     bit6-7  DMA channel - 4

   The driver writes the registry parameters here from HwInitialize, so the
   IRQ and DMA channel in use are the ones written here, not the values
   selected in the device configuration.

   The IRQ field is not a linear encoding: HwFindAdapter maps the requested
   IRQ through a jump table and stores a 3-bit code, so only these seven
   lines can be selected. */
static const uint8_t maynard_irq_map[8] = { 0, 3, 4, 5, 10, 11, 12, 15 };

static void
maynard_control(maynard_t *dev, uint8_t val)
{
    uint8_t irq = maynard_irq_map[(val >> 3) & 0x07];
    uint8_t dma = 4 + ((val >> 6) & 0x03);

    maynard_log("[MAYNARD] CardControl %02X: IRQ %d, DMA %d, reset %d\n",
                val, irq, dma, val & 0x01);

    if ((irq != 0) && (irq != dev->irq)) {
        if (dev->irq_state)
            picintc(1 << dev->irq);

        dev->irq = irq;

        if (dev->irq_state)
            picint(1 << irq);
    }

    dev->dma = dma;

    /* ResetScsiBus is a level: HwResetBus writes 1 twice, HwInitialize clears it. */
    if (val & 0x01) {
        if (!dev->reset_hold) {
            dev->reset_hold = 1;
            dev->dma_half   = 0;
            wd33c93_reset(&dev->wd);
        }
    } else {
        dev->reset_hold = 0;
    }
}

static uint8_t
maynard_read(uint16_t port, void *priv)
{
    maynard_t *dev = priv;
    uint16_t   off = port - dev->base;
    uint8_t    ret;

    switch (off) {
        case 0x00:
        case 0x01:
            ret = wd33c93_port_read(&dev->wd, off);
            maynard_log("[MAYNARD] in  %04X -> %02X\n", port, ret);
            return ret;

        default:
            return 0xff;
    }
}

static void
maynard_write(uint16_t port, uint8_t val, void *priv)
{
    maynard_t *dev = priv;
    uint16_t   off = port - dev->base;

    maynard_log("[MAYNARD] out %04X <- %02X\n", port, val);

    switch (off) {
        case 0x00:
        case 0x01:
            /* Programming the transfer count starts a new byte stream, so
               any half word left over from the previous one should be dropped,
               otherwise the 16-bit DMA words would be shifted by one byte. */
            if ((off == 0x01) && (dev->wd.addr == WD_TRANSFER_COUNT_MSB) &&
                (dev->wd.iface != WD_IFACE_DIRECT))
                dev->dma_half = 0;

            wd33c93_port_write(&dev->wd, off, val);
            break;

        case 0x02:
            maynard_control(dev, val);
            break;

        case 0x04:
        case 0x05:
            /* Direction-specific DMA start: base+5 for transfers from the
               target into host memory, base+4 for the other direction. The
               driver writes these right after issuing the command, so this
               is where the data actually moves. */
            maynard_log("[MAYNARD] DMA start %s\n",
                        (off == 0x05) ? "in" : "out");
            timer_on_auto(&dev->timer, 10.0);
            break;

        default:
            /* SetWriteLogic / SetReadLogic are not modelled. */
            break;
    }
}

static void *
maynard_init(const device_t *info)
{
    maynard_t *dev;
    uint8_t    bus;

    (void) info;

    bus = scsi_get_bus();
    if (bus == 0xff)
        return NULL;

    dev = calloc(1, sizeof(*dev));

    dev->base = device_get_config_hex16("base");

    /* The IRQ line and DMA channel are not board straps: the driver picks
       them and writes them to CardControl from HwInitialize. These are
       only the values used until that first write. */
    dev->irq = 10;
    dev->dma = 6;

    maynard_log("[MAYNARD] init: base %04X, IRQ %d, DMA %d, bus %d\n",
                dev->base, dev->irq, dev->dma, bus);

    dev->wd.bus       = bus;
    dev->wd.priv      = dev;
    dev->wd.iface     = WD_IFACE_INDIRECT;

    dev->wd.irq_ena   = maynard_irq;
    dev->wd.dma_read  = maynard_dma_read;
    dev->wd.dma_write = maynard_dma_write;

    dev->wd.scsibus.bus_device = bus;
    dev->wd.scsibus.priv       = dev;
    dev->wd.scsibus.timer      = maynard_timer;

    timer_add(&dev->timer, maynard_callback, dev, 0);

    wd33c93_reset(&dev->wd);

    io_sethandler(dev->base, 0x07,
                  maynard_read, NULL, NULL,
                  maynard_write, NULL, NULL, dev);
    scsi_bus_set_speed(bus, 10000000.0);

    return dev;
}

static void
maynard_close(void *priv)
{
    maynard_t *dev = priv;

    timer_disable(&dev->timer);

    if (dev->irq_state)
        picintc(1 << dev->irq);

    io_removehandler(dev->base, 0x07,
                     maynard_read, NULL, NULL,
                     maynard_write, NULL, NULL, dev);

    free(dev);
}

static void
maynard_reset(void *priv)
{
    maynard_t *dev = priv;

    dev->dma_half = 0;
    wd33c93_reset(&dev->wd);
}

// clang-format off
static const device_config_t maynard_config[] = {
    {
        .name           = "base",
        .description    = "Address",
        .type           = CONFIG_HEX16,
        .default_string = NULL,
        .default_int    = 0x360,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "360H", .value = 0x360 },
            { .description = "370H", .value = 0x370 },
            { .description = ""                     }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t maynard_wd33c93_device = {
    .name          = "Maynard 16-bit SCSI Adapter (WD33C93)",
    .internal_name = "maynard_wd33c93",
    .flags         = DEVICE_ISA16,
    .local         = 0,
    .init          = maynard_init,
    .close         = maynard_close,
    .reset         = maynard_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = maynard_config,
};
