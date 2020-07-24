/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the port 440h device from Virtual PC 2007.
 *
 *
 *
 * Author:	RichardG, <richardg867@gmail.com>
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
#include <86box/machine.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/mem.h>
#include "cpu.h"


typedef struct {
    uint8_t	port440, port440read, port442, port443, port444;
} vpc2007_t;


#ifdef ENABLE_VPC2007_LOG
int vpc2007_do_log = ENABLE_VPC2007_LOG;


static void
vpc2007_log(const char *fmt, ...)
{
    va_list ap;

    if (vpc2007_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
int vpc2007_do_log = 0;

#define vpc2007_log(fmt, ...)
#endif


static uint8_t
vpc2007_read(uint16_t port, void *priv)
{
    vpc2007_t *dev = (vpc2007_t *) priv;
    uint8_t ret = 0xff;

    switch (port) {
    	case 0x440:
    		ret = dev->port440read;
    		dev->port440read = 0x02;
    		break;

    	case 0x445:
    		if ((dev->port440 == 0x1e) && (dev->port442 == 0x48) && (dev->port444 == 0xa7)) {
    			switch (dev->port443) {
    				case 0x0b:
    					ret = 0x00;
    					break;

    				case 0x1b: case 0x05:
    					ret = 0x01;
    					break;

    				case 0x02:
    					ret = 0x02;
    					break;

    				case 0x11:
    					ret = 0x04;
    					break;

    				case 0x12:
    					ret = 0x06;
    					break;

    				case 0x04: case 0x0d:
    					ret = 0x08;
    					break;
    			
    				case 0x03: case 0x09:
    					ret = 0x0b;
    					break;
    			
    				case 0x15:
    					ret = 0x12;
    					break;

    				case 0x17:
    					ret = 0x40;
    					break;
    			}
    		}

    		if (ret == 0xff)
    			vpc2007_log("VPC2007: unknown combination %02X %02X %02X %02X\n", dev->port440, dev->port442, dev->port443, dev->port444);

    		break;

    	default:
    		vpc2007_log("VPC2007: read from unknown port %02X\n", port);
    		break;
    }

    return ret;
}


static void
vpc2007_write(uint16_t port, uint8_t val, void *priv)
{
    vpc2007_t *dev = (vpc2007_t *) priv;
    uint32_t seg;

    switch (port) {
    	case 0x440:
    		dev->port440 = val;
    		dev->port440read = 0x03;
    		break;

    	case 0x442:
    		dev->port442 = val;
    		break;

    	case 0x443:
    		dev->port443 = val;
    		break;

    	case 0x444:
    		dev->port444 = val;
    		break;
    }
}


static void *
vpc2007_init(const device_t *info)
{
    vpc2007_t *dev = (vpc2007_t *) malloc(sizeof(vpc2007_t));
    memset(dev, 0, sizeof(vpc2007_t));

    io_sethandler(0x440, 6,
		  vpc2007_read, NULL, NULL, vpc2007_write, NULL, NULL, dev);

    return dev;
}


static void
vpc2007_close(void *priv)
{
    vpc2007_t *dev = (vpc2007_t *) priv;

    io_removehandler(0x440, 6,
		     vpc2007_read, NULL, NULL, vpc2007_write, NULL, NULL, dev);

    free(dev);
}


const device_t vpc2007_device = {
    "Virtual PC 2007 Port 440h Device",
    DEVICE_ISA,
    0,
    vpc2007_init, vpc2007_close, NULL,
    NULL, NULL, NULL,
    NULL
};
