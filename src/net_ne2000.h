/* Copyright holders: SA1988
   see COPYING for more details
*/
#ifndef NET_NE2000_H
# define NET_NE2000_H


extern device_t	ne2000_device;
extern device_t	rtl8029as_device;


extern void	ne2000_generate_maclocal(uint32_t mac);
extern uint32_t	net2000_get_maclocal(void);

extern void	ne2000_generate_maclocal_pci(uint32_t mac);
extern uint32_t	net2000_get_maclocal_pci(void);


#endif	/*NET_NE2000_H*/
