/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Interface to the YMFM External audio device (USB)
 *          For OPL2Board arduino based.
 *
 * Authors: Jose Phillips, <jose@hddlive.net>
 *          Adrien Moulin, <adrien@elyosh.org>
 *          
 *          Copyright 2024 Jose Phillips.
 *          Copyright 2022 Adrien Moulin.
 */
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "ymfm/ymfm_opl.h"
#include <libserialport.h>

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
#include <86box/config.h>
#include <86box/ini.h>
#include <86box/device.h>

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

uint8_t lastval = 0x00;

class OPLBOARDChipBase {
public:
    OPLBOARDChipBase(UNUSED(uint32_t clock), fm_type type, uint32_t samplerate)
        : m_buf_pos(0)
        , m_flags(0)
        , m_type(type)
        , m_samplerate(samplerate)
    {
        memset(m_buffer, 0, sizeof(m_buffer));
    }

    virtual ~OPLBOARDChipBase()
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
class OPLBOARDChip : public OPLBOARDChipBase, public ymfm::ymfm_interface {
public:
    OPLBOARDChip(uint32_t clock, fm_type type, uint32_t samplerate)
        : OPLBOARDChipBase(clock, type, samplerate)
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

        timer_add(&m_timers[0], OPLBOARDChip::timer1, this, 0);
        timer_add(&m_timers[1], OPLBOARDChip::timer2, this, 0);
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
        OPLBOARDChip<ChipType> *drv = (OPLBOARDChip<ChipType> *) priv;
        drv->m_engine->engine_timer_expired(0);
    }

    static void timer2(void *priv)
    {
        OPLBOARDChip<ChipType> *drv = (OPLBOARDChip<ChipType> *) priv;
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
#include <86box/device.h>
#include <86box/config.h>
#include <86box/ini.h>

#ifdef ENABLE_OPL_LOG
int oplboard_do_log = ENABLE_OPL_LOG;

static void
oplboard_log(const char *fmt, ...)
{
    va_list ap;

    if (oplboard_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define oplboard_log(fmt, ...)
#endif

struct sp_port *port;

void opl2board_init() {
    device_add(&opl2board_device);
    const char* port_name = device_get_config_string("host_serial_path");
    device_context_restore();

    enum sp_return result;

    result = sp_get_port_by_name(port_name, &port);
    if (result != SP_OK) {
        oplboard_log("Error: Cannot find port %s\n", port_name);
        return;
    }

    result = sp_open(port, SP_MODE_READ_WRITE);
    if (result != SP_OK) {
        oplboard_log ("Error: Cannot open port %s\n", port_name);
        return;
    }

    // Set port configuration this values are hardcoded.
    sp_set_baudrate(port, 115200);
    sp_set_bits(port, 8);
    sp_set_parity(port, SP_PARITY_NONE);
    sp_set_stopbits(port, 1);
    sp_set_flowcontrol(port, SP_FLOWCONTROL_NONE);

    oplboard_log("OPL2Board Serial port %s initialized at 115200 baud.\n", port_name);
}

void opl2board_write(uint8_t data) {
    if (port == NULL) {
        oplboard_log(stderr, "Error: OPL2Board Port not initialized.\n");

        return;    
    }

    enum sp_return result = sp_blocking_write(port, &data, sizeof(data), 1000);
    if (result < 0) {
        oplboard_log(stderr, "Error: Failed to write to OPL2Board port.\n");
    } else {
        oplboard_log("OPL2Board: data sent: %02X\n", data);
    }
}

void opl2board_reset() {

    // Reset all voices to 0 
    oplboard_log("Performing OPL2Board reset\n");
    for (uint8_t i = 0x00; i < 0xFF; i++) {
		if (i >= 0x40 && i <= 0x55) {
			opl2board_write(i);
            opl2board_write(0x3F);
		} else {
            opl2board_write (i);
			opl2board_write(0x00);        
		}	}
}

void opl2board_close() {
    
    if (port != NULL) {
        opl2board_reset();
        sp_close(port);
        sp_free_port(port);
        port = NULL;
        oplboard_log("OPL2Board port closed.\n");
    }
}

static void *
ymfm_opl2board_drv_init(const device_t *info)
{
    OPLBOARDChipBase *fm;
    
    switch (info->local) {
        default:
        case FM_OPL2BOARD:
            fm = (OPLBOARDChipBase *) new OPLBOARDChip<ymfm::ym3812>(3579545, FM_OPL2BOARD, FREQ_49716);
            break;
    }
    fm->set_do_cycles(1);

    return fm;
}

static void
ymfm_opl2board_drv_close(void *priv)
{
    OPLBOARDChipBase *drv = (OPLBOARDChipBase *) priv;

    opl2board_close();
    if (drv != NULL)
        delete drv;
}

static uint8_t
ymfm_opl2board_drv_read(uint16_t port, void *priv)
{
    OPLBOARDChipBase *drv = (OPLBOARDChipBase *) priv;

    if ((port == 0x380) || (port == 0x381))
        port |= 4;

    /* Point to register read port. */
    if (drv->flags() & FLAG_CYCLES)
        cycles -= ((int) (isa_timing * 8));

    uint8_t ret = drv->read(port);
    drv->update();

    oplboard_log("OPLBoard read port %04x, status = %02x\n", port, ret);
    return ret;
}

static void
ymfm_opl2board_drv_write(uint16_t port, uint8_t val, void *priv)
{
    OPLBOARDChipBase *drv = (OPLBOARDChipBase *) priv;
 
    oplboard_log("OPLBoard write port %04x value = %02x\n", port, val);
    if ((port == 0x380) || (port == 0x381))
        port |= 4;
    // Allow initialization of adlib
    if ((val == 0x04 || val == 0x02) || (lastval == 0x04 || lastval == 0x02))
        drv->write(port, val);

    lastval = val;   
    opl2board_write(val);
    drv->update();
}

static int32_t *
ymfm_opl2board_drv_update(void *priv)
{
    if (port == NULL) {
        opl2board_init();
        opl2board_reset();
    }
    OPLBOARDChipBase *drv = (OPLBOARDChipBase *) priv;

    return drv->update();
}

static void
ymfm_opl2board_drv_reset_buffer(void *priv)
{
    OPLBOARDChipBase *drv = (OPLBOARDChipBase *) priv;

    drv->reset_buffer();
}

static void
ymfm_opl2board_drv_set_do_cycles(void *priv, int8_t do_cycles)
{
    OPLBOARDChipBase *drv = (OPLBOARDChipBase *) priv;

    drv->set_do_cycles(do_cycles);
}

static void
ymfm_opl2board_drv_generate(void *priv, int32_t *data, uint32_t num_samples)
{
    OPLBOARDChipBase *drv = (OPLBOARDChipBase *) priv;

    // drv->generate_resampled(data, num_samples);
    drv->generate(data, num_samples);
}

const device_t ym_opl2board_device = {
    .name          = "YMOPL2Board (External Device)",
    .internal_name = "ym_opl2board_device",
    .flags         = 0,
    .local         = FM_OPL2BOARD,
    .init          = ymfm_opl2board_drv_init,
    .close         = ymfm_opl2board_drv_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const fm_drv_t ymfm_opl2board_drv {
    .read          = &ymfm_opl2board_drv_read,
    .write         = &ymfm_opl2board_drv_write,
    .update        = &ymfm_opl2board_drv_update,
    .reset_buffer  = &ymfm_opl2board_drv_reset_buffer,
    .set_do_cycles = &ymfm_opl2board_drv_set_do_cycles,
    .priv          = NULL,
    .generate      = ymfm_opl2board_drv_generate
};

#ifdef __clang__
#    if __has_warning("-Wc99-designator")
#        pragma clang diagnostic pop
#    endif
#endif

}
