/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Modem ringback timing and audio tests.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include <gtest/gtest.h>

extern "C" {
#include <86box/modem/modem_sound.h>

int sound_sample_rate = 8000;
static void (*sound_callback)(int32_t *, uint16_t, void *);
static void *sound_callback_priv;

void
sound_add_handler(void (*get_buffer)(int32_t *, uint16_t, void *), void *priv)
{
    sound_callback = get_buffer;
    sound_callback_priv = priv;
}
}

namespace {

int32_t
render_ms(uint32_t duration)
{
    int32_t peak = 0;
    uint32_t samples = duration * static_cast<uint32_t>(sound_sample_rate) / 1000;
    while (samples) {
        const auto count = static_cast<uint16_t>(std::min(samples, 32000u));
        std::vector<int32_t> buffer(static_cast<size_t>(count) * 2);
        sound_callback(buffer.data(), count, sound_callback_priv);
        for (const int32_t sample : buffer)
            peak = std::max(peak, std::abs(sample));
        samples -= count;
    }
    return peak;
}

} // namespace

TEST(ModemSound, DialTimingUsesS6S8S11AndVirtualDialTone)
{
    EXPECT_EQ(modem_sound_dial_ms("12", 2, 3, 90, 0, 1), 1760u);
    EXPECT_EQ(modem_sound_dial_ms("12", 5, 3, 50, 0, 0), 5800u);
    EXPECT_EQ(modem_sound_dial_ms("1,W2", 2, 3, 90, 0, 1), 5560u);
    EXPECT_EQ(modem_sound_dial_ms("2", 2, 3, 50, 1, 1), 2300u);
    EXPECT_EQ(modem_sound_dial_ms("1", 0, 0, 0, 0, 0), 600u);
}

TEST(ModemSound, DialAudioHonorsConfiguredToneAndGap)
{
    modem_sound_t *sound = modem_sound_init();
    ASSERT_NE(sound, nullptr);
    ASSERT_NE(sound_callback, nullptr);

    modem_sound_event(sound, MODEM_SOUND_DIAL, "12", MODEM_SOUND_DIAL_ARG(1, 0, 50, 0, 0));
    EXPECT_GT(render_ms(1000), 300);
    EXPECT_GT(render_ms(50), 300);
    EXPECT_LT(render_ms(50), 100);
    EXPECT_GT(render_ms(50), 300);
    EXPECT_LT(render_ms(50), 100);
    EXPECT_LT(render_ms(600), 100);
    modem_sound_close(sound);
}

TEST(ModemSound, RingbackCountMatchesRegionalCadenceAndAnswerTiming)
{
    struct Region {
        int id;
        uint32_t wait_ms;
        uint32_t cycle_ms;
        uint32_t silence_ms;
    };
    const Region regions[] = {
        { MODEM_SOUND_REGION_NORTH_AMERICA, 2600, 6000, 2200 },
        { MODEM_SOUND_REGION_UK, 2900, 3000, 450 },
        { MODEM_SOUND_REGION_EUROPE, 5000, 5000, 1200 },
        { MODEM_SOUND_REGION_JAPAN, 2800, 3000, 1200 }
    };

    modem_sound_t *sound = modem_sound_init();
    ASSERT_NE(sound, nullptr);
    ASSERT_NE(sound_callback, nullptr);

    for (const auto &region : regions) {
        SCOPED_TRACE(region.id);
        modem_sound_set_region(sound, region.id);
        const uint32_t expected = MODEM_SOUND_RING_COUNT == 0 ? 0
            : region.wait_ms + (MODEM_SOUND_RING_COUNT - 1) * region.cycle_ms;
        EXPECT_EQ(modem_sound_ring_ms(sound), expected);

        modem_sound_event(sound, MODEM_SOUND_DIAL, "", MODEM_SOUND_DIAL_ARG(2, 0, 90, 0, 1));
        render_ms(modem_sound_dial_ms("", 2, 0, 90, 0, 1) + 1);
        for (int ring = 0; ring < MODEM_SOUND_RING_COUNT; ring++) {
            render_ms(100);
            EXPECT_GT(render_ms(100), 300);
            render_ms(region.silence_ms - 200);
            EXPECT_LT(render_ms(50), 100);
            uint32_t elapsed = region.silence_ms + 50;
            if (region.id == MODEM_SOUND_REGION_UK) {
                render_ms(700 - elapsed);
                EXPECT_GT(render_ms(50), 300);
                elapsed = 750;
            }
            if (ring + 1 < MODEM_SOUND_RING_COUNT)
                render_ms(region.cycle_ms - elapsed);
        }
    }

    const uint32_t default_wait = MODEM_SOUND_RING_COUNT == 0 ? 0
        : 2600 + (MODEM_SOUND_RING_COUNT - 1) * 6000;
    EXPECT_EQ(modem_sound_ring_ms(nullptr), default_wait);
    modem_sound_close(sound);
}
