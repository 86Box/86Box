/* Copyright holders: SA1988
   see COPYING for more details
*/
void ne2000_generate_maclocal(int mac);
void ne2000_generate_maclocal_pci(int mac);
int net2000_get_maclocal();
int net2000_get_maclocal_pci();

extern device_t ne2000_device;
extern device_t rtl8029as_device;
