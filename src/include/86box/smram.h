/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the SMRAM interface.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016-2020 Miran Grca.
 */

#ifndef EMU_SMRAM_H
# define EMU_SMRAM_H

typedef struct _smram_
{
    struct _smram_	*prev, *next;

    mem_mapping_t	mapping;

    uint32_t		host_base, ram_base,
			size,
			old_host_base, old_size;
} smram_t;


/* Make a backup copy of host_base and size of all the SMRAM structs, needed so that if
   the SMRAM mappings change while in SMM, they will be recalculated on return. */
extern void	smram_backup_all(void);
/* Recalculate any mappings, including the backup if returning from SMM. */
extern void	smram_recalc_all(int ret);
/* Delete a SMRAM mapping. */
extern void	smram_del(smram_t *smr);
/* Add a SMRAM mapping. */
extern smram_t *smram_add(void);
/* Set memory state in the specified model (normal or SMM) according to the specified flags,
   separately for bus and CPU. */
extern void	smram_map_ex(int bus, int smm, uint32_t addr, uint32_t size, int is_smram);
/* Set memory state in the specified model (normal or SMM) according to the specified flags. */
extern void	smram_map(int smm, uint32_t addr, uint32_t size, int is_smram);
/* Disable a specific SMRAM mapping. */
extern void	smram_disable(smram_t *smr);
/* Disable all SMRAM mappings. */
extern void	smram_disable_all(void);
/* Enable SMRAM mappings according to flags for both normal and SMM modes, separately for bus
   and CPU. */
extern void smram_enable_ex(smram_t *smr, uint32_t host_base, uint32_t ram_base, uint32_t size,
			    int flags_normal, int flags_normal_bus, int flags_smm, int flags_smm_bus);
/* Enable SMRAM mappings according to flags for both normal and SMM modes. */
extern void	smram_enable(smram_t *smr, uint32_t host_base, uint32_t ram_base, uint32_t size,
			     int flags_normal, int flags_smm);
/* Checks if a SMRAM mapping is enabled or not. */
extern int	smram_enabled(smram_t *smr);
/* Changes the SMRAM state. */
extern void	smram_state_change(smram_t *smr, int smm, int flags);
/* Enables or disables the use of a separate SMRAM for addresses below A0000. */
extern void	smram_set_separate_smram(uint8_t set);

#endif	/*EMU_SMRAM_H*/
