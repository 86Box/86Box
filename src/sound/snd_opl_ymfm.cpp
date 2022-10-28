/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Interface to the YMFM emulator.
 *
 *
 * Authors: Adrien Moulin, <adrien@elyosh.org>
 *
 *          Copyright 2022 Adrien Moulin.
 */
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "ymfm/ymfm_opl.h"

extern "C" {
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/sound.h>
#include <86box/snd_opl.h>
}

#define RSM_FRAC 10

enum {
    FLAG_CYCLES = (1 << 0)
};

class YMFMChipBase {
public:
    YMFMChipBase(uint32_t clock, fm_type type, uint32_t samplerate)
        : m_buf_pos(0)
        , m_flags(0)
        , m_type(type)
    {
        memset(m_buffer, 0, sizeof(m_buffer));
    }

    virtual ~YMFMChipBase()
    {
    }

    fm_type  type() const { return m_type; }
    int8_t   flags() const { return m_flags; }
    void     set_do_cycles(int8_t do_cycles) { do_cycles ? m_flags |= FLAG_CYCLES : m_flags &= ~FLAG_CYCLES; }
    int32_t *buffer() const { return (int32_t *) m_buffer; }
    void     reset_buffer() { m_buf_pos = 0; }

    virtual uint32_t sample_rate() const = 0;

    virtual void     write(uint16_t addr, uint8_t data)                      = 0;
    virtual void     generate(int32_t *data, uint32_t num_samples)           = 0;
    virtual void     generate_resampled(int32_t *data, uint32_t num_samples) = 0;
    virtual int32_t *update()                                                = 0;
    virtual uint8_t  read(uint16_t addr)                                     = 0;

protected:
    int32_t m_buffer[SOUNDBUFLEN * 2];
    int     m_buf_pos;
    int8_t  m_flags;
    fm_type m_type;
};

template <typename ChipType>
class YMFMChip : public YMFMChipBase, public ymfm::ymfm_interface {
public:
    YMFMChip(uint32_t clock, fm_type type, uint32_t samplerate)
        : YMFMChipBase(clock, type, samplerate)
        , m_chip(*this)
        , m_clock(clock)
        , m_samplecnt(0)
    {
        memset(m_samples, 0, sizeof(m_samples));
        memset(m_oldsamples, 0, sizeof(m_oldsamples));
        m_rateratio   = (samplerate << RSM_FRAC) / m_chip.sample_rate(m_clock);
        m_clock_us    = 1000000 / (double) m_clock;
        m_subtract[0] = 80.0;
        m_subtract[1] = 320.0;
        m_type        = type;

        timer_add(&m_timers[0], YMFMChip::timer1, this, 0);
        timer_add(&m_timers[1], YMFMChip::timer2, this, 0);
    }

    virtual uint32_t sample_rate() const override
    {
        return m_chip.sample_rate(m_clock);
    }

    virtual void ymfm_set_timer(uint32_t tnum, int32_t duration_in_clocks) override
    {
        if (tnum > 1)
            return;

        pc_timer_t *timer = &m_timers[tnum];
        if (duration_in_clocks < 0)
            timer_stop(timer);
        else {
            double period = m_clock_us * duration_in_clocks;
            if (period < m_subtract[tnum])
                m_engine->engine_timer_expired(tnum);
            else
                timer_on_auto(timer, period);
        }
    }

    virtual void generate(int32_t *data, uint32_t num_samples) override
    {
        for (uint32_t i = 0; i < num_samples; i++) {
            m_chip.generate(&m_output);
            if (ChipType::OUTPUTS == 1) {
                *data++ = m_output.data[0];
                *data++ = m_output.data[0];
            } else {
                *data++ = m_output.data[0];
                *data++ = m_output.data[1 % ChipType::OUTPUTS];
            }
        }
    }

    virtual void generate_resampled(int32_t *data, uint32_t num_samples) override
    {
        for (uint32_t i = 0; i < num_samples; i++) {
            while (m_samplecnt >= m_rateratio) {
                m_oldsamples[0] = m_samples[0];
                m_oldsamples[1] = m_samples[1];
                m_chip.generate(&m_output);
                if (ChipType::OUTPUTS == 1) {
                    m_samples[0] = m_output.data[0];
                    m_samples[1] = m_output.data[0];
                } else {
                    m_samples[0] = m_output.data[0];
                    m_samples[1] = m_output.data[1 % ChipType::OUTPUTS];
                }
                m_samplecnt -= m_rateratio;
            }

            *data++ = ((int32_t) ((m_oldsamples[0] * (m_rateratio - m_samplecnt)
                                   + m_samples[0] * m_samplecnt)
                                  / m_rateratio));
            *data++ = ((int32_t) ((m_oldsamples[1] * (m_rateratio - m_samplecnt)
                                   + m_samples[1] * m_samplecnt)
                                  / m_rateratio));

            m_samplecnt += 1 << RSM_FRAC;
        }
    }

    virtual int32_t *update() override
    {
        if (m_buf_pos >= sound_pos_global)
            return m_buffer;

        generate_resampled(&m_buffer[m_buf_pos * 2], sound_pos_global - m_buf_pos);

        for (; m_buf_pos < sound_pos_global; m_buf_pos++) {
            m_buffer[m_buf_pos * 2] /= 2;
            m_buffer[(m_buf_pos * 2) + 1] /= 2;
        }

        return m_buffer;
    }

    virtual void write(uint16_t addr, uint8_t data) override
    {
        m_chip.write(addr, data);
    }

    virtual uint8_t read(uint16_t addr) override
    {
        return m_chip.read(addr);
    }

    virtual uint32_t get_special_flags(void) override
    {
        return ((m_type == FM_YMF262) || (m_type == FM_YMF289B)) ? 0x8000 : 0x0000;
    }

    static void timer1(void *priv)
    {
        YMFMChip<ChipType> *drv = (YMFMChip<ChipType> *) priv;
        drv->m_engine->engine_timer_expired(0);
    }

    static void timer2(void *priv)
    {
        YMFMChip<ChipType> *drv = (YMFMChip<ChipType> *) priv;
        drv->m_engine->engine_timer_expired(1);
    }

private:
    ChipType                       m_chip;
    uint32_t                       m_clock;
    double                         m_clock_us, m_subtract[2];
    typename ChipType::output_data m_output;
    pc_timer_t                     m_timers[2];

    // Resampling
    int32_t m_rateratio;
    int32_t m_samplecnt;
    int32_t m_oldsamples[2];
    int32_t m_samples[2];
};

extern "C" {
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H

#include "cpu.h"
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/snd_opl.h>

#ifdef ENABLE_OPL_LOG
int ymfm_do_log = ENABLE_OPL_LOG;

static void
ymfm_log(const char *fmt, ...)
{
    va_list ap;

    if (ymfm_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
	}
}
#else
#    define ymfm_log(fmt, ...)
#endif

static void *
ymfm_drv_init(const device_t *info)
{
    YMFMChipBase *fm;

    switch (info->local) {
        case FM_YM3812:
        default:
            fm = (YMFMChipBase *) new YMFMChip<ymfm::ym3812>(3579545, FM_YM3812, 48000);
            break;

        case FM_YMF262:
            fm = (YMFMChipBase *) new YMFMChip<ymfm::ymf262>(14318181, FM_YMF262, 48000);
            break;

        case FM_YMF289B:
            fm = (YMFMChipBase *) new YMFMChip<ymfm::ymf289b>(33868800, FM_YMF289B, 48000);
            break;
    }

    fm->set_do_cycles(1);

    return fm;
}

static void
ymfm_drv_close(void *priv)
{
    YMFMChipBase *drv = (YMFMChipBase *) priv;

    if (drv != NULL)
        delete (drv);
}

static uint8_t
ymfm_drv_read(uint16_t port, void *priv)
{
    YMFMChipBase *drv = (YMFMChipBase *) priv;

    if (drv->flags() & FLAG_CYCLES) {
        cycles -= ((int) (isa_timing * 8));
    }

    uint8_t ret = drv->read(port);
    drv->update();

    ymfm_log("YMFM read port %04x, status = %02x\n", port, ret);
    return ret;
}

static void
ymfm_drv_write(uint16_t port, uint8_t val, void *priv)
{
    YMFMChipBase *drv = (YMFMChipBase *) priv;
    ymfm_log("YMFM write port %04x value = %02x\n", port, val);
    drv->write(port, val);
    drv->update();
}

static int32_t *
ymfm_drv_update(void *priv)
{
    YMFMChipBase *drv = (YMFMChipBase *) priv;

    return drv->update();
}

static void
ymfm_drv_reset_buffer(void *priv)
{
    YMFMChipBase *drv = (YMFMChipBase *) priv;

    drv->reset_buffer();
}

static void
ymfm_drv_set_do_cycles(void *priv, int8_t do_cycles)
{
    YMFMChipBase *drv = (YMFMChipBase *) priv;
    drv->set_do_cycles(do_cycles);
}

const device_t ym3812_ymfm_device = {
    .name          = "Yamaha YM3812 OPL2 (YMFM)",
    .internal_name = "ym3812_ymfm",
    .flags         = 0,
    .local         = FM_YM3812,
    .init          = ymfm_drv_init,
    .close         = ymfm_drv_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ymf262_ymfm_device = {
    .name          = "Yamaha YMF262 OPL3 (YMFM)",
    .internal_name = "ymf262_ymfm",
    .flags         = 0,
    .local         = FM_YMF262,
    .init          = ymfm_drv_init,
    .close         = ymfm_drv_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ymf289b_ymfm_device = {
    .name          = "Yamaha YMF289B OPL3-L (YMFM)",
    .internal_name = "ymf289b_ymfm",
    .flags         = 0,
    .local         = FM_YMF289B,
    .init          = ymfm_drv_init,
    .close         = ymfm_drv_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const fm_drv_t ymfm_drv {
    &ymfm_drv_read,
    &ymfm_drv_write,
    &ymfm_drv_update,
    &ymfm_drv_reset_buffer,
    &ymfm_drv_set_do_cycles,
    NULL,
};
}
