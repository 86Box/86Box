#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include <86box/modem/modem_ppp.h>
#include <86box/modem/modem_pap.h>

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
}

static void
send_auth_request(ppp_ctx_t *ctx, const std::string &username, const std::string &password)
{
    std::vector<uint8_t> packet = { PAP_CODE_AUTHENTICATE_REQUEST, 9, 0, 0,
                                    static_cast<uint8_t>(username.size()) };
    packet.insert(packet.end(), username.begin(), username.end());
    packet.push_back(static_cast<uint8_t>(password.size()));
    packet.insert(packet.end(), password.begin(), password.end());
    packet[2] = static_cast<uint8_t>(packet.size() >> 8);
    packet[3] = static_cast<uint8_t>(packet.size());
    ppp_pap_process(ctx, packet.data(), static_cast<int>(packet.size()));
}

TEST(ModemPap, AcceptsExactCredentials)
{
    ppp_ctx_t ctx{};
    std::strcpy(ctx.username, "alice");
    std::strcpy(ctx.password, "secret");
    state_advances = 0;

    send_auth_request(&ctx, "alice", "secret");

    EXPECT_EQ(sent_packet[0], PAP_CODE_AUTHENTICATE_ACK);
    EXPECT_TRUE(ctx.auth_complete);
    EXPECT_EQ(state_advances, 1);
}

TEST(ModemPap, RepeatsSuccessfulReplyForDuplicateRequestAfterAuthentication)
{
    ppp_ctx_t ctx{};
    std::strcpy(ctx.username, "alice");
    std::strcpy(ctx.password, "secret");
    state_advances = 0;

    send_auth_request(&ctx, "alice", "secret");
    ASSERT_EQ(sent_packet[0], PAP_CODE_AUTHENTICATE_ACK);

    sent_packet.clear();
    send_auth_request(&ctx, "alice", "secret");

    ASSERT_EQ(sent_packet[0], PAP_CODE_AUTHENTICATE_ACK);
    EXPECT_EQ(sent_packet[1], 9);
    EXPECT_EQ(state_advances, 1);
}

TEST(ModemPap, DoesNotAcknowledgeDifferentRequestWithReusedIdentifier)
{
    ppp_ctx_t ctx{};
    std::strcpy(ctx.username, "alice");
    std::strcpy(ctx.password, "secret");
    state_advances = 0;

    send_auth_request(&ctx, "alice", "secret");
    ASSERT_EQ(sent_packet[0], PAP_CODE_AUTHENTICATE_ACK);

    sent_packet.clear();
    send_auth_request(&ctx, "alice", "wrong");

    EXPECT_TRUE(sent_packet.empty());
    EXPECT_EQ(state_advances, 1);
}

TEST(ModemPap, RejectsIncorrectPassword)
{
    ppp_ctx_t ctx{};
    std::strcpy(ctx.username, "alice");
    std::strcpy(ctx.password, "secret");
    state_advances = 0;
    sent_packet.clear();

    send_auth_request(&ctx, "alice", "wrong");

    EXPECT_EQ(sent_packet[0], PAP_CODE_AUTHENTICATE_NAK);
    EXPECT_FALSE(ctx.auth_complete);
    EXPECT_EQ(state_advances, 0);
}

TEST(ModemPap, RejectsPacketShorterThanDeclaredLength)
{
    ppp_ctx_t ctx{};
    const std::vector<uint8_t> malformed = { PAP_CODE_AUTHENTICATE_REQUEST, 9, 0, 9,
                                             1, 'a', 3, 'x' };
    state_advances = 0;
    sent_packet.clear();

    ppp_pap_process(&ctx, malformed.data(), static_cast<int>(malformed.size()));

    EXPECT_TRUE(sent_packet.empty());
    EXPECT_FALSE(ctx.auth_complete);
    EXPECT_EQ(state_advances, 0);
}

TEST(ModemPap, RejectsCredentialsThatWouldHaveBeenTruncated)
{
    ppp_ctx_t ctx{};
    std::memset(ctx.username, 'a', sizeof(ctx.username) - 1);
    ctx.username[sizeof(ctx.username) - 1] = '\0';
    std::memset(ctx.password, 'b', sizeof(ctx.password) - 1);
    ctx.password[sizeof(ctx.password) - 1] = '\0';
    std::string username(ctx.username);
    std::string password(ctx.password);
    username.push_back('x');
    password.push_back('y');
    state_advances = 0;

    send_auth_request(&ctx, username, password);

    EXPECT_EQ(sent_packet[0], PAP_CODE_AUTHENTICATE_NAK);
    EXPECT_FALSE(ctx.auth_complete);
    EXPECT_EQ(state_advances, 0);
}