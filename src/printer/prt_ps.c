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
 *
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
#include <86box/86box.h>
#include <86box/language.h>
#include <86box/lpt.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/plat.h>
#include <86box/plat_dynld.h>
#include <86box/ui.h>
#include <86box/prt_devs.h>


#ifdef _WIN32
# define GSDLLAPI __stdcall
#else
# define GSDLLAPI
#endif


#define GS_ARG_ENCODING_UTF8	1
#define gs_error_Quit		-101 

#ifdef _WIN32
#define PATH_GHOSTSCRIPT_DLL		"gsdll32.dll"
#elif defined __APPLE__
#define PATH_GHOSTSCRIPT_DLL		"libgs.dylib"
#else
#define PATH_GHOSTSCRIPT_DLL		"libgs.so"
#endif

#define POSTSCRIPT_BUFFER_LENGTH	65536


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

    char	printer_path[260];

    char	filename[260];

    char	buffer[POSTSCRIPT_BUFFER_LENGTH];
    size_t	buffer_pos;
} ps_t;

typedef struct gsapi_revision_s {
    const char *product;
    const char *copyright;
    long revision;
    long revisiondate;
} gsapi_revision_t;


static int 	(GSDLLAPI *gsapi_revision)(gsapi_revision_t *pr, int len);
static int 	(GSDLLAPI *gsapi_new_instance)(void **pinstance, void *caller_handle);
static void 	(GSDLLAPI *gsapi_delete_instance)(void *instance);
static int 	(GSDLLAPI *gsapi_set_arg_encoding)(void *instance, int encoding);
static int 	(GSDLLAPI *gsapi_init_with_args)(void *instance, int argc, char **argv);
static int 	(GSDLLAPI *gsapi_exit)(void *instance);

static dllimp_t ghostscript_imports[] = {
  { "gsapi_revision",			&gsapi_revision			},
  { "gsapi_new_instance",		&gsapi_new_instance		},
  { "gsapi_delete_instance",		&gsapi_delete_instance		},
  { "gsapi_set_arg_encoding",		&gsapi_set_arg_encoding		},
  { "gsapi_init_with_args",		&gsapi_init_with_args		},
  { "gsapi_exit",			&gsapi_exit			},
  { NULL,				NULL				}
};

static void	*ghostscript_handle = NULL;


static void
reset_ps(ps_t *dev)
{
    if (dev == NULL)
	return;

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
    char input_fn[1024], output_fn[1024], *gsargv[9];

    strcpy(input_fn, dev->printer_path);
    plat_path_slash(input_fn);
    strcat(input_fn, dev->filename);

    strcpy(output_fn, input_fn);
    strcpy(output_fn + strlen(output_fn) - 3, ".pdf");

    gsargv[0] = "";
    gsargv[1] = "-dNOPAUSE";
    gsargv[2] = "-dBATCH";
    gsargv[3] = "-dSAFER";
    gsargv[4] = "-sDEVICE=pdfwrite";
    gsargv[5] = "-q";
    gsargv[6] = "-o";
    gsargv[7] = output_fn;
    gsargv[8] = input_fn;

    code = gsapi_new_instance(&instance, dev);
    if (code < 0)
	return code;

    code = gsapi_set_arg_encoding(instance, GS_ARG_ENCODING_UTF8);

    if (code == 0)
	code = gsapi_init_with_args(instance, 9, gsargv);

    if (code == 0 || code == gs_error_Quit)
	code = gsapi_exit(instance);
    else
	gsapi_exit(instance);

    gsapi_delete_instance(instance);

    if (code == 0)
	plat_remove(input_fn);
    else
	plat_remove(output_fn);

    return code;
}


static void
write_buffer(ps_t *dev, bool finish)
{
    char path[1024];
    FILE *fp;

    if (dev->buffer[0] == 0)
	return;

    if (dev->filename[0] == 0)
	plat_tempfile(dev->filename, NULL, ".ps");

    strcpy(path, dev->printer_path);
    plat_path_slash(path);
    strcat(path, dev->filename);

    fp = plat_fopen(path, "a");
    if (fp == NULL)
	return;

    fseek(fp, 0, SEEK_END);

    fprintf(fp, "%.*s", POSTSCRIPT_BUFFER_LENGTH, dev->buffer);

    fclose(fp);

    dev->buffer[0] = 0;
    dev->buffer_pos = 0;

    if (finish) {
	if (ghostscript_handle != NULL)
		convert_to_pdf(dev);

	dev->filename[0] = 0;
    }
}


static void
timeout_timer(void *priv)
{
    ps_t *dev = (ps_t *) priv;

    write_buffer(dev, true);

    timer_disable(&dev->timeout_timer);
}


static void
ps_write_data(uint8_t val, void *p)
{
    ps_t *dev = (ps_t *) p;

    if (dev == NULL)
	return;

    dev->data = (char) val;
}


static void
process_data(ps_t *dev)
{
    /* Check for non-printable characters */
    if ((dev->data < 0x20) || (dev->data == 0x7f)) {
	switch (dev->data) {
		/* The following characters are considered white-space
		   by the PostScript specification */
		case '\t':
		case '\n':
		case '\f':
		case '\r':
			break;

		/* Same with NUL, except we better change it to a space first */
		case '\0':
			dev->data = ' ';
			break;

		/* Ctrl+D (0x04) marks the end of the document */
		case '\4':
			write_buffer(dev, true);
			return;

		/* Don't bother with the others */
		default:
			return;
	}
    }

    /* Flush the buffer if we have run to its end */
    if (dev->buffer_pos == POSTSCRIPT_BUFFER_LENGTH - 1)
	write_buffer(dev, false);

    dev->buffer[dev->buffer_pos++] = dev->data;
    dev->buffer[dev->buffer_pos] = 0;
}


static void
ps_write_ctrl(uint8_t val, void *p)
{
    ps_t *dev = (ps_t *) p;

    if (dev == NULL)
	return;

    dev->autofeed = val & 0x02 ? true : false;

    if (val & 0x08)
	dev->select = true;

    if ((val & 0x04) && !(dev->ctrl & 0x04)) {
	/* Reset printer */
	dev->select = false;

	reset_ps(dev);
    }

    if (!(val & 0x01) && (dev->ctrl & 0x01)) {
	process_data(dev);

	dev->ack = true;

	timer_set_delay_u64(&dev->pulse_timer, ISACONST);
	timer_set_delay_u64(&dev->timeout_timer, 5000000 * TIMER_USEC);
    }

    dev->ctrl = val;
}


static uint8_t
ps_read_status(void *p)
{
    ps_t *dev = (ps_t *) p;
    uint8_t ret = 0x9f;

    if (!dev->ack)
	ret |= 0x40;

    return(ret);
}


static void *
ps_init(void *lpt)
{
    ps_t *dev;
    gsapi_revision_t rev;

    dev = (ps_t *) malloc(sizeof(ps_t));
    memset(dev, 0x00, sizeof(ps_t));
    dev->ctrl = 0x04;
    dev->lpt = lpt;

    reset_ps(dev);

    /* Try loading the DLL. */
    ghostscript_handle = dynld_module(PATH_GHOSTSCRIPT_DLL, ghostscript_imports);
    if (ghostscript_handle == NULL)
	ui_msgbox_header(MBX_ERROR, (wchar_t *) IDS_2114, (wchar_t *) IDS_2132);
    else {
	if (gsapi_revision(&rev, sizeof(rev)) == 0)
		pclog("Loaded %s, rev %ld (%ld)\n", rev.product, rev.revision, rev.revisiondate);
	else {
		dynld_close(ghostscript_handle);
		ghostscript_handle = NULL;
	}
    }

    /* Cache print folder path. */
    memset(dev->printer_path, 0x00, sizeof(dev->printer_path));
    plat_append_filename(dev->printer_path, usr_path, "printer");
    if (!plat_dir_check(dev->printer_path))
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

    if (dev == NULL)
	return;

    if (dev->buffer[0] != 0)
	write_buffer(dev, true);

    if (ghostscript_handle != NULL) {
	dynld_close(ghostscript_handle);
	ghostscript_handle = NULL;
    }

    free(dev);
}


const lpt_device_t lpt_prt_ps_device = {
    .name = "Generic PostScript Printer",
    .init = ps_init,
    .close = ps_close,
    .write_data = ps_write_data,
    .write_ctrl = ps_write_ctrl,
    .read_data = NULL,
    .read_status = ps_read_status,
    .read_ctrl = NULL
};
