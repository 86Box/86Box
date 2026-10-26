/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Handling of the emulated chipsets.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2019-2020 Miran Grca.
 */
#ifndef EMU_CHIPSET_H
#define EMU_CHIPSET_H

#include <stdint.h>

/* ACC */
extern const device_t acc2036_device;
extern const device_t acc2168_device;

/* ALi */
extern const device_t ali1217_device;
extern const device_t ali1429_device;
extern const device_t ali1429g_device;
extern const device_t ali1409_device;
extern const device_t ali1435_device;
extern const device_t ali1489_device;
extern const device_t ali1531_device;
extern const device_t ali1541_device;
extern const device_t ali1541_rowdecode_device;

#define ALI1543_SIO_370 0x10000ULL

extern const device_t ali1543_device;
extern const device_t ali1543c_device;
extern const device_t ali1621_device;
extern const device_t ali6117d_device;

/* AMD */
extern const device_t amd640_device;

/* ASUS */
extern const device_t asus48625_device;
extern const device_t isa486c_device;

/* Compaq */
extern const device_t compaq_device;

extern const device_t compaq_386_device;
extern const device_t compaq_genoa_device;

/* Contaq/Cypress */
extern const device_t contaq_82c596a_device;
extern const device_t contaq_82c597_device;

/* C&T */
extern const device_t ct_82c100_device;
extern const device_t neat_device;
extern const device_t neat_sx_device;
extern const device_t scat_device;
extern const device_t scat_4_device;
extern const device_t scat_sx_device;
extern const device_t cs8220_device;
extern const device_t cs8230_device;
extern const device_t cs4031_device;

/* G2 */
extern const device_t gc100_device;
extern const device_t gc100a_device;

/* GRiDcase */
extern const device_t grid1520_device;

/* Headland */
extern const device_t headland_gc10x_device;
extern const device_t headland_gc113_device;
extern const device_t headland_ht18a_device;
extern const device_t headland_ht18b_device;
extern const device_t headland_ht18c_device;
extern const device_t headland_ht21c_d_device;
extern const device_t headland_ht21e_device;

/* IMS */
extern const device_t ims8848_device;

/* Intel */
extern const device_t intel_82335_device;
extern const device_t i420ex_device;
extern const device_t i420ex_ide_device;
extern const device_t i420tx_device;
extern const device_t i420zx_device;
extern const device_t i430lx_device;
extern const device_t i430nx_device;
extern const device_t i430fx_device;
extern const device_t i430fx_old_device;
extern const device_t i430fx_rev02_device;
extern const device_t i430hx_device;
extern const device_t i430vx_device;
extern const device_t i430tx_device;
extern const device_t i440fx_device;
extern const device_t i440lx_device;
extern const device_t i440ex_device;
extern const device_t i440bx_device;
extern const device_t i440bx_no_agp_device;
extern const device_t i440gx_device;
extern const device_t i440zx_device;
extern const device_t i450kx_device;
extern const device_t i450gx_device;

extern const device_t pceb_device;
extern const device_t esc_device;

extern const device_t sio_device;
extern const device_t sio_zb_device;
extern const device_t sio_a_device;
extern void           sio_a_set_ioapic(void *priv, void *ioapic);

#define PIIX4_NVR_AMI_1995  0x0000000100000000
#define PIIX4_NVR_AMI_1995J 0x0000000200000000
/* Function 2 (USB) absent, for an emulated board whose PIIX4 has none. */
#define PIIX4_NO_USB        0x0000000400000000

extern const device_t piix_device;
extern const device_t piix_no_mirq_device;
extern const device_t piix_old_device;
extern const device_t piix_rev02_device;
extern void           piix_extsmi_raise(void);
extern const device_t piix3_device;
extern const device_t piix3_ioapic_device;
extern const device_t piix4_device;
extern const device_t piix4e_device;
extern const device_t slc90e66_device;

/* APIC devices are board-wiring entry points; chip/LAPIC internals stay opaque. */
extern const device_t ioapic_device;
extern const device_t ioapic_ami_device;
extern const device_t ioapic_esc_device;
extern void           ioapic_set_mmio_base(void *ioapic, uint32_t base);
extern int            ioapic_apic_path_active(const void *ioapic);
extern void           ioapic_set_intr_masked(void *ioapic, int masked);
extern void           ioapic_set_window_enabled(void *ioapic, int enabled);
extern void           ioapic_chip_irq(void *ioapic, uint8_t pin, int set);
extern const device_t ioapic_validated_up_device;
extern const device_t validated_up_apic_board_device;
extern const device_t integrated_cpu_lapic_device;

#define IOAPIC_LEGACY_IRQ_COUNT 16

typedef struct ioapic_legacy_irq_routing_t {
    uint8_t irq_to_intin[IOAPIC_LEGACY_IRQ_COUNT];
    /* The input PIRQ A-D are wired to, where the board wires them to the
       I/O APIC; 0 where it does not (INTIN0 is the 8259's INTR). */
    uint8_t pirq_to_intin[4];
} ioapic_legacy_irq_routing_t;

#define IOAPIC_TOPOLOGY_UP_APIC_DEFAULT   0x00000200
/* The MP table hook fires on the firmware's last POST code before booting:
   FFh on Award, 00h on AMI (checkpoint 00, "passing control to INT 19h"). */
#define IOAPIC_POST_TRIGGER_AMI           0x00000400
/* The I/O APIC inside the 82374 ESC rather than an 82093AA: sixteen pins,
   version 11h with MRE 0Fh, no SMI# input, and a window the ESC moves with
   its APICBASE register (ioapic_set_mmio_base). The 82379AB SIO.A's is the
   same: version 000F0011h, the same sixteen inputs, and an APICBASE of its
   own (82378ZB/82379AB data sheet 3.1.27, 3.7.4, Figure 8). */
#define IOAPIC_VARIANT_ESC                0x00000800
/* No I/O APIC at all: the processor's own local APIC on a board without
   one, with no inputs and no register window of its own. */
#define IOAPIC_VARIANT_NONE               0x00002000
/* The I/O APIC inside the VIA VT82C686B, and in the VT82C686A's CG silicon
   with the same registers (VT82C686A data sheet 1.82, p. 116): twenty-four
   pins with no SMI# input, an Arbitration ID that always reads 0, and two
   registers past the 82093AA's window, the IRQ Pin Assertion register at
   FEC00020h and the EOI register at FEC00040h (VT82C686B data sheet 1.8,
   p. 116). Its window is decoded while the southbridge's Internal APIC
   Enable says so (ioapic_set_window_enabled). */
#define IOAPIC_VARIANT_VIA                0x00004000

/* Append-only save ABI: loaders gate each later field by version. */
#define IOAPIC_STATE_VERSION          13
#define IOAPIC_STATE_VERSION_MIN      1
#define IOAPIC_STATE_MAX_LAPICS       16
#define IOAPIC_STATE_REDIR_ENTRIES    24
#define IOAPIC_STATE_LAPIC_REG_COUNT  (0x400 >> 4)
#define IOAPIC_STATE_LAPIC_BIT_WORDS  8
#define IOAPIC_STATE_PCI_INTIN_ROUTES 256

typedef struct ioapic_pci_intin_route_state_t {
    uint8_t bus;
    uint8_t device;
    uint8_t intpin;
    uint8_t intin;
} ioapic_pci_intin_route_state_t;

typedef struct ioapic_state_t {
    uint32_t version;

    uint8_t expose_mp_tables;
    uint8_t uniprocessor_apic;
    uint8_t mp_table_logged;
    uint8_t mp_table_skip_logged;
    uint8_t lowest_priority_next;

    uint8_t  lapic_count;
    uint8_t  lapic_id;
    uint8_t  lapic_extint_pending;
    uint8_t  lapic_extint_pin;
    uint8_t  lapic_timer_enabled;
    uint64_t lapic_base_msr;
    uint32_t lapic_regs[IOAPIC_STATE_LAPIC_REG_COUNT];
    uint32_t lapic_irr[IOAPIC_STATE_LAPIC_BIT_WORDS];
    uint32_t lapic_isr[IOAPIC_STATE_LAPIC_BIT_WORDS];
    uint32_t lapic_tmr[IOAPIC_STATE_LAPIC_BIT_WORDS];
    uint32_t lapic_timer_initial;
    uint32_t lapic_timer_current;

    /* Array fields carry current multi-LAPIC state; scalar fields preserve old saves. */
    uint8_t  lapic_ids[IOAPIC_STATE_MAX_LAPICS];
    uint8_t  lapic_extint_pending_arr[IOAPIC_STATE_MAX_LAPICS];
    uint8_t  lapic_extint_pin_arr[IOAPIC_STATE_MAX_LAPICS];
    uint8_t  lapic_timer_enabled_arr[IOAPIC_STATE_MAX_LAPICS];
    uint8_t  lapic_started[IOAPIC_STATE_MAX_LAPICS];
    uint8_t  lapic_init_asserted[IOAPIC_STATE_MAX_LAPICS];
    uint8_t  lapic_wait_for_sipi[IOAPIC_STATE_MAX_LAPICS];
    uint8_t  lapic_sipi_vector[IOAPIC_STATE_MAX_LAPICS];
    uint8_t  lapic_parked[IOAPIC_STATE_MAX_LAPICS];
    uint64_t lapic_base_msrs[IOAPIC_STATE_MAX_LAPICS];
    uint32_t lapic_regs_arr[IOAPIC_STATE_MAX_LAPICS][IOAPIC_STATE_LAPIC_REG_COUNT];
    uint32_t lapic_irr_arr[IOAPIC_STATE_MAX_LAPICS][IOAPIC_STATE_LAPIC_BIT_WORDS];
    uint32_t lapic_isr_arr[IOAPIC_STATE_MAX_LAPICS][IOAPIC_STATE_LAPIC_BIT_WORDS];
    uint32_t lapic_tmr_arr[IOAPIC_STATE_MAX_LAPICS][IOAPIC_STATE_LAPIC_BIT_WORDS];
    uint32_t lapic_timer_initial_arr[IOAPIC_STATE_MAX_LAPICS];
    uint32_t lapic_timer_current_arr[IOAPIC_STATE_MAX_LAPICS];

    uint8_t  ioapic_id;
    uint8_t  ioapic_index;
    uint8_t  ioapic_arb_id;
    uint32_t ioapic_redir[IOAPIC_STATE_REDIR_ENTRIES * 2];
    uint32_t ioapic_pin_raw_lines;
    uint32_t ioapic_pin_lines;
    uint32_t ioapic_pin_level;
    uint8_t  ioapic_smiout_asserted;
    uint8_t  ioapic_pin_vector[IOAPIC_STATE_REDIR_ENTRIES];
    uint16_t pci_intin_route_count;
    ioapic_pci_intin_route_state_t pci_intin_routes[IOAPIC_STATE_PCI_INTIN_ROUTES];
} ioapic_state_t;

typedef uint8_t (*lapic_index_source_t)(void);

/* APIC facade APIs hide LAPIC storage from board/PIC callers. */
extern void           apic_cpu_lapic_set_ioapic_policy(int enabled);
extern int            apic_cpu_lapic_ioapic_policy_enabled(void);
extern int            apic_cpu_lapic_enabled(void);
extern int            apic_cpu_feature_enabled_for_model(int model_capable);
extern void           lapic_set_index_source(lapic_index_source_t source);
extern void           lapic_id_sync_index(uint8_t lapic_index, uint8_t id);
extern void           lapic_apic_base_msr_sync_index(uint8_t lapic_index, uint64_t val);
extern void           lapic_svr_sync_index(uint8_t lapic_index, uint32_t svr);
extern void           lapic_tpr_sync_index(uint8_t lapic_index, uint32_t tpr);
extern void           lapic_ldr_sync_index(uint8_t lapic_index, uint32_t ldr);
extern void           lapic_dfr_sync_index(uint8_t lapic_index, uint32_t dfr);
extern void           lapic_version_sync_index(uint8_t lapic_index, uint32_t version);
extern void           lapic_arb_id_sync_index(uint8_t lapic_index, uint32_t apr);
extern void           lapic_ppr_sync_index(uint8_t lapic_index, uint32_t ppr);
extern void           lapic_esr_sync_index(uint8_t lapic_index, uint32_t esr);
extern void           lapic_icr_lo_sync_index(uint8_t lapic_index, uint32_t icr_lo);
extern void           lapic_icr_hi_sync_index(uint8_t lapic_index, uint32_t icr_hi);
extern void           lapic_rrr_sync_index(uint8_t lapic_index, uint32_t rrr);
extern void           lapic_lvt_timer_sync_index(uint8_t lapic_index, uint32_t lvt);
extern void           lapic_lvt_thermal_sync_index(uint8_t lapic_index, uint32_t lvt);
extern void           lapic_lvt_perf_sync_index(uint8_t lapic_index, uint32_t lvt);
extern void           lapic_lvt_lint0_sync_index(uint8_t lapic_index, uint32_t lvt);
extern void           lapic_lvt_lint1_sync_index(uint8_t lapic_index, uint32_t lvt);
extern void           lapic_lvt_error_sync_index(uint8_t lapic_index, uint32_t lvt);
extern void           lapic_timer_initial_sync_index(uint8_t lapic_index, uint32_t initial);
extern void           lapic_timer_divide_cfg_sync_index(uint8_t lapic_index, uint32_t divide_cfg);
extern void           lapic_lint1_redeliver_index(uint8_t index);
extern int            ioapic_external_interrupt_pending(void);
extern int            ioapic_external_interrupt_vector(void);
extern int            ioapic_pic_fallback_allowed(void);
extern int            ioapic_lint1_raise(void);
extern void           ioapic_retry_deliveries(void);
extern void           ioapic_pic_intr(int active);
extern int            ioapic_pci_intin_own_wire(uint8_t intin, uint8_t irq_line);
extern uint8_t        ioapic_pirq_intin(uint8_t pirq);
extern void           ioapic_set_pin0_smi(void *ioapic, int smi);
extern void           ioapic_intin_irq(uint8_t pin, int level, int set);
extern void           ioapic_legacy_irq(uint16_t mask, int level, int set);
extern int            ioapic_legacy_irq_enabled(void);
extern void           ioapic_set_legacy_irq_routing(void *ioapic, const ioapic_legacy_irq_routing_t *routing);
extern int            ioapic_state_save(ioapic_state_t *state);
extern int            ioapic_state_load(const ioapic_state_t *state);

/* VTech */
extern const device_t laserxt_device;
extern const device_t lxt3_device;

/* Olivetti */
extern const device_t olivetti_eva_device;

/* OPTi */
extern const device_t opti283_device;
extern const device_t opti291_device;
extern const device_t opti381_device;
extern const device_t opti391_device;
extern const device_t opti481_device;
extern const device_t opti493_device;
extern const device_t opti495slc_device;
extern const device_t opti495sx_device;
extern const device_t opti496_device;
extern const device_t opti498_device;
extern const device_t opti499_device;
extern const device_t opti601_device;
extern const device_t opti602_device;
extern const device_t opti802g_device;
extern const device_t opti802g_pci_device;
extern const device_t opti822_device;
extern const device_t opti895_device;

extern const device_t opti5x7_device;
extern const device_t opti5x7_pci_device;
extern const device_t opti55x_device;
extern const device_t opti55x_noide_device;

/* Philips */
extern const device_t philips_device;

/* Sanyo */
extern const device_t sanyo_device;

/* SiS */
extern const device_t rabbit_device;
extern const device_t sis_85c401_device;
extern const device_t sis_85c411_device;
extern void           sis_85c411_set_board_id(const char *mfg, uint16_t product, uint8_t rev);
extern const device_t sis_85c460_device;
extern const device_t sis_85c461_device;
extern const device_t sis_85c471_device;
extern const device_t sis_85c496_device;
extern const device_t sis_85c496_ls486e_device;
extern const device_t sis_85c50x_device;
extern const device_t sis_550x_85c503_device;
extern const device_t sis_85c50x_5503_device;
extern const device_t sis_550x_device;
extern const device_t sis_5511_device;
extern const device_t sis_5571_device;
extern const device_t sis_5581_device;
extern const device_t sis_5591_1997_device;
extern const device_t sis_530_device;
extern const device_t sis_620_1997_device;
extern const device_t sis_620_device;
extern const device_t sis_5591_device;
extern const device_t sis_5600_1997_device;
extern const device_t sis_5600_device;

/* ST */
extern const device_t stpc_client_device;
extern const device_t stpc_consumer2_device;
extern const device_t stpc_elite_device;
extern const device_t stpc_atlas_device;
extern const device_t stpc_serial_device;
extern const device_t stpc_lpt_device;

/* Symphony */
extern const device_t sl82c461_device;

/* UMC */
extern const device_t umc_8886f_device;
extern const device_t umc_8886af_device;
extern const device_t umc_8886bf_device;
extern const device_t umc_8890_device;
extern const device_t umc_hb4_device;

/* Utron */
extern const device_t ut85c50x_device;

/* VIA */
extern const device_t via_vt82c49x_device;
extern const device_t via_vt82c49x_pci_device;
extern const device_t via_vt82c49x_pci_ide_device;
extern const device_t via_vt82c505_device;
extern const device_t via_vpx_device;
extern const device_t via_vp3_device;
extern const device_t via_mvp3_device;
extern const device_t via_apro_device;
extern const device_t via_apro133_device;
extern const device_t via_apro133a_device;
extern const device_t via_vt8601_device;

#define VIA_PIPC_NO_KBC 0x0000000100000000ULL

extern const device_t via_vt82c586b_device;
extern const device_t via_vt82c596a_device;
extern const device_t via_vt82c596b_device;
extern const device_t via_vt82c686a_device;
extern const device_t via_vt82c686b_device;
extern void           via_pipc_set_ioapic(void *priv, void *ioapic);
extern const device_t via_vt8231_device;

/* VLSI */
/* Size of the secondary cache SRAM, for the Direct Access mode of CACHCTL. */
#define VL82C480_CACHE_128K  0x0400
#define VL82C480_CACHE_256K  0x0800
#define VL82C480_CACHE_SHIFT 8

extern const device_t vl82c480_device;
extern const device_t vl82c486_device;
extern const device_t vl82c59x_device;
extern const device_t vl82c59x_compaq_device;
extern const device_t vl82c59x_wildcat_device;
extern const device_t vl82c59x_wildcat_compaq_device;
extern const device_t vlsi_scamp_device;

/* WD */
extern const device_t wd76c10_device;

/* Miscellaneous Hardware */
extern const device_t tulip_jumper_device;

extern const device_t dell_jumper_device;

extern const device_t nec_mate_unk_device;

extern const device_t phoenix_486_jumper_device;
extern const device_t phoenix_486_jumper_pci_device;
extern const device_t phoenix_486_jumper_pci_pb600_device;
extern const device_t phoenix_486_jumper_monsoon_device;
extern const device_t phoenix_486_jumper_pb400_device;
extern const device_t phoenix_486_jumper_pb430_device;

extern const device_t ast_readout_device;
extern const device_t ast_nvr_device;

extern const device_t radisys_config_device;

extern const device_t zenith_scratchpad_device;
#endif /*EMU_CHIPSET_H*/
