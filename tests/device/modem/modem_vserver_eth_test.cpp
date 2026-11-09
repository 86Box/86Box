/* The virtual server as a guest with a network card sees it. */
#include <cstdint>
#include <cstring>
#include <vector>
#include <gtest/gtest.h>
#include <86box/network/net_vserver_config.h>
#include <86box/network/net_nbns.h>
#include <86box/network/net_vserver_eth.h>

extern "C" bool
ppp_random_bytes(uint8_t *buffer, uint8_t length)
{
    for (uint8_t i = 0; i < length; i++)
        buffer[i] = i;
    return true;
}

typedef std::vector<uint8_t> bytes;

static const uint32_t server_ip = 0x0A000204;
static const uint32_t guest_ip  = 0x0A00020F;
static const uint8_t  guest_mac[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static const uint8_t  server_mac[6] = { 0x52, 0x55, 0x0A, 0x00, 0x02, 0x04 };

static void put16(bytes &b, uint16_t v) { b.push_back(uint8_t(v >> 8)); b.push_back(uint8_t(v)); }
static void put32(bytes &b, uint32_t v) { put16(b, uint16_t(v >> 16)); put16(b, uint16_t(v)); }

static void
capture(void *priv, const uint8_t *frame, int len)
{
    static_cast<std::vector<bytes> *>(priv)->emplace_back(frame, frame + len);
}

static bytes
ethernet(const uint8_t *destination, uint16_t type, const bytes &payload)
{
    bytes f(destination, destination + 6);
    f.insert(f.end(), guest_mac, guest_mac + 6);
    put16(f, type);
    f.insert(f.end(), payload.begin(), payload.end());
    return f;
}

static bytes
arp_request(uint32_t target)
{
    bytes a;
    put16(a, 1); put16(a, 0x0800); a.push_back(6); a.push_back(4); put16(a, 1);
    a.insert(a.end(), guest_mac, guest_mac + 6);
    put32(a, guest_ip);
    a.insert(a.end(), 6, 0);
    put32(a, target);
    return a;
}

static bytes
udp(uint32_t destination, uint16_t port, const bytes &data)
{
    bytes p = { 0x45, 0 };
    put16(p, uint16_t(28 + data.size())); put32(p, 0); p.push_back(128); p.push_back(17); put16(p, 0);
    put32(p, guest_ip); put32(p, destination);
    put16(p, port); put16(p, port); put16(p, uint16_t(8 + data.size())); put16(p, 0);
    p.insert(p.end(), data.begin(), data.end());
    return p;
}

static bytes
name_query(const char *name, uint8_t suffix)
{
    bytes m = { 0x12, 0x34, 0x01, 0x10, 0, 1, 0, 0, 0, 0, 0, 0, 32 };
    uint8_t raw[16];
    memset(raw, ' ', 16);
    memcpy(raw, name, strlen(name));
    raw[15] = suffix;
    for (uint8_t c : raw) { m.push_back(uint8_t('A' + (c >> 4))); m.push_back(uint8_t('A' + (c & 15))); }
    m.push_back(0);
    put16(m, 0x20); put16(m, 1);
    return m;
}

class VserverEth : public ::testing::Test {
protected:
    void SetUp() override {
        eth = vserver_eth_init(nullptr, server_ip, true, true, false, false, true, true, capture, &sent);
        ASSERT_NE(eth, nullptr);
    }
    void TearDown() override { vserver_eth_close(eth); }
    bool in(const bytes &frame) { return vserver_eth_input(eth, frame.data(), int(frame.size())); }

    vserver_eth_t     *eth = nullptr;
    std::vector<bytes> sent;
};

static const uint8_t broadcast[6]  = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static const uint8_t functional[6] = { 3, 0, 0, 0, 0, 1 };

TEST_F(VserverEth, AnswersArpForItsAddressOnly)
{
    EXPECT_TRUE(in(ethernet(broadcast, 0x0806, arp_request(server_ip))));
    ASSERT_EQ(sent.size(), 1u);
    const bytes &r = sent[0];
    ASSERT_EQ(r.size(), 60u);
    EXPECT_EQ(memcmp(r.data(), guest_mac, 6), 0);
    EXPECT_EQ(memcmp(&r[6], server_mac, 6), 0);
    EXPECT_EQ(r[12], 0x08);
    EXPECT_EQ(r[13], 0x06);
    EXPECT_EQ(r[14 + 7], 2);
    EXPECT_EQ(memcmp(&r[14 + 8], server_mac, 6), 0);
    EXPECT_EQ(memcmp(&r[14 + 18], guest_mac, 6), 0);
    sent.clear();

    EXPECT_FALSE(in(ethernet(broadcast, 0x0806, arp_request(0x0A000202))));
    EXPECT_TRUE(sent.empty());
}

TEST_F(VserverEth, AnswersBroadcastQueriesForItsNames)
{
    EXPECT_TRUE(in(ethernet(broadcast, 0x0800, udp(0x0A0002FF, 137, name_query(VSERVER_SERVER_NAME, 0x20)))));
    ASSERT_EQ(sent.size(), 1u);
    const bytes &r = sent[0];
    EXPECT_EQ(memcmp(r.data(), guest_mac, 6), 0);
    uint32_t answer = (uint32_t(r[r.size() - 4]) << 24) | (r[r.size() - 3] << 16) | (r[r.size() - 2] << 8) | r.back();
    EXPECT_EQ(answer, server_ip);
    sent.clear();

    EXPECT_TRUE(in(ethernet(broadcast, 0x0800, udp(0x0A0002FF, 137, name_query(VSERVER_WORKGROUP_NAME, 0x1C)))));
    EXPECT_EQ(sent.size(), 1u);
}

TEST(VserverEthWithoutSmb, ContinuesAnsweringWinsQueries)
{
    std::vector<bytes> sent;
    vserver_eth_t *eth = vserver_eth_init(nullptr, server_ip, true, false, false, false, false, false, capture,
                                          &sent);
    ASSERT_NE(eth, nullptr);
    bytes query = ethernet(broadcast, 0x0800, udp(0x0A0002FF, 137, name_query(VSERVER_SERVER_NAME, 0x20)));
    EXPECT_TRUE(vserver_eth_input(eth, query.data(), int(query.size())));
    EXPECT_EQ(sent.size(), 1u);
    vserver_eth_close(eth);
}

TEST(VserverEthWithoutWins, LeavesNameQueriesAlone)
{
    std::vector<bytes> sent;
    vserver_eth_t *eth = vserver_eth_init(nullptr, server_ip, false, false, false, false, false, false,
                                          capture, &sent);
    ASSERT_NE(eth, nullptr);
    bytes query = ethernet(broadcast, 0x0800, udp(0x0A0002FF, 137, name_query(VSERVER_SERVER_NAME, 0x20)));
    EXPECT_FALSE(vserver_eth_input(eth, query.data(), int(query.size())));
    EXPECT_TRUE(sent.empty());
    vserver_eth_close(eth);
}

TEST_F(VserverEth, StaysSilentForNamesItDoesNotOwn)
{
    EXPECT_TRUE(in(ethernet(broadcast, 0x0800, udp(0x0A0002FF, 137, name_query("NOSUCHNAME", 0)))));
    EXPECT_TRUE(in(ethernet(broadcast, 0x0800, udp(0x0A0002FF, 137, name_query(VSERVER_WORKGROUP_NAME, 0)))));
    EXPECT_TRUE(sent.empty());
}

TEST_F(VserverEth, LeavesOtherTrafficToTheNetwork)
{
    EXPECT_FALSE(in(ethernet(broadcast, 0x0800, udp(0x08080808, 53, { 1, 2, 3 }))));
    EXPECT_FALSE(in(ethernet(broadcast, 0x8137, { 0xFF, 0xFF, 0, 30 })));
    EXPECT_FALSE(in(ethernet(broadcast, 0x86DD, bytes(40, 0))));
    EXPECT_TRUE(sent.empty());
}

TEST_F(VserverEth, TakesNetBeuiFramesAndIgnoresThePadding)
{
    bytes frame = ethernet(server_mac, 3, { 0xF0, 0xF0, 0x7F });
    frame.resize(60, 0xAA);
    EXPECT_TRUE(in(frame));
    ASSERT_EQ(sent.size(), 1u);
    const bytes &r = sent[0];
    EXPECT_EQ(memcmp(&r[6], server_mac, 6), 0);
    EXPECT_EQ(r[12], 0);
    EXPECT_EQ(r[13], 3); /* a length, not a type */
    EXPECT_EQ(r[14 + 1], 0xF1);
    EXPECT_EQ(r[14 + 2], 0x73);
}

TEST_F(VserverEth, HearsTheNetBiosFunctionalAddress)
{
    EXPECT_TRUE(in(ethernet(functional, 3, { 0xF0, 0xF0, 0xF3 })));
    EXPECT_EQ(sent.size(), 1u);
}

TEST_F(VserverEth, IgnoresNetBeuiForOtherStationsAndOtherSaps)
{
    static const uint8_t other[6] = { 0x52, 0x54, 0, 0, 0, 1 };

    EXPECT_FALSE(in(ethernet(other, 3, { 0xF0, 0xF0, 0x7F })));
    EXPECT_FALSE(in(ethernet(server_mac, 3, { 0xE0, 0xE0, 0x03 })));
    EXPECT_FALSE(in(ethernet(server_mac, 200, { 0xF0, 0xF0, 0x7F })));
    EXPECT_TRUE(sent.empty());
}

TEST(VserverEthWithoutNetbeui, LeavesLlcAlone)
{
    std::vector<bytes> sent;
    vserver_eth_t *eth = vserver_eth_init(nullptr, server_ip, true, true, false, false, false, false, capture, &sent);
    bytes frame = ethernet(server_mac, 3, { 0xF0, 0xF0, 0x7F });
    EXPECT_FALSE(vserver_eth_input(eth, frame.data(), int(frame.size())));
    vserver_eth_close(eth);
}

static bytes
rip_request()
{
    bytes p;
    put16(p, 0xFFFF); put16(p, 40); p.push_back(0); p.push_back(1);
    put32(p, 0); p.insert(p.end(), broadcast, broadcast + 6); put16(p, 0x453);
    put32(p, 0); p.insert(p.end(), guest_mac, guest_mac + 6); put16(p, 0x453);
    put16(p, 1); put32(p, 0xFFFFFFFFu); put16(p, 0xFFFF); put16(p, 0xFFFF);
    return p;
}

TEST_F(VserverEth, AnswersIpxInTheFramingItWasAskedIn)
{
    const bytes request = rip_request();

    EXPECT_TRUE(in(ethernet(broadcast, 0x8137, request)));
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0][12], 0x81);
    EXPECT_EQ(sent[0][13], 0x37);
    EXPECT_EQ(sent[0][14], 0xFF);
    sent.clear();

    EXPECT_TRUE(in(ethernet(broadcast, uint16_t(request.size()), request)));
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ((sent[0][12] << 8) | sent[0][13], 40);
    EXPECT_EQ(sent[0][14], 0xFF);
    sent.clear();

    bytes llc = { 0xE0, 0xE0, 0x03 };
    llc.insert(llc.end(), request.begin(), request.end());
    EXPECT_TRUE(in(ethernet(broadcast, uint16_t(llc.size()), llc)));
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ((sent[0][12] << 8) | sent[0][13], 43);
    EXPECT_EQ(sent[0][14], 0xE0);
    EXPECT_EQ(sent[0][17], 0xFF);
    sent.clear();

    bytes snap = { 0xAA, 0xAA, 0x03, 0, 0, 0, 0x81, 0x37 };
    snap.insert(snap.end(), request.begin(), request.end());
    EXPECT_TRUE(in(ethernet(broadcast, uint16_t(snap.size()), snap)));
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ((sent[0][12] << 8) | sent[0][13], 48);
    EXPECT_EQ(sent[0][14], 0xAA);
    EXPECT_EQ(sent[0][22], 0xFF);
}

TEST(VserverEthWithoutIpx, LeavesIpxAlone)
{
    std::vector<bytes> sent;
    vserver_eth_t *eth = vserver_eth_init(nullptr, server_ip, true, true, false, false, true, false, capture, &sent);
    bytes frame = ethernet(broadcast, 0x8137, rip_request());
    EXPECT_FALSE(vserver_eth_input(eth, frame.data(), int(frame.size())));
    vserver_eth_close(eth);
}

TEST_F(VserverEth, AnswersPingOnceItIsKnown)
{
    bytes icmp = { 8, 0, 0, 0, 0, 1, 0, 1 };
    bytes p = { 0x45, 0 };
    put16(p, 28); put32(p, 0); p.push_back(32); p.push_back(1); put16(p, 0);
    put32(p, guest_ip); put32(p, server_ip);
    p.insert(p.end(), icmp.begin(), icmp.end());
    EXPECT_TRUE(in(ethernet(server_mac, 0x0800, p)));
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0][12], 0x08);
    EXPECT_EQ(sent[0][14 + 20], 0);
}

TEST_F(VserverEth, IsSafeWithRunts)
{
    uint8_t runt[14] = { 0 };
    EXPECT_FALSE(vserver_eth_input(eth, runt, 14));
    EXPECT_FALSE(vserver_eth_input(eth, runt, 3));
    EXPECT_FALSE(vserver_eth_input(nullptr, runt, 14));
    for (int ms = 0; ms < 5000; ms++)
        vserver_eth_tick(eth);
}
