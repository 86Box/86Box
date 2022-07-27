/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/lpt.h>
#include <86box/pic.h>
#include <86box/sound.h>
#include <86box/prt_devs.h>
#include <86box/net_plip.h>

lpt_port_t lpt_ports[PARALLEL_MAX];

const lpt_device_t lpt_none_device = {
    .name          = "None",
    .internal_name = "none",
    .init          = NULL,
    .close         = NULL,
    .write_data    = NULL,
    .write_ctrl    = NULL,
    .read_data     = NULL,
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
    {"plip",            &lpt_plip_device          },
    {"dongle_savquest",	&lpt_hasp_savquest_device },
    {"",                NULL                      }
// clang-format on
};

char *
lpt_device_get_name(int id)
{
    if (strlen((char *) lpt_devices[id].internal_name) == 0)
        return NULL;
    if (!lpt_devices[id].device)
        return "None";
    return (char *) lpt_devices[id].device->name;
}

char *
lpt_device_get_internal_name(int id)
{
    if (strlen((char *) lpt_devices[id].internal_name) == 0)
        return NULL;
    return (char *) lpt_devices[id].internal_name;
}

int
lpt_device_get_from_internal_name(char *s)
{
    int c = 0;

    while (strlen((char *) lpt_devices[c].internal_name) != 0) {
        if (strcmp(lpt_devices[c].internal_name, s) == 0)
            return c;
        c++;
    }

    return 0;
}

void
lpt_devices_init(void)
{
    int i = 0;

    for (i = 0; i < PARALLEL_MAX; i++) {
        lpt_ports[i].dt = (lpt_device_t *) lpt_devices[lpt_ports[i].device].device;

        if (lpt_ports[i].dt && lpt_ports[i].dt->init)
            lpt_ports[i].priv = lpt_ports[i].dt->init(&lpt_ports[i]);
    }
}

void
lpt_devices_close(void)
{
    int         i = 0;
    lpt_port_t *dev;

    for (i = 0; i < PARALLEL_MAX; i++) {
        dev = &lpt_ports[i];

        if (lpt_ports[i].dt && lpt_ports[i].dt->close)
            dev->dt->close(dev->priv);

        dev->dt = NULL;
    }
}

void
lpt_write(uint16_t port, uint8_t val, void *priv)
{
    lpt_port_t *dev = (lpt_port_t *) priv;

    switch (port & 3) {
        case 0:
            if (dev->dt && dev->dt->write_data && dev->priv)
                dev->dt->write_data(val, dev->priv);
            dev->dat = val;
            break;

        case 1:
            break;

        case 2:
            if (dev->dt && dev->dt->write_ctrl && dev->priv)
                dev->dt->write_ctrl(val, dev->priv);
            dev->ctrl       = val;
            dev->enable_irq = val & 0x10;
            break;
    }
}

uint8_t
lpt_read(uint16_t port, void *priv)
{
    uint8_t     ret = 0xff;
    lpt_port_t *dev = (lpt_port_t *) priv;

    switch (port & 3) {
        case 0:
            if (dev->dt && dev->dt->read_data && dev->priv)
                ret = dev->dt->read_data(dev->priv);
            else
                ret = dev->dat;
            break;

        case 1:
            if (dev->dt && dev->dt->read_status && dev->priv)
                ret = dev->dt->read_status(dev->priv) | 0x07;
            else
                ret = 0xdf;
            break;

        case 2:
            if (dev->dt && dev->dt->read_ctrl && dev->priv)
                ret = (dev->dt->read_ctrl(dev->priv) & 0xef) | dev->enable_irq;
            else
                ret = 0xe0 | dev->ctrl | dev->enable_irq;
            break;
    }

    return ret;
}

void
lpt_irq(void *priv, int raise)
{
    lpt_port_t *dev = (lpt_port_t *) priv;

    if (dev->enable_irq && (dev->irq != 0xff)) {
        if (raise)
            picint(1 << dev->irq);
        else
            picintc(1 << dev->irq);
    }
}

void
lpt_init(void)
{
    int      i;
    uint16_t default_ports[PARALLEL_MAX] = { LPT1_ADDR, LPT2_ADDR, LPT_MDA_ADDR, LPT4_ADDR };
    uint8_t  default_irqs[PARALLEL_MAX]  = { LPT1_IRQ, LPT2_IRQ, LPT_MDA_IRQ, LPT4_IRQ };

    for (i = 0; i < PARALLEL_MAX; i++) {
        lpt_ports[i].addr       = 0xffff;
        lpt_ports[i].irq        = 0xff;
        lpt_ports[i].enable_irq = 0x10;

        if (lpt_ports[i].enabled) {
            lpt_port_init(i, default_ports[i]);
            lpt_port_irq(i, default_irqs[i]);
        }
    }
}

void
lpt_port_init(int i, uint16_t port)
{
    if (lpt_ports[i].enabled) {
        if (lpt_ports[i].addr != 0xffff)
            io_removehandler(lpt_ports[i].addr, 0x0003, lpt_read, NULL, NULL, lpt_write, NULL, NULL, &lpt_ports[i]);
        if (port != 0xffff)
            io_sethandler(port, 0x0003, lpt_read, NULL, NULL, lpt_write, NULL, NULL, &lpt_ports[i]);
        lpt_ports[i].addr = port;
    } else
        lpt_ports[i].addr = 0xffff;
}

void
lpt_port_irq(int i, uint8_t irq)
{
    if (lpt_ports[i].enabled)
        lpt_ports[i].irq = irq;
    else
        lpt_ports[i].irq = 0xff;
}

void
lpt_port_remove(int i)
{
    if (lpt_ports[i].enabled && (lpt_ports[i].addr != 0xffff)) {
        io_removehandler(lpt_ports[i].addr, 0x0003, lpt_read, NULL, NULL, lpt_write, NULL, NULL, &lpt_ports[i]);
        lpt_ports[i].addr = 0xffff;
    }
}

void
lpt1_remove_ams(void)
{
    if (lpt_ports[0].enabled)
        io_removehandler(lpt_ports[0].addr + 1, 0x0002, lpt_read, NULL, NULL, lpt_write, NULL, NULL, &lpt_ports[0]);
}
