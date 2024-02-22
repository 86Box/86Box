/*
 * VARCem   Virtual ARchaeological Computer EMulator.
 *          An emulator of (mostly) x86-based PC systems and devices,
 *          using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *          spanning the era between 1981 and 1995.
 *
 *          Definitions for the network module.
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

#ifndef EMU_NETWORK_H
#define EMU_NETWORK_H
#include <stdint.h>

/* Network provider types. */
#define NET_TYPE_NONE  0 /* use the null network driver */
#define NET_TYPE_SLIRP 1 /* use the SLiRP port forwarder */
#define NET_TYPE_PCAP  2 /* use the (Win)Pcap API */
#define NET_TYPE_VDE   3 /* use the VDE plug API */

#define NET_MAX_FRAME  1518
/* Queue size must be a power of 2 */
#define NET_QUEUE_LEN      16
#define NET_QUEUE_LEN_MASK (NET_QUEUE_LEN - 1)
#define NET_QUEUE_COUNT 3
#define NET_CARD_MAX       4
#define NET_HOST_INTF_MAX  64

#define NET_PERIOD_10M     0.8
#define NET_PERIOD_100M    0.08

/* Error buffers for network driver init */
#define NET_DRV_ERRBUF_SIZE 384

enum {
    NET_LINK_DOWN      = (1 << 1),
    NET_LINK_TEMP_DOWN = (1 << 2),
    NET_LINK_10_HD     = (1 << 3),
    NET_LINK_10_FD     = (1 << 4),
    NET_LINK_100_HD    = (1 << 5),
    NET_LINK_100_FD    = (1 << 6),
    NET_LINK_1000_HD   = (1 << 7),
    NET_LINK_1000_FD   = (1 << 8),
};

enum {
    NET_NONE = 0,
    NET_INTERNAL
};

enum {
    NET_QUEUE_RX      = 0,
    NET_QUEUE_TX_VM   = 1,
    NET_QUEUE_TX_HOST = 2
};

typedef struct netcard_conf_t {
    uint16_t device_num;
    int      net_type;
    char     host_dev_name[128];
    uint32_t link_state;
} netcard_conf_t;

extern netcard_conf_t net_cards_conf[NET_CARD_MAX];
extern uint16_t       net_card_current;

typedef int (*NETRXCB)(void *, uint8_t *, int);
typedef int (*NETSETLINKSTATE)(void *, uint32_t link_state);

typedef struct netpkt {
    uint8_t *data;
    int      len;
} netpkt_t;

typedef struct netqueue_t {
    netpkt_t packets[NET_QUEUE_LEN];
    int      head;
    int      tail;
} netqueue_t;

typedef struct _netcard_t netcard_t;

typedef struct netdrv_t {
    void (*notify_in)(void *priv);
    void *(*init)(const netcard_t *card, const uint8_t *mac_addr, void *priv, char *netdrv_errbuf);
    void (*close)(void *priv);
    void *priv;
} netdrv_t;

extern const netdrv_t net_pcap_drv;
extern const netdrv_t net_slirp_drv;
extern const netdrv_t net_vde_drv;
extern const netdrv_t net_null_drv;

struct _netcard_t {
    const device_t *device;
    void           *card_drv;
    struct netdrv_t host_drv;
    NETRXCB         rx;
    NETSETLINKSTATE set_link_state;
    netqueue_t      queues[NET_QUEUE_COUNT];
    netpkt_t        queued_pkt;
    mutex_t        *tx_mutex;
    mutex_t        *rx_mutex;
    pc_timer_t      timer;
    uint16_t        card_num;
    double          byte_period;
    uint32_t        led_timer;
    uint32_t        led_state;
    uint32_t        link_state;
};

typedef struct {
    char device[128];
    char description[128];
} netdev_t;

typedef struct {
    int has_slirp;
    int has_pcap;
    int has_vde;
} network_devmap_t;


#define HAS_NOSLIRP_NET(x)  (x.has_pcap || x.has_vde)

#ifdef __cplusplus
extern "C" {
#endif

/* Global variables. */
extern int              nic_do_log;     // config
extern network_devmap_t network_devmap;
extern int              network_ndev;   // Number of pcap devices
extern network_devmap_t network_devmap; // Bitmap of available network types
extern netdev_t         network_devs[NET_HOST_INTF_MAX];


/* Function prototypes. */
extern void       network_init(void);
extern netcard_t *network_attach(void *card_drv, uint8_t *mac, NETRXCB rx, NETSETLINKSTATE set_link_state);
extern void       netcard_close(netcard_t *card);
extern void       network_close(void);
extern void       network_reset(void);
extern int        network_available(void);
extern void       network_tx(netcard_t *card, uint8_t *, int);

extern int net_pcap_prepare(netdev_t *);
extern int net_vde_prepare(void);


extern void            network_connect(int id, int connect);
extern int             network_is_connected(int id);
extern int             network_dev_available(int);
extern int             network_dev_to_id(char *);
extern int             network_card_available(int);
extern int             network_card_has_config(int);
extern const char     *network_card_get_internal_name(int);
extern int             network_card_get_from_internal_name(char *);
#ifdef EMU_DEVICE_H
extern const device_t *network_card_getdevice(int);
#endif

extern int network_tx_pop(netcard_t *card, netpkt_t *out_pkt);
extern int network_tx_popv(netcard_t *card, netpkt_t *pkt_vec, int vec_size);
extern int network_rx_put(netcard_t *card, uint8_t *bufp, int len);
extern int network_rx_put_pkt(netcard_t *card, netpkt_t *pkt);

#ifdef EMU_DEVICE_H
/* 3Com Etherlink */
extern const device_t threec501_device;
extern const device_t threec503_device;

/* Novell NE2000 and compatibles */
extern const device_t ne1000_device;
extern const device_t ne1000_compat_device;
extern const device_t ne2000_device;
extern const device_t ne2000_compat_device;
extern const device_t ethernext_mc_device;
extern const device_t rtl8019as_device;
extern const device_t de220p_device;
extern const device_t rtl8029as_device;

/* AMD PCnet*/
extern const device_t pcnet_am79c960_device;
extern const device_t pcnet_am79c960_eb_device;
extern const device_t pcnet_am79c960_vlb_device;
extern const device_t pcnet_am79c961_device;
extern const device_t pcnet_am79c970a_device;
extern const device_t pcnet_am79c973_device;
extern const device_t pcnet_am79c973_onboard_device;

/* PLIP */
#ifdef EMU_LPT_H
extern const lpt_device_t lpt_plip_device;
#endif
extern const device_t     plip_device;

/* Realtek RTL8139C+ */
extern const device_t rtl8139c_plus_device;

/* DEC Tulip */
extern const device_t dec_tulip_device;
extern const device_t dec_tulip_21140_device;
extern const device_t dec_tulip_21140_vpc_device;
extern const device_t dec_tulip_21040_device;

/* WD 80x3 */
extern const device_t wd8003e_device;
extern const device_t wd8003eb_device;
extern const device_t wd8013ebt_device;
extern const device_t wd8003eta_device;
extern const device_t wd8003ea_device;
extern const device_t wd8013epa_device;
#endif

#ifdef __cplusplus
}
#endif

#endif /*EMU_NETWORK_H*/
