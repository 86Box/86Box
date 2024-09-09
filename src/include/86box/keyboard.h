/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the keyboard interface.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2025 Miran Grca.
 *          Copyright 2017-2019 Fred N. van Kempen.
 */

#ifndef EMU_KEYBOARD_H
#define EMU_KEYBOARD_H

enum {
    DEV_KBD = 0,
    DEV_AUX = 1
};

enum {
    DEV_STATE_MAIN_1                = 0,
    DEV_STATE_MAIN_OUT              = 1,
    DEV_STATE_MAIN_2                = 2,
    DEV_STATE_MAIN_CMD              = 3,
    DEV_STATE_MAIN_WANT_IN          = 4,
    DEV_STATE_MAIN_IN               = 5,
    DEV_STATE_EXECUTE_BAT           = 6,
    DEV_STATE_MAIN_WANT_EXECUTE_BAT = 7
};

/* Used by the AT / PS/2 keyboard controller, common device, keyboard, and mouse. */
typedef struct kbc_at_port_t {
    uint8_t wantcmd;
    uint8_t dat;

    int16_t out_new;

    void *priv;

    void (*poll)(void *priv);
} kbc_at_port_t;

/* Used by the AT / PS/2 common device, keyboard, and mouse. */
typedef struct atkbc_dev_t {
    const char *name; /* name of this device */

    uint8_t type;
    uint8_t command;
    uint8_t last_scan_code;
    uint8_t state;
    uint8_t resolution;
    uint8_t rate;
    uint8_t cmd_queue_start;
    uint8_t cmd_queue_end;
    uint8_t queue_start;
    uint8_t queue_end;

    uint16_t flags;

    /* Internal FIFO, not present on real devices, needed for commands that
       output multiple bytes. */
    uint8_t cmd_queue[16];

    uint8_t queue[64];

    int     fifo_mask;
    int     mode;
    int     x;
    int     y;
    int     z;
    int     b;
    int     ignore;

    int     *scan;

    void    (*process_cmd)(void *priv);
    void    (*execute_bat)(void *priv);

    kbc_at_port_t *port;
} atkbc_dev_t;

typedef struct scancode {
    const uint8_t mk[4];
    const uint8_t brk[4];
} scancode;

#define STATE_SHIFT_MASK 0x22
#define STATE_RSHIFT     0x20
#define STATE_LSHIFT     0x02

#define FAKE_LSHIFT_ON   0x100
#define FAKE_LSHIFT_OFF  0x101
#define LSHIFT_ON        0x102
#define LSHIFT_OFF       0x103
#define RSHIFT_ON        0x104
#define RSHIFT_OFF       0x105

/* KBC #define's */
/* IBM-style controllers */
#define KBC_IBM_PC_XT          0x0000 /* IBM PC/XT */
#define KBC_IBM_PCJR           0x0001 /* IBM PCjr */
#define KBC_IBM_TYPE_1         0x0002 /* IBM AT / PS/2 Type 1 */
#define KBC_IBM_TYPE_2         0x0003 /* IBM PS/2 Type 2 */
#define KBC_AMI_ACCESS_METHODS 0x0004 /* Access Methods AMI */
#define KBC_JU_JET             0x0005 /* Ju-Jet */
/* OEM proprietary */
#define KBC_TANDY       0x0011 /* Tandy 1000/1000HX */
#define KBC_TANDY_SL2   0x0012 /* Tandy 1000SL2 */
#define KBC_AMSTRAD     0x0013 /* Amstrad */
#define KBC_OLIVETTI_XT 0x0014 /* Olivetti XT */
#define KBC_OLIVETTI    0x0015 /* Olivetti AT */
#define KBC_TOSHIBA     0x0016 /* Toshiba AT */
#define KBC_COMPAQ      0x0017 /* Compaq */
#define KBC_NCR         0x0018 /* NCR */
#define KBC_QUADTEL     0x0019 /* Quadtel */
#define KBC_SIEMENS     0x001A /* Siemens */
/* Phoenix MultiKey/42 */
#define PHOENIX_MK42_105  0x0521 /* Phoenix MultiKey/42 1.05 */
#define PHOENIX_MK42_129  0x2921 /* Phoenix MultiKey/42 1.29 */
#define PHOENIX_MK42_138  0x3821 /* Phoenix MultiKey/42 1.38 */
#define PHOENIX_MK42_140  0x3821 /* Phoenix MultiKey/42 1.40 */
#define PHOENIX_MKC42_214 0x1422 /* Phoenix MultiKey/C42 2.14 */
#define PHOENIX_MK42I_416 0x1624 /* Phoenix MultiKey/42i 4.16 */
#define PHOENIX_MK42I_419 0x1924 /* Phoenix MultiKey/42i 4.19 */
/* AMI 0x3x */
#define KBC_ACER_V30             0x0030 /* Acer (0xA1 returns nothing, 0xAF returns 0x00) */
#define KBC_AMI_MEGAKEY_SUPER_IO 0x0035 /* AMI '5' MegaKey 1994 NSC (and SM(S)C?) */
#define KBC_AMI_8                0x0038 /* AMI '8' */
/* AMI 0x4x */
#define KBC_AMI_B    0x0042 /* AMI 'B' */
#define KBC_AMI_D    0x0044 /* AMI 'D' */
#define KBC_AMI_E    0x0045 /* AMI 'E' */
#define KBC_AMIKEY   0x0046 /* AMI 'F'/AMIKEY */
#define KBC_AMIKEY_2 0x0048 /* AMI 'H'/AMIEY-2 */
#define KBC_MR       0x004D /* MR 'M' - Temporary classification until we get a dump */
/* AMI 0x5x */
#define KBC_AMI_MEGAKEY_1993 0x0050 /* AMI 'P' MegaKey 1993 */
#define KBC_AMI_MEGAKEY_1994 0x0052 /* AMI 'R' MegaKey 1994 - 0xA0 returns 1993 copyright */
#define KBC_AMI_TRIGEM       0x005A /* TriGem AMI 'Z' (1990 AMI copyright) */
/* AMI 0x6x */
#define KBC_TANDON 0x0061 /* Tandon 'a' - Temporary classification until we get a dump */
/* Holtek */
#define KBC_HT_REGIONAL_6542   0x1046 /* Holtek 'F' (Regional 6542) */
#define KBC_HT_HT6542B_BESTKEY 0x1048 /* Holtek 'H' (Holtek HT6542B, BestKey) */
/* AMI 0x0x clone without command 0xA0 */
#define KBC_UNK_00 0x2000 /* Unknown 0x00 */
#define KBC_UNK_01 0x2001 /* Unknown 0x01 */
/* AMI 0x3x clone without command 0xA0 */
#define KBC_UNK_7         0x2037 /* Unknown '7' - Temporary classification until we get a dump */
#define KBC_UNK_9         0x2037 /* Unknown '9' - Temporary classification until we get a dump */
#define KBC_JETKEY_NO_VER 0x2038 /* No-version JetKey '8' */
/* AMI 0x4x clone without command 0xA0 */
#define KBC_UNK_A           0x2041 /* Unknown 'A' - Temporary classification until we get a dump */
#define KBC_JETKEY_5_W83C42 0x2046 /* JetKey 5.0 'F' and Winbond W83C42 */
#define KBC_UNK_G           0x2047 /* Unknown 'G' - Temporary classification until we get a dump */
#define KBC_MB_300E_SIS     0x2048 /* MB-300E Non-VIA 'H' and SiS 5582/559x */
#define KBC_UNK_L           0x204C /* Unknown 'L' - Temporary classification until we get a dump */
/* AMI 0x0x clone with command 0xA0 (Get Copyright String) only returning 0x00 */
#define KBC_VPC_2007 0x3000 /* Microsoft Virtual PC 2007 - everything returns 0x00 */
/* AMI 0x4x clone with command 0xA0 (Get Copyright String) only returning 0x00 */
#define KBC_ALI_M148X   0x3045 /* ALi M148x 'E'/'U' (0xA1 actually returns 'F' but BIOS shows 'E' or 'U') */
#define KBC_LANCE_UTRON 0x3046 /* Lance LT38C41 'F', Utron */
/* AMI 0x5x clone with command 0xA0 (Get Copyright String) only returning 0x00 */
#define KBC_SARC_6042 0x3055 /* SARC 6042 'U' */
/* Award and clones */
#define KBC_AWARD 0x4200         /* Award (0xA1 returns 0x00) - Temporary classification until we get \
                                    the real 0xAF return */
#define KBC_VIA_VT82C4XN  0x4246 /* VIA VT82C41N, VT82C4N (0xA1 returns 'F') */
#define KBC_VIA_VT82C586A 0x4346 /* VIA VT82C586A (0xA1 returns 'F') */
#define KBC_VIA_VT82C586B 0x4446 /* VIA VT82C586B (0xA1 returns 'F') */
#define KBC_VIA_VT82C686B 0x4546 /* VIA VT82C686B (0xA1 returns 'F') */
/* UMC */
#define KBC_UMC_UM8886 0x5048 /* UMC UM8886 'H' */
/* IBM-style controllers with inverted P1 video type bit polarity */
#define KBC_IBM_TYPE_1_XI8088 0x8000 /* Xi8088: IBM Type 1 */
/* AMI (this is the 0xA1 revision byte) with inverted P1 video type bit polarity */
#define KBC_ACER_V30_INV 0x8030 /* Acer (0xA1 returns nothing, 0xAF returns 0x00) */
/* Holtek with inverted P1 video type bit polarity */
#define KBC_HT_HT6542B_XI8088 0x9048 /* Xi8088: Holtek 'H' (Holtek HT6542B, BestKey) */
/* Award and clones with inverted P1 video type bit polarity */
#define KBC_VIA_VT82C4XN_XI8088 0xC246 /* Xi8088: VIA VT82C41N, VT82C4N (0xA1 returns 'F') */

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t keyboard_mode;
extern int     keyboard_scan;

extern uint16_t scancode_map[768];

extern void (*keyboard_send)(uint16_t val);
extern void kbd_adddata_process(uint16_t val, void (*adddata)(uint16_t val));

extern const scancode scancode_xt[512];

extern uint8_t keyboard_set3_flags[512];
extern uint8_t keyboard_set3_all_repeat;
extern uint8_t keyboard_set3_all_break;
extern int     mouse_queue_start;
extern int     mouse_queue_end;
extern int     mouse_cmd_queue_start;
extern int     mouse_cmd_queue_end;
extern int     mouse_scan;

extern kbc_at_port_t     *kbc_at_ports[2];

#ifdef EMU_DEVICE_H
extern const device_t keyboard_pc_device;
extern const device_t keyboard_pc82_device;
extern const device_t keyboard_pravetz_device;
extern const device_t keyboard_xt_device;
extern const device_t keyboard_xt86_device;
extern const device_t keyboard_xt_compaq_device;
extern const device_t keyboard_xt_t1x00_device;
extern const device_t keyboard_tandy_device;
#    ifdef USE_LASERXT
extern const device_t keyboard_xt_lxt3_device;
#    endif /* USE_LASERXT */
extern const device_t keyboard_xt_olivetti_device;
extern const device_t keyboard_xt_zenith_device;
extern const device_t keyboard_xt_hyundai_device;
extern const device_t keyboard_xtclone_device;
extern const device_t keyboard_at_device;
extern const device_t keyboard_at_ami_device;
extern const device_t keyboard_at_compaq_device;
extern const device_t keyboard_at_ncr_device;
extern const device_t keyboard_at_olivetti_device;
extern const device_t keyboard_at_siemens_device;
extern const device_t keyboard_at_tg_ami_device;
extern const device_t keyboard_at_toshiba_device;
extern const device_t keyboard_ps2_device;
extern const device_t keyboard_ps2_ps1_device;
extern const device_t keyboard_ps2_ps1_pci_device;
extern const device_t keyboard_ps2_xi8088_device;
extern const device_t keyboard_ps2_ami_device;
extern const device_t keyboard_ps2_holtek_device;
extern const device_t keyboard_ps2_mca_1_device;
extern const device_t keyboard_ps2_mca_2_device;
extern const device_t keyboard_ps2_olivetti_device;
extern const device_t keyboard_ps2_phoenix_device;
extern const device_t keyboard_ps2_quadtel_device;
extern const device_t keyboard_ps2_tg_ami_device;
extern const device_t keyboard_ps2_tg_ami_green_device;
extern const device_t keyboard_ps2_pci_device;
extern const device_t keyboard_ps2_ami_pci_device;
extern const device_t keyboard_ps2_intel_ami_pci_device;
extern const device_t keyboard_ps2_acer_pci_device;
extern const device_t keyboard_ps2_ali_pci_device;
extern const device_t keyboard_ps2_tg_ami_pci_device;

extern const device_t keyboard_at_generic_device;
#endif /*EMU_DEVICE_H*/

extern void     keyboard_init(void);
extern void     keyboard_close(void);
extern void     keyboard_set_table(const scancode *ptr);
extern void     keyboard_poll_host(void);
extern void     keyboard_process(void);
extern uint16_t keyboard_convert(int ch);
extern void     keyboard_input(int down, uint16_t scan);
extern void     keyboard_update_states(uint8_t cl, uint8_t nl, uint8_t sl);
extern uint8_t  keyboard_get_shift(void);
extern void     keyboard_get_states(uint8_t *cl, uint8_t *nl, uint8_t *sl);
extern void     keyboard_set_states(uint8_t cl, uint8_t nl, uint8_t sl);
extern int      keyboard_recv(uint16_t key);
extern int      keyboard_recv_ui(uint16_t key);
extern int      keyboard_isfsenter(void);
extern int      keyboard_isfsenter_up(void);
extern int      keyboard_isfsexit(void);
extern int      keyboard_isfsexit_up(void);
extern int      keyboard_ismsexit(void);
extern void     keyboard_set_is_amstrad(int ams);
extern void     kbc_at_set_ps2(void *priv, uint8_t ps2);

extern void         kbc_at_set_fast_reset(uint8_t new_fast_reset);
extern void         kbc_at_handler(int set, void *priv);

extern uint8_t      kbc_at_dev_queue_pos(atkbc_dev_t *dev, uint8_t main);
extern void         kbc_at_dev_queue_add(atkbc_dev_t *dev, uint8_t val, uint8_t main);
extern void         kbc_at_dev_reset(atkbc_dev_t *dev, int do_fa);
extern atkbc_dev_t *kbc_at_dev_init(uint8_t inst);
/* This is so we can disambiguate scan codes that would otherwise conflict and get
   passed on incorrectly. */
extern uint16_t     convert_scan_code(uint16_t scan_code);

#ifdef __cplusplus
}
#endif

#endif /*EMU_KEYBOARD_H*/
