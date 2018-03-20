/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of Chips&Technology's SCAT (82C235) chipset.
 *
 *		Re-worked version based on the 82C235 datasheet and errata.
 *
 * Version:	@(#)m_at_scat.c	1.0.12	2018/03/20
 *
 * Authors:	Original by GreatPsycho for PCem.
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../device.h"
#include "../cpu/cpu.h"
#include "../cpu/x86.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"
#include "../keyboard.h"
#include "../io.h"
#include "../mem.h"
#include "../nmi.h"
#include "machine.h"


#define SCAT_DMA_WAIT_STATE_CONTROL 0x01
#define SCAT_VERSION                0x40
#define SCAT_CLOCK_CONTROL          0x41
#define SCAT_PERIPHERAL_CONTROL     0x44
#define SCAT_MISCELLANEOUS_STATUS   0x45
#define SCAT_POWER_MANAGEMENT       0x46
#define SCAT_ROM_ENABLE             0x48
#define SCAT_RAM_WRITE_PROTECT      0x49
#define SCAT_SHADOW_RAM_ENABLE_1    0x4A
#define SCAT_SHADOW_RAM_ENABLE_2    0x4B
#define SCAT_SHADOW_RAM_ENABLE_3    0x4C
#define SCAT_DRAM_CONFIGURATION     0x4D
#define SCAT_EXTENDED_BOUNDARY      0x4E
#define SCAT_EMS_CONTROL            0x4F

#define SCATSX_LAPTOP_FEATURES          0x60
#define SCATSX_FAST_VIDEO_CONTROL       0x61
#define SCATSX_FAST_VIDEORAM_ENABLE     0x62
#define SCATSX_HIGH_PERFORMANCE_REFRESH 0x63
#define SCATSX_CAS_TIMING_FOR_DMA       0x64

typedef struct scat_t
{
        uint8_t regs_2x8;
        uint8_t regs_2x9;
} scat_t;


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

// TODO - 82C836 chipset's memory address mapping isn't fully implemented yet, so memory configuration is hardcoded now.
static int scatsx_mem_conf_val[33] = { 0x00, 0x01, 0x03, 0x04, 0x05, 0x08, 0x06, 0x06,
                                       0x0c, 0x09, 0x07, 0x07, 0x0d, 0x0a, 0x0f, 0x0f,
                                       0x0e, 0x0e, 0x10, 0x10, 0x13, 0x13, 0x11, 0x11,
                                       0x14, 0x14, 0x12, 0x12, 0x15, 0x15, 0x15, 0x15,
                                       0x16 };

uint8_t scat_read(uint16_t port, void *priv);
void scat_write(uint16_t port, uint8_t val, void *priv);

void scat_shadow_state_update()
{
        int i, val;

        // TODO - ROMCS enable features should be implemented later.
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
        uint32_t max_xms_size = (mem_size >= 16128) ? (((scat_regs[SCAT_VERSION] & 0xF0) != 0 && ((val & 0x10) != 0)) ? 0xFE0000 : 0xFC0000) : mem_size << 10;
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
                scat_xms_bound = (scat_regs[SCAT_VERSION] & 0xF0) == 0 ? 0x600000 : 0x500000;
                break;
                case 8:
                scat_xms_bound = (scat_regs[SCAT_VERSION] & 0xF0) == 0 ? 0x800000 : 0x700000;
                break;
                case 9:
                scat_xms_bound = (scat_regs[SCAT_VERSION] & 0xF0) == 0 ? 0xA00000 : 0x800000;
                break;
                case 10:
                scat_xms_bound = (scat_regs[SCAT_VERSION] & 0xF0) == 0 ? 0xC00000 : 0x900000;
                break;
                case 11:
                scat_xms_bound = (scat_regs[SCAT_VERSION] & 0xF0) == 0 ? 0xE00000 : 0xA00000;
                break;
                case 12:
                scat_xms_bound = (scat_regs[SCAT_VERSION] & 0xF0) == 0 ? max_xms_size : 0xB00000;
                break;
                case 13:
                scat_xms_bound = (scat_regs[SCAT_VERSION] & 0xF0) == 0 ? max_xms_size : 0xC00000;
                break;
                case 14:
                scat_xms_bound = (scat_regs[SCAT_VERSION] & 0xF0) == 0 ? max_xms_size : 0xD00000;
                break;
                case 15:
                scat_xms_bound = (scat_regs[SCAT_VERSION] & 0xF0) == 0 ? max_xms_size : 0xF00000;
                break;
                default:
                scat_xms_bound = max_xms_size;
                break;
        }

        if ((((scat_regs[SCAT_VERSION] & 0xF0) == 0) && (val & 0x40) == 0 && (scat_regs[SCAT_DRAM_CONFIGURATION] & 0x0F) == 3) || (((scat_regs[SCAT_VERSION] & 0xF0) != 0) && (scat_regs[SCAT_DRAM_CONFIGURATION] & 0x1F) == 3))
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
                addr = (addr & 0x3fff) | (((p->regs_2x9 & 3) << 8) | p->regs_2x8) << 14;

        if ((scat_regs[SCAT_VERSION] & 0xF0) == 0)
        {
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
        }
        else
        {
                if ((scat_regs[SCAT_DRAM_CONFIGURATION] & 0x1F) == 3 && (addr & ~0x600000) >= 0x100000 && (addr & ~0x600000) < 0x160000)
                        addr ^= 0x1F0000;
        }
        return addr;
}

void scat_memmap_state_update()
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

        for(i=((scat_regs[SCAT_VERSION] & 0xF0) == 0) ? 0 : 24; i<32; i++)
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
        flushmmucache();
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
                        case SCAT_DMA_WAIT_STATE_CONTROL:
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
                        // TODO - Only use AUX parity disable bit for this version. Other bits should be implemented later.
                        val &= (scat_regs[SCAT_VERSION] & 0xF0) == 0 ? 0x40 : 0x60;
                        scat_reg_valid = 1;
                        break;
                        case SCAT_DRAM_CONFIGURATION:
                        if((((scat_regs[SCAT_VERSION] & 0xF0) == 0) && (scat_regs[SCAT_EXTENDED_BOUNDARY] & 0x40) == 0) || ((scat_regs[SCAT_VERSION] & 0xF0) != 0))
                        {
                                if((((scat_regs[SCAT_VERSION] & 0xF0) == 0) && (val & 0x0F) == 3) || (((scat_regs[SCAT_VERSION] & 0xF0) != 0) && (val & 0x1F) == 3))
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

                        if((scat_regs[SCAT_VERSION] & 0xF0) == 0)
                        {
                                cpu_waitstates = (val & 0x70) == 0 ? 1 : 2;
                                cpu_update_waitstates();
                        }
                        else
                        {
                                if(mem_size != 1024 || ((val & 0x1F) != 2 && (val & 0x1F) != 3))
                                        val = (val & 0xe0) | scatsx_mem_conf_val[mem_size >> 9];
                        }

                        scat_reg_valid = 1;
                        break;
                        case SCAT_EXTENDED_BOUNDARY:
                        scat_set_xms_bound(val & ((scat_regs[SCAT_VERSION] & 0xF0) == 0 ? 0x4f : 0x1f));
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
                        case SCATSX_LAPTOP_FEATURES:
                        if((scat_regs[SCAT_VERSION] & 0xF0) != 0)
                        {
                                val = (val & ~8) | (scat_regs[SCATSX_LAPTOP_FEATURES] & 8);
                                scat_reg_valid = 1;
                        }
                        break;
                        case SCATSX_FAST_VIDEO_CONTROL:
                        case SCATSX_FAST_VIDEORAM_ENABLE:
                        case SCATSX_HIGH_PERFORMANCE_REFRESH:
                        case SCATSX_CAS_TIMING_FOR_DMA:
                        if((scat_regs[SCAT_VERSION] & 0xF0) != 0) scat_reg_valid = 1;
                        break;
                        default:
                        break;
                }
                if (scat_reg_valid)
                        scat_regs[scat_index] = val;
                else pclog("Attemped to write unimplemented SCAT register %02X at %04X:%04X\n", scat_index, val, CS, cpu_state.pc);
                if (scat_shadow_update)
                        scat_shadow_state_update();
                if (scat_map_update)
                        scat_memmap_state_update();
//                pclog("Write SCAT Register %02X to %02X at %04X:%04X\n", scat_index, val, CS, cpu_state.pc);
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
//                        pclog("Write SCAT EMS Control Port %04X to %02X at %04X:%04X\n", port, val, CS, cpu_state.pc);
                        if((scat_regs[SCAT_VERSION] & 0xF0) == 0) index = scat_ems_reg_2xA & 0x1F;
                        else index = ((scat_ems_reg_2xA & 0x40) >> 4) + (scat_ems_reg_2xA & 0x3) + 24;
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
//                        pclog("Write SCAT EMS Control Port %04X to %02X at %04X:%04X\n", port, val, CS, cpu_state.pc);
                        if((scat_regs[SCAT_VERSION] & 0xF0) == 0) index = scat_ems_reg_2xA & 0x1F;
                        else index = ((scat_ems_reg_2xA & 0x40) >> 4) + (scat_ems_reg_2xA & 0x3) + 24;
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
//                                        pclog("Map page %d(address %05X) to address %06X\n", scat_ems_reg_2xA & 0x1f, base_addr, virt_addr);
                                }
                                else
                                {
                                        mem_mapping_set_exec(&scat_mapping[index], ram + base_addr);
                                        mem_mapping_disable(&scat_mapping[index]);
                                        if(index < 24) mem_mapping_enable(&scat_4000_9FFF_mapping[index]);
//                                        pclog("Unmap page %d(address %06X)\n", scat_ems_reg_2xA & 0x1f, base_addr);
                                }
                                flushmmucache();
                        }

                        if (scat_ems_reg_2xA & 0x80)
                        {
                                scat_ems_reg_2xA = (scat_ems_reg_2xA & 0xe0) | ((scat_ems_reg_2xA + 1) & (((scat_regs[SCAT_VERSION] & 0xF0) == 0) ? 0x1f : 3));
                        }
                }
                break;
                case 0x20A:
                case 0x21A:
                if ((scat_regs[SCAT_EMS_CONTROL] & 0x41) == (0x40 | ((port & 0x10) >> 4)))
                {
//                        pclog("Write SCAT EMS Control Port %04X to %02X at %04X:%04X\n", port, val, CS, cpu_state.pc);
                        scat_ems_reg_2xA = ((scat_regs[SCAT_VERSION] & 0xF0) == 0) ? val : val & 0xc3;
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
                        val = (scat_regs[scat_index] & 0x3f) | (~nmi_mask & 0x80) | ((mem_a20_key & 2) << 5);
                        break;
                        case SCAT_DRAM_CONFIGURATION:
                        if ((scat_regs[SCAT_VERSION] & 0xF0) == 0) val = (scat_regs[scat_index] & 0x8f) | (cpu_waitstates == 1 ? 0 : 0x10);
                        else if(mem_size != 1024) val = (scat_regs[scat_index] & 0xe0) | scatsx_mem_conf_val[mem_size >> 9];
                        else val = scat_regs[scat_index];
                        break;
                        default:
                        val = scat_regs[scat_index];
                        break;
                }
//                pclog("Read SCAT Register %02X at %04X:%04X\n", scat_index, CS, cpu_state.pc);
                break;

                case 0x92:
                val = scat_port_92;
                break;

                case 0x208:
                case 0x218:
                if ((scat_regs[SCAT_EMS_CONTROL] & 0x41) == (0x40 | ((port & 0x10) >> 4)))
                {
//                        pclog("Read SCAT EMS Control Port %04X at %04X:%04X\n", port, CS, cpu_state.pc);
                        if((scat_regs[SCAT_VERSION] & 0xF0) == 0) index = scat_ems_reg_2xA & 0x1F;
                        else index = ((scat_ems_reg_2xA & 0x40) >> 4) + (scat_ems_reg_2xA & 0x3) + 24;
                        val = scat_stat[index].regs_2x8;
                }
                break;
                case 0x209:
                case 0x219:
                if ((scat_regs[SCAT_EMS_CONTROL] & 0x41) == (0x40 | ((port & 0x10) >> 4)))
                {
//                        pclog("Read SCAT EMS Control Port %04X at %04X:%04X\n", port, CS, cpu_state.pc);
                        if((scat_regs[SCAT_VERSION] & 0xF0) == 0) index = scat_ems_reg_2xA & 0x1F;
                        else index = ((scat_ems_reg_2xA & 0x40) >> 4) + (scat_ems_reg_2xA & 0x3) + 24;
                        val = scat_stat[index].regs_2x9;
                }
                break;
                case 0x20A:
                case 0x21A:
                if ((scat_regs[SCAT_EMS_CONTROL] & 0x41) == (0x40 | ((port & 0x10) >> 4)))
                {
//                        pclog("Read SCAT EMS Control Port %04X at %04X:%04X\n", port, CS, cpu_state.pc);
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


static void
scat_init(void)
{
	int i;

	io_sethandler(0x0022, 2, scat_read, NULL, NULL, scat_write, NULL, NULL,  NULL);

	port_92_reset();

	port_92_add();

        for (i = 0; i < 256; i++)
        {
                scat_regs[i] = 0xff;
        }

        scat_regs[SCAT_DMA_WAIT_STATE_CONTROL] = 0;
        scat_regs[SCAT_VERSION] = 4;
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

        /* TODO - Only normal CPU accessing address FF0000 to FFFFFF mapped to ROM.
		  Normal CPU accessing address FC0000 to FEFFFF map to ROM should be implemented later. */
        for (i = 12; i < 16; i++)
        {
                mem_mapping_add(&scat_high_mapping[i], (i << 14) + 0xFC0000, 0x04000, mem_read_bios, mem_read_biosw, mem_read_biosl, mem_write_null, mem_write_nullw, mem_write_nulll, rom + ((i << 14) & biosmask), 0, NULL);
        }

        for(i=0;i<6;i++)
                mem_mapping_add(&scat_shadowram_mapping[i], 0x100000 + (i << 16), 0x10000, mem_read_scatems, NULL, NULL, mem_write_scatems, NULL, NULL, mem_size >= 1024 ? ram + get_scat_addr(0x100000 + (i << 16), NULL) : NULL, MEM_MAPPING_INTERNAL, NULL);

        scat_set_xms_bound(0);
        scat_shadow_state_update();
}


static void
scatsx_init()
{
        int i;

        io_sethandler(0x0022, 0x0002, scat_read, NULL, NULL, scat_write, NULL, NULL,  NULL);

	port_92_reset();

	port_92_add();

        for (i = 0; i < 256; i++)
        {
                scat_regs[i] = 0xff;
        }

        scat_regs[SCAT_DMA_WAIT_STATE_CONTROL] = 0;
        scat_regs[SCAT_VERSION] = 0x13;
        scat_regs[SCAT_CLOCK_CONTROL] = 6;
        scat_regs[SCAT_PERIPHERAL_CONTROL] = 0;
        scat_regs[SCAT_MISCELLANEOUS_STATUS] = 0x37;
        scat_regs[SCAT_POWER_MANAGEMENT] = 0;
        scat_regs[SCAT_ROM_ENABLE] = 0xC0;
        scat_regs[SCAT_RAM_WRITE_PROTECT] = 0;
        scat_regs[SCAT_SHADOW_RAM_ENABLE_1] = 0;
        scat_regs[SCAT_SHADOW_RAM_ENABLE_2] = 0;
        scat_regs[SCAT_SHADOW_RAM_ENABLE_3] = 0;
        scat_regs[SCAT_DRAM_CONFIGURATION] = 1;
        scat_regs[SCAT_EXTENDED_BOUNDARY] = 0;
        scat_regs[SCAT_EMS_CONTROL] = 0;
        scat_regs[SCATSX_LAPTOP_FEATURES] = 0;
        scat_regs[SCATSX_FAST_VIDEO_CONTROL] = 0;
        scat_regs[SCATSX_FAST_VIDEORAM_ENABLE] = 0;
        scat_regs[SCATSX_HIGH_PERFORMANCE_REFRESH] = 8;
        scat_regs[SCATSX_CAS_TIMING_FOR_DMA] = 3;
        scat_port_92 = 0;

        mem_mapping_set_addr(&ram_low_mapping, 0, 0x80000);

        for (i = 16; i < 24; i++)
        {
                mem_mapping_add(&scat_4000_9FFF_mapping[i], 0x40000 + (i << 14), 0x4000, mem_read_scatems, NULL, NULL, mem_write_scatems, NULL, NULL, mem_size > 256 + (i << 4) ? ram + 0x40000 + (i << 14) : NULL, MEM_MAPPING_INTERNAL, NULL);
                mem_mapping_enable(&scat_4000_9FFF_mapping[i]);
        }

        mem_mapping_add(&scat_A000_BFFF_mapping, 0xA0000, 0x20000, mem_read_scatems, NULL, NULL, mem_write_scatems, NULL, NULL, ram + 0xA0000, MEM_MAPPING_INTERNAL, NULL);
        mem_mapping_enable(&scat_A000_BFFF_mapping);

        for (i = 24; i < 32; i++)
        {
                scat_stat[i].regs_2x8 = 0xff;
                scat_stat[i].regs_2x9 = 0x03;
                mem_mapping_add(&scat_mapping[i], (i + 28) << 14, 0x04000, mem_read_scatems, NULL, NULL, mem_write_scatems, NULL, NULL, ram + ((i + 28) << 14), 0, &scat_stat[i]);
                mem_mapping_disable(&scat_mapping[i]);
        }

        /* TODO - Only normal CPU accessing address FF0000 to FFFFFF mapped to ROM.
	   Normal CPU accessing address FC0000 to FEFFFF map to ROM should be implemented later. */
        for (i = 12; i < 16; i++)
        {
                mem_mapping_add(&scat_high_mapping[i], (i << 14) + 0xFC0000, 0x04000, mem_read_bios, mem_read_biosw, mem_read_biosl, mem_write_null, mem_write_nullw, mem_write_nulll, rom + ((i << 14) & biosmask), 0, NULL);
        }

        for(i=0;i<6;i++)
                mem_mapping_add(&scat_shadowram_mapping[i], 0x100000 + (i << 16), 0x10000, mem_read_scatems, NULL, NULL, mem_write_scatems, NULL, NULL, mem_size >= 1024 ? ram + get_scat_addr(0x100000 + (i << 16), NULL) : NULL, MEM_MAPPING_INTERNAL, NULL);

        scat_set_xms_bound(0);
        scat_shadow_state_update();
}


void
machine_at_scat_init(const machine_t *model)
{
    machine_at_init(model);
    device_add(&fdc_at_device);

    scat_init();
}


void
machine_at_scatsx_init(const machine_t *model)
{
    machine_at_common_init(model);

    device_add(&keyboard_at_ami_device);
    device_add(&fdc_at_device);

    scatsx_init();
}
