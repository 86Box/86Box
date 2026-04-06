/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Intel ICH2 GPIO Header
 *
 *
 *
 * Authors: Tiseno100,
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2022      Tiseno100.
 *          Copyright 2022-2023 Jasmine Iwanek.
 */

#ifndef EMU_INTEL_ICH2_GPIO_H
#define EMU_INTEL_ICH2_GPIO_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct intel_ich2_gpio_t {
    uint16_t gpio_addr;
    uint8_t  gpio_regs[48];
} intel_ich2_gpio_t;

void intel_ich2_gpio_base(int enable, uint16_t addr, intel_ich2_gpio_t *dev);

extern const device_t intel_ich2_gpio_device;

#ifdef __cplusplus
}
#endif

#endif /*EMU_INTEL_ICH2_GPIO_H*/
