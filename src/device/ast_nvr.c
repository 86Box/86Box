/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the AST Bravo MS secondary NVR
 *
 *
 *
 * Authors: win2kgamer
 *
 *          Copyright 2025 win2kgamer.
 */

#ifdef ENABLE_AST_NVR_LOG
#include <stdarg.h>
#endif

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/machine.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/rom.h>
#include <86box/log.h>

#ifdef ENABLE_AST_NVR_LOG
int ast_nvr_do_log = ENABLE_AST_NVR_LOG;

static void
ast_nvr_log(void *priv, const char *fmt, ...)
{
    if (ast_nvr_do_log) {
        va_list ap;
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define ast_nvr_log(fmt, ...)
#endif

typedef struct ast_nvr_t {
    int addr;
    int bank;

    uint8_t *ram;
    int     size;

    char *fn;

    void * log; // New logging system
} ast_nvr_t;

static uint8_t
ast_nvr_read(uint16_t port, void *priv)
{
    ast_nvr_t *nvr = (ast_nvr_t *) priv;
    uint8_t          ret = 0xff;

    switch (port) {
        case 0x800 ... 0x8FF:
            nvr->addr = ((nvr->bank << 8) + (port - 0x800));
            ret = nvr->ram[nvr->addr];
            break;
        default:
            break;
    }

    ast_nvr_log(nvr->log, "AST NVR Read [%02X:%02X] = %02X\n", nvr->bank, port, ret);

    return ret;
}

static void
ast_nvr_write(uint16_t port, uint8_t val, void *priv)
{
    ast_nvr_t *nvr = (ast_nvr_t *) priv;

    ast_nvr_log(nvr->log, "AST NVR Write [%02X:%02X] = %02X\n", nvr->bank, port, val);

    switch (port) {
        case 0x800 ... 0x8FF:
            nvr->addr = ((nvr->bank << 8) + (port - 0x800));
            nvr->ram[nvr->addr] = val;
            break;
        case 0xC00:
            nvr->bank = val;
        default:
            break;
    }

}

static void *
ast_nvr_init(const device_t *info)
{
    ast_nvr_t *nvr;
    FILE      *fp = NULL;
    int        c;

    nvr = (ast_nvr_t *) calloc(1, sizeof(ast_nvr_t));
    memset(nvr, 0x00, sizeof(ast_nvr_t));

    nvr->log = log_open("ASTNVR");

    nvr->size = 8192;

    /* Set up the NVR file's name */
    c       = strlen(machine_get_internal_name()) + 9;
    nvr->fn = (char *) calloc(1, (c + 1));
    sprintf(nvr->fn, "%s_sec.nvr", machine_get_internal_name());

    io_sethandler(0x0800, 0x100,
                  ast_nvr_read, NULL, NULL, ast_nvr_write, NULL, NULL, nvr);
    io_sethandler(0x0C00, 0x01,
                  ast_nvr_read, NULL, NULL, ast_nvr_write, NULL, NULL, nvr);

    fp = nvr_fopen(nvr->fn, "rb");

    nvr->ram = (uint8_t *) calloc(1, nvr->size);
    memset(nvr->ram, 0xff, nvr->size);
    if (fp != NULL) {
        if (fread(nvr->ram, 1, nvr->size, fp) != nvr->size)
            fatal("ast_nvr_init(): Error reading EEPROM data\n");
        fclose(fp);
    }

    return nvr;
}

static void
ast_nvr_close (void *priv)
{
    ast_nvr_t *nvr = (ast_nvr_t *) priv;
    FILE      *fp  = NULL;

    fp = nvr_fopen(nvr->fn, "wb");

    if (fp != NULL) {
        (void) fwrite(nvr->ram, nvr->size, 1, fp);
        fclose(fp);
    }

    if (nvr->ram != NULL)
        free(nvr->ram);

    if (nvr->log != NULL) {
        log_close(nvr->log);
        nvr->log = NULL;
    }

    free(nvr);
}

const device_t ast_nvr_device = {
    .name          = "AST Secondary NVRAM for Bravo MS",
    .internal_name = "ast_nvr",
    .flags         = 0,
    .local         = 0,
    .init          = ast_nvr_init,
    .close         = ast_nvr_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
