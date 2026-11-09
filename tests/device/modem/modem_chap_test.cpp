#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include <86box/modem/modem_ppp.h>
#include <86box/modem/modem_chap.h>
#include <86box/modem/modem_crypto.h>

static std::vector<uint8_t> sent_packet;
static int state_advances;
static bool random_available;

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

bool
ppp_random_bytes(uint8_t *buffer, uint8_t len)
{
    if (!random_available)
        return false;

    std::memset(buffer, 0x5A, len);
    return true;
}
}

static std::vector<uint8_t>
make_chap_response(ppp_ctx_t *ctx, uint8_t id, const std::string &username)
{
    uint8_t digest[MD5_DIGEST_LENGTH];
    modem_md5_ctx_t md5;
    modem_md5_init(&md5);
    modem_md5_update(&md5, &id, 1);
    modem_md5_update(&md5, reinterpret_cast<const uint8_t *>(ctx->password), std::strlen(ctx->password));
    modem_md5_update(&md5, ctx->chap_challenge, ctx->chap_challenge_len);
    modem_md5_final(&md5, digest);

    std::vector<uint8_t> packet = { CHAP_CODE_RESPONSE, id, 0, 0, MD5_DIGEST_LENGTH };
    packet.insert(packet.end(), digest, digest + sizeof(digest));
    packet.insert(packet.end(), username.begin(), username.end());
    packet[2] = static_cast<uint8_t>(packet.size() >> 8);
    packet[3] = static_cast<uint8_t>(packet.size());
    return packet;
}

TEST(ModemChap, IgnoresResponseForPreviousChallenge)
{
    ppp_ctx_t ctx{};
    ctx.auth_type = PPP_AUTH_CHAP_MD5;
    ctx.auth_id = 7;
    ctx.chap_challenge_len = 16;
    std::strcpy(ctx.username, "alice");
    std::strcpy(ctx.password, "secret");
    random_available = true;
    sent_packet.clear();

    std::vector<uint8_t> response = make_chap_response(&ctx, 6, "alice");
    ppp_chap_process(&ctx, response.data(), static_cast<int>(response.size()));

    EXPECT_TRUE(sent_packet.empty());
    EXPECT_FALSE(ctx.auth_complete);
}

TEST(ModemChap, AcceptsCurrentMd5ChallengeResponse)
{
    ppp_ctx_t ctx{};
    ctx.auth_type = PPP_AUTH_CHAP_MD5;
    ctx.auth_id = 7;
    ctx.chap_challenge_len = 16;
    std::strcpy(ctx.username, "alice");
    std::strcpy(ctx.password, "secret");
    random_available = true;
    state_advances = 0;

    std::vector<uint8_t> response = make_chap_response(&ctx, 7, "alice");
    ppp_chap_process(&ctx, response.data(), static_cast<int>(response.size()));

    EXPECT_EQ(sent_packet[0], CHAP_CODE_SUCCESS);
    EXPECT_TRUE(ctx.auth_complete);
    EXPECT_EQ(state_advances, 1);
}

TEST(ModemChap, RejectsEmbeddedNullSuffixInPeerName)
{
    ppp_ctx_t ctx{};
    ctx.auth_type = PPP_AUTH_CHAP_MD5;
    ctx.auth_id = 7;
    ctx.chap_challenge_len = 16;
    std::strcpy(ctx.username, "alice");
    std::strcpy(ctx.password, "secret");
    state_advances = 0;
    const std::string peer_name("alice\0attacker", 14);
    const std::vector<uint8_t> response = make_chap_response(&ctx, 7, peer_name);

    ppp_chap_process(&ctx, response.data(), static_cast<int>(response.size()));

    EXPECT_EQ(sent_packet[0], CHAP_CODE_FAILURE);
    EXPECT_FALSE(ctx.auth_complete);
    EXPECT_EQ(state_advances, 0);
}

TEST(ModemChap, AcceptsRfc2433MschapAndDerivesMppeKeys)
{
    const std::array<uint8_t, 8> challenge = {
        0x10, 0x2D, 0xB5, 0xDF, 0x08, 0x5D, 0x30, 0x41
    };
    const std::array<uint8_t, 24> nt_response = {
        0x4E, 0x9D, 0x3C, 0x8F, 0x9C, 0xFD, 0x38, 0x5D,
        0x5B, 0xF4, 0xD3, 0x24, 0x67, 0x91, 0x95, 0x6C,
        0xA4, 0xC3, 0x51, 0xAB, 0x40, 0x9A, 0x3D, 0x61
    };
    ppp_ctx_t ctx{};
    const uint8_t id = 7;
    std::vector<uint8_t> packet = { CHAP_CODE_RESPONSE, id, 0, 0, 49 };
    packet.resize(5 + 49, 0);
    std::memcpy(packet.data() + 5 + 24, nt_response.data(), nt_response.size());
    packet[5 + 48] = 1;
    packet.insert(packet.end(), { 'U', 's', 'e', 'r' });
    packet[2] = static_cast<uint8_t>(packet.size() >> 8);
    packet[3] = static_cast<uint8_t>(packet.size());
    ctx.auth_type = PPP_AUTH_MSCHAP;
    ctx.auth_id = id;
    ctx.chap_challenge_len = challenge.size();
    std::memcpy(ctx.chap_challenge, challenge.data(), challenge.size());
    std::strcpy(ctx.password, "MyPw");
    state_advances = 0;

    ppp_chap_process(&ctx, packet.data(), static_cast<int>(packet.size()));

    EXPECT_EQ(sent_packet[0], CHAP_CODE_SUCCESS);
    EXPECT_TRUE(ctx.auth_complete);
    EXPECT_TRUE(ctx.mppe_keys_ready);
    EXPECT_EQ(ctx.mppe_tx.key_bits, 128);
    EXPECT_EQ(std::memcmp(ctx.mppe_tx.start_key, ctx.mppe_rx.start_key,
                          PPP_MPPE_KEY_LENGTH), 0);
    EXPECT_EQ(state_advances, 1);
}

TEST(ModemChap, StripsDomainForMschapV2ChallengeHash)
{
    static const uint8_t authenticator_challenge[16] = {
        0x5B, 0x5D, 0x7C, 0x7D, 0x7B, 0x3F, 0x2F, 0x3E,
        0x3C, 0x2C, 0x60, 0x21, 0x32, 0x26, 0x26, 0x28
    };
    static const uint8_t response_value[49] = {
        0x21, 0x40, 0x23, 0x24, 0x25, 0x5E, 0x26, 0x2A,
        0x28, 0x29, 0x5F, 0x2B, 0x3A, 0x33, 0x7C, 0x7E,
        0, 0, 0, 0, 0, 0, 0, 0,
        0x82, 0x30, 0x9E, 0xCD, 0x8D, 0x70, 0x8B, 0x5E,
        0xA0, 0x8F, 0xAA, 0x39, 0x81, 0xCD, 0x83, 0x54,
        0x42, 0x33, 0x11, 0x4A, 0x3D, 0x85, 0xD6, 0xDF,
        0
    };
    ppp_ctx_t ctx{};
    ctx.auth_type = PPP_AUTH_MSCHAPV2;
    ctx.auth_id = 4;
    ctx.chap_challenge_len = 16;
    std::memcpy(ctx.chap_challenge, authenticator_challenge, sizeof(authenticator_challenge));
    std::strcpy(ctx.password, "clientPass");
    random_available = true;
    const std::string username = "DOMAIN\\User";
    std::vector<uint8_t> packet = { CHAP_CODE_RESPONSE, 4, 0, 0, sizeof(response_value) };
    packet.insert(packet.end(), response_value, response_value + sizeof(response_value));
    packet.insert(packet.end(), username.begin(), username.end());
    packet[2] = static_cast<uint8_t>(packet.size() >> 8);
    packet[3] = static_cast<uint8_t>(packet.size());

    ppp_chap_process(&ctx, packet.data(), static_cast<int>(packet.size()));

    ASSERT_EQ(sent_packet[0], CHAP_CODE_SUCCESS);
    EXPECT_EQ(std::string(sent_packet.begin() + 4, sent_packet.end()),
              "S=407A5589115FD0D6209F510FE9C04566932CDA56");
}

TEST(ModemChap, StopsWhenRandomSourceFails)
{
    ppp_ctx_t ctx{};
    ctx.state = PPP_STATE_AUTH;
    ctx.auth_type = PPP_AUTH_CHAP_MD5;
    sent_packet.clear();
    random_available = false;

    ppp_chap_send_challenge(&ctx);

    EXPECT_EQ(ctx.state, PPP_STATE_DEAD);
    EXPECT_TRUE(sent_packet.empty());
}