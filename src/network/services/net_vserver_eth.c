/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Adapts the virtual server to Ethernet frames for network guests.
 *
 *          The server speaks IP, LLC and IPX, so all this adds is what a
 *          segment needs on top: an address of its own, answers to ARP, a
 *          guest's broadcast name queries for clients that do not use WINS,
 *          and whichever of IPX's four framings the guest picked.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <86box/network/net_ipx.h>
#include <86box/network/net_nbf.h>
#include <86box/network/net_vserver.h>
#include <86box/network/net_vserver_eth.h>

#define ETH_HEADER_LENGTH 14
#define ETH_MIN_FRAME     60
#define ETH_MAX_FRAME     1514
#define ETH_ADDRESS       6
#define ETH_MAX_LENGTH    1500 /* a larger type field is an EtherType, not an 802.3 length */
#define ETHERTYPE_IPV4    0x0800
#define ETHERTYPE_ARP     0x0806
#define ETHERTYPE_IPX     0x8137

#define ARP_LENGTH        28
#define ARP_REQUEST       1
#define ARP_REPLY         2
#define LLC_SAP_NETBIOS   0xF0
#define LLC_SAP_IPX       0xE0
#define LLC_SAP_SNAP      0xAA
#define LLC_UI            0x03
#define LLC_IPX_LENGTH    3
#define SNAP_IPX_LENGTH   8

typedef enum eth_framing_t {
    ETH_FRAMING_802_2, /* what a guest falls back on with nothing to detect */
    ETH_FRAMING_802_3,
    ETH_FRAMING_II,
    ETH_FRAMING_SNAP
} eth_framing_t;

struct vserver_eth_t {
    vserver_t           *server;
    nbf_t               *nbf;
    ipx_t               *ipx;
    eth_framing_t        ipx_framing;
    vserver_eth_output_t output;
    void                *priv;
    uint32_t             server_ip;
    uint8_t              address[ETH_ADDRESS];
    uint8_t              guest[ETH_ADDRESS];
    bool                 guest_known;
};

static const uint8_t netbios_functional[ETH_ADDRESS] = { 0x03, 0x00, 0x00, 0x00, 0x00, 0x01 };
static const uint8_t broadcast_address[ETH_ADDRESS]  = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static const uint8_t llc_ipx[LLC_IPX_LENGTH]         = { LLC_SAP_IPX, LLC_SAP_IPX, LLC_UI };
static const uint8_t snap_ipx[SNAP_IPX_LENGTH]       = { LLC_SAP_SNAP, LLC_SAP_SNAP, LLC_UI, 0, 0, 0, 0x81, 0x37 };

static uint16_t
eth_get16(const uint8_t *p)
{
    return (uint16_t) ((p[0] << 8) | p[1]);
}

static void
eth_put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t) (value >> 8);
    p[1] = (uint8_t) value;
}

static uint32_t
eth_get32(const uint8_t *p)
{
    return ((uint32_t) eth_get16(p) << 16) | eth_get16(p + 2);
}

static void
eth_send(vserver_eth_t *eth, uint16_t type, const uint8_t *data, int length)
{
    uint8_t frame[ETH_MAX_FRAME];
    int     total = ETH_HEADER_LENGTH + length;

    if (!eth->guest_known || total > ETH_MAX_FRAME)
        return;
    memcpy(frame, eth->guest, ETH_ADDRESS);
    memcpy(frame + ETH_ADDRESS, eth->address, ETH_ADDRESS);
    eth_put16(frame + 12, type);
    memcpy(frame + ETH_HEADER_LENGTH, data, (size_t) length);
    if (total < ETH_MIN_FRAME) {
        memset(frame + total, 0, (size_t) (ETH_MIN_FRAME - total));
        total = ETH_MIN_FRAME;
    }
    eth->output(eth->priv, frame, total);
}

static void
eth_ip_output(void *priv, const uint8_t *ip_pkt, int len)
{
    eth_send((vserver_eth_t *) priv, ETHERTYPE_IPV4, ip_pkt, len);
}

static void
eth_llc_output(void *priv, const uint8_t *frame, int len)
{
    eth_send((vserver_eth_t *) priv, (uint16_t) len, frame, len);
}

/* Answers in the framing the guest last used, as it may ignore the other three. */
static void
eth_ipx_output(void *priv, const uint8_t *packet, int len)
{
    vserver_eth_t *eth = (vserver_eth_t *) priv;
    uint8_t        data[ETH_MAX_LENGTH];
    int            header = 0;

    switch (eth->ipx_framing) {
        case ETH_FRAMING_II:
            eth_send(eth, ETHERTYPE_IPX, packet, len);
            return;
        case ETH_FRAMING_802_2:
            memcpy(data, llc_ipx, LLC_IPX_LENGTH);
            header = LLC_IPX_LENGTH;
            break;
        case ETH_FRAMING_SNAP:
            memcpy(data, snap_ipx, SNAP_IPX_LENGTH);
            header = SNAP_IPX_LENGTH;
            break;
        default:
            break;
    }
    if (header + len > ETH_MAX_LENGTH)
        return;
    memcpy(data + header, packet, (size_t) len);
    eth_send(eth, (uint16_t) (header + len), data, header + len);
}

static bool
eth_ipx_input(vserver_eth_t *eth, eth_framing_t framing, const uint8_t *packet, int len)
{
    static const int overhead[] = { LLC_IPX_LENGTH, 0, 0, SNAP_IPX_LENGTH };

    eth->ipx_framing = framing;
    ipx_configure(eth->ipx, IPX_NETWORK, eth->address, ETH_MAX_LENGTH - overhead[framing]);
    return ipx_input(eth->ipx, packet, len);
}

static bool
eth_handle_arp(vserver_eth_t *eth, const uint8_t *arp, int len)
{
    uint8_t reply[ARP_LENGTH];

    if (len < ARP_LENGTH || eth_get16(arp) != 1 || eth_get16(arp + 2) != ETHERTYPE_IPV4 || arp[4] != ETH_ADDRESS
        || arp[5] != 4 || eth_get16(arp + 6) != ARP_REQUEST || eth_get32(arp + 24) != eth->server_ip)
        return false;

    memcpy(reply, arp, 6);
    eth_put16(reply + 6, ARP_REPLY);
    memcpy(reply + 8, eth->address, ETH_ADDRESS);
    memcpy(reply + 14, arp + 24, 4);
    memcpy(reply + 18, arp + 8, 10);
    eth_send(eth, ETHERTYPE_ARP, reply, ARP_LENGTH);
    return true;
}

bool
vserver_eth_input(vserver_eth_t *eth, const uint8_t *frame, int len)
{
    uint16_t type;
    int      payload = len - ETH_HEADER_LENGTH;

    if (!eth || !frame || payload < 1)
        return false;
    memcpy(eth->guest, frame + ETH_ADDRESS, ETH_ADDRESS);
    eth->guest_known = true;
    type             = eth_get16(frame + 12);

    if (type <= ETH_MAX_LENGTH) {
        const uint8_t *llc = frame + ETH_HEADER_LENGTH;

        /* 802.3 pads short frames, so only the length field says where the data ends. */
        if (type > payload)
            return false;
        if (eth->ipx && type >= 2) {
            if (llc[0] == 0xFF && llc[1] == 0xFF)
                return eth_ipx_input(eth, ETH_FRAMING_802_3, llc, type);
            if (type > LLC_IPX_LENGTH && !memcmp(llc, llc_ipx, LLC_IPX_LENGTH))
                return eth_ipx_input(eth, ETH_FRAMING_802_2, llc + LLC_IPX_LENGTH, type - LLC_IPX_LENGTH);
            if (type > SNAP_IPX_LENGTH && !memcmp(llc, snap_ipx, SNAP_IPX_LENGTH))
                return eth_ipx_input(eth, ETH_FRAMING_SNAP, llc + SNAP_IPX_LENGTH, type - SNAP_IPX_LENGTH);
        }
        if (!eth->nbf || llc[0] != LLC_SAP_NETBIOS
            || (memcmp(frame, eth->address, ETH_ADDRESS) && memcmp(frame, netbios_functional, ETH_ADDRESS)
                && memcmp(frame, broadcast_address, ETH_ADDRESS)))
            return false;
        nbf_input(eth->nbf, frame + ETH_HEADER_LENGTH, type);
        return true;
    }

    if (type == ETHERTYPE_IPX && eth->ipx)
        return eth_ipx_input(eth, ETH_FRAMING_II, frame + ETH_HEADER_LENGTH, payload);
    if (type == ETHERTYPE_ARP)
        return eth_handle_arp(eth, frame + ETH_HEADER_LENGTH, payload);
    if (type == ETHERTYPE_IPV4)
        return vserver_process_ipv4(eth->server, frame + ETH_HEADER_LENGTH, payload);
    return false;
}

vserver_t *
vserver_eth_server(vserver_eth_t *eth)
{
    return eth ? eth->server : NULL;
}

void
vserver_eth_tick(vserver_eth_t *eth)
{
    if (!eth)
        return;
    vserver_tick(eth->server);
    nbf_tick(eth->nbf);
    ipx_tick(eth->ipx);
}

vserver_eth_t *
vserver_eth_init(void *log, uint32_t server_ip, bool wins, bool smb, bool ftp, bool tftp, bool netbeui, bool ipx,
                 vserver_eth_output_t output, void *priv)
{
    vserver_eth_t *eth = (vserver_eth_t *) calloc(1, sizeof(vserver_eth_t));

    if (!eth)
        return NULL;
    eth->output    = output;
    eth->priv      = priv;
    eth->server_ip = server_ip;

    /* In SLiRP's locally administered range, ending in the IP address like its own. */
    eth->address[0] = 0x52;
    eth->address[1] = 0x55;
    eth->address[2] = (uint8_t) (server_ip >> 24);
    eth->address[3] = (uint8_t) (server_ip >> 16);
    eth->address[4] = (uint8_t) (server_ip >> 8);
    eth->address[5] = (uint8_t) server_ip;

    eth->server = vserver_init(log, server_ip, wins, smb, ftp, tftp, eth_ip_output, eth);
    if (!eth->server) {
        free(eth);
        return NULL;
    }
    if (wins)
        vserver_answer_broadcasts(eth->server);
    if (netbeui)
        eth->nbf = nbf_init(log, eth_llc_output, eth);
    if (ipx)
        eth->ipx = ipx_init(log, eth->address, eth_ipx_output, eth);
    return eth;
}

void
vserver_eth_close(vserver_eth_t *eth)
{
    if (!eth)
        return;
    nbf_close(eth->nbf);
    ipx_close(eth->ipx);
    vserver_close(eth->server);
    free(eth);
}
