#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <vector>
#include <gtest/gtest.h>
#include <86box/modem/modem_cslip.h>

static void
set_ip_checksum(std::vector<uint8_t> &packet)
{
    packet[10] = 0;
    packet[11] = 0;
    uint32_t sum = 0;
    for (size_t pos = 0; pos < 20; pos += 2)
        sum += (static_cast<uint16_t>(packet[pos]) << 8) | packet[pos + 1];
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    uint16_t checksum = static_cast<uint16_t>(~sum);
    packet[10] = static_cast<uint8_t>(checksum >> 8);
    packet[11] = static_cast<uint8_t>(checksum);
}

static std::vector<uint8_t>
make_tcp_packet(uint8_t ttl, uint16_t ip_id, uint32_t sequence)
{
    std::vector<uint8_t> packet(40, 0);
    packet[0] = 0x45;
    packet[2] = 0;
    packet[3] = static_cast<uint8_t>(packet.size());
    packet[4] = static_cast<uint8_t>(ip_id >> 8);
    packet[5] = static_cast<uint8_t>(ip_id);
    packet[8] = ttl;
    packet[9] = 6;
    packet[12] = 10;
    packet[15] = 1;
    packet[16] = 10;
    packet[19] = 2;
    packet[20] = 0x04;
    packet[21] = 0xD2;
    packet[22] = 0;
    packet[23] = 80;
    packet[24] = static_cast<uint8_t>(sequence >> 24);
    packet[25] = static_cast<uint8_t>(sequence >> 16);
    packet[26] = static_cast<uint8_t>(sequence >> 8);
    packet[27] = static_cast<uint8_t>(sequence);
    packet[32] = 0x50;
    packet[33] = 0x10;
    packet[34] = 0x10;
    set_ip_checksum(packet);
    return packet;
}

TEST(ModemCslip, SendsUncompressedWhenTtlChanges)
{
    cslip_ctx_t *ctx = cslip_init(nullptr);
    ASSERT_NE(ctx, nullptr);
    uint8_t output[128];
    int type = 0;
    auto first = make_tcp_packet(64, 1, 100);
    ASSERT_GT(cslip_compress(ctx, first.data(), static_cast<int>(first.size()), output, &type), 0);
    ASSERT_EQ(type, VJ_TYPE_UNCOMPRESSED_TCP);

    auto second = make_tcp_packet(63, 2, 101);
    ASSERT_GT(cslip_compress(ctx, second.data(), static_cast<int>(second.size()), output, &type), 0);

    EXPECT_EQ(type, VJ_TYPE_UNCOMPRESSED_TCP);
    cslip_close(ctx);
}

TEST(ModemCslip, RejectsInvalidArguments)
{
    cslip_ctx_t *ctx = cslip_init(nullptr);
    ASSERT_NE(ctx, nullptr);
    std::array<uint8_t, 64> output{};
    const uint8_t byte = 0;
    int type = VJ_TYPE_IP;

    EXPECT_EQ(cslip_compress(ctx, &byte, -1, output.data(), &type), 0);
    EXPECT_EQ(cslip_compress(ctx, nullptr, 1, output.data(), &type), 0);
    EXPECT_EQ(cslip_compress_logged(ctx, nullptr, 1, output.data(), &type), 0);
    EXPECT_EQ(cslip_compress(nullptr, &byte, 1, output.data(), &type), 0);
    EXPECT_EQ(cslip_decompress(ctx, &byte, -1, output.data(), VJ_TYPE_IP), 0);
    EXPECT_EQ(cslip_decompress(ctx, nullptr, 1, output.data(), VJ_TYPE_IP), 0);
    EXPECT_EQ(cslip_decompress_packet(ctx, nullptr, 1, output.data()), 0);
    EXPECT_EQ(cslip_decompress_packet(ctx, &byte, -1, output.data()), 0);
    EXPECT_EQ(cslip_decompress(nullptr, &byte, 1, output.data(), VJ_TYPE_IP), 0);

    cslip_close(ctx);
}

TEST(ModemCslip, SendsReservedSpecialChangeMasksUncompressed)
{
    cslip_ctx_t *ctx = cslip_init(nullptr);
    ASSERT_NE(ctx, nullptr);
    uint8_t output[VJ_MAX_HDR + 64]{};
    int type = 0;
    auto first = make_tcp_packet(64, 1, 100);
    ASSERT_GT(cslip_compress(ctx, first.data(), first.size(), output, &type), 0);
    ASSERT_EQ(type, VJ_TYPE_UNCOMPRESSED_TCP);

    auto special_i = first;
    special_i[5] = 2;
    special_i[27] = 101;
    special_i[33] = 0x30;
    special_i[35] = 1;
    special_i[39] = 1;
    set_ip_checksum(special_i);
    EXPECT_EQ(cslip_compress(ctx, special_i.data(), special_i.size(), output, &type),
              static_cast<int>(special_i.size()));
    EXPECT_EQ(type, VJ_TYPE_UNCOMPRESSED_TCP);

    auto special_d = special_i;
    special_d[5] = 3;
    special_d[27] = 102;
    special_d[31] = 1;
    special_d[35] = 2;
    special_d[39] = 2;
    set_ip_checksum(special_d);
    EXPECT_EQ(cslip_compress(ctx, special_d.data(), special_d.size(), output, &type),
              static_cast<int>(special_d.size()));
    EXPECT_EQ(type, VJ_TYPE_UNCOMPRESSED_TCP);
    cslip_close(ctx);
}

TEST(ModemCslip, CompressesCompatibleTcpHeaderChanges)
{
    cslip_ctx_t *ctx = cslip_init(nullptr);
    ASSERT_NE(ctx, nullptr);
    uint8_t output[128];
    int type = 0;
    auto first = make_tcp_packet(64, 1, 100);
    ASSERT_GT(cslip_compress(ctx, first.data(), static_cast<int>(first.size()), output, &type), 0);
    auto second = make_tcp_packet(64, 2, 101);

    ASSERT_GT(cslip_compress(ctx, second.data(), static_cast<int>(second.size()), output, &type), 0);

    EXPECT_EQ(type, VJ_TYPE_COMPRESSED_TCP);
    cslip_close(ctx);
}

TEST(ModemCslip, RoundTripsStatefulTcpCompression)
{
    cslip_ctx_t *compressor = cslip_init(nullptr);
    cslip_ctx_t *decompressor = cslip_init(nullptr);
    ASSERT_NE(compressor, nullptr);
    ASSERT_NE(decompressor, nullptr);
    uint8_t compressed[VJ_MAX_HDR + 64]{};
    uint8_t restored[VJ_MAX_HDR + 64]{};
    int type = 0;

    auto first = make_tcp_packet(64, 1, 100);
    int compressed_len = cslip_compress(compressor, first.data(), first.size(), compressed, &type);
    ASSERT_GT(compressed_len, 0);
    ASSERT_EQ(type, VJ_TYPE_UNCOMPRESSED_TCP);
    int restored_len = cslip_decompress(decompressor, compressed, compressed_len, restored, type);
    ASSERT_EQ(restored_len, static_cast<int>(first.size()));
    EXPECT_TRUE(std::equal(first.begin(), first.end(), restored));

    auto second = make_tcp_packet(64, 2, 101);
    second[31] = 1;
    set_ip_checksum(second);
    compressed_len = cslip_compress(compressor, second.data(), second.size(), compressed, &type);
    ASSERT_GT(compressed_len, 0);
    ASSERT_EQ(type, VJ_TYPE_COMPRESSED_TCP);
    restored_len = cslip_decompress(decompressor, compressed, compressed_len, restored, type);

    EXPECT_EQ(restored_len, static_cast<int>(second.size()));
    for (size_t pos = 0; pos < second.size(); pos++) {
        SCOPED_TRACE(pos);
        EXPECT_EQ(restored[pos], second[pos]);
    }
    cslip_close(decompressor);
    cslip_close(compressor);
}

TEST(ModemCslip, EncodesAndRestoresExplicitIpIdDeltas)
{
    cslip_ctx_t *compressor = cslip_init(nullptr);
    cslip_ctx_t *decompressor = cslip_init(nullptr);
    ASSERT_NE(compressor, nullptr);
    ASSERT_NE(decompressor, nullptr);
    uint8_t compressed[VJ_MAX_HDR + 64]{};
    uint8_t restored[VJ_MAX_HDR + 64]{};
    int type = 0;

    auto first = make_tcp_packet(64, 1, 100);
    int compressed_len = cslip_compress(compressor, first.data(), first.size(), compressed, &type);
    ASSERT_GT(compressed_len, 0);
    ASSERT_EQ(type, VJ_TYPE_UNCOMPRESSED_TCP);
    ASSERT_EQ(cslip_decompress(decompressor, compressed, compressed_len, restored, type),
              static_cast<int>(first.size()));

    auto second = make_tcp_packet(64, 1, 101);
    compressed_len = cslip_compress(compressor, second.data(), second.size(), compressed, &type);
    ASSERT_GT(compressed_len, 0);
    ASSERT_EQ(type, VJ_TYPE_COMPRESSED_TCP);
    EXPECT_NE(compressed[0] & VJ_NEW_I, 0);
    EXPECT_EQ(compressed_len, 7);
    EXPECT_EQ(std::vector<uint8_t>(compressed, compressed + compressed_len),
              (std::vector<uint8_t>{ static_cast<uint8_t>(VJ_TYPE_COMPRESSED_TCP | VJ_NEW_I
                                                           | VJ_NEW_S),
                                     0, 0, 1, 0, 0, 0 }));
    int restored_len = cslip_decompress(decompressor, compressed, compressed_len,
                                        restored, type);
    ASSERT_EQ(restored_len, static_cast<int>(second.size()));
    EXPECT_TRUE(std::equal(second.begin(), second.end(), restored));

    auto third = make_tcp_packet(64, 4, 102);
    compressed_len = cslip_compress(compressor, third.data(), third.size(), compressed, &type);
    ASSERT_GT(compressed_len, 0);
    ASSERT_EQ(type, VJ_TYPE_COMPRESSED_TCP);
    EXPECT_NE(compressed[0] & VJ_NEW_I, 0);
    restored_len = cslip_decompress(decompressor, compressed, compressed_len,
                                    restored, type);
    ASSERT_EQ(restored_len, static_cast<int>(third.size()));
    EXPECT_TRUE(std::equal(third.begin(), third.end(), restored));

    cslip_close(decompressor);
    cslip_close(compressor);
}

TEST(ModemCslip, IncludesConnectionIdWhenSlotIdCompressionIsDisabled)
{
    cslip_ctx_t *compressor = cslip_init(nullptr);
    cslip_ctx_t *decompressor = cslip_init(nullptr);
    ASSERT_NE(compressor, nullptr);
    ASSERT_NE(decompressor, nullptr);
    compressor->compress_slot_id = false;
    uint8_t compressed[VJ_MAX_HDR + 64]{};
    uint8_t restored[VJ_MAX_HDR + 64]{};
    int type = 0;

    auto first = make_tcp_packet(64, 1, 100);
    int compressed_len = cslip_compress(compressor, first.data(), first.size(), compressed, &type);
    ASSERT_GT(compressed_len, 0);
    ASSERT_EQ(type, VJ_TYPE_UNCOMPRESSED_TCP);
    ASSERT_EQ(cslip_decompress(decompressor, compressed, compressed_len, restored, type),
              static_cast<int>(first.size()));

    auto second = make_tcp_packet(64, 2, 101);
    second[31] = 1;
    set_ip_checksum(second);
    compressed_len = cslip_compress(compressor, second.data(), second.size(), compressed, &type);
    ASSERT_GT(compressed_len, 0);
    ASSERT_EQ(type, VJ_TYPE_COMPRESSED_TCP);
    EXPECT_NE(compressed[0] & VJ_NEW_C, 0);
    EXPECT_EQ(compressed[1], 1);
    const int restored_len = cslip_decompress(decompressor, compressed, compressed_len,
                                               restored, type);
    ASSERT_EQ(restored_len, static_cast<int>(second.size()));
    EXPECT_TRUE(std::equal(second.begin(), second.end(), restored));

    cslip_close(decompressor);
    cslip_close(compressor);
}

TEST(ModemCslip, RoundTripsVjSpecialDataPacket)
{
    cslip_ctx_t *compressor = cslip_init(nullptr);
    cslip_ctx_t *decompressor = cslip_init(nullptr);
    ASSERT_NE(compressor, nullptr);
    ASSERT_NE(decompressor, nullptr);
    uint8_t compressed[VJ_MAX_HDR + 64]{};
    uint8_t restored[VJ_MAX_HDR + 64]{};
    int type = 0;

    auto first = make_tcp_packet(64, 1, 100);
    first.resize(44, 0x41);
    first[3] = 44;
    set_ip_checksum(first);
    int compressed_len = cslip_compress(compressor, first.data(), first.size(), compressed, &type);
    ASSERT_GT(compressed_len, 0);
    ASSERT_EQ(type, VJ_TYPE_UNCOMPRESSED_TCP);
    ASSERT_EQ(cslip_decompress(decompressor, compressed, compressed_len, restored, type), 44);
    ASSERT_TRUE(std::equal(first.begin(), first.end(), restored));

    auto second = first;
    second[5] = 2;
    second[27] = 104;
    set_ip_checksum(second);
    compressed_len = cslip_compress(compressor, second.data(), second.size(), compressed, &type);
    ASSERT_GT(compressed_len, 0);
    ASSERT_EQ(type, VJ_TYPE_COMPRESSED_TCP);
    const uint8_t changes = compressed[0] & 0x0F;
    EXPECT_EQ(changes, VJ_SPECIAL_D);
    const int restored_len = cslip_decompress(decompressor, compressed, compressed_len, restored, type);

    EXPECT_EQ(restored_len, 44);
    for (size_t pos = 0; pos < second.size(); pos++) {
        SCOPED_TRACE(pos);
        EXPECT_EQ(restored[pos], second[pos]);
    }
    cslip_close(decompressor);
    cslip_close(compressor);
}

TEST(ModemCslip, CompressesIndependentSequenceAndAckDeltas)
{
    cslip_ctx_t *ctx = cslip_init(nullptr);
    ASSERT_NE(ctx, nullptr);
    uint8_t output[VJ_MAX_HDR + 64]{};
    int type = 0;
    auto first = make_tcp_packet(64, 1, 100);
    ASSERT_GT(cslip_compress(ctx, first.data(), first.size(), output, &type), 0);

    auto second = make_tcp_packet(64, 2, 103);
    second[31] = 1;
    set_ip_checksum(second);
    EXPECT_GT(cslip_compress(ctx, second.data(), second.size(), output, &type), 0);
    EXPECT_EQ(type, VJ_TYPE_COMPRESSED_TCP);
    cslip_close(ctx);
}

TEST(ModemCslip, UsesPreviousPayloadLengthForSpecialDataEncoding)
{
    cslip_ctx_t *compressor = cslip_init(nullptr);
    cslip_ctx_t *decompressor = cslip_init(nullptr);
    ASSERT_NE(compressor, nullptr);
    ASSERT_NE(decompressor, nullptr);
    uint8_t compressed[VJ_MAX_HDR + 64]{};
    uint8_t restored[VJ_MAX_HDR + 64]{};
    int type = 0;

    auto first = make_tcp_packet(64, 1, 100);
    first.resize(44, 0x41);
    first[3] = 44;
    set_ip_checksum(first);
    int compressed_len = cslip_compress(compressor, first.data(), first.size(), compressed, &type);
    ASSERT_GT(compressed_len, 0);
    ASSERT_EQ(type, VJ_TYPE_UNCOMPRESSED_TCP);
    ASSERT_EQ(cslip_decompress(decompressor, compressed, compressed_len, restored, type), 44);

    auto second = first;
    second.resize(45, 0x42);
    second[3] = 45;
    second[5] = 2;
    second[27] = 105;
    set_ip_checksum(second);
    compressed_len = cslip_compress(compressor, second.data(), second.size(), compressed, &type);
    ASSERT_GT(compressed_len, 0);
    ASSERT_EQ(type, VJ_TYPE_COMPRESSED_TCP);
    EXPECT_NE(compressed[0] & 0x0F, VJ_SPECIAL_D);
    const int restored_len = cslip_decompress(decompressor, compressed, compressed_len, restored, type);

    EXPECT_EQ(restored_len, static_cast<int>(second.size()));
    for (size_t pos = 0; pos < second.size(); pos++) {
        SCOPED_TRACE(pos);
        EXPECT_EQ(restored[pos], second[pos]);
    }
    cslip_close(decompressor);
    cslip_close(compressor);
}

TEST(ModemCslip, LeavesNonTcpAndFragmentedPacketsUncompressed)
{
    cslip_ctx_t *ctx = cslip_init(nullptr);
    ASSERT_NE(ctx, nullptr);
    uint8_t output[128]{};
    int type = 0;
    auto packet = make_tcp_packet(64, 1, 100);

    packet[9] = 17;
    EXPECT_EQ(cslip_compress(ctx, packet.data(), packet.size(), output, &type),
              static_cast<int>(packet.size()));
    EXPECT_EQ(type, VJ_TYPE_IP);

    packet = make_tcp_packet(64, 1, 100);
    packet[33] = 0;
    EXPECT_EQ(cslip_compress(ctx, packet.data(), packet.size(), output, &type),
              static_cast<int>(packet.size()));
    EXPECT_EQ(type, VJ_TYPE_IP);

    packet = make_tcp_packet(64, 1, 100);
    packet[33] = 0x02;
    EXPECT_EQ(cslip_compress(ctx, packet.data(), packet.size(), output, &type),
              static_cast<int>(packet.size()));
    EXPECT_EQ(type, VJ_TYPE_IP);

    packet = make_tcp_packet(64, 2, 101);
    packet[6] = 0x20;
    EXPECT_EQ(cslip_compress(ctx, packet.data(), packet.size(), output, &type),
              static_cast<int>(packet.size()));
    EXPECT_EQ(type, VJ_TYPE_IP);
    cslip_close(ctx);
}

TEST(ModemCslip, DropsCompressedPacketWithoutSavedConnectionState)
{
    cslip_ctx_t *ctx = cslip_init(nullptr);
    ASSERT_NE(ctx, nullptr);
    const uint8_t packet[] = { VJ_TYPE_COMPRESSED_TCP, 0, 0 };
    uint8_t output[32]{};

    EXPECT_EQ(cslip_decompress(ctx, packet, sizeof(packet), output, VJ_TYPE_COMPRESSED_TCP), 0);
    EXPECT_NE(ctx->flags & VJ_FLAG_TOSS, 0);
    cslip_close(ctx);
}

TEST(ModemCslip, TossesCompressedPacketsAfterInvalidUncompressedLength)
{
    cslip_ctx_t *ctx = cslip_init(nullptr);
    ASSERT_NE(ctx, nullptr);
    uint8_t output[VJ_MAX_HDR + 64]{};
    auto valid = make_tcp_packet(64, 1, 100);
    valid[9] = VJ_TYPE_UNCOMPRESSED_TCP;
    ASSERT_EQ(cslip_decompress(ctx, valid.data(), static_cast<int>(valid.size()),
                              output, VJ_TYPE_UNCOMPRESSED_TCP),
              static_cast<int>(valid.size()));

    auto malformed = valid;
    malformed[2] = 0;
    malformed[3] = 20;
    EXPECT_EQ(cslip_decompress(ctx, malformed.data(), static_cast<int>(malformed.size()),
                               output, VJ_TYPE_UNCOMPRESSED_TCP), 0);
    EXPECT_NE(ctx->flags & VJ_FLAG_TOSS, 0);

    const uint8_t compressed[] = { VJ_TYPE_COMPRESSED_TCP, 0, 0 };
    EXPECT_EQ(cslip_decompress(ctx, compressed, sizeof(compressed), output,
                               VJ_TYPE_COMPRESSED_TCP), 0);
    cslip_close(ctx);
}

TEST(ModemCslip, TossesImplicitSlotOutsideNegotiatedRange)
{
    cslip_ctx_t *ctx = cslip_init(nullptr);
    ASSERT_NE(ctx, nullptr);
    ctx->num_slots = 1;
    ctx->last_conn_recv = 1;
    ctx->slots[1].hdr_len = 40;
    const uint8_t compressed[] = { VJ_TYPE_COMPRESSED_TCP, 0, 0 };
    uint8_t output[VJ_MAX_HDR + 64]{};

    EXPECT_EQ(cslip_decompress(ctx, compressed, sizeof(compressed), output,
                               VJ_TYPE_COMPRESSED_TCP), 0);
    EXPECT_NE(ctx->flags & VJ_FLAG_TOSS, 0);
    cslip_close(ctx);
}