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
 * Authors: Sarah Walker, <http://pcem-emulator.co.uk/>
 *          Miran Grca, <mgrca8@gmail.com>
 *          Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2008-2019 Sarah Walker.
 *          Copyright 2016-2019 Miran Grca.
 *          Copyright 2017-2019 Fred N. van Kempen.
 */

#ifndef EMU_KEYBOARD_H
#define EMU_KEYBOARD_H

typedef struct {
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
#define KBC_UNKNOWN 0x0000 /* As yet unknown keyboard */

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

extern void (*keyboard_send)(uint16_t val);
extern void kbd_adddata_process(uint16_t val, void (*adddata)(uint16_t val));

extern const scancode scancode_xt[512];

extern uint8_t keyboard_set3_flags[512];
extern uint8_t keyboard_set3_all_repeat;
extern uint8_t keyboard_set3_all_break;
extern int     mouse_queue_start, mouse_queue_end;
extern int     mouse_scan;

#ifdef EMU_DEVICE_H
extern const device_t keyboard_pc_device;
extern const device_t keyboard_pc82_device;
extern const device_t keyboard_pravetz_device;
extern const device_t keyboard_xt_device;
extern const device_t keyboard_xt86_device;
extern const device_t keyboard_xt_compaq_device;
extern const device_t keyboard_tandy_device;
#    if defined(DEV_BRANCH) && defined(USE_LASERXT)
extern const device_t keyboard_xt_lxt3_device;
#    endif
extern const device_t keyboard_xt_olivetti_device;
extern const device_t keyboard_xt_zenith_device;
extern const device_t keyboard_xtclone_device;
extern const device_t keyboard_at_device;
extern const device_t keyboard_at_ami_device;
extern const device_t keyboard_at_samsung_device;
extern const device_t keyboard_at_toshiba_device;
extern const device_t keyboard_at_olivetti_device;
extern const device_t keyboard_at_ncr_device;
extern const device_t keyboard_ps2_device;
extern const device_t keyboard_ps2_ps1_device;
extern const device_t keyboard_ps2_ps1_pci_device;
extern const device_t keyboard_ps2_xi8088_device;
extern const device_t keyboard_ps2_ami_device;
extern const device_t keyboard_ps2_olivetti_device;
extern const device_t keyboard_ps2_mca_device;
extern const device_t keyboard_ps2_mca_2_device;
extern const device_t keyboard_ps2_quadtel_device;
extern const device_t keyboard_ps2_pci_device;
extern const device_t keyboard_ps2_ami_pci_device;
extern const device_t keyboard_ps2_intel_ami_pci_device;
extern const device_t keyboard_ps2_acer_pci_device;
extern const device_t keyboard_ps2_ali_pci_device;
#endif

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
extern int      keyboard_isfsexit(void);
extern int      keyboard_ismsexit(void);
extern void     keyboard_set_is_amstrad(int ams);

extern void    keyboard_at_adddata_mouse(uint8_t val);
extern void    keyboard_at_adddata_mouse_direct(uint8_t val);
extern void    keyboard_at_adddata_mouse_cmd(uint8_t val);
extern void    keyboard_at_mouse_reset(void);
extern uint8_t keyboard_at_mouse_pos(void);
extern int     keyboard_at_fixed_channel(void);
extern void    keyboard_at_set_mouse(void (*mouse_write)(uint8_t val, void *), void *);
extern void    keyboard_at_set_a20_key(int state);
extern void    keyboard_at_set_mode(int ps2);
extern uint8_t keyboard_at_get_mouse_scan(void);
extern void    keyboard_at_set_mouse_scan(uint8_t val);
extern void    keyboard_at_reset(void);

#ifdef __cplusplus
}
#endif

#endif /*EMU_KEYBOARD_H*/
