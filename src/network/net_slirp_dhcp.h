#ifndef NETWORK_NET_SLIRP_DHCP_H
#define NETWORK_NET_SLIRP_DHCP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool net_slirp_dhcp_add_vserver_options(uint8_t *frame, size_t *frame_length, size_t frame_capacity,
                                       uint32_t server_ip, bool enable_wins, bool enable_tftp,
                                       const char *boot_file_name);
bool net_slirp_dhcp_add_wins_options(uint8_t *frame, size_t *frame_length, size_t frame_capacity,
                                    uint32_t wins_server_ip);
bool net_slirp_dhcp_prepare_bootp_request(uint8_t *frame, size_t *frame_length, size_t frame_capacity);

#endif
