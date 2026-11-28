#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include <gtest/gtest.h>
#include <86box/modem/modem_ppp.h>

static std::vector<uint8_t> sent_packet;
static uint16_t sent_protocol;

static ppp_ctx_t
make_ccp_context()
{
    ppp_ctx_t state{};
    state.ppp_compression = PPP_COMPRESSION_PREFER_LZS;
    state.mppe_allowed_bits = CCP_MPPE_KEY_BITS;
    return state;
}

extern "C" {
void
ppp_send_frame(ppp_ctx_t *, uint16_t protocol, const uint8_t *data, int len)
{
    sent_protocol = protocol;
    sent_packet.assign(data, data + len);
}

void
ppp_advance_state(ppp_ctx_t *)
{
}

void
ppp_send_code_reject(ppp_ctx_t *, uint16_t, const uint8_t *, int)
{
}
}

TEST(ModemCcp, OffersSupportedKeyLengthsInStatelessModeAfterKeysExist)
{
    ppp_ctx_t ctx = make_ccp_context();
    ctx.mppe_keys_ready = true;

    ppp_ccp_start(&ctx);

    ASSERT_EQ(sent_protocol, PPP_PROTO_CCP);
    ASSERT_EQ(sent_packet.size(), 10u);
    EXPECT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_REQUEST);
    EXPECT_EQ(sent_packet[4], 18);
    EXPECT_EQ(sent_packet[5], 6);
    EXPECT_EQ(sent_packet[6], 1);
    EXPECT_EQ(sent_packet[7], 0);
    EXPECT_EQ(sent_packet[8], 0);
    EXPECT_EQ(sent_packet[9], 0xE1);
    EXPECT_TRUE(ctx.ccp_req_sent);
}

TEST(ModemCcp, FollowsCompressionOnlyPeerAndIgnoresStaleEncryptionNak)
{
    ppp_ctx_t ctx = make_ccp_context();
    ctx.mppe_keys_ready = true;
    ppp_ccp_start(&ctx);
    const uint8_t old_id = ctx.ccp_request_id;
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 1, 0, 15,
                                18, 6, 0, 0, 0, 1, 17, 5, 0, 1, 4 };
    ctx.ppp_compression = PPP_COMPRESSION_PREFER_MPPC;
    ppp_ccp_process(&ctx, request, sizeof(request));
    ASSERT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_REJECT);
    ASSERT_NE(ctx.ccp_request_id, old_id);
    EXPECT_EQ(ctx.ccp_request_bits, CCP_MPPC);
    const uint8_t stale_nak[] = { PPP_CODE_CONFIGURE_NAK, old_id, 0, 10,
                                  18, 6, 1, 0, 0, 0x41 };
    ppp_ccp_process(&ctx, stale_nak, sizeof(stale_nak));
    EXPECT_EQ(ctx.ccp_request_bits, CCP_MPPC);
    const uint8_t retry[] = { PPP_CODE_CONFIGURE_REQUEST, 2, 0, 10,
                              18, 6, 0, 0, 0, 1 };
    ppp_ccp_process(&ctx, retry, sizeof(retry));
    EXPECT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_ACK);
    std::vector<uint8_t> ack(ctx.ccp_request, ctx.ccp_request + ctx.ccp_request_len);
    ack[0] = PPP_CODE_CONFIGURE_ACK;
    ppp_ccp_process(&ctx, ack.data(), static_cast<int>(ack.size()));
    EXPECT_TRUE(ctx.ccp_open);
    EXPECT_EQ(ctx.ccp_tx_method, PPP_CCP_METHOD_MPPC);
    EXPECT_EQ(ctx.ccp_rx_method, PPP_CCP_METHOD_MPPC);
    EXPECT_FALSE(ctx.mppe_tx_enabled);
    EXPECT_FALSE(ctx.mppe_rx_enabled);
    ppp_ccp_codec_close(&ctx);
}

TEST(ModemCcp, RenegotiatesEvenIfEncryptionWasAlreadyAcknowledged)
{
    ppp_ctx_t ctx = make_ccp_context();
    ctx.mppe_keys_ready = true;
    ppp_ccp_start(&ctx);
    std::vector<uint8_t> ack(ctx.ccp_request, ctx.ccp_request + ctx.ccp_request_len);
    ack[0] = PPP_CODE_CONFIGURE_ACK;
    ppp_ccp_process(&ctx, ack.data(), static_cast<int>(ack.size()));
    ASSERT_TRUE(ctx.ccp_ack_received);
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 1, 0, 10,
                                18, 6, 0, 0, 0, 1 };
    ppp_ccp_process(&ctx, request, sizeof(request));
    EXPECT_FALSE(ctx.ccp_ack_received);
    EXPECT_TRUE(ctx.ccp_req_sent);
    EXPECT_EQ(ctx.ccp_request_bits, CCP_MPPC);
    EXPECT_FALSE(ctx.mppe_rx_enabled);
    ppp_ccp_codec_close(&ctx);
}

TEST(ModemCcp, RequiredEncryptionDoesNotFollowCompressionOnlyPeer)
{
    ppp_ctx_t ctx = make_ccp_context();
    ctx.mppe_keys_ready = true;
    ctx.mppe_min_bits = 128;
    ppp_ccp_start(&ctx);
    const uint32_t bits = ctx.ccp_request_bits;
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 1, 0, 10,
                                18, 6, 0, 0, 0, 1 };
    ppp_ccp_process(&ctx, request, sizeof(request));
    EXPECT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_REJECT);
    EXPECT_EQ(ctx.ccp_request_bits, bits);
    EXPECT_FALSE(ctx.ccp_open);
}

TEST(ModemCcp, OffersPreferredLzsWithoutMschapKeys)
{
    ppp_ctx_t ctx = make_ccp_context();
    sent_packet.clear();

    ppp_ccp_start(&ctx);

    ASSERT_EQ(sent_packet.size(), 9u);
    EXPECT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_REQUEST);
    EXPECT_EQ(sent_packet[2], 0);
    EXPECT_EQ(sent_packet[3], 9);
    EXPECT_EQ(sent_packet[4], 17);
    EXPECT_EQ(sent_packet[5], 5);
    EXPECT_EQ(sent_packet[6], 0);
    EXPECT_EQ(sent_packet[7], 1);
    EXPECT_EQ(sent_packet[8], 4);
    EXPECT_EQ(ctx.ccp_request_method, PPP_CCP_METHOD_LZS_EXTENDED);
    EXPECT_TRUE(ctx.ccp_req_sent);
}

TEST(ModemCcp, FallsBackFromLzsToMppc)
{
    ppp_ctx_t ctx = make_ccp_context();
    ppp_ccp_start(&ctx);
    std::vector<uint8_t> reject = sent_packet;
    reject[0] = PPP_CODE_CONFIGURE_REJECT;
    ppp_ccp_process(&ctx, reject.data(), static_cast<int>(reject.size()));

    ASSERT_EQ(sent_packet.size(), 10u);
    EXPECT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_REQUEST);
    EXPECT_EQ(sent_packet[2], 0);
    EXPECT_EQ(sent_packet[3], 10);
    EXPECT_EQ(sent_packet[4], 18);
    EXPECT_EQ(sent_packet[5], 6);
    EXPECT_EQ(sent_packet[6], 0);
    EXPECT_EQ(sent_packet[7], 0);
    EXPECT_EQ(sent_packet[8], 0);
    EXPECT_EQ(sent_packet[9], 1);
    EXPECT_EQ(ctx.ccp_request_method, PPP_CCP_METHOD_MPPC);
    EXPECT_TRUE(ctx.ccp_req_sent);
}

TEST(ModemCcp, NegotiatesPreferredCodecsInFallbackOrder)
{
    ppp_ctx_t ctx = make_ccp_context();
    const uint8_t expected_methods[] = {
        PPP_CCP_METHOD_LZS_EXTENDED,
        PPP_CCP_METHOD_MPPC,
        PPP_CCP_METHOD_PREDICTOR1
    };
    ppp_ccp_start(&ctx);

    for (size_t index = 0; index < sizeof(expected_methods); index++) {
        ASSERT_EQ(ctx.ccp_request_method, expected_methods[index]);
        if (index + 1 == sizeof(expected_methods))
            break;
        std::vector<uint8_t> reject(ctx.ccp_request,
                                    ctx.ccp_request + ctx.ccp_request_len);
        reject[0] = PPP_CODE_CONFIGURE_REJECT;
        ppp_ccp_process(&ctx, reject.data(), static_cast<int>(reject.size()));
    }

    std::vector<uint8_t> ack(ctx.ccp_request, ctx.ccp_request + ctx.ccp_request_len);
    ack[0] = PPP_CODE_CONFIGURE_ACK;
    ppp_ccp_process(&ctx, ack.data(), static_cast<int>(ack.size()));
    EXPECT_EQ(ctx.ccp_rx_method, PPP_CCP_METHOD_PREDICTOR1);
    EXPECT_NE(ctx.ccp_rx_codec_state, nullptr);
    ppp_ccp_codec_close(&ctx);
}

TEST(ModemCcp, RestrictsOfferToRequiredKeyStrength)
{
    ppp_ctx_t ctx = make_ccp_context();
    ctx.mppe_keys_ready = true;
    ctx.mppe_min_bits = 56;
    ctx.mppe_allowed_bits = CCP_MPPE_56 | CCP_MPPE_128;

    ppp_ccp_start(&ctx);

    ASSERT_EQ(sent_packet.size(), 10u);
    EXPECT_EQ(sent_packet[9], 0xC1);
}

TEST(ModemCcp, NaksMultiStrengthRequestToStrongestSupportedOption)
{
    ppp_ctx_t ctx = make_ccp_context();
    ctx.mppe_keys_ready = true;
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 3, 0, 10,
                                18, 6, 1, 0, 0, 0xE0 };

    ppp_ccp_process(&ctx, request, sizeof(request));

    ASSERT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_NAK);
    EXPECT_EQ(sent_packet[4], 18);
    EXPECT_EQ(sent_packet[8], 0);
    EXPECT_EQ(sent_packet[9], 0x40);
    EXPECT_FALSE(ctx.ccp_ack_sent);
}

TEST(ModemCcp, AcceptsConnectionlessStacLzsRequest)
{
    ppp_ctx_t ctx = make_ccp_context();
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 7, 0, 9,
                                17, 5, 0, 0, 0 };

    ppp_ccp_process(&ctx, request, sizeof(request));

    EXPECT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_ACK);
    EXPECT_EQ(ctx.ccp_tx_method, PPP_CCP_METHOD_LZS);
    EXPECT_NE(ctx.ccp_tx_codec_state, nullptr);
}

TEST(ModemCcp, AcceptsStacLzsExtendedModeRequest)
{
    ppp_ctx_t ctx = make_ccp_context();
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 7, 0, 9,
                                17, 5, 0, 1, 4 };

    ppp_ccp_process(&ctx, request, sizeof(request));

    EXPECT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_ACK);
    EXPECT_EQ(ctx.ccp_tx_method, PPP_CCP_METHOD_LZS_EXTENDED);
    EXPECT_NE(ctx.ccp_tx_codec_state, nullptr);
}

TEST(ModemCcp, AcceptsAllStandardStacLzsCheckModes)
{
    for (uint8_t check_mode = 0; check_mode <= 3; check_mode++) {
        ppp_ctx_t ctx = make_ccp_context();
        uint16_t history_count = check_mode == 0 ? 0 : 1;
        const uint8_t request[] = {
            PPP_CODE_CONFIGURE_REQUEST, 7, 0, 9,
            17, 5, 0, static_cast<uint8_t>(history_count), check_mode
        };

        ppp_ccp_process(&ctx, request, sizeof(request));

        ASSERT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_ACK) << "check mode " << +check_mode;
        EXPECT_EQ(ctx.ccp_tx_method, PPP_CCP_METHOD_LZS) << "check mode " << +check_mode;
        EXPECT_NE(ctx.ccp_tx_codec_state, nullptr) << "check mode " << +check_mode;
        ppp_ccp_codec_close(&ctx);
    }
}

TEST(ModemCcp, RejectsMppcOnlyWhenMppeIsRequired)
{
    ppp_ctx_t ctx = make_ccp_context();
    ctx.mppe_min_bits = 128;
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 2, 0, 10,
                                18, 6, 0, 0, 0, 1 };

    ppp_ccp_process(&ctx, request, sizeof(request));

    ASSERT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_REJECT);
    EXPECT_FALSE(ctx.ccp_ack_sent);
    EXPECT_EQ(ctx.ccp_tx_method, PPP_CCP_METHOD_NONE);
}

TEST(ModemCcp, AcceptsCombinedMppcAndEachMppeKeyStrength)
{
    for (uint8_t key_flags : { 0x20, 0x80, 0x40 }) {
        ppp_ctx_t ctx = make_ccp_context();
        ctx.mppe_keys_ready = true;
        const uint8_t request[] = {
            PPP_CODE_CONFIGURE_REQUEST, 2, 0, 10,
            18, 6, 0, 0, 0, static_cast<uint8_t>(0x01 | key_flags)
        };

        ppp_ccp_process(&ctx, request, sizeof(request));

        ASSERT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_ACK) << "key flags " << +key_flags;
        EXPECT_EQ(ctx.ccp_tx_method, PPP_CCP_METHOD_MPPC);
        EXPECT_TRUE(ctx.ccp_peer_mppe);
        EXPECT_EQ(ctx.mppe_tx.key_bits, key_flags == 0x20 ? 40
                                    : key_flags == 0x80 ? 56 : 128);
        EXPECT_NE(ctx.ccp_tx_codec_state, nullptr);
        ppp_ccp_codec_close(&ctx);
    }
}

TEST(ModemCcp, ExtendedLzsResetFlushesNextPacketWithoutAck)
{
    ppp_ctx_t ctx = make_ccp_context();
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 7, 0, 9,
                                17, 5, 0, 1, 4 };
    ppp_ccp_process(&ctx, request, sizeof(request));
    std::array<uint8_t, 32> encoded{};
    const uint8_t packet[] = { 0, PPP_PROTO_IP, 'a', 'a', 'a', 'a', 'a', 'a' };
    int encoded_len;
    ASSERT_TRUE(ppp_ccp_codec_compress(&ctx, packet, sizeof(packet), encoded.data(),
                                       encoded.size(), &encoded_len));

    sent_packet.clear();
    const uint8_t reset[] = { PPP_CODE_RESET_REQUEST, 9, 0, 6, 0, 1 };
    ppp_ccp_process(&ctx, reset, sizeof(reset));

    EXPECT_TRUE(sent_packet.empty());
    ASSERT_TRUE(ppp_ccp_codec_compress(&ctx, packet, sizeof(packet), encoded.data(),
                                       encoded.size(), &encoded_len));
    EXPECT_NE(encoded[0] & 0x80, 0);
    EXPECT_EQ(encoded[1], 1);
    ppp_ccp_codec_close(&ctx);
}

TEST(ModemCcp, NegotiatesExtendedLzsForReceiveAfterConfigureAck)
{
    ppp_ctx_t ctx = make_ccp_context();
    ppp_ccp_start(&ctx);
    ASSERT_EQ(ctx.ccp_request_method, PPP_CCP_METHOD_LZS_EXTENDED);

    std::vector<uint8_t> ack(ctx.ccp_request, ctx.ccp_request + ctx.ccp_request_len);
    ack[0] = PPP_CODE_CONFIGURE_ACK;
    ppp_ccp_process(&ctx, ack.data(), static_cast<int>(ack.size()));

    EXPECT_EQ(ctx.ccp_rx_method, PPP_CCP_METHOD_LZS_EXTENDED);
    EXPECT_NE(ctx.ccp_rx_codec_state, nullptr);
    ppp_ccp_codec_close(&ctx);
}

TEST(ModemCcp, AcknowledgesStacLzsResetWithHistoryNumber)
{
    ppp_ctx_t ctx = make_ccp_context();
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 7, 0, 9,
                                17, 5, 0, 0, 0 };
    ppp_ccp_process(&ctx, request, sizeof(request));
    const uint8_t reset[] = { PPP_CODE_RESET_REQUEST, 9, 0, 6, 0, 1 };

    ppp_ccp_process(&ctx, reset, sizeof(reset));

    ASSERT_EQ(sent_packet.size(), 6u);
    EXPECT_EQ(sent_packet[0], PPP_CODE_RESET_ACK);
    EXPECT_EQ(sent_packet[1], 9);
    EXPECT_EQ(sent_packet[4], 0);
    EXPECT_EQ(sent_packet[5], 1);
}

TEST(ModemCcp, AcceptsStacLzsSequenceNumberMode)
{
    ppp_ctx_t ctx = make_ccp_context();
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 8, 0, 9,
                                17, 5, 0, 1, 3 };

    ppp_ccp_process(&ctx, request, sizeof(request));

    ASSERT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_ACK);
    EXPECT_EQ(ctx.ccp_tx_method, PPP_CCP_METHOD_LZS);
    EXPECT_NE(ctx.ccp_tx_codec_state, nullptr);
    ppp_ccp_codec_close(&ctx);
}

TEST(ModemCcp, AcceptsAckForPendingConnectionlessLzsDcpRequest)
{
    ppp_ctx_t ctx = make_ccp_context();
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 1, 0, 10, 23, 6, 0, 0, 0, 0 };
    std::memcpy(ctx.ccp_request, request, sizeof(request));
    ctx.ccp_request_len = sizeof(request);
    ctx.ccp_request_id = request[1];
    ctx.ccp_request_method = PPP_CCP_METHOD_LZS_DCP;
    ctx.ccp_req_sent = true;

    std::vector<uint8_t> ack(ctx.ccp_request, ctx.ccp_request + ctx.ccp_request_len);
    ack[0] = PPP_CODE_CONFIGURE_ACK;
    ppp_ccp_process(&ctx, ack.data(), static_cast<int>(ack.size()));
    EXPECT_EQ(ctx.ccp_rx_method, PPP_CCP_METHOD_LZS_DCP);
    EXPECT_NE(ctx.ccp_rx_codec_state, nullptr);
    ppp_ccp_codec_close(&ctx);
}

TEST(ModemCcp, NaksUnsupportedLzsDcpModesToConnectionlessDefaults)
{
    ppp_ctx_t ctx = make_ccp_context();
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 8, 0, 10,
                                23, 6, 0, 1, 3, 1 };

    ppp_ccp_process(&ctx, request, sizeof(request));

    ASSERT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_NAK);
    ASSERT_EQ(sent_packet.size(), 10u);
    EXPECT_EQ(sent_packet[4], 23);
    EXPECT_EQ(sent_packet[5], 6);
    EXPECT_EQ(sent_packet[6], 0);
    EXPECT_EQ(sent_packet[7], 0);
    EXPECT_EQ(sent_packet[8], 0);
    EXPECT_EQ(sent_packet[9], 0);
    EXPECT_EQ(ctx.ccp_tx_method, PPP_CCP_METHOD_NONE);
}

TEST(ModemCcp, AcceptsPeerSelectedStateful56BitNak)
{
    ppp_ctx_t ctx = make_ccp_context();
    ctx.mppe_keys_ready = true;
    ppp_ccp_start(&ctx);
    const uint8_t nak[] = { PPP_CODE_CONFIGURE_NAK, ctx.ccp_request_id, 0, 10,
                            18, 6, 0, 0, 0, 0x80 };

    ppp_ccp_process(&ctx, nak, sizeof(nak));

    ASSERT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_REQUEST);
    EXPECT_EQ(sent_packet[6], 0);
    EXPECT_EQ(sent_packet[7], 0);
    EXPECT_EQ(sent_packet[8], 0);
    EXPECT_EQ(sent_packet[9], 0x80);
    EXPECT_EQ(ctx.ccp_request_bits, 0x80u);
}

TEST(ModemCcp, ReplacesMppeOfferWhenPeerRequestsMppcOnly)
{
    for (bool ack_first : { false, true }) {
        ppp_ctx_t ctx = make_ccp_context();
        ctx.mppe_keys_ready = true;
        ctx.mppe_allowed_bits = CCP_MPPE_KEY_BITS;
        ctx.ppp_compression = PPP_COMPRESSION_PREFER_MPPC;
        ppp_ccp_start(&ctx);
        std::vector<uint8_t> original = sent_packet;
        if (ack_first) {
            std::vector<uint8_t> ack = original;
            ack[0] = PPP_CODE_CONFIGURE_ACK;
            ppp_ccp_process(&ctx, ack.data(), static_cast<int>(ack.size()));
        }

        const uint8_t peer_request[] = {
            PPP_CODE_CONFIGURE_REQUEST, 4, 0, 10, 18, 6, 0, 0, 0, 1
        };
        ppp_ccp_process(&ctx, peer_request, sizeof(peer_request));

        ASSERT_EQ(sent_packet.size(), 10u);
        EXPECT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_ACK);
        EXPECT_EQ(sent_packet[9], CCP_MPPC);
        EXPECT_NE(ctx.ccp_request_id, original[1]);
        EXPECT_EQ(ctx.ccp_request_bits, CCP_MPPC);
        EXPECT_FALSE(ctx.ccp_open);
        ppp_ccp_codec_close(&ctx);
    }
}

TEST(ModemCcp, KeepsMppeOfferWhenPeerRequestsEncryption)
{
    ppp_ctx_t ctx = make_ccp_context();
    ctx.mppe_keys_ready = true;
    ctx.mppe_allowed_bits = CCP_MPPE_KEY_BITS;
    ctx.ppp_compression = PPP_COMPRESSION_PREFER_MPPC;
    ppp_ccp_start(&ctx);
    const uint8_t original_id = ctx.ccp_request_id;
    const uint32_t original_bits = ctx.ccp_request_bits;
    const uint8_t peer_request[] = {
        PPP_CODE_CONFIGURE_REQUEST, 4, 0, 10, 18, 6, 0, 0, 0, 0x41
    };

    ppp_ccp_process(&ctx, peer_request, sizeof(peer_request));

    EXPECT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_ACK);
    EXPECT_TRUE(ctx.ccp_peer_mppe);
    EXPECT_EQ(ctx.ccp_request_id, original_id);
    EXPECT_EQ(ctx.ccp_request_bits, original_bits);
    ppp_ccp_codec_close(&ctx);
}

TEST(ModemCcp, DoesNotDowngradeRequiredMppeForPeerMppcOnlyRequest)
{
    ppp_ctx_t ctx = make_ccp_context();
    ctx.mppe_keys_ready = true;
    ctx.mppe_allowed_bits = CCP_MPPE_KEY_BITS;
    ctx.mppe_min_bits = 128;
    ctx.ppp_compression = PPP_COMPRESSION_PREFER_MPPC;
    ppp_ccp_start(&ctx);
    const uint8_t original_id = ctx.ccp_request_id;
    const uint32_t original_bits = ctx.ccp_request_bits;
    const uint8_t peer_request[] = {
        PPP_CODE_CONFIGURE_REQUEST, 4, 0, 10, 18, 6, 0, 0, 0, 1
    };

    ppp_ccp_process(&ctx, peer_request, sizeof(peer_request));

    EXPECT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_REJECT);
    EXPECT_EQ(ctx.ccp_request_id, original_id);
    EXPECT_EQ(ctx.ccp_request_bits, original_bits);
    EXPECT_FALSE(ctx.ccp_ack_sent);
    ppp_ccp_codec_close(&ctx);
}

TEST(ModemCcp, RejectsMppeWhenAuthenticationDidNotDeriveKeys)
{
    ppp_ctx_t ctx = make_ccp_context();
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 4, 0, 10,
                                18, 6, 1, 0, 0, 0xE0 };

    ppp_ccp_process(&ctx, request, sizeof(request));

    ASSERT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_REJECT);
    EXPECT_EQ(sent_packet.size(), sizeof(request));
    EXPECT_EQ(std::memcmp(sent_packet.data() + 4, request + 4, 6), 0);
    EXPECT_FALSE(ctx.mppe_tx_enabled);
}

TEST(ModemCcp, AcceptsStateful56BitPeerRequest)
{
    ppp_ctx_t ctx = make_ccp_context();
    ctx.mppe_keys_ready = true;
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 8, 0, 10,
                                18, 6, 0, 0, 0, 0x80 };

    ppp_ccp_process(&ctx, request, sizeof(request));

    ASSERT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_ACK);
    EXPECT_TRUE(ctx.ccp_peer_mppe);
    EXPECT_EQ(ctx.mppe_tx.key_bits, 56);
    EXPECT_TRUE(ctx.mppe_tx.stateful);
}

TEST(ModemCcp, NaksPeerRequestBelowRequiredStrength)
{
    ppp_ctx_t ctx = make_ccp_context();
    ctx.mppe_keys_ready = true;
    ctx.mppe_min_bits = 56;
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 8, 0, 10,
                                18, 6, 0, 0, 0, 0x20 };

    ppp_ccp_process(&ctx, request, sizeof(request));

    ASSERT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_NAK);
    EXPECT_EQ(sent_packet[9], 0x80);
    EXPECT_FALSE(ctx.ccp_ack_sent);
}

TEST(ModemCcp, OpensOnlyAfterBothDirectionsAreNegotiated)
{
    ppp_ctx_t ctx = make_ccp_context();
    ctx.mppe_keys_ready = true;
    ppp_ccp_start(&ctx);
    std::vector<uint8_t> ack = sent_packet;
    ack[0] = PPP_CODE_CONFIGURE_ACK;
    ppp_ccp_process(&ctx, ack.data(), static_cast<int>(ack.size()));
    EXPECT_FALSE(ctx.ccp_open);
    EXPECT_FALSE(ctx.mppe_rx_enabled);

    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 9, 0, 10,
                                18, 6, 1, 0, 0, 0x40 };
    ppp_ccp_process(&ctx, request, sizeof(request));

    EXPECT_TRUE(ctx.ccp_open);
    EXPECT_TRUE(ctx.mppe_rx_enabled);
    EXPECT_TRUE(ctx.mppe_tx_enabled);
}

TEST(ModemCcp, FallsBackToPreferredLzsWhenOptionalMppeIsRejected)
{
    ppp_ctx_t ctx = make_ccp_context();
    ctx.mppe_keys_ready = true;
    ctx.mppe_format = PPP_MPPE_FORMAT_STANDARD;
    ppp_ccp_start(&ctx);
    const std::vector<uint8_t> reject = {
        PPP_CODE_CONFIGURE_REJECT, ctx.ccp_request_id, 0, 10,
        18, 6, 1, 0, 0, 0xE1
    };

    ppp_ccp_process(&ctx, reject.data(), static_cast<int>(reject.size()));

    EXPECT_FALSE(ctx.ccp_plaintext_fallback);
    EXPECT_FALSE(ctx.ccp_open);
    EXPECT_FALSE(ctx.mppe_tx_enabled);
    EXPECT_FALSE(ctx.mppe_rx_enabled);
    EXPECT_EQ(ctx.ccp_request_method, PPP_CCP_METHOD_LZS_EXTENDED);
    EXPECT_TRUE(ctx.ccp_req_sent);
}

TEST(ModemCcp, FailsClosedWhenRequiredMppeIsRejected)
{
    ppp_ctx_t ctx = make_ccp_context();
    ctx.state = PPP_STATE_IPCP_NEGOTIATE;
    ctx.mppe_keys_ready = true;
    ctx.mppe_min_bits = 128;
    ctx.mppe_allowed_bits = CCP_MPPE_128;
    ctx.mppe_format = PPP_MPPE_FORMAT_STANDARD;
    ppp_ccp_start(&ctx);
    ASSERT_TRUE(ctx.ccp_req_sent);
    std::vector<uint8_t> reject = sent_packet;
    reject[0] = PPP_CODE_CONFIGURE_REJECT;

    ppp_ccp_process(&ctx, reject.data(), static_cast<int>(reject.size()));

    EXPECT_EQ(ctx.state, PPP_STATE_DEAD);
    EXPECT_FALSE(ctx.ccp_plaintext_fallback);
    EXPECT_FALSE(ctx.mppe_tx_enabled);
    EXPECT_FALSE(ctx.mppe_rx_enabled);
}

TEST(ModemCcp, Predictor1RoundTripsRawAndCompressedPackets)
{
    ppp_ctx_t tx{};
    ppp_ctx_t rx{};
    std::array<uint8_t, 128> packet{};
    std::array<uint8_t, 160> encoded{};
    std::array<uint8_t, 128> decoded{};
    int encoded_len;
    int decoded_len;

    for (size_t index = 0; index < packet.size(); index++)
        packet[index] = static_cast<uint8_t>((index / 8) % 4);
    ASSERT_TRUE(ppp_ccp_codec_set(&tx, true, PPP_CCP_METHOD_PREDICTOR1));
    ASSERT_TRUE(ppp_ccp_codec_set(&rx, false, PPP_CCP_METHOD_PREDICTOR1));

    ASSERT_TRUE(ppp_ccp_codec_compress(&tx, packet.data(), packet.size(), encoded.data(),
                                       encoded.size(), &encoded_len));
    ASSERT_TRUE(ppp_ccp_codec_decompress(&rx, encoded.data(), encoded_len, decoded.data(),
                                         decoded.size(), &decoded_len));
    ASSERT_EQ(decoded_len, packet.size());
    EXPECT_EQ(decoded, packet);

    ASSERT_TRUE(ppp_ccp_codec_compress(&tx, packet.data(), packet.size(), encoded.data(),
                                       encoded.size(), &encoded_len));
    EXPECT_NE(encoded[0] & 0x80, 0);
    ASSERT_TRUE(ppp_ccp_codec_decompress(&rx, encoded.data(), encoded_len, decoded.data(),
                                         decoded.size(), &decoded_len));
    EXPECT_EQ(decoded, packet);

    ppp_ccp_codec_close(&tx);
    ppp_ccp_codec_close(&rx);
}

TEST(ModemCcp, DeflateRoundTripsPacketsWithSequence)
{
    ppp_ctx_t tx{};
    ppp_ctx_t rx{};
    std::array<uint8_t, 128> packet{};
    std::array<uint8_t, 256> encoded{};
    std::array<uint8_t, 128> decoded{};
    int encoded_len;
    int decoded_len;

    for (size_t index = 0; index < packet.size(); index++)
        packet[index] = static_cast<uint8_t>((index / 8) % 4);
    ASSERT_TRUE(ppp_ccp_codec_set(&tx, true, PPP_CCP_METHOD_DEFLATE));
    ASSERT_TRUE(ppp_ccp_codec_set(&rx, false, PPP_CCP_METHOD_DEFLATE));

    for (int packet_index = 0; packet_index < 2; packet_index++) {
        ASSERT_TRUE(ppp_ccp_codec_compress(&tx, packet.data(), packet.size(), encoded.data(),
                                           encoded.size(), &encoded_len));
        EXPECT_EQ(encoded[0], 0);
        EXPECT_EQ(encoded[1], packet_index);
        ASSERT_TRUE(ppp_ccp_codec_decompress(&rx, encoded.data(), encoded_len, decoded.data(),
                                             decoded.size(), &decoded_len));
        ASSERT_EQ(decoded_len, packet.size());
        EXPECT_EQ(decoded, packet);
    }

    ppp_ccp_codec_close(&tx);
    ppp_ccp_codec_close(&rx);
}

TEST(ModemCcp, MppcRoundTripsLzMatches)
{
    ppp_ctx_t tx{};
    ppp_ctx_t rx{};
    std::array<uint8_t, 128> packet{};
    std::array<uint8_t, 160> encoded{};
    std::array<uint8_t, 128> decoded{};
    int encoded_len;
    int decoded_len;

    for (size_t index = 0; index < packet.size(); index++)
        packet[index] = static_cast<uint8_t>((index / 8) % 4);
    ASSERT_TRUE(ppp_ccp_codec_set(&tx, true, PPP_CCP_METHOD_MPPC));
    ASSERT_TRUE(ppp_ccp_codec_set(&rx, false, PPP_CCP_METHOD_MPPC));

    ASSERT_TRUE(ppp_ccp_codec_compress(&tx, packet.data(), packet.size(), encoded.data(),
                                       encoded.size(), &encoded_len));
    EXPECT_EQ(encoded[0] & 0xA0, 0xA0);
    EXPECT_LT(encoded_len, packet.size());
    ASSERT_TRUE(ppp_ccp_codec_decompress(&rx, encoded.data(), encoded_len, decoded.data(),
                                         decoded.size(), &decoded_len));
    ASSERT_EQ(decoded_len, packet.size());
    EXPECT_EQ(decoded, packet);

    ppp_ccp_codec_close(&tx);
    ppp_ccp_codec_close(&rx);
}

TEST(ModemCcp, LzsEncodesRfcLiteralAndEndMarker)
{
    ppp_ctx_t tx{};
    const std::array<uint8_t, 1> packet = { 'A' };
    const std::array<uint8_t, 3> expected = { 0x20, 0xE0, 0x00 };
    std::array<uint8_t, 8> encoded{};
    int encoded_len;

    ASSERT_TRUE(ppp_ccp_codec_set(&tx, true, PPP_CCP_METHOD_LZS));
    ASSERT_TRUE(ppp_ccp_codec_compress(&tx, packet.data(), packet.size(), encoded.data(),
                                       encoded.size(), &encoded_len));
    ASSERT_EQ(encoded_len, expected.size());
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(), encoded.begin()));
    ppp_ccp_codec_close(&tx);
}

TEST(ModemCcp, LzsRestoresOptionalRemovedTrailingZero)
{
    ppp_ctx_t rx{};
    const std::array<uint8_t, 2> encoded_without_padding = { 0x20, 0xE0 };
    std::array<uint8_t, 8> decoded{};
    int decoded_len;

    ASSERT_TRUE(ppp_ccp_codec_set(&rx, false, PPP_CCP_METHOD_LZS));
    ASSERT_TRUE(ppp_ccp_codec_decompress(&rx, encoded_without_padding.data(),
                                         encoded_without_padding.size(), decoded.data(),
                                         decoded.size(), &decoded_len));
    ASSERT_EQ(decoded_len, 1);
    EXPECT_EQ(decoded[0], 'A');
    ppp_ccp_codec_close(&rx);
}

TEST(ModemCcp, LzsNoneLcbAndCrcModesUseTheirNegotiatedPacketFormats)
{
    const std::array<uint8_t, 1> packet = { 'A' };
    const std::array<uint8_t, 3> no_check = { 0x20, 0xE0, 0x00 };
    const std::array<uint8_t, 4> lcb = { 0xBE, 0x20, 0xE0, 0x00 };
    const std::array<uint8_t, 5> crc = { 0xF5, 0xA3, 0x20, 0xE0, 0x00 };
    std::array<uint8_t, 16> encoded{};
    std::array<uint8_t, 8> decoded{};

    for (uint8_t check_mode = 0; check_mode <= 2; check_mode++) {
        ppp_ctx_t tx{};
        ppp_ctx_t rx{};
        int encoded_len;
        int decoded_len;
        uint16_t history_count = check_mode == 0 ? 0 : 1;

        ASSERT_TRUE(ppp_ccp_codec_set_lzs(&tx, true, PPP_CCP_METHOD_LZS,
                                          history_count, check_mode));
        ASSERT_TRUE(ppp_ccp_codec_set_lzs(&rx, false, PPP_CCP_METHOD_LZS,
                                          history_count, check_mode));
        ASSERT_TRUE(ppp_ccp_codec_compress(&tx, packet.data(), packet.size(),
                                           encoded.data(), encoded.size(), &encoded_len));
        if (check_mode == 0) {
            ASSERT_EQ(encoded_len, no_check.size());
            EXPECT_TRUE(std::equal(no_check.begin(), no_check.end(), encoded.begin()));
        } else if (check_mode == 1) {
            ASSERT_EQ(encoded_len, lcb.size());
            EXPECT_TRUE(std::equal(lcb.begin(), lcb.end(), encoded.begin()));
        } else {
            ASSERT_EQ(encoded_len, crc.size());
            EXPECT_TRUE(std::equal(crc.begin(), crc.end(), encoded.begin()));
        }
        ASSERT_TRUE(ppp_ccp_codec_decompress(&rx, encoded.data(), encoded_len,
                                             decoded.data(), decoded.size(), &decoded_len));
        ASSERT_EQ(decoded_len, packet.size());
        EXPECT_EQ(decoded[0], packet[0]);

        if (check_mode != 0) {
            encoded[0] ^= 1;
            EXPECT_FALSE(ppp_ccp_codec_decompress(&rx, encoded.data(), encoded_len,
                                                  decoded.data(), decoded.size(), &decoded_len));
        }
        ppp_ccp_codec_close(&tx);
        ppp_ccp_codec_close(&rx);
    }
}

TEST(ModemCcp, LzsRoundTripsLiteralAndMatchPackets)
{
    ppp_ctx_t tx{};
    ppp_ctx_t rx{};
    std::array<uint8_t, 128> packet{};
    std::array<uint8_t, 160> encoded{};
    std::array<uint8_t, 128> decoded{};
    int encoded_len;
    int decoded_len;

    for (size_t index = 0; index < packet.size(); index++)
        packet[index] = static_cast<uint8_t>((index / 8) % 4);
    ASSERT_TRUE(ppp_ccp_codec_set(&tx, true, PPP_CCP_METHOD_LZS));
    ASSERT_TRUE(ppp_ccp_codec_set(&rx, false, PPP_CCP_METHOD_LZS));
    ASSERT_TRUE(ppp_ccp_codec_compress(&tx, packet.data(), packet.size(), encoded.data(),
                                       encoded.size(), &encoded_len));
    EXPECT_LT(encoded_len, packet.size());
    ASSERT_TRUE(ppp_ccp_codec_decompress(&rx, encoded.data(), encoded_len, decoded.data(),
                                         decoded.size(), &decoded_len));
    EXPECT_EQ(decoded_len, packet.size());
    EXPECT_EQ(decoded, packet);
    ppp_ccp_codec_close(&tx);
    ppp_ccp_codec_close(&rx);
}

TEST(ModemCcp, LzsExtendedFramesAndRoundTripsWithCoherency)
{
    ppp_ctx_t tx{};
    ppp_ctx_t rx{};
    std::array<uint8_t, 128> packet{};
    std::array<uint8_t, 256> encoded{};
    std::array<uint8_t, 128> decoded{};
    int encoded_len;
    int decoded_len;

    for (size_t index = 0; index < packet.size(); index++)
        packet[index] = static_cast<uint8_t>((index / 8) % 4);
    ASSERT_TRUE(ppp_ccp_codec_set(&tx, true, PPP_CCP_METHOD_LZS_EXTENDED));
    ASSERT_TRUE(ppp_ccp_codec_set(&rx, false, PPP_CCP_METHOD_LZS_EXTENDED));

    ASSERT_TRUE(ppp_ccp_codec_compress(&tx, packet.data(), packet.size(), encoded.data(),
                                       encoded.size(), &encoded_len));
    ASSERT_GE(encoded_len, 4);
    EXPECT_EQ(encoded[0] & 0xA0, 0xA0);
    EXPECT_EQ(encoded[1], 0);
    ASSERT_TRUE(ppp_ccp_codec_decompress(&rx, encoded.data(), encoded_len, decoded.data(),
                                         decoded.size(), &decoded_len));
    EXPECT_EQ(decoded_len, packet.size());
    EXPECT_EQ(decoded, packet);

    ASSERT_TRUE(ppp_ccp_codec_compress(&tx, packet.data(), packet.size(), encoded.data(),
                                       encoded.size(), &encoded_len));
    EXPECT_EQ(encoded[2] & 0x80, 0);
    EXPECT_EQ(encoded[1], 1);
    ASSERT_TRUE(ppp_ccp_codec_decompress(&rx, encoded.data(), encoded_len, decoded.data(),
                                         decoded.size(), &decoded_len));
    EXPECT_EQ(decoded_len, packet.size());
    EXPECT_EQ(decoded, packet);

    ppp_ccp_codec_close(&tx);
    ppp_ccp_codec_close(&rx);
}

TEST(ModemCcp, LzsDcpRoundTripsCompressedConnectionlessPacket)
{
    ppp_ctx_t tx{};
    ppp_ctx_t rx{};
    std::array<uint8_t, 128> packet{};
    std::array<uint8_t, 160> encoded{};
    std::array<uint8_t, 128> decoded{};
    int encoded_len;
    int decoded_len;

    for (size_t index = 0; index < packet.size(); index++)
        packet[index] = static_cast<uint8_t>((index / 8) % 4);
    ASSERT_TRUE(ppp_ccp_codec_set(&tx, true, PPP_CCP_METHOD_LZS_DCP));
    ASSERT_TRUE(ppp_ccp_codec_set(&rx, false, PPP_CCP_METHOD_LZS_DCP));
    ASSERT_TRUE(ppp_ccp_codec_compress(&tx, packet.data(), packet.size(), encoded.data(),
                                       encoded.size(), &encoded_len));
    ASSERT_EQ(encoded[0], 0xE0);
    ASSERT_TRUE(ppp_ccp_codec_decompress(&rx, encoded.data(), encoded_len, decoded.data(),
                                         decoded.size(), &decoded_len));
    EXPECT_EQ(decoded_len, packet.size());
    EXPECT_EQ(decoded, packet);
    ppp_ccp_codec_close(&tx);
    ppp_ccp_codec_close(&rx);
}

TEST(ModemCcp, LzsDcpRestoresOptionalRemovedTrailingZero)
{
    ppp_ctx_t rx{};
    const std::array<uint8_t, 3> encoded_without_padding = { 0xE0, 0x20, 0xE0 };
    std::array<uint8_t, 8> decoded{};
    int decoded_len;

    ASSERT_TRUE(ppp_ccp_codec_set(&rx, false, PPP_CCP_METHOD_LZS_DCP));
    ASSERT_TRUE(ppp_ccp_codec_decompress(&rx, encoded_without_padding.data(),
                                         encoded_without_padding.size(), decoded.data(),
                                         decoded.size(), &decoded_len));
    ASSERT_EQ(decoded_len, 1);
    EXPECT_EQ(decoded[0], 'A');
    ppp_ccp_codec_close(&rx);
}

TEST(ModemCcp, LzsDcpUsesUncompressedDcpPacketWhenCompressionExpands)
{
    ppp_ctx_t tx{};
    ppp_ctx_t rx{};
    const std::array<uint8_t, 2> packet = { 0x00, 0x21 };
    std::array<uint8_t, 8> encoded{};
    std::array<uint8_t, 8> decoded{};
    int encoded_len;
    int decoded_len;

    ASSERT_TRUE(ppp_ccp_codec_set(&tx, true, PPP_CCP_METHOD_LZS_DCP));
    ASSERT_TRUE(ppp_ccp_codec_set(&rx, false, PPP_CCP_METHOD_LZS_DCP));
    ASSERT_TRUE(ppp_ccp_codec_compress(&tx, packet.data(), packet.size(), encoded.data(),
                                       encoded.size(), &encoded_len));
    ASSERT_EQ(encoded[0], 0xA0);
    ASSERT_EQ(encoded_len, packet.size() + 1);
    ASSERT_TRUE(ppp_ccp_codec_decompress(&rx, encoded.data(), encoded_len, decoded.data(),
                                         decoded.size(), &decoded_len));
    EXPECT_EQ(decoded_len, packet.size());
    EXPECT_EQ(decoded[0], packet[0]);
    EXPECT_EQ(decoded[1], packet[1]);
    ppp_ccp_codec_close(&tx);
    ppp_ccp_codec_close(&rx);
}

TEST(ModemCcp, V44EncodesBasicDatagramAndResetsBetweenPackets)
{
    ppp_ctx_t tx{};
    ppp_ctx_t rx{};
    const std::array<uint8_t, 2> short_packet = { 'A', 'B' };
    const std::array<uint8_t, 12> repeated_packet = {
        'A', 'B', 'A', 'B', 'A', 'B', 'A', 'B', 'A', 'B', 'A', 'B'
    };
    const std::array<uint8_t, 3> expected = { 0x82, 0x84, 0x03 };
    std::array<uint8_t, 32> encoded{};
    std::array<uint8_t, 16> decoded{};
    std::array<uint8_t, 32> fresh_encoded{};
    int encoded_len;
    int decoded_len;
    int fresh_encoded_len;

    ASSERT_TRUE(ppp_ccp_codec_set(&tx, true, PPP_CCP_METHOD_V44));
    ASSERT_TRUE(ppp_ccp_codec_set(&rx, false, PPP_CCP_METHOD_V44));
    ASSERT_TRUE(ppp_ccp_codec_compress(&tx, short_packet.data(), short_packet.size(),
                                       encoded.data(), encoded.size(), &encoded_len));
    ASSERT_EQ(encoded_len, expected.size());
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(), encoded.begin()));
    ASSERT_TRUE(ppp_ccp_codec_decompress(&rx, encoded.data(), encoded_len, decoded.data(),
                                         decoded.size(), &decoded_len));
    EXPECT_EQ(decoded_len, short_packet.size());
    EXPECT_TRUE(std::equal(short_packet.begin(), short_packet.end(), decoded.begin()));

    ASSERT_TRUE(ppp_ccp_codec_compress(&tx, repeated_packet.data(), repeated_packet.size(),
                                       encoded.data(), encoded.size(), &encoded_len));
    ASSERT_TRUE(ppp_ccp_codec_decompress(&rx, encoded.data(), encoded_len, decoded.data(),
                                         decoded.size(), &decoded_len));
    EXPECT_EQ(decoded_len, repeated_packet.size());
    EXPECT_TRUE(std::equal(repeated_packet.begin(), repeated_packet.end(), decoded.begin()));

    ppp_ctx_t fresh_tx{};
    ASSERT_TRUE(ppp_ccp_codec_set(&fresh_tx, true, PPP_CCP_METHOD_V44));
    ASSERT_TRUE(ppp_ccp_codec_compress(&fresh_tx, repeated_packet.data(), repeated_packet.size(),
                                       fresh_encoded.data(), fresh_encoded.size(),
                                       &fresh_encoded_len));
    EXPECT_EQ(encoded_len, fresh_encoded_len);
    EXPECT_TRUE(std::equal(encoded.begin(), encoded.begin() + encoded_len,
                           fresh_encoded.begin()));
    ppp_ccp_codec_close(&tx);
    ppp_ccp_codec_close(&rx);
    ppp_ccp_codec_close(&fresh_tx);
}

TEST(ModemCcp, V44RejectsDatagramWithoutFlush)
{
    ppp_ctx_t rx{};
    const std::array<uint8_t, 1> truncated = { 0x41 };
    std::array<uint8_t, 8> decoded{};
    int decoded_len;

    ASSERT_TRUE(ppp_ccp_codec_set(&rx, false, PPP_CCP_METHOD_V44));
    EXPECT_FALSE(ppp_ccp_codec_decompress(&rx, truncated.data(), truncated.size(),
                                          decoded.data(), decoded.size(), &decoded_len));
    ppp_ccp_codec_close(&rx);
}

TEST(ModemCcp, MppcMatchesRfc2118LiteralAndTupleEncoding)
{
    ppp_ctx_t tx{};
    const std::array<uint8_t, 6> packet = { 'A', 'A', 'A', 'A', 'A', 'A' };
    const std::array<uint8_t, 5> expected = { 0xE0, 0x00, 0x41, 0xF0, 0x64 };
    std::array<uint8_t, 16> encoded{};
    int encoded_len;

    ASSERT_TRUE(ppp_ccp_codec_set(&tx, true, PPP_CCP_METHOD_MPPC));
    ASSERT_TRUE(ppp_ccp_codec_compress(&tx, packet.data(), packet.size(), encoded.data(),
                                       encoded.size(), &encoded_len));
    ASSERT_EQ(encoded_len, expected.size());
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(), encoded.begin()));
    ppp_ccp_codec_close(&tx);
}

TEST(ModemCcp, MppcUsesRawDatagramWhenCompressionExpands)
{
    ppp_ctx_t tx{};
    ppp_ctx_t rx{};
    std::array<uint8_t, 96> packet{};
    std::array<uint8_t, 96> compressible{};
    std::array<uint8_t, 160> encoded{};
    std::array<uint8_t, 96> decoded{};
    int encoded_len;
    int decoded_len;

    for (size_t index = 0; index < packet.size(); index++)
        packet[index] = static_cast<uint8_t>(index * 37 + 11);
    ASSERT_TRUE(ppp_ccp_codec_set(&tx, true, PPP_CCP_METHOD_MPPC));
    ASSERT_TRUE(ppp_ccp_codec_set(&rx, false, PPP_CCP_METHOD_MPPC));
    ASSERT_TRUE(ppp_ccp_codec_compress(&tx, packet.data(), packet.size(), encoded.data(),
                                       encoded.size(), &encoded_len));
    EXPECT_EQ(encoded[0] & 0x20, 0);
    ASSERT_TRUE(ppp_ccp_codec_decompress(&rx, encoded.data(), encoded_len, decoded.data(),
                                         decoded.size(), &decoded_len));
    EXPECT_EQ(decoded_len, packet.size());
    EXPECT_EQ(decoded, packet);

    for (size_t index = 0; index < compressible.size(); index++)
        compressible[index] = static_cast<uint8_t>((index / 8) % 4);
    ASSERT_TRUE(ppp_ccp_codec_compress(&tx, compressible.data(), compressible.size(),
                                       encoded.data(), encoded.size(), &encoded_len));
    EXPECT_EQ(encoded[0] & 0xA0, 0xA0);
    ASSERT_TRUE(ppp_ccp_codec_decompress(&rx, encoded.data(), encoded_len, decoded.data(),
                                         decoded.size(), &decoded_len));
    EXPECT_EQ(decoded_len, compressible.size());
    EXPECT_EQ(decoded, compressible);
    ppp_ccp_codec_close(&tx);
    ppp_ccp_codec_close(&rx);
}

TEST(ModemCcp, MppcRetainsHistoryAcrossDatagrams)
{
    ppp_ctx_t tx{};
    ppp_ctx_t rx{};
    std::array<uint8_t, 128> first_packet{};
    std::array<uint8_t, 32> second_packet{};
    std::array<uint8_t, 160> encoded{};
    std::array<uint8_t, 128> decoded{};
    int encoded_len;
    int decoded_len;

    for (size_t index = 0; index < 96; index++)
        first_packet[index] = static_cast<uint8_t>((index / 8) % 4);
    for (size_t index = 96; index < first_packet.size(); index++)
        first_packet[index] = static_cast<uint8_t>(index * 37 + 11);
    std::copy(first_packet.end() - second_packet.size(), first_packet.end(), second_packet.begin());

    ASSERT_TRUE(ppp_ccp_codec_set(&tx, true, PPP_CCP_METHOD_MPPC));
    ASSERT_TRUE(ppp_ccp_codec_set(&rx, false, PPP_CCP_METHOD_MPPC));
    ASSERT_TRUE(ppp_ccp_codec_compress(&tx, first_packet.data(), first_packet.size(),
                                       encoded.data(), encoded.size(), &encoded_len));
    EXPECT_EQ(encoded[0] & 0xA0, 0xA0);
    ASSERT_TRUE(ppp_ccp_codec_decompress(&rx, encoded.data(), encoded_len, decoded.data(),
                                         decoded.size(), &decoded_len));
    ASSERT_EQ(decoded_len, first_packet.size());
    EXPECT_EQ(decoded, first_packet);

    ASSERT_TRUE(ppp_ccp_codec_compress(&tx, second_packet.data(), second_packet.size(),
                                       encoded.data(), encoded.size(), &encoded_len));
    EXPECT_EQ(encoded[0] & 0x80, 0);
    EXPECT_NE(encoded[0] & 0x20, 0);
    EXPECT_LT(encoded_len, static_cast<int>(second_packet.size()));
    ASSERT_TRUE(ppp_ccp_codec_decompress(&rx, encoded.data(), encoded_len, decoded.data(),
                                         decoded.size(), &decoded_len));
    ASSERT_EQ(decoded_len, second_packet.size());
    EXPECT_TRUE(std::equal(second_packet.begin(), second_packet.end(), decoded.begin()));
    ppp_ccp_codec_close(&tx);
    ppp_ccp_codec_close(&rx);
}

TEST(ModemCcp, MppcMovesHistoryToFrontAtWindowBoundary)
{
    ppp_ctx_t tx{};
    ppp_ctx_t rx{};
    std::array<uint8_t, 1500> packet{};
    std::array<uint8_t, 1600> encoded{};
    std::array<uint8_t, 1500> decoded{};
    int encoded_len;
    int decoded_len;

    for (size_t index = 0; index < packet.size(); index++)
        packet[index] = static_cast<uint8_t>((index / 8) % 4);
    ASSERT_TRUE(ppp_ccp_codec_set(&tx, true, PPP_CCP_METHOD_MPPC));
    ASSERT_TRUE(ppp_ccp_codec_set(&rx, false, PPP_CCP_METHOD_MPPC));

    for (uint8_t packet_index = 0; packet_index < 6; packet_index++) {
        ASSERT_TRUE(ppp_ccp_codec_compress(&tx, packet.data(), packet.size(), encoded.data(),
                                           encoded.size(), &encoded_len));
        if (packet_index == 5) {
            EXPECT_NE(encoded[0] & 0x40, 0);
        }
        ASSERT_TRUE(ppp_ccp_codec_decompress(&rx, encoded.data(), encoded_len, decoded.data(),
                                             decoded.size(), &decoded_len));
        ASSERT_EQ(decoded_len, packet.size());
        EXPECT_EQ(decoded, packet);
    }
    ppp_ccp_codec_close(&tx);
    ppp_ccp_codec_close(&rx);
}

TEST(ModemCcp, MppcRejectsUnexpectedCoherencyCount)
{
    ppp_ctx_t tx{};
    ppp_ctx_t rx{};
    std::array<uint8_t, 64> packet{};
    std::array<uint8_t, 96> encoded{};
    std::array<uint8_t, 64> decoded{};
    int encoded_len;
    int decoded_len;

    for (size_t index = 0; index < packet.size(); index++)
        packet[index] = static_cast<uint8_t>((index / 4) % 3);
    ASSERT_TRUE(ppp_ccp_codec_set(&tx, true, PPP_CCP_METHOD_MPPC));
    ASSERT_TRUE(ppp_ccp_codec_set(&rx, false, PPP_CCP_METHOD_MPPC));
    ASSERT_TRUE(ppp_ccp_codec_compress(&tx, packet.data(), packet.size(), encoded.data(),
                                       encoded.size(), &encoded_len));
    ASSERT_TRUE(ppp_ccp_codec_decompress(&rx, encoded.data(), encoded_len, decoded.data(),
                                         decoded.size(), &decoded_len));
    ASSERT_TRUE(ppp_ccp_codec_compress(&tx, packet.data(), packet.size(), encoded.data(),
                                       encoded.size(), &encoded_len));
    encoded[1] ^= 0x04;

    EXPECT_FALSE(ppp_ccp_codec_decompress(&rx, encoded.data(), encoded_len, decoded.data(),
                                          decoded.size(), &decoded_len));
    ppp_ccp_codec_close(&tx);
    ppp_ccp_codec_close(&rx);
}

TEST(ModemCcp, MppcWrapsCoherencyCount)
{
    ppp_ctx_t tx{};
    ppp_ctx_t rx{};
    const std::array<uint8_t, 6> packet = { 'A', 'A', 'A', 'A', 'A', 'A' };
    std::array<uint8_t, 16> encoded{};
    std::array<uint8_t, 6> decoded{};
    int encoded_len;
    int decoded_len;

    ASSERT_TRUE(ppp_ccp_codec_set(&tx, true, PPP_CCP_METHOD_MPPC));
    ASSERT_TRUE(ppp_ccp_codec_set(&rx, false, PPP_CCP_METHOD_MPPC));

    for (int packet_index = 0; packet_index <= 0x1000; packet_index++) {
        ASSERT_TRUE(ppp_ccp_codec_compress(&tx, packet.data(), packet.size(), encoded.data(),
                                           encoded.size(), &encoded_len));
        uint16_t count = (uint16_t) (((encoded[0] & 0x0F) << 8) | encoded[1]);
        EXPECT_EQ(count, packet_index & 0x0FFF);
        if (packet_index == 0) {
            EXPECT_NE(encoded[0] & 0x80, 0);
        }
        if (encoded[0] & 0x80) {
            EXPECT_NE(encoded[0] & 0x40, 0);
        }
        ASSERT_TRUE(ppp_ccp_codec_decompress(&rx, encoded.data(), encoded_len, decoded.data(),
                                             decoded.size(), &decoded_len));
        ASSERT_EQ(decoded_len, packet.size());
        EXPECT_EQ(decoded, packet);
    }

    ppp_ccp_codec_close(&tx);
    ppp_ccp_codec_close(&rx);
}

TEST(ModemCcp, Predictor2BuffersFragmentsAndDrainsConcatenatedRecords)
{
    ppp_ctx_t tx{};
    ppp_ctx_t rx{};
    std::array<uint8_t, 128> packet{};
    std::array<uint8_t, 160> encoded{};
    std::array<uint8_t, 320> stream{};
    std::array<uint8_t, 128> decoded{};
    std::array<uint8_t, 128> first_packet{};
    std::array<uint8_t, 128> second_packet{};
    int encoded_len;
    int decoded_len;
    int first_encoded_len;
    int split_len;
    size_t stream_len;

    for (size_t index = 0; index < packet.size(); index++)
        packet[index] = static_cast<uint8_t>((index / 8) % 4);
    first_packet = packet;
    ASSERT_TRUE(ppp_ccp_codec_set(&tx, true, PPP_CCP_METHOD_PREDICTOR2));
    ASSERT_TRUE(ppp_ccp_codec_set(&rx, false, PPP_CCP_METHOD_PREDICTOR2));
    ASSERT_TRUE(ppp_ccp_codec_compress(&tx, packet.data(), packet.size(), encoded.data(),
                                       encoded.size(), &first_encoded_len));
    ASSERT_GT(first_encoded_len, 4);
    EXPECT_NE(encoded[0] & 0x80, 0);
    std::memcpy(stream.data(), encoded.data(), (size_t) first_encoded_len);

    for (size_t index = 0; index < packet.size(); index++)
        packet[index] = static_cast<uint8_t>(index * 37 + 11);
    second_packet = packet;
    ASSERT_TRUE(ppp_ccp_codec_compress(&tx, packet.data(), packet.size(), encoded.data(),
                                       encoded.size(), &encoded_len));
    EXPECT_EQ(encoded[0] & 0x80, 0);
    std::memcpy(stream.data() + first_encoded_len, encoded.data(), (size_t) encoded_len);
    stream_len = (size_t) first_encoded_len + (size_t) encoded_len;

    split_len = first_encoded_len / 2;
    ASSERT_GT(split_len, 2);
    ASSERT_TRUE(ppp_ccp_codec_decompress(&rx, stream.data(), split_len, decoded.data(),
                                         decoded.size(), &decoded_len));
    EXPECT_EQ(decoded_len, 0);
    ASSERT_TRUE(ppp_ccp_codec_decompress(&rx, stream.data() + split_len,
                                         static_cast<int>(stream_len - (size_t) split_len), decoded.data(),
                                         decoded.size(), &decoded_len));
    ASSERT_EQ(decoded_len, packet.size());
    EXPECT_EQ(decoded, first_packet);

    ASSERT_TRUE(ppp_ccp_codec_decompress(&rx, nullptr, 0, decoded.data(),
                                         decoded.size(), &decoded_len));
    ASSERT_EQ(decoded_len, packet.size());
    EXPECT_EQ(decoded, second_packet);

    ASSERT_TRUE(ppp_ccp_codec_decompress(&rx, nullptr, 0, decoded.data(),
                                         decoded.size(), &decoded_len));
    EXPECT_EQ(decoded_len, 0);
    ppp_ccp_codec_close(&tx);
    ppp_ccp_codec_close(&rx);
}

TEST(ModemCcp, Predictor2RejectsCorruptRecordCrc)
{
    ppp_ctx_t tx{};
    ppp_ctx_t rx{};
    std::array<uint8_t, 128> packet{};
    std::array<uint8_t, 160> encoded{};
    std::array<uint8_t, 128> decoded{};
    int encoded_len;
    int decoded_len = 99;

    for (size_t index = 0; index < packet.size(); index++)
        packet[index] = static_cast<uint8_t>((index / 8) % 4);
    ASSERT_TRUE(ppp_ccp_codec_set(&tx, true, PPP_CCP_METHOD_PREDICTOR2));
    ASSERT_TRUE(ppp_ccp_codec_set(&rx, false, PPP_CCP_METHOD_PREDICTOR2));
    ASSERT_TRUE(ppp_ccp_codec_compress(&tx, packet.data(), packet.size(), encoded.data(),
                                       encoded.size(), &encoded_len));
    encoded[encoded_len - 1] ^= 0x80;

    EXPECT_FALSE(ppp_ccp_codec_decompress(&rx, encoded.data(), encoded_len, decoded.data(),
                                          decoded.size(), &decoded_len));
    EXPECT_EQ(decoded_len, 0);
    ppp_ccp_codec_close(&tx);
    ppp_ccp_codec_close(&rx);
}

TEST(ModemCcp, BsdCompressRoundTripsLzwPackets)
{
    ppp_ctx_t tx{};
    ppp_ctx_t rx{};
    std::array<uint8_t, 1024> packet{};
    std::array<uint8_t, PPP_MAX_FRAME> encoded{};
    std::array<uint8_t, 1024> decoded{};
    int encoded_len;
    int decoded_len;
    uint32_t random = 0xA5C39E71;

    tx.ccp_tx_bsd_bits = 12;
    rx.ccp_rx_bsd_bits = 12;
    ASSERT_TRUE(ppp_ccp_codec_set_window(&tx, true, PPP_CCP_METHOD_BSD, 8192));
    ASSERT_TRUE(ppp_ccp_codec_set_window(&rx, false, PPP_CCP_METHOD_BSD, 8192));

    for (int packet_index = 0; packet_index < 10; packet_index++) {
        for (uint8_t &value : packet) {
            random = random * 1664525u + 1013904223u;
            value = static_cast<uint8_t>(random >> 24);
        }
        ASSERT_TRUE(ppp_ccp_codec_compress(&tx, packet.data(), packet.size(), encoded.data(),
                                           encoded.size(), &encoded_len));
        ASSERT_TRUE(ppp_ccp_codec_decompress(&rx, encoded.data(), encoded_len, decoded.data(),
                                             decoded.size(), &decoded_len));
        ASSERT_EQ(decoded_len, packet.size());
        EXPECT_EQ(decoded, packet);
    }

    ppp_ccp_codec_close(&tx);
    ppp_ccp_codec_close(&rx);
}

TEST(ModemCcp, AcceptsExtendedBsdDictionarySize)
{
    ppp_ctx_t ctx = make_ccp_context();
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 7, 0, 7, 21, 3, 0x2C };

    ppp_ccp_process(&ctx, request, sizeof(request));

    ASSERT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_ACK);
    EXPECT_EQ(ctx.ccp_tx_method, PPP_CCP_METHOD_BSD);
    EXPECT_EQ(ctx.ccp_tx_bsd_bits, 12);
    EXPECT_NE(ctx.ccp_tx_codec_state, nullptr);
}

TEST(ModemCcp, NaksUnsupportedBsdDictionarySize)
{
    ppp_ctx_t ctx = make_ccp_context();
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 8, 0, 7, 21, 3, 0x30 };

    ppp_ccp_process(&ctx, request, sizeof(request));

    ASSERT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_NAK);
    EXPECT_EQ(sent_packet[6], 0x2F);
}

TEST(ModemCcp, RetriesBsdNegotiationAtSmallerDictionarySize)
{
    ppp_ctx_t ctx = make_ccp_context();
    const uint8_t nak[] = { PPP_CODE_CONFIGURE_NAK, 9, 0, 7, 21, 3, 0x29 };
    ctx.ccp_req_sent = true;
    ctx.ccp_request_id = 9;
    ctx.ccp_request_method = PPP_CCP_METHOD_BSD;
    ctx.ccp_rx_bsd_bits = 12;

    ppp_ccp_process(&ctx, nak, sizeof(nak));

    ASSERT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_REQUEST);
    EXPECT_EQ(sent_packet[6], 0x29);
    EXPECT_EQ(ctx.ccp_rx_bsd_bits, 9);
}

TEST(ModemCcp, Nt31RasCodecRoundTripsAcrossPersistentFrames)
{
    ppp_ctx_t tx{};
    ppp_ctx_t rx{};
    std::array<uint8_t, 128> packet{};
    std::array<uint8_t, 256> encoded{};
    std::array<uint8_t, 128> decoded{};
    int encoded_len;
    int decoded_len;

    for (size_t index = 0; index < packet.size(); index++)
        packet[index] = static_cast<uint8_t>((index / 8) % 4);
    ASSERT_TRUE(ppp_ccp_codec_set(&tx, true, PPP_CCP_METHOD_NT31RAS));
    ASSERT_TRUE(ppp_ccp_codec_set(&rx, false, PPP_CCP_METHOD_NT31RAS));

    for (int packet_index = 0; packet_index < 2; packet_index++) {
        ASSERT_TRUE(ppp_ccp_codec_compress(&tx, packet.data(), packet.size(), encoded.data(),
                                           encoded.size(), &encoded_len));
        ASSERT_TRUE(ppp_ccp_codec_decompress(&rx, encoded.data(), encoded_len, decoded.data(),
                                             decoded.size(), &decoded_len));
        ASSERT_EQ(decoded_len, packet.size());
        EXPECT_EQ(decoded, packet);
    }

    EXPECT_LT(encoded_len, static_cast<int>(packet.size()));
    ppp_ccp_codec_close(&tx);
    ppp_ccp_codec_close(&rx);
}

TEST(ModemCcp, Nt31RasMatchesSourceLiteralAndCrcVector)
{
    ppp_ctx_t tx{};
    const std::array<uint8_t, 1> packet = { 0 };
    const std::array<uint8_t, 4> expected = { 0xA0, 0x10, 0x08, 0x00 };
    std::array<uint8_t, 16> encoded{};
    int encoded_len;

    ASSERT_TRUE(ppp_ccp_codec_set(&tx, true, PPP_CCP_METHOD_NT31RAS));
    ASSERT_TRUE(ppp_ccp_codec_compress(&tx, packet.data(), packet.size(), encoded.data(),
                                       encoded.size(), &encoded_len));
    ASSERT_EQ(encoded_len, expected.size());
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(), encoded.begin()));
    ppp_ccp_codec_close(&tx);
}

TEST(ModemCcp, AcceptsNt31RasCapabilityRequest)
{
    ppp_ctx_t ctx = make_ccp_context();
    const uint8_t request[] = {
        PPP_CODE_CONFIGURE_REQUEST, 5, 0, 26,
        PPP_CCP_METHOD_NT31RAS, 22,
        1, 0, 0, 0, 1, 0, 0, 0,
        220, 5, 0, 0, 220, 5, 0, 0, 0, 0, 0, 0
    };

    ppp_ccp_process(&ctx, request, sizeof(request));

    ASSERT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_ACK);
    EXPECT_EQ(sent_packet.size(), sizeof(request));
    EXPECT_TRUE(ctx.ccp_ack_sent);
    EXPECT_EQ(ctx.ccp_tx_method, PPP_CCP_METHOD_NT31RAS);
    EXPECT_NE(ctx.ccp_tx_codec_state, nullptr);
}

TEST(ModemCcp, Nt31RasExtendedWindowsRoundTripLongDistanceCopies)
{
    for (uint32_t window_size : { 16384u, 32768u, 65536u }) {
        ppp_ctx_t tx{};
        ppp_ctx_t rx{};
        std::array<uint8_t, 1300> packet{};
        std::array<uint8_t, 64> marker{};
        std::array<uint8_t, PPP_MAX_FRAME * 2> encoded{};
        std::array<uint8_t, PPP_MAX_FRAME> decoded{};
        uint32_t random = 0x8A31F07Du;
        uint32_t history_bytes = 0;
        int last_encoded_length = 0;

        auto random_byte = [&random]() {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            return static_cast<uint8_t>(random >> 24);
        };
        auto transfer = [&](const uint8_t *input, size_t input_length) {
            int decoded_length;
            if (!ppp_ccp_codec_compress(&tx, input, static_cast<int>(input_length),
                                        encoded.data(), encoded.size(),
                                        &last_encoded_length)) {
                ADD_FAILURE() << "NT31-RAS compression failed at window=" << window_size
                              << " history=" << history_bytes;
                return false;
            }
            if (last_encoded_length > PPP_MAX_FRAME) {
                ADD_FAILURE() << "NT31-RAS output exceeds PPP_MAX_FRAME=" << PPP_MAX_FRAME
                              << " encoded=" << last_encoded_length;
                return false;
            }
            if (!ppp_ccp_codec_decompress(&rx, encoded.data(), last_encoded_length,
                                          decoded.data(), decoded.size(),
                                          &decoded_length)) {
                ADD_FAILURE()
                    << "NT31-RAS decompression failed at window=" << window_size
                    << " history=" << history_bytes
                    << " input-length=" << input_length;
                return false;
            }
            bool matches = decoded_length == static_cast<int>(input_length)
                        && std::equal(input, input + input_length, decoded.begin());
            if (!matches)
                ADD_FAILURE() << "NT31-RAS output mismatch at window=" << window_size
                              << " history=" << history_bytes;
            return matches;
        };

        ASSERT_TRUE(ppp_ccp_codec_set_window(&tx, true, PPP_CCP_METHOD_NT31RAS,
                                             window_size));
        ASSERT_TRUE(ppp_ccp_codec_set_window(&rx, false, PPP_CCP_METHOD_NT31RAS,
                                             window_size));
        for (uint8_t &value : packet)
            value = random_byte();
        for (uint8_t &value : marker)
            value = random_byte();
        std::copy(marker.begin(), marker.end(), packet.begin());
        ASSERT_TRUE(transfer(packet.data(), packet.size()));
        history_bytes += packet.size() + 2;

        uint32_t target_distance = window_size * 3 / 4;
        while (history_bytes < target_distance) {
            size_t remaining = target_distance - history_bytes;
            size_t packet_length = std::min(packet.size(), remaining - 2);
            for (size_t index = 0; index < packet_length; index++)
                packet[index] = random_byte();
            ASSERT_TRUE(transfer(packet.data(), packet_length));
            history_bytes += static_cast<uint32_t>(packet_length + 2);
        }

        ASSERT_TRUE(transfer(marker.data(), marker.size()));
        ASSERT_LT(last_encoded_length, static_cast<int>(marker.size()));
        ppp_ccp_codec_close(&tx);
        ppp_ccp_codec_close(&rx);
    }
}

TEST(ModemCcp, AcceptsAllNt31RasWindowFeatureBits)
{
    ppp_ctx_t ctx = make_ccp_context();
    const uint8_t request[] = {
        PPP_CODE_CONFIGURE_REQUEST, 6, 0, 26,
        PPP_CCP_METHOD_NT31RAS, 22,
        0x0F, 0, 0, 0, 0x08, 0, 0, 0,
        220, 5, 0, 0, 220, 5, 0, 0, 0, 0, 0, 0
    };

    ppp_ccp_process(&ctx, request, sizeof(request));

    ASSERT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_ACK);
    EXPECT_EQ(ctx.ccp_tx_method, PPP_CCP_METHOD_NT31RAS);
    EXPECT_EQ(ctx.ccp_tx_window_size, 65536u);
}

TEST(ModemCcp, RetriesNt31RasWithEightKilobyteNak)
{
    ppp_ctx_t ctx = make_ccp_context();
    const uint8_t request[] = {
        PPP_CODE_CONFIGURE_REQUEST, 1, 0, 26, PPP_CCP_METHOD_NT31RAS, 22,
        0x0F, 0, 0, 0, 0x0F, 0, 0, 0,
        0xDC, 5, 0, 0, 0xDC, 5, 0, 0, 0, 0, 0, 0
    };
    std::memcpy(ctx.ccp_request, request, sizeof(request));
    ctx.ccp_request_len = sizeof(request);
    ctx.ccp_request_id = request[1];
    ctx.ccp_request_method = PPP_CCP_METHOD_NT31RAS;
    ctx.ccp_req_sent = true;

    std::vector<uint8_t> nak(ctx.ccp_request,
                             ctx.ccp_request + ctx.ccp_request_len);
    nak[0] = PPP_CODE_CONFIGURE_NAK;
    nak[6] = 1;
    nak[7] = 0;
    nak[8] = 0;
    nak[9] = 0;
    nak[10] = 1;
    nak[11] = 0;
    nak[12] = 0;
    nak[13] = 0;
    ppp_ccp_process(&ctx, nak.data(), static_cast<int>(nak.size()));

    ASSERT_EQ(ctx.ccp_request_method, PPP_CCP_METHOD_NT31RAS);
    ASSERT_EQ(ctx.ccp_request[0], PPP_CODE_CONFIGURE_REQUEST);
    EXPECT_EQ(ctx.ccp_request[6], 1);
    EXPECT_EQ(ctx.ccp_request[10], 1);
    std::vector<uint8_t> ack(ctx.ccp_request,
                             ctx.ccp_request + ctx.ccp_request_len);
    ack[0] = PPP_CODE_CONFIGURE_ACK;
    ppp_ccp_process(&ctx, ack.data(), static_cast<int>(ack.size()));
    EXPECT_EQ(ctx.ccp_rx_window_size, 8192u);
    EXPECT_EQ(ctx.ccp_rx_method, PPP_CCP_METHOD_NT31RAS);
    ppp_ccp_codec_close(&ctx);
}

TEST(ModemCcp, NegotiatesV44DatagramMode)
{
    ppp_ctx_t ctx = make_ccp_context();
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 7, 0, 8, 27, 4, 0, 0 };

    ppp_ccp_process(&ctx, request, sizeof(request));

    ASSERT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_ACK);
    EXPECT_EQ(ctx.ccp_tx_method, PPP_CCP_METHOD_V44);
    EXPECT_NE(ctx.ccp_tx_codec_state, nullptr);
}

TEST(ModemCcp, NaksUnsupportedV44ModeToDatagramMode)
{
    ppp_ctx_t ctx = make_ccp_context();
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 7, 0, 8, 27, 4, 1, 0 };

    ppp_ccp_process(&ctx, request, sizeof(request));

    ASSERT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_NAK);
    ASSERT_EQ(sent_packet.size(), sizeof(request));
    EXPECT_EQ(sent_packet[4], 27);
    EXPECT_EQ(sent_packet[5], 4);
    EXPECT_EQ(sent_packet[6], 0);
    EXPECT_EQ(sent_packet[7], 0);
}

static ppp_ctx_t
mppe_peer_context(uint8_t key_state_mode)
{
    ppp_ctx_t ctx = make_ccp_context();
    ctx.mppe_keys_ready     = true;
    ctx.mppe_allowed_bits   = CCP_MPPE_KEY_BITS;
    ctx.mppe_key_state_mode = key_state_mode;
    return ctx;
}

TEST(ModemCcp, AutoStatelessModeFallsBackAfterOneNak)
{
    ppp_ctx_t ctx = mppe_peer_context(PPP_MPPE_STATE_AUTO_STATELESS_PREFERRED);
    const uint8_t stateful[] = { PPP_CODE_CONFIGURE_REQUEST, 4, 0, 10,
                                 18, 6, 0, 0, 0, 0x40 };

    ppp_ccp_process(&ctx, stateful, sizeof(stateful));
    ASSERT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_NAK);
    EXPECT_EQ(sent_packet[6], 1);

    ppp_ccp_process(&ctx, stateful, sizeof(stateful));
    EXPECT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_ACK);
}

TEST(ModemCcp, AutoStatefulModeFallsBackAfterOneNak)
{
    ppp_ctx_t ctx = mppe_peer_context(PPP_MPPE_STATE_AUTO_STATEFUL_PREFERRED);
    const uint8_t stateless[] = { PPP_CODE_CONFIGURE_REQUEST, 4, 0, 10,
                                  18, 6, 1, 0, 0, 0x40 };

    ppp_ccp_process(&ctx, stateless, sizeof(stateless));
    ASSERT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_NAK);
    EXPECT_EQ(sent_packet[6], 0);

    ppp_ccp_process(&ctx, stateless, sizeof(stateless));
    EXPECT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_ACK);
}

TEST(ModemCcp, RequireStatelessKeepsRefusingStateful)
{
    ppp_ctx_t ctx = mppe_peer_context(PPP_MPPE_STATE_REQUIRE_STATELESS);
    const uint8_t stateful[] = { PPP_CODE_CONFIGURE_REQUEST, 4, 0, 10,
                                 18, 6, 0, 0, 0, 0x40 };

    ppp_ccp_process(&ctx, stateful, sizeof(stateful));
    EXPECT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_NAK);
    ppp_ccp_process(&ctx, stateful, sizeof(stateful));
    EXPECT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_NAK);
}

TEST(ModemCcp, OfferStatefulAcceptsNakAskingForStateless)
{
    ppp_ctx_t ctx = mppe_peer_context(PPP_MPPE_STATE_OFFER_STATEFUL);
    ppp_ccp_start(&ctx);
    ASSERT_EQ(ctx.ccp_request_bits & CCP_MPPE_STATELESS, 0u);
    const uint8_t nak[] = { PPP_CODE_CONFIGURE_NAK, ctx.ccp_request_id, 0, 10,
                            18, 6, 1, 0, 0, 0x40 };

    ppp_ccp_process(&ctx, nak, sizeof(nak));

    EXPECT_EQ(ctx.ccp_request_bits, CCP_MPPE_STATELESS | CCP_MPPE_128);
}

TEST(ModemCcp, RequireStatefulStillRefusesStatelessNak)
{
    ppp_ctx_t ctx = mppe_peer_context(PPP_MPPE_STATE_REQUIRE_STATEFUL);
    ppp_ccp_start(&ctx);
    const uint32_t offered = ctx.ccp_request_bits;
    const uint8_t nak[] = { PPP_CODE_CONFIGURE_NAK, ctx.ccp_request_id, 0, 10,
                            18, 6, 1, 0, 0, 0x40 };

    ppp_ccp_process(&ctx, nak, sizeof(nak));

    EXPECT_EQ(ctx.ccp_request_bits & CCP_MPPE_STATELESS, offered & CCP_MPPE_STATELESS);
}

TEST(ModemCcp, AutomaticFormatToleratesRepeatedDBit)
{
    ppp_ctx_t ctx = mppe_peer_context(PPP_MPPE_STATE_OFFER_STATELESS);
    ctx.mppe_format = PPP_MPPE_FORMAT_AUTOMATIC;
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 4, 0, 10,
                                18, 6, 1, 0, 0, 0x50 };

    ppp_ccp_process(&ctx, request, sizeof(request));
    ASSERT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_NAK);
    EXPECT_EQ(sent_packet[9] & CCP_MPPE_D, 0u);

    ppp_ccp_process(&ctx, request, sizeof(request));
    EXPECT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_ACK);
}

TEST(ModemCcp, StandardFormatKeepsRefusingDBit)
{
    ppp_ctx_t ctx = mppe_peer_context(PPP_MPPE_STATE_OFFER_STATELESS);
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 4, 0, 10,
                                18, 6, 1, 0, 0, 0x50 };

    ppp_ccp_process(&ctx, request, sizeof(request));
    ppp_ccp_process(&ctx, request, sizeof(request));

    EXPECT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_NAK);
}

TEST(ModemCcp, AutomaticFormatRetriesWithout56BitAfterReject)
{
    ppp_ctx_t ctx = mppe_peer_context(PPP_MPPE_STATE_OFFER_STATELESS);
    ctx.mppe_format = PPP_MPPE_FORMAT_AUTOMATIC;
    ppp_ccp_start(&ctx);
    ASSERT_NE(ctx.ccp_request_bits & CCP_MPPE_56, 0u);
    std::vector<uint8_t> reject = sent_packet;
    reject[0] = PPP_CODE_CONFIGURE_REJECT;

    ppp_ccp_process(&ctx, reject.data(), static_cast<int>(reject.size()));

    EXPECT_EQ(ctx.ccp_request_method, PPP_CCP_METHOD_MPPE);
    EXPECT_EQ(ctx.ccp_request_bits & CCP_MPPE_56, 0u);
    EXPECT_NE(ctx.ccp_request_bits & CCP_MPPE_128, 0u);
    EXPECT_FALSE(ctx.ccp_rejected);
}

TEST(ModemCcp, RequiredStrongestPicksBestOfferedKey)
{
    ppp_ctx_t ctx = mppe_peer_context(PPP_MPPE_STATE_OFFER_STATELESS);
    ctx.mppe_min_bits         = 40;
    ctx.mppe_prefer_strongest = true;
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 4, 0, 10,
                                18, 6, 1, 0, 0, 0x40 };

    ppp_ccp_process(&ctx, request, sizeof(request));

    EXPECT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_ACK);
}
