/* File commands of the emulated modem's SMB server, one group per protocol era. The
   formats follow what core, LAN Manager and NT clients are written to expect. */
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include <86box/network/net_vserver_config.h>
#include <86box/modem/modem_crypto.h>
#include <86box/network/net_nbns.h>
#include <86box/network/net_smb.h>
#include <86box/network/net_smb_fs.h>

typedef std::vector<uint8_t> bytes;

static const int ERRDOS = 1, ERRSRV = 2;
static const int ERRbadfile = 2, ERRbadpath = 3, ERRnofids = 4, ERRnoaccess = 5, ERRbadfid = 6, ERRnofiles = 18,
                 ERRfilexists = 80, ERRunknownlevel = 124, ERRinvtid = 5, ERRinvnetname = 6;

static void put_le16(bytes &b, uint16_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); }
static void put_le32(bytes &b, uint32_t v) { put_le16(b, uint16_t(v)); put_le16(b, uint16_t(v >> 16)); }
static void put_str(bytes &b, const std::string &s) { b.insert(b.end(), s.begin(), s.end()); b.push_back(0); }
static uint16_t get_le16(const uint8_t *p) { return uint16_t(p[0] | (p[1] << 8)); }
static uint32_t get_le32(const uint8_t *p) { return get_le16(p) | (uint32_t(get_le16(p + 2)) << 16); }
static std::string text(const uint8_t *p) { return reinterpret_cast<const char *>(p); }

static bytes
encode_name(const std::string &name, uint8_t suffix)
{
    uint8_t raw[16];
    memset(raw, ' ', 16);
    memcpy(raw, name.data(), name.size());
    raw[15] = suffix;
    bytes out = { 32 };
    for (uint8_t c : raw) { out.push_back(uint8_t('A' + (c >> 4))); out.push_back(uint8_t('A' + (c & 15))); }
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

/* A message with the given words and bytes. */
static bytes
smb(uint8_t command, uint16_t tid, const std::vector<uint16_t> &words = {}, const bytes &data = {})
{
    bytes m = { 0xFF, 'S', 'M', 'B', command };
    m.resize(32, 0);
    m[24] = uint8_t(tid);
    m[26] = 0x34; /* PID */
    m.push_back(uint8_t(words.size()));
    for (uint16_t w : words)
        put_le16(m, w);
    put_le16(m, uint16_t(data.size()));
    m.insert(m.end(), data.begin(), data.end());
    return m;
}

static bytes
ascii4(const std::string &path)
{
    bytes b = { 4 };
    put_str(b, path);
    return b;
}

static bytes
dialects(std::initializer_list<const char *> names)
{
    bytes b;
    for (const char *name : names) {
        b.push_back(2);
        put_str(b, name);
    }
    return b;
}

static bytes
search(uint8_t command, uint16_t tid, uint16_t count, uint16_t attributes, const std::string &path,
       const bytes &key = {})
{
    bytes b = ascii4(path);
    b.push_back(5);
    put_le16(b, uint16_t(key.size()));
    b.insert(b.end(), key.begin(), key.end());
    return smb(command, tid, { count, attributes }, b);
}

static bytes
trans2(uint16_t tid, uint16_t subcommand, const bytes &params, uint16_t max_data = 2048)
{
    const int params_at = 32 + 1 + 15 * 2 + 2 + 1;
    bytes     data      = { 0 };

    data.insert(data.end(), params.begin(), params.end());
    return smb(0x32, tid,
               { uint16_t(params.size()), 0, 16, max_data, 0, 0, 0, 0, 0, uint16_t(params.size()), params_at,
                 0, uint16_t(params_at + params.size()), 1, subcommand },
               data);
}

static bytes
find_first(uint16_t tid, uint16_t level, uint16_t count, const std::string &path, uint16_t flags = 0,
           uint16_t attributes = 0x16)
{
    bytes p;
    put_le16(p, attributes); put_le16(p, count); put_le16(p, flags); put_le16(p, level); put_le32(p, 0);
    put_str(p, path);
    return trans2(tid, 1, p);
}

static bytes
find_next(uint16_t tid, uint16_t sid, uint16_t level, uint16_t count, uint32_t key, uint16_t flags,
          const std::string &name = "")
{
    bytes p;
    put_le16(p, sid); put_le16(p, count); put_le16(p, level); put_le32(p, key); put_le16(p, flags);
    put_str(p, name);
    return trans2(tid, 2, p);
}

static bytes
query_path(uint16_t tid, uint16_t level, const std::string &path)
{
    bytes p;
    put_le16(p, level); put_le32(p, 0);
    put_str(p, path);
    return trans2(tid, 5, p);
}

static bytes
query_file(uint16_t tid, uint16_t fid, uint16_t level)
{
    bytes p;
    put_le16(p, fid); put_le16(p, level);
    return trans2(tid, 7, p);
}

static bytes
query_fs(uint16_t tid, uint16_t level)
{
    bytes p;
    put_le16(p, level);
    return trans2(tid, 3, p);
}

/* The words of a reply start here, and its bytes after them. */
static const uint8_t *reply_word(const bytes &r, int index) { return &r[33 + index * 2]; }
static const uint8_t *reply_bytes(const bytes &r) { return &r[32 + 1 + r[32] * 2 + 2]; }
static const uint8_t *trans_params(const bytes &r) { return &r[get_le16(reply_word(r, 4))]; }
static const uint8_t *trans_data(const bytes &r) { return &r[get_le16(reply_word(r, 7))]; }

class SmbFiles : public ::testing::Test {
protected:
    void SetUp() override {
        session = smb_session_create(nullptr);
        ASSERT_NE(session, nullptr);
        bytes request = encode_name(VSERVER_SERVER_NAME, 0x20);
        bytes calling = encode_name("CLIENT", 0);
        request.insert(request.end(), calling.begin(), calling.end());
        bytes in = nbt(0x81, request), out(SMB_MAX_OUTPUT);
        ASSERT_EQ(smb_session_input(session, in.data(), int(in.size()), out.data(), int(out.size())), 4);
    }
    void TearDown() override { smb_session_close(session); }

    bytes send(const bytes &message) {
        bytes in = nbt(0, message), out(SMB_MAX_OUTPUT);
        int   n  = smb_session_input(session, in.data(), int(in.size()), out.data(), int(out.size()));
        EXPECT_GE(n, 4);
        if (n < 4)
            return bytes(35, 0);
        return bytes(out.begin() + 4, out.begin() + n);
    }
    void expect_ok(const bytes &r) {
        EXPECT_EQ(r[9] & 0x80, 0x80);
        EXPECT_EQ(r[5], 0) << "error " << get_le16(&r[7]);
    }
    void expect_error(const bytes &r, int cls, int code) {
        EXPECT_EQ(r[5], cls);
        EXPECT_EQ(get_le16(&r[7]), code);
    }

    /* The three eras, each up to a connected tree. */
    uint16_t connect_core() {
        send(smb(0x72, 0, {}, dialects({ "PC NETWORK PROGRAM 1.0" })));
        bytes b = ascii4("\\\\86BOXHOST\\SHARED");
        bytes pw = ascii4(""), sv = ascii4("A:");
        b.insert(b.end(), pw.begin(), pw.end());
        b.insert(b.end(), sv.begin(), sv.end());
        bytes r = send(smb(0x70, 0, {}, b));
        expect_ok(r);
        EXPECT_EQ(r[32], 2);
        EXPECT_EQ(get_le16(reply_word(r, 0)), 4356);
        return get_le16(reply_word(r, 1));
    }
    uint16_t tree_connect(const std::string &share = "\\\\86BOXHOST\\SHARED") {
        bytes b = { 0 }; /* a one byte empty password */
        put_str(b, share);
        put_str(b, "?????");
        return get_le16(&send(smb(0x75, 0, { 0xFF, 0, 0, 1 }, b))[24]);
    }
    uint16_t connect_lanman() {
        bytes r = send(smb(0x72, 0, {}, dialects({ "LANMAN1.0", "LM1.2X002" })));
        EXPECT_EQ(r[32], 13);
        EXPECT_EQ(get_le16(reply_word(r, 0)), 1);
        send(smb(0x73, 0, { 0xFF, 0, 0, 4356, 2, 1, 0, 0, 0, 1 }, { 0, 0, 0 }));
        return tree_connect();
    }
    uint16_t connect_nt() {
        bytes r = send(smb(0x72, 0, {}, dialects({ "LANMAN2.1", "NT LM 0.12" })));
        EXPECT_EQ(r[32], 17);
        EXPECT_EQ(get_le16(reply_word(r, 0)), 1);
        send(smb(0x73, 0, { 0xFF, 0, 0, 4356, 2, 1, 0, 0, 0, 1, 0, 0, 0 }, { 0, 0, 0 }));
        return tree_connect();
    }
    uint16_t open_andx(uint16_t tid, const std::string &path, uint16_t access = 0, uint16_t mode = 1) {
        bytes b;
        put_str(b, path);
        bytes r = send(smb(0x2D, tid, { 0xFF, 0, 0, access, 0x16, 0, 0, 0, mode, 0, 0, 0, 0, 0, 0 }, b));
        return r[5] ? 0 : get_le16(reply_word(r, 2));
    }
    bytes read_all(uint16_t tid, uint16_t fid, uint16_t chunk) {
        bytes data;
        for (;;) {
            bytes r = send(smb(0x2E, tid, { 0xFF, 0, fid, uint16_t(data.size()), uint16_t(data.size() >> 16), chunk,
                                            chunk, 0, 0, 0 }));
            expect_ok(r);
            int count = get_le16(reply_word(r, 5));
            if (!count)
                return data;
            const uint8_t *at = &r[get_le16(reply_word(r, 6))];
            data.insert(data.end(), at, at + count);
        }
    }
    smb_session_t *session = nullptr;
};

/* --- the folder itself --- */

TEST(SmbFs, FindsPathsAndRefusesOthers)
{
    EXPECT_EQ(smb_fs_lookup(smb_fs_root(), ""), smb_fs_root());
    ASSERT_NE(smb_fs_lookup(smb_fs_root(), "DOCS"), nullptr);
    EXPECT_TRUE(smb_fs_is_directory(smb_fs_lookup(smb_fs_root(), "DOCS")));
    ASSERT_NE(smb_fs_lookup(smb_fs_root(), "DOCS\\PATTERN.TXT"), nullptr);
    EXPECT_EQ(smb_fs_lookup(smb_fs_root(), "NOPE"), nullptr);
    EXPECT_EQ(smb_fs_lookup(smb_fs_root(), "README.TXT\\X"), nullptr);
    EXPECT_EQ(smb_fs_lookup(smb_fs_root(), "DOCS\\NOPE"), nullptr);
    EXPECT_EQ(smb_fs_attributes(smb_fs_lookup(smb_fs_root(), "README.TXT")), SMB_FS_ATTR_READONLY);
}

TEST(SmbFs, ReadsStopAtTheEnd)
{
    const smb_fs_entry_t *file = smb_fs_lookup(smb_fs_root(), "README.TXT");
    uint32_t              size = smb_fs_size(file);
    uint8_t               data[32];

    EXPECT_EQ(smb_fs_read(file, 0, data, 4), 4);
    EXPECT_EQ(memcmp(data, "This", 4), 0);
    EXPECT_EQ(smb_fs_read(file, size - 2, data, 32), 2);
    EXPECT_EQ(smb_fs_read(file, size, data, 32), 0);
    EXPECT_EQ(smb_fs_read(file, 0xFFFFFFFFu, data, 32), 0);
    EXPECT_EQ(smb_fs_read(smb_fs_lookup(smb_fs_root(), "DOCS"), 0, data, 32), 0);
}

TEST(SmbFs, PatternLinesCarryTheirNumbers)
{
    const smb_fs_entry_t *file = smb_fs_lookup(smb_fs_root(), "DOCS\\PATTERN.TXT");
    uint8_t               data[35];

    EXPECT_EQ(smb_fs_size(file), 35u * 1024);
    ASSERT_EQ(smb_fs_read(file, 35 * 1023, data, 35), 35);
    EXPECT_EQ(memcmp(data, "86Box SMB test pattern line 01023\r\n", 35), 0);
    /* a read that starts part way through a line */
    ASSERT_EQ(smb_fs_read(file, 35 * 7 + 28, data, 10), 10);
    EXPECT_EQ(memcmp(data, "00007\r\n86B", 10), 0);
}

TEST(SmbFs, MatchesDosWildcards)
{
    for (const char *p : { "*", "*.*", "README.TXT", "readme.txt", "*.TXT", "READ*", "????????.???", "<.TXT", "READ>E.TXT",
                           "README\"TXT", "R*E*.T?T" })
        EXPECT_TRUE(smb_fs_match(p, "README.TXT")) << p;
    for (const char *p : { "*", "*.*", "DOCS", "DOCS.", "DOCS.*", "????????.???", "D???" })
        EXPECT_TRUE(smb_fs_match(p, "DOCS")) << p;
    for (const char *p : { "*.DOC", "README", "READ", "???.TXT", "X*", "README.TXT2" })
        EXPECT_FALSE(smb_fs_match(p, "README.TXT")) << p;
    EXPECT_TRUE(smb_fs_match("*", "."));
    EXPECT_TRUE(smb_fs_match("*.*", "."));
    EXPECT_FALSE(smb_fs_match("*.TXT", "."));
}

/* --- core: MS Client 3.0's basic redirector and DOS LAN Manager --- */

TEST_F(SmbFiles, CoreDialectGetsTheOneWordReply)
{
    bytes r = send(smb(0x72, 0, {}, dialects({ "XENIX CORE", "PC NETWORK PROGRAM 1.0", "SOMETHING" })));
    EXPECT_EQ(r[32], 1);
    EXPECT_EQ(get_le16(reply_word(r, 0)), 1);
    EXPECT_EQ(r.size(), 32u + 1 + 2 + 2);
}

TEST_F(SmbFiles, MsNetworksIsLanManagerShaped)
{
    bytes r = send(smb(0x72, 0, {}, dialects({ "PC NETWORK PROGRAM 1.0", "MICROSOFT NETWORKS 3.0" })));
    EXPECT_EQ(r[32], 13);
    EXPECT_EQ(get_le16(reply_word(r, 0)), 1);
    EXPECT_EQ(get_le16(reply_word(r, 1)), 3); /* user level, encrypted */
    EXPECT_EQ(get_le16(reply_word(r, 2)), 4356);
    EXPECT_EQ(get_le16(reply_word(r, 11)), 8);
    EXPECT_EQ(get_le16(&r[32 + 1 + 26]), 8);
    EXPECT_EQ(r.size(), size_t(32 + 1 + 26 + 2 + 8));
}

TEST_F(SmbFiles, CoreSearchListsTheRoot)
{
    uint16_t tid = connect_core();
    bytes    r   = send(search(0x81, tid, 16, 0x16, "\\????????.???"));
    expect_ok(r);
    ASSERT_EQ(get_le16(reply_word(r, 0)), 2);

    const uint8_t *b = reply_bytes(r);
    EXPECT_EQ(b[0], 5);
    EXPECT_EQ(get_le16(b + 1), 2 * 43);

    const uint8_t *e = b + 3;
    EXPECT_EQ(memcmp(e + 1, "README  TXT", 11), 0);
    EXPECT_EQ(e[21], 0x01);
    EXPECT_EQ(get_le16(e + 22), SMB_FS_DOS_TIME);
    EXPECT_EQ(get_le16(e + 24), SMB_FS_DOS_DATE);
    EXPECT_EQ(get_le32(e + 26), smb_fs_size(smb_fs_lookup(smb_fs_root(), "README.TXT")));
    EXPECT_EQ(text(e + 30), "README.TXT");

    e += 43;
    EXPECT_EQ(memcmp(e + 1, "DOCS       ", 11), 0);
    EXPECT_EQ(e[21], 0x10);
    EXPECT_EQ(text(e + 30), "DOCS");
}

TEST_F(SmbFiles, CoreSearchHidesDirectoriesUnlessAskedAndResumes)
{
    uint16_t tid = connect_core();
    bytes    r   = send(search(0x81, tid, 16, 0, "\\*.*"));
    ASSERT_EQ(get_le16(reply_word(r, 0)), 1);
    EXPECT_EQ(text(reply_bytes(r) + 3 + 30), "README.TXT");

    r = send(search(0x81, tid, 2, 0x10, "\\DOCS\\*.*"));
    ASSERT_EQ(get_le16(reply_word(r, 0)), 2);
    EXPECT_EQ(text(reply_bytes(r) + 3 + 30), ".");
    EXPECT_EQ(text(reply_bytes(r) + 3 + 43 + 30), "..");

    bytes key(reply_bytes(r) + 3 + 43, reply_bytes(r) + 3 + 43 + 21);
    key[20] = 0x7E; /* the client's half of the key must come back */
    r = send(search(0x81, tid, 2, 0x10, "", key));
    ASSERT_EQ(get_le16(reply_word(r, 0)), 1);
    EXPECT_EQ(text(reply_bytes(r) + 3 + 30), "PATTERN.TXT");
    EXPECT_EQ(reply_bytes(r)[3 + 20], 0x7E);

    key.assign(reply_bytes(r) + 3, reply_bytes(r) + 3 + 21);
    expect_error(send(search(0x81, tid, 2, 0x10, "", key)), ERRDOS, ERRnofiles);
}

TEST_F(SmbFiles, CoreSearchKeepsSeveralGoing)
{
    uint16_t tid = connect_core();
    bytes    first = send(search(0x81, tid, 1, 0x16, "\\*.*"));
    bytes    second = send(search(0x81, tid, 1, 0x16, "\\DOCS\\*.*"));
    bytes    key(reply_bytes(first) + 3, reply_bytes(first) + 3 + 21);

    (void) second;
    bytes r = send(search(0x81, tid, 1, 0x16, "", key));
    ASSERT_EQ(get_le16(reply_word(r, 0)), 1);
    EXPECT_EQ(text(reply_bytes(r) + 3 + 30), "DOCS");
}

TEST_F(SmbFiles, CoreSearchReportsWhatIsMissing)
{
    uint16_t tid = connect_core();
    expect_error(send(search(0x81, tid, 8, 0, "\\NOPE.*")), ERRDOS, ERRnofiles);
    expect_error(send(search(0x81, tid, 8, 0, "\\NOPE\\*.*")), ERRDOS, ERRbadpath);
    expect_error(send(search(0x81, tid, 8, 0, "", bytes(21, 0))), ERRDOS, ERRnofiles);
    expect_error(send(search(0x81, tid, 8, 0, "", bytes(5, 0))), ERRDOS, ERRnofiles);
}

TEST_F(SmbFiles, VolumeLabelSearch)
{
    uint16_t tid = connect_core();
    bytes    r   = send(search(0x81, tid, 8, 0x08, "\\*.*"));
    ASSERT_EQ(get_le16(reply_word(r, 0)), 1);
    EXPECT_EQ(reply_bytes(r)[3 + 21], 0x08);
    EXPECT_EQ(text(reply_bytes(r) + 3 + 30), VSERVER_SHARE_NAME);
}

TEST_F(SmbFiles, FindUniqueAndFindClose)
{
    uint16_t tid = connect_core();
    bytes    r   = send(search(0x83, tid, 8, 0, "\\README.TXT"));
    ASSERT_EQ(get_le16(reply_word(r, 0)), 1);
    bytes key(reply_bytes(r) + 3, reply_bytes(r) + 3 + 21);
    expect_error(send(search(0x82, tid, 8, 0, "", key)), ERRDOS, ERRnofiles);

    r = send(search(0x82, tid, 1, 0x16, "\\*.*"));
    ASSERT_EQ(get_le16(reply_word(r, 0)), 1);
    key.assign(reply_bytes(r) + 3, reply_bytes(r) + 3 + 21);
    r = send(search(0x84, tid, 0, 0, "", key));
    expect_ok(r);
    EXPECT_EQ(r[32], 1);
    EXPECT_EQ(get_le16(reply_word(r, 0)), 0);
    EXPECT_EQ(get_le16(&r[33 + 2]), 3);
    EXPECT_EQ(reply_bytes(r)[0], 5);
    expect_error(send(search(0x82, tid, 1, 0, "", key)), ERRDOS, ERRnofiles);
}

TEST_F(SmbFiles, CoreOpenReadSeekClose)
{
    uint16_t tid = connect_core();
    bytes    r   = send(smb(0x02, tid, { 0, 0x16 }, ascii4("\\readme.txt")));
    expect_ok(r);
    ASSERT_EQ(r[32], 7);
    uint16_t fid = get_le16(reply_word(r, 0));
    EXPECT_EQ(get_le16(reply_word(r, 1)), 0x01);
    EXPECT_EQ(get_le32(reply_word(r, 2)), SMB_FS_UNIX_TIME);
    uint32_t size = get_le32(reply_word(r, 4));
    EXPECT_EQ(size, smb_fs_size(smb_fs_lookup(smb_fs_root(), "README.TXT")));

    r = send(smb(0x0A, tid, { fid, 4, 5, 0, 0 }));
    expect_ok(r);
    ASSERT_EQ(r[32], 5);
    EXPECT_EQ(get_le16(reply_word(r, 0)), 4);
    EXPECT_EQ(reply_bytes(r)[0], 1);
    EXPECT_EQ(get_le16(reply_bytes(r) + 1), 4);
    EXPECT_EQ(memcmp(reply_bytes(r) + 3, "is t", 4), 0);

    r = send(smb(0x12, tid, { fid, 2, 0xFFFE, 0xFFFF }));
    expect_ok(r);
    EXPECT_EQ(get_le32(reply_word(r, 0)), size - 2);
    r = send(smb(0x12, tid, { fid, 1, 1, 0 }));
    EXPECT_EQ(get_le32(reply_word(r, 0)), size - 1);

    r = send(smb(0x0A, tid, { fid, 4, uint16_t(size), 0, 0 }));
    expect_ok(r);
    EXPECT_EQ(get_le16(reply_word(r, 0)), 0);

    expect_ok(send(smb(0x05, tid, { fid })));
    r = send(smb(0x23, tid, { fid }));
    expect_ok(r);
    ASSERT_EQ(r[32], 11);
    EXPECT_EQ(get_le16(reply_word(r, 0)), SMB_FS_DOS_DATE);
    EXPECT_EQ(get_le32(reply_word(r, 6)), size);
    EXPECT_EQ(get_le32(reply_word(r, 8)), 512u);
    EXPECT_EQ(get_le16(reply_word(r, 10)), 1);

    expect_ok(send(smb(0x04, tid, { fid, 0, 0 })));
    expect_error(send(smb(0x0A, tid, { fid, 4, 0, 0, 0 })), ERRDOS, ERRbadfid);
    expect_error(send(smb(0x04, tid, { fid, 0, 0 })), ERRDOS, ERRbadfid);
}

TEST_F(SmbFiles, CoreQueries)
{
    uint16_t tid = connect_core();
    bytes    r   = send(smb(0x08, tid, {}, ascii4("\\DOCS\\PATTERN.TXT")));
    expect_ok(r);
    ASSERT_EQ(r[32], 10);
    EXPECT_EQ(get_le16(reply_word(r, 0)), 0x01);
    EXPECT_EQ(get_le32(reply_word(r, 1)), SMB_FS_UNIX_TIME);
    EXPECT_EQ(get_le32(reply_word(r, 3)), 35u * 1024);

    r = send(smb(0x08, tid, {}, ascii4("\\")));
    expect_ok(r);
    EXPECT_EQ(get_le16(reply_word(r, 0)), 0x10);

    expect_error(send(smb(0x08, tid, {}, ascii4("\\NOPE"))), ERRDOS, ERRbadfile);
    expect_error(send(smb(0x08, tid, {}, ascii4("\\NOPE\\X"))), ERRDOS, ERRbadpath);
    expect_error(send(smb(0x08, tid, {}, ascii4("\\README.TXT\\X"))), ERRDOS, ERRbadpath);

    expect_ok(send(smb(0x10, tid, {}, ascii4("\\DOCS"))));
    expect_ok(send(smb(0x10, tid, {}, ascii4("\\"))));
    expect_error(send(smb(0x10, tid, {}, ascii4("\\README.TXT"))), ERRDOS, ERRbadpath);
    expect_error(send(smb(0x10, tid, {}, ascii4("\\NOPE"))), ERRDOS, ERRbadpath);

    r = send(smb(0x80, tid));
    expect_ok(r);
    ASSERT_EQ(r[32], 5);
    EXPECT_EQ(get_le16(reply_word(r, 1)), 64);
    EXPECT_EQ(get_le16(reply_word(r, 2)), 512);
}

TEST_F(SmbFiles, ProcessExitClosesOnlyThatProcessesFiles)
{
    uint16_t tid = connect_core();
    bytes    r   = send(smb(0x02, tid, { 0, 0 }, ascii4("\\README.TXT")));
    uint16_t fid = get_le16(reply_word(r, 0));
    bytes    m   = smb(0x11, tid);

    m[26] = 0x99;
    expect_ok(send(m));
    expect_ok(send(smb(0x0A, tid, { fid, 1, 0, 0, 0 })));
    expect_ok(send(smb(0x11, tid)));
    expect_error(send(smb(0x0A, tid, { fid, 1, 0, 0, 0 })), ERRDOS, ERRbadfid);
}

TEST_F(SmbFiles, EverythingThatWouldChangeTheFolderIsRefused)
{
    uint16_t tid = connect_core();

    expect_error(send(smb(0x02, tid, { 1, 0 }, ascii4("\\README.TXT"))), ERRDOS, ERRnoaccess);
    expect_error(send(smb(0x02, tid, { 2, 0 }, ascii4("\\README.TXT"))), ERRDOS, ERRnoaccess);
    expect_error(send(smb(0x02, tid, { 0, 0 }, ascii4("\\DOCS"))), ERRDOS, ERRnoaccess);
    for (uint8_t command : { 0x00, 0x01, 0x03, 0x06, 0x07, 0x09, 0x0B, 0x0E, 0x0F, 0x22, 0x2F })
        expect_error(send(smb(command, tid, { 0, 0 }, ascii4("\\X"))), ERRDOS, ERRnoaccess);
}

TEST_F(SmbFiles, DotDotCannotClimbOutOfTheShare)
{
    uint16_t tid = connect_core();

    expect_error(send(smb(0x08, tid, {}, ascii4("\\..\\README.TXT"))), ERRDOS, ERRbadpath);
    expect_error(send(smb(0x08, tid, {}, ascii4("\\DOCS\\.."))), ERRDOS, ERRbadpath);
}

/* --- LAN Manager: Windows for Workgroups, LAN Manager 2.x, MS Client's full redirector --- */

TEST_F(SmbFiles, LanManagerTreeConnectHasTwoWords)
{
    bytes b = { 0 };
    send(smb(0x72, 0, {}, dialects({ "LANMAN1.0" })));
    put_str(b, "\\\\86BOXHOST\\SHARED");
    put_str(b, "?????");
    bytes r = send(smb(0x75, 0, { 0xFF, 0, 0, 1 }, b));
    expect_ok(r);
    EXPECT_EQ(r[32], 2);
    EXPECT_EQ(text(reply_bytes(r)), "A:");
    EXPECT_EQ(get_le16(&r[32 + 1 + 4]), 3);
}

TEST_F(SmbFiles, OpenAndXFollowsItsOpenMode)
{
    uint16_t tid = connect_lanman();

    bytes b;
    put_str(b, "\\README.TXT");
    bytes r = send(smb(0x2D, tid, { 0xFF, 0, 0, 0, 0x16, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0 }, b));
    expect_ok(r);
    ASSERT_EQ(r[32], 15);
    EXPECT_EQ(r[33], 0xFF);
    EXPECT_NE(get_le16(reply_word(r, 2)), 0);
    EXPECT_EQ(get_le16(reply_word(r, 3)), 1);
    EXPECT_EQ(get_le32(reply_word(r, 4)), SMB_FS_UNIX_TIME);
    EXPECT_EQ(get_le32(reply_word(r, 6)), smb_fs_size(smb_fs_lookup(smb_fs_root(), "README.TXT")));
    EXPECT_EQ(get_le16(reply_word(r, 11)), 1);

    EXPECT_EQ(open_andx(tid, "\\README.TXT", 1, 1), 0); /* for writing */
    EXPECT_EQ(open_andx(tid, "\\README.TXT", 0, 2), 0); /* truncating */
    EXPECT_NE(open_andx(tid, "\\README.TXT", 3, 1), 0); /* for execution */

    b.clear();
    put_str(b, "\\README.TXT");
    expect_error(send(smb(0x2D, tid, { 0xFF, 0, 0, 0, 0, 0, 0, 0, 0x10, 0, 0, 0, 0, 0, 0 }, b)), ERRDOS, ERRfilexists);
    b.clear();
    put_str(b, "\\NEW.TXT");
    expect_error(send(smb(0x2D, tid, { 0xFF, 0, 0, 0, 0, 0, 0, 0, 0x11, 0, 0, 0, 0, 0, 0 }, b)), ERRDOS, ERRnoaccess);
    expect_error(send(smb(0x2D, tid, { 0xFF, 0, 0, 0, 0, 0, 0, 0, 0x01, 0, 0, 0, 0, 0, 0 }, b)), ERRDOS, ERRbadfile);
}

TEST_F(SmbFiles, ReadAndXReturnsWholeFilesInChunks)
{
    uint16_t tid  = connect_lanman();
    uint16_t fid  = open_andx(tid, "\\DOCS\\PATTERN.TXT");
    bytes    data = read_all(tid, fid, 4096);
    uint8_t  line[35];

    ASSERT_EQ(data.size(), 35u * 1024);
    for (int i = 0; i < 1024; i += 100) {
        smb_fs_read(smb_fs_lookup(smb_fs_root(), "DOCS\\PATTERN.TXT"), uint32_t(i) * 35, line, 35);
        EXPECT_EQ(memcmp(&data[size_t(i) * 35], line, 35), 0);
    }
}

TEST_F(SmbFiles, ReadAndXLimitsWhatOneReplyCarries)
{
    uint16_t tid = connect_lanman();
    uint16_t fid = open_andx(tid, "\\DOCS\\PATTERN.TXT");
    bytes    r   = send(smb(0x2E, tid, { 0xFF, 0, fid, 0, 0, 0xFFFF, 0xFFFF, 0, 0, 0 }));

    expect_ok(r);
    ASSERT_EQ(r[32], 12);
    EXPECT_EQ(get_le16(reply_word(r, 5)), 4096);
    EXPECT_EQ(get_le16(reply_word(r, 6)) % 2, 0);
    EXPECT_LE(r.size(), 4356u);

    r = send(smb(0x2E, tid, { 0xFF, 0, fid, 0, 0, 16, 16, 0, 0, 0, 1, 0 }));
    expect_ok(r);
    EXPECT_EQ(get_le16(reply_word(r, 5)), 0); /* past 4 GiB */
}

TEST_F(SmbFiles, OpenChainedToRead)
{
    uint16_t tid = connect_lanman();
    bytes    name;
    put_str(name, "\\README.TXT");
    bytes m = smb(0x2D, tid, { 0x2E, 0, 0, 0, 0x16, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0 }, name);
    m[33 + 2] = uint8_t(m.size());
    m.push_back(10);
    put_le16(m, 0xFF); put_le16(m, 0);
    put_le16(m, 0xFFFF); /* the file just opened */
    put_le32(m, 0); put_le16(m, 4); put_le16(m, 4); put_le32(m, 0); put_le16(m, 0);
    put_le16(m, 0);

    bytes r = send(m);
    expect_ok(r);
    EXPECT_EQ(r[33], 0x2E);
    int at = get_le16(reply_word(r, 1));
    ASSERT_LT(size_t(at), r.size());
    EXPECT_EQ(r[at], 12);
    int count = get_le16(&r[at + 1 + 5 * 2]);
    int data  = get_le16(&r[at + 1 + 6 * 2]);
    ASSERT_EQ(count, 4);
    EXPECT_EQ(memcmp(&r[data], "This", 4), 0);
}

TEST_F(SmbFiles, LocksAreAlwaysGranted)
{
    uint16_t tid = connect_lanman();
    uint16_t fid = open_andx(tid, "\\README.TXT");
    bytes    r   = send(smb(0x24, tid, { 0xFF, 0, fid, 0, 0, 0, 0, 1 }, bytes(10, 0)));

    expect_ok(r);
    EXPECT_EQ(r[32], 2);
    expect_ok(send(smb(0x0C, tid, { fid, 1, 0, 0, 0 })));
    expect_ok(send(smb(0x0D, tid, { fid, 1, 0, 0, 0 })));
    expect_error(send(smb(0x24, tid, { 0xFF, 0, 99, 0, 0, 0, 0, 1 }, bytes(10, 0))), ERRDOS, ERRbadfid);
}

TEST_F(SmbFiles, FindFirstStandardEntries)
{
    uint16_t tid = connect_lanman();
    bytes    r   = send(find_first(tid, 1, 16, "\\*.*", 2));

    expect_ok(r);
    ASSERT_EQ(r[32], 10);
    EXPECT_EQ(get_le16(reply_word(r, 3)), 10); /* parameter count */
    const uint8_t *p = trans_params(r);
    EXPECT_EQ(get_le16(p + 2), 2);
    EXPECT_EQ(get_le16(p + 4), 1); /* end of search */

    const uint8_t *e = trans_data(r);
    EXPECT_EQ(get_le16(e), SMB_FS_DOS_DATE);
    EXPECT_EQ(get_le16(e + 8), SMB_FS_DOS_DATE);
    EXPECT_EQ(get_le32(e + 12), smb_fs_size(smb_fs_lookup(smb_fs_root(), "README.TXT")));
    EXPECT_EQ(get_le32(e + 16), 512u);
    EXPECT_EQ(get_le16(e + 20), 1);
    EXPECT_EQ(e[22], 10);
    EXPECT_EQ(text(e + 23), "README.TXT");
    e += 23 + 11;
    EXPECT_EQ(get_le16(e + 20), 0x10);
    EXPECT_EQ(text(e + 23), "DOCS");
    EXPECT_EQ(get_le16(p + 8), 23 + 11 + 23); /* the last name */
    /* closed at the end of the search, as flagged */
    expect_error(send(smb(0x34, tid, { get_le16(p) })), ERRDOS, ERRbadfid);
}

TEST_F(SmbFiles, FindFirstWithEaSizeAndResumeKeys)
{
    uint16_t tid = connect_lanman();
    bytes    r   = send(find_first(tid, 2, 16, "\\*.TXT", 4));

    expect_ok(r);
    const uint8_t *e = trans_data(r);
    EXPECT_EQ(get_le32(e), 3u);
    EXPECT_EQ(get_le32(e + 4 + 22), 0u);
    EXPECT_EQ(e[4 + 26], 10);
    EXPECT_EQ(text(e + 4 + 27), "README.TXT");
    expect_ok(send(smb(0x34, tid, { get_le16(trans_params(r)) })));
}

TEST_F(SmbFiles, FindNextContinuesWhereTheLastReplyStopped)
{
    uint16_t tid = connect_lanman();
    bytes    r   = send(find_first(tid, 1, 2, "\\DOCS\\*"));
    const uint8_t *p = trans_params(r);
    uint16_t sid = get_le16(p);

    EXPECT_EQ(get_le16(p + 2), 2);
    EXPECT_EQ(get_le16(p + 4), 0);
    EXPECT_EQ(text(trans_data(r) + 23), ".");

    r = send(find_next(tid, sid, 1, 2, 0, 8));
    expect_ok(r);
    ASSERT_EQ(get_le16(reply_word(r, 3)), 8);
    p = trans_params(r);
    EXPECT_EQ(get_le16(p), 1);
    EXPECT_EQ(get_le16(p + 2), 1);
    EXPECT_EQ(text(trans_data(r) + 23), "PATTERN.TXT");
}

TEST_F(SmbFiles, FindNextResumesByKeyOrByName)
{
    uint16_t tid = connect_lanman();
    bytes    r   = send(find_first(tid, 1, 1, "\\*", 4));
    uint16_t sid = get_le16(trans_params(r));
    uint32_t key = get_le32(trans_data(r));

    r = send(find_next(tid, sid, 1, 1, key, 4));
    EXPECT_EQ(text(trans_data(r) + 4 + 23), "DOCS");
    r = send(find_next(tid, sid, 1, 1, key, 0));
    EXPECT_EQ(text(trans_data(r) + 23), "DOCS");
    r = send(find_next(tid, sid, 1, 1, 0, 0, "README.TXT"));
    EXPECT_EQ(text(trans_data(r) + 23), "DOCS");
}

TEST_F(SmbFiles, FindReportsItsFailures)
{
    uint16_t tid = connect_lanman();

    expect_error(send(find_first(tid, 1, 8, "\\NOPE*")), ERRDOS, ERRbadfile);
    expect_error(send(find_first(tid, 1, 8, "\\NOPE\\*")), ERRDOS, ERRbadpath);
    expect_error(send(find_first(tid, 0x7777, 8, "\\*")), ERRDOS, ERRunknownlevel);
    expect_error(send(find_next(tid, 99, 1, 8, 0, 8)), ERRDOS, ERRbadfid);
    expect_error(send(smb(0x34, tid, { 99 })), ERRDOS, ERRbadfid);
}

TEST_F(SmbFiles, FindRunsOutOfSlotsThenRecovers)
{
    uint16_t tid = connect_lanman();
    uint16_t sid = 0;

    for (int i = 0; i < 8; i++)
        sid = get_le16(trans_params(send(find_first(tid, 1, 1, "\\*"))));
    expect_error(send(find_first(tid, 1, 1, "\\*")), ERRDOS, ERRnofids);
    expect_ok(send(smb(0x34, tid, { sid })));
    expect_ok(send(find_first(tid, 1, 1, "\\*")));
}

TEST_F(SmbFiles, QueryPathLevels)
{
    uint16_t tid = connect_lanman();
    bytes    r   = send(query_path(tid, 1, "\\README.TXT"));

    expect_ok(r);
    ASSERT_EQ(get_le16(reply_word(r, 1)), 22);
    EXPECT_EQ(get_le16(trans_data(r) + 8), SMB_FS_DOS_DATE);
    EXPECT_EQ(get_le32(trans_data(r) + 12), smb_fs_size(smb_fs_lookup(smb_fs_root(), "README.TXT")));
    EXPECT_EQ(get_le16(trans_data(r) + 20), 1);

    EXPECT_EQ(get_le16(reply_word(send(query_path(tid, 2, "\\README.TXT")), 1)), 26);
    EXPECT_EQ(get_le16(reply_word(send(query_path(tid, 4, "\\README.TXT")), 1)), 4);
    r = send(query_path(tid, 6, "\\README.TXT"));
    expect_ok(r);
    EXPECT_EQ(get_le16(reply_word(r, 1)), 0);

    r = send(query_path(tid, 0x101, "\\DOCS"));
    expect_ok(r);
    ASSERT_EQ(get_le16(reply_word(r, 1)), 40);
    EXPECT_EQ(get_le32(trans_data(r) + 32), 0x10u);

    r = send(query_path(tid, 0x102, "\\DOCS"));
    ASSERT_EQ(get_le16(reply_word(r, 1)), 24);
    EXPECT_EQ(trans_data(r)[21], 1);

    r = send(query_path(tid, 0x104, "\\README.TXT"));
    EXPECT_EQ(get_le32(trans_data(r)), 10u);
    EXPECT_EQ(memcmp(trans_data(r) + 4, "README.TXT", 10), 0);

    r = send(query_path(tid, 0x107, "\\README.TXT"));
    ASSERT_EQ(get_le16(reply_word(r, 1)), 72 + 10);
    EXPECT_EQ(get_le32(trans_data(r) + 40 + 8), smb_fs_size(smb_fs_lookup(smb_fs_root(), "README.TXT")));
    EXPECT_EQ(get_le32(trans_data(r) + 40 + 28), 10u);

    expect_error(send(query_path(tid, 0x7777, "\\README.TXT")), ERRDOS, ERRunknownlevel);
    expect_error(send(query_path(tid, 1, "\\NOPE")), ERRDOS, ERRbadfile);
}

TEST_F(SmbFiles, QueryFileUsesTheOpenFile)
{
    uint16_t tid = connect_lanman();
    uint16_t fid = open_andx(tid, "\\DOCS\\PATTERN.TXT");
    bytes    r   = send(query_file(tid, fid, 0x102));

    expect_ok(r);
    EXPECT_EQ(get_le32(trans_data(r) + 8), 35u * 1024);
    expect_error(send(query_file(tid, 99, 1)), ERRDOS, ERRbadfid);
}

TEST_F(SmbFiles, QueryFilesystemLevels)
{
    uint16_t tid = connect_lanman();
    bytes    r   = send(query_fs(tid, 1));

    expect_ok(r);
    ASSERT_EQ(get_le16(reply_word(r, 1)), 18);
    EXPECT_EQ(get_le32(trans_data(r) + 4), 64u);
    EXPECT_EQ(get_le32(trans_data(r) + 8), 2048u);
    EXPECT_EQ(get_le16(trans_data(r) + 16), 512);

    r = send(query_fs(tid, 2));
    EXPECT_EQ(trans_data(r)[4], 6);
    EXPECT_EQ(text(trans_data(r) + 5), VSERVER_SHARE_NAME);

    r = send(query_fs(tid, 0x102));
    EXPECT_EQ(get_le32(trans_data(r) + 12), 6u);
    EXPECT_EQ(memcmp(trans_data(r) + 18, VSERVER_SHARE_NAME, sizeof(VSERVER_SHARE_NAME) - 1), 0);

    EXPECT_EQ(get_le16(reply_word(send(query_fs(tid, 0x103)), 1)), 24);
    EXPECT_EQ(get_le32(trans_data(send(query_fs(tid, 0x104)))), 7u);

    r = send(query_fs(tid, 0x105));
    EXPECT_NE(get_le32(trans_data(r)) & 0x00080000u, 0u);
    EXPECT_EQ(memcmp(trans_data(r) + 12, "FAT", 3), 0);

    expect_error(send(query_fs(tid, 0x7777)), ERRDOS, ERRunknownlevel);
}

TEST_F(SmbFiles, Trans2CommandsThatWriteAreRefused)
{
    uint16_t tid = connect_lanman();

    for (uint16_t subcommand : { 0, 4, 6, 8, 13 })
        expect_error(send(trans2(tid, subcommand, bytes(12, 0))), ERRDOS, ERRnoaccess);
}

/* --- NT LM: Windows 95 to XP --- */

TEST_F(SmbFiles, NtNegotiateLayout)
{
    bytes r = send(smb(0x72, 0, {}, dialects({ "NT LM 0.12" })));
    const uint8_t *w = &r[33];

    ASSERT_EQ(r[32], 17);
    EXPECT_EQ(get_le16(w), 0);
    EXPECT_EQ(w[2], 0x03);
    EXPECT_EQ(get_le16(w + 3), 2);
    EXPECT_EQ(get_le16(w + 5), 1);
    EXPECT_EQ(get_le32(w + 7), 4356u);
    EXPECT_EQ(get_le32(w + 19), 0u); /* no Unicode and no NT SMBs: a Windows 9x server */
    EXPECT_EQ(w[33], 8);
    int bytes_length = get_le16(w + 34);
    EXPECT_EQ(bytes_length, 8 + int(strlen(VSERVER_WORKGROUP_NAME)) + 1);
    EXPECT_EQ(text(w + 36 + 8), VSERVER_WORKGROUP_NAME);
    EXPECT_EQ(r.size(), size_t(32 + 1 + 34 + 2 + bytes_length));
}

TEST_F(SmbFiles, NtTreeConnectNamesTheFilesystem)
{
    bytes b = { 0 };
    send(smb(0x72, 0, {}, dialects({ "NT LM 0.12" })));
    put_str(b, "\\\\86boxhost\\shared");
    put_str(b, "?????");
    bytes r = send(smb(0x75, 0, { 0xFF, 0, 0, 1 }, b));
    expect_ok(r);
    ASSERT_EQ(r[32], 3);
    EXPECT_EQ(get_le16(reply_word(r, 2)), 1);
    EXPECT_EQ(text(reply_bytes(r)), "A:");
    EXPECT_EQ(text(reply_bytes(r) + 3), "FAT");
}

TEST_F(SmbFiles, RepliesNeverClaimUnicodeOrNtStatus)
{
    bytes m = smb(0x72, 0, {}, dialects({ "NT LM 0.12" }));
    m[10] = 0x41;
    m[11] = 0xC8;
    bytes r = send(m);
    EXPECT_EQ(get_le16(&r[10]), 0x0001);
}

TEST_F(SmbFiles, FindBothDirectoryEntries)
{
    uint16_t tid = connect_nt();
    bytes    r   = send(find_first(tid, 0x104, 16, "\\*"));

    expect_ok(r);
    ASSERT_EQ(get_le16(trans_params(r) + 2), 2);
    const uint8_t *e    = trans_data(r);
    uint32_t       next = get_le32(e);
    EXPECT_EQ(next % 4, 0u);
    EXPECT_EQ(get_le32(e + 40), smb_fs_size(smb_fs_lookup(smb_fs_root(), "README.TXT")));
    EXPECT_EQ(get_le32(e + 56), 1u);
    EXPECT_EQ(get_le32(e + 60), 10u);
    EXPECT_EQ(memcmp(e + 94, "README.TXT", 10), 0);
    e += next;
    EXPECT_EQ(get_le32(e), 0u);
    EXPECT_EQ(get_le32(e + 56), 0x10u);
    EXPECT_EQ(memcmp(e + 94, "DOCS", 4), 0);
}

TEST_F(SmbFiles, OtherNtFindLevels)
{
    uint16_t tid = connect_nt();
    bytes    r   = send(find_first(tid, 0x101, 1, "\\README.TXT"));
    EXPECT_EQ(memcmp(trans_data(r) + 64, "README.TXT", 10), 0);
    r = send(find_first(tid, 0x102, 1, "\\README.TXT"));
    EXPECT_EQ(memcmp(trans_data(r) + 68, "README.TXT", 10), 0);
    r = send(find_first(tid, 0x103, 1, "\\README.TXT"));
    EXPECT_EQ(get_le32(trans_data(r) + 8), 10u);
    EXPECT_EQ(memcmp(trans_data(r) + 12, "README.TXT", 10), 0);
}

TEST_F(SmbFiles, FindStopsAtTheClientsDataLimitAndCarriesOn)
{
    uint16_t tid = connect_nt();
    bytes    p;
    put_le16(p, 0x16); put_le16(p, 16); put_le16(p, 0); put_le16(p, 0x104); put_le32(p, 0);
    put_str(p, "\\*");
    bytes r = send(trans2(tid, 1, p, 112));

    expect_ok(r);
    ASSERT_EQ(get_le16(trans_params(r) + 2), 1);
    EXPECT_EQ(get_le16(trans_params(r) + 4), 0);
    r = send(find_next(tid, get_le16(trans_params(r)), 0x104, 16, 0, 8));
    ASSERT_EQ(get_le16(trans_params(r)), 1);
    EXPECT_EQ(memcmp(trans_data(r) + 94, "DOCS", 4), 0);
}

/* --- trees --- */

TEST_F(SmbFiles, UnknownSharesAreRefused)
{
    bytes b = { 0 };
    send(smb(0x72, 0, {}, dialects({ "NT LM 0.12" })));
    put_str(b, "\\\\86BOXHOST\\C$");
    put_str(b, "?????");
    expect_error(send(smb(0x75, 0, { 0xFF, 0, 0, 1 }, b)), ERRSRV, ERRinvnetname);
}

TEST_F(SmbFiles, FileCommandsNeedTheFolderTree)
{
    uint16_t share = connect_nt();
    uint16_t ipc   = tree_connect("\\\\86BOXHOST\\IPC$");

    EXPECT_NE(share, ipc);
    expect_error(send(find_first(ipc, 1, 8, "\\*")), ERRSRV, ERRinvtid);
    expect_error(send(find_first(0, 1, 8, "\\*")), ERRSRV, ERRinvtid);
    expect_error(send(find_first(99, 1, 8, "\\*")), ERRSRV, ERRinvtid);
    expect_error(send(smb(0x08, ipc, {}, ascii4("\\README.TXT"))), ERRSRV, ERRinvtid);
    expect_error(send(smb(0x80, ipc)), ERRSRV, ERRinvtid);
}

TEST_F(SmbFiles, TreesAreIndependent)
{
    uint16_t first  = connect_nt();
    uint16_t second = tree_connect();
    uint16_t a      = open_andx(first, "\\README.TXT");
    uint16_t b      = open_andx(second, "\\README.TXT");

    EXPECT_NE(first, second);
    expect_ok(send(smb(0x71, second)));
    expect_ok(send(smb(0x2E, first, { 0xFF, 0, a, 0, 0, 1, 1, 0, 0, 0 })));
    expect_error(send(smb(0x2E, first, { 0xFF, 0, b, 0, 0, 1, 1, 0, 0, 0 })), ERRDOS, ERRbadfid);
    expect_error(send(find_first(second, 1, 8, "\\*")), ERRSRV, ERRinvtid);
}

TEST_F(SmbFiles, LogoffEndsEverything)
{
    uint16_t tid = connect_nt();
    open_andx(tid, "\\README.TXT");
    expect_ok(send(smb(0x74, tid, { 0xFF, 0 })));
    expect_error(send(find_first(tid, 1, 8, "\\*")), ERRSRV, ERRinvtid);
}

TEST_F(SmbFiles, TreesAndFilesRunOutCleanly)
{
    uint16_t tid = connect_nt();

    for (int i = 0; i < 16; i++)
        EXPECT_NE(open_andx(tid, "\\README.TXT"), 0);
    bytes b;
    put_str(b, "\\README.TXT");
    expect_error(send(smb(0x2D, tid, { 0xFF, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0 }, b)), ERRDOS, ERRnofids);

    for (int i = 0; i < 7; i++)
        EXPECT_NE(tree_connect(), 0);
    bytes t = { 0 };
    put_str(t, "\\\\86BOXHOST\\SHARED");
    put_str(t, "?????");
    expect_error(send(smb(0x75, 0, { 0xFF, 0, 0, 1 }, t)), ERRSRV, 1);
}

TEST_F(SmbFiles, EchoReturnsItsData)
{
    connect_nt();
    bytes r = send(smb(0x2B, 0, { 1 }, { 1, 2, 3, 4 }));
    expect_ok(r);
    EXPECT_EQ(get_le16(&r[33 + 2]), 4);
    EXPECT_EQ(reply_bytes(r)[3], 4);
}

TEST_F(SmbFiles, TreeConnectChainedFromSessionSetup)
{
    send(smb(0x72, 0, {}, dialects({ "NT LM 0.12" })));
    bytes tree = { 4 };
    tree.push_back(0xFF); tree.push_back(0); put_le16(tree, 0); put_le16(tree, 0); put_le16(tree, 1);
    bytes tb = { 0 };
    put_str(tb, "\\\\86BOXHOST\\SHARED");
    put_str(tb, "A:");
    put_le16(tree, uint16_t(tb.size()));
    tree.insert(tree.end(), tb.begin(), tb.end());

    bytes m = smb(0x73, 0, { 0x75, 0, 0, 4356, 2, 1, 0, 0, 0, 1, 0, 0, 0 }, { 0, 0, 0 });
    m[33 + 2] = uint8_t(m.size());
    m.insert(m.end(), tree.begin(), tree.end());
    bytes r = send(m);
    expect_ok(r);
    EXPECT_EQ(r[33], 0x75);
    expect_ok(send(find_first(get_le16(&r[24]), 1, 8, "\\*")));
}

/* --- domain logon --- */

static const uint8_t test_challenge[8] = { '8', '6', 'B', 'o', 'x', 'S', 'M', 'B' };

static bytes
answer(const uint8_t hash[16])
{
    uint8_t padded[21] = { 0 };
    bytes   out(24);

    memcpy(padded, hash, 16);
    for (int i = 0; i < 3; i++)
        modem_des_encrypt_block(padded + i * 7, test_challenge, &out[size_t(i) * 8]);
    return out;
}

static bytes
lm_answer(const std::string &password)
{
    static const uint8_t magic[8] = { 'K', 'G', 'S', '!', '@', '#', '$', '%' };
    uint8_t              upper[14] = { 0 };
    uint8_t              hash[16];

    for (size_t i = 0; i < password.size(); i++)
        upper[i] = uint8_t(toupper(password[i]));
    modem_des_encrypt_block(upper, magic, hash);
    modem_des_encrypt_block(upper + 7, magic, hash + 8);
    return answer(hash);
}

static bytes
nt_answer(const std::string &password)
{
    bytes   wide;
    uint8_t hash[16];

    for (char c : password) {
        wide.push_back(uint8_t(c));
        wide.push_back(0);
    }
    modem_md4(wide.data(), wide.size(), hash);
    return answer(hash);
}

static bytes
nt_setup(const std::string &user, const bytes &oem, const bytes &wide)
{
    bytes b = oem;
    b.insert(b.end(), wide.begin(), wide.end());
    put_str(b, user);
    put_str(b, VSERVER_DOMAIN_NAME);
    return smb(0x73, 0, { 0xFF, 0, 4356, 2, 1, 0, 0, uint16_t(oem.size()), uint16_t(wide.size()), 0, 0, 0, 0 }, b);
}

static bytes
lm_setup(const std::string &user, const bytes &oem)
{
    bytes b = oem;
    put_str(b, user);
    put_str(b, VSERVER_DOMAIN_NAME);
    return smb(0x73, 0, { 0xFF, 0, 4356, 2, 1, 0, 0, uint16_t(oem.size()), 0, 0 }, b);
}

static bytes
rap_call(uint16_t tid, const bytes &params)
{
    static const char name[] = "\\PIPE\\LANMAN";
    int   params_at = 32 + 1 + 14 * 2 + 2 + int(sizeof(name));
    bytes b(name, name + sizeof(name));

    b.insert(b.end(), params.begin(), params.end());
    return smb(0x25, tid, { uint16_t(params.size()), 0, 8, 1024, 0, 0, 0, 0, 0, uint16_t(params.size()),
                            uint16_t(params_at), 0, uint16_t(params_at), 0 }, b);
}

static bytes
user_logon(uint16_t tid, const std::string &user = "86BOX")
{
    bytes p;
    put_le16(p, 132);
    put_str(p, "OOWb54WrLh");
    put_str(p, "WB21BWDWWDDDDDDDzzzD");
    put_le16(p, 1);
    bytes block(54, 0);
    memcpy(block.data(), user.data(), user.size());
    p.insert(p.end(), block.begin(), block.end());
    put_le16(p, 1024);
    return rap_call(tid, p);
}

class SmbLogon : public SmbFiles {
protected:
    uint16_t session_and_ipc(const bytes &setup, int expected_action) {
        bytes r = send(setup);
        expect_ok(r);
        EXPECT_EQ(get_le16(reply_word(r, 2)), expected_action);
        EXPECT_EQ(get_le16(&r[28]), 100);
        return tree_connect("\\\\" VSERVER_SERVER_NAME "\\" VSERVER_IPC_SHARE_NAME);
    }
};

TEST_F(SmbLogon, Windows9xLogsOnWithTheLmResponse)
{
    send(smb(0x72, 0, {}, dialects({ "NT LM 0.12" })));
    uint16_t ipc = session_and_ipc(nt_setup("86BOX", lm_answer(VSERVER_LOGON_PASSWORD), {}), 0);
    bytes    r   = send(user_logon(ipc));

    expect_ok(r);
    EXPECT_EQ(get_le16(reply_word(r, 3)), 6); /* an error, a converter and a size */
    EXPECT_EQ(get_le16(trans_params(r)), 0);
    const uint8_t *d = trans_data(r);
    EXPECT_EQ(text(d + 2), VSERVER_LOGON_USERNAME);
    EXPECT_EQ(get_le16(d + 24), 1);
    EXPECT_EQ(get_le32(d + 46), 0xFFFFFFFFu);
    EXPECT_EQ(text(d + get_le32(d + 62)), "\\\\86BOXHOST");
    EXPECT_EQ(text(d + get_le32(d + 66)), VSERVER_WORKGROUP_NAME);
    EXPECT_EQ(text(d + get_le32(d + 70)), "");
}

TEST_F(SmbLogon, TheNtResponseAlsoProvesThePassword)
{
    send(smb(0x72, 0, {}, dialects({ "NT LM 0.12" })));
    uint16_t ipc = session_and_ipc(nt_setup("86box", bytes(24, 0), nt_answer(VSERVER_LOGON_PASSWORD)), 0);
    EXPECT_EQ(get_le16(trans_params(send(user_logon(ipc)))), 0);
}

TEST_F(SmbLogon, LanManagerClientsLogOnToo)
{
    send(smb(0x72, 0, {}, dialects({ "DOS LM1.2X002" })));
    uint16_t ipc = session_and_ipc(lm_setup("86BOX", lm_answer("86BOX")), 0);
    EXPECT_EQ(get_le16(trans_params(send(user_logon(ipc)))), 0);
}

TEST_F(SmbLogon, TheNtPasswordIsCaseSensitive)
{
    send(smb(0x72, 0, {}, dialects({ "NT LM 0.12" })));
    uint16_t ipc = session_and_ipc(nt_setup("86BOX", {}, nt_answer("86BOX")), 1);
    EXPECT_EQ(get_le16(trans_params(send(user_logon(ipc)))), 5);
}

TEST_F(SmbLogon, AWrongPasswordIsAGuestWhoCannotLogOn)
{
    send(smb(0x72, 0, {}, dialects({ "NT LM 0.12" })));
    uint16_t ipc = session_and_ipc(nt_setup("86BOX", lm_answer("WRONG"), {}), 1);
    bytes    r   = send(user_logon(ipc));

    expect_ok(r);
    EXPECT_EQ(get_le16(trans_params(r)), 5);
    EXPECT_EQ(get_le16(reply_word(r, 1)), 0);
}

TEST_F(SmbLogon, GuestsCanReadSharedAndUseIpcButNotConnectToNetlogon)
{
    send(smb(0x72, 0, {}, dialects({ "NT LM 0.12" })));
    uint16_t share = (send(nt_setup("SOMEONE", { 0 }, {})), tree_connect());
    ASSERT_NE(share, 0);
    expect_ok(send(find_first(share, 1, 8, "\\*")));

    bytes netlogon = { 0 };
    put_str(netlogon, "\\\\" VSERVER_SERVER_NAME "\\" VSERVER_LOGON_SHARE_NAME);
    put_str(netlogon, "?????");
    expect_error(send(smb(0x75, 0, { 0xFF, 0, 0, 1 }, netlogon)), ERRDOS, ERRnoaccess);

    EXPECT_NE(tree_connect("\\\\" VSERVER_SERVER_NAME "\\" VSERVER_IPC_SHARE_NAME), 0);
}

TEST_F(SmbLogon, AnotherUserWithTheRightPasswordIsAGuest)
{
    send(smb(0x72, 0, {}, dialects({ "NT LM 0.12" })));
    session_and_ipc(nt_setup("SOMEONE", lm_answer(VSERVER_LOGON_PASSWORD), {}), 1);
}

TEST_F(SmbLogon, ClippedRequestsAreGuests)
{
    send(smb(0x72, 0, {}, dialects({ "NT LM 0.12" })));
    bytes setup = nt_setup("86BOX", lm_answer(VSERVER_LOGON_PASSWORD), {});
    /* a password length running past the end of the message */
    setup[33 + 7 * 2] = 200;
    session_and_ipc(setup, 1);
    /* no terminator on the user name */
    bytes b = lm_answer(VSERVER_LOGON_PASSWORD);
    b.insert(b.end(), { '8', '6' });
    session_and_ipc(smb(0x73, 0, { 0xFF, 0, 4356, 2, 1, 0, 0, 24, 0, 0, 0, 0, 0 }, b), 1);
}

TEST_F(SmbLogon, LogoffForgetsTheAccount)
{
    send(smb(0x72, 0, {}, dialects({ "NT LM 0.12" })));
    uint16_t ipc = session_and_ipc(nt_setup("86BOX", lm_answer(VSERVER_LOGON_PASSWORD), {}), 0);
    send(smb(0x74, ipc, { 0xFF, 0 }));
    ipc = tree_connect("\\\\86BOXHOST\\IPC$");
    EXPECT_EQ(get_le16(trans_params(send(user_logon(ipc)))), 5);
}

TEST_F(SmbLogon, TheLogonShareIsEmptyAndSeparate)
{
    send(smb(0x72, 0, {}, dialects({ "NT LM 0.12" })));
    uint16_t logon = (send(nt_setup("86BOX", lm_answer(VSERVER_LOGON_PASSWORD), {})),
                      tree_connect("\\\\" VSERVER_SERVER_NAME "\\" VSERVER_LOGON_SHARE_NAME));

    ASSERT_NE(logon, 0);
    expect_error(send(find_first(logon, 1, 8, "\\*")), ERRDOS, ERRbadfile);
    expect_error(send(query_path(logon, 1, "\\SCRIPTS.BAT")), ERRDOS, ERRbadfile);
    expect_error(send(query_path(logon, 1, "\\CONFIG.POL")), ERRDOS, ERRbadfile);
    expect_error(send(query_path(logon, 1, "\\README.TXT")), ERRDOS, ERRbadfile);
    expect_error(send(smb(0x2D, logon, { 0xFF, 0, 0, 2, 0, 0, 0, 0, 0x12, 0, 0, 0, 0, 0, 0 }, { 'X', 0 })), ERRDOS,
                 ERRnoaccess);
}

TEST_F(SmbLogon, ACoreSessionStaysShareLevel)
{
    uint16_t tid = connect_core();
    expect_ok(send(search(0x81, tid, 8, 0, "\\*.*")));
}

static bytes
account_list(uint16_t call, const std::string &user = "")
{
    bytes p;
    put_le16(p, call);
    put_str(p, user.empty() ? "WrLeh" : "zWrLeh");
    put_str(p, "B21");
    if (!user.empty())
        put_str(p, user);
    put_le16(p, 0); put_le16(p, 1024);
    return p;
}

TEST_F(SmbLogon, ListsTheAccountAndItsGroup)
{
    send(smb(0x72, 0, {}, dialects({ "NT LM 0.12" })));
    uint16_t ipc = session_and_ipc(nt_setup("86BOX", lm_answer(VSERVER_LOGON_PASSWORD), {}), 0);

    bytes r = send(rap_call(ipc, account_list(53)));
    EXPECT_EQ(get_le16(trans_params(r)), 0);
    EXPECT_EQ(get_le16(trans_params(r) + 4), 1);
    EXPECT_EQ(get_le16(reply_word(r, 1)), 21);
    EXPECT_EQ(text(trans_data(r)), VSERVER_LOGON_USERNAME);

    r = send(rap_call(ipc, account_list(47)));
    EXPECT_EQ(text(trans_data(r)), VSERVER_LOGON_GROUP_NAME);

    r = send(rap_call(ipc, account_list(59, "86BOX")));
    EXPECT_EQ(get_le16(trans_params(r) + 4), 1);
    EXPECT_EQ(text(trans_data(r)), VSERVER_LOGON_GROUP_NAME);
}

TEST_F(SmbLogon, ASmallListBufferReportsMoreData)
{
    send(smb(0x72, 0, {}, dialects({ "NT LM 0.12" })));
    uint16_t ipc = session_and_ipc(nt_setup("86BOX", lm_answer(VSERVER_LOGON_PASSWORD), {}), 0);
    bytes    p;
    put_le16(p, 53);
    put_str(p, "WrLeh");
    put_str(p, "B21");
    put_le16(p, 0); put_le16(p, 10);
    bytes r = send(rap_call(ipc, p));
    EXPECT_EQ(get_le16(trans_params(r)), 234);
    EXPECT_EQ(get_le16(trans_params(r) + 6), 1);
}

/* --- NTLMv2, which clients with LMCompatibility 3 send instead --- */

static void
hmac_md5(const bytes &key, const bytes &data, uint8_t out[16])
{
    bytes inner(64, 0x36), outer(64, 0x5C);
    for (size_t i = 0; i < key.size(); i++) {
        inner[i] ^= key[i];
        outer[i] ^= key[i];
    }
    inner.insert(inner.end(), data.begin(), data.end());
    uint8_t digest[16];
    modem_md5(inner.data(), inner.size(), digest);
    outer.insert(outer.end(), digest, digest + 16);
    modem_md5(outer.data(), outer.size(), out);
}

static bytes
v2_answer(const std::string &password, const std::string &user, const std::string &domain, const bytes &block)
{
    bytes wide, identity, message(test_challenge, test_challenge + 8);
    uint8_t nt[16], key[16], digest[16];

    for (char c : password) { wide.push_back(uint8_t(c)); wide.push_back(0); }
    modem_md4(wide.data(), wide.size(), nt);
    for (char c : user) { identity.push_back(uint8_t(toupper(c))); identity.push_back(0); }
    for (char c : domain) { identity.push_back(uint8_t(c)); identity.push_back(0); }
    hmac_md5(bytes(nt, nt + 16), identity, key);
    message.insert(message.end(), block.begin(), block.end());
    hmac_md5(bytes(key, key + 16), message, digest);
    bytes out(digest, digest + 16);
    out.insert(out.end(), block.begin(), block.end());
    return out;
}

TEST_F(SmbLogon, AcceptsLmv2)
{
    send(smb(0x72, 0, {}, dialects({ "NT LM 0.12" })));
    bytes answer = v2_answer(VSERVER_LOGON_PASSWORD, "86box", VSERVER_DOMAIN_NAME, { 1, 2, 3, 4, 5, 6, 7, 8 });
    ASSERT_EQ(answer.size(), 24u);
    uint16_t ipc = session_and_ipc(nt_setup("86box", answer, {}), 0);
    EXPECT_EQ(get_le16(trans_params(send(user_logon(ipc)))), 0);
}

TEST_F(SmbLogon, AcceptsNtlmv2)
{
    send(smb(0x72, 0, {}, dialects({ "NT LM 0.12" })));
    bytes block(40, 0x5A);
    session_and_ipc(nt_setup("86BOX", bytes(24, 0),
                             v2_answer(VSERVER_LOGON_PASSWORD, "86BOX", VSERVER_DOMAIN_NAME, block)), 0);
}

TEST_F(SmbLogon, RefusesNtlmv2WithTheWrongPassword)
{
    send(smb(0x72, 0, {}, dialects({ "NT LM 0.12" })));
    bytes block(40, 0x5A);
    session_and_ipc(nt_setup("86BOX", v2_answer("WRONG", "86BOX", VSERVER_DOMAIN_NAME, { 1, 2, 3, 4, 5, 6, 7, 8 }),
                             v2_answer("WRONG", "86BOX", VSERVER_DOMAIN_NAME, block)), 1);
}
