/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of a port 80h POST diagnostic card.
 *
 *
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2020 RichardG.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/machine.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/postcard.h>
#include "cpu.h"

uint8_t         postcard_codes[POSTCARDS_NUM];
char            postcard_diags[5] = { 0 };

static uint16_t postcard_port;
static uint8_t  postcard_written[POSTCARDS_NUM];
static uint8_t  postcard_ports_num = 1;
static uint8_t  postcard_prev_codes[POSTCARDS_NUM];
static uint8_t  postcard_dell_mode = 0;
static char     postcard_prev_diags[5] = { 0 };
#define UISTR_LEN 32
static char postcard_str[UISTR_LEN]; /* UI output string */

extern void ui_sb_bugui(char *__str);

#ifdef ENABLE_POSTCARD_LOG
int postcard_do_log = ENABLE_POSTCARD_LOG;

static void
postcard_log(const char *fmt, ...)
{
    va_list ap;

    if (postcard_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
int postcard_do_log = 0;

#    define postcard_log(fmt, ...)
#endif

static void
postcard_setui(void)
{
    if (postcard_ports_num > 1) {
        char ps[2][POSTCARDS_NUM][3] = { { { 0 },
                                           { 0 },
                                        } };

        for (uint8_t i = 0; i < POSTCARDS_NUM; i++) {
            if (!postcard_written[i]) {
                snprintf(ps[0][i], sizeof(ps[0][i]), "--");
                snprintf(ps[1][i], sizeof(ps[1][i]), "--");
            } else if (postcard_written[i] == 1) {
                snprintf(ps[0][i], sizeof(ps[0][i]), "%02X", postcard_codes[i]);
                snprintf(ps[1][i], sizeof(ps[1][i]), "--");
            } else {
                snprintf(ps[0][i], sizeof(ps[0][i]), "%02X", postcard_codes[i]);
                snprintf(ps[1][i], sizeof(ps[1][i]), "%02X", postcard_prev_codes[i]);
            }
        }
 
        switch (postcard_ports_num) {
            default:
            case 2:
                snprintf(postcard_str, sizeof(postcard_str), "POST: %s%s %s%s",
                        ps[0][0], ps[0][1], ps[1][0], ps[1][1]);
                break;
            case 3:
                snprintf(postcard_str, sizeof(postcard_str), "POST: %s/%s%s %s/%s%s",
                        ps[0][0], ps[0][1], ps[0][2], ps[1][0], ps[1][1], ps[1][2]);
                break;
            case 4:
                snprintf(postcard_str, sizeof(postcard_str), "POST: %s%s/%s%s %s%s/%s%s",
                        ps[0][0], ps[0][1], ps[0][2], ps[0][3],
                        ps[1][0], ps[1][1], ps[1][2], ps[1][3]);
                break;
        }
    } else {
        char dell_diags[11] = { 0 };
        if (postcard_dell_mode) {
            if (!postcard_written[1])
                snprintf(dell_diags, sizeof(dell_diags), " ---- ----");
            else if (postcard_written[1] == 1)
                snprintf(dell_diags, sizeof(dell_diags), " %s ----", postcard_diags);
            else
                snprintf(dell_diags, sizeof(dell_diags), " %s %s", postcard_diags, postcard_prev_diags);
        }

        if (!postcard_written[0])
            snprintf(postcard_str, sizeof(postcard_str), "POST: -- --%s", dell_diags);
        else if (postcard_written[0] == 1)
            snprintf(postcard_str, sizeof(postcard_str), "POST: %02X --%s", postcard_codes[0], dell_diags);
        else
            snprintf(postcard_str, sizeof(postcard_str), "POST: %02X %02X%s", postcard_codes[0], postcard_prev_codes[0], dell_diags);
    }

    ui_sb_bugui(postcard_str);

    if (postcard_do_log) {
        /* log same string sent to the UI */
        postcard_log("[%04X:%08X] %s\n", CS, cpu_state.pc, postcard_str);
    }
}

static void
postcard_reset(void)
{
    memset(postcard_written, 0x00, POSTCARDS_NUM * sizeof(uint8_t));

    memset(postcard_codes, 0x00, POSTCARDS_NUM * sizeof(uint8_t));
    memset(postcard_prev_codes, 0x00, POSTCARDS_NUM * sizeof(uint8_t));

    memset(postcard_diags, 0x00, 5 * sizeof(char));
    memset(postcard_prev_diags, 0x00, 5 * sizeof(char));

    postcard_setui();
}

static void
postcard_write(uint16_t port, uint8_t val, UNUSED(void *priv))
{
    if (postcard_written[port & POSTCARD_MASK] &&
        (val == postcard_codes[port & POSTCARD_MASK]))
        return;

    postcard_prev_codes[port & POSTCARD_MASK] = postcard_codes[port & POSTCARD_MASK];
    postcard_codes[port & POSTCARD_MASK]      = val;
    if (postcard_written[port & POSTCARD_MASK] < 2)
        postcard_written[port & POSTCARD_MASK]++;

    postcard_setui();
}

static int
postcard_cmp_diags(uint32_t val)
{
    int   ret = 0;
    char *pv  = (char *) &val;

    for (int i = 0; i < 4; i++)
        ret = ret || (pv[i] != postcard_diags[3 - i]);

    return ret;
}

static void
postcard_writel(uint16_t port, uint32_t val, UNUSED(void *priv))
{
    char *pv  = (char *) &val;

    if (postcard_written[1] && !postcard_cmp_diags(val))
        return;

    *(uint32_t *) postcard_prev_diags = *(uint32_t *) postcard_diags;
    for (int i = 0; i < 4; i++)
        postcard_diags[i] = pv[3 - i];
    if (postcard_written[1] < 2)
        postcard_written[1]++;

    postcard_setui();
}

static void *
postcard_init(UNUSED(const device_t *info))
{
    postcard_ports_num = 1;

    if (machine_has_bus(machine, MACHINE_BUS_MCA))
        postcard_port = 0x680; /* MCA machines */
    else if (strstr(machines[machine].name, " PS/2 ") ||
             strstr(machine_getname_ex(machine), " PS/1 "))
        postcard_port = 0x190; /* ISA PS/2 machines */
    else if (strstr(machines[machine].name, " IBM XT "))
        postcard_port = 0x60; /* IBM XT */
    else if (strstr(machines[machine].name, " IBM PCjr")) {
        postcard_port = 0x10; /* IBM PCjr */
        postcard_ports_num = 3; /* IBM PCjr error ports 11h and 12h */
    } else if (strstr(machines[machine].name, " Compaq ") && 
               !strstr(machines[machine].name, " Presario ") &&
               !strstr(machines[machine].name, " ProSignia "))
        postcard_port = 0x84; /* ISA Compaq machines */
    else if (strstr(machines[machine].name, "Olivetti"))
        postcard_port = 0x378; /* Olivetti machines */
    else if (!strcmp(machines[machine].internal_name, "isa486c"))
        postcard_port = 0x5080; /* ASUS ISA-486C */
    else
        postcard_port = 0x80; /* AT and clone machines */
    postcard_log("POST card initializing on port %04Xh\n", postcard_port);

    postcard_reset();

    if (postcard_port)
        io_sethandler(postcard_port, postcard_ports_num,
                      NULL, NULL, NULL, postcard_write, NULL, NULL, NULL);

    postcard_dell_mode = strstr(machines[machine].name, " Dell ") &&
                         (machine_get_chipset(machine) >= MACHINE_CHIPSET_INTEL_430FX);
    if (postcard_dell_mode)
        io_sethandler(is486 ? 0x00e0 : 0x00e4, 0x0001,
                      NULL, NULL, NULL, NULL, NULL, postcard_writel, NULL);

    return postcard_write;
}

static void
postcard_close(UNUSED(void *priv))
{
    if (postcard_port)
        io_removehandler(postcard_port, postcard_ports_num,
                         NULL, NULL, NULL, postcard_write, NULL, NULL, NULL);
}

const device_t postcard_device = {
    .name          = "POST Card",
    .internal_name = "postcard",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = postcard_init,
    .close         = postcard_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
