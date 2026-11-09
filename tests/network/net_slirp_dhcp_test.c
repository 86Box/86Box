#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "net_slirp_dhcp.h"

#define ETHERNET_HEADER_LENGTH 14
#define IPV4_HEADER_LENGTH 20
#define UDP_HEADER_LENGTH 8
#define DHCP_OPTIONS_OFFSET 240

static uint16_t
get_be16(const uint8_t *data)
{
    return (uint16_t) (((uint16_t) data[0] << 8) | data[1]);
}

static void
put_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t) (value >> 8);
    data[1] = (uint8_t) value;
}

static uint16_t
checksum(uint32_t sum, const uint8_t *data, size_t length)
{
    for (size_t i = 0; i + 1 < length; i += 2)
        sum += get_be16(data + i);
    if (length & 1)
        sum += (uint32_t) data[length - 1] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t) ~sum;
}

static uint32_t
pseudo_header_sum(const uint8_t *ip, uint16_t udp_length)
{
    return get_be16(ip + 12) + get_be16(ip + 14) + get_be16(ip + 16) + get_be16(ip + 18) + 17 + udp_length;
}

static size_t
make_reply(uint8_t *frame, uint8_t bootp_op, uint8_t message_type, bool udp_checksum, unsigned existing_options)
{
    uint8_t *ip = frame + ETHERNET_HEADER_LENGTH;
    uint8_t *udp = ip + IPV4_HEADER_LENGTH;
    uint8_t *bootp = udp + UDP_HEADER_LENGTH;
    uint8_t *options = bootp + DHCP_OPTIONS_OFFSET;
    size_t   options_length = 0;
    uint16_t udp_length;
    uint16_t ip_length;

    memset(frame, 0, 512);
    put_be16(frame + 12, 0x0800);
    ip[0] = 0x45;
    ip[8] = 64;
    ip[9] = 17;
    ip[12] = 10;
    ip[15] = 2;
    ip[16] = 255;
    ip[17] = 255;
    ip[18] = 255;
    ip[19] = 255;
    put_be16(udp, 67);
    put_be16(udp + 2, 68);
    bootp[0] = bootp_op;
    memcpy(bootp + 236, "\x63\x82\x53\x63", 4);
    options[options_length++] = 53;
    options[options_length++] = 1;
    options[options_length++] = message_type;
    if (existing_options & 1) {
        options[options_length++] = 44;
        options[options_length++] = 4;
        options[options_length++] = 10;
        options[options_length++] = 0;
        options[options_length++] = 2;
        options[options_length++] = 4;
    }
    if (existing_options & 2) {
        options[options_length++] = 46;
        options[options_length++] = 1;
        options[options_length++] = 8;
    }
    options[options_length++] = 255;

    udp_length = (uint16_t) (UDP_HEADER_LENGTH + DHCP_OPTIONS_OFFSET + options_length);
    ip_length = (uint16_t) (IPV4_HEADER_LENGTH + udp_length);
    put_be16(udp + 4, udp_length);
    put_be16(ip + 2, ip_length);
    put_be16(ip + 10, checksum(0, ip, IPV4_HEADER_LENGTH));
    if (udp_checksum) {
        uint16_t value = checksum(pseudo_header_sum(ip, udp_length), udp, udp_length);
        put_be16(udp + 6, value ? value : 0xFFFF);
    }
    return ETHERNET_HEADER_LENGTH + ip_length;
}

static size_t
make_bootp_request(uint8_t *frame, size_t bootp_length, bool udp_checksum)
{
    uint8_t *ip = frame + ETHERNET_HEADER_LENGTH;
    uint8_t *udp = ip + IPV4_HEADER_LENGTH;
    uint8_t *bootp = udp + UDP_HEADER_LENGTH;
    uint16_t udp_length = (uint16_t) (UDP_HEADER_LENGTH + bootp_length);
    uint16_t ip_length = (uint16_t) (IPV4_HEADER_LENGTH + udp_length);

    memset(frame, 0, 512);
    put_be16(frame + 12, 0x0800);
    ip[0] = 0x45;
    ip[8] = 64;
    ip[9] = 17;
    ip[16] = 255;
    ip[17] = 255;
    ip[18] = 255;
    ip[19] = 255;
    put_be16(udp, 68);
    put_be16(udp + 2, 67);
    put_be16(udp + 4, udp_length);
    bootp[0] = 1;
    bootp[1] = 1;
    bootp[2] = 6;
    put_be16(ip + 2, ip_length);
    put_be16(ip + 10, checksum(0, ip, IPV4_HEADER_LENGTH));
    if (udp_checksum) {
        uint16_t value = checksum(pseudo_header_sum(ip, udp_length), udp, udp_length);
        put_be16(udp + 6, value ? value : 0xFFFF);
    }
    return ETHERNET_HEADER_LENGTH + ip_length;
}

static size_t
count_option(const uint8_t *frame, uint8_t wanted, const uint8_t **value, uint8_t *length)
{
    const uint8_t *ip = frame + ETHERNET_HEADER_LENGTH;
    const uint8_t *udp = ip + IPV4_HEADER_LENGTH;
    const uint8_t *options = udp + UDP_HEADER_LENGTH + DHCP_OPTIONS_OFFSET;
    size_t         remaining = get_be16(udp + 4) - UDP_HEADER_LENGTH - DHCP_OPTIONS_OFFSET;
    size_t         count = 0;

    for (size_t i = 0; i < remaining;) {
        uint8_t option = options[i++];
        if (option == 0)
            continue;
        if (option == 255)
            break;
        uint8_t option_length = options[i++];
        if (option == wanted) {
            count++;
            if (value)
                *value = options + i;
            if (length)
                *length = option_length;
        }
        i += option_length;
    }
    return count;
}

static void
test_offer_and_ack(bool udp_checksum, uint8_t message_type)
{
    uint8_t      frame[512];
    size_t       length = make_reply(frame, 2, message_type, udp_checksum, false);
    const size_t old_length = length;
    const uint8_t *option_value;

    assert(net_slirp_dhcp_add_wins_options(frame, &length, sizeof(frame), 0x0A000204));
    assert(length == old_length + 9);
    assert(get_be16(frame + ETHERNET_HEADER_LENGTH + 2) == old_length - ETHERNET_HEADER_LENGTH + 9);
    const uint8_t *ip = frame + ETHERNET_HEADER_LENGTH;
    const uint8_t *udp = ip + IPV4_HEADER_LENGTH;
    assert(get_be16(udp + 4) == length - ETHERNET_HEADER_LENGTH - IPV4_HEADER_LENGTH);
    assert(count_option(frame, 44, &option_value, NULL) == 1);
    assert(option_value[0] == 10 && option_value[1] == 0 && option_value[2] == 2 && option_value[3] == 4);
    assert(count_option(frame, 46, &option_value, NULL) == 1);
    assert(option_value[0] == 8);
    assert(checksum(0, frame + ETHERNET_HEADER_LENGTH, IPV4_HEADER_LENGTH) == 0);
    if (udp_checksum)
        assert(checksum(pseudo_header_sum(frame + ETHERNET_HEADER_LENGTH, get_be16(udp + 4)), udp,
                        get_be16(udp + 4)) == 0);
    else
        assert(get_be16(udp + 6) == 0);
}

static void
test_ignored_packets_are_unchanged(void)
{
    uint8_t frame[512];
    uint8_t copy[512];
    size_t  length = make_reply(frame, 1, 1, true, false);

    memcpy(copy, frame, sizeof(frame));
    assert(!net_slirp_dhcp_add_wins_options(frame, &length, sizeof(frame), 0x0A000204));
    assert(memcmp(frame, copy, sizeof(frame)) == 0);

    length = make_reply(frame, 2, 6, true, false);
    memcpy(copy, frame, sizeof(frame));
    assert(!net_slirp_dhcp_add_wins_options(frame, &length, sizeof(frame), 0x0A000204));
    assert(memcmp(frame, copy, sizeof(frame)) == 0);

    length = make_reply(frame, 2, 2, false, 3);
    memcpy(copy, frame, sizeof(frame));
    assert(!net_slirp_dhcp_add_wins_options(frame, &length, sizeof(frame), 0x0A000204));
    assert(memcmp(frame, copy, sizeof(frame)) == 0);
}

static void
test_adds_only_missing_option(void)
{
    uint8_t      frame[512];
    const size_t length = make_reply(frame, 2, 5, false, 1);
    size_t       new_length = length;

    assert(net_slirp_dhcp_add_wins_options(frame, &new_length, sizeof(frame), 0x0A000204));
    assert(new_length == length + 3);
    assert(count_option(frame, 44, NULL, NULL) == 1);
    assert(count_option(frame, 46, NULL, NULL) == 1);
}

static void
test_adds_tftp_boot_options(void)
{
    uint8_t       frame[512];
    const char    boot_file[] = "boot.img";
    const char    tftp_server[] = "10.0.2.4";
    size_t        length = make_reply(frame, 2, 2, true, false);
    const size_t  old_length = length;
    const uint8_t *option_value;
    uint8_t        option_length;
    uint8_t       *bootp;
    uint8_t       *udp;

    assert(net_slirp_dhcp_add_vserver_options(frame, &length, sizeof(frame), 0x0A000204, true, true,
                                              boot_file));
    assert(length == old_length + sizeof(boot_file) + sizeof(tftp_server) + 11);
    bootp = frame + ETHERNET_HEADER_LENGTH + IPV4_HEADER_LENGTH + UDP_HEADER_LENGTH;
    assert(bootp[20] == 10 && bootp[21] == 0 && bootp[22] == 2 && bootp[23] == 4);
    assert(strcmp((const char *) bootp + 108, boot_file) == 0);
    assert(count_option(frame, 44, &option_value, NULL) == 1);
    assert(count_option(frame, 46, &option_value, NULL) == 1);
    assert(count_option(frame, 66, &option_value, &option_length) == 1);
    assert(option_length == sizeof(tftp_server) - 1);
    assert(memcmp(option_value, tftp_server, sizeof(tftp_server) - 1) == 0);
    assert(count_option(frame, 67, &option_value, &option_length) == 1);
    assert(option_length == sizeof(boot_file) - 1);
    assert(memcmp(option_value, boot_file, sizeof(boot_file) - 1) == 0);

    udp = frame + ETHERNET_HEADER_LENGTH + IPV4_HEADER_LENGTH;
    assert(checksum(pseudo_header_sum(frame + ETHERNET_HEADER_LENGTH, get_be16(udp + 4)), udp,
                    get_be16(udp + 4)) == 0);
    assert(!net_slirp_dhcp_add_vserver_options(frame, &length, sizeof(frame), 0x0A000204, true, true,
                                               boot_file));
}

static void
test_bootp_requests_are_forwarded_as_dhcp_discover(void)
{
    uint8_t      frame[512];
    size_t       length = make_bootp_request(frame, 236, true);
    const size_t old_length = length;
    uint8_t     *bootp;
    uint8_t     *udp;

    assert(net_slirp_dhcp_prepare_bootp_request(frame, &length, sizeof(frame)));
    assert(length == old_length + 8);
    bootp = frame + ETHERNET_HEADER_LENGTH + IPV4_HEADER_LENGTH + UDP_HEADER_LENGTH;
    assert(bootp[0] == 1);
    assert(memcmp(bootp + 236, "\x63\x82\x53\x63", 4) == 0);
    assert(bootp[240] == 53 && bootp[241] == 1 && bootp[242] == 1 && bootp[243] == 255);
    udp = frame + ETHERNET_HEADER_LENGTH + IPV4_HEADER_LENGTH;
    assert(checksum(pseudo_header_sum(frame + ETHERNET_HEADER_LENGTH, get_be16(udp + 4)), udp,
                    get_be16(udp + 4)) == 0);
    assert(!net_slirp_dhcp_prepare_bootp_request(frame, &length, sizeof(frame)));

    length = make_bootp_request(frame, 300, false);
    assert(net_slirp_dhcp_prepare_bootp_request(frame, &length, sizeof(frame)));
    assert(length == ETHERNET_HEADER_LENGTH + IPV4_HEADER_LENGTH + UDP_HEADER_LENGTH + 300);
    bootp = frame + ETHERNET_HEADER_LENGTH + IPV4_HEADER_LENGTH + UDP_HEADER_LENGTH;
    assert(memcmp(bootp + 236, "\x63\x82\x53\x63", 4) == 0);
    assert(bootp[240] == 53 && bootp[241] == 1 && bootp[242] == 1 && bootp[243] == 255);
}

static void
test_capacity_and_malformed_options(void)
{
    uint8_t frame[512];
    uint8_t copy[512];
    size_t  length = make_reply(frame, 2, 2, false, false);
    size_t  original_length = length;

    memcpy(copy, frame, sizeof(frame));
    assert(!net_slirp_dhcp_add_wins_options(frame, &length, length + 8, 0x0A000204));
    assert(length == original_length);
    assert(memcmp(frame, copy, sizeof(frame)) == 0);

    length = make_reply(frame, 2, 2, false, false);
    frame[length - 1] = 54; /* option without a length */
    memcpy(copy, frame, sizeof(frame));
    assert(!net_slirp_dhcp_add_wins_options(frame, &length, sizeof(frame), 0x0A000204));
    assert(length == original_length);
    assert(memcmp(frame, copy, sizeof(frame)) == 0);
}

int
main(void)
{
    test_offer_and_ack(false, 2);
    test_offer_and_ack(false, 5);
    test_offer_and_ack(true, 2);
    test_offer_and_ack(true, 5);
    test_ignored_packets_are_unchanged();
    test_adds_only_missing_option();
    test_adds_tftp_boot_options();
    test_bootp_requests_are_forwarded_as_dhcp_discover();
    test_capacity_and_malformed_options();
    return 0;
}
