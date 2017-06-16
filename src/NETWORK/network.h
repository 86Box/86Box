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
 * Version:	@(#)network.h	1.0.7	2017/06/03
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
    const char	*name;
    const char	*internal_name;
    device_t	*device;
    void	*private;
    int		(*poll)(void *);
    NETRXCB	rx;
} netcard_t;

typedef struct {
    char	device[128];
    char	description[128];
} netdev_t;


/* Global variables. */
extern int	nic_do_log;
extern int	network_card;
extern int	network_type;
extern int      network_ndev;
extern netdev_t network_devs[32];
extern char	network_pcap[512];


/* Function prototypes. */
extern void	network_init(void);
extern int	network_attach(void *, uint8_t *, NETRXCB);
extern void	network_close(void);
extern void	network_reset(void);
extern void	network_tx(uint8_t *, int);

extern int	network_pcap_init(netdev_t *);
extern void	network_pcap_reset(void);
extern int	network_pcap_setup(uint8_t *, NETRXCB, void *);
extern void	network_pcap_close(void);
extern void	network_pcap_in(uint8_t *, int);

extern int	network_slirp_setup(uint8_t *, NETRXCB, void *);
extern void	network_slirp_close(void);
extern void	network_slirp_in(uint8_t *, int);

extern int	network_dev_to_id(char *);
extern int	network_card_available(int);
extern char	*network_card_getname(int);
extern int	network_card_has_config(int);
extern char	*network_card_get_internal_name(int);
extern int	network_card_get_from_internal_name(char *);
extern device_t	*network_card_getdevice(int);


#endif	/*EMU_NETWORK_H*/
