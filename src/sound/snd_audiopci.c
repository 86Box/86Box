#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define _USE_MATH_DEFINES
#include <math.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/gameport.h>
#include <86box/io.h>
#include <86box/nmi.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/timer.h>
#include <86box/sound.h>
#include <86box/midi.h>
#include <86box/snd_ac97.h>


#define N 16

#define ES1371_NCoef 91

static float low_fir_es1371_coef[ES1371_NCoef];

typedef struct {
    uint8_t pci_command, pci_serr;

    uint32_t base_addr;

    uint8_t int_line;

    uint16_t pmcsr;

    uint32_t int_ctrl;
    uint32_t int_status;

    uint32_t legacy_ctrl;

    int mem_page;

    uint32_t si_cr;

    uint32_t sr_cir;
    uint16_t sr_ram[128];

    uint8_t uart_ctrl;
    uint8_t uart_status;

    ac97_codec_t *codec;
    uint32_t codec_ctrl;

    struct {
	uint32_t addr, addr_latch;
	uint16_t count, size;

	uint16_t samp_ct;
	int curr_samp_ct;

	pc_timer_t timer;
	uint64_t latch;

	uint32_t vf, ac;

	int16_t buffer_l[64], buffer_r[64];
	int buffer_pos, buffer_pos_end;

	int filtered_l[32], filtered_r[32];
	int f_pos;

	int16_t out_l, out_r;

	int32_t vol_l, vol_r;
    } dac[2], adc;

    int64_t dac_latch, dac_time;

    int master_vol_l, master_vol_r;
    int cd_vol_l, cd_vol_r;

    int card;

    int pos;
    int16_t buffer[SOUNDBUFLEN * 2];

    int type;
} es1371_t;


#define LEGACY_SB_ADDR			(1<<29)
#define LEGACY_SSCAPE_ADDR_SHIFT	27
#define LEGACY_CODEC_ADDR_SHIFT		25
#define LEGACY_FORCE_IRQ		(1<<24)
#define LEGACY_CAPTURE_SLAVE_DMA	(1<<23)
#define LEGACY_CAPTURE_SLAVE_PIC	(1<<22)
#define LEGACY_CAPTURE_MASTER_DMA	(1<<21)
#define LEGACY_CAPTURE_MASTER_PIC	(1<<20)
#define LEGACY_CAPTURE_ADLIB		(1<<19)
#define LEGACY_CAPTURE_SB		(1<<18)
#define LEGACY_CAPTURE_CODEC		(1<<17)
#define LEGACY_CAPTURE_SSCAPE		(1<<16)
#define LEGACY_EVENT_SSCAPE		(0<<8)
#define LEGACY_EVENT_CODEC		(1<<8)
#define LEGACY_EVENT_SB			(2<<8)
#define LEGACY_EVENT_ADLIB		(3<<8)
#define LEGACY_EVENT_MASTER_PIC		(4<<8)
#define LEGACY_EVENT_MASTER_DMA		(5<<8)
#define LEGACY_EVENT_SLAVE_PIC		(6<<8)
#define LEGACY_EVENT_SLAVE_DMA		(7<<8)
#define LEGACY_EVENT_MASK		(7<<8)
#define LEGACY_EVENT_ADDR_SHIFT		3
#define LEGACY_EVENT_ADDR_MASK		(0x1f<<3)
#define LEGACY_EVENT_TYPE_RW		(1<<2)
#define LEGACY_INT			(1<<0)

#define SRC_RAM_WE			(1<<24)

#define CODEC_READ			(1<<23)
#define CODEC_READY			(1<<31)

#define INT_DAC1_EN			(1<<6)
#define INT_DAC2_EN			(1<<5)
#define INT_UART_EN			(1<<3)

#define SI_P2_INTR_EN			(1<<9)
#define SI_P1_INTR_EN			(1<<8)

#define INT_STATUS_INTR			(1<<31)
#define INT_STATUS_UART			(1<<3)
#define INT_STATUS_DAC1			(1<<2)
#define INT_STATUS_DAC2			(1<<1)

#define UART_CTRL_RXINTEN		(1<<7)
#define UART_CTRL_TXINTEN		(1<<5)

#define UART_STATUS_RXINT		(1<<7)
#define UART_STATUS_TXINT		(1<<2)
#define UART_STATUS_TXRDY		(1<<1)
#define UART_STATUS_RXRDY		(1<<0)

#define FORMAT_MONO_8			0
#define FORMAT_STEREO_8			1
#define FORMAT_MONO_16			2
#define FORMAT_STEREO_16		3

static void es1371_fetch(es1371_t *es1371, int dac_nr);
static void update_legacy(es1371_t *es1371, uint32_t old_legacy_ctrl);

#ifdef ENABLE_AUDIOPCI_LOG
int audiopci_do_log = ENABLE_AUDIOPCI_LOG;


static void
audiopci_log(const char *fmt, ...)
{
    va_list ap;

    if (audiopci_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define audiopci_log(fmt, ...)
#endif


static void es1371_update_irqs(es1371_t *es1371)
{
	int irq = 0;
	
	if ((es1371->int_status & INT_STATUS_DAC1) && (es1371->si_cr & SI_P1_INTR_EN))
		irq = 1;
	if ((es1371->int_status & INT_STATUS_DAC2) && (es1371->si_cr & SI_P2_INTR_EN)) {
		irq = 1;
	}
	/*MIDI input is unsupported for now*/
	if ((es1371->int_status & INT_STATUS_UART) && (es1371->uart_status & UART_STATUS_TXINT)) {
		irq = 1;
	}

	if (irq)
		es1371->int_status |= INT_STATUS_INTR;
	else
		es1371->int_status &= ~INT_STATUS_INTR;

	if (es1371->legacy_ctrl & LEGACY_FORCE_IRQ)
		irq = 1;
	
	if (irq)
	{
		pci_set_irq(es1371->card, PCI_INTA);
//		audiopci_log("Raise IRQ\n");
	}
	else
	{
		pci_clear_irq(es1371->card, PCI_INTA);
//		audiopci_log("Drop IRQ\n");
	}
}

static uint8_t es1371_inb(uint16_t port, void *p)
{
	es1371_t *es1371 = (es1371_t *)p;
	uint8_t ret = 0;
	
	switch (port & 0x3f)
	{
		case 0x00:
		ret = es1371->int_ctrl & 0xff;
		break;
		case 0x01:
		ret = (es1371->int_ctrl >> 8) & 0xff;
		break;
		case 0x02:
		ret = (es1371->int_ctrl >> 16) & 0xff;
		break;
		case 0x03:
		ret = (es1371->int_ctrl >> 24) & 0xff;
		break;

		case 0x04:
		ret = es1371->int_status & 0xff;
		break;
		case 0x05:
		ret = (es1371->int_status >> 8) & 0xff;
		break;
		case 0x06:
		ret = (es1371->int_status >> 16) & 0xff;
		break;
		case 0x07:
		ret = (es1371->int_status >> 24) & 0xff;
		break;
		
		case 0x09:
		ret = es1371->uart_status & 0xc7;
		audiopci_log("ES1371 UART Status = %02x\n", es1371->uart_status);
		break;
		
		case 0x0c:
		ret = es1371->mem_page;
		break;
		
		case 0x1a:
		ret = es1371->legacy_ctrl >> 16;
		break;
		case 0x1b:
		ret = es1371->legacy_ctrl >> 24;
		break;
		
		case 0x20:
		ret = es1371->si_cr & 0xff;
		break;
		case 0x21:
		ret = es1371->si_cr >> 8;
		break;
		case 0x22:
		ret = (es1371->si_cr >> 16) | 0x80;
		break;
		case 0x23:		
		ret = 0xff;
		break;
		
		default:
		audiopci_log("Bad es1371_inb: port=%04x\n", port);
	}

	audiopci_log("es1371_inb: port=%04x ret=%02x\n", port, ret);
//	output = 3;
	return ret;
}
static uint16_t es1371_inw(uint16_t port, void *p)
{
	es1371_t *es1371 = (es1371_t *)p;
	uint16_t ret = 0;
	
	switch (port & 0x3e)
	{
		case 0x00:
		ret = es1371->int_ctrl & 0xffff;
		break;
		case 0x02:
		ret = (es1371->int_ctrl >> 16) & 0xffff;
		break;

		case 0x18:
		ret = es1371->legacy_ctrl & 0xffff;
//		audiopci_log("Read legacy ctrl %04x\n", ret);
		break;

		case 0x26:
		ret = es1371->dac[0].curr_samp_ct;
		break;

		case 0x2a:
		ret = es1371->dac[1].curr_samp_ct;
		break;
		
		case 0x36:
		switch (es1371->mem_page)
		{
			case 0xc:
			ret = es1371->dac[0].count;
			break;
			
			default:
			audiopci_log("Bad es1371_inw: mem_page=%x port=%04x\n", es1371->mem_page, port);
		}
		break;

		case 0x3e:
		switch (es1371->mem_page)
		{
			case 0xc:
			ret = es1371->dac[1].count;
			break;
			
			default:
			audiopci_log("Bad es1371_inw: mem_page=%x port=%04x\n", es1371->mem_page, port);
		}
		break;

		default:
		ret  = es1371_inb(port, p);
		ret |= es1371_inb(port + 1, p) << 8;
	}

//	audiopci_log("es1371_inw: port=%04x ret=%04x %04x:%08x\n", port, ret, CS,cpu_state.pc);
	return ret;
}
static uint32_t es1371_inl(uint16_t port, void *p)
{
	es1371_t *es1371 = (es1371_t *)p;
	uint32_t ret = 0;
	
	switch (port & 0x3c)
	{
		case 0x00:
		ret = es1371->int_ctrl;
		break;
		case 0x04:
		ret = es1371->int_status;
		break;
		
		case 0x10:
		ret = es1371->sr_cir & ~0xffff;
		ret |= es1371->sr_ram[es1371->sr_cir >> 25];
		break;
		
		case 0x14:
		ret = es1371->codec_ctrl | CODEC_READY;
		break;

		case 0x30:
		switch (es1371->mem_page) {
			case 0xe: case 0xf:
			audiopci_log("ES1371 0x30 read UART FIFO: val = %02x\n", ret & 0xff);
			break;
		}
		break;

		case 0x34:
		switch (es1371->mem_page) {
			case 0xc:
			ret = es1371->dac[0].size | (es1371->dac[0].count << 16);
			break;
			
			case 0xd:
			ret = es1371->adc.size | (es1371->adc.count << 16);
			break;

			case 0xe: case 0xf:
			audiopci_log("ES1371 0x34 read UART FIFO: val = %02x\n", ret & 0xff);
			break;

			default:
			audiopci_log("Bad es1371_inl: mem_page=%x port=%04x\n", es1371->mem_page, port);
		}
		break;

		case 0x38:
		switch (es1371->mem_page) {
			case 0xe: case 0xf:
			audiopci_log("ES1371 0x38 read UART FIFO: val = %02x\n", ret & 0xff);
			break;
		}
		break;		

		case 0x3c:
		switch (es1371->mem_page) {
			case 0xc:
			ret = es1371->dac[1].size | (es1371->dac[1].count << 16);
			break;
			
			case 0xe: case 0xf:
			audiopci_log("ES1371 0x3c read UART FIFO: val = %02x\n", ret & 0xff);
			break;			

			default:
			audiopci_log("Bad es1371_inl: mem_page=%x port=%04x\n", es1371->mem_page, port);
		}
		break;
		
		default:
		ret  = es1371_inw(port, p);
		ret |= es1371_inw(port + 2, p) << 16;
	}

	audiopci_log("es1371_inl: port=%04x ret=%08x\n", port, ret);
	return ret;
}

static void es1371_outb(uint16_t port, uint8_t val, void *p)
{
	es1371_t *es1371 = (es1371_t *)p;
        uint32_t old_legacy_ctrl;

	audiopci_log("es1371_outb: port=%04x val=%02x\n", port, val);
	switch (port & 0x3f)
	{
		case 0x00:
		if (!(es1371->int_ctrl & INT_DAC1_EN) && (val & INT_DAC1_EN))
		{
			es1371->dac[0].addr = es1371->dac[0].addr_latch;
			es1371->dac[0].buffer_pos = 0;
			es1371->dac[0].buffer_pos_end = 0;
			es1371_fetch(es1371, 0);
		}
		if (!(es1371->int_ctrl & INT_DAC2_EN) && (val & INT_DAC2_EN))
		{
			es1371->dac[1].addr = es1371->dac[1].addr_latch;
			es1371->dac[1].buffer_pos = 0;
			es1371->dac[1].buffer_pos_end = 0;
			es1371_fetch(es1371, 1);
		}
		es1371->int_ctrl = (es1371->int_ctrl & 0xffffff00) | val;
		break;
		case 0x01:
		es1371->int_ctrl = (es1371->int_ctrl & 0xffff00ff) | (val << 8);
		break;
		case 0x02:
		es1371->int_ctrl = (es1371->int_ctrl & 0xff00ffff) | (val << 16);
		break;
		case 0x03:
		es1371->int_ctrl = (es1371->int_ctrl & 0x00ffffff) | (val << 24);
		break;
		
		case 0x08:
		midi_raw_out_byte(val);
		break;
		
		case 0x09:
		es1371->uart_ctrl = val & 0xe3;
		audiopci_log("ES1371 UART Cntrl = %02x\n", es1371->uart_ctrl);
		break;
		
		case 0x0c:
		es1371->mem_page = val & 0xf;
		break;

		case 0x18:
		es1371->legacy_ctrl |= LEGACY_INT;
		nmi = 0;
		break;
		case 0x1a:
                old_legacy_ctrl = es1371->legacy_ctrl;
		es1371->legacy_ctrl = (es1371->legacy_ctrl & 0xff00ffff) | (val << 16);
		update_legacy(es1371, old_legacy_ctrl);
		break;
		case 0x1b:
                old_legacy_ctrl = es1371->legacy_ctrl;
		es1371->legacy_ctrl = (es1371->legacy_ctrl & 0x00ffffff) | (val << 24);
		es1371_update_irqs(es1371);
//		output = 3;
		update_legacy(es1371, old_legacy_ctrl);
		break;
		
		case 0x20:
		es1371->si_cr = (es1371->si_cr & 0xffff00) | val;
		break;
		case 0x21:
		es1371->si_cr = (es1371->si_cr & 0xff00ff) | (val << 8);
		if (!(es1371->si_cr & SI_P1_INTR_EN))
			es1371->int_status &= ~INT_STATUS_DAC1;
		if (!(es1371->si_cr & SI_P2_INTR_EN))
			es1371->int_status &= ~INT_STATUS_DAC2;
		es1371_update_irqs(es1371);
		break;
		case 0x22:
		es1371->si_cr = (es1371->si_cr & 0x00ffff) | (val << 16);
		break;
		
		default:
		audiopci_log("Bad es1371_outb: port=%04x val=%02x\n", port, val);
	}
}
static void es1371_outw(uint16_t port, uint16_t val, void *p)
{
	es1371_t *es1371 = (es1371_t *)p;

//	audiopci_log("es1371_outw: port=%04x val=%04x\n", port, val);
	switch (port & 0x3f)
	{
		case 0x0c:
		es1371->mem_page = val & 0xf;
		break;

		case 0x24:
		es1371->dac[0].samp_ct = val;
		break;

		case 0x28:
		es1371->dac[1].samp_ct = val;
		break;
		
		default:
		es1371_outb(port, val & 0xff, p);
		es1371_outb(port + 1, (val >> 8) & 0xff, p);
	}
}
static void es1371_outl(uint16_t port, uint32_t val, void *p)
{
	es1371_t *es1371 = (es1371_t *)p;

	audiopci_log("es1371_outl: port=%04x val=%08x\n", port, val);
	switch (port & 0x3f)
	{
		case 0x04:
		break;
		
		case 0x0c:
		es1371->mem_page = val & 0xf;
		break;

		case 0x10:
		es1371->sr_cir = val;
		if (es1371->sr_cir & SRC_RAM_WE)
		{
//			audiopci_log("Write SR RAM %02x %04x\n", es1371->sr_cir >> 25, val & 0xffff);
			es1371->sr_ram[es1371->sr_cir >> 25] = val & 0xffff;
			switch (es1371->sr_cir >> 25)
			{
				case 0x71:
				es1371->dac[0].vf = (es1371->dac[0].vf & ~0x1f8000) | ((val & 0xfc00) << 5);
				es1371->dac[0].ac = (es1371->dac[0].ac & ~0x7f8000) | ((val & 0x00ff) << 15);
                                es1371->dac[0].f_pos = 0;
				break;
				case 0x72:
				es1371->dac[0].ac = (es1371->dac[0].ac & ~0x7fff) | (val & 0x7fff);
				break;				
				case 0x73:
				es1371->dac[0].vf = (es1371->dac[0].vf & ~0x7fff) | (val & 0x7fff);
				break;

				case 0x75:
				es1371->dac[1].vf = (es1371->dac[1].vf & ~0x1f8000) | ((val & 0xfc00) << 5);
				es1371->dac[1].ac = (es1371->dac[1].ac & ~0x7f8000) | ((val & 0x00ff) << 15);
                                es1371->dac[1].f_pos = 0;
				break;
				case 0x76:
				es1371->dac[1].ac = (es1371->dac[1].ac & ~0x7fff) | (val & 0x7fff);
				break;				
				case 0x77:
				es1371->dac[1].vf = (es1371->dac[1].vf & ~0x7fff) | (val & 0x7fff);
				break;
				
				case 0x7c:
				es1371->dac[0].vol_l = (int32_t)(int16_t)(val & 0xffff);
				break;
				case 0x7d:
				es1371->dac[0].vol_r = (int32_t)(int16_t)(val & 0xffff);
				break;
				case 0x7e:
				es1371->dac[1].vol_l = (int32_t)(int16_t)(val & 0xffff);
				break;
				case 0x7f:
				es1371->dac[1].vol_r = (int32_t)(int16_t)(val & 0xffff);
				break;
			}
		}
		break;

		case 0x14:
		if (val & CODEC_READ) {
			es1371->codec_ctrl &= 0x00ff0000;
			val = (val >> 16) & 0x7e;
			es1371->codec_ctrl |= ac97_codec_read(es1371->codec, val);
			es1371->codec_ctrl |= ac97_codec_read(es1371->codec, val | 1) << 8;
		} else {
			es1371->codec_ctrl = val & 0x00ffffff;
			ac97_codec_write(es1371->codec,  (val >> 16) & 0x7e,      val & 0xff);
			ac97_codec_write(es1371->codec, ((val >> 16) & 0x7e) | 1, val >> 8);

			ac97_codec_getattn(es1371->codec, 0x02, &es1371->master_vol_l, &es1371->master_vol_r);
			ac97_codec_getattn(es1371->codec, 0x12, &es1371->cd_vol_l, &es1371->cd_vol_r);
		}
		break;
		
		case 0x24:
		es1371->dac[0].samp_ct = val & 0xffff;
		break;

		case 0x28:
		es1371->dac[1].samp_ct = val & 0xffff;
		break;

		case 0x30:
		switch (es1371->mem_page)
		{
			case 0x0: case 0x1: case 0x2: case 0x3:
			case 0x4: case 0x5: case 0x6: case 0x7:
			case 0x8: case 0x9: case 0xa: case 0xb:
			break;
			
			case 0xc:
			es1371->dac[0].addr_latch = val;
//			audiopci_log("DAC1 addr %08x\n", val);
			break;
			
			case 0xe: case 0xf:
			audiopci_log("ES1371 0x30 write UART FIFO: val = %02x\n", val & 0xff);
			break;
			
			default:
			audiopci_log("Bad es1371_outl: mem_page=%x port=%04x val=%08x\n", es1371->mem_page, port, val);
		}
		break;
		case 0x34:
		switch (es1371->mem_page)
		{
			case 0x0: case 0x1: case 0x2: case 0x3:
			case 0x4: case 0x5: case 0x6: case 0x7:
			case 0x8: case 0x9: case 0xa: case 0xb:
			break;

			case 0xc:
			es1371->dac[0].size = val & 0xffff;
			es1371->dac[0].count = val >> 16;
			break;
			
			case 0xd:
			es1371->adc.size = val & 0xffff;
			es1371->adc.count = val >> 16;
			break;

			case 0xe: case 0xf:
			audiopci_log("ES1371 0x34 write UART FIFO: val = %02x\n", val & 0xff);
			break;

			default:
			audiopci_log("Bad es1371_outl: mem_page=%x port=%04x val=%08x\n", es1371->mem_page, port, val);
		}
		break;
		case 0x38:
		switch (es1371->mem_page)
		{
			case 0x0: case 0x1: case 0x2: case 0x3:
			case 0x4: case 0x5: case 0x6: case 0x7:
			case 0x8: case 0x9: case 0xa: case 0xb:
			break;

			case 0xc:
			es1371->dac[1].addr_latch = val;
			break;
			
			case 0xd:
			break;
			
			case 0xe: case 0xf:
			audiopci_log("ES1371 0x38 write UART FIFO: val = %02x\n", val & 0xff);
			break;

			default:
			audiopci_log("Bad es1371_outl: mem_page=%x port=%04x val=%08x\n", es1371->mem_page, port, val);
		}
		break;
		case 0x3c:
		switch (es1371->mem_page)
		{
			case 0x0: case 0x1: case 0x2: case 0x3:
			case 0x4: case 0x5: case 0x6: case 0x7:
			case 0x8: case 0x9: case 0xa: case 0xb:
			break;

			case 0xc:
			es1371->dac[1].size = val & 0xffff;
			es1371->dac[1].count = val >> 16;
			break;		

			case 0xe: case 0xf:
			audiopci_log("ES1371 0x3c write UART FIFO: val = %02x\n", val & 0xff);
			break;

			default:
			audiopci_log("Bad es1371_outl: mem_page=%x port=%04x val=%08x\n", es1371->mem_page, port, val);
		}
		break;

		default:
		es1371_outw(port, val & 0xffff, p);
		es1371_outw(port + 2, (val >> 16) & 0xffff, p);
	}
}

static void capture_event(es1371_t *es1371, int type, int rw, uint16_t port)
{
	es1371->legacy_ctrl &= ~(LEGACY_EVENT_MASK | LEGACY_EVENT_ADDR_MASK);
	es1371->legacy_ctrl |= type;
	if (rw)	
		es1371->legacy_ctrl |= LEGACY_EVENT_TYPE_RW;
	else
		es1371->legacy_ctrl &= ~LEGACY_EVENT_TYPE_RW;
	es1371->legacy_ctrl |= ((port << LEGACY_EVENT_ADDR_SHIFT) & LEGACY_EVENT_ADDR_MASK);
	es1371->legacy_ctrl &= ~LEGACY_INT;
	nmi = 1;
//	audiopci_log("Event! %s %04x\n", rw ? "write" : "read", port);
}

static void capture_write_sscape(uint16_t port, uint8_t val, void *p)
{
	capture_event(p, LEGACY_EVENT_SSCAPE, 1, port);
}
static void capture_write_codec(uint16_t port, uint8_t val, void *p)
{
	capture_event(p, LEGACY_EVENT_CODEC, 1, port);
}
static void capture_write_sb(uint16_t port, uint8_t val, void *p)
{
	capture_event(p, LEGACY_EVENT_SB, 1, port);
}
static void capture_write_adlib(uint16_t port, uint8_t val, void *p)
{
	capture_event(p, LEGACY_EVENT_ADLIB, 1, port);
}
static void capture_write_master_pic(uint16_t port, uint8_t val, void *p)
{
	capture_event(p, LEGACY_EVENT_MASTER_PIC, 1, port);
}
static void capture_write_master_dma(uint16_t port, uint8_t val, void *p)
{
	capture_event(p, LEGACY_EVENT_MASTER_DMA, 1, port);
}
static void capture_write_slave_pic(uint16_t port, uint8_t val, void *p)
{
	capture_event(p, LEGACY_EVENT_SLAVE_PIC, 1, port);
}
static void capture_write_slave_dma(uint16_t port, uint8_t val, void *p)
{
	capture_event(p, LEGACY_EVENT_SLAVE_DMA, 1, port);
}

static uint8_t capture_read_sscape(uint16_t port, void *p)
{
	capture_event(p, LEGACY_EVENT_SSCAPE, 0, port);
	return 0xff;
}
static uint8_t capture_read_codec(uint16_t port, void *p)
{
	capture_event(p, LEGACY_EVENT_CODEC, 0, port);
	return 0xff;
}
static uint8_t capture_read_sb(uint16_t port, void *p)
{
	capture_event(p, LEGACY_EVENT_SB, 0, port);
	return 0xff;
}
static uint8_t capture_read_adlib(uint16_t port, void *p)
{
	capture_event(p, LEGACY_EVENT_ADLIB, 0, port);
	return 0xff;
}
static uint8_t capture_read_master_pic(uint16_t port, void *p)
{
	capture_event(p, LEGACY_EVENT_MASTER_PIC, 0, port);
	return 0xff;
}
static uint8_t capture_read_master_dma(uint16_t port, void *p)
{
	capture_event(p, LEGACY_EVENT_MASTER_DMA, 0, port);
	return 0xff;
}
static uint8_t capture_read_slave_pic(uint16_t port, void *p)
{
	capture_event(p, LEGACY_EVENT_SLAVE_PIC, 0, port);
	return 0xff;
}
static uint8_t capture_read_slave_dma(uint16_t port, void *p)
{
	capture_event(p, LEGACY_EVENT_SLAVE_DMA, 0, port);
	return 0xff;
}

static void update_legacy(es1371_t *es1371, uint32_t old_legacy_ctrl)
{
        if (old_legacy_ctrl & LEGACY_CAPTURE_SSCAPE)
        {
                switch ((old_legacy_ctrl >> LEGACY_SSCAPE_ADDR_SHIFT) & 3)
                {
                        case 0: io_removehandler(0x0320, 0x0008, capture_read_sscape,NULL,NULL, capture_write_sscape,NULL,NULL, es1371); break;
                        case 1: io_removehandler(0x0330, 0x0008, capture_read_sscape,NULL,NULL, capture_write_sscape,NULL,NULL, es1371); break;
                        case 2: io_removehandler(0x0340, 0x0008, capture_read_sscape,NULL,NULL, capture_write_sscape,NULL,NULL, es1371); break;
                        case 3: io_removehandler(0x0350, 0x0008, capture_read_sscape,NULL,NULL, capture_write_sscape,NULL,NULL, es1371); break;
                }
        }
        if (old_legacy_ctrl & LEGACY_CAPTURE_CODEC)
        {
                switch ((old_legacy_ctrl >> LEGACY_CODEC_ADDR_SHIFT) & 3)
                {
                        case 0: io_removehandler(0x5300, 0x0080, capture_read_codec,NULL,NULL, capture_write_codec,NULL,NULL, es1371); break;
                        case 2: io_removehandler(0xe800, 0x0080, capture_read_codec,NULL,NULL, capture_write_codec,NULL,NULL, es1371); break;
                        case 3: io_removehandler(0xf400, 0x0080, capture_read_codec,NULL,NULL, capture_write_codec,NULL,NULL, es1371); break;
                }
        }
        if (old_legacy_ctrl & LEGACY_CAPTURE_SB)
        {
                if (!(old_legacy_ctrl & LEGACY_SB_ADDR))
                        io_removehandler(0x0220, 0x0010, capture_read_sb,NULL,NULL, capture_write_sb,NULL,NULL, es1371);
                else
                        io_removehandler(0x0240, 0x0010, capture_read_sb,NULL,NULL, capture_write_sb,NULL,NULL, es1371);
        }
        if (old_legacy_ctrl & LEGACY_CAPTURE_ADLIB)
                io_removehandler(0x0388, 0x0004, capture_read_adlib,NULL,NULL, capture_write_adlib,NULL,NULL, es1371);
        if (old_legacy_ctrl & LEGACY_CAPTURE_MASTER_PIC)
                io_removehandler(0x0020, 0x0002, capture_read_master_pic,NULL,NULL, capture_write_master_pic,NULL,NULL, es1371);
        if (old_legacy_ctrl & LEGACY_CAPTURE_MASTER_DMA)
                io_removehandler(0x0000, 0x0010, capture_read_master_dma,NULL,NULL, capture_write_master_dma,NULL,NULL, es1371);
        if (old_legacy_ctrl & LEGACY_CAPTURE_SLAVE_PIC)
                io_removehandler(0x00a0, 0x0002, capture_read_slave_pic,NULL,NULL, capture_write_slave_pic,NULL,NULL, es1371);
        if (old_legacy_ctrl & LEGACY_CAPTURE_SLAVE_DMA)
                io_removehandler(0x00c0, 0x0020, capture_read_slave_dma,NULL,NULL, capture_write_slave_dma,NULL,NULL, es1371);

        if (es1371->legacy_ctrl & LEGACY_CAPTURE_SSCAPE)
        {
                switch ((es1371->legacy_ctrl >> LEGACY_SSCAPE_ADDR_SHIFT) & 3)
                {
                        case 0: io_sethandler(0x0320, 0x0008, capture_read_sscape,NULL,NULL, capture_write_sscape,NULL,NULL, es1371); break;
                        case 1: io_sethandler(0x0330, 0x0008, capture_read_sscape,NULL,NULL, capture_write_sscape,NULL,NULL, es1371); break;
                        case 2: io_sethandler(0x0340, 0x0008, capture_read_sscape,NULL,NULL, capture_write_sscape,NULL,NULL, es1371); break;
                        case 3: io_sethandler(0x0350, 0x0008, capture_read_sscape,NULL,NULL, capture_write_sscape,NULL,NULL, es1371); break;
                }
        }
        if (es1371->legacy_ctrl & LEGACY_CAPTURE_CODEC)
        {
                switch ((es1371->legacy_ctrl >> LEGACY_CODEC_ADDR_SHIFT) & 3)
                {
                        case 0: io_sethandler(0x5300, 0x0080, capture_read_codec,NULL,NULL, capture_write_codec,NULL,NULL, es1371); break;
                        case 2: io_sethandler(0xe800, 0x0080, capture_read_codec,NULL,NULL, capture_write_codec,NULL,NULL, es1371); break;
                        case 3: io_sethandler(0xf400, 0x0080, capture_read_codec,NULL,NULL, capture_write_codec,NULL,NULL, es1371); break;
                }
        }
        if (es1371->legacy_ctrl & LEGACY_CAPTURE_SB)
        {
                if (!(es1371->legacy_ctrl & LEGACY_SB_ADDR))
                        io_sethandler(0x0220, 0x0010, capture_read_sb,NULL,NULL, capture_write_sb,NULL,NULL, es1371);
                else
                        io_sethandler(0x0240, 0x0010, capture_read_sb,NULL,NULL, capture_write_sb,NULL,NULL, es1371);
        }
        if (es1371->legacy_ctrl & LEGACY_CAPTURE_ADLIB)        
                io_sethandler(0x0388, 0x0004, capture_read_adlib,NULL,NULL, capture_write_adlib,NULL,NULL, es1371);
        if (es1371->legacy_ctrl & LEGACY_CAPTURE_MASTER_PIC)
                io_sethandler(0x0020, 0x0002, capture_read_master_pic,NULL,NULL, capture_write_master_pic,NULL,NULL, es1371);
        if (es1371->legacy_ctrl & LEGACY_CAPTURE_MASTER_DMA)
                io_sethandler(0x0000, 0x0010, capture_read_master_dma,NULL,NULL, capture_write_master_dma,NULL,NULL, es1371);
        if (es1371->legacy_ctrl & LEGACY_CAPTURE_SLAVE_PIC)
                io_sethandler(0x00a0, 0x0002, capture_read_slave_pic,NULL,NULL, capture_write_slave_pic,NULL,NULL, es1371);
        if (es1371->legacy_ctrl & LEGACY_CAPTURE_SLAVE_DMA)
                io_sethandler(0x00c0, 0x0020, capture_read_slave_dma,NULL,NULL, capture_write_slave_dma,NULL,NULL, es1371);
}


static uint8_t es1371_pci_read(int func, int addr, void *p)
{
	es1371_t *es1371 = (es1371_t *)p;

	if (func)
		return 0;

	//audiopci_log("ES1371 PCI read %08X PC=%08x\n", addr, cpu_state.pc);

	if (addr > 0x3f)
		return 0x00;

	switch (addr)
	{
		case 0x00: return 0x74; /*Ensoniq*/
		case 0x01: return 0x12;
		
		case 0x02: return 0x71; /* ES1371 */
		case 0x03: return 0x13;

		case 0x04: return es1371->pci_command;
		case 0x05: return es1371->pci_serr;
		
		case 0x06: return 0x10;	/* Supports ACPI */
		case 0x07: return 0x00;

		case 0x08: return 0x02;	/* Revision ID */
		case 0x09: return 0x00;	/* Multimedia audio device */
		case 0x0a: return 0x01;
		case 0x0b: return 0x04;
		
		case 0x10: return 0x01 | (es1371->base_addr & 0xc0);	/* memBaseAddr */
		case 0x11: return es1371->base_addr >> 8;
		case 0x12: return es1371->base_addr >> 16;
		case 0x13: return es1371->base_addr >> 24;

		case 0x2c: return 0x74;	/* Subsystem vendor ID */
		case 0x2d: return 0x12;
		case 0x2e: return 0x71;
		case 0x2f: return 0x13;

		case 0x34: return 0xdc; /*Capabilites pointer*/

		case 0x3c: return es1371->int_line;
		case 0x3d: return 0x01; /*INTA*/

		case 0x3e: return 0xc; /*Minimum grant*/
		case 0x3f: return 0x80; /*Maximum latency*/
		
		case 0xdc: return 0x01; /*Capabilities identifier*/
		case 0xdd: return 0x00; /*Next item pointer*/
		case 0xde: return 0x31; /*Power management capabilities*/
		case 0xdf: return 0x6c;
		
		case 0xe0: return es1371->pmcsr & 0xff;
		case 0xe1: return es1371->pmcsr >> 8;
	}
	return 0;
}

static void es1371_pci_write(int func, int addr, uint8_t val, void *p)
{
	es1371_t *es1371 = (es1371_t *)p;
	
	if (func)
		return;

//	audiopci_log("ES1371 PCI write %04X %02X PC=%08x\n", addr, val, cpu_state.pc);

	switch (addr)
	{
		case 0x04:
		if (es1371->pci_command & PCI_COMMAND_IO)
			io_removehandler(es1371->base_addr, 0x0040, es1371_inb, es1371_inw, es1371_inl, es1371_outb, es1371_outw, es1371_outl, es1371);
		es1371->pci_command = val & 0x05;
		if (es1371->pci_command & PCI_COMMAND_IO)
			io_sethandler(es1371->base_addr, 0x0040, es1371_inb, es1371_inw, es1371_inl, es1371_outb, es1371_outw, es1371_outl, es1371);
		break;
		case 0x05:
		es1371->pci_serr = val & 1;
		break;
		
		case 0x10:
		if (es1371->pci_command & PCI_COMMAND_IO)
			io_removehandler(es1371->base_addr, 0x0040, es1371_inb, es1371_inw, es1371_inl, es1371_outb, es1371_outw, es1371_outl, es1371);
		es1371->base_addr = (es1371->base_addr & 0xffffff00) | (val & 0xc0);
		if (es1371->pci_command & PCI_COMMAND_IO)
			io_sethandler(es1371->base_addr, 0x0040, es1371_inb, es1371_inw, es1371_inl, es1371_outb, es1371_outw, es1371_outl, es1371);
		break;
		case 0x11:
		if (es1371->pci_command & PCI_COMMAND_IO)
			io_removehandler(es1371->base_addr, 0x0040, es1371_inb, es1371_inw, es1371_inl, es1371_outb, es1371_outw, es1371_outl, es1371);
		es1371->base_addr = (es1371->base_addr & 0xffff00c0) | (val << 8);
		if (es1371->pci_command & PCI_COMMAND_IO)
			io_sethandler(es1371->base_addr, 0x0040, es1371_inb, es1371_inw, es1371_inl, es1371_outb, es1371_outw, es1371_outl, es1371);
		break;
		case 0x12:
		es1371->base_addr = (es1371->base_addr & 0xff00ffc0) | (val << 16);
		break;
		case 0x13:
		es1371->base_addr = (es1371->base_addr & 0x00ffffc0) | (val << 24);
		break;

		case 0x3c:
		es1371->int_line = val;
		break;

		case 0xe0:
		es1371->pmcsr = (es1371->pmcsr & 0xff00) | (val & 0x03);
		break;
		case 0xe1:
		es1371->pmcsr = (es1371->pmcsr & 0x00ff) | ((val & 0x01) << 8);
		break;
	}
//	audiopci_log("es1371->base_addr %08x\n", es1371->base_addr);
}

static void es1371_fetch(es1371_t *es1371, int dac_nr)
{
	int format = dac_nr ? ((es1371->si_cr >> 2) & 3) : (es1371->si_cr & 3);
	int pos = es1371->dac[dac_nr].buffer_pos & 63;
	int c;

//audiopci_log("Fetch format=%i %08x %08x  %08x %08x  %08x\n", format, es1371->dac[dac_nr].count, es1371->dac[dac_nr].size,  es1371->dac[dac_nr].curr_samp_ct,es1371->dac[dac_nr].samp_ct, es1371->dac[dac_nr].addr);
	switch (format)
	{
		case FORMAT_MONO_8:
		for (c = 0; c < 32; c += 4)
		{
			es1371->dac[dac_nr].buffer_l[(pos+c)   & 63] = es1371->dac[dac_nr].buffer_r[(pos+c)   & 63] = (mem_readb_phys(es1371->dac[dac_nr].addr) ^ 0x80) << 8;
			es1371->dac[dac_nr].buffer_l[(pos+c+1) & 63] = es1371->dac[dac_nr].buffer_r[(pos+c+1) & 63] = (mem_readb_phys(es1371->dac[dac_nr].addr+1) ^ 0x80) << 8;
			es1371->dac[dac_nr].buffer_l[(pos+c+2) & 63] = es1371->dac[dac_nr].buffer_r[(pos+c+2) & 63] = (mem_readb_phys(es1371->dac[dac_nr].addr+2) ^ 0x80) << 8;
			es1371->dac[dac_nr].buffer_l[(pos+c+3) & 63] = es1371->dac[dac_nr].buffer_r[(pos+c+3) & 63] = (mem_readb_phys(es1371->dac[dac_nr].addr+3) ^ 0x80) << 8;
			es1371->dac[dac_nr].addr += 4;
	
			es1371->dac[dac_nr].buffer_pos_end += 4;
			es1371->dac[dac_nr].count++;
			if (es1371->dac[dac_nr].count > es1371->dac[dac_nr].size)
			{
				es1371->dac[dac_nr].count = 0;
				es1371->dac[dac_nr].addr = es1371->dac[dac_nr].addr_latch;
				break;
			}
		}
		break;
		case FORMAT_STEREO_8:
		for (c = 0; c < 16; c += 2)
		{
			es1371->dac[dac_nr].buffer_l[(pos+c)   & 63] = (mem_readb_phys(es1371->dac[dac_nr].addr) ^ 0x80) << 8;
			es1371->dac[dac_nr].buffer_r[(pos+c)   & 63] = (mem_readb_phys(es1371->dac[dac_nr].addr + 1) ^ 0x80) << 8;
			es1371->dac[dac_nr].buffer_l[(pos+c+1) & 63] = (mem_readb_phys(es1371->dac[dac_nr].addr + 2) ^ 0x80) << 8;
			es1371->dac[dac_nr].buffer_r[(pos+c+1) & 63] = (mem_readb_phys(es1371->dac[dac_nr].addr + 3) ^ 0x80) << 8;
			es1371->dac[dac_nr].addr += 4;
	
			es1371->dac[dac_nr].buffer_pos_end += 2;
			es1371->dac[dac_nr].count++;
			if (es1371->dac[dac_nr].count > es1371->dac[dac_nr].size)
			{
				es1371->dac[dac_nr].count = 0;
				es1371->dac[dac_nr].addr = es1371->dac[dac_nr].addr_latch;
				break;
			}
		}
		break;
		case FORMAT_MONO_16:
		for (c = 0; c < 16; c += 2)
		{
			es1371->dac[dac_nr].buffer_l[(pos+c)   & 63] = es1371->dac[dac_nr].buffer_r[(pos+c)   & 63] = mem_readw_phys(es1371->dac[dac_nr].addr);
			es1371->dac[dac_nr].buffer_l[(pos+c+1) & 63] = es1371->dac[dac_nr].buffer_r[(pos+c+1) & 63] = mem_readw_phys(es1371->dac[dac_nr].addr + 2);
			es1371->dac[dac_nr].addr += 4;

			es1371->dac[dac_nr].buffer_pos_end += 2;
			es1371->dac[dac_nr].count++;
			if (es1371->dac[dac_nr].count > es1371->dac[dac_nr].size)
			{
				es1371->dac[dac_nr].count = 0;
				es1371->dac[dac_nr].addr = es1371->dac[dac_nr].addr_latch;
				break;
			}
		}
		break;
		case FORMAT_STEREO_16:
		for (c = 0; c < 4; c++)
		{
			es1371->dac[dac_nr].buffer_l[(pos+c) & 63] = mem_readw_phys(es1371->dac[dac_nr].addr);
			es1371->dac[dac_nr].buffer_r[(pos+c) & 63] = mem_readw_phys(es1371->dac[dac_nr].addr + 2);
//			audiopci_log("Fetch %02x %08x  %04x %04x\n", (pos+c) & 63, es1371->dac[dac_nr].addr, es1371->dac[dac_nr].buffer_l[(pos+c) & 63], es1371->dac[dac_nr].buffer_r[(pos+c) & 63]);
			es1371->dac[dac_nr].addr += 4;
	
			es1371->dac[dac_nr].buffer_pos_end++;
			es1371->dac[dac_nr].count++;
			if (es1371->dac[dac_nr].count > es1371->dac[dac_nr].size)
			{
				es1371->dac[dac_nr].count = 0;
				es1371->dac[dac_nr].addr = es1371->dac[dac_nr].addr_latch;
				break;
			}
		}
		break;
	}
}

static inline float low_fir_es1371(int dac_nr, int i, float NewSample)
{
        static float x[2][2][128]; //input samples
        static int x_pos[2] = {0, 0};
        float out = 0.0;
	int read_pos;
	int n_coef;
	int pos = x_pos[dac_nr];

        x[dac_nr][i][pos] = NewSample;

        /*Since only 1/16th of input samples are non-zero, only filter those that
          are valid.*/
	read_pos = (pos + 15) & (127 & ~15);
	n_coef = (16 - pos) & 15;

	while (n_coef < ES1371_NCoef)
	{
		out += low_fir_es1371_coef[n_coef] * x[dac_nr][i][read_pos];
		read_pos = (read_pos + 16) & (127 & ~15);
		n_coef += 16;
	}

        if (i == 1)
        {
        	x_pos[dac_nr] = (x_pos[dac_nr] + 1) & 127;
        	if (x_pos[dac_nr] > 127)
        		x_pos[dac_nr] = 0;
        }

        return out;
}

static void es1371_next_sample_filtered(es1371_t *es1371, int dac_nr, int out_idx)
{
        int out_l, out_r;
        int c;
        
	if ((es1371->dac[dac_nr].buffer_pos - es1371->dac[dac_nr].buffer_pos_end) >= 0)
	{
		es1371_fetch(es1371, dac_nr);
	}

        out_l = es1371->dac[dac_nr].buffer_l[es1371->dac[dac_nr].buffer_pos & 63];
        out_r = es1371->dac[dac_nr].buffer_r[es1371->dac[dac_nr].buffer_pos & 63];
        
        es1371->dac[dac_nr].filtered_l[out_idx] = (int)low_fir_es1371(dac_nr, 0, (float)out_l);
        es1371->dac[dac_nr].filtered_r[out_idx] = (int)low_fir_es1371(dac_nr, 1, (float)out_r);
        for (c = 1; c < 16; c++)
        {
                es1371->dac[dac_nr].filtered_l[out_idx+c] = (int)low_fir_es1371(dac_nr, 0, 0);
                es1371->dac[dac_nr].filtered_r[out_idx+c] = (int)low_fir_es1371(dac_nr, 1, 0);
        }
        
//	audiopci_log("Use %02x %04x %04x\n", es1371->dac[dac_nr].buffer_pos & 63, es1371->dac[dac_nr].out_l, es1371->dac[dac_nr].out_r);
	
	es1371->dac[dac_nr].buffer_pos++;
//	audiopci_log("Next sample %08x %08x  %08x\n", es1371->dac[dac_nr].buffer_pos, es1371->dac[dac_nr].buffer_pos_end, es1371->dac[dac_nr].curr_samp_ct);
}

//static FILE *es1371_f;//,*es1371_f2;

static void es1371_update(es1371_t *es1371)
{
        int32_t l, r;
                                
        l = (es1371->dac[0].out_l * es1371->dac[0].vol_l) >> 12;
        l += ((es1371->dac[1].out_l * es1371->dac[1].vol_l) >> 12);
        r = (es1371->dac[0].out_r * es1371->dac[0].vol_r) >> 12;
        r += ((es1371->dac[1].out_r * es1371->dac[1].vol_r) >> 12);
                
        l >>= 1;
        r >>= 1;
                
        l = (l * es1371->master_vol_l) >> 15;
        r = (r * es1371->master_vol_r) >> 15;
                                
        if (l < -32768)
                l = -32768;
        else if (l > 32767)
                l = 32767;
        if (r < -32768)
                r = -32768;
        else if (r > 32767)
                r = 32767;

        for (; es1371->pos < sound_pos_global; es1371->pos++)
        {                                        
                es1371->buffer[es1371->pos*2]     = l;
                es1371->buffer[es1371->pos*2 + 1] = r;
        }
}

static void es1371_poll(void *p)
{
	es1371_t *es1371 = (es1371_t *)p;
	
	timer_advance_u64(&es1371->dac[1].timer, es1371->dac[1].latch);

	es1371_update(es1371);		

	if (es1371->int_ctrl & INT_UART_EN) {
		audiopci_log("UART INT Enabled\n");
		if (es1371->uart_ctrl & UART_CTRL_RXINTEN) { /*We currently don't implement MIDI Input.*/
			/*But if anything sets MIDI Input and Output together we'd have to take account
			of the MIDI Output case, and disable IRQ's and RX bits when MIDI Input is enabled as well but not in the MIDI Output portion*/
			if (es1371->uart_ctrl & UART_CTRL_TXINTEN) 
				es1371->int_status |= INT_STATUS_UART;
			else
				es1371->int_status &= ~INT_STATUS_UART;
		} else if (!(es1371->uart_ctrl & UART_CTRL_RXINTEN) && ((es1371->uart_ctrl & UART_CTRL_TXINTEN))) { /*Or enable the UART IRQ and the respective TX bits only when the MIDI Output is enabled*/
			es1371->int_status |= INT_STATUS_UART;
		}
		
		if (es1371->uart_ctrl & UART_CTRL_RXINTEN) {
			if (es1371->uart_ctrl & UART_CTRL_TXINTEN) 
				es1371->uart_status |= (UART_STATUS_TXINT | UART_STATUS_TXRDY);
			else
				es1371->uart_status &= ~(UART_STATUS_TXINT | UART_STATUS_TXRDY);
		} else
			es1371->uart_status |= (UART_STATUS_TXINT | UART_STATUS_TXRDY);
		
		es1371_update_irqs(es1371);
	}		
		
	if (es1371->int_ctrl & INT_DAC1_EN) {
                int frac = es1371->dac[0].ac & 0x7fff;
                int idx = es1371->dac[0].ac >> 15;
                int samp1_l = es1371->dac[0].filtered_l[idx];
                int samp1_r = es1371->dac[0].filtered_r[idx];
                int samp2_l = es1371->dac[0].filtered_l[(idx + 1) & 31];
                int samp2_r = es1371->dac[0].filtered_r[(idx + 1) & 31];
                
                es1371->dac[0].out_l = ((samp1_l * (0x8000 - frac)) + (samp2_l * frac)) >> 15;
                es1371->dac[0].out_r = ((samp1_r * (0x8000 - frac)) + (samp2_r * frac)) >> 15;
                es1371->dac[0].ac += es1371->dac[0].vf;
                es1371->dac[0].ac &= ((32 << 15) - 1);
                if ((es1371->dac[0].ac >> (15+4)) != es1371->dac[0].f_pos)
		{
                        es1371_next_sample_filtered(es1371, 0, es1371->dac[0].f_pos ? 16 : 0);
                        es1371->dac[0].f_pos = (es1371->dac[0].f_pos + 1) & 1;

                        es1371->dac[0].curr_samp_ct--;
                        if (es1371->dac[0].curr_samp_ct < 0)
			{
				es1371->int_status |= INT_STATUS_DAC1;
				es1371_update_irqs(es1371);
				es1371->dac[0].curr_samp_ct = es1371->dac[0].samp_ct;
			}
		}
	}

	if (es1371->int_ctrl & INT_DAC2_EN)
	{
                int frac = es1371->dac[1].ac & 0x7fff;
                int idx = es1371->dac[1].ac >> 15;
                int samp1_l = es1371->dac[1].filtered_l[idx];
                int samp1_r = es1371->dac[1].filtered_r[idx];
                int samp2_l = es1371->dac[1].filtered_l[(idx + 1) & 31];
                int samp2_r = es1371->dac[1].filtered_r[(idx + 1) & 31];
                
                es1371->dac[1].out_l = ((samp1_l * (0x8000 - frac)) + (samp2_l * frac)) >> 15;
                es1371->dac[1].out_r = ((samp1_r * (0x8000 - frac)) + (samp2_r * frac)) >> 15;
		es1371->dac[1].ac += es1371->dac[1].vf;
                es1371->dac[1].ac &= ((32 << 15) - 1);
                if ((es1371->dac[1].ac >> (15+4)) != es1371->dac[1].f_pos)
		{
                        es1371_next_sample_filtered(es1371, 1, es1371->dac[1].f_pos ? 16 : 0);
                        es1371->dac[1].f_pos = (es1371->dac[1].f_pos + 1) & 1;

                        es1371->dac[1].curr_samp_ct--;
                        if (es1371->dac[1].curr_samp_ct < 0)
			{
				es1371->int_status |= INT_STATUS_DAC2;
				es1371_update_irqs(es1371);
                                es1371->dac[1].curr_samp_ct = es1371->dac[1].samp_ct;
			}
		}
	}
}

static void es1371_get_buffer(int32_t *buffer, int len, void *p)
{
	es1371_t *es1371 = (es1371_t *)p;
	int c;

        es1371_update(es1371);

	for (c = 0; c < len * 2; c++)
		buffer[c] += (es1371->buffer[c] / 2);
	
	es1371->pos = 0;
}

static void es1371_filter_cd_audio(int channel, double *buffer, void *p)
{
	es1371_t *es1371 = (es1371_t *)p;
	int32_t c;
	int cd = channel ? es1371->cd_vol_r : es1371->cd_vol_l;
	int master = channel ? es1371->master_vol_r : es1371->master_vol_l;

        c = (((int32_t) *buffer) * cd) >> 15;
	c = (c * master) >> 15;

	*buffer     = (double) c;
}

static inline double sinc(double x)
{
	return sin(M_PI * x) / (M_PI * x);
}

static void generate_es1371_filter()
{
        /*Cutoff frequency = 1 / 32*/
        float fC = 1.0 / 32.0;
        float gain;
        int n;
        
        for (n = 0; n < ES1371_NCoef; n++)
        {
                /*Blackman window*/
                double w = 0.42 - (0.5 * cos((2.0*n*M_PI)/(double)(ES1371_NCoef-1))) + (0.08 * cos((4.0*n*M_PI)/(double)(ES1371_NCoef-1)));
                /*Sinc filter*/
                double h = sinc(2.0 * fC * ((double)n - ((double)(ES1371_NCoef-1) / 2.0)));
                
                /*Create windowed-sinc filter*/
                low_fir_es1371_coef[n] = w * h;
        }
        
        low_fir_es1371_coef[(ES1371_NCoef - 1) / 2] = 1.0;

        gain = 0.0;
        for (n = 0; n < ES1371_NCoef; n++)
                gain += low_fir_es1371_coef[n] / (float)N;

	gain /= 0.95;

        /*Normalise filter, to produce unity gain*/
        for (n = 0; n < ES1371_NCoef; n++)
                low_fir_es1371_coef[n] /= gain;
}        

static void *es1371_init(const device_t *info)
{
	es1371_t *es1371 = malloc(sizeof(es1371_t));
	memset(es1371, 0, sizeof(es1371_t));
		
	sound_add_handler(es1371_get_buffer, es1371);
	sound_set_cd_audio_filter(es1371_filter_cd_audio, es1371);

	/* Add our own always-present game port to override the standalone ISAPnP one. */
	gameport_remap(gameport_add(&gameport_pnp_device), 0x200);

	es1371->card = pci_add_card(info->local ? PCI_ADD_SOUND : PCI_ADD_NORMAL, es1371_pci_read, es1371_pci_write, es1371);
	
	timer_add(&es1371->dac[1].timer, es1371_poll, es1371, 1); 
        
        generate_es1371_filter();

	ac97_codec = &es1371->codec;
	ac97_codec_count = 1;
	ac97_codec_id = 0;
	if (!info->local) /* let the machine decide the codec on onboard implementations */
		device_add(&cs4297a_device);

	return es1371;
}

static void es1371_close(void *p)
{
	es1371_t *es1371 = (es1371_t *)p;
	
	free(es1371);
}

static void es1371_speed_changed(void *p)
{
	es1371_t *es1371 = (es1371_t *)p;
	
	es1371->dac[1].latch = (uint64_t)((double)TIMER_USEC * (1000000.0 / 48000.0));
}
 
void es1371_add_status_info_dac(es1371_t *es1371, char *s, int max_len, int dac_nr)
{
        int ena = dac_nr ? INT_DAC2_EN : INT_DAC1_EN;
        char *dac_name = dac_nr ? "DAC2 (Wave)" : "DAC1 (MIDI)";
        char temps[128];

        if (es1371->int_ctrl & ena)
        {
                int format = dac_nr ? ((es1371->si_cr >> 2) & 3) : (es1371->si_cr & 3);
                double freq = 48000.0 * ((double)es1371->dac[dac_nr].vf / (32768.0 * 16.0));

                switch (format)
                {
                        case FORMAT_MONO_8:
                        snprintf(temps, 128, "%s format : 8-bit mono\n", dac_name);
                        break;
                        case FORMAT_STEREO_8:
                        snprintf(temps, 128, "%s format : 8-bit stereo\n", dac_name);
                        break;
                        case FORMAT_MONO_16:
                        snprintf(temps, 128, "%s format : 16-bit mono\n", dac_name);
                        break;
                        case FORMAT_STEREO_16:
                        snprintf(temps, 128, "%s format : 16-bit stereo\n", dac_name);
                        break;
                }
                
                strncat(s, temps, max_len);
                max_len -= strlen(temps);

                snprintf(temps, 128, "Playback frequency : %i Hz\n", (int)freq);
                strncat(s, temps, max_len);
        }
        else
        {
                snprintf(temps, max_len, "%s stopped\n", dac_name);
                strncat(s, temps, max_len);
        }
}

const device_t es1371_device =
{
    "Ensoniq AudioPCI (ES1371)",
    DEVICE_PCI,
    0,
    es1371_init,
    es1371_close,
    NULL,
    { NULL },
    es1371_speed_changed,
    NULL,
    NULL
};

const device_t es1371_onboard_device =
{
    "Ensoniq AudioPCI (ES1371) (On-Board)",
    DEVICE_PCI,
    1,
    es1371_init,
    es1371_close,
    NULL,
    { NULL },
    es1371_speed_changed,
    NULL,
    NULL
};
