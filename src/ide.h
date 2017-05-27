/* Copyright holders: Sarah Walker, Tenshi, SA1988
   see COPYING for more details
*/
#ifndef __IDE__
#define __IDE__

#ifdef __MSC__
# pragma pack(push,1)
typedef struct IDE
#else
typedef struct __attribute__((__packed__)) IDE
#endif
{
	int type;
	int board;
	uint8_t atastat;
	uint8_t error;
	int secount,sector,cylinder,head,drive,cylprecomp;
	uint8_t command;
	uint8_t fdisk;
	int pos;
	int packlen;
	int spt,hpc;
	int tracks;
	int packetstatus;
	uint8_t asc;
	int reset;
	FILE *hdfile;
	uint16_t buffer[65536];
	int irqstat;
	int service;
	int lba;
	int channel;
	uint32_t lba_addr;
	int skip512;
	int blocksize, blockcount;
	uint16_t dma_identify_data[3];
	int hdi,base;
	int hdc_num;
	uint8_t specify_success;
	int mdma_mode;
} IDE;
#ifdef __MSC__
# pragma pack(pop)
#endif

extern void writeide(int ide_board, uint16_t addr, uint8_t val);
extern void writeidew(int ide_board, uint16_t val);
extern uint8_t readide(int ide_board, uint16_t addr);
extern uint16_t readidew(int ide_board);
extern void callbackide(int ide_board);
extern void resetide(void);
extern void ide_init();
extern void ide_xtide_init();
extern void ide_ter_init();
extern void ide_qua_init();
extern void ide_pri_enable();
extern void ide_sec_enable();
extern void ide_ter_enable();
extern void ide_qua_enable();
extern void ide_pri_disable();
extern void ide_sec_disable();
extern void ide_ter_disable();
extern void ide_qua_disable();
extern void ide_set_bus_master(int (*read)(int channel, uint8_t *data, int transfer_length), int (*write)(int channel, uint8_t *data, int transfer_length), void (*set_irq)(int channel));

extern int ideboard;

extern int ide_enable[5];
extern int ide_irq[4];

extern int idecallback[5];

void ide_irq_raise(IDE *ide);
void ide_irq_lower(IDE *ide);

IDE ide_drives[IDE_NUM + XTIDE_NUM];

void ide_padstr8(uint8_t *buf, int buf_size, const char *src);

void win_cdrom_eject(uint8_t id);
void win_cdrom_reload(uint8_t id);

#endif

void ide_pri_disable();
void ide_pri_enable_ex();
void ide_set_base(int controller, uint16_t port);
void ide_set_side(int controller, uint16_t port);
