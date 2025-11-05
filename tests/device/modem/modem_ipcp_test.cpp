#include <cstdint>
#include <cstring>
#include <vector>
#include <gtest/gtest.h>
#include <86box/modem/modem_ppp.h>
#include <86box/modem/modem_ipcp.h>

static std::vector<uint8_t> sent_packet;
static int state_advances;

extern "C" {
void
ppp_send_frame(ppp_ctx_t *, uint16_t, const uint8_t *data, int len)
{
    sent_packet.assign(data, data + len);
}

void
ppp_advance_state(ppp_ctx_t *)
{
    state_advances++;
}

void
ppp_send_code_reject(ppp_ctx_t *, uint16_t, const uint8_t *, int)
{
}
}

static std::vector<uint8_t>
make_response(const ppp_ctx_t &ctx, uint8_t code, uint8_t id)
{
    return { code, id, 0, 10, IPCP_OPT_IP_ADDRESS, 6,
             static_cast<uint8_t>(ctx.ipcp_request_ip >> 24),
             static_cast<uint8_t>(ctx.ipcp_request_ip >> 16),
             static_cast<uint8_t>(ctx.ipcp_request_ip >> 8),
             static_cast<uint8_t>(ctx.ipcp_request_ip) };
}

TEST(ModemIpcp, IgnoresNullContextOrPacket)
{
    ppp_ctx_t ctx{};
    const uint8_t packet[] = { PPP_CODE_CONFIGURE_REQUEST, 1, 0, 4 };

    ppp_ipcp_process(nullptr, packet, sizeof(packet));
    ppp_ipcp_process(&ctx, nullptr, sizeof(packet));
    EXPECT_EQ(ctx.state, PPP_STATE_DEAD);
}

TEST(ModemIpcp, IgnoresStaleConfigureAck)
{
    ppp_ctx_t ctx{};
    ctx.our_ip = 0x0A000202;
    ppp_ipcp_send_config_request(&ctx);
    const std::vector<uint8_t> ack = make_response(ctx, PPP_CODE_CONFIGURE_ACK,
                                                   static_cast<uint8_t>(ctx.ipcp_request_id + 1));

    ppp_ipcp_process(&ctx, ack.data(), static_cast<int>(ack.size()));

    EXPECT_FALSE(ctx.ipcp_ack_received);
    EXPECT_EQ(state_advances, 0);
}

TEST(ModemIpcp, AcceptsMatchingConfigureAck)
{
    ppp_ctx_t ctx{};
    ctx.our_ip = 0x0A000202;
    ppp_ipcp_send_config_request(&ctx);
    const std::vector<uint8_t> ack = make_response(ctx, PPP_CODE_CONFIGURE_ACK,
                                                   ctx.ipcp_request_id);

    ppp_ipcp_process(&ctx, ack.data(), static_cast<int>(ack.size()));

    EXPECT_TRUE(ctx.ipcp_ack_received);
    EXPECT_EQ(state_advances, 1);
}

TEST(ModemIpcp, RejectsConfigureAckWithReorderedOptions)
{
    ppp_ctx_t ctx{};
    ctx.our_ip = 0x0A000202;
    ctx.ipcp_vj_request = true;
    ctx.vj_rx_max_slot_id = IPCP_VJ_MAX_SLOT_ID;
    ctx.vj_rx_comp_slot_id = true;
    state_advances = 0;
    ppp_ipcp_send_config_request(&ctx);
    const std::vector<uint8_t> ack = {
        PPP_CODE_CONFIGURE_ACK, ctx.ipcp_request_id, 0, 16,
        IPCP_OPT_IP_COMPRESSION, 6, 0, 0x2D, IPCP_VJ_MAX_SLOT_ID, 1,
        IPCP_OPT_IP_ADDRESS, 6, 10, 0, 2, 2
    };

    ppp_ipcp_process(&ctx, ack.data(), static_cast<int>(ack.size()));

    EXPECT_FALSE(ctx.ipcp_ack_received);
    EXPECT_TRUE(ctx.ipcp_req_sent);
    EXPECT_EQ(state_advances, 0);
}

TEST(ModemIpcp, ConfigureRejectDoesNotCompleteNetworkLayer)
{
    ppp_ctx_t ctx{};
    ctx.our_ip = 0x0A000202;
    ppp_ipcp_send_config_request(&ctx);
    const std::vector<uint8_t> reject = make_response(ctx, PPP_CODE_CONFIGURE_REJECT,
                                                      ctx.ipcp_request_id);

    ppp_ipcp_process(&ctx, reject.data(), static_cast<int>(reject.size()));

    EXPECT_FALSE(ctx.ipcp_ack_received);
    EXPECT_EQ(ctx.state, PPP_STATE_DEAD);
}

TEST(ModemIpcp, RejectedPeerRequestClearsPreviousAck)
{
    ppp_ctx_t ctx{};
    ctx.state = PPP_STATE_IPCP_NEGOTIATE;
    ctx.ipcp_ack_sent = true;
    ctx.ipcp_ack_received = true;
    state_advances = 0;
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 3, 0, 6, 99, 2 };

    ppp_ipcp_process(&ctx, request, sizeof(request));

    EXPECT_FALSE(ctx.ipcp_ack_sent);
    EXPECT_EQ(ctx.state, PPP_STATE_IPCP_NEGOTIATE);
    EXPECT_EQ(state_advances, 1);
}

TEST(ModemIpcp, IgnoresConfigureNakAfterAck)
{
    ppp_ctx_t ctx{};
    ctx.our_ip = 0x0A000202;
    ppp_ipcp_send_config_request(&ctx);
    const std::vector<uint8_t> ack = make_response(ctx, PPP_CODE_CONFIGURE_ACK,
                                                   ctx.ipcp_request_id);
    ppp_ipcp_process(&ctx, ack.data(), static_cast<int>(ack.size()));
    ASSERT_TRUE(ctx.ipcp_ack_received);
    ASSERT_FALSE(ctx.ipcp_req_sent);

    std::vector<uint8_t> nak = make_response(ctx, PPP_CODE_CONFIGURE_NAK,
                                             ctx.ipcp_request_id);
    nak[9] = 3;
    ppp_ipcp_process(&ctx, nak.data(), static_cast<int>(nak.size()));

    EXPECT_EQ(ctx.our_ip, 0x0A000202u);
    EXPECT_TRUE(ctx.ipcp_ack_received);
    EXPECT_EQ(ctx.ipcp_retries, 0);
}

TEST(ModemIpcp, AppliesMatchingConfigureNakAddress)
{
    ppp_ctx_t ctx{};
    ctx.our_ip = 0x0A000202;
    ppp_ipcp_send_config_request(&ctx);
    std::vector<uint8_t> nak = make_response(ctx, PPP_CODE_CONFIGURE_NAK,
                                             ctx.ipcp_request_id);
    nak[6] = 10;
    nak[7] = 0;
    nak[8] = 2;
    nak[9] = 3;

    ppp_ipcp_process(&ctx, nak.data(), static_cast<int>(nak.size()));

    EXPECT_EQ(ctx.our_ip, 0x0A000203u);
    EXPECT_EQ(ctx.ipcp_request_ip, 0x0A000203u);
    EXPECT_TRUE(ctx.ipcp_req_sent);
    EXPECT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_REQUEST);
}

TEST(ModemIpcp, SuggestsConfiguredWinsAddresses)
{
    ppp_ctx_t ctx{};
    ctx.ipcp_dns_wins_mode = PPP_IPCP_DNS_WINS_OFFER;
    ctx.state = PPP_STATE_IPCP_NEGOTIATE;
    ctx.wins1 = 0x0A00020A;
    ctx.wins2 = 0x0A00020B;
    sent_packet.clear();
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 4, 0, 16,
                                IPCP_OPT_NBNS_PRIMARY, 6, 0, 0, 0, 0,
                                IPCP_OPT_NBNS_SECONDARY, 6, 0, 0, 0, 0 };

    ppp_ipcp_process(&ctx, request, sizeof(request));

    ASSERT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_NAK);
    ASSERT_EQ(sent_packet.size(), 16u);
    EXPECT_EQ(sent_packet[4], IPCP_OPT_NBNS_PRIMARY);
    EXPECT_EQ(sent_packet[6], 10);
    EXPECT_EQ(sent_packet[9], 10);
    EXPECT_EQ(sent_packet[10], IPCP_OPT_NBNS_SECONDARY);
    EXPECT_EQ(sent_packet[12], 10);
    EXPECT_EQ(sent_packet[15], 11);
}

TEST(ModemIpcp, AcknowledgesMatchingConfiguredWinsAddresses)
{
    ppp_ctx_t ctx{};
    ctx.ipcp_dns_wins_mode = PPP_IPCP_DNS_WINS_OFFER;
    ctx.state = PPP_STATE_IPCP_NEGOTIATE;
    ctx.wins1 = 0x0A00020A;
    ctx.wins2 = 0x0A00020B;
    sent_packet.clear();
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 4, 0, 16,
                                IPCP_OPT_NBNS_PRIMARY, 6, 10, 0, 2, 10,
                                IPCP_OPT_NBNS_SECONDARY, 6, 10, 0, 2, 11 };

    ppp_ipcp_process(&ctx, request, sizeof(request));

    ASSERT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_ACK);
    EXPECT_EQ(sent_packet.size(), sizeof(request));
    EXPECT_EQ(std::memcmp(sent_packet.data() + 4, request + 4, sizeof(request) - 4), 0);
    EXPECT_TRUE(ctx.ipcp_ack_sent);
}

TEST(ModemIpcp, RejectsWinsOptionWhenServerIsUnconfigured)
{
    ppp_ctx_t ctx{};
    ctx.state = PPP_STATE_IPCP_NEGOTIATE;
    sent_packet.clear();
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 4, 0, 10,
                                IPCP_OPT_NBNS_PRIMARY, 6, 0, 0, 0, 0 };

    ppp_ipcp_process(&ctx, request, sizeof(request));

    ASSERT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_REJECT);
    EXPECT_EQ(sent_packet.size(), sizeof(request));
    EXPECT_EQ(std::memcmp(sent_packet.data() + 4, request + 4, sizeof(request) - 4), 0);
}

TEST(ModemIpcp, NaksUnconfiguredWinsAddressToZero)
{
    ppp_ctx_t ctx{};
    ctx.ipcp_dns_wins_mode = PPP_IPCP_DNS_WINS_OFFER;
    ctx.state = PPP_STATE_IPCP_NEGOTIATE;
    sent_packet.clear();
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 4, 0, 10,
                                IPCP_OPT_NBNS_PRIMARY, 6, 10, 0, 2, 10 };

    ppp_ipcp_process(&ctx, request, sizeof(request));

    ASSERT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_NAK);
    ASSERT_EQ(sent_packet.size(), sizeof(request));
    EXPECT_EQ(sent_packet[4], IPCP_OPT_NBNS_PRIMARY);
    EXPECT_EQ(sent_packet[6], 0);
    EXPECT_EQ(sent_packet[7], 0);
    EXPECT_EQ(sent_packet[8], 0);
    EXPECT_EQ(sent_packet[9], 0);
}

TEST(ModemIpcp, RejectsUnconfiguredDnsRequests)
{
    ppp_ctx_t ctx{};
    ctx.state = PPP_STATE_IPCP_NEGOTIATE;
    sent_packet.clear();
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 4, 0, 16,
                                IPCP_OPT_DNS_PRIMARY, 6, 0, 0, 0, 0,
                                IPCP_OPT_DNS_SECONDARY, 6, 0, 0, 0, 0 };

    ppp_ipcp_process(&ctx, request, sizeof(request));

    ASSERT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_REJECT);
    EXPECT_EQ(sent_packet.size(), sizeof(request));
    EXPECT_EQ(std::memcmp(sent_packet.data() + 4, request + 4, sizeof(request) - 4), 0);
}

TEST(ModemIpcp, NaksDnsRequestsToConfiguredServers)
{
    ppp_ctx_t ctx{};
    ctx.ipcp_dns_wins_mode = PPP_IPCP_DNS_WINS_OFFER;
    ctx.state = PPP_STATE_IPCP_NEGOTIATE;
    ctx.dns1 = 0x01010101;
    ctx.dns2 = 0x08080404;
    sent_packet.clear();
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 4, 0, 16,
                                IPCP_OPT_DNS_PRIMARY, 6, 0, 0, 0, 0,
                                IPCP_OPT_DNS_SECONDARY, 6, 0, 0, 0, 0 };

    ppp_ipcp_process(&ctx, request, sizeof(request));

    ASSERT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_NAK);
    ASSERT_EQ(sent_packet.size(), sizeof(request));
    EXPECT_EQ(sent_packet[4], IPCP_OPT_DNS_PRIMARY);
    EXPECT_EQ(sent_packet[6], 1);
    EXPECT_EQ(sent_packet[7], 1);
    EXPECT_EQ(sent_packet[8], 1);
    EXPECT_EQ(sent_packet[9], 1);
    EXPECT_EQ(sent_packet[10], IPCP_OPT_DNS_SECONDARY);
    EXPECT_EQ(sent_packet[12], 8);
    EXPECT_EQ(sent_packet[13], 8);
    EXPECT_EQ(sent_packet[14], 4);
    EXPECT_EQ(sent_packet[15], 4);
}

TEST(ModemIpcp, RequestsVanJacobsonCompression)
{
    ppp_ctx_t ctx{};
    ctx.our_ip = 0x0A000202;
    ctx.ipcp_vj_request = true;
    ctx.vj_rx_max_slot_id = IPCP_VJ_MAX_SLOT_ID;
    ctx.vj_rx_comp_slot_id = true;
    sent_packet.clear();

    ppp_ipcp_send_config_request(&ctx);

    ASSERT_EQ(sent_packet.size(), 16u);
    EXPECT_EQ(sent_packet[10], IPCP_OPT_IP_COMPRESSION);
    EXPECT_EQ(sent_packet[11], 6);
    EXPECT_EQ(sent_packet[12], 0);
    EXPECT_EQ(sent_packet[13], 0x2D);
    EXPECT_EQ(sent_packet[14], IPCP_VJ_MAX_SLOT_ID);
    EXPECT_EQ(sent_packet[15], 1);
}

TEST(ModemIpcp, EnablesReceiveCompressionAfterMatchingAck)
{
    ppp_ctx_t ctx{};
    ctx.our_ip = 0x0A000202;
    ctx.ipcp_vj_request = true;
    ctx.vj_rx_max_slot_id = IPCP_VJ_MAX_SLOT_ID;
    ctx.vj_rx_comp_slot_id = true;
    ppp_ipcp_send_config_request(&ctx);
    std::vector<uint8_t> ack = sent_packet;
    ack[0] = PPP_CODE_CONFIGURE_ACK;

    ppp_ipcp_process(&ctx, ack.data(), static_cast<int>(ack.size()));

    EXPECT_TRUE(ctx.vj_rx_enabled);
}

TEST(ModemIpcp, EnablesTransmitCompressionAfterPeerRequest)
{
    ppp_ctx_t ctx{};
    ctx.state = PPP_STATE_IPCP_NEGOTIATE;
    sent_packet.clear();
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 8, 0, 10,
                                IPCP_OPT_IP_COMPRESSION, 6, 0, 0x2D,
                                IPCP_VJ_MAX_SLOT_ID, 1 };

    ppp_ipcp_process(&ctx, request, sizeof(request));

    ASSERT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_ACK);
    EXPECT_TRUE(ctx.vj_tx_enabled);
    EXPECT_EQ(ctx.vj_tx_max_slot_id, IPCP_VJ_MAX_SLOT_ID);
    EXPECT_TRUE(ctx.vj_tx_comp_slot_id);
}

TEST(ModemIpcp, DisablesRejectedOptionalVjCompression)
{
    ppp_ctx_t ctx{};
    ctx.our_ip = 0x0A000202;
    ctx.ipcp_vj_request = true;
    ctx.vj_rx_max_slot_id = IPCP_VJ_MAX_SLOT_ID;
    ctx.vj_rx_comp_slot_id = true;
    ppp_ipcp_send_config_request(&ctx);
    const uint8_t reject[] = { PPP_CODE_CONFIGURE_REJECT, ctx.ipcp_request_id,
                               0, 10, IPCP_OPT_IP_COMPRESSION, 6,
                               0, 0x2D, IPCP_VJ_MAX_SLOT_ID, 1 };

    ppp_ipcp_process(&ctx, reject, sizeof(reject));

    EXPECT_EQ(ctx.state, PPP_STATE_DEAD);
    EXPECT_FALSE(ctx.ipcp_vj_request);
    ASSERT_EQ(sent_packet.size(), 10u);
    EXPECT_EQ(sent_packet[0], PPP_CODE_CONFIGURE_REQUEST);
}