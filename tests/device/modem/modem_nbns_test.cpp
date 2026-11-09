#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include <86box/network/net_vserver_config.h>
#include <86box/network/net_nbns.h>

static const uint32_t server_ip = 0x0A000204;
static const uint32_t guest_ip  = 0x0A00020F;

enum { OP_QUERY = 0, OP_REGISTER = 5, OP_RELEASE = 6, OP_REFRESH = 8, OP_REFRESH_ALT = 9, OP_MULTIHOMED = 15 };

static std::vector<uint8_t>
encode_name(const std::string &text, uint8_t suffix)
{
    uint8_t name[16];
    memset(name, ' ', sizeof(name));
    memcpy(name, text.data(), text.size());
    name[15] = suffix;

    std::vector<uint8_t> out = { 32 };
    for (uint8_t byte : name) {
        out.push_back(static_cast<uint8_t>('A' + (byte >> 4)));
        out.push_back(static_cast<uint8_t>('A' + (byte & 0x0F)));
    }
    out.push_back(0);
    return out;
}

static std::vector<uint8_t>
make_request(int opcode, const std::string &text, uint8_t suffix, uint16_t qtype = 0x0020)
{
    std::vector<uint8_t> msg = { 0x12, 0x34, static_cast<uint8_t>(opcode << 3), 0x10, 0, 1, 0, 0, 0, 0, 0, 0 };
    std::vector<uint8_t> name = encode_name(text, suffix);
    msg.insert(msg.end(), name.begin(), name.end());
    msg.push_back(static_cast<uint8_t>(qtype >> 8));
    msg.push_back(static_cast<uint8_t>(qtype));
    msg.push_back(0);
    msg.push_back(1);
    return msg;
}

static std::vector<uint8_t>
make_with_address(int opcode, const std::string &text, uint8_t suffix, uint16_t nb_flags, uint32_t address)
{
    std::vector<uint8_t> msg = make_request(opcode, text, suffix);
    msg[11] = 1; /* ARCOUNT */
    const uint8_t rr[] = { 0xC0, 0x0C, 0x00, 0x20, 0x00, 0x01, 0, 0, 0x0E, 0x10, 0, 6,
                           static_cast<uint8_t>(nb_flags >> 8), static_cast<uint8_t>(nb_flags),
                           static_cast<uint8_t>(address >> 24), static_cast<uint8_t>(address >> 16),
                           static_cast<uint8_t>(address >> 8), static_cast<uint8_t>(address) };
    msg.insert(msg.end(), rr, rr + sizeof(rr));
    return msg;
}

static std::vector<uint8_t>
ask(nbns_t *nbns, const std::vector<uint8_t> &msg)
{
    std::vector<uint8_t> reply(NBNS_MAX_REPLY);
    int length = nbns_process(nbns, guest_ip, msg.data(), static_cast<int>(msg.size()), reply.data(),
                              static_cast<int>(reply.size()));
    reply.resize(static_cast<size_t>(length));
    return reply;
}

static uint16_t
flags_of(const std::vector<uint8_t> &reply)
{
    return static_cast<uint16_t>((reply[2] << 8) | reply[3]);
}

static uint32_t
address_at(const std::vector<uint8_t> &reply, size_t pos)
{
    return (static_cast<uint32_t>(reply[pos]) << 24) | (static_cast<uint32_t>(reply[pos + 1]) << 16)
         | (static_cast<uint32_t>(reply[pos + 2]) << 8) | reply[pos + 3];
}

/* 12 header + 34 name + 10 record header + 2 NB_FLAGS */
static const size_t first_address = 12 + 34 + 10 + 2;

class ModemNbns : public ::testing::Test {
protected:
    void SetUp() override { nbns = nbns_init(nullptr, server_ip); ASSERT_NE(nbns, nullptr); }
    void TearDown() override { nbns_close(nbns); }
    nbns_t *nbns = nullptr;
};

TEST_F(ModemNbns, ServerNameFitsInANetbiosName)
{
    EXPECT_LE(strlen(VSERVER_SERVER_NAME), 15u);
}

TEST_F(ModemNbns, ResolvesThePreRegisteredServerName)
{
    for (uint8_t suffix : { 0x00, 0x03, 0x20 }) {
        auto reply = ask(nbns, make_request(OP_QUERY, VSERVER_SERVER_NAME, suffix));
        ASSERT_FALSE(reply.empty());
        EXPECT_EQ(flags_of(reply), 0x8580);
        EXPECT_EQ(reply[0], 0x12);
        EXPECT_EQ(reply[1], 0x34);
        EXPECT_EQ(reply[7], 1);
        EXPECT_EQ(address_at(reply, first_address), server_ip);
    }
}

TEST_F(ModemNbns, WorkgroupResolvesAsBroadcastGroup)
{
    auto reply = ask(nbns, make_request(OP_QUERY, VSERVER_WORKGROUP_NAME, 0x00));
    ASSERT_FALSE(reply.empty());
    EXPECT_EQ(flags_of(reply), 0x8580);
    EXPECT_NE(reply[first_address - 2] & 0x80, 0);
    EXPECT_EQ(address_at(reply, first_address), 0xFFFFFFFFu);
}

TEST_F(ModemNbns, UnknownNameGetsTheRfcNegativeResponse)
{
    auto reply = ask(nbns, make_request(OP_QUERY, "NOSUCHNAME", 0x00));
    ASSERT_EQ(reply.size(), 12u + 34u + 10u);
    EXPECT_EQ(flags_of(reply), 0x8583);
    EXPECT_EQ(reply[7], 0);
    EXPECT_EQ(reply[12 + 34 + 1], 0x0A); /* RR_TYPE NULL */
}

TEST_F(ModemNbns, RegistersAndResolvesAGuestName)
{
    auto reply = ask(nbns, make_with_address(OP_REGISTER, "WIN95", 0x00, 0x6000, guest_ip));
    ASSERT_EQ(reply.size(), 12u + 34u + 16u);
    EXPECT_EQ(flags_of(reply), 0xAD80);

    reply = ask(nbns, make_request(OP_QUERY, "WIN95", 0x00));
    ASSERT_FALSE(reply.empty());
    EXPECT_EQ(flags_of(reply), 0x8580);
    EXPECT_EQ(address_at(reply, first_address), guest_ip);
}

TEST_F(ModemNbns, SuffixesAreSeparateNames)
{
    ask(nbns, make_with_address(OP_REGISTER, "WIN95", 0x00, 0x6000, guest_ip));

    EXPECT_EQ(flags_of(ask(nbns, make_request(OP_QUERY, "WIN95", 0x20))), 0x8583);
}

TEST_F(ModemNbns, RefusesTheServerNameToTheGuest)
{
    auto reply = ask(nbns, make_with_address(OP_REGISTER, VSERVER_SERVER_NAME, 0x20, 0x6000, guest_ip));
    ASSERT_FALSE(reply.empty());
    EXPECT_EQ(flags_of(reply), 0xAD86);
    EXPECT_EQ(address_at(reply, first_address), server_ip);
}

TEST_F(ModemNbns, RefusesANameOwnedByAnotherAddress)
{
    ask(nbns, make_with_address(OP_REGISTER, "WIN95", 0x00, 0x6000, guest_ip));

    auto reply = ask(nbns, make_with_address(OP_REGISTER, "WIN95", 0x00, 0x6000, guest_ip + 1));
    EXPECT_EQ(flags_of(reply), 0xAD86);
    EXPECT_EQ(address_at(reply, first_address), guest_ip);
}

TEST_F(ModemNbns, ReRegistrationBySameOwnerSucceeds)
{
    ask(nbns, make_with_address(OP_REGISTER, "WIN95", 0x00, 0x6000, guest_ip));

    EXPECT_EQ(flags_of(ask(nbns, make_with_address(OP_REGISTER, "WIN95", 0x00, 0x6000, guest_ip))), 0xAD80);
}

TEST_F(ModemNbns, GuestCanJoinTheWorkgroup)
{
    EXPECT_EQ(flags_of(ask(nbns, make_with_address(OP_REGISTER, VSERVER_WORKGROUP_NAME, 0x00, 0xE000, guest_ip))), 0xAD80);
    EXPECT_EQ(flags_of(ask(nbns, make_with_address(OP_REGISTER, VSERVER_WORKGROUP_NAME, 0x1E, 0xE000, guest_ip))), 0xAD80);
}

TEST_F(ModemNbns, UniqueRegistrationOfAGroupNameIsRefused)
{
    EXPECT_EQ(flags_of(ask(nbns, make_with_address(OP_REGISTER, VSERVER_WORKGROUP_NAME, 0x00, 0x6000, guest_ip))), 0xAD86);
}

TEST_F(ModemNbns, RefreshIsAnsweredAsARegistration)
{
    for (int opcode : { OP_REFRESH, OP_REFRESH_ALT, OP_MULTIHOMED }) {
        auto reply = ask(nbns, make_with_address(opcode, "WIN95", 0x00, 0x6000, guest_ip));
        ASSERT_FALSE(reply.empty());
        EXPECT_EQ(flags_of(reply), 0xAD80);
    }
}

TEST_F(ModemNbns, ReleaseRemovesTheName)
{
    ask(nbns, make_with_address(OP_REGISTER, "WIN95", 0x00, 0x6000, guest_ip));

    auto reply = ask(nbns, make_with_address(OP_RELEASE, "WIN95", 0x00, 0x6000, guest_ip));
    ASSERT_FALSE(reply.empty());
    EXPECT_EQ(flags_of(reply), 0xB400);
    EXPECT_EQ(flags_of(ask(nbns, make_request(OP_QUERY, "WIN95", 0x00))), 0x8583);
}

TEST_F(ModemNbns, ReleaseByAnotherAddressIsRefused)
{
    ask(nbns, make_with_address(OP_REGISTER, "WIN95", 0x00, 0x6000, guest_ip));

    EXPECT_EQ(flags_of(ask(nbns, make_with_address(OP_RELEASE, "WIN95", 0x00, 0x6000, guest_ip + 1))), 0xB406);
    EXPECT_EQ(flags_of(ask(nbns, make_request(OP_QUERY, "WIN95", 0x00))), 0x8580);
}

TEST_F(ModemNbns, ServerNameCannotBeReleased)
{
    EXPECT_EQ(flags_of(ask(nbns, make_with_address(OP_RELEASE, VSERVER_SERVER_NAME, 0x20, 0x6000, guest_ip))), 0xB406);
    EXPECT_EQ(flags_of(ask(nbns, make_request(OP_QUERY, VSERVER_SERVER_NAME, 0x20))), 0x8580);
}

TEST_F(ModemNbns, DomainGroupKeepsItsAddressList)
{
    ask(nbns, make_with_address(OP_REGISTER, "DOMAIN", 0x1C, 0xE000, guest_ip));
    ask(nbns, make_with_address(OP_REGISTER, "DOMAIN", 0x1C, 0xE000, guest_ip + 1));

    auto reply = ask(nbns, make_request(OP_QUERY, "DOMAIN", 0x1C));
    ASSERT_EQ(reply.size(), 12u + 34u + 10u + 12u);
    EXPECT_EQ(address_at(reply, first_address), guest_ip);
    EXPECT_EQ(address_at(reply, first_address + 6), guest_ip + 1);
}

TEST_F(ModemNbns, NamesExpireAfterTheirTtl)
{
    ask(nbns, make_with_address(OP_REGISTER, "WIN95", 0x00, 0x6000, guest_ip));

    const uint32_t ttl_ms = 518400u * 1000u;
    for (uint32_t ms = 1; ms < ttl_ms; ms++)
        nbns_tick(nbns);
    EXPECT_EQ(flags_of(ask(nbns, make_request(OP_QUERY, "WIN95", 0x00))), 0x8580);

    nbns_tick(nbns);
    EXPECT_EQ(flags_of(ask(nbns, make_request(OP_QUERY, "WIN95", 0x00))), 0x8583);
    EXPECT_EQ(flags_of(ask(nbns, make_request(OP_QUERY, VSERVER_SERVER_NAME, 0x20))), 0x8580);
}

TEST_F(ModemNbns, NodeStatusListsTheServersNames)
{
    auto reply = ask(nbns, make_request(OP_QUERY, "*", 0x00, 0x0021));
    const size_t rdata = 12 + 34 + 10;
    ASSERT_EQ(reply.size(), rdata + 1 + 5 * 18 + 46);
    EXPECT_EQ(flags_of(reply), 0x8400);
    EXPECT_EQ(reply[12 + 34 + 1], 0x21);
    EXPECT_EQ(reply[rdata], 5);
    EXPECT_EQ(std::string(reinterpret_cast<const char *>(&reply[rdata + 1]), 9), VSERVER_SERVER_NAME);
}

TEST_F(ModemNbns, IgnoresMalformedMessages)
{
    auto valid = make_request(OP_QUERY, "WIN95", 0x00);

    EXPECT_TRUE(ask(nbns, { 0, 1, 2 }).empty());
    for (size_t length = 0; length < valid.size(); length++) {
        std::vector<uint8_t> shortened(valid.begin(), valid.begin() + length);
        EXPECT_TRUE(ask(nbns, shortened).empty()) << length;
    }
    auto response = valid;
    response[2] |= 0x80;
    EXPECT_TRUE(ask(nbns, response).empty());
    auto bad_name = valid;
    bad_name[13] = 'Z';
    EXPECT_TRUE(ask(nbns, bad_name).empty());
}

TEST_F(ModemNbns, RegistrationWithoutAnAddressIsAFormatError)
{
    auto reply = ask(nbns, make_request(OP_REGISTER, "WIN95", 0x00));
    ASSERT_FALSE(reply.empty());
    EXPECT_EQ(flags_of(reply), 0xAD81);
}

TEST_F(ModemNbns, SurvivesAFullTable)
{
    for (int i = 0; i < 200; i++)
        ask(nbns, make_with_address(OP_REGISTER, "NAME" + std::to_string(i), 0x00, 0x6000, guest_ip));

    EXPECT_EQ(flags_of(ask(nbns, make_with_address(OP_REGISTER, "LASTNAME", 0x00, 0x6000, guest_ip))), 0xAD85);
    EXPECT_EQ(flags_of(ask(nbns, make_request(OP_QUERY, VSERVER_SERVER_NAME, 0x20))), 0x8580);
}

static std::vector<uint8_t>
wrap_ipv4(const std::vector<uint8_t> &msg, uint32_t destination, uint16_t port, uint8_t protocol = 17)
{
    const int total = 20 + 8 + static_cast<int>(msg.size());
    std::vector<uint8_t> packet(static_cast<size_t>(total));
    packet[0] = 0x45;
    packet[2] = static_cast<uint8_t>(total >> 8);
    packet[3] = static_cast<uint8_t>(total);
    packet[8] = 128;
    packet[9] = protocol;
    for (int i = 0; i < 4; i++) {
        packet[12 + i] = static_cast<uint8_t>(guest_ip >> (24 - i * 8));
        packet[16 + i] = static_cast<uint8_t>(destination >> (24 - i * 8));
    }
    packet[20] = 0x04; /* source port 1234 */
    packet[21] = 0xD2;
    packet[22] = static_cast<uint8_t>(port >> 8);
    packet[23] = static_cast<uint8_t>(port);
    packet[24] = static_cast<uint8_t>((8 + msg.size()) >> 8);
    packet[25] = static_cast<uint8_t>(8 + msg.size());
    memcpy(packet.data() + 28, msg.data(), msg.size());
    return packet;
}

static uint32_t
sum_words(const uint8_t *data, size_t length)
{
    uint32_t sum = 0;
    for (size_t i = 0; i + 1 < length; i += 2)
        sum += (static_cast<uint32_t>(data[i]) << 8) | data[i + 1];
    if (length & 1)
        sum += static_cast<uint32_t>(data[length - 1]) << 8;
    return sum;
}

static uint16_t
fold(uint32_t sum)
{
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return static_cast<uint16_t>(sum);
}

TEST_F(ModemNbns, Ipv4WrapperBuildsAValidReply)
{
    auto    packet = wrap_ipv4(make_request(OP_QUERY, VSERVER_SERVER_NAME, 0x20), server_ip, 137);
    uint8_t reply[NBNS_MAX_REPLY];

    int length = nbns_process_ipv4(nbns, packet.data(), static_cast<int>(packet.size()), reply, sizeof(reply));
    ASSERT_GT(length, 28);

    EXPECT_EQ(reply[0], 0x45);
    EXPECT_EQ((reply[2] << 8) | reply[3], length);
    EXPECT_EQ(reply[9], 17);
    EXPECT_EQ(address_at(std::vector<uint8_t>(reply, reply + 20), 12), server_ip);
    EXPECT_EQ(address_at(std::vector<uint8_t>(reply, reply + 20), 16), guest_ip);
    EXPECT_EQ(fold(sum_words(reply, 20)), 0xFFFF);
    EXPECT_EQ((reply[20] << 8) | reply[21], 137);
    EXPECT_EQ((reply[22] << 8) | reply[23], 1234);

    const int udp_length = length - 20;
    uint32_t  pseudo     = sum_words(reply + 12, 8) + 17 + static_cast<uint32_t>(udp_length);
    EXPECT_EQ(fold(pseudo + sum_words(reply + 20, static_cast<size_t>(udp_length))), 0xFFFF);
}

TEST_F(ModemNbns, Ipv4WrapperLeavesOtherTrafficAlone)
{
    auto    msg = make_request(OP_QUERY, VSERVER_SERVER_NAME, 0x20);
    uint8_t reply[NBNS_MAX_REPLY];

    auto other_host = wrap_ipv4(msg, server_ip + 1, 137);
    EXPECT_EQ(nbns_process_ipv4(nbns, other_host.data(), static_cast<int>(other_host.size()), reply, sizeof(reply)), -1);
    auto broadcast = wrap_ipv4(msg, 0x0AFFFFFF, 137);
    EXPECT_EQ(nbns_process_ipv4(nbns, broadcast.data(), static_cast<int>(broadcast.size()), reply, sizeof(reply)), -1);
    auto other_port = wrap_ipv4(msg, server_ip, 138);
    EXPECT_EQ(nbns_process_ipv4(nbns, other_port.data(), static_cast<int>(other_port.size()), reply, sizeof(reply)), -1);
    auto tcp = wrap_ipv4(msg, server_ip, 137, 6);
    EXPECT_EQ(nbns_process_ipv4(nbns, tcp.data(), static_cast<int>(tcp.size()), reply, sizeof(reply)), -1);
}

TEST_F(ModemNbns, Ipv4WrapperConsumesUnusablePacketsForTheServer)
{
    uint8_t reply[NBNS_MAX_REPLY];
    auto    packet = wrap_ipv4({ 1, 2, 3 }, server_ip, 137);

    EXPECT_EQ(nbns_process_ipv4(nbns, packet.data(), static_cast<int>(packet.size()), reply, sizeof(reply)), 0);
}
