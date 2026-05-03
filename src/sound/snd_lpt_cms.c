/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          cmslpt emulation.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/lpt.h>
#include <86box/io.h>
#include <86box/sound.h>
#include <86box/snd_cms.h>
#include <86box/sound.h>
#include <86box/plat_unused.h>

#ifdef ENABLE_CMSLPT_LOG
uint8_t cmslpt_do_log = ENABLE_CMSLPT_LOG;

static void
cmslpt_log(const char *fmt, ...)
{
    va_list ap;

    if (cmslpt_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define cmslpt_log(fmt, ...)
#endif

typedef struct cmslpt_s {
    void      *lpt;
    cms_t     *cms;
    uint8_t    control;
    uint8_t    data_latch;
    uint8_t    status;
    pc_timer_t ready_timer;
} cmslpt_t;

static void
cmslpt_write_data(const uint8_t val, void *priv)
{
    cmslpt_t *const cms_lpt = (cmslpt_t *) priv;

    cmslpt_log("cmslpt_write_data: val=%02x\n", val);

    cms_lpt->data_latch = val;
}

static void
cmslpt_write_ctrl(const uint8_t val, void *priv)
{
    cmslpt_t *const cms_lpt = (cmslpt_t *) priv;
    const uint8_t prev = cms_lpt->control;

    cmslpt_log("cmslpt_write_ctrl: val=%02x\n", val);

    cms_lpt->control = val;
    if ((prev & 0x04) && !(val & 0x04)) {
          cms_lpt->status &= ~0x40; /* busy */
          timer_set_delay_u64(&cms_lpt->ready_timer, 2ULL * TIMER_USEC);
        /* Control signals:
           - Bit 0: Inverted A0 (0=address register, 1=data register)
           - Bit 1 with Bit 3: Chip select (bit1=1,bit3=0 = SAA#2; bit1=0,bit3=1 = SAA#1)
           - Bit 2: WR strobe (toggled 1->0->1 to write)
           - Bit 3 with Bit 1: Chip select
           
           Reference mapping:
           - 0x0C (1100): SAA #1 address (bit3=1, bit2=1, bit1=0, bit0=0)
           - 0x0D (1101): SAA #1 data    (bit3=1, bit2=1, bit1=0, bit0=1)
           - 0x06 (0110): SAA #2 address (bit3=0, bit2=1, bit1=1, bit0=0)
           - 0x07 (0111): SAA #2 data    (bit3=0, bit2=1, bit1=1, bit0=1) */

        /* Decode A0 from bit 0 (inverted): bit0=0->addr port, bit0=1->data port */
        const uint16_t port_offset = (val & 0x01) ? 0 : 1;  /* 1->port 0 (data), 0->port 1 (addr) */

        /* Decode chip select from bits 1 and 3 */
        if ((val & 0x08) && !(val & 0x02)) {
            /* First chip: bit3=1, bit1=0 */
            cmslpt_log("cmslpt: Writing to SAA #1 port %d, data=%02x\n", port_offset, cms_lpt->data_latch);
            cms_write(port_offset, cms_lpt->data_latch, cms_lpt->cms);
        } else if ((val & 0x02) && !(val & 0x08)) {
            /* Second chip: bit1=1, bit3=0 */
            cmslpt_log("cmslpt: Writing to SAA #2 port %d, data=%02x\n", 2 + port_offset, cms_lpt->data_latch);
            cms_write(2 + port_offset, cms_lpt->data_latch, cms_lpt->cms);
        }
    }
}

static void
cmslpt_ready_cb(void *priv)
{
    cmslpt_t *const cms_lpt = (cmslpt_t *) priv;

    cms_lpt->status |= 0x40; /* ready */
    lpt_irq(cms_lpt->lpt, 1);
}

static uint8_t
cmslpt_read_status(UNUSED(void *priv))
{
    const cmslpt_t *const cms_lpt = (cmslpt_t *) priv;

    return (cms_lpt->status & 0x40) ? 0x7f : 0x3f;
}

static void *
cmslpt_init(UNUSED(const device_t *info))
{
    cmslpt_t *const cmslpt = calloc(1, sizeof(cmslpt_t));
    cmslpt->lpt = lpt_attach(cmslpt_write_data,
                             cmslpt_write_ctrl,
                             NULL,
                             cmslpt_read_status,
                             NULL,
                             NULL,
                             NULL,
                             cmslpt);

    cmslpt_log("cmslpt_init\n");

    cmslpt->cms = calloc(1, sizeof(cms_t));
    
    sound_add_handler(cms_get_buffer, cmslpt->cms);

    cmslpt->status = 0x40;
    memset(&cmslpt->ready_timer, 0x00, sizeof(pc_timer_t));
    timer_add(&cmslpt->ready_timer, cmslpt_ready_cb, cmslpt, 0);

    return cmslpt;
}

static void
cmslpt_close(void *priv)
{
    cmslpt_t *const cms_lpt = (cmslpt_t *) priv;

    cmslpt_log("cmslpt_close\n");

    if (cms_lpt->cms)
        free(cms_lpt->cms);

    timer_disable(&cms_lpt->ready_timer);
    free(cms_lpt);
}

const device_t lpt_cms_device = {
    .name          = "Creative Music System-on-LPT (CMSLPT)",
    .internal_name = "lpt_cms",
    .flags         = DEVICE_LPT,
    .local         = 0,
    .init          = cmslpt_init,
    .close         = cmslpt_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
