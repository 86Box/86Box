#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "net_slirp_rarp.h"

#define RARP_FRAME_LENGTH 42

static uint16_t
get_be16(const uint8_t *data)
{
    return (uint16_t) (((uint16_t) data[0] << 8) | data[1]);
}

static uint32_t
get_be32(const uint8_t *data)
{
    return ((uint32_t) data[0] << 24) | ((uint32_t) data[1] << 16) | ((uint32_t) data[2] << 8) | data[3];
}

static void
put_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t) (value >> 8);
    data[1] = (uint8_t) value;
}

static void
make_request(uint8_t *frame, const uint8_t guest_mac[6])
{
    memset(frame, 0, RARP_FRAME_LENGTH);
    memset(frame, 0xFF, 6);
    memcpy(frame + 6, guest_mac, 6);
    put_be16(frame + 12, 0x8035);
    put_be16(frame + 14, 1);
    put_be16(frame + 16, 0x0800);
    frame[18] = 6;
    frame[19] = 4;
    put_be16(frame + 20, 3);
    memcpy(frame + 22, guest_mac, 6);
    memcpy(frame + 32, guest_mac, 6);
}

static void
test_replies_with_configured_guest_address(void)
{
    const uint8_t guest_mac[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
    const uint8_t server_mac[6] = { 0x52, 0x52, 10, 0, 2, 2 };
    uint8_t       request[RARP_FRAME_LENGTH];
    uint8_t       reply[RARP_FRAME_LENGTH];
    size_t        reply_length = 0;

    make_request(request, guest_mac);
    memset(request + 28, 0xA5, 4);
    memset(request + 38, 0x5A, 4);
    assert(net_slirp_rarp_reply(request, sizeof(request), guest_mac, 0x0A00020F, server_mac, 0x0A000202,
                                reply, sizeof(reply), &reply_length));
    assert(reply_length == RARP_FRAME_LENGTH);
    assert(memcmp(reply, guest_mac, 6) == 0);
    assert(memcmp(reply + 6, server_mac, 6) == 0);
    assert(get_be16(reply + 12) == 0x8035);
    assert(get_be16(reply + 14) == 1 && get_be16(reply + 16) == 0x0800);
    assert(reply[18] == 6 && reply[19] == 4 && get_be16(reply + 20) == 4);
    assert(memcmp(reply + 22, server_mac, 6) == 0);
    assert(get_be32(reply + 28) == 0x0A000202);
    assert(memcmp(reply + 32, guest_mac, 6) == 0);
    assert(get_be32(reply + 38) == 0x0A00020F);
}

static void
test_ignores_other_rarp_requests(void)
{
    const uint8_t guest_mac[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
    const uint8_t other_mac[6] = { 0x52, 0x54, 0x00, 0xAA, 0xBB, 0xCC };
    const uint8_t server_mac[6] = { 0x52, 0x52, 10, 0, 2, 2 };
    uint8_t       request[RARP_FRAME_LENGTH];
    uint8_t       reply[RARP_FRAME_LENGTH];
    size_t        reply_length = 0;

    make_request(request, guest_mac);
    assert(!net_slirp_rarp_reply(request, sizeof(request), other_mac, 0x0A00020F, server_mac, 0x0A000202,
                                 reply, sizeof(reply), &reply_length));
    assert(!net_slirp_rarp_reply(request, sizeof(request) - 1, guest_mac, 0x0A00020F, server_mac, 0x0A000202,
                                 reply, sizeof(reply), &reply_length));

    put_be16(request + 20, 4);
    assert(!net_slirp_rarp_reply(request, sizeof(request), guest_mac, 0x0A00020F, server_mac, 0x0A000202,
                                 reply, sizeof(reply), &reply_length));
}

int
main(void)
{
    test_replies_with_configured_guest_address();
    test_ignores_other_rarp_requests();
    return 0;
}
