/* Copyright holders: Greatpsycho
   see COPYING for more details
*/
/*This is the chipset used in the Award 286 clone model*/
#include "ibm.h"
#include "io.h"
#include "scat.h"
#include "mem.h"

static uint8_t scat_regs[256];
static int scat_index;
static uint8_t scat_port_92 = 0;
static uint8_t scat_ems_reg_2xA = 0;
static mem_mapping_t scat_mapping[32];
static mem_mapping_t scat_high_mapping[16];
static scat_t scat_stat[32];
static uint32_t scat_xms_bound;
static mem_mapping_t scat_shadowram_mapping;
static mem_mapping_t scat_512k_clip_mapping;

void scat_shadow_state_update()
{
        int i, val, val2;

        // TODO - Segment A000 to BFFF shadow ram enable features and ROM enable features should be implemented later.
        for (i = 8; i < 24; i++)
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
                        mem_set_mem_state((i + 40) << 14, 0x4000, val);
                }
        }

        flushmmucache();
}

void scat_set_xms_bound(uint8_t val)
{
        uint32_t max_xms_size = (mem_size >= 16384) ? 0xFC0000 : mem_size << 10;

        switch (val)
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

        if ((scat_regs[SCAT_DRAM_CONFIGURATION] & 0x0F) == 3)
        {
                if (val != 1)
                {
                        mem_mapping_enable(&scat_shadowram_mapping);
                        if (val == 0)
                                scat_xms_bound = 0x160000;
                }
                else
                {
                        mem_mapping_disable(&scat_shadowram_mapping);
                }
                pclog("Set XMS bound(%02X) = %06X(%dKbytes for EMS access)\n", val, scat_xms_bound, (0x160000 - scat_xms_bound) >> 10);
                if (scat_xms_bound > 0x100000)
                        mem_set_mem_state(0x100000, scat_xms_bound - 0x100000, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                if (scat_xms_bound < 0x160000)
                        mem_set_mem_state(scat_xms_bound, 0x160000 - scat_xms_bound, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
        }
        else
        {
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
        else if (p == NULL && mem_size < 2048 && ((scat_regs[SCAT_DRAM_CONFIGURATION] & 0x0F) > 7))
                addr &= 0x7FFFF;
        if ((scat_regs[SCAT_EXTENDED_BOUNDARY] & 0x40) == 0 && (scat_regs[SCAT_DRAM_CONFIGURATION] & 0x0F) == 3 && addr >= 0x100000)
                addr -= 0x60000;

        return addr;
}

void scat_write(uint16_t port, uint8_t val, void *priv)
{
        uint8_t scat_reg_valid = 0, scat_shadow_update = 0, index;
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
                        case SCAT_EMS_CONTROL:
                        scat_reg_valid = 1;
                        break;
                        case SCAT_POWER_MANAGEMENT:
                        val &= 0x40; // TODO - Only use AUX parity disable bit for this version. Other bits should be implemented later.
                        scat_reg_valid = 1;
                        break;
                        case SCAT_DRAM_CONFIGURATION:
                        if((scat_regs[SCAT_EXTENDED_BOUNDARY] & 0x40) == 0)
                        {
                                if((val & 0x0F) == 3)
                                {
                                        mem_mapping_enable(&scat_shadowram_mapping);
                                }
                                else
                                {
                                        mem_mapping_disable(&scat_shadowram_mapping);
                                }
                                if(mem_size < 2048)
                                {
                                        if ((val & 0x0F) > 7) mem_mapping_enable(&scat_512k_clip_mapping);
                                        else mem_mapping_disable(&scat_512k_clip_mapping);
                                }
                        }
                        flushmmucache();
                        scat_reg_valid = 1;
                        break;
                        case SCAT_EXTENDED_BOUNDARY:
                        scat_set_xms_bound(val & 0x0f);
                        mem_set_mem_state(0x40000, 0x60000, (val & 0x20) ? MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL : MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                        flushmmucache();
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
                if (scat_shadow_update)
                        scat_shadow_state_update();
                pclog("Write SCAT Register %02X to %02X at %04X:%04X\n", scat_index, val, CS, cpu_state.pc);
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
                }
                scat_port_92 = val;
                break;

                case 0x208:
                if ((scat_regs[SCAT_EMS_CONTROL] & 0x41) == 0x40)
                {
                        pclog("Write SCAT EMS Control Port %04X to %02X at %04X:%04X\n", port, val, CS, cpu_state.pc);
                        index = scat_ems_reg_2xA & 0x1F;
                        scat_stat[index].regs_2x8 = val;
                }
                break;
                case 0x209:
                if ((scat_regs[SCAT_EMS_CONTROL] & 0x41) == 0x40)
                {
                        pclog("Write SCAT EMS Control Port %04X to %02X at %04X:%04X\n", port, val, CS, cpu_state.pc);
                        index = scat_ems_reg_2xA & 0x1F;
                        scat_stat[index].regs_2x9 = val;
                        base_addr = (index + 16) << 14;
                        if(index >= 24)
                                base_addr += 0x30000;

                        if (val & 0x80)
                        {
                                virt_addr = (((scat_stat[index].regs_2x9 & 3) << 8) | scat_stat[index].regs_2x8) << 14;
                                mem_mapping_enable(&scat_mapping[index]);
                                mem_mapping_set_exec(&scat_mapping[index], ram + get_scat_addr(virt_addr, &scat_stat[index]));
                                pclog("Map page %d(address %05X) to address %06X\n", scat_ems_reg_2xA & 0x1f, base_addr, virt_addr);
                        }
                        else
                        {
                                mem_mapping_disable(&scat_mapping[index]);
                                pclog("Unmap page %d(address %06X)\n", scat_ems_reg_2xA & 0x1f, base_addr);
                        }

                        if (scat_ems_reg_2xA & 0x80)
                        {
                                scat_ems_reg_2xA = (scat_ems_reg_2xA & 0xe0) | ((scat_ems_reg_2xA + 1) & 0x1f);
                        }
                }
                break;
                case 0x20A:
                if ((scat_regs[SCAT_EMS_CONTROL] & 0x41) == 0x40)
                {
                        pclog("Write SCAT EMS Control Port %04X to %02X at %04X:%04X\n", port, val, CS, cpu_state.pc);
                        scat_ems_reg_2xA = val;
                }
                break;

                case 0x218:
                if ((scat_regs[SCAT_EMS_CONTROL] & 0x41) == 0x41)
                {
                        pclog("Write SCAT EMS Control Port %04X to %02X at %04X:%04X\n", port, val, CS, cpu_state.pc);
                        index = scat_ems_reg_2xA & 0x1F;
                        scat_stat[index].regs_2x8 = val;
                }
                break;
                case 0x219:
                if ((scat_regs[SCAT_EMS_CONTROL] & 0x41) == 0x41)
                {
                        pclog("Write SCAT EMS Control Port %04X to %02X at %04X:%04X\n", port, val, CS, cpu_state.pc);
                        index = scat_ems_reg_2xA & 0x1F;
                        scat_stat[index].regs_2x9 = val;
                        base_addr = (index + 16) << 14;
                        if (index >= 24)
                                base_addr += 0x30000;

                        if (val & 0x80)
                        {
                                virt_addr = (((scat_stat[index].regs_2x9 & 3) << 8) | scat_stat[index].regs_2x8) << 14;
                                mem_mapping_enable(&scat_mapping[index]);
                                mem_mapping_set_exec(&scat_mapping[index], ram + get_scat_addr(virt_addr, &scat_stat[index]));
                                pclog("Map page %d(address %05X) to address %06X\n", scat_ems_reg_2xA & 0x1f, base_addr, virt_addr);
                        }
                        else
                        {
                                mem_mapping_disable(&scat_mapping[index]);
                                pclog("Unmap page %d(address %05X)\n", scat_ems_reg_2xA & 0x1f, base_addr);
                        }

                        if (scat_ems_reg_2xA & 0x80)
                        {
                                scat_ems_reg_2xA = (scat_ems_reg_2xA & 0xe0) | ((scat_ems_reg_2xA + 1) & 0x1f);
                        }
                }
                break;
                case 0x21A:
                if ((scat_regs[SCAT_EMS_CONTROL] & 0x41) == 0x41)
                {
                        pclog("Write SCAT EMS Control Port %04X to %02X at %04X:%04X\n", port, val, CS, cpu_state.pc);
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
                        val = (scat_regs[scat_index] & 0xbf) | ((scat_port_92 & 2) << 5);
                        break;
                        default:
                        val = scat_regs[scat_index];
                        break;
                }
                pclog("Read SCAT Register %02X at %04X:%04X\n", scat_index, CS, cpu_state.pc);
                break;

                case 0x92:
                val = scat_port_92;
                break;

                case 0x208:
                if ((scat_regs[SCAT_EMS_CONTROL] & 0x41) == 0x40)
                {
                        pclog("Read SCAT EMS Control Port %04X at %04X:%04X\n", port, CS, cpu_state.pc);
                        index = scat_ems_reg_2xA & 0x1F;
                        val = scat_stat[index].regs_2x8;
                }
                break;
                case 0x209:
                if ((scat_regs[SCAT_EMS_CONTROL] & 0x41) == 0x40)
                {
                        pclog("Read SCAT EMS Control Port %04X at %04X:%04X\n", port, CS, cpu_state.pc);
                        index = scat_ems_reg_2xA & 0x1F;
                        val = scat_stat[index].regs_2x9;
                }
                break;
                case 0x20A:
                if ((scat_regs[SCAT_EMS_CONTROL] & 0x41) == 0x40)
                {
                        pclog("Read SCAT EMS Control Port %04X at %04X:%04X\n", port, CS, cpu_state.pc);
                        val = scat_ems_reg_2xA;
                }
                break;

                case 0x218:
                if ((scat_regs[SCAT_EMS_CONTROL] & 0x41) == 0x41)
                {
                        pclog("Read SCAT EMS Control Port %04X at %04X:%04X\n", port, CS, cpu_state.pc);
                        index = scat_ems_reg_2xA & 0x1F;
                        val = scat_stat[index].regs_2x8;
                }
                break;
                case 0x219:
                if ((scat_regs[SCAT_EMS_CONTROL] & 0x41) == 0x41)
                {
                        pclog("Read SCAT EMS Control Port %04X at %04X:%04X\n", port, CS, cpu_state.pc);
                        index = scat_ems_reg_2xA & 0x1F;
                        val = scat_stat[index].regs_2x9;
                }
                break;
                case 0x21A:
                if ((scat_regs[SCAT_EMS_CONTROL] & 0x41) == 0x41)
                {
                        pclog("Read SCAT EMS Control Port %04X at %04X:%04X\n", port, CS, cpu_state.pc);
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
uint16_t mem_read_scatemsw(uint32_t addr, void *priv)
{
        uint16_t val = 0xffff;
        scat_t *stat = (scat_t *)priv;

        addr = get_scat_addr(addr, stat);
        if (addr < (mem_size << 10))
                val = mem_read_ramw(addr, priv);

        return val;
}
uint32_t mem_read_scatemsl(uint32_t addr, void *priv)
{
        uint32_t val = 0xffffffff;
        scat_t *stat = (scat_t *)priv;

        addr = get_scat_addr(addr, stat);
        if (addr < (mem_size << 10))
                val = mem_read_raml(addr, priv);

        return val;
}

void mem_write_scatems(uint32_t addr, uint8_t val, void *priv)
{
        scat_t *stat = (scat_t *)priv;

        addr = get_scat_addr(addr, stat);
        if (addr < (mem_size << 10))
                mem_write_ram(addr, val, priv);
}
void mem_write_scatemsw(uint32_t addr, uint16_t val, void *priv)
{
        scat_t *stat = (scat_t *)priv;

        addr = get_scat_addr(addr, stat);
        if (addr < (mem_size << 10))
                mem_write_ramw(addr, val, priv);
}
void mem_write_scatemsl(uint32_t addr, uint32_t val, void *priv)
{
        scat_t *stat = (scat_t *)priv;

        addr = get_scat_addr(addr, stat);
        if (addr < (mem_size << 10))
                mem_write_raml(addr, val, priv);
}

void scat_init()
{
        int i;

        io_sethandler(0x0022, 0x0002, scat_read, NULL, NULL, scat_write, NULL, NULL,  NULL);
        io_sethandler(0x0092, 0x0001, scat_read, NULL, NULL, scat_write, NULL, NULL,  NULL);
        io_sethandler(0x0208, 0x0003, scat_read, NULL, NULL, scat_write, NULL, NULL,  NULL);
        io_sethandler(0x0218, 0x0003, scat_read, NULL, NULL, scat_write, NULL, NULL,  NULL);

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
        scat_regs[SCAT_DRAM_CONFIGURATION] = 2;
        scat_regs[SCAT_EXTENDED_BOUNDARY] = 0;
        scat_regs[SCAT_EMS_CONTROL] = 0;

        for (i = 0; i < 32; i++)
        {
                scat_stat[i].regs_2x8 = 0xff;
                scat_stat[i].regs_2x9 = 0x03;
                mem_mapping_add(&scat_mapping[i], (i + (i >= 24 ? 28 : 16)) << 14, 0x04000, mem_read_scatems, mem_read_scatemsw, mem_read_scatemsl, mem_write_scatems, mem_write_scatemsw, mem_write_scatemsl, ram + ((i + (i >= 24 ? 28 : 16)) << 14), 0, &scat_stat[i]);
                mem_mapping_disable(&scat_mapping[i]);
        }

        // TODO - Only normal CPU accessing address FF0000 to FFFFFF mapped to ROM. Normal CPU accessing address FC0000 to FEFFFF map to ROM should be implemented later.
        for (i = 12; i < 16; i++)
        {
                mem_mapping_add(&scat_high_mapping[i], (i << 14) + 0xFC0000, 0x04000, mem_read_bios, mem_read_biosw, mem_read_biosl, mem_write_null, mem_write_nullw, mem_write_nulll, rom + (i << 14), 0, NULL);
        }

        if (mem_size == 1024)
        {
                mem_mapping_add(&scat_shadowram_mapping, 0x100000, 0x60000, mem_read_scatems, mem_read_scatemsw, mem_read_scatemsl, mem_write_scatems, mem_write_scatemsw, mem_write_scatemsl, ram + 0xA0000, MEM_MAPPING_INTERNAL, NULL);
        }

        // Need to RAM 512kb clipping emulation if only 256KB or 64KB modules installed in memory bank.
        // TODO - 512KB clipping should be applied all RAM refer.
        mem_mapping_add(&scat_512k_clip_mapping, 0x80000, 0x20000, mem_read_scatems, mem_read_scatemsw, mem_read_scatemsl, mem_write_scatems, mem_write_scatemsw, mem_write_scatemsl, ram, MEM_MAPPING_INTERNAL, NULL);
        mem_mapping_disable(&scat_512k_clip_mapping);
        // ---

        scat_set_xms_bound(0);
        scat_shadow_state_update();
}
