#include "ibm.h"
#include "io.h"
#include "pic.h"
#include "pit.h"


int output;
int intclear;
int keywaiting=0;
int pic_intpending;
PIC pic, pic2;


void pic_updatepending()
{
	uint16_t temp_pending = 0;
	if (AT)
	{
	        if ((pic2.pend&~pic2.mask)&~pic2.mask2)
        	        pic.pend |= (1 << 2);
	        else
        	        pic.pend &= ~(1 << 2);
	}
        pic_intpending = (pic.pend & ~pic.mask) & ~pic.mask2;
	if (AT)
	{
	        if (!((pic.mask | pic.mask2) & (1 << 2)))
		{
			temp_pending = ((pic2.pend&~pic2.mask)&~pic2.mask2);
			temp_pending <<= 8;
        	        pic_intpending |= temp_pending;
		}
	}
/*        pclog("pic_intpending = %i  %02X %02X %02X %02X\n", pic_intpending, pic.ins, pic.pend, pic.mask, pic.mask2);
        pclog("                    %02X %02X %02X %02X %i %i\n", pic2.ins, pic2.pend, pic2.mask, pic2.mask2, ((pic.mask | pic.mask2) & (1 << 2)), ((pic2.pend&~pic2.mask)&~pic2.mask2)); */
}


void pic_reset()
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

void pic_update_mask(uint8_t *mask, uint8_t ins)
{
        int c;
        *mask = 0;
        for (c = 0; c < 8; c++)
        {
                if (ins & (1 << c))
                {
                        *mask = 0xff << c;
                        return;
                }
        }
}

static void pic_autoeoi()
{
        int c;
        
        for (c=0;c<8;c++)
        {
                if (pic.ins&(1<<c))
                {
                        pic.ins&=~(1<<c);
                        pic_update_mask(&pic.mask2, pic.ins);

			if (AT)
			{
	                        if (c == 2 && (pic2.pend&~pic2.mask)&~pic2.mask2)
	                                pic.pend |= (1 << 2);
			}

                        pic_updatepending();
                        return;
                }
        }
}

void pic_write(uint16_t addr, uint8_t val, void *priv)
{
        int c;
        if (addr&1)
        {
                switch (pic.icw)
                {
                        case 0: /*OCW1*/
                        pic.mask=val;
                        pic_updatepending();
                        break;
                        case 1: /*ICW2*/
                        pic.vector=val&0xF8;
                        if (pic.icw1&2) pic.icw=3;
                        else            pic.icw=2;
                        break;
                        case 2: /*ICW3*/
                        if (pic.icw1&1) pic.icw=3;
                        else            pic.icw=0;
                        break;
                        case 3: /*ICW4*/
                        pic.icw4 = val;
                        pic.icw=0;
                        break;
                }
        }
        else
        {
                if (val&16) /*ICW1*/
                {
                        pic.mask = 0;
                        pic.mask2=0;
                        pic.icw=1;
                        pic.icw1=val;
                        pic.ins = 0;
                        pic_updatepending();
                }
                else if (!(val&8)) /*OCW2*/
                {
                        if ((val&0xE0)==0x60)
                        {
                                pic.ins&=~(1<<(val&7));
                                pic_update_mask(&pic.mask2, pic.ins);
				if (AT)
				{
	                                if ((val&7) == 2 && (pic2.pend&~pic2.mask)&~pic2.mask2)
        	                                pic.pend |= (1 << 2);
				}
                                pic_updatepending();
                        }
                        else
                        {
                                for (c=0;c<8;c++)
                                {
                                        if (pic.ins&(1<<c))
                                        {
                                                pic.ins&=~(1<<c);
                                                pic_update_mask(&pic.mask2, pic.ins);

						if (AT)
						{
	                                                if (c == 2 && (pic2.pend&~pic2.mask)&~pic2.mask2)
        	                                                pic.pend |= (1 << 2);
						}

                                                if (c==1 && keywaiting)
                                                {
                                                        intclear&=~1;
                                                }
                                                pic_updatepending();
                                                return;
                                        }
                                }
                        }
                }
                else               /*OCW3*/
                {
                        if (val&2) pic.read=(val&1);
                        if (val&0x40) { }
                }
        }
}

uint8_t pic_read(uint16_t addr, void *priv)
{
        if (addr&1) { /*pclog("Read PIC mask %02X\n",pic.mask);*/ return pic.mask; }
        if (pic.read) { /*pclog("Read PIC ins %02X\n",pic.ins);*/ return pic.ins | (pic2.ins ? 4 : 0); }
        return pic.pend;
}

void pic_init()
{
        io_sethandler(0x0020, 0x0002, pic_read, NULL, NULL, pic_write, NULL, NULL, NULL);
}

static void pic2_autoeoi()
{
        int c;
        
        for (c = 0; c < 8; c++)
        {
                if (pic2.ins & (1 << c))
                {
                        pic2.ins &= ~(1 << c);
                        pic_update_mask(&pic2.mask2, pic2.ins);

                        pic_updatepending();
                        return;
                }
        }
}

void pic2_write(uint16_t addr, uint8_t val, void *priv)
{
        int c;
        if (addr&1)
        {
                switch (pic2.icw)
                {
                        case 0: /*OCW1*/
                        pic2.mask=val;
                        pic_updatepending();
                        break;
                        case 1: /*ICW2*/
                        pic2.vector=val&0xF8;
                        if (pic2.icw1&2) pic2.icw=3;
                        else            pic2.icw=2;
                        break;
                        case 2: /*ICW3*/
                        if (pic2.icw1&1) pic2.icw=3;
                        else            pic2.icw=0;
                        break;
                        case 3: /*ICW4*/
                        pic2.icw4 = val;
                        pic2.icw=0;
                        break;
                }
        }
        else
        {
                if (val&16) /*ICW1*/
                {
                        pic2.mask = 0;
                        pic2.mask2=0;
                        pic2.icw=1;
                        pic2.icw1=val;
                        pic2.ins = 0;
                        pic_updatepending();
                }
                else if (!(val&8)) /*OCW2*/
                {
                        if ((val&0xE0)==0x60)
                        {
                                pic2.ins&=~(1<<(val&7));
                                pic_update_mask(&pic2.mask2, pic2.ins);

                                pic_updatepending();
                        }
                        else
                        {
                                for (c=0;c<8;c++)
                                {
                                        if (pic2.ins&(1<<c))
                                        {
                                                pic2.ins &= ~(1<<c);
                                                pic_update_mask(&pic2.mask2, pic2.ins);
                                                        
                                                pic_updatepending();
                                                return;
                                        }
                                }
                        }
                }
                else               /*OCW3*/
                {
                        if (val&2) pic2.read=(val&1);
                }
        }
}

uint8_t pic2_read(uint16_t addr, void *priv)
{
        if (addr&1) { /*pclog("Read PIC2 mask %02X %04X:%08X\n",pic2.mask,CS,pc);*/ return pic2.mask; }
        if (pic2.read) { /*pclog("Read PIC2 ins %02X %04X:%08X\n",pic2.ins,CS,pc);*/ return pic2.ins; }
        /*pclog("Read PIC2 pend %02X %04X:%08X\n",pic2.pend,CS,pc);*/
        return pic2.pend;
}

void pic2_init()
{
        io_sethandler(0x00a0, 0x0002, pic2_read, NULL, NULL, pic2_write, NULL, NULL, NULL);
}


void clearpic()
{
        pic.pend=pic.ins=0;
        pic_updatepending();
}

int pic_current[16];

void picint(uint16_t num)
{
        if (AT && (num == (1 << 2)))
                num = 1 << 9;
        if (num>0xFF)
        {
		if (!AT)
		{
			return;
		}

                pic2.pend|=(num>>8);
                if ((pic2.pend&~pic2.mask)&~pic2.mask2)
                        pic.pend |= (1 << 2);
        }
        else
        {
                pic.pend|=num;
        }
/* if (num == 0x40)
{
        pclog("picint : PEND now %02X %02X\n", pic.pend, pic2.pend);
} */
        pic_updatepending();
/*	if (num == 0x40)
	{
		pclog("Processing FDC interrupt, pending: %s, previously pending: %s, masked: %s, masked (2): %s, T: %s, I: %s\n", (pic_intpending & num) ? "yes" : "no", (old_pend & num) ? "yes" : "no", (pic.mask & num) ? "yes" : "no", (pic.mask2 & num) ? "yes" : "no", (flags & 0x100) ? "yes" : "no", (flags&I_FLAG) ? "yes" : "no");
	} */
}

void picintlevel(uint16_t num)
{
        int c = 0;
        while (!(num & (1 << c))) c++;
        if (AT && (c == 2))
        {
                c = 9;
                num = 1 << 9;
        }
        if (!pic_current[c])
        {
                pic_current[c]=1;
                if (num>0xFF)
                {
			if (!AT)
			{
				return;
			}

                        pic2.pend|=(num>>8);
                }
                else
                {
                        pic.pend|=num;
                }
        }
        pic_updatepending();
}
void picintc(uint16_t num)
{
        int c = 0;
        if (!num)
                return;
        while (!(num & (1 << c))) c++;
        if (AT && (c == 2))
        {
                c = 9;
                num = 1 << 9;
        }
        pic_current[c]=0;

        if (num > 0xff)
        {
		if (!AT)
		{
			return;
		}

                pic2.pend &= ~(num >> 8);
                if (!((pic2.pend&~pic2.mask)&~pic2.mask2))
                        pic.pend &= ~(1 << 2);
        }
        else
        {
                pic.pend&=~num;
        }
        pic_updatepending();
}

uint8_t picinterrupt()
{
        uint8_t temp=pic.pend&~pic.mask;
        int c;
        for (c = 0; c < 2; c++)
        {
                if (temp & (1 << c))
                {
                        pic.pend &= ~(1 << c);
                        pic.ins |= (1 << c);
                        pic_update_mask(&pic.mask2, pic.ins);                      
                   
                        pic_updatepending();
                        if (!c)
                                pit_set_gate(&pit2, 0, 0);
                        
                        if (pic.icw4 & 0x02)
                                pic_autoeoi();
                                
                        return c+pic.vector;
                }
        }
        if ((temp & (1 << 2)) && !AT)
        {
                if (temp & (1 << 2))
                {
                        pic.pend &= ~(1 << 2);
                        pic.ins |= (1 << 2);
                        pic_update_mask(&pic.mask2, pic.ins);                      
                        pic_updatepending();
                        
                        if (pic.icw4 & 0x02)
                                pic_autoeoi();
                                
                        return c+pic.vector;
                }
	}
        if ((temp & (1 << 2)) && AT)
        {
                uint8_t temp2 = pic2.pend & ~pic2.mask;
                for (c = 0; c < 8; c++)
                {
                        if (temp2 & (1 << c))
                        {
                                pic2.pend &= ~(1 << c);
                                pic2.ins |= (1 << c);
                                pic_update_mask(&pic2.mask2, pic2.ins);
                        
                                pic.ins |= (1 << 2); /*Cascade IRQ*/
                                pic_update_mask(&pic.mask2, pic.ins);

                                pic_updatepending();

                                if (pic2.icw4 & 0x02)
                                        pic2_autoeoi();

                                return c+pic2.vector;
                        }
                }
        }
        for (c = 3; c < 8; c++)
        {
                if (temp & (1 << c))
                {
                        pic.pend &= ~(1 << c);
                        pic.ins |= (1 << c);
                        pic_update_mask(&pic.mask2, pic.ins);                      
                        pic_updatepending();

                        if (pic.icw4 & 0x02)
                                pic_autoeoi();

                        return c+pic.vector;
                }
        }
        return 0xFF;
}

void dumppic()
{
        pclog("PIC1 : MASK %02X PEND %02X INS %02X VECTOR %02X\n",pic.mask,pic.pend,pic.ins,pic.vector);
	if (AT)
	{
	        pclog("PIC2 : MASK %02X PEND %02X INS %02X VECTOR %02X\n",pic2.mask,pic2.pend,pic2.ins,pic2.vector);
	}
}

