/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of a generic PostScript printer and a
 *          generic PCL 5e printer.
 *
 *
 *
 * Authors: David Hrdlička, <hrdlickadavid@outlook.com>
 *          Cacodemon345
 *
 *          Copyright 2019 David Hrdlička.
 *          Copyright 2024 Cacodemon345.
 */

#include <inttypes.h>
#include <memory.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/lpt.h>
#include <86box/pit.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/plat_dynld.h>
#include <86box/ui.h>
#include <86box/prt_devs.h>

#ifdef _WIN32
#    define GSDLLAPI __stdcall
#else
#    define GSDLLAPI
#endif

#define GS_ARG_ENCODING_UTF8 1
#define gs_error_Quit        -101

#ifdef _WIN32
#    if (!(defined __amd64__ || defined _M_X64 || defined __aarch64__ || defined _M_ARM64))
#        define PATH_GHOSTSCRIPT_DLL "gsdll32.dll"
#        define PATH_GHOSTPCL_DLL    "gpcl6dll32.dll"
#    else
#        define PATH_GHOSTSCRIPT_DLL "gsdll64.dll"
#        define PATH_GHOSTPCL_DLL    "gpcl6dll64.dll"
#    endif
#elif defined __APPLE__
#    define PATH_GHOSTSCRIPT_DLL "libgs.dylib"
#    define PATH_GHOSTPCL_DLL    "libgpcl6.9.54.dylib"
#else
#    define PATH_GHOSTSCRIPT_DLL      "libgs.so.9"
#    define PATH_GHOSTSCRIPT_DLL_ALT1 "libgs.so.10"
#    define PATH_GHOSTSCRIPT_DLL_ALT2 "libgs.so"
#    define PATH_GHOSTPCL_DLL         "libgpcl6.so.9"
#    define PATH_GHOSTPCL_DLL_ALT1    "libgpcl6.so.10"
#    define PATH_GHOSTPCL_DLL_ALT2    "libgpcl6.so"
#endif

#define POSTSCRIPT_BUFFER_LENGTH 65536

typedef struct ps_t {
    const char *name;

    void *lpt;

    pc_timer_t pulse_timer;
    pc_timer_t timeout_timer;

    char    data;
    bool    ack;
    bool    select;
    bool    busy;
    bool    int_pending;
    bool    error;
    bool    autofeed;
    bool    pcl;
    bool    pcl_escape;
    uint8_t ctrl;

    char printer_path[260];

    char filename[260];

    char   buffer[POSTSCRIPT_BUFFER_LENGTH];
    size_t buffer_pos;
} ps_t;

typedef struct gsapi_revision_s {
    const char *product;
    const char *copyright;
    long        revision;
    long        revisiondate;
} gsapi_revision_t;

static int(GSDLLAPI *gsapi_revision)(gsapi_revision_t *pr, int len);
static int(GSDLLAPI *gsapi_new_instance)(void **pinstance, void *caller_handle);
static void(GSDLLAPI *gsapi_delete_instance)(void *instance);
static int(GSDLLAPI *gsapi_set_arg_encoding)(void *instance, int encoding);
static int(GSDLLAPI *gsapi_init_with_args)(void *instance, int argc, char **argv);
static int(GSDLLAPI *gsapi_exit)(void *instance);

static dllimp_t ghostscript_imports[] = {
  // clang-format off
    { "gsapi_revision",         &gsapi_revision         },
    { "gsapi_new_instance",     &gsapi_new_instance     },
    { "gsapi_delete_instance",  &gsapi_delete_instance  },
    { "gsapi_set_arg_encoding", &gsapi_set_arg_encoding },
    { "gsapi_init_with_args",   &gsapi_init_with_args   },
    { "gsapi_exit",             &gsapi_exit             },
    { NULL,                     NULL                    }
  // clang-format on
};

static void *ghostscript_handle = NULL;

static void
reset_ps(ps_t *dev)
{
    if (dev == NULL)
        return;

    dev->ack = false;

    dev->buffer[0]  = 0;
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
    volatile int code, arg = 0;
    void        *instance = NULL;
    char         input_fn[1024];
    char         output_fn[1024];
    char        *gsargv[11];

    strcpy(input_fn, dev->printer_path);
    path_slash(input_fn);
    strcat(input_fn, dev->filename);

    strcpy(output_fn, input_fn);
    strcpy(output_fn + strlen(output_fn) - (dev->pcl ? 4 : 3), ".pdf");

    gsargv[arg++] = "";
    gsargv[arg++] = "-dNOPAUSE";
    gsargv[arg++] = "-dBATCH";
    gsargv[arg++] = "-dSAFER";
    gsargv[arg++] = "-sDEVICE=pdfwrite";
    if (dev->pcl) {
        gsargv[arg++] = "-LPCL";
        gsargv[arg++] = "-lPCL5E";
    }
    gsargv[arg++] = "-q";
    gsargv[arg++] = "-o";
    gsargv[arg++] = output_fn;
    gsargv[arg++] = input_fn;

    code = gsapi_new_instance(&instance, dev);
    if (code < 0)
        return code;

    code = gsapi_set_arg_encoding(instance, GS_ARG_ENCODING_UTF8);

    if (code == 0)
        code = gsapi_init_with_args(instance, arg, gsargv);

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
    char  path[1024];
    FILE *fp;

    if (dev->buffer[0] == 0)
        return;

    if (dev->filename[0] == 0)
        plat_tempfile(dev->filename, NULL, dev->pcl ? ".pcl" : ".ps");

    strcpy(path, dev->printer_path);
    path_slash(path);
    strcat(path, dev->filename);

    fp = plat_fopen(path, dev->pcl ? "ab" : "a");
    if (fp == NULL)
        return;

    fseek(fp, 0, SEEK_END);

    if (dev->pcl)
        fwrite(dev->buffer, 1, dev->buffer_pos, fp);
    else
        fprintf(fp, "%.*s", POSTSCRIPT_BUFFER_LENGTH, dev->buffer);

    fclose(fp);

    dev->buffer[0]  = 0;
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
ps_write_data(uint8_t val, void *priv)
{
    ps_t *dev = (ps_t *) priv;

    if (dev == NULL)
        return;

    dev->data = (char) val;
}

static void
process_data(ps_t *dev)
{
    /* On PCL, check for escape sequences. */
    if (dev->pcl) {
        if (dev->data == 0x1B)
            dev->pcl_escape = true;
        else if (dev->pcl_escape) {
            dev->pcl_escape = false;
            if (dev->data == 0xE) {
                dev->buffer[dev->buffer_pos++] = dev->data;
                dev->buffer[dev->buffer_pos]   = 0;

                if (dev->buffer_pos > 2)
                    write_buffer(dev, true);

                return;
            }
        }
    }
    /* On PostScript, check for non-printable characters. */
    else if ((dev->data < 0x20) || (dev->data == 0x7f)) {
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
    dev->buffer[dev->buffer_pos]   = 0;
}

static void
ps_write_ctrl(uint8_t val, void *priv)
{
    ps_t *dev = (ps_t *) priv;

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
ps_read_status(void *priv)
{
    const ps_t   *dev = (ps_t *) priv;
    uint8_t       ret = 0x9f;

    if (!dev->ack)
        ret |= 0x40;

    return ret;
}

static void *
ps_init(void *lpt)
{
    ps_t            *dev = (ps_t *) calloc(1, sizeof(ps_t));
    gsapi_revision_t rev;

    dev->ctrl = 0x04;
    dev->lpt  = lpt;
    dev->pcl  = false;

    /* Try loading the DLL. */
    ghostscript_handle = dynld_module(PATH_GHOSTSCRIPT_DLL, ghostscript_imports);
#ifdef PATH_GHOSTSCRIPT_DLL_ALT1
    if (ghostscript_handle == NULL) {
        ghostscript_handle = dynld_module(PATH_GHOSTSCRIPT_DLL_ALT1, ghostscript_imports);
#    ifdef PATH_GHOSTSCRIPT_DLL_ALT2
        if (ghostscript_handle == NULL)
            ghostscript_handle = dynld_module(PATH_GHOSTSCRIPT_DLL_ALT2, ghostscript_imports);
#    endif
    }
#endif
    if (ghostscript_handle == NULL) {
        ui_msgbox_header(MBX_ERROR, plat_get_string(STRING_GHOSTSCRIPT_ERROR_TITLE), plat_get_string(STRING_GHOSTSCRIPT_ERROR_DESC));
    } else {
        if (gsapi_revision(&rev, sizeof(rev)) == 0) {
            pclog("Loaded %s, rev %ld (%ld)\n", rev.product, rev.revision, rev.revisiondate);
        } else {
            dynld_close(ghostscript_handle);
            ghostscript_handle = NULL;
        }
    }

    /* Cache print folder path. */
    memset(dev->printer_path, 0x00, sizeof(dev->printer_path));
    path_append_filename(dev->printer_path, usr_path, "printer");
    if (!plat_dir_check(dev->printer_path))
        plat_dir_create(dev->printer_path);
    path_slash(dev->printer_path);

    timer_add(&dev->pulse_timer, pulse_timer, dev, 0);
    timer_add(&dev->timeout_timer, timeout_timer, dev, 0);

    reset_ps(dev);

    return dev;
}

#ifdef USE_PCL
static void *
pcl_init(void *lpt)
{
    ps_t            *dev = (ps_t *) calloc(1, sizeof(ps_t));
    gsapi_revision_t rev;

    dev->ctrl = 0x04;
    dev->lpt  = lpt;
    dev->pcl  = true;

    /* Try loading the DLL. */
    ghostscript_handle = dynld_module(PATH_GHOSTPCL_DLL, ghostscript_imports);
#ifdef PATH_GHOSTPCL_DLL_ALT1
    if (ghostscript_handle == NULL) {
        ghostscript_handle = dynld_module(PATH_GHOSTPCL_DLL_ALT1, ghostscript_imports);
#    ifdef PATH_GHOSTPCL_DLL_ALT2
        if (ghostscript_handle == NULL)
            ghostscript_handle = dynld_module(PATH_GHOSTPCL_DLL_ALT2, ghostscript_imports);
#    endif
    }
#endif
    if (ghostscript_handle == NULL) {
        ui_msgbox_header(MBX_ERROR, plat_get_string(STRING_GHOSTPCL_ERROR_TITLE), plat_get_string(STRING_GHOSTPCL_ERROR_DESC));
    } else {
        if (gsapi_revision(&rev, sizeof(rev)) == 0) {
            pclog("Loaded %s, rev %ld (%ld)\n", rev.product, rev.revision, rev.revisiondate);
        } else {
            dynld_close(ghostscript_handle);
            ghostscript_handle = NULL;
        }
    }

    /* Cache print folder path. */
    memset(dev->printer_path, 0x00, sizeof(dev->printer_path));
    path_append_filename(dev->printer_path, usr_path, "printer");
    if (!plat_dir_check(dev->printer_path))
        plat_dir_create(dev->printer_path);
    path_slash(dev->printer_path);

    timer_add(&dev->pulse_timer, pulse_timer, dev, 0);
    timer_add(&dev->timeout_timer, timeout_timer, dev, 0);

    reset_ps(dev);

    return dev;
}
#endif

static void
ps_close(void *priv)
{
    ps_t *dev = (ps_t *) priv;

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
    .name          = "Generic PostScript Printer",
    .internal_name = "postscript",
    .init          = ps_init,
    .close         = ps_close,
    .write_data    = ps_write_data,
    .write_ctrl    = ps_write_ctrl,
    .read_data     = NULL,
    .read_status   = ps_read_status,
    .read_ctrl     = NULL
};

#ifdef USE_PCL
const lpt_device_t lpt_prt_pcl_device = {
    .name          = "Generic PCL5e Printer",
    .internal_name = "pcl",
    .init          = pcl_init,
    .close         = ps_close,
    .write_data    = ps_write_data,
    .write_ctrl    = ps_write_ctrl,
    .read_data     = NULL,
    .read_status   = ps_read_status,
    .read_ctrl     = NULL
};
#endif
