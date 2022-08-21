/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Definitions for the network module.
 *
 *
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2017-2019 Fred N. van Kempen.
 *
 *		Redistribution and  use  in source  and binary forms, with
 *		or  without modification, are permitted  provided that the
 *		following conditions are met:
 *
 *		1. Redistributions of  source  code must retain the entire
 *		   above notice, this list of conditions and the following
 *		   disclaimer.
 *
 *		2. Redistributions in binary form must reproduce the above
 *		   copyright  notice,  this list  of  conditions  and  the
 *		   following disclaimer in  the documentation and/or other
 *		   materials provided with the distribution.
 *
 *		3. Neither the  name of the copyright holder nor the names
 *		   of  its  contributors may be used to endorse or promote
 *		   products  derived from  this  software without specific
 *		   prior written permission.
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
# define EMU_NETWORK_H
# include <stdint.h>


/* Network provider types. */
#define NET_TYPE_NONE	0		/* networking disabled */
#define NET_TYPE_SLIRP	1		/* use the SLiRP port forwarder */
#define NET_TYPE_PCAP   2       /* use the (Win)Pcap API */

#define NET_MAX_FRAME 1518
#define NET_QUEUE_LEN 8
#define NET_CARD_MAX 4

/* Supported network cards. */
enum {
    NONE = 0,
    NE1000,
    NE2000,
    RTL8019AS,
    RTL8029AS
};

enum {
    NET_QUEUE_RX,
    NET_QUEUE_TX_VM,
    NET_QUEUE_TX_HOST
};

typedef struct {
    int device_num;
    int net_type;
    char host_dev_name[128];
} netcard_conf_t;

extern netcard_conf_t net_cards_conf[NET_CARD_MAX];
extern int net_card_current;

typedef int (*NETRXCB)(void *, uint8_t *, int);
typedef int (*NETWAITCB)(void *);
typedef int (*NETSETLINKSTATE)(void *);


typedef struct netpkt {
    uint8_t		*data;
    int			len;
    uint64_t tsc;
} netpkt_t;

typedef struct {
    netpkt_t packets[NET_QUEUE_LEN];
    int size;
    int head;
    int tail;
} netqueue_t;

typedef struct _netcard_t netcard_t;

typedef struct netdrv_t {
    void (*notify_in)(void *priv);
    void *(*init)(const netcard_t *card, const uint8_t *mac_addr, void *priv);
    void (*close)(void *priv);
    void *priv;
} netdrv_t;

extern const netdrv_t net_pcap_drv;
extern const netdrv_t net_slirp_drv;

struct _netcard_t {
    const device_t *device;
    void           *card_drv;
    struct netdrv_t host_drv;
    int (*poll)(void *);
    NETRXCB         rx;
    NETWAITCB       wait;
    NETSETLINKSTATE set_link_state;
    netqueue_t      queues[3];
    netpkt_t        queued_pkt;
    mutex_t        *tx_mutex;
    mutex_t        *rx_mutex;
    pc_timer_t      timer;
};

typedef struct {
    char		device[128];
    char		description[128];
} netdev_t;


#ifdef __cplusplus
extern "C" {
#endif

/* Global variables. */
extern int	nic_do_log;				/* config */
extern int      network_ndev;
extern netdev_t network_devs[32];


/* Function prototypes. */
extern void	network_init(void);
extern netcard_t *network_attach(void *card_drv, uint8_t *mac, NETRXCB rx, NETWAITCB wait, NETSETLINKSTATE set_link_state);
extern void netcard_close(netcard_t *card);
extern void	network_close(void);
extern void	network_reset(void);
extern int	network_available(void);
extern void	network_tx(netcard_t *card, uint8_t *, int);

extern int	net_pcap_prepare(netdev_t *);

extern int	network_dev_to_id(char *);
extern int	network_card_available(int);
extern int	network_card_has_config(int);
extern char	*network_card_get_internal_name(int);
extern int	network_card_get_from_internal_name(char *);
extern const device_t	*network_card_getdevice(int);

extern int network_tx_pop(netcard_t *card, netpkt_t *out_pkt);
extern int network_rx_put(netcard_t *card, uint8_t *bufp, int len);
#ifdef __cplusplus
}
#endif


#endif	/*EMU_NETWORK_H*/
