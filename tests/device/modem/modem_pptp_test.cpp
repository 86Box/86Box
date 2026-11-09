/* PPTP on the virtual server, driven from the guest's side of the wire: TCP to port 1723,
   then PPP in GRE all the way to a ping of the server inside the tunnel. */
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include <86box/modem/modem_ppp.h>
#include <86box/network/net_vserver.h>
#include <86box/modem/modem_pptp.h>
#include <86box/network/net_vserver_config.h>

typedef std::vector<uint8_t> bytes;

static const uint32_t server_ip = 0x0A000204;
static const uint32_t guest_ip  = 0x0A00020F;
static const int      GRE_ACK_WAIT = 100;

static void put16(bytes &b, uint16_t v) { b.push_back(uint8_t(v >> 8)); b.push_back(uint8_t(v)); }
static void put32(bytes &b, uint32_t v) { put16(b, uint16_t(v >> 16)); put16(b, uint16_t(v)); }
static uint16_t get16(const uint8_t *p) { return uint16_t((p[0] << 8) | p[1]); }
static uint32_t get32(const uint8_t *p) { return (uint32_t(get16(p)) << 16) | get16(p + 2); }

static void
capture(void *priv, const uint8_t *packet, int len)
{
    static_cast<std::vector<bytes> *>(priv)->emplace_back(packet, packet + len);
}

static uint16_t
checksum(uint32_t sum, const uint8_t *data, size_t length)
{
    for (size_t i = 0; i + 1 < length; i += 2)
        sum += get16(data + i);
    if (length & 1)
        sum += uint32_t(data[length - 1]) << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return uint16_t(~sum);
}

static bytes
ip_packet(uint8_t protocol, const bytes &payload, uint32_t from = guest_ip, uint32_t to = server_ip)
{
    bytes p = { 0x45, 0 };
    put16(p, uint16_t(20 + payload.size()));
    put32(p, 0);
    p.push_back(64);
    p.push_back(protocol);
    put16(p, 0);
    put32(p, from);
    put32(p, to);
    uint16_t sum = checksum(0, p.data(), 20);
    p[10] = uint8_t(sum >> 8);
    p[11] = uint8_t(sum);
    p.insert(p.end(), payload.begin(), payload.end());
    return p;
}

static bytes
tcp_segment(uint16_t from, uint16_t to, uint32_t seq, uint32_t ack, uint8_t flags, const bytes &data = {})
{
    bytes t;
    put16(t, from);
    put16(t, to);
    put32(t, seq);
    put32(t, ack);
    t.push_back(5 << 4);
    t.push_back(flags);
    put16(t, 8192);
    put16(t, 0);
    put16(t, 0);
    t.insert(t.end(), data.begin(), data.end());
    return ip_packet(6, t);
}

static bytes
control(uint16_t type, const bytes &body, size_t length)
{
    bytes m;
    put16(m, uint16_t(length));
    put16(m, 1);
    put32(m, 0x1A2B3C4D);
    put16(m, type);
    put16(m, 0);
    m.insert(m.end(), body.begin(), body.end());
    m.resize(length, 0);
    return m;
}

class Pptp : public ::testing::Test {
protected:
    void SetUp() override {
        server = vserver_init(nullptr, server_ip, true, true, false, false, capture, &sent);
        ASSERT_NE(server, nullptr);
        ASSERT_TRUE(pptp_attach(server, nullptr, false, false));
    }
    void TearDown() override { vserver_close(server); }

    void in(const bytes &packet) { vserver_process_ipv4(server, packet.data(), int(packet.size())); }
    void tick(int ms) { for (int i = 0; i < ms; i++) vserver_tick(server); }

    void connect() {
        in(tcp_segment(1024, PPTP_PORT, 100, 0, 0x02));
        ASSERT_EQ(sent.size(), 1u);
        ASSERT_EQ(sent[0][20 + 13] & 0x12, 0x12);
        server_seq = get32(&sent[0][20 + 4]) + 1;
        guest_seq  = 101;
        in(tcp_segment(1024, PPTP_PORT, guest_seq, server_seq, 0x10));
        sent.clear();
    }

    /* Sends a control message and returns those that came back, in order. */
    std::vector<bytes> talk(const bytes &message) {
        in(tcp_segment(1024, PPTP_PORT, guest_seq, server_seq, 0x18, message));
        guest_seq += uint32_t(message.size());
        return replies();
    }

    std::vector<bytes> replies() {
        std::vector<bytes> found;
        std::vector<bytes> rest;
        for (const bytes &packet : sent) {
            if (packet[9] == 6) {
                size_t header = 20 + (packet[20 + 12] >> 4) * 4;
                if (packet.size() > header) {
                    found.emplace_back(packet.begin() + header, packet.end());
                    server_seq += uint32_t(packet.size() - header);
                } else {
                    rest.push_back(packet);
                }
            } else {
                rest.push_back(packet);
            }
        }
        sent.swap(rest);
        return found;
    }

    void start_call() {
        connect();
        bytes body;
        put16(body, 0x0100);
        auto reply = talk(control(1, body, 156));
        ASSERT_EQ(reply.size(), 1u);
        ASSERT_EQ(get16(&reply[0][8]), 2);
        ASSERT_EQ(reply[0][14], 1);

        bytes call;
        put16(call, peer_call);
        put16(call, 1);
        put32(call, 300);
        put32(call, 10000000);
        put32(call, 3);
        put32(call, 3);
        put16(call, 64);
        reply = talk(control(7, call, 168));
        ASSERT_EQ(reply.size(), 1u);
        ASSERT_EQ(get16(&reply[0][8]), 8);
        ASSERT_EQ(reply[0][16], 1);
        call_id = get16(&reply[0][12]);
        EXPECT_EQ(get16(&reply[0][14]), peer_call);
    }

    /* Sends a PPP frame through the tunnel. */
    void send_ppp(uint16_t protocol, const bytes &payload) {
        bytes frame = { 0xFF, 0x03 };
        put16(frame, protocol);
        frame.insert(frame.end(), payload.begin(), payload.end());
        bytes gre = { 0x30, 0x81 };
        put16(gre, 0x880B);
        put16(gre, uint16_t(frame.size()));
        put16(gre, call_id);
        put32(gre, gre_seq++);
        put32(gre, last_server_seq);
        gre.insert(gre.end(), frame.begin(), frame.end());
        in(ip_packet(47, gre));
    }

    /* Takes the PPP frames that have come out of the tunnel. */
    std::vector<bytes> take_ppp() {
        std::vector<bytes> frames;
        for (const bytes &packet : sent) {
            if (packet[9] != 47)
                continue;
            const uint8_t *gre = packet.data() + 20;
            EXPECT_EQ(gre[0] & 0x20, 0x20);
            EXPECT_EQ(gre[1] & 0x7F, 1);
            EXPECT_EQ(get16(gre + 2), 0x880B);
            EXPECT_EQ(get16(gre + 6), peer_call);
            size_t pos = 8;
            if (gre[0] & 0x10) {
                last_server_seq = get32(gre + pos);
                pos += 4;
            }
            if (gre[1] & 0x80)
                pos += 4;
            if (get16(gre + 4))
                frames.emplace_back(gre + pos, gre + pos + get16(gre + 4));
        }
        sent.clear();
        return frames;
    }

    /* Finds the first frame of a protocol and code, skipping the address and control bytes. */
    static const bytes *find(const std::vector<bytes> &frames, uint16_t protocol, uint8_t code) {
        for (const bytes &f : frames) {
            size_t pos = f[0] == 0xFF ? 2 : 0;
            uint16_t p;
            if (f[pos] & 1) {
                p = f[pos];
                pos++;
            } else {
                p = get16(&f[pos]);
                pos += 2;
            }
            if (p == protocol && f[pos] == code)
                return &f;
        }
        return nullptr;
    }

    static bytes info(const bytes &frame) {
        size_t pos = frame[0] == 0xFF ? 2 : 0;
        pos += (frame[pos] & 1) ? 1 : 2;
        return bytes(frame.begin() + pos, frame.end());
    }

    /* The guest's half of bringing up the link, down to an address. */
    void bring_up_ppp() {
        std::vector<bytes> frames = take_ppp();
        const bytes *request = find(frames, 0xC021, 1);
        ASSERT_NE(request, nullptr) << "no LCP request";

        /* Agrees to everything except the method, asking for PAP instead. */
        bytes options = info(*request);
        uint8_t id = options[1];
        bytes nak = { 3, id, 0, 9, 3, 4, 0xC0, 0x23 };
        nak[3] = 8;
        nak.resize(8);
        send_ppp(0xC021, nak);
        frames = take_ppp();
        request = find(frames, 0xC021, 1);
        ASSERT_NE(request, nullptr) << "no second LCP request";
        bytes ack = info(*request);
        ack[0] = 2;
        send_ppp(0xC021, ack);

        send_ppp(0xC021, { 1, 7, 0, 4 });
        frames = take_ppp();
        ASSERT_NE(find(frames, 0xC021, 2), nullptr) << "LCP not acknowledged";

        bytes pap = { 1, 1, 0, 0, 5 };
        pap.insert(pap.end(), { '8', '6', 'B', 'o', 'x', 5, '8', '6', 'B', 'o', 'x' });
        pap[3] = uint8_t(pap.size());
        send_ppp(0xC023, pap);
        frames = take_ppp();
        ASSERT_NE(find(frames, 0xC023, 2), nullptr) << "PAP not accepted";

        request = find(frames, 0x8021, 1);
        if (!request) {
            tick(50);
            frames = take_ppp();
            request = find(frames, 0x8021, 1);
        }
        if (request) {
            bytes server_request = info(*request);
            server_request[0] = 2;
            send_ppp(0x8021, server_request);
        }

        send_ppp(0x8021, { 1, 2, 0, 10, 3, 6, 0, 0, 0, 0 });
        frames = take_ppp();
        const bytes *address = find(frames, 0x8021, 3);
        ASSERT_NE(address, nullptr) << "no address offered";
        bytes offered = info(*address);
        EXPECT_EQ(get32(&offered[6]), PPTP_GUEST_IP);
        send_ppp(0x8021, { 1, 3, 0, 10, 3, 6, offered[6], offered[7], offered[8], offered[9] });
        frames = take_ppp();
        ASSERT_NE(find(frames, 0x8021, 2), nullptr) << "address not acknowledged";
    }

    vserver_t         *server = nullptr;
    std::vector<bytes> sent;
    uint32_t           server_seq = 0;
    uint32_t           guest_seq = 0;
    uint16_t           peer_call = 0x4242;
    uint16_t           call_id = 0;
    uint32_t           gre_seq = 0;
    uint32_t           last_server_seq = 0;
};

TEST(PptpOff, Port1723IsClosedUntilAttached)
{
    std::vector<bytes> sent;
    vserver_t *server = vserver_init(nullptr, server_ip, true, true, false, false, capture, &sent);
    bytes syn = tcp_segment(1024, PPTP_PORT, 100, 0, 0x02);
    vserver_process_ipv4(server, syn.data(), int(syn.size()));
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0][20 + 13] & 0x04, 0x04);
    vserver_close(server);
}

TEST_F(Pptp, AnswersTheStartOfAControlConnection)
{
    connect();
    bytes body;
    put16(body, 0x0100);
    auto reply = talk(control(1, body, 156));
    ASSERT_EQ(reply.size(), 1u);
    const bytes &r = reply[0];
    ASSERT_EQ(r.size(), 156u);
    EXPECT_EQ(get16(&r[0]), 156);
    EXPECT_EQ(get16(&r[2]), 1);
    EXPECT_EQ(get32(&r[4]), 0x1A2B3C4Du);
    EXPECT_EQ(get16(&r[8]), 2);
    EXPECT_EQ(get16(&r[12]), 0x0100);
    EXPECT_EQ(r[14], 1);
    EXPECT_EQ(memcmp(&r[28], VSERVER_SERVER_NAME, sizeof(VSERVER_SERVER_NAME) - 1), 0);
    EXPECT_EQ(memcmp(&r[92], VSERVER_PPTP_VENDOR_NAME, sizeof(VSERVER_PPTP_VENDOR_NAME) - 1), 0);
}

TEST_F(Pptp, TakesAMessageInPieces)
{
    connect();
    bytes message = control(1, { 1, 0 }, 156);
    EXPECT_TRUE(talk(bytes(message.begin(), message.begin() + 5)).empty());
    EXPECT_TRUE(talk(bytes(message.begin() + 5, message.begin() + 100)).empty());
    EXPECT_EQ(talk(bytes(message.begin() + 100, message.end())).size(), 1u);
}

TEST_F(Pptp, RefusesASecondControlConnection)
{
    connect();
    sent.clear();
    in(tcp_segment(1025, PPTP_PORT, 500, 0, 0x02));
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0][20 + 13] & 0x04, 0x04);
}

TEST_F(Pptp, ClosesOnABadCookie)
{
    connect();
    bytes message = control(1, { 1, 0 }, 156);
    message[4] = 0;
    talk(message);
    ASSERT_FALSE(sent.empty());
    EXPECT_EQ(sent.back()[20 + 13] & 0x01, 0x01);
}

TEST_F(Pptp, EchoesTheIdentifier)
{
    connect();
    auto reply = talk(control(5, { 1, 2, 3, 4 }, 16));
    ASSERT_EQ(reply.size(), 1u);
    EXPECT_EQ(reply[0].size(), 20u);
    EXPECT_EQ(get16(&reply[0][8]), 6);
    EXPECT_EQ(get32(&reply[0][12]), 0x01020304u);
    EXPECT_EQ(reply[0][16], 1);
}

TEST_F(Pptp, RefusesACallBeforeTheStart)
{
    connect();
    bytes call;
    put16(call, 1);
    EXPECT_TRUE(talk(control(7, call, 168)).empty());
}

TEST_F(Pptp, TakesOneCallAtATime)
{
    start_call();
    bytes call;
    put16(call, 99);
    auto reply = talk(control(7, call, 168));
    ASSERT_EQ(reply.size(), 1u);
    EXPECT_EQ(reply[0][16], 7);
    EXPECT_EQ(get16(&reply[0][14]), 99);
}

TEST_F(Pptp, StartsPppAsSoonAsTheCallIsUp)
{
    start_call();
    auto frames = take_ppp();
    EXPECT_NE(find(frames, 0xC021, 1), nullptr);
}

TEST_F(Pptp, IgnoresGreForAnotherCall)
{
    start_call();
    take_ppp();
    uint16_t right = call_id;
    call_id = uint16_t(right + 1);
    send_ppp(0xC021, { 1, 7, 0, 4 });
    call_id = right;
    EXPECT_TRUE(take_ppp().empty());
}

TEST_F(Pptp, IgnoresALateOrRepeatedPacket)
{
    start_call();
    take_ppp();
    send_ppp(0xC021, { 1, 7, 0, 4 });
    EXPECT_NE(find(take_ppp(), 0xC021, 2), nullptr);
    gre_seq--;
    send_ppp(0xC021, { 1, 8, 0, 4 });
    EXPECT_EQ(find(take_ppp(), 0xC021, 2), nullptr);
}

TEST_F(Pptp, AcknowledgesOnItsOwnWhenThereIsNothingToSend)
{
    start_call();
    take_ppp();
    send_ppp(0xC021, { 11, 1, 0, 8, 0, 0, 0, 0 }); /* a Discard-Request draws no answer */
    EXPECT_TRUE(sent.empty());
    tick(GRE_ACK_WAIT);
    ASSERT_EQ(sent.size(), 1u);
    ASSERT_EQ(sent[0].size(), 20u + 12u);
    const uint8_t *gre = sent[0].data() + 20;
    EXPECT_EQ(gre[0], 0x20);
    EXPECT_EQ(gre[1], 0x81);
    EXPECT_EQ(get16(gre + 4), 0);
    EXPECT_EQ(get32(gre + 8), gre_seq - 1);
    sent.clear();
    tick(1000);
    for (const bytes &packet : sent)
        EXPECT_TRUE(packet[20] & 0x10); /* nothing further to acknowledge */
}

TEST_F(Pptp, CarriesAPingToTheServerInTheTunnel)
{
    start_call();
    bring_up_ppp();

    bytes icmp = { 8, 0, 0, 0, 0, 1, 0, 1, 'h', 'i' };
    uint16_t sum = checksum(0, icmp.data(), icmp.size());
    icmp[2] = uint8_t(sum >> 8);
    icmp[3] = uint8_t(sum);
    send_ppp(0x0021, ip_packet(1, icmp, PPTP_GUEST_IP, PPTP_SERVER_IP));

    auto frames = take_ppp();
    ASSERT_FALSE(frames.empty());
    const bytes &frame = frames[0];
    size_t pos = frame[0] == 0xFF ? 2 : 0;
    pos += (frame[pos] & 1) ? 1 : 2;
    ASSERT_GE(frame.size(), pos + 30);
    EXPECT_EQ(frame[pos + 9], 1);
    EXPECT_EQ(get32(&frame[pos + 12]), PPTP_SERVER_IP);
    EXPECT_EQ(get32(&frame[pos + 16]), PPTP_GUEST_IP);
    EXPECT_EQ(frame[pos + 20], 0);
    EXPECT_EQ(frame[pos + 28], 'h');
}

TEST_F(Pptp, ClearingACallNotifiesTheGuestAndAllowsAnother)
{
    start_call();
    bytes clear;
    put16(clear, peer_call);
    auto reply = talk(control(12, clear, 16));
    ASSERT_EQ(reply.size(), 1u);
    EXPECT_EQ(get16(&reply[0][8]), 13);
    EXPECT_EQ(reply[0].size(), 148u);
    EXPECT_EQ(get16(&reply[0][12]), call_id);
    EXPECT_EQ(reply[0][14], 4);
    sent.clear();

    bytes call;
    put16(call, 77);
    reply = talk(control(7, call, 168));
    ASSERT_EQ(reply.size(), 1u);
    EXPECT_EQ(reply[0][16], 1);
    EXPECT_NE(get16(&reply[0][12]), call_id);
}

TEST_F(Pptp, TellsTheGuestWhenPppEnds)
{
    start_call();
    take_ppp();
    send_ppp(0xC021, { 5, 1, 0, 4 }); /* Terminate-Request */
    sent.clear();
    bool notified = false;
    for (int i = 0; i < 12000 && !notified; i++) {
        tick(1);
        for (const bytes &reply : replies())
            notified = notified || get16(&reply[8]) == 13;
    }
    EXPECT_TRUE(notified);
}

TEST_F(Pptp, StoppingClosesTheConnectionAndForgetsTheCall)
{
    start_call();
    auto reply = talk(control(3, { 1 }, 16));
    ASSERT_EQ(reply.size(), 1u);
    EXPECT_EQ(get16(&reply[0][8]), 4);
    EXPECT_EQ(reply[0][12], 1);
    ASSERT_FALSE(sent.empty());
    EXPECT_EQ(sent.back()[20 + 13] & 0x01, 0x01);
}

TEST_F(Pptp, ResetOfTheConnectionDropsTheCall)
{
    start_call();
    in(tcp_segment(1024, PPTP_PORT, guest_seq, server_seq, 0x04));
    sent.clear();
    tick(100);
    EXPECT_TRUE(take_ppp().empty());
    connect();
}
