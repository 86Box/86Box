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
 * Version:	@(#)prt_ps.c	1.0.1	2019/12/06
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
#include "../lang/language.h"
#include "../lpt.h"
#include "../timer.h"
#include "../pit.h"
#include "../plat.h"
#include "../plat_dynld.h"
#include "../ui.h"
#include "prt_devs.h"

#if defined(_WIN32) && !defined(__WINDOWS__)
#define __WINDOWS__
#endif
#include <ghostscript/iapi.h>
#include <ghostscript/ierrors.h>


#define PATH_GHOSTSCRIPT_DLL		"gsdll32.dll"
#define PATH_GHOSTSCRIPT_SO		"libgs.so"

#define POSTSCRIPT_BUFFER_LENGTH	65536

static GSDLLAPI int	(*ghostscript_revision)(gsapi_revision_t *pr, int len);
static GSDLLAPI int	(*ghostscript_new_instance)(void **pinstance, void *caller_handle);
static GSDLLAPI void	(*ghostscript_delete_instance)(void *instance);
static GSDLLAPI int	(*ghostscript_set_arg_encoding)(void *instance, int encoding);
static GSDLLAPI int	(*ghostscript_init_with_args)(void *instance, int argc, char **argv);
static GSDLLAPI int	(*ghostscript_exit)(void *instance);

static dllimp_t ghostscript_imports[] = {
  { "gsapi_revision",			&ghostscript_revision			},
  { "gsapi_new_instance",		&ghostscript_new_instance		},
  { "gsapi_delete_instance",		&ghostscript_delete_instance		},
  { "gsapi_set_arg_encoding",		&ghostscript_set_arg_encoding		},
  { "gsapi_init_with_args",		&ghostscript_init_with_args		},
  { "gsapi_exit",			&ghostscript_exit			},
  { NULL,				NULL					}
};

static void	*ghostscript_handle = NULL;
static bool	ghostscript_initialized = false;

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

    char	buffer[POSTSCRIPT_BUFFER_LENGTH];
    uint16_t	buffer_pos;
} ps_t;

static void
reset_ps(ps_t *dev)
{
    if (dev == NULL) { 
	return;
    }

    dev->ack = false;

    dev->buffer[0] = 0;
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

static int
convert_to_pdf(ps_t *dev)
{
    volatile int code;
    void *instance = NULL;
    wchar_t input_fn[1024], output_fn[1024], *gsargv[9];

    input_fn[0] = 0;
    wcscat(input_fn, dev->printer_path);
    wcscat(input_fn, dev->filename);

    output_fn[0] = 0;
    wcscat(output_fn, input_fn);
    wcscpy(output_fn + wcslen(output_fn) - 3, L".pdf");

    gsargv[0] = L"";
    gsargv[1] = L"-dNOPAUSE";
    gsargv[2] = L"-dBATCH";
    gsargv[3] = L"-dSAFER";
    gsargv[4] = L"-sDEVICE=pdfwrite";
    gsargv[5] = L"-q";
    gsargv[6] = L"-o";
    gsargv[7] = output_fn;
    gsargv[8] = input_fn;

    code = ghostscript_new_instance(&instance, dev);
    if (code < 0) {
	return code;
    }

    code = ghostscript_set_arg_encoding(instance, GS_ARG_ENCODING_UTF16LE);

    if (code == 0) {
	code = ghostscript_init_with_args(instance, 9, (char **) gsargv);
    }

    if (code == 0 || code == gs_error_Quit) {
	code = ghostscript_exit(instance);
    } else {
	ghostscript_exit(instance);
    }

    ghostscript_delete_instance(instance);

    if (code == 0) {
	plat_remove(input_fn);
    } else {
	plat_remove(output_fn);
    }

    return code;
}

static void
finish_document(ps_t *dev)
{
    if (ghostscript_handle != NULL) {
	convert_to_pdf(dev);
    }

    dev->filename[0] = 0;
}

static void
write_buffer(ps_t *dev, bool newline)
{
    wchar_t path[1024];
    FILE *fp;

    if (dev->buffer[0] == 0) {
	return;
    }

    if (dev->filename[0] == 0) {
	plat_tempfile(dev->filename, NULL, L".ps");
    }

    path[0] = 0;
    wcscat(path, dev->printer_path);
    wcscat(path, dev->filename);

    fp = plat_fopen(path, L"a");
    if (fp == NULL) {
	return;
    }

    fseek(fp, 0, SEEK_END);

    fprintf(fp, "%.*s%s", POSTSCRIPT_BUFFER_LENGTH, dev->buffer, newline ? "\n" : "");

    fclose(fp);

    dev->buffer[0] = 0;
    dev->buffer_pos = 0;
}

static void
timeout_timer(void *priv)
{
    ps_t *dev = (ps_t *) priv;

    write_buffer(dev, false);
    finish_document(dev);

    timer_disable(&dev->timeout_timer);
}

static void
ps_write_data(uint8_t val, void *p)
{
    ps_t *dev = (ps_t *) p;

    if (dev == NULL) { 
	return;
    }

    dev->data = (char) val;
}

static bool
process_nonprintable(ps_t *dev)
{
	switch (dev->data) {
		case '\b':
			dev->buffer_pos--;
			break;
		case '\r':
			dev->buffer_pos = 0;
			if (dev->autofeed)
				write_buffer(dev, true);
			break;
		case '\v':
		case '\f':
		case '\n':
			write_buffer(dev, true);
			break;
		case 0x04:	// Ctrl+D
			write_buffer(dev, false);
			finish_document(dev);
			break;
		
		/* Characters that should be written to the buffer as-is */
		case '\t':
			return false;
	}

	return true;
}

static void
process_data(ps_t *dev)
{
    if (dev->data < 0x20 || dev->data == 0x7F) {
	if (process_nonprintable(dev)) {
		return;
	}
    }

    if (dev->buffer_pos == POSTSCRIPT_BUFFER_LENGTH) {
	write_buffer(dev, false);
    }

    dev->buffer[dev->buffer_pos++] = dev->data;
    dev->buffer[dev->buffer_pos] = 0;
}

static void
ps_write_ctrl(uint8_t val, void *p)
{
    ps_t *dev = (ps_t *) p;

    if (dev == NULL) { 
	return;
    }

    dev->autofeed = val & 0x02 ? true : false;

    if (val & 0x08) {
	dev->select = true;
    }

    if ((val & 0x04) && !(dev->ctrl & 0x04)) {
	// reset printer
	dev->select = false;

	reset_ps(dev);
    }

    if (!(val & 0x01) && (dev->ctrl & 0x01)) {
	process_data(dev);

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

    if (!dev->ack) {
	ret |= 0x40;
    }

    return(ret);
}

static void
ghostscript_init()
{
    gsapi_revision_t rev;

    ghostscript_initialized = true;

    /* Try loading the DLL. */
    ghostscript_handle = dynld_module(PATH_GHOSTSCRIPT_DLL, ghostscript_imports);
    if (ghostscript_handle == NULL) {
	ui_msgbox(MBX_ERROR, (wchar_t *) IDS_2123);
	return;
    }

    if (ghostscript_revision(&rev, sizeof(rev)) == 0) {
	pclog("Loaded %s, rev %ld (%ld)\n", rev.product, rev.revision, rev.revisiondate);
    } else {
	ghostscript_handle = NULL;
    }
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

    if (!ghostscript_initialized) {
	ghostscript_init();
    }

    // Cache print folder path
    memset(dev->printer_path, 0x00, sizeof(dev->printer_path));
    plat_append_filename(dev->printer_path, usr_path, L"printer");
    if (!plat_dir_check(dev->printer_path)) {
	plat_dir_create(dev->printer_path);
    }
    plat_path_slash(dev->printer_path);

    timer_add(&dev->pulse_timer, pulse_timer, dev, 0);
    timer_add(&dev->timeout_timer, timeout_timer, dev, 0);

    return(dev);
}

static void
ps_close(void *p)
{
    ps_t *dev = (ps_t *) p;

    if (dev == NULL) { 
	return;
    }

    if (dev->buffer[0] != 0) {
	write_buffer(dev, false);
	finish_document(dev);
    }

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