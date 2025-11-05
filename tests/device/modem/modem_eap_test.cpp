#include <cstddef>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <86box/modem/modem_ppp.h>
#include <86box/modem/modem_crypto.h>
#include <86box/modem/modem_eap.h>

static uint16_t sent_protocol;
static uint8_t  sent_packet[64];
static int      sent_length;
static int      state_advances;
static uint8_t  random_value;
static bool     random_available;

extern "C" {
void
ppp_send_frame(ppp_ctx_t *, uint16_t protocol, const uint8_t *data, int len)
{
    sent_protocol = protocol;
    sent_length   = len;
    std::memcpy(sent_packet, data, len);
}

void
ppp_advance_state(ppp_ctx_t *)
{
    state_advances++;
}

bool
ppp_random_bytes(uint8_t *buffer, uint8_t len)
{
    if (!random_available)
        return false;

    for (uint8_t i = 0; i < len; i++)
        buffer[i] = random_value++;
    return true;
}
}

static void
reset_test_state(ppp_ctx_t *ctx)
{
    std::memset(ctx, 0, sizeof(*ctx));
    ctx->state = PPP_STATE_AUTH;
    ctx->auth_type = PPP_AUTH_EAP;
    sent_protocol = 0;
    sent_length = 0;
    state_advances = 0;
    random_value = 1;
    random_available = true;
}

static void
send_identity_response(ppp_ctx_t *ctx, const char *identity)
{
    const size_t identity_len = std::strlen(identity);
    uint8_t      response[64] = { EAP_CODE_RESPONSE, 0, 0, 0, EAP_TYPE_IDENTITY };
    const size_t response_len = 5 + identity_len;

    response[2] = static_cast<uint8_t>(response_len >> 8);
    response[3] = static_cast<uint8_t>(response_len);
    std::memcpy(response + 5, identity, identity_len);
    ppp_eap_process(ctx, response, static_cast<int>(response_len));
}

TEST(ModemEap, IdentityThenMD5Success)
{
    ppp_ctx_t ctx;
    reset_test_state(&ctx);
    std::strcpy(ctx.username, "alice");
    std::strcpy(ctx.password, "secret");

    ppp_eap_start(&ctx);
    ASSERT_EQ(sent_protocol, PPP_PROTO_EAP);
    ASSERT_EQ(sent_length, 5);
    EXPECT_EQ(sent_packet[0], EAP_CODE_REQUEST);
    EXPECT_EQ(sent_packet[1], 0);
    EXPECT_EQ(sent_packet[4], EAP_TYPE_IDENTITY);

    send_identity_response(&ctx, "alice");
    ASSERT_EQ(sent_protocol, PPP_PROTO_EAP);
    ASSERT_EQ(sent_length, 22);
    ASSERT_EQ(sent_packet[0], EAP_CODE_REQUEST);
    ASSERT_EQ(sent_packet[1], 1);
    ASSERT_EQ(sent_packet[4], EAP_TYPE_MD5_CHALLENGE);
    ASSERT_EQ(sent_packet[5], 16);

    uint8_t response[22] = { EAP_CODE_RESPONSE, sent_packet[1], 0, 22,
                             EAP_TYPE_MD5_CHALLENGE, 16 };
    uint8_t digest[MD5_DIGEST_LENGTH];
    modem_md5_ctx_t md5;

    modem_md5_init(&md5);
    modem_md5_update(&md5, response + 1, 1);
    modem_md5_update(&md5, reinterpret_cast<const uint8_t *>(ctx.password), std::strlen(ctx.password));
    modem_md5_update(&md5, sent_packet + 6, 16);
    modem_md5_final(&md5, digest);
    std::memcpy(response + 6, digest, sizeof(digest));

    ppp_eap_process(&ctx, response, sizeof(response));
    EXPECT_EQ(sent_protocol, PPP_PROTO_EAP);
    EXPECT_EQ(sent_length, 4);
    EXPECT_EQ(sent_packet[0], EAP_CODE_SUCCESS);
    EXPECT_EQ(sent_packet[1], response[1]);
    EXPECT_TRUE(ctx.auth_complete);
    EXPECT_EQ(state_advances, 1);
}

TEST(ModemEap, RejectsTrailingDataInMd5Response)
{
    ppp_ctx_t ctx;
    reset_test_state(&ctx);
    std::strcpy(ctx.username, "alice");
    std::strcpy(ctx.password, "secret");

    ppp_eap_start(&ctx);
    send_identity_response(&ctx, "alice");

    uint8_t response[23] = { EAP_CODE_RESPONSE, ctx.eap_request_id, 0, 23,
                             EAP_TYPE_MD5_CHALLENGE, 16 };
    uint8_t digest[MD5_DIGEST_LENGTH];
    modem_md5_ctx_t md5;
    modem_md5_init(&md5);
    modem_md5_update(&md5, response + 1, 1);
    modem_md5_update(&md5, reinterpret_cast<const uint8_t *>(ctx.password),
                     std::strlen(ctx.password));
    modem_md5_update(&md5, ctx.eap_challenge, sizeof(ctx.eap_challenge));
    modem_md5_final(&md5, digest);
    std::memcpy(response + 6, digest, sizeof(digest));

    ppp_eap_process(&ctx, response, sizeof(response));

    EXPECT_EQ(sent_packet[0], EAP_CODE_FAILURE);
    EXPECT_FALSE(ctx.auth_complete);
    EXPECT_EQ(state_advances, 0);
}

TEST(ModemEap, IgnoresNullContext)
{
    const uint8_t response[] = { EAP_CODE_RESPONSE, 0, 0, 5, EAP_TYPE_IDENTITY };

    ppp_eap_process(nullptr, response, sizeof(response));
}

TEST(ModemEap, RejectsUnexpectedIdentity)
{
    ppp_ctx_t ctx;
    reset_test_state(&ctx);
    std::strcpy(ctx.username, "alice");

    ppp_eap_start(&ctx);
    send_identity_response(&ctx, "mallory");

    EXPECT_EQ(sent_packet[0], EAP_CODE_FAILURE);
    EXPECT_FALSE(ctx.auth_complete);
    EXPECT_EQ(state_advances, 0);
}

TEST(ModemEap, RejectsUnsupportedMethodNak)
{
    ppp_ctx_t ctx;
    reset_test_state(&ctx);
    std::strcpy(ctx.username, "alice");

    ppp_eap_start(&ctx);
    send_identity_response(&ctx, "alice");

    const uint8_t response[] = { EAP_CODE_RESPONSE, sent_packet[1], 0, 6,
                                 EAP_TYPE_NAK, EAP_TYPE_OTP };
    ppp_eap_process(&ctx, response, sizeof(response));

    EXPECT_EQ(sent_packet[0], EAP_CODE_FAILURE);
    EXPECT_FALSE(ctx.auth_complete);
    EXPECT_EQ(state_advances, 0);
}

TEST(ModemEap, FailsAuthenticationWhenRandomSourceFails)
{
    ppp_ctx_t ctx;
    reset_test_state(&ctx);
    std::strcpy(ctx.username, "alice");

    ppp_eap_start(&ctx);
    random_available = false;
    send_identity_response(&ctx, "alice");

    EXPECT_EQ(sent_packet[0], EAP_CODE_FAILURE);
    EXPECT_FALSE(ctx.auth_complete);
    EXPECT_EQ(state_advances, 0);
}