#include <cstddef>
#include <cstdint>
#include <array>
#include <algorithm>
#include <vector>
#include <gtest/gtest.h>
#include <86box/modem/modem_slip.h>

TEST(ModemSlip, DecodesEscapedEndAndEscape)
{
    const std::vector<uint8_t> frame = { SLIP_END, 0x41, SLIP_ESC, SLIP_ESC_END,
                                         SLIP_ESC, SLIP_ESC_ESC, SLIP_END };
    std::array<uint8_t, 8> packet{};
    size_t packet_len = 0;

    ASSERT_TRUE(slip_decode_frame(frame.data(), frame.size(), packet.data(), packet.size(),
                                  &packet_len));

    const std::vector<uint8_t> expected = { 0x41, SLIP_END, SLIP_ESC };
    EXPECT_EQ(packet_len, expected.size());
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(), packet.begin()));
}

TEST(ModemSlip, EncodesAndRoundTripsEscapedEndAndEscape)
{
    const std::vector<uint8_t> packet = { 0x41, SLIP_END, SLIP_ESC, 0x42 };
    const std::vector<uint8_t> expected = {
        SLIP_END, 0x41, SLIP_ESC, SLIP_ESC_END,
        SLIP_ESC, SLIP_ESC_ESC, 0x42, SLIP_END
    };
    std::array<uint8_t, 16> frame{};
    std::array<uint8_t, 8> decoded{};
    size_t frame_len = 0;
    size_t decoded_len = 0;

    ASSERT_TRUE(slip_encode_frame(packet.data(), packet.size(), frame.data(), frame.size(),
                                  &frame_len));
    ASSERT_EQ(frame_len, expected.size());
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(), frame.begin()));
    ASSERT_TRUE(slip_decode_frame(frame.data(), frame_len, decoded.data(), decoded.size(),
                                  &decoded_len));
    EXPECT_EQ(decoded_len, packet.size());
    EXPECT_TRUE(std::equal(packet.begin(), packet.end(), decoded.begin()));
}

TEST(ModemSlip, EncodesEmptyPacketAsTwoEndMarkers)
{
    const std::array<uint8_t, 2> expected = { SLIP_END, SLIP_END };
    std::array<uint8_t, 2> frame{};
    size_t frame_len = 0;

    ASSERT_TRUE(slip_encode_frame(nullptr, 0, frame.data(), frame.size(), &frame_len));
    EXPECT_EQ(frame_len, expected.size());
    EXPECT_EQ(frame, expected);
}

TEST(ModemSlip, RejectsEncodedFrameBufferOverflow)
{
    const uint8_t packet = SLIP_END;
    std::array<uint8_t, 3> frame{};
    size_t frame_len = 99;

    EXPECT_FALSE(slip_encode_frame(&packet, 1, frame.data(), frame.size(), &frame_len));
    EXPECT_EQ(frame_len, 0u);
}

TEST(ModemSlip, RejectsMalformedOrUnterminatedFrames)
{
    const std::array<std::vector<uint8_t>, 4> invalid_frames = {{
        { 0x41 },
        { 0x41, SLIP_ESC, SLIP_END },
        { 0x41, SLIP_ESC, 0x00, SLIP_END },
        { 0x41, SLIP_END, 0x42, SLIP_END },
    }};
    std::array<uint8_t, 8> packet{};

    for (const auto &frame : invalid_frames) {
        size_t packet_len = 99;
        EXPECT_FALSE(slip_decode_frame(frame.data(), frame.size(), packet.data(), packet.size(),
                                       &packet_len));
        EXPECT_EQ(packet_len, 0u);
    }
}

TEST(ModemSlip, RejectsOutputBufferOverflow)
{
    const std::vector<uint8_t> frame = { 0x41, 0x42, SLIP_END };
    uint8_t packet[1]{};
    size_t packet_len = 99;

    EXPECT_FALSE(slip_decode_frame(frame.data(), frame.size(), packet, sizeof(packet),
                                   &packet_len));
    EXPECT_EQ(packet_len, 0u);
}