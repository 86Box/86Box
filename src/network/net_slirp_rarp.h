#ifndef NETWORK_NET_SLIRP_RARP_H
#define NETWORK_NET_SLIRP_RARP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool net_slirp_rarp_reply(const uint8_t *request, size_t request_length, const uint8_t guest_mac[6],
                          uint32_t guest_ip, const uint8_t server_mac[6], uint32_t server_ip,
                          uint8_t *reply, size_t reply_capacity, size_t *reply_length);

#endif
