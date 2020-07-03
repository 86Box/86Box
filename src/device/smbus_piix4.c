/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of a generic PIIX4-compatible SMBus host controller.
 *
 *
 *
 * Authors:	RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2020 RichardG.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/smbus.h>
#include <86box/smbus_piix4.h>


#ifdef ENABLE_SMBUS_PIIX4_LOG
int smbus_piix4_do_log = ENABLE_SMBUS_PIIX4_LOG;


static void
smbus_piix4_log(const char *fmt, ...)
{
    va_list ap;

    if (smbus_piix4_do_log) {
    	va_start(ap, fmt);
    	pclog_ex(fmt, ap);
    	va_end(ap);
    }
}
#else
#define smbus_piix4_log(fmt, ...)
#endif


static uint8_t
smbus_piix4_read(uint16_t addr, void *priv)
{
    smbus_piix4_t *dev = (smbus_piix4_t *) priv;
    uint8_t ret = 0x00;

    switch (addr - dev->io_base) {
    	case 0x00:
    		ret = dev->stat;
    		break;
    	case 0x02:
    		dev->index = 0; /* reading from this resets the block data index */
    		ret = dev->ctl;
    		break;
    	case 0x03:
    		ret = dev->cmd;
    		break;
    	case 0x04:
    		ret = dev->addr;
    		break;
    	case 0x05:
    		ret = dev->data0;
    		break;
    	case 0x06:
    		ret = dev->data1;
    		break;
    	case 0x07:
    		ret = dev->data[dev->index++];
    		if (dev->index >= SMBUS_PIIX4_BLOCK_DATA_SIZE)
    			dev->index = 0;
    		break;
    }

    smbus_piix4_log("SMBus PIIX4: read(%02x) = %02x\n", addr, ret);

    return ret;
}


static void
smbus_piix4_write(uint16_t addr, uint8_t val, void *priv)
{
    smbus_piix4_t *dev = (smbus_piix4_t *) priv;
    uint8_t smbus_addr, smbus_read, prev_stat;
    uint16_t temp;

    smbus_piix4_log("SMBus PIIX4: write(%02x, %02x)\n", addr, val);

    prev_stat = dev->next_stat;
    dev->next_stat = 0;
    switch (addr - dev->io_base) {
    	case 0x00:
    		/* some status bits are reset by writing 1 to them */
    		for (smbus_addr = 0x02; smbus_addr <= 0x10; smbus_addr <<= 1) {
    			if (val & smbus_addr)
    				dev->stat &= ~smbus_addr;
    		}
    		break;
    	case 0x02:
    		dev->ctl = val & ~(0x40); /* START always reads 0 */
    		if (val & 0x02) { /* cancel an in-progress command if KILL is set */
    			/* cancel only if a command is in progress */
    			if (prev_stat) {
    				dev->stat = 0x10; /* raise FAILED */
    				timer_disable(&dev->response_timer);
    			}
    		}
    		if (val & 0x40) { /* dispatch command if START is set */
    			smbus_addr = (dev->addr >> 1);
    			if (!smbus_has_device(smbus_addr)) {
    				/* raise DEV_ERR if no device is at this address */
    				dev->next_stat = 0x4;
    				break;
    			}
    			smbus_read = (dev->addr & 0x01);

    			/* decode the 3-bit command protocol */
    			switch ((val >> 2) & 0x7) {
    				case 0x0: /* quick R/W */
    					dev->next_stat = 0x2;
    					break;
    				case 0x1: /* byte R/W */
    					if (smbus_read)
    						dev->data0 = smbus_read_byte(smbus_addr);
    					else
    						smbus_write_byte(smbus_addr, dev->data0);
    					dev->next_stat = 0x2;
    					break;
    				case 0x2: /* byte data R/W */
    					if (smbus_read)
    						dev->data0 = smbus_read_byte_cmd(smbus_addr, dev->cmd);
    					else
    						smbus_write_byte_cmd(smbus_addr, dev->cmd, dev->data0);
    					dev->next_stat = 0x2;
    					break;
    				case 0x3: /* word data R/W */
    					if (smbus_read) {
    						temp = smbus_read_word_cmd(smbus_addr, dev->cmd);
    						dev->data0 = (temp & 0xFF);
    						dev->data1 = (temp >> 8);
    					} else {
    						temp = ((dev->data1 << 8) | dev->data0);
    						smbus_write_word_cmd(smbus_addr, dev->cmd, temp);
    					}
    					dev->next_stat = 0x2;
    					break;
    				case 0x5: /* block R/W */
    					if (smbus_read)
    						dev->data0 = smbus_read_block_cmd(smbus_addr, dev->cmd, dev->data, SMBUS_PIIX4_BLOCK_DATA_SIZE);
    					else
    						smbus_write_block_cmd(smbus_addr, dev->cmd, dev->data, dev->data0);
    					dev->next_stat = 0x2;
    					break;
    				default:
    					/* other command protocols have undefined behavior, but raise DEV_ERR to be safe */
    					dev->next_stat = 0x4;
    					break;
    			}
    		}
    		break;
    	case 0x03:
    		dev->cmd = val;
    		break;
    	case 0x04:
    		dev->addr = val;
    		break;
    	case 0x05:
    		dev->data0 = val;
    		break;
    	case 0x06:
    		dev->data1 = val;
    		break;
    	case 0x07:
    		dev->data[dev->index++] = val;
    		if (dev->index >= SMBUS_PIIX4_BLOCK_DATA_SIZE)
    			dev->index = 0;
    		break;
    }

    /* if a status register update was given, dispatch it after 10ms to ensure nothing breaks */
    if (dev->next_stat) {
    	dev->stat = 0x1; /* raise HOST_BUSY while waiting */
    	timer_disable(&dev->response_timer);
    	timer_set_delay_u64(&dev->response_timer, 10 * TIMER_USEC);
    }
}


static void
smbus_piix4_response(void *priv)
{
    smbus_piix4_t *dev = (smbus_piix4_t *) priv;

    /* dispatch the status register update */
    dev->stat = dev->next_stat;
}


void
smbus_piix4_remap(smbus_piix4_t *dev, uint16_t new_io_base, uint8_t enable)
{
    if (dev->io_base != 0x0000)
	io_removehandler(dev->io_base, 0x10, smbus_piix4_read, NULL, NULL, smbus_piix4_write, NULL, NULL, dev);

    dev->io_base = new_io_base;
    smbus_piix4_log("SMBus PIIX4: remap to %04Xh\n", dev->io_base);

    if ((enable) && (dev->io_base != 0x0000))
	io_sethandler(dev->io_base, 0x10, smbus_piix4_read, NULL, NULL, smbus_piix4_write, NULL, NULL, dev);
}


static void *
smbus_piix4_init(const device_t *info)
{
    smbus_piix4_t *dev = (smbus_piix4_t *) malloc(sizeof(smbus_piix4_t));
    memset(dev, 0, sizeof(smbus_piix4_t));

    smbus_init();
    timer_add(&dev->response_timer, smbus_piix4_response, dev, 0);

    return dev;
}


static void
smbus_piix4_close(void *priv)
{
    smbus_piix4_t *dev = (smbus_piix4_t *) priv;

    free(dev);
}


const device_t piix4_smbus_device = {
    "PIIX4-compatible SMBus Host Controller",
    DEVICE_AT,
    0,
    smbus_piix4_init, smbus_piix4_close, NULL,
    NULL, NULL, NULL,
    NULL
};
