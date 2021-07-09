/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Implementation of a generic text printer.
 *
 *		Simple old text printers were unable to do any formatting
 *		of the text. They were just sheets of paper with a fixed
 *		size (in the U.S., this would be Letter, 8.5"x11") with a
 *		set of fixed margings to allow for proper operation of the
 *		printer mechanics. This would lead to a page being 66 lines
 *		of 80 characters each.
 *
 *
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2018,2019 Fred N. van Kempen.
 *
 *		Redistribution and  use  in source  and binary forms, with
 *		or  without modification, are permitted  provided that the
 *		following conditions are met:
 *
 *		1. Redistributions of  source  code must retain the entire
 *		   above notice, this list of conditions and the following
 *		   disclaimer.
 *
 *		2. Redistributions in binary form must reproduce the above
 *		   copyright  notice,  this list  of  conditions  and  the
 *		   following disclaimer in  the documentation and/or other
 *		   materials provided with the distribution.
 *
 *		3. Neither the  name of the copyright holder nor the names
 *		   of  its  contributors may be used to endorse or promote
 *		   products  derived from  this  software without specific
 *		   prior written permission.
 *
 * THIS SOFTWARE  IS  PROVIDED BY THE  COPYRIGHT  HOLDERS AND CONTRIBUTORS
 * "AS IS" AND  ANY EXPRESS  OR  IMPLIED  WARRANTIES,  INCLUDING, BUT  NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE  ARE  DISCLAIMED. IN  NO  EVENT  SHALL THE COPYRIGHT
 * HOLDER OR  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL,  EXEMPLARY,  OR  CONSEQUENTIAL  DAMAGES  (INCLUDING,  BUT  NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE  GOODS OR SERVICES;  LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED  AND ON  ANY
 * THEORY OF  LIABILITY, WHETHER IN  CONTRACT, STRICT  LIABILITY, OR  TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING  IN ANY  WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/plat.h> 
#include <86box/lpt.h>
#include <86box/printer.h>
#include <86box/prt_devs.h>


#define FULL_PAGE	1			/* set if no top/bot margins */


/* Default page values (for now.) */
#define PAGE_WIDTH	8.5			/* standard U.S. Letter */
#define PAGE_HEIGHT	11
#define PAGE_LMARGIN	0.25			/* 0.25" left and right */
#define PAGE_RMARGIN	0.25
#if FULL_PAGE
# define PAGE_TMARGIN	0
# define PAGE_BMARGIN	0
#else
# define PAGE_TMARGIN	0.25
# define PAGE_BMARGIN	0.25
#endif
#define PAGE_CPI	10.0			/* standard 10 cpi */
#define PAGE_LPI	6.0			/* standard 6 lpi */


typedef struct {
    int8_t	dirty;		/* has the page been printed on? */
    char	pad;

    uint8_t	w;		/* size //INFO */
    uint8_t	h;

    char	*chars;		/* character data */
} psurface_t;


typedef struct {
    const char *name;

    void *	lpt;

    /* Output file name. */
    char	filename[1024];

    /* Printer timeout. */
    pc_timer_t	pulse_timer;
    pc_timer_t	timeout_timer;

    /* page data (TODO: make configurable) */
    double	page_width,	/* all in inches */
		page_height,
		left_margin,
		top_margin, 
		right_margin,
		bot_margin;

    /* internal page data */
    psurface_t	*page;
    uint8_t	max_chars,
		max_lines;
    uint8_t	curr_x,			/* print head position (chars) */
		curr_y;

    /* font data */
    double	cpi,			/* defined chars per inch */
		lpi;			/* defined lines per inch */

    /* handshake data */
    uint8_t	data;
    int8_t	ack;
    int8_t	select;
    int8_t	busy;
    int8_t	int_pending;
    int8_t	error;
    int8_t	autofeed;
    uint8_t	ctrl;
} prnt_t;


/* Dump the current page into a formatted file. */
static void 
dump_page(prnt_t *dev)
{
    char path[1024];
    uint16_t x, y;
    uint8_t ch;
    FILE *fp;

    /* Create the full path for this file. */
    memset(path, 0x00, sizeof(path));
    plat_append_filename(path, usr_path, "printer");
    if (! plat_dir_check(path))
        plat_dir_create(path);
    plat_path_slash(path);
    strcat(path, dev->filename);

    /* Create the file. */
    fp = plat_fopen(path, "a");
    if (fp == NULL) {
	//ERRLOG("PRNT: unable to create print page '%s'\n", path);
	return;
    }
    fseek(fp, 0, SEEK_END);

    /* If this is not a new file, add a formfeed first. */
    if (ftell(fp) != 0)
	fputc('\014', fp);

    for (y = 0; y < dev->curr_y; y++) {
	for (x = 0; x < dev->page->w; x++) {
		ch = dev->page->chars[(y * dev->page->w) + x];
		if (ch == 0x00) {
			/* End of line marker. */
			fputc('\n', fp);
			break;
		} else {
			fputc(ch, fp);
		}
	}
    }

    /* All done, close the file. */
    fclose(fp);
}


static void
new_page(prnt_t *dev)
{
    /* Dump the current page if needed. */
    if (dev->page->dirty)
	dump_page(dev);

    /* Clear page. */
    memset(dev->page->chars, 0x00, dev->page->h * dev->page->w);
    dev->curr_y = 0;
    dev->page->dirty = 0;
}


static void
pulse_timer(void *priv)
{
    prnt_t *dev = (prnt_t *) priv;

    if (dev->ack) {
	dev->ack = 0;
	lpt_irq(dev->lpt, 1);
    }

    timer_disable(&dev->pulse_timer);
}


static void
timeout_timer(void *priv)
{
    prnt_t *dev = (prnt_t *) priv;

    if (dev->page->dirty)
	new_page(dev);

    timer_disable(&dev->timeout_timer);
}


static void
reset_printer(prnt_t *dev)
{
    /* TODO: these three should be configurable */
    dev->page_width = PAGE_WIDTH;
    dev->page_height = PAGE_HEIGHT;
    dev->left_margin = PAGE_LMARGIN;
    dev->right_margin = PAGE_RMARGIN;
    dev->top_margin = PAGE_TMARGIN;
    dev->bot_margin = PAGE_BMARGIN;
    dev->cpi = PAGE_CPI;
    dev->lpi = PAGE_LPI;
    dev->ack = 0;

    /* Default page layout. */
    dev->max_chars = (int) ((dev->page_width - dev->left_margin - dev->right_margin) * dev->cpi);
    dev->max_lines = (int) ((dev->page_height -dev->top_margin - dev->bot_margin) * dev->lpi);

    dev->curr_x = dev->curr_y = 0;

    if (dev->page != NULL)
	dev->page->dirty = 0;

    /* Create a file for this page. */
    plat_tempfile(dev->filename, NULL, ".txt");

    timer_disable(&dev->pulse_timer);
    timer_disable(&dev->timeout_timer);
}


static int
process_char(prnt_t *dev, uint8_t ch)
{
    uint8_t i;

    switch (ch) {
	case 0x07:  /* Beeper (BEL) */
		/* TODO: beep? */
		return 1;

	case 0x08:	/* Backspace (BS) */
		if (dev->curr_x > 0)
			dev->curr_x--;
		return 1;

	case 0x09:	/* Tab horizontally (HT) */
		/* Find tab right to current pos. */
		i = dev->curr_x;
		dev->page->chars[(dev->curr_y * dev->page->w) + i++] = ' ';
		while ((i < dev->max_chars) && ((i % 8) != 0)) {
			dev->page->chars[(dev->curr_y * dev->page->w) + i] = ' ';
			i++;
		}
		dev->curr_x = i;
		return 1;

	case 0x0b:	/* Tab vertically (VT) */
		dev->curr_x = 0;
		return 1;

	case 0x0c:	/* Form feed (FF) */
		new_page(dev);
		return 1;

	case 0x0d:	/* Carriage Return (CR) */
		dev->curr_x = 0;
		if (! dev->autofeed)
			return 1;
		/*FALLTHROUGH*/

	case 0x0a:	/* Line feed */
		dev->curr_x = 0;
		if (++dev->curr_y >= dev->max_lines)
			new_page(dev);
		return 1;

	case 0x0e:	/* select wide printing (SO) */
		/* Ignore. */
		return 1;

	case 0x0f:	/* select condensed printing (SI) */
		/* Ignore. */
		return 1;

	case 0x11:	/* select printer (DC1) */
		/* Ignore. */
		return 0;

	case 0x12:	/* cancel condensed printing (DC2) */
		/* Ignore. */
		return 1;

	case 0x13:	/* deselect printer (DC3) */
		/* Ignore. */
		return 1;

	case 0x14:	/* cancel double-width printing (one line) (DC4) */
		/* Ignore. */
		return 1;

	case 0x18:	/* cancel line (CAN) */
		/* Ignore. */
		return 1;

	case 0x1b:	/* ESC */
		/* Ignore. */
		return 1;

	default:
		break;
    }

    /* Just a printable character. */
    return(0);
}


static void
handle_char(prnt_t *dev)
{
    uint8_t ch = dev->data;

    if (dev->page == NULL) return;

    if (process_char(dev, ch) == 1) {
	/* Command was processed. */
	return;
    }

    /* Store character in the page buffer. */
    dev->page->chars[(dev->curr_y * dev->page->w) + dev->curr_x] = ch;
    dev->page->dirty = 1;

    /* Update print head position. */
    if (++dev->curr_x >= dev->max_chars) {
	dev->curr_x = 0;
	if (++dev->curr_y >= dev->max_lines)
		new_page(dev);
    }
}


static void
write_data(uint8_t val, void *priv)
{
    prnt_t *dev = (prnt_t *)priv;

    if (dev == NULL) return;

    dev->data = val;
}


static void
write_ctrl(uint8_t val, void *priv)
{
    prnt_t *dev = (prnt_t *)priv;

    if (dev == NULL) return;

    /* set autofeed value */
    dev->autofeed = val & 0x02 ? 1 : 0;

    if (val & 0x08) {		/* SELECT */
	/* select printer */
	dev->select = 1;
    }

    if ((val & 0x04) && !(dev->ctrl & 0x04)) {
	/* reset printer */
	dev->select = 0;

	reset_printer(dev);
    }

    if (!(val & 0x01) && (dev->ctrl & 0x01)) {		/* STROBE */
	/* Process incoming character. */
	handle_char(dev);

	/* ACK it, will be read on next READ STATUS. */
	dev->ack = 1;

	timer_set_delay_u64(&dev->pulse_timer, ISACONST);
	timer_set_delay_u64(&dev->timeout_timer, 5000000 * TIMER_USEC);
    }

    dev->ctrl = val;
}


static uint8_t
read_status(void *priv)
{
    prnt_t *dev = (prnt_t *)priv;
    uint8_t ret = 0x1f;

    ret |= 0x80;

    if (!dev->ack)
	ret |= 0x40;

    return(ret);
}


static void *
prnt_init(void *lpt)
{
    prnt_t *dev;

    /* Initialize a device instance. */
    dev = (prnt_t *)malloc(sizeof(prnt_t));
    memset(dev, 0x00, sizeof(prnt_t));
    dev->ctrl = 0x04;
    dev->lpt = lpt;

    /* Initialize parameters. */
    reset_printer(dev);

    /* Create a page buffer. */
    dev->page = (psurface_t *)malloc(sizeof(psurface_t));
    dev->page->w = dev->max_chars;
    dev->page->h = dev->max_lines;
    dev->page->chars = (char *)malloc(dev->page->w * dev->page->h);
    memset(dev->page->chars, 0x00, dev->page->w * dev->page->h);

    timer_add(&dev->pulse_timer, pulse_timer, dev, 0);
    timer_add(&dev->timeout_timer, timeout_timer, dev, 0);

    return(dev);
}


static void
prnt_close(void *priv)
{
    prnt_t *dev = (prnt_t *)priv;

    if (dev == NULL)
	return;

    if (dev->page) {
	/* print last page if it contains data */
	if (dev->page->dirty)
		dump_page(dev);

	if (dev->page->chars != NULL)
		free(dev->page->chars);
	free(dev->page);
    }

    free(dev);
}


const lpt_device_t lpt_prt_text_device = {
    "Generic Text Printer",
    prnt_init,
    prnt_close,
    write_data,
    write_ctrl,
    NULL,
    read_status,
    NULL
};
