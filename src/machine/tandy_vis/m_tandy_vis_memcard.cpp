#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
extern "C"
{
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/nmi.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/nvr.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/fdc_ext.h>
#include <86box/gameport.h>
#include <86box/keyboard.h>
#include <86box/sound.h>
#include <86box/snd_sn76489.h>
#include <86box/machine.h>
#include <86box/m_tandy.h>
#include <86box/plat_unused.h>
}

class ds6417_memcard
{
public:
    void reset()
    {
        m_read = false;
        m_start = false;
        m_count = 0;
        m_command = 0;
    }

	void data_w(int state) { if(!m_read) m_data = state; }
	void clock_w(int state);
	void reset_w(int state) { if(!state && m_reset) reset(); m_reset = state; }
	int data_r() { return m_read ? m_data : 0; }
private:
	uint8_t calccrc(uint8_t bit, uint8_t crc) const;
	enum {
		CMD_READ = 0x06,
		CMD_WRITE = 0x11,
		CMD_READPROT = 0x05,
		CMD_WRITEPROT = 0x0e,
		CMD_READMASK = 0x18,
		CMD_READCRC = 0x03
	};
    
    FILE* file;

	bool m_reset;
	bool m_clk;
	bool m_data;
	bool m_read;
	bool m_start;
	uint8_t m_count;
	uint8_t m_shiftreg;
	uint8_t m_command;
	uint8_t m_crc;
	uint8_t m_selbits;
	uint32_t m_addr;
	uint16_t m_select;
	uint16_t m_selectval;
};

uint8_t ds6417_memcard::calccrc(uint8_t bit, uint8_t crc) const
{
	bit = (crc ^ bit) & 1;
	if(bit)
		return ((crc >> 1) | (bit << 7)) ^ 0x66;
	else
		return crc >> 1;
}

void ds6417_memcard::clock_w(int state)
{
	if(!m_reset)
		return;

	if(m_clk == (state != 0))
		return;

	m_clk = state;

	if(m_read && !m_clk && m_start)
	{
		if(!(m_count & 7))
		{
			switch(m_command)
			{
				case CMD_READ:
				case CMD_READMASK:
					fread(&m_shiftreg, 1, 1, file);
					break;
				case CMD_READPROT:
					m_shiftreg = m_selectval;
					break;
				case CMD_READCRC:
					m_shiftreg = m_crc;
					break;
			}
		}

		m_data = m_shiftreg & 1;
		m_shiftreg >>= 1;
		m_count++;
		m_crc = calccrc(m_data, m_crc);
	}
	else if(m_clk && !m_read)
	{
		m_shiftreg = (m_shiftreg >> 1) | (m_data ? 0x80 : 0);
		m_count++;

		if(m_start)
		{
			m_crc = calccrc(m_data, m_crc);
			if(!(m_count & 7))
			{
				switch(m_command)
				{
					case CMD_WRITE:
						fwrite(&m_shiftreg, 1, 1, file);
						break;
					case CMD_WRITEPROT:
						m_selectval = m_shiftreg;
						break;
				}
			}
		}
		else
		{
			switch(m_count)
			{
				case 8:
					if((m_shiftreg != 0xe8) && (m_shiftreg != 0x17))
						reset();
					break;
				case 16:
					m_addr = m_shiftreg;
					break;
				case 24:
					m_addr |= m_shiftreg << 8;
					break;
				case 32:
					m_addr |= (m_shiftreg & 7) << 16;
					m_command = m_shiftreg >> 3;
					break;
				case 40:
					m_select = m_shiftreg;
					break;
				case 48:
					m_select |= m_shiftreg << 8;
					break;
				case 56:
					// command crc
					if((m_command & CMD_READMASK) == CMD_READMASK)
					{
						m_selbits = m_command & 7;
						m_command &= 0x18;
					}
					switch(m_command)
					{
						case CMD_READ:
						case CMD_READMASK:
							m_crc = 0; [[fallthrough]];
						case CMD_READPROT:
						case CMD_READCRC:
							m_read = true;
							break;
						case CMD_WRITE:
							m_crc = 0; [[fallthrough]];
						case CMD_WRITEPROT:
							break;
						default:
							reset();
							return;

					}
					m_start = true;
					fseek(file, m_addr & 0x7fff, SEEK_SET);
					break;
			}
		}
	}
}

