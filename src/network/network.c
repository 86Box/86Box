/*
 * VARCem   Virtual ARchaeological Computer EMulator.
 *          An emulator of (mostly) x86-based PC systems and devices,
 *          using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *          spanning the era between 1981 and 1995.
 *
 *          Implementation of the network module.
 *
 * NOTE     The definition of the netcard_t is currently not optimal;
 *          it should be malloc'ed and then linked to the NETCARD def.
 *          Will be done later.
 *
 *
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2017-2019 Fred N. van Kempen.
 *
 *          Redistribution and  use  in source  and binary forms, with
 *          or  without modification, are permitted  provided that the
 *          following conditions are met:
 *
 *          1. Redistributions of  source  code must retain the entire
 *             above notice, this list of conditions and the following
 *             disclaimer.
 *
 *          2. Redistributions in binary form must reproduce the above
 *             copyright  notice,  this list  of  conditions  and  the
 *             following disclaimer in  the documentation and/or other
 *             materials provided with the distribution.
 *
 *          3. Neither the  name of the copyright holder nor the names
 *             of  its  contributors may be used to endorse or promote
 *             products  derived from  this  software without specific
 *             prior written permission.
 *
 * THIS SOFTWARE  IS  PROVIDED BY THE  COPYRIGHT  HOLDERS AND CONTRIBUTORS
 * "AS IS" AND  ANY EXPRESS  OR  IMPLIED  WARRANTIES,  INCLUDING, BUT  NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE  ARE  DISCLAIMED. IN  NO  EVENT  SHALL THE COPYRIGHT
 * HOLDER OR  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL,  EXEMPLARY,  OR  CONSEQUENTIAL  DAMAGES  (INCLUDING,  BUT  NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE  GOODS OR SERVICES;  LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED  AND ON  ANY
 * THEORY OF  LIABILITY, WHETHER IN  CONTRACT, STRICT  LIABILITY, OR  TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING  IN ANY  WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <time.h>
#ifndef _MSC_VER
#include <sys/time.h>
#endif
#include <stdbool.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/ui.h>
#include <86box/timer.h>
#include <86box/network.h>
#include <86box/net_ne2000.h>
#include <86box/net_pcnet.h>
#include <86box/net_wd8003.h>

#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#    include <winsock2.h>
#endif

typedef struct {
    const device_t *device;
} NETWORK_CARD;

static const NETWORK_CARD net_cards[] = {
    // clang-format off
    { &device_none                },
    { &device_internal            },
    { &threec501_device           },
    { &threec503_device           },
    { &pcnet_am79c960_device      },
    { &pcnet_am79c961_device      },
    { &de220p_device              },
    { &ne1000_compat_device       },
    { &ne2000_compat_device       },
    { &ne2000_compat_8bit_device  },
    { &ne1000_device              },
    { &ne2000_device              },
    { &pcnet_am79c960_eb_device   },
    { &rtl8019as_pnp_device       },
    { &wd8003e_device             },
    { &wd8003eb_device            },
    { &wd8013ebt_device           },
    { &plip_device                },
    { &ethernext_mc_device        },
    { &wd8003eta_device           },
    { &wd8003ea_device            },
    { &wd8013epa_device           },
    { &pcnet_am79c973_device      },
    { &pcnet_am79c970a_device     },
    { &dec_tulip_device           },
    { &rtl8029as_device           },
    { &rtl8139c_plus_device       },
    { &dec_tulip_21140_device     },
    { &dec_tulip_21140_vpc_device },
    { &dec_tulip_21040_device     },
    { &pcnet_am79c960_vlb_device  },
    { &modem_device               },
    { NULL                        }
    // clang-format on
};

netcard_conf_t net_cards_conf[NET_CARD_MAX];
uint16_t       net_card_current = 0;

/* Global variables. */
network_devmap_t network_devmap = {0};
int  network_ndev;
netdev_t network_devs[NET_HOST_INTF_MAX];

/* Local variables. */
#ifdef ENABLE_NETWORK_LOG
int             network_do_log = ENABLE_NETWORK_LOG;
static FILE    *network_dump   = NULL;
static mutex_t *network_dump_mutex;

static void
network_log(const char *fmt, ...)
{
    va_list ap;

    if (network_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}

static void
network_dump_packet(netpkt_t *pkt)
{
    if (!network_dump)
        return;

    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct {
        uint32_t ts_sec;
        uint32_t ts_usec;
        uint32_t incl_len;
        uint32_t orig_len;
    } pcap_packet_hdr = {
          .ts_sec = tv.tv_sec,
         .ts_usec = tv.tv_usec,
        .incl_len = pkt->len,
        .orig_len = pkt->len
    };

    if (network_dump_mutex)
        thread_wait_mutex(network_dump_mutex);

    size_t written;
    if ((written = fwrite(&pcap_packet_hdr, 1, sizeof(pcap_packet_hdr), network_dump)) < sizeof(pcap_packet_hdr)) {
        network_log("NETWORK: failed to write dump packet header\n");
        fseek(network_dump, -written, SEEK_CUR);
    } else {
        if ((written = fwrite(pkt->data, 1, pkt->len, network_dump)) < pkt->len) {
            network_log("NETWORK: failed to write dump packet data\n");
            fseek(network_dump, -written - sizeof(pcap_packet_hdr), SEEK_CUR);
        } else {
            fflush(network_dump);
        }
    }

    if (network_dump_mutex)
        thread_release_mutex(network_dump_mutex);
}
#else
#    define network_log(fmt, ...)
#    define network_dump_packet(pkt)
#endif

#ifdef _WIN32
static void
network_winsock_clean(void)
{
    WSACleanup();
}
#endif

/*
 * Initialize the configured network cards.
 *
 * This function gets called only once, from the System
 * Platform initialization code (currently in pc.c) to
 * set our local stuff to a known state.
 */
void
network_init(void)
{
    int i;

#ifdef _WIN32
    WSADATA Data;
    WSAStartup(MAKEWORD(2, 0), &Data);
    atexit(network_winsock_clean);
#endif

    /* Create a first device entry that's always there, as needed by UI. */
    strcpy(network_devs[0].device, "none");
    strcpy(network_devs[0].description, "None");
    network_ndev = 1;

    /* Initialize the Pcap system module, if present. */

    network_devmap.has_slirp = 1;
    i = net_pcap_prepare(&network_devs[network_ndev]);
    if (i > 0) {
        network_devmap.has_pcap = 1;
        network_ndev += i;
    }
    
#ifdef HAS_VDE
    // Try to load the VDE plug library
    if (!net_vde_prepare())
        network_devmap.has_vde = 1;
#endif

#ifdef ENABLE_NETWORK_LOG
    /* Start packet dump. */
    network_dump = fopen("network.pcap", "wb");
    if (network_dump) {
        struct {
            uint32_t magic_number;
            uint16_t version_major;
            uint16_t version_minor;
            int32_t  thiszone;
            uint32_t sigfigs;
            uint32_t snaplen;
            uint32_t network;
        } pcap_hdr = {
             .magic_number = 0xa1b2c3d4,
            .version_major = 2,
            .version_minor = 4,
                 .thiszone = 0,
                  .sigfigs = 0,
                  .snaplen = 65535,
                  .network = 1
        };
        if (fwrite(&pcap_hdr, 1, sizeof(pcap_hdr), network_dump) < sizeof(pcap_hdr)) {
            network_log("NETWORK: failed to write dump header\n");
            fclose(network_dump);
            network_dump = NULL;
        } else {
            fflush(network_dump);
        }
    } else {
        network_log("NETWORK: failed to open dump file\n");
    }
#endif
}

void
network_queue_init(netqueue_t *queue)
{
    queue->head = queue->tail = 0;
    for (int i = 0; i < NET_QUEUE_LEN; i++) {
        queue->packets[i].data = calloc(1, NET_MAX_FRAME);
        queue->packets[i].len  = 0;
    }
}

static bool
network_queue_full(netqueue_t *queue)
{
    return ((queue->head + 1) & NET_QUEUE_LEN_MASK) == queue->tail;
}

static bool
network_queue_empty(netqueue_t *queue)
{
    return (queue->head == queue->tail);
}

static inline void
network_swap_packet(netpkt_t *pkt1, netpkt_t *pkt2)
{
    netpkt_t tmp = *pkt2;
    *pkt2        = *pkt1;
    *pkt1        = tmp;
}

int
network_queue_put(netqueue_t *queue, uint8_t *data, int len)
{
    if (len == 0 || len > NET_MAX_FRAME || network_queue_full(queue)) {
        return 0;
    }

    netpkt_t *pkt = &queue->packets[queue->head];
    memcpy(pkt->data, data, len);
    pkt->len    = len;
    queue->head = (queue->head + 1) & NET_QUEUE_LEN_MASK;
    return 1;
}

int
network_queue_put_swap(netqueue_t *queue, netpkt_t *src_pkt)
{
    if (src_pkt->len == 0 || src_pkt->len > NET_MAX_FRAME || network_queue_full(queue)) {
#ifdef DEBUG
        if (src_pkt->len == 0) {
            network_log("Discarded zero length packet.\n");
        } else if (src_pkt->len > NET_MAX_FRAME) {
            network_log("Discarded oversized packet of len=%d.\n", src_pkt->len);
        } else {
            network_log("Discarded %d bytes packet because the queue is full.\n", src_pkt->len);
        }
#endif
        return 0;
    }

    netpkt_t *dst_pkt = &queue->packets[queue->head];
    network_swap_packet(src_pkt, dst_pkt);

    queue->head = (queue->head + 1) & NET_QUEUE_LEN_MASK;
    return 1;
}

static int
network_queue_get_swap(netqueue_t *queue, netpkt_t *dst_pkt)
{
    if (network_queue_empty(queue))
        return 0;

    netpkt_t *src_pkt = &queue->packets[queue->tail];
    network_swap_packet(src_pkt, dst_pkt);
    queue->tail = (queue->tail + 1) & NET_QUEUE_LEN_MASK;
    return 1;
}

static int
network_queue_move(netqueue_t *dst_q, netqueue_t *src_q)
{
    if (network_queue_empty(src_q))
        return 0;

    if (network_queue_full(dst_q)) {
        return 0;
    }

    netpkt_t *src_pkt = &src_q->packets[src_q->tail];
    netpkt_t *dst_pkt = &dst_q->packets[dst_q->head];

    network_swap_packet(src_pkt, dst_pkt);
    dst_q->head = (dst_q->head + 1) & NET_QUEUE_LEN_MASK;
    src_q->tail = (src_q->tail + 1) & NET_QUEUE_LEN_MASK;

    return dst_pkt->len;
}

void
network_queue_clear(netqueue_t *queue)
{
    for (int i = 0; i < NET_QUEUE_LEN; i++) {
        free(queue->packets[i].data);
        queue->packets[i].len = 0;
    }
    queue->tail = queue->head = 0;
}

static void
network_rx_queue(void *priv)
{
    netcard_t *card = (netcard_t *) priv;

    uint32_t new_link_state = net_cards_conf[card->card_num].link_state;
    if (new_link_state != card->link_state) {
        if (card->set_link_state)
            card->set_link_state(card->card_drv, new_link_state);
        card->link_state = new_link_state;
    }

    uint32_t rx_bytes = 0;
    for (int i = 0; i < NET_QUEUE_LEN; i++) {
        if (card->queued_pkt.len == 0) {
            thread_wait_mutex(card->rx_mutex);
            int res = network_queue_get_swap(&card->queues[NET_QUEUE_RX], &card->queued_pkt);
            thread_release_mutex(card->rx_mutex);
            if (!res)
                break;
        }

        network_dump_packet(&card->queued_pkt);
        int res = card->rx(card->card_drv, card->queued_pkt.data, card->queued_pkt.len);
        if (!res)
            break;
        rx_bytes += card->queued_pkt.len;
        card->queued_pkt.len = 0;
    }

    /* Transmission. */
    uint32_t tx_bytes = 0;
    thread_wait_mutex(card->tx_mutex);
    for (int i = 0; i < NET_QUEUE_LEN; i++) {
        uint32_t bytes = network_queue_move(&card->queues[NET_QUEUE_TX_HOST], &card->queues[NET_QUEUE_TX_VM]);
        if (!bytes)
            break;
        tx_bytes += bytes;
    }
    thread_release_mutex(card->tx_mutex);
    if (tx_bytes) {
        /* Notify host that a packet is available in the TX queue */
        card->host_drv.notify_in(card->host_drv.priv);
    }

    double timer_period = card->byte_period * (rx_bytes > tx_bytes ? rx_bytes : tx_bytes);
    if (timer_period < 200)
        timer_period = 200;

    timer_on_auto(&card->timer, timer_period);

    bool activity = rx_bytes || tx_bytes;
    bool led_on   = card->led_timer & 0x80000000;
    if ((activity && !led_on) || (card->led_timer & 0x7fffffff) >= 150000) {
        ui_sb_update_icon(SB_NETWORK | card->card_num, activity);
        card->led_timer = 0 | (activity << 31);
    }

    card->led_timer += timer_period;
}

/*
 * Attach a network card to the system.
 *
 * This function is called by a hardware driver ("card") after it has
 * finished initializing itself, to link itself to the platform support
 * modules.
 */
netcard_t *
network_attach(void *card_drv, uint8_t *mac, NETRXCB rx, NETSETLINKSTATE set_link_state)
{
    netcard_t *card       = calloc(1, sizeof(netcard_t));
    int net_type          = net_cards_conf[net_card_current].net_type;
    card->queued_pkt.data = calloc(1, NET_MAX_FRAME);
    card->card_drv        = card_drv;
    card->rx              = rx;
    card->set_link_state  = set_link_state;
    card->tx_mutex        = thread_create_mutex();
    card->rx_mutex        = thread_create_mutex();
    card->card_num        = net_card_current;
    card->byte_period     = NET_PERIOD_10M;

    char net_drv_error[NET_DRV_ERRBUF_SIZE];
    wchar_t tempmsg[NET_DRV_ERRBUF_SIZE * 2];

    for (int i = 0; i < NET_QUEUE_COUNT; i++) {
        network_queue_init(&card->queues[i]);
    }

    if ((!strcmp(network_card_get_internal_name(net_cards_conf[net_card_current].device_num), "modem") ||
         !strcmp(network_card_get_internal_name(net_cards_conf[net_card_current].device_num), "plip")) && (net_type >= NET_TYPE_PCAP)) {
        /* Force SLiRP here. Modem and PLIP only operate on non-Ethernet frames. */
        net_type = NET_TYPE_SLIRP;
    }

    switch (net_type) {
        case NET_TYPE_SLIRP:
            card->host_drv      = net_slirp_drv;
            card->host_drv.priv = card->host_drv.init(card, mac, NULL, net_drv_error);
            break;

        case NET_TYPE_PCAP:
            card->host_drv      = net_pcap_drv;
            card->host_drv.priv = card->host_drv.init(card, mac, net_cards_conf[net_card_current].host_dev_name, net_drv_error);
            break;
#ifdef HAS_VDE
        case NET_TYPE_VDE:
            card->host_drv      = net_vde_drv;
            card->host_drv.priv = card->host_drv.init(card, mac, net_cards_conf[net_card_current].host_dev_name, net_drv_error);
            break;
#endif
        default:
            card->host_drv.priv = NULL;
            break;
    }

    // Use null driver on:
    // * No specific driver selected (card->host_drv.priv is set to null above)
    // * Failure to init a specific driver (in which case card->host_drv.priv is null)
    if (!card->host_drv.priv) {

        if(net_cards_conf[net_card_current].net_type != NET_TYPE_NONE) {
            // We're here because of a failure
            swprintf(tempmsg, sizeof_w(tempmsg), L"%ls:<br /><br />%s<br /><br />%ls", plat_get_string(STRING_NET_ERROR), net_drv_error, plat_get_string(STRING_NET_ERROR_DESC));
            ui_msgbox(MBX_ERROR, tempmsg);
            net_cards_conf[net_card_current].net_type = NET_TYPE_NONE;
        }

        // Init null driver
        card->host_drv      = net_null_drv;
        card->host_drv.priv = card->host_drv.init(card, mac, NULL, net_drv_error);
        // Set link state to disconnected by default
        network_connect(card->card_num, 0);
        ui_sb_update_icon_state(SB_NETWORK | card->card_num, 1);

        // If null fails, something is very wrong
        // Clean up and fatal
        if(!card->host_drv.priv) {
            thread_close_mutex(card->tx_mutex);
            thread_close_mutex(card->rx_mutex);
            for (int i = 0; i < NET_QUEUE_COUNT; i++) {
                network_queue_clear(&card->queues[i]);
            }

            free(card->queued_pkt.data);
            free(card);
            // Placeholder - insert the error message
            fatal("Error initializing the network device: Null driver initialization failed\n");
            return NULL;
        }

    }

    timer_add(&card->timer, network_rx_queue, card, 0);
    timer_on_auto(&card->timer, 100);

    return card;
}

void
netcard_close(netcard_t *card)
{
    timer_stop(&card->timer);
    card->host_drv.close(card->host_drv.priv);

    thread_close_mutex(card->tx_mutex);
    thread_close_mutex(card->rx_mutex);
    for (int i = 0; i < NET_QUEUE_COUNT; i++) {
        network_queue_clear(&card->queues[i]);
    }

    free(card->queued_pkt.data);
    free(card);
}

/* Stop any network activity. */
void
network_close(void)
{
#ifdef ENABLE_NETWORK_LOG
    thread_close_mutex(network_dump_mutex);
    network_dump_mutex = NULL;
#endif

    network_log("NETWORK: closed.\n");
}

/*
 * Reset the network card(s).
 *
 * This function is called each time the system is reset,
 * either a hard reset (including power-up) or a soft reset
 * including C-A-D reset.)  It is responsible for connecting
 * everything together.
 */
void
network_reset(void)
{
    ui_sb_update_icon(SB_NETWORK, 0);

#ifdef ENABLE_NETWORK_LOG
    network_dump_mutex = thread_create_mutex();
#endif

    for (uint8_t i = 0; i < NET_CARD_MAX; i++) {
        if (!network_dev_available(i)) {
            continue;
        }

        net_card_current = i;
        if (net_cards_conf[i].device_num > NET_INTERNAL)
            device_add_inst(net_cards[net_cards_conf[i].device_num].device, i + 1);
    }
}

/* Queue a packet for transmission to one of the network providers. */
void
network_tx(netcard_t *card, uint8_t *bufp, int len)
{
    network_queue_put(&card->queues[NET_QUEUE_TX_VM], bufp, len);
}

int
network_tx_pop(netcard_t *card, netpkt_t *out_pkt)
{
    int ret = 0;

    thread_wait_mutex(card->tx_mutex);
    ret = network_queue_get_swap(&card->queues[NET_QUEUE_TX_HOST], out_pkt);
    thread_release_mutex(card->tx_mutex);

    return ret;
}

int
network_tx_popv(netcard_t *card, netpkt_t *pkt_vec, int vec_size)
{
    int pkt_count = 0;

    netqueue_t *queue = &card->queues[NET_QUEUE_TX_HOST];
    thread_wait_mutex(card->tx_mutex);
    for (int i = 0; i < vec_size; i++) {
        if (!network_queue_get_swap(queue, pkt_vec))
            break;
        network_dump_packet(pkt_vec);
        pkt_count++;
        pkt_vec++;
    }
    thread_release_mutex(card->tx_mutex);

    return pkt_count;
}

int
network_rx_put(netcard_t *card, uint8_t *bufp, int len)
{
    int ret = 0;

    thread_wait_mutex(card->rx_mutex);
    ret = network_queue_put(&card->queues[NET_QUEUE_RX], bufp, len);
    thread_release_mutex(card->rx_mutex);

    return ret;
}

int
network_rx_put_pkt(netcard_t *card, netpkt_t *pkt)
{
    int ret = 0;

    thread_wait_mutex(card->rx_mutex);
    ret = network_queue_put_swap(&card->queues[NET_QUEUE_RX], pkt);
    thread_release_mutex(card->rx_mutex);

    return ret;
}

void
network_connect(int id, int connect)
{
    if (id >= NET_CARD_MAX)
        return;

    if (connect) {
        net_cards_conf[id].link_state &= ~NET_LINK_DOWN;
    } else {
        net_cards_conf[id].link_state |= NET_LINK_DOWN;
    }
}

int
network_is_connected(int id)
{
    if (id >= NET_CARD_MAX)
        return 0;

    return !(net_cards_conf[id].link_state & NET_LINK_DOWN);
}

int
network_dev_to_id(char *devname)
{
    for (int i = 0; i < network_ndev; i++) {
        if (!strcmp((char *) network_devs[i].device, devname)) {
            return i;
        }
    }

    return -1;
}

/* UI */
int
network_dev_available(int id)
{
    int available = (net_cards_conf[id].device_num > 0);

    if ((net_cards_conf[id].net_type == NET_TYPE_PCAP) &&
        (network_dev_to_id(net_cards_conf[id].host_dev_name) <= 0))
        available = 0;

    // TODO: Handle VDE device

    return available;
}

int
network_available(void)
{
    int available = 0;

    for (int i = 0; i < NET_CARD_MAX; i++) {
        available |= network_dev_available(i);
    }

    return available;
}

/* UI */
int
network_card_available(int card)
{
    if (net_cards[card].device)
        return (device_available(net_cards[card].device));

    return 1;
}

/* UI */
const device_t *
network_card_getdevice(int card)
{
    return (net_cards[card].device);
}

/* UI */
int
network_card_has_config(int card)
{
    if (!net_cards[card].device)
        return 0;

    return (device_has_config(net_cards[card].device) ? 1 : 0);
}

/* UI */
const char *
network_card_get_internal_name(int card)
{
    return device_get_internal_name(net_cards[card].device);
}

/* UI */
int
network_card_get_from_internal_name(char *s)
{
    int c = 0;

    while (net_cards[c].device != NULL) {
        if (!strcmp(net_cards[c].device->internal_name, s))
            return c;
        c++;
    }

    return 0;
}
