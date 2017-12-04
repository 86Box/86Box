/* Emulation for C&T 82C206 ("NEAT") chipset. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../io.h"
#include "machine.h"


static uint8_t neat_regs[256];
static int neat_index;
static int neat_emspage[4];


static void
neat_write(uint16_t port, uint8_t val, void *priv)
{
    switch (port) {
	case 0x22:
		neat_index = val;
		break;

	case 0x23:
		neat_regs[neat_index] = val;
		switch (neat_index) {
			case 0x6E: /*EMS page extension*/
				neat_emspage[3] = (neat_emspage[3] & 0x7F) | (( val       & 3) << 7);
				neat_emspage[2] = (neat_emspage[2] & 0x7F) | (((val >> 2) & 3) << 7);
				neat_emspage[1] = (neat_emspage[1] & 0x7F) | (((val >> 4) & 3) << 7);
				neat_emspage[0] = (neat_emspage[0] & 0x7F) | (((val >> 6) & 3) << 7);
				break;
		}
		break;

	case 0x0208: case 0x0209: case 0x4208: case 0x4209:
	case 0x8208: case 0x8209: case 0xC208: case 0xC209:
		neat_emspage[port >> 14] = (neat_emspage[port >> 14] & 0x180) | (val & 0x7F);		
		break;
    }
}


static uint8_t
neat_read(uint16_t port, void *priv)
{
    uint8_t ret = 0xff;

    switch (port) {
	case 0x22:
		ret = neat_index;
		break;

	case 0x23:
		ret = neat_regs[neat_index];
		break;
    }

    return(ret);
}


#if NOT_USED
static void
neat_writeems(uint32_t addr, uint8_t val)
{
    ram[(neat_emspage[(addr >> 14) & 3] << 14) + (addr & 0x3FFF)] = val;
}


static uint8_t
neat_readems(uint32_t addr)
{
    return ram[(neat_emspage[(addr >> 14) & 3] << 14) + (addr & 0x3FFF)];
}
#endif


static void
neat_init(void)
{
    io_sethandler(0x0022, 2,
		  neat_read,NULL,NULL, neat_write,NULL,NULL, NULL);
    io_sethandler(0x0208, 2,
		  neat_read,NULL,NULL, neat_write,NULL,NULL, NULL);
    io_sethandler(0x4208, 2,
		  neat_read,NULL,NULL, neat_write,NULL,NULL, NULL);
    io_sethandler(0x8208, 2,
		  neat_read,NULL,NULL, neat_write,NULL,NULL, NULL);
    io_sethandler(0xc208, 2,
		  neat_read,NULL,NULL, neat_write,NULL,NULL, NULL);
}


void
machine_at_4gpv31_init(machine_t *model)
{
	machine_at_init(model);

	neat_init();
}
