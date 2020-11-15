/*um8669f :
        
  aa to 108 unlocks
  next 108 write is register select (Cx?)
  data read/write to 109
  55 to 108 locks
  
C1
bit 7 - enable PnP registers

PnP registers :

07 - device :
        0 = FDC
        1 = COM1
        2 = COM2
        3 = LPT1
        5 = Game port
30 - enable
60/61 - addr
70 - IRQ
74 - DMA*/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/pci.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/sio.h>


#define DEV_FDC  0
#define DEV_COM1 1
#define DEV_COM2 2
#define DEV_LPT1 3
#define DEV_GAME 5

#define REG_DEVICE 0x07
#define REG_ENABLE 0x30
#define REG_ADDRHI 0x60
#define REG_ADDRLO 0x61
#define REG_IRQ    0x70
#define REG_DMA    0x74


typedef struct um8669f_t
{
    int locked, cur_reg_108,
	cur_reg, cur_device,
	pnp_active;

    uint8_t regs_108[256];
        
    struct {
	int enable;
	uint16_t addr;
	int irq;
	int dma;
    } dev[8];

    fdc_t *fdc;
    serial_t *uart[2];
} um8669f_t;


static void
um8669f_pnp_write(uint16_t port, uint8_t val, void *priv)
{
    um8669f_t *dev = (um8669f_t *) priv;

    uint8_t valxor = 0;
    uint8_t lpt_irq = 0xff;

    if (port == 0x279)
	dev->cur_reg = val;
    else {
	if (dev->cur_reg == REG_DEVICE)
		dev->cur_device = val & 7;
	else {
		switch (dev->cur_reg) {
			case REG_ENABLE:
				valxor = dev->dev[dev->cur_device].enable ^ val;
				dev->dev[dev->cur_device].enable = val;
				break;
			case REG_ADDRLO:
				valxor = (dev->dev[dev->cur_device].addr & 0xff) ^ val;
				dev->dev[dev->cur_device].addr = (dev->dev[dev->cur_device].addr & 0xff00) | val;
				break;
			case REG_ADDRHI:
				valxor = ((dev->dev[dev->cur_device].addr >> 8) & 0xff) ^ val;
				dev->dev[dev->cur_device].addr = (dev->dev[dev->cur_device].addr & 0x00ff) | (val << 8);
				break;
			case REG_IRQ:
				valxor = dev->dev[dev->cur_device].irq ^ val;
				dev->dev[dev->cur_device].irq = val;
				break;
			case REG_DMA:
				valxor = dev->dev[dev->cur_device].dma ^ val;
				dev->dev[dev->cur_device].dma = val;
				break;
			default:
				valxor = 0;
				break;
		}

		switch (dev->cur_device) {
			case DEV_FDC:
				if ((dev->cur_reg == REG_ENABLE) && valxor) {
					fdc_remove(dev->fdc);
					if (dev->dev[DEV_FDC].enable & 1)
						fdc_set_base(dev->fdc, 0x03f0);
				}
				break;
			case DEV_COM1:
				if ((dev->cur_reg == REG_ENABLE) && valxor) {
	                                serial_remove(dev->uart[0]);
					if (dev->dev[DEV_COM1].enable & 1)
						serial_setup(dev->uart[0], dev->dev[DEV_COM1].addr, dev->dev[DEV_COM1].irq);
				}
				break;
			case DEV_COM2:
				if ((dev->cur_reg == REG_ENABLE) && valxor) {
					serial_remove(dev->uart[1]);
					if (dev->dev[DEV_COM2].enable & 1)
						serial_setup(dev->uart[1], dev->dev[DEV_COM2].addr, dev->dev[DEV_COM2].irq);
				}
				break;
			case DEV_LPT1:
				if ((dev->cur_reg == REG_ENABLE) && valxor) {
					lpt1_remove();
					if (dev->dev[DEV_LPT1].enable & 1)
						lpt1_init(dev->dev[DEV_LPT1].addr);
				}
				if (dev->dev[DEV_LPT1].irq <= 15)
					lpt_irq = dev->dev[DEV_LPT1].irq;
				lpt1_irq(lpt_irq);
				break;
		}
	}
    }
}


static uint8_t
um8669f_pnp_read(uint16_t port, void *priv)
{
    um8669f_t *dev = (um8669f_t *) priv;
    uint8_t ret = 0xff;

    switch (dev->cur_reg) {
	case REG_DEVICE:
		ret = dev->cur_device;
		break;
	case REG_ENABLE:
		ret = dev->dev[dev->cur_device].enable;
		break;
	case REG_ADDRLO:
		ret = dev->dev[dev->cur_device].addr & 0xff;
		break;
	case REG_ADDRHI:
		ret = dev->dev[dev->cur_device].addr >> 8;
		break;
	case REG_IRQ:
		ret = dev->dev[dev->cur_device].irq;
		break;
	case REG_DMA:
		ret = dev->dev[dev->cur_device].dma;
		break;
    }

    return ret;
}


void
um8669f_write(uint16_t port, uint8_t val, void *priv)
{
    um8669f_t *dev = (um8669f_t *) priv;
    int new_pnp_active;

    if (dev->locked) {
	if ((port == 0x108) && (val == 0xaa))
		dev->locked = 0;
    } else {
	if (port == 0x108) {
		if (val == 0x55)
			dev->locked = 1;
		else
			dev->cur_reg_108 = val;
	} else {
		dev->regs_108[dev->cur_reg_108] = val;

		if (dev->cur_reg_108 == 0xc1) {
			new_pnp_active = !!(dev->regs_108[0xc1] & 0x80);
			if (new_pnp_active != dev->pnp_active) {
				if (new_pnp_active) {
					io_sethandler(0x0279, 0x0001,
						      NULL, NULL, NULL, um8669f_pnp_write, NULL, NULL, dev);
					io_sethandler(0x0a79, 0x0001,
						      NULL, NULL, NULL, um8669f_pnp_write, NULL, NULL, dev);
					io_sethandler(0x03e3, 0x0001,
						      um8669f_pnp_read, NULL, NULL, NULL, NULL, NULL, dev);
				} else {
					io_removehandler(0x0279, 0x0001,
							 NULL, NULL, NULL, um8669f_pnp_write, NULL, NULL, dev);
					io_removehandler(0x0a79, 0x0001,
							 NULL, NULL, NULL, um8669f_pnp_write, NULL, NULL, dev);
					io_removehandler(0x03e3, 0x0001,
							 um8669f_pnp_read, NULL, NULL, NULL, NULL, NULL, dev);
				}
				dev->pnp_active = new_pnp_active;
			}
		}
	}
    }
}


uint8_t
um8669f_read(uint16_t port, void *priv)
{
    um8669f_t *dev = (um8669f_t *) priv;
    uint8_t ret = 0xff;

    if (!dev->locked) {
	if (port == 0x108)
		ret = dev->cur_reg_108;	/* ??? */
	else
		ret = dev->regs_108[dev->cur_reg_108];
    }

    return ret;
}


void
um8669f_reset(um8669f_t *dev)
{
    fdc_reset(dev->fdc);

    serial_remove(dev->uart[0]);
    serial_setup(dev->uart[0], SERIAL1_ADDR, SERIAL1_IRQ);

    serial_remove(dev->uart[1]);
    serial_setup(dev->uart[1], SERIAL2_ADDR, SERIAL2_IRQ);

    lpt1_remove();
    lpt1_init(0x378);

    if (dev->pnp_active) {
	io_removehandler(0x0279, 0x0001, NULL, NULL, NULL, um8669f_pnp_write, NULL, NULL, dev);
	io_removehandler(0x0a79, 0x0001, NULL, NULL, NULL, um8669f_pnp_write, NULL, NULL, dev);
	io_removehandler(0x03e3, 0x0001, um8669f_pnp_read, NULL, NULL, NULL, NULL, NULL, dev);
	dev->pnp_active = 0;
    }

    dev->locked = 1;

    dev->dev[DEV_FDC].enable = 1;
    dev->dev[DEV_FDC].addr = 0x03f0;
    dev->dev[DEV_FDC].irq = 6;
    dev->dev[DEV_FDC].dma = 2;

    dev->dev[DEV_COM1].enable = 1;
    dev->dev[DEV_COM1].addr = 0x03f8;
    dev->dev[DEV_COM1].irq = 4;

    dev->dev[DEV_COM2].enable = 1;
    dev->dev[DEV_COM2].addr = 0x02f8;
    dev->dev[DEV_COM2].irq = 3;

    dev->dev[DEV_LPT1].enable = 1;
    dev->dev[DEV_LPT1].addr = 0x0378;
    dev->dev[DEV_LPT1].irq = 7;
}


static void
um8669f_close(void *priv)
{
    um8669f_t *dev = (um8669f_t *) priv;

    free(dev);
}


static void *
um8669f_init(const device_t *info)
{
    um8669f_t *dev = (um8669f_t *) malloc(sizeof(um8669f_t));
    memset(dev, 0, sizeof(um8669f_t));

    dev->fdc = device_add(&fdc_at_smc_device);

    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    io_sethandler(0x0108, 0x0002,
		  um8669f_read, NULL, NULL, um8669f_write, NULL, NULL, dev);

    um8669f_reset(dev);

    return dev;
}


const device_t um8669f_device = {
    "UMC UM8669F Super I/O",
    0,
    0,
    um8669f_init, um8669f_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};
