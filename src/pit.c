/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Intel 8253/8254 Programmable Interval
 *          Timer.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2019 Miran Grca.
 */
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/cassette.h>
#include <86box/dma.h>
#include <86box/io.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/pit_fast.h>
#include <86box/ppi.h>
#include <86box/machine.h>
#include <86box/sound.h>
#include <86box/snd_speaker.h>
#include <86box/video.h>
#include <86box/plat_unused.h>

pit_intf_t pit_devs[2];

double cpuclock;
double PITCONSTD;
double PAS16CONSTD;
double PAS16CONST2D;
double PASSCSICONSTD;
double SYSCLK;
double isa_timing;
double bus_timing;
double pci_timing;
double agp_timing;
double PCICLK;
double AGPCLK;

uint64_t PITCONST;
uint64_t PAS16CONST;
uint64_t PAS16CONST2;
uint64_t PASSCSICONST;
uint64_t ISACONST;
uint64_t CGACONST;
uint64_t MDACONST;
uint64_t HERCCONST;
uint64_t VGACONST1;
uint64_t VGACONST2;
uint64_t RTCCONST;
uint64_t ACPICONST;

int refresh_at_enable = 1;
int io_delay          = 5;

int64_t firsttime = 1;

#define PIT_PS2          16  /* The PIT is the PS/2's second PIT. */
#define PIT_EXT_IO       32  /* The PIT has externally specified port I/O. */
#define PIT_CUSTOM_CLOCK 64  /* The PIT uses custom clock inputs provided by another provider. */
#define PIT_SECONDARY    128 /* The PIT is secondary (ports 0048-004B). */

#ifdef ENABLE_PIT_LOG
int pit_do_log = ENABLE_PIT_LOG;

static void
pit_log(const char *fmt, ...)
{
    va_list ap;

    if (pit_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define pit_log(fmt, ...)
#endif

static void
ctr_set_out(ctr_t *ctr, int out, void *priv)
{
    pit_t *pit = (pit_t *)priv;

    if (ctr == NULL)
        return;

    if (ctr->out_func != NULL)
        ctr->out_func(out, ctr->out, pit);

    ctr->out = out;
}


/* 86BOX_MACHINE_EXACT_V1
 * NOCONA_PIT_EXACT_CONSOLIDATED_V1
 * Bridge the public PIT API to the pin-clocked core. */
static pitx_type_t
pit_exact_type(const pit_t *pit)
{
    return (pit->flags & PIT_8254) ? PITX_8254 : PITX_8253;
}

static unsigned
pit_exact_index(const pit_t *pit, const ctr_t *ctr)
{
    const ptrdiff_t index = ctr - pit->counters;
    return (index >= 0 && index < NUM_COUNTERS) ? (unsigned)index : 0u;
}

static int
pit_exact_bcd_count(uint16_t value)
{
    if (value == 0)
        return 10000;
    return ((value >> 12) & 0x0f) * 1000 +
           ((value >> 8) & 0x0f) * 100 +
           ((value >> 4) & 0x0f) * 10 + (value & 0x0f);
}

static void
pit_exact_sync_channel(pit_t *pit, unsigned index)
{
    pitx_channel_t *x = &pit->exact.channel[index];
    ctr_t *ctr = &pit->counters[index];
    const int new_out = x->output ? 1 : 0;
    ctr->ctrl = x->control;
    ctr->m = x->mode;
    ctr->bcd = x->bcd ? 1 : 0;
    ctr->l = x->count_register;
    ctr->lback = ctr->lback2 = ctr->l;
    ctr->count = (x->counting_element == 0x10000u) ? 0x10000 : (int)x->counting_element;
    ctr->null_count = x->null_count ? 1 : 0;
    ctr->gate = x->gate ? 1 : 0;
    ctr->state = (int)x->state;
    ctr->rm = (int)x->rw_mode | (x->read_phase ? 0x80 : 0);
    ctr->wm = (int)x->rw_mode | (x->write_phase ? 0x80 : 0);
    ctr->rl = x->output_latch;
    ctr->latched = x->count_latched ? ((x->rw_mode == 3u) ? (x->read_phase ? 1 : 2) : 1) : 0;
    ctr->read_status = x->status_latch;
    ctr->do_read_status = x->status_latched ? 1 : 0;
    ctr->incomplete = x->incomplete_reload ? 1 : 0;
    if (ctr->out != new_out)
        ctr_set_out(ctr, new_out, pit);
}

static void
pit_exact_sync_all(pit_t *pit)
{
    for (unsigned i = 0; i < NUM_COUNTERS; ++i)
        pit_exact_sync_channel(pit, i);
}

static void
pit_exact_notify_load(pit_t *pit, unsigned index)
{
    ctr_t *ctr = &pit->counters[index];
    const pitx_channel_t *x = &pit->exact.channel[index];
    if (ctr->load_func == NULL)
        return;
    const int count = x->bcd ? pit_exact_bcd_count(x->count_register)
                             : (x->count_register ? x->count_register : 0x10000);
    ctr->load_func(x->mode, count);
}

static void
ctr_tick(ctr_t *ctr, void *priv)
{
    pit_t *pit = (pit_t *)priv;
    const unsigned index = pit_exact_index(pit, ctr);
    pitx_tick_channel(&pit->exact, index);
    pit_exact_sync_channel(pit, index);
}


void
ctr_clock(void *data, int counter_id)
{
    pit_t *pit = (pit_t *)data;
    ctr_t *ctr = &pit->counters[counter_id];
    if (ctr->using_timer)
        return;
    pitx_tick_channel(&pit->exact, (unsigned)counter_id);
    pit_exact_sync_channel(pit, (unsigned)counter_id);
}


uint16_t
pit_ctr_get_count(void *data, int counter_id)
{
    const pit_t *pit = (const pit_t *)data;
    return pit->exact.channel[counter_id].count_register;
}


int
pit_ctr_get_outlevel(void *data, int counter_id)
{
    const pit_t *pit = (const pit_t *)data;
    return pitx_get_output(&pit->exact, (unsigned)counter_id) ? 1 : 0;
}


void
pit_ctr_set_load_func(void *data, int counter_id, void (*func)(uint8_t new_m, int new_count))
{
    if (data == NULL)
        return;

    pit_t *pit = (pit_t *) data;
    ctr_t *ctr = &pit->counters[counter_id];

    ctr->load_func = func;
}

void
pit_ctr_set_out_func(void *data, int counter_id, void (*func)(int new_out, int old_out, void *priv))
{
    if (data == NULL)
        return;

    pit_t *pit = (pit_t *) data;
    ctr_t *ctr = &pit->counters[counter_id];

    ctr->out_func = func;
}

void
pit_ctr_set_gate(void *data, int counter_id, int gate)
{
    pit_t *pit = (pit_t *)data;
    if (pit == NULL || counter_id < 0 || counter_id >= NUM_COUNTERS)
        return;
    pitx_set_gate(&pit->exact, (unsigned)counter_id, gate != 0);
    pit_exact_sync_channel(pit, (unsigned)counter_id);
}


static __inline void
pit_ctr_set_clock_common(ctr_t *ctr, int clock, void *priv)
{
    pit_t *pit = (pit_t *)priv;
    const int old = ctr->clock;
    ctr->clock = clock;
    if (ctr->using_timer && old && !clock)
        ctr_tick(ctr, pit);
}


void
pit_ctr_set_clock(ctr_t *ctr, int clock, void *priv)
{
    pit_ctr_set_clock_common(ctr, clock, priv);
}

void
pit_ctr_set_using_timer(void *data, int counter_id, int using_timer)
{
    if (tsc > 0)
        timer_process();
    pit_t *pit       = (pit_t *) data;
    ctr_t *ctr       = &pit->counters[counter_id];
    ctr->using_timer = using_timer;
}

static void
pit_timer_over(void *priv)
{
    pit_t *dev = (pit_t *) priv;

    dev->clock ^= 1;

    for (uint8_t i = 0; i < NUM_COUNTERS; i++)
        pit_ctr_set_clock_common(&dev->counters[i], dev->clock, dev);

    timer_advance_u64(&dev->callback_timer, dev->pit_const >> 1ULL);
}

static void
pit_write(uint16_t addr, uint8_t val, void *priv)
{
    pit_t *dev = (pit_t *)priv;
    const unsigned port = addr & 3u;
    if (port == 3u) {
        dev->ctrl = val;
        pitx_control_write(&dev->exact, val);
        pit_exact_sync_all(dev);
        return;
    }
    pitx_channel_t *x = &dev->exact.channel[port];
    const uint8_t rw = x->rw_mode;
    const uint8_t old_phase = x->write_phase;
    pitx_data_write(&dev->exact, port, val);
    pit_exact_sync_channel(dev, port);
    if (rw == 1u || rw == 2u || (rw == 3u && old_phase == 1u))
        pit_exact_notify_load(dev, port);
}


extern uint8_t *ram;

uint8_t
pit_read_reg(void *priv, uint8_t reg)
{
    pit_t  *dev = (pit_t *) priv;
    uint8_t ret = 0xff;

    switch (reg) {
        case 0x00:
        case 0x02:
        case 0x04:
            ret = dev->counters[reg >> 1].l & 0xff;
            break;
        case 0x01:
        case 0x03:
        case 0x05:
            ret = (dev->counters[reg >> 1].l >> 8) & 0xff;
            break;
        case 0x06:
            ret = dev->ctrl;
            break;
        case 0x07:
            /* The SiS 551x datasheet is unclear about how exactly
               this register is structured.
               Update: But the SiS 5571 datasheet is clear. */
            ret = (dev->counters[0].rm & 0x80) ? 0x01 : 0x00;
            ret |= (dev->counters[1].rm & 0x80) ? 0x02 : 0x00;
            ret |= (dev->counters[2].rm & 0x80) ? 0x04 : 0x00;
            ret |= (dev->counters[0].wm & 0x80) ? 0x08 : 0x00;
            ret |= (dev->counters[1].wm & 0x80) ? 0x10 : 0x00;
            ret |= (dev->counters[2].wm & 0x80) ? 0x20 : 0x00;
            break;
    }

    return ret;
}

static uint8_t
pit_read(uint16_t addr, void *priv)
{
    pit_t *dev = (pit_t *)priv;
    const unsigned port = addr & 3u;
    if (port == 3u)
        return (dev->flags & PIT_8254) ? dev->ctrl : 0x00;
    const uint8_t value = pitx_data_read(&dev->exact, port);
    pit_exact_sync_channel(dev, port);
    return value;
}


void
pit_irq0_timer_ps2(int new_out, int old_out, UNUSED(void *priv))
{
    if (new_out && !old_out) {
        picint(1);
        pit_devs[1].set_gate(pit_devs[1].data, 0, 1);
    }

    if (!new_out)
        picintc(1);

    if (!new_out && old_out)
        pit_devs[1].ctr_clock(pit_devs[1].data, 0);
}


void
pit_refresh_timer_xt(int new_out, int old_out, UNUSED(void *priv))
{
    if (new_out && !old_out)
        dma_xt_refresh_request();
}


void
pit_refresh_timer_at(int new_out, int old_out, UNUSED(void *priv))
{
    if (refresh_at_enable && new_out && !old_out)
        ppi.pb ^= 0x10;
}

void
pit_speaker_timer(int new_out, UNUSED(int old_out), UNUSED(void *priv))
{
    int l;

    if (cassette != NULL)
        pc_cas_set_out(cassette, new_out);

    speaker_update();

    uint16_t count = pit_devs[0].get_count(pit_devs[0].data, 2);
    l              = count ? count : 0x10000;
    if (l < 25)
        speakon = 0;
    else
        speakon = new_out;

    ppispeakon = new_out;
}

void
pit_nmi_timer_ps2(int new_out, UNUSED(int old_out), UNUSED(void *priv))
{
    nmi = new_out;

    if (nmi)
        nmi_auto_clear = 1;
}

static void
ctr_reset(ctr_t *ctr)
{
    ctr->ctrl        = 0;
    ctr->m           = 0;
    ctr->gate        = 0;
    ctr->l           = 0xffff;
    ctr->using_timer = 1;
    ctr->state       = 0;
    ctr->null_count  = 1;

    ctr->latch = 0;

    ctr->s1_det = 0;
    ctr->l_det  = 0;
}

void
pit_device_reset(pit_t *dev)
{
    dev->clock = 0;
    for (uint8_t i = 0; i < NUM_COUNTERS; i++)
        ctr_reset(&dev->counters[i]);
    pitx_reset(&dev->exact, pit_exact_type(dev));
    pit_exact_sync_all(dev);
}


void
pit_reset(pit_t *dev)
{
    /* Preserve out/load callbacks, timer registration, flags and device-private
     * wiring installed by the machine. Only counter state is reset. */
    dev->clock = 0;
    for (uint8_t i = 0; i < NUM_COUNTERS; i++)
        ctr_reset(&dev->counters[i]);
    pitx_reset(&dev->exact, pit_exact_type(dev));
    pitx_set_gate(&dev->exact, 2u, false);
    pit_exact_sync_all(dev);
}


void
pit_handler(int set, uint16_t base, int size, void *priv)
{
    io_handler(set, base, size, pit_read, NULL, NULL, pit_write, NULL, NULL, priv);
}

void
pit_set_pit_const(void *data, uint64_t pit_const)
{
    pit_t *pit = (pit_t *) data;

    pit->pit_const = pit_const;
}

static void
pit_speed_changed(void *priv)
{
    pit_set_pit_const(priv, PITCONST);
}

static void
pit_close(void *priv)
{
    pit_t *dev = (pit_t *) priv;

    if (dev == pit_devs[0].data)
        pit_devs[0].data = NULL;

    if (dev == pit_devs[1].data)
        pit_devs[1].data = NULL;

    if (dev != NULL)
        free(dev);
}

static void *
pit_init(const device_t *info)
{
    pit_t *dev = (pit_t *)calloc(1, sizeof(pit_t));
    if (dev == NULL)
        return NULL;
    dev->flags = info->local;
    pit_reset(dev);
    pit_set_pit_const(dev, PITCONST);
    if (!(dev->flags & PIT_PS2) && !(dev->flags & PIT_CUSTOM_CLOCK)) {
        timer_add(&dev->callback_timer, pit_timer_over, (void *)dev, 0);
        timer_set_delay_u64(&dev->callback_timer, dev->pit_const >> 1ULL);
    }
    dev->dev_priv = NULL;
    if (!(dev->flags & PIT_EXT_IO)) {
        io_sethandler((dev->flags & PIT_SECONDARY) ? 0x0048 : 0x0040, 0x0004,
                      pit_read, NULL, NULL, pit_write, NULL, NULL, dev);
    }
    return dev;
}


const device_t i8253_device = {
    .name          = "Intel 8253/8253-5 Programmable Interval Timer",
    .internal_name = "i8253",
    .flags         = DEVICE_ISA | DEVICE_PIT,
    .local         = PIT_8253,
    .init          = pit_init,
    .close         = pit_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = pit_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t i8253_ext_io_device = {
    .name          = "Intel 8253 Programmable Interval Timer (External I/O)",
    .internal_name = "i8253_ext_io",
    .flags         = DEVICE_ISA,
    .local         = PIT_8253 | PIT_EXT_IO,
    .init          = pit_init,
    .close         = pit_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t i8254_device = {
    .name          = "Intel 8254 Programmable Interval Timer",
    .internal_name = "i8254",
    .flags         = DEVICE_ISA | DEVICE_PIT,
    .local         = PIT_8254,
    .init          = pit_init,
    .close         = pit_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = pit_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t i8254_sec_device = {
    .name          = "Intel 8254 Programmable Interval Timer (Secondary)",
    .internal_name = "i8254_sec",
    .flags         = DEVICE_ISA,
    .local         = PIT_8254 | PIT_SECONDARY,
    .init          = pit_init,
    .close         = pit_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = pit_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t i8254_ext_io_device = {
    .name          = "Intel 8254 Programmable Interval Timer (External I/O)",
    .internal_name = "i8254_ext_io",
    .flags         = DEVICE_ISA,
    .local         = PIT_8254 | PIT_EXT_IO,
    .init          = pit_init,
    .close         = pit_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t i8254_ps2_device = {
    .name          = "Intel 8254 Programmable Interval Timer (PS/2)",
    .internal_name = "i8254_ps2",
    .flags         = DEVICE_ISA,
    .local         = PIT_8254 | PIT_PS2 | PIT_EXT_IO,
    .init          = pit_init,
    .close         = pit_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = pit_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};

pit_t *
pit_common_init(int type, void (*out0)(int new_out, int old_out, void *priv), void (*out1)(int new_out, int old_out, void *priv))
{
    void *pit;

    pit_intf_t *pit_intf = &pit_devs[0];

    switch (type) {
        default:
        case PIT_8253:
            pit       = device_add(&i8253_device);
            *pit_intf = pit_classic_intf;
            break;
        case PIT_8254:
            pit       = device_add(&i8254_device);
            *pit_intf = pit_classic_intf;
            break;
        case PIT_8253_FAST:
            pit       = device_add(&i8253_fast_device);
            *pit_intf = pit_fast_intf;
            break;
        case PIT_8254_FAST:
            pit       = device_add(&i8254_fast_device);
            *pit_intf = pit_fast_intf;
            break;
    }

    pit_intf->data = pit;

    for (uint8_t i = 0; i < 3; i++) {
        pit_intf->set_gate(pit_intf->data, i, 1);
        pit_intf->set_using_timer(pit_intf->data, i, 1);
    }

    pit_intf->set_out_func(pit_intf->data, 0, out0);
    pit_intf->set_out_func(pit_intf->data, 1, out1);
    pit_intf->set_out_func(pit_intf->data, 2, pit_speaker_timer);
    pit_intf->set_load_func(pit_intf->data, 2, speaker_set_count);

    pit_intf->set_gate(pit_intf->data, 2, 0);

    return pit;
}

pit_t *
pit_ps2_init(int type)
{
    void *pit;

    pit_intf_t *ps2_pit = &pit_devs[1];

    switch (type) {
        default:
        case PIT_8254:
            pit      = device_add(&i8254_ps2_device);
            *ps2_pit = pit_classic_intf;
            break;

        case PIT_8254_FAST:
            pit      = device_add(&i8254_ps2_fast_device);
            *ps2_pit = pit_fast_intf;
            break;
    }

    ps2_pit->data = pit;

    ps2_pit->set_gate(ps2_pit->data, 0, 0);
    for (int i = 0; i < 3; i++) {
        ps2_pit->set_using_timer(ps2_pit->data, i, 0);
    }

    io_sethandler(0x0044, 0x0001, ps2_pit->read, NULL, NULL, ps2_pit->write, NULL, NULL, pit);
    io_sethandler(0x0047, 0x0001, ps2_pit->read, NULL, NULL, ps2_pit->write, NULL, NULL, pit);

    pit_devs[0].set_out_func(pit_devs[0].data, 0, pit_irq0_timer_ps2);
    ps2_pit->set_out_func(ps2_pit->data, 0, pit_nmi_timer_ps2);

    return pit;
}

void
pit_change_pas16_consts(double prescale)
{
    PAS16CONST  = (uint64_t) ((PAS16CONSTD / prescale) * (double) (1ULL << 32));
    PAS16CONST2 = (uint64_t) ((PAS16CONST2D / prescale) * (double) (1ULL << 32));
}

void
pit_set_clock(uint32_t clock)
{
    /* Set default CPU/crystal clock and xt_cpu_multi. */
    if (cpu_s->cpu_type >= CPU_286) {
        uint32_t remainder = (clock % 100000000);
        if (remainder == 66666666)
            cpuclock = (double) (clock - remainder) + (200000000.0 / 3.0);
        else if (remainder == 33333333)
            cpuclock = (double) (clock - remainder) + (100000000.0 / 3.0);
        else
            cpuclock = (double) clock;

        PITCONSTD    = (cpuclock / 1193182.0);
        PITCONST     = (uint64_t) (PITCONSTD * (double) (1ULL << 32));
#ifdef IMPRECISE_CGACONST
        CGACONST     = (uint64_t) ((cpuclock / (19687503.0 / 11.0)) * (double) (1ULL << 32));
#else
        CGACONST     = (uint64_t) ((cpuclock / (157500000.0 / 88.0)) * (double) (1ULL << 32));
#endif
        ISACONST     = (uint64_t) ((cpuclock / (double) cpu_isa_speed) * (double) (1ULL << 32));
        xt_cpu_multi = 1ULL;
    } else {
        cpuclock     = (157500000.0 / 11.0);
        PITCONSTD    = 12.0;
        PITCONST     = (12ULL << 32ULL);
        CGACONST     = (8ULL << 32ULL);
        xt_cpu_multi = 3ULL;

        switch (cpu_s->rspeed) {
            case 7159092:
                if (cpu_s->cpu_flags & CPU_ALTERNATE_XTAL) {
                    cpuclock     = 28636368.0;
                    xt_cpu_multi = 4ULL;
                } else
                    xt_cpu_multi = 2ULL;
                break;

            case 8000000:
                cpuclock = 24000000.0;
                break;
            case 9545456:
                cpuclock = 28636368.0;
                break;
            case 10000000:
                cpuclock = 30000000.0;
                break;
            case 12000000:
                cpuclock = 36000000.0;
                break;
            case 16000000:
                cpuclock = 48000000.0;
                break;

            default:
                if (cpu_s->cpu_flags & CPU_ALTERNATE_XTAL) {
                    cpuclock     = 28636368.0;
                    xt_cpu_multi = 6ULL;
                }
                break;
        }

        if (cpuclock == 28636368.0) {
            PITCONSTD = 24.0;
            PITCONST  = (24ULL << 32LL);
            CGACONST  = (16ULL << 32LL);
        } else if (cpuclock != 14318184.0) {
            PITCONSTD = (cpuclock / 1193182.0);
            PITCONST  = (uint64_t) (PITCONSTD * (double) (1ULL << 32));
#ifdef IMPRECISE_CGACONST
            CGACONST  = (uint64_t) ((cpuclock / (19687503.0 / 11.0)) * (double) (1ULL << 32));
#else
            CGACONST  = (uint64_t) ((cpuclock / (157500000.0 / 88.0)) * (double) (1ULL << 32));
#endif
        }
        
        if (machines[machine].init == machine_xt_ibm5550_init) {
            PITCONSTD = (cpuclock / 2000000.0); /* CLK input 2.0 MHz */
            PITCONST  = (uint64_t) (PITCONSTD * (double) (1ULL << 32));
        }

        if (cpuclock == 24000000.0)
            ISACONST     = (uint64_t) ((cpuclock / 14318184.0) * (double) (1ULL << 32));
        else
            ISACONST = (1ULL << 32ULL);
    }
    xt_cpu_multi <<= 32ULL;

    /* Delay for empty I/O ports. */
    io_delay = (int) round(((double) cpu_s->rspeed) / 3000000.0);

#ifdef WRONG_MDACONST
    MDACONST  = (uint64_t) (cpuclock / 2032125.0 * (double) (1ULL << 32));
#else
    MDACONST  = (uint64_t) (cpuclock / (16257000.0 / 9.0) * (double) (1ULL << 32));
#endif
    HERCCONST = (uint64_t) (cpuclock / 16000000.0 * (double) (1ULL << 32));
    VGACONST1 = (uint64_t) (cpuclock / 25175000.0 * (double) (1ULL << 32));
    VGACONST2 = (uint64_t) (cpuclock / 28322000.0 * (double) (1ULL << 32));
    RTCCONST  = (uint64_t) (cpuclock / 32768.0 * (double) (1ULL << 32));

    TIMER_USEC = (uint64_t) ((cpuclock / 1000000.0) * (double) (1ULL << 32));

    PAS16CONSTD = (cpuclock / 441000.0);
    PAS16CONST  = (uint64_t) (PAS16CONSTD * (double) (1ULL << 32));

    PAS16CONST2D = (cpuclock / 1008000.0);
    PAS16CONST2  = (uint64_t) (PAS16CONST2D * (double) (1ULL << 32));

    PASSCSICONSTD = (cpuclock / (28224000.0 / 14.0));
    PASSCSICONST  = (uint64_t) (PASSCSICONSTD * (double) (1ULL << 32));

    isa_timing = (cpuclock / (double) cpu_isa_speed);
    if (cpu_64bitbus)
        bus_timing = (cpuclock / (cpu_busspeed / 2));
    else
        bus_timing = (cpuclock / cpu_busspeed);
    pci_timing = (cpuclock / (double) cpu_pci_speed);
    agp_timing = (cpuclock / (double) cpu_agp_speed);

    /* PCICLK in us for use with timer_on_auto(). */
    PCICLK = pci_timing / (cpuclock / 1000000.0);
    AGPCLK = agp_timing / (cpuclock / 1000000.0);

    if (cpu_busspeed >= 30000000)
        SYSCLK = bus_timing * 4.0;
    else
        SYSCLK = bus_timing * 3.0;

    video_update_timing();

    device_speed_changed();
}

const pit_intf_t pit_classic_intf = {
    .read            = &pit_read,
    .write           = &pit_write,
    .get_count       = &pit_ctr_get_count,
    .get_outlevel    = &pit_ctr_get_outlevel,
    .set_gate        = &pit_ctr_set_gate,
    .set_using_timer = &pit_ctr_set_using_timer,
    .set_out_func    = &pit_ctr_set_out_func,
    .set_load_func   = &pit_ctr_set_load_func,
    .ctr_clock       = &ctr_clock,
    .set_pit_const   = &pit_set_pit_const,
    .data            = NULL,
};
