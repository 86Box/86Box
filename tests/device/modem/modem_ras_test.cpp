#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <86box/modem/modem_crypto.h>
#include <86box/modem/modem_ras.h>

struct ras_capture_t {
    int                    frames = 0;
    uint8_t                address = 0;
    std::vector<uint8_t>   payload;
    std::vector<modem_ras_event_t> events;
    uint16_t               event_length = 0;
    uint16_t               event_actual = 0;
    uint16_t               event_expected = 0;
};

struct ras_message_capture_t {
    std::vector<std::vector<uint8_t>> messages;
};

static void
capture_ras_frame(void *priv, uint8_t address, const uint8_t *payload, size_t length)
{
    auto *capture = static_cast<ras_capture_t *>(priv);
    capture->frames++;
    capture->address = address;
    capture->payload.assign(payload, payload + length);
}

static void
capture_ras_event(void *priv, modem_ras_event_t event, uint8_t, uint16_t length,
                  uint16_t actual, uint16_t expected)
{
    auto *capture = static_cast<ras_capture_t *>(priv);
    capture->events.push_back(event);
    capture->event_length = length;
    capture->event_actual = actual;
    capture->event_expected = expected;
}

static void
capture_serial_bytes(void *priv, const uint8_t *data, int length)
{
    auto *capture = static_cast<std::vector<uint8_t> *>(priv);
    capture->assign(data, data + length);
}

static bool
capture_ras_message(void *priv, const uint8_t *message, size_t length)
{
    auto *capture = static_cast<ras_message_capture_t *>(priv);
    capture->messages.emplace_back(message, message + length);
    return true;
}

static bool
fill_ras_challenge(uint8_t *buffer, uint8_t length)
{
    for (uint8_t i = 0; i < length; i++)
        buffer[i] = i;
    return true;
}

static bool
fail_ras_challenge(uint8_t *, uint8_t)
{
    return false;
}

static void
write_le32(std::vector<uint8_t> &message, size_t offset, uint32_t value)
{
    message[offset] = static_cast<uint8_t>(value);
    message[offset + 1] = static_cast<uint8_t>(value >> 8);
    message[offset + 2] = static_cast<uint8_t>(value >> 16);
    message[offset + 3] = static_cast<uint8_t>(value >> 24);
}

static void
write_projected_name(std::vector<uint8_t> &message, size_t index, const char *name,
                     uint8_t suffix, uint16_t flags)
{
    const size_t offset = 37 + index * 18;
    const size_t name_length = std::min(std::strlen(name), size_t(15));

    std::fill(message.begin() + static_cast<std::ptrdiff_t>(offset),
              message.begin() + static_cast<std::ptrdiff_t>(offset + 15), ' ');
    std::memcpy(message.data() + offset, name, name_length);
    message[offset + 15] = suffix;
    message[offset + 16] = static_cast<uint8_t>(flags);
    message[offset + 17] = static_cast<uint8_t>(flags >> 8);
}

static std::vector<uint8_t>
make_ras_configuration_request(bool request_ip = false)
{
    std::vector<uint8_t> message(333, 0);

    message[0] = MODEM_RAS_FRAME_CONFIGURATION_REQUEST;
    write_le32(message, 1, MODEM_RAS_PROTOCOL_VERSION_20);
    write_le32(message, 19, 1514);
    write_le32(message, 23, 1514);
    write_le32(message, 27, 115200);
    write_le32(message, 31, 1);
    message[35] = 5;
    write_projected_name(message, 0, "86BOX95", 0x00, 1);
    write_projected_name(message, 1, "WORKGROUP", 0x00, 2);
    write_projected_name(message, 2, "86BOX95", 0x03, 1);
    write_projected_name(message, 3, "86BOX95", 0x20, 1);
    write_projected_name(message, 4, "WORKGROUP", 0x1E, 2);
    if (request_ip)
        write_le32(message, 325, 1);
    return message;
}

static std::vector<uint8_t>
make_ras_response_20(const char *username, const char *password,
                     bool include_username_padding = false, bool use_nt_response = false)
{
    constexpr size_t username_offset = 1;
    constexpr size_t username_length = 257;
    constexpr size_t lm_response_offset = 274;
    constexpr size_t nt_response_offset = 298;
    constexpr size_t use_nt_response_offset = 322;
    const uint8_t challenge[MODEM_RAS_CHALLENGE_LENGTH] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    const uint8_t magic[8] = { 'K', 'G', 'S', '!', '@', '#', '$', '%' };
    uint8_t upper_password[14] = {};
    uint8_t password_hash[16];
    uint8_t padded_hash[21] = {};
    std::vector<uint8_t> message(333, 0);

    message[0] = MODEM_RAS_FRAME_RESPONSE_20;
    std::memcpy(message.data() + username_offset, username,
                std::min(std::strlen(username), username_length - 1));
    if (include_username_padding) {
        const size_t terminator = username_offset + std::strlen(username);
        message[terminator + 1] = 0x41;
        message[terminator + 2] = 0x42;
    }

    if (use_nt_response) {
        uint8_t unicode_password[128] = {};
        const size_t password_length = std::min(std::strlen(password), sizeof(unicode_password) / 2);
        for (size_t i = 0; i < password_length; i++) {
            unicode_password[i * 2] = static_cast<uint8_t>(password[i]);
            unicode_password[i * 2 + 1] = 0;
        }
        modem_md4(unicode_password, password_length * 2, password_hash);
        message[use_nt_response_offset] = 1;
        std::memcpy(padded_hash, password_hash, sizeof(password_hash));
        for (size_t i = 0; i < 3; i++)
            modem_des_encrypt_block(padded_hash + i * 7, challenge,
                                    message.data() + nt_response_offset + i * 8);
    } else {
        const size_t password_length = std::min(std::strlen(password), sizeof(upper_password));
        for (size_t i = 0; i < password_length; i++) {
            uint8_t value = static_cast<uint8_t>(password[i]);
            if (value >= 'a' && value <= 'z')
                value = static_cast<uint8_t>(value - 'a' + 'A');
            upper_password[i] = value;
        }
        modem_des_encrypt_block(upper_password, magic, password_hash);
        modem_des_encrypt_block(upper_password + 7, magic, password_hash + 8);
        std::memcpy(padded_hash, password_hash, sizeof(password_hash));
        for (size_t i = 0; i < 3; i++)
            modem_des_encrypt_block(padded_hash + i * 7, challenge,
                                    message.data() + lm_response_offset + i * 8);
    }
    return message;
}

static std::vector<uint8_t>
encode_ras_frame(uint8_t address, const std::vector<uint8_t> &payload)
{
    std::vector<uint8_t> output(payload.size() + 7);
    size_t output_length = 0;
    EXPECT_TRUE(modem_ras_encode_frame(address, payload.data(), payload.size(),
                                       output.data(), output.size(), &output_length));
    output.resize(output_length);
    return output;
}

static std::vector<uint8_t>
encode_ras_frame_xon_xoff(uint8_t address, const std::vector<uint8_t> &payload, uint32_t mask)
{
    std::vector<uint8_t> output(MODEM_RAS_MAX_ENCODED_FRAME_SIZE);
    size_t output_length = 0;
    EXPECT_TRUE(modem_ras_encode_frame_xon_xoff(address, payload.data(), payload.size(), mask,
                                                output.data(), output.size(), &output_length));
    output.resize(output_length);
    return output;
}

TEST(ModemRas, EncodesTheDocumentedUncompressedFrameLayout)
{
    const uint8_t payload[] = { 0xF0, 0xF0, 0x03 };
    const uint8_t expected[] = { 0x16, 0x02, 0x00, 0x03, 0xF0, 0xF0, 0x03, 0x03, 0x1E, 0x97 };
    uint8_t output[sizeof(expected)] = {};
    size_t output_length = 0;

    ASSERT_TRUE(modem_ras_encode_frame(MODEM_RAS_DESTINATION, payload, sizeof(payload),
                                       output, sizeof(output), &output_length));
    ASSERT_EQ(output_length, sizeof(expected));
    EXPECT_EQ(std::vector<uint8_t>(output, output + output_length),
              std::vector<uint8_t>(expected, expected + sizeof(expected)));
}

TEST(ModemRas, SendsNbfPayloadAsDestinationFrame)
{
    const std::vector<uint8_t> payload = { 0xF0, 0xF0, 0x03 };
    std::vector<uint8_t> wire;

    ASSERT_TRUE(modem_ras_send_nbf(capture_serial_bytes, &wire, payload.data(),
                                   (int) payload.size()));
    EXPECT_EQ(wire, encode_ras_frame(MODEM_RAS_DESTINATION, payload));
}

TEST(ModemRas, EncodesXonXoffFrameWithEscapedControlsAndSevenBitLength)
{
    const std::vector<uint8_t> payload = { 0x11, 0x13, 0x7D, 0x16 };
    const auto wire = encode_ras_frame_xon_xoff(MODEM_RAS_DESTINATION, payload,
                                                MODEM_RAS_XONXOFF_MASK);
    const std::vector<uint8_t> expected = {
        0x36, 0x22, 0x80, 0x8A, 0x7D, 0x31, 0x7D, 0x33, 0x7D, 0x5D,
        0x16, 0x03, 0xD2, 0x7B
    };

    EXPECT_EQ(wire, expected);

    std::vector<uint8_t> sent;
    ASSERT_TRUE(modem_ras_send_nbf_xon_xoff(capture_serial_bytes, &sent, payload.data(),
                                            (int) payload.size(), MODEM_RAS_XONXOFF_MASK));
    EXPECT_EQ(sent, encode_ras_frame_xon_xoff(MODEM_RAS_DESTINATION, payload,
                                              MODEM_RAS_XONXOFF_MASK));
}

TEST(ModemRas, DecodesXonXoffFrameAndRestoresEscapedPayload)
{
    const std::vector<uint8_t> payload = { 0x11, 0x13, 0x7D, 0x16, 0x03 };
    auto wire = encode_ras_frame_xon_xoff(MODEM_RAS_BROADCAST, payload,
                                          MODEM_RAS_XONXOFF_MASK | (1u << 0x03));
    wire.insert(wire.begin() + 4, 0x11);
    wire[3] = static_cast<uint8_t>(0x80 | (wire.size() - 4));
    ras_capture_t capture;
    modem_ras_decoder_t decoder;
    modem_ras_decoder_init(&decoder, capture_ras_frame, capture_ras_event, &capture);
    modem_ras_decoder_set_xon_xoff_mask(&decoder,
                                        MODEM_RAS_XONXOFF_MASK | (1u << 0x03));

    for (uint8_t byte : wire)
        modem_ras_rx_byte(&decoder, byte);

    EXPECT_EQ(capture.frames, 1);
    EXPECT_EQ(capture.address, MODEM_RAS_BROADCAST);
    EXPECT_EQ(capture.payload, payload);
    EXPECT_EQ(capture.events, std::vector<modem_ras_event_t> { MODEM_RAS_EVENT_FRAME_RECEIVED });
}

TEST(ModemRas, ReportsIncompleteXonXoffEscapeAndRecovers)
{
    const uint8_t malformed[] = { 0x36, 0x22, 0x80, 0x84, 0x55, 0x03, 0x00, 0x7D };
    const std::vector<uint8_t> payload = { 0xF0, 0xF0, 0x03 };
    const auto valid = encode_ras_frame(MODEM_RAS_DESTINATION, payload);
    ras_capture_t capture;
    modem_ras_decoder_t decoder;
    modem_ras_decoder_init(&decoder, capture_ras_frame, capture_ras_event, &capture);

    for (uint8_t byte : malformed)
        modem_ras_rx_byte(&decoder, byte);
    EXPECT_EQ(capture.events,
              std::vector<modem_ras_event_t> { MODEM_RAS_EVENT_INVALID_ESCAPE });

    for (uint8_t byte : valid)
        modem_ras_rx_byte(&decoder, byte);
    EXPECT_EQ(capture.frames, 1);
    EXPECT_EQ(capture.payload, payload);
    ASSERT_EQ(capture.events.size(), 2u);
    EXPECT_EQ(capture.events.back(), MODEM_RAS_EVENT_FRAME_RECEIVED);
}

TEST(ModemRas, NegotiatesVersionAndIssuesChallenge)
{
    const uint8_t protocol_request[13] = { MODEM_RAS_FRAME_PROTOCOL, 20, 0 };
    modem_ras_session_t session;
    ras_message_capture_t capture;
    modem_ras_session_init(&session);

    EXPECT_EQ(modem_ras_session_process_message(&session, protocol_request,
                                                sizeof(protocol_request), "", "", capture_ras_message,
                                                &capture, fill_ras_challenge),
              MODEM_RAS_SESSION_PROTOCOL_NEGOTIATED);
    ASSERT_EQ(capture.messages.size(), 2);
    ASSERT_EQ(capture.messages[0].size(), sizeof(protocol_request));
    EXPECT_EQ(capture.messages[0][0], MODEM_RAS_FRAME_PROTOCOL);
    EXPECT_EQ(capture.messages[0][1], MODEM_RAS_PROTOCOL_VERSION_20);
    EXPECT_EQ(capture.messages[0][2], 0);
    EXPECT_EQ(capture.messages[1][0], MODEM_RAS_FRAME_CHALLENGE);
    for (uint8_t i = 0; i < MODEM_RAS_CHALLENGE_LENGTH; i++)
        EXPECT_EQ(capture.messages[1][i + 1], i);
    EXPECT_EQ(session.client_version, 20);
    EXPECT_EQ(session.state, MODEM_RAS_SESSION_WAIT_RESPONSE);
}

TEST(ModemRas, ReportsChallengeRandomFailure)
{
    const uint8_t protocol_request[13] = { MODEM_RAS_FRAME_PROTOCOL, 20, 0 };
    modem_ras_session_t session;
    ras_message_capture_t capture;
    modem_ras_session_init(&session);

    EXPECT_EQ(modem_ras_session_process_message(&session, protocol_request,
                                                sizeof(protocol_request), "", "", capture_ras_message,
                                                &capture, fail_ras_challenge),
              MODEM_RAS_SESSION_RANDOM_FAILURE);
    ASSERT_EQ(capture.messages.size(), 1u);
    EXPECT_EQ(capture.messages[0][0], MODEM_RAS_FRAME_PROTOCOL);
    EXPECT_NE(session.state, MODEM_RAS_SESSION_WAIT_RESPONSE);
}

TEST(ModemRas, AcceptsZeroPaddedWindows95ProtocolMessage)
{
    std::vector<uint8_t> protocol_request(333, 0);
    protocol_request[0] = MODEM_RAS_FRAME_PROTOCOL;
    protocol_request[1] = 20;
    modem_ras_session_t session;
    ras_message_capture_t capture;
    modem_ras_session_init(&session);

    EXPECT_EQ(modem_ras_session_process_message(&session, protocol_request.data(),
                                                protocol_request.size(), "", "", capture_ras_message,
                                                &capture, fill_ras_challenge),
              MODEM_RAS_SESSION_PROTOCOL_NEGOTIATED);
    ASSERT_EQ(capture.messages.size(), 2);
    EXPECT_EQ(capture.messages[0],
              (std::vector<uint8_t> { MODEM_RAS_FRAME_PROTOCOL, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }));
    EXPECT_EQ(capture.messages[1][0], MODEM_RAS_FRAME_CHALLENGE);
}

TEST(ModemRas, RejectsNonzeroProtocolPadding)
{
    std::vector<uint8_t> protocol_request(333, 0);
    protocol_request[0] = MODEM_RAS_FRAME_PROTOCOL;
    protocol_request[1] = 20;
    protocol_request.back() = 1;
    modem_ras_session_t session;
    ras_message_capture_t capture;
    modem_ras_session_init(&session);

    EXPECT_EQ(modem_ras_session_process_message(&session, protocol_request.data(),
                                                protocol_request.size(), "", "", capture_ras_message,
                                                &capture, fill_ras_challenge),
              MODEM_RAS_SESSION_INVALID_MESSAGE);
    EXPECT_TRUE(capture.messages.empty());
}

TEST(ModemRas, AcceptsConfiguredCredentialsAndIgnoresBytesAfterUsernameTerminator)
{
    const uint8_t protocol_request[13] = { MODEM_RAS_FRAME_PROTOCOL, 20, 0 };
    const std::vector<uint8_t> challenge_response =
        make_ras_response_20("86Box", "86Box", true);
    const std::vector<uint8_t> configuration_request = make_ras_configuration_request();
    modem_ras_session_t session;
    ras_message_capture_t capture;
    modem_ras_session_init(&session);

    ASSERT_EQ(modem_ras_session_process_message(&session, protocol_request,
                                                sizeof(protocol_request), "86Box", "86Box",
                                                capture_ras_message,
                                                &capture, fill_ras_challenge),
              MODEM_RAS_SESSION_PROTOCOL_NEGOTIATED);
    EXPECT_EQ(modem_ras_session_process_message(&session, challenge_response.data(),
                                                challenge_response.size(), "86Box", "86Box",
                                                capture_ras_message,
                                                &capture, fill_ras_challenge),
              MODEM_RAS_SESSION_AUTHENTICATED_RESULT);
    ASSERT_EQ(capture.messages.size(), 3);
    EXPECT_EQ(capture.messages[2],
              (std::vector<uint8_t> { MODEM_RAS_FRAME_RESULT, 0x00, 0x61 }));
    EXPECT_EQ(session.state, MODEM_RAS_SESSION_AUTHENTICATED);
    EXPECT_EQ(modem_ras_session_process_message(&session, configuration_request.data(),
                                                configuration_request.size(), "86Box", "86Box",
                                                capture_ras_message, &capture, fill_ras_challenge),
              MODEM_RAS_SESSION_CONFIGURATION_ACCEPTED);
    ASSERT_EQ(capture.messages.size(), 5u);
    ASSERT_EQ(capture.messages[3].size(), 333u);
    EXPECT_EQ(capture.messages[3][0], MODEM_RAS_FRAME_CONFIGURATION_RESULT);
    EXPECT_EQ(capture.messages[3][1], 0x00);
    EXPECT_EQ(capture.messages[3][2], 0x00);
    EXPECT_EQ(capture.messages[3][13], 0xEA);
    EXPECT_EQ(capture.messages[3][14], 0x05);
    EXPECT_EQ(capture.messages[3][17], 0xEA);
    EXPECT_EQ(capture.messages[3][18], 0x05);
    EXPECT_EQ(capture.messages[3][21], 0x00);
    EXPECT_EQ(capture.messages[3][22], 0xC2);
    EXPECT_EQ(capture.messages[3][25], 0x00);
    EXPECT_EQ(capture.messages[3][26], 0x65);
    EXPECT_EQ(capture.messages[3][34], 5);
    EXPECT_EQ(capture.messages[3][35], 0);
    const char *projected_names[] = { "86BOX95", "WORKGROUP", "86BOX95", "86BOX95",
                                      "WORKGROUP" };
    const uint8_t projected_suffixes[] = { 0x00, 0x00, 0x03, 0x20, 0x1E };
    const uint8_t projected_flags[] = { 1, 2, 1, 1, 2 };
    for (size_t i = 0; i < 5; i++) {
        std::string expected_name = projected_names[i];
        expected_name.resize(15, ' ');
        const size_t offset = 36 + i * 18;

        EXPECT_EQ(std::memcmp(capture.messages[3].data() + offset, expected_name.data(), 15), 0);
        EXPECT_EQ(capture.messages[3][offset + 15], projected_suffixes[i]);
        EXPECT_EQ(capture.messages[3][offset + 16], projected_flags[i]);
        EXPECT_EQ(capture.messages[3][offset + 17], 0);
    }
    EXPECT_EQ(session.projected_name_count, 5);
    EXPECT_EQ(std::memcmp(session.projected_names[0].name, "86BOX95        ", 15), 0);
    EXPECT_EQ(session.projected_names[0].name[15], 0x00);
    EXPECT_EQ(session.projected_names[0].flags, 1);
    EXPECT_EQ(capture.messages[4],
              (std::vector<uint8_t> { MODEM_RAS_FRAME_LINK_SPEED, 0, 0, 0, 0 }));
    EXPECT_EQ(session.state, MODEM_RAS_SESSION_CONFIGURED);
    EXPECT_EQ(modem_ras_session_process_message(&session, configuration_request.data(),
                                                configuration_request.size(), "86Box", "86Box",
                                                capture_ras_message, &capture, fill_ras_challenge),
              MODEM_RAS_SESSION_POST_CONFIGURATION_UNIMPLEMENTED);
}

TEST(ModemRas, RejectsResponseWhenConfiguredCredentialsDoNotMatch)
{
    const uint8_t protocol_request[13] = { MODEM_RAS_FRAME_PROTOCOL, 20, 0 };
    const std::vector<uint8_t> challenge_response =
        make_ras_response_20("86Box", "86Box");
    modem_ras_session_t session;
    ras_message_capture_t capture;
    modem_ras_session_init(&session);

    ASSERT_EQ(modem_ras_session_process_message(&session, protocol_request,
                                                sizeof(protocol_request), "wrong", "wrong",
                                                capture_ras_message,
                                                &capture, fill_ras_challenge),
              MODEM_RAS_SESSION_PROTOCOL_NEGOTIATED);
    EXPECT_EQ(modem_ras_session_process_message(&session, challenge_response.data(),
                                                challenge_response.size(), "wrong", "wrong",
                                                capture_ras_message,
                                                &capture, fill_ras_challenge),
              MODEM_RAS_SESSION_AUTH_REJECTED);
    EXPECT_EQ(capture.messages.back(),
              (std::vector<uint8_t> { MODEM_RAS_FRAME_RESULT, 0x00, 0x62 }));
}

TEST(ModemRas, AcceptsNtResponseWhenSelected)
{
    const uint8_t protocol_request[13] = { MODEM_RAS_FRAME_PROTOCOL, 20, 0 };
    const std::vector<uint8_t> challenge_response =
        make_ras_response_20("86Box", "86Box", false, true);
    modem_ras_session_t session;
    ras_message_capture_t capture;
    modem_ras_session_init(&session);

    ASSERT_EQ(modem_ras_session_process_message(&session, protocol_request,
                                                sizeof(protocol_request), "86Box", "86Box",
                                                capture_ras_message, &capture,
                                                fill_ras_challenge),
              MODEM_RAS_SESSION_PROTOCOL_NEGOTIATED);
    EXPECT_EQ(modem_ras_session_process_message(&session, challenge_response.data(),
                                                challenge_response.size(), "86Box", "86Box",
                                                capture_ras_message, &capture,
                                                fill_ras_challenge),
              MODEM_RAS_SESSION_AUTHENTICATED_RESULT);
}

TEST(ModemRas, AcceptsLegacyLmResponse)
{
    const uint8_t protocol_request[13] = { MODEM_RAS_FRAME_PROTOCOL, 20, 0 };
    const std::vector<uint8_t> response_20 = make_ras_response_20("86Box", "86Box");
    std::vector<uint8_t> response_10(46, 0);
    modem_ras_session_t session;
    ras_message_capture_t capture;
    modem_ras_session_init(&session);

    response_10[0] = MODEM_RAS_FRAME_RESPONSE;
    std::memcpy(response_10.data() + 1, "86Box", 5);
    std::memcpy(response_10.data() + 22, response_20.data() + 274, 24);
    ASSERT_EQ(modem_ras_session_process_message(&session, protocol_request,
                                                sizeof(protocol_request), "86Box", "86Box",
                                                capture_ras_message, &capture,
                                                fill_ras_challenge),
              MODEM_RAS_SESSION_PROTOCOL_NEGOTIATED);
    EXPECT_EQ(modem_ras_session_process_message(&session, response_10.data(),
                                                response_10.size(), "86Box", "86Box",
                                                capture_ras_message, &capture,
                                                fill_ras_challenge),
              MODEM_RAS_SESSION_AUTHENTICATED_RESULT);
}

TEST(ModemRas, RejectsUnsupportedConfigurationProjection)
{
    const uint8_t protocol_request[13] = { MODEM_RAS_FRAME_PROTOCOL, 20, 0 };
    const std::vector<uint8_t> challenge_response = make_ras_response_20("86Box", "86Box");
    const std::vector<uint8_t> configuration_request = make_ras_configuration_request(true);
    modem_ras_session_t session;
    ras_message_capture_t capture;
    modem_ras_session_init(&session);

    ASSERT_EQ(modem_ras_session_process_message(&session, protocol_request,
                                                sizeof(protocol_request), "86Box", "86Box",
                                                capture_ras_message, &capture, fill_ras_challenge),
              MODEM_RAS_SESSION_PROTOCOL_NEGOTIATED);
    ASSERT_EQ(modem_ras_session_process_message(&session, challenge_response.data(),
                                                challenge_response.size(), "86Box", "86Box",
                                                capture_ras_message, &capture, fill_ras_challenge),
              MODEM_RAS_SESSION_AUTHENTICATED_RESULT);
    EXPECT_EQ(modem_ras_session_process_message(&session, configuration_request.data(),
                                                configuration_request.size(), "86Box", "86Box",
                                                capture_ras_message, &capture, fill_ras_challenge),
              MODEM_RAS_SESSION_CONFIGURATION_REJECTED);
    ASSERT_EQ(capture.messages.back().size(), 333u);
    EXPECT_EQ(capture.messages.back()[0], MODEM_RAS_FRAME_CONFIGURATION_RESULT);
    EXPECT_EQ(capture.messages.back()[1], 0x00);
    EXPECT_EQ(capture.messages.back()[2], 0x74);
    EXPECT_EQ(session.projected_name_count, 0);
    EXPECT_EQ(session.state, MODEM_RAS_SESSION_REJECTED);
}

TEST(ModemRas, RejectsMalformedResponse)
{
    const uint8_t protocol_request[13] = { MODEM_RAS_FRAME_PROTOCOL, 20, 0 };
    const uint8_t challenge_response[] = { MODEM_RAS_FRAME_RESPONSE_20 };
    modem_ras_session_t session;
    ras_message_capture_t capture;
    modem_ras_session_init(&session);

    ASSERT_EQ(modem_ras_session_process_message(&session, protocol_request,
                                                sizeof(protocol_request), "", "",
                                                capture_ras_message, &capture,
                                                fill_ras_challenge),
              MODEM_RAS_SESSION_PROTOCOL_NEGOTIATED);
    EXPECT_EQ(modem_ras_session_process_message(&session, challenge_response,
                                                sizeof(challenge_response), "", "",
                                                capture_ras_message, &capture,
                                                fill_ras_challenge),
              MODEM_RAS_SESSION_INVALID_MESSAGE);
    EXPECT_EQ(capture.messages.size(), 2u);
}

TEST(ModemRas, DecodesByteByByteAndPreservesPayloadBytes)
{
    const std::vector<uint8_t> payload = { 0xF0, 0xF0, 0x03, 0x16, 0x00, 0x7E };
    const auto wire = encode_ras_frame(MODEM_RAS_BROADCAST, payload);
    ras_capture_t capture;
    modem_ras_decoder_t decoder;
    modem_ras_decoder_init(&decoder, capture_ras_frame, capture_ras_event, &capture);

    for (uint8_t byte : wire)
        modem_ras_rx_byte(&decoder, byte);

    EXPECT_EQ(capture.frames, 1);
    EXPECT_EQ(capture.address, MODEM_RAS_BROADCAST);
    EXPECT_EQ(capture.payload, payload);
    EXPECT_EQ(capture.events, std::vector<modem_ras_event_t> { MODEM_RAS_EVENT_FRAME_RECEIVED });
}

TEST(ModemRas, DropsBadCrcAndAcceptsFollowingFrame)
{
    const std::vector<uint8_t> payload = { 0xF0, 0xF0, 0x03, 0x01 };
    auto invalid = encode_ras_frame(MODEM_RAS_DESTINATION, payload);
    invalid.back() ^= 0x80;
    const auto valid = encode_ras_frame(MODEM_RAS_DESTINATION, payload);
    ras_capture_t capture;
    modem_ras_decoder_t decoder;
    modem_ras_decoder_init(&decoder, capture_ras_frame, capture_ras_event, &capture);

    for (uint8_t byte : invalid)
        modem_ras_rx_byte(&decoder, byte);
    EXPECT_EQ(capture.frames, 0);
    ASSERT_EQ(capture.events.size(), 1);
    EXPECT_EQ(capture.events[0], MODEM_RAS_EVENT_BAD_CRC);
    EXPECT_EQ(capture.event_length, static_cast<uint16_t>(payload.size()));
    EXPECT_NE(capture.event_actual, capture.event_expected);

    for (uint8_t byte : valid)
        modem_ras_rx_byte(&decoder, byte);
    EXPECT_EQ(capture.frames, 1);
    EXPECT_EQ(capture.payload, payload);
    ASSERT_EQ(capture.events.size(), 2);
    EXPECT_EQ(capture.events[1], MODEM_RAS_EVENT_FRAME_RECEIVED);
}

TEST(ModemRas, ReportsInvalidAddressLengthAndEtx)
{
    const uint8_t invalid_address[] = { 0x16, 0x22 };
    const uint8_t oversized_length[] = { 0x16, 0x02, 0x05, 0xDD };
    auto invalid_etx = encode_ras_frame(MODEM_RAS_DESTINATION, { 0xF0, 0xF0, 0x03 });
    invalid_etx[7] = 0x04;
    ras_capture_t capture;
    modem_ras_decoder_t decoder;
    modem_ras_decoder_init(&decoder, capture_ras_frame, capture_ras_event, &capture);

    for (uint8_t byte : invalid_address)
        modem_ras_rx_byte(&decoder, byte);
    for (uint8_t byte : oversized_length)
        modem_ras_rx_byte(&decoder, byte);
    EXPECT_EQ(capture.frames, 0);
    ASSERT_EQ(capture.events.size(), 2);
    EXPECT_EQ(capture.event_actual, 0x05DD);
    EXPECT_EQ(capture.event_expected, MODEM_RAS_MAX_PAYLOAD);

    for (uint8_t byte : invalid_etx)
        modem_ras_rx_byte(&decoder, byte);
    EXPECT_EQ(capture.frames, 0);
    ASSERT_EQ(capture.events.size(), 3);
    EXPECT_EQ(capture.events[0], MODEM_RAS_EVENT_INVALID_ADDRESS);
    EXPECT_EQ(capture.events[1], MODEM_RAS_EVENT_INVALID_LENGTH);
    EXPECT_EQ(capture.events[2], MODEM_RAS_EVENT_INVALID_ETX);
}

TEST(ModemRas, EnforcesPayloadAndOutputCapacityLimits)
{
    std::vector<uint8_t> payload(MODEM_RAS_MAX_PAYLOAD, 0x55);
    std::vector<uint8_t> output(payload.size() + 7);
    size_t output_length = 0;

    EXPECT_TRUE(modem_ras_encode_frame(MODEM_RAS_DESTINATION, payload.data(), payload.size(),
                                        output.data(), output.size(), &output_length));
    EXPECT_EQ(output_length, output.size());
    EXPECT_FALSE(modem_ras_encode_frame(MODEM_RAS_DESTINATION, payload.data(), payload.size(),
                                        output.data(), output.size() - 1, &output_length));
    EXPECT_EQ(output_length, 0);
    payload.push_back(0x55);
    EXPECT_FALSE(modem_ras_encode_frame(MODEM_RAS_DESTINATION, payload.data(), payload.size(),
                                        output.data(), output.size(), &output_length));
    EXPECT_EQ(output_length, 0);
}
