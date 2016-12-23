/* Copyright holders: Sarah Walker, Tenshi, SA1988
   see COPYING for more details
*/
#ifndef __IDE__
#define __IDE__

struct IDE;

extern void writeide(int ide_board, uint16_t addr, uint8_t val);
extern void writeidew(int ide_board, uint16_t val);
extern uint8_t readide(int ide_board, uint16_t addr);
extern uint16_t readidew(int ide_board);
extern void callbackide(int ide_board);
extern void resetide(void);
extern void ide_init();
extern void ide_ter_init();
extern void ide_pri_enable();
extern void ide_sec_enable();
extern void ide_ter_enable();
extern void ide_pri_disable();
extern void ide_sec_disable();
extern void ide_ter_disable();
extern void ide_set_bus_master(int (*read_sector)(int channel, uint8_t *data), int (*write_sector)(int channel, uint8_t *data), void (*set_irq)(int channel));

extern int ideboard;

extern int idecallback[3];

extern char ide_fn[6][512];

extern int atapi_cdrom_channel;

#endif //__IDE__
