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
#define NET_TYPE_PCAP	1		/* use the (Win)Pcap API */
#define NET_TYPE_SLIRP	2		/* use the SLiRP port forwarder */

/* Supported network cards. */
enum {
    NONE = 0,
    NE1000,
    NE2000,
    RTL8019AS,
    RTL8029AS
};


typedef int (*NETRXCB)(void *, uint8_t *, int);
typedef int (*NETWAITCB)(void *);
typedef int (*NETSETLINKSTATE)(void *);


typedef struct netpkt {
    void		*priv;
    uint8_t		data[65536];	/* Maximum length + 1 to round up to the nearest power of 2. */
    int			len;

    struct netpkt	*prev, *next;
} netpkt_t;

typedef struct {
    const device_t	*device;
    void		*priv;
    int			(*poll)(void *);
    NETRXCB		rx;
    NETWAITCB		wait;
    NETSETLINKSTATE	set_link_state;
} netcard_t;

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
extern int	network_rx_pause;
extern netdev_t network_devs[32];


/* Function prototypes. */
extern void	network_wait(uint8_t wait);

extern void	network_init(void);
extern void	network_attach(void *, uint8_t *, NETRXCB, NETWAITCB, NETSETLINKSTATE);
extern void	network_close(void);
extern void	network_reset(void);
extern int	network_available(void);
extern void	network_tx(uint8_t *, int);
extern int	network_tx_queue_check(void);

extern int	net_pcap_prepare(netdev_t *);
extern int	net_pcap_init(void);
extern int	net_pcap_reset(const netcard_t *, uint8_t *);
extern void	net_pcap_close(void);
extern void	net_pcap_in(uint8_t *, int);

extern int	net_slirp_init(void);
extern int	net_slirp_reset(const netcard_t *, uint8_t *);
extern void	net_slirp_close(void);
extern void	net_slirp_in(uint8_t *, int);

extern int	network_dev_to_id(char *);
extern int	network_card_available(int);
extern int	network_card_has_config(int);
extern char	*network_card_get_internal_name(int);
extern int	network_card_get_from_internal_name(char *);
extern const device_t	*network_card_getdevice(int);

extern void	network_set_wait(int wait);
extern int	network_get_wait(void);

extern void	network_timer_stop(void);

extern void	network_queue_put(int tx, void *priv, uint8_t *data, int len);

#ifdef __cplusplus
}
#endif


#endif	/*EMU_NETWORK_H*/
