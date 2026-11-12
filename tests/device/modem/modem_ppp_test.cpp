#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <gtest/gtest.h>
#include <vector>
#include <86box/modem/modem_cslip.h>
#include <86box/modem/modem_ppp.h>
#include <86box/modem/modem_mppe.h>
#include <86box/modem/modem_pap.h>
extern "C" {
#include <86box/modem/modem_ipv6cp.h>
#include <86box/modem/modem_nbfcp.h>
#include <86box/network/net_ipx.h>
#include <86box/modem/modem_ipxcp.h>
}

static void
discard_serial(void *, const uint8_t *, int)
{
}

static void
capture_serial(void *priv, const uint8_t *data, int len)
{
    auto *output = static_cast<std::vector<uint8_t> *>(priv);
    output->insert(output->end(), data, data + len);
}

static void
send_frame(ppp_ctx_t *ctx, const std::vector<uint8_t> &packet, bool corrupt_fcs = false,
           uint16_t protocol = PPP_PROTO_LCP)
{
    std::vector<uint8_t> raw = { PPP_ADDRESS, PPP_CONTROL,
                                 static_cast<uint8_t>(protocol >> 8),
                                 static_cast<uint8_t>(protocol) };
    raw.insert(raw.end(), packet.begin(), packet.end());
    uint16_t fcs = ppp_fcs16(raw.data(), static_cast<int>(raw.size()));
    raw.push_back(static_cast<uint8_t>(fcs));
    raw.push_back(static_cast<uint8_t>(fcs >> 8));
    if (corrupt_fcs)
        raw.back() ^= 1;

    ppp_rx_byte(ctx, PPP_FLAG);
    for (uint8_t byte : raw) {
        if (byte == PPP_FLAG || byte == PPP_ESCAPE) {
            ppp_rx_byte(ctx, PPP_ESCAPE);
            ppp_rx_byte(ctx, byte ^ 0x20);
        } else {
            ppp_rx_byte(ctx, byte);
        }
    }
    ppp_rx_byte(ctx, PPP_FLAG);
}

static void
capture_network(void *priv, const uint8_t *data, int len)
{
    auto *output = static_cast<std::vector<uint8_t> *>(priv);
    output->assign(data, data + len);
}

static void
capture_network_packets(void *priv, const uint8_t *data, int len)
{
    auto *output = static_cast<std::vector<std::vector<uint8_t>> *>(priv);
    output->emplace_back(data, data + len);
}

struct ras_capture_t {
    std::vector<uint8_t> serial;
    std::vector<std::vector<uint8_t>> packets;
};

static void
capture_ras_serial(void *priv, const uint8_t *data, int len)
{
    auto *capture = static_cast<ras_capture_t *>(priv);
    capture->serial.insert(capture->serial.end(), data, data + len);
}

static void
capture_ras_network(void *priv, const uint8_t *data, int len)
{
    auto *capture = static_cast<ras_capture_t *>(priv);
    capture->packets.emplace_back(data, data + len);
}

static std::vector<uint8_t>
make_tcp_packet(uint16_t ip_id, uint32_t sequence)
{
    std::vector<uint8_t> packet(40, 0);
    packet[0] = 0x45;
    packet[3] = static_cast<uint8_t>(packet.size());
    packet[4] = static_cast<uint8_t>(ip_id >> 8);
    packet[5] = static_cast<uint8_t>(ip_id);
    packet[8] = 64;
    packet[9] = 6;
    packet[12] = 10;
    packet[15] = 1;
    packet[16] = 10;
    packet[19] = 2;
    packet[20] = 0x04;
    packet[21] = 0xD2;
    packet[23] = 80;
    packet[24] = static_cast<uint8_t>(sequence >> 24);
    packet[25] = static_cast<uint8_t>(sequence >> 16);
    packet[26] = static_cast<uint8_t>(sequence >> 8);
    packet[27] = static_cast<uint8_t>(sequence);
    packet[32] = 0x50;
    packet[33] = 0x10;
    packet[34] = 0x10;
    uint32_t sum = 0;
    for (size_t pos = 0; pos < 20; pos += 2)
        sum += (static_cast<uint16_t>(packet[pos]) << 8) | packet[pos + 1];
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    uint16_t checksum = static_cast<uint16_t>(~sum);
    packet[10] = static_cast<uint8_t>(checksum >> 8);
    packet[11] = static_cast<uint8_t>(checksum);
    return packet;
}

static std::vector<uint8_t>
decode_captured_frame(const std::vector<uint8_t> &wire, uint16_t &protocol)
{
    std::vector<uint8_t> raw;
    for (size_t pos = 1; pos + 1 < wire.size(); pos++) {
        uint8_t byte = wire[pos];
        if (byte == PPP_ESCAPE)
            byte = wire[++pos] ^ 0x20;
        raw.push_back(byte);
    }
    if (raw.size() < 6)
        return {};
    protocol = static_cast<uint16_t>((raw[2] << 8) | raw[3]);
    return { raw.begin() + 4, raw.end() - 2 };
}

static ppp_ctx_t make_context();

TEST(ModemPpp, RequiresLowestSelectedMppeStrength)
{
    EXPECT_EQ(ppp_mppe_minimum_strength(0), 0);
    EXPECT_EQ(ppp_mppe_minimum_strength(CCP_MPPE_40), 40);
    EXPECT_EQ(ppp_mppe_minimum_strength(CCP_MPPE_56), 56);
    EXPECT_EQ(ppp_mppe_minimum_strength(CCP_MPPE_128), 128);
    EXPECT_EQ(ppp_mppe_minimum_strength(CCP_MPPE_40 | CCP_MPPE_128), 40);
    EXPECT_EQ(ppp_mppe_minimum_strength(CCP_MPPE_56 | CCP_MPPE_128), 56);
    EXPECT_EQ(ppp_mppe_minimum_strength(CCP_MPPE_KEY_BITS), 40);
}

TEST(ModemPpp, FcsMatchesStandardCheckValue)
{
    const uint8_t check[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    EXPECT_EQ(ppp_fcs16(check, sizeof(check)), 0x906Eu);
}

TEST(ModemPpp, MultilinkLcpOptionsAreOptIn)
{
    std::vector<uint8_t> unconfigured_wire;
    std::vector<uint8_t> configured_wire;
    std::vector<uint8_t> network;
    ppp_ctx_t *unconfigured = ppp_init(&unconfigured_wire, nullptr,
                                       capture_serial, capture_network);
    ppp_ctx_t *configured = ppp_init(&configured_wire, nullptr,
                                     capture_serial, capture_network);
    ASSERT_NE(unconfigured, nullptr);
    ASSERT_NE(configured, nullptr);

    ppp_start(unconfigured);
    ppp_multilink_configure(configured, "modem-pair");
    ppp_start(configured);

    uint16_t protocol = 0;
    auto ordinary_request = decode_captured_frame(unconfigured_wire, protocol);
    ASSERT_EQ(protocol, PPP_PROTO_LCP);
    bool ordinary_has_mrru = false;
    for (size_t pos = 4; pos + 2 <= ordinary_request.size();) {
        uint8_t option_length = ordinary_request[pos + 1];
        ASSERT_GE(option_length, 2);
        ASSERT_LE(pos + option_length, ordinary_request.size());
        ordinary_has_mrru |= ordinary_request[pos] == LCP_OPT_MRRU;
        pos += option_length;
    }
    EXPECT_FALSE(ordinary_has_mrru);

    auto multilink_request = decode_captured_frame(configured_wire, protocol);
    ASSERT_EQ(protocol, PPP_PROTO_LCP);
    bool has_mrru = false;
    bool has_short_sequence = false;
    bool has_endpoint = false;
    for (size_t pos = 4; pos + 2 <= multilink_request.size();) {
        uint8_t option_length = multilink_request[pos + 1];
        ASSERT_GE(option_length, 2);
        ASSERT_LE(pos + option_length, multilink_request.size());
        has_mrru |= multilink_request[pos] == LCP_OPT_MRRU;
        has_short_sequence |= multilink_request[pos] == LCP_OPT_SHORT_SEQUENCE;
        has_endpoint |= multilink_request[pos] == LCP_OPT_ENDPOINT_DISC;
        pos += option_length;
    }
    EXPECT_TRUE(has_mrru);
    EXPECT_TRUE(has_short_sequence);
    EXPECT_TRUE(has_endpoint);

    ppp_close(unconfigured);
    ppp_close(configured);
}

TEST(ModemPpp, SendsEscapedFrameWithValidFcs)
{
    ppp_ctx_t ctx = make_context();
    std::vector<uint8_t> wire;
    ctx.modem = &wire;
    ctx.serial_push = capture_serial;
    ctx.peer_accm = 1u << 0x11;
    const std::vector<uint8_t> payload = { PPP_FLAG, PPP_ESCAPE, 0x11 };

    ppp_send_frame(&ctx, PPP_PROTO_LCP, payload.data(), static_cast<int>(payload.size()));

    ASSERT_GE(wire.size(), 2u);
    EXPECT_EQ(wire.front(), PPP_FLAG);
    EXPECT_EQ(wire.back(), PPP_FLAG);
    std::vector<uint8_t> raw;
    for (size_t pos = 1; pos + 1 < wire.size(); pos++) {
        uint8_t byte = wire[pos];
        if (byte == PPP_ESCAPE) {
            ASSERT_LT(pos + 1, wire.size() - 1);
            byte = wire[++pos] ^ 0x20;
        } else {
            EXPECT_NE(byte, PPP_FLAG);
        }
        raw.push_back(byte);
    }

    ASSERT_GE(raw.size(), 6u);
    uint16_t received_fcs = static_cast<uint16_t>(raw[raw.size() - 2])
                          | (static_cast<uint16_t>(raw.back()) << 8);
    EXPECT_EQ(ppp_fcs16(raw.data(), static_cast<int>(raw.size() - 2)), received_fcs);
    const std::vector<uint8_t> expected = { PPP_ADDRESS, PPP_CONTROL, 0xC0, 0x21,
                                            PPP_FLAG, PPP_ESCAPE, 0x11 };
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(), raw.begin()));
}

TEST(ModemPpp, EscapesFlowControlCharactersRegardlessOfPeerAccm)
{
    ppp_ctx_t ctx = make_context();
    std::vector<uint8_t> wire;
    ctx.modem = &wire;
    ctx.serial_push = capture_serial;
    ctx.peer_accm = 0;
    ctx.flow_accm = (1u << 0x11) | (1u << 0x13);
    const std::vector<uint8_t> payload = { 0x11, 0x13 };
    ppp_send_frame(&ctx, PPP_PROTO_LCP, payload.data(), static_cast<int>(payload.size()));
    ASSERT_FALSE(wire.empty());
    EXPECT_EQ(std::count(wire.begin(), wire.end(), 0x11), 0);
    EXPECT_EQ(std::count(wire.begin(), wire.end(), 0x13), 0);
    uint16_t protocol = 0;
    EXPECT_EQ(decode_captured_frame(wire, protocol), payload);
    EXPECT_EQ(protocol, PPP_PROTO_LCP);
}

TEST(ModemPpp, EnforcesNegotiatedMruInBothDirections)
{
    ppp_ctx_t tx = make_context();
    std::vector<uint8_t> wire;
    tx.state = PPP_STATE_NETWORK;
    tx.modem = &wire;
    tx.serial_push = capture_serial;
    tx.peer_mru = 6;
    const std::vector<uint8_t> oversized = { 1, 2, 3, 4, 5 };

    ppp_send_frame(&tx, PPP_PROTO_IP, oversized.data(), static_cast<int>(oversized.size()));

    EXPECT_TRUE(wire.empty());

    ppp_ctx_t rx = make_context();
    std::vector<uint8_t> received;
    rx.state = PPP_STATE_NETWORK;
    rx.modem = &received;
    rx.network_send_ip = capture_network;
    rx.our_mru = 6;
    send_frame(&rx, oversized, false, PPP_PROTO_IP);
    EXPECT_TRUE(received.empty());
}

TEST(ModemPpp, NaksMruBelowRfcMinimum)
{
    ppp_ctx_t ctx = make_context();
    std::vector<uint8_t> wire;
    ctx.modem = &wire;
    ctx.serial_push = capture_serial;
    const std::vector<uint8_t> request = {
        PPP_CODE_CONFIGURE_REQUEST, 3, 0, 8,
        LCP_OPT_MRU, 4, 0, 64
    };

    send_frame(&ctx, request);

    uint16_t protocol = 0;
    const std::vector<uint8_t> response = decode_captured_frame(wire, protocol);
    ASSERT_EQ(protocol, PPP_PROTO_LCP);
    ASSERT_EQ(response.size(), 8u);
    EXPECT_EQ(response[0], PPP_CODE_CONFIGURE_NAK);
    EXPECT_EQ(response[4], LCP_OPT_MRU);
    EXPECT_EQ(response[6], static_cast<uint8_t>(PPP_DEFAULT_MRU >> 8));
    EXPECT_EQ(response[7], static_cast<uint8_t>(PPP_DEFAULT_MRU));
    EXPECT_FALSE(ctx.lcp_ack_sent);
}

TEST(ModemPpp, SendsTerminateRequestWhenClosingActiveSession)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = ppp_init(&wire, nullptr, capture_serial, capture_network);
    ASSERT_NE(ctx, nullptr);
    ctx->state = PPP_STATE_NETWORK;

    ppp_close(ctx);

    uint16_t protocol = 0;
    const std::vector<uint8_t> payload = decode_captured_frame(wire, protocol);
    EXPECT_EQ(protocol, PPP_PROTO_LCP);
    ASSERT_EQ(payload.size(), 4u);
    EXPECT_EQ(payload[0], PPP_CODE_TERMINATE_REQUEST);
    EXPECT_EQ(payload[2], 0);
    EXPECT_EQ(payload[3], 4);
}

TEST(ModemPpp, LzsExtendedResynchronizesAfterCoherencyMismatch)
{
    ppp_ctx_t tx = make_context();
    ppp_ctx_t rx = make_context();
    std::vector<uint8_t> tx_wire;
    std::vector<uint8_t> rx_wire;
    std::vector<uint8_t> packet(128);
    for (size_t index = 0; index < packet.size(); index++)
        packet[index] = static_cast<uint8_t>((index / 8) % 4);

    tx.state = PPP_STATE_NETWORK;
    tx.modem = &tx_wire;
    tx.serial_push = capture_serial;
    tx.ccp_open = true;
    tx.ccp_tx_method = PPP_CCP_METHOD_LZS_EXTENDED;
    tx.peer_pfc = true;
    ASSERT_TRUE(ppp_ccp_codec_set(&tx, true, PPP_CCP_METHOD_LZS_EXTENDED));

    rx.state = PPP_STATE_NETWORK;
    rx.modem = &rx_wire;
    rx.serial_push = capture_serial;
    rx.network_send_ip = capture_network;
    rx.ccp_open = true;
    rx.ccp_rx_method = PPP_CCP_METHOD_LZS_EXTENDED;
    rx.our_pfc = true;
    ASSERT_TRUE(ppp_ccp_codec_set(&rx, false, PPP_CCP_METHOD_LZS_EXTENDED));

    ppp_send_frame(&tx, PPP_PROTO_IP, packet.data(), static_cast<int>(packet.size()));
    uint16_t protocol = 0;
    std::vector<uint8_t> payload = decode_captured_frame(tx_wire, protocol);
    ASSERT_EQ(protocol, PPP_PROTO_MPPE);
    EXPECT_EQ(payload[0] & 0xA0, 0xA0);
    send_frame(&rx, payload, false, protocol);
    EXPECT_EQ(rx_wire, packet);

    tx_wire.clear();
    rx_wire.clear();
    ppp_send_frame(&tx, PPP_PROTO_IP, packet.data(), static_cast<int>(packet.size()));
    payload = decode_captured_frame(tx_wire, protocol);
    ASSERT_EQ(protocol, PPP_PROTO_MPPE);
    EXPECT_EQ(payload[0] & 0x80, 0);
    ASSERT_EQ(payload[1], 1);
    payload[1] = 2;
    send_frame(&rx, payload, false, protocol);
    EXPECT_TRUE(rx.ccp_reset_pending);

    std::vector<uint8_t> reset = decode_captured_frame(rx_wire, protocol);
    ASSERT_EQ(protocol, PPP_PROTO_CCP);
    ASSERT_EQ(reset.size(), 6u);
    EXPECT_EQ(reset[0], PPP_CODE_RESET_REQUEST);
    EXPECT_EQ(reset[4], 0);
    EXPECT_EQ(reset[5], 1);

    tx_wire.clear();
    send_frame(&tx, reset, false, PPP_PROTO_CCP);
    EXPECT_TRUE(tx_wire.empty());

    tx_wire.clear();
    ppp_send_frame(&tx, PPP_PROTO_IP, packet.data(), static_cast<int>(packet.size()));
    payload = decode_captured_frame(tx_wire, protocol);
    ASSERT_EQ(protocol, PPP_PROTO_MPPE);
    EXPECT_NE(payload[0] & 0x80, 0);
    ASSERT_EQ(payload[1], 2);
    rx_wire.clear();
    send_frame(&rx, payload, false, protocol);
    EXPECT_FALSE(rx.ccp_reset_pending);
    EXPECT_EQ(rx_wire, packet);

    ppp_ccp_codec_close(&tx);
    ppp_ccp_codec_close(&rx);
}

TEST(ModemPpp, RoundTripsNegotiatedVjPacketsOverPpp)
{
    ppp_ctx_t tx = make_context();
    ppp_ctx_t rx = make_context();
    std::vector<uint8_t> wire;
    std::vector<uint8_t> received;
    tx.state = PPP_STATE_NETWORK;
    tx.modem = &wire;
    tx.serial_push = capture_serial;
    tx.vj_tx_ctx = cslip_init(nullptr);
    tx.vj_tx_enabled = true;
    tx.vj_tx_max_slot_id = IPCP_VJ_MAX_SLOT_ID;
    rx.state = PPP_STATE_NETWORK;
    rx.modem = &received;
    rx.network_send_ip = capture_network;
    rx.vj_rx_ctx = cslip_init(nullptr);
    rx.vj_rx_enabled = true;
    rx.vj_rx_max_slot_id = IPCP_VJ_MAX_SLOT_ID;
    ASSERT_NE(tx.vj_tx_ctx, nullptr);
    ASSERT_NE(rx.vj_rx_ctx, nullptr);

    auto first = make_tcp_packet(1, 100);
    ppp_wrap_ip(&tx, first.data(), static_cast<int>(first.size()));
    uint16_t protocol = 0;
    std::vector<uint8_t> payload = decode_captured_frame(wire, protocol);
    ASSERT_EQ(protocol, PPP_PROTO_VJ_UNCOMPRESSED);
    ASSERT_LE(payload[9], IPCP_VJ_MAX_SLOT_ID);
    send_frame(&rx, payload, false, protocol);
    EXPECT_EQ(received, first);

    wire.clear();
    auto second = make_tcp_packet(2, 101);
    second[31] = 1;
    second[10] = 0;
    second[11] = 0;
    uint32_t sum = 0;
    for (size_t pos = 0; pos < 20; pos += 2)
        sum += (static_cast<uint16_t>(second[pos]) << 8) | second[pos + 1];
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    uint16_t checksum = static_cast<uint16_t>(~sum);
    second[10] = static_cast<uint8_t>(checksum >> 8);
    second[11] = static_cast<uint8_t>(checksum);
    ppp_wrap_ip(&tx, second.data(), static_cast<int>(second.size()));
    payload = decode_captured_frame(wire, protocol);
    ASSERT_EQ(protocol, PPP_PROTO_VJ_COMPRESSED);
    send_frame(&rx, payload, false, protocol);
    EXPECT_EQ(received, second);

    cslip_close(tx.vj_tx_ctx);
    cslip_close(rx.vj_rx_ctx);
}

TEST(ModemPpp, DropsOversizedIpPacketBeforeVjCompression)
{
    ppp_ctx_t ctx = make_context();
    std::vector<uint8_t> wire;
    std::vector<uint8_t> oversized(PPP_MAX_FRAME + 1, 0);
    ctx.state = PPP_STATE_NETWORK;
    ctx.modem = &wire;
    ctx.serial_push = capture_serial;
    ctx.vj_tx_ctx = cslip_init(nullptr);
    ctx.vj_tx_enabled = true;
    ASSERT_NE(ctx.vj_tx_ctx, nullptr);

    ppp_wrap_ip(&ctx, oversized.data(), static_cast<int>(oversized.size()));

    EXPECT_TRUE(wire.empty());
    cslip_close(ctx.vj_tx_ctx);
}

TEST(ModemPpp, EncryptsNegotiatedPayloadAndRejectsPlaintext)
{
    ppp_ctx_t tx = make_context();
    ppp_ctx_t rx = make_context();
    std::vector<uint8_t> wire;
    std::vector<uint8_t> received;
    tx.state = PPP_STATE_NETWORK;
    tx.modem = &wire;
    tx.serial_push = capture_serial;
    tx.ccp_open = true;
    tx.mppe_keys_ready = true;
    tx.mppe_tx_enabled = true;
    ASSERT_TRUE(ppp_mppe_configure(&tx.mppe_tx, 128, false));
    rx.state = PPP_STATE_NETWORK;
    rx.modem = &received;
    rx.network_send_ip = capture_network;
    rx.ccp_open = true;
    rx.mppe_keys_ready = true;
    rx.mppe_rx_enabled = true;
    rx.mppe_min_bits = 128;
    ASSERT_TRUE(ppp_mppe_configure(&rx.mppe_rx, 128, false));

    const std::vector<uint8_t> ip_packet = { 0x45, 0x00, 0x00, 0x04 };
    ppp_send_frame(&tx, PPP_PROTO_IP, ip_packet.data(), static_cast<int>(ip_packet.size()));

    uint16_t protocol = 0;
    std::vector<uint8_t> payload = decode_captured_frame(wire, protocol);
    ASSERT_EQ(protocol, PPP_PROTO_MPPE);
    send_frame(&rx, payload, false, protocol);
    EXPECT_EQ(received, ip_packet);

    received.clear();
    send_frame(&rx, ip_packet, false, PPP_PROTO_IP);
    EXPECT_TRUE(received.empty());
}

TEST(ModemPpp, RequiredMppeRejectsPlaintextBeforeCcpOpens)
{
    ppp_ctx_t ctx = make_context();
    std::vector<uint8_t> received;
    ctx.state = PPP_STATE_NETWORK;
    ctx.modem = &received;
    ctx.network_send_ip = capture_network;
    ctx.mppe_min_bits = 128;
    const std::vector<uint8_t> ip_packet = { 0x45, 0x00, 0x00, 0x04 };

    send_frame(&ctx, ip_packet, false, PPP_PROTO_IP);
    EXPECT_TRUE(received.empty());
}

TEST(ModemPpp, OptionalMppeAllowsPlaintextForLegacyPeers)
{
    ppp_ctx_t ctx = make_context();
    std::vector<uint8_t> received;
    ctx.state = PPP_STATE_NETWORK;
    ctx.modem = &received;
    ctx.network_send_ip = capture_network;
    ctx.mppe_keys_ready = true;
    ctx.ccp_open = true;
    ctx.mppe_rx_enabled = true;
    const std::vector<uint8_t> ip_packet = { 0x45, 0x00, 0x00, 0x04 };

    send_frame(&ctx, ip_packet, false, PPP_PROTO_IP);
    EXPECT_EQ(received, ip_packet);
}

TEST(ModemPpp, AppliesMppcBeforeMppeAndReversesBothOnReceive)
{
    ppp_ctx_t tx = make_context();
    ppp_ctx_t rx = make_context();
    std::vector<uint8_t> wire;
    std::vector<uint8_t> received;
    tx.state = PPP_STATE_NETWORK;
    tx.modem = &wire;
    tx.serial_push = capture_serial;
    tx.ccp_open = true;
    tx.ccp_tx_method = PPP_CCP_METHOD_MPPC;
    tx.mppe_keys_ready = true;
    tx.mppe_tx_enabled = true;
    ASSERT_TRUE(ppp_ccp_codec_set(&tx, true, PPP_CCP_METHOD_MPPC));
    ASSERT_TRUE(ppp_mppe_configure(&tx.mppe_tx, 128, false));
    rx.state = PPP_STATE_NETWORK;
    rx.modem = &received;
    rx.network_send_ip = capture_network;
    rx.ccp_open = true;
    rx.ccp_rx_method = PPP_CCP_METHOD_MPPC;
    rx.mppe_keys_ready = true;
    rx.mppe_rx_enabled = true;
    ASSERT_TRUE(ppp_ccp_codec_set(&rx, false, PPP_CCP_METHOD_MPPC));
    ASSERT_TRUE(ppp_mppe_configure(&rx.mppe_rx, 128, false));

    std::vector<uint8_t> ip_packet(128);
    for (size_t index = 0; index < ip_packet.size(); index++)
        ip_packet[index] = static_cast<uint8_t>((index / 8) % 4);
    ppp_send_frame(&tx, PPP_PROTO_IP, ip_packet.data(), static_cast<int>(ip_packet.size()));

    uint16_t protocol = 0;
    std::vector<uint8_t> payload = decode_captured_frame(wire, protocol);
    ASSERT_EQ(protocol, PPP_PROTO_MPPE);
    send_frame(&rx, payload, false, protocol);
    EXPECT_EQ(received, ip_packet);
    ppp_ccp_codec_close(&tx);
    ppp_ccp_codec_close(&rx);
}

TEST(ModemPpp, CompressionOnlyNegotiationCarriesTrafficBothWays)
{
    ppp_ctx_t modem = make_context();
    ppp_ctx_t client = make_context();
    ras_capture_t modem_capture;
    ras_capture_t client_capture;
    modem.state = client.state = PPP_STATE_NETWORK;
    modem.modem = &modem_capture;
    modem.serial_push = capture_ras_serial;
    modem.network_send_ip = capture_ras_network;
    modem.mppe_keys_ready = true;
    modem.mppe_allowed_bits = CCP_MPPE_KEY_BITS;
    modem.ppp_compression = PPP_COMPRESSION_PREFER_MPPC;
    ppp_ccp_start(&modem);
    const uint8_t peer_request[] = {
        PPP_CODE_CONFIGURE_REQUEST, 1, 0, 10, 18, 6, 0, 0, 0, 1
    };
    ppp_ccp_process(&modem, peer_request, sizeof(peer_request));
    std::vector<uint8_t> ack(modem.ccp_request, modem.ccp_request + modem.ccp_request_len);
    ack[0] = PPP_CODE_CONFIGURE_ACK;
    ppp_ccp_process(&modem, ack.data(), static_cast<int>(ack.size()));
    ASSERT_TRUE(modem.ccp_open);
    ASSERT_FALSE(modem.mppe_tx_enabled);
    ASSERT_FALSE(modem.mppe_rx_enabled);

    client.modem = &client_capture;
    client.serial_push = capture_ras_serial;
    client.network_send_ip = capture_ras_network;
    client.ccp_open = true;
    ASSERT_TRUE(ppp_ccp_codec_set(&client, true, PPP_CCP_METHOD_MPPC));
    ASSERT_TRUE(ppp_ccp_codec_set(&client, false, PPP_CCP_METHOD_MPPC));
    const auto request = make_tcp_packet(1, 100);
    ppp_send_frame(&client, PPP_PROTO_IP, request.data(), static_cast<int>(request.size()));
    uint16_t protocol = 0;
    auto payload = decode_captured_frame(client_capture.serial, protocol);
    ASSERT_EQ(protocol, PPP_PROTO_MPPE);
    send_frame(&modem, payload, false, protocol);
    ASSERT_EQ(modem_capture.packets.size(), 1u);
    EXPECT_EQ(modem_capture.packets[0], request);

    modem_capture.serial.clear();
    const auto reply = make_tcp_packet(2, 200);
    ppp_send_frame(&modem, PPP_PROTO_IP, reply.data(), static_cast<int>(reply.size()));
    payload = decode_captured_frame(modem_capture.serial, protocol);
    ASSERT_EQ(protocol, PPP_PROTO_MPPE);
    send_frame(&client, payload, false, protocol);
    ASSERT_EQ(client_capture.packets.size(), 1u);
    EXPECT_EQ(client_capture.packets[0], reply);
    ppp_ccp_codec_close(&modem);
    ppp_ccp_codec_close(&client);
}

TEST(ModemPpp, DecodesWindows2000MppcFrames)
{
    ppp_ctx_t rx = make_context();
    std::vector<std::vector<uint8_t>> received;
    rx.state = PPP_STATE_NETWORK;
    rx.modem = &received;
    rx.network_send_ip = capture_network_packets;
    rx.mppe_keys_ready = true;
    rx.mppe_allowed_bits = CCP_MPPE_KEY_BITS;
    rx.ppp_compression = PPP_COMPRESSION_PREFER_MPPC;
    ppp_ccp_start(&rx);
    /* Exercise the receive-side safeguard for an already asymmetric session. */
    rx.ccp_ack_sent = true;
    ASSERT_TRUE(ppp_ccp_codec_set(&rx, true, PPP_CCP_METHOD_MPPC));
    const uint8_t nak[] = {
        PPP_CODE_CONFIGURE_NAK, rx.ccp_request_id, 0, 10, 18, 6, 0, 0, 0, 0x41
    };
    ppp_ccp_process(&rx, nak, sizeof(nak));
    std::vector<uint8_t> ack(rx.ccp_request, rx.ccp_request + rx.ccp_request_len);
    ack[0] = PPP_CODE_CONFIGURE_ACK;
    ppp_ccp_process(&rx, ack.data(), static_cast<int>(ack.size()));
    ASSERT_TRUE(rx.ccp_open);
    ASSERT_TRUE(rx.mppe_rx_enabled);

    const std::vector<std::vector<uint8_t>> frames = {
        { 0xe0, 0x00, 0x00, 0x21, 0x45, 0x00, 0x01, 0x48, 0x00, 0x00, 0x00, 0x00,
          0x80, 0x08, 0x96, 0xc5, 0xc2, 0x80, 0x00, 0x83, 0xef, 0xf7, 0xfb, 0xfd,
          0xfc, 0x01, 0x10, 0x01, 0x0c, 0x04, 0xd2, 0x88, 0xc4, 0x02, 0x10, 0x0c,
          0x00, 0x04, 0x27, 0x04, 0x27, 0x06, 0xf8, 0x5e, 0xe4, 0x78, 0xef, 0x0e,
          0x7c, 0x59, 0x53, 0x45, 0xf1, 0xe7, 0xc5, 0xbf, 0x1f, 0x1f, 0x27, 0x3f,
          0x2f, 0x5f, 0x37, 0x7f, 0x3f, 0x87, 0xd1, 0xe3, 0xf4, 0xf9, 0x7d, 0x5e,
          0x7f, 0x5f, 0xa7, 0xd9, 0xeb, 0xf6, 0xf5, 0x63, 0x81, 0x29, 0xb1, 0x9a,
          0x80, 0x84, 0x1e, 0x83, 0x84, 0x00, 0x74, 0xd4, 0x86, 0x07, 0x9c, 0x1b,
          0x31, 0x37, 0xbc, 0x16, 0xb2, 0x9a, 0x1b, 0x1b, 0xb2, 0x18, 0x18, 0x33,
          0x31, 0x9e, 0x04, 0x26, 0xa9, 0xa3, 0x2a, 0x10, 0x1a, 0x97, 0x18, 0x1b,
          0x82, 0x03, 0x16, 0x15, 0x87, 0xdf, 0xff, 0xfd, 0x40, 0x00 },
        { 0x20, 0x01, 0x00, 0xc0, 0x0a, 0xa0, 0x1c, 0x00, 0xa9, 0x8b, 0x77, 0x74,
          0x60, 0x05, 0x65, 0x3f, 0xc0, 0x0a, 0xc4, 0x2f, 0xf5, 0x7b, 0x91, 0xe3,
          0xbc, 0x39, 0xf1, 0x67, 0x8c, 0x27, 0xca, 0x9f, 0x16, 0xfc, 0x7c, 0x7c,
          0x9c, 0xfc, 0xbd, 0x7c, 0xdd, 0xfc, 0xfe, 0x1f, 0x47, 0x8f, 0xd3, 0xe5,
          0xf5, 0x79, 0xfd, 0x7e, 0x9f, 0x67, 0xaf, 0xdb, 0xcb, 0x00, 0x2b, 0xdf,
          0x00 },
        { 0x20, 0x02, 0xc0, 0x0a, 0x80, 0x03, 0xc0, 0x00, 0x2c, 0x00, 0xa0, 0x08,
          0xf4, 0x87, 0xbb, 0xa0, 0x20, 0x23, 0xc2, 0x00, 0x25, 0x2e, 0x01, 0x00,
          0x00, 0x80, 0x30, 0xb1, 0x31, 0xb2, 0x32, 0xb3, 0x33, 0xb4, 0x34, 0xb5,
          0x35, 0xb6, 0x36, 0xb7, 0x37, 0xb8, 0x38, 0xb9, 0x39, 0xba, 0x3a, 0xbb,
          0x3b, 0xfa, 0xf8, 0x34, 0x80 },
        { 0x20, 0x03, 0xff, 0xac, 0x0f, 0xfe, 0x99, 0x07, 0xfd, 0x92, 0x4f,
          0xfc, 0x02, 0xff, 0xbc, 0x06, 0x90 },
        { 0x20, 0x04, 0xff, 0xac, 0x13, 0xfe, 0x98, 0xff, 0xfd, 0x92, 0x47,
          0xfc, 0x03, 0xff, 0xbc, 0x06, 0x90 },
        { 0x20, 0x05, 0xff, 0xac, 0x17, 0xfe, 0x98, 0xf7, 0xfd, 0x92, 0x3f,
          0xfc, 0x04, 0xff, 0xbc, 0x06, 0x90 }
    };
    send_frame(&rx, frames[0], false, PPP_PROTO_MPPE);
    EXPECT_TRUE(received.empty());
    ASSERT_FALSE(rx.ccp_open);
    ASSERT_EQ(rx.ccp_request_bits, CCP_MPPC);
    ASSERT_FALSE(rx.mppe_rx_enabled);
    EXPECT_EQ(rx.ccp_rx_method, PPP_CCP_METHOD_NONE);
    ppp_ccp_process(&rx, ack.data(), static_cast<int>(ack.size()));
    EXPECT_FALSE(rx.ccp_ack_received);
    send_frame(&rx, frames[0], false, PPP_PROTO_MPPE);
    EXPECT_TRUE(received.empty());
    const uint8_t first_retry_id = rx.ccp_request_id;
    rx.ccp_timeout_ms = 2999;
    ppp_timer_tick(&rx);
    EXPECT_NE(rx.ccp_request_id, first_retry_id);
    std::vector<uint8_t> mppc_ack(rx.ccp_request, rx.ccp_request + rx.ccp_request_len);
    mppc_ack[0] = PPP_CODE_CONFIGURE_ACK;
    ppp_ccp_process(&rx, mppc_ack.data(), static_cast<int>(mppc_ack.size()));
    ASSERT_TRUE(rx.ccp_open);
    ASSERT_FALSE(rx.mppe_rx_enabled);
    for (const auto &frame : frames)
        send_frame(&rx, frame, false, PPP_PROTO_MPPE);

    ASSERT_EQ(received.size(), frames.size());
    ASSERT_EQ(received[0].size(), 0x148u);
    for (const auto &packet : received)
        EXPECT_EQ(packet[0] >> 4, 4);
    for (size_t index = 3; index < received.size(); index++) {
        ASSERT_GE(received[index].size(), 28u);
        EXPECT_EQ(received[index][9], 1);
        EXPECT_EQ(received[index][20], 8);
    }
    ppp_ccp_codec_close(&rx);
}

TEST(ModemPpp, WaitsForMppcAckDuringAsymmetricRenegotiation)
{
    ppp_ctx_t rx = make_context();
    std::vector<uint8_t> received;
    rx.state = PPP_STATE_NETWORK;
    rx.modem = &received;
    rx.network_send_ip = capture_network;
    rx.ccp_direction = PPP_CCP_DIRECTION_ASYMMETRIC_PREFERRED;
    rx.ccp_open = true;
    rx.ccp_ack_sent = true;
    rx.ccp_ack_received = true;
    rx.ccp_tx_method = PPP_CCP_METHOD_MPPC;
    rx.ccp_rx_method = PPP_CCP_METHOD_MPPC;
    rx.mppe_keys_ready = true;
    rx.mppe_rx_enabled = true;
    ASSERT_TRUE(ppp_ccp_codec_set(&rx, false, PPP_CCP_METHOD_MPPC));
    ASSERT_TRUE(ppp_mppe_configure(&rx.mppe_rx, 128, true));

    const std::vector<uint8_t> frame = { 0xc0, 0, 0, 0x21, 0x45, 0, 0, 4 };
    send_frame(&rx, frame, false, PPP_PROTO_MPPE);
    EXPECT_TRUE(rx.ccp_open);
    EXPECT_TRUE(rx.ccp_req_sent);
    send_frame(&rx, frame, false, PPP_PROTO_MPPE);
    EXPECT_TRUE(received.empty());

    std::vector<uint8_t> ack(rx.ccp_request, rx.ccp_request + rx.ccp_request_len);
    ack[0] = PPP_CODE_CONFIGURE_ACK;
    ppp_ccp_process(&rx, ack.data(), static_cast<int>(ack.size()));
    send_frame(&rx, frame, false, PPP_PROTO_MPPE);
    EXPECT_EQ(received, (std::vector<uint8_t>{ 0x45, 0, 0, 4 }));
    ppp_ccp_codec_close(&rx);
}

TEST(ModemPpp, RenegotiatesMppcWithLivePeer)
{
    ppp_ctx_t peer = make_context();
    ppp_ctx_t server = make_context();
    std::vector<uint8_t> peer_wire;
    ras_capture_t server_io;
    peer.state = PPP_STATE_NETWORK;
    peer.modem = &peer_wire;
    peer.serial_push = capture_serial;
    peer.ccp_open = true;
    peer.ccp_ack_sent = true;
    peer.ccp_ack_received = true;
    peer.ppp_compression = PPP_COMPRESSION_PREFER_MPPC;
    ASSERT_TRUE(ppp_ccp_codec_set(&peer, true, PPP_CCP_METHOD_MPPC));

    server.state = PPP_STATE_NETWORK;
    server.modem = &server_io;
    server.serial_push = capture_ras_serial;
    server.network_send_ip = capture_ras_network;
    server.ccp_open = true;
    server.ccp_ack_sent = true;
    server.ccp_ack_received = true;
    server.ccp_tx_method = PPP_CCP_METHOD_MPPC;
    server.mppe_keys_ready = true;
    server.mppe_rx_enabled = true;
    ASSERT_TRUE(ppp_ccp_codec_set(&server, false, PPP_CCP_METHOD_MPPC));
    ASSERT_TRUE(ppp_mppe_configure(&server.mppe_rx, 128, true));

    const std::vector<uint8_t> ip_packet(128, 0x45);
    ppp_send_frame(&peer, PPP_PROTO_IP, ip_packet.data(), static_cast<int>(ip_packet.size()));
    uint16_t protocol = 0;
    std::vector<uint8_t> frame = decode_captured_frame(peer_wire, protocol);
    ASSERT_EQ(protocol, PPP_PROTO_MPPE);
    send_frame(&server, frame, false, protocol);
    ASSERT_TRUE(server_io.packets.empty());
    ASSERT_TRUE(server.ccp_req_sent);

    frame = decode_captured_frame(server_io.serial, protocol);
    ASSERT_EQ(protocol, PPP_PROTO_CCP);
    ASSERT_EQ(frame[0], PPP_CODE_CONFIGURE_REQUEST);
    peer_wire.clear();
    send_frame(&peer, frame, false, protocol);
    frame = decode_captured_frame(peer_wire, protocol);
    ASSERT_EQ(protocol, PPP_PROTO_CCP);
    ASSERT_EQ(frame[0], PPP_CODE_CONFIGURE_ACK);
    send_frame(&server, frame, false, protocol);
    ASSERT_TRUE(server.ccp_open);
    ASSERT_FALSE(server.mppe_rx_enabled);

    peer_wire.clear();
    ppp_send_frame(&peer, PPP_PROTO_IP, ip_packet.data(), static_cast<int>(ip_packet.size()));
    frame = decode_captured_frame(peer_wire, protocol);
    ASSERT_EQ(protocol, PPP_PROTO_MPPE);
    send_frame(&server, frame, false, protocol);
    ASSERT_EQ(server_io.packets.size(), 1u);
    EXPECT_EQ(server_io.packets[0], ip_packet);
    ppp_ccp_codec_close(&peer);
    ppp_ccp_codec_close(&server);
}

TEST(ModemPpp, DiscardsMppcAfterAsymmetricRenegotiationTimesOut)
{
    ppp_ctx_t rx = make_context();
    std::vector<uint8_t> received;
    rx.state = PPP_STATE_NETWORK;
    rx.modem = &received;
    rx.network_send_ip = capture_network;
    rx.ccp_direction = PPP_CCP_DIRECTION_ASYMMETRIC_PREFERRED;
    rx.ccp_open = true;
    rx.ccp_ack_sent = true;
    rx.ccp_ack_received = true;
    rx.ccp_tx_method = PPP_CCP_METHOD_MPPC;
    rx.ccp_rx_method = PPP_CCP_METHOD_MPPC;
    rx.mppe_keys_ready = true;
    rx.mppe_rx_enabled = true;
    ASSERT_TRUE(ppp_ccp_codec_set(&rx, false, PPP_CCP_METHOD_MPPC));
    ASSERT_TRUE(ppp_mppe_configure(&rx.mppe_rx, 128, true));

    const std::vector<uint8_t> frame = { 0xc0, 0, 0, 0x21, 0x45, 0, 0, 4 };
    send_frame(&rx, frame, false, PPP_PROTO_MPPE);
    rx.ccp_timeout_ms = 2999;
    rx.ccp_timeout_retries = 10;
    ppp_timer_tick(&rx);

    EXPECT_TRUE(rx.ccp_open);
    EXPECT_FALSE(rx.ccp_req_sent);
    EXPECT_EQ(rx.ccp_rx_method, PPP_CCP_METHOD_NONE);
    send_frame(&rx, frame, false, PPP_PROTO_MPPE);
    EXPECT_TRUE(received.empty());
    ppp_ccp_codec_close(&rx);
}

TEST(ModemPpp, DoesNotRenegotiateForOtherUnencryptedMppcPackets)
{
    for (int scenario = 0; scenario < 5; scenario++) {
        ppp_ctx_t rx = make_context();
        std::vector<uint8_t> wire;
        rx.state = PPP_STATE_NETWORK;
        rx.modem = &wire;
        rx.serial_push = capture_serial;
        rx.ccp_open = true;
        rx.ccp_ack_sent = true;
        rx.ccp_ack_received = true;
        rx.ccp_tx_method = PPP_CCP_METHOD_MPPC;
        rx.ccp_rx_method = PPP_CCP_METHOD_MPPC;
        rx.mppe_keys_ready = true;
        rx.mppe_rx_enabled = true;
        rx.mppe_min_bits = scenario == 0 ? 128 : 0;
        rx.ccp_peer_mppe = scenario == 1;
        ASSERT_TRUE(ppp_mppe_configure(&rx.mppe_rx, 128, true));
        if (scenario == 2)
            rx.mppe_rx.receive_initialized = true;
        const uint8_t flags = scenario == 3 ? 0x20 : 0xe0;
        const uint8_t count = scenario == 4 ? 1 : 0;

        send_frame(&rx, { flags, count, 0, 0x21, 0x45 }, false, PPP_PROTO_MPPE);

        EXPECT_TRUE(wire.empty()) << "scenario " << scenario;
        EXPECT_TRUE(rx.ccp_open) << "scenario " << scenario;
        EXPECT_TRUE(rx.mppe_rx_enabled) << "scenario " << scenario;
    }
}

TEST(ModemPpp, TransportsStacLzsAndUsesNativeProtocolWhenItExpands)
{
    ppp_ctx_t tx = make_context();
    ppp_ctx_t rx = make_context();
    std::vector<uint8_t> wire;
    std::vector<uint8_t> received;
    tx.state = PPP_STATE_NETWORK;
    tx.modem = &wire;
    tx.serial_push = capture_serial;
    tx.ccp_open = true;
    tx.ccp_tx_method = PPP_CCP_METHOD_LZS;
    rx.state = PPP_STATE_NETWORK;
    rx.modem = &received;
    rx.network_send_ip = capture_network;
    rx.ccp_open = true;
    rx.ccp_rx_method = PPP_CCP_METHOD_LZS;
    ASSERT_TRUE(ppp_ccp_codec_set(&tx, true, PPP_CCP_METHOD_LZS));
    ASSERT_TRUE(ppp_ccp_codec_set(&rx, false, PPP_CCP_METHOD_LZS));

    std::vector<uint8_t> compressible(128);
    for (size_t index = 0; index < compressible.size(); index++)
        compressible[index] = static_cast<uint8_t>((index / 8) % 4);
    ppp_send_frame(&tx, PPP_PROTO_IP, compressible.data(),
                   static_cast<int>(compressible.size()));

    uint16_t protocol = 0;
    std::vector<uint8_t> payload = decode_captured_frame(wire, protocol);
    ASSERT_EQ(protocol, PPP_PROTO_MPPE);
    send_frame(&rx, payload, false, protocol);
    EXPECT_EQ(received, compressible);

    wire.clear();
    received.clear();
    const std::vector<uint8_t> incompressible = { 0x45, 0xA3 };
    ppp_send_frame(&tx, PPP_PROTO_IP, incompressible.data(),
                   static_cast<int>(incompressible.size()));
    payload = decode_captured_frame(wire, protocol);
    ASSERT_EQ(protocol, PPP_PROTO_IP);
    EXPECT_EQ(payload, incompressible);
    send_frame(&rx, payload, false, protocol);
    EXPECT_EQ(received, incompressible);

    ppp_ccp_codec_close(&tx);
    ppp_ccp_codec_close(&rx);
}

TEST(ModemPpp, ReplaysPapAckForDuplicateRequestAfterAuthentication)
{
    ppp_ctx_t ctx = make_context();
    std::vector<uint8_t> wire;
    ctx.state = PPP_STATE_NETWORK;
    ctx.modem = &wire;
    ctx.serial_push = capture_serial;
    ctx.auth_type = PPP_AUTH_PAP;
    ctx.auth_complete = true;
    const std::vector<uint8_t> request = {
        PAP_CODE_AUTHENTICATE_REQUEST, 9, 0, 8, 1, 'a', 1, 'b'
    };
    ctx.pap_last_request_len = static_cast<uint16_t>(request.size());
    std::memcpy(ctx.pap_last_request, request.data(), request.size());
    ctx.pap_last_request_valid = true;

    send_frame(&ctx, request, false, PPP_PROTO_PAP);

    uint16_t protocol = 0;
    const std::vector<uint8_t> response = decode_captured_frame(wire, protocol);
    EXPECT_EQ(protocol, PPP_PROTO_PAP);
    ASSERT_GE(response.size(), 2u);
    EXPECT_EQ(response[0], PAP_CODE_AUTHENTICATE_ACK);
    EXPECT_EQ(response[1], 9);
}

TEST(ModemPpp, TransportsV44AndUsesNativeProtocolWhenItExpands)
{
    ppp_ctx_t tx = make_context();
    ppp_ctx_t rx = make_context();
    std::vector<uint8_t> wire;
    std::vector<uint8_t> received;
    tx.state = PPP_STATE_NETWORK;
    tx.modem = &wire;
    tx.serial_push = capture_serial;
    tx.ccp_open = true;
    tx.ccp_tx_method = PPP_CCP_METHOD_V44;
    rx.state = PPP_STATE_NETWORK;
    rx.modem = &received;
    rx.network_send_ip = capture_network;
    rx.ccp_open = true;
    rx.ccp_rx_method = PPP_CCP_METHOD_V44;
    ASSERT_TRUE(ppp_ccp_codec_set(&tx, true, PPP_CCP_METHOD_V44));
    ASSERT_TRUE(ppp_ccp_codec_set(&rx, false, PPP_CCP_METHOD_V44));

    std::vector<uint8_t> compressible(128);
    std::vector<uint8_t> v44_plaintext = { static_cast<uint8_t>(PPP_PROTO_IP >> 8),
                                           static_cast<uint8_t>(PPP_PROTO_IP) };
    std::vector<uint8_t> v44_encoded(PPP_MAX_FRAME);
    std::vector<uint8_t> v44_decoded(PPP_MAX_FRAME);
    int v44_encoded_len;
    int v44_decoded_len;
    for (size_t index = 0; index < compressible.size(); index++)
        compressible[index] = static_cast<uint8_t>((index / 8) % 4);
    v44_plaintext.insert(v44_plaintext.end(), compressible.begin(), compressible.end());
    for (size_t length = 1; length <= v44_plaintext.size(); length++) {
        SCOPED_TRACE(::testing::Message() << "V.44 input length=" << length);
        ASSERT_TRUE(ppp_ccp_codec_compress(&tx, v44_plaintext.data(),
                                           static_cast<int>(length),
                                           v44_encoded.data(), v44_encoded.size(),
                                           &v44_encoded_len));
        ASSERT_TRUE(ppp_ccp_codec_decompress(&rx, v44_encoded.data(), v44_encoded_len,
                                             v44_decoded.data(), v44_decoded.size(),
                                             &v44_decoded_len)) << "input length=" << length;
        ASSERT_EQ(v44_decoded_len, length);
        EXPECT_TRUE(std::equal(v44_plaintext.begin(), v44_plaintext.begin() + length,
                               v44_decoded.begin()));
    }

    ppp_send_frame(&tx, PPP_PROTO_IP, compressible.data(),
                   static_cast<int>(compressible.size()));

    uint16_t protocol = 0;
    std::vector<uint8_t> payload = decode_captured_frame(wire, protocol);
    ASSERT_EQ(protocol, PPP_PROTO_MPPE);
    send_frame(&rx, payload, false, protocol);
    EXPECT_EQ(received, compressible);

    wire.clear();
    received.clear();
    const std::vector<uint8_t> incompressible = { 0x45, 0xA3 };
    ppp_send_frame(&tx, PPP_PROTO_IP, incompressible.data(),
                   static_cast<int>(incompressible.size()));
    payload = decode_captured_frame(wire, protocol);
    ASSERT_EQ(protocol, PPP_PROTO_IP);
    EXPECT_EQ(payload, incompressible);
    send_frame(&rx, payload, false, protocol);
    EXPECT_EQ(received, incompressible);

    ppp_ccp_codec_close(&tx);
    ppp_ccp_codec_close(&rx);
}

TEST(ModemPpp, DeliversFragmentedAndConcatenatedPredictor2Records)
{
    ppp_ctx_t tx = make_context();
    ppp_ctx_t rx = make_context();
    std::vector<std::vector<uint8_t>> received;
    std::array<uint8_t, 160> encoded{};
    std::vector<uint8_t> stream;
    int encoded_len;
    int first_encoded_len;

    tx.ccp_tx_method = PPP_CCP_METHOD_PREDICTOR2;
    ASSERT_TRUE(ppp_ccp_codec_set(&tx, true, PPP_CCP_METHOD_PREDICTOR2));
    ASSERT_TRUE(ppp_ccp_codec_set(&rx, false, PPP_CCP_METHOD_PREDICTOR2));
    rx.state = PPP_STATE_NETWORK;
    rx.ccp_open = true;
    rx.ccp_rx_method = PPP_CCP_METHOD_PREDICTOR2;
    rx.modem = &received;
    rx.network_send_ip = capture_network_packets;

    std::vector<uint8_t> first_packet = { 0x00, 0x21 };
    std::vector<uint8_t> second_packet = { 0x00, 0x21 };
    for (uint16_t index = 0; index < 128; index++) {
        first_packet.push_back(static_cast<uint8_t>((index / 8) % 4));
        second_packet.push_back(static_cast<uint8_t>(index * 37 + 11));
    }

    ASSERT_TRUE(ppp_ccp_codec_compress(&tx, first_packet.data(), first_packet.size(),
                                       encoded.data(), encoded.size(), &encoded_len));
    first_encoded_len = encoded_len;
    stream.insert(stream.end(), encoded.begin(), encoded.begin() + encoded_len);
    ASSERT_TRUE(ppp_ccp_codec_compress(&tx, second_packet.data(), second_packet.size(),
                                       encoded.data(), encoded.size(), &encoded_len));
    stream.insert(stream.end(), encoded.begin(), encoded.begin() + encoded_len);

    const size_t split = static_cast<size_t>(first_encoded_len / 2);
    ASSERT_GT(split, 2u);
    send_frame(&rx, { stream.begin(), stream.begin() + split }, false, PPP_PROTO_MPPE);
    EXPECT_TRUE(received.empty());
    EXPECT_FALSE(rx.ccp_reset_pending);

    send_frame(&rx, { stream.begin() + split, stream.end() }, false, PPP_PROTO_MPPE);
    ASSERT_EQ(received.size(), 2u);
    EXPECT_EQ(received[0], std::vector<uint8_t>(first_packet.begin() + 2, first_packet.end()));
    EXPECT_EQ(received[1], std::vector<uint8_t>(second_packet.begin() + 2, second_packet.end()));
    EXPECT_FALSE(rx.ccp_reset_pending);

    ppp_ccp_codec_close(&tx);
    ppp_ccp_codec_close(&rx);
}

TEST(ModemPpp, RecoversMppcAfterMalformedPacketReset)
{
    ppp_ctx_t tx = make_context();
    ppp_ctx_t rx = make_context();
    std::vector<uint8_t> tx_wire;
    std::vector<uint8_t> rx_wire;
    std::vector<uint8_t> received;
    tx.state = PPP_STATE_NETWORK;
    tx.modem = &tx_wire;
    tx.serial_push = capture_serial;
    tx.ccp_open = true;
    tx.ccp_tx_method = PPP_CCP_METHOD_MPPC;
    rx.state = PPP_STATE_NETWORK;
    rx.modem = &rx_wire;
    rx.serial_push = capture_serial;
    rx.network_send_ip = capture_network;
    rx.ccp_open = true;
    rx.ccp_rx_method = PPP_CCP_METHOD_MPPC;
    ASSERT_TRUE(ppp_ccp_codec_set(&tx, true, PPP_CCP_METHOD_MPPC));
    ASSERT_TRUE(ppp_ccp_codec_set(&rx, false, PPP_CCP_METHOD_MPPC));

    send_frame(&rx, { 0xA0, 0x00, 0x00, 0x80 }, false, PPP_PROTO_MPPE);
    ASSERT_TRUE(rx.ccp_reset_pending);
    uint16_t protocol = 0;
    std::vector<uint8_t> reset_request = decode_captured_frame(rx_wire, protocol);
    ASSERT_EQ(protocol, PPP_PROTO_CCP);
    ASSERT_EQ(reset_request[0], PPP_CODE_RESET_REQUEST);

    tx_wire.clear();
    send_frame(&tx, reset_request, false, PPP_PROTO_CCP);
    std::vector<uint8_t> reset_ack = decode_captured_frame(tx_wire, protocol);
    ASSERT_EQ(protocol, PPP_PROTO_CCP);
    ASSERT_EQ(reset_ack[0], PPP_CODE_RESET_ACK);

    rx_wire.clear();
    std::vector<uint8_t> stale_reset_ack = reset_ack;
    stale_reset_ack[1]++;
    send_frame(&rx, stale_reset_ack, false, PPP_PROTO_CCP);
    EXPECT_TRUE(rx.ccp_reset_pending);

    send_frame(&rx, reset_ack, false, PPP_PROTO_CCP);
    EXPECT_FALSE(rx.ccp_reset_pending);
    rx.modem = &received;
    rx.serial_push = discard_serial;

    const std::vector<uint8_t> ip_packet = { 0x45, 0x00, 0x00, 0x04 };
    tx_wire.clear();
    ppp_send_frame(&tx, PPP_PROTO_IP, ip_packet.data(), static_cast<int>(ip_packet.size()));
    std::vector<uint8_t> compressed = decode_captured_frame(tx_wire, protocol);
    ASSERT_EQ(protocol, PPP_PROTO_MPPE);
    EXPECT_NE(compressed[0] & 0x80, 0);
    send_frame(&rx, compressed, false, PPP_PROTO_MPPE);
    EXPECT_EQ(received, ip_packet);

    ppp_ccp_codec_close(&tx);
    ppp_ccp_codec_close(&rx);
}

TEST(ModemPpp, DoesNotSendPlaintextBeforeCcpOpens)
{
    ppp_ctx_t ctx = make_context();
    std::vector<uint8_t> wire;
    ctx.state = PPP_STATE_NETWORK;
    ctx.modem = &wire;
    ctx.serial_push = capture_serial;
    ctx.mppe_keys_ready = true;
    ctx.mppe_min_bits = 128;
    const std::vector<uint8_t> ip_packet = { 0x45, 0x00, 0x00, 0x04 };

    ppp_send_frame(&ctx, PPP_PROTO_IP, ip_packet.data(), static_cast<int>(ip_packet.size()));

    EXPECT_TRUE(wire.empty());
}

TEST(ModemPpp, RequiredMppeNeedsMschapV2)
{
    ppp_ctx_t ctx = make_context();
    ctx.mppe_min_bits = 40;
    ctx.lcp_req_sent = false;

    ppp_start(&ctx);

    EXPECT_EQ(ctx.state, PPP_STATE_DEAD);
    EXPECT_FALSE(ctx.lcp_req_sent);
}

TEST(ModemPpp, FallsBackToPlaintextWhenPeerRejectsCcp)
{
    ppp_ctx_t ctx = make_context();
    std::vector<uint8_t> wire;
    ctx.state = PPP_STATE_IPCP_NEGOTIATE;
    ctx.ipcp_ack_sent = true;
    ctx.ipcp_ack_received = true;
    ctx.mppe_keys_ready = true;
    ctx.modem = &wire;
    ctx.serial_push = capture_serial;
    const std::vector<uint8_t> reject = {
        PPP_CODE_PROTOCOL_REJECT, 2, 0, 6,
        static_cast<uint8_t>(PPP_PROTO_CCP >> 8),
        static_cast<uint8_t>(PPP_PROTO_CCP)
    };

    send_frame(&ctx, reject);

    EXPECT_TRUE(ctx.ccp_plaintext_fallback);
    EXPECT_EQ(ctx.state, PPP_STATE_NETWORK);
    wire.clear();
    const std::vector<uint8_t> ip_packet = { 0x45, 0x00, 0x00, 0x04 };
    ppp_send_frame(&ctx, PPP_PROTO_IP, ip_packet.data(), static_cast<int>(ip_packet.size()));
    uint16_t protocol = 0;
    EXPECT_EQ(decode_captured_frame(wire, protocol), ip_packet);
    EXPECT_EQ(protocol, PPP_PROTO_IP);
}

static ppp_ctx_t
make_context()
{
    ppp_ctx_t ctx{};
    ctx.state = PPP_STATE_LCP_NEGOTIATE;
    ctx.our_mru = PPP_DEFAULT_MRU;
    ctx.peer_mru = PPP_DEFAULT_MRU;
    ctx.auth_type = PPP_AUTH_CHAP_MD5;
    ctx.serial_push = discard_serial;
    ctx.lcp_req_sent = true;
    ctx.lcp_peer_heard = true; /* keep each reply the only frame captured */
    ctx.lcp_request_len = 9;
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 7, 0, 9,
                                LCP_OPT_AUTH_PROTO, 5, 0xC2, 0x23, CHAP_ALG_MD5 };
    std::memcpy(ctx.lcp_request, request, sizeof(request));
    return ctx;
}

TEST(ModemPpp, LcpIdentificationIsLoggedWithoutReply)
{
    ppp_ctx_t ctx = make_context();
    std::vector<uint8_t> wire;
    ctx.state = PPP_STATE_NETWORK;
    ctx.modem = &wire;
    ctx.serial_push = capture_serial;
    const std::vector<uint8_t> identification = {
        PPP_CODE_IDENTIFICATION, 1, 0, 13, 0x12, 0x34, 0x56, 0x78, 't', 'e', 's', 't', '\n'
    };

    send_frame(&ctx, identification);

    EXPECT_TRUE(wire.empty());
    EXPECT_EQ(ctx.state, PPP_STATE_NETWORK);
}

TEST(ModemPpp, LcpRetriesConfigureRequestAndStopsAfterLimit)
{
    ppp_ctx_t ctx = make_context();
    std::vector<uint8_t> wire;
    ctx.serial_push = capture_serial;
    ctx.modem = &wire;

    for (int retry = 0; retry < 11; retry++) {
        for (int millisecond = 0; millisecond < 3000; millisecond++)
            ppp_timer_tick(&ctx);
        if (retry < 10) {
            EXPECT_EQ(ctx.state, PPP_STATE_LCP_NEGOTIATE);
        }
    }

    EXPECT_EQ(ctx.state, PPP_STATE_DEAD);
    EXPECT_FALSE(ctx.lcp_req_sent);
    EXPECT_EQ(ctx.lcp_retries, 11);
    EXPECT_FALSE(wire.empty());
}

TEST(ModemPpp, AuthenticationTimesOutWhenPeerDoesNotRespond)
{
    ppp_ctx_t ctx = make_context();
    ctx.state = PPP_STATE_AUTH;

    for (int millisecond = 0; millisecond < 29999; millisecond++)
        ppp_timer_tick(&ctx);
    EXPECT_EQ(ctx.state, PPP_STATE_AUTH);

    ppp_timer_tick(&ctx);
    EXPECT_EQ(ctx.state, PPP_STATE_DEAD);
}

TEST(ModemPpp, ConfigureNakFallsBackInConfiguredOrder)
{
    ppp_ctx_t ctx = make_context();
    ctx.auth_type = PPP_AUTH_MSCHAPV2;
    ctx.lcp_request[8] = CHAP_ALG_MSCHAPV2;
    auto send_nak = [&ctx](const std::vector<uint8_t> &option) {
        std::vector<uint8_t> nak = { PPP_CODE_CONFIGURE_NAK, ctx.lcp_request[1], 0, 0 };
        nak.insert(nak.end(), option.begin(), option.end());
        nak[2] = static_cast<uint8_t>(nak.size() >> 8);
        nak[3] = static_cast<uint8_t>(nak.size());
        send_frame(&ctx, nak);
    };

    send_nak({ LCP_OPT_AUTH_PROTO, 5, 0xC2, 0x23, CHAP_ALG_MSCHAP });
    EXPECT_EQ(ctx.state, PPP_STATE_LCP_NEGOTIATE);
    EXPECT_EQ(ctx.auth_type, PPP_AUTH_MSCHAP);
    EXPECT_EQ(ctx.lcp_request[18], CHAP_ALG_MSCHAP);

    send_nak({ LCP_OPT_AUTH_PROTO, 5, 0xC2, 0x23, CHAP_ALG_MD5 });
    EXPECT_EQ(ctx.auth_type, PPP_AUTH_CHAP_MD5);
    EXPECT_EQ(ctx.lcp_request[18], CHAP_ALG_MD5);

    send_nak({ LCP_OPT_AUTH_PROTO, 4, 0xC0, 0x23 });
    EXPECT_EQ(ctx.state, PPP_STATE_LCP_NEGOTIATE);
    EXPECT_EQ(ctx.auth_type, PPP_AUTH_PAP);
    EXPECT_EQ(ctx.lcp_request[15], 4);
    EXPECT_EQ(ctx.lcp_request[16], 0xC0);
    EXPECT_EQ(ctx.lcp_request[17], 0x23);
}

TEST(ModemPpp, ConfigureNakFallsBackThroughChapHashOrder)
{
    ppp_ctx_t ctx = make_context();
    ctx.auth_type = PPP_AUTH_CHAP_SHA512;
    ctx.lcp_request[8] = CHAP_ALG_SHA512;
    auto send_nak = [&ctx](const std::vector<uint8_t> &option) {
        std::vector<uint8_t> nak = { PPP_CODE_CONFIGURE_NAK, ctx.lcp_request[1], 0, 0 };
        nak.insert(nak.end(), option.begin(), option.end());
        nak[2] = static_cast<uint8_t>(nak.size() >> 8);
        nak[3] = static_cast<uint8_t>(nak.size());
        send_frame(&ctx, nak);
    };

    send_nak({ LCP_OPT_AUTH_PROTO, 5, 0xC2, 0x23, CHAP_ALG_SHA384 });
    EXPECT_EQ(ctx.auth_type, PPP_AUTH_CHAP_SHA384);
    EXPECT_EQ(ctx.lcp_request[18], CHAP_ALG_SHA384);

    send_nak({ LCP_OPT_AUTH_PROTO, 5, 0xC2, 0x23, CHAP_ALG_SHA256 });
    EXPECT_EQ(ctx.auth_type, PPP_AUTH_CHAP_SHA256);
    EXPECT_EQ(ctx.lcp_request[18], CHAP_ALG_SHA256);

    send_nak({ LCP_OPT_AUTH_PROTO, 5, 0xC2, 0x23, CHAP_ALG_SHA1 });
    EXPECT_EQ(ctx.auth_type, PPP_AUTH_CHAP_SHA1);
    EXPECT_EQ(ctx.lcp_request[18], CHAP_ALG_SHA1);

    send_nak({ LCP_OPT_AUTH_PROTO, 5, 0xC2, 0x23, CHAP_ALG_MD5 });
    EXPECT_EQ(ctx.auth_type, PPP_AUTH_CHAP_MD5);
    EXPECT_EQ(ctx.lcp_request[18], CHAP_ALG_MD5);

    send_nak({ LCP_OPT_AUTH_PROTO, 4, 0xC0, 0x23 });
    EXPECT_EQ(ctx.state, PPP_STATE_LCP_NEGOTIATE);
    EXPECT_EQ(ctx.auth_type, PPP_AUTH_PAP);
    EXPECT_EQ(ctx.lcp_request[15], 4);
}

TEST(ModemPpp, ConfigureNakCannotDowngradeWhenMppeIsRequired)
{
    ppp_ctx_t ctx = make_context();
    ctx.auth_type = PPP_AUTH_MSCHAPV2;
    ctx.mppe_min_bits = 40;
    ctx.lcp_request[8] = CHAP_ALG_MSCHAPV2;
    const std::vector<uint8_t> nak = { PPP_CODE_CONFIGURE_NAK, 7, 0, 9,
                                       LCP_OPT_AUTH_PROTO, 5, 0xC2, 0x23,
                                       CHAP_ALG_MSCHAP };

    send_frame(&ctx, nak);

    EXPECT_EQ(ctx.state, PPP_STATE_DEAD);
    EXPECT_EQ(ctx.auth_type, PPP_AUTH_MSCHAPV2);
}

TEST(ModemPpp, ConfigureRejectFallsBackFromChapMd5ToPap)
{
    ppp_ctx_t ctx = make_context();
    const std::vector<uint8_t> reject = { PPP_CODE_CONFIGURE_REJECT, 7, 0, 9,
                                          LCP_OPT_AUTH_PROTO, 5, 0xC2, 0x23, CHAP_ALG_MD5 };

    send_frame(&ctx, reject);

    EXPECT_EQ(ctx.state, PPP_STATE_LCP_NEGOTIATE);
    EXPECT_EQ(ctx.auth_type, PPP_AUTH_PAP);
    EXPECT_TRUE(ctx.lcp_req_sent);
    EXPECT_FALSE(ctx.auth_complete);
}

TEST(ModemPpp, ConfigureRejectCannotDowngradeWhenMppeIsRequired)
{
    ppp_ctx_t ctx = make_context();
    ctx.auth_type = PPP_AUTH_MSCHAPV2;
    ctx.mppe_min_bits = 40;
    ctx.lcp_request[8] = CHAP_ALG_MSCHAPV2;
    const std::vector<uint8_t> reject = { PPP_CODE_CONFIGURE_REJECT, 7, 0, 9,
                                          LCP_OPT_AUTH_PROTO, 5, 0xC2, 0x23,
                                          CHAP_ALG_MSCHAPV2 };

    send_frame(&ctx, reject);

    EXPECT_EQ(ctx.state, PPP_STATE_DEAD);
    EXPECT_EQ(ctx.auth_type, PPP_AUTH_MSCHAPV2);
}

TEST(ModemPpp, ConfigureRejectCannotDisablePapAuthentication)
{
    ppp_ctx_t ctx = make_context();
    ctx.auth_type = PPP_AUTH_PAP;
    ctx.lcp_request_len = 8;
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 7, 0, 8,
                                LCP_OPT_AUTH_PROTO, 4, 0xC0, 0x23 };
    std::memcpy(ctx.lcp_request, request, sizeof(request));
    const std::vector<uint8_t> reject = { PPP_CODE_CONFIGURE_REJECT, 7, 0, 8,
                                          LCP_OPT_AUTH_PROTO, 4, 0xC0, 0x23 };

    send_frame(&ctx, reject);

    EXPECT_EQ(ctx.state, PPP_STATE_DEAD);
    EXPECT_EQ(ctx.auth_type, PPP_AUTH_PAP);
}

TEST(ModemPpp, StartsOptionalCcpAfterPapWithoutMppeKeys)
{
    ppp_ctx_t ctx = make_context();
    std::vector<uint8_t> wire;
    ctx.state = PPP_STATE_AUTH;
    ctx.auth_type = PPP_AUTH_PAP;
    ctx.auth_complete = true;
    ctx.ppp_compression = PPP_COMPRESSION_PREFER_MPPC;
    ctx.modem = &wire;
    ctx.serial_push = capture_serial;

    ppp_advance_state(&ctx);

    EXPECT_EQ(ctx.state, PPP_STATE_IPCP_NEGOTIATE);
    EXPECT_FALSE(ctx.mppe_keys_ready);
    EXPECT_TRUE(ctx.ccp_req_sent);
    EXPECT_EQ(ctx.ccp_request_method, PPP_CCP_METHOD_MPPC);
}

TEST(ModemPpp, IgnoresStaleConfigureNak)
{
    ppp_ctx_t ctx = make_context();
    const std::vector<uint8_t> nak = { PPP_CODE_CONFIGURE_NAK, 8, 0, 8,
                                       LCP_OPT_AUTH_PROTO, 4, 0xC0, 0x23 };

    send_frame(&ctx, nak);

    EXPECT_EQ(ctx.state, PPP_STATE_LCP_NEGOTIATE);
    EXPECT_EQ(ctx.auth_type, PPP_AUTH_CHAP_MD5);
}

TEST(ModemPpp, IgnoresFrameWithInvalidFcs)
{
    ppp_ctx_t ctx = make_context();
    const std::vector<uint8_t> nak = { PPP_CODE_CONFIGURE_NAK, 7, 0, 8,
                                       LCP_OPT_AUTH_PROTO, 4, 0xC0, 0x23 };

    send_frame(&ctx, nak, true);

    EXPECT_EQ(ctx.state, PPP_STATE_LCP_NEGOTIATE);
    EXPECT_TRUE(ctx.lcp_req_sent);
    EXPECT_FALSE(ctx.lcp_ack_received);
}

TEST(ModemPpp, RejectedPeerRequestClearsPreviousAck)
{
    ppp_ctx_t ctx = make_context();
    ctx.lcp_ack_sent = true;
    ctx.lcp_ack_received = true;
    const std::vector<uint8_t> request = { PPP_CODE_CONFIGURE_REQUEST, 3, 0, 8,
                                           LCP_OPT_AUTH_PROTO, 4, 0xC0, 0x23 };

    send_frame(&ctx, request);

    EXPECT_EQ(ctx.state, PPP_STATE_LCP_NEGOTIATE);
    EXPECT_FALSE(ctx.lcp_ack_sent);
}

TEST(ModemPpp, RejectsEndpointDiscriminatorWithoutMultilink)
{
    ppp_ctx_t ctx = make_context();
    std::vector<uint8_t> wire;
    ctx.modem = &wire;
    ctx.serial_push = capture_serial;
    const std::vector<uint8_t> request = {
        PPP_CODE_CONFIGURE_REQUEST, 3, 0, 8,
        LCP_OPT_ENDPOINT_DISC, 4, 1, 0x42
    };

    send_frame(&ctx, request);

    uint16_t protocol = 0;
    const std::vector<uint8_t> response = decode_captured_frame(wire, protocol);
    ASSERT_EQ(protocol, PPP_PROTO_LCP);
    EXPECT_EQ(response, (std::vector<uint8_t> {
        PPP_CODE_CONFIGURE_REJECT, 3, 0, 8,
        LCP_OPT_ENDPOINT_DISC, 4, 1, 0x42
    }));
}

TEST(ModemPpp, NaksZeroOrLoopedBackMagicNumber)
{
    constexpr uint32_t local_magic = 0x12345678;
    const uint32_t invalid_magic[] = { 0, local_magic };

    for (size_t index = 0; index < std::size(invalid_magic); index++) {
        ppp_ctx_t ctx = make_context();
        std::vector<uint8_t> wire;
        ctx.modem = &wire;
        ctx.serial_push = capture_serial;
        ctx.our_magic = local_magic;
        const uint32_t magic = invalid_magic[index];
        const std::vector<uint8_t> request = {
            PPP_CODE_CONFIGURE_REQUEST, static_cast<uint8_t>(index + 1), 0, 10,
            LCP_OPT_MAGIC_NUMBER, 6,
            static_cast<uint8_t>(magic >> 24), static_cast<uint8_t>(magic >> 16),
            static_cast<uint8_t>(magic >> 8), static_cast<uint8_t>(magic)
        };

        send_frame(&ctx, request);

        uint16_t protocol = 0;
        const std::vector<uint8_t> response = decode_captured_frame(wire, protocol);
        ASSERT_EQ(protocol, PPP_PROTO_LCP);
        ASSERT_EQ(response.size(), 10u);
        EXPECT_EQ(response[0], PPP_CODE_CONFIGURE_NAK);
        EXPECT_EQ(response[4], LCP_OPT_MAGIC_NUMBER);
        const uint32_t suggested_magic = (static_cast<uint32_t>(response[6]) << 24)
                                       | (static_cast<uint32_t>(response[7]) << 16)
                                       | (static_cast<uint32_t>(response[8]) << 8)
                                       | response[9];
        EXPECT_NE(suggested_magic, 0u);
        EXPECT_NE(suggested_magic, local_magic);
    }

    ppp_ctx_t ctx = make_context();
    std::vector<uint8_t> wire;
    ctx.modem = &wire;
    ctx.serial_push = capture_serial;
    ctx.our_magic = local_magic;
    const uint32_t valid_magic = 0x87654321;
    const std::vector<uint8_t> request = {
        PPP_CODE_CONFIGURE_REQUEST, 3, 0, 10,
        LCP_OPT_MAGIC_NUMBER, 6,
        static_cast<uint8_t>(valid_magic >> 24), static_cast<uint8_t>(valid_magic >> 16),
        static_cast<uint8_t>(valid_magic >> 8), static_cast<uint8_t>(valid_magic)
    };

    send_frame(&ctx, request);

    uint16_t protocol = 0;
    const std::vector<uint8_t> response = decode_captured_frame(wire, protocol);
    ASSERT_EQ(protocol, PPP_PROTO_LCP);
    ASSERT_EQ(response.size(), 10u);
    EXPECT_EQ(response[0], PPP_CODE_CONFIGURE_ACK);
    EXPECT_EQ(ctx.peer_magic, valid_magic);
}

TEST(ModemPpp, IgnoresConfigureNakAfterAck)
{
    ppp_ctx_t ctx = make_context();
    std::vector<uint8_t> ack(ctx.lcp_request, ctx.lcp_request + ctx.lcp_request_len);
    ack[0] = PPP_CODE_CONFIGURE_ACK;
    send_frame(&ctx, ack);
    ASSERT_TRUE(ctx.lcp_ack_received);
    ASSERT_FALSE(ctx.lcp_req_sent);

    const std::vector<uint8_t> nak = { PPP_CODE_CONFIGURE_NAK, 7, 0, 8,
                                       LCP_OPT_AUTH_PROTO, 4, 0xC0, 0x23 };
    send_frame(&ctx, nak);

    EXPECT_EQ(ctx.auth_type, PPP_AUTH_CHAP_MD5);
    EXPECT_TRUE(ctx.lcp_ack_received);
    EXPECT_FALSE(ctx.lcp_req_sent);
}

TEST(ModemPpp, AppliesConfigureNakSuggestions)
{
    ppp_ctx_t ctx = make_context();
    ctx.auth_type = PPP_AUTH_NONE;
    ctx.our_mru = 1500;
    ctx.our_accm = 0xFFFF0000;
    ctx.request_pfc = true;
    ctx.request_acfc = true;
    const uint8_t request[] = { PPP_CODE_CONFIGURE_REQUEST, 7, 0, 18,
                                LCP_OPT_MRU, 4, 0x05, 0xDC,
                                LCP_OPT_ACCM, 6, 0xFF, 0xFF, 0, 0,
                                LCP_OPT_PFC, 2, LCP_OPT_ACFC, 2 };
    std::memcpy(ctx.lcp_request, request, sizeof(request));
    ctx.lcp_request_len = sizeof(request);
    const std::vector<uint8_t> nak = { PPP_CODE_CONFIGURE_NAK, 7, 0, 18,
                                       LCP_OPT_MRU, 4, 0x05, 0x78,
                                       LCP_OPT_ACCM, 6, 0, 0, 0, 0x0F,
                                       LCP_OPT_PFC, 2, LCP_OPT_ACFC, 2 };

    send_frame(&ctx, nak);

    EXPECT_EQ(ctx.our_mru, 1400);
    EXPECT_EQ(ctx.our_accm, 0xFFFF000Fu);
    EXPECT_FALSE(ctx.request_pfc);
    EXPECT_FALSE(ctx.request_acfc);
    ASSERT_EQ(ctx.lcp_request_len, 20);
    EXPECT_EQ(ctx.lcp_request[4], LCP_OPT_MRU);
    EXPECT_EQ(ctx.lcp_request[5], 4);
    EXPECT_EQ(ctx.lcp_request[6], 0x05);
    EXPECT_EQ(ctx.lcp_request[7], 0x78);
    EXPECT_EQ(ctx.lcp_request[8], LCP_OPT_ACCM);
    EXPECT_EQ(ctx.lcp_request[14], LCP_OPT_MAGIC_NUMBER);
}

TEST(ModemPpp, NegotiatedNt31RasUsesRasEnvelopeForIpv4)
{
    ppp_ctx_t tx = make_context();
    ppp_ctx_t rx = make_context();
    std::vector<uint8_t> wire;
    std::vector<uint8_t> received;
    std::vector<uint8_t> packet(128);
    tx.state = PPP_STATE_NETWORK;
    tx.modem = &wire;
    tx.serial_push = capture_serial;
    tx.ccp_open = true;
    tx.ccp_tx_method = PPP_CCP_METHOD_NT31RAS;
    rx.state = PPP_STATE_NETWORK;
    rx.modem = &received;
    rx.network_send_ip = capture_network;
    rx.ccp_open = true;
    rx.ccp_rx_method = PPP_CCP_METHOD_NT31RAS;
    for (size_t index = 0; index < packet.size(); index++)
        packet[index] = static_cast<uint8_t>((index / 8) % 4);
    ASSERT_TRUE(ppp_ccp_codec_set(&tx, true, PPP_CCP_METHOD_NT31RAS));
    ASSERT_TRUE(ppp_ccp_codec_set(&rx, false, PPP_CCP_METHOD_NT31RAS));

    for (int frame_index = 0; frame_index < 2; frame_index++) {
        wire.clear();
        ppp_send_frame(&tx, PPP_PROTO_IP, packet.data(), static_cast<int>(packet.size()));

        ASSERT_GT(wire.size(), 10u);
        EXPECT_EQ(wire[0], PPP_RAS_SYN);
        EXPECT_EQ(wire[1], PPP_RAS_SOH_DEST | PPP_RAS_SOH_TYPE | PPP_RAS_SOH_COMPRESS);
        EXPECT_EQ(wire[4], 0x08);
        EXPECT_EQ(wire[5], 0x00);
        for (uint8_t byte : wire)
            ppp_rx_byte(&rx, byte);
        EXPECT_EQ(received, packet);
    }
}

TEST(ModemPpp, Nt31RasFlushesAndRecoversAfterTicketGap)
{
    ppp_ctx_t tx = make_context();
    ppp_ctx_t rx = make_context();
    std::vector<uint8_t> tx_wire;
    ras_capture_t rx_capture;
    std::vector<uint8_t> packet(128);
    tx.state = PPP_STATE_NETWORK;
    tx.modem = &tx_wire;
    tx.serial_push = capture_serial;
    tx.ccp_open = true;
    tx.ccp_tx_method = PPP_CCP_METHOD_NT31RAS;
    tx.ccp_rx_method = PPP_CCP_METHOD_NT31RAS;
    rx.state = PPP_STATE_NETWORK;
    rx.modem = &rx_capture;
    rx.serial_push = capture_ras_serial;
    rx.network_send_ip = capture_ras_network;
    rx.ccp_open = true;
    rx.ccp_rx_method = PPP_CCP_METHOD_NT31RAS;
    ASSERT_TRUE(ppp_ccp_codec_set(&tx, true, PPP_CCP_METHOD_NT31RAS));
    ASSERT_TRUE(ppp_ccp_codec_set(&tx, false, PPP_CCP_METHOD_NT31RAS));
    ASSERT_TRUE(ppp_ccp_codec_set(&rx, false, PPP_CCP_METHOD_NT31RAS));
    for (size_t index = 0; index < packet.size(); index++)
        packet[index] = static_cast<uint8_t>((index / 8) % 4);

    ppp_send_frame(&tx, PPP_PROTO_IP, packet.data(), static_cast<int>(packet.size()));
    for (uint8_t byte : tx_wire)
        ppp_rx_byte(&rx, byte);
    ASSERT_EQ(rx_capture.packets.size(), 1u);

    tx_wire.clear();
    ppp_send_frame(&tx, PPP_PROTO_IP, packet.data(), static_cast<int>(packet.size()));
    tx_wire.back() ^= 1;
    for (uint8_t byte : tx_wire)
        ppp_rx_byte(&rx, byte);
    EXPECT_EQ(rx_capture.packets.size(), 1u);

    tx_wire.clear();
    ppp_send_frame(&tx, PPP_PROTO_IP, packet.data(), static_cast<int>(packet.size()));
    for (uint8_t byte : tx_wire)
        ppp_rx_byte(&rx, byte);
    ASSERT_EQ(rx_capture.serial.size(), 10u);
    EXPECT_EQ(rx_capture.serial[6], 0xFF);
    for (uint8_t byte : rx_capture.serial)
        ppp_rx_byte(&tx, byte);

    tx_wire.clear();
    ppp_send_frame(&tx, PPP_PROTO_IP, packet.data(), static_cast<int>(packet.size()));
    ASSERT_EQ(tx_wire[6], 32);
    for (uint8_t byte : tx_wire)
        ppp_rx_byte(&rx, byte);
    ASSERT_EQ(rx_capture.packets.size(), 2u);
    EXPECT_EQ(rx_capture.packets.back(), packet);
}

static std::vector<uint8_t>
lcp_option_types(const std::vector<uint8_t> &wire, size_t frame_start = 0)
{
    uint16_t protocol = 0;
    std::vector<uint8_t> frame(wire.begin() + frame_start, wire.end());
    std::vector<uint8_t> packet = decode_captured_frame(frame, protocol);
    std::vector<uint8_t> types;
    for (size_t pos = 4; pos + 2 <= packet.size() && packet[pos + 1] >= 2; pos += packet[pos + 1])
        types.push_back(packet[pos]);
    return types;
}

static bool
has_option(const std::vector<uint8_t> &types, uint8_t type)
{
    return std::find(types.begin(), types.end(), type) != types.end();
}

/* Splits the capture into frames and returns the last Configure-Request sent. */
static std::vector<uint8_t>
last_lcp_request(const std::vector<uint8_t> &wire)
{
    std::vector<uint8_t> result;
    size_t start = 0;
    while (start < wire.size()) {
        size_t end = start + 1;
        while (end < wire.size() && wire[end] != PPP_FLAG)
            end++;
        if (end >= wire.size())
            break;
        std::vector<uint8_t> frame(wire.begin() + start, wire.begin() + end + 1);
        uint16_t protocol = 0;
        std::vector<uint8_t> packet = decode_captured_frame(frame, protocol);
        if (protocol == PPP_PROTO_LCP && !packet.empty()
            && packet[0] == PPP_CODE_CONFIGURE_REQUEST)
            result = lcp_option_types(frame);
        start = end + 1;
    }
    return result;
}

static ppp_ctx_t *
start_capturing(std::vector<uint8_t> *wire)
{
    ppp_ctx_t *ctx = ppp_init(wire, nullptr, capture_serial, capture_network);
    return ctx;
}

TEST(ModemPppAuto, BlankCredentialsMeanNoAuthentication)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_capturing(&wire);
    ASSERT_NE(ctx, nullptr);
    ctx->auth_type = PPP_AUTH_MSCHAPV2;

    ppp_start(ctx);

    EXPECT_EQ(ctx->auth_type, PPP_AUTH_NONE);
    EXPECT_FALSE(has_option(last_lcp_request(wire), LCP_OPT_AUTH_PROTO));
    ppp_close(ctx);
}

TEST(ModemPppAuto, BlankCredentialsKeepPapAndRequiredMppe)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *pap = start_capturing(&wire);
    ASSERT_NE(pap, nullptr);
    pap->auth_type = PPP_AUTH_PAP;
    ppp_start(pap);
    EXPECT_EQ(pap->auth_type, PPP_AUTH_PAP);
    ppp_close(pap);

    ppp_ctx_t *mppe = start_capturing(&wire);
    ASSERT_NE(mppe, nullptr);
    mppe->auth_type = PPP_AUTH_MSCHAPV2;
    ppp_mppe_set_policy(mppe, CCP_MPPE_REQUIRED_ANY, PPP_MPPE_FORMAT_AUTOMATIC);
    ppp_start(mppe);
    EXPECT_EQ(mppe->auth_type, PPP_AUTH_MSCHAPV2);
    EXPECT_EQ(mppe->state, PPP_STATE_LCP_NEGOTIATE);
    ppp_close(mppe);
}

TEST(ModemPppAuto, AutomaticAuthenticationWalksTheWholeChain)
{
    const ppp_auth_type_t expected[] = { PPP_AUTH_MSCHAP, PPP_AUTH_CHAP_SHA512, PPP_AUTH_CHAP_SHA384,
                                         PPP_AUTH_CHAP_SHA256, PPP_AUTH_CHAP_SHA1, PPP_AUTH_CHAP_MD5,
                                         PPP_AUTH_EAP, PPP_AUTH_PAP };
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_capturing(&wire);
    ASSERT_NE(ctx, nullptr);
    ctx->auth_type = PPP_AUTH_MSCHAPV2;
    ctx->auth_auto = true;
    strcpy(ctx->password, "secret");
    ppp_start(ctx);

    for (ppp_auth_type_t next : expected) {
        const std::vector<uint8_t> reject = { PPP_CODE_CONFIGURE_REJECT,
                                              static_cast<uint8_t>(ctx->lcp_request[1]), 0, 7,
                                              LCP_OPT_AUTH_PROTO, 3, 0 };
        std::vector<uint8_t> full = reject;
        /* A Reject must carry the option exactly as we sent it. */
        for (int pos = 4; pos < ctx->lcp_request_len; pos += ctx->lcp_request[pos + 1]) {
            if (ctx->lcp_request[pos] == LCP_OPT_AUTH_PROTO) {
                full.assign({ PPP_CODE_CONFIGURE_REJECT, ctx->lcp_request[1], 0,
                              static_cast<uint8_t>(4 + ctx->lcp_request[pos + 1]) });
                full.insert(full.end(), ctx->lcp_request + pos,
                            ctx->lcp_request + pos + ctx->lcp_request[pos + 1]);
            }
        }
        send_frame(ctx, full);
        ASSERT_EQ(ctx->auth_type, next);
        ASSERT_EQ(ctx->state, PPP_STATE_LCP_NEGOTIATE);
    }
    ppp_close(ctx);
}

TEST(ModemPppAuto, ForcedAuthenticationKeepsItsShortChain)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_capturing(&wire);
    ASSERT_NE(ctx, nullptr);
    ctx->auth_type = PPP_AUTH_MSCHAP;
    strcpy(ctx->password, "secret");
    ppp_start(ctx);
    const std::vector<uint8_t> reject = { PPP_CODE_CONFIGURE_REJECT, ctx->lcp_request[1], 0, 9,
                                          LCP_OPT_AUTH_PROTO, 5, 0xC2, 0x23, CHAP_ALG_MSCHAP };

    send_frame(ctx, reject);

    EXPECT_EQ(ctx->auth_type, PPP_AUTH_CHAP_MD5);
    ppp_close(ctx);
}

TEST(ModemPppAuto, DisabledHeaderCompressionRefusesThePeer)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_capturing(&wire);
    ASSERT_NE(ctx, nullptr);
    ctx->ppp_pfc_mode  = PPP_LCP_COMPRESSION_DISABLED;
    ctx->ppp_acfc_mode = PPP_LCP_COMPRESSION_DISABLED;
    ctx->request_pfc   = false;
    ctx->request_acfc  = false;
    ppp_start(ctx);
    wire.clear();
    const std::vector<uint8_t> request = { PPP_CODE_CONFIGURE_REQUEST, 1, 0, 8,
                                           LCP_OPT_PFC, 2, LCP_OPT_ACFC, 2 };

    send_frame(ctx, request);

    uint16_t protocol = 0;
    std::vector<uint8_t> reply = decode_captured_frame(wire, protocol);
    ASSERT_FALSE(reply.empty());
    EXPECT_EQ(reply[0], PPP_CODE_CONFIGURE_REJECT);
    EXPECT_FALSE(ctx->peer_pfc);
    EXPECT_FALSE(ctx->peer_acfc);
    ppp_close(ctx);
}

TEST(ModemPppAuto, OfferedHeaderCompressionFollowsThePeer)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_capturing(&wire);
    ASSERT_NE(ctx, nullptr);
    ppp_start(ctx);
    const std::vector<uint8_t> request = { PPP_CODE_CONFIGURE_REQUEST, 1, 0, 8,
                                           LCP_OPT_PFC, 2, LCP_OPT_ACFC, 2 };

    send_frame(ctx, request);

    EXPECT_TRUE(ctx->peer_pfc);
    EXPECT_TRUE(ctx->peer_acfc);
    ppp_close(ctx);
}

static const std::vector<uint8_t> numbered_request = {
    PPP_CODE_CONFIGURE_REQUEST, 1, 0, 14,
    LCP_OPT_MAGIC_NUMBER, 6, 0x12, 0x34, 0x56, 0x78,
    LCP_OPT_NUMBERED_MODE, 4, 3, 3
};

TEST(ModemPppAuto, LargeNumberedWindowAdaptsToModuloEightPeer)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_capturing(&wire);
    ASSERT_NE(ctx, nullptr);
    ctx->ppp_numbered_mode   = PPP_NUMBERED_ENABLED;
    ctx->ppp_numbered_window = 15;
    ppp_start(ctx);
    ASSERT_EQ(ctx->nm_our_window, 15);

    send_frame(ctx, numbered_request);

    EXPECT_TRUE(ctx->lcp_ack_sent);
    EXPECT_TRUE(ctx->nm_peer_active);
    EXPECT_EQ(ctx->nm_our_window, 7);
    ppp_close(ctx);
}

TEST(ModemPppAuto, AutomaticNumberedModeWaitsForThePeer)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_capturing(&wire);
    ASSERT_NE(ctx, nullptr);
    ctx->ppp_numbered_mode = PPP_NUMBERED_AUTOMATIC;
    ppp_start(ctx);
    EXPECT_FALSE(has_option(last_lcp_request(wire), LCP_OPT_NUMBERED_MODE));

    send_frame(ctx, numbered_request);

    EXPECT_TRUE(ctx->lcp_ack_sent);
    EXPECT_TRUE(ctx->nm_peer_active);
    EXPECT_TRUE(has_option(last_lcp_request(wire), LCP_OPT_NUMBERED_MODE));
    EXPECT_FALSE(has_option(last_lcp_request(wire), LCP_OPT_ACFC));
    ppp_close(ctx);
}

TEST(ModemPppAuto, DisabledNumberedModeStillRejects)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_capturing(&wire);
    ASSERT_NE(ctx, nullptr);
    ppp_start(ctx);

    send_frame(ctx, numbered_request);

    EXPECT_FALSE(ctx->lcp_ack_sent);
    EXPECT_FALSE(has_option(last_lcp_request(wire), LCP_OPT_NUMBERED_MODE));
    ppp_close(ctx);
}

TEST(ModemPppAuto, AutomaticCompoundFramesWaitForThePeer)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_capturing(&wire);
    ASSERT_NE(ctx, nullptr);
    ctx->ppp_compound_mode = PPP_COMPOUND_AUTOMATIC;
    ppp_start(ctx);
    EXPECT_FALSE(has_option(last_lcp_request(wire), LCP_OPT_COMPOUND_FRAMES));
    const std::vector<uint8_t> request = { PPP_CODE_CONFIGURE_REQUEST, 1, 0, 9,
                                           LCP_OPT_SELF_DESCRIBING_PAD, 3, 8,
                                           LCP_OPT_COMPOUND_FRAMES, 2 };

    send_frame(ctx, request);

    EXPECT_TRUE(ctx->lcp_ack_sent);
    EXPECT_TRUE(has_option(last_lcp_request(wire), LCP_OPT_COMPOUND_FRAMES));
    EXPECT_TRUE(has_option(last_lcp_request(wire), LCP_OPT_SELF_DESCRIBING_PAD));
    ppp_close(ctx);
}

TEST(ModemPppAuto, AcceptOnlyOptionsAreNeverRequested)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_capturing(&wire);
    ASSERT_NE(ctx, nullptr);
    ctx->ppp_fcs_mode = PPP_FCS_ACCEPT;
    ctx->ppp_sdp_mode = PPP_SDP_ACCEPT;
    ctx->ppp_lqm_mode = PPP_LQM_ACCEPT;
    ppp_start(ctx);

    auto types = last_lcp_request(wire);
    EXPECT_FALSE(has_option(types, LCP_OPT_FCS_ALTERNATIVES));
    EXPECT_FALSE(has_option(types, LCP_OPT_SELF_DESCRIBING_PAD));
    EXPECT_FALSE(has_option(types, LCP_OPT_QUALITY_PROTO));

    const std::vector<uint8_t> request = { PPP_CODE_CONFIGURE_REQUEST, 1, 0, 7,
                                           LCP_OPT_FCS_ALTERNATIVES, 3, 0x06 };
    send_frame(ctx, request);
    EXPECT_TRUE(ctx->lcp_ack_sent);
    EXPECT_TRUE(ctx->fcs_tx32);
    ppp_close(ctx);
}

TEST(ModemPppAuto, AutomaticMultilinkGroupsLinksOfOnePeer)
{
    const std::vector<uint8_t> request = { PPP_CODE_CONFIGURE_REQUEST, 1, 0, 15,
                                           LCP_OPT_MRRU, 4, 0x05, 0xDE,
                                           LCP_OPT_ENDPOINT_DISC, 7, 0, 1, 2, 3, 4 };
    std::vector<uint8_t> other = request;
    other[14] = 9;
    std::vector<uint8_t> wire_a, wire_b, wire_c;
    ppp_ctx_t *a = start_capturing(&wire_a);
    ppp_ctx_t *b = start_capturing(&wire_b);
    ppp_ctx_t *c = start_capturing(&wire_c);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(c, nullptr);
    for (ppp_ctx_t *ctx : { a, b, c }) {
        ctx->multilink_auto = true;
        ppp_start(ctx);
    }
    EXPECT_FALSE(has_option(last_lcp_request(wire_a), LCP_OPT_MRRU));

    send_frame(a, request);
    send_frame(b, request);
    send_frame(c, other);

    EXPECT_TRUE(a->lcp_ack_sent);
    EXPECT_TRUE(has_option(last_lcp_request(wire_a), LCP_OPT_MRRU));
    EXPECT_STRNE(a->multilink_group, "");
    EXPECT_STREQ(a->multilink_group, b->multilink_group);
    EXPECT_STRNE(a->multilink_group, c->multilink_group);
    ppp_close(a);
    ppp_close(b);
    ppp_close(c);
}

TEST(ModemPppAuto, MultilinkStaysOffWithoutAutomaticMode)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_capturing(&wire);
    ASSERT_NE(ctx, nullptr);
    ppp_start(ctx);
    const std::vector<uint8_t> request = { PPP_CODE_CONFIGURE_REQUEST, 1, 0, 8,
                                           LCP_OPT_MRRU, 4, 0x05, 0xDE };

    send_frame(ctx, request);

    EXPECT_STREQ(ctx->multilink_group, "");
    EXPECT_FALSE(ctx->lcp_ack_sent);
    ppp_close(ctx);
}

TEST(ModemPppAuto, AutomaticIpv6WaitsForThePeer)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_capturing(&wire);
    ASSERT_NE(ctx, nullptr);
    ctx->ppp_ipv6_mode = PPP_IPV6_AUTOMATIC;
    ppp_start(ctx);
    ctx->state = PPP_STATE_IPCP_NEGOTIATE;

    ppp_ipv6cp_start(ctx);
    EXPECT_FALSE(ctx->ipv6cp_active);

    const std::vector<uint8_t> request = { PPP_CODE_CONFIGURE_REQUEST, 1, 0, 14,
                                           1, 10, 1, 2, 3, 4, 5, 6, 7, 8 };
    send_frame(ctx, request, false, PPP_PROTO_IPV6CP);
    EXPECT_TRUE(ctx->ipv6cp_active);
    EXPECT_TRUE(ctx->ipv6cp_req_sent);
    ppp_close(ctx);
}

static std::vector<uint8_t> nbf_received;

static void
capture_nbf(void *priv, const uint8_t *frame, int len)
{
    (void) priv;
    nbf_received.assign(frame, frame + len);
}

/* Brings a link to the point a NetBEUI guest would have reached. */
static ppp_ctx_t *
start_nbf(std::vector<uint8_t> *wire)
{
    ppp_ctx_t *ctx = start_capturing(wire);

    ctx->network_send_nbf = capture_nbf;
    ppp_start(ctx);
    ctx->state = PPP_STATE_IPCP_NEGOTIATE;
    return ctx;
}

static std::vector<uint8_t> ipx_received;

static void
capture_ipx(void *modem, const uint8_t *packet, int len)
{
    (void) modem;
    ipx_received.assign(packet, packet + len);
}

static ppp_ctx_t *
start_ipx(std::vector<uint8_t> *wire)
{
    ppp_ctx_t *ctx = start_capturing(wire);

    ctx->network_send_ipx = capture_ipx;
    ppp_start(ctx);
    ctx->state = PPP_STATE_IPCP_NEGOTIATE;
    return ctx;
}

static std::vector<uint8_t>
ipxcp_request(uint8_t id, uint32_t network, const std::vector<uint8_t> &node)
{
    std::vector<uint8_t> r = { PPP_CODE_CONFIGURE_REQUEST, id, 0, 0, IPXCP_OPT_NETWORK, 6, uint8_t(network >> 24),
                               uint8_t(network >> 16), uint8_t(network >> 8), uint8_t(network), IPXCP_OPT_NODE, 8 };
    r.insert(r.end(), node.begin(), node.end());
    r[3] = uint8_t(r.size());
    return r;
}

static const std::vector<uint8_t> ipx_guest = { 0x02, 0, 0, 0, 0, 0x42 };

TEST(ModemIpxcp, WaitsForThePeerThenOpens)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_ipx(&wire);
    ASSERT_NE(ctx, nullptr);
    wire.clear();

    EXPECT_FALSE(ctx->ipxcp_active);
    send_frame(ctx, ipxcp_request(1, 0x12345678, ipx_guest), false, PPP_PROTO_IPXCP);
    EXPECT_TRUE(ctx->ipxcp_active);
    EXPECT_TRUE(ctx->ipxcp_ack_sent);
    EXPECT_EQ(ppp_ipx_network(ctx), 0x12345678u); /* the higher number wins */

    const std::vector<uint8_t> ack = { PPP_CODE_CONFIGURE_ACK, ctx->ipxcp_request_id, 0, 4 };
    send_frame(ctx, ack, false, PPP_PROTO_IPXCP);
    EXPECT_TRUE(ctx->ipxcp_open);
    EXPECT_TRUE(ppp_ipx_ready(ctx));
    ppp_close(ctx);
}

TEST(ModemIpxcp, GivesAnAddressToAGuestWithoutOne)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_ipx(&wire);
    ASSERT_NE(ctx, nullptr);

    /* The first one also sets off this end's own request, which goes out ahead of the answer. */
    send_frame(ctx, ipxcp_request(1, 0, std::vector<uint8_t>(6, 0)), false, PPP_PROTO_IPXCP);
    wire.clear();
    send_frame(ctx, ipxcp_request(2, 0, std::vector<uint8_t>(6, 0)), false, PPP_PROTO_IPXCP);
    EXPECT_FALSE(ctx->ipxcp_ack_sent);
    uint16_t protocol = 0;
    std::vector<uint8_t> nak = decode_captured_frame(wire, protocol);
    ASSERT_EQ(nak.size(), 18u);
    EXPECT_EQ(nak[0], PPP_CODE_CONFIGURE_NAK);
    EXPECT_EQ(protocol, PPP_PROTO_IPXCP);
    EXPECT_EQ(nak[4], IPXCP_OPT_NETWORK);
    EXPECT_EQ(nak[7], (IPX_NETWORK >> 16) & 0xFF);
    EXPECT_EQ(nak[10], IPXCP_OPT_NODE);
    EXPECT_NE(nak[17], 0);
    EXPECT_NE(nak[17], ppp_ipx_node(ctx)[5]);

    send_frame(ctx, ipxcp_request(3, IPX_NETWORK, std::vector<uint8_t>(nak.begin() + 12, nak.end())), false, PPP_PROTO_IPXCP);
    EXPECT_TRUE(ctx->ipxcp_ack_sent);
    ppp_close(ctx);
}

TEST(ModemIpxcp, RejectsCompressionAndStrangeRouting)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_ipx(&wire);
    ASSERT_NE(ctx, nullptr);

    const std::vector<uint8_t> request = { PPP_CODE_CONFIGURE_REQUEST, 1, 0, 16, IPXCP_OPT_COMPRESSION, 4, 0, 2,
                                           IPXCP_OPT_ROUTING, 4, 0, 4, IPXCP_OPT_ROUTING, 4, 0, 2 };
    send_frame(ctx, request, false, PPP_PROTO_IPXCP);
    EXPECT_FALSE(ctx->ipxcp_ack_sent);
    const std::vector<uint8_t> accepted = { PPP_CODE_CONFIGURE_REQUEST, 2, 0, 14, IPXCP_OPT_ROUTING, 4, 0, 2,
                                            IPXCP_OPT_ROUTER_NAME, 4, 'A', 'B', IPXCP_OPT_CONFIG_COMPLETE, 2 };
    send_frame(ctx, accepted, false, PPP_PROTO_IPXCP);
    EXPECT_TRUE(ctx->ipxcp_ack_sent);
    ppp_close(ctx);
}

TEST(ModemIpxcp, FollowsThePeerToAHigherNetworkAndDropsWhatItRejects)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_ipx(&wire);
    ASSERT_NE(ctx, nullptr);

    send_frame(ctx, ipxcp_request(1, IPX_NETWORK, ipx_guest), false, PPP_PROTO_IPXCP);
    std::vector<uint8_t> nak = { PPP_CODE_CONFIGURE_NAK, ctx->ipxcp_request_id, 0, 10, IPXCP_OPT_NETWORK, 6, 0x7F, 0, 0, 1 };
    send_frame(ctx, nak, false, PPP_PROTO_IPXCP);
    EXPECT_EQ(ppp_ipx_network(ctx), 0x7F000001u);

    std::vector<uint8_t> reject = { PPP_CODE_CONFIGURE_REJECT, ctx->ipxcp_request_id, 0, 12, IPXCP_OPT_NODE, 8, 0, 0, 0, 0, 0, 0 };
    send_frame(ctx, reject, false, PPP_PROTO_IPXCP);
    EXPECT_FALSE(ctx->ipxcp_send_node);
    EXPECT_TRUE(ctx->ipxcp_req_sent);
    ppp_close(ctx);
}

TEST(ModemIpxcp, IsRefusedWhenThereIsNoServerBehindIt)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_capturing(&wire);
    ASSERT_NE(ctx, nullptr);
    ppp_start(ctx);
    ctx->state = PPP_STATE_IPCP_NEGOTIATE;
    wire.clear();

    send_frame(ctx, ipxcp_request(1, 0, ipx_guest), false, PPP_PROTO_IPXCP);
    uint16_t protocol = 0;
    decode_captured_frame(wire, protocol);
    EXPECT_EQ(protocol, PPP_PROTO_LCP);
    EXPECT_FALSE(ctx->ipxcp_active);
    ppp_close(ctx);
}

TEST(ModemIpxcp, CarriesPacketsBothWaysOnceOpen)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_ipx(&wire);
    ASSERT_NE(ctx, nullptr);

    const std::vector<uint8_t> packet(30, 0xAB);
    ipx_received.clear();
    send_frame(ctx, packet, false, PPP_PROTO_IPX);
    EXPECT_TRUE(ipx_received.empty());

    send_frame(ctx, ipxcp_request(1, IPX_NETWORK, ipx_guest), false, PPP_PROTO_IPXCP);
    const std::vector<uint8_t> ack = { PPP_CODE_CONFIGURE_ACK, ctx->ipxcp_request_id, 0, 4 };
    send_frame(ctx, ack, false, PPP_PROTO_IPXCP);

    send_frame(ctx, packet, false, PPP_PROTO_IPX);
    EXPECT_EQ(ipx_received, packet);

    wire.clear();
    ppp_wrap_ipx(ctx, packet.data(), int(packet.size()));
    uint16_t protocol = 0;
    EXPECT_EQ(decode_captured_frame(wire, protocol), packet);
    EXPECT_EQ(protocol, PPP_PROTO_IPX);
    ppp_close(ctx);
}

TEST(ModemNbfcp, WaitsForThePeerAndAcceptsItsNames)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_nbf(&wire);
    ASSERT_NE(ctx, nullptr);
    wire.clear();

    EXPECT_FALSE(ctx->nbfcp_active);
    std::vector<uint8_t> request = { PPP_CODE_CONFIGURE_REQUEST, 1, 0, 25, NBFCP_OPT_NAME_PROJECTION, 19 };
    request.insert(request.end(), { '8', '6', 'B', 'O', 'X', '9', '5', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', 0x00, 1 });
    request.push_back(NBFCP_OPT_MULTICAST_FILTER);
    request.push_back(2);
    request[3] = uint8_t(request.size());
    send_frame(ctx, request, false, PPP_PROTO_NBFCP);

    EXPECT_TRUE(ctx->nbfcp_active);
    EXPECT_TRUE(ctx->nbfcp_ack_sent);
    EXPECT_TRUE(ctx->nbfcp_req_sent);
    EXPECT_FALSE(ctx->nbfcp_open);

    const std::vector<uint8_t> ack = { PPP_CODE_CONFIGURE_ACK, ctx->nbfcp_request_id, 0, 4 };
    send_frame(ctx, ack, false, PPP_PROTO_NBFCP);
    EXPECT_TRUE(ctx->nbfcp_open);
    EXPECT_TRUE(ppp_nbf_ready(ctx));
    ppp_close(ctx);
}

TEST(ModemNbfcp, RejectsWhatItDoesNotKnow)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_nbf(&wire);
    ASSERT_NE(ctx, nullptr);

    const std::vector<uint8_t> request = { PPP_CODE_CONFIGURE_REQUEST, 1, 0, 7, 99, 3, 0 };
    send_frame(ctx, request, false, PPP_PROTO_NBFCP);
    EXPECT_FALSE(ctx->nbfcp_ack_sent);
    ppp_close(ctx);
}

TEST(ModemNbfcp, IsRefusedWhenThereIsNoServerBehindIt)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_capturing(&wire);
    ASSERT_NE(ctx, nullptr);
    ppp_start(ctx);
    ctx->state = PPP_STATE_IPCP_NEGOTIATE;
    wire.clear();

    const std::vector<uint8_t> request = { PPP_CODE_CONFIGURE_REQUEST, 1, 0, 4 };
    send_frame(ctx, request, false, PPP_PROTO_NBFCP);
    EXPECT_FALSE(ctx->nbfcp_active);
    uint16_t protocol = 0;
    auto     frame    = decode_captured_frame(wire, protocol);
    EXPECT_EQ(protocol, PPP_PROTO_LCP);
    ASSERT_FALSE(frame.empty());
    EXPECT_EQ(frame[0], PPP_CODE_PROTOCOL_REJECT);
    ppp_close(ctx);
}

TEST(ModemNbfcp, CarriesFramesBothWaysOnceOpen)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_nbf(&wire);
    ASSERT_NE(ctx, nullptr);

    const std::vector<uint8_t> frame = { 0xF0, 0xF0, 0x7F };
    nbf_received.clear();
    send_frame(ctx, frame, false, PPP_PROTO_NBF);
    EXPECT_TRUE(nbf_received.empty()); /* nothing passes before NBFCP opens */

    const std::vector<uint8_t> request = { PPP_CODE_CONFIGURE_REQUEST, 1, 0, 4 };
    send_frame(ctx, request, false, PPP_PROTO_NBFCP);
    const std::vector<uint8_t> ack = { PPP_CODE_CONFIGURE_ACK, ctx->nbfcp_request_id, 0, 4 };
    send_frame(ctx, ack, false, PPP_PROTO_NBFCP);

    send_frame(ctx, frame, false, PPP_PROTO_NBF);
    EXPECT_EQ(nbf_received, frame);

    wire.clear();
    ppp_wrap_nbf(ctx, frame.data(), int(frame.size()));
    uint16_t protocol = 0;
    EXPECT_EQ(decode_captured_frame(wire, protocol), frame);
    EXPECT_EQ(protocol, PPP_PROTO_NBF);
    ppp_close(ctx);
}

TEST(ModemNbfcp, AddsAndRemovesTheHeaderWhenAskedFor)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_nbf(&wire);
    ASSERT_NE(ctx, nullptr);

    const std::vector<uint8_t> request = { PPP_CODE_CONFIGURE_REQUEST, 1, 0, 6, NBFCP_OPT_MAC_REQUIRED, 2 };
    send_frame(ctx, request, false, PPP_PROTO_NBFCP);
    const std::vector<uint8_t> ack = { PPP_CODE_CONFIGURE_ACK, ctx->nbfcp_request_id, 0, 4 };
    send_frame(ctx, ack, false, PPP_PROTO_NBFCP);
    ASSERT_TRUE(ctx->nbfcp_mac_required);
    EXPECT_EQ(ppp_nbf_max_frame(ctx), ctx->peer_mru - 12);

    std::vector<uint8_t> incoming(12, 0x55);
    incoming.insert(incoming.end(), { 0xF0, 0xF0, 0x7F });
    send_frame(ctx, incoming, false, PPP_PROTO_NBF);
    EXPECT_EQ(nbf_received, std::vector<uint8_t>({ 0xF0, 0xF0, 0x7F }));

    wire.clear();
    const std::vector<uint8_t> frame = { 0xF0, 0xF1, 0x73 };
    ppp_wrap_nbf(ctx, frame.data(), int(frame.size()));
    uint16_t protocol = 0;
    auto     out      = decode_captured_frame(wire, protocol);
    ASSERT_EQ(out.size(), 15u);
    EXPECT_EQ(out[12], 0xF0);
    EXPECT_EQ(out[14], 0x73);
    ppp_close(ctx);
}

TEST(ModemNbfcp, RepeatsItsRequestThenGivesUp)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_nbf(&wire);
    ASSERT_NE(ctx, nullptr);
    const std::vector<uint8_t> request = { PPP_CODE_CONFIGURE_REQUEST, 1, 0, 4 };
    send_frame(ctx, request, false, PPP_PROTO_NBFCP);

    uint8_t first = ctx->nbfcp_request_id;
    for (int ms = 0; ms < NBFCP_TIMEOUT_MS; ms++)
        ppp_nbfcp_tick(ctx);
    EXPECT_NE(ctx->nbfcp_request_id, first);
    for (int ms = 0; ms < NBFCP_TIMEOUT_MS * (NBFCP_MAX_RETRIES + 1); ms++)
        ppp_nbfcp_tick(ctx);
    EXPECT_FALSE(ctx->nbfcp_active);
    ppp_close(ctx);
}

TEST(ModemNbfcp, ANetBeuiOnlyGuestOutlastsIpcp)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_nbf(&wire);
    ASSERT_NE(ctx, nullptr);
    ctx->ipcp_req_sent = true; /* the guest ignores what ppp_start would have sent */

    const std::vector<uint8_t> request = { PPP_CODE_CONFIGURE_REQUEST, 1, 0, 4 };
    send_frame(ctx, request, false, PPP_PROTO_NBFCP);
    const std::vector<uint8_t> ack = { PPP_CODE_CONFIGURE_ACK, ctx->nbfcp_request_id, 0, 4 };
    send_frame(ctx, ack, false, PPP_PROTO_NBFCP);

    for (int ms = 0; ms < 120000; ms++)
        ppp_timer_tick(ctx);
    EXPECT_FALSE(ctx->ipcp_req_sent);
    EXPECT_NE(ctx->state, PPP_STATE_DEAD);
    EXPECT_TRUE(ppp_nbf_ready(ctx));
    ppp_close(ctx);
}

TEST(ModemPppAuto, KeepaliveDropsSilentPeerAfterFourMissedIntervals)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_capturing(&wire);
    ASSERT_NE(ctx, nullptr);
    ctx->ppp_keepalive_s = 2;
    ppp_start(ctx);
    ctx->state = PPP_STATE_NETWORK;
    wire.clear();

    for (int ms = 0; ms < 2000; ms++)
        ppp_timer_tick(ctx);
    EXPECT_EQ(ctx->keepalive_missed, 1);
    EXPECT_FALSE(wire.empty());

    for (int ms = 0; ms < 6000; ms++)
        ppp_timer_tick(ctx);
    EXPECT_EQ(ctx->keepalive_missed, 4);
    EXPECT_EQ(ctx->state, PPP_STATE_NETWORK);

    for (int ms = 0; ms < 2000; ms++)
        ppp_timer_tick(ctx);
    EXPECT_EQ(ctx->state, PPP_STATE_DEAD);
    ppp_close(ctx);
}

TEST(ModemPppAuto, ZeroKeepaliveIntervalTurnsItOff)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_capturing(&wire);
    ASSERT_NE(ctx, nullptr);
    ctx->ppp_keepalive_s = 0;
    ppp_start(ctx);
    ctx->state = PPP_STATE_NETWORK;
    wire.clear();

    for (int ms = 0; ms < 600000; ms++)
        ppp_timer_tick(ctx);

    EXPECT_TRUE(wire.empty());
    EXPECT_EQ(ctx->state, PPP_STATE_NETWORK);
    ppp_close(ctx);
}

static int
count_lcp_requests(const std::vector<uint8_t> &wire)
{
    int    count = 0;
    size_t start = 0;
    while (start < wire.size()) {
        size_t end = start + 1;
        while (end < wire.size() && wire[end] != PPP_FLAG)
            end++;
        if (end >= wire.size())
            break;
        std::vector<uint8_t> frame(wire.begin() + start, wire.begin() + end + 1);
        uint16_t protocol = 0;
        std::vector<uint8_t> packet = decode_captured_frame(frame, protocol);
        if (protocol == PPP_PROTO_LCP && !packet.empty()
            && packet[0] == PPP_CODE_CONFIGURE_REQUEST)
            count++;
        start = end + 1;
    }
    return count;
}

static const std::vector<uint8_t> plain_peer_request = {
    PPP_CODE_CONFIGURE_REQUEST, 1, 0, 10,
    LCP_OPT_MAGIC_NUMBER, 6, 0x12, 0x34, 0x56, 0x78
};

TEST(ModemPppAuto, SoftwareFlowRequestsAndRequiresControlCharacterEscaping)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_capturing(&wire);
    ASSERT_NE(ctx, nullptr);
    ctx->flow_accm = (1u << 0x11) | (1u << 0x13);
    ctx->our_accm |= ctx->flow_accm;
    ppp_start(ctx);

    const std::vector<uint8_t> request(ctx->lcp_request, ctx->lcp_request + ctx->lcp_request_len);
    const std::vector<uint8_t> option = { LCP_OPT_ACCM, 6, 0, 0x0a, 0, 0 };
    ASSERT_NE(std::search(request.begin() + 4, request.end(), option.begin(), option.end()), request.end());

    std::vector<uint8_t> ack(request);
    ack[0] = PPP_CODE_CONFIGURE_ACK;
    send_frame(ctx, ack);
    EXPECT_TRUE(ctx->lcp_ack_received);
    EXPECT_NE(ctx->state, PPP_STATE_DEAD);
    ppp_close(ctx);
}

TEST(ModemPppAuto, SoftwareFlowDoesNotNegotiateWithoutItsAccm)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_capturing(&wire);
    ASSERT_NE(ctx, nullptr);
    ctx->flow_accm = (1u << 0x11) | (1u << 0x13);
    ctx->our_accm |= ctx->flow_accm;
    ppp_start(ctx);

    for (int attempt = 0; attempt < 12 && ctx->state != PPP_STATE_DEAD; attempt++) {
        const std::vector<uint8_t> reject = { PPP_CODE_CONFIGURE_REJECT, ctx->lcp_request[1], 0, 10,
                                              LCP_OPT_ACCM, 6, 0, 0x0a, 0, 0 };
        send_frame(ctx, reject);
    }
    EXPECT_EQ(ctx->state, PPP_STATE_DEAD);
    EXPECT_FALSE(ctx->lcp_ack_received);
    ppp_close(ctx);
}

TEST(ModemPppAuto, FirstPeerRequestRepeatsOursOnce)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_capturing(&wire);
    ASSERT_NE(ctx, nullptr);
    ppp_start(ctx);
    const uint8_t first_id = ctx->lcp_request[1];
    ASSERT_EQ(count_lcp_requests(wire), 1);

    send_frame(ctx, plain_peer_request);
    EXPECT_EQ(count_lcp_requests(wire), 2);
    EXPECT_NE(ctx->lcp_request[1], first_id);
    EXPECT_TRUE(ctx->lcp_ack_sent);
    EXPECT_EQ(ctx->lcp_retries, 0);

    send_frame(ctx, plain_peer_request);
    EXPECT_EQ(count_lcp_requests(wire), 2);
    ppp_close(ctx);
}

TEST(ModemPppAuto, AckForEarlierRequestIsIgnoredAfterRepeat)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_capturing(&wire);
    ASSERT_NE(ctx, nullptr);
    ppp_start(ctx);
    std::vector<uint8_t> stale(ctx->lcp_request, ctx->lcp_request + ctx->lcp_request_len);
    stale[0] = PPP_CODE_CONFIGURE_ACK;

    send_frame(ctx, plain_peer_request);
    send_frame(ctx, stale);
    EXPECT_FALSE(ctx->lcp_ack_received);

    std::vector<uint8_t> current(ctx->lcp_request, ctx->lcp_request + ctx->lcp_request_len);
    current[0] = PPP_CODE_CONFIGURE_ACK;
    send_frame(ctx, current);
    EXPECT_TRUE(ctx->lcp_ack_received);
    ppp_close(ctx);
}

TEST(ModemPppAuto, NoRepeatWhenPeerAlreadyAckedOurRequest)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_capturing(&wire);
    ASSERT_NE(ctx, nullptr);
    ppp_start(ctx);
    std::vector<uint8_t> ack(ctx->lcp_request, ctx->lcp_request + ctx->lcp_request_len);
    ack[0] = PPP_CODE_CONFIGURE_ACK;
    send_frame(ctx, ack);

    send_frame(ctx, plain_peer_request);

    EXPECT_EQ(count_lcp_requests(wire), 1);
    ppp_close(ctx);
}

TEST(ModemPppAuto, RetryTimerStillCoversASilentPeer)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_capturing(&wire);
    ASSERT_NE(ctx, nullptr);
    ppp_start(ctx);

    for (int ms = 0; ms < 3000; ms++)
        ppp_timer_tick(ctx);

    EXPECT_EQ(count_lcp_requests(wire), 2);
    EXPECT_EQ(ctx->lcp_retries, 1);
    ppp_close(ctx);
}

TEST(ModemPppAuto, MirroredOptionAndRepeatSendOneRequest)
{
    std::vector<uint8_t> wire;
    ppp_ctx_t *ctx = start_capturing(&wire);
    ASSERT_NE(ctx, nullptr);
    ctx->ppp_numbered_mode = PPP_NUMBERED_AUTOMATIC;
    ppp_start(ctx);

    send_frame(ctx, numbered_request);

    EXPECT_EQ(count_lcp_requests(wire), 2);
    EXPECT_TRUE(has_option(last_lcp_request(wire), LCP_OPT_NUMBERED_MODE));
    ppp_close(ctx);
}
