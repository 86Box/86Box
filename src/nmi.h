/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
extern int nmi_mask;
extern int nmi;
extern int nmi_auto_clear;


extern void nmi_init(void);

extern void nmi_write(uint16_t port, uint8_t val, void *p);
