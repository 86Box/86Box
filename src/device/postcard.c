/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of a port 80h POST diagnostic card.
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


static uint16_t	postcard_port;
static uint8_t	postcard_written;
static uint8_t	postcard_code, postcard_prev_code;
#define UISTR_LEN	13
static char	postcard_str[UISTR_LEN];	/* UI output string */


extern void	ui_sb_bugui(char *__str);


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

#define postcard_log(fmt, ...)
#endif


static void
postcard_setui(void)
{
    if (!postcard_written)
	sprintf(postcard_str, "POST: -- --");
    else if (postcard_written == 1)
	sprintf(postcard_str, "POST: %02X --", postcard_code);
    else
	sprintf(postcard_str, "POST: %02X %02X", postcard_code, postcard_prev_code);

    ui_sb_bugui(postcard_str);

    if (postcard_do_log) {
	/* log same string sent to the UI */
	postcard_log("[%04X:%08X] %s\n", CS, cpu_state.pc, postcard_str);
    }
}


static void
postcard_reset(void)
{
    postcard_written = 0;
    postcard_code = postcard_prev_code = 0x00;

    postcard_setui();
}


static void
postcard_write(uint16_t port, uint8_t val, void *priv)
{
    if (postcard_written && (val == postcard_code))
	return;

    postcard_prev_code = postcard_code;
    postcard_code = val;
    if (postcard_written < 2)
	postcard_written++;

    postcard_setui();
}


static void *
postcard_init(const device_t *info)
{
    postcard_reset();

    if (machine_has_bus(machine, MACHINE_BUS_MCA))
	postcard_port = 0x680; /* MCA machines */
    else if (strstr(machines[machine].name, " PS/2 ") || strstr(machine_getname_ex(machine), " PS/1 "))
	postcard_port = 0x190; /* ISA PS/2 machines */
    else if (strstr(machines[machine].name, " IBM XT "))
	postcard_port = 0x60;  /* IBM XT */
    else if (strstr(machines[machine].name, " IBM PCjr"))
	postcard_port = 0x10;  /* IBM PCjr */
    else if (strstr(machines[machine].name, " Compaq ") && !machine_has_bus(machine, MACHINE_BUS_PCI))
	postcard_port = 0x84;  /* ISA Compaq machines */
    else
	postcard_port = 0x80;  /* AT and clone machines */
    postcard_log("POST card initializing on port %04Xh\n", postcard_port);

    if (postcard_port) io_sethandler(postcard_port, 1,
		  NULL, NULL, NULL, postcard_write, NULL, NULL,  NULL);

    return postcard_write;
}


static void
postcard_close(UNUSED(void *priv))
{
    if (postcard_port) io_removehandler(postcard_port, 1,
		     NULL, NULL, NULL, postcard_write, NULL, NULL,  NULL);
}


const device_t postcard_device = {
    .name = "POST Card",
    .internal_name = "postcard",
    .flags = DEVICE_ISA,
    .local = 0,
    .init = postcard_init,
    .close = postcard_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
