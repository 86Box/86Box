#include <cstddef>
#include <cstdint>
#include <array>
#include <gtest/gtest.h>
#include <86box/modem/modem_ppp.h>
#include <86box/modem/modem_mppe.h>

TEST(ModemMppe, DerivesRfc3079ServerSendKey)
{
    const std::array<uint8_t, 16> password_hash_hash = {
        0x41, 0xC0, 0x0C, 0x58, 0x4B, 0xD2, 0xD9, 0x1C,
        0x40, 0x17, 0xA2, 0xA1, 0x2F, 0xA5, 0x9F, 0x3F
    };
    const std::array<uint8_t, 24> nt_response = {
        0x82, 0x30, 0x9E, 0xCD, 0x8D, 0x70, 0x8B, 0x5E,
        0xA0, 0x8F, 0xAA, 0x39, 0x81, 0xCD, 0x83, 0x54,
        0x42, 0x33, 0x11, 0x4A, 0x3D, 0x85, 0xD6, 0xDF
    };
    const std::array<uint8_t, 16> expected_start = {
        0x8B, 0x7C, 0xDC, 0x14, 0x9B, 0x99, 0x3A, 0x1B,
        0xA1, 0x18, 0xCB, 0x15, 0x3F, 0x56, 0xDC, 0xCB
    };
    const std::array<uint8_t, 16> expected_session = {
        0x40, 0x5C, 0xB2, 0x24, 0x7A, 0x79, 0x56, 0xE6,
        0xE2, 0x11, 0x00, 0x7A, 0xE2, 0x7B, 0x22, 0xD4
    };
    ppp_mppe_state_t send_state{};
    ppp_mppe_state_t receive_state{};

    ppp_mppe_derive_mschapv2_keys(password_hash_hash.data(), nt_response.data(),
                                  &send_state, &receive_state);

    EXPECT_EQ(std::equal(expected_start.begin(), expected_start.end(), send_state.start_key), true);
    EXPECT_EQ(std::equal(expected_session.begin(), expected_session.end(), send_state.session_key), true);
}

TEST(ModemMppe, DerivesRfc3079MschapV1KeysAtAllStrengths)
{
    const std::array<uint8_t, 16> password_hash_hash = {
        0x41, 0xC0, 0x0C, 0x58, 0x4B, 0xD2, 0xD9, 0x1C,
        0x40, 0x17, 0xA2, 0xA1, 0x2F, 0xA5, 0x9F, 0x3F
    };
    const std::array<uint8_t, 16> lm_password_hash = {
        0x76, 0xA1, 0x52, 0x93, 0x60, 0x96, 0xD7, 0x83,
        0x0E, 0x23, 0x90, 0x22, 0x74, 0x04, 0xAF, 0xD2
    };
    const std::array<uint8_t, 8> challenge = {
        0x10, 0x2D, 0xB5, 0xDF, 0x08, 0x5D, 0x30, 0x41
    };
    const std::array<uint8_t, 16> expected_start = {
        0xA8, 0x94, 0x78, 0x50, 0xCF, 0xC0, 0xAC, 0xC1,
        0xD1, 0x78, 0x9F, 0xB6, 0x2D, 0xDC, 0xDD, 0xB0
    };
    const std::array<uint8_t, 16> expected_128 = {
        0x59, 0xD1, 0x59, 0xBC, 0x09, 0xF7, 0x6F, 0x1D,
        0xA2, 0xA8, 0x6A, 0x28, 0xFF, 0xEC, 0x0B, 0x1E
    };
    const std::array<uint8_t, 8> expected_40 = {
        0xD1, 0x26, 0x9E, 0x53, 0x8C, 0xEC, 0x4A, 0x08
    };
    const std::array<uint8_t, 8> expected_56 = {
        0xD1, 0x08, 0x01, 0x53, 0x8C, 0xEC, 0x4A, 0x08
    };
    ppp_mppe_state_t send_state{};
    ppp_mppe_state_t receive_state{};

    ppp_mppe_derive_mschapv1_keys(password_hash_hash.data(), lm_password_hash.data(),
                                  challenge.data(), &send_state, &receive_state);
    EXPECT_TRUE(std::equal(expected_start.begin(), expected_start.end(), send_state.start_key));
    EXPECT_TRUE(std::equal(expected_128.begin(), expected_128.end(), send_state.session_key));
    EXPECT_TRUE(std::equal(send_state.start_key, send_state.start_key + PPP_MPPE_KEY_LENGTH,
                           receive_state.start_key));
    ASSERT_TRUE(ppp_mppe_configure(&send_state, 40, false));
    EXPECT_TRUE(std::equal(expected_40.begin(), expected_40.end(), send_state.session_key));
    ASSERT_TRUE(ppp_mppe_configure(&send_state, 56, false));
    EXPECT_TRUE(std::equal(expected_56.begin(), expected_56.end(), send_state.session_key));
}

TEST(ModemMppe, EncryptsAndDecryptsAStatelessPacket)
{
    const std::array<uint8_t, 16> password_hash_hash = {
        0x41, 0xC0, 0x0C, 0x58, 0x4B, 0xD2, 0xD9, 0x1C,
        0x40, 0x17, 0xA2, 0xA1, 0x2F, 0xA5, 0x9F, 0x3F
    };
    const std::array<uint8_t, 24> nt_response = {
        0x82, 0x30, 0x9E, 0xCD, 0x8D, 0x70, 0x8B, 0x5E,
        0xA0, 0x8F, 0xAA, 0x39, 0x81, 0xCD, 0x83, 0x54,
        0x42, 0x33, 0x11, 0x4A, 0x3D, 0x85, 0xD6, 0xDF
    };
    const uint8_t plain[] = "test message";
    ppp_mppe_state_t send_state{};
    ppp_mppe_state_t receive_state{};
    uint8_t packet[32]{};
    uint8_t restored[32]{};
    size_t packet_len = 0;
    size_t restored_len = 0;

    ppp_mppe_derive_mschapv2_keys(password_hash_hash.data(), nt_response.data(),
                                  &send_state, &receive_state);
    receive_state = send_state;

    ASSERT_TRUE(ppp_mppe_encrypt(&send_state, plain, sizeof(plain) - 1, 0,
                                 packet, sizeof(packet), &packet_len));
    ASSERT_EQ(packet_len, sizeof(plain) - 1 + PPP_MPPE_HEADER_LENGTH);
    EXPECT_EQ(packet[0], 0x90);
    EXPECT_EQ(packet[1], 0);

    ASSERT_TRUE(ppp_mppe_decrypt(&receive_state, packet, packet_len,
                                 restored, sizeof(restored), &restored_len));
    EXPECT_EQ(restored_len, sizeof(plain) - 1);
    EXPECT_TRUE(std::equal(plain, plain + restored_len, restored));
}

TEST(ModemMppe, StatelessReceiveResynchronizesAfterPacketLoss)
{
    const std::array<uint8_t, 16> password_hash_hash = {
        0x41, 0xC0, 0x0C, 0x58, 0x4B, 0xD2, 0xD9, 0x1C,
        0x40, 0x17, 0xA2, 0xA1, 0x2F, 0xA5, 0x9F, 0x3F
    };
    const std::array<uint8_t, 24> nt_response = {
        0x82, 0x30, 0x9E, 0xCD, 0x8D, 0x70, 0x8B, 0x5E,
        0xA0, 0x8F, 0xAA, 0x39, 0x81, 0xCD, 0x83, 0x54,
        0x42, 0x33, 0x11, 0x4A, 0x3D, 0x85, 0xD6, 0xDF
    };
    ppp_mppe_state_t send_state{};
    ppp_mppe_state_t receive_state{};
    uint8_t packet[32]{};
    uint8_t restored[32]{};
    size_t packet_len = 0;
    size_t restored_len = 0;
    const uint8_t first[] = { 0x00, 0x21, 1 };
    const uint8_t second[] = { 0x00, 0x21, 2 };
    const uint8_t third[] = { 0x00, 0x21, 3 };

    ppp_mppe_derive_mschapv2_keys(password_hash_hash.data(), nt_response.data(),
                                  &send_state, &receive_state);
    receive_state = send_state;

    ASSERT_TRUE(ppp_mppe_encrypt(&send_state, first, sizeof(first), 0, packet,
                                 sizeof(packet), &packet_len));
    ASSERT_TRUE(ppp_mppe_decrypt(&receive_state, packet, packet_len, restored,
                                 sizeof(restored), &restored_len));
    ASSERT_TRUE(ppp_mppe_encrypt(&send_state, second, sizeof(second), 0, packet,
                                 sizeof(packet), &packet_len));
    ASSERT_TRUE(ppp_mppe_encrypt(&send_state, third, sizeof(third), 0, packet,
                                 sizeof(packet), &packet_len));
    ASSERT_EQ(packet[1], 2);
    ASSERT_TRUE(ppp_mppe_decrypt(&receive_state, packet, packet_len, restored,
                                 sizeof(restored), &restored_len));
    EXPECT_EQ(restored_len, sizeof(third));
    EXPECT_TRUE(std::equal(third, third + sizeof(third), restored));
}

TEST(ModemMppe, DerivesRfc3079FortyAndFiftySixBitKeys)
{
    const std::array<uint8_t, 16> password_hash_hash = {
        0x41, 0xC0, 0x0C, 0x58, 0x4B, 0xD2, 0xD9, 0x1C,
        0x40, 0x17, 0xA2, 0xA1, 0x2F, 0xA5, 0x9F, 0x3F
    };
    const std::array<uint8_t, 24> nt_response = {
        0x82, 0x30, 0x9E, 0xCD, 0x8D, 0x70, 0x8B, 0x5E,
        0xA0, 0x8F, 0xAA, 0x39, 0x81, 0xCD, 0x83, 0x54,
        0x42, 0x33, 0x11, 0x4A, 0x3D, 0x85, 0xD6, 0xDF
    };
    const std::array<uint8_t, 8> expected_40 = {
        0xD1, 0x26, 0x9E, 0xC4, 0x9F, 0xA6, 0x2E, 0x3E
    };
    const std::array<uint8_t, 8> expected_56 = {
        0xD1, 0x5C, 0x00, 0xC4, 0x9F, 0xA6, 0x2E, 0x3E
    };
    ppp_mppe_state_t send_state{};
    ppp_mppe_state_t receive_state{};

    ppp_mppe_derive_mschapv2_keys(password_hash_hash.data(), nt_response.data(),
                                  &send_state, &receive_state);
    ASSERT_TRUE(ppp_mppe_configure(&send_state, 40, false));
    EXPECT_EQ(send_state.key_length, 8);
    EXPECT_TRUE(std::equal(expected_40.begin(), expected_40.end(), send_state.session_key));
    ASSERT_TRUE(ppp_mppe_configure(&send_state, 56, false));
    EXPECT_EQ(send_state.key_length, 8);
    EXPECT_TRUE(std::equal(expected_56.begin(), expected_56.end(), send_state.session_key));
}

TEST(ModemMppe, StatefulRekeysOnTheCoherencyFlagPacket)
{
    const std::array<uint8_t, 16> password_hash_hash = {
        0x41, 0xC0, 0x0C, 0x58, 0x4B, 0xD2, 0xD9, 0x1C,
        0x40, 0x17, 0xA2, 0xA1, 0x2F, 0xA5, 0x9F, 0x3F
    };
    const std::array<uint8_t, 24> nt_response = {
        0x82, 0x30, 0x9E, 0xCD, 0x8D, 0x70, 0x8B, 0x5E,
        0xA0, 0x8F, 0xAA, 0x39, 0x81, 0xCD, 0x83, 0x54,
        0x42, 0x33, 0x11, 0x4A, 0x3D, 0x85, 0xD6, 0xDF
    };
    ppp_mppe_state_t send_state{};
    ppp_mppe_state_t receive_state{};
    uint8_t packet[16]{};
    uint8_t restored[16]{};
    size_t packet_len = 0;
    size_t restored_len = 0;
    const uint8_t payload[] = { 0x00, 0x21, 0x45, 0x00 };

    ppp_mppe_derive_mschapv2_keys(password_hash_hash.data(), nt_response.data(),
                                  &send_state, &receive_state);
    ASSERT_TRUE(ppp_mppe_configure(&send_state, 128, true));
    receive_state = send_state;
    for (uint16_t count = 0; count <= 0x00FF; count++) {
        ASSERT_TRUE(ppp_mppe_encrypt(&send_state, payload, sizeof(payload), 0, packet,
                                     sizeof(packet), &packet_len));
        ASSERT_EQ(packet[1], count & 0xFF);
        if (count == 0x00FE) {
            EXPECT_EQ(packet[0] & 0x80, 0);
        }
        if (count == 0x00FF) {
            EXPECT_NE(packet[0] & 0x80, 0);
        }
        ASSERT_TRUE(ppp_mppe_decrypt(&receive_state, packet, packet_len, restored,
                                     sizeof(restored), &restored_len));
        EXPECT_TRUE(std::equal(payload, payload + sizeof(payload), restored));
    }
}

TEST(ModemMppe, StatefulPacketLossRequestsAndRecoversWithFlush)
{
    const std::array<uint8_t, 16> password_hash_hash = {
        0x41, 0xC0, 0x0C, 0x58, 0x4B, 0xD2, 0xD9, 0x1C,
        0x40, 0x17, 0xA2, 0xA1, 0x2F, 0xA5, 0x9F, 0x3F
    };
    const std::array<uint8_t, 24> nt_response = {
        0x82, 0x30, 0x9E, 0xCD, 0x8D, 0x70, 0x8B, 0x5E,
        0xA0, 0x8F, 0xAA, 0x39, 0x81, 0xCD, 0x83, 0x54,
        0x42, 0x33, 0x11, 0x4A, 0x3D, 0x85, 0xD6, 0xDF
    };
    ppp_mppe_state_t send_state{};
    ppp_mppe_state_t receive_state{};
    uint8_t packet[16]{};
    uint8_t restored[16]{};
    size_t packet_len = 0;
    size_t restored_len = 0;
    const uint8_t payload[] = { 0x00, 0x21, 0x45, 0x01 };

    ppp_mppe_derive_mschapv2_keys(password_hash_hash.data(), nt_response.data(),
                                  &send_state, &receive_state);
    ASSERT_TRUE(ppp_mppe_configure(&send_state, 128, true));
    receive_state = send_state;
    ASSERT_TRUE(ppp_mppe_encrypt(&send_state, payload, sizeof(payload), 0, packet,
                                 sizeof(packet), &packet_len));
    ASSERT_TRUE(ppp_mppe_decrypt(&receive_state, packet, packet_len, restored,
                                 sizeof(restored), &restored_len));
    ASSERT_TRUE(ppp_mppe_encrypt(&send_state, payload, sizeof(payload), 0, packet,
                                 sizeof(packet), &packet_len));
    ASSERT_TRUE(ppp_mppe_encrypt(&send_state, payload, sizeof(payload), 0, packet,
                                 sizeof(packet), &packet_len));
    EXPECT_FALSE(ppp_mppe_decrypt(&receive_state, packet, packet_len, restored,
                                  sizeof(restored), &restored_len));
    EXPECT_TRUE(receive_state.reset_requested);

    ppp_mppe_request_reset(&send_state);
    ASSERT_TRUE(ppp_mppe_encrypt(&send_state, payload, sizeof(payload), 0, packet,
                                 sizeof(packet), &packet_len));
    EXPECT_NE(packet[0] & 0x80, 0);
    ASSERT_TRUE(ppp_mppe_decrypt(&receive_state, packet, packet_len, restored,
                                 sizeof(restored), &restored_len));
    EXPECT_FALSE(receive_state.discard);
    EXPECT_TRUE(std::equal(payload, payload + sizeof(payload), restored));
}

TEST(ModemMppe, StatefulResetResynchronizesAcrossKeyChanges)
{
    for (unsigned reset_count : { 255u, 514u }) {
        SCOPED_TRACE(reset_count);
        ppp_mppe_state_t send_state{};
        ppp_mppe_state_t receive_state{};
        uint8_t packet[32]{};
        uint8_t restored[32]{};
        size_t packet_len = 0;
        size_t restored_len = 0;
        const uint8_t payload[] = { 0x00, 0x21, 0x45 };
        ASSERT_TRUE(ppp_mppe_configure(&send_state, 128, true));
        receive_state = send_state;

        ASSERT_TRUE(ppp_mppe_encrypt(&send_state, payload, sizeof(payload), 0,
                                     packet, sizeof(packet), &packet_len));
        ASSERT_TRUE(ppp_mppe_decrypt(&receive_state, packet, packet_len, restored,
                                     sizeof(restored), &restored_len));
        ASSERT_TRUE(ppp_mppe_encrypt(&send_state, payload, sizeof(payload), 0,
                                     packet, sizeof(packet), &packet_len));
        ASSERT_TRUE(ppp_mppe_encrypt(&send_state, payload, sizeof(payload), 0,
                                     packet, sizeof(packet), &packet_len));
        EXPECT_FALSE(ppp_mppe_decrypt(&receive_state, packet, packet_len, restored,
                                      sizeof(restored), &restored_len));
        for (unsigned count = 3; count < reset_count; count++)
            ASSERT_TRUE(ppp_mppe_encrypt(&send_state, payload, sizeof(payload), 0,
                                         packet, sizeof(packet), &packet_len));
        ppp_mppe_request_reset(&send_state);
        ASSERT_TRUE(ppp_mppe_encrypt(&send_state, payload, sizeof(payload), 0,
                                     packet, sizeof(packet), &packet_len));
        ASSERT_TRUE(ppp_mppe_decrypt(&receive_state, packet, packet_len, restored,
                                     sizeof(restored), &restored_len));
        ASSERT_EQ(restored_len, sizeof(payload));
        EXPECT_TRUE(std::equal(payload, payload + sizeof(payload), restored));
    }
}

TEST(ModemMppe, StatefulResetWithoutPacketLossPreservesSynchronization)
{
    for (unsigned reset_count : { 1u, 255u, 256u }) {
        SCOPED_TRACE(reset_count);
        ppp_mppe_state_t send_state{};
        ppp_mppe_state_t receive_state{};
        uint8_t packet[32]{};
        uint8_t restored[32]{};
        size_t packet_len = 0;
        size_t restored_len = 0;
        const uint8_t payload[] = { 0x00, 0x21, 0x45 };
        ASSERT_TRUE(ppp_mppe_configure(&send_state, 128, true));
        receive_state = send_state;

        for (unsigned count = 0; count <= reset_count; count++) {
            if (count == reset_count)
                ppp_mppe_request_reset(&send_state);
            ASSERT_TRUE(ppp_mppe_encrypt(&send_state, payload, sizeof(payload), 0,
                                         packet, sizeof(packet), &packet_len));
            ASSERT_TRUE(ppp_mppe_decrypt(&receive_state, packet, packet_len, restored,
                                         sizeof(restored), &restored_len));
            ASSERT_EQ(restored_len, sizeof(payload));
            EXPECT_TRUE(std::equal(payload, payload + sizeof(payload), restored));
        }
    }
}