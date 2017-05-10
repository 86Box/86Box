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
 * Version:	@(#)network.h	1.0.1	2017/05/09
 *
 * Authors:	Kotori, <oubattler@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 */
#ifndef NETWORK_H
# define NETWORK_H
# include <stdint.h>


#define NE2000		1
#define RTL8029AS	2


extern int	network_card_current;
extern uint8_t	ethif;
extern int	inum;


extern void	network_init(void);
extern void	network_reset(void);
extern void	network_add_handler(void (*poller)(void *p), void *p);

extern int	network_card_available(int card);
extern char	*network_card_getname(int card);
extern int	network_card_has_config(int card);
extern char	*network_card_get_internal_name(int card);
extern int	network_card_get_from_internal_name(char *s);
extern struct device_t *network_card_getdevice(int card);

extern void	initpcap(void);
extern void	closepcap(void);


#endif	/*NETWORK_H*/
