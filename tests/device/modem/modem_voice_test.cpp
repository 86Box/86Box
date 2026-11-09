/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and IBM PC systems and compatibles.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Voice modem media tests.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2026 Jasmine Iwanek.
 */
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

extern "C" {
#include <86box/modem/modem_voice.h>
}

namespace {

constexpr double kPi = 3.14159265358979323846;

struct FrameCapture {
    int type = 0;
    std::vector<uint8_t> payload;
    int calls = 0;
};

void
capture_frame(int type, const uint8_t *payload, size_t len, void *priv)
{
    auto *capture = static_cast<FrameCapture *>(priv);
    capture->type = type;
    capture->payload.assign(payload, payload + len);
    capture->calls++;
}

std::array<int16_t, 160>
make_audio()
{
    std::array<int16_t, 160> audio{};
    for (size_t i = 0; i < audio.size(); i++) {
        const double phase = 2.0 * kPi * 440.0 * static_cast<double>(i) / 8000.0;
        audio[i] = static_cast<int16_t>(12000.0 * std::sin(phase));
    }
    return audio;
}

} // namespace

TEST(ModemVoiceCodec, G711RoundTripsStayWithinQuantizationError)
{
    for (int sample = -32768; sample <= 32767; sample += 257) {
        const auto input = static_cast<int16_t>(sample);
        const auto ulaw = voice_ulaw_decode(voice_ulaw_encode(input));
        const auto alaw = voice_alaw_decode(voice_alaw_encode(input));
        EXPECT_LE(std::abs(static_cast<int>(input) - ulaw), 1100);
        EXPECT_LE(std::abs(static_cast<int>(input) - alaw), 1100);
    }
    EXPECT_EQ(voice_ulaw_encode(0), 0xff);
    EXPECT_EQ(voice_alaw_decode(voice_alaw_encode(0)), 8);
}

TEST(ModemVoiceCodec, RockwellAdpcmEncodesAndDecodesWholeFramesAtEveryRate)
{
    const auto input = make_audio();
    for (int bits_per_sample = 2; bits_per_sample <= 4; bits_per_sample++) {
        rv_adpcm_t encoder;
        rv_adpcm_t decoder;
        std::array<uint8_t, 128> encoded{};
        std::array<int16_t, input.size()> decoded{};
        rv_adpcm_init(&encoder, bits_per_sample);
        rv_adpcm_init(&decoder, bits_per_sample);

        size_t bytes = rv_adpcm_encode(&encoder, input.data(), input.size(),
                                       encoded.data(), encoded.size());
        bytes += rv_adpcm_flush(&encoder, encoded.data() + bytes,
                                encoded.size() - bytes);
        const size_t expected_bytes = (input.size() * bits_per_sample + 7) / 8;
        ASSERT_EQ(bytes, expected_bytes);
        EXPECT_EQ(rv_adpcm_decode(&decoder, encoded.data(), bytes,
                                  decoded.data(), decoded.size()), input.size());

        int64_t squared_error = 0;
        for (size_t i = 0; i < input.size(); i++) {
            const int error = static_cast<int>(input[i]) - decoded[i];
            squared_error += static_cast<int64_t>(error) * error;
        }
        const auto mean_squared_error = squared_error / static_cast<int64_t>(input.size());
        EXPECT_LT(mean_squared_error, 250000000);
    }
}

TEST(ModemVoiceResampler, UpsamplingAndDownsamplingHaveExpectedCountsAndEndpoints)
{
    std::array<int16_t, 80> input{};
    for (size_t i = 0; i < input.size(); i++)
        input[i] = static_cast<int16_t>(i * 100);

    voice_resampler_t upsampler;
    std::array<int16_t, 160> upsampled{};
    voice_resampler_init(&upsampler, 8000, 16000);
    const size_t up_count = voice_resample(&upsampler, input.data(), input.size(),
                                           upsampled.data(), upsampled.size());
    EXPECT_EQ(up_count, 158u);
    EXPECT_EQ(upsampled[0], input[0]);
    EXPECT_EQ(upsampled[1], 50);
    EXPECT_EQ(upsampled[up_count - 1], 7850);

    voice_resampler_t downsampler;
    std::array<int16_t, 80> downsampled{};
    voice_resampler_init(&downsampler, 16000, 8000);
    const size_t down_count = voice_resample(&downsampler, upsampled.data(), up_count,
                                             downsampled.data(), downsampled.size());
    EXPECT_EQ(down_count, 79u);
    EXPECT_EQ(downsampled[0], upsampled[0]);

    voice_resampler_t split_resampler;
    std::array<int16_t, 160> split_output{};
    voice_resampler_init(&split_resampler, 8000, 16000);
    const size_t first = voice_resample(&split_resampler, input.data(), 31,
                                        split_output.data(), split_output.size());
    const size_t second = voice_resample(&split_resampler, input.data() + 31,
                                         input.size() - 31, split_output.data() + first,
                                         split_output.size() - first);
    ASSERT_EQ(first + second, up_count);
    EXPECT_TRUE(std::equal(upsampled.begin(), upsampled.begin() + up_count,
                           split_output.begin()));
}

TEST(ModemVoiceTone, DtmfKeysMapToTheStandardFrequencyPairs)
{
    static constexpr char keys[] = "123A456B789C*0#D";
    static constexpr double rows[] = { 697, 770, 852, 941 };
    static constexpr double cols[] = { 1209, 1336, 1477, 1633 };
    for (size_t i = 0; keys[i]; i++) {
        double row = 0.0;
        double col = 0.0;
        ASSERT_TRUE(voice_dtmf_freqs(keys[i], &row, &col));
        EXPECT_DOUBLE_EQ(row, rows[i / 4]);
        EXPECT_DOUBLE_EQ(col, cols[i % 4]);
    }
    double row = 0.0;
    double col = 0.0;
    EXPECT_TRUE(voice_dtmf_freqs('d', &row, &col));
    EXPECT_DOUBLE_EQ(row, 941);
    EXPECT_DOUBLE_EQ(col, 1633);
    EXPECT_FALSE(voice_dtmf_freqs('E', &row, &col));
}

TEST(ModemVoiceTone, GeneratedDtmfToneHasRequestedDurationAndSignal)
{
    double row = 0.0;
    double col = 0.0;
    ASSERT_TRUE(voice_dtmf_freqs('5', &row, &col));
    voice_tone_t tone{};
    std::array<int16_t, 200> samples{};
    voice_tone_start(&tone, row, col, 20, 6000.0);
    EXPECT_FALSE(voice_tone_mix(&tone, samples.data(), samples.size()));
    EXPECT_EQ(tone.left, 0u);
    EXPECT_TRUE(std::any_of(samples.begin(), samples.begin() + 160,
                            [](int16_t sample) { return sample != 0; }));
    EXPECT_TRUE(std::all_of(samples.begin() + 160, samples.end(),
                            [](int16_t sample) { return sample == 0; }));
}

TEST(ModemVoiceSilence, TracksQuietAndHeardTenMillisecondBlocks)
{
    voice_silence_t silence;
    std::array<int16_t, VOICE_LINE_RATE> samples{};
    voice_silence_init(&silence, 2);
    voice_silence_feed(&silence, samples.data(), samples.size());
    EXPECT_EQ(silence.quiet_ms, 1000u);
    EXPECT_EQ(silence.heard_ms, 0u);

    samples.fill(4000);
    voice_silence_feed(&silence, samples.data(), samples.size());
    EXPECT_EQ(silence.quiet_ms, 0u);
    EXPECT_EQ(silence.heard_ms, 1000u);

    samples.fill(0);
    voice_silence_feed(&silence, samples.data(), samples.size());
    EXPECT_EQ(silence.quiet_ms, 1000u);
    EXPECT_EQ(silence.heard_ms, 1000u);
}

TEST(ModemVoiceFrames, FramedPayloadCanArriveAcrossArbitraryChunks)
{
    constexpr std::array<uint8_t, 3> payload = { '5', 20, 128 };
    std::array<uint8_t, VOICE_FRAME_HDR + payload.size()> frame{};
    const size_t frame_size = voice_frame(frame.data(), VOICE_FRAME_DTMF,
                                          payload.data(), payload.size());
    ASSERT_EQ(frame_size, frame.size());

    voice_deframer_t deframer{};
    FrameCapture capture;
    EXPECT_EQ(voice_deframe(&deframer, frame.data(), 2, capture_frame, &capture), 0);
    EXPECT_EQ(capture.calls, 0);
    EXPECT_EQ(deframer.have, 2u);
    EXPECT_EQ(voice_deframe(&deframer, frame.data() + 2, frame.size() - 2,
                            capture_frame, &capture), 0);
    EXPECT_EQ(capture.calls, 1);
    EXPECT_EQ(capture.type, VOICE_FRAME_DTMF);
    EXPECT_EQ(capture.payload, std::vector<uint8_t>(payload.begin(), payload.end()));
    EXPECT_EQ(deframer.have, 0u);
}

TEST(ModemVoiceFrames, TruncatedAndMalformedFramesAreHandledSafely)
{
    voice_deframer_t deframer{};
    FrameCapture capture;
    const std::array<uint8_t, 5> truncated = { VOICE_FRAME_AUDIO, 4, 0, 0xff, 0x7f };
    EXPECT_EQ(voice_deframe(&deframer, truncated.data(), truncated.size(),
                            capture_frame, &capture), 0);
    EXPECT_EQ(deframer.have, truncated.size());
    EXPECT_EQ(capture.calls, 0);

    const std::array<uint8_t, 2> rest = { 0x80, 0x00 };
    EXPECT_EQ(voice_deframe(&deframer, rest.data(), rest.size(), capture_frame, &capture), 0);
    EXPECT_EQ(capture.calls, 1);
    EXPECT_EQ(capture.payload, (std::vector<uint8_t>{ 0xff, 0x7f, 0x80, 0x00 }));

    const std::array<uint8_t, 3> oversized = {
        VOICE_FRAME_AUDIO,
        static_cast<uint8_t>(VOICE_FRAME_MAX + 1),
        static_cast<uint8_t>((VOICE_FRAME_MAX + 1) >> 8)
    };
    EXPECT_EQ(voice_deframe(&deframer, oversized.data(), oversized.size(),
                            capture_frame, &capture), -1);
    EXPECT_EQ(deframer.have, 0u);
    EXPECT_EQ(capture.calls, 1);
}

TEST(ModemVoiceFrames, FrameBuilderBoundsPayloadToMaximumFrameSize)
{
    std::array<uint8_t, VOICE_FRAME_MAX + 20> payload{};
    std::array<uint8_t, VOICE_FRAME_HDR + VOICE_FRAME_MAX> frame{};
    const size_t frame_size = voice_frame(frame.data(), VOICE_FRAME_AUDIO,
                                          payload.data(), payload.size());
    EXPECT_EQ(frame_size, frame.size());
    EXPECT_EQ(frame[1], VOICE_FRAME_MAX & 0xff);
    EXPECT_EQ(frame[2], VOICE_FRAME_MAX >> 8);
}
