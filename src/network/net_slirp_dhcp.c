#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "net_slirp_dhcp.h"

#define ETHERNET_HEADER_LENGTH 14
#define IPV4_MIN_HEADER_LENGTH 20
#define UDP_HEADER_LENGTH 8
#define BOOTP_FIXED_LENGTH 236
#define DHCP_COOKIE_LENGTH 4
#define DHCP_OPTIONS_OFFSET (BOOTP_FIXED_LENGTH + DHCP_COOKIE_LENGTH)

#define ETHERTYPE_IPV4 0x0800
#define IP_PROTOCOL_UDP 17
#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68
#define DHCP_BOOTREPLY 2
#define DHCP_OPTION_PAD 0
#define DHCP_OPTION_END 255
#define DHCP_OPTION_MESSAGE_TYPE 53
#define DHCP_OPTION_WINS 44
#define DHCP_OPTION_NODE_TYPE 46
#define DHCP_OPTION_TFTP_SERVER_NAME 66
#define DHCP_OPTION_BOOTFILE_NAME 67
#define DHCP_BOOTFILE_NAME_OFFSET 108
#define DHCP_BOOTFILE_NAME_LENGTH 128
#define DHCP_OFFER 2
#define DHCP_ACK 5
#define DHCP_H_NODE 8

static uint16_t
dhcp_get_be16(const uint8_t *data)
{
    return (uint16_t) (((uint16_t) data[0] << 8) | data[1]);
}

static void
dhcp_put_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t) (value >> 8);
    data[1] = (uint8_t) value;
}

static uint32_t
dhcp_pseudo_header_sum(const uint8_t *ip, uint16_t udp_length)
{
    return dhcp_get_be16(ip + 12) + dhcp_get_be16(ip + 14) + dhcp_get_be16(ip + 16)
         + dhcp_get_be16(ip + 18) + IP_PROTOCOL_UDP + udp_length;
}

static uint16_t
dhcp_checksum(uint32_t sum, const uint8_t *data, size_t length)
{
    for (size_t i = 0; i + 1 < length; i += 2)
        sum += dhcp_get_be16(data + i);
    if (length & 1)
        sum += (uint32_t) data[length - 1] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t) ~sum;
}

static void
dhcp_update_checksums(uint8_t *ip, size_t ip_header_length, uint8_t *udp, uint16_t udp_length,
                     uint16_t old_udp_checksum)
{
    dhcp_put_be16(ip + 10, 0);
    dhcp_put_be16(ip + 10, dhcp_checksum(0, ip, ip_header_length));

    if (old_udp_checksum) {
        dhcp_put_be16(udp + 6, 0);
        uint16_t checksum = dhcp_checksum(dhcp_pseudo_header_sum(ip, udp_length), udp, udp_length);
        dhcp_put_be16(udp + 6, checksum ? checksum : 0xFFFF);
    }
}

bool
net_slirp_dhcp_add_vserver_options(uint8_t *frame, size_t *frame_length, size_t frame_capacity,
                                   uint32_t server_ip, bool enable_wins, bool enable_tftp,
                                   const char *boot_file_name)
{
    uint8_t *ip;
    uint8_t *udp;
    uint8_t *bootp;
    uint8_t *options;
    size_t   ip_header_length;
    size_t   ip_length;
    size_t   udp_length;
    size_t   options_length;
    size_t   end_offset = 0;
    size_t   boot_file_offset = 0;
    size_t   boot_file_length = 0;
    size_t   boot_file_name_length = 0;
    size_t   append_length = 0;
    char     tftp_server_name[16];
    int      tftp_server_name_length = 0;
    ptrdiff_t boot_file_delta = 0;
    ptrdiff_t total_delta;
    uint16_t old_udp_checksum;
    bool     has_message_type = false;
    bool     has_wins = false;
    bool     has_node_type = false;
    bool     has_tftp_server_name = false;
    bool     has_boot_file = false;
    bool     is_offer_or_ack = false;
    bool     changed = false;

    if (!frame || !frame_length || *frame_length > frame_capacity || *frame_length < ETHERNET_HEADER_LENGTH
        || dhcp_get_be16(frame + 12) != ETHERTYPE_IPV4 || (!enable_wins && !enable_tftp))
        return false;
    if (enable_tftp) {
        if (!boot_file_name || !*boot_file_name)
            return false;
        boot_file_name_length = strlen(boot_file_name);
        if (boot_file_name_length > UINT8_MAX || boot_file_name_length > DHCP_BOOTFILE_NAME_LENGTH)
            return false;
        tftp_server_name_length = snprintf(tftp_server_name, sizeof(tftp_server_name), "%u.%u.%u.%u",
                                           (server_ip >> 24) & 0xFF, (server_ip >> 16) & 0xFF,
                                           (server_ip >> 8) & 0xFF, server_ip & 0xFF);
        if (tftp_server_name_length < 0 || (size_t) tftp_server_name_length >= sizeof(tftp_server_name))
            return false;
    }

    ip = frame + ETHERNET_HEADER_LENGTH;
    if ((ip[0] >> 4) != 4)
        return false;
    ip_header_length = (size_t) (ip[0] & 0x0F) * 4;
    if (ip_header_length < IPV4_MIN_HEADER_LENGTH || *frame_length < ETHERNET_HEADER_LENGTH + ip_header_length
        || ip[9] != IP_PROTOCOL_UDP || (dhcp_get_be16(ip + 6) & 0x3FFF))
        return false;

    ip_length = dhcp_get_be16(ip + 2);
    if (ip_length < ip_header_length + UDP_HEADER_LENGTH + DHCP_OPTIONS_OFFSET
        || ip_length > *frame_length - ETHERNET_HEADER_LENGTH)
        return false;

    udp = ip + ip_header_length;
    udp_length = dhcp_get_be16(udp + 4);
    if (dhcp_get_be16(udp) != DHCP_SERVER_PORT || dhcp_get_be16(udp + 2) != DHCP_CLIENT_PORT
        || udp_length < UDP_HEADER_LENGTH + DHCP_OPTIONS_OFFSET || udp_length > ip_length - ip_header_length)
        return false;

    bootp = udp + UDP_HEADER_LENGTH;
    if (bootp[0] != DHCP_BOOTREPLY || memcmp(bootp + BOOTP_FIXED_LENGTH, "\x63\x82\x53\x63", DHCP_COOKIE_LENGTH))
        return false;

    options = bootp + DHCP_OPTIONS_OFFSET;
    options_length = udp_length - UDP_HEADER_LENGTH - DHCP_OPTIONS_OFFSET;
    for (size_t i = 0; i < options_length;) {
        uint8_t option = options[i];
        size_t  length;

        if (option == DHCP_OPTION_PAD) {
            i++;
            continue;
        }
        if (option == DHCP_OPTION_END) {
            end_offset = (size_t) (options + i - frame);
            break;
        }
        if (i + 2 > options_length)
            return false;
        length = options[i + 1];
        if (i + 2 + length > options_length)
            return false;

        if (option == DHCP_OPTION_MESSAGE_TYPE && length == 1) {
            has_message_type = true;
            is_offer_or_ack = options[i + 2] == DHCP_OFFER || options[i + 2] == DHCP_ACK;
        } else if (option == DHCP_OPTION_WINS && length >= 4) {
            has_wins = true;
        } else if (option == DHCP_OPTION_NODE_TYPE && length == 1) {
            has_node_type = true;
        } else if (enable_tftp && option == DHCP_OPTION_TFTP_SERVER_NAME) {
            has_tftp_server_name = true;
        } else if (enable_tftp && option == DHCP_OPTION_BOOTFILE_NAME && !has_boot_file) {
            has_boot_file = true;
            boot_file_offset = (size_t) (options + i - frame);
            boot_file_length = length;
        }
        i += 2 + length;
    }
    if (!end_offset || !has_message_type || !is_offer_or_ack)
        return false;

    if (enable_wins)
        append_length += (has_wins ? 0 : 6) + (has_node_type ? 0 : 3);
    if (enable_tftp) {
        if (!has_tftp_server_name)
            append_length += (size_t) tftp_server_name_length + 2;
        if (has_boot_file) {
            boot_file_delta = (ptrdiff_t) boot_file_name_length - (ptrdiff_t) boot_file_length;
            changed = boot_file_delta != 0
                   || memcmp(frame + boot_file_offset + 2, boot_file_name, boot_file_name_length) != 0;
        } else {
            append_length += boot_file_name_length + 2;
            changed = true;
        }
        changed = changed || dhcp_get_be16(bootp + 20) != (uint16_t) (server_ip >> 16)
                || dhcp_get_be16(bootp + 22) != (uint16_t) server_ip;
        for (size_t i = 0; i < DHCP_BOOTFILE_NAME_LENGTH; i++) {
            uint8_t expected = i < boot_file_name_length ? (uint8_t) boot_file_name[i] : 0;

            changed = changed || bootp[DHCP_BOOTFILE_NAME_OFFSET + i] != expected;
        }
    }
    total_delta = boot_file_delta + (ptrdiff_t) append_length;
    if (!append_length && !boot_file_delta && !changed)
        return false;

    if (total_delta > 0
        && ((size_t) total_delta > frame_capacity || *frame_length > frame_capacity - (size_t) total_delta))
        return false;
    if (total_delta < 0 && (size_t) -total_delta > *frame_length)
        return false;
    if ((total_delta > 0 && (ip_length > UINT16_MAX - (size_t) total_delta
                             || udp_length > UINT16_MAX - (size_t) total_delta))
        || (total_delta < 0
            && (ip_length < (size_t) -total_delta || udp_length < (size_t) -total_delta)))
        return false;

    old_udp_checksum = dhcp_get_be16(udp + 6);
    if (boot_file_delta) {
        size_t source = boot_file_offset + 2 + boot_file_length;
        size_t target = boot_file_offset + 2 + boot_file_name_length;

        memmove(frame + target, frame + source, *frame_length - source);
        frame[boot_file_offset + 1] = (uint8_t) boot_file_name_length;
        memcpy(frame + boot_file_offset + 2, boot_file_name, boot_file_name_length);
        *frame_length = (size_t) ((ptrdiff_t) *frame_length + boot_file_delta);
        ip_length = (size_t) ((ptrdiff_t) ip_length + boot_file_delta);
        udp_length = (size_t) ((ptrdiff_t) udp_length + boot_file_delta);
        end_offset = (size_t) ((ptrdiff_t) end_offset + boot_file_delta);
    } else if (enable_tftp && has_boot_file && changed) {
        memcpy(frame + boot_file_offset + 2, boot_file_name, boot_file_name_length);
    }

    if (append_length) {
        memmove(frame + end_offset + append_length, frame + end_offset, *frame_length - end_offset);
        size_t at = end_offset;

        if (enable_wins && !has_wins) {
            frame[at++] = DHCP_OPTION_WINS;
            frame[at++] = 4;
            frame[at++] = (uint8_t) (server_ip >> 24);
            frame[at++] = (uint8_t) (server_ip >> 16);
            frame[at++] = (uint8_t) (server_ip >> 8);
            frame[at++] = (uint8_t) server_ip;
        }
        if (enable_wins && !has_node_type) {
            frame[at++] = DHCP_OPTION_NODE_TYPE;
            frame[at++] = 1;
            frame[at++] = DHCP_H_NODE;
        }
        if (enable_tftp && !has_tftp_server_name) {
            frame[at++] = DHCP_OPTION_TFTP_SERVER_NAME;
            frame[at++] = (uint8_t) tftp_server_name_length;
            memcpy(frame + at, tftp_server_name, (size_t) tftp_server_name_length);
            at += (size_t) tftp_server_name_length;
        }
        if (enable_tftp && !has_boot_file) {
            frame[at++] = DHCP_OPTION_BOOTFILE_NAME;
            frame[at++] = (uint8_t) boot_file_name_length;
            memcpy(frame + at, boot_file_name, boot_file_name_length);
        }
        *frame_length += append_length;
        ip_length += append_length;
        udp_length += append_length;
    }

    if (enable_tftp) {
        dhcp_put_be16(bootp + 20, (uint16_t) (server_ip >> 16));
        dhcp_put_be16(bootp + 22, (uint16_t) server_ip);
        memset(bootp + DHCP_BOOTFILE_NAME_OFFSET, 0, DHCP_BOOTFILE_NAME_LENGTH);
        memcpy(bootp + DHCP_BOOTFILE_NAME_OFFSET, boot_file_name, boot_file_name_length);
    }
    dhcp_put_be16(ip + 2, (uint16_t) ip_length);
    dhcp_put_be16(udp + 4, (uint16_t) udp_length);
    dhcp_update_checksums(ip, ip_header_length, udp, (uint16_t) udp_length, old_udp_checksum);
    return true;
}

bool
net_slirp_dhcp_add_wins_options(uint8_t *frame, size_t *frame_length, size_t frame_capacity,
                                uint32_t wins_server_ip)
{
    return net_slirp_dhcp_add_vserver_options(frame, frame_length, frame_capacity, wins_server_ip, true, false,
                                              NULL);
}

bool
net_slirp_dhcp_prepare_bootp_request(uint8_t *frame, size_t *frame_length, size_t frame_capacity)
{
    uint8_t *ip;
    uint8_t *udp;
    uint8_t *bootp;
    size_t   ip_header_length;
    size_t   ip_length;
    size_t   udp_length;
    size_t   bootp_length;
    size_t   insert_offset;
    size_t   insert_length;
    size_t   options_length;
    size_t   option_end = 0;
    bool     has_message_type = false;
    uint16_t old_udp_checksum;

    if (!frame || !frame_length || *frame_length > frame_capacity || *frame_length < ETHERNET_HEADER_LENGTH
        || dhcp_get_be16(frame + 12) != ETHERTYPE_IPV4)
        return false;

    ip = frame + ETHERNET_HEADER_LENGTH;
    if ((ip[0] >> 4) != 4)
        return false;
    ip_header_length = (size_t) (ip[0] & 0x0F) * 4;
    if (ip_header_length < IPV4_MIN_HEADER_LENGTH || *frame_length < ETHERNET_HEADER_LENGTH + ip_header_length
        || ip[9] != IP_PROTOCOL_UDP || (dhcp_get_be16(ip + 6) & 0x3FFF))
        return false;

    ip_length = dhcp_get_be16(ip + 2);
    if (ip_length < ip_header_length + UDP_HEADER_LENGTH + BOOTP_FIXED_LENGTH
        || ip_length > *frame_length - ETHERNET_HEADER_LENGTH)
        return false;
    udp = ip + ip_header_length;
    udp_length = dhcp_get_be16(udp + 4);
    if (dhcp_get_be16(udp) != DHCP_CLIENT_PORT || dhcp_get_be16(udp + 2) != DHCP_SERVER_PORT
        || udp_length < UDP_HEADER_LENGTH + BOOTP_FIXED_LENGTH || udp_length > ip_length - ip_header_length)
        return false;

    bootp = udp + UDP_HEADER_LENGTH;
    if (bootp[0] != 1)
        return false;
    bootp_length = udp_length - UDP_HEADER_LENGTH;
    old_udp_checksum = dhcp_get_be16(udp + 6);
    if (bootp_length < DHCP_OPTIONS_OFFSET
        || memcmp(bootp + BOOTP_FIXED_LENGTH, "\x63\x82\x53\x63", DHCP_COOKIE_LENGTH)) {
        size_t new_bootp_length = bootp_length < DHCP_OPTIONS_OFFSET + 4
                                ? DHCP_OPTIONS_OFFSET + 4 : bootp_length;
        size_t extra = new_bootp_length - bootp_length;

        if (extra > frame_capacity || *frame_length > frame_capacity - extra
            || ip_length > UINT16_MAX - extra || udp_length > UINT16_MAX - extra)
            return false;
        if (extra) {
            size_t packet_end = ETHERNET_HEADER_LENGTH + ip_length;

            memmove(frame + packet_end + extra, frame + packet_end, *frame_length - packet_end);
            *frame_length += extra;
            ip_length += extra;
            udp_length += extra;
            dhcp_put_be16(ip + 2, (uint16_t) ip_length);
            dhcp_put_be16(udp + 4, (uint16_t) udp_length);
        }
        memcpy(bootp + BOOTP_FIXED_LENGTH, "\x63\x82\x53\x63", DHCP_COOKIE_LENGTH);
        bootp[DHCP_OPTIONS_OFFSET] = DHCP_OPTION_MESSAGE_TYPE;
        bootp[DHCP_OPTIONS_OFFSET + 1] = 1;
        bootp[DHCP_OPTIONS_OFFSET + 2] = 1;
        bootp[DHCP_OPTIONS_OFFSET + 3] = DHCP_OPTION_END;
        dhcp_update_checksums(ip, ip_header_length, udp, (uint16_t) udp_length, old_udp_checksum);
        return true;
    }

    options_length = bootp_length - DHCP_OPTIONS_OFFSET;
    for (size_t i = 0; i < options_length;) {
        uint8_t option = bootp[DHCP_OPTIONS_OFFSET + i];
        size_t  length;

        if (option == DHCP_OPTION_PAD) {
            i++;
            continue;
        }
        if (option == DHCP_OPTION_END) {
            option_end = DHCP_OPTIONS_OFFSET + i;
            break;
        }
        if (i + 2 > options_length)
            return false;
        length = bootp[DHCP_OPTIONS_OFFSET + i + 1];
        if (i + 2 + length > options_length)
            return false;
        if (option == DHCP_OPTION_MESSAGE_TYPE && length == 1)
            has_message_type = true;
        i += 2 + length;
    }
    if (has_message_type)
        return false;

    insert_offset = option_end ? (size_t) (bootp + option_end - frame)
                               : (size_t) (bootp + bootp_length - frame);
    insert_length = option_end ? 3 : 4;
    if (*frame_length > frame_capacity - insert_length || ip_length > UINT16_MAX - insert_length
        || udp_length > UINT16_MAX - insert_length)
        return false;
    memmove(frame + insert_offset + insert_length, frame + insert_offset, *frame_length - insert_offset);
    frame[insert_offset++] = DHCP_OPTION_MESSAGE_TYPE;
    frame[insert_offset++] = 1;
    frame[insert_offset++] = 1;
    if (!option_end)
        frame[insert_offset] = DHCP_OPTION_END;
    *frame_length += insert_length;
    ip_length += insert_length;
    udp_length += insert_length;
    dhcp_put_be16(ip + 2, (uint16_t) ip_length);
    dhcp_put_be16(udp + 4, (uint16_t) udp_length);
    dhcp_update_checksums(ip, ip_header_length, udp, (uint16_t) udp_length, old_udp_checksum);
    return true;
}
