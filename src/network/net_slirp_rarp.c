#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "net_slirp_rarp.h"

#define ETHERNET_HEADER_LENGTH 14
#define RARP_PACKET_LENGTH 28
#define RARP_FRAME_LENGTH (ETHERNET_HEADER_LENGTH + RARP_PACKET_LENGTH)

#define ETHERTYPE_RARP 0x8035
#define ETHERTYPE_IPV4 0x0800
#define RARP_HARDWARE_ETHERNET 1
#define RARP_REQUEST 3
#define RARP_REPLY 4

static uint16_t
rarp_get_be16(const uint8_t *data)
{
    return (uint16_t) (((uint16_t) data[0] << 8) | data[1]);
}

static void
rarp_put_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t) (value >> 8);
    data[1] = (uint8_t) value;
}

static void
rarp_put_be32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t) (value >> 24);
    data[1] = (uint8_t) (value >> 16);
    data[2] = (uint8_t) (value >> 8);
    data[3] = (uint8_t) value;
}

bool
net_slirp_rarp_reply(const uint8_t *request, size_t request_length, const uint8_t guest_mac[6], uint32_t guest_ip,
                     const uint8_t server_mac[6], uint32_t server_ip, uint8_t *reply, size_t reply_capacity,
                     size_t *reply_length)
{
    uint8_t response[RARP_FRAME_LENGTH] = { 0 };
    const uint8_t *rarp;

    if (!request || !guest_mac || !server_mac || !reply || !reply_length || reply_capacity < RARP_FRAME_LENGTH
        || request_length < RARP_FRAME_LENGTH || rarp_get_be16(request + 12) != ETHERTYPE_RARP)
        return false;

    rarp = request + ETHERNET_HEADER_LENGTH;
    if (rarp_get_be16(rarp) != RARP_HARDWARE_ETHERNET || rarp_get_be16(rarp + 2) != ETHERTYPE_IPV4
        || rarp[4] != 6 || rarp[5] != 4 || rarp_get_be16(rarp + 6) != RARP_REQUEST
        || memcmp(request + 6, guest_mac, 6) || memcmp(rarp + 8, guest_mac, 6)
        || memcmp(rarp + 18, guest_mac, 6) || !guest_ip)
        return false;

    memcpy(response, guest_mac, 6);
    memcpy(response + 6, server_mac, 6);
    rarp_put_be16(response + 12, ETHERTYPE_RARP);
    rarp_put_be16(response + 14, RARP_HARDWARE_ETHERNET);
    rarp_put_be16(response + 16, ETHERTYPE_IPV4);
    response[18] = 6;
    response[19] = 4;
    rarp_put_be16(response + 20, RARP_REPLY);
    memcpy(response + 22, server_mac, 6);
    rarp_put_be32(response + 28, server_ip);
    memcpy(response + 32, guest_mac, 6);
    rarp_put_be32(response + 38, guest_ip);

    memcpy(reply, response, sizeof(response));
    *reply_length = sizeof(response);
    return true;
}
