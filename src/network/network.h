/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the network module.
 *
 * Version:	@(#)network.h	1.0.10	2017/11/01
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 */
#ifndef EMU_NETWORK_H
# define EMU_NETWORK_H
# include <stdint.h>


/* Network provider types. */
#define NET_TYPE_NONE	0		/* networking disabled */
#define NET_TYPE_PCAP	1		/* use the (Win)Pcap API */
#define NET_TYPE_SLIRP	2		/* use the SLiRP port forwarder */

/* Supported network cards. */
#define NE1000		1
#define NE2000		2
#define RTL8029AS	3


typedef void (*NETRXCB)(void *, uint8_t *, int);


typedef struct {
    char	name[64];
    char	internal_name[32];
    device_t	*device;
    void	*priv;
    int		(*poll)(void *);
    NETRXCB	rx;
    uint8_t	*mac;
} netcard_t;

typedef struct {
    char	device[128];
    char	description[128];
} netdev_t;


#ifdef __cplusplus
extern "C" {
#endif

/* Global variables. */
extern int	nic_do_log;				/* config */
extern int	network_card;				/* config */
extern int	network_type;				/* config */
extern char	network_pcap[512];			/* config */
extern int      network_ndev;
extern netdev_t network_devs[32];


/* Function prototypes. */
extern void	network_wait(uint8_t wait);
extern void	network_poll(void);
extern void	network_busy(uint8_t set);
extern void	network_end(void);

extern void	network_init(void);
extern void	network_attach(void *, uint8_t *, NETRXCB);
extern void	network_close(void);
extern void	network_reset(void);
extern int	network_available(void);
extern void	network_tx(uint8_t *, int);

extern int	net_pcap_prepare(netdev_t *);
extern int	net_pcap_init(void);
extern int	net_pcap_reset(netcard_t *);
extern void	net_pcap_close(void);
extern void	net_pcap_in(uint8_t *, int);

extern int	net_slirp_init(void);
extern int	net_slirp_reset(netcard_t *);
extern void	net_slirp_close(void);
extern void	net_slirp_in(uint8_t *, int);

extern int	network_dev_to_id(char *);
extern int	network_card_available(int);
extern char	*network_card_getname(int);
extern int	network_card_has_config(int);
extern char	*network_card_get_internal_name(int);
extern int	network_card_get_from_internal_name(char *);
extern device_t	*network_card_getdevice(int);

#ifdef __cplusplus
}
#endif


#endif	/*EMU_NETWORK_H*/
