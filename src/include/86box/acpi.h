/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the ACPI emulation.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2020 Miran Grca.
 */
#ifndef ACPI_H
#define ACPI_H

#ifdef __cplusplus
extern "C" {
#endif

#define ACPI_TIMER_FREQ 3579545
#define PM_FREQ         ACPI_TIMER_FREQ

#define RSM_STS         (1 << 15)
#define PWRBTN_STS      (1 << 8)
#define GBL_STS         (1 << 5)
#define BM_STS          (1 << 4)
#define TMROF_STS       (1 << 0)

#define RTC_EN          (1 << 10)
#define PWRBTN_EN       (1 << 8)
#define GBL_EN          (1 << 5)
#define TMROF_EN        (1 << 0)

#define SCI_EN          (1 << 0)
#define SUS_EN          (1 << 13)

#define SUS_POWER_OFF   (1 << 0)
#define SUS_SUSPEND     (1 << 1)
#define SUS_NVR         (1 << 2)
#define SUS_RESET_CPU   (1 << 3)
#define SUS_RESET_CACHE (1 << 4)
#define SUS_RESET_PCI   (1 << 5)

#define ACPI_ENABLE     0xf1
#define ACPI_DISABLE    0xf0

#define VEN_ALI         0x010b9
#define VEN_INTEL       0x08086
#define VEN_SIS         0x01039
#define VEN_SMC         0x01055
#define VEN_VIA         0x01106
#define VEN_VIA_596B    0x11106

typedef struct
{
    uint8_t acpitst, auxen, auxsts, plvl2, plvl3,
        smicmd, gpio_dir,
        gpio_val, muxcntrl, ali_soft_smi,
        timer32, smireg,
        gpireg[3], gporeg[4],
        extiotrapsts, extiotrapen;
    uint16_t pmsts, pmen,
        pmcntrl, gpsts, gpsts1,
        gpen, gpen1, gpscien,
        gpcntrl, gplvl, gpmux,
        gpsel, gpsmien, pscntrl,
        gpscists;
    int      smi_lock, smi_active;
    uint32_t pcntrl, p2cntrl, glbsts,
        devsts, glben,
        glbctl, devctl,
        padsts, paden,
        gptren, gptimer,
        gpo_val, gpi_val,
        extsmi_val, pad0;
} acpi_regs_t;

typedef struct
{
    acpi_regs_t regs;
    uint8_t     gpireg2_default, pad[3],
        gporeg_default[4],
        suspend_types[8];
    uint16_t io_base, aux_io_base;
    int      vendor,
        slot, irq_mode,
        irq_pin, irq_line,
        mirq_is_level;
    pc_timer_t timer, resume_timer;
    nvr_t     *nvr;
    apm_t     *apm;
    void      *i2c,
        (*trap_update)(void *priv), *trap_priv;
} acpi_t;

/* Global variables. */
extern int acpi_rtc_status;

extern const device_t acpi_ali_device;
extern const device_t acpi_intel_device;
extern const device_t acpi_smc_device;
extern const device_t acpi_via_device;
extern const device_t acpi_via_596b_device;

/* Functions */
extern void    acpi_update_irq(acpi_t *dev);
extern void    acpi_raise_smi(void *priv, int do_smi);
extern void    acpi_update_io_mapping(acpi_t *dev, uint32_t base, int chipset_en);
extern void    acpi_update_aux_io_mapping(acpi_t *dev, uint32_t base, int chipset_en);
extern void    acpi_init_gporeg(acpi_t *dev, uint8_t val0, uint8_t val1, uint8_t val2, uint8_t val3);
extern void    acpi_set_timer32(acpi_t *dev, uint8_t timer32);
extern void    acpi_set_slot(acpi_t *dev, int slot);
extern void    acpi_set_irq_mode(acpi_t *dev, int irq_mode);
extern void    acpi_set_irq_pin(acpi_t *dev, int irq_pin);
extern void    acpi_set_irq_line(acpi_t *dev, int irq_line);
extern void    acpi_set_mirq_is_level(acpi_t *dev, int mirq_is_level);
extern void    acpi_set_gpireg2_default(acpi_t *dev, uint8_t gpireg2_default);
extern void    acpi_set_nvr(acpi_t *dev, nvr_t *nvr);
extern void    acpi_set_trap_update(acpi_t *dev, void (*update)(void *priv), void *priv);
extern uint8_t acpi_ali_soft_smi_status_read(acpi_t *dev);
extern void    acpi_ali_soft_smi_status_write(acpi_t *dev, uint8_t soft_smi);

#ifdef __cplusplus
}
#endif

#endif /*ACPI_H*/
