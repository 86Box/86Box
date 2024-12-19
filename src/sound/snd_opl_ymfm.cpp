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
#include "ymfm/ymfm_ssg.h"
#include "ymfm/ymfm_misc.h"
#include "ymfm/ymfm_opl.h"
#include "ymfm/ymfm_opm.h"
#include "ymfm/ymfm_opn.h"
#include "ymfm/ymfm_opq.h"
#if 0
#include "ymfm/ymfm_opx.h"
#endif
#include "ymfm/ymfm_opz.h"

extern "C" {
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/sound.h>
#include <86box/snd_opl.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/plat_unused.h>

// Disable c99-designator to avoid the warnings in *_ymfm_device
#ifdef __clang__
#    if __has_warning("-Wc99-designator")
#        pragma clang diagnostic push
#        pragma clang diagnostic ignored "-Wc99-designator"
#    endif
#endif
}

#define RSM_FRAC 10

enum {
    FLAG_CYCLES = (1 << 0)
};

class YMFMChipBase {
public:
    YMFMChipBase(UNUSED(uint32_t clock), fm_type type, uint32_t samplerate)
        : m_buf_pos(0)
        , m_flags(0)
        , m_type(type)
        , m_samplerate(samplerate)
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
    virtual int32_t *update()                                                = 0;
    virtual uint8_t  read(uint16_t addr)                                     = 0;
    virtual void     set_clock(uint32_t clock)                               = 0;

protected:
    int32_t  m_buffer[MUSICBUFLEN * 2];
    int      m_buf_pos;
    int      *m_buf_pos_global;
    int8_t   m_flags;
    fm_type  m_type;
    uint32_t m_samplerate;
};

template <typename ChipType>
class YMFMChip : public YMFMChipBase, public ymfm::ymfm_interface {
public:
    YMFMChip(uint32_t clock, fm_type type, uint32_t samplerate)
        : YMFMChipBase(clock, type, samplerate)
        , m_chip(*this)
        , m_clock(clock)
        , m_samplerate(samplerate)
        , m_samplecnt(0)
    {
        memset(m_samples, 0, sizeof(m_samples));
        memset(m_oldsamples, 0, sizeof(m_oldsamples));
        m_rateratio      = (samplerate << RSM_FRAC) / m_chip.sample_rate(m_clock);
        m_clock_us       = 1000000.0 / (double) m_clock;
        m_subtract[0]    = 80.0;
        m_subtract[1]    = 320.0;
        m_type           = type;
        m_buf_pos_global = (samplerate == FREQ_49716) ? &music_pos_global : &wavetable_pos_global;

        if (m_type == FM_YMF278B) {
            if (rom_load_linear("roms/sound/yamaha/yrw801.rom", 0, 0x200000, 0, m_yrw801) == 0) {
                fatal("YRW801 ROM image \"roms/sound/yamaha/yrw801.rom\" not found\n");
            }
        }

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

        m_duration_in_clocks[tnum] = duration_in_clocks;
        pc_timer_t *timer          = &m_timers[tnum];
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

    virtual void set_clock(uint32_t clock) override
    {
        m_clock     = clock;
        m_clock_us  = 1000000.0 / (double) m_clock;
        m_rateratio = (m_samplerate << RSM_FRAC) / m_chip.sample_rate(m_clock);

        ymfm_set_timer(0, m_duration_in_clocks[0]);
        ymfm_set_timer(1, m_duration_in_clocks[1]);
    }

    virtual void generate(int32_t *data, uint32_t num_samples) override
    {
        for (uint32_t i = 0; i < num_samples; i++) {
            m_chip.generate(&m_output);
            if ((m_type == FM_YMF278B) && (sizeof(m_output.data) > (4 * sizeof(int32_t)))) {
                if (ChipType::OUTPUTS == 1) {
                    *data++ = m_output.data[4];
                    *data++ = m_output.data[4];
                } else {
                    *data++ = m_output.data[4];
                    *data++ = m_output.data[5];
                }
            } else if (ChipType::OUTPUTS == 1) {
                *data++ = m_output.data[0];
                *data++ = m_output.data[0];
            } else {
                *data++ = m_output.data[0];
                *data++ = m_output.data[1 % ChipType::OUTPUTS];
            }
        }
    }

#if 0
    virtual void generate_resampled(int32_t *data, uint32_t num_samples) override
    {
        if ((m_samplerate == FREQ_49716) || (m_samplerate == FREQ_44100)) {
            generate(data, num_samples);
            return;
        }

        for (uint32_t i = 0; i < num_samples; i++) {
            while (m_samplecnt >= m_rateratio) {
                m_oldsamples[0] = m_samples[0];
                m_oldsamples[1] = m_samples[1];
                m_chip.generate(&m_output);
                if ((m_type == FM_YMF278B) && (sizeof(m_output.data) > (4 * sizeof(int32_t)))) {
                    if (ChipType::OUTPUTS == 1) {
                        m_samples[0] = m_output.data[4];
                        m_samples[1] = m_output.data[4];
                    } else {
                        m_samples[0] = m_output.data[4];
                        m_samples[1] = m_output.data[5];
                    }
                } else if (ChipType::OUTPUTS == 1) {
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
#endif

    virtual int32_t *update() override
    {
        if (m_buf_pos >= *m_buf_pos_global)
            return m_buffer;

        generate(&m_buffer[m_buf_pos * 2], *m_buf_pos_global - m_buf_pos);

        for (; m_buf_pos < *m_buf_pos_global; m_buf_pos++) {
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
        return ((m_type == FM_YMF262) || (m_type == FM_YMF289B) || (m_type == FM_YMF278B)) ? 0x8000 : 0x0000;
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

    virtual uint8_t ymfm_external_read(ymfm::access_class type, uint32_t address) override
    {
        if (type == ymfm::access_class::ACCESS_PCM && address < 0x200000) {
            return m_yrw801[address];
        }
        return 0xFF;
    }

private:
    ChipType                       m_chip;
    uint32_t                       m_clock;
    double                         m_clock_us;
    double                         m_subtract[2];
    typename ChipType::output_data m_output;
    pc_timer_t                     m_timers[2];
    int32_t                        m_duration_in_clocks[2]; // Needed for clock switches.
    uint32_t                       m_samplerate;

    // YRW801-M wavetable ROM.
    uint8_t m_yrw801[0x200000];

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
        case FM_YM2149: /* OPL */
            // TODO: Check rates and frequency
            fm = (YMFMChipBase *) new YMFMChip<ymfm::ym2149>(14318181, FM_YM2149, FREQ_49716);
            break;

        case FM_YM3526: /* OPL */
            // TODO: Check rates and frequency
            fm = (YMFMChipBase *) new YMFMChip<ymfm::ym3526>(14318181, FM_YM3526, FREQ_49716);
            break;

        case FM_Y8950: /* MSX-Audio (OPL with ADPCM) */
            // TODO: Check rates and frequency
            fm = (YMFMChipBase *) new YMFMChip<ymfm::y8950>(14318181, FM_Y8950, FREQ_49716);
            break;

        default:
        case FM_YM3812: /* OPL2 */
            fm = (YMFMChipBase *) new YMFMChip<ymfm::ym3812>(3579545, FM_YM3812, FREQ_49716);
            break;

        case FM_YMF262: /* OPL3 */
            fm = (YMFMChipBase *) new YMFMChip<ymfm::ymf262>(14318181, FM_YMF262, FREQ_49716);
            break;

        case FM_YMF289B: /* OPL3-L */
            /* According to the datasheet, we should be using 33868800, but YMFM appears
               to cheat and does it using the same values as the YMF262. */
            fm = (YMFMChipBase *) new YMFMChip<ymfm::ymf289b>(14318181, FM_YMF289B, FREQ_49716);
            break;

        case FM_YMF278B: /* OPL4 */
            fm = (YMFMChipBase *) new YMFMChip<ymfm::ymf278b>(33868800, FM_YMF278B, FREQ_44100);
            break;

        case FM_YM2413: /* OPLL */
            // TODO: Check rates and frequency
            fm = (YMFMChipBase *) new YMFMChip<ymfm::ym2413>(14318181, FM_YM2413, FREQ_49716);
            break;

        case FM_YM2423: /* OPLL-X */
            // TODO: Check rates and frequency
            fm = (YMFMChipBase *) new YMFMChip<ymfm::ym2423>(14318181, FM_YM2423, FREQ_49716);
            break;

        case FM_YMF281: /* OPLLP */
            // TODO: Check rates and frequency
            fm = (YMFMChipBase *) new YMFMChip<ymfm::ymf281>(14318181, FM_YMF281, FREQ_49716);
            break;

        case FM_DS1001: /* Konami VRC7 MMC */
            // TODO: Check rates and frequency
            fm = (YMFMChipBase *) new YMFMChip<ymfm::ds1001>(14318181, FM_DS1001, FREQ_49716);
            break;

        case FM_YM2151: /* OPM */
            // TODO: Check rates and frequency
            fm = (YMFMChipBase *) new YMFMChip<ymfm::ym2151>(14318181, FM_YM2151, FREQ_49716);
            break;

        case FM_YM2203: /* OPN */
            // TODO: Check rates and frequency
            fm = (YMFMChipBase *) new YMFMChip<ymfm::ym2203>(14318181, FM_YM2203, FREQ_49716);
            break;

        case FM_YM2608: /* OPNA */
            // TODO: Check rates and frequency
            fm = (YMFMChipBase *) new YMFMChip<ymfm::ym2608>(14318181, FM_YM2608, FREQ_49716);
            break;

        case FM_YMF288: /* OPN3L */
            // TODO: Check rates and frequency
            fm = (YMFMChipBase *) new YMFMChip<ymfm::ymf288>(14318181, FM_YMF288, FREQ_49716);
            break;

        case FM_YM2610: /* OPNB */
            // TODO: Check rates and frequency
            fm = (YMFMChipBase *) new YMFMChip<ymfm::ym2610>(14318181, FM_YM2610, FREQ_49716);
            break;

        case FM_YM2610B: /* OPNB2 */
            // TODO: Check rates and frequency
            fm = (YMFMChipBase *) new YMFMChip<ymfm::ym2610b>(14318181, FM_YM2610B, FREQ_49716);
            break;

        case FM_YM2612: /* OPN2 */
            // TODO: Check rates and frequency
            fm = (YMFMChipBase *) new YMFMChip<ymfm::ym2612>(14318181, FM_YM2612, FREQ_49716);
            break;

        case FM_YM3438: /* OPN2C */
            // TODO: Check rates and frequency
            fm = (YMFMChipBase *) new YMFMChip<ymfm::ym3438>(14318181, FM_YM3438, FREQ_49716);
            break;

        case FM_YMF276: /* OPN2L */
            // TODO: Check function call, rates and frequency
            fm = (YMFMChipBase *) new YMFMChip<ymfm::ymf276>(14318181, FM_YMF276, FREQ_49716);
            break;

        case FM_YM2164: /* OPP */
            // TODO: Check rates and frequency
            fm = (YMFMChipBase *) new YMFMChip<ymfm::ym2164>(14318181, FM_YM2164, FREQ_49716);
            break;

        case FM_YM3806: /* OPQ */
            // TODO: Check rates and frequency
            fm = (YMFMChipBase *) new YMFMChip<ymfm::ym3806>(14318181, FM_YM3806, FREQ_49716);
            break;

#if 0
        case FM_YMF271: /* OPX */
            // TODO: Check rates and frequency
            fm = (YMFMChipBase *) new YMFMChip<ymfm::ymf271>(14318181, FM_YMF271, FREQ_49716);
            break;
#endif

        case FM_YM2414: /* OPZ */
            // TODO: Check rates and frequency
            fm = (YMFMChipBase *) new YMFMChip<ymfm::ym2414>(14318181, FM_YM2414, FREQ_49716);
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
        delete drv;
}

static uint8_t
ymfm_drv_read(uint16_t port, void *priv)
{
    YMFMChipBase *drv = (YMFMChipBase *) priv;

    if ((port == 0x380) || (port == 0x381))
        port |= 4;

    /* Point to register read port. */
    if (drv->flags() & FLAG_CYCLES)
        cycles -= ((int) (isa_timing * 8));

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
    if ((port == 0x380) || (port == 0x381))
        port |= 4;
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

static void
ymfm_drv_generate(void *priv, int32_t *data, uint32_t num_samples)
{
    YMFMChipBase *drv = (YMFMChipBase *) priv;

    // drv->generate_resampled(data, num_samples);
    drv->generate(data, num_samples);
}

const device_t ym2149_ymfm_device = {
    .name          = "Yamaha 2149 SSG (YMFM)",
    .internal_name = "ym2149_ymfm",
    .flags         = 0,
    .local         = FM_YM2149,
    .init          = ymfm_drv_init,
    .close         = ymfm_drv_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ym3526_ymfm_device = {
    .name          = "Yamaha YM3526 OPL (YMFM)",
    .internal_name = "ym3526_ymfm",
    .flags         = 0,
    .local         = FM_YM3526,
    .init          = ymfm_drv_init,
    .close         = ymfm_drv_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t y8950_ymfm_device = {
    .name          = "Yamaha Y8950 (YMFM)",
    .internal_name = "y8950_ymfm",
    .flags         = 0,
    .local         = FM_Y8950,
    .init          = ymfm_drv_init,
    .close         = ymfm_drv_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ym3812_ymfm_device = {
    .name          = "Yamaha YM3812 OPL2 (YMFM)",
    .internal_name = "ym3812_ymfm",
    .flags         = 0,
    .local         = FM_YM3812,
    .init          = ymfm_drv_init,
    .close         = ymfm_drv_close,
    .reset         = NULL,
    .available     = NULL,
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
    .available     = NULL,
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
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ymf278b_ymfm_device = {
    .name          = "Yamaha YMF278B OPL4 (YMFM)",
    .internal_name = "ymf278b_ymfm",
    .flags         = 0,
    .local         = FM_YMF278B,
    .init          = ymfm_drv_init,
    .close         = ymfm_drv_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ym2413_ymfm_device = {
    .name          = "Yamaha YM2413 OPLL (YMFM)",
    .internal_name = "ym2413_ymfm",
    .flags         = 0,
    .local         = FM_YM2413,
    .init          = ymfm_drv_init,
    .close         = ymfm_drv_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ym2423_ymfm_device = {
    .name          = "Yamaha YM2423 OPLL-X (YMFM)",
    .internal_name = "ym2423_ymfm",
    .flags         = 0,
    .local         = FM_YM2423,
    .init          = ymfm_drv_init,
    .close         = ymfm_drv_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ymf281_ymfm_device = {
    .name          = "Yamaha YMF281 OPLLP (YMFM)",
    .internal_name = "ymf281_ymfm",
    .flags         = 0,
    .local         = FM_YMF281,
    .init          = ymfm_drv_init,
    .close         = ymfm_drv_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ds1001_ymfm_device = {
    .name          = "Konami VRC7 MMC (YMFM)",
    .internal_name = "ds1001_ymfm",
    .flags         = 0,
    .local         = FM_DS1001,
    .init          = ymfm_drv_init,
    .close         = ymfm_drv_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ym2151_ymfm_device = {
    .name          = "Yamaha YM2151 OPM (YMFM)",
    .internal_name = "ym2151_ymfm",
    .flags         = 0,
    .local         = FM_YM2151,
    .init          = ymfm_drv_init,
    .close         = ymfm_drv_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ym2203_ymfm_device = {
    .name          = "Yamaha YM2203 OPN (YMFM)",
    .internal_name = "ym2203_ymfm",
    .flags         = 0,
    .local         = FM_YM2203,
    .init          = ymfm_drv_init,
    .close         = ymfm_drv_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ym2608_ymfm_device = {
    .name          = "Yamaha YM2608 OPNA (YMFM)",
    .internal_name = "ym2608_ymfm",
    .flags         = 0,
    .local         = FM_YM2608,
    .init          = ymfm_drv_init,
    .close         = ymfm_drv_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ymf288_ymfm_device = {
    .name          = "Yamaha YMF288 OPN3L (YMFM)",
    .internal_name = "ymf288_ymfm",
    .flags         = 0,
    .local         = FM_YMF288,
    .init          = ymfm_drv_init,
    .close         = ymfm_drv_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ym2610_ymfm_device = {
    .name          = "Yamaha YM2610 OPNB (YMFM)",
    .internal_name = "ym2610_ymfm",
    .flags         = 0,
    .local         = FM_YM2610,
    .init          = ymfm_drv_init,
    .close         = ymfm_drv_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ym2610b_ymfm_device = {
    .name          = "Yamaha YM2610b OPNB2 (YMFM)",
    .internal_name = "ym2610b_ymfm",
    .flags         = 0,
    .local         = FM_YM2610B,
    .init          = ymfm_drv_init,
    .close         = ymfm_drv_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ym2612_ymfm_device = {
    .name          = "Yamaha YM2612 OPN2 (YMFM)",
    .internal_name = "ym2612_ymfm",
    .flags         = 0,
    .local         = FM_YM2612,
    .init          = ymfm_drv_init,
    .close         = ymfm_drv_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ym3438_ymfm_device = {
    .name          = "Yamaha YM3438 OPN2C (YMFM)",
    .internal_name = "ym3438_ymfm",
    .flags         = 0,
    .local         = FM_YM3438,
    .init          = ymfm_drv_init,
    .close         = ymfm_drv_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ymf276_ymfm_device = {
    .name          = "Yamaha YMF276 OPN2L (YMFM)",
    .internal_name = "ymf276_ymfm",
    .flags         = 0,
    .local         = FM_YMF276,
    .init          = ymfm_drv_init,
    .close         = ymfm_drv_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ym2164_ymfm_device = {
    .name          = "Yamaha YM2164 OPP (YMFM)",
    .internal_name = "ym2164_ymfm",
    .flags         = 0,
    .local         = FM_YM2164,
    .init          = ymfm_drv_init,
    .close         = ymfm_drv_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ym3806_ymfm_device = {
    .name          = "Yamaha YM3806 OPQ (YMFM)",
    .internal_name = "ym3806_ymfm",
    .flags         = 0,
    .local         = FM_YM3806,
    .init          = ymfm_drv_init,
    .close         = ymfm_drv_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

#if 0
const device_t ymf271_ymfm_device = {
    .name          = "Yamaha YMF271 OPX (YMFM)",
    .internal_name = "ym271_ymfm",
    .flags         = 0,
    .local         = FM_YMF271,
    .init          = ymfm_drv_init,
    .close         = ymfm_drv_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
#endif

const device_t ym2414_ymfm_device = {
    .name          = "Yamaha YM2414 OPZ (YMFM)",
    .internal_name = "ym2414_ymfm",
    .flags         = 0,
    .local         = FM_YM2414,
    .init          = ymfm_drv_init,
    .close         = ymfm_drv_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};


const fm_drv_t ymfm_drv {
    .read          = &ymfm_drv_read,
    .write         = &ymfm_drv_write,
    .update        = &ymfm_drv_update,
    .reset_buffer  = &ymfm_drv_reset_buffer,
    .set_do_cycles = &ymfm_drv_set_do_cycles,
    .priv          = NULL,
    .generate      = ymfm_drv_generate,
};

#ifdef __clang__
#    if __has_warning("-Wc99-designator")
#        pragma clang diagnostic pop
#    endif
#endif

}
