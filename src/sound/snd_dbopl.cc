/* Copyright holders: Sarah Walker, SA1988
   see COPYING for more details
*/
#include "dbopl.h"
#include "nukedopl.h"
#include "sound.h"
#include "snd_dbopl.h"


int opl_type = 0;


static struct
{
        DBOPL::Chip chip;
	opl3_chip opl3chip;
        int addr;
        int timer[2];
        uint8_t timer_ctrl;
        uint8_t status_mask;
        uint8_t status;
        int is_opl3;

        void (*timer_callback)(void *param, int timer, uint64_t period);
        void *timer_param;
} opl[2];

enum
{
        STATUS_TIMER_1 = 0x40,
        STATUS_TIMER_2 = 0x20,
        STATUS_TIMER_ALL = 0x80
};

enum
{
        CTRL_IRQ_RESET   = 0x80,
        CTRL_TIMER1_MASK = 0x40,
        CTRL_TIMER2_MASK = 0x20,
        CTRL_TIMER2_CTRL = 0x02,
        CTRL_TIMER1_CTRL = 0x01
};

void opl_init(void (*timer_callback)(void *param, int timer, uint64_t period), void *timer_param, int nr, int is_opl3)
{
	opl[nr].timer_callback = timer_callback;
	opl[nr].timer_param = timer_param;
	opl[nr].is_opl3 = is_opl3;

	if (!opl_type)
	{
		DBOPL::InitTables();
		opl[nr].chip.Setup(48000, is_opl3);
	}
	else
	{
		opl[nr].opl3chip.newm = 0;
		OPL3_Reset(&opl[nr].opl3chip, 48000);
	}
}

void opl_status_update(int nr)
{
        if (opl[nr].status & (STATUS_TIMER_1 | STATUS_TIMER_2) & opl[nr].status_mask)
                opl[nr].status |= STATUS_TIMER_ALL;
        else
                opl[nr].status &= ~STATUS_TIMER_ALL;
}

void opl_timer_over(int nr, int timer)
{
        if (!timer)
        {
                opl[nr].status |= STATUS_TIMER_1;
                opl[nr].timer_callback(opl[nr].timer_param, 0, opl[nr].timer[0] * 4);
        }
        else
        {
                opl[nr].status |= STATUS_TIMER_2;
                opl[nr].timer_callback(opl[nr].timer_param, 1, opl[nr].timer[1] * 16);
        }
                
        opl_status_update(nr);
}

void opl_write(int nr, uint16_t addr, uint8_t val)
{
        if (!(addr & 1))
	{
		if (!opl_type)
			opl[nr].addr = (int)opl[nr].chip.WriteAddr(addr, val) & 0x1ff;
		else
			opl[nr].addr = (int)OPL3_WriteAddr(&opl[nr].opl3chip, addr, val) & 0x1ff;
		if (!opl[nr].is_opl3)
			opl[nr].addr &= 0xff;
	}
        else
        {
		if (!opl_type)
			opl[nr].chip.WriteReg(opl[nr].addr, val);
		else {
			OPL3_WriteRegBuffered(&opl[nr].opl3chip, (uint16_t) opl[nr].addr, val);
			if (opl[nr].addr == 0x105)
				opl[nr].opl3chip.newm = opl[nr].addr & 0x01;
		}

                switch (opl[nr].addr)
                {
                        case 0x02: /*Timer 1*/
                        opl[nr].timer[0] = 256 - val;
                        break;
                        case 0x03: /*Timer 2*/
                        opl[nr].timer[1] = 256 - val;
                        break;
                        case 0x04: /*Timer control*/
                        if (val & CTRL_IRQ_RESET) /*IRQ reset*/
                        {
                                opl[nr].status &= ~(STATUS_TIMER_1 | STATUS_TIMER_2);
                                opl_status_update(nr);
                                return;
                        }
                        if ((val ^ opl[nr].timer_ctrl) & CTRL_TIMER1_CTRL)
                        {
                                if (val & CTRL_TIMER1_CTRL)
                                        opl[nr].timer_callback(opl[nr].timer_param, 0, opl[nr].timer[0] * 4);
                                else
                                        opl[nr].timer_callback(opl[nr].timer_param, 0, 0);
                        }
                        if ((val ^ opl[nr].timer_ctrl) & CTRL_TIMER2_CTRL)
                        {
                                if (val & CTRL_TIMER2_CTRL)
                                        opl[nr].timer_callback(opl[nr].timer_param, 1, opl[nr].timer[1] * 16);
                                else
                                        opl[nr].timer_callback(opl[nr].timer_param, 1, 0);
                        }
                        opl[nr].status_mask = (~val & (CTRL_TIMER1_MASK | CTRL_TIMER2_MASK)) | 0x80;
                        opl[nr].timer_ctrl = val;
                        break;
                }
        }
                
}

uint8_t opl_read(int nr, uint16_t addr)
{
        if (!(addr & 1))
        {
                return (opl[nr].status & opl[nr].status_mask) | (opl[nr].is_opl3 ? 0 : 0x06);
        }
        return opl[nr].is_opl3 ? 0 : 0xff;
}

void opl2_update(int nr, int16_t *buffer, int samples)
{
        int c;
        Bit32s buffer_32[samples];

	if (opl_type)
	{
		OPL3_GenerateStream(&opl[nr].opl3chip, buffer, samples);
	}
	else
	{
	        opl[nr].chip.GenerateBlock2(samples, buffer_32);

	        for (c = 0; c < samples; c++)
	                buffer[c*2] = (int16_t)buffer_32[c];
	}
}

void opl3_update(int nr, int16_t *buffer, int samples)
{
        int c;
        Bit32s buffer_32[samples*2];

	if (opl_type)
	{
		OPL3_GenerateStream(&opl[nr].opl3chip, buffer, samples);
	}
	else
	{
		opl[nr].chip.GenerateBlock3(samples, buffer_32);

		for (c = 0; c < samples*2; c++)
			buffer[c] = (int16_t)buffer_32[c];
	}
}
