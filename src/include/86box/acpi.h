/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the ACPI emulation.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2020 Miran Grca.
 */
#ifndef ACPI_H
#define ACPI_H

#ifdef __cplusplus
extern "C" {
#else
#include <stdatomic.h>
#endif

#define ACPI_TIMER_FREQ 3579545
#define PM_FREQ         ACPI_TIMER_FREQ

#define RSM_STS         (1 << 15)
#define WAK_STS         (1 << 15)
#define PWRBTNOR_STS    (1 << 11)
#define RTC_STS         (1 << 10)
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

/* General-purpose event sources, as acpi_gpe_event() takes them. Each chip
   maps them onto its own status bits; one it lacks is ignored. */
#define ACPI_GPE_RI     0 /* ring indicate (RI#) */
#define ACPI_GPE_USB    1 /* USB resume */
#define ACPI_GPE_LID    2 /* notebook lid */
#define ACPI_GPE_THRM   3 /* thermal alarm (THRM#) */
#define ACPI_GPE_GPI    4 /* general-purpose input (PIIX4 GPI1#, VIA PME#) */
#define ACPI_GPE_MAX    5

/* acpi_set_event_hook() events: what a southbridge with its own SMI enables
   decides about. */
#define ACPI_HOOK_SMI      0 /* an enabled event in legacy mode (SCI_EN clear) */
#define ACPI_HOOK_PWRBTN   1 /* the power button was pressed */
#define ACPI_HOOK_OVERRIDE 2 /* may a 4 s press force soft off? (returns 0 if not) */

#define ACPI_ENABLE     0xf1
#define ACPI_DISABLE    0xf0

#define VEN_ALI              0x010b9
#define VEN_INTEL            0x08086
#define VEN_SIS_5582         0x01039
#define VEN_SIS_5595_1997    0x11039
#define VEN_SIS_5595         0x21039
#define VEN_SMC              0x01055
#define VEN_VIA              0x01106
#define VEN_VIA_596B         0x11106

typedef struct acpi_regs_t {
    uint8_t  acpitst;
    uint8_t  auxen;
    uint8_t  auxsts;
    uint8_t  smicmd;
    uint8_t  gpio_dir;
    uint8_t  gpio_val;
    uint8_t  muxcntrl;
    uint8_t  ali_soft_smi;
    uint8_t  timer32;
    uint8_t  smireg;
    uint8_t  gpireg[3];
    uint8_t  gporeg[4];
    uint8_t  extiotrapsts;
    uint8_t  extiotrapen;
    uint8_t  reg_12;
    uint8_t  reg_13;
    uint8_t  smi_cmd;
    uint8_t  reg_24;
    uint8_t  reg_25;
    uint8_t  reg_26;
    uint8_t  smi_en_val;
    uint8_t  smi_dis_val;
    uint8_t  mail_box;
    uint8_t  reg_2b;
    uint8_t  gp_tmr;
    uint8_t  leg_sts;
    uint8_t  leg_en;
    uint8_t  tst_ctl;
    uint8_t  reg_34;
    uint8_t  index;
    uint8_t  reg_ff;
    uint8_t  pm2_cntrl;
    uint16_t pmsts;
    uint16_t pmen;
    uint16_t pmcntrl;
    uint16_t gpsts;
    uint16_t gpsts1;
    uint16_t gpen;
    uint16_t gpen1;
    uint16_t gpscien;
    uint16_t gpcntrl;
    uint16_t gplvl;
    uint16_t gpmux;
    uint16_t gpsel;
    uint16_t gpsmien;
    uint16_t pscntrl;
    uint16_t gpscists;
    uint16_t reg_14;
    uint16_t reg_16;
    uint16_t reg_18;
    uint16_t reg_1a;
    uint16_t reg_1c;
    uint16_t gpe_mul;
    uint16_t gpe_ctl;
    uint16_t gpe_smi;
    uint16_t gpe_rl;
    int      smi_lock;
    int      smi_active;
    uint32_t pcntrl;
    uint32_t p2cntrl;
    uint32_t glbsts;
    uint32_t devsts;
    uint32_t glben;
    uint32_t glbctl;
    uint32_t devctl;
    uint32_t padsts;
    uint32_t paden;
    uint32_t gptren;
    uint32_t gptimer;
    uint32_t gpo_val;
    uint32_t gpi_val;
    uint32_t extsmi_val;
    uint32_t reg_0c;
    uint32_t gpe_sts;
    uint32_t gpe_en;
    uint32_t gpe_pin;
    uint32_t gpe_io;
    uint32_t gpe_pol;
} acpi_regs_t;

typedef struct acpi_t {
    acpi_regs_t regs;
    uint8_t     gpireg2_default;
    uint8_t     irq_state;
    uint8_t     pad[2];
    uint8_t     gporeg_default[4];
    uint8_t     suspend_types[8];
    uint16_t    io_base;
    uint16_t    aux_io_base;
    int         vendor;
    int         slot;
    int         irq_mode;
    int         irq_pin;
    int         irq_line;
    int         mirq_is_level;
    int         usb_sts_w1c; /* GLBSTS bit 1 as on the SLC90E66 */
    int         usb_smi_level; /* SiS 5595: last level of the OHCI's SMI output */
    int         rtc_sts_seen;  /* acpi_rtc_status at the last poll */
    int         sleeping;      /* In a sleeping state (S1-S3) since SLP_EN */
    int         sleep_typ;     /* SLP_TYP written with SLP_EN */
    int         sleep_flags;   /* Its suspend_types[] entry */
    int         wake_pending;  /* A wake event, acted on by acpi_sleep_run() */
    int         pwrbtn_held;   /* ms the power button has been held */
    uint64_t    pmtmr_latch;
    int         power_on;      /* The next reset is a power on: resume event */
    int         gp_count;      /* SiS general purpose timer: its count, */
    uint64_t    gp_last_clock; /* and the ACPI clock when it started */
    int         piix4_ab;      /* PIIX4 before the PIIX4E: its throttling */
    pc_timer_t  timer;
    pc_timer_t  resume_timer;
    pc_timer_t  pwrbtn_timer;
    pc_timer_t  gp_timer;
    pc_timer_t  per_timer;
    nvr_t      *nvr;
#ifdef APM_H
    apm_t      *apm;
#else
    void      *apm;
#endif
    void       *i2c;
    void      (*trap_update)(void *priv);
    void       *trap_priv;
    void      (*sci_hook)(void *priv, int set);
    void       *sci_hook_priv;
    int       (*event_hook)(void *priv, int event);
    void       *event_hook_priv;
    void       *smbus;
    void       *priv;
} acpi_t;

/* Global variables. */
extern int        acpi_rtc_status;
#ifndef __cplusplus
/* std::atomic_int is a different type from C's atomic_int, so C++ uses acpi_pwrbut_press(). */
extern atomic_int acpi_pwrbut_pressed;
#endif
extern int        acpi_enabled;
/* Nonzero while the machine is in an ACPI sleeping state: pc_run() then calls
   acpi_sleep_run() instead of running the CPU. */
extern int        acpi_sleeping;

extern const device_t acpi_ali_device;
extern const device_t acpi_intel_device;
extern const device_t acpi_smc_device;
extern const device_t acpi_via_device;
extern const device_t acpi_via_596b_device;
extern const device_t acpi_sis_5582_device;
extern const device_t acpi_sis_5595_1997_device;
extern const device_t acpi_sis_5595_device;

/* Functions */
extern void    acpi_pwrbut_press(void);
/* Holds the power button until the 4 s override (or 5 s where the chip has
   it turned off), from any thread. */
extern void    acpi_pwrbut_hold(void);
/* Runs the machine's timers for cycs CPU cycles without the CPU. */
extern void    acpi_sleep_run(int32_t cycs);
/* An event on a general-purpose input of the board's ACPI chip. */
extern void    acpi_gpe_event(int source);
extern void    acpi_update_irq(acpi_t *dev);
extern void    acpi_raise_smi(void *priv, int do_smi);
extern void    acpi_gp_timer_timeout(void *priv, int n);
extern void    acpi_update_io_mapping(acpi_t *dev, uint32_t base, int chipset_en);
extern void    acpi_update_aux_io_mapping(acpi_t *dev, uint32_t base, int chipset_en);
extern void    acpi_init_gporeg(acpi_t *dev, uint8_t val0, uint8_t val1, uint8_t val2, uint8_t val3);
extern void    acpi_set_timer32(acpi_t *dev, uint8_t timer32);
extern void    acpi_set_slot(acpi_t *dev, int slot);
/* The PIIX4 (82371AB) rather than the PIIX4E: its throttling starts at a
   P_LVL2 read, ends at a break event and has the duty cycle inverted. */
extern void    acpi_set_piix4_ab(acpi_t *dev, int ab);
extern void    acpi_set_irq_mode(acpi_t *dev, int irq_mode);
extern void    acpi_set_irq_pin(acpi_t *dev, int irq_pin);
extern void    acpi_set_irq_line(acpi_t *dev, int irq_line);
extern void    acpi_set_mirq_is_level(acpi_t *dev, int mirq_is_level);
/* Makes GLBSTS bit 1 behave as the SLC90E66's USB_STS: write-1-to-clear,
   and an SMI# source only with GLBEN bit 0 set. Without it the bit is the
   PIIX4's LEGACY_USB_STS. */
extern void    acpi_set_usb_sts_w1c(acpi_t *dev, int w1c);
extern void    acpi_set_gpireg2_default(acpi_t *dev, uint8_t gpireg2_default);
extern void    acpi_set_nvr(acpi_t *dev, nvr_t *nvr);
extern void    acpi_set_trap_update(acpi_t *dev, void (*update)(void *priv), void *priv);
extern void    acpi_set_sci_hook(acpi_t *dev, void (*hook)(void *priv, int set), void *priv);
extern void    acpi_set_event_hook(acpi_t *dev, int (*hook)(void *priv, int event), void *priv);
extern uint8_t acpi_ali_soft_smi_status_read(acpi_t *dev);
extern void    acpi_ali_soft_smi_status_write(acpi_t *dev, uint8_t soft_smi);
extern void *  acpi_get_smbus(void *priv);
extern void    acpi_sis5582_pmu_event(void *priv);
extern void    acpi_sis5595_smi_raise(void *priv);
extern void    acpi_sis5595_pmu_event(void *priv);
extern void    acpi_sis5595_smbus_event(void *priv);
/* The OHCI's SMI output as the 5595's USB SMI# source (LEG_STS bit 6). */
extern void    acpi_sis5595_usb_smi(void *priv, int level);
extern void    acpi_sis_usb_event(acpi_t *dev);
extern void    acpi_sis5595_software_smi(void *priv);

#ifdef __cplusplus
}
#endif

#endif /*ACPI_H*/
