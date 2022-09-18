/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		SMRAM handling.
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016-2020 Miran Grca.
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
#include "cpu.h"
#include "x86_ops.h"
#include "x86.h"
#include <86box/config.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/smram.h>

static smram_t *base_smram, *last_smram;

static uint8_t use_separate_smram = 0;
static uint8_t smram[0x40000];

#ifdef ENABLE_SMRAM_LOG
int smram_do_log = ENABLE_SMRAM_LOG;

static void
smram_log(const char *fmt, ...)
{
    va_list ap;

    if (smram_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define smram_log(fmt, ...)
#endif

static uint8_t
smram_read(uint32_t addr, void *priv)
{
    smram_t *dev      = (smram_t *) priv;
    uint32_t new_addr = addr - dev->host_base + dev->ram_base;

    if (new_addr >= (1 << 30))
        return mem_read_ram_2gb(new_addr, priv);
    else if (!use_separate_smram || (new_addr >= 0xa0000))
        return mem_read_ram(new_addr, priv);
    else
        return dev->mapping.exec[addr - dev->host_base];
}

static uint16_t
smram_readw(uint32_t addr, void *priv)
{
    smram_t *dev      = (smram_t *) priv;
    uint32_t new_addr = addr - dev->host_base + dev->ram_base;

    if (new_addr >= (1 << 30))
        return mem_read_ram_2gbw(new_addr, priv);
    else if (!use_separate_smram || (new_addr >= 0xa0000))
        return mem_read_ramw(new_addr, priv);
    else
        return *(uint16_t *) &(dev->mapping.exec[addr - dev->host_base]);
}

static uint32_t
smram_readl(uint32_t addr, void *priv)
{
    smram_t *dev      = (smram_t *) priv;
    uint32_t new_addr = addr - dev->host_base + dev->ram_base;

    if (new_addr >= (1 << 30))
        return mem_read_ram_2gbl(new_addr, priv);
    else if (!use_separate_smram || (new_addr >= 0xa0000))
        return mem_read_raml(new_addr, priv);
    else
        return *(uint32_t *) &(dev->mapping.exec[addr - dev->host_base]);
}

static void
smram_write(uint32_t addr, uint8_t val, void *priv)
{
    smram_t *dev      = (smram_t *) priv;
    uint32_t new_addr = addr - dev->host_base + dev->ram_base;

    if (!use_separate_smram || (new_addr >= 0xa0000))
        mem_write_ram(new_addr, val, priv);
    else
        dev->mapping.exec[addr - dev->host_base] = val;
}

static void
smram_writew(uint32_t addr, uint16_t val, void *priv)
{
    smram_t *dev      = (smram_t *) priv;
    uint32_t new_addr = addr - dev->host_base + dev->ram_base;

    if (!use_separate_smram || (new_addr >= 0xa0000))
        mem_write_ramw(new_addr, val, priv);
    else
        *(uint16_t *) &(dev->mapping.exec[addr - dev->host_base]) = val;
}

static void
smram_writel(uint32_t addr, uint32_t val, void *priv)
{
    smram_t *dev      = (smram_t *) priv;
    uint32_t new_addr = addr - dev->host_base + dev->ram_base;

    if (!use_separate_smram || (new_addr >= 0xa0000))
        mem_write_raml(new_addr, val, priv);
    else
        *(uint32_t *) &(dev->mapping.exec[addr - dev->host_base]) = val;
}

/* Make a backup copy of host_base and size of all the SMRAM structs, needed so that if
   the SMRAM mappings change while in SMM, they will be recalculated on return. */
void
smram_backup_all(void)
{
    smram_t *temp_smram = base_smram, *next;

    while (temp_smram != NULL) {
        temp_smram->old_host_base = temp_smram->host_base;
        temp_smram->old_size      = temp_smram->size;

        next       = temp_smram->next;
        temp_smram = next;
    }
}

/* Recalculate any mappings, including the backup if returning from SMM. */
void
smram_recalc_all(int ret)
{
    smram_t *temp_smram = base_smram, *next;

    if (base_smram == NULL)
        return;

    if (ret) {
        while (temp_smram != NULL) {
            if (temp_smram->old_size != 0x00000000)
                mem_mapping_recalc(temp_smram->old_host_base, temp_smram->old_size);
            temp_smram->old_host_base = temp_smram->old_size = 0x00000000;

            next       = temp_smram->next;
            temp_smram = next;
        }
    }

    temp_smram = base_smram;

    while (temp_smram != NULL) {
        if (temp_smram->size != 0x00000000)
            mem_mapping_recalc(temp_smram->host_base, temp_smram->size);

        next       = temp_smram->next;
        temp_smram = next;
    }

    flushmmucache();
}

/* Delete a SMRAM mapping. */
void
smram_del(smram_t *smr)
{
    /* Do a sanity check */
    if ((base_smram == NULL) && (last_smram != NULL)) {
        fatal("smram_del(): NULL base SMRAM with non-NULL last SMRAM\n");
        return;
    } else if ((base_smram != NULL) && (last_smram == NULL)) {
        fatal("smram_del(): Non-NULL base SMRAM with NULL last SMRAM\n");
        return;
    } else if ((base_smram != NULL) && (base_smram->prev != NULL)) {
        fatal("smram_del(): Base SMRAM with a preceding SMRAM\n");
        return;
    } else if ((last_smram != NULL) && (last_smram->next != NULL)) {
        fatal("smram_del(): Last SMRAM with a following SMRAM\n");
        return;
    }

    if (smr == NULL) {
        fatal("smram_del(): Invalid SMRAM mapping\n");
        return;
    }

    /* Disable the entry. */
    smram_disable(smr);

    /* Zap it from the list. */
    if (smr->prev != NULL)
        smr->prev->next = smr->next;
    if (smr->next != NULL)
        smr->next->prev = smr->prev;

    /* Check if it's the first or the last mapping. */
    if (base_smram == smr)
        base_smram = smr->next;
    if (last_smram == smr)
        last_smram = smr->prev;

    free(smr);
}

/* Add a SMRAM mapping. */
smram_t *
smram_add(void)
{
    smram_t *temp_smram;

    /* Do a sanity check */
    if ((base_smram == NULL) && (last_smram != NULL)) {
        fatal("smram_add(): NULL base SMRAM with non-NULL last SMRAM\n");
        return NULL;
    } else if ((base_smram != NULL) && (last_smram == NULL)) {
        fatal("smram_add(): Non-NULL base SMRAM with NULL last SMRAM\n");
        return NULL;
    } else if ((base_smram != NULL) && (base_smram->prev != NULL)) {
        fatal("smram_add(): Base SMRAM with a preceding SMRAM\n");
        return NULL;
    } else if ((last_smram != NULL) && (last_smram->next != NULL)) {
        fatal("smram_add(): Last SMRAM with a following SMRAM\n");
        return NULL;
    }

    temp_smram = (smram_t *) malloc(sizeof(smram_t));
    if (temp_smram == NULL) {
        fatal("smram_add(): temp_smram malloc failed\n");
        return NULL;
    }
    memset(temp_smram, 0x00, sizeof(smram_t));
    memset(&(temp_smram->mapping), 0x00, sizeof(mem_mapping_t));

    /* Add struct to the beginning of the list if necessary.*/
    if (base_smram == NULL)
        base_smram = temp_smram;

    /* Add struct to the end of the list.*/
    if (last_smram == NULL)
        temp_smram->prev = NULL;
    else {
        temp_smram->prev = last_smram;
        last_smram->next = temp_smram;
    }
    last_smram = temp_smram;

    mem_mapping_add(&(temp_smram->mapping), 0x00000000, 0x00000000,
                    smram_read, smram_readw, smram_readl,
                    smram_write, smram_writew, smram_writel,
                    ram, MEM_MAPPING_SMRAM, temp_smram);

    smram_set_separate_smram(0);

    return temp_smram;
}

/* Set memory state in the specified model (normal or SMM) according to the specified flags,
   separately for bus and CPU. */
void
smram_map_ex(int bus, int smm, uint32_t addr, uint32_t size, int is_smram)
{
    if (bus)
        mem_set_access_smram_bus(smm, addr, size, is_smram);
    else
        mem_set_access_smram_cpu(smm, addr, size, is_smram);
}

/* Set memory state in the specified model (normal or SMM) according to the specified flags. */
void
smram_map(int smm, uint32_t addr, uint32_t size, int is_smram)
{
    smram_map_ex(0, smm, addr, size, is_smram);
    smram_map_ex(1, smm, addr, size, is_smram);
}

/* Disable a specific SMRAM mapping. */
void
smram_disable(smram_t *smr)
{
    if (smr == NULL) {
        fatal("smram_disable(): Invalid SMRAM mapping\n");
        return;
    }

    if (smr->size != 0x00000000) {
        smram_map(0, smr->host_base, smr->size, 0);
        smram_map(1, smr->host_base, smr->size, 0);

        smr->host_base = smr->ram_base = 0x00000000;
        smr->size                      = 0x00000000;

        mem_mapping_disable(&(smr->mapping));
    }
}

/* Disable all SMRAM mappings. */
void
smram_disable_all(void)
{
    smram_t *temp_smram = base_smram, *next;

    while (temp_smram != NULL) {
        smram_disable(temp_smram);

        next       = temp_smram->next;
        temp_smram = next;
    }
}

/* Enable SMRAM mappings according to flags for both normal and SMM modes, separately for bus
   and CPU. */
void
smram_enable_ex(smram_t *smr, uint32_t host_base, uint32_t ram_base, uint32_t size,
                int flags_normal, int flags_normal_bus, int flags_smm, int flags_smm_bus)
{
    if (smr == NULL) {
        fatal("smram_add(): Invalid SMRAM mapping\n");
        return;
    }

    if ((size != 0x00000000) && (flags_normal || flags_smm)) {
        smr->host_base = host_base;
        smr->ram_base  = ram_base,
        smr->size      = size;

        mem_mapping_set_addr(&(smr->mapping), smr->host_base, smr->size);
        if (!use_separate_smram || (smr->ram_base >= 0x000a0000)) {
            if (smr->ram_base < (1 << 30))
                mem_mapping_set_exec(&(smr->mapping), ram + smr->ram_base);
            else
                mem_mapping_set_exec(&(smr->mapping), ram2 + smr->ram_base - (1 << 30));
        } else {
            if (smr->ram_base == 0x00030000)
                mem_mapping_set_exec(&(smr->mapping), smram);
            else if (smr->ram_base == 0x00040000)
                mem_mapping_set_exec(&(smr->mapping), smram + 0x10000);
            else if (smr->ram_base == 0x00060000)
                mem_mapping_set_exec(&(smr->mapping), smram + 0x20000);
            else if (smr->ram_base == 0x00070000)
                mem_mapping_set_exec(&(smr->mapping), smram + 0x30000);
        }

        smram_map_ex(0, 0, host_base, size, flags_normal);
        smram_map_ex(1, 0, host_base, size, flags_normal_bus);
        smram_map_ex(0, 1, host_base, size, flags_smm);
        smram_map_ex(1, 1, host_base, size, flags_smm_bus);
    } else
        smram_disable(smr);
}

/* Enable SMRAM mappings according to flags for both normal and SMM modes. */
void
smram_enable(smram_t *smr, uint32_t host_base, uint32_t ram_base, uint32_t size, int flags_normal, int flags_smm)
{
    smram_enable_ex(smr, host_base, ram_base, size, flags_normal, flags_normal, flags_smm, flags_smm);
}

/* Checks if a SMRAM mapping is enabled or not. */
int
smram_enabled(smram_t *smr)
{
    int ret = 0;

    if (smr == NULL)
        ret = 0;
    else
        ret = (smr->size != 0x00000000);

    return ret;
}

/* Changes the SMRAM state. */
void
smram_state_change(smram_t *smr, int smm, int flags)
{
    if (smr == NULL) {
        fatal("smram_tate_change(): Invalid SMRAM mapping\n");
        return;
    }

    smram_map(smm, smr->host_base, smr->size, flags);
}

void
smram_set_separate_smram(uint8_t set)
{
    use_separate_smram = set;
}
