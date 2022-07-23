/*
 * Intel ICH2 Trap Header
 *
 * Authors:	Tiseno100,
 *
 * Copyright 2022 Tiseno100.
 */

#ifndef EMU_INTEL_ICH2_TRAP_H
# define EMU_INTEL_ICH2_TRAP_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct intel_ich2_trap_t
{
    acpi_t  *acpi;
    void *trap;
} intel_ich2_trap_t;

extern void intel_ich2_trap_set_acpi(intel_ich2_trap_t *trap, acpi_t *acpi);
extern void intel_ich2_device_trap_setup(int enable, uint8_t acpi_reg, uint8_t acpi_reg_val, uint16_t addr, uint16_t size, int is_hdd, intel_ich2_trap_t *trap);

extern const device_t   intel_ich2_trap_device;

#ifdef __cplusplus
}
#endif

#endif	/*EMU_INTEL_ICH2_TRAP_H*/
