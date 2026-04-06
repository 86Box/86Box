/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Intel ICH2 Trap Header
 *
 *
 *
 * Authors: Tiseno100,
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2022      Tiseno100.
 *          Copyright 2022-2023 Jasmine Iwanek.
 */

#ifndef EMU_INTEL_ICH2_TRAP_H
#define EMU_INTEL_ICH2_TRAP_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct intel_ich2_trap_t {
    acpi_t *acpi;
    void   *trap;
} intel_ich2_trap_t;

extern void intel_ich2_trap_set_acpi(intel_ich2_trap_t *trap, acpi_t *acpi);
extern void intel_ich2_device_trap_setup(uint8_t acpi_reg, uint8_t acpi_reg_val, uint16_t addr, uint16_t size, intel_ich2_trap_t *dev);

extern const device_t intel_ich2_trap_device;

#ifdef __cplusplus
}
#endif

#endif /*EMU_INTEL_ICH2_TRAP_H*/
