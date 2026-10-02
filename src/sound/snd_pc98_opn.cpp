/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          ymfm YM2203/YM2608 bridge for the PC-98 sound boards.
 *
 *          snd_opl_ymfm.cpp already wraps ymfm for the OPL family, but its
 *          YMFMChip template is built around chips with no ADPCM and a
 *          single output pair, and it routes everything through the OPL
 *          music buffer.  The OPN parts need ADPCM-A ROM reads, ADPCM-B RAM
 *          reads and writes, and their own mixing, so they get a small
 *          bridge of their own here rather than more special cases there.
 */
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "ymfm/ymfm.h"
#include "ymfm/ymfm_opn.h"

extern "C" {
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/sound.h>
#include <86box/snd_pc98_opn.h>
#include <86box/plat_unused.h>
}

#define RSM_FRAC 10

namespace {

class pc98_opn_base {
public:
    virtual ~pc98_opn_base() = default;

    virtual void    reset()                            = 0;
    virtual uint8_t read(uint32_t addr)                = 0;
    virtual void    write(uint32_t addr, uint8_t data) = 0;
    virtual void    generate(int32_t *buf, uint32_t frames) = 0;

    virtual void set_adpcm_rom(const uint8_t *rom, uint32_t len) = 0;
    virtual void set_extended(bool enabled) = 0;
    virtual void set_output_enabled(bool enabled) = 0;
    virtual bool set_adpcm_ram(bool enabled) = 0;

    uint8_t port_a = 0xffu;
    uint8_t port_b = 0x00u;
};

template <typename ChipType>
class pc98_opn_chip final : public pc98_opn_base, public ymfm::ymfm_interface {
public:
    pc98_opn_chip(uint32_t clock, void (*irq)(void *, int), void *priv)
        : m_chip(*this)
        , m_clock(clock)
        , m_irq(irq)
        , m_priv(priv)
    {
        m_clock_us  = 1000000.0 / (double) m_clock;
        update_rateratio();
        std::memset(m_samples, 0, sizeof(m_samples));
        std::memset(m_oldsamples, 0, sizeof(m_oldsamples));
        std::memset(m_buffer, 0, sizeof(m_buffer));

        timer_add(&m_timers[0], pc98_opn_chip::timer0, this, 0);
        timer_add(&m_timers[1], pc98_opn_chip::timer1, this, 0);
        timer_add(&m_busy_timer, pc98_opn_chip::busy_timer_expired, this, 0);
    }

    /*
     * The two timer objects live inside this allocation and are linked into
     * 86Box's global timer list by timer_add().  Freeing the chip without
     * unhooking them leaves that list holding pointers into freed memory,
     * and the emulator dies on the next tick.  This is the same
     * use-after-free PC98_MERGE_NOTES.txt records for the video device's
     * frame timer; it bites here whenever a sound card is replaced, which
     * includes every settings change.
     */
    ~pc98_opn_chip() override
    {
        timer_disable(&m_timers[0]);
        timer_disable(&m_timers[1]);
        timer_disable(&m_busy_timer);
        std::free(m_ram);
    }

    void
    reset() override
    {
        m_busy = false;
        timer_disable(&m_timers[0]);
        timer_disable(&m_timers[1]);
        timer_disable(&m_busy_timer);
        m_buffer_pos = 0;
        m_samplecnt = 0;
        std::memset(m_samples, 0, sizeof(m_samples));
        std::memset(m_oldsamples, 0, sizeof(m_oldsamples));
        std::memset(m_buffer, 0, sizeof(m_buffer));
        update_rateratio();
        m_extended = false;
        m_output_enabled = true;
        m_address_port = 0;
        m_address_data = 0;
        m_irq_enable = 0x1f;
        m_chip.reset();
        m_chip.write(0u, 0u);
        port_a = 0xffu;
        port_b = 0x00u;
    }

    uint8_t
    read(uint32_t addr) override
    {
        update_to((uint32_t) sound_pos_global);
        return m_chip.read(addr);
    }

    void
    write(uint32_t addr, uint8_t data) override
    {
        update_to((uint32_t) sound_pos_global);
        if ((addr == 0u) || (addr == 2u)) {
            m_address_port = addr;
            m_address_data = data;
        } else if constexpr (ChipType::OUTPUTS == 3) {
            if ((addr == 1u) && (m_address_port == 0u) && (m_address_data == 0x29u)) {
                m_irq_enable = data;
                data = (data & 0x7fu) | (m_extended ? 0x80u : 0u);
            }
        }
        m_chip.write(addr, data);
    }

    void
    set_extended(bool enabled) override
    {
        if constexpr (ChipType::OUTPUTS == 3) {
            update_to((uint32_t) sound_pos_global);
            m_extended = enabled;
            /* Board mode selects three or six FM channels. Preserve the
             * guest's IRQ mask and the chip's current address latch. */
            m_chip.write(0u, 0x29u);
            m_chip.write(1u, (m_irq_enable & 0x7fu) | (enabled ? 0x80u : 0u));
            m_chip.write(m_address_port, m_address_data);
        }
    }

    void
    set_output_enabled(bool enabled) override
    {
        update_to((uint32_t) sound_pos_global);
        m_output_enabled = enabled;
    }

    bool
    set_adpcm_ram(bool enabled) override
    {
        update_to((uint32_t) sound_pos_global);
        if (enabled && !m_ram)
            m_ram = (uint8_t *) std::calloc(1, PC98_OPNA_RAM_SIZE);
        else if (!enabled) {
            std::free(m_ram);
            m_ram = nullptr;
        }
        return !enabled || m_ram != nullptr;
    }

    void
    set_adpcm_rom(const uint8_t *rom, uint32_t len) override
    {
        m_adpcm_rom     = rom;
        m_adpcm_rom_len = len;
    }

    /*
     * Resample the chip's native rate up to SOUND_FREQ and accumulate.  The
     * caller's buffer already holds whatever other devices have mixed in,
     * so this adds rather than assigns.
     */
    void
    generate(int32_t *buf, uint32_t frames) override
    {
        update_rateratio();
        /*
         * sound_poll() calls handlers at the end of the output buffer.  YM
         * writes have already advanced the chip to sound_pos_global, so
         * finish the tail, mix the buffered samples, and begin the next
         * output buffer at position zero.
         */
        update_to(frames);
        const uint32_t mix_frames = (frames < SOUNDBUFLEN) ? frames : SOUNDBUFLEN;
        for (uint32_t i = 0; i < mix_frames; i++) {
            buf[i * 2]     += m_buffer[i * 2];
            buf[i * 2 + 1] += m_buffer[i * 2 + 1];
        }

        m_buffer_pos = 0;
        std::memset(m_buffer, 0, sizeof(m_buffer));
    }

    void
    update_rateratio()
    {
        const uint32_t output_rate = (sound_sample_rate > 0)
                                   ? (uint32_t)sound_sample_rate
                                   : SOUND_FREQ;
        const uint32_t chip_rate = m_chip.sample_rate(m_clock);

        m_rateratio = (chip_rate != 0)
                    ? (uint32_t)(((uint64_t)output_rate << RSM_FRAC) / chip_rate)
                    : (1u << RSM_FRAC);
        if (!m_rateratio)
            m_rateratio = 1;
    }

    void
    update_to(uint32_t position)
    {
        if (position > SOUNDBUFLEN)
            position = SOUNDBUFLEN;

        /* A sound buffer is retired by generate() before the global cursor
         * wraps, so a backwards cursor here can only be a reset/reconfigure. */
        if (position < m_buffer_pos) {
            m_buffer_pos = 0;
            std::memset(m_buffer, 0, sizeof(m_buffer));
        }

        if (position > m_buffer_pos) {
            generate_samples(&m_buffer[m_buffer_pos * 2], position - m_buffer_pos);
            m_buffer_pos = position;
        }
    }

    void
    generate_samples(int32_t *buf, uint32_t frames)
    {
        for (uint32_t i = 0; i < frames; i++) {
            while (m_samplecnt >= m_rateratio) {
                m_oldsamples[0] = m_samples[0];
                m_oldsamples[1] = m_samples[1];
                step();
                m_samplecnt -= m_rateratio;
            }

            const int32_t l = (int32_t) ((m_oldsamples[0] * (int64_t) (m_rateratio - m_samplecnt) +
                                          m_samples[0] * (int64_t) m_samplecnt) /
                                         m_rateratio);
            const int32_t r = (int32_t) ((m_oldsamples[1] * (int64_t) (m_rateratio - m_samplecnt) +
                                          m_samples[1] * (int64_t) m_samplecnt) /
                                         m_rateratio);

            buf[i * 2]     = m_output_enabled ? l : 0;
            buf[i * 2 + 1] = m_output_enabled ? r : 0;

            m_samplecnt += 1 << RSM_FRAC;
        }
    }

    /* ymfm_interface ------------------------------------------------- */

    void
    ymfm_set_timer(uint32_t tnum, int32_t duration_in_clocks) override
    {
        /* ymfm sets bit 15 when its first timer interval includes a phase
         * subtraction. Keep the timer number separate from that flag; a
         * corrected interval that is already due must expire into the FM
         * engine instead of being treated as a timer-cancel request. */
        const bool phase_corrected = (tnum & 0x8000u) != 0;
        tnum &= 0x7fffu;
        if (tnum > 1)
            return;

        if ((duration_in_clocks < 0) && !phase_corrected) {
            timer_stop(&m_timers[tnum]);
        } else if (duration_in_clocks <= 0) {
            m_engine->engine_timer_expired(tnum);
        } else {
            timer_on_auto(&m_timers[tnum], m_clock_us * (double) duration_in_clocks);
        }
    }

    void
    ymfm_update_irq(bool asserted) override
    {
        if (m_irq)
            m_irq(m_priv, asserted ? 1 : 0);
    }

    void
    ymfm_set_busy_end(uint32_t clocks) override
    {
        m_busy = (clocks != 0);
        if (m_busy)
            timer_on_auto(&m_busy_timer, m_clock_us * (double) clocks);
        else
            timer_disable(&m_busy_timer);
    }

    bool
    ymfm_is_busy() override
    {
        return m_busy;
    }

    uint8_t
    ymfm_external_read(ymfm::access_class type, uint32_t address) override
    {
        switch (type) {
            case ymfm::ACCESS_IO:
                if (address == 0u)
                    return port_a;
                if (address == 1u)
                    return port_b;
                return 0xffu;

            case ymfm::ACCESS_ADPCM_A:
                /* The rhythm samples.  Silence, not garbage, when the ROM
                 * was not supplied. */
                if (m_adpcm_rom && address < m_adpcm_rom_len)
                    return m_adpcm_rom[address];
                return 0u;

            case ymfm::ACCESS_ADPCM_B:
                return m_ram ? m_ram[address & (PC98_OPNA_RAM_SIZE - 1u)] : 0xffu;

            default:
                return 0u;
        }
    }

    void
    ymfm_external_write(ymfm::access_class type, uint32_t address,
                        uint8_t data) override
    {
        if (type == ymfm::ACCESS_IO) {
            if (address == 0u)
                port_a = data;
            else if (address == 1u)
                port_b = data;
            return;
        }

        /* Only the ADPCM-B RAM is writable; the rhythm ROM is not. */
        if ((type == ymfm::ACCESS_ADPCM_B) && m_ram)
            m_ram[address & (PC98_OPNA_RAM_SIZE - 1u)] = data;
    }

private:
    void
    step()
    {
        typename ChipType::output_data out;
        m_chip.generate(&out);

        /*
         * if constexpr, not if: the two branches index different numbers of
         * outputs, so the YM2608 instantiation would not compile against
         * data[3] even though it never reaches it.
         */
        if constexpr (ChipType::OUTPUTS == 4) {
            /*
             * YM2203: data[0] is the mono FM output and data[1..3] are the
             * three SSG channels.  The board sums them into one mono line,
             * so both sides get the same value.
             */
            const int32_t ssg = out.data[1] + out.data[2] + out.data[3];
            const int32_t mix = out.data[0] + (ssg >> 3);
            m_samples[0] = m_samples[1] = mix;
        } else {
            /*
             * YM2608: data[0]/data[1] are FM left/right and data[2] is the
             * mono SSG, which feeds both sides.
             */
            const int32_t ssg = out.data[2] / 2;
            m_samples[0]      = out.data[0] + (ssg >> 2);
            m_samples[1]      = out.data[1 % ChipType::OUTPUTS] + (ssg >> 2);
        }
    }

    static void
    timer0(void *priv)
    {
        pc98_opn_chip *self = (pc98_opn_chip *) priv;
        self->update_to((uint32_t) sound_pos_global);
        self->m_engine->engine_timer_expired(0);
    }

    static void
    timer1(void *priv)
    {
        pc98_opn_chip *self = (pc98_opn_chip *) priv;
        self->update_to((uint32_t) sound_pos_global);
        self->m_engine->engine_timer_expired(1);
    }

    static void
    busy_timer_expired(void *priv)
    {
        pc98_opn_chip *self = (pc98_opn_chip *) priv;
        self->m_busy = false;
    }

    ChipType m_chip;
    uint32_t m_clock;
    double   m_clock_us    = 0.0;
    bool     m_busy        = false;
    uint32_t m_rateratio   = 1u << RSM_FRAC;
    uint32_t m_samplecnt   = 0u;
    int32_t  m_samples[2]  = { 0, 0 };
    int32_t  m_oldsamples[2] = { 0, 0 };
    int32_t  m_buffer[SOUNDBUFLEN * 2] {};
    uint32_t m_buffer_pos = 0;

    void (*m_irq)(void *, int) = nullptr;
    void *m_priv               = nullptr;

    const uint8_t *m_adpcm_rom     = nullptr;
    uint32_t       m_adpcm_rom_len = 0u;
    uint8_t       *m_ram = nullptr;
    bool           m_extended = false;
    bool           m_output_enabled = true;
    uint32_t       m_address_port = 0u;
    uint8_t        m_address_data = 0u;
    uint8_t        m_irq_enable = 0x1fu;

    pc_timer_t m_timers[2] {};
    pc_timer_t m_busy_timer {};
};

} /* namespace */

struct pc98_opn_t {
    pc98_opn_base *chip;
};

extern "C" {

pc98_opn_t *
pc98_opn_create(pc98_opn_type_t type, uint32_t clock,
                void (*irq)(void *priv, int level), void *priv)
{
    pc98_opn_t *handle = (pc98_opn_t *) std::calloc(1, sizeof(pc98_opn_t));

    if (!handle)
        return nullptr;

    if (type == PC98_OPN_YM2608)
        handle->chip = new pc98_opn_chip<ymfm::ym2608>(clock, irq, priv);
    else
        handle->chip = new pc98_opn_chip<ymfm::ym2203>(clock, irq, priv);

    if (!handle->chip) {
        std::free(handle);
        return nullptr;
    }

    handle->chip->reset();
    return handle;
}

void
pc98_opn_destroy(pc98_opn_t *chip)
{
    if (!chip)
        return;
    delete chip->chip;
    std::free(chip);
}

void
pc98_opn_reset(pc98_opn_t *chip)
{
    if (chip && chip->chip)
        chip->chip->reset();
}

uint8_t
pc98_opn_read(pc98_opn_t *chip, uint32_t addr)
{
    if (!chip || !chip->chip)
        return 0xffu;
    return chip->chip->read(addr);
}

void
pc98_opn_write(pc98_opn_t *chip, uint32_t addr, uint8_t data)
{
    if (chip && chip->chip)
        chip->chip->write(addr, data);
}

void
pc98_opn_set_adpcm_rom(pc98_opn_t *chip, const uint8_t *rom, uint32_t len)
{
    if (chip && chip->chip)
        chip->chip->set_adpcm_rom(rom, len);
}

void
pc98_opn_generate(pc98_opn_t *chip, int32_t *buf, uint32_t frames)
{
    if (chip && chip->chip)
        chip->chip->generate(buf, frames);
}

void
pc98_opn_set_extended(pc98_opn_t *chip, int enabled)
{
    if (chip && chip->chip)
        chip->chip->set_extended(enabled != 0);
}

void
pc98_opn_set_output_enabled(pc98_opn_t *chip, int enabled)
{
    if (chip && chip->chip)
        chip->chip->set_output_enabled(enabled != 0);
}

int
pc98_opn_set_adpcm_ram(pc98_opn_t *chip, int enabled)
{
    return chip && chip->chip && chip->chip->set_adpcm_ram(enabled != 0);
}

void
pc98_opn_set_port_a(pc98_opn_t *chip, uint8_t value)
{
    if (chip && chip->chip)
        chip->chip->port_a = value;
}

uint8_t
pc98_opn_get_port_b(const pc98_opn_t *chip)
{
    if (!chip || !chip->chip)
        return 0u;
    return chip->chip->port_b;
}

} /* extern "C" */
