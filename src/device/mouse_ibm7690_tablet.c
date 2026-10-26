/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          IBM 7690 optical touch panel and ADC.
 *
 * Authors: Josh Rodd, <josh@rodd.us>
 *
 *          Copyright (C) 2026 Simplebooks Foundation
 *          Copyright (C) 2026 Josh Rodd
 */
#include <stdint.h>
#include <stdlib.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/plat_unused.h>
#include <86box/timer.h>
#include <86box/mouse.h>
#include <86box/vid_mcga.h>

/*
 * The 7690's touch panel is an LED infrared beam frame over the LCD, wired to
 * the interface adapter at F300h-F303h. The guest driver scans the beams and
 * does its own conversion, so this models only what the scans touch: the two
 * selection latches, the optical controls, the ADC, and the diagnostic
 * inputs. Nothing here feeds the guest a coordinate; a finger on the LCD
 * obscures the beams it sits over, which is all the hardware exposes.
 *
 * Register behaviour follows the 7690 reference:
 *   F300h read/write  selection data: bits 7-4 Y-8/Y-4/Y-2/Y-1, bits 3-0
 *                     X-8/X-4/X-2/X-1
 *   F301h read        diagnostic inputs: 7 panel missing, 6 selection all
 *                     high (0 when the latch is FFh), 5 keyboard missing,
 *                     3 scanner type, 2 ADC check, 1 LCD power, 0 +12V
 *   F302h read/write  optical controls: 3 start ADC / convert busy (reads 1
 *                     again once the result is read), 2 receiver select,
 *                     1 driver enable, 0 LED select
 *   F303h read        6-bit ADC result; reading it consumes the result latch
 */

#define IBM7690_TOUCH_WIDTH 640
#define IBM7690_TOUCH_HEIGHT 480
#define IBM7690_TOUCH_BEAMS 64              /* 40 across, 24 down         */
#define IBM7690_TOUCH_HORIZ 40
#define IBM7690_TOUCH_CONTACT_ACROSS 8.0    /* host contact half-width, px */
#define IBM7690_TOUCH_CONTACT_DOWN 10.0
/* Provisional conversion time, not a measured ADC specification. Completion
   must follow emulated time, never the number of status reads. */
#define IBM7690_TOUCH_CONVERT_US 10

typedef struct {
    pc_timer_t adc_timer;
    void      *lcd;
    uint8_t    sel;
    uint8_t    control;
    uint8_t    receiver;
    uint8_t    emitter;
    uint8_t    enabled;   /* the selected emitter is driving */
    uint8_t    adc_ready; /* an unread conversion result is available */
    uint8_t    adc_sample;
    uint8_t    result;
} ibm7690_touch_t;

/*
 * Whether the beam a selection latches is broken by the host contact. The
 * beams are the panel's own geometry: the first 40 cross it, the remaining 24
 * run down it, and the picture's placement is the same one the panel renders
 * with, so a finger over a scanline obscures the beam for that scanline.
 */
static int
ibm7690_touch_blocked(const ibm7690_touch_t *dev)
{
    double px, py;
    int    index, top, content;

    if (mouse_tablet_in_proximity == 0)
        return 0;

    /* The receiver selection identifies the beam; a substituted emitter picks
       the same one by construction. */
    index = ((dev->receiver >> 4) * 8) + (dev->receiver & 7);
    if (index >= IBM7690_TOUCH_BEAMS)
        return 0;

    mouse_get_abs_coords(&px, &py);
    mcga_get_lcd_geometry(dev->lcd, &top, &content);

    px *= IBM7690_TOUCH_WIDTH;
    py  = (py * IBM7690_TOUCH_HEIGHT) - top;

    if (index < IBM7690_TOUCH_HORIZ) {
        const double beam = ((index + 0.5) * IBM7690_TOUCH_WIDTH) / IBM7690_TOUCH_HORIZ;

        return (px >= (beam - IBM7690_TOUCH_CONTACT_ACROSS)) &&
               (px <= (beam + IBM7690_TOUCH_CONTACT_ACROSS));
    }

    if ((py < 0.0) || (py >= content))
        return 0;

    const double beam = (((index - IBM7690_TOUCH_HORIZ) + 0.5) * content) /
                        (IBM7690_TOUCH_BEAMS - IBM7690_TOUCH_HORIZ);

    return (py >= (beam - IBM7690_TOUCH_CONTACT_DOWN)) &&
           (py <= (beam + IBM7690_TOUCH_CONTACT_DOWN));
}

static uint8_t
ibm7690_touch_sample(const ibm7690_touch_t *dev)
{
    /* Ideal clear/dark endpoints; the physical analog transfer is unmeasured. */
    return (dev->enabled && !ibm7690_touch_blocked(dev)) ? 0x00 : 0x3f;
}

static void
ibm7690_touch_convert(void *priv)
{
    ibm7690_touch_t *dev = (ibm7690_touch_t *) priv;

    dev->result    = dev->adc_sample;
    dev->adc_ready = 1;
}

static uint8_t
ibm7690_touch_read(uint16_t addr, void *priv)
{
    ibm7690_touch_t *dev = (ibm7690_touch_t *) priv;

    switch (addr & 3) {
        case 0x00:
            /* The reference does not establish useful readback here. */
            return dev->sel;

        case 0x01:
            {
                uint8_t ret = 0x00;

                /* All-high comparator: low only when every line is selected. */
                if (dev->sel != 0xff)
                    ret |= 0x40;
                /* +12 V is present. LCD Power OK is bit 1; bit 2 is the
                   independent ADC half-scale comparator input. */
                ret |= 0x01;
                if (mcga_get_lcd_power(dev->lcd))
                    ret |= 0x02;
                if (ibm7690_touch_sample(dev) >= 0x20)
                    ret |= 0x04;
                return ret;
            }

        case 0x02:
            {
                /* The low three bits read back the control latch the adapter
                   was written; the adapter self-test checks exactly that,
                   masking bit 3 out. */
                uint8_t ret = dev->control & 0x07;

                if (!dev->adc_ready)
                    ret |= 0x08;
                return ret;
            }

        default:
            break;
    }

    /* Consuming a result rearms the status latch, not the converter. It
       remains consumed until another start pulse completes in machine time. */
    dev->adc_ready = 0;
    return dev->result & 0x3f;
}

static void
ibm7690_touch_write(uint16_t addr, uint8_t val, void *priv)
{
    ibm7690_touch_t *dev = (ibm7690_touch_t *) priv;

    switch (addr & 3) {
        case 0x00:
            dev->sel = val;
            break;

        case 0x01:
            break;

        case 0x02: {
            const uint8_t old_control = dev->control;

            dev->control = val;
            /* Strobe the receiver selection. */
            if (val & 0x04)
                dev->receiver = dev->sel;
            /* Strobe the LED selection, then enable or disable it. */
            if (val & 0x01)
                dev->emitter = dev->sel;
            dev->enabled = (val & 0x02) ? 1 : 0;
            /* Both preserved drivers pulse start high then low. The exact
               physical edge is unknown; hold reset high and sample on falling. */
            if (val & 0x08) {
                timer_disable(&dev->adc_timer);
                dev->adc_ready = 0;
            } else if (old_control & 0x08) {
                dev->adc_ready  = 0;
                dev->adc_sample = ibm7690_touch_sample(dev);
                timer_set_delay_u64(&dev->adc_timer, IBM7690_TOUCH_CONVERT_US * TIMER_USEC);
            }
            break;
        }

        default:
            break;
    }
}

static void *
ibm7690_touch_init(UNUSED(const device_t *info))
{
    ibm7690_touch_t *dev = (ibm7690_touch_t *) calloc(1, sizeof(ibm7690_touch_t));

    /* The display is initialized before the internal tablet. */
    dev->lcd      = device_get_priv(&ibm7690_video_device);
    dev->receiver = 0xff;   /* nothing latched */
    timer_add(&dev->adc_timer, ibm7690_touch_convert, dev, 0);

    /* The adapter's ports; the panel's placement comes from the display. */
    io_sethandler(0xF300, 4,
                  ibm7690_touch_read, NULL, NULL,
                  ibm7690_touch_write, NULL, NULL, dev);

    return dev;
}

static void
ibm7690_touch_close(void *priv)
{
    ibm7690_touch_t *dev = (ibm7690_touch_t *) priv;

    timer_disable(&dev->adc_timer);

    io_removehandler(0xF300, 4,
                     ibm7690_touch_read, NULL, NULL,
                     ibm7690_touch_write, NULL, NULL, dev);

    free(dev);
}

/* A machine reset clears only the adapter's selection latches, optical
   controls and conversion phase. The LCD state belongs to the display. */
static void
ibm7690_touch_reset(void *priv)
{
    ibm7690_touch_t *dev = (ibm7690_touch_t *) priv;

    timer_disable(&dev->adc_timer);

    dev->sel        = 0x00;
    dev->control    = 0x00;
    dev->receiver   = 0xff;   /* nothing latched */
    dev->emitter    = 0x00;
    dev->enabled    = 0;
    dev->adc_ready  = 0;
    dev->adc_sample = 0;
    dev->result     = 0x00;
}

const device_t mouse_ibm7690_touch_device = {
    .name          = "IBM 7690 LED touchscreen",
    .internal_name = "ibm7690_touch",
    .flags         = DEVICE_ISA | DEVICE_ONBOARD,
    .local         = 0,
    .init          = ibm7690_touch_init,
    .close         = ibm7690_touch_close,
    .reset         = ibm7690_touch_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
