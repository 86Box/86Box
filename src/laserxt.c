/*This is the chipset used in the LaserXT series model*/
#include "ibm.h"
#include "io.h"
#include "mem.h"

static int laserxt_emspage[4];
static int laserxt_emscontrol[4];
static mem_mapping_t laserxt_ems_mapping[4];
static int laserxt_ems_baseaddr_index = 0;

uint32_t get_laserxt_ems_addr(uint32_t addr)
{
        if(laserxt_emspage[(addr >> 14) & 3] & 0x80)
        {
                addr = 0xA0000 + ((laserxt_emspage[(addr >> 14) & 3] & 0x0F) << 14) + ((laserxt_emspage[(addr >> 14) & 3] & 0x40) << 12) + (addr & 0x3FFF);
        }
        return addr;
}

void laserxt_write(uint16_t port, uint8_t val, void *priv)
{
        int i;
        uint32_t paddr, vaddr;
        switch (port)
        {
                case 0x0208: case 0x4208: case 0x8208: case 0xC208:
                laserxt_emspage[port >> 14] = val;
                paddr = 0xC0000 + (port & 0xC000) + (((laserxt_ems_baseaddr_index + (4 - (port >> 14))) & 0x0C) << 14);
                if(val & 0x80)
                {
                        mem_mapping_enable(&laserxt_ems_mapping[port >> 14]);
                        vaddr = get_laserxt_ems_addr(paddr);
                        //pclog("Mapping address %05X to %06X\n", paddr, vaddr);
                        mem_mapping_set_exec(&laserxt_ems_mapping[port >> 14], ram + vaddr);
                }
                else
                {
                        //pclog("Unmap address %05X\n", paddr);
                        mem_mapping_disable(&laserxt_ems_mapping[port >> 14]);
                }
                //pclog("Write LaserXT port %04X to %02X at %04X:%04X\n", port, val, CS, cpu_state.pc);
                flushmmucache();
                break;
                case 0x0209: case 0x4209: case 0x8209: case 0xC209:
                laserxt_emscontrol[port >> 14] = val;
                laserxt_ems_baseaddr_index = 0;
                for(i=0; i<4; i++)
                {
                        laserxt_ems_baseaddr_index |= (laserxt_emscontrol[i] & 0x80) >> (7 - i);
                }
                //pclog("Set base_index to %d\n", laserxt_ems_baseaddr_index);
                if(laserxt_ems_baseaddr_index < 3)
                {
                        mem_mapping_disable(&romext_mapping);
                }
                else
                {
                        mem_mapping_enable(&romext_mapping);
                }

                mem_mapping_set_addr(&laserxt_ems_mapping[0], 0xC0000 + (((laserxt_ems_baseaddr_index + 4) & 0x0C) << 14), 0x4000);
                mem_mapping_set_addr(&laserxt_ems_mapping[1], 0xC4000 + (((laserxt_ems_baseaddr_index + 3) & 0x0C) << 14), 0x4000);
                mem_mapping_set_addr(&laserxt_ems_mapping[2], 0xC8000 + (((laserxt_ems_baseaddr_index + 2) & 0x0C) << 14), 0x4000);
                mem_mapping_set_addr(&laserxt_ems_mapping[3], 0xCC000 + (((laserxt_ems_baseaddr_index + 1) & 0x0C) << 14), 0x4000);
                //pclog("Write LaserXT port %04X to %02X at %04X:%04X\n", port, val, CS, cpu_state.pc);
                flushmmucache();
                break;
        }
}

uint8_t laserxt_read(uint16_t port, void *priv)
{
        switch (port)
        {
                case 0x0208: case 0x4208: case 0x8208: case 0xC208:
                //pclog("Read LaserXT port %04X at %04X:%04X\n", port, CS, cpu_state.pc);
                return laserxt_emspage[port >> 14];
                case 0x0209: case 0x4209: case 0x8209: case 0xC209:
                return laserxt_emscontrol[port >> 14];
                break;
        }
        return 0xff;
}

void mem_write_laserxtems(uint32_t addr, uint8_t val, void *priv)
{
        addr = get_laserxt_ems_addr(addr);
        if (addr < (mem_size << 10))
                ram[addr] = val;
}

uint8_t mem_read_laserxtems(uint32_t addr, void *priv)
{
        uint8_t val = 0xFF;
        addr = get_laserxt_ems_addr(addr);
        if (addr < (mem_size << 10))
                val = ram[addr];
        return val;
}

void laserxt_init()
{
        int i;

        if(mem_size > 640)
        {
                io_sethandler(0x0208, 0x0002, laserxt_read, NULL, NULL, laserxt_write, NULL, NULL,  NULL);
                io_sethandler(0x4208, 0x0002, laserxt_read, NULL, NULL, laserxt_write, NULL, NULL,  NULL);
                io_sethandler(0x8208, 0x0002, laserxt_read, NULL, NULL, laserxt_write, NULL, NULL,  NULL);
                io_sethandler(0xc208, 0x0002, laserxt_read, NULL, NULL, laserxt_write, NULL, NULL,  NULL);
        }

        for (i = 0; i < 4; i++)
        {
                laserxt_emspage[i] = 0x7F;
                laserxt_emscontrol[i] = (i == 3) ? 0x00 : 0x80;
                mem_mapping_add(&laserxt_ems_mapping[i], 0xE0000 + (i << 14), 0x4000, mem_read_laserxtems, NULL, NULL, mem_write_laserxtems, NULL, NULL, ram + 0xA0000 + (i << 14), 0, NULL);
                mem_mapping_disable(&laserxt_ems_mapping[i]);
        }
        mem_set_mem_state(0x0c0000, 0x40000, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
}
