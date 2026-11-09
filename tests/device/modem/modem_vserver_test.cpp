#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include <86box/network/net_vserver_config.h>
#include <86box/network/net_browser.h>
#include <86box/network/net_nbns.h>
#include <86box/network/net_smb.h>
#include <86box/network/net_vserver.h>

static const uint32_t server_ip = 0x0A000204;
static const uint32_t guest_ip  = 0x0A00020F;

typedef std::vector<uint8_t> bytes;

static void
capture(void *priv, const uint8_t *pkt, int len)
{
    static_cast<std::vector<bytes> *>(priv)->emplace_back(pkt, pkt + len);
}

static void put_be16(bytes &b, uint16_t v) { b.push_back(uint8_t(v >> 8)); b.push_back(uint8_t(v)); }
static void put_be32(bytes &b, uint32_t v) { put_be16(b, uint16_t(v >> 16)); put_be16(b, uint16_t(v)); }
static void put_le16(bytes &b, uint16_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); }
static void put_str(bytes &b, const std::string &s) { b.insert(b.end(), s.begin(), s.end()); b.push_back(0); }
static uint16_t get_le16(const uint8_t *p) { return uint16_t(p[0] | (p[1] << 8)); }
static uint32_t get_le32(const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
static uint32_t get_be32(const uint8_t *p) { return (uint32_t(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static uint16_t get_be16(const uint8_t *p) { return uint16_t((p[0] << 8) | p[1]); }

static bytes
ipv4(uint8_t protocol, uint32_t destination, const bytes &payload)
{
    bytes p;
    p.push_back(0x45); p.push_back(0);
    put_be16(p, uint16_t(20 + payload.size()));
    put_be32(p, 0);
    p.push_back(128); p.push_back(protocol); put_be16(p, 0);
    put_be32(p, guest_ip); put_be32(p, destination);
    p.insert(p.end(), payload.begin(), payload.end());
    return p;
}

static bytes
tcp(uint16_t port, uint32_t seq, uint32_t ack, uint8_t flags, const bytes &data = {}, uint16_t dest = 139)
{
    bytes t;
    put_be16(t, port); put_be16(t, dest); put_be32(t, seq); put_be32(t, ack);
    t.push_back(0x50); t.push_back(flags); put_be16(t, 8192); put_be16(t, 0); put_be16(t, 0);
    t.insert(t.end(), data.begin(), data.end());
    return ipv4(6, server_ip, t);
}

static bytes
encode_name(const std::string &text, uint8_t suffix)
{
    uint8_t name[16];
    memset(name, ' ', 16);
    memcpy(name, text.data(), text.size());
    name[15] = suffix;
    bytes out = { 32 };
    for (uint8_t c : name) { out.push_back(uint8_t('A' + (c >> 4))); out.push_back(uint8_t('A' + (c & 15))); }
    out.push_back(0);
    return out;
}

static bytes
nbt(uint8_t type, const bytes &body)
{
    bytes out = { type, 0, uint8_t(body.size() >> 8), uint8_t(body.size()) };
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

static bytes
smb_header(uint8_t command)
{
    bytes h = { 0xFF, 'S', 'M', 'B', command };
    h.resize(32, 0);
    return h;
}

static bytes
negotiate()
{
    bytes m = smb_header(0x72);
    bytes dialects;
    for (const char *d : { "PC NETWORK PROGRAM 1.0", "MICROSOFT NETWORKS 3.0", "DOS LM1.2X002", "DOS LANMAN2.1",
                           "Windows for Workgroups 3.1a", "NT LM 0.12" }) {
        dialects.push_back(2);
        put_str(dialects, d);
    }
    m.push_back(0);
    put_le16(m, uint16_t(dialects.size()));
    m.insert(m.end(), dialects.begin(), dialects.end());
    return m;
}

/* A Windows 95 style SESSION_SETUP chained to a TREE_CONNECT of IPC$. */
static bytes
session_and_tree(const std::string &share = "\\\\86BOXHOST\\IPC$")
{
    bytes m = smb_header(0x73);
    m.push_back(10);
    m.push_back(0x75); m.push_back(0);
    size_t offset_at = m.size();
    put_le16(m, 0);
    for (int i = 0; i < 8; i++)
        put_le16(m, 0);
    put_le16(m, 1); /* BCC */
    m.push_back(0);
    m[offset_at]     = uint8_t(m.size());
    m[offset_at + 1] = uint8_t(m.size() >> 8);
    m.push_back(4);
    m.push_back(0xFF); m.push_back(0); put_le16(m, 0);
    put_le16(m, 0); put_le16(m, 1);
    bytes tail = { 0 };
    tail.insert(tail.end(), share.begin(), share.end());
    tail.push_back(0);
    put_str(tail, "?????");
    put_le16(m, uint16_t(tail.size()));
    m.insert(m.end(), tail.begin(), tail.end());
    return m;
}

static bytes
rap(const bytes &params)
{
    static const char name[] = "\\PIPE\\LANMAN";
    bytes m = smb_header(0x25);
    m[24] = 1; /* the tree session_and_tree connected */
    int   name_at = 32 + 1 + 14 * 2 + 2;
    int   params_at = name_at + int(sizeof(name));
    m.push_back(14);
    put_le16(m, uint16_t(params.size())); put_le16(m, 0); put_le16(m, 8); put_le16(m, 1024);
    m.push_back(0); m.push_back(0); put_le16(m, 0); put_le16(m, 0); put_le16(m, 0); put_le16(m, 0);
    put_le16(m, uint16_t(params.size())); put_le16(m, uint16_t(params_at));
    put_le16(m, 0); put_le16(m, uint16_t(params_at));
    m.push_back(0); m.push_back(0);
    put_le16(m, uint16_t(sizeof(name) + params.size()));
    m.insert(m.end(), name, name + sizeof(name));
    m.insert(m.end(), params.begin(), params.end());
    return m;
}

static bytes
server_enum(uint16_t level, uint32_t type, const std::string &domain, uint16_t api = 104)
{
    bytes p;
    put_le16(p, api);
    put_str(p, "WrLehDz");
    put_str(p, "B16BBDz");
    put_le16(p, level); put_le16(p, 1024);
    put_le16(p, uint16_t(type)); put_le16(p, uint16_t(type >> 16));
    put_str(p, domain);
    return rap(p);
}

class Smb : public ::testing::Test {
protected:
    void SetUp() override {
        session = smb_session_create(nullptr);
        ASSERT_NE(session, nullptr);
        bytes request = encode_name(VSERVER_SERVER_NAME, 0x20);
        bytes calling = encode_name("86BOX95", 0x00);
        request.insert(request.end(), calling.begin(), calling.end());
        bytes reply = feed(nbt(0x81, request));
        ASSERT_EQ(reply.size(), 4u);
        ASSERT_EQ(reply[0], 0x82);
    }
    void TearDown() override { smb_session_close(session); }
    bytes feed(const bytes &in) {
        bytes out(SMB_MAX_OUTPUT);
        int n = smb_session_input(session, in.data(), int(in.size()), out.data(), int(out.size()));
        EXPECT_GE(n, 0);
        out.resize(size_t(n > 0 ? n : 0));
        return out;
    }
    /* Returns the SMB message without its NetBIOS header. */
    bytes message(const bytes &in) {
        bytes reply = feed(nbt(0, in));
        if (reply.size() < 4)
            return {};
        return bytes(reply.begin() + 4, reply.end());
    }
    void connect() {
        message(negotiate());
        ASSERT_EQ(message(session_and_tree())[5], 0);
    }
    smb_session_t *session = nullptr;
};

TEST_F(Smb, RefusesAnUnknownCalledName)
{
    smb_session_t *other = smb_session_create(nullptr);
    bytes          request = encode_name("SOMEONEELSE", 0x20);
    bytes          calling = encode_name("86BOX95", 0x00);
    request.insert(request.end(), calling.begin(), calling.end());
    bytes packet = nbt(0x81, request);
    bytes out(SMB_MAX_OUTPUT);

    int n = smb_session_input(other, packet.data(), int(packet.size()), out.data(), int(out.size()));
    ASSERT_EQ(n, 5);
    EXPECT_EQ(out[0], 0x83);
    EXPECT_EQ(out[4], 0x82);
    smb_session_close(other);
}

TEST_F(Smb, ChoosesTheBestDialect)
{
    bytes reply = message(negotiate());
    ASSERT_GE(reply.size(), 32u + 1 + 34 + 2);
    EXPECT_EQ(reply[9] & 0x80, 0x80);
    EXPECT_EQ(reply[32], 17);
    EXPECT_EQ(get_le16(&reply[33]), 5); /* NT LM 0.12 */
    EXPECT_EQ(reply[35], 3);            /* user level, encrypted */
}

TEST_F(Smb, ReportsNoCommonDialect)
{
    bytes m = smb_header(0x72);
    bytes d = { 2 };
    put_str(d, "XENIX CORE");
    m.push_back(0);
    put_le16(m, uint16_t(d.size()));
    m.insert(m.end(), d.begin(), d.end());

    bytes reply = message(m);
    ASSERT_GE(reply.size(), 37u);
    EXPECT_EQ(reply[32], 1);
    EXPECT_EQ(get_le16(&reply[33]), 0xFFFF);
}

TEST_F(Smb, ChainedSessionAndTreeConnect)
{
    message(negotiate());
    bytes reply = message(session_and_tree());
    ASSERT_GT(reply.size(), 40u);
    EXPECT_EQ(reply[5], 0);
    EXPECT_EQ(get_le16(&reply[24]), 1);
    EXPECT_EQ(get_le16(&reply[28]), 100);
    EXPECT_EQ(reply[32], 3);
    EXPECT_EQ(reply[33], 0x75);

    int next = get_le16(&reply[35]);
    ASSERT_LT(size_t(next), reply.size());
    EXPECT_EQ(reply[size_t(next)], 3);
    EXPECT_EQ(reply[size_t(next) + 1], 0xFF);
    EXPECT_STREQ(reinterpret_cast<const char *>(&reply[size_t(next) + 9]), "IPC");
}

TEST_F(Smb, RefusesOtherShares)
{
    message(negotiate());
    bytes reply = message(session_and_tree("\\\\86BOXHOST\\NOSUCH"));
    ASSERT_GE(reply.size(), 35u);
    EXPECT_EQ(reply[5], 2);
    EXPECT_EQ(get_le16(&reply[7]), 6);
}

TEST_F(Smb, RejectsATransactionWithoutATree)
{
    bytes reply = message(server_enum(1, 0xFFFFFFFFu, VSERVER_WORKGROUP_NAME));
    ASSERT_GE(reply.size(), 35u);
    EXPECT_EQ(reply[5], 2);
    EXPECT_EQ(get_le16(&reply[7]), 5);
}

static const uint8_t *
rap_params(const bytes &reply)
{
    return &reply[get_le16(&reply[32 + 1 + 8])];
}

static const uint8_t *
rap_data(const bytes &reply)
{
    return &reply[get_le16(&reply[32 + 1 + 14])];
}

TEST_F(Smb, ServerEnumListsItself)
{
    connect();
    bytes reply = message(server_enum(1, 0xFFFFFFFFu, VSERVER_WORKGROUP_NAME));
    ASSERT_GT(reply.size(), 60u);
    EXPECT_EQ(reply[32], 10);
    EXPECT_EQ(get_le16(rap_params(reply)), 0);
    EXPECT_EQ(get_le16(rap_params(reply) + 4), 1);
    EXPECT_STREQ(reinterpret_cast<const char *>(rap_data(reply)), VSERVER_SERVER_NAME);
    EXPECT_EQ(rap_data(reply)[16], 4);
    uint32_t comment = uint32_t(get_le16(rap_data(reply) + 22));
    EXPECT_STREQ(reinterpret_cast<const char *>(rap_data(reply)) + comment, VSERVER_SERVER_COMMENT);
}

TEST_F(Smb, ServerEnum3ListsItself)
{
    connect();
    bytes reply = message(server_enum(1, 0xFFFFFFFFu, VSERVER_WORKGROUP_NAME, 215));
    ASSERT_GT(reply.size(), 60u);
    EXPECT_EQ(get_le16(rap_params(reply) + 4), 1);
    EXPECT_STREQ(reinterpret_cast<const char *>(rap_data(reply)), VSERVER_SERVER_NAME);
}

TEST_F(Smb, DomainEnumListsTheWorkgroup)
{
    connect();
    bytes reply = message(server_enum(1, 0x80000000u, ""));
    ASSERT_GT(reply.size(), 60u);
    EXPECT_EQ(get_le16(rap_params(reply) + 4), 1);
    EXPECT_STREQ(reinterpret_cast<const char *>(rap_data(reply)), VSERVER_WORKGROUP_NAME);
}

TEST_F(Smb, OtherWorkgroupIsEmpty)
{
    connect();
    bytes reply = message(server_enum(1, 0xFFFFFFFFu, "ELSEWHERE"));
    ASSERT_GT(reply.size(), 40u);
    EXPECT_EQ(get_le16(rap_params(reply)), 0);
    EXPECT_EQ(get_le16(rap_params(reply) + 4), 0);
}

TEST_F(Smb, ShareEnumListsTheFolderAndIpc)
{
    connect();
    bytes p;
    put_le16(p, 0);
    put_str(p, "WrLeh");
    put_str(p, "B13BWz");
    put_le16(p, 1); put_le16(p, 1024);
    bytes reply = message(rap(p));
    EXPECT_EQ(get_le16(rap_params(reply) + 4), 3);
    EXPECT_STREQ(reinterpret_cast<const char *>(rap_data(reply)), VSERVER_SHARE_NAME);
    EXPECT_EQ(get_le16(rap_data(reply) + 14), 0);
    EXPECT_STREQ(reinterpret_cast<const char *>(rap_data(reply) + 20), VSERVER_LOGON_SHARE_NAME);
    EXPECT_STREQ(reinterpret_cast<const char *>(rap_data(reply) + 40), VSERVER_IPC_SHARE_NAME);
    EXPECT_EQ(get_le16(rap_data(reply) + 40 + 14), 3);
    EXPECT_STREQ(reinterpret_cast<const char *>(rap_data(reply) + get_le16(rap_data(reply) + 16)),
                 VSERVER_SHARE_REMARK);
}

TEST_F(Smb, UnknownRapCallIsRefused)
{
    connect();
    bytes p;
    put_le16(p, 999);
    put_str(p, "");
    put_str(p, "");
    bytes reply = message(rap(p));
    EXPECT_EQ(get_le16(rap_params(reply)), 50);
}

TEST_F(Smb, SmallBufferReportsMoreData)
{
    connect();
    bytes p;
    put_le16(p, 104);
    put_str(p, "WrLehDz");
    put_str(p, "B16BBDz");
    put_le16(p, 1); put_le16(p, 8);
    put_le16(p, 0xFFFF); put_le16(p, 0xFFFF);
    put_str(p, VSERVER_WORKGROUP_NAME);
    bytes reply = message(rap(p));
    EXPECT_EQ(get_le16(rap_params(reply)), 234);
}

TEST_F(Smb, ShareInfoNamesTheShare)
{
    connect();
    bytes p;
    put_le16(p, 1);
    put_str(p, "zWrLh");
    put_str(p, "B13BWz");
    put_str(p, VSERVER_SHARE_NAME);
    put_le16(p, 1); put_le16(p, 1024);
    bytes reply = message(rap(p));
    EXPECT_EQ(get_le16(rap_params(reply)), 0);
    EXPECT_STREQ(reinterpret_cast<const char *>(rap_data(reply)), VSERVER_SHARE_NAME);

    bytes q;
    put_le16(q, 1);
    put_str(q, "zWrLh");
    put_str(q, "B13BWz");
    put_str(q, "OTHER");
    put_le16(q, 1); put_le16(q, 1024);
    EXPECT_EQ(get_le16(rap_params(message(rap(q)))), 2310);
}

TEST_F(Smb, UnknownCommandGetsAnError)
{
    bytes m = smb_header(0xA2);
    m.push_back(0); put_le16(m, 0);
    bytes reply = message(m);
    ASSERT_GE(reply.size(), 35u);
    EXPECT_EQ(reply[5], 2);
}

TEST_F(Smb, HandlesMessagesSplitAcrossSegments)
{
    bytes packet = nbt(0, negotiate());
    bytes first(packet.begin(), packet.begin() + 10);
    bytes rest(packet.begin() + 10, packet.end());

    EXPECT_TRUE(feed(first).empty());
    EXPECT_EQ(feed(rest)[4 + 32], 17);
}

TEST_F(Smb, KeepaliveGetsNoReply)
{
    EXPECT_TRUE(feed(nbt(0x85, {})).empty());
}

static bytes
browser_request(uint8_t type, const bytes &frame, uint8_t destination_suffix = 0x1D)
{
    static const char name[] = "\\MAILSLOT\\BROWSE";
    bytes smb = smb_header(0x25);
    int   name_at = 32 + 1 + 17 * 2 + 2;
    int   data_at = name_at + int(sizeof(name));
    smb.push_back(17);
    put_le16(smb, 0); put_le16(smb, uint16_t(frame.size())); put_le16(smb, 0); put_le16(smb, 0);
    smb.push_back(0); smb.push_back(0); put_le16(smb, 0); put_le16(smb, 0); put_le16(smb, 0); put_le16(smb, 0);
    put_le16(smb, 0); put_le16(smb, uint16_t(data_at));
    put_le16(smb, uint16_t(frame.size())); put_le16(smb, uint16_t(data_at));
    smb.push_back(3); smb.push_back(0);
    put_le16(smb, 1); put_le16(smb, 1); put_le16(smb, 2);
    put_le16(smb, uint16_t(sizeof(name) + frame.size()));
    smb.insert(smb.end(), name, name + sizeof(name));
    smb.insert(smb.end(), frame.begin(), frame.end());

    bytes d = { type, 2, 0, 1 };
    put_be32(d, guest_ip);
    put_be16(d, 138);
    put_be16(d, uint16_t(68 + smb.size()));
    put_be16(d, 0);
    bytes source = encode_name("86BOX95", 0x00);
    bytes destination = encode_name(VSERVER_WORKGROUP_NAME, destination_suffix);
    d.insert(d.end(), source.begin(), source.end());
    d.insert(d.end(), destination.begin(), destination.end());
    d.insert(d.end(), smb.begin(), smb.end());
    return d;
}

/* The frame carried by one of our own datagrams, parsed the way a guest would. */
static bytes
frame_of(const uint8_t *dgm, int len)
{
    const uint8_t *smb   = dgm + 14 + 68;
    const uint8_t *words = smb + 33;
    int            count = get_le16(words + 22);
    int            at    = get_le16(words + 24);

    EXPECT_EQ(memcmp(smb, "\xFFSMB", 4), 0);
    EXPECT_EQ(smb[32], 17);
    EXPECT_EQ(words[26], 3);
    EXPECT_EQ(get_le16(words + 28), 1);
    EXPECT_STREQ(reinterpret_cast<const char *>(smb + 33 + 34 + 2), "\\MAILSLOT\\BROWSE");
    EXPECT_LE(14 + 68 + at + count, len);
    return bytes(smb + at, smb + at + count);
}

TEST(Browser, AnswersGetBackupList)
{
    uint8_t reply[1024];
    bytes   frame = { 9, 4, 0xAA, 0xBB, 0xCC, 0xDD };
    bytes   request = browser_request(0x11, frame);

    int n = browser_process(server_ip, request.data(), int(request.size()), reply, sizeof(reply));
    ASSERT_GT(n, 0);
    EXPECT_EQ(reply[0], 0x10);
    EXPECT_EQ(get_be16(reply + 10), n - 14);
    bytes answer = frame_of(reply, n);
    ASSERT_EQ(answer.size(), 6u);
    EXPECT_EQ(answer[0], 10);
    EXPECT_EQ(answer[1], 0);
    EXPECT_EQ(answer[2], 0xAA);
    EXPECT_EQ(get_be32(answer.data() + 2), 0xAABBCCDDu);
    /* addressed to the requester */
    EXPECT_EQ(memcmp(reply + 14 + 34, request.data() + 14, 34), 0);
}

TEST(Browser, AnswersAnnouncementRequestDirectly)
{
    uint8_t reply[1024];
    bytes   request = browser_request(0x11, { 2, 0, 'C', 'L', 'I', 'E', 'N', 'T', 0 });

    int n = browser_process(server_ip, request.data(), int(request.size()), reply, sizeof(reply));
    ASSERT_GT(n, 0);
    EXPECT_EQ(reply[0], 0x10);
    EXPECT_EQ(memcmp(reply + 14 + 34, request.data() + 14, 34), 0);
    bytes frame = frame_of(reply, n);
    EXPECT_EQ(frame[0], 1);
    EXPECT_EQ(frame[1], 0);

    request = browser_request(0x11, { 2, 1 });
    EXPECT_EQ(browser_process(server_ip, request.data(), int(request.size()), reply, sizeof(reply)), 0);
}

TEST(Browser, EntersElections)
{
    uint8_t reply[1024];
    bytes   election(14, 0);
    election[0] = 8;
    election[1] = 1;
    election.insert(election.end(), VSERVER_SERVER_NAME, VSERVER_SERVER_NAME + strlen(VSERVER_SERVER_NAME) + 1);
    bytes request = browser_request(0x11, election, 0x1E);

    int n = browser_process(server_ip, request.data(), int(request.size()), reply, sizeof(reply));
    ASSERT_GT(n, 0);
    bytes answer = frame_of(reply, n);
    EXPECT_EQ(answer[0], 8);
    ASSERT_GE(answer.size(), 14u + sizeof(VSERVER_SERVER_NAME));
    EXPECT_EQ(get_le32(answer.data() + 2), 0x20010F2Eu);
    EXPECT_EQ(get_le32(answer.data() + 6), 0);
    EXPECT_EQ(get_le32(answer.data() + 10), 0);
    EXPECT_STREQ(reinterpret_cast<const char *>(answer.data() + 14), VSERVER_SERVER_NAME);

    election.resize(14);
    request = browser_request(0x11, election, 0x1E);
    EXPECT_EQ(browser_process(server_ip, request.data(), int(request.size()), reply, sizeof(reply)), 0);
}

TEST(Browser, AnnouncesTheServer)
{
    uint8_t reply[1024];
    int     n = browser_announcement(server_ip, 0, reply, sizeof(reply));

    ASSERT_GT(n, 0);
    bytes frame = frame_of(reply, n);
    EXPECT_EQ(frame[0], 1);
    EXPECT_EQ(frame[1], 0);
    EXPECT_STREQ(reinterpret_cast<const char *>(&frame[6]), VSERVER_SERVER_NAME);
    EXPECT_EQ(get_le16(&frame[30]), 0xAA55);

    n = browser_announcement(server_ip, 1, reply, sizeof(reply));
    ASSERT_GT(n, 0);
    EXPECT_EQ(frame_of(reply, n)[0], 15);
}

/* The mailslot write a Windows 9x logon starts with, for the domain's <1C> name. */
static bytes
netlogon_request()
{
    bytes frame;
    put_le16(frame, 0);
    put_str(frame, "86BOX95");
    put_str(frame, "86BOX");
    put_str(frame, "\\MAILSLOT\\TEMP\\NETLOGON");
    frame.push_back(0);
    put_le16(frame, 0xFFFF);
    put_le16(frame, 0xFFFF);

    bytes request = browser_request(0x11, frame);
    static const char from[] = "\\MAILSLOT\\BROWSE";
    static const char to[]   = "\\MAILSLOT\\NET\\NETLOGON";
    std::string text(request.begin(), request.end());
    size_t      at = text.find(std::string(from, sizeof(from)));
    EXPECT_NE(at, std::string::npos);
    bytes out(request.begin(), request.begin() + long(at));
    out.insert(out.end(), to, to + sizeof(to));
    out.insert(out.end(), request.begin() + long(at) + long(sizeof(from)), request.end());
    int delta = int(sizeof(to) - sizeof(from));
    /* the frame moved along with the longer name */
    int smb_at = 14 + 34 + 34;
    uint16_t offset = uint16_t(get_le16(&out[size_t(smb_at) + 33 + 24]) + delta);
    out[size_t(smb_at) + 33 + 24] = uint8_t(offset);
    out[size_t(smb_at) + 33 + 25] = uint8_t(offset >> 8);
    return out;
}

TEST(Browser, AnswersALogonRequestOnTheClientsMailslot)
{
    uint8_t reply[1024];
    bytes   request = netlogon_request();
    int     length  = browser_process(server_ip, request.data(), int(request.size()), reply, sizeof(reply));

    ASSERT_GT(length, 0);
    std::string text(reply, reply + length);
    EXPECT_NE(text.find("\\MAILSLOT\\TEMP\\NETLOGON"), std::string::npos);
    size_t at = text.find(std::string("\x06\x00\\\\" VSERVER_SERVER_NAME, 13));
    ASSERT_NE(at, std::string::npos);
    EXPECT_EQ(get_le16(reply + at + 2 + strlen("\\\\" VSERVER_SERVER_NAME) + 1), 0xFFFF);
}

TEST(Browser, IgnoresBrokenLogonRequests)
{
    uint8_t reply[1024];
    bytes   request = netlogon_request();

    for (size_t cut = 100; cut < request.size(); cut++)
        browser_process(server_ip, request.data(), int(cut), reply, sizeof(reply));
    bytes other = request;
    other[other.size() - 8] = 'X'; /* a request with no terminated strings left is ignored too */
    browser_process(server_ip, other.data(), int(other.size()), reply, sizeof(reply));
}

TEST(Browser, IgnoresOtherMailslotsAndJunk)
{
    uint8_t reply[1024];
    bytes   request = browser_request(0x11, { 9, 4, 0, 0, 0, 0 });

    EXPECT_EQ(browser_process(server_ip, request.data(), 20, reply, sizeof(reply)), 0);
    request[82] = 0;
    EXPECT_EQ(browser_process(server_ip, request.data(), int(request.size()), reply, sizeof(reply)), 0);
}

class Server : public ::testing::Test {
protected:
    void SetUp() override {
        server = vserver_init(nullptr, server_ip, true, true, false, false, capture, &sent);
        ASSERT_NE(server, nullptr);
    }
    void TearDown() override { vserver_close(server); }
    bool send(const bytes &p) { return vserver_process_ipv4(server, p.data(), int(p.size())); }

    /* Performs the handshake and returns the first data sequence number the server will use. */
    uint32_t handshake(uint16_t port) {
        sent.clear();
        EXPECT_TRUE(send(tcp(port, 1000, 0, 0x02)));
        EXPECT_EQ(sent.size(), 1u);
        if (sent.empty())
            return 0;
        EXPECT_EQ(sent[0][20 + 13], 0x12);
        EXPECT_EQ(get_be32(&sent[0][20 + 8]), 1001u);
        uint32_t iss = get_be32(&sent[0][20 + 4]);
        send(tcp(port, 1001, iss + 1, 0x10));
        sent.clear();
        return iss + 1;
    }
    vserver_t         *server = nullptr;
    std::vector<bytes> sent;
};

static void
expect_valid_checksums(const bytes &p)
{
    uint32_t sum = 0;
    for (int i = 0; i < 20; i += 2)
        sum += get_be16(&p[size_t(i)]);
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    EXPECT_EQ(sum, 0xFFFFu);

    int      length = int(p.size()) - 20;
    uint32_t tsum   = p[9] + uint32_t(length);
    for (int i = 12; i < 20; i += 2)
        tsum += get_be16(&p[size_t(i)]);
    for (int i = 0; i + 1 < length; i += 2)
        tsum += get_be16(&p[size_t(20 + i)]);
    if (length & 1)
        tsum += uint32_t(p[size_t(20 + length - 1)]) << 8;
    while (tsum >> 16)
        tsum = (tsum & 0xFFFF) + (tsum >> 16);
    EXPECT_EQ(tsum, 0xFFFFu);
}

TEST_F(Server, CompletesAFullBrowseConversation)
{
    uint32_t snd = handshake(1025);
    uint32_t seq = 1001;

    bytes request = encode_name(VSERVER_SERVER_NAME, 0x20);
    bytes calling = encode_name("86BOX95", 0x00);
    request.insert(request.end(), calling.begin(), calling.end());

    for (const bytes &stream : { nbt(0x81, request), nbt(0, negotiate()), nbt(0, session_and_tree()),
                                 nbt(0, server_enum(1, 0xFFFFFFFFu, VSERVER_WORKGROUP_NAME)) }) {
        sent.clear();
        send(tcp(1025, seq, snd, 0x18, stream));
        seq += uint32_t(stream.size());
        ASSERT_EQ(sent.size(), 1u);
        const bytes &reply = sent[0];
        expect_valid_checksums(reply);
        EXPECT_EQ(get_be32(&reply[24]), snd);
        EXPECT_EQ(get_be32(&reply[28]), seq);
        snd += uint32_t(reply.size() - 40);
    }

    const bytes &last = sent[0];
    EXPECT_NE(std::string(last.begin(), last.end()).find(VSERVER_SERVER_NAME), std::string::npos);

    sent.clear();
    send(tcp(1025, seq, snd, 0x11));
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0][20 + 13] & 0x01, 0x01);
    send(tcp(1025, seq + 1, snd + 1, 0x10));
    sent.clear();
    send(tcp(1025, seq + 1, snd + 1, 0x18, nbt(0, negotiate())));
    EXPECT_EQ(sent[0][20 + 13] & 0x04, 0x04); /* the connection really is gone */
}

TEST_F(Server, CarriesAWholeFileOverSmallSegments)
{
    static const char name[] = "\\DOCS\\PATTERN.TXT";
    uint32_t snd = handshake(1025);
    uint32_t seq = 1001;
    bytes    stream;
    bytes    file;

    /* Sends one request and acknowledges segments until its reply has all arrived. */
    auto exchange_message = [&](const bytes &packet) {
        sent.clear();
        send(tcp(1025, seq, snd, 0x18, packet));
        seq += uint32_t(packet.size());
        for (;;) {
            bool progress = false;
            std::vector<bytes> arrived;
            arrived.swap(sent);
            for (const bytes &segment : arrived) {
                size_t length = segment.size() - 40;
                if (get_be32(&segment[24]) == snd && length) {
                    stream.insert(stream.end(), segment.begin() + 40, segment.end());
                    snd += uint32_t(length);
                    progress = true;
                }
            }
            if (!progress)
                break;
            send(tcp(1025, seq, snd, 0x10));
        }
    };

    bytes request = encode_name(VSERVER_SERVER_NAME, 0x20);
    bytes calling = encode_name("86BOX95", 0x00);
    request.insert(request.end(), calling.begin(), calling.end());
    exchange_message(nbt(0x81, request));
    exchange_message(nbt(0, negotiate()));
    exchange_message(nbt(0, session_and_tree("\\\\86BOXHOST\\SHARED")));
    stream.clear();

    bytes open = smb_header(0x2D);
    open[24] = 1;
    open.push_back(15);
    open.push_back(0xFF); open.push_back(0);
    for (int word : { 0, 0, 0, 0x16, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0 })
        put_le16(open, uint16_t(word));
    put_le16(open, sizeof(name));
    open.insert(open.end(), name, name + sizeof(name));
    exchange_message(nbt(0, open));
    ASSERT_GE(stream.size(), 4u + 33);
    ASSERT_EQ(stream[4 + 5], 0);
    uint16_t fid = get_le16(&stream[4 + 33 + 4]);

    for (uint32_t offset = 0;;) {
        bytes read = smb_header(0x2E);
        read[24] = 1;
        read.push_back(10);
        read.push_back(0xFF); read.push_back(0);
        for (int word : { 0, int(fid), int(offset & 0xFFFF), int(offset >> 16), 4096, 4096, 0, 0, 0 })
            put_le16(read, uint16_t(word));
        put_le16(read, 0);
        stream.clear();
        exchange_message(nbt(0, read));
        ASSERT_GE(stream.size(), 4u + 33 + 24);
        ASSERT_EQ((size_t(stream[2]) << 8 | stream[3]) + 4, stream.size());
        int count = get_le16(&stream[4 + 33 + 10]);
        int at    = get_le16(&stream[4 + 33 + 12]);
        if (!count)
            break;
        file.insert(file.end(), stream.begin() + 4 + at, stream.begin() + 4 + at + count);
        offset += uint32_t(count);
    }

    ASSERT_EQ(file.size(), 35u * 1024);
    EXPECT_EQ(memcmp(file.data(), "86Box SMB test pattern line 00000\r\n", 35), 0);
    EXPECT_EQ(memcmp(&file[35 * 1023], "86Box SMB test pattern line 01023\r\n", 35), 0);
}

TEST_F(Server, RetransmitsUnacknowledgedData)
{
    uint32_t snd = handshake(1025);
    bytes request = encode_name(VSERVER_SERVER_NAME, 0x20);
    bytes calling = encode_name("86BOX95", 0x00);
    request.insert(request.end(), calling.begin(), calling.end());
    send(tcp(1025, 1001, snd, 0x18, nbt(0x81, request)));
    ASSERT_EQ(sent.size(), 1u);

    bytes original = sent[0];
    sent.clear();
    for (int ms = 0; ms < 1500; ms++)
        vserver_tick(server);
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(get_be32(&sent[0][24]), get_be32(&original[24]));
}

TEST_F(Server, GivesUpOnASilentPeer)
{
    uint32_t snd = handshake(1025);
    bytes request = encode_name(VSERVER_SERVER_NAME, 0x20);
    bytes calling = encode_name("86BOX95", 0x00);
    request.insert(request.end(), calling.begin(), calling.end());
    send(tcp(1025, 1001, snd, 0x18, nbt(0x81, request)));

    for (int ms = 0; ms < 20000; ms++)
        vserver_tick(server);
    sent.clear();
    for (int ms = 0; ms < 5000; ms++)
        vserver_tick(server);
    for (const bytes &p : sent)
        EXPECT_NE(p[9], 6);
}

TEST_F(Server, ResendsSynAckIfItWasLost)
{
    EXPECT_TRUE(send(tcp(1025, 1000, 0, 0x02)));
    EXPECT_TRUE(send(tcp(1025, 1000, 0, 0x02)));
    ASSERT_EQ(sent.size(), 2u);
    EXPECT_EQ(sent[1][20 + 13], 0x12);
}

TEST_F(Server, ResetsOtherTcpPorts)
{
    EXPECT_TRUE(send(tcp(1025, 1000, 0, 0x02, {}, 445)));
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0][20 + 13] & 0x04, 0x04);
}

TEST_F(Server, RefusesAFifthConnection)
{
    for (uint16_t port = 1025; port < 1029; port++)
        send(tcp(port, 1000, 0, 0x02));
    sent.clear();
    send(tcp(2000, 1000, 0, 0x02));
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0][20 + 13] & 0x04, 0x04);
}

TEST_F(Server, AnswersPing)
{
    bytes icmp = { 8, 0, 0, 0, 0, 1, 0, 2, 'h', 'i' };
    EXPECT_TRUE(send(ipv4(1, server_ip, icmp)));
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0][20], 0);
    EXPECT_EQ(sent[0][28], 'h');
}

static bytes
udp(uint32_t destination, uint16_t port, const bytes &data)
{
    bytes u;
    put_be16(u, 138); put_be16(u, port); put_be16(u, uint16_t(8 + data.size())); put_be16(u, 0);
    u.insert(u.end(), data.begin(), data.end());
    return ipv4(17, destination, u);
}

TEST_F(Server, TakesBrowserBroadcasts)
{
    bytes request = browser_request(0x11, { 9, 4, 1, 2, 3, 4 });

    EXPECT_TRUE(send(udp(0x0AFFFFFF, 138, request)));
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(get_be32(&sent[0][16]), guest_ip);
    EXPECT_EQ(get_be16(&sent[0][20]), 138);
    expect_valid_checksums(sent[0]);
}

TEST_F(Server, LeavesInternetTrafficAlone)
{
    EXPECT_FALSE(send(udp(0x08080808, 53, { 1, 2, 3 })));
    bytes other = { 0x45, 0 };
    EXPECT_FALSE(send(ipv4(6, 0x08080808, tcp(1025, 1, 0, 2))));
    EXPECT_TRUE(sent.empty());
}

TEST_F(Server, KeepsNameBroadcastsOffTheHostNetwork)
{
    EXPECT_TRUE(send(udp(0x0AFFFFFF, 137, { 1, 2, 3 })));
    EXPECT_TRUE(send(udp(0x0A0002FF, 137, { 1, 2, 3 })));
    EXPECT_TRUE(send(udp(0xFFFFFFFF, 137, { 1, 2, 3 })));
    EXPECT_FALSE(send(udp(0x08080808, 137, { 1, 2, 3 })));
    EXPECT_TRUE(sent.empty());
}

TEST_F(Server, AnnouncesOnceTheGuestIsKnown)
{
    for (int ms = 0; ms < 70000; ms++)
        vserver_tick(server);
    EXPECT_TRUE(sent.empty());

    send(udp(0x0AFFFFFF, 138, browser_request(0x11, { 1, 0 })));
    for (int ms = 0; ms < 2000; ms++)
        vserver_tick(server);
    EXPECT_EQ(sent.size(), 2u);
}

TEST_F(Server, ResolvesTheMasterBrowserName)
{
    bytes msg = { 0x12, 0x34, 0x01, 0x10, 0, 1, 0, 0, 0, 0, 0, 0 };
    bytes name = encode_name(VSERVER_WORKGROUP_NAME, 0x1D);
    msg.insert(msg.end(), name.begin(), name.end());
    put_be16(msg, 0x20); put_be16(msg, 1);

    send(udp(server_ip, 137, msg));
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(get_be32(&sent[0][sent[0].size() - 4]), server_ip);
}

TEST_F(Server, ResolvesTheLogonServerName)
{
    bytes msg = { 0x12, 0x34, 0x01, 0x10, 0, 1, 0, 0, 0, 0, 0, 0 };
    bytes name = encode_name(VSERVER_WORKGROUP_NAME, 0x1C);
    msg.insert(msg.end(), name.begin(), name.end());
    put_be16(msg, 0x20); put_be16(msg, 1);

    send(udp(server_ip, 137, msg));
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(get_be32(&sent[0][sent[0].size() - 4]), server_ip);
}

TEST_F(Server, ResolvesTheDomainMasterName)
{
    bytes msg = { 0x12, 0x34, 0x01, 0x10, 0, 1, 0, 0, 0, 0, 0, 0 };
    bytes name = encode_name(VSERVER_WORKGROUP_NAME, 0x1B);
    msg.insert(msg.end(), name.begin(), name.end());
    put_be16(msg, 0x20); put_be16(msg, 1);

    send(udp(server_ip, 137, msg));
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(get_be32(&sent[0][sent[0].size() - 4]), server_ip);
}

TEST(ServerWithoutSmb, StillKeepsNameBroadcasts)
{
    std::vector<bytes> sent;
    vserver_t *server = vserver_init(nullptr, server_ip, true, false, false, false, capture, &sent);
    ASSERT_NE(server, nullptr);

    bytes bcast = udp(0x0AFFFFFF, 137, { 1, 2, 3 });
    EXPECT_TRUE(vserver_process_ipv4(server, bcast.data(), int(bcast.size())));
    EXPECT_TRUE(sent.empty());
    vserver_close(server);
}

TEST(ServerWithoutSmb, OnlyAnswersNames)
{
    std::vector<bytes> sent;
    vserver_t *server = vserver_init(nullptr, server_ip, false, false, false, false, capture, &sent);
    ASSERT_NE(server, nullptr);

    bytes syn = tcp(1025, 1000, 0, 0x02);
    bytes bcast = udp(0x0AFFFFFF, 138, browser_request(0x11, { 9, 4, 1, 2, 3, 4 }));
    EXPECT_FALSE(vserver_process_ipv4(server, syn.data(), int(syn.size())));
    EXPECT_FALSE(vserver_process_ipv4(server, bcast.data(), int(bcast.size())));
    for (int ms = 0; ms < 70000; ms++)
        vserver_tick(server);
    EXPECT_TRUE(sent.empty());
    vserver_close(server);
}
