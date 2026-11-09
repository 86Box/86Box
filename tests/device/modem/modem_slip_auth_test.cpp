#include <cstdint>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include <86box/modem/modem_slip_auth.h>

static void
capture_serial(void *priv, const uint8_t *data, int len)
{
    auto *output = static_cast<std::vector<uint8_t> *>(priv);
    output->insert(output->end(), data, data + len);
}

static bool
send_text(slip_auth_ctx_t *ctx, const std::string &text)
{
    bool done = false;
    for (uint8_t byte : text)
        done = slip_auth_rx_byte(ctx, byte) || done;
    return done;
}

TEST(ModemSlipAuth, AcceptsConfiguredCredentials)
{
    std::vector<uint8_t> output;
    slip_auth_ctx_t *ctx = slip_auth_init(&output, nullptr, capture_serial, "guest", "secret");
    ASSERT_NE(ctx, nullptr);
    slip_auth_start(ctx);

    EXPECT_FALSE(send_text(ctx, "guest\r"));
    EXPECT_TRUE(send_text(ctx, "secret\r"));
    EXPECT_EQ(ctx->state, SLIP_AUTH_DONE_OK);
    EXPECT_FALSE(ctx->active);

    slip_auth_close(ctx);
}

TEST(ModemSlipAuth, AcceptsCarriageReturnLineFeeds)
{
    std::vector<uint8_t> output;
    slip_auth_ctx_t *ctx = slip_auth_init(&output, nullptr, capture_serial, "guest", "secret");
    ASSERT_NE(ctx, nullptr);
    slip_auth_start(ctx);

    send_text(ctx, "guest\r\nsecret\r\n");

    EXPECT_EQ(ctx->state, SLIP_AUTH_DONE_OK);
    EXPECT_FALSE(ctx->active);
    EXPECT_FALSE(ctx->ignore_lf);
    slip_auth_close(ctx);
}

TEST(ModemSlipAuth, RejectsIncorrectCredentials)
{
    std::vector<uint8_t> output;
    slip_auth_ctx_t *ctx = slip_auth_init(&output, nullptr, capture_serial, "guest", "secret");
    ASSERT_NE(ctx, nullptr);
    slip_auth_start(ctx);

    for (unsigned attempt = 1; attempt <= SLIP_AUTH_MAX_ATTEMPTS; attempt++) {
        EXPECT_FALSE(send_text(ctx, "guest\r"));
        EXPECT_EQ(send_text(ctx, "wrong\r"), attempt == SLIP_AUTH_MAX_ATTEMPTS);
        EXPECT_EQ(ctx->attempts, attempt);
        if (attempt < SLIP_AUTH_MAX_ATTEMPTS) {
            EXPECT_EQ(ctx->state, SLIP_AUTH_RECV_USERNAME);
            EXPECT_TRUE(ctx->active);
        }
    }
    EXPECT_EQ(ctx->state, SLIP_AUTH_DONE_FAIL);
    EXPECT_FALSE(ctx->active);

    slip_auth_close(ctx);
}

TEST(ModemSlipAuth, AllowsEmptyCredentialsWhenUnconfigured)
{
    std::vector<uint8_t> output;
    slip_auth_ctx_t *ctx = slip_auth_init(&output, nullptr, capture_serial, "", "");
    ASSERT_NE(ctx, nullptr);
    slip_auth_start(ctx);

    EXPECT_FALSE(send_text(ctx, "guest\r"));
    EXPECT_TRUE(send_text(ctx, "\r"));
    EXPECT_EQ(ctx->state, SLIP_AUTH_DONE_OK);

    slip_auth_close(ctx);
}

TEST(ModemSlipAuth, RejectsEmbeddedNullBytesInCredentials)
{
    std::vector<uint8_t> output;
    slip_auth_ctx_t *username_ctx = slip_auth_init(&output, nullptr, capture_serial,
                                                   "guest", "secret");
    ASSERT_NE(username_ctx, nullptr);
    slip_auth_start(username_ctx);
    std::string username = "guest";
    username.push_back('\0');
    username += "attacker\r";
    send_text(username_ctx, username);
    EXPECT_FALSE(send_text(username_ctx, "secret\r"));
    EXPECT_EQ(username_ctx->attempts, 1u);
    EXPECT_EQ(username_ctx->state, SLIP_AUTH_RECV_USERNAME);
    EXPECT_FALSE(send_text(username_ctx, "guest\r"));
    EXPECT_TRUE(send_text(username_ctx, "secret\r"));
    EXPECT_EQ(username_ctx->state, SLIP_AUTH_DONE_OK);
    slip_auth_close(username_ctx);

    slip_auth_ctx_t *password_ctx = slip_auth_init(&output, nullptr, capture_serial,
                                                   "guest", "secret");
    ASSERT_NE(password_ctx, nullptr);
    slip_auth_start(password_ctx);
    send_text(password_ctx, "guest\r");
    std::string password = "secret";
    password.push_back('\0');
    password += "attacker\r";
    EXPECT_FALSE(send_text(password_ctx, password));
    EXPECT_EQ(password_ctx->attempts, 1u);
    EXPECT_EQ(password_ctx->state, SLIP_AUTH_RECV_USERNAME);
    EXPECT_FALSE(send_text(password_ctx, "guest\r"));
    EXPECT_TRUE(send_text(password_ctx, "secret\r"));
    EXPECT_EQ(password_ctx->state, SLIP_AUTH_DONE_OK);
    slip_auth_close(password_ctx);
}

TEST(ModemSlipAuth, RejectsOverlongCredentialsInsteadOfTruncating)
{
    std::vector<uint8_t> output;
    const std::string max_username(SLIP_AUTH_BUF_SIZE - 1, 'u');
    slip_auth_ctx_t *username_ctx = slip_auth_init(&output, nullptr, capture_serial,
                                                   max_username.c_str(), "secret");
    ASSERT_NE(username_ctx, nullptr);
    slip_auth_start(username_ctx);
    send_text(username_ctx, max_username + "x\r");
    EXPECT_FALSE(send_text(username_ctx, "secret\r"));
    EXPECT_EQ(username_ctx->attempts, 1u);
    EXPECT_EQ(username_ctx->state, SLIP_AUTH_RECV_USERNAME);
    EXPECT_FALSE(send_text(username_ctx, max_username + "\r"));
    EXPECT_TRUE(send_text(username_ctx, "secret\r"));
    EXPECT_EQ(username_ctx->state, SLIP_AUTH_DONE_OK);
    slip_auth_close(username_ctx);

    slip_auth_ctx_t *password_ctx = slip_auth_init(&output, nullptr, capture_serial,
                                                   "guest", max_username.c_str());
    ASSERT_NE(password_ctx, nullptr);
    slip_auth_start(password_ctx);
    send_text(password_ctx, "guest\r");
    EXPECT_FALSE(send_text(password_ctx, max_username + "x\r"));
    EXPECT_EQ(password_ctx->attempts, 1u);
    EXPECT_EQ(password_ctx->state, SLIP_AUTH_RECV_USERNAME);
    EXPECT_FALSE(send_text(password_ctx, "guest\r"));
    EXPECT_TRUE(send_text(password_ctx, max_username + "\r"));
    EXPECT_EQ(password_ctx->state, SLIP_AUTH_DONE_OK);
    slip_auth_close(password_ctx);
}