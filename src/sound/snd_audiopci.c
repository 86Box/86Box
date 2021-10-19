/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Ensoniq AudioPCI (ES1371) emulation.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		RichardG, <richardg867@gmail.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2008-2021 Sarah Walker.
 *		Copyright 2021 RichardG.
 *		Copyright 2021 Miran Grca.
 */
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
    uint8_t		pci_command, pci_serr;

    uint32_t		base_addr;

    uint8_t		int_line;

    uint16_t		pmcsr;

    uint32_t		int_ctrl, int_status,
			legacy_ctrl;

    int			mem_page;

    uint32_t		si_cr;

    uint32_t		sr_cir;
    uint16_t		sr_ram[128];

    uint8_t		uart_ctrl, uart_status,
			uart_res;
    uint32_t		uart_fifo;

    ac97_codec_t *	codec;
    uint32_t		codec_ctrl;

    struct {
	uint32_t		addr, addr_latch;
	uint16_t		count, size;

	uint16_t		samp_ct;
	int			curr_samp_ct;

	pc_timer_t		timer;
	uint64_t		latch;

	uint32_t		vf, ac;

	int16_t			buffer_l[64], buffer_r[64];
	int			buffer_pos, buffer_pos_end;

	int			filtered_l[32], filtered_r[32];
	int			f_pos;

	int16_t			out_l, out_r;

	int32_t			vol_l, vol_r;
    } dac[2], adc;

    int64_t		dac_latch, dac_time;

    int			master_vol_l, master_vol_r,
			cd_vol_l, cd_vol_r;

    int			card;

    int			pos;
    int16_t		buffer[SOUNDBUFLEN * 2];

    int			type;
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

#define SI_P2_PAUSE			(1<<12)
#define SI_P1_PAUSE			(1<<11)
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


static void es1371_fetch(es1371_t *dev, int dac_nr);
static void update_legacy(es1371_t *dev, uint32_t old_legacy_ctrl);


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


static void
es1371_update_irqs(es1371_t *dev)
{
    int irq = 0;

    if ((dev->int_status & INT_STATUS_DAC1) && (dev->si_cr & SI_P1_INTR_EN))
	irq = 1;
    if ((dev->int_status & INT_STATUS_DAC2) && (dev->si_cr & SI_P2_INTR_EN))
	irq = 1;

    /* MIDI input is unsupported for now */
    if ((dev->int_status & INT_STATUS_UART) && (dev->uart_status & UART_STATUS_TXINT))
	irq = 1;

    if (irq)
	dev->int_status |= INT_STATUS_INTR;
    else
	dev->int_status &= ~INT_STATUS_INTR;

    if (dev->legacy_ctrl & LEGACY_FORCE_IRQ)
	irq = 1;

    if (irq)
	pci_set_irq(dev->card, PCI_INTA);
    else
	pci_clear_irq(dev->card, PCI_INTA);
}


static void
es1371_reset(void *p)
{
    es1371_t *dev = (es1371_t *) p;

    nmi = 0;

    /* Interrupt/Chip Select Control Register, Address 00H
       Addressable as byte, word, longword */
    dev->int_ctrl = 0xfc0f0000;

    /* Interrupt/Chip Select Control Register, Address 00H
       Addressable as longword only */
    dev->int_status = 0x7ffffec0;

    /* UART Status Register, Address 09H
       Addressable as byte only */
    dev->uart_status = 0x00;

    /* UART Control Register, Address 09H
       Addressable as byte only */
    dev->uart_ctrl = 0x00;

    /* UART Reserved Register, Address 0AH
       Addressable as byte only */
    dev->uart_res = 0x00;

    /* Memory Page Register, Address 0CH
       Addressable as byte, word, longword */
    dev->mem_page = 0x00;

    /* Sample Rate Converter Interface Register, Address 10H
       Addressable as longword only */
    dev->sr_cir = 0x00000000;

    /* CODEC Write Register, Address 14H
       Addressable as longword only */
    dev->codec_ctrl = 0x00000000;

    /* Legacy Control/Status Register, Address 18H
       Addressable as byte, word, longword */
    dev->legacy_ctrl = 0x0000f800;

    /* Serial Interface Control Register, Address 20H
       Addressable as byte, word, longword */
    dev->si_cr = 0xff800000;

    /* DAC1 Channel Sample Count Register, Address 24H
       Addressable as word, longword */
    dev->dac[0].samp_ct = 0x00000000;
    dev->dac[0].curr_samp_ct = 0x00000000;

    /* DAC2 Channel Sample Count Register, Address 28H
       Addressable as word, longword */
    dev->dac[1].samp_ct = 0x00000000;
    dev->dac[1].curr_samp_ct = 0x00000000;

    /* ADC Channel Sample Count Register, Address 2CH
       Addressable as word, longword */
    dev->adc.samp_ct = 0x00000000;
    dev->adc.curr_samp_ct = 0x00000000;

    /* DAC1 Frame Register 1, Address 30H, Memory Page 1100b
       Addressable as longword only */
    dev->dac[0].addr_latch = 0x00000000;

    /* DAC1 Frame Register 2, Address 34H, Memory Page 1100b
       Addressable as longword only */
    dev->dac[0].size = 0x00000000;
    dev->dac[0].count = 0x00000000;

    /* DAC2 Frame Register 1, Address 38H, Memory Page 1100b
       Addressable as longword only */
    dev->dac[1].addr_latch = 0x00000000;

    /* DAC2 Frame Register 2, Address 3CH, Memory Page 1100b
       Addressable as longword only */
    dev->dac[1].size = 0x00000000;
    dev->dac[1].count = 0x00000000;

    /* ADC Frame Register 1, Address 30H, Memory Page 1101b
       Addressable as longword only */
    dev->adc.addr_latch = 0x00000000;

    /* ADC Frame Register 2, Address 34H, Memory Page 1101b
       Addressable as longword only */
    dev->adc.size = 0x00000000;
    dev->adc.count = 0x00000000;

    /* UART FIFO Register, Address 30H, 34H, 38H, 3CH, Memory Page 1110b, 1111b
       Addressable as longword only */
    dev->uart_fifo = 0xfffffe00;
}


static uint32_t
es1371_read_frame_reg(es1371_t *dev, int frame, int page)
{
    uint32_t ret = 0xffffffff;

    switch (frame) {
	case 0x30:
		switch (page) {
			/* DAC1 Frame Register 1, Address 30H, Memory Page 1100b
			   Addressable as longword only */
			case 0xc:
				ret = dev->dac[0].addr_latch;
				break;
			/* ADC Frame Register 1, Address 30H, Memory Page 1101b
			   Addressable as longword only */
			case 0xd:
				ret = dev->adc.addr_latch;
				break;
			/* UART FIFO Register, Address 30H, 34H, 38H, 3CH, Memory Page 1110b, 1111b
			   Addressable as longword only */
			case 0xe: case 0xf:
				ret = dev->uart_fifo;
				break;
		}
		break;
	case 0x34:
		switch (page) {
			/* DAC1 Frame Register 2, Address 34H, Memory Page 1100b
			   Addressable as longword only */
			case 0xc:
				ret = dev->dac[0].size | (dev->dac[0].count << 16);
				break;
			/* ADC Frame Register 2, Address 34H, Memory Page 1101b
			   Addressable as longword only */
			case 0xd:
				ret = dev->adc.size | (dev->adc.count << 16);
				break;
			/* UART FIFO Register, Address 30H, 34H, 38H, 3CH, Memory Page 1110b, 1111b
			   Addressable as longword only */
			case 0xe: case 0xf:
				ret = dev->uart_fifo;
				break;
		}
		break;
	case 0x38:
		switch (page) {
			/* DAC2 Frame Register 1, Address 38H, Memory Page 1100b
			   Addressable as longword only */
			case 0xc:
				ret = dev->dac[1].addr_latch;
				break;
			/* UART FIFO Register, Address 30H, 34H, 38H, 3CH, Memory Page 1110b, 1111b
			   Addressable as longword only */
			case 0xe: case 0xf:
				ret = dev->uart_fifo;
				break;
		}
		break;
	case 0x3c:
		switch (page) {
			/* DAC2 Frame Register 2, Address 3CH, Memory Page 1100b
			   Addressable as longword only */
			case 0xc:
				ret = dev->dac[1].size | (dev->dac[1].count << 16);
				break;
			/* UART FIFO Register, Address 30H, 34H, 38H, 3CH, Memory Page 1110b, 1111b
			   Addressable as longword only */
			case 0xe: case 0xf:
				ret = dev->uart_fifo;
				break;
		}
		break;
    }

    return ret;
}


static void
es1371_write_frame_reg(es1371_t *dev, int frame, int page, uint32_t val)
{
    switch (frame) {
	case 0x30:
		switch (page) {
			/* DAC1 Frame Register 1, Address 30H, Memory Page 1100b
			   Addressable as longword only */
			case 0xc:
				dev->dac[0].addr_latch = val;
				break;
			/* ADC Frame Register 1, Address 30H, Memory Page 1101b
			   Addressable as longword only */
			case 0xd:
				dev->adc.addr_latch = val;
				break;
			/* UART FIFO Register, Address 30H, 34H, 38H, 3CH, Memory Page 1110b, 1111b
			   Addressable as longword only */
			case 0xe: case 0xf:
				dev->uart_fifo = (dev->uart_fifo & 0xfffffe00) | (val & 0x000001ff);
				break;
		}
		break;
	case 0x34:
		switch (page) {
			/* DAC1 Frame Register 2, Address 34H, Memory Page 1100b
			   Addressable as longword only */
			case 0xc:
				dev->dac[0].size = val & 0xffff;
				dev->dac[0].count = val >> 16;
				break;
			/* ADC Frame Register 2, Address 34H, Memory Page 1101b
			   Addressable as longword only */
			case 0xd:
				dev->adc.size = val & 0xffff;
				dev->adc.count = val >> 16;
				break;
			/* UART FIFO Register, Address 30H, 34H, 38H, 3CH, Memory Page 1110b, 1111b
			   Addressable as longword only */
			case 0xe: case 0xf:
				dev->uart_fifo = (dev->uart_fifo & 0xfffffe00) | (val & 0x000001ff);
				break;
		}
		break;
	case 0x38:
		switch (page) {
			/* DAC2 Frame Register 1, Address 38H, Memory Page 1100b
			   Addressable as longword only */
			case 0xc:
				dev->dac[1].addr_latch = val;
				break;
			/* UART FIFO Register, Address 30H, 34H, 38H, 3CH, Memory Page 1110b, 1111b
			   Addressable as longword only */
			case 0xe: case 0xf:
				dev->uart_fifo = (dev->uart_fifo & 0xfffffe00) | (val & 0x000001ff);
				break;
		}
		break;
	case 0x3c:
		switch (page) {
			/* DAC2 Frame Register 2, Address 3CH, Memory Page 1100b
			   Addressable as longword only */
			case 0xc:
				dev->dac[1].size = val & 0xffff;
				dev->dac[1].count = val >> 16;
				break;
			/* UART FIFO Register, Address 30H, 34H, 38H, 3CH, Memory Page 1110b, 1111b
			   Addressable as longword only */
			case 0xe: case 0xf:
				dev->uart_fifo = (dev->uart_fifo & 0xfffffe00) | (val & 0x000001ff);
				break;
		}
		break;
    }
}


static uint8_t
es1371_inb(uint16_t port, void *p)
{
    es1371_t *dev = (es1371_t *) p;
    uint8_t ret = 0xff;

    switch (port & 0x3f) {
	/* Interrupt/Chip Select Control Register, Address 00H
	   Addressable as byte, word, longword */
	case 0x00:
		ret = dev->int_ctrl & 0xff;
		break;
	case 0x01:
		ret = (dev->int_ctrl >> 8) & 0xff;
		break;
	case 0x02:
		ret = (dev->int_ctrl >> 16) & 0x0f;
		break;
	case 0x03:
		ret = ((dev->int_ctrl >> 24) & 0x03) | 0xfc;
		break;

	/* Interrupt/Chip Select Status Register, Address 04H
	   Addressable as longword only, but PCem implements byte access, which
	   must be for a reason */
	case 0x04:
		ret = dev->int_status & 0xff;
		break;
	case 0x05:
		ret = (dev->int_status >> 8) & 0xff;
		break;
	case 0x06:
		ret = (dev->int_status >> 16) & 0x0f;
		break;
	case 0x07:
		ret = ((dev->int_status >> 24) & 0x03) | 0xfc;
		break;

	/* UART Data Register, Address 08H
	   Addressable as byte only */
	case 0x08:
		ret = 0x00;
		break;

	/* UART Status Register, Address 09H
	   Addressable as byte only */
	case 0x09:
		ret = dev->uart_status & 0x87;
		audiopci_log("ES1371 UART Status = %02x\n", dev->uart_status);
		break;

	/* UART Reserved Register, Address 0AH
	   Addressable as byte only */
	case 0x0a:
		ret = dev->uart_res & 0x01;
		break;

	/* Memory Page Register, Address 0CH
	   Addressable as byte, word, longword */
	case 0x0c:
		ret = dev->mem_page;
		break;
	case 0x0d ... 0x0e:
		ret = 0x00;
		break;
		
	/* Legacy Control/Status Register, Address 18H
	   Addressable as byte, word, longword */
	case 0x18:
		ret = dev->legacy_ctrl & 0xfd;
		break;
	case 0x19:
		ret = ((dev->legacy_ctrl >> 8) & 0x07) | 0xf8;
		break;
	case 0x1a:
		ret = dev->legacy_ctrl >> 16;
		break;
	case 0x1b:
		ret = dev->legacy_ctrl >> 24;
		break;

	/* Serial Interface Control Register, Address 20H
	    Addressable as byte, word, longword */
	case 0x20:
		ret = dev->si_cr & 0xff;
		break;
	case 0x21:
		ret = dev->si_cr >> 8;
		break;
	case 0x22:
		ret = (dev->si_cr >> 16) | 0x80;
		break;
	case 0x23:		
		ret = 0xff;
		break;
		
	default:
		audiopci_log("Bad es1371_inb: port=%04x\n", port);
    }

    audiopci_log("es1371_inb: port=%04x ret=%02x\n", port, ret);
    return ret;
}


static uint16_t
es1371_inw(uint16_t port, void *p)
{
    es1371_t *dev = (es1371_t *) p;
    uint16_t ret = 0xffff;

    switch (port & 0x3e) {
	/* Interrupt/Chip Select Control Register, Address 00H
	   Addressable as byte, word, longword */
	case 0x00:
		ret = dev->int_ctrl & 0xffff;
		break;
	case 0x02:
		ret = ((dev->int_ctrl >> 16) & 0x030f) | 0xfc00;
		break;

	/* Memory Page Register, Address 0CH
	   Addressable as byte, word, longword */
	case 0x0c:
		ret = dev->mem_page;
		break;
	case 0x0e:
		ret = 0x0000;
		break;

	/* Legacy Control/Status Register, Address 18H
	   Addressable as byte, word, longword */
	case 0x18:
		ret = (dev->legacy_ctrl & 0x07fd) | 0xf800;
		break;
	case 0x1a:
		ret = dev->legacy_ctrl >> 16;
		break;

	/* Serial Interface Control Register, Address 20H
	    Addressable as byte, word, longword */
	case 0x20:
		ret = dev->si_cr & 0xffff;
		break;
	case 0x22:
		ret = (dev->si_cr >> 16) | 0xff80;
		break;

	/* DAC1 Channel Sample Count Register, Address 24H
	   Addressable as word, longword */
	case 0x24:
		ret = dev->dac[0].samp_ct;
		break;
	case 0x26:
		ret = dev->dac[0].curr_samp_ct;
		break;

	/* DAC2 Channel Sample Count Register, Address 28H
	   Addressable as word, longword */
	case 0x28:
		ret = dev->dac[1].samp_ct;
		break;
	case 0x2a:
		ret = dev->dac[1].curr_samp_ct;
		break;

	/* ADC Channel Sample Count Register, Address 2CH
	   Addressable as word, longword */
	case 0x2c:
		ret = dev->adc.samp_ct;
		break;
	case 0x2e:
		ret = dev->adc.curr_samp_ct;
		break;

	case 0x30: case 0x34: case 0x38: case 0x3c:
		ret = es1371_read_frame_reg(dev, port & 0x3c, dev->mem_page) & 0xffff;
		break;
	case 0x32: case 0x36: case 0x3a: case 0x3e:
		ret = es1371_read_frame_reg(dev, port & 0x3c, dev->mem_page) >> 16;
		break;
    }

    return ret;
}


static uint32_t
es1371_inl(uint16_t port, void *p)
{
    es1371_t *dev = (es1371_t *) p;
    uint32_t ret = 0xffffffff;

    switch (port & 0x3c) {
	/* Interrupt/Chip Select Control Register, Address 00H
	   Addressable as byte, word, longword */
	case 0x00:
		ret = (dev->int_ctrl & 0x030fffff) | 0xfc000000;
		break;

	/* Interrupt/Chip Select Status Register, Address 04H
	   Addressable as longword only */
	case 0x04:
		ret = dev->int_status;
		break;

	/* Memory Page Register, Address 0CH
	   Addressable as byte, word, longword */
	case 0x0c:
		ret = dev->mem_page;
		break;

	/* Sample Rate Converter Interface Register, Address 10H
	   Addressable as longword only */
	case 0x10:
		ret = dev->sr_cir & ~0xffff;
		ret |= dev->sr_ram[dev->sr_cir >> 25];
		break;
		
	/* CODEC Read Register, Address 14H
	   Addressable as longword only */
	case 0x14:
		ret = dev->codec_ctrl | CODEC_READY;
		break;

	/* Legacy Control/Status Register, Address 18H
	   Addressable as byte, word, longword */
	case 0x18:
		ret = (dev->legacy_ctrl & 0xffff07fd) | 0x0000f800;
		break;

	/* Serial Interface Control Register, Address 20H
	    Addressable as byte, word, longword */
	case 0x20:
		ret = dev->si_cr | 0xff800000;
		break;

	/* DAC1 Channel Sample Count Register, Address 24H
	   Addressable as word, longword */
	case 0x24:
		ret = dev->dac[0].samp_ct | (((uint32_t) dev->dac[0].curr_samp_ct) << 16);
		break;

	/* DAC2 Channel Sample Count Register, Address 28H
	   Addressable as word, longword */
	case 0x28:
		ret = dev->dac[1].samp_ct | (((uint32_t) dev->dac[1].curr_samp_ct) << 16);
		break;

	/* ADC Channel Sample Count Register, Address 2CH
	   Addressable as word, longword */
	case 0x2c:
		ret = dev->adc.samp_ct | (((uint32_t) dev->adc.curr_samp_ct) << 16);
		break;

	case 0x30: case 0x34: case 0x38: case 0x3c:
		ret = es1371_read_frame_reg(dev, port & 0x3c, dev->mem_page);
		break;
    }

    audiopci_log("es1371_inl: port=%04x ret=%08x\n", port, ret);
    return ret;
}


static void
es1371_outb(uint16_t port, uint8_t val, void *p)
{
    es1371_t *dev = (es1371_t *)p;
    uint32_t old_legacy_ctrl;

    audiopci_log("es1371_outb: port=%04x val=%02x\n", port, val);

    switch (port & 0x3f) {
	/* Interrupt/Chip Select Control Register, Address 00H
	   Addressable as byte, word, longword */
	case 0x00:
		if (!(dev->int_ctrl & INT_DAC1_EN) && (val & INT_DAC1_EN)) {
			dev->dac[0].addr = dev->dac[0].addr_latch;
			dev->dac[0].buffer_pos = 0;
			dev->dac[0].buffer_pos_end = 0;
			es1371_fetch(dev, 0);
		}
		if (!(dev->int_ctrl & INT_DAC2_EN) && (val & INT_DAC2_EN)) {
			dev->dac[1].addr = dev->dac[1].addr_latch;
			dev->dac[1].buffer_pos = 0;
			dev->dac[1].buffer_pos_end = 0;
			es1371_fetch(dev, 1);
		}
		dev->int_ctrl = (dev->int_ctrl & 0xffffff00) | val;
		break;
	case 0x01:
		dev->int_ctrl = (dev->int_ctrl & 0xffff00ff) | (val << 8);
		break;
	case 0x02:
		dev->int_ctrl = (dev->int_ctrl & 0xff00ffff) | (val << 16);
		break;
	case 0x03:
		dev->int_ctrl = (dev->int_ctrl & 0x00ffffff) | (val << 24);
		break;

	/* UART Data Register, Address 08H
	   Addressable as byte only */
	case 0x08:
		midi_raw_out_byte(val);
		break;

	/* UART Control Register, Address 09H
	   Addressable as byte only */
	case 0x09:
		dev->uart_ctrl = val & 0xe3;
		audiopci_log("ES1371 UART Cntrl = %02x\n", dev->uart_ctrl);
		break;

	/* UART Reserved Register, Address 0AH
	   Addressable as byte only */
	case 0x0a:
		dev->uart_res = val & 0x01;
		break;

	/* Memory Page Register, Address 0CH
	   Addressable as byte, word, longword */
	case 0x0c:
		dev->mem_page = val & 0xf;
		break;
	case 0x0d ... 0x0f:
		break;

	/* Legacy Control/Status Register, Address 18H
	   Addressable as byte, word, longword */
	case 0x18:
		dev->legacy_ctrl |= LEGACY_INT;
		break;
	case 0x1a:
		old_legacy_ctrl = dev->legacy_ctrl;
		dev->legacy_ctrl = (dev->legacy_ctrl & 0xff00ffff) | (val << 16);
		update_legacy(dev, old_legacy_ctrl);
		break;
	case 0x1b:
		old_legacy_ctrl = dev->legacy_ctrl;
		dev->legacy_ctrl = (dev->legacy_ctrl & 0x00ffffff) | (val << 24);
		es1371_update_irqs(dev);
		update_legacy(dev, old_legacy_ctrl);
		break;

	/* Serial Interface Control Register, Address 20H
	    Addressable as byte, word, longword */
	case 0x20:
		dev->si_cr = (dev->si_cr & 0xffffff00) | val;
		break;
	case 0x21:
		dev->si_cr = (dev->si_cr & 0xffff00ff) | (val << 8);
		if (!(dev->si_cr & SI_P1_INTR_EN))
			dev->int_status &= ~INT_STATUS_DAC1;
		if (!(dev->si_cr & SI_P2_INTR_EN))
			dev->int_status &= ~INT_STATUS_DAC2;
		es1371_update_irqs(dev);
		break;
	case 0x22:
		dev->si_cr = (dev->si_cr & 0xff80ffff) | ((val & 0x7f) << 16);
		break;

	default:
		audiopci_log("Bad es1371_outb: port=%04x val=%02x\n", port, val);
    }
}


static void
es1371_outw(uint16_t port, uint16_t val, void *p)
{
    es1371_t *dev = (es1371_t *)p;
    uint32_t old_legacy_ctrl;

    switch (port & 0x3f) {
	/* Interrupt/Chip Select Control Register, Address 00H
	   Addressable as byte, word, longword */
	case 0x00:
		if (!(dev->int_ctrl & INT_DAC1_EN) && (val & INT_DAC1_EN)) {
			dev->dac[0].addr = dev->dac[0].addr_latch;
			dev->dac[0].buffer_pos = 0;
			dev->dac[0].buffer_pos_end = 0;
			es1371_fetch(dev, 0);
		}
		if (!(dev->int_ctrl & INT_DAC2_EN) && (val & INT_DAC2_EN)) {
			dev->dac[1].addr = dev->dac[1].addr_latch;
			dev->dac[1].buffer_pos = 0;
			dev->dac[1].buffer_pos_end = 0;
			es1371_fetch(dev, 1);
		}
		dev->int_ctrl = (dev->int_ctrl & 0xffff0000) | val;
		break;
	case 0x02:
		dev->int_ctrl = (dev->int_ctrl & 0x0000ffff) | (val << 16);
		break;

	/* Memory Page Register, Address 0CH
	   Addressable as byte, word, longword */
	case 0x0c:
		dev->mem_page = val & 0xf;
		break;
	case 0x0e:
		break;

	/* Legacy Control/Status Register, Address 18H
	   Addressable as byte, word, longword */
	case 0x18:
		dev->legacy_ctrl |= LEGACY_INT;
		break;
	case 0x1a:
		old_legacy_ctrl = dev->legacy_ctrl;
		dev->legacy_ctrl = (dev->legacy_ctrl & 0x0000ffff) | (val << 16);
		es1371_update_irqs(dev);
		update_legacy(dev, old_legacy_ctrl);
		break;

	/* Serial Interface Control Register, Address 20H
	    Addressable as byte, word, longword */
	case 0x20:
		dev->si_cr = (dev->si_cr & 0xffff0000) | val;
		if (!(dev->si_cr & SI_P1_INTR_EN))
			dev->int_status &= ~INT_STATUS_DAC1;
		if (!(dev->si_cr & SI_P2_INTR_EN))
			dev->int_status &= ~INT_STATUS_DAC2;
		es1371_update_irqs(dev);
		break;
	case 0x22:
		dev->si_cr = (dev->si_cr & 0xff80ffff) | ((val & 0x007f) << 16);
		break;

	/* DAC1 Channel Sample Count Register, Address 24H
	   Addressable as word, longword */
	case 0x24:
		dev->dac[0].samp_ct = val;
		break;

	/* DAC2 Channel Sample Count Register, Address 28H
	   Addressable as word, longword */
	case 0x28:
		dev->dac[1].samp_ct = val;
		break;

	/* ADC Channel Sample Count Register, Address 2CH
	   Addressable as word, longword */
	case 0x2c:
		dev->adc.samp_ct = val;
		break;
    }
}


static void
es1371_outl(uint16_t port, uint32_t val, void *p)
{
    es1371_t *dev = (es1371_t *)p;
    uint32_t old_legacy_ctrl;

    audiopci_log("es1371_outl: port=%04x val=%08x\n", port, val);

    switch (port & 0x3f) {
	/* Interrupt/Chip Select Control Register, Address 00H
	   Addressable as byte, word, longword */
	case 0x00:
		if (!(dev->int_ctrl & INT_DAC1_EN) && (val & INT_DAC1_EN)) {
			dev->dac[0].addr = dev->dac[0].addr_latch;
			dev->dac[0].buffer_pos = 0;
			dev->dac[0].buffer_pos_end = 0;
			es1371_fetch(dev, 0);
		}
		if (!(dev->int_ctrl & INT_DAC2_EN) && (val & INT_DAC2_EN)) {
			dev->dac[1].addr = dev->dac[1].addr_latch;
			dev->dac[1].buffer_pos = 0;
			dev->dac[1].buffer_pos_end = 0;
			es1371_fetch(dev, 1);
		}
		dev->int_ctrl = val;
		break;

	/* Interrupt/Chip Select Status Register, Address 04H
	   Addressable as longword only */
	case 0x04:
		break;

	/* Memory Page Register, Address 0CH
	   Addressable as byte, word, longword */
	case 0x0c:
		dev->mem_page = val & 0xf;
		break;

	/* Sample Rate Converter Interface Register, Address 10H
	   Addressable as longword only */
	case 0x10:
		dev->sr_cir = val;
		if (dev->sr_cir & SRC_RAM_WE) {
			dev->sr_ram[dev->sr_cir >> 25] = val & 0xffff;
			switch (dev->sr_cir >> 25) {
				case 0x71:
					dev->dac[0].vf = (dev->dac[0].vf & ~0x1f8000) | ((val & 0xfc00) << 5);
					dev->dac[0].ac = (dev->dac[0].ac & ~0x7f8000) | ((val & 0x00ff) << 15);
					dev->dac[0].f_pos = 0;
					break;
				case 0x72:
					dev->dac[0].ac = (dev->dac[0].ac & ~0x7fff) | (val & 0x7fff);
					break;				
				case 0x73:
					dev->dac[0].vf = (dev->dac[0].vf & ~0x7fff) | (val & 0x7fff);
					break;

				case 0x75:
					dev->dac[1].vf = (dev->dac[1].vf & ~0x1f8000) | ((val & 0xfc00) << 5);
					dev->dac[1].ac = (dev->dac[1].ac & ~0x7f8000) | ((val & 0x00ff) << 15);
					dev->dac[1].f_pos = 0;
					break;
				case 0x76:
					dev->dac[1].ac = (dev->dac[1].ac & ~0x7fff) | (val & 0x7fff);
					break;
				case 0x77:
					dev->dac[1].vf = (dev->dac[1].vf & ~0x7fff) | (val & 0x7fff);
					break;

				case 0x7c:
					dev->dac[0].vol_l = (int32_t)(int16_t)(val & 0xffff);
					break;
				case 0x7d:
					dev->dac[0].vol_r = (int32_t)(int16_t)(val & 0xffff);
					break;
				case 0x7e:
					dev->dac[1].vol_l = (int32_t)(int16_t)(val & 0xffff);
					break;
				case 0x7f:
					dev->dac[1].vol_r = (int32_t)(int16_t)(val & 0xffff);
					break;
			}
		}
		break;

	/* CODEC Write Register, Address 14H
	   Addressable as longword only */
	case 0x14:
		if (val & CODEC_READ) {
			dev->codec_ctrl &= 0x00ff0000;
			val = (val >> 16) & 0x7e;
			dev->codec_ctrl |= ac97_codec_read(dev->codec, val);
			dev->codec_ctrl |= ac97_codec_read(dev->codec, val | 1) << 8;
		} else {
			dev->codec_ctrl = val & 0x00ffffff;
			ac97_codec_write(dev->codec,  (val >> 16) & 0x7e,      val & 0xff);
			ac97_codec_write(dev->codec, ((val >> 16) & 0x7e) | 1, val >> 8);

			ac97_codec_getattn(dev->codec, 0x02, &dev->master_vol_l, &dev->master_vol_r);
			ac97_codec_getattn(dev->codec, 0x12, &dev->cd_vol_l, &dev->cd_vol_r);
		}
		break;

	/* Legacy Control/Status Register, Address 18H
	   Addressable as byte, word, longword */
	case 0x18:
		old_legacy_ctrl = dev->legacy_ctrl;
		dev->legacy_ctrl = (dev->legacy_ctrl & 0x0000ffff) | (val & 0xffff0000);
		dev->legacy_ctrl |= LEGACY_INT;
		es1371_update_irqs(dev);
		update_legacy(dev, old_legacy_ctrl);
		break;

	/* Serial Interface Control Register, Address 20H
	    Addressable as byte, word, longword */
	case 0x20:
		dev->si_cr = (val & 0x007fffff) | 0xff800000;
		if (!(dev->si_cr & SI_P1_INTR_EN))
			dev->int_status &= ~INT_STATUS_DAC1;
		if (!(dev->si_cr & SI_P2_INTR_EN))
			dev->int_status &= ~INT_STATUS_DAC2;
		es1371_update_irqs(dev);
		break;

	/* DAC1 Channel Sample Count Register, Address 24H
	   Addressable as word, longword */
	case 0x24:
		dev->dac[0].samp_ct = val & 0xffff;
		break;

	/* DAC2 Channel Sample Count Register, Address 28H
	   Addressable as word, longword */
	case 0x28:
		dev->dac[1].samp_ct = val & 0xffff;
		break;

	/* ADC Channel Sample Count Register, Address 2CH
	   Addressable as word, longword */
	case 0x2c:
		dev->adc.samp_ct = val & 0xffff;
		break;

	case 0x30: case 0x34: case 0x38: case 0x3c:
		es1371_write_frame_reg(dev, port & 0x3c, dev->mem_page, val);
		break;
    }
}


static void
capture_event(es1371_t *dev, int type, int rw, uint16_t port)
{
    dev->legacy_ctrl &= ~(LEGACY_EVENT_MASK | LEGACY_EVENT_ADDR_MASK);
    dev->legacy_ctrl |= type;
    if (rw)	
	dev->legacy_ctrl |= LEGACY_EVENT_TYPE_RW;
    else
	dev->legacy_ctrl &= ~LEGACY_EVENT_TYPE_RW;
    dev->legacy_ctrl |= ((port << LEGACY_EVENT_ADDR_SHIFT) & LEGACY_EVENT_ADDR_MASK);
    dev->legacy_ctrl &= ~LEGACY_INT;
    nmi = 1;
}


static void
capture_write_sscape(uint16_t port, uint8_t val, void *p)
{
    capture_event(p, LEGACY_EVENT_SSCAPE, 1, port);
}


static void
capture_write_codec(uint16_t port, uint8_t val, void *p)
{
    capture_event(p, LEGACY_EVENT_CODEC, 1, port);
}


static void
capture_write_sb(uint16_t port, uint8_t val, void *p)
{
    capture_event(p, LEGACY_EVENT_SB, 1, port);
}


static void
capture_write_adlib(uint16_t port, uint8_t val, void *p)
{
    capture_event(p, LEGACY_EVENT_ADLIB, 1, port);
}


static void
capture_write_master_pic(uint16_t port, uint8_t val, void *p)
{
    capture_event(p, LEGACY_EVENT_MASTER_PIC, 1, port);
}


static void
capture_write_master_dma(uint16_t port, uint8_t val, void *p)
{
    capture_event(p, LEGACY_EVENT_MASTER_DMA, 1, port);
}


static void
capture_write_slave_pic(uint16_t port, uint8_t val, void *p)
{
    capture_event(p, LEGACY_EVENT_SLAVE_PIC, 1, port);
}


static void
capture_write_slave_dma(uint16_t port, uint8_t val, void *p)
{
    capture_event(p, LEGACY_EVENT_SLAVE_DMA, 1, port);
}


static uint8_t
capture_read_sscape(uint16_t port, void *p)
{
    capture_event(p, LEGACY_EVENT_SSCAPE, 0, port);
    return 0xff;
}


static uint8_t
capture_read_codec(uint16_t port, void *p)
{
    capture_event(p, LEGACY_EVENT_CODEC, 0, port);
    return 0xff;
}


static uint8_t
capture_read_sb(uint16_t port, void *p)
{
    capture_event(p, LEGACY_EVENT_SB, 0, port);
    return 0xff;
}


static uint8_t
capture_read_adlib(uint16_t port, void *p)
{
    capture_event(p, LEGACY_EVENT_ADLIB, 0, port);
    return 0xff;
}


static uint8_t
capture_read_master_pic(uint16_t port, void *p)
{
    capture_event(p, LEGACY_EVENT_MASTER_PIC, 0, port);
    return 0xff;
}


static uint8_t
capture_read_master_dma(uint16_t port, void *p)
{
    capture_event(p, LEGACY_EVENT_MASTER_DMA, 0, port);
    return 0xff;
}


static uint8_t
capture_read_slave_pic(uint16_t port, void *p)
{
    capture_event(p, LEGACY_EVENT_SLAVE_PIC, 0, port);
    return 0xff;
}


static uint8_t
capture_read_slave_dma(uint16_t port, void *p)
{
    capture_event(p, LEGACY_EVENT_SLAVE_DMA, 0, port);
    return 0xff;
}


static void
update_legacy(es1371_t *dev, uint32_t old_legacy_ctrl)
{
    if (old_legacy_ctrl & LEGACY_CAPTURE_SSCAPE) {
	switch ((old_legacy_ctrl >> LEGACY_SSCAPE_ADDR_SHIFT) & 3) {
		case 0:
			io_removehandler(0x0320, 0x0008,
					 capture_read_sscape, NULL, NULL,
					 capture_write_sscape, NULL, NULL, dev);
			break;
		case 1:
			io_removehandler(0x0330, 0x0008,
					 capture_read_sscape, NULL, NULL,
					 capture_write_sscape, NULL, NULL, dev);
			break;
		case 2:
			io_removehandler(0x0340, 0x0008,
					 capture_read_sscape, NULL ,NULL,
					 capture_write_sscape, NULL, NULL, dev);
			break;
		case 3:
			io_removehandler(0x0350, 0x0008,
					 capture_read_sscape, NULL, NULL,
					 capture_write_sscape, NULL, NULL, dev);
			break;
	}
    }

    if (old_legacy_ctrl & LEGACY_CAPTURE_CODEC) {
	switch ((old_legacy_ctrl >> LEGACY_CODEC_ADDR_SHIFT) & 3) {
		case 0:
			io_removehandler(0x0530, 0x0008,
					 capture_read_codec, NULL, NULL,
					 capture_write_codec, NULL, NULL, dev);
			break;
		case 2:
			io_removehandler(0x0e80, 0x0008,
					 capture_read_codec, NULL, NULL,
					 capture_write_codec,NULL,NULL, dev);
			break;
		case 3:
			io_removehandler(0x0f40, 0x0008,
					 capture_read_codec, NULL, NULL,
					 capture_write_codec, NULL, NULL, dev);
			break;
	}
    }

    if (old_legacy_ctrl & LEGACY_CAPTURE_SB) {
	if (!(old_legacy_ctrl & LEGACY_SB_ADDR)) {
		io_removehandler(0x0220, 0x0010,
				 capture_read_sb, NULL, NULL,
				 capture_write_sb, NULL, NULL, dev);
	} else {
		io_removehandler(0x0240, 0x0010,
				 capture_read_sb, NULL, NULL,
				 capture_write_sb, NULL, NULL, dev);
	}
    }

    if (old_legacy_ctrl & LEGACY_CAPTURE_ADLIB) {
	io_removehandler(0x0388, 0x0004,
			 capture_read_adlib, NULL, NULL,
			 capture_write_adlib, NULL, NULL, dev);
    }

    if (old_legacy_ctrl & LEGACY_CAPTURE_MASTER_PIC) {
	io_removehandler(0x0020, 0x0002,
			 capture_read_master_pic, NULL, NULL,
			 capture_write_master_pic,NULL,NULL, dev);
    }

    if (old_legacy_ctrl & LEGACY_CAPTURE_MASTER_DMA) {
	io_removehandler(0x0000, 0x0010,
			 capture_read_master_dma, NULL, NULL,
			 capture_write_master_dma, NULL, NULL, dev);
    }

    if (old_legacy_ctrl & LEGACY_CAPTURE_SLAVE_PIC) {
	io_removehandler(0x00a0, 0x0002,
			 capture_read_slave_pic, NULL, NULL,
			 capture_write_slave_pic, NULL, NULL, dev);
    }

    if (old_legacy_ctrl & LEGACY_CAPTURE_SLAVE_DMA) {
	io_removehandler(0x00c0, 0x0020,
			 capture_read_slave_dma, NULL, NULL,
			 capture_write_slave_dma, NULL, NULL, dev);
    }

    if (dev->legacy_ctrl & LEGACY_CAPTURE_SSCAPE) {
	switch ((dev->legacy_ctrl >> LEGACY_SSCAPE_ADDR_SHIFT) & 3) {
		case 0:
			io_sethandler(0x0320, 0x0008,
				      capture_read_sscape, NULL, NULL,
				      capture_write_sscape, NULL, NULL, dev);
			break;
		case 1:
			io_sethandler(0x0330, 0x0008,
				      capture_read_sscape, NULL, NULL,
				      capture_write_sscape, NULL, NULL, dev);
			break;
		case 2:
			io_sethandler(0x0340, 0x0008,
				      capture_read_sscape, NULL, NULL,
				      capture_write_sscape, NULL, NULL, dev);
			break;
		case 3:
			io_sethandler(0x0350, 0x0008,
				      capture_read_sscape, NULL, NULL,
				      capture_write_sscape, NULL, NULL, dev);
			break;
	}
    }

    if (dev->legacy_ctrl & LEGACY_CAPTURE_CODEC) {
	switch ((dev->legacy_ctrl >> LEGACY_CODEC_ADDR_SHIFT) & 3) {
		case 0:
			io_sethandler(0x0530, 0x0008,
				      capture_read_codec, NULL, NULL,
				      capture_write_codec, NULL, NULL, dev);
			break;
		case 2:
			io_sethandler(0x0e80, 0x0008,
				      capture_read_codec, NULL, NULL,
				      capture_write_codec, NULL, NULL, dev);
			break;
		case 3:
			io_sethandler(0x0f40, 0x0008,
				      capture_read_codec, NULL, NULL,
				      capture_write_codec, NULL, NULL, dev);
			break;
	}
    }

    if (dev->legacy_ctrl & LEGACY_CAPTURE_SB) {
	if (!(dev->legacy_ctrl & LEGACY_SB_ADDR)) {
		io_sethandler(0x0220, 0x0010,
			      capture_read_sb, NULL, NULL,
			      capture_write_sb, NULL, NULL, dev);
	} else {
		io_sethandler(0x0240, 0x0010,
			      capture_read_sb, NULL, NULL,
			      capture_write_sb, NULL, NULL, dev);
	}
    }

    if (dev->legacy_ctrl & LEGACY_CAPTURE_ADLIB) {
	io_sethandler(0x0388, 0x0004,
		      capture_read_adlib, NULL, NULL,
		      capture_write_adlib, NULL, NULL, dev);
    }

    if (dev->legacy_ctrl & LEGACY_CAPTURE_MASTER_PIC) {
	io_sethandler(0x0020, 0x0002,
		      capture_read_master_pic, NULL, NULL,
		      capture_write_master_pic, NULL, NULL, dev);
    }

    if (dev->legacy_ctrl & LEGACY_CAPTURE_MASTER_DMA) {
	io_sethandler(0x0000, 0x0010,
		      capture_read_master_dma, NULL, NULL,
		      capture_write_master_dma, NULL, NULL, dev);
    }

    if (dev->legacy_ctrl & LEGACY_CAPTURE_SLAVE_PIC) {
	io_sethandler(0x00a0, 0x0002,
		      capture_read_slave_pic, NULL, NULL,
		      capture_write_slave_pic, NULL, NULL, dev);
    }

    if (dev->legacy_ctrl & LEGACY_CAPTURE_SLAVE_DMA) {
	io_sethandler(0x00c0, 0x0020,
		      capture_read_slave_dma, NULL, NULL,
		      capture_write_slave_dma, NULL, NULL, dev);
    }
}


static uint8_t
es1371_pci_read(int func, int addr, void *p)
{
    es1371_t *dev = (es1371_t *)p;

    if (func > 0)
	return 0xff;

    if (addr > 0x3f)
	return 0x00;

    switch (addr) {
	case 0x00: return 0x74;		/* Ensoniq */
	case 0x01: return 0x12;

	case 0x02: return 0x71;		/* ES1371 */
	case 0x03: return 0x13;

	case 0x04: return dev->pci_command;
	case 0x05: return dev->pci_serr;

	case 0x06: return 0x10;		/* Supports ACPI */
	case 0x07: return 0x00;

	case 0x08: return 0x02;		/* Revision ID */
	case 0x09: return 0x00;		/* Multimedia audio device */
	case 0x0a: return 0x01;
	case 0x0b: return 0x04;

	case 0x10: return 0x01 | (dev->base_addr & 0xc0);	/* memBaseAddr */
	case 0x11: return dev->base_addr >> 8;
	case 0x12: return dev->base_addr >> 16;
	case 0x13: return dev->base_addr >> 24;

	case 0x2c: return 0x74;		/* Subsystem vendor ID */
	case 0x2d: return 0x12;
	case 0x2e: return 0x71;
	case 0x2f: return 0x13;

	case 0x34: return 0xdc;		/* Capabilites pointer */

	case 0x3c: return dev->int_line;
	case 0x3d: return 0x01;		/* INTA */

	case 0x3e: return 0xc;		/* Minimum grant */
	case 0x3f: return 0x80;		/* Maximum latency */

	case 0xdc: return 0x01;		/* Capabilities identifier */
	case 0xdd: return 0x00;		/* Next item pointer */
	case 0xde: return 0x31;		/* Power management capabilities */
	case 0xdf: return 0x6c;

	case 0xe0: return dev->pmcsr & 0xff;
	case 0xe1: return dev->pmcsr >> 8;
    }

    return 0x00;
}


static void
es1371_io_set(es1371_t *dev, int set)
{
    if (dev->pci_command & PCI_COMMAND_IO) {
	io_handler(set, dev->base_addr, 0x0040,
		   es1371_inb, es1371_inw, es1371_inl,
		   es1371_outb, es1371_outw, es1371_outl, dev);
    }
}


static void
es1371_pci_write(int func, int addr, uint8_t val, void *p)
{
    es1371_t *dev = (es1371_t *)p;

    if (func)
	return;

    switch (addr) {
	case 0x04:
		es1371_io_set(dev, 0);
		dev->pci_command = val & 0x05;
		es1371_io_set(dev, 1);
		break;
	case 0x05:
		dev->pci_serr = val & 1;
		break;

	case 0x10:
		es1371_io_set(dev, 0);
		dev->base_addr = (dev->base_addr & 0xffffff00) | (val & 0xc0);
		es1371_io_set(dev, 1);
		break;
	case 0x11:
		es1371_io_set(dev, 0);
		dev->base_addr = (dev->base_addr & 0xffff00c0) | (val << 8);
		es1371_io_set(dev, 1);
		break;
	case 0x12:
		dev->base_addr = (dev->base_addr & 0xff00ffc0) | (val << 16);
		break;
	case 0x13:
		dev->base_addr = (dev->base_addr & 0x00ffffc0) | (val << 24);
		break;

	case 0x3c:
		dev->int_line = val;
		break;

	case 0xe0:
		dev->pmcsr = (dev->pmcsr & 0xff00) | (val & 0x03);
		break;
	case 0xe1:
		dev->pmcsr = (dev->pmcsr & 0x00ff) | ((val & 0x01) << 8);
		break;
    }
}


static void
es1371_fetch(es1371_t *dev, int dac_nr)
{
    if (dev->si_cr & (dac_nr ? SI_P2_PAUSE : SI_P1_PAUSE))
	return;

    int format = dac_nr ? ((dev->si_cr >> 2) & 3) : (dev->si_cr & 3);
    int pos = dev->dac[dac_nr].buffer_pos & 63;
    int c;

    switch (format) {
	case FORMAT_MONO_8:
		for (c = 0; c < 32; c += 4) {
			dev->dac[dac_nr].buffer_l[(pos+c)   & 63] = dev->dac[dac_nr].buffer_r[(pos+c)   & 63] =
				(mem_readb_phys(dev->dac[dac_nr].addr) ^ 0x80) << 8;
			dev->dac[dac_nr].buffer_l[(pos+c+1) & 63] = dev->dac[dac_nr].buffer_r[(pos+c+1) & 63] =
				(mem_readb_phys(dev->dac[dac_nr].addr+1) ^ 0x80) << 8;
			dev->dac[dac_nr].buffer_l[(pos+c+2) & 63] = dev->dac[dac_nr].buffer_r[(pos+c+2) & 63] =
				(mem_readb_phys(dev->dac[dac_nr].addr+2) ^ 0x80) << 8;
			dev->dac[dac_nr].buffer_l[(pos+c+3) & 63] = dev->dac[dac_nr].buffer_r[(pos+c+3) & 63] =
				(mem_readb_phys(dev->dac[dac_nr].addr+3) ^ 0x80) << 8;
			dev->dac[dac_nr].addr += 4;

			dev->dac[dac_nr].buffer_pos_end += 4;
			dev->dac[dac_nr].count++;

			if (dev->dac[dac_nr].count > dev->dac[dac_nr].size) {
				dev->dac[dac_nr].count = 0;
				dev->dac[dac_nr].addr = dev->dac[dac_nr].addr_latch;
				break;
			}
		}
		break;

	case FORMAT_STEREO_8:
		for (c = 0; c < 16; c += 2) {
			dev->dac[dac_nr].buffer_l[(pos+c)   & 63] = (mem_readb_phys(dev->dac[dac_nr].addr) ^ 0x80) << 8;
			dev->dac[dac_nr].buffer_r[(pos+c)   & 63] = (mem_readb_phys(dev->dac[dac_nr].addr + 1) ^ 0x80) << 8;
			dev->dac[dac_nr].buffer_l[(pos+c+1) & 63] = (mem_readb_phys(dev->dac[dac_nr].addr + 2) ^ 0x80) << 8;
			dev->dac[dac_nr].buffer_r[(pos+c+1) & 63] = (mem_readb_phys(dev->dac[dac_nr].addr + 3) ^ 0x80) << 8;
			dev->dac[dac_nr].addr += 4;

			dev->dac[dac_nr].buffer_pos_end += 2;
			dev->dac[dac_nr].count++;

			if (dev->dac[dac_nr].count > dev->dac[dac_nr].size) {
				dev->dac[dac_nr].count = 0;
				dev->dac[dac_nr].addr = dev->dac[dac_nr].addr_latch;
				break;
			}
		}
		break;

	case FORMAT_MONO_16:
		for (c = 0; c < 16; c += 2) {
			dev->dac[dac_nr].buffer_l[(pos+c)   & 63] = dev->dac[dac_nr].buffer_r[(pos+c)   & 63] =
				mem_readw_phys(dev->dac[dac_nr].addr);
			dev->dac[dac_nr].buffer_l[(pos+c+1) & 63] = dev->dac[dac_nr].buffer_r[(pos+c+1) & 63] =
				mem_readw_phys(dev->dac[dac_nr].addr + 2);
			dev->dac[dac_nr].addr += 4;

			dev->dac[dac_nr].buffer_pos_end += 2;
			dev->dac[dac_nr].count++;

			if (dev->dac[dac_nr].count > dev->dac[dac_nr].size) {
				dev->dac[dac_nr].count = 0;
				dev->dac[dac_nr].addr = dev->dac[dac_nr].addr_latch;
				break;
			}
		}
		break;

	case FORMAT_STEREO_16:
		for (c = 0; c < 4; c++) {
			dev->dac[dac_nr].buffer_l[(pos+c) & 63] = mem_readw_phys(dev->dac[dac_nr].addr);
			dev->dac[dac_nr].buffer_r[(pos+c) & 63] = mem_readw_phys(dev->dac[dac_nr].addr + 2);
			dev->dac[dac_nr].addr += 4;

			dev->dac[dac_nr].buffer_pos_end++;
			dev->dac[dac_nr].count++;

			if (dev->dac[dac_nr].count > dev->dac[dac_nr].size) {
				dev->dac[dac_nr].count = 0;
				dev->dac[dac_nr].addr = dev->dac[dac_nr].addr_latch;
				break;
			}
		}
		break;
    }
}


static inline float
low_fir_es1371(int dac_nr, int i, float NewSample)
{
    static float x[2][2][128]; //input samples
    static int x_pos[2] = {0, 0};
    float out = 0.0;
    int read_pos, n_coef;
    int pos = x_pos[dac_nr];

    x[dac_nr][i][pos] = NewSample;

    /* Since only 1/16th of input samples are non-zero, only filter those that
       are valid.*/
    read_pos = (pos + 15) & (127 & ~15);
    n_coef = (16 - pos) & 15;

    while (n_coef < ES1371_NCoef) {
	out += low_fir_es1371_coef[n_coef] * x[dac_nr][i][read_pos];
	read_pos = (read_pos + 16) & (127 & ~15);
	n_coef += 16;
    }

    if (i == 1) {
	x_pos[dac_nr] = (x_pos[dac_nr] + 1) & 127;
	if (x_pos[dac_nr] > 127)
		x_pos[dac_nr] = 0;
    }

    return out;
}


static void
es1371_next_sample_filtered(es1371_t *dev, int dac_nr, int out_idx)
{
    int out_l, out_r;
    int c;

    if ((dev->dac[dac_nr].buffer_pos - dev->dac[dac_nr].buffer_pos_end) >= 0)
	es1371_fetch(dev, dac_nr);

    out_l = dev->dac[dac_nr].buffer_l[dev->dac[dac_nr].buffer_pos & 63];
    out_r = dev->dac[dac_nr].buffer_r[dev->dac[dac_nr].buffer_pos & 63];

    dev->dac[dac_nr].filtered_l[out_idx] = (int)low_fir_es1371(dac_nr, 0, (float)out_l);
    dev->dac[dac_nr].filtered_r[out_idx] = (int)low_fir_es1371(dac_nr, 1, (float)out_r);

    for (c = 1; c < 16; c++) {
	dev->dac[dac_nr].filtered_l[out_idx+c] = (int)low_fir_es1371(dac_nr, 0, 0);
	dev->dac[dac_nr].filtered_r[out_idx+c] = (int)low_fir_es1371(dac_nr, 1, 0);
    }

    dev->dac[dac_nr].buffer_pos++;
}


static void
es1371_update(es1371_t *dev)
{
    int32_t l, r;

    l = (dev->dac[0].out_l * dev->dac[0].vol_l) >> 12;
    l += ((dev->dac[1].out_l * dev->dac[1].vol_l) >> 12);
    r = (dev->dac[0].out_r * dev->dac[0].vol_r) >> 12;
    r += ((dev->dac[1].out_r * dev->dac[1].vol_r) >> 12);

    l >>= 1;
    r >>= 1;

    l = (l * dev->master_vol_l) >> 15;
    r = (r * dev->master_vol_r) >> 15;

    if (l < -32768)
	l = -32768;
    else if (l > 32767)
	l = 32767;
    if (r < -32768)
	r = -32768;
    else if (r > 32767)
	r = 32767;

   for (; dev->pos < sound_pos_global; dev->pos++) {                                        
	dev->buffer[dev->pos*2]     = l;
	dev->buffer[dev->pos*2 + 1] = r;
    }
}


static void
es1371_poll(void *p)
{
    es1371_t *dev = (es1371_t *)p;
    int frac, idx, samp1_l, samp1_r, samp2_l, samp2_r;

    timer_advance_u64(&dev->dac[1].timer, dev->dac[1].latch);

    es1371_update(dev);		

    if (dev->int_ctrl & INT_UART_EN) {
	audiopci_log("UART INT Enabled\n");
	if (dev->uart_ctrl & UART_CTRL_RXINTEN) {
		/* We currently don't implement MIDI Input.
		   But if anything sets MIDI Input and Output together we'd have to take account
		   of the MIDI Output case, and disable IRQ's and RX bits when MIDI Input is
		   enabled as well but not in the MIDI Output portion */
		if (dev->uart_ctrl & UART_CTRL_TXINTEN) 
			dev->int_status |= INT_STATUS_UART;
		else
			dev->int_status &= ~INT_STATUS_UART;
	} else if (!(dev->uart_ctrl & UART_CTRL_RXINTEN) && ((dev->uart_ctrl & UART_CTRL_TXINTEN))) {
		/* Or enable the UART IRQ and the respective TX bits only when the MIDI Output is
		   enabled */
		dev->int_status |= INT_STATUS_UART;
	}

	if (dev->uart_ctrl & UART_CTRL_RXINTEN) {
		if (dev->uart_ctrl & UART_CTRL_TXINTEN) 
			dev->uart_status |= (UART_STATUS_TXINT | UART_STATUS_TXRDY);
		else
			dev->uart_status &= ~(UART_STATUS_TXINT | UART_STATUS_TXRDY);
	} else
		dev->uart_status |= (UART_STATUS_TXINT | UART_STATUS_TXRDY);

	es1371_update_irqs(dev);
    }

    if (dev->int_ctrl & INT_DAC1_EN) {
	frac = dev->dac[0].ac & 0x7fff;
	idx = dev->dac[0].ac >> 15;
	samp1_l = dev->dac[0].filtered_l[idx];
	samp1_r = dev->dac[0].filtered_r[idx];
	samp2_l = dev->dac[0].filtered_l[(idx + 1) & 31];
	samp2_r = dev->dac[0].filtered_r[(idx + 1) & 31];

	dev->dac[0].out_l = ((samp1_l * (0x8000 - frac)) + (samp2_l * frac)) >> 15;
	dev->dac[0].out_r = ((samp1_r * (0x8000 - frac)) + (samp2_r * frac)) >> 15;
	dev->dac[0].ac += dev->dac[0].vf;
	dev->dac[0].ac &= ((32 << 15) - 1);
	if ((dev->dac[0].ac >> (15+4)) != dev->dac[0].f_pos) {
		es1371_next_sample_filtered(dev, 0, dev->dac[0].f_pos ? 16 : 0);
		dev->dac[0].f_pos = (dev->dac[0].f_pos + 1) & 1;

		dev->dac[0].curr_samp_ct--;
		if (dev->dac[0].curr_samp_ct < 0) {
			dev->int_status |= INT_STATUS_DAC1;
			es1371_update_irqs(dev);
			dev->dac[0].curr_samp_ct = dev->dac[0].samp_ct;
		}
	}
    }

    if (dev->int_ctrl & INT_DAC2_EN) {
	frac = dev->dac[1].ac & 0x7fff;
	idx = dev->dac[1].ac >> 15;
	samp1_l = dev->dac[1].filtered_l[idx];
	samp1_r = dev->dac[1].filtered_r[idx];
	samp2_l = dev->dac[1].filtered_l[(idx + 1) & 31];
	samp2_r = dev->dac[1].filtered_r[(idx + 1) & 31];

	dev->dac[1].out_l = ((samp1_l * (0x8000 - frac)) + (samp2_l * frac)) >> 15;
	dev->dac[1].out_r = ((samp1_r * (0x8000 - frac)) + (samp2_r * frac)) >> 15;
	dev->dac[1].ac += dev->dac[1].vf;
	dev->dac[1].ac &= ((32 << 15) - 1);
	if ((dev->dac[1].ac >> (15+4)) != dev->dac[1].f_pos) {
		es1371_next_sample_filtered(dev, 1, dev->dac[1].f_pos ? 16 : 0);
		dev->dac[1].f_pos = (dev->dac[1].f_pos + 1) & 1;

		dev->dac[1].curr_samp_ct--;
		if (dev->dac[1].curr_samp_ct < 0) {
			dev->int_status |= INT_STATUS_DAC2;
			es1371_update_irqs(dev);
			dev->dac[1].curr_samp_ct = dev->dac[1].samp_ct;
		}
	}
    }
}


static void
es1371_get_buffer(int32_t *buffer, int len, void *p)
{
    es1371_t *dev = (es1371_t *)p;
    int c;

    es1371_update(dev);

    for (c = 0; c < len * 2; c++)
	buffer[c] += (dev->buffer[c] / 2);

    dev->pos = 0;
}


static void
es1371_filter_cd_audio(int channel, double *buffer, void *p)
{
    es1371_t *dev = (es1371_t *)p;
    int32_t c;
    int cd = channel ? dev->cd_vol_r : dev->cd_vol_l;
    int master = channel ? dev->master_vol_r : dev->master_vol_l;

    c = (((int32_t) *buffer) * cd) >> 15;
    c = (c * master) >> 15;

    *buffer     = (double) c;
}


static inline
double sinc(double x)
{
    return sin(M_PI * x) / (M_PI * x);
}


static void
generate_es1371_filter(void)
{
    /* Cutoff frequency = 1 / 32 */
    float fC = 1.0 / 32.0;
    float gain;
    int n;

    for (n = 0; n < ES1371_NCoef; n++) {
	/* Blackman window */
	double w = 0.42 - (0.5 * cos((2.0*n*M_PI)/(double)(ES1371_NCoef-1))) + (0.08 * cos((4.0*n*M_PI)/(double)(ES1371_NCoef-1)));
	/* Sinc filter */
	double h = sinc(2.0 * fC * ((double)n - ((double)(ES1371_NCoef-1) / 2.0)));

	/* Create windowed-sinc filter */
	low_fir_es1371_coef[n] = w * h;
    }

    low_fir_es1371_coef[(ES1371_NCoef - 1) / 2] = 1.0;

    gain = 0.0;
    for (n = 0; n < ES1371_NCoef; n++)
	gain += low_fir_es1371_coef[n] / (float)N;

    gain /= 0.95;

    /* Normalise filter, to produce unity gain */
    for (n = 0; n < ES1371_NCoef; n++)
	low_fir_es1371_coef[n] /= gain;
}


static void *
es1371_init(const device_t *info)
{
    es1371_t *dev = malloc(sizeof(es1371_t));
    memset(dev, 0x00, sizeof(es1371_t));

    sound_add_handler(es1371_get_buffer, dev);
    sound_set_cd_audio_filter(es1371_filter_cd_audio, dev);

    /* Add our own always-present game port to override the standalone ISAPnP one. */
    gameport_remap(gameport_add(&gameport_pnp_device), 0x200);

    dev->card = pci_add_card(info->local ? PCI_ADD_SOUND : PCI_ADD_NORMAL, es1371_pci_read, es1371_pci_write, dev);

    timer_add(&dev->dac[1].timer, es1371_poll, dev, 1); 

    generate_es1371_filter();

    ac97_codec = &dev->codec;
    ac97_codec_count = 1;
    ac97_codec_id = 0;
    /* Let the machine decide the codec on onboard implementations. */
    if (!info->local)
	device_add(&cs4297a_device);

    es1371_reset(dev);

    return dev;
}


static void
es1371_close(void *p)
{
    es1371_t *dev = (es1371_t *) p;

    free(dev);
}


static void
es1371_speed_changed(void *p)
{
    es1371_t *dev = (es1371_t *)p;

    dev->dac[1].latch = (uint64_t)((double)TIMER_USEC * (1000000.0 / 48000.0));
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
    es1371_reset,
    { NULL },
    es1371_speed_changed,
    NULL,
    NULL
};
