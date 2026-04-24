/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/fifo.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/lpt.h>
#include <86box/pic.h>
#include <86box/sound.h>
#include <86box/thread.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/plat_fallthrough.h>

static int    next_inst               = 0;
static int    lpt_3bc_used            = 0;

static lpt_t *lpt1;

lpt_port_t    lpt_ports[PARALLEL_MAX];

lpt_device_t  lpt_devs[PARALLEL_MAX];

#ifdef ENABLE_LPT_LOG
int lpt_do_log = ENABLE_LPT_LOG;

static void
lpt_log(const char *fmt, ...)
{
    va_list ap;

    if (lpt_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define lpt_log(fmt, ...)
#endif

static inline void
lpt_char_update_control(lpt_t *dev, uint32_t flags)
{
    if (dev->port.chardev.control && (dev->port.lpt.control != flags)) {
        dev->port.lpt.control = flags;
        dev->port.chardev.control(CHAR_RAW_CONTROL(flags), dev->port.chardev.priv);
    }
}

static void
lpt_char_pti_mode(lpt_t *dev, uint8_t mode)
{
    dev->char_pti_mode    = mode;
    dev->char_pti_readout = 0xf; /* values are unknown but required for cable detection */
    switch (mode) {
        case 0xe0:
            dev->char_pti_readout = 0xc;
            break;

        case 0xe1:
            lpt_log("[W] PTI bidirectional receive mode\n");
            dev->char_pti_readout = 0xa;
            goto rx_setup_wait;

        case 0xe2:
            dev->char_pti_readout = 0x0;
            break;

        case 0xe3:
            lpt_log("[W] PTI ECP receive mode\n");
            dev->char_pti_readout = 0x1;
rx_setup_wait:
            /* Tell the transmitter to set up (!nFault) but not send data just yet (!Select). */
            if (dev->port.chardev.write) {
                lpt_char_update_control(dev, dev->port.lpt.control & ~CHAR_LPT_EPP);
                uint8_t buf = 0xe0;
                dev->port.chardev.write(&buf, sizeof(buf), dev->port.chardev.priv);
            }
            break;

        case 0xe5:
            lpt_log("[W] PTI 4-bit mode\n");
            break;

        case 0xf1:
            lpt_log("[W] PTI bidirectional transmit mode\n");
            goto tx_setup_wait;

        case 0xf3:
            lpt_log("[W] PTI ECP transmit mode\n");
tx_setup_wait:
            /* Assert nFault to wait for the receiver to set up. */
            dev->char_read |= 0x01;
            break;

        default:
            break;
    }
    lpt_log("[R] PTI %02X = %02X (status %02X)\n", mode, dev->char_pti_readout, dev->char_pti_readout << 3);
}

static void
lpt_char_write_data(uint8_t val, void *priv)
{
    lpt_t *dev = (lpt_t *) priv;

    /* Reset status read spin loop counter. */
    dev->char_spin_count = 0;

    if ((dev->port.chardev.flags & CHAR_LPT_PTI) && ((dev->ctrl & 0x0f) == 0x0e)) {
        /* PTI command mode. (SelectIn|nInit|autofd) */
        lpt_char_pti_mode(dev, val);
    } else if (((dev->char_pti_mode & 0xed) == 0xe1) && (dev->ctrl & 0x08)) {
        /* PTI bidirectional/ECP modes: stop sending status updates until the receiver
           is set up (SelectIn goes low). This ensures the status we've sent upon
           entering a mode isn't overwritten by the E5 written after exiting command mode. */
        lpt_log("Not writing byte %02X due to status hold (ctrl=%02X ecr=%02X)\n", val, dev->ctrl, dev->ecr);
    } else if ((dev->port.chardev.flags & CHAR_LPT_USESTROBE) || (dev->char_pti_mode == 0xf1)) { /* PTI bidirectional transmit */
        /* Store data for strobe. */
        dev->char_write = val;
    } else {
        lpt_char_update_control(dev, dev->port.lpt.control & ~CHAR_LPT_EPP);
        dev->port.chardev.write(&val, sizeof(val), dev->port.chardev.priv);
    }
}

static void
lpt_char_strobe(uint8_t old, uint8_t val, void *priv)
{
    lpt_t *dev = (lpt_t *) priv;

    if ((dev->port.chardev.flags & CHAR_LPT_USESTROBE) && !(val & 0x01) && (old & 0x01)) {
        lpt_char_update_control(dev, dev->port.lpt.control & ~CHAR_LPT_EPP);
        dev->port.chardev.write(&dev->char_write, sizeof(dev->char_write), dev->port.chardev.priv);
    }
}

static void
lpt_char_read_data(void *priv)
{
    lpt_t *dev = (lpt_t *) priv;

    /* It's less likely that we get more than one byte at a time,
       but modern 64-bit CPUs give us a free transition check for 8. */
    uint8_t buf[8] = { 0 };
    int     input  = (dev->ext || dev->epp || (dev->ecp && (dev->ecr & 0xe0))) && (dev->ctrl & 0x20) && /* bidirectional input */
                     !((dev->char_pti_mode == 0xe1) && ((dev->ctrl & 0x0c) != 0x04)); /* PTI bidirectional receive not in progress (SelectIn || !nInit) */
    size_t  read;
    if (input) {
        if (dev->char_pti_mode == 0xe1) { /* PTI bidirectional receive in progress (!SelectIn && nInit) */
            /* Only read if a receive has been strobed and is pending. */
            read = !(dev->ctrl & 0x02) ^ !(dev->char_read & 0x10);
        } else if (dev->ecp && (dev->ecr & 0xc0)) { /* ECP receive */
            read = fifo_get_remaining(dev->fifo);
            if (read > sizeof(buf))
                read = sizeof(buf);
        } else { /* regular bidirectional receive */
            read = 1;
        }
    } else { /* 4-bit receive */
        read = sizeof(buf);
    }
    int period = 100;
    if (LIKELY(read > 0)) {
        read = dev->port.chardev.read(buf, read, dev->port.chardev.priv);
        if (read > 0) {
            lpt_log("Read %cb: %02X%c%02X%c%02X%c%02X%c%02X%c%02X%c%02X%c%02X",
                input ? '8': '4',
                buf[0], (read > 1) ? ' ' : '\0',
                buf[1], (read > 2) ? ' ' : '\0',
                buf[2], (read > 3) ? ' ' : '\0',
                buf[3], (read > 4) ? ' ' : '\0',
                buf[4], (read > 5) ? ' ' : '\0',
                buf[5], (read > 6) ? ' ' : '\0',
                buf[6], (read > 7) ? ' ' : '\0',
                buf[7]
            );
            lpt_log("\n");
            if (input) {
                for (size_t i = 0; i < read; i++)
                    lpt_write_to_fifo(dev, buf[i]);
                if (dev->char_pti_mode == 0xe1) {
                    /* PTI bidirectional receive: signal that a byte was received, accounting for BUSY inversion. */
                    if (dev->ctrl & 0x02)
                        buf[read - 1] |= 0x10;
                    else
                        buf[read - 1] &= ~0x10;
                }
                period = 1;
            } else {
                uint64_t masked = AS_U64(buf[0]) & 0x0808080808080808ULL;
                uint64_t prev   = (masked << 8) | (dev->char_read & 0x08);
                if (~prev & masked)
                    lpt_irq(dev, 1);
    #ifdef DROP_EMULATION_SPEED_INSTEAD
                if (dev->enable_irq)
    #endif
                    period = 2;
            }
            dev->char_read = buf[read - 1];
        }
    } else if (input) {
        period = 1;
    }
    timer_advance_u64(&dev->char_in_timer, TIMER_USEC * period);
}

static void
lpt_char_write_ctrl(uint8_t val, void *priv)
{
    lpt_t *dev = (lpt_t *) priv;

    /* Reset status read spin loop counter. */
    dev->char_spin_count = 0;

    if ((dev->port.chardev.flags & CHAR_LPT_PTI) && ((val & 0x0f) == 0x0e)) { /* PTI command mode (SelectIn|nInit|autofd) */
        lpt_char_pti_mode(dev, dev->dat);
    } else if ((dev->char_pti_mode & 0xef) == 0xe1) { /* PTI bidirectional modes */
        /* Tell the other end to begin bidirectional transfer (Select) once SelectIn goes low. */
        if ((dev->ctrl & 0x08) && !(val & 0x08)) {
            lpt_char_update_control(dev, dev->port.lpt.control & ~CHAR_LPT_EPP);
            uint8_t buf = 0xe2 | (dev->char_pti_mode & 0x10);
            dev->port.chardev.write(&buf, sizeof(buf), dev->port.chardev.priv);
        }

        /* Strobe data on autofd edge if !SelectIn && nInit. */
        if (((dev->ctrl & 0x0c) == 0x04) && ((dev->ctrl ^ val) & 0x02)) {
            lpt_char_update_control(dev, dev->port.lpt.control & ~CHAR_LPT_EPP);
            /* Send byte when in transmit mode. */
            if ((dev->char_pti_mode & 0x10) && dev->port.chardev.write)
                dev->port.chardev.write(&dev->char_write, sizeof(dev->char_write), dev->port.chardev.priv);

            /* Move on to the next byte, accounting for BUSY inversion.
               This means both transmit complete and receive pending. */
            if (val & 0x02)
                dev->char_read &= ~0x10;
            else
                dev->char_read |= 0x10;
        }
    } else {
        lpt_char_update_control(dev, (dev->port.lpt.control & ~0xff00) | ((uint16_t) val << 8));
        if (dev->port.chardev.write)
            lpt_char_strobe(dev->ctrl, val, dev);
    }
}

static uint8_t
lpt_char_read_status(void *priv)
{
    lpt_t *dev = (lpt_t *) priv;

    if ((dev->port.chardev.flags & CHAR_LPT_PTI) && ((dev->ctrl & 0x0f) == 0x0e)) { /* PTI command mode */
        return dev->char_pti_readout << 3;
    } else if (dev->port.chardev.flags & (CHAR_LPT_NIBBLE | CHAR_LPT_PTI)) {
        /* Trigger a read outside the timer if the guest appears to be waiting for a
           response by spin-looping status reads. Strictest loop limits for reference:
           - Windows DCC (ptilink.sys 1.1.0.0): 4096 spins with 1us wait
           - Linux PLIP >=1.1.20: 500 spins with 1us wait
                         <1.1.20: 4 jiffies (40ms?)
           - Crynwr DOS PLIP: 137ms */
        if (++dev->char_spin_count >= 125) {
            dev->char_spin_count = 0;
            lpt_char_read_data(dev);
        }
        return (dev->char_read << 3) ^ (CHAR_RAW_STATUS(0) >> 8);
    } else if (dev->port.chardev.status) {
        return CHAR_RAW_STATUS(dev->port.chardev.status(dev->port.chardev.priv)) >> 8;
    } else {
        return 0xff;
    }
}

static void
lpt_char_epp_write_data(uint8_t is_addr, uint8_t val, void *priv)
{
    lpt_t *dev = (lpt_t *) priv;

    lpt_char_update_control(dev, (dev->port.lpt.control & ~CHAR_LPT_EPP) | (is_addr ? CHAR_LPT_EPP_ADDR : CHAR_LPT_EPP_DATA));
    dev->port.chardev.write(&val, sizeof(val), dev->port.chardev.priv);
}

static void
lpt_char_epp_request_read(uint8_t is_addr, void *priv)
{
    lpt_t *dev = (lpt_t *) priv;

    lpt_char_update_control(dev, (dev->port.lpt.control & ~CHAR_LPT_EPP) | (is_addr ? CHAR_LPT_EPP_ADDR : CHAR_LPT_EPP_DATA));
    dev->port.chardev.read(&dev->in_dat, sizeof(dev->in_dat), dev->port.chardev.priv);
}

void
lpt_devices_init(void)
{
    for (uint8_t i = 0; i < PARALLEL_MAX; i++) {
        memset(&(lpt_devs[i]), 0x00, sizeof(lpt_device_t));

        lpt_t *lpt = lpt_ports[i].lpt;
        if (!lpt)
            continue;

        memset(&lpt->port, 0, sizeof(lpt->port));
        if (lpt_ports[i].device) {
            lpt->port.type = CHAR_PORT_LPT;
            snprintf(lpt->port.name, sizeof(lpt->port.name), "LPT%i", i + 1);
            const device_t *device = char_get_device(lpt_ports[i].device);
            char_init(&lpt->port, device, i + 1);
            if (lpt->port.attached) {
                /* Attach character device shim. */
                lpt->port.lpt.control = -1; /* force update on first control write */
                device_context_inst(device, i + 1);
                lpt_attach(
                    lpt->port.chardev.write ? lpt_char_write_data : NULL,
                    (lpt->port.chardev.control || (lpt->port.chardev.flags & (CHAR_LPT_USESTROBE | CHAR_LPT_PTI))) ? lpt_char_write_ctrl : NULL,
                    (lpt->port.chardev.write && (lpt->port.chardev.flags & CHAR_LPT_USESTROBE)) ? lpt_char_strobe : NULL,
                    (lpt->port.chardev.status || (lpt->port.chardev.flags & (CHAR_LPT_NIBBLE | CHAR_LPT_PTI))) ? lpt_char_read_status : NULL,
                    NULL, /* read_ctrl */
                    lpt->port.chardev.write ? lpt_char_epp_write_data : NULL,
                    lpt->port.chardev.read ? lpt_char_epp_request_read : NULL,
                    lpt
                );
                if (lpt->port.chardev.read)
                    timer_set_delay_u64(&lpt->char_in_timer, (uint64_t) (2.0 * (double) TIMER_USEC));
            }
        }
    }
}

void *
lpt_attach(void    (*write_data)(uint8_t val, void *priv),
           void    (*write_ctrl)(uint8_t val, void *priv),
           void    (*strobe)(uint8_t old, uint8_t val,void *priv),
           uint8_t (*read_status)(void *priv),
           uint8_t (*read_ctrl)(void *priv),
           void    (*epp_write_data)(uint8_t is_addr, uint8_t val, void *priv),
           void    (*epp_request_read)(uint8_t is_addr, void *priv),
           void    *priv)
{
    int port                        = device_get_instance() - 1;

    lpt_devs[port].write_data       = write_data;
    lpt_devs[port].write_ctrl       = write_ctrl;
    lpt_devs[port].strobe           = strobe;
    lpt_devs[port].read_status      = read_status;
    lpt_devs[port].read_ctrl        = read_ctrl;
    lpt_devs[port].epp_write_data   = epp_write_data;
    lpt_devs[port].epp_request_read = epp_request_read;
    lpt_devs[port].priv             = priv;

    return lpt_ports[port].lpt;
}

void
lpt_devices_close(void)
{
    for (uint8_t i = 0; i < PARALLEL_MAX; i++) {
        memset(&(lpt_devs[i]), 0x00, sizeof(lpt_device_t));

        if (lpt_ports[i].lpt)
            timer_disable(&lpt_ports[i].lpt->char_in_timer);
    }
}

void
lpt_devices_reset(void)
{
    device_close_by_flags(DEVICE_LPT);

    lpt_devices_close();

    lpt_devices_init();
}

static uint8_t
lpt_get_ctrl_raw(const lpt_t *dev)
{
    uint8_t ret;

    if (dev->dt && dev->dt->read_ctrl && dev->dt->priv)
        ret = (dev->dt->read_ctrl(dev->dt->priv) & 0xef) | dev->enable_irq;
    else
        ret = 0xc0 | dev->ctrl | dev->enable_irq;

    return ret;
}

static uint8_t
lpt_is_epp(const lpt_t *dev)
{
    return (dev->epp || ((dev->ecp) && ((dev->ecr & 0xe0) == 0x80)));
}

static uint8_t
lpt_get_ctrl(const lpt_t *dev)
{
    uint8_t ret = lpt_get_ctrl_raw(dev);

    if (!dev->ecp && !dev->epp)
        ret |= 0x20;

    return ret;
}

static void
lpt_write_fifo(lpt_t *dev, const uint8_t val, const uint8_t tag)
{
    if (!fifo_get_full(dev->fifo)) {
        fifo_write_evt_tagged(tag, val, dev->fifo);

        if (!timer_is_enabled(&dev->fifo_out_timer))
            timer_set_delay_u64(&dev->fifo_out_timer, (uint64_t) ((1000000.0 / 2500000.0) * (double) TIMER_USEC));
    }
}

static void
lpt_ecp_update_irq(lpt_t *dev)
{
    if (dev->irq == 0xff)
        return;
    else if (!(dev->ecr & 0x04) && ((dev->fifo_stat | dev->dma_stat) & 0x04))
        picintlevel(1 << dev->irq, &dev->irq_state);
    else
        picintclevel(1 << dev->irq, &dev->irq_state);
}

static void
lpt_strobe(lpt_t *dev, const uint8_t val)
{
    if (dev->dt && dev->dt->strobe && dev->dt->priv)
        dev->dt->strobe(dev->strobe, val, dev->dt->priv);

    dev->strobe = val;
}

static void
lpt_fifo_out_callback(void *priv)
{
    lpt_t *dev = (lpt_t *) priv;

    if ((dev->ecr & 0xe0) != 0xc0)  switch (dev->state) {
        default:
            break;

        case LPT_STATE_READ_DMA:
            ;
            int ret = 0xff;

            if (dev->dma == 0xff)
                ret = DMA_NODATA;
            else
                ret = dma_channel_read(dev->dma);

            if (ret != DMA_NODATA) {
                fifo_write_evt_tagged(0x01, (uint8_t) (ret & 0xff), dev->fifo);

                if (ret & DMA_OVER)
                    /* Internal flag to indicate we have finished the DMA reads. */
                    dev->dma_stat = 0x08;
            }

            timer_advance_u64(&dev->fifo_out_timer,
                              (uint64_t) ((1000000.0 / 2500000.0) * (double) TIMER_USEC));

            if (dev->dma_stat || fifo_get_full(dev->fifo))
                dev->state = LPT_STATE_WRITE_FIFO;
            break;

        case LPT_STATE_WRITE_FIFO:
            if (!fifo_get_empty(dev->fifo)) {
                uint8_t tag = 0x00;
                const uint8_t val = fifo_read_evt_tagged(&tag, dev->fifo);

                lpt_log("FIFO: %02X, TAG = %02X\n", val, tag);

                /* We do not currently support sending commands. */
                if (tag == 0x01) {
                    if (dev->dt && dev->dt->write_data && dev->dt->priv)
                        dev->dt->write_data(val, dev->dt->priv);

                    lpt_strobe(dev, 1);
                    lpt_strobe(dev, 0);
                }
            }

            if (((dev->ecr & 0xe0) != 0xc0) && (dev->ecr & 0x08)) {
                if (fifo_get_empty(dev->fifo)) {
                    if (dev->dma_stat) {
                        /* Now actually set the external flag. */
                        dev->dma_stat = 0x04;
                        dev->state = LPT_STATE_IDLE;
                        lpt_ecp_update_irq(dev);
                    } else {
                        dev->state = LPT_STATE_READ_DMA;

                        timer_advance_u64(&dev->fifo_out_timer,
                                          (uint64_t) ((1000000.0 / 2500000.0) * (double) TIMER_USEC));
                    }
                } else
                    timer_advance_u64(&dev->fifo_out_timer,
                                      (uint64_t) ((1000000.0 / 2500000.0) * (double) TIMER_USEC));
            } else if (!fifo_get_empty(dev->fifo))
                timer_advance_u64(&dev->fifo_out_timer,
                                  (uint64_t) ((1000000.0 / 2500000.0) * (double) TIMER_USEC));
            break;
    }
}

static void
lpt_fifo_d_ready_evt(void *priv)
{
    lpt_t *dev = (lpt_t *) priv;

    if (!(dev->ecr & 0x08)) {
        if (((dev->ecr & 0xe0) == 0xc0) || ((dev->ecr & 0xe0) == 0x40) ||
            (lpt_get_ctrl_raw(dev) & 0x20))
            dev->fifo_stat = fifo_get_ready(dev->fifo) ? 0x04 : 0x00;
        else
            dev->fifo_stat = fifo_get_ready(dev->fifo) ? 0x00 : 0x04;
    }

    lpt_ecp_update_irq(dev);
}

void
lpt_write(const uint16_t port, const uint8_t val, void *priv)
{
    lpt_t *dev  = (lpt_t *) priv;
    uint16_t    mask = 0x0407;

    lpt_log("[W] %04X = %02X (ECR = %02X)\n", port, val, dev->ecr);

    /* This is needed so the parallel port at 3BC works. */
    if (dev->addr & 0x0004)
        mask = 0x0403;

    switch (port & mask) {
        case 0x0000:
            if (dev->ecp) {
                if (((dev->ecr & 0xe0) == 0x40) || ((dev->ecr & 0xe0) == 0x60) ||
                    ((dev->ecr & 0xe0) == 0xc0))
                    /* AFIFO */
                    lpt_write_fifo(dev, val, 0x00);
                else if (!(dev->ecr & 0xc0) && (!(dev->ecr & 0x20) || !(lpt_get_ctrl_raw(dev) & 0x20)) &&
                           dev->dt && dev->dt->write_data && dev->dt->priv)
                    /* DATAR */
                    dev->dt->write_data(val, dev->dt->priv);
                dev->dat = val;
            } else {
                /* DTR */
                if ((!(dev->ext || dev->epp) || !(lpt_get_ctrl_raw(dev) & 0x20)) && dev->dt &&
                    dev->dt->write_data && dev->dt->priv)
                    dev->dt->write_data(val, dev->dt->priv);
                dev->dat = val;
            }
            break;

        case 0x0001:
            break;

        case 0x0002:
            if (dev->dt && dev->dt->write_ctrl && dev->dt->priv)
                dev->dt->write_ctrl(val, dev->dt->priv);
            dev->ctrl       = val;
            dev->strobe     = val & 0x01;
            dev->autofeed   = val & 0x02;
            dev->enable_irq = val & 0x10;
            if (!(val & 0x10) && (dev->irq != 0xff))
                picintc(1 << dev->irq);
            dev->irq_state  = 0;
            break;

        case 0x0003:
            if (lpt_is_epp(dev)) {
                if (dev->dt && dev->dt->epp_write_data && dev->dt->priv)
                    dev->dt->epp_write_data(1, val, dev->dt->priv);
            }
            break;

        case 0x0004 ... 0x0007:
            if (lpt_is_epp(dev)) {
                if (dev->dt && dev->dt->epp_write_data && dev->dt->priv)
                    dev->dt->epp_write_data(0, val, dev->dt->priv);
            }
            break;

        case 0x0404:
            if (dev->cfg_regs_enabled) {
                switch (dev->eir) {
                    case 0x00:
                        dev->ext_regs[0x00] = val & 0x31;
                        break;
                    case 0x02:
                        dev->ext_regs[0x02] = val & 0xd0;
                        if (dev->ext_regs[0x02] & 0x80)
                            dev->ecr        = dev->ret_ecr;
                        else  switch (dev->ret_ecr & 0xe0) {
                            case 0x00: case 0x20:
                            case 0x80:
                                dev->ecr = (dev->ret_ecr & 0x1f) | 0x60;
                                break;
                        }
                        break;
                    case 0x04:
                        dev->ext_regs[0x00] = val & 0x37;
                        break;
                    case 0x05:
                        dev->ext_regs[0x00] = val;
                        dev->cnfga_readout  = (val & 0x80) ? 0x1c : 0x14;
                        dev->cnfgb_readout  = (dev->cnfgb_readout & 0xc0) | (val & 0x3b);
                        break;
                }
                break;
            } else
                fallthrough;
        case 0x0400:
            switch (dev->ecr >> 5) {
                default:
                    break;
                case 2: case 6:
                    lpt_write_fifo(dev, val, 0x01);
                    break;
                case 3:
                    if (!(lpt_get_ctrl_raw(dev) & 0x20))
                        lpt_write_fifo(dev, val, 0x01);
                    break;
            }
            break;

        case 0x0402: case 0x0406:
            if ((val & 0xc0) == 0x40) { /* FIFO modes */
                if (((dev->ecr & 0x0c) != 0x08) && ((val & 0x0c) == 0x08)) { /* transition to dmaEn && !serviceIntr */
                    dev->dma_stat = 0x00;
                    dev->state = LPT_STATE_READ_DMA;
                } else if ((val & 0x0c) != 0x08) { /* !dmaEn || serviceIntr */
                    dev->state = LPT_STATE_WRITE_FIFO;
                }
                if (((dev->char_pti_mode & 0xef) == 0xe3) && dev->port.chardev.write) {
                    /* PTI ECP modes: tell the other end to begin ECP transfer (Select).
                       There's a control write (nInit|ackIntEn) right before this ECR
                       write, but we're not taking any chances with code block boundaries. */
                    lpt_char_update_control(dev, dev->port.lpt.control & ~CHAR_LPT_EPP);
                    uint8_t buf = 0xe2;
                    dev->port.chardev.write(&buf, sizeof(buf), dev->port.chardev.priv);
                    dev->char_pti_mode = 0x00;
                }
                if (!timer_is_enabled(&dev->fifo_out_timer))
                    timer_set_delay_u64(&dev->fifo_out_timer, (uint64_t) ((1000000.0 / 2500000.0) * (double) TIMER_USEC));
            } else { /* non-FIFO modes */
                if ((dev->ecr & 0xc0) && !(val & 0xc0)) /* reset FIFO on transition to standard or PS/2 */
                    fifo_reset(dev->fifo);
                dev->state = LPT_STATE_IDLE;
                if (timer_is_enabled(&dev->fifo_out_timer))
                    timer_disable(&dev->fifo_out_timer);
            }
            if (dev->ext_regs[0x02] & 0x80)
                dev->ecr = val;
            else  switch (val & 0xe0) {
                case 0x00: case 0x20:
                case 0x80:
                    dev->ecr = (val & 0x1f) | 0x60;
                    break;
            }
            dev->ret_ecr = val;
            lpt_fifo_d_ready_evt(dev); /* update fifo_stat and IRQ */
            break;

        case 0x0403: case 0x0407:
            if (dev->cfg_regs_enabled)
                dev->eir = val & 0x07;
            break;

        default:
            break;
    }
}

void
lpt_write_to_fifo(void *priv, const uint8_t val)
{
    lpt_t *dev = (lpt_t *) priv;

    if (dev->ecp) {
        if (((dev->ecr & 0xe0) == 0x20) && (lpt_get_ctrl_raw(dev) & 0x20))
            dev->in_dat = val;
        else if (((dev->ecr & 0xe0) == 0x40) && !fifo_get_full(dev->fifo))
            fifo_write_evt_tagged(0x01, val, dev->fifo);
        else if (((dev->ecr & 0xe0) == 0xc0) && !fifo_get_full(dev->fifo))
            fifo_write_evt_tagged(0x01, val, dev->fifo);
        else if (((dev->ecr & 0xe0) == 0x60) && (lpt_get_ctrl_raw(dev) & 0x20) &&
            !fifo_get_full(dev->fifo))
            fifo_write_evt_tagged(0x01, val, dev->fifo);

        if (((dev->ecr & 0xe0) != 0xc0) && ((dev->ecr & 0x0c) == 0x08) && (dev->dma != 0xff)) {
            const int ret = dma_channel_write(dev->dma, val);

            if (ret & DMA_OVER)
                dev->dma_stat |= 0x04;
        }
    } else {
        if ((dev->ext || dev->epp) && (lpt_get_ctrl_raw(dev) & 0x20))
            dev->in_dat = val;
    }
}

void
lpt_write_to_dat(void *priv, const uint8_t val)
{
    lpt_t *dev = (lpt_t *) priv;

    dev->in_dat = val;
}

static uint8_t
lpt_read_fifo(const lpt_t *dev)
{
    uint8_t ret = 0xff;

    if (!fifo_get_empty(dev->fifo))
        ret = fifo_read(dev->fifo);

    return ret;
}

uint8_t
lpt_read_status(lpt_t *dev)
{
    uint8_t     low_bits = 0x07;
    uint8_t     ret;

    if (dev->ext) {
        low_bits = 0x03 | (dev->irq_state ? 0x00 : 0x04);
        if (dev->irq != 0xff)
            picintclevel(1 << dev->irq, &dev->irq_state);
        dev->irq_state = 0;
    }
    if (dev->epp || dev->ecp) {
        low_bits = lpt_is_epp(dev) ? 0x02 : 0x03;
        if (lpt_get_ctrl_raw(dev) & 0x10)
            low_bits |= (dev->irq_state ? 0x00 : 0x04);
        else
            low_bits |= 0x04;
    }

    if (dev->dt && dev->dt->read_status && dev->dt->priv)
        ret = (dev->dt->read_status(dev->dt->priv) & 0xf8) | low_bits;
    else
        ret = 0xd8 | low_bits;

    return ret;
}

uint8_t
lpt_read_ecp_mode(lpt_t *dev)
{
    return ((dev->ret_ecr & 0xe0) == 0x60) ? 0x60 : 0x00;
}

uint8_t
lpt_read(const uint16_t port, void *priv)
{
    lpt_t    *dev  = (lpt_t *) priv;
    uint16_t  mask = 0x0407;
    uint8_t   ret  = 0xff;

    /* This is needed so the parallel port at 3BC works. */
    if (dev->addr & 0x0004)
        mask = 0x0403;

    switch (port & mask) {
        case 0x0000:
            if (dev->ecp) {
                if (dev->ecr & 0xc0) {
                    if (((dev->ecr & 0xe0) == 0xc0) && !fifo_get_empty(dev->fifo)) {
                        uint8_t tag = 0x00;
                        ret = fifo_read_evt_tagged(&tag, dev->fifo);
                    } else if (((dev->ecr & 0xe0) == 0x60) && !(lpt_get_ctrl_raw(dev) & 0x20) &&
                               !fifo_get_empty(dev->fifo)) {
                        uint8_t tag = 0x00;
                        ret = fifo_read_evt_tagged(&tag, dev->fifo);
                    } else if ((dev->ecr & 0xe0) == 0xe0) {
                        /* The Windows Direct Cable Connection driver (at least ptilink.sys 1.1.0.0)
                           has a bug where it reads data instead of cnfgA in configuration mode while
                           probing the ECP FIFO. This is undefined, and the code doesn't like it if
                           the FIFO width is not 8 bits, so hardware must be aliasing cnfgA to data. */
                        ret = dev->cnfga_readout;
                    }
                } else
                    ret = (lpt_get_ctrl_raw(dev) & 0x20) ? dev->in_dat : dev->dat;
            } else {
                /* DTR */
                ret = (lpt_get_ctrl_raw(dev) & 0x20) ? dev->in_dat : dev->dat;
            }
            break;

        case 0x0001:
            ret = lpt_read_status(dev);
            break;

        case 0x0002:
            ret = lpt_get_ctrl(dev);
            if (dev->ecp)
                ret = (ret & 0xfc) | (dev->ctrl & 0x03);
            break;

        case 0x0003:
            if (lpt_is_epp(dev)) {
                if (dev->dt && dev->dt->epp_request_read && dev->dt->priv)
                    dev->dt->epp_request_read(1, dev->dt->priv);
                ret = dev->in_dat;
            }
            break;

        case 0x0004 ... 0x0007:
            if (lpt_is_epp(dev)) {
                if (dev->dt && dev->dt->epp_request_read && dev->dt->priv)
                    dev->dt->epp_request_read(0, dev->dt->priv);
                ret = dev->in_dat;
            }
            break;

        case 0x0404:
            if (dev->cfg_regs_enabled) {
                switch (dev->eir) {
                    default:
                        ret = 0xff;
                        break;
                    case 0x00: case 0x02:
                    case 0x04: case 0x05:
                        ret = dev->ext_regs[dev->eir];
                        break;
                }
                break;
            } else
                fallthrough;
        case 0x0400:
            switch (dev->ecr >> 5) {
                default:
                    break;
                case 3:
                    if (lpt_get_ctrl_raw(dev) & 0x20)
                        ret = lpt_read_fifo(dev);
                    break;
                case 6:
                    /* TFIFO */
                    if (!fifo_get_empty(dev->fifo))
                        ret = fifo_read_evt(dev->fifo);
                    break;
                case 7:
                    /* CNFGA */
                    ret = dev->cnfga_readout;
                    break;
            }
            break;

        case 0x0405:
            if (dev->cfg_regs_enabled) {
                ret = 0x00;
                break;
            } else
                fallthrough;
        case 0x0401:
            if ((dev->ecr & 0xe0) == 0xe0) {
                /* CNFGB */
                ret = dev->cnfgb_readout | (dev->irq_state ? 0x40 : 0x00);
            }
            break;

        case 0x0402: case 0x0406:
            ret = dev->ret_ecr | dev->fifo_stat | (dev->dma_stat & 0x04);
            if (fifo_get_full(dev->fifo))
                ret |= 0x02;
            else
                ret &= ~0x02;
            if (fifo_get_empty(dev->fifo))
                ret |= 0x01;
            else
                ret &= ~0x01;
            break;

        case 0x0403: case 0x0407:
            if (dev->cfg_regs_enabled)
                ret = dev->eir;
            break;

        default:
            break;
    }

    lpt_log("[R] %04X = %02X\n", port, ret);

    return ret;
}

uint8_t
lpt_read_port(lpt_t *dev, const uint16_t reg)
{
    return lpt_read(reg, dev);
}

void
lpt_irq(void *priv, const int raise)
{
    lpt_t *dev = (lpt_t *) priv;

    if (dev->enable_irq) {
        if (dev->irq != 0xff) {
            if (dev->ext || dev->epp) {
                if (raise)
                    picintlevel(1 << dev->irq, &dev->irq_state);
                else
                    picintclevel(1 << dev->irq, &dev->irq_state);
            } else {
                if (raise)
                    picint(1 << dev->irq);
                else
                    picintc(1 << dev->irq);
            }
        }

        if (!(dev->ext || dev->epp) || (dev->irq == 0xff))
            dev->irq_state = raise;
    } else {
        if (dev->irq != 0xff) {
            if (dev->ext || dev->epp)
                picintclevel(1 << dev->irq, &dev->irq_state);
            else
                picintc(1 << dev->irq);
        }

        dev->irq_state = 0;
    }
}

void
lpt_set_ext(lpt_t *dev, const uint8_t ext)
{
    if (lpt_ports[dev->id].enabled)
        dev->ext = ext;
}

void
lpt_set_ecp(lpt_t *dev, const uint8_t ecp)
{
    if (lpt_ports[dev->id].enabled)
        dev->ecp = ecp;
}

void
lpt_set_epp(lpt_t *dev, const uint8_t epp)
{
    if (lpt_ports[dev->id].enabled)
        dev->epp = epp;
}

void
lpt_set_lv2(lpt_t *dev, const uint8_t lv2)
{
    if (lpt_ports[dev->id].enabled)
        dev->lv2 = lv2;
}

void
lpt_set_fifo_threshold(lpt_t *dev, const int threshold)
{
    if (lpt_ports[dev->id].enabled)
        fifo_set_trigger_len(dev->fifo, threshold);
}

void
lpt_set_cfg_regs_enabled(lpt_t *dev, const uint8_t cfg_regs_enabled)
{
    if (lpt_ports[dev->id].enabled)
        dev->cfg_regs_enabled = cfg_regs_enabled;
}

void
lpt_set_cnfga_readout(lpt_t *dev, const uint8_t cnfga_readout)
{
    if (lpt_ports[dev->id].enabled)
        dev->cnfga_readout = cnfga_readout;
}

void
lpt_set_cnfgb_readout(lpt_t *dev, const uint8_t cnfgb_readout)
{
    if (lpt_ports[dev->id].enabled)
        dev->cnfgb_readout = (dev->cnfgb_readout & 0xc0) | (cnfgb_readout & 0x3f);
}

void
lpt_port_setup(lpt_t *dev, const uint16_t port)
{
    if (lpt_ports[dev->id].enabled) {
        if ((dev->addr != 0x0000) && (dev->addr != 0xffff)) {
            io_removehandler(dev->addr, 0x0007, lpt_read, NULL, NULL, lpt_write, NULL, NULL, dev);
            io_removehandler(dev->addr + 0x0400, 0x0007, lpt_read, NULL, NULL, lpt_write, NULL, NULL, dev);
        }
        if ((port != 0x0000) && (port != 0xffff)) {
            lpt_log("Set handler: %04X-%04X\n", port, port + 0x0003);
            io_sethandler(port, 0x0003, lpt_read, NULL, NULL, lpt_write, NULL, NULL, dev);
            if (dev->epp)
                io_sethandler(port + 0x0003, 0x0005, lpt_read, NULL, NULL, lpt_write, NULL, NULL, dev);
            if (dev->ecp || dev->lv2) {
                io_sethandler(port + 0x0400, 0x0003, lpt_read, NULL, NULL, lpt_write, NULL, NULL, dev);
                if (dev->epp)
                    io_sethandler(port + 0x0404, 0x0003, lpt_read, NULL, NULL, lpt_write, NULL, NULL, dev);
            }
        }
        dev->addr = port;
    } else
        dev->addr = 0xffff;
}

void
lpt_port_irq(lpt_t *dev, const uint8_t irq)
{
    if (lpt_ports[dev->id].enabled)
        dev->irq = irq;
    else
        dev->irq = 0xff;

    lpt_log("Port %i IRQ = %02X\n", dev->id, irq);
}

void
lpt_port_dma(lpt_t *dev, const uint8_t dma)
{
    if (lpt_ports[dev->id].enabled)
        dev->dma = dma;
    else
        dev->dma = 0xff;

    lpt_log("Port %i DMA = %02X\n", dev->id, dma);
}

void
lpt1_dma(const uint8_t dma)
{
    if (lpt1 != NULL)
        lpt_port_dma(lpt1, dma);
}

void
lpt_port_remove(lpt_t *dev)
{
    if (lpt_ports[dev->id].enabled && (dev->addr != 0xffff)) {
        io_removehandler(dev->addr, 0x0007, lpt_read, NULL, NULL, lpt_write, NULL, NULL, dev);
        io_removehandler(dev->addr + 0x0400, 0x0007, lpt_read, NULL, NULL, lpt_write, NULL, NULL, dev);

        dev->addr = 0xffff;
    }
}

void
lpt1_remove_ams(lpt_t *dev)
{
    if (dev->enabled)
        io_removehandler(dev->addr + 1, 0x0002, lpt_read, NULL, NULL, lpt_write, NULL, NULL, dev);
}

void
lpt_speed_changed(void *priv)
{
    lpt_t *dev = (lpt_t *) priv;

    if (timer_is_enabled(&dev->fifo_out_timer)) {
        timer_disable(&dev->fifo_out_timer);
        timer_set_delay_u64(&dev->fifo_out_timer, (uint64_t) ((1000000.0 / 2500000.0) * (double) TIMER_USEC));
    }
}

void
lpt_port_zero(lpt_t *dev)
{
    lpt_t temp = { 0 };

    temp.irq            = dev->irq;
    temp.id             = dev->id;
    temp.dt             = dev->dt;
    temp.fifo           = dev->fifo;
    temp.fifo_out_timer = dev->fifo_out_timer;
    temp.char_in_timer  = dev->char_in_timer;

    if (lpt_ports[dev->id].enabled)
        lpt_port_remove(dev);

    memset(dev, 0x00, sizeof(lpt_t));

    dev->addr           = 0xffff;
    dev->irq            = temp.irq;
    dev->id             = temp.id;
    dev->dt             = temp.dt;
    dev->fifo           = temp.fifo;
    dev->fifo_out_timer = temp.fifo_out_timer;
    dev->char_in_timer  = temp.char_in_timer;

    if (machine_has_bus(machine, MACHINE_BUS_MCA))
        dev->ext = 1;
}

static void
lpt_close(void *priv)
{
    lpt_t *dev = (lpt_t *) priv;

    if (lpt_ports[dev->id].enabled) {
        fifo_close(dev->fifo);
        dev->fifo       = NULL;

        timer_disable(&dev->fifo_out_timer);
        timer_disable(&dev->char_in_timer);
    }

    if (lpt1 == dev)
        lpt1 = NULL;

    free(dev);
}

static void
lpt_reset(void *priv)
{
    lpt_t *dev = (lpt_t *) priv;

    if (lpt_ports[dev->id].enabled)
        if (timer_is_enabled(&dev->fifo_out_timer))
            timer_disable(&dev->fifo_out_timer);

    lpt_port_zero(dev);

    if (lpt_ports[dev->id].enabled) {
        if (dev->irq_state) {
            if (dev->irq == 0xff)
                dev->irq_state = 0x00;
            else {
                picintclevel(dev->irq, &dev->irq_state);
                picintc(dev->irq);
            }
        }

        dev->enable_irq       = 0x00;
        dev->cfg_regs_enabled = 0;
        dev->ext              = !!(machine_has_bus(machine, MACHINE_BUS_MCA));
        dev->epp              = 0;
        dev->ecp              = 0;
        dev->ecr              = 0x15;
        dev->ret_ecr          = 0x15;
        dev->dat              = 0xff;
        dev->fifo_stat        = 0x00;
        dev->dma_stat         = 0x00;

        memset(dev->ext_regs, 0x00, 8);
        dev->ext_regs[0x02]   = 0x80;
        dev->ext_regs[0x04]   = 0x07;
        dev->cnfga_readout   &= 0xf7;
    }
}

static void *
lpt_init(const device_t *info)
{
    lpt_t *dev = (lpt_t *) calloc(1, sizeof(lpt_t));
    int orig_inst   = next_inst;
    int dev_inst    = device_get_instance();

    const uint16_t default_ports[PARALLEL_MAX] = { LPT1_ADDR, LPT2_ADDR, LPT_MDA_ADDR, LPT4_ADDR };
    const uint8_t  default_irqs[PARALLEL_MAX]  = { LPT1_IRQ, LPT2_IRQ, LPT_MDA_IRQ, LPT4_IRQ };

    if (info->local & 0xFFF00000)
        next_inst = PARALLEL_MAX - 1;
    else if (dev_inst > 0)
        next_inst = dev_inst - 1;

    dev->id = next_inst;

    if (lpt_ports[next_inst].enabled || (info->local & 0xFFF00000)) {
        lpt_log("Adding parallel port %i...\n", next_inst);
        dev->dt                  = &(lpt_devs[next_inst]);
        lpt_ports[next_inst].lpt = dev;

        dev->fifo                = NULL;
        memset(&dev->fifo_out_timer, 0x00, sizeof(pc_timer_t));
        memset(&dev->char_in_timer, 0x00, sizeof(pc_timer_t));

        lpt_port_zero(dev);

        dev->addr             = 0xffff;
        dev->irq              = 0xff;
        if ((jumpered_internal_ecp_dma >= 0) && (jumpered_internal_ecp_dma != 4))
            dev->dma              = jumpered_internal_ecp_dma;
        else
            dev->dma              = 0xff;
        dev->enable_irq       = 0x00;
        dev->cfg_regs_enabled = 0;
        dev->ext              = 0;
        dev->epp              = 0;
        dev->ecp              = 0;
        dev->ecr              = 0x15;
        dev->ret_ecr          = 0x15;
        dev->cnfga_readout    = 0x10;
        dev->cnfgb_readout    = 0x00;

        memset(dev->ext_regs, 0x00, 8);
        dev->ext_regs[0x02]   = 0x80;

        if (lpt_ports[dev->id].enabled) {
            if (info->local & 0xfff00000) {
                lpt_port_setup(dev, info->local >> 20);
                lpt_port_irq(dev, (info->local >> 16) & 0xF);
                next_inst          = orig_inst;
            } else {
                if ((dev->id == 2) && (lpt_3bc_used)) {
                    lpt_port_setup(dev, LPT1_ADDR);
                    lpt_port_irq(dev, LPT1_IRQ);
                } else {
                    lpt_port_setup(dev, default_ports[dev->id]);
                    lpt_port_irq(dev, default_irqs[dev->id]);
                }
                if (dev_inst > 0)
                    next_inst          = orig_inst;
            }

            dev->fifo       = fifo16_init();

            fifo_set_trigger_len(dev->fifo, 8);

            fifo_set_d_ready_evt(dev->fifo, lpt_fifo_d_ready_evt);
            fifo_set_priv(dev->fifo, dev);

            timer_add(&dev->fifo_out_timer, lpt_fifo_out_callback, dev, 0);
            timer_add(&dev->char_in_timer, lpt_char_read_data, dev, 0);
        }
    }

    if (!(info->local & 0xfff00000))
        next_inst++;

    if (lpt1 == NULL)
        lpt1 = dev;

    return dev;
}

void
lpt_set_next_inst(int ni)
{
    next_inst = ni;
}

void
lpt_set_3bc_used(int is_3bc_used)
{
    lpt_3bc_used = is_3bc_used;
}

void
lpt_standalone_init(void)
{
    while (next_inst < (PARALLEL_MAX - 1))
        device_add_inst(&lpt_port_device, next_inst + 1);
}

void
lpt_ports_reset(void)
{
    for (int i = 0; i < PARALLEL_MAX; i++)
        lpt_ports[i].lpt = NULL;
}

const device_t lpt_port_device = {
    .name          = "Parallel Port",
    .internal_name = "lpt",
    .flags         = 0,
    .local         = 0,
    .init          = lpt_init,
    .close         = lpt_close,
    .reset         = lpt_reset,
    .available     = NULL,
    .speed_changed = lpt_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};
