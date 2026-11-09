/* NetBEUI for the virtual server, driven the way a guest's stack drives it: LLC connection,
   NetBIOS name lookup, session, then SMB messages in data frames. */
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include <86box/network/net_vserver_config.h>
#include <86box/network/net_nbf.h>
#include <86box/network/net_nbns.h>
#include <86box/network/net_smb.h>
#include <86box/network/net_smb_fs.h>
#include <86box/modem/modem_crypto.h>

typedef std::vector<uint8_t> bytes;

extern "C" bool
ppp_random_bytes(uint8_t *buffer, uint8_t length)
{
    if (!buffer)
        return false;
    for (uint8_t i = 0; i < length; i++)
        buffer[i] = uint8_t(i + 1);
    return true;
}

static void put_le16(bytes &b, uint16_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); }
static void put_str(bytes &b, const std::string &s) { b.insert(b.end(), s.begin(), s.end()); b.push_back(0); }
static uint16_t get_le16(const uint8_t *p) { return uint16_t(p[0] | (p[1] << 8)); }

static void
write_le32(bytes &b, size_t offset, uint32_t value)
{
    b[offset] = uint8_t(value);
    b[offset + 1] = uint8_t(value >> 8);
    b[offset + 2] = uint8_t(value >> 16);
    b[offset + 3] = uint8_t(value >> 24);
}

static bytes
make_ras_response_20()
{
    const uint8_t challenge[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    const uint8_t magic[8] = { 'K', 'G', 'S', '!', '@', '#', '$', '%' };
    uint8_t upper_password[14] = { '8', '6', 'B', 'O', 'X' };
    uint8_t password_hash[16];
    uint8_t padded_hash[21] = {};
    bytes   response(333, 0);

    response[0] = 109;
    std::memcpy(response.data() + 1, "86Box", 5);
    modem_des_encrypt_block(upper_password, magic, password_hash);
    modem_des_encrypt_block(upper_password + 7, magic, password_hash + 8);
    std::memcpy(padded_hash, password_hash, sizeof(password_hash));
    for (size_t i = 0; i < 3; i++)
        modem_des_encrypt_block(padded_hash + i * 7, challenge, response.data() + 274 + i * 8);
    return response;
}

static void
capture(void *priv, const uint8_t *frame, int len)
{
    static_cast<std::vector<bytes> *>(priv)->emplace_back(frame, frame + len);
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
names_frame(uint8_t command, uint8_t data1, uint16_t data2, uint16_t response, const std::string &to,
            const std::string &from, const bytes &payload = {})
{
    bytes f = { 0xF0, 0xF0, 0x03 };
    put_le16(f, 44); put_le16(f, 0xEFFF);
    f.push_back(command); f.push_back(data1); put_le16(f, data2);
    put_le16(f, 0); put_le16(f, response);
    f.insert(f.end(), to.begin(), to.end());
    f.insert(f.end(), from.begin(), from.end());
    f.insert(f.end(), payload.begin(), payload.end());
    return f;
}

static bytes
session_header(uint8_t command, uint8_t data1, uint16_t data2, uint16_t transmit, uint16_t response,
               uint8_t destination, uint8_t source)
{
    bytes h;
    put_le16(h, 14); put_le16(h, 0xEFFF);
    h.push_back(command); h.push_back(data1); put_le16(h, data2);
    put_le16(h, transmit); put_le16(h, response);
    h.push_back(destination); h.push_back(source);
    return h;
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

class Nbf : public ::testing::Test {
protected:
    void SetUp() override {
        nbf = nbf_init(nullptr, capture, &sent);
        ASSERT_NE(nbf, nullptr);
    }
    void TearDown() override { nbf_close(nbf); }

    void in(const bytes &frame) { nbf_input(nbf, frame.data(), int(frame.size())); }

    void link() {
        in({ 0xF0, 0xF0, 0x7F });
        ASSERT_EQ(sent.size(), 1u);
        EXPECT_EQ(sent[0][1], 0xF1);
        EXPECT_EQ(sent[0][2], 0x73);
        sent.clear();
        ns = nr = 0;
    }

    /* Sends a numbered frame and takes in any numbered frames that came back, acknowledging
       them as a guest would. */
    void info(const bytes &payload) {
        bytes f = { 0xF0, 0xF0, uint8_t(ns << 1), uint8_t(nr << 1) };
        f.insert(f.end(), payload.begin(), payload.end());
        ns = uint8_t((ns + 1) % 128);
        in(f);
        collect();
    }

    void collect() {
        for (;;) {
            std::vector<bytes> arrived;
            arrived.swap(sent);
            bool got = false;
            for (const bytes &f : arrived) {
                if (f.size() >= 4 && !(f[2] & 1) && (f[2] >> 1) == nr) {
                    nr = uint8_t((nr + 1) % 128);
                    received.emplace_back(f.begin() + 4, f.end());
                    got = true;
                } else if (f.size() >= 3) {
                    other.push_back(f);
                }
            }
            if (!got)
                return;
            bytes rr = { 0xF0, 0xF1, 0x01, uint8_t(nr << 1) };
            in(rr);
        }
    }

    uint8_t open_session() {
        link();
        in(names_frame(0x0A, 0, 7, 0x1234, nb_name(VSERVER_SERVER_NAME, 0x20), nb_name("86BOX95", 0)));
        EXPECT_EQ(sent.size(), 1u);
        if (sent.empty())
            return 0;
        const bytes reply = sent[0];
        sent.clear();
        EXPECT_EQ(reply[3 + 4], 0x0E);
        EXPECT_EQ(get_le16(&reply[3 + 8]), 0x1234);
        uint8_t server = reply[3 + 6];
        EXPECT_NE(server, 0);
        info(session_header(0x19, 0x81, 1024, get_le16(&reply[3 + 10]), 0x2222, server, 7));
        EXPECT_EQ(received.size(), 1u);
        if (received.empty())
            return 0;
        EXPECT_EQ(received[0][4], 0x17);
        EXPECT_EQ(get_le16(&received[0][8]), 0x2222);
        EXPECT_EQ(received[0][12], 7);
        EXPECT_EQ(received[0][13], server);
        received.clear();
        return server;
    }

    /* Runs an SMB exchange over a session and returns the reply. */
    bytes exchange(uint8_t server, const bytes &message, uint16_t chunk = 900) {
        size_t pos = 0;

        received.clear();
        while (message.size() - pos > chunk) {
            bytes frame = session_header(0x15, 0x01, 0, 0, 0x3333, server, 7);
            frame.insert(frame.end(), message.begin() + long(pos), message.begin() + long(pos + chunk));
            pos += chunk;
            info(frame);
        }
        bytes frame = session_header(0x16, 0, 0, 0, 0x4444, server, 7);
        frame.insert(frame.end(), message.begin() + long(pos), message.end());
        info(frame);

        bytes reply;
        for (size_t i = 0; i < received.size(); i++) {
            const bytes &r = received[i];
            if (r[4] == 0x15 || r[4] == 0x16) {
                reply.insert(reply.end(), r.begin() + 14, r.end());
                if (r[4] == 0x16) {
                    bytes ack = session_header(0x14, 0, 0, get_le16(&r[10]), 0, server, 7);
                    size_t before = received.size();
                    info(ack);
                    (void) before;
                }
            }
        }
        return reply;
    }

    nbf_t             *nbf = nullptr;
    std::vector<bytes> sent, received, other;
    uint8_t            ns = 0, nr = 0;
};

TEST_F(Nbf, AnswersQueriesForTheServerOnly)
{
    in(names_frame(0x0A, 0, 0, 0x55, nb_name(VSERVER_SERVER_NAME, 0x20), nb_name("86BOX95", 0)));
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0][2], 0x03);
    EXPECT_EQ(sent[0][3 + 4], 0x0E);
    EXPECT_EQ(sent[0][3 + 6], 0); /* a FIND.NAME takes no session */
    EXPECT_EQ(memcmp(&sent[0][3 + 12], nb_name("86BOX95", 0).data(), 16), 0);
    EXPECT_EQ(memcmp(&sent[0][3 + 28], nb_name(VSERVER_SERVER_NAME, 0x20).data(), 16), 0);
    sent.clear();

    in(names_frame(0x0A, 0, 5, 0x55, nb_name("SOMEONE", 0x20), nb_name("86BOX95", 0)));
    EXPECT_TRUE(sent.empty());

    in(names_frame(0x0A, 0, 0, 0x55, nb_name(VSERVER_WORKGROUP_NAME, 0x1C), nb_name("86BOX95", 0)));
    EXPECT_EQ(sent.size(), 1u);
}

TEST_F(Nbf, NameQueryBeforeLlcConnectKeepsItsRasSession)
{
    ASSERT_TRUE(nbf_set_ras_credentials(nbf, "86Box", "86Box"));
    in(names_frame(0x0A, 0, 7, 0x1234, nb_name("DIALIN_GATEWAY", 0x20),
                   nb_name("86BOX95", 0)));
    ASSERT_EQ(sent.size(), 1u);
    const uint8_t server = sent[0][3 + 6];
    ASSERT_NE(server, 0);
    sent.clear();

    link();
    info(session_header(0x19, 0x81, 1024, 0x2345, 0x2222, server, 7));
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0][4], 0x17);
    EXPECT_EQ(get_le16(&received[0][8]), 0x2222);
    EXPECT_EQ(received[0][12], 7);
    EXPECT_EQ(received[0][13], server);

    bytes protocol(333, 0);
    protocol[0] = 100;
    protocol[1] = 20;
    bytes message = session_header(0x16, 0, 0, 0, 0x3456, server, 7);
    message.insert(message.end(), protocol.begin(), protocol.end());
    received.clear();
    info(message);

    bool     got_protocol_response = false;
    uint16_t protocol_correlator = 0;
    for (const bytes &frame : received) {
        if (frame.size() <= 14 || (frame[4] != 0x15 && frame[4] != 0x16))
            continue;
        if (frame[14] == 100) {
            got_protocol_response = true;
            protocol_correlator = get_le16(&frame[10]);
        }
    }
    EXPECT_TRUE(got_protocol_response);

    received.clear();
    info(session_header(0x14, 0, 0, protocol_correlator, 0, server, 7));
    bool got_challenge = false;
    for (const bytes &frame : received) {
        if (frame.size() > 14 && (frame[4] == 0x15 || frame[4] == 0x16))
            got_challenge |= frame[14] == 101;
    }
    EXPECT_TRUE(got_challenge);

    uint16_t challenge_correlator = 0;
    for (const bytes &frame : received) {
        if (frame.size() > 14 && (frame[4] == 0x15 || frame[4] == 0x16)
            && frame[14] == 101) {
            challenge_correlator = get_le16(&frame[10]);
            break;
        }
    }
    ASSERT_NE(challenge_correlator, 0);

    received.clear();
    info(session_header(0x14, 0, 0, challenge_correlator, 0, server, 7));
    bytes response(333, 0);
    response[0] = 109;
    std::memcpy(response.data() + 1, "86Box", 5);
    bytes auth_message = session_header(0x16, 0, 0, 0, 0x4567, server, 7);
    auth_message.insert(auth_message.end(), response.begin(), response.end());
    received.clear();
    info(auth_message);

    bool got_authentication_rejection = false;
    for (const bytes &frame : received) {
        if (frame.size() >= 17 && (frame[4] == 0x15 || frame[4] == 0x16)
            && frame[14] == 104) {
            got_authentication_rejection = frame[15] == 0x00 && frame[16] == 0x62;
        }
    }
    EXPECT_TRUE(got_authentication_rejection);
}

TEST_F(Nbf, RASProjectionRegistersNamesForNbfLookups)
{
    ASSERT_TRUE(nbf_set_ras_credentials(nbf, "86Box", "86Box"));
    in(names_frame(0x0A, 0, 7, 0x1234, nb_name("DIALIN_GATEWAY", 0x20),
                   nb_name("86BOX95", 0)));
    ASSERT_EQ(sent.size(), 1u);
    const uint8_t server = sent[0][3 + 6];
    ASSERT_NE(server, 0);
    sent.clear();

    link();
    info(session_header(0x19, 0x81, 1024, 0x2345, 0x2222, server, 7));
    ASSERT_EQ(received.size(), 1u);
    received.clear();

    auto find_ras_reply = [&](uint8_t expected_type) -> const bytes * {
        for (const bytes &reply : received) {
            if (reply.size() > 14 && (reply[4] == 0x15 || reply[4] == 0x16)
                && reply[14] == expected_type)
                return &reply;
        }
        return nullptr;
    };
    auto send_ras_message = [&](const bytes &payload, uint8_t expected_type) {
        bytes frame = session_header(0x16, 0, 0, 0, 0x3456, server, 7);
        frame.insert(frame.end(), payload.begin(), payload.end());
        received.clear();
        info(frame);
        const bytes *reply = find_ras_reply(expected_type);
        return reply ? get_le16(&(*reply)[10]) : uint16_t(0);
    };
    auto ack_ras_message = [&](uint16_t correlator) {
        received.clear();
        info(session_header(0x14, 0, 0, correlator, 0, server, 7));
    };

    bytes protocol(333, 0);
    protocol[0] = 100;
    protocol[1] = 20;
    uint16_t correlator = send_ras_message(protocol, 100);
    ASSERT_NE(correlator, 0);
    ack_ras_message(correlator);
    correlator = 0;
    for (const bytes &reply : received) {
        if (reply.size() > 14 && (reply[4] == 0x15 || reply[4] == 0x16)
            && reply[14] == 101) {
            correlator = get_le16(&reply[10]);
            break;
        }
    }
    ASSERT_NE(correlator, 0);
    ack_ras_message(correlator);

    correlator = send_ras_message(make_ras_response_20(), 104);
    ASSERT_NE(correlator, 0);
    const bytes *auth_reply = find_ras_reply(104);
    ASSERT_NE(auth_reply, nullptr);
    ASSERT_GE(auth_reply->size(), 17u);
    EXPECT_EQ((*auth_reply)[15], 0x00);
    EXPECT_EQ((*auth_reply)[16], 0x61);
    ack_ras_message(correlator);

    bytes configuration(333, 0);
    configuration[0] = 107;
    write_le32(configuration, 1, 20);
    write_le32(configuration, 19, 1514);
    write_le32(configuration, 23, 1514);
    write_le32(configuration, 27, 115200);
    write_le32(configuration, 31, 1);
    configuration[35] = 5;
    const char *names[] = { "86BOX95", "WORKGROUP", "86BOX95", "86BOX95", "WORKGROUP" };
    const uint8_t suffixes[] = { 0x00, 0x00, 0x03, 0x20, 0x1E };
    const uint8_t flags[] = { 1, 2, 1, 1, 2 };
    for (size_t i = 0; i < 5; i++) {
        const std::string name = nb_name(names[i], suffixes[i]);
        const size_t offset = 37 + i * 18;
        std::memcpy(configuration.data() + offset, name.data(), name.size());
        configuration[offset + 16] = flags[i];
    }
    correlator = send_ras_message(configuration, 108);
    ASSERT_NE(correlator, 0);
    const bytes *configuration_reply = find_ras_reply(108);
    ASSERT_NE(configuration_reply, nullptr);
    ASSERT_EQ(configuration_reply->size(), 347u);
    EXPECT_EQ((*configuration_reply)[48], 5);
    EXPECT_EQ((*configuration_reply)[49], 0);
    ack_ras_message(correlator);

    correlator = 0;
    for (const bytes &reply : received) {
        if (reply.size() > 14 && (reply[4] == 0x15 || reply[4] == 0x16)
            && reply[14] == 113) {
            correlator = get_le16(&reply[10]);
            break;
        }
    }
    ASSERT_NE(correlator, 0);
    ack_ras_message(correlator);

    in(names_frame(0x0A, 0, 0, 0x7788, nb_name("86BOX95", 0x20), nb_name("PEER", 0)));
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0][3 + 4], 0x0E);
    EXPECT_EQ(get_le16(&sent[0][3 + 6]), 0);
    EXPECT_EQ(memcmp(&sent[0][3 + 28], nb_name("86BOX95", 0x20).data(), 16), 0);

    info(session_header(0x18, 0, 0, 0, 0, server, 7));
    sent.clear();
    in(names_frame(0x0A, 0, 0, 0x7789, nb_name("86BOX95", 0x20), nb_name("PEER", 0)));
    EXPECT_TRUE(sent.empty());
}

TEST_F(Nbf, ARepeatedQueryKeepsItsSession)
{
    in(names_frame(0x0A, 0, 7, 0x55, nb_name(VSERVER_SERVER_NAME, 0x20), nb_name("86BOX95", 0)));
    in(names_frame(0x0A, 0, 7, 0x55, nb_name(VSERVER_SERVER_NAME, 0x20), nb_name("86BOX95", 0)));
    ASSERT_EQ(sent.size(), 2u);
    EXPECT_EQ(sent[0][3 + 6], sent[1][3 + 6]);
}

TEST_F(Nbf, SessionsRunOutPolitely)
{
    for (int i = 1; i <= 9; i++)
        in(names_frame(0x0A, 0, uint16_t(i), 0x55, nb_name(VSERVER_SERVER_NAME, 0x20), nb_name("86BOX95", 0)));
    ASSERT_EQ(sent.size(), 9u);
    EXPECT_EQ(sent[8][3 + 6], 0xFF);
}

TEST_F(Nbf, DefendsItsNames)
{
    in(names_frame(0x01, 0, 0, 0x77, std::string(16, '\0'), nb_name(VSERVER_SERVER_NAME, 0x20)));
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0][3 + 4], 0x0D);
    EXPECT_EQ(get_le16(&sent[0][3 + 8]), 0x77);
    sent.clear();
    in(names_frame(0x01, 0, 0, 0x77, std::string(16, '\0'), nb_name("86BOX95", 0)));
    EXPECT_TRUE(sent.empty());
}

TEST_F(Nbf, RefusesNumberedFramesWithoutALink)
{
    in({ 0xF0, 0xF0, 0, 0, 1, 2, 3 });
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0][2], 0x1F);
}

TEST_F(Nbf, EchoesXidAndTest)
{
    in({ 0xF0, 0xF0, 0xBF, 0x81, 0x01, 0x02 });
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0], bytes({ 0xF0, 0xF1, 0xBF, 0x81, 0x01, 0x02 }));
    sent.clear();
    in({ 0xF0, 0xF0, 0xF3, 'h', 'i' });
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0][1], 0xF1);
    EXPECT_EQ(sent[0][3], 'h');
}

TEST_F(Nbf, OpensASession)
{
    EXPECT_NE(open_session(), 0);
}

TEST_F(Nbf, SessionInitForAnUnknownSessionIsEnded)
{
    link();
    info(session_header(0x19, 0, 1024, 0, 1, 3, 7));
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0][4], 0x18);
}

TEST_F(Nbf, CarriesSmbWithAcknowledgements)
{
    uint8_t server = open_session();
    bytes   dialects = { 2 };
    put_str(dialects, "NT LM 0.12");
    bytes   reply = exchange(server, smb(0x72, 0, {}, dialects));

    ASSERT_GE(reply.size(), 37u);
    EXPECT_EQ(memcmp(reply.data(), "\xFFSMB\x72", 5), 0);
    EXPECT_EQ(reply[32], 17);
    EXPECT_EQ(received[0][4], 0x14); /* the guest's request was acknowledged */
    EXPECT_EQ(get_le16(&received[0][8]), 0x4444);
}

TEST_F(Nbf, ReassemblesASplitRequest)
{
    uint8_t server = open_session();
    bytes   dialects;
    for (int i = 0; i < 70; i++) {
        dialects.push_back(2);
        put_str(dialects, "SOME OTHER DIALECT NAME THAT IS QUITE LONG FOR THIS TEST");
    }
    dialects.push_back(2);
    put_str(dialects, "NT LM 0.12");
    received.clear();
    bytes reply = exchange(server, smb(0x72, 0, {}, dialects));

    ASSERT_GE(reply.size(), 37u);
    EXPECT_EQ(get_le16(&reply[33]), 70);
    int continues = 0;
    for (const bytes &r : received)
        continues += r[4] == 0x1C;
    EXPECT_GE(continues, 1);
}

TEST_F(Nbf, CarriesAWholeFileAcrossSeveralFrames)
{
    uint8_t server = open_session();
    bytes   dialects = { 2 };
    put_str(dialects, "NT LM 0.12");
    exchange(server, smb(0x72, 0, {}, dialects));
    bytes setup = smb(0x73, 0, { 0xFF, 0, 4356, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0 });
    exchange(server, setup);

    bytes tree = { 0 };
    put_str(tree, "\\\\86BOXHOST\\SHARED");
    put_str(tree, "?????");
    bytes reply = exchange(server, smb(0x75, 0, { 0xFF, 0, 0, 1 }, tree));
    ASSERT_GE(reply.size(), 35u);
    ASSERT_EQ(reply[5], 0);
    uint16_t tid = get_le16(&reply[24]);

    bytes name;
    put_str(name, "\\DOCS\\PATTERN.TXT");
    reply = exchange(server, smb(0x2D, tid, { 0xFF, 0, 0, 0, 0x16, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0 }, name));
    ASSERT_EQ(reply[5], 0);
    uint16_t fid = get_le16(&reply[33 + 4]);

    bytes file;
    for (;;) {
        reply = exchange(server, smb(0x2E, tid, { 0xFF, 0, fid, uint16_t(file.size()), uint16_t(file.size() >> 16),
                                                  4096, 4096, 0, 0, 0 }));
        ASSERT_GE(reply.size(), 35u);
        ASSERT_EQ(reply[5], 0);
        int count = get_le16(&reply[33 + 10]);
        int at    = get_le16(&reply[33 + 12]);
        if (!count)
            break;
        file.insert(file.end(), reply.begin() + at, reply.begin() + at + count);
    }
    ASSERT_EQ(file.size(), 35u * 1024);
    EXPECT_EQ(memcmp(&file[35 * 700], "86Box SMB test pattern line 00700\r\n", 35), 0);
}

TEST_F(Nbf, RespectsTheSmallestFrameTheSessionAgreed)
{
    uint8_t server = open_session();
    bytes   dialects = { 2 };
    put_str(dialects, "NT LM 0.12");
    nbf_set_max_frame(nbf, 4 + 14 + 16);
    exchange(server, smb(0x72, 0, {}, dialects));
    for (const bytes &r : received)
        EXPECT_LE(r.size(), 14u + 16);
}

TEST_F(Nbf, StopsAtTheWindowUntilAcknowledged)
{
    uint8_t server = open_session();
    bytes   dialects = { 2 };
    put_str(dialects, "NT LM 0.12");
    nbf_set_max_frame(nbf, 4 + 14 + 4);

    bytes frame = session_header(0x16, 0, 0, 0, 0x4444, server, 7);
    bytes message = smb(0x72, 0, {}, dialects);
    frame.insert(frame.end(), message.begin(), message.end());
    bytes f = { 0xF0, 0xF0, uint8_t(ns << 1), uint8_t(nr << 1) };
    f.insert(f.end(), frame.begin(), frame.end());
    sent.clear();
    in(f);
    int numbered = 0;
    for (const bytes &s : sent)
        numbered += !(s[2] & 1);
    EXPECT_EQ(numbered, 7);
}

TEST_F(Nbf, RetransmitsWhatIsNotAcknowledged)
{
    uint8_t server = open_session();
    bytes   dialects = { 2 };
    put_str(dialects, "NT LM 0.12");

    bytes frame = session_header(0x16, 0, 0, 0, 0x4444, server, 7);
    bytes message = smb(0x72, 0, {}, dialects);
    frame.insert(frame.end(), message.begin(), message.end());
    bytes f = { 0xF0, 0xF0, uint8_t(ns << 1), uint8_t(nr << 1) };
    f.insert(f.end(), frame.begin(), frame.end());
    sent.clear();
    in(f);
    size_t first = sent.size();
    ASSERT_GE(first, 2u);
    bytes original = sent[0];
    sent.clear();
    for (int ms = 0; ms < 1500; ms++)
        nbf_tick(nbf);
    ASSERT_EQ(sent.size(), first);
    EXPECT_EQ(sent[0], original);
}

TEST_F(Nbf, AnsweringRejectGoesBackToTheNamedFrame)
{
    uint8_t server = open_session();
    bytes   dialects = { 2 };
    put_str(dialects, "NT LM 0.12");

    bytes frame = session_header(0x16, 0, 0, 0, 0x4444, server, 7);
    bytes message = smb(0x72, 0, {}, dialects);
    frame.insert(frame.end(), message.begin(), message.end());
    bytes f = { 0xF0, 0xF0, uint8_t(ns << 1), uint8_t(nr << 1) };
    f.insert(f.end(), frame.begin(), frame.end());
    sent.clear();
    in(f);
    ASSERT_GE(sent.size(), 1u);
    bytes first = sent[0];
    sent.clear();
    in({ 0xF0, 0xF1, 0x09, uint8_t(nr << 1) });
    ASSERT_FALSE(sent.empty());
    EXPECT_EQ(sent[0], first);
}

TEST_F(Nbf, RejectsAGapOnceAndIgnoresTheFrame)
{
    open_session();
    uint8_t skipped = uint8_t(ns + 1);
    bytes   gap = { 0xF0, 0xF0, uint8_t(skipped << 1), uint8_t(nr << 1) };
    bytes   header = session_header(0x1F, 0, 0, 0, 0, 1, 7);
    gap.insert(gap.end(), header.begin(), header.end());
    sent.clear();
    in(gap);
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0][2], 0x09);
    EXPECT_EQ(sent[0][3] >> 1, ns);
    sent.clear();
    in(gap);
    EXPECT_TRUE(sent.empty());
}

TEST_F(Nbf, AnswersAPoll)
{
    link();
    in({ 0xF0, 0xF0, 0x01, 0x01 });
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0][1], 0xF1);
    EXPECT_EQ(sent[0][2], 0x01);
    EXPECT_EQ(sent[0][3], 0x01);
}

TEST_F(Nbf, HoldsOffWhileTheGuestIsBusy)
{
    uint8_t server = open_session();
    bytes   dialects = { 2 };
    put_str(dialects, "NT LM 0.12");
    in({ 0xF0, 0xF1, 0x05, uint8_t(nr << 1) });

    bytes frame = session_header(0x16, 0, 0, 0, 0x4444, server, 7);
    bytes message = smb(0x72, 0, {}, dialects);
    frame.insert(frame.end(), message.begin(), message.end());
    bytes f = { 0xF0, 0xF0, uint8_t(ns << 1), uint8_t(nr << 1) };
    f.insert(f.end(), frame.begin(), frame.end());
    sent.clear();
    in(f);
    int numbered = 0;
    for (const bytes &s : sent)
        numbered += !(s[2] & 1);
    EXPECT_EQ(numbered, 0);

    sent.clear();
    in({ 0xF0, 0xF1, 0x01, uint8_t(nr << 1) });
    for (const bytes &s : sent)
        numbered += !(s[2] & 1);
    EXPECT_GE(numbered, 1);
}

TEST_F(Nbf, ResendsFromWhereTheGuestStopped)
{
    uint8_t server = open_session();
    bytes   dialects = { 2 };
    put_str(dialects, "NT LM 0.12");

    bytes frame = session_header(0x16, 0, 0, 0, 0x4444, server, 7);
    bytes message = smb(0x72, 0, {}, dialects);
    frame.insert(frame.end(), message.begin(), message.end());
    info(frame);
    bytes whole;
    for (const bytes &r : received)
        if (r[4] == 0x16)
            whole.assign(r.begin() + 14, r.end());
    ASSERT_FALSE(whole.empty());

    received.clear();
    info(session_header(0x1A, 0, 10, 0, 0, server, 7));
    info(session_header(0x1B, 0, 10, 0, 0, server, 7));
    ASSERT_FALSE(received.empty());
    EXPECT_EQ(received.back()[4], 0x16);
    EXPECT_EQ(received.back().size(), 14 + whole.size() - 10);
    EXPECT_EQ(memcmp(&received.back()[14], &whole[10], whole.size() - 10), 0);
}

TEST_F(Nbf, EndingASessionFreesIt)
{
    uint8_t server = open_session();
    info(session_header(0x18, 0, 0, 0, 0, server, 7));
    received.clear();
    info(session_header(0x16, 0, 0, 0, 1, server, 7));
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0][4], 0x18);
}

TEST_F(Nbf, DiscClosesEverything)
{
    uint8_t server = open_session();
    sent.clear();
    in({ 0xF0, 0xF0, 0x53 });
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0][2], 0x73);
    sent.clear();
    link();
    received.clear();
    info(session_header(0x16, 0, 0, 0, 1, server, 7));
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0][4], 0x18);
}

TEST_F(Nbf, ResetForgetsTheCall)
{
    open_session();
    nbf_reset(nbf);
    sent.clear();
    in({ 0xF0, 0xF0, 0, 0, 1, 2, 3 });
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0][2], 0x1F);
}

TEST_F(Nbf, GivesUpOnASilentGuest)
{
    uint8_t server = open_session();
    bytes   dialects = { 2 };
    put_str(dialects, "NT LM 0.12");

    bytes frame = session_header(0x16, 0, 0, 0, 0x4444, server, 7);
    bytes message = smb(0x72, 0, {}, dialects);
    frame.insert(frame.end(), message.begin(), message.end());
    bytes f = { 0xF0, 0xF0, uint8_t(ns << 1), uint8_t(nr << 1) };
    f.insert(f.end(), frame.begin(), frame.end());
    in(f);
    sent.clear();
    for (int ms = 0; ms < 20000; ms++)
        nbf_tick(nbf);
    ASSERT_FALSE(sent.empty());
    EXPECT_EQ(sent.back()[2], 0x1F);
}

static bytes
mailslot_write(const char *slot, const bytes &frame)
{
    int   name_at = 32 + 1 + 17 * 2 + 2;
    int   data_at = name_at + int(strlen(slot)) + 1;
    bytes data;
    put_str(data, slot);
    data.insert(data.end(), frame.begin(), frame.end());
    bytes m = smb(0x25, 0, { 0, uint16_t(frame.size()), 0, 0, 0, 0, 0, 0, 0, 0, uint16_t(data_at),
                             uint16_t(frame.size()), uint16_t(data_at), 3, 1, 1, 2 }, data);
    m[32 + 1 + 26] = 3;
    m[32 + 1 + 27] = 0;
    return m;
}

TEST_F(Nbf, AnswersLogonDatagrams)
{
    bytes frame;
    put_le16(frame, 0);
    put_str(frame, "86BOX95");
    put_str(frame, "86BOX");
    put_str(frame, "\\MAILSLOT\\TEMP\\NETLOGON");
    frame.push_back(0);
    put_le16(frame, 0xFFFF); put_le16(frame, 0xFFFF);

    in(names_frame(0x08, 0, 0, 0, nb_name(VSERVER_WORKGROUP_NAME, 0x1C), nb_name("86BOX95", 0),
                   mailslot_write("\\MAILSLOT\\NET\\NETLOGON", frame)));
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0][3 + 4], 0x08);
    EXPECT_EQ(memcmp(&sent[0][3 + 12], nb_name("86BOX95", 0).data(), 16), 0);
    EXPECT_EQ(memcmp(&sent[0][3 + 28], nb_name(VSERVER_SERVER_NAME, 0).data(), 16), 0);
    std::string text(sent[0].begin(), sent[0].end());
    EXPECT_NE(text.find("\\MAILSLOT\\TEMP\\NETLOGON"), std::string::npos);
    EXPECT_NE(text.find("\\\\" VSERVER_SERVER_NAME), std::string::npos);
}

TEST_F(Nbf, AnnouncesOnceAGuestHasShownItself)
{
    for (int ms = 0; ms < 70000; ms++)
        nbf_tick(nbf);
    EXPECT_TRUE(sent.empty());
    in(names_frame(0x09, 0, 0, 0, std::string(16, '\0'), nb_name("86BOX95", 0), { 0xFF }));
    sent.clear();
    for (int ms = 0; ms < 2000; ms++)
        nbf_tick(nbf);
    ASSERT_EQ(sent.size(), 2u);
    EXPECT_EQ(sent[0][3 + 4], 0x08);
    EXPECT_EQ(memcmp(&sent[0][3 + 12], nb_name(VSERVER_WORKGROUP_NAME, 0x1D).data(), 16), 0);
}

TEST_F(Nbf, IgnoresOtherServiceAccessPoints)
{
    in({ 0xAA, 0xAA, 0x7F });
    EXPECT_TRUE(sent.empty());
    in({ 0xF0 });
    EXPECT_TRUE(sent.empty());
}
