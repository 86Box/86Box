/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          SLIP authentication implementation - optional text-based
 *          login prompt before entering SLIP data mode. Emulates a
 *          classic ISP shell login for SLIP connections.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <86box/modem/modem_crypto.h>
#include <86box/modem/modem_slip_auth.h>
#include <86box/log.h>
#include <86box/modem/modem_debug.h>

#define SLIP_AUTH_USERNAME_PROMPT "\r\n86Box SLIP Server\r\nlogin: "
#define SLIP_AUTH_PASSWORD_PROMPT "\r\nPassword: "
#define SLIP_AUTH_RETRY_PROMPT    "\r\nLogin incorrect.\r\n\r\nlogin: "

#ifdef ENABLE_MODEM_LOG
extern uint8_t modem_do_log;

static void
slip_auth_log(void *priv, const char *fmt, ...)
{
    va_list ap;
    if (modem_do_log) {
        va_start(ap, fmt);
        log_out(priv, fmt, ap);
        va_end(ap);
    }
}
#else
#    define slip_auth_log(priv, fmt, ...)
#endif

static void
slip_auth_send_string(slip_auth_ctx_t *ctx, const char *str)
{
    ctx->serial_push(ctx->modem, (const uint8_t *) str, (int) strlen(str));
}

slip_auth_ctx_t *
slip_auth_init(void *modem, void *log,
               void (*serial_push)(void *, const uint8_t *, int),
               const char *username,
               const char *password)
{
    slip_auth_ctx_t *ctx = (slip_auth_ctx_t *) calloc(1, sizeof(slip_auth_ctx_t));
    if (!ctx)
        return NULL;

    ctx->modem       = modem;
    ctx->log         = log;
    ctx->serial_push = serial_push;
    ctx->state       = SLIP_AUTH_SEND_USERNAME_PROMPT;
    ctx->active      = false;

    if (username)
        strncpy(ctx->expected_user, username, SLIP_AUTH_BUF_SIZE - 1);
    if (password)
        strncpy(ctx->expected_pass, password, SLIP_AUTH_BUF_SIZE - 1);

    return ctx;
}

void
slip_auth_close(slip_auth_ctx_t *ctx)
{
    if (ctx) {
        memset(ctx->expected_pass, 0, sizeof(ctx->expected_pass));
        memset(ctx->password_buf, 0, sizeof(ctx->password_buf));
        free(ctx);
    }
}

static void
slip_auth_reset_input(slip_auth_ctx_t *ctx)
{
    ctx->username_pos  = 0;
    ctx->password_pos  = 0;
    ctx->invalid_input = false;
    memset(ctx->username_buf, 0, sizeof(ctx->username_buf));
    memset(ctx->password_buf, 0, sizeof(ctx->password_buf));
}

void
slip_auth_start(slip_auth_ctx_t *ctx)
{
    ctx->state        = SLIP_AUTH_SEND_USERNAME_PROMPT;
    ctx->active       = true;
    ctx->attempts     = 0;
    ctx->idle_ms      = 0;
    slip_auth_reset_input(ctx);

    slip_auth_send_string(ctx, SLIP_AUTH_USERNAME_PROMPT);
    ctx->state = SLIP_AUTH_RECV_USERNAME;

    slip_auth_log(ctx->log, "SLIP Auth: Started, waiting for username\n");
    MODEM_DEBUG_LOG(ctx->log, "SLIP Auth: prompt sent, state=%d\n", ctx->state);
}

bool
slip_auth_tick(slip_auth_ctx_t *ctx)
{
    if (!ctx || !ctx->active)
        return false;
    if (++ctx->idle_ms < SLIP_AUTH_IDLE_MS)
        return false;

    slip_auth_log(ctx->log, "SLIP Auth: Timed out waiting for input\n");
    slip_auth_send_string(ctx, "\r\nLogin timed out after 60 seconds.\r\n");
    ctx->state  = SLIP_AUTH_DONE_FAIL;
    ctx->active = false;
    return true;
}

bool
slip_auth_rx_byte(slip_auth_ctx_t *ctx, uint8_t byte)
{
    if (ctx->ignore_lf) {
        ctx->ignore_lf = false;
        if (byte == '\n')
            return false;
    }

    ctx->idle_ms = 0;

    switch (ctx->state) {
        case SLIP_AUTH_RECV_USERNAME:
            if (byte == '\r' || byte == '\n') {
                ctx->ignore_lf = byte == '\r';
                /* Zero the tail so characters left behind by backspaces cannot
                   take part in the comparison. */
                memset(ctx->username_buf + ctx->username_pos, 0,
                       sizeof(ctx->username_buf) - (size_t) ctx->username_pos);
                if (ctx->username_pos == 0) {
                    /* DUN scripts commonly send CR first to prompt a login banner. */
                    slip_auth_send_string(ctx, SLIP_AUTH_USERNAME_PROMPT);
                    break;
                }
                slip_auth_log(ctx->log, "SLIP Auth: Got username '%s'\n", ctx->username_buf);
                MODEM_DEBUG_LOG(ctx->log, "SLIP Auth: username field length=%u\n",
                                (unsigned) ctx->username_pos);

                slip_auth_send_string(ctx, SLIP_AUTH_PASSWORD_PROMPT);
                ctx->state = SLIP_AUTH_RECV_PASSWORD;
            } else if (byte == '\b' || byte == 0x7F) {
                if (ctx->username_pos > 0) {
                    ctx->username_pos--;
                    /* Echo backspace */
                    slip_auth_send_string(ctx, "\b \b");
                }
            } else if (byte == '\0') {
                ctx->invalid_input = true;
            } else if (ctx->username_pos < SLIP_AUTH_BUF_SIZE - 1) {
                ctx->username_buf[ctx->username_pos++] = (char) byte;
                /* Echo character */
                ctx->serial_push(ctx->modem, &byte, 1);
            } else {
                ctx->invalid_input = true;
            }
            break;

        case SLIP_AUTH_RECV_PASSWORD:
            if (byte == '\r' || byte == '\n') {
                ctx->ignore_lf = byte == '\r';
                memset(ctx->password_buf + ctx->password_pos, 0,
                       sizeof(ctx->password_buf) - (size_t) ctx->password_pos);
                slip_auth_log(ctx->log, "SLIP Auth: Got password, verifying...\n");
                MODEM_DEBUG_LOG(ctx->log, "SLIP Auth: password field length=%u\n",
                                (unsigned) ctx->password_pos);

                /* Verify credentials. Both fields are compared over their whole
                   zero-padded buffers so timing does not reveal which was wrong
                   or how long the correct one is. */
                bool ok = false;
                if (!ctx->invalid_input && ctx->expected_user[0] == '\0'
                    && ctx->expected_pass[0] == '\0') {
                    ok = true; /* No credentials configured */
                } else if (!ctx->invalid_input) {
                    bool user_ok = modem_constant_time_equal(
                        (const uint8_t *) ctx->username_buf,
                        (const uint8_t *) ctx->expected_user, SLIP_AUTH_BUF_SIZE);
                    bool pass_ok = modem_constant_time_equal(
                        (const uint8_t *) ctx->password_buf,
                        (const uint8_t *) ctx->expected_pass, SLIP_AUTH_BUF_SIZE);

                    ok = user_ok && pass_ok;
                }

                /* Securely clear password buffer */
                memset(ctx->password_buf, 0, sizeof(ctx->password_buf));
                MODEM_DEBUG_LOG(ctx->log, "SLIP Auth: credential check %s\n",
                                ok ? "accepted" : "rejected");

                if (ok) {
                    slip_auth_send_string(ctx, "\r\nSLIP session starting...\r\n");
                    ctx->state  = SLIP_AUTH_DONE_OK;
                    ctx->active = false;
                    slip_auth_log(ctx->log, "SLIP Auth: Success\n");
                    return true; /* Auth phase complete */
                }

                if (++ctx->attempts < SLIP_AUTH_MAX_ATTEMPTS) {
                    /* Classic login(1) behaviour: show the failure and ask again. */
                    slip_auth_log(ctx->log, "SLIP Auth: Failed (attempt %u of %u)\n",
                                  ctx->attempts, (unsigned) SLIP_AUTH_MAX_ATTEMPTS);
                    slip_auth_reset_input(ctx);
                    slip_auth_send_string(ctx, SLIP_AUTH_RETRY_PROMPT);
                    ctx->state = SLIP_AUTH_RECV_USERNAME;
                    return false;
                }

                slip_auth_send_string(ctx, "\r\nLogin incorrect.\r\n");
                ctx->state  = SLIP_AUTH_DONE_FAIL;
                ctx->active = false;
                slip_auth_log(ctx->log, "SLIP Auth: Failed\n");
                return true; /* Auth phase complete */
            } else if (byte == '\b' || byte == 0x7F) {
                if (ctx->password_pos > 0)
                    ctx->password_pos--;
                /* Don't echo password characters */
            } else if (byte == '\0') {
                ctx->invalid_input = true;
            } else if (ctx->password_pos < SLIP_AUTH_BUF_SIZE - 1) {
                /* Like login(1), nothing is echoed while the password is typed. */
                ctx->password_buf[ctx->password_pos++] = (char) byte;
            } else {
                ctx->invalid_input = true;
            }
            break;

        default:
            return true; /* Already done */
    }

    return false; /* Not done yet */
}
