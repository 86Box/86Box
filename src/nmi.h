/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
extern int nmi_mask;


extern void nmi_init(void);
extern void nmi_write(uint16_t port, uint8_t val, void *p);
