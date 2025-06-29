/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/fifo.h>
#include <86box/timer.h>
#include <86box/dma.h>
#include <86box/lpt.h>
#include <86box/pic.h>
#include <86box/sound.h>
#include <86box/prt_devs.h>
#include <86box/thread.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/network.h>

lpt_port_t lpt_ports[PARALLEL_MAX];

const lpt_device_t lpt_none_device = {
    .name          = "None",
    .internal_name = "none",
    .init          = NULL,
    .close         = NULL,
    .write_data    = NULL,
    .write_ctrl    = NULL,
    .read_status   = NULL,
    .read_ctrl     = NULL
};

static const struct {
    const char         *internal_name;
    const lpt_device_t *device;
} lpt_devices[] = {
  // clang-format off
    {"none",            &lpt_none_device          },
    {"dss",             &dss_device               },
    {"lpt_dac",         &lpt_dac_device           },
    {"lpt_dac_stereo",  &lpt_dac_stereo_device    },
    {"text_prt",        &lpt_prt_text_device      },
    {"dot_matrix",      &lpt_prt_escp_device      },
    {"postscript",      &lpt_prt_ps_device        },
#ifdef USE_PCL
    {"pcl",             &lpt_prt_pcl_device       },
#endif
    {"plip",            &lpt_plip_device          },
    {"dongle_savquest", &lpt_hasp_savquest_device },
    {"",                NULL                      }
  // clang-format on
};

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

const char *
lpt_device_get_name(const int id)
{
    if (strlen(lpt_devices[id].internal_name) == 0)
        return NULL;
    if (lpt_devices[id].device == NULL)
        return "None";
    return lpt_devices[id].device->name;
}

const char *
lpt_device_get_internal_name(const int id)
{
    if (strlen(lpt_devices[id].internal_name) == 0)
        return NULL;
    return lpt_devices[id].internal_name;
}

int
lpt_device_get_from_internal_name(const char *s)
{
    int c = 0;

    while (strlen(lpt_devices[c].internal_name) != 0) {
        if (strcmp(lpt_devices[c].internal_name, s) == 0)
            return c;
        c++;
    }

    return 0;
}

void
lpt_devices_init(void)
{
    for (uint8_t i = 0; i < PARALLEL_MAX; i++) {
        lpt_ports[i].dt = (lpt_device_t *) lpt_devices[lpt_ports[i].device].device;

        if (lpt_ports[i].dt && lpt_ports[i].dt->init)
            lpt_ports[i].priv = lpt_ports[i].dt->init(&lpt_ports[i]);
    }
}

void
lpt_devices_close(void)
{
    for (uint8_t i = 0; i < PARALLEL_MAX; i++) {
        lpt_port_t *dev = &lpt_ports[i];

        if (lpt_ports[i].dt && lpt_ports[i].dt->close)
            dev->dt->close(dev->priv);

        dev->dt = NULL;
    }
}

static uint8_t
lpt_get_ctrl_raw(const lpt_port_t *dev)
{
    uint8_t ret;

    if (dev->dt && dev->dt->read_ctrl && dev->priv)
        ret = (dev->dt->read_ctrl(dev->priv) & 0xef) | dev->enable_irq;
    else
        ret = 0xc0 | dev->ctrl | dev->enable_irq;

    return ret & 0xdf;
}

static uint8_t
lpt_is_epp(const lpt_port_t *dev)
{
    return (dev->epp || ((dev->ecp) && ((dev->ecr & 0xe0) == 0x80)));
}

static uint8_t
lpt_get_ctrl(const lpt_port_t *dev)
{
    uint8_t ret = lpt_get_ctrl_raw(dev);

    if (!dev->ecp && !dev->epp)
        ret |= 0x20;

    return ret;
}

static void
lpt_write_fifo(lpt_port_t *dev, const uint8_t val, const uint8_t tag)
{
    if (!fifo_get_full(dev->fifo)) {
        fifo_write_evt_tagged(tag, val, dev->fifo);

        if (!timer_is_enabled(&dev->fifo_out_timer))
            timer_set_delay_u64(&dev->fifo_out_timer, (uint64_t) ((1000000.0 / 2500000.0) * (double) TIMER_USEC));
    }
}

static void
lpt_ecp_update_irq(lpt_port_t *dev)
{
    if (!(dev->ecr & 0x04) && ((dev->fifo_stat | dev->dma_stat) & 0x04))
        picintlevel(1 << dev->irq, &dev->irq_state);
    else
        picintclevel(1 << dev->irq, &dev->irq_state);
}

static void
lpt_autofeed(lpt_port_t *dev, const uint8_t val)
{
    if (dev->dt && dev->dt->autofeed && dev->priv)
        dev->dt->autofeed(val, dev->priv);

    dev->autofeed = val;
}

static void
lpt_strobe(lpt_port_t *dev, const uint8_t val)
{
    if (dev->dt && dev->dt->strobe && dev->priv)
        dev->dt->strobe(dev->strobe, val, dev->priv);

    dev->strobe = val;
}

static void
lpt_fifo_out_callback(void *priv)
{
    lpt_port_t *dev = (lpt_port_t *) priv;

    switch (dev->state) {
        default:
            break;

        case LPT_STATE_READ_DMA:
            ;
            int ret = 0xff;

            if (dev->dma == 0xff)
                ret = DMA_NODATA;
            else
                ret = dma_channel_read(dev->dma);

            lpt_log("DMA %02X: %08X\n", dev->dma, ret);

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
                    if (dev->dt && dev->dt->write_data && dev->priv)
                        dev->dt->write_data(val, dev->priv);

                    lpt_strobe(dev, 1);
                    lpt_strobe(dev, 0);
                }
            }

            if (dev->ecr & 0x08) {
                if (fifo_get_empty(dev->fifo)) {
                    if (dev->dma_stat) {
                        /* Now actually set the external flag. */
                        dev->dma_stat = 0x04;
                        dev->state = LPT_STATE_IDLE;
                        lpt_ecp_update_irq(dev);
                        lpt_autofeed(dev, 0);
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
            else
                lpt_autofeed(dev, 0);
            break;
    }
}

void
lpt_write(const uint16_t port, const uint8_t val, void *priv)
{
    lpt_port_t *dev  = (lpt_port_t *) priv;
    uint16_t    mask = 0x0407;

    lpt_log("[W] %04X = %02X\n", port, val);

    /* This is needed so the parallel port at 3BC works. */
    if (dev->addr & 0x0004)
        mask = 0x0403;

    switch (port & mask) {
        case 0x0000:
            if (dev->ecp) {
                if ((dev->ecr & 0xe0) == 0x60)
                    /* AFIFO */
                    lpt_write_fifo(dev, val, 0x00);
                else if (!(dev->ecr & 0xc0) && (!(dev->ecr & 0x20) || !(lpt_get_ctrl_raw(dev) & 0x20)) &&
                           dev->dt && dev->dt->write_data && dev->priv)
                    /* DATAR */
                    dev->dt->write_data(val, dev->priv);
                dev->dat = val;
            } else {
                /* DTR */
                if ((!dev->ext || !(lpt_get_ctrl_raw(dev) & 0x20)) && dev->dt &&
                    dev->dt->write_data && dev->priv)
                    dev->dt->write_data(val, dev->priv);
                dev->dat = val;
            }
            break;

        case 0x0001:
            break;

        case 0x0002:
            if (dev->dt && dev->dt->write_ctrl && dev->priv) {
                if (dev->ecp)
                    dev->dt->write_ctrl((val & 0xfc) | dev->autofeed | dev->strobe, dev->priv);
                else
                    dev->dt->write_ctrl(val, dev->priv);
            }
            dev->ctrl       = val;
            dev->enable_irq = val & 0x10;
            if (!(val & 0x10) && (dev->irq != 0xff))
                picintc(1 << dev->irq);
            dev->irq_state  = 0;
            break;

        case 0x0003:
            if (lpt_is_epp(dev)) {
                if (dev->dt && dev->dt->epp_write_data && dev->priv)
                    dev->dt->epp_write_data(1, val, dev->priv);
            }
            break;

        case 0x0004 ... 0x0007:
            if (lpt_is_epp(dev)) {
                if (dev->dt && dev->dt->epp_write_data && dev->priv)
                    dev->dt->epp_write_data(0, val, dev->priv);
            }
            break;

        case 0x0400: case 0x0404:
            switch (dev->ecr >> 5) {
                default:
                    break;
                case 2:
                    lpt_write_fifo(dev, val, 0x01);
                    break;
                case 3:
                    if (!(lpt_get_ctrl_raw(dev) & 0x20))
                        lpt_write_fifo(dev, val, 0x01);
                    break;
                case 6:
                    /* TFIFO */
                    if (!fifo_get_full(dev->fifo))
                        fifo_write_evt(val, dev->fifo);
                    break;
            }
            break;

        case 0x0402: case 0x0406:
            if (!(val & 0x0c))
                lpt_autofeed(dev, 0x00);
            else
                lpt_autofeed(dev, 0x02);

            if ((dev->ecr & 0x04) && !(val & 0x04)) {
                dev->dma_stat  = 0x00;
                fifo_reset(dev->fifo);
                if (val & 0x08) {
                    dev->state = LPT_STATE_READ_DMA;
                    dev->fifo_stat = 0x00;
                    if (!timer_is_enabled(&dev->fifo_out_timer))
                        timer_set_delay_u64(&dev->fifo_out_timer, (uint64_t) ((1000000.0 / 2500000.0) * (double) TIMER_USEC));
                } else {
                    dev->state = LPT_STATE_WRITE_FIFO;
                    if (lpt_get_ctrl_raw(dev) & 0x20)
                        dev->fifo_stat = fifo_get_ready(dev->fifo) ? 0x04 : 0x00;
                    else
                        dev->fifo_stat = fifo_get_ready(dev->fifo) ? 0x00 : 0x04;
                }
            } else if ((val & 0x04) && !(dev->ecr & 0x04)) {
                if (timer_is_enabled(&dev->fifo_out_timer))
                    timer_disable(&dev->fifo_out_timer);

                dev->state = LPT_STATE_IDLE;
            }
            dev->ecr        = val;
            lpt_ecp_update_irq(dev);
            break;

        default:
            break;
    }
}

static void
lpt_fifo_d_ready_evt(void *priv)
{
    lpt_port_t *dev = (lpt_port_t *) priv;

    if (!(dev->ecr & 0x08)) {
        if (lpt_get_ctrl_raw(dev) & 0x20)
            dev->fifo_stat = fifo_get_ready(dev->fifo) ? 0x04 : 0x00;
        else
            dev->fifo_stat = fifo_get_ready(dev->fifo) ? 0x00 : 0x04;
    }

    lpt_ecp_update_irq(dev);
}

void
lpt_write_to_fifo(void *priv, const uint8_t val)
{
    lpt_port_t *dev = (lpt_port_t *) priv;

    if (dev->ecp) {
        if (((dev->ecr & 0xe0) == 0x20) && (lpt_get_ctrl_raw(dev) & 0x20))
            dev->dat = val;
        else if (((dev->ecr & 0xe0) == 0x60) && (lpt_get_ctrl_raw(dev) & 0x20) &&
            !fifo_get_full(dev->fifo))
            fifo_write_evt_tagged(0x01, val, dev->fifo);

        if (((dev->ecr & 0x0c) == 0x08) && (dev->dma != 0xff)) {
            const int ret = dma_channel_write(dev->dma, val);

            if (ret & DMA_OVER)
                dev->dma_stat |= 0x04;
        }
    } else {
        if (dev->ext && (lpt_get_ctrl_raw(dev) & 0x20))
            dev->dat = val;
    }
}

void
lpt_write_to_dat(void *priv, const uint8_t val)
{
    lpt_port_t *dev = (lpt_port_t *) priv;

    dev->dat = val;
}

static uint8_t
lpt_read_fifo(const lpt_port_t *dev)
{
    uint8_t ret = 0xff;

    if (!fifo_get_empty(dev->fifo))
        ret = fifo_read(dev->fifo);

    return ret;
}

uint8_t
lpt_read_status(const int port)
{
    lpt_port_t *dev      = &lpt_ports[port];
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

    if (dev->dt && dev->dt->read_status && dev->priv)
        ret = (dev->dt->read_status(dev->priv) & 0xf8) | low_bits;
    else
        ret = 0xd8 | low_bits;

    return ret;
}

uint8_t
lpt_read(const uint16_t port, void *priv)
{
    const lpt_port_t *dev      = (lpt_port_t *) priv;
    uint16_t          mask     = 0x0407;
    uint8_t           ret      = 0xff;

    /* This is needed so the parallel port at 3BC works. */
    if (dev->addr & 0x0004)
        mask = 0x0403;

    switch (port & mask) {
        case 0x0000:
            if (dev->ecp) {
                if (!(dev->ecr & 0xc0))
                    ret = dev->dat;
            } else {
                /* DTR */
                ret = dev->dat;
            }
            break;

        case 0x0001:
            ret = lpt_read_status(dev->id);
            break;

        case 0x0002:
            ret = lpt_get_ctrl(dev);
            if (dev->ecp)
                ret = (ret & 0xfc) | (dev->ctrl & 0x03);
            break;

        case 0x0003:
            if (lpt_is_epp(dev)) {
                if (dev->dt && dev->dt->epp_request_read && dev->priv)
                    dev->dt->epp_request_read(1, dev->priv);
                ret = dev->dat;
            }
            break;

        case 0x0004 ... 0x0007:
            if (lpt_is_epp(dev)) {
                if (dev->dt && dev->dt->epp_request_read && dev->priv)
                    dev->dt->epp_request_read(0, dev->priv);
                ret = dev->dat;
            }
            break;

        case 0x0400: case 0x0404:
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
                    ret = 0x14;
                    break;
            }
            break;

        case 0x0401: case 0x0405:
            if ((dev->ecr & 0xe0) == 0xe0) {
                /* CNFGB */
                ret = 0x08;
                ret |= (dev->irq_state ? 0x40 : 0x00);
                ret |= ((dev->irq == 0x05) ?  0x30 : 0x00);
                if ((dev->dma >= 1) && (dev->dma <= 3))
                    ret |= dev->dma;
            }
            break;

        case 0x0402: case 0x0406:
            ret = dev->ecr | dev->fifo_stat | (dev->dma_stat & 0x04);
            ret |= (fifo_get_full(dev->fifo) ? 0x02 : 0x00);
            ret |= (fifo_get_empty(dev->fifo) ? 0x01 : 0x00);
            break;

        default:
            break;
    }

    lpt_log("[R] %04X = %02X\n", port, ret);

    return ret;
}

uint8_t
lpt_read_port(const int port, const uint16_t reg)
{
    lpt_port_t *dev = &(lpt_ports[port]);

    return lpt_read(reg, dev);
}

void
lpt_irq(void *priv, const int raise)
{
    lpt_port_t *dev = (lpt_port_t *) priv;

    if (dev->enable_irq) {
        if (dev->irq != 0xff) {
            if (dev->ext) {
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

        if (!dev->ext || (dev->irq == 0xff))
            dev->irq_state = raise;
    } else {
        if (dev->irq != 0xff) {
            if (dev->ext)
                picintclevel(1 << dev->irq, &dev->irq_state);
            else
                picintc(1 << dev->irq);
        }

        dev->irq_state = 0;
    }
}

void
lpt_set_ext(const int port, const uint8_t ext)
{
    if (lpt_ports[port].enabled)
        lpt_ports[port].ext = ext;
}

void
lpt_set_ecp(const int port, const uint8_t ecp)
{
    if (lpt_ports[port].enabled) {
        const uint16_t addr = lpt_ports[port].addr;
        lpt_port_setup(port, 0xfff);
        lpt_ports[port].ecp = ecp;
        lpt_port_setup(port, addr);
    }
}

void
lpt_set_epp(const int port, const uint8_t epp)
{
    if (lpt_ports[port].enabled) {
        const uint16_t addr = lpt_ports[port].addr;
        lpt_port_setup(port, 0xfff);
        lpt_ports[port].epp = epp;
        lpt_port_setup(port, addr);
    }
}

void
lpt_set_lv2(const int port, const uint8_t lv2)
{
    if (lpt_ports[port].enabled) {
        const uint16_t addr = lpt_ports[port].addr;
        lpt_port_setup(port, 0xfff);
        lpt_ports[port].lv2 = lv2;
        lpt_port_setup(port, addr);
    }
}

void
lpt_close(void)
{
    for (uint8_t i = 0; i < PARALLEL_MAX; i++) {
        if (lpt_ports[i].enabled) {
            fifo_close(lpt_ports[i].fifo);
            lpt_ports[i].fifo       = NULL;

            timer_disable(&lpt_ports[i].fifo_out_timer);
        }
    }
}

void
lpt_port_zero(lpt_port_t *dev)
{
    lpt_port_t temp = { 0 };

    temp.irq            = dev->irq;
    temp.id             = dev->id;
    temp.device         = dev->device;
    temp.dt             = dev->dt;
    temp.priv           = dev->priv;
    temp.enabled        = dev->enabled;
    temp.fifo           = dev->fifo;
    temp.fifo_out_timer = dev->fifo_out_timer;

    if (dev->enabled)
        lpt_port_remove(dev->id);

    memset(dev, 0x00, sizeof(lpt_port_t));

    dev->addr           = 0xffff;
    dev->irq            = temp.irq;
    dev->id             = temp.id;
    dev->device         = temp.device;
    dev->dt             = temp.dt;
    dev->priv           = temp.priv;
    dev->enabled        = temp.enabled;
    dev->fifo           = temp.fifo;
    dev->fifo_out_timer = temp.fifo_out_timer;

    if (machine_has_bus(machine, MACHINE_BUS_MCA))
        dev->ext = 1;
}

void
lpt_reset(void)
{
    for (uint8_t i = 0; i < PARALLEL_MAX; i++) {
        if (lpt_ports[i].enabled)
            if (timer_is_enabled(&lpt_ports[i].fifo_out_timer))
                timer_disable(&lpt_ports[i].fifo_out_timer);

        lpt_port_zero(&(lpt_ports[i]));

        if (lpt_ports[i].enabled) {
            if (lpt_ports[i].irq_state) {
                if (lpt_ports[i].irq == 0xff)
                    lpt_ports[i].irq_state = 0x00;
                else {
                    picintclevel(lpt_ports[i].irq, &lpt_ports[i].irq_state);
                    picintc(lpt_ports[i].irq);
                }
            }

            lpt_ports[i].enable_irq = 0x00;
            lpt_ports[i].ext        = !!(machine_has_bus(machine, MACHINE_BUS_MCA));
            lpt_ports[i].epp        = 0;
            lpt_ports[i].ecp        = 0;
            lpt_ports[i].ecr        = 0x15;
            lpt_ports[i].dat        = 0xff;
            lpt_ports[i].fifo_stat  = 0x00;
            lpt_ports[i].dma_stat   = 0x00;
        }
    }
}

void
lpt_init(void)
{
    const uint16_t default_ports[PARALLEL_MAX] = { LPT1_ADDR, LPT2_ADDR, LPT_MDA_ADDR, LPT4_ADDR };
    const uint8_t  default_irqs[PARALLEL_MAX]  = { LPT1_IRQ, LPT2_IRQ, LPT_MDA_IRQ, LPT4_IRQ };

    for (uint8_t i = 0; i < PARALLEL_MAX; i++) {
        lpt_ports[i].id         = i;
        lpt_ports[i].dt         = NULL;
        lpt_ports[i].priv       = NULL;
        lpt_ports[i].fifo       = NULL;
        memset(&lpt_ports[i].fifo_out_timer, 0x00, sizeof(pc_timer_t));

        lpt_port_zero(&(lpt_ports[i]));

        lpt_ports[i].addr       = 0xffff;
        lpt_ports[i].irq        = 0xff;
        lpt_ports[i].dma        = 0xff;
        lpt_ports[i].enable_irq = 0x00;
        lpt_ports[i].ext        = 0;
        lpt_ports[i].epp        = 0;
        lpt_ports[i].ecp        = 0;
        lpt_ports[i].ecr        = 0x15;

        if (lpt_ports[i].enabled) {
            lpt_port_setup(i, default_ports[i]);
            lpt_port_irq(i, default_irqs[i]);

            lpt_ports[i].fifo       = fifo16_init();

            fifo_set_trigger_len(lpt_ports[i].fifo, 8);

            fifo_set_d_ready_evt(lpt_ports[i].fifo, lpt_fifo_d_ready_evt);
            fifo_set_priv(lpt_ports[i].fifo, &lpt_ports[i]);

            timer_add(&lpt_ports[i].fifo_out_timer, lpt_fifo_out_callback, &lpt_ports[i], 0);
        }
    }
}

void
lpt_port_setup(const int i, const uint16_t port)
{
    if (lpt_ports[i].enabled) {
        if (lpt_ports[i].addr != 0xffff) {
            io_removehandler(lpt_ports[i].addr, 0x0007, lpt_read, NULL, NULL, lpt_write, NULL, NULL, &lpt_ports[i]);
            io_removehandler(lpt_ports[i].addr + 0x0400, 0x0007, lpt_read, NULL, NULL, lpt_write, NULL, NULL, &lpt_ports[i]);
        }
        if (port != 0xffff) {
            lpt_log("Set handler: %04X-%04X\n", port, port + 0x0003);
            io_sethandler(port, 0x0003, lpt_read, NULL, NULL, lpt_write, NULL, NULL, &lpt_ports[i]);
            if (lpt_ports[i].epp)
                io_sethandler(lpt_ports[i].addr + 0x0003, 0x0003, lpt_read, NULL, NULL, lpt_write, NULL, NULL, &lpt_ports[i]);
            if (lpt_ports[i].ecp || lpt_ports[i].lv2) {
                io_sethandler(port + 0x0400, 0x0003, lpt_read, NULL, NULL, lpt_write, NULL, NULL, &lpt_ports[i]);
                if (lpt_ports[i].epp)
                    io_sethandler(lpt_ports[i].addr + 0x0403, 0x0003, lpt_read, NULL, NULL, lpt_write, NULL, NULL, &lpt_ports[i]);
            }
        }
        lpt_ports[i].addr = port;
    } else
        lpt_ports[i].addr = 0xffff;
}

void
lpt_port_irq(const int i, const uint8_t irq)
{
    if (lpt_ports[i].enabled)
        lpt_ports[i].irq = irq;
    else
        lpt_ports[i].irq = 0xff;

    lpt_log("Port %i IRQ = %02X\n", i, irq);
}

void
lpt_port_dma(const int i, const uint8_t dma)
{
    if (lpt_ports[i].enabled)
        lpt_ports[i].dma = dma;
    else
        lpt_ports[i].dma = 0xff;

    lpt_log("Port %i DMA = %02X\n", i, dma);
}

void
lpt_port_remove(const int i)
{
    if (lpt_ports[i].enabled && (lpt_ports[i].addr != 0xffff)) {
        io_removehandler(lpt_ports[i].addr, 0x0007, lpt_read, NULL, NULL, lpt_write, NULL, NULL, &lpt_ports[i]);
        io_removehandler(lpt_ports[i].addr + 0x0400, 0x0007, lpt_read, NULL, NULL, lpt_write, NULL, NULL, &lpt_ports[i]);

        lpt_ports[i].addr = 0xffff;
    }
}

void
lpt1_remove_ams(void)
{
    if (lpt_ports[0].enabled)
        io_removehandler(lpt_ports[0].addr + 1, 0x0002, lpt_read, NULL, NULL, lpt_write, NULL, NULL, &lpt_ports[0]);
}
