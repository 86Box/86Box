/*This is the chipset used in the Award 286 clone model*/
#include "ibm.h"
#include "cpu/cpu.h"
#include "io.h"
#include "mem.h"
#include "scat.h"
#include "cpu/x86.h"
#include "cpu/cpu.h"


static uint8_t scat_regs[256];
static int scat_index;
static uint8_t scat_port_92 = 0;
static uint8_t scat_ems_reg_2xA = 0;
static mem_mapping_t scat_mapping[32];
static mem_mapping_t scat_high_mapping[16];
static scat_t scat_stat[32];
static uint32_t scat_xms_bound;
static mem_mapping_t scat_shadowram_mapping[6];
static mem_mapping_t scat_4000_9FFF_mapping[24];
static mem_mapping_t scat_A000_BFFF_mapping;


uint8_t scat_read(uint16_t port, void *priv);
void scat_write(uint16_t port, uint8_t val, void *priv);


void scat_shadow_state_update(void)
{
        int i, val;

        /* TODO - ROMCS enable features should be implemented later. */
        for (i = 0; i < 24; i++)
        {
                val = ((scat_regs[SCAT_SHADOW_RAM_ENABLE_1 + (i >> 3)] >> (i & 7)) & 1) ? MEM_READ_INTERNAL : MEM_READ_EXTERNAL;
                if (i < 8)
                {
                        val |= ((scat_regs[SCAT_SHADOW_RAM_ENABLE_1 + (i >> 3)] >> (i & 7)) & 1) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTERNAL;
                }
                else
                {
                        if ((scat_regs[SCAT_RAM_WRITE_PROTECT] >> ((i - 8) >> 1)) & 1)
                        {
                                val |= MEM_WRITE_DISABLED;
                        }
                        else
                        {
                                val |= ((scat_regs[SCAT_SHADOW_RAM_ENABLE_1 + (i >> 3)] >> (i & 7)) & 1) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTERNAL;
                        }
                }
                mem_set_mem_state((i + 40) << 14, 0x4000, val);
        }

        flushmmucache();
}


void scat_set_xms_bound(uint8_t val)
{
        uint32_t max_xms_size = (mem_size >= 16384) ? 0xFC0000 : mem_size << 10;
	int i;

        switch (val & 0x0F)
        {
                case 1:
                scat_xms_bound = 0x100000;
                break;
                case 2:
                scat_xms_bound = 0x140000;
                break;
                case 3:
                scat_xms_bound = 0x180000;
                break;
                case 4:
                scat_xms_bound = 0x200000;
                break;
                case 5:
                scat_xms_bound = 0x300000;
                break;
                case 6:
                scat_xms_bound = 0x400000;
                break;
                case 7:
                scat_xms_bound = 0x600000;
                break;
                case 8:
                scat_xms_bound = 0x800000;
                break;
                case 9:
                scat_xms_bound = 0xA00000;
                break;
                case 10:
                scat_xms_bound = 0xC00000;
                break;
                case 11:
                scat_xms_bound = 0xE00000;
                break;
                default:
                scat_xms_bound = (mem_size >= 16384 ? 0xFC0000 : mem_size << 10);
                break;
        }

        if ((val & 0x40) == 0 && (scat_regs[SCAT_DRAM_CONFIGURATION] & 0x0F) == 3)
        {
                if (val != 1)
                {
                        if(mem_size > 1024) mem_mapping_disable(&ram_high_mapping);
                        for(i=0;i<6;i++)
                                mem_mapping_enable(&scat_shadowram_mapping[i]);
                        if ((val & 0x0F) == 0)
                                scat_xms_bound = 0x160000;
                }
                else
                {
                        for(i=0;i<6;i++)
                                mem_mapping_disable(&scat_shadowram_mapping[i]);
                        if(mem_size > 1024) mem_mapping_enable(&ram_high_mapping);
                }
                pclog("Set XMS bound(%02X) = %06X(%dKbytes for EMS access)\n", val, scat_xms_bound, (0x160000 - scat_xms_bound) >> 10);
                if (scat_xms_bound > 0x100000)
                        mem_set_mem_state(0x100000, scat_xms_bound - 0x100000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                if (scat_xms_bound < 0x160000)
                        mem_set_mem_state(scat_xms_bound, 0x160000 - scat_xms_bound, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
        }
        else
        {
                for(i=0;i<6;i++)
                        mem_mapping_disable(&scat_shadowram_mapping[i]);
                if(mem_size > 1024) mem_mapping_enable(&ram_high_mapping);

                if (scat_xms_bound > max_xms_size)
                        scat_xms_bound = max_xms_size;
                pclog("Set XMS bound(%02X) = %06X(%dKbytes for EMS access)\n", val, scat_xms_bound, ((mem_size << 10) - scat_xms_bound) >> 10);
                if (scat_xms_bound > 0x100000)
                        mem_set_mem_state(0x100000, scat_xms_bound - 0x100000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                if (scat_xms_bound < (mem_size << 10))
                        mem_set_mem_state(scat_xms_bound, (mem_size << 10) - scat_xms_bound, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
        }
}


uint32_t get_scat_addr(uint32_t addr, scat_t *p)
{
        if (p && (scat_regs[SCAT_EMS_CONTROL] & 0x80) && (p->regs_2x9 & 0x80))
        {
                addr = (addr & 0x3fff) | (((p->regs_2x9 & 3) << 8) | p->regs_2x8) << 14;
        }

        if (mem_size < 2048 && ((scat_regs[SCAT_DRAM_CONFIGURATION] & 0x0F) > 7 || (scat_regs[SCAT_EXTENDED_BOUNDARY] & 0x40) != 0))
                addr = (addr & ~0x780000) | ((addr & 0x600000) >> 2);
        else if((scat_regs[SCAT_DRAM_CONFIGURATION] & 0x0F) < 8 && (scat_regs[SCAT_EXTENDED_BOUNDARY] & 0x40) == 0)
        {
                addr &= ~0x600000;
                if(mem_size > 2048 || (mem_size == 2048 && (scat_regs[SCAT_DRAM_CONFIGURATION] & 0x0F) < 6))
                        addr |= (addr & 0x180000) << 2;
        }

        if ((scat_regs[SCAT_EXTENDED_BOUNDARY] & 0x40) == 0 && (scat_regs[SCAT_DRAM_CONFIGURATION] & 0x0F) == 3 && (addr & ~0x600000) >= 0x100000 && (addr & ~0x600000) < 0x160000)
                addr ^= mem_size < 2048 ? 0x1F0000 : 0x670000;
        return addr;
}


void scat_memmap_state_update(void)
{
        int i;
        uint32_t addr;

        for(i=16;i<24;i++)
        {
                addr = get_scat_addr(0x40000 + (i << 14), NULL);
                mem_mapping_set_exec(&scat_4000_9FFF_mapping[i], addr < (mem_size << 10) ? ram + addr : NULL);
        }
        addr = get_scat_addr(0xA0000, NULL);
        mem_mapping_set_exec(&scat_A000_BFFF_mapping, addr < (mem_size << 10) ? ram + addr : NULL);
        for(i=0;i<6;i++)
        {
                addr = get_scat_addr(0x100000 + (i << 16), NULL);
                mem_mapping_set_exec(&scat_shadowram_mapping[i], addr < (mem_size << 10) ? ram + addr : NULL);
        }

        flushmmucache();
}


void scat_set_global_EMS_state(int state)
{
        int i;
        uint32_t base_addr, virt_addr;

        for(i=0; i<32; i++)
        {
                base_addr = (i + 16) << 14;
                if(i >= 24)
                        base_addr += 0x30000;
                if(state && (scat_stat[i].regs_2x9 & 0x80))
                {
                        virt_addr = get_scat_addr(base_addr, &scat_stat[i]);
                        if(i < 24) mem_mapping_disable(&scat_4000_9FFF_mapping[i]);
                        mem_mapping_enable(&scat_mapping[i]);
                        if(virt_addr < (mem_size << 10)) mem_mapping_set_exec(&scat_mapping[i], ram + virt_addr);
                        else mem_mapping_set_exec(&scat_mapping[i], NULL);
                }
                else
                {
                        mem_mapping_set_exec(&scat_mapping[i], ram + base_addr);
                        mem_mapping_disable(&scat_mapping[i]);
                        if(i < 24) mem_mapping_enable(&scat_4000_9FFF_mapping[i]);
                }
        }
}


void scat_write(uint16_t port, uint8_t val, void *priv)
{
        uint8_t scat_reg_valid = 0, scat_shadow_update = 0, scat_map_update = 0, index;
        uint32_t base_addr, virt_addr;
 
        switch (port)
        {
                case 0x22:
                scat_index = val;
                break;
                
                case 0x23:
                switch (scat_index)
                {
                        case SCAT_CLOCK_CONTROL:
                        case SCAT_PERIPHERAL_CONTROL:
                        scat_reg_valid = 1;
                        break;
                        case SCAT_EMS_CONTROL:
                        if(val & 0x40)
                        {
                                if(val & 1)
                                {
                                        io_sethandler(0x0218, 0x0003, scat_read, NULL, NULL, scat_write, NULL, NULL,  NULL);
                                        io_removehandler(0x0208, 0x0003, scat_read, NULL, NULL, scat_write, NULL, NULL,  NULL);
                                }
                                else
                                {
                                        io_sethandler(0x0208, 0x0003, scat_read, NULL, NULL, scat_write, NULL, NULL,  NULL);
                                        io_removehandler(0x0218, 0x0003, scat_read, NULL, NULL, scat_write, NULL, NULL,  NULL);
                                }
                        }
                        else
                        {
                                io_removehandler(0x0208, 0x0003, scat_read, NULL, NULL, scat_write, NULL, NULL,  NULL);
                                io_removehandler(0x0218, 0x0003, scat_read, NULL, NULL, scat_write, NULL, NULL,  NULL);
                        }
                        scat_set_global_EMS_state(val & 0x80);
                        scat_reg_valid = 1;
                        break;
                        case SCAT_POWER_MANAGEMENT:
                        val &= 0x40;
                        scat_reg_valid = 1;
                        break;
                        case SCAT_DRAM_CONFIGURATION:
                        if((scat_regs[SCAT_EXTENDED_BOUNDARY] & 0x40) == 0)
                        {
                                if((val & 0x0F) == 3)
                                {
                                        if(mem_size > 1024) mem_mapping_disable(&ram_high_mapping);
                                        for(index=0;index<6;index++)
                                                mem_mapping_enable(&scat_shadowram_mapping[index]);
                                }
                                else
                                {
                                        for(index=0;index<6;index++)
                                                mem_mapping_disable(&scat_shadowram_mapping[index]);
                                        if(mem_size > 1024) mem_mapping_enable(&ram_high_mapping);
                                }
                        }
                        else
                        {
                                for(index=0;index<6;index++)
                                        mem_mapping_disable(&scat_shadowram_mapping[index]);
                                if(mem_size > 1024) mem_mapping_enable(&ram_high_mapping);
                        }
                        scat_map_update = 1;

                        cpu_waitstates = (val & 0x70) == 0 ? 1 : 2;
                        cpu_update_waitstates();

                        scat_reg_valid = 1;
                        break;
                        case SCAT_EXTENDED_BOUNDARY:
                        scat_set_xms_bound(val & 0x4f);
                        mem_set_mem_state(0x40000, 0x60000, (val & 0x20) ? MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL : MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                        if((val ^ scat_regs[SCAT_EXTENDED_BOUNDARY]) & 0x40) scat_map_update = 1;
                        scat_reg_valid = 1;
                        break;
                        case SCAT_ROM_ENABLE:
                        case SCAT_RAM_WRITE_PROTECT:
                        case SCAT_SHADOW_RAM_ENABLE_1:
                        case SCAT_SHADOW_RAM_ENABLE_2:
                        case SCAT_SHADOW_RAM_ENABLE_3:
                        scat_reg_valid = 1;
                        scat_shadow_update = 1;
                        break;
                        default:
                        break;
                }
                if (scat_reg_valid)
                        scat_regs[scat_index] = val;
#ifndef RELEASE_BUILD
                else pclog("Attemped to write unimplemented SCAT register %02X at %04X:%04X\n", scat_index, val, CS, cpu_state.pc);
#endif
                if (scat_shadow_update)
                        scat_shadow_state_update();
                if (scat_map_update)
                        scat_memmap_state_update();
                break;

                case 0x92:
                if ((mem_a20_alt ^ val) & 2)
                {
                         mem_a20_alt = val & 2;
                         mem_a20_recalc();
                }
                if ((~scat_port_92 & val) & 1)
                {
                         softresetx86();
                        cpu_set_edx();
                }
                scat_port_92 = val;
                break;

                case 0x208:
                case 0x218:
                if ((scat_regs[SCAT_EMS_CONTROL] & 0x41) == (0x40 | ((port & 0x10) >> 4)))
                {
                        index = scat_ems_reg_2xA & 0x1F;
                        scat_stat[index].regs_2x8 = val;
                        base_addr = (index + 16) << 14;
                        if(index >= 24)
                                base_addr += 0x30000;

                        if((scat_regs[SCAT_EMS_CONTROL] & 0x80) && (scat_stat[index].regs_2x9 & 0x80))
                        {
                                virt_addr = get_scat_addr(base_addr, &scat_stat[index]);
                                if(virt_addr < (mem_size << 10)) mem_mapping_set_exec(&scat_mapping[index], ram + virt_addr);
                                else mem_mapping_set_exec(&scat_mapping[index], NULL);
                                flushmmucache();
                        }
                }
                break;
                case 0x209:
                case 0x219:
                if ((scat_regs[SCAT_EMS_CONTROL] & 0x41) == (0x40 | ((port & 0x10) >> 4)))
                {
                        index = scat_ems_reg_2xA & 0x1F;
                        scat_stat[index].regs_2x9 = val;
                        base_addr = (index + 16) << 14;
                        if(index >= 24)
                                base_addr += 0x30000;

                        if (scat_regs[SCAT_EMS_CONTROL] & 0x80)
                        {
                                if (val & 0x80)
                                {
                                        virt_addr = get_scat_addr(base_addr, &scat_stat[index]);
                                        if(index < 24) mem_mapping_disable(&scat_4000_9FFF_mapping[index]);
                                        if(virt_addr < (mem_size << 10)) mem_mapping_set_exec(&scat_mapping[index], ram + virt_addr);
                                        else mem_mapping_set_exec(&scat_mapping[index], NULL);
                                        mem_mapping_enable(&scat_mapping[index]);
                                }
                                else
                                {
                                        mem_mapping_set_exec(&scat_mapping[index], ram + base_addr);
                                        mem_mapping_disable(&scat_mapping[index]);
                                        if(index < 24) mem_mapping_enable(&scat_4000_9FFF_mapping[index]);
                                }
                                flushmmucache();
                        }

                        if (scat_ems_reg_2xA & 0x80)
                        {
                                scat_ems_reg_2xA = (scat_ems_reg_2xA & 0xe0) | ((scat_ems_reg_2xA + 1) & 0x1f);
                        }
                }
                break;
                case 0x20A:
                case 0x21A:
                if ((scat_regs[SCAT_EMS_CONTROL] & 0x41) == (0x40 | ((port & 0x10) >> 4)))
                {
                        scat_ems_reg_2xA = val;
                }
                break;
        }
}


uint8_t scat_read(uint16_t port, void *priv)
{
        uint8_t val = 0xff, index;
        switch (port)
        {
                case 0x23:
                switch (scat_index)
                {
                        case SCAT_MISCELLANEOUS_STATUS:
                        val = (scat_regs[scat_index] & 0xbf) | ((mem_a20_key & 2) << 5);
                        break;
                        case SCAT_DRAM_CONFIGURATION:
                        val = (scat_regs[scat_index] & 0x8f) | (cpu_waitstates == 1 ? 0 : 0x10);
                        break;
                        default:
                        val = scat_regs[scat_index];
                        break;
                }
                break;

                case 0x92:
                val = scat_port_92;
                break;

                case 0x208:
                case 0x218:
                if ((scat_regs[SCAT_EMS_CONTROL] & 0x41) == (0x40 | ((port & 0x10) >> 4)))
                {
                        index = scat_ems_reg_2xA & 0x1F;
                        val = scat_stat[index].regs_2x8;
                }
                break;
                case 0x209:
                case 0x219:
                if ((scat_regs[SCAT_EMS_CONTROL] & 0x41) == (0x40 | ((port & 0x10) >> 4)))
                {
                        index = scat_ems_reg_2xA & 0x1F;
                        val = scat_stat[index].regs_2x9;
                }
                break;
                case 0x20A:
                case 0x21A:
                if ((scat_regs[SCAT_EMS_CONTROL] & 0x41) == (0x40 | ((port & 0x10) >> 4)))
                {
                        val = scat_ems_reg_2xA;
                }
                break;
        }
        return val;
}


uint8_t mem_read_scatems(uint32_t addr, void *priv)
{
        uint8_t val = 0xff;
        scat_t *stat = (scat_t *)priv;

        addr = get_scat_addr(addr, stat);
        if (addr < (mem_size << 10))
                val = mem_read_ram(addr, priv);

        return val;
}


void mem_write_scatems(uint32_t addr, uint8_t val, void *priv)
{
        scat_t *stat = (scat_t *)priv;

        addr = get_scat_addr(addr, stat);
        if (addr < (mem_size << 10))
                mem_write_ram(addr, val, priv);
}


void scat_init(void)
{
        int i;

        io_sethandler(0x0022, 0x0002, scat_read, NULL, NULL, scat_write, NULL, NULL,  NULL);
        io_sethandler(0x0092, 0x0001, scat_read, NULL, NULL, scat_write, NULL, NULL,  NULL);

        for (i = 0; i < 256; i++)
        {
                scat_regs[i] = 0xff;
        }

        scat_regs[SCAT_DMA_WAIT_STATE_CONTROL] = 0;
        scat_regs[SCAT_VERSION] = 10;
        scat_regs[SCAT_CLOCK_CONTROL] = 2;
        scat_regs[SCAT_PERIPHERAL_CONTROL] = 0x80;
        scat_regs[SCAT_MISCELLANEOUS_STATUS] = 0x37;
        scat_regs[SCAT_POWER_MANAGEMENT] = 0;
        scat_regs[SCAT_ROM_ENABLE] = 0xC0;
        scat_regs[SCAT_RAM_WRITE_PROTECT] = 0;
        scat_regs[SCAT_SHADOW_RAM_ENABLE_1] = 0;
        scat_regs[SCAT_SHADOW_RAM_ENABLE_2] = 0;
        scat_regs[SCAT_SHADOW_RAM_ENABLE_3] = 0;
        scat_regs[SCAT_DRAM_CONFIGURATION] = cpu_waitstates == 1 ? 2 : 0x12;
        scat_regs[SCAT_EXTENDED_BOUNDARY] = 0;
        scat_regs[SCAT_EMS_CONTROL] = 0;
	scat_port_92 = 0;

        mem_mapping_set_addr(&ram_low_mapping, 0, 0x40000);

        for (i = 0; i < 24; i++)
        {
                mem_mapping_add(&scat_4000_9FFF_mapping[i], 0x40000 + (i << 14), 0x4000, mem_read_scatems, NULL, NULL, mem_write_scatems, NULL, NULL, mem_size > 256 + (i << 4) ? ram + 0x40000 + (i << 14) : NULL, MEM_MAPPING_INTERNAL, NULL);
                mem_mapping_enable(&scat_4000_9FFF_mapping[i]);
        }

        mem_mapping_add(&scat_A000_BFFF_mapping, 0xA0000, 0x20000, mem_read_scatems, NULL, NULL, mem_write_scatems, NULL, NULL, ram + 0xA0000, MEM_MAPPING_INTERNAL, NULL);
        mem_mapping_enable(&scat_A000_BFFF_mapping);

        for (i = 0; i < 32; i++)
        {
                scat_stat[i].regs_2x8 = 0xff;
                scat_stat[i].regs_2x9 = 0x03;
                mem_mapping_add(&scat_mapping[i], (i + (i >= 24 ? 28 : 16)) << 14, 0x04000, mem_read_scatems, NULL, NULL, mem_write_scatems, NULL, NULL, ram + ((i + (i >= 24 ? 28 : 16)) << 14), 0, &scat_stat[i]);
                mem_mapping_disable(&scat_mapping[i]);
        }

        for(i=4;i<10;i++) isram[i] = 0;

        for (i = 12; i < 16; i++)
        {
                mem_mapping_add(&scat_high_mapping[i], (i << 14) + 0xFC0000, 0x04000, mem_read_bios, mem_read_biosw, mem_read_biosl, mem_write_null, mem_write_nullw, mem_write_nulll, rom + (i << 14), 0, NULL);
        }

        for(i=0;i<6;i++)
                mem_mapping_add(&scat_shadowram_mapping[i], 0x100000 + (i << 16), 0x10000, mem_read_scatems, NULL, NULL, mem_write_scatems, NULL, NULL, mem_size >= 1024 ? ram + get_scat_addr(0x100000 + (i << 16), NULL) : NULL, MEM_MAPPING_INTERNAL, NULL);

        scat_set_xms_bound(0);
        scat_shadow_state_update();
}
