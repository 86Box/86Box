/* IBM 5140 PC Convertible motherboard, fixed floppy adapter and clock gating. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include "x86.h"
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/char.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/machine.h>
#include <86box/dma.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/nmi.h>
#include <86box/fdd.h>
#include <86box/fdc_ext.h>
#include <86box/fdc.h>
#include <86box/serial.h>
#include <86box/lpt.h>
#include <86box/video.h>
#include <86box/vid_ibm5140.h>
#include <86box/ibm5140_power.h>
#include <86box/m_ibm5140.h>

extern const device_t ibm5140_device;

typedef struct ibm5140_t {
    ibm5140_video_t *video;
    ibm5140_power_t *power;
    fdc_t *fdc;
    serial_t *serial;
    lpt_t *parallel;
    uint8_t feature_control;
    uint8_t disk_control;
    uint8_t dor;
    uint8_t memory_control;
    pc_timer_t printer_timer;
    double printer_bit_usec;
    uint16_t printer_divisor, printer_shift;
    uint8_t printer_mode, printer_data, printer_full, printer_bits;
    uint8_t printer_running, printer_space, printer_pending, printer_irq;
} ibm5140_t;

static ibm5140_t *ibm5140;


void
ibm5140_feature_control(uint8_t value)
{
    ibm5140_t *dev = ibm5140;
    if (!dev)
        return;
    if ((dev->feature_control ^ value) & 5) {
        if (dev->serial) {
            serial_remove(dev->serial);
            if (value & 4)
                serial_setup(dev->serial, (value & 1) ? COM1_ADDR : COM2_ADDR,
                             (value & 1) ? COM1_IRQ : COM2_IRQ);
        }
        if (dev->parallel) {
            lpt_port_remove(dev->parallel);
            if (value & 4)
                lpt_port_setup(dev->parallel, 0x378);
        }
    }
    dev->feature_control = value;
}

static void
ibm5140_fdc_request(ibm5140_t *dev)
{
    if ((dev->disk_control & 0xc0) == 0x80)
        ibm5140_power_nmi(dev->power, 0x80, 1);
}

static uint8_t
ibm5140_fdc_read(uint16_t port, void *priv)
{
    ibm5140_t *dev = priv;
    if (port == 0x77) {
        if (dev->disk_control & 0x10)
            return dev->disk_control;
        int fdd_id = (dev->disk_control & 8) ? 0 : 1;
        fdd_id += dev->fdc->bus;
        return fdd_current_track(&drives[fdd_id]);
    }
    switch (port & 7) {
        case 2:
            /* DOR is write-only, but its read select still requests power. */
            ibm5140_fdc_request(dev);
            return 0xff;
        case 4: case 5:
            if (!(dev->disk_control & 0x40)) {
                ibm5140_fdc_request(dev);
                return 0xff;
            }
            return fdc_read(port, dev->fdc);
        case 7: {
            unsigned drive = dev->dor & 3;
            uint8_t value = (dev->dor & 0x10) | ((dev->dor & 0x20) >> 2);
            if (drive < 2) {
                fdd_drive_t *drv = (fdd_drive_t *) dev->fdc->fdd[drive];
                if ((dev->dor & 8) && !dev->fdc->drive_interface_gated)
                    value |= drive ? 0x20 : 0x40;
                if (drv->changed || drv->empty)
                    value |= 0x80;
                if (fdd_track0(drv))
                    value |= 1;
            }
            return value;
        }
        default:
            return 0xff;
    }
}

static void
ibm5140_fdc_write(uint16_t port, uint8_t value, void *priv)
{
    ibm5140_t *dev = priv;
    if (port == 0x77) {
        const uint8_t old = dev->disk_control;
        dev->disk_control = value;
        if ((old ^ value) & 0x40) {
            /* Controller power does not move the physical heads or erase the
               board's DOR latch. The ROM resynchronizes the 765's lost PCNs. */
            fdc_reset(dev->fdc);
            dev->fdc->dor = dev->dor;
            fdc_set_power_down(dev->fdc, !(value & 0x40));
        }
        dev->fdc->drive_interface_gated = !!(value & 0x20) || !(value & 0x40);
        if (value & 0x40)
            ibm5140_power_nmi(dev->power, 0x80, 0);
        return;
    }
    switch (port & 7) {
        case 2:
            dev->dor = value & 0x3f;
            if (dev->disk_control & 0x40)
                fdc_write(port, dev->dor, dev->fdc);
            else {
                dev->fdc->dor = dev->dor;
                for (unsigned i = 0; i < 2; i++)
                    fdd_set_motor_enable(&drives[dev->fdc->bus + i], dev->dor & (0x10 << i));
                ibm5140_fdc_request(dev);
            }
            break;
        case 5:
            if (dev->disk_control & 0x40)
                fdc_write(port, value, dev->fdc);
            else
                ibm5140_fdc_request(dev);
            break;
        /* MSR and DIR are read-only; this is not an AT rate/DSR decoder. */
    }
}

static uint8_t
ibm5140_memory_read(uint16_t port, void *priv)
{
    (void) port;
    return ((ibm5140_t *) priv)->memory_control;
}

static void
ibm5140_memory_write(uint16_t port, uint8_t value, void *priv)
{
    ibm5140_t *dev = priv;
    (void) port;
    dev->memory_control = value & 1;
    ibm5140_video_substitute(dev->video, value & 1);
}

/* The system-board transmitter exists even with no portable printer attached.
 * It has a holding register and an 8N2 shifter, not an 8250 register bank.
 * Diagnostic mode loops the serial output into the error-status input. */
static void
ibm5140_printer_irq(ibm5140_t *dev)
{
    const int asserted = !!(dev->printer_pending & dev->printer_mode & 0x30);
    if (asserted != dev->printer_irq)
        picint_common(0x80, PIC_IRQ_LEVEL, asserted, &dev->printer_irq);
}

static void
ibm5140_printer_line(ibm5140_t *dev, uint8_t space)
{
    if ((dev->printer_mode & 0x60) == 0x60 && dev->printer_space != space)
        dev->printer_pending |= 0x20;
    dev->printer_space = space;
    ibm5140_printer_irq(dev);
}

static int
ibm5140_printer_busy(const ibm5140_t *dev)
{
    /* Unattached connector inputs are high. Diagnostic busy gates the
     * shifter, not writes into the separate holding register. */
    return (dev->printer_mode & 0x48) == 0x48;
}

static void
ibm5140_printer_start(ibm5140_t *dev)
{
    dev->printer_running = 1;
    ibm5140_printer_line(dev, !(dev->printer_shift & 1));
    timer_set_delay_u64(&dev->printer_timer, dev->printer_bit_usec * TIMER_USEC);
}

static void
ibm5140_printer_tick(void *priv)
{
    ibm5140_t *dev = priv;
    if (dev->printer_running) {
        dev->printer_shift >>= 1;
        if (--dev->printer_bits) {
            ibm5140_printer_line(dev, !(dev->printer_shift & 1));
            timer_advance_u64(&dev->printer_timer, dev->printer_bit_usec * TIMER_USEC);
            return;
        }
        dev->printer_running = 0;
        ibm5140_printer_line(dev, 0);
    }
    if (dev->printer_full) {
        dev->printer_shift = 0x600 | ((uint16_t) dev->printer_data << 1);
        dev->printer_bits = 11;
        dev->printer_full = 0;
        dev->printer_pending |= 0x10;
        ibm5140_printer_irq(dev);
        if (!ibm5140_printer_busy(dev))
            ibm5140_printer_start(dev);
    }
}

static uint8_t
ibm5140_printer_read(uint16_t port, void *priv)
{
    ibm5140_t *dev = priv;
    if (port == 0x7a)
        return dev->printer_mode;
    if (dev->printer_mode & 0x80)
        return dev->printer_divisor >> (8 * (port & 1));
    if (port == 0x78)
        return dev->printer_data;

    uint8_t status = 0x50;
    if (!dev->printer_full)
        status |= 0x80;
    /* The shifter is empty during its final stop mark, but the framing timer
     * must hold that mark for a full bit before starting another character. */
    if (dev->printer_bits <= 1 && !dev->printer_full)
        status |= 1;
    if (!ibm5140_printer_busy(dev))
        status |= 2;
    if (!(dev->printer_mode & 0x40) || dev->printer_space)
        status |= 8;
    if (dev->printer_pending & dev->printer_mode & 0x30)
        status |= 4;
    dev->printer_pending = 0;
    ibm5140_printer_irq(dev);
    return status;
}

static void
ibm5140_printer_write(uint16_t port, uint8_t value, void *priv)
{
    ibm5140_t *dev = priv;
    if (port == 0x7a) {
        const uint8_t old = dev->printer_mode;
        dev->printer_mode = value & 0xfc;
        if ((value & ~old & 0x10) && !dev->printer_full)
            dev->printer_pending |= 0x10;
        ibm5140_printer_irq(dev);
        if (dev->printer_bits && !dev->printer_running && !ibm5140_printer_busy(dev))
            ibm5140_printer_start(dev);
    } else if (dev->printer_mode & 0x80) {
        if (port & 1)
            dev->printer_divisor = (dev->printer_divisor & 0xff) | ((uint16_t) value << 8);
        else
            dev->printer_divisor = (dev->printer_divisor & 0xff00) | value;
        /* The rate counter counts up to rollover on the 4.77 MHz clock.
         * The ROM loads F076h for approximately 1200 bits/second. */
        dev->printer_bit_usec = (0x10000u - dev->printer_divisor) *
                                (3000000.0 / 14318184.0);
    } else if (port == 0x78) {
        dev->printer_data = value;
        dev->printer_full = 1;
        dev->printer_pending &= ~0x10;
        ibm5140_printer_irq(dev);
        if (!dev->printer_bits && !timer_is_enabled(&dev->printer_timer))
            timer_set_delay_u64(&dev->printer_timer, dev->printer_bit_usec * TIMER_USEC);
    }
}

void
ibm5140_chipset_reset(void)
{
    ibm5140_t *dev = ibm5140;
    if (!dev)
        return;
    pic_reset();
    pic_init_ibm5140(ibm5140_clock_wake);
    dma_reset();
    dma_init_ibm5140();
    dev->disk_control = dev->dor = dev->memory_control = 0;
    timer_disable(&dev->printer_timer);
    dev->printer_divisor = dev->printer_shift = 0;
    dev->printer_bit_usec = 65536.0 * (3000000.0 / 14318184.0);
    dev->printer_mode = dev->printer_data = dev->printer_full = dev->printer_bits = 0;
    dev->printer_running = dev->printer_space = dev->printer_pending = dev->printer_irq = 0;
    if (dev->fdc) {
        fdc_reset(dev->fdc);
        dev->fdc->dor = 0;
        dev->fdc->drive_interface_gated = 1;
        fdc_set_power_down(dev->fdc, 1);
        for (unsigned i = 0; i < 2; i++) {
            fdd_drive_t *drv = (fdd_drive_t *) dev->fdc->fdd[i];
            fdd_set_motor_enable(drv, 0);
            drv->changed = 1;
        }
    }
}

static void
ibm5140_close(void *priv)
{
    ibm5140_t *dev = priv;
    io_removehandler(0x77, 1, ibm5140_fdc_read, NULL, NULL, ibm5140_fdc_write, NULL, NULL, dev);
    io_removehandler(0x78, 3, ibm5140_printer_read, NULL, NULL, ibm5140_printer_write, NULL, NULL, dev);
    timer_disable(&dev->printer_timer);
    if (dev->printer_irq)
        picintclevel(0x80, &dev->printer_irq);
    io_removehandler(0x3f0, 8, ibm5140_fdc_read, NULL, NULL, ibm5140_fdc_write, NULL, NULL, dev);
    io_removehandler(0x80, 1, ibm5140_memory_read, NULL, NULL, ibm5140_memory_write, NULL, NULL, dev);
    if (dev->serial)
        serial_remove(dev->serial);
    if (dev->parallel)
        lpt_port_remove(dev->parallel);
    ibm5140 = NULL;
    free(dev);
}

int
machine_ibm5140_init(const machine_t *model)
{
    int ret = bios_load_linear("roms/machines/ibm5140/BIOS.BIN", 0xf0000, 0x10000, 0);
    if (bios_only || !ret)
        return ret;

    ibm5140_t *dev = calloc(1, sizeof(*dev));
    if (!dev)
        return 0;
    ibm5140 = dev;
    device_context(model->device);
    const int display = machine_get_config_int("display");
    const int composite = machine_get_config_int("composite");
    const int adapter = machine_get_config_int("serial_parallel");
    device_context_restore();

    const int configured_pit = pit_mode;
    pit_mode = 0; /* The native suspend interface needs the classic counters. */
    machine_common_init(model);
    pit_mode = configured_pit;
    pic_init_ibm5140(ibm5140_clock_wake);
    dma_init_ibm5140();
    /* No timer1 refresh callback: all installed working memory is SRAM. */
    pit_devs[0].set_using_timer(pit_devs[0].data, 1, 0);

    fdc_current[0] = FDC_INTERNAL;
    dev->fdc = device_add(&fdc_ibm5140_device);
    for (unsigned i = 0; i < FDD_NUM; i++) {
        fdd_set_type(&drives[dev->fdc->bus + i], i < 2 ? fdd_get_from_internal_name("35_2dd") : 0);
        fdd_set_turbo(&drives[dev->fdc->bus + i], 0);
    }
    video_reset(gfxcard[0]);
    dev->video = ibm5140_video_create(display, composite);
    if (adapter) {
        dev->serial = device_add(&ns16450_device); /* 8250A-compatible scratch register. */
        serial_remove(dev->serial);
        dev->parallel = device_add(&lpt_port_device);
        lpt_port_remove(dev->parallel);
        lpt_port_irq(dev->parallel, 7);
    }
    /* Optional attachments use the existing UART/LPT instances. The built-in
     * printer transmitter has no attached printer protocol or text spooler. */
    serial_set_next_inst(SERIAL_MAX - 1);
    lpt_set_next_inst(PARALLEL_MAX - 1);
    timer_add(&dev->printer_timer, ibm5140_printer_tick, dev, 0);
    dev->power = ibm5140_power_create(dev->video);
    io_sethandler(0x77, 1, ibm5140_fdc_read, NULL, NULL, ibm5140_fdc_write, NULL, NULL, dev);
    io_sethandler(0x78, 3, ibm5140_printer_read, NULL, NULL, ibm5140_printer_write, NULL, NULL, dev);
    io_sethandler(0x3f0, 8, ibm5140_fdc_read, NULL, NULL, ibm5140_fdc_write, NULL, NULL, dev);
    io_sethandler(0x80, 1, ibm5140_memory_read, NULL, NULL, ibm5140_memory_write, NULL, NULL, dev);
    /* Close the bus wrapper before its child controllers. */
    device_add_ex(&ibm5140_device, dev);
    return 1;
}

static const device_config_t ibm5140_config[] = {
    {
        .name = "display", .description = "Display", .type = CONFIG_SELECTION,
        .default_int = 0,
        .selection = {
            { .description = "Backlit LCD", .value = 0 },
            { .description = "LCD", .value = 1 },
            { .description = "Improved-contrast LCD", .value = 2 },
            { .description = "IBM 5144 monochrome CRT", .value = 3 },
            { .description = "IBM 5145 color CRT", .value = 4 },
            { .description = "TV connector 8285989 (composite)", .value = 5 },
            { .description = "" }
        }
    },
    {
        .name = "composite", .description = "Composite model", .type = CONFIG_SELECTION,
        .default_int = 1,
        .selection = {
            { .description = "Old CGA", .value = 0 },
            { .description = "New CGA", .value = 1 },
            { .description = "PCjr", .value = 2 },
            { .description = "" }
        }
    },
    {
        .name = "serial_parallel", .description = "Serial/parallel adapter", .type = CONFIG_BINARY,
        .default_int = 0
    },
    { .name = "", .type = CONFIG_END }
};

const device_t ibm5140_device = {
    .name = "IBM PC Convertible",
    .internal_name = "ibm5140",
    .close = ibm5140_close,
    .config = ibm5140_config
};
