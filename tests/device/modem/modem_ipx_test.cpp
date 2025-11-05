/* IPX for the virtual server. Frames are built the way the Windows for Workgroups driver
   builds them, for NetBIOS sessions, and as [MS-CIFS] describes for direct hosting. */
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include <86box/network/net_vserver_config.h>
#include <86box/network/net_ipx.h>
#include <86box/network/net_nbns.h>
#include <86box/network/net_smb.h>
#include <86box/network/net_smb_fs.h>

typedef std::vector<uint8_t> bytes;

static const uint8_t server_node[6] = { 2, 0x86, 0xB0, 0, 0, 1 };
static const uint8_t guest_node[6]  = { 2, 0x86, 0xB0, 0, 0, 2 };
static const uint8_t everyone[6]    = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static void put_le16(bytes &b, uint16_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); }
static void put_be16(bytes &b, uint16_t v) { b.push_back(uint8_t(v >> 8)); b.push_back(uint8_t(v)); }
static void put_be32(bytes &b, uint32_t v) { put_be16(b, uint16_t(v >> 16)); put_be16(b, uint16_t(v)); }
static void put_str(bytes &b, const std::string &s) { b.insert(b.end(), s.begin(), s.end()); }
static uint16_t get_le16(const uint8_t *p) { return uint16_t(p[0] | (p[1] << 8)); }
static uint16_t get_be16(const uint8_t *p) { return uint16_t((p[0] << 8) | p[1]); }

static void
capture(void *priv, const uint8_t *packet, int len)
{
    static_cast<std::vector<bytes> *>(priv)->emplace_back(packet, packet + len);
}

static bytes
ipx_packet(uint8_t type, const uint8_t *to, uint16_t to_socket, uint16_t from_socket, const bytes &data)
{
    bytes p;
    put_be16(p, 0xFFFF);
    put_be16(p, uint16_t(30 + data.size()));
    p.push_back(0);
    p.push_back(type);
    put_be32(p, IPX_NETWORK);
    p.insert(p.end(), to, to + 6);
    put_be16(p, to_socket);
    put_be32(p, IPX_NETWORK);
    p.insert(p.end(), guest_node, guest_node + 6);
    put_be16(p, from_socket);
    p.insert(p.end(), data.begin(), data.end());
    return p;
}

static std::string
nb_name(const std::string &text, uint8_t suffix)
{
    std::string name = text;
    name.resize(15, ' ');
    name.push_back(char(suffix));
    return name;
}

static bytes
name_frame(uint8_t type, const std::string &name, uint8_t flags = 0)
{
    bytes f(32, 0);
    f[1] = type;
    f.push_back(flags);
    f.push_back(type);
    put_str(f, name);
    return f;
}

static bytes
session_frame(uint8_t control, uint8_t type, uint16_t source, uint16_t dest, uint16_t seq, uint16_t total,
              uint16_t offset, uint16_t recv, const bytes &data = {}, uint16_t received = 0)
{
    bytes f = { control, type };
    put_le16(f, source);
    put_le16(f, dest);
    put_le16(f, seq);
    put_le16(f, total);
    put_le16(f, offset);
    put_le16(f, uint16_t(data.size()));
    put_le16(f, recv);
    put_le16(f, received);
    f.insert(f.end(), data.begin(), data.end());
    return f;
}

static bytes
smb(uint8_t command, uint16_t tid, const std::vector<uint16_t> &words = {}, const bytes &data = {})
{
    bytes m = { 0xFF, 'S', 'M', 'B', command };
    m.resize(32, 0);
    m[24] = uint8_t(tid);
    m.push_back(uint8_t(words.size()));
    for (uint16_t w : words)
        put_le16(m, w);
    put_le16(m, uint16_t(data.size()));
    m.insert(m.end(), data.begin(), data.end());
    return m;
}

static bytes
negotiate()
{
    bytes dialect = { 2 };
    put_str(dialect, "NT LM 0.12");
    dialect.push_back(0);
    return smb(0x72, 0, {}, dialect);
}

static const uint8_t *
payload(const bytes &packet)
{
    return packet.data() + 30;
}

class Ipx : public ::testing::Test {
protected:
    void SetUp() override {
        ipx = ipx_init(nullptr, server_node, capture, &sent);
        ASSERT_NE(ipx, nullptr);
    }
    void TearDown() override { ipx_close(ipx); }

    bool in(const bytes &packet) { return ipx_input(ipx, packet.data(), int(packet.size())); }
    void tick(int ms) { for (int i = 0; i < ms; i++) ipx_tick(ipx); }

    /* --- NetBIOS sessions --- */
    bytes init_data(uint16_t max_data = 1024) {
        bytes init;
        put_str(init, nb_name("GUEST", 0x00));
        put_str(init, nb_name(VSERVER_SERVER_NAME, 0x20));
        put_le16(init, max_data);
        put_le16(init, 0x1234);
        put_le16(init, 0x5678);
        return init;
    }

    void open_session(uint16_t max_data = 1024) {
        bytes init = init_data(max_data);
        sent.clear();
        in(ipx_packet(4, server_node, 0x455, 0x455,
                      session_frame(0x40, 6, guest_conn, 0xFFFF, 0, uint16_t(init.size()), 0, 0, init)));
        ASSERT_EQ(sent.size(), 1u);
        server_conn = get_le16(payload(sent[0]) + 2);
        rx_seq = 0;
        tx_seq = 1;
        sent.clear();
    }

    void ack_with(uint8_t control, uint16_t recv, uint16_t received) {
        in(ipx_packet(4, server_node, 0x455, 0x455,
                      session_frame(control, 6, guest_conn, server_conn, tx_seq, 0, 0, recv, {}, received)));
    }

    /* Sends a message in pieces of at most chunk and returns what comes back whole,
       acknowledging as a guest does. */
    bytes exchange(const bytes &message, size_t chunk = 1000) {
        bytes reply;
        for (size_t offset = 0; offset < message.size(); offset += chunk) {
            size_t length = std::min(chunk, message.size() - offset);
            bool   last   = offset + length == message.size();

            sent.clear();
            in(ipx_packet(4, server_node, 0x455, 0x455,
                          session_frame(uint8_t(0x40 | (last ? 0x10 : 0)), 6, guest_conn, server_conn, tx_seq,
                                        uint16_t(message.size()), uint16_t(offset), rx_seq,
                                        bytes(message.begin() + offset, message.begin() + offset + length))));
            EXPECT_GE(sent.size(), 1u);
            if (sent.empty())
                return reply;
            EXPECT_EQ(payload(sent[0])[0], 0x80);
            if (!last) {
                EXPECT_EQ(get_le16(payload(sent[0]) + 16), offset + length);
            }
        }
        tx_seq++;
        EXPECT_EQ(get_le16(payload(sent[0]) + 14), tx_seq);
        EXPECT_EQ(get_le16(payload(sent[0]) + 16), 0);

        for (;;) {
            std::vector<bytes> arrived;
            arrived.swap(sent);
            bool got = false;
            for (const bytes &packet : arrived) {
                const uint8_t *p = payload(packet);
                if (p[0] & 0x80)
                    continue;
                EXPECT_EQ(p[1], 6);
                EXPECT_EQ(get_le16(p + 6), rx_seq);
                EXPECT_EQ(get_le16(p + 10), reply.size());
                total = get_le16(p + 8);
                reply.insert(reply.end(), p + 18, p + 18 + get_le16(p + 12));
                got = true;
                if (reply.size() == total) {
                    EXPECT_TRUE(p[0] & 0x10);
                    rx_seq++;
                    ack_with(0x80, rx_seq, 0);
                } else {
                    ack_with(0x80, rx_seq, uint16_t(reply.size()));
                }
            }
            if (!got)
                break;
        }
        return reply;
    }

    /* Leaves out the announcements that come and go on their own schedule. */
    size_t session_packets() {
        size_t count = 0;
        for (const bytes &packet : sent) {
            if (packet[5] == 4 && payload(packet)[1] == 6)
                count++;
        }
        return count;
    }

    /* --- Direct hosting --- */
    bytes direct(bytes message, uint16_t cid, uint16_t sequence) {
        message[18] = uint8_t(cid);
        message[19] = uint8_t(cid >> 8);
        message[20] = uint8_t(sequence);
        message[21] = uint8_t(sequence >> 8);
        sent.clear();
        in(ipx_packet(4, server_node, 0x550, 0x552, message));
        if (sent.empty())
            return {};
        EXPECT_EQ(sent.size(), 1u);
        EXPECT_EQ(get_be16(sent[0].data() + 16), 0x552);
        EXPECT_EQ(get_be16(sent[0].data() + 28), 0x550);
        return bytes(sent[0].begin() + 30, sent[0].end());
    }

    ipx_t             *ipx = nullptr;
    std::vector<bytes> sent;
    uint16_t           guest_conn = 0x0102;
    uint16_t           server_conn = 0;
    uint16_t           rx_seq = 0;
    uint16_t           tx_seq = 1;
    size_t             total = 0;
};

TEST_F(Ipx, IgnoresWhatIsNotForIt)
{
    static const uint8_t other[6] = { 2, 0, 0, 0, 0, 9 };

    EXPECT_FALSE(in(ipx_packet(4, other, 0x455, 0x455, name_frame(1, nb_name(VSERVER_SERVER_NAME, 0x20)))));
    EXPECT_FALSE(in(ipx_packet(4, server_node, 0x4000, 0x4000, { 1, 2, 3 })));
    EXPECT_FALSE(in({ 1, 2, 3 }));
    EXPECT_TRUE(sent.empty());
}

TEST_F(Ipx, RefusesALengthLongerThanThePacket)
{
    bytes packet = ipx_packet(4, server_node, 0x455, 0x455, name_frame(1, nb_name(VSERVER_SERVER_NAME, 0x20)));
    packet[3] += 10;
    EXPECT_FALSE(in(packet));
}

TEST_F(Ipx, AnswersRipForItsNetwork)
{
    bytes request;
    put_be16(request, 1);
    put_be32(request, 0xFFFFFFFFu);
    put_be16(request, 0xFFFF);
    put_be16(request, 0xFFFF);
    EXPECT_TRUE(in(ipx_packet(1, everyone, 0x453, 0x453, request)));
    ASSERT_EQ(sent.size(), 1u);
    const bytes &r = sent[0];
    EXPECT_EQ(r[5], 1);
    EXPECT_EQ(get_be16(payload(r)), 2);
    EXPECT_EQ(get_be16(payload(r) + 2), IPX_NETWORK >> 16);
    EXPECT_EQ(get_be16(payload(r) + 4), IPX_NETWORK & 0xFFFF);
    EXPECT_EQ(get_be16(payload(r) + 6), 1);
    EXPECT_EQ(memcmp(r.data() + 10, guest_node, 6), 0);
    EXPECT_EQ(memcmp(r.data() + 22, server_node, 6), 0);
}

TEST_F(Ipx, IsSilentOnRipForOtherNetworks)
{
    bytes request;
    put_be16(request, 1);
    put_be32(request, 0x12345678u);
    put_be16(request, 0xFFFF);
    put_be16(request, 0xFFFF);
    in(ipx_packet(1, everyone, 0x453, 0x453, request));
    EXPECT_TRUE(sent.empty());
}

TEST_F(Ipx, RecognisesItsNames)
{
    for (const std::string &name : { nb_name(VSERVER_SERVER_NAME, 0x20), nb_name(VSERVER_SERVER_NAME, 0x00),
                                     nb_name(VSERVER_WORKGROUP_NAME, 0x1C), nb_name(VSERVER_WORKGROUP_NAME, 0x1D) }) {
        sent.clear();
        EXPECT_TRUE(in(ipx_packet(20, everyone, 0x455, 0x455, name_frame(1, name))));
        ASSERT_EQ(sent.size(), 1u);
        ASSERT_EQ(sent[0].size(), 80u);
        const uint8_t *p = payload(sent[0]);
        EXPECT_EQ(sent[0][5], 4);
        EXPECT_EQ(p[1], 2);
        EXPECT_EQ(p[33], 2);
        EXPECT_EQ(p[32] & 0x44, 0x44);
        EXPECT_EQ(memcmp(p + 34, name.data(), 16), 0);
    }
    sent.clear();
    in(ipx_packet(20, everyone, 0x455, 0x455, name_frame(1, nb_name("NOSUCHNAME", 0x20))));
    EXPECT_TRUE(sent.empty());
}

TEST_F(Ipx, DefendsItsNames)
{
    in(ipx_packet(20, everyone, 0x455, 0x455, name_frame(3, nb_name(VSERVER_SERVER_NAME, 0x20))));
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(payload(sent[0])[33], 4);
    sent.clear();
    in(ipx_packet(20, everyone, 0x455, 0x455, name_frame(3, nb_name("GUEST", 0x20))));
    EXPECT_TRUE(sent.empty());
}

TEST_F(Ipx, AnswersASessionRequestAsTheDriverExpects)
{
    bytes init = init_data();
    in(ipx_packet(4, server_node, 0x455, 0x455, session_frame(0x40, 6, guest_conn, 0xFFFF, 0, 38, 0, 0, init)));
    ASSERT_EQ(sent.size(), 1u);
    const bytes &r = sent[0];
    ASSERT_EQ(r.size(), 30u + 18u + 38u);
    const uint8_t *p = payload(r);
    EXPECT_EQ(r[5], 4);
    EXPECT_EQ(p[0], 0x80);
    EXPECT_EQ(p[1], 6);
    EXPECT_EQ(get_le16(p + 4), guest_conn);
    EXPECT_EQ(get_le16(p + 6), 0);
    EXPECT_EQ(get_le16(p + 8), 38);
    EXPECT_EQ(get_le16(p + 12), 38);
    EXPECT_EQ(get_le16(p + 14), 1);
    EXPECT_EQ(memcmp(p + 18, nb_name(VSERVER_SERVER_NAME, 0x20).data(), 16), 0);
    EXPECT_EQ(memcmp(p + 34, nb_name("GUEST", 0).data(), 16), 0);
    EXPECT_EQ(get_le16(p + 50), 1024);
    EXPECT_EQ(get_le16(p + 52), 0x1234);
    EXPECT_EQ(get_le16(p + 54), 0x5678);
}

TEST_F(Ipx, ARepeatedRequestKeepsItsSession)
{
    open_session();
    bytes init = init_data();
    in(ipx_packet(4, server_node, 0x455, 0x455, session_frame(0x40, 6, guest_conn, 0xFFFF, 0, 38, 0, 0, init)));
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(get_le16(payload(sent[0]) + 2), server_conn);
}

TEST_F(Ipx, RefusesSessionsToOtherNames)
{
    bytes init;
    put_str(init, nb_name("GUEST", 0));
    put_str(init, nb_name(VSERVER_WORKGROUP_NAME, 0x1D));
    init.resize(38, 0);
    in(ipx_packet(4, server_node, 0x455, 0x455, session_frame(0x40, 6, guest_conn, 0xFFFF, 0, 38, 0, 0, init)));
    EXPECT_TRUE(sent.empty());
}

TEST_F(Ipx, CarriesSmbOverASession)
{
    open_session();
    bytes reply = exchange(negotiate());
    ASSERT_GT(reply.size(), 32u);
    EXPECT_EQ(reply[4], 0x72);
    EXPECT_EQ(reply[9] & 0x80, 0x80);
    EXPECT_EQ(reply[32], 17);

    /* The next message moves on a sequence number. */
    reply = exchange(smb(0x2B, 0, { 1 }, { 'h', 'i' }));
    EXPECT_EQ(reply[4], 0x2B);
}

TEST_F(Ipx, ReassemblesASplitRequest)
{
    open_session();
    bytes reply = exchange(negotiate(), 7);
    ASSERT_GT(reply.size(), 32u);
    EXPECT_EQ(reply[4], 0x72);
}

TEST_F(Ipx, SplitsAReplyToTheAgreedSize)
{
    open_session(64);
    bytes reply = exchange(smb(0x2B, 0, { 1 }, bytes(1000, 0x55)));
    ASSERT_EQ(reply.size(), 32u + 1u + 2u + 2u + 1000u);
    EXPECT_EQ(reply[37], 0x55);
}

TEST_F(Ipx, CarriesAWholeFile)
{
    open_session();
    exchange(negotiate());
    exchange(smb(0x73, 0, { 0xFF, 0, 4356, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0 }));
    bytes tree;
    put_str(tree, "\\\\86BOXHOST\\SHARED");
    tree.insert(tree.end(), { 0, 0 });
    put_str(tree, "?????");
    tree.push_back(0);
    bytes reply = exchange(smb(0x75, 0, { 0xFF, 0, 0, 1 }, tree));
    uint16_t tid = get_le16(&reply[24]);

    bytes name;
    put_str(name, "\\DOCS\\PATTERN.TXT");
    name.push_back(0);
    reply = exchange(smb(0x2D, tid, { 0xFF, 0, 0, 0, 0x16, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0 }, name));
    ASSERT_EQ(reply[32], 15);
    uint16_t fid = get_le16(&reply[37]);

    size_t got = 0;
    while (got < 35840) {
        reply = exchange(smb(0x2E, tid, { 0xFF, 0, fid, uint16_t(got), uint16_t(got >> 16), 4096, 4096, 0, 0, 0, 0 }));
        ASSERT_EQ(reply[32], 12);
        uint16_t length = get_le16(&reply[32 + 1 + 10]);
        ASSERT_GT(length, 0);
        got += length;
    }
    EXPECT_EQ(got, 35840u);
}

TEST_F(Ipx, AcknowledgesARepeatWithoutRunningItTwice)
{
    open_session();
    exchange(smb(0x2B, 0, { 1 }, { 1 }));
    sent.clear();
    bytes again = smb(0x2B, 0, { 1 }, { 1 });
    in(ipx_packet(4, server_node, 0x455, 0x455,
                  session_frame(0x50, 6, guest_conn, server_conn, uint16_t(tx_seq - 1), uint16_t(again.size()), 0,
                                rx_seq, again)));
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(payload(sent[0])[0], 0x80);
}

TEST_F(Ipx, AsksForAResendAfterAGap)
{
    open_session();
    in(ipx_packet(4, server_node, 0x455, 0x455,
                  session_frame(0x50, 6, guest_conn, server_conn, uint16_t(tx_seq + 2), 3, 0, rx_seq, { 1, 2, 3 })));
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(payload(sent[0])[0], 0x88);
}

TEST_F(Ipx, AnswersAProbe)
{
    open_session();
    ack_with(0xC0, rx_seq, 0);
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(payload(sent[0])[0], 0x80);
}

TEST_F(Ipx, RetransmitsThenGivesUp)
{
    open_session();
    bytes message = smb(0x2B, 0, { 1 }, { 1 });
    in(ipx_packet(4, server_node, 0x455, 0x455,
                  session_frame(0x50, 6, guest_conn, server_conn, tx_seq, uint16_t(message.size()), 0, rx_seq, message)));
    ASSERT_EQ(sent.size(), 2u); /* the acknowledgement and the reply */
    bytes reply = sent[1];
    sent.clear();
    tick(1999);
    EXPECT_EQ(session_packets(), 0u);
    tick(1);
    ASSERT_EQ(session_packets(), 1u);
    sent.clear();
    tick(2000 * 12);
    EXPECT_EQ(session_packets(), 9u);

    /* The session has gone, so nothing answers it now. */
    sent.clear();
    ack_with(0xC0, rx_seq, 0);
    EXPECT_TRUE(sent.empty());
}

TEST_F(Ipx, ResendsFromWhereTheGuestSaysItGotTo)
{
    open_session(64);
    bytes message = smb(0x2B, 0, { 1 }, bytes(100, 7));
    in(ipx_packet(4, server_node, 0x455, 0x455,
                  session_frame(0x50, 6, guest_conn, server_conn, tx_seq, uint16_t(message.size()), 0, rx_seq, message)));
    ASSERT_EQ(sent.size(), 2u);
    sent.clear();
    ack_with(0x88, rx_seq, 0);
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(get_le16(payload(sent[0]) + 10), 0);
    sent.clear();
    ack_with(0x80, rx_seq, 64);
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(get_le16(payload(sent[0]) + 10), 64);
}

TEST_F(Ipx, IgnoresAPeerWithTheWrongConnection)
{
    open_session();
    in(ipx_packet(4, server_node, 0x455, 0x455,
                  session_frame(0xC0, 6, uint16_t(guest_conn + 1), server_conn, tx_seq, 0, 0, rx_seq)));
    EXPECT_TRUE(sent.empty());
}

TEST_F(Ipx, EndingASessionFreesIt)
{
    open_session();
    in(ipx_packet(4, server_node, 0x455, 0x455, session_frame(0x40, 7, guest_conn, server_conn, tx_seq, 0, 0, rx_seq)));
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(payload(sent[0])[0], 0x80);
    EXPECT_EQ(payload(sent[0])[1], 8);
    sent.clear();
    ack_with(0xC0, rx_seq, 0);
    EXPECT_TRUE(sent.empty());
}

TEST_F(Ipx, RunsOutOfSessionsByDroppingTheStalest)
{
    for (int i = 0; i < 9; i++) {
        guest_conn = uint16_t(0x200 + i);
        open_session();
        EXPECT_NE(server_conn, 0);
        tick(1);
    }
}

TEST_F(Ipx, ResetForgetsSessions)
{
    open_session();
    ipx_reset(ipx);
    ack_with(0xC0, rx_seq, 0);
    EXPECT_TRUE(sent.empty());
}

TEST_F(Ipx, TakesDatagramsWithAndWithoutRoutes)
{
    bytes datagram = { 0, 11 };
    put_str(datagram, nb_name(VSERVER_WORKGROUP_NAME, 0x1D));
    put_str(datagram, nb_name("GUEST", 0));
    datagram.resize(100, 0);
    EXPECT_TRUE(in(ipx_packet(4, server_node, 0x455, 0x455, datagram)));
    bytes routed(32, 0);
    routed.insert(routed.end(), datagram.begin(), datagram.end());
    EXPECT_TRUE(in(ipx_packet(20, everyone, 0x455, 0x455, routed)));
}

TEST_F(Ipx, AnnouncesOverWhicheverTransportTheGuestUsed)
{
    tick(120000);
    EXPECT_TRUE(sent.empty());
    in(ipx_packet(20, everyone, 0x455, 0x455, name_frame(1, nb_name("NOSUCHNAME", 0))));
    tick(3000);
    ASSERT_EQ(sent.size(), 2u);
    EXPECT_EQ(sent[0][5], 20);
    EXPECT_EQ(get_be16(sent[0].data() + 16), 0x455);
    EXPECT_EQ(memcmp(sent[0].data() + 10, everyone, 6), 0);
    EXPECT_EQ(payload(sent[0])[33], 11);
    EXPECT_EQ(memcmp(payload(sent[0]) + 34, nb_name(VSERVER_WORKGROUP_NAME, 0x1D).data(), 16), 0);
}

TEST_F(Ipx, FindsTheServerByDirectHostingName)
{
    bytes query(32, 0);
    query.insert(query.end(), { 0xF3, 1, 0x34, 0x12 });
    bytes name(16, ' ');
    memcpy(name.data(), VSERVER_SERVER_NAME, sizeof(VSERVER_SERVER_NAME) - 1);
    query.insert(query.end(), name.begin(), name.end());
    bytes source(16, ' ');
    memcpy(source.data(), "GUEST", 5);
    query.insert(query.end(), source.begin(), source.end());
    EXPECT_TRUE(in(ipx_packet(20, everyone, 0x551, 0x551, query)));
    ASSERT_EQ(sent.size(), 1u);
    const uint8_t *p = payload(sent[0]);
    EXPECT_EQ(sent[0].size(), 30u + 68u);
    EXPECT_EQ(p[32], 0xF4);
    EXPECT_EQ(p[33], 1);
    EXPECT_EQ(get_le16(p + 34), 0x1234);
    EXPECT_EQ(memcmp(p + 36, name.data(), 16), 0);
    EXPECT_EQ(memcmp(p + 52, VSERVER_SERVER_NAME, sizeof(VSERVER_SERVER_NAME) - 1), 0);
    EXPECT_EQ(get_be16(sent[0].data() + 16), 0x551);
}

TEST_F(Ipx, ALowerCaseOrNulPaddedNameIsStillItsName)
{
    bytes query(32, 0);
    query.insert(query.end(), { 0xF3, 2, 0, 0 });
    bytes name(16, 0);
    memcpy(name.data(), "workgroup", 9);
    query.insert(query.end(), name.begin(), name.end());
    query.resize(68, 0);
    in(ipx_packet(20, everyone, 0x551, 0x551, query));
    EXPECT_EQ(sent.size(), 1u);
    sent.clear();
    memcpy(&query[36], "NOSUCHNAME", 10);
    in(ipx_packet(20, everyone, 0x551, 0x551, query));
    EXPECT_TRUE(sent.empty());
}

TEST_F(Ipx, NegotiatesAConnectionAndAPacketSizedBuffer)
{
    bytes reply = direct(negotiate(), 0, 0);
    ASSERT_GT(reply.size(), 32u);
    EXPECT_NE(get_le16(&reply[18]), 0);
    EXPECT_EQ(reply[32], 17);
    uint32_t buffer = reply[33 + 7] | (reply[33 + 8] << 8);
    EXPECT_LE(buffer + 30, IPX_MAX_PACKET);
    EXPECT_GE(buffer, 1024u);
}

TEST_F(Ipx, TakesItsBufferSizeFromTheLink)
{
    ipx_configure(ipx, IPX_NETWORK, server_node, 576);
    bytes reply = direct(negotiate(), 0, 0);
    uint32_t buffer = reply[33 + 7] | (reply[33 + 8] << 8);
    EXPECT_EQ(buffer, 576u - 30u);
}

TEST_F(Ipx, AnUnknownConnectionIsToldSo)
{
    bytes reply = direct(smb(0x2B, 0, { 1 }, { 1 }), 0x7777, 0);
    ASSERT_GE(reply.size(), 32u);
    EXPECT_EQ(reply[5], 2);
    EXPECT_EQ(get_le16(&reply[7]), 0x10);
}

TEST_F(Ipx, UnsequencedCommandsAreJustAnswered)
{
    uint16_t cid = get_le16(&direct(negotiate(), 0, 0)[18]);
    bytes reply = direct(smb(0x2B, 0, { 1 }, { 1 }), cid, 0);
    ASSERT_GT(reply.size(), 32u);
    EXPECT_EQ(reply[4], 0x2B);
    EXPECT_EQ(get_le16(&reply[18]), cid);
    EXPECT_EQ(get_le16(&reply[20]), 0);
}

TEST_F(Ipx, RepeatsASequencedReplyRatherThanRunningItAgain)
{
    uint16_t cid = get_le16(&direct(negotiate(), 0, 0)[18]);
    bytes first = direct(smb(0x2B, 0, { 1 }, { 1 }), cid, 1);
    ASSERT_GT(first.size(), 32u);
    EXPECT_EQ(get_le16(&first[20]), 1);
    bytes again = direct(smb(0x2B, 0, { 1 }, { 2 }), cid, 1);
    EXPECT_EQ(first, again);
}

TEST_F(Ipx, IgnoresASequenceNumberOutOfOrder)
{
    uint16_t cid = get_le16(&direct(negotiate(), 0, 0)[18]);
    direct(smb(0x2B, 0, { 1 }, { 1 }), cid, 1);
    EXPECT_TRUE(direct(smb(0x2B, 0, { 1 }, { 1 }), cid, 3).empty());
    EXPECT_FALSE(direct(smb(0x2B, 0, { 1 }, { 1 }), cid, 2).empty());
}

TEST_F(Ipx, SequenceNumbersWrapPastZero)
{
    uint16_t cid = get_le16(&direct(negotiate(), 0, 0)[18]);
    direct(smb(0x2B, 0, { 1 }, { 1 }), cid, 0xFFFF);
    EXPECT_FALSE(direct(smb(0x2B, 0, { 1 }, { 1 }), cid, 1).empty());
}

TEST_F(Ipx, AnotherNegotiateStartsOver)
{
    uint16_t first  = get_le16(&direct(negotiate(), 0, 0)[18]);
    uint16_t second = get_le16(&direct(negotiate(), 0, 0)[18]);
    EXPECT_NE(first, second);
    EXPECT_EQ(direct(smb(0x2B, 0, { 1 }, { 1 }), first, 0)[5], 2);
}

TEST_F(Ipx, FailsAReplyThatWouldNotFit)
{
    ipx_configure(ipx, IPX_NETWORK, server_node, 576);
    uint16_t cid = get_le16(&direct(negotiate(), 0, 0)[18]);
    bytes reply = direct(smb(0x2B, 0, { 1 }, bytes(530, 1)), cid, 0);
    ASSERT_FALSE(reply.empty());
    EXPECT_LE(reply.size(), 576u - 30u);
}

TEST_F(Ipx, ReadsWholeFilesInPacketSizedPieces)
{
    uint16_t cid = get_le16(&direct(negotiate(), 0, 0)[18]);
    direct(smb(0x73, 0, { 0xFF, 0, 1400, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0 }), cid, 1);
    bytes tree;
    put_str(tree, "\\\\86BOXHOST\\SHARED");
    tree.insert(tree.end(), { 0, 0 });
    put_str(tree, "?????");
    tree.push_back(0);
    bytes reply = direct(smb(0x75, 0, { 0xFF, 0, 0, 1 }, tree), cid, 2);
    uint16_t tid = get_le16(&reply[24]);
    bytes name;
    put_str(name, "\\DOCS\\PATTERN.TXT");
    name.push_back(0);
    reply = direct(smb(0x2D, tid, { 0xFF, 0, 0, 0, 0x16, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0 }, name), cid, 3);
    ASSERT_EQ(reply[32], 15);
    uint16_t fid = get_le16(&reply[37]);

    size_t got = 0;
    while (got < 35840) {
        reply = direct(smb(0x2E, tid, { 0xFF, 0, fid, uint16_t(got), uint16_t(got >> 16), 4096, 4096, 0, 0, 0, 0 }), cid, 0);
        ASSERT_EQ(reply[32], 12);
        ASSERT_LE(reply.size() + 30, IPX_MAX_PACKET);
        uint16_t length = get_le16(&reply[32 + 1 + 10]);
        ASSERT_GT(length, 0);
        got += length;
    }
    EXPECT_EQ(got, 35840u);
}

TEST_F(Ipx, ForgetsAnIdleClient)
{
    uint16_t cid = get_le16(&direct(negotiate(), 0, 0)[18]);
    tick(10 * 60 * 1000);
    EXPECT_EQ(direct(smb(0x2B, 0, { 1 }, { 1 }), cid, 0)[5], 2);
}
