/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Intel PIC chip emulation.
 *
 * Version:	@(#)pic.c	1.0.0	2018/04/29
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016-2018 Miran Grca.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "86box.h"
#include "cpu/cpu.h"
#include "machine/machine.h"
#include "io.h"
#include "pci.h"
#include "pic.h"
#include "pit.h"


int output;
int intclear;
int keywaiting=0;
int pic_intpending;
PIC pic, pic2;
uint16_t pic_current;


#ifdef ENABLE_PIC_LOG
int pic_do_log = ENABLE_PIC_LOG;
#endif


static void
pic_log(const char *format, ...)
{
#ifdef ENABLE_PIC_LOG
    va_list ap;

    if (pic_do_log) {
	va_start(ap, format);
	pclog_ex(format, ap);
	va_end(ap);
    }
#endif
}


void
pic_updatepending()
{
    uint16_t temp_pending = 0;
    if (AT) {
	if ((pic2.pend&~pic2.mask)&~pic2.mask2)
		pic.pend |= pic.icw3;
	else
		pic.pend &= ~pic.icw3;
    }
    pic_intpending = (pic.pend & ~pic.mask) & ~pic.mask2;
    if (AT) {
	if (!((pic.mask | pic.mask2) & pic.icw3)) {
		temp_pending = ((pic2.pend&~pic2.mask)&~pic2.mask2);
		temp_pending <<= 8;
		pic_intpending |= temp_pending;
	}
    }
    pic_log("pic_intpending = %i  %02X %02X %02X %02X\n", pic_intpending, pic.ins, pic.pend, pic.mask, pic.mask2);
    pic_log("                    %02X %02X %02X %02X %i %i\n", pic2.ins, pic2.pend, pic2.mask, pic2.mask2, ((pic.mask | pic.mask2) & (1 << 2)), ((pic2.pend&~pic2.mask)&~pic2.mask2));
}


void
pic_reset()
{
    pic.icw=0;
    pic.mask=0xFF;
    pic.mask2=0;
    pic.pend=pic.ins=0;
    pic.vector=8;
    pic.read=1;
    pic2.icw=0;
    pic2.mask=0xFF;
    pic.mask2=0;
    pic2.pend=pic2.ins=0;
    pic_intpending = 0;
}


void
pic_update_mask(uint8_t *mask, uint8_t ins)
{
    int c;
    *mask = 0;
    for (c = 0; c < 8; c++) {
	if (ins & (1 << c)) {
		*mask = 0xff << c;
		return;
	}
    }
}


static int
picint_is_level(uint16_t irq)
{
    if (PCI)
	return pci_irq_is_level(irq);
    else {
	if (irq < 8)
		return (pic.icw1 & 8) ? 1 : 0;
	else
		return (pic2.icw1 & 8) ? 1 : 0;
    }
}


static void
pic_autoeoi()
{
    int c;

    for (c = 0; c < 8; c++) {
	if (pic.ins & ( 1 << c)) {
		pic.ins &= ~(1 << c);
		pic_update_mask(&pic.mask2, pic.ins);

		if (AT) {
			if (((1 << c) == pic.icw3) && (pic2.pend&~pic2.mask)&~pic2.mask2)
				pic.pend |= pic.icw3;
		}

		if ((pic_current & (1 << c)) && picint_is_level(c)) {
			if (((1 << c) != pic.icw3) || !AT)
				pic.pend |= 1 << c;
		}

		pic_updatepending();
		return;
	}
    }
}


void
pic_write(uint16_t addr, uint8_t val, void *priv)
{
    int c;
    if (addr&1) {
	switch (pic.icw) {
		case 0: /*OCW1*/
			pic.mask=val;
			pic_updatepending();
			break;
		case 1: /*ICW2*/
			pic.vector=val&0xF8;
			if (pic.icw1 & 2) pic.icw=3;
			else		  pic.icw=2;
			break;
		case 2: /*ICW3*/
			pic.icw3 = val;
			pic_log("PIC1 ICW3 now %02X\n", val);
			if (pic.icw1 & 1) pic.icw=3;
			else		  pic.icw=0;
			break;
		case 3: /*ICW4*/
			pic.icw4 = val;
			pic.icw=0;
			break;
	}
    } else {
	if (val & 16) { /*ICW1*/
		pic.mask = 0;
		pic.mask2=0;
		pic.icw=1;
		pic.icw1=val;
		pic.ins = 0;
		pic_updatepending();
	}
	else if (!(val & 8)) { /*OCW2*/
		if ((val & 0xE0) == 0x60) {
			pic.ins &= ~(1 << (val & 7));
			pic_update_mask(&pic.mask2, pic.ins);
			if (AT) {
				if (((val&7) == pic2.icw3) && (pic2.pend&~pic2.mask)&~pic2.mask2)
					pic.pend |= pic.icw3;
			}

			if ((pic_current & (1 << (val & 7))) && picint_is_level(val & 7)) {
				if ((((1 << (val & 7)) != pic.icw3) || !AT))
					pic.pend |= 1 << (val & 7);
			}

			pic_updatepending();
		} else {
			for (c = 0; c < 8; c++) {
				if (pic.ins & (1 << c)) {
					pic.ins &= ~(1 << c);
					pic_update_mask(&pic.mask2, pic.ins);

					if (AT) {
						if (((1 << c) == pic.icw3) && (pic2.pend&~pic2.mask)&~pic2.mask2)
							pic.pend |= pic.icw3;
					}

					if ((pic_current & (1 << c)) && picint_is_level(c)) {
						if ((((1 << c) != pic.icw3) || !AT))
							pic.pend |= 1 << c;
					}

					if (c==1 && keywaiting)
						intclear&=~1;
					pic_updatepending();
					return;
				}
			}
		}
	} else {               /*OCW3*/
		if (val & 2)
			pic.read=(val & 1);
	}
    }
}


uint8_t
pic_read(uint16_t addr, void *priv)
{
    if (addr & 1) {
	pic_log("Read PIC mask %02X\n", pic.mask);
	return pic.mask;
    }
    if (pic.read) {
	pic_log("Read PIC ins %02X\n", pic.ins);
	return pic.ins | (pic2.ins ? 4 : 0);
    }
    return pic.pend;
}


void
pic_init()
{
    io_sethandler(0x0020, 0x0002, pic_read, NULL, NULL, pic_write, NULL, NULL, NULL);
}


static void
pic2_autoeoi()
{
    int c;

    for (c = 0; c < 8; c++) {
	if (pic2.ins & (1 << c)) {
		pic2.ins &= ~(1 << c);
		pic_update_mask(&pic2.mask2, pic2.ins);

		if (pic_current & (0x100 << c) && picint_is_level(c + 8)) {
			pic2.pend |= (1 << c);
			pic.pend |= (1 << pic2.icw3);
		}

		pic_updatepending();
		return;
	}
    }
}


void
pic2_write(uint16_t addr, uint8_t val, void *priv)
{
    int c;
    if (addr & 1) {
	switch (pic2.icw) {
		case 0: /*OCW1*/
			pic2.mask=val;
			pic_updatepending();
			break;
		case 1: /*ICW2*/
			pic2.vector=val & 0xF8;
			pic_log("PIC2 vector now: %02X\n", pic2.vector);
			if (pic2.icw1 & 2) pic2.icw=3;
			else		   pic2.icw=2;
			break;
		case 2: /*ICW3*/
			pic2.icw3 = val;
			pic_log("PIC2 ICW3 now %02X\n", val);
			if (pic2.icw1 & 1) pic2.icw=3;
			else		   pic2.icw=0;
			break;
		case 3: /*ICW4*/
			pic2.icw4 = val;
			pic2.icw=0;
			break;
	}
    } else {
	if (val & 16) { /*ICW1*/
		pic2.mask = 0;
		pic2.mask2=0;
		pic2.icw=1;
		pic2.icw1 = val;
		pic2.ins = 0;
		pic_updatepending();
	} else if (!(val & 8)) { /*OCW2*/
		if ((val & 0xE0) == 0x60) {
			pic2.ins &= ~(1 << (val & 7));
			pic_update_mask(&pic2.mask2, pic2.ins);

			if (pic_current & (0x100 << (val & 7)) && picint_is_level((val & 7) + 8)) {
				pic2.pend |= (1 << (val & 7));
				pic.pend |= (1 << pic2.icw3);
			}

			pic_updatepending();
		} else {
			for (c = 0; c < 8; c++) {
				if (pic2.ins&(1<<c)) {
					pic2.ins &= ~(1<<c);
					pic_update_mask(&pic2.mask2, pic2.ins);

					if (pic_current & (0x100 << c) && picint_is_level(c + 8)) {
						pic2.pend |= (1 << c);
						pic.pend |= (1 << pic2.icw3);
					}

					pic_updatepending();
					return;
				}
			}
		}
	} else {               /*OCW3*/
		if (val & 2)
			pic2.read=(val & 1);
	}
    }
}


uint8_t
pic2_read(uint16_t addr, void *priv)
{
    if (addr&1) {
	pic_log("Read PIC2 mask %02X %04X:%08X\n", pic2.mask, CS, cpu_state.pc);
	return pic2.mask;
    }
    if (pic2.read) {
	pic_log("Read PIC2 ins %02X %04X:%08X\n", pic2.ins, CS, cpu_state.pc);
	return pic2.ins;
    }
    pic_log("Read PIC2 pend %02X %04X:%08X\n", pic2.pend, CS, cpu_state.pc);
    return pic2.pend;
}


void
pic2_init()
{
    io_sethandler(0x00a0, 0x0002, pic2_read, NULL, NULL, pic2_write, NULL, NULL, NULL);
}


void
clearpic()
{
    pic.pend=pic.ins=pic_current=0;
    pic_updatepending();
}


void
picint_common(uint16_t num, int level)
{
    int c = 0;

    if (!num) {
	pic_log("Attempting to raise null IRQ\n");
	return;
    }

    if (AT && (num == pic.icw3) && (pic.icw3 == 4))
	num = 1 << 9;

    while (!(num & (1 << c)))
	c++;

    if (AT && (num == pic.icw3) && (pic.icw3 != 4)) {
	pic_log("Attempting to raise cascaded IRQ %i\n");
	return;
    }

    if (!(pic_current & num) || !level) {
	pic_log("Raising IRQ %i\n", c);

	if (level)
                pic_current |= num;

        if (num>0xFF) {
		if (!AT)
			return;

		pic2.pend|=(num>>8);
		if ((pic2.pend&~pic2.mask)&~pic2.mask2)
			pic.pend |= (1 << pic2.icw3);
        } else
                pic.pend|=num;

        pic_updatepending();
    }
}


void
picint(uint16_t num)
{
    picint_common(num, 0);
}


void
picintlevel(uint16_t num)
{
    picint_common(num, 1);
}


void
picintc(uint16_t num)
{
    int c = 0;

    if (!num) {
	pic_log("Attempting to lower null IRQ\n");
	return;
    }

    if (AT && (num == pic.icw3) && (pic.icw3 == 4))
	num = 1 << 9;

    while (!(num & (1 << c)))
	c++;

    if (AT && (num == pic.icw3) && (pic.icw3 != 4)) {
	pic_log("Attempting to lower cascaded IRQ %i\n");
	return;
    }

    if (pic_current & num)
        pic_current &= ~num;

    pic_log("Lowering IRQ %i\n", c);

    if (num > 0xff) {
	if (!AT)
		return;

	pic2.pend &= ~(num >> 8);
	if (!((pic2.pend&~pic2.mask)&~pic2.mask2))
		pic.pend &= ~(1 << pic2.icw3);
    } else
	pic.pend&=~num;

    pic_updatepending();
}


static uint8_t
pic_process_interrupt(PIC* target_pic, int c)
{
    uint8_t pending = target_pic->pend & ~target_pic->mask;

    int pic_int = c & 7;
    int pic_int_num = 1 << pic_int;

    if (pending & pic_int_num) {
	target_pic->pend &= ~pic_int_num;
	target_pic->ins |= pic_int_num;
	pic_update_mask(&target_pic->mask2, target_pic->ins);

	if (c >= 8) {
		pic.ins |= (1 << pic2.icw3); /*Cascade IRQ*/
		pic_update_mask(&pic.mask2, pic.ins);
	}

	pic_updatepending();

	if (target_pic->icw4 & 0x02)
		(c >= 8) ? pic2_autoeoi() : pic_autoeoi();

	if (!c)
		pit_set_gate(&pit2, 0, 0);

	return pic_int + target_pic->vector;
    } else
	return 0xFF;
}


uint8_t
picinterrupt()
{
    int c, d;
    uint8_t ret;

    for (c = 0; c <= 7; c++) {
	if (AT && ((1 << c) == pic.icw3)) {
		for (d = 8; d <= 15; d++) {
			ret = pic_process_interrupt(&pic2, d);
			if (ret != 0xFF)  return ret;
		}
	} else {
		ret = pic_process_interrupt(&pic, c);
		if (ret != 0xFF)  return ret;
	}
    }
    return 0xFF;
}


void
dumppic()
{
    pic_log("PIC1 : MASK %02X PEND %02X INS %02X LEVEL %02X VECTOR %02X CASCADE %02X\n", pic.mask, pic.pend, pic.ins, (pic.icw1 & 8) ? 1 : 0, pic.vector, pic.icw3);
    if (AT)
	pic_log("PIC2 : MASK %02X PEND %02X INS %02X LEVEL %02X VECTOR %02X CASCADE %02X\n", pic2.mask, pic2.pend, pic2.ins, (pic2.icw1 & 8) ? 1 : 0, pic2.vector, pic2.icw3);
}
