/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of a generic PostScript printer.
 *
 * Version:	@(#)prt_ps.c	1.0.0	2019/xx/xx
 *
 * Authors:	David Hrdlička, <hrdlickadavid@outlook.com>
 *
 *		Copyright 2019 David Hrdlička.
 */

#include <inttypes.h>
#include <memory.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../lpt.h"
#include "../timer.h"
#include "../pit.h"
#include "../plat.h"
#include "prt_devs.h"

typedef struct
{
    const char	*name;

    void	*lpt;

    pc_timer_t	pulse_timer;
    pc_timer_t	timeout_timer;

    char	data;
    bool	ack;
    bool	select;
    bool	busy;
    bool	int_pending;
    bool	error;
    bool	autofeed;
    uint8_t	ctrl;

    wchar_t	printer_path[260];

    wchar_t	filename[260];

    char	buffer[65536];
    uint16_t	buffer_pos;
} ps_t;

static void
reset_ps(ps_t *dev)
{
    if (dev == NULL) return;

    dev->ack = false;

    memset(&dev->buffer, 0x00, sizeof(dev->buffer));
    dev->buffer_pos = 0;

    timer_disable(&dev->pulse_timer);
    timer_disable(&dev->timeout_timer);
}

static void
pulse_timer(void *priv)
{
    ps_t *dev = (ps_t *) priv;

    if (dev->ack) {
	dev->ack = 0;
	lpt_irq(dev->lpt, 1);
    }

    timer_disable(&dev->pulse_timer);
}

static void
finish_document(ps_t *dev)
{
    // todo: convert to PDF

    dev->filename[0] = 0;
}

static void
write_buffer(ps_t *dev)
{
    wchar_t path[1024];
    FILE *fp;

    if (dev->filename[0] == 0)
    {
	plat_tempfile(dev->filename, NULL, L".ps");
    }

    path[0] = 0;
    wcscat(path, dev->printer_path);
    wcscat(path, dev->filename);

    fp = plat_fopen(path, L"a");
    if (fp == NULL)
	return;

    fseek(fp, 0, SEEK_END);

    fprintf(fp, "%s\n", dev->buffer);

    fclose(fp);
}

static void
timeout_timer(void *priv)
{
    ps_t *dev = (ps_t *) priv;

    if (dev == NULL) return;

    write_buffer(dev);
    finish_document(dev);

    timer_disable(&dev->timeout_timer);
}

static void
ps_write_data(uint8_t val, void *p)
{
    ps_t *dev = (ps_t *) p;

    if (dev == NULL) return;

    dev->data = (char) val;
}

static void
ps_write_ctrl(uint8_t val, void *p)
{
    ps_t *dev = (ps_t *) p;

    if (dev == NULL) return;

    dev->autofeed = val & 0x02 ? true : false;

    if (val & 0x08)
    {
	dev->select = true;
    }

    if((val & 0x04) && !(dev->ctrl & 0x04))
    {
	// reset printer
	dev->select = false;

	reset_ps(dev);
    }

    if(!(val & 0x01) && (dev->ctrl & 0x01))
    {
	if (dev->data < 0x20 && dev->data != '\t')
	{
		switch (dev->data)
		{
			case '\b':
				dev->buffer[dev->buffer_pos--] = 0;
				break;
			case '\r':
				dev->buffer_pos = 0;
				if(!dev->autofeed)
					break;
			case '\n':
				write_buffer(dev);
				dev->buffer[0] = 0;
				dev->buffer_pos = 0;
				break;
		}
	}
	else
	{
		dev->buffer[dev->buffer_pos++] = dev->data;
		dev->buffer[dev->buffer_pos] = 0;
	}

	dev->ack = true;

	timer_set_delay_u64(&dev->pulse_timer, ISACONST);
	timer_set_delay_u64(&dev->timeout_timer, 500000 * TIMER_USEC);
    }

    dev->ctrl = val;
}

static uint8_t
ps_read_status(void *p)
{
    ps_t *dev = (ps_t *) p;
    uint8_t ret = 0x1f;

    ret |= 0x80;

    if(!dev->ack)
	ret |= 0x40;

    return(ret);
}

static void *
ps_init(void *lpt)
{
    ps_t *dev;

    dev = (ps_t *) malloc(sizeof(ps_t));
    memset(dev, 0x00, sizeof(ps_t));
    dev->ctrl = 0x04;
    dev->lpt = lpt;

    reset_ps(dev);

    // Cache print folder path
    memset(dev->printer_path, 0x00, sizeof(dev->printer_path));
    plat_append_filename(dev->printer_path, usr_path, L"printer");
    if (! plat_dir_check(dev->printer_path))
	plat_dir_create(dev->printer_path);
    plat_path_slash(dev->printer_path);

    timer_add(&dev->pulse_timer, pulse_timer, dev, 0);
    timer_add(&dev->timeout_timer, timeout_timer, dev, 0);

    return(dev);
}

static void
ps_close(void *p)
{
    ps_t *dev = (ps_t *) p;

    if (dev == NULL) return;

    write_buffer(dev);
    finish_document(dev);

    free(dev);
}

const lpt_device_t lpt_prt_ps_device = {
    name: "Generic PostScript printer",
    init: ps_init,
    close: ps_close,
    write_data: ps_write_data,
    write_ctrl: ps_write_ctrl,
    read_data: NULL,
    read_status: ps_read_status,
    read_ctrl: NULL
};