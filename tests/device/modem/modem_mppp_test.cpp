#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

#include <gtest/gtest.h>

extern "C" {
#include <86box/modem/modem_mppp.h>
}

namespace {

struct PacketCapture {
    std::vector<std::vector<uint8_t>> packets;
};

struct FragmentCapture {
    std::vector<std::vector<uint8_t>> fragments;
};

void
capture_packet(void *opaque, const uint8_t *packet, size_t length)
{
    auto *capture = static_cast<PacketCapture *>(opaque);
    capture->packets.emplace_back(packet, packet + length);
}

std::vector<uint8_t>
make_fragment(bool short_sequence, uint32_t sequence, bool begin, bool end,
              std::initializer_list<uint8_t> payload)
{
    std::vector<uint8_t> fragment(ppp_mppp_header_size(short_sequence) + payload.size());
    EXPECT_TRUE(ppp_mppp_encode_header(fragment.data(), fragment.size(), short_sequence,
                                       sequence, begin, end));
    std::copy(payload.begin(), payload.end(), fragment.begin() +
              static_cast<std::ptrdiff_t>(ppp_mppp_header_size(short_sequence)));
    return fragment;
}

TEST(ModemMultilink, EncodesShortAndLongSequenceHeaders)
{
    uint8_t header[4] = {};

    ASSERT_TRUE(ppp_mppp_encode_header(header, sizeof(header), false, 0x123456, true, false));
    EXPECT_EQ(header[0], 0x80);
    EXPECT_EQ(header[1], 0x12);
    EXPECT_EQ(header[2], 0x34);
    EXPECT_EQ(header[3], 0x56);

    ASSERT_TRUE(ppp_mppp_encode_header(header, sizeof(header), true, 0x0ABC, false, true));
    EXPECT_EQ(header[0], 0x4A);
    EXPECT_EQ(header[1], 0xBC);
    EXPECT_FALSE(ppp_mppp_encode_header(header, sizeof(header), true, 0x1000, false, false));
}

TEST(ModemMultilink, ReassemblesFragmentsInSequenceOrder)
{
    ppp_mppp_reassembler_t state;
    PacketCapture capture;
    ASSERT_TRUE(ppp_mppp_reassembler_init(&state, 32, false));

    auto first = make_fragment(false, 100, true, false, { 0x00, 0x21 });
    auto last = make_fragment(false, 102, false, true, { 0xBB });
    auto middle = make_fragment(false, 101, false, false, { 0xAA });

    ASSERT_TRUE(ppp_mppp_reassembler_input(&state, first.data(), first.size(), capture_packet, &capture));
    ASSERT_TRUE(ppp_mppp_reassembler_input(&state, last.data(), last.size(), capture_packet, &capture));
    EXPECT_TRUE(capture.packets.empty());
    ASSERT_TRUE(ppp_mppp_reassembler_input(&state, middle.data(), middle.size(), capture_packet, &capture));

    ASSERT_EQ(capture.packets.size(), 1u);
    EXPECT_EQ(capture.packets[0], (std::vector<uint8_t>{ 0x00, 0x21, 0xAA, 0xBB }));
    ppp_mppp_reassembler_close(&state);
}

TEST(ModemMultilink, BuffersContinuationBeforeInitialBegin)
{
    ppp_mppp_reassembler_t state;
    PacketCapture capture;
    ASSERT_TRUE(ppp_mppp_reassembler_init(&state, 32, false));

    auto continuation = make_fragment(false, 201, false, true, { 0xAA, 0xBB });
    auto begin = make_fragment(false, 200, true, false, { 0x00, 0x21 });
    ASSERT_TRUE(ppp_mppp_reassembler_input(&state, continuation.data(), continuation.size(),
                                           capture_packet, &capture));
    EXPECT_TRUE(capture.packets.empty());
    ASSERT_TRUE(ppp_mppp_reassembler_input(&state, begin.data(), begin.size(),
                                           capture_packet, &capture));

    ASSERT_EQ(capture.packets.size(), 1u);
    EXPECT_EQ(capture.packets[0], (std::vector<uint8_t>{ 0x00, 0x21, 0xAA, 0xBB }));
    ppp_mppp_reassembler_close(&state);
}

TEST(ModemMultilink, HandlesLongSequenceWrap)
{
    ppp_mppp_reassembler_t state;
    PacketCapture capture;
    ASSERT_TRUE(ppp_mppp_reassembler_init(&state, 16, false));

    auto first = make_fragment(false, 0xFFFFFF, true, false, { 0x00, 0x21 });
    auto last = make_fragment(false, 0, false, true, { 0x42 });
    ASSERT_TRUE(ppp_mppp_reassembler_input(&state, first.data(), first.size(), capture_packet, &capture));
    ASSERT_TRUE(ppp_mppp_reassembler_input(&state, last.data(), last.size(), capture_packet, &capture));

    ASSERT_EQ(capture.packets.size(), 1u);
    EXPECT_EQ(capture.packets[0], (std::vector<uint8_t>{ 0x00, 0x21, 0x42 }));
    ppp_mppp_reassembler_close(&state);
}

TEST(ModemMultilink, DropsPacketsLargerThanMrru)
{
    ppp_mppp_reassembler_t state;
    PacketCapture capture;
    ASSERT_TRUE(ppp_mppp_reassembler_init(&state, 3, true));

    auto first = make_fragment(true, 0xFFF, true, false, { 0x00, 0x21 });
    auto last = make_fragment(true, 0, false, true, { 0x42, 0x43 });
    ASSERT_TRUE(ppp_mppp_reassembler_input(&state, first.data(), first.size(), capture_packet, &capture));
    ASSERT_TRUE(ppp_mppp_reassembler_input(&state, last.data(), last.size(), capture_packet, &capture));
    EXPECT_TRUE(capture.packets.empty());
    ppp_mppp_reassembler_close(&state);
}

TEST(ModemMultilink, ResetsPartialPacketAfterLinkLoss)
{
    ppp_mppp_reassembler_t state;
    PacketCapture capture;
    ASSERT_TRUE(ppp_mppp_reassembler_init(&state, 32, false));

    auto partial = make_fragment(false, 10, true, false, { 0x00, 0x21, 0x01 });
    auto complete = make_fragment(false, 200, true, true, { 0x00, 0x21, 0x02 });
    ASSERT_TRUE(ppp_mppp_reassembler_input(&state, partial.data(), partial.size(), capture_packet, &capture));
    ppp_mppp_reassembler_reset(&state);
    ASSERT_TRUE(ppp_mppp_reassembler_input(&state, complete.data(), complete.size(), capture_packet, &capture));

    ASSERT_EQ(capture.packets.size(), 1u);
    EXPECT_EQ(capture.packets[0], (std::vector<uint8_t>{ 0x00, 0x21, 0x02 }));
    ppp_mppp_reassembler_close(&state);
}

}

bool
capture_fragment(void *opaque, const uint8_t *fragment, size_t length)
{
    auto *capture = static_cast<FragmentCapture *>(opaque);
    capture->fragments.emplace_back(fragment, fragment + length);
    return true;
}

TEST(ModemMultilink, FragmentsAcrossLinksInRoundRobinOrder)
{
    ppp_mppp_sender_t sender;
    FragmentCapture first_link;
    FragmentCapture second_link;
    const uint8_t ip_packet[] = { 1, 2, 3, 4, 5, 6 };

    ASSERT_TRUE(ppp_mppp_sender_init(&sender, 32, false));
    ASSERT_TRUE(ppp_mppp_sender_add_link(&sender, capture_fragment, &first_link, 4));
    ASSERT_TRUE(ppp_mppp_sender_add_link(&sender, capture_fragment, &second_link, 4));
    ASSERT_TRUE(ppp_mppp_sender_send(&sender, 0x0021, ip_packet, sizeof(ip_packet)));

    ASSERT_EQ(first_link.fragments.size(), 1u);
    ASSERT_EQ(second_link.fragments.size(), 1u);
    EXPECT_EQ(first_link.fragments[0][0], 0x80);
    EXPECT_EQ(first_link.fragments[0][1], 0x00);
    EXPECT_EQ(first_link.fragments[0][2], 0x00);
    EXPECT_EQ(first_link.fragments[0][3], 0x00);
    EXPECT_EQ(second_link.fragments[0][0], 0x40);
    EXPECT_EQ(second_link.fragments[0][3], 0x01);
    EXPECT_EQ(first_link.fragments[0][4], 0x00);
    EXPECT_EQ(first_link.fragments[0][5], 0x21);
}