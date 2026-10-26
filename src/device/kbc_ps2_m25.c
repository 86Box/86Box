/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          IBM Personal System/2 Model 25 keyboard and pointing-device
 *          interface.
 *
 * The original 8086 Model 25 has no 8042. Two serial interfaces in the
 * system-board gate array expose device data at 67h and 68h. Their control
 * and status registers occupy 66h, 69h, and 6Ah. BIOS copies received data
 * into the software latch at 60h before dispatching the normal keyboard or
 * pointing-device handler. The implementation follows the IBM Personal
 * System/2 Model 25 Technical Reference, first edition (June 1987).
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include <86box/device.h>
#include <86box/io.h>
#include <86box/keyboard.h>
#include <86box/machine.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/plat_unused.h>
#include <86box/timer.h>

typedef struct ps2_m25_kbc_t {
    uint8_t port_60;
    uint8_t port_63;
    uint8_t port_66;
    uint8_t port_69;
    uint8_t diagnostic_irqs;
    uint8_t tx_ready[2];
    uint8_t rx_data[2];
    uint8_t irq_source;

    pc_timer_t poll_timer;
} ps2_m25_kbc_t;

static kbc_at_port_t *
ps2_m25_kbc_interface(unsigned interface)
{
    /*
     * With the connectors used as marked, interface 1 is the pointing device
     * and interface 2 is the keyboard. Port 66h bit 3 records that assignment
     * for BIOS; it does not switch the electrical interfaces.
     */
    return (interface < 2) ? kbc_at_ports[interface ^ 1] : NULL;
}

static void
ps2_m25_kbc_ack_port(ps2_m25_kbc_t *dev, unsigned port)
{
    kbc_at_port_t *iface = ps2_m25_kbc_interface(port);

    if ((iface != NULL) && (iface->out_new != -1)) {
        iface->out_new = -1;
        if (dev->irq_source) {
            picintc(1 << 1);
            dev->irq_source = 0;
        }
    }
}

static void
ps2_m25_kbc_start_tx(ps2_m25_kbc_t *dev, unsigned port, uint8_t val)
{
    kbc_at_port_t *iface = ps2_m25_kbc_interface(port);

    if ((port < 2) && dev->tx_ready[port] &&
        (iface != NULL) && (iface->priv != NULL)) {
        iface->dat          = val;
        iface->wantcmd      = 1;
        dev->tx_ready[port] = 0;
    }
}

static uint8_t
ps2_m25_kbc_read(uint16_t addr, void *priv)
{
    ps2_m25_kbc_t *dev = (ps2_m25_kbc_t *) priv;

    switch (addr) {
        case 0x0060:
            return dev->port_60;

        case 0x0063:
            /*
             * Port 69h bit 6 selects which master interrupt-controller
             * initialization word is visible through this register. BIOS
             * normally reads ICW2 here to locate the IRQ1 vector.
             */
            {
                const uint8_t ret = (dev->port_69 & 0x40) ?
                                    pic_read_icw(0, 0) :
                                    pic_read_icw(0, 1);
                return ret;
            }

        case 0x0066:
            /*
             * Bit 2 reflects the external keyboard-lock input. The Model 25
             * has no active lock here, so the status is always "unlocked".
             */
            return dev->port_66 | 0x04;

        case 0x0067:
        case 0x0068:
            {
                const unsigned port = addr - 0x0067;
                kbc_at_port_t *iface = ps2_m25_kbc_interface(port);

                if ((iface != NULL) && (iface->out_new != -1))
                    dev->rx_data[port] = iface->out_new;
                return dev->rx_data[port];
            }

        case 0x0069:
            return dev->port_69;

        case 0x006a:
            /*
             * The Model 25 BIOS waits on bit 5 for data at 67h and bit 2
             * for data at 68h.
             */
            return ((ps2_m25_kbc_interface(0) != NULL) &&
                    (ps2_m25_kbc_interface(0)->out_new != -1) ? 0x20 : 0x00) |
                   ((ps2_m25_kbc_interface(1) != NULL) &&
                    (ps2_m25_kbc_interface(1)->out_new != -1) ? 0x04 : 0x00);

        case 0x00a0:
            /*
             * Internal IRQ1 source status: bit 2 is interface 1 (67h),
             * and bit 3 is interface 2 (68h).
             */
            return ((ps2_m25_kbc_interface(0) != NULL) &&
                    (ps2_m25_kbc_interface(0)->out_new != -1) ? 0x04 : 0x00) |
                   ((ps2_m25_kbc_interface(1) != NULL) &&
                    (ps2_m25_kbc_interface(1)->out_new != -1) ? 0x08 : 0x00);

        default:
            return 0xff;
    }
}

static void
ps2_m25_kbc_write(uint16_t addr, uint8_t val, void *priv)
{
    ps2_m25_kbc_t *dev = (ps2_m25_kbc_t *) priv;

    switch (addr) {
        case 0x0060:
            dev->port_60 = val;
            break;

        case 0x0063:
            /*
             * Port 69h bit 7 enables the gate array's interrupt diagnostic
             * path. Bits 7-2 then drive IRQ7-IRQ2, while bit 0 drives the
             * diagnostic NMI input when port 69h bit 2 is also enabled.
             */
            if (dev->port_69 & 0x80) {
                if (dev->diagnostic_irqs)
                    picintc(dev->diagnostic_irqs);

                dev->diagnostic_irqs = val & 0xfc;
                if (dev->diagnostic_irqs)
                    picint(dev->diagnostic_irqs);

                if ((dev->port_69 & 0x04) &&
                    !(dev->port_63 & 0x01) && (val & 0x01))
                    nmi_raise();
            }
            dev->port_63 = val;
            break;

        case 0x0066:
            /*
             * A low-to-high pulse on bit 4 acknowledges interface 1;
             * bit 6 performs the same operation for interface 2.
             */
            if (!(dev->port_66 & 0x10) && (val & 0x10))
                ps2_m25_kbc_ack_port(dev, 0);
            if (!(dev->port_66 & 0x40) && (val & 0x40))
                ps2_m25_kbc_ack_port(dev, 1);
            dev->port_66 = val & ~0x04;
            break;

        case 0x0067:
        case 0x0068:
            {
                const unsigned port = addr - 0x0067;

                /*
                 * BIOS first pulses the interface's transmit preparation
                 * latch at 69h, then writes the byte to the data register.
                 */
                ps2_m25_kbc_start_tx(dev, port, val);
                break;
            }

        case 0x0069:
            /* A rising bit prepares the associated data register to transmit. */
            if (!(dev->port_69 & 0x08) && (val & 0x08))
                dev->tx_ready[0] = 1;
            if (!(dev->port_69 & 0x10) && (val & 0x10))
                dev->tx_ready[1] = 1;
            if ((dev->port_69 & 0x80) && !(val & 0x80) &&
                dev->diagnostic_irqs) {
                picintc(dev->diagnostic_irqs);
                dev->diagnostic_irqs = 0;
            }
            dev->port_69 = val;
            break;

        case 0x00a0:
            /* Bit 7 is the documented global NMI enable. */
            nmi_mask = val & 0x80;
            break;

        default:
            break;
    }
}

static void
ps2_m25_kbc_poll(void *priv)
{
    ps2_m25_kbc_t *dev = (ps2_m25_kbc_t *) priv;
    int pending     = 0;
    int newly_ready = 0;

    timer_advance_u64(&dev->poll_timer, 100ULL * TIMER_USEC);

    for (unsigned port = 0; port < 2; port++) {
        kbc_at_port_t *iface = ps2_m25_kbc_interface(port);

        if ((iface != NULL) && (iface->priv != NULL)) {
            const int old_out = iface->out_new;

            iface->poll(iface->priv);
            newly_ready |= ((old_out == -1) && (iface->out_new != -1));
            if (iface->out_new != -1)
                dev->rx_data[port] = iface->out_new;
            pending |= (iface->out_new != -1);
        }
    }

    /*
     * Receive-buffer-full is latched until software acknowledges the
     * interface at port 66h. If IRQ1 was accepted while the previous
     * keyboard handler was still returning, the PIC request can disappear
     * before that acknowledgement. Keep the gate-array request visible
     * whenever its receive latch is still full and the PIC no longer has
     * IRQ1 pending or in service.
     */
    const int irq_visible = (pic.irr | pic.isr) & (1 << 1);

    if (pending && (!dev->irq_source || newly_ready || !irq_visible)) {
        /*
         * Each receive-buffer-full transition latches a new gate-array
         * interrupt request. This also lets the higher-priority keyboard
         * interface interrupt while a pointing-device byte remains pending.
         * Interface 2 has priority when both receive buffers are full.
         */
        dev->irq_source = ((ps2_m25_kbc_interface(1) != NULL) &&
                           (ps2_m25_kbc_interface(1)->out_new != -1)) ? 2 : 1;
        picint(1 << 1);
    }
}

static void
ps2_m25_kbc_reset(void *priv)
{
    ps2_m25_kbc_t *dev = (ps2_m25_kbc_t *) priv;

    dev->port_60    = 0x00;
    dev->port_63    = 0x00;
    dev->port_66    = 0x00;
    dev->port_69    = 0x00;
    if (dev->diagnostic_irqs)
        picintc(dev->diagnostic_irqs);
    dev->diagnostic_irqs = 0;
    dev->irq_source = 0;
    /* The system-board NMI latch powers up enabled. */
    nmi_mask = 0x80;
    memset(dev->tx_ready, 0x00, sizeof(dev->tx_ready));
    memset(dev->rx_data, 0x00, sizeof(dev->rx_data));
    picintc(1 << 1);

    for (unsigned port = 0; port < 2; port++) {
        if (kbc_at_ports[port] != NULL) {
            kbc_at_ports[port]->wantcmd = 0;
            kbc_at_ports[port]->out_new = -1;
        }
    }
}

static void *
ps2_m25_kbc_init(UNUSED(const device_t *info))
{
    ps2_m25_kbc_t *dev = (ps2_m25_kbc_t *) calloc(1, sizeof(ps2_m25_kbc_t));

    for (unsigned port = 0; port < 2; port++) {
        kbc_at_ports[port] = (kbc_at_port_t *) calloc(1, sizeof(kbc_at_port_t));
        kbc_at_ports[port]->out_new = -1;
    }

    if (keyboard_type == KEYBOARD_TYPE_INTERNAL) {
        if (machine_has_flags(machine, MACHINE_KEYBOARD_JIS))
            device_add(&keyboard_ps55_device);
        else
            device_add_params(&keyboard_at_generic_device,
                              (void *) (uintptr_t) FLAG_PS2_KBD);
    } else
        keyboard_add_device();

    /*
     * There is no 8042 scan-code translator on this machine. BIOS selects
     * set 1 at the keyboard, and IBM software expects that selection to
     * survive the keyboard's Set Default commands.
     */
    keyboard_at_set_scancode_set_persistent(1);

    io_sethandler(0x0060, 1,
                  ps2_m25_kbc_read, NULL, NULL,
                  ps2_m25_kbc_write, NULL, NULL, dev);
    io_sethandler(0x0063, 1,
                  ps2_m25_kbc_read, NULL, NULL,
                  ps2_m25_kbc_write, NULL, NULL, dev);
    io_sethandler(0x0066, 5,
                  ps2_m25_kbc_read, NULL, NULL,
                  ps2_m25_kbc_write, NULL, NULL, dev);
    io_sethandler(0x00a0, 1,
                  ps2_m25_kbc_read, NULL, NULL,
                  ps2_m25_kbc_write, NULL, NULL, dev);

    timer_add(&dev->poll_timer, ps2_m25_kbc_poll, dev, 1);
    ps2_m25_kbc_reset(dev);

    return dev;
}

static void
ps2_m25_kbc_close(void *priv)
{
    ps2_m25_kbc_t *dev = (ps2_m25_kbc_t *) priv;

    timer_disable(&dev->poll_timer);
    picintc(1 << 1);
    if (dev->diagnostic_irqs)
        picintc(dev->diagnostic_irqs);

    io_removehandler(0x0060, 1,
                     ps2_m25_kbc_read, NULL, NULL,
                     ps2_m25_kbc_write, NULL, NULL, dev);
    io_removehandler(0x0063, 1,
                     ps2_m25_kbc_read, NULL, NULL,
                     ps2_m25_kbc_write, NULL, NULL, dev);
    io_removehandler(0x0066, 5,
                     ps2_m25_kbc_read, NULL, NULL,
                     ps2_m25_kbc_write, NULL, NULL, dev);
    io_removehandler(0x00a0, 1,
                     ps2_m25_kbc_read, NULL, NULL,
                     ps2_m25_kbc_write, NULL, NULL, dev);

    for (unsigned port = 0; port < 2; port++) {
        free(kbc_at_ports[port]);
        kbc_at_ports[port] = NULL;
    }

    free(dev);
}

const device_t kbc_ps2_m25_device = {
    .name          = "IBM PS/2 Model 25 Keyboard/Pointing Device Interface",
    .internal_name = "kbc_ps2_m25",
    .flags         = DEVICE_KBC,
    .local         = 0,
    .init          = ps2_m25_kbc_init,
    .close         = ps2_m25_kbc_close,
    .reset         = ps2_m25_kbc_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
