/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Memory handling and MMU.
 *
 * Authors: Sarah Walker, <tommowalker@tommowalker.co.uk>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2016-2020 Miran Grca.
 *          Copyright 2017-2020 Fred N. van Kempen.
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/version.h>
#include "cpu.h"
#include "x86_ops.h"
#include "x86.h"
#include "x86seg_common.h"
#include <86box/machine.h>
#include <86box/m_xt_xi8088.h>
#include <86box/config.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/plat.h>
#include <86box/rom.h>
#include <86box/gdbstub.h>

/* As below, 1 = exec, 4 = read. */
int    read_type = 4;

/* Set trap for data address breakpoints - 1 = exec, 2 = write, 4 = read. */
void
mem_debug_check_addr(uint32_t addr, int flags)
{
    uint32_t bp_addr;
    uint32_t bp_mask;
    uint32_t len_type_pair;
    int bp_enabled;
    uint8_t match_flags[4] = { 0, 2, 0, 6 };

    if (cpu_state.abrt || ((flags == 1) && (cpu_state.eflags & RF_FLAG)))
        return;

    if (dr[7] & 0x000000ff)  for (uint8_t i = 0; i < 4; i++) {
        bp_addr = dr[i];
        bp_enabled = (dr[7] >> (i << 1)) & 0x03;
        len_type_pair = (dr[7] >> (16 + (i << 2))) & 0x0f;
        bp_mask = ~((len_type_pair >> 2) & 0x03);

        if ((flags & match_flags[len_type_pair & 0x03]) && ((bp_addr & bp_mask) == (addr & bp_mask))) {
            /*
               From the Intel i386 documemntation:

               (Note that the processor sets Bn regardless of whether Gn or
               Ln is set. If more than one breakpoint condition occurs at one time and if
               the breakpoint trap occurs due to an enabled condition other than n, Bn may
               be set, even though neither Gn nor Ln is set.)
             */
            dr[6] |= (1 << i);
            if (bp_enabled)
                trap |= (read_type == 1) ? 8 : 4;
        }
    }
}

uint8_t
mem_readb_map(uint32_t addr)
{
    mem_mapping_t *map = read_mapping[addr >> MEM_GRANULARITY_BITS];
    uint8_t        ret = 0xff;

    mem_logical_addr = 0xffffffff;

    if (map && map->read_b)
        ret = map->read_b(addr, map->priv);

    return ret;
}

uint16_t
mem_readw_map(uint32_t addr)
{
    mem_mapping_t  *map = read_mapping[addr >> MEM_GRANULARITY_BITS];
    uint16_t        ret;

    mem_logical_addr = 0xffffffff;

    if (((addr & MEM_GRANULARITY_MASK) <= MEM_GRANULARITY_HBOUND) && (map && map->read_w))
        ret = map->read_w(addr, map->priv);
    else {
        ret = mem_readb_map(addr);
        ret |= ((uint16_t) mem_readb_map(addr + 1)) << 8;
    }

    return ret;
}

uint32_t
mem_readl_map(uint32_t addr)
{
    mem_mapping_t  *map = read_mapping[addr >> MEM_GRANULARITY_BITS];
    uint32_t        ret;

    mem_logical_addr = 0xffffffff;

    if (!cpu_16bitbus && ((addr & MEM_GRANULARITY_MASK) <= MEM_GRANULARITY_QBOUND) && (map && map->read_l))
        ret = map->read_l(addr, map->priv);
    else {
        ret = mem_readw_map(addr);
        ret |= ((uint32_t) mem_readw_map(addr + 2)) << 16;
    }

    return ret;
}

void
mem_writeb_map(uint32_t addr, uint8_t val)
{
    mem_mapping_t *map = read_mapping[addr >> MEM_GRANULARITY_BITS];

    mem_logical_addr = 0xffffffff;

    if (map && map->write_b)
        map->write_b(addr, val, map->priv);
}

void
mem_writew_map(uint32_t addr, uint16_t val)
{
    mem_mapping_t  *map = read_mapping[addr >> MEM_GRANULARITY_BITS];

    mem_logical_addr = 0xffffffff;

    if (((addr & MEM_GRANULARITY_MASK) <= MEM_GRANULARITY_HBOUND) && (map && map->write_w))
        map->write_w(addr, val, map->priv);
    else {
        mem_writeb_map(addr, val & 0xff);
        mem_writeb_map(addr + 1, val >> 8);
    }
}

void
mem_writel_map(uint32_t addr, uint32_t val)
{
    mem_mapping_t  *map = read_mapping[addr >> MEM_GRANULARITY_BITS];

    mem_logical_addr = 0xffffffff;

    if (!cpu_16bitbus && ((addr & MEM_GRANULARITY_MASK) <= MEM_GRANULARITY_QBOUND) && (map && map->write_l))
        map->write_l(addr, val, map->priv);
    else {
        mem_writew_map(addr, val & 0xffff);
        mem_writew_map(addr + 2, val >> 16);
    }
}

#define mmutranslate_read_2386(addr) mmutranslatereal_2386(addr,0)
#define mmutranslate_write_2386(addr) mmutranslatereal_2386(addr,1)

uint64_t
mmutranslatereal_2386(uint32_t addr, int rw)
{
    uint32_t temp;
    uint32_t temp2;
    uint32_t temp3;
    uint32_t addr2;

    if (cpu_state.abrt)
        return 0xffffffffffffffffULL;

    addr2 = ((cr3 & ~0xfff) + ((addr >> 20) & 0xffc));
    temp = temp2 = mem_readl_map(addr2);
    if (!(temp & 1)) {
        cr2 = addr;
        temp &= 1;
        if (CPL == 3)
            temp |= 4;
        if (rw)
            temp |= 2;
        cpu_state.abrt = ABRT_PF;
        abrt_error     = temp;
        return 0xffffffffffffffffULL;
    }

    if ((temp & 0x80) && (cr4 & CR4_PSE)) {
        /*4MB page*/
        if (((CPL == 3) && !(temp & 4) && !cpl_override) || (rw && !cpl_override && !(temp & 2) && (((CPL == 3) && !cpl_override) || ((is486 || isibm486) && (cr0 & WP_FLAG))))) {
            cr2 = addr;
            temp &= 1;
            if (CPL == 3)
                temp |= 4;
            if (rw)
                temp |= 2;
            cpu_state.abrt = ABRT_PF;
            abrt_error     = temp;

            return 0xffffffffffffffffULL;
        }

        mem_writel_map(addr2, mem_readl_map(addr2) | (rw ? 0x60 : 0x20));

        return (temp & ~0x3fffff) + (addr & 0x3fffff);
    }

    temp = mem_readl_map((temp & ~0xfff) + ((addr >> 10) & 0xffc));
    temp3 = temp & temp2;
    if (!(temp & 1) || ((CPL == 3) && !(temp3 & 4) && !cpl_override) || (rw && !cpl_override && !(temp3 & 2) && (((CPL == 3) && !cpl_override) || ((is486 || isibm486) && (cr0 & WP_FLAG))))) {
        cr2 = addr;
        temp &= 1;
        if (CPL == 3)
            temp |= 4;
        if (rw)
            temp |= 2;
        cpu_state.abrt = ABRT_PF;
        abrt_error     = temp;
        return 0xffffffffffffffffULL;
    }

    mem_writel_map(addr2, mem_readl_map(addr2) | 0x20);
    mem_writel_map((temp2 & ~0xfff) + ((addr >> 10) & 0xffc),
                   mem_readl_map((temp2 & ~0xfff) + ((addr >> 10) & 0xffc)) | (rw ? 0x60 : 0x20));

    return (uint64_t) ((temp & ~0xfff) + (addr & 0xfff));
}

uint64_t
mmutranslate_noabrt_2386(uint32_t addr, int rw)
{
    uint32_t temp;
    uint32_t temp2;
    uint32_t temp3;
    uint32_t addr2;

    if (cpu_state.abrt)
        return 0xffffffffffffffffULL;

    addr2 = ((cr3 & ~0xfff) + ((addr >> 20) & 0xffc));
    temp = temp2 = mem_readl_map(addr2);

    if (!(temp & 1))
        return 0xffffffffffffffffULL;

    if ((temp & 0x80) && (cr4 & CR4_PSE)) {
        /*4MB page*/
        if (((CPL == 3) && !(temp & 4) && !cpl_override) || (rw && !cpl_override && !(temp & 2) && ((CPL == 3) || (cr0 & WP_FLAG))))
            return 0xffffffffffffffffULL;

        return (temp & ~0x3fffff) + (addr & 0x3fffff);
    }

    temp  = mem_readl_map((temp & ~0xfff) + ((addr >> 10) & 0xffc));
    temp3 = temp & temp2;

    if (!(temp & 1) || ((CPL == 3) && !(temp3 & 4) && !cpl_override) || (rw && !cpl_override && !(temp3 & 2) && ((CPL == 3) || (cr0 & WP_FLAG))))
        return 0xffffffffffffffffULL;

    return (uint64_t) ((temp & ~0xfff) + (addr & 0xfff));
}

uint8_t
readmembl_2386(uint32_t addr)
{
    mem_mapping_t *map;
    uint64_t       a;
    uint32_t       temp_cr0 = cpu_old_paging ? (cr0 ^ 0x80000000) : cr0;

    GDBSTUB_MEM_ACCESS(addr, GDBSTUB_MEM_READ, 1);

    mem_debug_check_addr(addr, read_type);
    addr64           = (uint64_t) addr;
    mem_logical_addr = addr;

    high_page = 0;

    if (temp_cr0 >> 31) {
        a      = mmutranslate_read_2386(addr);
        addr64 = (uint32_t) a;

        if (a > 0xffffffffULL)
            return 0xff;
    }
    addr = (uint32_t) (addr64 & rammask);

    map = read_mapping[addr >> MEM_GRANULARITY_BITS];
    if (map && map->read_b)
        return map->read_b(addr, map->priv);

    return 0xff;
}

void
writemembl_2386(uint32_t addr, uint8_t val)
{
    mem_mapping_t *map;
    uint64_t       a;
    uint32_t       temp_cr0 = cpu_old_paging ? (cr0 ^ 0x80000000) : cr0;

    mem_debug_check_addr(addr, 2);
    GDBSTUB_MEM_ACCESS(addr, GDBSTUB_MEM_WRITE, 1);

    addr64           = (uint64_t) addr;
    mem_logical_addr = addr;

    high_page = 0;

    if (temp_cr0 >> 31) {
        a      = mmutranslate_write_2386(addr);
        addr64 = (uint32_t) a;

        if (a > 0xffffffffULL)
            return;
    }
    addr = (uint32_t) (addr64 & rammask);

    map = write_mapping[addr >> MEM_GRANULARITY_BITS];
    if (map && map->write_b)
        map->write_b(addr, val, map->priv);
}

/* Read a byte from memory without MMU translation - result of previous MMU translation passed as value. */
uint8_t
readmembl_no_mmut_2386(uint32_t addr, uint32_t a64)
{
    mem_mapping_t *map;
    uint32_t       temp_cr0 = cpu_old_paging ? (cr0 ^ 0x80000000) : cr0;

    GDBSTUB_MEM_ACCESS(addr, GDBSTUB_MEM_READ, 1);

    mem_logical_addr = addr;

    if (temp_cr0 >> 31) {
        if (cpu_state.abrt || high_page)
            return 0xff;

        addr = a64 & rammask;
    } else
        addr &= rammask;

    map = read_mapping[addr >> MEM_GRANULARITY_BITS];
    if (map && map->read_b)
        return map->read_b(addr, map->priv);

    return 0xff;
}

/* Write a byte to memory without MMU translation - result of previous MMU translation passed as value. */
void
writemembl_no_mmut_2386(uint32_t addr, uint32_t a64, uint8_t val)
{
    mem_mapping_t *map;
    uint32_t       temp_cr0 = cpu_old_paging ? (cr0 ^ 0x80000000) : cr0;

    GDBSTUB_MEM_ACCESS(addr, GDBSTUB_MEM_WRITE, 1);

    mem_logical_addr = addr;

    if (temp_cr0 >> 31) {
        if (cpu_state.abrt || high_page)
            return;

        addr = a64 & rammask;
    } else
        addr &= rammask;

    map = write_mapping[addr >> MEM_GRANULARITY_BITS];
    if (map && map->write_b)
        map->write_b(addr, val, map->priv);
}

uint16_t
readmemwl_2386(uint32_t addr)
{
    mem_mapping_t *map;
    uint64_t       a;
    uint32_t       temp_cr0 = cpu_old_paging ? (cr0 ^ 0x80000000) : cr0;

    addr64a[0] = addr;
    addr64a[1] = addr + 1;
    mem_debug_check_addr(addr, read_type);
    mem_debug_check_addr(addr + 1, read_type);
    GDBSTUB_MEM_ACCESS_FAST(addr64a, GDBSTUB_MEM_READ, 2);

    mem_logical_addr = addr;

    high_page = 0;

    if (addr & 1) {
        if (!cpu_cyrix_alignment || (addr & 7) == 7)
            cycles -= timing_misaligned;
        if ((addr & 0xfff) > 0xffe) {
            if (temp_cr0 >> 31) {
                for (uint8_t i = 0; i < 2; i++) {
                    a          = mmutranslate_read_2386(addr + i);
                    addr64a[i] = (uint32_t) a;

                    if (a > 0xffffffffULL)
                        return 0xffff;
                }
            }

            return readmembl_no_mmut_2386(addr, addr64a[0]) |
                   (((uint16_t) readmembl_no_mmut_2386(addr + 1, addr64a[1])) << 8);
        }
    }

    if (temp_cr0 >> 31) {
        a          = mmutranslate_read_2386(addr);
        addr64a[0] = (uint32_t) a;

        if (a > 0xffffffffULL)
            return 0xffff;
    } else
        addr64a[0] = (uint64_t) addr;

    addr = addr64a[0] & rammask;

    map = read_mapping[addr >> MEM_GRANULARITY_BITS];

    if (map && map->read_w)
        return map->read_w(addr, map->priv);

    if (map && map->read_b) {
        return map->read_b(addr, map->priv) | ((uint16_t) (map->read_b(addr + 1, map->priv)) << 8);
    }

    return 0xffff;
}

void
writememwl_2386(uint32_t addr, uint16_t val)
{
    mem_mapping_t *map;
    uint64_t       a;
    uint32_t       temp_cr0 = cpu_old_paging ? (cr0 ^ 0x80000000) : cr0;

    addr64a[0] = addr;
    addr64a[1] = addr + 1;
    mem_debug_check_addr(addr, 2);
    mem_debug_check_addr(addr + 1, 2);
    GDBSTUB_MEM_ACCESS_FAST(addr64a, GDBSTUB_MEM_WRITE, 2);

    mem_logical_addr = addr;

    high_page = 0;

    if (addr & 1) {
        if (!cpu_cyrix_alignment || (addr & 7) == 7)
            cycles -= timing_misaligned;
        if ((addr & 0xfff) > 0xffe) {
            if (temp_cr0 >> 31) {
                for (uint8_t i = 0; i < 2; i++) {
                    /* Do not translate a page that has a valid lookup, as that is by definition valid
                       and the whole purpose of the lookup is to avoid repeat identical translations. */
                    if (!page_lookup[(addr + i) >> 12] || !page_lookup[(addr + i) >> 12]->write_b) {
                        a          = mmutranslate_write_2386(addr + i);
                        addr64a[i] = (uint32_t) a;

                        if (a > 0xffffffffULL)
                            return;
                    }
                }
            }

            /* No need to waste precious CPU host cycles on mmutranslate's that were already done, just pass
               their result as a parameter to be used if needed. */
            writemembl_no_mmut_2386(addr, addr64a[0], val);
            writemembl_no_mmut_2386(addr + 1, addr64a[1], val >> 8);
            return;
        }
    }

    if (temp_cr0 >> 31) {
        a          = mmutranslate_write_2386(addr);
        addr64a[0] = (uint32_t) a;

        if (a > 0xffffffffULL)
            return;
    }

    addr = addr64a[0] & rammask;

    map = write_mapping[addr >> MEM_GRANULARITY_BITS];

    if (map && map->write_w) {
        map->write_w(addr, val, map->priv);
        return;
    }

    if (map && map->write_b) {
        map->write_b(addr, val, map->priv);
        map->write_b(addr + 1, val >> 8, map->priv);
        return;
    }
}

/* Read a word from memory without MMU translation - results of previous MMU translation passed as array. */
uint16_t
readmemwl_no_mmut_2386(uint32_t addr, uint32_t *a64)
{
    mem_mapping_t *map;
    uint32_t       temp_cr0 = cpu_old_paging ? (cr0 ^ 0x80000000) : cr0;

    GDBSTUB_MEM_ACCESS(addr, GDBSTUB_MEM_READ, 2);

    mem_logical_addr = addr;

    if (addr & 1) {
        if (!cpu_cyrix_alignment || (addr & 7) == 7)
            cycles -= timing_misaligned;
        if ((addr & 0xfff) > 0xffe) {
            if (temp_cr0 >> 31) {
                if (cpu_state.abrt || high_page)
                    return 0xffff;
            }

            return readmembl_no_mmut_2386(addr, a64[0]) |
                   (((uint16_t) readmembl_no_mmut_2386(addr + 1, a64[1])) << 8);
        }
    }

    if (temp_cr0 >> 31) {
        if (cpu_state.abrt || high_page)
            return 0xffff;

        addr = (uint32_t) (a64[0] & rammask);
    } else
        addr &= rammask;

    map = read_mapping[addr >> MEM_GRANULARITY_BITS];

    if (map && map->read_w)
        return map->read_w(addr, map->priv);

    if (map && map->read_b) {
        return map->read_b(addr, map->priv) | ((uint16_t) (map->read_b(addr + 1, map->priv)) << 8);
    }

    return 0xffff;
}

/* Write a word to memory without MMU translation - results of previous MMU translation passed as array. */
void
writememwl_no_mmut_2386(uint32_t addr, uint32_t *a64, uint16_t val)
{
    mem_mapping_t *map;
    uint32_t       temp_cr0 = cpu_old_paging ? (cr0 ^ 0x80000000) : cr0;

    GDBSTUB_MEM_ACCESS(addr, GDBSTUB_MEM_WRITE, 2);

    mem_logical_addr = addr;

    if (addr & 1) {
        if (!cpu_cyrix_alignment || (addr & 7) == 7)
            cycles -= timing_misaligned;
        if ((addr & 0xfff) > 0xffe) {
            if (temp_cr0 >> 31) {
                if (cpu_state.abrt || high_page)
                    return;
            }

            writemembl_no_mmut_2386(addr, a64[0], val);
            writemembl_no_mmut_2386(addr + 1, a64[1], val >> 8);
            return;
        }
    }

    if (temp_cr0 >> 31) {
        if (cpu_state.abrt || high_page)
            return;

        addr = (uint32_t) (a64[0] & rammask);
    } else
        addr &= rammask;

    map = write_mapping[addr >> MEM_GRANULARITY_BITS];

    if (map && map->write_w) {
        map->write_w(addr, val, map->priv);
        return;
    }

    if (map && map->write_b) {
        map->write_b(addr, val, map->priv);
        map->write_b(addr + 1, val >> 8, map->priv);
        return;
    }
}

uint32_t
readmemll_2386(uint32_t addr)
{
    mem_mapping_t *map;
    int            i;
    uint64_t       a = 0x0000000000000000ULL;
    uint32_t       temp_cr0 = cpu_old_paging ? (cr0 ^ 0x80000000) : cr0;

    for (i = 0; i < 4; i++) {
        addr64a[i] = (uint64_t) (addr + i);
        mem_debug_check_addr(addr + i, read_type);
    }
    GDBSTUB_MEM_ACCESS_FAST(addr64a, GDBSTUB_MEM_READ, 4);

    mem_logical_addr = addr;

    high_page = 0;

    if (cpu_16bitbus || (addr & 3)) {
        if ((addr & 3) && (!cpu_cyrix_alignment || (addr & 7) > 4))
            cycles -= timing_misaligned;
        if ((addr & 0xfff) > 0xffc) {
            if (temp_cr0 >> 31) {
                for (i = 0; i < 4; i++) {
                    if (i == 0) {
                        a          = mmutranslate_read_2386(addr + i);
                        addr64a[i] = (uint32_t) a;
                    } else if (!((addr + i) & 0xfff)) {
                        a          = mmutranslate_read_2386(addr + 3);
                        addr64a[i] = (uint32_t) a;
                        if (!cpu_state.abrt) {
                            a          = (a & ~0xfffLL) | ((uint64_t) ((addr + i) & 0xfff));
                            addr64a[i] = (uint32_t) a;
                        }
                    } else {
                        a          = (a & ~0xfffLL) | ((uint64_t) ((addr + i) & 0xfff));
                        addr64a[i] = (uint32_t) a;
                    }

                    if (a > 0xffffffffULL)
                        return 0xffff;
                }
            }

            /* No need to waste precious CPU host cycles on mmutranslate's that were already done, just pass
               their result as a parameter to be used if needed. */
            return readmemwl_no_mmut_2386(addr, addr64a) |
                   (((uint32_t) readmemwl_no_mmut_2386(addr + 2, &(addr64a[2]))) << 16);
        }
    }

    if (temp_cr0 >> 31) {
        a          = mmutranslate_read_2386(addr);
        addr64a[0] = (uint32_t) a;

        if (a > 0xffffffffULL)
            return 0xffffffff;
    }

    addr = addr64a[0] & rammask;

    map = read_mapping[addr >> MEM_GRANULARITY_BITS];

    if (map && map->read_l)
        return map->read_l(addr, map->priv);

    if (map && map->read_w)
        return map->read_w(addr, map->priv) | ((uint32_t) (map->read_w(addr + 2, map->priv)) << 16);

    if (map && map->read_b)
        return map->read_b(addr, map->priv) | ((uint32_t) (map->read_b(addr + 1, map->priv)) << 8) | ((uint32_t) (map->read_b(addr + 2, map->priv)) << 16) | ((uint32_t) (map->read_b(addr + 3, map->priv)) << 24);

    return 0xffffffff;
}

void
writememll_2386(uint32_t addr, uint32_t val)
{
    mem_mapping_t *map;
    int            i;
    uint64_t       a = 0x0000000000000000ULL;
    uint32_t       temp_cr0 = cpu_old_paging ? (cr0 ^ 0x80000000) : cr0;

    for (i = 0; i < 4; i++) {
        addr64a[i] = (uint64_t) (addr + i);
        mem_debug_check_addr(addr + i, 2);
    }
    GDBSTUB_MEM_ACCESS_FAST(addr64a, GDBSTUB_MEM_WRITE, 4);

    mem_logical_addr = addr;

    high_page = 0;

    if (cpu_16bitbus || (addr & 3)) {
        if ((addr & 3) && (!cpu_cyrix_alignment || (addr & 7) > 4))
            cycles -= timing_misaligned;
        if ((addr & 0xfff) > 0xffc) {
            if (temp_cr0 >> 31) {
                for (i = 0; i < 4; i++) {
                    /* Do not translate a page that has a valid lookup, as that is by definition valid
                       and the whole purpose of the lookup is to avoid repeat identical translations. */
                    if (!page_lookup[(addr + i) >> 12] || !page_lookup[(addr + i) >> 12]->write_b) {
                        if (i == 0) {
                            a          = mmutranslate_write_2386(addr + i);
                            addr64a[i] = (uint32_t) a;
                        } else if (!((addr + i) & 0xfff)) {
                            a          = mmutranslate_write_2386(addr + 3);
                            addr64a[i] = (uint32_t) a;
                            if (!cpu_state.abrt) {
                                a          = (a & ~0xfffLL) | ((uint64_t) ((addr + i) & 0xfff));
                                addr64a[i] = (uint32_t) a;
                            }
                        } else {
                            a          = (a & ~0xfffLL) | ((uint64_t) ((addr + i) & 0xfff));
                            addr64a[i] = (uint32_t) a;
                        }

                        if (a > 0xffffffffULL)
                            return;
                    }
                }
            }

            /* No need to waste precious CPU host cycles on mmutranslate's that were already done, just pass
               their result as a parameter to be used if needed. */
            writememwl_no_mmut_2386(addr, &(addr64a[0]), val);
            writememwl_no_mmut_2386(addr + 2, &(addr64a[2]), val >> 16);
            return;
        }
    }

    if (temp_cr0 >> 31) {
        a          = mmutranslate_write_2386(addr);
        addr64a[0] = (uint32_t) a;

        if (a > 0xffffffffULL)
            return;
    }

    addr = addr64a[0] & rammask;

    map = write_mapping[addr >> MEM_GRANULARITY_BITS];

    if (map && map->write_l) {
        map->write_l(addr, val, map->priv);
        return;
    }
    if (map && map->write_w) {
        map->write_w(addr, val, map->priv);
        map->write_w(addr + 2, val >> 16, map->priv);
        return;
    }
    if (map && map->write_b) {
        map->write_b(addr, val, map->priv);
        map->write_b(addr + 1, val >> 8, map->priv);
        map->write_b(addr + 2, val >> 16, map->priv);
        map->write_b(addr + 3, val >> 24, map->priv);
        return;
    }
}

/* Read a long from memory without MMU translation - results of previous MMU translation passed as array. */
uint32_t
readmemll_no_mmut_2386(uint32_t addr, uint32_t *a64)
{
    mem_mapping_t *map;
    uint32_t       temp_cr0 = cpu_old_paging ? (cr0 ^ 0x80000000) : cr0;

    GDBSTUB_MEM_ACCESS(addr, GDBSTUB_MEM_READ, 4);

    mem_logical_addr = addr;

    if (cpu_16bitbus || (addr & 3)) {
        if ((addr & 3) && (!cpu_cyrix_alignment || (addr & 7) > 4))
            cycles -= timing_misaligned;
        if ((addr & 0xfff) > 0xffc) {
            if (temp_cr0 >> 31) {
                if (cpu_state.abrt || high_page)
                    return 0xffffffff;
            }

            return readmemwl_no_mmut_2386(addr, a64) |
                   ((uint32_t) (readmemwl_no_mmut_2386(addr + 2, &(a64[2]))) << 16);
        }
    }

    if (temp_cr0 >> 31) {
        if (cpu_state.abrt || high_page)
            return 0xffffffff;

        addr = (uint32_t) (a64[0] & rammask);
    } else
        addr &= rammask;

    map = read_mapping[addr >> MEM_GRANULARITY_BITS];

    if (map && map->read_l)
        return map->read_l(addr, map->priv);

    if (map && map->read_w)
        return map->read_w(addr, map->priv) | ((uint32_t) (map->read_w(addr + 2, map->priv)) << 16);

    if (map && map->read_b)
        return map->read_b(addr, map->priv) | ((uint32_t) (map->read_b(addr + 1, map->priv)) << 8) | ((uint32_t) (map->read_b(addr + 2, map->priv)) << 16) | ((uint32_t) (map->read_b(addr + 3, map->priv)) << 24);

    return 0xffffffff;
}

/* Write a long to memory without MMU translation - results of previous MMU translation passed as array. */
void
writememll_no_mmut_2386(uint32_t addr, uint32_t *a64, uint32_t val)
{
    mem_mapping_t *map;
    uint32_t       temp_cr0 = cpu_old_paging ? (cr0 ^ 0x80000000) : cr0;

    GDBSTUB_MEM_ACCESS(addr, GDBSTUB_MEM_WRITE, 4);

    mem_logical_addr = addr;

    if (cpu_16bitbus || (addr & 3)) {
        if ((addr & 3) && (!cpu_cyrix_alignment || (addr & 7) > 4))
            cycles -= timing_misaligned;
        if ((addr & 0xfff) > 0xffc) {
            if (temp_cr0 >> 31) {
                if (cpu_state.abrt || high_page)
                    return;
            }

            writememwl_no_mmut_2386(addr, &(a64[0]), val);
            writememwl_no_mmut_2386(addr + 2, &(a64[2]), val >> 16);
            return;
        }
    }

    if (temp_cr0 >> 31) {
        if (cpu_state.abrt || high_page)
            return;

        addr = (uint32_t) (a64[0] & rammask);
    } else
        addr &= rammask;

    map = write_mapping[addr >> MEM_GRANULARITY_BITS];

    if (map && map->write_l) {
        map->write_l(addr, val, map->priv);
        return;
    }
    if (map && map->write_w) {
        map->write_w(addr, val, map->priv);
        map->write_w(addr + 2, val >> 16, map->priv);
        return;
    }
    if (map && map->write_b) {
        map->write_b(addr, val, map->priv);
        map->write_b(addr + 1, val >> 8, map->priv);
        map->write_b(addr + 2, val >> 16, map->priv);
        map->write_b(addr + 3, val >> 24, map->priv);
        return;
    }
}

uint64_t
readmemql_2386(uint32_t addr)
{
    mem_mapping_t *map;
    int            i;
    uint64_t       a = 0x0000000000000000ULL;
    uint32_t       temp_cr0 = cpu_old_paging ? (cr0 ^ 0x80000000) : cr0;

    for (i = 0; i < 8; i++) {
        addr64a[i] = (uint64_t) (addr + i);
        mem_debug_check_addr(addr + i, read_type);
    }
    GDBSTUB_MEM_ACCESS_FAST(addr64a, GDBSTUB_MEM_READ, 8);

    mem_logical_addr = addr;

    high_page = 0;

    if (addr & 7) {
        cycles -= timing_misaligned;
        if ((addr & 0xfff) > 0xff8) {
            if (temp_cr0 >> 31) {
                for (i = 0; i < 8; i++) {
                    if (i == 0) {
                        a          = mmutranslate_read_2386(addr + i);
                        addr64a[i] = (uint32_t) a;
                    } else if (!((addr + i) & 0xfff)) {
                        a          = mmutranslate_read_2386(addr + 7);
                        addr64a[i] = (uint32_t) a;
                        if (!cpu_state.abrt) {
                            a          = (a & ~0xfffLL) | ((uint64_t) ((addr + i) & 0xfff));
                            addr64a[i] = (uint32_t) a;
                        }
                    } else {
                        a          = (a & ~0xfffLL) | ((uint64_t) ((addr + i) & 0xfff));
                        addr64a[i] = (uint32_t) a;
                    }

                    if (a > 0xffffffffULL)
                        return 0xffff;
                }
            }

            /* No need to waste precious CPU host cycles on mmutranslate's that were already done, just pass
               their result as a parameter to be used if needed. */
            return readmemll_no_mmut_2386(addr, addr64a) |
                   (((uint64_t) readmemll_no_mmut_2386(addr + 4, &(addr64a[4]))) << 32);
        }
    }

    if (temp_cr0 >> 31) {
        a          = mmutranslate_read_2386(addr);
        addr64a[0] = (uint32_t) a;

        if (a > 0xffffffffULL)
            return 0xffffffffffffffffULL;
    }

    addr = addr64a[0] & rammask;

    map = read_mapping[addr >> MEM_GRANULARITY_BITS];

    if (map && map->read_l)
        return map->read_l(addr, map->priv) |
               ((uint64_t) map->read_l(addr + 4, map->priv) << 32);

    if (map && map->read_w)
        return map->read_w(addr, map->priv) |
               ((uint64_t) map->read_w(addr + 2, map->priv) << 16) |
               ((uint64_t) map->read_w(addr + 4, map->priv) << 32) |
               ((uint64_t) map->read_w(addr + 6, map->priv) << 48);

    if (map && map->read_b)
        return map->read_b(addr, map->priv) |
               ((uint64_t) map->read_b(addr + 1, map->priv) << 8) |
               ((uint64_t) map->read_b(addr + 2, map->priv) << 16) |
               ((uint64_t) map->read_b(addr + 3, map->priv) << 24) |
               ((uint64_t) map->read_b(addr + 4, map->priv) << 32) |
               ((uint64_t) map->read_b(addr + 5, map->priv) << 40) |
               ((uint64_t) map->read_b(addr + 6, map->priv) << 48) |
               ((uint64_t) map->read_b(addr + 7, map->priv) << 56);

    return 0xffffffffffffffffULL;
}

void
writememql_2386(uint32_t addr, uint64_t val)
{
    mem_mapping_t *map;
    int            i;
    uint64_t       a = 0x0000000000000000ULL;
    uint32_t       temp_cr0 = cpu_old_paging ? (cr0 ^ 0x80000000) : cr0;

    for (i = 0; i < 8; i++) {
        addr64a[i] = (uint64_t) (addr + i);
        mem_debug_check_addr(addr + i, 2);
    }
    GDBSTUB_MEM_ACCESS_FAST(addr64a, GDBSTUB_MEM_WRITE, 8);

    mem_logical_addr = addr;

    high_page = 0;

    if (addr & 7) {
        cycles -= timing_misaligned;
        if ((addr & 0xfff) > 0xff8) {
            if (temp_cr0 >> 31) {
                for (i = 0; i < 8; i++) {
                    /* Do not translate a page that has a valid lookup, as that is by definition valid
                       and the whole purpose of the lookup is to avoid repeat identical translations. */
                    if (!page_lookup[(addr + i) >> 12] || !page_lookup[(addr + i) >> 12]->write_b) {
                        if (i == 0) {
                            a          = mmutranslate_write_2386(addr + i);
                            addr64a[i] = (uint32_t) a;
                        } else if (!((addr + i) & 0xfff)) {
                            a          = mmutranslate_write_2386(addr + 7);
                            addr64a[i] = (uint32_t) a;
                            if (!cpu_state.abrt) {
                                a          = (a & ~0xfffLL) | ((uint64_t) ((addr + i) & 0xfff));
                                addr64a[i] = (uint32_t) a;
                            }
                        } else {
                            a          = (a & ~0xfffLL) | ((uint64_t) ((addr + i) & 0xfff));
                            addr64a[i] = (uint32_t) a;
                        }

                        if (addr64a[i] > 0xffffffffULL)
                            return;
                    }
                }
            }

            /* No need to waste precious CPU host cycles on mmutranslate's that were already done, just pass
               their result as a parameter to be used if needed. */
            writememll_no_mmut_2386(addr, addr64a, val);
            writememll_no_mmut_2386(addr + 4, &(addr64a[4]), val >> 32);
            return;
        }
    }

    if (temp_cr0 >> 31) {
        addr64a[0] = mmutranslate_write_2386(addr);
        if (addr64a[0] > 0xffffffffULL)
            return;
    }

    addr = addr64a[0] & rammask;

    map = write_mapping[addr >> MEM_GRANULARITY_BITS];

    if (map && map->write_l) {
        map->write_l(addr, val, map->priv);
        map->write_l(addr + 4, val >> 32, map->priv);
        return;
    }
    if (map && map->write_w) {
        map->write_w(addr, val, map->priv);
        map->write_w(addr + 2, val >> 16, map->priv);
        map->write_w(addr + 4, val >> 32, map->priv);
        map->write_w(addr + 6, val >> 48, map->priv);
        return;
    }
    if (map && map->write_b) {
        map->write_b(addr, val, map->priv);
        map->write_b(addr + 1, val >> 8, map->priv);
        map->write_b(addr + 2, val >> 16, map->priv);
        map->write_b(addr + 3, val >> 24, map->priv);
        map->write_b(addr + 4, val >> 32, map->priv);
        map->write_b(addr + 5, val >> 40, map->priv);
        map->write_b(addr + 6, val >> 48, map->priv);
        map->write_b(addr + 7, val >> 56, map->priv);
        return;
    }
}

void
do_mmutranslate_2386(uint32_t addr, uint32_t *a64, int num, int write)
{
    int i;
    uint32_t last_addr = addr + (num - 1);
    uint64_t a = 0x0000000000000000ULL;
    uint32_t temp_cr0 = cpu_old_paging ? (cr0 ^ 0x80000000) : cr0;

    mem_debug_check_addr(addr, write ? 2 : read_type);

    for (i = 0; i < num; i++)
        a64[i] = (uint64_t) addr;

    if (!(temp_cr0 >> 31))
        return;

    for (i = 0; i < num; i++) {
        /* If we have encountered at least one page fault, mark all subsequent addresses as
           having page faulted, prevents false negatives in readmem*l_no_mmut. */
        if ((i > 0) && cpu_state.abrt && !high_page)
            a64[i] = a64[i - 1];
        /* If we are on the same page, there is no need to translate again, as we can just
           reuse the previous result. */
        else if (i == 0) {
            a = mmutranslatereal_2386(addr, write);
            a64[i] = (uint32_t) a;
        } else if (!(addr & 0xfff)) {
            a = mmutranslatereal_2386(last_addr, write);
            a64[i] = (uint32_t) a;

            if (!cpu_state.abrt) {
                a = (a & 0xfffffffffffff000ULL) | ((uint64_t) (addr & 0xfff));
                a64[i] = (uint32_t) a;
            }
        } else {
            a = (a & 0xfffffffffffff000ULL) | ((uint64_t) (addr & 0xfff));
            a64[i] = (uint32_t) a;
        }

        addr++;
    }
}
