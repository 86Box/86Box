/*This is the chipset used in the AMI 286 clone model*/
#include "ibm.h"
#include "io.h"
#include "neat.h"

static uint8_t neat_regs[256];
static int neat_index;
static int neat_emspage[4];

void neat_write(uint16_t port, uint8_t val, void *priv)
{
        switch (port)
        {
                case 0x22:
                neat_index = val;
                break;
                
                case 0x23:
                neat_regs[neat_index] = val;
                switch (neat_index)
                {
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

uint8_t neat_read(uint16_t port, void *priv)
{
        switch (port)
        {
                case 0x22:
                return neat_index;
                
                case 0x23:
                return neat_regs[neat_index];
        }
        return 0xff;
}

void neat_writeems(uint32_t addr, uint8_t val)
{
        ram[(neat_emspage[(addr >> 14) & 3] << 14) + (addr & 0x3FFF)] = val;
}

uint8_t neat_readems(uint32_t addr)
{
        return ram[(neat_emspage[(addr >> 14) & 3] << 14) + (addr & 0x3FFF)];
}

void neat_init()
{
        io_sethandler(0x0022, 0x0002, neat_read, NULL, NULL, neat_write, NULL, NULL,  NULL);
        io_sethandler(0x0208, 0x0002, neat_read, NULL, NULL, neat_write, NULL, NULL,  NULL);
        io_sethandler(0x4208, 0x0002, neat_read, NULL, NULL, neat_write, NULL, NULL,  NULL);
        io_sethandler(0x8208, 0x0002, neat_read, NULL, NULL, neat_write, NULL, NULL,  NULL);
        io_sethandler(0xc208, 0x0002, neat_read, NULL, NULL, neat_write, NULL, NULL,  NULL);
}
