/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#ifndef NETWORK_H
# define NETWORK_H
# include <stdint.h>


#define NE2000		1
#define RTL8029AS	2


extern int network_card_current;


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
